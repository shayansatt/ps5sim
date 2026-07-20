#include "ps5HomeScreen.h"

#include "configuration.h"
#include "configurationItem.h"
#include "configurationListWidget.h"
#include "ps5SettingsDialog.h"
#include "trophyViewerDialog.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEasingCurve>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSequentialAnimationGroup>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <utility>

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

// Common base for every tile living in the home row (games + system tiles):
// lets the unified row-focus walk them uniformly.
class RowTile: public QWidget {
public:
	explicit RowTile(QWidget* parent): QWidget(parent) {}
	~RowTile() override = default;

	virtual void SetFocused(bool focused) = 0;
};

// A single game tile: rounded icon that grows and gains a glow ring when focused.
class GameTile: public RowTile {
public:
	GameTile(ConfigurationItem* item, QWidget* parent): RowTile(parent), m_item(item) {
		// Sized to the icon when idle so row gaps stay even; grows (with ring
		// padding) only while focused, like the system tiles.
		setFixedSize(TILE_W, TILE_FOCUS_H + 16);
		setAttribute(Qt::WA_Hover, true);
		setCursor(Qt::PointingHandCursor);
	}

	// Mouse support: single click focuses the tile, double click launches.
	// Plain std::function callbacks keep this file-local class moc-free.
	void SetOnClicked(std::function<void()> cb) { m_on_clicked = std::move(cb); }
	void SetOnDoubleClicked(std::function<void()> cb) { m_on_double_clicked = std::move(cb); }

	void SetFocused(bool focused) override {
		if (m_focused != focused) {
			m_focused = focused;
			setFixedSize(m_focused ? TILE_FOCUS_W + 16 : TILE_W, TILE_FOCUS_H + 16);
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

		// Icon pixmaps are cached per state: loading + rounding on every paint
		// makes the boot/focus animations stutter badly.
		QPixmap& cache = m_focused ? m_pm_focused : m_pm_normal;
		if (cache.isNull()) {
			cache = RoundPixmap(LoadIcon(m_item->GetInfo(), side), side, radius);
		}
		p.drawPixmap(r.topLeft(), cache);

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

	void mousePressEvent(QMouseEvent* event) override {
		if (event->button() == Qt::LeftButton && m_on_clicked) {
			m_on_clicked();
			return;
		}
		QWidget::mousePressEvent(event);
	}

	void mouseDoubleClickEvent(QMouseEvent* event) override {
		if (event->button() == Qt::LeftButton && m_on_double_clicked) {
			m_on_double_clicked();
			return;
		}
		QWidget::mouseDoubleClickEvent(event);
	}

private:
	ConfigurationItem* m_item    = nullptr;
	bool               m_focused = false;
	QPixmap            m_pm_normal;
	QPixmap            m_pm_focused;
	std::function<void()> m_on_clicked;
	std::function<void()> m_on_double_clicked;
};

// Small square system tile (PS Store / Explore / PS Plus / Library) shown in the
// tile row, painted as a dark navy rounded square with its official-style SVG icon.
class SystemTile: public RowTile {
public:
	SystemTile(const QString& svg_path, const QString& tip, QWidget* parent)
	    : RowTile(parent), m_icon(svg_path) {
		setFixedSize(TILE_W, TILE_FOCUS_H + 16);
		setToolTip(tip);
		setAttribute(Qt::WA_Hover, true);
		setCursor(Qt::PointingHandCursor);
	}

	void SetOnClicked(std::function<void()> cb) { m_on_clicked = std::move(cb); }
	void SetOnDoubleClicked(std::function<void()> cb) { m_on_double_clicked = std::move(cb); }

	void SetFocused(bool focused) override {
		if (m_focused != focused) {
			m_focused = focused;
			// Focused system tile grows exactly like a focused game tile.
			setFixedSize(m_focused ? TILE_FOCUS_W + 16 : TILE_W, TILE_FOCUS_H + 16);
			update();
		}
	}

protected:
	void paintEvent(QPaintEvent* /*event*/) override {
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::SmoothPixmapTransform);

		const int side   = m_focused ? TILE_FOCUS_W : TILE_W;
		const int radius = side * 22 / 100;
		const QRect r((width() - side) / 2, (height() - side) / 2, side, side);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(20, 34, 62, 235));
		p.drawRoundedRect(r, radius, radius);

		const int     icon_side = side * 45 / 100;
		const QPixmap pm        = m_icon.pixmap(icon_side, icon_side);
		const QSize   ps        = pm.deviceIndependentSize().toSize();
		p.drawPixmap(r.center().x() - ps.width() / 2, r.center().y() - ps.height() / 2, pm);

		if (m_focused) {
			// Same thin white focus ring the game tiles use.
			p.setBrush(Qt::NoBrush);
			p.setPen(QPen(QColor(255, 255, 255, 235), 3));
			p.drawRoundedRect(r.adjusted(-5, -5, 5, 5), radius + 5, radius + 5);
		}
	}

	void mousePressEvent(QMouseEvent* event) override {
		if (event->button() == Qt::LeftButton && m_on_clicked) {
			m_on_clicked();
			return;
		}
		QWidget::mousePressEvent(event);
	}

	void mouseDoubleClickEvent(QMouseEvent* event) override {
		if (event->button() == Qt::LeftButton && m_on_double_clicked) {
			m_on_double_clicked();
			return;
		}
		QWidget::mouseDoubleClickEvent(event);
	}

private:
	QIcon m_icon;
	bool  m_focused = false;
	std::function<void()> m_on_clicked;
	std::function<void()> m_on_double_clicked;
};

// Static catalog for the Media tab, mirroring the console: each entry carries
// its tile art (official-style icon from resources), hero texts and the
// full-screen backdrop gradient.
struct MediaAppSpec {
	const char* name;
	const char* category;
	const char* desc;
	const char* url;
	const char* icon;      // resource path
	bool        full_tile; // true = icon IS the tile art (Apple Music, Spotify)
	QColor      tile_a;
	QColor      tile_b;
	QColor      bg_a; // full-screen backdrop gradient while focused
	QColor      bg_b;
};

const MediaAppSpec MEDIA_APPS[] = {
    {"All Apps", "Apps", "Browse every media app available on this console.", "",
     ":/ps5/media_allapps.svg", false, QColor(30, 24, 40, 235), QColor(20, 15, 28, 235),
     QColor(32, 26, 46), QColor(13, 10, 21)},
    {"TV & Video", "Video", "Netflix, YouTube, Twitch and more video apps.", "",
     ":/ps5/media_video.png", false, QColor(30, 24, 40, 235), QColor(20, 15, 28, 235),
     QColor(28, 24, 44), QColor(12, 10, 20)},
    {"Apple Music", "Music",
     "Stream millions of songs with Apple Music on Playstation. All ad-free.",
     "https://music.apple.com", ":/ps5/media_applemusic.png", true, QColor(0xfd, 0x45, 0x5f),
     QColor(0xd6, 0x1e, 0x35), QColor(0x8e, 0x26, 0x33), QColor(0x66, 0x13, 0x1e)},
    {"Spotify", "Music", "Play music and podcasts from Spotify while you game.",
     "https://open.spotify.com", ":/ps5/media_spotify.png", true, QColor(0x19, 0x14, 0x14),
     QColor(0x0e, 0x0b, 0x0b), QColor(0x14, 0x38, 0x26), QColor(0x08, 0x18, 0x10)},
    {"App Library", "Apps", "All your media apps in one place.", "",
     ":/ps5/media_gallery.png", false, QColor(30, 24, 40, 235), QColor(20, 15, 28, 235),
     QColor(36, 29, 50), QColor(15, 12, 24)},
};
constexpr int MEDIA_APP_COUNT = static_cast<int>(sizeof(MEDIA_APPS) / sizeof(MEDIA_APPS[0]));
constexpr int APPLIB_INDEX    = MEDIA_APP_COUNT - 1; // App Library trails the row

// Media-app tile: official-style icon on a rounded square; grows and gains the
// white ring when focused, exactly like the game tiles.
class MediaTile: public QWidget {
public:
	MediaTile(int app_index, QWidget* parent): QWidget(parent), m_app(app_index) {
		setFixedSize(TILE_W, TILE_FOCUS_H + 16);
		setToolTip(QString::fromUtf8(MEDIA_APPS[m_app].name));
		setAttribute(Qt::WA_Hover, true);
		setCursor(Qt::PointingHandCursor);
	}

	void SetOnClicked(std::function<void()> cb) { m_on_clicked = std::move(cb); }
	void SetOnDoubleClicked(std::function<void()> cb) { m_on_double_clicked = std::move(cb); }

	void SetFocused(bool focused) {
		if (m_focused != focused) {
			m_focused = focused;
			setFixedSize(m_focused ? TILE_FOCUS_W + 16 : TILE_W, TILE_FOCUS_H + 16);
			update();
		}
	}

protected:
	void paintEvent(QPaintEvent* /*event*/) override {
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::SmoothPixmapTransform);

		const auto& app    = MEDIA_APPS[m_app];
		const int   side   = m_focused ? TILE_FOCUS_W : TILE_W;
		const int   radius = side * 22 / 100;
		const QRect r((width() - side) / 2, (height() - side) / 2, side, side);

		if (app.full_tile) {
			// The icon itself is the tile (Apple Music / Spotify app art),
			// scaled to cover and clipped to the rounded square.
			QPixmap& cache = m_focused ? m_pm_focused : m_pm_normal;
			if (cache.isNull()) {
				QPixmap src(QString::fromUtf8(app.icon));
				if (!src.isNull()) {
					cache = RoundPixmap(src.scaled(side, side, Qt::KeepAspectRatioByExpanding,
					                               Qt::SmoothTransformation),
					                    side, radius);
				}
			}
			if (!cache.isNull()) {
				p.drawPixmap(r.topLeft(), cache);
			}
		} else {
			QLinearGradient tile(r.topLeft(), r.bottomRight());
			tile.setColorAt(0.0, app.tile_a);
			tile.setColorAt(1.0, app.tile_b);
			p.setPen(Qt::NoPen);
			p.setBrush(tile);
			p.drawRoundedRect(r, radius, radius);

			// White mark centered on the dark tile (per-state cache: the tile
			// grows when focused, so the icon is rendered at both sizes).
			const int icon_side = side * 48 / 100;
			QPixmap&  cache     = m_focused ? m_pm_focused : m_pm_normal;
			if (cache.isNull()) {
				cache = QIcon(QString::fromUtf8(app.icon)).pixmap(icon_side, icon_side);
			}
			const QSize ps = cache.deviceIndependentSize().toSize();
			p.drawPixmap(r.center().x() - ps.width() / 2, r.center().y() - ps.height() / 2,
			             cache);
		}

		if (m_focused) {
			p.setBrush(Qt::NoBrush);
			p.setPen(QPen(QColor(255, 255, 255, 235), 3));
			p.drawRoundedRect(r.adjusted(-5, -5, 5, 5), radius + 5, radius + 5);
		}
	}

	void mousePressEvent(QMouseEvent* event) override {
		if (event->button() == Qt::LeftButton && m_on_clicked) {
			m_on_clicked();
			return;
		}
		QWidget::mousePressEvent(event);
	}

	void mouseDoubleClickEvent(QMouseEvent* event) override {
		if (event->button() == Qt::LeftButton && m_on_double_clicked) {
			m_on_double_clicked();
			return;
		}
		QWidget::mouseDoubleClickEvent(event);
	}

private:
	int     m_app     = 0;
	bool    m_focused = false;
	QPixmap m_pm_normal;
	QPixmap m_pm_focused;
	std::function<void()> m_on_clicked;
	std::function<void()> m_on_double_clicked;
};

// Opaque overlay page that paints the settings light-beam wallpaper (cover
// scaled + darkened) — used by the search screen so the home underneath stays
// hidden while matching the settings-page look.
class WallpaperPage: public QWidget {
public:
	WallpaperPage(const QPixmap& bg, QWidget* parent): QWidget(parent), m_bg(bg) {}

protected:
	void paintEvent(QPaintEvent* /*event*/) override {
		QPainter p(this);
		if (m_bg.isNull()) {
			p.fillRect(rect(), QColor(16, 20, 32));
			return;
		}
		if (m_for != size() || m_scaled.isNull()) {
			m_scaled = m_bg.scaled(size(), Qt::KeepAspectRatioByExpanding,
			                       Qt::SmoothTransformation);
			m_for    = size();
		}
		const int x = (m_scaled.width() - width()) / 2;
		const int y = (m_scaled.height() - height()) / 2;
		p.drawPixmap(0, 0, m_scaled, x, y, width(), height());

		QLinearGradient fade(0, 0, 0, height());
		fade.setColorAt(0.0, QColor(8, 12, 24, 90));
		fade.setColorAt(1.0, QColor(8, 12, 24, 170));
		p.fillRect(rect(), fade);
	}

private:
	QPixmap m_bg;
	QPixmap m_scaled;
	QSize   m_for;
};

// Square cover tile inside the Game Library grid (PS5 look: plain square cover
// art, no corner rounding, thin white ring when focused).
class LibraryGridTile: public QWidget {
public:
	static constexpr int SIDE = 150;
	static constexpr int PAD  = 12; // room for the focus ring

	LibraryGridTile(ConfigurationItem* item, QWidget* parent): QWidget(parent), m_item(item) {
		setFixedSize(SIDE + PAD, SIDE + PAD);
		setCursor(Qt::PointingHandCursor);
		setToolTip(item->GetInfo().name);
	}

	void SetOnClicked(std::function<void()> cb) { m_on_clicked = std::move(cb); }
	void SetOnDoubleClicked(std::function<void()> cb) { m_on_double_clicked = std::move(cb); }

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

		constexpr int radius = 0; // PS5 library covers are sharp squares
		const QRect   r((width() - SIDE) / 2, (height() - SIDE) / 2, SIDE, SIDE);

		if (m_pm.isNull()) {
			m_pm = RoundPixmap(LoadIcon(m_item->GetInfo(), SIDE), SIDE, radius);
		}
		p.drawPixmap(r.topLeft(), m_pm);

		if (m_focused) {
			p.setBrush(Qt::NoBrush);
			p.setPen(QPen(QColor(255, 255, 255, 235), 3));
			p.drawRoundedRect(r.adjusted(-4, -4, 4, 4), 4, 4);
		}

		if (m_item->IsRunning()) {
			p.setBrush(QColor(58, 208, 122, 220));
			p.setPen(Qt::NoPen);
			p.drawEllipse(r.right() - 18, r.top() + 8, 10, 10);
		}
	}

	void mousePressEvent(QMouseEvent* event) override {
		if (event->button() == Qt::LeftButton && m_on_clicked) {
			m_on_clicked();
			return;
		}
		QWidget::mousePressEvent(event);
	}

	void mouseDoubleClickEvent(QMouseEvent* event) override {
		if (event->button() == Qt::LeftButton && m_on_double_clicked) {
			m_on_double_clicked();
			return;
		}
		QWidget::mouseDoubleClickEvent(event);
	}

private:
	ConfigurationItem* m_item    = nullptr;
	bool               m_focused = false;
	QPixmap            m_pm;
	std::function<void()> m_on_clicked;
	std::function<void()> m_on_double_clicked;
};

Ps5HomeScreen::Ps5HomeScreen(ConfigurationListWidget* backend, QWidget* parent)
    : QWidget(parent), m_backend(backend) {
	setFocusPolicy(Qt::StrongFocus);
	setAutoFillBackground(false);

	// PS5 light-beam wallpaper, shown whenever no game artwork backs the screen.
	m_home_bg.load(QStringLiteral(":/ps5/settings_bg.jpg"));

	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(56, 28, 56, 0);
	root->setSpacing(0);

	// Top function bar: "Games / Media" tabs on the left; search / settings glyphs,
	// profile avatar and the clock on the right (PS5 dashboard style).
	// Wrapped in a widget so the boot intro can slide/fade it as one block.
	m_top_bar = new QWidget(this);
	m_top_bar->setStyleSheet(QStringLiteral("background: transparent;"));
	auto* top = new QHBoxLayout(m_top_bar);
	top->setContentsMargins(0, 0, 0, 0);
	top->setSpacing(0);

	m_tab_games = new QPushButton(tr("Games"), this);
	m_tab_media = new QPushButton(tr("Media"), this);
	for (auto* tab: {m_tab_games, m_tab_media}) {
		tab->setCursor(Qt::PointingHandCursor);
		tab->setFocusPolicy(Qt::NoFocus);
		tab->setFlat(true);
	}
	connect(m_tab_games, &QPushButton::clicked, this, &Ps5HomeScreen::ShowGamesTab);
	connect(m_tab_media, &QPushButton::clicked, this, &Ps5HomeScreen::ShowMediaTab);
	UpdateTabStyles();
	top->addWidget(m_tab_games, 0, Qt::AlignVCenter);
	top->addSpacing(26);
	top->addWidget(m_tab_media, 0, Qt::AlignVCenter);
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
	auto* search_btn = make_glyph(0xE721, tr("Search")); // search
	connect(search_btn, &QPushButton::clicked, this, &Ps5HomeScreen::OpenSearch);
	top->addWidget(search_btn, 0, Qt::AlignVCenter);

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

	root->addWidget(m_top_bar);

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
	auto* store_tile   = new SystemTile(QStringLiteral(":/ps5/store.svg"), tr("Store"), strip);
	store_tile->SetOnClicked([this]() {
		FocusRow(0);
		setFocus();
	});
	auto* explore_tile = new SystemTile(QStringLiteral(":/ps5/explore.svg"), tr("Explore"), strip);
	// Single click only focuses (like game tiles); double click opens/closes.
	explore_tile->SetOnClicked([this]() {
		FocusRow(1);
		setFocus();
	});
	explore_tile->SetOnDoubleClicked([this]() {
		FocusRow(1);
		setFocus();
		if (m_explore_mode) {
			CloseExplore();
		} else {
			OpenExplore();
		}
	});
	m_explore_tile = explore_tile;
	auto* psplus_tile =
	    new SystemTile(QStringLiteral(":/ps5/psplus.svg"), tr("PlayStation Plus"), strip);
	psplus_tile->SetOnClicked([this]() {
		FocusRow(m_row_first_game + static_cast<int>(m_tiles.size()));
		setFocus();
	});
	m_tiles_layout->addWidget(store_tile);
	m_tiles_layout->addWidget(explore_tile);

	// Caption shown beside the Explore tile while its view is open.
	m_explore_label = new QLabel(tr("Explore"), strip);
	m_explore_label->setStyleSheet(
	    QStringLiteral("color: rgba(240,246,255,235); font-size: 26px; background: transparent;"));
	m_explore_label->hide();
	m_tiles_layout->addWidget(m_explore_label, 0, Qt::AlignVCenter);

	m_tiles_layout->addWidget(psplus_tile);
	m_pre_library_tiles = {store_tile, explore_tile, psplus_tile};
	auto* library_tile = new SystemTile(QStringLiteral(":/ps5/library.svg"), tr("Game Library"), strip);
	library_tile->SetOnClicked([this]() {
		FocusRow(m_row_first_game + static_cast<int>(m_tiles.size()) + 1);
		setFocus();
	});
	library_tile->SetOnDoubleClicked([this]() {
		FocusRow(m_row_first_game + static_cast<int>(m_tiles.size()) + 1);
		setFocus();
		if (m_library_mode) {
			CloseLibrary();
		} else {
			OpenLibrary();
		}
	});
	m_library_tile = library_tile;
	m_tiles_layout->addWidget(library_tile);

	// Section name shown beside the tile while the library view is open,
	// like the console's "Game Library" caption.
	m_library_label = new QLabel(tr("Game Library"), strip);
	m_library_label->setStyleSheet(
	    QStringLiteral("color: rgba(240,246,255,235); font-size: 26px; background: transparent;"));
	m_library_label->hide();
	m_tiles_layout->addSpacing(10);
	m_tiles_layout->addWidget(m_library_label, 0, Qt::AlignVCenter);
	m_tiles_layout->addStretch(1);
	m_scroll->setWidget(strip);
	root->addWidget(m_scroll);

	// In-home Game Library section (hidden until the Library tile is opened):
	// "Console storage" / "Sort by" header line with the cover grid below,
	// exactly like the real PS5 library view.
	m_library_section = new QWidget(this);
	m_library_section->setStyleSheet(QStringLiteral("background: transparent;"));
	{
		auto* lib_layout = new QVBoxLayout(m_library_section);
		lib_layout->setContentsMargins(0, 26, 0, 0);
		lib_layout->setSpacing(18);

		auto* lib_head = new QHBoxLayout();
		m_library_storage = new QLabel(m_library_section);
		m_library_storage->setStyleSheet(
		    QStringLiteral("color: rgba(235,242,255,225); font-size: 20px;"));

		// The "Sort by" text is a live control: clicking flips A-Z <-> Z-A.
		m_library_sort_btn = new QPushButton(tr("Sort by: Name (A - Z)"), m_library_section);
		m_library_sort_btn->setCursor(Qt::PointingHandCursor);
		m_library_sort_btn->setFocusPolicy(Qt::NoFocus);
		m_library_sort_btn->setStyleSheet(QStringLiteral(
		    "QPushButton { color: rgba(235,242,255,225); background: transparent; border: none; "
		    "font-size: 20px; padding: 2px 10px; }"
		    "QPushButton:hover { color: #ffffff; background: rgba(255,255,255,22); "
		    "border-radius: 8px; }"));
		connect(m_library_sort_btn, &QPushButton::clicked, this, [this]() {
			m_library_sort = (m_library_sort == 0) ? 1 : 0;
			m_library_sort_btn->setText(m_library_sort == 0 ? tr("Sort by: Name (A - Z)")
			                                                : tr("Sort by: Name (Z - A)"));
			if (m_library_mode) {
				RebuildLibraryGrid();
				FocusLibraryIndex(0);
				setFocus();
			}
		});

		lib_head->addWidget(m_library_storage);
		lib_head->addStretch(1);
		lib_head->addWidget(m_library_sort_btn);
		lib_layout->addLayout(lib_head);

		m_library_scroll = new QScrollArea(m_library_section);
		m_library_scroll->setWidgetResizable(true);
		m_library_scroll->setFrameShape(QFrame::NoFrame);
		m_library_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		m_library_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		m_library_scroll->setFocusPolicy(Qt::NoFocus);
		m_library_scroll->setStyleSheet(QStringLiteral(
		    "QScrollArea { background: transparent; border: none; }"
		    "QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }"
		    "QScrollBar::handle:vertical { background: rgba(200,210,230,110); min-height: 40px; "
		    "border-radius: 5px; }"
		    "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"));

		m_library_host = new QWidget(m_library_scroll);
		m_library_host->setStyleSheet(QStringLiteral("background: transparent;"));
		m_library_grid = new QGridLayout(m_library_host);
		m_library_grid->setContentsMargins(0, 0, 0, 0);
		m_library_grid->setHorizontalSpacing(18);
		m_library_grid->setVerticalSpacing(18);
		m_library_scroll->setWidget(m_library_host);
		lib_layout->addWidget(m_library_scroll, 1);
	}
	m_library_section->hide();
	root->addWidget(m_library_section, 1);

	// Media tab content: media-app tile row on top, then the PS5-style hero
	// (category chip, app name, description, Read More, Start pill).
	m_media_section = new QWidget(this);
	m_media_section->setStyleSheet(QStringLiteral("background: transparent;"));
	{
		auto* media_layout = new QVBoxLayout(m_media_section);
		media_layout->setContentsMargins(0, 0, 0, 0);
		media_layout->setSpacing(0);

		auto* media_row = new QHBoxLayout();
		media_row->setSpacing(TILE_SPACING);
		for (int i = 0; i < MEDIA_APP_COUNT; i++) {
			auto* tile = new MediaTile(i, m_media_section);
			if (i == APPLIB_INDEX) {
				// "App Library" opens in place, mirroring the Game Library tile.
				tile->SetOnClicked([this]() {
					if (m_applib_mode) {
						CloseAppLibrary();
					} else {
						OpenAppLibrary();
					}
				});
			} else {
				tile->SetOnClicked([this, i]() {
					SetMediaFocus(i);
					setFocus();
				});
				tile->SetOnDoubleClicked([this, i]() {
					SetMediaFocus(i);
					StartFocusedMediaApp();
				});
			}
			m_media_tiles.append(tile);
			media_row->addWidget(tile);
		}

		// Caption shown beside the App Library tile while its view is open.
		m_applib_label = new QLabel(tr("App Library"), m_media_section);
		m_applib_label->setStyleSheet(QStringLiteral(
		    "color: rgba(240,246,255,235); font-size: 26px; background: transparent;"));
		m_applib_label->hide();
		media_row->addSpacing(10);
		media_row->addWidget(m_applib_label, 0, Qt::AlignVCenter);
		media_row->addStretch(1);
		media_layout->addLayout(media_row);

		media_layout->addSpacing(56);

		// Hero block lives in its own widget so the App Library view can swap
		// it out for the app grid.
		m_media_hero      = new QWidget(m_media_section);
		m_media_hero->setStyleSheet(QStringLiteral("background: transparent;"));
		auto* hero_layout = new QVBoxLayout(m_media_hero);
		hero_layout->setContentsMargins(0, 0, 0, 0);
		hero_layout->setSpacing(0);

		// Category chip: outlined rectangle like the console ("Music" / "Video").
		auto* chip_row = new QHBoxLayout();
		m_media_chip  = new QLabel(m_media_section);
		m_media_chip->setStyleSheet(QStringLiteral(
		    "color: #ffffff; font-size: 14px; font-weight: 700; letter-spacing: 1px; "
		    "border: 2px solid rgba(255,255,255,190); padding: 8px 24px; background: transparent;"));
		chip_row->addWidget(m_media_chip);
		chip_row->addStretch(1);
		hero_layout->addLayout(chip_row);

		hero_layout->addSpacing(26);

		m_media_name = new QLabel(m_media_section);
		m_media_name->setStyleSheet(
		    QStringLiteral("color: #ffffff; font-size: 44px; font-weight: 600;"));
		hero_layout->addWidget(m_media_name);

		hero_layout->addSpacing(14);

		m_media_desc = new QLabel(m_media_section);
		m_media_desc->setStyleSheet(
		    QStringLiteral("color: rgba(255,255,255,215); font-size: 20px;"));
		m_media_desc->setWordWrap(true);
		m_media_desc->setMaximumWidth(680);
		hero_layout->addWidget(m_media_desc);

		hero_layout->addSpacing(24);

		auto* read_more = new QLabel(tr("Read More"), m_media_section);
		read_more->setStyleSheet(
		    QStringLiteral("color: #ffffff; font-size: 17px; font-weight: 700;"));
		hero_layout->addWidget(read_more);

		hero_layout->addStretch(1);

		m_media_start = new QPushButton(tr("Start"), m_media_section);
		m_media_start->setCursor(Qt::PointingHandCursor);
		m_media_start->setFocusPolicy(Qt::NoFocus);
		m_media_start->setFixedSize(340, 66);
		m_media_start->setStyleSheet(QStringLiteral(
		    "QPushButton { background: rgba(0,0,0,70); color: #ffffff; border: none; "
		    "border-radius: 33px; font-size: 21px; font-weight: 700; }"
		    "QPushButton:hover { background: rgba(0,0,0,110); }"
		    "QPushButton:pressed { background: rgba(0,0,0,150); }"));
		connect(m_media_start, &QPushButton::clicked, this,
		        &Ps5HomeScreen::StartFocusedMediaApp);
		auto* start_row = new QHBoxLayout();
		start_row->addWidget(m_media_start);
		start_row->addStretch(1);
		hero_layout->addLayout(start_row);

		hero_layout->addSpacing(46);
		media_layout->addWidget(m_media_hero, 1);

		// App Library grid host (hidden until the App Library tile is opened):
		// same look as the Game Library grid, one square tile per media app.
		m_applib_host = new QWidget(m_media_section);
		m_applib_host->setStyleSheet(QStringLiteral("background: transparent;"));
		{
			auto* applib_layout = new QVBoxLayout(m_applib_host);
			applib_layout->setContentsMargins(0, 26, 0, 0);
			applib_layout->setSpacing(18);

			auto* head = new QHBoxLayout();
			auto* apps_count = new QLabel(
			    tr("Installed apps: %1").arg(MEDIA_APP_COUNT - 2), m_applib_host);
			apps_count->setStyleSheet(
			    QStringLiteral("color: rgba(235,242,255,225); font-size: 20px;"));

			// Live sort control, same as the Game Library one: click flips A-Z <-> Z-A.
			m_applib_sort_btn = new QPushButton(tr("Sort by: Name (A - Z)"), m_applib_host);
			m_applib_sort_btn->setCursor(Qt::PointingHandCursor);
			m_applib_sort_btn->setFocusPolicy(Qt::NoFocus);
			m_applib_sort_btn->setStyleSheet(QStringLiteral(
			    "QPushButton { color: rgba(235,242,255,225); background: transparent; "
			    "border: none; font-size: 20px; padding: 2px 10px; }"
			    "QPushButton:hover { color: #ffffff; background: rgba(255,255,255,22); "
			    "border-radius: 8px; }"));
			connect(m_applib_sort_btn, &QPushButton::clicked, this, [this]() {
				m_applib_sort = (m_applib_sort == 0) ? 1 : 0;
				m_applib_sort_btn->setText(m_applib_sort == 0 ? tr("Sort by: Name (A - Z)")
				                                              : tr("Sort by: Name (Z - A)"));
				SortAppLibraryRow();
				FocusAppIndex(0);
				setFocus();
			});

			head->addWidget(apps_count);
			head->addStretch(1);
			head->addWidget(m_applib_sort_btn);
			applib_layout->addLayout(head);

			m_applib_row = new QHBoxLayout();
			m_applib_row->setSpacing(18);
			// Every launchable media app (skip All Apps and App Library itself).
			for (int i = 0; i < MEDIA_APP_COUNT; i++) {
				if (i == 0 || i == APPLIB_INDEX) {
					continue;
				}
				auto* tile = new MediaTile(i, m_applib_host);
				tile->SetOnClicked([this, i]() {
					FocusAppIndex(static_cast<int>(m_applib_indices.indexOf(i)));
					setFocus();
				});
				tile->SetOnDoubleClicked([this, i]() {
					SetMediaFocus(i);
					StartFocusedMediaApp();
				});
				m_applib_tiles.append(tile);
				m_applib_indices.append(i);
				m_applib_row->addWidget(tile);
			}
			m_applib_row->addStretch(1);
			applib_layout->addLayout(m_applib_row);
			applib_layout->addStretch(1);
			SortAppLibraryRow(); // apps come from a fixed array: normalize to A-Z
		}
		m_applib_host->hide();
		media_layout->addWidget(m_applib_host, 1);
	}
	m_media_section->hide();
	root->addWidget(m_media_section, 1);

	// The hero art shows through this gap (hidden while the library is open so
	// the grid can take the full height).
	m_hero_gap = new QWidget(this);
	m_hero_gap->setStyleSheet(QStringLiteral("background: transparent;"));
	root->addWidget(m_hero_gap, 1);

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
	// A rescan invalidates the ConfigurationItem pointers the library grid holds.
	CloseLibrary();
	CloseExplore();

	for (auto* tile: m_tiles) {
		m_tiles_layout->removeWidget(tile);
		tile->deleteLater();
	}
	m_tiles.clear();

	m_items = m_backend->GetItems();

	int insert_at = 2; // between the leading (Store, Explore) and trailing system tiles
	for (auto* item: m_items) {
		auto*     tile  = new GameTile(item, m_scroll->widget());
		const int index = static_cast<int>(m_tiles.size());
		tile->SetOnClicked([this, index]() {
			FocusRow(m_row_first_game + index);
			setFocus(); // keep keyboard navigation alive after a mouse click
		});
		tile->SetOnDoubleClicked([this, index]() {
			FocusRow(m_row_first_game + index);
			LaunchFocused();
		});
		m_tiles_layout->insertWidget(insert_at++, tile);
		m_tiles.append(tile);
	}

	// Unified focus order across the whole row: Store, Explore, games, PS+, Library.
	m_row_widgets.clear();
	m_row_first_game = 2;
	m_row_widgets.append(m_pre_library_tiles[0]); // Store
	m_row_widgets.append(m_pre_library_tiles[1]); // Explore
	for (auto* tile: m_tiles) {
		m_row_widgets.append(tile);
	}
	m_row_widgets.append(m_pre_library_tiles[2]); // PS Plus
	m_row_widgets.append(m_library_tile);         // Game Library

	m_empty_hint->setVisible(m_items.isEmpty());
	m_play_btn->setVisible(!m_items.isEmpty());

	// Land on the previously focused game if any, else the first game, else Store.
	const int game_count = static_cast<int>(m_items.size());
	FocusRow(game_count > 0 ? m_row_first_game + qBound(0, m_focus, game_count - 1) : 0);
}

void Ps5HomeScreen::FocusRow(int index) {
	if (m_row_widgets.isEmpty()) {
		m_row_focus = -1;
		m_focus     = -1;
		UpdateHero();
		return;
	}

	m_row_focus = qBound(0, index, static_cast<int>(m_row_widgets.size()) - 1);

	for (int i = 0; i < m_row_widgets.size(); i++) {
		static_cast<RowTile*>(m_row_widgets[i])->SetFocused(i == m_row_focus);
	}

	// m_focus keeps tracking the focused GAME (hero panel, backdrop, launch);
	// on a system tile it holds the last game so the hero stays meaningful.
	const int  game_index = m_row_focus - m_row_first_game;
	const bool on_game    = game_index >= 0 && game_index < m_items.size();
	if (on_game) {
		m_focus = game_index;
	}

	// Hero contents belong to the focused GAME only. On a system tile the game
	// info, Play button and trophies hide and just that tile's title shows.
	if (!m_library_mode && !m_media_mode && !m_explore_mode) {
		m_hero_meta->setVisible(on_game);
		m_hero_status->setVisible(on_game);
		m_play_btn->setVisible(on_game && !m_items.isEmpty());
		if (on_game) {
			UpdateHero();
		} else {
			m_trophy_card->hide();
			QWidget* w = m_row_widgets[m_row_focus];
			m_hero_name->setText(w == m_library_tile ? tr("Game Library") : w->toolTip());
		}
	}

	m_scroll->ensureWidgetVisible(m_row_widgets[m_row_focus], TILE_FOCUS_W / 2, 0);
	update(); // repaint background (pic0 of focused game)
}

void Ps5HomeScreen::ActivateRow(int index) {
	// Enter/X on a row tile behaves like clicking it.
	if (index < 0 || index >= m_row_widgets.size()) {
		return;
	}
	QWidget* w = m_row_widgets[index];
	if (w == m_pre_library_tiles[0]) {
		return; // Store: no action yet
	}
	if (w == m_explore_tile) {
		if (m_explore_mode) {
			CloseExplore();
		} else {
			OpenExplore();
		}
		return;
	}
	if (w == m_pre_library_tiles[2]) {
		return; // PS Plus: no action yet
	}
	if (w == m_library_tile) {
		if (m_library_mode) {
			CloseLibrary();
		} else {
			OpenLibrary();
		}
		return;
	}
	LaunchFocused(); // a game tile
}

void Ps5HomeScreen::FocusIndex(int index) {
	// Legacy entry point (game-relative): route through the unified row.
	if (m_items.isEmpty()) {
		m_row_focus = -1;
		m_focus     = -1;
		UpdateHero();
		return;
	}
	FocusRow(m_row_first_game + qBound(0, index, static_cast<int>(m_items.size()) - 1));
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
		// Never resurface the card while the library or media view owns the
		// screen (the clock timer calls UpdateHero periodically).
		m_trophy_card->setVisible(!m_library_mode && !m_media_mode);
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
	if (m_settings_page != nullptr) {
		return;
	}

	// PS5-style: settings open as a full-screen page inside the app, not a window.
	m_backend->FillGlobalSettings(&m_settings_info);
	auto* page = new Ps5SettingsDialog(m_settings_info, this);
	page->setWindowFlags(Qt::Widget);
	page->SetGameDirectories(m_backend->GetGameDirectories());
	page->setGeometry(rect());
	connect(page, &QDialog::finished, this, [this, page](int result) {
		if (result == QDialog::Accepted) {
			m_backend->ApplyGlobalSettings(m_settings_info, page->GetGameDirectories());
		}
		page->deleteLater();
		m_settings_page = nullptr;
		setFocus();
	});
	m_settings_page = page;
	page->show();
	page->raise();
	page->setFocus();
}

void Ps5HomeScreen::OpenLibrary() {
	if (m_library_mode) {
		return;
	}
	CloseExplore();
	m_library_mode = true;

	// Hero area gives way to the library grid; the tile row stays on top.
	m_hero_name->hide();
	m_hero_meta->hide();
	m_hero_status->hide();
	m_empty_hint->hide();
	m_play_btn->hide();
	m_trophy_card->hide();
	m_hero_gap->hide();

	// Like the console: everything before the Library tile leaves the row, the
	// Library tile itself takes the focused (grown + ringed) look.
	for (auto* tile: m_pre_library_tiles) {
		tile->hide();
	}
	for (auto* tile: m_tiles) {
		tile->hide();
	}

	RebuildLibraryGrid();
	m_library_section->show();
	m_library_label->show();
	if (auto* tile = static_cast<SystemTile*>(m_library_tile)) {
		tile->SetFocused(true);
	}
	// The viewport only gets its real width once the layout runs; recompute the
	// column count then so the grid fills the window like the console UI.
	if (layout() != nullptr) {
		layout()->activate();
	}
	m_library_cols = 0;
	RelayoutLibraryGrid();
	FocusLibraryIndex(0);
	setFocus();
	update(); // switch the backdrop to the settings wallpaper
}

void Ps5HomeScreen::ShowMediaTab() {
	if (m_media_mode) {
		return;
	}
	CloseLibrary();
	CloseExplore();
	m_media_mode = true;

	// Games content leaves; the media row + hero take over.
	m_scroll->hide();
	m_hero_name->hide();
	m_hero_meta->hide();
	m_hero_status->hide();
	m_empty_hint->hide();
	m_play_btn->hide();
	m_trophy_card->hide();
	m_hero_gap->hide();

	m_media_section->show();
	SetMediaFocus(m_media_focus);
	UpdateTabStyles();
	setFocus();
	update();
}

void Ps5HomeScreen::ShowGamesTab() {
	if (!m_media_mode) {
		return;
	}
	CloseAppLibrary();
	m_media_mode = false;

	m_media_section->hide();
	m_scroll->show();
	m_hero_gap->show();
	m_hero_name->show();
	m_empty_hint->setVisible(m_items.isEmpty());
	FocusRow(m_row_focus); // restores hero details for the focused tile kind
	UpdateTabStyles();
	setFocus();
	update();
}

void Ps5HomeScreen::UpdateTabStyles() {
	const QString active = QStringLiteral(
	    "QPushButton { color: #ffffff; font-size: 21px; font-weight: 700; letter-spacing: 1px; "
	    "background: transparent; border: none; padding: 0; }");
	const QString idle = QStringLiteral(
	    "QPushButton { color: rgba(230,238,252,110); font-size: 21px; font-weight: 600; "
	    "letter-spacing: 1px; background: transparent; border: none; padding: 0; }"
	    "QPushButton:hover { color: rgba(240,246,255,190); }");
	m_tab_games->setStyleSheet(m_media_mode ? idle : active);
	m_tab_media->setStyleSheet(m_media_mode ? active : idle);
}

void Ps5HomeScreen::SetMediaFocus(int index) {
	m_media_focus = qBound(0, index, MEDIA_APP_COUNT - 1);
	for (int i = 0; i < m_media_tiles.size(); i++) {
		static_cast<MediaTile*>(m_media_tiles[i])->SetFocused(i == m_media_focus);
	}

	const auto& app = MEDIA_APPS[m_media_focus];
	m_media_chip->setText(tr(app.category));
	m_media_name->setText(QString::fromUtf8(app.name));
	m_media_desc->setText(tr(app.desc));
	update(); // backdrop gradient follows the focused app
}

void Ps5HomeScreen::StartFocusedMediaApp() {
	const auto& app = MEDIA_APPS[m_media_focus];
	if (app.url[0] != '\0') {
		QDesktopServices::openUrl(QUrl(QString::fromUtf8(app.url)));
	}
}

void Ps5HomeScreen::OpenAppLibrary() {
	if (m_applib_mode) {
		return;
	}
	m_applib_mode = true;

	// Row collapses to the App Library tile + caption; hero gives way to the grid.
	for (int i = 0; i < m_media_tiles.size(); i++) {
		m_media_tiles[i]->setVisible(i == APPLIB_INDEX);
	}
	static_cast<MediaTile*>(m_media_tiles[APPLIB_INDEX])->SetFocused(true);
	m_applib_label->show();
	m_media_hero->hide();
	m_applib_host->show();
	FocusAppIndex(0);
	setFocus();
	update();
}

void Ps5HomeScreen::CloseAppLibrary() {
	if (!m_applib_mode) {
		return;
	}
	m_applib_mode = false;

	m_applib_host->hide();
	m_applib_label->hide();
	static_cast<MediaTile*>(m_media_tiles[APPLIB_INDEX])->SetFocused(false);
	for (auto* tile: m_media_tiles) {
		tile->show();
	}
	m_media_hero->show();
	SetMediaFocus(m_media_focus);
	setFocus();
	update();
}

void Ps5HomeScreen::SortAppLibraryRow() {
	if (m_applib_tiles.isEmpty()) {
		return;
	}

	// Sort tiles and their app indices together by app name.
	QList<int> order;
	for (int i = 0; i < m_applib_indices.size(); i++) {
		order.append(i);
	}
	const bool descending = (m_applib_sort == 1);
	std::sort(order.begin(), order.end(), [this, descending](int a, int b) {
		const int cmp = QString::compare(QString::fromUtf8(MEDIA_APPS[m_applib_indices[a]].name),
		                                 QString::fromUtf8(MEDIA_APPS[m_applib_indices[b]].name),
		                                 Qt::CaseInsensitive);
		return descending ? cmp > 0 : cmp < 0;
	});

	QList<QWidget*> new_tiles;
	QList<int>      new_indices;
	for (int pos: order) {
		new_tiles.append(m_applib_tiles[pos]);
		new_indices.append(m_applib_indices[pos]);
	}
	m_applib_tiles   = new_tiles;
	m_applib_indices = new_indices;

	// Re-place widgets in the row layout in the new order (stretch stays last).
	for (auto* tile: m_applib_tiles) {
		m_applib_row->removeWidget(tile);
	}
	for (int i = 0; i < m_applib_tiles.size(); i++) {
		m_applib_row->insertWidget(i, m_applib_tiles[i]);
	}
}

void Ps5HomeScreen::FocusAppIndex(int index) {
	if (m_applib_tiles.isEmpty()) {
		m_applib_focus = -1;
		return;
	}
	m_applib_focus = qBound(0, index, static_cast<int>(m_applib_tiles.size()) - 1);
	for (int i = 0; i < m_applib_tiles.size(); i++) {
		static_cast<MediaTile*>(m_applib_tiles[i])->SetFocused(i == m_applib_focus);
	}
}

void Ps5HomeScreen::CloseLibrary() {
	if (!m_library_mode) {
		return;
	}
	m_library_mode = false;

	m_library_section->hide();
	m_library_label->hide();
	if (auto* tile = static_cast<SystemTile*>(m_library_tile)) {
		tile->SetFocused(false);
	}

	for (auto* tile: m_pre_library_tiles) {
		tile->show();
	}
	for (auto* tile: m_tiles) {
		tile->show();
	}

	m_hero_gap->show();
	m_hero_name->show();
	m_empty_hint->setVisible(m_items.isEmpty());
	// Hero details/Play visibility depend on whether a game tile is focused.
	FocusRow(m_row_focus);
	setFocus();
	update(); // back to the normal home backdrop
}

void Ps5HomeScreen::RebuildLibraryGrid() {
	for (auto* tile: m_library_tiles) {
		m_library_grid->removeWidget(tile);
		tile->deleteLater();
	}
	m_library_tiles.clear();
	m_library_focus = -1;

	// PS5 sorts by name; the header shows the installed count. Plain
	// case-insensitive comparison keeps the order predictable, with the title id
	// as tie-breaker / fallback for unnamed entries.
	auto items = m_backend->GetItems();
	const bool descending = (m_library_sort == 1);
	std::sort(items.begin(), items.end(),
	          [descending](ConfigurationItem* a, ConfigurationItem* b) {
		          const QString an =
		              a->GetInfo().name.isEmpty() ? a->GetInfo().title_id : a->GetInfo().name;
		          const QString bn =
		              b->GetInfo().name.isEmpty() ? b->GetInfo().title_id : b->GetInfo().name;
		          const int cmp = QString::compare(an.trimmed(), bn.trimmed(), Qt::CaseInsensitive);
		          if (cmp != 0) {
			          return descending ? cmp > 0 : cmp < 0;
		          }
		          const int tie = QString::compare(a->GetInfo().title_id, b->GetInfo().title_id,
		                                           Qt::CaseInsensitive);
		          return descending ? tie > 0 : tie < 0;
	          });
	m_library_storage->setText(tr("Console storage: %1").arg(items.size()));

	for (auto* item: items) {
		auto*     tile  = new LibraryGridTile(item, m_library_host);
		const int index = static_cast<int>(m_library_tiles.size());
		tile->SetOnClicked([this, index]() {
			FocusLibraryIndex(index);
			setFocus();
		});
		tile->SetOnDoubleClicked([this, item]() { LaunchLibraryItem(item); });
		m_library_tiles.append(tile);
	}

	m_library_cols = 0; // force placement
	RelayoutLibraryGrid();
}

void Ps5HomeScreen::RelayoutLibraryGrid() {
	if (m_library_grid == nullptr || m_library_tiles.isEmpty()) {
		return;
	}

	const int tile_w  = LibraryGridTile::SIDE + LibraryGridTile::PAD;
	const int spacing = m_library_grid->horizontalSpacing();
	const int avail   = qMax(tile_w, m_library_scroll->viewport()->width());
	const int cols    = qMax(1, (avail + spacing) / (tile_w + spacing));
	if (cols == m_library_cols) {
		return;
	}
	m_library_cols = cols;

	// Drop stretches from a previous column count before re-placing. Counts are
	// snapshotted first: setRowStretch/setColumnStretch at index == count grows
	// the grid, so looping against the live count never terminates.
	const int old_cols = m_library_grid->columnCount();
	const int old_rows = m_library_grid->rowCount();
	for (int c = 0; c < old_cols; c++) {
		m_library_grid->setColumnStretch(c, 0);
	}
	for (int r = 0; r < old_rows; r++) {
		m_library_grid->setRowStretch(r, 0);
	}

	for (int i = 0; i < m_library_tiles.size(); i++) {
		m_library_grid->removeWidget(m_library_tiles[i]);
		m_library_grid->addWidget(m_library_tiles[i], i / cols, i % cols);
	}
	// Keep the grid packed to the top-left like the console.
	m_library_grid->setColumnStretch(cols, 1);
	m_library_grid->setRowStretch(static_cast<int>((m_library_tiles.size() - 1) / cols) + 1, 1);
}

void Ps5HomeScreen::FocusLibraryIndex(int index) {
	if (m_library_tiles.isEmpty()) {
		m_library_focus = -1;
		return;
	}

	m_library_focus = qBound(0, index, static_cast<int>(m_library_tiles.size()) - 1);
	for (int i = 0; i < m_library_tiles.size(); i++) {
		if (auto* tile = static_cast<LibraryGridTile*>(m_library_tiles[i])) {
			tile->SetFocused(i == m_library_focus);
		}
	}
	m_library_scroll->ensureWidgetVisible(m_library_tiles[m_library_focus], 40, 40);
}

void Ps5HomeScreen::LaunchLibraryItem(ConfigurationItem* item) {
	// Behaves like the console: back to the home row on that game, then launch.
	const int idx = static_cast<int>(m_items.indexOf(item));
	CloseLibrary();
	if (idx >= 0) {
		FocusIndex(idx);
		LaunchFocused();
	}
}


namespace {

// Rounded dark card used across the Explore page (PS5 dashboard look).
QFrame* MakeExploreCard(QWidget* parent, bool outlined = false) {
	auto* card = new QFrame(parent);
	card->setStyleSheet(outlined
	                        ? QStringLiteral("QFrame { background: rgba(16,20,30,190); "
	                                         "border: 2px solid rgba(255,255,255,120); "
	                                         "border-radius: 14px; }")
	                        : QStringLiteral("QFrame { background: rgba(16,20,30,190); "
	                                         "border: none; border-radius: 14px; }"));
	return card;
}

// Donut chart for the Console Storage card: colored segments + free-space text.
class StorageDonut: public QWidget {
public:
	StorageDonut(double used_ratio, const QString& free_text, QWidget* parent)
	    : QWidget(parent), m_used(used_ratio), m_free_text(free_text) {
		setFixedSize(130, 130);
		setStyleSheet(QStringLiteral("background: transparent;"));
	}

protected:
	void paintEvent(QPaintEvent* /*event*/) override {
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);

		const QRectF arc_rect(12, 12, 106, 106);

		// Dim full track first (the free portion shows through it).
		QPen track_pen(QColor(255, 255, 255, 55), 11);
		track_pen.setCapStyle(Qt::FlatCap);
		p.setPen(track_pen);
		p.drawArc(arc_rect, 0, 360 * 16);

		// Used portion split into the PS5 category colors, drawn clockwise
		// from 12 o'clock as consecutive segments.
		struct Seg {
			QColor color;
			double share;
		};
		const Seg segs[] = {{QColor(70, 160, 250), 0.72},  // games and apps
		                    {QColor(160, 90, 250), 0.08},  // media gallery
		                    {QColor(255, 168, 40), 0.20}}; // saved data
		const double used_deg = 360.0 * qBound(0.0, m_used, 1.0);
		double       start    = 90.0; // Qt angles: 0 = 3 o'clock, CCW positive
		for (const auto& seg: segs) {
			const double span = used_deg * seg.share;
			QPen         pen(seg.color, 11);
			pen.setCapStyle(Qt::FlatCap);
			p.setPen(pen);
			p.drawArc(arc_rect, static_cast<int>(start * 16), static_cast<int>(-span * 16));
			start -= span;
		}

		// Free-space figure centered inside the ring, "Free" underneath.
		p.setPen(QColor(255, 255, 255));
		QFont f = font();
		f.setPointSize(12);
		f.setBold(true);
		p.setFont(f);
		p.drawText(arc_rect.adjusted(0, -8, 0, -8), Qt::AlignCenter, m_free_text);
		f.setPointSize(9);
		f.setBold(false);
		p.setFont(f);
		p.setPen(QColor(230, 238, 252, 200));
		p.drawText(arc_rect.adjusted(0, 22, 0, 22), Qt::AlignCenter, QStringLiteral("Free"));
	}

private:
	double  m_used = 0.5;
	QString m_free_text;
};

} // namespace

void Ps5HomeScreen::OpenExplore() {
	if (m_explore_mode) {
		return;
	}
	m_explore_mode = true;

	// Like the Game Library: the tile row collapses to the Explore tile with its
	// caption, the hero area hides, and the dashboard opens below the row.
	for (auto* tile: m_pre_library_tiles) {
		tile->setVisible(tile == m_explore_tile);
	}
	for (auto* tile: m_tiles) {
		tile->hide();
	}
	m_library_tile->hide();
	static_cast<SystemTile*>(m_explore_tile)->SetFocused(true);
	m_explore_label->show();

	m_hero_name->hide();
	m_hero_meta->hide();
	m_hero_status->hide();
	m_empty_hint->hide();
	m_play_btn->hide();
	m_trophy_card->hide();
	m_hero_gap->hide();

	// Rebuilt on every open so trophy/storage numbers stay current.
	if (m_explore_page != nullptr) {
		m_explore_page->deleteLater();
	}
	m_explore_page = new QWidget(this);
	m_explore_page->setStyleSheet(QStringLiteral("background: transparent;"));

	auto* page_layout = new QVBoxLayout(m_explore_page);
	page_layout->setContentsMargins(0, 30, 0, 30);
	page_layout->setSpacing(0);

	auto* welcome = new QLabel(tr("Welcome back, Player"), m_explore_page);
	welcome->setStyleSheet(QStringLiteral(
	    "color: rgba(240,246,255,235); font-size: 26px; background: transparent;"));
	page_layout->addWidget(welcome);

	page_layout->addSpacing(16);

	// Aggregate real trophy counts across every installed game.
	int t_plat = 0, t_gold = 0, t_silver = 0, t_bronze = 0, t_total = 0, t_earned = 0;
	for (auto* item: m_backend->GetItems()) {
		const auto c = TrophyViewerDialog::CountTrophies(&item->GetInfo());
		if (c.valid) {
			t_plat += c.earned_platinum;
			t_gold += c.earned_gold;
			t_silver += c.earned_silver;
			t_bronze += c.earned_bronze;
			t_total += c.total;
			t_earned += c.earned_total;
		}
	}
	const int t_percent = t_total > 0 ? t_earned * 100 / t_total : 0;

	auto* columns = new QHBoxLayout();
	columns->setSpacing(24);

	// LEFT column: trophy card (outlined = focused, like the reference),
	// DualSense card, Console Storage card.
	auto* left = new QVBoxLayout();
	left->setSpacing(20);

	auto* trophy_card = MakeExploreCard(m_explore_page, true);
	{
		auto* lay = new QVBoxLayout(trophy_card);
		lay->setContentsMargins(24, 18, 24, 18);
		lay->setSpacing(12);

		auto* counts_row = new QHBoxLayout();
		counts_row->setSpacing(26);
		const auto add_grade = [&](const QString& img, int n) {
			auto* icon = new QLabel(trophy_card);
			icon->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
			icon->setPixmap(QPixmap(img).scaled(18, 24, Qt::KeepAspectRatio,
			                                    Qt::SmoothTransformation));
			auto* num = new QLabel(QString::number(n), trophy_card);
			num->setStyleSheet(QStringLiteral(
			    "color: #ffffff; font-size: 17px; font-weight: 600; background: transparent; "
			    "border: none;"));
			counts_row->addWidget(icon);
			counts_row->addWidget(num);
			counts_row->addSpacing(6);
		};
		add_grade(QStringLiteral(":/ps5/trophy_plat.png"), t_plat);
		add_grade(QStringLiteral(":/ps5/trophy_gold.png"), t_gold);
		add_grade(QStringLiteral(":/ps5/trophy_silver.png"), t_silver);
		add_grade(QStringLiteral(":/ps5/trophy_bronze.png"), t_bronze);
		counts_row->addStretch(1);
		lay->addLayout(counts_row);

		auto* level_row = new QHBoxLayout();
		auto* level_label = new QLabel(tr("Next trophy level"), trophy_card);
		level_label->setStyleSheet(QStringLiteral(
		    "color: rgba(230,238,252,200); font-size: 15px; background: transparent; "
		    "border: none;"));
		auto* percent_label = new QLabel(QStringLiteral("%1%").arg(t_percent), trophy_card);
		percent_label->setStyleSheet(QStringLiteral(
		    "color: #ffffff; font-size: 15px; font-weight: 600; background: transparent; "
		    "border: none;"));
		level_row->addWidget(level_label);
		level_row->addStretch(1);
		level_row->addWidget(percent_label);
		lay->addLayout(level_row);

		auto* bar = new QFrame(trophy_card);
		bar->setFixedHeight(4);
		bar->setStyleSheet(QStringLiteral(
		    "QFrame { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, "
		    "stop:0 #ffffff, stop:%1 #ffffff, stop:%2 rgba(255,255,255,60), "
		    "stop:1 rgba(255,255,255,60)); border: none; border-radius: 2px; }")
		                       .arg(qMax(0.01, t_percent / 100.0 - 0.001))
		                       .arg(qMin(0.999, t_percent / 100.0 + 0.001)));
		lay->addWidget(bar);
	}
	left->addWidget(trophy_card);

	auto* pad_card = MakeExploreCard(m_explore_page);
	{
		auto* lay = new QHBoxLayout(pad_card);
		lay->setContentsMargins(24, 16, 24, 16);
		lay->setSpacing(18);

		// Blue-ringed controller badge.
		auto* badge = new QLabel(QString(QChar(0xE7FC)), pad_card);
		badge->setFixedSize(64, 64);
		badge->setAlignment(Qt::AlignCenter);
		badge->setStyleSheet(QStringLiteral(
		    "color: #ffffff; font-family: 'Segoe Fluent Icons','Segoe MDL2 Assets'; "
		    "font-size: 24px; background: rgba(10,14,22,220); border: 4px solid #35a0f0; "
		    "border-radius: 32px;"));
		lay->addWidget(badge);

		auto* name = new QLabel(tr("DualSense"), pad_card);
		name->setStyleSheet(QStringLiteral(
		    "color: #ffffff; font-size: 17px; font-weight: 600; background: transparent; "
		    "border: none;"));
		lay->addWidget(name);
		lay->addStretch(1);
	}
	left->addWidget(pad_card);

	auto* storage_card = MakeExploreCard(m_explore_page);
	storage_card->setMinimumHeight(190); // room for heading + legend + donut
	{
		auto* lay = new QHBoxLayout(storage_card);
		lay->setContentsMargins(24, 18, 24, 18);
		lay->setSpacing(12);

		auto* text_col = new QVBoxLayout();
		text_col->setSpacing(10);
		auto* head_row = new QHBoxLayout();
		auto* disk_glyph = new QLabel(QString(QChar(0xEDA2)), storage_card);
		disk_glyph->setStyleSheet(QStringLiteral(
		    "color: #ffffff; font-family: 'Segoe Fluent Icons','Segoe MDL2 Assets'; "
		    "font-size: 17px; background: transparent; border: none;"));
		auto* head = new QLabel(tr("Console Storage"), storage_card);
		head->setStyleSheet(QStringLiteral(
		    "color: #ffffff; font-size: 17px; font-weight: 600; background: transparent; "
		    "border: none;"));
		head_row->addWidget(disk_glyph);
		head_row->addSpacing(8);
		head_row->addWidget(head);
		head_row->addStretch(1);
		text_col->addLayout(head_row);

		const auto add_legend = [&](const QColor& color, const QString& label) {
			auto* row = new QHBoxLayout();
			auto* dot = new QLabel(storage_card);
			dot->setFixedSize(10, 10);
			dot->setStyleSheet(QStringLiteral("background: %1; border-radius: 5px; border: none;")
			                       .arg(color.name()));
			auto* text = new QLabel(label, storage_card);
			text->setStyleSheet(QStringLiteral(
			    "color: rgba(230,238,252,210); font-size: 14px; background: transparent; "
			    "border: none;"));
			row->addWidget(dot);
			row->addSpacing(8);
			row->addWidget(text);
			row->addStretch(1);
			text_col->addLayout(row);
		};
		add_legend(QColor(70, 160, 250), tr("Games and Apps"));
		add_legend(QColor(160, 90, 250), tr("Media Gallery"));
		add_legend(QColor(255, 168, 40), tr("Saved Data"));
		lay->addLayout(text_col, 1);

		// Real free space of the drive hosting the emulator.
		const QStorageInfo storage(QCoreApplication::applicationDirPath());
		const double total_gb = static_cast<double>(storage.bytesTotal()) / (1024.0 * 1024 * 1024);
		const double free_gb  = static_cast<double>(storage.bytesFree()) / (1024.0 * 1024 * 1024);
		const double used     = total_gb > 0 ? 1.0 - free_gb / total_gb : 0.5;
		lay->addWidget(new StorageDonut(used, QStringLiteral("%1 GB").arg(free_gb, 0, 'f', 1),
		                                storage_card));
	}
	left->addWidget(storage_card);
	left->addStretch(1);
	columns->addLayout(left, 36);

	// MIDDLE column: Game Captures card backed by the focused game's pic0.
	auto* captures_card = new QFrame(m_explore_page);
	{
		QString art;
		if (m_focus >= 0 && m_focus < m_items.size()) {
			const auto& info = m_items[m_focus]->GetInfo();
			if (!info.basedir.isEmpty()) {
				const QString pic0 =
				    QDir(info.basedir).filePath(QStringLiteral("sce_sys/pic0.png"));
				if (QFileInfo::exists(pic0)) {
					art = pic0;
				}
			}
		}
		captures_card->setStyleSheet(
		    QStringLiteral("QFrame { background: rgba(12,16,26,220); border: none; "
		                   "border-radius: 14px; }"));
		auto* lay = new QVBoxLayout(captures_card);
		lay->setContentsMargins(20, 20, 20, 20);
		if (!art.isEmpty()) {
			auto* img = new QLabel(captures_card);
			QPixmap pm(art);
			img->setPixmap(pm.scaled(560, 560, Qt::KeepAspectRatio, Qt::SmoothTransformation));
			img->setAlignment(Qt::AlignCenter);
			img->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
			lay->addWidget(img, 1);
		} else {
			lay->addStretch(1);
		}
		auto* cap_row = new QHBoxLayout();
		auto* cap_glyph = new QLabel(QString(QChar(0xE722)), captures_card);
		cap_glyph->setStyleSheet(QStringLiteral(
		    "color: #ffffff; font-family: 'Segoe Fluent Icons','Segoe MDL2 Assets'; "
		    "font-size: 17px; background: transparent; border: none;"));
		auto* cap_text = new QLabel(tr("Game Captures"), captures_card);
		cap_text->setStyleSheet(QStringLiteral(
		    "color: #ffffff; font-size: 17px; font-weight: 600; background: transparent; "
		    "border: none;"));
		cap_row->addWidget(cap_glyph);
		cap_row->addSpacing(10);
		cap_row->addWidget(cap_text);
		cap_row->addStretch(1);
		lay->addLayout(cap_row);
	}
	columns->addWidget(captures_card, 40);

	// RIGHT column: "What's New" banner card.
	auto* right = new QVBoxLayout();
	right->setSpacing(20);
	auto* news_card = new QFrame(m_explore_page);
	news_card->setStyleSheet(
	    QStringLiteral("QFrame { background: qlineargradient(x1:0,y1:0,x2:1,y2:1, "
	                   "stop:0 #1b6fe8, stop:1 #0f4dbb); border: none; border-radius: 14px; }"));
	{
		auto* lay = new QVBoxLayout(news_card);
		lay->setContentsMargins(24, 22, 24, 22);
		lay->setSpacing(8);
		auto* head = new QLabel(tr("What's New"), news_card);
		head->setStyleSheet(QStringLiteral(
		    "color: #ffffff; font-size: 20px; font-weight: 700; background: transparent; "
		    "border: none;"));
		auto* sub = new QLabel(tr("Discover PS5 tips and updates"), news_card);
		sub->setStyleSheet(QStringLiteral(
		    "color: rgba(235,242,255,220); font-size: 14px; background: transparent; "
		    "border: none;"));
		lay->addWidget(head);
		lay->addWidget(sub);
		lay->addStretch(1);
	}
	news_card->setMinimumHeight(150);
	right->addWidget(news_card);
	right->addStretch(1);
	columns->addLayout(right, 24);

	page_layout->addLayout(columns, 1);

	// Slot the dashboard into the home layout right below the tile row, where
	// the library grid also lives.
	if (auto* root = qobject_cast<QVBoxLayout*>(layout())) {
		const int at = root->indexOf(m_library_section);
		root->insertWidget(at >= 0 ? at : root->count(), m_explore_page, 1);
	}
	m_explore_page->show();
	setFocus();
	update();
}

void Ps5HomeScreen::CloseExplore() {
	if (!m_explore_mode) {
		return;
	}
	m_explore_mode = false;

	if (m_explore_page != nullptr) {
		if (auto* root = qobject_cast<QVBoxLayout*>(layout())) {
			root->removeWidget(m_explore_page);
		}
		m_explore_page->deleteLater();
		m_explore_page = nullptr;
	}

	m_explore_label->hide();
	static_cast<SystemTile*>(m_explore_tile)->SetFocused(false);
	for (auto* tile: m_pre_library_tiles) {
		tile->show();
	}
	for (auto* tile: m_tiles) {
		tile->show();
	}
	m_library_tile->show();

	m_hero_gap->show();
	m_hero_name->show();
	m_empty_hint->setVisible(m_items.isEmpty());
	FocusRow(m_row_focus); // restores hero details for the focused tile kind
	setFocus();
	update();
}

void Ps5HomeScreen::OpenSearch() {
	if (m_search_mode) {
		return;
	}
	m_search_mode = true;

	if (m_search_page == nullptr) {
		// Built lazily on first open; overlay covering the whole home screen.
		// Opaque page painting the settings light-beam wallpaper itself.
		m_search_page = new WallpaperPage(m_home_bg, this);
		auto* page_layout = new QVBoxLayout(m_search_page);
		page_layout->setContentsMargins(120, 44, 120, 24);
		page_layout->setSpacing(0);

		// Filter tabs: All / Games / Media / Players — clicking scopes the results.
		auto* chips = new QHBoxLayout();
		chips->setSpacing(56);
		const char* chip_names[] = {"All", "Games", "Media", "Players"};
		for (int i = 0; i < 4; i++) {
			auto* chip = new QPushButton(tr(chip_names[i]), m_search_page);
			chip->setCursor(Qt::PointingHandCursor);
			chip->setFocusPolicy(Qt::NoFocus);
			chip->setFlat(true);
			connect(chip, &QPushButton::clicked, this, [this, i]() {
				m_search_cat = i;
				for (int c = 0; c < m_search_chips.size(); c++) {
					m_search_chips[c]->setStyleSheet(
					    c == m_search_cat
					        ? QStringLiteral("QPushButton { color: #ffffff; font-size: 22px; "
					                         "font-weight: 600; background: transparent; "
					                         "border: none; padding: 0; }")
					        : QStringLiteral(
					              "QPushButton { color: rgba(230,238,252,140); font-size: 22px; "
					              "background: transparent; border: none; padding: 0; }"
					              "QPushButton:hover { color: rgba(240,246,255,200); }"));
				}
				UpdateSearchResults();
			});
			m_search_chips.append(chip);
			chips->addWidget(chip);
		}
		// Apply the initial selected/idle styles.
		for (int c = 0; c < m_search_chips.size(); c++) {
			m_search_chips[c]->setStyleSheet(
			    c == 0 ? QStringLiteral("QPushButton { color: #ffffff; font-size: 22px; "
			                            "font-weight: 600; background: transparent; border: none; "
			                            "padding: 0; }")
			           : QStringLiteral(
			                 "QPushButton { color: rgba(230,238,252,140); font-size: 22px; "
			                 "background: transparent; border: none; padding: 0; }"
			                 "QPushButton:hover { color: rgba(240,246,255,200); }"));
		}
		chips->addStretch(1);
		page_layout->addLayout(chips);

		page_layout->addSpacing(30);

		// Big rounded search field with the magnifier glyph inside.
		m_search_field = new QLineEdit(m_search_page);
		m_search_field->setPlaceholderText(tr("Search for games, movies, TV shows, players, and apps"));
		m_search_field->setFixedHeight(74);
		m_search_field->setClearButtonEnabled(true);
		{
			QFont f = m_search_field->font();
			f.setPointSize(17);
			m_search_field->setFont(f);
		}
		m_search_field->setStyleSheet(QStringLiteral(
		    "QLineEdit { background: rgba(10,14,24,110); color: #ffffff; "
		    "border: 2px solid rgba(255,255,255,200); border-radius: 8px; padding: 0 24px; }"
		    "QLineEdit:focus { border: 2px solid #ffffff; }"));
		connect(m_search_field, &QLineEdit::textChanged, this,
		        &Ps5HomeScreen::UpdateSearchResults);
		page_layout->addWidget(m_search_field);

		page_layout->addSpacing(26);

		m_search_heading = new QLabel(tr("Trending"), m_search_page);
		m_search_heading->setStyleSheet(
		    QStringLiteral("color: rgba(235,242,255,225); font-size: 20px;"));
		page_layout->addWidget(m_search_heading);

		page_layout->addSpacing(16);

		// Results: scrollable grid of game covers (reuses the library tile look).
		auto* scroll = new QScrollArea(m_search_page);
		scroll->setWidgetResizable(true);
		scroll->setFrameShape(QFrame::NoFrame);
		scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		scroll->setFocusPolicy(Qt::NoFocus);
		scroll->setStyleSheet(QStringLiteral(
		    "QScrollArea { background: transparent; border: none; }"
		    "QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }"
		    "QScrollBar::handle:vertical { background: rgba(200,210,230,110); "
		    "min-height: 40px; border-radius: 5px; }"
		    "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"));
		m_search_host = new QWidget(scroll);
		m_search_host->setStyleSheet(QStringLiteral("background: transparent;"));
		m_search_grid = new QGridLayout(m_search_host);
		m_search_grid->setContentsMargins(0, 0, 0, 0);
		m_search_grid->setHorizontalSpacing(18);
		m_search_grid->setVerticalSpacing(18);
		scroll->setWidget(m_search_host);
		page_layout->addWidget(scroll, 1);
	}

	m_search_page->setGeometry(rect());
	m_search_field->clear();
	UpdateSearchResults();
	m_search_page->show();
	m_search_page->raise();
	m_search_field->setFocus();
	update();
}

void Ps5HomeScreen::CloseSearch() {
	if (!m_search_mode) {
		return;
	}
	m_search_mode = false;
	m_search_page->hide();
	setFocus();
	update();
}

void Ps5HomeScreen::UpdateSearchResults() {
	// Rebuild the cover grid from the query; empty query = every game ("Trending").
	for (auto* tile: m_search_tiles) {
		m_search_grid->removeWidget(tile);
		tile->deleteLater();
	}
	m_search_tiles.clear();

	const QString query = m_search_field->text().trimmed();
	m_search_heading->setText(query.isEmpty() ? tr("Trending") : tr("Search Results"));

	// Games: installed games matching the query (categories All / Games).
	QList<ConfigurationItem*> matches;
	if (m_search_cat == 0 || m_search_cat == 1) {
		for (auto* item: m_backend->GetItems()) {
			const auto& info = item->GetInfo();
			if (query.isEmpty() || info.name.contains(query, Qt::CaseInsensitive) ||
			    info.title_id.contains(query, Qt::CaseInsensitive)) {
				matches.append(item);
			}
		}
	}

	// Media apps matching the query (categories All / Media).
	QList<int> app_matches;
	if (m_search_cat == 0 || m_search_cat == 2) {
		for (int i = 0; i < MEDIA_APP_COUNT; i++) {
			if (i == 0 || i == APPLIB_INDEX) {
				continue; // section tiles, not launchable apps
			}
			if (query.isEmpty() ||
			    QString::fromUtf8(MEDIA_APPS[i].name).contains(query, Qt::CaseInsensitive)) {
				app_matches.append(i);
			}
		}
	}

	const int tile_w   = LibraryGridTile::SIDE + LibraryGridTile::PAD;
	const int spacing  = m_search_grid->horizontalSpacing();
	const int avail    = qMax(tile_w, width() - 240);
	const int cols     = qMax(1, (avail + spacing) / (tile_w + spacing));

	int cell = 0;
	for (auto* item: matches) {
		auto* tile = new LibraryGridTile(item, m_search_host);
		tile->SetOnClicked([this, item]() {
			// Launch from search, like picking a result on the console.
			const int idx = static_cast<int>(m_items.indexOf(item));
			CloseSearch();
			if (idx >= 0) {
				FocusIndex(idx);
				LaunchFocused();
			}
		});
		m_search_grid->addWidget(tile, cell / cols, cell % cols);
		tile->show();
		m_search_tiles.append(tile);
		cell++;
	}
	for (int app: app_matches) {
		auto* tile = new MediaTile(app, m_search_host);
		tile->SetOnClicked([this, app]() {
			CloseSearch();
			ShowMediaTab();
			SetMediaFocus(app);
			StartFocusedMediaApp();
		});
		m_search_grid->addWidget(tile, cell / cols, cell % cols);
		tile->show();
		m_search_tiles.append(tile);
		cell++;
	}

	// Players: nothing to search offline — the heading alone communicates it.
	if (m_search_cat == 3) {
		m_search_heading->setText(tr("No players (offline)"));
	}

	m_search_grid->setColumnStretch(cols, 1);
	if (cell > 0) {
		m_search_grid->setRowStretch((cell - 1) / cols + 1, 1);
	}
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
	// Explore page: any dismiss key returns to the dashboard.
	if (m_explore_mode) {
		switch (event->key()) {
			case Qt::Key_Escape:
			case Qt::Key_Return:
			case Qt::Key_Enter: CloseExplore(); return;
			case Qt::Key_F11: ToggleFullscreen(); return;
			default: return;
		}
	}

	// Search overlay: Esc closes (unhandled keys from the QLineEdit bubble here).
	if (m_search_mode) {
		if (event->key() == Qt::Key_Escape) {
			CloseSearch();
			return;
		}
		QWidget::keyPressEvent(event);
		return;
	}

	// App Library view inside the Media tab: arrows walk the app grid.
	if (m_applib_mode) {
		switch (event->key()) {
			case Qt::Key_Escape: CloseAppLibrary(); return;
			case Qt::Key_Left: FocusAppIndex(m_applib_focus - 1); return;
			case Qt::Key_Right: FocusAppIndex(m_applib_focus + 1); return;
			case Qt::Key_Return:
			case Qt::Key_Enter:
			case Qt::Key_X:
				if (m_applib_focus >= 0 && m_applib_focus < m_applib_indices.size()) {
					SetMediaFocus(m_applib_indices[m_applib_focus]);
					StartFocusedMediaApp();
				}
				return;
			case Qt::Key_F11: ToggleFullscreen(); return;
			default: return;
		}
	}

	// Media tab: left/right walk the app row, Enter starts, Esc returns to Games.
	if (m_media_mode) {
		switch (event->key()) {
			case Qt::Key_Left: SetMediaFocus(m_media_focus - 1); return;
			case Qt::Key_Right: SetMediaFocus(m_media_focus + 1); return;
			case Qt::Key_Return:
			case Qt::Key_Enter:
			case Qt::Key_X: StartFocusedMediaApp(); return;
			case Qt::Key_Escape: ShowGamesTab(); return;
			case Qt::Key_F11: ToggleFullscreen(); return;
			default: return;
		}
	}

	// In library mode the arrows walk the grid; Esc/L go back to the plain home.
	if (m_library_mode) {
		switch (event->key()) {
			case Qt::Key_Escape:
			case Qt::Key_L: CloseLibrary(); return;
			case Qt::Key_Left: FocusLibraryIndex(m_library_focus - 1); return;
			case Qt::Key_Right: FocusLibraryIndex(m_library_focus + 1); return;
			case Qt::Key_Up: FocusLibraryIndex(m_library_focus - m_library_cols); return;
			case Qt::Key_Down: FocusLibraryIndex(m_library_focus + m_library_cols); return;
			case Qt::Key_Return:
			case Qt::Key_Enter:
			case Qt::Key_X:
				if (m_library_focus >= 0 && m_library_focus < m_library_tiles.size()) {
					if (auto* tile =
					        static_cast<LibraryGridTile*>(m_library_tiles[m_library_focus])) {
						LaunchLibraryItem(tile->Item());
					}
				}
				return;
			case Qt::Key_F11: ToggleFullscreen(); return;
			default: return;
		}
	}

	switch (event->key()) {
		case Qt::Key_Left: FocusRow(m_row_focus - 1); return;
		case Qt::Key_Right: FocusRow(m_row_focus + 1); return;
		case Qt::Key_Return:
		case Qt::Key_Enter:
		case Qt::Key_X: ActivateRow(m_row_focus); return;
		case Qt::Key_S: OpenSettingsForFocused(); return;
		case Qt::Key_L: OpenLibrary(); return;
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

	// Library views (Game Library / App Library) use the settings light-beam
	// wallpaper regardless of the focused game/app, like a distinct system page.
	if ((m_library_mode || m_applib_mode || m_explore_mode) && !m_home_bg.isNull()) {
		const auto cover = [this](const QPixmap& src, QPixmap& cache, QSize& made_for) {
			if (made_for != size() || cache.isNull()) {
				cache    = src.scaled(size(), Qt::KeepAspectRatioByExpanding,
				                      Qt::SmoothTransformation);
				made_for = size();
			}
		};
		cover(m_home_bg, m_home_bg_scaled, m_home_bg_for);
		const int x = (m_home_bg_scaled.width() - width()) / 2;
		const int y = (m_home_bg_scaled.height() - height()) / 2;
		p.drawPixmap(0, 0, m_home_bg_scaled, x, y, width(), height());

		// Stronger darkening than the plain home: keeps the grid readable.
		QLinearGradient fade(0, 0, 0, height());
		fade.setColorAt(0.0, QColor(8, 12, 24, 90));
		fade.setColorAt(1.0, QColor(8, 12, 24, 170));
		p.fillRect(rect(), fade);
		return;
	}

	// Media tab: full-screen gradient themed to the focused app (Apple Music
	// red, Spotify green, ...), like the console.
	if (m_media_mode) {
		const auto&     app = MEDIA_APPS[m_media_focus];
		QLinearGradient bg(0, 0, width(), height());
		bg.setColorAt(0.0, app.bg_a);
		bg.setColorAt(1.0, app.bg_b);
		p.fillRect(rect(), bg);
		return;
	}

	// Backdrops are cached at the current window size: cover-scaling a large
	// image (or re-loading pic0 from disk) on every repaint tanks the frame
	// rate of the intro/focus animations.

	// Focused game's pic0, if any, becomes the backdrop, PS5-style.
	QString pic0_path;
	if (m_focus >= 0 && m_focus < m_items.size()) {
		const auto& info = m_items[m_focus]->GetInfo();
		if (!info.basedir.isEmpty()) {
			const QString pic0 =
			    QDir(info.basedir).filePath(QStringLiteral("sce_sys/pic0.png"));
			if (QFileInfo::exists(pic0)) {
				pic0_path = pic0;
			}
		}
	}
	if (pic0_path != m_backdrop_path) {
		m_backdrop_path = pic0_path;
		m_backdrop_src  = QPixmap();
		m_backdrop_scaled = QPixmap();
		if (!m_backdrop_path.isEmpty()) {
			m_backdrop_src.load(m_backdrop_path);
		}
	}

	const auto cover_scaled = [this](const QPixmap& src, QPixmap& cache, QSize& made_for) {
		if (made_for != size() || cache.isNull()) {
			cache = src.scaled(size(), Qt::KeepAspectRatioByExpanding,
			                   Qt::SmoothTransformation);
			made_for = size();
		}
	};

	if (m_backdrop_src.isNull()) {
		// No game selected / no artwork: PS5 light-beam wallpaper (same as settings).
		if (!m_home_bg.isNull()) {
			cover_scaled(m_home_bg, m_home_bg_scaled, m_home_bg_for);
			const int x = (m_home_bg_scaled.width() - width()) / 2;
			const int y = (m_home_bg_scaled.height() - height()) / 2;
			p.drawPixmap(0, 0, m_home_bg_scaled, x, y, width(), height());

			// Gentle bottom darkening so tiles and text stay legible.
			QLinearGradient fade(0, 0, 0, height());
			fade.setColorAt(0.0, QColor(8, 12, 24, 40));
			fade.setColorAt(1.0, QColor(8, 12, 24, 140));
			p.fillRect(rect(), fade);
			return;
		}

		// Fallback: base PS5 blue gradient.
		QLinearGradient bg(0, 0, width(), height());
		bg.setColorAt(0.0, QColor(16, 24, 46));
		bg.setColorAt(0.5, QColor(23, 38, 74));
		bg.setColorAt(1.0, QColor(12, 18, 36));
		p.fillRect(rect(), bg);
		return;
	}

	// Base PS5 blue gradient under the dimmed game art.
	QLinearGradient bg(0, 0, width(), height());
	bg.setColorAt(0.0, QColor(16, 24, 46));
	bg.setColorAt(0.5, QColor(23, 38, 74));
	bg.setColorAt(1.0, QColor(12, 18, 36));
	p.fillRect(rect(), bg);

	cover_scaled(m_backdrop_src, m_backdrop_scaled, m_backdrop_for);
	p.setOpacity(0.35);
	p.drawPixmap((width() - m_backdrop_scaled.width()) / 2,
	             (height() - m_backdrop_scaled.height()) / 2, m_backdrop_scaled);
	p.setOpacity(1.0);

	QLinearGradient fade(0, 0, 0, height());
	fade.setColorAt(0.0, QColor(10, 16, 32, 120));
	fade.setColorAt(1.0, QColor(10, 16, 32, 235));
	p.fillRect(rect(), fade);
}

void Ps5HomeScreen::PlayIntroAnimation() {
	// PS5 boot reveal, matching the real console: the game row slides in from the
	// right first, then the top bar drops down from above, and finally the hero
	// texts, Play button and trophy card fade in.
	const auto animate = [this](QWidget* w, QPoint offset, int delay, int duration) {
		if (w == nullptr || !w->isVisible()) {
			return;
		}
		auto* eff = new QGraphicsOpacityEffect(w);
		w->setGraphicsEffect(eff);
		eff->setOpacity(0.0);

		const QPoint end   = w->pos();
		const QPoint start = end + offset;
		w->move(start);

		auto* pos = new QPropertyAnimation(w, "pos", w);
		pos->setStartValue(start);
		pos->setEndValue(end);
		pos->setDuration(duration);
		pos->setEasingCurve(QEasingCurve::OutCubic);

		auto* fade = new QPropertyAnimation(eff, "opacity", eff);
		fade->setStartValue(0.0);
		fade->setEndValue(1.0);
		fade->setDuration(duration);
		fade->setEasingCurve(QEasingCurve::OutCubic);

		auto* group = new QParallelAnimationGroup();
		group->addAnimation(pos);
		group->addAnimation(fade);
		connect(group, &QParallelAnimationGroup::finished, w, [w]() {
			// Back to plain rendering: effects slightly soften text while active.
			w->setGraphicsEffect(nullptr);
		});

		if (delay > 0) {
			auto* seq = new QSequentialAnimationGroup(w);
			seq->addPause(delay);
			seq->addAnimation(group);
			seq->start(QAbstractAnimation::DeleteWhenStopped);
		} else {
			group->setParent(w);
			group->start(QAbstractAnimation::DeleteWhenStopped);
		}
	};

	animate(m_scroll, QPoint(240, 0), 0, 650);    // game row: right -> left
	animate(m_top_bar, QPoint(0, -60), 300, 500); // top bar: above -> down
	for (QWidget* w: std::initializer_list<QWidget*> {m_hero_name, m_hero_meta, m_hero_status,
	                                                  m_empty_hint, m_play_btn, m_trophy_card}) {
		animate(w, QPoint(0, 0), 700, 500); // hero info: fade in place
	}
}

void Ps5HomeScreen::resizeEvent(QResizeEvent* event) {
	QWidget::resizeEvent(event);
	if (m_settings_page != nullptr) {
		m_settings_page->setGeometry(rect());
	}
	if (m_library_mode) {
		RelayoutLibraryGrid();
	}
	if (m_search_page != nullptr && m_search_mode) {
		m_search_page->setGeometry(rect());
		UpdateSearchResults();
	}
	update();
}
