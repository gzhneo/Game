/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: TestAids.h //////////////////////////////////////////////////////////////////////////////
// Test helpers for single player tests: money, power, general's points, instant build, invulnerability,
// reveal map, time lapse, quick save and so on.
//
// Enabled with the command line switch -testhilfen or Options.ini "Testhilfen = yes", effective only in
// single player matches (skirmish, campaign), never in multiplayer (lockstep). Everything that touches
// the game logic runs as game message MSG_TEST_AID through the logic (TestAidsLogic.cpp) and is thus
// recorded in the replay; the client (TestAidsClient.cpp) only sends commands, shows the sidebar and
// the overlay and handles time lapse and quick save.
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _TESTAIDS_H_
#define _TESTAIDS_H_

#include "Lib/BaseType.h"

class AIGroup;
class GameMessage;
class Player;

// ------------------------------------------------------------------------------------------------
/** Test helper commands. The game commands (1 to 99) are stored in replays as the first argument of
	* MSG_TEST_AID: never renumber the values, only append new ones. The client commands (100 and up)
	* never leave the machine. */
// ------------------------------------------------------------------------------------------------
enum TestAidCommand
{
	TA_NONE								= 0,

	// Game commands (MSG_TEST_AID, recorded in the replay)
	TA_MONEY_PLUS						= 1,		///< +10,000 money
	TA_FREEZE_MONEY			= 2,		///< Toggle: spending deducts nothing
	TA_UNLIMITED_POWER		= 3,		///< Toggle: power balance always sufficient
	TA_POWER_PLUS					= 4,		///< Power bonus +TESTAID_POWER_STEP
	TA_POWER_MINUS				= 5,		///< Power bonus -TESTAID_POWER_STEP (to create a shortage)
	TA_GENERAL_POINT				= 6,		///< +1 general's point (science purchase point)
	TA_ALL_ABILITIES		= 7,		///< Unlock all general's abilities
	TA_RANK_PLUS						= 8,		///< Rank +1
	TA_INSTANT_BUILD						= 9,		///< Toggle: build and research without build time
	TA_NO_COOLDOWN			= 10,		///< Toggle: special powers without cooldown
	TA_INVULNERABLE					= 11,		///< Toggle: own objects take no damage
	TA_REVEAL_MAP			= 12,		///< Toggle: map permanently revealed
	TA_DESTROY_SELECTION		= 13,		///< Destroy selected own objects
	TA_DESTROY_ALL_ENEMIES	= 14,	///< Destroy all enemy objects
	TA_WIN									= 15,		///< Mission or match won immediately

	TA_LAST_GAME_COMMAND	= TA_WIN,

	// Client commands (not in the replay)
	TA_MENU								= 100,	///< Sidebar open/closed
	TA_OVERLAY,											///< Overlay of active test helpers on/off
	TA_FAST_FORWARD,									///< Time lapse x1 -> x2 -> x4 -> x8 -> x1
	TA_QUICK_SAVE,						///< Write save game "Testhilfen-Schnellspeicher"
	TA_QUICK_LOAD									///< Load it again
};

// ------------------------------------------------------------------------------------------------
/** Toggle state per player (Player::getTestAids). Stored in the save game (Player::xfer
	* version 9): never renumber the bits. */
// ------------------------------------------------------------------------------------------------
enum TestAidState
{
	TESTAID_MONEY_FROZEN		= 0x0001,
	TESTAID_UNLIMITED_POWER		= 0x0002,
	TESTAID_INSTANT_BUILD						= 0x0004,
	TESTAID_NO_COOLDOWN			= 0x0008,
	TESTAID_INVULNERABLE				= 0x0010,
	TESTAID_MAP_REVEALED		= 0x0020
};

enum
{
	TESTAID_MONEY_AMOUNT			= 10000,	///< TA_MONEY_PLUS
	TESTAID_POWER_STEP	= 5				///< TA_POWER_PLUS/_MINUS
};

// Logic (Source/GameLogic/System/TestAidsLogic.cpp) --------------------------------------------

/// May test helpers take effect in the current match? Single player (skirmish, campaign) or the replay
/// of one; never in multiplayer. Deliberately independent of the command line switch, so replays with
/// test helpers also play back without it.
Bool TestAids_LogicAllowed( void );

/// Executes MSG_TEST_AID (GameLogic::logicMessageDispatcher). Arguments: command (Int), optionally
/// target player index (Int, -1 = sender).
void TestAids_Execute( const GameMessage *msg, Player *sender, AIGroup *selection );

/// Special powers of this player without cooldown? (TESTAID_NO_COOLDOWN, in the debug build
/// also the global EA switch m_specialPowerUsesDelay)
Bool TestAids_NoCooldown( const Player *player );

// Client (Source/GameClient/TestAidsClient.cpp) -----------------------------------------------

/// Test helpers enabled (-testhilfen or Options.ini) and a single player match is running, not a replay
Bool TestAids_ClientAllowed( void );

/// Triggers a command (key, sidebar button, script file); targetPlayer -1 = local player
void TestAids_Command( Int command, Int targetPlayer = -1 );

/// Command for a name from the script file or the test sequence command "testhilfe" (geld_plus,
/// sofortbau, ...); TA_NONE if unknown
Int TestAids_CommandByName( const char *name );

/// Meta message MSG_META_TESTHILFE_* from CommandXlat; TRUE = handled (discard the message)
Bool TestAids_Key( Int metaType );

/// Per client frame (GameClient::update, also with -runner): script file, sidebar, quick load
void TestAids_Update( void );

/// Overlay of the active test helpers (InGameUI::postDraw)
void TestAids_Draw( void );

/// Time lapse factor for the main loop (1, 2, 4 or 8; 1 outside of permitted matches)
Int TestAids_FastForward( void );

/// -mission <campaign> <nr>: sets campaign and mission and enters the map as start file
/// (GameEngine::init, after the campaigns are loaded). FALSE = not found.
Bool TestAids_PrepareMission( void );

#endif // _TESTAIDS_H_
