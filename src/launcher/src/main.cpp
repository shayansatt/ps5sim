#include "mainDialog.h"

#include <QApplication>
#include <QArgument>
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

	return QApplication::exec();
}
