/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// main.cpp -- the GUI editor of GZH Neo (window layouts, .wnd). The game's window system draws the
// layout as the game would show it; tree, properties and problems come from the layout model.
//
//   GuiEditor --data <game data folder> [--game generals|zh] [layout.wnd]
//   GuiEditor --selftest <folder> --data <game data folder> [--game generals|zh] [--layout <path>]...
//   GuiEditor --selftest <folder> --no-engine --layout <path>...   (without the engine, also outside Windows)
//   -vulkan renders the layout with the Vulkan renderer (read by the engine DLL).
//
// A layout path that is no file is looked up in the folder Window of the game data, loose or in an
// archive, as the game does (Menus/MainMenu.wnd).

#include "App/EditorWindow.h"
#include "App/GuiEngine.h"
#include "App/SelfTest.h"
#include "EditorCommon/Language.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>

using namespace GuiEditorApp;
using namespace EditorCommon;

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	QApplication::setApplicationName(QStringLiteral("GuiEditor"));
	QApplication::setOrganizationName(QStringLiteral("GZH Neo"));
	setTranslationFile(QStringLiteral(":/guieditor/de.lang"));
	Options options = parseOptions(QApplication::arguments());
	// The engine makes the data folder the working directory: paths given here are resolved first.
	if (!options.data.isEmpty())
		options.data = QDir(options.data).absolutePath();
	for (QString &layout : options.layouts)
		if (QFileInfo::exists(layout))
			layout = QFileInfo(layout).absoluteFilePath();
	// The self test draws in English and the dark theme, whatever the system or the settings say.
	const bool selftest = !options.selftest.isEmpty();
	setLanguage(selftest ? Language::English : systemLanguage());
	EditorWindow window(app, options.game, !selftest);
	if (selftest)
		return runSelfTest(window, options);
	window.show();
	if (options.data.isEmpty())
	{
		QMessageBox::information(&window, T("GUI Editor"), T("Start the editor with --data <game data folder>."));
		return 0;
	}
	QString error;
	if (!window.startEngine(options.data, QString(), &error))
	{
		QMessageBox::warning(&window, T("GUI Editor"), error);
		return 1;
	}
	if (!options.layouts.isEmpty())
		window.openLayout(options.layouts.front());
	else if (const LayoutSource last = LayoutSource::fromKey(QSettings().value(QStringLiteral("last")).toString());
			 last.kind != LayoutSource::Kind::None)
		window.open(last);
	const int result = QApplication::exec();
	GuiEngine::instance().stop();
	return result;
}
