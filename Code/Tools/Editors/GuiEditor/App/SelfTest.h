/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SelfTest.h -- the command line of the GUI editor and its self test (--selftest).

#pragma once

#include "EditorCommon/Theme.h"

#include <QString>
#include <QStringList>

namespace GuiEditorApp
{

class EditorWindow;

struct Options
{
	QString data;
	EditorCommon::Game game = EditorCommon::Game::ZeroHour;
	QString selftest;
	QStringList layouts;
	bool engine = true;		// --no-engine: the self test without MapEditorEngine.dll
};

Options parseOptions(const QStringList &args);

// Draws layouts of the game data into <folder>/<name>.bmp and checks that they show something, then
// the viewer: game layouts, tree and reader agree, hidden windows, hit test, properties, resolutions,
// language and theme, opening a file. With --no-engine only what works without the engine: the layouts given
// with --layout read into model and tree, editing, saving, comparing, the window as picture.
// Writes selftest.log there; 0 if every step passed.
int runSelfTest(EditorWindow &window, Options options);

} // namespace GuiEditorApp
