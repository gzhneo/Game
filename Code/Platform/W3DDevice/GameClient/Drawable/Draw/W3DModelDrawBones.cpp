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

// Turret, recoil and weapon bones, client particle system bones, bone positions

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::doStartOrStopParticleSys()
{
	Bool hidden = getDrawable()->isDrawableEffectivelyHidden() || m_fullyObscuredByShroud;

	for (std::vector<ParticleSysTrackerType>::const_iterator it = m_particleSystemIDs.begin(); it != m_particleSystemIDs.end(); ++it)
	//for (std::vector<ParticleSystemID>::const_iterator it = m_particleSystemIDs.begin(); it != m_particleSystemIDs.end(); ++it)
	{
		ParticleSystem *sys = TheParticleSystemManager->findParticleSystem((*it).id);
		if (sys != nullptr) {
			// this can be NULL
			if (hidden) {
				sys->stop();
			} else {
				sys->start();
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::stopClientParticleSystems() 
{
	for (std::vector<ParticleSysTrackerType>::const_iterator it = m_particleSystemIDs.begin(); it != m_particleSystemIDs.end(); ++it)
	//for (std::vector<ParticleSystemID>::const_iterator it = m_particleSystemIDs.begin(); it != m_particleSystemIDs.end(); ++it)
	{
		ParticleSystem *sys = TheParticleSystemManager->findParticleSystem((*it).id);
		if (sys != nullptr) 
		{
			// this can be NULL
			sys->destroy();
		}
	}
	m_particleSystemIDs.clear();
}

//-------------------------------------------------------------------------------------------------
/*
	DANGER WARNING READ ME
	DANGER WARNING READ ME
	DANGER WARNING READ ME

	This function must not EVER do ANYTHING which can in any way, shape, or form
	affect GameLogic in any way; if it does, net desyncs will occur and we
	will all lose our jobs. This must remain pure-client-only, and ensure
	that nothing it does can be detected, even in read-only form, by GameLogic!

	DANGER WARNING READ ME
	DANGER WARNING READ ME
	DANGER WARNING READ ME
*/
void W3DModelDraw::handleClientTurretPositioning()
{
	if (!m_curState || !(m_curState->m_validStuff & ModelConditionInfo::TURRETS_VALID))
		return;

	for (int tslot = 0; tslot < MAX_TURRETS; ++tslot)
	{
		const ModelConditionInfo::TurretInfo& tur = m_curState->m_turrets[tslot];
		Real turretAngle = 0;
		Real turretPitch = 0;
		if (tur.m_turretAngleBone || tur.m_turretPitchBone)
		{
			const Object *obj = getDrawable()->getObject();
			if (obj)
			{
				const AIUpdateInterface* ai = obj->getAIUpdateInterface();
				if (ai)
					ai->getTurretRotAndPitch(static_cast<WhichTurretType>(tslot), &turretAngle, &turretPitch);
			}

			// do turret, if any
			if (tur.m_turretAngleBone != 0)
			{
				if (m_curState)
					turretAngle += tur.m_turretArtAngle;
				Matrix3D turretXfrm(1);
				turretXfrm.Rotate_Z(turretAngle);
				if (m_renderObject)
				{
					m_renderObject->Capture_Bone( tur.m_turretAngleBone );
					m_renderObject->Control_Bone( tur.m_turretAngleBone, turretXfrm );	
				}
			}

			// do turret pitch, if any
			if (tur.m_turretPitchBone != 0)
			{
				if (m_curState)
					turretPitch += tur.m_turretArtPitch;
				Matrix3D turretPitchXfrm(1);
				turretPitchXfrm.Rotate_Y(-turretPitch);
				if (m_renderObject)
				{
					m_renderObject->Capture_Bone( tur.m_turretPitchBone );
					m_renderObject->Control_Bone( tur.m_turretPitchBone, turretPitchXfrm );	
				}
			}
		}
	} // next tslot
}

//-------------------------------------------------------------------------------------------------
/*
	Note that, strictly speaking this code is WRONG, since it assumes it will get called every frame,
	but in fact, will only get called every frame that it is visible. In practice, this isn't a big problem,
	but...

	@todo fix me someday (srj)
*/
void W3DModelDraw::handleClientRecoil()
{
	const W3DModelDrawModuleData* d = getW3DModelDrawModuleData();
	if (!(m_curState->m_validStuff & ModelConditionInfo::BARRELS_VALID))
	{
		return;
	}

	// do recoil, if any
	for (int wslot = 0; wslot < WEAPONSLOT_COUNT; ++wslot)
	{
		if (!m_curState->m_hasRecoilBonesOrMuzzleFlashes[wslot])
			continue;

		const ModelConditionInfo::WeaponBarrelInfoVec& barrels = m_curState->m_weaponBarrelInfoVec[wslot];
		WeaponRecoilInfoVec& recoils = m_weaponRecoilInfoVec[wslot];
		Int count = static_cast<Int>(barrels.size());
		Int recoilCount = static_cast<Int>(recoils.size());
		DEBUG_ASSERTCRASH(count == recoilCount, ("Barrel count != recoil count!"));
		count = (count>recoilCount)?recoilCount:count;
		for (Int i = 0; i < count; ++i)
		{
			if (barrels[i].m_muzzleFlashBone != 0)
			{
				Bool hidden = recoils[i].m_state != WeaponRecoilInfo::RECOIL_START;
				//DEBUG_LOG(("adjust muzzleflash %08lx for Draw %08lx state %s to %d at frame %d\n",subObjToHide,this,m_curState->m_description.str(),hidden?1:0,TheGameLogic->getFrame()));
				barrels[i].setMuzzleFlashHidden(m_renderObject, hidden);
			}

			const Real TINY_RECOIL = 0.01f;
			if (barrels[i].m_recoilBone != 0)
			{
				// recoil state machine runs only in the tick; the bone is still set every frame
				if (!GameClient_IsRenderOnlyFrame())
				switch (recoils[i].m_state )
				{
					case WeaponRecoilInfo::IDLE:
						// nothing
						break;

					case WeaponRecoilInfo::RECOIL_START:
					case WeaponRecoilInfo::RECOIL:
						recoils[i].m_shift += recoils[i].m_recoilRate;
						recoils[i].m_recoilRate *= d->m_recoilDamping;			// recoil decelerates
						if (recoils[i].m_shift >= d->m_maxRecoil)
						{
							recoils[i].m_shift = d->m_maxRecoil;
							recoils[i].m_state = WeaponRecoilInfo::SETTLE;
						}
						else if (fabs(recoils[i].m_recoilRate) < TINY_RECOIL)
						{
							recoils[i].m_state = WeaponRecoilInfo::SETTLE;
						}
						break;

					case WeaponRecoilInfo::SETTLE:
						recoils[i].m_shift -= d->m_recoilSettle;
						if (recoils[i].m_shift <= 0.0f)
						{
							recoils[i].m_shift = 0.0f;
							recoils[i].m_state = WeaponRecoilInfo::IDLE;
						}
						break;
				}

				Matrix3D gunXfrm;
				gunXfrm.Make_Identity();
				gunXfrm.Translate_X( -recoils[i].m_shift );
				//DEBUG_ASSERTLOG(recoils[i].m_shift==0.0f,("adjust bone %d by %f\n",recoils[i].m_recoilBone,recoils[i].m_shift));

				if (m_renderObject)
				{
					m_renderObject->Capture_Bone( barrels[i].m_recoilBone );
					m_renderObject->Control_Bone( barrels[i].m_recoilBone, gunXfrm );
				}
			}
			else
			{
				recoils[i].m_state = WeaponRecoilInfo::IDLE;
				//DEBUG_LOG(("reset Draw %08lx state %08lx\n",this,m_curState));
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/*
	DANGER WARNING READ ME
	DANGER WARNING READ ME
	DANGER WARNING READ ME

	This function must not EVER do ANYTHING which can in any way, shape, or form
	affect GameLogic in any way; if it does, net desyncs will occur and we
	will all lose our jobs. This must remain pure-client-only, and ensure
	that nothing it does can be detected, even in read-only form, by GameLogic!

	DANGER WARNING READ ME
	DANGER WARNING READ ME
	DANGER WARNING READ ME
*/
void W3DModelDraw::recalcBonesForClientParticleSystems()
{
	if (m_needRecalcBoneParticleSystems)
	{
		const Drawable* drawable = getDrawable();
		if (drawable != nullptr )
		{
		
			if( m_curState != nullptr && drawable->testDrawableStatus( DRAWABLE_STATUS_NO_STATE_PARTICLES ) == FALSE ) 
			{
				for (std::vector<ParticleSysBoneInfo>::const_iterator it = m_curState->m_particleSysBones.begin(); it != m_curState->m_particleSysBones.end(); ++it)
				{
					ParticleSystem *sys = TheParticleSystemManager->createParticleSystem(it->particleSystemTemplate);
					if (sys != nullptr) 
					{
						Coord3D pos;
						pos.zero();
						Real rotation = 0.0f;

						Int boneIndex = m_renderObject ? m_renderObject->Get_Bone_Index(it->boneName.str()) : 0;
						if (boneIndex != 0)
						{
							// ugh... kill the mtx so we get it in modelspace, not world space
							Matrix3D originalTransform = m_renderObject->Get_Transform();	// save the transform
							Matrix3D tmp(true);
							tmp.Scale(getDrawable()->getScale());
							m_renderObject->Set_Transform(tmp);					// set to identity transform

							const Matrix3D boneTransform = m_renderObject->Get_Bone_Transform(boneIndex);
							Vector3 vpos = boneTransform.Get_Translation();
							rotation = boneTransform.Get_Z_Rotation();

							m_renderObject->Set_Transform(originalTransform);					// restore it

							pos.x = vpos.X;
							pos.y = vpos.Y;
							pos.z = vpos.Z;
						}

						// got the bone position...
						sys->setPosition(&pos);

						// and the direction, so that the system is oriented like the bone is
						sys->rotateLocalTransformZ(rotation);

						// Attatch it to the object...
						sys->attachToDrawable(drawable);

						// important: mark it as do-not-save, since we'll just re-create it when we reload.
						sys->setSaveable(FALSE);

						if (drawable->isDrawableEffectivelyHidden() || m_fullyObscuredByShroud) 
						{
							sys->stop(); // don't start the systems for drawables that are hidden.
						}

						// store the particle system id so we can kill it later.

						ParticleSysTrackerType tracker;
						tracker.id = sys->getSystemID();
						tracker.boneIndex = boneIndex;

						m_particleSystemIDs.push_back( tracker );
					}
				}
			}
		}
		m_needRecalcBoneParticleSystems = false;
	}
}

//-------------------------------------------------------------------------------------------------
/*
	DANGER WARNING READ ME
	DANGER WARNING READ ME
	DANGER WARNING READ ME

	This function must not EVER do ANYTHING which can in any way, shape, or form
	affect GameLogic in any way; if it does, net desyncs will occur and we
	will all lose our jobs. This must remain pure-client-only, and ensure
	that nothing it does can be detected, even in read-only form, by GameLogic!

	DANGER WARNING READ ME
	DANGER WARNING READ ME
	DANGER WARNING READ ME
*/
Bool W3DModelDraw::updateBonesForClientParticleSystems()
{
	const Drawable* drawable = getDrawable();
	if (drawable != nullptr && m_curState != nullptr && m_renderObject != nullptr ) 
	{

//		Matrix3D originalTransform = m_renderObject->Get_Transform();
//		Matrix3D tmp = originalTransform;
 //   Vector3 zeroTranslation(0,0,0);
  //  tmp.Set_Translation( zeroTranslation );
	//	tmp.Scale(drawable->getScale());
//		m_renderObject->Set_Transform(tmp);					
				
    

		for (std::vector<ParticleSysTrackerType>::const_iterator it = m_particleSystemIDs.begin(); it != m_particleSystemIDs.end(); ++it)
		{
			ParticleSystem *sys = TheParticleSystemManager->findParticleSystem((*it).id);
			Int boneIndex = (*it).boneIndex;
			if ( (sys != nullptr) && (boneIndex != 0)  ) 
			{
    		const Matrix3D boneTransform = m_renderObject->Get_Bone_Transform(boneIndex);// just a little worried about state changes
        
        Vector3 vpos = boneTransform.Get_Translation();

        Coord3D pos;
				pos.x = vpos.X;
				pos.y = vpos.Y;
				pos.z = vpos.Z;

				sys->setPosition(&pos);

        Real orientation = boneTransform.Get_Z_Rotation();
				sys->rotateLocalTransformZ(orientation);
        
        sys->setLocalTransform(&boneTransform);
        sys->setSkipParentXfrm(true);

			}
		}// next praticle system


//  	m_renderObject->Set_Transform(originalTransform);	

	}// end if Drawable

	return TRUE;

}

//-------------------------------------------------------------------------------------------------
Bool W3DModelDraw::getProjectileLaunchOffset(
	const ModelConditionFlags& condition, 
	WeaponSlotType wslot, 
	Int specificBarrelToUse, 
	Matrix3D* launchPos, 
	WhichTurretType tur, 
	Coord3D* turretRotPos, 
	Coord3D* turretPitchPos
) const
{
	/*
		Note, this recalcs the state every time, rather than using m_curState,
		because m_curState could be a transition state, and we really need
		to get the pristine bone(s) for the state that logic believes to be current,
		not the one the client might currently be using...
	*/
	const ModelConditionInfo* stateToUse = findBestInfo(condition);
	if (!stateToUse)
	{
		CRCDEBUG_LOG(("can't find best info\n"));
		//BONEPOS_LOG(("can't find best info\n"));
		return false;
	}
#if defined(_DEBUG)
	CRCDEBUG_LOG(("W3DModelDraw::getProjectileLaunchOffset() for %s\n",
		stateToUse->getDescription().str()));
#endif

#ifdef INTENSE_DEBUG
	AsciiString flags;
	for (Int i=0; i<condition.size(); ++i)
	{
		if (condition.test(i))
		{
			if (flags.isNotEmpty())
				flags.concat(',');
			flags.concat(condition.getNameFromSingleBit(i));
		}
	}
#endif // INTENSE_DEBUG

	const W3DModelDrawModuleData* d = getW3DModelDrawModuleData();
	//CRCDEBUG_LOG(("validateStuffs() from within W3DModelDraw::getProjectileLaunchOffset()\n"));
	//DUMPREAL(getDrawable()->getScale());
	//BONEPOS_LOG(("validateStuffs() from within W3DModelDraw::getProjectileLaunchOffset()\n"));
	//BONEPOS_DUMPREAL(getDrawable()->getScale());
	stateToUse->validateStuff(nullptr, getDrawable()->getScale(), d->m_extraPublicBones);

	DEBUG_ASSERTCRASH(stateToUse->m_transitionSig == NO_TRANSITION, 
		("It is never legal to getProjectileLaunchOffset from a Transition state (they vary on a per-client basis)... however, we can fix this (see srj)\n"));

	DEBUG_ASSERTCRASH(specificBarrelToUse >= 0, ("specificBarrelToUse should now always be explicit"));

#ifdef CACHE_ATTACH_BONE
#else
	Coord3D techOffset;
	techOffset.zero();
	// must use pristine bone here since the result is used by logic
	Matrix3D pivot;
	if (d->m_attachToDrawableBone.isNotEmpty() &&
			getDrawable()->getPristineBonePositions( d->m_attachToDrawableBone.str(), 0, nullptr, &pivot, 1 ) == 1)
	{
		pivot.Pre_Rotate_Z(getDrawable()->getOrientation());
		techOffset.x = pivot.Get_X_Translation();
		techOffset.y = pivot.Get_Y_Translation();
		techOffset.z = pivot.Get_Z_Translation();
	}
#endif

	CRCDEBUG_LOG(("wslot = %d\n", wslot));
	const ModelConditionInfo::WeaponBarrelInfoVec& wbvec = stateToUse->m_weaponBarrelInfoVec[wslot];	
	if( wbvec.empty() )
	{
		// Can't find the launch pos, but they might still want the other info they asked for
		CRCDEBUG_LOG(("empty wbvec\n"));
		//BONEPOS_LOG(("empty wbvec\n"));
		launchPos = nullptr;
	}
	else
	{
		if (specificBarrelToUse < 0 || specificBarrelToUse >= wbvec.size())
			specificBarrelToUse = 0;

		if (launchPos)
		{
			CRCDEBUG_LOG(("specificBarrelToUse = %d\n", specificBarrelToUse));
			*launchPos = wbvec[specificBarrelToUse].m_projectileOffsetMtx;

			if (tur != TURRET_INVALID)
			{
				launchPos->Pre_Rotate_Z(stateToUse->m_turrets[tur].m_turretArtAngle);
				launchPos->Pre_Rotate_Y(-stateToUse->m_turrets[tur].m_turretArtPitch);
			}
#ifdef CACHE_ATTACH_BONE
#else
			launchPos->Adjust_Translation( Vector3( techOffset.x, techOffset.y, techOffset.z ) );
#endif

			DUMPMATRIX3D(launchPos);
		}
	}

	if (turretRotPos) 
		turretRotPos->zero();
	if (turretPitchPos) 
		turretPitchPos->zero();

	if (tur != TURRET_INVALID)
	{
#ifdef CACHE_ATTACH_BONE
		const Vector3* offset = d->getAttachToDrawableBoneOffset(getDrawable());
#endif
		const ModelConditionInfo::TurretInfo& turInfo = stateToUse->m_turrets[tur]; 
		if (turretRotPos)
		{
			if (turInfo.m_turretAngleNameKey != NAMEKEY_INVALID &&
					!stateToUse->findPristineBonePos(turInfo.m_turretAngleNameKey, *turretRotPos))
			{
				assetErrorLog("*** ASSET ERROR: TurretBone %s not found!", KEYNAME(turInfo.m_turretAngleNameKey).str());
			}
#ifdef CACHE_ATTACH_BONE
			if (offset)
			{
				turretRotPos->x += offset->X;
				turretRotPos->y += offset->Y;
				turretRotPos->z += offset->Z;
			}
#endif
		}
		if (turretPitchPos)
		{
			if (turInfo.m_turretPitchNameKey != NAMEKEY_INVALID &&
					!stateToUse->findPristineBonePos(turInfo.m_turretPitchNameKey, *turretPitchPos))
			{
				assetErrorLog("*** ASSET ERROR: TurretBone %s not found!", KEYNAME(turInfo.m_turretPitchNameKey).str());
			}
#ifdef CACHE_ATTACH_BONE
			if (offset)
			{
				turretPitchPos->x += offset->X;
				turretPitchPos->y += offset->Y;
				turretPitchPos->z += offset->Z;
			}
#endif
		}
	}
	return launchPos != nullptr;// return if LaunchPos is valid or not
}

//-------------------------------------------------------------------------------------------------
Int W3DModelDraw::getPristineBonePositionsForConditionState(
	const ModelConditionFlags& condition,
	const char* boneNamePrefix, 
	Int startIndex, 
	Coord3D* positions, 
	Matrix3D* transforms, 
	Int maxBones
) const
{
	/*
		Note, this recalcs the state every time, rather than using m_curState,
		because m_curState could be a transition state, and we really need
		to get the pristine bone(s) for the state that logic believes to be current,
		not the one the client might currently be using...
	*/
	const ModelConditionInfo* stateToUse = findBestInfo(condition);
	if (!stateToUse)
		return 0;

//	if (isValidTimeToCalcLogicStuff())
//	{
//		CRCDEBUG_LOG(("W3DModelDraw::getPristineBonePositionsForConditionState() - state = '%s'\n",
//			stateToUse->getDescription().str()));
//		//CRCDEBUG_LOG(("renderObject == NULL: %d\n", (stateToUse==m_curState)?(m_renderObject == NULL):1));
//	}

	//BONEPOS_LOG(("validateStuff() from within W3DModelDraw::getPristineBonePositionsForConditionState()\n"));
	//BONEPOS_DUMPREAL(getDrawable()->getScale());
	// we must call this every time we set m_nextState, to ensure cached bones are happy
	stateToUse->validateStuff(
		// if the state is the current state, pass in the current render object
		// so that we don't have to re-create it!
		stateToUse == m_curState ? m_renderObject : nullptr, 
		getDrawable()->getScale(),
		getW3DModelDrawModuleData()->m_extraPublicBones);

	const int MAX_BONE_GET = 64;
	Matrix3D tmpMtx[MAX_BONE_GET];

	if (maxBones > MAX_BONE_GET)
		maxBones = MAX_BONE_GET;

	if (transforms == nullptr)
		transforms = tmpMtx;

	Int posCount = 0;
	Int endIndex = (startIndex == 0) ? 0 : 99;
	char buffer[256];
	Int i;
	for (i = startIndex; i <= endIndex; ++i)
	{
		if (i == 0)
			safe_strcpy(buffer, boneNamePrefix);
		else
			WWFormat::Into(buffer, "{}{:02d}", boneNamePrefix, i);
		
		for (char *c = buffer; c && *c; ++c)
		{
			// convert to all-lowercase since that's how we filled in the map
			*c = static_cast<char>(tolower(*c));
		}

		const Matrix3D* mtx = stateToUse->findPristineBone(NAMEKEY(buffer), nullptr);
		if (mtx)
		{
			transforms[posCount] = *mtx;
//			if (isValidTimeToCalcLogicStuff())
//			{
//				DUMPMATRIX3D(mtx);
//			}
		}
		else
		{
			//DEBUG_CRASH(("*** ASSET ERROR: Bone %s not found!\n",buffer));
			const Object *obj = getDrawable()->getObject();
			if (obj)
				transforms[posCount] = *obj->getTransformMatrix();
			else
				transforms[posCount].Make_Identity();
			break;
		}

		++posCount;
		if (posCount >= maxBones)
			break;
	}
	
	if (positions && transforms)
	{
		for (i = 0; i < posCount; ++i)
		{
			Vector3 pos = transforms[i].Get_Translation();
			positions[i].x = pos.X;
			positions[i].y = pos.Y;
			positions[i].z = pos.Z;
//			if (isValidTimeToCalcLogicStuff())
//			{
//				DUMPCOORD3D(&(positions[i]));
//			}
		}
	}

//	if (isValidTimeToCalcLogicStuff())
//	{
//		CRCDEBUG_LOG(("end of W3DModelDraw::getPristineBonePositionsForConditionState()\n"));
//	}

	return posCount;
}

//-------------------------------------------------------------------------------------------------
// (gth) C&C3 Added this accessor for a bone transform in the render object
// this method must ONLY be called from the client, NEVER From the logic, not even indirectly.
Bool W3DModelDraw::clientOnly_getRenderObjBoneTransform(const AsciiString & boneName,Matrix3D * set_tm) const
{
	if (!m_renderObject) {
		return false;
	}

	int idx = m_renderObject->Get_Bone_Index(boneName.str());
	if (idx == 0) {
		set_tm->Make_Identity();
		return false;
	} else {
		*set_tm = m_renderObject->Get_Bone_Transform(idx);
		return true;
	}
}

//-------------------------------------------------------------------------------------------------
Bool W3DModelDraw::getCurrentWorldspaceClientBonePositions(const char* boneName, Matrix3D& transform) const
{
	if (!m_renderObject)
		return false;

	Int boneIndex = m_renderObject->Get_Bone_Index(boneName);
	if (boneIndex == 0)
		return false;

	transform = m_renderObject->Get_Bone_Transform(boneIndex);
	return true;
}

//-------------------------------------------------------------------------------------------------
Int W3DModelDraw::getCurrentBonePositions(
	const char* boneNamePrefix, 
	Int startIndex, 
	Coord3D* positions, 
	Matrix3D* transforms, 
	Int maxBones
) const
{
	const int MAX_BONE_GET = 64;
	Matrix3D tmpMtx[MAX_BONE_GET];

	if (maxBones > MAX_BONE_GET)
		maxBones = MAX_BONE_GET;

	if (transforms == nullptr)
		transforms = tmpMtx;

	if( !m_renderObject )
		return 0;

	// ugh... kill the mtx so we get it in modelspace, not world space
	Matrix3D originalTransform = m_renderObject->Get_Transform();	// save the transform
// which is faster: slamming the xform (and invalidating the objects xforms)
// or inverting it ourself? gotta profile to see. (srj)
#define DO_INV
#ifdef DO_INV
	Matrix3D inverse;
	originalTransform.Get_Orthogonal_Inverse(inverse);
	inverse.Scale(getDrawable()->getScale());
#else
	Matrix3D tmp(true);
	tmp.Scale(getDrawable()->getScale());
	m_renderObject->Set_Transform(tmp);					// set to identity transform
#endif

	Int posCount = 0;
	Int endIndex = (startIndex == 0) ? 0 : 99;
	char buffer[256];
	Int i;
	for (i = startIndex; i <= endIndex; ++i)
	{
		if (i == 0)
			safe_strcpy(buffer, boneNamePrefix);
		else
			WWFormat::Into(buffer, "{}{:02d}", boneNamePrefix, i);
		
		Int boneIndex = m_renderObject->Get_Bone_Index(buffer);
		if (boneIndex == 0)
			break;

		transforms[posCount] = m_renderObject->Get_Bone_Transform(boneIndex);
#ifdef DO_INV
		transforms[posCount].preMul(inverse);
#endif
		//DEBUG_ASSERTCRASH(!m_renderObject->Is_Bone_Captured(boneIndex), ("bone is captured!"));

		++posCount;
		if (posCount >= maxBones)
			break;
	}
	
	if (positions && transforms)
	{
		for (i = 0; i < posCount; ++i)
		{
			Vector3 pos = transforms[i].Get_Translation();
			positions[i].x = pos.X;
			positions[i].y = pos.Y;
			positions[i].z = pos.Z;
		}
	}

#ifdef DO_INV
#else
	m_renderObject->Set_Transform(originalTransform);					// restore it
#endif

	return posCount;
}

//-------------------------------------------------------------------------------------------------
Int W3DModelDraw::getBarrelCount(WeaponSlotType wslot) const
{
	return (m_curState && (m_curState->m_validStuff & ModelConditionInfo::BARRELS_VALID)) ?
		static_cast<Int>(m_curState->m_weaponBarrelInfoVec[wslot].size()) : 0;
}

//-------------------------------------------------------------------------------------------------
Bool W3DModelDraw::handleWeaponFireFX(WeaponSlotType wslot, Int specificBarrelToUse, const FXList* fxl, Real weaponSpeed, const Coord3D* victimPos, Real damageRadius)
{
	DEBUG_ASSERTCRASH(specificBarrelToUse >= 0, ("specificBarrelToUse should now always be explicit"));

	if (!m_curState || !(m_curState->m_validStuff & ModelConditionInfo::BARRELS_VALID))
		return false;

	const ModelConditionInfo::WeaponBarrelInfoVec& wbvec = m_curState->m_weaponBarrelInfoVec[wslot];
	if (wbvec.empty())
	{
		// don't do this... some other module of our drawable may have handled it.
		// just return false and let the caller sort it out.
		// FXList::doFXPos(fxl, getDrawable()->getPosition(), getDrawable()->getTransformMatrix(), weaponSpeed, victimPos);
		return false;
	}
	
	Bool handled = false;

	if (specificBarrelToUse < 0 || specificBarrelToUse > wbvec.size())
		specificBarrelToUse = 0;

	const ModelConditionInfo::WeaponBarrelInfo& info = wbvec[specificBarrelToUse];

	if (fxl)
	{
		if (info.m_fxBone && m_renderObject)
		{ 
			const Object *logicObject = getDrawable()->getObject();// This is slow, so store it
			if( ! m_renderObject->Is_Hidden() || (logicObject == nullptr) )
			{
				// I can ask the drawable's bone position if I am not hidden (if I have no object I have no choice)
				Matrix3D mtx = m_renderObject->Get_Bone_Transform(info.m_fxBone);
				Coord3D pos;
				pos.x = mtx.Get_X_Translation();
				pos.y = mtx.Get_Y_Translation();
				pos.z = mtx.Get_Z_Translation();
				FXList::doFXPos(fxl, &pos, &mtx, weaponSpeed, victimPos, damageRadius);
			}
			else
			{
				// Else, I should just use my logic position for the effect placement.
				// Things in transports regularly fire from inside (hidden), so this is not weird.
				const Matrix3D *mtx;
				Coord3D pos;
				mtx = logicObject->getTransformMatrix();
				pos = *(logicObject->getPosition());
				/** @todo Once Firepoint bones are actually implemented, this matrix will become correct.
				// Unless of course they decide to not have the tracers come out of the windows, but rather go towards the target.
				// In that case, tracers will have to be rewritten to be "point towards secondary" instead of 
				// "point straight ahead" which assumes we are facing the target.
				*/
				FXList::doFXPos(fxl, &pos, mtx, weaponSpeed, victimPos, damageRadius);
			}

			handled = true;
		}
		else
		{
			DEBUG_LOG(("*** no FXBone found for a non-null FXL\n"));
		}
	}

	if (info.m_recoilBone || info.m_muzzleFlashBone)
	{
		//DEBUG_LOG(("START muzzleflash %08lx for Draw %08lx state %s at frame %d\n",info.m_muzzleFlashBone,this,m_curState->m_description.str(),TheGameLogic->getFrame()));
		WeaponRecoilInfo& recoil = m_weaponRecoilInfoVec[wslot][specificBarrelToUse];
		recoil.m_state = WeaponRecoilInfo::RECOIL_START;
		recoil.m_recoilRate = getW3DModelDrawModuleData()->m_initialRecoil;
		if (info.m_muzzleFlashBone != 0)
			info.setMuzzleFlashHidden(m_renderObject, false);
	}

	return handled;
} 

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::rebuildWeaponRecoilInfo(const ModelConditionInfo* state) 
{
	Int wslot;

	if (state == nullptr)
	{
		for (wslot = 0; wslot < WEAPONSLOT_COUNT; ++wslot)
		{
			m_weaponRecoilInfoVec[wslot].clear();
		}
	}
	else
	{
		for (wslot = 0; wslot < WEAPONSLOT_COUNT; ++wslot)
		{
			Int ncount = static_cast<Int>(state->m_weaponBarrelInfoVec[wslot].size());
			if (m_weaponRecoilInfoVec[wslot].size() != ncount)
			{
				WeaponRecoilInfo tmp;
				m_weaponRecoilInfoVec[wslot].resize(ncount, tmp);
			}

			for (WeaponRecoilInfoVec::iterator it = m_weaponRecoilInfoVec[wslot].begin(); it != m_weaponRecoilInfoVec[wslot].end(); ++it)
			{
				it->clear();
			}
		}
	}
} 
