#ifndef PS5_HOME_SCREEN_H
#define PS5_HOME_SCREEN_H

#include "common.h"
#include "configuration.h"

#include <QPixmap>
#include <QSize>
#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QScrollArea;
class QGridLayout;
class QHBoxLayout;
class QPushButton;
class QTimer;
class QFrame;

class ConfigurationListWidget;
class ConfigurationItem;
class GameTile;
class Ps5SettingsDialog;

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
	void PlayIntroAnimation();

protected:
	void keyPressEvent(QKeyEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	void FocusIndex(int index);
	void FocusRow(int index);
	void ActivateRow(int index);
	void UpdateHero();
	void LaunchFocused();
	void OpenSettingsForFocused();
	void OpenLibrary();
	void CloseLibrary();
	void ShowGamesTab();
	void ShowMediaTab();
	void UpdateTabStyles();
	void SetMediaFocus(int index);
	void StartFocusedMediaApp();
	void OpenAppLibrary();
	void CloseAppLibrary();
	void FocusAppIndex(int index);
	void OpenSearch();
	void CloseSearch();
	void UpdateSearchResults();
	void OpenExplore();
	void CloseExplore();
	void RebuildLibraryGrid();
	void RelayoutLibraryGrid();
	void FocusLibraryIndex(int index);
	void LaunchLibraryItem(ConfigurationItem* item);
	void ToggleFullscreen();

	ConfigurationListWidget* m_backend = nullptr;

	QWidget*     m_top_bar     = nullptr;
	QLabel*      m_clock       = nullptr;
	QLabel*      m_hero_name   = nullptr;
	QLabel*      m_hero_meta   = nullptr;
	QLabel*      m_hero_status = nullptr;
	QPushButton* m_play_btn    = nullptr;
	QPushButton* m_fullscreen_btn = nullptr;
	Ps5SettingsDialog* m_settings_page = nullptr;
	Configuration      m_settings_info;

	// In-home Game Library (PS5-style: tile row stays on top, grid fills the
	// space below). m_library_tiles holds file-local LibraryGridTile widgets.
	QWidget*        m_library_section = nullptr;
	QWidget*        m_hero_gap        = nullptr;
	QWidget*        m_library_tile    = nullptr;
	QList<QWidget*> m_pre_library_tiles;
	QScrollArea*    m_library_scroll  = nullptr;
	QWidget*        m_library_host    = nullptr;
	QGridLayout*    m_library_grid    = nullptr;
	QLabel*         m_library_storage  = nullptr;
	QPushButton*    m_library_sort_btn = nullptr;

	// Media tab (PS5-style): its own tile row + hero, themed backdrop.
	QPushButton*    m_tab_games     = nullptr;
	QPushButton*    m_tab_media     = nullptr;
	QWidget*        m_media_section = nullptr;
	QWidget*        m_media_hero    = nullptr;
	QLabel*         m_media_chip    = nullptr;
	QLabel*         m_media_name    = nullptr;
	QLabel*         m_media_desc    = nullptr;
	QPushButton*    m_media_start   = nullptr;
	QList<QWidget*> m_media_tiles;
	int             m_media_focus = 0;
	bool            m_media_mode  = false;

	// Opened-state labels next to the tiles (PS5 shows the section name there).
	QLabel* m_library_label = nullptr;
	QLabel* m_applib_label  = nullptr;

	// Full-screen search page (PS5-style: filter chips, big search field,
	// "Trending"/results grid of game covers).
	QWidget*            m_search_page    = nullptr;
	QLineEdit*          m_search_field   = nullptr;
	QLabel*             m_search_heading = nullptr;
	QWidget*            m_search_host    = nullptr;
	QGridLayout*        m_search_grid    = nullptr;
	QList<QWidget*>     m_search_tiles;
	QList<QPushButton*> m_search_chips;
	int                 m_search_cat  = 0; // 0=All 1=Games 2=Media 3=Players
	bool                m_search_mode = false;

	// Explore view (PS5 "Welcome back" dashboard with trophy/storage cards),
	// shown in-home below the tile row like the Game Library.
	QWidget* m_explore_page  = nullptr;
	QWidget* m_explore_tile  = nullptr;
	QLabel*  m_explore_label = nullptr;
	bool     m_explore_mode  = false;

	// "App Library" inside the Media tab, mirroring the Game Library behavior.
	QWidget*        m_applib_host = nullptr;
	QHBoxLayout*    m_applib_row  = nullptr;
	QPushButton*    m_applib_sort_btn = nullptr;
	QList<QWidget*> m_applib_tiles;
	QList<int>      m_applib_indices;
	int             m_applib_focus = -1;
	int             m_applib_sort  = 0; // 0 = Name A-Z, 1 = Name Z-A
	bool            m_applib_mode  = false;
	void            SortAppLibraryRow();
	QList<QWidget*> m_library_tiles;
	int             m_library_focus = -1;
	int             m_library_cols  = 0;
	int             m_library_sort  = 0; // 0 = Name A-Z, 1 = Name Z-A
	bool            m_library_mode  = false;
	QPixmap            m_home_bg;
	QPixmap            m_home_bg_scaled;
	QSize              m_home_bg_for;
	QString            m_backdrop_path;
	QPixmap            m_backdrop_src;
	QPixmap            m_backdrop_scaled;
	QSize              m_backdrop_for;
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

	// Unified home-row focus: [0..3] system tiles (Store, Explore, PS+,
	// Library-after-games ordering handled in the list itself), then games.
	QList<QWidget*> m_row_widgets; // every focusable tile in visual order
	int             m_row_focus = -1;
	int             m_row_first_game = 0; // index in m_row_widgets of first game tile
	int                       m_hero_refresh_tick = 0;
};

#endif // PS5_HOME_SCREEN_H
