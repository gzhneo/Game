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

// Attack states: approach, pursue, aim, fire, attack-move, squads, areas, hunting

//-----------------------------------------------------------------------------------------------------------
class AIAttackMoveStateMachine : public StateMachine
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AIAttackMoveStateMachine, "AIAttackMoveStateMachine" );

public:

	AIAttackMoveStateMachine( Object *owner, AsciiString name );

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer ) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess() override;
};

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackMoveStateMachine::crc( Xfer *xfer )
{
	StateMachine::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackMoveStateMachine::xfer( Xfer *xfer )
{
	XferVersion cv = 1;	
	XferVersion v = cv; 
	xfer->xferVersion( &v, cv );

	StateMachine::xfer(xfer);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackMoveStateMachine::loadPostProcess( void )
{
	StateMachine::loadPostProcess();
}  // end loadPostProcess

//-----------------------------------------------------------------------------------------------------------
AIAttackMoveStateMachine::AIAttackMoveStateMachine(Object *owner, AsciiString name) : StateMachine(owner, name)
{
	// order matters: first state is the default state.
	defineState( AI_IDLE, newInstance(AIIdleState)( this, AIIdleState::AIIdleTargetingType::DO_NOT_LOOK_FOR_TARGETS ), AI_IDLE, AI_IDLE );
	defineState( AI_PICK_UP_CRATE, newInstance(AIPickUpCrateState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_ATTACK_OBJECT,	newInstance(AIAttackState)(this, false, true, false, nullptr ), AI_IDLE, AI_IDLE);
}

//----------------------------------------------------------------------------------------------------------
AIAttackMoveStateMachine::~AIAttackMoveStateMachine()
{
}

/**
 * Returns true if we can pursue the unit.  Requires that :
 * 1. We are faster than the other unit.
 * 2. The other unit is moving.
 * 3. The other unit is moving away from us.
 */
static Bool canPursue(Object *source, Weapon *weapon, Object *victim) 
{
	/* This state is only used if the target is moving away from us, and has physics. */
	if (!victim->getPhysics()) {
		return false;
	}
	AIUpdateInterface *ai = source->getAI();
	if (!ai) {
		return false;
	}	

	// Have to have a turret to pursue.
	WhichTurretType tur = ai->getWhichTurretForCurWeapon();
	if (tur == TURRET_INVALID) {
		return false;
	}

	if (TheAI->getAiData()->m_aiCrushesInfantry) {
		if ( source->getControllingPlayer() && 
			(source->getControllingPlayer()->getPlayerType() == PLAYER_COMPUTER) &&
			source->canCrushOrSquish(victim) ) {
			return true;	// Always pursue if we can squish.
		}
	}

	if (weapon->isTooClose(source, victim)) {
		return false;		// Don't chase it if we are already too close.
	}

	Real ourMaxSpeed = source->getAI()->getCurLocomotorSpeed();

	Real victimSpeed = victim->getPhysics()->getForwardSpeed2D();
	if (victimSpeed >= ourMaxSpeed) {
		return false; // we can't catch them.
	}
	if (victimSpeed < ourMaxSpeed/10) {
		return false; // They aren't moving very fast, so don't chase.
	}
	Real dx = victim->getPosition()->x - source->getPosition()->x;
	Real dy = victim->getPosition()->y - source->getPosition()->y;
	Coord3D victimVector = *victim->getUnitDirectionVector2D();
	if (dx*victimVector.x + dy*victimVector.y < 0 ) {
		return false; // they are moving towards us.
	}
	return true;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Compute a valid spot to fire our weapon from.
 * Result in m_goalPosition.
 * Return false if can't find a good spot.
 */
Bool AIAttackApproachTargetState::computePath()
{
	Bool forceRepath = false;

	// if we're immobile we can't possibly approach the target
	if( getMachineOwner()->isMobile() == false )
		return false;

	//CRCDEBUG_LOG(("AIAttackApproachTargetState::computePath - begin for object %d\n", getMachineOwner()->getID()));

	AIUpdateInterface *ai = getMachineOwner()->getAI();

	if (ai->isBlockedAndStuck()) 
	{
		forceRepath = true;
		// Intense logging. jba
		//CRCDEBUG_LOG(("AIAttackApproachTargetState::computePath - stuck, recomputing for object %d\n", getMachineOwner()->getID()));
	}
	if (m_waitingForPath) return true;

	if (!forceRepath && ai->getPath()==nullptr && !ai->isWaitingForPath()) 
	{
		forceRepath = true;
	}

	// force minimum time between recomputation
	/// @todo Unify recomputation conditions & account for obj ID so everyone doesnt compute on the same frame (MSB)
	if (!forceRepath && TheGameLogic->getFrame() - m_approachTimestamp < MIN_RECOMPUTE_TIME)
	{
		//CRCDEBUG_LOG(("AIAttackApproachTargetState::computePath - bailing because of min time for object %d\n", getMachineOwner()->getID()));
		return true;
	}

	m_approachTimestamp = TheGameLogic->getFrame();

	// if we have a goal object, move to it, otherwise move to goal position
	if (getMachineGoalObject())
	{

		Object* source = getMachineOwner();
		// if our victim's position hasn't changed, don't re-path
		if (!forceRepath && isSamePosition(source->getPosition(), &m_prevVictimPos, getMachineGoalObject()->getPosition() ))
		{
			CRCDEBUG_LOG(("AIAttackApproachTargetState::computePath - bailing because victim in same place for object %d\n", getMachineOwner()->getID()));
			return true;
		}

		Weapon* weapon = source->getCurrentWeapon();
		if (!weapon) 
		{
			CRCDEBUG_LOG(("AIAttackApproachTargetState::computePath - bailing because of no weapon for object %d\n", getMachineOwner()->getID()));
			return false;
		}

		// remember where we think our victim is, so if it moves, we can re-path
		Object *victim = getMachineGoalObject();	 
		m_prevVictimPos = *victim->getPosition();
		if (canPursue(source, weapon, victim)) 
		{
			return false;  // break out, and do the pursuit state.
		}
		setAdjustsDestination(true);
		if (weapon->isContactWeapon()) 
		{
			// Weapon is basically a contact weapon, so let the attacker pathfind into the target.
			ai->ignoreObstacle(victim);
			setAdjustsDestination(false); // We want to run into the target.
			ai->setPathExtraDistance(10*PATHFIND_CELL_SIZE_F); // We don't want it to slow down.
		}

		m_goalPosition = m_prevVictimPos;
		m_waitingForPath = true;

		Coord3D pos;
		victim->getGeometryInfo().getCenterPosition( *victim->getPosition(), pos );
		
		CRCDEBUG_LOG(("AIAttackApproachTargetState::computePath - requestAttackPath() for object %d\n", getMachineOwner()->getID()));
		ai->requestAttackPath(victim->getID(), &pos );
		m_stopIfInRange = false; // we have calculated a position to shoot from, so go there.

		CRCDEBUG_LOG(("AIAttackApproachTargetState::computePath - bailing after repathing for object %d\n", getMachineOwner()->getID()));
		return true;
	} 
	else 
	{
		// goal position.
		setAdjustsDestination(true);
		m_stopIfInRange = false; // Attack position is used by missiles, and they hit the position.
		m_goalPosition = *getMachineGoalPosition();
		if (!forceRepath) 
		{
			CRCDEBUG_LOG(("AIAttackApproachTargetState::computePath - bailing because we're aiming for a fixed position for object %d\n", getMachineOwner()->getID()));
			return true; // fixed positions don't move.
		}
		// must use computeAttackPath so that min ranges are considered.
		m_waitingForPath = true;
		ai->requestAttackPath(INVALID_ID, &m_goalPosition);
		CRCDEBUG_LOG(("AIAttackApproachTargetState::computePath - bailing after repathing at a fixed position for object %d\n", getMachineOwner()->getID()));
		return true;
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackApproachTargetState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackApproachTargetState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
  AIInternalMoveToState::xfer( xfer );
 
	xfer->xferCoord3D(&m_prevVictimPos);
	xfer->xferUnsignedInt(&m_approachTimestamp);
	xfer->xferBool(&m_follow);
	xfer->xferBool(&m_isAttackingObject);
	xfer->xferBool(&m_stopIfInRange);
	xfer->xferBool(&m_isInitialApproach);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackApproachTargetState::loadPostProcess( void )
{
 // extend base class
  AIInternalMoveToState::loadPostProcess();
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackApproachTargetState::onEnter()
{
	// contained by AIAttackState, so no separate timer
	// urg. hacky. if we are a projectile, turn on precise z-pos.
	//CRCDEBUG_LOG(("AIAttackApproachTargetState::onEnter() - object %d\n", getMachineOwner()->getID()));
	Object* source = getMachineOwner();
	AIUpdateInterface* ai = source->getAI();
	if (source->isKindOf(KINDOF_PROJECTILE))
	{
		if (ai->getCurLocomotor())
			ai->getCurLocomotor()->setUsePreciseZPos(true);
	}

	if (getMachine()->isGoalObjectDestroyed()) 
	{
		return STATE_SUCCESS; // Already killed victim.
	}

	//If our object is deployed, can't tell him to move!
	//if( source && source->testStatus( OBJECT_STATUS_DEPLOYED ) )
//	{
//		return STATE_SUCCESS;
//	}

	m_prevVictimPos.x = 0.0f;
	m_prevVictimPos.y = 0.0f;
	m_prevVictimPos.z = 0.0f;

	m_approachTimestamp = static_cast<UnsignedInt>(-MIN_RECOMPUTE_TIME);

	// See if we're close enough.
	Object *victim = getMachineGoalObject();
	if (victim) 
	{
		Weapon* weapon = source->getCurrentWeapon();
		if (!weapon) 
		{
			return STATE_FAILURE;
		}
		if (weapon->isWithinAttackRange(source, victim)) 
		{
			Bool viewBlocked = false;
			if (source && victim && ai->isDoingGroundMovement() && !victim->isSignificantlyAboveTerrain()) 
			{
				viewBlocked = TheAI->pathfinder()->isAttackViewBlockedByObstacle(source, *source->getPosition(), victim, *victim->getPosition());
			}
			if (!viewBlocked) 
			{
				return STATE_SUCCESS;
			}
		}

		// Check here:  If we are a player, and we got to this state via an ai command (ie we auto-acquired), 
		// we don't want to chase the unit. isAllowedToChase is set when we are in a deploy and attack state (troop crawler).
		// Kris (July 2003): If we are retaliating... don't fail out!
		if( ai->getCurrentStateID() != AI_GUARD_RETALIATE )
		{
			if (source->getControllingPlayer()->getPlayerType() == PLAYER_HUMAN) 
			{
				if (ai->getLastCommandSource() == CMD_FROM_AI && !ai->isAllowedToChase() ) 
				{
					if (!weapon->isContactWeapon()) 
					{
						return STATE_FAILURE; 
					}
				}
			} else {
				// Computer player.  Don't chase aircraft, unless we're hunting. jba [8/27/2003]
				Bool hunt = ai->getCurrentStateID() == AI_HUNT;
				if (!hunt && victim->isKindOf(KINDOF_AIRCRAFT) && victim->isAirborneTarget()) 
				{
					return STATE_FAILURE; 
				}
			}
		}
		if (canPursue(source, weapon, victim)) 
		{
			return STATE_SUCCESS;  // break out, and do the pursuit state.
		}
	} else {
		// Attacking a position.  For a varitey of reasons, we need to destroy any existing path or we spin. jba. [8/25/2003]
		ai->destroyPath();
	}
	// If we have a turret, start aiming.
	WhichTurretType tur = ai->getWhichTurretForCurWeapon();
	if (tur != TURRET_INVALID)
	{
		if (m_isAttackingObject)
		{
			ai->setTurretTargetObject(tur, victim, m_isForceAttacking);
		}
		else
		{
			ai->setTurretTargetPosition(tur, getMachineGoalPosition());
		}
	}

	// find a good spot to shoot from
	//CRCDEBUG_LOG(("AIAttackApproachTargetState::onEnter() - calling computePath() for object %d\n", getMachineOwner()->getID()));
	if (computePath() == false)
		return STATE_FAILURE;

	setAdjustsDestination(false);
	StateReturnType ret =  AIInternalMoveToState::onEnter();
	setAdjustsDestination(true);
	return ret;
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackApproachTargetState::updateInternal()
{
	AIUpdateInterface* ai = getMachineOwner()->getAI();
	//CRCDEBUG_LOG(("AIAttackApproachTargetState::updateInternal() - object %d\n", getMachineOwner()->getID()));
	if (getMachine()->isGoalObjectDestroyed()) 
	{
		ai->notifyVictimIsDead();
		ai->setCurrentVictim(nullptr);
		return STATE_FAILURE;
	}
	m_stopIfInRange = !ai->isAttackPath();

	StateReturnType code = STATE_FAILURE;
 	Object* source = getMachineOwner();
	Weapon* weapon = source->getCurrentWeapon();
	Object *victim = getMachineGoalObject();
	if (victim) 
	{ 
 		if (source->getControllingPlayer()->getPlayerType() == PLAYER_COMPUTER) 
		{
			Bool hunt = ai->getCurrentStateID() == AI_HUNT;
			// Computer player.  Don't chase aircraft unless hunting. jba [8/27/2003]
			if (!hunt && victim->isKindOf(KINDOF_AIRCRAFT) && victim->isAirborneTarget()) 
			{
				return STATE_FAILURE; 
			}
		}	
		if( victim->testStatus( OBJECT_STATUS_STEALTHED ) && !victim->testStatus( OBJECT_STATUS_DETECTED ) && !victim->testStatus( OBJECT_STATUS_DISGUISED ) ) 
		{
			return STATE_FAILURE;	// If obj is stealthed, can no longer approach.
		}
		ai->setCurrentVictim(victim);
		// Attacking an object.
		if (weapon && weapon->isContactWeapon() && weapon->isWithinAttackRange(source, victim)) 
		{
			return STATE_SUCCESS;
		}
		if (m_stopIfInRange && weapon && weapon->isWithinAttackRange(source, victim)) 
		{
			Bool viewBlocked = false;
			if (victim && ai->isDoingGroundMovement() && !victim->isSignificantlyAboveTerrain()) 
			{
				viewBlocked = TheAI->pathfinder()->isAttackViewBlockedByObstacle(source, *source->getPosition(), victim, *victim->getPosition());
			}
			if (!viewBlocked) 
			{
				return STATE_SUCCESS;
			}
		}
		// find a good spot to shoot from
		//CRCDEBUG_LOG(("AIAttackApproachTargetState::updateInternal() - calling computePath() to victim for object %d\n", getMachineOwner()->getID()));
		if (computePath() == false)
			return STATE_SUCCESS;
		code = AIInternalMoveToState::update();


		if (code != STATE_CONTINUE) 
		{
			return STATE_SUCCESS;	// Always return state success, as state failure exits the attack.
			// we may need to aim & do another approach if the target moved.  jba.
		}
	}																			
	else 
	{
		// Attacking a position.
		// find a good spot to shoot from
		//CRCDEBUG_LOG(("AIAttackApproachTargetState::updateInternal() - calling computePath() to position for object %d\n", getMachineOwner()->getID()));
		if (m_stopIfInRange && weapon && weapon->isWithinAttackRange(source, &m_goalPosition)) 
		{
			Bool viewBlocked = false;
			if ( ai->isDoingGroundMovement() ) 
			{
				viewBlocked = TheAI->pathfinder()->isAttackViewBlockedByObstacle(source, *source->getPosition(), nullptr, m_goalPosition);
			}
			if (!viewBlocked) 
			{
				return STATE_SUCCESS;
			}
		}
		if (computePath() == false)
			return STATE_FAILURE;
		code = AIInternalMoveToState::update();
	}
	return code;
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackApproachTargetState::update()
{
	// contained by AIAttackState, so no separate timer

	StateReturnType code = updateInternal();
	Object* source = getMachineOwner();
	AIUpdateInterface *ai = source->getAI();

	if (m_follow && m_isAttackingObject)
	{
		// Basically, if the object is alive, we continue, in case the target moves.
		Object* victim = getMachineGoalObject();
		if (source && victim && source->isMobile() && !victim->getTemplate()->isKindOf(KINDOF_IMMOBILE)) 
		{
			if (code != STATE_CONTINUE) 
			{
				m_isInitialApproach = false;
			}
			// Object is still alive (and so are we)
			// It could move (and so can we), so just continue & keep checking.
			code = STATE_CONTINUE;
		}
	}

	if (m_isInitialApproach) 
	{
		WhichTurretType tur = ai->getWhichTurretForCurWeapon();
		if (tur != TURRET_INVALID) 
		{
			Object *temporaryTarget = ai->getNextMoodTarget( true, false );
			if (temporaryTarget) 
			{
				ai->setTurretTargetObject(tur, temporaryTarget, m_isForceAttacking);
			}
		}
	}

	return code;
}

//----------------------------------------------------------------------------------------------------------
void AIAttackApproachTargetState::onExit( StateExitType status )
{
	// contained by AIAttackState, so no separate timer
	AIInternalMoveToState::onExit( status );

	AIUpdateInterface *ai = getMachineOwner()->getAI();
	Object *obj = getMachineOwner();
	if (ai) {
		ai->ignoreObstacle(nullptr);
		
		// Per JohnA, this state should not be calling ai->destroyPath, because we can have spastic users
		// that click the target repeadedly. This will prevent the unit from stuttering for said spastic 
		// users.
		// ai->destroyPath();
		// urg. hacky. if we are a projectile, reset precise z-pos.
		if (getMachineOwner()->isKindOf(KINDOF_PROJECTILE))
		{
			if (ai && ai->getCurLocomotor())
				ai->getCurLocomotor()->setUsePreciseZPos(false);
		}
		if (ai->isDoingGroundMovement()) {
			Real dx = m_goalPosition.x-obj->getPosition()->x;
			Real dy = m_goalPosition.y-obj->getPosition()->y;
			if (dx*dx+dy*dy<PATHFIND_CELL_SIZE_F*PATHFIND_CELL_SIZE_F*0.125) 
			{
				// We are doing accurate ground movement, so make sure we end exactly at the goal.
				obj->setPosition(&m_goalPosition);
			}
		}
	}

	m_isInitialApproach = false;	// We only want to allow turreted things to fire at enemies during their
																// first approach
}

//----------------------------------------------------------------------------------------------------------
/**
 * Compute a valid spot to fire our weapon from.
 * Result in m_goalPosition.
 * Return false if can't find a good spot.
 */
Bool AIAttackPursueTargetState::computePath()
{
	Bool forceRepath = false;

	// if we're immobile we can't possibly approach the target
	if( getMachineOwner()->isMobile() == false )
		return false;

	AIUpdateInterface *ai = getMachineOwner()->getAI();

	if (ai->isBlockedAndStuck()) 
	{
		return false;
	}
	if (m_waitingForPath) return true;

	if (!forceRepath && ai->getPath()==nullptr && !ai->isWaitingForPath()) 
	{
		forceRepath = true;
	}

	// force minimum time between recomputation
	/// @todo Unify recomputation conditions & account for obj ID so everyone doesnt compute on the same frame (MSB)
	if (!forceRepath && TheGameLogic->getFrame() - m_approachTimestamp < MIN_RECOMPUTE_TIME)
	{
		return true;
	}

	m_approachTimestamp = TheGameLogic->getFrame();

	DEBUG_ASSERTLOG(getMachineGoalObject(), ("***************************Should only be pursuing objects.  jba"));
	// if we have a goal object, move to it, otherwise fail & continue to AIAttackApproachTargetState
	if (getMachineGoalObject())
	{

		Object* source = getMachineOwner();
		// if our victim's position hasn't changed, don't re-path
		if (!forceRepath && isSamePosition(source->getPosition(), &m_prevVictimPos, getMachineGoalObject()->getPosition() ))
			return true;

		Weapon* weapon = source->getCurrentWeapon();
		if (!weapon) 
		{
			return false;
		}
		if (!canPursue(source, weapon, getMachineGoalObject())) {
			return false;
		}

		// remember where we think our victim is, so if it moves, we can re-path
		Object *victim = getMachineGoalObject();
		m_prevVictimPos = *victim->getPosition();

		setAdjustsDestination(true);

		m_goalPosition = m_prevVictimPos;
		m_waitingForPath = true;
		ai->requestPath(&m_goalPosition, false);
		m_stopIfInRange = false; // we have calculated a position to shoot from, so go there.
		return true;
	} 

	return false;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackPursueTargetState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackPursueTargetState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
  AIInternalMoveToState::xfer( xfer );
 
	xfer->xferCoord3D(&m_prevVictimPos);
	xfer->xferUnsignedInt(&m_approachTimestamp);
	xfer->xferBool(&m_follow);
	xfer->xferBool(&m_isAttackingObject);
	xfer->xferBool(&m_stopIfInRange);
	xfer->xferBool(&m_isInitialApproach);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackPursueTargetState::loadPostProcess( void )
{
 // extend base class
  AIInternalMoveToState::loadPostProcess();
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackPursueTargetState::onEnter()
{
	// contained by AIAttackState, so no separate timer
	// If we return STATE_SUCCESS or STATE_FAILURE, we proceed to AIAttackApproachTargetState.
	Object* source = getMachineOwner();
	AIUpdateInterface* ai = source->getAI();	 

	if (source->isKindOf(KINDOF_PROJECTILE))
	{
		//CRCDEBUG_LOG(("AIAttackPursueTargetState::onEnter() - is a projectile for object %d (%s)\n", getMachineOwner()->getID(), getMachineOwner()->getTemplate()->getName().str()));
		return STATE_SUCCESS;	 // Projectiles go directly to AIAttackApproachTargetState.
	}

	if (getMachine()->isGoalObjectDestroyed()) 
	{
		//CRCDEBUG_LOG(("AIAttackPursueTargetState::onEnter() - goal object is destroyed for object %d (%s)\n", getMachineOwner()->getID(), getMachineOwner()->getTemplate()->getName().str()));
		return STATE_SUCCESS; // Already killed victim.
	}
	if (!m_isAttackingObject)	{
		//CRCDEBUG_LOG(("AIAttackPursueTargetState::onEnter() - not attacking for object %d (%s)\n", getMachineOwner()->getID(), getMachineOwner()->getTemplate()->getName().str()));
		return STATE_SUCCESS; // only pursue objects - positions don't move.
	}

	setAdjustsDestination(false);

	// Check here:  If we are a player, and we got to this state via an ai command (ie we auto-acquired), 
	// we don't want to chase the unit. 
	// Kris (July 2003): If we are retaliating... don't succeed out!
	if( ai->getCurrentStateID() != AI_GUARD_RETALIATE )
	{
		if (source->getControllingPlayer()->getPlayerType() == PLAYER_HUMAN) 
		{
			if (ai->getLastCommandSource() == CMD_FROM_AI) 
			{
				return STATE_SUCCESS;

			}
		}
	}

	m_prevVictimPos.x = 0.0f;
	m_prevVictimPos.y = 0.0f;
	m_prevVictimPos.z = 0.0f;

	m_approachTimestamp = static_cast<UnsignedInt>(-MIN_RECOMPUTE_TIME);

	// See if we're close enough.
	Object *victim = getMachineGoalObject();
	if (victim) {	
		Weapon* weapon = source->getCurrentWeapon();
		if (!weapon) 
		{
			return STATE_FAILURE;
		}
		if (!canPursue(source, weapon, victim) ) 
		{
			//CRCDEBUG_LOG(("AIAttackPursueTargetState::onEnter() - can't pursue for object %d (%s)\n", getMachineOwner()->getID(), getMachineOwner()->getTemplate()->getName().str()));
			return STATE_SUCCESS;
		}
	}	else {
		//CRCDEBUG_LOG(("AIAttackPursueTargetState::onEnter() - no victim for object %d (%s)\n", getMachineOwner()->getID(), getMachineOwner()->getTemplate()->getName().str()));
		return STATE_SUCCESS; // gotta have a victim.
	}
	// If we have a turret, start aiming.
	WhichTurretType tur = ai->getWhichTurretForCurWeapon();
	if (tur != TURRET_INVALID)
	{
		ai->setTurretTargetObject(tur, victim, m_isForceAttacking);
	} else {
		//CRCDEBUG_LOG(("AIAttackPursueTargetState::onEnter() - no turret for object %d (%s)\n", getMachineOwner()->getID(), getMachineOwner()->getTemplate()->getName().str()));
		return STATE_SUCCESS; // we only pursue with turrets, as non-turreted weapons can't fire on the run.
	}

	// find a good spot to shoot from
	if (computePath() == false)
		return STATE_SUCCESS;

	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackPursueTargetState::updateInternal()
{
	AIUpdateInterface* ai = getMachineOwner()->getAI();	  
	if (getMachine()->isGoalObjectDestroyed()) 
	{
		ai->notifyVictimIsDead();
		ai->setCurrentVictim(nullptr);
		return STATE_FAILURE;
	}
	m_stopIfInRange = false;

	Object* source = getMachineOwner();
	StateReturnType code = STATE_FAILURE;
 	Object *victim = getMachineGoalObject();
	if (victim) 
	{ 
		if( victim->testStatus( OBJECT_STATUS_STEALTHED ) && !victim->testStatus( OBJECT_STATUS_DETECTED ) && !victim->testStatus( OBJECT_STATUS_DISGUISED ) )
		{
			return STATE_FAILURE;	// If obj is stealthed, can no longer pursue.
		}
		ai->setCurrentVictim(victim);
		// Attacking an object.
		// find a good spot to shoot from
		if (computePath() == false)
			return STATE_FAILURE;
		code = AIInternalMoveToState::update();
		if (code != STATE_CONTINUE) 
		{
			//CRCDEBUG_LOG(("AIAttackPursueTargetState::updateInternal() - failed internal update() for object %d (%s)\n", getMachineOwner()->getID(), getMachineOwner()->getTemplate()->getName().str()));
			return STATE_SUCCESS;	// Always return state success, as state failure exits the attack.
			// we may need to aim & do another approach if the target moved.  jba.
		}
		Weapon* weapon = source->getCurrentWeapon();
		if (!weapon) 
			return STATE_FAILURE;
		// If we have a turret, start aiming.
		WhichTurretType tur = ai->getWhichTurretForCurWeapon();
		if (tur == TURRET_INVALID)
		{
			//CRCDEBUG_LOG(("AIAttackPursueTargetState::updateInternal() - no turret for object %d (%s)\n", getMachineOwner()->getID(), getMachineOwner()->getTemplate()->getName().str()));
			return STATE_SUCCESS; // We currently only pursue with a turret weapon.
		}

		Bool viewBlocked = false;
		if (ai->isDoingGroundMovement() && !victim->isSignificantlyAboveTerrain()) 
		{
			viewBlocked = TheAI->pathfinder()->isAttackViewBlockedByObstacle(source, *source->getPosition(), victim, *victim->getPosition());
		}
		if (!viewBlocked && victim->getPhysics() && weapon->isWithinAttackRange(source, victim)) {
			// If we have a turret, start aiming.
			ai->setTurretTargetObject(tur, victim, m_isForceAttacking);
			//  match speeds;
			m_isInitialApproach = false;
			Real victimSpeed = victim->getPhysics()->getForwardSpeed2D();
			if (weapon->isGoalPosWithinAttackRange(source, source->getPosition(), victim, victim->getPosition())){
				victimSpeed *= 0.95f;
			}
			if (source->canCrushOrSquish(victim)) {
				victimSpeed = FAST_AS_POSSIBLE;
			}
			ai->setDesiredSpeed(victimSpeed);
			// Really intense debug info.  jba.
			// DEBUG_LOG(("VS %f, OS %f, goal %f\n", victim->getPhysics()->getForwardSpeed2D(), source->getPhysics()->getForwardSpeed2D(), victimSpeed));
		}	else {
			ai->setDesiredSpeed(FAST_AS_POSSIBLE);
		}
	}																			
	return code;
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackPursueTargetState::update()
{
	// contained by AIAttackState, so no separate timer

	StateReturnType code = updateInternal();
	Object* source = getMachineOwner();
	AIUpdateInterface *ai = source->getAI();

	if (m_isInitialApproach) 
	{
		WhichTurretType tur = ai->getWhichTurretForCurWeapon();
		if (tur != TURRET_INVALID) 
		{
			Object *temporaryTarget = ai->getNextMoodTarget( true, false );
			if (temporaryTarget) 
			{
				ai->setTurretTargetObject(tur, temporaryTarget, m_isForceAttacking);
			}
		}
	}

	return code;
}

//----------------------------------------------------------------------------------------------------------
void AIAttackPursueTargetState::onExit( StateExitType status )
{
	// contained by AIAttackState, so no separate timer
	//CRCDEBUG_LOG(("AIAttackPursueTargetState::onExit() for object %d (%s)\n", getMachineOwner()->getID(), getMachineOwner()->getTemplate()->getName().str()));
	AIInternalMoveToState::onExit( status );

	m_isInitialApproach = false;	// We only want to allow turreted things to fire at enemies during their
																// first approach
}

//-------------------------------------------------------------------------------------------------
AIAttackMoveToState::AIAttackMoveToState( StateMachine *machine ) : AIMoveToState(machine)
{
#ifdef STATE_MACHINE_DEBUG
	setName("AIAttackMoveToState");
#endif	// set up the state
	m_isMoveTo = false;
	m_frameToSleepUntil = 0;
	m_retryCount = ATTACK_RETRY_COUNT;
	m_attackMoveMachine = newInstance(AIAttackMoveStateMachine)(getMachineOwner(), "AIAttackMoveMachine");
	m_attackMoveMachine->initDefaultState();
}

//----------------------------------------------------------------------------------------------------------
AIAttackMoveToState::~AIAttackMoveToState()
{
	deleteInstanceOf(m_attackMoveMachine);
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackMoveToState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackMoveToState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 2;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
  AIMoveToState::xfer( xfer );

	if (version>=2) {
		xfer->xferUnsignedInt(&m_frameToSleepUntil);
		xfer->xferInt(&m_retryCount);
	}
	xfer->xferSnapshot(m_attackMoveMachine);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackMoveToState::loadPostProcess( void )
{
}  // end loadPostProcess

#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AIAttackMoveToState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if (m_attackMoveMachine) name.concat(m_attackMoveMachine->getCurrentStateName());
	else name.concat("*NULL m_deployMachine");
	return name;
}
#endif

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackMoveToState::onEnter()
{
	Object *owner = getMachineOwner();
	AIUpdateInterface *ai = owner->getAI();
	m_attackMoveMachine->clear();
	m_attackMoveMachine->setState( AI_IDLE );	
	m_commandSrc = ai->getLastCommandSource();
	m_retryCount = ATTACK_RETRY_COUNT;
	m_frameToSleepUntil = 0;

	return AIMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------------------
void AIAttackMoveToState::onExit( StateExitType status )
{
	m_attackMoveMachine->setState(AI_IDLE);
	AIMoveToState::onExit(status);
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackMoveToState::update()
{

	Object *owner = getMachineOwner();
	AIUpdateInterface *ai = owner->getAI();

	Bool forceRetargetThisFrame = false;
	Bool shouldRepathThisFrame = false;

	JetAIUpdate *jetAI = ai->getJetAIUpdate();
	if( jetAI && jetAI->isOutOfSpecialReloadAmmo() )
	{
		//We need to return to base to reload!
		return STATE_SUCCESS;
	}

	if (!m_attackMoveMachine->isInIdleState()) 
	{
		ai->setLocomotorGoalNone();
		owner->clearModelConditionState(MODELCONDITION_MOVING);
		m_attackMoveMachine->updateStateMachine();
		
		// if the machine is now idling, then we need to attempt to get a new target
		if (m_attackMoveMachine->isInIdleState()) {
			forceRetargetThisFrame = true;
			shouldRepathThisFrame = true;
			ai->friend_setLastCommandSource(m_commandSrc);
		} else {
			return STATE_CONTINUE;
		}
	}

	if (m_attackMoveMachine->isInIdleState())
	{

		// Check to see if we have created a crate we need to pick up.
		Object* crate = ai->checkForCrateToPickup();
		if (crate)
		{
			m_attackMoveMachine->setGoalObject(crate);
			m_attackMoveMachine->setState( AI_PICK_UP_CRATE );
			return STATE_CONTINUE;
		}
		
		Object* nextObjectToAttack;
		nextObjectToAttack = ai->getNextMoodTarget( !forceRetargetThisFrame, false );
		if (nextObjectToAttack != nullptr)
		{
			ai->friend_endingMove();
			m_attackMoveMachine->setGoalObject(nextObjectToAttack);
			m_attackMoveMachine->setState( AI_ATTACK_OBJECT );
			shouldRepathThisFrame = false;	// we're about to drop out of this function, but this is semantic emphasis.
			// Note that we picked up this command from the ai.
			ai->friend_setLastCommandSource(CMD_FROM_AI);
			// we don't want an update to take place 'till next frame.
			return STATE_CONTINUE;
		}
	}

	if (m_frameToSleepUntil>TheGameLogic->getFrame()) {
		return STATE_CONTINUE;
	} else if (m_frameToSleepUntil == TheGameLogic->getFrame()) {
		shouldRepathThisFrame = true;
	}

	if (shouldRepathThisFrame) 
	{
		AIMoveToState::onEnter();
		forceRepath();
	}

	StateReturnType ret = AIMoveToState::update();
	if (ret != STATE_CONTINUE) {
		if (m_retryCount<1) return ret;
		/* check for close enough. */
		Real distSqr = sqr(owner->getPosition()->x - m_pathGoalPosition.x) + sqr(owner->getPosition()->y-m_pathGoalPosition.y);
		if (distSqr < sqr(static_cast<Real>(ATTACK_CLOSE_ENOUGH_CELLS)*PATHFIND_CELL_SIZE_F)) {
			return ret;
		}
		DEBUG_LOG(("AIAttackMoveToState::update Distance from goal %f, retrying.\n", sqrt(distSqr)));

		ret = STATE_CONTINUE;
		m_retryCount--;
		// Sleep 3 seconds.  We can attack during these frames, just not move.
		m_frameToSleepUntil = TheGameLogic->getFrame() + 3*LOGICFRAMES_PER_SECOND;
	}
	return ret;
}

//-------------------------------------------------------------------------------------------------
AIAttackFollowWaypointPathState::AIAttackFollowWaypointPathState ( StateMachine *machine, Bool asGroup ) : 
AIFollowWaypointPathState ( machine, asGroup, false ) 
{
#ifdef STATE_MACHINE_DEBUG
	setName("AIAttackFollowWaypointPathState");
#endif	
	m_attackFollowMachine = newInstance(AIAttackMoveStateMachine)(getMachineOwner(), "AIAttackFollowMachine");
	m_attackFollowMachine->initDefaultState();
}

//-------------------------------------------------------------------------------------------------
AIAttackFollowWaypointPathState::~AIAttackFollowWaypointPathState()
{
	deleteInstanceOf(m_attackFollowMachine);
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackFollowWaypointPathState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackFollowWaypointPathState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
  AIFollowWaypointPathState::xfer( xfer );
 
	xfer->xferSnapshot(m_attackFollowMachine);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackFollowWaypointPathState::loadPostProcess( void )
{
}  // end loadPostProcess

#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AIAttackFollowWaypointPathState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if (m_attackFollowMachine) name.concat(m_attackFollowMachine->getCurrentStateName());
	else name.concat("*NULL m_attackFollowMachine");
	return name;
}
#endif

//-------------------------------------------------------------------------------------------------
StateReturnType AIAttackFollowWaypointPathState::update()
{

	Object *owner = getMachineOwner();
	AIUpdateInterface *ai = owner->getAI();

	Bool forceRetargetThisFrame = false;
	Bool shouldRepathThisFrame = false;
	if (!m_attackFollowMachine->isInIdleState()) 
	{
		ai->setLocomotorGoalNone();
		owner->clearModelConditionState(MODELCONDITION_MOVING);
		m_attackFollowMachine->updateStateMachine();
		
		// if the machine is now idling, then we need to attempt to get a new target
		if (m_attackFollowMachine->isInIdleState()) 
		{
			forceRetargetThisFrame = true;
			shouldRepathThisFrame = true;
		} 
		else 
		{
			return STATE_CONTINUE;
		}
	}

	if (m_attackFollowMachine->isInIdleState())
	{
		// Check to see if we have created a crate we need to pick up.
		Object* crate = ai->checkForCrateToPickup();
		if (crate)
		{
			m_attackFollowMachine->setGoalObject(crate);
			m_attackFollowMachine->setState( AI_PICK_UP_CRATE );
			return STATE_CONTINUE;
		}

		Object* nextObjectToAttack;
		{

			nextObjectToAttack = ai->getNextMoodTarget( !forceRetargetThisFrame, false );
		}
		if (nextObjectToAttack != nullptr)
		{
			m_attackFollowMachine->setGoalObject(nextObjectToAttack);
			m_attackFollowMachine->setState( AI_ATTACK_OBJECT );
			shouldRepathThisFrame = false;	// we're about to drop out of this function, but this is semantic emphasis.

			// we don't want an update to take place 'till next frame.
			return STATE_CONTINUE;
		}
	}

	if (shouldRepathThisFrame) 
	{
		// Update the goal waypoint if we've moved along the path.
		computeGoal(m_moveAsGroup);
		computePath();
	}

	return AIFollowWaypointPathState::update();
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackAimAtTargetState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackAimAtTargetState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferBool(&m_canTurnInPlace);
	xfer->xferBool(&m_setLocomotor);

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackAimAtTargetState::loadPostProcess( void )
{
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackAimAtTargetState::onEnter()
{
	// contained by AIAttackState, so no separate timer
	Object* source = getMachineOwner();
	Weapon* weapon = source->getCurrentWeapon();
	Object* victim = getMachineGoalObject();
	const Coord3D* targetPos = getMachineGoalPosition();
	AIUpdateInterface* sourceAI = source->getAI();
	AIUpdateInterface* victimAI = victim ? victim->getAI() : nullptr;

	Locomotor* curLoco = sourceAI->getCurLocomotor();
	m_canTurnInPlace = curLoco ? curLoco->getMinSpeed() == 0.0f : false;


//	if (!victim) 
//		return STATE_CONTINUE; // Just continue till we get a victim.
// Ick.  This was originally a safety to a single line that required victim, and was never meant
// as an early return to all cases.  We now want to use preattack frames on ground position targets

	Bool inFiringRange = FALSE;

	//If the object is garrisoning a building, then we want to force the object
	//to move to the best fire point, check if it's in firing range, and if not
	//move it back so another unit with longer range can!
	Object *containedBy = source->getContainedBy();
	ContainModuleInterface *contain = containedBy ? containedBy->getContain() : nullptr;
  
	if( containedBy && weapon && contain && contain->isEnclosingContainerFor( source ) )
	{                                          // non enclosing garrison containers do not use firepoints. Lorenzen, 6/11/03
		if (victim)
		{
			inFiringRange = contain->attemptBestFirePointPosition( source, weapon, victim );
		}
		else
		{
			inFiringRange = contain->attemptBestFirePointPosition( source, weapon, targetPos );
		}
	}
	else if( victim && weapon )
		inFiringRange = weapon->isWithinAttackRange( source, victim );
	else if( weapon )
		inFiringRange = weapon->isWithinAttackRange( source, targetPos );//See, I can be attacking the ground.  Perfectly valid.
	else
		return STATE_FAILURE; // can't happen.

	// add ourself as a targeter BEFORE calling isTemporarilyPreventingAimSuccess().
	if (victimAI)
		victimAI->addTargeter(source->getID(), true);
	
	if( sourceAI->areTurretsLinked() )
	{
		//Order all turrets to attack.
		for( Int i = 0; i < MAX_TURRETS; i++ )
		{
			if( m_isAttackingObject )
			{
				sourceAI->setTurretTargetObject( static_cast<WhichTurretType>(i), victim, m_isForceAttacking );
			}
			else
			{
				sourceAI->setTurretTargetPosition( static_cast<WhichTurretType>(i), getMachineGoalPosition() );
			}
		}
	}
	else
	{
		WhichTurretType tur = sourceAI->getWhichTurretForCurWeapon();
		if (tur != TURRET_INVALID)
		{
			//Order specific turret to attack.
			if (m_isAttackingObject)
			{
				sourceAI->setTurretTargetObject(tur, victim, m_isForceAttacking);
			}
			else
			{
				sourceAI->setTurretTargetPosition(tur, getMachineGoalPosition());
			}
		}
		else
		{
			// GS moved contact weapon check in here, because Success can never be given to a unit in this state 
			// using a turret to attack.  Check out ::update and you will see.

			Bool preventing = victimAI && victimAI->isTemporarilyPreventingAimSuccess();

			// Contact weapons don't aim.  They just go boom.  jba.
			if( weapon && weapon->isContactWeapon() && inFiringRange && !preventing ) 
				return STATE_SUCCESS;
		}
	}
	m_setLocomotor = false;

	source->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_IS_AIMING_WEAPON ) );
	return STATE_CONTINUE;
}

StateReturnType AIAttackAimAtTargetState::update()
{
	// contained by AIAttackState, so no separate timer
	Object* source = getMachineOwner();
	AIUpdateInterface* sourceAI = source->getAI();

	if (!source->hasAnyWeapon())
		return STATE_FAILURE;

	Object* victim = getMachineGoalObject();
	if (m_isAttackingObject)
	{
		if (!victim || victim->isEffectivelyDead())
			return STATE_FAILURE;	// can't aim at dead things
	}

	WhichTurretType tur = sourceAI->getWhichTurretForCurWeapon();
	if (tur != TURRET_INVALID)
	{
		if (m_isAttackingObject)
		{
			sourceAI->setTurretTargetObject(tur, victim, m_isForceAttacking);
		}
		else
		{
			sourceAI->setTurretTargetPosition(tur, getMachineGoalPosition());
		}
		// if we have a turret, but it is incapable of turning, turn ourself.
		// (gotta do this for units like the Comanche, which have fake "turrets"
		// solely to allow for attacking-on-the-move...)
		if (sourceAI->getTurretTurnRate(tur) != 0.0f)	
		{
			// The Body can never return Success if the weapon is on the turret, or else we end
			// up shooting the current weapon (which is on the turret) in the wrong direction.
			// We always say Continue, so the Turret can do its own Aiming state.
//			if (m_isAttackingObject && source->canCrushOrSquish(victim)) {
//				return STATE_SUCCESS;
//			}
			return STATE_CONTINUE;
		}

		// else fall thru!
	}
	
	// no else here!
	{
		Real relAngle = m_isAttackingObject ?
											ThePartitionManager->getRelativeAngle2D( source, victim ) : 
											ThePartitionManager->getRelativeAngle2D( source, getMachineGoalPosition() );

		const Real REL_THRESH = 0.035f;	// about 2 degrees. (getRelativeAngle2D is current only accurate to about 1.25 degrees)

		Weapon* weapon = source->getCurrentWeapon();
		Real aimDelta = weapon ? weapon->getAimDelta() : 0.0f;
		
		if (aimDelta < REL_THRESH) 
		{
			aimDelta = REL_THRESH;
		}

		//DEBUG_LOG(("AIM: desired %f, actual %f, delta %f, aimDelta %f, goalpos %f %f\n",rad2deg(obj->getOrientation() + relAngle),rad2deg(obj->getOrientation()),rad2deg(relAngle),rad2deg(aimDelta),victim->getPosition()->x,victim->getPosition()->y));
		if (m_canTurnInPlace)
		{
			if (fabs(relAngle) > aimDelta) 
			{
				Real desiredAngle = source->getOrientation() + relAngle;
				sourceAI->setLocomotorGoalOrientation(desiredAngle);
				m_setLocomotor = true;
			}
		}
		else
		{
			sourceAI->setLocomotorGoalPositionExplicit(m_isAttackingObject ? *victim->getPosition() : *getMachineGoalPosition());
		}

		if (fabs(relAngle) < aimDelta /*&& !m_preAttackFrames*/ )
		{
			AIUpdateInterface* victimAI = victim ? victim->getAI() : nullptr;
			// add ourself as a targeter BEFORE calling isTemporarilyPreventingAimSuccess().
			// we do this every time thru, just in case we get into a squabble with our turret-ai
			// over whether or not we are a targeter... (srj)
			if (victimAI)
				victimAI->addTargeter(source->getID(), true);
			Bool preventing = victimAI && victimAI->isTemporarilyPreventingAimSuccess();
			if (preventing)
			{
				return STATE_CONTINUE;
			}
			else
			{
				return STATE_SUCCESS;
			}
		}
	}

	if (source->isDisabledByType(DISABLED_HELD))
	{
		// We are contained by something (transport, building). This means we can't 
		// actually move to the location and if we are firing on a specific target
		// and that target is no longer in range, then we need to abort the state
		// so it can fire on other targets.

		// Are we still in range?
		Weapon *weapon = source->getCurrentWeapon();

		// NO BAD WRONG!!! How can this be the one spot to convert an Object to a center pos?  We have
		// an attack object check for a reason!  The center and the edge can be very far apart.
//		const Coord3D *pos = m_isAttackingObject ? victim->getPosition() : getMachineGoalPosition();
		
		Bool inRange = FALSE;
		if( m_isAttackingObject )
			inRange = weapon ? weapon->isWithinAttackRange(source, victim) : FALSE;
		else
			inRange = weapon ? weapon->isWithinAttackRange(source, getMachineGoalPosition()) : FALSE;

		if( !weapon || !inRange )
		{
			// We're no longer in range, so exit with failure so we can automatically 
			// reacquire a closer target if possible.
			return STATE_FAILURE;
		}
	}

	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------------------
void AIAttackAimAtTargetState::onExit( StateExitType status )
{
	// contained by AIAttackState, so no separate timer
	if (m_canTurnInPlace)
	{
		AIUpdateInterface* sourceAI = getMachineOwner()->getAI();
		// Tell the ai we are done moving, if we set the locomotor goal.
		if (sourceAI && m_setLocomotor) 
			sourceAI->setLocomotorGoalNone();
	}
	else
	{
		// don't do the loco call, or else we will "wiggle"... we already have an appropriate goal
	}

	getMachineOwner()->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_IS_AIMING_WEAPON ) );

	//getMachineOwner()->clearModelConditionState( MODELCONDITION_PREATTACK );
}

//----------------------------------------------------------------------------------------------------------
/**
 * Start firing.
 */
StateReturnType AIAttackFireWeaponState::onEnter()
{
	// contained by AIAttackState, so no separate timer
	DEBUG_ASSERTCRASH(m_att != nullptr, ("m_att may not be null"));

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	// Passive stuff will approach but not attack, so we check here (after approach is complete)
	UnsignedInt adjust = ai->getMoodMatrixActionAdjustment(MoodMatrixAction::MM_Action_Attack);
	if ((adjust & MAA_Action_Ok) == 0) 
	{
		return STATE_FAILURE;
	}
	Object *victim = getMachineGoalObject();

	if (victim && obj->getTeam()->getPrototype()->getTemplateInfo()->m_attackCommonTarget) {
		if (obj->getTeam()->getTeamTargetObject()==nullptr) {
			obj->getTeam()->setTeamTargetObject(victim);
		}
	}

	obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_IS_FIRING_WEAPON ) );
	obj->preFireCurrentWeapon( getMachineGoalObject() );
	return STATE_CONTINUE;	
}

StateReturnType AIAttackFireWeaponState::update()
{
	// contained by AIAttackState, so no separate timer

	Object *obj = getMachineOwner();
	Object* victim = getMachineGoalObject();

	if (!m_att)
		return STATE_FAILURE;

	if (m_att->isAttackingObject())
	{
		// if our target is dead, go ahead and stop.
		if (!victim || victim->isEffectivelyDead())
			return STATE_FAILURE;
	}
	WeaponSlotType wslot;
	Weapon* weapon = obj->getCurrentWeapon(&wslot);
	if (!weapon)
	{
		return STATE_FAILURE;
	}
	
	WeaponStatus status = weapon->getStatus();
	if (status == PRE_ATTACK)
	{
		return STATE_CONTINUE;
	} 
	else if (status != READY_TO_FIRE)
	{
		return STATE_FAILURE;
	}

	/**
		this is the weird case where we have multi turrets, and turret 'a' wants
		to fire, but someone has changed the current weapon to be one not on him.
		rather than addressing the situation, we just punt and wait for it to clear
		up on its own.
	*/
	if (m_att && !m_att->isWeaponSlotOkToFire(wslot))
	{
		return STATE_FAILURE;
	}

	// must adjust the state BEFORE calling fireWeapon, for FX to work correctly...
	obj->setFiringConditionForCurrentWeapon();

	if (m_att->isAttackingObject())
	{
    // Since it is very late in the project, and there is no call for such code...
    // there is currently no support here for linked turrets, as regards Attacking Objects (victims)
    // If the concept of linked turrets is further developed then God help you, and put more code right here
    // that lookl like the //LINKED TURRETS// block, below


		obj->fireCurrentWeapon(victim);

		//Kris: October 21, 2003 - Patch 1.01
		//Fixes cases where some units couldn't transfer their attack to a different object. One example was Colonel Burton attacking
		//any GLA structure. When the structure was destroyed becoming a hole, Burton would stop attacking. Even though there is code
		//to transfer attackers (AIUpdateInterface::transferAttack), it is unable to modify our current victim in our attack state
		//machine. When we move immediately to the aim state in the same frame as the transfer (after this call in fact), the victim
		//was still pointing to the building and not the hole we transferred to. This code fixes that.
		if( victim != obj->getAI()->getCurrentVictim() )
		{
			getMachine()->setGoalObject( obj->getAI()->getCurrentVictim() );
		}

		// clear this, just in case.
		obj->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_IGNORING_STEALTH ) );
		Real continueRange = weapon->getContinueAttackRange();
		if (
			continueRange > 0.0f &&
			victim && 
			(victim->isDestroyed() || victim->isEffectivelyDead() || (victim->isKindOf(KINDOF_MINE) && victim->testStatus(OBJECT_STATUS_MASKED)))
		)
		{
			const Coord3D* originalVictimPos = m_att ? m_att->getOriginalVictimPos() : nullptr;
			if (originalVictimPos)
			{
				// note that it is important to use getLastCommandSource here; this allows
				// dozers that were ordered to clear mines by the human to continue to autoacquire,
				// but not if they were ordered by ai.
				AIUpdateInterface* ai = obj->getAI();
				CommandSourceType lastCmdSource = ai ? ai->getLastCommandSource() : CMD_FROM_AI;
				PartitionFilterSamePlayer filterPlayer( victim->getControllingPlayer() );
				PartitionFilterSameMapStatus filterMapStatus(obj);
				PartitionFilterPossibleToAttack filterAttack(ATTACK_NEW_TARGET, obj, lastCmdSource);
				PartitionFilter *filters[] = { &filterAttack, &filterPlayer, &filterMapStatus, nullptr };
				// note that we look around originalVictimPos, *not* the current victim's pos.
				victim = ThePartitionManager->getClosestObject( originalVictimPos, continueRange, DistanceCalculationType::FROM_CENTER_2D, filters );// could be null. this is ok.
				if (victim)
				{
					getMachine()->setGoalObject(victim);
					m_att->notifyNewVictimChosen(victim);
				}
			}
		}
	}
	else
	{
    
    if( getMachineOwner()->getAI()->areTurretsLinked() ) //LINKED TURRETS
    {// it doesn;t matter which weapon slot is locked, current or whatever
      for ( Int slot = PRIMARY_WEAPON; slot < WEAPONSLOT_COUNT ; slot++ )
      {// were firing with all barrels
        Weapon *weapon2 = obj->getWeaponInWeaponSlot( static_cast<WeaponSlotType>(slot) );
        if ( weapon2 )
        {
          if ( weapon2->fireWeapon(obj, getMachineGoalPosition()) ) //fire() returns 'reloaded'
            obj->releaseWeaponLock(LOCKED_TEMPORARILY);// unlock, 'cause we're loaded

	  	    obj->notifyFiringTrackerShotFired(weapon2, INVALID_ID);
        }
      }
    }
    else
      obj->fireCurrentWeapon(getMachineGoalPosition());
    // clear this, just in case.
    obj->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_IGNORING_STEALTH ) );
	}
		
	m_att->notifyFired();

	return STATE_SUCCESS;
}

//----------------------------------------------------------------------------------------------------------
/** 
	Stop firing.
	*/
void AIAttackFireWeaponState::onExit( StateExitType status )
{
	// contained by AIAttackState, so no separate timer
	Object *obj = getMachineOwner();
	obj->clearStatus( MAKE_OBJECT_STATUS_MASK2( OBJECT_STATUS_IS_FIRING_WEAPON, OBJECT_STATUS_IGNORING_STEALTH ) );

	// this can occur if we start a preattack (eg, bayonet)
	// and the target moves out range before we can actually "fire"...
	// leaving us thinking we're still "pre attacking". cancel this state
	// to avoid confusion. (srj)
	Weapon* weapon = obj->getCurrentWeapon();
	if (weapon && weapon->getStatus() == PRE_ATTACK)
	{
		weapon->setPreAttackFinishedFrame(0);
	}
}

//----------------------------------------------------------------------------------------------------------
AIAttackState::AIAttackState( StateMachine *machine, Bool follow, Bool attackingObject, Bool forceAttacking, AttackExitConditionsInterface* attackParameters) : 
	State( machine , "AIAttackState"), 
	m_attackMachine(nullptr),
	m_runningMachine(nullptr),
	m_zombieMachine(nullptr),
	m_attackParameters(attackParameters), 
	m_lockedWeaponOnEnter(nullptr), 
	m_follow(follow),
	m_isAttackingObject(attackingObject),
	m_isForceAttacking(forceAttacking),
	m_victimTeam( nullptr )
{
	m_originalVictimPos.zero();
#ifdef STATE_MACHINE_DEBUG
	if (machine->getWantsDebugOutput()) {
		DEBUG_LOG(("Creating attack state follow %d, attacking object %d, force attacking %d\n", 
			follow, attackingObject, forceAttacking));
	}
#endif
}

//----------------------------------------------------------------------------------------------------------
AIAttackState::~AIAttackState()
{
	// nope, don't do this, since we may well still have it targeted
	// even though we're leaving this state.
	// turn it off when we do setCurrentVictim(NULL).
	//addSelfAsTargeter(false);

	if (m_attackMachine) 
	{
		m_attackMachine->halt();
		deleteInstanceOf(m_attackMachine);
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	Bool hasMachine = m_attackMachine!=nullptr;
	
	xfer->xferBool(&hasMachine);
	xfer->xferCoord3D(&m_originalVictimPos);

	if (hasMachine && m_attackMachine==nullptr)	{
		// create new state machine for attack behavior
		m_attackMachine = newInstance(AttackStateMachine)(getMachineOwner(), this, "AIAttackMachine", m_follow, m_isAttackingObject, m_isForceAttacking  );
	}
	if (hasMachine) {
		xfer->xferSnapshot(m_attackMachine);						///< state sub-machine for attack behavior
	}
	/* Not saved or loaded - passed in on creation.
	Bool										m_follow;
	Bool										m_isAttackingObject;								// if false, attacking position
	AttackExitConditionsInterface*	m_attackParameters;					///< these are not owned by this, and will not be deleted on destruction
	Bool										m_isForceAttacking
	*/
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackState::loadPostProcess( void )
{
	Object* victim = getMachineGoalObject();
	if (victim) 
	{
		m_victimTeam = victim->getTeam();
	}
	Object* source = getMachineOwner();
	m_lockedWeaponOnEnter = source->isCurWeaponLocked() ? source->getCurrentWeapon() : nullptr;
}  // end loadPostProcess

#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AIAttackState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if (m_attackMachine) name.concat(m_attackMachine->getCurrentStateName());
	else name.concat("*NULL m_attackMachine");
	return name;
}
#endif

//----------------------------------------------------------------------------------------------------------
Bool AIAttackState::chooseWeapon()
{
	Object* victim = getMachineGoalObject();
	if (m_isAttackingObject && !victim)
		return FALSE;

	Object* source = getMachineOwner();
	AIUpdateInterface *ai = source->getAI();

	Bool found = FALSE;
	if( TheGlobalData->m_weaponChoiceLikeGenerals )
	{
		// The Generals weapon choice fails without a target and against targets out of range
		// (WeaponSet::chooseBestWeaponForTarget), so choose only with a target and ignore the result;
		// otherwise every attack on the ground and every approach would abort.
		if( victim )
			source->chooseBestWeaponForTarget(victim, WeaponChoiceCriteria::PREFER_MOST_DAMAGE, ai->getLastCommandSource());
		found = TRUE;
	}
	else
	{
//	if (victim) // Pardon?  We still need to pick a weapon if we are attacking the ground.
//	{
		found = source->chooseBestWeaponForTarget(victim, WeaponChoiceCriteria::PREFER_MOST_DAMAGE, ai->getLastCommandSource());
		//DEBUG_ASSERTLOG(found, ("unable to autochoose any weapon for %s\n",source->getTemplate()->getName().str()));
//	}
	}

	// Check if we need to update because of the weapon choice switch.
	source->adjustModelConditionForWeaponStatus();

	return found;
}

//----------------------------------------------------------------------------------------------------------
void AIAttackState::notifyNewVictimChosen(Object* victim)
{
	// do NOT update m_originalVictimPos here. It's a new victim, not the original!
	getMachine()->setGoalObject(victim);
	if (m_attackMachine)
		m_attackMachine->setGoalObject(victim);
}

StateReturnType AIAttackState::onEnter()
{
	USE_PERF_TIMER(AIAttackState)
	//CRCDEBUG_LOG(("AIAttackState::onEnter() - start for object %d\n", getMachineOwner()->getID()));
	Object* source = getMachineOwner();
	AIUpdateInterface *ai = source->getAI();
	// if we are in sleep mode, we will not attack
	if ((ai->getMoodMatrixActionAdjustment(MoodMatrixAction::MM_Action_Attack) & MAA_Action_Ok) == 0)
		return STATE_SUCCESS;

	// if we've met the conditions specified by m_attackParameters, we consider ourselves "successful."
	if (m_attackParameters && m_attackParameters->shouldExit(getMachine())) 
		return STATE_SUCCESS;

	//Kris: Jan 12, 2005
	//Don't allow units under construction to attack! The selection/action manager system was responsible for preventing this
	//from ever happening, but failed in two cases which I fixed. This is an extra check to mitigate cheats.
	if( source->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
	{
		return STATE_FAILURE;
	}

	// if all of our weapons are out of ammo, can't attack.
	// (this can happen for units which never auto-reload, like the Raptor)
	if (source->isOutOfAmmo() && !source->isKindOf(KINDOF_PROJECTILE))
		return STATE_FAILURE;

	// create new state machine for attack behavior
	//CRCDEBUG_LOG(("AIAttackState::onEnter() - constructing state machine for object %d\n", getMachineOwner()->getID()));
	m_attackMachine = newInstance(AttackStateMachine)(source, this, "AIAttackMachine", m_follow, m_isAttackingObject, m_isForceAttacking  );

#ifdef STATE_MACHINE_DEBUG
	m_attackMachine->setDebugOutput(getMachine()->getWantsDebugOutput());
#endif
	// tell the attack machine who the victim of the attack is
	if (m_isAttackingObject)
	{
		Object* victim = getMachineGoalObject();
		if (victim == nullptr || victim->isEffectivelyDead())	
		{
			ai->notifyVictimIsDead();
			return STATE_FAILURE;	// we have nothing to attack!
		}
		m_victimTeam = victim->getTeam();
		m_attackMachine->setGoalObject( victim );
		m_originalVictimPos = *victim->getPosition();
	}
	else
	{
		m_attackMachine->setGoalPosition(getMachineGoalPosition());		
		m_originalVictimPos = *getMachineGoalPosition();
	}

	// Something can happen to make none of our weapons work.  Return failure, or we will start shooting
	// our Primary (default pick) regardless of legality.
	Bool weaponPicked = chooseWeapon();
	if( !weaponPicked )
		return STATE_FAILURE;

	Weapon* curWeapon = source->getCurrentWeapon();
	if (curWeapon)
	{
		curWeapon->setMaxShotCount(NO_MAX_SHOTS_LIMIT);
		// icky special case for ignoring stealth units we might be targeting, that are currently stealthed. (srj)
		if (curWeapon->getContinueAttackRange() > 0.0f)
			source->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_IGNORING_STEALTH ) );
	}

	m_lockedWeaponOnEnter = source->isCurWeaponLocked() ? curWeapon : nullptr;

	StateReturnType retType = m_attackMachine->initDefaultState();
	if( retType == STATE_CONTINUE )
	{
		source->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_IS_ATTACKING ) );
		source->setModelConditionState( MODELCONDITION_ATTACKING );
	}
	return retType;
}

StateReturnType AIAttackState::update()
{
	USE_PERF_TIMER(AIAttackState)
	// if we've met the conditions specified by m_attackParameters, we consider ourselves "successful."
	if (m_attackParameters && m_attackParameters->shouldExit(getMachine())) 
	{
		return STATE_SUCCESS;
	}

	Object* source = getMachineOwner();
	
	// if all of our weapons are out of ammo, can't attack.
	// (this can happen for units which never auto-reload, like the Raptor)
	if (source->isOutOfAmmo() && !source->isKindOf(KINDOF_PROJECTILE))
	{
		return STATE_FAILURE;
	}

	if (m_isAttackingObject)
	{
		Object* victim = getMachineGoalObject();

		if (victim == nullptr || victim->isEffectivelyDead()) 	
		{
			source->getAI()->notifyVictimIsDead();
			return STATE_SUCCESS;	// my, that was easy
		}

		if (victim) 
		{
			source->getAI()->setCurrentVictim(victim);
		}

		if( victim->getTeam() != m_victimTeam )
		{
			// If, while I have been attacking my victim, it has lost its ability to attack 
			//(a recently de-garrisoned building) I should bail here... 
			// We are not sure whether the problem occurs here or sometime before, but this is an edge case failsafe for it
			// Steven calls this hack 'greasy,' and I agreesy.-Lorenzen
			AIUpdateInterface *ai = source->getAI();
			if (ai)
			{
				if( !victim->getStatusBits().test( OBJECT_STATUS_CAN_ATTACK ) )
				{
					if ( victim->getContain() != nullptr )
					{
						if (victim->getContain()->isGarrisonable() && (victim->getContain()->getContainCount() == 0) )
						{
							if ( source->getRelationship( victim ) == NEUTRAL )
							{
								ai->friend_setGoalObject(nullptr);
								if (victim==source->getTeam()->getTeamTargetObject()) {
									source->getTeam()->setTeamTargetObject(nullptr);
								}
								ai->notifyVictimIsDead();	// well, not "dead", but longer attackable
								return STATE_FAILURE;
							}
						}
					}
				}
			}
			//The team of the victim has changed since we began attacking it. Evaluate
			//whether or not we should keep attacking it.
			// order matters: we want to know if I consider it to be an enemy, not vice versa
			if( source->getRelationship( victim ) != ENEMIES)
			{
				ai->friend_setGoalObject(nullptr);
				if (victim==source->getTeam()->getTeamTargetObject()) {
					source->getTeam()->setTeamTargetObject(nullptr);
				}
				ai->notifyVictimIsDead();	// well, not "dead", but longer attackable
				return STATE_FAILURE;
			}
		}	

		if (victim != m_attackMachine->getGoalObject()) 
		{
			// Our parent machine has changed the goal.  We need to reset to the new goal.  jba.
			m_attackMachine->setGoalObject( victim );
		}
	}

	// re-evaluate our weapon choice every frame, so the sub-states don't have to.

	// Something can happen to make none of our weapons work.  Return failure, or we will start shooting
	// our Primary (default pick) regardless of legality.
	Bool weaponPicked = chooseWeapon();
	if( !weaponPicked )
		return STATE_FAILURE;

	Weapon* curWeapon = source->getCurrentWeapon();

	// if we entered with a locked weapon (ie, a special weapon), then we will
	// only keep attacking as long as that weapon remains the cur weapon...
	// if anything ever changes that weapon, we exit attack mode immediately.
	if (m_lockedWeaponOnEnter != nullptr && m_lockedWeaponOnEnter != curWeapon)
		return STATE_FAILURE;

	// we've shot as many times as we are allowed to
	if (curWeapon == nullptr || curWeapon->getMaxShotCount() <= 0)
		return STATE_FAILURE;

	/**
	 * Run the attack state sub-machine.
	 * If the attack state machine returns anything other than CONTINUE,
	 * it has finished. Propagating the return code will cause
	 * the containing state machine to do the right thing.
	 * Note the use of CONVERT_SLEEP_TO_CONTINUE; even if the sub-machine
	 * sleeps, we still need to be called every frame.
	 */
	// The attack machine can be torn down while it runs: a shot that damages a garrisoned building orders its
	// passengers out, and one of them may be this unit (its AI is cleared through onExit). onExit then parks
	// the machine in m_zombieMachine instead of freeing it under its own feet.
	AttackStateMachine *const running = m_attackMachine;
	m_runningMachine = running;
	const StateReturnType status = running->updateStateMachine();
	m_runningMachine = nullptr;
	if (m_zombieMachine == running)
	{
		deleteInstanceOf(m_zombieMachine);
		m_zombieMachine = nullptr;
		return STATE_FAILURE;	// this state has been exited meanwhile
	}
	return CONVERT_SLEEP_TO_CONTINUE(status);
}

//----------------------------------------------------------------------------------------------------------
void AIAttackState::onExit( StateExitType status )
{
	USE_PERF_TIMER(AIAttackState)
	// nope, don't do this, since we may well still have it targeted
	// even though we're leaving this state. turn it off when we
	// turn it off when we do setCurrentVictim(NULL).
	//addSelfAsTargeter(false);

	// destroy the attack machine (not while it is running, see update)
	if (m_attackMachine)
	{
		if (m_attackMachine == m_runningMachine)
		{
			m_attackMachine->halt();
			m_zombieMachine = m_attackMachine;
		}
		else
		{
			deleteInstanceOf(m_attackMachine);
		}
		m_attackMachine = nullptr;
	}

	Object *obj = getMachineOwner();
	obj->clearStatus( MAKE_OBJECT_STATUS_MASK4( OBJECT_STATUS_IS_FIRING_WEAPON, 
																							OBJECT_STATUS_IS_AIMING_WEAPON, 
																							OBJECT_STATUS_IS_ATTACKING, 
																							OBJECT_STATUS_IGNORING_STEALTH ) );
	obj->clearModelConditionState( MODELCONDITION_ATTACKING );

	obj->clearLeechRangeModeForAllWeapons();

	AIUpdateInterface *ai = obj->getAI();
	if (ai) 
	{	
		//ai->notifyVictimIsDead();	no, do NOT do this here.
		ai->setCurrentVictim(nullptr);
		for (int i = 0; i < MAX_TURRETS; ++i)
			ai->setTurretTargetObject(static_cast<WhichTurretType>(i), NULL, NULL);
		ai->friend_setGoalObject(nullptr);
	}
}

//-----------------------------------------------------------------------------------------------------------
class AIAttackThenIdleStateMachine : public StateMachine
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AIAttackThenIdleStateMachine, "AIAttackThenIdleStateMachine" );

public:

	AIAttackThenIdleStateMachine( Object *owner, AsciiString name );
protected:
	// snapshot interface .
	virtual void crc( Xfer *xfer ) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess() override;
};

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackThenIdleStateMachine::crc( Xfer *xfer )
{
	StateMachine::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackThenIdleStateMachine::xfer( Xfer *xfer )
{
	XferVersion cv = 1;	
	XferVersion v = cv; 
	xfer->xferVersion( &v, cv );

	StateMachine::xfer(xfer);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackThenIdleStateMachine::loadPostProcess( void )
{
	StateMachine::loadPostProcess();
}  // end loadPostProcess

//-----------------------------------------------------------------------------------------------------------
AIAttackThenIdleStateMachine::AIAttackThenIdleStateMachine(Object *owner, AsciiString name) : StateMachine(owner, name)
{
	// order matters: first state is the default state.
	defineState( AI_ATTACK_OBJECT,	newInstance(AIAttackState)(this, false, true, false, nullptr ), AI_IDLE, AI_IDLE );
	defineState( AI_PICK_UP_CRATE, newInstance(AIPickUpCrateState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_IDLE, newInstance(AIIdleState)( this, AIIdleState::AIIdleTargetingType::DO_NOT_LOOK_FOR_TARGETS ), AI_IDLE, AI_IDLE );
}

//----------------------------------------------------------------------------------------------------------
AIAttackThenIdleStateMachine::~AIAttackThenIdleStateMachine()
{
}

//----------------------------------------------------------------------------------------------------------
AIAttackSquadState::~AIAttackSquadState()
{
	if (m_attackSquadMachine)	{
		m_attackSquadMachine->halt();
		deleteInstanceOf(m_attackSquadMachine);
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackSquadState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackSquadState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );
	
	Bool hasMachine = m_attackSquadMachine!=nullptr;
	
	xfer->xferBool(&hasMachine);

	if (hasMachine && m_attackSquadMachine==nullptr)	{
		// create new state machine for attack behavior
		m_attackSquadMachine = newInstance(AIAttackThenIdleStateMachine)( getMachineOwner(), "AIAttackMachine"  );
	}

	if (hasMachine) {
		xfer->xferSnapshot(m_attackSquadMachine);
	}
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackSquadState::loadPostProcess( void )
{
}  // end loadPostProcess

#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AIAttackSquadState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if (m_attackSquadMachine) name.concat(m_attackSquadMachine->getCurrentStateName());
	else name.concat("*NULL m_attackSquadMachine");
	return name;
}
#endif

//----------------------------------------------------------------------------------------------------------
/**
 * Begin an attack on the machine's goal team.
 * To do this, we use a sub attack machine.
 */
StateReturnType AIAttackSquadState::onEnter( void )
{
	// create new state machine for attack behavior
	m_attackSquadMachine = newInstance(AIAttackThenIdleStateMachine)( getMachineOwner(), "AIAttackMachine"  );
	
	Object *victim = chooseVictim();
	// tell the attack machine who the victim of the attack is
	m_attackSquadMachine->setGoalObject( victim );

	// initial state of attack state machine
	return m_attackSquadMachine->initDefaultState();
}

StateReturnType AIAttackSquadState::update( void )
{

	if( !m_attackSquadMachine )
	{
		return STATE_FAILURE;
	}

	/* 
		Note the use of CONVERT_SLEEP_TO_CONTINUE; even if the sub-machine
		sleeps, we still need to be called every frame.
	*/
	StateReturnType attackStatus = CONVERT_SLEEP_TO_CONTINUE(m_attackSquadMachine->updateStateMachine());

	// if we're in attack state, 
	if (m_attackSquadMachine->getCurrentStateID() != AI_IDLE) 
	{
		return attackStatus;
	}

	// Check to see if we have created a crate we need to pick up.
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	Object* crate = ai->checkForCrateToPickup();
	if (crate)
	{
		m_attackSquadMachine->setGoalObject(crate);
		m_attackSquadMachine->setState( AI_PICK_UP_CRATE );
		return STATE_CONTINUE;
	}

	// choose a new target and start over.
	Object *victim = chooseVictim();
	if (!victim) 
	{
		return STATE_SUCCESS;
	}

	m_attackSquadMachine->setGoalObject( victim );
	m_attackSquadMachine->setState(AI_ATTACK_OBJECT);
	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------------------
void AIAttackSquadState::onExit( StateExitType status )
{
	if( m_attackSquadMachine )
	{
		// destroy the attack machine
		deleteInstanceOf(m_attackSquadMachine);
		m_attackSquadMachine = nullptr;
	}
}

//----------------------------------------------------------------------------------------------------------
Object *AIAttackSquadState::chooseVictim(void)
{
	Squad *victimSquad = (static_cast<AIStateMachine*>(getMachine()))->getGoalSquad();
	if (!victimSquad) 
	{
		return nullptr;
	}

	Object *owner = getMachineOwner();
	AIUpdateInterface *ai = owner->getAI();
	UnsignedInt moodVal = ai->getMoodMatrixValue();
	
	if (moodVal & MM_Controller_AI) 
	{
		if (moodVal & MM_Mood_Sleep) 
		{
			return nullptr;
		}

		if (moodVal & MM_Mood_Passive) 
		{
			BodyModuleInterface *bmi = owner->getBodyModule();
			if (!bmi) {
				return nullptr;
			}
			
			const DamageInfo *di = bmi->getLastDamageInfo();
			if (!di) 
			{
				return nullptr;
			}

			return TheGameLogic->findObjectByID(di->in.m_sourceID);
		}
	}

	GameDifficulty difficulty = owner->getControllingPlayer()->getPlayerDifficulty();
	const CommandSourceType cmdSource = ai->getLastCommandSource();
	
	if (cmdSource == CMD_FROM_PLAYER) 
	{
		// if a player did this, we want to always give them the Seek and obliterate method.
		difficulty = DIFFICULTY_HARD;
	}

	if (TheScriptEngine->getChooseVictimAlwaysUsesNormal())
	{
		difficulty = DIFFICULTY_NORMAL;
	}	

	switch(difficulty)
	{
		case DIFFICULTY_EASY:
		{
			// pick a random unit
			VecObjectPtr objects = victimSquad->getLiveObjects();
			Int numUnits = static_cast<Int>(objects.size());
			if (numUnits == 0) 
			{
				return nullptr;
			}

			Int unitToAttack = GameLogicRandomValue(0, numUnits - 1);
			return objects[unitToAttack];
			break;
		}

		case DIFFICULTY_NORMAL:
		{
			// pick the closest unit

			PartitionFilterAcceptOnSquad f1(victimSquad);
			PartitionFilterSameMapStatus filterMapStatus(getMachineOwner());
			PartitionFilter *filters[] = { &f1, &filterMapStatus, nullptr };
			
			Object *victim = ThePartitionManager->getClosestObject(getMachineOwner(), HUGE_DIST, DistanceCalculationType::FROM_CENTER_2D, filters, nullptr, nullptr);
			return victim;
			break;
		}

		case DIFFICULTY_HARD:
		{
			// everyone picks the same unit
			VecObjectPtr objects = victimSquad->getLiveObjects();
			if (objects.size() > 0) 
			{
				return objects[0];
			}

			return nullptr;
			break;
		}
	};

	return nullptr;
}

//----------------------------------------------------------------------------------------------------------
AIHuntState::~AIHuntState()
{
	if (m_huntMachine) 
	{
		m_huntMachine->halt();
		deleteInstanceOf(m_huntMachine);
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIHuntState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIHuntState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	Bool hasMachine = m_huntMachine!=nullptr;
	
	xfer->xferBool(&hasMachine);

	if (hasMachine && m_huntMachine==nullptr)	{
		// create new state machine for hunt behavior
		m_huntMachine = newInstance(AIAttackThenIdleStateMachine)( getMachineOwner(), "AIAttackThenIdleStateMachine");
	}
	if (hasMachine) {
		xfer->xferSnapshot(m_huntMachine);
	}
	xfer->xferUnsignedInt(&m_nextEnemyScanTime);

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIHuntState::loadPostProcess( void )
{
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
//Is our hunt state in an attack sub-state?
Bool AIHuntState::isAttack() const
{
	if( m_huntMachine )
	{
		return m_huntMachine->isInAttackState();
	}
	return FALSE;
}

StateReturnType AIHuntState::onEnter()
{
	// create new state machine for hunt behavior
	m_huntMachine = newInstance(AIAttackThenIdleStateMachine)( getMachineOwner(), "AIAttackThenIdleStateMachine");

	// first time thru, use a random amount so that everyone doesn't scan on the same frame,
	// to avoid "spikes". 
	UnsignedInt sleepTime = GameLogicRandomValue(0, ENEMY_SCAN_RATE);
	UnsignedInt now = TheGameLogic->getFrame();
	m_nextEnemyScanTime = now + sleepTime;

	// initial state of hunt state machine
	return m_huntMachine->initDefaultState();
}

//----------------------------------------------------------------------------------------------------------
void AIHuntState::onExit( StateExitType status )
{
	// destroy the hunt machine
	deleteInstanceOf(m_huntMachine);
	m_huntMachine = nullptr;

	Object *obj = getMachineOwner();
	if (obj) 
	{
		obj->releaseWeaponLock(LOCKED_TEMPORARILY);	// release any temporary locks.
	}
}

#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AIHuntState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if (m_huntMachine) name.concat(m_huntMachine->getCurrentStateName());
	else name.concat("*NULL huntMachine");
	return name;
}
#endif

//----------------------------------------------------------------------------------------------------------
StateReturnType AIHuntState::update()
{

	// look around for better victims every so often
	UnsignedInt now = TheGameLogic->getFrame();
	if (now >= m_nextEnemyScanTime)
	{
		Object* owner = getMachineOwner();

		// if all of our weapons are out of ammo, can't hunt.
		// (this can happen for units which never auto-reload, like the Raptor)
		if (owner->isOutOfAmmo() && !owner->isKindOf(KINDOF_PROJECTILE))
			return STATE_FAILURE;

		// Check to see if we have created a crate we need to pick up.
		AIUpdateInterface *ai = owner->getAI();
		Object* crate = ai->checkForCrateToPickup();
		if (crate)
		{
			m_huntMachine->setGoalObject(crate);
			m_huntMachine->setState( AI_PICK_UP_CRATE );
			return STATE_CONTINUE;
		}

		m_nextEnemyScanTime = now + ENEMY_SCAN_RATE;

		const AttackPriorityInfo *info = nullptr;
		info = ai->getAttackInfo();

		// Check if team auto targets same victim.
		Object* teamVictim = nullptr;
		if (owner->getTeam()->getPrototype()->getTemplateInfo()->m_attackCommonTarget) 
		{
			teamVictim = owner->getTeam()->getTeamTargetObject();
		}
		Object* victim = nullptr;
		if (teamVictim && info==nullptr) 
		{
			victim = teamVictim;
		} 
		else 
		{
			// do NOT do line of sight check - we want to find everything
			victim = TheAI->findClosestEnemy( owner, 9999.9f, AI::CAN_ATTACK, info );
			if (victim==nullptr && owner->getControllingPlayer() && owner->getControllingPlayer()->getUnitsShouldHunt()) {
				// If we are doing an all hunt, try hunting without the attack priority info. jba.
				victim = TheAI->findClosestEnemy(owner, 9999.9f, AI::CAN_ATTACK, nullptr);
			}
			if (owner->getTeam()->getPrototype()->getTemplateInfo()->m_attackCommonTarget) 
			{
				// Check priorities.
				if (teamVictim && info) {
					if (victim==nullptr) {
						DEBUG_LOG(("Couldnt' find victim. hmm."));
						victim = teamVictim;
					}
					Int teamVictimPriority = info->getPriority(teamVictim->getTemplate());
					Int victimPriority;
					if( victim )
						victimPriority = info->getPriority(victim->getTemplate());
					else
						victimPriority = 0;

					if (teamVictimPriority>=victimPriority) {
						victim = teamVictim;
					}
				}
				owner->getTeam()->setTeamTargetObject(victim);
			}
		}
		m_huntMachine->setGoalObject( victim );

		if (m_huntMachine->getCurrentStateID() == AI_IDLE && victim)	
		{
			m_huntMachine->setState( AI_ATTACK_OBJECT );
		}
		if (owner->getControllingPlayer() && owner->getControllingPlayer()->getUnitsShouldHunt()==FALSE) {
			// If we are not doing an all hunt, then exit hunt state - no more victims.
			if (m_huntMachine->getCurrentStateID() == AI_IDLE && victim==nullptr) {
				return STATE_SUCCESS; // we killed everything :) jba.
			}
		}
	}

	getMachine()->lock("AIHuntState::update");	// The idle state in the sub machine can sometimes acquire targets. 
												// It is important to not switch out of this state via a sub machine call. jba.
	/* 
		Note the use of CONVERT_SLEEP_TO_CONTINUE; even if the sub-machine
		sleeps, we still need to be called every frame.
	*/
			/// @todo srj -- find a way to sleep for a number of frames here, if possible
	StateReturnType ret = CONVERT_SLEEP_TO_CONTINUE(m_huntMachine->updateStateMachine());
	getMachine()->unlock();
	return ret;
}

//----------------------------------------------------------------------------------------------------------
AIAttackAreaState::~AIAttackAreaState()
{
	if (m_attackMachine) {
		m_attackMachine->halt();
		deleteInstanceOf(m_attackMachine);
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackAreaState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackAreaState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	Bool hasMachine = m_attackMachine!=nullptr;
	
	xfer->xferBool(&hasMachine);

	if (hasMachine && m_attackMachine==nullptr)	{
		// create new state machine for hunt behavior
		m_attackMachine = newInstance(AIAttackThenIdleStateMachine)( getMachineOwner(), "AIAttackThenIdleStateMachine");
	}
	if (hasMachine) {
		xfer->xferSnapshot(m_attackMachine);
	}
	xfer->xferUnsignedInt(&m_nextEnemyScanTime);

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackAreaState::loadPostProcess( void )
{
}  // end loadPostProcess

#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AIAttackAreaState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if (m_attackMachine) name.concat(m_attackMachine->getCurrentStateName());
	else name.concat("*NULL m_attackMachine");
	return name;
}
#endif

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackAreaState::onEnter()
{

	// create new state machine for hunt behavior
	m_attackMachine = newInstance(AIAttackThenIdleStateMachine)( getMachineOwner(), "AIAttackThenIdleStateMachine");

	// first time thru, use a random amount so that everyone doesn't scan on the same frame,
	// to avoid "spikes". 
	UnsignedInt now = TheGameLogic->getFrame();
	m_nextEnemyScanTime = now + GameLogicRandomValue(0, ENEMY_SCAN_RATE);

	// initial state of hunt state machine
	return m_attackMachine->initDefaultState();
}

//----------------------------------------------------------------------------------------------------------
void AIAttackAreaState::onExit( StateExitType status )
{
	// destroy the hunt machine
	deleteInstanceOf(m_attackMachine);
	m_attackMachine = nullptr;
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackAreaState::update()
{
	// look around for better victims every so often
	UnsignedInt now = TheGameLogic->getFrame();
	if (now >= m_nextEnemyScanTime)
	{
		Object* owner = getMachineOwner();

		// if all of our weapons are out of ammo, can't hunt.
		// (this can happen for units which never auto-reload, like the Raptor)
		if (owner->isOutOfAmmo() && !owner->isKindOf(KINDOF_PROJECTILE))
			return STATE_FAILURE;

		// first time thru, add a random amount so that everyone doesn't scan on the same frame,
		// to avoid "spikes". Note that this implementation ensures that this unit checks immediately
		// upon entering this state, then wait a possibly-longer-than-usual time (due to randomness),
		// then settle into a regular schedule.
		m_nextEnemyScanTime = now + ENEMY_SCAN_RATE;

		AIUpdateInterface *ai = owner->getAI();
		if (ai->getAreaToGuard() == nullptr) 
			return STATE_FAILURE;

		const AttackPriorityInfo *info = nullptr;
		info = ai->getAttackInfo();
		PartitionFilterPolygonTrigger polyFilter(ai->getAreaToGuard());

		// do NOT do line of sight check - we want to find everything
		Object *victim = TheAI->findClosestEnemy( owner, 9999.9f, AI::CAN_ATTACK, info, &polyFilter );
		m_attackMachine->setGoalObject( victim );

		if (m_attackMachine->getCurrentStateID() == AI_IDLE && victim)	
		{
			m_attackMachine->setState( AI_ATTACK_OBJECT );
		}
		if (victim==nullptr) {
			return STATE_SUCCESS;
		}
	}

	getMachine()->lock("AIAttackAreaState::update");	// The idle state in the sub machine can sometimes acquire targets. 
												// It is important to not switch out of this state via a sub machine call. jba.
	/* 
		Note the use of CONVERT_SLEEP_TO_CONTINUE; even if the sub-machine
		sleeps, we still need to be called every frame.
	*/
			/// @todo srj -- find a way to sleep for a number of frames here, if possible
	StateReturnType ret = CONVERT_SLEEP_TO_CONTINUE(m_attackMachine->updateStateMachine());
	getMachine()->unlock();
	return ret;
}
