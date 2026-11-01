/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Theme.h -- design tokens of the editors and their translation into a Qt palette and style sheet.
//
// Every colour, size and radius of the interface comes from here; widgets never set their own
// colours. Two themes (dark, light), and an accent colour per game so the user always sees which
// game the editor works for.

#pragma once

#include <QColor>
#include <QString>

class QApplication;

namespace EditorCommon
{

enum class ThemeKind
{
	Dark,
	Light
};

enum class Game
{
	Generals,
	ZeroHour
};

struct Tokens
{
	QColor bg;			// window background
	QColor s1;			// bars, side panels, title bar
	QColor s2;			// input fields, tiles
	QColor s3;			// hover, selected segments
	QColor bd;			// lines, borders
	QColor tx;			// text
	QColor tx2;			// secondary text
	QColor tx3;			// hints (still 4.5:1 on s1)
	QColor acc;			// accent fill, white text on it
	QColor accFg;		// accent for lines, selection, text on dark or light surfaces
	QColor accBg;		// accent tint behind selected items
	QColor error;
	QColor axisX;
	QColor axisY;
	QColor axisZ;
};

Tokens tokens(ThemeKind theme, Game game);

// Applies palette, style sheet and font to the application.
void applyTheme(QApplication &app, ThemeKind theme, Game game);

QString gameName(Game game);

} // namespace EditorCommon
