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

// Damage, healing, death, disabled states and kills

//-------------------------------------------------------------------------------------------------
/** Something is attempting to damage this object */
//-------------------------------------------------------------------------------------------------
void Object::attemptDamage( DamageInfo *damageInfo )
{
	BodyModuleInterface* body = getBodyModule();
	if (body)
		body->attemptDamage( damageInfo );
			
	// Process any shockwave forces that might affect this object due to the incurred damage
	if (damageInfo->in.m_shockWaveAmount > 0.0f && damageInfo->in.m_shockWaveRadius > 0.0f)
	{
	  //KindOfMaskType immuneToShockwaveKindofs;                                                                      //NEW RESTRICTIONS ADDED
	  //immuneToShockwaveKindofs.set(KINDOF_PROJECTILE);// projectiles go idle in midair when they get sw'd           //NEW RESTRICTIONS ADDED
	  //immuneToShockwaveKindofs.set(KINDOF_PRODUCED_AT_HELIPAD);//helicopters go all wonky when they get shockwaved  //NEW RESTRICTIONS ADDED

		PhysicsBehavior *behavior = getPhysics();
		if ( behavior && (isAirborneTarget() == FALSE) && (! isKindOf(KINDOF_PROJECTILE) ) )
//		if (behavior && isAnyKindOf( immuneToShockwaveKindofs ) == FALSE )//NEW RESTRICTIONS ADDED
		{ 
			// Calculate the shockwave taperoff amount due to distance from ground zero
			Real shockWaveScalar = damageInfo->in.m_shockWaveVector.length();
			Real distanceFromCenter = min(1.0f, shockWaveScalar / damageInfo->in.m_shockWaveRadius); 
			Real distanceTaper = (distanceFromCenter) * (1.0f - damageInfo->in.m_shockWaveTaperOff);
			Real shockTaperMult = 1.0f - distanceTaper;

			// Set up the shockwave force to use apply on object
			Coord3D shockWaveForce;
			shockWaveForce.set( &damageInfo->in.m_shockWaveVector );
			shockWaveForce.normalize();
			shockWaveForce.scale( damageInfo->in.m_shockWaveAmount * shockTaperMult );
			shockWaveForce.z = shockWaveForce.length(); // Apply up force equal to the lateral force for dramatic effect

			// Apply the shock to the object
			behavior->applyShock(&shockWaveForce);

			// Add random rotation to the object for drama
      
			behavior->applyRandomRotation();

			// Set stunned state due to the shock for the object
      behavior->setStunned(true);
			
      setModelConditionState(MODELCONDITION_STUNNED_FLAILING);
		}
	}

	
	/// @todo track damage dealt/attempted

	//
	// if actual damage occurred, and this is an object owned by the local player we 
	// might do a radar event for under attack. Note that we do not even try
	// to do radar events for DAMAGE_PENALTY as that damage type is a type of damage
	// that occurs with explicit player knowledge
	//
	if( damageInfo->out.m_actualDamageDealt > 0.0f &&
			damageInfo->in.m_damageType != DAMAGE_PENALTY &&
			damageInfo->in.m_damageType != DAMAGE_HEALING &&
			getControllingPlayer() &&
			!BitTest(damageInfo->in.m_sourcePlayerMask, getControllingPlayer()->getPlayerMask()) && 
			m_radarData != nullptr &&
			getControllingPlayer() == ThePlayerList->getLocalPlayer() )
		TheRadar->tryUnderAttackEvent( this );

}

//-------------------------------------------------------------------------------------------------
void Object::attemptHealing(Real amount, const Object* source)
{
	BodyModuleInterface* body = getBodyModule();
	if (body)
	{
		DamageInfo damageInfo;
		damageInfo.in.m_damageType = DAMAGE_HEALING;
		damageInfo.in.m_deathType = DEATH_NONE;
		damageInfo.in.m_sourceID = source ? source->getID() : INVALID_ID;
		damageInfo.in.m_amount = amount;
		body->attemptHealing( &damageInfo );
	}
}

ObjectID Object::getSoleHealingBenefactor( void ) const 
{
	UnsignedInt now = TheGameLogic->getFrame();
	if( now > m_soleHealingBenefactorExpirationFrame )
		return INVALID_ID;

	return	m_soleHealingBenefactorID; 

}

Bool Object::attemptHealingFromSoleBenefactor ( Real amount, const Object* source, UnsignedInt duration )
{///< for the non-stacking healers like ambulance and propaganda

	if( ! source ) // sanity
		return FALSE;

	UnsignedInt now = TheGameLogic->getFrame();
	ObjectID id = source->getID();

// Either it is ok to accept healing from any who offer or this is my guy, calling again
	if( now > m_soleHealingBenefactorExpirationFrame || m_soleHealingBenefactorID == id ) 
	{
		m_soleHealingBenefactorID = id;
		m_soleHealingBenefactorExpirationFrame = now + duration;

		BodyModuleInterface* body = getBodyModule();
		if (body)
		{
			DamageInfo damageInfo;
			damageInfo.in.m_damageType = DAMAGE_HEALING;
			damageInfo.in.m_deathType = DEATH_NONE;
			damageInfo.in.m_sourceID = source ? source->getID() : INVALID_ID;
			damageInfo.in.m_amount = amount;
			body->attemptHealing( &damageInfo );
		}

		return TRUE;
	}

	return FALSE;

}

//-------------------------------------------------------------------------------------------------
Real Object::estimateDamage( DamageInfoInput& damageInfo ) const
{
	BodyModuleInterface* body = getBodyModule();
	if (body)
		return body->estimateDamage( damageInfo );
			
	return 0.0f;
}

//-------------------------------------------------------------------------------------------------
/** Do so much damage to an object that it will certainly die */
//-------------------------------------------------------------------------------------------------
void Object::kill( DamageType damageType, DeathType deathType )
{
	DamageInfo damageInfo;

	// Do unmodifiable damage equal to their max health to kill.
	damageInfo.in.m_damageType = damageType;
	damageInfo.in.m_deathType = deathType;
	damageInfo.in.m_sourceID = INVALID_ID;
	damageInfo.in.m_amount = getBodyModule()->getMaxHealth();
	damageInfo.in.m_kill = TRUE; // Triggers object to die no matter what.
	attemptDamage( &damageInfo );

	DEBUG_ASSERTCRASH(!damageInfo.out.m_noEffect, ("Attempting to kill an unKillable object (InactiveBody?)\n"));

}  // end kill

//-------------------------------------------------------------------------------------------------
/** Restore max health to this Object */
//-------------------------------------------------------------------------------------------------
void Object::healCompletely()	
{
	attemptHealing(HUGE_DAMAGE_AMOUNT, nullptr);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void Object::setEffectivelyDead(Bool dead)
{
	if (dead)
		BitSet(m_privateStatus, EFFECTIVELY_DEAD);
	else
		BitClear(m_privateStatus, EFFECTIVELY_DEAD);

	if (dead)
	{
		if( m_radarData )
			TheRadar->removeObject( this );
	}
}

//-------------------------------------------------------------------------------------------------
void Object::setCaptured(Bool isCaptured)
{
	if (isCaptured)
		BitSet(m_privateStatus, CAPTURED);
	else 
	{
		DEBUG_LOG(("Clearing Captured Status. This should never happen. jkmcd"));
		BitClear(m_privateStatus, CAPTURED);
	}

	// No need to see if we should skip updates, this flag has no effect on skipping updates.
}

//-------------------------------------------------------------------------------------------------
//- DISABLEDNESS STUFF ----------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void Object::setDisabled( DisabledType type )
{
	setDisabledUntil(type, FOREVER);
}

//-------------------------------------------------------------------------------------------------
void Object::setDisabledUntil( DisabledType type, UnsignedInt frame )
{
	Bool edgeCase = !isDisabled();

	if( type < 0 || type >= DISABLED_COUNT )
	{
		DEBUG_CRASH( ("Invalid disabled type value %d specified -- doesn't not exist!", type ) );
		return;
	}

	//Handle audio events!
 	AudioEventRTS sound;
	if( type == DISABLED_UNMANNED && !isKindOf( KINDOF_DRONE ) )
	{
		//We've been sniped! Play a splatter sound for the pilot losing his face.
		sound = TheLogicAudio->getMiscAudio()->m_splatterVehiclePilotsBrain;
		sound.setPosition( getPosition() );
		TheLogicAudio->addAudioEvent( &sound );
	}
	else if( type == DISABLED_UNDERPOWERED || type == DISABLED_EMP || type == DISABLED_SUBDUED || type == DISABLED_HACKED )
	{
		//We've lost power -- make sure we aren't already out of power as the sounds shouldn't happen
		//if you were already disabled.
		if( !isDisabledByType( DISABLED_UNDERPOWERED ) && 
				!isDisabledByType( DISABLED_EMP ) &&
				!isDisabledByType( DISABLED_SUBDUED ) &&
				!isDisabledByType( DISABLED_HACKED ) )
		{
			if( isKindOf( KINDOF_STRUCTURE ) )
			{
				sound = TheLogicAudio->getMiscAudio()->m_buildingDisabled;
				sound.setPosition( getPosition() );
				TheLogicAudio->addAudioEvent( &sound );
			}
			else if( isKindOf( KINDOF_VEHICLE ) )
			{
				sound = TheLogicAudio->getMiscAudio()->m_vehicleDisabled;
				sound.setPosition( getPosition() );
				TheLogicAudio->addAudioEvent( &sound );
			}
		}
	}

	if( m_disabledTillFrame[ type ] != frame )
	{
		// an edge-test for disabledness, for type. This INCREMENTS m_pauseCount
		// srj sez: HELD nevers disables special powers.
		if ( type != DISABLED_HELD && !isDisabledByType( type ) )
			pauseAllSpecialPowers( TRUE );
		
		m_disabledTillFrame[ type ] = frame;
		m_disabledMask.set( type, frame > TheGameLogic->getFrame() );

		if( m_drawable )
		{
			if( isDisabled() )
			{
				// Held does not tint anybody.  If we are multiply disabled, the other setting will hit the tint,
				// and in clear, only-held and not-disabled are both causes to untint.
				// Doh. Also shouldn't be tinting when disabled by scripting.
				// Doh^2. Also shouldn't be CLEARING tinting if we're disabling by held or script disabledness
				// Doh^3. Unmanned is no tint too
				if( type != DISABLED_HELD && type != DISABLED_SCRIPT_DISABLED && type != DISABLED_UNMANNED )
				{
					TheLogicToClient->drawable( m_drawable, []( Drawable *d ) { d->setTintStatus( TINT_STATUS_DISABLED ); } );
				}
			}
		}

		ContainModuleInterface *contain = getContain();
		if ( contain )
		{
			Object *rider = const_cast<Object*>(contain->friend_getRider());
			if ( rider )
			{
				rider->setDisabledUntil(type, frame);
			}
		}

		if ( isKindOf( KINDOF_SPAWNS_ARE_THE_WEAPONS ) )
		{
			SpawnBehaviorInterface *sbi = this->getSpawnBehaviorInterface();
			if ( sbi )
			{
				//Kris: Patch 1.01 - November 12, 2003
				//Actually, we want to disable the slaves, not order them to go idle! This fix was made to
				//stinger sites getting hit by an EMP to prevent the soldiers from attacking.
				//sbi->orderSlavesToGoIdle( CMD_FROM_AI ); // the canattack() will take care of any future attempts to fire
				sbi->orderSlavesDisabledUntil( type, frame );
			}

		}

	}

	if( type == DISABLED_UNMANNED && !isKindOf( KINDOF_DRONE ) )
	{
		//strange but true: If I am a carbomb, 
		//my driver actually has a dead-man's 
		//trigger for my dynamite... 
		//If he gets sniped, I blow up! Wheeee!

		WeaponSetFlags flags;
		flags.set( WEAPONSET_CARBOMB );
		const WeaponTemplateSet* set = getTemplate()->findWeaponTemplateSet( flags );
		if( set && set->testWeaponSetFlag( WEAPONSET_CARBOMB ) )
		{
			Object* sniper = TheGameLogic->findObjectByID( getBodyModule()->getLastDamageInfo()->in.m_sourceID );
			if ( sniper )
				sniper->scoreTheKill( this );
			
			kill();
		}
		else
		{
			//This vehicle's pilot has been sniped, so we want to clear the veterancy rating (if any)
			ExperienceTracker *xpTracker = getExperienceTracker();
			if( xpTracker )
			{
				xpTracker->setExperienceAndLevel( 0, FALSE );
			}
			//Not only that, but it also loses any healing bonuses it may have earned in its prior life
			{
				static const NameKeyType key_AutoHealBehavior = NAMEKEY("AutoHealBehavior");
				AutoHealBehavior* autoHeal = static_cast<AutoHealBehavior*>(findUpdateModule( key_AutoHealBehavior ));
				if (autoHeal)
					autoHeal->undoUpgrade();
				

			}
		}
		
	}
  
	// This will only be called if we were NOT disabled before coming into this function.
	if (edgeCase) {
		onDisabledEdge(true);
	}
}

//-------------------------------------------------------------------------------------------------
UnsignedInt Object::getDisabledUntil( DisabledType type ) const
{
	if( type == DISABLED_ANY )
	{
		UnsignedInt highestFrame = 0;
		//Iterate through each disabled type and return the one with the highest frame.
		for( Int i = 0; i < DISABLED_COUNT; i++ )
		{
			if( m_disabledMask.test( i ) && m_disabledTillFrame[ i ] > highestFrame )
			{
				highestFrame = m_disabledTillFrame[ i ];
			}
		}
		return highestFrame;
	}
	else if( m_disabledMask.test( type ) )
	{
		//Specific query.
		return m_disabledTillFrame[ type ];
	}
	//Not disabled.
	return 0;
}

//-------------------------------------------------------------------------------------------------
Bool Object::clearDisabled( DisabledType type )
{
	if( type < 0 || type >= DISABLED_COUNT )
	{
		DEBUG_CRASH( ("Invalid disabled type value %d specified -- doesn't not exist!", type ) );
		return FALSE;
	}

	if (!isDisabledByType(type)) {
		return FALSE;
	}

	if( type == DISABLED_UNDERPOWERED || type == DISABLED_EMP || type == DISABLED_SUBDUED || type == DISABLED_HACKED )
	{
		//We've regained power-- make sure we aren't still disabled by another type.
	 	AudioEventRTS sound;
		if( (!isDisabledByType( DISABLED_UNDERPOWERED ) || type == DISABLED_UNDERPOWERED ) &&
				(!isDisabledByType( DISABLED_EMP ) || type == DISABLED_EMP ) &&
				(!isDisabledByType( DISABLED_SUBDUED ) || type == DISABLED_SUBDUED ) &&
				(!isDisabledByType( DISABLED_HACKED ) || type == DISABLED_HACKED ) )
		{
			if( isKindOf( KINDOF_STRUCTURE ) )
			{
				sound = TheLogicAudio->getMiscAudio()->m_buildingReenabled;
				sound.setPosition( getPosition() );
				TheLogicAudio->addAudioEvent( &sound );
			}
			else if( isKindOf( KINDOF_VEHICLE ) )
			{
				sound = TheLogicAudio->getMiscAudio()->m_vehicleReenabled;
				sound.setPosition( getPosition() );
				TheLogicAudio->addAudioEvent( &sound );
			}
		}
	}


	// an edge-test for disabledness, for type. This DECREMENTS m_pauseCount
	// srj sez: HELD nevers disables special powers.
	if ( type != DISABLED_HELD && isDisabledByType( type ) )
		pauseAllSpecialPowers( FALSE );

	ContainModuleInterface *contain = getContain();
	if ( contain )
	{
		// We explicitly pass stuff in up in the set, so we need to turn it off if it is a forever type
		Object *rider = const_cast<Object*>(contain->friend_getRider());
		if( rider  &&  (m_disabledTillFrame[ type ] == FOREVER) )
		{
			rider->clearDisabled(type);
		}
	}

	if ( isKindOf( KINDOF_SPAWNS_ARE_THE_WEAPONS ) )
	{
		SpawnBehaviorInterface *sbi = this->getSpawnBehaviorInterface();
		if ( sbi )
		{
			//Kris: Patch 1.02 - December 17, 2003
			//Make sure slaves can recover from being disabled by subdual (stinger site soldier case)
			sbi->orderSlavesToClearDisabled( type );
		}

	}

	m_disabledTillFrame[ type ] = NEVER;
	m_disabledMask.set( type, 0 );

	DisabledMaskType exceptions;
	exceptions.set(DISABLED_HELD);
	exceptions.set(DISABLED_SCRIPT_DISABLED);
	exceptions.set(DISABLED_UNMANNED);

	DisabledMaskType myFlagsMinusExceptions = getDisabledFlags();
	myFlagsMinusExceptions.clearAndSet(exceptions, DISABLEDMASK_NONE);

	// to clarify, if I am NOT disabled by anything other than DISABLED_HELD, or DISABLED_SCRIPT_DISABLED
	
	// to clarify, count inverse intersection gives you the number of exceptions you don't have, 
	// and has nothing to do with checking other disabled types
//	if( !isDisabled() || getDisabledFlags().countInverseIntersection( exceptions ) == 0 )  
	if( myFlagsMinusExceptions.count() == 0 )  
	{
		// I have no disabled flag that is not one of the exceptions above.
		if (m_drawable)
			TheLogicToClient->drawable( m_drawable, []( Drawable *d ) { d->clearTintStatus( TINT_STATUS_DISABLED ); } );
	}

	checkDisabledStatus();// in case we just edged

	// if we're no longer disabled by anything, then call the edge function.
	if (!isDisabled()) {
		onDisabledEdge(false);
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
//Checks any timers and clears disabled statii that have expired.
//-------------------------------------------------------------------------------------------------
void Object::checkDisabledStatus()
{
	UnsignedInt now = TheGameLogic->getFrame();
	for( int i = 0; i < DISABLED_COUNT; i++ )
	{
		DisabledType type = static_cast<DisabledType>(i);
		if( isDisabledByType( type ) )
		{
			if ( now >= m_disabledTillFrame[ i ] )
			{
				clearDisabled( type ); // This will also DECREMENT m_pauseCount in all specialpowers
				m_disabledMask.set( type, 0 );
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void Object::pauseAllSpecialPowers( const Bool disabling ) const
{ 
	for (BehaviorModule** m = m_behaviors; *m; ++m)
	{
		SpecialPowerModuleInterface* sp = (*m)->getSpecialPower();
		if (!sp)
			continue;

		sp->pauseCountdown( disabling );// So it will pause if we are disabling.
	}
}

//-------------------------------------------------------------------------------------------------
void Object::scoreTheKill( const Object *victim )
{
	// Do stuff that has nothing to do with experience points here, like tell our Player we killed something
	/// @todo Multiplayer score hook location?

	Player* victimController = victim->getControllingPlayer();
	// if the other player is not a playable side (i.e. they are civilian, observer, whatever)
	// we shouldn't count the kill.
	if (victimController->isPlayableSide() == FALSE)
	{
		return;
	}

	
	if ( victim->isKindOf( KINDOF_IGNORED_IN_GUI ) )
		return;


	Player* controller = getControllingPlayer();

	if (victimController)
	{
		victimController->getScoreKeeper()->addObjectLost(victim);
		AIStats_logKill( this, victim );
	}

	Relationship r = getRelationship(victim);
	if (r != ENEMIES)
		return;

	// Don't count kills that I do on my own buildings or units, cause thats just silly.
	if (controller == victimController)
	{
		return;
	}

	if (controller)
	{
		controller->getScoreKeeper()->addObjectDestroyed(victim);
		controller->addSkillPointsForKill(this, victim);
		controller->doBountyForKill(this, victim);
	}

	// Now handle experience, if we can gain any
	if (m_experienceTracker && m_experienceTracker->isAcceptingExperiencePoints())
	{
		// srj sez: per dustin, no experience (et al) for killing things under construction.
		if (!victim->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
		{
			Int experienceValue = victim->getExperienceTracker()->getExperienceValue( this );
			getExperienceTracker()->addExperiencePoints( experienceValue );
		}
	}
}

//-------------------------------------------------------------------------------------------------
void Object::onDisabledEdge(Bool becomingDisabled)
{
	// rip through the behavior modules and call the onDisabledEdge for any modules that care
	for( BehaviorModule **module = m_behaviors; *module; ++module )
		(*module)->onDisabledEdge( becomingDisabled );

	DozerAIInterface *dozerAI = getAI() ? getAI()->getDozerAIInterface() : nullptr;
	if( becomingDisabled  &&  dozerAI )
	{
		// Have to say goodbye to the thing we might be building or repairing so someone else can do it.
		if( dozerAI->getCurrentTask() != DOZER_TASK_INVALID )
			dozerAI->cancelTask( dozerAI->getCurrentTask() );
	}

	Player* controller = getControllingPlayer();
	// can be called during game teardown, thus controller can be null
	if (controller)
	{
		//@todo jkmcd - Colin suggested we rewrite this to use the interface stuff. I agree, but need
		// to get some more bugs fixed today. 
		static NameKeyType radar = NAMEKEY("RadarUpgrade");
		Module *mod = mod = findModule(radar);
		if (mod) {
			RadarUpgrade *radarMod = static_cast<RadarUpgrade*>(mod);
			if (radarMod->isAlreadyUpgraded()) {
				// Need to decrement the count here, because we own a radar upgrade
				if (becomingDisabled) {
					controller->removeRadar(radarMod->getIsDisableProof());
				} else {
					controller->addRadar(radarMod->getIsDisableProof());
				}
			}
		}
	}

	// We will need to adjust power ... somehow ...
	Int powerToAdjust = getTemplate()->getEnergyProduction();
	
	if( powerToAdjust > 0 )
	{
		// We can't affect something that consumes, or else we go low power which removes the consumption
		// which makes us not low power so we add the consumption so we go low power...
		// This check also guaards the IsDisabled in friend_adjustPower above
		static NameKeyType powerPlant = NAMEKEY("PowerPlantUpgrade");
		static NameKeyType overCharge = NAMEKEY("OverchargeBehavior");
		
		Module* mod = findModule(powerPlant);
		if (mod) {
			PowerPlantUpgrade *powerPlantMod = static_cast<PowerPlantUpgrade*>(mod);
			if (powerPlantMod->isAlreadyUpgraded()) {
				powerToAdjust += getTemplate()->getEnergyBonus();
			}
		}
		
		mod = findModule(overCharge);
		if (mod) {
			OverchargeBehavior *overChargeMod = static_cast<OverchargeBehavior*>(mod);
			if (overChargeMod->isOverchargeActive()) {
				powerToAdjust += getTemplate()->getEnergyBonus();
			}
		}
		
		// Now, adjust the power for the player.
		if (controller)
			controller->getEnergy()->adjustPower(powerToAdjust, !becomingDisabled);
	}
}

//-------------------------------------------------------------------------------------------------
/// Object level events that need to happen upon game death
void Object::onDie( DamageInfo *damageInfo )
{

	checkAndDetonateBoobyTrap(nullptr);// Already dying, so no need to handle death case of explosion

#if defined(_DEBUG)
	DEBUG_ASSERTCRASH(m_hasDiedAlready == false, ("Object::onDie has been called multiple times. This is invalid. jkmcd"));
	m_hasDiedAlready = true;
#endif
	
	Bool selfInflicted = (damageInfo->in.m_sourceID == getID());

	// FIRST, call our die modules.
	for (BehaviorModule** d = m_behaviors; *d; ++d)
	{
		DieModuleInterface* die = (*d)->getDie();
		if (die)
			die->onDie(damageInfo);
	}

	// When objects die we remove from the radar as they're really not interesting anymore
	if( m_radarData )
		TheRadar->removeObject( this );
	
	// Just in case I have been sporting one of thise fancy Terrain Decals,
	//I naturally lose it now, because I'm dead.
	Drawable *draw = getDrawable();
	TheLogicToClient->drawable( draw, []( Drawable *d ) { d->setTerrainDecalFadeTarget(0.0f, -0.03f); } );//fade...
	//if (draw) draw->setTerrainDecal(TERRAIN_DECAL_NONE);//pop!


	// objects that were spawned from something, need to tell their spawner that they have died
	Object* spawner = TheGameLogic->findObjectByID( getProducerID() );
	if( spawner )
	{

		// get the spawn behavior interface of the spawner
		SpawnBehaviorInterface *spawnerBehavior = spawner->getSpawnBehaviorInterface();
		if( spawnerBehavior )
			spawnerBehavior->onSpawnDeath( getID(), damageInfo );

	}

	handlePartitionCellMaintenance();
	if(m_team)
		m_team->notifyTeamOfObjectDeath();

	// Death sound from the object data (SoundDie, SoundDieFire, SoundDieToxin), as Generals plays it.
	// Zero Hour plays death sounds through the FX lists of the die modules; its data sets none of the
	// three fields, so the block is skipped there.
	if( getTemplate()->getSoundDie()->getEventName().isNotEmpty() ||
			getTemplate()->getSoundDieFire()->getEventName().isNotEmpty() ||
			getTemplate()->getSoundDieToxin()->getEventName().isNotEmpty() )
	{
		// Play death sound here.

		AudioEventRTS deathSound = *getTemplate()->getSoundDie();
		// If we were killed by fire, or by poison, we should play those die sounds instead of the usual
		// sound
		if (damageInfo->in.m_deathType == DEATH_BURNED)
			deathSound = *getTemplate()->getSoundDieFire();
		else if (damageInfo->in.m_deathType == DEATH_POISONED || damageInfo->in.m_deathType == DEATH_POISONED_BETA)
			deathSound = *getTemplate()->getSoundDieToxin();

		// If we didn't actually have a specialized die sound (for the case of fire or poison, we
		// should use the generic death sound)
		if (!TheLogicAudio->isValidAudioEvent(&deathSound))
			deathSound = *getTemplate()->getSoundDie();

		// Use the position. Next frame, when this unit is gone, this sound will be clipped because we
		// can no longer automatically find its position. - jkmcd
		deathSound.setPosition(getPosition());
		PlayerIndex index = getControllingPlayer() ? getControllingPlayer()->getPlayerIndex() : 0;
		deathSound.setPlayerIndex( index );
		TheLogicAudio->addAudioEvent(&deathSound);
	}

	if (isLocallyControlled() && !selfInflicted) // wasLocallyControlled? :-)
	{
		if (isKindOf(KINDOF_STRUCTURE) && isKindOf(KINDOF_MP_COUNT_FOR_VICTORY)) 
		{
			TheLogicToClient->evaSetShouldPlay(EVA_BuldingLost);
		} 
		else if (isKindOf(KINDOF_INFANTRY) || isKindOf(KINDOF_VEHICLE)) 
		{
			TheLogicToClient->evaSetShouldPlay(EVA_UnitLost);
			//Create a fake radar event so the user can use the spacebar to quickly jump to this!
			TheRadar->tryEvent( RADAR_EVENT_FAKE, getPosition() );
		}
	}

	// This call won't do anything if we aren't actually in the list.
	//Kris: Added NULL check to prevent crash with combat bikes & their riders getting deleted on exit.
	if( getControllingPlayer() )
	{
		TheLogicToClient->uiRemoveIdleWorker( this, getControllingPlayer()->getPlayerIndex() );
	}

	//When a GLA hole is in the process of rebuilding, and that rebuild is lost, we need to
	//tell anyone attacking it to transfer the attack to the hole that still exists.
	if( testStatus( OBJECT_STATUS_RECONSTRUCTING ) )
	{
		Object *hole = TheGameLogic->findObjectByID( getProducerID() );
		if( hole )
		{
			// set the information in the hole about what to build
			RebuildHoleBehaviorInterface *rhbi = RebuildHoleBehavior::getRebuildHoleBehaviorInterfaceFromObject( hole );

			// sanity
			DEBUG_ASSERTCRASH( rhbi, ("Object::onDie() -  No Rebuild Hole Behavior interface on hole\n") );

			// start the rebuild process
			if( rhbi )
			{
				rhbi->startRebuildProcess( getTemplate(), getID() );
			}

			//Transfer any attackers from the destroyed building to the hole.
			for ( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
			{
				AIUpdateInterface* ai = obj->getAI();
				if (!ai)
					continue;

				ai->transferAttack( getID(), hole->getID() );
			}
		}
	}

}

//-------------------------------------------------------------------------------------------------
void Object::doStatusDamage( ObjectStatusTypes status, Real duration )
{
	if(m_statusDamageHelper)
		m_statusDamageHelper->doStatusDamage(status, duration);
}

//-------------------------------------------------------------------------------------------------
void Object::doTempWeaponBonus( WeaponBonusConditionType status, UnsignedInt duration )
{
	if(m_tempWeaponBonusHelper)
		m_tempWeaponBonusHelper->doTempWeaponBonus(status, duration);
}

//-------------------------------------------------------------------------------------------------
void Object::notifySubdualDamage( Real amount )
{
	if(m_subdualDamageHelper)
		m_subdualDamageHelper->notifySubdualDamage( amount );
	
	// If we are gaining subdual damage, we are slowly tinting
	if( getDrawable() )
	{
		if( amount > 0 )
			TheLogicToClient->drawable( getDrawable(), []( Drawable *d ) { d->setTintStatus(TINT_STATUS_GAINING_SUBDUAL_DAMAGE); } );
		else
			TheLogicToClient->drawable( getDrawable(), []( Drawable *d ) { d->clearTintStatus(TINT_STATUS_GAINING_SUBDUAL_DAMAGE); } );
	}
}

//=============================================================================
// Object::goInvulnerable
//=============================================================================
void Object::goInvulnerable( UnsignedInt time )
{
	const Bool WITHOUT_DEFECTOR_FX = FALSE;


	friend_setUndetectedDefector( time > 0 );

	if (m_defectionHelper)
		m_defectionHelper->startDefectionTimer(time, WITHOUT_DEFECTOR_FX);

}
