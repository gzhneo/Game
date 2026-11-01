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
#include "Common/LogicAudio.h"
#include "Common/BitFlagsIO.h"
#include "Common/BuildAssistant.h"
#include "Common/Dict.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/ModuleFactory.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Radar.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/WellKnownKeys.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"
#include "Common/PerfTimer.h"
#include "GameClient/Anim2D.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/Eva.h"
#include "GameClient/GameClient.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/AIStats.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/FiringTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/AutoHealBehavior.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/CountermeasuresBehavior.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Module/DamageModule.h"
#include "GameLogic/Module/DeletionUpdate.h"
#include "GameLogic/Module/DestroyModule.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/ObjectDefectionHelper.h"
#include "GameLogic/Module/ObjectRepulsorHelper.h"
#include "GameLogic/Module/ObjectSMCHelper.h"
#include "GameLogic/Module/ObjectWeaponStatusHelper.h"
#include "GameLogic/Module/OverchargeBehavior.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/PowerPlantUpgrade.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/RadarUpgrade.h"
#include "GameLogic/Module/RebuildHoleBehavior.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/SpecialAbilityUpdate.h"
#include "GameLogic/Module/StatusDamageHelper.h"
#include "GameLogic/Module/StickyBombUpdate.h"
#include "GameLogic/Module/SubdualDamageHelper.h"
#include "GameLogic/Module/TempWeaponBonusHelper.h"
#include "GameLogic/Module/ToppleUpdate.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/UpgradeModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSet.h"
#include "GameLogic/Module/RadarUpdate.h"
#include "GameLogic/Module/PowerPlantUpdate.h"
#include "Common/CRCDebug.h"
#include "Common/MiscAudio.h"
#include "Common/AudioEventInfo.h"
#include "Common/DynamicAudioEventInfo.h"

// Special powers, command buttons and module interfaces

//-------------------------------------------------------------------------------------------------
Bool Object::hasSpecialPower( SpecialPowerType type ) const
{
	return TEST_SPECIALPOWERMASK( m_specialPowerBits, type );
}

//-------------------------------------------------------------------------------------------------
Bool Object::hasAnySpecialPower() const
{
  return SPECIALPOWERMASK_ANY_SET( m_specialPowerBits );
}

//-------------------------------------------------------------------------------------------------
/** Given a special power template, find the module in the object that can implement it.
	* There can be at most one */
//-------------------------------------------------------------------------------------------------
SpecialPowerModuleInterface *Object::getSpecialPowerModule( const SpecialPowerTemplate *specialPowerTemplate ) const
{

	// sanity
	if( specialPowerTemplate == nullptr )
		return nullptr;

	// search the modules for the one with the matching template
	for( BehaviorModule** m = m_behaviors; *m; ++m )
	{
		SpecialPowerModuleInterface* sp = (*m)->getSpecialPower();
		if (!sp)
			continue;

		if( sp->isModuleForPower( specialPowerTemplate ) )
			return sp;
	}

	return nullptr;

}

//-------------------------------------------------------------------------------------------------
/** Execute special power */
//-------------------------------------------------------------------------------------------------
void Object::doSpecialPower( const SpecialPowerTemplate *specialPowerTemplate, UnsignedInt commandOptions, Bool forced )
{

	if (isDisabled())
		return;
	
	// sanity
	if( !forced && TheSpecialPowerStore->canUseSpecialPower( this, specialPowerTemplate ) == FALSE )
		return;

	// get the module and execute
	SpecialPowerModuleInterface *mod = getSpecialPowerModule( specialPowerTemplate );
	if( mod )
		mod->doSpecialPower( commandOptions );

}

//-------------------------------------------------------------------------------------------------
/** Execute special power */
//-------------------------------------------------------------------------------------------------
void Object::doSpecialPowerAtObject( const SpecialPowerTemplate *specialPowerTemplate, Object *obj, UnsignedInt commandOptions, Bool forced )
{

	if (isDisabled())
		return;
	
	// sanity
	if( !forced && TheSpecialPowerStore->canUseSpecialPower( this, specialPowerTemplate ) == FALSE )
		return;

	// get the module and execute
	SpecialPowerModuleInterface *mod = getSpecialPowerModule( specialPowerTemplate );
	if( mod )
		mod->doSpecialPowerAtObject( obj, commandOptions );
}

//-------------------------------------------------------------------------------------------------
/** Execute special power */
//-------------------------------------------------------------------------------------------------
void Object::doSpecialPowerAtLocation( const SpecialPowerTemplate *specialPowerTemplate, 
																			 const Coord3D *loc, Real angle, UnsignedInt commandOptions, Bool forced )
{

	if (isDisabled())
		return;
	
	// sanity
	if( !forced && TheSpecialPowerStore->canUseSpecialPower( this, specialPowerTemplate ) == FALSE )
		return;

	// get the module and execute
	SpecialPowerModuleInterface *mod = getSpecialPowerModule( specialPowerTemplate );
	if( mod )
		mod->doSpecialPowerAtLocation( loc, angle, commandOptions );

}  

//-------------------------------------------------------------------------------------------------
/** Execute special power */
//-------------------------------------------------------------------------------------------------
void Object::doSpecialPowerUsingWaypoints( const SpecialPowerTemplate *specialPowerTemplate, const Waypoint *way, UnsignedInt commandOptions, Bool forced )
{

	if (isDisabled())
		return;
	
	// sanity
	if( !forced && TheSpecialPowerStore->canUseSpecialPower( this, specialPowerTemplate ) == FALSE )
		return;

	// get the module and execute
	SpecialPowerModuleInterface *mod = getSpecialPowerModule( specialPowerTemplate );
	if( mod )
		mod->doSpecialPowerUsingWaypoints( way, commandOptions );

}

//-------------------------------------------------------------------------------------------------
/** Execute command button ability */
//-------------------------------------------------------------------------------------------------
void Object::doCommandButton( const CommandButton *commandButton, CommandSourceType cmdSource )
{
	if (isDisabled())
		return;
	
	AIUpdateInterface *ai = getAIUpdateInterface();
	if( commandButton )
	{
		switch( commandButton->getCommandType() )
		{
			case GUI_COMMAND_SPECIAL_POWER:
				if( commandButton->getSpecialPowerTemplate() )
				{
					CommandOption commandOptions = static_cast<CommandOption>(commandButton->getOptions() | COMMAND_FIRED_BY_SCRIPT);
					doSpecialPower( commandButton->getSpecialPowerTemplate(), commandOptions, cmdSource == CMD_FROM_SCRIPT );
					return;
				}
				break;
			case GUI_COMMAND_STOP:
				if( ai )
				{
					ai->aiIdle( cmdSource );
					return;
				}
				break;

			case GUI_COMMAND_SWITCH_WEAPON:
				{
					WeaponSlotType weaponSlot = commandButton->getWeaponSlot();
					// GUI_COMMAND_SWITCH_WEAPON switches until un-switched, or switched to something else.
					setWeaponLock( weaponSlot, LOCKED_PERMANENTLY );
					return;
				}

			case GUI_COMMAND_FIRE_WEAPON:
				// With Generals scripts the button only locks the weapon (until the next attack ends) and does not fire.
				if( TheGlobalData->m_scriptsLikeGenerals )
				{
					setWeaponLock( commandButton->getWeaponSlot(), LOCKED_TEMPORARILY );
					return;
				}
				if( ai )
				{
					if( !BitTest( commandButton->getOptions(), COMMAND_OPTION_NEED_OBJECT_TARGET ) && !BitTest( commandButton->getOptions(), NEED_TARGET_POS ) )
					{
						setWeaponLock( commandButton->getWeaponSlot(), LOCKED_TEMPORARILY );
						//LOCATION BASED FIRE WEAPON
						ai->aiAttackPosition( nullptr, commandButton->getMaxShotsToFire(), cmdSource );
					}
					else
					{
						DEBUG_CRASH( ("WARNING: Script doCommandButton for button %s cannot fire weapon with NO POSITION. Skipping.", commandButton->getName().str()) );
					}
					return;
				}
				break;

			case GUI_COMMAND_OBJECT_UPGRADE:
			case GUI_COMMAND_PLAYER_UPGRADE:
				{
					const UpgradeTemplate *upgradeT = commandButton->getUpgradeTemplate();
					DEBUG_ASSERTCRASH( upgradeT, ("Undefined upgrade '%s' in player upgrade command\n", "UNKNOWN") );
					// sanity
					if( upgradeT == nullptr )
						break;
					if( upgradeT->getUpgradeType() == UPGRADE_TYPE_OBJECT )
					{
						if( hasUpgrade( upgradeT )  || !affectedByUpgrade( upgradeT ) )
							break;
					}
					// producer must have a production update
					ProductionUpdateInterface *pu = getProductionUpdateInterface();
					if( pu == nullptr )
						break;
					// queue the upgrade "research"
					pu->queueUpgrade( upgradeT );
				}
				return;
			case GUI_COMMAND_UNIT_BUILD:
			case GUI_COMMAND_DOZER_CONSTRUCT: {
				const ThingTemplate *tt = commandButton->getThingTemplate();
				ProductionUpdateInterface *pu = this->getProductionUpdateInterface();
				if (pu && tt) {
					pu->queueCreateUnit( tt, pu->requestUniqueUnitID());
					return;
				}
				break;
			}
			case GUI_COMMAND_HACK_INTERNET:{
				if( ai )
				{
					ai->aiHackInternet( cmdSource );
					return;
				}
				break;
			}

			case GUI_COMMAND_SELL:
				// with Generals scripts the button sells nothing
				if( !TheGlobalData->m_scriptsLikeGenerals )
					TheBuildAssistant->sellObject( this );
				return;
			
			//Feel free to implement object based command buttons.
			case GUI_COMMAND_COMBATDROP:
			case GUI_COMMAND_DOZER_CONSTRUCT_CANCEL:
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
			case GUI_COMMAND_TOGGLE_OVERCHARGE:
			case GUICOMMANDMODE_HIJACK_VEHICLE:
			case GUICOMMANDMODE_CONVERT_TO_CARBOMB:
			default:
				break;
		}
		// Generals does not report unimplemented buttons.
		DEBUG_ASSERTCRASH( TheGlobalData->m_scriptsLikeGenerals, ("WARNING: Script doCommandButton for button %s not implemented. Doing nothing.", commandButton->getName().str()) );
	}
}

//-------------------------------------------------------------------------------------------------
/** Execute command button ability directed at an object target */
//-------------------------------------------------------------------------------------------------
void Object::doCommandButtonAtObject( const CommandButton *commandButton, Object *obj, CommandSourceType cmdSource )
{
	if (isDisabled())
		return;
	
	AIUpdateInterface *ai = getAIUpdateInterface();
	if( commandButton )
	{
		switch( commandButton->getCommandType() )
		{
			case GUI_COMMAND_COMBATDROP:
				if( ai )
				{
					ai->aiCombatDrop( obj, *(obj->getPosition()), cmdSource );
				}
				return;
			case GUI_COMMAND_SPECIAL_POWER:
			{
				if( commandButton->getSpecialPowerTemplate() )
				{
					CommandOption commandOptions = static_cast<CommandOption>(commandButton->getOptions() | COMMAND_FIRED_BY_SCRIPT);
					doSpecialPowerAtObject( commandButton->getSpecialPowerTemplate(), obj, commandOptions, cmdSource == CMD_FROM_SCRIPT );
				}
				return;
			}

			case GUI_COMMAND_STOP:
				if( ai )
				{
					ai->aiIdle( cmdSource );
				}
				return;

			case GUI_COMMAND_FIRE_WEAPON:
				// with Generals scripts the button does not fire at an object
				if( TheGlobalData->m_scriptsLikeGenerals )
					return;
				if( ai )
				{
					if( BitTest( commandButton->getOptions(), COMMAND_OPTION_NEED_OBJECT_TARGET ) )
					{
						//OBJECT BASED FIRE WEAPON
						if( !obj )
						{
							break;
						}

						if( !commandButton->isValidObjectTarget( this, obj ) )
						{
							break;
						}

						setWeaponLock( commandButton->getWeaponSlot(), LOCKED_TEMPORARILY );

						if( BitTest( commandButton->getOptions(), ATTACK_OBJECTS_POSITION ) )
						{
							//Actually, you know what.... we want to attack the object's location instead.
							ai->aiAttackPosition( obj->getPosition(), commandButton->getMaxShotsToFire(), cmdSource );
						}
						else
						{
							ai->aiAttackObject( obj, commandButton->getMaxShotsToFire(), cmdSource );
						}
					}
					else
					{
						DEBUG_CRASH( ("WARNING: Script doCommandButtonAtObject for button %s cannot fire weapon at AN OBJECT. Skipping.", commandButton->getName().str()) );
					}
					return;
				}
				break;
				
			case GUICOMMANDMODE_HIJACK_VEHICLE:
			case GUICOMMANDMODE_CONVERT_TO_CARBOMB:
			case GUICOMMANDMODE_SABOTAGE_BUILDING:
				// Generals scripts only handle hijacking here (car bomb: terrorists do nothing).
				if( TheGlobalData->m_scriptsLikeGenerals && commandButton->getCommandType() != GUICOMMANDMODE_HIJACK_VEHICLE )
					return;
				if( ai )
				{
					ai->aiEnter( obj, cmdSource );
				}
				return;

			//Feel free to implement object based command buttons.
			case GUI_COMMAND_DOZER_CONSTRUCT:
			case GUI_COMMAND_DOZER_CONSTRUCT_CANCEL:
			case GUI_COMMAND_UNIT_BUILD:
			case GUI_COMMAND_CANCEL_UNIT_BUILD:
			case GUI_COMMAND_PLAYER_UPGRADE:
			case GUI_COMMAND_OBJECT_UPGRADE:
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
			case GUI_COMMAND_SWITCH_WEAPON:

			default:
				break;
		}
		// Generals does not report unimplemented buttons.
		DEBUG_ASSERTCRASH( TheGlobalData->m_scriptsLikeGenerals, ("WARNING: Script doCommandButtonAtObject for button %s not implemented. Doing nothing.", commandButton->getName().str()) );
	}
}

//-------------------------------------------------------------------------------------------------
/** Execute command button ability directed at a location */
//-------------------------------------------------------------------------------------------------
void Object::doCommandButtonAtPosition( const CommandButton *commandButton, const Coord3D *pos, CommandSourceType cmdSource )
{
	if (isDisabled())
		return;
	
	AIUpdateInterface *ai = getAIUpdateInterface();
	if( commandButton )
	{
		switch( commandButton->getCommandType() )
		{
			case GUI_COMMAND_SPECIAL_POWER:
			{
				if( commandButton->getSpecialPowerTemplate() )
				{
					CommandOption commandOptions = static_cast<CommandOption>(commandButton->getOptions() | COMMAND_FIRED_BY_SCRIPT);
					doSpecialPowerAtLocation( commandButton->getSpecialPowerTemplate(), pos, INVALID_ANGLE, commandOptions, cmdSource == CMD_FROM_SCRIPT );
					return;
				}
				break;
			}
			case GUI_COMMAND_ATTACK_MOVE:
				if( ai )
				{
					ai->aiAttackMoveToPosition( pos, commandButton->getMaxShotsToFire(), cmdSource );
					return;
				}
				break;
			case GUI_COMMAND_STOP:
				if( ai )
				{
					ai->aiIdle( cmdSource );
					return;
				}
				break;

			case GUI_COMMAND_DOZER_CONSTRUCT:
				TheBuildAssistant->buildObjectNow( this, commandButton->getThingTemplate(), pos, 0.0f, getControllingPlayer() );
				return;

			case GUI_COMMAND_FIRE_WEAPON:
				// with Generals scripts the button does not fire at a position
				if( TheGlobalData->m_scriptsLikeGenerals )
					return;
				if( ai )
				{
					if( BitTest( commandButton->getOptions(), NEED_TARGET_POS ) )
					{
						//LOCATION BASED FIRE WEAPON
						if( !pos )
						{
							break;
						}
						setWeaponLock( commandButton->getWeaponSlot(), LOCKED_TEMPORARILY );
						ai->aiAttackPosition( pos, commandButton->getMaxShotsToFire(), cmdSource );
					}
					else
					{
						DEBUG_CRASH( ("WARNING: Script doCommandButtonAtPosition for button %s cannot fire weapon at A POSITION. Skipping.", commandButton->getName().str()) );
					}
					return;
				}
				break;
				
			case GUI_COMMAND_DOZER_CONSTRUCT_CANCEL:
			case GUI_COMMAND_UNIT_BUILD:
			case GUI_COMMAND_CANCEL_UNIT_BUILD:
			case GUI_COMMAND_PLAYER_UPGRADE:
			case GUI_COMMAND_OBJECT_UPGRADE:
			case GUI_COMMAND_CANCEL_UPGRADE:
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
			case GUI_COMMAND_COMBATDROP:
			case GUI_COMMAND_SWITCH_WEAPON:
			case GUICOMMANDMODE_HIJACK_VEHICLE:
			case GUICOMMANDMODE_CONVERT_TO_CARBOMB:
			default:
				break;
		}
		// Generals does not report unimplemented buttons.
		DEBUG_ASSERTCRASH( TheGlobalData->m_scriptsLikeGenerals, ("WARNING: Script doCommandButtonAtPosition for button %s not implemented. Doing nothing.", commandButton->getName().str()) );
	}
}

//-------------------------------------------------------------------------------------------------
/** Execute command button ability directed at a location */
//-------------------------------------------------------------------------------------------------
void Object::doCommandButtonUsingWaypoints( const CommandButton *commandButton, const Waypoint *way, CommandSourceType cmdSource )
{
	if (isDisabled())
		return;
	
	if( commandButton )
	{
		if( !BitTest( commandButton->getOptions(), CAN_USE_WAYPOINTS ) )
		{
			//Our button doesn't support waypoints.
			DEBUG_CRASH( ("WARNING: Script doCommandButtonUsingWaypoints for button %s lacks CAN_USE_WAYPOINTS option. Doing nothing.", commandButton->getName().str()) );
			return;
		}
		switch( commandButton->getCommandType() )
		{
			case GUI_COMMAND_SPECIAL_POWER:
			{
				if( commandButton->getSpecialPowerTemplate() )
				{
					CommandOption commandOptions = static_cast<CommandOption>(commandButton->getOptions() | COMMAND_FIRED_BY_SCRIPT);
					doSpecialPowerUsingWaypoints( commandButton->getSpecialPowerTemplate(), way, commandOptions, cmdSource == CMD_FROM_SCRIPT );
					return;
				}
				break;
			}
			case GUI_COMMAND_ATTACK_MOVE:
			case GUI_COMMAND_STOP:
			case GUI_COMMAND_DOZER_CONSTRUCT:
			case GUI_COMMAND_DOZER_CONSTRUCT_CANCEL:
			case GUI_COMMAND_UNIT_BUILD:
			case GUI_COMMAND_CANCEL_UNIT_BUILD:
			case GUI_COMMAND_PLAYER_UPGRADE:
			case GUI_COMMAND_OBJECT_UPGRADE:
			case GUI_COMMAND_CANCEL_UPGRADE:
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
			case GUI_COMMAND_FIRE_WEAPON:
			case GUI_COMMAND_HACK_INTERNET:
			case GUI_COMMAND_TOGGLE_OVERCHARGE:
			case GUI_COMMAND_COMBATDROP:
			case GUI_COMMAND_SWITCH_WEAPON:
			case GUICOMMANDMODE_HIJACK_VEHICLE:
			case GUICOMMANDMODE_CONVERT_TO_CARBOMB:
			default:
				break;
		}
		DEBUG_CRASH( ("WARNING: Script doCommandButtonUsingWaypoints for button %s not implemented. Doing nothing.", commandButton->getName().str()) );
	}
}

// ------------------------------------------------------------------------------------------------
/** Search our update modules for a production update interface and return it if one is found */
// ------------------------------------------------------------------------------------------------
ProductionUpdateInterface* Object::getProductionUpdateInterface( void )
{
	ProductionUpdateInterface *pui;

	// tell our update modules that we intend to do this special power.
	for( BehaviorModule** u = m_behaviors; *u; ++u )
	{

		pui = (*u)->getProductionUpdateInterface();
		if( pui )
			return pui;

	}  // end for

	return nullptr;

}  // end getProductionUpdateInterface

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
DockUpdateInterface *Object::getDockUpdateInterface( void )
{
	DockUpdateInterface *dock = nullptr;

	for( BehaviorModule **u = m_behaviors; *u; ++u )
	{
		if( (dock = (*u)->getDockUpdateInterface()) != nullptr )
			return dock;
	}

	return nullptr;

}  // end getDockUpdateInterface

// ------------------------------------------------------------------------------------------------
// Search our special power modules for a specific one.
// ------------------------------------------------------------------------------------------------
SpecialPowerModuleInterface* Object::findSpecialPowerModuleInterface( SpecialPowerType type ) const
{
	for (BehaviorModule** m = m_behaviors; *m; ++m)
	{
		SpecialPowerModuleInterface* sp = (*m)->getSpecialPower();
		if (!sp)
			continue;

		const SpecialPowerTemplate *spTemplate = sp->getSpecialPowerTemplate();
		if (spTemplate && spTemplate->getSpecialPowerType() == type || type == SPECIAL_INVALID )
		{
			return sp; 
		}
	}
	return nullptr;
}

// ------------------------------------------------------------------------------------------------
// Search our special power modules for the first occurrence of a shortcut special.
// ------------------------------------------------------------------------------------------------
SpecialPowerModuleInterface* Object::findAnyShortcutSpecialPowerModuleInterface() const
{
	for( BehaviorModule** m = m_behaviors; *m; ++m )
	{
		SpecialPowerModuleInterface* sp = (*m)->getSpecialPower();
		if (!sp)
			continue;

		const SpecialPowerTemplate *spTemplate = sp->getSpecialPowerTemplate();
		if( spTemplate && spTemplate->isShortcutPower() )
		{
			return sp; 
		}
	}
	return nullptr;
}

// ------------------------------------------------------------------------------------------------
/** Get spawn behavior interface from object */
// ------------------------------------------------------------------------------------------------
SpawnBehaviorInterface* Object::getSpawnBehaviorInterface() const
{
	for (BehaviorModule** m = m_behaviors; *m; ++m)
	{
		SpawnBehaviorInterface *sbi = (*m)->getSpawnBehaviorInterface();
		if( sbi )
		{
			return sbi;
		}
	}
	return nullptr;
}  // end getSpawnBehaviorInterfaceFromObject

// ------------------------------------------------------------------------------------------------
ProjectileUpdateInterface* Object::getProjectileUpdateInterface() const
{
	for (BehaviorModule** m = m_behaviors; *m; ++m)
	{
		ProjectileUpdateInterface *pui = (*m)->getProjectileUpdateInterface();
		if( pui )
		{
			return pui;
		}
	}
	return nullptr;
}

// ------------------------------------------------------------------------------------------------
// Simply find the special power module that is currently allowing plotting of positions to target.
// ------------------------------------------------------------------------------------------------
SpecialPowerUpdateInterface* Object::findSpecialPowerWithOverridableDestinationActive( SpecialPowerType type ) const
{
	for( BehaviorModule** u = m_behaviors; *u; ++u )
	{
		SpecialPowerUpdateInterface *spInterface = (*u)->getSpecialPowerUpdateInterface();
		if( spInterface )
		{
			if( spInterface->doesSpecialPowerHaveOverridableDestinationActive() )
			{
				return spInterface;
			}
		}
	}  // end for
	return nullptr;
}

// ------------------------------------------------------------------------------------------------
// Simply find the special power module that is potentially allowed to plot positions to target.
// ------------------------------------------------------------------------------------------------
SpecialPowerUpdateInterface* Object::findSpecialPowerWithOverridableDestination( SpecialPowerType type ) const
{
	for( BehaviorModule** u = m_behaviors; *u; ++u )
	{
		SpecialPowerUpdateInterface *spInterface = (*u)->getSpecialPowerUpdateInterface();
		if( spInterface )
		{
			if( spInterface->doesSpecialPowerHaveOverridableDestination() )
			{
				return spInterface;
			}
		}
	}  // end for
	return nullptr;
}

// ------------------------------------------------------------------------------------------------
// Search our special ability updates for a specific one.
// ------------------------------------------------------------------------------------------------
SpecialAbilityUpdate* Object::findSpecialAbilityUpdate( SpecialPowerType type ) const
{
	for( BehaviorModule** u = m_behaviors; *u; ++u )
	{
		SpecialPowerUpdateInterface *spInterface = (*u)->getSpecialPowerUpdateInterface();
		if( spInterface && spInterface->isSpecialAbility() )
		{
			SpecialAbilityUpdate *spUpdate = static_cast<SpecialAbilityUpdate*>(spInterface);
			if( spUpdate->getSpecialPowerType() == type )
			{
				return spUpdate;
			}
		}
	}  // end for

	return nullptr;
}

// ------------------------------------------------------------------------------------------------
SpecialPowerCompletionDie* Object::findSpecialPowerCompletionDie() const
{
	static NameKeyType key_SpecialPowerCompletionDie = NAMEKEY("SpecialPowerCompletionDie");
	return reinterpret_cast<SpecialPowerCompletionDie*>(findModule(key_SpecialPowerCompletionDie));
}

//=============================================================================
const AsciiString& Object::getCommandSetString() const 
{ 
	if (m_commandSetStringOverride.isNotEmpty())
		return m_commandSetStringOverride; 

	return getTemplate()->friend_getCommandSetString();
}

//=============================================================================
Bool Object::canProduceUpgrade( const UpgradeTemplate *upgrade )
{
	// We need to have the button to make the upgrade.  CommandSets are a weird Logic/Client hybrid.
	const CommandSet *set = TheControlBar->findCommandSet(getCommandSetString());

	for( Int buttonIndex = 0; buttonIndex < MAX_COMMANDS_PER_SET; buttonIndex++ )
	{
		const CommandButton *button = set->getCommandButton(buttonIndex);
		if( button  &&  button->getUpgradeTemplate()  &&  (button->getUpgradeTemplate() == upgrade) )		
			return TRUE; // getUpgradeTemplate only returns something if it is actually an upgrade
	}

	return FALSE;// Cheatin' punk.
}
