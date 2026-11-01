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
#include "Common/ActionManager.h"
#include "Common/DiscreteCircle.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Radar.h"
#include "Common/ThingFactory.h"	// for bullet type hack
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/Squad.h"
#include "GameLogic/GhostObject.h"
#include "GameClient/Line2D.h"
#include "GameClient/ControlBar.h"
#include "Common/PlayerList.h"
#include "Common/MapObject.h"

#include "PartitionManagerInternal.h"

// PartitionFilter implementations

//-----------------------------------------------------------------------------
PartitionFilterRejectBuildings::PartitionFilterRejectBuildings(const Object *o) : 
	m_self(o),
	m_acquireEnemies(false)
{ 
	// if I am a computer-controlled opponent, auto-aquire enemy buildings
	if (m_self->getControllingPlayer()->getPlayerType() == PLAYER_COMPUTER)
	{
		m_acquireEnemies = true;
	}
}

//-----------------------------------------------------------------------------
Bool PartitionFilterRejectBuildings::allow( Object *other )
{
	// this filter allows all non-buildings
	// um, no, it clearly doesn't any more.
	// didn't the person adding the code below read the comment?
	if (!other->isKindOf( KINDOF_STRUCTURE ))
		return true;

	const Player* myPlayer = m_self->getControllingPlayer();
	if (!myPlayer)
		return false;

	// Get the controlling team of other.
	ContainModuleInterface* contain = other->getContain();
	const Player* otherPlayer = contain ? contain->getApparentControllingPlayer(myPlayer) : nullptr;

	if (!otherPlayer)
		otherPlayer = other->getControllingPlayer();

	if (!otherPlayer)
		return false;

	Relationship relationship = myPlayer->getRelationship(otherPlayer->getDefaultTeam());
	if (relationship != ENEMIES)
		return false;

	// if I am a computer-controlled opponent, auto-aquire enemy buildings (if we can see them!)
	if (m_acquireEnemies)
		return true;

	if (other->isKindOf( KINDOF_FS_BASE_DEFENSE))
	{
		// Don't reject base defenses.
		return true;
	}

	if (other->getContain() != nullptr && other->isAbleToAttack())
	{
		// Don't reject garrisoned buildings that can attack
		return true;
  }

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Bool PartitionFilterFreeOfFog::allow( Object *other )
{
	return other->getShroudedStatus(m_comparisonIndex) == OBJECTSHROUD_CLEAR;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Bool PartitionFilterInsignificantBuildings::allow( Object *other )
{
	if (other->isStructure()) {
		if (other->isNonFactionStructure() && !m_allowInsignificant) {
			ContainModuleInterface *cmi = other->getContain();
			if (cmi) {
				if (!cmi->isGarrisonable() || cmi->getContainCount() == 0) {
					return false;
				}
			}
		}
		return true;
		
	} else {
		if (m_allowNonBuildings) {
			return true;
		}
	}

	return false;
}

Bool PartitionFilterRepulsor::allow( Object *other )
{
	if (other == m_self) 
	{
		// don't repulse yourself. :)
		return false;
	}

	// If it's flagged, it's a repulsor.
	if (other->testStatus(OBJECT_STATUS_REPULSOR)) 
	{
		return	true;
	}

	if (other->isEffectivelyDead()) 
		return false; // no dead enemies.

	Relationship r = m_self->getRelationship(other);
	if (r != ENEMIES) 
	{
		return false; // only enemies auto repulse.
	}

	if (other->isKindOf( KINDOF_STRUCTURE ))
	{
		// always pay attention to buildings that can attack
		if (other->isAbleToAttack())
			return true;
		return false;
	}
	
	if ( other->isKindOf( KINDOF_INERT ))
		return false;


	if ( ! other->isAbleToAttack() )
	{
		return false;
	}

	return true;
}

Bool PartitionFilterIrregularArea::allow( Object *other )
{

	return PointInsideArea2D(other->getPosition(), m_area, m_numPointsInArea);
}

//-----------------------------------------------------------------------------
Bool PartitionFilterPolygonTrigger::allow( Object *other )
{
	ICoord3D iPos;
	iPos.x = static_cast<Int>(other->getPosition()->x);
	iPos.y = static_cast<Int>(other->getPosition()->y);
	iPos.z = 0; // Trigger areas compare on xy only.
	return m_trigger->pointInTrigger(iPos);
}

Bool PartitionFilterPlayer::allow( Object *other )
{
	return ((m_player == other->getControllingPlayer()) == m_match);
}

//-----------------------------------------------------------------------------
Bool PartitionFilterPlayerAffiliation::allow( Object *other )
{
	Relationship rel = m_player->getRelationship(other->getTeam());
	switch (rel)
	{
		case ENEMIES:	
			if (m_affiliation & ALLOW_ENEMIES) {
				return m_match;
			}
			break;

		case NEUTRAL:
			if (m_affiliation & ALLOW_NEUTRAL) {
				return m_match;
			}
			break;

		case ALLIES:
			if (m_affiliation & ALLOW_ALLIES) {
				return m_match;
			}
			break;
	}

	if (other->getControllingPlayer() == m_player) {
		return m_match;		
	}

	return !m_match;
}

//-----------------------------------------------------------------------------
Bool PartitionFilterThing::allow( Object *other )
{
	return (m_tThing->isEquivalentTo(other->getTemplate()) == m_match);
}

//-----------------------------------------------------------------------------
Bool PartitionFilterGarrisonable::allow( Object *other )
{
	ContainModuleInterface *cmi = other->getContain();
	if (!cmi) {
		return !m_match;
	}

	return (cmi->isGarrisonable() == m_match);
}

//-----------------------------------------------------------------------------
Bool PartitionFilterGarrisonableByPlayer::allow( Object *other )
{
	return TheActionManager->canPlayerGarrison(m_player, other, m_commandSource) == m_match;
}

//-----------------------------------------------------------------------------
Bool PartitionFilterUnmannedObject::allow( Object *other )
{
	return (other->isDisabledByType( DISABLED_UNMANNED ) == m_match);
}

//-----------------------------------------------------------------------------
Bool PartitionFilterValidCommandButtonTarget::allow( Object *other )
{
	return (m_commandButton->isValidToUseOn(m_source, other, nullptr, m_commandSource) == m_match);
}

//-----------------------------------------------------------------------------
Bool PartitionFilterIsFlying::allow(Object *objOther)
{
	return objOther->isUsingAirborneLocomotor();
}

//-----------------------------------------------------------------------------
PartitionFilterWouldCollide::PartitionFilterWouldCollide(const Coord3D& pos, const GeometryInfo& geom, Real angle, Bool desired) :
  m_position(pos),
	m_geom(geom),
  m_angle(angle),
  m_desiredCollisionResult(desired)
{
}

Bool PartitionFilterWouldCollide::allow(Object *objOther)
{
	CollideInfo thisInfo(&m_position, m_geom, m_angle);
	CollideInfo thatInfo(objOther->getPosition(), objOther->getGeometryInfo(), objOther->getOrientation());

  Bool doesCollide;

	// invariant for all geometries: first do z collision check.
	if (thisInfo.position.z + thisInfo.geom.getMaxHeightAbovePosition() >= thatInfo.position.z && 
			thisInfo.position.z <= thatInfo.position.z + thatInfo.geom.getMaxHeightAbovePosition())
	{
		GeometryType thisGeom = m_geom.getGeomType();
		GeometryType thatGeom = objOther->getGeometryInfo().getGeomType();

		//
		// NOTE: This assumes geometry enumerations that start at GEOMETRY_FIRST AND depends on the
		// order in which they appear in the enum list
		//
		CollideTestProc collideProc = theCollideTestProcs[ (thisGeom - GEOMETRY_FIRST) * GEOMETRY_NUM_TYPES + (thatGeom - GEOMETRY_FIRST) ];
		CollideLocAndNormal cinfo;
		doesCollide = (*collideProc)(&thisInfo, &thatInfo, &cinfo);
	}
	else
	{
		// no z-intersection -> no collision.
		doesCollide = false;
	}

  return doesCollide == m_desiredCollisionResult;
}

//-----------------------------------------------------------------------------
Bool PartitionFilterSamePlayer::allow(Object *objOther)
{
	if (m_player == objOther->getControllingPlayer())
		return true;

	return false;
}

//-----------------------------------------------------------------------------
Bool PartitionFilterRelationship::allow(Object *objOther)
{
	Relationship r = m_obj->getRelationship(objOther);

	if ((m_flags & (1<<r)) != 0)
		return true;

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
PartitionFilterAcceptOnTeam::PartitionFilterAcceptOnTeam(const Team *team) : m_team(team)
{
}

//-----------------------------------------------------------------------------
Bool PartitionFilterAcceptOnTeam::allow(Object *objOther) 
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)
	return (objOther->getTeam() == m_team);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
PartitionFilterAcceptOnSquad::PartitionFilterAcceptOnSquad(const Squad *squad) : m_squad(squad)
{
}

Bool PartitionFilterAcceptOnSquad::allow(Object *objOther)
{
	return (m_squad && m_squad->isOnSquad(objOther) && !objOther->isEffectivelyDead());
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Bool PartitionFilterSameMapStatus::allow(Object* objOther)
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)
	return objOther->isOffMap() == m_obj->isOffMap();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Bool PartitionFilterOnMap::allow(Object* objOther)
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)
	return !objOther->isOffMap();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Bool PartitionFilterAlive::allow( Object *objOther )
{
	return !objOther->isEffectivelyDead();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
PartitionFilterRejectBehind::PartitionFilterRejectBehind( Object *obj )
{
	m_obj = obj;
}

Bool PartitionFilterRejectBehind::allow( Object *other )
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)

//const Coord3D *pos = m_obj->getPosition();
//const Coord3D *dir = m_obj->getUnitDirectionVector2D();
	Vector3 dir = m_obj->getTransformMatrix()->Get_X_Vector();
	dir.Normalize();
//const Coord3D *otherPos = other->getPosition();

	Coord3D v;
	ThePartitionManager->getVectorTo( m_obj, other, DistanceCalculationType::FROM_CENTER_3D, v );

	Real dot = dir.X * v.x + dir.Y * v.y + dir.Z * v.z;

	if (dot > 0.0f)
		return true;

	return false;
}

//-----------------------------------------------------------------------------
PartitionFilterLineOfSight::PartitionFilterLineOfSight(const Object *obj)
{
	m_obj = obj;
}

//-----------------------------------------------------------------------------
Bool PartitionFilterLineOfSight::allow(Object *objOther)
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)

	if (!ThePartitionManager->isClearLineOfSightTerrain(m_obj, *m_obj->getPosition(), objOther, *objOther->getPosition()))
		return false;

	if (TheAI && TheAI->pathfinder()->isViewBlockedByObstacle(m_obj, objOther))
		return false;

	return true;
}

//-----------------------------------------------------------------------------
PartitionFilterPossibleToAttack::PartitionFilterPossibleToAttack(AbleToAttackType t, const Object *obj, CommandSourceType commandSource) :
	m_attackType(t),
	m_obj(obj),
	m_commandSource(commandSource)
{
}

//-----------------------------------------------------------------------------
Bool PartitionFilterPossibleToAttack::allow(Object *objOther)
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)
	
	// we should have already filtered out isAbleToAttack!
#ifdef _DEBUG
	// disable this assert for INTERNAL builds (srj)
	// A dozer looks for mines to clear whatever its current status (findMine); only real attackers are checked.
	DEBUG_ASSERTCRASH(m_obj && (m_obj->isAbleToAttack() || m_commandSource == CMD_FROM_DOZER), ("if the object is unable to attack at all, you should filter that out ahead of time!"));
#endif
	CanAttackResult result = m_obj->getAbleToAttackSpecificObject( m_attackType, objOther, m_commandSource );
	if( result == CanAttackResult::ATTACKRESULT_POSSIBLE || result == CanAttackResult::ATTACKRESULT_POSSIBLE_AFTER_MOVING )
	{
		return TRUE;
	}
	return FALSE;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
PartitionFilterPossibleToEnter::PartitionFilterPossibleToEnter(const Object *obj, CommandSourceType commandSource) :
	m_obj(obj),
	m_commandSource(commandSource)
{
}

//-----------------------------------------------------------------------------
Bool PartitionFilterPossibleToEnter::allow(Object *objOther)
{
	if (!objOther || !m_obj) 
		return FALSE;

	if( TheActionManager->canEnterObject( m_obj, objOther, m_commandSource, CanEnterType::DONT_CHECK_CAPACITY ) )
		return TRUE;
	else
		return FALSE;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
PartitionFilterPossibleToHijack::PartitionFilterPossibleToHijack(const Object *obj, CommandSourceType commandSource) :
	m_obj(obj),
	m_commandSource(commandSource)
{
}

//-----------------------------------------------------------------------------
Bool PartitionFilterPossibleToHijack::allow(Object *objOther)
{
	if (!objOther || !m_obj) 
		return FALSE;

	if( TheActionManager->canHijackVehicle(m_obj, objOther, m_commandSource) )
		return TRUE;
	else
		return FALSE;
}

//-----------------------------------------------------------------------------
PartitionFilterLastAttackedBy::PartitionFilterLastAttackedBy(Object *obj) 
{
	if (obj && obj->getBodyModule()) {
		m_lastAttackedBy = obj->getBodyModule()->getLastDamageInfo()->in.m_sourceID;
	} else {
		m_lastAttackedBy = INVALID_ID;
	}
}

Bool PartitionFilterLastAttackedBy::allow(Object *other)
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)
	if (other->getID() == m_lastAttackedBy)
		return true;

	return false;
}

//-----------------------------------------------------------------------------
Bool PartitionFilterAcceptByObjectStatus::allow(Object *objOther)
{ 
	ObjectStatusMaskType status = objOther->getStatusBits();
	return status.testForAll( m_mustBeSet ) && status.testForNone( m_mustBeClear );
}

//-----------------------------------------------------------------------------
Bool PartitionFilterRejectByObjectStatus::allow(Object *objOther)
{ 
	ObjectStatusMaskType status = objOther->getStatusBits();

	return !( status.testForAll( m_mustBeSet ) && status.testForNone( m_mustBeClear ) );
}

//-----------------------------------------------------------------------------
Bool PartitionFilterAcceptByKindOf::allow(Object *objOther)
{
	return objOther->isKindOfMulti(m_mustBeSet, m_mustBeClear);
}

//-----------------------------------------------------------------------------
Bool PartitionFilterRejectByKindOf::allow(Object *objOther)
{
	return !objOther->isKindOfMulti(m_mustBeSet, m_mustBeClear); 
}

//-----------------------------------------------------------------------------
Bool PartitionFilterStealthedAndUndetected::allow( Object *objOther )
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)

	Bool stealthed = objOther->testStatus( OBJECT_STATUS_STEALTHED );
	Bool detected = objOther->testStatus( OBJECT_STATUS_DETECTED );
	Bool disguised = objOther->testStatus( OBJECT_STATUS_DISGUISED );

	if( stealthed && !detected )
	{
		if( !objOther->isKindOf( KINDOF_DISGUISER ) )
		{
			//We have a stealthed & undetected object.
			return m_allow;
		}
		else if( disguised )
		{
			//Exception case -- bomb trucks can't be considered stealthed units when they are disguised as the enemy.
      
      StealthUpdate *update = objOther->getStealth();

			if( update && update->isDisguised() )
			{
				Player *ourPlayer = m_obj->getControllingPlayer();
				Player *otherPlayer = ThePlayerList->getNthPlayer( update->getDisguisedPlayerIndex() );
				if( ourPlayer && otherPlayer )
				{
					if( ourPlayer->getRelationship( otherPlayer->getDefaultTeam() ) == ENEMIES )
					{
						//Our stealthed & undetected object is disguised as a unit perceived to be our enemy,
						//therefore it's not considered to be stealthed!
						return !m_allow;
					}
					//Our object is disguised as a "friendly" object, therefore it's considered to be stealthed.
					return m_allow;
				}
			}
			//Our bomb truck is not disguised therefore it's not stealthed.
			return !m_allow;
		}
	}
	else
	{
		//This handles neutral containers that hold stealth units. This specifically fixes a bug where hunt scripts would ignore
		//this case -- units would acquire the building Jarmen Kell occupied even though it was not stealth detected.
		const ContainModuleInterface* contain = objOther->getContain();
		if( contain )
		{
			const Player* victimApparentController = contain->getApparentControllingPlayer( m_obj->getControllingPlayer() );
			//Check if it's stealthed!
			// Check the count first: for an empty container 0 == 0, and begin() == end() would be
			// dereferenced below.
			if( contain->getContainCount() > 0 && contain->getStealthUnitsContained() == contain->getContainCount() )
			{
				//Check if the first object inside is detected (if one is detected, all are detected).
				ContainedItemsList::const_iterator it = contain->getContainedItemsList()->begin();
				Object *member = (*it);
				if( member && !(*it)->getStatusBits().test( OBJECT_STATUS_DETECTED ) )
				{
					//Finally check the relationship!
					if( victimApparentController && m_obj->getTeam()->getRelationship( victimApparentController->getDefaultTeam() ) == ENEMIES )
					{
						//Our object is a neutral building garrisoned by enemy units we can't see, therefore it is stealthed.
						return m_allow;
					}
				}
			}
		}
	}

	//The unit is not stealthed!
	return !m_allow;
}
