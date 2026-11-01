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

#include "Common/GameState.h"
#include "Common/INI.h"
#include "Common/PerfTimer.h"
#include "Common/ThingFactory.h"
#include "Common/GameLOD.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameClient/DebugDisplay.h"
#include "GameClient/Display.h"
#include "GameClient/GameClient.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"

// Particles: saving and loading -- particle, system, system info and manager

// ------------------------------------------------------------------------------------------------
/** The key frames being approached, for a state whose target keys and rates are set (loading) */
// ------------------------------------------------------------------------------------------------
static void refreshNextKeyFrames( ParticleState &s, const ParticleKeys &k )
{
	const Bool alphaInRange = s.alphaTargetKey >= 1 && s.alphaTargetKey < MAX_KEYFRAMES;
	s.nextAlphaFrame = alphaInRange ? k.alphaKey[ s.alphaTargetKey ].frame : 0;
	const Bool colorInRange = s.colorTargetKey >= 1 && s.colorTargetKey < MAX_KEYFRAMES;
	s.nextColorFrame = colorInRange ? k.colorKey[ s.colorTargetKey ].frame : 0;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void ParticleInfo::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void ParticleInfo::xfer( Xfer *xfer )
{
	Int i;

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// velocity
	xfer->xferCoord3D( &m_vel );

	// position
	xfer->xferCoord3D( &m_pos );

	// emitter position
	xfer->xferCoord3D( &m_emitterPos );

	// velocity damping
	xfer->xferReal( &m_velDamping );

	// angle
	Real tempAngle=0;	//temporary value to save out for backwards compatibility when we supported x,y
	xfer->xferReal( &tempAngle );
	xfer->xferReal( &tempAngle );
	xfer->xferReal( &m_angleZ );

	// angular rate
	xfer->xferReal( &tempAngle );
	xfer->xferReal( &tempAngle );
	xfer->xferReal( &m_angularRateZ );

	// lifetime
	xfer->xferUnsignedInt( &m_lifetime );

	// size
	xfer->xferReal( &m_size );

	// size rate
	xfer->xferReal( &m_sizeRate );

	// size rate damping
	xfer->xferReal( &m_sizeRateDamping );

	// alpha keys
	for( i = 0; i < MAX_KEYFRAMES; ++i )
	{

		xfer->xferReal( &m_alphaKey[ i ].value );
		xfer->xferUnsignedInt( &m_alphaKey[ i ].frame );

	}  // end for, i

	// color keys
	for( i = 0; i < MAX_KEYFRAMES; ++i )
	{

		xfer->xferRGBColor( &m_colorKey[ i ].color );
		xfer->xferUnsignedInt( &m_colorKey[ i ].frame );

	}  // end for, i

	// color scale
	xfer->xferReal( &m_colorScale );

	// particle up towards emitter
	xfer->xferBool( &m_particleUpTowardsEmitter );

	// wind randomness
	xfer->xferReal( &m_windRandomness );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void ParticleInfo::loadPostProcess( void )
{

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void Particle::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void Particle::xfer( Xfer *xfer )
{

	// version 
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	ParticleState &s = m_system->getParticleState( this );
	ParticleKeys &k = m_system->getParticleKeys( this );

	// base class particle info (the format from when Particle was derived from ParticleInfo)
	ParticleInfo info;
	info.m_vel = s.vel;
	info.m_pos = s.pos;
	info.m_emitterPos = s.emitterPos;
	info.m_velDamping = s.velDamping;
	info.m_angleZ = s.angleZ;
	info.m_angularRateZ = s.angularRateZ;
	info.m_lifetime = k.lifetime;
	info.m_size = s.size;
	info.m_sizeRate = s.sizeRate;
	info.m_sizeRateDamping = s.sizeRateDamping;
	for( Int i = 0; i < MAX_KEYFRAMES; ++i )
	{
		info.m_alphaKey[ i ] = k.alphaKey[ i ];
		info.m_colorKey[ i ] = k.colorKey[ i ];
	}
	info.m_colorScale = s.colorScale;
	info.m_particleUpTowardsEmitter = s.upTowardsEmitter;
	info.m_windRandomness = s.windRandomness;
	info.xfer( xfer );

	// personality
	xfer->xferUnsignedInt( &s.personality );

	// acceleration
	xfer->xferCoord3D( &s.accel );

	// last position (never used)
	Coord3D lastPos;
	lastPos.zero();
	xfer->xferCoord3D( &lastPos );

	// lifetime left
	xfer->xferUnsignedInt( &s.lifetimeLeft );

	// creation timestamp
	xfer->xferUnsignedInt( &s.createTimestamp );

	// alpha
	xfer->xferReal( &s.alpha );

	// alpha rate
	xfer->xferReal( &s.alphaRate );

	// alpha target key
	xfer->xferInt( &s.alphaTargetKey );

	// color
	xfer->xferRGBColor( &s.color );

	// color rate
	xfer->xferRGBColor( &s.colorRate );

	// color target key
	xfer->xferInt( &s.colorTargetKey );

	// drawable
	DrawableID drawableID = INVALID_DRAWABLE_ID;
	xfer->xferDrawableID( &drawableID );	//saving for backwards compatibility when we supported drawables.

	// system under control as an id
	ParticleSystemID systemUnderControlID = m_systemUnderControl ? m_systemUnderControl->getSystemID() : INVALID_PARTICLE_SYSTEM_ID;
	xfer->xferUser( &systemUnderControlID, sizeof( ParticleSystemID ) );

	if( xfer->getXferMode() == XFER_LOAD )
	{
		s.vel = info.m_vel;
		s.pos = info.m_pos;
		s.emitterPos = info.m_emitterPos;
		s.velDamping = info.m_velDamping;
		s.angleZ = info.m_angleZ;
		s.angularRateZ = info.m_angularRateZ;
		k.lifetime = info.m_lifetime;
		s.size = info.m_size;
		s.sizeRate = info.m_sizeRate;
		s.sizeRateDamping = info.m_sizeRateDamping;
		for( Int i = 0; i < MAX_KEYFRAMES; ++i )
		{
			k.alphaKey[ i ] = info.m_alphaKey[ i ];
			k.colorKey[ i ] = info.m_colorKey[ i ];
		}
		s.colorScale = info.m_colorScale;
		s.upTowardsEmitter = info.m_particleUpTowardsEmitter;
		s.windRandomness = info.m_windRandomness;
		refreshNextKeyFrames( s, k );

		// the pointer is restored from the id in loadPostProcess, once all systems are loaded
		m_systemUnderControl = nullptr;
		m_systemUnderControlID = systemUnderControlID;
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void Particle::loadPostProcess( void )
{

	// m_systemUnderControlID shares its storage with the pointer
	ParticleSystemID id = m_systemUnderControlID;
	m_systemUnderControl = nullptr;
	if( id == INVALID_PARTICLE_SYSTEM_ID )
		return;

	// an attached system that was not saved is simply gone
	ParticleSystem *system = TheParticleSystemManager->findParticleSystem( id );
	if( system == nullptr || system->isDestroyed() )
	{
		DEBUG_LOG(( "Particle::loadPostProcess - attached particle system %u not found\n", static_cast<UnsignedInt>( id ) ));
		return;
	}

	system->setControlParticle( this );
	controlParticleSystem( system );

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void ParticleSystemInfo::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void ParticleSystemInfo::xfer( Xfer *xfer )
{
	Int i;

	// version 
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// is one shot
	xfer->xferBool( &m_isOneShot );

	// shader type
	xfer->xferUser( &m_shaderType, sizeof( ParticleShaderType ) );

	// particle type
	xfer->xferUser( &m_particleType, sizeof( ParticleType ) );

	// particle type name
	xfer->xferAsciiString( &m_particleTypeName );

	// angles
	GameClientRandomVariable	tempRandom;	//for backwards compatibility when we supported x,y
	xfer->xferUser( &tempRandom, sizeof( GameClientRandomVariable ) );
	xfer->xferUser( &tempRandom, sizeof( GameClientRandomVariable ) );
	xfer->xferUser( &m_angleZ, sizeof( GameClientRandomVariable ) );

	// angular rate
	xfer->xferUser( &tempRandom, sizeof( GameClientRandomVariable ) );
	xfer->xferUser( &tempRandom, sizeof( GameClientRandomVariable ) );
	xfer->xferUser( &m_angularRateZ, sizeof( GameClientRandomVariable ) );

	// angular damping
	xfer->xferUser( &m_angularDamping, sizeof( GameClientRandomVariable ) );

	// velocity damping
	xfer->xferUser( &m_velDamping, sizeof( GameClientRandomVariable ) );

	// lifetime
	xfer->xferUser( &m_lifetime, sizeof( GameClientRandomVariable ) );

	// system lifetime
	xfer->xferUnsignedInt( &m_systemLifetime );

	// start size
	xfer->xferUser( &m_startSize, sizeof( GameClientRandomVariable ) );

	// start size rate
	xfer->xferUser( &m_startSizeRate, sizeof( GameClientRandomVariable ) );

	// size rate
	xfer->xferUser( &m_sizeRate, sizeof( GameClientRandomVariable ) );

	// size rate damping
	xfer->xferUser( &m_sizeRateDamping, sizeof( GameClientRandomVariable ) );

	// alpha keys
	for( i = 0; i < MAX_KEYFRAMES; ++i )
	{

		xfer->xferUser( &m_alphaKey[ i ].var, sizeof( GameClientRandomVariable ) );
		xfer->xferUnsignedInt( &m_alphaKey[ i ].frame );

	}  // end for, i

	// color keys
	for( i = 0; i < MAX_KEYFRAMES; ++i )
	{

		xfer->xferRGBColor( &m_colorKey[ i ].color );
		xfer->xferUnsignedInt( &m_colorKey[ i ].frame );

	}  // end for, i

	// color scale
	xfer->xferUser( &m_colorScale, sizeof( GameClientRandomVariable ) );
	
	// burst delay
	xfer->xferUser( &m_burstDelay, sizeof( GameClientRandomVariable ) );
	
	// burst count
	xfer->xferUser( &m_burstCount, sizeof( GameClientRandomVariable ) );
	
	// initial delay
	xfer->xferUser( &m_initialDelay, sizeof( GameClientRandomVariable ) );
	
	// drift velocity
	xfer->xferCoord3D( &m_driftVelocity );
	
	// gravity
	xfer->xferReal( &m_gravity );
		
	// slave system name
	xfer->xferAsciiString( &m_slaveSystemName );

	// slave position offset
	xfer->xferCoord3D( &m_slavePosOffset );

	// attached system name
	xfer->xferAsciiString( &m_attachedSystemName );

	// emission velocity type, this must come before m_emissionVelocity
	xfer->xferUser( &m_emissionVelocityType, sizeof( EmissionVelocityType ) );

	// particle priority
	xfer->xferUser( &m_priority, sizeof( ParticlePriorityType ) );

	// emission velocity
	switch( m_emissionVelocityType )
	{

		// --------------------------------------------------------------------------------------------
		case ORTHO:
			xfer->xferUser( &m_emissionVelocity.ortho.x, sizeof( GameClientRandomVariable ) );
			xfer->xferUser( &m_emissionVelocity.ortho.y, sizeof( GameClientRandomVariable ) );
			xfer->xferUser( &m_emissionVelocity.ortho.z, sizeof( GameClientRandomVariable ) );
			break;

		// --------------------------------------------------------------------------------------------
		case SPHERICAL:		
			xfer->xferUser( &m_emissionVelocity.spherical.speed, sizeof( GameClientRandomVariable ) );			
			break;

		// --------------------------------------------------------------------------------------------
		case HEMISPHERICAL:
			xfer->xferUser( &m_emissionVelocity.hemispherical.speed, sizeof( GameClientRandomVariable ) );			
			break;

		// --------------------------------------------------------------------------------------------
		case CYLINDRICAL:
			xfer->xferUser( &m_emissionVelocity.cylindrical.radial, sizeof( GameClientRandomVariable ) );						
			xfer->xferUser( &m_emissionVelocity.cylindrical.normal, sizeof( GameClientRandomVariable ) );
			break;

		// --------------------------------------------------------------------------------------------
		case OUTWARD:
			xfer->xferUser( &m_emissionVelocity.outward.speed, sizeof( GameClientRandomVariable ) );
			xfer->xferUser( &m_emissionVelocity.outward.otherSpeed, sizeof( GameClientRandomVariable ) );
			break;

	}  // end switch, m_emissionVelocityType

	// emission volume type
	xfer->xferUser( &m_emissionVolumeType, sizeof( EmissionVolumeType ) );

	// emission volume
	switch( m_emissionVolumeType )
	{

		// --------------------------------------------------------------------------------------------
		case POINT:
			// point has no data, it uses the systems position			
			break;

		// --------------------------------------------------------------------------------------------
		case LINE:
			xfer->xferCoord3D( &m_emissionVolume.line.start );
			xfer->xferCoord3D( &m_emissionVolume.line.end );
			break;

		// --------------------------------------------------------------------------------------------
		case BOX:
			xfer->xferCoord3D( &m_emissionVolume.box.halfSize );
			break;

		// --------------------------------------------------------------------------------------------
		case SPHERE:
			xfer->xferReal( &m_emissionVolume.sphere.radius );
			break;

		// --------------------------------------------------------------------------------------------
		case CYLINDER:
			xfer->xferReal( &m_emissionVolume.cylinder.radius );
			xfer->xferReal( &m_emissionVolume.cylinder.length );
			break;

	}  // end switch, m_emissionVolumeType

	// is emission volume hollow
	xfer->xferBool( &m_isEmissionVolumeHollow );

	// is ground aligned
	xfer->xferBool( &m_isGroundAligned );

	// emit above ground only
	xfer->xferBool( &m_isEmitAboveGroundOnly );

	// is particle up towards emitter
	xfer->xferBool( &m_isParticleUpTowardsEmitter );

	// wind motion
	xfer->xferUser( &m_windMotion, sizeof( WindMotion ) );

	// wind angle
	xfer->xferReal( &m_windAngle );

	// wind angle change
	xfer->xferReal( &m_windAngleChange );

	// wind angle change min
	xfer->xferReal( &m_windAngleChangeMin );

	// wind angle change max
	xfer->xferReal( &m_windAngleChangeMax );

	// wind motion start angle
	xfer->xferReal( &m_windMotionStartAngle );

	// wind motion start angle min
	xfer->xferReal( &m_windMotionStartAngleMin );

	// wind motion start angle max
	xfer->xferReal( &m_windMotionStartAngleMax );

	// wind motion end angle
	xfer->xferReal( &m_windMotionEndAngle );

	// wind motion end angle min
	xfer->xferReal( &m_windMotionEndAngleMin );

	// wind motion end angle max
	xfer->xferReal( &m_windMotionEndAngleMax );

	// wind motion moving to end angle
	xfer->xferByte( &m_windMotionMovingToEndAngle );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void ParticleSystemInfo::loadPostProcess( void )
{

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// base class info
	ParticleSystemInfo::xfer( xfer );

	// particle system ID
	xfer->xferUser( &m_systemID, sizeof( ParticleSystemID ) );

	// attached to drawable id
	xfer->xferDrawableID( &m_attachedToDrawableID );

	// attached to object id
	xfer->xferObjectID( &m_attachedToObjectID );

	// is local identity
	xfer->xferBool( &m_isLocalIdentity );

	// local transform
	xfer->xferUser( &m_localTransform, sizeof( Matrix3D ) );

	// is identity
	xfer->xferBool( &m_isIdentity );

	// transform
	xfer->xferUser( &m_transform, sizeof( Matrix3D ) );

	// burst delay left
	xfer->xferUnsignedInt( &m_burstDelayLeft );

	// delay left
	xfer->xferUnsignedInt( &m_delayLeft );

	// start timestamp
	xfer->xferUnsignedInt( &m_startTimestamp );

	// system lifetime left
	xfer->xferUnsignedInt( &m_systemLifetimeLeft );

	// personality store
	xfer->xferUnsignedInt( &m_personalityStore );

	// is forever
	xfer->xferBool( &m_isForever );

	// accumulated size bonus
	xfer->xferReal( &m_accumulatedSizeBonus );

	// is stopped
	xfer->xferBool( &m_isStopped );

	// we never save destroyed particle systems so there is no need to consider m_isDestroyed
	// m_isDestroyed <-- do nothing with me

	// ditto for m_isSaveable
	// m_isSaveable <-- do nothing with me

	// velCoeff
	xfer->xferCoord3D( &m_velCoeff );

	// count coeff
	xfer->xferReal( &m_countCoeff );

	// delay coeff
	xfer->xferReal( &m_delayCoeff );

	// size coeff
	xfer->xferReal( &m_sizeCoeff );

	// position
	xfer->xferCoord3D( &m_pos );

	// last position
	xfer->xferCoord3D( &m_lastPos );

	// is first pos
	xfer->xferBool( &m_isFirstPos );

	// slave system id
	xfer->xferUser( &m_slaveSystemID, sizeof( ParticleSystemID ) );

	// master system
	xfer->xferUser( &m_masterSystemID, sizeof( ParticleSystemID ) );

	// particle count
	UnsignedInt particleCount = m_particleCount;
	xfer->xferUnsignedInt( &particleCount );

	// particles
	if( xfer->getXferMode() == XFER_SAVE )
	{
		// go through all particles in this system
		for( size_t i = 0; i < m_particleHandles.size(); ++i )
		{

			// write particle information
			if( m_particleHandles[ i ] )
				xfer->xferSnapshot( m_particleHandles[ i ] );

		}  // end for, i

	}  // end if, save
	else
	{
		ParticlePriorityType priority = getPriority();
		const ParticleInfo *info = generateParticleInfo( 0, 1 );
		Particle *particle;

		// read each particle data block
		for( UnsignedInt i = 0; i < particleCount; ++i )
		{

			// create a new particle
			particle = createParticle( info, priority, TRUE );

			// sanity
			DEBUG_ASSERTCRASH( particle, ("ParticleSyste::xfer - Unable to create particle for loading\n") );

			// read in the particle data
			xfer->xferSnapshot( particle );

		}  // end for i

	}  // end else, load

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::loadPostProcess( void )
{

	// call base class post process
	ParticleSystemInfo::loadPostProcess();

	// reconnect slave pointers if needed
	if( m_slaveSystemID != INVALID_PARTICLE_SYSTEM_ID )
	{

		// sanity
		if( m_slaveSystem != nullptr )
		{

			DEBUG_CRASH(( "ParticleSystem::loadPostProcess - m_slaveSystem is not NULL but should be\n" ));
			throw SaveCode::SC_INVALID_DATA;

		}  // end if

		// assign system
		m_slaveSystem = TheParticleSystemManager->findParticleSystem( m_slaveSystemID );

		// sanity
		if( m_slaveSystem == nullptr || m_slaveSystem->isDestroyed() == TRUE )
		{

			DEBUG_CRASH(( "ParticleSystem::loadPostProcess - m_slaveSystem is NULL or destroyed\n" ));
			throw SaveCode::SC_INVALID_DATA;

		}  // end if

	}  // end if

	// reconnect master pointers if needed
	if( m_masterSystemID != INVALID_PARTICLE_SYSTEM_ID )
	{

		// sanity
		if( m_masterSystem != nullptr )
		{

			DEBUG_CRASH(( "ParticleSystem::loadPostProcess - m_masterSystem is not NULL but should be\n" ));
			throw SaveCode::SC_INVALID_DATA;

		}  // end if

		// assign system
		m_masterSystem = TheParticleSystemManager->findParticleSystem( m_masterSystemID );

		// sanity
		if( m_masterSystem == nullptr || m_masterSystem->isDestroyed() == TRUE )
		{

			DEBUG_CRASH(( "ParticleSystem::loadPostProcess - m_masterSystem is NULL or destroyed\n" ));
			throw SaveCode::SC_INVALID_DATA;

		}  // end if

	}  // end if

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// unique system ID counter
	xfer->xferUser( &m_uniqueSystemID, sizeof( ParticleSystemID ) );

	// count of particle systems in the world
	UnsignedInt systemCount = m_particleSystemCount;
	xfer->xferUnsignedInt( &systemCount );

	// particle systems data
	AsciiString systemName;
	ParticleSystem *system;
	if( xfer->getXferMode() == XFER_SAVE )
	{

		// iterate each particle system
		ParticleSystemListIt it;
		for( it = m_allParticleSystemList.begin(); it != m_allParticleSystemList.end(); ++it )
		{
			systemCount--;
			// get system
			system = *it;

			// ignore destroyed systems and non-saveable systems
			if( system->isDestroyed() == TRUE || system->isSaveable() == FALSE )	{
				AsciiString mtString = "";
				xfer->xferAsciiString(&mtString); // write null string as key for destroyed system.
				continue;
			}

			// write template name
			systemName = system->getTemplate()->getName();
			xfer->xferAsciiString( &systemName );

			// write system data
			xfer->xferSnapshot( system );

		}  // end for, it
		DEBUG_ASSERTCRASH(systemCount==0, ("Mismatch in write count."));

	}  // end if, save
	else
	{
		const ParticleSystemTemplate *systemTemplate;

		// read each particle system
		for( UnsignedInt i = 0; i < systemCount; ++i )
		{

			// read system name and find template
			xfer->xferAsciiString( &systemName );
			if (systemName.isEmpty()) {
				continue; // destroyed particle system.
			}
			systemTemplate = findTemplate( systemName );

			// sanity
			if( systemTemplate == nullptr )
			{

				DEBUG_CRASH(( "ParticleSystemManager::xfer - Unknown particle system template '%s'\n",
											systemName.str() ));
				throw SaveCode::SC_INVALID_DATA;

			}  // end if

			// create system
			system = createParticleSystem( systemTemplate, FALSE );

			if( system == nullptr )
			{

				DEBUG_CRASH(( "ParticleSystemManager::xfer - Unable to allocate particle system '%s'\n",
											systemName.str() ));
				throw SaveCode::SC_INVALID_DATA;

			}  // end if

			// read system data
			xfer->xferSnapshot( system );

		}  // end for, i

	}  // end else, load

}  // end particleSystemManager

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::loadPostProcess( void )
{

}  // end loadPostProcess
