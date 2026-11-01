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

// AIStates.cpp
// Implementation of AI behavior states
// Author: Michael S. Booth, January 2002
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/LogicAudio.h"


#include "Common/ActionManager.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/CRCDebug.h"
#include "Common/GameAudio.h"
#include "Common/GlobalData.h"
#include "Common/Money.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/RandomValue.h"
#include "Common/Team.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"

#include "GameClient/ControlBar.h"
#include "GameClient/FXList.h"
#include "GameClient/InGameUI.h"

#include "GameLogic/AIDock.h"
#include "GameLogic/AIGuard.h"
#include "GameLogic/AIGuardRetaliate.h"
#include "GameLogic/AITNGuard.h"
#include "GameLogic/AIStateMachine.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Squad.h"
#include "GameLogic/TurretAI.h"
#include "GameLogic/Weapon.h"

#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/JetAIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "AIStatesInternal.h"

static Bool cannotPossiblyAttackObject( State *thisState, void* userData );

//----------------------------------------------------------------------------------------------------------
AICommandParms::AICommandParms(AICommandType cmd, CommandSourceType cmdSource) : 
	m_cmd(cmd),
	m_cmdSource(cmdSource), 
	m_obj(nullptr),
	m_otherObj(nullptr),
	m_team(nullptr),
	m_waypoint(nullptr),
	m_polygon(nullptr),
	m_intValue(0),
	m_commandButton(nullptr),
	m_path(nullptr)
{ 
		m_pos.zero();
		m_coords.clear();
}

//----------------------------------------------------------------------------------------------------------
void AICommandParmsStorage::store(const AICommandParms& parms)
{
	m_cmd = parms.m_cmd;
  m_cmdSource = parms.m_cmdSource;
  m_pos = parms.m_pos;
  m_obj = parms.m_obj ? parms.m_obj->getID() : INVALID_ID;
  m_otherObj = parms.m_otherObj ? parms.m_otherObj->getID() : INVALID_ID;
  m_teamName = parms.m_team ? parms.m_team->getName() : AsciiString::TheEmptyString;
	m_coords = parms.m_coords;
  m_waypoint = parms.m_waypoint; 
  m_polygon = parms.m_polygon;     
  m_intValue = parms.m_intValue;       /// misc usage
  m_damage = parms.m_damage;
	m_commandButton = parms.m_commandButton;
	m_path = parms.m_path;	/// @todo srj -- probably need a better way to safely save/restore this
}

//----------------------------------------------------------------------------------------------------------
void AICommandParmsStorage::reconstitute(AICommandParms& parms) const
{
	parms.m_cmd = m_cmd;
  parms.m_cmdSource = m_cmdSource;
  parms.m_pos = m_pos;
  parms.m_obj = TheGameLogic->findObjectByID(m_obj);
  parms.m_otherObj = TheGameLogic->findObjectByID(m_otherObj);
  parms.m_team = TheTeamFactory->findTeam(m_teamName);
	parms.m_coords = m_coords;
  parms.m_waypoint = m_waypoint;
  parms.m_polygon = m_polygon;
  parms.m_intValue = m_intValue;
  parms.m_damage = m_damage;
	parms.m_commandButton = m_commandButton;
	parms.m_path = m_path;	/// @todo srj -- probably need a better way to safely save/restore this
}

//----------------------------------------------------------------------------------------------------------
void AICommandParmsStorage::doXfer(Xfer *xfer) 
{
	xfer->xferUser(&m_cmd, sizeof(m_cmd));
	xfer->xferUser(&m_cmd, sizeof(m_cmdSource));
	xfer->xferCoord3D(&m_pos);
	xfer->xferObjectID(&m_obj);
	xfer->xferObjectID(&m_otherObj);
	xfer->xferAsciiString(&m_teamName);
	Int numCoords = static_cast<Int>(m_coords.size());
	xfer->xferInt(&numCoords);
	Int i;
	if (xfer->getXferMode() == XFER_LOAD)
	{
		for (i=0; i<numCoords; i++) {
			Coord3D pos;
			xfer->xferCoord3D(&pos);
			m_coords.push_back(pos);
		}
	} else {
		for (i=0; i<numCoords; i++) {
			Coord3D pos = m_coords[i];
			xfer->xferCoord3D(&pos);
		}
	}

 	UnsignedInt id = INVALID_WAYPOINT_ID;
	if (m_waypoint) {
		id = m_waypoint->getID();
	}
	xfer->xferUnsignedInt(&id);
	if (xfer->getXferMode() == XFER_LOAD && id!= INVALID_WAYPOINT_ID)
	{
		m_waypoint = TheTerrainLogic->getWaypointByID(id);
	}

	AsciiString triggerName;
	if (m_polygon) triggerName = m_polygon->getTriggerName();
	xfer->xferAsciiString(&triggerName);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		if (triggerName.isNotEmpty()) {
			m_polygon = TheTerrainLogic->getTriggerAreaByName(triggerName);
		}
	} 

	xfer->xferInt(&m_intValue);

	xfer->xferSnapshot(&m_damage);

	AsciiString cmdName;
	if (m_commandButton) {
		cmdName = m_commandButton->getName();
	}
	xfer->xferAsciiString(&cmdName);
	if (cmdName.isNotEmpty() && m_commandButton==nullptr) {
		m_commandButton = TheControlBar->findCommandButton(cmdName);
	}

	Bool hasPath = m_path!=nullptr;
	xfer->xferBool(&hasPath);
	if (hasPath && m_path==nullptr) {
		m_path = newInstance(Path);
	}
	if (hasPath) {
		xfer->xferSnapshot(m_path);
	}

}

//----------------------------------------------------------------------------------------------------------
/**
 * Compare two positions to see if they are logically equal
 * @todo Move this somewhere more useful (MSB)
 */
Bool isSamePosition( const Coord3D *ourPos, const Coord3D *prevTargetPos, const Coord3D *curTargetPos )
{
	Coord3D diff;

	// for pathfinding purposes, only care about 2d pos. (srj)
	diff.x = curTargetPos->x - prevTargetPos->x;
	diff.y = curTargetPos->y - prevTargetPos->y;

	Coord3D toTarget;
 	// for pathfinding purposes, only care about 2d pos. (srj)
	toTarget.x = curTargetPos->x - ourPos->x;
	toTarget.y = curTargetPos->y - ourPos->y;

	// Tolerance is (dist/10)squared.
	const Real TOLERANCE_FACTOR = 1.0f / (10.0f * 10.0f);
	Real toleranceSqr = (toTarget.x*toTarget.x+toTarget.y*toTarget.y) * TOLERANCE_FACTOR;

	if (diff.x * diff.x + diff.y * diff.y > toleranceSqr)
		return false;

	return true;
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AttackStateMachine::crc( Xfer *xfer )
{
	StateMachine::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AttackStateMachine::xfer( Xfer *xfer )
{
	XferVersion cv = 1;	
	XferVersion v = cv; 
	xfer->xferVersion( &v, cv );

	StateMachine::xfer(xfer);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AttackStateMachine::loadPostProcess( void )
{
	StateMachine::loadPostProcess();
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
static Bool inWeaponRangeObject(State *thisState, void* userData);

//----------------------------------------------------------------------------------------------------------
/**
 * Create an AI state machine. Define all of the states the machine 
 * can possibly be in, and set the initial (default) state.
 */
AttackStateMachine::AttackStateMachine( Object *obj, AIAttackState* att, AsciiString name, Bool follow, Bool attackingObject, Bool forceAttacking ) 
	: StateMachine( obj, name )
{
	// we want to use the CONTINUE mode (not NEW) since we already have acquired the target.
	static const StateConditionInfo objectConditionsNormal[] =
	{
		StateConditionInfo(outOfWeaponRangeObject, AttackStateMachine::CHASE_TARGET, nullptr),
		StateConditionInfo(wantToSquishTarget, AttackStateMachine::CHASE_TARGET, nullptr),
		StateConditionInfo(cannotPossiblyAttackObject, EXIT_MACHINE_WITH_FAILURE, reinterpret_cast<void*>(ATTACK_CONTINUED_TARGET)),
		StateConditionInfo(NULL, NULL, NULL)	// keep last
	};

	// we want to use the CONTINUE mode (not NEW) since we already have acquired the target.
	static const StateConditionInfo objectConditionsForced[] =
	{
		StateConditionInfo(outOfWeaponRangeObject, AttackStateMachine::CHASE_TARGET, nullptr),
		StateConditionInfo(cannotPossiblyAttackObject, EXIT_MACHINE_WITH_FAILURE, reinterpret_cast<void*>(ATTACK_CONTINUED_TARGET_FORCED)),
		StateConditionInfo(wantToSquishTarget, AttackStateMachine::CHASE_TARGET, nullptr),
		StateConditionInfo(NULL, NULL, NULL)	// keep last
	};

	const StateConditionInfo* objectConditions = forceAttacking ? objectConditionsForced : objectConditionsNormal;

	static const StateConditionInfo positionConditions[] = 
	{
		StateConditionInfo(outOfWeaponRangePosition, AttackStateMachine::CHASE_TARGET, nullptr),
		StateConditionInfo(NULL, NULL, NULL)	// keep last
	};

#ifdef STATE_MACHINE_DEBUG
		AsciiString fullName = name;
		if (follow) fullName.concat(" follow");
		if (attackingObject) fullName.concat(" object");
		setName(fullName);
		//setDebugOutput(true);
#endif 

	// order matters: first state is the default state.
	// The default is Aim rather than Approach so things that cannot move will be able to shoot
	// things that are in range.  Things that cannot move will automatically FAILURE on approach state.
	/*
		This state will succeed when we are aiming a useful weapon at the victim, and fail 
		if the victim is dead. (Exception: if the weapon is on a turret, we don't leave this
		state unless we get out of range.)
	*/
	defineState(	AttackStateMachine::AIM_AT_TARGET, 
								newInstance(AIAttackAimAtTargetState)( this, attackingObject, forceAttacking ), 
								AttackStateMachine::FIRE_WEAPON, 
								EXIT_MACHINE_WITH_FAILURE,
								attackingObject ? objectConditions : positionConditions );

	/*
		Note that the fire state succeeds iff it is able to fire... it will "fail"
		if unable to fire. However, it may be unable to fire because the target object
		is already dead.
	*/
	defineState( AttackStateMachine::FIRE_WEAPON, 
								newInstance(AIAttackFireWeaponState)( this, att ),
								AttackStateMachine::AIM_AT_TARGET,
								AttackStateMachine::AIM_AT_TARGET,
								attackingObject ? objectConditions : positionConditions );


	if (obj->isKindOf(KINDOF_IMMOBILE) == FALSE)
	{
		if (obj->isKindOf(KINDOF_PORTABLE_STRUCTURE) && obj->isKindOf(KINDOF_CAN_ATTACK))
		{
			static const StateConditionInfo portableStructureChaseConditions[] =
			{
				StateConditionInfo(inWeaponRangeObject, AttackStateMachine::AIM_AT_TARGET, nullptr),
				StateConditionInfo(NULL, NULL, NULL)	// keep last
			};

			/* we're a rider on a mobile object, so we can't control our motion. 
				just make bogus states that always fall back into "aim".
			*/
			defineState(	AttackStateMachine::CHASE_TARGET, 
										newInstance(ContinueState)(this), 
										EXIT_MACHINE_WITH_FAILURE, 
										EXIT_MACHINE_WITH_FAILURE,
										portableStructureChaseConditions );
		}
		else if (attackingObject) 
		{
			/*
				This state will pursue a target that is moving away from it.  If it is not moving away, 
				it will drop into the AIAttackApproachTarget state.
			*/
			defineState(	AttackStateMachine::CHASE_TARGET, 
										newInstance(AIAttackPursueTargetState)( this, follow, attackingObject, forceAttacking ), 
										AttackStateMachine::APPROACH_TARGET, 
										AttackStateMachine::APPROACH_TARGET );

			/*
				This state will succeed when we have a useful weapon within range of victim, and fail 
				if the victim is dead
			*/
			defineState(	AttackStateMachine::APPROACH_TARGET, 
										newInstance(AIAttackApproachTargetState)( this, follow, attackingObject, forceAttacking ), 
										AttackStateMachine::AIM_AT_TARGET, 
										EXIT_MACHINE_WITH_FAILURE );
		}	
		else 
		{
			/*
				This state will succeed when we have a useful weapon within range of victim, and fail 
				if the victim is dead
			*/
			defineState(	AttackStateMachine::CHASE_TARGET, 
										newInstance(AIAttackApproachTargetState)( this, follow, attackingObject, forceAttacking ), 
										AttackStateMachine::AIM_AT_TARGET, 
										EXIT_MACHINE_WITH_FAILURE );

			/*
				This state will succeed when we have a useful weapon within range of victim, and fail 
				if the victim is dead
			*/
			defineState(	AttackStateMachine::APPROACH_TARGET, 
										newInstance(AIAttackApproachTargetState)( this, follow, attackingObject, forceAttacking ), 
										AttackStateMachine::AIM_AT_TARGET, 
										EXIT_MACHINE_WITH_FAILURE );
		}

	}
	else
	{
		/*
			This state always instantly fails, so when immobile things transition here, we bail.
		*/
		defineState(	AttackStateMachine::CHASE_TARGET, 
									newInstance(FailureState)(this), 
									EXIT_MACHINE_WITH_FAILURE, 
									EXIT_MACHINE_WITH_FAILURE );
	}

};

//----------------------------------------------------------------------------------------------------------
AttackStateMachine::~AttackStateMachine()
{
}

//-----------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------
static Object* findEnemyInContainer(Object* killer, Object* bldg)
{
	const ContainedItemsList* items = bldg->getContain() ? bldg->getContain()->getContainedItemsList() : nullptr;
	if (items)
	{
		for (ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it )
		{
			if ((*it)->isEffectivelyDead())
			{
				DEBUG_CRASH(("why is there a dead thing in this container?"));
				continue;
			}

			// order matters: we want to know if I consider it to be an enemy, not vice versa
			if (killer->getRelationship(*it) == ENEMIES)
			{
				return *it;
			}
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------------------------------------
static Int killEnemiesInContainer(Object* killer, Object* bldg, Int maxToKill)
{
	Int numKilled = 0;
	while (numKilled < maxToKill)
	{
		Object* enemy = findEnemyInContainer(killer, bldg);
		if (enemy)
		{
			Object *containedByObject = enemy->getContainedBy();
			if( containedByObject )
			{
				ContainModuleInterface *contain = containedByObject->getContain();
				if( contain )
				{
					contain->removeFromContain(enemy);
				}
			}
			if (killer)
				killer->scoreTheKill( enemy );
			enemy->kill();
			++numKilled;
		}
		else
		{
			break;
		}
	}
	return numKilled;
}

//-----------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------
AIRappelState::AIRappelState( StateMachine *machine ) : State( machine, "AIRappelState" ) 
{ 
}

//-----------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIRappelState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIRappelState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );
 
	xfer->xferReal(&m_rappelRate);
	xfer->xferReal(&m_destZ);
	xfer->xferBool(&m_targetIsBldg);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIRappelState::loadPostProcess( void )
{
}  // end loadPostProcess
//-----------------------------------------------------------------------------------------------------------
StateReturnType AIRappelState::onEnter()
{
	Object* obj = getMachineOwner();
	if (!obj->isKindOf(KINDOF_CAN_RAPPEL))
		return STATE_FAILURE;

	//AIUpdateInterface* ai = obj->getAI();

	obj->setModelConditionState(MODELCONDITION_RAPPELLING);
// don't do this, or we'll be unable to be forced out of this state.
// instead, just manipulate physics directly.
//obj->setHeld();
	obj->getPhysics()->resetDynamicPhysics();

	m_targetIsBldg = true;
	Object* bldg = getMachineGoalObject();
	if (bldg == nullptr || bldg->isEffectivelyDead() || !bldg->isKindOf(KINDOF_STRUCTURE))
		m_targetIsBldg = false;

	const Coord3D* pos = obj->getPosition();

	const Bool onlyHealthyBridges = true;	// ignore dead bridges.
	PathfindLayerEnum layerAtDest = TheTerrainLogic->getHighestLayerForDestination(pos, onlyHealthyBridges);
	m_destZ = TheTerrainLogic->getLayerHeight(pos->x, pos->y, layerAtDest);
	if (m_targetIsBldg)
		m_destZ += bldg->getGeometryInfo().getMaxHeightAbovePosition();
	else
		obj->setLayer(layerAtDest);

	AIUpdateInterface *ai = obj->getAI();
	Real MAX_RAPPEL_RATE = fabs(TheGlobalData->m_gravity) * static_cast<Real>(LOGICFRAMES_PER_SECOND) * 2.5f;
	m_rappelRate = -min(ai->getDesiredSpeed(), MAX_RAPPEL_RATE);

	return STATE_CONTINUE;
}

//-----------------------------------------------------------------------------------------------------------

StateReturnType AIRappelState::update()
{

	StateReturnType result = STATE_CONTINUE;
	
	Object* obj = getMachineOwner();
	const Coord3D* pos = obj->getPosition();

	Object* bldg = getMachineGoalObject();
	if (m_targetIsBldg && (bldg == nullptr || bldg->isEffectivelyDead()))
	{
		// if bldg is destroyed, just head for the ground
		// BGC - bldg could be destroyed as they are heading down the rope.
		m_targetIsBldg = false;
		m_destZ = TheTerrainLogic->getGroundHeight(pos->x, pos->y);
	}
	
	// nuke 2d speed...
	obj->getPhysics()->scrubVelocity2D(0);
	// and clamp z speed to rappel rate (gravity will have accelerated us)
	obj->getPhysics()->scrubVelocityZ(m_rappelRate);

	if (!m_targetIsBldg)
	{
		// if heading for ground, do this every frame... since jitter at the very start
		// can move us slightly, and on uneven ground it might matter.
		m_destZ = TheTerrainLogic->getLayerHeight(pos->x, pos->y, obj->getLayer());
	}
	if (pos->z <= m_destZ)
	{
		Coord3D tmp = *pos;
		tmp.z = m_destZ;
		obj->setPosition(&tmp);
		
		if (m_targetIsBldg)
		{
			DEBUG_ASSERTCRASH(TheActionManager->canEnterObject(obj, bldg, obj->getAI()->getLastCommandSource(), CanEnterType::COMBATDROP_INTO), ("Hmm, this seems unlikely"));
			// if there are enemies... kill up to two. if we kill two, then we die ourselves,
			// otherwise we enter the bldg.
			const Int MAX_TO_KILL = 2;
			Int numKilled = killEnemiesInContainer(obj, bldg, MAX_TO_KILL);
			
			if (numKilled > 0)
			{
				const FXList* fx = obj->getTemplate()->getPerUnitFX("CombatDropKillFX");
				FXList::doFXObj(fx, bldg, nullptr);
				DEBUG_LOG(("Killing %d enemies in combat drop!\n",numKilled));
			}

			if (numKilled == MAX_TO_KILL)
			{
				obj->kill();
				DEBUG_LOG(("Killing SELF in combat drop!\n"));
			}
			else
			{
				if (bldg->getContain() && bldg->getContain()->isValidContainerFor(obj, TRUE ))
				{
					bldg->getContain()->addToContain(obj);
				}
				else
				{
					// this can legitimately happen if you drop into a full (or nearly full) building.
					// let's just place the guy on the ground nearby, since it sucks to fall from the top of a building.
					Real exitAngle = bldg->getOrientation();
					// Garrison doesn't have reserveDoor or exitDelay, so if we do nothing, everyone will appear on top 
					// of each other and get stuck inside each others' extent (except for the first guy).  So we'll
					// scatter the start point around a little to make it better.
					Real offset = min(obj->getGeometryInfo().getBoundingCircleRadius(), 
														bldg->getGeometryInfo().getBoundingCircleRadius());
					Real angle = GameLogicRandomValueReal( PI, 2*PI );//Downish.
					Coord3D startPosition = *bldg->getPosition();
					startPosition.x += offset * Cos( angle );
					startPosition.y += offset * Sin( angle );
					startPosition.z = TheTerrainLogic->getGroundHeight( startPosition.x, startPosition.y );

					obj->setPosition( &startPosition );
					obj->setOrientation( exitAngle );
					
					FindPositionOptions options;
					options.startAngle = static_cast<Real>(1.5 * PI);//Down.
					options.maxRadius = 200;
					Coord3D endPosition;
					Bool foundPosition = ThePartitionManager->findPositionAround( &startPosition, &options, &endPosition );

					if( foundPosition )
					{
						std::vector<Coord3D> exitPath;
						exitPath.push_back(endPosition);
						AIUpdateInterface* ai = obj->getAI();
						if( ai )
						{
							ai->aiFollowPath( &exitPath, bldg, CMD_FROM_AI );
						}
					}

				}
			}
		}

		result = STATE_SUCCESS;
	}

	return result;
}

//-----------------------------------------------------------------------------------------------------------
void AIRappelState::onExit( StateExitType status )
{
	Object* obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();
	obj->clearModelConditionState(MODELCONDITION_RAPPELLING);
// don't do this, or we'll be unable to be forced out of this state.
// instead, just manipulate physics directly.
//obj->clearHeld();
	ai->setDesiredSpeed( FAST_AS_POSSIBLE );
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
/*

	NOTE NOTE NOTE NOTE NOTE

	Do NOT subclass this unless you want ALL of the states this machine possesses.
	If you only want SOME of the states, please make a new StateMachine, descended
	from StateMachine, NOT AIStateMachine. Thank you. (srj)

	NOTE NOTE NOTE NOTE NOTE

*/
AIStateMachine::AIStateMachine( Object *obj, AsciiString name ) : StateMachine( obj, name )
{
	DEBUG_ASSERTCRASH(getOwner(), ("An AI State Machine '%s' was constructed without an owner, please tell JKMCD", name.str()));
	DEBUG_ASSERTCRASH(getOwner()->getAI(), ("An AI State Machine '%s' was constructed without an AIUpdateInterface, please tell JKMCD", name.str()));

	m_goalPath.clear();
	m_goalWaypoint = nullptr;
	m_goalSquad = nullptr;

	m_temporaryState = nullptr;
	m_temporaryStateFramEnd = 0;

	// order matters: first state is the default state.
	defineState( AI_IDLE,																	newInstance(AIIdleState)( this, AIIdleState::AIIdleTargetingType::LOOK_FOR_TARGETS), AI_IDLE, AI_IDLE );
	defineState( AI_MOVE_TO,															newInstance(AIMoveToState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_MOVE_OUT_OF_THE_WAY,									newInstance(AIMoveOutOfTheWayState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_MOVE_AND_TIGHTEN,											newInstance(AIMoveAndTightenState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_MOVE_AWAY_FROM_REPULSORS,							newInstance(AIMoveAwayFromRepulsorsState)( this ), AI_WANDER_IN_PLACE, AI_WANDER_IN_PLACE );
	defineState( AI_WANDER_IN_PLACE,											newInstance(AIWanderInPlaceState)( this ), AI_MOVE_AWAY_FROM_REPULSORS, AI_MOVE_AWAY_FROM_REPULSORS );
	defineState( AI_ATTACK_MOVE_TO,												newInstance(AIAttackMoveToState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_ATTACKFOLLOW_WAYPOINT_PATH_AS_TEAM,					newInstance(AIAttackFollowWaypointPathState)( this, true ), AI_IDLE, AI_IDLE );
	defineState( AI_ATTACKFOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS,	newInstance(AIAttackFollowWaypointPathState)( this, false ), AI_IDLE, AI_IDLE );
	
	defineState( AI_FOLLOW_WAYPOINT_PATH_AS_TEAM,					newInstance(AIFollowWaypointPathState)( this, true ), AI_IDLE, AI_IDLE );
	defineState( AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS,	newInstance(AIFollowWaypointPathState)( this, false ), AI_IDLE, AI_IDLE );
	defineState( AI_FOLLOW_WAYPOINT_PATH_AS_TEAM_EXACT,		newInstance(AIFollowWaypointPathExactState)( this, true ), AI_IDLE, AI_IDLE );
	defineState( AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS_EXACT,newInstance(AIFollowWaypointPathExactState)( this, false ), AI_IDLE, AI_IDLE );
	defineState( AI_FOLLOW_PATH,													newInstance(AIFollowPathState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_FOLLOW_EXITPRODUCTION_PATH,						newInstance(AIFollowPathState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_MOVE_AND_EVACUATE,					newInstance(AIMoveAndEvacuateState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_MOVE_AND_EVACUATE_AND_EXIT,	newInstance(AIMoveAndEvacuateState)( this ), AI_MOVE_AND_DELETE, AI_MOVE_AND_DELETE );
	defineState( AI_MOVE_AND_DELETE,						newInstance(AIMoveAndDeleteState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_WAIT,												newInstance(AIWaitState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_ATTACK_POSITION,						newInstance(AIAttackState)( this, false, false, false,  nullptr ), AI_IDLE, AI_IDLE );
	defineState( AI_ATTACK_OBJECT,							newInstance(AIAttackState)( this, false, true, false, nullptr ), AI_IDLE, AI_IDLE );
	defineState( AI_FORCE_ATTACK_OBJECT,				newInstance(AIAttackState)( this, false, true, true, nullptr ), AI_IDLE, AI_IDLE );

	defineState( AI_ATTACK_AND_FOLLOW_OBJECT,		newInstance(AIAttackState)( this, true, true, false, nullptr ), AI_IDLE, AI_IDLE );
	defineState( AI_ATTACK_SQUAD,								newInstance(AIAttackSquadState)( this, nullptr ), AI_IDLE, AI_IDLE );
	defineState( AI_WANDER,											newInstance(AIWanderState)( this ), AI_IDLE, AI_MOVE_AWAY_FROM_REPULSORS );
	defineState( AI_PANIC,											newInstance(AIPanicState)( this ), AI_IDLE, AI_MOVE_AWAY_FROM_REPULSORS );
	defineState( AI_DEAD,												newInstance(AIDeadState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_DOCK,												newInstance(AIDockState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_ENTER,											newInstance(AIEnterState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_EXIT,												newInstance(AIExitState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_EXIT_INSTANTLY,							newInstance(AIExitInstantlyState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_GUARD,											newInstance(AIGuardState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_GUARD_TUNNEL_NETWORK,				newInstance(AITunnelNetworkGuardState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_GUARD_RETALIATE,						newInstance(AIGuardRetaliateState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_HUNT,												newInstance(AIHuntState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_ATTACK_AREA,								newInstance(AIAttackAreaState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_FACE_OBJECT,								newInstance(AIFaceState)( this, true ), AI_IDLE, AI_IDLE );
	defineState( AI_FACE_POSITION,							newInstance(AIFaceState)( this, false ), AI_IDLE, AI_IDLE );
	defineState( AI_PICK_UP_CRATE,							newInstance(AIPickUpCrateState)( this ), AI_IDLE, AI_IDLE );

	defineState( AI_RAPPEL_INTO,								newInstance(AIRappelState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_BUSY,												newInstance(AIBusyState)( this ), AI_IDLE, AI_IDLE );
}

//----------------------------------------------------------------------------------------------------------
AIStateMachine::~AIStateMachine()
{
	if (m_goalSquad) 
	{
		deleteInstanceOf(m_goalSquad);
	}
}


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIStateMachine::crc( Xfer *xfer )
{
	StateMachine::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIStateMachine::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
	StateMachine::xfer(xfer);

	Int i;
	Int count = static_cast<Int>(m_goalPath.size());
	xfer->xferInt(&count);
	for (i=0; i<count; i++) {
		Coord3D pos;
		if (xfer->getXferMode() != XFER_LOAD)
		{
			pos = m_goalPath[i];
		} 
		xfer->xferCoord3D(&pos);
		if (xfer->getXferMode() == XFER_LOAD)
		{
			m_goalPath.push_back(pos);
		}
	}

	AsciiString waypointName;
	if (m_goalWaypoint) waypointName = m_goalWaypoint->getName();
	xfer->xferAsciiString(&waypointName);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		if (waypointName.isNotEmpty()) {
			m_goalWaypoint = TheTerrainLogic->getWaypointByName(waypointName);
		}
	} 
	Bool hasSquad = (m_goalSquad!=nullptr);
	xfer->xferBool(&hasSquad);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		if (hasSquad && m_goalSquad==nullptr) {
			m_goalSquad = newInstance( Squad );
		}
	} 
	if (hasSquad) {
		xfer->xferSnapshot(m_goalSquad);
	}

	StateID id = INVALID_STATE_ID;
	if (m_temporaryState) {
		id = m_temporaryState->getID();
		DEBUG_ASSERTCRASH(id!=INVALID_STATE_ID, ("State has invalid state id, no really. jba."));
	}
	xfer->xferUnsignedInt(&id);
	if (xfer->getXferMode() == XFER_LOAD && id != INVALID_STATE_ID) {
		m_temporaryState = internalGetState( id );
	}
	if (m_temporaryState!=nullptr) {
		xfer->xferSnapshot(m_temporaryState);
	}

	xfer->xferUnsignedInt(&m_temporaryStateFramEnd);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIStateMachine::loadPostProcess( void )
{
	StateMachine::loadPostProcess();
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
/**
 * Define a simple path
 */
void AIStateMachine::setGoalPath( const std::vector<Coord3D>* path )
{
	m_goalPath = *path;
}

#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
/**
 * Get the current state name.
 */
AsciiString AIStateMachine::getCurrentStateName(void) const
{
	AsciiString name = StateMachine::getCurrentStateName();

	if (m_temporaryState) {
		name.concat(" /T/");
		name.concat(m_temporaryState->getName()); 
	}
	return name;					
}
#endif

//-----------------------------------------------------------------------------
/**
 * Run one step of the machine
 */
StateReturnType AIStateMachine::updateStateMachine()
{
	//-extraLogging
	#if defined(_DEBUG)
		Bool idle = getOwner()->getAI()->isIdle();
		if( !idle && TheGlobalData->m_extraLogging )
			DEBUG_LOG( ("%d - %s::update() start - %s", TheGameLogic->getFrame(), getCurrentStateName().str(), getOwner()->getTemplate()->getName().str() ) );
	#endif
	//end -extraLogging 

	if (m_temporaryState)
	{
		// execute this state
		StateReturnType status = m_temporaryState->update();
		if (m_temporaryStateFramEnd < TheGameLogic->getFrame()) {
			// ran out of time.
			if (status == STATE_CONTINUE) {
				status = STATE_SUCCESS;
			}
		}
		if (status==STATE_CONTINUE)	
		{
			//-extraLogging
			#if defined(_DEBUG)
				if( !idle && TheGlobalData->m_extraLogging )
					DEBUG_LOG( (" - RETURN EARLY STATE_CONTINUE\n") );
			#endif
			//end -extraLogging 

			return status;
		}
		m_temporaryState->onExit(StateExitType::EXIT_NORMAL);
		m_temporaryState = nullptr;
	}
	StateReturnType retType = StateMachine::updateStateMachine();

	//-extraLogging 
	#if defined(_DEBUG)
		AsciiString result;
		if( TheGlobalData->m_extraLogging )
		{
			switch( retType )
			{
				case STATE_CONTINUE:
					result.format( "CONTINUE" );
					break;
				case STATE_SUCCESS:
					result.format( "SUCCESS" );
					break;
				case STATE_FAILURE:
					result.format( "FAILURE" );
					break;
				default:
					result.format( "UNKNOWN %d", retType );
					break;
			}	
			if( !idle )
				DEBUG_LOG( (" - RETURNING %s\n", result.str() ) );
		}
	#endif
	//end -extraLogging 

	return retType;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Change the temporary state of the machine, and number of frames limit.th
 */
StateReturnType AIStateMachine::setTemporaryState( StateID newStateID, Int frameLimitCoount )

{
	// extract the state associated with the given ID
	State *newState = internalGetState( newStateID );
#ifdef STATE_MACHINE_DEBUG
	if (getWantsDebugOutput()) 
	{
		StateID curState = INVALID_STATE_ID;
		if (m_temporaryState) {
			curState = m_temporaryState->getID();
		}
		DEBUG_LOG(("%d '%s' -(TEMP)- '%s' %p exit ", TheGameLogic->getFrame(), getOwner()->getTemplate()->getName().str(), getName().str(), this));
		if (m_temporaryState) {
			DEBUG_LOG((" '%s' ", m_temporaryState->getName().str()));
		} else {
			DEBUG_LOG((" INVALID_STATE_ID "));
		}
		if (newState) {
			DEBUG_LOG(("enter '%s' \n", newState->getName().str()));
		} else {
			DEBUG_LOG(("to INVALID_STATE\n"));
		}
	}
#endif
	if (m_temporaryState) {
		m_temporaryState->onExit(StateExitType::EXIT_RESET);
		m_temporaryState = nullptr;
	}
	if (newState) {
		m_temporaryState = newState;
		StateReturnType ret = m_temporaryState->onEnter();
		if (ret != STATE_CONTINUE) {
			m_temporaryState->onExit(StateExitType::EXIT_NORMAL);
			m_temporaryState = nullptr;
			return ret;
		}
		enum {FRAME_COUNT_MAX = 60*LOGICFRAMES_PER_SECOND};
		// If you need to up this check, ok, but 1 minute seems overly long for a temporary state override.  jba.
		DEBUG_ASSERTCRASH(frameLimitCoount<=FRAME_COUNT_MAX, ("Unusually long time to set temporary state."));
		if (frameLimitCoount>FRAME_COUNT_MAX) {
			frameLimitCoount = FRAME_COUNT_MAX;
		}
		m_temporaryStateFramEnd = TheGameLogic->getFrame()+frameLimitCoount; 
		return ret;
	}
	return STATE_FAILURE;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Add a point to a simple path
 */
void AIStateMachine::addToGoalPath( const Coord3D *pathPoint)
{	
	if (m_goalPath.size()==0) {
		m_goalPath.push_back(*pathPoint);
	}	else {
		Coord3D *finalPoint = &m_goalPath[ m_goalPath.size() - 1 ];
		if( !finalPoint->equals( *pathPoint ) )
		{
			m_goalPath.push_back(*pathPoint);
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * Return path position at index "i"
 */
const Coord3D *AIStateMachine::getGoalPathPosition( Int i ) const
{
	if (i < 0 || i >= m_goalPath.size())
		return nullptr;

	return &m_goalPath[i];
}

//----------------------------------------------------------------------------------------------------------
/**
 * Set the current goal waypoint. If we reach this waypoint and there 
 * are connections to further points, continue on.
 */
void AIStateMachine::setGoalWaypoint( const Waypoint *way )
{
	m_goalWaypoint = way;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Return the current goal waypoint
 */
const Waypoint *AIStateMachine::getGoalWaypoint()
{
	return m_goalWaypoint;
}

//----------------------------------------------------------------------------------------------------------
void AIStateMachine::clear()
{
	StateMachine::clear();
	m_goalPath.clear();
	m_goalWaypoint = nullptr;
	// Free the goal squad; the next setGoal* creates a new one.
	if (m_goalSquad)
		deleteInstanceOf(m_goalSquad);
	m_goalSquad = nullptr;

	AIUpdateInterface* ai = getOwner()->getAI();
	if (ai)
		ai->friend_notifyStateMachineChanged();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIStateMachine::resetToDefaultState()
{
	StateReturnType tmp = StateMachine::resetToDefaultState();

	AIUpdateInterface* ai = getOwner()->getAI();
	if (ai)
		ai->friend_notifyStateMachineChanged();

	return tmp;
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIStateMachine::setState(StateID newStateID)
{
	StateID oldID = getCurrentStateID();
	StateReturnType tmp = StateMachine::setState(newStateID);

	AIUpdateInterface* ai = getOwner()->getAI();
	if (ai && oldID != newStateID)
		ai->friend_notifyStateMachineChanged();

	return tmp;
}

//----------------------------------------------------------------------------------------------------------
void AIStateMachine::setGoalTeam( const Team *team )
{
	if (m_goalSquad == nullptr) {
		m_goalSquad = newInstance( Squad );
	}

	m_goalSquad->squadFromTeam(team, true);
}

//----------------------------------------------------------------------------------------------------------
void AIStateMachine::setGoalSquad( const Squad *squad )
{
	if (m_goalSquad == nullptr) {
		m_goalSquad = newInstance( Squad );
	}

	(*m_goalSquad) = (*squad);
}

//----------------------------------------------------------------------------------------------------------
void AIStateMachine::setGoalAIGroup( const AIGroup *group )
{
	if (m_goalSquad == nullptr) {
		m_goalSquad = newInstance( Squad );
	}

	m_goalSquad->squadFromAIGroup(group, true);
}

//----------------------------------------------------------------------------------------------------------
Squad *AIStateMachine::getGoalSquad( void )
{
	return m_goalSquad;
}

// State transition conditions ----------------------------------------------------------------------------
/**
 * Return true if the machine's owner's current weapon's range 
 * cannot reach the goalObject.
 */
Bool outOfWeaponRangeObject( State *thisState, void* userData )
{
	Object *obj = thisState->getMachineOwner();
	Object *victim = thisState->getMachineGoalObject();
	Weapon *weapon = obj->getCurrentWeapon();

	CRCDEBUG_LOG(("outOfWeaponRangeObject()\n"));
	if (victim && weapon)
	{
		Bool viewBlocked = false;
		AIUpdateInterface *ai = obj->getAI();	 
		Bool onGround = true;
		if (ai) {
			onGround = ai->isDoingGroundMovement();
		}
		if( obj->isKindOf(KINDOF_IMMOBILE) ) {
			onGround = true;
		}
		// brutal special case for stinger soldiers, who
		// generally don't have locomotors, but are still on the ground.
		if (obj->isKindOf(KINDOF_SPAWNS_ARE_THE_WEAPONS))
		{
			onGround = true;
		}
		Object *containedBy = obj->getContainedBy();
		if( containedBy && (containedBy->isKindOf( KINDOF_STRUCTURE ) || !containedBy->isAirborneTarget()) )
		{
			//Contained objects on the ground -- garrisoned buildings for example!
			onGround = true;
		}
		// srj sez: at tiny ranges, isAttackViewBlockedByObstacle() can return false positives,
		// so just skip it for contact weapons
		if (victim && !weapon->isContactWeapon() && onGround && !victim->isSignificantlyAboveTerrain()) 
		{
			viewBlocked = TheAI->pathfinder()->isAttackViewBlockedByObstacle(obj, *obj->getPosition(), victim, *victim->getPosition());
		}	 
		// A weapon with leech range temporarily has unlimited range and is locked onto its target.
		if (!weapon->hasLeechRange() && viewBlocked) 
		{
			//CRCDEBUG_LOG(("outOfWeaponRangeObject() - object %d (%s) view is blocked for attacking %d (%s)\n",
			//	obj->getID(), obj->getTemplate()->getName().str(),
			//	victim->getID(), victim->getTemplate()->getName().str()));
			return true;
		}
		if (!weapon->hasLeechRange() && !weapon->isWithinAttackRange(obj, victim))
		{
			//CRCDEBUG_LOG(("outOfWeaponRangeObject() - object %d (%s) is out of range for attacking %d (%s)\n",
			//	obj->getID(), obj->getTemplate()->getName().str(),
			//	victim->getID(), victim->getTemplate()->getName().str()));
			return true;
		}
	}

	return false;
}

static Bool inWeaponRangeObject(State *thisState, void* userData)
{
	return !outOfWeaponRangeObject(thisState, userData);
}

Bool wantToSquishTarget( State *thisState, void* userData )
{
	Object *obj = thisState->getMachineOwner();
	Object *victim = thisState->getMachineGoalObject();

	if (obj && victim)
	{
		if (victim->getContainedBy()) {
			return false; // can't crush guys in buildings or vehicles.
		}
		if( obj->getAI() && (obj->getAI()->getWhichTurretForCurWeapon() != TURRET_INVALID) )
		{
			// I can only decide to crush-attack if I am attacking with a turreted weapon.
			if (TheAI->getAiData()->m_aiCrushesInfantry) {
				if (obj && obj->getControllingPlayer() && 
					obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER) {
					if (obj->canCrushOrSquish(victim)) {
						if (!obj->isKindOf(KINDOF_DONT_AUTO_CRUSH_INFANTRY)) {
							return true;
						}
					}
				}
			}
		}
	}

	return false;
}

Bool outOfWeaponRangePosition( State *thisState, void* userData )
{
	Object *obj = thisState->getMachineOwner();
	const Coord3D *pos = thisState->getMachineGoalPosition();
	Weapon *weapon = obj->getCurrentWeapon();

	if (weapon && pos)
	{
		AIUpdateInterface *ai = obj->getAI();
		Bool onGround = true;
		if (ai) {
			onGround = ai->isDoingGroundMovement();
		}
		if( obj->isKindOf(KINDOF_IMMOBILE) ) {
			onGround = true;
		}
		// brutal special case for stinger soldiers, who
		// generally don't have locomotors, but are still on the ground.
		if (obj->isKindOf(KINDOF_SPAWNS_ARE_THE_WEAPONS))
		{
			onGround = true;
		}
		Object *containedBy = obj->getContainedBy();
		if( containedBy && (containedBy->isKindOf( KINDOF_STRUCTURE ) || !containedBy->isAirborneTarget()) )
		{
			//Contained objects on the ground -- garrisoned buildings for example!
			onGround = true;
		}

		Bool viewBlocked = false;
		if (onGround) 
		{
			viewBlocked = TheAI->pathfinder()->isAttackViewBlockedByObstacle(obj, *obj->getPosition(), nullptr, *pos);
		}	 
		if (viewBlocked)
		{
			return true;
		}
		if (!weapon->isWithinAttackRange(obj, pos))
		{
			return true;
		}
	}

	return false;
}

/**
 * Return true if the machine's owner's cannot attack in any way
 */
static Bool cannotPossiblyAttackObject( State *thisState, void* userData )
{
	AbleToAttackType attackType = static_cast<AbleToAttackType>(reinterpret_cast<uintptr_t>(userData));
	Object *obj = thisState->getMachineOwner();
	Object *victim = thisState->getMachineGoalObject();

	if (obj && victim)
	{
		if( !obj->isAbleToAttack() )
		{
			return TRUE; 
		}
		CanAttackResult result = obj->getAbleToAttackSpecificObject( attackType, victim, obj->getAI()->getLastCommandSource() );
		if( result != CanAttackResult::ATTACKRESULT_POSSIBLE && result != CanAttackResult::ATTACKRESULT_POSSIBLE_AFTER_MOVING )
		{
			return TRUE;
		}
	}

	return FALSE;
}


//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------

const UnsignedInt IDLE_COUNTDOWN_DELAY = (LOGICFRAMES_PER_SECOND * 2);

//----------------------------------------------------------------------------------------------
AIIdleState::AIIdleState( StateMachine *machine, AIIdleState::AIIdleTargetingType shouldLookForTargets ) : 
	State( machine,"AIIdleState"), 
	m_shouldLookForTargets(shouldLookForTargets == AIIdleState::AIIdleTargetingType::LOOK_FOR_TARGETS)
{
		m_inited = FALSE;
		m_initialSleepOffset = static_cast<UnsignedShort>(-1);
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIIdleState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIIdleState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );
 
	xfer->xferUnsignedShort(&m_initialSleepOffset);
	xfer->xferBool(&m_shouldLookForTargets);
	xfer->xferBool(&m_inited);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIIdleState::loadPostProcess( void )
{
}  // end loadPostProcess
//----------------------------------------------------------------------------------------------
/**
 * Stake out our space.
 */
DECLARE_PERF_TIMER(AIIdleState)
StateReturnType AIIdleState::onEnter()
{
	USE_PERF_TIMER(AIIdleState)
	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	// We could possibly not have ai here if we were constructed this frame. Strange but true. :-<
	if (ai) 
		ai->resetNextMoodCheckTime();

	m_inited = true;

	// reset the idle countdown so that we don't do expensive checks too often,
	// randomized so we avoid spikes
	m_initialSleepOffset = static_cast<UnsignedShort>(GameLogicRandomValue(0, IDLE_COUNTDOWN_DELAY));

	// never sleep at the start, since we have to do checkGoalPos first time thru
	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------
void AIIdleState::doInitIdleState()
{
	// Only do it once, but do it in the update cause onEnter for idle is called during object creation.  jba.

	if (!m_inited)
		return;

	m_inited = false;

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();
	const Locomotor* loco = ai->getCurLocomotor();
	Bool ultraAccurate = (loco != nullptr && loco->isUltraAccurate());
#define NO_STOP_AND_SLIDE
	if (ai->isIdle() && ai->isDoingGroundMovement()) 
	{
		/*

			You may be asking yourself, "If I'm in an idle state, how can I be doing ground movement?"

			answer from jba:

			If a unit is moving, and you hit stop, it forces it into the idle state.  
			Depending where it is, it may be between pathfind grids.  
			This is a bad thing. 
			So it "cheat moves", to the nearest grid. 
			Also, for locos the "close enough" distance is 1 or so. 
			So it moves the rest of the way to it's goal location by cheating.

		*/
		// Update the goal.
		Coord3D goalPos = *obj->getPosition();
		// but only if we have a valid position.
		if (goalPos.x || goalPos.y || goalPos.z)
		{
			TheAI->pathfinder()->updateGoal(obj, &goalPos, obj->getLayer());
			if (!ultraAccurate && TheAI->pathfinder()->goalPosition(obj, &goalPos)) 
			{
				if (TheGameLogic->getFrame()<=1) {
					obj->setPosition(&goalPos);
				} else {
#ifdef STOP_AND_SLIDE
					ai->setFinalPosition(&goalPos);
#endif
				}
				TheAI->pathfinder()->updateGoal(obj, &goalPos, obj->getLayer());
			}
		}
	}

	ai->setLocomotorGoalNone();
	ai->setCurrentVictim(nullptr);	 
}

//----------------------------------------------------------------------------------------------
/**
 * Just sit there.
 */
StateReturnType AIIdleState::update()
{
	USE_PERF_TIMER(AIIdleState)

	doInitIdleState();

	UnsignedInt timeToSleep = IDLE_COUNTDOWN_DELAY + m_initialSleepOffset;
	UnsignedInt oldSleepOffset = m_initialSleepOffset;
	m_initialSleepOffset = 0;

	// This state is used internally some places, so we don't necessarily want to be looking for targets
	// Places that use AI_IDLE internally should set this to false in the constructor. jkmcd
	if ( m_shouldLookForTargets && !getMachine()->isLocked() )
	{
		// if we are here, it's time to check again
		Object *obj = getMachineOwner();
		AIUpdateInterface *ai = obj->getAI();

		// do repulsor logic
		if (obj->isKindOf(KINDOF_CAN_BE_REPULSED) && ai->isIdle()) 
		{
			Object* enemy = TheAI->findClosestRepulsor(obj, obj->getVisionRange());
			if (enemy) 
			{
				getMachine()->setState(AI_MOVE_AWAY_FROM_REPULSORS);
				// since we just changed the state, it doesn't really matter what we return here.
				return STATE_CONTINUE;
			}
		}

		// Check to see if we have created a crate we need to pick up.
		Object* crate = ai->checkForCrateToPickup();
		if (crate)
		{
			ai->aiMoveToObject(crate, CMD_FROM_AI);
			// since we just changed the state, it doesn't really matter what we return here.
			return STATE_CONTINUE;
		}

		
		if (! obj->isDisabledByType( DISABLED_PARALYZED ) &&
				! obj->isDisabledByType( DISABLED_UNMANNED ) &&
				! obj->isDisabledByType( DISABLED_EMP ) &&
				! obj->isDisabledByType( DISABLED_SUBDUED ) &&
				! obj->isDisabledByType( DISABLED_HACKED ) )
		{
			// mood targeting
			UnsignedInt moodAdjust = ai->getMoodMatrixActionAdjustment(MoodMatrixAction::MM_Action_Idle);
			if ((moodAdjust & MAA_Affect_Range_IgnoreAll) == 0)
			{
				// If we're supposed to attack based on mood, etc, then we will do so.
				Object* enemy = ai->getNextMoodTarget( true, true );
				if (enemy) 
				{
	 				ai->aiAttackObject(enemy, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
					// weird but true. return state_continue, because if we're here, we're actually an attack state
					// since we just changed the state, it doesn't really matter what we return here.
					return STATE_CONTINUE;
				}
			}
		}

		UnsignedInt now = TheGameLogic->getFrame();
		UnsignedInt nextMoodCheckTime = ai->getNextMoodCheckTime();
		if (nextMoodCheckTime > now)
		{
			// if we need to look for targets, might need to sleep less.
			UnsignedInt moodSleep = nextMoodCheckTime - now;
			if (moodSleep < timeToSleep)
			{
				timeToSleep = moodSleep;
				// if we do this, save the random sleep offset for next time.
				m_initialSleepOffset = static_cast<UnsignedShort>(oldSleepOffset);
			}
		}
	}  // end if, should look for targets
	
	return STATE_SLEEP(timeToSleep);
}

//----------------------------------------------------------------------------------------------
/**
 * Just sit there, but dead-like.
 */
StateReturnType AIDeadState::onEnter()
{
	Object *obj = getMachineOwner();

	// How can an object be NULL here? I don't think it actually can, but this check must be 
	// here for a reason. - jkmcd
	if (obj)
	{
		ModelConditionFlags nonDyingStuff;
		nonDyingStuff.set(MODELCONDITION_USING_WEAPON_A);
		nonDyingStuff.set(MODELCONDITION_USING_WEAPON_B);
		nonDyingStuff.set(MODELCONDITION_USING_WEAPON_C);
		nonDyingStuff.set(MODELCONDITION_FIRING_A);
		nonDyingStuff.set(MODELCONDITION_FIRING_B);
		nonDyingStuff.set(MODELCONDITION_FIRING_C);
		nonDyingStuff.set(MODELCONDITION_BETWEEN_FIRING_SHOTS_A);
		nonDyingStuff.set(MODELCONDITION_BETWEEN_FIRING_SHOTS_B);
		nonDyingStuff.set(MODELCONDITION_BETWEEN_FIRING_SHOTS_C);
		nonDyingStuff.set(MODELCONDITION_RELOADING_A);
		nonDyingStuff.set(MODELCONDITION_RELOADING_B);
		nonDyingStuff.set(MODELCONDITION_RELOADING_C);
		nonDyingStuff.set(MODELCONDITION_PREATTACK_A);
		nonDyingStuff.set(MODELCONDITION_PREATTACK_B);
		nonDyingStuff.set(MODELCONDITION_PREATTACK_C);
		nonDyingStuff.set(MODELCONDITION_MOVING);

		// dying objects are NEVER firing, surrendered, etc, so clear 'em all here.
		obj->clearAndSetModelConditionFlags(nonDyingStuff, MAKE_MODELCONDITION_MASK(MODELCONDITION_DYING));
		TheScriptEngine->notifyOfObjectCreationOrDestruction();
	}

	return STATE_CONTINUE;
}


StateReturnType AIDeadState::update()
{

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	ai->setLocomotorGoalNone();

	// re-mark this every time, just in case our health miraculously heals from 0 to nonzero...
	obj->setEffectivelyDead(true);

	if (obj->isKindOf(KINDOF_INFANTRY))
	{
		PhysicsBehavior* phys = obj->getPhysics();
		if (phys)
		{
			// we want 'em to stop, but looks wonky if they stop dead in their tracks in onEnter.
			// this slows 'em down quickly 
			const Real FACTOR = 0.8f;	// 0.8 ^ 30 == 0.012, so they slow to 4% of speed over 1 sec
			Real vel = phys->getVelocityMagnitude();
			phys->scrubVelocity2D(vel * FACTOR);
		}
	}

	return STATE_CONTINUE;
}

void AIDeadState::onExit( StateExitType status )
{
	Object *obj = getMachineOwner();
	obj->clearModelConditionState(MODELCONDITION_DYING);
}

/**
 * Execute the moveTo behavior towards GoalPosition.
 */

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//  
// note - has no crc/xfer as has no member vars. jba.

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//  
// note - has no crc/xfer as has no member vars. jba.

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------




//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------


//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------




//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------


//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
StateReturnType AIAttackFollowWaypointPathState ::onEnter()
{
	m_attackFollowMachine->clear();
	m_attackFollowMachine->setState( AI_IDLE );	

	return AIFollowWaypointPathState::onEnter();
}

//-------------------------------------------------------------------------------------------------
void AIAttackFollowWaypointPathState ::onExit( StateExitType status )
{
	m_attackFollowMachine->setState(AI_IDLE);
	AIFollowWaypointPathState::onExit(status);
}


//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
/**
 * Wander along a waypoint path.
 */



//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
/**
 * Wander around a point.
 */


//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------


//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
/**
 * Orient the machine's owner to face towards the given target
 */

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
/**
 * Fire the owner's weapon once and exit.
 */

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
/**
 * Do nothing for a period of time.
 */

StateReturnType AIWaitState::update()
{
			/// @todo srj -- find a way to sleep for a number of frames here, if possible
	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
/**
 * Begin an attack on the machine's goal object.
 * To do this complex behavior, instantiate another state machine as a "sub-machine" of 
 * the attack state.
 */
DECLARE_PERF_TIMER(AIAttackState)

//----------------------------------------------------------------------------------------------------------
/**
 * Execute one frame of "attack enemy" behavior.
 */

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
/**
 * Execute one frame of "attack enemy" behavior.
 */

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

/**
 * We are in the AI_DOCK state, execute the docking behavior.
 */

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------



//----------------------------------------------------------------------------------------------------------
/**
 * Guard location.
 */

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------




//----------------------------------------------------------------------------------------------------------
/**
 * Guard location.
 */

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------



//----------------------------------------------------------------------------------------------------------
/**
 * Guard location.
 */

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
/**
 * Hunt (seek and destroy).
 */


//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------


//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIFaceState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIFaceState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferBool(&m_canTurnInPlace);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIFaceState::loadPostProcess( void )
{
	// empty.  jba.
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
StateReturnType AIFaceState::onEnter()
{
	Object* source = getMachineOwner();

	AIUpdateInterface* ai = source->getAI();
	Locomotor* curLoco = ai->getCurLocomotor();
	m_canTurnInPlace = curLoco ? curLoco->getMinSpeed() == 0.0f : false;

	Object* target = getMachineGoalObject();
	if (m_obj && target == nullptr )
	{
		// Nothing to face...
		return STATE_FAILURE; 
	}

	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------------------
void AIFaceState::onExit( StateExitType status )
{
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIFaceState::update()
{

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	const Coord3D* pos = getMachineGoalPosition();
	if (m_obj)
	{
		Object *target = getMachineGoalObject();
		if (!target)
		{
			// Nothing to face.
			return STATE_FAILURE;	
		}
		pos = target->getPosition();
	}
	Real relAngle = ThePartitionManager->getRelativeAngle2D( obj, pos );

	const Real REL_THRESH = 0.035f;	// about 2 degrees. (getRelativeAngle2D is current only accurate to about 1.25 degrees)
	if( fabs( relAngle ) < REL_THRESH )
	{
		return STATE_SUCCESS;
	}

	// turnDelta = yawRate()	NO, do not get this, it is not useful. (srj)

	if (m_canTurnInPlace)
	{
		Real desiredAngle = obj->getOrientation() + relAngle;
		ai->setLocomotorGoalOrientation( desiredAngle );
	}
	else
	{
		ai->setLocomotorGoalPositionExplicit(*pos);
	}

	return STATE_CONTINUE;
}
