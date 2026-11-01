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

// ScriptActions: orders to teams

//-------------------------------------------------------------------------------------------------
/** doTeamSetRepulsor */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamSetRepulsor(const AsciiString& teamName, Bool repulsor)
{
	Team *theSrcTeam = TheScriptEngine->getTeamNamed(teamName);
	if (!theSrcTeam) {
		return;
	}

	if (theSrcTeam) 
	{
		for (DLINK_ITERATOR<Object> iter = theSrcTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
		{
			Object *obj = iter.cur();
			if (!obj) 
			{
				continue;
			}
			obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_REPULSOR ), repulsor );
		}
	}

}

//-------------------------------------------------------------------------------------------------
/** doTeamAttackArea */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAttackArea(const AsciiString& teamName, const AsciiString& areaName)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if (!theTeam) {
		return;
	}

	AIGroup* theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}

	theTeam->getTeamAsAIGroup(theGroup);

	PolygonTrigger *pTrig = TheScriptEngine->getQualifiedTriggerAreaByName(areaName);
	if (!pTrig) {
		return;
	}

	theGroup->groupAttackArea(pTrig, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamAttackNamed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAttackNamed(const AsciiString& teamName, const AsciiString& unitName)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if (!theTeam) {
		return;
	}

	Object *theVictim = TheScriptEngine->getUnitNamed( unitName );
	if (!theVictim) {
		return;
	}

	AIGroup* theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}

	theTeam->getTeamAsAIGroup(theGroup);
	theGroup->groupAttackObject(theVictim, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamEnterNamed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamEnterNamed(const AsciiString& teamName, const AsciiString& unitDestName)
{
	Team *theSrcTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theSrcTeam) {
		return;
	}
	
	Object *theTransport = TheScriptEngine->getUnitNamed( unitDestName );
	if (!theTransport) {
		return;
	}

	AIGroup* theGroup = TheAI->createGroup();
	theSrcTeam->getTeamAsAIGroup(theGroup);

	theGroup->groupEnter(theTransport, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamExitAll */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamExitAll(const AsciiString& teamName)
{
	Team *theTeamOfTransports = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeamOfTransports) {
		return;
	}

	AIGroup* theGroup = TheAI->createGroup();
	theTeamOfTransports->getTeamAsAIGroup(theGroup);

	theGroup->groupEvacuate( CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
/** doTeamFollowSkirmishApproachPath */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamFollowSkirmishApproachPath(const AsciiString& teamName, const AsciiString& waypointPathLabel, Bool asTeam)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}

	AIGroup* theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
	theTeam->getTeamAsAIGroup(theGroup);
	Int count = 0;
	Coord3D pos;
	pos.x=pos.y=pos.z=0;

	Object *firstUnit=nullptr;
	// Get the center point for the team
	for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		Coord3D objPos = *obj->getPosition();
		pos.x += objPos.x;
		pos.y += objPos.y;
		pos.z += objPos.z; // Not actually used by getClosestWaypointOnPath, but hey, might as well be correct.
		count++;
		if (firstUnit==nullptr) {
			firstUnit = obj;
		}
	}
	if (count==0) return; // empty team.
	pos.x /= count;
	pos.y /= count;
	pos.z /= count;

	Player *enemyPlayer = TheScriptEngine->getSkirmishEnemyPlayer();
	if (enemyPlayer==nullptr) return;
	Int mpNdx = enemyPlayer->getMpStartIndex()+1;

	AsciiString pathLabel;
	pathLabel.format("%s%d", waypointPathLabel.str(), mpNdx);
	Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &pos, pathLabel );
	if (!way) {
		return;
	}

	Player *aiPlayer = TheScriptEngine->getCurrentPlayer();
	if (aiPlayer && firstUnit) {
		aiPlayer->checkBridges(firstUnit, way);
	}

	DEBUG_ASSERTLOG(TheTerrainLogic->isPurposeOfPath(way, pathLabel), ("***Wrong waypoint purpose. Make jba fix this.\n"));
	if (asTeam) 
	{
		theGroup->groupFollowWaypointPathAsTeam(way, CMD_FROM_SCRIPT);
	}	else {
		theGroup->groupFollowWaypointPath(way, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamFollowSkirmishApproachPath */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamMoveToSkirmishApproachPath(const AsciiString& teamName, const AsciiString& waypointPathLabel)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}

	AIGroup* theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
	theTeam->getTeamAsAIGroup(theGroup);
	Int count = 0;
	Coord3D pos;
	pos.x=pos.y=pos.z=0;

	// Get the center point for the team
	for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		Coord3D objPos = *obj->getPosition();
		pos.x += objPos.x;
		pos.y += objPos.y;
		pos.z += objPos.z; // Not actually used by getClosestWaypointOnPath, but hey, might as well be correct.
		count++;
	}
	if (count==0) return; // empty team.
	pos.x /= count;
	pos.y /= count;
	pos.z /= count;

	Player *enemyPlayer = TheScriptEngine->getSkirmishEnemyPlayer();
	if (enemyPlayer==nullptr) return;
	Int mpNdx = enemyPlayer->getMpStartIndex()+1;

	AsciiString pathLabel;
	pathLabel.format("%s%d", waypointPathLabel.str(), mpNdx);
	Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &pos, pathLabel );
	if (!way) {
		return;
	}
	DEBUG_ASSERTLOG(TheTerrainLogic->isPurposeOfPath(way, pathLabel), ("***Wrong waypoint purpose. Make jba fix this.\n"));
	theGroup->groupMoveToPosition(way->getLocation(), false, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamFollowWaypoints */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamFollowWaypoints(const AsciiString& teamName, const AsciiString& waypointPathLabel, Bool asTeam)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}

	AIGroup* theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
	theTeam->getTeamAsAIGroup(theGroup);
	Int count = 0;
	Coord3D pos;
	pos.x=pos.y=pos.z=0;

	// Get the center point for the team
	for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		Coord3D objPos = *obj->getPosition();
		pos.x += objPos.x;
		pos.y += objPos.y;
		pos.z += objPos.z; // Not actually used by getClosestWaypointOnPath, but hey, might as well be correct.
		count++;
	}
	if (count==0) return; // empty team.
	pos.x /= count;
	pos.y /= count;
	pos.z /= count;

	Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &pos, waypointPathLabel );
	if (!way) {
		return;
	}
	DEBUG_ASSERTLOG(TheTerrainLogic->isPurposeOfPath(way, waypointPathLabel), ("***Wrong waypoint purpose. Make jba fix this.\n"));
	if (asTeam) 
	{
		theGroup->groupFollowWaypointPathAsTeam(way, CMD_FROM_SCRIPT);
	}	else {
		theGroup->groupFollowWaypointPath(way, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamFollowWaypointsExact */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamFollowWaypointsExact(const AsciiString& teamName, const AsciiString& waypointPathLabel, Bool asTeam)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}

	AIGroup* theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
	theTeam->getTeamAsAIGroup(theGroup);
	Int count = 0;
	Coord3D pos;
	pos.x=pos.y=pos.z=0;

	// Get the center point for the team
	for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		Coord3D objPos = *obj->getPosition();
		pos.x += objPos.x;
		pos.y += objPos.y;
		pos.z += objPos.z; // Not actually used by getClosestWaypointOnPath, but hey, might as well be correct.
		count++;
	}
	if (count==0) return; // empty team.
	pos.x /= count;
	pos.y /= count;
	pos.z /= count;

	Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &pos, waypointPathLabel );
	if (!way) {
		return;
	}
	DEBUG_ASSERTLOG(TheTerrainLogic->isPurposeOfPath(way, waypointPathLabel), ("***Wrong waypoint purpose. Make jba fix this.\n"));
	if (asTeam) 
	{
		theGroup->groupFollowWaypointPathAsTeamExact(way, CMD_FROM_SCRIPT);
	}	else {
		theGroup->groupFollowWaypointPathExact(way, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamGuard */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamGuard(const AsciiString& teamName)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}

	// Have all the members of the team guard at their current pos.
	for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai) {
			continue;
		}
		Coord3D pos = *obj->getPosition();
		ai->aiGuardPosition(&pos, GUARDMODE_NORMAL, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamGuardPosition */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamGuardPosition(const AsciiString& teamName, const AsciiString& waypointName)
{
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypointName); 
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam || !way) {
		return;
	}

	AIGroup* theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
	theTeam->getTeamAsAIGroup(theGroup);
	Coord3D position = *way->getLocation();

	theGroup->groupGuardPosition( &position, GUARDMODE_NORMAL, CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
/** doTeamGuardObject */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamGuardObject(const AsciiString& teamName, const AsciiString& unitName)
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam || !theUnit) {
		return;
	}

	AIGroup* theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
	theTeam->getTeamAsAIGroup(theGroup);

	theGroup->groupGuardObject( theUnit, GUARDMODE_NORMAL, CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
/** doTeamGuardArea */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamGuardArea(const AsciiString& teamName, const AsciiString& areaName)
{
	PolygonTrigger *pTrig = TheScriptEngine->getQualifiedTriggerAreaByName(areaName);
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam || !pTrig) {
		return;
	}

	AIGroup* theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
	theTeam->getTeamAsAIGroup(theGroup);

	theGroup->groupGuardArea( pTrig, GUARDMODE_NORMAL, CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
/** doTeamHunt */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamHunt(const AsciiString& teamName)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}

	AIGroup* theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
	theTeam->getTeamAsAIGroup(theGroup);

	theGroup->groupHunt( CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
/** doTeamHunt */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamHuntWithCommandButton(const AsciiString& teamName, const AsciiString& ability)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}


	const CommandButton *commandButton = TheControlBar->findCommandButton( ability );
	if( !commandButton )
	{
		return;
	}

		switch( commandButton->getCommandType() )
		{

			case GUI_COMMAND_SPECIAL_POWER:
				if( commandButton->getSpecialPowerTemplate() )
				{
					if (BitTest( commandButton->getOptions(), COMMAND_OPTION_NEED_OBJECT_TARGET )) 
					{
						// OK, we can hunt with a power that targets an object.
						break;
					}
					AsciiString msg = "ERROR-Team hunt with command button - cannot hunt with ability ";
					msg.concat(ability);
					TheScriptEngine->AppendDebugMessage(msg, false);
					return;
				}
				return;
			case GUI_COMMAND_SWITCH_WEAPON:
			case GUI_COMMAND_FIRE_WEAPON:
				{
					// ok, we can hunt with a weapon.
					break;
				}
			
			case GUICOMMANDMODE_HIJACK_VEHICLE:
			case GUICOMMANDMODE_CONVERT_TO_CARBOMB:
			case GUICOMMANDMODE_SABOTAGE_BUILDING:
				//Various enter type hunts.
				// With Generals scripts, units do not hunt with enter abilities (CHI05 "Command_GLAInfantryHijack");
				// same message as in the default case below.
				if( TheGlobalData->m_scriptsLikeGenerals )
				{
					AsciiString msg = "ERROR-Team hunt with command button - cannot hunt with ability ";
					msg.concat(ability);
					TheScriptEngine->AppendDebugMessage(msg, false);
					return;
				}
				break;

			case GUI_COMMAND_OBJECT_UPGRADE:
			case GUI_COMMAND_PLAYER_UPGRADE:
			case GUI_COMMAND_DOZER_CONSTRUCT:
			case GUI_COMMAND_DOZER_CONSTRUCT_CANCEL:
			case GUI_COMMAND_UNIT_BUILD:
			case GUI_COMMAND_CANCEL_UNIT_BUILD:
			case GUI_COMMAND_CANCEL_UPGRADE:
			case GUI_COMMAND_ATTACK_MOVE:
			case GUI_COMMAND_GUARD:
			case GUI_COMMAND_GUARD_WITHOUT_PURSUIT:
			case GUI_COMMAND_GUARD_FLYING_UNITS_ONLY:
			case GUI_COMMAND_WAYPOINTS:
			case GUI_COMMAND_EXIT_CONTAINER:
			case GUI_COMMAND_EVACUATE:
			case GUI_COMMAND_EXECUTE_RAILED_TRANSPORT:
			case GUI_COMMAND_BEACON_DELETE:
			case GUI_COMMAND_SET_RALLY_POINT:
			case GUI_COMMAND_SELL:
			case GUI_COMMAND_HACK_INTERNET:
			case GUI_COMMAND_TOGGLE_OVERCHARGE:
			default:
				{
					AsciiString msg = "ERROR-Team hunt with command button - cannot hunt with ability ";
					msg.concat(ability);
					TheScriptEngine->AppendDebugMessage(msg, false);
					return;
				}
				break;
		}


	// Have all the members of the team do the command button.
	for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai) {
			continue;
		}
		Bool foundCommand = false;
		const CommandSet *commandSet = TheControlBar->findCommandSet( obj->getCommandSetString( ) );
		if( commandSet )
		{
			for( int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
			{
				const CommandButton *aCommandButton = commandSet->getCommandButton(i);
				if( commandButton == aCommandButton )
				{
					//We found the matching command button so now order the unit to do what the button wants.
					foundCommand = true;
					break;
				}
			}
		}
		if (!foundCommand) {
			AsciiString msg = "Error - Team hunt with command button - unit type '";
			msg.concat(obj->getTemplate()->getName().str());
			msg.concat("' is not valid for ability ");
			msg.concat(ability);
			TheScriptEngine->AppendDebugMessage(msg, false);
			continue;

		}

		switch( commandButton->getCommandType() )
		{

			case GUI_COMMAND_FIRE_WEAPON:
			case GUI_COMMAND_SWITCH_WEAPON:
			case GUI_COMMAND_SPECIAL_POWER:
			case GUICOMMANDMODE_HIJACK_VEHICLE:
			case GUICOMMANDMODE_CONVERT_TO_CARBOMB:
			case GUICOMMANDMODE_SABOTAGE_BUILDING:
			{
					static NameKeyType key_CommandButtonHuntUpdate = NAMEKEY("CommandButtonHuntUpdate");

					CommandButtonHuntUpdate* huntUpdate = static_cast<CommandButtonHuntUpdate*>(obj->findUpdateModule(key_CommandButtonHuntUpdate));
					if( huntUpdate  )
					{
						huntUpdate->setCommandButton(ability);
					} else {
						AsciiString msg = "Error - Team hunt with command button - unit type '";
						msg.concat(obj->getTemplate()->getName().str());
						msg.concat("' requires CommandButtonHuntUpdate in .ini definition to hunt with ");
						msg.concat(ability);
						TheScriptEngine->AppendDebugMessage(msg, false);
					}  // end if
				}
				break;
				
		}
			
	}

}

//-------------------------------------------------------------------------------------------------
/** doTeamDelete */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamDelete(const AsciiString& teamName, Bool ignoreDead)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);

	if (!team) {
		return;
	}

	team->deleteTeam(ignoreDead);
}

//-------------------------------------------------------------------------------------------------
/** doTeamWander */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamWander(const AsciiString& teamName, const AsciiString& waypointPathLabel)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);

	if (!team) {
		return;
	}

	// Have all the members of the team wander.
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai) {
			continue;
		}
		Coord3D pos = *obj->getPosition();
		Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &pos, waypointPathLabel );
		if (!way) {
			return;
		}
		ai->chooseLocomotorSet(LOCOMOTORSET_WANDER);
		ai->aiWander(way, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamIncreasePriority */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamIncreasePriority(const AsciiString& teamName)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);

	if (!team) {
		return;
	}
	const TeamPrototype *theTeamProto = team->getPrototype();

	if (!theTeamProto) {
		return;
	}
	theTeamProto->increaseAIPriorityForSuccess();
	AsciiString msg;
	msg.format("Team '%s' priority increased to %d for success.", teamName.str(), theTeamProto->getTemplateInfo()->m_productionPriority);
	TheScriptEngine->AppendDebugMessage(msg, false);

}

//-------------------------------------------------------------------------------------------------
/** doTeamDecreasePriority */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamDecreasePriority(const AsciiString& teamName)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);

	if (!team) {
		return;
	}
	const TeamPrototype *theTeamProto = team->getPrototype();

	if (!theTeamProto) {
		return;
	}
	theTeamProto->decreaseAIPriorityForFailure();
	AsciiString msg;
	msg.format("Team '%s' priority decreased to %d for failure.", teamName.str(), theTeamProto->getTemplateInfo()->m_productionPriority);
	TheScriptEngine->AppendDebugMessage(msg, false);

}

//-------------------------------------------------------------------------------------------------
/** doTeamWanderInPlace */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamWanderInPlace(const AsciiString& teamName)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);

	if (!team) {
		return;
	}

	// Have all the members of the team wander.
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai) {
			continue;
		}
		ai->chooseLocomotorSet(LOCOMOTORSET_WANDER);
		ai->aiWanderInPlace(CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamPanic */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamPanic(const AsciiString& teamName, const AsciiString& waypointPathLabel)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);

	if (!team) {
		return;
	}

	// Get the center point for the team
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai) {
			continue;
		}
		Coord3D pos = *obj->getPosition();
		Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &pos, waypointPathLabel );
		if (!way) {
			return;
		}
		ai->chooseLocomotorSet(LOCOMOTORSET_PANIC);
		ai->aiPanic(way, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamKill */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamKill(const AsciiString& teamName)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);

	if (!team) {
		return;
	}

	team->killTeam();
}

//-------------------------------------------------------------------------------------------------
/** doTeamAvailableForRecruitment */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAvailableForRecruitment(const AsciiString& teamName, Bool availability)
{
	Team *theTeam = TheScriptEngine->getTeamNamed(teamName);
	if (!theTeam) {
		return;
	}

	theTeam->setRecruitable(availability);
}

//-------------------------------------------------------------------------------------------------
/** doTeamGarrisonSpecificBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamGarrisonSpecificBuilding(const AsciiString& teamName, const AsciiString& buildingName)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}

	Object *theBuilding = TheScriptEngine->getUnitNamed(buildingName);
	if (!theBuilding) {
		return;
	}

	if( !theBuilding->getContain() )
	{
		DEBUG_CRASH( ("doTeamGarrisonSpecificBuilding script -- building doesn't have a container!" ) );
		return;
	}
	PlayerMaskType player = theBuilding->getContain()->getPlayerWhoEntered();

	if (!(theBuilding->isKindOf(KINDOF_STRUCTURE) && 
		(player == 0) || (player == theTeam->getControllingPlayer()->getPlayerMask()))) {
		return;
	}
	
	AIGroup* theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
	
	theTeam->getTeamAsAIGroup(theGroup);
	theGroup->groupEnter(theBuilding, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamGarrisonNearestBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamGarrisonNearestBuilding(const AsciiString& teamName)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}
	
	DLINK_ITERATOR<Object> diter = theTeam->iterate_TeamMemberList();
	Object *leader = diter.cur();
	if (!leader) {
		return;
	}


	PartitionFilter *filters[16];
	Int count = 0;

	PartitionFilterAcceptByKindOf f1( MAKE_KINDOF_MASK( KINDOF_FS_INTERNET_CENTER ), KINDOFMASK_NONE );
	PartitionFilterGarrisonableByPlayer f2(theTeam->getControllingPlayer(), true, CMD_FROM_SCRIPT);

	if( leader->isKindOf( KINDOF_MONEY_HACKER ) )
	{
		//If the leader is a hacker, then look for an internet center instead of a normal building!
		filters[ count++ ] = &f1;
	}
	else
	{
		//If the leader ISN'T a hacker, then look for standard fare garrisonable buildings (internet centers won't show up)!
		filters[ count++ ] = &f2;
	}

	PartitionFilterSameMapStatus filterMapStatus(leader);
	filters[ count++ ] = &filterMapStatus;

	filters[count++] = nullptr;

	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(leader, REALLY_FAR, DistanceCalculationType::FROM_CENTER_3D, filters, IterOrderType::ITER_SORTED_NEAR_TO_FAR);
	MemoryPoolObjectHolder hold(iter);

	
	// here's what we do. Find out how many slots each building has open, and tell each unit individually to 
	// garrison a specific building. We won't use the partition solver because we've already done most of the work
	
	for (Object *theBuilding = iter->first(); theBuilding; theBuilding = iter->next()) {
		ContainModuleInterface *cmi = theBuilding->getContain();
		if (!cmi) {
			continue;
		}

		Int slotsAvailable = cmi->getContainMax() - cmi->getContainCount();
		for (int i = 0; i < slotsAvailable; ) {
			Object *obj = diter.cur();
			if (diter.done() || !obj) {
				return;
			}

			AIUpdateInterface *ai = obj->getAIUpdateInterface();
			if (ai && obj->isKindOf(KINDOF_INFANTRY) && !obj->isKindOf(KINDOF_NO_GARRISON)) {
				ai->aiEnter(theBuilding, CMD_FROM_SCRIPT);
				++i;
			}
			diter.advance();
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamExitAllBuildings */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamExitAllBuildings(const AsciiString& teamName)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}
		
	for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance()) {
		Object *obj = iter.cur();
		if (!obj) {
			continue;
		}

		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai) {
			continue;
		}

		ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
		ai->aiExit(nullptr, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamEnableStealth */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamEnableStealth( const AsciiString& teamName, Bool enabled )
{
	Team *team = TheScriptEngine->getTeamNamed( teamName );
	if( !team ) 
	{
		return;
	}
		
	for( DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance() )
	{
		Object *obj = iter.cur();
		if( obj ) 
		{
			obj->setScriptStatus( OBJECT_STATUS_SCRIPT_UNSTEALTHED, !enabled );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamSetUnmanned */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamSetUnmanned( const AsciiString& teamName )
{
	Team *team = TheScriptEngine->getTeamNamed( teamName );
	if( !team ) 
	{
		return;
	}
		
	for( DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); )
	{
		Object *obj = iter.cur();

		//Advance the iterator NOW because when we unman the object, the team gets nuked, and 
		//the iterator stops after the first iteration!
		iter.advance();
		//*************

		if( obj ) 
		{
			obj->setDisabled( DISABLED_UNMANNED );
			TheGameLogic->deselectObject( obj, PLAYERMASK_ALL, TRUE );
			// Convert it to the neutral team so it renders gray giving visual representation that it is unmanned.
			obj->setTeam( ThePlayerList->getNeutralPlayer()->getDefaultTeam() );
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamSetBoobytrapped( const AsciiString& thingTemplateName, const AsciiString& teamName )
{
	Team *team = TheScriptEngine->getTeamNamed( teamName );
	if( !team ) 
	{
		return;
	}
		
	for( DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance() )
	{
		Object *obj = iter.cur();

		const ThingTemplate *thing = TheThingFactory->findTemplate( thingTemplateName );
		if( thing )
		{
			Object *boobytrap = TheThingFactory->newObject( thing, obj->getTeam() );
			if( boobytrap )
			{
				static NameKeyType key_StickyBombUpdate = NAMEKEY( "StickyBombUpdate" );
				StickyBombUpdate *update = static_cast<StickyBombUpdate*>(boobytrap->findUpdateModule( key_StickyBombUpdate ));
				if( update )
				{
					//The charge gets positioned randomly on the outside of the perimeter of the victim.
					Coord3D pos;
					obj->getGeometryInfo().makeRandomOffsetOnPerimeter( pos );

					//Get the angle and transform matrix from the obj... then transform the calculated
					//position 
					const Matrix3D *transform = obj->getTransformMatrix();
					transform->Transform_Vector( *transform, *reinterpret_cast<Vector3*>(&pos), reinterpret_cast<Vector3*>(&pos) );

					update->initStickyBomb( obj, nullptr, &pos );
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonAbility( const AsciiString& team, const AsciiString& ability )
{
	Team *theTeam = TheScriptEngine->getTeamNamed( team );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if( !theTeam ) 
	{
		return;
	}

	const CommandButton *commandButton = TheControlBar->findCommandButton( ability );
	if( !commandButton )
	{
		return;
	}


	AIGroup* theGroup = TheAI->createGroup();
	if( !theGroup ) 
	{
		return;
	}

	theTeam->getTeamAsAIGroup( theGroup );
	
	theGroup->groupDoCommandButton( commandButton, CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonAbilityOnNamed( const AsciiString& team, const AsciiString& ability, const AsciiString& target )
{
	Team *theTeam = TheScriptEngine->getTeamNamed( team );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if( !theTeam ) 
	{
		return;
	}

	Object *theObj = TheScriptEngine->getUnitNamed( target );
	if( !theObj )
	{
		return;
	}

	const CommandButton *commandButton = TheControlBar->findCommandButton( ability );
	if( !commandButton )
	{
		return;
	}


	AIGroup* theGroup = TheAI->createGroup();
	if( !theGroup ) 
	{
		return;
	}

	theTeam->getTeamAsAIGroup( theGroup );
	
	theGroup->groupDoCommandButtonAtObject( commandButton, theObj, CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonAbilityAtWaypoint( const AsciiString& team, const AsciiString& ability, const AsciiString& waypoint )
{
	Team *theTeam = TheScriptEngine->getTeamNamed( team );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if( !theTeam ) 
	{
		return;
	}

	Waypoint *pWaypoint = TheTerrainLogic->getWaypointByName( waypoint );
	if( !pWaypoint )
	{
		return;
	}

	const CommandButton *commandButton = TheControlBar->findCommandButton( ability );
	if( !commandButton )
	{
		return;
	}


	AIGroup* theGroup = TheAI->createGroup();
	if( !theGroup ) 
	{
		return;
	}

	theTeam->getTeamAsAIGroup( theGroup );
	
	theGroup->groupDoCommandButtonAtPosition( commandButton, pWaypoint->getLocation(), CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
/** doTeamStop */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamStop(const AsciiString& teamName, Bool shouldDisband)
{
	Team *theTeam = TheScriptEngine->getTeamNamed(teamName);
	if (!theTeam) {
		return;
	}

	AIGroup* theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}

	theTeam->getTeamAsAIGroup(theGroup);
	theGroup->groupIdle(CMD_FROM_SCRIPT);

	if (shouldDisband) {
		Team *playerDefaultTeam = theTeam->getControllingPlayer()->getDefaultTeam();
		
		for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance()) {
			Object *obj = iter.cur();

			AIUpdateInterface *ai = obj->getAIUpdateInterface();
			if (!ai) {
				continue;
			}

			ai->setIsRecruitable(TRUE);
		}

		doMergeTeamIntoTeam(teamName, playerDefaultTeam->getName());
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamSetOverrideRelationToTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamSetOverrideRelationToTeam(const AsciiString& teamName, const AsciiString& otherTeam, Int relation)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	Team *theOtherTeam = TheScriptEngine->getTeamNamed( otherTeam );
	if (theTeam && theOtherTeam) {
		theTeam->setOverrideTeamRelationship(theOtherTeam->getID(), static_cast<Relationship>(relation));
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamRemoveOverrideRelationToTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamRemoveOverrideRelationToTeam(const AsciiString& teamName, const AsciiString& otherTeam)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	Team *theOtherTeam = TheScriptEngine->getTeamNamed( otherTeam );
	if (theTeam && theOtherTeam) {
		theTeam->removeOverrideTeamRelationship(theOtherTeam->getID());
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamSetOverrideRelationToPlayer */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamSetOverrideRelationToPlayer(const AsciiString& teamName, const AsciiString& otherPlayer, Int relation)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	Player *theOtherPlayer = ThePlayerList->findPlayerWithNameKey(NAMEKEY(otherPlayer));
	if (theTeam && theOtherPlayer) {
		theTeam->setOverridePlayerRelationship(theOtherPlayer->getPlayerIndex(), static_cast<Relationship>(relation));
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamRemoveOverrideRelationToTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamRemoveOverrideRelationToPlayer(const AsciiString& teamName, const AsciiString& otherPlayer)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	Player *theOtherPlayer = ThePlayerList->findPlayerWithNameKey(NAMEKEY(otherPlayer));
	if (theTeam && theOtherPlayer) {
		theTeam->removeOverridePlayerRelationship(theOtherPlayer->getPlayerIndex());
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamRemoveAllOverrideRelations */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamRemoveAllOverrideRelations(const AsciiString& teamName)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (theTeam) {
		// invalid ID is OK -- it removes all relationships
		theTeam->removeOverrideTeamRelationship( NULL );
		theTeam->removeOverridePlayerRelationship( NULL );
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamStartSequentialScript */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamStartSequentialScript(const AsciiString& teamName, const AsciiString& scriptName, Int loopVal)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	Script *script = const_cast<Script*>(TheScriptEngine->findScriptByName(scriptName));
	if (!script) {
		return;
	}

	// Idle the team so the seq script will start executing. jba.
	AIGroup* theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}

	team->getTeamAsAIGroup(theGroup);
	theGroup->groupIdle(CMD_FROM_SCRIPT);


	SequentialScript* seqScript = newInstance(SequentialScript);
	seqScript->m_teamToExecOn = team;
	seqScript->m_scriptToExecuteSequentially = script;
	seqScript->m_timesToLoop = loopVal;
	
	TheScriptEngine->appendSequentialScript(seqScript);

	deleteInstanceOf(seqScript);
}

//-------------------------------------------------------------------------------------------------
/** doTeamStopSequentialScript */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamStopSequentialScript(const AsciiString& teamName)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	TheScriptEngine->removeAllSequentialScripts(team);
}

//-------------------------------------------------------------------------------------------------
/** doTeamGuardForFramecount */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamGuardForFramecount(const AsciiString& teamName, Int framecount)
{
	Team *theTeam = TheScriptEngine->getTeamNamed(teamName);
	if (!theTeam) {
		return;
	}

	// Have all the members of the team guard at their current pos.
	for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai) {
			continue;
		}
		Coord3D pos = *obj->getPosition();
		ai->aiGuardPosition(&pos, GUARDMODE_NORMAL, CMD_FROM_SCRIPT);
	}
	TheScriptEngine->setSequentialTimer(theTeam, framecount);
}

//-------------------------------------------------------------------------------------------------
/** doTeamIdleForFramecount */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamIdleForFramecount(const AsciiString& teamName, Int framecount)
{
	Team *theTeam = TheScriptEngine->getTeamNamed(teamName);
	if (!theTeam) {
		return;
	}

	AIGroup* theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}

	theTeam->getTeamAsAIGroup(theGroup);
	Coord3D center;
	theGroup->getCenter(&center);

	theGroup->groupIdle(CMD_FROM_SCRIPT);
	TheScriptEngine->setSequentialTimer(theTeam, framecount);
}

//-------------------------------------------------------------------------------------------------
/** doTeamSpinForFramecount */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamSpinForFramecount( const AsciiString& teamName, Int waitForFrames )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	TheScriptEngine->setSequentialTimer(team, waitForFrames);
}

//-------------------------------------------------------------------------------------------------
/** doTeamUseCommandButtonOnNamed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonOnNamed( const AsciiString& teamName, const AsciiString& commandAbility, const AsciiString& unitName )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroup *theGroup = TheAI->createGroup();
	team->getTeamAsAIGroup(theGroup);

	const CommandButton *commandButton = TheControlBar->findCommandButton(commandAbility);
	if(!commandButton) {
		return;
	}

	Object *srcObj = nullptr;
	if (commandButton->getSpecialPowerTemplate()) {
		srcObj = theGroup->getSpecialPowerSourceObject(commandButton->getSpecialPowerTemplate()->getID());
	} else {
		srcObj = theGroup->getCommandButtonSourceObject(commandButton->getCommandType());
	}

	if (!srcObj) {
		return;
	}

	Object *obj = TheScriptEngine->getUnitNamed( unitName );
	if (!obj) {
		return;
	}

	if (commandButton->isValidToUseOn(srcObj, obj, nullptr, CMD_FROM_SCRIPT)) {
		theGroup->groupDoCommandButtonAtObject(commandButton, obj, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamUseCommandButtonOnNearestEnemy */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonOnNearestEnemy( const AsciiString& teamName, const AsciiString& commandAbility )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroup *theGroup = TheAI->createGroup();
	team->getTeamAsAIGroup(theGroup);

	const CommandButton *commandButton = TheControlBar->findCommandButton(commandAbility);
	if(!commandButton) {
		return;
	}

	Object *srcObj = nullptr;
	if (commandButton->getSpecialPowerTemplate()) {
		srcObj = theGroup->getSpecialPowerSourceObject(commandButton->getSpecialPowerTemplate()->getID());
	} else {
		srcObj = theGroup->getCommandButtonSourceObject(commandButton->getCommandType());
	}

	if (!srcObj) {
		return;
	}

	PartitionFilterPlayerAffiliation f1(team->getControllingPlayer(), ALLOW_ENEMIES, true);
	PartitionFilterValidCommandButtonTarget f2(srcObj, commandButton, true, CMD_FROM_SCRIPT);
	PartitionFilterSameMapStatus filterMapStatus(srcObj);

	Coord3D pos;
	theGroup->getCenter(&pos);

	PartitionFilter *filters[] = { &f1, &f2, &filterMapStatus, 0 };
	Object *obj = ThePartitionManager->getClosestObject(&pos, REALLY_FAR, DistanceCalculationType::FROM_CENTER_2D, filters);
	if (!obj) {
		return;
	}

	// already been checked for validity
	theGroup->groupDoCommandButtonAtObject(commandButton, obj, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamUseCommandButtonOnNearestGarrisonedBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonOnNearestGarrisonedBuilding( const AsciiString& teamName, const AsciiString& commandAbility )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroup *theGroup = TheAI->createGroup();
	team->getTeamAsAIGroup(theGroup);

	const CommandButton *commandButton = TheControlBar->findCommandButton(commandAbility);
	if(!commandButton) {
		return;
	}

	Object *srcObj = nullptr;
	if (commandButton->getSpecialPowerTemplate()) {
		srcObj = theGroup->getSpecialPowerSourceObject(commandButton->getSpecialPowerTemplate()->getID());
	} else {
		srcObj = theGroup->getCommandButtonSourceObject(commandButton->getCommandType());
	}

	if (!srcObj) {
		return;
	}

	PartitionFilterPlayerAffiliation f1(team->getControllingPlayer(), ALLOW_ENEMIES, true);
	PartitionFilterAcceptByKindOf f2(MAKE_KINDOF_MASK(KINDOF_STRUCTURE), KINDOFMASK_NONE);
	PartitionFilterGarrisonable f3(true);
	PartitionFilterValidCommandButtonTarget f4(srcObj, commandButton, true, CMD_FROM_SCRIPT);
	PartitionFilterSameMapStatus filterMapStatus(srcObj);

	Coord3D pos;
	theGroup->getCenter(&pos);

	PartitionFilter *filters[] = { &f1, &f2, &f3, &f4, &filterMapStatus, 0 };
	Object *obj = ThePartitionManager->getClosestObject(&pos, REALLY_FAR, DistanceCalculationType::FROM_CENTER_2D, filters);
	if (!obj) {
		return;
	}

	// already been checked for validity
	theGroup->groupDoCommandButtonAtObject(commandButton, obj, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamUseCommandButtonOnNearestKindof */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonOnNearestKindof( const AsciiString& teamName, const AsciiString& commandAbility, Int kindofBit )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroup *theGroup = TheAI->createGroup();
	team->getTeamAsAIGroup(theGroup);

	const CommandButton *commandButton = TheControlBar->findCommandButton(commandAbility);
	if (!commandButton) {
		return;
	}

	Object *srcObj = nullptr;
	if (commandButton->getSpecialPowerTemplate()) {
		srcObj = theGroup->getSpecialPowerSourceObject(commandButton->getSpecialPowerTemplate()->getID());
	} else {
		srcObj = theGroup->getCommandButtonSourceObject(commandButton->getCommandType());
	}

	if (!srcObj) {
		return;
	}

	PartitionFilterPlayerAffiliation f1(team->getControllingPlayer(), ALLOW_ENEMIES, true);
	PartitionFilterAcceptByKindOf f2(MAKE_KINDOF_MASK(kindofBit), KINDOFMASK_NONE);
	PartitionFilterValidCommandButtonTarget f3(srcObj, commandButton, true, CMD_FROM_SCRIPT);
	PartitionFilterSameMapStatus filterMapStatus(srcObj);

	Coord3D pos;
	theGroup->getCenter(&pos);

	PartitionFilter *filters[] = { &f1, &f2, &f3, &filterMapStatus, 0 };
	Object *obj = ThePartitionManager->getClosestObject(&pos, REALLY_FAR, DistanceCalculationType::FROM_CENTER_2D, filters);
	if (!obj) {
		return;
	}

	// already been checked for validity
	theGroup->groupDoCommandButtonAtObject(commandButton, obj, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamUseCommandButtonOnNearestBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonOnNearestBuilding( const AsciiString& teamName, const AsciiString& commandAbility )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroup *theGroup = TheAI->createGroup();
	team->getTeamAsAIGroup(theGroup);

	const CommandButton *commandButton = TheControlBar->findCommandButton(commandAbility);
	if (!commandButton) {
		return;
	}

	Object *srcObj = nullptr;
	if (commandButton->getSpecialPowerTemplate()) {
		srcObj = theGroup->getSpecialPowerSourceObject(commandButton->getSpecialPowerTemplate()->getID());
	} else {
		srcObj = theGroup->getCommandButtonSourceObject(commandButton->getCommandType());
	}

	if (!srcObj) {
		return;
	}

	PartitionFilterPlayerAffiliation f1(team->getControllingPlayer(), ALLOW_ENEMIES, true);
	PartitionFilterAcceptByKindOf f2(MAKE_KINDOF_MASK(KINDOF_STRUCTURE), KINDOFMASK_NONE);
	PartitionFilterValidCommandButtonTarget f3(srcObj, commandButton, true, CMD_FROM_SCRIPT);
	PartitionFilterSameMapStatus filterMapStatus(srcObj);

	Coord3D pos;
	theGroup->getCenter(&pos);

	PartitionFilter *filters[] = { &f1, &f2, &f3, &filterMapStatus, 0 };
	Object *obj = ThePartitionManager->getClosestObject(&pos, REALLY_FAR, DistanceCalculationType::FROM_CENTER_2D, filters);
	if (!obj) {
		return;
	}

	// already been checked for validity
	theGroup->groupDoCommandButtonAtObject(commandButton, obj, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamUseCommandButtonOnNearestBuildingClass */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonOnNearestBuildingClass( const AsciiString& teamName, const AsciiString& commandAbility, Int kindofBit )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroup *theGroup = TheAI->createGroup();
	team->getTeamAsAIGroup(theGroup);

	const CommandButton *commandButton = TheControlBar->findCommandButton(commandAbility);
	if (!commandButton) {
		return;
	}

	Object *srcObj = nullptr;
	if (commandButton->getSpecialPowerTemplate()) {
		srcObj = theGroup->getSpecialPowerSourceObject(commandButton->getSpecialPowerTemplate()->getID());
	} else {
		srcObj = theGroup->getCommandButtonSourceObject(commandButton->getCommandType());
	}

	if (!srcObj) {
		return;
	}

	PartitionFilterPlayerAffiliation f1(team->getControllingPlayer(), ALLOW_ENEMIES, true);
	PartitionFilterAcceptByKindOf f2(MAKE_KINDOF_MASK(KINDOF_STRUCTURE), KINDOFMASK_NONE);
	PartitionFilterAcceptByKindOf f3(MAKE_KINDOF_MASK(kindofBit), KINDOFMASK_NONE);
	PartitionFilterValidCommandButtonTarget f4(srcObj, commandButton, true, CMD_FROM_SCRIPT);
	PartitionFilterSameMapStatus filterMapStatus(srcObj);

	Coord3D pos;
	theGroup->getCenter(&pos);

	PartitionFilter *filters[] = { &f1, &f2, &f3, &f4, &filterMapStatus, 0 };
	Object *obj = ThePartitionManager->getClosestObject(&pos, REALLY_FAR, DistanceCalculationType::FROM_CENTER_2D, filters);
	if (!obj) {
		return;
	}

	// already been checked for validity
	theGroup->groupDoCommandButtonAtObject(commandButton, obj, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamUseCommandButtonOnNearestObjectType */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonOnNearestObjectType( const AsciiString& teamName, const AsciiString& commandAbility, const AsciiString& objectType )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroup *theGroup = TheAI->createGroup();
	team->getTeamAsAIGroup(theGroup);

	const CommandButton *commandButton = TheControlBar->findCommandButton(commandAbility);
	if (!commandButton) {
		return;
	}

	Object *srcObj = nullptr;
	if (commandButton->getSpecialPowerTemplate()) {
		srcObj = theGroup->getSpecialPowerSourceObject(commandButton->getSpecialPowerTemplate()->getID());
	} else {
		srcObj = theGroup->getCommandButtonSourceObject(commandButton->getCommandType());
	}

	if (!srcObj) {
		return;
	}

	Object *bestObj = nullptr;

	//First look for a specific object type (object lists will fail)
	// With Generals scripts only object types count here, no object lists (in USA08 "Power List",
	// "Factory List" and "Bombables List" have no effect); if the name is no object type, the action
	// ends (with the findTemplate message in debug builds).
	const ThingTemplate *thingTemplate = TheThingFactory->findTemplate( objectType, TheGlobalData->m_scriptsLikeGenerals );
	if( !thingTemplate && TheGlobalData->m_scriptsLikeGenerals )
	{
		return;
	}
	if( thingTemplate ) 
	{
		PartitionFilterPlayerAffiliation f1(team->getControllingPlayer(), ALLOW_ENEMIES | ALLOW_NEUTRAL, true);
		PartitionFilterThing f2(thingTemplate, true);
		PartitionFilterValidCommandButtonTarget f3(srcObj, commandButton, true, CMD_FROM_SCRIPT);
		PartitionFilterSameMapStatus filterMapStatus(srcObj);
		PartitionFilter *filters[] = { &f1, &f2, &f3, &filterMapStatus, 0 };

		Coord3D pos;
		theGroup->getCenter(&pos);

		bestObj = ThePartitionManager->getClosestObject(&pos, REALLY_FAR, DistanceCalculationType::FROM_CENTER_2D, filters);
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
			PartitionFilterPlayerAffiliation f1(team->getControllingPlayer(), ALLOW_ENEMIES | ALLOW_NEUTRAL, true);
			PartitionFilterValidCommandButtonTarget f3(srcObj, commandButton, true, CMD_FROM_SCRIPT);
			PartitionFilterSameMapStatus f4(srcObj);

			Coord3D pos;
			theGroup->getCenter(&pos);
			Real closestDist = 0;
			Real dist;

			for( Int typeIndex = 0; static_cast<UnsignedInt>(typeIndex) < objectTypes->getListSize(); typeIndex++ )
			{
				AsciiString thisTypeName = objectTypes->getNthInList( typeIndex );
				const ThingTemplate *thisType = TheThingFactory->findTemplate( thisTypeName );
				if( thisType )
				{
					PartitionFilterThing f2( thisType, true );
					PartitionFilter *filters[] = { &f1, &f2, &f3, &f4, 0 };

					Object *obj = ThePartitionManager->getClosestObject(&pos, REALLY_FAR, DistanceCalculationType::FROM_CENTER_2D, filters, &dist );
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

	// already been checked for validity
	theGroup->groupDoCommandButtonAtObject(commandButton, bestObj, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamPartialUseCommandButton */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamPartialUseCommandButton( Real percentage, const AsciiString& teamName, const AsciiString& commandAbility )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	const CommandButton *commandButton = TheControlBar->findCommandButton(commandAbility);
	if (!commandButton) {
		return;
	}

	std::vector<Object *> objList;
	DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList();

	for (iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance()) {
		Object *obj = iter.cur();
		if (commandButton->isValidToUseOn(obj, nullptr, nullptr, CMD_FROM_SCRIPT)) {	
			objList.push_back(obj);
		}
	}
	
	Int numObjs = /*REAL_TO_INT_CEIL*/(static_cast<Int>(percentage / 100.0f * objList.size()));
	// Generals rounds up (10 % of three drones is one there, none here).
	if( TheGlobalData->m_scriptsLikeGenerals )
	{
		numObjs = REAL_TO_INT_CEIL(percentage / 100.0f * objList.size());
	}
	Int count = 0;
	for (std::vector<Object*>::const_iterator it = objList.begin(); it != objList.end(); ++it)
	{
		Object *obj = (*it);

		if (count >= numObjs) 
			return;

		obj->doCommandButton(commandButton, CMD_FROM_SCRIPT);
		++count;
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamCaptureNearestUnownedFactionUnit */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamCaptureNearestUnownedFactionUnit( const AsciiString& teamName )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroup *theGroup = TheAI->createGroup();
	team->getTeamAsAIGroup(theGroup);

	PartitionFilterPlayerAffiliation f1(team->getControllingPlayer(), ALLOW_ENEMIES | ALLOW_NEUTRAL, true);
	PartitionFilterUnmannedObject f2(true);
	PartitionFilterOnMap filterMapStatus;

	Coord3D pos;
	theGroup->getCenter(&pos);

	PartitionFilter *filters[] = { &f1, &f2, &filterMapStatus, 0 };
	Object *obj = ThePartitionManager->getClosestObject(&pos, REALLY_FAR, DistanceCalculationType::FROM_CENTER_2D, filters);
	if (!obj) {
		return;
	}

	theGroup->groupEnter(obj, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamFaceNamed( const AsciiString &teamName, const AsciiString &faceUnitName )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if( team )
	{
		Object *faceObj = TheScriptEngine->getUnitNamed( faceUnitName );
		if( faceObj )
		{
			DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList();
			for( iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance() )
			{
				Object *obj = iter.cur();
				if( obj )
				{
					AIUpdateInterface *ai = obj->getAI();
					if( ai )
					{
						ai->clearWaypointQueue();
						obj->leaveGroup();
						ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
						ai->aiFaceObject( faceObj, CMD_FROM_SCRIPT );
					}
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamFaceWaypoint( const AsciiString &teamName, const AsciiString &faceWaypointName )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if( team )
	{
		Waypoint *way = TheTerrainLogic->getWaypointByName( faceWaypointName );
		if( way )
		{
			DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList();
			for( iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance() )
			{
				Object *obj = iter.cur();
				if( obj )
				{
					AIUpdateInterface *ai = obj->getAI();
					if( ai )
					{
						ai->clearWaypointQueue();
						obj->leaveGroup();
						ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
						ai->aiFacePosition( way->getLocation(), CMD_FROM_SCRIPT );
					}
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamGuardInTunnelNetwork(const AsciiString& teamName)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}
	// Have all the members of the team guard at a tunnel network.
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai) {
			continue;
		}
		ai->aiGuardTunnelNetwork(GUARDMODE_NORMAL, CMD_FROM_SCRIPT);
	}
}
