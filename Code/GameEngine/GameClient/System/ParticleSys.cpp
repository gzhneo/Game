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

// ParticleSys.cpp ////////////////////////////////////////////////////////////////////////////////
// Particle System implementation
// Author: Michael S. Booth, November 2001
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Engine/ThreadRole.h"

#define DEFINE_PARTICLE_SYSTEM_NAMES

#include "Common/GameEngine.h"
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

#include <bit>

//------------------------------------------------------------------------------ Performance Timers 
//#include "Common/PerfMetrics.h"
//#include "Common/PerfTimer.h"

//static PerfTimer s_particleSys("ParticleSys::update", false, PERFMETRICS_LOGIC_STARTFRAME, PERFMETRICS_LOGIC_STOPFRAME);
//-------------------------------------------------------------------------------------------------

// the singleton

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
ParticleInfo::ParticleInfo( void )
{
	//Added By Sadullah Nader
	//Initializations inserted
	m_angleX = m_angleY = m_angleZ = 0.0f;
	m_angularDamping = 0.0f;
	m_angularRateX = m_angularRateY = m_angularRateZ = 0.0f;
	m_colorScale =0.0f;
  m_size = 0.0f;
	m_sizeRate = 0.0f;
	m_sizeRateDamping = 0.0f;
	m_velDamping = 0.0f;
	m_windRandomness = 0.0f;

	m_emitterPos.zero();
	m_pos.zero();
	m_vel.zero();

	m_lifetime = 0;
	m_particleUpTowardsEmitter = FALSE;
	
	//
}  // end ParticleInfo

/** Load post process */
// ------------------------------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
enum
{
	MAX_SIZE_BONUS = 50
};


//todo move this somewhere more useful.
static Real angleBetween(const Coord2D *vecA, const Coord2D *vecB);

///////////////////////////////////////////////////////////////////////////////////////////////////
// Particle ///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
/** Compute alpha rate to get to next key on given frame */
// ------------------------------------------------------------------------------------------------
static void computeAlphaRate( ParticleState &s, const ParticleKeys &k )
{
	// Past the last key the first color key counts as the next alpha key, as when both key arrays lay
	// back to back in one struct; this keeps the tiny one-tick rate that results.
	Keyframe target;
	if (s.alphaTargetKey < MAX_KEYFRAMES)
		target = k.alphaKey[ s.alphaTargetKey ];
	else
	{
		target.value = k.colorKey[ 0 ].color.red;
		target.frame = std::bit_cast<UnsignedInt>( k.colorKey[ 0 ].color.green );
	}
	s.nextAlphaFrame = (s.alphaTargetKey < MAX_KEYFRAMES) ? target.frame : 0;

	if (target.frame == 0)
	{
		s.alphaRate = 0.0f;
		return;
	}

	Real delta = target.value - k.alphaKey[ s.alphaTargetKey-1 ].value;
	UnsignedInt time = target.frame - k.alphaKey[ s.alphaTargetKey-1 ].frame;

	s.alphaRate = delta/time;
}

// ------------------------------------------------------------------------------------------------
/** Compute color rate to get to next key on given frame */
// ------------------------------------------------------------------------------------------------
static void computeColorRate( ParticleState &s, const ParticleKeys &k )
{
	if (s.colorTargetKey >= MAX_KEYFRAMES || k.colorKey[ s.colorTargetKey ].frame == 0)
	{
		s.nextColorFrame = 0;
		s.colorRate.red = 0.0f;
		s.colorRate.green = 0.0f;
		s.colorRate.blue = 0.0f;
		return;
	}
	s.nextColorFrame = k.colorKey[ s.colorTargetKey ].frame;

	UnsignedInt time = k.colorKey[ s.colorTargetKey ].frame - k.colorKey[ s.colorTargetKey-1 ].frame;
	Real delta = k.colorKey[ s.colorTargetKey ].color.red - k.colorKey[ s.colorTargetKey-1 ].color.red;
	s.colorRate.red = delta/time;

	delta = k.colorKey[ s.colorTargetKey ].color.green - k.colorKey[ s.colorTargetKey-1 ].color.green;
	s.colorRate.green = delta/time;

	delta = k.colorKey[ s.colorTargetKey ].color.blue - k.colorKey[ s.colorTargetKey-1 ].color.blue;
	s.colorRate.blue = delta/time;
}

// ------------------------------------------------------------------------------------------------
/** Construct a particle from a particle template */
// ------------------------------------------------------------------------------------------------
Particle::Particle( ParticleSystem *system, const ParticleInfo *info )
{
	m_system = system;
	m_index = 0;
	m_systemUnderControl = nullptr;

	m_inSystemList = m_inOverallList = FALSE;
	m_overallPrev = m_overallNext = nullptr;

	// add this particle to the global list, retaining particle creation order
	TheParticleSystemManager->addParticle(this, system->getPriority() );

	// add this particle to the Particle System list, retaining local creation order
	m_system->addParticle(this, info);
}

// ------------------------------------------------------------------------------------------------
/** Destructor */
// ------------------------------------------------------------------------------------------------
Particle::~Particle()
{
	// tell the particle system that this particle is gone
	m_system->removeParticle( this );

	// if this particle was controlling another particle system, destroy that system
	if (m_systemUnderControl)
	{
		m_systemUnderControl->detachControlParticle( this );
		m_systemUnderControl->destroy();
	}
	m_systemUnderControl = nullptr;

	// remove from the global list
	TheParticleSystemManager->removeParticle(this);
}

// ------------------------------------------------------------------------------------------------
/** What one update of a system's particles needs from the system */
// ------------------------------------------------------------------------------------------------
struct ParticleStep
{
	Coord3D drift;
	Coord3D windCenter;						///< system position plus the attached object or drawable
	Real windCos;
	Real windSin;
	UnsignedInt now;							///< client frame
	ParticleSystemInfo::ParticleShaderType shader;
	Bool wind;
};

// ------------------------------------------------------------------------------------------------
/** Return true if this particle is invisible */
// ------------------------------------------------------------------------------------------------
static Bool isParticleInvisible( const ParticleState &s, ParticleSystemInfo::ParticleShaderType shader )
{
	switch (shader)
	{
		case ParticleSystemInfo::ADDITIVE:
			// if color is black, this particle is invisible
			
			// check that we're not in the process of going to another color
			if (s.nextColorFrame == 0)
			{
				if ((s.color.red + s.color.green + s.color.blue) <= 0.06f)
					return true;
			}
			return false;

		case ParticleSystemInfo::ALPHA:
			// if alpha is zero, this particle is invisible
			if (s.alpha < 0.02f)
				return true;
			return false;

		case ParticleSystemInfo::ALPHA_TEST:
			// hmm... assume these particles are never invisible
			return false;

		case ParticleSystemInfo::MULTIPLY:
			// if color is white, this particle is invisible

			// check that we're not in the process of going to another color
			if (s.nextColorFrame == 0)
			{
				if ((s.color.red * s.color.green * s.color.blue) > 0.95f)
					return true;
			}
			return false;
	}

	// should never get here - if we do, data is incorrect
	return true;
}

// ------------------------------------------------------------------------------------------------
/** Update the behavior of an individual particle - return false if dead */
// ------------------------------------------------------------------------------------------------
static Bool updateParticleState( ParticleState &s, const ParticleKeys &k, const ParticleStep &step )
{
	// integrate acceleration into velocity
	s.vel.x += s.accel.x;
	s.vel.y += s.accel.y;
	s.vel.z += s.accel.z;

	s.vel.x *= s.velDamping;
	s.vel.y *= s.velDamping;
	s.vel.z *= s.velDamping;

	// integrate velocity into position
	s.pos.x += s.vel.x + step.drift.x;
	s.pos.y += s.vel.y + step.drift.y;
	s.pos.z += s.vel.z + step.drift.z;

	// integrate the wind (if specified) into position
	if( step.wind )
	{
		//
		// compute a vector from the system position in the world to the particle ... we will use
		// this to compute how much force we apply
		//
		Coord3D v;
		v.x = s.pos.x - step.windCenter.x;
		v.y = s.pos.y - step.windCenter.y;
		v.z = s.pos.z - step.windCenter.z;

		// distance amounts for full force from wind and no force at all
		Real fullForceDistance = 75.0f;
		Real noForceDistance = 200.0f;

		//
		// given the distance from the wind position to the particle ... figure out how much
		// force we're going to apply to it.  When it's further away (outside of the full force
		// distance) we will apply only a fraction of the force
		//
		Real distFromWind = v.length();
		if( distFromWind < noForceDistance )
		{
			Real windForceStrength = 2.0f * s.windRandomness;

			// only apply force if still within the circle of influence
			if( distFromWind > fullForceDistance )
				windForceStrength *= (1.0f - ((distFromWind - fullForceDistance) / 
																			(noForceDistance - fullForceDistance)));

			// integate the wind motion into the position
			s.pos.x += (step.windCos * windForceStrength);
			s.pos.y += (step.windSin * windForceStrength);

		}  // end if
	}

	// update orientation
	s.angleZ += s.angularRateZ;
	s.angularRateZ *= s.angularDamping;

	if (s.upTowardsEmitter) {
		// adjust the up position back towards the particle
		static const Coord2D upVec = { 0.0f, 1.0f };
		Coord2D emitterDir;
		emitterDir.x = s.pos.x - s.emitterPos.x;
		emitterDir.y = s.pos.y - s.emitterPos.y;
		s.angleZ = (angleBetween(&upVec, &emitterDir) + PI);
	}

	// update size
	s.size += s.sizeRate;
	s.sizeRate *= s.sizeRateDamping;

	//
	// Update alpha (if used)
	//

	if (step.shader != ParticleSystemInfo::ADDITIVE)
	{
		s.alpha += s.alphaRate;

		if (s.nextAlphaFrame)
		{
			if (step.now - s.createTimestamp >= s.nextAlphaFrame)
			{
				s.alpha = k.alphaKey[ s.alphaTargetKey ].value;
				s.alphaTargetKey++;
				computeAlphaRate( s, k );
			}
		}
		else
			s.alphaRate = 0.0f;

		if (s.alpha < 0.0f)
			s.alpha = 0.0f;
		else if (s.alpha > 1.0f)
			s.alpha = 1.0f;
	}


	//
	// Update color
	//
	s.color.red += s.colorRate.red;
	s.color.green += s.colorRate.green;
	s.color.blue += s.colorRate.blue;

	if (s.nextColorFrame)
	{
		if (step.now - s.createTimestamp >= s.nextColorFrame)
		{
			// can't set, because of colorscale
			s.colorTargetKey++;
			computeColorRate( s, k );
		}
	}
	else
	{
		s.colorRate.red = 0.0f;
		s.colorRate.green = 0.0f;
		s.colorRate.blue = 0.0f;
	}

	/// @todo Rethink this - at least its name
	s.color.red += s.colorScale;
	s.color.green += s.colorScale;
	s.color.blue += s.colorScale;

	if (s.color.red < 0.0f)
		s.color.red = 0.0f;
	else if (s.color.red > 1.0f)
		s.color.red = 1.0f;

	if (s.color.green < 0.0f)
		s.color.green = 0.0f;
	else if (s.color.green > 1.0f)
		s.color.green = 1.0f;

	if (s.color.blue < 0.0f)
		s.color.blue = 0.0f;
	else if (s.color.blue > 1.0f)
		s.color.blue = 1.0f;


	// reset the acceleration for accumulation next frame
	s.accel.z=s.accel.y=s.accel.x= 0.0f;

	// monitor lifetime
	if (s.lifetimeLeft && --s.lifetimeLeft == 0)
		return false;

	DEBUG_ASSERTCRASH( s.lifetimeLeft, ( "A particle has an infinite lifetime..." ));

	// if we've gone totally invisible, destroy ourselves
	if (isParticleInvisible( s, step.shader ))
		return false;
	return true;
}

// ------------------------------------------------------------------------------------------------
/** Get priority of a particle ... which is the priority of it's attached system */
// ------------------------------------------------------------------------------------------------
ParticlePriorityType Particle::getPriority( void )
{ 
	return m_system->getPriority();
}


///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
ParticleSystemInfo::ParticleSystemInfo()
{
	m_priority = PARTICLE_PRIORITY_LOWEST;
	m_isGroundAligned = false;
	m_isEmitAboveGroundOnly = false;
	m_isParticleUpTowardsEmitter = false;
	
	//Added By Sadullah Nader
	//Initializations inserted
	m_driftVelocity.zero();
	m_gravity = 0.0f;
	m_isEmissionVolumeHollow = FALSE;
	m_isOneShot = FALSE;
	m_slavePosOffset.zero();
	m_systemLifetime = 0;
	
	//
	// some default values for the wind motion values
	m_windMotion = WIND_MOTION_NOT_USED;
	m_windAngle = 0.0f;
	m_windAngleChange = 0.15f;  // higher is ping pong faster
	m_windAngleChangeMin = 0.15f;
	m_windAngleChangeMax = 0.45f;
	m_windMotionStartAngleMin = 0.0f;
	m_windMotionStartAngleMax = PI / 4.0f;
	m_windMotionStartAngle = m_windMotionStartAngleMin;
	m_windMotionEndAngleMin = TWO_PI - (PI / 4.0f);
	m_windMotionEndAngleMax = TWO_PI;
	m_windMotionEndAngle = m_windMotionEndAngleMin;
	m_windMotionMovingToEndAngle = TRUE;
	m_volumeParticleDepth = DEFAULT_VOLUME_PARTICLE_DEPTH;

	// Only the Generals INI sets X/Y. Without a default the values in pooled templates would be
	// undefined; a constant 0 draws no client random value in getValue().
	m_angleX.setRange( 0.0f, 0.0f, GameClientRandomVariable::CONSTANT );
	m_angleY.setRange( 0.0f, 0.0f, GameClientRandomVariable::CONSTANT );
	m_angularRateX.setRange( 0.0f, 0.0f, GameClientRandomVariable::CONSTANT );
	m_angularRateY.setRange( 0.0f, 0.0f, GameClientRandomVariable::CONSTANT );

	// A block may leave any field out, and a template made by a tool starts from here; every
	// value is defined so writing or drawing such a template never reads garbage.
	m_shaderType = INVALID_SHADER;
	m_particleType = INVALID_TYPE;
	m_emissionVelocityType = INVALID_VELOCITY;
	m_emissionVolumeType = INVALID_VOLUME;
	GameClientRandomVariable *zeroed[] = {
		&m_angleZ, &m_angularRateZ, &m_angularDamping, &m_velDamping, &m_lifetime,
		&m_startSize, &m_startSizeRate, &m_sizeRate, &m_sizeRateDamping, &m_colorScale,
		&m_burstDelay, &m_burstCount, &m_initialDelay,
		&m_emissionVelocity.ortho.x, &m_emissionVelocity.ortho.y, &m_emissionVelocity.ortho.z };
	for ( GameClientRandomVariable *var : zeroed )
		var->setRange( 0.0f, 0.0f, GameClientRandomVariable::CONSTANT );
	for ( Int i = 0; i < MAX_KEYFRAMES; ++i )
	{
		m_alphaKey[ i ].var.setRange( 0.0f, 0.0f, GameClientRandomVariable::CONSTANT );
		m_alphaKey[ i ].frame = 0;
		m_colorKey[ i ].color.red = m_colorKey[ i ].color.green = m_colorKey[ i ].color.blue = 0.0f;
		m_colorKey[ i ].frame = 0;
	}
	// Ortho and the line are the largest members of their unions.
	m_emissionVolume.line.start.zero();
	m_emissionVolume.line.end.zero();
}


void ParticleSystemInfo::tintAllColors( Color tintColor )
{
	RGBColor rgb;
	rgb.setFromInt(tintColor);

	//This tints all but the first colorKey!!!
	for (int key = 1; key < MAX_KEYFRAMES; ++key )
	{
		m_colorKey[ key ].color.red   *= static_cast<Real>(rgb.red  ) / 255.0f;
		m_colorKey[ key ].color.green *= static_cast<Real>(rgb.green) / 255.0f;
		m_colorKey[ key ].color.blue  *= static_cast<Real>(rgb.blue ) / 255.0f;
	}

}  // end loadPostProcess


///////////////////////////////////////////////////////////////////////////////////////////////////
// ParticleSystem /////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
/** Read particle system properties from given file */
// ------------------------------------------------------------------------------------------------
ParticleSystem::ParticleSystem( const ParticleSystemTemplate *sysTemplate, 
																ParticleSystemID id, 
																Bool createSlaves )
{
	m_removedParticles = 0;

	m_isFirstPos = true;
	m_template = sysTemplate;
	m_systemID = id;

	//Added By Sadullah Nader
	//Initializations inserted
	m_lastPos.zero();
	m_pos.zero();
	m_velCoeff.zero();
	//

	m_attachedToDrawableID = INVALID_DRAWABLE_ID;
	m_attachedToObjectID = INVALID_ID;

	m_isLocalIdentity = true;
	m_localTransform.Make_Identity();

	m_isIdentity = true;
	m_transform.Make_Identity();
  m_skipParentXfrm = false;

	m_isStopped = false;
	m_isDestroyed = false;
	m_isSaveable = true;

	m_slavePosOffset = sysTemplate->m_slavePosOffset;


	///@todo: further formalize this parameter with an UnsignedInt field in the editor
	m_volumeParticleDepth = DEFAULT_VOLUME_PARTICLE_DEPTH;


	m_driftVelocity = sysTemplate->m_driftVelocity;

	m_velCoeff.x = 1.0f;
	m_velCoeff.y = 1.0f;
	m_velCoeff.z = 1.0f;
	m_countCoeff = 1.0f;
	m_delayCoeff = 1.0f;
	m_sizeCoeff = 1.0f;

	m_gravity = sysTemplate->m_gravity;

	m_lifetime = sysTemplate->m_lifetime;
	m_startSize = sysTemplate->m_startSize;
	m_startSizeRate = sysTemplate->m_startSizeRate;
	m_sizeRate = sysTemplate->m_sizeRate;
	m_sizeRateDamping = sysTemplate->m_sizeRateDamping;

	int i;
	for( i=0; i<MAX_KEYFRAMES; i++ )
		m_alphaKey[i] = sysTemplate->m_alphaKey[i];

	for( i=0; i<MAX_KEYFRAMES; i++ )
		m_colorKey[i] = sysTemplate->m_colorKey[i];

	/// @todo It is confusing to do this conversion here...
	Real low = sysTemplate->m_colorScale.getMinimumValue();
	Real hi = sysTemplate->m_colorScale.getMaximumValue();
	m_colorScale.setRange( low / 255.0f, hi / 255.0f );

	m_burstDelay = sysTemplate->m_burstDelay;
	m_burstDelayLeft = 0;

	m_burstCount = sysTemplate->m_burstCount;

	m_isOneShot = sysTemplate->m_isOneShot;

	m_delayLeft = static_cast<UnsignedInt>(sysTemplate->m_initialDelay.getValue());

	m_startTimestamp = TheGameClient->getFrame();
	m_systemLifetimeLeft = sysTemplate->m_systemLifetime;
	if (sysTemplate->m_systemLifetime)
		m_isForever = false;
	else
		m_isForever = true;

	m_accumulatedSizeBonus = 0;

	m_velDamping = sysTemplate->m_velDamping;

	m_angleX = sysTemplate->m_angleX;
	m_angleY = sysTemplate->m_angleY;
	m_angleZ = sysTemplate->m_angleZ;
	m_angularRateX = sysTemplate->m_angularRateX;
	m_angularRateY = sysTemplate->m_angularRateY;
	m_angularRateZ = sysTemplate->m_angularRateZ;
	m_angularDamping = sysTemplate->m_angularDamping;

	m_priority = sysTemplate->m_priority;

	m_emissionVelocityType = sysTemplate->m_emissionVelocityType;
	m_emissionVelocity = sysTemplate->m_emissionVelocity;

	m_emissionVolumeType = sysTemplate->m_emissionVolumeType;
	m_emissionVolume = sysTemplate->m_emissionVolume;

	m_isEmissionVolumeHollow = sysTemplate->m_isEmissionVolumeHollow;
	m_isGroundAligned = sysTemplate->m_isGroundAligned;
	m_isEmitAboveGroundOnly = sysTemplate->m_isEmitAboveGroundOnly;
	m_isParticleUpTowardsEmitter = sysTemplate->m_isParticleUpTowardsEmitter;

	m_windMotion = sysTemplate->m_windMotion;
	m_windAngleChange = sysTemplate->m_windAngleChange;
	m_windAngleChangeMin = sysTemplate->m_windAngleChangeMin;
	m_windAngleChangeMax = sysTemplate->m_windAngleChangeMax;
	m_windMotionStartAngleMin = sysTemplate->m_windMotionStartAngleMin;
	m_windMotionStartAngleMax = sysTemplate->m_windMotionStartAngleMax;
	m_windMotionEndAngleMin = sysTemplate->m_windMotionEndAngleMin;
	m_windMotionEndAngleMax = sysTemplate->m_windMotionEndAngleMax;
	m_windMotionMovingToEndAngle = sysTemplate->m_windMotionMovingToEndAngle;
	m_windMotionStartAngle = GameClientRandomValueReal( m_windMotionStartAngleMin, m_windMotionStartAngleMax );
	m_windMotionEndAngle = GameClientRandomValueReal( m_windMotionEndAngleMin, m_windMotionEndAngleMax );
	m_windAngle = GameClientRandomValueReal( m_windMotionStartAngle, m_windMotionEndAngle );
			
	m_shaderType = sysTemplate->m_shaderType;

	m_particleType = sysTemplate->m_particleType;
	m_particleTypeName = sysTemplate->m_particleTypeName;

	m_isStopped = false;

	// set up slave particle system, if any
	m_masterSystemID = INVALID_PARTICLE_SYSTEM_ID;
	m_slaveSystemID = INVALID_PARTICLE_SYSTEM_ID;
	m_masterSystem = nullptr;
	m_slaveSystem = nullptr;
	if( createSlaves )
	{
		ParticleSystem *slaveSystem = sysTemplate->createSlaveSystem();

		if( slaveSystem )
		{

			setSlave( slaveSystem );
			m_slaveSystem->setMaster( this );

		}  // end if

	}  // end if

	m_attachedSystemName = sysTemplate->m_attachedSystemName;
	m_particleCount = 0;
	m_personalityStore = 0;
	m_controlParticle = nullptr;

	TheParticleSystemManager->friend_addParticleSystem(this);

	//DEBUG_ASSERTLOG(!(m_totalParticleSystemCount % 10 == 0), ( "TotalParticleSystemCount = %d\n", m_totalParticleSystemCount ));
}

// ------------------------------------------------------------------------------------------------
/** Destroy particle system and all of its particles */
// ------------------------------------------------------------------------------------------------
ParticleSystem::~ParticleSystem()
{

	// tell any of our slave systems that we are going away
	if( m_slaveSystem )
	{

		DEBUG_ASSERTCRASH( m_slaveSystem->getMaster() == this, ("~ParticleSystem: Our slave doesn't have us as a master!\n") );
		m_slaveSystem->setMaster( nullptr );
		setSlave( nullptr );

	}  // end if

	// tell any master system that *we* are going away
	if( m_masterSystem )
	{

		DEBUG_ASSERTCRASH( m_masterSystem->getSlave() == this, ("~ParticleSystem: Our master doesn't have us as a slave!\n") );
		m_masterSystem->setSlave( nullptr );
		setMaster( nullptr );

	}  // end if
	

	// destroy all particles "in the air"
	for (size_t i = 0; i < m_particleHandles.size(); ++i)
	{
		if (m_particleHandles[i])
			deleteInstanceOf(m_particleHandles[i]);
	}

	m_attachedToDrawableID = INVALID_DRAWABLE_ID;
	m_attachedToObjectID = INVALID_ID;

	// if this system was controlled by a particle, detach
	if (m_controlParticle)
		m_controlParticle->detachControlledParticleSystem();

	m_controlParticle = nullptr;
	
	TheParticleSystemManager->friend_removeParticleSystem(this);
	//DEBUG_ASSERTLOG(!(m_totalParticleSystemCount % 10 == 0), ( "TotalParticleSystemCount = %d\n", m_totalParticleSystemCount ));
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ParticleSystem::setMaster( ParticleSystem *master )
{

	m_masterSystem = master;
	m_masterSystemID = master ? master->getSystemID() : INVALID_PARTICLE_SYSTEM_ID;

}  // end set Master

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ParticleSystem::setSlave( ParticleSystem *slave )
{

	m_slaveSystem = slave;
	m_slaveSystemID = slave ? slave->getSystemID() : INVALID_PARTICLE_SYSTEM_ID;

}  // end setSlave

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ParticleSystem::setSaveable(Bool b)
{
	m_isSaveable = b;
	if (m_slaveSystem)
		m_slaveSystem->setSaveable(b);
}

// ------------------------------------------------------------------------------------------------
/** (Re)start a stopped particle system */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::start( void )
{
	m_isStopped = false;
}

// ------------------------------------------------------------------------------------------------
/** Stop a particle system from emitting */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::stop( void )
{
	m_isStopped = true;
}

// ------------------------------------------------------------------------------------------------
/** Stop emitting, wait for all of our particles to die, then destroy self. */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::destroy( void )
{
	m_isDestroyed = true;
	if( m_slaveSystem )
	{
		m_slaveSystem->destroy();  // If we don't it will leak forever.  We are solely responsible for it.
	}
}

// ------------------------------------------------------------------------------------------------
/** Get the position of the particle system */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::getPosition( Coord3D *pos )
{
	Vector3 vec;
	m_localTransform.Get_Translation(&vec);
	if (pos)
	{	pos->x=vec.X;
		pos->y=vec.Y;
		pos->z=vec.Z;
	}
}

// ------------------------------------------------------------------------------------------------
/** Set the position of the particle system */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::setPosition( const Coord3D *pos )
{
	m_localTransform.Set_X_Translation( pos->x );
	m_localTransform.Set_Y_Translation( pos->y );
	m_localTransform.Set_Z_Translation( pos->z );
	m_isLocalIdentity = false;
}

// ------------------------------------------------------------------------------------------------
/** Set the system's local transform */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::setLocalTransform( const Matrix3D *matrix )
{
	m_localTransform = *matrix;
	m_isLocalIdentity = false;
}

// ------------------------------------------------------------------------------------------------
/** Rotate local transform matrix */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::rotateLocalTransformX( Real x )
{
	m_localTransform.Rotate_X( x );
	m_isLocalIdentity = false;
}

// ------------------------------------------------------------------------------------------------
/** Rotate local transform matrix */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::rotateLocalTransformY( Real y )
{
	m_localTransform.Rotate_Y( y );
	m_isLocalIdentity = false;
}

// ------------------------------------------------------------------------------------------------
/** Rotate local transform matrix */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::rotateLocalTransformZ( Real z )
{
	m_localTransform.Rotate_Z( z );
	m_isLocalIdentity = false;
}

// ------------------------------------------------------------------------------------------------
/** Attach this particle system to a Drawable */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::attachToDrawable( const Drawable *draw )
{
	if (draw)
		m_attachedToDrawableID = draw->getID();
	else
		m_attachedToDrawableID = INVALID_DRAWABLE_ID;
}

// ------------------------------------------------------------------------------------------------
/** Attach this particle system to a Drawable */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::attachToObject( const Object *obj )
{
	if (obj)
		m_attachedToObjectID = obj->getID();
	else
		m_attachedToObjectID = INVALID_ID;
}

// ------------------------------------------------------------------------------------------------
/** Compute a random point on a unit sphere
 * @todo The density of random points generated is not uniform within the sphere */
// ------------------------------------------------------------------------------------------------
const Coord3D *ParticleSystem::computePointOnUnitSphere( void )
{
	static Coord3D point;

	do
	{
		point.x = GameClientRandomValueReal( -1.0f, 1.0f );
		point.y = GameClientRandomValueReal( -1.0f, 1.0f );
		point.z = GameClientRandomValueReal( -1.0f, 1.0f );
	}
	while (point.x == 0.0f && point.y == 0.0f && point.z == 0.0f);

	point.normalize();

	return &point;
}

// ------------------------------------------------------------------------------------------------
/** Compute a velocity vector based on emission properties */
// ------------------------------------------------------------------------------------------------
const Coord3D *ParticleSystem::computeParticleVelocity( const Coord3D *pos )
{
	static Coord3D newVel;

	switch( m_emissionVelocityType )
	{
		case ORTHO:
			newVel.x = m_emissionVelocity.ortho.x.getValue();
			newVel.y = m_emissionVelocity.ortho.y.getValue();
			newVel.z = m_emissionVelocity.ortho.z.getValue();
			break;
		
		case CYLINDRICAL:
		{
			Real radialSpeed, angle;
			radialSpeed = m_emissionVelocity.cylindrical.radial.getValue();

			angle = GameClientRandomValueReal( 0, 2.0f*PI );
			newVel.x = radialSpeed * cos( angle );
			newVel.y = radialSpeed * sin( angle );

			newVel.z = m_emissionVelocity.cylindrical.normal.getValue();
			break;
		}

		// "outward" velocity is directed along the surface normal of the emission volume
		case OUTWARD:
		{
			Real speed = m_emissionVelocity.outward.speed.getValue();
			Real otherSpeed = m_emissionVelocity.outward.otherSpeed.getValue();
			Coord3D sysPos;

			/*
			sysPos.x = m_localTransform.Get_X_Translation();
			sysPos.y = m_localTransform.Get_Y_Translation();
			sysPos.z = m_localTransform.Get_Z_Translation();
			*/
			sysPos.x = 0.0f;
			sysPos.y = 0.0f;
			sysPos.z = 0.0f;

			switch( m_emissionVolumeType )
			{
				case CYLINDER:
					Coord2D disk;

					disk.x = pos->x - sysPos.x;
					disk.y = pos->y - sysPos.y;
					disk.normalize();

					newVel.x = speed * disk.x;
					newVel.y = speed * disk.y;
					newVel.z = otherSpeed;
					break;

				case BOX:				///< @todo Implement BOX OUTWARD velocity
				case SPHERE:
				{
					newVel.x = pos->x - sysPos.x;
					newVel.y = pos->y - sysPos.y;
					newVel.z = pos->z - sysPos.z;
					newVel.normalize();

					newVel.x *= speed;
					newVel.y *= speed;
					newVel.z *= speed;
					break;
				}

				case LINE:
				{
					Coord3D along;			// unit vector along line direction

					along.x = m_emissionVolume.line.end.x - m_emissionVolume.line.start.x;
					along.y = m_emissionVolume.line.end.y - m_emissionVolume.line.start.y;
					along.z = m_emissionVolume.line.end.z - m_emissionVolume.line.start.z;
					along.normalize();

					Coord3D perp;				// unit vector perpendicular to the along/up plane
					Coord3D up;					// unit vector in the up direction (Z)
					up.x = 0.0;
					up.y = 0.0;
					up.z = 1.0;
					perp.crossProduct( &up, &along, &perp );
					up.crossProduct( &along, &perp, &up );

					// "speed" is in 'horizontal' plane, and "otherSpeed" is 'vertical'
					newVel.x = speed * perp.x + otherSpeed * up.x;
					newVel.y = speed * perp.y + otherSpeed * up.y;
					newVel.z = speed * perp.z + otherSpeed * up.z;								
					break;
				}

				case POINT:
				{
					Coord3D vel = *computePointOnUnitSphere();

					newVel.x = speed * vel.x;
					newVel.y = speed * vel.y;
					newVel.z = speed * vel.z;
					break;
				}
			}

			break;
		}

		case SPHERICAL:
		{
			Real speed = m_emissionVelocity.spherical.speed.getValue();
			Coord3D vel = *computePointOnUnitSphere();

			newVel.x = speed * vel.x;
			newVel.y = speed * vel.y;
			newVel.z = speed * vel.z;
			break;
		}

		case HEMISPHERICAL:
		{
			Coord3D vel;
			Real speed = m_emissionVelocity.spherical.speed.getValue();

			do
			{
				vel.x = GameClientRandomValueReal( -1.0f, 1.0f );
				vel.y = GameClientRandomValueReal( -1.0f, 1.0f );
				vel.z = GameClientRandomValueReal( 0.0f, 1.0f );
			}
			while (vel.x == 0.0f && vel.y == 0.0f && vel.z == 0.0f);

			vel.normalize();

			newVel.x = speed * vel.x;
			newVel.y = speed * vel.y;
			newVel.z = speed * vel.z;
			break;
		}

		default:
			newVel.x = 0.0f;
			newVel.y = 0.0f;
			newVel.z = 0.0f;
			break;
	}

	// scale the velocity by the velocity multiplier
	newVel.x *= m_velCoeff.x*(0.5f+TheGlobalData->m_particleScale/2.0f);
	newVel.y *= m_velCoeff.y*(0.5f+TheGlobalData->m_particleScale/2.0f);
	newVel.z *= m_velCoeff.z*(0.5f+TheGlobalData->m_particleScale/2.0f);

	return &newVel;
}

// ------------------------------------------------------------------------------------------------
/** Compute a position based on emission properties */
// ------------------------------------------------------------------------------------------------
const Coord3D *ParticleSystem::computeParticlePosition( void )
{
	static Coord3D newPos;

	switch( m_emissionVolumeType )
	{
		case CYLINDER:
		{
			Real angle = GameClientRandomValueReal( 0, 2.0f*PI );
			Real radius;
			
			if (m_isEmissionVolumeHollow)
				radius = m_emissionVolume.cylinder.radius;
			else
				radius = GameClientRandomValueReal( 0.0f, m_emissionVolume.cylinder.radius );

			newPos.x = radius * cos( angle );
			newPos.y = radius * sin( angle );

			Real halfLength = m_emissionVolume.cylinder.length/2.0f;
			newPos.z = GameClientRandomValueReal( -halfLength, halfLength );

			break;
		}

		case SPHERE:
		{
			Real radius;

			if (m_isEmissionVolumeHollow)
				radius = m_emissionVolume.sphere.radius;
			else
				radius = GameClientRandomValueReal( 0.0f, m_emissionVolume.sphere.radius );

			newPos = *computePointOnUnitSphere();

			newPos.x *= radius;
			newPos.y *= radius;
			newPos.z *= radius;

			break;
		}

		case BOX:
		{			
			if (m_isEmissionVolumeHollow) {
				// determine which side to generate on.
				// 0 is bottom, 3 is top, 
				// 1 is left , 4 is right
				// 2 is front, 5 is right back

				int side = GameClientRandomValue(0, 6);
				if (side % 3 == 0) {
					// generate X, Y
					newPos.x = GameClientRandomValueReal( -m_emissionVolume.box.halfSize.x, m_emissionVolume.box.halfSize.x );
					newPos.y = GameClientRandomValueReal( -m_emissionVolume.box.halfSize.y, m_emissionVolume.box.halfSize.y );
					if (side == 0) {
						newPos.z = -m_emissionVolume.box.halfSize.z;
					} else {
						newPos.z = m_emissionVolume.box.halfSize.z;
					}
															
				} else if (side % 3 == 1) {
					// generate Y, Z
					newPos.y = GameClientRandomValueReal( -m_emissionVolume.box.halfSize.y, m_emissionVolume.box.halfSize.y );
					newPos.z = GameClientRandomValueReal( -m_emissionVolume.box.halfSize.z, m_emissionVolume.box.halfSize.z );
					if (side == 1) {
						newPos.x = -m_emissionVolume.box.halfSize.x;
					} else {
						newPos.x = m_emissionVolume.box.halfSize.x;
					}

				} else if (side % 3 == 2) {
					// generate X, Z
					newPos.x = GameClientRandomValueReal( -m_emissionVolume.box.halfSize.x, m_emissionVolume.box.halfSize.x );
					newPos.z = GameClientRandomValueReal( -m_emissionVolume.box.halfSize.z, m_emissionVolume.box.halfSize.z );
					if (side == 2) {
						newPos.y = -m_emissionVolume.box.halfSize.y;
					} else {
						newPos.y = m_emissionVolume.box.halfSize.y;
					}
				}
			} else {
				newPos.x = GameClientRandomValueReal( -m_emissionVolume.box.halfSize.x, m_emissionVolume.box.halfSize.x );
				newPos.y = GameClientRandomValueReal( -m_emissionVolume.box.halfSize.y, m_emissionVolume.box.halfSize.y );
				newPos.z = GameClientRandomValueReal( -m_emissionVolume.box.halfSize.z, m_emissionVolume.box.halfSize.z );
			break;
			}
		}
			break;

		case LINE:
		{
			Coord3D delta, start, end;

			start = m_emissionVolume.line.start;
			end = m_emissionVolume.line.end;

			delta.x = end.x - start.x;
			delta.y = end.y - start.y;
			delta.z = end.z - start.z;

			Real t = GameClientRandomValueReal( 0.0f, 1.0f );

			newPos.x = start.x + t * delta.x;
			newPos.y = start.y + t * delta.y;
			newPos.z = start.z + t * delta.z;
			break;
		}

		case POINT:
		default:
			newPos.x = 0.0f;
			newPos.y = 0.0f;
			newPos.z = 0.0f;
			break;
	}
	newPos.x *= (0.5f+TheGlobalData->m_particleScale/2.0f);
	newPos.y *= (0.5f+TheGlobalData->m_particleScale/2.0f);
	newPos.z *= (0.5f+TheGlobalData->m_particleScale/2.0f);
	return &newPos;
}

// ------------------------------------------------------------------------------------------------
/** Factory method for particles. */
// ------------------------------------------------------------------------------------------------
Particle *ParticleSystem::createParticle( const ParticleInfo *info, 
																					ParticlePriorityType priority,
																					Bool forceCreate )
{

	//
	// if we aren't absolutely forcing this particle to be created (which is needed when
	// loading and creating particle systems from the save games) we need to check a few
	// restrictions before this particle can really be created
	//
	if( forceCreate == FALSE )
	{

		if (TheGlobalData->m_useFX == FALSE)
			return nullptr;

		//
		// Enforce particle limit.
		// If we are at the limit, destroy the oldest particle in order
		// to make room for this one.
		//

		//
		// Check if particle is below priorities we allow for this FPS or if it being skipped because 
		// all particesl are being skipped (excluding special fps independent particles at 
		// getMinDynamicParticleSkipPriority()) 
		//
		if( priority < TheGameLODManager->getMinDynamicParticlePriority() ||
				(priority < TheGameLODManager->getMinDynamicParticleSkipPriority() && 
				 TheGameLODManager->isParticleSkipped()) )
			return nullptr;

		if ( getParticleCount() > 0 && priority == AREA_EFFECT && m_isGroundAligned && TheParticleSystemManager->getFieldParticleCount() > static_cast<UnsignedInt>(TheGlobalData->m_maxFieldParticleCount) )
			return nullptr;
		
		// ALWAYS_RENDER particles are exempt from all count limits, and are always created, regardless of LOD issues.
		if (priority != ALWAYS_RENDER)
		{
			int numInExcess = TheParticleSystemManager->getParticleCount() - static_cast<UnsignedInt>(TheGlobalData->m_maxParticleCount);
			if ( numInExcess > 0)
			{
				if( TheParticleSystemManager->removeOldestParticles(static_cast<UnsignedInt>(numInExcess), priority) != numInExcess )
					return nullptr;  // could not remove enough particles, don't create new stuff
			}

			if (TheGlobalData->m_maxParticleCount == 0)
				return nullptr;
		}

	}  // end if

	Particle *p = newInstance(Particle)( this, info );
	return p;

}

// ------------------------------------------------------------------------------------------------
/** Generate a new, random set of ParticleInfo
 * particleNum and particleCount are used to get 'tween frame particles emitted in the correct
 * place. (jkmcd) */
// ------------------------------------------------------------------------------------------------
const ParticleInfo *ParticleSystem::generateParticleInfo( Int particleNum, Int particleCount )
{
	static ParticleInfo info;
	if (particleCount == 0) {
		DEBUG_CRASH(("particleCount must NOT be 0. Set to 1 or greater."));
		return &info;
	}

	// NOTE: position MUST be computed before velocity, in case OUTWARD velocity is
	// specified, which must know where the particle is in space.
	info.m_pos = *computeParticlePosition();
	info.m_vel = *computeParticleVelocity( &info.m_pos );

	// transform the position and velocity, if necessary
	/// @todo Avoid conversion from Coord3D to Vector3 somehow
	if (m_isIdentity == false)
	{
		// transform particle position to world coordinates
		Vector3 p, pr;
		
		Coord3D emissionAdjustment;	// this is the adjustment for inter-frame emission
		// @todo : This should work, if m_lastPos = m_pos is removed from here but it doesn't. 
		// @todo : Investigate why. jkmcd
		if (m_isFirstPos) {
			m_lastPos = m_pos;
			m_isFirstPos = false;
		}
		
		emissionAdjustment.x = (1 - (INT_TO_REAL(particleNum) / particleCount)) * (m_pos.x - m_lastPos.x);
		emissionAdjustment.y = (1 - (INT_TO_REAL(particleNum) / particleCount)) * (m_pos.y - m_lastPos.y);
		emissionAdjustment.z = (1 - (INT_TO_REAL(particleNum) / particleCount)) * (m_pos.z - m_lastPos.z);

		p.X = info.m_pos.x;
		p.Y = info.m_pos.y;
		p.Z = info.m_pos.z;

		m_transform.mulVector3(p, pr);

		info.m_pos.x = pr.X - emissionAdjustment.x;
		info.m_pos.y = pr.Y - emissionAdjustment.y;
		info.m_pos.z = pr.Z - emissionAdjustment.z;

		// transform particle velocity to world coordinates
		Vector3 v, vr;

		v.X = info.m_vel.x;
		v.Y = info.m_vel.y;
		v.Z = info.m_vel.z;

		Matrix3D::Rotate_Vector( m_transform, v, &vr );

		info.m_vel.x = vr.X;
		info.m_vel.y = vr.Y;
		info.m_vel.z = vr.Z;
	}

	info.m_velDamping = m_velDamping.getValue();
	info.m_angularDamping = m_angularDamping.getValue();

	// Random draw order as in Generals; with the Zero Hour data X/Y draw nothing
	info.m_angleX = m_angleX.getValue();
	info.m_angleY = m_angleY.getValue();
	info.m_angleZ = m_angleZ.getValue();
	info.m_angularRateX = m_angularRateX.getValue();
	info.m_angularRateY = m_angularRateY.getValue();
	info.m_angularRateZ = m_angularRateZ.getValue();

	info.m_lifetime = static_cast<UnsignedInt>(m_lifetime.getValue());

	info.m_size = m_startSize.getValue()*m_sizeCoeff*TheGlobalData->m_particleScale;
	info.m_sizeRate = m_sizeRate.getValue()*m_sizeCoeff*TheGlobalData->m_particleScale;
	info.m_sizeRateDamping = m_sizeRateDamping.getValue();

	// Keeping a running tally makes each successive particle spawned start a bit bigger (or smaller).
	info.m_size += m_accumulatedSizeBonus;
	m_accumulatedSizeBonus += m_startSizeRate.getValue();
	if( m_accumulatedSizeBonus )
		m_accumulatedSizeBonus = min( m_accumulatedSizeBonus, static_cast<float>(MAX_SIZE_BONUS) );

	for( int i=0; i<MAX_KEYFRAMES; i++ )
	{
		info.m_alphaKey[i].value = m_alphaKey[i].var.getValue();
		info.m_alphaKey[i].frame = m_alphaKey[i].frame;
		info.m_colorKey[i] = m_colorKey[i];
	}

/*
	info.m_color.red = m_color.red.getValue();
	info.m_color.green = m_color.green.getValue();
	info.m_color.blue = m_color.blue.getValue();
*/

	info.m_colorScale = m_colorScale.getValue();
	Vector3 pos;
	m_transform.mulVector3(Vector3(0, 0, 0), pos);
	info.m_emitterPos.x = pos.X;
	info.m_emitterPos.y = pos.Y;
	info.m_emitterPos.z = pos.Z;
	info.m_particleUpTowardsEmitter = m_isParticleUpTowardsEmitter;

	info.m_windRandomness = GameClientRandomValueReal( 0.7f, 1.3f );

	return &info;
}

// ------------------------------------------------------------------------------------------------
/** Update this particle system, potentially generating new particles */
// ------------------------------------------------------------------------------------------------
Bool ParticleSystem::update( Int localPlayerIndex  )
{
	if (TheGlobalData->m_useFX == FALSE)
		return false;

	// do initial delay ... note, this currently delays the lifetime
	if (m_delayLeft)
	{
		--m_delayLeft;

		// system actually "starts" once initial delay is over
		/// @todo reset start time when system is stopped/started
		if (m_delayLeft == 0)
			m_startTimestamp = TheGameClient->getFrame();

		return true;
	}

	// update the wind motion
	if (m_windMotion != ParticleSystemInfo::WIND_MOTION_NOT_USED )
		updateWindMotion();

	// if this system is attached to a Drawable/Object, update the current transform
	// matrix so generated particles' are relative to the parent Drawable's
	// position and orientation
	Bool transformSet = false;
	const Matrix3D *parentXfrm = nullptr;
	Bool isShrouded = false;

	if (m_attachedToDrawableID)
	{
		Drawable *attachedTo = TheGameClient->findDrawableByID( m_attachedToDrawableID );

		if (attachedTo)
		{
			if (attachedTo->getFullyObscuredByShroud())
				isShrouded = true;

			parentXfrm = attachedTo->getTransformMatrix();

			m_lastPos = m_pos;
			m_pos = *attachedTo->getPosition();
		}
		else
		{ 
			// Drawable has been destroyed - lose our attachment to it
			m_attachedToDrawableID = INVALID_DRAWABLE_ID;

			// destroy ourselves
			destroy();
		}
	}
	else if (m_attachedToObjectID)
	{
		Object *objectAttachedTo = TheGameLogic->findObjectByID( m_attachedToObjectID );

		if (objectAttachedTo)
		{
			if (!isShrouded)
				isShrouded = (objectAttachedTo->getShroudedStatus(localPlayerIndex) >= OBJECTSHROUD_FOGGED);

			const Drawable * draw = objectAttachedTo->getDrawable();
			if ( draw )
				parentXfrm = draw->getTransformMatrix();
			else
				parentXfrm = objectAttachedTo->getTransformMatrix();




			m_lastPos = m_pos;
			m_pos = *objectAttachedTo->getPosition();
		}
		else
		{ 
			// Drawable has been destroyed - lose our attachment to it
			m_attachedToObjectID = INVALID_ID;

			// destroy ourselves
			destroy();
		}
	}


	if (TheGlobalData->m_logActions && m_startTimestamp == TheGameClient->getFrame())
		GameEngine_LogAction(m_startTimestamp, "particle", m_template ? m_template->getName().str() : "unknown", m_pos.x, m_pos.y, m_pos.z);

	if (parentXfrm)
	{
    if (m_skipParentXfrm)
    {
      //this particle system is already in world space so no need to apply parent xform.
      m_transform = m_localTransform;
    }
    else
    {
		  // if system has its own local transform, concatenate them
		  if (m_isLocalIdentity == false)
			  m_transform.mul(*parentXfrm, m_localTransform);
		  else
			  m_transform = *parentXfrm;
    }

		  m_isIdentity = false;
		  transformSet = true;
	}


	if (transformSet == false)
	{
		if (m_isLocalIdentity == false)
		{
			m_transform = m_localTransform;
			m_isIdentity = false;
		}
		else
		{
			m_isIdentity = true;
		}
	}

	// if we are controlled by a particle, its position is local origin
	if (m_controlParticle)
	{
		const Coord3D *controlPos = m_controlParticle->getPosition();
		/// @todo Concatenate this, instead of overriding (MSB)
		m_transform.Set_X_Translation( controlPos->x );
		m_transform.Set_Y_Translation( controlPos->y );
		m_transform.Set_Z_Translation( controlPos->z );
		m_isIdentity = false;
		m_lastPos = m_pos;
		m_pos = *controlPos;
	}


	//
	// Generate new particles if the system hasn't been 'stopped' or 'destroyed'
	// If we are a slave system, do not generate particles ourselves - our master will force us to
	//
	if (m_isDestroyed == false)
	{
		if (m_isForever || (m_isForever == false && m_systemLifetimeLeft > 0))
		{
			if (!isShrouded && m_isStopped == false && m_masterSystem == nullptr)
			{
				if (m_burstDelayLeft == 0)
				{
					ParticlePriorityType priority = getPriority();

					// emit a burst of particles
					Int count = REAL_TO_INT(m_burstCount.getValue());

					count = static_cast<Int>(count * (m_countCoeff));

					for( Int i=0; i<count; i++ )
					{
						// generate this particle's unique attributes
						const ParticleInfo *info = generateParticleInfo(i, count);
						if (!m_isEmitAboveGroundOnly || (info->m_pos.z >= TheTerrainLogic->getGroundHeight(info->m_pos.x, info->m_pos.y)))
						{
							// actually create a particle
							Particle *p = createParticle( info, priority );
							if (p == nullptr)
								continue;

							if (m_attachedSystemName.isEmpty() == false)
							{
								const ParticleSystemTemplate *tmp = TheParticleSystemManager->findTemplate( m_attachedSystemName );
								if (tmp)
								{
									ParticleSystem *sys = TheParticleSystemManager->createParticleSystem( tmp, TRUE );
									sys->setControlParticle( p );
									p->controlParticleSystem( sys );
								}
							}

							// create a slave particle, if necessary
							if (m_slaveSystem)
							{
								ParticleInfo mergeInfo = ParticleSystem::mergeRelatedParticleSystems(this, m_slaveSystem, false);

								// create slaved particle
								m_slaveSystem->createParticle( &mergeInfo, priority );
							}
						}
					}
						
					// compute next burst delay
					m_burstDelayLeft = static_cast<UnsignedInt>(m_burstDelay.getValue());
					m_burstDelayLeft = static_cast<UnsignedInt>(m_burstDelayLeft * (m_delayCoeff));
				}
				else
				{
					m_burstDelayLeft--;
				}

			} // end if stopped check
		} // end if system lifetime check
	} // end if is destroyed

	//
	// Update all particles in the system
	//
	updateParticles();

	//
	// If we have been "destroyed", wait for all of our particles to die off,
	// then destroy ourselves (return false).
	//
	if (m_isDestroyed && m_particleCount == 0)
		return false;


	// monitor particle system lifetime
	if (m_isForever == false)
	{
		// decrement lifetime if not zero
		if (m_systemLifetimeLeft)
			m_systemLifetimeLeft--;

		// if there are still particles "in the air", don't destroy yet
		if (getParticleCount())
			return true;

		// check if time is up
		if (m_systemLifetimeLeft == 0)
			return false;
	}

	return true;
}

// ------------------------------------------------------------------------------------------------
/** Update the wind motion */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::updateWindMotion( void )
{

	switch( m_windMotion )
	{

		// --------------------------------------------------------------------------------------------
		case ParticleSystemInfo::WIND_MOTION_PING_PONG:
		{
			Real startAngle = m_windMotionStartAngle;
			Real endAngle = m_windMotionEndAngle;

			// this only works when start angle is less than end angle
			DEBUG_ASSERTCRASH( startAngle < endAngle, ("updateWindMotion: startAngle must be < endAngle\n") );

			// how big is the total angle span
			Real totalSpan = endAngle - startAngle;
			Real halfSpan = totalSpan / 2.0f;

			// given our current angle ... how far away from the "center" of the span are we
			Real diffFromCenter = fabs( halfSpan - m_windAngle + startAngle );
	
			//
			// given our distance from the center ... we need to compute how much we will change
			// the angle.  When we are closer to the center we change it faster (more), and when
			// we are near the edges we change is slower (less)
			//
			Real change = (1.0f - (diffFromCenter / halfSpan)) * m_windAngleChange;

			// we will always change a little bit
			#define MINIMUM_CHANGE 0.005f  // lower #'s have softer swings at the edge angles
			if( change < MINIMUM_CHANGE )
				change = MINIMUM_CHANGE;
			
			//
			// if we are moving toward the end angle we add the change, if we're moving away
			// from it we subtract it
			//
			if( m_windMotionMovingToEndAngle )
			{

				// add angle
				m_windAngle += change;

				// see if we're at the end and should switch directions
				if( m_windAngle >= endAngle )
				{

					// change directions
					m_windMotionMovingToEndAngle = FALSE;

					// pick a new change delta
					m_windAngleChange = 
							GameClientRandomValueReal( m_windAngleChangeMin, m_windAngleChangeMax );

					// pick new start and end angles
					m_windMotionStartAngle = 
							GameClientRandomValueReal( m_windMotionStartAngleMin, 
																				 m_windMotionStartAngleMax );
					m_windMotionEndAngle = 
							GameClientRandomValueReal( m_windMotionEndAngleMin, 
																				 m_windMotionEndAngleMax );

				}  // end if

			}  // end if
			else
			{

				// subtract angle
				m_windAngle -= change;

				// see if we're at the end and should switch directions
				if( m_windAngle <= startAngle )
				{

					// change directions
					m_windMotionMovingToEndAngle = TRUE;

					// pick a new change delta
					m_windAngleChange = 
							GameClientRandomValueReal( m_windAngleChangeMin, m_windAngleChangeMax );

					// pick new start and end angles
					m_windMotionStartAngle = 
							GameClientRandomValueReal( m_windMotionStartAngleMin, 
																				 m_windMotionStartAngleMax );
					m_windMotionEndAngle = 
							GameClientRandomValueReal( m_windMotionEndAngleMin, 
																				 m_windMotionEndAngleMax );

				}  // end if

			}  // end else

			break;

		}  // end case

		// --------------------------------------------------------------------------------------------
		case ParticleSystemInfo::WIND_MOTION_CIRCULAR:
		{

			// give us a wind angle change if one hasn't been specifed (this plays nice with the particle editor)
			if( m_windAngleChange == 0.0f )
				m_windAngleChange = GameClientRandomValueReal( m_windAngleChangeMin, m_windAngleChangeMax );

			// add to our wind angle
			m_windAngle += m_windAngleChange;
			
			// keep in 0 to 2PI range just to keep the numbers safe and sane
			if( m_windAngle > TWO_PI )
				m_windAngle -= TWO_PI;
			else if( m_windAngle < 0.0f )
				m_windAngle += TWO_PI;

			break;

		}  // end case

		// ---------------------------------------------------------------------------------------------
		default:
		{

			break;

		}  // end default

	}  // end if

}  // end updateWindMotion

// ------------------------------------------------------------------------------------------------
/** Move all particles one tick, delete those that died */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::updateParticles( void )
{
	// Close the gaps once they make up a quarter of the slots: the oldest particles die first, so
	// compacting after every death would move nearly all states each tick.
	if (m_removedParticles * 4 >= m_particleHandles.size())
		compactParticles();

	const size_t slots = m_particleStates.size();
	if (slots == 0)
		return;

	ParticleStep step;
	step.drift = m_driftVelocity;
	step.shader = m_shaderType;
	step.now = TheGameClient->getFrame();
	step.wind = (m_windMotion != ParticleSystemInfo::WIND_MOTION_NOT_USED);
	step.windCenter.zero();
	step.windCos = 0.0f;
	step.windSin = 0.0f;
	if (step.wind)
	{
		getPosition( &step.windCenter );

		// when we're attached objects and drawables we offset by that position as well
		if( m_attachedToObjectID )
		{
			Object *obj = TheGameLogic->findObjectByID( m_attachedToObjectID );
			if( obj )
			{
				const Coord3D *objPos = obj->getPosition();
				step.windCenter.x += objPos->x;
				step.windCenter.y += objPos->y;
				step.windCenter.z += objPos->z;
			}
		}
		else if( m_attachedToDrawableID )
		{
			Drawable *draw = TheGameClient->findDrawableByID( m_attachedToDrawableID );
			if( draw )
			{
				const Coord3D *drawPos = draw->getPosition();
				step.windCenter.x += drawPos->x;
				step.windCenter.y += drawPos->y;
				step.windCenter.z += drawPos->z;
			}
		}

		step.windCos = Cos( m_windAngle );
		step.windSin = Sin( m_windAngle );
	}

	for (size_t i = 0; i < slots; ++i)
	{
		ParticleState &s = m_particleStates[i];
		if (!s.alive)
			continue;

		// apply 'gravity' force
		if (m_gravity != 0.0f)
		{
			s.accel.x += 0.0f;
			s.accel.y += 0.0f;
			s.accel.z += m_gravity;
		}

		// Deleting only marks the slot, so the arrays stay put during the loop.
		if (updateParticleState( s, m_particleKeys[i], step ) == false)
			deleteInstanceOf(m_particleHandles[i]);
	}
}

// ------------------------------------------------------------------------------------------------
/** Close the gaps left by removed particles, keeping the order */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::compactParticles( void )
{
	if (m_removedParticles == 0)
		return;

	size_t out = 0;
	for (size_t i = 0; i < m_particleHandles.size(); ++i)
	{
		Particle *p = m_particleHandles[i];
		if (p == nullptr)
			continue;
		if (out != i)
		{
			m_particleStates[out] = m_particleStates[i];
			m_particleKeys[out] = m_particleKeys[i];
			m_particleHandles[out] = p;
			p->m_index = static_cast<UnsignedInt>(out);
		}
		++out;
	}
	m_particleStates.resize(out);
	m_particleKeys.resize(out);
	m_particleHandles.resize(out);
	m_removedParticles = 0;
}

// ------------------------------------------------------------------------------------------------
/** Append the state of a new particle - ONLY FOR USE BY PARTICLE */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::addParticle( Particle *particleToAdd, const ParticleInfo *info )
{
	if (particleToAdd->m_inSystemList)
		return;

	ParticleState s;
	ParticleKeys k;

	s.pos = info->m_pos;
	s.vel = info->m_vel;
	s.accel.zero();
	s.velDamping = info->m_velDamping;
	s.angleZ = info->m_angleZ;
	s.angularRateZ = info->m_angularRateZ;
	s.angularDamping = info->m_angularDamping;
	s.size = info->m_size;
	s.sizeRate = info->m_sizeRate;
	s.sizeRateDamping = info->m_sizeRateDamping;
	s.colorScale = info->m_colorScale;
	s.windRandomness = info->m_windRandomness;
	s.emitterPos = info->m_emitterPos;
	s.upTowardsEmitter = info->m_particleUpTowardsEmitter;
	s.lifetimeLeft = info->m_lifetime;
	s.createTimestamp = TheGameClient->getFrame();
	s.alive = TRUE;
	k.lifetime = info->m_lifetime;

	// set up alpha
	for( Int i=0; i<MAX_KEYFRAMES; i++ )
		k.alphaKey[i] = info->m_alphaKey[i];

	s.alpha = k.alphaKey[0].value;
	s.alphaTargetKey = 1;
	computeAlphaRate( s, k );

	// set up colors
	for( Int i=0; i<MAX_KEYFRAMES; i++ )
		k.colorKey[i] = info->m_colorKey[i];

	s.color = k.colorKey[0].color;
	s.colorTargetKey = 1;
	computeColorRate( s, k );

	s.personality = m_personalityStore++;

	particleToAdd->m_index = static_cast<UnsignedInt>(m_particleStates.size());
	m_particleStates.push_back( s );
	m_particleKeys.push_back( k );
	m_particleHandles.push_back( particleToAdd );
	particleToAdd->m_inSystemList = TRUE;

	++m_particleCount;
}

// ------------------------------------------------------------------------------------------------
/** Remove given particle from the list - ONLY FOR USE BY PARTICLE */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::removeParticle( Particle *particleToRemove )
{
	if (!particleToRemove->m_inSystemList)
		return;

	m_particleStates[ particleToRemove->m_index ].alive = FALSE;
	m_particleHandles[ particleToRemove->m_index ] = nullptr;
	++m_removedParticles;

	particleToRemove->m_inSystemList = FALSE;
	--m_particleCount;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
ParticleInfo ParticleSystem::mergeRelatedParticleSystems( ParticleSystem *masterParticleSystem, ParticleSystem *slaveParticleSystem, Bool slaveNeedsFullPromotion)
{
	if (!masterParticleSystem || !slaveParticleSystem) {
		DEBUG_CRASH(("masterParticleSystem or slaveParticleSystem was NULL. Should not happen. JKMCD"));
		ParticleInfo bogus;
		memset( &bogus, 0, sizeof( bogus ) );
		return bogus;
	}

	// copy info
	ParticleInfo mergeInfo = *masterParticleSystem->generateParticleInfo(1, 1);

	// generate one from the slave system
	const ParticleInfo *info = slaveParticleSystem->generateParticleInfo(1, 1);

	// override unique attributes of slave particle
	mergeInfo.m_lifetime = info->m_lifetime;

	// size becomes a scale factor of master's particles
	mergeInfo.m_size *= info->m_size;
	mergeInfo.m_sizeRate *= info->m_sizeRate;
	mergeInfo.m_sizeRateDamping *= info->m_sizeRateDamping;

	mergeInfo.m_angleX = info->m_angleX;
	mergeInfo.m_angleY = info->m_angleY;
	mergeInfo.m_angleZ = info->m_angleZ;
	mergeInfo.m_angularRateX = info->m_angularRateX;
	mergeInfo.m_angularRateY = info->m_angularRateY;
	mergeInfo.m_angularRateZ = info->m_angularRateZ;
	mergeInfo.m_angularDamping = info->m_angularDamping;

	int i;
	for( i=0; i<MAX_KEYFRAMES; i++ )
		mergeInfo.m_alphaKey[i] = info->m_alphaKey[i];

	for( i=0; i<MAX_KEYFRAMES; i++ )
		mergeInfo.m_colorKey[i] = info->m_colorKey[i];

	mergeInfo.m_colorScale = info->m_colorScale;

	// offset slave's position relative to master's
	const Coord3D *offset = slaveParticleSystem->getSlavePositionOffset();
	mergeInfo.m_pos.x += offset->x;
	mergeInfo.m_pos.y += offset->y;
	mergeInfo.m_pos.z += offset->z;

	if (slaveNeedsFullPromotion) {
		slaveParticleSystem->m_burstCount = masterParticleSystem->m_burstCount;
		slaveParticleSystem->m_burstDelay = masterParticleSystem->m_burstDelay;

		slaveParticleSystem->m_priority = masterParticleSystem->m_priority;
		slaveParticleSystem->m_emissionVelocity = masterParticleSystem->m_emissionVelocity;
		slaveParticleSystem->m_emissionVelocityType = masterParticleSystem->m_emissionVelocityType;
		slaveParticleSystem->m_emissionVolume = masterParticleSystem->m_emissionVolume;
		slaveParticleSystem->m_emissionVolumeType = masterParticleSystem->m_emissionVolumeType;
		slaveParticleSystem->m_isEmissionVolumeHollow = masterParticleSystem->m_isEmissionVolumeHollow;

		
		slaveParticleSystem->m_startSize.setRange(masterParticleSystem->m_startSize.getMinimumValue() * slaveParticleSystem->m_startSize.getMinimumValue(),
																							masterParticleSystem->m_startSize.getMaximumValue() * slaveParticleSystem->m_startSize.getMaximumValue(),
																							masterParticleSystem->m_startSize.getDistributionType());
	
		slaveParticleSystem->m_sizeRate.setRange(masterParticleSystem->m_sizeRate.getMinimumValue() * slaveParticleSystem->m_sizeRate.getMinimumValue(),
																							masterParticleSystem->m_sizeRate.getMaximumValue() * slaveParticleSystem->m_sizeRate.getMaximumValue(),
																							masterParticleSystem->m_sizeRate.getDistributionType());

		slaveParticleSystem->m_sizeRateDamping.setRange(masterParticleSystem->m_sizeRateDamping.getMinimumValue() * slaveParticleSystem->m_sizeRateDamping.getMinimumValue(),
																							masterParticleSystem->m_sizeRateDamping.getMaximumValue() * slaveParticleSystem->m_sizeRateDamping.getMaximumValue(),
																							masterParticleSystem->m_sizeRateDamping.getDistributionType());
		
//		slaveParticleSystem->m_burstCount.setRange(masterParticleSystem->m_burstCount.getMinimumValue() / 2, 
//																							 masterParticleSystem->m_burstCount.getMaximumValue() / 2,
//																							 masterParticleSystem->m_burstCount.getDistributionType());
		
	}

	return mergeInfo;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ParticleSystem::setLifetimeRange( Real min, Real max )
{
	m_lifetime.setRange( min, max );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ParticleSystem::setControlParticle( Particle *p )
{
	m_controlParticle = p;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// ParticleSystemTemplate /////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
/** INI parse data */
// ------------------------------------------------------------------------------------------------
const FieldParse ParticleSystemTemplate::m_fieldParseTable[] = 
{
	{ "Priority",								INI::parseIndexList, ParticlePriorityNames, offsetof( ParticleSystemTemplate, m_priority ) },
	{ "IsOneShot",							INI::parseBool,						nullptr,		offsetof( ParticleSystemTemplate, m_isOneShot ) },
	{ "Shader",									INI::parseIndexList,			ParticleShaderTypeNames,		offsetof( ParticleSystemTemplate, m_shaderType ) },
	{ "Type",										INI::parseIndexList,			ParticleTypeNames,		offsetof( ParticleSystemTemplate, m_particleType ) },
	{ "ParticleName",						INI::parseAsciiString,		nullptr,		offsetof( ParticleSystemTemplate, m_particleTypeName ) },
	// X/Y of the Generals INI (see ParticleSys.h)
	{ "AngleX",									INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_angleX ) },
	{ "AngleY",									INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_angleY ) },
	{ "AngleZ",									INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_angleZ ) },
	{ "AngularRateX",						INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_angularRateX ) },
	{ "AngularRateY",						INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_angularRateY ) },
	{ "AngularRateZ",						INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_angularRateZ ) },
	{ "AngularDamping",					INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_angularDamping ) },

	{ "VelocityDamping",				INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_velDamping ) },
	{ "Gravity",								INI::parseReal,																nullptr,		offsetof( ParticleSystemTemplate, m_gravity ) },
	{ "SlaveSystem",						INI::parseAsciiString,												nullptr,		offsetof( ParticleSystemTemplate, m_slaveSystemName ) },
	{ "SlavePosOffset",					INI::parseCoord3D,														nullptr,		offsetof( ParticleSystemTemplate, m_slavePosOffset ) },
	{ "PerParticleAttachedSystem",		INI::parseAsciiString,								nullptr,		offsetof( ParticleSystemTemplate, m_attachedSystemName ) },

	{ "Lifetime",								INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_lifetime ) },
	{ "SystemLifetime",					INI::parseUnsignedInt,												nullptr,		offsetof( ParticleSystemTemplate, m_systemLifetime ) },

	{ "Size",										INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_startSize ) },
	{ "StartSizeRate",					INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_startSizeRate ) },
	{ "SizeRate",								INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_sizeRate ) },
	{ "SizeRateDamping",				INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_sizeRateDamping ) },

	{ "Alpha1",									ParticleSystemTemplate::parseRandomKeyframe,	nullptr,		offsetof( ParticleSystemTemplate, m_alphaKey[0] ) },
	{ "Alpha2",									ParticleSystemTemplate::parseRandomKeyframe,	nullptr,		offsetof( ParticleSystemTemplate, m_alphaKey[1] ) },
	{ "Alpha3",									ParticleSystemTemplate::parseRandomKeyframe,	nullptr,		offsetof( ParticleSystemTemplate, m_alphaKey[2] ) },
	{ "Alpha4",									ParticleSystemTemplate::parseRandomKeyframe,	nullptr,		offsetof( ParticleSystemTemplate, m_alphaKey[3] ) },
	{ "Alpha5",									ParticleSystemTemplate::parseRandomKeyframe,	nullptr,		offsetof( ParticleSystemTemplate, m_alphaKey[4] ) },
	{ "Alpha6",									ParticleSystemTemplate::parseRandomKeyframe,	nullptr,		offsetof( ParticleSystemTemplate, m_alphaKey[5] ) },
	{ "Alpha7",									ParticleSystemTemplate::parseRandomKeyframe,	nullptr,		offsetof( ParticleSystemTemplate, m_alphaKey[6] ) },
	{ "Alpha8",									ParticleSystemTemplate::parseRandomKeyframe,	nullptr,		offsetof( ParticleSystemTemplate, m_alphaKey[7] ) },

	{ "Color1",									ParticleSystemTemplate::parseRGBColorKeyframe,nullptr,		offsetof( ParticleSystemTemplate, m_colorKey[0] ) },
	{ "Color2",									ParticleSystemTemplate::parseRGBColorKeyframe,nullptr,		offsetof( ParticleSystemTemplate, m_colorKey[1] ) },
	{ "Color3",									ParticleSystemTemplate::parseRGBColorKeyframe,nullptr,		offsetof( ParticleSystemTemplate, m_colorKey[2] ) },
	{ "Color4",									ParticleSystemTemplate::parseRGBColorKeyframe,nullptr,		offsetof( ParticleSystemTemplate, m_colorKey[3] ) },
	{ "Color5",									ParticleSystemTemplate::parseRGBColorKeyframe,nullptr,		offsetof( ParticleSystemTemplate, m_colorKey[4] ) },
	{ "Color6",									ParticleSystemTemplate::parseRGBColorKeyframe,nullptr,		offsetof( ParticleSystemTemplate, m_colorKey[5] ) },
	{ "Color7",									ParticleSystemTemplate::parseRGBColorKeyframe,nullptr,		offsetof( ParticleSystemTemplate, m_colorKey[6] ) },
	{ "Color8",									ParticleSystemTemplate::parseRGBColorKeyframe,nullptr,		offsetof( ParticleSystemTemplate, m_colorKey[7] ) },

//	{ "COLOR",									ParticleSystemTemplate::parseRandomRGBColor,	NULL,		offsetof( ParticleSystemTemplate, m_color ) },
	{ "ColorScale",							INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_colorScale ) },

	{ "BurstDelay",							INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_burstDelay ) },
	{ "BurstCount",							INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_burstCount ) },

	{ "InitialDelay",						INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_initialDelay ) },

	{ "DriftVelocity",					INI::parseCoord3D,														nullptr,		offsetof( ParticleSystemTemplate, m_driftVelocity ) },
	{ "VelocityType",						INI::parseIndexList,													EmissionVelocityTypeNames,		offsetof( ParticleSystemTemplate, m_emissionVelocityType ) },
	{ "VelOrthoX",							INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_emissionVelocity.ortho.x ) },
	{ "VelOrthoY",							INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_emissionVelocity.ortho.y ) },
	{ "VelOrthoZ",							INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_emissionVelocity.ortho.z ) },

	{ "VelSpherical",						INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_emissionVelocity.spherical.speed ) },
	{ "VelHemispherical",				INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_emissionVelocity.hemispherical.speed ) },

	{ "VelCylindricalRadial",		INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_emissionVelocity.cylindrical.radial ) },
	{ "VelCylindricalNormal",		INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_emissionVelocity.cylindrical.normal ) },

	{ "VelOutward",							INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_emissionVelocity.outward.speed ) },
	{ "VelOutwardOther",				INI::parseGameClientRandomVariable,	nullptr,		offsetof( ParticleSystemTemplate, m_emissionVelocity.outward.otherSpeed ) },

	{ "VolumeType",							INI::parseIndexList,													EmissionVolumeTypeNames,		offsetof( ParticleSystemTemplate, m_emissionVolumeType ) },
	{ "VolLineStart",						INI::parseCoord3D,														nullptr,		offsetof( ParticleSystemTemplate, m_emissionVolume.line.start ) },
	{ "VolLineEnd",							INI::parseCoord3D,														nullptr,		offsetof( ParticleSystemTemplate, m_emissionVolume.line.end ) },

	{ "VolBoxHalfSize",					INI::parseCoord3D,														nullptr,		offsetof( ParticleSystemTemplate, m_emissionVolume.box.halfSize ) },

	{ "VolSphereRadius",				INI::parseReal,																nullptr,		offsetof( ParticleSystemTemplate, m_emissionVolume.sphere.radius ) },

	{ "VolCylinderRadius",			INI::parseReal,																nullptr,		offsetof( ParticleSystemTemplate, m_emissionVolume.cylinder.radius ) },
	{ "VolCylinderLength",			INI::parseReal,																nullptr,		offsetof( ParticleSystemTemplate, m_emissionVolume.cylinder.length ) },

	{ "IsHollow",								INI::parseBool,																nullptr,		offsetof( ParticleSystemTemplate, m_isEmissionVolumeHollow ) },
	{ "IsGroundAligned",				INI::parseBool,																nullptr,		offsetof( ParticleSystemTemplate, m_isGroundAligned ) },
	{ "IsEmitAboveGroundOnly",	INI::parseBool,																nullptr,		offsetof( ParticleSystemTemplate, m_isEmitAboveGroundOnly) },
	{ "IsParticleUpTowardsEmitter",	INI::parseBool,																nullptr,		offsetof( ParticleSystemTemplate, m_isParticleUpTowardsEmitter) },

	{ "WindMotion",					INI::parseIndexList, WindMotionNames, offsetof( ParticleSystemTemplate, m_windMotion ) },

	{ "WindAngleChangeMin", INI::parseReal, nullptr, offsetof( ParticleSystemTemplate, m_windAngleChangeMin ) },
	{ "WindAngleChangeMax", INI::parseReal, nullptr, offsetof( ParticleSystemTemplate, m_windAngleChangeMax ) },

	{ "WindPingPongStartAngleMin",			INI::parseReal, nullptr, offsetof( ParticleSystemTemplate, m_windMotionStartAngleMin ) },
	{ "WindPingPongStartAngleMax",			INI::parseReal, nullptr, offsetof( ParticleSystemTemplate, m_windMotionStartAngleMax ) },

	{ "WindPingPongEndAngleMin",				INI::parseReal, nullptr, offsetof( ParticleSystemTemplate, m_windMotionEndAngleMin ) },
	{ "WindPingPongEndAngleMax",				INI::parseReal, nullptr, offsetof( ParticleSystemTemplate, m_windMotionEndAngleMax ) },


	{ nullptr,											nullptr,																					nullptr,		0 },
};

// ------------------------------------------------------------------------------------------------
/** Parse a "random keyframe".
 * The format is "FIELD = low high frame". */
// ------------------------------------------------------------------------------------------------
void ParticleSystemTemplate::parseRandomKeyframe( INI* ini, void *instance,
																											 void *store, const void* /*userData*/ )
{
	RandomKeyframe *key = static_cast<RandomKeyframe *>(store);

	Real low = ini->scanReal(ini->getNextToken());
	Real high = ini->scanReal(ini->getNextToken());
	key->frame = ini->scanUnsignedInt(ini->getNextToken());

	// set the range of the random variable
	key->var.setRange( low, high );
}

// ------------------------------------------------------------------------------------------------
/** Parse a "color keyframe".
 * The format is "FIELD = R:r G:g B:b frame". */
// ------------------------------------------------------------------------------------------------
void ParticleSystemTemplate::parseRGBColorKeyframe( INI* ini, void *instance,
																													void *store, const void* /*userData*/ )
{
	RGBColorKeyframe *key = static_cast<RGBColorKeyframe *>(store);

	INI::parseRGBColor( ini, instance, &key->color, nullptr );
	INI::parseUnsignedInt( ini, instance, &key->frame, nullptr );
}

// ------------------------------------------------------------------------------------------------
/** Parse a RandomVariable RGB color.
 * Note that the components may be negative, as this is used for rates, as well. */
// ------------------------------------------------------------------------------------------------
void ParticleSystemTemplate::parseRandomRGBColor( INI* ini, void *instance, 
																											 void *store, const void* /*userData*/ )
{
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
ParticleSystemTemplate::ParticleSystemTemplate( const AsciiString &name ) :
	m_name(name)
{ 
	//Added By Sadullah Nader
	//Initializations inserted
	m_slaveTemplate = nullptr;
	//
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
ParticleSystemTemplate::~ParticleSystemTemplate()
{
 
}

// ------------------------------------------------------------------------------------------------
/** If returns non-NULL, it is a slave system for use ... the create slaves parameter
 * tells *this* slave system whether or not it should create any slaves itself
 * automatically during its own constructor */
// ------------------------------------------------------------------------------------------------
ParticleSystem *ParticleSystemTemplate::createSlaveSystem( Bool createSlaves ) const
{
	if (m_slaveTemplate == nullptr && m_slaveSystemName.isEmpty() == false)
		m_slaveTemplate = TheParticleSystemManager->findTemplate( m_slaveSystemName );

	ParticleSystem *slave = nullptr;
	if (m_slaveTemplate)
		slave = TheParticleSystemManager->createParticleSystem( m_slaveTemplate, createSlaves );

	return slave;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// ParticleSystemManager //////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
static Real angleBetween(const Coord2D *vecA, const Coord2D *vecB)
{
	if (!(vecA && vecA->length() && vecB && vecB->length())) {
		return 0.0;
	}

	Real lengthA = vecA->length();
	Real lengthB = vecB->length();
	Real dotProduct = (vecA->x * vecB->x + vecA->y * vecB->y);
	Real cosTheta = dotProduct / (lengthA * lengthB);

	// If the dotproduct is 0.0, then they are orthogonal
	if (dotProduct == 0.0f) {
		if (vecB->x > 0) {
			return PI;
		}

		return 0.0f;
	}

	Real theta = ACos( cosTheta );

	if (vecB->x > 0) {
		return theta;
	}
	
	return -theta;
}

