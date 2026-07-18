#include "ps5HomeScreen.h"

#include "configuration.h"
#include "configurationItem.h"
#include "configurationListWidget.h"
#include "trophyViewerDialog.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// Tile sizes follow the real PS5 dashboard proportions: small square tiles,
// with the focused one noticeably larger and ringed in white.
constexpr int TILE_W       = 96;
constexpr int TILE_FOCUS_W = 128;
constexpr int TILE_FOCUS_H = 128;
constexpr int TILE_SPACING = 14;

QString StatusText(Configuration::GameStatus status) {
	switch (status) {
		case Configuration::GameStatus::InGame: return QStringLiteral("Playable");
		case Configuration::GameStatus::MainMenu: return QStringLiteral("Reaches menu");
		case Configuration::GameStatus::Logo: return QStringLiteral("Boots to logo");
		case Configuration::GameStatus::DoesntBoot: return QStringLiteral("Does not boot");
		case Configuration::GameStatus::Unknown: return QStringLiteral("Untested");
	}
	return QStringLiteral("Untested");
}

QString StatusColor(Configuration::GameStatus status) {
	switch (status) {
		case Configuration::GameStatus::InGame: return QStringLiteral("#3ad07a");
		case Configuration::GameStatus::MainMenu: return QStringLiteral("#7ec8ff");
		case Configuration::GameStatus::Logo: return QStringLiteral("#ffd85e");
		case Configuration::GameStatus::DoesntBoot: return QStringLiteral("#ff6b6b");
		case Configuration::GameStatus::Unknown: return QStringLiteral("#9aa7bd");
	}
	return QStringLiteral("#9aa7bd");
}

QPixmap LoadIcon(const Configuration& info, int size) {
	QPixmap pm;
	if (!info.basedir.isEmpty()) {
		const QString icon = QDir(info.basedir).filePath(QStringLiteral("sce_sys/icon0.png"));
		if (QFileInfo::exists(icon)) {
			pm.load(icon);
		}
	}
	if (pm.isNull()) {
		pm = QPixmap(size, size);
		pm.fill(QColor(40, 48, 66));
	}
	return pm.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
}

// Render a source pixmap into a transparent, rounded-corner pixmap. Doing the
// rounding here (instead of a live clip path at paint time) keeps the corners
// clean and antialiased.
QPixmap RoundPixmap(const QPixmap& src, int side, int radius) {
	QPixmap out(side, side);
	out.fill(Qt::transparent);

	QPainter p(&out);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	QPainterPath path;
	path.addRoundedRect(0, 0, side, side, radius, radius);
	p.setClipPath(path);
	p.drawPixmap(0, 0, src.copy((src.width() - side) / 2, (src.height() - side) / 2, side, side));
	return out;
}

} // namespace

// A single game tile: rounded icon that grows and gains a glow ring when focused.
class GameTile: public QWidget {
public:
	GameTile(ConfigurationItem* item, QWidget* parent): QWidget(parent), m_item(item) {
		// Widget is padded past the focused size so the white focus ring
		// (drawn outside the tile) is not clipped.
		setFixedSize(TILE_FOCUS_W + 16, TILE_FOCUS_H + 16);
		setAttribute(Qt::WA_Hover, true);
	}

	void SetFocused(bool focused) {
		if (m_focused != focused) {
			m_focused = focused;
			update();
		}
	}

	[[nodiscard]] ConfigurationItem* Item() const { return m_item; }

protected:
	void paintEvent(QPaintEvent* /*event*/) override {
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::SmoothPixmapTransform);

		const int  side   = m_focused ? TILE_FOCUS_W : TILE_W;
		const int  radius = side * 22 / 100; // PS5-like strongly rounded corners
		const QRect r((width() - side) / 2, (height() - side) / 2, side, side);

		p.drawPixmap(r.topLeft(), RoundPixmap(LoadIcon(m_item->GetInfo(), side), side, radius));

		if (m_focused) {
			// Real PS5 focus: a thin white ring floating just outside the tile.
			p.setBrush(Qt::NoBrush);
			p.setPen(QPen(QColor(255, 255, 255, 235), 3));
			p.drawRoundedRect(r.adjusted(-5, -5, 5, 5), radius + 5, radius + 5);
		}

		if (m_item->IsRunning()) {
			p.setBrush(QColor(58, 208, 122, 220));
			p.setPen(Qt::NoPen);
			p.drawEllipse(r.right() - 16, r.top() + 6, 10, 10);
		}
	}

private:
	ConfigurationItem* m_item    = nullptr;
	bool               m_focused = false;
};

// Small square system tile (PS Store / Explore / PS Plus / Library) shown in the
// tile row, painted as a dark navy rounded square with its official-style SVG icon.
class SystemTile: public QWidget {
public:
	SystemTile(const QString& svg_path, const QString& tip, QWidget* parent)
	    : QWidget(parent), m_icon(svg_path) {
		setFixedSize(TILE_W, TILE_FOCUS_H + 16);
		setToolTip(tip);
	}

protected:
	void paintEvent(QPaintEvent* /*event*/) override {
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::SmoothPixmapTransform);

		const int radius = TILE_W * 22 / 100;
		const QRect r(0, (height() - TILE_W) / 2, TILE_W, TILE_W);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(20, 34, 62, 235));
		p.drawRoundedRect(r, radius, radius);

		const int     icon_side = TILE_W * 45 / 100;
		const QPixmap pm        = m_icon.pixmap(icon_side, icon_side);
		const QSize   ps        = pm.deviceIndependentSize().toSize();
		p.drawPixmap(r.center().x() - ps.width() / 2, r.center().y() - ps.height() / 2, pm);
	}

private:
	QIcon m_icon;
};

Ps5HomeScreen::Ps5HomeScreen(ConfigurationListWidget* backend, QWidget* parent)
    : QWidget(parent), m_backend(backend) {
	setFocusPolicy(Qt::StrongFocus);
	setAutoFillBackground(false);

	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(56, 28, 56, 0);
	root->setSpacing(0);

	// Top function bar: "Games / Media" tabs on the left; search / settings glyphs,
	// profile avatar and the clock on the right (PS5 dashboard style).
	auto* top = new QHBoxLayout();
	top->setSpacing(0);

	auto* tab_games = new QLabel(tr("Games"), this);
	tab_games->setStyleSheet(
	    "color: #ffffff; font-size: 21px; font-weight: 700; letter-spacing: 1px;");
	auto* tab_media = new QLabel(tr("Media"), this);
	tab_media->setStyleSheet(
	    "color: rgba(230,238,252,110); font-size: 21px; font-weight: 600; letter-spacing: 1px;");
	top->addWidget(tab_games, 0, Qt::AlignVCenter);
	top->addSpacing(26);
	top->addWidget(tab_media, 0, Qt::AlignVCenter);
	top->addStretch(1);

	// Right-side function glyphs as flat icon buttons. Segoe Fluent Icons /
	// Segoe MDL2 Assets ship with Windows and expose these in the private-use area,
	// so they render reliably. Build via QChar (BMP code points) to avoid any UTF-8
	// literal decoding issues.
	const auto make_glyph = [this](ushort code, const QString& tip) {
		auto* g = new QPushButton(QString(QChar(code)), this);
		g->setToolTip(tip);
		g->setCursor(Qt::PointingHandCursor);
		g->setFocusPolicy(Qt::NoFocus);
		g->setFixedSize(38, 38);
		g->setStyleSheet(
		    "QPushButton { color: rgba(230,238,252,215); background: transparent; border: none; "
		    "font-family: 'Segoe Fluent Icons','Segoe MDL2 Assets'; font-size: 17px; }"
		    "QPushButton:hover { color: #ffffff; background: rgba(255,255,255,28); "
		    "border-radius: 19px; }"
		    "QPushButton:pressed { background: rgba(255,255,255,45); }");
		return g;
	};
	top->addWidget(make_glyph(0xE721, tr("Search")), 0, Qt::AlignVCenter); // search

	top->addSpacing(10);
	auto* gear_btn = make_glyph(0xE713, tr("Settings (S)")); // gear
	connect(gear_btn, &QPushButton::clicked, this, &Ps5HomeScreen::OpenSettingsForFocused);
	top->addWidget(gear_btn, 0, Qt::AlignVCenter);

	top->addSpacing(4);
	auto* refresh_btn = make_glyph(0xE72C, tr("Rescan (F5)")); // refresh, next to the gear
	connect(refresh_btn, &QPushButton::clicked, this,
	        [this]() { m_backend->ScanGameDirectory(); });
	top->addWidget(refresh_btn, 0, Qt::AlignVCenter);

	top->addSpacing(4);
	m_fullscreen_btn = make_glyph(0xE740, tr("Fullscreen (F11)")); // expand
	connect(m_fullscreen_btn, &QPushButton::clicked, this, &Ps5HomeScreen::ToggleFullscreen);
	top->addWidget(m_fullscreen_btn, 0, Qt::AlignVCenter);
	top->addSpacing(16);

	// Profile avatar: a small filled circle drawn via stylesheet.
	auto* avatar = new QLabel(this);
	avatar->setFixedSize(34, 34);
	avatar->setStyleSheet(
	    "background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #4a7bd0, stop:1 #7ec8ff); "
	    "border-radius: 17px;");
	top->addWidget(avatar, 0, Qt::AlignVCenter);
	top->addSpacing(20);

	m_clock = new QLabel(this);
	m_clock->setStyleSheet("color: #e6eefc; font-size: 17px; font-weight: 600;");
	top->addWidget(m_clock, 0, Qt::AlignVCenter);

	root->addLayout(top);

	root->addSpacing(34);

	// Tile strip sits near the TOP, right under the tabs (PS5 layout).
	m_scroll = new QScrollArea(this);
	m_scroll->setWidgetResizable(true);
	m_scroll->setFrameShape(QFrame::NoFrame);
	m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_scroll->setFixedHeight(TILE_FOCUS_H + 32);
	m_scroll->setStyleSheet("background: transparent;");
	m_scroll->setFocusPolicy(Qt::NoFocus);

	auto* strip = new QWidget(m_scroll);
	strip->setStyleSheet("background: transparent;");
	m_tiles_layout = new QHBoxLayout(strip);
	m_tiles_layout->setContentsMargins(4, 8, 4, 8);
	m_tiles_layout->setSpacing(TILE_SPACING);
	// System tiles arranged like the real dashboard: Store + Explore lead the row,
	// PS Plus + Library trail after the game tiles.
	m_tiles_layout->addWidget(new SystemTile(QStringLiteral(":/ps5/store.svg"), tr("Store"), strip));
	m_tiles_layout->addWidget(
	    new SystemTile(QStringLiteral(":/ps5/explore.svg"), tr("Explore"), strip));
	m_tiles_layout->addWidget(
	    new SystemTile(QStringLiteral(":/ps5/psplus.svg"), tr("PlayStation Plus"), strip));
	m_tiles_layout->addWidget(
	    new SystemTile(QStringLiteral(":/ps5/library.svg"), tr("Game Library"), strip));
	m_tiles_layout->addStretch(1);
	m_scroll->setWidget(strip);
	root->addWidget(m_scroll);

	// The hero art shows through this gap.
	root->addStretch(1);

	// Hero panel sits at the BOTTOM-LEFT: game name, details, and a Play button.
	m_hero_name = new QLabel(this);
	m_hero_name->setStyleSheet(
	    "color: #ffffff; font-size: 46px; font-weight: 800; letter-spacing: 1px;");
	m_hero_meta = new QLabel(this);
	m_hero_meta->setStyleSheet("color: rgba(220,230,246,190); font-size: 15px;");
	m_hero_status = new QLabel(this);
	m_hero_status->setStyleSheet("font-size: 15px; font-weight: 600;");

	root->addWidget(m_hero_name);
	root->addSpacing(6);
	root->addWidget(m_hero_meta);
	root->addSpacing(4);
	root->addWidget(m_hero_status);

	m_empty_hint = new QLabel(
	    tr("No games found.\nAdd game folders in settings (S), then rescan (F5)."), this);
	m_empty_hint->setStyleSheet("color: rgba(230,238,252,150); font-size: 20px;");
	root->addWidget(m_empty_hint);

	root->addSpacing(18);

	// "Play Game" pill button, PS5-style.
	m_play_btn = new QPushButton(tr("Play Game"), this);
	m_play_btn->setCursor(Qt::PointingHandCursor);
	m_play_btn->setFixedSize(230, 50);
	m_play_btn->setStyleSheet(
	    "QPushButton { background: #f2f5fb; color: #10182e; border: none; border-radius: 25px; "
	    "font-size: 17px; font-weight: 700; }"
	    "QPushButton:hover { background: #ffffff; }"
	    "QPushButton:disabled { background: rgba(210,220,236,60); color: rgba(255,255,255,90); }");
	connect(m_play_btn, &QPushButton::clicked, this, &Ps5HomeScreen::LaunchFocused);

	// Trophy card (bottom-right, PS5 style): Progress / Earned columns like the
	// real dashboard, with the per-grade breakdown underneath.
	m_trophy_card = new QFrame(this);
	m_trophy_card->setStyleSheet(
	    "QFrame { background: rgba(10,18,36,150); border-radius: 10px; }");
	auto* card_layout = new QVBoxLayout(m_trophy_card);
	card_layout->setContentsMargins(24, 12, 24, 12);
	card_layout->setSpacing(2);

	auto* card_heads = new QHBoxLayout();
	card_heads->setSpacing(40);
	auto* progress_head = new QLabel(tr("Progress"), m_trophy_card);
	auto* earned_head   = new QLabel(tr("Earned"), m_trophy_card);
	const QString head_style =
	    "color: rgba(220,230,246,150); font-size: 12px; background: transparent;";
	progress_head->setStyleSheet(head_style);
	earned_head->setStyleSheet(head_style);
	card_heads->addWidget(progress_head);
	card_heads->addWidget(earned_head);
	card_heads->addStretch(1);
	card_layout->addLayout(card_heads);

	auto* card_values = new QHBoxLayout();
	card_values->setSpacing(40);
	m_trophy_progress = new QLabel(m_trophy_card);
	m_trophy_earned   = new QLabel(m_trophy_card);
	const QString value_style =
	    "color: #ffffff; font-size: 18px; font-weight: 700; background: transparent;";
	m_trophy_progress->setStyleSheet(value_style);
	m_trophy_earned->setStyleSheet(value_style);
	card_values->addWidget(m_trophy_progress);
	card_values->addWidget(m_trophy_earned);
	card_values->addStretch(1);
	card_layout->addLayout(card_values);

	// Per-grade row: official-style trophy icons with counts, like the console.
	auto* grades_row = new QHBoxLayout();
	grades_row->setSpacing(6);
	const auto make_trophy_pair = [this](const QString& img, QLabel*& count_out) {
		auto* icon = new QLabel(m_trophy_card);
		icon->setStyleSheet("background: transparent;");
		icon->setPixmap(QPixmap(img).scaled(16, 21, Qt::KeepAspectRatio,
		                                    Qt::SmoothTransformation));
		auto* count = new QLabel(m_trophy_card);
		count->setStyleSheet(
		    "color: rgba(220,230,246,200); font-size: 14px; background: transparent;");
		count_out = count;
		auto* pair = new QHBoxLayout();
		pair->setSpacing(5);
		pair->addWidget(icon);
		pair->addWidget(count);
		return pair;
	};
	grades_row->addLayout(
	    make_trophy_pair(QStringLiteral(":/ps5/trophy_gold.png"), m_trophy_gold));
	grades_row->addSpacing(18);
	grades_row->addLayout(
	    make_trophy_pair(QStringLiteral(":/ps5/trophy_silver.png"), m_trophy_silver));
	grades_row->addSpacing(18);
	grades_row->addLayout(
	    make_trophy_pair(QStringLiteral(":/ps5/trophy_bronze.png"), m_trophy_bronze));
	grades_row->addStretch(1);
	card_layout->addSpacing(6);
	card_layout->addLayout(grades_row);

	auto* btn_row = new QHBoxLayout();
	btn_row->setContentsMargins(0, 0, 0, 0);
	btn_row->addWidget(m_play_btn);
	btn_row->addStretch(1);
	btn_row->addWidget(m_trophy_card, 0, Qt::AlignBottom);
	root->addLayout(btn_row);

	root->addSpacing(30);

	m_clock_timer = new QTimer(this);
	m_clock_timer->setInterval(1000);
	connect(m_clock_timer, &QTimer::timeout, this, [this]() {
		m_clock->setText(QDateTime::currentDateTime().toString(QStringLiteral("h:mm AP")));
		// While the focused game is running, refresh the hero panel periodically
		// so trophy unlocks written by the emulator show up live.
		if (++m_hero_refresh_tick >= 5) {
			m_hero_refresh_tick = 0;
			if (m_focus >= 0 && m_focus < m_items.size() && m_items[m_focus]->IsRunning()) {
				UpdateHero();
			}
		}
	});
	m_clock_timer->start();
	m_clock->setText(QDateTime::currentDateTime().toString(QStringLiteral("h:mm AP")));

	connect(m_backend, &ConfigurationListWidget::Scanned, this, &Ps5HomeScreen::RebuildTiles);

	RebuildTiles();
}

Ps5HomeScreen::~Ps5HomeScreen() = default;

void Ps5HomeScreen::RebuildTiles() {
	for (auto* tile: m_tiles) {
		m_tiles_layout->removeWidget(tile);
		tile->deleteLater();
	}
	m_tiles.clear();

	m_items = m_backend->GetItems();

	int insert_at = 2; // between the leading (Store, Explore) and trailing system tiles
	for (auto* item: m_items) {
		auto* tile = new GameTile(item, m_scroll->widget());
		m_tiles_layout->insertWidget(insert_at++, tile);
		m_tiles.append(tile);
	}

	m_empty_hint->setVisible(m_items.isEmpty());
	m_play_btn->setVisible(!m_items.isEmpty());

	if (m_items.isEmpty()) {
		m_focus = -1;
		UpdateHero();
		return;
	}

	FocusIndex(qBound(0, m_focus, static_cast<int>(m_items.size()) - 1));
}

void Ps5HomeScreen::FocusIndex(int index) {
	if (m_items.isEmpty()) {
		m_focus = -1;
		UpdateHero();
		return;
	}

	m_focus = qBound(0, index, static_cast<int>(m_items.size()) - 1);

	for (int i = 0; i < m_tiles.size(); i++) {
		m_tiles[i]->SetFocused(i == m_focus);
	}

	m_scroll->ensureWidgetVisible(m_tiles[m_focus], TILE_FOCUS_W / 2, 0);
	UpdateHero();
	update(); // repaint background (pic0 of focused game)
}

void Ps5HomeScreen::UpdateHero() {
	if (m_focus < 0 || m_focus >= m_items.size()) {
		m_hero_name->clear();
		m_hero_meta->clear();
		m_hero_status->clear();
		m_trophy_card->hide();
		return;
	}

	const auto& info = m_items[m_focus]->GetInfo();

	m_hero_name->setText(!info.name.isEmpty() ? info.name : info.title_id);

	QStringList meta;
	if (!info.title_id.isEmpty()) {
		meta << info.title_id;
	}
	if (!info.firmwareVer.isEmpty()) {
		meta << tr("FW %1").arg(info.firmwareVer);
	}
	if (m_items[m_focus]->IsRunning()) {
		meta << tr("Running");
	}
	m_hero_meta->setText(meta.join(QStringLiteral("   ") + QChar(0x2022) + QStringLiteral("   ")));

	m_hero_status->setText(StatusText(info.game_status));
	m_hero_status->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 600; color: %1;")
	                                 .arg(StatusColor(info.game_status)));

	// Trophy card: real progress read from the game's trophy definitions plus
	// the unlock state the emulator persists to sce_sys/trophy2/unlocked.dat.
	const auto counts = TrophyViewerDialog::CountTrophies(&info);
	if (counts.valid) {
		const int percent =
		    counts.total > 0 ? counts.earned_total * 100 / counts.total : 0;
		m_trophy_progress->setText(QStringLiteral("%1%").arg(percent));
		m_trophy_earned->setText(tr("%1/%2").arg(counts.earned_total).arg(counts.total));
		m_trophy_gold->setText(
		    QStringLiteral("%1/%2").arg(counts.earned_gold).arg(counts.gold));
		m_trophy_silver->setText(
		    QStringLiteral("%1/%2").arg(counts.earned_silver).arg(counts.silver));
		m_trophy_bronze->setText(
		    QStringLiteral("%1/%2").arg(counts.earned_bronze).arg(counts.bronze));
		m_trophy_card->show();
	} else {
		m_trophy_card->hide();
	}
}

void Ps5HomeScreen::LaunchFocused() {
	if (m_focus >= 0 && m_focus < m_items.size()) {
		m_backend->RunItem(m_items[m_focus]);
	}
}

void Ps5HomeScreen::OpenSettingsForFocused() {
	m_backend->OpenGlobalSettings();
}

void Ps5HomeScreen::ToggleFullscreen() {
	auto* top_level = window();
	if (top_level == nullptr) {
		return;
	}

	if (top_level->isFullScreen()) {
		top_level->showNormal();
		if (m_fullscreen_btn != nullptr) {
			m_fullscreen_btn->setText(QString(QChar(0xE740))); // expand glyph
			m_fullscreen_btn->setToolTip(tr("Fullscreen (F11)"));
		}
	} else {
		top_level->showFullScreen();
		if (m_fullscreen_btn != nullptr) {
			m_fullscreen_btn->setText(QString(QChar(0xE73F))); // contract glyph
			m_fullscreen_btn->setToolTip(tr("Exit fullscreen (F11 / Esc)"));
		}
	}
	setFocus();
}

void Ps5HomeScreen::keyPressEvent(QKeyEvent* event) {
	switch (event->key()) {
		case Qt::Key_Left: FocusIndex(m_focus - 1); return;
		case Qt::Key_Right: FocusIndex(m_focus + 1); return;
		case Qt::Key_Return:
		case Qt::Key_Enter:
		case Qt::Key_X: LaunchFocused(); return;
		case Qt::Key_S: OpenSettingsForFocused(); return;
		case Qt::Key_F5: m_backend->ScanGameDirectory(); return;
		case Qt::Key_F11: ToggleFullscreen(); return;
		case Qt::Key_Escape:
			if (window() != nullptr && window()->isFullScreen()) {
				ToggleFullscreen();
				return;
			}
			break;
		default: break;
	}
	QWidget::keyPressEvent(event);
}

void Ps5HomeScreen::paintEvent(QPaintEvent* /*event*/) {
	QPainter p(this);

	// Base PS5 blue gradient.
	QLinearGradient bg(0, 0, width(), height());
	bg.setColorAt(0.0, QColor(16, 24, 46));
	bg.setColorAt(0.5, QColor(23, 38, 74));
	bg.setColorAt(1.0, QColor(12, 18, 36));
	p.fillRect(rect(), bg);

	// Focused game's pic0 as a dimmed backdrop, PS5-style.
	if (m_focus >= 0 && m_focus < m_items.size()) {
		const auto& info = m_items[m_focus]->GetInfo();
		if (!info.basedir.isEmpty()) {
			const QString pic0 =
			    QDir(info.basedir).filePath(QStringLiteral("sce_sys/pic0.png"));
			if (QFileInfo::exists(pic0)) {
				QPixmap pm(pic0);
				if (!pm.isNull()) {
					pm = pm.scaled(size(), Qt::KeepAspectRatioByExpanding,
					               Qt::SmoothTransformation);
					p.setOpacity(0.35);
					p.drawPixmap((width() - pm.width()) / 2, (height() - pm.height()) / 2, pm);
					p.setOpacity(1.0);

					QLinearGradient fade(0, 0, 0, height());
					fade.setColorAt(0.0, QColor(10, 16, 32, 120));
					fade.setColorAt(1.0, QColor(10, 16, 32, 235));
					p.fillRect(rect(), fade);
				}
			}
		}
	}
}

void Ps5HomeScreen::resizeEvent(QResizeEvent* event) {
	QWidget::resizeEvent(event);
	update();
}
