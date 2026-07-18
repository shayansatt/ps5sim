#include "ps5SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

constexpr char PAGE_STYLE[] = R"(
QComboBox, QSpinBox, QLineEdit {
	background: rgba(255, 255, 255, 14);
	border: 1px solid rgba(255, 255, 255, 40);
	border-radius: 6px;
	color: white;
	padding: 6px 10px;
	min-width: 220px;
}
QComboBox:focus, QSpinBox:focus, QLineEdit:focus {
	border: 2px solid white;
}
QComboBox QAbstractItemView {
	background: #1b2431;
	color: white;
	selection-background-color: #2f81f7;
}
QCheckBox { color: white; spacing: 10px; }
QCheckBox::indicator {
	width: 22px; height: 22px;
	border: 1px solid rgba(255, 255, 255, 90);
	border-radius: 11px;
	background: transparent;
}
QCheckBox::indicator:checked { background: #2f81f7; border-color: #2f81f7; }
QCheckBox::indicator:focus { border: 2px solid white; }
QLabel { color: white; }
QPushButton {
	background: rgba(255, 255, 255, 18);
	border: none; border-radius: 16px;
	color: white; padding: 8px 22px;
}
QPushButton:hover, QPushButton:focus { background: white; color: black; }
QListWidget {
	background: transparent; border: none; color: white; outline: none;
}
QListWidget::item {
	background: transparent;
	border: none;
	border-bottom: 1px solid rgba(255, 255, 255, 26);
	border-radius: 0px;
	padding: 15px 18px;
	margin: 2px 0;
}
QListWidget::item:hover {
	background: rgba(255, 255, 255, 18);
	border-bottom: 1px solid transparent;
	border-radius: 10px;
}
QListWidget::item:selected, QListWidget::item:selected:active, QListWidget::item:selected:!active {
	background: rgba(255, 255, 255, 235);
	color: black;
	border: none;
	border-radius: 10px;
}
)";

QLabel* SectionLabel(const QString& text) {
	auto* l = new QLabel(text);
	auto  f = l->font();
	f.setPointSize(13);
	f.setBold(true);
	l->setFont(f);
	l->setStyleSheet(QStringLiteral("color: rgba(255,255,255,160); margin-top: 8px;"));
	return l;
}

} // namespace

Ps5SettingsDialog::Ps5SettingsDialog(Configuration& info, QWidget* parent)
    : QDialog(parent), m_info(info) {
	setWindowTitle(tr("Settings"));
	setModal(true);
	resize(1180, 720);
	m_background.load(QStringLiteral(":/ps5/settings_bg.jpg"));
	setStyleSheet(QString::fromLatin1(PAGE_STYLE));

	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(48, 32, 48, 24);
	root->setSpacing(18);

	auto* title = new QLabel(tr("Settings"));
	{
		auto f = title->font();
		f.setPointSize(30);
		f.setWeight(QFont::Normal);
		title->setFont(f);
		title->setStyleSheet(QStringLiteral("color: rgba(255,255,255,235);"));
	}
	root->addWidget(title);
	root->addSpacing(10);

	auto* body = new QHBoxLayout();
	body->setSpacing(28);
	root->addLayout(body, 1);

	m_categories = new QListWidget();
	m_categories->setFixedWidth(360);
	m_categories->setFocusPolicy(Qt::StrongFocus);
	m_categories->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_categories->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	{
		auto f = m_categories->font();
		f.setPointSize(13);
		m_categories->setFont(f);
	}
	body->addWidget(m_categories);

	m_pages = new QStackedWidget();
	body->addWidget(m_pages, 1);

	struct Page {
		QChar    glyph;
		QString  name;
		QWidget* widget;
	};
	const Page pages[] = {
	    {QChar(0xE7F4), tr("Screen and Video"), BuildScreenPage()},
	    {QChar(0xE767), tr("Sound"), BuildSoundPage()},
	    {QChar(0xE7FC), tr("Graphics"), BuildGraphicsPage()},
	    {QChar(0xE770), tr("System"), BuildSystemPage()},
	    {QChar(0xEDA2), tr("Storage"), BuildStoragePage()},
	};
	const QFont glyph_font(QStringLiteral("Segoe Fluent Icons"), 13);
	for (const auto& p: pages) {
		auto* item = new QListWidgetItem(QString(p.glyph) + QStringLiteral("   ") + p.name);
		item->setFont(glyph_font);
		m_categories->addItem(item);
		auto* scroll = new QScrollArea();
		scroll->setWidgetResizable(true);
		scroll->setFrameShape(QFrame::NoFrame);
		scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
		p.widget->setStyleSheet(QStringLiteral("background: transparent;"));
		scroll->setWidget(p.widget);
		m_pages->addWidget(scroll);
	}
	connect(m_categories, &QListWidget::currentRowChanged, m_pages,
	        &QStackedWidget::setCurrentIndex);
	m_categories->setCurrentRow(0);

	auto* buttons = new QHBoxLayout();
	buttons->addStretch(1);
	auto* ok = new QPushButton(tr("OK"));
	ok->setDefault(true);
	connect(ok, &QPushButton::clicked, this, [this]() {
		Apply();
		accept();
	});
	auto* cancel = new QPushButton(tr("Cancel"));
	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
	buttons->addWidget(ok);
	buttons->addSpacing(8);
	buttons->addWidget(cancel);
	root->addLayout(buttons);
}

QWidget* Ps5SettingsDialog::BuildCategoryPage(const QString& title, QWidget* content) {
	auto* page   = new QWidget();
	auto* layout = new QVBoxLayout(page);
	layout->setContentsMargins(4, 0, 4, 0);
	layout->setSpacing(14);
	auto* header = new QLabel(title);
	{
		auto f = header->font();
		f.setPointSize(17);
		f.setBold(true);
		header->setFont(f);
	}
	layout->addWidget(header);
	layout->addWidget(content, 1);
	return page;
}

QWidget* Ps5SettingsDialog::BuildScreenPage() {
	auto* content = new QWidget();
	auto* form    = new QFormLayout(content);
	form->setVerticalSpacing(16);
	form->setLabelAlignment(Qt::AlignLeft);

	m_resolution = new QComboBox();
	m_resolution->addItems(EnumToList<Configuration::Resolution>());
	m_resolution->setCurrentText(EnumToText(m_info.screen_resolution));
	form->addRow(tr("Resolution"), m_resolution);

	m_vblank = new QSpinBox();
	m_vblank->setRange(1, 300);
	m_vblank->setSuffix(tr(" Hz"));
	m_vblank->setValue(m_info.vblank_frequency);
	form->addRow(tr("VBlank Frequency"), m_vblank);

	return BuildCategoryPage(tr("Screen and Video"), content);
}

QWidget* Ps5SettingsDialog::BuildSoundPage() {
	auto* content = new QWidget();
	auto* layout  = new QVBoxLayout(content);
	layout->setSpacing(12);

	m_home_music = new QCheckBox(tr("Home Screen Music"));
	m_home_music->setChecked(m_info.home_music_enabled);
	layout->addWidget(m_home_music);

	auto* hint = new QLabel(tr("Plays the PS5 home menu music in the background after boot."));
	hint->setStyleSheet(QStringLiteral("color: rgba(255,255,255,140); font-size: 12px;"));
	layout->addWidget(hint);

	layout->addStretch(1);
	return BuildCategoryPage(tr("Sound"), content);
}

QWidget* Ps5SettingsDialog::BuildGraphicsPage() {
	auto* content = new QWidget();
	auto* layout  = new QVBoxLayout(content);
	layout->setSpacing(12);

	auto* form = new QFormLayout();
	form->setVerticalSpacing(16);
	m_shader_opt = new QComboBox();
	m_shader_opt->addItems(EnumToList<Configuration::ShaderOptimizationType>());
	m_shader_opt->setCurrentText(EnumToText(m_info.shader_optimization_type));
	form->addRow(tr("Shader Optimization"), m_shader_opt);
	layout->addLayout(form);

	m_shader_val = new QCheckBox(tr("Shader Validation"));
	m_shader_val->setChecked(m_info.shader_validation_enabled);
	layout->addWidget(m_shader_val);

	m_vulkan_val = new QCheckBox(tr("Vulkan Validation Layers"));
	m_vulkan_val->setChecked(m_info.vulkan_validation_enabled);
	layout->addWidget(m_vulkan_val);

	m_ngg_rectlist = new QCheckBox(tr("NGG Rectlist Draw"));
	m_ngg_rectlist->setChecked(m_info.ngg_rectlist_draw_enabled);
	layout->addWidget(m_ngg_rectlist);

	m_renderdoc = new QCheckBox(tr("RenderDoc Capture Support"));
	m_renderdoc->setChecked(m_info.renderdoc_enabled);
	layout->addWidget(m_renderdoc);

	layout->addWidget(SectionLabel(tr("Shader Log")));
	auto* log_form = new QFormLayout();
	log_form->setVerticalSpacing(16);
	m_shader_log = new QComboBox();
	m_shader_log->addItems(EnumToList<Configuration::ShaderLogDirection>());
	m_shader_log->setCurrentText(EnumToText(m_info.shader_log_direction));
	log_form->addRow(tr("Direction"), m_shader_log);
	m_shader_log_folder = new QLineEdit(m_info.shader_log_folder);
	log_form->addRow(tr("Folder"), m_shader_log_folder);
	layout->addLayout(log_form);

	m_cb_dump = new QCheckBox(tr("Dump Command Buffers"));
	m_cb_dump->setChecked(m_info.command_buffer_dump_enabled);
	layout->addWidget(m_cb_dump);
	auto* dump_form = new QFormLayout();
	m_cb_dump_folder = new QLineEdit(m_info.command_buffer_dump_folder);
	dump_form->addRow(tr("Dump Folder"), m_cb_dump_folder);
	layout->addLayout(dump_form);

	layout->addStretch(1);
	return BuildCategoryPage(tr("Graphics"), content);
}

QWidget* Ps5SettingsDialog::BuildSystemPage() {
	auto* content = new QWidget();
	auto* layout  = new QVBoxLayout(content);
	layout->setSpacing(12);

	layout->addWidget(SectionLabel(tr("Console Output (printf)")));
	auto* form = new QFormLayout();
	form->setVerticalSpacing(16);
	m_printf_dir = new QComboBox();
	m_printf_dir->addItems(EnumToList<Configuration::LogDirection>());
	m_printf_dir->setCurrentText(EnumToText(m_info.printf_direction));
	form->addRow(tr("Direction"), m_printf_dir);
	m_printf_file = new QLineEdit(m_info.printf_output_file);
	form->addRow(tr("Output File"), m_printf_file);
	layout->addLayout(form);

	layout->addWidget(SectionLabel(tr("Profiler")));
	auto* prof_form = new QFormLayout();
	m_profiler = new QComboBox();
	m_profiler->addItems(EnumToList<Configuration::ProfilerDirection>());
	m_profiler->setCurrentText(EnumToText(m_info.profiler_direction));
	prof_form->addRow(tr("Direction"), m_profiler);
	layout->addLayout(prof_form);

	layout->addStretch(1);
	return BuildCategoryPage(tr("System"), content);
}

QWidget* Ps5SettingsDialog::BuildStoragePage() {
	auto* content = new QWidget();
	auto* layout  = new QVBoxLayout(content);
	layout->setSpacing(12);

	layout->addWidget(SectionLabel(tr("Game Directories")));
	m_game_dirs = new QListWidget();
	m_game_dirs->setMinimumHeight(220);
	layout->addWidget(m_game_dirs, 1);

	auto* buttons = new QHBoxLayout();
	auto* add     = new QPushButton(tr("Add..."));
	connect(add, &QPushButton::clicked, this, [this]() {
		const auto dir = QFileDialog::getExistingDirectory(this, tr("Select game directory"));
		if (!dir.isEmpty()) {
			m_game_dirs->addItem(QDir::toNativeSeparators(dir));
		}
	});
	auto* remove = new QPushButton(tr("Remove"));
	connect(remove, &QPushButton::clicked, this,
	        [this]() { delete m_game_dirs->currentItem(); });
	buttons->addWidget(add);
	buttons->addSpacing(8);
	buttons->addWidget(remove);
	buttons->addStretch(1);
	layout->addLayout(buttons);

	return BuildCategoryPage(tr("Storage"), content);
}

void Ps5SettingsDialog::SetGameDirectories(const QStringList& dirs) {
	m_game_dirs->clear();
	for (const auto& dir: dirs) {
		m_game_dirs->addItem(dir);
	}
}

QStringList Ps5SettingsDialog::GetGameDirectories() const {
	QStringList result;
	for (int i = 0; i < m_game_dirs->count(); i++) {
		result << m_game_dirs->item(i)->text();
	}
	return result;
}

void Ps5SettingsDialog::Apply() {
	m_info.screen_resolution =
	    TextToEnum<Configuration::Resolution>(m_resolution->currentText());
	m_info.vblank_frequency          = m_vblank->value();
	m_info.home_music_enabled        = m_home_music->isChecked();
	m_info.shader_optimization_type =
	    TextToEnum<Configuration::ShaderOptimizationType>(m_shader_opt->currentText());
	m_info.shader_validation_enabled = m_shader_val->isChecked();
	m_info.vulkan_validation_enabled = m_vulkan_val->isChecked();
	m_info.ngg_rectlist_draw_enabled = m_ngg_rectlist->isChecked();
	m_info.renderdoc_enabled         = m_renderdoc->isChecked();
	m_info.shader_log_direction =
	    TextToEnum<Configuration::ShaderLogDirection>(m_shader_log->currentText());
	m_info.shader_log_folder            = m_shader_log_folder->text();
	m_info.command_buffer_dump_enabled  = m_cb_dump->isChecked();
	m_info.command_buffer_dump_folder   = m_cb_dump_folder->text();
	m_info.printf_direction =
	    TextToEnum<Configuration::LogDirection>(m_printf_dir->currentText());
	m_info.printf_output_file = m_printf_file->text();
	m_info.profiler_direction =
	    TextToEnum<Configuration::ProfilerDirection>(m_profiler->currentText());
}

void Ps5SettingsDialog::paintEvent(QPaintEvent* event) {
	QPainter painter(this);
	if (m_background.isNull()) {
		painter.fillRect(rect(), QColor(0x10, 0x18, 0x20));
	} else {
		// Scale to cover the dialog, keeping aspect ratio and centering the overflow.
		const auto scaled = m_background.scaled(size(), Qt::KeepAspectRatioByExpanding,
		                                        Qt::SmoothTransformation);
		const int  x      = (scaled.width() - width()) / 2;
		const int  y      = (scaled.height() - height()) / 2;
		painter.drawPixmap(0, 0, scaled, x, y, width(), height());
	}
	Q_UNUSED(event);
}

void Ps5SettingsDialog::keyPressEvent(QKeyEvent* event) {
	if (event->key() == Qt::Key_Escape) {
		reject();
		return;
	}
	QDialog::keyPressEvent(event);
}
