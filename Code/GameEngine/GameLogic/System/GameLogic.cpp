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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: GameLogic.cpp ////////////////////////////////////////////////////////////////////////////
// GameLogic class implementation
// Author: Michael S. Booth, October 2000
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/LogicToClient.h"
#include "formatinto.h"
#include "GameLogic/DesyncReport.h"
#include "Common/LogicAudio.h"
#include "Common/OwnedSubsystem.h"

#include "Common/AudioAffect.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/BuildAssistant.h"
#include "Common/CRCDebug.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/GameLOD.h"
#include "Common/GameState.h"
#include "Common/INI.h"
#include "Common/LatchRestore.h"
#include "Common/MapObject.h"
#include "Common/ModuleFactory.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Radar.h"
#include "Common/RandomValue.h"
#include "Common/Recorder.h"
#include "Common/StatsCollector.h"
#include "Common/ThingFactory.h"
#include "Common/Team.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Water.h"
#include "GameClient/Snow.h"
#include "Common/WellKnownKeys.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"
#include "Common/XferDeepCRC.h"

#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameText.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/InGameUI.h"
#include "GameClient/LoadScreen.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/TerrainVisual.h"
#include "GameClient/View.h"
#include "GameClient/ControlBar.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/GameWindowTransitions.h"

#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/CaveSystem.h"
#include "GameLogic/CrateSystem.h"
#include "GameLogic/FPUControl.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Module/DestroyModule.h"
#include "GameLogic/Module/OpenContain.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptActions.h"
#include "GameLogic/ScriptConditions.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/GhostObject.h"

#include "Common/DataChunk.h"
#include "GameLogic/Scripts.h"

#include "GameNetwork/GameLobbyCallbacks.h"
#include "GameNetwork/NetworkInterface.h"

DECLARE_PERF_TIMER(SleepyMaintenance)


#include "Engine/Clock.h"
#include "Engine/ThreadRole.h"
#include "Engine/JobSystem.h"
#include "Engine/Watchdog.h"
#include "Lib/SafeString.h"
#include "GameLogic/AIStats.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <vector>

// The logic time in Engine/Clock.h must use the same tick rate as the engine.
static_assert(static_cast<int>(Engine::LogicTime::FRAMES_PER_SECOND) == static_cast<int>(LOGICFRAMES_PER_SECOND),
	"Engine::LogicTime::FRAMES_PER_SECOND and LOGICFRAMES_PER_SECOND must match");





// I'm making this larger now that we know how big our maps are going to be. 
enum { OBJ_HASH_SIZE	= 8192 };

/// The GameLogic singleton instance



// ------------------------------------------------------------------------------------------------
/** This enum is for loading screen bar progress */


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void setFPMode( void )
{
  // Set floating point round mode to CHOP, which only comes
  // into play when precision is exceeded.  This is necessary
  // for the fast float to int routines used elsewhere in the
  // system.
	//
	// Also set floating point precision to low.  It could be
	// anything as long as it is consistent, really, but this
	// is in the (vain?) hope of any slight speed boost.
	//
	_fpreset();

	UnsignedInt curVal = _statusfp();
	UnsignedInt newVal = curVal;
	newVal = (newVal & ~_MCW_RC) | (_RC_NEAR & _MCW_RC);
	//newVal = (newVal & ~_MCW_RC) | (_RC_CHOP & _MCW_RC);
	// x64 MSVC computes with SSE only; there is no x87 precision control, and _MCW_PC is invalid
	// there (CRT assert in ieee.c). Only the rounding mode is set.
	_controlfp(newVal, _MCW_RC);
}

// ------------------------------------------------------------------------------------------------
/** GameLogic class constructor */
// ------------------------------------------------------------------------------------------------
GameLogic::GameLogic( void )
{
	//Added By Sadullah Nader
	//Initializations missing and necessary 
	m_background = nullptr;
	m_CRC = 0;
	m_isInUpdate = FALSE;

	m_rankPointsToAddAtGameStart = 0;
	m_graceEndFrame = 0;
	m_xferRandomStatePending = FALSE;
	m_xferNextObjID = INVALID_ID;

	for(Int i = 0; i < MAX_SLOTS; i++)
	{
		m_progressComplete[i] = FALSE;
		m_progressCompleteTimeout[i] = 0;
	}

	m_shouldValidateCRCs = FALSE;
	
	m_startNewGame = FALSE;
	//

	m_frame = 0;
	m_frameObjectsChangedTriggerAreas = 0;
	m_width = 0;
	m_height = 0;
	m_objList = nullptr;
	m_curUpdateModule = nullptr;
	m_nextObjID = INVALID_ID;
	m_startNewGame = FALSE;
	m_gameMode = GAME_NONE;
	m_rankLevelLimit = 1000;
	m_gamePaused = FALSE;
	m_inputEnabledMemory = TRUE;
	m_mouseVisibleMemory = TRUE;
	m_loadScreen = nullptr;
	m_startFailed = FALSE;
	m_forceGameStartByTimeOut = FALSE;
#ifdef DUMP_PERF_STATS
	m_overallFailedPathfinds = 0;
#endif

	m_loadingMap = FALSE;
	m_loadingSave = FALSE;
	m_clearingGameData = FALSE;
}

// ------------------------------------------------------------------------------------------------
/** Utility function to set class variables to default values. */
// ------------------------------------------------------------------------------------------------
void GameLogic::setDefaults( Bool loadingSaveGame )
{
	m_frame = 0;
	m_width = DEFAULT_WORLD_WIDTH;
	m_height = DEFAULT_WORLD_HEIGHT;
	m_objList = nullptr;
	for (std::vector<UpdateModulePtr>::iterator it = m_sleepyUpdates.begin(); it != m_sleepyUpdates.end(); ++it)
	{
		(*it)->friend_setIndexInLogic(-1);
	}
	m_sleepyUpdates.clear();
	m_curUpdateModule = nullptr;

	//
	// only reset the next object ID allocater counter when we're not loading a save game.
	// for save games, we read this value out of the save game file and it is important
	// that we preserve it as we load and execute the game
	//
	if( loadingSaveGame == FALSE )
		m_nextObjID = static_cast<ObjectID>(1);

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool GameLogic::isInSinglePlayerGame( void )
{
	return (m_gameMode == GAME_SINGLE_PLAYER ||
		(TheRecorder && TheRecorder->getMode() == RecorderModeType::RECORDERMODETYPE_PLAYBACK && TheRecorder->getGameMode() == GAME_SINGLE_PLAYER));
}


//-------------------------------------------------------------------------------------------------
/** Destroy all objects immediately */
//-------------------------------------------------------------------------------------------------
void GameLogic::destroyAllObjectsImmediate()
{
	// destroy all remaining objects
	Object *obj;
	Object *nextObj;
	for( obj = m_objList; obj; obj = nextObj )
	{
		nextObj = obj->getNextObject();
		destroyObject( obj );
	}

	// process the destroy list immediately
	processDestroyList();
	DEBUG_ASSERTCRASH( m_objList == nullptr, ("destroyAllObjectsImmediate: Object list not cleared\n") );

}  // end destroyAllObjectsImmediate

//-------------------------------------------------------------------------------------------------
/**GameLogic class destructor, the destruction order should mirror the
 * initialization order */
// ------------------------------------------------------------------------------------------------
GameLogic::~GameLogic()
{

	// clear any object TOC we might have
	m_objectTOC.clear();

	if (m_background)
	{
		m_background->destroyWindows();
		deleteInstanceOf(m_background);
		m_background = nullptr;
	}

	// destroy all remaining objects
	destroyAllObjectsImmediate();

	// delete the logical terrain
	destroySubsystem(m_own.terrainLogic, ::Engine::context().terrainLogic);

	destroySubsystem(m_own.ghostObjectManager, ::Engine::context().ghostObjectManager);

	// delete the partition manager
	destroySubsystem(m_own.partitionManager, ::Engine::context().partitionManager);

	destroySubsystem(m_own.scriptActions, ::Engine::context().scriptActions);

	destroySubsystem(m_own.scriptConditions, ::Engine::context().scriptConditions);

	// delete the Script Engine
	destroySubsystem(m_own.scriptEngine, ::Engine::context().scriptEngine);

	// a game still running at the end of the program had its collector left behind
	destroySubsystem(m_own.statsCollector, ::Engine::context().statsCollector);
	
	// Null out TheGameLogic
	::Engine::context().gameLogic = nullptr;
}

// ------------------------------------------------------------------------------------------------
/** (re)initialize the instance. */
// ------------------------------------------------------------------------------------------------
void GameLogic::init( void )
{

	setFPMode();

	/// @todo Clear object and destroy lists
	setDefaults( FALSE );

	// create the partition manager
	::Engine::context().partitionManager = adoptSubsystem(m_own.partitionManager, NEW PartitionManager);
	ThePartitionManager->init();
	ThePartitionManager->setName("ThePartitionManager");


	// Create system for holding deleted objects that are
	// still in the partition manager because player has a fogged
	// view of them.
	::Engine::context().ghostObjectManager = adoptSubsystem(m_own.ghostObjectManager, createGhostObjectManager());

	// create the terrain logic
	::Engine::context().terrainLogic = adoptSubsystem(m_own.terrainLogic, createTerrainLogic());
	TheTerrainLogic->init();
	TheTerrainLogic->setName("TheTerrainLogic");

	// Create script engine system.
	::Engine::context().scriptActions = adoptSubsystem(m_own.scriptActions, NEW ScriptActions);		 // Basically, a subsystem of TheScriptEngine.
	::Engine::context().scriptConditions = adoptSubsystem(m_own.scriptConditions, NEW ScriptConditions);	 // Basically, a subsystem of TheScriptEngine.
	::Engine::context().scriptEngine = adoptSubsystem(m_own.scriptEngine, NEW ScriptEngine);
	TheScriptEngine->init();
	TheScriptEngine->setName("TheScriptEngine");

	// create a team for the player
	//DEBUG_ASSERTCRASH(ThePlayerList, ("null ThePlayerList"));
	//ThePlayerList->setLocalPlayer(0);

	m_CRC = 0;
	m_gamePaused = FALSE;
	m_inputEnabledMemory = TRUE;
	m_mouseVisibleMemory = TRUE;
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_progressComplete[i] = FALSE;
		m_progressCompleteTimeout[i] = 0;
	}
	m_forceGameStartByTimeOut = FALSE;

	m_isScoringEnabled = TRUE;
	m_showBehindBuildingMarkers = TRUE;
	m_drawIconUI = TRUE;
	m_showDynamicLOD = TRUE;
	m_scriptHulkMaxLifetimeOverride = -1;
	
	m_isInUpdate = FALSE;

	m_rankPointsToAddAtGameStart = 0;
	m_graceEndFrame = 0;
	m_xferRandomStatePending = FALSE;
	m_xferNextObjID = INVALID_ID;
}

//-------------------------------------------------------------------------------------------------
/** Reset the game logic systems */
//-------------------------------------------------------------------------------------------------
void GameLogic::reset( void )
{
	m_thingTemplateBuildableOverrides.clear();
	m_controlBarOverrides.clear();

	m_gamePaused = FALSE;
	m_inputEnabledMemory = TRUE;
	m_mouseVisibleMemory = TRUE;
	setFPMode();

	// destroy all objects
	destroyAllObjectsImmediate();

	// set the hash to be rather large. We need to optimize this value later.
//	m_objHash.clear();
//	m_objHash.resize(OBJ_HASH_SIZE);
	//
	// Only after destroyAllObjectsImmediate(): a game with more than OBJ_HASH_SIZE (8192) objects
	// has grown m_objVector, and shrinking it first would make removeObjectFromLookupTable() write
	// past size(), while findObjectByID() would no longer find objects that are still alive.
	m_objVector.clear();
	m_objVector.resize(OBJ_HASH_SIZE, nullptr);
	m_objGeneration.reset(OBJ_HASH_SIZE);

	m_nextObjID = static_cast<ObjectID>(1);

	m_frameObjectsChangedTriggerAreas = 0;

	TheGhostObjectManager->reset();
	ThePartitionManager->reset();
	TheTerrainLogic->reset();
	TheAI->reset();
	TheScriptEngine->reset();

	m_CRC = 0;
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_progressComplete[i] = FALSE;
		m_progressCompleteTimeout[i] = 0;
	}
	m_forceGameStartByTimeOut = FALSE;

	destroySubsystem(m_own.statsCollector, ::Engine::context().statsCollector);

	// clear any table of contents we have
	m_objectTOC.clear();

	setDefaults( FALSE );

	m_isScoringEnabled = TRUE;
	m_showBehindBuildingMarkers = TRUE;
	m_drawIconUI = TRUE;
	m_showDynamicLOD = TRUE;
	m_scriptHulkMaxLifetimeOverride = -1;

	// Clean up any water transparency overrides that were generated for this map.
	WaterTransparencySetting *wt = const_cast<WaterTransparencySetting*>(TheWaterTransparency.getNonOverloadedPointer());
	if (wt)	// without a WaterTransparency block in Water.ini it only exists after the water is drawn
		TheWaterTransparency = static_cast<WaterTransparencySetting*>(wt->deleteOverrides());

	// Clean up any weather overrides that were generated for this map.
	WeatherSetting *ws = const_cast<WeatherSetting*>(TheWeatherSetting.getNonOverloadedPointer());
	if (ws)	// without Weather.ini it only exists after SnowManager::init
		TheWeatherSetting = static_cast<WeatherSetting*>(ws->deleteOverrides());

	m_rankPointsToAddAtGameStart = 0;
	m_graceEndFrame = 0;
	m_xferRandomStatePending = FALSE;
	m_xferNextObjID = INVALID_ID;
}  // end reset

// ------------------------------------------------------------------------------------------------
/** Delete the load screen */
// ------------------------------------------------------------------------------------------------
void GameLogic::deleteLoadScreen( void )
{

	if( m_loadScreen )
	{

		m_loadScreen.reset();

	}  // end if

}  // end deleteLoadScreen

//-----------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------





// ------------------------------------------------------------------------------------------------
/** Leave a game whose start failed while its map.ini or map was read; the game data is cleared through
  * the usual message, so network games tell the other players that we left. */
// ------------------------------------------------------------------------------------------------
void GameLogic::abortNewGame( void )
{
	m_startFailed = TRUE;
	deleteLoadScreen();
	TheMouse->setVisibility(TRUE);
	TheWritableGlobalData->m_loadScreenRender = FALSE;
	setLoadingMap( FALSE );
	if (isInMultiplayerGame() && TheNetwork)
		TheNetwork->quitGame();
	else
		TheMessageStream->appendMessage( GameMessage::MSG_CLEAR_GAME_DATA );
}

// ------------------------------------------------------------------------------------------------
/** Process the destroy list, destroying all pending objects.
 * The destroy list exists to ensure that all objects have a chance to
 * see each other at each simulation frame - the object list is the
 * same at the start of the update as it is at the end of the update. */
// ------------------------------------------------------------------------------------------------
//DECLARE_PERF_TIMER(processDestroyList)
void GameLogic::processDestroyList( void )
{
	//USE_PERF_TIMER(processDestroyList)

	for( ObjectPointerListIterator iterator = m_objectsToDestroy.begin(); iterator != m_objectsToDestroy.end(); iterator++ )
	{
		Object* currentObject = (*iterator);

		/*
			this looks odd, but is necessary; since erasing a single entry can shuffle others in the list
			(in order to maintain its heap-ness), we must do two passes: one to find the updates for this
			object, another to actually erase 'em. 
			
			(in case you're wondering: yes, this is still more efficient than just deleting them
			and rebalancing the entire heap afterwards, at least for real-world maps, since an individual
			rebalance is O(log N) and a full rebalance is O(N)... so unless you are deleting the majority
			of the objects in the world every frame, we come out well ahead this way.)
		*/

		const Int MAX_SUO = 256;
		UpdateModulePtr sleepyUpdatesForThisObject[MAX_SUO];
		Int numSUO = 0;

		for (std::vector<UpdateModulePtr>::iterator it2 = m_sleepyUpdates.begin(); it2 != m_sleepyUpdates.end(); ++it2)
		{
			UpdateModulePtr u = *it2;
			if (u->friend_getObject() == currentObject && numSUO < MAX_SUO)
			{
				sleepyUpdatesForThisObject[numSUO++] = u;
			}
		}

		for (--numSUO; numSUO >= 0; --numSUO)
		{
			// have to re-get idx each time since each call to erase might change others.
			Int idx = sleepyUpdatesForThisObject[numSUO]->friend_getIndexInLogic();
			DEBUG_ASSERTCRASH(m_sleepyUpdates[idx] == sleepyUpdatesForThisObject[numSUO], ("Hmm, expected update mismatch here"));
			eraseSleepyUpdate(idx);
			DEBUG_ASSERTCRASH(sleepyUpdatesForThisObject[numSUO]->friend_getIndexInLogic() == -1, ("Hmm, expected index to be -1 here"));
		}


		if (TheGlobalData->m_logActions)
		{
			const Coord3D *pos = currentObject->getPosition();
			const char *kind = currentObject->isKindOf(KINDOF_STRUCTURE) ? "structure" : currentObject->isKindOf(KINDOF_AIRCRAFT) ? "aircraft"
				: currentObject->isKindOf(KINDOF_VEHICLE) ? "vehicle" : currentObject->isKindOf(KINDOF_INFANTRY) ? "infantry" : "object";
			GameEngine_LogAction(getFrame(), kind, currentObject->getTemplate()->getName().str(), pos->x, pos->y, pos->z);
		}

		currentObject->removeFromList(&m_objList);//remove from object list

		// remove object from lookup table
		removeObjectFromLookupTable( currentObject );

		currentObject->friend_deleteInstance();//actual delete
	}

	m_objectsToDestroy.clear();//list full of bad pointers now, clear it.  If anyone's deletion resulted
	//in the request for a new deletion (sub-object), the new object was added to the end of this list.
}

//-------------------------------------------------------------------------------------------------
/** Process the command list passed to the logic from the network */
//-------------------------------------------------------------------------------------------------
void GameLogic::processCommandList( CommandList *list )
{
	m_cachedCRCs.clear();
	m_shouldValidateCRCs = FALSE;

	GameMessage* msg;

	for( msg = list->getFirstMessage(); msg; msg = msg->next() )
	{
#ifdef _DEBUG
		DEBUG_ASSERTCRASH(msg != nullptr && msg != (GameMessage*)(uintptr_t)0xdeadbeef, ("bad msg"));
#endif
		logicMessageDispatcher( msg, nullptr );
	}

	if (m_shouldValidateCRCs && !TheNetwork->sawCRCMismatch())
	{
		Bool sawCRCMismatch = FALSE;
		Int numPlayers = 0;
		DEBUG_ASSERTCRASH(TheNetwork, ("No Network!"));
		if (TheNetwork)
		{
			for (Int i=0; i<MAX_SLOTS; ++i)
			{
				// observers' commands, their CRCs among them, are not executed in a network game; a player
				// back after a drop counts from its second CRC on
				const GameSlot *slot = TheGameInfo ? TheGameInfo->getConstSlot(i) : nullptr;
				if (TheNetwork->isPlayerConnected(i) && !TheNetwork->isCRCPending(i) && !(slot && slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER))
					++numPlayers;
			}

			if (m_cachedCRCs.size() < numPlayers)
			{
				DEBUG_CRASH(("Not enough CRCs!"));
				sawCRCMismatch = TRUE;
			}
			else
			{
				//DEBUG_LOG(("Comparing %d CRCs on frame %d\n", m_cachedCRCs.size(), m_frame));
				std::map<Int, UnsignedInt>::const_iterator crcIt = m_cachedCRCs.begin();
				Int validatorCRC = crcIt->second;
				//DEBUG_LOG(("Validator CRC from player %d is %8.8X\n", crcIt->first, validatorCRC));
				while (++crcIt != m_cachedCRCs.end())
				{
					Int validatedCRC = crcIt->second;
					//DEBUG_LOG(("CRC to validate is from player %d: %8.8X\n", crcIt->first, validatedCRC));
					if (validatorCRC != validatedCRC)
					{
						DEBUG_CRASH(("CRC mismatch!"));
						sawCRCMismatch = TRUE;
					}
				}
			}
		}

		if (sawCRCMismatch)
		{
#ifdef DEBUG_LOGGING
			DEBUG_LOG(("CRC Mismatch - saw %d CRCs from %d players\n", static_cast<Int>(m_cachedCRCs.size()), numPlayers));
			for (std::map<Int, UnsignedInt>::const_iterator crcIt = m_cachedCRCs.begin(); crcIt != m_cachedCRCs.end(); ++crcIt)
			{
				Player *player = ThePlayerList->getNthPlayer(crcIt->first);
				DEBUG_LOG(("CRC from player %d (%ls) = %X\n", crcIt->first,
					player?player->getPlayerDisplayName().str():L"<NONE>", crcIt->second));
			}
#endif DEBUG_LOGGING
			WriteDesyncReport(m_cachedCRCs, numPlayers);
			TheNetwork->setSawCRCMismatch();
		}
	}

}  // end processCommandList

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool GameLogic::isIntroMoviePlaying()
{
	/// @todo remove this hack
	return m_startNewGame && TheDisplay->isMoviePlaying();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::selectObject(Object *obj, Bool createNewSelection, PlayerMaskType playerMask, Bool affectClient)
{
	if (!obj) 
	{
		return;
	}

	if (!obj->isMassSelectable() && !createNewSelection) 
	{
		DEBUG_LOG(("GameLogic::selectObject() - Object attempted to be added to selection, but isn't mass-selectable.\n"));
		return;
	}

	while( playerMask ) 
	{
		Player *player = ThePlayerList->getEachPlayerFromMask(playerMask);
		if( !player ) 
		{
			return;
		}

		AIGroup *group = nullptr;
		CRCGEN_LOG(( "Creating AIGroup in GameLogic::selectObject()\n" ));
		group = TheAI->createGroup();
		group->add(obj);

		// add all selected agents to the AI group
		if (createNewSelection)	
		{
			player->setCurrentlySelectedAIGroup(group);
		} 
		else 
		{
			player->addAIGroupToCurrentSelection(group);
		}

		TheAI->destroyGroup(group);

		if( affectClient ) 
		{
			Drawable *draw = obj->getDrawable();
			if( draw ) 
			{
				TheInGameUI->selectDrawable(draw);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::deselectObject(Object *obj, PlayerMaskType playerMask, Bool affectClient)
{
	if (!obj) {
		return;
	}

	while (playerMask) {
		Player *player = ThePlayerList->getEachPlayerFromMask(playerMask);
		if (!player) {
			return;
		}

		AIGroup *group = nullptr;
		CRCGEN_LOG(( "Removing a unit from a selected group in GameLogic::deselectObject()\n" ));
		group = TheAI->createGroup();
		player->getCurrentSelectionAsAIGroup(group);
		
		Bool deleted = FALSE;
		Bool actuallyRemoved = FALSE;
		
		if (group) {
			deleted = group->remove(obj);
			actuallyRemoved = TRUE;
		}
		
		if (actuallyRemoved) {
			// Set this to be the currently selected group.
			if (!deleted) {
				player->setCurrentlySelectedAIGroup(group);
				// Then, cleanup the group.
				TheAI->destroyGroup(group);
			} else {
				// NULL will clear the group.
				player->setCurrentlySelectedAIGroup(nullptr);
			}

			if (affectClient) {
				Drawable *draw = obj->getDrawable();
				if (draw) {
					TheInGameUI->deselectDrawable(draw);
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
inline void GameLogic::validateSleepyUpdate() const
{
// pretty slow, so do only for DEBUG for now. turn on if you suspect wonkiness.
#ifdef _DEBUG
	#define SLEEPY_DEBUG
#endif
#ifdef SLEEPY_DEBUG
	int sz = static_cast<int>(m_sleepyUpdates.size());
	if (sz == 0)
		return;

	int i;
	//DEBUG_LOG(("\n\n"));
	//for (i = 0; i < sz; ++i)
	//{
	//	DEBUG_LOG(("u %04d: %08lx %08lx\n",i,m_sleepyUpdates[i],m_sleepyUpdates[i]->friend_getNextCallFrame()));
	//}
	for (i = 0; i < sz; ++i)
	{
		DEBUG_ASSERTCRASH(m_sleepyUpdates[i]->friend_getIndexInLogic() == i, ("index mismatch: expected %d, got %d\n",i,m_sleepyUpdates[i]->friend_getIndexInLogic()));
		UnsignedInt pri = m_sleepyUpdates[i]->friend_getPriority();
		if (i > 0)
		{
			Int i0 = (i+1)/2-1;
			UnsignedInt pri0 = m_sleepyUpdates[i0]->friend_getPriority();
			DEBUG_ASSERTCRASH(pri >= pri0, ("sleepyUpdates are munged (0)"));
		}
		Int i1 = 2*(i+1)-1;
		Int i2 = 2*(i+1);
		if (i1 < sz)
		{
			UnsignedInt pri1 = m_sleepyUpdates[i1]->friend_getPriority();
			DEBUG_ASSERTCRASH(pri <= pri1, ("sleepyUpdates are munged (1)"));
		}
		if (i2 < sz)
		{
			UnsignedInt pri2 = m_sleepyUpdates[i2]->friend_getPriority();
			DEBUG_ASSERTCRASH(pri <= pri2, ("sleepyUpdates are munged (2)"));
		}
	}
#endif
}

// ------------------------------------------------------------------------------------------------
void GameLogic::eraseSleepyUpdate(Int i)
{
	USE_PERF_TIMER(SleepyMaintenance)

	DEBUG_ASSERTCRASH(i >= 0 && i < m_sleepyUpdates.size(), ("bad sleepy idx"));

	// swap with the final item, toss the final item, then rebalance
	m_sleepyUpdates[i]->friend_setIndexInLogic(-1);

	Int final = static_cast<Int>(m_sleepyUpdates.size()) - 1;
	if (i < final)
	{
		m_sleepyUpdates[i] = m_sleepyUpdates[final];
		m_sleepyUpdates[i]->friend_setIndexInLogic(i);
		m_sleepyUpdates.pop_back();
		rebalanceSleepyUpdate(i);
	}
	else
	{
		m_sleepyUpdates.pop_back();
	}
}

// ------------------------------------------------------------------------------------------------
inline Bool isLowerPriority(const UpdateModulePtr a, const UpdateModulePtr b)
{
	// return true iff a is lower pri than b.
	// remember: lower ordinal value means higher priority.
	// therefore, higher ordinal value means lower priority.
	DEBUG_ASSERTCRASH(a && b, ("these may no longer be null"));
	UnsignedInt f1 = a->friend_getPriority();
	UnsignedInt f2 = b->friend_getPriority();
	return f1 > f2;
}

// ------------------------------------------------------------------------------------------------
Int GameLogic::rebalanceParentSleepyUpdate(Int i)
{
	USE_PERF_TIMER(SleepyMaintenance)

	DEBUG_ASSERTCRASH(i >= 0 && i < m_sleepyUpdates.size(), ("bad sleepy idx"));

	Int parent = ((i+1)>>1)-1;
	while (parent >= 0 && isLowerPriority(m_sleepyUpdates[parent], m_sleepyUpdates[i]))
	{
		UpdateModulePtr a = m_sleepyUpdates[parent];
		UpdateModulePtr b = m_sleepyUpdates[i];

		m_sleepyUpdates[i] = a;
		m_sleepyUpdates[parent] = b;

		a->friend_setIndexInLogic(i);
		b->friend_setIndexInLogic(parent);

		i = parent;
		parent = ((parent+1)>>1)-1;
	}

	return i;
}

// ------------------------------------------------------------------------------------------------
Int GameLogic::rebalanceChildSleepyUpdate(Int i)
{
	USE_PERF_TIMER(SleepyMaintenance)

	DEBUG_ASSERTCRASH(i >= 0 && i < m_sleepyUpdates.size(), ("bad sleepy idx"));

// this function gets the brunt of the work (we frequently
// balance down, not up), so this one is hand-unrolled for
// max efficiency. I have left the pristine non-unrolled
// version present for clarity. (Yes, this is worth doing.) (srj) 
#if 1
	// With _ITERATOR_DEBUG_LEVEL, std::vector::operator[] checks the bounds even when only forming a
	// pointer to index size() (see "yes, this is off the end"), although that pointer is only
	// compared, never dereferenced. .data() + index computes the pointer without the check.
	UpdateModulePtr* pI = m_sleepyUpdates.data() + i;

	// our children are i*2 and i*2+1
  Int child = ((i+1)<<1)-1;
	UpdateModulePtr* pChild = m_sleepyUpdates.data() + child;
	UpdateModulePtr* pSZ = m_sleepyUpdates.data() + m_sleepyUpdates.size();	// yes, this is off the end.

  while (pChild < pSZ) 
	{
		// choose the higher-priority of the two children; we must be higher-pri than that.
		if (pChild < pSZ-1 && isLowerPriority(*pChild, *(pChild+1)))
		{
      ++pChild;
			++child;
		}

		// if we're higher-pri than our children, we're done.
		if (!isLowerPriority(*pI, *pChild))
		{
			break;
		}

		// doh. swap with the highest-pri child we have.
		UpdateModulePtr a = *pChild;
		UpdateModulePtr b = *pI;

		*pI = a;
		*pChild = b;

		a->friend_setIndexInLogic(i);
		b->friend_setIndexInLogic(child);

		i = child;
		pI = pChild;

		child = ((i+1)<<1)-1;
		pChild = m_sleepyUpdates.data() + child;
  }
#else
	// our children are i*2 and i*2+1
	Int sz = m_sleepyUpdates.size();
  Int child = ((i+1)<<1)-1;
  while (child < sz) 
	{
		// choose the higher-priority of the two children; we must be higher-pri than that.
		if (child < sz-1 && isLowerPriority(m_sleepyUpdates[child], m_sleepyUpdates[child+1]))
      ++child;
		
		// if we're higher-pri than our children, we're done.
		if (!isLowerPriority(m_sleepyUpdates[i], m_sleepyUpdates[child]))
		{
			break;
		}

		// doh. swap with the highest-pri child we have.
		UpdateModulePtr a = m_sleepyUpdates[child];
		UpdateModulePtr b = m_sleepyUpdates[i];

		m_sleepyUpdates[i] = a;
		m_sleepyUpdates[child] = b;

		a->friend_setIndexInLogic(i);
		b->friend_setIndexInLogic(child);
		i = child;
		child = ((i+1)<<1)-1;
  }
#endif
	return i;
}

// ------------------------------------------------------------------------------------------------
void GameLogic::rebalanceSleepyUpdate(Int i)
{
	USE_PERF_TIMER(SleepyMaintenance)

	i = rebalanceParentSleepyUpdate(i);
	i = rebalanceChildSleepyUpdate(i);
}

// ------------------------------------------------------------------------------------------------
void GameLogic::remakeSleepyUpdate()
{
	USE_PERF_TIMER(SleepyMaintenance)

	Int parent = static_cast<Int>(m_sleepyUpdates.size()) / 2;
  while (true) 
	{
    rebalanceChildSleepyUpdate(parent);
    if (parent == 0)
			break;
    --parent;
  }

	validateSleepyUpdate();
}

// ------------------------------------------------------------------------------------------------
void GameLogic::pushSleepyUpdate(UpdateModulePtr u)
{
	USE_PERF_TIMER(SleepyMaintenance)

	DEBUG_ASSERTCRASH(u != nullptr, ("You may not pass null for sleepy update info"));

	m_sleepyUpdates.push_back(u);
	u->friend_setIndexInLogic(static_cast<Int>(m_sleepyUpdates.size()) - 1);
	
	rebalanceParentSleepyUpdate(static_cast<Int>(m_sleepyUpdates.size())-1);
}

// ------------------------------------------------------------------------------------------------
UpdateModulePtr GameLogic::peekSleepyUpdate() const
{
	USE_PERF_TIMER(SleepyMaintenance)

	UpdateModulePtr u = m_sleepyUpdates.front();
	DEBUG_ASSERTCRASH(u->friend_getIndexInLogic() == 0, ("index mismatch: expected %d, got %d\n",0,u->friend_getIndexInLogic()));
	return u;
}

// ------------------------------------------------------------------------------------------------
void GameLogic::popSleepyUpdate()
{
	USE_PERF_TIMER(SleepyMaintenance)

	Int sz = static_cast<Int>(m_sleepyUpdates.size());
	if (sz == 0)
	{
		DEBUG_CRASH(("should not happen"));
		return;
	}

	m_sleepyUpdates[0]->friend_setIndexInLogic(-1);
	if (sz > 1)
	{
		m_sleepyUpdates[0] = m_sleepyUpdates[sz-1];
		m_sleepyUpdates[0]->friend_setIndexInLogic(0);
		m_sleepyUpdates.pop_back();
		rebalanceChildSleepyUpdate(0);
	}
	else
	{
		m_sleepyUpdates.pop_back();
	}
}

// ------------------------------------------------------------------------------------------------
// this should be called only by UpdateModule, thanks.
// ------------------------------------------------------------------------------------------------
//DECLARE_PERF_TIMER(friend_awakenUpdateModule)
void GameLogic::friend_awakenUpdateModule(Object* obj, UpdateModulePtr u, UnsignedInt whenToWakeUp)
{
	//USE_PERF_TIMER(friend_awakenUpdateModule)
	UnsignedInt now = TheGameLogic->getFrame();
	DEBUG_ASSERTCRASH(whenToWakeUp >= now, ("setWakeFrame frame is in the past... are you sure this is what you want?"));

	if (u == m_curUpdateModule)
	{
		DEBUG_CRASH(("You should not call setWakeFrame() from inside your update(), because it will be ignored, in favor of the return code from update.\n"));
		return;
	}

	if (whenToWakeUp == u->friend_getNextCallFrame())
		return;	// my, that was easy

	if ((now > 0) && (u->friend_getNextCallFrame() == now) && (whenToWakeUp == now + 1))
	{
		// subtle but important case: if we already awake, and someone calls 
		// setWakeFrame(self, UPDATE_SLEEP_NONE), we don't want to reset our wake frame,
		// since that would prevent us from getting called THIS frame. since UPDATE_SLEEP_NONE
		// really means "wake up as soon as possible", we don't want to change our status
		// if we are already awake. (srj)
		return;
	}

	Int idx = u->friend_getIndexInLogic();
	if (obj->isInList(&m_objList))
	{
		if (idx < 0 || idx >= m_sleepyUpdates.size())
		{
			RELEASE_CRASH("fatal error! sleepy update module illegal index.\n");
			return;
		}

		if (m_sleepyUpdates[idx] != u)
		{
			RELEASE_CRASH("fatal error! sleepy update module index mismatch.\n");
			return;
		}

		// update the value.
		u->friend_setNextCallFrame(whenToWakeUp);

		// rebalance.
		rebalanceSleepyUpdate(idx);
		
		// validate. (harmless except in debug mode)
		validateSleepyUpdate();

		return;
	}
	else
	{
		if (idx != -1)
		{
			RELEASE_CRASH("fatal error! sleepy update module index mismatch.\n");
			return;
		}

		// this can happen if stuff happens during object initialization. fortunately, 
		// it's easy to deal with:
		u->friend_setNextCallFrame(whenToWakeUp);
		return;
	}
}







// ------------------------------------------------------------------------------------------------

DECLARE_PERF_TIMER(GameLogic_update)
DECLARE_PERF_TIMER(GameLogic_update_normal)
DECLARE_PERF_TIMER(GameLogic_update_sleepy)

#ifdef DUMP_PERF_STATS
extern __int64 Total_Get_Texture_Time;
extern __int64 Total_Get_HAnim_Time;
extern __int64 Total_Create_Render_Obj_Time;
extern __int64 Total_Load_3D_Assets;
#endif

// ------------------------------------------------------------------------------------------------
// Time per tick phase and per update module type. Collected only with -crcLog (measurement runs), so a
// normal game pays nothing but a null check per phase and update call.
// ------------------------------------------------------------------------------------------------
namespace
{
	enum LogicPhase
	{
		PHASE_SCRIPT, PHASE_TERRAIN, PHASE_COMMANDS, PHASE_UPDATES, PHASE_AI, PHASE_BUILD, PHASE_PARTITION, PHASE_ENDOFTICK,
		PHASE_COUNT
	};
	const char *const s_phaseNames[PHASE_COUNT] =
		{ "skript", "gelaende", "befehle", "updates", "ki", "bau", "partition", "tickende" };

	struct PhaseStatistics
	{
		double sumMs[PHASE_COUNT] = {};
		double maxMs[PHASE_COUNT] = {};
		UnsignedInt ticks = 0;
		struct ModuleTime { double ms = 0.0; double maxMs = 0.0; unsigned long long calls = 0; };
		std::unordered_map<NameKeyType, ModuleTime> modules;
		// the slowest single update call
		double slowestMs = 0.0;
		UnsignedInt slowestFrame = 0;
		NameKeyType slowestModule = NAMEKEY_INVALID;
		AsciiString slowestObject;
		// the slowest tick and its phases
		double slowestTickMs = 0.0;
		UnsignedInt slowestTickFrame = 0;
		double slowestTickPhaseMs[PHASE_COUNT] = {};
		double tickPhaseMs[PHASE_COUNT] = {};
	};
	PhaseStatistics *s_phaseStats = nullptr;
	// The first ticks load assets on demand; the spike report starts after them.
	const UnsignedInt PHASE_WARMUP_TICKS = 30;

	using PhaseClock = std::chrono::steady_clock;

	inline double elapsedMs( PhaseClock::time_point start )
	{
		return std::chrono::duration<double, std::milli>( PhaseClock::now() - start ).count();
	}

	class PhaseTimer
	{
	public:
		explicit PhaseTimer( LogicPhase phase ) : m_phase( phase ), m_active( s_phaseStats != nullptr )
		{
			if( m_active )
				m_start = PhaseClock::now();
		}
		~PhaseTimer()
		{
			if( !m_active || s_phaseStats == nullptr )
				return;
			const double ms = elapsedMs( m_start );
			s_phaseStats->tickPhaseMs[ m_phase ] += ms;
			s_phaseStats->sumMs[ m_phase ] += ms;
			if( ms > s_phaseStats->maxMs[ m_phase ] )
				s_phaseStats->maxMs[ m_phase ] = ms;
		}
	private:
		LogicPhase m_phase;
		Bool m_active;
		PhaseClock::time_point m_start;
	};
}

Bool GameLogic::formatPhaseStatistics( AsciiString &phases, AsciiString &modules, AsciiString &spikes ) const
{
	if( s_phaseStats == nullptr || s_phaseStats->ticks == 0 )
		return FALSE;
	const double ticks = static_cast<double>( s_phaseStats->ticks );
	double total = 0.0;
	for( Int i = 0; i < PHASE_COUNT; ++i )
		total += s_phaseStats->sumMs[ i ];
	phases.format( "LOGIKPHASEN %u Ticks, %.4f ms je Tick (Mittel/hoechstens ms):", s_phaseStats->ticks, total / ticks );
	for( Int i = 0; i < PHASE_COUNT; ++i )
	{
		AsciiString part;
		part.format( " %s %.4f/%.3f", s_phaseNames[ i ], s_phaseStats->sumMs[ i ] / ticks, s_phaseStats->maxMs[ i ] );
		phases.concat( part );
	}

	// the twelve most expensive update module types
	std::vector<std::pair<NameKeyType, PhaseStatistics::ModuleTime>> list( s_phaseStats->modules.begin(), s_phaseStats->modules.end() );
	std::sort( list.begin(), list.end(),
		[]( const auto &a, const auto &b ) { return a.second.ms > b.second.ms || ( a.second.ms == b.second.ms && a.first < b.first ); } );
	modules.format( "LOGIKMODULE %u Typen (ms je Tick, Aufrufe je Tick, laengster Aufruf ms):", static_cast<UnsignedInt>( list.size() ) );
	for( size_t i = 0; i < list.size() && i < 12; ++i )
	{
		AsciiString part;
		part.format( " %s %.4f %.1f %.3f", TheNameKeyGenerator->keyToName( list[ i ].first ).str(),
			list[ i ].second.ms / ticks, static_cast<double>( list[ i ].second.calls ) / ticks, list[ i ].second.maxMs );
		modules.concat( part );
	}

	// where the longest tick and the longest single update went
	spikes.format( "LOGIKSPITZE ab Tick %u: laengster Tick %u %.3f ms:", PHASE_WARMUP_TICKS + 1, s_phaseStats->slowestTickFrame, s_phaseStats->slowestTickMs );
	AsciiString part;
	for( Int i = 0; i < PHASE_COUNT; ++i )
	{
		part.format( " %s %.3f", s_phaseNames[ i ], s_phaseStats->slowestTickPhaseMs[ i ] );
		spikes.concat( part );
	}
	if( s_phaseStats->slowestModule != NAMEKEY_INVALID )
	{
		part.format( "; laengstes Update %.3f ms Tick %u %s an %s", s_phaseStats->slowestMs, s_phaseStats->slowestFrame,
			TheNameKeyGenerator->keyToName( s_phaseStats->slowestModule ).str(), s_phaseStats->slowestObject.str() );
		spikes.concat( part );
	}
	return TRUE;
}

void GameLogic::releasePhaseStatistics( void )
{
	delete s_phaseStats;
	s_phaseStats = nullptr;
}

// ------------------------------------------------------------------------------------------------
/** Update all objects in the world by invoking their update() methods. */
// ------------------------------------------------------------------------------------------------
void GameLogic::update( void )
{
	ASSERT_SIM_THREAD();
	USE_PERF_TIMER(GameLogic_update)

	LatchRestore<Bool> inUpdateLatch(m_isInUpdate, TRUE);

	setFPMode();
	
	/// @todo remove this hack
	if ( m_startNewGame && !TheDisplay->isMoviePlaying())
	{
	#ifdef DUMP_PERF_STATS
		Total_Get_Texture_Time=0;
		Total_Get_HAnim_Time=0;
		Total_Create_Render_Obj_Time=0;
		Total_Load_3D_Assets=0;
	#endif

		startNewGame( FALSE );
		m_startNewGame = FALSE;

	#ifdef DUMP_PERF_STATS
		char Buf[1024];
		__int64 freq64;
		GetPrecisionTimerTicksPerSec(&freq64);

		WWFormat::Into(Buf, "Texture={:f}, Anim={:f}, CreateRobj={:f}, Load3DAssets={:f}\n", (static_cast<double>(Total_Get_Texture_Time)/static_cast<double>(freq64)*1000.0), (static_cast<double>(Total_Get_HAnim_Time)/static_cast<double>(freq64)*1000.0), (static_cast<double>(Total_Create_Render_Obj_Time)/static_cast<double>(freq64)*1000.0), (static_cast<double>(Total_Load_3D_Assets)/static_cast<double>(freq64)*1000.0));

	DEBUG_LOG(("%s", Buf));
	#endif
	}

	// send the current time to the GameClient
	DEBUG_ASSERTCRASH(TheGameLogic == this, ("hmm, TheGameLogic is not right"));
	UnsignedInt now = TheGameLogic->getFrame();
	TheGameClient->setFrame(now);

	if( s_phaseStats == nullptr && TheGlobalData->m_crcLogFile.isNotEmpty() && isInGame() && !isInShellGame() )
		s_phaseStats = NEW PhaseStatistics;
	if( s_phaseStats != nullptr )
	{
		++s_phaseStats->ticks;
		for( Int i = 0; i < PHASE_COUNT; ++i )
			s_phaseStats->tickPhaseMs[ i ] = 0.0;
	}

	// update (execute) scripts
	{
		PROF_ZONE("Logic::ScriptEngine");
		PhaseTimer phaseTimer( PHASE_SCRIPT );
		TheScriptEngine->UPDATE();
	}

	// the scripts may have queued camera calls; the view must have them before it is asked
	TheLogicToClient->syncCameraForLogic();
	Bool freezeTime = TheTacticalView->isTimeFrozen() && !TheTacticalView->isCameraMovementFinished();
	freezeTime = freezeTime || TheScriptEngine->isTimeFrozenDebug() || TheScriptEngine->isTimeFrozenScript();
	
	if (freezeTime) 
	{
		if (TheCommandList->containsMessageOfType(GameMessage::MSG_CLEAR_GAME_DATA)) 
		{
			TheScriptEngine->forceUnfreezeTime();
		} 
		else 
		{
			/// @todo - make sure this never happens during a network game.  jba.
			return;
		}
	}

	// Note - TerrainLogic update needs to happen after ScriptEngine update, but before object updates.  jba.
	// This way changes in bridges are noted in the script engine before being cleared in TerrainLogic->update
	{
		PROF_ZONE("Logic::TerrainLogic");
		PhaseTimer phaseTimer( PHASE_TERRAIN );
		TheTerrainLogic->UPDATE();
	}

	// force CRC calculation, so we can keep a cache of the last N CRCs.  We do this right where the recorder
	// would be getting the CRC anyway, so replays can get the CRCs from the exact instant in time as the original.
	Bool isMPGameOrReplay = (TheRecorder && TheRecorder->isMultiplayer() && getGameMode() != GAME_SHELL && getGameMode() != GAME_NONE);
	Bool isSoloGameOrReplay = (TheRecorder && !TheRecorder->isMultiplayer() && getGameMode() != GAME_SHELL && getGameMode() != GAME_NONE);
	Bool generateForMP = (isMPGameOrReplay && (m_frame % TheGameInfo->getCRCInterval()) == 0);
#if defined(_DEBUG)
	Bool generateForSolo = isSoloGameOrReplay && ((m_frame && (m_frame%100 == 0)) ||
		(getFrame() > static_cast<UnsignedInt>(TheCRCFirstFrameToLog) && getFrame() < TheCRCLastFrameToLog && ((m_frame % REPLAY_CRC_INTERVAL) == 0)));
#else
	Bool generateForSolo = isSoloGameOrReplay && ((m_frame % REPLAY_CRC_INTERVAL) == 0);
#endif // defined(_DEBUG)

	if (generateForSolo || generateForMP)
	{
		PROF_ZONE("Logic::CRC");
		m_CRC = getCRC( CRC_RECALC );
		if (isMPGameOrReplay)
		{
			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_LOGIC_CRC );
			msg->appendIntegerArgument( m_CRC );
			msg->appendBooleanArgument( (TheRecorder && TheRecorder->getMode() == RecorderModeType::RECORDERMODETYPE_PLAYBACK) ); // playback CRC
			//DEBUG_LOG(("Appended CRC of %8.8X on frame %d\n", m_CRC, m_frame));
		}
		else
		{
			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_LOGIC_CRC );
			msg->appendIntegerArgument( m_CRC );
			msg->appendBooleanArgument( (TheRecorder && TheRecorder->getMode() == RecorderModeType::RECORDERMODETYPE_PLAYBACK) ); // playback CRC
			//DEBUG_LOG(("Appended Playback CRC of %8.8X on frame %d\n", m_CRC, m_frame));
		}
	}

	// CRC log for determinism tests (-crcLog), at a fixed interval and independent of the replay and
	// network CRC. getCRC only reads the state (XferCRC). A player who has left a network game runs its last
	// frames alone, without the others' commands, until it clears the game (Network::isFrameDataReady); those
	// frames are not part of the shared game and are not logged.
	const Bool leftNetworkGame = TheNetwork && TheNetwork->hasLocalPlayerLeft();
	if (TheGlobalData->m_crcLogFile.isNotEmpty() && getGameMode() != GAME_SHELL && getGameMode() != GAME_NONE
		&& (m_frame % TheGlobalData->m_crcLogInterval) == 0 && !leftNetworkGame)
	{
		PROF_ZONE("Logic::CRCLog");
		UnsignedInt crc = (generateForSolo || generateForMP) ? m_CRC : getCRC( CRC_RECALC );
		char line[64];
		WWFormat::Into(line, "{:d} {:08X}", m_frame, crc);
		GameEngine_RunnerLog(line);
	}

	// Log for AI-versus-AI series (-aiStats); only reads the state.
	if (TheGlobalData->m_aiStatsInterval > 0 && getGameMode() != GAME_SHELL && getGameMode() != GAME_NONE
		&& (m_frame % TheGlobalData->m_aiStatsInterval) == 0)
		AIStats_logFrame(m_frame);

	// Deep CRC dump with offset index per logic frame within -deepCRCFrames, next to the -crcLog
	// file. Two runs can be compared byte by byte with it.
	if (TheGlobalData->m_crcLogFile.isNotEmpty() && getGameMode() != GAME_SHELL && getGameMode() != GAME_NONE
		&& static_cast<Int>(m_frame) >= TheGlobalData->m_deepCRCFrameFirst && static_cast<Int>(m_frame) <= TheGlobalData->m_deepCRCFrameLast)
	{
		AsciiString deepName;
		deepName.format("%s.f%u.deep", TheGlobalData->m_crcLogFile.str(), m_frame);
		getCRC( CRC_RECALC, deepName );
	}

	// Test of the desync report without a second player: as if player 1 had reported another CRC.
	if (TheGlobalData->m_desyncReportFrame > 0 && static_cast<Int>(m_frame) == TheGlobalData->m_desyncReportFrame
		&& getGameMode() != GAME_SHELL && getGameMode() != GAME_NONE)
	{
		std::map<Int, UnsignedInt> crcs;
		crcs[0] = m_CRC;
		crcs[1] = m_CRC ^ 1;
		WriteDesyncReport(crcs, 2);
	}

	// collect stats
	if(TheStatsCollector)
	{
		TheStatsCollector->update();
	}

	// Update the Recorder
	{
		TheRecorder->UPDATE();
	}

	// process client commands
	{
		PROF_ZONE("Logic::CommandList");
		PhaseTimer phaseTimer( PHASE_COMMANDS );
		processCommandList( TheCommandList );
	}

	{
		PROF_ZONE("Logic::SleepyUpdates");
		PhaseTimer phaseTimer( PHASE_UPDATES );
		while (!m_sleepyUpdates.empty())
		{
			UpdateModulePtr u = peekSleepyUpdate();

			if (!u)
			{
				DEBUG_CRASH(("Null update. should not happen."));
				continue;
			}

			// we're done, everyone else is sleeping. 
			// break from the loop BEFORE we pop this item off.
			if (u->friend_getNextCallFrame() > now)
			{
				break;
			}

			UpdateSleepTime sleepLen = UPDATE_SLEEP_NONE;	// default, if it is disabled.

			DisabledMaskType dis = u->friend_getObject()->getDisabledFlags();
			if (!dis.any() || dis.anyIntersectionWith(u->getDisabledTypesToProcess()))
			{
				USE_PERF_TIMER(GameLogic_update_sleepy)

				//DEBUG_LOG(("calling update %08lx (%d %d)... ",update,update->friend_getNextCallFrame(),update->friend_getNextCallPhase()));
				m_curUpdateModule = u;

				if( s_phaseStats != nullptr )
				{
					const PhaseClock::time_point start = PhaseClock::now();
					sleepLen = u->update();
					const double ms = elapsedMs( start );
					PhaseStatistics::ModuleTime &t = s_phaseStats->modules[ u->getModuleNameKey() ];
					t.ms += ms;
					++t.calls;
					if( ms > t.maxMs )
						t.maxMs = ms;
					if( ms > s_phaseStats->slowestMs && s_phaseStats->ticks > PHASE_WARMUP_TICKS )
					{
						s_phaseStats->slowestMs = ms;
						s_phaseStats->slowestFrame = now;
						s_phaseStats->slowestModule = u->getModuleNameKey();
						const Object *owner = u->friend_getObject();
						s_phaseStats->slowestObject = owner ? owner->getTemplate()->getName() : AsciiString( "?" );
					}
				}
				else
					sleepLen = u->update();
				DEBUG_ASSERTCRASH(sleepLen > 0, ("you may not return 0 from update"));
				if (sleepLen < 1) 
					sleepLen = UPDATE_SLEEP_NONE;

				m_curUpdateModule = nullptr;

			}

			// else defer it till next frame and re-push it
			u->friend_setNextCallFrame(now + sleepLen);
			rebalanceSleepyUpdate(0);
		}
	}

	validateSleepyUpdate();

	// update the Artificial Intelligence system
	{
		PROF_ZONE("Logic::AI");
		PhaseTimer phaseTimer( PHASE_AI );
		TheAI->UPDATE();
	}

	// production updates
	{
		PROF_ZONE("Logic::BuildAssistant");
		PhaseTimer phaseTimer( PHASE_BUILD );
		TheBuildAssistant->UPDATE();
	}

	// update partition info
	{
		PROF_ZONE("Logic::PartitionManager");
		PhaseTimer phaseTimer( PHASE_PARTITION );
		ThePartitionManager->UPDATE();
	}

	//
	// End of frame clean-up
	//

	{
		PROF_ZONE("Logic::EndOfTick");
		PhaseTimer phaseTimer( PHASE_ENDOFTICK );

	// destroy all pending objects
	processDestroyList();

	// reset the command list, destroying all messages
	TheCommandList->reset();

	TheWeaponStore->UPDATE();	
	TheLocomotorStore->UPDATE();	
	TheVictoryConditions->UPDATE();

	}

	if( s_phaseStats != nullptr )
	{
		double tickMs = 0.0;
		for( Int i = 0; i < PHASE_COUNT; ++i )
			tickMs += s_phaseStats->tickPhaseMs[ i ];
		if( tickMs > s_phaseStats->slowestTickMs && s_phaseStats->ticks > PHASE_WARMUP_TICKS )
		{
			s_phaseStats->slowestTickMs = tickMs;
			s_phaseStats->slowestTickFrame = now;
			for( Int i = 0; i < PHASE_COUNT; ++i )
				s_phaseStats->slowestTickPhaseMs[ i ] = s_phaseStats->tickPhaseMs[ i ];
		}
	}

	{
		//Handle disabled statii (and re-enable objects once frame matches)
		for( Object *obj = m_objList; obj; obj = obj->getNextObject() )
		{
			if( obj->isDisabled() )
			{
				obj->checkDisabledStatus();
			}
		}
	}

  



	// Start protection: announce its length at the start and its end.
	if (m_graceEndFrame != 0 && (m_frame == 1 || m_frame == m_graceEndFrame))
	{
		Bool exists = FALSE;
		if (m_frame == 1)
		{
			UnicodeString text = TheGameText ? TheGameText->fetch( "GZH:GracePeriodStart", &exists ) : UnicodeString::TheEmptyString;
			if (exists)
				TheLogicToClient->uiMessage( text, static_cast<Int>(m_graceEndFrame / (60 * LOGICFRAMES_PER_SECOND)) );
		}
		else
		{
			UnicodeString text = TheGameText ? TheGameText->fetch( "GZH:GracePeriodEnded", &exists ) : UnicodeString::TheEmptyString;
			if (exists)
				TheLogicToClient->uiMessage( text );
		}
		char line[64];
		WWFormat::Into(line, "GRACE frame {:d} end {:d}", m_frame, m_graceEndFrame);
		GameEngine_RunnerLog(line);
	}

	// Desync report at the chosen frame (-crcReport <file> <frame>).
	if (TheGlobalData->m_crcReportFrame != 0 && m_frame == TheGlobalData->m_crcReportFrame
			&& TheGlobalData->m_crcReportFile.isNotEmpty())
	{
		writeDesyncReport( TheGlobalData->m_crcReportFile.str() );
		GameEngine_RunnerLog( "CRCREPORT written" );
	}

#if defined(_DEBUG)
	if (m_frame <= 1)
		DEBUG_LOG(("Loading: GameLogic::update end (frame %u) at %u ms\n", m_frame, Engine::Clock::milliseconds()));
#endif
	// status line for the runner log (also in release builds, which have no debug log)
	if (TheGlobalData->m_crcLogFile.isNotEmpty() && (m_frame == 1 || (m_frame % 900) == 0))
	{
		Int objects = 0;
		for (Object *o = m_objList; o; o = o->getNextObject())
			++objects;
		char line[160];
		// Also the living particle systems/particles: the count shows whether the particle simulation
		// runs in the runner without drawing (see GameClient::update).
		WWFormat::Into(line, "STATE frame {:d} mode {:d} players {:d} objects {:d} recorder {:d} particles {:d}/{:d}", m_frame, static_cast<Int>(getGameMode()), ThePlayerList ? ThePlayerList->getPlayerCount() : -1, objects, TheRecorder ? static_cast<Int>(TheRecorder->getMode()) : -1, TheParticleSystemManager ? TheParticleSystemManager->getParticleSystemCount() : 0, TheParticleSystemManager ? TheParticleSystemManager->getParticleCount() : 0);
		GameEngine_RunnerLog(line);
	}
	// increment world time
	if (!m_startNewGame)
	{
		m_frame++;
	}
}

// ------------------------------------------------------------------------------------------------
/** Return the first object in the world list */
// ------------------------------------------------------------------------------------------------
Object *GameLogic::getFirstObject( void )
{
	return m_objList;
}

// ------------------------------------------------------------------------------------------------
/** Return a new unique object id. */
// ------------------------------------------------------------------------------------------------
ObjectID GameLogic::allocateObjectID( void )
{
	/// @todo Find unused value in current object set
	ObjectID ret = m_nextObjID;
	m_nextObjID = static_cast<ObjectID>(static_cast<UnsignedInt>(m_nextObjID) + 1);
	return ret;
}

// ------------------------------------------------------------------------------------------------
/** Add object ID to the lookup table */
// ------------------------------------------------------------------------------------------------
void GameLogic::addObjectToLookupTable( Object *obj )
{

	// sanity
	if( obj == nullptr )
		return;

	// add to lookup
//	m_objHash[ obj->getID() ] = obj;
	ObjectID newID = obj->getID();
	// Lower bound for doubling: with an empty vector, size() * 2 would stay 0 and the loop would
	// never end.
	while( newID >= m_objVector.size() ) // Fail case is hella rare, so faster to double up on size() call
		m_objVector.resize((m_objVector.size() > 0) ? (m_objVector.size() * 2) : OBJ_HASH_SIZE, nullptr);

	m_objVector[ newID ] = obj;

	// Bump the slot generation, so a handle to an earlier object in this slot no longer matches.
	m_objGeneration.onSlotOccupied( static_cast<size_t>(newID) );

}  // end addObjectToLookupTable

// ------------------------------------------------------------------------------------------------
/** Remove object from the ID lookup table */
// ------------------------------------------------------------------------------------------------
void GameLogic::removeObjectFromLookupTable( Object *obj )
{

	// sanity
	if( obj == nullptr )
		return;

	// remove from lookup table
//	m_objHash.erase( obj->getID() );
	// bounds check as in findObjectByID(): reset() shrinks the table
	if( static_cast<UnsignedInt>(obj->getID()) < m_objVector.size() )
		m_objVector[ obj->getID() ] = nullptr;

	// Death bumps the generation too: a handle to this object resolves to NULL from now on, even if
	// the slot is reused right away.
	m_objGeneration.onSlotFreed( static_cast<size_t>(obj->getID()) );

}  // end removeObjectFromLookupTable

// ------------------------------------------------------------------------------------------------
/** Make a handle to a living object. */
// ------------------------------------------------------------------------------------------------
ObjectHandle GameLogic::makeObjectHandle( const Object *obj ) const
{
	if( obj == nullptr || obj->getID() == INVALID_ID )
		return ObjectHandle();

	return ObjectHandle( obj->getID(), getObjectGeneration( obj->getID() ) );
}

// ------------------------------------------------------------------------------------------------
/** Object for the handle, or NULL if it no longer lives (or another object took the slot
	* in the meantime). */
// ------------------------------------------------------------------------------------------------
Object *GameLogic::resolveObjectHandle( const ObjectHandle& handle )
{
	if( !handle.isSet() )
		return nullptr;

	Object *obj = findObjectByID( handle.getID() );
	if( obj == nullptr )
		return nullptr;

	if( getObjectGeneration( handle.getID() ) != handle.getGeneration() )
		return nullptr;	// slot reused in the meantime

	return obj;
}

// ------------------------------------------------------------------------------------------------
/** Generation of a slot in the object table (0 = never used). */
// ------------------------------------------------------------------------------------------------
ObjectGeneration GameLogic::getObjectGeneration( ObjectID id ) const
{
	if( id == INVALID_ID )
		return 0;

	return m_objGeneration.generation( static_cast<size_t>(id) );
}

// ------------------------------------------------------------------------------------------------
/** Given an object, register it with the GameLogic and give it a unique ID. */
// ------------------------------------------------------------------------------------------------
void GameLogic::registerObject( Object *obj )
{

	// add the object to the global list
	obj->prependToList(&m_objList);

	// add object to lookup table
	addObjectToLookupTable( obj );

	UnsignedInt now = TheGameLogic->getFrame();
	if (now == 0)
		now = 1;
	for (BehaviorModule** b = obj->getBehaviorModules(); *b; ++b)
	{
#ifdef DIRECT_UPDATEMODULE_ACCESS
		// evil, but necessary at this point. (srj)
		UpdateModulePtr u = static_cast<UpdateModulePtr>((*b)->getUpdate());
#else
		UpdateModulePtr u = (*b)->getUpdate();
#endif
		if (!u)
			continue;

		UnsignedInt when = u->friend_getNextCallFrame();
		// note that 'when' can be zero here for any update module
		// that didn't bother to call setWakeFrame() in its ctor.
		// this is legal.
		if (when == 0)
			u->friend_setNextCallFrame(now);
		{
			DEBUG_ASSERTCRASH(u->friend_getNextCallFrame() >= now, ("you may not specify a zero initial sleep time for sleepy modules (%d %d)",u->friend_getNextCallFrame(),now));
			pushSleepyUpdate(u);
		}
	}

}

// ------------------------------------------------------------------------------------------------
/** create an object based on the thing template, the reason that this is
	* here and not in the ThingFactory is so that we can mirror what the
	* creation process is on the client side because clients have specific
	* device dependent drawables (such as a W3DDrawable derived from Drawable).
	* here we will allocate an object of the correct type based on thing
	* template properties
	*
	* if we want to have the thing manager actually contain the pools of
	* object and drawable storage it seems OK to have it be friends with the
	* GameLogic/Client for those purposes, or we could put the allocation pools
	* in the GameLogic and GameClient themselves */
// ------------------------------------------------------------------------------------------------
Object *GameLogic::friend_createObject( const ThingTemplate *thing, const ObjectStatusMaskType &statusBits, Team *team )
{
	Object *obj;

	obj = newInstance(Object)( thing, statusBits, team );

	return obj;
} 

// ------------------------------------------------------------------------------------------------
/** Mark the object as destroyed, and place on list for deletion at the end of the next update.
 * This is the only interface to destroy objects - objects cannot be directly deleted. */
// ------------------------------------------------------------------------------------------------
void GameLogic::destroyObject( Object *obj )
{
	DEBUG_ASSERTCRASH(obj != nullptr, ("destroying null object"));

	// if already flagged for destruction, ignore
	if (!obj || obj->isDestroyed())
		return;

	// run the object onDestroy event if provied
	for (BehaviorModule** m = obj->getBehaviorModules(); *m; ++m)
	{
		DestroyModuleInterface* destroy = (*m)->getDestroy();
		if (destroy)
			destroy->onDestroy();
	}

	// mark object as destroyed
	obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_DESTROYED ) );

	// We desperately need to stop here, or else the destructor of the statemachine will try to do
	// stopping logic, which uses virtual functions and deleted modules, which will crash us.
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if( ai )
	{
		ai->setLocomotorGoalNone();
		ai->destroyPath();
	}

	// add to end of destruction list, in case something is being destroyed and trying to destroy subobjects
	m_objectsToDestroy.push_back(obj);

	// run any on destroy logic internal to the object
	obj->onDestroy();

	// remove wall pieces from the pathfinder
	if( obj->isKindOf( KINDOF_WALK_ON_TOP_OF_WALL ) )
		TheAI->pathfinder()->removeWallPiece( obj );

	//Clean up special power shortcut bars
	if( obj->hasAnySpecialPower() )
	{
		if( ThePlayerList->getLocalPlayer() == obj->getControllingPlayer() )
		{
			TheLogicToClient->controlBarMarkUIDirty();
		}
	}


}  // end destroyObject

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
/** A new GameLogic object has been constructed, therefore create
 * a corresponding drawable and bind them together. */
// ------------------------------------------------------------------------------------------------
void GameLogic::sendObjectCreated( Object *obj )
{
	Drawable *draw = TheThingFactory->newDrawable(obj->getTemplate());

/// @todo COLIN ... shouldn't we have a check here for existing drawable!!!!!

	// bind drawable to object and object to drawable
	bindObjectAndDrawable(obj, draw);

}

// ------------------------------------------------------------------------------------------------
void GameLogic::bindObjectAndDrawable(Object* obj, Drawable* draw)
{
	draw->friend_bindToObject( obj );
	obj->friend_bindToDrawable( draw );
}

// ------------------------------------------------------------------------------------------------
/** Send notification of object destruction. */
// ------------------------------------------------------------------------------------------------
void GameLogic::sendObjectDestroyed( Object *obj )
{
	// Because this implementation is a bridge between the Logic and Interface,
	// we must take extra care to handle such cases as when the system it
	// shutting down.
	if(TheGameClient == nullptr)
		return;

	// destroy the drawable
	Drawable *draw = obj->getDrawable();
	if(draw)
	{
		TheGameClient->destroyDrawable( draw );
	}

	// erase the binding of the drawable to this object
	obj->friend_bindToDrawable( nullptr );

}

// ------------------------------------------------------------------------------------------------
/** Return if the game is paused or not */
// ------------------------------------------------------------------------------------------------
Bool GameLogic::isGamePaused( void )
{
	return m_gamePaused;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::setGamePaused( Bool paused, Bool pauseMusic )
{
	// We need to ignore an unpause called when we are unpaused or else:
	// Mouse is hidden for some reason (script or something)
	// GamePaused
	// Remember that we were hidden
	// Show mouse
	// GameUnpaused
	// Set mouse the way it was

	// Time passes, mouse is unhidden

	// GameUnpaused
	// Set mouse the way it "was" <--- Was counting on right answer being set in Pause.
	if( paused == m_gamePaused )
		return;

	m_gamePaused = paused; 

	AudioAffect audToAffect = static_cast<AudioAffect>(pauseMusic ? AudioAffect_All : (AudioAffect_All & ~AudioAffect_Music));
	
	if(paused)
	{
		// remember the state of the mouse/input so we can return to the same state once we "unpause"
		m_inputEnabledMemory = TheInGameUI->getInputEnabled();
		m_mouseVisibleMemory = TheMouse->getVisibility();
		
		// Make sure the mouse is visible and the cursor is an arrow
		TheMouse->setVisibility(TRUE);
		TheMouse->setCursor( Mouse::ARROW );
		
		// if Input is enabled, disable it
		if(m_inputEnabledMemory)
		{
			TheInGameUI->setInputEnabled(FALSE);
		}
		TheLogicAudio->pauseAudio(audToAffect);

	}
	else
	{
		// set the mouse/input states to what they were before we paused.
		TheMouse->setVisibility(m_mouseVisibleMemory);
		if(m_inputEnabledMemory)
			TheInGameUI->setInputEnabled(TRUE);
		TheLogicAudio->resumeAudio(audToAffect);

	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::processProgress(Int playerId, Int percentage)
{
	if(m_loadScreen)
		m_loadScreen->processProgress(playerId, percentage);
	lastHeardFrom(playerId);
}

// ------------------------------------------------------------------------------------------------
/** Whenever we get a progress complete packet for a Net Game,
	* Set a flag that that player is ready */
// ------------------------------------------------------------------------------------------------
void GameLogic::processProgressComplete(Int playerId)
{
	if(playerId < 0 || playerId >= MAX_SLOTS)
	{
		DEBUG_ASSERTCRASH(FALSE,("GameLogic::processProgressComplete, Invalid playerid was passed in %d\n", playerId));
		return;
	}
	if(m_progressComplete[playerId] == TRUE)
	{
		DEBUG_LOG(("GameLogic::processProgressComplete, playerId %d is marked TRUE already yet we're trying to mark him as true again\n", playerId));
		return;
	}
	DEBUG_LOG(("Progress Complete for Player %d\n", playerId));
	m_progressComplete[playerId] = TRUE;
	lastHeardFrom(playerId);
}

// ------------------------------------------------------------------------------------------------
/// @TODO: Add check to account for timeouts
// ------------------------------------------------------------------------------------------------
Bool GameLogic::isProgressComplete( void )
{
	//If we're not in a network game, always return true
	if(!isInMultiplayerGame() || !TheNetwork || m_forceGameStartByTimeOut)
		return TRUE;
	// a player coming back into a running game does not wait for the others to load
	if(TheNetwork->isReturning())
		return TRUE;

	// Only loop on the Number of players we got in here
	for(Int i =0; i < MAX_SLOTS; ++i)
	{
		if(!m_progressComplete[i])
			return FALSE;
	}
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::lastHeardFrom( Int playerId )
{
	if( playerId < 0 || playerId >= MAX_SLOTS)
		return;
	m_progressCompleteTimeout[playerId] = Engine::Clock::milliseconds();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::testTimeOut( void )
{
	// if everyone is loaded, lets just load the game like normal.
	if(isProgressComplete())
		return;

	Int curTime = Engine::Clock::milliseconds();
	// Loop and test everyone in our game.
	for(Int i =0; i < MAX_SLOTS; ++i)
	{
		// If they've completed their progress, ignore them
		if(m_progressComplete[i])
			continue;
		if(	m_progressCompleteTimeout[i] + PROGRESS_COMPLETE_TIMEOUT > curTime )
			return;
	}
	// if we made it this far, that means everyone has timed out.
	m_forceGameStartByTimeOut = TRUE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::timeOutGameStart( void )
{
	DEBUG_LOG(("We got the Force TimeOut Start Message\n"));
	m_forceGameStartByTimeOut = TRUE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::initTimeOutValues( void )
{
	if (!TheNetwork)
		return;
	for(Int i = 0; i < TheNetwork->getNumPlayers(); ++i)
	{
		m_progressCompleteTimeout[i] = Engine::Clock::milliseconds();
	}
}

// ------------------------------------------------------------------------------------------------
/** returns the total number of objects in the world */
// ------------------------------------------------------------------------------------------------
UnsignedInt GameLogic::getObjectCount( void )
{
	UnsignedInt totalObjects = 0;
	Object *obj;
	for( obj = getFirstObject(); obj; obj = obj->getNextObject() )
	{
		++totalObjects;
	}
	return totalObjects;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
GhostObjectManager *GameLogic::createGhostObjectManager(void)
{ 
	return NEW GhostObjectManager;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
TerrainLogic *GameLogic::createTerrainLogic( void )
{
	return NEW TerrainLogic;
}

// ------------------------------------------------------------------------------------------------
void GameLogic::setBuildableStatusOverride(const ThingTemplate* tt, BuildableStatus bs)
{
	if (tt)
	{
		m_thingTemplateBuildableOverrides[tt->getName()] = bs;
	}
}

// ------------------------------------------------------------------------------------------------
Bool GameLogic::findBuildableStatusOverride(const ThingTemplate* tt, BuildableStatus& bs) const
{
	if (tt)
	{
		BuildableMap::const_iterator it = m_thingTemplateBuildableOverrides.find(tt->getName());
		if (it != m_thingTemplateBuildableOverrides.end())
		{
			bs = it->second;
			return true;
		}
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
void GameLogic::setControlBarOverride(const AsciiString& commandSetName, Int slot, ConstCommandButtonPtr commandButton)
{
	char buf[256];
	WWFormat::Into(buf, "{:c}{}", '0' + slot, commandSetName.str());
	m_controlBarOverrides[buf] = commandButton;
}

// ------------------------------------------------------------------------------------------------
Bool GameLogic::findControlBarOverride(const AsciiString& commandSetName, Int slot, ConstCommandButtonPtr& commandButton) const
{
	char buf[256];
	WWFormat::Into(buf, "{:c}{}", '0' + slot, commandSetName.str());

	ControlBarOverrideMap::const_iterator it = m_controlBarOverrides.find(buf);
	if (it != m_controlBarOverrides.end())
	{
		commandButton = it->second;	// could be null.
		return true;
	}

	// leave commandButton unmodified.
	return false;
}

#ifdef DUMP_PERF_STATS
// ------------------------------------------------------------------------------------------------
void GameLogic::getAIMetricsStatistics( UnsignedInt *numAI, UnsignedInt *numMoving, UnsignedInt *numAttacking, UnsignedInt *numWaitingForPath, UnsignedInt *overallFailedPathfinds )
{
	Object *obj;
	*numAI = 0;
	*numMoving = 0;
	*numAttacking = 0;
	*numWaitingForPath = 0;
	for( obj = getFirstObject(); obj; obj = obj->getNextObject() )
	{
		AIUpdateInterface *ai = obj->getAI();
		if( ai )
		{
			(*numAI)++;

			if( ai->isMoving() )
			{
				(*numMoving)++;
			}
			if( ai->isWaitingForPath() )
			{
				(*numWaitingForPath)++;
			}
			if( ai->isAttacking() )
			{
				(*numAttacking)++;
			}
		}
	}
	*overallFailedPathfinds = m_overallFailedPathfinds;
}
#endif

// ------------------------------------------------------------------------------------------------
Bool GameLogic::isGraceProtected( const Object *victim, const Object *source, PlayerMaskType sourceMask ) const
{
	if ( m_frame >= m_graceEndFrame || victim == nullptr )
		return FALSE;
	const Player *owner = victim->getControllingPlayer();
	if ( owner == nullptr || owner->getDefaultTeam() == nullptr )
		return FALSE;
	const Player *sourcePlayer = source ? source->getControllingPlayer() : nullptr;
	for ( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		const Player *p = ThePlayerList->getNthPlayer( i );
		if ( p == nullptr || p == owner )
			continue;
		const Bool isSource = sourcePlayer ? (p == sourcePlayer) : ((sourceMask & p->getPlayerMask()) != 0);
		if ( isSource && p->getRelationship( owner->getDefaultTeam() ) == ENEMIES )
			return TRUE;
	}
	return FALSE;
}
