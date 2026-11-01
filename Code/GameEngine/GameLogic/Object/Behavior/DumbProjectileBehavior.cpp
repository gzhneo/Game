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

// FILE: DumbProjectileBehavior.cpp
// Author: Steven Johnson, July 2002
// Desc:   

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/BezierSegment.h"
#include "Common/GameCommon.h"
#include "Common/GameState.h"
#include "Common/Player.h"
#include "Common/ThingTemplate.h"
#include "Common/RandomValue.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameClient/FXList.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/DumbProjectileBehavior.h"
#include "GameLogic/Module/MissileAIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Weapon.h"

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
const Int DEFAULT_MAX_LIFESPAN = 10 * LOGICFRAMES_PER_SECOND;

//-----------------------------------------------------------------------------
DumbProjectileBehaviorModuleData::DumbProjectileBehaviorModuleData() : 
	m_maxLifespan(DEFAULT_MAX_LIFESPAN),
	m_detonateCallsKill(FALSE),
	m_orientToFlightPath(TRUE),
	m_tumbleRandomly(FALSE),
	m_firstHeight(0.0f),
	m_secondHeight(0.0f),
	m_firstPercentIndent(0.0f),
	m_secondPercentIndent(0.0f),	
	m_garrisonHitKillCount(0),
	m_garrisonHitKillFX(nullptr),
	m_flightPathAdjustDistPerFrame(0.0f)
{
}

//-----------------------------------------------------------------------------
void DumbProjectileBehaviorModuleData::buildFieldParse(MultiIniFieldParse& p)
{
  UpdateModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] = 
	{
		{ "MaxLifespan", INI::parseDurationUnsignedInt, nullptr, offsetof( DumbProjectileBehaviorModuleData, m_maxLifespan ) },
		{ "TumbleRandomly", INI::parseBool, nullptr, offsetof( DumbProjectileBehaviorModuleData, m_tumbleRandomly ) },
		{ "DetonateCallsKill", INI::parseBool, nullptr, offsetof( DumbProjectileBehaviorModuleData, m_detonateCallsKill ) },
		{ "OrientToFlightPath", INI::parseBool, nullptr, offsetof( DumbProjectileBehaviorModuleData, m_orientToFlightPath ) },

		{ "FirstHeight",					INI::parseReal,						nullptr, offsetof( DumbProjectileBehaviorModuleData, m_firstHeight ) },
		{ "SecondHeight",					INI::parseReal,						nullptr, offsetof( DumbProjectileBehaviorModuleData, m_secondHeight ) },
		{ "FirstPercentIndent",		INI::parsePercentToReal,	nullptr, offsetof( DumbProjectileBehaviorModuleData, m_firstPercentIndent ) },
		{ "SecondPercentIndent",	INI::parsePercentToReal,	nullptr, offsetof( DumbProjectileBehaviorModuleData, m_secondPercentIndent ) },

		{ "GarrisonHitKillRequiredKindOf", KindOfMaskType::parseFromINI, nullptr, offsetof( DumbProjectileBehaviorModuleData, m_garrisonHitKillKindof ) },
		{ "GarrisonHitKillForbiddenKindOf", KindOfMaskType::parseFromINI, nullptr, offsetof( DumbProjectileBehaviorModuleData, m_garrisonHitKillKindofNot ) },
		{ "GarrisonHitKillCount", INI::parseUnsignedInt, nullptr, offsetof( DumbProjectileBehaviorModuleData, m_garrisonHitKillCount ) },
		{ "GarrisonHitKillFX", INI::parseFXList, nullptr, offsetof( DumbProjectileBehaviorModuleData, m_garrisonHitKillFX ) },

		{ "FlightPathAdjustDistPerSecond", INI::parseVelocityReal, nullptr, offsetof( DumbProjectileBehaviorModuleData, m_flightPathAdjustDistPerFrame ) },


		{ 0, 0, 0, 0 }
	};

  p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
DumbProjectileBehavior::DumbProjectileBehavior( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	m_launcherID = INVALID_ID;
	m_victimID = INVALID_ID;
	m_detonationWeaponTmpl = nullptr;
	m_lifespanFrame = 0;
	m_flightPath.clear();
	m_flightPathSegments = 0;
	m_flightPathSpeed = 0;
	m_flightPathStart.zero();
	m_flightPathEnd.zero();
	m_currentFlightPathStep = 0;
	m_extraBonusFlags = 0;

  m_hasDetonated = FALSE;
} 

//-------------------------------------------------------------------------------------------------
DumbProjectileBehavior::~DumbProjectileBehavior()
{
}

//-------------------------------------------------------------------------------------------------
inline Bool within(Real min, Real val, Real max)
{
	return min <= val && val <= max;
}

//-------------------------------------------------------------------------------------------------
inline Bool notWithin(Real min, Real val, Real max)
{
	return val < min || val > max;
}

//-------------------------------------------------------------------------------------------------
// Prepares the missile for launch via proper weapon-system channels.
//-------------------------------------------------------------------------------------------------
void DumbProjectileBehavior::projectileLaunchAtObjectOrPosition(
	const Object* victim, 
	const Coord3D* victimPos, 
	const Object* launcher, 
	WeaponSlotType wslot, 
	Int specificBarrelToUse, 
	const WeaponTemplate* detWeap,
	const ParticleSystemTemplate* exhaustSysOverride
)
{
	const DumbProjectileBehaviorModuleData* d = getDumbProjectileBehaviorModuleData();

	DEBUG_ASSERTCRASH(specificBarrelToUse>=0, ("specificBarrelToUse must now be explicit"));

	m_launcherID = launcher ? launcher->getID() : INVALID_ID;
	m_extraBonusFlags = launcher ? launcher->getWeaponBonusCondition() : 0;
	m_victimID = victim ? victim->getID() : INVALID_ID;
	m_detonationWeaponTmpl = detWeap;
	m_lifespanFrame = TheGameLogic->getFrame() + d->m_maxLifespan;

	Object* projectile = getObject();

	Weapon::positionProjectileForLaunch(projectile, launcher, wslot, specificBarrelToUse);

	projectileFireAtObjectOrPosition( victim, victimPos, detWeap, exhaustSysOverride );
}

//-------------------------------------------------------------------------------------------------
// The actual firing of the missile once setup. Uses a Bezier curve with points parameterized in ini
//-------------------------------------------------------------------------------------------------
void DumbProjectileBehavior::projectileFireAtObjectOrPosition( const Object *victim, const Coord3D *victimPos, const WeaponTemplate *detWeap, const ParticleSystemTemplate* exhaustSysOverride )
{
	const DumbProjectileBehaviorModuleData* d = getDumbProjectileBehaviorModuleData();
	Object* projectile = getObject();
	Real weaponSpeed = detWeap ? detWeap->getWeaponSpeed() : 0.0f;
	Real minWeaponSpeed = detWeap ? detWeap->getMinWeaponSpeed() : 0.0f;

	// if an object, aim at the center, not the ground part
	Coord3D victimPosToUse;
	if (victim)
		victim->getGeometryInfo().getCenterPosition(*victim->getPosition(), victimPosToUse);
	else
		victimPosToUse = *victimPos;

	if( detWeap  &&  detWeap->isScaleWeaponSpeed() )
	{
		// Some weapons want to scale their start speed to the range
		Real minRange = detWeap->getMinimumAttackRange();
		Real maxRange = detWeap->getUnmodifiedAttackRange();
		Real range = sqrt(ThePartitionManager->getDistanceSquared( projectile, &victimPosToUse, DistanceCalculationType::FROM_CENTER_2D ) );
		Real rangeRatio = (range - minRange) / (maxRange - minRange);
		m_flightPathSpeed = (rangeRatio * (weaponSpeed - minWeaponSpeed)) + minWeaponSpeed;
	}
	else
	{
		m_flightPathSpeed = weaponSpeed;
	}
	
 	PhysicsBehavior* physics = projectile->getPhysics();
 	if ( d->m_tumbleRandomly && physics)
	{
		physics->setPitchRate( GameLogicRandomValueReal( -1.0f/PI, 1.0f/PI ) );
		physics->setYawRate( GameLogicRandomValueReal( -1.0f/PI, 1.0f/PI ) );
		physics->setRollRate( GameLogicRandomValueReal( -1.0f/PI, 1.0f/PI ) );
	}

	m_flightPathStart = *getObject()->getPosition();
	m_flightPathEnd = victimPosToUse;
	if (!calcFlightPath(true))
	{
		//Can only fail if wildly incorrect points
		TheGameLogic->destroyObject( projectile );
		return;
	}
	m_currentFlightPathStep = 0;// We are at the first point, because the launching put us there
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool DumbProjectileBehavior::calcFlightPath(Bool recalcNumSegments)
{
	const DumbProjectileBehaviorModuleData* d = getDumbProjectileBehaviorModuleData();

	Coord3D controlPoints[4];
	//First point is us, last point is them
	controlPoints[0] = m_flightPathStart;
	controlPoints[3] = m_flightPathEnd;

	Real highestInterveningTerrain;
	Bool onMap = ThePartitionManager->estimateTerrainExtremesAlongLine( controlPoints[0], controlPoints[3], nullptr, &highestInterveningTerrain, nullptr, nullptr );
	if( !onMap )
	{
		return false;
	}

	// X and Y for inner points are along the line between us, so normalize and scale a vector between us, but
	// only use the x and y of the result
	Vector3 targetVector;// 0 origin vector between me and him
	targetVector.X = controlPoints[3].x - controlPoints[0].x;
	targetVector.Y = controlPoints[3].y - controlPoints[0].y;
	targetVector.Z = controlPoints[3].z - controlPoints[0].z;

	Real targetDistance = targetVector.Length();
	targetVector.Normalize();
	Vector3 firstPointAlongLine = targetVector * (targetDistance * d->m_firstPercentIndent );
	Vector3 secondPointAlongLine = targetVector * (targetDistance * d->m_secondPercentIndent );

	controlPoints[1].x = firstPointAlongLine.X + controlPoints[0].x;// add world start to offset along the origin based vector
	controlPoints[1].y = firstPointAlongLine.Y + controlPoints[0].y;
	controlPoints[2].x = secondPointAlongLine.X + controlPoints[0].x;
	controlPoints[2].y = secondPointAlongLine.Y + controlPoints[0].y;

	// Z's are determined using the highest intervening height so they won't hit hills, low end bounded by current Zs
	highestInterveningTerrain = max( highestInterveningTerrain, controlPoints[0].z );
	highestInterveningTerrain = max( highestInterveningTerrain, controlPoints[3].z );
	controlPoints[1].z = highestInterveningTerrain + d->m_firstHeight;
	controlPoints[2].z = highestInterveningTerrain + d->m_secondHeight;

	// With four control points, we have a curve.  We will decide how many frames we want to take to get to the target,
	// and fill our vector with those curve points.
	BezierSegment flightCurve( controlPoints );
	if (recalcNumSegments)
	{
		Real flightDistance = flightCurve.getApproximateLength();
		m_flightPathSegments = static_cast<Int>(ceil( flightDistance / m_flightPathSpeed ));
	}
	flightCurve.getSegmentPoints( m_flightPathSegments, &m_flightPath );
	DEBUG_ASSERTCRASH(m_flightPathSegments == m_flightPath.size(), ("m_flightPathSegments mismatch"));

#if defined(_DEBUG)
	if( TheGlobalData->m_debugProjectilePath )
		displayFlightPath();
#endif

	return true;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool DumbProjectileBehavior::projectileHandleCollision( Object *other )
{
	const DumbProjectileBehaviorModuleData* d = getDumbProjectileBehaviorModuleData();

	if (other != nullptr)
	{
		Object *projectileLauncher = TheGameLogic->findObjectByID( projectileGetLauncherID() );

			// if it's not the specific thing we were targeting, see if we should incidentally collide...
		if (!m_detonationWeaponTmpl->shouldProjectileCollideWith(projectileLauncher, getObject(), other, m_victimID))
		{
			//DEBUG_LOG(("ignoring projectile collision with %s at frame %d\n",other->getTemplate()->getName().str(),TheGameLogic->getFrame()));
			return true;
		}

		if (d->m_garrisonHitKillCount > 0)
		{
			ContainModuleInterface* contain = other->getContain();
			if( contain && contain->getContainCount() > 0 && contain->isGarrisonable() && !contain->isImmuneToClearBuildingAttacks() )
			{
				Int numKilled = 0;

				// garrisonable buildings subvert the normal process here.
				const ContainedItemsList* items = contain->getContainedItemsList();
				if (items)
				{
					// also stop at the list end: there may be fewer killable occupants than GarrisonHitKillCount
					for (ContainedItemsList::const_iterator it = items->begin(); it != items->end() && *it != nullptr && numKilled < d->m_garrisonHitKillCount; )
					{
						Object* thingToKill = *it++;
						if (!thingToKill->isEffectivelyDead() && thingToKill->isKindOfMulti(d->m_garrisonHitKillKindof, d->m_garrisonHitKillKindofNot))
						{
							//DEBUG_LOG(("Killed a garrisoned unit (%08lx %s) via Flash-Bang!\n",thingToKill,thingToKill->getTemplate()->getName().str()));
							if (projectileLauncher)
								projectileLauncher->scoreTheKill( thingToKill );
							thingToKill->kill();
							++numKilled;
						}
					} // next contained item
				} // if items
				
				if (numKilled > 0)
				{
					// note, fx is played at center of building, not at grenade's location
					FXList::doFXObj(d->m_garrisonHitKillFX, other, nullptr);

					getObject()->getControllingPlayer()->getAcademyStats()->recordClearedGarrisonedBuilding();

					// don't do the normal explosion; just destroy ourselves & return
					TheGameLogic->destroyObject(getObject());

					return true;
				}
			}	// if a garrisonable thing
		}

	}

	// collided with something... blow'd up!
	detonate();

	// mark ourself as "no collisions" (since we might still exist in slow death mode)
	getObject()->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_NO_COLLISIONS ) );
	return true;
}

//-------------------------------------------------------------------------------------------------
void DumbProjectileBehavior::detonate()
{
  if ( m_hasDetonated )
    return;

	Object* obj = getObject();
	if (m_detonationWeaponTmpl)
	{
		TheWeaponStore->handleProjectileDetonation(m_detonationWeaponTmpl, obj, obj->getPosition(), m_extraBonusFlags);

		if ( getDumbProjectileBehaviorModuleData()->m_detonateCallsKill )
		{
			// don't call kill(); do it manually, so we can specify DEATH_DETONATED
			DamageInfo damageInfo;
			damageInfo.in.m_damageType = DAMAGE_UNRESISTABLE;
			damageInfo.in.m_deathType = DEATH_DETONATED;
			damageInfo.in.m_sourceID = INVALID_ID;
			damageInfo.in.m_amount = obj->getBodyModule()->getMaxHealth();
			obj->attemptDamage( &damageInfo );
		}
		else
		{
			TheGameLogic->destroyObject( obj );
		}

	}
	else
	{
		// don't call kill(); do it manually, so we can specify DEATH_DETONATED
		DamageInfo damageInfo;
		damageInfo.in.m_damageType = DAMAGE_UNRESISTABLE;
		damageInfo.in.m_deathType = DEATH_DETONATED;
		damageInfo.in.m_sourceID = INVALID_ID;
		damageInfo.in.m_amount = obj->getBodyModule()->getMaxHealth();
		obj->attemptDamage( &damageInfo );
	}

	if (obj->getDrawable())
		obj->getDrawable()->setDrawableHidden(true);
  
  m_hasDetonated = TRUE; 

}

//-------------------------------------------------------------------------------------------------
/**
 * Simulate one frame of a missile's behavior
 */
UpdateSleepTime DumbProjectileBehavior::update()
{
	const DumbProjectileBehaviorModuleData* d = getDumbProjectileBehaviorModuleData();

	if (m_lifespanFrame != 0 && TheGameLogic->getFrame() >= m_lifespanFrame)
	{
		// lifetime demands detonation
		detonate();
		return UPDATE_SLEEP_NONE;
	}

	if( m_currentFlightPathStep >= m_flightPath.size() )
	{
		// No more steps to use. Would go out of bounds on vector, so have to do something.
		// We could allow physics to take over and make us fall, but the point of this whole task
		// is to guarentee where the shell explodes.  This way, it _will_ explode at the target point.
		detonate();
		return UPDATE_SLEEP_NONE;
	}

	if (m_victimID != INVALID_ID && d->m_flightPathAdjustDistPerFrame > 0.0f)
	{
		Object* victim = TheGameLogic->findObjectByID(m_victimID);
		if (victim)
		{
			Coord3D newVictimPos;
			victim->getGeometryInfo().getCenterPosition(*victim->getPosition(), newVictimPos);
			Coord3D delta;
			delta.x = newVictimPos.x - m_flightPathEnd.x;
			delta.y = newVictimPos.y - m_flightPathEnd.y;
			delta.z = newVictimPos.z - m_flightPathEnd.z;
			Real distVictimMovedSqr = sqr(delta.x) + sqr(delta.y) + sqr(delta.z);
			if (distVictimMovedSqr > 0.1f)
			{
				Real distVictimMoved = sqrtf(distVictimMovedSqr);
				if (distVictimMoved > d->m_flightPathAdjustDistPerFrame)
					distVictimMoved = d->m_flightPathAdjustDistPerFrame;
				delta.normalize();
				m_flightPathEnd.x += distVictimMoved * delta.x;
				m_flightPathEnd.y += distVictimMoved * delta.y;
				m_flightPathEnd.z += distVictimMoved * delta.z;
				if (!calcFlightPath(false))
				{
					DEBUG_CRASH(("Hmm, recalc of flight path returned false... should this happen?"));
					detonate();
					return UPDATE_SLEEP_NONE;
				}
			}
		}
	}

	//Otherwise, continue to force the flight path
	Coord3D flightStep = m_flightPath[m_currentFlightPathStep];

	if (d->m_orientToFlightPath && (!d->m_tumbleRandomly) )
  {
    if ( m_currentFlightPathStep > 0)
	  {
	  // this seems reasonable; however, if this object has a PhysicsBehavior on it, this calc will be wrong, 
	  // since Physics is applying gravity, which we duly ignore, but the prevPos won't be what we expect.
	  // get it from the flight path instead. (srj)
	  //Coord3D prevPos = *getObject()->getPosition();

		  Coord3D prevPos = m_flightPath[m_currentFlightPathStep - 1];

		  Vector3 curDir(flightStep.x - prevPos.x, flightStep.y - prevPos.y, flightStep.z - prevPos.z);
		  curDir.Normalize();	// buildTransformMatrix wants it this way
      Matrix3D orientMtx;
		  orientMtx.buildTransformMatrix(Vector3(flightStep.x, flightStep.y, flightStep.z), curDir);
		  getObject()->setTransformMatrix(&orientMtx);
    }
    else // oops! how do we orient the projectile on the zeroeth frame? This didn't matter until we started using the
      //long, blurry projectile graphics which look badly oriented on step 0 of the flight path
      // so lets orient it the same as if it were on frame 1!
    {
		  Coord3D prevPos = m_flightPath[0];
		  Coord3D curPos = m_flightPath[1];

		  Vector3 curDir(curPos.x - prevPos.x, curPos.y - prevPos.y, curPos.z - prevPos.z);
		  curDir.Normalize();	// buildTransformMatrix wants it this way
      Matrix3D orientMtx;
		  orientMtx.buildTransformMatrix(Vector3(flightStep.x, flightStep.y, flightStep.z), curDir);
		  getObject()->setTransformMatrix(&orientMtx);
    }

	}
	else
	{
		getObject()->setPosition(&flightStep);
	}

	// note that we want to use getHighestLayerForDestination() here, so that anything even slightly
	// below the bridge translates into GROUND. (getLayerForDestination just does a "closest" check)
	PathfindLayerEnum oldLayer = getObject()->getLayer();
	PathfindLayerEnum newLayer = TheTerrainLogic->getHighestLayerForDestination(getObject()->getPosition());
	getObject()->setLayer(newLayer);

	if (oldLayer != LAYER_GROUND && newLayer == LAYER_GROUND)
	{
		// see if we' still in the bridge's xy area
		Coord3D tmp = *getObject()->getPosition();
		tmp.z = 9999.0f;
		PathfindLayerEnum testLayer = TheTerrainLogic->getHighestLayerForDestination(&tmp);
		if (testLayer == oldLayer)
		{
			// ensure we are slightly above the bridge, to account for fudge & sloppy art
			const Real FUDGE = 2.0f;
			tmp.z = TheTerrainLogic->getLayerHeight(tmp.x, tmp.y, testLayer) + FUDGE;
			getObject()->setPosition(&tmp);
			// blow'd up!
			detonate();
			return UPDATE_SLEEP_NONE;
		}
	}

	++m_currentFlightPathStep;

	return UPDATE_SLEEP_NONE;//This no longer flys with physics, so it needs to not sleep
}

// ------------------------------------------------------------------------------------------------
/** displayFlightPath for debugging */
// ------------------------------------------------------------------------------------------------
#if defined(_DEBUG)
void DumbProjectileBehavior::displayFlightPath()
{
	extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
	for( Int pointIndex = 0; pointIndex < m_flightPath.size(); ++pointIndex )
	{
		addIcon(&m_flightPath[pointIndex], TheGlobalData->m_debugProjectileTileWidth, 
										TheGlobalData->m_debugProjectileTileDuration, 
										TheGlobalData->m_debugProjectileTileColor);
	}
}
#endif

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void DumbProjectileBehavior::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void DumbProjectileBehavior::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// launcher
	xfer->xferObjectID( &m_launcherID );

	// victim ID
	xfer->xferObjectID( &m_victimID );

	xfer->xferInt( &m_flightPathSegments );
	xfer->xferReal( &m_flightPathSpeed );
	xfer->xferCoord3D( &m_flightPathStart );
	xfer->xferCoord3D( &m_flightPathEnd );

	// weapon template
	AsciiString weaponTemplateName = AsciiString::TheEmptyString;
	if( m_detonationWeaponTmpl )
		weaponTemplateName = m_detonationWeaponTmpl->getName();
	xfer->xferAsciiString( &weaponTemplateName );
	if( xfer->getXferMode() == XFER_LOAD )
	{

		if( weaponTemplateName == AsciiString::TheEmptyString )
			m_detonationWeaponTmpl = nullptr;
		else
		{

			// find template
			m_detonationWeaponTmpl = TheWeaponStore->findWeaponTemplate( weaponTemplateName );

			// sanity
			if( m_detonationWeaponTmpl == nullptr )
			{

				DEBUG_CRASH(( "DumbProjectileBehavior::xfer - Unknown weapon template '%s'\n",
											weaponTemplateName.str() ));
				throw SaveCode::SC_INVALID_DATA;

			}  // end if

		}  // end else

	}  // end if

	// lifespan frame
	xfer->xferUnsignedInt( &m_lifespanFrame );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void DumbProjectileBehavior::loadPostProcess( void )
{

	// extend base class
	UpdateModule::loadPostProcess();

}  // end loadPostProcess
