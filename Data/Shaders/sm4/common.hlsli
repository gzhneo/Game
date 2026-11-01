/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// common.hlsli -----------------------------------------------------------------------------------
//
// Constant buffer layout and interpolants shared by the uber shader (ffp.vs/ps.hlsl) and the
// individual shaders. The C++ counterpart is in WW3D2/rendercb.h and checks size and alignment
// with static_assert.
//
// Matrix convention: all matrices are row_major and come unchanged from _Set_DX8_Transform
// (D3D row vectors, translation in row 4). Shaders therefore compute mul(float4(pos,1), m) and
// never transpose.
// ------------------------------------------------------------------------------------------------

#ifndef FFP_COMMON_HLSLI
#define FFP_COMMON_HLSLI

// Four stage descriptions because the vertex part needs that many: the terrain shaders generate
// the coordinates of stages 2 and 3 by texgen from the camera space position and apply a texture
// matrix (W3DShaderManager::set). The pixel shader combiner chain stays at two stages; the
// fixed-function path needs no more.
#define FFP_MAX_STAGES       4   // stages with texgen and texture matrix (vertex part)
#define FFP_COMBINER_STAGES  2   // emulated combiner stages (pixel part)
#define FFP_MAX_LIGHTS       4   // fixed-function vertex lighting: at most four lights

// --- Light types (WW3D_LIGHT_*) -----------------------------------------------------------------
#define FFP_LIGHT_POINT        1
#define FFP_LIGHT_SPOT         2
#define FFP_LIGHT_DIRECTIONAL  3

// --- Material sources (WW3D_MCS_*) --------------------------------------------------------------
#define FFP_MCS_MATERIAL  0
#define FFP_MCS_COLOR1    1
#define FFP_MCS_COLOR2    2

// --- Combiner operations (WW3D_TOP_*) -----------------------------------------------------------
#define FFP_TOP_DISABLE                    1
#define FFP_TOP_SELECTARG1                 2
#define FFP_TOP_SELECTARG2                 3
#define FFP_TOP_MODULATE                   4
#define FFP_TOP_MODULATE2X                 5
#define FFP_TOP_MODULATE4X                 6
#define FFP_TOP_ADD                        7
#define FFP_TOP_ADDSIGNED                  8
#define FFP_TOP_ADDSIGNED2X                9
#define FFP_TOP_SUBTRACT                  10
#define FFP_TOP_ADDSMOOTH                 11
#define FFP_TOP_BLENDDIFFUSEALPHA         12
#define FFP_TOP_BLENDTEXTUREALPHA         13
#define FFP_TOP_BLENDFACTORALPHA          14
#define FFP_TOP_BLENDTEXTUREALPHAPM       15
#define FFP_TOP_BLENDCURRENTALPHA         16
#define FFP_TOP_PREMODULATE               17
#define FFP_TOP_MODULATEALPHA_ADDCOLOR    18
#define FFP_TOP_MODULATECOLOR_ADDALPHA    19
#define FFP_TOP_BUMPENVMAP                22
#define FFP_TOP_BUMPENVMAPLUMINANCE       23
#define FFP_TOP_DOTPRODUCT3               24
#define FFP_TOP_MULTIPLYADD               25
#define FFP_TOP_LERP                      26

// --- Combiner arguments (WW3D_TA_*) -------------------------------------------------------------
#define FFP_TA_DIFFUSE          0
#define FFP_TA_CURRENT          1
#define FFP_TA_TEXTURE          2
#define FFP_TA_TFACTOR          3
#define FFP_TA_SPECULAR         4
#define FFP_TA_SELECTMASK       0x0F
#define FFP_TA_COMPLEMENT       0x10
#define FFP_TA_ALPHAREPLICATE   0x20

// --- Texture coordinate generation (WW3D_TCI_*, upper 16 bits of TEXCOORDINDEX) -----------------
#define FFP_TEXGEN_PASSTHRU                     0
#define FFP_TEXGEN_CAMERASPACENORMAL            1
#define FFP_TEXGEN_CAMERASPACEPOSITION          2
#define FFP_TEXGEN_CAMERASPACEREFLECTIONVECTOR  3

// --- Compare functions (WW3D_CMP_*) -------------------------------------------------------------
#define FFP_CMP_NEVER         1
#define FFP_CMP_LESS          2
#define FFP_CMP_EQUAL         3
#define FFP_CMP_LESSEQUAL     4
#define FFP_CMP_GREATER       5
#define FFP_CMP_NOTEQUAL      6
#define FFP_CMP_GREATEREQUAL  7
#define FFP_CMP_ALWAYS        8

// ================================================================================================
// Constant buffers
// ================================================================================================

struct FFPLight
{
	float4	position;		///< xyz world space position, w = light type
	float4	direction;		///< xyz world space direction
	float4	diffuse;
	float4	specular;
	float4	ambient;
	float4	attenuation;	///< x = Range, y/z/w = Attenuation0/1/2
	float4	spot;			///< x = Falloff, y = cos(Theta/2), z = cos(Phi/2), w unused
};

struct FFPStage
{
	int4	color_op;		///< x = Op, y = Arg1, z = Arg2, w = Arg0
	int4	alpha_op;
	int4	texgen;			///< x = texgen mode, y = UV set, z = component count, w = projected
	float4	bump;			///< BUMPENVMAT00/01/10/11
	float4	bump_scale;		///< x = LSCALE, y = LOFFSET, z = component count of the UV set (0 = missing), w = texture bound
};

#include "slots.hlsli"
#include "sceneformat.hlsli"
cbuffer cbFrame : register(SLOT_CB_FRAME)
{
	row_major float4x4	f_view;
	row_major float4x4	f_proj;
	row_major float4x4	f_viewProj;
	float4				f_fogColor;
	float4				f_fogParams;		///< x = start, y = end, z = 1 if fog is on
	float4				f_cameraPosWorld;
	// Scene light as the terrain is prelit (BaseHeightMapRenderObjClass::doTheLight without point lights).
	float4				f_ambient;			///< rgb = terrain ambient light
	float4				f_sunRay[3];		///< xyz = direction to the light (not normalized), w = 1 active
	float4				f_sunDiffuse[3];	///< rgb = light color
	float4				f_viewport;			///< x, y, width, height of the current viewport (pixels, after render scale)
	float4				f_depthRange;		///< x = MinZ, y = MaxZ, z = 1/(MaxZ-MinZ) (1 if empty)
	float4				f_hdr;				///< x = factor for additively blended draws with HDR (o_flags.z = 5)
};

cbuffer cbObject : register(SLOT_CB_OBJECT)
{
	row_major float4x4	o_world;
	row_major float4x4	o_worldView;
	row_major float4x4	o_worldViewProj;
	row_major float4x4	o_worldInvTranspose;

	float4				o_matDiffuse;
	float4				o_matAmbient;
	float4				o_matSpecular;
	float4				o_matEmissive;
	float4				o_matPower;			///< x = Specular Power
	float4				o_globalAmbient;
	float4				o_tfactor;
	float4				o_alphaRef;			///< x = reference value 0..1

	int4				o_lightFlags;		///< x = bit mask of active lights, y = lighting on,
											///< z = specular on, w = LocalViewer
	int4				o_colorSource;		///< x/y/z/w = source for diffuse/ambient/emissive/specular
	int4				o_alphaTest;		///< x = alpha test on, y = compare function, z = samples for ALPHA_COVERAGE
	int4				o_flags;			///< x = NormalizeNormals, y = ColorVertex, z = 1/2/3/4: Scene_Out rounds/clamps/rounds to R11G11B10/only its grid (HDR)/additive factor (HDR), w = destination alpha (Scene_Out)

	FFPLight			o_lights[FFP_MAX_LIGHTS];
	FFPStage			o_stages[FFP_MAX_STAGES];
	row_major float4x4	o_texMatrix[FFP_MAX_STAGES];
	float4				o_teamColor;		///< x = hue in degrees (-1: grey), y = saturation, z = bit mask of the stages drawn in it, w = 1: normal map (PIXEL_LIGHT)
};

// Pixel output into the scene. With a wider scene target (-sceneFormat) a draw without blending is rounded to 8 bits
// as an 8-bit target rounds on write, so it gives the same image as with BGRA8 (o_flags.z = 1, set by the backend;
// 3 with R11G11B10, then also to its grid). A blended draw is only clamped (o_flags.z = 2): a UNORM target clamps
// the source before blending, a float target does not, and the hardware blends the unrounded source with 8 bits too.
float4 Scene_Out(float4 c)
{
	if (o_flags.z == 1 || o_flags.z == 3) c = round(saturate(c) * 255.0) / 255.0;
	else if (o_flags.z == 2) c = saturate(c);
	if (o_flags.z == 3) c.rgb = Grid_R11G11B10(c.rgb);
	// HDR (RENDER_EFFECT_HDR): values above 1 stay, R11G11B10 still gets its grid.
	if (o_flags.z == 4) c.rgb = Grid_R11G11B10(max(c.rgb, 0.0));
	// HDR, additively blended draw (fire, explosions): the particles were tuned for clamping; a factor tones down how far
	// their sum goes above 1.
	if (o_flags.z == 5) c.rgb *= f_hdr.x;
	// A scene target without alpha keeps the destination alpha in a target of its own: a draw that writes only
	// alpha goes there as red (o_flags.w = 2).
	if (o_flags.w == 2) c = c.aaaa;
	return c;
}

// Destination alpha of a scene target without alpha, for draws that blend with it (o_flags.w = 1): the backend
// turns DESTALPHA into SRCALPHA, so the alpha target becomes the source alpha. Shaders that may draw such a blend
// pass their pixel position.
Texture2D<float> scene_alpha_target : register(SLOT_T_SCENE_ALPHA);

float4 Scene_Out(float4 c, float4 pos)
{
	c = Scene_Out(c);
	if (o_flags.w == 1) c.a = scene_alpha_target.Load(int3(int2(pos.xy), 0));
	return c;
}

// The pieces that set their own constants (Set_*_Shader_Constant: screen filters, sea, trees, water) declare their
// layout from renderpieces.h as a cbuffer on SLOT_CB_PIECE_VS / SLOT_CB_PIECE_PS themselves.

// ================================================================================================
// Interpolants -- identical for the uber shader and the individual shaders
// ================================================================================================

struct VS_OUT
{
	float4	pos    : SV_Position;
	float4	color0 : COLOR0;		///< Diffuse (+ Ambient + Emissive)
	float4	color1 : COLOR1;		///< specular, added in the PS after the stages
	float4	uv0    : TEXCOORD0;
	float4	uv1    : TEXCOORD1;
	float4	uv2    : TEXCOORD2;
	float4	uv3    : TEXCOORD3;
	float4	fog    : TEXCOORD4;		///< x = fog factor (1 = clear)
#ifdef PIXEL_LIGHT
	float4	normal  : TEXCOORD5;	///< xyz world normal, w its length at the vertex
	float4	diffuse : TEXCOORD6;	///< rgb diffuse material (color0 then holds ambient + emissive)
#endif
};

#endif // FFP_COMMON_HLSLI
