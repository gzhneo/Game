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

// FILE Object.cpp ////////////////////////////////////////////////////////////////////////////////
// Simple base object
// Author: Michael S. Booth, October 2000
///////////////////////////////////////////////////////////////////////////////////////////////////
 
// INCLUDES /////////////////////////////////////////////////////////////////////////////////////// 
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

#ifdef DEBUG_OBJECT_ID_EXISTS
ObjectID TheObjectIDToDebug = INVALID_ID;
#endif



//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
#ifdef DEBUG_LOGGING
AsciiString DescribeObject(const Object *obj)
{
	if (!obj)
		return "<No Object>";

	AsciiString ret;

	if (obj->getName().isNotEmpty())
	{
		ret.format("Object %d (%s) [%s, owned by player %d (%ls)]",
			obj->getID(), obj->getName().str(), obj->getTemplate()->getName().str(),
			obj->getControllingPlayer()->getPlayerIndex(),
			obj->getControllingPlayer()->getPlayerDisplayName().str());
	}
	else
	{
		ret.format("Object %d [%s, owned by player %d (%ls)]",
			obj->getID(), obj->getTemplate()->getName().str(),
			obj->getControllingPlayer()->getPlayerIndex(),
			obj->getControllingPlayer()->getPlayerDisplayName().str());
	}

	return ret;
}
#endif // DEBUG_LOGGING

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Object::Object( const ThingTemplate *tt, const ObjectStatusMaskType &objectStatusMask, Team *team ) : 
	Thing(tt),
	m_indicatorColor(0),
	m_ai(nullptr),
	m_physics(nullptr),
	m_geometryInfo(tt->getTemplateGeometryInfo()),
	m_containedBy(nullptr),
	m_xferContainedByID(INVALID_ID),
	m_containedByFrame(0),
	m_behaviors(nullptr),
	m_body(nullptr),
	m_contain(nullptr),
  m_stealth(nullptr),
	m_partitionData(nullptr),
	m_radarData(nullptr),
	m_drawable(nullptr),
	m_next(nullptr),
	m_prev(nullptr),
	m_team(nullptr),
	m_experienceTracker(nullptr),
	m_firingTracker(nullptr),
	m_repulsorHelper(nullptr),
	m_statusDamageHelper(nullptr),
	m_tempWeaponBonusHelper(nullptr),
	m_subdualDamageHelper(nullptr),
	m_smcHelper(nullptr),
	m_wsHelper(nullptr),
	m_defectionHelper(nullptr),
	m_partitionLastLook(nullptr),
	m_partitionRevealAllLastLook(nullptr),
	m_partitionLastShroud(nullptr),
	m_partitionLastThreat(nullptr),
	m_partitionLastValue(nullptr),
	m_smcUntil(NEVER),
	m_privateStatus(0),
	m_formationID(NO_FORMATION_ID),
	m_isReceivingDifficultyBonus(FALSE),
	m_singleUseCommandUsed(FALSE),
	m_scriptStatus(0),
	m_enteredOrExitedFrame(0),
	m_visionSpiedMask (PLAYERMASK_NONE),
	m_numTriggerAreasActive(0)
{
#if defined(_DEBUG)
	m_hasDiedAlready = false;
#endif
	//Modules have not been created yet!
	m_modulesReady = false;

	// Force the thing template to use the most overridden version of itself - jkmcd
	// Note that after this, the object will be using m_template, which forces the usage of the 
	// most overridden version of tt, so this is okay.
	tt = static_cast<const ThingTemplate *>(tt->getFinalOverride());
		
	Int i, modIdx;
	AsciiString modName;
	
	//Added By Sadullah Nader
	//Initializations inserted
	m_formationOffset.x = m_formationOffset.y = 0.0f;
	m_iPos.zero();
	//
	for (i = 0; i < MAX_PLAYER_COUNT; ++i) 
	{
		m_visionSpiedBy[i] = 0;
	}

	for( i = 0; i < DISABLED_COUNT; i++ )
	{
		m_disabledTillFrame[ i ] = NEVER;
	}

	m_weaponBonusCondition = 0;
	m_curWeaponSetFlags.clear();

	// sanity
	if( TheGameLogic == nullptr || tt == nullptr )
	{

		assert( 0 );
		return;

	}  // end if

	// Object's set of these persist for the life of the object.
	m_partitionLastLook = newInstance(SightingInfo);
	m_partitionLastLook->reset();
	m_partitionRevealAllLastLook = newInstance(SightingInfo);
	m_partitionRevealAllLastLook->reset();
	m_partitionLastShroud = newInstance(SightingInfo);
	m_partitionLastShroud->reset();
	m_partitionLastThreat = newInstance(SightingInfo);
	m_partitionLastThreat->reset();
	m_partitionLastValue = newInstance(SightingInfo);
	m_partitionLastValue->reset();

	// must set ID to zero, since some of these set methods
	// will cause network messages to be sent
	// which use this ID.
	m_id = INVALID_ID;
	m_producerID = INVALID_ID;
	m_builderID = INVALID_ID;

	m_status = objectStatusMask;
	m_layer = LAYER_GROUND;

	m_group = nullptr;

	m_constructionPercent = CONSTRUCTION_COMPLETE;  // complete by default

	m_visionRange = tt->friend_calcVisionRange();
	m_shroudClearingRange = tt->friend_calcShroudClearingRange();
	if( m_shroudClearingRange == -1.0f )
		m_shroudClearingRange = m_visionRange;// Backwards compatible, and perfectly logical default to assign
	m_shroudRange = 0.0f;
	
	m_singleUseCommandUsed = false;

	// assign unique object id
	setID( TheGameLogic->allocateObjectID() );

	//
	// allocate any modules we need to, we should keep
	// this at or near the end of the drawable construction so that we have
	// all the valid data about the thing when we create the module
	//
	Int totalModules = tt->getBehaviorModuleInfo().getCount() + NUM_SLEEP_HELPERS;  // need to take into account all the helper modules

	// allocate the publicModule arrays
// pool[]ify
	m_behaviors = MSGNEW("ModulePtrs") BehaviorModule*[totalModules + 1];
	BehaviorModule** curB = m_behaviors;
	const ModuleInfo& mi = tt->getBehaviorModuleInfo();

	// set m_team to null before the first call, to avoid naughtiness...
	// If no team is specified in the constructor, then assign the object
	// to the neutral team.
	setTeam(team ? team : ThePlayerList->getNeutralPlayer()->getDefaultTeam());

	// the helpers are done first -- even before Behaviors! -- in case a module needs
	// to call something that uses them.
	static const NameKeyType smcHelperModuleDataTagNameKey = NAMEKEY( "ModuleTag_SMCHelper" );
	static ObjectSMCHelperModuleData smcModuleData;
	smcModuleData.setModuleTagNameKey( smcHelperModuleDataTagNameKey );
	m_smcHelper = newInstance(ObjectSMCHelper)(this, &smcModuleData);		
	*curB++ = m_smcHelper;

	//Inactive bodies can't take special damage since they can't take damage
	Bool isInactiveBody = FALSE;
	for( Int infoIndex = 0; infoIndex < mi.getCount(); ++infoIndex )
	{
		modName = mi.getNthName(infoIndex);
		if (modName.isEmpty())
			continue;

		if( modName.compare("InactiveBody") == 0 )
		{
			isInactiveBody = TRUE;
			break;
		}
	}

	if( !isInactiveBody )
	{
		static const NameKeyType statusHelperModuleDataTagNameKey = NAMEKEY( "ModuleTag_StatusDamageHelper" );
		static StatusDamageHelperModuleData statusModuleData;
		statusModuleData.setModuleTagNameKey( statusHelperModuleDataTagNameKey );
		m_statusDamageHelper = newInstance(StatusDamageHelper)(this, &statusModuleData);		
		*curB++ = m_statusDamageHelper;

		static const NameKeyType subdualHelperModuleDataTagNameKey = NAMEKEY( "ModuleTag_SubdualDamageHelper" );
		static SubdualDamageHelperModuleData subdualModuleData;
		subdualModuleData.setModuleTagNameKey( subdualHelperModuleDataTagNameKey );
		m_subdualDamageHelper = newInstance(SubdualDamageHelper)(this, &subdualModuleData);		
		*curB++ = m_subdualDamageHelper;
	}

	if (TheAI != nullptr
			&& TheAI->getAiData()->m_enableRepulsors
			&& isKindOf(KINDOF_CAN_BE_REPULSED))
	{
		// if we can ever be a temporary-repulsor, make a repulsor helper. (srj)
		static const NameKeyType repulsorHelperModuleDataTagNameKey = NAMEKEY( "ModuleTag_RepulsorHelper" );
		static ObjectRepulsorHelperModuleData repulsorModuleData;
		repulsorModuleData.setModuleTagNameKey( repulsorHelperModuleDataTagNameKey );
		m_repulsorHelper = newInstance(ObjectRepulsorHelper)(this, &repulsorModuleData);		
		*curB++ = m_repulsorHelper;
	}

	/** @todo srj -- figure out how to create this only on demand.
		currently we don't have a good way to add/remove update modules from
		an object on-the-fly, so we fake it here, and just skip the creation
		if it is impossible for this object to ever defect... */

	// shrubbery cannot defect. no, really.
	if (!tt->isKindOf(KINDOF_SHRUBBERY))
	{
		static const NameKeyType defectionModuleDataTagNameKey = NAMEKEY( "ModuleTag_DefectionHelper" );
		static ObjectDefectionHelperModuleData defectionModuleData;
		defectionModuleData.setModuleTagNameKey( defectionModuleDataTagNameKey );
		m_defectionHelper = newInstance(ObjectDefectionHelper)(this, &defectionModuleData);		
		*curB++ = m_defectionHelper;
	}

	if (tt->canPossiblyHaveAnyWeapon())
	{
		// we only need a firingtracker and wshelper if we can possibly have a weapon.
		static const NameKeyType weaponStatusModuleDataTagNameKey = NAMEKEY( "ModuleTag_WeaponStatusHelper" );
		static ObjectWeaponStatusHelperModuleData weaponStatusModuleData;
		weaponStatusModuleData.setModuleTagNameKey( weaponStatusModuleDataTagNameKey );
		m_wsHelper = newInstance(ObjectWeaponStatusHelper)(this, &weaponStatusModuleData);		
		*curB++ = m_wsHelper;

		static const NameKeyType firingTrackerModuleDataTagNameKey = NAMEKEY( "ModuleTag_FiringTrackerHelper" );
		static FiringTrackerModuleData firingTrackerModuleData;
		firingTrackerModuleData.setModuleTagNameKey( firingTrackerModuleDataTagNameKey );
		m_firingTracker = newInstance(FiringTracker)(this, &firingTrackerModuleData);
		*curB++ = m_firingTracker;

		static const NameKeyType tempWeaponBonusHelperModuleDataTagNameKey = NAMEKEY( "ModuleTag_TempWeaponBonusHelper" );
		static TempWeaponBonusHelperModuleData tempWeaponBonusModuleData;
		tempWeaponBonusModuleData.setModuleTagNameKey( tempWeaponBonusHelperModuleDataTagNameKey );
		m_tempWeaponBonusHelper = newInstance(TempWeaponBonusHelper)(this, &tempWeaponBonusModuleData);		
		*curB++ = m_tempWeaponBonusHelper;
	}

	// behaviors are always done first, so they get into the publicModule arrays
	// before anything else.
	for (modIdx = 0; modIdx < mi.getCount(); ++modIdx)
	{
		modName = mi.getNthName(modIdx);
		if (modName.isEmpty())
			continue;

		BehaviorModule* newMod = static_cast<BehaviorModule*>(TheModuleFactory->newModule(this, modName, mi.getNthData(modIdx), MODULETYPE_BEHAVIOR));
		*curB++ = newMod;

		BodyModuleInterface* body = newMod->getBody();
		if (body) 
		{
			DEBUG_ASSERTCRASH(m_body == nullptr, ("Duplicate bodies"));
			m_body = body;
		}

		ContainModuleInterface* contain = newMod->getContain();
		if (contain) 
		{
			DEBUG_ASSERTCRASH(m_contain == nullptr, ("Duplicate containers"));
			m_contain = contain;
		}

    StealthUpdate* stealth = static_cast<StealthUpdate*>(newMod->getStealth());
    if ( stealth )
    {
      DEBUG_ASSERTCRASH( m_stealth == nullptr, ("DuplicateStealthUpdates!") );
      m_stealth = stealth;
    }


		AIUpdateInterface* ai = newMod->getAIUpdateInterface();
		if (ai)
		{
			if( m_ai ) 
			{
				DEBUG_ASSERTCRASH( m_ai == nullptr, ("%s has more than one AI module. This is illegal!\n", getTemplate()->getName().str()) );
			}
			m_ai = ai;
		}

		static NameKeyType key_PhysicsUpdate = NAMEKEY("PhysicsBehavior");
		if (newMod->getModuleNameKey() == key_PhysicsUpdate)
		{
			DEBUG_ASSERTCRASH(m_physics == nullptr, ("You should never have more than one Physics module (%s)\n",getTemplate()->getName().str()));
			m_physics = static_cast<PhysicsBehavior*>(newMod);
		}
	}

	*curB = nullptr;

	AIUpdateInterface *ai = getAIUpdateInterface();
	if (ai) {
		ai->setAttitude(getTeam()->getPrototype()->getTemplateInfo()->m_initialTeamAttitude);
		if (m_team && m_team->getPrototype() && m_team->getPrototype()->getAttackPriorityName().isNotEmpty()) {
			AsciiString name = m_team->getPrototype()->getAttackPriorityName();
			const AttackPriorityInfo *info = TheScriptEngine->getAttackInfo(name);
			if (info && info->getName().isNotEmpty()) {
				ai->setAttackInfo(info);
			}
		}	
	}

	// allocate experience tracker
	m_experienceTracker = newInstance(ExperienceTracker)(this);

	// If a valid team has been assigned me, then I have a Player I can ask about my starting level
	const Player* controller = getControllingPlayer();
	m_experienceTracker->setVeterancyLevel( controller->getProductionVeterancyLevel( getTemplate()->getName() ) );

	/// allow for inter-Module resolution
	for (BehaviorModule** b = m_behaviors; *b; ++b)
	{
		(*b)->onObjectCreated();
	}

	m_numTriggerAreasActive = 0;
	m_enteredOrExitedFrame = 0;
	m_isSelectable = tt->isKindOf(KINDOF_SELECTABLE);

	m_healthBoxOffset.zero();// this is used for units that are amorphous, like angry mob

	//Modules have now been completely created!
	m_modulesReady = true;

	TheRadar->addObject( this );

	// register the object with the GameLogic
	TheGameLogic->registerObject( this );

	//disable occlusion for some time after object is created to allow them to exit the factory/building.
	m_safeOcclusionFrame = TheGameLogic->getFrame()+tt->getOcclusionDelay();


	m_soleHealingBenefactorID = INVALID_ID; ///< who is the only other object that can give me this non-stacking heal benefit?
	m_soleHealingBenefactorExpirationFrame = 0; ///< on what frame can I accept healing (thus to switch) from a new benefactor



}  // end Object

//-------------------------------------------------------------------------------------------------
/** Emit message announcing object's creation
 * Note: Have to do this in virtual init() method because virtual methods 
 * don't become virtual until AFTER the constructor has completed, and we 
 * need to send our type in this message via virtual getType(). */
//-------------------------------------------------------------------------------------------------
void Object::initObject()
{
	// Weapons & Damage -------------------------------------------------------------------------------------------------
	// Force the initial weapon set to be instantiated & reloaded.

	//GS No Bad Wrong
	// The flags are constructed to empty, and between then and now they may be set in valid ways by onCreate modules.
	// We don't want to blow that away.  updateWeaponSet is safe to call on its own, so I will move that to the end.
//	m_curWeaponSetFlags.clear();
//	m_weaponSet.updateWeaponSet(this);
//	m_weaponBonusCondition = 0;

	for (int i = 0; i < WEAPONSLOT_COUNT; ++i)
		m_lastWeaponCondition[i] = WSF_INVALID;

	// emit message announcing object's creation
	TheGameLogic->sendObjectCreated( this );

	// If I have a valid team assigned, I can run through my Upgrade modules with his flags
	updateUpgradeModules();

	//If the player has battle plans (America Strategy Center), then apply those bonuses
	//to this object if applicable. Internally it validates certain kinds of objects.
	const Player* controller = getControllingPlayer();
	if (controller)
	{
		if (!getReceivingDifficultyBonus() && TheScriptEngine->getObjectsShouldReceiveDifficultyBonus())
		{
			setReceivingDifficultyBonus(TRUE);
		}
			
		if (controller->getNumBattlePlansActive() > 0)
		{
			controller->applyBattlePlanBonusesForObject( this );
		}
	}


	//For each special power module that we have, add it's type to the specialpower bits. This is
	//for optimal access later.
	for (BehaviorModule** m = m_behaviors; *m; ++m)
	{
		SpecialPowerModuleInterface* sp = (*m)->getSpecialPower();
		if (!sp)
			continue;

		const SpecialPowerTemplate *spTemplate = sp->getSpecialPowerTemplate();
		if( spTemplate )
		{
			SET_SPECIALPOWERMASK( m_specialPowerBits, spTemplate->getSpecialPowerType() );
		}
	}

	// Kris -- All missiles must be projectiles! This is the perfect place to assert them!
	// srj: yes, but only in debug...
#if defined(_DEBUG)
	if( !isKindOf( KINDOF_PROJECTILE ) )
	{
		if( isKindOf( KINDOF_SMALL_MISSILE ) || isKindOf( KINDOF_BALLISTIC_MISSILE ) )
		{
			//Warning only...
			DEBUG_CRASH( ("Missile %s must also be a KindOf = PROJECTILE in addition to being either a SMALL_MISSILE or PROJECTILE_MISSILE -- call Kris (36844) for questions!", getTemplate()->getName().str() ) );
		}
	}
#endif
	if (!isKindOf(KINDOF_PROJECTILE) && !isKindOf(KINDOF_INERT)) {
		// Notify script conditions to update conditions that consider unit counts.
		// We ignore projectiles cause they are frequently created & destroyed, and are not
		// of general interest.  Normal unit count tests consider tanks or infantry or planes, etc. jba.
		TheScriptEngine->notifyOfObjectCreationOrDestruction();
		TheGameLogic->updateObjectsChangedTriggerAreas();
	}

	// Everything (like weaponSet flags) is inited, so check if the WeaponSet needs to change.
	m_weaponSet.updateWeaponSet(this);
	
	if( isKindOf( KINDOF_MINE ) || isKindOf( KINDOF_BOOBY_TRAP ) || isKindOf( KINDOF_DEMOTRAP ) )
	{
		ThePlayerList->getNeutralPlayer()->getAcademyStats()->recordMine();	
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Object::~Object()
{

	// tell the AI the building is gone
	/// @todo Generalize the notion of objects entering and leaving the world, so we don't have to special case this
	TheAI->pathfinder()->removeObjectFromPathfindMap( this );

	if (!isKindOf(KINDOF_PROJECTILE) && !isKindOf(KINDOF_INERT)) {
		// Notify script conditions to update conditions that consider unit counts.
		// We ignore projectiles cause they are frequently created & destroyed, and are not
		// of general interest.  Normal unit count tests consider tanks or infantry or planes, etc. jba.
		TheGameLogic->updateObjectsChangedTriggerAreas();
		TheScriptEngine->notifyOfObjectCreationOrDestruction();
	}

	//
	// remove from radar before we NULL out the team ... the order of ops are critical here
	// because the radar code will sometimes look at the team info and it is assumed through
	// the team and player code that the team is valid
	//
	if( m_radarData )
		TheRadar->removeObject( this );

	// emit message announcing object's destruction. Again, order is important; we must do this
	// before wiping out the team.
	TheGameLogic->sendObjectDestroyed( this );

	// empty the team
	setTeam( nullptr );

	// Object's set of these persist for the life of the object.
	deleteInstanceOf(m_partitionLastLook);
	m_partitionLastLook = nullptr;
	deleteInstanceOf(m_partitionRevealAllLastLook);
	m_partitionRevealAllLastLook = nullptr;
	deleteInstanceOf(m_partitionLastShroud);
	m_partitionLastShroud = nullptr;
	deleteInstanceOf(m_partitionLastThreat);
	m_partitionLastThreat = nullptr;
	deleteInstanceOf(m_partitionLastValue);
	m_partitionLastValue = nullptr;

	// remove the object from the partition system if present
	if( m_partitionData )
		ThePartitionManager->unRegisterObject( this );

	// if we are in a group, remove us
	if (m_group)
		m_group->remove( this );
	
	// note, do NOT free these, there are just a shadow copy!
	m_ai = nullptr;
	m_physics = nullptr;

	// delete any modules present
	for (BehaviorModule** b = m_behaviors; *b; ++b)
	{
		deleteInstanceOf((*b));
		*b = nullptr;	// in case other modules call findModule from their dtor!
	}

	delete [] m_behaviors;		
	m_behaviors = nullptr;

	if( m_experienceTracker )
		deleteInstanceOf(m_experienceTracker);

	m_experienceTracker = nullptr;

	// we don't need to delete these, there were deleted on the m_behaviors list
	m_firingTracker = nullptr;
	m_repulsorHelper = nullptr;

	m_statusDamageHelper = nullptr;
	m_tempWeaponBonusHelper = nullptr;
	m_subdualDamageHelper = nullptr;
	m_smcHelper = nullptr;
	m_wsHelper = nullptr;
	m_defectionHelper = nullptr;

	// reset id to zero so we never mistaken grab "dead" objects
	m_id = INVALID_ID;

	// Instead of removing it from the named cache, notify the script engine that it has died.
	// The script engine will remove it from the cache if necessary. The script engine needs to take
	// a crack at this in case it is the current "This Object" pointer.
	TheScriptEngine->notifyOfObjectDestruction(this);
}

//-------------------------------------------------------------------------------------------------
/// this object now contained in "containedBy"
//-------------------------------------------------------------------------------------------------
void Object::onContainedBy( Object *containedBy )
{
	setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_UNSELECTABLE ) );
	if (containedBy && containedBy->getContain()->isEnclosingContainerFor(this))
		setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_MASKED ) );
	else
		clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_MASKED ) );
	m_containedBy = containedBy;
	m_containedByFrame = TheGameLogic->getFrame();

  handlePartitionCellMaintenance(); // which should unlook me now that I am contained
  
}

//-------------------------------------------------------------------------------------------------
/// this object no longer contained in "containedBy"
//-------------------------------------------------------------------------------------------------
void Object::onRemovedFrom( Object *removedFrom )
{
	clearStatus( MAKE_OBJECT_STATUS_MASK2( OBJECT_STATUS_MASKED, OBJECT_STATUS_UNSELECTABLE ) );
	m_containedBy = nullptr;
	m_containedByFrame = 0;

  handlePartitionCellMaintenance(); // get a clean look, now that I am outdoors, again

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Int Object::getTransportSlotCount() const
{
	Int count = getTemplate()->getRawTransportSlotCount();
	ContainModuleInterface* contain = getContain();
	if ( contain && contain->isSpecialZeroSlotContainer() )
	{
		count = 0;
		const ContainedItemsList* items = contain->getContainedItemsList();
		if (items)
		{
			for (ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it)
			{
				count += (*it)->getTransportSlotCount();
			}
		}
	}
	return count;
}

//-------------------------------------------------------------------------------------------------
/** Run from GameLogic::destroyObject */
//-------------------------------------------------------------------------------------------------
void Object::onDestroy()
{

	// This is the old cleanUpContain safeguard.  Say goodbye so they don't try to look us up.
	if( m_containedBy && m_containedBy->getContain() )
	{
		m_containedBy->getContain()->removeFromContain( this );
	}

	//
	// run the onDelete on all modules present so they each have an opportunity to cleanup
	// anything they need to ... including talking to any other modules
	//
	for (BehaviorModule** b = m_behaviors; *b; ++b)
	{
		(*b)->onDelete();
	}

	//Have to remove ourself from looking as well.  RebuildHoleWorkers definately hit here.
	handlePartitionCellMaintenance();
}  // end onDestroy

//=============================================================================
//=============================================================================
void Object::setGeometryInfo(const GeometryInfo& geom) 
{ 
	m_geometryInfo = geom; 
	if( m_partitionData )
	{
		// if our geometry changes, we unregister and re-register with the partitionmgr
		// so that our size gets updated appropriately. this shouldn't be a problem 
		// unless setGeometryInfo gets called frequently. (srj)
		ThePartitionManager->unRegisterObject( this );
		ThePartitionManager->registerObject( this );
	}

	if (m_drawable)
		m_drawable->reactToGeometryChange();
}

//=============================================================================
//=============================================================================
void Object::setGeometryInfoZ( Real newZ ) 
{ 
	// A Z change only does not need to un/register with the PartitionManager
	m_geometryInfo.setMaxHeightAbovePosition( newZ );

	if (m_drawable)
		m_drawable->reactToGeometryChange();
}

//=============================================================================
void Object::friend_setUndetectedDefector( Bool status )
{
	if (status)
		m_privateStatus |= UNDETECTED_DEFECTOR;
	else
		m_privateStatus &= ~UNDETECTED_DEFECTOR;
}

//=============================================================================
void Object::restoreOriginalTeam()
{
	if( m_team == nullptr || m_originalTeamName.isEmpty() )
		return;
	
	Team* origTeam = TheTeamFactory->findTeam(m_originalTeamName);
	if (origTeam == nullptr)
	{
		DEBUG_CRASH(("Object original team (%s) could not be found or created! (srj)\n",m_originalTeamName.str()));
		return;
	}

	if (m_team == origTeam)
	{
		DEBUG_CRASH(("Object appears to still be on its original team, so why are we attempting to restore it? (srj)\n"));
		return;
	}

	setTeam(origTeam);
}

//=============================================================================
//=============================================================================
void Object::setTeam( Team *team )
{
	// In order to prevent spawning useful units for a player after he dies, we
	// just assign objects to the neutral player if we try to misbehave.
	if (team && !team->getControllingPlayer()->isPlayerActive())
		team = ThePlayerList->getNeutralPlayer()->getDefaultTeam();

	setTemporaryTeam(team);
	m_originalTeamName = m_team ? m_team->getName() : AsciiString::TheEmptyString;
}

//=============================================================================
//=============================================================================
void Object::setTemporaryTeam( Team *team )
{
	const Bool restoring = false;
	setOrRestoreTeam(team, restoring);
}

//=============================================================================
//=============================================================================
void Object::setOrRestoreTeam( Team* team, Bool restoring )
{
	// don't do anything if the team hasn't changed
	if( m_team == team )
		return;

	Team* oldTeam = m_team;

	// Before Switch //////////////////////////
	if (m_team)
	{
		if (m_team->isInList_TeamMemberList(this))
		{
			m_team->removeFrom_TeamMemberList(this);
			m_team->getControllingPlayer()->becomingTeamMember(this, false);
		}
	}
		
	// Switch //////////////////////////
	m_team = team;

	// After Switch //////////////////////////
	if (m_team)
	{
		if (!m_team->isInList_TeamMemberList(this))
		{
			m_team->prependTo_TeamMemberList(this);
			m_team->getControllingPlayer()->becomingTeamMember(this, true);
		}
		
		// now, adjust the attitude of the unit to its new team.
		const TeamPrototype* proto = m_team->getPrototype();
		if (proto && proto->getTemplateInfo()) 
		{
			AIUpdateInterface *ai = getAIUpdateInterface();
			if (ai) 
			{
				ai->setAttitude(proto->getTemplateInfo()->m_initialTeamAttitude);
				if (proto->getAttackPriorityName().isNotEmpty()) {
					AsciiString name = proto->getAttackPriorityName();
					const AttackPriorityInfo *info = TheScriptEngine->getAttackInfo(name);
					if (info && info->getName().isNotEmpty()) {
						ai->setAttackInfo(info);
					}
				}	
			}
		}
		// emit message announcing object's new alliance
		Drawable *draw = getDrawable();
		if (draw)
			draw->changedTeam();
	}

	// This can't just go in ::defect, because some things just do setTeam.  The act of
	// setting a new team needs to tell the modules and do other important stuff.
	// And it needs to happen after the switch.
	if( oldTeam && team && !restoring )
		onCapture( oldTeam->getControllingPlayer(), team->getControllingPlayer() );

	//
	// the team changed we have a change in priorities on the radar if we are
	// a candidate for the radar as it is
	//
	if( m_radarData )
	{

		// removing it and adding it will cause a resort to happen
		TheRadar->removeObject( this );
		TheRadar->addObject( this );
	}

	// Tell TheInGameUI that the object has changed hands
	Int oldPlayerIndex = (oldTeam)?(oldTeam->getControllingPlayer()->getPlayerIndex()):-1;
	Int newPlayerIndex = (m_team)?(m_team->getControllingPlayer()->getPlayerIndex()):-1;
	if (oldPlayerIndex != newPlayerIndex)
		TheInGameUI->objectChangedTeam(this, oldPlayerIndex, newPlayerIndex);
}

//=============================================================================
enum 
{
	BOOBY_TRAP_SCAN_RANGE = 25
};
Bool Object::checkAndDetonateBoobyTrap(const Object *victim)
{
	if( !testStatus(OBJECT_STATUS_BOOBY_TRAPPED) )
		return FALSE;

	PartitionFilterAcceptByKindOf kindFilter(MAKE_KINDOF_MASK(KINDOF_BOOBY_TRAP), KINDOFMASK_NONE);
	PartitionFilterSameMapStatus filterMapStatus(this);
	PartitionFilter *filters[3];
	filters[0] = &kindFilter;
	filters[1] = &filterMapStatus;
	filters[2] = nullptr;

	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( getPosition(), static_cast<Real>(BOOBY_TRAP_SCAN_RANGE) + getGeometryInfo().getBoundingCircleRadius(), 
		DistanceCalculationType::FROM_CENTER_2D, filters, IterOrderType::ITER_SORTED_NEAR_TO_FAR );
	MemoryPoolObjectHolder hold(iter);// This is the magic thing that frees the dynamically made iter in its destructor

	Object *ourBoobyTrap = nullptr;
	for( Object *other = iter->first(); other; other = iter->next() )
	{
		if( other->getProducerID() == getID() )// Sticky bombs call the thing they are on their producer for just such an occasion
		{
			ourBoobyTrap = other;
			break;
		}
	}

	if( ourBoobyTrap )
	{
		static NameKeyType key_StickyBombUpdate = NAMEKEY( "StickyBombUpdate" );
		StickyBombUpdate *update = static_cast<StickyBombUpdate*>(ourBoobyTrap->findUpdateModule( key_StickyBombUpdate ));
		if( update )
		{
			if( victim && ourBoobyTrap->getControllingPlayer()->getRelationship(victim->getTeam()) == ALLIES )
				return FALSE;// Friends don't touch friends boobies.

			update->detonate();
			return TRUE;// Booby Trapped status will be cleared by stickybomb, as they set it
		}
	}

	return FALSE;
}

//=============================================================================
void Object::setStatus( ObjectStatusMaskType objectStatus, Bool set )
{
	ObjectStatusMaskType oldStatus = m_status;

	if (set)
		m_status.set( objectStatus );
	else
		m_status.clear( objectStatus );

	if (m_status != oldStatus)
	{
		if( set && objectStatus.test( OBJECT_STATUS_REPULSOR ) && m_repulsorHelper != nullptr )
		{
			// Damaged repulsable civilians scare (repulse) other civs, but only
			// for a short amount of time... use the repulsor helper to turn off repulsion shortly.
			m_repulsorHelper->sleepUntil(TheGameLogic->getFrame() + 2*LOGICFRAMES_PER_SECOND);
		}

		if( objectStatus.test( OBJECT_STATUS_STEALTHED ) || objectStatus.test( OBJECT_STATUS_DETECTED ) || objectStatus.test( OBJECT_STATUS_DISGUISED ) )
		{
			//Kris: Aug 20, 2003
			//When any of the three key status bits for stealth go on or off, then handle partition updates for vision.
			if( getTemplate()->getShroudRevealToAllRange() > 0.0f )
			{
				handlePartitionCellMaintenance();
			}
		}


		// when an object's construction status changes, it needs to have its partition data updated,
		// in order to maintain the shroud correctly.
		if( m_status.test( OBJECT_STATUS_UNDER_CONSTRUCTION ) != oldStatus.test( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		{

			// CHECK FOR MINES, AND DETONATE THEM NOW 
			ObjectIterator *iter = 
					ThePartitionManager->iteratePotentialCollisions( getPosition(), getGeometryInfo(), getOrientation() );
			MemoryPoolObjectHolder hold( iter );
			Object *them;
			for( them = iter->first(); them; them = iter->next() )
			{
				if (them->isKindOf( KINDOF_MINE ))
				{
					//DETONATE ANY ENEMY MINES, OR DELETE FRIENDLY ONES
					Relationship r = getRelationship(them);
					if (r == ENEMIES)
					{
						them->kill(); // detonate mine 
					}
					else
					{
						TheGameLogic->destroyObject(them); 
					}
				}
			}// next object

			if (m_partitionData)
				m_partitionData->makeDirty(true);
		}

	}

}

//=============================================================================
void Object::setScriptStatus( ObjectScriptStatusBit bit, Bool set )
{
	UnsignedInt oldScriptStatus = m_scriptStatus;

	if( set )
	{
		m_scriptStatus |= bit;
	}
	else
	{
		m_scriptStatus &= ~bit;
	}

	if( m_scriptStatus != oldScriptStatus )
	{
		if( static_cast<UnsignedInt>(m_scriptStatus & OBJECT_STATUS_SCRIPT_DISABLED) != static_cast<UnsignedInt>(oldScriptStatus & OBJECT_STATUS_SCRIPT_DISABLED) )
		{
			if( m_partitionData )
			{
				// if an object becomes disabled or unpowered, then you have to update its partition data because it will
				// change how far it can see. 
				m_partitionData->makeDirty(true);
			}
			if( m_scriptStatus & OBJECT_STATUS_SCRIPT_DISABLED )
			{
				//I am now disabled, so tell the main game engine!
				setDisabled( DISABLED_SCRIPT_DISABLED );
			}
			else
			{
				//I am no longer disabled, so tell the main game engine!
				clearDisabled( DISABLED_SCRIPT_DISABLED );
			}
		}
		if( static_cast<UnsignedInt>(m_scriptStatus & OBJECT_STATUS_SCRIPT_UNPOWERED) != static_cast<UnsignedInt>(oldScriptStatus & OBJECT_STATUS_SCRIPT_UNPOWERED) )
		{
			if( m_partitionData )
			{
				// if an object becomes disabled or unpowered, then you have to update its partition data because it will
				// change how far it can see. 
				m_partitionData->makeDirty(true);
			}
			if( m_scriptStatus & OBJECT_STATUS_SCRIPT_UNPOWERED )
			{
				//I am now underpowered, so tell the main game engine!
				setDisabled( DISABLED_SCRIPT_UNDERPOWERED );
			}
			else
			{
				//I am no longer undperpowered, so tell the main game engine!
				clearDisabled( DISABLED_SCRIPT_UNDERPOWERED );
			}
		}
	}
}

//=============================================================================
Bool Object::canCrushOrSquish(Object *otherObj, CrushSquishTestType testType ) const
{
	DEBUG_ASSERTCRASH(this, ("null this in canCrushOrSquish"));

	if( !otherObj ) 
	{
		//Can't crush anything.
		return false;  
	}

	if( isDisabledByType( DISABLED_UNMANNED ) )
	{
		//Unmanned vehicles cannot crush troops. This was happening when Jarmen Kell sniped
		//the vehicle and booted the guys out while still moving, as the vehicle is now
		//on a different team.
		return false;
	}

	UnsignedByte crusherLevel = getCrusherLevel();
	
	// order matters: we want to know if I consider it to be an ally, not vice versa
	if( getRelationship( otherObj ) == ALLIES ) 
	{
		//Friends don't let friends crush friends.
		return false; 
	}

	if( !crusherLevel )
	{
		//Can't crush anything!
		return false;
	}

	//Test this case for generic infantry getting squished by vehicles!
	if( testType == CrushSquishTestType::TEST_SQUISH_ONLY || testType == CrushSquishTestType::TEST_CRUSH_OR_SQUISH )
	{

		//****************************************************************************************
		//NOTE: This section of code is used by the pathfinder to determine if the object should
		//      move to the target. I don't think it's the right place to check for this because
		//      the semantics check to see if we can squish something -- not approach it. However
		//      I'm not moving it for fear of some major breakage! -- KM
		//Bool squisher = crusherLevel > 0;
		//if( !squisher )
		//{
		//	Weapon *weapon = getCurrentWeapon();
		//	if( weapon && weapon->isContactWeapon() )
		//	{
		//		squisher = true;
		//	}
		//}
		//if( squisher )
		//NOTE2: *** IF YOU REENABLE THIS CODE -- Move the "if( !crusherLevel ) return false" below
		//                                        this squish section.
		//****************************************************************************************
		{
			// See if other is squishable
			static NameKeyType key_squish = NAMEKEY( "SquishCollide" );
			if( otherObj->findModule( key_squish ) ) 
			{
				return true; // squishable.
			}
		}
	}


	UnsignedByte crushableLevel = otherObj->getCrushableLevel();

	if( testType == CrushSquishTestType::TEST_CRUSH_ONLY || testType == CrushSquishTestType::TEST_CRUSH_OR_SQUISH )
	{
		if( crusherLevel > crushableLevel )
		{
			return true;
		}
	}
		
	return false;
}

//-------------------------------------------------------------------------------------------------
UnsignedByte Object::getCrusherLevel() const
{
	return getTemplate()->getCrusherLevel();
}

//-------------------------------------------------------------------------------------------------
UnsignedByte Object::getCrushableLevel() const
{
	return getTemplate()->getCrushableLevel();
}


// ------------------------------------------------------------------------------------------------
/** Topple an object, if possible */
// ------------------------------------------------------------------------------------------------
void Object::topple( const Coord3D *toppleDirection, Real toppleSpeed, UnsignedInt options )
{
	static NameKeyType key_ToppleUpdate = NAMEKEY("ToppleUpdate");

	ToppleUpdate* toppleUpdate = static_cast<ToppleUpdate*>(findModule(key_ToppleUpdate));
	if( toppleUpdate && toppleUpdate->isAbleToBeToppled() )
	{

		// apply the topple force
		toppleUpdate->applyTopplingForce( toppleDirection, toppleSpeed, options );

	}  // end if

}  // end topple

//=============================================================================
void Object::setModelConditionState( ModelConditionFlagType a )
{
	if (m_drawable)
	{
		m_drawable->setModelConditionState(a);
	}
}

//=============================================================================
void Object::clearModelConditionState( ModelConditionFlagType a )
{
	if (m_drawable)
	{
		m_drawable->clearModelConditionState(a);
	}
}

//=============================================================================
void Object::clearAndSetModelConditionState( ModelConditionFlagType clr, ModelConditionFlagType set )
{
	if (m_drawable)
	{
		m_drawable->clearAndSetModelConditionState(clr, set);
	}
}

//=============================================================================
void Object::clearModelConditionFlags( const ModelConditionFlags& clr )
{
	if (m_drawable)
	{
		m_drawable->clearModelConditionFlags(clr);
	}
}

//=============================================================================
void Object::setModelConditionFlags( const ModelConditionFlags& set )
{
	if (m_drawable)
	{
		m_drawable->setModelConditionFlags(set);
	}
}

//=============================================================================
void Object::clearAndSetModelConditionFlags( const ModelConditionFlags& clr, const ModelConditionFlags& set )
{
	if (m_drawable)
	{
		m_drawable->clearAndSetModelConditionFlags(clr, set);
	}
}

//=============================================================================
// Special model states are states that are turned on for a period of time, and 
// turned off automatically -- used for cheer, and scripted special moment 
// animations. Setting a special state will automatically clear any other 
// special states that may be turned on so you can only have one at a time.
//=============================================================================
void Object::setSpecialModelConditionState( ModelConditionFlagType set, UnsignedInt frames )
{
	clearSpecialModelConditionStates();

	setModelConditionState( set );

	if( frames == 0 )
	{
		frames = 1;
	}

	m_smcUntil = TheGameLogic->getFrame() + frames;
	m_smcHelper->sleepUntil(m_smcUntil);
}

//=============================================================================
void Object::clearSpecialModelConditionStates()
{
	clearModelConditionFlags( MAKE_MODELCONDITION_MASK( MODELCONDITION_SPECIAL_CHEERING ) );
	m_smcUntil = NEVER;
}

// Lorenzen has some interest in this, ask before deleting
//=============================================================================
//const ModelConditionFlags& Object::getModelConditionFlags() const
//{ 
//	if (m_drawable)
//	{
//		return m_drawable->getModelConditionFlags(); 
//	}
//	else
//	{
//		DEBUG_CRASH(("NULL Drawable at this point, you can't get modelconditionflags now."));
//		static ModelConditionFlags noFlags;
//		return noFlags;
//	}
//}



//=============================================================================
// Object::getRelationship
//=============================================================================
Relationship Object::getRelationship(const Object *that) const
{ 
	const Team *myTeam = getTeam();

	if (myTeam && that)
	{
		if (getIsUndetectedDefector())
		{
			return NEUTRAL; // so my AI does not give away my position by auto acquire
		}
		else if (that->getIsUndetectedDefector())
		{
			return ALLIES; // so I treat undetecteddefectors like they were my very own
		}
		else
		{
			return myTeam->getRelationship( that->getTeam() );
		}
	}

	return NEUTRAL;

}

//=============================================================================
// Object::getControllingPlayer
//=============================================================================
Player * Object::getControllingPlayer() const
{ 
	const Team* myTeam = this->getTeam();	
	if (myTeam)
		return myTeam->getControllingPlayer(); 

	return nullptr;
}

//=============================================================================
void Object::setProducer(const Object* obj)
{
	m_producerID = obj ? obj->getID() : INVALID_ID;
// seems like a good idea, but is not. (srj)
//	if (obj)
//		m_indicatorColor = obj->m_indicatorColor;
}

//=============================================================================
void Object::setBuilder( const Object *obj )
{

  m_builderID = obj ? obj->getID() : INVALID_ID;

}

//=============================================================================
void Object::setCustomIndicatorColor(Color c) 
{ 
	if (m_indicatorColor != c)
	{
		m_indicatorColor = c;
		if (m_drawable)
			m_drawable->changedTeam();
	}
}

//=============================================================================
void Object::removeCustomIndicatorColor() 
{ 
	setCustomIndicatorColor(0); 
}

//=============================================================================
// Object::getIndicatorColor
//=============================================================================
Color Object::getIndicatorColor() const
{
	if (m_indicatorColor == 0)
	{
		const Team *myTeam = getTeam();
		if (myTeam)
		{
			const Player* p = myTeam->getControllingPlayer();
			if (p)
			{
				return p->getPlayerColor();
			}
		}
		return GameMakeColor(0, 0, 0, 255);
	}
	else
	{
		return m_indicatorColor;
	}
}

//=============================================================================
// Object::getNightIndicatorColor - used to make blue/purple easier to see on night models.
//=============================================================================
Color Object::getNightIndicatorColor() const
{
	if (m_indicatorColor == 0)
	{
		const Team *myTeam = getTeam();
		if (myTeam)
		{
			const Player* p = myTeam->getControllingPlayer();
			if (p)
			{
				return p->getPlayerNightColor();
			}
		}
		return GameMakeColor(0, 0, 0, 255);
	}
	else
	{
		return m_indicatorColor;
	}
}

//=============================================================================
// Object::isLocallyControlled
//=============================================================================
Bool Object::isLocallyControlled() const
{
	return getControllingPlayer() == ThePlayerList->getLocalPlayer();
}

//=============================================================================
// Object::isLocallyControlled
//=============================================================================
Bool Object::isNeutralControlled() const
{
	return getControllingPlayer() == ThePlayerList->getNeutralPlayer();
}

//-------------------------------------------------------------------------------------------------
inline Bool isPosDifferent(const Coord3D* a, const Coord3D* b)
{
	// this is necessary because PhysicsBehavior may generate tiny changes even when 
	// "standing still", due to roundoff errors. It's important that we only invalidate
	// the PartitionManager stuff when the pos/orientation really changes (for efficiency purposes)
	// so we must put in some cleverness...
	const Real THRESH = 0.01f;

	if (fabs(a->x - b->x) > THRESH)
		return true;

	if (fabs(a->y - b->y) > THRESH)
		return true;

	if (fabs(a->z - b->z) > THRESH)
		return true;

	return false;
}

//-------------------------------------------------------------------------------------------------
inline Bool isAngleDifferent(Real a, Real b)
{
	// this is necessary because PhysicsBehavior may generate tiny changes even when 
	// "standing still", due to roundoff errors. It's important that we only invalidate
	// the PartitionManager stuff when the pos/orientation really changes (for efficiency purposes)
	// so we must put in some cleverness...

	const Real THRESH = 0.01f;	// in radians, this is approx 1/2 degree.

	if (fabs(a - b) > THRESH)
		return true;

	return false;
}

//-------------------------------------------------------------------------------------------------
void Object::reactToTurretChange( WhichTurretType turret, Real oldRotation, Real oldPitch )
{
	Real currentRotation = 0.0f;
	Real currentPitch = 0.0f;
	if( getAI() )
	{
		getAI()->getTurretRotAndPitch( turret, &currentRotation, &currentPitch );
	}
	Bool rotationChange = (currentRotation != oldRotation);
//	Bool pitchChange = (currentPitch != oldPitch);

	if( rotationChange )
	{
		if (getContain())
			getContain()->containReactToTransformChange();
	}
}

//-------------------------------------------------------------------------------------------------
//DECLARE_PERF_TIMER(Object_reactToTransformChange)
void Object::reactToTransformChange(const Matrix3D* oldMtx, const Coord3D* oldPos, Real oldAngle)
{
	//USE_PERF_TIMER(Object_reactToTransformChange)
	if(_isnan(getPosition()->x) || _isnan(getPosition()->y) || _isnan(getPosition()->z)) {
		DEBUG_CRASH(("Object pos is nan."));
		TheGameLogic->destroyObject(this);
	}
	if (m_drawable)
	{
  	m_drawable->setTransformMatrix( this->getTransformMatrix() );
	}

	Bool posDiff = isPosDifferent(oldPos, getPosition());
	Bool angDiff = isAngleDifferent(oldAngle, getOrientation());

	if (posDiff || angDiff)
	{
		if (m_partitionData)
			m_partitionData->makeDirty(true);
		
		if (getContain())
			getContain()->containReactToTransformChange();
	}

	if (posDiff)
	{
		setTriggerAreaFlagsForChangeInPosition(); // Update for entered/exited
		
		Region3D mapExtent;
		TheTerrainLogic->getExtent(&mapExtent);
		if (mapExtent.isInRegionNoZ(getPosition()))
			m_privateStatus &= ~OFF_MAP;
		else
			m_privateStatus |= OFF_MAP;
	}
}




//-------------------------------------------------------------------------------------------------
Bool Object::isStructure(void) const
{
	return isKindOf(KINDOF_STRUCTURE);
}

//-------------------------------------------------------------------------------------------------
Bool Object::isFactionStructure(void) const
{
	return isAnyKindOf( KINDOFMASK_FS );
}

//-------------------------------------------------------------------------------------------------
Bool Object::isNonFactionStructure(void) const
{
	return isStructure() && !isFactionStructure();
}

void localIsHero( Object *obj, void* userData )
{
	Bool *hero = static_cast<Bool*>(userData);
	
	if( obj && obj->isKindOf( KINDOF_HERO ) )
	{
		*hero = TRUE;
	}
}

//-------------------------------------------------------------------------------------------------
Bool Object::isHero(void) const
{
	ContainModuleInterface *contain = getContain();
	if( contain )
	{
		Bool heroInside = FALSE;
		contain->iterateContained( localIsHero, reinterpret_cast<void*>(&heroInside), FALSE );
		if( heroInside )
		{
			return TRUE;
		}
	}
	return isKindOf( KINDOF_HERO );
}

//-------------------------------------------------------------------------------------------------
void Object::setReceivingDifficultyBonus(Bool receive)
{
	if (receive == m_isReceivingDifficultyBonus) {
		return;
	}

	m_isReceivingDifficultyBonus = receive;
	getControllingPlayer()->friend_applyDifficultyBonusesForObject(this, m_isReceivingDifficultyBonus);
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/** Clear the previous entered/exited flags. */
//-------------------------------------------------------------------------------------------------
void Object::updateTriggerAreaFlags()
{
	Int j = 0;
	// Update the flags, and remove any trigger areas that this object isn't inside.
	for (Int i=0; i<m_numTriggerAreasActive; i++) 
	{
		if (!m_triggerInfo[j].isInside) 
			continue;
		m_triggerInfo[j].entered = false;
		m_triggerInfo[j].exited = false;
		m_triggerInfo[j].isInside = m_triggerInfo[i].isInside;
		m_triggerInfo[j].pTrigger = m_triggerInfo[i].pTrigger;
		j++;
	}
	m_numTriggerAreasActive = static_cast<Byte>(j);
}

//-------------------------------------------------------------------------------------------------
void Object::onCollide( Object *other, const Coord3D *loc, const Coord3D *normal )
{
	for (BehaviorModule** m = m_behaviors; *m; ++m)
	{
		CollideModuleInterface* collide = (*m)->getCollide();
		if (!collide)
			continue;

		// check each time thru the loop, in case a collide module sets it
		if( getStatusBits().test( OBJECT_STATUS_NO_COLLISIONS ) )
		{
#ifdef DEBUG_CRC
			//DEBUG_LOG(("Object::onCollide() - OBJECT_STATUS_NO_COLLISIONS set\n"));
#endif
			break;
		}
#ifdef DEBUG_CRC
		//DEBUG_LOG(("Object::onCollide() - calling collide module\n"));
#endif
		collide->onCollide(other, loc, normal);
	}
}

//-------------------------------------------------------------------------------------------------
Bool Object::isSalvageCrate() const
{
	for( BehaviorModule** m = m_behaviors; *m; ++m )
	{
		CollideModuleInterface* collide = (*m)->getCollide();
		if( collide && collide->isSalvageCrateCollide() )
		{
			return true;
		}
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
/** 
	Our owning player is telling us to recheck our UpgradeModules, as an upgrade has completed
 */
void Object::updateUpgradeModules()
{
	if( testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION) )
		return; // No upgrade can run if we are under construction.  The three places that clear UnderConstruction will re-update us.
	
	if( testStatus( OBJECT_STATUS_DESTROYED ) )
		return; // Patch 1.03 -- Fixes crash when you upgrade a fake GLA command center to a real one if (toxic or demo).
	
	if( getControllingPlayer() == nullptr )
		return;  // This can only happen in game teardown.  No upgrades for you without a player.  Weird crashes are bad.

	UpgradeMaskType playerMask = getControllingPlayer()->getCompletedUpgradeMask();
	UpgradeMaskType objectMask = getObjectCompletedUpgradeMask();
	UpgradeMaskType maskToCheck = playerMask;
	maskToCheck.set( objectMask );
	// We need to add in all of the already owned upgrades to handle "AND" requiring upgrades.
	// We combine all the masks in case someone has a Object AND Player combination

	for (BehaviorModule** module = m_behaviors; *module; ++module)
	{
		UpgradeModuleInterface* upgrade = (*module)->getUpgrade();
		if (!upgrade)
			continue;

		if( !upgrade->isAlreadyUpgraded() )
		{
			upgrade->attemptUpgrade( maskToCheck );
		}
	}
}

//-------------------------------------------------------------------------------------------------
//This function sucks.
//It was added for objects that can disguise as other objects and contain upgraded subobject overrides. 
//A concrete example is the bomb truck. Different payloads are displayed based on which upgrades have been
//made. When the bomb truck disguises as something else, these subobjects are lost because the vector is
//stored in W3DDrawModule. When we revert back to the original bomb truck, we call this function to 
//recalculate those upgraded subobjects.
//-------------------------------------------------------------------------------------------------
void Object::forceRefreshSubObjectUpgradeStatus()
{
	for (BehaviorModule** module = m_behaviors; *module; ++module)
	{
		UpgradeModuleInterface* upgrade = (*module)->getUpgrade();
		if (!upgrade)
			continue;

		if( upgrade->isSubObjectsUpgrade() )
		{
			upgrade->forceRefreshUpgrade();
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Returns whether an object entered or exited an area. */
//-------------------------------------------------------------------------------------------------
Bool Object::didEnterOrExit() const
{
	if (isKindOf(KINDOF_INERT)) {
		return FALSE;
	}
	// note that this needs to return true if we
	// entered or exited on the current frame OR
	// the previous frame... since the current execution
	// order is ScriptEngine, then ObjectUpdates,
	// enter/exits detected in ObjectUpdate on frame N
	// won't be noticed by the ScriptEngine till frame N+1.
	UnsignedInt now = TheGameLogic->getFrame();
	return m_enteredOrExitedFrame == now || m_enteredOrExitedFrame == now - 1;
}

//-------------------------------------------------------------------------------------------------
/** Returns whether an object entered an area. */
//-------------------------------------------------------------------------------------------------
Bool Object::didEnter(const PolygonTrigger *pTrigger) const
{
	if (!didEnterOrExit()) 
		return false;

	DEBUG_ASSERTCRASH(!isKindOf(KINDOF_INERT), ("Asking whether an inert object entered or exited. This is invalid.\n"));

	for (Int i=0; i<m_numTriggerAreasActive; i++) 
	{
		if (m_triggerInfo[i].entered && m_triggerInfo[i].pTrigger == pTrigger) 
			return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
/** Returns whether an object entered an area. */
//-------------------------------------------------------------------------------------------------
Bool Object::didExit(const PolygonTrigger *pTrigger) const
{
	if (!didEnterOrExit()) 
		return false;

	DEBUG_ASSERTCRASH(!isKindOf(KINDOF_INERT), ("Asking whether an inert object entered or exited. This is invalid.\n"));
	for (Int i=0; i<m_numTriggerAreasActive; i++) 
	{
		if (m_triggerInfo[i].exited && m_triggerInfo[i].pTrigger == pTrigger) 
			return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
/** Returns whether an object is inside an area. */
//-------------------------------------------------------------------------------------------------
Bool Object::isInside(const PolygonTrigger *pTrigger) const
{
	DEBUG_ASSERTCRASH(!isKindOf(KINDOF_INERT), ("Asking whether an inert is inside a trigger area. This is invalid.\n"));

	for (Int i=0; i<m_numTriggerAreasActive; i++) 
	{
		if (m_triggerInfo[i].isInside && m_triggerInfo[i].pTrigger == pTrigger) 
			return true;
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
/** Get production exit interface in object is present */
// ------------------------------------------------------------------------------------------------
ExitInterface *Object::getObjectExitInterface() const
{
	ExitInterface *exitInterface = nullptr;

	for( BehaviorModule **umod = m_behaviors; *umod; ++umod )
	{
		if( (exitInterface = (*umod)->getUpdateExitInterface()) != nullptr )
			break;
	}

	// If you don't have a fancy one, you may have one from your contain module,
	// since if you can contain something, they will need to get out.
	if( exitInterface == nullptr )
	{
		ContainModuleInterface *cmod = getContain();
		if( cmod )
		{
			exitInterface = cmod->getContainExitInterface();
		}
	}

	return exitInterface;

}  // end getObjectExitInterface

//-------------------------------------------------------------------------------------------------
/** Checks the object against trigger areas when the position changes. */
//-------------------------------------------------------------------------------------------------
void Object::setTriggerAreaFlagsForChangeInPosition()
{
	// projectiles cannot trigger areas. (jkmcd)
	// neither can inert objects, like the radar ping, etc. (jkmcd)
	if (isKindOf(KINDOF_PROJECTILE) || isKindOf(KINDOF_INERT)) 
		return;

	ICoord3D iPos;
	Coord3D pos = *getPosition();
	iPos.x = REAL_TO_INT(pos.x);
	iPos.y = REAL_TO_INT(pos.y);
	iPos.z = 0; // Trigger areas compare on xy only.
	if (m_iPos.x == iPos.x && m_iPos.y == iPos.y) 
	{
		return; // didn't move enough to change integer position.
	}			 

	if (!isKindOf(KINDOF_IMMOBILE)) {
		if (isKindOf(KINDOF_INFANTRY) || isKindOf(KINDOF_VEHICLE) ) {
			TheGameClient->notifyTerrainObjectMoved(this);
		}
	}

	if (getAIUpdateInterface()) 
	{
		TheAI->pathfinder()->updatePos(this, getPosition());
	}

	UnsignedInt now = TheGameLogic->getFrame();
	if (m_enteredOrExitedFrame != 0 && m_enteredOrExitedFrame != now)
		updateTriggerAreaFlags();

	// Check for exited.
	Int i;
	for (i=0; i<m_numTriggerAreasActive; i++) 
	{
		if (!m_triggerInfo[i].pTrigger->pointInTrigger(m_iPos)) 
		{
			m_triggerInfo[i].isInside = false;
			m_triggerInfo[i].exited = true;
			m_enteredOrExitedFrame = now;
			if (m_team) 
				m_team->setEnteredExited();
			TheGameLogic->updateObjectsChangedTriggerAreas();
#ifdef _DEBUG
			//TheScriptEngine->AppendDebugMessage("Object exited.", false);
#endif
		}
	}

	m_iPos = iPos;

	for (const PolygonTrigger *pTrig = PolygonTrigger::getFirstPolygonTrigger(); pTrig; pTrig = pTrig->getNext()) 
	{
		Bool skip = false;
		for (i = 0; i < m_numTriggerAreasActive; i++) 
		{
			if (m_triggerInfo[i].pTrigger == pTrig) 
			{
				// Already handled this one in the check for exited above.
				skip = true;
				break;
			}
		}
		if (skip) 
			continue;
		if (pTrig->pointInTrigger(m_iPos)) 
		{
			if (m_numTriggerAreasActive < MAX_TRIGGER_AREA_INFOS) 
			{
				m_triggerInfo[m_numTriggerAreasActive].isInside = true;
				m_triggerInfo[m_numTriggerAreasActive].entered = true;
				m_triggerInfo[m_numTriggerAreasActive].exited = false;
				m_triggerInfo[m_numTriggerAreasActive].pTrigger = pTrig;  
				m_enteredOrExitedFrame = now;
				if (m_team) 
					m_team->setEnteredExited();
				TheGameLogic->updateObjectsChangedTriggerAreas();
				++m_numTriggerAreasActive;
#ifdef _DEBUG
				//TheScriptEngine->AppendDebugMessage("Object entered.", false);
#endif
			} 
			else 
			{
				// Shouldn't happen.
				static Bool didWarn = false;
				if (!didWarn) 
				{
					didWarn = true;
					TheScriptEngine->AppendDebugMessage("***WARNING - Too many nested trigger areas. ***", true);
				}
			}
		}

	}

}



//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool Object::isInList(Object **pListHead) const
{
	Bool result = m_prev || m_next || *pListHead == this;
#ifdef INTENSE_DEBUG
	Bool found = false;
	for (Object* o = *pListHead; o; o = o->m_next)
	{
		if (o == this)
		{
			found = true;
			break;
		}
	}
	DEBUG_ASSERTCRASH(found==result,("inconsistent links in Object::isInList"));
#endif
	return result;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void Object::prependToList(Object **pListHead)
{
	DEBUG_ASSERTCRASH(!isInList(pListHead), ("obj is already in a list"));

	m_prev = nullptr;
	m_next = *pListHead;
	if (*pListHead)
		(*pListHead)->m_prev = this;
	*pListHead = this;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void Object::setLayer(PathfindLayerEnum layer)
{
	if (layer!=m_layer) {
#define no_SET_LAYER_INTENSE_DEBUG
#ifdef SET_LAYER_INTENSE_DEBUG
		DEBUG_LOG(("Changing layer from %d to %d\n", m_layer, layer));
		if (m_layer != LAYER_GROUND) {
			if (TheTerrainLogic->objectInteractsWithBridgeLayer(this, m_layer)) {
				DEBUG_CRASH(("Probably shouldn't be chaging layer. jba."));
			}
		}
#endif
		TheAI->pathfinder()->removePos(this);
		m_layer = layer;
		TheAI->pathfinder()->updatePos(this, getPosition());
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void Object::setDestinationLayer(PathfindLayerEnum layer)
{
	if (layer!=m_destinationLayer) {
		m_destinationLayer = layer;
	}
}

// ------------------------------------------------------------------------------------------------
/** Set unique ID */
// ------------------------------------------------------------------------------------------------
void Object::setID( ObjectID id )
{

	// sanity 
	DEBUG_ASSERTCRASH( id != INVALID_ID, ("Object::setID - Invalid id\n") );

	// if id hasn't changed do nothing
	if( m_id == id )
		return;
			
	// remove this objects previous id from the lookup table
	TheGameLogic->removeObjectFromLookupTable( this );

	// assign new id
	m_id = id;

	// add new id to lookup table
	TheGameLogic->addObjectToLookupTable( this );

}  // end setID

// ------------------------------------------------------------------------------------------------
Real Object::calculateHeightAboveTerrain(void) const 
{
	const Coord3D* pos = getPosition();
	Real terrainZ = TheTerrainLogic->getLayerHeight( pos->x, pos->y, m_layer );
	Real myZ = pos->z;
	return myZ - terrainZ;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void Object::removeFromList(Object **pListHead)
{
	if (m_next)
		m_next->m_prev = m_prev;

	if (m_prev)
		m_prev->m_next = m_next;
	else
		*pListHead = m_next;

	m_prev = nullptr;
	m_next = nullptr;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void Object::friend_prepareForMapBoundaryAdjust(void)
{
	// NOTE - DO NOT remove from pathfind map. jba.
	// NO NO. jba. TheAI->pathfinder()->removeObjectFromPathfindMap( this );

	// remove from the radar, remove from the partition manager	
	TheRadar->removeObject(this);
	ThePartitionManager->unRegisterObject(this);

	// The whole PartitionManager and all of the Looker data is about to be blown away, 
	// so forget what I think I have done
	m_partitionLastLook->reset();
	m_partitionRevealAllLastLook->reset();
	m_partitionLastShroud->reset();

	m_partitionLastThreat->reset();
	m_partitionLastValue->reset();
	
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void Object::friend_notifyOfNewMapBoundary(void)
{
	ThePartitionManager->registerObject(this);
	TheRadar->addObject(this);
	TheAI->pathfinder()->addObjectToPathfindMap( this );

	// Now that the PartitionManager has finished its reset, we need to relook
	handlePartitionCellMaintenance();

	Region3D mapExtent;
	TheTerrainLogic->getExtent(&mapExtent);
	if (mapExtent.isInRegionNoZ(getPosition()))
		m_privateStatus &= ~OFF_MAP;
	else
		m_privateStatus |= OFF_MAP;
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void Object::calcNaturalRallyPoint(Coord2D *pt)
{
	const Matrix3D *transform = getTransformMatrix();
	Vector3 v;

	//
	// get the natural rally point from the template, this coord is in model space relative
	// to the model (0,0,0)
	//
/*
	const Coord3D *naturalRallyPoint;
  naturalRallyPoint = m_template->getNaturalRallyPoint();
	v.X = naturalRallyPoint->x;
	v.Y = naturalRallyPoint->y;
	v.Z = naturalRallyPoint->z;
*/
	v.Set( 0, 0, 0 );

	// transform the point into world space
	transform->Transform_Vector( *transform, v, &v );

	// we're only concerned with the 2D elements for now
	pt->x = v.X;
	pt->y = v.Y;

}

//-------------------------------------------------------------------------------------------------
Module* Object::findModule(NameKeyType key) const 
{
	Module* m = nullptr;

	for (BehaviorModule** b = m_behaviors; *b; ++b)
	{
		if ((*b)->getModuleNameKey() == key)
		{
#ifdef INTENSE_DEBUG
			if (m == nullptr)
			{
				m = *b;
			}
			else
			{
				DEBUG_CRASH(("Duplicate modules found for name %s!\n",TheNameKeyGenerator->keyToName(key).str()));
			}
#else
			m = *b;
			break;
#endif
		}
	}

	return m;
}

//-------------------------------------------------------------------------------------------------
/**
 * Returns true if object is currently able to move.
 */
Bool Object::isMobile() const
{
	if (isKindOf(KINDOF_IMMOBILE))
		return false;

	if( isDisabled() )
		return false;

	return true;
}

//-------------------------------------------------------------------------------------------------
VeterancyLevel Object::getVeterancyLevel() const 
{ 
	return m_experienceTracker ? m_experienceTracker->getVeterancyLevel() : LEVEL_REGULAR; 
}

//-------------------------------------------------------------------------------------------------
void Object::friend_bindToDrawable( Drawable *draw ) 
{ 
	m_drawable = draw;
	if (m_drawable)
	{
		ModelConditionFlags set;
		ModelConditionFlags clr;
		for (int i = 0; i < WEAPONSET_COUNT; ++i)
		{
			ModelConditionFlagType mcs = TheWeaponSetTypeToModelConditionTypeMap[i];
			if( mcs != MODELCONDITION_INVALID )
			{
				if (m_curWeaponSetFlags.test(i))
					set.set(mcs);
				else
					clr.set(mcs);
			}
		}
		if (TheGlobalData)
		{
			if (TheGlobalData->m_forceModelsToFollowTimeOfDay)
			{
				set.set(MODELCONDITION_NIGHT, (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT) ? 1 : 0);
			}

			if (TheGlobalData->m_forceModelsToFollowWeather)
			{
				set.set(MODELCONDITION_SNOW, (TheGlobalData->m_weather == WEATHER_SNOWY) ? 1 : 0);
			}
		}
		m_drawable->clearAndSetModelConditionFlags(clr, set);
	}

	for (BehaviorModule** b = m_behaviors; *b; ++b)
	{
		(*b)->onDrawableBoundToObject();
	}
}	

//-------------------------------------------------------------------------------------------------
void Object::setSelectable(Bool selectable) 
{ 
	m_isSelectable = selectable; 
	if (m_drawable)
	{
		m_drawable->setSelectable(selectable);
	}
}

//-------------------------------------------------------------------------------------------------
Bool Object::isSelectable() const
{
//	return getTemplate()->isKindOf(KINDOF_ALWAYS_SELECTABLE) 
//				|| (m_isSelectable 
//						&& !testStatus(OBJECT_STATUS_UNSELECTABLE) 
//						&& !isEffectivelyDead()
//						&& !getTemplate()->isKindOf(KINDOF_DRONE)//Most drones are unselectable from being slaved, but the SpyDrone needs help
//						);
 

	if (getTemplate()->isKindOf(KINDOF_ALWAYS_SELECTABLE))
    return TRUE;

	if ( m_isSelectable )
    if ( !testStatus(OBJECT_STATUS_UNSELECTABLE) ) 
		  if ( !isEffectivelyDead() )
				//if ( !getTemplate()->isKindOf(KINDOF_DRONE) )//Most drones are unselectable from being slaved, but the SpyDrone needs help
				// Generals excludes drones here (including the unbound spy drone); Zero Hour marks its drones
				// with KINDOF_NO_SELECT, which only "next unit" checks.
				if( !TheGlobalData->m_dronesNeverSelectable || !getTemplate()->isKindOf( KINDOF_DRONE ) )
					return TRUE;

  return FALSE;
}

//-------------------------------------------------------------------------------------------------
Bool Object::isMassSelectable() const
{
	return isSelectable() && !isKindOf(KINDOF_STRUCTURE);
}

//-------------------------------------------------------------------------------------------------
void Object::onVeterancyLevelChanged( VeterancyLevel oldLevel, VeterancyLevel newLevel, Bool provideFeedback )
{
	updateUpgradeModules();

	const UpgradeTemplate* up = TheUpgradeCenter->findVeterancyUpgrade(newLevel);
	if (up)
		giveUpgrade(up);

	BodyModuleInterface* body = getBodyModule();
	if (body)
		body->onVeterancyLevelChanged( oldLevel, newLevel, provideFeedback );
	
	Bool hideAnimationForStealth = FALSE;
	if( !isLocallyControlled() && 
			testStatus( OBJECT_STATUS_STEALTHED ) && 
			!testStatus( OBJECT_STATUS_DETECTED ) && 
			!testStatus( OBJECT_STATUS_DISGUISED ) )
	{
		hideAnimationForStealth = TRUE;
	}

	Bool doAnimation = ( ! hideAnimationForStealth 
											&& (newLevel > oldLevel) 
											&& ( ! isKindOf(KINDOF_IGNORED_IN_GUI))); //First, we plan to do the animation if the level went up

	switch (newLevel)
	{
		case LEVEL_REGULAR:
			clearWeaponSetFlag(WEAPONSET_VETERAN);
			clearWeaponSetFlag(WEAPONSET_ELITE);
			clearWeaponSetFlag(WEAPONSET_HERO);
			clearWeaponBonusCondition(WEAPONBONUSCONDITION_VETERAN);
			clearWeaponBonusCondition(WEAPONBONUSCONDITION_ELITE);
			clearWeaponBonusCondition(WEAPONBONUSCONDITION_HERO);
			doAnimation = FALSE;//... but not if somehow up to Regular
			break;
		case LEVEL_VETERAN:
			setWeaponSetFlag(WEAPONSET_VETERAN);
			clearWeaponSetFlag(WEAPONSET_ELITE);
			clearWeaponSetFlag(WEAPONSET_HERO);
			setWeaponBonusCondition(WEAPONBONUSCONDITION_VETERAN);
			clearWeaponBonusCondition(WEAPONBONUSCONDITION_ELITE);
			clearWeaponBonusCondition(WEAPONBONUSCONDITION_HERO);
			break;
		case LEVEL_ELITE:
			clearWeaponSetFlag(WEAPONSET_VETERAN);
			setWeaponSetFlag(WEAPONSET_ELITE);
			clearWeaponSetFlag(WEAPONSET_HERO);
			clearWeaponBonusCondition(WEAPONBONUSCONDITION_VETERAN);
			setWeaponBonusCondition(WEAPONBONUSCONDITION_ELITE);
			clearWeaponBonusCondition(WEAPONBONUSCONDITION_HERO);
			break;
		case LEVEL_HEROIC:
			clearWeaponSetFlag(WEAPONSET_VETERAN);
			clearWeaponSetFlag(WEAPONSET_ELITE);
			setWeaponSetFlag(WEAPONSET_HERO);
			clearWeaponBonusCondition(WEAPONBONUSCONDITION_VETERAN);
			clearWeaponBonusCondition(WEAPONBONUSCONDITION_ELITE);
			setWeaponBonusCondition(WEAPONBONUSCONDITION_HERO);
			break;
	}

	if( doAnimation && TheGameLogic->getDrawIconUI() && provideFeedback )
	{
		if( TheAnim2DCollection && TheGlobalData->m_levelGainAnimationName.isEmpty() == FALSE )
		{
			Anim2DTemplate *animTemplate = TheAnim2DCollection->findTemplate( TheGlobalData->m_levelGainAnimationName );

			Coord3D pos = *getPosition(); 
			pos.add(&m_healthBoxOffset);

			TheLogicToClient->uiAddWorldAnimation( animTemplate,
																			&pos,
																			WORLD_ANIM_FADE_ON_EXPIRE,
																			TheGlobalData->m_levelGainAnimationDisplayTimeInSeconds,
																			TheGlobalData->m_levelGainAnimationZRisePerSecond);
		}

		AudioEventRTS soundToPlay = TheLogicAudio->getMiscAudio()->m_unitPromoted;	
		soundToPlay.setObjectID( getID() );
		TheLogicAudio->addAudioEvent( &soundToPlay );
	}

}

//-------------------------------------------------------------------------------------------------
/**
	* Mask/Un-Mask an object
	*/
void Object::maskObject( Bool mask )
{

	// set or clear the mask bit
	setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_MASKED ), mask );

	//
	// when masking objects they become unselected ... we do this in any situation for
	// any player cause you aren't allowed to select masked objects, if the object is not
	// selected (ie, belongs to another player) it's no big deal cause it won't be selected
	// anyway
	//

	if (mask)
		TheGameLogic->deselectObject(this, ~getControllingPlayer()->getPlayerMask(), TRUE);

}  // end maskObject

//-------------------------------------------------------------------------------------------------
/*
 * returns true if the current locomotor is an airborne one
 */
Bool Object::isUsingAirborneLocomotor( void ) const
{
	return ( m_ai && m_ai->getCurLocomotor() && ((m_ai->getCurLocomotor()->getLegalSurfaces() & LOCOMOTORSURFACE_AIR) != 0) );
}

//-------------------------------------------------------------------------------------------------
//THIS FUNCTION BELONGS AT THE OBJECT LEVEL BECAUSE THERE IS AT LEAST ONE SPECIAL UNIT
//(ANGRY MOB) WHICH NEEDS LOGIC-SIDE POSITION CALC'S...
//IT WOULD PROBABLY BE WISE TO MOVE ALL THE HARD-CODED DEFAULTS BELOW
//INTO A NEW Drawable::getHealthBox..() WHICH USES GEOM0INFO, MODEL DATA, INI DATA, ETC.
void Object::getHealthBoxPosition(Coord3D& pos) const
{ 
	pos = *getPosition(); 
	pos.z += getGeometryInfo().getMaxHeightAbovePosition() + 10;
	pos.add(&m_healthBoxOffset);

	// this needs to get moved to the mobspawnerupdate
	if (isKindOf(KINDOF_MOB_NEXUS)) // quicker idiot test
	{
		pos.z += 20;// dear God, I confess my kluge, and repent.
	}
}

//-------------------------------------------------------------------------------------------------
//THIS FUNCTION BELONGS AT THE OBJECT LEVEL BECAUSE THERE IS AT LEAST ONE SPECIAL UNIT
//(ANGRY MOB) WHICH NEEDS LOGIC-SIDE POSITION CALC'S...
//IT WOULD PROBABLY BE WISE TO MOVE ALL THE HARD-CODED DEFAULTS BELOW
//INTO A NEW Drawable::getHealthBox..() WHICH USES GEOM0INFO, MODEL DATA, INI DATA, ETC.
Bool Object::getHealthBoxDimensions(Real &healthBoxHeight, Real &healthBoxWidth) const
{ 

#ifdef CALC_HEALTHBAR_FROM_HITPOINTS
	Real maxHP = getBodyModule()->getMaxHealth();

	if( isKindOf( KINDOF_STRUCTURE ) )
	{
		//enforce healthBoxHeightMinimum/Maximum
		healthBoxHeight = min(3.0f, max(5.0f, maxHP/50));
		//enforce healthBoxWidthMinimum/Maximum
		healthBoxWidth = min(150.0f, max(100.0f, maxHP/10));
		return true;
	}
	else if ( isKindOf(KINDOF_MOB_NEXUS) ) 
	{
		//enforce healthBoxHeightMinimum/Maximum
		healthBoxHeight = min(3.0f, max(5.0f, maxHP/50));
		//enforce healthBoxWidthMinimum/Maximum
		healthBoxWidth = min(100.0f, max(66.0f, maxHP/10));
		return true;
	}
	else if ( isKindOf( KINDOF_IGNORED_IN_GUI ) )
	{
		healthBoxHeight = 0;
		healthBoxWidth = 0;
		return false;
	}
	else
	{
		//enforce healthBoxHeightMinimum/Maximum
		healthBoxHeight = min(3.0f, max(5.0f, maxHP/50));
		//enforce healthBoxWidthMinimum/Maximum
		healthBoxWidth = min(150.0f, max(35.0f, maxHP/10));
		return true;
	}
#else

	if ( isKindOf( KINDOF_IGNORED_IN_GUI ) )
	{
		healthBoxHeight = 0;
		healthBoxWidth = 0;
		return false;
	}

	//just add the major and minor axes
	Real size = MAX(20.0f, MIN(150.0f, (getGeometryInfo().getMajorRadius() + getGeometryInfo().getMinorRadius())) );
	healthBoxHeight = 3.0f; 
	healthBoxWidth = MAX(20.0f, size * 2.0f);
	return TRUE;

#endif

}


//-------------------------------------------------------------------------------------------------
/**
 * Update this object instance with properties from the map object
 * 
 */
void Object::updateObjValuesFromMapProperties(Dict* properties)
{
	Bool exists;

	AsciiString valStr;
	Bool valBool = false;
	Int valInt = 0;
	Real valReal = 0.0f;

	valStr = properties->getAsciiString(TheKey_objectName, &exists);
	if (exists) {
		setName(valStr);
	}

	valInt = properties->getInt(TheKey_objectMaxHPs, &exists);
	if (exists && valInt >= 0) {
		BodyModuleInterface* body = getBodyModule();
		if (body)	{
			body->setMaxHealth(static_cast<Real>(valInt));
		}
	}

	valInt = properties->getInt(TheKey_objectInitialHealth, &exists);
	if (exists) {
		BodyModuleInterface* body = getBodyModule();
		if (body)	{
			body->setInitialHealth(valInt);
		}
	}

	// set the veterancy level
	valInt = properties->getInt(TheKey_objectVeterancy, &exists);
	if (exists) {
		if (m_experienceTracker && m_experienceTracker->isTrainable())
		{
			m_experienceTracker->setVeterancyLevel(static_cast<VeterancyLevel>(valInt));
		}
	}

	// set the aggressiveness/mood
	valInt = properties->getInt(TheKey_objectAggressiveness, &exists);
	if (exists) {
		AIUpdateInterface *ai = getAIUpdateInterface();
		if (ai)
		{
			ai->setAttitude(static_cast<AttitudeType>(valInt));
		}
	}
	
	// set recruitable
	valBool = properties->getBool(TheKey_objectRecruitableAI, &exists);
	if (exists) {
		if (getAIUpdateInterface())
		{
			getAIUpdateInterface()->setIsRecruitable(valBool);
		}
	}
	
	// set selectable
	valBool = properties->getBool(TheKey_objectSelectable, &exists);
	if (exists) {
		if (valBool != isSelectable()) {
			setSelectable(valBool);
		}
	}
	
	// set the stopping distance
	valReal = properties->getReal(TheKey_objectStoppingDistance, &exists);
	if (exists && valReal >= 0.5f)
	{
		if (getAIUpdateInterface() && getAIUpdateInterface()->getCurLocomotor())
		{
			Locomotor *loco = getAIUpdateInterface()->getCurLocomotor();
			loco->setCloseEnoughDist(valReal);
		}
	}
	
	// set the disabledness of this object
	valBool = properties->getBool(TheKey_objectEnabled, &exists);
	if (exists) {
		setScriptStatus(OBJECT_STATUS_SCRIPT_DISABLED, !valBool);
	}

	// set the disabledness of this object
	valBool = properties->getBool(TheKey_objectPowered, &exists);
	if (exists) {
		setScriptStatus(OBJECT_STATUS_SCRIPT_UNPOWERED, !valBool);
	}

	// set the invulnerability of the object
	valBool = properties->getBool(TheKey_objectIndestructible, &exists);
	if (exists) {
		BodyModuleInterface* body = getBodyModule();
		if (body)	{
			body->setIndestructible(valBool);
		}
	}

	// set the sellability of the object
	valBool = properties->getBool(TheKey_objectUnsellable, &exists);
	if (exists) {
		setScriptStatus(OBJECT_STATUS_SCRIPT_UNSELLABLE, valBool);
	}

	//Set the player targetable setting of the object
	valBool = properties->getBool( TheKey_objectTargetable, &exists );
	if( exists ) 
	{
		setScriptStatus(OBJECT_STATUS_SCRIPT_TARGETABLE, valBool);
	}

	// adjust the vision distance of this object, overriding its default vision distance
	valInt = properties->getInt(TheKey_objectVisualRange, &exists);
	if (exists)
	{
		if (valInt < 0)
			valInt = 0;
		m_visionRange = INT_TO_REAL(valInt);
	}

	// adjust the shroud clearing distance of this object, overriding its default distance
	valInt = properties->getInt(TheKey_objectShroudClearingDistance, &exists);
	if (exists)
	{
		if (valInt < 0)
			valInt = static_cast<Int>(0.0f);
		m_shroudClearingRange = INT_TO_REAL(valInt);
	}


	Int upgradeNum = 0;
	do 
	{
		AsciiString keyName;
		keyName.format("%s%d", TheNameKeyGenerator->keyToName(TheKey_objectGrantUpgrade).str(), upgradeNum);
		valStr = properties->getAsciiString(NAMEKEY(keyName), &exists);

		if (exists) 
		{
			const UpgradeTemplate *ut = TheUpgradeCenter->findUpgrade(valStr);
			if (ut)
				giveUpgrade(ut);
		}
		else 
		{
			valStr.clear();
		}

		++upgradeNum;
	} while (!valStr.isEmpty());
	
	Drawable	*drawable = getDrawable();
  if ( drawable )
  {
    valInt = properties->getInt(TheKey_objectTime, &exists);
    if (exists)
    {
      switch (valInt)
      {
      case 1:
        drawable->clearModelConditionState(MODELCONDITION_NIGHT);
        break;
      case 2:
        drawable->setModelConditionState(MODELCONDITION_NIGHT);
        break;
      default:
        break;
      }
    }
    
    valInt = properties->getInt(TheKey_objectWeather, &exists);
    if (exists)
    {
      switch (valInt)
      {
      case 1:
        drawable->clearModelConditionState(MODELCONDITION_SNOW);
        break;
      case 2:
        drawable->setModelConditionState(MODELCONDITION_SNOW);
        break;
      default:
        break;
      }
    }
    
    // See if we are supposed to playing the ambient sound
    Bool soundEnabledExists;
    Bool soundEnabled = properties->getBool( TheKey_objectSoundAmbientEnabled, &soundEnabledExists );
     
    DynamicAudioEventInfo * audioToModify = nullptr;
    Bool infoModified = false;
    valStr = properties->getAsciiString( TheKey_objectSoundAmbient, &exists );
    if ( exists )
    {
      if ( valStr.isEmpty() )
      {
        drawable->setCustomSoundAmbientOff();
        soundEnabledExists = true;
        soundEnabled = false; // Don't bother trying to enable later
      }
      else
      {
        const AudioEventInfo * baseInfo = TheLogicAudio->findAudioEventInfo( valStr );
        DEBUG_ASSERTCRASH( baseInfo != nullptr, ("Cannot find customized ambient sound '%s'", valStr.str() ) );
        if ( baseInfo != nullptr )
        {
          audioToModify = newInstance( DynamicAudioEventInfo )( *baseInfo );
          infoModified = true;
        }
      }
    }

    // Don't do anything more to audio if we forced the ambient sound off
    if ( !( exists && valStr.isEmpty() ) )
    {
      valBool = properties->getBool( TheKey_objectSoundAmbientCustomized, &exists );
      if ( exists && valBool )
      {
        if ( audioToModify == nullptr )
        {
          const AudioEventInfo * baseInfo = drawable->getBaseSoundAmbientInfo( );
          DEBUG_ASSERTCRASH( baseInfo != nullptr, ("getBaseSoundAmbientInfo() return NULL" ) );
          if ( baseInfo != nullptr )
          {
            audioToModify = newInstance( DynamicAudioEventInfo )( *baseInfo );
          }
        }

        if ( audioToModify != nullptr )
        {
          valBool = properties->getBool( TheKey_objectSoundAmbientLooping, &exists );
          if ( exists )
          {
             audioToModify->overrideLoopFlag( valBool );
             infoModified = true;
          }

          valInt = properties->getInt( TheKey_objectSoundAmbientLoopCount, &exists );
          if ( exists && BitTest( audioToModify->m_control, AC_LOOP ) )
          {
            audioToModify->overrideLoopCount( valInt );
            infoModified = true;
          }

          valReal = properties->getReal( TheKey_objectSoundAmbientMinVolume, &exists );
          if ( exists )
          {
            audioToModify->overrideMinVolume( valReal );
            infoModified = true;
          }

          valReal = properties->getReal( TheKey_objectSoundAmbientVolume, &exists );
          if ( exists )
          {
            audioToModify->overrideVolume( valReal );
            infoModified = true;
          }

          valReal = properties->getReal( TheKey_objectSoundAmbientMinRange, &exists );
          if ( exists )
          {
            audioToModify->overrideMinRange( valReal );
            infoModified = true;
          }
          
          valReal = properties->getReal( TheKey_objectSoundAmbientMaxRange, &exists );
          if ( exists )
          {
            audioToModify->overrideMaxRange( valReal );
            infoModified = true;
          }

          valInt = properties->getInt( TheKey_objectSoundAmbientPriority, &exists );
          if ( exists )
          {
            audioToModify->overridePriority ( static_cast<AudioPriority>(valInt) );
            infoModified = true;
          }
        }
      }
    }

    if ( !soundEnabledExists )
    {
      // Decide if the sound should start enabled or not, since the map maker didn't record
      // a preference. Enable permanently looping sounds, disable one-shot sounds by default
      // NOTE: This test should match the tests done in MapObjectProps::mapObjectPageSound::dictToEnabled()
      // when it decided whether or not to show a customized sound as enabled
      if ( audioToModify != nullptr )
      {
        soundEnabled = audioToModify->isPermanentSound();
        soundEnabledExists = true; // To get into enableAmbientSoundFromScript() call.
      }
      else
      {
        // Use default audio
        const AudioEventInfo * baseInfo = drawable->getBaseSoundAmbientInfo( );
        if ( baseInfo != nullptr )
        {
          soundEnabled = baseInfo->isPermanentSound();
          soundEnabledExists = true; // To get into enableAmbientSoundFromScript() call.
        }
      }
    }

    if ( soundEnabledExists && !soundEnabled )
    {
      // Make sure sound doesn't start playing when we set it
      // ...FromScript because this is also controlled by the map designer not the game logic
      drawable->enableAmbientSoundFromScript( false );
    }
    
    if ( infoModified && audioToModify != nullptr )
    {
      // Give a custom, level-specific name
      drawable->mangleCustomAudioName( audioToModify );

      // Pass to TheAudio
      TheLogicAudio->addAudioEventInfo( audioToModify );

      drawable->setCustomSoundAmbientInfo( audioToModify );
      audioToModify = nullptr; // Belongs to TheAudio now
    }

    if ( audioToModify != nullptr )
    {
      deleteInstanceOf(audioToModify);
      audioToModify = nullptr;
    }

    if ( soundEnabledExists && soundEnabled )
    {
      // Play sound now that it is set up, if needed. Don't call if already enabled because that
      // can cause sound to play twice
      // ...FromScript because this is also controlled by the map designer not the game logic
      if ( !drawable->getAmbientSoundEnabledFromScript() )
      {
        drawable->enableAmbientSoundFromScript( true );
      }      
    } 
  }
}

//-------------------------------------------------------------------------------------------------
void Object::friend_adjustPowerForPlayer( Bool incoming )
{
	if (isDisabled() && getTemplate()->getEnergyProduction() > 0) 
	{
		// Disabledness only affects Producers, not Consumers.
		return;
	}

	if (incoming) {
		getControllingPlayer()->getEnergy()->objectEnteringInfluence(this);
	} else {
		getControllingPlayer()->getEnergy()->objectLeavingInfluence(this);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
/** Does this object have this upgrade */
//-------------------------------------------------------------------------------------------------
Bool Object::hasUpgrade( const UpgradeTemplate *upgradeT ) const 
{
	if( m_objectUpgradesCompleted.testForAll( upgradeT->getUpgradeMask() ) )
	{
		return TRUE;
	}
	return FALSE;
}  // end hasUpgrade

//-------------------------------------------------------------------------------------------------
/** Is this object capable of having this upgrade */
//-------------------------------------------------------------------------------------------------
Bool Object::affectedByUpgrade( const UpgradeTemplate *upgradeT ) const 
{
	UpgradeMaskType objectMask = getObjectCompletedUpgradeMask();
	UpgradeMaskType playerMask = getControllingPlayer()->getCompletedUpgradeMask();
	UpgradeMaskType maskToCheck = playerMask;
	maskToCheck.set( objectMask );
	maskToCheck.set( upgradeT->getUpgradeMask() );

	// We need to add in all of the already owned upgrades to handle "AND" requiring upgrades.
	// We combine all the masks in case someone has a Object AND Player combination

	for (BehaviorModule** module = m_behaviors; *module; ++module)
	{
		UpgradeModuleInterface* upgrade = (*module)->getUpgrade();
		if (!upgrade)
			continue;

		if( upgrade->wouldUpgrade( maskToCheck ) )
		{
			// if any of my many upgrade modules would execute in response to this flag, say yes.
			return TRUE;
		}
	}
	return FALSE;

}  // end affectedByUpgrade

//-------------------------------------------------------------------------------------------------
/** Give this upgrade to this object */
//-------------------------------------------------------------------------------------------------
void Object::giveUpgrade( const UpgradeTemplate *upgradeT )
{
	if (upgradeT)
	{
		m_objectUpgradesCompleted.set( upgradeT->getUpgradeMask() );

		//
		// iterate through all the upgrade modules of this object and call the method to
		// grant a new upgrade
		//
		updateUpgradeModules();
	}
}  // end giveUpgrade

//-------------------------------------------------------------------------------------------------
/** Remove this upgrade from this object */
//-------------------------------------------------------------------------------------------------
void Object::removeUpgrade( const UpgradeTemplate *upgradeT )
{
	m_objectUpgradesCompleted.clear( upgradeT->getUpgradeMask() );
	for (BehaviorModule** module = m_behaviors; *module; ++module)
	{
		UpgradeModuleInterface* upgrade = (*module)->getUpgrade();
		if (!upgrade)
			continue;

		// Whoa, please note that while the function is called Object::RemoveUpgrade, it is not removing anything
		// in the sense of undoing the effects.  It is just resetting the upgrade so it may be run again.
		upgrade->resetUpgrade( upgradeT->getUpgradeMask() );
	}
}

//-------------------------------------------------------------------------------------------------
/** Central point for onCapture logic */
//-------------------------------------------------------------------------------------------------
void Object::onCapture( Player *oldOwner, Player *newOwner )
{
	// Everybody dhills when they captured so they don't keep doing something the new player might not want him to be doing
	if( getAIUpdateInterface()  &&  (oldOwner != newOwner) )
		getAIUpdateInterface()->aiIdle(CMD_FROM_AI);

	// this gets the new owner some points
	newOwner->getScoreKeeper()->addObjectCaptured(this);

	// rip through the behavior modules and call the onCapture for any modules that care
	for( BehaviorModule **module = m_behaviors; *module; ++module )
		(*module)->onCapture( oldOwner, newOwner );

	//
	// We have to undo our look for the old team and redo it for the new.
	// onCapture is used now, so it better be called after ownership changes and not before.
	//
	handlePartitionCellMaintenance();
	
	// Design needs the player to be able to sell buildings he steals from the AI's build list, and this is the
	// easiest fix.  The only snafu would be a key building build listed by the AI that the player can capture
	// and the AI tries to capture back but needs to not sell.  In that case, a Cinematic Unsellable version
	// of the building needs to be made.  This fix has been okayed as the most non-lethal in November.
	clearScriptStatus(OBJECT_STATUS_SCRIPT_UNSELLABLE);

	// mark the command bar to redraw
	TheLogicToClient->controlBarMarkUIDirty();

	if (oldOwner!=newOwner && newOwner->isSkirmishAIPlayer()) {
		// The skirmish ai doesn't know what to do with captured faction buildings except sell them.
		if (isFactionStructure()) {
			TheBuildAssistant->sellObject( this );
		}
	}

}  // end onCapture




// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool Object::getSingleLogicalBonePosition(const char* boneName, Coord3D* position, Matrix3D* transform) const
{
	if (m_drawable && m_drawable->getPristineBonePositions( boneName, 0, position, transform, 1 ) == 1 )
	{
		m_drawable->convertBonePosToWorldPos( position, transform, position, transform );
		return true;
	}
	else
	{
		if (position) 
			*position = *getPosition();
		if (transform) 
			*transform = *getTransformMatrix();
		return false;
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool Object::getSingleLogicalBonePositionOnTurret( WhichTurretType whichTurret, const char* boneName, Coord3D* position, Matrix3D* transform ) const
{
	Coord3D turretPosition;
	Coord3D bonePosition;
	if( getDrawable() == nullptr  || getAI() == nullptr )
		return FALSE;

	// We need to find the TurretBone's pristine position.
	getDrawable()->getProjectileLaunchOffset( PRIMARY_WEAPON, 1, nullptr, whichTurret, &turretPosition, nullptr );
	// And the required bone's pristine position
	if( getDrawable()->getPristineBonePositions(boneName, 0, &bonePosition, nullptr, 1) != 1 )
		return FALSE;
	//Then we mojo the Logic position of the required bone like Missile firing does.  Using the logic twist of the turret
	Real turretRotation;
	getAI()->getTurretRotAndPitch( whichTurret, &turretRotation, nullptr );

	Matrix3D boneOffset(TRUE);// This will be from the turret to the requested bone

//	Vector3 bonePositionVector(	bonePosition.x - turretPosition.x, 
//															bonePosition.y - turretPosition.y, 
//															bonePosition.z - turretPosition.z );
	Vector3 bonePositionVector(	bonePosition.x, 
															bonePosition.y, 
															bonePosition.z );
	boneOffset.Translate(bonePositionVector);

	Matrix3D turnAdjustment(TRUE);// this is the turret twist to be applied to the final answer

	turnAdjustment.Translate( turretPosition.x, turretPosition.y, turretPosition.z );
	turnAdjustment.In_Place_Pre_Rotate_Z(turretRotation);
	turnAdjustment.Translate( -turretPosition.x, -turretPosition.y, -turretPosition.z );

	Matrix3D boneLogicTransform;
	boneLogicTransform.mul( turnAdjustment, boneOffset );

	Matrix3D worldTransform;
	convertBonePosToWorldPos(nullptr, &boneLogicTransform, nullptr, &worldTransform);

	Vector3 tmp = worldTransform.Get_Translation();
	Coord3D worldPos;
	worldPos.x = tmp.X;
	worldPos.y = tmp.Y;
	worldPos.z = tmp.Z;

	if( position )
		*position = worldPos;
	if( transform )
		*transform = worldTransform;

	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Int Object::getMultiLogicalBonePosition(const char* boneNamePrefix, Int maxBones, 
																				Coord3D* positions, Matrix3D* transforms, 
																				Bool convertToWorld ) const
{
	Int count;
	if (m_drawable && (count = m_drawable->getPristineBonePositions( boneNamePrefix, 1, positions, transforms, maxBones )) > 0 )
	{
		if( convertToWorld )
		{
			for (Int i = 0; i < count; ++i)
				m_drawable->convertBonePosToWorldPos( positions ? &positions[i] : nullptr, transforms ? &transforms[i] : nullptr, positions ? &positions[i] : nullptr, transforms ? &transforms[i] : nullptr );
		}
		return count;
	}
	else
	{
		return 0;
	}
}

//=============================================================================
// Object::defect, and related methods                                        =
//=============================================================================
void Object::defect( Team* newTeam, UnsignedInt detectionTime )
{
	if ( isContained() ) //@todo (KRIS?) make contained units unselectable, until then... lorenzen 
	{
		return;
	}

	Player *player = getControllingPlayer();
	if ( !player )
		return;

	Team* myTeam = player->getDefaultTeam();
	if ( myTeam == newTeam ) // can't defect from my own team, that would be silly
		return;
	
	// things that are under construction, or sold, cannot defect.
	if (testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION) ||
			testStatus(OBJECT_STATUS_SOLD))
	{
		return;
	}	

	// Before switch ////////////////////////////////////////

	//Design says: 
	ProductionUpdateInterface *production = getProductionUpdateInterface();
	if ( production )
	{
		production->cancelAndRefundAllProduction();
	}

	// pop it up on the radar, so as to warn those who care
	// do this first, since after setTeam() the infiltrator
	// becomes the controllingplayer, not me 

	// But don't do this is if the new team is not a real team.  "'Enemy' infiltration" wouldn't make
	// sense, and we are probably just reverting a cave or something.
	if( friend_getRadarData() && newTeam->getControllingPlayer()->isPlayableSide() && myTeam->getControllingPlayer()->isPlayableSide())
	{
		TheRadar->tryInfiltrationEvent( this );
	}

	friend_setUndetectedDefector( detectionTime > 0 );

	if (m_defectionHelper)
		m_defectionHelper->startDefectionTimer(detectionTime);

	// Switch ////////////////////////////////////////
	setTeam( newTeam );

	// After switch ////////////////////////////////////////
	
	AIUpdateInterface *ai = getAI();

	handlePartitionCellMaintenance();// to clear the shoud for my new master

	if ( ai )
	{
		ai->aiIdle( CMD_FROM_AI );
	}

	// Play our sound indicating we've been defected. (weird verbage, but true.)
	AudioEventRTS voiceDefect = *getTemplate()->getVoiceDefect();
	voiceDefect.setObjectID(getID());
	TheLogicAudio->addAudioEvent(&voiceDefect);

	//make the new recruit the only selected thing, awaiting new command to move, attack, etc...
	Drawable *dr = getDrawable();
	if (dr)
	{
		TheLogicToClient->drawableFlashAsSelected( dr ); //This is the first of several flashes which get cue'd by doDefectorUpdateStuff()
		AudioEventRTS defectorTimerSound = TheLogicAudio->getMiscAudio()->m_defectorTimerTickSound;
		defectorTimerSound.setObjectID( getID() );
		TheLogicAudio->addAudioEvent(&defectorTimerSound);
	}
	
	ContainModuleInterface *ct = getContain();
	if( ct  &&  ct->isKickOutOnCapture() )
	{
		// Caves really really don't want to do this.
		ct->removeAllContained( TRUE );
	}

	// if it has parking places, defect anything parked there.
	for (BehaviorModule** i = getBehaviorModules(); *i; ++i)
	{
		ParkingPlaceBehaviorInterface* pp = (*i)->getParkingPlaceBehaviorInterface();
		if (pp)
		{
			pp->defectAllParkedUnits(newTeam, detectionTime);
			break;
		}
	}

	// defect any mines that are owned by this structure, right now.
	// unfortunately, structures don't keep list of mines they own, so we must do
	// this the hard way :-( [fortunately, this doens't happen very often, so this
	// is probably an acceptable, if icky, solution.] (srj)
	for (Object* mine = TheGameLogic->getFirstObject(); mine; mine = mine->getNextObject())
	{
		if (mine->isKindOf(KINDOF_MINE))
		{
			if (mine->getProducerID() == this->getID())
			{
				mine->setTeam(newTeam);
			}
		}
	}

}

// ------------------------------------------------------------------------------------------------
/** Return the radar priority for this object type */
// ------------------------------------------------------------------------------------------------
RadarPriorityType Object::getRadarPriority( void ) const 
{
	RadarPriorityType priority = RADAR_PRIORITY_INVALID;

	// first, get the priority at the thing template level
	priority = getTemplate()->getDefaultRadarPriority();

	//
	// there are some objects that we want to show up on the radar when they have
	// certain properties ... here we will check for those properties unless the INI
	// setting of "not on radar" has been manually entered which explicitly forbids an
	// object from being on the radar ... by default objects get an "invalid" priority
	// on the radar and this means that we are free to decide one here if we want
	//
	if( priority == RADAR_PRIORITY_INVALID )
	{

		// objects that are "garrisonable" show up on the radar
		ContainModuleInterface *cmi = getContain();
		if( cmi && cmi->isGarrisonable() )
			priority = RADAR_PRIORITY_STRUCTURE;

		// objects that are "capturable" show up on the radar
		if( isKindOf( KINDOF_CAPTURABLE ) )
			priority = RADAR_PRIORITY_STRUCTURE;


	}  // end if

	// Carbombs will show up as units regardless of their default priority
	if ( testStatus( OBJECT_STATUS_IS_CARBOMB ) )
		priority = RADAR_PRIORITY_UNIT;


	// return the priority we're going to use
	return priority;

}  // end getRadarPriority

// ------------------------------------------------------------------------------------------------
AIGroup *Object::getGroup(void)
{
	return m_group;
}

//-------------------------------------------------------------------------------------------------
void Object::enterGroup( AIGroup *group )
{
//	DEBUG_LOG(("***AIGROUP %x involved in enterGroup on %x\n", group, this));
	// if we are in another group, remove ourselves from it first
	leaveGroup();

	m_group = group;
}

//-------------------------------------------------------------------------------------------------
void Object::leaveGroup( void )
{
//	DEBUG_LOG(("***AIGROUP %x involved in leaveGroup on %x\n", m_group, this));
	// if we are in a group, remove ourselves from it
	if (m_group)
	{
		// to avoid recursion, set m_group to NULL before removing
		AIGroup *group = m_group;
		m_group = nullptr;
		group->remove( this );
	}
}

//-------------------------------------------------------------------------------------------------
Real Object::getCarrierDeckHeight() const
{
	Object *producer = TheGameLogic->findObjectByID( getProducerID() );
	if( producer )
	{
		// Find a parking place behavior.
		for( BehaviorModule** i = producer->getBehaviorModules(); *i; ++i )
		{
			ParkingPlaceBehaviorInterface* pp = (*i)->getParkingPlaceBehaviorInterface();
			if( pp )
			{
				return pp->getLandingDeckHeightOffset();
			}
		}
	}
	return 0.0f;
}
