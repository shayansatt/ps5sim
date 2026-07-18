#include "configurationListWidget.h"
#include "mainDialog.h"
#include "ps5BootSplash.h"
#include "ps5HomeScreen.h"

#include <QApplication>
#include <QArgument>
#include <QCoreApplication>
#include <QDir>
#include <QFont>
#include <QIcon>
#include <QLinearGradient>
#include <QObject>
#include <QPainter>
#include <QPixmap>
#include <QStyleFactory>

class QStyle;

// Draw the application icon at runtime: a PS5-blue rounded square with "PS5".
// Keeps the build free of resource files while replacing the default exe icon.
static QIcon MakeAppIcon() {
	QIcon icon;
	for (const int size: {16, 24, 32, 48, 64, 128, 256}) {
		QPixmap pm(size, size);
		pm.fill(Qt::transparent);

		QPainter p(&pm);
		p.setRenderHint(QPainter::Antialiasing);

		QLinearGradient bg(0, 0, size, size);
		bg.setColorAt(0.0, QColor(23, 38, 74));
		bg.setColorAt(1.0, QColor(12, 18, 36));
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		const qreal radius = size * 0.22;
		p.drawRoundedRect(QRectF(0, 0, size, size), radius, radius);

		QFont f(QStringLiteral("Segoe UI"));
		f.setBold(true);
		f.setPixelSize(qMax(6, static_cast<int>(size * 0.42)));
		p.setFont(f);
		p.setPen(QColor(126, 200, 255));
		p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, QStringLiteral("PS5"));

		icon.addPixmap(pm);
	}
	return icon;
}

int main(int argc, char* argv[]) {
	QApplication a(argc, argv);
	QApplication::setWindowIcon(MakeAppIcon());
	MainDialog w;

	QStyle* s = QStyleFactory::create("fusion");
	QApplication::setStyle(s);

	QObject::connect(&a, &QApplication::aboutToQuit, &w, &MainDialog::Quit);

	w.emit Start();

	w.show();

	// PS5 boot video splash: plays boot_splash.mp4 (next to the exe) with sound
	// as a full-window overlay inside the launcher, not skippable; the home
	// screen appears when it ends. Missing file => no splash.
	const QString boot_video =
	    QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("boot_splash.mp4"));
	const QString boot_chime =
	    QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("boot_chime.mp3"));
	const QString home_music =
	    QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("home_music.mp3"));

	// Looping home-screen music (starts once the chime finishes); the on/off
	// state lives in the saved emulator settings ("Sound" page).
	auto* cfg = w.findChild<ConfigurationListWidget*>();
	Ps5ConfigureHomeMusic(home_music, cfg == nullptr || cfg->IsHomeMusicEnabled());

	auto* splash = new Ps5BootSplash(boot_video, &w);
	auto* home   = w.findChild<Ps5HomeScreen*>();
	if (splash->IsValid()) {
		// Start the dashboard intro in the same event-loop turn the video ends,
		// so the home screen animates in with no black gap in between. The PS5
		// home chime plays in the background over the reveal, then the music.
		QObject::connect(splash, &Ps5BootSplash::Finished, &w, [splash, home, boot_chime] {
			Ps5PlayBootChime(boot_chime);
			if (home != nullptr) {
				home->PlayIntroAnimation();
			}
			splash->deleteLater();
		});
		splash->show();
		splash->raise();
		splash->setFocus();
	} else {
		delete splash;
		Ps5PlayBootChime(boot_chime);
		if (home != nullptr) {
			home->PlayIntroAnimation();
		}
	}

	return QApplication::exec();
}
