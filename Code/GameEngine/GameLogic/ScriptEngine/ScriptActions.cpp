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

// FILE: ScriptActions.cpp /////////////////////////////////////////////////////////////////////////
// The game scripting engine.  Interprets scripts.
// Author: John Ahlquist, Nov. 2001
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/LogicToClient.h"
#include "Common/LogicAudio.h"

#include "Common/AudioAffect.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/MapObject.h"							// For MAP_XY_FACTOR
#include "Common/PartitionSolver.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Radar.h"									// For TheRadar
#include "Common/SpecialPower.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Team.h"
#include "Common/Upgrade.h"

#include "GameClient/Anim2D.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/CommandXlat.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/Eva.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameText.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/InGameUI.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Mouse.h"
#include "GameClient/View.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Snow.h"

#include "GameLogic/AI.h"
#include "GameLogic/AISkirmishPlayer.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/CaveContain.h"
#include "GameLogic/Module/CommandButtonHuntUpdate.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/DeliverPayloadAIUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/SupplyWarehouseDockUpdate.h"
#include "GameLogic/Module/TransportContain.h"
#include "GameLogic/Module/MobNexusContain.h"
#include "GameLogic/Module/RailroadGuideAIUpdate.h"
#include "GameLogic/Module/StickyBombUpdate.h"
#include "GameLogic/ObjectTypes.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptActions.h"
#include "ScriptActionsInternal.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/AIPathfind.h"
#include "Lib/SafeString.h"

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
// when you set controlling player or merge teams, we don't always update all the upgrade stuff
// or the indicator color. this allows us to force the situation. (srj)
void updateTeamAndPlayerStuff( Object *obj, void *userData )
{
	if (obj)
	{
		obj->updateUpgradeModules();
		
		// srj sez: apparently we have to do this too, since Team::setControllingPlayer
		// does not. Might make more sense to do it there, but am scared to at this point.
		Drawable* draw = obj->getDrawable();
		if (draw)
		{
			const Color color = (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT) ? obj->getNightIndicatorColor() : obj->getIndicatorColor();
			TheLogicToClient->drawable( draw, [color]( Drawable *d ) { d->setIndicatorColor( color ); } );
		}
	}
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

// STATICS ////////////////////////////////////////////////////////////////////////////////////////

// DEFINES ////////////////////////////////////////////////////////////////////////////////////////

// GLOBALS ////////////////////////////////////////////////////////////////////////////////////////
GameWindow *ScriptActions::m_messageWindow = nullptr;

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ScriptActions::ScriptActions()
{
	m_suppressNewWindows = FALSE;
	m_unnamedUnit = AsciiString::TheEmptyString;

}  // end ScriptActions

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ScriptActions::~ScriptActions()
{
	reset(); // just in case.
}  // end ~ScriptActions

//-------------------------------------------------------------------------------------------------
/** Init */
//-------------------------------------------------------------------------------------------------
void ScriptActions::init( void )
{

	reset();

}  // end init

//-------------------------------------------------------------------------------------------------
/** Reset */
//-------------------------------------------------------------------------------------------------
void ScriptActions::reset( void )
{
	m_suppressNewWindows = FALSE;
	closeWindows(FALSE); // Close victory or defeat windows.

}  // end reset

//-------------------------------------------------------------------------------------------------
/** Update */
//-------------------------------------------------------------------------------------------------
void ScriptActions::update( void )
{
	// Empty for now.  jba.
}  // end update


//-------------------------------------------------------------------------------------------------
/** closeWindows */
//-------------------------------------------------------------------------------------------------
void ScriptActions::closeWindows( Bool suppressNewWindows )
{
	m_suppressNewWindows = suppressNewWindows;

	if (m_messageWindow) {
		TheWindowManager->winDestroy(m_messageWindow);
		m_messageWindow = nullptr;
	}
}  

//-------------------------------------------------------------------------------------------------
/** doQuickVictory */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doQuickVictory( void )
{
	closeWindows(FALSE);
	TheGameLogic->closeWindows();
	doDisableInput();
	if(TheCampaignManager)
		TheCampaignManager->SetVictorious(TRUE);
	TheScriptEngine->startQuickEndGameTimer();
}

//-------------------------------------------------------------------------------------------------
/** doSetInfantryLightingOverride */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetInfantryLightingOverride(Real setting)
{
	DEBUG_ASSERTCRASH( (setting == -1.0f) || (setting > 0.0f), ("Invalid setting (%g) in Infantry Lighting Override script.", setting) );
	TheWritableGlobalData->m_scriptOverrideInfantryLightScale = setting;
}

//-------------------------------------------------------------------------------------------------
/** doVictory */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doVictory( void )
{
	closeWindows(FALSE);
	TheGameLogic->closeWindows();
	doDisableInput();
	if (!m_suppressNewWindows)
	{
		const Player *localPlayer = ThePlayerList->getLocalPlayer();
		Bool showObserverWindow = localPlayer->isPlayerObserver() || TheScriptEngine->hasShownMPLocalDefeatWindow();
		if(showObserverWindow)
			m_messageWindow = TheWindowManager->winCreateFromScript("Menus/ObserverQuit.wnd");
		else
		{
			m_messageWindow = TheWindowManager->winCreateFromScript("Menus/Victorious.wnd");
		}
	}	
	if(TheCampaignManager)
		TheCampaignManager->SetVictorious(TRUE);
	TheScriptEngine->startEndGameTimer();
}

//-------------------------------------------------------------------------------------------------
/** doDefeat */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDefeat( void )
{
	closeWindows(FALSE);
	TheGameLogic->closeWindows();
	doDisableInput();
	if (!m_suppressNewWindows)
	{
		const Player *localPlayer = ThePlayerList->getLocalPlayer();
		Bool showObserverWindow = localPlayer->isPlayerObserver() || TheScriptEngine->hasShownMPLocalDefeatWindow();
		if(showObserverWindow)
			m_messageWindow = TheWindowManager->winCreateFromScript("Menus/ObserverQuit.wnd");
		else
		{
			m_messageWindow = TheWindowManager->winCreateFromScript("Menus/Defeat.wnd");
		}
	}
	if(TheCampaignManager)
		TheCampaignManager->SetVictorious(FALSE);
	TheScriptEngine->startEndGameTimer();
}

//-------------------------------------------------------------------------------------------------
/** doLocalDefeat */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doLocalDefeat( void )
{
	TheScriptEngine->markMPLocalDefeatWindowShown();
	closeWindows(FALSE);
	TheGameLogic->closeWindows();
	if (!m_suppressNewWindows)
	{
		if(!TheVictoryConditions->amIObserver())
			m_messageWindow = TheWindowManager->winCreateFromScript("Menus/LocalDefeat.wnd");
	}
	if(TheCampaignManager)
		TheCampaignManager->SetVictorious(FALSE);
	TheScriptEngine->startCloseWindowTimer();
}

//-------------------------------------------------------------------------------------------------
/** changeObjectPanelFlagForSingleObject */
//-------------------------------------------------------------------------------------------------
void ScriptActions::changeObjectPanelFlagForSingleObject(Object *obj, const AsciiString& flagToChange, Bool newVal )
{
	// Enabled flag
	if (flagToChange == TheObjectFlagsNames[0])
	{
		obj->setScriptStatus(OBJECT_STATUS_SCRIPT_DISABLED, !newVal);
		return;
	}

	// Powered flag
	if (flagToChange == TheObjectFlagsNames[1])
	{
		obj->setScriptStatus(OBJECT_STATUS_SCRIPT_UNPOWERED, !newVal);
		return;
	}

	// Indestructible flag
	if (flagToChange == TheObjectFlagsNames[2])
	{
		BodyModuleInterface* body = obj->getBodyModule();
		if (body)	{
			body->setIndestructible(newVal);
		}
		return;
	}

	// Unsellable flag
	if (flagToChange == TheObjectFlagsNames[3])
	{
		obj->setScriptStatus(OBJECT_STATUS_SCRIPT_UNSELLABLE, newVal);
		return;
	}

	// Selectable flag
	if (flagToChange == TheObjectFlagsNames[4])
	{
		if (obj->isSelectable() != newVal) {
			obj->setSelectable(newVal);
		}
		return;
	}

	// AI Recruitable flag
	if (flagToChange == TheObjectFlagsNames[5])
	{
		if (obj->getAIUpdateInterface()) {
			obj->getAIUpdateInterface()->setIsRecruitable(newVal);
		}

		return;
	}

	// Player targetable flag
	if( flagToChange == TheObjectFlagsNames[6] )
	{
		obj->setScriptStatus( OBJECT_STATUS_SCRIPT_TARGETABLE, newVal );
		return;
	}

}



//-------------------------------------------------------------------------------------------------
/** doDamageTeamMembers */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDamageTeamMembers(const AsciiString& team, Real amount)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( team );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if (theTeam) {
		theTeam->damageTeamMembers(amount);
	}
}  

//-------------------------------------------------------------------------------------------------
/** doMoveToWaypoint */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMoveToWaypoint(const AsciiString& team, const AsciiString& waypoint)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( team );

	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if (theTeam) {
		AIGroup* theGroup = TheAI->createGroup();
		if (!theGroup) {
			return;
		}

		theTeam->getTeamAsAIGroup(theGroup);
		Waypoint *way = TheTerrainLogic->getWaypointByName(waypoint);
		if (way) {
			Coord3D destination = *way->getLocation();
			//DEBUG_LOG(("Moving team to waypoint %f, %f, %f\n", destination.x, destination.y, destination.z));
 			theGroup->groupMoveToPosition( &destination, false, CMD_FROM_SCRIPT );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doSetTeamState */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetTeamState(const AsciiString& team, const AsciiString& state)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( team );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if (theTeam) {
		theTeam->setState(state);
	}
}	

/** doCreateReinforcements */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCreateReinforcements(const AsciiString& team, const AsciiString& waypoint)
{
	TeamPrototype *theTeamProto = TheTeamFactory->findTeamPrototype( team );
	Coord3D destination;


	Bool needToMoveToDestination = false;
	//Validate the waypoint
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypoint); 
	if (way==nullptr) 
	{
		return;
	}

	destination = *way->getLocation();
	if (!theTeamProto) {
		DEBUG_LOG(("***WARNING - Team %s not found.\n", team.str()));
		return;
	}
	const TeamTemplateInfo *pInfo = theTeamProto->getTemplateInfo();
	Coord3D origin = destination;
	way = TheTerrainLogic->getWaypointByName(pInfo->m_startReinforceWaypoint);
	if (way) {
		origin = *way->getLocation();
		if (origin.x != destination.x || origin.y != destination.y) {
			needToMoveToDestination = true;
		}
	}

	//Create the team (not the units inside team).
	Team *theTeam = TheTeamFactory->createInactiveTeam( team );
	if (theTeam==nullptr) {
		return;
	}
	const ThingTemplate *transportTemplate;
	const ThingTemplate *unitTemplate;

	//Create the transport first (if applicable), so we can determine if it has paradrop capabilities.
	//If so, we'll be doing a lot of things differently!
	Object *transport=nullptr;
	ContainModuleInterface *contain = nullptr;
	transportTemplate = TheThingFactory->findTemplate( pInfo->m_transportUnitType );
	if( transportTemplate ) 
	{
		transport = TheThingFactory->newObject( transportTemplate, theTeam );
		transport->setPosition( &origin );
		transport->setOrientation( 0.0f );
		if( transport ) 
		{
			contain = transport->getContain();
		}
	}
	Int transportCount = 1;

	//Check to see if we have a transport, and if our transport has paradrop capabilities. If this is the
	//case, we'll need to create each unit inside "parachute containers".
	static NameKeyType key_DeliverPayloadAIUpdate = NAMEKEY("DeliverPayloadAIUpdate");
	DeliverPayloadAIUpdate *dp = nullptr;
	if( transport )
	{
		dp = static_cast<DeliverPayloadAIUpdate*>(transport->findUpdateModule(key_DeliverPayloadAIUpdate));
	}

	//Our tranport has a deliverPayload update module. This means it'll do airborned drops.

	const ThingTemplate* putInContainerTemplate  = nullptr;
	if( dp )
	{
		//Check to see if we are packaging our units
		putInContainerTemplate = dp->getPutInContainerTemplateViaModuleData();
	}

	//Now create the units that make up the team.
	Int i, j;
	for (i=0; i<pInfo->m_numUnitsInfo; i++) 
	{
		// get thing template based from map object name
		unitTemplate = TheThingFactory->findTemplate(pInfo->m_unitsInfo[i].unitThingName);
		Coord3D pos = origin;
		if (unitTemplate && theTeam) 
		{
			Object *obj = nullptr;
			for (j=0; j<pInfo->m_unitsInfo[i].maxUnits; j++) 
			{
				// create new object in the world
				obj = TheThingFactory->newObject( unitTemplate, theTeam );
				if( obj )
				{
					/// @todo - have better positioning for reinforcement units.
					pos.x = static_cast<Real>(origin.x + 2.25*(j)*obj->getGeometryInfo().getMajorRadius());
					pos.z = TheTerrainLogic->getGroundHeight(pos.x, pos.y);
					obj->setPosition( &pos );
					obj->setOrientation(0.0f);
          

				}  // end if
			}
			if (obj) pos.y += 2*obj->getGeometryInfo().getMajorRadius();
		}
		origin.y = pos.y;
	}
	origin = destination;
	if (pInfo->m_teamStartsFull) 
	{
		// Have them load into transports in the team.
		EntriesVec vecOfUnits;
		SpacesVec vecOfTransports;
		
		for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
		{
			Object *obj = iter.cur();
			if (!obj) {
				continue;
			}
			if (obj==transport) {
				continue; // Skip the one we created.  The team loads into the transports on the team for team starts full. jba
			}
			if (obj->isKindOf(KINDOF_TRANSPORT)) 
			{
				ContainModuleInterface *contain3 = obj->getContain();
				if( contain3 )
				{
					vecOfTransports.push_back(std::make_pair(obj->getID(), (static_cast<TransportContain*>(contain3))->getContainMax()));
				}
				else
				{
					DEBUG_CRASH( ("doCreateReinforcement script -- transport doesn't have contain to hold guys.") );
				}
			} 
			else 
			{
				Int slots = obj->getTransportSlotCount();
				if (slots==0) slots = 0x7fffff; // 0 means lots.
				vecOfUnits.push_back(std::make_pair(obj->getID(), slots));
			}
		}

		// we've now built the necessary stuff. Partition the units.

		PartitionSolver partition(vecOfUnits, vecOfTransports, SolutionType::PREFER_FAST_SOLUTION);
		partition.solve();
		SolutionVec solution = partition.getSolution();
		for (int i2 = 0; i2 < solution.size(); ++i2) {
			Object *unit = TheGameLogic->findObjectByID(solution[i2].first);
			Object *trans = TheGameLogic->findObjectByID(solution[i2].second);
			if (!unit || !trans) {
				continue;
			}
			ContainModuleInterface *contain2 = trans->getContain();
			if (contain2) 
			{
				contain2->addToContain(unit);
			}
		}		

	}
	contain = nullptr;
	if( transport )
	{
		contain = transport->getContain();
	}
	if (contain) 
	{
		for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
		{
			Object *obj = iter.cur();
			if (!obj) 
			{
				continue;
			}
			if (obj->getTemplate()->isEquivalentTo(transport->getTemplate()) ) 
			{
				// it's our transport.
				continue;
			}
			if (obj->getContainedBy() != nullptr) 
			{
				continue;
			}
			//Check to see if it's a valid transport for this unit, even if it's full.

			Coord3D pos = origin;
			pos.x += transportCount*transport->getGeometryInfo().getMajorRadius();
			pos.z = TheTerrainLogic->getGroundHeight(pos.x, pos.y);
			
			if (contain && contain->isValidContainerFor(obj, false)) 
			{
				//Now that we know it fits in the transport, check to see if it's full. If it is,
				//then we'll create a new transport.
				if (!contain->isValidContainerFor(obj, true)) 
				{
					// full, try building another.
					transport = TheThingFactory->newObject( transportTemplate, theTeam );
					transport->setPosition( &pos );
					transportCount++;
					transport->setOrientation(0.0f);
					if (transport) 
					{
						contain = transport->getContain();
					}
				}
				//If our unit is going to be put in another container (such as infantry being contained by a parachute)
				//do so now.
				if( putInContainerTemplate )
				{
					Object* container = TheThingFactory->newObject( putInContainerTemplate, theTeam );
					container->setPosition( &pos );

					//Make sure this is valid.
					if( container->getContain() && container->getContain()->isValidContainerFor( obj, true ) )
					{
						container->getContain()->addToContain( obj );
						obj = container;
					}
					else
					{
						DEBUG_CRASH( ("doCreateReinforcements: PutInContainer %s is full, or not valid for the payload %s!", putInContainerTemplate->getName().str(), obj->getTemplate()->getName().str() ) );
					}
				}

				//Add our unit to the transport.
				contain->addToContain( obj );
			}
		}
	}


	if (theTeam) 
	{
		if (transport) 
		{
			for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
			{
				Object *obj = iter.cur();
				if (!obj) 
				{
					continue;
				}
				AIUpdateInterface *ai = obj->getAIUpdateInterface();
				if (obj->getTemplate()->isEquivalentTo(transport->getTemplate()) ) 
				{
					if( dp )
					{
						dp->deliverPayloadViaModuleData( &destination );
					}
					// it's our transport.
					else if( pInfo->m_transportsExit )
					{
						if( ai )
						{
							ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
							ai->aiMoveToAndEvacuateAndExit(&destination, CMD_FROM_SCRIPT);
						}
					}
					else
					{
						if( ai )
						{
							ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
							ai->aiMoveToAndEvacuate( &destination, CMD_FROM_SCRIPT );
						}
					}
				}	
				else 
				{
					// If there are any units that aren't transportable, move them to the goal.
					if( !obj->isDisabledByType( DISABLED_HELD ) ) 
					{
						if( ai )
						{
							ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
							ai->aiMoveToPosition(&destination, CMD_FROM_SCRIPT);
						}
					}
				}
			}
		} 
		else 
		{
			theTeam->setActive();
			if (needToMoveToDestination) 
			{
				AIGroup* theGroup = TheAI->createGroup();
				if (!theGroup) 
				{
					return;
				}
				theTeam->getTeamAsAIGroup(theGroup);
				theGroup->groupMoveToPosition( &destination, false, CMD_FROM_SCRIPT );
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doCreateObject */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCreateObject(const AsciiString& objectName, const AsciiString& thingName, const AsciiString& teamName, Coord3D *pos, Real angle )
{
	Object* pOldObj = nullptr;

	if (objectName != m_unnamedUnit) {
		 pOldObj = TheScriptEngine->getUnitNamed(objectName);

		if (pOldObj && !pOldObj->isEffectivelyDead()) {
			AsciiString str = "WARNING - Object with name ";
			str.concat(objectName);
			str.concat(" already exists. Failed Create.");
			TheScriptEngine->AppendDebugMessage(str, false);
				// Unit by that name already exists
			return;
		}
	}

	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if (theTeam==nullptr && TheTeamFactory->findTeamPrototype( teamName )) {
		// We may need to create the team; createTeam throws for a name without a prototype.
		theTeam = TheTeamFactory->createTeam( teamName );
	}
	if (!theTeam) {
		TheScriptEngine->AppendDebugMessage("***WARNING - Team not found:***", false);
		TheScriptEngine->AppendDebugMessage(teamName, true);
		DEBUG_LOG(("WARNING - Team %s not found.\n", teamName.str()));
		return;
	}
	const ThingTemplate *thingTemplate;
	// get thing template based from map object name
	thingTemplate = TheThingFactory->findTemplate(thingName);
	if (thingTemplate) {
		// create new object in the world
		Object *obj = TheThingFactory->newObject( thingTemplate, theTeam );
		if( obj )
		{
			if (objectName != m_unnamedUnit) {
				obj->setName(objectName);
				if (pOldObj || TheScriptEngine->didUnitExist(objectName)) {
					TheScriptEngine->transferObjectName(objectName, obj);
				} else {
					TheScriptEngine->addObjectToCache(obj);
				}
			}

			obj->setOrientation(angle);
			obj->setPosition( pos );
      
      if ( obj->isKindOf( KINDOF_BLAST_CRATER ) ) // since these footprints are permanent
      {
        TheTerrainLogic->createCraterInTerrain( obj );
        TheAI->pathfinder()->addObjectToPathfindMap( obj );
      }


		}  // end if
	} else {
		DEBUG_LOG(("WARNING - ThingTemplate '%s' not found.\n", thingName.str()));
	}
}

//-------------------------------------------------------------------------------------------------
/** doAttack */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doAttack(const AsciiString& attackerName, const AsciiString& victimName)
{
	Team *attackingTeam = TheScriptEngine->getTeamNamed( attackerName );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	const Team *victimTeam = TheScriptEngine->getTeamNamed( victimName );
	
	// sanity
	if( attackingTeam == nullptr || victimTeam == nullptr )
		return;

	AIGroup *aiGroup = TheAI->createGroup();
	if (!aiGroup) {
		return;
	}

	attackingTeam->getTeamAsAIGroup(aiGroup);
	aiGroup->groupAttackTeam(victimTeam, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT);

}

//-------------------------------------------------------------------------------------------------
/** doBuildBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildBuilding(const AsciiString& buildingType)
{
	// This action ALWAYS occur on the current player.
	Player *thePlayer = TheScriptEngine->getCurrentPlayer();
	if (thePlayer) {
		thePlayer->buildSpecificBuilding(buildingType);
	}
}

//-------------------------------------------------------------------------------------------------
/** doBuildSupplyCenter */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildSupplyCenter(const AsciiString& player, const AsciiString& buildingType, Int cash)
{
	Player* thePlayer = TheScriptEngine->getPlayerFromAsciiString(player);
	if (thePlayer) {
		thePlayer->buildBySupplies(cash, buildingType);
	}
}

//-------------------------------------------------------------------------------------------------
/** doBuildObjectNearestTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildObjectNearestTeam( const AsciiString& playerName, const AsciiString& buildingType, const AsciiString& teamName )
{
	Player *thePlayer = TheScriptEngine->getPlayerFromAsciiString( playerName );
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if( thePlayer && theTeam ) 
	{
		thePlayer->buildSpecificBuildingNearestTeam( buildingType, theTeam );
	}
}

//-------------------------------------------------------------------------------------------------
/** doBuildUpgrade */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildUpgrade(const AsciiString& player, const AsciiString& upgrade)
{
	Player* thePlayer = TheScriptEngine->getPlayerFromAsciiString(player);
	if (thePlayer) {
		thePlayer->buildUpgrade(upgrade);
	}
}


//-------------------------------------------------------------------------------------------------
/** doBuildBaseDefense */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildBaseDefense(Bool flank)
{
	// This action ALWAYS occur on the current player.
	Player *thePlayer = TheScriptEngine->getCurrentPlayer();
	if (thePlayer) {
		thePlayer->buildBaseDefense(flank);
	}
}

//-------------------------------------------------------------------------------------------------
/** doBuildBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildBaseStructure(const AsciiString& buildingType, Bool flank)
{
	// This action ALWAYS occur on the current player.
	Player *thePlayer = TheScriptEngine->getCurrentPlayer();
	if (thePlayer) {
		thePlayer->buildBaseDefenseStructure(buildingType, flank);
	}
}


//-------------------------------------------------------------------------------------------------
/** createUnitOnTeamAt */
//-------------------------------------------------------------------------------------------------
void ScriptActions::createUnitOnTeamAt(const AsciiString& unitName, const AsciiString& objType, const AsciiString& teamName, const AsciiString& waypoint)
{
	Object* pOldObj = TheScriptEngine->getUnitNamed(unitName);

	if (pOldObj && !pOldObj->isEffectivelyDead()) {
		AsciiString str = "WARNING - Object with name ";
		str.concat(unitName);
		str.concat(" already exists. Failed Create.");
		TheScriptEngine->AppendDebugMessage(str, false);
			// Unit by that name already exists
		return;
	}

	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if (theTeam==nullptr && TheTeamFactory->findTeamPrototype( teamName )) {
		// We may need to create the team; createTeam throws for a name without a prototype.
		theTeam = TheTeamFactory->createTeam( teamName );
	}
	if (!theTeam) {
		TheScriptEngine->AppendDebugMessage("***WARNING - Team not found:***", false);
		TheScriptEngine->AppendDebugMessage(teamName, true);
		DEBUG_LOG(("WARNING - Team %s not found.\n", teamName.str()));
		return;
	}
	const ThingTemplate *thingTemplate;
	// get thing template based from map object name
	thingTemplate = TheThingFactory->findTemplate(objType);
	if (thingTemplate) {
		// create new object in the world
		Object *obj = TheThingFactory->newObject( thingTemplate, theTeam );
		if( obj )
		{
			if (unitName != m_unnamedUnit) {
				obj->setName(unitName);
				if (pOldObj || TheScriptEngine->didUnitExist(unitName)) {
					TheScriptEngine->transferObjectName(unitName, obj);
				} else {
					TheScriptEngine->addObjectToCache(obj);
				}
			}
	
			Waypoint *way = TheTerrainLogic->getWaypointByName( waypoint );
			if (way)
			{
				Coord3D destination = *way->getLocation();
				obj->setPosition(&destination);
			}
		}  // end if
	} else {
		DEBUG_LOG(("WARNING - ThingTemplate '%s' not found.\n", objType.str()));
	}
}

//-------------------------------------------------------------------------------------------------
/** updateNamedAttackPrioritySet */
//-------------------------------------------------------------------------------------------------
void ScriptActions::updateNamedAttackPrioritySet(const AsciiString& unitName, const AsciiString& attackPrioritySet)
{
	Object *theSrcUnit = TheScriptEngine->getUnitNamed(unitName);
	if (!theSrcUnit) {
		return;
	}

	AIUpdateInterface *pInterface = theSrcUnit->getAIUpdateInterface();
	if (!pInterface) {
		return;
	}
	
	const AttackPriorityInfo *info = TheScriptEngine->getAttackInfo(attackPrioritySet);

	pInterface->setAttackInfo(info);

}

//-------------------------------------------------------------------------------------------------
/** updateTeamAttackPrioritySet */
//-------------------------------------------------------------------------------------------------
void ScriptActions::updateTeamAttackPrioritySet(const AsciiString& teamName, const AsciiString& attackPrioritySet)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);

	if (!team) {
		return;
	}

	const AttackPriorityInfo *info = TheScriptEngine->getAttackInfo(attackPrioritySet);
	
	if (info->getName().isNotEmpty()) {
		team->setAttackPriorityName(info->getName());
	}

	// Set team member's attack priority.
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai) {
			continue;
		}
		ai->setAttackInfo(info);
	}
}

//-------------------------------------------------------------------------------------------------
/** updateBaseConstructionSpeed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::updateBaseConstructionSpeed(const AsciiString& playerName, Int delayInSeconds)
{
	Player* thePlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (thePlayer) {
		thePlayer->setTeamDelaySeconds(delayInSeconds);
	}
}

//-------------------------------------------------------------------------------------------------
/** updateNamedSetAttitude */
//-------------------------------------------------------------------------------------------------
void ScriptActions::updateNamedSetAttitude(const AsciiString& unitName, Int attitude)
{
	Object *theSrcUnit = TheScriptEngine->getUnitNamed(unitName);
	if (!theSrcUnit) {
		return;
	}

	AIUpdateInterface *pInterface = theSrcUnit->getAIUpdateInterface();
	if (!pInterface) {
		return;
	}
	
	pInterface->setAttitude(static_cast<AttitudeType>(attitude));
}

//-------------------------------------------------------------------------------------------------
/** updateTeamSetAttitude */
//-------------------------------------------------------------------------------------------------
void ScriptActions::updateTeamSetAttitude(const AsciiString& teamName, Int attitude)
{
	Team *theSrcTeam = TheScriptEngine->getTeamNamed(teamName);
	if (!theSrcTeam) {
		return;
	}

	AIGroup *pAIGroup = TheAI->createGroup();
	if (!pAIGroup) {
		return;
	}

	theSrcTeam->getTeamAsAIGroup(pAIGroup);
	pAIGroup->setAttitude(static_cast<AttitudeType>(attitude));
}

//-------------------------------------------------------------------------------------------------
/** doLoadAllTransports */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doLoadAllTransports(const AsciiString& teamName)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if (!theTeam) {
		return;
	}

	EntriesVec vecOfUnits;
	SpacesVec vecOfTransports;
	
	for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		if (!obj) {
			continue;
		}

		if( obj->isKindOf(KINDOF_TRANSPORT) ) 
		{
			ContainModuleInterface *contain = obj->getContain();
			if( contain )
			{
				vecOfTransports.push_back(std::make_pair(obj->getID(), (static_cast<TransportContain*>(obj->getContain()))->getContainMax()));
			}
			else
			{
				DEBUG_CRASH( ("doLoadAllTransports script -- transport doesn't have a container!") );
			}
		} 
		else 
		{
			vecOfUnits.push_back(std::make_pair(obj->getID(), obj->getTransportSlotCount()));
		}
	}

	// we've now built the necessary stuff. Partition the units.

	PartitionSolver partition(vecOfUnits, vecOfTransports, SolutionType::PREFER_FAST_SOLUTION);
	partition.solve();
	SolutionVec solution = partition.getSolution();
	for (int i = 0; i < solution.size(); ++i) {
		Object *unit = TheGameLogic->findObjectByID(solution[i].first);
		Object *trans = TheGameLogic->findObjectByID(solution[i].second);
		if (!unit || !trans) {
			continue;
		}
		if( unit->getAIUpdateInterface() )
		{
			unit->getAIUpdateInterface()->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
			unit->getAIUpdateInterface()->aiEnter(trans, CMD_FROM_SCRIPT);
		}
	}		
}





//-------------------------------------------------------------------------------------------------
/** doBuildTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildTeam(const AsciiString& teamName)
{
	TeamPrototype *theTeamProto = TheTeamFactory->findTeamPrototype( teamName );
	if (theTeamProto) {
		Player *player = theTeamProto->getControllingPlayer();
		if (player) {
			player->buildSpecificTeam(theTeamProto);
		}
	}	
}

//-------------------------------------------------------------------------------------------------
/** doRecruitTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRecruitTeam(const AsciiString& teamName, Real recruitRadius)
{
	TeamPrototype *theTeamProto = TheTeamFactory->findTeamPrototype( teamName );
	if (theTeamProto) {
		Player *player = theTeamProto->getControllingPlayer();
		if (player) {
			player->recruitSpecificTeam(theTeamProto, recruitRadius);
		}
	}	
}

#define ARBITRARY_BUFFER_SIZE	128

//-------------------------------------------------------------------------------------------------
/** doSpeechPlay */
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
/** excludePlayerFromScoreScreen */
//-------------------------------------------------------------------------------------------------
void ScriptActions::excludePlayerFromScoreScreen(const AsciiString& playerName)
{
	Player *pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (pPlayer == nullptr) {
		return;
	}

	pPlayer->setListInScoreScreen(FALSE);
}

//-------------------------------------------------------------------------------------------------
/** excludePlayerFromScoreScreen */
//-------------------------------------------------------------------------------------------------
void ScriptActions::enableScoring(Bool score)
{
	TheGameLogic->enableScoring(score);
}

static PlayerMaskType getHumanPlayerMask( void )
{
	// start empty and combine with |=
	PlayerMaskType mask = PLAYERMASK_NONE;
	for (Int i=0; i<ThePlayerList->getPlayerCount(); ++i)
	{
		const Player *player = ThePlayerList->getNthPlayer(i);
		if (player->getPlayerType() == PLAYER_HUMAN)
			mask |= player->getPlayerMask();
	}

	//DEBUG_LOG(("getHumanPlayerMask(): mask was %4.4X\n", mask));
	return mask;
}

//-------------------------------------------------------------------------------------------------
/** doRevealMapAtWaypoint */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRevealMapAtWaypoint(const AsciiString& waypointName, Real radiusToReveal, const AsciiString& playerName)
{
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypointName);
	if (!way) {
		return;
	}

	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	PlayerMaskType playerMask;
	if (player && playerName.isNotEmpty())
		playerMask = player->getPlayerMask();
	else
		playerMask = getHumanPlayerMask();

	Real positionX = way->getLocation()->x;
	Real positionY = way->getLocation()->y;

	// A reveal script is a quick look.  That way a Radar Jammer will still function correctly.
	ThePartitionManager->doShroudReveal(positionX, positionY, radiusToReveal, playerMask);
	ThePartitionManager->undoShroudReveal(positionX, positionY, radiusToReveal, playerMask);
}

//-------------------------------------------------------------------------------------------------
/** doRevealMapAtWaypoint */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doShroudMapAtWaypoint(const AsciiString& waypointName, Real radiusToShroud, const AsciiString& playerName)
{
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypointName);
	if (!way) {
		return;
	}

	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	PlayerMaskType playerMask;
	if (player && playerName.isNotEmpty())
		playerMask = player->getPlayerMask();
	else
		playerMask = getHumanPlayerMask();

	Real positionX = way->getLocation()->x;
	Real positionY = way->getLocation()->y;

	// Likewise, this script does a dollop of shroud.  Not permanent active shroud
	ThePartitionManager->doShroudCover(positionX, positionY, radiusToShroud, playerMask);
	ThePartitionManager->undoShroudCover(positionX, positionY, radiusToShroud, playerMask);
}

//-------------------------------------------------------------------------------------------------
/** doRevealMapEntire */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRevealMapEntire(const AsciiString& playerName)
{
	DEBUG_LOG(("ScriptActions::doRevealMapEntire() for player named '%s'\n", playerName.str()));
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (player && playerName.isNotEmpty())
	{
		DEBUG_LOG(("ScriptActions::doRevealMapEntire() for player named '%ls' in position %d\n", player->getPlayerDisplayName().str(), player->getPlayerIndex()));
		ThePartitionManager->revealMapForPlayer( player->getPlayerIndex() );
	}
	else
	{
		DEBUG_LOG(("ScriptActions::doRevealMapEntire() - no player, so doing all human players\n"));
		for (Int i=0; i<ThePlayerList->getPlayerCount(); ++i)
		{
			Player *player6 = ThePlayerList->getNthPlayer(i);
			if (player6->getPlayerType() == PLAYER_HUMAN)
			{
				DEBUG_LOG(("ScriptActions::doRevealMapEntire() for player %d\n", i));
				ThePartitionManager->revealMapForPlayer( i );
			}
		}
	}
}

void ScriptActions::doRevealMapEntirePermanently( Bool reveal, const AsciiString& playerName )
{
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (player && playerName.isNotEmpty())
	{
		if( reveal )
			ThePartitionManager->revealMapForPlayerPermanently( player->getPlayerIndex() );
		else
			ThePartitionManager->undoRevealMapForPlayerPermanently( player->getPlayerIndex() );
	}
	else
	{
		for (Int i=0; i<ThePlayerList->getPlayerCount(); ++i)
		{
			Player *player5 = ThePlayerList->getNthPlayer(i);
			if (player5->getPlayerType() == PLAYER_HUMAN)
			{
				DEBUG_LOG(("ScriptActions::doRevealMapEntirePermanently() for player %d\n", i));
				if( reveal )
					ThePartitionManager->revealMapForPlayerPermanently( i );
				else
					ThePartitionManager->undoRevealMapForPlayerPermanently( i );
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doShroudMapEntire */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doShroudMapEntire(const AsciiString& playerName)
{
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (player && playerName.isNotEmpty())
	{
		ThePartitionManager->shroudMapForPlayer( player->getPlayerIndex() );
	}
	else
	{
		for (Int i=0; i<ThePlayerList->getPlayerCount(); ++i)
		{
			Player *player4 = ThePlayerList->getNthPlayer(i);
			if (player4->getPlayerType() == PLAYER_HUMAN)
			{
				DEBUG_LOG(("ScriptActions::doShroudMapEntire() for player %d\n", i));
				ThePartitionManager->shroudMapForPlayer( i );
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doCollectNearbyForTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCollectNearbyForTeam(const AsciiString& teamName)
{
	DEBUG_CRASH(("You would think this has been implemented, but you'd be wrong. (doCollectNearbyForTeam)"));
}

//-------------------------------------------------------------------------------------------------
/** doMergeTeamIntoTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMergeTeamIntoTeam(const AsciiString& teamSrcName, const AsciiString& teamDestName)
{
	Team *teamSrc = TheScriptEngine->getTeamNamed(teamSrcName);
	Team *teamDest = TheScriptEngine->getTeamNamed(teamDestName);
	if (teamDest==nullptr) {
		teamDest = TheTeamFactory->findTeam( teamDestName );
	}
	if (!teamSrc || !teamDest) {
		return;
	}

//	Bool done = FALSE;
	
	DLINK_ITERATOR<Object> iter = teamSrc->iterate_TeamMemberList();
	Object *nextObj = iter.cur();

	while (!iter.done()) {
		Object *obj = nextObj;
		if (!obj) {
			break;
		}

		// this has to be done here, setting the team will screw up the iterator. total bummer dude. jkmcd
		nextObj = iter.cur();
		iter.advance();
		obj->setTeam(teamDest);
		updateTeamAndPlayerStuff(obj, nullptr);
	}

	if (nextObj) {
		nextObj->setTeam(teamDest);
		updateTeamAndPlayerStuff(nextObj, nullptr);
	}

	teamSrc->deleteTeam();
	teamDest->setActive(); // in case we just created him.
}

//-------------------------------------------------------------------------------------------------
/** doDisableInput */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDisableInput()
{
#if defined(_DEBUG)
	if (!TheGlobalData->m_disableScriptedInputDisabling)
#endif
	{
		TheInGameUI->setInputEnabled(false);
		TheMouse->setVisibility(false);
		TheInGameUI->deselectAllDrawables();
		TheInGameUI->clearAttackMoveToMode();
		TheInGameUI->setWaypointMode( FALSE );
		TheControlBar->deleteBuildTooltipLayout();
		TheLookAtTranslator->resetModes();
	}
}

//-------------------------------------------------------------------------------------------------
/** doEnableInput */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doEnableInput()
{
	TheInGameUI->setInputEnabled(true);
	TheMouse->setVisibility(true);
}

//-------------------------------------------------------------------------------------------------
/** doSetBorderShroud */
//-------------------------------------------------------------------------------------------------
// Client part, run after the logic tick.
static void setBorderShroud( Bool setting )
{
	if( setting )
		TheDisplay->setBorderShroudLevel(TheGlobalData->m_shroudAlpha);
	else
		TheDisplay->setBorderShroudLevel(TheGlobalData->m_clearAlpha);
}

void ScriptActions::doSetBorderShroud( Bool setting )
{
	TheLogicToClient->post( [setting]() { setBorderShroud( setting ); } );
}

//-------------------------------------------------------------------------------------------------
/** doIdleAllPlayerUnits */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doIdleAllPlayerUnits(const AsciiString& playerName)
{
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (player && playerName.isNotEmpty())
	{
		player->setUnitsShouldIdleOrResume(true);
	}
	else
	{
		for (Int i=0; i<ThePlayerList->getPlayerCount(); ++i)
		{
			Player *player3 = ThePlayerList->getNthPlayer(i);
			if (player3->getPlayerType() == PLAYER_HUMAN)
			{
				DEBUG_LOG(("ScriptActions::doIdleAllPlayerUnits() for player %d\n", i));
				player3->setUnitsShouldIdleOrResume(true);
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doResumeSupplyTruckingForIdleUnits */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doResumeSupplyTruckingForIdleUnits(const AsciiString& playerName)
{
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (player && playerName.isNotEmpty())
	{
		player->setUnitsShouldIdleOrResume(false);
	}
	else
	{
		for (Int i=0; i<ThePlayerList->getPlayerCount(); ++i)
		{
			Player *player2 = ThePlayerList->getNthPlayer(i);
			if (player2->getPlayerType() == PLAYER_HUMAN)
			{
				DEBUG_LOG(("ScriptActions::doResumeSupplyTruckingForIdleUnits() for player %d\n", i));
				player2->setUnitsShouldIdleOrResume(false);
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doExitSpecificBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doExitSpecificBuilding(const AsciiString& buildingName)
{
	Object *theBuilding = TheScriptEngine->getUnitNamed(buildingName);
	if (!theBuilding) 
	{
		return;
	}
	
	if (!theBuilding->isKindOf(KINDOF_STRUCTURE)) 
	{
		return;
	}

	AIUpdateInterface *ai = theBuilding->getAIUpdateInterface();
	if (ai) 
	{
		ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
		ai->aiEvacuate( FALSE, CMD_FROM_SCRIPT );
		return;
	}

	ContainModuleInterface *contain = theBuilding->getContain();
	if (contain) 
	{
		contain->removeAllContained( FALSE );
	}
}

//-------------------------------------------------------------------------------------------------
/** doSkyBox */
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
/** doWeather */
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
/** Freeze time */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doFreezeTime(void)
{
	TheScriptEngine->doFreezeTime();
}

//-------------------------------------------------------------------------------------------------
/** Unfreeze time */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnfreezeTime(void)
{
	TheScriptEngine->doUnfreezeTime();
}

//-------------------------------------------------------------------------------------------------
/** doSetStoppingDistance */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetStoppingDistance(const AsciiString& team, Real stoppingDistance)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( team );

	if (theTeam)
	{
		for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
		{
			Object *obj = iter.cur();

			AIUpdateInterface *aiUpdate = obj->getAIUpdateInterface();
			if (!aiUpdate || !aiUpdate->getCurLocomotor()) {
				return;
			}
			
			if (stoppingDistance >= 0.5f)
			{
				aiUpdate->getCurLocomotor()->setCloseEnoughDist(stoppingDistance);
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doTransferTeamToPlayer */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTransferTeamToPlayer(const AsciiString& teamName, const AsciiString& playerName)
{

	Team *theTeam = TheScriptEngine->getTeamNamed(teamName);
	Player* playerDest = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (!(theTeam && playerDest)) {
		return;
	}

	theTeam->setControllingPlayer(playerDest);

	// srj sez: ensure that all these guys get the upgrades that belong to the new player
	theTeam->iterateObjects(updateTeamAndPlayerStuff, nullptr);
}

//-------------------------------------------------------------------------------------------------
/** doNamedFireSpecialPowerAtArea */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSkirmishFireSpecialPowerAtMostCost( const AsciiString &player, const AsciiString& specialPower )
{
	Int enemyNdx;
	Player *enemyPlayer = TheScriptEngine->getSkirmishEnemyPlayer();
	if (enemyPlayer == nullptr) return;
	enemyNdx = enemyPlayer->getPlayerIndex();

	const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplate(specialPower);
	if (power==nullptr) 
		return;
	Real radius = 50.0f;
	if (power->getRadiusCursorRadius()>radius) {
		radius = power->getRadiusCursorRadius();
	}
	
	Player::PlayerTeamList::const_iterator it;
	
	Player *pPlayer = TheScriptEngine->getPlayerFromAsciiString(player);
	if (pPlayer==nullptr) 
		return;

	// Generals finds the target once and fires the power of the first member with the module, even if
	// it is not ready yet (scripts that fire on a timer only: GLA05 "Carpet Bomb01..05", GLA04 "China
	// Strikes Back"). Zero Hour uses ready modules only and finds the target per module.
	if( TheGlobalData->m_scriptsLikeGenerals )
	{
		Coord3D location;
		pPlayer->computeSuperweaponTarget(power, &location, enemyNdx, radius);

		for (it = pPlayer->getPlayerTeams()->begin(); it != pPlayer->getPlayerTeams()->end(); ++it) {
			for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance()) {
				Team *team = iter.cur();
				if (!team) continue;
				for (DLINK_ITERATOR<Object> iter3 = team->iterate_TeamMemberList(); !iter3.done(); iter3.advance()) {
					Object *pObj = iter3.cur();
					if (!pObj) continue;

					SpecialPowerModuleInterface *mod = pObj->getSpecialPowerModule(power);
					if (mod && !mod->isScriptOnly())	// never with a script-only module (flying aircraft)
					{
						mod->doSpecialPowerAtLocation( &location, INVALID_ANGLE, COMMAND_FIRED_BY_SCRIPT );
						break;
					}
				}
			}
		}
		return;
	}


	for (it = pPlayer->getPlayerTeams()->begin(); it != pPlayer->getPlayerTeams()->end(); ++it) 
	{
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance()) 
		{
			Team *team = iter.cur();
			if (!team) 
				continue;

			for (DLINK_ITERATOR<Object> iter2 = team->iterate_TeamMemberList(); !iter2.done(); iter2.advance()) 
			{
				Object *pObj = iter2.cur();
				if (!pObj) 
					continue;

				SpecialPowerModuleInterface *mod = pObj->getSpecialPowerModule(power);
				if (mod && !mod->isScriptOnly())	// never with a script-only module (flying aircraft)
				{
					if( !mod->isReady() )
						continue;
					
          
	        Coord3D location;
          Bool locationFound = FALSE;

					locationFound = pPlayer->computeSuperweaponTarget(power, &location, enemyNdx, radius);

					if( locationFound && power->getSpecialPowerType() == SPECIAL_SNEAK_ATTACK )
					{
						//We need to modify the location. We're already calculated the sweet spot, but we need to modify that
						//position if we can't place it in the current location.
						const ThingTemplate *sneakAttackTemplate = mod->getReferenceThingTemplate();
						if( sneakAttackTemplate )
						{
							locationFound = pPlayer->calcClosestConstructionZoneLocation( sneakAttackTemplate, &location );
						}
					}

          DEBUG_ASSERTCRASH( locationFound, ("ScriptActions::doSkirmishFireSpecialPowerAtMostCost() could not find a valid (costly) location.") );

					if( locationFound && location.lengthSqr() > 0.0f )
					{
						mod->doSpecialPowerAtLocation( &location, INVALID_ANGLE, COMMAND_FIRED_BY_SCRIPT );
					}
					break;
				}
			}
		}
	}
}

		





//-------------------------------------------------------------------------------------------------
/** doWaterChangeHeight */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doWaterChangeHeight(const AsciiString& waterName, Real newHeight)
{
	const WaterHandle *water = TheTerrainLogic->getWaterHandleByName(waterName);
	if (!water) {
		return;
	}

	TheTerrainLogic->setWaterHeight(water, newHeight, 999999.9f, TRUE );
}

//-------------------------------------------------------------------------------------------------
/** doWaterChangeHeightOverTime */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doWaterChangeHeightOverTime( const AsciiString& waterName, Real newHeight, Real time, Real damage )
{
	const WaterHandle *water = TheTerrainLogic->getWaterHandleByName( waterName );
	if( water )
	{
		TheTerrainLogic->changeWaterHeightOverTime( water, newHeight, time, damage );
	}
}

//-------------------------------------------------------------------------------------------------
/** doBorderSwitch */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doBorderSwitch(Int borderToUse)
{
	/*
	 *	The observer player always has to have the map completely revealed for him.
	 *	Border switching doesn't play nice with the permanent map reveal so for the
	 *	observer player we need to undo the old permanent reveal, switch map borders,
	 *	and re-reveal the map. BGC
	*/
	Int observerPlayerIndex = -1;
	if (ThePlayerList != nullptr)
	{
		Player *observer = ThePlayerList->findPlayerWithNameKey(TheNameKeyGenerator->nameToKey("ReplayObserver"));

		if (observer != nullptr) {
			observerPlayerIndex = observer->getPlayerIndex();
		}
	}

	if (observerPlayerIndex != -1)
	{
		ThePartitionManager->undoRevealMapForPlayerPermanently( observerPlayerIndex );
	}

	TheTerrainLogic->setActiveBoundary(borderToUse);

	if (observerPlayerIndex != -1)
	{
		ThePartitionManager->revealMapForPlayerPermanently( observerPlayerIndex );
	}
	
	ThePartitionManager->refreshShroudForLocalPlayer();
}

//-------------------------------------------------------------------------------------------------
/** doForceObjectSelection */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doForceObjectSelection(const AsciiString& teamName, const AsciiString& objectType, Bool centerInView, const AsciiString& audioToPlay)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	
	if (!team) {
		return;
	}

	Object *bestGuess = nullptr;

	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance()) {
		Object *obj = iter.cur();
		if (!obj) {
			continue;
		}
		
		if (obj->getTemplate() && obj->getTemplate()->getName() == objectType) {
			if (bestGuess == nullptr || obj->getID() < bestGuess->getID()) { // lower ID means its newer
				bestGuess = obj;				
			}
		}
	}
	
	if (!(bestGuess && bestGuess->getDrawable())) {
		return;
	}

	TheInGameUI->deselectAllDrawables();
	TheInGameUI->selectDrawable(bestGuess->getDrawable());
	// play the sound
	AudioEventRTS audioEvent(audioToPlay);
	audioEvent.setPlayerIndex(ThePlayerList->getLocalPlayer()->getPlayerIndex());
	TheLogicAudio->addAudioEvent(&audioEvent);

	if (centerInView) {
		const Coord3D pos = *bestGuess->getPosition();
		TheLogicToClient->camera( [pos]() { TheTacticalView->moveCameraTo(&pos, 0, 0, FALSE, 0.0f, 0.0f); } );
	}
}

void* __cdecl killTheObject( Object *obj, void* userObj )
{
	userObj;
	if (obj)
		obj->kill();
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
/** doForceObjectSelection */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDestroyAllContained(const AsciiString& unitName, Int damageType )
{
	Object *obj = TheScriptEngine->getUnitNamed(unitName);
	if (!obj) {
		return;
	}

	ContainModuleInterface *cmi = obj->getContain();
	if( !cmi || cmi->getContainCount() == 0 ) 
	{
		return;
	}

	cmi->iterateContained(reinterpret_cast<ContainIterateFunc>(killTheObject), nullptr, false);
}

//-------------------------------------------------------------------------------------------------
/** doModifyBuildableStatus */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doModifyBuildableStatus( const AsciiString& objectType, Int buildableStatus )
{
	const ThingTemplate *templ = TheThingFactory->findTemplate(objectType);
	if (!templ) 
	{
		return;
	}

	TheGameLogic->setBuildableStatusOverride(templ, static_cast<BuildableStatus>(buildableStatus));
}

//-------------------------------------------------------------------------------------------------
static CaveInterface* findCave(Object* obj)
{
	for (BehaviorModule** i = obj->getBehaviorModules(); *i; ++i)
	{
		CaveInterface* c = (*i)->getCaveInterface();
		if (c != nullptr)
			return c;
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
/** doSetCaveIndex */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetCaveIndex( const AsciiString& caveName, Int caveIndex )
{
	Object *obj = TheScriptEngine->getUnitNamed(caveName);
	if (!obj) 
	{
		return;
	}

	CaveInterface *caveModule = findCave(obj);
	if( caveModule == nullptr )
		return;

	caveModule->tryToSetCaveIndex( caveIndex );
}

//-------------------------------------------------------------------------------------------------
/** doSetWarehouseValue */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetWarehouseValue( const AsciiString& warehouseName, Int cashValue )
{
	Object *obj = TheScriptEngine->getUnitNamed(warehouseName);
	if (!obj) 
	{
		return;
	}

	static const NameKeyType warehouseModuleKey = TheNameKeyGenerator->nameToKey( "SupplyWarehouseDockUpdate" );
	SupplyWarehouseDockUpdate *warehouseModule = static_cast<SupplyWarehouseDockUpdate *>(obj->findUpdateModule( warehouseModuleKey ));
	if( warehouseModule == nullptr )
		return;

	warehouseModule->setCashValue( cashValue );
}

//-------------------------------------------------------------------------------------------------
/** doSetToppleDirection */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetToppleDirection( const AsciiString& unitName, const Coord3D *dir )
{
	TheScriptEngine->setToppleDirection(unitName, dir);
}

//-------------------------------------------------------------------------------------------------
/** doMoveTeamTowardsNearest */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMoveUnitTowardsNearest( const AsciiString& unitName, const AsciiString& objectType, AsciiString triggerName)
{
	Object *obj = TheScriptEngine->getUnitNamed(unitName);
	if (!obj) 
	{
		return;
	}

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) 
	{
		return;
	}
	
	PolygonTrigger *trig = TheScriptEngine->getQualifiedTriggerAreaByName(triggerName);
	if (!trig) 
	{
		return;
	}

	Object *bestObj = nullptr;

	// With Generals scripts only object types count here, no object lists; if the name is no object
	// type, the action ends (with the findTemplate message in debug builds).
	const ThingTemplate *templ = TheThingFactory->findTemplate( objectType, TheGlobalData->m_scriptsLikeGenerals );
	if( !templ && TheGlobalData->m_scriptsLikeGenerals )
	{
		return;
	}
	if( templ ) 
	{
		PartitionFilterThing thingsToAccept(templ, true);
		PartitionFilterPolygonTrigger acceptWithin(trig);
		PartitionFilterSameMapStatus filterMapStatus(obj);

		PartitionFilter *filters[] = { &thingsToAccept, &acceptWithin, &filterMapStatus, nullptr };

		bestObj = ThePartitionManager->getClosestObject( obj->getPosition(), REALLY_FAR, DistanceCalculationType::FROM_CENTER_2D, filters );
		if( !bestObj ) 
		{
			return;
		}
	}
	else
	{
		ObjectTypes *objectTypes = TheScriptEngine->getObjectTypes( objectType );
		if( objectTypes )
		{
			PartitionFilterPolygonTrigger acceptWithin( trig );
			PartitionFilterSameMapStatus filterMapStatus( obj );

			Coord3D pos = *obj->getPosition();
			Real closestDist = 0;
			Real dist;

			for( Int typeIndex = 0; static_cast<UnsignedInt>(typeIndex) < objectTypes->getListSize(); typeIndex++ )
			{
				AsciiString thisTypeName = objectTypes->getNthInList( typeIndex );
				const ThingTemplate *thisType = TheThingFactory->findTemplate( thisTypeName );
				if( thisType )
				{
					PartitionFilterThing f2( thisType, true );
					PartitionFilter *filters[] = { &f2, &acceptWithin, &filterMapStatus, 0 };

					Object *obj2 = ThePartitionManager->getClosestObject( &pos, REALLY_FAR, DistanceCalculationType::FROM_CENTER_2D, filters, &dist );
					if( obj2 )
					{
						if( !bestObj || dist < closestDist )
						{
							bestObj = obj2;
							closestDist = dist;
						}
					}
				}
			}
		}
	}
	
	ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
	ai->aiMoveToObject( bestObj, CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
/** doMoveTeamTowardsNearest */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMoveTeamTowardsNearest( const AsciiString& teamName, const AsciiString& objectType, AsciiString triggerName)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) 
	{
		return;
	}

	// Generals sends each member to the object nearest to its own position (Zero Hour: all to the
	// object nearest to the team center), knows no object lists and stops as soon as nothing is found
	// for a member (GLA02 villagers and money crates, GLA04 crate squad).
	if( TheGlobalData->m_scriptsLikeGenerals )
	{
		const ThingTemplate *templ = TheThingFactory->findTemplate(objectType);
		if (!templ) {
			return;
		}

		PolygonTrigger *trig = TheScriptEngine->getQualifiedTriggerAreaByName(triggerName);
		if (!trig) {
			return;
		}

		for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
		{
			Object *obj = iter.cur();
			if (!obj) {
				continue;
			}
			AIUpdateInterface *ai = obj->getAIUpdateInterface();
			if (!ai) {
				continue;
			}

			PartitionFilterThing thingsToAccept(templ, true);
			PartitionFilterPolygonTrigger acceptWithin(trig);
			PartitionFilterSameMapStatus filterMapStatus(obj);

			PartitionFilter *filters[] = { &thingsToAccept, &acceptWithin, &filterMapStatus, nullptr };

			Object *dest = ThePartitionManager->getClosestObject(obj->getPosition(), REALLY_FAR, DistanceCalculationType::FROM_CENTER_2D, filters);
			if (!dest) {
				return;
			}

			ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
			ai->aiMoveToObject(dest, CMD_FROM_SCRIPT);
		}
		return;
	}
	
	PolygonTrigger *trig = TheScriptEngine->getQualifiedTriggerAreaByName(triggerName);
	if (!trig) 
	{
		return;
	}

	//Get the first object (to use in the partition filter checks).
	Object *teamObj = nullptr;
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		teamObj = iter.cur();
		if( teamObj ) 
		{
			AIUpdateInterface *ai = teamObj->getAIUpdateInterface();
			if( ai )
			{
				break;
			}
		}
	}
	if( !teamObj )
	{
		return;
	}

	Coord3D teamPos = *team->getEstimateTeamPosition();
	PartitionFilterSameMapStatus filterMapStatus( teamObj );
	PartitionFilterPolygonTrigger acceptWithin( trig );
	Object *bestObj = nullptr;

	const ThingTemplate *templ = TheThingFactory->findTemplate( objectType, FALSE );
	if( templ )
	{
		//Find the closest specified template.
		PartitionFilterThing thingsToAccept( templ, true );
		PartitionFilter *filters[] = { &thingsToAccept, &acceptWithin, &filterMapStatus, nullptr };
		bestObj = ThePartitionManager->getClosestObject( &teamPos, REALLY_FAR, DistanceCalculationType::FROM_CENTER_2D, filters );
		if (!bestObj) 
		{
			return;
		}
	}
	else
	{
		//Find the closest object within the object template list.
		ObjectTypes *objectTypes = TheScriptEngine->getObjectTypes( objectType );
		if( objectTypes )
		{
			Real closestDist = 0;
			Real dist;
			for( Int typeIndex = 0; static_cast<UnsignedInt>(typeIndex) < objectTypes->getListSize(); typeIndex++ )
			{
				AsciiString thisTypeName = objectTypes->getNthInList( typeIndex );
				const ThingTemplate *thisType = TheThingFactory->findTemplate( thisTypeName );
				if( thisType )
				{
					PartitionFilterThing thingToAccept( thisType, true );
					PartitionFilter *filters[] = { &thingToAccept, &acceptWithin, &filterMapStatus, nullptr };
						
					Object *obj = ThePartitionManager->getClosestObject( &teamPos, REALLY_FAR, DistanceCalculationType::FROM_CENTER_2D, filters, &dist );
					if( obj )
					{
						if( !bestObj || dist < closestDist )
						{
							bestObj = obj;
							closestDist = dist;
						}
					}
				}
			}
		}
	}
	for( DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance() )
	{
		Object *obj = iter.cur();
		if( !obj )
		{
			return;
		}
		AIUpdateInterface *ai = obj->getAI();
		if( !ai )
		{
			return;
		}
		ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
		ai->aiMoveToObject( bestObj, CMD_FROM_SCRIPT );
	}
}

//-------------------------------------------------------------------------------------------------
/** doSkirmishAttackNearestGroupWithValue */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSkirmishAttackNearestGroupWithValue( const AsciiString& teamName, Int comparison, Int value )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroup* theGroup = TheAI->createGroup();
	team->getTeamAsAIGroup(theGroup);

	Player *player = team->getControllingPlayer();

	if (!player)
		return;

	Coord3D loc;
	Coord3D groupLoc;
	theGroup->getCenter(&groupLoc);
	if (comparison == Parameter::GREATER_EQUAL || comparison == Parameter::GREATER) {
		ThePartitionManager->getNearestGroupWithValue(player->getPlayerIndex(), ALLOW_ENEMIES, ValueOrThreat::VOT_CashValue, 
			&groupLoc, value, true, &loc);
	}

	theGroup->groupAttackMoveToPosition( &loc, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
/** doSkirmishCommandButtonOnMostValuable */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSkirmishCommandButtonOnMostValuable( const AsciiString& teamName, const AsciiString& ability, Real range, Bool allTeamMembers)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroup *theGroup = TheAI->createGroup();
	team->getTeamAsAIGroup(theGroup);

	Player *player = team->getControllingPlayer();
	if (!player)
		return;

	const CommandButton *commandButton = TheControlBar->findCommandButton( ability );
	if( !commandButton ) {
		return;
	}

	Object *srcObj = nullptr;
	if (commandButton->getSpecialPowerTemplate()) {
		srcObj = theGroup->getSpecialPowerSourceObject(commandButton->getSpecialPowerTemplate()->getID());
	} else {
		srcObj = theGroup->getCommandButtonSourceObject(commandButton->getCommandType());
	}

	if ( !srcObj ) {
		return;
	}

	Coord3D pos;
	theGroup->getCenter(&pos);

	PartitionFilterPlayerAffiliation f1(team->getControllingPlayer(), ALLOW_ENEMIES, true);
	PartitionFilterValidCommandButtonTarget f2(srcObj, commandButton, true, CMD_FROM_SCRIPT);
	PartitionFilterSameMapStatus filterMapStatus(srcObj);

	PartitionFilter *filters[] = { &f1, &f2, &filterMapStatus, 0 };
	// @todo: Should we add the group's radius to the range? Seems like a possibility.
	SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(&pos, range, DistanceCalculationType::FROM_CENTER_2D, filters, IterOrderType::ITER_SORTED_EXPENSIVE_TO_CHEAP);
	MemoryPoolObjectHolder hold(iter);

	if (iter && iter->first()) {
		theGroup->groupDoCommandButtonAtObject(commandButton, iter->first(), CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doCreateTeamFromCapturedUnits */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCreateTeamFromCapturedUnits( const AsciiString& playerName, const AsciiString& teamName )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

}

//-------------------------------------------------------------------------------------------------
// if addObject, we're adding an object to a list. If not addObject, we're removing the associated 
// object.
//-------------------------------------------------------------------------------------------------
void ScriptActions::doObjectTypeListMaintenance(const AsciiString& objectList, const AsciiString& objectType, Bool addObject)
{
	TheScriptEngine->doObjectTypeListMaintenance( objectList, objectType, addObject );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doRevealMapAtWaypointPermanent(const AsciiString& waypointName, Real radiusToReveal, const AsciiString& playerName, const AsciiString& lookName)
{
	TheScriptEngine->createNamedMapReveal(lookName, waypointName, radiusToReveal, playerName);
	TheScriptEngine->doNamedMapReveal(lookName);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doUndoRevealMapAtWaypointPermanent(const AsciiString& lookName)
{
	TheScriptEngine->undoNamedMapReveal(lookName);
	TheScriptEngine->removeNamedMapReveal(lookName);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doOverrideHulkLifetime( Real seconds )
{
	if( seconds < 0.0f )
	{
		// Turn it off.
		TheGameLogic->setHulkMaxLifetimeOverride(-1);
	}
	else
	{
		// Convert real seconds into frames.
		Int frames = static_cast<Int>(seconds * static_cast<Real>(LOGICFRAMES_PER_SECOND));
		TheGameLogic->setHulkMaxLifetimeOverride(frames);
	}
}

//-------------------------------------------------------------------------------------------------
// MBL CNC3 INCURSION - This is to Have Max camera animation playback
//
void ScriptActions::doC3CameraEnableSlaveMode
(
	const AsciiString& thingTemplateName, 
	const AsciiString& boneName 
)
{
	TheLogicToClient->camera( [thingTemplateName, boneName]() { TheTacticalView->cameraEnableSlaveMode( thingTemplateName, boneName ); } );
}

//-------------------------------------------------------------------------------------------------
// WST 11.12.2002 CNC3 INCURSION - This is for New Camera Shake
//
void ScriptActions::doC3CameraShake
( 
	const AsciiString &waypointName, 
	const Real amplitude, 
	const Real duration_seconds, 
	const Real radius 
)
{
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypointName);
	DEBUG_ASSERTLOG( (way != nullptr), ("Camera shake with No Valid Waypoint\n") );
	Coord3D pos = *way->getLocation();

	TheLogicToClient->camera( [pos, radius, duration_seconds, amplitude]() { TheTacticalView->Add_Camera_Shake(pos, radius, duration_seconds, amplitude); } );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doAffectObjectPanelFlagsUnit(const AsciiString& unitName, const AsciiString& flagName, Bool enable)
{
	Object *obj = TheScriptEngine->getUnitNamed( unitName );
	if (!obj) {
		return;
	}

	changeObjectPanelFlagForSingleObject(obj, flagName, enable);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doAffectObjectPanelFlagsTeam(const AsciiString& teamName, const AsciiString& flagName, Bool enable)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList();
	for (iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance()) {
		Object *obj = iter.cur();
		changeObjectPanelFlagForSingleObject(obj, flagName, enable);
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doGuardSupplyCenter(const AsciiString& teamName, Int supplies)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}
	Player *player = team->getControllingPlayer();
	if (!player) {
		return;
	}

	player->guardSupplyCenter(team, supplies);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doRemoveCommandBarButton(const AsciiString& buttonName, const AsciiString& objectType)
{
	const ThingTemplate *templ = TheThingFactory->findTemplate(objectType);
	if (!templ) {
		return;
	}
	const CommandSet *cs = TheControlBar->findCommandSet(templ->friend_getCommandSetString());
	if (!cs) {
		return;
	}

	Int slotNum = -1;
	for (Int i = 0; i < MAX_COMMANDS_PER_SET; ++i) 
	{
		if (cs->getCommandButton(i) && (cs->getCommandButton(i)->getName() == buttonName)) 
		{
			slotNum = i;
			break;
		}
	}
	
	if (slotNum >= 0)
	{
		TheGameLogic->setControlBarOverride(templ->friend_getCommandSetString(), slotNum, nullptr);
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doAddCommandBarButton(const AsciiString& buttonName, const AsciiString& objectType, Int slotNum)
{
	const ThingTemplate *templ = TheThingFactory->findTemplate(objectType);
	if (!templ) {
		return;
	}

	const CommandButton *commandButton = TheControlBar->findCommandButton( buttonName );
	if (commandButton == nullptr)
	{
		// not here. use doRemoveCommandBarButton to remove one.
		return;
	}

	// decrement it, so we can present it as 1-12 for the designers.
	--slotNum;

	if (slotNum < 0 || slotNum >= MAX_COMMANDS_PER_SET)
		return;

	TheGameLogic->setControlBarOverride(templ->friend_getCommandSetString(), slotNum, commandButton);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::deleteAllUnmanned()
{
	Object *obj = TheGameLogic->getFirstObject();
	while (obj) {
		if (obj->isDisabledByType(DISABLED_UNMANNED))
			TheGameLogic->destroyObject(obj);
		obj = obj->getNextObject();
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doEnableOrDisableObjectDifficultyBonuses(Bool enableBonuses)
{
	// Loops over every object in the game, applying bonuses or not.
	Object *obj = TheGameLogic->getFirstObject();
	while (obj) {
		obj->setReceivingDifficultyBonus(enableBonuses);
		obj = obj->getNextObject();
	}

	// notify the script engine of our decision
	TheScriptEngine->setObjectsShouldReceiveDifficultyBonus(enableBonuses);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doChooseVictimAlwaysUsesNormal(Bool enable)
{
	TheScriptEngine->setChooseVictimAlwaysUsesNormal(enable);
}




//-------------------------------------------------------------------------------------------------
/** Execute an action */
//-------------------------------------------------------------------------------------------------
void ScriptActions::executeAction( ScriptAction *pAction )
{
	switch (pAction->getActionType()) {
		default: 
			DEBUG_CRASH(("Unknown ScriptAction type %d", pAction->getActionType())); return;
		case ScriptAction::DEBUG_MESSAGE_BOX: 
			doDebugMessage(pAction->getParameter(0)->getString(), true);
			return;
		case ScriptAction::DEBUG_STRING: 
			doDebugMessage(pAction->getParameter(0)->getString(), false);
			return;
		case ScriptAction::DAMAGE_MEMBERS_OF_TEAM: 
			doDamageTeamMembers(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal());
			return;
		case ScriptAction::MOVE_TEAM_TO: 
			doMoveToWaypoint(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::MOVE_NAMED_UNIT_TO: 
			doNamedMoveToWaypoint(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_SET_STATE: 
			doSetTeamState(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_FOLLOW_WAYPOINTS: 
			doTeamFollowWaypoints(pAction->getParameter(0)->getString(), 
				pAction->getParameter(1)->getString(), 
				pAction->getParameter(2)->getInt());
			return;
		case ScriptAction::TEAM_FOLLOW_WAYPOINTS_EXACT: 
			doTeamFollowWaypointsExact(pAction->getParameter(0)->getString(), 
				pAction->getParameter(1)->getString(), 
				pAction->getParameter(2)->getInt());
			return;
		case ScriptAction::NAMED_FOLLOW_WAYPOINTS_EXACT: 
			doNamedFollowWaypointsExact(pAction->getParameter(0)->getString(), 
				pAction->getParameter(1)->getString());
			return;
		case ScriptAction::SKIRMISH_FOLLOW_APPROACH_PATH: 
			doTeamFollowSkirmishApproachPath(pAction->getParameter(0)->getString(), 
				pAction->getParameter(1)->getString(), 
				pAction->getParameter(2)->getInt());
			return;
		case ScriptAction::SKIRMISH_MOVE_TO_APPROACH_PATH: 
			doTeamMoveToSkirmishApproachPath(pAction->getParameter(0)->getString(), 
				pAction->getParameter(1)->getString());
			return;
		case ScriptAction::CREATE_REINFORCEMENT_TEAM: 
			doCreateReinforcements(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::SKIRMISH_BUILD_BUILDING: 
			doBuildBuilding(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::AI_PLAYER_BUILD_SUPPLY_CENTER: 
			doBuildSupplyCenter(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt());
			return;
		case ScriptAction::AI_PLAYER_BUILD_TYPE_NEAREST_TEAM:
			doBuildObjectNearestTeam( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString() );
			return;
		case ScriptAction::AI_PLAYER_BUILD_UPGRADE: 
			doBuildUpgrade(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::SKIRMISH_BUILD_BASE_DEFENSE_FRONT: 
			doBuildBaseDefense(false);
			return;
		case ScriptAction::SKIRMISH_BUILD_BASE_DEFENSE_FLANK: 
			doBuildBaseDefense(true);
			return;
		case ScriptAction::SKIRMISH_BUILD_STRUCTURE_FRONT: 
			doBuildBaseStructure(pAction->getParameter(0)->getString(), false);
			return;
		case ScriptAction::SKIRMISH_BUILD_STRUCTURE_FLANK: 
			doBuildBaseStructure(pAction->getParameter(0)->getString(), true);
			return;
		case ScriptAction::RECRUIT_TEAM: 
			doRecruitTeam(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal());
			return;
		case ScriptAction::PLAY_SOUND_EFFECT: 
			doPlaySoundEffect(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::MOVE_CAMERA_TO: 
			doMoveCameraTo(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getReal(), pAction->getParameter(4)->getReal());
			return;
		case ScriptAction::SETUP_CAMERA: 
			doSetupCamera(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getString());
			return;
		case ScriptAction::ZOOM_CAMERA: 
			doZoomCamera(pAction->getParameter(0)->getReal(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getReal());
			return;
		case ScriptAction::PITCH_CAMERA:
			doPitchCamera(pAction->getParameter(0)->getReal(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getReal());
			return;
		case ScriptAction::CAMERA_FOLLOW_NAMED:
			doCameraFollowNamed(pAction->getParameter(0)->getString(), (pAction->getParameter(1) && pAction->getParameter(1)->getInt() != 0));
			return;
		case ScriptAction::CAMERA_STOP_FOLLOW:
			doStopCameraFollowUnit();
			return;
		case ScriptAction::OVERSIZE_TERRAIN:  
			doOversizeTheTerrain(pAction->getParameter(0)->getInt());
			return;
		case ScriptAction::CAMERA_MOD_LOOK_TOWARD: 
			doModCameraLookToward(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::CAMERA_MOD_FINAL_LOOK_TOWARD: 
			doModCameraFinalLookToward(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::MOVE_CAMERA_ALONG_WAYPOINT_PATH: 
			doMoveCameraAlongWaypointPath(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getReal(), pAction->getParameter(4)->getReal());
			return;
		case ScriptAction::ROTATE_CAMERA: 
			doRotateCamera(pAction->getParameter(0)->getReal(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getReal());
			return;
		case ScriptAction::CAMERA_LOOK_TOWARD_OBJECT:
			doRotateCameraTowardObject(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getReal(), pAction->getParameter(4)->getReal());
			return;
		case ScriptAction::CAMERA_LOOK_TOWARD_WAYPOINT:
			doRotateCameraTowardWaypoint(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getReal(), pAction->getParameter(4)->getInt());
			return;
		case ScriptAction::RESET_CAMERA: 
			doResetCamera(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getReal());
			return;
		case ScriptAction::MOVE_CAMERA_TO_SELECTION: 
			doModCameraMoveToSelection();
			return;
		case ScriptAction::CAMERA_MOD_FREEZE_TIME: 
			TheLogicToClient->camera( []() { TheTacticalView->cameraModFreezeTime(); } );
			return;
		case ScriptAction::CAMERA_MOD_FREEZE_ANGLE: 
			TheLogicToClient->camera( []() { TheTacticalView->cameraModFreezeAngle(); } );
			return;
		case ScriptAction::CAMERA_MOD_SET_FINAL_ZOOM: 
		{
			const Real zoom = pAction->getParameter(0)->getReal(), easeIn = pAction->getParameter(1)->getReal(), easeOut = pAction->getParameter(2)->getReal();
			TheLogicToClient->camera( [zoom, easeIn, easeOut]() { TheTacticalView->cameraModFinalZoom(zoom, easeIn, easeOut); } );
			return;
		}
		case ScriptAction::CAMERA_MOD_SET_FINAL_PITCH: 
		{
			const Real pitch = pAction->getParameter(0)->getReal(), easeIn = pAction->getParameter(1)->getReal(), easeOut = pAction->getParameter(2)->getReal();
			TheLogicToClient->camera( [pitch, easeIn, easeOut]() { TheTacticalView->cameraModFinalPitch(pitch, easeIn, easeOut); } );
			return;
		}
		case ScriptAction::CAMERA_MOD_SET_FINAL_SPEED_MULTIPLIER: 
		{
			const Int multiplier = pAction->getParameter(0)->getInt();
			TheLogicToClient->camera( [multiplier]() { TheTacticalView->cameraModFinalTimeMultiplier(multiplier); } );
			return;
		}
		case ScriptAction::CAMERA_MOD_SET_ROLLING_AVERAGE: 
		{
			const Int frames = pAction->getParameter(0)->getInt();
			TheLogicToClient->camera( [frames]() { TheTacticalView->cameraModRollingAverage(frames); } );
			return;
		}
		case ScriptAction::SET_VISUAL_SPEED_MULTIPLIER: 
		{
			const Int multiplier = pAction->getParameter(0)->getInt();
			TheLogicToClient->camera( [multiplier]() { TheTacticalView->setTimeMultiplier(multiplier); } );
			return;
		}
		case ScriptAction::SUSPEND_BACKGROUND_SOUNDS:
			TheLogicAudio->pauseAudio(AudioAffect_Sound);
			return;
		case ScriptAction::RESUME_BACKGROUND_SOUNDS: 
			TheLogicAudio->resumeAudio(AudioAffect_Sound);
			return;
		case ScriptAction::PLAY_SOUND_EFFECT_AT: 
			doPlaySoundEffectAt(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::SET_INFANTRY_LIGHTING_OVERRIDE:
			doSetInfantryLightingOverride(pAction->getParameter(0)->getReal());
			return;
		case ScriptAction::RESET_INFANTRY_LIGHTING_OVERRIDE:
			doSetInfantryLightingOverride(-1.0f);
			return;
		case ScriptAction::QUICKVICTORY:
			doQuickVictory();
			return;
		case ScriptAction::VICTORY:
			doVictory();
			return;
		case ScriptAction::DEFEAT:
			doDefeat();
			return;
		case ScriptAction::LOCALDEFEAT:
			doLocalDefeat();
			return;
		case ScriptAction::CREATE_OBJECT:
		{
			Coord3D pos;
			pAction->getParameter(2)->getCoord3D(&pos);
			doCreateObject( m_unnamedUnit, pAction->getParameter(0)->getString(),  pAction->getParameter(1)->getString(), &pos, pAction->getParameter(3)->getReal() );
			return;
		}

		case ScriptAction::TEAM_ATTACK_TEAM:
			doAttack( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString() );
			return;
		case ScriptAction::NAMED_ATTACK_NAMED:
			doNamedAttack( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString() );
			return;
		case ScriptAction::CREATE_NAMED_ON_TEAM_AT_WAYPOINT:
			createUnitOnTeamAt( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString(), pAction->getParameter(3)->getString());
			return;
		case ScriptAction::CREATE_UNNAMED_ON_TEAM_AT_WAYPOINT:
			createUnitOnTeamAt( AsciiString::TheEmptyString, pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString());
			return;
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
		case ScriptAction::NAMED_APPLY_ATTACK_PRIORITY_SET:
			updateNamedAttackPrioritySet(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_APPLY_ATTACK_PRIORITY_SET:
			updateTeamAttackPrioritySet(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::SET_BASE_CONSTRUCTION_SPEED:
			updateBaseConstructionSpeed(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::NAMED_SET_ATTITUDE:
			updateNamedSetAttitude(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::TEAM_SET_ATTITUDE:
			updateTeamSetAttitude(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::NAMED_SET_REPULSOR:
			doNamedSetRepulsor(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::TEAM_SET_REPULSOR:
			doTeamSetRepulsor(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::NAMED_ATTACK_AREA:
			doNamedAttackArea(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::NAMED_ATTACK_TEAM:
			doNamedAttackTeam(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_ATTACK_AREA:
			doTeamAttackArea(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_ATTACK_NAMED:
			doTeamAttackNamed(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_LOAD_TRANSPORTS:
			doLoadAllTransports(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::NAMED_ENTER_NAMED:
			doNamedEnterNamed(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_ENTER_NAMED:
			doTeamEnterNamed(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::NAMED_EXIT_ALL:
			doNamedExitAll(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_EXIT_ALL:
			doTeamExitAll(pAction->getParameter(0)->getString());
			return;
    case ScriptAction::NAMED_SET_EVAC_LEFT_OR_RIGHT:
      doNamedSetGarrisonEvacDisposition(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
      return;
		case ScriptAction::NAMED_FOLLOW_WAYPOINTS:
			doNamedFollowWaypoints(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::NAMED_GUARD:
			doNamedGuard(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_GUARD:
			doTeamGuard(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_GUARD_POSITION:
			doTeamGuardPosition(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_GUARD_OBJECT:
			doTeamGuardObject(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_GUARD_AREA:
			doTeamGuardArea(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::NAMED_HUNT:
			doNamedHunt(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_HUNT:
			doTeamHunt(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_HUNT_WITH_COMMAND_BUTTON:
			doTeamHuntWithCommandButton(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::PLAYER_HUNT:
			doPlayerHunt(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::PLAYER_SELL_EVERYTHING:
			doPlayerSellEverything(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::PLAYER_DISABLE_BASE_CONSTRUCTION:
			doPlayerDisableBaseConstruction(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::PLAYER_DISABLE_FACTORIES:
			doPlayerDisableFactories(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::PLAYER_DISABLE_UNIT_CONSTRUCTION:
			doPlayerDisableUnitConstruction(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::PLAYER_ENABLE_BASE_CONSTRUCTION:
			doPlayerEnableBaseConstruction(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::PLAYER_ENABLE_FACTORIES:
			doPlayerEnableFactories(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString() );
			return;
		case ScriptAction::PLAYER_REPAIR_NAMED_STRUCTURE:
			doPlayerRepairStructure(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString() );
			return;
		case ScriptAction::PLAYER_ENABLE_UNIT_CONSTRUCTION:
			doPlayerEnableUnitConstruction(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::CAMERA_MOVE_HOME:
			doCameraMoveHome();
			return;
		case ScriptAction::BUILD_TEAM:
			doBuildTeam(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::NAMED_DAMAGE:
			doNamedDamage(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::NAMED_DELETE:
			doNamedDelete(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_DELETE:
			doTeamDelete(pAction->getParameter(0)->getString(), FALSE);
			return;
		case ScriptAction::TEAM_DELETE_LIVING:
			doTeamDelete(pAction->getParameter(0)->getString(), TRUE);
			return;
		case ScriptAction::TEAM_WANDER:
			doTeamWander(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_WANDER_IN_PLACE:
			doTeamWanderInPlace(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_INCREASE_PRIORITY:
			doTeamIncreasePriority(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_DECREASE_PRIORITY:
			doTeamDecreasePriority(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_PANIC:
			doTeamPanic(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::NAMED_KILL:
			doNamedKill(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_KILL:
			doTeamKill(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::PLAYER_KILL:
			doPlayerKill(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::DISPLAY_TEXT:
			doDisplayText(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::DISPLAY_CINEMATIC_TEXT:
			doDisplayCinematicText(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(),
				                     pAction->getParameter(2)->getInt());
			return;
		case ScriptAction::DEBUG_CRASH_BOX:
#if defined(_DEBUG)
			{
				const char* MSG = "Your Script requested the following message be displayed:\n\n";
				const char* MSG2 = "\n\nTHIS IS NOT A BUG. DO NOT REPORT IT.";
				DEBUG_CRASH(("%s%s%s\n",MSG,pAction->getParameter(0)->getString().str(),MSG2));
			}
#endif
			return;
		case ScriptAction::INGAME_POPUP_MESSAGE:
			doInGamePopupMessage(pAction->getParameter(0)->getString(),pAction->getParameter(1)->getInt(),
													pAction->getParameter(2)->getInt(), pAction->getParameter(3)->getInt(),
													pAction->getParameter(4)->getInt() );
			return;
		case ScriptAction::CAMEO_FLASH:
			doCameoFlash(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::NAMED_FLASH:
			doNamedFlash(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt(), nullptr);
			return;
		case ScriptAction::TEAM_FLASH:
			doTeamFlash(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt(), nullptr);
			return;
		case ScriptAction::NAMED_FLASH_WHITE:
			{
				RGBColor c;
				c.red = c.green = c.blue = 1.0f;
				doNamedFlash(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt(), &c);
			}
			return;
		case ScriptAction::NAMED_CUSTOM_COLOR:
			{
				doNamedCustomColor(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			}
			return;
		case ScriptAction::TEAM_FLASH_WHITE:
			{
				RGBColor c;
				c.red = c.green = c.blue = 1.0f;
				doTeamFlash(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt(), &c);
			}
			return;
		case ScriptAction::MOVIE_PLAY_FULLSCREEN:
			doMoviePlayFullScreen(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::MOVIE_PLAY_RADAR:
			doMoviePlayRadar(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::SOUND_PLAY_NAMED:
			doSoundPlayFromNamed(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::SPEECH_PLAY:
			doSpeechPlay(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::PLAYER_TRANSFER_OWNERSHIP_PLAYER:
			doPlayerTransferAssetsToPlayer(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::NAMED_TRANSFER_OWNERSHIP_PLAYER:
			doNamedTransferAssetsToPlayer(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::PLAYER_EXCLUDE_FROM_SCORE_SCREEN:
			excludePlayerFromScoreScreen(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::ENABLE_SCORING:
			enableScoring(TRUE);
			return;
		case ScriptAction::DISABLE_SCORING:
			enableScoring(FALSE);
			return;
		case ScriptAction::PLAYER_RELATES_PLAYER:
			updatePlayerRelationTowardPlayer(pAction->getParameter(0)->getString(), pAction->getParameter(2)->getInt(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::RADAR_CREATE_EVENT:
		{
			Coord3D pos;
			pAction->getParameter(0)->getCoord3D(&pos);
			doRadarCreateEvent(&pos, pAction->getParameter(1)->getInt());
			return;
		}
		case ScriptAction::OBJECT_CREATE_RADAR_EVENT:
			doObjectRadarCreateEvent(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::TEAM_CREATE_RADAR_EVENT:
			doTeamRadarCreateEvent(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::RADAR_DISABLE:
			doRadarDisable();
			return;
		case ScriptAction::RADAR_ENABLE:
			doRadarEnable();
			return;
		case ScriptAction::NAMED_SET_STEALTH_ENABLED:
			doNamedEnableStealth(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::TEAM_SET_STEALTH_ENABLED:
			doTeamEnableStealth(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::NAMED_SET_UNMANNED_STATUS:
			doNamedSetUnmanned(pAction->getParameter(0)->getString() );
			return;
		case ScriptAction::TEAM_SET_UNMANNED_STATUS:
			doTeamSetUnmanned(pAction->getParameter(0)->getString() );
			return;
		case ScriptAction::NAMED_SET_BOOBYTRAPPED:
			doNamedSetBoobytrapped( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString() );
			return;
		case ScriptAction::TEAM_SET_BOOBYTRAPPED:
			doTeamSetBoobytrapped( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString() );
			return;
		case ScriptAction::MAP_REVEAL_AT_WAYPOINT:
			doRevealMapAtWaypoint(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getString());
			return;
		case ScriptAction::MAP_SHROUD_AT_WAYPOINT:
			doShroudMapAtWaypoint(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getString());
			return;
		case ScriptAction::MAP_REVEAL_ALL:
			doRevealMapEntire(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::MAP_REVEAL_ALL_PERM:
			doRevealMapEntirePermanently(TRUE, pAction->getParameter(0)->getString());
			return;
		case ScriptAction::MAP_REVEAL_ALL_UNDO_PERM:
			doRevealMapEntirePermanently(FALSE, pAction->getParameter(0)->getString());
			return;
		case ScriptAction::MAP_SHROUD_ALL:
			doShroudMapEntire(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_AVAILABLE_FOR_RECRUITMENT:
			doTeamAvailableForRecruitment(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::TEAM_COLLECT_NEARBY_FOR_TEAM:
			doCollectNearbyForTeam(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_MERGE_INTO_TEAM:
			doMergeTeamIntoTeam(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::IDLE_ALL_UNITS:
			doIdleAllPlayerUnits(AsciiString::TheEmptyString);
			return;
		case ScriptAction::RESUME_SUPPLY_TRUCKING:
			doResumeSupplyTruckingForIdleUnits(AsciiString::TheEmptyString);
			return;
		case ScriptAction::DISABLE_INPUT:
			doDisableInput();
			return;
		case ScriptAction::ENABLE_INPUT:
			doEnableInput();
			return;
		case ScriptAction::DISABLE_BORDER_SHROUD:
			doSetBorderShroud(FALSE);
			return;
		case ScriptAction::ENABLE_BORDER_SHROUD:
			doSetBorderShroud(TRUE);
			return;
		case ScriptAction::SOUND_AMBIENT_PAUSE:
			doAmbientSoundsPause(true);
			return;
		case ScriptAction::SOUND_AMBIENT_RESUME:
			doAmbientSoundsPause(false);
			return;
		case ScriptAction::MUSIC_SET_TRACK:
			doMusicTrackChange(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt(), pAction->getParameter(2)->getInt());
			return;
		case ScriptAction::TEAM_GARRISON_SPECIFIC_BUILDING:
			doTeamGarrisonSpecificBuilding(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::EXIT_SPECIFIC_BUILDING:
			doExitSpecificBuilding(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_GARRISON_NEAREST_BUILDING:
			doTeamGarrisonNearestBuilding(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_EXIT_ALL_BUILDINGS:
			doTeamExitAllBuildings(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::NAMED_GARRISON_SPECIFIC_BUILDING:
			doUnitGarrisonSpecificBuilding(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::NAMED_GARRISON_NEAREST_BUILDING:
			doUnitGarrisonNearestBuilding(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::NAMED_EXIT_BUILDING:
			doUnitExitBuilding(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::PLAYER_GARRISON_ALL_BUILDINGS:
			doPlayerGarrisonAllBuildings(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::PLAYER_EXIT_ALL_BUILDINGS:
			doPlayerExitAllBuildings(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::CAMERA_LETTERBOX_BEGIN:
			doLetterBoxMode(true);
			return;
		case ScriptAction::CAMERA_LETTERBOX_END:
			doLetterBoxMode(false);
			return;
		case ScriptAction::CAMERA_BW_MODE_BEGIN:
			doBlackWhiteMode(true, pAction->getParameter(0) ? pAction->getParameter(0)->getInt() : 0);
			return;
		case ScriptAction::CAMERA_BW_MODE_END:
			doBlackWhiteMode(false, pAction->getParameter(0) ? pAction->getParameter(0)->getInt() : 0);
			return;
		case ScriptAction::DRAW_SKYBOX_BEGIN:
			doSkyBox(true);
			return;
		case ScriptAction::DRAW_SKYBOX_END:
			doSkyBox(false);
			return;
		case ScriptAction::SHOW_WEATHER:
			doWeather(pAction->getParameter(0)->getInt());
			return;
		case ScriptAction::CAMERA_MOTION_BLUR:
			doCameraMotionBlur(pAction->getParameter(0)->getInt(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::CAMERA_MOTION_BLUR_JUMP:
			doCameraMotionBlurJump(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::CAMERA_MOTION_BLUR_FOLLOW:
		{
			const FilterModes mode = static_cast<enum FilterModes>(FM_VIEW_MB_PAN_ALPHA+pAction->getParameter(0)->getInt());
			TheLogicToClient->camera( [mode]()
			{
				TheTacticalView->setViewFilterMode(mode);
				TheTacticalView->setViewFilter(FT_VIEW_MOTION_BLUR_FILTER);
			} );
			return;
		}
		case ScriptAction::CAMERA_MOTION_BLUR_END_FOLLOW:
			TheLogicToClient->camera( []()
			{
				TheTacticalView->setViewFilterMode(FM_VIEW_MB_END_PAN_ALPHA);
				TheTacticalView->setViewFilter(FT_VIEW_MOTION_BLUR_FILTER);
			} );
			return;
		case ScriptAction::FREEZE_TIME:
			doFreezeTime();
			return;
		case ScriptAction::UNFREEZE_TIME:
			doUnfreezeTime();
			return;
		case ScriptAction::SHOW_MILITARY_CAPTION:
			doMilitaryCaption(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::CAMERA_SET_AUDIBLE_DISTANCE:
			doCameraSetAudibleDistance(pAction->getParameter(0)->getReal());
			return;
		case ScriptAction::SET_STOPPING_DISTANCE:
			doSetStoppingDistance(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal());
			return;
		case ScriptAction::SET_FPS_LIMIT:
			if (!pAction->getParameter(0)->getInt())
			{
				TheGameEngine->setFramesPerSecondLimit(TheGlobalData->m_framesPerSecondLimit);
			}
			else
			{
				TheGameEngine->setFramesPerSecondLimit(pAction->getParameter(0)->getInt());
			}
			// Setting the fps limit doesn't do much good if we don't use it.  jba.
			TheWritableGlobalData->m_useFpsLimit = true; 
			return;
			
		case ScriptAction::DISABLE_SPECIAL_POWER_DISPLAY:
			doDisableSpecialPowerDisplay();
			return;

		case ScriptAction::ENABLE_SPECIAL_POWER_DISPLAY:
			doEnableSpecialPowerDisplay();
			return;

		case ScriptAction::NAMED_HIDE_SPECIAL_POWER_DISPLAY:
			doNamedHideSpecialPowerDisplay(pAction->getParameter(0)->getString());
			return;

		case ScriptAction::NAMED_SHOW_SPECIAL_POWER_DISPLAY:
			doNamedShowSpecialPowerDisplay(pAction->getParameter(0)->getString());
			return;

		case ScriptAction::NAMED_SET_STOPPING_DISTANCE:
			doNamedSetStoppingDistance(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal());
			return;

		case ScriptAction::NAMED_SET_HELD:
			doNamedSetHeld(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;

		case ScriptAction::MUSIC_SET_VOLUME:
			doAudioSetVolume(AudioAffect_Music, pAction->getParameter(0)->getReal());
			return;

		case ScriptAction::SOUND_SET_VOLUME:
			doAudioSetVolume(static_cast<AudioAffect>(AudioAffect_Sound | AudioAffect_Sound3D), pAction->getParameter(0)->getReal());
			return;

		case ScriptAction::SPEECH_SET_VOLUME:
			doAudioSetVolume(AudioAffect_Speech, pAction->getParameter(0)->getReal());
			return;

		case ScriptAction::TEAM_TRANSFER_TO_PLAYER:
			doTransferTeamToPlayer(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::PLAYER_SET_MONEY:
			doSetMoney(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::PLAYER_GIVE_MONEY:
			doGiveMoney(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;

		case ScriptAction::DISPLAY_COUNTDOWN_TIMER:
			doDisplayCountdownTimer(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;

		case ScriptAction::HIDE_COUNTDOWN_TIMER:
			doHideCountdownTimer(pAction->getParameter(0)->getString());
			return;

		case ScriptAction::DISPLAY_COUNTER:
			doDisplayCounter(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;

		case ScriptAction::HIDE_COUNTER:
			doHideCounter(pAction->getParameter(0)->getString());
			return;

		case ScriptAction::DISABLE_COUNTDOWN_TIMER_DISPLAY:
			doDisableCountdownTimerDisplay();
			return;

		case ScriptAction::ENABLE_COUNTDOWN_TIMER_DISPLAY:
			doEnableCountdownTimerDisplay();
			return;

		case ScriptAction::NAMED_STOP_SPECIAL_POWER_COUNTDOWN:
			doNamedStopSpecialPowerCountdown(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), TRUE);
			return;

		case ScriptAction::NAMED_START_SPECIAL_POWER_COUNTDOWN:
			doNamedStopSpecialPowerCountdown(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), FALSE);
			return;

		case ScriptAction::NAMED_SET_SPECIAL_POWER_COUNTDOWN:
			doNamedSetSpecialPowerCountdown(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt());
			return;

		case ScriptAction::NAMED_ADD_SPECIAL_POWER_COUNTDOWN:
			doNamedAddSpecialPowerCountdown(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt());
			return;

		case ScriptAction::NAMED_FIRE_SPECIAL_POWER_AT_WAYPOINT:
			doNamedFireSpecialPowerAtWaypoint(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString());
			return;

		case ScriptAction::SKIRMISH_FIRE_SPECIAL_POWER_AT_MOST_COST:
			doSkirmishFireSpecialPowerAtMostCost(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;

		case ScriptAction::NAMED_FIRE_SPECIAL_POWER_AT_NAMED:
			doNamedFireSpecialPowerAtNamed(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString());
			return;

		case ScriptAction::REFRESH_RADAR:
			doRadarRefresh();
			return;
		
		case ScriptAction::NAMED_STOP:
			doNamedStop(pAction->getParameter(0)->getString());
			return;

		case ScriptAction::TEAM_STOP:
			doTeamStop(pAction->getParameter(0)->getString(), FALSE);
			return;

		case ScriptAction::TEAM_STOP_AND_DISBAND:
			doTeamStop(pAction->getParameter(0)->getString(), TRUE);
			return;

		case ScriptAction::CAMERA_TETHER_NAMED: 
			doCameraTetherNamed(pAction->getParameter(0)->getString(), static_cast<Bool>(pAction->getParameter(1)->getInt()), pAction->getParameter(2)->getReal());
			return;

		case ScriptAction::CAMERA_STOP_TETHER_NAMED: 
			doCameraStopTetherNamed();
			return;

		case ScriptAction::CAMERA_SET_DEFAULT: 
			doCameraSetDefault(pAction->getParameter(0)->getReal(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal());
			return;

		case ScriptAction::TEAM_SET_OVERRIDE_RELATION_TO_TEAM:
			doTeamSetOverrideRelationToTeam(pAction->getParameter(0)->getString(),		// first team
									pAction->getParameter(1)->getString(),		// second team
									pAction->getParameter(2)->getInt());				// relation (ENEMIES, etc) 
			return;

		case ScriptAction::TEAM_REMOVE_OVERRIDE_RELATION_TO_TEAM:
			doTeamRemoveOverrideRelationToTeam(pAction->getParameter(0)->getString(),		// first team
									pAction->getParameter(1)->getString());		// second team
			return;

		case ScriptAction::TEAM_REMOVE_ALL_OVERRIDE_RELATIONS:
			doTeamRemoveAllOverrideRelations(pAction->getParameter(0)->getString());		// first team
			return;

		case ScriptAction::TEAM_SET_OVERRIDE_RELATION_TO_PLAYER:
			doTeamSetOverrideRelationToPlayer(pAction->getParameter(0)->getString(),		// first team
									pAction->getParameter(1)->getString(),			// second player
									pAction->getParameter(2)->getInt());				// relation (ENEMIES, etc) 
			return;

		case ScriptAction::TEAM_REMOVE_OVERRIDE_RELATION_TO_PLAYER:
			doTeamRemoveOverrideRelationToPlayer(pAction->getParameter(0)->getString(),		// first team
									pAction->getParameter(1)->getString());		// second player
			return;

		case ScriptAction::PLAYER_SET_OVERRIDE_RELATION_TO_TEAM:
			doPlayerSetOverrideRelationToTeam(pAction->getParameter(0)->getString(),		// first player
									pAction->getParameter(1)->getString(),		// second team
									pAction->getParameter(2)->getInt());				// relation (ENEMIES, etc) 
			return;

		case ScriptAction::PLAYER_REMOVE_OVERRIDE_RELATION_TO_TEAM:
			doPlayerRemoveOverrideRelationToTeam(pAction->getParameter(0)->getString(),		// first player
									pAction->getParameter(1)->getString());		// second team
			return;

		case ScriptAction::NAMED_FIRE_WEAPON_FOLLOWING_WAYPOINT_PATH:
			doNamedFireWeaponFollowingWaypointPath(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString() );
			return;

		case ScriptAction::NAMED_USE_COMMANDBUTTON_ABILITY:
			doNamedUseCommandButtonAbility( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString() );
			return;

		case ScriptAction::NAMED_USE_COMMANDBUTTON_ABILITY_ON_NAMED:
			doNamedUseCommandButtonAbilityOnNamed( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString() );
			return;

		case ScriptAction::NAMED_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT:
			doNamedUseCommandButtonAbilityAtWaypoint( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString() );
			return;
		
		case ScriptAction::NAMED_USE_COMMANDBUTTON_ABILITY_USING_WAYPOINT_PATH:
			doNamedUseCommandButtonAbilityUsingWaypointPath( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString() );
			return;
		
		case ScriptAction::TEAM_USE_COMMANDBUTTON_ABILITY:
			doTeamUseCommandButtonAbility( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString() );
			return;

		case ScriptAction::TEAM_USE_COMMANDBUTTON_ABILITY_ON_NAMED:
			doTeamUseCommandButtonAbilityOnNamed( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString() );
			return;

		case ScriptAction::TEAM_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT:
			doTeamUseCommandButtonAbilityAtWaypoint( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString() );
			return;
		
		case ScriptAction::UNIT_EXECUTE_SEQUENTIAL_SCRIPT:
			doUnitStartSequentialScript(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), 0);
			return;

		case ScriptAction::UNIT_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING:
			doUnitStartSequentialScript(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt() - 1);
			return;

		case ScriptAction::UNIT_STOP_SEQUENTIAL_SCRIPT:
			doUnitStopSequentialScript(pAction->getParameter(0)->getString());
			return;

		case ScriptAction::TEAM_EXECUTE_SEQUENTIAL_SCRIPT:
			doTeamStartSequentialScript(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), 0);
			return;

		case ScriptAction::TEAM_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING:
			doTeamStartSequentialScript(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt() - 1);
			return;

		case ScriptAction::TEAM_STOP_SEQUENTIAL_SCRIPT:
			doTeamStopSequentialScript(pAction->getParameter(0)->getString());
			return;

		case ScriptAction::UNIT_GUARD_FOR_FRAMECOUNT:
			doUnitGuardForFramecount(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;

		case ScriptAction::UNIT_IDLE_FOR_FRAMECOUNT:
			doUnitIdleForFramecount(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		
		case ScriptAction::TEAM_GUARD_FOR_FRAMECOUNT:
			doTeamIdleForFramecount(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		
		case ScriptAction::TEAM_IDLE_FOR_FRAMECOUNT:
			doTeamIdleForFramecount(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;

		case ScriptAction::WATER_CHANGE_HEIGHT:
			doWaterChangeHeight(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal());
			return;

		case ScriptAction::WATER_CHANGE_HEIGHT_OVER_TIME:
			doWaterChangeHeightOverTime( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(),
																	 pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getReal() );
			return;

		case ScriptAction::MAP_SWITCH_BORDER:
			doBorderSwitch(pAction->getParameter(0)->getInt());
			return;
			
		case ScriptAction::OBJECT_FORCE_SELECT:
			doForceObjectSelection(pAction->getParameter(0)->getString(),
														 pAction->getParameter(1)->getString(),
														 pAction->getParameter(2)->getInt(),
														 pAction->getParameter(3)->getString());
			return;
		case ScriptAction::UNIT_DESTROY_ALL_CONTAINED:
			doDestroyAllContained(pAction->getParameter(0)->getString(), 0);
			return;

		case ScriptAction::RADAR_FORCE_ENABLE:
			doRadarForceEnable();
			return;

		case ScriptAction::RADAR_REVERT_TO_NORMAL:
			doRadarRevertNormal();
			return;

		case ScriptAction::SCREEN_SHAKE:
			doScreenShake( static_cast<View::CameraShakeType>(pAction->getParameter( 0 )->getInt()) );
			return;

		case ScriptAction::TECHTREE_MODIFY_BUILDABILITY_OBJECT:
			doModifyBuildableStatus(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;


		case ScriptAction::SET_CAVE_INDEX:
			doSetCaveIndex(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;

		case ScriptAction::WAREHOUSE_SET_VALUE:
			doSetWarehouseValue(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;

		case ScriptAction::SOUND_DISABLE_TYPE:
			doSoundEnableType(pAction->getParameter(0)->getString(), false);
			return;

		case ScriptAction::SOUND_ENABLE_TYPE:
			doSoundEnableType(pAction->getParameter(0)->getString(), true);
			return;

		case ScriptAction::SOUND_ENABLE_ALL:
			doSoundEnableType(AsciiString::TheEmptyString, true);
			return;
			
		case ScriptAction::SOUND_REMOVE_ALL_DISABLED:
			doSoundRemoveAllDisabled();
			return;

		case ScriptAction::SOUND_REMOVE_TYPE:
			doSoundRemoveType(pAction->getParameter(0)->getString());
			return;

		case ScriptAction::AUDIO_OVERRIDE_VOLUME_TYPE:
			doSoundOverrideVolume(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal());
			return;
		case ScriptAction::AUDIO_RESTORE_VOLUME_TYPE:
			doSoundOverrideVolume(pAction->getParameter(0)->getString(), -100.0);
			return;
		case ScriptAction::AUDIO_RESTORE_VOLUME_ALL_TYPE:
			doSoundOverrideVolume(AsciiString::TheEmptyString, -100.0);
			return;

		case ScriptAction::NAMED_SET_TOPPLE_DIRECTION:
		{
			Coord3D dir;
			pAction->getParameter(1)->getCoord3D(&dir);

			doSetToppleDirection(pAction->getParameter(0)->getString(), &dir);
			return;
		}

		case ScriptAction::UNIT_MOVE_TOWARDS_NEAREST_OBJECT_TYPE:
			doMoveUnitTowardsNearest(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString());
			return;

		case ScriptAction::TEAM_MOVE_TOWARDS_NEAREST_OBJECT_TYPE:
			doMoveTeamTowardsNearest(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString());
			return;

		case ScriptAction::NAMED_RECEIVE_UPGRADE:
			doUnitReceiveUpgrade(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;

		case ScriptAction::SKIRMISH_ATTACK_NEAREST_GROUP_WITH_VALUE:
			doSkirmishAttackNearestGroupWithValue(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt(), pAction->getParameter(2)->getInt());
			return;

		case ScriptAction::SKIRMISH_PERFORM_COMMANDBUTTON_ON_MOST_VALUABLE_OBJECT:
			doSkirmishCommandButtonOnMostValuable(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getInt());
			return;

		case ScriptAction::SKIRMISH_WAIT_FOR_COMMANDBUTTON_AVAILABLE_ALL:
			// We should never get here.
			DEBUG_CRASH(("\"[Skirmish] Wait for command button available - all\" should never be used outside of Sequential scripts. - jkmcd"));
			return;

		case ScriptAction::SKIRMISH_WAIT_FOR_COMMANDBUTTON_AVAILABLE_PARTIAL:
			// We should never get here.
			DEBUG_CRASH(("\"[Skirmish] Wait for command button available - partial\" should never be used outside of Sequential scripts. - jkmcd"));
			return;

		case ScriptAction::TEAM_WAIT_FOR_NOT_CONTAINED_ALL:
			// We should never get here.
			DEBUG_CRASH(("\"[Team] Wait for team no longer contained - all\" should never be used outside of Sequential scripts. - jkmcd"));
			return;

		case ScriptAction::TEAM_WAIT_FOR_NOT_CONTAINED_PARTIAL:
			// We should never get here.
			DEBUG_CRASH(("\"[Team] Wait for team no longer contained - partial\" should never be used outside of Sequential scripts. - jkmcd"));
			return;

		case ScriptAction::TEAM_SPIN_FOR_FRAMECOUNT:
			doTeamSpinForFramecount( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt() );
			return;

		case ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NAMED:
			doTeamUseCommandButtonOnNamed(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString());
			return;
		case ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_ENEMY_UNIT:
			doTeamUseCommandButtonOnNearestEnemy(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_GARRISONED_BUILDING:
			doTeamUseCommandButtonOnNearestGarrisonedBuilding(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_KINDOF:
			doTeamUseCommandButtonOnNearestKindof(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt());
			return;
		case ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_ENEMY_BUILDING:
			doTeamUseCommandButtonOnNearestBuilding(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_ENEMY_BUILDING_CLASS:
			doTeamUseCommandButtonOnNearestBuildingClass(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt());
			return;
		case ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_OBJECTTYPE:
			doTeamUseCommandButtonOnNearestObjectType(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString());
			return;
		case ScriptAction::TEAM_PARTIAL_USE_COMMANDBUTTON:
			doTeamPartialUseCommandButton(pAction->getParameter(0)->getReal(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString());
			return;
		case ScriptAction::TEAM_CAPTURE_NEAREST_UNOWNED_FACTION_UNIT:
			doTeamCaptureNearestUnownedFactionUnit(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::PLAYER_CREATE_TEAM_FROM_CAPTURED_UNITS:
			doCreateTeamFromCapturedUnits(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::PLAYER_ADD_SKILLPOINTS:
			doPlayerAddSkillPoints(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::PLAYER_ADD_RANKLEVEL:
			doPlayerAddRankLevels(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::PLAYER_SET_RANKLEVEL:
			doPlayerSetRankLevel(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::PLAYER_SET_RANKLEVELLIMIT:
			doMapSetRankLevelLimit(pAction->getParameter(0)->getInt());
			return;
		case ScriptAction::PLAYER_GRANT_SCIENCE:
			doPlayerGrantScience(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::PLAYER_PURCHASE_SCIENCE:
			doPlayerPurchaseScience(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::PLAYER_SCIENCE_AVAILABILITY:
			doPlayerSetScienceAvailability(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString());
			return;
		case ScriptAction::TEAM_SET_EMOTICON:
			doTeamEmoticon( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getReal() );
			return;
		case ScriptAction::NAMED_SET_EMOTICON:
			doNamedEmoticon( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getReal() );
			return;
		case ScriptAction::OBJECTLIST_ADDOBJECTTYPE:
			doObjectTypeListMaintenance(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), TRUE);
			return;
		case ScriptAction::OBJECTLIST_REMOVEOBJECTTYPE:
			doObjectTypeListMaintenance(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), FALSE);
			return;
		case ScriptAction::MAP_REVEAL_PERMANENTLY_AT_WAYPOINT:
			doRevealMapAtWaypointPermanent(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getString(), pAction->getParameter(3)->getString());
			return;
		case ScriptAction::MAP_UNDO_REVEAL_PERMANENTLY_AT_WAYPOINT:
			doUndoRevealMapAtWaypointPermanent(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::EVA_SET_ENABLED_DISABLED:
			doEvaEnabledDisabled(pAction->getParameter(0)->getInt());
			return;
		case ScriptAction::OPTIONS_SET_OCCLUSION_MODE:
			doSetOcclusionMode(pAction->getParameter(0)->getInt());
			return;
		case ScriptAction::OPTIONS_SET_DRAWICON_UI_MODE:
			doSetDrawIconUIMode(pAction->getParameter(0)->getInt());
			return;
		case ScriptAction::CAMERA_ENABLE_SLAVE_MODE:
			{
				doC3CameraEnableSlaveMode
				(
					pAction->getParameter(0)->getString(),
					pAction->getParameter(1)->getString()
				);
			}
			return;
		case ScriptAction::CAMERA_DISABLE_SLAVE_MODE:
			{
				doC3CameraDisableSlaveMode();
			}
			return;
		case ScriptAction::CAMERA_ADD_SHAKER_AT: // WST 11.12.2002 (MBL)
			{
				doC3CameraShake
				(
					pAction->getParameter(0)->getString(),	// Waypoint name
					pAction->getParameter(1)->getReal(),	// Amplitude
					pAction->getParameter(2)->getReal(),	// Duration in seconds
					pAction->getParameter(3)->getReal()		// Radius
				);
			}
			return;
		case ScriptAction::OPTIONS_SET_PARTICLE_CAP_MODE:
			doSetDynamicLODMode(pAction->getParameter(0)->getInt());
			return;

		case ScriptAction::SCRIPTING_OVERRIDE_HULK_LIFETIME:
			doOverrideHulkLifetime( pAction->getParameter( 0 )->getReal() );
			return;

		case ScriptAction::NAMED_FACE_NAMED:
			doNamedFaceNamed( pAction->getParameter( 0 )->getString(), pAction->getParameter( 1 )->getString() );
			return;

		case ScriptAction::NAMED_FACE_WAYPOINT:
			doNamedFaceWaypoint( pAction->getParameter( 0 )->getString(), pAction->getParameter( 1 )->getString() );
			return;

		case ScriptAction::TEAM_FACE_NAMED:
			doTeamFaceNamed( pAction->getParameter( 0 )->getString(), pAction->getParameter( 1 )->getString() );
			return;

		case ScriptAction::TEAM_FACE_WAYPOINT:
			doTeamFaceWaypoint( pAction->getParameter( 0 )->getString(), pAction->getParameter( 1 )->getString() );
			return;
			
		case ScriptAction::UNIT_AFFECT_OBJECT_PANEL_FLAGS:
			doAffectObjectPanelFlagsUnit(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt());
			return;

		case ScriptAction::TEAM_AFFECT_OBJECT_PANEL_FLAGS:
			doAffectObjectPanelFlagsTeam(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt());
			return;

		case ScriptAction::PLAYER_SELECT_SKILLSET:
			doAffectPlayerSkillset(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;

		case ScriptAction::COMMANDBAR_REMOVE_BUTTON_OBJECTTYPE:
			doRemoveCommandBarButton(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;

		case ScriptAction::COMMANDBAR_ADD_BUTTON_OBJECTTYPE_SLOT:
			doAddCommandBarButton(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt());
			return;

		case ScriptAction::UNIT_SPAWN_NAMED_LOCATION_ORIENTATION:
		{
			Coord3D pos;
			pAction->getParameter(3)->getCoord3D(&pos);
			doCreateObject( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString(), &pos, pAction->getParameter(4)->getReal() );
			return;
		}
		
		case ScriptAction::PLAYER_AFFECT_RECEIVING_EXPERIENCE:
			doAffectSkillPointsModifier(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal());
			return;

		case ScriptAction::TEAM_GUARD_SUPPLY_CENTER:
			doGuardSupplyCenter(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;

		case ScriptAction::OBJECT_ALLOW_BONUSES:
			doEnableOrDisableObjectDifficultyBonuses(pAction->getParameter(0)->getInt());
			return;

		case ScriptAction::TEAM_GUARD_IN_TUNNEL_NETWORK:
			doTeamGuardInTunnelNetwork(pAction->getParameter(0)->getString());
			return;
			
		case ScriptAction::RESIZE_VIEW_GUARDBAND:
			doResizeViewGuardband( pAction->getParameter(0)->getReal(), pAction->getParameter(1)->getReal() );
			return;

		case ScriptAction::DELETE_ALL_UNMANNED:
			deleteAllUnmanned();
			return;

		case ScriptAction::CHOOSE_VICTIM_ALWAYS_USES_NORMAL:
			doChooseVictimAlwaysUsesNormal(pAction->getParameter(0)->getInt());
			return;

		case ScriptAction::SET_TRAIN_HELD:
			doNamedSetTrainHeld( pAction->getParameter( 0 )->getString(), static_cast<Bool>(pAction->getParameter( 1 )->getInt()) );
			return;

  	case ScriptAction::ENABLE_OBJECT_SOUND:
 			doEnableObjectSound(pAction->getParameter(0)->getString(), true);
			return;

		case ScriptAction::DISABLE_OBJECT_SOUND:
			doEnableObjectSound(pAction->getParameter(0)->getString(), false);
			return;			

	}  
}
