/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// OptionsMenuMods.h -- the mods page of the options menu, reached from the "Game" tab: every mod in the
// mods folder of the user data with a check box (active or not), its state, buttons to move it up or down
// (the top one ranks highest), its description, and the folder. The choice is Settings.ini Mods.Active and
// takes effect after a restart, which the menu offers after "Accept".

#pragma once

#include "Lib/BaseType.h"
#include "GameClient/OptionsMenuTabs.h"

class GameWindow;

namespace OptionsMenuMods
{

/// Where the page goes, in coordinates of the frame window, and the windows its gadgets copy.
struct Geometry
{
	GameWindow *frame;
	Int left, right, top, bottom, gap, rowHeight, headerHeight;
	GameWindow *checkModel, *labelModel, *headerModel, *buttonModel;
};

using AddWindow = void (*)( GameWindow *window, OptionsMenuTabs::FontLevel level );

/// Creates the windows of the page; add puts each on the page.
void build( const Geometry &geometry, AddWindow add );
/// After the page was shown: fills the rows and hides the unused ones.
void refresh();

enum class Handled
{
	No,
	Yes,
	Back		///< "Back": show the game tab again
};
Handled handleButton( GameWindow *control );

void stageChanges( OptionsMenuTabs::StageFunc stage );
void release();

/// "3 found, 1 on": the mods of the page and how many of them are checked.
UnicodeString summaryText();

/// A mod list (Engine::encodeModTag) for messages: "Name 1.2, Other", or the word for none.
UnicodeString listText( const AsciiString &tag );

}	// namespace OptionsMenuMods
