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

// renderbackend.h --------------------------------------------------------------------------------
//
// The device-facing subset of the render interface.
//
// `RenderDevice` is the static facade used by game code. It holds the state cache (RenderStates,
// TextureStageStates, SamplerStates, texture slots), deferred state (`render_state`), statistics,
// registry and the device list. Everything that actually talks to the graphics device sits behind
// this class. The implementation is `RenderCore` (rendercore.cpp), which runs on one of the device
// layers built in (gpulayers.h: Direct3D 11, Vulkan).
//
// The state setters are not device calls; they write a shadow state that
// `Apply_Render_State_Changes`/`Draw_*` translate into state objects, constant buffers and the
// uber shader.
//
// Only the backend-neutral types from `ww3dtypes.h` appear here; the backend puts its own objects
// behind the handles.
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef RENDERBACKEND_H
#define RENDERBACKEND_H

#include "always.h"
#include "ww3dtypes.h"
#include "ww3dformat.h"
#include "presentlayout.h"

/// Requested device state (input for Build_Device_Parameters; in windowed mode bit_depth may be
/// taken from the desktop and adjusted).
struct RenderDeviceSetup
{
	unsigned	adapter;				///< adapter index
	void*		hwnd;					///< window
	int			width;
	int			height;
	int			bit_depth;				///< in/out
	bool		windowed;
	int			multisample_samples;	///< requested sample count (0/1 = off)
	int			swap_interval;			///< 0 = no VSync, 1..3 = every n-th frame (as RenderDevice::Set_Swap_Interval)
};

/// A point light of the current frame (RenderDevice::Set_Dynamic_Lights), world space. The color already
/// carries the intensity.
struct RenderDynamicLight
{
	float		x, y, z;
	float		radius;
	float		r, g, b;
};

/// At most RENDER_MAX_DYNAMIC_LIGHTS lights are applied per frame, chosen from up to
/// RENDER_MAX_DYNAMIC_LIGHT_CANDIDATES submitted ones (those in view, strongest first).
enum { RENDER_MAX_DYNAMIC_LIGHTS = 32, RENDER_MAX_DYNAMIC_LIGHT_CANDIDATES = 64 };

/// Inputs for shadow-mapped sun shadows. The game supplies sun direction, shadow color and the
/// terrain height range; RenderDevice::Shadow_Scene_Begin fills in the camera view and projection
/// (D3D layout, row vectors).
struct RenderShadowParams
{
	float			sun_direction[3];		///< direction to the sun (world, normalized)
	float			shadow_color[3];		///< factor in full shadow per channel, like the stencil shadows (0..1)
	float			ground_min_z;			///< lowest terrain point of the map
	float			ground_max_z;			///< highest terrain point of the map
	WW3DMatrixRaw	view;					///< camera view matrix
	WW3DMatrixRaw	projection;				///< camera projection
};

/// Which terrain texture lies where in the terrain atlas. The atlas is split into grid cells
/// (WorldHeightMap::updateTileTexturePositions, cell = tile + border); per cell the detail material
/// of the class there (0 = no detail, 1..8) and its origin modulo one tile.
struct RenderTerrainDetailTable
{
	enum { MAX_CELLS = 1024 };
	float			atlas_width;		///< logical atlas width in pixels (normalizes U)
	float			atlas_height;		///< logical atlas height in pixels (normalizes V)
	float			cell_pixels;		///< edge length of a grid cell (tile + border)
	float			tile_pixels;		///< edge length of a tile; the detail texture covers exactly one
	unsigned		columns;
	unsigned		rows;
	unsigned		cells[MAX_CELLS];	///< material | (origin U mod tile) << 8 | (origin V mod tile) << 16
	/// Average color of the class in each cell, 0xAARRGGBB (A = 0: no class).
	unsigned		colors[MAX_CELLS];
};

/// The terrain prelighting lights (BaseHeightMapRenderObjClass::doTheLight), so the rock layer
/// computes per-pixel lighting with the same values.
struct RenderTerrainLighting
{
	float			ambient[3];			///< ambient light (first terrain light only)
	float			ray[3][3];			///< direction to each light (-m_terrainLightPos, not normalized)
	float			diffuse[3][3];		///< color per light
	unsigned		count;				///< active lights (0..3)
};

/// Terrain tessellation: heights of the terrain textures and how they mix (WorldHeightMap, when the atlas is
/// built). The height atlas has the layout of the terrain atlas; a class is placed in the world like the base
/// pass places its tiles.
struct RenderTerrainTessData
{
	enum { MAX_CLASSES = 256 };
	WW3DTextureHandle		atlas;				///< height atlas (R = height, 128 = mean)
	float					cell_u, cell_v;		///< atlas size of one terrain cell (u, v)
	unsigned				class_count;
	/// Per texture class: u of the origin, v of the upper edge, cells per class edge, 1 = has heights.
	float					classes[MAX_CLASSES][4];
	/// Per grid point (row by row, width x height): class A, weight A, class B, weight B.
	const unsigned char*	weights;
	int						width, height;
};

/// One particle for Draw_Particles (PointGroupClass): the quad is built by the vertex shader.
struct RenderParticleInstance
{
	float			x, y, z;			///< world position of the centre
	float			size;
	unsigned		color;				///< D3DCOLOR (Convert_Color_Clamp)
	unsigned char	orientation;		///< 0..255 as in PointGroupClass
	unsigned char	pad[3];
};

/// Per draw: billboard or ground aligned, UVs of the four corners (frame of the point group).
struct RenderParticleSetup
{
	bool			billboard;
	bool			lit;				///< alpha blended or alpha tested: takes the particle lighting (not additive)
	float			uv[4][2];
};

/// A dynamic light on the terrain (W3DDynamicLight: explosions, muzzle flashes, ...), lit per vertex
/// by the backend as HeightMapRenderObjClass::doTheDynamicLight does on the CPU.
struct RenderTerrainDynamicLight
{
	enum { MAX_LIGHTS = 20 };
	float			position[3];		///< point light: world position; directional: direction to the light
	float			directional;		///< 1 = directional (factor 1), 0 = point or spot
	float			mid_range;			///< full strength up to here
	float			range;				///< no light from here on
	float			diffuse[3];
	float			ambient[3];
};

/// The static prelighting of the terrain tiles (BaseHeightMapRenderObjClass::doTheLight) for the vertex
/// shader: ambient and global lights as in RenderTerrainLighting plus the static scene lights (map lights).
struct RenderTerrainStaticLighting
{
	enum { MAX_SCENE_LIGHTS = 16 };
	RenderTerrainLighting		global;
	unsigned					scene_count;
	RenderTerrainDynamicLight	scene[MAX_SCENE_LIGHTS];	///< same light model as the dynamic lights
};

class RenderBackend
{
public:
	virtual ~RenderBackend() {}

	// =============================================================================================
	// 1. Device, swap chain, frame
	// =============================================================================================
	/// Create the DXGI factory (no device yet). false if the runtime is missing.
	virtual bool				Create_Interface() = 0;
	virtual void				Release_Interface() = 0;
	virtual void				Set_Window(void* hwnd) = 0;
	virtual WW3DInterfaceHandle	Get_Interface() const = 0;
	virtual WW3DDeviceHandle	Get_Device() const = 0;

	// Adapter and mode queries; the device list itself (RenderDeviceDescClass) is kept by the wrapper.
	virtual unsigned			Get_Adapter_Count() = 0;
	virtual bool				Get_Adapter_Identifier(unsigned adapter, WW3DAdapterIdentifier* id) = 0;
	virtual unsigned			Get_Adapter_Mode_Count(unsigned adapter, unsigned device_format) = 0;
	virtual bool				Enum_Adapter_Mode(unsigned adapter, unsigned device_format, unsigned index,
										unsigned* width, unsigned* height, unsigned* format) = 0;
	virtual bool				Get_Adapter_Display_Mode(unsigned adapter, unsigned* width, unsigned* height, unsigned* format) = 0;

	/// Derive present parameters/formats from the requested state (mode search, multisampling).
	/// false if the request cannot be met. Follow with Create_Device or Reset_Device.
	virtual bool				Build_Device_Parameters(RenderDeviceSetup& setup) = 0;
	/// Create or release the device with the last built parameters.
	virtual bool				Create_Device() = 0;
	virtual void				Release_Device() = 0;
	/// Reset the device with the current parameters (recreates the buffers).
	/// The caller releases all DEFAULT pool resources before and recreates them afterwards.
	virtual HRESULT				Reset_Device() = 0;

	virtual HRESULT				Get_Device_State() = 0;		///< WW3D_OK / WW3D_ERR_DEVICELOST / WW3D_ERR_DEVICENOTRESET
	virtual bool				Validate_Device() = 0;

	virtual void				Begin_Scene() = 0;
	virtual void				End_Scene() = 0;
	virtual HRESULT				Present() = 0;
	virtual void				Clear(bool clear_color, bool clear_z_stencil, WW3DColor color, float z, unsigned stencil) = 0;
	virtual void				Set_Viewport(const WW3DViewport* viewport) = 0;
	virtual void				Get_Viewport(WW3DViewport* viewport) = 0;
	virtual void				Set_Gamma(float gamma, float bright, float contrast, bool calibrate, bool uselimit) = 0;
	virtual void				Set_Swap_Interval(int swap) = 0;	///< 0 = no VSync, 1..3 = every n-th frame
	virtual int					Get_Swap_Interval() = 0;
	virtual void				Set_Present_Size(int width, int height) = 0;	///< back buffer size for the next reset
	virtual PresentLayout		Get_Present_Layout() = 0;		///< scene position in the swap chain buffer

	virtual WW3DFormat			Get_Back_Buffer_Format() = 0;
	virtual bool				Has_Stencil() = 0;
	virtual WW3DSwapChainHandle	Create_Additional_Swap_Chain(void* render_window) = 0;
	/// Save the back buffer as a 24-bit BMP before present.
	virtual bool				Save_Back_Buffer_BMP(const char* path) = 0;
	/// Register a file name for the next present (NULL = cancel).
	virtual void				Request_Back_Buffer_Capture(const char* path) = 0;

	// =============================================================================================
	// 2. State
	// =============================================================================================
	virtual void				Set_Render_State(WW3DRenderStateType state, unsigned value) = 0;
	virtual void				Get_Render_State(WW3DRenderStateType state, unsigned* value) = 0;
	virtual void				Set_Texture_Stage_State(unsigned stage, WW3DTextureStageStateType state, unsigned value) = 0;
	virtual void				Set_Sampler_State(unsigned stage, WW3DSamplerStateType state, unsigned value) = 0;
	virtual void				Set_Texture(unsigned stage, WW3DTextureHandle texture) = 0;
	virtual void				Set_Material(const WW3DMaterial* material) = 0;
	virtual void				Set_Light(unsigned index, const WW3DLight* light) = 0;		///< NULL = light off
	virtual void				Set_Clip_Plane(unsigned index, const float* plane) = 0;
	virtual void				Set_Transform(WW3DTransformType transform, const WW3DMatrixRaw* m) = 0;
	virtual void				Get_Transform(WW3DTransformType transform, WW3DMatrixRaw* m) = 0;
	// Projection as a custom vertex shader needs it as a constant so it rasterizes like the
	// fixed-function path. D3D11 needs no half-pixel offset, so this is the unchanged matrix.
	virtual void				Get_Shader_Projection(WW3DMatrixRaw* m) { Get_Transform(WW3D_TS_PROJECTION, m); }

	// =============================================================================================
	// 3. Drawing
	// =============================================================================================
	virtual void				Set_Stream_Source(unsigned stream, WW3DVertexBufferHandle vb, unsigned offset, unsigned stride) = 0;
	virtual void				Set_Indices(WW3DIndexBufferHandle ib) = 0;
	virtual void				Draw_Indexed_Primitive(WW3DPrimitiveType type, int base_vertex, unsigned min_index,
										unsigned num_vertices, unsigned start_index, unsigned prim_count) = 0;
	virtual void				Draw_Primitive_UP(WW3DPrimitiveType type, unsigned prim_count, const void* data, unsigned stride) = 0;

	// =============================================================================================
	// 4. Shaders and vertex declarations
	// =============================================================================================
	virtual HRESULT				Set_Vertex_Shader(WW3DVertexShaderHandle shader) = 0;
	virtual HRESULT				Set_Pixel_Shader(WW3DPixelShaderHandle shader) = 0;
	virtual void				Set_Vertex_Shader_Constant(unsigned reg, const float* data, unsigned count4) = 0;
	virtual void				Set_Pixel_Shader_Constant(unsigned reg, const float* data, unsigned count4) = 0;
	virtual HRESULT				Create_Vertex_Shader_From_HLSL(const char* source, unsigned length, const char* debug_name, WW3DVertexShaderHandle* out) = 0;
	virtual HRESULT				Create_Pixel_Shader_From_HLSL(const char* source, unsigned length, const char* debug_name, WW3DPixelShaderHandle* out) = 0;
	virtual HRESULT				Create_Vertex_Declaration(const WW3DVertexElement* elements, WW3DVertexDeclHandle* out) = 0;
	virtual void				Set_Vertex_Declaration(WW3DVertexDeclHandle decl) = 0;

	// =============================================================================================
	// 5. Resources
	// =============================================================================================
	virtual HRESULT				Create_Texture(unsigned width, unsigned height, unsigned levels, unsigned usage,
										unsigned format, unsigned pool, WW3DTextureHandle* out) = 0;
	virtual HRESULT				Create_Vertex_Buffer(unsigned length, unsigned usage, unsigned fvf, unsigned pool, WW3DVertexBufferHandle* out) = 0;
	virtual HRESULT				Create_Index_Buffer(unsigned length, unsigned usage, unsigned format, unsigned pool, WW3DIndexBufferHandle* out) = 0;
	virtual WW3DSurfaceHandle	Create_Offscreen_Surface(unsigned width, unsigned height, unsigned device_format) = 0;

	virtual HRESULT				Get_Texture_Surface_Level(WW3DTextureHandle texture, unsigned level, WW3DSurfaceHandle* out) = 0;
	virtual HRESULT				Get_Texture_Level_Desc(WW3DTextureHandle texture, unsigned level, WW3DSurfaceDesc* desc) = 0;
	virtual HRESULT				Get_Surface_Desc(WW3DSurfaceHandle surface, WW3DSurfaceDesc* desc) = 0;
	virtual unsigned			Get_Texture_Level_Count(WW3DTextureHandle texture) = 0;
	/// A second handle on the same GPU texture that the pixel shader draws in a team colour (ZHCA textures);
	/// nullptr if the backend cannot, then the caller recolours on the CPU.
	virtual WW3DTextureHandle	Create_Team_Color_View(WW3DTextureHandle, unsigned) { return nullptr; }
	virtual unsigned			Get_Texture_Priority(WW3DTextureHandle texture) = 0;
	virtual unsigned			Set_Texture_Priority(WW3DTextureHandle texture, unsigned priority) = 0;
	virtual void				Set_Texture_LOD(WW3DTextureHandle texture, unsigned lod) = 0;

	virtual HRESULT				Lock_Surface(WW3DSurfaceHandle surface, WW3DLockedRect* out, const RECT* rect, unsigned flags) = 0;
	virtual HRESULT				Unlock_Surface(WW3DSurfaceHandle surface) = 0;
	virtual HRESULT				Lock_Texture(WW3DTextureHandle texture, unsigned level, WW3DLockedRect* out, const RECT* rect, unsigned flags) = 0;
	virtual HRESULT				Unlock_Texture(WW3DTextureHandle texture, unsigned level) = 0;
	virtual HRESULT				Lock_Vertex_Buffer(WW3DVertexBufferHandle vb, unsigned offset, unsigned size, void** data, unsigned flags) = 0;
	virtual HRESULT				Unlock_Vertex_Buffer(WW3DVertexBufferHandle vb) = 0;
	virtual HRESULT				Lock_Index_Buffer(WW3DIndexBufferHandle ib, unsigned offset, unsigned size, void** data, unsigned flags) = 0;
	virtual HRESULT				Unlock_Index_Buffer(WW3DIndexBufferHandle ib) = 0;

	virtual void				Add_Ref_Resource(WW3DTextureHandle texture) = 0;
	virtual void				Add_Ref_Resource(WW3DSurfaceHandle surface) = 0;
	virtual void				Release_Resource(WW3DTextureHandle texture) = 0;
	virtual void				Release_Resource(WW3DSurfaceHandle surface) = 0;
	virtual void				Release_Resource(WW3DVertexShaderHandle shader) = 0;
	virtual void				Release_Resource(WW3DPixelShaderHandle shader) = 0;
	virtual void				Release_Resource(WW3DVertexDeclHandle decl) = 0;
	virtual void				Release_Resource(WW3DVertexBufferHandle vb) = 0;
	virtual void				Release_Resource(WW3DIndexBufferHandle ib) = 0;

	/// Copy rectangles between two surfaces (CopySubresourceRegion).
	virtual void				Copy_Rects(WW3DSurfaceHandle source, const RECT* source_rects, unsigned num_rects,
										WW3DSurfaceHandle dest, const POINT* dest_points) = 0;
	/// Upload a system memory texture into the video memory texture.
	virtual void				Update_Texture(WW3DTextureHandle system, WW3DTextureHandle video) = 0;
	/// Copy the current color render target including alpha into a render target texture of the
	/// same size (multisampling is resolved). Scene copy for water refraction; false if size or
	/// format do not match.
	virtual bool				Copy_Render_Target_To_Texture(WW3DTextureHandle dest) = 0;

	virtual HRESULT				Get_Render_Target(WW3DSurfaceHandle* out) = 0;
	virtual HRESULT				Get_Depth_Stencil_Surface(WW3DSurfaceHandle* out) = 0;
	virtual HRESULT				Set_Render_Target_Surfaces(WW3DSurfaceHandle render_target, WW3DSurfaceHandle depth_stencil) = 0;
	/// Switch the render target, tracking the default target and the matching depth buffer.
	/// NULL restores the default target.
	virtual void				Set_Render_Target_Surface(WW3DSurfaceHandle render_target) = 0;
	virtual void				Set_Render_Target_Swap_Chain(WW3DSwapChainHandle swap_chain) = 0;
	virtual void				Release_Render_Target_Depth_Stencils() = 0;
	virtual WW3DSurfaceHandle	Get_Current_Render_Target() const = 0;	///< active texture render target (no AddRef), NULL = default target
	virtual WW3DSurfaceHandle	Get_Front_Buffer_Copy() = 0;		///< desktop-sized copy of the front buffer (screenshot)
	virtual HRESULT				Get_Back_Buffer(unsigned index, WW3DSurfaceHandle* out) = 0;

	// 64 bit so large amounts of video memory are not truncated.
	virtual unsigned __int64	Get_Free_Texture_RAM() = 0;

	// =============================================================================================
	// 6. Cursor and capabilities
	// =============================================================================================
	virtual BOOL				Show_Cursor(BOOL show) = 0;
	virtual HRESULT				Set_Cursor_Properties(unsigned hot_x, unsigned hot_y, WW3DSurfaceHandle bitmap) = 0;
	virtual void				Set_Cursor_Position(int x, int y, unsigned flags) = 0;

	/// Fills the Support_* fields of RenderCaps. They are fixed by feature level 11.0.
	virtual void				Compute_Caps() = 0;

	// =============================================================================================
	// 7. Graphics effects
	// =============================================================================================
	// The D3D11 backend implements them; the defaults here do nothing. Which effects are enabled
	// is given by RenderDevice::Get_Effect_Settings().

	/// The following draws are billboard particles (soft particles). Comes from RenderStateStruct.
	virtual void				Set_Soft_Particles(bool) {}
	/// The opaque part of the 3D scene is drawn (depth complete); water, transparent objects and
	/// particles follow: depth copy, ambient occlusion.
	virtual void				Scene_Opaque_Done() {}
	/// The 3D scene of the view (pixel rectangle) is done, the 2D UI follows: post processing.
	virtual void				Scene_3D_Done(int, int, int, int) {}
	/// The 3D scene of the view (pixel rectangle) begins. With RenderEffectSettings::render_scale below 1 the
	/// backend draws it into a share of the target until Scene_3D_Done scales it up (internal render resolution).
	virtual void				Scene_3D_Begin(int, int, int, int) {}
	/// Target pixels per engine pixel for draws into the current target (1 unless the scene is drawn scaled).
	virtual void				Get_Render_Scale(float* sx, float* sy) { if (sx) *sx = 1.0f; if (sy) *sy = 1.0f; }
	/// Request copies for later users (e.g. water), RENDER_SCENE_COPY_* (sticky until revoked).
	virtual void				Request_Scene_Copies(unsigned) {}
	/// Point lights (world space) for the next 3D scene; replaces the previous list. Used when
	/// RENDER_EFFECT_LIGHTS is on.
	virtual void				Set_Dynamic_Lights(const RenderDynamicLight*, unsigned) {}
	/// Depth or color copy of this frame's opaque scene, otherwise NULL (Set_Device_Texture).
	virtual WW3DTextureHandle	Get_Scene_Depth_Copy() { return nullptr; }
	virtual WW3DTextureHandle	Get_Scene_Color_Copy() { return nullptr; }
	/// One line with the measured GPU times of the effects (empty if nothing was measured).
	virtual void				Get_Effect_Statistics(char* text, unsigned size) { if (text && size) text[0] = 0; }
	/// One line about objects the device builds behind the scenes (Vulkan: pipelines), empty if none.
	virtual void				Get_Device_Statistics(char* text, unsigned size) { if (text && size) text[0] = 0; }

	// Shadow-mapped sun shadows.
	/// The opaque part of the scene begins: shadow casters are recorded from here until
	/// Scene_Opaque_Done draws the map and applies the shadows to the image.
	virtual void				Shadow_Scene_Begin(const RenderShadowParams&) {}
	/// Terrain and roads are drawn; props, bridges and objects follow (from
	/// HeightMapRenderObjClass::Render). The backend keeps the depth and the casters recorded so far
	/// as terrain, so terrain shadows on terrain can be weaker (RenderEffectSettings::shadow_terrain_strength).
	virtual void				Scene_Terrain_Done() {}
	/// Start (end false) or end of a part of the frame with its own GPU time (RenderGpuPass).
	virtual void				Gpu_Pass_Mark(unsigned, bool) {}
	/// true while shadow maps run in this scene pass -- stencil shadow volumes and model projected
	/// shadows stay off then.
	virtual bool				Shadow_Maps_Active() { return false; }
	/// While on, indexed draws are not drawn but only recorded as shadow casters (geometry outside the view
	/// that can still shadow the visible ground). Only takes effect while shadow maps record.
	virtual void				Set_Shadow_Caster_Only(bool) {}

	// Terrain detail layer.
	/// Placement of the terrain textures in the atlas (when the atlas is built, WorldHeightMap::getTerrainTexture).
	virtual void				Set_Terrain_Detail_Table(const RenderTerrainDetailTable&) {}
	/// The following draws are terrain with atlas coordinates (HeightMapRenderObjClass::Render).
	virtual void				Set_Terrain_Detail(bool) {}
	// Terrain rock layer.
	/// Map height field (one byte per grid point, row by row), border in cells, cell size and height scale (world).
	virtual void				Set_Terrain_Height_Field(const unsigned char*, int, int, int, float, float) {}
	/// Prelighting lights (map time of day).
	virtual void				Set_Terrain_Lighting(const RenderTerrainLighting&) {}
	/// Terrain relief: atlas of normals and gloss, laid out like the terrain atlas (WorldHeightMap). active =
	/// false drops it if it is the current one.
	virtual void				Set_Terrain_Relief_Atlas(WW3DTextureHandle, bool) {}
	/// Models lit per pixel: the normal map (<texture>_n.dds) of the stage 0 texture owner, nullptr for none. Used
	/// only while stage 0 holds owner.
	virtual void				Set_Model_Normal_Texture(WW3DTextureHandle, WW3DTextureHandle) {}
	// Terrain tessellation.
	/// The following draws are terrain geometry that may be tessellated (base pass, three-way blends, shroud).
	virtual void				Set_Terrain_Tessellation(bool) {}
	/// Largest distance (world units) the terrain tessellation moves the surface from its logic height, 0 when off.
	virtual float				Terrain_Tess_Max_Displacement() { return 0.0f; }
	/// Heights and mixing weights of the loaded map; active = false drops them if data.atlas is the current atlas.
	virtual void				Set_Terrain_Tess_Data(const RenderTerrainTessData&, bool) {}
	/// Displacement mask per grid point (255 = full, 0 = none: water, roads, buildings), same grid as the weights.
	virtual void				Set_Terrain_Tess_Mask(const unsigned char*, int, int) {}
	// Dynamic terrain lights on the GPU.
	/// true: the backend lights terrain draws with the lights from Set_Terrain_Dynamic_Lights, the
	/// terrain leaves its vertex buffers alone. Needs the height field (Set_Terrain_Height_Field).
	virtual bool				Supports_Terrain_Dynamic_Lights() { return false; }
	/// Lights of this frame (at most RenderTerrainDynamicLight::MAX_LIGHTS).
	virtual void				Set_Terrain_Dynamic_Lights(const RenderTerrainDynamicLight*, unsigned) {}
	/// The following draws are terrain tiles and get the dynamic lights (HeightMapRenderObjClass::Render).
	virtual void				Enable_Terrain_Dynamic_Lights(bool) {}
	/// Static prelighting of the terrain tiles in the vertex shader; nullptr = the vertex colors are prelit.
	/// With lighting, the tile vertex colors are white (times the shader's result) and keep the blend alpha.
	virtual void				Set_Terrain_Static_Lighting(const RenderTerrainStaticLighting*) {}
	// Skinning on the GPU.
	/// Most bones of one palette; 0 = no GPU skinning (the renderer deforms on the CPU).
	virtual unsigned			Max_Skin_Bones() { return 0; }
	/// Palette of the following draws: count bone matrices as 3x4 rows (Matrix3D), the vertex carries the
	/// bone index in UV set 2. count 0 switches skinning off.
	virtual void				Set_Skin_Bones(const float*, unsigned) {}
	// Particles built on the GPU.
	virtual bool				Supports_Particle_Instancing() { return false; }
	/// Instanced meshes: between the two calls, indexed draws that differ only in the object matrices
	/// may be merged; the caller changes nothing but lights and world transform in between.
	virtual void				Begin_Mesh_Instances() {}
	virtual void				End_Mesh_Instances() {}
	/// Orientation tables of PointGroupClass: 256 x 4 quad corners (x, y) for billboards, 256 x (X.x, X.y,
	/// Y.x, Y.y) ground vectors.
	virtual void				Set_Particle_Tables(const float*, const float*) {}
	/// Draws count particles as quads with the current state (shader, material, textures, view).
	virtual void				Draw_Particles(const RenderParticleInstance*, unsigned, const RenderParticleSetup&) {}
	/// Uploads particles once for several Draw_Particle_Batch calls (the sorting renderer: many short runs);
	/// false if they do not fit, then the caller uses Draw_Particles per run.
	virtual bool				Upload_Particle_Batch(const RenderParticleInstance*, unsigned) { return false; }
	/// Draws count particles of the last uploaded batch, starting at first.
	virtual void				Draw_Particle_Batch(unsigned, unsigned, const RenderParticleSetup&) {}
	/// Light of the current time of day relative to the afternoon, per channel (RENDER_EFFECT_PARTICLE_LIGHT).
	virtual void				Set_Particle_Lighting(const float*) {}
};

struct GpuLayerInfo;

/// The render core on a device layer (gpulayers.h); `RenderDevice::Init` creates it (rendercore.cpp).
RenderBackend* Render_Core_Create(const GpuLayerInfo& layer);
/// The layer could not create its device on the window: continue on another layer.
bool Render_Core_Use_Layer(RenderBackend* backend, const GpuLayerInfo& layer);

#endif // RENDERBACKEND_H
