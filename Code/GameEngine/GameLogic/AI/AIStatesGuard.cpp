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

// Guard states: guard, retaliate, guard tunnel networks

//----------------------------------------------------------------------------------------------------------
AIGuardState::~AIGuardState()
{
	if (m_guardMachine)	{
		m_guardMachine->halt();
		deleteInstanceOf(m_guardMachine);
	}
}

#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AIGuardState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if (m_guardMachine) name.concat(m_guardMachine->getCurrentStateName());
	else name.concat("*NULL guardMachine");
	return name;
}
#endif

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIGuardState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIGuardState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	Bool hasMachine = m_guardMachine!=nullptr;
	
	xfer->xferBool(&hasMachine);

	if (hasMachine && m_guardMachine==nullptr)	{
		// create new state machine for guard behavior
		m_guardMachine = newInstance(AIGuardMachine)( getMachineOwner());
	}
	if (hasMachine) {
		xfer->xferSnapshot(m_guardMachine);	
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIGuardState::loadPostProcess( void )
{
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
//Is our guard state in an attack sub-state?
Bool AIGuardState::isAttack() const
{
	if( m_guardMachine )
	{
		return m_guardMachine->isInAttackState();
	}
	return FALSE;
}

//----------------------------------------------------------------------------------------------------------
//Is our guard state in guard-idle?
Bool AIGuardState::isGuardIdle() const
{
	if( m_guardMachine )
	{
		return m_guardMachine->isInGuardIdleState();
	}
	return FALSE;
}

StateReturnType AIGuardState::onEnter()
{

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	m_guardMachine = newInstance(AIGuardMachine)( getMachineOwner());

	// tell the guarding machine what it is guarding with
	switch(ai->getGuardTargetType())
	{
		case GuardTargetType::GUARDTARGET_LOCATION: m_guardMachine->setTargetPositionToGuard( ai->getGuardLocation() ); break;
		case GuardTargetType::GUARDTARGET_OBJECT: m_guardMachine->setTargetToGuard( TheGameLogic->findObjectByID(ai->getGuardObject()) ); break;
		case GuardTargetType::GUARDTARGET_AREA: m_guardMachine->setAreaToGuard( ai->getAreaToGuard() ); break;
	}
	m_guardMachine->setGuardMode(ai->getGuardMode());

	// now that essential parameters are set, set the machine's initial state
	if (m_guardMachine->initDefaultState() == STATE_FAILURE) 
		return STATE_FAILURE;
	// counted for the academy advice at the end of the game
	obj->getControllingPlayer()->getAcademyStats()->recordGuardAbilityUsed();
	return m_guardMachine->setState(AI_GUARD_RETURN);
}

//----------------------------------------------------------------------------------------------------------
void AIGuardState::onExit( StateExitType status )
{
	deleteInstanceOf(m_guardMachine);
	m_guardMachine = nullptr;

	Object *obj = getMachineOwner();
	obj->getAI()->clearGuardTargetType();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIGuardState::update()
{
	//DEBUG_LOG(("AIGuardState frame %d: %08lx\n",TheGameLogic->getFrame(),getMachineOwner()));

	if (m_guardMachine == nullptr) 
	{
		return STATE_FAILURE; // We actually already exited.
	}

	// if all of our weapons are out of ammo, can't attack.
	// (this can happen for units which never auto-reload, like the Raptor)
	Object* owner = getMachineOwner();	
	if( owner->getAI()->getJetAIUpdate() && owner->isOutOfAmmo() && !owner->isKindOf(KINDOF_PROJECTILE) && !owner->getTemplate()->isEnterGuard())
	{
		DEBUG_CRASH(("Hmm, this should probably never happen, since this case should be intercepted by JetAIUpdate\n"));
		return STATE_FAILURE;
	}

	getMachine()->lock("AIGuardState::update");	// We don't want to switch out of guard during the update.
	StateReturnType ret = m_guardMachine->updateStateMachine();
	getMachine()->unlock();
	return ret;
}

//----------------------------------------------------------------------------------------------------------
AIGuardRetaliateState::~AIGuardRetaliateState()
{
	if (m_guardRetaliateMachine)	{
		m_guardRetaliateMachine->halt();
		deleteInstanceOf(m_guardRetaliateMachine);
	}
}

#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AIGuardRetaliateState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if( m_guardRetaliateMachine ) 
	{
		name.concat(m_guardRetaliateMachine->getCurrentStateName());
	}
	else 
	{
		name.concat("*NULL guardRetaliateMachine");
	}
	return name;
}
#endif

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	Bool hasMachine = m_guardRetaliateMachine!=nullptr;
	
	xfer->xferBool(&hasMachine);

	if (hasMachine && m_guardRetaliateMachine==nullptr)	
	{
		// create new state machine for guard behavior
		m_guardRetaliateMachine = newInstance(AIGuardRetaliateMachine)( getMachineOwner());
	}
	if (hasMachine) 
	{
		xfer->xferSnapshot(m_guardRetaliateMachine);	
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateState::loadPostProcess( void )
{
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
//Is our retaliate state in an attack sub-state?
Bool AIGuardRetaliateState::isAttack() const
{
	if( m_guardRetaliateMachine )
	{
		return m_guardRetaliateMachine->isInAttackState();
	}
	return FALSE;
}

StateReturnType AIGuardRetaliateState::onEnter()
{

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	m_guardRetaliateMachine = newInstance(AIGuardRetaliateMachine)( getMachineOwner());
#ifdef STATE_MACHINE_DEBUG
	m_guardRetaliateMachine->setDebugOutput(getMachine()->getWantsDebugOutput());
#endif
	// tell the guarding machine what it is guarding with
	m_guardRetaliateMachine->setTargetPositionToGuard( ai->getGoalPosition() );

	Object *goalObject = ai->getGoalObject();
	if( goalObject )
	{
		m_guardRetaliateMachine->setNemesisID( goalObject->getID() );
	}

	// now that essential parameters are set, set the machine's initial state
	return m_guardRetaliateMachine->initDefaultState();
}

//----------------------------------------------------------------------------------------------------------
void AIGuardRetaliateState::onExit( StateExitType status )
{
	deleteInstanceOf(m_guardRetaliateMachine);
	m_guardRetaliateMachine = nullptr;

	Object *obj = getMachineOwner();
	obj->getAI()->clearGuardTargetType();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIGuardRetaliateState::update()
{
	//DEBUG_LOG(("AIGuardRetaliateState frame %d: %08lx\n",TheGameLogic->getFrame(),getMachineOwner()));

	if (m_guardRetaliateMachine == nullptr) 
	{
		return STATE_FAILURE; // We actually already exited.
	}

	// if all of our weapons are out of ammo, can't attack.
	// (this can happen for units which never auto-reload, like the Raptor)
	Object* owner = getMachineOwner();	
	if( owner->getAI()->getJetAIUpdate() && owner->isOutOfAmmo() && !owner->isKindOf(KINDOF_PROJECTILE) && !owner->getTemplate()->isEnterGuard())
	{
		DEBUG_CRASH(("Hmm, this should probably never happen, since this case should be intercepted by JetAIUpdate\n"));
		return STATE_FAILURE;
	}

	StateReturnType ret = m_guardRetaliateMachine->updateStateMachine();
	return ret;
}

//----------------------------------------------------------------------------------------------------------
AITunnelNetworkGuardState::~AITunnelNetworkGuardState()
{
	if (m_guardMachine)	{
		m_guardMachine->halt();
		deleteInstanceOf(m_guardMachine);
	}
}

#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AITunnelNetworkGuardState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if (m_guardMachine) name.concat(m_guardMachine->getCurrentStateName());
	else name.concat("*NULL guardMachine");
	return name;
}
#endif

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AITunnelNetworkGuardState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AITunnelNetworkGuardState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	Bool hasMachine = m_guardMachine!=nullptr;
	
	xfer->xferBool(&hasMachine);

	if (hasMachine && m_guardMachine==nullptr)	{
		// create new state machine for guard behavior
		m_guardMachine = newInstance(AITNGuardMachine)( getMachineOwner());
	}
	if (hasMachine) {
		xfer->xferSnapshot(m_guardMachine);	
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AITunnelNetworkGuardState::loadPostProcess( void )
{
}  // end loadPostProcess

//----------------------------------------------------------------------------------------------------------
//Is our guard tunnel network state in an attack sub-state?
Bool AITunnelNetworkGuardState::isAttack() const
{
	if( m_guardMachine )
	{
		return m_guardMachine->isInAttackState();
	}
	return FALSE;
}

StateReturnType AITunnelNetworkGuardState::onEnter()
{

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	m_guardMachine = newInstance(AITNGuardMachine)( getMachineOwner());

	// tell the guarding machine what it is guarding with
	m_guardMachine->setTargetPositionToGuard( ai->getGuardLocation() ); 
	m_guardMachine->setGuardMode(ai->getGuardMode());

	// now that essential parameters are set, set the machine's initial state
	if (m_guardMachine->initDefaultState() == STATE_FAILURE) 
		return STATE_FAILURE;
	return m_guardMachine->setState(AI_GUARD_RETURN);
}

//----------------------------------------------------------------------------------------------------------
void AITunnelNetworkGuardState::onExit( StateExitType status )
{
	deleteInstanceOf(m_guardMachine);
	m_guardMachine = nullptr;

	Object *obj = getMachineOwner();
	obj->getAI()->clearGuardTargetType();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AITunnelNetworkGuardState::update()
{
	//DEBUG_LOG(("AITunnelNetworkGuardState frame %d: %08lx\n",TheGameLogic->getFrame(),getMachineOwner()));

	if (m_guardMachine == nullptr) 
	{
		return STATE_FAILURE; // We actually already exited.
	}

	// if all of our weapons are out of ammo, can't attack.
	// (this can happen for units which never auto-reload, like the Raptor)
	Object* owner = getMachineOwner();
	if (owner->isOutOfAmmo() && !owner->isKindOf(KINDOF_PROJECTILE))
	{
		DEBUG_CRASH(("Hmm, this should probably never happen, since this case should be intercepted by JetAIUpdate\n"));
		return STATE_FAILURE;
	}

	getMachine()->lock("AITunnelNetworkGuardState::update");	// We don't want to switch out of guard during the update.
	StateReturnType ret = m_guardMachine->updateStateMachine();
	getMachine()->unlock();
	return ret;
}
