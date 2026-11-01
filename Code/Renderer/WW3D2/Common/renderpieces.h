/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/
// renderpieces.h -- constant layouts of the drawing pieces that set their own shader constants.
//
// Screen filters, sea water and the like write their constants with RenderDevice::Set_*_Shader_Constant into the
// shared buffers on RENDER_SLOT_CB_PIECE_VS and RENDER_SLOT_CB_PIECE_PS. The pieces share these two slots because
// no two of them meet in one draw. Each layout here has a twin cbuffer of the same name and fields in its shader
// (Data/Shaders/sm4), checked by tools/render/plaetze_pruefen.py; RENDER_PIECE_REG gives the register a field
// starts at.
#pragma once

#include <cstddef>
#include "vector4.h"
#include "renderdevice.h"

#define RENDER_PIECE_REG(Layout, field) static_cast<int>(offsetof(Layout, field) / sizeof(Vector4))

/// Black and white screen filter (monochrome.ps.hlsl): luminance of the image, tinted, blended against the image.
struct RenderScreenFilterPS
{
	Vector4 lumWeights;		///< weights of the luminance (0.3, 0.59, 0.11)
	Vector4 tint;			///< color the luminance is multiplied with
	Vector4 fade;			///< blend factor against the original image
};

/// Sea water, vertex shader (wave.vs.hlsl). zero and one are written by the sea but read by no shader.
struct RenderSeaWaveVS
{
	Vector4 zero;
	Vector4 one;
	Vector4 worldViewProj[4];	///< transposed patch-to-clip matrix
	Vector4 texProj[4];			///< row 0: screen position to reflection texture coordinates
};

/// Sea water, pixel shader (wave.ps.hlsl).
struct RenderSeaWavePS
{
	Vector4 bumpEnvMat;		///< bump matrix of stage 1 (M00, M01, M10, M11); ps_4_0 has no texbem
};

/// Water version 2: lakes and sea surface (realwater.ps.hlsl, version 1 of the lakes reads w2Screen..w2Sky and
/// w2Env.z) and rivers (riverwater2.ps.hlsl); W3DWaterReal.cpp writes it.
struct RenderWater2PS
{
	Vector4 unused0[5];
	Vector4 w2Screen;	///< scene copy: xy scale, zw offset of the screen position
	Vector4 w2AxisZ;	///< w Fresnel F0 (xyz unused; the view axes come from cbFrame)
	Vector4 w2AxisX;	///< w refraction offset (uv)
	Vector4 w2AxisY;	///< w range of the depth code
	Vector4 w2Sigma;	///< xyz turbidity per world unit, w reflection strength
	Vector4 w2Scroll;	///< version 1: offsets of the wave layers
	Vector4 w2Wave;	///< version 1: wave scales, tilt, 1/reflection edge
	Vector4 w2Sky;	///< version 1: rgb sky color, w body gain
	Vector4 w2Sun;	///< xyz direction to the sun (camera space), w glint strength
	Vector4 w2SunColor;	///< rgb sun color, w share of the broad glint
	Vector4 w2Env;	///< xy cos/sin of the sun direction (sky rotation), z 1 = version 2, w depth copy valid
	Vector4 w2Light;	///< rgb sky tint, w foam strength
	Vector4 w2Scale;	///< uv per world unit: x layer A, y layer B, z layer A rotated, w variation
	Vector4 w2Move;	///< offsets: xy layer A, zw layer B
	Vector4 w2Move2;	///< xy layer A rotated, z offset of uv2, w surf phase
	Vector4 w2Amp;	///< slopes: x layer A, y layer B, z layer A rotated, w variation
	Vector4 w2Foam;	///< foam: fringe width, spacing of wave fronts, reach, 1/tile
	Vector4 w2Depth;	///< zw foam pattern offset (xy unused; depth range from cbFrame)
	Vector4 w2Ssr;	///< screen space reflection: strength, length, steps, reflection gain
	Vector4 w2Shade;	///< darkening with depth, glint fade at the shore, sky mip, EA pattern
	Vector4 w2DiagFlow;	///< lakes: x diagnostics (1 depths, 2 foam and glint); rivers: flow rate, phase, cycle
	Vector4 w2Wind;	///< xy wind direction, z uv per world unit of the wind fields, w roughness in calm
	Vector4 w2Wind2;	///< xy wind field drift, z ripple patch strength, w upwind distance of the wind shadow
};

/// Trees and grass tufts (Trees.vs.hlsl); the shadow map pass copies sway for each recorded tree draw.
struct RenderTreesVS
{
	Vector4 unused0[4];
	Vector4 worldViewProj[4];	///< transposed world-view-projection matrix
	Vector4 sway[11];			///< sway vector per sway index; index 0 is still, w is the flutter clock
	Vector4 unused19[13];
	Vector4 shroudOffset;		///< shroud texture: offset added to the position
	Vector4 shroudScale;		///< shroud texture: scale after the offset
};

static_assert(sizeof(RenderScreenFilterPS) <= sizeof(Vector4) * MAX_PIXEL_SHADER_CONSTANTS, "RenderScreenFilterPS too large");
static_assert(sizeof(RenderSeaWaveVS) <= sizeof(Vector4) * MAX_VERTEX_SHADER_CONSTANTS, "RenderSeaWaveVS too large");
static_assert(sizeof(RenderSeaWavePS) <= sizeof(Vector4) * MAX_PIXEL_SHADER_CONSTANTS, "RenderSeaWavePS too large");
static_assert(sizeof(RenderWater2PS) <= sizeof(Vector4) * MAX_PIXEL_SHADER_CONSTANTS, "RenderWater2PS too large");
static_assert(sizeof(RenderTreesVS) <= sizeof(Vector4) * MAX_VERTEX_SHADER_CONSTANTS, "RenderTreesVS too large");
