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


// FILE: W3DShadow.cpp ///////////////////////////////////////////////////////////
//
// Real time shadow representations
//
// Author: Mark Wilczynski, February 2002
//
//

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "always.h"
#include "GameClient/View.h"
#include "WW3D2/Scene/camera.h"
#include "WW3D2/Scene/light.h"
#include "WW3D2/Common/renderdevice.h"
#include "WW3D2/Mesh/hlod.h"
#include "WW3D2/Mesh/mesh.h"
#include "WW3D2/Mesh/meshmdl.h"
#include "Lib/BaseType.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "Common/GlobalData.h"
#include "W3DDevice/GameClient/W3DVolumetricShadow.h"
#include "W3DDevice/GameClient/W3DProjectedShadow.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "WW3D2/System/statistics.h"
#include "Common/Debug.h"
#include "Common/PerfTimer.h"

constexpr auto SUN_DISTANCE_FROM_GROUND = 10000.0f;	//distance of sun (our only light source).

// Global Variables and Functions /////////////////////////////////////////////
const FrustumClass *shadowCameraFrustum;

Vector3 LightPosWorld[ MAX_SHADOW_LIGHTS ] =
{

	Vector3( 94.0161f, 50.499f, 200.0f)
};

//DECLARE_PERF_TIMER(shadowsRender)
void DoShadows(RenderInfoClass & rinfo, Bool stencilPass)
{
	//USE_PERF_TIMER(shadowsRender)
	shadowCameraFrustum=&rinfo.Camera.Get_Frustum();
	Int projectionCount=0;

	//Projected shadows render first because they may fill the stencil buffer
	//which will be used by the shadow volumes
	// With shadow maps the model projected shadows in W3DProjectedShadowManager::renderShadows and the
	// stencil volumes here are skipped; decals stay.
	if (stencilPass == FALSE  && TheW3DProjectedShadowManager)
	{
			if (TheW3DShadowManager->isShadowScene())
				projectionCount=TheW3DProjectedShadowManager->renderShadows(rinfo);
	}

	if (stencilPass == TRUE && TheW3DVolumetricShadowManager)
	{

//		TheW3DShadowManager->loadTerrainShadows();

			//This function gets called many times by the W3D renderer
			//so we use this flag to make sure shadows rendered only once per frame.
			if (TheW3DShadowManager->isShadowScene() && !RenderDevice::Shadow_Maps_Active())
				TheW3DVolumetricShadowManager->renderShadows(projectionCount);
	}
	if (TheW3DShadowManager && stencilPass)	//reset so no more shadow processing this frame.
		TheW3DShadowManager->queueShadows(FALSE);

}
	
// At the start of the opaque scene pass (RTS3DScene::Render) passes sun direction, shadow color and
// the map height range to the render device. The backend uses them for shadow-mapped sun shadows
// when "-d3d11Effects shadowmap" is selected; otherwise this has no effect.
void BeginShadowMaps(RenderInfoClass & rinfo)
{
	if (!TheW3DShadowManager || !TheTerrainRenderObject)
		return;
	if (!(RenderDevice::Get_Effect_Settings().flags & RENDER_EFFECT_SHADOW_MAP))
		return;
	RenderShadowParams params;
	memset(&params, 0, sizeof(params));
	const Vector3 &sun = TheW3DShadowManager->getLightPosWorld(0);	// sun is far away, so this is the direction to the sun
	params.sun_direction[0] = sun.X;
	params.sun_direction[1] = sun.Y;
	params.sun_direction[2] = sun.Z;
	// Same color the stencil shadows multiply the image with (renderStencilShadows), so shadowed
	// areas stay equally dark.
	const UnsignedInt color = TheW3DShadowManager->getShadowColor();
	params.shadow_color[0] = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
	params.shadow_color[1] = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
	params.shadow_color[2] = static_cast<float>(color & 0xFF) / 255.0f;
	params.ground_min_z = TheTerrainRenderObject->getMinHeight();
	params.ground_max_z = TheTerrainRenderObject->getMaxHeight();
	RenderDevice::Shadow_Scene_Begin(params);
}

W3DShadowManager::W3DShadowManager( void )
{
	DEBUG_ASSERTCRASH(TheW3DVolumetricShadowManager == nullptr && TheW3DProjectedShadowManager == nullptr,
		("Creating new shadow managers without deleting old ones"));

	m_shadowColor = 0x7fa0a0a0;
	m_isShadowScene = FALSE;
	m_stencilShadowMask = 0;	//all bits can be used for storing shadows.

	Vector3 lightRay(-TheGlobalData->m_terrainLightPos[0].x,
		-TheGlobalData->m_terrainLightPos[0].y, -TheGlobalData->m_terrainLightPos[0].z);
	lightRay.Normalize();

	LightPosWorld[0]=lightRay*SUN_DISTANCE_FROM_GROUND;

	::Engine::context().w3DVolumetricShadowManager = NEW W3DVolumetricShadowManager;
	::Engine::context().projectedShadowManager = ::Engine::context().w3DProjectedShadowManager = NEW W3DProjectedShadowManager;
}

W3DShadowManager::~W3DShadowManager( void )
{
	delete TheW3DVolumetricShadowManager;
	::Engine::context().w3DVolumetricShadowManager = nullptr;
	delete TheW3DProjectedShadowManager;
	::Engine::context().projectedShadowManager = ::Engine::context().w3DProjectedShadowManager = nullptr;
}

/** Do one-time initilalization of shadow systems that need to be
active for full duration of game*/
Bool W3DShadowManager::init( void )
{
	Bool result=TRUE;

	if	(TheW3DVolumetricShadowManager && TheW3DVolumetricShadowManager->init())
	{
		if (TheW3DVolumetricShadowManager->ReAcquireResources())
			result = TRUE;
	}
	if ( TheW3DProjectedShadowManager && TheW3DProjectedShadowManager->init())
	{
		if (TheW3DProjectedShadowManager->ReAcquireResources())
			result = TRUE;
	}

	return result;
}

/** Do per-map reset.  This frees up shadows from all objects since
they may not exist on the next map*/
void W3DShadowManager::Reset( void )
{

	if (TheW3DVolumetricShadowManager)
		TheW3DVolumetricShadowManager->reset();
	if (TheW3DProjectedShadowManager)
		TheW3DProjectedShadowManager->reset();
}

Bool W3DShadowManager::ReAcquireResources()
{
	Bool result = TRUE;

	if (TheW3DVolumetricShadowManager && !TheW3DVolumetricShadowManager->ReAcquireResources())
		result = FALSE;
	if (TheW3DProjectedShadowManager && !TheW3DProjectedShadowManager->ReAcquireResources())
		result = FALSE;

	return result;
}

void W3DShadowManager::ReleaseResources(void)
{
	if (TheW3DVolumetricShadowManager)
		TheW3DVolumetricShadowManager->ReleaseResources();
	if (TheW3DProjectedShadowManager)
		TheW3DProjectedShadowManager->ReleaseResources();
}

Shadow *W3DShadowManager::addShadow( RenderObjClass *robj, Shadow::ShadowTypeInfo *shadowInfo, Drawable *draw)
{
	ShadowType type = SHADOW_VOLUME;

	if (shadowInfo)
		type = shadowInfo->m_type;

	switch(type)
	{
		case	SHADOW_VOLUME:
			if (TheW3DVolumetricShadowManager)
				return static_cast<Shadow *>(TheW3DVolumetricShadowManager->addShadow(robj, shadowInfo, draw));
			break;
		case	SHADOW_PROJECTION:
		case	SHADOW_DECAL:
			if (TheW3DProjectedShadowManager)
				return static_cast<Shadow *>(TheW3DProjectedShadowManager->addShadow(robj, shadowInfo, draw));
			break;
		default:
			return nullptr;
	}
		
	return nullptr;
}

void W3DShadowManager::removeShadow(Shadow *shadow)
{
	shadow->release();
}

void W3DShadowManager::removeAllShadows(void)
{
	if (TheW3DVolumetricShadowManager)
		TheW3DVolumetricShadowManager->removeAllShadows();
	if (TheW3DProjectedShadowManager)
		TheW3DProjectedShadowManager->removeAllShadows();
}

/**Force update of all shadows even when light source and object have not moved*/
void W3DShadowManager::invalidateCachedLightPositions(void)
{
	if (TheW3DVolumetricShadowManager)
		TheW3DVolumetricShadowManager->invalidateCachedLightPositions();
	if (TheW3DProjectedShadowManager)
		TheW3DProjectedShadowManager->invalidateCachedLightPositions();
}

Vector3 &W3DShadowManager::getLightPosWorld(Int lightIndex)
{
	return LightPosWorld[lightIndex];
}

void W3DShadowManager::setLightPosition(Int lightIndex, Real x, Real y, Real z)
{
	if (lightIndex != 0)
		return;	///@todo: Add support for multiple lights

	LightPosWorld[lightIndex]=Vector3(x,y,z);
}

void W3DShadowManager::setTimeOfDay(TimeOfDay tod)
{
	//Ray to light source
	const GlobalData::TerrainLighting *ol=&TheGlobalData->m_terrainObjectsLighting[tod][0];

	Vector3 lightRay(-ol->lightPos.x,-ol->lightPos.y,-ol->lightPos.z);

	lightRay.Normalize();
	lightRay *= SUN_DISTANCE_FROM_GROUND;

	setLightPosition(0, lightRay.X, lightRay.Y, lightRay.Z);
}
