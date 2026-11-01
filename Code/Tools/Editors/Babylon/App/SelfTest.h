/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SelfTest.h -- Babylon --selftest <folder>: builds a small project in the folder and runs the whole
// round with fixed answers (fill the database, export, a translator's sheet back, a changed text,
// game files, report, speech check), then takes pictures of the window and every dialog.
// Writes selftest.txt ("ok"/"FAIL" per step) and the pictures into the folder; exit code 0 if all
// steps passed.

#pragma once

#include <QString>

namespace BabylonApp
{

class MainWindow;

int runSelfTest(MainWindow &window, const QString &folder);

} // namespace BabylonApp
