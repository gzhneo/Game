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

#include "Common/AudioEventInfo.h"
#include "Common/DynamicAudioEventInfo.h"
#include "Common/AudioSettings.h"
#include "Common/BitFlagsIO.h"
#include "Common/BuildAssistant.h"
#include "Common/ClientUpdateModule.h"
#include "Common/DrawModule.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/GameLOD.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/ModuleFactory.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"		// for logic frame count
#include "GameLogic/Object.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "GameLogic/Module/StickyBombUpdate.h"
#include "GameLogic/Module/BattlePlanUpdate.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Weapon.h"
#include "GameClient/Anim2D.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawGroupInfo.h"
#include "GameClient/GameClient.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Image.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/LanguageFilter.h"
#include "GameClient/Shadow.h"
#include "GameClient/GameText.h"

// Drawable: the client-side physics look -- pitch, roll and bounce of treads, wheels, hover and wings

//-------------------------------------------------------------------------------------------------
void Drawable::applyPhysicsXform(Matrix3D* mtx)
{
	const Object *obj = getObject();

	if( !obj ||	obj->isDisabledByType( DISABLED_HELD ) || !TheGlobalData->m_showClientPhysics )
	{
		return;
	}

 	Bool frozen = TheTacticalView->isTimeFrozen() && !TheTacticalView->isCameraMovementFinished();
 	frozen = frozen || TheScriptEngine->isTimeFrozenDebug() || TheScriptEngine->isTimeFrozenScript();
	if (frozen)
		return;
	PhysicsXformInfo info;
	if (calcPhysicsXform(info))
	{
		mtx->Translate(0.0f, 0.0f, info.m_totalZ);
		mtx->Rotate_Y( info.m_totalPitch );
		mtx->Rotate_X( -info.m_totalRoll );
		mtx->Rotate_Z( info.m_totalYaw );

	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool Drawable::calcPhysicsXform(PhysicsXformInfo& info)
{
	const Object* obj = getObject();
	const AIUpdateInterface *ai = obj ? obj->getAIUpdateInterface() : nullptr;
	Bool hasPhysicsXform = false;
	if (ai) 
	{
		const Locomotor *locomotor = ai->getCurLocomotor(); 
		if (locomotor) 
		{
			switch (locomotor->getAppearance())
			{
				case LOCO_WHEELS_FOUR:
					calcPhysicsXformWheels(locomotor, info);
					hasPhysicsXform = true;
					break;
				case LOCO_MOTORCYCLE:
					calcPhysicsXformMotorcycle( locomotor, info );
					hasPhysicsXform = TRUE;
					break;
				case LOCO_TREADS:
					calcPhysicsXformTreads(locomotor, info);
					hasPhysicsXform = true;
					break;
				case LOCO_HOVER:
				case LOCO_WINGS:
					calcPhysicsXformHoverOrWings(locomotor, info);
					hasPhysicsXform = true;
					break;
				case LOCO_THRUST:	
					calcPhysicsXformThrust(locomotor, info);
					hasPhysicsXform = true;
					break;
			}
		}
	}

  if (hasPhysicsXform)
  {
    // HOTFIX: Ensure that we are not passing denormalized values back to caller
    // @todo remove hotfix
    if (info.m_totalPitch>-1e-20f&&info.m_totalPitch<1e-20f)
      info.m_totalPitch=0.f;
    if (info.m_totalRoll>-1e-20f&&info.m_totalRoll<1e-20f)
      info.m_totalRoll=0.f;
    if (info.m_totalYaw>-1e-20f&&info.m_totalYaw<1e-20f)
      info.m_totalYaw=0.f;
    if (info.m_totalZ>-1e-20f&&info.m_totalZ<1e-20f)
      info.m_totalZ=0.f;
  }

	return hasPhysicsXform;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void Drawable::calcPhysicsXformThrust( const Locomotor *locomotor, PhysicsXformInfo& info )
{
	if (m_locoInfo == nullptr)
		m_locoInfo = newInstance(DrawableLocoInfo);

	Real THRUST_ROLL = locomotor->getThrustRoll();
	Real WOBBLE_RATE = locomotor->getWobbleRate();
	Real MAX_WOBBLE  = locomotor->getMaxWobble();
	Real MIN_WOBBLE  = locomotor->getMinWobble();

	//
	// this is a kind of quick thrust implementation cause we need scud missiles to wobble *now*,
	// we deal with just adjusting pitch, yaw, and roll just a little bit
	//

	if( WOBBLE_RATE )
	{

		if( m_locoInfo->m_wobble >= 1.0f )
		{

			if( m_locoInfo->m_pitch < MAX_WOBBLE - WOBBLE_RATE * 2 )
			{

				m_locoInfo->m_pitch += WOBBLE_RATE;
				m_locoInfo->m_yaw += WOBBLE_RATE;

			}  // end if
			else
			{

				m_locoInfo->m_pitch += (WOBBLE_RATE / 2.0f);
				m_locoInfo->m_yaw += (WOBBLE_RATE / 2.0f);

			}  // end else

			if( m_locoInfo->m_pitch >= MAX_WOBBLE )
				m_locoInfo->m_wobble = -1.0f;

		}  // end if
		else
		{

			if( m_locoInfo->m_pitch >= MIN_WOBBLE + WOBBLE_RATE * 2.0f )
			{

				m_locoInfo->m_pitch -= WOBBLE_RATE;
				m_locoInfo->m_yaw -= WOBBLE_RATE;

			}  // end if
			else
			{

				m_locoInfo->m_pitch -= (WOBBLE_RATE / 2.0f);
				m_locoInfo->m_yaw -= (WOBBLE_RATE / 2.0f);

			}  // end else
			if( m_locoInfo->m_pitch <= MIN_WOBBLE )
				m_locoInfo->m_wobble = 1.0f;

		}  // end else

		info.m_totalPitch = m_locoInfo->m_pitch;
		info.m_totalYaw = m_locoInfo->m_yaw;

	}  // end if, wobble exists

	if( THRUST_ROLL )
	{

		m_locoInfo->m_roll += THRUST_ROLL;
		info.m_totalRoll = m_locoInfo->m_roll;

	}  // end if

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void Drawable::calcPhysicsXformHoverOrWings( const Locomotor *locomotor, PhysicsXformInfo& info )
{
	if (m_locoInfo == nullptr)
		m_locoInfo = newInstance(DrawableLocoInfo);

	const Real ACCEL_PITCH_LIMIT = locomotor->getAccelPitchLimit();
	const Real DECEL_PITCH_LIMIT = locomotor->getDecelPitchLimit();
	const Real PITCH_STIFFNESS = locomotor->getPitchStiffness();
	const Real ROLL_STIFFNESS =  locomotor->getRollStiffness();
	const Real PITCH_DAMPING = locomotor->getPitchDamping();
	const Real ROLL_DAMPING = locomotor->getRollDamping();
	const Real Z_VEL_PITCH_COEFF = locomotor->getPitchByZVelCoef();	
	const Real FORWARD_VEL_COEFF = locomotor->getForwardVelCoef();	
	const Real LATERAL_VEL_COEFF = locomotor->getLateralVelCoef();	
	const Real FORWARD_ACCEL_COEFF = locomotor->getForwardAccelCoef();	
	const Real LATERAL_ACCEL_COEFF = locomotor->getLateralAccelCoef();	
	const Real UNIFORM_AXIAL_DAMPING = locomotor->getUniformAxialDamping();	


	// get object from logic
	Object *obj = getObject();
	if (obj == nullptr)
		return;

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (ai == nullptr)
		return;

	// get object physics state
	PhysicsBehavior *physics = obj->getPhysics();
	if (physics == nullptr)
		return;

	// get our position and direction vector
//const Coord3D *pos = getPosition();
	const Coord3D* dir = getUnitDirectionVector2D();
	const Coord3D* accel = physics->getAcceleration();
	const Coord3D* vel = physics->getVelocity();

	m_locoInfo->m_pitchRate += ((-PITCH_STIFFNESS * m_locoInfo->m_pitch) + (-PITCH_DAMPING * m_locoInfo->m_pitchRate));		// spring/damper
	m_locoInfo->m_rollRate += ((-ROLL_STIFFNESS * m_locoInfo->m_roll) + (-ROLL_DAMPING * m_locoInfo->m_rollRate));		// spring/damper

	m_locoInfo->m_pitch += m_locoInfo->m_pitchRate * UNIFORM_AXIAL_DAMPING;
	m_locoInfo->m_roll += m_locoInfo->m_rollRate   * UNIFORM_AXIAL_DAMPING;

	// process chassis acceleration dynamics - damp back towards zero

	m_locoInfo->m_accelerationPitchRate += ((-PITCH_STIFFNESS * (m_locoInfo->m_accelerationPitch)) + (-PITCH_DAMPING * m_locoInfo->m_accelerationPitchRate));		// spring/damper
	m_locoInfo->m_accelerationPitch += m_locoInfo->m_accelerationPitchRate;

	m_locoInfo->m_accelerationRollRate += ((-ROLL_STIFFNESS * m_locoInfo->m_accelerationRoll) + (-ROLL_DAMPING * m_locoInfo->m_accelerationRollRate));		// spring/damper
	m_locoInfo->m_accelerationRoll += m_locoInfo->m_accelerationRollRate;

	// compute total pitch and roll of tank
	info.m_totalPitch = m_locoInfo->m_pitch + m_locoInfo->m_accelerationPitch;
	info.m_totalRoll = m_locoInfo->m_roll + m_locoInfo->m_accelerationRoll;

	if (physics->isMotive()) 
  {
		if (Z_VEL_PITCH_COEFF != 0.0f)
		{
			const Real TINY_DZ = 0.001f;
			if (fabs(vel->z) > TINY_DZ)
			{
				Real pitch = atan2(vel->z, sqrt(sqr(vel->x)+sqr(vel->y)));
				m_locoInfo->m_pitch -= Z_VEL_PITCH_COEFF * pitch;
			}
		}

		// cause the chassis to pitch & roll in reaction to current speed
		Real forwardVel = dir->x * vel->x + dir->y * vel->y;
		m_locoInfo->m_pitch += -(FORWARD_VEL_COEFF * forwardVel);

		Real lateralVel = -dir->y * vel->x + dir->x * vel->y;
		m_locoInfo->m_roll += -(LATERAL_VEL_COEFF * lateralVel);

		// cause the chassis to pitch & roll in reaction to acceleration/deceleration
		Real forwardAccel = dir->x * accel->x + dir->y * accel->y;
		m_locoInfo->m_accelerationPitchRate += -(FORWARD_ACCEL_COEFF * forwardAccel);

		Real lateralAccel = -dir->y * accel->x + dir->x * accel->y;
		m_locoInfo->m_accelerationRollRate += -(LATERAL_ACCEL_COEFF * lateralAccel);
	}

	// limit acceleration pitch and roll

	if (m_locoInfo->m_accelerationPitch > DECEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = DECEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationPitch < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = -ACCEL_PITCH_LIMIT;

	if (m_locoInfo->m_accelerationRoll > DECEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = DECEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationRoll < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = -ACCEL_PITCH_LIMIT;



	const Real RUDDER_CORRECTION_DEGREE   = locomotor->getRudderCorrectionDegree();	
	const Real RUDDER_CORRECTION_RATE     = locomotor->getRudderCorrectionRate();	
	const Real ELEVATOR_CORRECTION_DEGREE = locomotor->getElevatorCorrectionDegree();	
	const Real ELEVATOR_CORRECTION_RATE   = locomotor->getElevatorCorrectionRate();	

  info.m_totalYaw = RUDDER_CORRECTION_DEGREE * sin( m_locoInfo->m_yawModulator += RUDDER_CORRECTION_RATE );
  info.m_totalPitch += ELEVATOR_CORRECTION_DEGREE * cos( m_locoInfo->m_pitchModulator += ELEVATOR_CORRECTION_RATE );


	info.m_totalZ = 0.0f;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void Drawable::calcPhysicsXformTreads( const Locomotor *locomotor, PhysicsXformInfo& info )
{
	if (m_locoInfo == nullptr)
		m_locoInfo = newInstance(DrawableLocoInfo);

	const Real OVERLAP_SHRINK_FACTOR = 0.8f;
	const Real FLATTENED_OBJECT_HEIGHT = 0.5f;
	const Real LEAVE_OVERLAP_PITCH_KICK = PI/128;
	const Real OVERLAP_ROUGH_VIBRATION_FACTOR = 5.0f;
	const Real MAX_ROUGH_VIBRATION = 0.5f;
	const Real ACCEL_PITCH_LIMIT = locomotor->getAccelPitchLimit();
	const Real DECEL_PITCH_LIMIT = locomotor->getDecelPitchLimit();
	const Real PITCH_STIFFNESS = locomotor->getPitchStiffness();
	const Real ROLL_STIFFNESS =  locomotor->getRollStiffness();
	const Real PITCH_DAMPING = locomotor->getPitchDamping();
	const Real ROLL_DAMPING = locomotor->getRollDamping();
	const Real FORWARD_ACCEL_COEFF = locomotor->getForwardAccelCoef();	
	const Real LATERAL_ACCEL_COEFF = locomotor->getLateralAccelCoef();	
	const Real UNIFORM_AXIAL_DAMPING = locomotor->getUniformAxialDamping();	

	// get object from logic
	Object *obj = getObject();
	if (obj == nullptr)
		return;

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (ai == nullptr)
		return ;

	// get object physics state
	PhysicsBehavior *physics = obj->getPhysics();
	if (physics == nullptr)
		return;

	// get our position and direction vector
	const Coord3D *pos = getPosition();
	const Coord3D *dir = getUnitDirectionVector2D();
	const Coord3D *accel = physics->getAcceleration();
	const Coord3D *vel = physics->getVelocity();

	// compute perpendicular (2d)
	Coord3D perp;
	perp.x = -dir->y;
	perp.y = dir->x;
	perp.z = 0.0f;

	// find pitch and roll of terrain under chassis
	Coord3D normal;
/*	Real hheight = */ TheTerrainLogic->getLayerHeight( pos->x, pos->y, obj->getLayer(), &normal );

	// override surface normal if we are overlapping another object - crushing it
	Real overlapZ = 0.0f;

	// get object we are currently overlapping, if any
	Object* overlapped = TheGameLogic->findObjectByID(physics->getCurrentOverlap());
	if (overlapped && overlapped->isKindOf(KINDOF_SHRUBBERY)) {
		overlapped = nullptr; // We just smash through shrubbery.  jba.
	}

	if (overlapped)
	{
		const Coord3D *overPos = overlapped->getPosition();
		Real dx = overPos->x - pos->x;
		Real dy = overPos->y - pos->y;
		Real centerDistSqr = sqr(dx) + sqr(dy);

		// compute maximum distance between objects, if their edges just touched
		Real ourSize = getDrawableGeometryInfo().getBoundingCircleRadius();
		Real otherSize = overlapped->getGeometryInfo().getBoundingCircleRadius();
		Real maxCenterDist = otherSize + ourSize;

		// shrink the overlap distance a bit to avoid floating
		maxCenterDist *= OVERLAP_SHRINK_FACTOR;
		if (centerDistSqr < sqr(maxCenterDist))
		{
			Real centerDist = sqrtf(centerDistSqr);
			Real amount = 1.0f - centerDist/maxCenterDist;
			if (amount < 0.0f)
				amount = 0.0f;
			else if (amount > 1.0f)
				amount = 1.0f;

			// rough vibrations proportional to speed when we drive over something
			Real rough = (vel->x*vel->x + vel->y*vel->y) * OVERLAP_ROUGH_VIBRATION_FACTOR;
			if (rough > MAX_ROUGH_VIBRATION)
				rough = MAX_ROUGH_VIBRATION;
			
			Real height = overlapped->getGeometryInfo().getMaxHeightAbovePosition();

			// do not "go up" flattened crushed things
			Bool flat = false;
			if (overlapped->isKindOf(KINDOF_LOW_OVERLAPPABLE) ||
					overlapped->isKindOf(KINDOF_INFANTRY) ||
					(overlapped->getBodyModule()->getFrontCrushed() && overlapped->getBodyModule()->getBackCrushed()))
			{
				flat = true;
				height = FLATTENED_OBJECT_HEIGHT;
			}

			if (amount < FLATTENED_OBJECT_HEIGHT && flat == false)
			{
				overlapZ = height * 2.0f * amount;

				// compute vector along "surface"
				// not proportional to actual geometry to avoid overlay steep inclines, etc
				Coord3D v;
				v.x = dx/centerDist;
				v.y = dy/centerDist;
				v.z = 0.2f;		// 0.25

				Coord3D up;
				up.x = GameClientRandomValueReal( -rough, rough );
				up.y = GameClientRandomValueReal( -rough, rough );
				up.z = 1.0f;
				up.normalize();

				Coord3D prp;
				prp.crossProduct( &v, &up, &prp );
				normal.crossProduct( &prp, &v, &normal );

				// compute unit normal
				normal.normalize();
			}
			else
			{
				// sitting on top of object
				overlapZ = height;

				normal.x = GameClientRandomValueReal( -rough, rough );
				normal.y = GameClientRandomValueReal( -rough, rough );
				normal.z = 1.0f;
				normal.normalize();
			}
		}
	}
	else	// no overlap this frame
	{
		// if we had an overlap last frame, and we're now in the air, give a
		// kick to the pitch for effect
		if (physics->getPreviousOverlap() != INVALID_ID && m_locoInfo->m_overlapZ > 0.0f)
			m_locoInfo->m_pitchRate += LEAVE_OVERLAP_PITCH_KICK;
	}



	Real dot = normal.x * dir->x + normal.y * dir->y;
	Real groundPitch = dot * (PI/2.0f);

	dot = normal.x * perp.x + normal.y * perp.y;
	Real groundRoll = dot * (PI/2.0f);

	// process chassis suspension dynamics - damp back towards groundPitch

	// the ground can only push back if we're touching it
	if (overlapped || m_locoInfo->m_overlapZ <= 0.0f)
	{
		m_locoInfo->m_pitchRate += ((-PITCH_STIFFNESS * (m_locoInfo->m_pitch - groundPitch)) + (-PITCH_DAMPING * m_locoInfo->m_pitchRate));		// spring/damper
		if (m_locoInfo->m_pitchRate > 0.0f)
			m_locoInfo->m_pitchRate *= 0.5f;

		m_locoInfo->m_rollRate += ((-ROLL_STIFFNESS * (m_locoInfo->m_roll - groundRoll)) + (-ROLL_DAMPING * m_locoInfo->m_rollRate));		// spring/damper
	}

	m_locoInfo->m_pitch += m_locoInfo->m_pitchRate * UNIFORM_AXIAL_DAMPING;
	m_locoInfo->m_roll += m_locoInfo->m_rollRate   * UNIFORM_AXIAL_DAMPING;

	// process chassis recoil dynamics - damp back towards zero

	m_locoInfo->m_accelerationPitchRate += ((-PITCH_STIFFNESS * (m_locoInfo->m_accelerationPitch)) + (-PITCH_DAMPING * m_locoInfo->m_accelerationPitchRate));		// spring/damper
	m_locoInfo->m_accelerationPitch += m_locoInfo->m_accelerationPitchRate;

	m_locoInfo->m_accelerationRollRate += ((-ROLL_STIFFNESS * m_locoInfo->m_accelerationRoll) + (-ROLL_DAMPING * m_locoInfo->m_accelerationRollRate));		// spring/damper
	m_locoInfo->m_accelerationRoll += m_locoInfo->m_accelerationRollRate;

	// compute total pitch and roll of tank
	info.m_totalPitch = m_locoInfo->m_pitch + m_locoInfo->m_accelerationPitch;
	info.m_totalRoll = m_locoInfo->m_roll + m_locoInfo->m_accelerationRoll;

	if (physics->isMotive()) 
	{
		// cause the chassis to pitch & roll in reaction to acceleration/deceleration
		Real forwardAccel = dir->x * accel->x + dir->y * accel->y;
		m_locoInfo->m_accelerationPitchRate += -(FORWARD_ACCEL_COEFF * forwardAccel);

		Real lateralAccel = -dir->y * accel->x + dir->x * accel->y;
		m_locoInfo->m_accelerationRollRate += -(LATERAL_ACCEL_COEFF * lateralAccel);
	}

#ifdef RECOIL_FROM_BEING_DAMAGED
	// recoil from being hit
	/// @todo Recoil needs to be based on sane damage amounts (MSB)
	const DamageInfo *damageInfo = obj->getBodyModule()->getLastDamageInfo();
	if (damageInfo)
	{
		if (obj->getBodyModule()->getLastDamageTimestamp() > m_lastDamageTimestamp && damageInfo->in.m_amount > RECOIL_DAMAGE)
		{
			Object *attacker = TheGameLogic->getObject( damageInfo->in.m_sourceID );
			if (attacker)
			{
				Coord3D to;
				ThePartitionManager->getVectorTo( obj, attacker, DistanceCalculationType::FROM_CENTER_2D, &to );

				to.normalize();

				Real forward = dir->x * to.x + dir->y * to.y;
				Real lateral = perp.x * to.x + perp.y * to.y;

				Real recoil = PI/16.0f * GameClientRandomValueReal( 0.5f, 1.0f );
			
				m_locoInfo->m_accelerationPitchRate -= recoil * forward;
				m_locoInfo->m_accelerationRollRate -= recoil * lateral;
			}

			m_lastDamageTimestamp = obj->getBodyModule()->getLastDamageTimestamp();
		}
	}
#endif

	// limit recoil pitch and roll

	if (m_locoInfo->m_accelerationPitch > DECEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = DECEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationPitch < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = -ACCEL_PITCH_LIMIT;

	if (m_locoInfo->m_accelerationRoll > DECEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = DECEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationRoll < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = -ACCEL_PITCH_LIMIT;

	// adjust z
	if (overlapZ > m_locoInfo->m_overlapZ)
	{
		m_locoInfo->m_overlapZ = overlapZ;
		/// @todo Z needs to accelerate/decelerate, not be directly set (MSB)
		// m_locoInfo->m_overlapZ += 0.4f;
		m_locoInfo->m_overlapZVel = 0.0f;
	}
	
	Real ztmp = m_locoInfo->m_overlapZ/2.0f;

	// do fake Z physics
	if (m_locoInfo->m_overlapZ > 0.0f)
	{
		m_locoInfo->m_overlapZVel -= 0.2f;
		m_locoInfo->m_overlapZ += m_locoInfo->m_overlapZVel;
	}

	if (m_locoInfo->m_overlapZ <= 0.0f)
	{
		m_locoInfo->m_overlapZ = 0.0f;
		m_locoInfo->m_overlapZVel = 0.0f; 
	}
	info.m_totalZ = ztmp;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void Drawable::calcPhysicsXformWheels( const Locomotor *locomotor, PhysicsXformInfo& info )
{
	if (m_locoInfo == nullptr)
		m_locoInfo = newInstance(DrawableLocoInfo);

	const Real ACCEL_PITCH_LIMIT = locomotor->getAccelPitchLimit();
	const Real DECEL_PITCH_LIMIT = locomotor->getDecelPitchLimit();
	const Real BOUNCE_ANGLE_KICK = locomotor->getBounceKick();
	const Real PITCH_STIFFNESS = locomotor->getPitchStiffness();
	const Real ROLL_STIFFNESS =  locomotor->getRollStiffness();
	const Real PITCH_DAMPING = locomotor->getPitchDamping();
	const Real ROLL_DAMPING = locomotor->getRollDamping();
	const Real FORWARD_ACCEL_COEFF = locomotor->getForwardAccelCoef();	
	const Real LATERAL_ACCEL_COEFF = locomotor->getLateralAccelCoef();	
	const Real UNIFORM_AXIAL_DAMPING = locomotor->getUniformAxialDamping();	

	const Real MAX_SUSPENSION_EXTENSION = locomotor->getMaxWheelExtension(); //-2.3f;
//	const Real MAX_SUSPENSION_COMPRESSION = locomotor->getMaxWheelCompression(); //1.4f;
	const Real WHEEL_ANGLE = locomotor->getWheelTurnAngle(); //PI/8;

	const Bool DO_WHEELS = locomotor->hasSuspension();

	// get object from logic
	Object *obj = getObject();
	if (obj == nullptr)
		return;

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (ai == nullptr)
		return ;

	// get object physics state
	PhysicsBehavior *physics = obj->getPhysics();
	if (physics == nullptr)
		return ;

	// get our position and direction vector
	const Coord3D *pos = getPosition();
	const Coord3D *dir = getUnitDirectionVector2D();
	const Coord3D *accel = physics->getAcceleration();

	// compute perpendicular (2d)
	Coord3D perp;
	perp.x = -dir->y;
	perp.y = dir->x;
	perp.z = 0.0f;

	// find pitch and roll of terrain under chassis
	Coord3D normal;
	Real hheight = TheTerrainLogic->getLayerHeight( pos->x, pos->y, obj->getLayer(), &normal );

	Real dot = normal.x * dir->x + normal.y * dir->y;
	Real groundPitch = dot * (PI/2.0f);

	dot = normal.x * perp.x + normal.y * perp.y;
	Real groundRoll = dot * (PI/2.0f);

	Bool airborne = obj->isSignificantlyAboveTerrain();

	if (airborne) 
	{
		if (DO_WHEELS) 
		{	
			// Wheels extend when airborne.
			m_locoInfo->m_wheelInfo.m_framesAirborne = 0;
			m_locoInfo->m_wheelInfo.m_framesAirborneCounter++;
			if (pos->z - hheight > -MAX_SUSPENSION_EXTENSION) 
			{
				m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset += (MAX_SUSPENSION_EXTENSION - m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset)/2.0f;
				m_locoInfo->m_wheelInfo.m_rearRightHeightOffset += (MAX_SUSPENSION_EXTENSION - m_locoInfo->m_wheelInfo.m_rearRightHeightOffset)/2.0f;
			} 
			else 
			{
				m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset += (0 - m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset)/2.0f;
				m_locoInfo->m_wheelInfo.m_rearRightHeightOffset += (0 - m_locoInfo->m_wheelInfo.m_rearRightHeightOffset)/2.0f;
			}
		}
		// Calculate suspension info.
		Real length = obj->getGeometryInfo().getMajorRadius();
		Real width = obj->getGeometryInfo().getMinorRadius();
		Real pitchHeight = length*Sin(m_locoInfo->m_pitch + m_locoInfo->m_accelerationPitch - groundPitch);	
		Real rollHeight = width*Sin(m_locoInfo->m_roll + m_locoInfo->m_accelerationRoll - groundRoll);
		info.m_totalZ = fabs(pitchHeight)/4 + fabs(rollHeight)/4;
		return; // maintain the same orientation while we fly through the air.
	}

	// Bouncy.
	Real curSpeed = physics->getVelocityMagnitude();
#if 1 
	Real maxSpeed = ai->getCurLocomotorSpeed();
	if (!airborne && curSpeed > maxSpeed/10) 
	{
		Real factor = curSpeed/maxSpeed;
		if (fabs(m_locoInfo->m_pitchRate)<factor*BOUNCE_ANGLE_KICK/4 && fabs(m_locoInfo->m_rollRate)<factor*BOUNCE_ANGLE_KICK/8) 
		{
			// do the bouncy. 
			switch (GameClientRandomValue(0,3)) 
			{
			case 0:
				m_locoInfo->m_pitchRate -= BOUNCE_ANGLE_KICK*factor;
				m_locoInfo->m_rollRate -= BOUNCE_ANGLE_KICK*factor/2;
				break;
			case 1:
				m_locoInfo->m_pitchRate += BOUNCE_ANGLE_KICK*factor;
				m_locoInfo->m_rollRate -= BOUNCE_ANGLE_KICK*factor/2;
				break;
			case 2:
				m_locoInfo->m_pitchRate -= BOUNCE_ANGLE_KICK*factor;
				m_locoInfo->m_rollRate += BOUNCE_ANGLE_KICK*factor/2;
				break;
			case 3:
				m_locoInfo->m_pitchRate += BOUNCE_ANGLE_KICK*factor;
				m_locoInfo->m_rollRate += BOUNCE_ANGLE_KICK*factor/2;
				break;
			}
		}
	}

#endif

	// process chassis suspension dynamics - damp back towards groundPitch

	// the ground can only push back if we're touching it
	if (!airborne)
	{
		m_locoInfo->m_pitchRate += ((-PITCH_STIFFNESS * (m_locoInfo->m_pitch - groundPitch)) + (-PITCH_DAMPING * m_locoInfo->m_pitchRate));		// spring/damper
		if (m_locoInfo->m_pitchRate > 0.0f)
			m_locoInfo->m_pitchRate *= 0.5f;

		m_locoInfo->m_rollRate += ((-ROLL_STIFFNESS * (m_locoInfo->m_roll - groundRoll)) + (-ROLL_DAMPING * m_locoInfo->m_rollRate));		// spring/damper
	}

	m_locoInfo->m_pitch += m_locoInfo->m_pitchRate * UNIFORM_AXIAL_DAMPING;
	m_locoInfo->m_roll += m_locoInfo->m_rollRate   * UNIFORM_AXIAL_DAMPING;

	// process chassis acceleration dynamics - damp back towards zero

	m_locoInfo->m_accelerationPitchRate += ((-PITCH_STIFFNESS * (m_locoInfo->m_accelerationPitch)) + (-PITCH_DAMPING * m_locoInfo->m_accelerationPitchRate));		// spring/damper
	m_locoInfo->m_accelerationPitch += m_locoInfo->m_accelerationPitchRate;

	m_locoInfo->m_accelerationRollRate += ((-ROLL_STIFFNESS * m_locoInfo->m_accelerationRoll) + (-ROLL_DAMPING * m_locoInfo->m_accelerationRollRate));		// spring/damper
	m_locoInfo->m_accelerationRoll += m_locoInfo->m_accelerationRollRate;

	// compute total pitch and roll of tank
	info.m_totalPitch = m_locoInfo->m_pitch + m_locoInfo->m_accelerationPitch;
	info.m_totalRoll = m_locoInfo->m_roll + m_locoInfo->m_accelerationRoll;

	if (physics->isMotive()) 
	{
		// cause the chassis to pitch & roll in reaction to acceleration/deceleration
		Real forwardAccel = dir->x * accel->x + dir->y * accel->y;
		m_locoInfo->m_accelerationPitchRate += -(FORWARD_ACCEL_COEFF * forwardAccel);

		Real lateralAccel = -dir->y * accel->x + dir->x * accel->y;
		m_locoInfo->m_accelerationRollRate += -(LATERAL_ACCEL_COEFF * lateralAccel);
	}

	// limit acceleration pitch and roll

	if (m_locoInfo->m_accelerationPitch > DECEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = DECEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationPitch < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = -ACCEL_PITCH_LIMIT;

	if (m_locoInfo->m_accelerationRoll > DECEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = DECEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationRoll < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = -ACCEL_PITCH_LIMIT;

	info.m_totalZ = 0;

	// Calculate suspension info.
	Real length = obj->getGeometryInfo().getMajorRadius();
	Real width = obj->getGeometryInfo().getMinorRadius();
	Real pitchHeight = length*Sin(info.m_totalPitch-groundPitch);
	Real rollHeight = width*Sin(info.m_totalRoll-groundRoll);
	if (DO_WHEELS) 
	{	
		// calculate each wheel position
		m_locoInfo->m_wheelInfo.m_framesAirborne = m_locoInfo->m_wheelInfo.m_framesAirborneCounter;
		m_locoInfo->m_wheelInfo.m_framesAirborneCounter = 0;
		TWheelInfo newInfo = m_locoInfo->m_wheelInfo;
		PhysicsTurningType rotation = physics->getTurning();
		if (rotation == TURN_NEGATIVE) {
			newInfo.m_wheelAngle = -WHEEL_ANGLE;
		} else if (rotation == TURN_POSITIVE) {
			newInfo.m_wheelAngle = WHEEL_ANGLE;
		}	else {
			newInfo.m_wheelAngle = 0;
		}
		if (physics->getForwardSpeed2D() < 0.0f) {
			// if we're moving backwards, the wheels rotate in the opposite direction.
			newInfo.m_wheelAngle = -newInfo.m_wheelAngle;
		}

		//
		///@todo Steven/John ... please review this and make sure it makes sense (CBD)
		// we're going to add the angle to the current wheel rotation ... but we're going to 
		// divide that number to add small angles.  This allows for "smoother" wheel turning
		// transitions ... and when the AI has things move in a straight line, since it's
		// constantly telling the object to go left, go straight, go right, go straight,
		// etc, this smaller angle we'll be adding covers the constant wheel shifting 
		// left and right when moving in a relatively straight line
		//
		#define WHEEL_SMOOTHNESS 10.0f  // higher numbers add smaller angles, make it more "smooth"
		m_locoInfo->m_wheelInfo.m_wheelAngle += (newInfo.m_wheelAngle - m_locoInfo->m_wheelInfo.m_wheelAngle)/WHEEL_SMOOTHNESS;

		const Real SPRING_FACTOR = 0.9f;
		if (pitchHeight<0) {	// Front raising up
			newInfo.m_frontLeftHeightOffset = SPRING_FACTOR*(pitchHeight/3+pitchHeight/2);
			newInfo.m_frontRightHeightOffset = SPRING_FACTOR*(pitchHeight/3+pitchHeight/2);
			newInfo.m_rearLeftHeightOffset = -pitchHeight/2 + pitchHeight/4;
			newInfo.m_rearRightHeightOffset = -pitchHeight/2 + pitchHeight/4;
		}	else {	// Back rasing up.
			newInfo.m_frontLeftHeightOffset = (-pitchHeight/4+pitchHeight/2);
			newInfo.m_frontRightHeightOffset = (-pitchHeight/4+pitchHeight/2);
			newInfo.m_rearLeftHeightOffset = SPRING_FACTOR*(-pitchHeight/2 + -pitchHeight/3);
			newInfo.m_rearRightHeightOffset = SPRING_FACTOR*(-pitchHeight/2 + -pitchHeight/3);
		}
		if (rollHeight>0) {	// Right raising up
			newInfo.m_frontRightHeightOffset += -SPRING_FACTOR*(rollHeight/3+rollHeight/2);
			newInfo.m_rearRightHeightOffset += -SPRING_FACTOR*(rollHeight/3+rollHeight/2);
			newInfo.m_rearLeftHeightOffset += rollHeight/2 - rollHeight/4;
			newInfo.m_frontLeftHeightOffset += rollHeight/2 - rollHeight/4;
		}	else {	// Left rasing up.
			newInfo.m_frontRightHeightOffset += -rollHeight/2 + rollHeight/4;
			newInfo.m_rearRightHeightOffset += -rollHeight/2 + rollHeight/4;
			newInfo.m_rearLeftHeightOffset += SPRING_FACTOR*(rollHeight/3+rollHeight/2);
			newInfo.m_frontLeftHeightOffset += SPRING_FACTOR*(rollHeight/3+rollHeight/2);
		}
		if (newInfo.m_frontLeftHeightOffset < m_locoInfo->m_wheelInfo.m_frontLeftHeightOffset) {
			// If it's going down, dampen the movement a bit
			m_locoInfo->m_wheelInfo.m_frontLeftHeightOffset += (newInfo.m_frontLeftHeightOffset - m_locoInfo->m_wheelInfo.m_frontLeftHeightOffset)/2.0f;
		}	else {
			m_locoInfo->m_wheelInfo.m_frontLeftHeightOffset = newInfo.m_frontLeftHeightOffset;
		}
		if (newInfo.m_frontRightHeightOffset < m_locoInfo->m_wheelInfo.m_frontRightHeightOffset) {
			// If it's going down, dampen the movement a bit
			m_locoInfo->m_wheelInfo.m_frontRightHeightOffset += (newInfo.m_frontRightHeightOffset - m_locoInfo->m_wheelInfo.m_frontRightHeightOffset)/2.0f;
		}	else {
			m_locoInfo->m_wheelInfo.m_frontRightHeightOffset = newInfo.m_frontRightHeightOffset;
		}
		if (newInfo.m_rearLeftHeightOffset < m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset) {
			// If it's going down, dampen the movement a bit
			m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset += (newInfo.m_rearLeftHeightOffset - m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset)/2.0f;
		}	else {
			m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset = newInfo.m_rearLeftHeightOffset;
		}
		if (newInfo.m_rearRightHeightOffset < m_locoInfo->m_wheelInfo.m_rearRightHeightOffset) {
			// If it's going down, dampen the movement a bit
			m_locoInfo->m_wheelInfo.m_rearRightHeightOffset += (newInfo.m_rearRightHeightOffset - m_locoInfo->m_wheelInfo.m_rearRightHeightOffset)/2.0f;
		}	else {
			m_locoInfo->m_wheelInfo.m_rearRightHeightOffset = newInfo.m_rearRightHeightOffset;
		}
		//m_locoInfo->m_wheelInfo = newInfo;
		if (m_locoInfo->m_wheelInfo.m_frontLeftHeightOffset<MAX_SUSPENSION_EXTENSION) {
			m_locoInfo->m_wheelInfo.m_frontLeftHeightOffset=MAX_SUSPENSION_EXTENSION;
		}
		if (m_locoInfo->m_wheelInfo.m_frontRightHeightOffset<MAX_SUSPENSION_EXTENSION) {
			m_locoInfo->m_wheelInfo.m_frontRightHeightOffset=MAX_SUSPENSION_EXTENSION;
		}
		if (m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset<MAX_SUSPENSION_EXTENSION) {
			m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset=MAX_SUSPENSION_EXTENSION;
		}
		if (m_locoInfo->m_wheelInfo.m_rearRightHeightOffset<MAX_SUSPENSION_EXTENSION) {
			m_locoInfo->m_wheelInfo.m_rearRightHeightOffset=MAX_SUSPENSION_EXTENSION;
		}
/*
		if (m_locoInfo->m_wheelInfo.m_frontLeftHeightOffset>MAX_SUSPENSION_COMPRESSION) {
			m_locoInfo->m_wheelInfo.m_frontLeftHeightOffset=MAX_SUSPENSION_COMPRESSION;
		}
		if (m_locoInfo->m_wheelInfo.m_frontRightHeightOffset>MAX_SUSPENSION_COMPRESSION) {
			m_locoInfo->m_wheelInfo.m_frontRightHeightOffset=MAX_SUSPENSION_COMPRESSION;
		}
		if (m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset>MAX_SUSPENSION_COMPRESSION) {
			m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset=MAX_SUSPENSION_COMPRESSION;
		}
		if (m_locoInfo->m_wheelInfo.m_rearRightHeightOffset>MAX_SUSPENSION_COMPRESSION) {
			m_locoInfo->m_wheelInfo.m_rearRightHeightOffset=MAX_SUSPENSION_COMPRESSION;
		}	
		*/
	}
	// If we are > 22 degrees, need to raise height;
	Real divisor = 4;
	Real pitch = fabs(info.m_totalPitch-groundPitch);

	if (pitch>PI/8) {
		divisor = ((4*PI/8) + (1*(pitch-PI/8)))/pitch;
	}
	info.m_totalZ += fabs(pitchHeight)/divisor;
	info.m_totalZ += fabs(rollHeight)/divisor;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void Drawable::calcPhysicsXformMotorcycle( const Locomotor *locomotor, PhysicsXformInfo& info )
{
	if (m_locoInfo == nullptr)
		m_locoInfo = newInstance(DrawableLocoInfo);

	const Real ACCEL_PITCH_LIMIT = locomotor->getAccelPitchLimit();
	const Real DECEL_PITCH_LIMIT = locomotor->getDecelPitchLimit();
	const Real BOUNCE_ANGLE_KICK = locomotor->getBounceKick();
	const Real PITCH_STIFFNESS = locomotor->getPitchStiffness();
	const Real ROLL_STIFFNESS =  locomotor->getRollStiffness();
	const Real PITCH_DAMPING = locomotor->getPitchDamping();
	const Real ROLL_DAMPING = locomotor->getRollDamping();
	const Real FORWARD_ACCEL_COEFF = locomotor->getForwardAccelCoef();	
	const Real LATERAL_ACCEL_COEFF = locomotor->getLateralAccelCoef();	
	const Real UNIFORM_AXIAL_DAMPING = locomotor->getUniformAxialDamping();	

	const Real MAX_SUSPENSION_EXTENSION = locomotor->getMaxWheelExtension(); //-2.3f;
//	const Real MAX_SUSPENSION_COMPRESSION = locomotor->getMaxWheelCompression(); //1.4f;
	const Real WHEEL_ANGLE = locomotor->getWheelTurnAngle(); //PI/8;

	const Bool DO_WHEELS = locomotor->hasSuspension();

	// get object from logic
	Object *obj = getObject();
	if (obj == nullptr)
		return;

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (ai == nullptr)
		return ;

	// get object physics state
	PhysicsBehavior *physics = obj->getPhysics();
	if (physics == nullptr)
		return ;

	// get our position and direction vector
	const Coord3D *pos = getPosition();
	const Coord3D *dir = getUnitDirectionVector2D();
	const Coord3D *accel = physics->getAcceleration();

	// compute perpendicular (2d)
	Coord3D perp;
	perp.x = -dir->y;
	perp.y = dir->x;
	perp.z = 0.0f;

	// find pitch and roll of terrain under chassis
	Coord3D normal;
	Real hheight = TheTerrainLogic->getLayerHeight( pos->x, pos->y, obj->getLayer(), &normal );

	Real dot = normal.x * dir->x + normal.y * dir->y;
	Real groundPitch = dot * (PI/2.0f);

	dot = normal.x * perp.x + normal.y * perp.y;
	Real groundRoll = dot * (PI/2.0f);

	Bool airborne = obj->isSignificantlyAboveTerrain();

	if (airborne) 
	{
		if (DO_WHEELS) 
		{	
			// Wheels extend when airborne.
			m_locoInfo->m_wheelInfo.m_framesAirborne = 0;
			m_locoInfo->m_wheelInfo.m_framesAirborneCounter++;
			if (pos->z - hheight > -MAX_SUSPENSION_EXTENSION) 
			{
				m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset += (MAX_SUSPENSION_EXTENSION - m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset)/2.0f;
				m_locoInfo->m_wheelInfo.m_rearRightHeightOffset = m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset;
			} 
			else 
			{
				m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset += (0 - m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset)/2.0f;
				m_locoInfo->m_wheelInfo.m_rearRightHeightOffset = m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset;
			}
		}
		// Calculate suspension info.
		Real length = obj->getGeometryInfo().getMajorRadius();
		//Real width = obj->getGeometryInfo().getMinorRadius();
		Real pitchHeight = length*Sin(m_locoInfo->m_pitch + m_locoInfo->m_accelerationPitch - groundPitch);	
		//Real rollHeight = width*Sin(m_locoInfo->m_roll + m_locoInfo->m_accelerationRoll - groundRoll);
		info.m_totalZ = fabs(pitchHeight)/4;// + fabs(rollHeight)/4;
		//return; // maintain the same orientation while we fly through the air.
	}

	// Bouncy.
	Real curSpeed = physics->getVelocityMagnitude();
#if 1 
	Real maxSpeed = ai->getCurLocomotorSpeed();
	if (!airborne && curSpeed > maxSpeed/10) 
	{
		Real factor = curSpeed/maxSpeed;
		if (fabs(m_locoInfo->m_pitchRate)<factor*BOUNCE_ANGLE_KICK/4 && fabs(m_locoInfo->m_rollRate)<factor*BOUNCE_ANGLE_KICK/8) 
		{
			// do the bouncy. 
			switch (GameClientRandomValue(0,3)) 
			{
			case 0:
				m_locoInfo->m_pitchRate -= BOUNCE_ANGLE_KICK*factor;
				m_locoInfo->m_rollRate -= BOUNCE_ANGLE_KICK*factor/2;
				break;
			case 1:
				m_locoInfo->m_pitchRate += BOUNCE_ANGLE_KICK*factor;
				m_locoInfo->m_rollRate -= BOUNCE_ANGLE_KICK*factor/2;
				break;
			case 2:
				m_locoInfo->m_pitchRate -= BOUNCE_ANGLE_KICK*factor;
				m_locoInfo->m_rollRate += BOUNCE_ANGLE_KICK*factor/2;
				break;
			case 3:
				m_locoInfo->m_pitchRate += BOUNCE_ANGLE_KICK*factor;
				m_locoInfo->m_rollRate += BOUNCE_ANGLE_KICK*factor/2;
				break;
			}
		}
	}

#endif

	// process chassis suspension dynamics - damp back towards groundPitch

	// the ground can only push back if we're touching it
	if (!airborne)
	{
		m_locoInfo->m_pitchRate += ((-PITCH_STIFFNESS * (m_locoInfo->m_pitch - groundPitch)) + (-PITCH_DAMPING * m_locoInfo->m_pitchRate));		// spring/damper
		m_locoInfo->m_rollRate += ((-ROLL_STIFFNESS * (m_locoInfo->m_roll - groundRoll)) + (-ROLL_DAMPING * m_locoInfo->m_rollRate));		// spring/damper
	}
	else
	{
		//Autolevel
		m_locoInfo->m_pitchRate += ( (-PITCH_STIFFNESS * m_locoInfo->m_pitch) + (-PITCH_DAMPING * m_locoInfo->m_pitchRate) );		// spring/damper
		m_locoInfo->m_rollRate += ( (-ROLL_STIFFNESS * m_locoInfo->m_roll) + (-ROLL_DAMPING * m_locoInfo->m_rollRate) );		// spring/damper
	}

	m_locoInfo->m_pitch += m_locoInfo->m_pitchRate * UNIFORM_AXIAL_DAMPING;
	m_locoInfo->m_roll += m_locoInfo->m_rollRate   * UNIFORM_AXIAL_DAMPING;

	// process chassis acceleration dynamics - damp back towards zero

	m_locoInfo->m_accelerationPitchRate += ((-PITCH_STIFFNESS * (m_locoInfo->m_accelerationPitch)) + (-PITCH_DAMPING * m_locoInfo->m_accelerationPitchRate));		// spring/damper
	m_locoInfo->m_accelerationPitch += m_locoInfo->m_accelerationPitchRate;

	m_locoInfo->m_accelerationRollRate += ((-ROLL_STIFFNESS * m_locoInfo->m_accelerationRoll) + (-ROLL_DAMPING * m_locoInfo->m_accelerationRollRate));		// spring/damper
	m_locoInfo->m_accelerationRoll += m_locoInfo->m_accelerationRollRate;

	// compute total pitch and roll of tank
	info.m_totalPitch = m_locoInfo->m_pitch + m_locoInfo->m_accelerationPitch;


  // THis logic had recently been added to Drawable::applyPhysicsXform(), which was naughty, since it clamped the roll in every drawable in the game
  // Now only motorcycles enjoy this constraint
  Real unclampedRoll = m_locoInfo->m_roll + m_locoInfo->m_accelerationRoll;
  info.m_totalRoll = (unclampedRoll > 0.5f && unclampedRoll < -0.5f ? unclampedRoll : 0.0f);

	if( airborne )
	{
	}

	if (physics->isMotive()) 
	{
		// cause the chassis to pitch & roll in reaction to acceleration/deceleration
		Real forwardAccel = dir->x * accel->x + dir->y * accel->y;
		m_locoInfo->m_accelerationPitchRate += -(FORWARD_ACCEL_COEFF * forwardAccel);

		Real lateralAccel = -dir->y * accel->x + dir->x * accel->y;
		m_locoInfo->m_accelerationRollRate += -(LATERAL_ACCEL_COEFF * lateralAccel);
	}

	// limit acceleration pitch and roll

	if (m_locoInfo->m_accelerationPitch > DECEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = DECEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationPitch < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = -ACCEL_PITCH_LIMIT;

	if (m_locoInfo->m_accelerationRoll > DECEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = DECEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationRoll < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = -ACCEL_PITCH_LIMIT;

	info.m_totalZ = 0;

	// Calculate suspension info.
	Real length = obj->getGeometryInfo().getMajorRadius();
	Real width = obj->getGeometryInfo().getMinorRadius();
	Real pitchHeight = length*Sin(info.m_totalPitch-groundPitch);
	Real rollHeight = width*Sin(info.m_totalRoll-groundRoll);
	if (DO_WHEELS) 
	{	
		// calculate each wheel position
		m_locoInfo->m_wheelInfo.m_framesAirborne = m_locoInfo->m_wheelInfo.m_framesAirborneCounter;
		m_locoInfo->m_wheelInfo.m_framesAirborneCounter = 0;
		TWheelInfo newInfo = m_locoInfo->m_wheelInfo;
		PhysicsTurningType rotation = physics->getTurning();
		if (rotation == TURN_NEGATIVE) {
			newInfo.m_wheelAngle = -WHEEL_ANGLE;
		} else if (rotation == TURN_POSITIVE) {
			newInfo.m_wheelAngle = WHEEL_ANGLE;
		}	else {
			newInfo.m_wheelAngle = 0;
		}
		if (physics->getForwardSpeed2D() < 0.0f) {
			// if we're moving backwards, the wheels rotate in the opposite direction.
			newInfo.m_wheelAngle = -newInfo.m_wheelAngle;
		}

		//
		///@todo Steven/John ... please review this and make sure it makes sense (CBD)
		// we're going to add the angle to the current wheel rotation ... but we're going to 
		// divide that number to add small angles.  This allows for "smoother" wheel turning
		// transitions ... and when the AI has things move in a straight line, since it's
		// constantly telling the object to go left, go straight, go right, go straight,
		// etc, this smaller angle we'll be adding covers the constant wheel shifting 
		// left and right when moving in a relatively straight line
		//
		#define WHEEL_SMOOTHNESS 10.0f  // higher numbers add smaller angles, make it more "smooth"
		m_locoInfo->m_wheelInfo.m_wheelAngle += (newInfo.m_wheelAngle - m_locoInfo->m_wheelInfo.m_wheelAngle)/WHEEL_SMOOTHNESS;

		const Real SPRING_FACTOR = 0.9f;
		if (pitchHeight<0) 
		{	
			// Front raising up
			newInfo.m_frontLeftHeightOffset		= SPRING_FACTOR*(pitchHeight/3+pitchHeight/2);
			newInfo.m_rearLeftHeightOffset		= -pitchHeight/2 + pitchHeight/4;
			newInfo.m_frontRightHeightOffset	= newInfo.m_frontLeftHeightOffset;
			newInfo.m_rearRightHeightOffset		= newInfo.m_rearLeftHeightOffset;
		}	
		else 
		{	
			// Back raising up.
			newInfo.m_frontLeftHeightOffset		= (-pitchHeight/4+pitchHeight/2);
			newInfo.m_rearLeftHeightOffset		= SPRING_FACTOR*(-pitchHeight/2 + -pitchHeight/3);
			newInfo.m_frontRightHeightOffset	= newInfo.m_frontLeftHeightOffset;
			newInfo.m_rearRightHeightOffset		= newInfo.m_rearLeftHeightOffset;
		}
		/*
		if (rollHeight>0) {	// Right raising up
			newInfo.m_frontRightHeightOffset += -SPRING_FACTOR*(rollHeight/3+rollHeight/2);
			newInfo.m_rearLeftHeightOffset += rollHeight/2 - rollHeight/4;
		}	else {	// Left raising up.
			newInfo.m_frontRightHeightOffset += -rollHeight/2 + rollHeight/4;
			newInfo.m_rearLeftHeightOffset += SPRING_FACTOR*(rollHeight/3+rollHeight/2);
		}
		*/
		if (newInfo.m_frontLeftHeightOffset < m_locoInfo->m_wheelInfo.m_frontLeftHeightOffset) 
		{
			// If it's going down, dampen the movement a bit
			m_locoInfo->m_wheelInfo.m_frontLeftHeightOffset += (newInfo.m_frontLeftHeightOffset - m_locoInfo->m_wheelInfo.m_frontLeftHeightOffset)/2.0f;
			m_locoInfo->m_wheelInfo.m_frontRightHeightOffset = m_locoInfo->m_wheelInfo.m_frontLeftHeightOffset;
		}	
		else 
		{
			m_locoInfo->m_wheelInfo.m_frontLeftHeightOffset = newInfo.m_frontLeftHeightOffset;
			m_locoInfo->m_wheelInfo.m_frontRightHeightOffset = newInfo.m_frontLeftHeightOffset;
		}
		if (newInfo.m_rearLeftHeightOffset < m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset) 
		{
			// If it's going down, dampen the movement a bit
			m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset += (newInfo.m_rearLeftHeightOffset - m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset)/2.0f;
			m_locoInfo->m_wheelInfo.m_rearRightHeightOffset = m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset;
		}	
		else 
		{
			m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset = newInfo.m_rearLeftHeightOffset;
			m_locoInfo->m_wheelInfo.m_rearRightHeightOffset = newInfo.m_rearLeftHeightOffset;
		}
		//m_locoInfo->m_wheelInfo = newInfo;
		if (m_locoInfo->m_wheelInfo.m_frontLeftHeightOffset<MAX_SUSPENSION_EXTENSION) 
		{
			m_locoInfo->m_wheelInfo.m_frontLeftHeightOffset = MAX_SUSPENSION_EXTENSION;
			m_locoInfo->m_wheelInfo.m_frontRightHeightOffset = MAX_SUSPENSION_EXTENSION;
		}
		if (m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset<MAX_SUSPENSION_EXTENSION) 
		{
			m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset = MAX_SUSPENSION_EXTENSION;
			m_locoInfo->m_wheelInfo.m_rearRightHeightOffset = MAX_SUSPENSION_EXTENSION;
		}
	}
	// If we are > 22 degrees, need to raise height;
	Real divisor = 4;
	Real pitch = fabs(info.m_totalPitch-groundPitch);

	if (pitch>PI/8) {
		divisor = ((4*PI/8) + (1*(pitch-PI/8)))/pitch;
	} 
	
	if( !airborne )
	{
		info.m_totalZ += fabs(pitchHeight)/divisor;
		info.m_totalZ += fabs(rollHeight)/divisor;
	}
}
