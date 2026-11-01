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

// Starting a game: start positions, sides and colours, network buildings, load progress, map INI

static void findAndSelectCommandCenter(Object *obj, void* alreadyFound);

// ------------------------------------------------------------------------------------------------
enum
{
	LOAD_PROGRESS_START =0,
	LOAD_PROGRESS_POST_PARTICLE_INI_LOAD = LOAD_PROGRESS_START + 1,
	LOAD_PROGRESS_POST_LOAD_MAP = LOAD_PROGRESS_POST_PARTICLE_INI_LOAD + 1,
	LOAD_PROGRESS_SIDE_POPULATION = LOAD_PROGRESS_POST_LOAD_MAP + 1,  // Increment the next one by at least MAX_SLOTS
	LOAD_PROGRESS_POST_SIDE_LIST_INIT = LOAD_PROGRESS_SIDE_POPULATION + 1 + MAX_SLOTS,
	LOAD_PROGRESS_POST_PLAYER_LIST_RESET = LOAD_PROGRESS_POST_SIDE_LIST_INIT + 1,
	LOAD_PROGRESS_POST_SCRIPT_ENGINE_NEW_MAP = LOAD_PROGRESS_POST_PLAYER_LIST_RESET + 1,
	LOAD_PROGRESS_POST_VICTORY_CONDITION_SETUP = LOAD_PROGRESS_POST_SCRIPT_ENGINE_NEW_MAP + 2,
	LOAD_PROGRESS_POST_VICTORY_CONDITION_SET_VICTORY_CONDITION = LOAD_PROGRESS_POST_VICTORY_CONDITION_SETUP + 1,
	LOAD_PROGRESS_POST_GHOST_OBJECT_MANAGER_RESET = LOAD_PROGRESS_POST_VICTORY_CONDITION_SET_VICTORY_CONDITION + 1,
	LOAD_PROGRESS_POST_TERRAIN_LOGIC_NEW_MAP = LOAD_PROGRESS_POST_GHOST_OBJECT_MANAGER_RESET + 1,
	LOAD_PROGRESS_POST_BRIDGE_LOAD = LOAD_PROGRESS_POST_TERRAIN_LOGIC_NEW_MAP + 1,
	LOAD_PROGRESS_POST_PATHFINDER_NEW_MAP = LOAD_PROGRESS_POST_BRIDGE_LOAD + 1,

	LOAD_PROGRESS_LOOP_ALL_THE_FREAKN_OBJECTS = LOAD_PROGRESS_POST_BRIDGE_LOAD + 1,
	LOAD_PROGRESS_MAX_ALL_THE_FREAKN_OBJECTS = 80,// THE END OF THE BIG ASS PROGRESS PAUSE

	LOAD_PROGRESS_LOOP_INITIAL_NETWORK_BUILDINGS = LOAD_PROGRESS_MAX_ALL_THE_FREAKN_OBJECTS + 1,  // Increment the next one by at least MAX_SLOTS
	LOAD_PROGRESS_POST_INITIAL_NETWORK_BUILDINGS = LOAD_PROGRESS_LOOP_INITIAL_NETWORK_BUILDINGS + MAX_SLOTS + 1,

	LOAD_PROGRESS_POST_PRELOAD_ASSETS = LOAD_PROGRESS_POST_INITIAL_NETWORK_BUILDINGS + 1,
	LOAD_PROGRESS_POST_STARTING_CAMERA = LOAD_PROGRESS_POST_PRELOAD_ASSETS + 1,
	LOAD_PROGRESS_POST_STARTING_CAMERA_2 = LOAD_PROGRESS_POST_STARTING_CAMERA + 1,
	LOAD_PROGRESS_END = 100,
};

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
static Waypoint * findNamedWaypoint(AsciiString name)
{
	for (Waypoint *way = TheTerrainLogic->getFirstWaypoint(); way; way = way->getNext())
	{
		if (way->getName() == name)
		{
			return way;
		}
	}
	return nullptr;
}

static Object * placeObjectAtPosition(Int slotNum, AsciiString objectTemplateName, Coord3D& pos, Player *pPlayer,
																	const PlayerTemplate *pTemplate)
{
	const ThingTemplate* btt = TheThingFactory->findTemplate(objectTemplateName);

	DEBUG_ASSERTCRASH(btt, ("TheThingFactory didn't find a template in placeObjectAtPosition()") );

	Object *obj = TheThingFactory->newObject( btt, pPlayer->getDefaultTeam() );
	DEBUG_ASSERTCRASH(obj, ("TheThingFactory didn't give me a valid Object for player %d's (%ls) starting building\n",
		slotNum, pTemplate->getDisplayName().str()));
	if (obj)
	{
		obj->setOrientation(obj->getTemplate()->getPlacementViewAngle());	
		obj->setPosition( &pos );

		//DEBUG_LOG(("Placed a starting building for %s at waypoint %s\n", playerName.str(), waypointName.str()));
		CRCDEBUG_LOG(("Placed an object for %ls at pos (%g,%g,%g)\n", pPlayer->getPlayerDisplayName().str(),
			pos.x, pos.y, pos.z));
		DUMPCOORD3D(&pos);

		Team *team = pPlayer->getDefaultTeam();
		// Now onCreates were called at the constructor.  This magically created
		// thing needs to be considered as Built for Game specific stuff.
		for (BehaviorModule** m = obj->getBehaviorModules(); *m; ++m)
		{
			CreateModuleInterface* create = (*m)->getCreate();
			if (!create)
				continue;
			create->onBuildComplete();
		}

		// Since the team now has members, activate it.
		// srj sez: team should not be null, but could be for ill-formed user maps. so don't crash.
		if (team)
			team->setActive();
		TheAI->pathfinder()->addObjectToPathfindMap(obj);
		if (obj->getAIUpdateInterface() && !obj->isKindOf(KINDOF_IMMOBILE)) {
			CRCDEBUG_LOG(("Not immobile - adjusting dest\n"));
			if (TheAI->pathfinder()->adjustDestination(obj, obj->getAIUpdateInterface()->getLocomotorSet(), &pos)) {
				DUMPCOORD3D(&pos);
				TheAI->pathfinder()->updateGoal(obj, &pos, LAYER_GROUND);	// Units always start on the ground for now.  jba.
				obj->setPosition( &pos );
			}
		}
	}

	return obj;
}

static void placeNetworkBuildingsForPlayer(Int slotNum, const GameSlot *pSlot, Player *pPlayer, const PlayerTemplate *pTemplate)
{
	Int startPos = pSlot->getStartPos();
	AsciiString waypointName;
	waypointName.format("Player_%d_Start", startPos+1); // start pos waypoints are 1-based

	AsciiString rallyWaypointName;
	rallyWaypointName.format("Player_%d_Rally", startPos+1); // start pos waypoints are 1-based

	Waypoint *waypoint = findNamedWaypoint(waypointName);
	Waypoint *rallyWaypoint = findNamedWaypoint(rallyWaypointName);
	DEBUG_ASSERTCRASH(waypoint, ("Player %d has no starting waypoint (Player_%d_Start)\n", slotNum, startPos));
	if (!waypoint)
		return;

	Coord3D pos = *waypoint->getLocation();
	pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );

	AsciiString buildingTemplateName = pTemplate->getStartingBuilding();

	DEBUG_ASSERTCRASH(!buildingTemplateName.isEmpty(), ("no starting building type for player %d (playertemplate %ls)\n",
		slotNum, pTemplate->getDisplayName().str()));
	if (buildingTemplateName.isEmpty())
		return;

	DEBUG_LOG(("Placing starting building at waypoint %s\n", waypointName.str()));
	Object *conYard = placeObjectAtPosition(slotNum, buildingTemplateName, pos, pPlayer, pTemplate);

	if (!conYard)
		return;

	pPlayer->onStructureCreated(nullptr, conYard);
	pPlayer->onStructureConstructionComplete(nullptr, conYard, FALSE);

	//pos.x -= conYard->getGeometryInfo().getBoundingSphereRadius()/2;
	pos.y -= conYard->getGeometryInfo().getBoundingSphereRadius()/2;
	pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );

	if (rallyWaypoint)
	{
		pos = *rallyWaypoint->getLocation();
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
	}

	for (Int i=0; i<MAX_MP_STARTING_UNITS; ++i)
	{
		AsciiString objName = pTemplate->getStartingUnit(i);
		if (objName.isNotEmpty())
		{
			Coord3D objPos = pos;
			FindPositionOptions options;
			options.minRadius = conYard->getGeometryInfo().getBoundingSphereRadius() * 0.7f;
			options.maxRadius = conYard->getGeometryInfo().getBoundingSphereRadius() * 1.3f;
			DEBUG_LOG(("Placing starting object %d (%s)\n", i, objName.str()));
			ThePartitionManager->update();
			Bool foundPos = ThePartitionManager->findPositionAround(&pos, &options, &objPos);
			if (foundPos)
			{
				Object *unit = placeObjectAtPosition(slotNum, objName, objPos, pPlayer, pTemplate);
				if (unit) {
					pPlayer->onUnitCreated(nullptr, unit);
				}
			}
			else
			{
				DEBUG_LOG(("Could not find position\n"));
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
LoadScreen *GameLogic::getLoadScreen( Bool loadingSaveGame )
{
	switch (m_gameMode) 
	{
	case GAME_SHELL:
		return NEW ShellGameLoadScreen;
		break;                         
	case GAME_SINGLE_PLAYER:
	{
		Campaign* currentCampaign = TheCampaignManager->getCurrentCampaign();
		if( currentCampaign && loadingSaveGame == FALSE )
		{
			if ( currentCampaign->m_isChallengeCampaign) 
			{
				return NEW ChallengeLoadScreen;
			}
			return NEW SinglePlayerLoadScreen;
		}
		else
			return NEW ShellGameLoadScreen;
		break;
	}
	case GAME_SKIRMISH:
		return NEW MultiPlayerLoadScreen;
		break;
	case GAME_LAN:
		return NEW MultiPlayerLoadScreen;
		break;
	case GAME_REPLAY:
		return NEW ShellGameLoadScreen;
		break;
	case GAME_INTERNET:
		// no internet play
		return nullptr;
		break;
	case GAME_NONE:
	default:
		return nullptr;
	}

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void handleNameChange( MapObject *mapObj )
{
	if( !mapObj->getName().compare( "AmericaTankLeopard" ) )
	{
		mapObj->setName( "AmericaTankCrusader" );
		const ThingTemplate *thingTemplate = TheThingFactory->findTemplate( mapObj->getName() );
		mapObj->setThingTemplate( thingTemplate );
	}
	if( !mapObj->getName().compare( "AmericaVehicleHumVee" ) )
	{
		mapObj->setName( "AmericaVehicleHumvee" );
		const ThingTemplate *thingTemplate = TheThingFactory->findTemplate( mapObj->getName() );
		mapObj->setThingTemplate( thingTemplate );
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
static void checkForDuplicateColors( GameInfo *game )
{
	if(!game)
		return;
	Int i;

	// In QuickMatch, people can possibly set preferred color and house.
	// Here, we check for collisions in the color choice.  If there is a
	// collision, the first player will get the color, and the other(s)
	// will map to random.
	for (i=MAX_SLOTS-1; i>=0; --i)
	{
		GameSlot *slot = game->getSlot(i);

		if (!slot || !slot->isOccupied())
			continue;

		Int colorIdx = slot->getColor();
		if (colorIdx < 0 || colorIdx >= TheMultiplayerSettings->getNumColors())
			continue; // don't need to fix up random ones

		slot->setColor(-1);
		if (!game->isColorTaken(colorIdx))
		{
			// we were the only one using the color.  it is safe to keep using it.
			slot->setColor(colorIdx);
		}
		else if (colorIdx >= 0)
		{
			DEBUG_LOG(("Clearing color %d for player %d\n", colorIdx, i));
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
static void populateRandomSideAndColor( GameInfo *game )
{
	if(!game)
		return;
	Int i;

#define MORE_RANDOM
#ifdef MORE_RANDOM
	std::vector<Int> startSlots;
	for (i = 0; i < ThePlayerTemplateStore->getPlayerTemplateCount(); ++i)
	{
		const PlayerTemplate* ptTest = ThePlayerTemplateStore->getNthPlayerTemplate(i);
		if (!ptTest || ptTest->getStartingBuilding().isEmpty())
			continue;

		if ( game->oldFactionsOnly() && !ptTest->isOldFaction() )
		  continue;

		// Prevent from selecting the disabled Generals.
		// This is also enforced at GUI setup (GUIUtil.cpp).
		// @todo: unlock these when something rad happens
		Bool disallowLockedGenerals = TRUE;
		const GeneralPersona *general = TheChallengeGenerals->getGeneralByTemplateName(ptTest->getName());
		Bool startsLocked = general ? !general->isStartingEnabled() : FALSE;
		if (disallowLockedGenerals && startsLocked)
			continue;

		startSlots.push_back(i);
	}
#endif

	for (i=0; i<MAX_SLOTS; ++i)
	{
		GameSlot *slot = game->getSlot(i);

		if (!slot || !slot->isOccupied())
			continue;

		// clean up random factions
		Int playerTemplateIdx = slot->getPlayerTemplate();
		DEBUG_LOG(("Player %d has playerTemplate index %d\n", i, playerTemplateIdx));
		while (playerTemplateIdx != PLAYERTEMPLATE_OBSERVER && (playerTemplateIdx < 0 || playerTemplateIdx >= ThePlayerTemplateStore->getPlayerTemplateCount()))
		{
			DEBUG_ASSERTCRASH(playerTemplateIdx == PLAYERTEMPLATE_RANDOM, ("Non-random bad playerTemplate %d in slot %d\n", playerTemplateIdx, i));
#ifdef MORE_RANDOM
			// our RNG is basically shit -- horribly nonrandom at the start of the sequence.
			// get a few values at random to get rid of the dreck.
			// there's no mathematical basis for this, but empirically, it helps a lot.
			UnsignedInt silly = GetGameLogicRandomSeed() % 7;
			for (Int poo = 0; static_cast<UnsignedInt>(poo) < silly; ++poo) 
			{
				GameLogicRandomValue(0, 1);	// ignore result
			}
			Int idxIdx = GameLogicRandomValue(0, 1000) % startSlots.size();
			playerTemplateIdx = startSlots[idxIdx];
#else
			playerTemplateIdx = GameLogicRandomValue(0, ThePlayerTemplateStore->getPlayerTemplateCount()-1);
#endif
			const PlayerTemplate* pt = ThePlayerTemplateStore->getNthPlayerTemplate(playerTemplateIdx);
			if (!pt || pt->getStartingBuilding().isEmpty())
			{
#ifdef MORE_RANDOM
				DEBUG_CRASH(("should not be possible"));
#endif
				playerTemplateIdx = -1; // only pick playable factions
			}
			else
			{
				DEBUG_LOG(("Setting playerTemplateIdx %d to %d\n", i, playerTemplateIdx));
				slot->setPlayerTemplate(playerTemplateIdx);
			}
		}

		Int colorIdx = slot->getColor();
		if (colorIdx < 0 || colorIdx >= TheMultiplayerSettings->getNumColors())
		{
			DEBUG_ASSERTCRASH(colorIdx == -1, ("Non-random bad color %d in slot %d\n", colorIdx, i));
			while (colorIdx == -1)
			{
				colorIdx = GameLogicRandomValue(0, TheMultiplayerSettings->getNumColors()-1);
				if (game->isColorTaken(colorIdx))
					colorIdx = -1;
			}
			DEBUG_LOG(("Setting color %d to %d\n", i, colorIdx));
			slot->setColor(colorIdx);
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
static void populateRandomStartPosition( GameInfo *game )
{
	if(!game)
		return;

	Int i;
	Int numPlayers = MAX_SLOTS;
	const MapMetaData *md = TheMapCache->findMap( game->getMap() );
  DEBUG_ASSERTCRASH( md , ("Could not find map %s in the mapcache", game->getMap().str()));
	if (md)
		numPlayers = md->m_numPlayers;

	// generate a map of start spot distances
	Real startSpotDistance[MAX_SLOTS][MAX_SLOTS];
	for (i=0; i<MAX_SLOTS; ++i)
	{
		for (Int j=0; j<MAX_SLOTS; ++j)
		{
			if (i != j && (i<numPlayers && j<numPlayers))
			{
				AsciiString w1, w2;
				w1.format("Player_%d_Start", i+1);
				w2.format("Player_%d_Start", j+1);
				WaypointMap::const_iterator c1 = md->m_waypoints.find(w1);
				WaypointMap::const_iterator c2 = md->m_waypoints.find(w2);
				if (c1 == md->m_waypoints.end() || c2 == md->m_waypoints.end())
				{
					// couldn't find a waypoint.  must be kinda far away.
					startSpotDistance[i][j] = 1000000.0f;
				}
				else
				{
					Coord3D p1 = c1->second;
					Coord3D p2 = c2->second;
					startSpotDistance[i][j] = sqrt( sqr(p1.x-p2.x) + sqr(p1.y-p2.y) );
				}
			}
			else
			{
				startSpotDistance[i][j] = 0.0f; // not gonna need this
			}
		}
	}

	// see if a start spot has been chosen at all yet
	Bool hasStartSpotBeenPicked = FALSE;
	Bool taken[MAX_SLOTS];
	for (i=0; i<MAX_SLOTS; ++i)
	{
		taken[i] = (i<numPlayers)?FALSE:TRUE;
	}
	for (i=0; i<MAX_SLOTS; ++i)
	{
		GameSlot *slot = game->getSlot(i);

		if (!slot || !slot->isOccupied() || slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
			continue;

		Int posIdx = slot->getStartPos();
		if (posIdx >= 0 && posIdx < numPlayers)
		{
			hasStartSpotBeenPicked = TRUE;
			taken[posIdx] = TRUE;
		}
	}

	Int teamPosIdx[MAX_SLOTS];
	for (i=0; i<MAX_SLOTS; ++i)
		teamPosIdx[i] = -1;  //team has no starting position yet

	// now pick non-observer spots
	for (i=0; i<MAX_SLOTS; ++i)
	{
		GameSlot *slot = game->getSlot(i);

		if (!slot || !slot->isOccupied() || slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
			continue;  //slot not used

		Int posIdx = slot->getStartPos();
		if (posIdx >= 0  &&  posIdx < numPlayers)
			continue;  //position already assigned
		DEBUG_ASSERTCRASH(posIdx == -1, ("Non-random bad start position %d in slot %d\n", posIdx, i));

		//choose a starting position
		Int team = slot->getTeamNumber();
		if( !hasStartSpotBeenPicked )
		{	// We're the first real spot.  Pick randomly.
			while (posIdx == -1)
			{	// This while loop shouldn't be neccessary, since we're first.  Why not, though?
				posIdx = GameLogicRandomValue(0, numPlayers-1);
				if (game->isStartPositionTaken(posIdx))
					posIdx = -1;
			}
			DEBUG_LOG(("Setting start position %d to %d (random choice)\n", i, posIdx));
			hasStartSpotBeenPicked = TRUE;
			slot->setStartPos(posIdx);
			taken[posIdx] = TRUE;
			if( team > -1 )
				teamPosIdx[team] = posIdx;  //remember where this team is
		} else
		{	//pick teams far apart, team members close together
			if( team < 0  ||  teamPosIdx[ team ] == -1 )  //if team None or team not yet placed
			{	//pick position furthest from all other teams
				Real farthestDistance = 0.0f;
				Int farthestIndex = -1;
				for (posIdx = 0; posIdx < numPlayers; ++posIdx)
				{
					if (taken[posIdx])
						continue;  //skip occupied positions

					if (farthestIndex < 0)
					{	//take this one as best if none else
						farthestIndex = posIdx;
						for (Int n=0; n<numPlayers; ++n)
						{
							if (taken[n] && n != posIdx)
								farthestDistance += startSpotDistance[posIdx][n];
						}
					}
					else
					{	//find empty position furthest from all taken positions
						Real dist = 0.0f;
						for (Int n=0; n<numPlayers; ++n)
						{
							if (taken[n] && n != posIdx)
								dist += startSpotDistance[posIdx][n];
						}
						if (dist > farthestDistance)
						{
							farthestDistance = dist;
							farthestIndex = posIdx;
						}
					}
				} //for posInx

				DEBUG_ASSERTCRASH(farthestIndex >= 0, ("Couldn't find a farthest spot!\n"));
				slot->setStartPos(farthestIndex);
				taken[farthestIndex] = TRUE;
				if( team > -1 )
					teamPosIdx[team] = farthestIndex;  //remember where this team is
			} //pick farthest
			else  //team already has a starting position
			{	//pick position closest to team
				Real closestDist = FLT_MAX;
				Int  closestIdx = 0;
				for( Int n=0;  n < numPlayers;  ++n )
				{
					Real dist = startSpotDistance[ teamPosIdx[team] ][n];
					if( !taken[n]  &&  dist < closestDist )
					{	//found a better match
						closestDist = dist;
						closestIdx = n;
					}
				} //for n
				DEBUG_ASSERTCRASH( closestDist < FLT_MAX, ("Couldn't find a closest starting positon!\n"));
				slot->setStartPos(closestIdx);
				taken[closestIdx] = TRUE;
			}
		}
	} //for i

	// now go back & assign observer spots
	Int numPlayersInGame = 0;
	for (i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = game->getConstSlot(i);
		if (slot->isOccupied() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
			++numPlayersInGame;
	}
	for (i=0; i<MAX_SLOTS; ++i)
	{
		GameSlot *slot = game->getSlot(i);

		if (!slot || !slot->isOccupied())
			continue;

		if (slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
			continue;

		// clean up random start spots
		Int posIdx = -1;
		if (numPlayersInGame == 0)
			posIdx = 0;
		while (posIdx == -1)
		{
			posIdx = GameLogicRandomValue(0, numPlayers-1);
			if (!game->isStartPositionTaken(posIdx))
				posIdx = -1;
		}
		DEBUG_LOG(("Setting observer start position %d to %d\n", i, posIdx));
		slot->setStartPos(posIdx);
	}
}

// ------------------------------------------------------------------------------------------------
/** Update the load screen progress */
// ------------------------------------------------------------------------------------------------
void GameLogic::updateLoadProgress( Int progress )
{
#if defined(_DEBUG)
	// Load phase profile: every progress step logs the time since the previous step; the steps
	// match the LOAD_PROGRESS_* constants at the top of this file (object loop: one step per group).
	{
		static DWORD s_lastTime = 0;
		static DWORD s_startTime = 0;
		const DWORD now = timeGetTime();
		if (progress == LOAD_PROGRESS_START)
		{
			s_startTime = now;
			DEBUG_LOG(("Loading: start (%s)\n", TheGlobalData->m_mapName.str()));
		}
		else if (s_lastTime != 0)
		{
			DEBUG_LOG(("Loading: stage %3d  +%5u ms  (total %5u ms)\n", progress, static_cast<unsigned>(now - s_lastTime), static_cast<unsigned>(now - s_startTime)));
		}
		s_lastTime = now;
	}
#endif
	// map load time for the runner log (all builds)
	if (TheGlobalData->m_logTextureStats > 0)
	{
		static DWORD s_loadStart = 0;
		if (progress == LOAD_PROGRESS_START)
			s_loadStart = timeGetTime();
		else if (progress == LOAD_PROGRESS_END && s_loadStart != 0)
		{
			char line[160];
			WWFormat::Into(line, "LOADTIME {:d} ms map {}", static_cast<unsigned>(timeGetTime() - s_loadStart), TheGlobalData->m_mapName.str());
			GameEngine_RunnerLog(line);
			s_loadStart = 0;
		}
	}
	if( m_loadScreen )
		m_loadScreen->update( progress );

}  // end updateLoadProgress

// ------------------------------------------------------------------------------------------------
/** Entry point for starting a new game, the engine is already in clean state at this
	* point and ready to load up with all the data */
// ------------------------------------------------------------------------------------------------
void GameLogic::startNewGame( Bool loadingSaveGame )
{
	// The engine does not tick regularly while loading (building the map, reading assets), so the
	// watchdog is suspended and armed again at the end.
	::Engine::WatchdogSuspension watchdogSuspension;

#if defined(_DEBUG)
	DEBUG_LOG(("Loading: startNewGame entered at %u ms\n", Engine::Clock::milliseconds()));
#endif

	#ifdef DUMP_PERF_STATS
	__int64 startTime64;
	__int64 endTime64,freq64;
	GetPrecisionTimerTicksPerSec(&freq64);
	GetPrecisionTimer(&startTime64);
	#endif

	setLoadingMap( TRUE );

	if( loadingSaveGame == FALSE )
	{

		// record pristine map name when we're loading from the map (not a save game)
		TheGameState->setPristineMapName( TheGlobalData->m_mapName );

		//
		// sanity, the pristine map should never start with "Save", otherwise that would be
		// a map from the save directory
		//
		if (TheGameState->isInSaveDirectory(TheGlobalData->m_mapName))
		{

			DEBUG_CRASH(( "FATAL SAVE/LOAD ERROR! - Setting a pristine map name that refers to a map in the save directory.  The pristine map should always refer to the ORIGINAL map in the Maps directory, if the pristine map string is corrupt then map.ini files will not load correctly.\n" ));

		}  // end if

		if( m_startNewGame == FALSE )
		{
			/// @todo: Here is where we would look at the game mode & play an intro movie or something.
			// Failing that, we just set the flag so the actual game can start from a uniform
			// entry point (startNewGame() called from update()).
			if( m_gameMode == GAME_SINGLE_PLAYER )
			{

				if(m_background)
				{
					m_background->destroyWindows();
					deleteInstanceOf(m_background);
					m_background = nullptr;
				}
				m_loadScreen.reset( getLoadScreen( loadingSaveGame ) );
				if(m_loadScreen)
				{
					TheWritableGlobalData->m_loadScreenRender = TRUE;	///< mark it so only a few select things are rendered during load
					m_loadScreen->init(nullptr);
				}

			}

			m_startNewGame = TRUE;
			return;

		}  

	}  // end if

	m_rankLevelLimit = 1000;	// this is reset every game.
	setDefaults( loadingSaveGame );
	TheWritableGlobalData->m_loadScreenRender = TRUE;	///< mark it so only a few select things are rendered during load	
	TheWritableGlobalData->m_TiVOFastMode = FALSE;	//always disable the TIVO fast-forward mode at the start of a new game.

	m_showBehindBuildingMarkers = TRUE;
	m_drawIconUI = TRUE;
	m_showDynamicLOD = TRUE;
	m_scriptHulkMaxLifetimeOverride = -1;

	Campaign* currentCampaign = TheCampaignManager->getCurrentCampaign();
	Bool isChallengeCampaign = m_gameMode == GAME_SINGLE_PLAYER && currentCampaign && currentCampaign->m_isChallengeCampaign;

	// Fill in the game color and Factions before we do the Load Screen
	GameInfo *game = nullptr;
	::Engine::context().gameInfo = nullptr;
	Int localSlot = 0;
	if (TheNetwork)
	{
		if (TheLobby)
		{
			DEBUG_LOG(("Starting network game\n"));
			::Engine::context().gameInfo = game = TheLobby->GetMyGame();
		}
		else
		{
			// no internet play
		}
	}
	else
	{
		if (TheRecorder && TheRecorder->getMode() == RecorderModeType::RECORDERMODETYPE_PLAYBACK)
		{
			::Engine::context().gameInfo = game = TheRecorder->getGameInfo();
		}
		else if(m_gameMode == GAME_SKIRMISH)
		{
		  ::Engine::context().gameInfo = game = TheSkirmishGameInfo;
		}
		else if(isChallengeCampaign)
		{
			::Engine::context().gameInfo = game = TheChallengeGameInfo;
		}
	}

  // On a NEW game, we need to copy the superweapon restrictions from the game info to here
  // (because TheGameInfo is not always saved and doesn't carry over to replays). On a save
  // game, we save the superweapon restrictions in GameLogic::xfer()
  if ( !loadingSaveGame )
  {
    if ( TheGameInfo )
    {
      m_superweaponRestriction = TheGameInfo->getSuperweaponRestriction();
      m_graceEndFrame = static_cast<UnsignedInt>( TheGameInfo->getGraceMinutes() ) * 60 * LOGICFRAMES_PER_SECOND;
    }
    else
    {
      // ??? Apparently this is legit? Oh well, use defaults
      m_superweaponRestriction = 0;
      m_graceEndFrame = 0;
    }
  }

	checkForDuplicateColors( game );

	Bool isSkirmishOrSkirmishReplay = FALSE;
	if (game)
	{
		for (Int i=0; i<MAX_SLOTS; ++i)
		{
			GameSlot *slot = game->getSlot(i);
			if (!loadingSaveGame) {
				slot->saveOffOriginalInfo();
			}
			if (slot->isAI())
			{
				isSkirmishOrSkirmishReplay = TRUE;
				continue;
			}
		}
	} else {
		if (m_gameMode == GAME_SINGLE_PLAYER)	{
			if (TheSkirmishGameInfo) {
				delete TheSkirmishGameInfo;
				::Engine::context().skirmishGameInfo = nullptr;
			}
		}
	}

	populateRandomSideAndColor( game );
	populateRandomStartPosition( game );

	// resolved slots for the runner log (recording and playback must match here)
	if (TheGlobalData->m_crcLogFile.isNotEmpty())
	{
		char line[160];
		WWFormat::Into(line, "SLOTS mode {:d} seed {:d} localSlot {:d}", static_cast<Int>(m_gameMode), GetGameLogicRandomSeed(), game ? game->getLocalSlotNum() : -1);
		GameEngine_RunnerLog(line);
		for (Int i = 0; game && i < MAX_SLOTS; ++i)
		{
			GameSlot *slot = game->getSlot(i);
			if (!slot || !slot->isOccupied())
				continue;
			WWFormat::Into(line, "SLOT {:d} state {:d} ai {:d} template {:d} start {:d} color {:d} team {:d} kind {:d}", i, static_cast<Int>(slot->getState()), slot->isAI() ? 1 : 0, slot->getPlayerTemplate(), slot->getStartPos(), slot->getColor(), slot->getTeamNumber(), slot->getAIKind());	// AI kind
			GameEngine_RunnerLog(line);
		}
	}

	//****************************//
	// Start the LoadScreen Now!	//
	//****************************//

	// Get the m_loadScreen for this kind of game
	if(!m_loadScreen)
	{
		m_loadScreen.reset( getLoadScreen( loadingSaveGame ) );
		if(m_loadScreen)
		{
			TheMouse->setVisibility(FALSE);
			m_loadScreen->init(game);

			// 
			updateLoadProgress( LOAD_PROGRESS_START );
		}
	}
	if(m_background)
	{
		m_background->destroyWindows();
		deleteInstanceOf(m_background);
		m_background = nullptr;
	}
	setFPMode();
	if(TheCampaignManager)
		TheCampaignManager->SetVictorious(FALSE);
	m_startNewGame = FALSE;

	// update the loadscreen 
	if(m_loadScreen)
		updateLoadProgress(LOAD_PROGRESS_POST_PARTICLE_INI_LOAD);

	// reset the frame counter
	m_frame = 0;

	// before loading the map, load the map.ini file in the same directory.
	if ( !loadMapINI( TheGlobalData->m_mapName ) )
	{
		abortNewGame();
		return;
	}

	// load a map; a damaged one (also one transferred from the host) ends the start of this game
	// instead of the program
	try
	{
		TheTerrainLogic->loadMap( TheGlobalData->m_mapName, false );
	}
	catch (...)
	{
		DEBUG_LOG(("GameLogic::startNewGame - map %s cannot be read, the game is not started\n", TheGlobalData->m_mapName.str()));
		abortNewGame();
		return;
	}
	// anytime the world's size changes, must reset the partition mgr
	//ThePartitionManager->init();

	// update the loadscreen 
	updateLoadProgress(LOAD_PROGRESS_POST_LOAD_MAP);

	#ifdef DUMP_PERF_STATS
	GetPrecisionTimer(&endTime64);
	char Buf[256];
	WWFormat::Into(Buf, "After terrainlogic->loadmap={:f}\n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)*1000.0));
		//DEBUG_LOG(("Placed a starting building for %s at waypoint %s\n", playerName.str(), waypointName.str()));
	DEBUG_LOG(("%s", Buf));
	#endif

	Int progressCount = LOAD_PROGRESS_SIDE_POPULATION;
	if (game)
	{
		 
		if (TheGameEngine->isMultiplayerSession() || isSkirmishOrSkirmishReplay)
		{
			// Saves off any player, and resets the sides to 0 players so we can add the skirmish players.
			TheSidesList->prepareForMP_or_Skirmish();
		}

		//DEBUG_LOG(("Starting LAN game with %d players\n", game->getNumPlayers()));
		Dict d;
		for (int i=0; i<MAX_SLOTS; ++i)
		{
			// Add a Side to TheSidesList
			GameSlot *slot = game->getSlot(i);

			if (!slot || !slot->isHuman())
			{
				m_progressComplete[i] = TRUE;
				lastHeardFrom(i);
			}

			if (!slot || !slot->isOccupied())
				continue;

			d.clear();
			AsciiString playerName;
			playerName.format("player%d", i);
			d.setAsciiString(TheKey_playerName, playerName);
			d.setBool(TheKey_playerIsHuman, slot->isHuman());
			d.setUnicodeString(TheKey_playerDisplayName, slot->getName());
			const PlayerTemplate* pt;
			if (slot->getPlayerTemplate() >= 0)
				pt = ThePlayerTemplateStore->getNthPlayerTemplate(slot->getPlayerTemplate());
			else
				pt = ThePlayerTemplateStore->findPlayerTemplate( TheNameKeyGenerator->nameToKey("FactionObserver") );
			if (pt)
			{
				d.setAsciiString(TheKey_playerFaction, KEYNAME(pt->getNameKey()));
			}
			
			if (game->isPlayerPreorder(i))
			{
				d.setBool(TheKey_playerIsPreorder, TRUE);
			}
					
			AsciiString enemiesString, alliesString;
			Int team = slot->getTeamNumber();
			DEBUG_LOG(("Looking for allies of player %d, team %d\n", i, team));
			for(int j=0; j < MAX_SLOTS; ++j)
			{
				GameSlot *teamSlot = game->getSlot(j);
				// for check to see if we're trying to add ourselves
				if(i == j || !teamSlot->isOccupied())
					continue;
				
				DEBUG_LOG(("Player %d is team %d\n", j, teamSlot->getTeamNumber()));

				AsciiString teamPlayerName;
				teamPlayerName.format("player%d", j);
				// if our team is None, or our team is not equal to their team, 
				// then their our enemy
				Bool isEnemy = FALSE;
				if(team == -1 || teamSlot->getTeamNumber() != team ) isEnemy = TRUE;
				DEBUG_LOG(("Player %d is %s\n", j, (isEnemy)?"enemy":"ally"));

				if (isEnemy)
				{
					if(!enemiesString.isEmpty())
						enemiesString.concat(" ");
					enemiesString.concat(teamPlayerName);
				}
				else
				{
					// he's on our team, add him!
					if(!alliesString.isEmpty())
						alliesString.concat(" ");
					alliesString.concat(teamPlayerName);
				}				
			}
			d.setAsciiString(TheKey_playerAllies, alliesString);
			d.setAsciiString(TheKey_playerEnemies, enemiesString);
			DEBUG_LOG(("Player %d's teams are: allies=%s, enemies=%s\n", i,alliesString.str(),enemiesString.str()));
/*

			Int colorIdx = slot->getColor();
			if (colorIdx < 0 || colorIdx >= TheMultiplayerSettings->getNumColors())
			{
				DEBUG_ASSERTCRASH(colorIdx == -1, ("Non-random bad color %d in slot %d\n", colorIdx, i));
				while (colorIdx == -1)
				{
					colorIdx = GameLogicRandomValue(0, TheMultiplayerSettings->getNumColors()-1);
					if (game->isColorTaken(colorIdx))
						colorIdx = -1;
				}
				DEBUG_LOG(("Setting color %d to %d\n", i, colorIdx));
				slot->setColor(colorIdx);
			}
			*/

			d.setInt(TheKey_playerColor, TheMultiplayerSettings->getColor(slot->getColor())->getColor());
			d.setInt(TheKey_playerNightColor, TheMultiplayerSettings->getColor(slot->getColor())->getNightColor());
			d.setInt(TheKey_multiplayerStartIndex, slot->getStartPos());
//			d.setBool(TheKey_multiplayerIsLocal, slot->isLocalPlayer());
//			d.setBool(TheKey_multiplayerIsLocal, slot->getIP() == game->getLocalIP());
			d.setBool(TheKey_multiplayerIsLocal, slot->isHuman() && (slot->getName().compare(game->getSlot(game->getLocalSlotNum())->getName().str()) == 0));

/*
			if (slot->getIP() == game->getLocalIP())
			{
				localSlot = i;
				DEBUG_LOG(("GameLogic::StartNewGame - local slot is %d\n", localSlot));
			}
*/

			if (isSkirmishOrSkirmishReplay) 
			{
				d.setBool(TheKey_playerIsSkirmish, true);
				switch (slot->getState()) {
					case SLOT_EASY_AI : d.setInt(TheKey_skirmishDifficulty, DIFFICULTY_EASY); break;
					case SLOT_MED_AI : d.setInt(TheKey_skirmishDifficulty, DIFFICULTY_NORMAL); break;
					case SLOT_BRUTAL_AI : d.setInt(TheKey_skirmishDifficulty, DIFFICULTY_HARD); break;
					default: break;	 // no setting.
				}
				// AI kind in the second byte of the difficulty (Player::initFromDict). No new name key: the
				// classic AI keeps the same value, so its games stay CRC-identical.
				if (slot->isAI() && slot->getAIKind() != AI_KIND_CLASSIC)
					d.setInt(TheKey_skirmishDifficulty, d.getInt(TheKey_skirmishDifficulty) | (slot->getAIKind() << 8)
						| (slot->getAIPersonality() << 16));	// third byte: personality of the new AI
			}

			AsciiString slotNameAscii;
			slotNameAscii.translate(slot->getName());
			if (slot->isHuman() && game->getSlotNum(slotNameAscii) == game->getLocalSlotNum()) {
				localSlot = i;
			}
			TheSidesList->addSide(&d);

			AsciiString playerTeamName;
			playerTeamName.set("team");
			playerTeamName.concat(playerName);

			d.clear();
			d.setAsciiString(TheKey_teamName, playerTeamName);
			d.setAsciiString(TheKey_teamOwner, playerName);
			d.setBool(TheKey_teamIsSingleton, true);
			TheSidesList->addTeam(&d);

			DEBUG_LOG(("Added side %d\n", i));
			updateLoadProgress(progressCount + i);
		}		
	}
	//if(m_gameMode != GAME_REPLAY)
	//{
	
		// Always add in an observer Player
		Dict d;
		d.setAsciiString(TheKey_playerName, "ReplayObserver");
		d.setBool(TheKey_playerIsHuman, TRUE);
		d.setUnicodeString(TheKey_playerDisplayName, UnicodeString(L"Observer"));
		const PlayerTemplate* pt;
		pt = ThePlayerTemplateStore->findPlayerTemplate( TheNameKeyGenerator->nameToKey("FactionObserver") );
		if (pt)
		{
			d.setAsciiString(TheKey_playerFaction, KEYNAME(pt->getNameKey()));
		}
		d.setAsciiString(TheKey_playerAllies, AsciiString::TheEmptyString);
		d.setAsciiString(TheKey_playerEnemies, AsciiString::TheEmptyString);
		d.setInt(TheKey_playerColor, TheMultiplayerSettings->getColor(0)->getColor());
		d.setInt(TheKey_playerNightColor, TheMultiplayerSettings->getColor(0)->getNightColor());
		d.setInt(TheKey_multiplayerStartIndex, 0);
		d.setBool(TheKey_multiplayerIsLocal, FALSE);

		TheSidesList->addSide(&d);	
		d.clear();
		d.setAsciiString(TheKey_teamName, "teamReplayObserver");
		d.setAsciiString(TheKey_teamOwner, "ReplayObserver");
		d.setBool(TheKey_teamIsSingleton, true);
		TheSidesList->addTeam(&d);
	//}
	TheSidesList->validateSides();		

	// update the loadscreen 
	updateLoadProgress(LOAD_PROGRESS_POST_SIDE_LIST_INIT);

	// update the player list to match the new map.
	TheTeamFactory->reset();
	ThePlayerList->newGame();
	
	// update the loadscreen 
	updateLoadProgress(LOAD_PROGRESS_POST_PLAYER_LIST_RESET);

	// Tell the script engine that a newe set of scripts is loaded.
	TheScriptEngine->newMap();

	// update the loadscreen 
	updateLoadProgress(LOAD_PROGRESS_POST_SCRIPT_ENGINE_NEW_MAP);

	if (TheGameEngine->isMultiplayerSession() || isSkirmishOrSkirmishReplay)
	{
		// if there are no other teams (happens for debugging) don't end the game immediately
		Int numTeams = 0; // this can be higher than expected, but is accurate for determining 0, 1, 2+
		Int lastTeam = -1;
		if (game)
		{
			for (int i=0; i<MAX_SLOTS; ++i)
			{
				const GameSlot *slot = game->getConstSlot(i);
				if (slot->isOccupied() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
				{
					if (slot->getTeamNumber() == -1 || slot->getTeamNumber() != lastTeam)
					{
						++numTeams;
						lastTeam = slot->getTeamNumber();
					}
				}
			}
		}

		if (numTeams > 1)
		{
			// add in the multiplayer victory/defeat scripts
			AsciiString path = "Data\\Scripts\\MultiplayerScripts.scb";
			CachedFileInputStream theInputStream;
			if (theInputStream.open(path))
			{ 
				ChunkInputStream *pStrm = &theInputStream;
				DataChunkInput file( pStrm );
				file.registerParser( AsciiString("PlayerScriptsList"), AsciiString::TheEmptyString, ScriptList::ParseScriptsDataChunk );
				if (!file.parse(nullptr)) {
					DEBUG_LOG(("ERROR - Unable to read in multiplayer scripts.\n"));
					return;
				}
				ScriptList *scripts[MAX_PLAYER_COUNT];
				Int count = ScriptList::getReadScripts(scripts);
				if (count)
				{
					ScriptList *pSL = TheSidesList->getSideInfo(0)->getScriptList();
					Script *next = scripts[0]->getScript();
					while (next)
					{
						Script *dupe = next->duplicate();
						pSL->addScript(dupe, 0);
						next = next->getNext();
					}
				}
				for (Int i=0; i<count; ++i)
				{
					deleteInstanceOf(scripts[i]);
				}
			}
		}

		/*
		// add multiplayer single-player defeat script
		if (numTeams > 1)
		{
			AsciiString name;
			name.format("MultiplayerPlayerDefeat"); // name is for the ui in the editor
			Script *pNewScript = newInstance(Script);
			pNewScript->setName(name);

			Condition *pFalse1 = newInstance(Condition)(Condition::MULTIPLAYER_PLAYER_DEFEAT);
			OrCondition *pOr = newInstance(OrCondition);
			pOr->setFirstAndCondition(pFalse1);
			pNewScript->setOrCondition(pOr);

			ScriptAction *action = newInstance(ScriptAction)(ScriptAction::LOCALDEFEAT);
			pNewScript->setAction(action);

			ScriptList *pSL = TheSidesList->getSideInfo(0)->getScriptList();
			pSL->addScript(pNewScript, 0); // adds this as the player's first script (index 0)
		}

		// add multiplayer victory scripts
		if (numTeams > 1)
		{
			AsciiString name;
			name.format("MultiplayerVictory"); // name is for the ui in the editor
			Script *pNewScript = newInstance( Script );
			pNewScript->setName(name);

			Condition *pFalse1 = newInstance(Condition)(Condition::MULTIPLAYER_ALLIED_VICTORY);
			OrCondition *pOr = newInstance(OrCondition);
			pOr->setFirstAndCondition(pFalse1);
			pNewScript->setOrCondition(pOr);

			ScriptAction *action = newInstance(ScriptAction)(ScriptAction::VICTORY);
			pNewScript->setAction(action);

			ScriptList *pSL = TheSidesList->getSideInfo(0)->getScriptList();
			pSL->addScript(pNewScript, 0); // adds this as the player's first script (index 0)
		}

		// add multiplayer defeat scripts
		if (numTeams > 1)
		{
			AsciiString name;
			name.format("MultiplayerDefeat"); // name is for the ui in the editor
			Script *pNewScript = newInstance(Script);
			pNewScript->setName(name);

			Condition *pFalse1 = newInstance(Condition)(Condition::MULTIPLAYER_ALLIED_DEFEAT);
			OrCondition *pOr = newInstance(OrCondition);
			pOr->setFirstAndCondition(pFalse1);
			pNewScript->setOrCondition(pOr);

			ScriptAction *action = newInstance(ScriptAction)(ScriptAction::DEFEAT);
			pNewScript->setAction(action);

			ScriptList *pSL = TheSidesList->getSideInfo(0)->getScriptList();
			pSL->addScript(pNewScript, 0); // adds this as the player's first script (index 0)
		}
		*/
	}

	// update the loadscreen 
	updateLoadProgress(LOAD_PROGRESS_POST_VICTORY_CONDITION_SETUP);

	// set the radar as on a new map
	TheRadar->newMap( TheTerrainLogic );
	TheInGameUI->setClientQuiet( FALSE ); // okay to start beeping and stuff

	// Tell the multiplayer victory condition singleton that the players are created
	TheVictoryConditions->cachePlayerPtrs();
	TheVictoryConditions->setVictoryConditions(VICTORY_NOBUILDINGS);

	// update the loadscreen 
	updateLoadProgress(LOAD_PROGRESS_POST_VICTORY_CONDITION_SET_VICTORY_CONDITION);

	// set the world extents to that of the map
	Region3D extent;
	TheTerrainLogic->getExtent( &extent );

	TheGameLogic->setWidth( extent.hi.x - extent.lo.x );
	TheGameLogic->setHeight( extent.hi.y - extent.lo.y );

	// anytime the world's size changes, must reset the partition mgr
	ThePartitionManager->init();
	ThePartitionManager->refreshShroudForLocalPlayer();// Can't do this until after init, and doesn't seem right to do in init

	TheGhostObjectManager->setLocalPlayerIndex(ThePlayerList->getLocalPlayer()->getPlayerIndex());
	TheGhostObjectManager->reset();
	
	// update the loadscreen 
	updateLoadProgress(LOAD_PROGRESS_POST_GHOST_OBJECT_MANAGER_RESET);

	// update the terrain logic now that all is loaded
	TheTerrainLogic->newMap( loadingSaveGame );

	// update the loadscreen 
	updateLoadProgress(LOAD_PROGRESS_POST_TERRAIN_LOGIC_NEW_MAP);

	#ifdef DUMP_PERF_STATS
	GetPrecisionTimer(&endTime64);
	WWFormat::Into(Buf, "After terrainlogic->newmap={:f}\n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)*1000.0));
	DEBUG_LOG(("%s", Buf));
	#endif

		// Special case, load any bridge map objects.
 	const ThingTemplate *thingTemplate;
	MapObject *pMapObj;
	for (pMapObj = MapObject::getFirstMapObject(); pMapObj; pMapObj = pMapObj->getNext()) 
	{

		if (pMapObj->getFlag(FLAG_BRIDGE_FLAGS) || pMapObj->getFlag(FLAG_ROAD_FLAGS)) 
			continue;	// these roads & bridges are special cased in the terrain side.

		// get thing template based from map object name
		thingTemplate = pMapObj->getThingTemplate();
		if( thingTemplate == nullptr )
			continue;	

		Bool isBridgeLikeObject = false;

		if (thingTemplate->isBridge()) 
			isBridgeLikeObject = true;
		if (thingTemplate->isKindOf(KINDOF_WALK_ON_TOP_OF_WALL)) 
			isBridgeLikeObject = true;

		if (!isBridgeLikeObject)
			continue;

		Team *team = ThePlayerList->getNeutralPlayer()->getDefaultTeam();
		// create new object in the world
		Object *obj = TheThingFactory->newObject( thingTemplate, team ); //, OBJECT_STATUS_LOADING_FROM_MAP );
		if( obj )
		{
			Coord3D pos = *pMapObj->getLocation();
			pos.z += TheTerrainLogic->getGroundHeight( pos.x, pos.y );

			Real angle = normalizeAngle(pMapObj->getAngle());  
			obj->setOrientation(angle);
			obj->setPosition( &pos );
			if (thingTemplate->isBridge()) {
				TheTerrainLogic->addLandmarkBridgeToLogic(obj);
			}
			if (thingTemplate->isKindOf(KINDOF_WALK_ON_TOP_OF_WALL)) {
				TheAI->pathfinder()->addWallPiece(obj);
			}

			// Do this after positioning the object, because we may place objects in response to keys
			// in the map object properties.
			// update this object instance with properties from the map object
			obj->updateObjValuesFromMapProperties( pMapObj->getProperties() );
		}  // end if

	}	// for, loading bridge map objects

	// update the loadscreen 
	updateLoadProgress(LOAD_PROGRESS_POST_BRIDGE_LOAD);

	// refresh the radar to reflect loaded bridges
	TheRadar->refreshTerrain( TheTerrainLogic );

	// tell the AI about it
	// Note that it is important that the pathfinder be called before the map objects are loaded.
	TheAI->pathfinder()->newMap( );

	// update the loadscreen 
	updateLoadProgress(LOAD_PROGRESS_POST_PATHFINDER_NEW_MAP);

	// reveal the map for the permanent observer
	ThePartitionManager->revealMapForPlayerPermanently( ThePlayerList->findPlayerWithNameKey(TheNameKeyGenerator->nameToKey("ReplayObserver"))->getPlayerIndex() );
	DEBUG_LOG(("Reveal shroud for %ls whose index is %d\n", ThePlayerList->findPlayerWithNameKey(TheNameKeyGenerator->nameToKey("ReplayObserver"))->getPlayerDisplayName().str(),ThePlayerList->findPlayerWithNameKey(TheNameKeyGenerator->nameToKey("ReplayObserver"))->getPlayerIndex()));
	
	if (game)
	{
		for (int i=0; i<MAX_SLOTS; ++i)
		{
			GameSlot *slot = game->getSlot(i);
			
			if (!slot || !slot->isOccupied())
				continue;
			
			AsciiString playerName;
			playerName.format("player%d", i);
			Player *player = ThePlayerList->findPlayerWithNameKey(TheNameKeyGenerator->nameToKey(playerName));
			
			if (slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
			{
				DEBUG_LOG(("Clearing shroud for observer %s in playerList slot %d\n", playerName.str(), player->getPlayerIndex()));
				ThePartitionManager->revealMapForPlayerPermanently( player->getPlayerIndex() );
			}
			else
			{
				// remove shroud for the player in MP games
				if (!TheMultiplayerSettings->isShroudInMultiplayer())
					ThePartitionManager->revealMapForPlayer( player->getPlayerIndex() );
			}
		}
	}

	#ifdef DUMP_PERF_STATS
	GetPrecisionTimer(&endTime64);
	WWFormat::Into(Buf, "Before loading objects={:f}\n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)*1000.0));
	DEBUG_LOG(("%s", Buf));
	#endif

	Bool useTrees = TheGlobalData->m_useTrees;

	// If forceFluffToProp == true, removable objects get created on client only. [7/14/2003]
	// If static lod is HIGH, we don't do force fluff to client side only (create logic side props, more expensive. jba)
	// VeryHigh counts as High here; otherwise the highest level would have less detail than High
	// (fluff on the client only).
	Bool forceFluffToProp = TheGameLODManager->getStaticLODLevel() != STATIC_GAME_LOD_HIGH
			&& TheGameLODManager->getStaticLODLevel() != STATIC_GAME_LOD_VERY_HIGH;
	if (TheGameLODManager->getStaticLODLevel() == STATIC_GAME_LOD_CUSTOM && 
			TheGlobalData->m_useShadowVolumes) {
		// Custom LOD, and volumetric shadows turned on - very high detail.  So use logic props too. jba. [7/14/2003]
		forceFluffToProp = false;
	}

	if (TheRecorder && TheRecorder->isMultiplayer()) {
		useTrees = TRUE; // Always use trees in multiplayer, cause we want it to sync properly. jba.
		forceFluffToProp = TRUE; // Always do client side fluff - faster, and syncs properly. jba.
	}

	// Generals creates the decoration objects (CLEARED_BY_BUILD without fence width) as logic objects
	// regardless of detail level and game mode (selectable, destructible, in the CRC). On by default
	// only in Zero Hour (GlobalData, GameData.ini "MapFluffAsClientProps").
	if( !TheGlobalData->m_mapFluffAsClientProps )
		forceFluffToProp = FALSE;

	progressCount = LOAD_PROGRESS_LOOP_ALL_THE_FREAKN_OBJECTS;
	Int timer = Engine::Clock::milliseconds();
	if( loadingSaveGame ) {
		// Loading a loadingSaveGame, need to add the trees to the client. jba. [8/11/2003]
		for (pMapObj = MapObject::getFirstMapObject(); pMapObj; pMapObj = pMapObj->getNext()) 
		{
			// get thing template based from map object name
			thingTemplate = pMapObj->getThingTemplate();
			if( thingTemplate == nullptr )
				continue;
			// don't create trees and shrubs if this is one and we have that option off
			if( thingTemplate->isKindOf( KINDOF_SHRUBBERY ) && !useTrees )
				continue;

			Coord3D pos = *pMapObj->getLocation();
			pos.z += TheTerrainLogic->getGroundHeight( pos.x, pos.y );
			Real angle = normalizeAngle(pMapObj->getAngle());
			if (thingTemplate->isKindOf(KINDOF_OPTIMIZED_TREE)) {
				// Opt trees and props just get drawables to tell the client about it, then deleted. jba [6/5/2003]
				// This way there is no logic object to slow down partition manager and core logic stuff.				
				Drawable *draw = TheThingFactory->newDrawable(thingTemplate, DRAWABLE_STATUS_NONE);
				if (draw) {
					draw->setOrientation(angle);
					draw->setPosition( &pos );
					TheGameClient->destroyDrawable(draw);
				}
			}
		}
	} 
	else 
	{

		for (pMapObj = MapObject::getFirstMapObject(); pMapObj; pMapObj = pMapObj->getNext()) 
		{
		
			if (pMapObj->getFlag(FLAG_BRIDGE_FLAGS) || pMapObj->getFlag(FLAG_ROAD_FLAGS)) {
				continue;	// roads & bridges are special cased in the terrain side.
			}
			//Kris: Added this function so that we can rename objects and preserve them. If the patch
			//      entry is missing for a particular unit, it doesn't get added to the world.
			handleNameChange( pMapObj );

			// get thing template based from map object name
			thingTemplate = pMapObj->getThingTemplate();

			//
			// if no template continue, some map objects don't have thing templates like
			// lights (handled in the device).  Objects that are test objects have a 
			// test string (designated with *** and the define TEST_STRING) and will 
			// have temporary templates created 'on the fly' during a 
			// ThingFactory->findTemplate() call when loading from the map file
			//
			if( thingTemplate == nullptr )
				continue;

			Bool isBridgeLikeObject = false;
			if (thingTemplate->isBridge()) isBridgeLikeObject = true;
			if (thingTemplate->isKindOf(KINDOF_WALK_ON_TOP_OF_WALL)) isBridgeLikeObject = true;
			if (isBridgeLikeObject)
				continue;	// bridges have to be added earlier.

			// don't create trees and shrubs if this is one and we have that option off
			if( thingTemplate->isKindOf( KINDOF_SHRUBBERY ) && !useTrees )
				continue;

			Coord3D pos = *pMapObj->getLocation();
			pos.z += TheTerrainLogic->getGroundHeight( pos.x, pos.y );
			Real angle = normalizeAngle(pMapObj->getAngle());
			if (thingTemplate->isKindOf(KINDOF_OPTIMIZED_TREE)) {
				// Opt trees and props just get drawables to tell the client about it, then deleted. jba [6/5/2003]
				// This way there is no logic object to slow down partition manager and core logic stuff.
				
				Drawable *draw = TheThingFactory->newDrawable(thingTemplate, DRAWABLE_STATUS_NONE);
				if (draw) {
					draw->setOrientation(angle);
					draw->setPosition( &pos );

					TheGameClient->destroyDrawable(draw);
				}
				continue;
			}
#if 1
			Bool isProp = thingTemplate->isKindOf(KINDOF_PROP);
			Bool isFluff = false;
			if (thingTemplate->isKindOf(KINDOF_CLEARED_BY_BUILD)) {
				if (thingTemplate->getFenceWidth() == 0.0f) {
					// Objects that are cleared by building, but aren't fences, are fluff. jba. [7/14/2003]
					isFluff = true;
				}
			}
			if (isProp || (isFluff && forceFluffToProp)) {
				TheTerrainVisual->addProp(thingTemplate, &pos, angle);
				continue;
			}
#endif
			
			// Get the team information
			DEBUG_ASSERTCRASH(pMapObj->getProperties()->getType(TheKey_originalOwner) == Dict::DICT_ASCIISTRING, ("unit %s has no original owner specified (obsolete map file)\n",pMapObj->getName().str()));
			AsciiString originalOwner = pMapObj->getProperties()->getAsciiString(TheKey_originalOwner);
			Team *team = ThePlayerList->validateTeam(originalOwner);

			// create new object in the world
			Object *obj = TheThingFactory->newObject( thingTemplate, team ); //, OBJECT_STATUS_LOADING_FROM_MAP );
			if( obj )
			{
				if(pMapObj->getFlag(FLAG_DRAWS_IN_MIRROR) || obj->isKindOf(KINDOF_CAN_CAST_REFLECTIONS)) 
				{
					Drawable* draw = obj->getDrawable();
					if(draw)
						draw->setDrawableStatus( DRAWABLE_STATUS_DRAWS_IN_MIRROR );
				}
				obj->setOrientation(angle);
				obj->setPosition( &pos );

				// Do this after positioning the object, because we may place objects in response to keys
				// in the map object properties.
				// update this object instance with properties from the map object
				obj->updateObjValuesFromMapProperties( pMapObj->getProperties() );

				PathfindLayerEnum	layer = TheTerrainLogic->getLayerForDestination(&pos);
				obj->setLayer(layer);
				// Now onCreates were called at the constructor.  This magically created
				// thing needs to be considered as Built for Game specific stuff.
				for (BehaviorModule** m = obj->getBehaviorModules(); *m; ++m)
				{
					CreateModuleInterface* create = (*m)->getCreate();
					if (!create)
						continue;
					create->onBuildComplete();
				}

				// Since the team now has members, activate it.
				team->setActive();
				TheAI->pathfinder()->addObjectToPathfindMap( obj );

			}  // end if
		
			if(Engine::Clock::milliseconds() > static_cast<UnsignedInt>(timer + 500))
			{
				if(progressCount < LOAD_PROGRESS_MAX_ALL_THE_FREAKN_OBJECTS)
					progressCount ++;
				updateLoadProgress(progressCount);
				timer = Engine::Clock::milliseconds();
			}

		}	// for, loading map objects

	}  // end if, not loading save game

	#ifdef DUMP_PERF_STATS
	GetPrecisionTimer(&endTime64);
	WWFormat::Into(Buf, "After loading objects={:f}\n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)*1000.0));
	DEBUG_LOG(("%s", Buf));
	#endif

	progressCount = LOAD_PROGRESS_LOOP_INITIAL_NETWORK_BUILDINGS;
	// place initial network buildings/units
	if (game && !loadingSaveGame)
	{
		for (int i=0; i<MAX_SLOTS; ++i)
		{
			GameSlot *slot = game->getSlot(i);
			
			if (!slot || !slot->isOccupied())
				continue;
			
			AsciiString playerName;
			playerName.format("player%d", i);
			Player *player = ThePlayerList->findPlayerWithNameKey(TheNameKeyGenerator->nameToKey(playerName));
			
			if (slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
			{
				slot->setPlayerTemplate(0);
				const PlayerTemplate *pt3 = ThePlayerTemplateStore->findPlayerTemplate( TheNameKeyGenerator->nameToKey("FactionObserver") );
				if (pt3)
				{
					for (Int j=0; j<ThePlayerTemplateStore->getPlayerTemplateCount(); ++j)
					{
						if (pt3 == ThePlayerTemplateStore->getNthPlayerTemplate(j))
						{
							slot->setPlayerTemplate(j);
							break;
						}
					}
				}
				DEBUG_LOG(("Setting observer's playerTemplate to %d in slot %d\n", slot->getPlayerTemplate(), i));
			}
			else
			{
				const PlayerTemplate* pt2 = nullptr;
				pt2 = ThePlayerTemplateStore->getNthPlayerTemplate(slot->getPlayerTemplate());

				// Prevent from loading the disabled Generals, in case your game peer hacked their GUI.
				// The game will start, but the cheater will be instantly defeated because he has no troops.
				// This is also enforced at GUI setup (GUIUtil.cpp and UserSettings.cpp).
				// @todo: unlock these when something rad happens
				Bool disallowLockedGenerals = TRUE;
				const GeneralPersona *general = TheChallengeGenerals->getGeneralByTemplateName(pt2->getName());
				Bool startsLocked = general ? !general->isStartingEnabled() : FALSE;
				if (disallowLockedGenerals && startsLocked)
					continue;

				// prevent from loading disallowed templates, in case your peer hacked their GUI.
        

        // So that the global flag for restricting factions to "OLD" is applied only in the appropriate context!
        if ( isInInternetGame() )
        {
				  if ( game->oldFactionsOnly() && !pt2->isOldFaction() )
				    continue;
        }




				placeNetworkBuildingsForPlayer(i, slot, player, pt2);
			}

			updateLoadProgress(progressCount++);

		}
	}
	// update the loadscreen 
	updateLoadProgress(LOAD_PROGRESS_POST_INITIAL_NETWORK_BUILDINGS);

	//
	// tell the client to pre-load some assets that we will use such as faction things we
	// will build and various damage states for all the structures on the map so that we
	// don't have big pauses when building those objects or switching to those states
	//
	if( TheGlobalData->m_preloadAssets )
	{
		if (TheGlobalData->m_preloadEverything)
		{
			for (Int td = TIME_OF_DAY_FIRST; td < TIME_OF_DAY_COUNT; ++td)
				TheGameClient->preloadAssets(static_cast<TimeOfDay>(td));
		}
		else
		{
			TheGameClient->preloadAssets( TheGlobalData->m_timeOfDay );
		}
	}

	//put this here somewhat randomly.
	TheControlBar->hideCommunicator( FALSE );

	// update the loadscreen 
	updateLoadProgress(LOAD_PROGRESS_POST_PRELOAD_ASSETS);

	TheTacticalView->setAngleAndPitchToDefault();
	TheTacticalView->setZoomToDefault();

	if( TheRecorder )
		TheRecorder->initControls();

	// Note - WorldBuilderDoc.cpp also uses initial camera position, so if changed, update both.  jba
	// Note - We construct the multiplayer start spot name manually here, so change this if you
	//        change TheKey_Player_1_Start etc.  mdc
	AsciiString startingCamName = TheNameKeyGenerator->keyToName(TheKey_InitialCameraPosition);
	if (game)
	{
		GameSlot *slot = game->getSlot(localSlot);
		DEBUG_ASSERTCRASH(slot, ("Starting a LAN game without ourselves!"));
		
		if (slot->isHuman())
		{
			Int startPos = slot->getStartPos();
			startingCamName.format("Player_%d_Start", startPos+1); // start pos waypoints are 1-based
			DEBUG_LOG(("Using %s as the multiplayer initial camera position\n", startingCamName.str()));
		}
	}

	// update the loadscreen 
	updateLoadProgress(LOAD_PROGRESS_POST_STARTING_CAMERA);

	Waypoint *way = findNamedWaypoint(startingCamName);
	if (way)
	{
		Coord3D pos = *way->getLocation();
		TheTacticalView->lookAt( &pos );
	}
	else
	{
		// Just look somewhere.  lookAt does some terrain specific setup, so it is good
		// to call it.  jba
		Coord3D pos;
		pos.x = 50;
		pos.y = 50;
		pos.z = 0;
		TheTacticalView->lookAt( &pos );
		DEBUG_LOG(("Failed to find initial camera position waypoint %s\n", startingCamName.str()));
	}

	// Set up the camera height based on the map height & globalData.
	TheTacticalView->initHeightForMap();
	TheTacticalView->setAngleAndPitchToDefault();
	TheTacticalView->setZoomToDefault();

	// update the loadscreen 
	updateLoadProgress(LOAD_PROGRESS_POST_STARTING_CAMERA_2);

	// update partition info - We need to do the initial update so that it can be queried
	// during the first frame.  jba.
	ThePartitionManager->update();

	#ifdef DUMP_PERF_STATS
	GetPrecisionTimer(&endTime64);
	WWFormat::Into(Buf, "After partition manager update={:f}\n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)*1000.0));
	DEBUG_LOG(("%s", Buf));
	#endif


	// final step, run newMap for all players
	if( loadingSaveGame == FALSE )
		ThePlayerList->newMap();

	if ( isChallengeCampaign )
	{
		// Establish local player relationships with other teams as
		// they're set up for "ThePlayer" in challenge mode map.
		// Some designers have used "ThePlayer" as an empty player for Generals' 
		// Challenge maps which they reference in script and by which they set 
		// player relationships.  The skirmish local player will receive these
		// relationships.  If there is no "ThePlayer", we have to assume that
		// all players on the map are the local player's enemy, except neutral
		// and civilian players.
		Player *localPlayer = ThePlayerList->getLocalPlayer();
		DEBUG_ASSERTCRASH(localPlayer, ("Local player has not been established for Challenge map."));
		Player *placeholderThePlayer = ThePlayerList->findPlayerWithNameKey(NAMEKEY("ThePlayer"));
		DEBUG_ASSERTCRASH(placeholderThePlayer, ("Challenge maps without player \"ThePlayer\" assume that the local player is mutual enemies with all other players except the neutral and civilian players."));

		if (placeholderThePlayer)
		{
			PlayerMaskType thePlayerEnemysMask = ThePlayerList->getPlayersWithRelationship(placeholderThePlayer->getPlayerIndex(), ALLOW_ENEMIES);
			while( thePlayerEnemysMask )
			{
				// set local player enemy relationships based on "ThePlayer" enemy relationships
				Player *thePlayerEnemy = ThePlayerList->getEachPlayerFromMask(thePlayerEnemysMask);
				thePlayerEnemy->setPlayerRelationship(localPlayer, ENEMIES);
				localPlayer->setPlayerRelationship(thePlayerEnemy, ENEMIES);
			}
		} 
		else 
		{
			// This map has no "ThePlayer" by which to model the local player's 
			// relationships, so make assumptions.
			for (Int i = 0; i < ThePlayerList->getPlayerCount(); i++)
			{
				Relationship rel = NEUTRAL;
				Player *thatPlayer = ThePlayerList->getNthPlayer(i);
				if (thatPlayer == localPlayer)
				{
					rel = ALLIES;
				} 
				else if (thatPlayer != ThePlayerList->getNeutralPlayer()
					&& thatPlayer != ThePlayerList->findPlayerWithNameKey(NAMEKEY("PlyrCivilian")))
				{
					rel = ENEMIES;
				} 
				thatPlayer->setPlayerRelationship(localPlayer, rel);
				localPlayer->setPlayerRelationship(thatPlayer, rel);
			}
		}
	}


	// reset all the skill points in a single player game
	if(loadingSaveGame == FALSE && isInSinglePlayerGame())
	{
		for (Int i=0; i<MAX_PLAYER_COUNT; ++i)
		{
			Player *pPlayer = ThePlayerList->getNthPlayer(i);
			if (pPlayer && pPlayer->getPlayerType() != PLAYER_HUMAN)
				pPlayer = nullptr;

			if (pPlayer)
			{
				pPlayer->addSkillPoints(m_rankPointsToAddAtGameStart);
				DEBUG_LOG(("GameLogic::startNewGame() - adding m_rankPointsToAddAtGameStart==%d to player %d(%ls)\n",
					m_rankPointsToAddAtGameStart, i, pPlayer->getPlayerDisplayName().str()));
			}
		}
	}

	updateLoadProgress(LOAD_PROGRESS_END);

	if(isInMultiplayerGame() && TheNetwork)
	{
		TheNetwork->loadProgressComplete();
		TheNetwork->liteupdate();
	}

#if defined(_DEBUG)
	DEBUG_LOG(("Loading: after stage 100, before wait/fade at %u ms\n", Engine::Clock::milliseconds()));
#endif
	while(!isProgressComplete())
	{
		updateLoadProgress(101); // keep greater then 100
		testTimeOut();
		Sleep(100);
	}

	// if we're in a load game, don't fade yet
	if( loadingSaveGame == FALSE )
	{
		TheTransitionHandler->setGroup("FadeWholeScreen");
		while(!TheTransitionHandler->isFinished())
		{
			TheWindowManager->update();
			if(!TheTransitionHandler->isFinished())
			{
				TheDisplay->draw();
				setFPMode();
				Sleep(33);
			}
			
		}
	}

#if defined(_DEBUG)
	DEBUG_LOG(("Loading: after fade at %u ms\n", Engine::Clock::milliseconds()));
#endif
	if(m_loadScreen)
	{
		TheMouse->setVisibility(TRUE);

/*
		//
		// delete load screen only when not loading a save game, for save games we still
		// have more work to do and the load screen will be deleted elsewhere after
		// we're all done with the load game progress
		//
		if( loadingSaveGame == FALSE )
*/
			deleteLoadScreen();
#if defined(_DEBUG)
			DEBUG_LOG(("Loading: after deleteLoadScreen at %u ms\n", Engine::Clock::milliseconds()));
#endif

	}
	
	#ifdef DUMP_PERF_STATS
	GetPrecisionTimer(&endTime64);
	WWFormat::Into(Buf, "After delete load screen={:f}\n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)*1000.0));
	DEBUG_LOG(("%s", Buf));
	#endif

	if(m_gameMode == GAME_SHELL)
	{
		if(TheShell->getScreenCount() == 0)
			TheShell->push( AsciiString("Menus/MainMenu.wnd") );
		else if (TheShell->top())
		{
			TheShell->top()->hide(FALSE);
			TheShell->top()->bringForward();
		}
		HideControlBar();
	}
	else
	{
		
//		TheShell->hideShell();
		if(TheStatsCollector)
		{
			TheStatsCollector->reset();
		}
		else if( TheGlobalData->m_playStats > 0)
		{
			::Engine::context().statsCollector = adoptSubsystem(m_own.statsCollector, NEW StatsCollector);
			TheStatsCollector->reset();
		}

///		ShowControlBar(FALSE);

		// explicitly set the Control bar to Observer Mode
		if(m_gameMode == GAME_REPLAY )
		{
			
			ThePlayerList->setLocalPlayer(ThePlayerList->findPlayerWithNameKey(TheNameKeyGenerator->nameToKey("ReplayObserver")));
			TheRadar->forceOn(TRUE);
			ThePartitionManager->refreshShroudForLocalPlayer();
			TheControlBar->setControlBarSchemeByPlayer( ThePlayerList->getLocalPlayer());
			DEBUG_LOG(("Start of a replay game %ls, %d\n",ThePlayerList->getLocalPlayer()->getPlayerDisplayName().str(), ThePlayerList->getLocalPlayer()->getPlayerIndex()));
		}
		else
			TheControlBar->setControlBarSchemeByPlayer(ThePlayerList->getLocalPlayer());
//		ShowControlBar();
		
	}
	TheTacticalView->setOkToAdjustHeight(TRUE);



	// @todo remove this hack
//	TheGlobalData->m_inGame = TRUE;

	// If we are now starting a multiplayer or skirmish game, let us set the local players selectionto be the command center
	// We'll ask the Recorder, so we survive replays
	if( TheRecorder->isMultiplayer() )
	{
		// Iterate through each player's objects, and ask if the object 
		//is a command center, and if so, select it for that player
		for (Int i = 0; i < MAX_PLAYER_COUNT; ++i) 
		{
			Player *player = ThePlayerList->getNthPlayer(i);
			if (player && player->isPlayerActive())
			{
				// we need to iterate their objects, and select the first Command Center we find
				Bool alreadyFound = FALSE;
				player->iterateObjects(findAndSelectCommandCenter, &alreadyFound);
			}
		}
	}
	TheControlBar->initSpecialPowershortcutBar(ThePlayerList->getLocalPlayer());

	if(m_gameMode == GAME_SHELL)
	{
		HideControlBar();
	}
	else
	{
		ShowControlBar(FALSE);
	}

	TheWritableGlobalData->m_loadScreenRender = FALSE;	///< mark to resume rendering as normal
	
  if( loadingSaveGame == FALSE )
  {
    // Drawables need to do some work on level start; give them a chance to do it
    Drawable * drawable = TheGameClient->getDrawableList();
  
    while ( drawable != nullptr )
    {
      drawable->onLevelStart();
      drawable = drawable->getNextDrawable();
    }
  }
  
	//Added By Sadullah Nader
	//Added to fix the quit menu 
	//ReAllows quit menu to work during loading scene
	//setGameLoading(FALSE);
	setLoadingMap( FALSE );

#ifdef DUMP_PERF_STATS
	GetPrecisionTimer(&endTime64);
	WWFormat::Into(Buf, "Total startnewgame={:f}\n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)*1000.0));
	DEBUG_LOG(("%s", Buf));
#endif

  
  if ( isInReplayGame() && TheInGameUI && TheGameText )
  {
		// The Generals texts lack "GUI:FastForwardInstructions"; without the entry show no message
		// instead of "MISSING".
		Bool exists = FALSE;
		UnicodeString instructions = TheGameText->fetch( "GUI:FastForwardInstructions", &exists );
		if( exists )
			TheLogicToClient->uiMessage( instructions );
  }


}  // end startNewGame

//-----------------------------------------------------------------------------------------
static void findAndSelectCommandCenter(Object *obj, void* alreadyFound)
{
	if (!((*static_cast<Bool*>(alreadyFound))) && obj && obj->isKindOf(KINDOF_COMMANDCENTER) )
	{
		((*static_cast<Bool*>(alreadyFound))) = TRUE;
		TheGameLogic->selectObject(obj, TRUE, obj->getControllingPlayer()->getPlayerMask(), obj->isLocallyControlled());

	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool GameLogic::loadMapINI( AsciiString mapName )
{

	if (!TheMapCache) {
		// Need the map cache to get the map and user map directories.
		return TRUE;
	}

	char filename[_MAX_PATH];
	char fullFledgeFilename[_MAX_PATH];

	memset(filename, 0, _MAX_PATH);
	safe_strcpy(filename, mapName.str());

	//
	// if map name begins with a "SAVE_DIRECTORY\", then the map refers to a map
	// that has been extracted from a save game file ... in that case we need to get
	// the pristine map name string in order to manipulate and load the right map.ini
	// for that map from it's original location
	//
	if (TheGameState->isInSaveDirectory(filename))
		safe_strcpy( filename, TheGameState->getSaveGameInfo()->pristineMapName.str() );

	// sanity
	int length = static_cast<int>(strlen(filename));
	if (length < 4) { 
		return TRUE;
	}

	// back up over the ".map" extension and to the first directory separator
	char *extension = filename + length - 4;
	while ((extension > filename) && (*extension != '\\') && (*extension != '/')) {
		--extension;
	}
	*extension = 0;


	// module data from the map INI belongs to this map only (ModuleFactory::reset)
	if (TheModuleFactory)
		TheModuleFactory->beginMapModuleData();

	// Maps come from other players too: a damaged map.ini ends the start of this game instead of the
	// program. What was read up to the error is dropped with the game data (clearGameData).
	static const char *const mapInis[] = { "map.ini", "solo.ini" };
	for (const char *iniName : mapInis)
	{
		WWFormat::Into(fullFledgeFilename, "{}\\{}", filename, iniName);
		if (!TheFileSystem->doesFileExist(fullFledgeFilename))
			continue;
		DEBUG_LOG(("Loading %s\n", iniName));
		try
		{
			INI ini;
			ini.load( AsciiString(fullFledgeFilename), INILoadType::INI_LOAD_CREATE_OVERRIDES, nullptr );
		}
		catch (...)
		{
			DEBUG_LOG(("GameLogic::loadMapINI - %s cannot be read, the game is not started\n", fullFledgeFilename));
			return FALSE;
		}
	}
	
	// No error here. There could've just *not* been a map.ini file.

	// Resolve projectile and OCL names from weapon overrides only after the whole file: a projectile
	// may come further down in the same map.ini.
	if (TheWeaponStore)
		TheWeaponStore->postProcessLoad();

	// now look for a string file
	WWFormat::Into(fullFledgeFilename, "{}\\map.str", filename);

	if (TheFileSystem->doesFileExist(fullFledgeFilename)) {
		TheGameText->initMapStringFile(fullFledgeFilename);
	}

	// we want to do this before doing the actual map load!
	if (TheDisplay)
	{
		const char* ASSET_USAGE_FILE_NAME = "AssetUsage.txt";
		WWFormat::Into(fullFledgeFilename, "{}\\{}", filename, ASSET_USAGE_FILE_NAME);
		// note: call this EVEN IF THE FILE IN QUESTION DOES NOT EXIST.
		TheDisplay->doSmartAssetPurgeAndPreload(fullFledgeFilename);
	}

	return TRUE;
}
