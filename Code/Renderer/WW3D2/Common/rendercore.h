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

// rendercore.h ---------------------------------------------------------------------------------
//
// The Direct3D 11 implementation of RenderBackend.
//
// The header is device-neutral; the D3D11 objects live behind the handles from ww3dtypes.h and
// are resolved in rendercore*.cpp via d3d11d3d.h.
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef RENDERCORE_H
#define RENDERCORE_H

#include "always.h"
#include "renderbackend.h"

struct FFPDynLightsCB;

struct WW3DBufferCommon;	// d3d11d3d.h
class GpuOutput;
struct GpuLayerInfo;

class RenderCore : public RenderBackend
{
public:
	RenderCore();
	virtual ~RenderCore() override;

	void Reset_State();							///< back to the freshly constructed state (Init after Shutdown)

	/// The one render core on the given device layer (gpulayers.h), in its initial state.
	static RenderBackend* Create(const GpuLayerInfo& layer);
	/// Releases a partly created device and continues on another device layer (fallback).
	bool Use_Layer(const GpuLayerInfo& layer);

	virtual bool Create_Interface() override;
	virtual void Release_Interface() override;
	virtual void Set_Window(void* hwnd) override;
	virtual WW3DInterfaceHandle Get_Interface() const override;
	virtual WW3DDeviceHandle Get_Device() const override;
	virtual unsigned Get_Adapter_Count() override;
	virtual bool Get_Adapter_Identifier(unsigned adapter, WW3DAdapterIdentifier* id) override;
	virtual unsigned Get_Adapter_Mode_Count(unsigned adapter, unsigned device_format) override;
	virtual bool Enum_Adapter_Mode(unsigned adapter, unsigned device_format, unsigned index, unsigned* width, unsigned* height, unsigned* format) override;
	virtual bool Get_Adapter_Display_Mode(unsigned adapter, unsigned* width, unsigned* height, unsigned* format) override;
	virtual bool Build_Device_Parameters(RenderDeviceSetup& setup) override;
	virtual bool Create_Device() override;
	virtual void Release_Device() override;
	virtual HRESULT Reset_Device() override;
	virtual HRESULT Get_Device_State() override;
	virtual bool Validate_Device() override;
	virtual void Begin_Scene() override;
	virtual void End_Scene() override;
	virtual HRESULT Present() override;
	virtual void Clear(bool clear_color, bool clear_z_stencil, WW3DColor color, float z, unsigned stencil) override;
	virtual void Set_Viewport(const WW3DViewport* viewport) override;
	virtual void Get_Viewport(WW3DViewport* viewport) override;
	virtual void Set_Gamma(float gamma, float bright, float contrast, bool calibrate, bool uselimit) override;
	virtual void Set_Swap_Interval(int swap) override;
	virtual int Get_Swap_Interval() override;
	virtual void Set_Present_Size(int width, int height) override;
	virtual PresentLayout Get_Present_Layout() override;
	virtual WW3DFormat Get_Back_Buffer_Format() override;
	virtual bool Has_Stencil() override;
	virtual WW3DSwapChainHandle Create_Additional_Swap_Chain(void* render_window) override;
	virtual bool Save_Back_Buffer_BMP(const char* path) override;
	virtual void Request_Back_Buffer_Capture(const char* path) override;
	virtual void Set_Render_State(WW3DRenderStateType state, unsigned value) override;
	virtual void Get_Render_State(WW3DRenderStateType state, unsigned* value) override;
	virtual void Set_Texture_Stage_State(unsigned stage, WW3DTextureStageStateType state, unsigned value) override;
	virtual void Set_Sampler_State(unsigned stage, WW3DSamplerStateType state, unsigned value) override;
	virtual void Set_Texture(unsigned stage, WW3DTextureHandle texture) override;
	virtual void Set_Material(const WW3DMaterial* material) override;
	virtual void Set_Light(unsigned index, const WW3DLight* light) override;
	virtual void Set_Clip_Plane(unsigned index, const float* plane) override;
	virtual void Set_Transform(WW3DTransformType transform, const WW3DMatrixRaw* m) override;
	virtual void Get_Transform(WW3DTransformType transform, WW3DMatrixRaw* m) override;
	virtual void Set_Stream_Source(unsigned stream, WW3DVertexBufferHandle vb, unsigned offset, unsigned stride) override;
	virtual void Set_Indices(WW3DIndexBufferHandle ib) override;
	virtual void Draw_Indexed_Primitive(WW3DPrimitiveType type, int base_vertex, unsigned min_index, unsigned num_vertices, unsigned start_index, unsigned prim_count) override;
	virtual void Draw_Primitive_UP(WW3DPrimitiveType type, unsigned prim_count, const void* data, unsigned stride) override;
	virtual HRESULT Set_Vertex_Shader(WW3DVertexShaderHandle shader) override;
	virtual HRESULT Set_Pixel_Shader(WW3DPixelShaderHandle shader) override;
	virtual void Set_Vertex_Shader_Constant(unsigned reg, const float* data, unsigned count4) override;
	virtual void Set_Pixel_Shader_Constant(unsigned reg, const float* data, unsigned count4) override;
	virtual HRESULT Create_Vertex_Shader_From_HLSL(const char* source, unsigned length, const char* debug_name, WW3DVertexShaderHandle* out) override;
	virtual HRESULT Create_Pixel_Shader_From_HLSL(const char* source, unsigned length, const char* debug_name, WW3DPixelShaderHandle* out) override;
	virtual HRESULT Create_Vertex_Declaration(const WW3DVertexElement* elements, WW3DVertexDeclHandle* out) override;
	virtual void Set_Vertex_Declaration(WW3DVertexDeclHandle decl) override;
	virtual HRESULT Create_Texture(unsigned width, unsigned height, unsigned levels, unsigned usage, unsigned format, unsigned pool, WW3DTextureHandle* out) override;
	virtual HRESULT Create_Vertex_Buffer(unsigned length, unsigned usage, unsigned fvf, unsigned pool, WW3DVertexBufferHandle* out) override;
	virtual HRESULT Create_Index_Buffer(unsigned length, unsigned usage, unsigned format, unsigned pool, WW3DIndexBufferHandle* out) override;
	virtual WW3DSurfaceHandle Create_Offscreen_Surface(unsigned width, unsigned height, unsigned device_format) override;
	virtual HRESULT Get_Texture_Surface_Level(WW3DTextureHandle texture, unsigned level, WW3DSurfaceHandle* out) override;
	virtual HRESULT Get_Texture_Level_Desc(WW3DTextureHandle texture, unsigned level, WW3DSurfaceDesc* desc) override;
	virtual HRESULT Get_Surface_Desc(WW3DSurfaceHandle surface, WW3DSurfaceDesc* desc) override;
	virtual unsigned Get_Texture_Level_Count(WW3DTextureHandle texture) override;
	virtual WW3DTextureHandle Create_Team_Color_View(WW3DTextureHandle texture, unsigned color) override;
	virtual unsigned Get_Texture_Priority(WW3DTextureHandle texture) override;
	virtual unsigned Set_Texture_Priority(WW3DTextureHandle texture, unsigned priority) override;
	virtual void Set_Texture_LOD(WW3DTextureHandle texture, unsigned lod) override;
	virtual HRESULT Lock_Surface(WW3DSurfaceHandle surface, WW3DLockedRect* out, const RECT* rect, unsigned flags) override;
	virtual HRESULT Unlock_Surface(WW3DSurfaceHandle surface) override;
	virtual HRESULT Lock_Texture(WW3DTextureHandle texture, unsigned level, WW3DLockedRect* out, const RECT* rect, unsigned flags) override;
	virtual HRESULT Unlock_Texture(WW3DTextureHandle texture, unsigned level) override;
	virtual HRESULT Lock_Vertex_Buffer(WW3DVertexBufferHandle vb, unsigned offset, unsigned size, void** data, unsigned flags) override;
	virtual HRESULT Unlock_Vertex_Buffer(WW3DVertexBufferHandle vb) override;
	virtual HRESULT Lock_Index_Buffer(WW3DIndexBufferHandle ib, unsigned offset, unsigned size, void** data, unsigned flags) override;
	virtual HRESULT Unlock_Index_Buffer(WW3DIndexBufferHandle ib) override;
	virtual void Add_Ref_Resource(WW3DTextureHandle texture) override;
	virtual void Add_Ref_Resource(WW3DSurfaceHandle surface) override;
	virtual void Release_Resource(WW3DTextureHandle texture) override;
	virtual void Release_Resource(WW3DSurfaceHandle surface) override;
	virtual void Release_Resource(WW3DVertexShaderHandle shader) override;
	virtual void Release_Resource(WW3DPixelShaderHandle shader) override;
	virtual void Release_Resource(WW3DVertexDeclHandle decl) override;
	virtual void Release_Resource(WW3DVertexBufferHandle vb) override;
	virtual void Release_Resource(WW3DIndexBufferHandle ib) override;
	virtual void Copy_Rects(WW3DSurfaceHandle source, const RECT* source_rects, unsigned num_rects, WW3DSurfaceHandle dest, const POINT* dest_points) override;
	virtual void Update_Texture(WW3DTextureHandle system, WW3DTextureHandle video) override;
	virtual bool Copy_Render_Target_To_Texture(WW3DTextureHandle dest) override;
	virtual HRESULT Get_Render_Target(WW3DSurfaceHandle* out) override;
	virtual HRESULT Get_Depth_Stencil_Surface(WW3DSurfaceHandle* out) override;
	virtual HRESULT Set_Render_Target_Surfaces(WW3DSurfaceHandle render_target, WW3DSurfaceHandle depth_stencil) override;
	virtual void Set_Render_Target_Surface(WW3DSurfaceHandle render_target) override;
	virtual void Set_Render_Target_Swap_Chain(WW3DSwapChainHandle swap_chain) override;
	virtual void Release_Render_Target_Depth_Stencils() override;
	virtual WW3DSurfaceHandle Get_Current_Render_Target() const override;
	virtual WW3DSurfaceHandle Get_Front_Buffer_Copy() override;
	virtual HRESULT Get_Back_Buffer(unsigned index, WW3DSurfaceHandle* out) override;
	virtual unsigned __int64 Get_Free_Texture_RAM() override;	// 64 bit; video memory budget
	virtual BOOL Show_Cursor(BOOL show) override;
	virtual HRESULT Set_Cursor_Properties(unsigned hot_x, unsigned hot_y, WW3DSurfaceHandle bitmap) override;
	virtual void Set_Cursor_Position(int x, int y, unsigned flags) override;
	virtual void Compute_Caps() override;

	// Graphics effects (soft particles, scene copies, post processing), section 8 in the .cpp
	virtual void Set_Soft_Particles(bool enable) override;
	virtual void Scene_Opaque_Done() override;
	virtual void Scene_3D_Done(int x, int y, int width, int height) override;
	virtual void Scene_3D_Begin(int x, int y, int width, int height) override;	///< internal render resolution, section 15
	virtual void Get_Render_Scale(float* sx, float* sy) override;
	virtual void Request_Scene_Copies(unsigned flags) override;
	virtual void Set_Dynamic_Lights(const RenderDynamicLight* lights, unsigned count) override;
	virtual WW3DTextureHandle Get_Scene_Depth_Copy() override;
	virtual WW3DTextureHandle Get_Scene_Color_Copy() override;
	virtual void Get_Effect_Statistics(char* text, unsigned size) override;
	virtual void Get_Device_Statistics(char* text, unsigned size) override;

	// Sun shadows via shadow map, section 9 in the .cpp
	virtual void Shadow_Scene_Begin(const RenderShadowParams& params) override;
	virtual bool Shadow_Maps_Active() override;
	virtual void Set_Shadow_Caster_Only(bool enable) override;
	virtual void Scene_Terrain_Done() override;		///< terrain shadows
	virtual void Gpu_Pass_Mark(unsigned pass, bool end) override;	///< GPU time of a part of the frame

	// Terrain detail layer, section 10 in the .cpp
	virtual void Set_Terrain_Detail_Table(const RenderTerrainDetailTable& table) override;
	virtual void Set_Terrain_Detail(bool enable) override;

	// Terrain rock layer, section 11 in the .cpp
	virtual void Set_Terrain_Height_Field(const unsigned char* heights, int width, int height, int border,
		float cell_size, float height_scale) override;
	virtual void Set_Terrain_Lighting(const RenderTerrainLighting& lighting) override;

	// Terrain relief (normal maps and gloss), section 11b in the .cpp
	virtual void Set_Terrain_Relief_Atlas(WW3DTextureHandle atlas, bool active) override;
	virtual void Set_Model_Normal_Texture(WW3DTextureHandle owner, WW3DTextureHandle normal) override;

	// Terrain tessellation with displacement, section 11c in the .cpp
	virtual void Set_Terrain_Tessellation(bool enable) override;
	virtual float Terrain_Tess_Max_Displacement() override;
	virtual void Set_Terrain_Tess_Data(const RenderTerrainTessData& data, bool active) override;
	virtual void Set_Terrain_Tess_Mask(const unsigned char* mask, int width, int height) override;

	// Dynamic terrain lights, section 12 in the .cpp
	virtual bool Supports_Terrain_Dynamic_Lights() override;
	virtual void Set_Terrain_Dynamic_Lights(const RenderTerrainDynamicLight* lights, unsigned count) override;
	virtual void Enable_Terrain_Dynamic_Lights(bool enable) override;
	virtual void Set_Terrain_Static_Lighting(const RenderTerrainStaticLighting* lighting) override;

	// Skinning on the GPU, section 13 in the .cpp
	virtual unsigned Max_Skin_Bones() override;
	virtual void Set_Skin_Bones(const float* rows, unsigned count) override;

	// Particles built on the GPU, section 14 in the .cpp
	virtual bool Supports_Particle_Instancing() override;
	virtual void Begin_Mesh_Instances() override;
	virtual void End_Mesh_Instances() override;
	virtual void Set_Particle_Tables(const float* rotation, const float* ground) override;
	virtual void Draw_Particles(const RenderParticleInstance* particles, unsigned count, const RenderParticleSetup& setup) override;
	virtual bool Upload_Particle_Batch(const RenderParticleInstance* particles, unsigned count) override;
	virtual void Draw_Particle_Batch(unsigned first, unsigned count, const RenderParticleSetup& setup) override;
	virtual void Set_Particle_Lighting(const float* scale) override;

private:
	struct Impl;
	Impl*	M;		///< all D3D11 objects; defined in rendercore_internal.h

	void	Release_Scene_Target();
	bool	Create_Scene_Target(unsigned width, unsigned height);
	void	Update_Scene_Surfaces(unsigned width, unsigned height);	///< point the handles at the scene target
	void	Reset_Viewport_To_Target(unsigned width, unsigned height);	///< full-target viewport after a target change
	void	Drain_Debug_Messages();			///< debug layer messages into the game log
	void	Log_Missing(const char* what);		///< one-time message for unsupported paths

	// --- Draw path --------------------------------------------------------------------------
	void	Init_Default_States();				///< set the state shadow to the fixed-function defaults
	bool	Ensure_Draw_Resources();			///< constant buffers, uber shaders, helper resources
	void	Release_Draw_Resources();
	bool	Prepare_Draw(unsigned primitive_type);	///< bind everything a draw needs
	void	Ud_Draw();							///< upload diagnosis: line of the prepared draw (rendercore_diagnosis.cpp)
	void	Bind_State_Objects();				///< blend/depth-stencil/rasterizer from the state shadow
	void	Find_State_Objects();				///< state objects for the current render states (cached)
	void	Sa_Select();						///< destination alpha of a scene target without alpha: mode of the draw
	void	Sa_Bind();							///< binds the alpha target as the draw's target or texture
	void	Sa_Done();							///< undoes Sa_Bind after the draw
	void	Bind_Textures();					///< SRVs and samplers of slots 0..7
	void	Update_Constant_Buffers();
	/// Writes bytes into the constant buffer ring; window_bytes is the size the shader declares (0: bytes).
	bool	Cbr_Write(const void* data, unsigned bytes, unsigned window_bytes, unsigned* first, unsigned* count);
	void	Cbr_Disable();						///< back to one buffer per slot (Map with DISCARD)
	/// Makes room for bytes (several writes for one draw) so that none of them wraps the ring.
	bool	Cbr_Reserve(unsigned bytes);
	void	Fill_Frame_Constants();
	void	Fill_Object_Constants();
	/// Uber vertex shader for the permutation of the current vertex declaration (compiled on demand).
	WW3DVertexShaderHandle	Get_FFP_Vertex_Shader(unsigned permutation);
	/// Input layout for (declaration, vertex shader); cached in the declaration.
	void*	Get_Input_Layout(WW3DVertexDeclHandle decl, WW3DVertexShaderHandle vs);

	void	Update_Present_Layout();				///< swap chain buffer size for the current client area
	HRESULT	Resize_Swap_Chain();

	// --- Graphics effects -------------------------------------------------------------------------
	bool	Fx_Ensure_Resources();				///< full-screen VS, constant buffer, states
	bool	Fx_Ensure_Depth_Copy();
	bool	Fx_Ensure_Color_Copy();
	bool	Fx_Ensure_Post_Target();
	bool	Fx_Ensure_AO_Targets();
	bool	Fx_Ensure_Glow_Targets();			///< quarter to 1/32 resolution levels for bloom and the heat mask
	void	Fx_Release_Targets();				///< size-dependent targets (together with the scene target)
	void	Fx_Release_Resources();				///< shaders, buffers, states, queries
	void	Fx_Copy_Depth();
	void	Fx_Run_SSAO();
	void	Fx_Run_GTAO();						///< rendercore_gtao.cpp
	bool	Fx_Ensure_GTAO_Target();
	void*	Fx_GTAO_Shader(unsigned pass);			///< GpuPixelShader* of a gtao.ps.hlsl pass
	void	Fx_Release_GTAO_Targets();
	void	Fx_Release_GTAO_Shaders();
	bool	Taa_Wanted() const;					///< rendercore_taa.cpp
	void	Taa_Scene_Begin();
	void	Taa_Scene_End();
	const WW3DMatrixRaw& Taa_Jitter_Projection();
	bool	Taa_Ensure_Resources();
	void	Fx_Run_TAA(int x0, int y0, int x1, int y1);
	void	Taa_Release_Targets();
	void	Taa_Release_Resources();
	void	Fx_Glow_Pass(unsigned level, void* ps, void* srv0, void* srv1, unsigned src_w, unsigned src_h, void* blend);
	void	Fx_Run_Bloom_Down(void* scene_srv);	///< threshold and downsample/upsample chain from the scene
	void	Fx_Run_Bloom_Composite(int x0, int y0, int x1, int y1);
	void	Fx_Run_Heat_Mask(void* scene_srv);
	bool	Fx_Ensure_Light_Buffer();
	unsigned Fx_Build_Lights(FFPDynLightsCB& out);
	void*	Fx_Glow_Shader(unsigned index);		///< bloom.ps.hlsl / heat.ps.hlsl passes (FX_GLOW_*)
	void*	Fx_Post_Shader(unsigned variant);	///< GpuPixelShader* of the permutation (FX_POST_*)
	void*	Fx_SSAO_Shader(unsigned pass);
	void*	Fx_Gamma_Shader();					///< GpuPixelShader* for DO_GAMMA (post.ps.hlsl)
	bool	Fx_Apply_Gamma();					///< separate pass from Present, independent of RenderEffectSettings
	void	Fx_Full_Screen_Pass(void* rtv, const WW3DViewport& vp, void* ps, void* srv0, void* srv1, void* blend);
	bool	Fx_Convert_Scene(void* rtv, const WW3DViewport& vp, bool tonemap = false);	///< float scene target into an 8-bit target
	bool	Fx_Msaa_Depth() const;				///< depth effects with multisampling (on unless RENDER_EFFECT_NO_MSAA_DEPTH)
	bool	Fx_Depth_Readable() const;			///< the scene depth can be copied (single sample, or MSAA with a depth view)
	void	Fx_Unbind_Scene_Copies();			///< drop t0..t8 before a scene copy becomes a render target
	bool	Fx_Resolve_Depth(void* rtv);		///< multisampled scene depth into an R32_FLOAT target of the scene size
	bool	Fx_Create_Depth_Copy(GpuTexture2D** tex, GpuRenderTargetView** rtv, GpuShaderResourceView** srv);	///< target for a copy of the scene depth
	GpuTexture2D* Fx_Scene_8Bit(bool tonemap = false);	///< the scene in 8 bits for copies (resolved, converted)
	void	Fx_Scene_To_8Bit_Target();			///< after the 3D scene: the 2D interface draws into 8 bits
	void	Fx_Scene_Swap_Back();				///< at Present: the float scene target again
	bool	Hdr_Active() const;					///< RENDER_EFFECT_HDR applies to the current scene target
	bool	Hdr_Wanted() const;					///< RENDER_EFFECT_HDR is set and the target can carry it (no multisampling)
	void	Fx_Hdr_Bloom_Down();				///< bloom levels from the float scene before the tone curve
	void	Fx_Upload_Constants();
	void	Fx_Restore_Bindings();				///< restore the engine's target and viewport, drop bindings
	int		Fx_Soft_Mode();						///< blend mode for soft particles or -1
	void	Fx_Bind_Soft_Particles(int mode);
	WW3DPixelShaderHandle Get_FFP_Soft_Pixel_Shader();
	WW3DPixelShaderHandle Get_FFP_Coverage_Pixel_Shader();	///< ffp.ps.hlsl with ALPHA_COVERAGE
	WW3DPixelShaderHandle Get_FFP_Light_Pixel_Shader(bool coverage);	///< ffp.ps.hlsl with PIXEL_LIGHT
	void	Fx_Timestamp(unsigned index);
	void	Fx_End_Frame();						///< from Present: frame counter, collect queries
	bool	Fx_Timing_Wanted();					///< GPU timestamps: an effect, a render scale below 1 or -gpuTiming
	void	Fx_Scene_3D_Done(int x, int y, int width, int height);	///< post-processing of the view rectangle

	// --- Internal render resolution -----------------------------------------------------------------
	void	Rs_Scale_Viewport(WW3DViewport& viewport);	///< engine pixels to target pixels of the scaled scene
	unsigned Rs_Sampler_Bias_Bits();			///< mip LOD bias of the scaled scene as bits, 0 = none
	bool	Rs_Ensure_Resources();
	void	Rs_Release_Resources();
	bool	Rs_Upscale(const WW3DViewport& src, const WW3DViewport& dst);	///< small rectangle into the view rectangle
	void	Rs_Report_Scene_Size(bool larger);

	// --- Shadows ------------------------------------------------------------------------------------
	bool	Sh_Ensure_Resources();				///< shadow map, shaders, buffers, states, recording
	void	Sh_Release_Resources();
	bool	Sh_Compute_Light(const RenderShadowParams& params);	///< light matrix from camera and sun
	WW3DVertexShaderHandle Sh_Vertex_Shader(unsigned variant);
	/// Record the current draw as a shadow caster (after drawing, with the draw's state).
	void	Sh_Record(bool indexed, unsigned count, unsigned start, int base_vertex,
					  unsigned min_index, unsigned num_vertices, unsigned up_offset, unsigned up_stride);
	/// Before a buffer is overwritten or released: preserve the range used by recorded casters.
	void	Sh_Before_Write(WW3DBufferCommon* buffer, unsigned offset, unsigned size, bool discard, bool index);
	void	Sh_Preserve(void* buffer, unsigned lo, unsigned hi, bool index);	///< GpuBuffer*
	void	Sh_Render_Map();					///< replay the recording from the sun's view into the shadow map
	unsigned Sh_Draw_Casters(unsigned first, unsigned last);	///< casters [first, last) into the bound target
	bool	Sh_Ensure_Terrain();				///< terrain shadows: map and depth copy of the terrain
	bool	Sh_Ensure_Near();					///< near cascade map (same size as the far map)
	void	Sh_Near_Matrix(const WW3DMatrixRaw& far_matrix, WW3DMatrixRaw& out) const;
	float	Sh_Distance();						///< shadow range: the setting, twice that with cascades
	void	Sh_Release_Terrain();
	void	Sh_Apply();							///< apply shadows to the scene target (shadowmask.ps.hlsl)
	void	Sh_Atmosphere();					///< height fog and sun shafts after the occlusion (ATMOSPHERE)
	void	Sh_Clear_Records();

	// --- Terrain detail -----------------------------------------------------------------------------
	bool	Td_Active();						///< switch on, table present, scene target, resources created
	bool	Td_Ensure_Resources();				///< detail textures, samplers, constant buffer
	void	Td_Release_Resources();
	WW3DPixelShaderHandle Td_Pixel_Shader(WW3DPixelShaderHandle base);	///< TERRAIN_DETAIL permutation or NULL
	void	Td_Bind();							///< constants, textures and samplers for a detail draw

	// --- Terrain rock -------------------------------------------------------------------------------
	bool	Tr_Active();						///< switch on, height field and table present, scene target, resources created
	bool	Tr_Ensure_Resources();				///< rock textures, height field, samplers, constant buffer
	bool	Pa_Ensure_Resources();				///< particle vertex shader, input layout, constant buffers
	void	Pa_Release_Resources();
	bool	Mi_Capture(unsigned type, int base_vertex, unsigned min_index, unsigned num_vertices,
					   unsigned start_index, unsigned index_count);	///< collects the draw; false: draw it now
	bool	Mi_Upload(unsigned count);			///< instance matrices to the GPU
	void	Mi_Flush();							///< draws the collected meshes
	bool	Pa_Prepare(const RenderParticleSetup& setup);	///< constants, shaders and states of a particle draw
	bool	Pa_Write_Ring(const void* data, unsigned bytes, unsigned* offset);	///< into the Draw_Primitive_UP ring
	bool	Tr_Ensure_Height_Field();			///< height field texture (t11); also for the dynamic terrain lights
	bool	Tl_Ensure_Buffer();					///< cbTerrainLights with the height field constants, uploaded
	void	Tr_Release_Resources();
	WW3DPixelShaderHandle Tr_Pixel_Shader(WW3DPixelShaderHandle base, bool with_detail);	///< TERRAIN_ROCK permutation or NULL
	void	Tr_Bind(bool with_detail);			///< constants, textures and samplers for a rock draw

	// --- Terrain relief -----------------------------------------------------------------------------
	bool	Tn_Active();						///< switch on, relief atlas and height field present, scene target
	bool	Tn_Ensure_Resources();				///< constant buffer, height field and its sampler
	void	Tn_Release_Resources();
	WW3DPixelShaderHandle Tn_Pixel_Shader(WW3DPixelShaderHandle base, bool with_detail, bool with_rock);	///< TERRAIN_NORMAL permutation or NULL
	void	Tn_Bind();							///< constants, relief atlas, height field for a relief draw

	// --- Terrain light per pixel (rendercore_boden.cpp) ------------------------------------------------
	bool	Tb_Active();						///< switch on, lit terrain draw with the prelighting in the shader, scene target
	WW3DPixelShaderHandle Tb_Pixel_Shader(WW3DPixelShaderHandle base, bool with_detail, bool with_rock, bool with_normal);	///< TERRAIN_LIGHT permutation or NULL
	bool	Tb_Select(WW3DPixelShaderHandle& ps, bool with_detail, bool with_rock, bool with_normal);	///< replaces ps by its TERRAIN_LIGHT permutation
	void	Tb_Bind();							///< terrain lights, height field and its sampler in the pixel stage

	// --- Terrain tessellation -----------------------------------------------------------------------
	bool	Tt_Active();						///< switch on, heights and height field present, scene target, shaders created
	bool	Tt_Ensure_Resources();				///< hull/domain shader, constant buffer, sampler, weight and mask textures
	void	Tt_Release_Resources();
	WW3DVertexShaderHandle Tt_Vertex_Shader(unsigned permutation);	///< ffp.vs.hlsl as vs_5_0 (TERRAIN_TESS) or NULL
	float	Tt_Rock_Amplitude();				///< world units of rock displacement per height unit, 0 without rock
	void	Tt_Bind();							///< hull and domain shader with constants and textures for a patch draw
	void	Tt_Unbind();						///< hull and domain shader off
	void	Tt_Query_Begin();					///< pipeline statistics of one terrain section
	void	Tt_Query_End();
	void	Tt_Poll_Queries();					///< reads finished statistics without waiting
};

#endif // RENDERCORE_H
