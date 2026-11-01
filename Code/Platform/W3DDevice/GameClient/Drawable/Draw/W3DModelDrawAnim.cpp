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

#define NO_DEBUG_CRC

#include "Common/crc.h"
#include "formatinto.h"
#include "Common/CRCDebug.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/RandomValue.h"
#include "Common/ThingTemplate.h"
#include "Common/GameLOD.h"
#include "Common/Xfer.h"
#include "Common/GameState.h"
#include "Common/QuickTrig.h"
#include "GameClient/Drawable.h"
#include "GameClient/FXList.h"
#include "GameClient/Shadow.h"
#include "GameLogic/GameLogic.h"		// for real-time frame
#include "GameLogic/Object.h"
#include "GameLogic/WeaponSet.h"
#include "GameLogic/FPUControl.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/W3DTerrainTracks.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "WW3D2/Animation/hanim.h"
#include "WW3D2/Mesh/hlod.h"
#include "WW3D2/Mesh/rendobj.h"
#include "WW3D2/Mesh/mesh.h"
#include "WW3D2/Mesh/meshmdl.h"
#include "Common/BitFlagsIO.h"
#include "GameClient/GameClient.h"	// isRenderOnlyFrame()
#include "Lib/SafeString.h"
#include "W3DModelDrawInternal.h"

// Animation timing, frames and transitions

static const Int ALL_MAINTAIN_FRAME_FLAGS =
	(1<<MAINTAIN_FRAME_ACROSS_STATES) |
	(1<<MAINTAIN_FRAME_ACROSS_STATES2) |
	(1<<MAINTAIN_FRAME_ACROSS_STATES3) |
	(1<<MAINTAIN_FRAME_ACROSS_STATES4);

inline Bool isAnyMaintainFrameFlagSet(Int flags)
{
	return (flags & ALL_MAINTAIN_FRAME_FLAGS) != 0;
}

inline Bool isCommonMaintainFrameFlagSet(Int a, Int b)
{
	a &= ALL_MAINTAIN_FRAME_FLAGS;
	b &= ALL_MAINTAIN_FRAME_FLAGS;
	return (a & b) != 0;
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::adjustAnimSpeedToMovementSpeed()
{
	// if the cur animation has a "distance covered" number, try to throttle
	// the animation rate to match our actual speed.
	Real dist = getCurAnimDistanceCovered();
	if (dist > 0.0f)
	{
		const Object *obj = getDrawable()->getObject();
		if (obj)
		{
			const PhysicsBehavior* physics = obj->getPhysics();
			if (physics)
			{
				Real speed = physics->getVelocityMagnitude();	// in dist/frame
				if (speed > 0.0f)
				{
					// if distance-covered is specified for this anim, adjust the frame rate
					// so it appears to sync with our unit speed
					Real desiredDurationInMsec = dist / speed * MSEC_PER_LOGICFRAME_REAL;
					setCurAnimDurationInMsec(desiredDurationInMsec);
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
const ModelConditionInfo* W3DModelDraw::findTransitionForSig(TransitionSig sig) const
{
	const TransitionMap& transitionMap = getW3DModelDrawModuleData()->m_transitionMap;
	TransitionMap::const_iterator it = transitionMap.find(sig);
	if (it != transitionMap.end())
	{
		return &(*it).second;
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
Real W3DModelDraw::getCurrentAnimFraction() const
{
	if (m_curState != nullptr
			&& isAnyMaintainFrameFlagSet(m_curState->m_flags)
			&& m_renderObject != nullptr
			&& m_renderObject->Class_ID() == RenderObjClass::CLASSID_HLOD)
	{
		float framenum, dummy;
		int mode, numFrames;

		HLodClass* hlod = static_cast<HLodClass*>(m_renderObject);
		/*HAnimClass* anim =*/ hlod->Peek_Animation_And_Info(framenum, numFrames, mode, dummy);
		if (framenum < 0.0)
			return 0.0;
		else if (framenum >= numFrames)
			return 1.0;
		else
			return static_cast<Real>(framenum) / (static_cast<Real>(numFrames) - 1);
	}
	else
	{
		return -1.0;
	}
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::adjustAnimation(const ModelConditionInfo* prevState, Real prevAnimFraction)
{
	if (!m_curState)
		return;

	// if the current state has m_animations associated, do the right thing
	Int numAnims = static_cast<Int>(m_curState->m_animations.size());
	if (numAnims > 0)
	{
		if (numAnims == 1)
		{
			m_whichAnimInCurState = 0;
		}
		else if (prevState == m_curState)
		{
			// select an idle anim different than the currently playing one
			Int animToAvoid = m_whichAnimInCurState;
			while (m_whichAnimInCurState == animToAvoid)
				m_whichAnimInCurState = GameClientRandomValue(0, numAnims-1);
		}
		else
		{
			m_whichAnimInCurState = GameClientRandomValue(0, numAnims-1);
		}
		
		const W3DAnimationInfo& animInfo = m_curState->m_animations[m_whichAnimInCurState];
		
		HAnimClass* animHandle = animInfo.getAnimHandle();	// note that this now returns an ADDREFED handle, which must be released by the caller!
		if (animHandle && !m_renderObject)
		{
			// Without a render object the AddRef'ed handle must still be released, otherwise the animation
			// stays in the HAnimManager until program exit.
			REF_PTR_RELEASE(animHandle);
		}
		if (m_renderObject && animHandle)
		{
			Int startFrame = 0;
			if (m_curState->m_mode == RenderObjClass::ANIM_MODE_ONCE_BACKWARDS ||
					m_curState->m_mode == RenderObjClass::ANIM_MODE_LOOP_BACKWARDS)
			{
				startFrame = animHandle->Get_Num_Frames()-1;
			}

			if (testFlagBit(m_curState->m_flags, RANDOMIZE_START_FRAME))
			{
				startFrame = GameClientRandomValue(0, animHandle->Get_Num_Frames()-1);
			}
			else if (testFlagBit(m_curState->m_flags, START_FRAME_FIRST))
			{
				startFrame = 0;
			}
			else if (testFlagBit(m_curState->m_flags, START_FRAME_LAST))
			{
				startFrame = animHandle->Get_Num_Frames()-1;
			}
			// order is important here: MAINTAIN_FRAME_ACROSS_STATES is overridden by the other bits, above.
			else if (isAnyMaintainFrameFlagSet(m_curState->m_flags) &&
					prevState &&
					prevState != m_curState &&
					isAnyMaintainFrameFlagSet(prevState->m_flags) &&
					isCommonMaintainFrameFlagSet(m_curState->m_flags, prevState->m_flags) &&
					prevAnimFraction >= 0.0)
			{
				startFrame = REAL_TO_INT(prevAnimFraction * animHandle->Get_Num_Frames()-1);
			}

			m_renderObject->Set_Animation(animHandle, static_cast<float>(startFrame), m_curState->m_mode);
			REF_PTR_RELEASE(animHandle);
			animHandle = nullptr;

			if (m_renderObject->Class_ID() == RenderObjClass::CLASSID_HLOD)
			{
				HLodClass *hlod = static_cast<HLodClass*>(m_renderObject);
				Real factor = GameClientRandomValueReal( m_curState->m_animMinSpeedFactor, m_curState->m_animMaxSpeedFactor );
				hlod->Set_Animation_Frame_Rate_Multiplier( factor );
			}
		}

	}
	else
	{
		m_whichAnimInCurState = -1;
	}

}

//-------------------------------------------------------------------------------------------------
Bool W3DModelDraw::setCurAnimDurationInMsec(Real desiredDurationInMsec)
{
	if (m_renderObject && m_renderObject->Class_ID() == RenderObjClass::CLASSID_HLOD)
	{
		HLodClass* hlod = static_cast<HLodClass*>(m_renderObject);
		HAnimClass* anim = hlod->Peek_Animation();
		if (anim)
		{
			Real naturalDurationInMsec = anim->Get_Num_Frames() * 1000.0f / anim->Get_Frame_Rate();
			if (naturalDurationInMsec > 0.0f && desiredDurationInMsec > 0.0f)
			{
				Real multiplier = naturalDurationInMsec / desiredDurationInMsec;
				hlod->Set_Animation_Frame_Rate_Multiplier(multiplier);
				return true;
			}
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
Real W3DModelDraw::getCurAnimDistanceCovered() const
{
	if (m_curState != nullptr && m_whichAnimInCurState >= 0)
	{
		const W3DAnimationInfo& animInfo = m_curState->m_animations[m_whichAnimInCurState];
	#if defined(_DEBUG)
		if (TheSkateDistOverride != 0.0f)
			return TheSkateDistOverride;
	#endif
		return animInfo.getDistanceCovered();
	}
	return 0.0f;
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setAnimationLoopDuration(UnsignedInt numFrames)
{
// this is never defined -- srj
#ifdef NO_DURATIONS_ON_TRANSITIONS
	if (m_curState != nullptr && m_curState->m_transition != NO_TRANSITION && 
			m_nextState != nullptr && m_nextState->m_transition == NO_TRANSITION)
	{
		DEBUG_LOG(("deferring pending duration of %d frames\n",numFrames));
		m_nextStateAnimLoopDuration = numFrames;
		return;
	}

	m_nextStateAnimLoopDuration = NO_NEXT_DURATION;
	DEBUG_ASSERTCRASH(m_curState != nullptr && m_curState->m_transition == NO_TRANSITION, ("Hmm, setAnimationLoopDuration on a transition state is probably not right... see srj"));
#else
	m_nextStateAnimLoopDuration = NO_NEXT_DURATION;
#endif
	Real desiredDurationInMsec = ceilf(numFrames * MSEC_PER_LOGICFRAME_REAL);
	setCurAnimDurationInMsec(desiredDurationInMsec);
}

//-------------------------------------------------------------------------------------------------
/**
	similar to the above, but assumes that the current state is a "ONCE",
	and is smart about transition states... if there is a transition state 
	"inbetween", it is included in the completion time.
*/
void W3DModelDraw::setAnimationCompletionTime(UnsignedInt numFrames)
{
	if (m_curState != nullptr && m_curState->m_transitionSig != NO_TRANSITION && m_curState->m_animations.size() > 0 &&
			m_nextState != nullptr && m_nextState->m_transitionSig == NO_TRANSITION && m_nextState->m_animations.size() > 0)
	{
		// we have a transition; split up the time suitably.
		// note that this is just a guess, and assumes that the states
		// have only one anim each (or, if multiple, that they are
		// all pretty close in natural duration)
		const Real t1 = m_curState->m_animations.front().getNaturalDurationInMsec();
		const Real t2 = m_nextState->m_animations.front().getNaturalDurationInMsec();
		UnsignedInt transTime = REAL_TO_INT_FLOOR((numFrames * t1) / (t1 + t2));
		setAnimationLoopDuration(transTime);
		m_nextStateAnimLoopDuration = numFrames - transTime;
	}
	else
	{
		setAnimationLoopDuration(numFrames);
	}
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setAnimationFrame( int frame )
{
	if( m_renderObject && m_whichAnimInCurState >= 0 )
	{
		const W3DAnimationInfo& animInfo = m_curState->m_animations[ m_whichAnimInCurState ];
		HAnimClass* animHandle = animInfo.getAnimHandle();	// note that this now returns an ADDREFED handle, which must be released by the caller!
		m_renderObject->Set_Animation( animHandle, static_cast<float>(frame) );
		REF_PTR_RELEASE(animHandle);
	}
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setPauseAnimation(Bool pauseAnim)
{
	if (m_pauseAnimation == pauseAnim) 
	{
		return;
	}

	m_pauseAnimation = pauseAnim;

	if (m_renderObject && m_renderObject->Class_ID() == RenderObjClass::CLASSID_HLOD)
	{
		float framenum, dummy;
		int mode, numFrames;

		HLodClass* hlod = static_cast<HLodClass*>(m_renderObject);
		HAnimClass* anim = hlod->Peek_Animation_And_Info(framenum, numFrames, mode, dummy);
		if (anim)
		{
			if (m_pauseAnimation) 
			{
				m_animationMode = mode;
				hlod->Set_Animation(anim, framenum, RenderObjClass::ANIM_MODE_MANUAL);
			} 
			else 
			{
				hlod->Set_Animation(anim, framenum, m_animationMode);
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
#ifdef ALLOW_ANIM_INQUIRIES
// srj sez: not sure if this is a good idea, for net sync reasons...
Real W3DModelDraw::getAnimationScrubScalar( void ) const
{
	return getCurAnimDistanceCovered();
}
#endif
