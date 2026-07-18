#ifndef LAUNCHER_INCLUDE_PS5SETTINGSDIALOG_H_
#define LAUNCHER_INCLUDE_PS5SETTINGSDIALOG_H_

#include "common.h"
#include "configuration.h"

#include <QDialog>
#include <QPixmap>
#include <QString>
#include <QStringList>

class QListWidget;
class QStackedWidget;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class QLabel;

class Ps5SettingsDialog final: public QDialog {
	Q_OBJECT
	PS5SIM_QT_CLASS_NO_COPY(Ps5SettingsDialog);

public:
	explicit Ps5SettingsDialog(Configuration& info, QWidget* parent = nullptr);
	~Ps5SettingsDialog() override = default;

	void                      SetGameDirectories(const QStringList& dirs);
	[[nodiscard]] QStringList GetGameDirectories() const;

protected:
	void keyPressEvent(QKeyEvent* event) override;
	void paintEvent(QPaintEvent* event) override;

private:
	QWidget* BuildCategoryPage(const QString& title, QWidget* content);
	QWidget* BuildScreenPage();
	QWidget* BuildSoundPage();
	QWidget* BuildGraphicsPage();
	QWidget* BuildSystemPage();
	QWidget* BuildStoragePage();
	void     Apply();

	Configuration& m_info;
	QPixmap        m_background;

	QListWidget*    m_categories = nullptr;
	QStackedWidget* m_pages      = nullptr;

	QComboBox* m_resolution   = nullptr;
	QSpinBox*  m_vblank       = nullptr;
	QCheckBox* m_home_music   = nullptr;
	QComboBox* m_shader_opt   = nullptr;
	QCheckBox* m_shader_val   = nullptr;
	QCheckBox* m_vulkan_val   = nullptr;
	QCheckBox* m_ngg_rectlist = nullptr;
	QCheckBox* m_renderdoc    = nullptr;
	QComboBox* m_printf_dir   = nullptr;
	QLineEdit* m_printf_file  = nullptr;
	QComboBox* m_profiler     = nullptr;
	QComboBox* m_shader_log   = nullptr;
	QLineEdit* m_shader_log_folder = nullptr;
	QCheckBox* m_cb_dump           = nullptr;
	QLineEdit* m_cb_dump_folder    = nullptr;
	QListWidget* m_game_dirs       = nullptr;
};

#endif // LAUNCHER_INCLUDE_PS5SETTINGSDIALOG_H_
