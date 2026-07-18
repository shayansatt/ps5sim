#ifndef LAUNCHER_INCLUDE_PS5BOOTSPLASH_H_
#define LAUNCHER_INCLUDE_PS5BOOTSPLASH_H_

#include "common.h"

#include <QString>
#include <QWidget>

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;

class Ps5BootSplashPlayer;

// Full-window overlay inside the launcher window that plays the PS5 boot video
// (with sound) via Windows Media Foundation (MFPlay) over the home screen until
// it ends. Not skippable; input is swallowed while it plays.
class Ps5BootSplash final: public QWidget {
	Q_OBJECT

public:
	Ps5BootSplash(const QString& video_path, QWidget* parent);
	~Ps5BootSplash() override;

	PS5SIM_QT_CLASS_NO_COPY(Ps5BootSplash);

	[[nodiscard]] bool IsValid() const { return m_valid; }

signals:
	void Finished();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void showEvent(QShowEvent* event) override;

private:
	friend class Ps5BootSplashPlayer;

	void Finish();

	QString              m_video_path;
	Ps5BootSplashPlayer* m_player   = nullptr;
	bool                 m_valid    = false;
	bool                 m_finished = false;
};

// Fire-and-forget background audio (MFPlay, audio-only): used for the home
// screen chime right after the boot video. When the chime ends (or its file is
// missing), the looping home-screen music starts automatically. No-op if the
// file is missing.
void Ps5PlayBootChime(const QString& audio_path);

// Looping home-screen background music. Configure() stores the track path and
// the on/off state from settings; SetEnabled() toggles playback live.
void Ps5ConfigureHomeMusic(const QString& audio_path, bool enabled);
void Ps5SetHomeMusicEnabled(bool enabled);

#endif /* LAUNCHER_INCLUDE_PS5BOOTSPLASH_H_ */
