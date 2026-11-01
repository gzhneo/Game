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

// Model condition states: assets, bone, turret and weapon barrel caches

//-------------------------------------------------------------------------------------------------
void ModelConditionInfo::preloadAssets( TimeOfDay timeOfDay, Real scale )
{
	// load this asset
	if( m_modelName.isEmpty() == FALSE )
	{
		TheDisplay->preloadModelAssets( m_modelName );
	}
	
	// this can be called from the client, which is problematic
//	validateStuff(NULL, getDrawable()->getScale());
	//validateCachedBones(NULL, scale);
	//validateTurretInfo();
	//validateWeaponBarrelInfo();
}

//-------------------------------------------------------------------------------------------------
void ModelConditionInfo::addPublicBone(const AsciiString& boneName) const
{
	if (boneName.isEmpty() || boneName.isNone())
		return;

	AsciiString tmp = boneName;
	tmp.toLower();
	if (std::find(m_publicBones.begin(), m_publicBones.end(), tmp) == m_publicBones.end())
	{
		m_publicBones.push_back(tmp);
	}
}

//-------------------------------------------------------------------------------------------------
Bool ModelConditionInfo::matchesMode(Bool night, Bool snowy) const
{
	for (std::vector<ModelConditionFlags>::const_iterator it = m_conditionsYesVec.begin(); 
				it != m_conditionsYesVec.end(); 
				++it)
	{
		if (it->test(MODELCONDITION_NIGHT) == (night) &&
				it->test(MODELCONDITION_SNOW) == (snowy))
		{
			return true;
		}
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
static Bool findSingleBone(RenderObjClass* robj, const AsciiString& boneName, Matrix3D& mtx, Int& boneIndex)
{
	if (boneName.isNone() || boneName.isEmpty())
		return false;

	boneIndex = robj->Get_Bone_Index(boneName.str());
	if (boneIndex != 0)
	{
		mtx = robj->Get_Bone_Transform(boneIndex);
		return true;
	}
	else
	{
		return false;
	}
}

//-------------------------------------------------------------------------------------------------
static Bool findSingleSubObj(RenderObjClass* robj, const AsciiString& boneName, Matrix3D& mtx, Int& boneIndex)
{
	if (boneName.isNone() || boneName.isEmpty())
		return false;

	RenderObjClass* childObject = robj->Get_Sub_Object_By_Name(boneName.str());
	if (childObject)
	{
		mtx = childObject->Get_Transform();

		// you'd think this would work, but, it does not. do it the hard way.
		// boneIndex = childObject->Get_Sub_Object_Bone_Index(childObject);

		for (Int subObj = 0; subObj < robj->Get_Num_Sub_Objects(); subObj++)
		{	
			RenderObjClass* test = robj->Get_Sub_Object(subObj);
			if (test == childObject)
			{
				boneIndex = robj->Get_Sub_Object_Bone_Index(0, subObj);
#if defined(_DEBUG)
				test->Release_Ref();
				test = robj->Get_Sub_Object_On_Bone(0, boneIndex);
				DEBUG_ASSERTCRASH(test != nullptr && test == childObject, ("*** ASSET ERROR: Hmm, bone problem"));
#endif
			}
			if (test) test->Release_Ref();
		}

		childObject->Release_Ref();

		return true;
	}
	else
	{
		return false;
	}
}

//-------------------------------------------------------------------------------------------------
static Bool doSingleBoneName(RenderObjClass* robj, const AsciiString& boneName, PristineBoneInfoMap& map)
{
	Bool foundAsBone = false;
	Bool foundAsSubObj = false;

	PristineBoneInfo info;
	AsciiString tmp;

	AsciiString boneNameTmp = boneName;
	boneNameTmp.toLower();	// convert to all-lowercase to avoid case sens issues later

	setFPMode();

	if (findSingleBone(robj, boneNameTmp, info.mtx, info.boneIndex))
	{
//DEBUG_LOG(("added bone %s\n",boneNameTmp.str()));
		BONEPOS_LOG(("Caching bone %s (index %d)\n", boneNameTmp.str(), info.boneIndex));
		BONEPOS_DUMPMATRIX3D(&(info.mtx));

		map[NAMEKEY(boneNameTmp)] = info;
		foundAsBone = true;
	}

	for (Int i = 1; i <= 99; ++i)
	{
		tmp.format("%s%02d", boneNameTmp.str(), i);
		if (findSingleBone(robj, tmp, info.mtx, info.boneIndex))
		{
//DEBUG_LOG(("added bone %s\n",tmp.str()));
			BONEPOS_LOG(("Caching bone %s (index %d)\n", tmp.str(), info.boneIndex));
			BONEPOS_DUMPMATRIX3D(&(info.mtx));
			map[NAMEKEY(tmp)] = info;
			foundAsBone = true;
		}
		else
		{
			break;
		}
	}

	if (!foundAsBone)
	{
		if (findSingleSubObj(robj, boneNameTmp, info.mtx, info.boneIndex))
		{
//DEBUG_LOG(("added subobj %s\n",boneNameTmp.str()));
			BONEPOS_LOG(("Caching bone from subobject %s (index %d)\n", boneNameTmp.str(), info.boneIndex));
			BONEPOS_DUMPMATRIX3D(&(info.mtx));
			map[NAMEKEY(boneNameTmp)] = info;
			foundAsSubObj = true;
		}

		for (Int i = 1; i <= 99; ++i)
		{
			tmp.format("%s%02d", boneNameTmp.str(), i);
			if (findSingleSubObj(robj, tmp, info.mtx, info.boneIndex))
			{
//DEBUG_LOG(("added subobj %s\n",tmp.str()));
				BONEPOS_LOG(("Caching bone from subobject %s (index %d)\n", tmp.str(), info.boneIndex));
				BONEPOS_DUMPMATRIX3D(&(info.mtx));
				map[NAMEKEY(tmp)] = info;
				foundAsSubObj = true;
			}
			else
			{
				break;
			}
		}
	}

	return foundAsBone || foundAsSubObj;
}

//-------------------------------------------------------------------------------------------------
// Checks the INI Show/HideSubObject names once per state against the model. Names the model lacks
// (data errors) are dropped with a single log line instead of being searched and reported on every
// state change. The list belongs to the template, so the check covers all drawables of the type.
void ModelConditionInfo::validateHideShowSubObjs(RenderObjClass* robj, const char* templateName) const
{
	if ((m_validStuff & HIDESHOW_VALID) || robj == nullptr)
		return;

	for (std::vector<HideShowSubObjInfo>::iterator it = m_hideShowVec.begin(); it != m_hideShowVec.end(); )
	{
		RenderObjClass* subObj = robj->Get_Sub_Object_By_Name(it->subObjName.str());
		if (subObj != nullptr)
		{
			subObj->Release_Ref();
			++it;
		}
		else
		{
			DEBUG_LOG(("*** ASSET ERROR: SubObject %s not found in model %s (%s) -- entry ignored\n",
				it->subObjName.str(), m_modelName.str(), templateName));
			it = m_hideShowVec.erase(it);
		}
	}
	m_validStuff |= HIDESHOW_VALID;
}

//-------------------------------------------------------------------------------------------------
void ModelConditionInfo::validateStuff(RenderObjClass* robj, Real scale, const std::vector<AsciiString>& extraPublicBones) const
{
// srj sez: hm, this doesn't make sense; I think we really do need to validate transition states.
//	if (m_transition != NO_TRANSITION)
//		return;
	
	loadAnimations();
	if (!(m_validStuff & PUBLIC_BONES_VALID) && isValidTimeToCalcLogicStuff())
	{
		for (std::vector<AsciiString>::const_iterator bone_it = extraPublicBones.begin(); bone_it != extraPublicBones.end(); ++bone_it)
		{
			addPublicBone(*bone_it);
		}
		m_validStuff |= PUBLIC_BONES_VALID;
	}
	validateCachedBones(robj, scale);
	validateTurretInfo();
	validateWeaponBarrelInfo();
}

//-------------------------------------------------------------------------------------------------
void ModelConditionInfo::validateCachedBones(RenderObjClass* robj, Real scale) const
{
	//DEBUG_ASSERTCRASH(isValidTimeToCalcLogicStuff(), ("calling validateCachedBones() from in GameClient!"));
	if (m_validStuff & PRISTINE_BONES_VALID)
		return;

	if (!isValidTimeToCalcLogicStuff())
	{
		m_pristineBones.clear();
		m_validStuff &= ~PRISTINE_BONES_VALID;
		return;
	}

	setFPMode();

	BONEPOS_LOG(("Validating bones for %s: %s", m_modelName.str(), getDescription().str()));
	//BONEPOS_LOG(("Passing in valid render obj: %d\n", (robj != 0)));
	BONEPOS_DUMPREAL(scale);

	m_pristineBones.clear();

	// go ahead and set this here, in case we return early.
	m_validStuff |= PRISTINE_BONES_VALID;

	Bool tossRobj = false;
	if (robj == nullptr)
	{
		if (m_modelName.isEmpty())
		{
			//BONEPOS_LOG(("Bailing: model name is empty\n"));
			return;
		}

		robj = W3DDisplay::m_assetManager->Create_Render_Obj(m_modelName.str(), scale, 0);
		DEBUG_ASSERTCRASH(robj, ("*** ASSET ERROR: Model %s not found!\n",m_modelName.str()));
		if (!robj)
		{
			//BONEPOS_LOG(("Bailing: could not load render object\n"));
			return;
		}
		tossRobj = true;
	}

	Matrix3D			originalTransform = robj->Get_Transform();	// save the transform
	HLodClass*		hlod = nullptr;
	HAnimClass*		curAnim = nullptr;
	int						numFrames = 0;
	float					frame = 0.0f;
	int						mode = 0;
	float					mult = 1.0f;

	if (robj->Class_ID() == RenderObjClass::CLASSID_HLOD)
	{
		hlod = static_cast<HLodClass*>(robj);
		curAnim = hlod->Peek_Animation_And_Info(frame, numFrames, mode, mult);
	}

	// if we have any animations in this state, always choose the first, since the animations
	// vary on a per-client basis.
	HAnimClass* animToUse;
	if (m_animations.size() > 0)
	{
		animToUse = m_animations.front().getAnimHandle();	// return an AddRef'ed handle
	}
	else
	{
		animToUse = curAnim;	// Peek_Animation_And_Info does not addref, so we must do so here
		if (animToUse)
			animToUse->Add_Ref();
	}
	if (animToUse != nullptr)
	{
		// make sure we're in frame zero.
		Int whichFrame = testFlagBit(m_flags, PRISTINE_BONE_POS_IN_FINAL_FRAME) ? animToUse->Get_Num_Frames()-1 : 0;
		robj->Set_Animation(animToUse, static_cast<float>(whichFrame), RenderObjClass::ANIM_MODE_MANUAL);
		// must balance the addref, above
		REF_PTR_RELEASE(animToUse);
		animToUse = nullptr;
	}

	Matrix3D tmp(true);
	tmp.Scale(scale);
	robj->Set_Transform(tmp);					// set to identity transform

	if (TheGlobalData)
	{
		for (std::vector<AsciiString>::const_iterator it = TheGlobalData->m_standardPublicBones.begin(); it != TheGlobalData->m_standardPublicBones.end(); ++it)
		{
			if (!doSingleBoneName(robj, *it, m_pristineBones))
			{
				// don't crash here, since these are catch-all global bones and won't be present in most models.
				//DEBUG_CRASH(("public bone %s (and variations thereof) not found in model %s!\n",it->str(),m_modelName.str()));
			}
			//else
			//{
			//	DEBUG_LOG(("global bone %s (or variations thereof) found in model %s\n",it->str(),m_modelName.str()));
			//}
		}
	}
	for (std::vector<AsciiString>::const_iterator it = m_publicBones.begin(); it != m_publicBones.end(); ++it)
	{
		if (!doSingleBoneName(robj, *it, m_pristineBones))
		{
			// we specifically requested this bone for this model, so the fault is worth a log line
			assetErrorLog("*** ASSET ERROR: public bone '%s' (and variations thereof) not found in model %s!", it->str(), m_modelName.str());
		}
		//else
		//{
		//	DEBUG_LOG(("extra bone %s (or variations thereof) found in model %s\n",it->str(),m_modelName.str()));
		//}
	}

	robj->Set_Transform(originalTransform);			// restore previous transform
	if (curAnim != nullptr)
	{
		robj->Set_Animation(curAnim, frame, mode);
		hlod->Set_Animation_Frame_Rate_Multiplier(mult);
	}

	if (tossRobj)
	{
		REF_PTR_RELEASE(robj);
	}
}

//-------------------------------------------------------------------------------------------------
void ModelConditionInfo::validateWeaponBarrelInfo() const
{
	//DEBUG_ASSERTCRASH(isValidTimeToCalcLogicStuff(), ("calling validateWeaponBarrelInfo() from in GameClient!"));
	if (m_validStuff & BARRELS_VALID)
		return;

	if (!isValidTimeToCalcLogicStuff())
	{
		return;
	}

	setFPMode();

	for (int wslot = 0; wslot < WEAPONSLOT_COUNT; ++wslot)
	{
		m_weaponBarrelInfoVec[wslot].clear();
		m_hasRecoilBonesOrMuzzleFlashes[wslot] = false;

		const AsciiString& fxBoneName = m_weaponFireFXBoneName[wslot];
		const AsciiString& recoilBoneName = m_weaponRecoilBoneName[wslot];
		const AsciiString& mfName = m_weaponMuzzleFlashName[wslot];
		const AsciiString& plbName = m_weaponProjectileLaunchBoneName[wslot];
    
// a useful tool for finding missing INI entries in the weapon-bone block
// since this class has no idea which object it refers to, it must assume that the projectilelaunchbonename
// is required if the fxBoneName is specified
// this is not always true, since some weapons have no launched projectiles, hence the caveat in the assert message
//#if defined(_DEBUG)
//    if (
//          ( m_modelName.startsWith("UV") || m_modelName.startsWith("NV") || m_modelName.startsWith("AV") ||
//            m_modelName.startsWith("uv") || m_modelName.startsWith("nv") || m_modelName.startsWith("av")
//          )
//
//          && fxBoneName.isNotEmpty()
//          
//      )
//    DEBUG_ASSERTCRASH( plbName.isNotEmpty(), ("You appear to have a missing projectilelaunchbonename. \n Promptly ignore this assert if this model is used by a non-projectile-weapon-bearing unit\nModel name = %s.", m_modelName.str()) );
//#endif

		if (fxBoneName.isNotEmpty() || recoilBoneName.isNotEmpty() || mfName.isNotEmpty() || plbName.isNotEmpty())
		{
			Int prevFxBone = 0;
			char buffer[256];
			for (Int i = 1; i <= 99; ++i)
			{
				WeaponBarrelInfo info;
				info.m_projectileOffsetMtx.Make_Identity();

				if (!recoilBoneName.isEmpty())
				{
					WWFormat::Into(buffer, "{}{:02d}", recoilBoneName.str(), i);
					findPristineBone(NAMEKEY(buffer), &info.m_recoilBone);
				}
				if (!mfName.isEmpty())
				{
					WWFormat::Into(buffer, "{}{:02d}", mfName.str(), i);
					findPristineBone(NAMEKEY(buffer), &info.m_muzzleFlashBone);
#if defined(_DEBUG)
					if (info.m_muzzleFlashBone)
						info.m_muzzleFlashBoneName = buffer;
#endif
				}
				if (!fxBoneName.isEmpty())
				{
					WWFormat::Into(buffer, "{}{:02d}", fxBoneName.str(), i);
					findPristineBone(NAMEKEY(buffer), &info.m_fxBone);
					// special case: if we have multiple muzzleflashes, but only one fxbone, use that fxbone for everything.
					if (info.m_fxBone == 0 && info.m_muzzleFlashBone != 0)
						info.m_fxBone = prevFxBone;
				}

				Int plbBoneIndex = 0;
				if (!plbName.isEmpty())
				{
					WWFormat::Into(buffer, "{}{:02d}", plbName.str(), i);
					const Matrix3D* mtx = findPristineBone(NAMEKEY(buffer), &plbBoneIndex);
					if (mtx != nullptr)
						info.m_projectileOffsetMtx = *mtx;
				}

				if (info.m_fxBone == 0 && info.m_recoilBone == 0 && info.m_muzzleFlashBone == 0 && plbBoneIndex == 0)
					break;

				CRCDEBUG_LOG(("validateWeaponBarrelInfo() - model name %s wslot %d\n", m_modelName.str(), wslot));
				DUMPMATRIX3D(&(info.m_projectileOffsetMtx));
				BONEPOS_LOG(("validateWeaponBarrelInfo() - model name %s wslot %d\n", m_modelName.str(), wslot));
				BONEPOS_DUMPMATRIX3D(&(info.m_projectileOffsetMtx));
				m_weaponBarrelInfoVec[wslot].push_back(info);

				if (info.m_recoilBone != 0 || info.m_muzzleFlashBone != 0)
					m_hasRecoilBonesOrMuzzleFlashes[wslot] = true;

				prevFxBone = info.m_fxBone;
			}
			
			if (m_weaponBarrelInfoVec[wslot].empty())
			{
				// try the unadorned names
				WeaponBarrelInfo info;
				if (!recoilBoneName.isEmpty())
					findPristineBone(NAMEKEY(recoilBoneName), &info.m_recoilBone);

				if (!mfName.isEmpty())
					findPristineBone(NAMEKEY(mfName), &info.m_muzzleFlashBone);

#if defined(_DEBUG)
				if (info.m_muzzleFlashBone)
					info.m_muzzleFlashBoneName = mfName;
#endif

				const Matrix3D* plbMtx = plbName.isEmpty() ? nullptr : findPristineBone(NAMEKEY(plbName), nullptr);
				if (plbMtx != nullptr)
					info.m_projectileOffsetMtx = *plbMtx;
				else
					info.m_projectileOffsetMtx.Make_Identity();

				if (!fxBoneName.isEmpty())
					findPristineBone(NAMEKEY(fxBoneName), &info.m_fxBone);
				
				if (info.m_fxBone != 0 || info.m_recoilBone != 0 || info.m_muzzleFlashBone != 0 || plbMtx != nullptr)
				{
					CRCDEBUG_LOG(("validateWeaponBarrelInfo() - model name %s (unadorned) wslot %d\n", m_modelName.str(), wslot));
					DUMPMATRIX3D(&(info.m_projectileOffsetMtx));
					BONEPOS_LOG(("validateWeaponBarrelInfo() - model name %s (unadorned) wslot %d\n", m_modelName.str(), wslot));
					BONEPOS_DUMPMATRIX3D(&(info.m_projectileOffsetMtx));
					m_weaponBarrelInfoVec[wslot].push_back(info);
					if (info.m_recoilBone != 0 || info.m_muzzleFlashBone != 0)
						m_hasRecoilBonesOrMuzzleFlashes[wslot] = true;
				}
				else
				{
					CRCDEBUG_LOG(("validateWeaponBarrelInfo() - model name %s (unadorned) found nothing\n", m_modelName.str()));
					BONEPOS_LOG(("validateWeaponBarrelInfo() - model name %s (unadorned) found nothing\n", m_modelName.str()));
				}
			}	// if empty

			if (m_modelName.isNotEmpty() && m_weaponBarrelInfoVec[wslot].empty())
				assetErrorLog("*** ASSET ERROR: No fx bone named '%s' found in model %s!", fxBoneName.str(), m_modelName.str());
		}
	}
	m_validStuff |= BARRELS_VALID;
} 

//-------------------------------------------------------------------------------------------------
void ModelConditionInfo::validateTurretInfo() const
{
	//DEBUG_ASSERTCRASH(isValidTimeToCalcLogicStuff(), ("calling validateTurretInfo() from in GameClient!"));
	if (m_validStuff & TURRETS_VALID)
		return;

	setFPMode();

	for (int tslot = 0; tslot < MAX_TURRETS; ++tslot)
	{
		TurretInfo& tur = m_turrets[tslot];
		if (!isValidTimeToCalcLogicStuff() || m_modelName.isEmpty())
		{
			tur.m_turretAngleBone = 0;
			tur.m_turretPitchBone = 0;
			continue;
		}

		if (tur.m_turretAngleNameKey != NAMEKEY_INVALID)
		{
			if (findPristineBone(tur.m_turretAngleNameKey, &tur.m_turretAngleBone) == nullptr)
			{
				assetErrorLog("*** ASSET ERROR: TurretBone %s not found! (%s)", KEYNAME(tur.m_turretAngleNameKey).str(), m_modelName.str());
				tur.m_turretAngleBone = 0;
			}
		}
		else
		{
			tur.m_turretAngleBone = 0;
		}	

		if (tur.m_turretPitchNameKey != NAMEKEY_INVALID)
		{
			if (findPristineBone(tur.m_turretPitchNameKey, &tur.m_turretPitchBone) == nullptr)
			{
				assetErrorLog("*** ASSET ERROR: TurretBone %s not found! (%s)", KEYNAME(tur.m_turretPitchNameKey).str(), m_modelName.str());
				tur.m_turretPitchBone = 0;
			}
		}
		else
		{
			tur.m_turretPitchBone = 0;
		}	
	}

	if (isValidTimeToCalcLogicStuff())
	{
		m_validStuff |= TURRETS_VALID;
	}
} 

//-------------------------------------------------------------------------------------------------
const Matrix3D* ModelConditionInfo::findPristineBone(NameKeyType boneName, Int* boneIndex) const
{
	DEBUG_ASSERTCRASH((m_validStuff & PRISTINE_BONES_VALID), ("*** ASSET ERROR: bones are not valid"));
	if (!(m_validStuff & PRISTINE_BONES_VALID))
	{
		// set it to zero, some callers rely on this
		if (boneIndex)
			*boneIndex = 0;
		return nullptr;
	}

	if (boneName == NAMEKEY_INVALID)
	{
		// set it to zero, some callers rely on this
		if (boneIndex)
			*boneIndex = 0;
		return nullptr;
	}

	PristineBoneInfoMap::const_iterator it = m_pristineBones.find(boneName);
	if (it != m_pristineBones.end())
	{
		if (boneIndex)
			*boneIndex = it->second.boneIndex;
		return &it->second.mtx;
	}
	else
	{
		// set it to zero -- some callers rely on this!
		if (boneIndex)
			*boneIndex = 0;
		return nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
Bool ModelConditionInfo::findPristineBonePos(NameKeyType boneName, Coord3D& pos) const
{
	const Matrix3D* mtx = findPristineBone(boneName, nullptr);
	if (mtx)
	{
		Vector3 v = mtx->Get_Translation();
		pos.x = v.X;
		pos.y = v.Y;
		pos.z = v.Z;
		return true;
	}
	else
	{
		pos.zero();
		return false;
	}
}

//-------------------------------------------------------------------------------------------------
void ModelConditionInfo::loadAnimations() const
{ 
#ifdef RETAIN_ANIM_HANDLES
	for (W3DAnimationVector::const_iterator it2 = m_animations.begin(); it2 != m_animations.end(); ++it2)
	{
		HAnimClass* h = it2->getAnimHandle();	// just force it to get loaded
		REF_PTR_RELEASE(h);	
		h = nullptr;
	}
#else
	// srj sez: I think there is no real reason to preload these all anymore. things that need the anims
	// (for bones) will force an implicit load anyway, but there's no point in forcibly loading the anims 
	// that don't contain interesting logical bones.
#endif
}

//-------------------------------------------------------------------------------------------------
void ModelConditionInfo::clear()
{ 
	int i;
#if defined(_DEBUG)
	m_description.clear();
#endif
	m_conditionsYesVec.clear();
	m_modelName.clear();
	for (i = 0; i < MAX_TURRETS; ++i)
	{
		m_turrets[i].clear();
	}
	m_hideShowVec.clear();
	for (i = 0; i < WEAPONSLOT_COUNT; ++i)
	{
		m_weaponFireFXBoneName[i].clear();
		m_weaponRecoilBoneName[i].clear();
		m_weaponMuzzleFlashName[i].clear();
		m_weaponProjectileLaunchBoneName[i].clear();
		m_weaponBarrelInfoVec[i].clear();
		m_hasRecoilBonesOrMuzzleFlashes[i] = false;
	}
	m_particleSysBones.clear();
	m_animations.clear();
	m_flags = 0;
	m_transitionKey = NAMEKEY_INVALID;
	m_allowToFinishKey = NAMEKEY_INVALID;
	m_iniReadFlags = 0;
	m_mode = RenderObjClass::ANIM_MODE_ONCE;
	m_transitionSig = NO_TRANSITION;
	m_animMinSpeedFactor = 1.0f;
	m_animMaxSpeedFactor = 1.0f;
	m_pristineBones.clear();
	m_validStuff = 0;
}
