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

// Save game and CRC (W3DModelDraw, W3DModelDrawModuleData)

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DModelDraw::crc( Xfer *xfer )
{

	// extend base class
	DrawModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: Added animation frame (CBD)
	*/
// ------------------------------------------------------------------------------------------------
void W3DModelDraw::xfer( Xfer *xfer )
{

	// version
	const XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	DrawModule::xfer( xfer );

	// weapon recoil info vectors
	UnsignedByte recoilInfoCount;
	WeaponRecoilInfo weaponRecoilInfo;
	for( Int i = 0; i < WEAPONSLOT_COUNT; ++i )
	{

		// count of data here
		recoilInfoCount = static_cast<UnsignedByte>(static_cast<Int>(m_weaponRecoilInfoVec[ i ].size()));
		xfer->xferUnsignedByte( &recoilInfoCount );
		if( xfer->getXferMode() == XFER_SAVE )
		{

			WeaponRecoilInfoVec::const_iterator it;
			for( it = m_weaponRecoilInfoVec[ i ].begin(); it != m_weaponRecoilInfoVec[ i ].end(); ++it )
			{

				// state
				weaponRecoilInfo.m_state = (*it).m_state;
				xfer->xferUser( &weaponRecoilInfo.m_state, sizeof( WeaponRecoilInfo::RecoilState ) );

				// shift
				weaponRecoilInfo.m_shift = (*it).m_shift;
				xfer->xferReal( &weaponRecoilInfo.m_shift );

				// recoil rate
				weaponRecoilInfo.m_recoilRate = (*it).m_recoilRate;
				xfer->xferReal( &weaponRecoilInfo.m_recoilRate );

			}  // end for, it
			
		}  // end if, save
		else
		{

			// clear this list before loading
			m_weaponRecoilInfoVec[ i ].clear();

			// read each data item
			for( Int j = 0; j < recoilInfoCount; ++j )
			{

				// read state
				xfer->xferUser( &weaponRecoilInfo.m_state, sizeof( WeaponRecoilInfo::RecoilState ) );

				// read shift
				xfer->xferReal( &weaponRecoilInfo.m_shift );

				// read recoil rate
				xfer->xferReal( &weaponRecoilInfo.m_recoilRate );

				// stuff it in the vector
				m_weaponRecoilInfoVec[ i ].push_back( weaponRecoilInfo );

			}  // end for, j

		}  // end else, load

	}  // end for, i

	// sub object vector
	UnsignedByte subObjectCount = static_cast<UnsignedByte>(m_subObjectVec.size());
	xfer->xferUnsignedByte( &subObjectCount );
	ModelConditionInfo::HideShowSubObjInfo hideShowSubObjInfo;
	if( xfer->getXferMode() == XFER_SAVE )
	{

		std::vector<ModelConditionInfo::HideShowSubObjInfo>::const_iterator it;
		for( it = m_subObjectVec.begin(); it != m_subObjectVec.end(); ++it )
		{

			// sub object name
			hideShowSubObjInfo.subObjName = (*it).subObjName;
			xfer->xferAsciiString( &hideShowSubObjInfo.subObjName );

			// hide
			hideShowSubObjInfo.hide = (*it).hide;
			xfer->xferBool( &hideShowSubObjInfo.hide );

		}  // end for, it

	}  // end if, save
	else
	{

		// the vector must be emtpy
		m_subObjectVec.clear();

		// read each data item
		for( UnsignedByte i = 0; i < subObjectCount; ++i )
		{

			// name
			xfer->xferAsciiString( &hideShowSubObjInfo.subObjName );

			// hide
			xfer->xferBool( &hideShowSubObjInfo.hide );

			// stuff in vector
			m_subObjectVec.push_back( hideShowSubObjInfo );

		}  // end for, i

	}  // end else, load

	// animation
	if( version >= 2 )
	{
		
		if( xfer->getXferMode() == XFER_SAVE )
		{
				// srj sez: don't save info for transition states, since we can't really
				// restore them effectively.
			if ( m_renderObject 
					&& m_renderObject->Class_ID() == RenderObjClass::CLASSID_HLOD
					&& m_curState
					&& m_curState->m_transitionSig == NO_TRANSITION )
			{
				
				// cast to HLod
				HLodClass *hlod = static_cast<HLodClass*>(m_renderObject);

				// get animation info
				Int mode, numFrames;
				Real frame, dummy;
				HAnimClass *anim = hlod->Peek_Animation_And_Info( frame, numFrames, mode, dummy );

				// animation data is present
				Bool present = anim ? TRUE : FALSE;
				xfer->xferBool( &present );

				// if animation data is present
				if( anim )
				{

					// xfer mode
					xfer->xferInt( &mode );

					//
					// xfer frame as a fraction (this will allow the animations to change
					// in future patches but will still be mostly correct)
					//
					Real percent = frame / INT_TO_REAL( anim->Get_Num_Frames()-1 );
					xfer->xferReal( &percent );

				}  // end if, anim

			}  // end if
			else
			{

				// animation data is *NOT* present
				Bool present = FALSE;
				xfer->xferBool( &present );

			}  // end else

		}  // end if, save
		else
		{

			// read animation present flag
			Bool present;
			xfer->xferBool( &present );

			if( present )
			{
				
				{
					// read mode
					Int mode;
					xfer->xferInt( &mode );		// note, this will be ignored
				}

				// read percent
				Real percent;
				xfer->xferReal( &percent );

				//
				// cast render object to HLod, if this is no longer possible we have read the
				// data already and can just ignore it
				//
				if( m_renderObject && m_renderObject->Class_ID() == RenderObjClass::CLASSID_HLOD )
				{
					HLodClass *hlod = static_cast<HLodClass *>(m_renderObject);

					// get anim
					HAnimClass *anim = hlod->Peek_Animation();

					// set animation data
					if( anim )
					{

						// figure out frame number given percent written in file and total frames on anim
						Real frame = percent * INT_TO_REAL( anim->Get_Num_Frames()-1 );

						float dummy1, dummy2;
						int curMode, dummy3;
						hlod->Peek_Animation_And_Info(dummy1, dummy3, curMode, dummy2);
						
						// srj sez: do not change the animation mode. it's too risky, since if you change (say) a nonlooping
						// to a looping, something might break since it could rely on that anim terminating.

						// set animation data
						hlod->Set_Animation( anim, frame, curMode );

					}  // end if, anim

				}  // end if

			}  // end if

		}  // end else, load

	}  // end if, version with animation info

	// when loading, update the sub objects if we have any
	if( xfer->getXferMode() == XFER_LOAD && m_subObjectVec.empty() == FALSE )
		updateSubObjects();

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DModelDraw::loadPostProcess( void )
{

	// extend base class
	DrawModule::loadPostProcess();

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
void W3DModelDrawModuleData::crc( Xfer *x )
{
	xfer(x);
}

// ------------------------------------------------------------------------------------------------
void W3DModelDrawModuleData::xfer( Xfer *x )
{

	// version
	const XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	x->xferVersion( &version, currentVersion );

	for (ModelConditionVector::iterator it = m_conditionStates.begin(); it != m_conditionStates.end(); ++it)
	{
		ModelConditionInfo *info = &(*it);
		x->xferByte(&(info->m_validStuff));
#if defined(_DEBUG)
		x->xferAsciiString(&(info->m_description));
#endif
		if (info->m_validStuff)
		{
			for (PristineBoneInfoMap::iterator bit = info->m_pristineBones.begin(); bit != info->m_pristineBones.end(); ++bit)
			{
				PristineBoneInfo *bone = &(bit->second);
				x->xferInt(&(bone->boneIndex));
				x->xferUser(&(bone->mtx), sizeof(Matrix3D));
			}
			Int i;
			for (i=0; i<MAX_TURRETS; ++i)
			{
				x->xferInt(&(info->m_turrets[i].m_turretAngleBone));
				x->xferInt(&(info->m_turrets[i].m_turretPitchBone));
			}
			for (i=0; i<WEAPONSLOT_COUNT; ++i)
			{
				for (ModelConditionInfo::WeaponBarrelInfoVec::iterator wit = info->m_weaponBarrelInfoVec[i].begin(); wit != info->m_weaponBarrelInfoVec[i].end(); ++wit)
				{
					x->xferUser(&(wit->m_projectileOffsetMtx), sizeof(Matrix3D));
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
void W3DModelDrawModuleData::loadPostProcess( void )
{
}
