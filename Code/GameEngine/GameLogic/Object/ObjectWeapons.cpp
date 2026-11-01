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
#define DEFINE_WEAPONCONDITIONMAP
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

// ------------------------------------------------------------------------------------------------
static const ModelConditionFlags s_allWeaponFireFlags[WEAPONSLOT_COUNT] = 
{
	MAKE_MODELCONDITION_MASK5(
		MODELCONDITION_FIRING_A,
		MODELCONDITION_BETWEEN_FIRING_SHOTS_A,
		MODELCONDITION_RELOADING_A,
		MODELCONDITION_PREATTACK_A,
		MODELCONDITION_USING_WEAPON_A
	),
	MAKE_MODELCONDITION_MASK5(
		MODELCONDITION_FIRING_B,
		MODELCONDITION_BETWEEN_FIRING_SHOTS_B,
		MODELCONDITION_RELOADING_B,
		MODELCONDITION_PREATTACK_B,
		MODELCONDITION_USING_WEAPON_B
	),
	MAKE_MODELCONDITION_MASK5(
		MODELCONDITION_FIRING_C,
		MODELCONDITION_BETWEEN_FIRING_SHOTS_C,
		MODELCONDITION_RELOADING_C,
		MODELCONDITION_PREATTACK_C,
		MODELCONDITION_USING_WEAPON_C
	)
};

// Weapons, armor sets, firing and countermeasures

//=============================================================================
void Object::setArmorSetFlag(ArmorSetType ast)
{
	m_body->setArmorSetFlag(ast);
}

//=============================================================================
void Object::clearArmorSetFlag(ArmorSetType ast)
{
	m_body->clearArmorSetFlag(ast);
}

//=============================================================================
Bool Object::testArmorSetFlag(ArmorSetType ast) const 
{ 
	return m_body->testArmorSetFlag(ast); 
}

//=============================================================================
void Object::reloadAllAmmo(Bool now)
{
	m_weaponSet.reloadAllAmmo(this, now);
}

//=============================================================================
Bool Object::isOutOfAmmo() const
{
	return m_weaponSet.isOutOfAmmo();
}

//=============================================================================
Bool Object::hasAnyWeapon() const
{
	return m_weaponSet.hasAnyWeapon();
}

//=============================================================================
Bool Object::hasAnyDamageWeapon() const
{
	//First check to see if we have any weapons -- if not return false.
	if( !m_weaponSet.hasAnyDamageWeapon() )
	{
		return FALSE;
	}
	return TRUE;
}

//=============================================================================
UnsignedInt Object::getMostPercentReadyToFireAnyWeapon() const
{
	return m_weaponSet.getMostPercentReadyToFireAnyWeapon();
}

//=============================================================================
Bool Object::hasWeaponToDealDamageType(DamageType typeToDeal) const
{
	return m_weaponSet.hasWeaponToDealDamageType(typeToDeal);
}

//=============================================================================
Real Object::getLargestWeaponRange() const
{
	Real retVal = -1;
	for (Int i = PRIMARY_WEAPON; i < WEAPONSLOT_COUNT; ++i) {
		Weapon* weapon = m_weaponSet.getWeaponInWeaponSlot(static_cast<WeaponSlotType>(i));
		if (!weapon) {
			continue;
		}

		Real tmpVal = weapon->getAttackRange(this);
		if (tmpVal > retVal) {
			retVal = tmpVal;
		}
	}
	return retVal;
}

//=============================================================================
void Object::setFiringConditionForCurrentWeapon() const
{
	if (m_drawable)
	{
		WeaponSlotType wslot = m_weaponSet.getCurWeaponSlot();
		ModelConditionFlags c = m_weaponSet.getModelConditionForWeaponSlot(wslot, WSF_FIRING);
		m_drawable->clearAndSetModelConditionFlags(s_allWeaponFireFlags[wslot], c);
	}
}

//=============================================================================
Weapon* Object::getCurrentWeapon(WeaponSlotType* wslot)
{
	if (!m_weaponSet.hasAnyWeapon())
		return nullptr;

	if (wslot)
		*wslot = m_weaponSet.getCurWeaponSlot();
	return m_weaponSet.getCurWeapon();
}

//=============================================================================
const Weapon* Object::getCurrentWeapon(WeaponSlotType* wslot) const
{
	if (!m_weaponSet.hasAnyWeapon())
		return nullptr;

	if (wslot)
		*wslot = m_weaponSet.getCurWeaponSlot();
	return m_weaponSet.getCurWeapon();
}

//=============================================================================
Weapon* Object::findWaypointFollowingCapableWeapon()
{
	return m_weaponSet.findWaypointFollowingCapableWeapon();
}

//=============================================================================
Bool Object::getAmmoPipShowingInfo(Int& numTotal, Int& numFull) const
{
/// @todo srj -- may need to cache this inside weaponset.
	const Weapon* w = m_weaponSet.findAmmoPipShowingWeapon();
	if (w)
	{
		numTotal = w->getClipSize();
		numFull = w->getRemainingAmmo();
		return true;
	}
	else
	{
		return false;
	}
}

//=============================================================================
/*
	NOTE: getAbleToAttackSpecificObject NO LONGER internally calls isAbleToAttack(), 
	since that isn't an incredibly fast call, and this is called repeatedly in some inner loops
	where we already know that isAbleToAttack() == true. so you should always
	call isAbleToAttack prior to calling this! (srj)
*/
CanAttackResult Object::getAbleToAttackSpecificObject( AbleToAttackType t, const Object* target, CommandSourceType commandSource, WeaponSlotType specificSlot ) const
{
	// NO! BAD! WRONG!
	// If we can't attack at all, then we cannot attack this
	//if (!isAbleToAttack())
	//	return FALSE;

	// Start protection: no enemy may be attacked yet.
	if ( TheGameLogic->isGraceProtected( target, this, 0 ) )
		return CanAttackResult::ATTACKRESULT_NOT_POSSIBLE;

	// Otherwise leave it up to our weapons.
	return m_weaponSet.getAbleToAttackSpecificObject( t, this, target, commandSource, specificSlot );
}

//=============================================================================
//Used for base defenses and otherwise stationary units to see if you can attack a position potentially out of range.
CanAttackResult Object::getAbleToUseWeaponAgainstTarget( AbleToAttackType attackType, const Object *victim, const Coord3D *pos, CommandSourceType commandSource, WeaponSlotType specificSlot ) const
{
	if ( TheGameLogic->isGraceProtected( victim, this, 0 ) )
		return CanAttackResult::ATTACKRESULT_NOT_POSSIBLE;
	return m_weaponSet.getAbleToUseWeaponAgainstTarget( attackType, this, victim, pos, commandSource, specificSlot );
}

//=============================================================================
Bool Object::chooseBestWeaponForTarget(const Object* target, WeaponChoiceCriteria criteria, CommandSourceType cmdSource )
{
	return m_weaponSet.chooseBestWeaponForTarget(this, target, criteria, cmdSource );
}

//DECLARE_PERF_TIMER(fireCurrentWeapon)
//=============================================================================
void Object::fireCurrentWeapon(Object *target)
{
	//USE_PERF_TIMER(fireCurrentWeapon)

	// victim may have already been destroyed
	if (target == nullptr)
		return;

	Weapon* weapon = m_weaponSet.getCurWeapon();
	if (weapon && (weapon->getStatus() == READY_TO_FIRE))
	{
		Bool reloaded = weapon->fireWeapon(this, target);
		DEBUG_ASSERTCRASH(m_firingTracker, ("hey, we are firing but have no firing tracker. this is wrong."));
		if (m_firingTracker)
			m_firingTracker->shotFired(weapon, target->getID());
		if (reloaded)
			releaseWeaponLock(LOCKED_TEMPORARILY);	// release any temporary locks.

		friend_setUndetectedDefector( FALSE );// My secret is out
	}
}

//=============================================================================
void Object::fireCurrentWeapon(const Coord3D* pos)
{ 
	//USE_PERF_TIMER(fireCurrentWeapon)

	if (pos == nullptr)
		return;

	Weapon* weapon = m_weaponSet.getCurWeapon();
	if (weapon && (weapon->getStatus() == READY_TO_FIRE))
	{
		Bool reloaded = weapon->fireWeapon(this, pos);
		DEBUG_ASSERTCRASH(m_firingTracker, ("hey, we are firing but have no firing tracker. this is wrong."));
		if (m_firingTracker)
			m_firingTracker->shotFired(weapon, INVALID_ID);
		if (reloaded)
			releaseWeaponLock(LOCKED_TEMPORARILY);	// release any temporary locks.

		friend_setUndetectedDefector( FALSE );// My secret is out
	}
}

//==============================================================================
void Object::notifyFiringTrackerShotFired( const Weapon* weaponFired, ObjectID victimID ) 
{
  if ( m_firingTracker )
    m_firingTracker->shotFired( weaponFired, victimID );
}

//=============================================================================
void Object::preFireCurrentWeapon( const Object *victim )
{
	Weapon* weapon = m_weaponSet.getCurWeapon();

	//If we are going to be capable of firing our weapon NEXT frame, set the pre-attack
	//up now. This gets called by AIAttackFireWeaponState::onEnter().. but the update happens
	//next frame.
	if (weapon && TheGameLogic->getFrame() + 1 >= weapon->getPossibleNextShotFrame() )
	{
		weapon->preFireWeapon( this, victim );
		friend_setUndetectedDefector( FALSE );// My secret is out
	}
}

// ============================================================================
/** Using the firing tracker, return the frame a shot was last fired on */
// ============================================================================
UnsignedInt Object::getLastShotFiredFrame() const
{
	UnsignedInt recent = 0;
	for (int i = 0; i < WEAPONSLOT_COUNT; ++i)
	{
		const Weapon* w = m_weaponSet.getWeaponInWeaponSlot(static_cast<WeaponSlotType>(i));
		if (w)
		{
			UnsignedInt when = w->getLastShotFrame();
			if (when > recent)
				recent = when;
		}
	}
	return recent;
}

// ============================================================================
/** Get the victim ID we last shot at */
// ============================================================================
ObjectID Object::getLastVictimID() const
{
	return m_firingTracker ? m_firingTracker->getLastShotVictim() : INVALID_ID;
} 

//-------------------------------------------------------------------------------------------------
void Object::setWeaponSetFlag(WeaponSetType wst) 
{ 
	m_curWeaponSetFlags.set(wst); 
	m_weaponSet.updateWeaponSet(this);
	if (m_drawable)
	{
		m_drawable->setModelConditionState(TheWeaponSetTypeToModelConditionTypeMap[wst]);
	}
}

//-------------------------------------------------------------------------------------------------
void Object::clearWeaponSetFlag(WeaponSetType wst) 
{ 
	m_curWeaponSetFlags.set(wst, 0); 
	m_weaponSet.updateWeaponSet(this);
	if (m_drawable)
	{
		m_drawable->clearModelConditionState(TheWeaponSetTypeToModelConditionTypeMap[wst]);
	}
}

//-------------------------------------------------------------------------------------------------
/**
 * Returns true if object currently has some kind of attack capability
 */
Bool Object::isAbleToAttack() const
{

	//******************************************************
	//********* AUTOMATICALLY FALSE CONDITIONS *************
	//******************************************************

	// For things that may or may not be able to normally attack, but are under a status condition
	if( getStatusBits().test( OBJECT_STATUS_NO_ATTACK ) )
		return false;

	// if we're contained within a transport we cannot attack unless it specifically allows us
	const Object *containedBy = getContainedBy();
	DEBUG_ASSERTCRASH( (containedBy == nullptr) || (containedBy->getContain() != nullptr), ("A %s thinks they are contained by something with no contain module!", getTemplate()->getName().str() ) );
	if( containedBy && containedBy->getContain() && !containedBy->getContain()->isPassengerAllowedToFire( getID() ) )
		return false;
	

	// We can't fire if under construction
	if( testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION) )
		return false;

	// or being sold
	if( testStatus(OBJECT_STATUS_SOLD) )
		return false;

  if ( isDisabledByType( DISABLED_SUBDUED ) )
    return FALSE; // A Microwave Tank is cooking me

	//We can't fire if we, as a portable structure, are aptly disabled 
	if ( isKindOf( KINDOF_PORTABLE_STRUCTURE ) || isKindOf( KINDOF_SPAWNS_ARE_THE_WEAPONS ))
	{
		if( isDisabledByType( DISABLED_HACKED ) || isDisabledByType( DISABLED_EMP ) )
			return false;

    if ( isKindOf( KINDOF_INFANTRY ) ) // I must be a stinger soldier or similar
    {
      for (BehaviorModule** update = getBehaviorModules(); *update; ++update)//expensive search, limited only to stinger soldiers
      {
	      SlavedUpdateInterface* sdu = (*update)->getSlavedUpdateInterface();
	      if ( sdu )
	      {
          ObjectID slaverID = sdu->getSlaverID();
          if ( slaverID != INVALID_ID )
          {
            Object *slaver = TheGameLogic->findObjectByID( slaverID );
            if ( slaver && slaver->isDisabledByType( DISABLED_SUBDUED ))
              return FALSE;// if my stinger site is subdued, so am I
          }

          break;//only expect one slavedupdate, so stop searching
	      }
      }
    }


	}

  

	//We can't fire if all our weapons are disabled! 
	//Currently, only turreted weapons can be disabled.
	//ONLY DO THIS CHECK IF OUR UNIT DOESN'T HAVE THE
	//KINDOF_CAN_ATTACK flag... nuke cannons have disabled
	//turrets when not deployed, and need to be able to attack to deploy!
	//Strategy centers can't attack when bombardment isn't active!
	Bool anyEnabled = FALSE;
	Bool anyWeapon = FALSE;
	const AIUpdateInterface *ai = getAI();
	if( ai && !isKindOf( KINDOF_CAN_ATTACK ) )
	{
		for( Int i = 0; i < WEAPONSLOT_COUNT;	i++ )
		{
			//Find the weapon in this slot.
			Weapon* weapon = getWeaponInWeaponSlot( static_cast<WeaponSlotType>(i) );
			if( !weapon )
				continue;

			anyWeapon = TRUE;
			
			//We found a weapon, is it a turret?
			Real dummy;
			WhichTurretType tur = ai->getWhichTurretForWeaponSlot( static_cast<WeaponSlotType>(i), &dummy );
			if( tur == TURRET_INVALID )
			{
				//Currently impossible to disable a non-turreted weapon, so we
				//have a non turreted weapon that is enabled. Quit.
				anyEnabled = TRUE;
				break;
			}
			
			if( ai->isTurretEnabled( tur ) )
			{
				//The turret is enable, meaning we have an enabled weapon. Quit.
				anyEnabled = TRUE;
				break;;	
			}
		}
		if( anyWeapon && !anyEnabled )
		{
			//We failed to find any active weapons.
			return FALSE;
		}
	}


	//***************************************
	//********* TRUE CONDITIONS *************
	//***************************************

	// for certain buildings
	if (isKindOf(KINDOF_CAN_ATTACK))
		return true;

	// for garrisonned buildings that can attack sometimes
	if( getStatusBits().test( OBJECT_STATUS_CAN_ATTACK ) )
		return true;
	
	// for weaponless transports.  This will make me think I can, but I will check if I literally can by looking
	// at passenger weapons in CanAttack.
	const ContainModuleInterface* contain = getContain();
	if( contain && contain->isPassengerAllowedToFire( getID() ) && contain->getContainCount() > 0 )
		return true;

	// if we have AI and a weapon, assume we know how to use it
	if (getAIUpdateInterface() != nullptr && m_weaponSet.hasAnyWeapon())
	{

// actually, we don't want to do this; we want the troop crawler to be considered "able to attack"
// even if empty, so sayeth Dustin. (srj)
//		// special case: if the only damage we do is DEPLOY, we must have some guys contained.
//		if (m_weaponSet.hasSingleDamageType(DAMAGE_DEPLOY))
//		{
//			return contain->getContainCount() > 0;
//		}
//		else
		{
			return true;
		}
	}

	SpawnBehaviorInterface *spawnInterface = getSpawnBehaviorInterface();
	if( spawnInterface )
	{
		if( spawnInterface->canAnySlavesAttack() )
		{
			return TRUE;
		}
	}

	if (getTemplate()->isEnterGuard())
		return TRUE;

//Default is no
	return false;
}

//-------------------------------------------------------------------------------------------------
void Object::setWeaponBonusCondition(WeaponBonusConditionType wst) 
{
	WeaponBonusConditionFlags oldCondition = m_weaponBonusCondition;
	m_weaponBonusCondition |= (1 << wst); 

	if( oldCondition != m_weaponBonusCondition )
	{
		// Our weapon bonus just changed, so we need to immediately update our weapons
		m_weaponSet.weaponSetOnWeaponBonusChange(this);
	}
}

//-------------------------------------------------------------------------------------------------
void Object::clearWeaponBonusCondition(WeaponBonusConditionType wst) 
{
	WeaponBonusConditionFlags oldCondition = m_weaponBonusCondition;
	m_weaponBonusCondition &= ~(1 << wst); 

	if( oldCondition != m_weaponBonusCondition )
	{
		// Our weapon bonus just changed, so we need to immediately update our weapons
		m_weaponSet.weaponSetOnWeaponBonusChange(this);
	}
}

//-------------------------------------------------------------------------------------------------
/** 
	A weapon cannot be in charge of maintaining condition flags as it is all event driven.
	I will maintain my ModelCondition myself if it should change.  Firing is set by firing logic,
	so I don't include it here.  It is only the states that expire on timers that noone watches
	that I am concerned with.
*/
//-------------------------------------------------------------------------------------------------
void Object::adjustModelConditionForWeaponStatus()
{
	UnsignedInt now = TheGameLogic->getFrame();

	for (int i = 0; i < WEAPONSLOT_COUNT; ++i)
	{
		const Weapon* w = m_weaponSet.getWeaponInWeaponSlot(static_cast<WeaponSlotType>(i));
		if (!w)
		{
			m_lastWeaponCondition[i] = WSF_NONE;
			continue;
		}
		
		WeaponSetConditionType conditionToSet = WSF_INVALID;
		if (i != m_weaponSet.getCurWeaponSlot())
		{
			// if this isn't the current weapon, then we never set ANYTHING for it.
			conditionToSet = WSF_NONE;
		}
		else if (w->getLastShotFrame() == now)
		{
			// yep, this overrides any weapon-status condition!
			conditionToSet = WSF_FIRING;
		}
		else if (!testStatus( OBJECT_STATUS_IS_ATTACKING ))
		{
			// srj sez: not 100% sure about this one, but the problem is: say we were attacking,
			// then issue a move command. if we didn't do this here, we might still have a 'firing'
			// pose, because his weapon might be in 'reloading' mode. since we're not attacking, however,
			// we really don't care, so we just force the issue here. (This might still need tweaking for the pursue state.)
			conditionToSet = WSF_NONE;
		}
		else
		{
			WeaponStatus newStatus = w->getStatus();

			const static WeaponSetConditionType s_wsfLookup[WEAPON_STATUS_COUNT] =
			{
				WSF_NONE,				// READY_TO_FIRE,
				WSF_NONE,				// OUT_OF_AMMO,
				WSF_BETWEEN,		// BETWEEN_FIRING_SHOTS,
				WSF_RELOADING,	// RELOADING_CLIP,
				WSF_PREATTACK		// PRE_ATTACK,
			};
			conditionToSet = s_wsfLookup[newStatus];

			// special case this: say we are firing in bursts: pow-pow-pow-pause, etc.
			// then we might have a frame where we have reloaded and are ready-to-fire,
			// but haven't fired yet this frame. in that case, use 'between' so we still have
			// a firing pose, 'cuz if we use 'none' we will 'pop' back to idle for a frame. (srj)
			// additional note: only do if aiming or firing, since we could also be in this state if 
			// we are approaching or pursuing a target! (srj)
			if (newStatus == READY_TO_FIRE && conditionToSet == WSF_NONE && testStatus( OBJECT_STATUS_IS_ATTACKING ) &&
					(testStatus( OBJECT_STATUS_IS_AIMING_WEAPON ) || testStatus( OBJECT_STATUS_IS_FIRING_WEAPON )))
			{
				conditionToSet = WSF_BETWEEN;
			}

		}

		if (m_drawable)
		{
			m_drawable->updateDrawableClipStatus( w->getRemainingAmmo(), w->getClipSize(), w->getWeaponSlot() );
			if (conditionToSet != WSF_INVALID && conditionToSet != m_lastWeaponCondition[i])
			{
				m_lastWeaponCondition[i] = static_cast<Byte>(conditionToSet);
				ModelConditionFlags c = m_weaponSet.getModelConditionForWeaponSlot(static_cast<WeaponSlotType>(i), conditionToSet);
				m_drawable->clearAndSetModelConditionFlags(s_allWeaponFireFlags[i], c);
				if (conditionToSet == WSF_PREATTACK)
				{
					// in the preattack state, adjust the speed of the preattack anim to match the actual time it will take
					UnsignedInt preAttackDone = w->getPreAttackFinishedFrame();
					if (preAttackDone > now)
						m_drawable->setAnimationLoopDuration(preAttackDone - now);
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void Object::clearLeechRangeModeForAllWeapons()
{
	m_weaponSet.clearLeechRangeModeForAllWeapons();
}

// ------------------------------------------------------------------------------------------------
Int Object::getNumConsecutiveShotsFiredAtTarget( const Object *victim ) const
{
	return m_firingTracker ? m_firingTracker->getNumConsecutiveShotsAtVictim( victim ) : 0;
}

//-------------------------------------------------------------------------------------------------
CountermeasuresBehaviorInterface* Object::getCountermeasuresBehaviorInterface()
{
	for( BehaviorModule** i = getBehaviorModules(); *i; ++i )
	{
		CountermeasuresBehaviorInterface* cbi = (*i)->getCountermeasuresBehaviorInterface();
		if( cbi )
		{
			return cbi;
		}
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
const CountermeasuresBehaviorInterface* Object::getCountermeasuresBehaviorInterface() const
{
	for( BehaviorModule** i = getBehaviorModules(); *i; ++i )
	{
		const CountermeasuresBehaviorInterface* cbi = (*i)->getCountermeasuresBehaviorInterface();
		if( cbi )
		{
			return cbi;
		}
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
Bool Object::hasCountermeasures() const
{
	const CountermeasuresBehaviorInterface* cbi = getCountermeasuresBehaviorInterface();
	if( cbi && cbi->isActive() )
	{
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void Object::reportMissileForCountermeasures( Object *missile )
{
	for( BehaviorModule** i = getBehaviorModules(); *i; ++i )
	{
		CountermeasuresBehaviorInterface* cbi = (*i)->getCountermeasuresBehaviorInterface();
		if( cbi )
		{
			cbi->reportMissileForCountermeasures( missile );
		}
	}
}

//-------------------------------------------------------------------------------------------------
ObjectID Object::calculateCountermeasureToDivertTo( const Object& victim )
{
	AIUpdateInterface *ai = getAI();
	if( ai )
	{
		for( BehaviorModule** i = victim.getBehaviorModules(); *i; ++i )
		{
			CountermeasuresBehaviorInterface* cbi = (*i)->getCountermeasuresBehaviorInterface();
			if( cbi )
			{
				ObjectID decoyID = cbi->calculateCountermeasureToDivertTo( victim );
				return decoyID;
			}
		}
	}
	return INVALID_ID;
}
