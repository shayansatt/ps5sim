#ifndef TROPHY_VIEWER_DIALOG_H
#define TROPHY_VIEWER_DIALOG_H

#include "common.h"

#include <QDialog>

class QTabWidget;
class QWidget;

class Configuration;
class TrophyViewerDialog: public QDialog {
	PS5SIM_QT_CLASS_NO_COPY(TrophyViewerDialog);

public:
	explicit TrophyViewerDialog(QWidget* parent = nullptr);

	// Aggregate trophy counts for a game, read from its trophy2/*.ucp files.
	// Earned counts come from sce_sys/trophy2/unlocked.dat, written by the
	// emulator when the game unlocks trophies.
	struct TrophyCounts {
		bool valid           = false;
		int  total           = 0;
		int  bronze          = 0;
		int  silver          = 0;
		int  gold            = 0;
		int  platinum        = 0;
		int  earned_total    = 0;
		int  earned_bronze   = 0;
		int  earned_silver   = 0;
		int  earned_gold     = 0;
		int  earned_platinum = 0;
	};

	static TrophyCounts CountTrophies(const Configuration* info);

	static bool HasTrophyData(const Configuration* info);
	static void ShowForGame(const Configuration* info, QWidget* parent);

private:
	bool LoadGame(const Configuration& info, QString& error);

	QTabWidget* m_tabs = nullptr;
};

#endif // TROPHY_VIEWER_DIALOG_H
