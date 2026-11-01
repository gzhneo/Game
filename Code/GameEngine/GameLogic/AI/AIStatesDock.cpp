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

// Docking, entering and exiting containers

//----------------------------------------------------------------------------------------------------------
AIDockState::~AIDockState()
{
	if (m_dockMachine) {
		m_dockMachine->halt();
		deleteInstanceOf(m_dockMachine);
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIDockState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIDockState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	Bool hasMachine = m_dockMachine!=nullptr;
	
	xfer->xferBool(&hasMachine);

	if (hasMachine && m_dockMachine==nullptr)	{
		// create new state machine for attack behavior
		m_dockMachine = newInstance(AIDockMachine)( getMachineOwner());
	}
	if (hasMachine) {
		xfer->xferSnapshot(m_dockMachine);
	}
	xfer->xferBool(&m_usingPrecisionMovement);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIDockState::loadPostProcess( void )
{
}  // end loadPostProcess

#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AIDockState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if (m_dockMachine) name.concat(m_dockMachine->getCurrentStateName());
	else name.concat("*NULL m_dockMachine");
	return name;
}
#endif

//----------------------------------------------------------------------------------------------
/**
 * Dock with the GoalObject.
 * When we enter the AI_DOCK state, create a docking state machine
 * that implements the details of the docking behavior.
 */
StateReturnType AIDockState::onEnter()
{
	// who are we docking with?
	Object *dockWithMe = getMachineGoalObject();
	if (dockWithMe == nullptr)
	{
		// we have nothing to dock with!
		DEBUG_LOG(("No goal in AIDockState::onEnter - exiting.\n"));
		return STATE_FAILURE;
	}
  DockUpdateInterface *dock = nullptr;
	dock = dockWithMe->getDockUpdateInterface();

	// if we have nothing to dock with, fail
	if (dock == nullptr)	{
		DEBUG_LOG(("Goal is not a dock in AIDockState::onEnter - exiting.\n"));
		return STATE_FAILURE;
	}

	// tell the pathfinder to ignore the object we are docking with, so it doesn't block us
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if( ai ) 
	{
		ai->ignoreObstacle( dockWithMe );
	}

	// create new state machine for attack behavior
	m_dockMachine = newInstance(AIDockMachine)( getMachineOwner());

	// tell the docking machine what it is docking with
	m_dockMachine->setGoalObject( dockWithMe );
	// now that essential parameters are set, set the machine's initial state
	return m_dockMachine->initDefaultState( );
}

/**
 * For whatever reason, we are leaving the AI_DOCK state.
 * Destroy the docking sub-machine.
 */
void AIDockState::onExit( StateExitType status )
{
	// destroy the dock machine
	if (m_dockMachine) {
		m_dockMachine->halt();// GS, you have to halt before you delete to do cleanup.
		deleteInstanceOf(m_dockMachine);
		m_dockMachine = nullptr;
	}	else {
		DEBUG_LOG(("Dock exited immediately\n"));
	}

	// stop ignoring our goal object
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (ai)
	{
		ai->setCanPathThroughUnits(false);
		ai->ignoreObstacle( nullptr );
	}
	
}

StateReturnType AIDockState::update()
{

	/**
	 * Run the docking state sub-machine.
	 * If the dock state machine returns anything other than CONTINUE,
	 * it has finished. propagating the return code will cause
	 * the containing state machine to do the right thing.
	 */
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (ai)
	{
		ai->setCanPathThroughUnits(true);
		//if (ai->isBlockedAndStuck()) {
			//DEBUG_LOG(("Blocked and stuck.\n"));
		//}
		//if (ai->getNumFramesBlocked()>5) {
			//DEBUG_LOG(("Blocked %d frames\n", ai->getNumFramesBlocked()));
		//}
	}
	/* 
		Note the use of CONVERT_SLEEP_TO_CONTINUE; even if the sub-machine
		sleeps, we still need to be called every frame.
	*/
	return CONVERT_SLEEP_TO_CONTINUE(m_dockMachine->updateStateMachine());
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIEnterState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIEnterState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	AIInternalMoveToState::xfer(xfer);

	xfer->xferObjectID(&m_entryToClear);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIEnterState::loadPostProcess( void )
{
	AIInternalMoveToState::loadPostProcess();
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
StateReturnType AIEnterState::onEnter()
{
	m_entryToClear = INVALID_ID;

	Object* obj = getMachineOwner();
	Object* goal = getMachineGoalObject();
	if (goal)
	{
		if( !TheActionManager->canEnterObject( obj, goal, obj->getAI()->getLastCommandSource(), CanEnterType::CHECK_CAPACITY ) )
			return STATE_FAILURE;	

		m_goalPosition = *goal->getPosition();

		ContainModuleInterface* contain = goal->getContain();
		if (contain)
		{
			contain->onObjectWantsToEnterOrExit(obj, WANTS_TO_ENTER);
			m_entryToClear = goal->getID();
		}
	}
	else
	{
		return STATE_FAILURE;
	}

	// tell the pathfinder to ignore the enterable object
	AIUpdateInterface *ai = obj->getAI();
	ai->ignoreObstacle( getMachineGoalObject() );
	if (ai->getCurLocomotor()) 
	{
		ai->getCurLocomotor()->setAllowInvalidPosition(true);
	}
	setAdjustsDestination(false);
	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------------------
void AIEnterState::onExit( StateExitType status )
{
	Object* obj = getMachineOwner();
	AIInternalMoveToState::onExit( status );

	// tell the pathfinder to stop ignoring the object
	AIUpdateInterface *ai = obj->getAI();
	if (ai) 
	{

		ai->ignoreObstacle( nullptr );
		if (ai->getCurLocomotor()) 
		{
			ai->getCurLocomotor()->setAllowInvalidPosition(false);
		}
	}

	// use this, rather than getMachineGoalObject, in case the goal
	// is killed while we were waiting...
	if (m_entryToClear != INVALID_ID)
	{
		Object* goal = TheGameLogic->findObjectByID(m_entryToClear);
		if (goal)
		{
			ContainModuleInterface* contain = goal->getContain();
			if (contain)
			{
				contain->onObjectWantsToEnterOrExit(obj, WANTS_NEITHER);
			}
		}
	}
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIEnterState::update()
{

	// update the goal position to coincide with the GoalObject
	Object* obj = getMachineOwner();
	Object* goal = getMachineGoalObject();
	if (goal)
	{
		// if our goal is contained by something else, give up. this is for the following bug:
		// -- tell some rangers to enter a humvee
		// -- tell the humvee to enter a chinook
		// -- fly the chinook around; the rangers follow the chinook like dopes
		// this just bails in this case. (srj)
		if (goal->getContainedBy() != nullptr && goal->isAboveTerrain() && !obj->isAboveTerrain())
		{
			return STATE_FAILURE;	
		}

		m_goalPosition = *goal->getPosition();
		obj->getAI()->friend_setGoalObject(goal);
		if (!TheActionManager->canEnterObject(obj, goal, obj->getAI()->getLastCommandSource(), CanEnterType::CHECK_CAPACITY))
		{
			/*
				special-case: if it's an enemy, try attacking it instead. this is to address this bug: (srj)

				Bug: Units stop instead of attacking if building they were trying to garrison is taken by enemy units first.

				1. any game/any faction
				2. build infantry units that can garrison neutral buildings
				3. have infantry  units garrison a neutral building, before they garrison the building have some enemy units garrison it first.

				Expected result: Based on test plan: Instead of just stopping when enemy units garrison building first, they should continue and attack the building.
			*/
			if( obj->getRelationship(goal) == ENEMIES && obj->getAI() )
			{
				CanAttackResult result = TheActionManager->getCanAttackObject(obj, goal, obj->getAI()->getLastCommandSource(), ATTACK_NEW_TARGET);
				if( result == CanAttackResult::ATTACKRESULT_POSSIBLE || result == CanAttackResult::ATTACKRESULT_POSSIBLE_AFTER_MOVING )
				{
	 				obj->getAI()->aiAttackObject(goal, NO_MAX_SHOTS_LIMIT, obj->getAI()->getLastCommandSource());
					// weird but true. return state_continue, because if we're here, we're actually an attack state
					// since we just changed the state, it doesn't really matter what we return here.
					return STATE_CONTINUE;
				}
				return STATE_FAILURE;	
			}
			return STATE_FAILURE;	
		}

		// If we are held, then we must have entered the goal.
		if( getMachineOwner()->isDisabledByType( DISABLED_HELD ) )
		{
			return STATE_SUCCESS;
		}
	} 
	else 
	{
		return STATE_FAILURE;
	}

	StateReturnType code = AIInternalMoveToState::update();

	// if it's airborne, wait for it to land
	if (code == STATE_SUCCESS && goal->isAboveTerrain() && !obj->isAboveTerrain())
	{
		code = STATE_CONTINUE;
	}

	if (code == STATE_SUCCESS) 
	{
		// Make sure we entered the container.
		// srj sez: I don't think we want to restrict this to HELD items. See the intro of GLA02.map
		// for an example of guys-off-the-border-but-not-held who need this check.
		//if( obj->isDisabledByType( DISABLED_HELD ) )
		{
			if (goal)
			{
				// we didn't enter.  See if we're close.
				Real dx = (obj->getPosition()->x - goal->getPosition()->x);
				Real dy = (obj->getPosition()->y - goal->getPosition()->y);
				Real radius = goal->getGeometryInfo().getMinorRadius();
				if (goal->getGeometryInfo().getGeomType()!=GEOMETRY_BOX) {
					radius = goal->getGeometryInfo().getMajorRadius();
				}
				Bool closeEnough = dx*dx+dy*dy < sqr(radius);
				if (closeEnough) {
					// Grab the container and force ourselves into it.
					// This case is primarily to handle transports on the map border for scripted setup.
					// The partition manager doesn't generate collisions in the border area, so we have to
					// add ourselves.  jba.
					ContainModuleInterface* contain = goal->getContain();
					if (contain)
					{
						contain->addToContain(obj);
					}
				}
			}
		}
	}

	return code;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIExitState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIExitState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferObjectID(&m_entryToClear);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIExitState::loadPostProcess( void )
{
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
StateReturnType AIExitState::onEnter()
{
	m_entryToClear = INVALID_ID;

	Object* obj = getMachineOwner();
	Object* goal = getMachineGoalObject();
	if (goal)
	{
		ContainModuleInterface* contain = goal->getContain();
		if (contain)
		{
			contain->onObjectWantsToEnterOrExit(obj, WANTS_TO_EXIT);
			m_entryToClear = goal->getID();
		}
		return STATE_CONTINUE;
	}
	else
	{
		return STATE_FAILURE;
	}
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIExitState::update()
{

	// update the goal position to coincide with the GoalObject
	Object* obj = getMachineOwner();
	Object* goal = getMachineGoalObject();
	if (goal)
	{
		AIUpdateInterface* goalAI = goal->getAI();
		if (goalAI && goalAI->getAiFreeToExit(obj) == WAIT_TO_EXIT)
			return STATE_CONTINUE;

		DEBUG_ASSERTCRASH(obj, ("obj must not be null here"));

		//GS.  The goal of unified ExitInterfaces dies a horrible death.  I can't ask Object for the exit,
		// as removeFromContain is only in the Contain type.  I'm spliting the names in shame.
		ExitInterface* goalExitInterface = goal->getContain() ? goal->getContain()->getContainExitInterface() : nullptr;
		if( goalExitInterface == nullptr )
			return STATE_FAILURE;

		if( goalExitInterface->isExitBusy() )
			return STATE_CONTINUE;// Just wait a sec.

		ExitDoorType exitDoor = goalExitInterface ? goalExitInterface->reserveDoorForExit(obj->getTemplate(), obj) : DOOR_NONE_NEEDED;
		if (exitDoor == DOOR_NONE_AVAILABLE)
			return STATE_FAILURE;

		goalExitInterface->exitObjectViaDoor(obj, exitDoor);
		if( getMachine()->getCurrentStateID() != getID() )
			return STATE_CONTINUE;// Not sucess, because exitViaDoor has changed us to FollowPath, and if we say Success, our machine will think FollowPath succeeded
		else
			return STATE_SUCCESS;
	} 
	else 
	{
		return STATE_FAILURE;
	}
}

//----------------------------------------------------------------------------------------------------------
void AIExitState::onExit( StateExitType status )
{
	Object* obj = getMachineOwner();

	// use this, rather than getMachineGoalObject, in case the goal
	// is killed while we were waiting...
	if (m_entryToClear != INVALID_ID)
	{
		Object* goal = TheGameLogic->findObjectByID(m_entryToClear);
		if (goal)
		{
			ContainModuleInterface* contain = goal->getContain();
			if (contain)
			{
				contain->onObjectWantsToEnterOrExit(obj, WANTS_NEITHER);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIExitInstantlyState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIExitInstantlyState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferObjectID(&m_entryToClear);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIExitInstantlyState::loadPostProcess( void )
{
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
StateReturnType AIExitInstantlyState::onEnter()
{
	m_entryToClear = INVALID_ID;

	Object* obj = getMachineOwner();
	Object* goal = getMachineGoalObject();
	if (goal)
	{
		ContainModuleInterface* contain = goal->getContain();
		if (contain)
		{
			contain->onObjectWantsToEnterOrExit(obj, WANTS_TO_EXIT);
			m_entryToClear = goal->getID();
		}

		DEBUG_ASSERTCRASH(obj, ("obj must not be null here"));

		//GS.  The goal of unified ExitInterfaces dies a horrible death.  I can't ask Object for the exit,
		// as removeFromContain is only in the Contain type.  I'm spliting the names in shame.
		ExitInterface* goalExitInterface = goal->getContain() ? goal->getContain()->getContainExitInterface() : nullptr;
		if( goalExitInterface == nullptr )
			return STATE_FAILURE;

		goalExitInterface->exitObjectViaDoor( obj, DOOR_1 );

		return STATE_CONTINUE;// Not success, because exitViaDoor has changed us to FollowPath, and if we say Success, our machine will think FollowPath succeeded
	}
	else
	{
		return STATE_FAILURE;
	}
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIExitInstantlyState::update()
{
	if( getMachine()->getCurrentStateID() != getID() )
	{
		return STATE_CONTINUE;// Not success, because exitViaDoor has changed us to FollowPath, and if we say Success, our machine will think FollowPath succeeded
	}
	return STATE_SUCCESS;
}

//----------------------------------------------------------------------------------------------------------
void AIExitInstantlyState::onExit( StateExitType status )
{
	Object* obj = getMachineOwner();

	// use this, rather than getMachineGoalObject, in case the goal
	// is killed while we were waiting...
	if (m_entryToClear != INVALID_ID)
	{
		Object* goal = TheGameLogic->findObjectByID(m_entryToClear);
		if (goal)
		{
			ContainModuleInterface* contain = goal->getContain();
			if (contain)
			{
				contain->onObjectWantsToEnterOrExit(obj, WANTS_NEITHER);
			}
		}
	}
}
