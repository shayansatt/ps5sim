#ifndef PS5_HOME_SCREEN_H
#define PS5_HOME_SCREEN_H

#include "common.h"

#include <QWidget>

class QLabel;
class QScrollArea;
class QHBoxLayout;
class QPushButton;
class QTimer;
class QFrame;

class ConfigurationListWidget;
class ConfigurationItem;
class GameTile;

// Full-screen PS5-style home screen. Wraps ConfigurationListWidget (kept hidden)
// as the scanning/running/settings backend and presents a console-like front end:
// a horizontal row of game tiles, a large hero panel for the focused game, and
// keyboard/gamepad navigation.
class Ps5HomeScreen: public QWidget {
	Q_OBJECT

public:
	explicit Ps5HomeScreen(ConfigurationListWidget* backend, QWidget* parent = nullptr);
	~Ps5HomeScreen() override;

	PS5SIM_QT_CLASS_NO_COPY(Ps5HomeScreen);

	void RebuildTiles();

protected:
	void keyPressEvent(QKeyEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	void FocusIndex(int index);
	void UpdateHero();
	void LaunchFocused();
	void OpenSettingsForFocused();
	void ToggleFullscreen();

	ConfigurationListWidget* m_backend = nullptr;

	QLabel*      m_clock       = nullptr;
	QLabel*      m_hero_name   = nullptr;
	QLabel*      m_hero_meta   = nullptr;
	QLabel*      m_hero_status = nullptr;
	QPushButton* m_play_btn    = nullptr;
	QPushButton* m_fullscreen_btn = nullptr;
	QFrame*      m_trophy_card = nullptr;
	QLabel*      m_trophy_progress = nullptr;
	QLabel*      m_trophy_earned   = nullptr;
	QLabel*      m_trophy_gold     = nullptr;
	QLabel*      m_trophy_silver   = nullptr;
	QLabel*      m_trophy_bronze   = nullptr;
	QScrollArea* m_scroll      = nullptr;
	QHBoxLayout* m_tiles_layout = nullptr;
	QLabel*      m_empty_hint  = nullptr;
	QTimer*      m_clock_timer = nullptr;

	QList<GameTile*>          m_tiles;
	QList<ConfigurationItem*> m_items;
	int                       m_focus = -1;
	int                       m_hero_refresh_tick = 0;
};

#endif // PS5_HOME_SCREEN_H
