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

// ScriptActions: orders to named units

//-------------------------------------------------------------------------------------------------
/** doNamedMoveToWaypoint */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedMoveToWaypoint(const AsciiString& unit, const AsciiString& waypoint)
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	if (theObj) 
	{
		Waypoint *way = TheTerrainLogic->getWaypointByName(waypoint);
		if (!way) {
			return;
		}

		Coord3D destination = *way->getLocation();

		AIUpdateInterface *aiUpdate = theObj->getAIUpdateInterface();
		if (!aiUpdate) {
			return;
		}
		
		aiUpdate->clearWaypointQueue();
		theObj->leaveGroup();
		aiUpdate->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
		aiUpdate->aiMoveToPosition( &destination, CMD_FROM_SCRIPT );

	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedAttack */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedAttack(const AsciiString& attackerName, const AsciiString& victimName)
{
	/// @todo Implement me (MSB)

	Object *attackingObj = TheScriptEngine->getUnitNamed( attackerName );
	Object *victimObj = TheScriptEngine->getUnitNamed( victimName );

	if (!attackingObj || !victimObj) {
		return;
	}
	// tell every member of attacking team to attack a random member of victim team

	{
		AIUpdateInterface *aiUpdate = attackingObj->getAIUpdateInterface();

		if (aiUpdate)
		{
			/// @todo Teams should have a method that returns the number of members in the team (MSB)
			attackingObj->leaveGroup();
			aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
			aiUpdate->aiForceAttackObject( victimObj, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedSetRepulsor */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedSetRepulsor(const AsciiString& unitName, Bool repulsor)
{
	Object *theSrcUnit = TheScriptEngine->getUnitNamed(unitName);
	if (!theSrcUnit) {
		return;
	}
	theSrcUnit->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_REPULSOR ), repulsor);
}

//-------------------------------------------------------------------------------------------------
/** doNamedAttackArea */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedAttackArea(const AsciiString& unitName, const AsciiString& areaName)
{
	Object *theSrcUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theSrcUnit) {
		return;
	}
	
	PolygonTrigger *pTrig = TheScriptEngine->getQualifiedTriggerAreaByName(areaName);
	if (!pTrig) {
		return;
	}


	AIUpdateInterface* aiUpdate = theSrcUnit->getAIUpdateInterface();
	if( !aiUpdate )
	{
		return;
	}
	theSrcUnit->leaveGroup();
	aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	aiUpdate->aiAttackArea(pTrig, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doNamedAttackTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedAttackTeam(const AsciiString& unitName, const AsciiString& teamName)
{
	Object *theSrcUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theSrcUnit) {
		return;
	}
	
	const Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}

	AIUpdateInterface* aiUpdate = theSrcUnit->getAIUpdateInterface();
	if (!aiUpdate) {
		return;
	}

	theSrcUnit->leaveGroup();
	aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	aiUpdate->aiAttackTeam(theTeam, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doNamedEnterNamed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedEnterNamed(const AsciiString& unitSrcName, const AsciiString& unitDestName)
{
	Object *theSrcUnit = TheScriptEngine->getUnitNamed( unitSrcName );
	if (!theSrcUnit) {
		return;
	}
	
	Object *theTransport = TheScriptEngine->getUnitNamed( unitDestName );
	if (!theTransport) {
		return;
	}

	AIUpdateInterface* aiUpdate = theSrcUnit->getAIUpdateInterface();
	if (!aiUpdate) {
		return;
	}
	theSrcUnit->leaveGroup();
	aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	aiUpdate->aiEnter(theTransport, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doNamedExitAll */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedExitAll(const AsciiString& unitName)
{
	Object *theTransport = TheScriptEngine->getUnitNamed( unitName );
	if (!theTransport) {
		return;
	}

	AIUpdateInterface* aiUpdate = theTransport->getAIUpdateInterface();
	if (!aiUpdate) {
		return;
	}

	theTransport->leaveGroup();
	aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	aiUpdate->aiEvacuate( FALSE, CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
/** doNamedSetGarrisonEvacDisposition */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedSetGarrisonEvacDisposition(const AsciiString& unitName, UnsignedInt disp )
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theUnit) {
		return;
	}

	ContainModuleInterface *contain = theUnit->getContain();
  if( contain )
		contain->setEvacDisposition( static_cast<EvacDisposition>(disp) ); 
    // should be safe to cast any-old int to this enum, 
    // since only 1(EVAC_TO_LEFT) and 2(EVAC_TO_RIGHT) differ from default case

}

//-------------------------------------------------------------------------------------------------
/** doNamedFollowWaypoints */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedFollowWaypoints(const AsciiString& unitName, const AsciiString& waypointPathLabel)
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theUnit) {
		return;
	}
	Coord3D pos = *theUnit->getPosition();
	AIUpdateInterface* aiUpdate = theUnit->getAIUpdateInterface();
	if (!aiUpdate) {
		return;
	}

	Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &pos, waypointPathLabel );
	if (!way) {
		return;
	}

	DEBUG_ASSERTLOG(TheTerrainLogic->isPurposeOfPath(way, waypointPathLabel), ("***Wrong waypoint purpose. Make jba fix this.\n"));
	
	theUnit->leaveGroup();
	aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	aiUpdate->aiFollowWaypointPath(way, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doNamedFollowWaypointsExact */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedFollowWaypointsExact(const AsciiString& unitName, const AsciiString& waypointPathLabel)
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theUnit) {
		return;
	}
	Coord3D pos = *theUnit->getPosition();
	AIUpdateInterface* aiUpdate = theUnit->getAIUpdateInterface();
	if (!aiUpdate) {
		return;
	}

	Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &pos, waypointPathLabel );
	if (!way) {
		return;
	}

	DEBUG_ASSERTLOG(TheTerrainLogic->isPurposeOfPath(way, waypointPathLabel), ("***Wrong waypoint purpose. Make jba fix this.\n"));
	
	theUnit->leaveGroup();
	aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	aiUpdate->aiFollowWaypointPathExact(way, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doNamedGuard */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedGuard(const AsciiString& unitName)
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theUnit) {
		return;
	}

	AIUpdateInterface* aiUpdate = theUnit->getAIUpdateInterface();
	if (!aiUpdate) {
		return;
	}
	
	theUnit->leaveGroup();
	Coord3D position = *theUnit->getPosition();
	aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	aiUpdate->aiGuardPosition(&position, GUARDMODE_NORMAL, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doNamedHunt */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedHunt(const AsciiString& unitName)
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theUnit) {
		return;
	}

	AIUpdateInterface* aiUpdate = theUnit->getAIUpdateInterface();
	if (!aiUpdate) {
		return;
	}

	aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	aiUpdate->aiHunt( CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
/** doNamedDamage */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedDamage(const AsciiString& unitName, Int damageAmt)
{
	Object *pUnit = TheScriptEngine->getUnitNamed(unitName);

	if (!pUnit) {
		return;
	}
	DamageInfo damageInfo;
	damageInfo.in.m_damageType = DAMAGE_UNRESISTABLE;
	damageInfo.in.m_deathType = DEATH_NORMAL;
	damageInfo.in.m_sourceID = INVALID_ID;
	damageInfo.in.m_amount = static_cast<Real>(damageAmt);
	pUnit->attemptDamage( &damageInfo );
}

//-------------------------------------------------------------------------------------------------
/** doNamedDelete */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedDelete(const AsciiString& unitName)
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theUnit) {
		return;
	}

	TheGameLogic->destroyObject(theUnit);
}

//-------------------------------------------------------------------------------------------------
/** doNamedKill */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedKill(const AsciiString& unitName)
{
	Object *pUnit = TheScriptEngine->getUnitNamed(unitName);

	if (!pUnit) {
		return;
	}
	pUnit->kill();
}

//-------------------------------------------------------------------------------------------------
/** doNamedTransferAssetsToPlayer */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedTransferAssetsToPlayer(const AsciiString& unitName, const AsciiString& playerDstName)
{
	Object *pObj = TheScriptEngine->getUnitNamed(unitName);
	Player *pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerDstName);

	if (!pObj || !pPlayer) {
		return;
	}

	Team *playerTeam = pPlayer->getDefaultTeam();
	if (!playerTeam) {
		return;
	}

	pObj->setTeam(playerTeam);
	updateTeamAndPlayerStuff(pObj, nullptr);
}

//-------------------------------------------------------------------------------------------------
/** doUnitGarrisonSpecificBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitGarrisonSpecificBuilding(const AsciiString& unitName, const AsciiString& buildingName)
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theUnit) {
		return;
	}

	Object *theBuilding = TheScriptEngine->getUnitNamed(buildingName);
	if (!theBuilding) {
		return;
	}

	ContainModuleInterface *contain = theBuilding->getContain();
	if( !contain )
	{
		DEBUG_CRASH(("doUnitGarrisonSpecificBuilding script -- building doesn't have a container" ));
		return;
	}
	PlayerMaskType player = theBuilding->getContain()->getPlayerWhoEntered();

	if (!(theBuilding->isKindOf(KINDOF_STRUCTURE) && 
		(player == 0) || (player == theUnit->getControllingPlayer()->getPlayerMask()))) {
		return;
	}
	AIUpdateInterface *ai = theUnit->getAIUpdateInterface();
	if (!ai) {
		return;
	}

	ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	ai->aiEnter(theBuilding, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doUnitGarrisonNearestBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitGarrisonNearestBuilding(const AsciiString& unitName)
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theUnit) {
		return;
	}

	AIUpdateInterface *ai = theUnit->getAIUpdateInterface();
	if (!ai) {
		return;
	}


	PartitionFilter *filters[16];
	Int count = 0;

	PartitionFilterAcceptByKindOf f1(MAKE_KINDOF_MASK(KINDOF_STRUCTURE), KINDOFMASK_NONE);
	filters[ count++ ] = &f1;

	PartitionFilterSameMapStatus filterMapStatus(theUnit);
	filters[ count++ ] = &filterMapStatus;


	PartitionFilterAcceptByKindOf f2( MAKE_KINDOF_MASK( KINDOF_FS_INTERNET_CENTER ), KINDOFMASK_NONE );
	PartitionFilterRejectByKindOf f3( MAKE_KINDOF_MASK( KINDOF_FS_INTERNET_CENTER ), KINDOFMASK_NONE );
	
	if( theUnit->isKindOf( KINDOF_MONEY_HACKER ) )
	{
		//If the unit is a hacker, then look for an internet center instead of a normal building!
		filters[ count++ ] = &f2;
	}
	else
	{
		//If the unit ISN'T a hacker, then ignore internet centers!
		filters[ count++ ] = &f3;
	}

	filters[count++] = nullptr;
	
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(theUnit, REALLY_FAR, DistanceCalculationType::FROM_CENTER_3D, filters, IterOrderType::ITER_SORTED_NEAR_TO_FAR);
	MemoryPoolObjectHolder hold(iter);

	for (Object *theBuilding = iter->first(); theBuilding; theBuilding = iter->next()) 
	{
		ContainModuleInterface *contain = theBuilding->getContain();
		if( !contain )
		{
			DEBUG_CRASH( ("doUnitGarrisonNearestBuilding script -- building doesn't have a container.") );
			continue;
		}
		PlayerMaskType player = theBuilding->getContain()->getPlayerWhoEntered();
		if (!((player == 0) || (player == theUnit->getControllingPlayer()->getPlayerMask()))) {
			continue;
		}

		ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
		ai->aiEnter(theBuilding, CMD_FROM_SCRIPT);
		return;
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedEnableStealth */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedEnableStealth( const AsciiString& unitName, Bool enabled )
{
	Object *self = TheScriptEngine->getUnitNamed( unitName );
	if( self )
	{
		self->setScriptStatus( OBJECT_STATUS_SCRIPT_UNSTEALTHED, !enabled );
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedSetUnmanned */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedSetUnmanned( const AsciiString& unitName )
{
	Object *self = TheScriptEngine->getUnitNamed( unitName );
	if( self )
	{
		self->setDisabled( DISABLED_UNMANNED );
		TheGameLogic->deselectObject( self, PLAYERMASK_ALL, TRUE );
		// Convert it to the neutral team so it renders gray giving visual representation that it is unmanned.
		self->setTeam( ThePlayerList->getNeutralPlayer()->getDefaultTeam() );
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedSetBoobytrapped( const AsciiString& thingTemplateName, const AsciiString& unitName )
{
	Object *obj = TheScriptEngine->getUnitNamed( unitName );
	if( obj )
	{
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
/** doUnitExitBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitExitBuilding(const AsciiString& unitName)
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theUnit) {
		return;
	}

	AIUpdateInterface *ai = theUnit->getAIUpdateInterface();
	if (!ai) {
		return;
	}

	ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	ai->aiExit(nullptr, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doNamedSetHeld */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedSetHeld(const AsciiString& unit, Bool held)
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	if (theObj) 
	{
		theObj->setDisabledUntil( DISABLED_HELD, held ? FOREVER : NEVER );
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedSetStoppingDistance */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedSetStoppingDistance(const AsciiString& unit, Real stoppingDistance)
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	if (theObj) 
	{
		AIUpdateInterface *aiUpdate = theObj->getAIUpdateInterface();
		if (!aiUpdate || !aiUpdate->getCurLocomotor()) {
			return;
		}
		
		if (stoppingDistance >= 0.5f)
		{
			aiUpdate->getCurLocomotor()->setCloseEnoughDist(stoppingDistance);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedStopSpecialPowerCountdown */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedStopSpecialPowerCountdown(const AsciiString& unit, const AsciiString& specialPower, Bool stop)
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplate(specialPower);
	if (theObj && power)
	{
		SpecialPowerModuleInterface *mod = theObj->getSpecialPowerModule(power);
		if (mod)
		{
			mod->pauseCountdown(stop);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedSetSpecialPowerCountdown */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedSetSpecialPowerCountdown( const AsciiString& unit, const AsciiString& specialPower, Int seconds )
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplate(specialPower);
	if (theObj && power)
	{
		SpecialPowerModuleInterface *mod = theObj->getSpecialPowerModule(power);
		if (mod)
		{
			Int frames = LOGICFRAMES_PER_SECOND * seconds;
			mod->setReadyFrame(TheGameLogic->getFrame() + frames);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedAddSpecialPowerCountdown */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedAddSpecialPowerCountdown( const AsciiString& unit, const AsciiString& specialPower, Int seconds )
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplate(specialPower);
	if (theObj && power)
	{
		SpecialPowerModuleInterface *mod = theObj->getSpecialPowerModule(power);
		if (mod)
		{
			Int frames = LOGICFRAMES_PER_SECOND * seconds;
			mod->setReadyFrame(mod->getReadyFrame() + frames);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedFireSpecialPowerAtArea */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedFireSpecialPowerAtWaypoint( const AsciiString& unit, const AsciiString& specialPower, const AsciiString& waypoint )
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplate(specialPower);
	if (theObj && power)
	{
		SpecialPowerModuleInterface *mod = theObj->getSpecialPowerModule(power);
		if (mod)
		{
			Waypoint *way = TheTerrainLogic->getWaypointByName(waypoint);
			if (!way) {
				return;
			}
			mod->doSpecialPowerAtLocation(way->getLocation(), INVALID_ANGLE, COMMAND_FIRED_BY_SCRIPT );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedFireSpecialPowerAtNamed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedFireSpecialPowerAtNamed( const AsciiString& unit, const AsciiString& specialPower, const AsciiString& target )
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	Object *theTarget = TheScriptEngine->getUnitNamed( target );
	const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplate(specialPower);
	if (theObj && power && theTarget)
	{
		SpecialPowerModuleInterface *mod = theObj->getSpecialPowerModule(power);
		if (mod)
		{
			mod->doSpecialPowerAtObject(theTarget, COMMAND_FIRED_BY_SCRIPT );
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedUseCommandButtonAbility( const AsciiString& unit, const AsciiString& ability )
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	
	//Sanity check
	if( !theObj )
	{
		return;
	}

	const CommandSet *commandSet = TheControlBar->findCommandSet( theObj->getCommandSetString() );
	if( commandSet )
	{
		for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
		{
			//Get the command button.
			const CommandButton *commandButton = commandSet->getCommandButton(i);

			if( commandButton )
			{
				if( !commandButton->getName().isEmpty() )
				{
					if( commandButton->getName() == ability )
					{
						theObj->doCommandButton( commandButton, CMD_FROM_SCRIPT );
					}
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedUseCommandButtonAbilityOnNamed( const AsciiString& unit, const AsciiString& ability, const AsciiString& target )
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	Object *theTarget = TheScriptEngine->getUnitNamed( target );
	
	//Sanity check
	if( !theObj || !theTarget )
	{
		return;
	}

	const CommandSet *commandSet = TheControlBar->findCommandSet( theObj->getCommandSetString() );
	if( commandSet )
	{
		for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
		{
			//Get the command button.
			const CommandButton *commandButton = commandSet->getCommandButton(i);

			if( commandButton )
			{
				if( !commandButton->getName().isEmpty() )
				{
					if( commandButton->getName() == ability )
					{
						theObj->doCommandButtonAtObject( commandButton, theTarget, CMD_FROM_SCRIPT );
					}
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedUseCommandButtonAbilityAtWaypoint( const AsciiString& unit, const AsciiString& ability, const AsciiString& waypoint )
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	Waypoint *pWaypoint = TheTerrainLogic->getWaypointByName( waypoint );
	
	//Sanity check
	if( !theObj || !pWaypoint )
	{
		return;
	}

	const CommandSet *commandSet = TheControlBar->findCommandSet( theObj->getCommandSetString() );
	if( commandSet )
	{
		for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
		{
			//Get the command button.
			const CommandButton *commandButton = commandSet->getCommandButton(i);

			if( commandButton )
			{
				if( !commandButton->getName().isEmpty() )
				{
					if( commandButton->getName() == ability )
					{
						theObj->doCommandButtonAtPosition( commandButton, pWaypoint->getLocation(), CMD_FROM_SCRIPT );
					}
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedUseCommandButtonAbilityUsingWaypointPath( const AsciiString& unit, const AsciiString& ability, const AsciiString& waypointPath )
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	if( !theObj )
	{
		return;
	}

	Waypoint *pWaypoint = TheTerrainLogic->getClosestWaypointOnPath( theObj->getPosition(), waypointPath );
	
	//Sanity check
	if( !pWaypoint )
	{
		return;
	}

	const CommandSet *commandSet = TheControlBar->findCommandSet( theObj->getCommandSetString() );
	if( commandSet )
	{
		for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
		{
			//Get the command button.
			const CommandButton *commandButton = commandSet->getCommandButton(i);

			if( commandButton )
			{
				if( !commandButton->getName().isEmpty() )
				{
					if( commandButton->getName() == ability )
					{
						theObj->doCommandButtonUsingWaypoints( commandButton, pWaypoint, CMD_FROM_SCRIPT );
					}
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedStop */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedStop(const AsciiString& unitName)
{
	Object *theObj = TheScriptEngine->getUnitNamed( unitName );
	if (!theObj) {
		return;
	}

	AIUpdateInterface *ai = theObj->getAIUpdateInterface();
	if (!ai) {
		return;
	}

	ai->aiIdle(CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doUnitStartSequentialScript */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitStartSequentialScript(const AsciiString& unitName, const AsciiString& scriptName, Int loopVal)
{
	Object *obj = TheScriptEngine->getUnitNamed(unitName);
	if (!obj) {
		return;
	}

	Script *script = const_cast<Script*>(TheScriptEngine->findScriptByName(scriptName));
	if (!script) {
		return;
	}

	SequentialScript* seqScript = newInstance(SequentialScript);
	seqScript->m_objectID = obj->getID();
	seqScript->m_scriptToExecuteSequentially = script;
	seqScript->m_timesToLoop = loopVal;
	
	TheScriptEngine->appendSequentialScript(seqScript);

	deleteInstanceOf(seqScript);
}

//-------------------------------------------------------------------------------------------------
/** doUnitStopSequentialScript */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitStopSequentialScript(const AsciiString& unitName)
{
	Object *obj = TheScriptEngine->getUnitNamed(unitName);
	if (!obj) {
		return;
	}

	TheScriptEngine->removeAllSequentialScripts(obj);
}

//-------------------------------------------------------------------------------------------------
/** doTeamStartSequentialScript */
//-------------------------------------------------------------------------------------------------
/** doNamedFireWeaponFollowingWaypointPath -- Kris
		Orders unit to fire a waypoint following capable weapon to follow a waypoint and attack the
		final waypoint position. */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedFireWeaponFollowingWaypointPath( const AsciiString& unit, const AsciiString& waypointPath )
{
	//Get the unit... if it fails, abort.
	Object *theUnit = TheScriptEngine->getUnitNamed( unit );
	if( !theUnit ) 
	{
		return;
	}
	
	Coord3D pos = *theUnit->getPosition();


	//Find the closest waypoint on the path.
	Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &pos, waypointPath );
	if( !way )
	{
		return;
	}
	//We have to do special checking to make sure our unit even has a waypoint following capable weapon.
	Weapon *weapon = theUnit->findWaypointFollowingCapableWeapon();
	if( !weapon )
	{
		return;
	}

	Object *projectile = weapon->forceFireWeapon( theUnit, &pos );
	if( projectile )
	{
		//Get the AIUpdateInterface... if it fails, abort.
		AIUpdateInterface* aiUpdate = projectile->getAIUpdateInterface();
		if( !aiUpdate )
		{
			return;
		}
		DEBUG_ASSERTLOG(TheTerrainLogic->isPurposeOfPath(way, waypointPath), ("***Wrong waypoint purpose. Make jba fix this.\n"));
		
		projectile->leaveGroup();
		aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
		aiUpdate->aiFollowWaypointPath(way, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doUnitGuardForFramecount */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitGuardForFramecount(const AsciiString& unitName, Int framecount)
{
	Object *obj = TheScriptEngine->getUnitNamed(unitName);
	if (!obj) {
		return;
	}

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) {
		return;
	}

	Coord3D pos = *obj->getPosition();
	ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	ai->aiGuardPosition(&pos, GUARDMODE_NORMAL, CMD_FROM_SCRIPT);
	TheScriptEngine->setSequentialTimer(obj, framecount);
}

//-------------------------------------------------------------------------------------------------
/** doUnitIdleForFramecount */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitIdleForFramecount(const AsciiString& unitName, Int framecount)
{
	Object *obj = TheScriptEngine->getUnitNamed(unitName);
	if (!obj) {
		return;
	}

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) {
		return;
	}

	ai->aiIdle(CMD_FROM_SCRIPT);
	TheScriptEngine->setSequentialTimer(obj, framecount);
}

//-------------------------------------------------------------------------------------------------
/** doUnitReceiveUpgrade */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitReceiveUpgrade( const AsciiString& unitName, const AsciiString& upgradeName )
{
	Object *obj = TheScriptEngine->getUnitNamed(unitName);
	if (!obj) {
		return;
	}

	const UpgradeTemplate *templ = TheUpgradeCenter->findUpgrade(upgradeName);
	if (!templ) {
		return;
	}

	DEBUG_ASSERTCRASH(obj->affectedByUpgrade(templ), ("Design bug: Unit '%s' was given upgrade '%s', but he is unaffected.", unitName.str(), upgradeName.str()));

	obj->giveUpgrade(templ);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedFaceNamed( const AsciiString &unitName, const AsciiString &faceUnitName )
{
	Object *obj = TheScriptEngine->getUnitNamed( unitName );
	if( obj ) 
	{
		Object *faceObj = TheScriptEngine->getUnitNamed( faceUnitName );
		if( faceObj )
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

//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedFaceWaypoint( const AsciiString &unitName, const AsciiString &faceWaypointName )
{
	Object *obj = TheScriptEngine->getUnitNamed( unitName );
	if( obj ) 
	{
		Waypoint *way = TheTerrainLogic->getWaypointByName( faceWaypointName );
		if( way )
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

//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedSetTrainHeld( const AsciiString &locoName, const Bool set )
{
	Object *obj = TheScriptEngine->getUnitNamed( locoName );
	if( obj ) 
	{

		static const NameKeyType rrkey = NAMEKEY( "RailroadBehavior" );
		RailroadBehavior *rBehavior = static_cast<RailroadBehavior*>(obj->findUpdateModule( rrkey ));

    if ( rBehavior )
    {
      rBehavior->RailroadBehavior::setHeld( set );
    }
    
	}
}
