/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: TestAidsLogic.cpp ///////////////////////////////////////////////////////////////////////
// Test helpers, logic side: executes the game command MSG_TEST_AID.
//
// Everything that touches the game logic runs as a command through
// GameLogic::logicMessageDispatcher, so the recorder writes it into the replay and playback
// computes the same. Without a test helper command nothing changes in the simulation.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/LogicToClient.h"

#include "Common/Energy.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Recorder.h"
#include "Common/Science.h"
#include "Common/TestAids.h"

#include "GameClient/InGameUI.h"

#include "GameLogic/AI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/ScriptActions.h"
#include "GameLogic/Scripts.h"

#include <vector>

// ------------------------------------------------------------------------------------------------
Bool TestAids_LogicAllowed( void )
{
	if( TheGameLogic == nullptr || !TheGameLogic->isInGame() )
		return FALSE;
	const Int mode = TheGameLogic->getGameMode();
	if( mode == GAME_SINGLE_PLAYER || mode == GAME_SKIRMISH )
		return TRUE;
	// Replay: the mode of the recorded game counts. A multiplayer replay can only contain the command
	// if someone sent it; the recording logic dropped it then, so playback drops it too.
	if( mode == GAME_REPLAY && TheRecorder != nullptr )
	{
		const Int original = TheRecorder->getGameMode();
		return (original == GAME_SINGLE_PLAYER || original == GAME_SKIRMISH) ? TRUE : FALSE;
	}
	return FALSE;
}

// ------------------------------------------------------------------------------------------------
Bool TestAids_NoCooldown( const Player *player )
{
#if defined(_DEBUG)
	// EA developer cheat (DEMO_TOGGLE_SPECIAL_POWER_DELAYS), global for all players
	if( TheGlobalData->m_specialPowerUsesDelay == FALSE )
		return TRUE;
#endif
	return (player != nullptr && player->hasTestAid( TESTAID_NO_COOLDOWN )) ? TRUE : FALSE;
}

// ------------------------------------------------------------------------------------------------
/** Feedback only for the local player and only when rendering. Display only; the logic does
	* not depend on it. */
// ------------------------------------------------------------------------------------------------
static void notice( const Player *target, const wchar_t *text, Bool withState = FALSE, Bool state = FALSE )
{
	if( TheInGameUI == nullptr || TheGlobalData->m_runnerMode || target != ThePlayerList->getLocalPlayer() )
		return;
	UnicodeString s;
	if( withState )
		s.format( L"Test aid: %s %s", text, state ? L"on" : L"off" );
	else
		s.format( L"Test aid: %s", text );
	TheLogicToClient->uiMessage( s );
}

// ------------------------------------------------------------------------------------------------
static void toggle( Player *target, UnsignedInt bit, const wchar_t *text )
{
	const Bool enabled = !target->hasTestAid( bit );
	target->setTestAid( bit, enabled );
	notice( target, text, TRUE, enabled );
}

// ------------------------------------------------------------------------------------------------
void TestAids_Execute( const GameMessage *msg, Player *sender, AIGroup *selection )
{
	if( !TestAids_LogicAllowed() || msg == nullptr || msg->getArgumentCount() < 1 )
		return;

	const Int command = msg->getArgument( 0 )->integer;
	const Int targetIndex = (msg->getArgumentCount() >= 2) ? msg->getArgument( 1 )->integer : -1;
	Player *target = (targetIndex >= 0) ? ThePlayerList->getNthPlayer( targetIndex ) : sender;
	if( target == nullptr )
		return;

	DEBUG_LOG(( "test aid %d for player %d in frame %u\n", command, target->getPlayerIndex(), TheGameLogic->getFrame() ));

	switch( command )
	{
		case TA_MONEY_PLUS:
			target->getMoney()->deposit( TESTAID_MONEY_AMOUNT );
			notice( target, L"+10,000 money" );
			break;

		case TA_FREEZE_MONEY:
			toggle( target, TESTAID_MONEY_FROZEN, L"Money frozen" );
			break;

		case TA_UNLIMITED_POWER:
			toggle( target, TESTAID_UNLIMITED_POWER, L"Unlimited power" );
			break;

		case TA_POWER_PLUS:
		case TA_POWER_MINUS:
		{
			Energy *energy = target->getEnergy();
			energy->setTestAidPowerBonus( energy->getTestAidPowerBonus()
				+ ((command == TA_POWER_PLUS) ? TESTAID_POWER_STEP : -TESTAID_POWER_STEP) );
			if( TheInGameUI != nullptr && !TheGlobalData->m_runnerMode && target == ThePlayerList->getLocalPlayer() )
				TheLogicToClient->uiMessage( UnicodeString( L"Test aid: power bonus %+d" ), energy->getTestAidPowerBonus() );
			break;
		}

		case TA_GENERAL_POINT:
			target->addSciencePurchasePoints( 1 );
			notice( target, L"+1 general point" );
			break;

		case TA_ALL_ABILITIES:
		{
			// as EA DEMO_GIVE_ALL_SCIENCES; the order comes from the ScienceStore and is the same on all
			// machines running the same build
			std::vector<AsciiString> names = TheScienceStore->friend_getScienceNames();
			for( size_t i = 0; i < names.size(); ++i )
			{
				ScienceType st = TheScienceStore->getScienceFromInternalName( names[i] );
				if( st != SCIENCE_INVALID && TheScienceStore->isScienceGrantable( st ) )
					target->grantScience( st );
			}
			notice( target, L"all general abilities" );
			break;
		}

		case TA_RANK_PLUS:
			target->setRankLevel( target->getRankLevel() + 1 );
			notice( target, L"Rank +1" );
			break;

		case TA_INSTANT_BUILD:
			toggle( target, TESTAID_INSTANT_BUILD, L"Instant build" );
			break;

		case TA_NO_COOLDOWN:
			toggle( target, TESTAID_NO_COOLDOWN, L"Special powers without cooldown" );
			break;

		case TA_INVULNERABLE:
			toggle( target, TESTAID_INVULNERABLE, L"Invulnerable" );
			break;

		case TA_REVEAL_MAP:
		{
			// like the script action "reveal map entirely permanently" (ScriptActions::doRevealMapEntirePermanently);
			// undone like EA DEMO_ENSHROUD: remove the permanent vision, explored areas stay fogged
			const Bool enabled = !target->hasTestAid( TESTAID_MAP_REVEALED );
			target->setTestAid( TESTAID_MAP_REVEALED, enabled );
			if( enabled )
				ThePartitionManager->revealMapForPlayerPermanently( target->getPlayerIndex() );
			else
			{
				ThePartitionManager->undoRevealMapForPlayerPermanently( target->getPlayerIndex() );
				ThePartitionManager->shroudMapForPlayer( target->getPlayerIndex() );
			}
			notice( target, L"Map revealed", TRUE, enabled );
			break;
		}

		case TA_DESTROY_SELECTION:
		{
			// as EA MSG_DEBUG_KILL_SELECTION: the sender's logic selection (own objects only)
			if( selection != nullptr && target == sender )
			{
				const VecObjectID &ids = selection->getAllIDs();
				for( VecObjectID::const_iterator it = ids.begin(); it != ids.end(); ++it )
				{
					Object *obj = TheGameLogic->findObjectByID( *it );
					if( obj != nullptr )
						obj->kill();
				}
			}
			break;
		}

		case TA_DESTROY_ALL_ENEMIES:
		{
			// collect first, then kill: kill() can create objects (debris, pilots)
			std::vector<ObjectID> enemies;
			for( Object *obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject() )
			{
				if( obj->isEffectivelyDead() || obj->getControllingPlayer() == nullptr )
					continue;
				if( target->getRelationship( obj->getTeam() ) == ENEMIES )
					enemies.push_back( obj->getID() );
			}
			for( size_t i = 0; i < enemies.size(); ++i )
			{
				Object *obj = TheGameLogic->findObjectByID( enemies[i] );
				if( obj != nullptr && !obj->isEffectivelyDead() )
					obj->kill();
			}
			notice( target, L"All enemies destroyed" );
			break;
		}

		case TA_WIN:
		{
			// as EA DEMO_WIN (ScriptEngine::debugVictory), and free the script action afterwards
			ScriptAction *action = newInstance( ScriptAction )( ScriptAction::VICTORY );
			TheScriptActions->executeAction( action );
			deleteInstanceOf(action);
			break;
		}

		default:
			DEBUG_LOG(( "test aid: unknown command %d\n", command ));
			break;
	}
}
