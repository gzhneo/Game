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

// Showing and hiding sub-objects (bones, muzzle flashes, headlights, garrison flags, projectiles)

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::showSubObject( const AsciiString& name, Bool show )
{
	if( name.isNotEmpty() )
	{
		Bool found = false;
		for( std::vector<ModelConditionInfo::HideShowSubObjInfo>::iterator it = m_subObjectVec.begin(); it != m_subObjectVec.end(); ++it )
		{
			if( stricmp( it->subObjName.str(), name.str() ) == 0 )
			{
				it->hide = !show;
				found = true;
			}
		}

		if( !found )
		{
			ModelConditionInfo::HideShowSubObjInfo info;
			info.subObjName = name;
			info.hide = !show;
			m_subObjectVec.push_back( info );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/**
	Utility function to make it easier to recursively hide all objects connected to a certain bone.
	We will hide all objects connected to bones which are children of boneIdx
*/
static void doHideShowBoneSubObjs(Bool state, Int numSubObjects, Int boneIdx, RenderObjClass *fullObject, const HTreeClass *htree)
{
#if 1	//(gth) fixed and tested this version
	for (Int i=0; i < numSubObjects; i++) 
	{
		bool is_child = false;
		Int parentBoneIndex = fullObject->Get_Sub_Object_Bone_Index(0, i);
		
		while (parentBoneIndex != 0) 
		{
			parentBoneIndex = htree->Get_Parent_Index(parentBoneIndex);

			if (parentBoneIndex == boneIdx) 
			{
				is_child = true;
				break;
			}
		}

		if (is_child) 
		{
			RenderObjClass* childObject = fullObject->Get_Sub_Object(i);
			childObject->Set_Hidden(state);
			childObject->Release_Ref();
		}
	}
#endif
}

//-------------------------------------------------------------------------------------------------
void ModelConditionInfo::WeaponBarrelInfo::setMuzzleFlashHidden(RenderObjClass *fullObject, Bool hide) const
{
	if (fullObject)
	{
		RenderObjClass* childObject = fullObject->Get_Sub_Object_On_Bone(0, m_muzzleFlashBone);
		if (childObject)
		{
			childObject->Set_Hidden(hide);
			childObject->Release_Ref();
		}
		else
		{
			DEBUG_CRASH(("*** ASSET ERROR: childObject %s not found in setMuzzleFlashHidden()\n",m_muzzleFlashBoneName.str()));
		}
	}
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::doHideShowSubObjs(const std::vector<ModelConditionInfo::HideShowSubObjInfo>* vec)
{
	if (!m_renderObject)
		return;

	if (!vec->empty())
	{
		for (std::vector<ModelConditionInfo::HideShowSubObjInfo>::const_iterator it = vec->begin(); it != vec->end(); ++it)
		{
			Int objIndex;
			RenderObjClass* subObj;

			if ((subObj = m_renderObject->Get_Sub_Object_By_Name(it->subObjName.str(), &objIndex)) != nullptr)
			{
				subObj->Set_Hidden(it->hide);

				const HTreeClass *htree = m_renderObject->Get_HTree();
				if (htree)
				{	
					//get the bone of this subobject so we can hide all other child objects that use this bone
					//as a parent.
					Int boneIdx = m_renderObject->Get_Sub_Object_Bone_Index(0, objIndex);
					doHideShowBoneSubObjs(it->hide, m_renderObject->Get_Num_Sub_Objects(), boneIdx, m_renderObject, htree);
				}
				subObj->Release_Ref();
			}
			else
			{
				// INI lists are prechecked by validateHideShowSubObjs(); only script and projectile lists get here.
				DEBUG_LOG(("*** ASSET ERROR: SubObject %s not found (%s)!\n",it->subObjName.str(),getDrawable()->getTemplate()->getName().str()));
			}
		}
	}

	//Kris (added Aug 2002) 
	//This is really important as it allows a person to override modelcondition show/hide objects. Used by the A10 strike
	//September 2002 -- now used by the UpgradeSubObject system.
	if( !m_subObjectVec.empty() )
	{
		updateSubObjects();
	}
}

//-------------------------------------------------------------------------------------------------
#if defined(_DEBUG)	//art wants to see buildings without flags as a test.
void W3DModelDraw::hideGarrisonFlags(Bool hide)
{
	if (!m_renderObject)
		return;

	Int objIndex;
	RenderObjClass* subObj;

	if ((subObj = m_renderObject->Get_Sub_Object_By_Name("POLE", &objIndex)) != nullptr)
	{
		subObj->Set_Hidden(hide);

		const HTreeClass *htree = m_renderObject->Get_HTree();
		if (htree)
		{	
			//get the bone of this subobject so we can hide all other child objects that use this bone
			//as a parent.
			Int boneIdx = m_renderObject->Get_Sub_Object_Bone_Index(0, objIndex);
			doHideShowBoneSubObjs(hide, m_renderObject->Get_Num_Sub_Objects(), boneIdx, m_renderObject, htree);
		}
		subObj->Release_Ref();
	}
}
#endif

//-------------------------------------------------------------------------------------------------
/** Hides all subobjects which are headlights.  Used to disable lights on models during the day.*/
void W3DModelDraw::hideAllHeadlights(Bool hide)
{
	if (m_renderObject)
	{
		for (Int subObj = 0; subObj < m_renderObject->Get_Num_Sub_Objects(); subObj++)
		{	
			RenderObjClass* test = m_renderObject->Get_Sub_Object(subObj);
			if (strstr(test->Get_Name(),"HEADLIGHT"))
			{
				test->Set_Hidden(hide);
			}
			test->Release_Ref();
		}
	}
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::hideAllMuzzleFlashes(const ModelConditionInfo* state, RenderObjClass* renderObject)
{
	if (!state || !renderObject)
		return;

	if (!(state->m_validStuff & ModelConditionInfo::BARRELS_VALID))
	{
		return;
	}

	for (int wslot = 0; wslot < WEAPONSLOT_COUNT; ++wslot)
	{
		if (!state->m_hasRecoilBonesOrMuzzleFlashes[wslot])
			continue;

		const ModelConditionInfo::WeaponBarrelInfoVec& barrels = state->m_weaponBarrelInfoVec[wslot];
		for (ModelConditionInfo::WeaponBarrelInfoVec::const_iterator it = barrels.begin(); it != barrels.end(); ++it)
		{
			if (it->m_muzzleFlashBone != 0)
			{
				it->setMuzzleFlashHidden(renderObject, true);
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::updateProjectileClipStatus( UnsignedInt shotsRemaining, UnsignedInt maxShots, WeaponSlotType slot )
{
	if ((getW3DModelDrawModuleData()->m_projectileBoneFeedbackEnabledSlots & (1 << slot)) == 0)
		return; // Only if specified.

	doHideShowProjectileObjects( shotsRemaining, maxShots, slot );// Means effectively, show m of n.
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::updateDrawModuleSupplyStatus( Int , Int currentSupply )
{
	// This level only cares if you have anything or not
	if( currentSupply > 0 )
	{
		getDrawable()->setModelConditionState( MODELCONDITION_CARRYING );
	}
	else
	{
		getDrawable()->clearModelConditionState( MODELCONDITION_CARRYING );
	}
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::doHideShowProjectileObjects( UnsignedInt showCount, UnsignedInt maxCount, WeaponSlotType slot )
{
	// Loop through the projectile bones, and start hiding off the front.  That is, 8,9 means to hide the first only.
	if (maxCount < showCount)
	{
		DEBUG_CRASH(("Someone is trying to show more projectiles than they have.") );
		return;
	}
	Int hideCount = maxCount - showCount;

	std::vector<ModelConditionInfo::HideShowSubObjInfo> showHideVector;
	ModelConditionInfo::HideShowSubObjInfo oneEntry;
	if (m_curState->m_weaponProjectileHideShowName[slot].isEmpty())
	{
		for( Int projectileIndex = 0; static_cast<UnsignedInt>(projectileIndex) < maxCount; projectileIndex++ )
		{
			oneEntry.subObjName.format("%s%02d", m_curState->m_weaponProjectileLaunchBoneName[slot].str(), (projectileIndex + 1));
			oneEntry.hide = ((projectileIndex + 1) <= hideCount);
			showHideVector.push_back( oneEntry );
		}
	}
	else
	{
		oneEntry.subObjName = m_curState->m_weaponProjectileHideShowName[slot];
		oneEntry.hide = (0 < hideCount);
		showHideVector.push_back( oneEntry );
	}

	// Rather than duplicate recursive utility stuff, I will build a vector like those used by the main ShowHide setting
	doHideShowSubObjs(&showHideVector);
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::updateSubObjects()
{
	if (!m_renderObject)
		return;

	if (!m_subObjectVec.empty())
	{
		for (std::vector<ModelConditionInfo::HideShowSubObjInfo>::const_iterator it = m_subObjectVec.begin(); it != m_subObjectVec.end(); ++it)
		{
			Int objIndex;
			RenderObjClass* subObj;

			if ((subObj = m_renderObject->Get_Sub_Object_By_Name(it->subObjName.str(), &objIndex)) != nullptr)
			{
				subObj->Set_Hidden(it->hide);

				const HTreeClass *htree = m_renderObject->Get_HTree();
				if (htree)
				{	
					//get the bone of this subobject so we can hide all other child objects that use this bone
					//as a parent.
					Int boneIdx = m_renderObject->Get_Sub_Object_Bone_Index(0, objIndex);
					doHideShowBoneSubObjs( it->hide, m_renderObject->Get_Num_Sub_Objects(), boneIdx, m_renderObject, htree);
				}
				subObj->Release_Ref();
			}
			else
			{
				// INI lists are prechecked by validateHideShowSubObjs(); only script and projectile lists get here.
				DEBUG_LOG(("*** ASSET ERROR: SubObject %s not found (%s)!\n",it->subObjName.str(),getDrawable()->getTemplate()->getName().str()));
			}
		}
	}
}
