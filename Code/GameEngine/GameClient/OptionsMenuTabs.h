/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// OptionsMenuTabs.h -- the options menu as pages behind tab buttons. The window file of the game data
// (OptionsMenu.wnd) has one page with display, sound and control groups and a separate panel for the
// detail settings; the tabs show one group at a time and add the settings the window file has no
// controls for. Everything is placed relative to the windows of the file, so the menu follows its scaling.

#pragma once

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"

class GameWindow;

namespace OptionsMenuTabs
{

enum Page
{
	PAGE_DISPLAY,
	PAGE_DETAILS,
	PAGE_SOUND,
	PAGE_CONTROLS,
	PAGE_GAME,
	PAGE_EFFECTS,
	PAGE_MODS,		///< no tab of its own: reached from the game tab
	PAGE_ABOUT,		///< no tab of its own: version, source, licence, trademarks; reached from the game tab

	PAGE_COUNT,
	PAGE_TAB_COUNT = PAGE_MODS
};

/// Receives a changed value on "Accept"; unset: remove the value from the file.
/// The four font levels of the pages: tabs and buttons; section headers; labels, values and check boxes;
/// notes. Each level has one font per page; a level that does not fit shrinks on the whole page.
enum class FontLevel
{
	Title,
	Section,
	Text,
	Hint
};

typedef void (*StageFunc)( const char *id, const AsciiString &value, Bool unset );

/// Builds the tabs below the menu parent. FALSE if a window of the file is missing; the menu then
/// stays as the window file lays it out.
Bool build( GameWindow *menuParent );
Bool isBuilt();
void showPage( Int page );

/// TRUE if the control is a tab button (the page has been switched).
Bool handleButton( GameWindow *control );

/// Hands the settings changed in the added controls to stage.
void stageChanges( StageFunc stage );
/// Sets the fonts of a page again after its texts changed (the mods page fills its rows when shown).
void fitPage( Int page );
/// After the changes have been saved: values the game keeps elsewhere take them over.
void applySaved();
/// The added controls show the defaults.
void setDefaults();
/// Forgets the windows; the layout destroys them.
void release();

}	// namespace OptionsMenuTabs
