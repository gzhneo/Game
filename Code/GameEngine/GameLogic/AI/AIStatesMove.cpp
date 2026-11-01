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

// Movement states: move to, follow paths and waypoints, wander, panic

//----------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIInternalMoveToState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIInternalMoveToState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );
 
 // extend base class
	xfer->xferCoord3D(&m_goalPosition);
	xfer->xferUser(&m_goalLayer, sizeof(m_goalLayer));
	xfer->xferBool(&m_waitingForPath);
	xfer->xferCoord3D(&m_pathGoalPosition);
	xfer->xferUnsignedInt(&m_pathTimestamp);
	xfer->xferUnsignedInt(&m_blockedRepathTimestamp);
	xfer->xferBool(&m_adjustDestinations);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIInternalMoveToState::loadPostProcess( void )
{
	startMoveSound();
}  // end loadPostProcess

Bool AIInternalMoveToState::getAdjustsDestination() const 
{ 
	const Object *obj = getMachineOwner();
	if (obj->testStatus(OBJECT_STATUS_PARACHUTING))
		return false;

	const AIUpdateInterface* ai = obj->getAI();
	if (ai && !ai->isAllowedToAdjustDestination())
		return false;

	return m_adjustDestinations; 
}

/**
 * (Re)compute a path to the goal position, if we are on our own, 
 * or we are the leader of a group.
 */
Bool AIInternalMoveToState::computePath()
{
	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	m_waitingForPath = true;
	ai->requestPath(&m_goalPosition, getAdjustsDestination());
	ai->friend_startingMove(); 
	return true;
}

/**
 * We are initiating a moveTo action.
 * Pathfind from m_goalPosition to goal.
 */
StateReturnType AIInternalMoveToState::onEnter()
{
	m_ambientPlayingHandle = AHSV_Error;
	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();
	m_waitingForPath = ai->isWaitingForPath();

	if( obj->testStatus( OBJECT_STATUS_IMMOBILE ) )
	{
		return STATE_FAILURE;
	}

	if (ai->getCurLocomotor()) {
		ai->getCurLocomotor()->startMove();
		if (ai->getCurLocomotor()->isUltraAccurate()) 
		{
			setAdjustsDestination(false); // if we're being ultra accurate, we can't adjust the destination. jba.
		}
	}
	m_tryOneMoreRepath = true;  // We may try one more repath after the first one is finished.
	ai->friend_startingMove(); 

	// get object physics state
	PhysicsBehavior *physics = obj->getPhysics();
	if (physics && physics->isMotive()) {
		// If we were already moving, go immediately to the animation.
		// This is so if you redirect a moving infantry or other animated model, he doesn't
		// pop into idle for 1 frame.  jba.
		//Determine if we are on a cliff cell... if so, use the climbing model condition
		//instead of moving.
		Int cellX = REAL_TO_INT( obj->getPosition()->x / PATHFIND_CELL_SIZE );
		Int cellY = REAL_TO_INT( obj->getPosition()->y / PATHFIND_CELL_SIZE );
		
		PathfindCell* cell = TheAI->pathfinder()->getCell( obj->getLayer(), cellX, cellY );
		ModelConditionFlagType modelConditionFlag = (cell && cell->getType() != PathfindCell::CELL_CLIFF) ? MODELCONDITION_MOVING : MODELCONDITION_CLIMBING;
		obj->setModelConditionState( modelConditionFlag );
	}
	// This is a classic January 2 type of hack
	// Since the worker can go into its attack modelcondition in AIAttack::OnEnter(), 
	// it needs to get its moving modelcondition set timely enough to trigger a transition 
	// from conditionX into ATTACKING|MOVING... Thanks for reading, MLorenzen,  Jan 2, 2003
	else if ( obj->isKindOf( KINDOF_DOZER ) && obj->isKindOf( KINDOF_HARVESTER ) )
		obj->setModelConditionState( MODELCONDITION_MOVING );



	if( getAdjustsDestination() && !obj->testStatus( OBJECT_STATUS_RIDER8 ) ) 
	{
		if (!TheAI->pathfinder()->adjustDestination(obj, ai->getLocomotorSet(), &m_goalPosition)) 
		{
			TheAI->pathfinder()->snapClosestGoalPosition(obj, &m_goalPosition);
		}
		TheAI->pathfinder()->updateGoal(obj, &m_goalPosition, TheTerrainLogic->getLayerForDestination(&m_goalPosition));
	}

	// request a path to the destination
	if (!computePath()) 
	{
		ai->friend_endingMove();
		return STATE_FAILURE;
	}

	// Target to stop at the end of this path.  
	// This value will be overriden by the FollowWaypoint ai state.
	ai->setPathExtraDistance(0); 
	ai->setDesiredSpeed( FAST_AS_POSSIBLE );

	startMoveSound();
	return STATE_CONTINUE;
}

/**
 * Start playing the object's move sound.
 */
void AIInternalMoveToState::startMoveSound(void)
{
	Object *obj = getMachineOwner();
	const BodyModuleInterface *objBody = obj->getBodyModule();
	if (objBody && IS_CONDITION_WORSE(objBody->getDamageState(), BODY_DAMAGED))
	{
		AudioEventRTS soundEventMoveDamaged = *obj->getTemplate()->getSoundMoveStartDamaged();
		if (!soundEventMoveDamaged.getEventName().isEmpty()) 
		{
			soundEventMoveDamaged.setObjectID(obj->getID());
			TheLogicAudio->addAudioEvent( &soundEventMoveDamaged );
		} 
		else 
		{
			soundEventMoveDamaged = *obj->getTemplate()->getSoundMoveLoopDamaged();
			if (!soundEventMoveDamaged.getEventName().isEmpty())
			{
				soundEventMoveDamaged.setObjectID(obj->getID());
				m_ambientPlayingHandle = TheLogicAudio->addAudioEvent( &soundEventMoveDamaged );
			}
		}
	} 
	else 
	{
		AudioEventRTS soundEventMove = *obj->getTemplate()->getSoundMoveStart();
		soundEventMove.setObjectID(obj->getID());

		if (!soundEventMove.getEventName().isEmpty()) 
		{
			TheLogicAudio->addAudioEvent( &soundEventMove );
		} 
		else 
		{
			soundEventMove = *obj->getTemplate()->getSoundMoveLoop();
			soundEventMove.setObjectID(obj->getID());
			if (!soundEventMove.getEventName().isEmpty())
			{
				m_ambientPlayingHandle = TheLogicAudio->addAudioEvent( &soundEventMove );
			}
		}
	}

}

/**
 * We are leaving the moveTo state.
 */
void AIInternalMoveToState::onExit( StateExitType status )
{
	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	// stop ambient sound associated with movement
	TheLogicAudio->removeAudioEvent( m_ambientPlayingHandle );

 	// If this onExit is the result of the state machine being deleted, then there is no AI.
	// (This is why destructors should not do game logic)
	if (ai) {
		ai->friend_endingMove();
		DEBUG_ASSERTLOG(obj->getTeam(), ("AIInternalMoveToState::onExit obj has NULL team.\n"));
		if (obj->getTeam() && ai->isDoingGroundMovement() && ai->getCurLocomotor() && 
								ai->getCurLocomotor()->isUltraAccurate()) {
			Real dx = m_goalPosition.x-obj->getPosition()->x;
			Real dy = m_goalPosition.y-obj->getPosition()->y;
			if (dx*dx+dy*dy<PATHFIND_CELL_SIZE_F*PATHFIND_CELL_SIZE_F) 
			{
				// We are doing accurate ground movement, so make sure we end exactly at the goal.
				ai->setFinalPosition(&m_goalPosition);
			}
		}
	}
}

StateReturnType AIInternalMoveToState::update()
{

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();


	//If we're deployed, don't move! But keep the state around until we can packup (edge case).
	//if( obj->testStatus( OBJECT_STATUS_DEPLOYED ) )
	//{
	//	return STATE_CONTINUE;
	//}

	Path *thePath = ai->getPath();
	if (m_waitingForPath) 
	{
		// bump the timer.
		m_pathTimestamp = TheGameLogic->getFrame();
		if (ai->isWaitingForPath()) {
			/// @todo srj -- find a way to sleep for a number of frames here, if possible
			return STATE_CONTINUE;
		}
		if (thePath==nullptr) 
		{
			return STATE_FAILURE;
		}
		m_waitingForPath = false;
		m_pathGoalPosition = m_goalPosition;
		if (this->getAdjustsDestination()) {
				TheAI->pathfinder()->updateGoal(obj, thePath->getLastNode()->getPosition(), thePath->getLastNode()->getLayer());
		} else {
				TheAI->pathfinder()->removeGoal(obj);
		}
		if (!ai->getRetryPath()) {
			m_tryOneMoreRepath = false;
		}
	}

	Bool forceRecompute = false;
	if (thePath==nullptr) {
		forceRecompute = true;
	}
	Bool blocked=false;
	if (ai->isBlockedAndStuck() || ai->getNumFramesBlocked()>2*LOGICFRAMES_PER_SECOND) {
		forceRecompute = true;
		blocked = true;
		m_blockedRepathTimestamp = TheGameLogic->getFrame();
		// Intense debug logging jba.
		//DEBUG_LOG(("Info - Blocked - recomputing.\n"));
	}

	//Determine if we are on a cliff cell... if so, use the climbing model condition
	//instead of moving.
	Int cellX = REAL_TO_INT( obj->getPosition()->x / PATHFIND_CELL_SIZE );
	Int cellY = REAL_TO_INT( obj->getPosition()->y / PATHFIND_CELL_SIZE );
	
	PathfindCell* cell = TheAI->pathfinder()->getCell( obj->getLayer(), cellX, cellY );
	ModelConditionFlagType setConditionFlag = MODELCONDITION_MOVING;
	// Totally hacky set of conditions to make col. burton's monkey ass not slide down 
	// the cliffs backwards.  This could use some improvement at some point.  jba. 31DEC2002
	if (cell && cell->getType() == PathfindCell::CELL_CLIFF && !cell->getPinched()) {

		if (ai->getCurLocomotor() && ai->getCurLocomotor()->isMovingBackwards()) {
			setConditionFlag = MODELCONDITION_RAPPELLING;
			obj->clearModelConditionState( MODELCONDITION_CLIMBING );
		} else {
			setConditionFlag = MODELCONDITION_CLIMBING;
			obj->clearModelConditionState( MODELCONDITION_RAPPELLING );
		}
	}
	if (ai->getNumFramesBlocked()>LOGICFRAMES_PER_SECOND/4) 
	{
		obj->clearModelConditionState( MODELCONDITION_MOVING );
	}	
	else 
	{
		//Clear climbing if modelConditionFlag is not climbing
		if( setConditionFlag == MODELCONDITION_MOVING )
		{
			obj->clearModelConditionState( MODELCONDITION_CLIMBING );
			obj->clearModelConditionState( MODELCONDITION_RAPPELLING );
		}

		obj->setModelConditionState(MODELCONDITION_MOVING);

		if (setConditionFlag!=MODELCONDITION_MOVING) {
			obj->setModelConditionState( setConditionFlag );
		}

	}

	if (ai->canComputeQuickPath()) {
		if (obj->isKindOf(KINDOF_PROJECTILE)) {
			m_pathTimestamp = 0; // We can compute paths cheap, so don't delay to recompute.  jba.
			forceRecompute = true;
		}
	}

	if (thePath != nullptr) 
		ai->setLocomotorGoalPositionOnPath();

	// if our goal has moved, recompute our path
	if (forceRecompute || TheGameLogic->getFrame() - m_pathTimestamp > MIN_REPATH_TIME)
	{
		if (forceRecompute || !isSamePosition(obj->getPosition(), &m_pathGoalPosition, &m_goalPosition ))
		{
			// goal moved - repath
			if (!computePath()) 
			{
				return STATE_FAILURE;
			}

			// srj sez: must re-set setLocoGoal after computePath, since computePath
			// can set the loco goal to NONE...
			if (ai->getPath() != nullptr) 
				ai->setLocomotorGoalPositionOnPath();
			else
			{
				return STATE_CONTINUE;
			}
		}
	}

	//
	// Check if we have reached our destination
	//
	Real onPathDistToGoal = ai->getLocomotorDistanceToGoal();
	//DEBUG_LOG(("onPathDistToGoal = %f %s\n",onPathDistToGoal, obj->getTemplate()->getName().str()));
	if (ai->getCurLocomotor() && (onPathDistToGoal < ai->getCurLocomotor()->getCloseEnoughDist()))
	{
		if (ai->isDoingGroundMovement()) {
			// sanity check 
			Coord3D delta;
			Coord3D goalPos = m_goalPosition;
			if (ai->getPath()->getLastNode()) {
				goalPos = *ai->getPath()->getLastNode()->getPosition();
			}
			delta.x = obj->getPosition()->x - goalPos.x;
			delta.y = obj->getPosition()->y - goalPos.y;
			delta.z = 0;
			if (delta.length() > 4*PATHFIND_CELL_SIZE_F) {
				//DEBUG_LOG(("AIInternalMoveToState Trying to finish early.  Continuing...\n"));
				onPathDistToGoal = ai->getLocomotorDistanceToGoal();
				return STATE_CONTINUE;
			}
		}
		// we have reached the end of the path
		if (getAdjustsDestination()) 
		{
			ai->setLocomotorGoalNone();
		}
		DEBUG_ASSERTLOG(!getMachine()->getWantsDebugOutput(), ("AIInternalMoveToState::update: reached end of path, exiting state with success\n"));
		return STATE_SUCCESS;
	}

	return STATE_CONTINUE;
}

//-------------------------------------------------------------------------------------------------
AIMoveToState::AIMoveToState(StateMachine *machine) : m_isMoveTo(true), AIInternalMoveToState( machine, "AIMoveToState" )
{
	// m_isMoveTo is a boolean that specifies that this thing is ACTUALLY A MOVE TO.
	// child classes should set it false. (We don't have or want RTTI, so...)
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveToState::onEnter()
{

	//Kris: 7/01/03 (Temporary debug hook for units not being able to leave maps)
	if( getMachineOwner()->testStatus( OBJECT_STATUS_RIDER8 ) )
	{
		int blah = 0;
		blah++;
	}
	setAdjustsDestination(true);

	//If we have a goal object and are trying to ignore it as an obstacle...
	//This is used in the case of units trying to get really close.
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if( getMachineGoalObject() ) 
	{
		if( ai && getMachineGoalObject()->getID() == ai->getIgnoredObstacleID() ) 
		{
			setAdjustsDestination( false );
		}
	}

	// if we have a goal object, move to it, otherwise move to goal position
	if (getMachineGoalObject())	{
		m_goalPosition = *getMachineGoalObject()->getPosition();
		if (getMachineOwner()->isKindOf(KINDOF_PROJECTILE)) {
			Real halfHeight = getMachineGoalObject()->getGeometryInfo().getMaxHeightAbovePosition()/2.0f;
			m_goalPosition.z += halfHeight;
			if (getMachineGoalObject()->getPosition()->z < m_goalPosition.z) {
				m_goalPosition.z += halfHeight;
			}
		}
	} else
		m_goalPosition = *getMachineGoalPosition();

	StateReturnType ret = AIInternalMoveToState::onEnter();
	if (getMachineOwner()->getFormationID() != NO_FORMATION_ID) {
		AIGroup *group = ai->getGroup();
		if (group) {
			Real speed = group->getSpeed();
			ai->setDesiredSpeed(speed);
		}
	}
	return ret;
}

//----------------------------------------------------------------------------------------------------------
void AIMoveToState::onExit( StateExitType status )
{
	AIInternalMoveToState::onExit( status );
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveToState::update()
{
	AIUpdateInterface *ai = getMachineOwner()->getAI();

	//Kris: 7/01/03 (Temporary debug hook for units not being able to leave maps)
	if( getMachineOwner()->testStatus( OBJECT_STATUS_RIDER8 ) )
	{
		Int blah = 0;
		blah++;
	}

	UnsignedInt adjustment = ai->getMoodMatrixActionAdjustment(MoodMatrixAction::MM_Action_Move);
	if (m_isMoveTo && (adjustment & MAA_Action_To_AttackMove))
		ai->aiAttackMoveToPosition(&m_goalPosition, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);

	// if we have a goal object, move to it, as it may have moved
	Object* goalObj = getMachineGoalObject();
	Object *obj = getMachineOwner();
	if (goalObj)
	{
		m_goalPosition = *goalObj->getPosition();
		Bool gotPhysics = obj->getPhysics()!=nullptr && goalObj->getPhysics()!=nullptr;
		Bool isMissile = obj->isKindOf(KINDOF_PROJECTILE);
		if (isMissile) {
			Real halfHeight = getMachineGoalObject()->getGeometryInfo().getMaxHeightAbovePosition()/2.0f;
			m_goalPosition.z += halfHeight;
			Real zDelta = m_goalPosition.z - obj->getPosition()->z;
			if (zDelta>0) {
				m_goalPosition.z += zDelta;
			}
		}
		//gotPhysics = false;
		if (gotPhysics && isMissile && !goalObj->isKindOf(KINDOF_IMMOBILE)) {
			Coord3D ourPos = *obj->getPosition();
			Coord3D delta;
			delta.x = m_goalPosition.x - ourPos.x;
			delta.y = m_goalPosition.y - ourPos.y;
			delta.z = m_goalPosition.z - ourPos.z;
			Real mySpeed = obj->getPhysics()->getVelocityMagnitude();
			Real goalSpeed = goalObj->getPhysics()->getVelocityMagnitude();
			if (mySpeed<5.0f) mySpeed = 5.0f; // avoid divide by 0.
			Real leadDistance = static_cast<Real>((0.5*delta.length()) * goalSpeed / mySpeed);
			Coord3D dir;
			goalObj->getUnitDirectionVector3D(dir);
			m_goalPosition.x += dir.x*leadDistance;
			m_goalPosition.y += dir.y*leadDistance;
			m_goalPosition.z += dir.z*leadDistance;
		}
		//DEBUG_LOG(("update goal pos to %f %f %f\n",m_goalPosition.x,m_goalPosition.y,m_goalPosition.z));
	} else {
		Bool isMissile = obj->isKindOf(KINDOF_PROJECTILE);
		if (isMissile) {
			// When missiles are moving uphill, they need to start up quickly to clear hills.  jba.
			m_goalPosition = *getMachineGoalPosition();
			Real zDelta = m_goalPosition.z - obj->getPosition()->z;
			if (zDelta>0) {
				m_goalPosition.z += zDelta;
			}
		}

	}

	return AIInternalMoveToState::update();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveOutOfTheWayState::onEnter()
{
	setAdjustsDestination(true);
	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	if (ai->getPath()==nullptr) {
		// Must have existing path.
		return STATE_FAILURE;
	}
	m_goalPosition = *ai->getPath()->getLastNode()->getPosition();
	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------------------
Bool AIMoveOutOfTheWayState::computePath() 
{
	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();
	m_waitingForPath = true;
	if (ai->isBlockedAndStuck()) {
		ai->setCanPathThroughUnits(true);
		return true; // don't repath, just stop.
	}
	return true; // just use the existing path.  See above.
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveOutOfTheWayState::update()
{
	if (getMachineOwner()->isEffectivelyDead()) {
		return STATE_SUCCESS;
	}
	return AIInternalMoveToState::update();
}

//----------------------------------------------------------------------------------------------------------
void AIMoveOutOfTheWayState::onExit( StateExitType status )
{
	AIInternalMoveToState::onExit(status);
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (ai) {
		ai->destroyPath();
		ai->setCanPathThroughUnits(false);
		ai->clearMoveOutOfWay();
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIMoveAndTightenState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIMoveAndTightenState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );
 
 // extend base class
	AIInternalMoveToState::xfer(xfer);
	xfer->xferInt(&m_okToRepathTimes);
	xfer->xferBool(&m_checkForPath);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIMoveAndTightenState::loadPostProcess( void )
{
	AIInternalMoveToState::loadPostProcess();
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveAndTightenState::onEnter()
{
	setAdjustsDestination(false);
	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();
	m_okToRepathTimes = 1;
	m_checkForPath = true;
	TheAI->pathfinder()->removeGoal(obj);
	m_goalPosition = *getMachineGoalPosition();
	ai->requestApproachPath(&m_goalPosition);
	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveAndTightenState::update()
{		 
	if (m_checkForPath) {
		Object *obj = getMachineOwner();
		AIUpdateInterface *ai = obj->getAI();
		Path *thePath = ai->getPath();
		if (thePath && !ai->isWaitingForPath()) {
			setAdjustsDestination(true);
			m_checkForPath = false;
		}
	}
	return AIInternalMoveToState::update();
}

//----------------------------------------------------------------------------------------------------------
Bool AIMoveAndTightenState::computePath() 
{
	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();
	if (ai->isBlockedAndStuck()) {
		if (m_okToRepathTimes>0) {
			m_okToRepathTimes--;
			m_waitingForPath = true;
			ai->requestPath(&m_goalPosition, true);
			return true;
		}
		//DEBUG_LOG(("AIMoveAndTightenState::computePath - stuck, failing.\n"));
		return false;		 // don't repath for now.  jba.
	}
	return true; // just use the existing path.  See above.
}

StateReturnType AIMoveAwayFromRepulsorsState::onEnter()	  
{
	setAdjustsDestination(false);
	Object *obj = getMachineOwner();
	Object* enemy = TheAI->findClosestRepulsor(getMachineOwner(), obj->getVisionRange());
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (!enemy || !ai) {
		return STATE_FAILURE;
	}
	ai->chooseLocomotorSet(LOCOMOTORSET_PANIC);

	if (obj)
	{
		obj->setModelConditionState(MODELCONDITION_PANICKING);
	}
	m_okToRepathTimes = 1;
	m_checkForPath = true;
	TheAI->pathfinder()->removeGoal(obj);
	ai->requestSafePath(enemy->getID());
	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveAwayFromRepulsorsState::update()
{		 
	if (m_checkForPath) {
		Object *obj = getMachineOwner();
		AIUpdateInterface *ai = obj->getAI();
		Path *thePath = ai->getPath();
		if (thePath && !ai->isWaitingForPath()) {
			m_goalPosition = *thePath->getLastNode()->getPosition();
			setAdjustsDestination(false);
			m_checkForPath = false;
		}
	}
	return AIInternalMoveToState::update();
}

//----------------------------------------------------------------------------------------------------------
Bool AIMoveAwayFromRepulsorsState::computePath() 
{	
	if (m_okToRepathTimes>0) {
		m_okToRepathTimes--;
		return true;
	}
	return false;  // don't recompute path, just stop moving.
}

//----------------------------------------------------------------------------------------------------------
void AIMoveAwayFromRepulsorsState::onExit( StateExitType status )
{
	AIInternalMoveToState::onExit( status );
	Object *obj = getMachineOwner();
	if (obj)
	{
		obj->clearModelConditionState(MODELCONDITION_PANICKING);
	}
}

//----------------------------------------------------------------------------------------------------------
Bool AIPickUpCrateState::computePath()
{
	return AIInternalMoveToState::computePath();
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIPickUpCrateState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIPickUpCrateState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
  AIInternalMoveToState::xfer( xfer );
 
	xfer->xferInt(&m_delayCounter);
	xfer->xferCoord3D(&m_goalPosition);

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIPickUpCrateState::loadPostProcess( void )
{
 // extend base class
  AIInternalMoveToState::loadPostProcess();
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
StateReturnType AIPickUpCrateState::onEnter()
{
	Object* goalObj = getMachineGoalObject();
	if (!goalObj) {
		return STATE_FAILURE;
	}
	setAdjustsDestination(true);
	m_goalPosition = *goalObj->getPosition();
	m_delayCounter = 3;
	return STATE_CONTINUE;

}

//----------------------------------------------------------------------------------------------------------
void AIPickUpCrateState::onExit( StateExitType status )
{
	AIInternalMoveToState::onExit( status );

}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIPickUpCrateState::update()
{
			/// @todo srj -- find a way to sleep for a number of frames here, if possible

	if (m_delayCounter) {
		m_delayCounter--;
		if (m_delayCounter == 0) {
			return AIInternalMoveToState::onEnter();
		}
		return STATE_CONTINUE;
	}
	// do movement
	StateReturnType status = AIInternalMoveToState::update();

	return status;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIFollowPathState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIFollowPathState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	AIInternalMoveToState::xfer(xfer);

	xfer->xferInt(&m_index);
	xfer->xferBool(&m_adjustFinal);
	xfer->xferBool(&m_adjustFinalOverride);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIFollowPathState::loadPostProcess( void )
{
	AIInternalMoveToState::loadPostProcess();
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
StateReturnType AIFollowPathState::onEnter()
{
	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	m_index = 0;
	const Coord3D *pos = ai->friend_getGoalPathPosition( m_index );

	if (pos == nullptr)
		return STATE_FAILURE;

	// set initial movement goal
	m_goalPosition = *pos;
 	const Coord3D *nextPos = ai->friend_getGoalPathPosition( m_index+1 );
	m_adjustFinal = true;

	//Assign this value to the AIUpdateInterface so object's can access this value while
	//determine which waypoints to plot in the waypoint renderer.
	ai->friend_setCurrentGoalPathIndex( m_index ); 


 	if (getID() == AI_FOLLOW_EXITPRODUCTION_PATH) {
		ai->setCanPathThroughUnits(true);
		setAdjustsDestination(false);
		m_adjustFinal = true;
	}
	StateReturnType ret = AIInternalMoveToState::onEnter();
	if (obj->getFormationID() != NO_FORMATION_ID) {
		AIGroup *group = ai->getGroup();
		if (group) {
			Real speed = group->getSpeed();
			ai->setDesiredSpeed(speed);
		}
	}
 	if (nextPos) 
	{
		Coord2D delta;
		delta.x = nextPos->x - pos->x;
		delta.y = nextPos->y - pos->y;
		Real offset = delta.length();
 		const Coord3D *followingPos = ai->friend_getGoalPathPosition( m_index+2 );
		if (followingPos) offset += 4*PATHFIND_CELL_SIZE_F;
		ai->setPathExtraDistance(offset);
		// We are in the middle of a path, so don't set the final goal location yet.
		setAdjustsDestination(false);
	} 
	else 
	{
		setAdjustsDestination(m_adjustFinal);
		ai->setPathExtraDistance(0);

		// urg. hacky. if we are a projectile on the last segment, turn on precise z-pos.
		if (obj->isKindOf(KINDOF_PROJECTILE))
		{
			if (ai && ai->getCurLocomotor())
				ai->getCurLocomotor()->setUsePreciseZPos(true);
		}
	}	
	return ret;
}

//----------------------------------------------------------------------------------------------------------
void AIFollowPathState::onExit( StateExitType status )
{
	AIInternalMoveToState::onExit( status );

	// turn off precision-z-pos when we exit, just in case.
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (!ai) return;
	ai->setCanPathThroughUnits(false);
	if (ai->getCurLocomotor())
		ai->getCurLocomotor()->setUsePreciseZPos(false);

	//Assign this value to the AIUpdateInterface so object's can access this value while
	//determine which waypoints to plot in the waypoint renderer.
	ai->friend_setCurrentGoalPathIndex( -1 ); 
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIFollowPathState::update()
{
	getMachine()->setGoalPosition(&m_goalPosition);
	// do movement
	StateReturnType status = AIInternalMoveToState::update();
	// if move to has finished, move to next point on path
	if (status == STATE_SUCCESS || status == STATE_FAILURE)
	{
		Object *obj = getMachineOwner();
		AIUpdateInterface *ai = obj->getAI();
		if (status == STATE_FAILURE && m_retryCount>0) { 
			// If we failed, & haven't reached retry limit, try again.  jba.
			m_retryCount--;
		}	else {
			++m_index;
		}
		const Coord3D *pos = ai->friend_getGoalPathPosition( m_index );

		Bool tooClose=true;
		while (pos && tooClose) {
			Real dx = pos->x - obj->getPosition()->x;
			Real dy = pos->y - obj->getPosition()->y;
			tooClose = false;
			if (sqr(dx) + sqr(dy) < sqr(PATHFIND_CELL_SIZE_F)) {
				tooClose = true;
			}
			if (tooClose) {
				m_index++;
				pos = ai->friend_getGoalPathPosition(m_index);
			}
		}
		

		//Assign this value to the AIUpdateInterface so object's can access this value while
		//determine which waypoints to plot in the waypoint renderer.
		ai->friend_setCurrentGoalPathIndex( m_index ); 
		ai->ignoreObstacleID(INVALID_ID); // we have exited whatever object we are leaving, if any.  jba.
		if (pos == nullptr)
		{
			// reached the end of the path
			return STATE_SUCCESS;
		}

		ai->friend_startingMove();
		// set next movement goal
		m_goalPosition = *pos;
 		const Coord3D *nextPos = ai->friend_getGoalPathPosition( m_index+1 );

 		if (nextPos) 
		{
			Coord2D delta;
			delta.x = nextPos->x - pos->x;
			delta.y = nextPos->y - pos->y;
			Real offset = delta.length();
 			const Coord3D *followingPos = ai->friend_getGoalPathPosition( m_index+2 );
			if (followingPos) offset += 4*PATHFIND_CELL_SIZE_F;
			ai->setPathExtraDistance(offset);
			// We are in the middle of a path, so don't set the final goal location yet.
			setAdjustsDestination(false);
		} 
		else 
		{
			setAdjustsDestination(m_adjustFinal && (m_adjustFinalOverride || ai->isDoingGroundMovement()));
			if (getAdjustsDestination()) 
			{
				if (!TheAI->pathfinder()->adjustDestination(getMachineOwner(), ai->getLocomotorSet(), &m_goalPosition)) {
					return STATE_FAILURE;
				}
				TheAI->pathfinder()->updateGoal(getMachineOwner(), &m_goalPosition, TheTerrainLogic->getLayerForDestination(&m_goalPosition));
			}

			// urg. hacky. if we are a projectile on the last segment, turn on precise z-pos.
			if (obj->isKindOf(KINDOF_PROJECTILE))
			{
				if (ai && ai->getCurLocomotor())
					ai->getCurLocomotor()->setUsePreciseZPos(true);
			}
		}
		computePath();
		return STATE_CONTINUE;
	}

	return status;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIMoveAndEvacuateState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIMoveAndEvacuateState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	AIInternalMoveToState::xfer(xfer);

	xfer->xferCoord3D(&m_origin);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIMoveAndEvacuateState::loadPostProcess( void )
{
	AIInternalMoveToState::loadPostProcess();
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveAndEvacuateState::onEnter()
{
	Object *obj = getMachineOwner();

	getMachine()->lock("AIMoveAndEvacuateState::onEnter");		// This state is not user interruptable.

	m_origin = *obj->getPosition();
	setAdjustsDestination(true);

	// if we have a goal object, move to it, otherwise move to goal position
	if (getMachine()->getGoalObject())
		m_goalPosition = *getMachine()->getGoalObject()->getPosition();
	else
		m_goalPosition = *getMachine()->getGoalPosition();
	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveAndEvacuateState::update()
{
	Object *obj = getMachine()->getOwner();
	if (obj->isEffectivelyDead()) 
	{
		return STATE_FAILURE;
	}

	// do movement
	StateReturnType status = AIInternalMoveToState::update();
	if (status != STATE_CONTINUE) 
	{
		Object *obj5 = getMachineOwner();
		if (obj5->isEffectivelyDead()) 
		{
			return STATE_FAILURE;
		}

		AIUpdateInterface *ai = obj5->getAI();
		ai->aiEvacuate(FALSE, CMD_FROM_AI);
		obj5->getTeam()->setActive();
	}

	return status;
}

//----------------------------------------------------------------------------------------------------------
void AIMoveAndEvacuateState::onExit( StateExitType status )
{
	getMachine()->unlock();
	getMachine()->setGoalPosition(&m_origin); // In case we follow with a AIMoveAndDeleteState.
	AIInternalMoveToState::onExit( status );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIMoveAndDeleteState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIMoveAndDeleteState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	AIInternalMoveToState::xfer(xfer);

	xfer->xferBool(&m_appendGoalPosition);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIMoveAndDeleteState::loadPostProcess( void )
{
	AIInternalMoveToState::loadPostProcess();
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveAndDeleteState::onEnter()
{
	setAdjustsDestination(false);
	getMachine()->lock("AIMoveAndDeleteState::onEnter");
	// if we have a goal object, move to it, otherwise move to goal position
	if (getMachine()->getGoalObject())
		m_goalPosition = *getMachine()->getGoalObject()->getPosition();
	else
		m_goalPosition = *getMachine()->getGoalPosition();
	m_appendGoalPosition = true; // We may be moving off the map.
	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveAndDeleteState::update()
{
	Object *obj = getMachine()->getOwner();
	if (obj->isEffectivelyDead()) 
	{
		return STATE_FAILURE;
	}
	// do movement
	AIUpdateInterface *ai = obj->getAI();
	if (ai->getCurLocomotor()) 
	{
		ai->getCurLocomotor()->setAllowInvalidPosition(true);
	}
	if (m_appendGoalPosition) 
	{
		Path *thePath = ai->getPath();
		if (!ai->isWaitingForPath() && ai->getPath()) 
		{
			m_goalPosition.z = TheTerrainLogic->getGroundHeight(m_goalPosition.x, m_goalPosition.y);
			thePath->appendNode( &m_goalPosition, LAYER_GROUND);
			m_appendGoalPosition = false; // just did it.
		}
	}
	StateReturnType status = AIInternalMoveToState::update();
	if (status != STATE_CONTINUE) 
	{
		Object *obj4 = getMachineOwner();
		TheGameLogic->destroyObject(obj4);
	}
	return status;
}

//----------------------------------------------------------------------------------------------------------
void AIMoveAndDeleteState::onExit( StateExitType status )
{
	getMachine()->unlock();
	AIInternalMoveToState::onExit( status );
}

#define ALLOW_BACKTRACK

//----------------------------------------------------------------------------------------------------------
const Waypoint * AIFollowWaypointPathState::getNextWaypoint(void)
{
#ifdef ALLOW_BACKTRACK
	Int linkCount = m_currentWaypoint->getNumLinks();
	Int which = GameLogicRandomValue( 0, linkCount-1 );
	const Waypoint *nextWay = m_currentWaypoint->getLink( which );
	m_priorWaypoint = m_currentWaypoint;

	getMachine()->setGoalPosition(m_currentWaypoint->getLocation());// THANKS, JOHN
	return nextWay;
#else 
	if (!hasNextWaypoint()) {
		m_priorWaypoint = m_currentWaypoint;
		return nullptr;
	}
	Int skip = -1;
	Int i;
	Int linkCount = m_currentWaypoint->getNumLinks();
	for (i=0; i<linkCount; i++) {
		if (m_priorWaypoint == m_currentWaypoint->getLink(i)) {
			skip = i;
			break;
		}
	}
	Int which = 0;
	if (skip >= 0) {
		which = GameLogicRandomValue( 0, linkCount-2 );
		if (which == skip) which = linkCount-1;
	}	else {
		// pick a random link
		which = GameLogicRandomValue( 0, linkCount-1 );
	}
	const Waypoint *nextWay = m_currentWaypoint->getLink( which );
	m_priorWaypoint = m_currentWaypoint;
	return nextWay;
#endif
}

//----------------------------------------------------------------------------------------------------------
Bool AIFollowWaypointPathState::hasNextWaypoint(void)
{
#ifdef ALLOW_BACKTRACK
	return m_currentWaypoint->getNumLinks()>0;
#else 
	if (m_currentWaypoint->getNumLinks()==0) {
		return false; // no links, no next.
	}
	if (m_priorWaypoint==nullptr) {
		return m_currentWaypoint->getNumLinks()>0;
	}
	if (m_currentWaypoint->getNumLinks()>1) {
		// Two links, always works.
		return true;
	}
	// We have a prior waypoint, and 1 link. 
	if (m_priorWaypoint == m_currentWaypoint->getLink(0)) {
		return false; // don't go back to same waypoint.
	}
	return true;
#endif
}

//----------------------------------------------------------------------------------------------------------
Real AIFollowWaypointPathState::calcExtraPathDistance(void)
{
	Real extra = PATHFIND_CELL_SIZE_F/10.0f;
	const Waypoint *curWay = m_currentWaypoint;
	Int limit = 5; // just look ahead 5, in case of circular paths.  jba
	while (curWay && limit>0) {
		limit--;
		Int linkCount = curWay->getNumLinks();
		if (linkCount == 0) return extra;
		Int which = 0;
		const Waypoint *nextWay = curWay->getLink( which );
		Coord2D delta;
		delta.x = nextWay->getLocation()->x - curWay->getLocation()->x;
		delta.y = nextWay->getLocation()->y - curWay->getLocation()->y;
		extra += delta.length();
		curWay = nextWay;
	}
	return extra;
}

//----------------------------------------------------------------------------------------------------------
void AIFollowWaypointPathState::computeGoal(Bool useGroupOffsets)
{
	if (m_currentWaypoint == nullptr)
		return;

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();
	Coord3D dest = *m_currentWaypoint->getLocation();

	m_goalLayer = LAYER_GROUND; // waypoints are always on the ground.

	if (TheAI->pathfinder()->isPointOnWall(&dest)) {
		// except when they're on the wall.  jba.
		dest.z = TheAI->pathfinder()->getWallHeight();
		m_goalLayer = LAYER_WALL; 
	}

	ai->setPathExtraDistance(calcExtraPathDistance());
	if (hasNextWaypoint()) {
		// We are in the middle of a path, so don't set the final goal location yet.
		setAdjustsDestination(false);
	} else {
		setAdjustsDestination(true);
		// urg. hacky. if we are a projectile on the last segment, turn on precise z-pos.
		if (obj->isKindOf(KINDOF_PROJECTILE))
		{
			if (ai && ai->getCurLocomotor())
				ai->getCurLocomotor()->setUsePreciseZPos(true);
		}
	}	

#define NO_ROTATE_OFFSETS
#ifdef ROTATE_OFFSETS
	Real dx = (dest.x) - (obj->getPosition()->x - m_groupOffset.x);
	Real dy = (dest.y) - (obj->getPosition()->y - m_groupOffset.y);
	Real angle;
	if (m_priorWaypoint) {
		dx = dest.x - m_priorWaypoint->getLocation()->x;
		dy = dest.y - m_priorWaypoint->getLocation()->y;
		angle = atan2(dy, dx);
		Real deltaAngle = angle - m_angle;
		Real s = sin(deltaAngle);
		Real c = cos(deltaAngle);
		Real x = m_groupOffset.x * c - m_groupOffset.y * s;
		Real y = m_groupOffset.y * c + m_groupOffset.x * s;
		m_groupOffset.x = x;
		m_groupOffset.y = y;
	}	else {
		angle = atan2(dy, dx);
	}
	m_angle = angle;
#endif
	

	m_goalPosition = dest;
	m_goalPosition.x += m_groupOffset.x;
	m_goalPosition.y += m_groupOffset.y;
	if (m_goalLayer == LAYER_WALL) {
		if (!TheAI->pathfinder()->isPointOnWall(&m_goalPosition)) {
			m_goalPosition = dest;
		}
	} else {
		m_goalPosition.z = TheTerrainLogic->getGroundHeight(m_goalPosition.x, m_goalPosition.y);
	}	
	Region3D extent;
	TheTerrainLogic->getMaximumPathfindExtent(&extent);

	if (extent.isInRegionNoZ(&dest)) {
		// The waypoint is on the map.  Check & see if the adjusted position is off map [8/28/2003]
		if (!extent.isInRegionNoZ(&m_goalPosition)) {
			// clamp to in region. [8/28/2003]	
			if (m_goalPosition.x < extent.lo.x+PATHFIND_CELL_SIZE_F) {
				m_goalPosition.x = extent.lo.x+PATHFIND_CELL_SIZE_F;
			}
			if (m_goalPosition.y < extent.lo.y+PATHFIND_CELL_SIZE_F) {
				m_goalPosition.y = extent.lo.y+PATHFIND_CELL_SIZE_F;
			}
			if (m_goalPosition.x > extent.hi.x-PATHFIND_CELL_SIZE_F) {
				m_goalPosition.x = extent.hi.x-PATHFIND_CELL_SIZE_F;
			}
			if (m_goalPosition.y > extent.hi.y-PATHFIND_CELL_SIZE_F) {
				m_goalPosition.y = extent.hi.y-PATHFIND_CELL_SIZE_F;
			}
		}
	}

	if (!extent.isInRegionNoZ(&m_goalPosition)) {
		setAdjustsDestination(false); // moving off the map.
		ai->getCurLocomotor()->setAllowInvalidPosition(true); // allow it to move off the map.
		m_appendGoalPosition = true; // Moving off the map.
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIFollowWaypointPathState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIFollowWaypointPathState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	AIInternalMoveToState::xfer(xfer);

	xfer->xferCoord2D(&m_groupOffset);
	xfer->xferReal(&m_angle);
	xfer->xferInt(&m_framesSleeping);

	UnsignedInt id = INVALID_WAYPOINT_ID;
	if (m_currentWaypoint) {
		id = m_currentWaypoint->getID();
	}
	xfer->xferUnsignedInt(&id);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		m_currentWaypoint = TheTerrainLogic->getWaypointByID(id);
	}
	id = INVALID_WAYPOINT_ID;
	if (m_priorWaypoint) {
		id = m_priorWaypoint->getID();
	}
	xfer->xferUnsignedInt(&id);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		m_priorWaypoint = TheTerrainLogic->getWaypointByID(id);
	}

	xfer->xferBool(&m_appendGoalPosition);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIFollowWaypointPathState::loadPostProcess( void )
{
	AIInternalMoveToState::loadPostProcess();
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
StateReturnType AIFollowWaypointPathState::onEnter()
{
	m_appendGoalPosition = false; // not moving off the map at this point.
	m_priorWaypoint = nullptr;
	m_currentWaypoint = (static_cast<AIStateMachine *>(getMachine()))->getGoalWaypoint();
	AIUpdateInterface *ai = getMachineOwner()->getAI();

	if (m_currentWaypoint == nullptr && !m_moveAsGroup)		return STATE_FAILURE;

	getMachine()->setGoalPosition(m_currentWaypoint->getLocation());

	m_framesSleeping = 0;
	m_groupOffset.x = m_groupOffset.y = 0;

	Object *obj = getMachineOwner();
/*	Interesting thought experiment.  Didn't work well. jba
	Real distSqrLimit = 9*obj->getGeometryInfo().getMajorRadius()*obj->getGeometryInfo().getMajorRadius();
	const Waypoint *way = m_currentWaypoint;
	Bool doPrecise = false;
	while (way) {
		if (way->getNext()) {
			Real dx = way->getLocation()->x - way->getNext()->getLocation()->x;
			Real dy = way->getLocation()->y - way->getNext()->getLocation()->y;
			Real distSqr = dx*dx + dy*dy;
			if (distSqr < distSqrLimit) {
				doPrecise = true;
			}
		}
		way = way->getNext();
	}
	if (doPrecise && ai->getCurLocomotor()) {
		//ai->getCurLocomotor()->setUltraAccurate(true);
	}
*/


	Real speed = FAST_AS_POSSIBLE;
	if (m_moveAsGroup && m_currentWaypoint) {
		obj->getTeam()->setCurrentWaypoint(m_currentWaypoint);
		AIGroup *group = ai->getGroup();
		if (group) {
			speed = group->getSpeed();
			Coord3D center;
			
			group->getCenter( &center );
			m_groupOffset.x = obj->getPosition()->x - center.x;
			m_groupOffset.y = obj->getPosition()->y - center.y;
		}
	}
	if (m_currentWaypoint==nullptr && m_moveAsGroup) {
		m_currentWaypoint = obj->getTeam()->getCurrentWaypoint();
	}
	// set initial movement goal
	computeGoal(m_moveAsGroup);
	StateReturnType ret = AIInternalMoveToState::onEnter();

	ai->setDesiredSpeed(speed);

	// Update the extra path distance.   AIInternalMoveToState::onEnter resets it.
	ai->setPathExtraDistance(calcExtraPathDistance());
	if (hasNextWaypoint()) {
		// We are in the middle of a path, so don't set the final goal location yet.
		setAdjustsDestination(false);
	} else {
		setAdjustsDestination(ai->isDoingGroundMovement());
		if (getAdjustsDestination()) {
			if (!TheAI->pathfinder()->adjustDestination(getMachineOwner(), ai->getLocomotorSet(), &m_goalPosition)) {
				DEBUG_LOG(("Breaking out of follow waypoint path\n"));
				return STATE_FAILURE;
			}
			TheAI->pathfinder()->updateGoal(getMachineOwner(), &m_goalPosition, m_goalLayer);
		}
		// urg. hacky. if we are a projectile on the last segment, turn on precise z-pos.
		if (obj->isKindOf(KINDOF_PROJECTILE))
		{
			if (ai && ai->getCurLocomotor())
				ai->getCurLocomotor()->setUsePreciseZPos(true);
		}
	}
	if (ret != STATE_CONTINUE) {
		DEBUG_LOG(("Breaking out of follow waypoint path\n"));
	}
	return ret;
}

//----------------------------------------------------------------------------------------------------------
void AIFollowWaypointPathState::onExit( StateExitType status )
{
	AIInternalMoveToState::onExit( status );

	// turn off precision-z-pos when we exit, just in case.
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (ai && ai->getCurLocomotor()) {
		ai->getCurLocomotor()->setUsePreciseZPos(false);
		ai->getCurLocomotor()->setUltraAccurate(false);
	}
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIFollowWaypointPathState::update()
{
	if (m_framesSleeping>0) {
		m_framesSleeping--;
		return STATE_CONTINUE;
	}
	Object *obj = getMachineOwner();
 	AIUpdateInterface *ai = obj->getAI();


	getMachine()->setGoalPosition(m_currentWaypoint->getLocation());
	
	UnsignedInt adjustment = ai->getMoodMatrixActionAdjustment(MoodMatrixAction::MM_Action_Move);
	if (m_isFollowWaypointPathState && (adjustment & MAA_Action_To_AttackMove))	{
		if (m_moveAsGroup) {
			ai->aiAttackFollowWaypointPathAsTeam(m_currentWaypoint, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
		}	else {
			ai->aiAttackFollowWaypointPath(m_currentWaypoint, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
		}
	}

	if (m_appendGoalPosition) {	 
		Path *thePath = ai->getPath();
		if (!ai->isWaitingForPath() && ai->getPath()) {
			//Coord3D pathEnd = *thePath->getLastNode()->getPosition();
			thePath->appendNode(&m_goalPosition, LAYER_GROUND);	// waypoints are always on the ground.
			m_appendGoalPosition = false; // just did it.
		}
	}
	if (m_moveAsGroup && m_currentWaypoint != obj->getTeam()->getCurrentWaypoint()) {
		m_priorWaypoint = m_currentWaypoint;
		m_currentWaypoint = obj->getTeam()->getCurrentWaypoint();
		if (m_currentWaypoint == nullptr) {
			return STATE_SUCCESS;
		}			 
		computeGoal(false);
		if (getAdjustsDestination() && ai->isDoingGroundMovement()) {
			if (!TheAI->pathfinder()->adjustDestination(obj, ai->getLocomotorSet(), &m_goalPosition)) {
				if (m_currentWaypoint) {
					DEBUG_LOG(("Breaking out of follow waypoint path %s of %s\n", 
					m_currentWaypoint->getName().str(), m_currentWaypoint->getPathLabel1().str()));
				}
				return STATE_FAILURE;
			}
		}
		ai->friend_startingMove();
		computePath();
		if (getAdjustsDestination()) {
			TheAI->pathfinder()->updateGoal(obj, &m_goalPosition, m_goalLayer);
		}
	}

	// do movement
	StateReturnType status = AIInternalMoveToState::update();
	
	// We may want to allow ourselves to bail out of this one early. In order to do this, we check and
	// see if we're moving as a group, and then if our team is owned by an AI Skirmish player. 
	// If it is, then we compute the group centroid, and see if it is within some distance of the 
	if (m_moveAsGroup) {
		if (obj->getControllingPlayer()->isSkirmishAIPlayer()) {
			Team *team = obj->getTeam();
			AIGroup *group = TheAI->createGroup();
			team->getTeamAsAIGroup(group);

			Coord3D pos;
			group->getCenter(&pos);

			pos.x -= m_goalPosition.x;
			pos.y -= m_goalPosition.y;
			pos.z = 0;

			Int numInGroup = group->getCount();
			if (pos.length() <= (numInGroup * TheAI->getAiData()->m_skirmishGroupFudgeValue)) {
				// Consider ourselves close enough.
				status = STATE_SUCCESS;
			}
		}
	}

	// if move to has finished, move to next point on waypoint path
	if (status != STATE_CONTINUE)
	{
		m_currentWaypoint = getNextWaypoint();

		//LORENZEN ADDED LORENZEN ADDED LORENZEN ADDED 
		Object *obj3 = getMachineOwner();
		AIUpdateInterface *ai2 = obj3->getAI();
		if ( m_priorWaypoint )
			ai2->setPriorWaypointID( m_priorWaypoint->getID() );
		if ( m_currentWaypoint )
			ai2->setCurrentWaypointID( m_currentWaypoint->getID() );
		//LORENZEN ADDED LORENZEN ADDED LORENZEN ADDED 



		// if there are no links from this waypoint, we're done
		if (m_currentWaypoint==nullptr)	{
			/// Trigger "end of waypoint path" scripts (jba)
			ai2->setCompletedWaypoint(m_priorWaypoint);			
			return STATE_SUCCESS;
		}
		if (m_moveAsGroup) {
			obj3->getTeam()->setCurrentWaypoint(m_currentWaypoint);
		} 
		
		computeGoal(false);
		if (getAdjustsDestination() && ai2->isDoingGroundMovement()) {
			if (!TheAI->pathfinder()->adjustDestination(obj3, ai2->getLocomotorSet(), &m_goalPosition)) {
				if (m_currentWaypoint) {
					DEBUG_LOG(("Breaking out of follow waypoint path %s of %s\n", 
					m_currentWaypoint->getName().str(), m_currentWaypoint->getPathLabel1().str()));
				}
				return STATE_FAILURE;
			}
		}
		ai2->friend_startingMove();
		computePath();
		if (getAdjustsDestination()) {
			TheAI->pathfinder()->updateGoal(obj3, &m_goalPosition, m_goalLayer);
		}

		return STATE_CONTINUE;
	}
	if (status != STATE_CONTINUE) {
		DEBUG_LOG(("Breaking out of follow waypoint path\n"));
	}
	return status;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIFollowWaypointPathExactState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIFollowWaypointPathExactState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	AIInternalMoveToState::xfer(xfer);
	UnsignedInt id = INVALID_WAYPOINT_ID;
	if (m_lastWaypoint) {
		id = m_lastWaypoint->getID();
	}
	xfer->xferUnsignedInt(&id);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		m_lastWaypoint = TheTerrainLogic->getWaypointByID(id);
	}
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIFollowWaypointPathExactState::loadPostProcess( void )
{
	AIInternalMoveToState::loadPostProcess();
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
StateReturnType AIFollowWaypointPathExactState::onEnter()
{
	const Waypoint *currentWaypoint = (static_cast<AIStateMachine *>(getMachine()))->getGoalWaypoint();
	AIUpdateInterface *ai = getMachineOwner()->getAI();

	if (currentWaypoint == nullptr) return STATE_FAILURE;

	getMachine()->setGoalPosition(currentWaypoint->getLocation());

	Coord2D groupOffset;
	groupOffset.x = groupOffset.y = 0;

	Object *obj = getMachineOwner();

	Real speed = FAST_AS_POSSIBLE;
	if (m_moveAsGroup) {
		AIGroup *group = ai->getGroup();
		if (group) {
			speed = group->getSpeed();
			Coord3D center;
			
			group->getCenter( &center );
			groupOffset.x = obj->getPosition()->x - center.x;
			groupOffset.y = obj->getPosition()->y - center.y;
		}
	}
	ai->setCanPathThroughUnits(true);
	setAdjustsDestination(false);
	// set initial movement goal
	StateReturnType ret = AIInternalMoveToState::onEnter();
	ai->setPathFromWaypoint(currentWaypoint, &groupOffset);	
	m_lastWaypoint = currentWaypoint;
	ai->getCurLocomotor()->setAllowInvalidPosition(true); // allow it to move off the map.

	//Kris: October 4, 2002 -- Commented out by guidance of John A.
	//			Artist couldn't load his map, and turned out that it was because
	//			there was a waypoint path that pointed to itself (2 point path).
	//			John said that this code only needs "a" point, not the "last" point.
	//while (m_lastWaypoint && m_lastWaypoint->getLink(0)) {
	//	m_lastWaypoint = m_lastWaypoint->getLink(0);
	//}
	ai->setDesiredSpeed(speed);

	return ret;
}

//----------------------------------------------------------------------------------------------------------
void AIFollowWaypointPathExactState::onExit( StateExitType status )
{
	AIInternalMoveToState::onExit( status );

	// turn off precision-z-pos when we exit, just in case.
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (ai) {
		ai->setCompletedWaypoint(m_lastWaypoint);			
		ai->setCanPathThroughUnits(false);
		ai->getCurLocomotor()->setAllowInvalidPosition(false); // turn off allow it to move off the map.
	}
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIFollowWaypointPathExactState::update()
{

	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (ai) ai->setCanPathThroughUnits(true);
	// do movement
	StateReturnType status = AIInternalMoveToState::update();

	return status;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIWanderState::crc( Xfer *xfer )
{
	AIFollowWaypointPathState::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIWanderState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	AIFollowWaypointPathState::xfer(xfer);

	xfer->xferInt(&m_waitFrames);
	xfer->xferInt(&m_timer);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIWanderState::loadPostProcess( void )
{
	AIFollowWaypointPathState::loadPostProcess();
}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
StateReturnType AIWanderState::onEnter()
{
	m_currentWaypoint = (static_cast<AIStateMachine *>(getMachine()))->getGoalWaypoint();

	AIUpdateInterface *ai = getMachineOwner()->getAI();
	m_priorWaypoint = nullptr;
	if (m_currentWaypoint == nullptr || ai==nullptr)
		return STATE_FAILURE;
	m_groupOffset.x = m_groupOffset.y = 0;
	Locomotor* curLoco = ai->getCurLocomotor();
	if (curLoco && curLoco->getWanderWidthFactor() > 0.0f) {
		Int delta = REAL_TO_INT_FLOOR(curLoco->getWanderWidthFactor()+0.5f);
		if (delta<1) delta = 1;
		m_groupOffset.x = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
		m_groupOffset.y = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
	}
	m_timer = 0;
	m_waitFrames = 10 + (getMachineOwner()->getID() & 0x7);
	// set initial movement goal
	computeGoal(false);
	StateReturnType ret = AIInternalMoveToState::onEnter();
	ai->setPathExtraDistance(calcExtraPathDistance());
	return ret;
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIWanderState::update()
{
	// do movement
	Object *obj = getMachineOwner();
	StateReturnType status = AIInternalMoveToState::update();
	if (obj->isKindOf(KINDOF_CAN_BE_REPULSED)) {
		m_timer--;
		if (m_timer<0) {
			m_timer = m_waitFrames;
			Object* enemy = TheAI->findClosestRepulsor(getMachineOwner(), obj->getVisionRange());
			if (enemy) {
				return STATE_FAILURE;
			}
		}
	}
	// if move to has finished, move to next point on waypoint path
	if (status != STATE_CONTINUE)
	{
		AIUpdateInterface *ai = obj->getAI();

		m_currentWaypoint = getNextWaypoint();
		// if there are no links from this waypoint, we're done
		if (m_currentWaypoint == nullptr)	{
			/// Trigger "end of waypoint path" scripts (jba)
			ai->setCompletedWaypoint(m_priorWaypoint);
			
			return STATE_SUCCESS;
		}

		Locomotor* curLoco = ai->getCurLocomotor();
		if (curLoco && curLoco->getWanderWidthFactor() > 0.0f) {
			Int delta = REAL_TO_INT_FLOOR(curLoco->getWanderWidthFactor()+0.5f);
			if (delta<1) delta = 1;
			m_groupOffset.x = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
			m_groupOffset.y = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
		}
		computeGoal(false);
		computePath();
		return STATE_CONTINUE;
	}
	// Never leave this state until told to.
	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------------------
void AIWanderState::onExit( StateExitType status )
{
	AIFollowWaypointPathState::onExit( status );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIWanderInPlaceState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIWanderInPlaceState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	AIInternalMoveToState::xfer(xfer);

	xfer->xferCoord3D(&m_origin);
	xfer->xferInt(&m_waitFrames);
	xfer->xferInt(&m_timer);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** LoadPostProcess */
// ------------------------------------------------------------------------------------------------
void AIWanderInPlaceState::loadPostProcess( void )
{
	AIInternalMoveToState::loadPostProcess();
}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
StateReturnType AIWanderInPlaceState::onEnter()
{
	m_origin = *getMachineOwner()->getPosition();

	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (ai) {
		ai->chooseLocomotorSet(LOCOMOTORSET_WANDER);
	}

	Int delta = 3;
	if (ai->getCurLocomotor()) {
		delta = REAL_TO_INT_FLOOR( (ai->getCurLocomotor()->getWanderAboutPointRadius()/PATHFIND_CELL_SIZE_F) + 0.5f);
	}
	Coord3D offset;
	offset.x = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
	offset.y = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
	m_goalPosition = m_origin;
	m_goalPosition.x += offset.x;
	m_goalPosition.y += offset.y;
	m_timer = 0;
	m_waitFrames = 10 + (getMachineOwner()->getID() & 0x7);
	StateReturnType ret = AIInternalMoveToState::onEnter();
	return ret;
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIWanderInPlaceState::update()
{
	// do movement
	StateReturnType status = AIInternalMoveToState::update();

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (!ai) return STATE_FAILURE;
	if (obj->isKindOf(KINDOF_CAN_BE_REPULSED)) {
		m_timer--;
		if (m_timer<0) {
			m_timer = m_waitFrames;
			Object* enemy = TheAI->findClosestRepulsor(getMachineOwner(), obj->getVisionRange());
			if (enemy) {
				return STATE_FAILURE;
			}
		}
	}
	// if move to has finished, move to next point on waypoint path
	if (status != STATE_CONTINUE)
	{
		Int delta = 3;
		if (ai->getCurLocomotor()) {
			delta = REAL_TO_INT_FLOOR( (ai->getCurLocomotor()->getWanderAboutPointRadius()/PATHFIND_CELL_SIZE_F) + 0.5f);
		}
		Coord3D offset;
		offset.x = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
		offset.y = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
		m_goalPosition = m_origin;
		m_goalPosition.x += offset.x;
		m_goalPosition.y += offset.y;
		AIInternalMoveToState::onEnter();
		return STATE_CONTINUE;
	}
	// Never leave this state until told to.
	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------------------
void AIWanderInPlaceState::onExit( StateExitType status )
{
	AIInternalMoveToState::onExit( status );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIPanicState::crc( Xfer *xfer )
{
	AIFollowWaypointPathState::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIPanicState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	AIFollowWaypointPathState::xfer(xfer);

	xfer->xferInt(&m_waitFrames);
	xfer->xferInt(&m_timer);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIPanicState::loadPostProcess( void )
{
	AIFollowWaypointPathState::loadPostProcess();
}  // end loadPostProcess

/**
 * Panic and run screaming along a waypoint path.
 */
StateReturnType AIPanicState::onEnter()
{
	m_currentWaypoint = (static_cast<AIStateMachine *>(getMachine()))->getGoalWaypoint();

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();
	if (m_currentWaypoint == nullptr)
		return STATE_FAILURE;
	// set initial movement goal
	Locomotor* curLoco = ai->getCurLocomotor();
	if (curLoco && curLoco->getWanderWidthFactor() > 0.0f) {
		Int delta = REAL_TO_INT_FLOOR(curLoco->getWanderWidthFactor()+0.5f);
		if (delta<1) delta = 1;
		m_groupOffset.x = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
		m_groupOffset.y = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
	}
	computeGoal(false);
	StateReturnType ret = AIInternalMoveToState::onEnter();

	m_timer = 0;
	m_waitFrames = 10 + (getMachineOwner()->getID() & 0x7);
	// Update the extra path distance.   AIInternalMoveToState::onEnter resets it.
	ai->setPathExtraDistance(calcExtraPathDistance());
	if (obj)
	{
		obj->setModelConditionState(MODELCONDITION_PANICKING);
	}

	return ret;
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIPanicState::update()
{
	// do movement
	StateReturnType status = AIInternalMoveToState::update();

	Object *obj = getMachineOwner();
	if (obj->isKindOf(KINDOF_CAN_BE_REPULSED)) {
		m_timer--;
		if (m_timer<0) {
			m_timer = m_waitFrames;
			Object* enemy = TheAI->findClosestRepulsor(getMachineOwner(), obj->getVisionRange());
			if (enemy) {
				return STATE_FAILURE;
			}
		}
	}

	// if move to has finished, move to next point on waypoint path
	if (status == STATE_SUCCESS)
	{
		Object *obj2 = getMachineOwner();
		AIUpdateInterface *ai = obj2->getAI();

		m_currentWaypoint = getNextWaypoint();
		// if there are no links from this waypoint, we're done
		if (m_currentWaypoint == nullptr)	{
			/// Trigger "end of waypoint path" scripts (jba)
			ai->setCompletedWaypoint(m_priorWaypoint);
			
			return STATE_SUCCESS;
		}
		Locomotor* curLoco = ai->getCurLocomotor();
		if (curLoco && curLoco->getWanderWidthFactor() > 0.0f) {
			Int delta = REAL_TO_INT_FLOOR(curLoco->getWanderWidthFactor()+0.5f);
			if (delta<1) delta = 1;
			m_groupOffset.x = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
			m_groupOffset.y = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
		}
		computeGoal(false);
		computePath();
		return STATE_CONTINUE;
	}
	// Never leave this state until told to.
	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------------------
void AIPanicState::onExit( StateExitType status )
{
	Object *obj = getMachineOwner();
	obj->clearModelConditionState(MODELCONDITION_PANICKING);
	AIInternalMoveToState::onExit( status );
}
