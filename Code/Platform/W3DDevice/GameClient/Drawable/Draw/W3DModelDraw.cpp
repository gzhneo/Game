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

// FILE: W3DModelDraw.cpp ///////////////////////////////////////////////////////////////////////
// Author: Colin Day, November 2001
// Desc:   Default w3d draw module
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////

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

#ifdef DEBUG_LOGGING
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <mutex>
#endif

// Faults in the asset data (a bone or animation the model lacks) are logged, once per message, and no longer
// stop a Debug run with an assertion: the EA data has a few (Laser Tank debris, cinematic buildings) that
// every Debug start would report. Nothing depends on the result; the model simply lacks the part.
// Messages already logged are remembered by hash in a fixed array: a container would allocate from the
// memory pools and show up as a leak in the report at exit, which runs before the statics are destroyed.
#ifdef DEBUG_LOGGING
void assetErrorLog(const char *format, ...)
{
	char buffer[512];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	std::uint64_t hash = 1469598103934665603ull;	// FNV-1a
	for (const char *c = buffer; *c; ++c)
		hash = (hash ^ static_cast<unsigned char>(*c)) * 1099511628211ull;
	static std::mutex lock;
	static std::uint64_t seen[2048];
	static int seenCount = 0;
	std::lock_guard<std::mutex> guard(lock);
	for (int i = 0; i < seenCount; ++i)
		if (seen[i] == hash)
			return;
	if (seenCount < 2048)
		seen[seenCount++] = hash;
	DEBUG_LOG(("%s\n", buffer));
}
#endif
#include "WW3D2/Mesh/mesh.h"
#include "WW3D2/Mesh/meshmdl.h"
#include "Common/BitFlagsIO.h"
#include "GameClient/GameClient.h"	// isRenderOnlyFrame()
#include "Lib/SafeString.h"
#include "W3DModelDrawInternal.h"

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

#if defined(DEBUG_CRC) && defined(_DEBUG)
LogClass::LogClass(const char *fname)
{
	char buffer[ _MAX_PATH ];
	GetModuleFileName( nullptr, buffer, sizeof( buffer ) );
	char *pEnd = buffer + strlen( buffer );
	while( pEnd != buffer )
	{
		if( *pEnd == '\\' )
		{
			*pEnd = 0;
			break;
		}
		pEnd--;
	}
	AsciiString fullPath;
	fullPath.format("%s\\%s", buffer, fname);
	m_fp = fopen(fullPath.str(), "wt");
}

LogClass::~LogClass()
{
	if (m_fp)
	{
		fclose(m_fp);
	}
}

void LogClass::log(const char *fmt, ...)
{
	DEBUG_ASSERTCRASH(isValidTimeToCalcLogicStuff(), ("Calc'ing logic bone pos in client!!!"));
	if (!m_fp /*|| !isValidTimeToCalcLogicStuff()*/)
		return;
	static char buf[1024];
	static Int lastFrame = 0;
	static Int lastIndex = 0;
	if (lastFrame != TheGameLogic->getFrame())
	{
		lastFrame = TheGameLogic->getFrame();
		lastIndex = 0;
	}

	va_list va;
	va_start( va, fmt );
	_vsnprintf(buf, 1024, fmt, va );
	buf[1023] = 0;
	va_end( va );

	char *tmp = buf;
	while (tmp && *tmp)
	{
		if (*tmp == '\r' || *tmp == '\n')
		{
			*tmp = ' ';
		}
		++tmp;
	}

	fprintf(m_fp, "%d:%d %s\n", lastFrame, lastIndex++, buf);
	fflush(m_fp);
}

void LogClass::dumpMatrix3D(const Matrix3D *m, AsciiString name, AsciiString fname, Int line)
{
	fname.toLower();
	fname = fname.reverseFind('\\') + 1;
	const Real *matrix = (const Real *)m;
	log("dumpMatrix3D() %s:%d %s\n",
		fname.str(), line, name.str());
	for (Int i=0; i<3; ++i)
		log("      0x%08X 0x%08X 0x%08X 0x%08X\n",
			AS_INT(matrix[(i<<2)+0]), AS_INT(matrix[(i<<2)+1]), AS_INT(matrix[(i<<2)+2]), AS_INT(matrix[(i<<2)+3]));
}

void LogClass::dumpReal(Real r, AsciiString name, AsciiString fname, Int line)
{
	if (!m_fp || !isValidTimeToCalcLogicStuff())
		return;
	fname.toLower();
	fname = fname.reverseFind('\\') + 1;
	log("dumpReal() %s:%d %s %8.8X (%f)\n",
		fname.str(), line, name.str(), AS_INT(r), r);
}

LogClass BonePosLog("bonePositions.txt");

#endif // DEBUG_CRC

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------


/** @todo: Move this to some kind of INI file*/
//
// Note: these values are saved in save files, so you MUST NOT REMOVE OR CHANGE
// existing values!
//
static const char *TerrainDecalTextureName[TERRAIN_DECAL_MAX]=
{
	"TERRAIN_DECAL_DEMORALIZED_OBSOLETE",
	"EXHorde",//enthusiastic
	"EXHorde_UP", //enthusiastic with nationalism 
	"EXHordeB",//enthusiastic vehicle
	"EXHordeB_UP", //enthusiastic vehicle with nationalism
	"EXJunkCrate",//Marks a crate as special
	"EXHordeC_UP", //enthusiastic with fanaticism 
	"EXChemSuit", //Marks a unit as having chemical suit on
	"",	//dummy entry for TERRAIN_DECAL_NONE
	"" //dummy entry for TERRAIN_DECAL_SHADOW_TEXTURE
};


//-------------------------------------------------------------------------------------------------
W3DAnimationInfo::W3DAnimationInfo(const AsciiString& name, Bool isIdle, Real distanceCovered) : 
#ifdef RETAIN_ANIM_HANDLES
	m_handle(nullptr), 
	m_naturalDurationInMsec(0),
#else
	m_naturalDurationInMsec(-1),
#endif
	m_name(name),
	m_isIdleAnim(isIdle), 
	m_distanceCovered(distanceCovered)
{ 
}

//-------------------------------------------------------------------------------------------------
W3DAnimationInfo::W3DAnimationInfo( const W3DAnimationInfo &r ) : 
	m_name(r.m_name),
#ifdef RETAIN_ANIM_HANDLES
	m_handle(r.m_handle),
#endif
	m_distanceCovered(r.m_distanceCovered),
	m_isIdleAnim(r.m_isIdleAnim),
	m_naturalDurationInMsec(r.m_naturalDurationInMsec)
{
#ifdef RETAIN_ANIM_HANDLES
	if (m_handle)
		m_handle->Add_Ref();
#endif
}

//-------------------------------------------------------------------------------------------------
W3DAnimationInfo& W3DAnimationInfo::operator=(const W3DAnimationInfo &r)
{
	m_name = r.m_name;
	m_distanceCovered = r.m_distanceCovered;
	m_naturalDurationInMsec = r.m_naturalDurationInMsec;
	m_isIdleAnim = r.m_isIdleAnim;

#ifdef RETAIN_ANIM_HANDLES
	REF_PTR_RELEASE(m_handle);
	m_handle = r.m_handle;
	if (m_handle)
		m_handle->Add_Ref();
#endif

	return (*this);
}


//-------------------------------------------------------------------------------------------------
// note that this now returns an ADDREFED handle, which must be released by the caller!
HAnimClass* W3DAnimationInfo::getAnimHandle() const
{ 
#ifdef RETAIN_ANIM_HANDLES
	if (m_handle == nullptr)
	{
		// Get_HAnim addrefs it, so we'll have to release it in our dtor.
		m_handle = W3DDisplay::m_assetManager->Get_HAnim(m_name.str());
		if (!m_handle)
			assetErrorLog("*** ASSET ERROR: animation %s not found", m_name.str());
		if (m_handle)
		{
			m_naturalDurationInMsec = m_handle->Get_Num_Frames() * 1000.0f / m_handle->Get_Frame_Rate();
		}
	}
	// since we have it locally, must addref.
	if (m_handle)
		m_handle->Add_Ref();
	return m_handle;
#else
	HAnimClass* handle = W3DDisplay::m_assetManager->Get_HAnim(m_name.str());
	if (!handle)
		assetErrorLog("*** ASSET ERROR: animation %s not found", m_name.str());
	if (handle != nullptr && m_naturalDurationInMsec < 0)
	{
		m_naturalDurationInMsec = handle->Get_Num_Frames() * 1000.0f / handle->Get_Frame_Rate();
	}
	// since Get_HAnim() returns an addrefed handle, we must NOT addref here.
	//if (handle)
	//	handle->Add_Ref();
	return handle;
#endif
}

//-------------------------------------------------------------------------------------------------
W3DAnimationInfo::~W3DAnimationInfo() 
{ 
#ifdef RETAIN_ANIM_HANDLES
	REF_PTR_RELEASE(m_handle); 
	m_handle = nullptr; 
#endif
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DModelDraw::W3DModelDraw(Thing *thing, const ModuleData* moduleData) : DrawModule(thing, moduleData)
{
	int i;
	m_animationMode = RenderObjClass::ANIM_MODE_LOOP;
	m_hideHeadlights = true;
	m_pauseAnimation = false;
	m_curState = nullptr;
	m_hexColor = 0;
	m_renderObject = nullptr;
	m_shadow = nullptr;
	m_shadowEnabled = TRUE;
	m_terrainDecal = nullptr;
	m_trackRenderObject = nullptr;
	m_whichAnimInCurState = -1;
	m_nextState = nullptr;
	m_nextStateAnimLoopDuration = NO_NEXT_DURATION;
	for (i = 0; i < WEAPONSLOT_COUNT; ++i)
	{
		m_weaponRecoilInfoVec[i].clear();
	}
	m_needRecalcBoneParticleSystems = false;
	m_fullyObscuredByShroud = false;

	// only validate the current time-of-day and weather conditions by default.
	getW3DModelDrawModuleData()->validateStuffForTimeAndWeather(getDrawable(), 
											TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT, 
											TheGlobalData->m_weather == WEATHER_SNOWY);

	ModelConditionFlags emptyFlags;
	const ModelConditionInfo* info = findBestInfo(emptyFlags);
	if (!info)
	{
		DEBUG_CRASH(("*** ASSET ERROR: all draw modules must have an IDLE state\n"));
		throw INI_INVALID_DATA;
	}

	Drawable* draw = getDrawable();

  if ( draw )
  {
	  Object* obj = draw->getObject();
	  if (obj)
	  {	
		  if (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT)
			  m_hexColor = obj->getNightIndicatorColor();
		  else
			  m_hexColor = obj->getIndicatorColor();
	  }

    // THE VAST MAJORITY OF THESE SHOULD BE TRUE
    if ( ! getW3DModelDrawModuleData()->m_receivesDynamicLights)
    {
      draw->setReceivesDynamicLights( FALSE );
		  DEBUG_LOG(("setReceivesDynamicLights = FALSE: %s\n", draw->getTemplate()->getName().str()));
    }
  }

	setModelState(info);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DModelDraw::onDrawableBoundToObject(void)
{
	getW3DModelDrawModuleData()->validateStuffForTimeAndWeather(getDrawable(), 
											TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT, 
											TheGlobalData->m_weather == WEATHER_SNOWY);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DModelDraw::~W3DModelDraw(void)
{
	if (m_trackRenderObject && TheTerrainTracksRenderObjClassSystem)
	{	
		TheTerrainTracksRenderObjClassSystem->unbindTrack(m_trackRenderObject);
		m_trackRenderObject = nullptr;
	}

	nukeCurrentRender(nullptr);
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setHidden(Bool hidden)
{
	if (m_renderObject)
		m_renderObject->Set_Hidden(hidden);

	if (m_shadow)
		m_shadow->enableShadowRender(!hidden);

	m_shadowEnabled = hidden;

	if (m_terrainDecal)
		m_terrainDecal->enableShadowRender(!hidden);
	
	if (m_trackRenderObject && hidden)
	{	const Coord3D* pos = getDrawable()->getPosition();
		m_trackRenderObject->addCapEdgeToTrack(pos->x,pos->y);
	}
	
	doStartOrStopParticleSys();
}

/**Free all data used by this model's shadow.  This is used to dynamically enable/disable shadows by the options screen*/
void W3DModelDraw::releaseShadows(void)	///< frees all shadow resources used by this module - used by Options screen.
{
	if (m_shadow)
		m_shadow->release();
	m_shadow = nullptr;
}

/** Create shadow resources if not already present. This is used to dynamically enable/disable shadows by the options screen*/
void W3DModelDraw::allocateShadows(void)
{
	const ThingTemplate *tmplate=getDrawable()->getTemplate();

	//Check if we don't already have a shadow but need one for this type of model.
	if (m_shadow == nullptr && m_renderObject && TheW3DShadowManager && tmplate->getShadowType() != SHADOW_NONE)
	{	
		Shadow::ShadowTypeInfo shadowInfo;
		safe_strcpy(shadowInfo.m_ShadowName, tmplate->getShadowTextureName().str());
		DEBUG_ASSERTCRASH(shadowInfo.m_ShadowName[0] != '\0', ("this should be validated in ThingTemplate now"));
		shadowInfo.allowUpdates			= FALSE;		//shadow image will never update
		shadowInfo.allowWorldAlign	= TRUE;	//shadow image will wrap around world objects
		shadowInfo.m_type						= static_cast<ShadowType>(tmplate->getShadowType());
		shadowInfo.m_sizeX					= tmplate->getShadowSizeX();
		shadowInfo.m_sizeY					= tmplate->getShadowSizeY();
		shadowInfo.m_offsetX				= tmplate->getShadowOffsetX();
		shadowInfo.m_offsetY				= tmplate->getShadowOffsetY();
  		m_shadow = TheW3DShadowManager->addShadow(m_renderObject, &shadowInfo);
		if (m_shadow)
		{	m_shadow->enableShadowInvisible(m_fullyObscuredByShroud);
			if (m_renderObject->Is_Hidden() || !m_shadowEnabled)
				m_shadow->enableShadowRender(FALSE);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setShadowsEnabled(Bool enable)
{
	if (m_shadow)
		m_shadow->enableShadowRender(enable);
	m_shadowEnabled = enable;
}

/**collect some stats about the rendering cost of this draw module */
#if defined(_DEBUG)	
void W3DModelDraw::getRenderCost(RenderCost & rc) const
{
	getRenderCostRecursive(rc,m_renderObject);
	if (m_shadow)
		m_shadow->getRenderCost(rc);
}
#endif //_DEBUG


/**recurse through sub-objs to collect stats about the rendering cost of this draw module */
#if defined(_DEBUG)	
void W3DModelDraw::getRenderCostRecursive(RenderCost & rc,RenderObjClass * robj) const 
{
	if (robj == nullptr) return;

	// recurse through sub-objects
	for (int i=0; i<robj->Get_Num_Sub_Objects(); i++) {
		RenderObjClass * sub_obj = robj->Get_Sub_Object(i);
		getRenderCostRecursive(rc,sub_obj);
		REF_PTR_RELEASE(sub_obj);
	}

	// Only consider visible sub-objects.  Some vehicles have hidden headlight cones for example
	// that are not normally rendered.
	if (robj->Is_Not_Hidden_At_All()) {
		
		// collect stats from meshes
		if (robj->Class_ID() == RenderObjClass::CLASSID_MESH) {
			MeshClass * mesh = static_cast<MeshClass*>(robj);
			MeshModelClass * model = mesh->Peek_Model();
			if (model != nullptr)
			{	if (model->Get_Flag(MeshGeometryClass::SORT))
					rc.addSortedMeshes(1);
				if (model->Get_Flag(MeshGeometryClass::SKIN))
					rc.addSkinMeshes(1);
			}

			rc.addDrawCalls(mesh->Get_Draw_Call_Count());
		}
		
		// collect bone stats.
		const HTreeClass * htree = robj->Get_HTree();
		if (htree != nullptr) {
			rc.addBones(htree->Num_Pivots());
		}
	}
}
#endif //_DEBUG

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setFullyObscuredByShroud(Bool fullyObscured)
{
	if (m_fullyObscuredByShroud != fullyObscured)
	{
		m_fullyObscuredByShroud = fullyObscured;

		if (m_shadow)
			m_shadow->enableShadowInvisible(m_fullyObscuredByShroud);
		if (m_terrainDecal)
			m_terrainDecal->enableShadowInvisible(m_fullyObscuredByShroud);

		doStartOrStopParticleSys();
	}
}

//-------------------------------------------------------------------------------------------------
static Bool isAnimationComplete(RenderObjClass* r)
{
	if (r && r->Class_ID() == RenderObjClass::CLASSID_HLOD)
	{
		HLodClass *hlod = static_cast<HLodClass*>(r);
		return hlod->Is_Animation_Complete();
	}

	return true;
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::adjustTransformMtx(Matrix3D& mtx) const
{
	const W3DModelDrawModuleData* d = getW3DModelDrawModuleData();

#ifdef CACHE_ATTACH_BONE
	const Vector3* offset = d->getAttachToDrawableBoneOffset(getDrawable());
	if (offset)
	{
		Vector3 tmp = mtx.Rotate_Vector(*offset);
		mtx.Adjust_X_Translation(tmp.X);
		mtx.Adjust_Y_Translation(tmp.Y);
		mtx.Adjust_Z_Translation(tmp.Z);
	}
#else
	if (d->m_attachToDrawableBone.isNotEmpty())
	{
		// override the mtx in this case. yes, call drawable, since the bone in question is 
		// likely to be in another module!
		Matrix3D boneMtx;
		if (getDrawable()->getCurrentWorldspaceClientBonePositions(d->m_attachToDrawableBone.str(), boneMtx))
		{
			mtx = boneMtx;
		}
		else
		{
			DEBUG_LOG(("m_attachToDrawableBone %s not found\n",getW3DModelDrawModuleData()->m_attachToDrawableBone.str()));
		}
	}
#endif

	if (m_curState->m_flags & (1<<ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT))
	{
		const Object *obj = getDrawable()->getObject();
		if (obj)
		{
			Real pct = obj->getConstructionPercent();
			if (pct >= 0.0f)
			{
				Real height = obj->getGeometryInfo().getMaxHeightAbovePosition();
				mtx.Translate_Z(-height + (height * pct / 100.0f));
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::doDrawModule(const Matrix3D* transformMtx)
{
	// update whether or not we should be animating.
	setPauseAnimation( !getDrawable()->getShouldAnimate(getW3DModelDrawModuleData()->m_animationsRequirePower) );

	Matrix3D scaledTransform;
	if (getDrawable()->getInstanceScale() != 1.0f)
	{	
		// do custom scaling of the W3D model.
		scaledTransform = *transformMtx;
		scaledTransform.Scale(getDrawable()->getInstanceScale());
		transformMtx = &scaledTransform;
		if (m_renderObject)
			m_renderObject->Set_ObjectScale(getDrawable()->getInstanceScale());
	}

	if (isAnimationComplete(m_renderObject))
	{
		if (m_curState != nullptr && m_nextState != nullptr)
		{
			//DEBUG_LOG(("transition %s is complete\n",m_curState->m_description.str()));
			const ModelConditionInfo* nextState = m_nextState;
			UnsignedInt nextDuration = m_nextStateAnimLoopDuration;
			m_nextState = nullptr;
			m_nextStateAnimLoopDuration = NO_NEXT_DURATION;
			setModelState(nextState);
			if (nextDuration != NO_NEXT_DURATION)
			{
				//DEBUG_LOG(("restoring pending duration of %d frames\n",nextDuration));
				setAnimationLoopDuration(nextDuration);
			}
		}
		
		if (m_renderObject &&
					m_curState != nullptr &&
					m_whichAnimInCurState != -1)
		{
			if (m_curState->m_animations[m_whichAnimInCurState].isIdleAnim())
			{
				//DEBUG_LOG(("randomly switching to new idle state!\n"));

				// state hasn't changed, if it's been awhile, switch the idle anim
				// (yes, that's right: pass curState for prevState)
				adjustAnimation(m_curState, -1.0);
			}
			else if (testFlagBit(m_curState->m_flags, RESTART_ANIM_WHEN_COMPLETE))
			{
				adjustAnimation(m_curState, -1.0);
			}
		}
	}

	adjustAnimSpeedToMovementSpeed();

	// set our position in the render object to our position of the drawable
	if (m_renderObject)
	{
		Matrix3D mtx = *transformMtx;
		adjustTransformMtx(mtx);
		m_renderObject->Set_Transform(mtx);
	}

	handleClientTurretPositioning();
	recalcBonesForClientParticleSystems();
  
  const W3DModelDrawModuleData *modData = getW3DModelDrawModuleData();
  if ( modData->m_particlesAttachedToAnimatedBones )
    updateBonesForClientParticleSystems();// LORENZEN ADDED THIS 
                                          // IT REPOSITIONS PARTICLESYSTEMS TO TSTAY IN SYNC WITH ANIMATED BONES
	
  handleClientRecoil();

}


//void W3DModelDraw::handleClientFlagPositioning()
//{
//	const ModelConditionInfo::TurretInfo& tur = m_curState->m_turrets[tslot];
//	Real turretAngle = TheGlobalData->m_downwindAngle;
//	if (tur.m_flagAngleBone )
//	{
//		Matrix3D turretXfrm(1);
//		turretXfrm.Rotate_Z(turretAngle);
//		if (m_renderObject)
//		{
//			m_renderObject->Capture_Bone( tur.m_turretAngleBone );
//			m_renderObject->Control_Bone( tur.m_turretAngleBone, turretXfrm );	
//		}
//	}
//}


//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setTerrainDecal(TerrainDecalType type)
{
	if (m_terrainDecal)
		m_terrainDecal->release();

	m_terrainDecal = nullptr;

	if (type == TERRAIN_DECAL_NONE || type >= TERRAIN_DECAL_MAX)
		//turning off decals on this object. (or bad value.)
		return;

	const ThingTemplate *tmplate=getDrawable()->getTemplate();

	//create a new terrain decal
	Shadow::ShadowTypeInfo decalInfo;
	decalInfo.allowUpdates = FALSE;	//shadow image will never update
	decalInfo.allowWorldAlign = TRUE;	//shadow image will wrap around world objects
	decalInfo.m_type = SHADOW_ALPHA_DECAL;

	//decalInfo.m_type = SHADOW_ADDITIVE_DECAL;//temporary kluge to test graphics

	if (type == TERRAIN_DECAL_SHADOW_TEXTURE)
		safe_strcpy(decalInfo.m_ShadowName,tmplate->getShadowTextureName().str());
	else
		safe_strcpy(decalInfo.m_ShadowName,TerrainDecalTextureName[type]);
	decalInfo.m_sizeX = tmplate->getShadowSizeX();
	decalInfo.m_sizeY = tmplate->getShadowSizeY();
	decalInfo.m_offsetX = tmplate->getShadowOffsetX();
	decalInfo.m_offsetY = tmplate->getShadowOffsetY();
	if (TheProjectedShadowManager)
		m_terrainDecal = TheProjectedShadowManager->addDecal(m_renderObject,&decalInfo);
	if (m_terrainDecal)
	{	m_terrainDecal->enableShadowInvisible(m_fullyObscuredByShroud);
		m_terrainDecal->enableShadowRender(m_shadowEnabled);
	}
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setTerrainDecalSize(Real x, Real y)
{
	if (m_terrainDecal)
	{
		m_terrainDecal->setSize(x,y);
	}
}
//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setTerrainDecalOpacity(Real o)
{
	if (m_terrainDecal)
	{
		m_terrainDecal->setOpacity(static_cast<Int>(255.0f * o));
	}
}


//-------------------------------------------------------------------------------------------------
void W3DModelDraw::nukeCurrentRender(Matrix3D* xform)
{
	// this needs to be "dirtied" so that the new pausing of the animation will be triggered.
	m_pauseAnimation = false;

	// changing geometry, so we need to remove shadow if present
	if (m_shadow)
		m_shadow->release();
	m_shadow = nullptr;

	if(m_terrainDecal)
		m_terrainDecal->release();
	m_terrainDecal = nullptr;

	// remove existing render object from the scene
	if (m_renderObject)
	{
		// save the transform for the new model
		if (xform)
			*xform = m_renderObject->Get_Transform();
		W3DDisplay::m_3DScene->Remove_Render_Object(m_renderObject);
		REF_PTR_RELEASE(m_renderObject);
		m_renderObject = nullptr;
	}
	else
	{
		if (xform)
		{	
			*xform = *getDrawable()->getTransformMatrix();
		}
	}
}

//-------------------------------------------------------------------------------------------------
static Bool turretNamesDiffer(const ModelConditionInfo* a, const ModelConditionInfo* b)
{
	if (!(a->m_validStuff & ModelConditionInfo::TURRETS_VALID) || !(b->m_validStuff & ModelConditionInfo::TURRETS_VALID))
	{
		return true;
	}
	for (int i = 0; i < MAX_TURRETS; ++i)
		if (a->m_turrets[i].m_turretAngleNameKey != b->m_turrets[i].m_turretAngleNameKey || 
				a->m_turrets[i].m_turretPitchNameKey != b->m_turrets[i].m_turretPitchNameKey)
			return true;

	return false;
}

//-------------------------------------------------------------------------------------------------
/** Change the model for this drawable. If color is non-zero, it will also apply team color to the
	model */
//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setModelState(const ModelConditionInfo* newState)
{
	DEBUG_ASSERTCRASH(newState, ("invalid state in W3DModelDraw::setModelState\n")); 

#ifdef DEBUG_OBJECT_ID_EXISTS
	if (getDrawable() && getDrawable()->getObject() && getDrawable()->getObject()->getID() == TheObjectIDToDebug)
	{
		DEBUG_LOG(("REQUEST switching to state %s for obj %s %d\n",newState->m_description.str(),getDrawable()->getObject()->getTemplate()->getName().str(),getDrawable()->getObject()->getID()));
	}
#endif
	const ModelConditionInfo* nextState = nullptr;
	if (m_curState != nullptr && newState != nullptr)
	{ 
		// if the requested state is the current state (and nothing is pending),
		// or if the requested state is pending, just punt.
		//
		// note that if the requested state matches the current state AND
		// we also have a pending state, then we nuke both of 'em. This sounds
		// weird, but is deliberate: nextState and newState will always be "real" states (or null),
		// leaving us these possibilities:
		//
		// -- curState is a transition state
		//		--in this case, new state should take precedence (unless there's a wait-to-finish marking, 
		//		which would be handled by the else clause)
		//
		// -- curState is a real state, nextState is a real state with a wait-to-finish clause
		//		-- if curState == newState, presume that we should restart curState, thus punting thepending nextState
		//		-- if curState != newState, presume we should switch to it, thus punting curState and nextState
		// 
		// -- curState is a real state, nextState is a real state without a wait-to-finish clause
		//		-- should be impossible!

		if ((m_curState == newState && m_nextState == nullptr) ||
				(m_curState != nullptr && m_nextState == newState))
		{
#ifdef DEBUG_OBJECT_ID_EXISTS
			if (getDrawable() && getDrawable()->getObject() && getDrawable()->getObject()->getID() == TheObjectIDToDebug)
			{
				DEBUG_LOG(("IGNORE duplicate state %s for obj %s %d\n",newState->m_description.str(),getDrawable()->getObject()->getTemplate()->getName().str(),getDrawable()->getObject()->getID()));
			}
#endif
			// I don't think he'll be interested...
			// he's already got one, you see
			return;
		} 
		// check for allow-to-finish implicit transitions
		else if (newState != m_curState &&
							newState->m_allowToFinishKey != NAMEKEY_INVALID &&
							newState->m_allowToFinishKey == m_curState->m_transitionKey &&
							m_renderObject &&
							!isAnimationComplete(m_renderObject))
		{
#ifdef DEBUG_OBJECT_ID_EXISTS
			if (getDrawable() && getDrawable()->getObject() && getDrawable()->getObject()->getID() == TheObjectIDToDebug)
			{
				DEBUG_LOG(("ALLOW_TO_FINISH state %s for obj %s %d\n",newState->m_description.str(),getDrawable()->getObject()->getTemplate()->getName().str(),getDrawable()->getObject()->getID()));
			}
#endif
			m_nextState = newState;
			m_nextStateAnimLoopDuration = NO_NEXT_DURATION;
			return;
		}
		// check for a normal->normal state transition
		else if (newState != m_curState && 
					m_curState->m_transitionKey != NAMEKEY_INVALID &&
					newState->m_transitionKey != NAMEKEY_INVALID)
		{
			TransitionSig sig = buildTransitionSig(m_curState->m_transitionKey, newState->m_transitionKey);
			const ModelConditionInfo* transState = findTransitionForSig(sig);
			if (transState != nullptr)
			{
#ifdef DEBUG_OBJECT_ID_EXISTS
				if (getDrawable() && getDrawable()->getObject() && getDrawable()->getObject()->getID() == TheObjectIDToDebug)
				{
					DEBUG_LOG(("using TRANSITION state %s before requested state %s for obj %s %d\n",transState->m_description.str(),newState->m_description.str(),getDrawable()->getObject()->getTemplate()->getName().str(),getDrawable()->getObject()->getID()));
				}
#endif
				nextState = newState;
				newState = transState;
			}
		}
	}
	
	// get this here, before we change anything... we'll need it to pass to adjustAnimation (srj)
	Real prevAnimFraction = getCurrentAnimFraction();

	//
	// Set up any particle effects for the new model.  Also stop the old ones, note that we
	// will never allow the placement of new ones if the status bit is set that tells us
	// we should not automatically create particle systems for the model condition states
	//
	if( getDrawable()->testDrawableStatus( DRAWABLE_STATUS_NO_STATE_PARTICLES ) == FALSE )
		m_needRecalcBoneParticleSystems = true;

	// always stop particle systems now
	stopClientParticleSystems();

	// and always hide muzzle flashes, in case modelstate and model have crossed at the same time
	hideAllMuzzleFlashes(newState, m_renderObject);

	// note that different states might use the same model; for these, don't go thru the
	// expense of creating a new render-object. (exception: if color is changing, or subobjs are changing,
	// or a few other things...)
	if (m_curState == nullptr || 
			newState->m_modelName != m_curState->m_modelName || 
			turretNamesDiffer(newState, m_curState)
			// srj sez: I'm not sure why we want to do the "hard stuff" if we have projectile bones; I think
			// it is a holdover from days gone by when bones were handled quite differently, rather than being cached.
			// but doing this hard stuff is a lot more work, and I think it's unnecessary, so let's remove this.
			//|| (newState->m_validStuff & ModelConditionInfo::HAS_PROJECTILE_BONES)
		)
	{
		Matrix3D transform;
		nukeCurrentRender(&transform);
		Drawable* draw = getDrawable();

		// create a new render object and set into drawable
		if (newState->m_modelName.isEmpty())
		{
			m_renderObject = nullptr;
		}
		else
		{
			m_renderObject = W3DDisplay::m_assetManager->Create_Render_Obj(newState->m_modelName.str(), draw->getScale(), m_hexColor);
			DEBUG_ASSERTCRASH(m_renderObject, ("*** ASSET ERROR: Model %s not found!\n",newState->m_modelName.str()));
		}

		//BONEPOS_LOG(("validateStuff() from within W3DModelDraw::setModelState()\n"));
		//BONEPOS_DUMPREAL(draw->getScale());

		newState->validateStuff(m_renderObject, draw->getScale(), getW3DModelDrawModuleData()->m_extraPublicBones);
		// ensure that any muzzle flashes from the *new* state, start out hidden...
//		hideAllMuzzleFlashes(newState, m_renderObject);//moved to above
		rebuildWeaponRecoilInfo(newState);
		newState->validateHideShowSubObjs(m_renderObject, getDrawable()->getTemplate()->getName().str());
		doHideShowSubObjs(&newState->m_hideShowVec);

#if defined(_DEBUG)	//art wants to see buildings without flags as a test.
		if (TheGlobalData->m_hideGarrisonFlags && draw->isKindOf(KINDOF_STRUCTURE))
			hideGarrisonFlags(TRUE);
#endif

		const ThingTemplate *tmplate = draw->getTemplate();

		// set up tracks, if not already set.
		if (m_renderObject &&
				TheGlobalData->m_makeTrackMarks &&  
				!m_trackRenderObject && 
				TheTerrainTracksRenderObjClassSystem != nullptr && 
				!getW3DModelDrawModuleData()->m_trackFile.isEmpty())
		{
			m_trackRenderObject = TheTerrainTracksRenderObjClassSystem->bindTrack(m_renderObject, 1.0f*MAP_XY_FACTOR, getW3DModelDrawModuleData()->m_trackFile.str());
			if (draw && m_trackRenderObject)
				m_trackRenderObject->setOwnerDrawable(draw);
		}

		// set up shadows
		if (m_renderObject && TheW3DShadowManager && tmplate->getShadowType() != SHADOW_NONE)
		{	
			Shadow::ShadowTypeInfo shadowInfo;
			safe_strcpy(shadowInfo.m_ShadowName, tmplate->getShadowTextureName().str());
			DEBUG_ASSERTCRASH(shadowInfo.m_ShadowName[0] != '\0', ("this should be validated in ThingTemplate now"));
			shadowInfo.allowUpdates			= FALSE;		//shadow image will never update
			shadowInfo.allowWorldAlign	= TRUE;	//shadow image will wrap around world objects
			shadowInfo.m_type						= static_cast<ShadowType>(tmplate->getShadowType());
			shadowInfo.m_sizeX					= tmplate->getShadowSizeX();
			shadowInfo.m_sizeY					= tmplate->getShadowSizeY();
			shadowInfo.m_offsetX				= tmplate->getShadowOffsetX();
			shadowInfo.m_offsetY				= tmplate->getShadowOffsetY();
  			m_shadow = TheW3DShadowManager->addShadow(m_renderObject, &shadowInfo, draw);
			if (m_shadow)
			{	m_shadow->enableShadowInvisible(m_fullyObscuredByShroud);
				m_shadow->enableShadowRender(m_shadowEnabled);
			}
		}

		if( m_renderObject )
		{
			// set collision type for render object.  Used by WW3D2 collision code.
			if (tmplate->isKindOf(KINDOF_SELECTABLE))  
			{
				m_renderObject->Set_Collision_Type( PICK_TYPE_SELECTABLE );
			}

			if( tmplate->isKindOf( KINDOF_SHRUBBERY ))
			{
				m_renderObject->Set_Collision_Type( PICK_TYPE_SHRUBBERY );
			}
			if( tmplate->isKindOf( KINDOF_MINE ))
			{
				m_renderObject->Set_Collision_Type( PICK_TYPE_MINES );
			}
			if( tmplate->isKindOf( KINDOF_FORCEATTACKABLE ))
			{
				m_renderObject->Set_Collision_Type( PICK_TYPE_FORCEATTACKABLE );
			}
			if( tmplate->isKindOf( KINDOF_CLICK_THROUGH ))
			{
				m_renderObject->Set_Collision_Type( 0 );
			}
			
			Object *obj = draw->getObject();
 			if( obj )
   		{

				// for non bridge objects we adjust some collision types
				if( obj->isKindOf( KINDOF_BRIDGE ) == FALSE &&
						obj->isKindOf( KINDOF_BRIDGE_TOWER ) == FALSE )
				{

					if( obj->isKindOf( KINDOF_STRUCTURE ) && draw->getModelConditionFlags().test( MODELCONDITION_RUBBLE ) )
					{
	 					//A dead building, -- don't allow the user to click on rubble! Treat it as a location instead.
	   				m_renderObject->Set_Collision_Type( 0 );
					}
					else if( obj->isEffectivelyDead() )
					{
 						//A dead object, -- don't allow the user to click on rubble/hulks! Treat it as a location instead.
	   				m_renderObject->Set_Collision_Type( 0 );
					}
				}
   		}

			// add render object to our scene
			W3DDisplay::m_3DScene->Add_Render_Object(m_renderObject);

			// tie in our drawable as the user data pointer in the render object
			m_renderObject->Set_User_Data(draw->getDrawableInfo());
	
			setTerrainDecal(draw->getTerrainDecalType());

			//We created a new render object so we need to preserve the visibility state
			//of the previous render object.
			if (draw->isDrawableEffectivelyHidden())
			{
				m_renderObject->Set_Hidden(TRUE);
				if (m_shadow)
					m_shadow->enableShadowRender(FALSE);
				m_shadowEnabled = FALSE;
			}
			
			//
			// set the transform for the new model to that we saved before, we do this so that the
			// model transition is smooth and will immediately appear at the same orientation and location
			// as the previous one
			//
			m_renderObject->Set_Transform(transform);
			onRenderObjRecreated(); 
		}
	} 
	else 
	{

		//BONEPOS_LOG(("validateStuff() from within W3DModelDraw::setModelState()\n"));
		//BONEPOS_DUMPREAL(getDrawable()->getScale());

		newState->validateStuff(m_renderObject, getDrawable()->getScale(), getW3DModelDrawModuleData()->m_extraPublicBones);
		rebuildWeaponRecoilInfo(newState);

		// ensure that any muzzle flashes from the *previous* state, are hidden...
//		hideAllMuzzleFlashes(m_curState, m_renderObject);// moved to above
		
		newState->validateHideShowSubObjs(m_renderObject, getDrawable()->getTemplate()->getName().str());
		doHideShowSubObjs(&newState->m_hideShowVec);
	}
	hideAllHeadlights(m_hideHeadlights);

	const ModelConditionInfo* prevState = m_curState;	// save, to pass to adjustAnimation (could be null)
	m_curState = newState;
	m_nextState = nextState;
	m_nextStateAnimLoopDuration = NO_NEXT_DURATION;
	adjustAnimation(prevState, prevAnimFraction);
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::replaceModelConditionState(const ModelConditionFlags& c)
{
	m_hideHeadlights = c.test(MODELCONDITION_NIGHT) ? false : true;

	const ModelConditionInfo* info = findBestInfo(c);
	if (info)
		setModelState(info);

	hideAllHeadlights(m_hideHeadlights);
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setSelectable(Bool selectable)
{
	// set collision type for render object.  Used by WW3D2 collision code.
	if( m_renderObject )
	{
		int current = m_renderObject->Get_Collision_Type();
		if (selectable) {
			current |= PICK_TYPE_SELECTABLE;
		} else {
			current &= ~PICK_TYPE_SELECTABLE;
		}
		m_renderObject->Set_Collision_Type(current);
	}  // end if
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::replaceIndicatorColor(Color color)
{
	if (!getW3DModelDrawModuleData()->m_okToChangeModelColor)
		return;

	if (getRenderObject())
	{	
		Int newColor = (color == 0) ? 0 : (color | 0xFF000000);
		if (newColor != m_hexColor)
		{
			m_hexColor = newColor;

			// set these to NULL to force a regen of everything in setModelState.
			const ModelConditionInfo* tmp = m_curState;
			m_curState = nullptr;
			m_nextState = nullptr;
			m_nextStateAnimLoopDuration = NO_NEXT_DURATION;
			setModelState(tmp);
		}
	}
}

//-------------------------------------------------------------------------------------------------
// this method must ONLY be called from the client, NEVER From the logic, not even indirectly.
Bool W3DModelDraw::clientOnly_getRenderObjInfo(Coord3D* pos, Real* boundingSphereRadius, Matrix3D* transform) const
{
	if (!m_renderObject)
		return false;

	Vector3 objPos = m_renderObject->Get_Position();	//get position of object
	pos->x = objPos.X;
	pos->y = objPos.Y;
	pos->z = objPos.Z;

	*transform = m_renderObject->Get_Transform();
	*boundingSphereRadius = m_renderObject->Get_Bounding_Sphere().Radius;

	return true;
}


//-------------------------------------------------------------------------------------------------
// (gth) C&C3 Added this accessor for the bounding box of a render object in a W3DModelDraw module
// this method must ONLY be called from the client, NEVER From the logic, not even indirectly.
Bool W3DModelDraw::clientOnly_getRenderObjBoundBox(OBBoxClass * boundbox) const
{
	if (!m_renderObject)
		return false;

	AABoxClass aabox;
	m_renderObject->Get_Obj_Space_Bounding_Box(aabox);

	Matrix3D tm = m_renderObject->Get_Transform();

	// build an OBB for this AAB,transform
	OBBoxClass box0(aabox.Center,aabox.Extent);
	OBBoxClass::Transform(tm,box0,boundbox);

	return true;
}


//-------------------------------------------------------------------------------------------------
void W3DModelDraw::reactToTransformChange( const Matrix3D* oldMtx, 
																					 const Coord3D* oldPos, 
																					 Real oldAngle )
{

	// set the position of our render object
	if( m_renderObject )
	{
		Matrix3D mtx = *getDrawable()->getTransformMatrix();
		adjustTransformMtx(mtx);
		m_renderObject->Set_Transform(mtx);
	}

	if (m_trackRenderObject) 
	{
		Object *obj = getDrawable()->getObject();
		const Coord3D* pos = getDrawable()->getPosition();

		if ( m_fullyObscuredByShroud || obj->testStatus( OBJECT_STATUS_STEALTHED ) == TRUE )
		{
				m_trackRenderObject->addCapEdgeToTrack(pos->x, pos->y);
		}
		else
		{
			if (obj && obj->isSignificantlyAboveTerrain())
			{
				m_trackRenderObject->setAirborne();
			} 
			m_trackRenderObject->addEdgeToTrack(pos->x, pos->y);
		}
	}
} 

//-------------------------------------------------------------------------------------------------
const ModelConditionInfo* W3DModelDraw::findBestInfo(const ModelConditionFlags& c) const
{
	return getW3DModelDrawModuleData()->findBestInfo(c);
}

//-------------------------------------------------------------------------------------------------
/** Preload any assets for the time of day requested */
//-------------------------------------------------------------------------------------------------
void W3DModelDraw::preloadAssets( TimeOfDay timeOfDay )
{
	const W3DModelDrawModuleData *modData = getW3DModelDrawModuleData();

	if( modData )
		modData->preloadAssets( timeOfDay, getDrawable()->getScale() );
}

//-------------------------------------------------------------------------------------------------
Bool W3DModelDraw::isVisible() const
{
	return (m_renderObject && m_renderObject->Is_Really_Visible());
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

