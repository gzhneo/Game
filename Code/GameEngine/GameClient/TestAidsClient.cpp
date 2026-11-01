/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: TestAidsClient.cpp //////////////////////////////////////////////////////////////////////
// Test helpers, client side: keys, clickable side panel, display of the active test helpers, time
// lapse, quick save/load, -mission and the script file for the replay test.
//
// Anything that touches game logic is sent only as game message MSG_TEST_AID (executed in
// TestAidsLogic.cpp, recorded in the replay). The client changes no logic state itself; the time
// lapse only runs more ticks per main loop pass (GameEngine::execute).
//
// The side panel is built in code from the game's window system (GameWindowManager, push buttons),
// rebuilt after expanding, collapsing or a resolution change; it sits at the left screen edge.
// Windows disappear on their own when the engine resets (game end, load), so the panel is always
// looked up by its window ID, never through a stored pointer.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "formatinto.h"

#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Recorder.h"
#include "Common/TestAids.h"

#include "GameClient/CampaignManager.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Shell.h"

#include "GameLogic/GameLogic.h"

#include "GameNetwork/NetworkInterface.h"

#include <stdio.h>
#include <string.h>

// ------------------------------------------------------------------------------------------------
// Client state
// ------------------------------------------------------------------------------------------------
static Int s_fastForward = 1;								///< 1, 2, 4, 8
static Bool s_menuOpen = FALSE;
static Bool s_overlayOn = TRUE;
static Bool s_savePending = FALSE;
static Bool s_loadPending = FALSE;
static Bool s_rebuildBar = TRUE;
static Bool s_barBuilt = FALSE;	///< a panel exists that must go when the game ends
static Int s_barWidth = 0;							///< resolution at the last build
static Int s_barHeight = 0;
static UnsignedInt s_lastState = 0;			///< state shown by the panel button texts
static UnsignedInt barState( void );

static const char *QUICK_SAVE_FILE = "Testhilfen-Schnellspeicher.sav";

// Meta messages MSG_META_TESTHILFE_* in the same order as in MessageStream.h
static const Int s_metaCommands[] =
{
	TA_MENU, TA_OVERLAY, TA_FAST_FORWARD, TA_QUICK_SAVE, TA_QUICK_LOAD, TA_MONEY_PLUS, TA_FREEZE_MONEY,
	TA_UNLIMITED_POWER, TA_POWER_PLUS, TA_POWER_MINUS, TA_GENERAL_POINT, TA_ALL_ABILITIES, TA_RANK_PLUS,
	TA_INSTANT_BUILD, TA_NO_COOLDOWN, TA_INVULNERABLE, TA_REVEAL_MAP, TA_DESTROY_SELECTION,
	TA_DESTROY_ALL_ENEMIES, TA_WIN
};
static_assert( sizeof(s_metaCommands) / sizeof(s_metaCommands[0])
	== GameMessage::MSG_META_TEST_AID_WIN - GameMessage::MSG_META_TEST_AID_MENU + 1,
	"s_metaBefehle does not match MSG_META_TESTHILFE_* in MessageStream.h" );

// Side panel buttons; bit != 0: toggle, shows its state
struct TestAidButton
{
	Int command;
	const wchar_t *text;
	const wchar_t *key;
	UnsignedInt bit;
};

static const TestAidButton s_buttons[] =
{
	{ TA_MONEY_PLUS,							L"Money +10,000",					L"Ctrl + = (German layout: key right of \u00df)",	0 },
	{ TA_FREEZE_MONEY,				L"Freeze money",				L"Alt + U",											TESTAID_MONEY_FROZEN },
	{ TA_UNLIMITED_POWER,			L"Unlimited power",			L"Alt + E",											TESTAID_UNLIMITED_POWER },
	{ TA_POWER_PLUS,					L"Power +5",						L"Alt + P",											0 },
	{ TA_POWER_MINUS,					L"Power -5",						L"Shift + Alt + P",					0 },
	{ TA_GENERAL_POINT,					L"General point +1",			L"Ctrl + Shift + P",					0 },
	{ TA_ALL_ABILITIES,			L"All abilities",	L"Ctrl + Shift + Alt + K",		0 },
	{ TA_RANK_PLUS,							L"Rank +1",								L"Ctrl + K",										0 },
	{ TA_INSTANT_BUILD,							L"Instant build",							L"Alt + F",											TESTAID_INSTANT_BUILD },
	{ TA_NO_COOLDOWN,				L"No cooldown",				L"Ctrl + S",										TESTAID_NO_COOLDOWN },
	{ TA_INVULNERABLE,					L"Invulnerable",					L"Alt + I",											TESTAID_INVULNERABLE },
	{ TA_REVEAL_MAP,				L"Reveal map",				L"Ctrl + R",										TESTAID_MAP_REVEALED },
	{ TA_DESTROY_SELECTION,		L"Destroy selection",		L"Ctrl + Shift + X",					0 },
	{ TA_DESTROY_ALL_ENEMIES,	L"Destroy all enemies",	L"Ctrl + Shift + Num /",		0 },
	{ TA_WIN,									L"Win mission",			L"Ctrl + W",										0 },
	{ TA_FAST_FORWARD,						L"Fast forward",						L"Alt + S",											0 },
	{ TA_QUICK_SAVE,			L"Quick save",			L"Shift + F5 (campaign only)",	0 },
	{ TA_QUICK_LOAD,					L"Quick load",					L"Shift + F9",								0 },
	{ TA_OVERLAY,								L"Overlay",								L"Alt + A",											0 },
};
enum { BUTTON_COUNT = sizeof(s_buttons) / sizeof(s_buttons[0]) };

// Names for the script file (-testhilfenAblauf)
struct TestAidName
{
	const char *name;
	Int command;
};

static const TestAidName s_names[] =
{
	{ "geld_plus", TA_MONEY_PLUS },									{ "geld_einfrieren", TA_FREEZE_MONEY },
	{ "energie_unendlich", TA_UNLIMITED_POWER },	{ "energie_plus", TA_POWER_PLUS },
	{ "energie_minus", TA_POWER_MINUS },					{ "generalspunkt", TA_GENERAL_POINT },
	{ "alle_faehigkeiten", TA_ALL_ABILITIES },	{ "rang_plus", TA_RANK_PLUS },
	{ "sofortbau", TA_INSTANT_BUILD },									{ "keine_wartezeit", TA_NO_COOLDOWN },
	{ "unverwundbar", TA_INVULNERABLE },						{ "karte_aufdecken", TA_REVEAL_MAP },
	{ "auswahl_vernichten", TA_DESTROY_SELECTION },	{ "alle_feinde_vernichten", TA_DESTROY_ALL_ENEMIES },
	{ "sieg", TA_WIN },														{ "zeitraffer", TA_FAST_FORWARD },
	{ "schnellspeichern", TA_QUICK_SAVE },		{ "schnellladen", TA_QUICK_LOAD },
	{ "menue", TA_MENU },													{ "anzeige", TA_OVERLAY },
};

// ------------------------------------------------------------------------------------------------
Int TestAids_CommandByName( const char *name )
{
	for( size_t i = 0; i < sizeof(s_names) / sizeof(s_names[0]); ++i )
	{
		if( _stricmp( s_names[i].name, name ) == 0 )
			return s_names[i].command;
	}
	return TA_NONE;
}

// ------------------------------------------------------------------------------------------------
static void showHint( const wchar_t *text )
{
	if( TheInGameUI != nullptr && !TheGlobalData->m_runnerMode )
	{
		UnicodeString s;
		s.format( L"Testhilfe: %s", text );
		TheInGameUI->message( s );
	}
}

// ------------------------------------------------------------------------------------------------
Bool TestAids_ClientAllowed( void )
{
	if( TheGlobalData == nullptr || !TheGlobalData->m_testAids )
		return FALSE;
	if( TheGameLogic == nullptr || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return FALSE;
	if( TheGameLogic->isInReplayGame() || (TheRecorder != nullptr && TheRecorder->getMode() == RecorderModeType::RECORDERMODETYPE_PLAYBACK) )
		return FALSE;
	if( TheNetwork != nullptr )
		return FALSE;	// multiplayer: never (lockstep)
	const Int mode = TheGameLogic->getGameMode();
	return (mode == GAME_SINGLE_PLAYER || mode == GAME_SKIRMISH) ? TRUE : FALSE;
}

// ------------------------------------------------------------------------------------------------
Int TestAids_FastForward( void )
{
	if( s_fastForward <= 1 || !TestAids_ClientAllowed() )
		return 1;
	return s_fastForward;
}

// ------------------------------------------------------------------------------------------------
void TestAids_Command( Int command, Int targetPlayer )
{
	if( !TestAids_ClientAllowed() )
		return;

	if( command >= TA_MONEY_PLUS && command <= TA_LAST_GAME_COMMAND )
	{
		// game message: goes through the logic and into the replay
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_TEST_AID );
		msg->appendIntegerArgument( command );
		msg->appendIntegerArgument( targetPlayer );
		return;	// the panel toggle texts follow the state (updateBar)
	}

	switch( command )
	{
		case TA_MENU:
			s_menuOpen = !s_menuOpen;
			s_rebuildBar = TRUE;
			break;

		case TA_OVERLAY:
			s_overlayOn = !s_overlayOn;
			break;

		case TA_FAST_FORWARD:
		{
			s_fastForward = (s_fastForward >= 8) ? 1 : s_fastForward * 2;
			UnicodeString s;
			s.format( L"Fast forward x%d", s_fastForward );
			showHint( s.str() );
			break;
		}

		case TA_QUICK_SAVE:
			s_savePending = TRUE;
			break;

		case TA_QUICK_LOAD:
			s_loadPending = TRUE;
			break;

		default:
			break;
	}
}

// ------------------------------------------------------------------------------------------------
Bool TestAids_Key( Int metaType )
{
	const Int index = metaType - GameMessage::MSG_META_TEST_AID_MENU;
	if( index < 0 || index >= static_cast<Int>(sizeof(s_metaCommands) / sizeof(s_metaCommands[0])) )
		return FALSE;
	TestAids_Command( s_metaCommands[index] );
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// Quick save and load: called from TestAids_Update, i.e. outside message processing. The game
// saves campaigns only (the save menu is disabled in skirmish); the test helper does the same.
// ------------------------------------------------------------------------------------------------
static void quickSave( void )
{
	if( TheGameLogic->getGameMode() != GAME_SINGLE_PLAYER )
	{
		showHint( L"Quick save only in the campaign" );
		return;
	}
	if( TheInGameUI != nullptr && TheInGameUI->getInputEnabled() == FALSE )
	{
		showHint( L"Quick save is not possible during a cutscene" );
		return;
	}
	const SaveCode code = TheGameState->saveGame( AsciiString( QUICK_SAVE_FILE ),
		UnicodeString( L"Testhilfen-Schnellspeicher" ), SAVE_FILE_TYPE_NORMAL );
	showHint( code == SaveCode::SC_OK ? L"saved (Shift + F9 loads)" : L"save failed" );
}

static void quickLoad( void )
{
	const AsciiString leaf( QUICK_SAVE_FILE );
	// check first: GameState::doesSaveGameExist asserts on a missing file in debug builds
	const AsciiString path = TheGameState->getFilePathInSaveDirectory( leaf );
	if( ::GetFileAttributesA( path.str() ) == INVALID_FILE_ATTRIBUTES || !TheGameState->doesSaveGameExist( leaf ) )
	{
		showHint( L"no quick save yet (Shift + F5)" );
		return;
	}
	AvailableGameInfo info;
	info.filename = leaf;
	info.next = nullptr;
	info.prev = nullptr;
	TheGameState->getSaveGameInfoFromFile( path, &info.saveGameInfo );
	// like PopupSaveLoad doLoadGame in a game
	if( TheGameState->loadGame( info ) != SaveCode::SC_OK )
	{
		if( TheGameLogic->isInGame() )
			TheGameLogic->clearGameData( FALSE );
		TheGameEngine->reset();
		TheShell->showShell( TRUE );
	}
	s_rebuildBar = TRUE;
}

// ------------------------------------------------------------------------------------------------
// Side panel
// ------------------------------------------------------------------------------------------------
static NameKeyType barId( void )
{
	static NameKeyType s_id = NAMEKEY_INVALID;
	if( s_id == NAMEKEY_INVALID )
		s_id = TheNameKeyGenerator->nameToKey( AsciiString( "Testhilfen.wnd:Leiste" ) );
	return s_id;
}

static NameKeyType buttonId( Int index )	// -1 = expand/collapse button
{
	static NameKeyType s_ids[BUTTON_COUNT + 1];
	static Bool s_init = FALSE;
	if( !s_init )
	{
		for( Int i = -1; i < BUTTON_COUNT; ++i )
		{
			AsciiString name;
			name.format( "Testhilfen.wnd:Knopf%d", i + 1 );
			s_ids[i + 1] = TheNameKeyGenerator->nameToKey( name );
		}
		s_init = TRUE;
	}
	return s_ids[index + 1];
}

static GameWindow *findBar( void )
{
	if( TheWindowManager == nullptr )
		return nullptr;
	GameWindow *bar = TheWindowManager->winGetWindowFromId( nullptr, barId() );
	if( bar != nullptr && BitTest( bar->winGetStatus(), WIN_STATUS_DESTROYED ) )
		return nullptr;
	return bar;
}

static void removeBar( void )
{
	GameWindow *bar = findBar();
	if( bar != nullptr )
		TheWindowManager->winDestroy( bar );
}

static UnicodeString buttonText( const TestAidButton &k, const Player *aidPlayer )
{
	UnicodeString s;
	if( k.command == TA_FAST_FORWARD )
		s.format( L"Fast forward x%d", s_fastForward );
	else if( k.command == TA_OVERLAY )
		s.format( L"[%s] Overlay", s_overlayOn ? L"x" : L" " );
	else if( k.bit != 0 )
		s.format( L"[%s] %s", (aidPlayer != nullptr && aidPlayer->hasTestAid( k.bit )) ? L"x" : L" ", k.text );
	else
		s.format( L"%s", k.text );
	return s;
}

static WindowMsgHandledType barSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	if( msg != GBM_SELECTED )
		return WindowMsgHandledType::MSG_IGNORED;
	GameWindow *button = reinterpret_cast<GameWindow *>(mData1);
	if( button == nullptr )
		return WindowMsgHandledType::MSG_IGNORED;
	const Int id = button->winGetWindowId();
	if( id == buttonId( -1 ) )
	{
		TestAids_Command( TA_MENU );
		return WindowMsgHandledType::MSG_HANDLED;
	}
	for( Int i = 0; i < BUTTON_COUNT; ++i )
	{
		if( id == buttonId( i ) )
		{
			TestAids_Command( s_buttons[i].command );
			return WindowMsgHandledType::MSG_HANDLED;
		}
	}
	return WindowMsgHandledType::MSG_IGNORED;
}

static void drawBar( GameWindow *window, WinInstanceData *instData )
{
	if( !s_menuOpen )
		return;
	Int x, y, w, h;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &w, &h );
	TheDisplay->drawFillRect( x, y, w, h, GameMakeColor( 0, 0, 0, 160 ) );
	TheDisplay->drawOpenRect( x, y, w, h, 1.0f, GameMakeColor( 200, 200, 120, 200 ) );
}

static GameWindow *createButton( GameWindow *bar, Int x, Int y, Int w, Int h, const UnicodeString &text,
	const wchar_t *tooltip, NameKeyType id, GameFont *font )
{
	WinInstanceData instData;
	instData.init();
	BitSet( instData.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
	if( tooltip != nullptr )
		instData.setTooltipText( UnicodeString( tooltip ) );
	GameWindow *button = TheWindowManager->gogoGadgetPushButton( bar, WIN_STATUS_ENABLED, x, y, w, h, &instData, font, TRUE );
	if( button != nullptr )
	{
		button->winSetWindowId( id );
		// own colors instead of the red gadget default: dark, translucent, yellow on hover
		button->winSetEnabledColor( 0, GameMakeColor( 25, 25, 25, 210 ) );
		button->winSetEnabledBorderColor( 0, GameMakeColor( 150, 140, 90, 255 ) );
		button->winSetHiliteColor( 0, GameMakeColor( 70, 62, 30, 230 ) );
		button->winSetHiliteBorderColor( 0, GameMakeColor( 255, 220, 60, 255 ) );
		button->winSetEnabledTextColors( GameMakeColor( 235, 235, 235, 255 ), GameMakeColor( 0, 0, 0, 255 ) );
		button->winSetHiliteTextColors( GameMakeColor( 255, 220, 60, 255 ), GameMakeColor( 0, 0, 0, 255 ) );
		GadgetButtonSetText( button, text );
	}
	return button;
}

static void buildBar( void )
{
	removeBar();
	if( TheDisplay == nullptr || TheWindowManager == nullptr )
		return;

	const Int screenWidth = static_cast<Int>(TheDisplay->getWidth());
	const Int screenHeight = static_cast<Int>(TheDisplay->getHeight());
	s_barWidth = screenWidth;
	s_barHeight = screenHeight;

	// sizes from the screen height so the panel looks the same at every resolution; left edge
	Int line = screenHeight / 40;
	if( line < 18 ) line = 18;
	if( line > 32 ) line = 32;
	const Int spacing = 2;
	const Int columnWidth = line * 8;
	const Int rand = 4;
	const Int top = screenHeight / 8;

	// two columns if one does not fit above the control bar
	const Int space = screenHeight * 70 / 100 - top - line;
	const Int columns = (BUTTON_COUNT * (line + spacing) > space) ? 2 : 1;
	const Int lines = (BUTTON_COUNT + columns - 1) / columns;

	const Int width = s_menuOpen ? (rand * 2 + columns * columnWidth + (columns - 1) * spacing) : columnWidth / 2 + rand * 2;
	const Int height = s_menuOpen ? (rand * 2 + (lines + 1) * (line + spacing)) : line + rand * 2;

	GameWindow *bar = TheWindowManager->winCreate( nullptr, WIN_STATUS_ENABLED, 0, top, width, height, barSystem, nullptr );
	if( bar == nullptr )
		return;
	bar->winSetWindowId( barId() );
	bar->winSetDrawFunc( drawBar );
	s_barBuilt = TRUE;

	Int fontPointSize = line / 2;
	if( fontPointSize < 9 ) fontPointSize = 9;
	AsciiString fontName( "Arial" );
	if( TheGlobalLanguageData != nullptr && TheGlobalLanguageData->m_defaultWindowFont.name.isNotEmpty() )
		fontName = TheGlobalLanguageData->m_defaultWindowFont.name;
	GameFont *font = TheFontLibrary->getFont( fontName, fontPointSize, FALSE );

	createButton( bar, rand, rand, width - 2 * rand, line, UnicodeString( s_menuOpen ? L"Testhilfen <<" : L"Testhilfen" ),
		L"Show/hide test aids (Alt + M)", buttonId( -1 ), font );

	if( !s_menuOpen )
		return;

	const Player *aidPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	s_lastState = barState();
	for( Int i = 0; i < BUTTON_COUNT; ++i )
	{
		const Int column = i / lines;
		const Int row = i % lines;
		const Int x = rand + column * (columnWidth + spacing);
		const Int y = rand + (row + 1) * (line + spacing);
		createButton( bar, x, y, columnWidth, line, buttonText( s_buttons[i], aidPlayer ), s_buttons[i].key, buttonId( i ), font );
	}
}

// Update the toggle texts without a rebuild (the logic executes commands one frame later)
static UnsignedInt barState( void )
{
	const Player *aidPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	return (aidPlayer ? aidPlayer->getTestAids() : 0) ^ (static_cast<UnsignedInt>(s_fastForward) << 16)
		^ (s_overlayOn ? 0x80000000u : 0u);
}

static void updateBar( GameWindow *bar )
{
	if( !s_menuOpen )
		return;
	const Player *aidPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	const UnsignedInt state = barState();
	if( state == s_lastState )
		return;
	s_lastState = state;
	for( Int i = 0; i < BUTTON_COUNT; ++i )
	{
		if( s_buttons[i].bit == 0 && s_buttons[i].command != TA_FAST_FORWARD && s_buttons[i].command != TA_OVERLAY )
			continue;
		GameWindow *button = TheWindowManager->winGetWindowFromId( bar, buttonId( i ) );
		if( button != nullptr )
			GadgetButtonSetText( button, buttonText( s_buttons[i], aidPlayer ) );
	}
}

// ------------------------------------------------------------------------------------------------
// Script file (-testhilfenAblauf): lines "logic-frame command [player index]", '#' comment. Triggers
// the commands like keys, for the test helper replay test (runner.py record/check).
// ------------------------------------------------------------------------------------------------
struct ScriptEntry
{
	UnsignedInt frame;
	Int command;
	Int aidPlayer;
};

enum { SCRIPT_AI_BASE = -100 };	///< target "kiN" is stored as SCRIPT_AI_BASE - N

/// N-th AI player (from 1): computer, not neutral, observer or civilian; -1 = none
static Int aiPlayer( Int n )
{
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		const Player *p = ThePlayerList->getNthPlayer( i );
		if( p == nullptr || p == ThePlayerList->getNeutralPlayer() || p->isPlayerObserver()
			|| p->getPlayerType() != PLAYER_COMPUTER || p->getSide().isEmpty()
			|| p->getSide().compareNoCase( "Civilian" ) == 0 )
			continue;
		if( --n == 0 )
			return i;
	}
	return -1;
}

// Fixed table instead of std::vector: a static vector would outlive the leak check at the end of
// WinMain and be reported as a leak.
enum { SCRIPT_MAX_ENTRIES = 256 };
static ScriptEntry s_script[SCRIPT_MAX_ENTRIES];
static size_t s_scriptCount = 0;
static size_t s_scriptPos = 0;
static Bool s_scriptLoaded = FALSE;

static void loadAidScript( void )
{
	s_scriptLoaded = TRUE;
	FILE *f = fopen( TheGlobalData->m_testAidsScript.str(), "r" );
	if( f == nullptr )
	{
		GameEngine_RunnerLog( "TESTHILFE ERROR script file not found" );
		return;
	}
	char line[256];
	while( fgets( line, sizeof(line), f ) != nullptr )
	{
		char *comment = strchr( line, '#' );
		if( comment != nullptr )
			*comment = 0;
		unsigned int frame = 0;
		char name[64] = { 0 };
		char target[32] = { 0 };
		const int n = sscanf( line, "%u %63s %31s", &frame, name, target );
		if( n < 2 )
			continue;
		const Int command = TestAids_CommandByName( name );
		if( command == TA_NONE )
		{
			char notice[128];
			WWFormat::Into(notice, "TESTHILFE ERROR unknown command {}", name);
			GameEngine_RunnerLog( notice );
			continue;
		}
		ScriptEntry e;
		e.frame = frame;
		e.command = command;
		// target: player index, "kiN" (N-th AI player, from 1) or nothing (local player)
		e.aidPlayer = -1;
		if( n >= 3 )
			e.aidPlayer = (_strnicmp( target, "ki", 2 ) == 0) ? SCRIPT_AI_BASE - atoi( target + 2 ) : atoi( target );
		if( s_scriptCount < SCRIPT_MAX_ENTRIES )
			s_script[s_scriptCount++] = e;
	}
	fclose( f );
}

static void runAidScript( void )
{
	if( !s_scriptLoaded )
		loadAidScript();
	const UnsignedInt frame = TheGameLogic->getFrame();
	static Bool s_playersReported = FALSE;
	if( !s_playersReported && s_scriptPos < s_scriptCount && s_script[s_scriptPos].frame <= frame )
	{
		// log before the first command (the player list exists then) so the targets can be traced
		s_playersReported = TRUE;
		for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
		{
			const Player *p = ThePlayerList->getNthPlayer( i );
			char notice[160];
			WWFormat::Into(notice, "TESTHILFE SPIELER {:d} {} {}{}", i, p->getSide().str(), p->getPlayerType() == PLAYER_HUMAN ? "human" : "computer", p == ThePlayerList->getLocalPlayer() ? " local" : "");
			GameEngine_RunnerLog( notice );
		}
	}
	while( s_scriptPos < s_scriptCount && s_script[s_scriptPos].frame <= frame )
	{
		ScriptEntry e = s_script[s_scriptPos++];
		if( e.aidPlayer <= SCRIPT_AI_BASE )
			e.aidPlayer = aiPlayer( SCRIPT_AI_BASE - e.aidPlayer );
		char notice[96];
		WWFormat::Into(notice, "TESTHILFE {:d} command {:d} player {:d}", frame, e.command, e.aidPlayer);
		GameEngine_RunnerLog( notice );
		TestAids_Command( e.command, e.aidPlayer );
	}
}

// ------------------------------------------------------------------------------------------------
void TestAids_Update( void )
{
	if( !TestAids_ClientAllowed() )
	{
		s_fastForward = 1;
		s_savePending = FALSE;
		s_loadPending = FALSE;
		if( TheGameLogic == nullptr || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		{
			s_rebuildBar = TRUE;	// next game: rebuild
			// The panel is a window without a layout and would stay after the game ends; destroy it once no
			// game is running.
			if( s_barBuilt )
			{
				removeBar();
				s_barBuilt = FALSE;
			}
		}
		return;
	}

	// -mission: once the mission runs, clear the start file placeholder; otherwise the end of the
	// mission would exit the game (GameLogic::clearGameData) and block the way to the next mission
	if( TheGlobalData->m_testAidsMission.isNotEmpty() && TheGlobalData->m_initialFile.isNotEmpty()
		&& TheGameLogic->getFrame() > 1 )
		TheWritableGlobalData->m_initialFile.clear();

	if( TheGlobalData->m_testAidsScript.isNotEmpty() && TheGameLogic->getFrame() > 0 )
		runAidScript();

	if( s_savePending )
	{
		s_savePending = FALSE;
		quickSave();
	}
	if( s_loadPending )
	{
		s_loadPending = FALSE;
		quickLoad();
		return;	// the engine was reset, the next frame builds the panel
	}

	if( TheGlobalData->m_runnerMode || TheDisplay == nullptr )
		return;

	// rebuild on expand/collapse, a new game (windows gone) and a resolution change;
	// updateBar updates the toggle texts
	GameWindow *bar = findBar();
	if( bar == nullptr || s_rebuildBar
		|| s_barWidth != static_cast<Int>(TheDisplay->getWidth()) || s_barHeight != static_cast<Int>(TheDisplay->getHeight()) )
	{
		s_rebuildBar = FALSE;
		buildBar();
		return;
	}
	updateBar( bar );
}

// ------------------------------------------------------------------------------------------------
void TestAids_Draw( void )
{
	if( !s_overlayOn || !TestAids_ClientAllowed() || TheDisplay == nullptr || TheDisplayStringManager == nullptr )
		return;
	const Player *aidPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	if( aidPlayer == nullptr )
		return;

	// active test helpers of the local player in one line at the top center
	UnicodeString text;
	struct { UnsignedInt bit; const wchar_t *name; } const entries[] =
	{
		{ TESTAID_MONEY_FROZEN,		L"Money frozen" },
		{ TESTAID_UNLIMITED_POWER,	L"Unlimited power" },
		{ TESTAID_INSTANT_BUILD,					L"Instant build" },
		{ TESTAID_NO_COOLDOWN,		L"No cooldown" },
		{ TESTAID_INVULNERABLE,				L"Invulnerable" },
		{ TESTAID_MAP_REVEALED,		L"Map revealed" },
	};
	for( size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i )
	{
		if( !aidPlayer->hasTestAid( entries[i].bit ) )
			continue;
		if( !text.isEmpty() )
			text.concat( L" | " );
		text.concat( entries[i].name );
	}
	const Int bonus = aidPlayer->getEnergy()->getTestAidPowerBonus();
	if( bonus != 0 )
	{
		UnicodeString s;
		s.format( L"%sEnergie %+d", text.isEmpty() ? L"" : L" | ", bonus );
		text.concat( s );
	}
	if( s_fastForward > 1 )
	{
		UnicodeString s;
		s.format( L"%sZeitraffer x%d", text.isEmpty() ? L"" : L" | ", s_fastForward );
		text.concat( s );
	}
	if( text.isEmpty() )
		return;
	UnicodeString line;
	line.format( L"Testhilfen: %s", text.str() );

	Int fontSize = static_cast<Int>(TheDisplay->getHeight()) / 70;
	if( fontSize < 9 ) fontSize = 9;
	AsciiString fontName( "Arial" );
	if( TheGlobalLanguageData != nullptr && TheGlobalLanguageData->m_defaultWindowFont.name.isNotEmpty() )
		fontName = TheGlobalLanguageData->m_defaultWindowFont.name;

	DisplayString *ds = TheDisplayStringManager->newDisplayString();
	if( ds == nullptr )
		return;
	ds->setFont( TheFontLibrary->getFont( fontName, fontSize, TRUE ) );
	ds->setText( line );
	Int w = 0, h = 0;
	ds->getSize( &w, &h );
	const Int x = (static_cast<Int>(TheDisplay->getWidth()) - w) / 2;
	const Int y = 4;
	TheDisplay->drawFillRect( x - 6, y - 2, w + 12, h + 4, GameMakeColor( 0, 0, 0, 150 ) );
	ds->draw( x, y, GameMakeColor( 255, 220, 60, 255 ), GameMakeColor( 0, 0, 0, 255 ) );
	TheDisplayStringManager->freeDisplayString( ds );
}

// ------------------------------------------------------------------------------------------------
Bool TestAids_PrepareMission( void )
{
	const AsciiString campaign = TheGlobalData->m_testAidsMission;
	const Int number = TheGlobalData->m_testAidsMissionNr;
	if( TheCampaignManager == nullptr || campaign.isEmpty() || number < 1 )
	{
		DEBUG_LOG(( "Test aids: -mission %s %d invalid\n", campaign.str(), number ));
		GameEngine_RunnerLog( "TESTHILFE ERROR -mission invalid" );
		return FALSE;
	}
	TheCampaignManager->setCampaign( campaign );
	if( TheCampaignManager->getCurrentCampaign() == nullptr || TheCampaignManager->getCurrentMission() == nullptr )
	{
		DEBUG_LOG(( "Test aids: campaign %s not found\n", campaign.str() ));
		GameEngine_RunnerLog( "TESTHILFE ERROR campaign not found" );
		return FALSE;
	}
	for( Int i = 1; i < number; ++i )
	{
		if( TheCampaignManager->gotoNextMission() == nullptr )
		{
			DEBUG_LOG(( "Test aids: campaign %s has no mission %d\n", campaign.str(), number ));
			GameEngine_RunnerLog( "TESTHILFE ERROR mission not found" );
			TheCampaignManager->setCampaign( AsciiString::TheEmptyString );
			return FALSE;
		}
	}
	const AsciiString map = TheCampaignManager->getCurrentMap();
	if( map.isEmpty() )
	{
		TheCampaignManager->setCampaign( AsciiString::TheEmptyString );
		return FALSE;
	}
	TheCampaignManager->setGameDifficulty( DIFFICULTY_NORMAL );
	TheWritableGlobalData->m_initialFile = map;
	DEBUG_LOG(( "Test aids: -mission %s %d starts %s\n", campaign.str(), number, map.str() ));
	return TRUE;
}
