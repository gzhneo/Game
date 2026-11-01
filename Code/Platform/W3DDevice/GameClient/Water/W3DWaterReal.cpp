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

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "stdio.h"
#include "formatinto.h"
#include "W3DDevice/GameClient/W3DWater.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "W3DDevice/GameClient/W3DWaterTracks.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "texture.h"
#include "assetmgr.h"
#include "rinfo.h"
#include "camera.h"
#include "scene.h"
#include "WW3D2/Common/renderdevice.h"
#include "WW3D2/Common/renderbackend.h"	// viewport for the scene copy
#include "WW3D2/Common/renderpieces.h"
#include "light.h"
#include "wwd3dmath.h"	// matrix math helpers
#include "simplevec.h"
#include "mesh.h"
#include "matinfo.h"

#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/Xfer.h"
#include "Common/GameLOD.h"
#include "Common/GameEngine.h"	// GameEngine_RenderTimeMs
#include "Common/FileSystem.h"		// water textures from ProjectData
#include "Common/file.h"
#include "Common/ArchiveFileSystem.h"	// sky tiling for HD texture archives
#include "Common/UserSettings.h"		// Options.ini "WaterTexture"
#include <vector>

#include "GameClient/Water.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptEngine.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DPoly.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DCustomScene.h"
#include "formatinto.h"
#include "W3DWaterInternal.h"

// Realistic water and water version 2: turbidity, scene copy, wave maps, free water textures

static TextureClass *loadWater2Dds(const char *name);

//=================================================================================================
// Realistic standing water
//=================================================================================================
// Beer-Lambert turbidity, Fresnel reflection and refraction for the trapezoid water surfaces.
// Visual only; nothing here touches game logic. Requires destination alpha in the frame buffer
// (as the soft water edge does), the realwater.ps.hlsl shader and WaterQuality >= 1.
// WorldBuilder keeps the classic water.

#define REALWATER_F0					0.02f	///< Fresnel at normal incidence (water, n = 1.33)
#define REALWATER_REFRACTION_UV			0.025f	///< refraction offset in texture coordinates per wave slope
#define REALWATER_WAVE_TILT				0.30f	///< tilt of the wave normals
#define REALWATER_WAVE_SCALE_1			0.25f	///< layer 1: 64 world units per tile (uv2 = world / 16)
#define REALWATER_WAVE_SCALE_2			0.40f	///< layer 2: 40 world units per tile
#define REALWATER_REFLECTION_EDGE		1.5f	///< depth in world units over which the reflection fades in at the shore
#define REALWATER_NORMALMAP_SIZE		128
#define REALWATER_BODY_GAIN				1.08f	///< water color slightly brighter than the texture (scattered light)

/// Reason why realistic water is off (0 = on); logged whenever it changes.
static Int realWaterState(void)
{
	const WaterTransparencySetting *wt = TheWaterTransparency;
	if (!wt || !TheGlobalData || !TheWaterRenderObj || (TheGlobalData->m_isWorldBuilder && !TheGlobalData->m_editorGameGraphics))
		return 1;
	if (TheGlobalData->m_waterQuality <= 0 || !TheGlobalData->m_showSoftWaterEdge)
		return 2;
	// Maps that disable the soft edge, blend additively or set turbidity to 0 keep the classic water.
	if (wt->m_transparentWaterDepth == 0 || wt->m_turbidityDepth <= 0 || wt->m_additiveBlend)
		return 3;
	if (RenderDevice::getBackBufferFormat() != WW3D_FORMAT_A8R8G8B8)
		return 5;
	return 0;
}

Bool WaterRenderObjClass::useDepthCodedAlpha(void)
{
	Int state = realWaterState();
	if (state == 0 && (!TheWaterRenderObj->m_realWaterPixelShader || !TheWaterRenderObj->m_trapezoidWaterPixelShader ||
		!TheWaterRenderObj->m_waveNormalTexture))
		state = 4;
	static Int lastState = -1;
	if (state != lastState)
	{
		static const char *reasons[] = { "an", "no water object/WorldBuilder", "water level 0 or without soft edge",
			"map: TurbidityDepth 0, TransparentWaterDepth 0 or additive", "shader or wave map missing", "no destination alpha in the frame buffer" };
		DEBUG_LOG(("Water: realistic water %s (level %d)\n", reasons[state], TheGlobalData ? TheGlobalData->m_waterQuality : -1));
		lastState = state;
	}
	return state == 0;
}

/** Transmission per color channel after TurbidityDepth, clamped to a numerically safe range. */
static Real turbidityTransmit(const WaterTransparencySetting *wt, Int channel)
{
	const Real t[3] = { wt->m_turbidityColor.red, wt->m_turbidityColor.green, wt->m_turbidityColor.blue };
	return WWMath::Clamp(t[channel], 0.01f, 0.999f);
}

/** Effective visibility depth (reference for TurbidityColor). If the map wants opaque water
(TransparentWaterMinOpacity 1, opaque from TransparentWaterDepth on), the water must not be clearer:
at most 20 % of the ground (channel mean) survives the way down and back at that depth. Averaged over
the shore zone this matches the brightness of the classic linear ramp. Maps with translucent water
(minimum opacity below 1, e.g. Tournament Lake) keep TurbidityDepth. */
static Real effectiveTurbidityDepth(const WaterTransparencySetting *wt)
{
	Real depth = wt->m_turbidityDepth;
	if (wt->m_minWaterOpacity >= 0.99f && wt->m_transparentWaterDepth > 0.0f)
	{
		Real meanSigma = 0.0f;
		for (Int c = 0; c < 3; c++)
			meanSigma += -static_cast<Real>(log(turbidityTransmit(wt, c))) / depth;
		meanSigma /= 3.0f;
		const Real required = 1.609438f / (2.0f * wt->m_transparentWaterDepth);	// ln 5 over the path 2 * depth
		if (meanSigma < required)
			depth *= meanSigma / required;
	}
	return depth;
}

Real WaterRenderObjClass::getDepthCodeRange(void)
{
	// Three visibility depths: less than 1 % of the ground is left, deeper water looks the same.
	const WaterTransparencySetting *wt = TheWaterTransparency;
	Real range = wt ? 3.0f * effectiveTurbidityDepth(wt) : 30.0f;
	if (range < 16.0f) range = 16.0f;
	if (range > 160.0f) range = 160.0f;
	return range;
}

Real WaterRenderObjClass::getFrameAlphaClearValue(void)
{
	if (useDepthCodedAlpha())
		return 1.0f;	// without a shore tile: deeper than getDepthCodeRange()
	const WaterTransparencySetting *wt = TheWaterTransparency;
	return wt ? wt->m_minWaterOpacity : 1.0f;
}

/** Tileable normal map from a few sine waves with integer wave numbers. Red and green hold the
slope in X and Y (0.5 = flat). Computed deterministically so every run shows the same images. */
void WaterRenderObjClass::createWaveNormalTexture(void)
{
	REF_PTR_RELEASE(m_waveNormalTexture);
	const Int size = REALWATER_NORMALMAP_SIZE;
	struct Wave { Real kx, ky, amp, phase; };
	static const Wave waves[] =
	{
		{  1.0f,  2.0f, 1.00f, 0.0f },
		{  3.0f, -1.0f, 0.60f, 1.3f },
		{ -2.0f,  3.0f, 0.45f, 2.1f },
		{  5.0f,  1.0f, 0.30f, 4.0f },
		{ -4.0f, -5.0f, 0.20f, 0.7f },
		{  7.0f, -3.0f, 0.15f, 5.2f },
		{  2.0f,  9.0f, 0.10f, 3.3f },
	};
	const Int numWaves = sizeof(waves) / sizeof(waves[0]);
	// Largest possible slope, so the map uses the full value range without clipping.
	Real maxSlope = 0.0f;
	for (Int w = 0; w < numWaves; w++)
		maxSlope += waves[w].amp * static_cast<Real>(sqrt(waves[w].kx * waves[w].kx + waves[w].ky * waves[w].ky));

	m_waveNormalTexture = MSGNEW("TextureClass") TextureClass(size, size, WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_1,
		TextureClass::POOL_MANAGED, false, false);
	SurfaceClass *surf = m_waveNormalTexture->Get_Surface_Level();
	if (!surf)
	{	REF_PTR_RELEASE(m_waveNormalTexture);
		return;
	}
	Int pitch;
	UnsignedByte *bits = static_cast<UnsignedByte *>(surf->Lock(&pitch));
	if (bits)
	{
		for (Int y = 0; y < size; y++)
		{
			UnsignedInt *row = reinterpret_cast<UnsignedInt *>(bits + y * pitch);
			for (Int x = 0; x < size; x++)
			{
				const Real u = static_cast<Real>(x) / static_cast<Real>(size);
				const Real v = static_cast<Real>(y) / static_cast<Real>(size);
				Real sx = 0.0f, sy = 0.0f;
				for (Int w = 0; w < numWaves; w++)
				{
					const Real c = static_cast<Real>(cos(2.0f * PI * (waves[w].kx * u + waves[w].ky * v) + waves[w].phase));
					sx += waves[w].amp * waves[w].kx * c;
					sy += waves[w].amp * waves[w].ky * c;
				}
				Int r = REAL_TO_INT(127.5f - 127.5f * sx / maxSlope);
				Int g = REAL_TO_INT(127.5f - 127.5f * sy / maxSlope);
				if (r < 0) r = 0; if (r > 255) r = 255;
				if (g < 0) g = 0; if (g > 255) g = 255;
				row[x] = 0xff000000 | (r << 16) | (g << 8) | 0xff;
			}
		}
		surf->Unlock();
	}
	REF_PTR_RELEASE(surf);
	m_waveNormalTexture->Get_Filter().Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_REPEAT);
	m_waveNormalTexture->Get_Filter().Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_REPEAT);
}

void WaterRenderObjClass::releaseSceneCopy(void)
{
	if (m_sceneCopyTexture)
	{	RenderDevice::Set_Device_Texture(4, nullptr);
		RenderDevice::Release_Resource(m_sceneCopyTexture);
	}
	m_sceneCopyTexture = nullptr;
	m_sceneCopyWidth = m_sceneCopyHeight = m_sceneCopyFormat = 0;
	m_sceneCopyValid = FALSE;
}

/** Copy the current color target (ground, objects, depth code in alpha) into the scene copy. The copy
follows size and format of the target, also when a screen filter renders into a texture. */
Bool WaterRenderObjClass::updateSceneCopy(void)
{
	WW3DSurfaceHandle target = nullptr;
	if (RenderDevice::Get_Render_Target(&target) != S_OK || !target)
		return FALSE;
	WW3DSurfaceDesc desc;
	::memset(&desc, 0, sizeof(desc));
	HRESULT hr = RenderDevice::Get_Surface_Desc(target, &desc);
	RenderDevice::Release_Resource(target);
	if (hr != S_OK || desc.Width == 0 || desc.Height == 0)
		return FALSE;

	if (!m_sceneCopyTexture || desc.Width != m_sceneCopyWidth || desc.Height != m_sceneCopyHeight || desc.Format != m_sceneCopyFormat)
	{
		releaseSceneCopy();
		if (RenderDevice::Create_Device_Render_Target_Texture(desc.Width, desc.Height, static_cast<WW3DDeviceFormat>(desc.Format), &m_sceneCopyTexture) != S_OK)
		{	m_sceneCopyTexture = nullptr;
			return FALSE;
		}
		m_sceneCopyWidth = desc.Width;
		m_sceneCopyHeight = desc.Height;
		m_sceneCopyFormat = desc.Format;
	}
	return RenderDevice::Copy_Render_Target_To_Texture(m_sceneCopyTexture) ? TRUE : FALSE;
}

/** After setupFlatWaterShader: swap the shader, bind scene copy and waves, set constants.
FALSE if realistic water is not possible here; the caller then draws the classic water. */
Bool WaterRenderObjClass::setupRealWaterShader(void)
{
	if (!useDepthCodedAlpha())
		return FALSE;
	if (m_sceneCopyPending)
	{	m_sceneCopyPending = FALSE;
		m_sceneCopyValid = updateSceneCopy();
	}
	if (!m_sceneCopyValid)
		return FALSE;

	const WaterTransparencySetting *wt = TheWaterTransparency;

	// Stage 1 supplies the camera space position (the sparkles read the stage 0 coordinates).
	RenderDevice::Set_Texture_Stage(1, TextureStageDesc().Tex_Coord_Index(WW3D_TCI_CAMERASPACEPOSITION|1).Tex_Transform(WW3D_TTFF_COUNT3));
	Matrix4x4 identity(true);
	RenderDevice::_Set_Device_Transform(WW3D_TS_TEXTURE1, identity);

	RenderDevice::Set_Device_Texture(4, m_sceneCopyTexture);
	RenderDevice::Set_Sampler_State(4, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_CLAMP);
	RenderDevice::Set_Sampler_State(4, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_CLAMP);
	RenderDevice::Set_Sampler_State(4, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Sampler_State(4, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Sampler_State(4, WW3D_SAMP_MIPFILTER, WW3D_TEXF_NONE);
	// After a device loss the computed texture is empty (like m_whiteTexture): recompute it.
	if (!m_waveNormalTexture->Is_Initialized())
		createWaveNormalTexture();
	if (!m_waveNormalTexture)
		return FALSE;
	RenderDevice::Set_Device_Texture(5, m_waveNormalTexture->Peek_D3D_Texture());
	RenderDevice::Set_Sampler_State(5, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
	RenderDevice::Set_Sampler_State(5, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);
	RenderDevice::Set_Sampler_State(5, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Sampler_State(5, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Sampler_State(5, WW3D_SAMP_MIPFILTER, WW3D_TEXF_NONE);

	// Projection and view come from cbFrame (application projection without raster offset, so the
	// texture coordinate hits the center of the matching scene copy pixel).
	WW3DViewport vp;
	RenderDevice::Get_Backend()->Get_Viewport(&vp);
	const Real w = static_cast<Real>(m_sceneCopyWidth), h = static_cast<Real>(m_sceneCopyHeight);
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Screen), Vector4(0.5f * vp.Width / w, -0.5f * vp.Height / h,
		(vp.X + 0.5f * vp.Width) / w, (vp.Y + 0.5f * vp.Height) / h));

	// Only w; xyz of these registers are the view axes from cbFrame.
	const Real refraction = (TheGlobalData->m_waterQuality >= 2) ? REALWATER_REFRACTION_UV * wt->m_refractionStrength : 0.0f;
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2AxisZ), Vector4(0.0f, 0.0f, 0.0f, REALWATER_F0));
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2AxisX), Vector4(0.0f, 0.0f, 0.0f, refraction));
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2AxisY), Vector4(0.0f, 0.0f, 0.0f, getDepthCodeRange()));

	// Beer-Lambert: TurbidityColor is the transmission per channel after the effective visibility depth.
	Real sigma[3];
	const Real turbidityDepth = effectiveTurbidityDepth(wt);
	for (Int c = 0; c < 3; c++)
		sigma[c] = -static_cast<Real>(log(turbidityTransmit(wt, c))) / turbidityDepth;
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Sigma), Vector4(sigma[0], sigma[1], sigma[2], wt->m_reflectionStrength));

	// Waves: two layers moving against each other (render clock via m_riverVOrigin). Only the
	// fractional part, so the coordinates stay small.
	const Real t = m_riverVOrigin;
	auto frac = [](Real x) { return x - static_cast<Real>(floor(x)); };
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Scroll), Vector4(frac(t * 0.65f), frac(t * 0.35f),
		frac(-t * 0.45f), frac(t * 0.55f)));
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Wave), Vector4(REALWATER_WAVE_SCALE_1, REALWATER_WAVE_SCALE_2,
		REALWATER_WAVE_TILT, 1.0f / REALWATER_REFLECTION_EDGE));

	// Sky color for the reflection: a pale blue lit like the terrain (dark at night).
	Real skyR = TheGlobalData->m_terrainAmbient[0].red + TheGlobalData->m_terrainDiffuse[0].red;
	Real skyG = TheGlobalData->m_terrainAmbient[0].green + TheGlobalData->m_terrainDiffuse[0].green;
	Real skyB = TheGlobalData->m_terrainAmbient[0].blue + TheGlobalData->m_terrainDiffuse[0].blue;
	skyR = WWMath::Clamp(skyR, 0.0f, 1.0f) * 0.62f;
	skyG = WWMath::Clamp(skyG, 0.0f, 1.0f) * 0.70f;
	skyB = WWMath::Clamp(skyB, 0.0f, 1.0f) * 0.80f;
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Sky), Vector4(skyR, skyG, skyB, REALWATER_BODY_GAIN));

	// Water version 2
	m_water2Active = FALSE;
	if (water2Measurement(FALSE) != 1)
	{
		m_water2Active = setupRealWater2();
		if (!m_water2Active)
			RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Env), Vector4(0.0f, 0.0f, 0.0f, 0.0f));	// shader computes version 1
	}

	RenderDevice::Set_Pixel_Shader_Object(m_realWaterPixelShader);
	return TRUE;
}

void WaterRenderObjClass::cleanupRealWaterShader(void)
{
	RenderDevice::Set_Device_Texture(4, nullptr);
	RenderDevice::Set_Device_Texture(5, nullptr);
	RenderDevice::Set_Texture_Stage(1, TextureStageDesc().Tex_Coord_Index(0).Tex_Transform(WW3D_TTFF_DISABLE));
	if (m_water2Active)
	{	// version 2: unbind its own textures and restore the samplers of the classic path
		RenderDevice::Set_Device_Texture(1, nullptr);
		RenderDevice::Set_Device_Texture(2, nullptr);
		RenderDevice::Set_Device_Texture(7, nullptr);
		RenderDevice::Set_Device_Texture(6, nullptr);
		for (Int stage = 1; stage <= 5; stage += (stage == 2) ? 3 : 1)
		{	RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
			RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
		}
		RenderDevice::Set_Sampler_State(5, WW3D_SAMP_MIPFILTER, WW3D_TEXF_NONE);
		RenderDevice::Set_Sampler_State(2, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
		RenderDevice::Set_Sampler_State(2, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);
		m_water2Active = FALSE;
	}
}

//=================================================================================================
// Water version 2
//=================================================================================================
// Only together with realistic water (useDepthCodedAlpha). The shader sm4/realwater.ps.hlsl then computes:
// waves from a slope map in three layers with large-scale variation, reflection of an environment map
// per time of day (CC0 HDRIs), sun glint from the light direction, foam and surf from the water depth
// (scene depth copy) and, from quality 2 on, a screen space reflection. Visual only; no effect on game logic.

#define WATER2_TILE_A			96.0f	///< world units per tile of layer A (wind waves, peak at 2.4 units)
#define WATER2_TILE_B			27.0f	///< layer B (ripples), runs against layer A
#define WATER2_TILE_A2			157.0f	///< layer A again, rotated 37 degrees and larger (breaks up the tiling)
#define WATER2_TILE_VARIATION	1300.0f	///< large-scale variation (calm and rough patches)
#define WATER2_SPEED_A			2.2f	///< world units per second
#define WATER2_SPEED_B			1.3f
#define WATER2_SPEED_A2			1.5f
#define WATER2_SLOPE_A			0.120f	///< mean slope of layer A
#define WATER2_SLOPE_B			0.065f
#define WATER2_SLOPE_A2			0.045f
#define WATER2_VARIATION		0.85f	///< share of variation in wave strength (0 = uniform)
#define WATER2_ENV_LOD			1.5f	///< mip level of the environment map (blurred like a moving surface)
#define WATER2_SKY_GAIN			0.70f	///< sky brightness as in version 1 (light * 0.62..0.80)
#define WATER2_SKY_TINT			0.5f	///< share of the map light color in the sky (0 = HDRI only)
#define WATER2_GLINT			1.0f	///< sun glint
#define WATER2_GLINT_BROAD		1.0f	///< share of the broad shimmer
#define WATER2_GLINT_EDGE		0.8f	///< depth in world units at which the glint is full
#define WATER2_FOAM				0.80f	///< foam opacity (0 = no foam, the classic surf lines stay)
#define WATER2_FOAM_WIDTH		6.0f	///< width of the fringe at the waterline (world units, horizontal)
#define WATER2_FOAM_SPACING		5.0f	///< spacing of the wave fronts (world units, horizontal)
#define WATER2_FOAM_REACH		2.5f	///< wave fronts up to fringe width * reach
#define WATER2_FOAM_TILE		60.0f	///< world units per tile of the foam pattern
#define WATER2_FOAM_FRONTS		0.30f	///< wave fronts per second
#define WATER2_DEEP_DARKEN		0.22f	///< how much darker deep water is
#define WATER2_EA_PATTERN		0.85f	///< share of the painted wave pattern of the water texture (rest: its mean)
#define WATER2_REFLECTION_GAIN	2.4f	///< reflection stronger than Fresnel with F0 = 0.02 (else invisible from above)
#define WATER2_SSR_STRENGTH		1.0f	///< screen space reflection with RENDER_EFFECT_WATER_SSR (quality 2 only)
#define WATER2_SSR_LENGTH		90.0f	///< world units the reflected ray is traced
#define WATER2_SSR_STEPS		12.0f	///< steps (at most 16, shader limit)
#define WATER2_WIND_TILE		650.0f	///< world units per tile of the wind fields
#define WATER2_WIND_DRIFT		6.0f	///< world units per second the gust fields drift with the wind
#define WATER2_CALM				0.30f	///< roughness between gusts (0 = mirror smooth)
#define WATER2_PAWS				0.75f	///< strength of the fine ripple patches
#define WATER2_LEE_DIST			40.0f	///< upwind distance for the wind shadow (world units); fetch 3x and 7x of it
#define WATER2_RIVER_CYCLE		2.0f	///< seconds per flow map cycle (rivers)
#define WATER2_RIVER_REFLECTION	1.0f	///< reflection strength on rivers (times the map ReflectionStrength)

/** Measurement helper: environment variable CNC_WASSER_MESSUNG = n draws every standing water surface
n times (repeat = TRUE returns n), CNC_WASSER_FASSUNG = 1 forces version 1 (repeat = FALSE returns the version).
Read once on the first call; without the variables 1 or 0, i.e. no effect. */
Int water2Measurement(Bool repeat)
{
	static Int count = -1, version = -1;
	if (count < 0)
	{
		const char *n = getenv("CNC_WASSER_MESSUNG");
		const char *v = getenv("CNC_WASSER_FASSUNG");
		count = n ? atoi(n) : 1;
		if (count < 1) count = 1;
		if (count > 200) count = 200;
		version = v ? atoi(v) : 0;
		if (count > 1 || version)
			DEBUG_LOG(("Water: measuring aid active, %d passes, version %d\n", count, version));
	}
	return repeat ? count : version;
}

/** A texture from ProjectData\Textures\Wasser: 32 bit A8R8G8B8 DDS with mip chain. Read through the game
file system (loose file next to the exe), because the WW3D file factory would prefix "Art\Textures\". */
static TextureClass *loadWater2Dds(const char *name)
{
	AsciiString path;
	path.format("ProjectData\\Textures\\Wasser\\%s", name);
	File *file = TheFileSystem ? TheFileSystem->openFile(path.str(), File::READ | File::BINARY) : nullptr;
	// The game runs in its exe folder, where ProjectData lies; the map editor runs in the game data
	// folder and has ProjectData next to its exe, as the terrain detail and rock textures expect.
	if (!file && TheFileSystem)
	{
		char exePath[MAX_PATH];
		const DWORD exeLength = ::GetModuleFileNameA(nullptr, exePath, MAX_PATH);
		char *slash = (exeLength > 0 && exeLength < MAX_PATH) ? strrchr(exePath, '\\') : nullptr;
		if (slash)
		{
			slash[1] = 0;
			AsciiString full;
			full.format("%s%s", exePath, path.str());
			file = TheFileSystem->openFile(full.str(), File::READ | File::BINARY);
		}
	}
	if (!file)
	{	DEBUG_LOG(("Water: %s missing\n", path.str()));
		return nullptr;
	}
	const Int size = file->size();
	std::vector<UnsignedByte> data(size > 128 ? static_cast<size_t>(size) : 0);
	const Bool ok = !data.empty() && file->read(data.data(), size) == size;
	file->close();
	if (!ok || ::memcmp(data.data(), "DDS ", 4) != 0)
	{	DEBUG_LOG(("Water: %s is not a DDS\n", path.str()));
		return nullptr;
	}
	UnsignedInt h[31];		// DDS_HEADER without magic
	::memcpy(h, data.data() + 4, sizeof(h));
	const UnsignedInt height = h[2], width = h[3], levels = h[6] ? h[6] : 1;
	if (h[21] != 32 || h[22] != 0x00FF0000u || h[23] != 0x0000FF00u || h[24] != 0x000000FFu || h[25] != 0xFF000000u ||
		width < 4 || height < 4 || width > 2048 || height > 2048 || levels > 12)
	{	DEBUG_LOG(("Water: %s does not have the expected format (A8R8G8B8)\n", path.str()));
		return nullptr;
	}
	TextureClass *tex = MSGNEW("TextureClass") TextureClass(width, height, WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_ALL,
		TextureClass::POOL_MANAGED, false, false);
	// Get_Mip_Level_Count returns 0 for MIP_LEVELS_ALL (the enum, not the count): query the levels of the created
	// texture from the device, otherwise all levels stay unfilled (garbage in the image).
	const UnsignedInt texLevels = tex->Peek_D3D_Texture() ? RenderDevice::Get_Texture_Level_Count(tex->Peek_D3D_Texture()) : 0;
	if (texLevels == 0 || texLevels > levels)
	{	DEBUG_LOG(("Water: %s has %u mip levels, the texture %u -- not used\n", path.str(), levels, texLevels));
		REF_PTR_RELEASE(tex);
		return nullptr;
	}
	size_t offset = 128;
	UnsignedInt w = width, hh = height;
	for (UnsignedInt level = 0; level < levels && level < texLevels; level++)
	{
		const size_t bytes = static_cast<size_t>(w) * hh * 4;
		if (offset + bytes > data.size())
			break;
		SurfaceClass *surf = tex->Get_Surface_Level(level);
		Int pitch = 0;
		UnsignedByte *bits = surf ? static_cast<UnsignedByte *>(surf->Lock(&pitch)) : nullptr;
		if (bits)
		{	for (UnsignedInt y = 0; y < hh; y++)
				::memcpy(bits + y * pitch, data.data() + offset + static_cast<size_t>(y) * w * 4, static_cast<size_t>(w) * 4);
			surf->Unlock();
		}
		REF_PTR_RELEASE(surf);
		offset += bytes;
		w = (w > 1) ? w / 2 : 1;
		hh = (hh > 1) ? hh / 2 : 1;
	}
	return tex;
}

Bool UseFreeWaterTextures()
{
	if (TheGlobalData->m_waterTexture < 0)
	{
		Int choice = 0;
		if (!TheGlobalData->m_isWorldBuilder)
		{
			choice = (UserSettings::getText("Detail.WaterTexture", AsciiString::TheEmptyString).compareNoCase("ea") == 0) ? 0 : 1;
		}
		TheWritableGlobalData->m_waterTexture = choice;
		DEBUG_LOG(("Water: textures %s\n", choice ? "free (ProjectData)" : "from the game data"));
	}
	return TheGlobalData->m_waterTexture == 1;
}

TextureClass *LoadFreeWaterTexture(const char *freeName, const char *gameDataName)
{
	TextureClass *tex = UseFreeWaterTextures() ? loadWater2Dds(freeName) : nullptr;
	if (!tex)
		tex = WW3DAssetManager::Get_Instance()->Get_Texture(gameDataName);
	return tex;
}

Bool WaterRenderObjClass::loadWater2Textures(void)
{
	if (m_water2Tried)
		return m_water2Waves && m_water2Foam && m_water2Wind && m_water2Sky[0] && m_water2Sky[1] && m_water2Sky[2] && m_water2Sky[3];
	m_water2Tried = TRUE;
	m_water2Waves = loadWater2Dds("wellen.dds");
	m_water2Foam = loadWater2Dds("schaum.dds");
	m_water2Wind = loadWater2Dds("wind.dds");
	static const char *const sky[4] = { "himmel_morgen.dds", "himmel_tag.dds", "himmel_abend.dds", "himmel_nacht.dds" };
	for (Int i = 0; i < 4; i++)
		m_water2Sky[i] = loadWater2Dds(sky[i]);
	const Bool ok = m_water2Waves && m_water2Foam && m_water2Wind && m_water2Sky[0] && m_water2Sky[1] && m_water2Sky[2] && m_water2Sky[3];
	DEBUG_LOG(("Water: version 2 (waves, sky, foam) %s\n", ok ? "loaded" : "not available, staying with version 1"));
	return ok;
}

void WaterRenderObjClass::releaseWater2Textures(void)
{
	REF_PTR_RELEASE(m_water2Waves);
	REF_PTR_RELEASE(m_water2Foam);
	REF_PTR_RELEASE(m_water2Wind);
	for (Int i = 0; i < 4; i++)
		REF_PTR_RELEASE(m_water2Sky[i]);
	m_water2Tried = FALSE;
}

/** Color texture of standing water (river FALSE) or of rivers. By default a free tile from
ProjectData\Textures\Wasser, chosen by the name of the texture in StandingWaterTexture (Water.ini, map.ini):
TSWater03 is the muddy water of some maps, TWWater01 and TSWater the clear water; rivers get their own tile.
-waterTexture ea (Options.ini "WaterTexture = ea") keeps the texture of the game data, as do other names (mods)
and missing files. The map editor keeps it unless the switch is given. */
TextureClass *WaterRenderObjClass::waterColorTexture(Bool river)
{
	if (!UseFreeWaterTextures() || !TheWaterTransparency)
		return m_riverTexture;

	AsciiString base = TheWaterTransparency->m_standingWaterTexture;
	base.toLower();
	Int index;
	if (base == "tswater03.tga" || base == "tswater03.dds")
		index = 2;
	else if (base == "twwater01.tga" || base == "twwater01.dds" || base == "tswater.tga" || base == "tswater.dds")
		index = river ? 1 : 0;
	else
		return m_riverTexture;

	if (!m_freeWaterTried[index])
	{
		static const char *const names[3] = { "wasser_see.dds", "wasser_fluss.dds", "wasser_trueb.dds" };
		m_freeWaterTried[index] = TRUE;
		m_freeWaterTexture[index] = loadWater2Dds(names[index]);
		DEBUG_LOG(("Water: %s %s\n", names[index], m_freeWaterTexture[index] ? "loaded" : "missing, keeping the texture of the game data"));
	}
	return m_freeWaterTexture[index] ? m_freeWaterTexture[index] : m_riverTexture;
}

void WaterRenderObjClass::releaseFreeWaterTextures(void)
{
	for (Int i = 0; i < 3; i++)
	{	REF_PTR_RELEASE(m_freeWaterTexture[i]);
		m_freeWaterTried[i] = FALSE;
	}
}

Bool WaterRenderObjClass::useShoreFoam(void)
{
	if (WATER2_FOAM <= 0.0f || !useDepthCodedAlpha())
		return FALSE;
	return TheWaterRenderObj->loadWater2Textures();
}

/** After setupRealWaterShader (version 1 is set up): set textures and constants of version 2. FALSE if the
textures are missing; the shader then computes version 1. */
Bool WaterRenderObjClass::setupRealWater2(void)
{
	if (!loadWater2Textures())
		return FALSE;
	if (!m_water2Waves->Is_Initialized() || !m_water2Foam->Is_Initialized() || !m_water2Wind->Is_Initialized())
		return FALSE;

	// Depth copy of the opaque scene, available from the next frame on. Soft particles create it anyway;
	// if it is missing (multisampling, other target), the depth code in destination alpha is used.
	RenderDevice::Request_Scene_Copies(RENDER_SCENE_COPY_DEPTH);
	WW3DTextureHandle depthCopy = RenderDevice::Get_Scene_Depth_Copy();

	Int tod = TheGlobalData->m_timeOfDay - TIME_OF_DAY_FIRST;
	if (tod < 0 || tod > 3)
		tod = 1;
	TextureClass *sky = m_water2Sky[tod];

	// Stage 1: foam and variation (instead of sparkles), 2: environment map (instead of noise), 5: waves, 7: depth
	RenderDevice::Set_Device_Texture(1, m_water2Foam->Peek_D3D_Texture());
	RenderDevice::Set_Sampler_State(1, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
	RenderDevice::Set_Sampler_State(1, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);
	RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MINFILTER, WW3D_TEXF_ANISOTROPIC);
	RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MIPFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Device_Texture(2, sky->Peek_D3D_Texture());
	RenderDevice::Set_Sampler_State(2, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_CLAMP);
	RenderDevice::Set_Sampler_State(2, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_CLAMP);
	RenderDevice::Set_Sampler_State(2, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Sampler_State(2, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Sampler_State(2, WW3D_SAMP_MIPFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Device_Texture(5, m_water2Waves->Peek_D3D_Texture());
	RenderDevice::Set_Sampler_State(5, WW3D_SAMP_MINFILTER, WW3D_TEXF_ANISOTROPIC);
	RenderDevice::Set_Sampler_State(5, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Sampler_State(5, WW3D_SAMP_MIPFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Device_Texture(7, depthCopy);
	RenderDevice::Set_Device_Texture(6, m_water2Wind->Peek_D3D_Texture());
	RenderDevice::Set_Sampler_State(6, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
	RenderDevice::Set_Sampler_State(6, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);
	RenderDevice::Set_Sampler_State(6, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Sampler_State(6, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Sampler_State(6, WW3D_SAMP_MIPFILTER, WW3D_TEXF_LINEAR);

	Matrix4x4 view;
	RenderDevice::_Get_Device_Transform(WW3D_TS_VIEW, view);
	auto toView = [&view](Real x, Real y, Real z) {
		return Vector3(x * view[0][0] + y * view[1][0] + z * view[2][0],
					   x * view[0][1] + y * view[1][1] + z * view[2][1],
					   x * view[0][2] + y * view[1][2] + z * view[2][2]);
	};
	auto frac = [](double x) { return static_cast<Real>(x - floor(x)); };

	// Sun: direction towards map light 0 (m_terrainLightPos is the direction the light travels)
	Vector3 sun(-TheGlobalData->m_terrainLightPos[0].x, -TheGlobalData->m_terrainLightPos[0].y, -TheGlobalData->m_terrainLightPos[0].z);
	if (sun.Length2() < 1e-6f)
		sun.Set(0.0f, 0.0f, 1.0f);
	sun.Normalize();
	const Vector3 sunView = toView(sun.X, sun.Y, sun.Z);
	const Real glint = WATER2_GLINT * WWMath::Clamp(sun.Z * 8.0f, 0.0f, 1.0f);	// sun below the horizon: no glint
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Sun), Vector4(sunView.X, sunView.Y, sunView.Z, glint));
	const RGBColor &diffuse = TheGlobalData->m_terrainDiffuse[0];
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2SunColor), Vector4(WWMath::Clamp(diffuse.red, 0.0f, 1.5f),
		WWMath::Clamp(diffuse.green, 0.0f, 1.5f), WWMath::Clamp(diffuse.blue, 0.0f, 1.5f), WATER2_GLINT_BROAD));

	// Sky: rotate the environment map so its sun (direction +x) lines up with the map sun
	Real sunXY = static_cast<Real>(sqrt(sun.X * sun.X + sun.Y * sun.Y));
	const Real sunCos = (sunXY > 1e-4f) ? sun.X / sunXY : 1.0f, sunSin = (sunXY > 1e-4f) ? sun.Y / sunXY : 0.0f;
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Env), Vector4(sunCos, sunSin, 1.0f, depthCopy ? 1.0f : 0.0f));

	// Sky tint: map light as in version 1 (dark at night), color half from the HDRI
	Real light[3] = { TheGlobalData->m_terrainAmbient[0].red + diffuse.red, TheGlobalData->m_terrainAmbient[0].green + diffuse.green,
		TheGlobalData->m_terrainAmbient[0].blue + diffuse.blue };
	for (Int c = 0; c < 3; c++)
		light[c] = WWMath::Clamp(light[c], 0.0f, 1.0f);
	const Real lum = 0.2126f * light[0] + 0.7152f * light[1] + 0.0722f * light[2];
	Real tint[3];
	for (Int c = 0; c < 3; c++)
		tint[c] = (lum + (light[c] - lum) * WATER2_SKY_TINT) * WATER2_SKY_GAIN;
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Light), Vector4(tint[0], tint[1], tint[2], WATER2_FOAM));

	// Waves: scales (uv per world unit) and offsets from the render clock
	const double t = GameEngine_RenderTimeMs() / 1000.0;
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Scale), Vector4(1.0f / WATER2_TILE_A, 1.0f / WATER2_TILE_B, 1.0f / WATER2_TILE_A2,
		1.0f / WATER2_TILE_VARIATION));
	const double moveA = t * WATER2_SPEED_A / WATER2_TILE_A, moveB = t * WATER2_SPEED_B / WATER2_TILE_B;
	const double moveA2 = t * WATER2_SPEED_A2 / WATER2_TILE_A2;
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Move), Vector4(frac(moveA * 0.866), frac(moveA * 0.5),		// wind from 30 degrees
		frac(-moveB * 0.342), frac(-moveB * 0.940)));											// ripples from 250 degrees
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Move2), Vector4(frac(moveA2 * 0.94), frac(-moveA2 * 0.34), m_riverVOrigin,
		frac(t * WATER2_FOAM_FRONTS)));
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Amp), Vector4(WATER2_SLOPE_A, WATER2_SLOPE_B, WATER2_SLOPE_A2, WATER2_VARIATION));

	// Foam
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Foam), Vector4(WATER2_FOAM_WIDTH, WATER2_FOAM_SPACING, WATER2_FOAM_REACH,
		1.0f / WATER2_FOAM_TILE));
	// xy (depth range) come from cbFrame.
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Depth), Vector4(0.0f, 0.0f, frac(t * 0.011), frac(t * 0.007)));

	// Screen space reflection only with its effect, from quality 2 on and with a depth copy
	const Bool ssr = depthCopy && TheGlobalData->m_waterQuality >= 2 && (TheGlobalData->m_renderEffects & RENDER_EFFECT_WATER_SSR);
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Ssr), Vector4(ssr ? WATER2_SSR_STRENGTH : 0.0f, WATER2_SSR_LENGTH, WATER2_SSR_STEPS,
		WATER2_REFLECTION_GAIN));
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Shade), Vector4(WATER2_DEEP_DARKEN, WATER2_GLINT_EDGE, WATER2_ENV_LOD, WATER2_EA_PATTERN));
	// Wind: direction as layer A (30 degrees), the fields drift with the wind
	const Real windCos = 0.8660254f, windSin = 0.5f;
	const double drift = t * WATER2_WIND_DRIFT / WATER2_WIND_TILE;
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Wind), Vector4(windCos, windSin, 1.0f / WATER2_WIND_TILE, WATER2_CALM));
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Wind2), Vector4(frac(-drift * windCos), frac(-drift * windSin), WATER2_PAWS, WATER2_LEE_DIST));
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2DiagFlow), Vector4(water2Measurement(FALSE) >= 8 ? static_cast<Real>(water2Measurement(FALSE) - 7) : 0.0f,
		0.0f, 0.0f, 0.0f));	// diagnostics: 8 shows depths, 9 foam and glint as color

	static Bool logged = FALSE;
	if (!logged)
	{	logged = TRUE;
		DEBUG_LOG(("Water: version 2 on (sky %d, depth copy %s, screen-space reflection %s)\n", tod,
			depthCopy ? "yes" : "no", ssr ? "on" : "off"));
	}
	return TRUE;
}

/** Rivers (drawRiverWater after setupJbaWaterShader): riverwater2.ps.hlsl with the textures and constants of
version 2. FALSE without shader, textures or below water quality 1; riverwater.ps.hlsl is used then. */
Bool WaterRenderObjClass::setupRiverWater2(void)
{
	if (!m_riverWater2PixelShader || !TheGlobalData ||
		(TheGlobalData->m_isWorldBuilder && !TheGlobalData->m_editorGameGraphics) || TheGlobalData->m_waterQuality <= 0 || !TheWaterTransparency)
		return FALSE;
	if (!setupRealWater2())
		return FALSE;
	m_water2Active = TRUE;

	// Stage 1: camera space position (view direction, depth); stage 3 keeps the alpha edge, stage 2 the world position.
	RenderDevice::Set_Texture_Stage(1, TextureStageDesc().Tex_Coord_Index(WW3D_TCI_CAMERASPACEPOSITION|1).Tex_Transform(WW3D_TTFF_COUNT3));
	Matrix4x4 identity(true);
	RenderDevice::_Set_Device_Transform(WW3D_TS_TEXTURE1, identity);

	// Projection and view axes come from cbFrame; register 6 carries only w.
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2AxisZ), Vector4(0.0f, 0.0f, 0.0f, REALWATER_F0));
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2AxisX), Vector4(0.0f, 0.0f, 0.0f, 0.0f));
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2AxisY), Vector4(0.0f, 0.0f, 0.0f, 0.0f));
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2Sigma), Vector4(0.0f, 0.0f, 0.0f, TheWaterTransparency->m_reflectionStrength * WATER2_RIVER_REFLECTION));

	// Measure the river texture progress (m_riverVOrigin grows per update, not per second) so the waves
	// flow as fast as the texture.
	const UnsignedInt now = GameEngine_RenderTimeMs();
	if (m_riverRateTime && now > m_riverRateTime + 250)
	{
		const Real rate = (m_riverVOrigin - m_riverRateOrigin) * 1000.0f / static_cast<Real>(now - m_riverRateTime);
		if (rate > 0.0f && rate < 10.0f)
			m_riverRate = 0.8f * m_riverRate + 0.2f * rate;
		m_riverRateTime = now;
		m_riverRateOrigin = m_riverVOrigin;
	}
	else if (!m_riverRateTime || now < m_riverRateTime)
	{	m_riverRateTime = now;
		m_riverRateOrigin = m_riverVOrigin;
	}
	const double t = now / 1000.0;
	const Real phase = static_cast<Real>(t / WATER2_RIVER_CYCLE - floor(t / WATER2_RIVER_CYCLE));
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderWater2PS, w2DiagFlow), Vector4(m_riverRate, phase, WATER2_RIVER_CYCLE, 0.0f));

	RenderDevice::Set_Pixel_Shader_Object(m_riverWater2PixelShader);
	return TRUE;
}
