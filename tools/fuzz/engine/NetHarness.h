/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Stand-ins the network harnesses share: the connection manager opens the disconnect screen and
// shows chat; neither has windows here.

#pragma once

#include "GameClient/GameWindowManager.h"
#include "GameClient/InGameUI.h"
#include "GameNetwork/LockstepEvents.h"

namespace NetHarness
{

// Chat goes to the in-game messages; they are not shown here.
class InGameUI : public ::InGameUI
{
public:
	virtual void draw( void ) override {}
	virtual View *createView( void ) override { return nullptr; }
	virtual void messageColor( const RGBColor *, UnicodeString, ... ) override {}
	virtual void message( UnicodeString, ... ) override {}
	virtual void message( AsciiString, ... ) override {}
};

// What the network reports to the player goes nowhere: the harnesses test the network, and the
// game's handler plays sounds the test data does not define.
class QuietEvents final : public LockstepEvents
{
public:
	void playerLeft( Int, const UnicodeString & ) override {}
	void playersChanged( void ) override {}
	void chat( Int, UnsignedByte, Int, const UnicodeString & ) override {}
	void disconnectChat( const UnicodeString & ) override {}
	void fileNotSent( const UnicodeString & ) override {}
	void waitingForPlayers( Bool ) override {}
	void waitingPlayer( Int, const UnicodeString &, Int ) override {}
	void dropCountdown( Int, Int ) override {}
	void networkClosed( void ) override {}
	void removalVote( Int, Int, Int, Int ) override {}
	void removedByVote( Int, const UnicodeString & ) override {}
};

// The disconnect screen asks for its layout and windows; there are none, so it has none.
class WindowManager : public GameWindowManager
{
public:
	virtual GameWindow *allocateNewWindow( void ) override { return nullptr; }
	virtual WindowLayout *winCreateLayout( AsciiString ) override { return nullptr; }
	virtual GameWinDrawFunc getPushButtonImageDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getPushButtonDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getCheckBoxImageDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getCheckBoxDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getRadioButtonImageDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getRadioButtonDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getTabControlImageDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getTabControlDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getListBoxImageDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getListBoxDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getComboBoxImageDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getComboBoxDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getHorizontalSliderImageDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getHorizontalSliderDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getVerticalSliderImageDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getVerticalSliderDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getProgressBarImageDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getProgressBarDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getStaticTextImageDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getStaticTextDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getTextEntryImageDrawFunc( void ) override { return nullptr; }
	virtual GameWinDrawFunc getTextEntryDrawFunc( void ) override { return nullptr; }
};

} // namespace NetHarness
