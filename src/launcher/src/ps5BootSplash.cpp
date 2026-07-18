#include "ps5BootSplash.h"

#include <QEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>

#ifdef _WIN32

// MFPlay is officially deprecated but ships with every Windows 10/11 and is the
// lightest way to play an mp4 (video + audio) into a raw HWND without bundling
// Qt Multimedia / ffmpeg. Silence the deprecation noise around the include.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#pragma warning(push)
#pragma warning(disable : 4996)

#include <windows.h>

#include <mferror.h>
#include <mfplay.h>

// COM callback bridging MFPlay events to the splash widget. MFPlay raises its
// events on the thread that created the player (our GUI thread), so touching
// the widget directly here is safe.
class Ps5BootSplashPlayer final: public IMFPMediaPlayerCallback {
public:
	explicit Ps5BootSplashPlayer(Ps5BootSplash* owner): m_owner(owner) {}

	Ps5BootSplashPlayer(const Ps5BootSplashPlayer&)            = delete;
	Ps5BootSplashPlayer& operator=(const Ps5BootSplashPlayer&) = delete;

	bool Start(const QString& path, HWND hwnd) {
		const auto    wpath = path.toStdWString();
		const HRESULT hr =
		    MFPCreateMediaPlayer(wpath.c_str(), TRUE /* start playback */, 0, this, hwnd, &m_player);
		return SUCCEEDED(hr) && m_player != nullptr;
	}

	void Shutdown() {
		m_owner = nullptr;
		if (m_player != nullptr) {
			m_player->Shutdown();
			m_player->Release();
			m_player = nullptr;
		}
	}

	void UpdateVideo() {
		if (m_player != nullptr) {
			m_player->UpdateVideo();
		}
	}

	// "Cover" fit: crop the source so its aspect matches the window, then let it
	// stretch edge-to-edge. No letterbox/pillarbox bars at any window size.
	void ApplyCoverCrop(int win_w, int win_h) {
		if (m_player == nullptr || win_w <= 0 || win_h <= 0) {
			return;
		}
		SIZE native = {0, 0};
		SIZE ar     = {0, 0};
		if (FAILED(m_player->GetNativeVideoSize(&native, &ar))) {
			return;
		}
		const LONG vw = (ar.cx > 0) ? ar.cx : native.cx;
		const LONG vh = (ar.cy > 0) ? ar.cy : native.cy;
		if (vw <= 0 || vh <= 0) {
			return;
		}

		const double video_ar = static_cast<double>(vw) / static_cast<double>(vh);
		const double win_ar   = static_cast<double>(win_w) / static_cast<double>(win_h);

		MFVideoNormalizedRect src = {0.0F, 0.0F, 1.0F, 1.0F};
		if (win_ar > video_ar) {
			// Window is wider: crop top/bottom.
			const auto h = static_cast<float>(video_ar / win_ar);
			src.top      = (1.0F - h) / 2.0F;
			src.bottom   = src.top + h;
		} else if (win_ar < video_ar) {
			// Window is taller: crop left/right.
			const auto w = static_cast<float>(win_ar / video_ar);
			src.left     = (1.0F - w) / 2.0F;
			src.right    = src.left + w;
		}

		m_player->SetAspectRatioMode(MFVideoARMode_None);
		m_player->SetVideoSourceRect(&src);
		m_player->UpdateVideo();
	}

	// IUnknown
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
		if (ppv == nullptr) {
			return E_POINTER;
		}
		if (riid == IID_IUnknown || riid == __uuidof(IMFPMediaPlayerCallback)) {
			*ppv = static_cast<IMFPMediaPlayerCallback*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}

	STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }

	STDMETHODIMP_(ULONG) Release() override {
		const ULONG r = InterlockedDecrement(&m_ref);
		if (r == 0) {
			delete this;
		}
		return r;
	}

	void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* event_header) override {
		if (event_header == nullptr || m_owner == nullptr) {
			return;
		}
		// The video size becomes known once the media item is set: fit it now.
		if (event_header->eEventType == MFP_EVENT_TYPE_MEDIAITEM_SET) {
			ApplyCoverCrop(m_owner->width(), m_owner->height());
		}
		// End of the clip or any playback error: hand over to the main window.
		if (event_header->eEventType == MFP_EVENT_TYPE_PLAYBACK_ENDED ||
		    FAILED(event_header->hrEvent)) {
			m_owner->Finish();
		}
	}

private:
	~Ps5BootSplashPlayer() = default;

	Ps5BootSplash*   m_owner  = nullptr;
	IMFPMediaPlayer* m_player = nullptr;
	LONG             m_ref    = 1;
};

// Self-owning audio-only MFPlay player for the home-screen music: loops until
// stopped from the settings toggle.
class Ps5HomeMusicPlayer final: public IMFPMediaPlayerCallback {
public:
	static Ps5HomeMusicPlayer* g_current;

	static void Start(const QString& path) {
		if (g_current != nullptr) {
			return; // already playing
		}
		auto*            music  = new Ps5HomeMusicPlayer();
		const auto       wpath  = path.toStdWString();
		IMFPMediaPlayer* player = nullptr;
		const HRESULT    hr =
		    MFPCreateMediaPlayer(wpath.c_str(), TRUE /* start playback */, 0, music, nullptr, &player);
		if (FAILED(hr) || player == nullptr) {
			music->Release();
			return;
		}
		player->SetVolume(0.45F); // ambient bed under the UI, PS5-style
		music->m_player = player;
		g_current       = music;
	}

	static void Stop() {
		if (g_current == nullptr) {
			return;
		}
		auto* music = g_current;
		g_current   = nullptr;
		if (music->m_player != nullptr) {
			music->m_player->Shutdown();
			music->m_player->Release();
			music->m_player = nullptr;
		}
		music->Release();
	}

	Ps5HomeMusicPlayer(const Ps5HomeMusicPlayer&)            = delete;
	Ps5HomeMusicPlayer& operator=(const Ps5HomeMusicPlayer&) = delete;

	// IUnknown
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
		if (ppv == nullptr) {
			return E_POINTER;
		}
		if (riid == IID_IUnknown || riid == __uuidof(IMFPMediaPlayerCallback)) {
			*ppv = static_cast<IMFPMediaPlayerCallback*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}

	STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }

	STDMETHODIMP_(ULONG) Release() override {
		const ULONG r = InterlockedDecrement(&m_ref);
		if (r == 0) {
			delete this;
		}
		return r;
	}

	void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* event_header) override {
		if (event_header == nullptr || this != g_current || m_player == nullptr) {
			return;
		}
		if (FAILED(event_header->hrEvent)) {
			Stop();
			return;
		}
		if (event_header->eEventType == MFP_EVENT_TYPE_PLAYBACK_ENDED) {
			// Loop: rewind and play again.
			PROPVARIANT pos;
			PropVariantInit(&pos);
			pos.vt            = VT_I8;
			pos.hVal.QuadPart = 0;
			m_player->SetPosition(MFP_POSITIONTYPE_100NS, &pos);
			m_player->Play();
		}
	}

private:
	Ps5HomeMusicPlayer()  = default;
	~Ps5HomeMusicPlayer() = default;

	IMFPMediaPlayer* m_player = nullptr;
	LONG             m_ref    = 1;
};

Ps5HomeMusicPlayer* Ps5HomeMusicPlayer::g_current = nullptr;

namespace {
QString g_home_music_path;
bool    g_home_music_enabled = true;

void StartHomeMusicIfWanted() {
	if (g_home_music_enabled && QFileInfo::exists(g_home_music_path)) {
		Ps5HomeMusicPlayer::Start(g_home_music_path);
	}
}
} // namespace

// Self-owning audio-only MFPlay player for the post-boot chime: starts playback
// on creation and releases itself when the clip ends or errors out.
class Ps5BootChimePlayer final: public IMFPMediaPlayerCallback {
public:
	static void Play(const QString& path) {
		auto*      chime = new Ps5BootChimePlayer();
		const auto wpath = path.toStdWString();
		IMFPMediaPlayer* player = nullptr;
		// Null hwnd: audio only, no video surface needed.
		const HRESULT hr =
		    MFPCreateMediaPlayer(wpath.c_str(), TRUE /* start playback */, 0, chime, nullptr, &player);
		if (FAILED(hr) || player == nullptr) {
			chime->Release();
			StartHomeMusicIfWanted(); // no chime: go straight to the music bed
			return;
		}
		chime->m_player = player; // chime keeps itself alive until PLAYBACK_ENDED
	}

	Ps5BootChimePlayer(const Ps5BootChimePlayer&)            = delete;
	Ps5BootChimePlayer& operator=(const Ps5BootChimePlayer&) = delete;

	// IUnknown
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
		if (ppv == nullptr) {
			return E_POINTER;
		}
		if (riid == IID_IUnknown || riid == __uuidof(IMFPMediaPlayerCallback)) {
			*ppv = static_cast<IMFPMediaPlayerCallback*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}

	STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }

	STDMETHODIMP_(ULONG) Release() override {
		const ULONG r = InterlockedDecrement(&m_ref);
		if (r == 0) {
			delete this;
		}
		return r;
	}

	void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* event_header) override {
		if (event_header == nullptr) {
			return;
		}
		if (event_header->eEventType == MFP_EVENT_TYPE_PLAYBACK_ENDED ||
		    FAILED(event_header->hrEvent)) {
			if (m_player != nullptr) {
				m_player->Shutdown();
				m_player->Release();
				m_player = nullptr;
			}
			StartHomeMusicIfWanted(); // chime done: fade into the home music bed
			Release(); // drop the self-reference: destroys this object
		}
	}

private:
	Ps5BootChimePlayer() = default;
	~Ps5BootChimePlayer() = default;

	IMFPMediaPlayer* m_player = nullptr;
	LONG             m_ref    = 1;
};

#pragma warning(pop)
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#endif /* _WIN32 */

void Ps5PlayBootChime(const QString& audio_path) {
#ifdef _WIN32
	if (QFileInfo::exists(audio_path)) {
		Ps5BootChimePlayer::Play(audio_path);
	} else {
		StartHomeMusicIfWanted(); // no chime shipped: start the music directly
	}
#else
	(void)audio_path;
#endif
}

void Ps5ConfigureHomeMusic(const QString& audio_path, bool enabled) {
#ifdef _WIN32
	g_home_music_path    = audio_path;
	g_home_music_enabled = enabled;
#else
	(void)audio_path;
	(void)enabled;
#endif
}

void Ps5SetHomeMusicEnabled(bool enabled) {
#ifdef _WIN32
	if (g_home_music_enabled == enabled) {
		return;
	}
	g_home_music_enabled = enabled;
	if (enabled) {
		StartHomeMusicIfWanted();
	} else {
		Ps5HomeMusicPlayer::Stop();
	}
#else
	(void)enabled;
#endif
}

Ps5BootSplash::Ps5BootSplash(const QString& video_path, QWidget* parent)
    : QWidget(parent), m_video_path(video_path) {
	setAttribute(Qt::WA_NativeWindow);
	setFocusPolicy(Qt::StrongFocus);
	setCursor(Qt::BlankCursor);

	QPalette pal = palette();
	pal.setColor(QPalette::Window, Qt::black);
	setPalette(pal);
	setAutoFillBackground(true);

	// Cover the whole launcher window and follow its resizes.
	if (parent != nullptr) {
		setGeometry(parent->rect());
		parent->installEventFilter(this);
	}

#ifdef _WIN32
	m_valid = QFileInfo::exists(m_video_path);
#endif
}

Ps5BootSplash::~Ps5BootSplash() {
#ifdef _WIN32
	if (m_player != nullptr) {
		m_player->Shutdown();
		m_player->Release();
		m_player = nullptr;
	}
#endif
}

void Ps5BootSplash::showEvent(QShowEvent* event) {
	QWidget::showEvent(event);

#ifdef _WIN32
	if (m_player == nullptr && !m_finished) {
		auto* player = new Ps5BootSplashPlayer(this);
		if (player->Start(m_video_path, reinterpret_cast<HWND>(winId()))) {
			m_player = player;
			// Safety net: never let a broken/stalled file keep the app hidden.
			QTimer::singleShot(30000, this, [this] { Finish(); });
		} else {
			player->Shutdown();
			player->Release();
			QTimer::singleShot(0, this, [this] { Finish(); });
		}
	}
#else
	QTimer::singleShot(0, this, [this] { Finish(); });
#endif
}

void Ps5BootSplash::Finish() {
	if (m_finished) {
		return;
	}
	m_finished = true;

#ifdef _WIN32
	if (m_player != nullptr) {
		m_player->Shutdown();
		m_player->Release();
		m_player = nullptr;
	}
#endif

	emit Finished();
	close();
}

bool Ps5BootSplash::eventFilter(QObject* watched, QEvent* event) {
	if (watched == parentWidget() && event->type() == QEvent::Resize) {
		setGeometry(parentWidget()->rect());
	}
	return QWidget::eventFilter(watched, event);
}

// Not skippable: swallow input so nothing reaches the home screen underneath.
void Ps5BootSplash::keyPressEvent(QKeyEvent* event) {
	event->accept();
}

void Ps5BootSplash::mousePressEvent(QMouseEvent* event) {
	event->accept();
}

void Ps5BootSplash::paintEvent(QPaintEvent* /*event*/) {
	QPainter p(this);
	p.fillRect(rect(), Qt::black);
#ifdef _WIN32
	if (m_player != nullptr) {
		m_player->UpdateVideo();
	}
#endif
}

void Ps5BootSplash::resizeEvent(QResizeEvent* event) {
	QWidget::resizeEvent(event);
#ifdef _WIN32
	if (m_player != nullptr) {
		m_player->ApplyCoverCrop(width(), height());
	}
#endif
}
