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

// FILE: W3DShaderManager.cpp ////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: W3DShaderManager.cpp
//
// Created:   Mark Wilczynski, August 2001
//
// Desc:      Perform tests on currently selected WW3D/D3D device to determine
//			  which of our rendering features are supported.  The system allows
//			  setting up a few custom shaders that are selected based on video
//			  card features.
//
//			  To add a new shader to the system:
//			  0) Add your shader to the ShaderTypes enum
//			  1) Create shader using W3DShaderInterface
//			  2) Repeat step 1 for any alternate shaders
//			  3) Create list of alternate shaders sorted by order of preference.
//				 The first shader which passes hardware validation will be selected.
//			  4) Add list from step 3) to MasterShaderList[].
//
//-----------------------------------------------------------------------------

#include "WW3D2/Common/renderdevice.h"
#include "WW3D2/Common/renderpieces.h"
#include "assetmgr.h"
#include "Lib/BaseType.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DWater.h"	// destination alpha clear value
#include "W3DDevice/GameClient/W3DShroud.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DCustomScene.h"
#include "W3DDevice/GameClient/W3DSmudge.h"
#include "GameClient/View.h"
#include "GameClient/CommandXlat.h"
#include "GameClient/Display.h"
#include "GameClient/Water.h"
#include "GameLogic/GameLogic.h"
#include "Common/GlobalData.h"
#include "Common/GameLOD.h"
#include "WW3D2/System/wwd3dmath.h"	// replacement for the D3DX math helpers
#include "WW3D2/Common/rendercaps.h"
#include "Common/GameLOD.h"
#include "GameClient/GameClient.h"	// GameClient_IsRenderOnlyFrame()
#include "Lib/SafeString.h"

// Turn this on to turn off pixel shaders. jba[4/3/2003]
#define do_not_DISABLE_PIXEL_SHADERS 1

/** Interface definition for custom shaders we define in our app.  These shaders can perform more complex
	operations than those allowed in the WW3D2 shader system.
*/
class W3DShaderInterface
{
public:
	Int getNumPasses(void) {return m_numPasses;};	///<return number of passes needed for this shader
	virtual Int set(Int pass) {return TRUE;};		///<setup shader for the specified rendering pass.
	 ///do any custom resetting necessary to bring W3D in sync.
	virtual void reset(void) {
		ShaderClass::Invalidate();
		RenderDevice::Set_Device_Texture(0, nullptr);
		RenderDevice::Set_Device_Texture(1, nullptr);};
	virtual Int init(void) = 0;			///<perform any one time initialization and validation
	virtual Int shutdown(void) { return TRUE;};			///<release resources used by shader
protected:
	Int m_numPasses;						///<number of passes to complete shader
};

//this table will contain custom versions of each shader tuned for specific video card and user options.
static W3DFilterInterface *W3DFilters[FT_MAX];
static W3DShaderInterface *W3DShaders[W3DShaderManager::ST_MAX];
static Int W3DShadersPassCount[W3DShaderManager::ST_MAX];	//number of passes for each of the above shaders
TextureClass *W3DShaderManager::m_Textures[8];
W3DShaderManager::ShaderTypes W3DShaderManager::m_currentShader;
FilterTypes W3DShaderManager::m_currentFilter=FT_NULL_FILTER; ///< Last filter that was set.
Int W3DShaderManager::m_currentShaderPass;

Bool W3DShaderManager::m_renderingToTexture = false;
WW3DSurfaceHandle W3DShaderManager::m_oldRenderSurface=nullptr;	///<previous render target
WW3DTextureHandle W3DShaderManager::m_renderTexture=nullptr;		///<texture into which rendering will be redirected.
WW3DSurfaceHandle W3DShaderManager::m_newRenderSurface=nullptr;	///<new render target inside m_renderTexture
WW3DSurfaceHandle W3DShaderManager::m_oldDepthSurface=nullptr;	///<previous depth buffer surface
/*===========================================================================================*/
/*=========      Screen Shaders	=============================================================*/
/*===========================================================================================*/

class ScreenDefaultFilter : public W3DFilterInterface
{
public:
	virtual Int init(void) override;			///<perform any one time initialization and validation
	virtual Bool preRender(Bool &skipRender, CustomScenePassModes &scenePassMode) override; ///< Set up at start of render.  Only applies to screen filter shaders.
	virtual Bool postRender(enum FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender) override; ///< Called after render.  Only applies to screen filter shaders.
	virtual Bool setup(enum FilterModes mode) override {return true;} ///< Called when the filter is started, one time before the first prerender.
protected:
	virtual Int set(enum FilterModes mode) override;		///<setup shader for the specified rendering pass.
	virtual void reset(void) override;		///<do any custom resetting necessary to bring W3D in sync.
};

ScreenDefaultFilter screenDefaultFilter;

///Default filter that just renders screen to off-screen texture and then copies it the the screen.
///Useful because we added some full-time unit effects (microwave tank smudge) to Generals MD that need access
///to the background as a texture.  This filter makes that texture always available for these effects.
W3DFilterInterface *ScreenDefaultFilterList[]=
{
	&screenDefaultFilter,
	nullptr
};

Int ScreenDefaultFilter::init(void)
{
	if (!W3DShaderManager::canRenderToTexture()) {
		// Have to be able to render to texture.
		return FALSE;
	}

	W3DFilters[FT_VIEW_DEFAULT]=&screenDefaultFilter;

	return TRUE;
}

Bool ScreenDefaultFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	//Right now this filter is only used for smudges, so don't bother if none are present.
	if (TheSmudgeManager)
	{	if ((static_cast<W3DSmudgeManager *>(TheSmudgeManager))->getSmudgeCountLastFrame() == 0)
			return FALSE;
	}
	W3DShaderManager::startRenderToTexture();
	return true;
}

Bool ScreenDefaultFilter::postRender(enum FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
	WW3DTextureHandle tex =	W3DShaderManager::endRenderToTexture();
	DEBUG_ASSERTCRASH(tex, ("Require rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	// Screen-space quad drawn with RenderDevice::Draw_Screen_Quad_UP.
	struct _TRANS_LIT_TEX_VERTEX {
		Vector3 p;	// pixel coordinates
		DWORD color;   // diffuse color
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	RenderDevice::Set_Device_Texture(0,tex);	//previously rendered frame inside this texture
	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

	//bottom right
	v[0].p = Vector3( static_cast<float>(xpos+width), static_cast<float>(ypos+height), 0.0f );
	v[0].u = static_cast<Real>(xpos+width)/static_cast<Real>(TheDisplay->getWidth());	v[0].v = static_cast<Real>(ypos+height)/static_cast<Real>(TheDisplay->getHeight());
	//top right
	v[1].p = Vector3( static_cast<float>(xpos+width), static_cast<float>(ypos), 0.0f );
	v[1].u = static_cast<Real>(xpos+width)/static_cast<Real>(TheDisplay->getWidth());	v[1].v = static_cast<Real>(ypos)/static_cast<Real>(TheDisplay->getHeight());
	//bottom left
	v[2].p = Vector3(  static_cast<float>(xpos), static_cast<float>(ypos+height), 0.0f );
	v[2].u = static_cast<Real>(xpos)/static_cast<Real>(TheDisplay->getWidth());	v[2].v = static_cast<Real>(ypos+height)/static_cast<Real>(TheDisplay->getHeight());
	//top left
	v[3].p = Vector3(  static_cast<float>(xpos),  static_cast<float>(ypos), 0.0f );
	v[3].u = static_cast<Real>(xpos)/static_cast<Real>(TheDisplay->getWidth());	v[3].v = static_cast<Real>(ypos)/static_cast<Real>(TheDisplay->getHeight());
	v[0].color = 0xffffffff;
	v[1].color = 0xffffffff;
	v[2].color = 0xffffffff;
	v[3].color = 0xffffffff;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	RenderDevice::Draw_Screen_Quad_UP(v, sizeof(_TRANS_LIT_TEX_VERTEX), WW3D_FVF_XYZ | WW3D_FVF_DIFFUSE | WW3D_FVF_TEX1);

	reset();
	return true;
}

Int ScreenDefaultFilter::set(enum FilterModes mode)
{
	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	RenderDevice::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
	RenderDevice::Set_Shader(ShaderClass::_PresetOpaqueShader);
	RenderDevice::Set_Texture(0,nullptr);
	RenderDevice::Apply_Render_State_Changes();	//force update of view and projection matrices

	RenderDevice::Set_Render_State(WW3D_RS_ZFUNC,WW3D_CMP_ALWAYS);
	RenderDevice::Set_Render_State(WW3D_RS_ZWRITEENABLE,FALSE);
	RenderDevice::Apply_Render_State_Changes();	//force update of view and projection matrices

	return true;
}

void ScreenDefaultFilter::reset(void)
{
	RenderDevice::Set_Device_Texture(0,nullptr);	//previously rendered frame inside this texture
	RenderDevice::Invalidate_Cached_Render_States();
}

/*=========  ScreenBWFilter	=============================================================*/
///converts viewport to black & white.

Int ScreenBWFilter::m_fadeFrames;
Int ScreenBWFilter::m_curFadeFrame;
Real ScreenBWFilter::m_curFadeValue;
Int ScreenBWFilter::m_fadeDirection;

ScreenBWFilter screenBWFilter;

///List of different BW shader implementations in order of preference
W3DFilterInterface *ScreenBWFilterList[]=
{
	&screenBWFilter,
	nullptr
};

Int ScreenBWFilter::init(void)
{
	HRESULT hr;

	m_dwBWPixelShader = nullptr;
	m_curFadeFrame = 0;

	if (!W3DShaderManager::canRenderToTexture()) {
		// Have to be able to render to texture.
		return false;
	}

	//this shader needs some assets that need to be loaded
	// No vertex declaration: LoadAndCreateD3DShader ignores pDeclaration for pixel shaders.
	//Monochrome pixel shader.
	hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\monochrome.pso", nullptr, 0, false, reinterpret_cast<void**>(&m_dwBWPixelShader));
	if (FAILED(hr))
		return FALSE;

	W3DFilters[FT_VIEW_BW_FILTER]=&screenBWFilter;

	return TRUE;
}

Bool ScreenBWFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	skipRender = false;
	W3DShaderManager::startRenderToTexture();
	return true;
}

Bool ScreenBWFilter::postRender(enum FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
	WW3DTextureHandle tex =	W3DShaderManager::endRenderToTexture();
	DEBUG_ASSERTCRASH(tex, ("Require rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	// Screen-space quad drawn with RenderDevice::Draw_Screen_Quad_UP.
	struct _TRANS_LIT_TEX_VERTEX {
		Vector3 p;	// pixel coordinates
		DWORD color;   // diffuse color
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	RenderDevice::Set_Device_Texture(0,tex);	//previously rendered frame inside this texture
	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

	//bottom right
	v[0].p = Vector3( static_cast<float>(xpos+width), static_cast<float>(ypos+height), 0.0f );
	v[0].u = static_cast<Real>(xpos+width)/static_cast<Real>(TheDisplay->getWidth());	v[0].v = static_cast<Real>(ypos+height)/static_cast<Real>(TheDisplay->getHeight());
	//top right
	v[1].p = Vector3( static_cast<float>(xpos+width), static_cast<float>(ypos), 0.0f );
	v[1].u = static_cast<Real>(xpos+width)/static_cast<Real>(TheDisplay->getWidth());	v[1].v = static_cast<Real>(ypos)/static_cast<Real>(TheDisplay->getHeight());
	//bottom left
	v[2].p = Vector3(  static_cast<float>(xpos), static_cast<float>(ypos+height), 0.0f );
	v[2].u = static_cast<Real>(xpos)/static_cast<Real>(TheDisplay->getWidth());	v[2].v = static_cast<Real>(ypos+height)/static_cast<Real>(TheDisplay->getHeight());
	//top left
	v[3].p = Vector3(  static_cast<float>(xpos),  static_cast<float>(ypos), 0.0f );
	v[3].u = static_cast<Real>(xpos)/static_cast<Real>(TheDisplay->getWidth());	v[3].v = static_cast<Real>(ypos)/static_cast<Real>(TheDisplay->getHeight());
	v[0].color = 0xffffffff;
	v[1].color = 0xffffffff;
	v[2].color = 0xffffffff;
	v[3].color = 0xffffffff;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	RenderDevice::Draw_Screen_Quad_UP(v, sizeof(_TRANS_LIT_TEX_VERTEX), WW3D_FVF_XYZ | WW3D_FVF_DIFFUSE | WW3D_FVF_TEX1);

	reset();
	return true;
}

Int ScreenBWFilter::set(enum FilterModes mode)
{

	if (mode > FM_NULL_MODE)
	{	//rendering a quad with redirected rendering surface tinted by pixel shader

		if (m_fadeDirection > 0)
		{	//turning effect on
			if (!GameClient_IsRenderOnlyFrame())	// the fade counter advances once per logic tick
				m_curFadeFrame++;
			Int fade = m_curFadeFrame;

			if (fade<m_fadeFrames)
			{
				m_curFadeValue = static_cast<Real>(fade)/static_cast<Real>(m_fadeFrames);
			}
			else
			{
				m_curFadeFrame = 0;
				m_curFadeValue = 1.0f;
				m_fadeDirection = 0;
			}
		}
		else
		if (m_fadeDirection < 0)
		{	//turning effect off
			if (!GameClient_IsRenderOnlyFrame())	// the fade counter advances once per logic tick
				m_curFadeFrame++;
			Int fade = m_curFadeFrame;
			if (fade<m_fadeFrames)
			{
				m_curFadeValue = 1.0f - static_cast<Real>(fade)/static_cast<Real>(m_fadeFrames);
			}
			else
			{	m_curFadeValue = 0.0f;
				TheTacticalView->setViewFilterMode(FM_NULL_MODE);
				TheTacticalView->setViewFilter(FT_NULL_FILTER);
				m_curFadeFrame = 0;
				m_fadeDirection = 0;
			}
		}

		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		RenderDevice::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		RenderDevice::Set_Shader(ShaderClass::_PresetOpaqueShader);
		RenderDevice::Set_Texture(0,nullptr);
		RenderDevice::Apply_Render_State_Changes();	//force update of view and projection matrices

		RenderDevice::Set_Render_State(WW3D_RS_ZFUNC,WW3D_CMP_ALWAYS);
		RenderDevice::Set_Render_State(WW3D_RS_ZWRITEENABLE,FALSE);
		RenderDevice::Apply_Render_State_Changes();	//force update of view and projection matrices

		RenderDevice::Set_Pixel_Shader_Object(m_dwBWPixelShader);
		// luminance weights for the monochrome conversion
		RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderScreenFilterPS, lumWeights), Vector4(0.3f, 0.59f, 0.11f, 1.0f));

		Vector4	color(1.0f,1.0f,1.0f,1.0f);	//multiply color

		if (mode == FM_VIEW_BW_BLACK_AND_WHITE)
		{	//back & white mode
			color.X=1.0f;
			color.Y=1.0f;
			color.Z=1.0f;
		}
		if (mode == FM_VIEW_BW_RED_AND_WHITE)
		{	//red is on
			color.X = 1.0f;
			color.Y = 0.0f;
			color.Z = 0.0f;
			//inverse red is on
			//red is on
//			color.X = 0.0f;
//			color.Y = 1.0f;
//			color.Z = 1.0f;
		}
		if (mode == FM_VIEW_BW_GREEN_AND_WHITE)
		{
			color.X = 0.0f;
			color.Y = 1.0f;
			color.Z = 0.0f;
		}

		RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderScreenFilterPS, tint), color);
		RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderScreenFilterPS, fade), Vector4(m_curFadeValue, m_curFadeValue, m_curFadeValue, 1.0f));
/*		RenderDevice::_Get_D3D_Device()->SetPixelShaderConstant(2,   D3DXVECTOR4(150.0f/255.0f, 150.0f/255.0f, 150.0f/255.0f, 0.0f), 1);
		RenderDevice::_Get_D3D_Device()->SetPixelShaderConstant(3,   D3DXVECTOR4((765.0f/450.0f)/3, (765.0f/450.0f)/3, (765.0f/450.0f)/3, 1.0f), 1);
		RenderDevice::_Get_D3D_Device()->SetPixelShaderConstant(4,   D3DXVECTOR4(0.5f, 0.5f, 0.5f, 0), 1);
		RenderDevice::_Get_D3D_Device()->SetPixelShaderConstant(5,   D3DXVECTOR4((60.0f)/255.0f, (60.0f)/255.0f, (60.0f)/255.0f, 0), 1);
		RenderDevice::_Get_D3D_Device()->SetPixelShaderConstant(6,   D3DXVECTOR4((157.0f)/255.0f, (157.0f)/255.0f, (157.0f)/255.0f, 0), 1);
		RenderDevice::_Get_D3D_Device()->SetPixelShaderConstant(7,   D3DXVECTOR4((30.0f)/255.0f, (30.0f)/255.0f, (30.0f)/255.0f, 0), 1);
*/
		return true;
	}
	return false;
}

void ScreenBWFilter::reset(void)
{
	RenderDevice::Set_Device_Texture(0,nullptr);	//previously rendered frame inside this texture
	RenderDevice::Set_Pixel_Shader_Object(0);	//turn off pixel shader
	RenderDevice::Invalidate_Cached_Render_States();
}

Int ScreenBWFilter::shutdown(void)
{
	if (m_dwBWPixelShader)
		RenderDevice::Release_Resource(m_dwBWPixelShader);

	m_dwBWPixelShader=nullptr;

	return TRUE;
}







/*=========  ScreenCrossFadeFilter	=============================================================*/
///Fades screen between 2 different views of the scene with both being visible at once.

Int ScreenCrossFadeFilter::m_fadeFrames;
Int ScreenCrossFadeFilter::m_curFadeFrame;
Real ScreenCrossFadeFilter::m_curFadeValue;
Int ScreenCrossFadeFilter::m_fadeDirection;
TextureClass *ScreenCrossFadeFilter::m_fadePatternTexture=nullptr;
Bool ScreenCrossFadeFilter::m_skipRender = FALSE;

ScreenCrossFadeFilter screenCrossFadeFilter;

///List of different BW shader implementations in order of preference
///@todo: Add a version that doesn't require pixel shader
W3DFilterInterface *ScreenCrossFadeFilterList[]=
{
	&screenCrossFadeFilter,
	nullptr
};

Int ScreenCrossFadeFilter::init(void)
{
	if (!TheDisplay)
		return FALSE;	//effect is useless without a view so no point initializing for the WB, etc.

	m_curFadeFrame = 0;

	if (!W3DShaderManager::canRenderToTexture())
		// Have to be able to render to texture.
		return FALSE;

	//Load an alpha mask texture that will mix foreground/background views.
	m_fadePatternTexture=WW3DAssetManager::Get_Instance()->Get_Texture("exmask_g.tga");
	if (!m_fadePatternTexture)
		return FALSE;
	m_fadePatternTexture->Get_Filter().Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
	m_fadePatternTexture->Get_Filter().Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
	m_fadePatternTexture->Get_Filter().Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_NONE);

	W3DFilters[FT_VIEW_CROSSFADE]=&screenCrossFadeFilter;

	return TRUE;
}

Bool ScreenCrossFadeFilter::updateFadeLevel(void)
{
	if (m_fadeDirection > 0)
	{	//turning effect on
		if (!GameClient_IsRenderOnlyFrame())	// the fade counter advances once per logic tick
			m_curFadeFrame++;
		Int fade = m_curFadeFrame;

		if (fade<m_fadeFrames)
		{
			m_curFadeValue = static_cast<Real>(fade)/static_cast<Real>(m_fadeFrames);
		}
		else
		{
			m_curFadeFrame = 0;
			m_curFadeValue = 1.0f;
			m_fadeDirection = 0;
			return false;
		}
	}
	else
	if (m_fadeDirection < 0)
	{	//turning effect off
		Int fade = m_curFadeFrame;
		if (fade<m_fadeFrames)
		{
			m_curFadeValue = 1.0f - static_cast<Real>(fade)/static_cast<Real>(m_fadeFrames);
			if (!GameClient_IsRenderOnlyFrame())	// the fade counter advances once per logic tick
				m_curFadeFrame++;
		}
		else
		{	m_curFadeValue = 0.0f;
			TheTacticalView->setViewFilterMode(FM_NULL_MODE);
			TheTacticalView->setViewFilter(FT_NULL_FILTER);
			m_curFadeFrame = 0;
			m_fadeDirection = 0;
			return false;
		}
	}
	return true;
}

Bool ScreenCrossFadeFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	if (updateFadeLevel())
	{	//if fade has not completed
		W3DShaderManager::startRenderToTexture();
		scenePassMode=CustomScenePassModes::SCENE_PASS_ALPHA_MASK;
		skipRender = false;
		m_skipRender=true;	//tell the postRender function not to draw into framebuffer yet.
		return true;
	}
	//fade must have completed
	return true;
}

Bool ScreenCrossFadeFilter::postRender(enum FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
	WW3DTextureHandle tex;

	if (m_skipRender)
	{
		//don't render anything to frame buffer because we still need to draw the new scene
		//that we're fading into.  Okay to render on the next call.
		m_skipRender = false;
		doExtraRender = TRUE;
		tex =	W3DShaderManager::endRenderToTexture();
		return true;	
	}

	tex=W3DShaderManager::getRenderTexture();

	DEBUG_ASSERTCRASH(tex, ("Require last rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	// Screen-space quad drawn with RenderDevice::Draw_Screen_Quad_UP.
	struct _TRANS_LIT_TEX_VERTEX {
		Vector3 p;	// pixel coordinates
		DWORD color;   // diffuse color
		float	u;
		float	v;
		float	u1;
		float	v1;
	} v[4];

	Int xpos, ypos, width, height;
	Real radius = 0.0f;

	RenderDevice::Set_Device_Texture(0,tex);	//previously rendered frame inside this texture
	if (mode == FM_VIEW_CROSSFADE_CIRCLE)
	{	RenderDevice::Set_Device_Texture(1,m_fadePatternTexture->Peek_D3D_Texture());
		//Use the current fade level to scale the mask texture, for other modes the texture
		//comes pre-scaled so doesn't require uv scaling.
		radius = (1.0f-m_curFadeValue)*2.0f;
		if (radius <= 0)
			radius = 0.01f;
		radius = 0.5f/radius;
	}

	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

/*	Real radius = (1.0f-m_curFadeValue);
	if (radius <= 0)
		radius = 0.01f;
	radius = 25.0f-radius*24.75f;
*/
	//bottom right
	v[0].p = Vector3( static_cast<float>(xpos+width), static_cast<float>(ypos+height), 0.0f );
	v[0].u = static_cast<Real>(xpos+width)/static_cast<Real>(TheDisplay->getWidth());	v[0].v = static_cast<Real>(ypos+height)/static_cast<Real>(TheDisplay->getHeight());
	v[0].u1 = 0.5f+radius;	v[0].v1 = 0.5f+radius;
	//top right
	v[1].p = Vector3( static_cast<float>(xpos+width), static_cast<float>(ypos), 0.0f );
	v[1].u = static_cast<Real>(xpos+width)/static_cast<Real>(TheDisplay->getWidth());	v[1].v = static_cast<Real>(ypos)/static_cast<Real>(TheDisplay->getHeight());
	v[1].u1 = 0.5f+radius;	v[1].v1 = 0.5f-radius;
	//bottom left
	v[2].p = Vector3(  static_cast<float>(xpos), static_cast<float>(ypos+height), 0.0f );
	v[2].u = static_cast<Real>(xpos)/static_cast<Real>(TheDisplay->getWidth());	v[2].v = static_cast<Real>(ypos+height)/static_cast<Real>(TheDisplay->getHeight());
	v[2].u1 = 0.5f-radius;	v[2].v1 = 0.5f+radius;
	//top left
	v[3].p = Vector3(  static_cast<float>(xpos),  static_cast<float>(ypos), 0.0f );
	v[3].u = static_cast<Real>(xpos)/static_cast<Real>(TheDisplay->getWidth());	v[3].v = static_cast<Real>(ypos)/static_cast<Real>(TheDisplay->getHeight());
	v[3].u1 = 0.5f-radius;	v[3].v1 = 0.5f-radius;

	DWORD diffuse = 0xffffffff;//((Int)((m_curFadeValue) * 255.0f) << 24) | 0x00ffffff;	//store alpha value in vertex diffuse

	v[0].color = diffuse;
	v[1].color = diffuse;
	v[2].color = diffuse;
	v[3].color = diffuse;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.

//		m_pDev->SetTextureStageState(0,D3DTSS_MAGFILTER,WW3D_TEXF_POINT);
//		m_pDev->SetTextureStageState(0,D3DTSS_MINFILTER,WW3D_TEXF_POINT);

	RenderDevice::Draw_Screen_Quad_UP(v, sizeof(_TRANS_LIT_TEX_VERTEX), WW3D_FVF_XYZ | WW3D_FVF_DIFFUSE | WW3D_FVF_TEX2);

	reset();
	return true;
}

Int ScreenCrossFadeFilter::set(enum FilterModes mode)
{
	if (mode > FM_NULL_MODE)
	{	//rendering a quad with redirected rendering surface
		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		RenderDevice::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		RenderDevice::Set_Shader(ShaderClass::_PresetAlphaShader);
		RenderDevice::Set_Texture(0,nullptr);
		RenderDevice::Set_Texture(1,nullptr);
		RenderDevice::Apply_Render_State_Changes();	//force update of view and projection matrices

		RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_CLAMP);
		RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_CLAMP);

		if (mode == FM_VIEW_CROSSFADE_CIRCLE)
		{	//cross-fading using circle mask stored in stage 1
			RenderDevice::Set_Texture_Stage(1, TextureStageDesc()
				.Color(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_CURRENT)
				.Alpha(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_CURRENT)
				.Tex_Coord_Index(1)
			);
			RenderDevice::Set_Sampler_State( 1, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_CLAMP);
			RenderDevice::Set_Sampler_State( 1, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_CLAMP);
			RenderDevice::Set_Sampler_State( 1, WW3D_SAMP_MIPFILTER, WW3D_TEXF_NONE);
		}

		RenderDevice::Set_Render_State(WW3D_RS_ZFUNC,WW3D_CMP_ALWAYS);
		RenderDevice::Set_Render_State(WW3D_RS_ZWRITEENABLE,FALSE);

		return true;
	}
	return false;
}

void ScreenCrossFadeFilter::reset(void)
{
	RenderDevice::Disable_Texture_Stage(1);
	RenderDevice::Set_Device_Texture(0,nullptr);	//previously rendered frame inside this texture
	RenderDevice::Invalidate_Cached_Render_States();
}

Int ScreenCrossFadeFilter::shutdown(void)
{
	REF_PTR_RELEASE(m_fadePatternTexture);

	return TRUE;
}

/*=========  ScreenMotionBlurFilter	=============================================================*/
///applies motion blur to viewport.

ScreenMotionBlurFilter screenMotionBlurFilter;

Coord3D ScreenMotionBlurFilter::m_zoomToPos;
Bool ScreenMotionBlurFilter::m_zoomToValid = false;

ScreenMotionBlurFilter::ScreenMotionBlurFilter():
m_decrement(false),
m_maxCount(0),
m_lastFrame(0), 
m_skipRender(false)
{
}
///List of different motion blur implementations in order of preference
W3DFilterInterface *ScreenMotionBlurFilterList[]=
{
	&screenMotionBlurFilter,
	nullptr
};

Int ScreenMotionBlurFilter::init(void)
{
	if (!W3DShaderManager::canRenderToTexture()) {
		// Have to be able to render to texture.
		return false;
	}
	W3DFilters[FT_VIEW_MOTION_BLUR_FILTER]=this;
	return true;
}

Bool ScreenMotionBlurFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	skipRender = m_skipRender;
	W3DShaderManager::startRenderToTexture();
	return true;
}

Bool ScreenMotionBlurFilter::postRender(enum FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
	WW3DTextureHandle tex =	W3DShaderManager::endRenderToTexture();
	DEBUG_ASSERTCRASH(tex, ("Require rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	// Screen-space quad drawn with RenderDevice::Draw_Screen_Quad_UP.
	Bool continueEffect = true;
	struct _TRANS_LIT_TEX_VERTEX {
		Vector3 p;	// pixel coordinates
		DWORD color;   // diffuse color
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	RenderDevice::Set_Device_Texture(0,tex);	//previously rendered frame inside this texture
	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

	//bottom right
	v[0].p = Vector3( static_cast<float>(xpos+width), static_cast<float>(ypos+height), 0.0f );
	v[0].u = static_cast<Real>(xpos+width)/static_cast<Real>(TheDisplay->getWidth());	v[0].v = static_cast<Real>(ypos+height)/static_cast<Real>(TheDisplay->getHeight());
	//top right
	v[1].p = Vector3( static_cast<float>(xpos+width), static_cast<float>(ypos), 0.0f );
	v[1].u = static_cast<Real>(xpos+width)/static_cast<Real>(TheDisplay->getWidth());	v[1].v = static_cast<Real>(ypos)/static_cast<Real>(TheDisplay->getHeight());
	//bottom left
	v[2].p = Vector3(  static_cast<float>(xpos), static_cast<float>(ypos+height), 0.0f );
	v[2].u = static_cast<Real>(xpos)/static_cast<Real>(TheDisplay->getWidth());	v[2].v = static_cast<Real>(ypos+height)/static_cast<Real>(TheDisplay->getHeight());
	//top left
	v[3].p = Vector3(  static_cast<float>(xpos),  static_cast<float>(ypos), 0.0f );
	v[3].u = static_cast<Real>(xpos)/static_cast<Real>(TheDisplay->getWidth());	v[3].v = static_cast<Real>(ypos)/static_cast<Real>(TheDisplay->getHeight());
	v[0].color = 0xffffffff;
	v[1].color = 0xffffffff;
	v[2].color = 0xffffffff;
	v[3].color = 0xffffffff;


	if (m_additive) {
		RenderDevice::Set_Render_State(WW3D_RS_SRCBLEND,WW3D_BLEND_SRCALPHA);
		RenderDevice::Set_Render_State(WW3D_RS_DESTBLEND,WW3D_BLEND_ONE);
	} else {
		RenderDevice::Set_Render_State(WW3D_RS_SRCBLEND,WW3D_BLEND_SRCALPHA);
		RenderDevice::Set_Render_State(WW3D_RS_DESTBLEND,WW3D_BLEND_INVSRCALPHA);
	}
	RenderDevice::Set_Render_State(WW3D_RS_ALPHABLENDENABLE,false);
	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	RenderDevice::Apply_Render_State_Changes();

	Coord2D center;
	center.x = 0.5f;
	center.y = 0.5f;
	Bool pan = false;
	if (mode>=FM_VIEW_MB_PAN_ALPHA) {
		Real len = sqrt(scrollDelta.x*scrollDelta.x + scrollDelta.y*scrollDelta.y);
		//center.x += 0.5f * (scrollDelta.x/len);
		center.y -= 0.5f; // * (scrollDelta.y/len);
		m_decrement = false;
		m_maxCount = (static_cast<Int>(len*200*m_panFactor/static_cast<Real>(DEFAULT_PAN_FACTOR)));
		if (m_maxCount<m_panFactor/2) 
			m_maxCount = m_panFactor/2;
		if (m_maxCount>m_panFactor) 
			m_maxCount=m_panFactor;
		pan = true;
		m_priorDelta = scrollDelta;
	} else if (mode == FM_VIEW_MB_END_PAN_ALPHA) {
		Real len = sqrt(m_priorDelta.x*m_priorDelta.x + m_priorDelta.y*m_priorDelta.y);
		center.x += 0.5f * (m_priorDelta.x/len);
		center.y -= 0.5f * (m_priorDelta.y/len);
		m_decrement = false;
		if (!GameClient_IsRenderOnlyFrame())	// counts once per logic tick
			m_maxCount--;
		if (m_maxCount<2) {
			continueEffect = false;
		}
		pan = true;
	}


	m_skipRender = false;
	if (!pan && static_cast<UnsignedInt>(m_lastFrame) != TheGameLogic->getFrame()) {
		if (m_decrement) {
			m_maxCount-=COUNT_STEP;
			if (m_maxCount<1) {
				m_decrement = false;
				continueEffect = false;
			}	else {
				m_skipRender = true;
			}
		} else {
			m_maxCount+=COUNT_STEP;
			if (m_maxCount>=MAX_COUNT) {
				m_decrement = true;
				if (m_doZoomTo && m_zoomToValid) {
					TheTacticalView->lookAt(&m_zoomToPos);
				} else {
					continueEffect = false;
				}
			}	else {
				m_skipRender = true;
			}
		}
	}
	Int	 i, j;
	if (!pan) {
		for (i=0; i<4; i++) {
			Real factor = 1.0f - (m_maxCount/static_cast<Real>(MAX_COUNT))*0.90f;
			factor = sqrt(factor);
			v[i].u = ((v[i].u-center.x)*factor) + center.x;
			v[i].v = ((v[i].v-center.y)*factor) + center.y;
		}
	}
	RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Alpha(WW3D_TOP_SELECTARG1, WW3D_TA_CURRENT, WW3D_TA_TEXTURE));
	RenderDevice::Draw_Screen_Quad_UP(v, sizeof(_TRANS_LIT_TEX_VERTEX), WW3D_FVF_XYZ | WW3D_FVF_DIFFUSE | WW3D_FVF_TEX1);
	RenderDevice::Set_Render_State(WW3D_RS_ALPHABLENDENABLE,true);

	RenderDevice::Apply_Render_State_Changes();
	{
		Int limit = m_maxCount;
		if (m_maxCount>30) limit = 30;
		for (j=0; j<limit; j++) {
			for (i=0; i<4; i++) {
				Real factor = 0.99f;
				if (m_additive) factor = 0.98f;
				Int alpha = 0x15;
				if (m_additive) {
					alpha = 0x09;
					if (m_maxCount>limit) {
						alpha += (m_maxCount-limit)/5;
					}
					if (m_maxCount==MAX_COUNT) alpha += 60;
				}
				v[i].color = (alpha<<24)|0x00ffffff; // 
				if (pan) {
					v[i].u = static_cast<float>(((v[i].u-center.x)*(factor+.006)) + center.x);
					v[i].v = ((v[i].v-center.y)*factor) + center.y;
				} else {
					v[i].u = ((v[i].u-center.x)*factor) + center.x;
					v[i].v = ((v[i].v-center.y)*factor) + center.y;
				}
			}
			RenderDevice::Draw_Screen_Quad_UP(v, sizeof(_TRANS_LIT_TEX_VERTEX), WW3D_FVF_XYZ | WW3D_FVF_DIFFUSE | WW3D_FVF_TEX1);

		}
	}
	m_lastFrame = TheGameLogic->getFrame();
	if (pan){
		m_skipRender = false;
	}
	reset();
	if (!continueEffect) {
		m_zoomToValid = false;
	}
	return continueEffect;
}

Bool ScreenMotionBlurFilter::setup(enum FilterModes mode)
{

	m_additive = false;

	if (mode == FM_VIEW_MB_IN_AND_OUT_SATURATE ||
			mode == FM_VIEW_MB_IN_SATURATE ||
			mode == FM_VIEW_MB_OUT_SATURATE) {
		m_additive = true;
	}

	m_doZoomTo = false;
	if (mode == FM_VIEW_MB_IN_AND_OUT_SATURATE ||
			mode == FM_VIEW_MB_IN_AND_OUT_ALPHA ) {
		m_doZoomTo = true;
	}
	if (mode >= FM_VIEW_MB_PAN_ALPHA)	{
		m_panFactor = static_cast<int>(mode) - FM_VIEW_MB_PAN_ALPHA;
		if (m_panFactor<1) m_panFactor = DEFAULT_PAN_FACTOR;
	}
	m_skipRender = false;
	if (mode != FM_VIEW_MB_END_PAN_ALPHA) 
		m_maxCount = 0;
	m_decrement = false;
	m_skipRender = false;
	switch (mode) {
		case FM_VIEW_MB_OUT_SATURATE:
		case FM_VIEW_MB_OUT_ALPHA:
			m_maxCount = MAX_COUNT;
			m_decrement = TRUE;
			break;
	}
	return true;
}

Int ScreenMotionBlurFilter::set(enum FilterModes mode)
{
	if (mode > FM_NULL_MODE)
	{	//rendering a quad with redirected rendering surface motion blurred

		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		RenderDevice::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		RenderDevice::Set_Shader(ShaderClass::_PresetOpaqueShader);
		RenderDevice::Set_Texture(0,nullptr);
		RenderDevice::Set_Texture(1,nullptr);
		RenderDevice::Apply_Render_State_Changes();	//force update of view and projection matrices

		RenderDevice::Set_Render_State(WW3D_RS_ZFUNC,WW3D_CMP_ALWAYS);
		RenderDevice::Set_Render_State(WW3D_RS_ZWRITEENABLE,FALSE);
		RenderDevice::Apply_Render_State_Changes();	//force update of view and projection matrices
	}
	return TRUE;
}

void ScreenMotionBlurFilter::reset(void)
{
	RenderDevice::Set_Device_Texture(0,nullptr);	//previously rendered frame inside this texture
	RenderDevice::Invalidate_Cached_Render_States();
}

Int ScreenMotionBlurFilter::shutdown(void)
{
	return TRUE;
}

/*===========================================================================================*/
/*=========      Shroud Shaders	=============================================================*/
/*===========================================================================================*/

///Shroud layer rendering shader
class ShroudTextureShader : public W3DShaderInterface
{
	virtual Int set(Int pass) override;		///<setup shader for the specified rendering pass.
	virtual Int init(void) override;			///<perform any one time initialization and validation
	virtual void reset(void) override;		///<do any custom resetting necessary to bring W3D in sync.
	Int m_stageOfSet;
} shroudTextureShader;

///List of different shroud shader implementations in order of preference
W3DShaderInterface *ShroudShaderList[]=
{
	&shroudTextureShader,
	nullptr
};

//#define SHROUD_STRETCH_FACTOR	(1.0f/MAP_XY_FACTOR)	//1 texel per heightmap cell width

Int ShroudTextureShader::init(void)
{
	W3DShaders[W3DShaderManager::ST_SHROUD_TEXTURE]=&shroudTextureShader;
	W3DShadersPassCount[W3DShaderManager::ST_SHROUD_TEXTURE]=1;

	return TRUE;
}

//Setup a texture projection in the given stage that applies our shroud.
Int ShroudTextureShader::set(Int stage)
{
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	RenderDevice::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
	RenderDevice::Set_Texture(stage, W3DShaderManager::getShaderTexture(0));	//shroud always stored in texture 0

	if (stage == 0)
	{
#if defined(_DEBUG)
	if (TheGlobalData && TheGlobalData->m_fogOfWarOn)
		RenderDevice::Set_Shader(ShaderClass::_PresetAlphaSpriteShader);
	else
		RenderDevice::Set_Shader(ShaderClass::_PresetMultiplicativeSpriteShader);
#else
	RenderDevice::Set_Shader(ShaderClass::_PresetMultiplicativeSpriteShader);
#endif
	}
	RenderDevice::Apply_Render_State_Changes();

	RenderDevice::Set_Texture_Stage(stage, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_CAMERASPACEPOSITION)
		.Tex_Transform(WW3D_TTFF_COUNT2)
	);
	RenderDevice::Set_Render_State(WW3D_RS_ZFUNC,WW3D_CMP_EQUAL);

	//We need to scale so shroud texel stretches over one full terrain cell.  Each texel
	//is 1/128 the size of full texture. (assuming 128x128 vid-mem texture).
	W3DShroud *shroud;
	if ((shroud=TheTerrainRenderObject->getShroud()) != 0)
	{	///@todo: All this code really only need to be done once per camera/view.  Find a way to optimize it out.
		WWD3DMatrix inv;
		float det;

		Matrix4x4 curView;
		RenderDevice::_Get_Device_Transform(WW3D_TS_VIEW, curView);

		WWD3D::MatrixInverse(&inv, &det, reinterpret_cast<WWD3DMatrix*>(&curView));

		WWD3DMatrix scale,offset;

		//We need to make all world coordinates be relative to the heightmap data origin since that
		//is where the shroud begins.

		float xoffset = 0;
		float yoffset = 0;
		Real width=shroud->getCellWidth();
		Real height=shroud->getCellHeight();

		if (TheTerrainRenderObject->getMap())
		{	//subtract origin position from all coordinates.  Origin is shifted by 1 cell width/height to allow for unused border texels.
			xoffset = -static_cast<float>(shroud->getDrawOriginX()) + width;
			yoffset = -static_cast<float>(shroud->getDrawOriginY()) + height;
		}

		WWD3D::MatrixTranslation(&offset, xoffset, yoffset,0);

		width = 1.0f/(width*shroud->getTextureWidth());
		height = 1.0f/(height*shroud->getTextureHeight());
		WWD3D::MatrixScaling(&scale, width, height, 1);
		*(reinterpret_cast<WWD3DMatrix *>(&curView)) = (inv * offset) * scale;
		RenderDevice::_Set_Device_Transform(static_cast<WW3DTransformType>(WW3D_TS_TEXTURE0+stage), *(static_cast<Matrix4x4*>(&curView)));
	}
	m_stageOfSet=stage;
	return TRUE;
}

void ShroudTextureShader::reset(void)
{
	RenderDevice::Set_Texture(m_stageOfSet,nullptr);
	RenderDevice::Set_Render_State(WW3D_RS_ZFUNC,WW3D_CMP_LESSEQUAL);
	RenderDevice::Set_Texture_Stage(m_stageOfSet, TextureStageDesc()
		.Tex_Coord_Index(m_stageOfSet)
		.Tex_Transform(WW3D_TTFF_DISABLE)
	);
}

///Shroud layer rendering shader

///List of different shroud shader implementations in order of preference

//#define SHROUD_STRETCH_FACTOR	(1.0f/MAP_XY_FACTOR)	//1 texel per heightmap cell width


//Setup a texture projection in the given stage that applies our shroud.


///Mask layer rendering shader
class MaskTextureShader : public W3DShaderInterface
{
	virtual Int set(Int pass) override;		///<setup shader for the specified rendering pass.
	virtual Int init(void) override;			///<perform any one time initialization and validation
	virtual void reset(void) override;		///<do any custom resetting necessary to bring W3D in sync.
} maskTextureShader;

///List of different shroud shader implementations in order of preference
W3DShaderInterface *MaskShaderList[]=
{
	&maskTextureShader,
	nullptr
};

Int MaskTextureShader::init(void)
{
	W3DShaders[W3DShaderManager::ST_MASK_TEXTURE]=&maskTextureShader;
	W3DShadersPassCount[W3DShaderManager::ST_MASK_TEXTURE]=1;

	return TRUE;
}

Int MaskTextureShader::set(Int pass)
{
	Real fadeLevel=ScreenCrossFadeFilter::getCurrentFadeValue();

	//Use the current fade level to scale the mask texture
	Real radius = (1.0f-fadeLevel)*2.0f;
	if (radius <= 0)
		radius = 0.01f;
	radius = 0.5f/radius;

	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	RenderDevice::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.

	//For now we're always going to project the texture coming from the crossfade effect
	RenderDevice::Set_Texture(0, ScreenCrossFadeFilter::getCurrentMaskTexture());
	ShaderClass shader=ShaderClass::_PresetOpaqueShader;
	shader.Set_Primary_Gradient(ShaderClass::GRADIENT_DISABLE);
	RenderDevice::Set_Shader(shader);
	RenderDevice::Apply_Render_State_Changes();
	Matrix4x4 curView;
	RenderDevice::_Get_Device_Transform(WW3D_TS_VIEW, curView);

	RenderDevice::Set_Texture_Stage(0, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_CAMERASPACEPOSITION)
		.Tex_Transform(WW3D_TTFF_COUNT2)
	);

	WWD3DMatrix inv;
	float det;

	//Get inverse view matrix so we can transform camera space points back to world space
	WWD3D::MatrixInverse(&inv, &det, reinterpret_cast<WWD3DMatrix*>(&curView));

	WWD3DMatrix scale,offset,offsetTextureCenter;
	// centerPos is only set when TheTacticalView exists but is always read below (C4701);
	// initialise it so early startup does not pass garbage to WWD3D::MatrixTranslation().
	Coord3D centerPos = { 0, 0, 0 };

	//Find center of projection (this should be returned from some other filter, etc. but
	//for now assume terrain location at center of screen.
	if (TheTacticalView)
	{	Int xpos,ypos;

		TheTacticalView->getOrigin(&xpos,&ypos);

		ICoord2D screenPos;
		screenPos.x=static_cast<Int>(static_cast<Real>(TheTacticalView->getWidth())*0.5f);
		screenPos.y=static_cast<Int>(static_cast<Real>(TheTacticalView->getHeight())*0.5f);
		TheTacticalView->screenToTerrain(&screenPos,&centerPos);
	}

	WWD3D::MatrixTranslation(&offset, -centerPos.x, -centerPos.y,0);

	WWD3D::MatrixTranslation(&offsetTextureCenter, 0.5f, 0.5f, 0);	//shift coordinates so center of projection falls at uv 0.5,0.5

	Real worldTexelWidth=(1.0f-fadeLevel)*25.0f;	//9 worked well for circle but weird shape requires more stretch to cover.
	Real worldTexelHeight=(1.0f-fadeLevel)*25.0f;

	///@todo: Fix this to work with non 128x128 textures.
	if (worldTexelWidth != 0 && worldTexelHeight != 0)
	{	Real widthScale = 1.0f/(worldTexelWidth*128.0f);
		Real heightScale = 1.0f/(worldTexelHeight*128.0f);
		WWD3D::MatrixScaling(&scale, widthScale, heightScale, 1);
		*(reinterpret_cast<WWD3DMatrix *>(&curView)) = ((inv * offset) * scale)*offsetTextureCenter;
	}
	else
	{	WWD3D::MatrixScaling(&scale, 0, 0, 1);	//scaling by 0 will set uv coordinates to 0,0
		*(reinterpret_cast<WWD3DMatrix *>(&curView)) = ((inv * offset) * scale);
	}

	RenderDevice::_Set_Device_Transform(WW3D_TS_TEXTURE0, *(static_cast<Matrix4x4*>(&curView)));

	return TRUE;
}

void MaskTextureShader::reset(void)
{
	RenderDevice::Set_Texture(0,nullptr);
	RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Tex_Coord_Index(0).Tex_Transform(WW3D_TTFF_DISABLE));
}

/*===========================================================================================*/
/*=========      Terrain Shaders	=========================================================*/
/*===========================================================================================*/

///regular terrain shader that should work on all multi-texture video cards (slowest version)
class TerrainShader2Stage : public W3DShaderInterface
{
public:
	float m_xSlidePerSecond ;	 ///< How far the clouds move per second.
	float m_ySlidePerSecond ;	 ///< How far the clouds move per second.
	int	  m_curTick;
	float m_xOffset;
	float m_yOffset;
	virtual Int set(Int pass) override;		///<setup shader for the specified rendering pass.
	virtual Int init(void) override;			///<perform any one time initialization and validation
	virtual void reset(void) override;		///<do any custom resetting necessary to bring W3D in sync.
	void updateNoise1 (WWD3DMatrix *destMatrix,WWD3DMatrix *curViewInverse, Bool doUpdate=true);	///<generate the uv coordinates for Noise1 (i.e clouds)
	void updateNoise2 (WWD3DMatrix *destMatrix,WWD3DMatrix *curViewInverse, Bool doUpdate=true);	///<generate the uv coordinates for Noise2 (i.e lightmap)
} terrainShader2Stage;

///regular terrain shader that should work on all multi-texture video cards (slowest version)

///regular terrain shader that should work on all multi-texture video cards (slowest version)

// The terrain stages set their own linear filter and never request anisotropy, so the terrain blurs at flat
// camera angles. With TerrainAnisotropy > 1 the atlas gets anisotropic filtering with a linear mip filter;
// the cloud and light maps (stages 2/3, soft and camera projected) keep their filter.
// The sampler state shadow stores floats as their bit pattern.
static unsigned terrainLodBiasState(Real bias)
{
	unsigned bits;
	::memcpy(&bits, &bias, sizeof(bits));
	return bits;
}

void W3DShaderManager::applyTerrainAtlasFilter(Int stage)
{
	if (!TheGlobalData)
		return;
	if (TheGlobalData->m_terrainLodBias != 0.0f)
		RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MIPMAPLODBIAS, terrainLodBiasState(TheGlobalData->m_terrainLodBias));
	if (TheGlobalData->m_terrainAnisotropy <= 1)
		return;
	if (!TheGlobalData->m_bilinearTerrainTex && !TheGlobalData->m_trilinearTerrainTex)
		return;		// point filtering explicitly requested
	unsigned level = static_cast<unsigned>(TheGlobalData->m_terrainAnisotropy);
	const unsigned caps = RenderCaps::Get_Max_Anisotropy();
	if (caps >= 1 && level > caps)
		level = caps;
	if (level > 16)
		level = 16;
	RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MINFILTER, WW3D_TEXF_ANISOTROPIC);
	RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MIPFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MAXANISOTROPY, level);
}

// Other textures set only min, mag and mip filters (TextureFilterClass::Apply); the anisotropy level comes from
// _Init_Filters for all stages. Restore it after the terrain so the following models do not inherit the terrain level.
void W3DShaderManager::restoreTerrainAtlasFilter(Int stage)
{
	if (!TheGlobalData)
		return;
	// No other texture sets a LOD bias, so 0 is the state to return to.
	if (TheGlobalData->m_terrainLodBias != 0.0f)
		RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MIPMAPLODBIAS, terrainLodBiasState(0.0f));
	if (TheGlobalData->m_terrainAnisotropy <= 1)
		return;
	unsigned level = RenderCaps::Get_Max_Anisotropy();
	if (level > 16)
		level = 16;
	if (level < 1)
		level = 1;
	RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MAXANISOTROPY, level);
}

///Pixel shader based terrain shader - fastest method for the newest cards.
class TerrainShaderPixelShader : public W3DShaderInterface
{
	WW3DPixelShaderHandle 					m_dwBasePixelShader;	///<handle to terrain D3D pixel shader
	WW3DPixelShaderHandle 					m_dwBaseNoise1PixelShader;	///<handle to terrain/single noise D3D pixel shader
	WW3DPixelShaderHandle 					m_dwBaseNoise2PixelShader;	///<handle to terrain/double noise D3D pixel shader

	virtual Int set(Int pass) override;		///<setup shader for the specified rendering pass.
	virtual void reset(void) override;		///<do any custom resetting necessary to bring W3D in sync.
	virtual Int init(void) override;			///<perform any one time initialization and validation
	virtual Int shutdown(void) override;			///<release resources used by shader
} terrainShaderPixelShader;

///List of different terrain shader implementations in order of preference
W3DShaderInterface *TerrainShaderList[]=
{
	&terrainShaderPixelShader,
	&terrainShader2Stage,
	nullptr
};

///List of different terrain shader implementations in order of preference

Int TerrainShader2Stage::init( void )
{
	//initialize settings for uv animated clouds
	m_xSlidePerSecond = -0.02f;	 
	m_ySlidePerSecond =  1.50f * m_xSlidePerSecond;
	m_curTick = 0;
	m_curTick = WW3D::Get_Sync_Time();//::GetTickCount();
	m_xOffset = 0;
	m_yOffset = 0;

	//no special device validation needed - anything in our min spec should handle this.

	W3DShaders[W3DShaderManager::ST_TERRAIN_BASE]=&terrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE]=2;
	W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE1]=&terrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE1]=3;
	W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE2]=&terrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE2]=3;
	W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE12]=&terrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE12]=3;

	return TRUE;
}

void TerrainShader2Stage::reset(void)
{
	ShaderClass::Invalidate();
	W3DShaderManager::restoreTerrainAtlasFilter(0);
	W3DShaderManager::restoreTerrainAtlasFilter(1);

	//Free references to textures
	RenderDevice::Set_Device_Texture(0, nullptr);
	RenderDevice::Set_Device_Texture(1, nullptr);

	RenderDevice::Set_Texture_Stage(0, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_PASSTHRU|0)
		.Tex_Transform(WW3D_TTFF_DISABLE)
	);

	RenderDevice::Set_Texture_Stage(1, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_PASSTHRU|1)
		.Tex_Transform(WW3D_TTFF_DISABLE)
	);
}

void TerrainShader2Stage::updateNoise1(WWD3DMatrix *destMatrix,WWD3DMatrix *curViewInverse, Bool doUpdate)
{
	#define STRETCH_FACTOR ((float)(1/(63.0*MAP_XY_FACTOR/2))) /* covers 63/2 tiles */

	WWD3DMatrix scale;

	WWD3D::MatrixScaling(&scale, STRETCH_FACTOR, STRETCH_FACTOR,1);
	*destMatrix = *curViewInverse * scale;

	WWD3DMatrix offset;

	Int delta = m_curTick;
	m_curTick = WW3D::Get_Sync_Time();//::GetTickCount();
	delta = m_curTick-delta;
	m_xOffset += m_xSlidePerSecond*delta/1000;
	m_yOffset += m_ySlidePerSecond*delta/1000;


	//m_xOffset += m_xSlidePerSecond*delta/500;
	//m_yOffset += m_ySlidePerSecond*delta/500;


	//m_yOffset = sinf( (float)m_curTick * 0.0001f );
	//m_xOffset = cosf( (float)m_curTick * 0.0001f );

	while (m_xOffset > 1) m_xOffset -= 1;
	while (m_yOffset > 1) m_yOffset -= 1;
	while (m_xOffset < -1) m_xOffset += 1;
	while (m_yOffset < -1) m_yOffset += 1;

	WWD3D::MatrixTranslation(&offset, m_xOffset, m_yOffset,0);
	*destMatrix *= offset;
}

void TerrainShader2Stage::updateNoise2(WWD3DMatrix *destMatrix,WWD3DMatrix *curViewInverse, Bool doUpdate)
{
			
	WWD3DMatrix scale;

	WWD3D::MatrixScaling(&scale, STRETCH_FACTOR, STRETCH_FACTOR,1);
	*destMatrix = *curViewInverse * scale;
}

Int TerrainShader2Stage::set(Int pass)																											  
{
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	RenderDevice::Apply_Render_State_Changes();

	if (TheGlobalData && TheGlobalData->m_bilinearTerrainTex || TheGlobalData->m_trilinearTerrainTex) {
		RenderDevice::Set_Sampler_State(0, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
		RenderDevice::Set_Sampler_State(0, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
	} else {
		RenderDevice::Set_Sampler_State(0, WW3D_SAMP_MINFILTER, WW3D_TEXF_POINT);
		RenderDevice::Set_Sampler_State(0, WW3D_SAMP_MAGFILTER, WW3D_TEXF_POINT);
		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MINFILTER, WW3D_TEXF_POINT);
		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MAGFILTER, WW3D_TEXF_POINT);
	}
	if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex) {
		RenderDevice::Set_Sampler_State(0, WW3D_SAMP_MIPFILTER, WW3D_TEXF_LINEAR);
		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MIPFILTER, WW3D_TEXF_LINEAR);
	} else {
		RenderDevice::Set_Sampler_State(0, WW3D_SAMP_MIPFILTER, WW3D_TEXF_POINT);
		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MIPFILTER, WW3D_TEXF_LINEAR);
	}
	// anisotropic filtering for the terrain atlas
	if (pass < 2) {
		W3DShaderManager::applyTerrainAtlasFilter(0);
		W3DShaderManager::applyTerrainAtlasFilter(1);
	}

	switch (pass)
	{
		case 0:
			RenderDevice::Set_Device_Texture(0, W3DShaderManager::getShaderTexture(0)->Peek_D3D_Texture());
			RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_CLAMP);
			RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_CLAMP);

			// Modulate the diffuse color with the texture as lighting comes from diffuse.
			RenderDevice::Set_Texture_Stage(0, TextureStageDesc()
				.Color(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_DIFFUSE)
				.Alpha_Op(WW3D_TOP_DISABLE)
			);
			RenderDevice::Disable_Texture_Stage(1);
			RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Tex_Coord_Index(0));
			RenderDevice::Set_Render_State(WW3D_RS_ALPHABLENDENABLE,false);
			break;
		case 1:
			RenderDevice::Set_Device_Texture(0, W3DShaderManager::getShaderTexture(1)->Peek_D3D_Texture());
			RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_CLAMP);
			RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_CLAMP);

			// Modulate the diffuse color with the texture as lighting comes from diffuse.
			RenderDevice::Set_Texture_Stage(0, TextureStageDesc()
				.Color(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_DIFFUSE)
				.Alpha_Op(WW3D_TOP_MODULATE)
				.Tex_Coord_Index(1)
			);
			// Blend the result using the alpha. (came from diffuse mod texture)
			RenderDevice::Set_Render_State(WW3D_RS_ALPHABLENDENABLE,true);
			RenderDevice::Set_Render_State(WW3D_RS_SRCBLEND,WW3D_BLEND_SRCALPHA);
			RenderDevice::Set_Render_State(WW3D_RS_DESTBLEND,WW3D_BLEND_INVSRCALPHA);
			// Disable stage 2.
			RenderDevice::Disable_Texture_Stage(1);
			break;
		case 2:
			// Noise/cloud pass
			Matrix4x4 curView;
			RenderDevice::_Get_Device_Transform(WW3D_TS_VIEW, curView);

			//these states apply to all noise/cloud combination passes
			RenderDevice::Set_Texture_Stage(0, TextureStageDesc()
				.Color(WW3D_TOP_SELECTARG1, WW3D_TA_TEXTURE, WW3D_TA_DIFFUSE)
				.Alpha_Op(WW3D_TOP_DISABLE)
			);

			RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Tex_Coord_Index(WW3D_TCI_CAMERASPACEPOSITION));
			// Two output coordinates are used.
			RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Tex_Transform(WW3D_TTFF_COUNT2));
			RenderDevice::Set_Sampler_State(0, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
			RenderDevice::Set_Sampler_State(0, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);

			//blend into frame buffer
			RenderDevice::Set_Render_State(WW3D_RS_ALPHABLENDENABLE,true);
			RenderDevice::Set_Render_State(WW3D_RS_SRCBLEND,WW3D_BLEND_DESTCOLOR);
			RenderDevice::Set_Render_State(WW3D_RS_DESTBLEND,WW3D_BLEND_ZERO);

			
			WWD3DMatrix inv;
			float det;

			WWD3D::MatrixInverse(&inv, &det, reinterpret_cast<WWD3DMatrix*>(&curView));

			if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_TERRAIN_BASE_NOISE12)
			{
				//setup cloud pass
				RenderDevice::Set_Device_Texture(0, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());

				updateNoise1((reinterpret_cast<WWD3DMatrix*>(&curView)),&inv);	//update curView with texture matrix
				RenderDevice::_Set_Device_Transform(WW3D_TS_TEXTURE0, curView);
				//clouds always need bilinear filtering
				RenderDevice::Set_Sampler_State(0, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
				RenderDevice::Set_Sampler_State(0, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);

				//setup noise pass
				RenderDevice::Set_Device_Texture(1, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());

				updateNoise2((reinterpret_cast<WWD3DMatrix*>(&curView)),&inv);
				RenderDevice::_Set_Device_Transform(WW3D_TS_TEXTURE1, curView);
				//noise always needs point/linear filtering.  Why point!?
				RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MINFILTER, WW3D_TEXF_POINT);
				RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);

				RenderDevice::Set_Texture_Stage(1, TextureStageDesc()
					.Color(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_CURRENT)
					.Alpha_Op(WW3D_TOP_DISABLE)
					.Tex_Coord_Index(WW3D_TCI_CAMERASPACEPOSITION)
				);
				// Two output coordinates are used.
				RenderDevice::Set_Texture_Stage(1, TextureStageDesc().Tex_Transform(WW3D_TTFF_COUNT2));

				RenderDevice::Set_Sampler_State( 1, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
				RenderDevice::Set_Sampler_State( 1, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);
			} //ST_TERRAIN_BASE_NOISE12
			else
			{	//only 1 noise or cloud texture
				// Now setup the texture pipeline.
				if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_TERRAIN_BASE_NOISE1)
				{	//setup cloud pass
					RenderDevice::Set_Device_Texture(0, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());
					updateNoise1((reinterpret_cast<WWD3DMatrix*>(&curView)),&inv);	//update curView with texture matrix
					RenderDevice::Set_Sampler_State(0, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
					RenderDevice::Set_Sampler_State(0, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
				}
				else
				{
					//setup noise pass
					RenderDevice::Set_Device_Texture(0, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());
					updateNoise2((reinterpret_cast<WWD3DMatrix*>(&curView)),&inv);	//update curView with texture matrix
					RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MINFILTER, WW3D_TEXF_POINT);
					RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
				}

				RenderDevice::Disable_Texture_Stage(1);
				RenderDevice::_Set_Device_Transform(WW3D_TS_TEXTURE0, *(static_cast<Matrix4x4*>(&curView)));
			}
			break;
	}

	return TRUE;
}

Int TerrainShaderPixelShader::shutdown(void)
{
	if (m_dwBasePixelShader)
		RenderDevice::Release_Resource(m_dwBasePixelShader);

	if (m_dwBaseNoise1PixelShader)
		RenderDevice::Release_Resource(m_dwBaseNoise1PixelShader);

	if (m_dwBaseNoise2PixelShader)
		RenderDevice::Release_Resource(m_dwBaseNoise2PixelShader);

	m_dwBasePixelShader=nullptr;
	m_dwBaseNoise1PixelShader=nullptr;
	m_dwBaseNoise2PixelShader=nullptr;

	return TRUE;
}

Int TerrainShaderPixelShader::init( void )
{	
#ifdef DISABLE_PIXEL_SHADERS
	return false;
#endif
	//this shader will also use the 2Stage shader for some of the passes so initialize it too.
	if (terrainShader2Stage.init())
	{
		//this shader needs some assets that need to be loaded

		//base version which doesn't apply any noise textures.
		HRESULT hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\terrain.pso", nullptr, 0, false, reinterpret_cast<void**>(&m_dwBasePixelShader));
		if (FAILED(hr))
			return FALSE;

		//version which blends 1 noise texture.
		hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\terrainnoise.pso", nullptr, 0, false, reinterpret_cast<void**>(&m_dwBaseNoise1PixelShader));
		if (FAILED(hr))
			return FALSE;

		//version which blends 2 noise textures.
		hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\terrainnoise2.pso", nullptr, 0, false, reinterpret_cast<void**>(&m_dwBaseNoise2PixelShader));
		if (FAILED(hr))
			return FALSE;

		W3DShaders[W3DShaderManager::ST_TERRAIN_BASE]=&terrainShaderPixelShader;
		W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE1]=&terrainShaderPixelShader;
		W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE2]=&terrainShaderPixelShader;
		W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE12]=&terrainShaderPixelShader;
		W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE]=1;
		W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE1]=1;
		W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE2]=1;
		W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE12]=1;
		return TRUE;
	}
	return FALSE;
}

Int TerrainShaderPixelShader::set(Int pass)
{	
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	RenderDevice::Apply_Render_State_Changes();

	//setup base pass
	RenderDevice::Set_Device_Texture(0, W3DShaderManager::getShaderTexture(0)->Peek_D3D_Texture());
	RenderDevice::Set_Device_Texture(1, W3DShaderManager::getShaderTexture(1)->Peek_D3D_Texture());

	RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_CLAMP);
	RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_CLAMP);
	RenderDevice::Set_Sampler_State( 1, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_CLAMP);
	RenderDevice::Set_Sampler_State( 1, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_CLAMP);

	//tell pixel shader which UV set to use for each stage
	RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Tex_Coord_Index(0));
	RenderDevice::Set_Texture_Stage(1, TextureStageDesc().Tex_Coord_Index(1));

	if (TheGlobalData && TheGlobalData->m_bilinearTerrainTex || TheGlobalData->m_trilinearTerrainTex) {
		RenderDevice::Set_Sampler_State(0, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
		RenderDevice::Set_Sampler_State(0, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
	} else {
		RenderDevice::Set_Sampler_State(0, WW3D_SAMP_MINFILTER, WW3D_TEXF_POINT);
		RenderDevice::Set_Sampler_State(0, WW3D_SAMP_MAGFILTER, WW3D_TEXF_POINT);
		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MINFILTER, WW3D_TEXF_POINT);
		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MAGFILTER, WW3D_TEXF_POINT);
	}
	if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex) {
		RenderDevice::Set_Sampler_State(0, WW3D_SAMP_MIPFILTER, WW3D_TEXF_LINEAR);
		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MIPFILTER, WW3D_TEXF_LINEAR);
	} else {
		RenderDevice::Set_Sampler_State(0, WW3D_SAMP_MIPFILTER, WW3D_TEXF_POINT);
		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MIPFILTER, WW3D_TEXF_LINEAR);
	}
	// anisotropic filtering for the terrain atlas
	W3DShaderManager::applyTerrainAtlasFilter(0);
	W3DShaderManager::applyTerrainAtlasFilter(1);

	if (W3DShaderManager::getCurrentShader() >= W3DShaderManager::ST_TERRAIN_BASE_NOISE1)
	{	
		Matrix4x4 curView;
		RenderDevice::_Get_Device_Transform(WW3D_TS_VIEW, curView);

		WWD3DMatrix inv;
		float det;
		WWD3D::MatrixInverse(&inv, &det, reinterpret_cast<WWD3DMatrix*>(&curView));

		RenderDevice::Set_Texture_Stage(2, TextureStageDesc().Tex_Coord_Index(WW3D_TCI_CAMERASPACEPOSITION));
		// Two output coordinates are used.
		RenderDevice::Set_Texture_Stage(2, TextureStageDesc().Tex_Transform(WW3D_TTFF_COUNT2));

		RenderDevice::Set_Sampler_State(2, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
		RenderDevice::Set_Sampler_State(2, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);
		
		if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_TERRAIN_BASE_NOISE12)
		{	//full shader
			RenderDevice::Set_Sampler_State(3, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
			RenderDevice::Set_Sampler_State(3, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);
			RenderDevice::Set_Device_Texture(2, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());
			RenderDevice::Set_Device_Texture(3, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());
			RenderDevice::Set_Pixel_Shader_Object(m_dwBaseNoise2PixelShader);

			RenderDevice::Set_Sampler_State(2, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
			RenderDevice::Set_Sampler_State(2, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);

			RenderDevice::Set_Sampler_State(3, WW3D_SAMP_MINFILTER, WW3D_TEXF_POINT);
			RenderDevice::Set_Sampler_State(3, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);

			terrainShader2Stage.updateNoise1((reinterpret_cast<WWD3DMatrix*>(&curView)),&inv);	//update curView with texture matrix
			RenderDevice::_Set_Device_Transform(WW3D_TS_TEXTURE2, curView);

			terrainShader2Stage.updateNoise2((reinterpret_cast<WWD3DMatrix*>(&curView)),&inv);	//update curView with texture matrix
			RenderDevice::_Set_Device_Transform(WW3D_TS_TEXTURE3, curView);

			RenderDevice::Set_Texture_Stage(3, TextureStageDesc().Tex_Coord_Index(WW3D_TCI_CAMERASPACEPOSITION));
			// Two output coordinates are used.
			RenderDevice::Set_Texture_Stage(3, TextureStageDesc().Tex_Transform(WW3D_TTFF_COUNT2));
		}
		else
		{	//single noise texture shader
			RenderDevice::Set_Pixel_Shader_Object(m_dwBaseNoise1PixelShader);

			if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_TERRAIN_BASE_NOISE1)
			{	//cloud map
				RenderDevice::Set_Device_Texture(2, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());
				terrainShader2Stage.updateNoise1((reinterpret_cast<WWD3DMatrix*>(&curView)),&inv);	//update curView with texture matrix
				RenderDevice::Set_Sampler_State(2, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
				RenderDevice::Set_Sampler_State(2, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
			}
			else
			{	//light map
				RenderDevice::Set_Device_Texture(2, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());
				terrainShader2Stage.updateNoise2((reinterpret_cast<WWD3DMatrix*>(&curView)),&inv);	//update curView with texture matrix
				RenderDevice::Set_Sampler_State(2, WW3D_SAMP_MINFILTER, WW3D_TEXF_POINT);
				RenderDevice::Set_Sampler_State(2, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
			}
			RenderDevice::_Set_Device_Transform(WW3D_TS_TEXTURE2, curView);
		}
	}
	else
	{	//just base texturing
		RenderDevice::Set_Pixel_Shader_Object(m_dwBasePixelShader);
	}

	return TRUE;
}

void TerrainShaderPixelShader::reset(void)
{
	W3DShaderManager::restoreTerrainAtlasFilter(0);
	W3DShaderManager::restoreTerrainAtlasFilter(1);
	RenderDevice::Set_Device_Texture(2,nullptr);	//release reference to any texture
	RenderDevice::Set_Device_Texture(3,nullptr);	//release reference to any texture

	RenderDevice::Set_Pixel_Shader_Object(0);	//turn off pixel shader

	RenderDevice::Set_Device_Texture(0, nullptr);
	RenderDevice::Set_Device_Texture(1, nullptr);

	RenderDevice::Set_Texture_Stage(0, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_PASSTHRU|0)
		.Tex_Transform(WW3D_TTFF_DISABLE)
	);

	RenderDevice::Set_Texture_Stage(1, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_PASSTHRU|1)
		.Tex_Transform(WW3D_TTFF_DISABLE)
	);

	RenderDevice::Set_Texture_Stage(2, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_PASSTHRU|2)
		.Tex_Transform(WW3D_TTFF_DISABLE)
	);

	RenderDevice::Set_Texture_Stage(3, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_PASSTHRU|3)
		.Tex_Transform(WW3D_TTFF_DISABLE)
	);


	RenderDevice::Invalidate_Cached_Render_States();
}

///Cloud layer rendering shader - used for objects similar to terrain which only need the cloud layer.
class CloudTextureShader : public W3DShaderInterface
{
	virtual Int set(Int stage) override;		///<setup shader for the specified rendering pass.
	virtual Int init(void) override;			///<perform any one time initialization and validation
	virtual void reset(void) override;		///<do any custom resetting necessary to bring W3D in sync.
	Int m_stageOfSet;
} cloudTextureShader;

///List of different cloud shader implementations in order of preference
W3DShaderInterface *CloudShaderList[]=
{
	&cloudTextureShader,
	nullptr
};

Int CloudTextureShader::init(void)
{
	W3DShaders[W3DShaderManager::ST_CLOUD_TEXTURE]=&cloudTextureShader;
	W3DShadersPassCount[W3DShaderManager::ST_CLOUD_TEXTURE]=1;

	return TRUE;
}

/**Setup a certain texture stage to project our cloud texture*/
Int CloudTextureShader::set(Int stage)
{
	Matrix4x4 curView;
	RenderDevice::_Get_Device_Transform(WW3D_TS_VIEW, curView);

	WWD3DMatrix inv;
	float det;

	WWD3D::MatrixInverse(&inv, &det, reinterpret_cast<WWD3DMatrix*>(&curView));

	//Get a texture matrix that applies the current cloud position
	terrainShader2Stage.updateNoise1((reinterpret_cast<WWD3DMatrix*>(&curView)),&inv,false);	//update curView with texture matrix

	RenderDevice::Set_Texture_Stage(stage, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_CAMERASPACEPOSITION)
		.Tex_Transform(WW3D_TTFF_COUNT2)
	);
	RenderDevice::_Set_Device_Transform(static_cast<WW3DTransformType>(WW3D_TS_TEXTURE0+stage), curView);
	RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
	RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);

	RenderDevice::Set_Texture_Stage(stage, TextureStageDesc()
		.Color(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_CURRENT)
		.Alpha(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_CURRENT)
	);

	RenderDevice::Set_Device_Texture(stage, W3DShaderManager::getShaderTexture(stage)->Peek_D3D_Texture());

	m_stageOfSet=stage;
	return TRUE;
}

void CloudTextureShader::reset(void)
{
	//Free reference to texture
	RenderDevice::Set_Device_Texture(m_stageOfSet, nullptr);
	//Turn off texture projection
	RenderDevice::Set_Texture_Stage(m_stageOfSet, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_PASSTHRU|m_stageOfSet)
		.Tex_Transform(WW3D_TTFF_DISABLE)
	);

	RenderDevice::Set_Texture_Stage(m_stageOfSet, TextureStageDesc()
		.Color_Op(WW3D_TOP_DISABLE)
		.Alpha_Op(WW3D_TOP_DISABLE)
	);
}

/*===========================================================================================*/
/*=========      Road Shaders	=========================================================*/
/*===========================================================================================*/
class RoadShaderPixelShader : public W3DShaderInterface
{
	WW3DPixelShaderHandle 					m_dwBaseNoise2PixelShader;	///<handle to road/double noise D3D pixel shader

	virtual Int set(Int pass) override;		///<setup shader for the specified rendering pass.
	virtual void reset(void) override;		///<do any custom resetting necessary to bring W3D in sync.
	virtual Int init(void) override;			///<perform any one time initialization and validation
	virtual Int shutdown(void) override;			///<release resources used by shader
} roadShaderPixelShader;

class RoadShader2Stage : public W3DShaderInterface
{	friend class RoadShaderPixelShader;	//pixel shader version uses some of the same features.

	virtual Int set(Int pass) override;		///<setup shader for the specified rendering pass.
	virtual Int init(void) override;			///<perform any one time initialization and validation
	virtual void reset(void) override;
} roadShader2Stage;

///List of different terrain shader implementations in order of preference
W3DShaderInterface *RoadShaderList[]=
{
	&roadShaderPixelShader,
	&roadShader2Stage,
	nullptr
};

Int RoadShaderPixelShader::shutdown(void)
{
	if (m_dwBaseNoise2PixelShader)
		RenderDevice::Release_Resource(m_dwBaseNoise2PixelShader);

	m_dwBaseNoise2PixelShader=nullptr;

	return TRUE;
}

Int RoadShaderPixelShader::init( void )
{	

	//this shader will also use the 2Stage shader for some of the passes so initialize it too.
	if (roadShader2Stage.init())
	{
		//this shader needs some assets that need to be loaded

		//version which blends 2 noise textures.
		HRESULT hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\roadnoise2.pso", nullptr, 0, false, reinterpret_cast<void**>(&m_dwBaseNoise2PixelShader));
		if (FAILED(hr))
			return FALSE;

		//Only set this shader for use in dual noise mode.  The 2Stage shader will take care of
		//all the other modes.
		W3DShaders[W3DShaderManager::ST_ROAD_BASE_NOISE12]=&roadShaderPixelShader;
		W3DShadersPassCount[W3DShaderManager::ST_ROAD_BASE_NOISE12]=1;
		return TRUE;
	}
	return FALSE;
}

Int RoadShaderPixelShader::set(Int pass)
{
	RenderDevice::Set_Texture(0,W3DShaderManager::getShaderTexture(0));
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	RenderDevice::Apply_Render_State_Changes();

	//tell pixel shader which UV set to use for each stage
	RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Tex_Coord_Index(0));

	RenderDevice::Set_Render_State(WW3D_RS_ZFUNC,WW3D_CMP_LESSEQUAL);
	RenderDevice::Set_Render_State(WW3D_RS_ZWRITEENABLE,FALSE);
	RenderDevice::Set_Render_State(WW3D_RS_LIGHTING, FALSE);

	RenderDevice::Set_Render_State(WW3D_RS_ALPHABLENDENABLE,true);	//blend roads into terrain
	RenderDevice::Set_Render_State(WW3D_RS_SRCBLEND,WW3D_BLEND_SRCALPHA);
	RenderDevice::Set_Render_State(WW3D_RS_DESTBLEND,WW3D_BLEND_INVSRCALPHA);

	Matrix4x4 curView;
	RenderDevice::_Get_Device_Transform(WW3D_TS_VIEW, curView);

	WWD3DMatrix inv;
	float det;
	WWD3D::MatrixInverse(&inv, &det, reinterpret_cast<WWD3DMatrix*>(&curView));

	if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex)
	{	RenderDevice::Set_Sampler_State(0, WW3D_SAMP_MIPFILTER, WW3D_TEXF_LINEAR);
		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MIPFILTER, WW3D_TEXF_LINEAR);
	}
	else
	{	RenderDevice::Set_Sampler_State(0, WW3D_SAMP_MIPFILTER, WW3D_TEXF_POINT);
		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MIPFILTER, WW3D_TEXF_POINT);
	}

	RenderDevice::Set_Texture_Stage(1, TextureStageDesc().Tex_Coord_Index(WW3D_TCI_CAMERASPACEPOSITION));
	// Two output coordinates are used.
	RenderDevice::Set_Texture_Stage(1, TextureStageDesc().Tex_Transform(WW3D_TTFF_COUNT2));

	RenderDevice::Set_Sampler_State(1, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
	RenderDevice::Set_Sampler_State(1, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);
	
	RenderDevice::Set_Sampler_State(2, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
	RenderDevice::Set_Sampler_State(2, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);

	RenderDevice::Set_Texture(1,W3DShaderManager::getShaderTexture(1));
	RenderDevice::Set_Texture(2,W3DShaderManager::getShaderTexture(2));

	RenderDevice::Set_Pixel_Shader_Object(m_dwBaseNoise2PixelShader);

	RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);

	RenderDevice::Set_Sampler_State(2, WW3D_SAMP_MINFILTER, WW3D_TEXF_POINT);
	RenderDevice::Set_Sampler_State(2, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);

	terrainShader2Stage.updateNoise1((reinterpret_cast<WWD3DMatrix*>(&curView)),&inv, false);	//get texture projection matrix
	RenderDevice::_Set_Device_Transform(WW3D_TS_TEXTURE1, curView);

	terrainShader2Stage.updateNoise2((reinterpret_cast<WWD3DMatrix*>(&curView)),&inv, false);	//get texture projection matrix
	RenderDevice::_Set_Device_Transform(WW3D_TS_TEXTURE2, curView);

	RenderDevice::Set_Texture_Stage(2, TextureStageDesc().Tex_Coord_Index(WW3D_TCI_CAMERASPACEPOSITION));
	// Two output coordinates are used.
	RenderDevice::Set_Texture_Stage(2, TextureStageDesc().Tex_Transform(WW3D_TTFF_COUNT2));

	return TRUE;
}

void RoadShaderPixelShader::reset(void)
{
	W3DShaderManager::restoreTerrainAtlasFilter(0);	// three-way blends use the atlas in stage 0

	RenderDevice::Set_Pixel_Shader_Object(0);	//turn off pixel shader

	RenderDevice::Set_Texture_Stage(0, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_PASSTHRU|0)
		.Tex_Transform(WW3D_TTFF_DISABLE)
	);

	RenderDevice::Set_Texture_Stage(1, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_PASSTHRU|1)
		.Tex_Transform(WW3D_TTFF_DISABLE)
	);

	RenderDevice::Set_Texture_Stage(2, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_PASSTHRU|2)
		.Tex_Transform(WW3D_TTFF_DISABLE)
	);

	RenderDevice::Set_Texture_Stage(3, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_PASSTHRU|3)
		.Tex_Transform(WW3D_TTFF_DISABLE)
	);


	RenderDevice::Invalidate_Cached_Render_States();
}

Int RoadShader2Stage::init( void )
{
	//no special device validation needed - anything in our min spec should handle this.
	W3DShaders[W3DShaderManager::ST_ROAD_BASE]=&roadShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_BASE]=1;
	W3DShaders[W3DShaderManager::ST_ROAD_BASE_NOISE1]=&roadShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_BASE_NOISE1]=1;
	W3DShaders[W3DShaderManager::ST_ROAD_BASE_NOISE2]=&roadShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_BASE_NOISE2]=1;
	W3DShaders[W3DShaderManager::ST_ROAD_BASE_NOISE12]=&roadShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_BASE_NOISE12]=2;

	return TRUE;
}

Int RoadShader2Stage::set(Int pass)																											  
{
	//First stage always contains base texture.
	RenderDevice::Set_Texture(0,W3DShaderManager::getShaderTexture(0));
	//Force system to apply world/view transforms.
	RenderDevice::Apply_Render_State_Changes();

	RenderDevice::Set_Render_State(WW3D_RS_ZFUNC,WW3D_CMP_LESSEQUAL);
	RenderDevice::Set_Render_State(WW3D_RS_ZWRITEENABLE,FALSE);
	RenderDevice::Set_Render_State(WW3D_RS_LIGHTING, FALSE);

	// Modulate the diffuse color with the texture as lighting comes from diffuse.
	RenderDevice::Set_Texture_Stage(0, TextureStageDesc()
		.Color(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_DIFFUSE)
		.Alpha(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_DIFFUSE)
	);

	RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Tex_Coord_Index(0));
	RenderDevice::Set_Render_State(WW3D_RS_ALPHABLENDENABLE,true);	//blend roads into terrain

	if (pass == 0)
	{	
		RenderDevice::Set_Render_State(WW3D_RS_SRCBLEND,WW3D_BLEND_SRCALPHA);
		RenderDevice::Set_Render_State(WW3D_RS_DESTBLEND,WW3D_BLEND_INVSRCALPHA);

		if (W3DShaderManager::getCurrentShader() >= W3DShaderManager::ST_ROAD_BASE_NOISE1)
		{	//second texture unit will contain a noise pass
			Matrix4x4 curView;
			RenderDevice::_Get_Device_Transform(WW3D_TS_VIEW, curView);

			WWD3DMatrix inv;
			float det;
			WWD3D::MatrixInverse(&inv, &det, reinterpret_cast<WWD3DMatrix*>(&curView));

			if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex)
				RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MIPFILTER, WW3D_TEXF_LINEAR);
			else
				RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MIPFILTER, WW3D_TEXF_POINT);

			RenderDevice::Set_Texture_Stage(1, TextureStageDesc().Tex_Coord_Index(WW3D_TCI_CAMERASPACEPOSITION));
			// Two output coordinates are used.
			RenderDevice::Set_Texture_Stage(1, TextureStageDesc().Tex_Transform(WW3D_TTFF_COUNT2));

			RenderDevice::Set_Sampler_State(1, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
			RenderDevice::Set_Sampler_State(1, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);

			RenderDevice::Set_Texture_Stage(1, TextureStageDesc()
				.Color(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_CURRENT)
				.Alpha(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_CURRENT)
			);

			if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_ROAD_BASE_NOISE12)
			{	//full shader, apply noise 1 in pass 0.
				RenderDevice::Set_Texture(1,W3DShaderManager::getShaderTexture(1));
				RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
				RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);

				terrainShader2Stage.updateNoise1((reinterpret_cast<WWD3DMatrix*>(&curView)),&inv, false);	//get texture projection matrix
				RenderDevice::_Set_Device_Transform(WW3D_TS_TEXTURE1, curView);
			}
			else
			{	//single noise texture shader
				if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_ROAD_BASE_NOISE1)
				{	//cloud map
					RenderDevice::Set_Texture(1,W3DShaderManager::getShaderTexture(1));
					terrainShader2Stage.updateNoise1((reinterpret_cast<WWD3DMatrix*>(&curView)),&inv, false);	//update curView with texture matrix
					RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
					RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
				}
				else
				{	//light map
					RenderDevice::Set_Texture(1,W3DShaderManager::getShaderTexture(2));
					terrainShader2Stage.updateNoise2((reinterpret_cast<WWD3DMatrix*>(&curView)),&inv, false);	//update curView with texture matrix
					RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MINFILTER, WW3D_TEXF_POINT);
					RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
				}
				RenderDevice::_Set_Device_Transform(WW3D_TS_TEXTURE1, curView);
			}
		}
		else
		{	//just base texturing
			RenderDevice::Disable_Texture_Stage(1);
		}
	}	//pass 0
	else
	{	//pass 1, apply additional noise pass
		Matrix4x4 curView;
		RenderDevice::_Get_Device_Transform(WW3D_TS_VIEW, curView);

		WWD3DMatrix inv;
		float det;
		WWD3D::MatrixInverse(&inv, &det, reinterpret_cast<WWD3DMatrix*>(&curView));

		if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex)
			RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MIPFILTER, WW3D_TEXF_LINEAR);
		else
			RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MIPFILTER, WW3D_TEXF_POINT);

		RenderDevice::Set_Texture(1,W3DShaderManager::getShaderTexture(2));

		terrainShader2Stage.updateNoise2((reinterpret_cast<WWD3DMatrix*>(&curView)),&inv, false);	//update curView with texture matrix
		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MINFILTER, WW3D_TEXF_POINT);
		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);

		RenderDevice::Set_Texture_Stage(1, TextureStageDesc().Tex_Coord_Index(WW3D_TCI_CAMERASPACEPOSITION));
		// Two output coordinates are used.
		RenderDevice::Set_Texture_Stage(1, TextureStageDesc().Tex_Transform(WW3D_TTFF_COUNT2));

		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);

		//Copy alpha channel into stage 1 but mask out color channel by replacing with white.
		RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Color_Arg1(WW3D_TA_TEXTURE));
		//Force color channel to white by copying the alpha into RGB
		RenderDevice::Set_Texture_Stage(0, TextureStageDesc()
			.Alpha(WW3D_TOP_SELECTARG1, WW3D_TA_TEXTURE, WW3D_TA_DIFFUSE)
			.Color_Op(WW3D_TOP_SELECTARG2)
			.Color_Arg2(WW3D_TA_DIFFUSE|WW3D_TA_ALPHAREPLICATE)
		);

		RenderDevice::Set_Texture_Stage(1, TextureStageDesc()
			.Color(WW3D_TOP_BLENDCURRENTALPHA, WW3D_TA_TEXTURE, WW3D_TA_CURRENT)
			.Alpha(WW3D_TOP_DISABLE, WW3D_TA_TEXTURE, WW3D_TA_CURRENT)
		);

		//Modulate into existing roads with clouds applied. - only apply where roads are transparent by
		//using road texture as a mask.
		RenderDevice::Set_Render_State(WW3D_RS_SRCBLEND,WW3D_BLEND_ZERO);
		RenderDevice::Set_Render_State(WW3D_RS_DESTBLEND,WW3D_BLEND_SRCCOLOR);

		RenderDevice::_Set_Device_Transform(WW3D_TS_TEXTURE0, curView);
	}

	return TRUE;
}

void RoadShader2Stage::reset(void)
{
	W3DShaderManager::restoreTerrainAtlasFilter(0);	// three-way blends use the atlas in stage 0
	ShaderClass::Invalidate();

	//Free references to textures
	RenderDevice::Set_Texture_Stage(0, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_PASSTHRU|0)
		.Tex_Transform(WW3D_TTFF_DISABLE)
	);

	RenderDevice::Set_Texture_Stage(1, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_PASSTHRU|1)
		.Tex_Transform(WW3D_TTFF_DISABLE)
	);
}

/** List of all custom shader lists - each list in this list contains variations of the same
	shader to allow it to work on different hardware configurations.
*/
W3DShaderInterface **MasterShaderList[]=
{
	TerrainShaderList,
	ShroudShaderList,
	RoadShaderList,
	MaskShaderList,
	CloudShaderList,
	nullptr
};

/** List of all custom filter lists - each list in this list contains variations of the same
	filter to allow it to work on different hardware configurations.
*/
W3DFilterInterface **MasterFilterList[]=
{
	ScreenDefaultFilterList,
	ScreenBWFilterList,
	ScreenMotionBlurFilterList,
	ScreenCrossFadeFilterList,
	nullptr
};

// W3DShaderManager::W3DShaderManager =========================================
/** Constructor - just clears some variables */
//=============================================================================
W3DShaderManager::W3DShaderManager(void)
{
	m_currentShader = ST_INVALID;
	m_currentFilter = FT_NULL_FILTER;
	m_oldRenderSurface = nullptr;
	m_renderTexture = nullptr;
	m_newRenderSurface = nullptr;
	m_oldDepthSurface = nullptr;
	m_renderingToTexture = false;
	Int i;
	for (i=0; i<W3DShaderManager::ST_MAX; i++)
	{	W3DShaders[i]=nullptr;
		W3DShadersPassCount[i]=0;
	}
	for (i=0; i<FT_MAX; i++)
	{	W3DFilters[i]=nullptr;
	}
	for (i=0; i<8; i++)
	{
		m_Textures[i]=nullptr;
	}
	m_currentShader=static_cast<W3DShaderManager::ShaderTypes>(-1);
}

// W3DShaderManager::init =======================================================
/** Walk through all shaders and find versions suitable for current hardware */
//=============================================================================
void W3DShaderManager::init(void)
{
	int i,j;

	WW3DSurfaceDesc desc;

	//Some of our effects require an offscreen render target, so try creating it here.
	HRESULT hr=RenderDevice::Get_Render_Target(&m_oldRenderSurface);

	RenderDevice::Get_Surface_Desc(m_oldRenderSurface, &desc);

	hr=RenderDevice::Create_Device_Render_Target_Texture(desc.Width,desc.Height,static_cast<WW3DDeviceFormat>(desc.Format),&m_renderTexture);

	if (hr != S_OK)
	{
		if (m_oldRenderSurface) RenderDevice::Release_Resource(m_oldRenderSurface);
		m_oldRenderSurface = nullptr;
		m_renderTexture = nullptr;
	} else {
		hr = RenderDevice::Get_Texture_Surface_Level(m_renderTexture, 0, &m_newRenderSurface);
		if (hr != S_OK)
		{
			if (m_renderTexture) RenderDevice::Release_Resource(m_renderTexture);
			m_renderTexture = nullptr;
			m_newRenderSurface = nullptr;
		}	else {
			hr = RenderDevice::Get_Depth_Stencil_Surface(&m_oldDepthSurface);
			if (hr != S_OK)
			{
				if (m_newRenderSurface) RenderDevice::Release_Resource(m_newRenderSurface);
				if (m_renderTexture) RenderDevice::Release_Resource(m_renderTexture);
				m_renderTexture = nullptr;
				m_newRenderSurface = nullptr;
				m_oldDepthSurface = nullptr;
			}
		}
	}

	W3DShaderInterface **shaders;

	for (i=0; MasterShaderList[i] != nullptr; i++)
	{	
		shaders=MasterShaderList[i];
		for (j=0; shaders[j] != nullptr; j++)
		{
			if (shaders[j]->init())
				break;	//found a working shader
		}
	}
	W3DFilterInterface **filters;

	for (i=0; MasterFilterList[i] != nullptr; i++)
	{	
		filters=MasterFilterList[i];
		for (j=0; filters[j] != nullptr; j++)
		{
			if (filters[j]->init())
				break;	//found a working shader
		}
	}

}

// W3DShaderManager::shutdown =======================================================
/** Any shaders which allocate resources will be allowed to free them */
//=============================================================================
void W3DShaderManager::shutdown(void)
{
	if (m_newRenderSurface) RenderDevice::Release_Resource(m_newRenderSurface);
	if (m_renderTexture) RenderDevice::Release_Resource(m_renderTexture);
	if (m_oldRenderSurface) RenderDevice::Release_Resource(m_oldRenderSurface);
	if (m_oldDepthSurface) RenderDevice::Release_Resource(m_oldDepthSurface);
	m_renderTexture = nullptr;
	m_newRenderSurface = nullptr;
	m_oldDepthSurface = nullptr;
	m_oldRenderSurface = nullptr;
	m_currentShader = ST_INVALID;
	m_currentFilter = FT_NULL_FILTER;
	//release any assets associated with a shader (vertex/pixel shaders, textures, etc.)
	Int i;
	for (i=0; i<W3DShaderManager::ST_MAX; i++) {
		if (W3DShaders[i]) {
			W3DShaders[i]->shutdown();
		}
	}

	for (i=0; i < FT_MAX; i++)
	{	
		if (W3DFilters[i])
		{
			W3DFilters[i]->shutdown();
		}
	}

}

// W3DShaderManager::getShaderPasses =======================================================
/** Return number of renderig passes required in perform the desired shader on current
	hardware.  App will need to re-render the polygons this many times to complete the
	effect.
 */
//=============================================================================
Int W3DShaderManager::getShaderPasses(ShaderTypes shader)
{
	return W3DShadersPassCount[shader];
}

// W3DShaderManager::setShader =======================================================
/** Must call this method before each rendering pass in order to perform proper D3D
	setup for each shader.
 */
//=============================================================================
Int W3DShaderManager::setShader(ShaderTypes shader, Int pass)
{
	if (shader == m_currentShader && pass == m_currentShaderPass)
		return TRUE;	//shader is already set
	m_currentShader=shader;
	m_currentShaderPass = pass;
	if (W3DShaders[shader])
		return W3DShaders[shader]->set(pass);
	return FALSE;
}

// W3DShaderManager::resetShader =======================================================
/** Must call this method after all polygons and rendering passes have been submitted.
	This method allows D3D to reset itself to a default state that doesn't conflict
	with the WW3D2 Shader system.
 */
//=============================================================================
void W3DShaderManager::resetShader(ShaderTypes shader)
{	
	if (m_currentShader == ST_INVALID)
		return;	//last shader is already reset.
	if (W3DShaders[shader])
		W3DShaders[shader]->reset();
	m_currentShader = ST_INVALID;
}
// W3DShaderManager::filterPreRender =======================================================
/** Call to view filter shaders before rendering starts.
 */
//=============================================================================
Bool W3DShaderManager::filterPreRender(FilterTypes filter, Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	if (W3DFilters[filter])
	{	Bool result=W3DFilters[filter]->preRender(skipRender,scenePassMode);
		if (result)
			m_currentFilter = filter;
		return result;
	}
	return FALSE;
}

// W3DShaderManager::filterPostRender =======================================================
/** Call to view filter shaders after rendering is complete.
 */
//=============================================================================
Bool W3DShaderManager::filterPostRender(FilterTypes filter, enum FilterModes mode, Coord2D &scrollDelta, Bool &doExtraRender)
{
	if (W3DFilters[filter])
		return W3DFilters[filter]->postRender(mode, scrollDelta,doExtraRender);

	m_currentFilter = FT_NULL_FILTER;
	return FALSE;
}

// W3DShaderManager::filterPostRender =======================================================
/** Call to view filter shaders after rendering is complete.
 */
//=============================================================================
Bool W3DShaderManager::filterSetup(FilterTypes filter, enum FilterModes mode)
{
	if (W3DFilters[filter])
		return W3DFilters[filter]->setup(mode);
	return FALSE;
}

/*Draws 2 triangles covering the viewport given the current render states*/
void W3DShaderManager::drawViewport(Int color)
{
	// Screen-space quad drawn with RenderDevice::Draw_Screen_Quad_UP.
	struct _TRANS_LIT_TEX_VERTEX {
		Vector3 p;	// pixel coordinates
		DWORD color;   // diffuse color
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

	//bottom right
	v[0].p = Vector3( static_cast<float>(xpos+width), static_cast<float>(ypos+height), 0.0f );
	v[0].u = static_cast<Real>(xpos+width)/static_cast<Real>(TheDisplay->getWidth());	v[0].v = static_cast<Real>(ypos+height)/static_cast<Real>(TheDisplay->getHeight());
	//top right
	v[1].p = Vector3( static_cast<float>(xpos+width), static_cast<float>(ypos), 0.0f );
	v[1].u = static_cast<Real>(xpos+width)/static_cast<Real>(TheDisplay->getWidth());	v[1].v = static_cast<Real>(ypos)/static_cast<Real>(TheDisplay->getHeight());
	//bottom left
	v[2].p = Vector3(  static_cast<float>(xpos), static_cast<float>(ypos+height), 0.0f );
	v[2].u = static_cast<Real>(xpos)/static_cast<Real>(TheDisplay->getWidth());	v[2].v = static_cast<Real>(ypos+height)/static_cast<Real>(TheDisplay->getHeight());
	//top left
	v[3].p = Vector3(  static_cast<float>(xpos),  static_cast<float>(ypos), 0.0f );
	v[3].u = static_cast<Real>(xpos)/static_cast<Real>(TheDisplay->getWidth());	v[3].v = static_cast<Real>(ypos)/static_cast<Real>(TheDisplay->getHeight());
	v[0].color = color;
	v[1].color = color;
	v[2].color = color;
	v[3].color = color;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	RenderDevice::Draw_Screen_Quad_UP(v, sizeof(_TRANS_LIT_TEX_VERTEX), WW3D_FVF_XYZ | WW3D_FVF_DIFFUSE | WW3D_FVF_TEX1);
}

// W3DShaderManager::startRenderToTexture =======================================================
/** Starts rendering to a texture.
 */
//=============================================================================
void W3DShaderManager::startRenderToTexture(void)
{	
	DEBUG_ASSERTCRASH(!m_renderingToTexture, ("Already rendering to texture - cannot nest calls."));

	if (m_renderingToTexture || m_newRenderSurface==nullptr || m_oldDepthSurface==nullptr) return;
	// Sets color and depth surface in one step without Set_Render_Target's bookkeeping;
	// endRenderToTexture restores the previous target.
	HRESULT hr = RenderDevice::Begin_Surface_Pass(RENDER_GPU_PASS_SCREEN_FILTER, m_newRenderSurface, m_oldDepthSurface);
	DEBUG_ASSERTCRASH(hr==S_OK, ("Set target failed unexpectedly."));
	if (hr != S_OK)
		return;
	m_renderingToTexture = true;
	if (TheGlobalData->m_showSoftWaterEdge)
	{	//Soft water edges use frame buffer destination alpha so we must clear it to a known value.
		if (m_currentFilter == FT_VIEW_MOTION_BLUR_FILTER || m_currentFilter == FT_VIEW_CROSSFADE)
		{	//these filters rely on the previous frame being visible so we must be careful about clearing
			//frame buffer.  Only clear the alpha channel
			RenderDevice::Set_Render_State(WW3D_RS_COLORWRITEENABLE,WW3D_COLORWRITEENABLE_ALPHA);	//only clear alpha
			ShaderClass shader=ShaderClass::_PresetOpaqueSolidShader;
			shader.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);
			shader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);
			RenderDevice::Set_Shader(shader);

			VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
			RenderDevice::Set_Material(vmat);
			REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		
			drawViewport(0x00ffffff | ((static_cast<Int>(WaterRenderObjClass::getFrameAlphaClearValue()*255.0f)) <<24));
			RenderDevice::Set_Render_State(WW3D_RS_COLORWRITEENABLE,WW3D_COLORWRITEENABLE_RED|WW3D_COLORWRITEENABLE_GREEN|WW3D_COLORWRITEENABLE_BLUE);	//disable writes to alpha
		}
		else	//normal clear that overwrites everything.
			RenderDevice::Clear(true, false, Vector3( 0.0f, 0.0f, 0.0f ), WaterRenderObjClass::getFrameAlphaClearValue());
	}
}

// W3DShaderManager::startRenderToTexture =======================================================
/** Ends rendering to a texture.
 */
//=============================================================================
WW3DTextureHandle W3DShaderManager::endRenderToTexture(void)
{	
	DEBUG_ASSERTCRASH(m_renderingToTexture, ("Not rendering to texture."));
	if (!m_renderingToTexture) return nullptr;
	HRESULT hr = RenderDevice::End_Surface_Pass(RENDER_GPU_PASS_SCREEN_FILTER, m_oldRenderSurface, m_oldDepthSurface);	//restore original render target
	DEBUG_ASSERTCRASH(hr==S_OK, ("Set target failed unexpectedly."));
	if (hr == S_OK)
	{
		//assume render target texure will be in stage 0.  Most hardware has "conditional" support for
		//non-power-of-2 textures so we must force some required states:
		RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_CLAMP);
		RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_CLAMP);
		RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSW, WW3D_TADDRESS_CLAMP);
		RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
		RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
		RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_MIPFILTER, WW3D_TEXF_NONE);

		m_renderingToTexture = false;
	}
	return m_renderTexture;
}

/**Returns texture containing the image that was last rendered using any of the effects requiring render target
textures.  Used mostly for cross-fading effects that need an unmodified version of the view before the effect
was applied.  NOTE: This texture does not survive device reset.. so quit effect on reset!*/
WW3DTextureHandle W3DShaderManager::getRenderTexture(void)
{
	return m_renderTexture;
}

//=============================================================================
// WaterRenderObjClass::LoadAndCreateShader
//=============================================================================
/** Loads and creates a D3D pixel or vertex shader.*/
//=============================================================================
// Reads an HLSL file through the game file system and compiles it via RenderDevice
// (entry point "main", shader model 4.0).
static HRESULT loadAndCreateHLSLShader(const char* path, Bool isVertexShader, void** pHandle)
{
	// Callers check for NULL and fall back to their fixed-function path, so always leave
	// a defined value, even if the file cannot be opened.
	if (pHandle) *pHandle = nullptr;

	File* file = TheFileSystem->openFile(path, File::READ | File::BINARY);
	if (file == nullptr)
		return E_FAIL;
	FileInfo fileInfo;
	TheFileSystem->getFileInfo(AsciiString(path), &fileInfo);
	const unsigned size = fileInfo.sizeLow;
	char* source = new char[size + 1];
	file->read(source, size);
	file->close();
	source[size] = 0;
	HRESULT hr;
	if (isVertexShader)
		hr = RenderDevice::Create_Vertex_Shader_From_HLSL(source, size, path, reinterpret_cast<WW3DVertexShaderHandle*>(pHandle));
	else
		hr = RenderDevice::Create_Pixel_Shader_From_HLSL(source, size, path, reinterpret_cast<WW3DPixelShaderHandle*>(pHandle));
	delete [] source;
	return hr;
}

HRESULT W3DShaderManager::LoadAndCreateD3DShader(const char* strFilePath, const DWORD* /*pDeclaration*/, DWORD /*Usage*/, Bool ShaderType, void** pHandle)
{
	// Maps "shaders\\name.pso" to "shaders\\sm4\\name.ps.hlsl" (.vso -> .vs.hlsl); paths already ending in
	// .ps.hlsl/.vs.hlsl only get "sm4\\" inserted. The sources are copied to Shaders/sm4 next to the exe.
	// If compilation fails the shader stays NULL and the caller takes its fixed-function path.
	char base[MAX_PATH];
	strncpy(base, strFilePath, sizeof(base) - 1);
	base[sizeof(base) - 1] = 0;
	const size_t len = strlen(base);
	const Bool isHlsl = (len >= 5 && _stricmp(base + len - 5, ".hlsl") == 0);
	if (isHlsl && len >= 8 && (_stricmp(base + len - 8, ".ps.hlsl") == 0 ||
							   _stricmp(base + len - 8, ".vs.hlsl") == 0)) {
		base[len - 8] = 0;
	}
	else {
		char* dot = strrchr(base, '.');
		if (dot) *dot = 0;
	}

	const char* suffix = ShaderType ? ".vs.hlsl" : ".ps.hlsl";

	// split directory and file name to insert "sm4\\"
	char sm4Path[MAX_PATH];
	char* slash = strrchr(base, '\\');
	if (!slash) slash = strrchr(base, '/');
	if (slash) {
		const size_t dir_len = static_cast<size_t>(slash - base) + 1;
		strncpy(sm4Path, base, dir_len);
		sm4Path[dir_len] = 0;
		strncat(sm4Path, "sm4\\", sizeof(sm4Path) - strlen(sm4Path) - 1);
		strncat(sm4Path, slash + 1, sizeof(sm4Path) - strlen(sm4Path) - 1);
	}
	else {
		safe_strcpy(sm4Path, "sm4\\");
		strncat(sm4Path, base, sizeof(sm4Path) - strlen(sm4Path) - 1);
	}
	strncat(sm4Path, suffix, sizeof(sm4Path) - strlen(sm4Path) - 1);

	if (SUCCEEDED(loadAndCreateHLSLShader(sm4Path, ShaderType, pHandle)))
		return S_OK;
	WWDEBUG_SAY(("LoadAndCreateD3DShader: %s missing or does not compile\n", sm4Path));
	return E_FAIL;
}



// W3DShaderManager::setShroudTex =======================================================
/** Puts the shroud texture into a texture stage.
 */
//=============================================================================
Int W3DShaderManager::setShroudTex(Int stage)
{
	//We need to scale so shroud texel stretches over one full terrain cell.  Each texel
	//is 1/128 the size of full texture. (assuming 128x128 vid-mem texture).
	W3DShroud *shroud;
	if ((shroud=TheTerrainRenderObject->getShroud()) != 0)
	{	 
		RenderDevice::Set_Texture(stage, shroud->getShroudTexture());

		RenderDevice::Set_Texture_Stage(stage, TextureStageDesc()
			.Color(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_CURRENT)
			.Alpha(WW3D_TOP_SELECTARG2, WW3D_TA_TEXTURE, WW3D_TA_CURRENT)
			.Tex_Coord_Index(WW3D_TCI_CAMERASPACEPOSITION)
			.Tex_Transform(WW3D_TTFF_COUNT2)
		);
		WWD3DMatrix inv;
		float det;

		Matrix4x4 curView;
		RenderDevice::_Get_Device_Transform(WW3D_TS_VIEW, curView);

		WWD3D::MatrixInverse(&inv, &det, reinterpret_cast<WWD3DMatrix*>(&curView));

		WWD3DMatrix scale,offset;

		//We need to make all world coordinates be relative to the heightmap data origin since that
		//is where the shroud begins.

		float xoffset = 0;
		float yoffset = 0;
		Real width=shroud->getCellWidth();
		Real height=shroud->getCellHeight();

		if (TheTerrainRenderObject->getMap())
		{	//subtract origin position from all coordinates.  Origin is shifted by 1 cell width/height to allow for unused border texels.
			xoffset = -static_cast<float>(shroud->getDrawOriginX()) + width;
			yoffset = -static_cast<float>(shroud->getDrawOriginY()) + height;
		}

		WWD3D::MatrixTranslation(&offset, xoffset, yoffset,0);

		width = 1.0f/(width*shroud->getTextureWidth());
		height = 1.0f/(height*shroud->getTextureHeight());
		WWD3D::MatrixScaling(&scale, width, height, 1);
		*(reinterpret_cast<WWD3DMatrix *>(&curView)) = (inv * offset) * scale;
		RenderDevice::_Set_Device_Transform(static_cast<WW3DTransformType>(WW3D_TS_TEXTURE0+stage), *(static_cast<Matrix4x4*>(&curView)));
		return TRUE;
	}
	return FALSE;
}




















