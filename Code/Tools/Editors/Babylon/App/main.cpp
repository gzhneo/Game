/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// main.cpp -- Babylon, the translation tool of GZH Neo. Works without any office program: translators
// get .xlsx, .ods or .csv sheets that Excel, LibreOffice, OpenOffice, SoftMaker and others open.
//
//   Babylon [--project <folder>] [--str <file>] [--db <file>] [--dialog <folder>] [--game generals|zh] [--light]
//   Babylon --selftest <folder>
//
// Without --project the folder of the last session is opened, else the current folder
// (Data/Generals.str, Data/main.db, dialog/).

#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "EditorCommon/Language.h"
#include "EditorCommon/Theme.h"

#include <QApplication>
#include <QDir>
#include <QSettings>

using namespace BabylonApp;
using namespace EditorCommon;

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	QApplication::setApplicationName(QStringLiteral("Babylon"));
	QApplication::setOrganizationName(QStringLiteral("GZH Neo"));
	setTranslationFile(QStringLiteral(":/babylon/de.lang"));

	const QStringList args = QApplication::arguments();
	QString selftest;
	QString project;
	QString str;
	QString db;
	QString dialog;
	Game game = Game::ZeroHour;
	ThemeKind theme = ThemeKind::Dark;
	for (int i = 1; i < args.size(); ++i)
	{
		const QString &a = args[i];
		const bool hasValue = i + 1 < args.size();
		if (a == QStringLiteral("--selftest") && hasValue)
			selftest = args[++i];
		else if (a == QStringLiteral("--project") && hasValue)
			project = args[++i];
		else if (a == QStringLiteral("--str") && hasValue)
			str = args[++i];
		else if (a == QStringLiteral("--db") && hasValue)
			db = args[++i];
		else if (a == QStringLiteral("--dialog") && hasValue)
			dialog = args[++i];
		else if (a == QStringLiteral("--game") && hasValue)
			game = args[++i].compare(QStringLiteral("generals"), Qt::CaseInsensitive) == 0 ? Game::Generals : Game::ZeroHour;
		else if (a == QStringLiteral("--light"))
			theme = ThemeKind::Light;
	}

	// The self test runs in English and the dark theme, whatever the system or the settings say.
	const QString saved = QSettings().value(QStringLiteral("language")).toString();
	const Language language = !selftest.isEmpty() ? Language::English
		: saved == QStringLiteral("de")			  ? Language::German
		: saved == QStringLiteral("en")			  ? Language::English
												  : systemLanguage();
	setLanguage(language);
	applyTheme(app, selftest.isEmpty() ? theme : ThemeKind::Dark, game);

	MainWindow window;
	if (!selftest.isEmpty())
		return runSelfTest(window, QDir(selftest).absolutePath());

	if (project.isEmpty())
		project = QSettings().value(QStringLiteral("project"), QDir::currentPath()).toString();
	Babylon::ProjectPaths paths = Babylon::ProjectPaths::inFolder(Babylon::pathFromText(QDir(project).absolutePath().toStdString()));
	if (!str.isEmpty())
		paths.str = Babylon::pathFromText(QDir(str).absolutePath().toStdString());
	if (!db.isEmpty())
		paths.db = Babylon::pathFromText(QDir(db).absolutePath().toStdString());
	if (!dialog.isEmpty())
		paths.dialog = Babylon::pathFromText(QDir(dialog).absolutePath().toStdString());
	window.show();
	window.openProject(paths);
	return QApplication::exec();
}
