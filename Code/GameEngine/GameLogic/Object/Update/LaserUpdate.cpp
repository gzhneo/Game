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

// FILE: LaserUpdate.cpp //////////////////////////////////////////////////////////////////////////
// Author: Kris Morness, July 2002
// Desc:   Handles laser update processing for render purposes and game control.
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include <map>
#include <utility>

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Xfer.h"
#include "Common/DrawModule.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/Object.h" 
#include "GameLogic/GameLogic.h" // For frame number
#include "GameLogic/Module/LaserUpdate.h"
#include "WWMath/vector3.h"

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
LaserUpdateModuleData::LaserUpdateModuleData()
{
	m_punchThroughScalar = 0.0f;
	m_parentFireBoneOnTurret = FALSE;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void LaserUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] = 
	{
		{ "MuzzleParticleSystem",		INI::parseAsciiString,	nullptr, offsetof( LaserUpdateModuleData, m_particleSystemName ) },
		{ "TargetParticleSystem",		INI::parseAsciiString,  nullptr, offsetof( LaserUpdateModuleData, m_targetParticleSystemName ) },
		{ "PunchThroughScalar",			INI::parseReal,					nullptr, offsetof( LaserUpdateModuleData, m_punchThroughScalar ) },
		// Generals fields; loading the Generals data needs them.
		{ "ParentFireBoneName",			INI::parseAsciiString,  nullptr, offsetof( LaserUpdateModuleData, m_parentFireBoneName ) },
		{ "ParentFireBoneOnTurret",	INI::parseBool,				nullptr, offsetof( LaserUpdateModuleData, m_parentFireBoneOnTurret ) },
		{ 0, 0, 0, 0 }
	};
	p.add(dataFieldParse);
}

// Live lasers per shooter/target pair: the newest laser drawable and the count. Client-side drawing state only.
namespace
{
	struct LaserPair
	{
		DrawableID newest = INVALID_DRAWABLE_ID;
		LaserUpdate *newestUpdate = nullptr;
		Int count = 0;
		Real brightness = 1.0f;
	};
	using LaserPairKey = std::pair<DrawableID, DrawableID>;
	using LaserPairMap = std::map<LaserPairKey, LaserPair>;

	// Allocated while lasers live and freed with the last one, so no memory is held at shutdown.
	LaserPairMap *s_laserPairs = nullptr;

	LaserPair *findLaserPair( DrawableID parent, DrawableID target )
	{
		if( !s_laserPairs )
			return nullptr;
		auto it = s_laserPairs->find( LaserPairKey( parent, target ) );
		return it != s_laserPairs->end() ? &it->second : nullptr;
	}

	LaserPair &addLaserPair( DrawableID parent, DrawableID target )
	{
		if( !s_laserPairs )
			s_laserPairs = new LaserPairMap;
		return ( *s_laserPairs )[ LaserPairKey( parent, target ) ];
	}

	void removeLaserPair( DrawableID parent, DrawableID target )
	{
		if( !s_laserPairs )
			return;
		s_laserPairs->erase( LaserPairKey( parent, target ) );
		if( s_laserPairs->empty() )
		{
			delete s_laserPairs;
			s_laserPairs = nullptr;
		}
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
LaserUpdate::LaserUpdate( Thing *thing, const ModuleData* moduleData ) : ClientUpdateModule( thing, moduleData )
{
	m_pairParentID = INVALID_DRAWABLE_ID;
	m_pairTargetID = INVALID_DRAWABLE_ID;
	//Added By Sadullah Nader
	//Initialization missing and needed
	m_dirty = FALSE;
	m_endPos.zero();
	m_startPos.zero();
	//
	m_particleSystemID = INVALID_PARTICLE_SYSTEM_ID;
	m_targetParticleSystemID = INVALID_PARTICLE_SYSTEM_ID;
	m_widening = false;
	m_widenStartFrame = 0;
	m_widenFinishFrame = 0;
	m_currentWidthScalar = 1.0f;
	m_decaying = false;
	m_decayStartFrame = 0;
	m_decayFinishFrame = 0;
	m_parentID = INVALID_DRAWABLE_ID;
	m_targetID = INVALID_DRAWABLE_ID;
	m_parentBoneName.clear();
} 

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
LaserUpdate::~LaserUpdate( void )
{
	if( m_pairParentID != INVALID_DRAWABLE_ID )
	{
		if( LaserPair *pair = findLaserPair( m_pairParentID, m_pairTargetID ) )
		{
			if( pair->newestUpdate == this )
				pair->newestUpdate = nullptr;
			if( --pair->count <= 0 )
				removeLaserPair( m_pairParentID, m_pairTargetID );
		}
	}

	if( m_particleSystemID )
		TheParticleSystemManager->destroyParticleSystemByID( m_particleSystemID );
	if( m_targetParticleSystemID )
		TheParticleSystemManager->destroyParticleSystemByID( m_targetParticleSystemID );

}

//-------------------------------------------------------------------------------------------------
void LaserUpdate::updateStartPos()
{
	Coord3D oldStartPos = m_startPos;

	if( m_parentID == INVALID_DRAWABLE_ID )
		return;// Can't update if not told to update

	const Drawable *parentDrawable = TheGameClient->findDrawableByID(m_parentID);
	if( parentDrawable == nullptr )
		return;// Can't update if no one to ask
		
	if( m_parentBoneName.isNotEmpty() )
	{
		Matrix3D startPosMatrix;
		
		if( !parentDrawable->getCurrentWorldspaceClientBonePositions( m_parentBoneName.str(), startPosMatrix ) )
		{
			// failed to find the required bone, so just die

			//Kris: Doing this CRASHES THE GAME LATER!!!! Instead, let's set the position to the drawable, then
			//      create a nasty assert.
			//TheGameClient->destroyDrawable( getDrawable() );
			
			m_startPos.set( parentDrawable->getPosition() );
			DEBUG_LOG( ("LaserUpdate::updateStartPos() -- Drawable %s is expecting to find a bone %s but can't. Defaulting to position of drawable.\n", 
				parentDrawable->getTemplate()->getName().str(), m_parentBoneName.str() ) );

			return;
		}

		

		m_startPos.x = startPosMatrix.Get_X_Translation();
		m_startPos.y = startPosMatrix.Get_Y_Translation();
		m_startPos.z = startPosMatrix.Get_Z_Translation();
	}
	else
	{
		// Just use parent position
		m_startPos = *parentDrawable->getPosition();
	}

	if( !(m_startPos == oldStartPos) ) // No != operator.  Heh.
		m_dirty = TRUE;
}

//-------------------------------------------------------------------------------------------------
void LaserUpdate::updateEndPos()
{
	const LaserUpdateModuleData *data = getLaserUpdateModuleData();
	Coord3D oldEndPos = m_endPos;

	if( m_targetID == INVALID_DRAWABLE_ID )
		return;// Can't update if not told to update

	const Drawable *targetDrawable = TheGameClient->findDrawableByID(m_targetID);
	Bool targetDead = (targetDrawable && targetDrawable->getObject()) 
										? targetDrawable->getObject()->isEffectivelyDead() 
										: FALSE;
	if( targetDrawable == nullptr || targetDead )
	{
		// If here, we used to track something, but now it is gone.  So make our end point pierce through
		// the old spot, and then stop trying to find a target Drawable
		if( data->m_punchThroughScalar > 0 )
		{
			Vector3 laserVector;
			laserVector.Set(m_endPos.x, m_endPos.y, m_endPos.z);
			laserVector = laserVector - Vector3(m_startPos.x, m_startPos.y, m_startPos.z);
			laserVector *= data->m_punchThroughScalar;
			laserVector = laserVector + Vector3(m_startPos.x, m_startPos.y, m_startPos.z);
			m_endPos.x = laserVector.X;
			m_endPos.y = laserVector.Y;
			m_endPos.z = laserVector.Z;
		}

		m_targetID = INVALID_DRAWABLE_ID;
	}
	else
	{
		m_endPos = *targetDrawable->getPosition();
	}

	if( !(m_endPos == oldEndPos) ) // No != operator.  Heh.
		m_dirty = TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool LaserUpdate::isSupersededInPair() const
{
	if( m_pairParentID == INVALID_DRAWABLE_ID )
		return FALSE;
	const LaserPair *pair = findLaserPair( m_pairParentID, m_pairTargetID );
	return pair && pair->newest != getDrawable()->getID();
}

//-------------------------------------------------------------------------------------------------
Real LaserUpdate::getPairBrightness() const
{
	if( m_pairParentID == INVALID_DRAWABLE_ID )
		return 1.0f;
	const LaserPair *pair = findLaserPair( m_pairParentID, m_pairTargetID );
	return pair ? pair->brightness : 1.0f;
}

//-------------------------------------------------------------------------------------------------
/** The update callback. */
//-------------------------------------------------------------------------------------------------
void LaserUpdate::clientUpdate( void )
{
	updateStartPos();
	updateEndPos();

	// A superseded laser of a stacked stream drops its muzzle and target flares; the newest one keeps them and
	// follows the number of live lasers of the pair slowly.
	if( m_pairParentID != INVALID_DRAWABLE_ID && !isSupersededInPair() )
	{
		if( LaserPair *pair = findLaserPair( m_pairParentID, m_pairTargetID ) )
			pair->brightness += ( static_cast<Real>( pair->count ) - pair->brightness ) * 0.1f;
	}
	else if( isSupersededInPair() )
	{
		if( m_particleSystemID )
			TheParticleSystemManager->destroyParticleSystemByID( m_particleSystemID );
		if( m_targetParticleSystemID )
			TheParticleSystemManager->destroyParticleSystemByID( m_targetParticleSystemID );
		m_particleSystemID = INVALID_PARTICLE_SYSTEM_ID;
		m_targetParticleSystemID = INVALID_PARTICLE_SYSTEM_ID;
	}

	if( m_decaying )
	{
		UnsignedInt now = TheGameLogic->getFrame();
		m_currentWidthScalar = 1.0f - static_cast<Real>(now - m_decayStartFrame) / static_cast<Real>(m_decayFinishFrame - m_decayStartFrame);
		m_dirty = true;
		if( m_currentWidthScalar <= 0.0f )
		{
			m_currentWidthScalar = 0.0f;

			//When decay is finished... delete the laser.
			//TheGameLogic->destroyObject( getObject() );
			return;
		}
	}
	else if( m_widening )
	{
		//We need to resize our laser width based on the growth ratio completed.
		UnsignedInt now = TheGameLogic->getFrame();
		m_currentWidthScalar = static_cast<Real>(now - m_widenStartFrame) / static_cast<Real>(m_widenFinishFrame - m_widenStartFrame);
		m_dirty = true;
		if( m_currentWidthScalar >= 1.0f )
		{
			m_currentWidthScalar = 1.0f;
			m_widening = false;
		}
	}
	return;
}

void LaserUpdate::setDecayFrames( UnsignedInt decayFrames )
{
	if( decayFrames > 0 )
	{
		m_decaying = true;
		m_decayStartFrame = TheGameLogic->getFrame();
		m_decayFinishFrame = m_decayStartFrame + decayFrames;
		m_currentWidthScalar = 1.0f;
	}
}


//-------------------------------------------------------------------------------------------------
void LaserUpdate::initLaser( const Object *parent, const Object *target, const Coord3D *startPos, const Coord3D *endPos, AsciiString parentBoneName, Int sizeDeltaFrames )
{
	const LaserUpdateModuleData *data = getLaserUpdateModuleData();
	ParticleSystem *system;
	if( sizeDeltaFrames > 0 )
	{
		m_widening = true;
		m_widenStartFrame = TheGameLogic->getFrame();
		m_widenFinishFrame = m_widenStartFrame + sizeDeltaFrames;
		m_currentWidthScalar = 0.0f;
	}
	else if( sizeDeltaFrames < 0 )
	{
		m_decaying = true;
		m_decayStartFrame = TheGameLogic->getFrame();
		m_decayFinishFrame = m_decayStartFrame - sizeDeltaFrames;
		m_currentWidthScalar = 1.0f;
	}

	// Write down the bone name override
	m_parentBoneName = parentBoneName;
	// The Generals data names the bone on the module (ParentFireBoneName), not on the weapon
	// (LaserBoneName). Zero Hour data does not set the field.
	if( m_parentBoneName.isEmpty() && data->m_parentFireBoneName.isNotEmpty() )
		m_parentBoneName = data->m_parentFireBoneName;

	//Record IDs if we have them, then figure out starting points
	if( parent )
	{
		// If a source object, use it
		if( parent->getDrawable() )
			m_parentID = parent->getDrawable()->getID();

		updateStartPos();
	}
	else if( startPos )
	{
		// or just use what they gave
		m_startPos = *startPos;
	}
	else
	{
		// if they gave nothing, then we are screwed
		TheGameClient->destroyDrawable( getDrawable() );
		return;
	}

	if( target && !endPos )
	{
		// If a target object, use it (unless we override it!)
		if( target->getDrawable() )
			m_targetID = target->getDrawable()->getID();

		m_endPos = *target->getPosition();
	}
	else if( endPos )
	{
		// just use what they gave, no override here 
		m_endPos = *endPos;
	}
	else
	{
		// if they gave nothing, then we are screwed
		TheGameClient->destroyDrawable( getDrawable() );
		return;
	}

	// Weapons pass the target position as well, so m_targetID stays unset; the pair uses the target's drawable.
	const DrawableID pairTargetID = ( target && target->getDrawable() ) ? target->getDrawable()->getID() : INVALID_DRAWABLE_ID;

	// A stacked stream continues the flares of the pair's newest laser instead of starting new ones, so they
	// (and the light they cast) do not restart with every shot.
	if( m_parentID != INVALID_DRAWABLE_ID && pairTargetID != INVALID_DRAWABLE_ID && !m_particleSystemID && !m_targetParticleSystemID )
	{
		const LaserPair *pair = findLaserPair( m_parentID, pairTargetID );
		if( pair && pair->newestUpdate && pair->newestUpdate != this )
		{
			LaserUpdate *previous = pair->newestUpdate;
			m_particleSystemID = previous->m_particleSystemID;
			m_targetParticleSystemID = previous->m_targetParticleSystemID;
			previous->m_particleSystemID = INVALID_PARTICLE_SYSTEM_ID;
			previous->m_targetParticleSystemID = INVALID_PARTICLE_SYSTEM_ID;
		}
	}

	// Create special particle systems
	//PLEASE NOTE You cannot check an ID for NULL.  This should be a check against INVALID_PARTICLE_SYSTEM_ID.  Can't change it on the last day without a bug though.
	if( !m_particleSystemID )
	{
		const Player *localPlayer = ThePlayerList->getLocalPlayer();

		//Make sure the laser flare is visible to the player. If no parent, assume laser owner will handle it.
		if (!parent || parent->getShroudedStatus( localPlayer->getPlayerIndex() ) <= OBJECTSHROUD_PARTIAL_CLEAR )
		{

			//If we don't have a particle system for the lense flare (muzzle flare), create it.
			if( data->m_particleSystemName.isNotEmpty() )
			{
				const ParticleSystemTemplate *tmp = TheParticleSystemManager->findTemplate( data->m_particleSystemName );
				if( tmp )
				{
					system = TheParticleSystemManager->createParticleSystem( tmp );
					if( system )
					{
						m_particleSystemID = system->getSystemID();
					}
				}
			}

			//If we don't have a particle system for the target effect, create it.
			if( data->m_targetParticleSystemName.isNotEmpty() )
			{
				const ParticleSystemTemplate *tmp = TheParticleSystemManager->findTemplate( data->m_targetParticleSystemName );
				if( tmp )
				{
					system = TheParticleSystemManager->createParticleSystem( tmp );
					if( system )
					{
						m_targetParticleSystemID = system->getSystemID();
					}
				}
			}
		}
	}

	//Adjust the position of any existing particle system.
	//PLEASE NOTE You cannot check an ID for NULL.  This should be a check against INVALID_PARTICLE_SYSTEM_ID.  Can't change it on the last day without a bug though.
	if( m_particleSystemID )
	{
		system = TheParticleSystemManager->findParticleSystem( m_particleSystemID );
		if( system )
		{
			system->setPosition( &m_startPos );
		}
	}
	
	//PLEASE NOTE You cannot check an ID for NULL.  This should be a check against INVALID_PARTICLE_SYSTEM_ID.  Can't change it on the last day without a bug though.
	if( m_targetParticleSystemID )
	{
		system = TheParticleSystemManager->findParticleSystem( m_targetParticleSystemID );
		if( system )
		{
			system->setPosition( &m_endPos );
		}
	}

	//Important! Set the laser position to the average of both points or else
	//it probably won't get rendered!!!
	// And as a client update, we cannot set the logic position.
	Coord3D posToUse;
	if( parent == nullptr )
	{
		posToUse.set( startPos );
		posToUse.add( endPos );
		posToUse.scale( 0.5 );
	}
	else
	{
		posToUse = *parent->getPosition();
	}

	Drawable *draw = getDrawable();
	if( draw )
	{
		draw->setPosition( &posToUse );
	}

	if( draw && m_parentID != INVALID_DRAWABLE_ID && pairTargetID != INVALID_DRAWABLE_ID && m_pairParentID == INVALID_DRAWABLE_ID )
	{
		m_pairParentID = m_parentID;
		m_pairTargetID = pairTargetID;
		LaserPair &pair = addLaserPair( m_pairParentID, m_pairTargetID );
		pair.newest = draw->getID();
		pair.newestUpdate = this;
		++pair.count;
	}

	m_dirty = true;
}

//-------------------------------------------------------------------------------------------------
Real LaserUpdate::getCurrentLaserRadius() const
{
	const Drawable *draw = getDrawable();
	const LaserDrawInterface* ldi = nullptr;
	for( const DrawModule** d = draw->getDrawModules(); *d; ++d )
	{
		ldi = (*d)->getLaserDrawInterface();
		if( ldi )
		{
			//***NOTE***
			//While it appears the logic is accessing client data, it is actually accessing template module
			//data from the client. This value is INI constant thus can't change. It's grouped with other 
			//laser defining attributes and having it there makes it easier for artists.
			return ldi->getLaserTemplateWidth() * m_currentWidthScalar;
		}
	}
	return 0.0f;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void LaserUpdate::crc( Xfer *xfer )
{

	// extend base class
	ClientUpdateModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void LaserUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	ClientUpdateModule::xfer( xfer );

	// start pos
	xfer->xferCoord3D( &m_startPos );

	// end pos
	xfer->xferCoord3D( &m_endPos );

	// dirty
	xfer->xferBool( &m_dirty );

	// particle system ID
	xfer->xferUser( &m_particleSystemID, sizeof( ParticleSystemID ) );

	// target particle system id
	xfer->xferUser( &m_targetParticleSystemID, sizeof( ParticleSystemID ) );

	// widening
	xfer->xferBool( &m_widening );

	// decaying
	xfer->xferBool( &m_decaying );

	// widen start frame
	xfer->xferUnsignedInt( &m_widenStartFrame );

	// widen finish frame
	xfer->xferUnsignedInt( &m_widenFinishFrame );

	// current width scalar
	xfer->xferReal( &m_currentWidthScalar );

	// decay start frame
	xfer->xferUnsignedInt( &m_decayStartFrame );

	// decay finish frame
	xfer->xferUnsignedInt( &m_decayFinishFrame );

	xfer->xferDrawableID(&m_parentID);
	xfer->xferDrawableID(&m_targetID);

	xfer->xferAsciiString(&m_parentBoneName);

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void LaserUpdate::loadPostProcess( void )
{

	// extend base class
	ClientUpdateModule::loadPostProcess();

}  // end loadPostProcess
