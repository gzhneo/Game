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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : WW3D                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/renderdevice.cpp                         $*
 *                                                                                             *
 *              Original Author:: Jani Penttinen                                               *
 *                                                                                             *
 *                      $Author:: Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 08/05/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 170                                                         $*
 *                                                                                             *
 * 06/26/02 KM Matrix name change to avoid MAX conflicts                                       *
 * 06/27/02 KM Render to shadow buffer texture support														*
 * 06/27/02 KM Shader system updates																				*
 * 08/05/02 KM Texture class redesign 
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   RenderDevice::_Update_Texture -- Copies a texture from system memory to video memory        *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

//#define CREATE_DEVICE_MULTI_THREADED
//#define CREATE_DEVICE_FPU_PRESERVE

#include "renderdevice.h"
#include "formatinto.h"
#include "renderbackend.h"
#include "renderfvf.h"
#include "vertexbuffer.h"
#include "indexbuffer.h"
#include "meshrenderer.h"
#include "ww3d.h"
#include "camera.h"
#include "wwstring.h"
#include "matrix4.h"
#include "vertmaterial.h"
#include "rddesc.h"
#include "lightenvironment.h"
#include "statistics.h"
#include "boxrobj.h"
#include "pointgr.h"
#include "render2d.h"
#include "sortingrenderer.h"
#include "shattersystem.h"
#include "light.h"
#include "assetmgr.h"
#include "textureloader.h"
#include "missingtexture.h"
#include "thread.h"
#include <stdio.h>
// texutil.h (DirectXTex) for texture conversion.
#include "texutil.h"
#include "WW3D2/Material/pot.h"
#include "wwprofile.h"
#include "ffactory.h"
#include "rendercaps.h"
#include "formconv.h"
#include "texturemanager.h"
#include "bound.h"

#include "shdlib.h"
#include "gpulayers.h"
#include "formatinto.h"

const int DEFAULT_RESOLUTION_WIDTH = 640;
const int DEFAULT_RESOLUTION_HEIGHT = 480;
const int DEFAULT_BIT_DEPTH = 32;

bool RenderDevice_IsWindowed = true;

// FPU_PRESERVE
int RenderDevice_PreserveFPU = 0;

/***********************************************************************************
**
** RenderDevice Static Variables
**
***********************************************************************************/

static HWND						_Hwnd															= nullptr;
static int						_FullscreenAreaWidth							= 0;	///< Set_Fullscreen_Area
static int						_FullscreenAreaHeight							= 0;
bool								RenderDevice::IsInitted									= false;
bool								RenderDevice::_EnableTriangleDraw						= true;
bool								RenderDevice::_ConstantBufferRing						= true;
bool								RenderDevice::_MeshInstancing							= true;

int								RenderDevice::CurRenderDevice							= -1;
int								RenderDevice::ResolutionWidth							= DEFAULT_RESOLUTION_WIDTH;
int								RenderDevice::ResolutionHeight							= DEFAULT_RESOLUTION_HEIGHT;
int								RenderDevice::BitDepth										= DEFAULT_BIT_DEPTH;
bool								RenderDevice::IsWindowed									= false;

WW3DMatrixRaw						RenderDevice::old_world;
WW3DMatrixRaw						RenderDevice::old_view;
WW3DMatrixRaw						RenderDevice::old_prj;

// shader system additions KJM v
DWORD								RenderDevice::Vertex_Shader								= 0;
DWORD								RenderDevice::Pixel_Shader								= 0;
WW3DVertexDeclHandle 	RenderDevice::CurrentVertexDeclaration				= nullptr;
unsigned							RenderDevice::CachedIndexBaseOffset;

Vector4							RenderDevice::Vertex_Shader_Constants[MAX_VERTEX_SHADER_CONSTANTS];
Vector4							RenderDevice::Pixel_Shader_Constants[MAX_PIXEL_SHADER_CONSTANTS];

LightEnvironmentClass*		RenderDevice::Light_Environment							= nullptr;
RenderInfoClass*				RenderDevice::Render_Info									= nullptr;

ZTextureClass*					RenderDevice::Shadow_Map[MAX_SHADOW_MAPS];

Vector3							RenderDevice::Ambient_Color;
// shader system additions KJM ^

bool								RenderDevice::world_identity;
unsigned							RenderDevice::RenderStates[256];
unsigned							RenderDevice::TextureStageStates[MAX_TEXTURE_SLOTS][33];
unsigned							RenderDevice::SamplerStates[MAX_TEXTURE_SLOTS][16];
WW3DTextureHandle 		RenderDevice::Textures[MAX_TEXTURE_SLOTS];
RenderStateStruct				RenderDevice::render_state;
unsigned							RenderDevice::render_state_changed;

bool								RenderDevice::FogEnable									= false;
WW3DColor							RenderDevice::FogColor										= 0;

RenderBackend*							RenderDevice::Backend									= nullptr;
char									RenderDevice::BackendRequest[16]						= "";
const GpuLayerInfo*						RenderDevice::BackendLayer								= nullptr;
bool									RenderDevice::FloatDepth								= false;
char									RenderDevice::BackendFallbackReason[512]				= "";
WW3DInterfaceHandle 					RenderDevice::D3DInterface								= nullptr;
WW3DDeviceHandle 			RenderDevice::D3DDevice									= nullptr;
WW3DSurfaceHandle 			RenderDevice::CurrentRenderTarget						= nullptr;
WW3DSurfaceHandle 			RenderDevice::CurrentDepthBuffer						= nullptr;
WW3DSurfaceHandle 			RenderDevice::DefaultRenderTarget						= nullptr;
WW3DSurfaceHandle 			RenderDevice::DefaultDepthBuffer						= nullptr;

// Presentation parameters requested by the game; passed to the backend on every Set_Render_Device.
static int					_RequestedSwapInterval								= 1;	// 1 = VSync on
static int					_RequestedMultisample								= 0;	// 0 = no multisampling
// Selected graphics effects and the soft particle state last sent to the backend
// (Apply_Render_State_Changes).
static RenderEffectSettings	_EffectSettings;
static bool					_SoftParticlesSent									= false;
static bool					_ShadowCasterOnly									= false;
bool								RenderDevice::IsRenderToTexture							= false;

unsigned							RenderDevice::matrix_changes								= 0;
unsigned							RenderDevice::material_changes							= 0;
unsigned							RenderDevice::vertex_buffer_changes					= 0;
unsigned							RenderDevice::index_buffer_changes                = 0;
unsigned							RenderDevice::light_changes								= 0;
unsigned							RenderDevice::texture_changes							= 0;
unsigned							RenderDevice::render_state_changes						= 0;
unsigned							RenderDevice::texture_stage_state_changes			= 0;
unsigned							RenderDevice::draw_calls									= 0;
unsigned							RenderDevice::_MainThreadID								= 0;
bool								RenderDevice::CurrentDeviceLightEnables[4];
bool								RenderDevice::IsDeviceLost;
int								RenderDevice::ZBias;
float								RenderDevice::ZNear;
float								RenderDevice::ZFar;
Matrix4x4						RenderDevice::ProjectionMatrix;
Matrix4x4						RenderDevice::DeviceTransforms[WW3D_TS_WORLD+1];


// Hack test... this disables rendering of batches of too few polygons.
unsigned							RenderDevice::DrawPolygonLowBoundLimit=0;

WW3DAdapterIdentifier		RenderDevice::CurrentAdapterIdentifier;

unsigned long RenderDevice::FrameCount = 0;

bool								_DeviceSingleThreaded										= false;

unsigned							number_of_API_calls										= 0;
static unsigned				last_frame_matrix_changes								= 0;
static unsigned				last_frame_material_changes							= 0;
static unsigned				last_frame_vertex_buffer_changes						= 0;
static unsigned				last_frame_index_buffer_changes						= 0;
static unsigned				last_frame_light_changes								= 0;
static unsigned				last_frame_texture_changes								= 0;
static unsigned				last_frame_render_state_changes						= 0;
static unsigned				last_frame_texture_stage_state_changes				= 0;
static unsigned				last_frame_number_of_API_calls						= 0;
static unsigned				last_frame_draw_calls									= 0;

static DynamicVectorClass<StringClass>					_RenderDeviceNameTable;
static DynamicVectorClass<StringClass>					_RenderDeviceShortNameTable;
static DynamicVectorClass<RenderDeviceDescClass>	_RenderDeviceDescriptionTable;

RenderCleanupHook	 *RenderDevice::m_pCleanupHook=nullptr;
#ifdef EXTENDED_STATS
RenderExtendedStats	 RenderDevice::stats;
#endif
/***********************************************************************************
**
** RenderDevice Implementation
**
***********************************************************************************/

// Names of the most common device error codes, logged next to the hex HRESULT for diagnosis.
static const char* Device_Error_Code_Name(unsigned res)
{
	switch (res) {
		case WW3D_OK:						return "WW3D_OK";
		case WW3D_ERR_INVALIDCALL:			return "WW3D_ERR_INVALIDCALL";
		case WW3D_ERR_DEVICELOST:			return "WW3D_ERR_DEVICELOST";
		case WW3D_ERR_DEVICENOTRESET:		return "WW3D_ERR_DEVICENOTRESET";
		case WW3D_ERR_DRIVERINTERNALERROR:	return "WW3D_ERR_DRIVERINTERNALERROR";
		case WW3D_ERR_OUTOFVIDEOMEMORY:		return "WW3D_ERR_OUTOFVIDEOMEMORY";
		case WW3D_ERR_NOTAVAILABLE:			return "WW3D_ERR_NOTAVAILABLE";
		case WW3D_ERR_WASSTILLDRAWING:		return "WW3D_ERR_WASSTILLDRAWING";
		case static_cast<unsigned>(E_OUTOFMEMORY):		return "E_OUTOFMEMORY";
		case static_cast<unsigned>(E_INVALIDARG):		return "E_INVALIDARG";
		case static_cast<unsigned>(E_FAIL):				return "E_FAIL";
		default:							return "unknown";
	}
}

void Log_Device_ErrorCode(unsigned res)
{
	char tmp[256]="";
	WWFormat::Into(tmp, "DX Error: 0x{:08X} ({})", res, Device_Error_Code_Name(res));
	WWDEBUG_SAY(("%s", tmp));

	WWASSERT(0);
}

void Non_Fatal_Log_Device_ErrorCode(unsigned res,const char * file,int line)
{
	char tmp[256]="";
	WWFormat::Into(tmp, "DX Error: 0x{:08X} ({})", res, Device_Error_Code_Name(res));
	WWDEBUG_SAY(("Direct3D Error: %s, File: %s, Line: %d\n",tmp,file,line));
}



bool RenderDevice::Init(void * hwnd, bool lite)
{
	WWASSERT(!IsInitted);

	// zero memory
	memset(Textures,0,sizeof(Textures));
	memset(RenderStates,0,sizeof(unsigned)*256);
	memset(TextureStageStates,0,sizeof(TextureStageStates));
	memset(SamplerStates,0,sizeof(SamplerStates));
	memset(Vertex_Shader_Constants,0,sizeof(Vector4)*MAX_VERTEX_SHADER_CONSTANTS);
	memset(Pixel_Shader_Constants,0,sizeof(Vector4)*MAX_PIXEL_SHADER_CONSTANTS);
	memset(&render_state,0,sizeof(RenderStateStruct));
	memset(Shadow_Map,0,sizeof(ZTextureClass*)*MAX_SHADOW_MAPS);

	/*
	** Initialize all variables!
	*/
	_Hwnd = static_cast<HWND>(hwnd);
	_MainThreadID=ThreadClass::_Get_Current_Thread_ID();
	WWDEBUG_SAY(("RenderDevice main thread: 0x%x\n",_MainThreadID));
	CurRenderDevice = -1;
	ResolutionWidth = DEFAULT_RESOLUTION_WIDTH;
	ResolutionHeight = DEFAULT_RESOLUTION_HEIGHT;
	// Initialize Render2DClass Screen Resolution
	Render2DClass::Set_Screen_Resolution( RectClass( static_cast<float>(0), static_cast<float>(0), static_cast<float>(ResolutionWidth), static_cast<float>(ResolutionHeight) ) );
	BitDepth = DEFAULT_BIT_DEPTH;
	IsWindowed = false;	
	RenderDevice_IsWindowed = false;

	for (int light=0;light<4;++light) CurrentDeviceLightEnables[light]=false;

	::ZeroMemory(&old_world, sizeof(WW3DMatrixRaw));
	::ZeroMemory(&old_view, sizeof(WW3DMatrixRaw));
	::ZeroMemory(&old_prj, sizeof(WW3DMatrixRaw));

	//old_vertex_shader; TODO
	//old_sr_shader;
	//current_shader;

	//world_identity;
	//CurrentFogColor;

	D3DInterface = nullptr;
	D3DDevice = nullptr;

	WWDEBUG_SAY(("Reset RenderDevice statistics\n"));
	Reset_Statistics();

	Invalidate_Cached_Render_States();

	if (!lite) {
		// The backend creates the DXGI factory itself; D3DInterface/D3DDevice only mirror the backend
		// pointers (Has_Device, Device_Assert). Shutdown() keeps the backend alive; the factory returns
		// the same static object reset to its initial state, so an Init after Shutdown starts clean.
		// A request that cannot be met falls back to the first usable device layer; the caller reports the reason.
		Backend = nullptr;
		BackendLayer = nullptr;
		BackendFallbackReason[0] = 0;
		const GpuLayerInfo* refused = nullptr;
		if (BackendRequest[0]) {
			const GpuLayerInfo* wanted = Gpu_Find_Layer(BackendRequest);
			if (!wanted) {
				WWFormat::Into(BackendFallbackReason, "{} is not built into this program", BackendRequest);
			}
			else if (wanted->probe(BackendFallbackReason, sizeof(BackendFallbackReason))) {
				BackendLayer = wanted;
			}
			else {
				refused = wanted;
				if (!BackendFallbackReason[0]) WWFormat::Into(BackendFallbackReason, "unknown reason");
			}
			if (!BackendLayer) {
				WWDEBUG_SAY(("Render backend: %s not used (%s)\n", BackendRequest, BackendFallbackReason));
			}
		}
		for (unsigned i = 0; !BackendLayer && i < Gpu_Layer_Count(); ++i) {
			const GpuLayerInfo* layer = Gpu_Layer(i);
			char reason[512] = "";
			if (layer == refused) continue;
			if (layer->probe(reason, sizeof(reason))) {
				BackendLayer = layer;
			}
			else {
				WWDEBUG_SAY(("Render backend: %s not usable (%s)\n", layer->title, reason));
			}
		}
		if (!BackendLayer) {
			Report_Renderer_Unavailable("no usable device layer");
			return(false);
		}
		Backend = Render_Core_Create(*BackendLayer);
		Backend->Set_Window(hwnd);
		WWDEBUG_SAY(("Create render backend (%s)\n", BackendLayer->title));
		if (!Backend->Create_Interface()) {
			Report_Renderer_Unavailable("interface");
			return(false);
		}
		D3DInterface = Backend->Get_Interface();
		IsInitted = true;

		/*
		** Enumerate the available devices
		*/
		WWDEBUG_SAY(("Enumerate devices\n"));
		Enumerate_Devices();
		WWDEBUG_SAY(("RenderDevice Init completed\n"));
	}

	return(true);
}

void RenderDevice::Shutdown(void)
{
	if (D3DDevice) {

		Set_Render_Target (static_cast<WW3DSurfaceHandle>(nullptr));
		Release_Device();
	}

	if (Backend) {
		Backend->Release_Interface();	// the backend holds the only reference
	}
	D3DInterface=nullptr;

	{
		int max=RenderCaps::Get_Max_Textures_Per_Pass();
		for (int i = 0; i < max; i++) 
		{
			if (Textures[i]) 
			{
				Release_Resource(Textures[i]);
				Textures[i] = nullptr;
			}
		}
	}

	// The backend object stays alive on purpose. Subsystems torn down after WW3D::Shutdown
	// (TheParticleSystemManager -> PointGroupClass/SegLineRenderer -> TextureBaseClass) still free
	// their textures through Release_Resource, which needs only the COM release, not the device.
	// The object is static, so it is not in the memory pool leak report; Init resets it.
	// The runtime DLLs stay loaded until process exit for the same reason.

	_RenderDeviceNameTable.Clear();		 // note - Delete_All() resizes the vector, causing a reallocation.  Clear is better. jba.
	_RenderDeviceShortNameTable.Clear();
	_RenderDeviceDescriptionTable.Clear();	

	RenderCaps::Shutdown();
	IsInitted = false;		// 010803 srj
}

void RenderDevice::Do_Onetime_Device_Dependent_Inits(void)
{
	/*
	** Set Global render states (some of which depend on caps)
	*/
	Compute_Caps();

   /*
	** Initalize any other subsystems inside of WW3D
	*/
	MissingTexture::_Init();
	TextureFilterClass::_Init_Filters(static_cast<TextureFilterClass::TextureFilterMode>(WW3D::Get_Texture_Filter()));
	TheMeshRenderer.Init();
	SHD_INIT;
	BoxRenderObjClass::Init();
	VertexMaterialClass::Init();
	PointGroupClass::_Init(); // This needs the VertexMaterialClass to be initted
	ShatterSystem::Init();
	TextureLoader::Init();

	Set_Default_Global_Render_States();
}

inline DWORD F2DW(float f) { return *(reinterpret_cast<unsigned*>(&f)); }

// Legacy D3D state values used only by the debug name tables below (Get_Render_State_Name and the
// Get_*_Name helpers). The strings keep the original D3D names.
namespace {
enum {
	DBG_RS_WRAP4 = 132, DBG_RS_WRAP5 = 133, DBG_RS_WRAP6 = 134, DBG_RS_WRAP7 = 135,
	DBG_RS_PATCHEDGESTYLE = 163, DBG_RS_DEBUGMONITORTOKEN = 165, DBG_RS_INDEXEDVERTEXBLENDENABLE = 167, DBG_RS_TWEENFACTOR = 170,
	DBG_TA_SELECTMASK = 0x0000000f,
	DBG_ZB_FALSE = 0, DBG_ZB_TRUE = 1, DBG_ZB_USEW = 2,
	DBG_SHADE_PHONG = 3,
	DBG_BLEND_BOTHSRCALPHA = 12, DBG_BLEND_BOTHINVSRCALPHA = 13,
	DBG_VBF_DISABLE = 0, DBG_VBF_1WEIGHTS = 1, DBG_VBF_2WEIGHTS = 2, DBG_VBF_3WEIGHTS = 3, DBG_VBF_TWEENING = 255, DBG_VBF_0WEIGHTS = 256,
	DBG_PATCHEDGE_DISCRETE = 0, DBG_PATCHEDGE_CONTINUOUS = 1,
	DBG_DMT_ENABLE = 0, DBG_DMT_DISABLE = 1
};
}
void RenderDevice::Set_Default_Global_Render_States(void)
{
	RENDER_THREAD_ASSERT();
	const unsigned raster_caps = RenderCaps::Get_Raster_Caps();

	Set_Render_State(WW3D_RS_RANGEFOGENABLE, (raster_caps & WW3D_PRASTERCAPS_FOGRANGE) ? TRUE : FALSE);
	Set_Render_State(WW3D_RS_FOGTABLEMODE, WW3D_FOG_NONE);
	Set_Render_State(WW3D_RS_FOGVERTEXMODE, WW3D_FOG_LINEAR);
	Set_Render_State(WW3D_RS_SPECULARMATERIALSOURCE, WW3D_MCS_MATERIAL);
	Set_Render_State(WW3D_RS_COLORVERTEX, TRUE);
	Set_Render_State(WW3D_RS_DEPTHBIAS,0);
	Set_Texture_Stage_State(1, WW3D_TSS_BUMPENVLSCALE, F2DW(1.0f));
	Set_Texture_Stage_State(1, WW3D_TSS_BUMPENVLOFFSET, F2DW(0.0f));
	Set_Texture_Stage_State(0, WW3D_TSS_BUMPENVMAT00,F2DW(1.0f));
	Set_Texture_Stage_State(0, WW3D_TSS_BUMPENVMAT01,F2DW(0.0f));
	Set_Texture_Stage_State(0, WW3D_TSS_BUMPENVMAT10,F2DW(0.0f));
	Set_Texture_Stage_State(0, WW3D_TSS_BUMPENVMAT11,F2DW(1.0f));

//	Set_Render_State(WW3D_RS_CULLMODE, WW3D_CULL_CW);
	// Set dither mode here?
}

//MW: I added this for 'Generals'.
bool RenderDevice::Validate_Device(void)
{
	return Backend != nullptr && Backend->Validate_Device();
}

void RenderDevice::Invalidate_Cached_Render_States(void)
{
	render_state_changed=0;

	int a;
	for (a=0;a<sizeof(RenderStates)/sizeof(unsigned);++a) {
		RenderStates[a]=0x12345678;
	}
	for (a=0;a<MAX_TEXTURE_SLOTS;++a) 
	{
		for (int b=0; b<33;b++)
		{
			TextureStageStates[a][b]=0x12345678;
		}
		for (int c=0; c<16;c++)
		{
			SamplerStates[a][c]=0x12345678;
		}
		//Need to explicitly set texture to NULL, otherwise app will not be able to
		//set it to null because of redundant state checker. MW
		if (Backend && Has_Device())
			Backend->Set_Texture(a,nullptr);
		if (Textures[a] != nullptr) {
			Release_Resource(Textures[a]);
		}
		Textures[a]=nullptr;
	}

	// Invalidate the vertex declaration cache
	if (Backend && Has_Device()) {
		Backend->Set_Vertex_Declaration(nullptr);
	}
	CurrentVertexDeclaration = nullptr;

	ShaderClass::Invalidate();

	//Need to explicitly set render_state texture pointers to NULL. MW
	Release_Render_State();

	// (gth) clear the matrix shadows too
	for (int i=0; i<WW3D_TS_WORLD+1; i++) {
		DeviceTransforms[i][0].Set(0,0,0,0);
		DeviceTransforms[i][1].Set(0,0,0,0);
		DeviceTransforms[i][2].Set(0,0,0,0);
		DeviceTransforms[i][3].Set(0,0,0,0);
	}

}

void RenderDevice::Do_Onetime_Device_Dependent_Shutdowns(void)
{
	/*
	** Shutdown ww3d systems
	*/
	int i;
	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		if (render_state.vertex_buffers[i]) render_state.vertex_buffers[i]->Release_Engine_Ref();
		REF_PTR_RELEASE(render_state.vertex_buffers[i]);
	}
	if (render_state.index_buffer) render_state.index_buffer->Release_Engine_Ref();
	REF_PTR_RELEASE(render_state.index_buffer);
	REF_PTR_RELEASE(render_state.material);
	for (i=0;i<RenderCaps::Get_Max_Textures_Per_Pass();++i) REF_PTR_RELEASE(render_state.Textures[i]);


	TextureLoader::Deinit();
	SortingRendererClass::Deinit();
	DynamicVBAccessClass::_Deinit();
	DynamicIBAccessClass::_Deinit();
	ShatterSystem::Shutdown();
	PointGroupClass::_Shutdown();
	VertexMaterialClass::Shutdown();
	BoxRenderObjClass::Shutdown();
	SHD_SHUTDOWN;
	TheMeshRenderer.Shutdown();
	MissingTexture::_Deinit();


}


void RenderDevice::Set_Backend_Request(const char* name)
{
	WWFormat::Into(BackendRequest, "{}", name ? name : "");
}

const char* RenderDevice::Get_Backend_Name()	{ return BackendLayer ? BackendLayer->name : ""; }
const char* RenderDevice::Get_Backend_Title()	{ return BackendLayer ? BackendLayer->title : ""; }
unsigned RenderDevice::Get_Backend_Choices()	{ return Gpu_Layer_Count(); }

bool RenderDevice::Is_Backend(const char* name)
{
	return BackendLayer && name && _stricmp(BackendLayer->name, name) == 0;
}

// No device layer starts. Tells the user once per process what the layer in use (or the default one)
// needs (log and message box -- game and WorldBuilder retry with 16 bit afterwards). The caller then
// aborts (game: ERROR_INVALID_D3D, WorldBuilder: DEBUG_CRASH).
void RenderDevice::Report_Renderer_Unavailable(const char* stage)
{
	const GpuLayerInfo* layer = BackendLayer ? BackendLayer : Gpu_Layer(0);
	WWDEBUG_SAY(("Render backend: %s not available (%s). Minimum is %s.\n", layer->title, stage, layer->requirement));
	static bool reported = false;
	if (reported) return;
	reported = true;
	char text[512];
	char caption[64];
	WWFormat::Into(text, "{} could not be started.\n\nThe minimum is {}.", layer->title, layer->requirement);
	WWFormat::Into(caption, "{} not available", layer->title);
	::MessageBoxA(_Hwnd, text, caption, MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

bool RenderDevice::Create_Device(void)
{
	WWASSERT(D3DDevice==nullptr);	// for now, once you've created a device, you're stuck with it!

	// The backend creates the device and sets the adapter identifier. A device can still fail on the
	// window itself (Wine has no Vulkan surfaces for child windows): then the next usable device layer
	// takes over, with the reason kept for the caller like a failed probe.
	bool created = Backend->Create_Device();
	const GpuLayerInfo* failed = BackendLayer;
	for (unsigned i = 0; !created && i < Gpu_Layer_Count(); ++i) {
		const GpuLayerInfo* layer = Gpu_Layer(i);
		char reason[512] = "";
		if (layer == failed || !layer->probe(reason, sizeof(reason))) continue;
		const char* error = failed->device_error ? failed->device_error() : nullptr;
		WWFormat::Into(BackendFallbackReason, "{} device could not be created: {}", failed->title,
			(error && error[0]) ? error : "unknown reason");
		WWDEBUG_SAY(("Render backend: %s, starting %s\n", BackendFallbackReason, layer->title));
		BackendLayer = layer;
		created = Render_Core_Use_Layer(Backend, *layer) && Backend->Create_Device();
	}
	if (!created) {
		Report_Renderer_Unavailable("device");
		return false;
	}
	D3DDevice = Backend->Get_Device();

	/*
	** Initialize all subsystems
	*/
	Do_Onetime_Device_Dependent_Inits();
	return true;
}

bool RenderDevice::Reset_Device(bool reload_assets)
{
	WWDEBUG_SAY(("Resetting device.\n"));
	RENDER_THREAD_ASSERT();
	if ((IsInitted) && (D3DDevice != nullptr)) {
		// Release all non-MANAGED stuff
		WW3D::_Invalidate_Textures();

		for (unsigned i=0;i<MAX_VERTEX_STREAMS;++i) 
		{
			Set_Vertex_Buffer (nullptr,i);
		}
		Set_Index_Buffer (nullptr, 0);
		if (m_pCleanupHook) {
			m_pCleanupHook->ReleaseResources();
		}
		DynamicVBAccessClass::_Deinit();
		DynamicIBAccessClass::_Deinit();
		TextureManagerClass::Release_Textures();
		Backend->Release_Render_Target_Depth_Stencils();	// release default-pool surfaces before the reset
		SHD_SHUTDOWN_SHADERS;

		// Reset frame count to reflect the flipping chain being reset by Reset()
		FrameCount = 0;

		memset(Vertex_Shader_Constants,0,sizeof(Vector4)*MAX_VERTEX_SHADER_CONSTANTS);
		memset(Pixel_Shader_Constants,0,sizeof(Vector4)*MAX_PIXEL_SHADER_CONSTANTS);

		HRESULT hr=Backend->Get_Device_State();
		if (hr != WW3D_ERR_DEVICELOST )
		{	hr = Backend->Reset_Device();
			if (hr != WW3D_OK)
				return false;	//reset failed.
		}
		else
			return false;	//device is lost and can't be reset.

		if (reload_assets)
		{
			TextureManagerClass::Recreate_Textures();
			if (m_pCleanupHook) {
				m_pCleanupHook->ReAcquireResources();
			}
		}
		Invalidate_Cached_Render_States();
		Set_Default_Global_Render_States();
		SHD_INIT_SHADERS;
		WWDEBUG_SAY(("Device reset completed\n"));
		return true;
	}
	WWDEBUG_SAY(("Device reset failed\n"));
	return false;
}

void RenderDevice::Release_Device(void)
{
	Release_Vertex_Declarations();	// release before the device
	if (D3DDevice) {

		for (int a=0;a<MAX_TEXTURE_SLOTS;++a)	// all slots, not only the tracked stages
		{	//release references to any textures that were used in last rendering call
			Backend->Set_Texture(a,nullptr);
		}

		Backend->Set_Stream_Source(0, nullptr, 0, 0);	//release reference count on last rendered vertex buffer
		Backend->Set_Indices(nullptr);	//release reference count on last rendered index buffer


		/*
		** Release the current vertex and index buffers
		*/
		for (unsigned i=0;i<MAX_VERTEX_STREAMS;++i) 
		{
			if (render_state.vertex_buffers[i]) render_state.vertex_buffers[i]->Release_Engine_Ref();
			REF_PTR_RELEASE(render_state.vertex_buffers[i]);
		}
		if (render_state.index_buffer) render_state.index_buffer->Release_Engine_Ref();
		REF_PTR_RELEASE(render_state.index_buffer);

		Backend->Release_Render_Target_Depth_Stencils();

		/*
		** Shutdown all subsystems
		*/
		Do_Onetime_Device_Dependent_Shutdowns();

		/*
		** Release the device
		*/

		Backend->Release_Device();
		D3DDevice=nullptr;
	}
}

// =========================================================================================
// Vertex declarations instead of FVF codes, runtime HLSL compilation.
// =========================================================================================

HRESULT RenderDevice::Create_Vertex_Declaration(const WW3DVertexElement* elements, WW3DVertexDeclHandle * out)
{
	HRESULT hr = WW3D_OK;
	Device_ErrorCode(hr = Backend->Create_Vertex_Declaration(elements, out));
	return hr;
}

void RenderDevice::Set_Vertex_Declaration(WW3DVertexDeclHandle decl)
{
	if (decl == CurrentVertexDeclaration) return;
	CurrentVertexDeclaration = decl;
	Backend->Set_Vertex_Declaration(decl);
	render_state_changed |= VERTEX_BUFFER_CHANGED;
}

// Declaration cache per FVF code. The declarations are device objects, released in Release_Device
// and recreated on demand.
struct FVFDeclarationEntry { DWORD fvf; WW3DVertexDeclHandle decl; };
static const unsigned MAX_FVF_DECLARATIONS = 32;
static FVFDeclarationEntry _FVFDeclarations[MAX_FVF_DECLARATIONS];
static unsigned _FVFDeclarationCount = 0;

WW3DVertexDeclHandle RenderDevice::Get_Vertex_Declaration_For_FVF(DWORD fvf)
{
	for (unsigned i = 0; i < _FVFDeclarationCount; ++i) {
		if (_FVFDeclarations[i].fvf == fvf) return _FVFDeclarations[i].decl;
	}
	WW3DVertexElement elements[WW3D_MAX_DECL_LENGTH + 1];
	FVF_To_Vertex_Elements(fvf, elements, WW3D_MAX_DECL_LENGTH + 1);
	WW3DVertexDeclHandle decl = nullptr;
	HRESULT hr = Create_Vertex_Declaration(elements, &decl);
	if (FAILED(hr) || !decl) {
		WWDEBUG_SAY(("Get_Vertex_Declaration_For_FVF: cannot create declaration for FVF 0x%08X (0x%08X)\n", (unsigned)fvf, (unsigned)hr));
		return nullptr;
	}
	if (_FVFDeclarationCount < MAX_FVF_DECLARATIONS) {
		_FVFDeclarations[_FVFDeclarationCount].fvf = fvf;
		_FVFDeclarations[_FVFDeclarationCount].decl = decl;
		++_FVFDeclarationCount;
	} else {
		WWASSERT_PRINT(0, "FVF declaration cache full");
		Release_Resource(decl);	// cache full: returning it uncached would leak
		return nullptr;
	}
	return decl;
}

void RenderDevice::Release_Vertex_Declarations()
{
	for (unsigned i = 0; i < _FVFDeclarationCount; ++i) {
		if (_FVFDeclarations[i].decl) Release_Resource(_FVFDeclarations[i].decl);
		_FVFDeclarations[i].decl = nullptr;
	}
	_FVFDeclarationCount = 0;
	CurrentVertexDeclaration = nullptr;
}

void RenderDevice::Draw_Screen_Quad_UP(const void* vertices, UINT stride, DWORD fvf)
{
	RENDER_THREAD_ASSERT();

	WW3DViewport vp;
	Backend->Get_Viewport(&vp);
	if (vp.Width == 0 || vp.Height == 0) return;

	// Pixel ortho projection (Westwood convention, column vectors): x_ndc = (px - X) * 2 / W - 1,
	// y_ndc = 1 - (py - Y) * 2 / H, z unchanged. The viewport transform maps this back to exact
	// screen coordinates. Callers pass whole pixel coordinates without a -0.5 correction.
	// While the 3D scene is drawn at a lower internal resolution, the target pixels per caller pixel are
	// sx/sy (the viewport is already scaled): x_ndc = (px * sx - X) * 2 / W - 1.
	float sx = 1.0f, sy = 1.0f;
	Backend->Get_Render_Scale(&sx, &sy);
	const float w = static_cast<float>(vp.Width), h = static_cast<float>(vp.Height);
	Matrix4x4 ortho(
		Vector4(2.0f * sx / w, 0.0f, 0.0f, -1.0f - 2.0f * static_cast<float>(vp.X) / w),
		Vector4(0.0f, -2.0f * sy / h, 0.0f, 1.0f + 2.0f * static_cast<float>(vp.Y) / h),
		Vector4(0.0f, 0.0f, 1.0f, 0.0f),
		Vector4(0.0f, 0.0f, 0.0f, 1.0f));
	Matrix4x4 old_proj(true);
	Get_Transform(WW3D_TS_PROJECTION, old_proj);

	// Set the matrices directly on the device (it expects the transpose); the wrapper's world/view
	// are restored at the end.
	Matrix4x4 identity(true);
	_Set_Device_Transform(WW3D_TS_WORLD, identity);
	_Set_Device_Transform(WW3D_TS_VIEW, identity);
	Matrix4x4 t = ortho.Transpose();
	_Set_Device_Transform(WW3D_TS_PROJECTION, t);

	// Screen quads bypass lighting and vertex fog.
	unsigned lighting = Get_Render_State(WW3D_RS_LIGHTING);
	unsigned fog = Get_Render_State(WW3D_RS_FOGENABLE);
	Set_Render_State(WW3D_RS_LIGHTING, FALSE);
	Set_Render_State(WW3D_RS_FOGENABLE, FALSE);

	Set_Vertex_Shader(fvf);
	Backend->Draw_Primitive_UP(WW3D_PT_TRIANGLESTRIP, 2, vertices, stride);

	Set_Render_State(WW3D_RS_LIGHTING, lighting);
	Set_Render_State(WW3D_RS_FOGENABLE, fog);
	t = old_proj.Transpose();
	_Set_Device_Transform(WW3D_TS_PROJECTION, t);
	// Restore world and view on the device right away, not only as changed flags. Callers (screen
	// filters, shadows, smudge) often call Invalidate_Cached_Render_States() afterwards, which clears
	// the flags; the identity view would then stay on the device and later draws that set only their
	// world matrix would end up behind the camera.
	_Set_Device_Transform(WW3D_TS_WORLD, render_state.world);
	_Set_Device_Transform(WW3D_TS_VIEW, render_state.view);
	render_state_changed |= WORLD_CHANGED | VIEW_CHANGED;
}

HRESULT RenderDevice::Create_Vertex_Shader_From_HLSL(const char* source, unsigned length, const char* debug_name, WW3DVertexShaderHandle * out)
{
	// Compiled by the backend (shader model 4.0, sources under Shaders/sm4).
	return Backend->Create_Vertex_Shader_From_HLSL(source, length, debug_name, out);
}

HRESULT RenderDevice::Create_Pixel_Shader_From_HLSL(const char* source, unsigned length, const char* debug_name, WW3DPixelShaderHandle * out)
{
	return Backend->Create_Pixel_Shader_From_HLSL(source, length, debug_name, out);
}

void RenderDevice::Enumerate_Devices()
{
	Device_Assert();

	const unsigned adapter_count = Backend->Get_Adapter_Count();
	for (unsigned adapter_index=0; adapter_index<adapter_count; adapter_index++) {

		WW3DAdapterIdentifier id;
		if (Backend->Get_Adapter_Identifier(adapter_index,&id)) {

			/*
			** Set up the render device description
			** TODO: Fill in more fields of the render device description?  (need some lookup tables)
			*/
			RenderDeviceDescClass desc;
			desc.set_device_name(id.Description);
			desc.set_driver_name(id.Driver);

			char buf[64];
			WWFormat::Into(buf, "{:d}.{:d}.{:d}.{:d}",
				HIWORD((unsigned)(id.DriverVersion >> 32)),
				LOWORD((unsigned)(id.DriverVersion >> 32)),
				HIWORD((unsigned)(id.DriverVersion & 0xFFFFFFFFu)),
				LOWORD((unsigned)(id.DriverVersion & 0xFFFFFFFFu)));

			desc.set_driver_version(buf);

			desc.AdapterIdentifier = id;

			/*
			** Enumerate the resolutions
			*/
			desc.reset_resolution_list();
			const unsigned mode_count = Backend->Get_Adapter_Mode_Count(adapter_index, WW3D_FMT_X8R8G8B8);
			for (unsigned mode_index=0; mode_index<mode_count; mode_index++) {
				unsigned mode_width=0, mode_height=0, mode_format=0;

				if (Backend->Enum_Adapter_Mode(adapter_index,WW3D_FMT_X8R8G8B8,mode_index,&mode_width,&mode_height,&mode_format)) {
					int bits = 0;
					switch (mode_format)
					{
						case WW3D_FMT_R8G8B8:
						case WW3D_FMT_A8R8G8B8:
						case WW3D_FMT_X8R8G8B8:		bits = 32; break;

						case WW3D_FMT_R5G6B5:
						case WW3D_FMT_X1R5G5B5:		bits = 16; break;
					}

					/*
					** If we recognize the format, add it to the list
					** TODO: should we handle more formats?  will any cards report more than 24 or 16 bit?
					*/
					if (bits != 0) {
						desc.add_resolution(mode_width,mode_height,bits);
					}
				}
			}

			// IML: If the device has one or more valid resolutions add it to the device list.
			// NOTE: Testing has shown that there are drivers with zero resolutions.
			if (desc.Enumerate_Resolutions().Count() > 0) {

				/*
				** Set up the device name
				*/
				StringClass device_name(id.Description,true);
				_RenderDeviceNameTable.Add(device_name);
				_RenderDeviceShortNameTable.Add(device_name);	// for now, just add the same name to the "pretty name table"

				/*
				** Add the render device to our table
				*/
				_RenderDeviceDescriptionTable.Add(desc);
			}
		}
	}
}

bool RenderDevice::Set_Any_Render_Device(void)
{
	// Then fullscreen
	int dev_number;
	for (dev_number = 0; dev_number < _RenderDeviceNameTable.Count(); dev_number++) {
		if (Set_Render_Device(dev_number,-1,-1,-1,0,false)) {
			return true;
		}
	}

	// Try windowed first
	for (dev_number = 0; dev_number < _RenderDeviceNameTable.Count(); dev_number++) {
		if (Set_Render_Device(dev_number,-1,-1,-1,1,false)) {
			return true;
		}
	}

	return false;
}

bool RenderDevice::Set_Render_Device
(
	const char * dev_name,
	int width,
	int height,
	int bits,
	int windowed,
	bool resize_window
)
{
	for ( int dev_number = 0; dev_number < _RenderDeviceNameTable.Count(); dev_number++) {
		if ( strcmp( dev_name, _RenderDeviceNameTable[dev_number]) == 0) {
			return Set_Render_Device( dev_number, width, height, bits, windowed, resize_window );
		}

		if ( strcmp( dev_name, _RenderDeviceShortNameTable[dev_number]) == 0) {
			return Set_Render_Device( dev_number, width, height, bits, windowed, resize_window );
		}
	}
	return false;
}

void RenderDevice::Get_Format_Name(unsigned int format, StringClass *tex_format)
{
		*tex_format="Unknown";
		switch (format) {
		case WW3D_FMT_A8R8G8B8: *tex_format="WW3D_FMT_A8R8G8B8"; break;
		case WW3D_FMT_R8G8B8: *tex_format="WW3D_FMT_R8G8B8"; break;
		case WW3D_FMT_A4R4G4B4: *tex_format="WW3D_FMT_A4R4G4B4"; break;
		case WW3D_FMT_A1R5G5B5: *tex_format="WW3D_FMT_A1R5G5B5"; break;
		case WW3D_FMT_R5G6B5: *tex_format="WW3D_FMT_R5G6B5"; break;
		case WW3D_FMT_L8: *tex_format="WW3D_FMT_L8"; break;
		case WW3D_FMT_A8: *tex_format="WW3D_FMT_A8"; break;
		case WW3D_FMT_P8: *tex_format="WW3D_FMT_P8"; break;
		case WW3D_FMT_X8R8G8B8: *tex_format="WW3D_FMT_X8R8G8B8"; break;
		case WW3D_FMT_X1R5G5B5: *tex_format="WW3D_FMT_X1R5G5B5"; break;
		case WW3D_FMT_R3G3B2: *tex_format="WW3D_FMT_R3G3B2"; break;
		case WW3D_FMT_A8R3G3B2: *tex_format="WW3D_FMT_A8R3G3B2"; break;
		case WW3D_FMT_X4R4G4B4: *tex_format="WW3D_FMT_X4R4G4B4"; break;
		case WW3D_FMT_A8P8: *tex_format="WW3D_FMT_A8P8"; break;
		case WW3D_FMT_A8L8: *tex_format="WW3D_FMT_A8L8"; break;
		case WW3D_FMT_A4L4: *tex_format="WW3D_FMT_A4L4"; break;
		case WW3D_FMT_V8U8: *tex_format="WW3D_FMT_V8U8"; break;
		case WW3D_FMT_L6V5U5: *tex_format="WW3D_FMT_L6V5U5"; break;  
		case WW3D_FMT_X8L8V8U8: *tex_format="WW3D_FMT_X8L8V8U8"; break;
		case WW3D_FMT_Q8W8V8U8: *tex_format="WW3D_FMT_Q8W8V8U8"; break;
		case WW3D_FMT_V16U16: *tex_format="WW3D_FMT_V16U16"; break;
		case WW3D_FMT_UYVY: *tex_format="WW3D_FMT_UYVY"; break;
		case WW3D_FMT_YUY2: *tex_format="WW3D_FMT_YUY2"; break;
		case WW3D_FMT_DXT1: *tex_format="WW3D_FMT_DXT1"; break;
		case WW3D_FMT_DXT2: *tex_format="WW3D_FMT_DXT2"; break;
		case WW3D_FMT_DXT3: *tex_format="WW3D_FMT_DXT3"; break;
		case WW3D_FMT_DXT4: *tex_format="WW3D_FMT_DXT4"; break;
		case WW3D_FMT_DXT5: *tex_format="WW3D_FMT_DXT5"; break;
		case WW3D_FMT_BC7: *tex_format="WW3D_FMT_BC7"; break;
		case WW3D_FMT_D16_LOCKABLE: *tex_format="WW3D_FMT_D16_LOCKABLE"; break;
		case WW3D_FMT_D32: *tex_format="WW3D_FMT_D32"; break;
		case WW3D_FMT_D15S1: *tex_format="WW3D_FMT_D15S1"; break;
		case WW3D_FMT_D24S8: *tex_format="WW3D_FMT_D24S8"; break;
		case WW3D_FMT_D16: *tex_format="WW3D_FMT_D16"; break;
		case WW3D_FMT_D24X8: *tex_format="WW3D_FMT_D24X8"; break;
		case WW3D_FMT_D24X4S4: *tex_format="WW3D_FMT_D24X4S4"; break;
		default:	break;
		}
}

// Fits the window to the display mode. Windowed: the client area gets exactly the resolution
// (AdjustWindowRect adds frame and caption). Fullscreen: there is no exclusive mode; the window
// becomes a borderless window covering the monitor it is on, not topmost, so Alt-Tab works as with
// any other window. The swap chain keeps the chosen resolution; Present places it into the window
// with the correct aspect ratio (presentlayout.h).
void RenderDevice::Fit_Window_To_Resolution(bool windowed)
{
#ifdef _WINDOWS
	if (!_Hwnd) return;

	if (!windowed) {
		MONITORINFO info;
		::ZeroMemory(&info, sizeof(info));
		info.cbSize = sizeof(info);
		HMONITOR monitor = ::MonitorFromWindow(_Hwnd, MONITOR_DEFAULTTONEAREST);
		if (!monitor || !::GetMonitorInfo(monitor, &info))
			return;
		RECT target = info.rcMonitor;
		const int monitor_width  = target.right - target.left;
		const int monitor_height = target.bottom - target.top;
		if (_FullscreenAreaWidth > 0 && _FullscreenAreaHeight > 0 &&
			_FullscreenAreaWidth <= monitor_width && _FullscreenAreaHeight <= monitor_height) {
			target.left  += (monitor_width  - _FullscreenAreaWidth)  / 2;
			target.top   += (monitor_height - _FullscreenAreaHeight) / 2;
			target.right  = target.left + _FullscreenAreaWidth;
			target.bottom = target.top  + _FullscreenAreaHeight;
		}
		RECT current;
		if (::GetWindowRect(_Hwnd, &current) && ::EqualRect(&current, &target))
			return;
		::SetWindowPos(_Hwnd, nullptr, target.left, target.top, target.right - target.left,
					   target.bottom - target.top, SWP_NOZORDER | SWP_NOACTIVATE);
		return;
	}

	// Get the current dimensions of the 'render area' of the window
	RECT rect = { 0 };
	::GetClientRect (_Hwnd, &rect);

	// Is the window the correct size for this resolution?
	if ((rect.right-rect.left) == ResolutionWidth && (rect.bottom-rect.top) == ResolutionHeight)
		return;

	// Calculate what the main window's bounding rectangle should be to
	// accomodate this resolution
	rect.left = 0;
	rect.top = 0;
	rect.right = ResolutionWidth;
	rect.bottom = ResolutionHeight;
	DWORD dwstyle = ::GetWindowLong (_Hwnd, GWL_STYLE);
	AdjustWindowRect (&rect, dwstyle, FALSE);

	// Resize the window to fit this resolution
	::SetWindowPos (_Hwnd,
					 nullptr,
					 0,
					 0,
					 rect.right-rect.left,
					 rect.bottom-rect.top,
					 SWP_NOZORDER | SWP_NOMOVE);
#endif
}

void RenderDevice::Set_Fullscreen_Area(int width, int height)
{
	_FullscreenAreaWidth  = (width  > 0) ? width  : 0;
	_FullscreenAreaHeight = (height > 0) ? height : 0;
}

void RenderDevice::Window_To_Render(int & x, int & y)
{
#ifdef _WINDOWS
	if (!_Hwnd || !Backend) return;
	RECT client = { 0, 0, 0, 0 };
	if (!::GetClientRect(_Hwnd, &client)) return;
	Present_Window_To_Render(Backend->Get_Present_Layout(), client.right, client.bottom, x, y);
#endif
}

bool RenderDevice::Set_Render_Device(int dev, int width, int height, int bits, int windowed,
								   bool resize_window,bool reset_device, bool restore_assets)
{
	WWASSERT(IsInitted);
	WWASSERT(dev >= -1);
	WWASSERT(dev < _RenderDeviceNameTable.Count());

	/*
	** If user has never selected a render device, start out with device 0
	*/
	if ((CurRenderDevice == -1) && (dev == -1)) {
		CurRenderDevice = 0;
	} else if (dev != -1) {
		CurRenderDevice = dev;
	}
	
	/*
	** If user doesn't want to change res, set the res variables to match the 
	** current resolution
	*/
	if (width != -1)		ResolutionWidth = width;
	if (height != -1)		ResolutionHeight = height;
	
	// Initialize Render2DClass Screen Resolution
	Render2DClass::Set_Screen_Resolution( RectClass( static_cast<float>(0), static_cast<float>(0), static_cast<float>(ResolutionWidth), static_cast<float>(ResolutionHeight) ) );

	if (bits != -1)		BitDepth = bits;
	if (windowed != -1)	IsWindowed = (windowed != 0);
	RenderDevice_IsWindowed = IsWindowed;

	WWDEBUG_SAY(("Attempting Set_Render_Device: name: %s (%s:%s), width: %d, height: %d, windowed: %d\n",
		_RenderDeviceNameTable[CurRenderDevice].Peek_Buffer(),_RenderDeviceDescriptionTable[CurRenderDevice].Get_Driver_Name(),
		_RenderDeviceDescriptionTable[CurRenderDevice].Get_Driver_Version(),ResolutionWidth,ResolutionHeight,(IsWindowed ? 1 : 0)));

#ifdef _WINDOWS
	if (resize_window)
		Fit_Window_To_Resolution(IsWindowed);
#endif
	//must be either resetting existing device or creating a new one.
	WWASSERT(reset_device || D3DDevice == nullptr);

	/*
	** The backend builds the present parameters, picks formats and multisampling; in windowed mode
	** BitDepth may come from the desktop mode.
	*/
	RenderDeviceSetup setup;
	setup.adapter             = static_cast<unsigned>(CurRenderDevice);
	setup.hwnd                = _Hwnd;
	setup.width               = ResolutionWidth;
	setup.height              = ResolutionHeight;
	setup.bit_depth           = BitDepth;
	setup.windowed            = IsWindowed;
	setup.multisample_samples = _RequestedMultisample;
	setup.swap_interval       = _RequestedSwapInterval;

	if (!Backend->Build_Device_Parameters(setup))
		return false;
	BitDepth = setup.bit_depth;

	bool ret;

	if (reset_device)
		ret = Reset_Device(restore_assets);	//reset device without restoring data - we're likely switching out of the app.
	else
		ret = Create_Device();

	WWDEBUG_SAY(("Reset/Create_Device done, reset_device=%d, restore_assets=%d\n", reset_device, restore_assets));

	return ret;
}

bool RenderDevice::Set_Next_Render_Device(void)
{
	int new_dev = (CurRenderDevice + 1) % _RenderDeviceNameTable.Count();
	return Set_Render_Device(new_dev);
}

void RenderDevice::Set_Swap_Interval(int swap)
{
	// Remembered so it survives a later rebuild of the present parameters (Set_Render_Device on a
	// resolution change). Before device creation only the remembering matters (Reset_Device is a no-op).
	_RequestedSwapInterval = swap;
	if (Backend) Backend->Set_Swap_Interval(swap);

	if (D3DDevice) Reset_Device();
}

void RenderDevice::Set_Multisample_Samples(int samples)
{
	if (samples < 0) samples = 0;
	if (samples > 16) samples = 16;
	_RequestedMultisample = samples;
}

int RenderDevice::Get_Multisample_Samples(void)
{
	return _RequestedMultisample;
}

int RenderDevice::Get_Swap_Interval(void)
{
	return Backend ? Backend->Get_Swap_Interval() : 0;
}

bool RenderDevice::Has_Stencil(void)
{
	return Backend != nullptr && Backend->Has_Stencil();
}

int RenderDevice::Get_Render_Device_Count(void)
{
	return _RenderDeviceNameTable.Count();

}
int RenderDevice::Get_Render_Device(void)
{
	assert(IsInitted);
	return CurRenderDevice;
}

const RenderDeviceDescClass & RenderDevice::Get_Render_Device_Desc(int deviceidx)
{
	WWASSERT(IsInitted);

	if ((deviceidx == -1) && (CurRenderDevice == -1)) {
		CurRenderDevice = 0;
	}

	// if the device index is -1 then we want the current device
	if (deviceidx == -1) {
		WWASSERT(CurRenderDevice >= 0);
		WWASSERT(CurRenderDevice < _RenderDeviceNameTable.Count());
		return _RenderDeviceDescriptionTable[CurRenderDevice];
	}

	// We can only ask for multiple device information if the devices
	// have been detected.
	WWASSERT(deviceidx >= 0);
	WWASSERT(deviceidx < _RenderDeviceNameTable.Count());
	return _RenderDeviceDescriptionTable[deviceidx];
}

const char * RenderDevice::Get_Render_Device_Name(int device_index)
{
	device_index = device_index % _RenderDeviceShortNameTable.Count();
	return _RenderDeviceShortNameTable[device_index];
}

bool RenderDevice::Set_Device_Resolution(int width,int height,int bits,int windowed, bool resize_window)
{
	if (D3DDevice != nullptr) {

		if (width != -1) {
			ResolutionWidth = width;
		}
		if (height != -1) {
			ResolutionHeight = height;
		}
		if (resize_window)
			Fit_Window_To_Resolution(IsWindowed);
#pragma message("TODO: support changing windowed status and changing the bit depth")
		if (Backend) Backend->Set_Present_Size(ResolutionWidth, ResolutionHeight);
		return Reset_Device();
	} else {
		return false;
	}
}

void RenderDevice::Get_Device_Resolution(int & set_w,int & set_h,int & set_bits,bool & set_windowed)
{
	WWASSERT(IsInitted);

	set_w = ResolutionWidth;
	set_h = ResolutionHeight;
	set_bits = BitDepth;
	set_windowed = IsWindowed;

	return ;
}

void RenderDevice::Get_Render_Target_Resolution(int & set_w,int & set_h,int & set_bits,bool & set_windowed)
{
	WWASSERT(IsInitted);

	if (CurrentRenderTarget != nullptr) {
		WW3DSurfaceDesc info;
		Backend->Get_Surface_Desc(CurrentRenderTarget, &info);

		set_w				= info.Width;
		set_h				= info.Height;
		set_bits			= BitDepth;		// should we get the actual bit depth of the target?
		set_windowed	= IsWindowed;	// this doesn't really make sense for render targets (shouldn't matter)...

	} else {
		Get_Device_Resolution (set_w, set_h, set_bits, set_windowed);
	}

	return ;
}



void RenderDevice::Reset_Statistics()
{
	matrix_changes	= 0;
	material_changes = 0;
	vertex_buffer_changes = 0;
	index_buffer_changes = 0;
	light_changes = 0;
	texture_changes = 0;
	render_state_changes =0;
	texture_stage_state_changes =0;
	draw_calls =0;

	number_of_API_calls = 0;
	last_frame_matrix_changes = 0;
	last_frame_material_changes = 0;
	last_frame_vertex_buffer_changes = 0;
	last_frame_index_buffer_changes = 0;
	last_frame_light_changes = 0;
	last_frame_texture_changes = 0;
	last_frame_render_state_changes = 0;
	last_frame_texture_stage_state_changes = 0;
	last_frame_number_of_API_calls = 0;
	last_frame_draw_calls =0;
}

void RenderDevice::Begin_Statistics()
{
	matrix_changes=0;
	material_changes=0;
	vertex_buffer_changes=0;
	index_buffer_changes=0;
	light_changes=0;
	texture_changes = 0;
	render_state_changes =0;
	texture_stage_state_changes =0;
	number_of_API_calls=0;
	draw_calls=0;
}

void RenderDevice::End_Statistics()
{
	last_frame_matrix_changes=matrix_changes;
	last_frame_material_changes=material_changes;
	last_frame_vertex_buffer_changes=vertex_buffer_changes;
	last_frame_index_buffer_changes=index_buffer_changes;
	last_frame_light_changes=light_changes;
	last_frame_texture_changes = texture_changes;
	last_frame_render_state_changes = render_state_changes;
	last_frame_texture_stage_state_changes = texture_stage_state_changes;
	last_frame_number_of_API_calls=number_of_API_calls;
	last_frame_draw_calls=draw_calls;
}

unsigned RenderDevice::Get_Last_Frame_Matrix_Changes()			{ return last_frame_matrix_changes; }
unsigned RenderDevice::Get_Last_Frame_Material_Changes()		{ return last_frame_material_changes; }
unsigned RenderDevice::Get_Last_Frame_Vertex_Buffer_Changes()	{ return last_frame_vertex_buffer_changes; }
unsigned RenderDevice::Get_Last_Frame_Index_Buffer_Changes()	{ return last_frame_index_buffer_changes; }
unsigned RenderDevice::Get_Last_Frame_Light_Changes()			{ return last_frame_light_changes; }
unsigned RenderDevice::Get_Last_Frame_Texture_Changes()			{ return last_frame_texture_changes; }
unsigned RenderDevice::Get_Last_Frame_Render_State_Changes()	{ return last_frame_render_state_changes; }
unsigned RenderDevice::Get_Last_Frame_Texture_Stage_State_Changes()	{ return last_frame_texture_stage_state_changes; }
unsigned RenderDevice::Get_Last_Frame_API_Calls()					{ return last_frame_number_of_API_calls; }
unsigned RenderDevice::Get_Last_Frame_Draw_Calls()				{ return last_frame_draw_calls; }
unsigned long RenderDevice::Get_FrameCount(void) {return FrameCount;}

void Device_Assert()
{
	WWASSERT(RenderDevice::Get_Backend() != nullptr);
	RENDER_THREAD_ASSERT();
}

void RenderDevice::Begin_Scene(void)
{
	RENDER_THREAD_ASSERT();

	Backend->Begin_Scene();
}

// Back buffer screenshot for image comparison. The caller (W3DDisplay::draw) registers a file name; the
// backend reads the back buffer right before the next Present, i.e. exactly the displayed image.
void RenderDevice::Request_Back_Buffer_Capture(const char *bmp_path)
{
	if (Backend) Backend->Request_Back_Buffer_Capture(bmp_path);
}

// --- Graphics effects -----------------------------------------------------------------------------
// Implemented by the backend; the RenderBackend defaults do nothing.

void RenderDevice::Set_Effect_Settings(const RenderEffectSettings& settings)
{
	_EffectSettings = settings;
}

const RenderEffectSettings& RenderDevice::Get_Effect_Settings()
{
	return _EffectSettings;
}

void RenderDevice::Set_Soft_Particles(bool enable)
{
	// Kept in the render state so the sorter restores it per node; sent to the backend in
	// Apply_Render_State_Changes right before the draw.
	render_state.soft_particles = enable;
}

void RenderDevice::Scene_Opaque_Done()
{
	if (Backend) Backend->Scene_Opaque_Done();
}

void RenderDevice::Scene_3D_Begin(int x, int y, int width, int height)
{
	if (Backend) Backend->Scene_3D_Begin(x, y, width, height);
}

void RenderDevice::Scene_3D_Done(int x, int y, int width, int height)
{
	if (Backend) Backend->Scene_3D_Done(x, y, width, height);
}

void RenderDevice::Set_Dynamic_Lights(const RenderDynamicLight* lights, unsigned count)
{
	if (Backend) Backend->Set_Dynamic_Lights(lights, count);
}

void RenderDevice::Request_Scene_Copies(unsigned flags)
{
	if (Backend) Backend->Request_Scene_Copies(flags);
}

WW3DTextureHandle RenderDevice::Get_Scene_Depth_Copy()
{
	return Backend ? Backend->Get_Scene_Depth_Copy() : nullptr;
}

WW3DTextureHandle RenderDevice::Get_Scene_Color_Copy()
{
	return Backend ? Backend->Get_Scene_Color_Copy() : nullptr;
}

void RenderDevice::Get_Effect_Statistics(char* text, unsigned size)
{
	if (!text || !size) return;
	text[0] = 0;
	if (Backend) Backend->Get_Effect_Statistics(text, size);
}

void RenderDevice::Get_Device_Statistics(char* text, unsigned size)
{
	if (!text || !size) return;
	text[0] = 0;
	if (Backend) Backend->Get_Device_Statistics(text, size);
}

// Start of the opaque scene pass. The view is deferred (render_state.view reaches the backend with the
// next draw) while the projection goes to the backend immediately; both are passed here in the form
// the backend sees when drawing.
void RenderDevice::Shadow_Scene_Begin(RenderShadowParams& params)
{
	if (!Backend) return;
	if (!(_EffectSettings.flags & RENDER_EFFECT_SHADOW_MAP)) return;
	static_assert(sizeof(params.view) == sizeof(render_state.view), "Sichtmatrix: 4x4 float");
	::memcpy(&params.view, &render_state.view, sizeof(params.view));
	Backend->Get_Transform(WW3D_TS_PROJECTION, &params.projection);
	Backend->Shadow_Scene_Begin(params);
}

bool RenderDevice::Shadow_Maps_Active()
{
	return Backend ? Backend->Shadow_Maps_Active() : false;
}

void RenderDevice::Set_Shadow_Caster_Only(bool enable)
{
	_ShadowCasterOnly = enable && Backend && Backend->Shadow_Maps_Active();
	if (Backend) Backend->Set_Shadow_Caster_Only(_ShadowCasterOnly);
}

// Terrain is drawn, objects follow.
void RenderDevice::Scene_Terrain_Done()
{
	if (Backend && (_EffectSettings.flags & RENDER_EFFECT_SHADOW_MAP)) Backend->Scene_Terrain_Done();
}

void RenderDevice::Gpu_Pass_Begin(RenderGpuPass pass)
{
	if (Backend) Backend->Gpu_Pass_Mark(static_cast<unsigned>(pass), false);
}

void RenderDevice::Gpu_Pass_End(RenderGpuPass pass)
{
	if (Backend) Backend->Gpu_Pass_Mark(static_cast<unsigned>(pass), true);
}

void RenderDevice::Begin_Target_Pass(RenderGpuPass pass, TextureClass* target, ZTextureClass* depth)
{
	Gpu_Pass_Begin(pass);
	Set_Render_Target_With_Z(target, depth);
}

void RenderDevice::End_Target_Pass(RenderGpuPass pass)
{
	Set_Render_Target(static_cast<WW3DSurfaceHandle>(nullptr));
	Gpu_Pass_End(pass);
}

HRESULT RenderDevice::Begin_Surface_Pass(RenderGpuPass pass, WW3DSurfaceHandle target, WW3DSurfaceHandle depth)
{
	const HRESULT hr = Set_Render_Target_Raw(target, depth);
	if (hr == S_OK)
		Gpu_Pass_Begin(pass);
	return hr;
}

HRESULT RenderDevice::End_Surface_Pass(RenderGpuPass pass, WW3DSurfaceHandle target, WW3DSurfaceHandle depth)
{
	Gpu_Pass_End(pass);
	return Set_Render_Target_Raw(target, depth);
}

// The table describes the atlas of the loaded map and is sent regardless of the effect flag, so a
// detail level change works without reloading. The backend evaluates the draw flag when drawing.
void RenderDevice::Set_Terrain_Detail_Table(const RenderTerrainDetailTable& table)
{
	if (Backend) Backend->Set_Terrain_Detail_Table(table);
}

void RenderDevice::Set_Terrain_Detail(bool enable)
{
	if (Backend) Backend->Set_Terrain_Detail(enable);
}

// Needed by the rock layer and by the dynamic terrain lights. The backend compares the height field
// with its copy and uploads only on a change.
void RenderDevice::Set_Terrain_Height_Field(const unsigned char* heights, int width, int height, int border,
	float cell_size, float height_scale)
{
	if (Backend) Backend->Set_Terrain_Height_Field(heights, width, height, border, cell_size, height_scale);
}

void RenderDevice::Set_Terrain_Lighting(const RenderTerrainLighting& lighting)
{
	if (Backend) Backend->Set_Terrain_Lighting(lighting);
}

void RenderDevice::Set_Terrain_Relief_Atlas(WW3DTextureHandle atlas, bool active)
{
	if (Backend) Backend->Set_Terrain_Relief_Atlas(atlas, active);
}

void RenderDevice::Set_Model_Normal_Texture(WW3DTextureHandle owner, WW3DTextureHandle normal)
{
	if (Backend) Backend->Set_Model_Normal_Texture(owner, normal);
}

void RenderDevice::Set_Terrain_Tessellation(bool enable)
{
	if (Backend && (_EffectSettings.flags & RENDER_EFFECT_TERRAIN_TESS)) Backend->Set_Terrain_Tessellation(enable);
}

float RenderDevice::Terrain_Tess_Max_Displacement()
{
	return (Backend && (_EffectSettings.flags & RENDER_EFFECT_TERRAIN_TESS)) ? Backend->Terrain_Tess_Max_Displacement() : 0.0f;
}

void RenderDevice::Set_Terrain_Tess_Data(const RenderTerrainTessData& data, bool active)
{
	if (Backend) Backend->Set_Terrain_Tess_Data(data, active);
}

void RenderDevice::Set_Terrain_Tess_Mask(const unsigned char* mask, int width, int height)
{
	if (Backend) Backend->Set_Terrain_Tess_Mask(mask, width, height);
}

bool RenderDevice::Terrain_Dynamic_Lights_On_GPU()
{
	return Backend && Backend->Supports_Terrain_Dynamic_Lights();
}

void RenderDevice::Set_Terrain_Dynamic_Lights(const RenderTerrainDynamicLight* lights, unsigned count)
{
	if (Backend) Backend->Set_Terrain_Dynamic_Lights(lights, count);
}

void RenderDevice::Enable_Terrain_Dynamic_Lights(bool enable)
{
	if (Backend) Backend->Enable_Terrain_Dynamic_Lights(enable);
}

void RenderDevice::Set_Terrain_Static_Lighting(const RenderTerrainStaticLighting* lighting)
{
	if (Backend) Backend->Set_Terrain_Static_Lighting(lighting);
}

unsigned RenderDevice::Max_Skin_Bones()
{
	return Backend ? Backend->Max_Skin_Bones() : 0;
}

void RenderDevice::Set_Skin_Bones(const float* rows, unsigned count)
{
	if (Backend) Backend->Set_Skin_Bones(rows, count);
}

void RenderDevice::Begin_Mesh_Instances()
{
	if (Backend && _MeshInstancing) Backend->Begin_Mesh_Instances();
}

void RenderDevice::End_Mesh_Instances()
{
	if (Backend) Backend->End_Mesh_Instances();
}

bool RenderDevice::Supports_Particle_Instancing()
{
	return Backend && Backend->Supports_Particle_Instancing();
}

void RenderDevice::Set_Particle_Tables(const float* rotation, const float* ground)
{
	if (Backend) Backend->Set_Particle_Tables(rotation, ground);
}

void RenderDevice::Draw_Particles(const RenderParticleInstance* particles, unsigned count, const RenderParticleSetup& setup)
{
	if (!Backend || !particles || !count || !_Is_Triangle_Draw_Enabled()) return;
	Apply_Render_State_Changes();
	Backend->Draw_Particles(particles, count, setup);
	// The backend bound its own instance buffer: rebind the engine's stream on the next draw.
	render_state_changed |= VERTEX_BUFFER_CHANGED;
}

bool RenderDevice::Upload_Particle_Batch(const RenderParticleInstance* particles, unsigned count)
{
	return Backend && particles && count && Backend->Upload_Particle_Batch(particles, count);
}

void RenderDevice::Draw_Particle_Batch(unsigned first, unsigned count, const RenderParticleSetup& setup)
{
	if (!Backend || !count || !_Is_Triangle_Draw_Enabled()) return;
	Apply_Render_State_Changes();
	Backend->Draw_Particle_Batch(first, count, setup);
	render_state_changed |= VERTEX_BUFFER_CHANGED;
}

static unsigned s_effect_clock_ms = 0;
static bool s_effect_clock_set = false;

void RenderDevice::Set_Effect_Clock(unsigned ms)
{
	s_effect_clock_ms = ms;
	s_effect_clock_set = true;
}

unsigned RenderDevice::Get_Effect_Clock()
{
	return s_effect_clock_set ? s_effect_clock_ms : static_cast<unsigned>(::GetTickCount());
}

void RenderDevice::Set_Particle_Lighting(const float scale[3])
{
	if (Backend) Backend->Set_Particle_Lighting(scale);
}

void RenderDevice::End_Scene(bool flip_frames)
{
	RENDER_THREAD_ASSERT();
	Backend->End_Scene();

	if (flip_frames) {
		Device_Assert();
		HRESULT hr;
		{
			WWPROFILE("RenderDevice::Present()");
			hr=Backend->Present();	// includes a requested back buffer capture
		}

		if (SUCCEEDED(hr)) {
#ifdef EXTENDED_STATS
			if (stats.m_sleepTime) {
				::Sleep(stats.m_sleepTime);
			}
#endif
			IsDeviceLost=false;
			FrameCount++;
		}
		else {
			IsDeviceLost=true;
		}

		// If the device was lost we need to check for cooperative level and possibly reset the device
		if (hr==static_cast<HRESULT>(WW3D_ERR_DEVICELOST)) {
			hr=Backend->Get_Device_State();
			if (hr==static_cast<HRESULT>(WW3D_ERR_DEVICENOTRESET)) {
				Reset_Device();
			}
			else {
				// Sleep it not active
				ThreadClass::Sleep_Ms(200);
			}
		}
		else {
			Device_ErrorCode(hr);
		}
	}

	// Each frame, release all of the buffers and textures.
	Set_Vertex_Buffer(nullptr);
	Set_Index_Buffer(nullptr,0);
	for (int i=0;i<RenderCaps::Get_Max_Textures_Per_Pass();++i) Set_Texture(i,nullptr);
	Set_Material(nullptr);
}


//**********************************************************************************************
//! Clear current render device
/*! KM
/* 5/17/02 KM Fixed support for render to texture with depth/stencil buffers
*/
void RenderDevice::Clear(bool clear_color, bool clear_z_stencil, const Vector3 &color, float dest_alpha, float z, unsigned int stencil)
{
	RENDER_THREAD_ASSERT();
	// The backend checks whether the current depth buffer has stencil (render to texture).
	Backend->Clear(clear_color, clear_z_stencil, Convert_Color(color,dest_alpha), z, stencil);
}

void RenderDevice::Set_Viewport(CONST WW3DViewport* pViewport)
{
	RENDER_THREAD_ASSERT();
	Backend->Set_Viewport(pViewport);
}

// ----------------------------------------------------------------------------
//
// Set vertex buffer. A reference to previous vertex buffer is released and
// this one is assigned the current vertex buffer. The device vertex buffer will
// actually be set in Apply() which is called by Draw_Indexed_Triangles().
//
// ----------------------------------------------------------------------------

void RenderDevice::Set_Vertex_Buffer(const VertexBufferClass* vb, unsigned stream)
{
	render_state.vba_offset=0;
	render_state.vba_count=0;
	if (render_state.vertex_buffers[stream]) {
		render_state.vertex_buffers[stream]->Release_Engine_Ref();
	}
	REF_PTR_SET(render_state.vertex_buffers[stream],const_cast<VertexBufferClass*>(vb));
	if (vb) {
		vb->Add_Engine_Ref();
		render_state.vertex_buffer_types[stream]=vb->Type();
	}
	else {
		render_state.vertex_buffer_types[stream]=BUFFER_TYPE_INVALID;
	}
	render_state_changed|=VERTEX_BUFFER_CHANGED;
}

// ----------------------------------------------------------------------------
//
// Set index buffer. A reference to previous index buffer is released and
// this one is assigned the current index buffer. The device index buffer will
// actually be set in Apply() which is called by Draw_Indexed_Triangles().
//
// ----------------------------------------------------------------------------

void RenderDevice::Set_Index_Buffer(const IndexBufferClass* ib,unsigned short index_base_offset)
{
	render_state.iba_offset=0;
	if (render_state.index_buffer) {
		render_state.index_buffer->Release_Engine_Ref();
	}
	REF_PTR_SET(render_state.index_buffer,const_cast<IndexBufferClass*>(ib));
	render_state.index_base_offset=index_base_offset;
	if (ib) {
		ib->Add_Engine_Ref();
		render_state.index_buffer_type=ib->Type();
	}
	else {
		render_state.index_buffer_type=BUFFER_TYPE_INVALID;
	}
	render_state_changed|=INDEX_BUFFER_CHANGED;
}

// ----------------------------------------------------------------------------
//
// Set vertex buffer using dynamic access object.
//
// ----------------------------------------------------------------------------

void RenderDevice::Set_Vertex_Buffer(const DynamicVBAccessClass& vba_)
{
	// Release all streams (only one stream allowed in the legacy pipeline)
	for (int i=1;i<MAX_VERTEX_STREAMS;++i) {
		RenderDevice::Set_Vertex_Buffer(nullptr, i);
	}

	if (render_state.vertex_buffers[0]) render_state.vertex_buffers[0]->Release_Engine_Ref();
	DynamicVBAccessClass& vba=const_cast<DynamicVBAccessClass&>(vba_);
	render_state.vertex_buffer_types[0]=vba.Get_Type();
	render_state.vba_offset=vba.VertexBufferOffset;
	render_state.vba_count=vba.Get_Vertex_Count();
	REF_PTR_SET(render_state.vertex_buffers[0],vba.VertexBuffer);
	render_state.vertex_buffers[0]->Add_Engine_Ref();
	render_state_changed|=VERTEX_BUFFER_CHANGED;
	render_state_changed|=INDEX_BUFFER_CHANGED;		// vba_offset changes so index buffer needs to be reset as well.
}

// ----------------------------------------------------------------------------
//
// Set index buffer using dynamic access object.
//
// ----------------------------------------------------------------------------

void RenderDevice::Set_Index_Buffer(const DynamicIBAccessClass& iba_,unsigned short index_base_offset)
{
	if (render_state.index_buffer) render_state.index_buffer->Release_Engine_Ref();

	DynamicIBAccessClass& iba=const_cast<DynamicIBAccessClass&>(iba_);
	render_state.index_base_offset=index_base_offset;
	render_state.index_buffer_type=iba.Get_Type();
	render_state.iba_offset=iba.IndexBufferOffset;
	REF_PTR_SET(render_state.index_buffer,iba.IndexBuffer);
	render_state.index_buffer->Add_Engine_Ref();
	render_state_changed|=INDEX_BUFFER_CHANGED;
}

// ----------------------------------------------------------------------------
//
// Private function for the special case of rendering polygons from sorting
// index and vertex buffers.
//
// ----------------------------------------------------------------------------

void RenderDevice::Draw_Sorting_IB_VB(
	unsigned primitive_type,
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	WWASSERT(render_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || render_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING);
	WWASSERT(render_state.index_buffer_type==BUFFER_TYPE_SORTING || render_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING);

	// Fill dynamic vertex buffer with sorting vertex buffer vertices
	DynamicVBAccessClass dyn_vb_access(BUFFER_TYPE_DYNAMIC_DEVICE,dynamic_fvf_type,vertex_count);
	{
		DynamicVBAccessClass::WriteLockClass lock(&dyn_vb_access);
		VertexFormatXYZNDUV2* src = static_cast<SortingVertexBufferClass*>(render_state.vertex_buffers[0])->VertexBuffer;
		VertexFormatXYZNDUV2* dest= lock.Get_Formatted_Vertex_Array();
		src += render_state.vba_offset + render_state.index_base_offset + min_vertex_index;
		unsigned  size = dyn_vb_access.FVF_Info().Get_FVF_Size()*vertex_count/sizeof(unsigned);
		unsigned *dest_u =reinterpret_cast<unsigned*>(dest);
		unsigned *src_u = reinterpret_cast<unsigned*>(src);

		for (unsigned i=0;i<size;++i) {
			*dest_u++=*src_u++;
		}
	}

	// FVF buffers use a vertex declaration derived from the FVF code instead of a shader.
	Backend->Set_Stream_Source(
		0,
		static_cast<DeviceVertexBufferClass*>(dyn_vb_access.VertexBuffer)->Get_Device_Vertex_Buffer(),
		0,
		dyn_vb_access.FVF_Info().Get_FVF_Size());
	// If using FVF format VB, set the FVF as vertex shader (may not be needed here KM)
	unsigned fvf=dyn_vb_access.FVF_Info().Get_FVF();
	if (fvf!=0) {
		Backend->Set_Vertex_Shader(nullptr);
		Set_Vertex_Declaration(Get_Vertex_Declaration_For_FVF(fvf));
	}
	WW3D_RECORD_VERTEX_BUFFER_CHANGE();

	unsigned index_count=0;
	switch (primitive_type) {
	case WW3D_PT_TRIANGLELIST: index_count=polygon_count*3; break;
	case WW3D_PT_TRIANGLESTRIP: index_count=polygon_count+2; break;
	case WW3D_PT_TRIANGLEFAN: index_count=polygon_count+2; break;
	default: WWASSERT(0); break; // Unsupported primitive type
	}

	// Fill dynamic index buffer with sorting index buffer vertices
	DynamicIBAccessClass dyn_ib_access(BUFFER_TYPE_DYNAMIC_DEVICE,static_cast<unsigned short>(index_count));
	{
		DynamicIBAccessClass::WriteLockClass lock(&dyn_ib_access);
		unsigned short* dest=lock.Get_Index_Array();
		unsigned short* src=nullptr;
		src=static_cast<SortingIndexBufferClass*>(render_state.index_buffer)->index_buffer;
		src+=render_state.iba_offset+start_index;

		try {
		for (unsigned short i=0;i<index_count;++i) {
			unsigned short index=*src++;
			index-=min_vertex_index;
			WWASSERT(index<vertex_count);
			*dest++=index;
		}
		IndexBufferExceptionFunc();
		} catch(...) {
			IndexBufferExceptionFunc();
		}
	}

	// The base vertex index is passed to Draw_Indexed_Primitive, not to Set_Indices.
	Backend->Set_Indices(
		static_cast<DeviceIndexBufferClass*>(dyn_ib_access.IndexBuffer)->Get_Device_Index_Buffer());
	WW3D_RECORD_INDEX_BUFFER_CHANGE();

	WW3D_RECORD_DRAW_CALLS();
	Backend->Draw_Indexed_Primitive(
		static_cast<WW3DPrimitiveType>(WW3D_PT_TRIANGLELIST),
		dyn_vb_access.VertexBufferOffset,	// BaseVertexIndex
		0,		// start vertex
		vertex_count,
		dyn_ib_access.IndexBufferOffset,
		polygon_count);

	WW3D_RECORD_RENDER(polygon_count,vertex_count,render_state.shader);
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void RenderDevice::Draw(
	unsigned primitive_type,
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	if (DrawPolygonLowBoundLimit && DrawPolygonLowBoundLimit>=polygon_count) return;

	RENDER_THREAD_ASSERT();
	SNAPSHOT_SAY(("D3D - draw\n"));

	Apply_Render_State_Changes();

	// Debug feature to disable triangle drawing...
	if (!_Is_Triangle_Draw_Enabled()) return;

#ifdef MESH_RENDER_SNAPSHOT_ENABLED
	if (WW3D::Is_Snapshot_Activated()) {
		SNAPSHOT_SAY(("ValidateDevice: "));
		HRESULT res=Backend->Validate_Device() ? static_cast<HRESULT>(WW3D_OK) : static_cast<HRESULT>(E_FAIL);
		switch (res) {
		case WW3D_OK:
			SNAPSHOT_SAY(("OK\n"));
			break;

		// The backend reports only valid/invalid.
		case WW3D_ERR_DEVICELOST:
			SNAPSHOT_SAY(("WW3D_ERR_DEVICELOST\n"));
			break;
		default:
			SNAPSHOT_SAY(("UNKNOWN Error\n"));
			break;
		}
	}
#endif	// MESH_RENDER_SHAPSHOT_ENABLED


	SNAPSHOT_SAY(("D3D - draw %d polygons (%d vertices)\n",polygon_count,vertex_count));

	if (vertex_count<3) {
		min_vertex_index=0;
		switch (render_state.vertex_buffer_types[0]) {
		case BUFFER_TYPE_DEVICE:
		case BUFFER_TYPE_SORTING:
			vertex_count=render_state.vertex_buffers[0]->Get_Vertex_Count()-render_state.index_base_offset-render_state.vba_offset-min_vertex_index;
			break;
		case BUFFER_TYPE_DYNAMIC_DEVICE:
		case BUFFER_TYPE_DYNAMIC_SORTING:
			vertex_count=render_state.vba_count;
			break;
		}
	}

	switch (render_state.vertex_buffer_types[0]) {
	case BUFFER_TYPE_DEVICE:
	case BUFFER_TYPE_DYNAMIC_DEVICE:
		switch (render_state.index_buffer_type) {
		case BUFFER_TYPE_DEVICE:
		case BUFFER_TYPE_DYNAMIC_DEVICE:
			{
/*				if ((start_index+render_state.iba_offset+polygon_count*3) > render_state.index_buffer->Get_Index_Count())
				{	WWASSERT_PRINT(0,"OVERFLOWING INDEX BUFFER");
					///@todo: MUST FIND OUT WHY THIS HAPPENS WITH LOTS OF PARTICLES ON BIG FIGHT!  -MW
					break;
				}*/
				if (!_ShadowCasterOnly) {
					WW3D_RECORD_RENDER(polygon_count,vertex_count,render_state.shader);
					WW3D_RECORD_DRAW_CALLS();
				}
				// The base vertex index comes from CachedIndexBaseOffset (set with
				// INDEX_BUFFER_CHANGED in Apply_Render_State_Changes).
				Backend->Draw_Indexed_Primitive(
					static_cast<WW3DPrimitiveType>(primitive_type),
					CachedIndexBaseOffset,
					min_vertex_index,
					vertex_count,
					start_index+render_state.iba_offset,
					polygon_count);
			}
			break;
		case BUFFER_TYPE_SORTING:
		case BUFFER_TYPE_DYNAMIC_SORTING:
			WWASSERT_PRINT(0,"VB and IB must of same type (sorting or device)");
			break;
		case BUFFER_TYPE_INVALID:
			WWASSERT(0);
			break;
		}
		break;
	case BUFFER_TYPE_SORTING:
	case BUFFER_TYPE_DYNAMIC_SORTING:
		switch (render_state.index_buffer_type) {
		case BUFFER_TYPE_DEVICE:
		case BUFFER_TYPE_DYNAMIC_DEVICE:
			WWASSERT_PRINT(0,"VB and IB must of same type (sorting or device)");
			break;
		case BUFFER_TYPE_SORTING:
		case BUFFER_TYPE_DYNAMIC_SORTING:
			Draw_Sorting_IB_VB(primitive_type,start_index,polygon_count,min_vertex_index,vertex_count);
			break;
		case BUFFER_TYPE_INVALID:
			WWASSERT(0);
			break;
		}
		break;
	case BUFFER_TYPE_INVALID:
		WWASSERT(0);
		break;
	}
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void RenderDevice::Draw_Triangles(
	unsigned buffer_type,
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	if (buffer_type==BUFFER_TYPE_SORTING || buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) {
		SortingRendererClass::Insert_Triangles(start_index,polygon_count,min_vertex_index,vertex_count);
	}
	else {
		Draw(WW3D_PT_TRIANGLELIST,start_index,polygon_count,min_vertex_index,vertex_count);
	}
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void RenderDevice::Draw_Triangles(
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	Draw(WW3D_PT_TRIANGLELIST,start_index,polygon_count,min_vertex_index,vertex_count);
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void RenderDevice::Draw_Strip(
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	Draw(WW3D_PT_TRIANGLESTRIP,start_index,polygon_count,min_vertex_index,vertex_count);
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void RenderDevice::Apply_Render_State_Changes()
{
	SNAPSHOT_SAY(("RenderDevice::Apply_Render_State_Changes()\n"));

	// Report soft particles to the backend only on a change. Placed before the early return below
	// because the flag is not tracked in render_state_changed.
	if (render_state.soft_particles != _SoftParticlesSent) {
		_SoftParticlesSent = render_state.soft_particles;
		if (Backend) Backend->Set_Soft_Particles(_SoftParticlesSent);
	}
	
	if (!render_state_changed) return;
	if (render_state_changed&SHADER_CHANGED) {
		SNAPSHOT_SAY(("D3D - apply shader\n"));
		render_state.shader.Apply();
	}

	unsigned mask=TEXTURE0_CHANGED;
	int i;
	for (i=0;i<RenderCaps::Get_Max_Textures_Per_Pass();++i,mask<<=1)
	{
		if (render_state_changed&mask) 
		{
			SNAPSHOT_SAY(("D3D - apply texture %d (%s)\n",i,render_state.Textures[i] ? (const char *)render_state.Textures[i]->Get_Full_Path() : "NULL"));

			if (render_state.Textures[i]) 
			{
				render_state.Textures[i]->Apply(i);
			}
			else 
			{
				TextureBaseClass::Apply_Null(i);
			}
		}
	}

	if (render_state_changed&MATERIAL_CHANGED) 
	{
		SNAPSHOT_SAY(("D3D - apply material\n"));
		VertexMaterialClass* material=const_cast<VertexMaterialClass*>(render_state.material);
		if (material) 
		{
			material->Apply();
		}
		else VertexMaterialClass::Apply_Null();
	}

	if (render_state_changed&LIGHTS_CHANGED)
	{
		unsigned light_mask=LIGHT0_CHANGED;
		for (unsigned index=0;index<4;++index,light_mask<<=1) {
			if (render_state_changed&light_mask) {
				SNAPSHOT_SAY(("D3D - apply light %d\n",index));
				if (render_state.LightEnable[index]) {
#ifdef MESH_RENDER_SNAPSHOT_ENABLED		
					if ( WW3D::Is_Snapshot_Activated() ) {
						[[maybe_unused]] WW3DLight * light = &(render_state.Lights[index]);
						static const char * _light_types[] = { "Unknown", "Point","Spot", "Directional" };
						WWASSERT((light->Type >= 0) && (light->Type <= 3));					

						SNAPSHOT_SAY((" type = %s amb = %4.2f,%4.2f,%4.2f  diff = %4.2f,%4.2f,%4.2f spec = %4.2f, %4.2f, %4.2f\n",
							_light_types[light->Type],
							light->Ambient.r,light->Ambient.g,light->Ambient.b,
							light->Diffuse.r,light->Diffuse.g,light->Diffuse.b,
							light->Specular.r,light->Specular.g,light->Specular.b ));
						SNAPSHOT_SAY((" pos = %f, %f, %f  dir = %f, %f, %f\n",
							light->Position.x, light->Position.y, light->Position.z,
							light->Direction.x, light->Direction.y, light->Direction.z ));
					}
#endif

					Set_Device_Light(index,&render_state.Lights[index]);
				}
				else {
					Set_Device_Light(index,nullptr);
					SNAPSHOT_SAY((" clearing light to NULL\n"));
				}
			}
		}
	}

	if (render_state_changed&WORLD_CHANGED) {
		SNAPSHOT_SAY(("D3D - apply world matrix\n"));
		_Set_Device_Transform(WW3D_TS_WORLD,render_state.world);
	}
	if (render_state_changed&VIEW_CHANGED) {
		SNAPSHOT_SAY(("D3D - apply view matrix\n"));
		_Set_Device_Transform(WW3D_TS_VIEW,render_state.view);
	}
	if (render_state_changed&VERTEX_BUFFER_CHANGED) {
		SNAPSHOT_SAY(("D3D - apply vb change\n"));
		for (i=0;i<MAX_VERTEX_STREAMS;++i) {
			if (render_state.vertex_buffers[i]) {
				switch (render_state.vertex_buffer_types[i]) {//->Type()) {
				case BUFFER_TYPE_DEVICE:
				case BUFFER_TYPE_DYNAMIC_DEVICE:
					Backend->Set_Stream_Source(
						i,
						static_cast<DeviceVertexBufferClass*>(render_state.vertex_buffers[i])->Get_Device_Vertex_Buffer(),
						0,
						render_state.vertex_buffers[i]->FVF_Info().Get_FVF_Size());
					WW3D_RECORD_VERTEX_BUFFER_CHANGE();
					{
						// If the VB format is FVF, set the FVF as a vertex shader
						unsigned fvf=render_state.vertex_buffers[i]->FVF_Info().Get_FVF();
						if (fvf!=0) {
							Set_Vertex_Shader(fvf);
						}
					}
					break;
				case BUFFER_TYPE_SORTING:
				case BUFFER_TYPE_DYNAMIC_SORTING:
					break;
				default:
					WWASSERT(0);
				}
			} else {
				Backend->Set_Stream_Source(i,nullptr,0,0);
				WW3D_RECORD_VERTEX_BUFFER_CHANGE();
			}
		}
	}
	if (render_state_changed&INDEX_BUFFER_CHANGED) {
		SNAPSHOT_SAY(("D3D - apply ib change\n"));
		if (render_state.index_buffer) {
			switch (render_state.index_buffer_type) {//->Type()) {
			case BUFFER_TYPE_DEVICE:
			case BUFFER_TYPE_DYNAMIC_DEVICE:
				// Set_Indices takes no base vertex index; it is cached and passed to the next
				// Draw_Indexed_Primitive.
				CachedIndexBaseOffset = render_state.index_base_offset+render_state.vba_offset;
				Backend->Set_Indices(
					static_cast<DeviceIndexBufferClass*>(render_state.index_buffer)->Get_Device_Index_Buffer());
				WW3D_RECORD_INDEX_BUFFER_CHANGE();
				break;
			case BUFFER_TYPE_SORTING:
			case BUFFER_TYPE_DYNAMIC_SORTING:
				break;
			default:
				WWASSERT(0);
			}
		}
		else {
			CachedIndexBaseOffset = 0;
			Backend->Set_Indices(nullptr);
			WW3D_RECORD_INDEX_BUFFER_CHANGE();
		}
	}

	render_state_changed&=(static_cast<unsigned>(WORLD_IDENTITY)|static_cast<unsigned>(VIEW_IDENTITY));

	SNAPSHOT_SAY(("RenderDevice::Apply_Render_State_Changes() - finished\n"));
}

WW3DTextureHandle RenderDevice::_Create_Device_Texture
(
	unsigned int width,
	unsigned int height,
	WW3DFormat format,
	MipCountType mip_level_count,
	unsigned pool,
	bool rendertarget
)
{
	RENDER_THREAD_ASSERT();
	Device_Assert();
	WW3DTextureHandle texture = nullptr;

	// Paletted textures not supported!
	WWASSERT(format!=WW3D_FORMAT_P8);	// compare with the WW3DFormat value, not the device format

	// NOTE: If 'format' is not supported as a texture format, this function will find the closest
	// format that is supported and use that instead.

	// Render target may return NOTAVAILABLE, in
	// which case we return NULL.
	if (rendertarget) {
		unsigned ret=Backend->Create_Texture(
			width,
			height,
			mip_level_count,
			WW3D_USAGE_RENDERTARGET,
			WW3DFormat_To_D3DFormat(format),
			pool,
			&texture);

		if (ret==WW3D_ERR_NOTAVAILABLE) {
			Non_Fatal_Log_Device_ErrorCode(ret,__FILE__,__LINE__);
			return nullptr;
		}

		// If ran out of texture ram, try invalidating some textures and mesh cache.
		if (ret==WW3D_ERR_OUTOFVIDEOMEMORY) {
			WWDEBUG_SAY(("Error: Out of memory while creating render target. Trying to release assets...\n"));
			// Invalidate the mesh cache
			WW3D::_Invalidate_Mesh_Cache();

			ret=Backend->Create_Texture(
				width,
				height,
				mip_level_count,
				WW3D_USAGE_RENDERTARGET,
				WW3DFormat_To_D3DFormat(format),
				pool,
				&texture);

			if (SUCCEEDED(ret)) {
				WWDEBUG_SAY(("...Render target creation succesful.\n"));
			}
			else {
				WWDEBUG_SAY(("...Render target creation failed.\n"));
			}
			if (ret==WW3D_ERR_OUTOFVIDEOMEMORY) {
				Non_Fatal_Log_Device_ErrorCode(ret,__FILE__,__LINE__);
				return nullptr;
			}
		}

		Device_ErrorCode(ret);
		// Just return the texture, no reduction
		// allowed for render targets.
		return texture;
	}

	// We should never run out of video memory when allocating a non-rendertarget texture.
	// However, it seems to happen sometimes when there are a lot of textures in memory and so
	// if it happens we'll release assets and try again (anything is better than crashing).
	unsigned ret=Backend->Create_Texture(
		width,
		height,
		mip_level_count,
		0,
		WW3DFormat_To_D3DFormat(format),
		pool,
		&texture);

	// If ran out of texture ram, try invalidating some textures and mesh cache.
	if (ret==WW3D_ERR_OUTOFVIDEOMEMORY) {
		WWDEBUG_SAY(("Error: Out of memory while creating texture. Trying to release assets...\n"));
		// Invalidate the mesh cache
		WW3D::_Invalidate_Mesh_Cache();

		ret=Backend->Create_Texture(
			width,
			height,
			mip_level_count,
			0,
			WW3DFormat_To_D3DFormat(format),
			pool,
			&texture);
		if (SUCCEEDED(ret)) {
			WWDEBUG_SAY(("...Texture creation succesful.\n"));
		}
		else {
			StringClass format_name(0,true);
			Get_WW3D_Format_Name(format, format_name);
			WWDEBUG_SAY(("...Texture creation failed. (%d x %d, format: %s, mips: %d\n",width,height,format_name.Peek_Buffer(),mip_level_count));
		}

	}
	Device_ErrorCode(ret);

	return texture;
}

WW3DTextureHandle RenderDevice::_Create_Device_Texture
(
	WW3DSurfaceHandle surface,
	MipCountType mip_level_count
)
{
	RENDER_THREAD_ASSERT();
	Device_Assert();
	WW3DTextureHandle texture = nullptr;

	WW3DSurfaceDesc surface_desc;
	::ZeroMemory(&surface_desc, sizeof(WW3DSurfaceDesc));
	Backend->Get_Surface_Desc(surface, &surface_desc);

	// This function will create a texture with a different (but similar) format if the surface is
	// not in a supported texture format.
	WW3DFormat format=D3DFormat_To_WW3DFormat(surface_desc.Format);
	texture = _Create_Device_Texture(surface_desc.Width, surface_desc.Height, format, mip_level_count);

	// Copy the surface to the texture (TexUtil, DirectXTex)
	WW3DSurfaceHandle tex_surface = nullptr;
	Backend->Get_Texture_Surface_Level(texture, 0, &tex_surface);
	Device_ErrorCode(TexUtil::Copy_Surface(tex_surface, nullptr, surface, nullptr, TexUtil::FILTER_BOX));
	Release_Resource(tex_surface);

	// Create mipmaps if needed
	if (mip_level_count!=MIP_LEVELS_1)
	{
		Device_ErrorCode(TexUtil::Generate_Mip_Levels(texture, TexUtil::FILTER_BOX));
	}

	return texture;

}

/*!
 * KJM create depth stencil texture
 */
WW3DTextureHandle RenderDevice::_Create_Device_ZTexture
(
	unsigned int width,
	unsigned int height,
	WW3DZFormat zformat,
	MipCountType mip_level_count,
	unsigned pool
)
{
	RENDER_THREAD_ASSERT();
	Device_Assert();
	WW3DTextureHandle texture = nullptr;

	WW3DDeviceFormat zfmt=WW3DZFormat_To_D3DFormat(zformat);

	unsigned ret=Backend->Create_Texture
	(
		width,
		height,
		mip_level_count,
		WW3D_USAGE_DEPTHSTENCIL,
		zfmt,
		pool,
		&texture
	);

	if (ret==WW3D_ERR_NOTAVAILABLE) 
	{
		Non_Fatal_Log_Device_ErrorCode(ret,__FILE__,__LINE__);
		return nullptr;
	}

	// If ran out of texture ram, try invalidating some textures and mesh cache.
	if (ret==WW3D_ERR_OUTOFVIDEOMEMORY) 
	{
		WWDEBUG_SAY(("Error: Out of memory while creating render target. Trying to release assets...\n"));
		// Invalidate the mesh cache
		WW3D::_Invalidate_Mesh_Cache();

		ret=Backend->Create_Texture
		(
			width,
			height,
			mip_level_count,
			WW3D_USAGE_DEPTHSTENCIL,
			zfmt,
			pool,
			&texture
		);

		if (SUCCEEDED(ret)) 
		{
			WWDEBUG_SAY(("...Render target creation succesful.\n"));
		}
		else 
		{
			WWDEBUG_SAY(("...Render target creation failed.\n"));
		}
		if (ret==WW3D_ERR_OUTOFVIDEOMEMORY) 
		{
			Non_Fatal_Log_Device_ErrorCode(ret,__FILE__,__LINE__);
			return nullptr;
		}
	}

	Device_ErrorCode(ret);

	Add_Ref_Resource(texture); // don't release this texture

	// Just return the texture, no reduction
	// allowed for render targets.

	return texture;
}

WW3DSurfaceHandle RenderDevice::_Create_Device_Surface(unsigned int width, unsigned int height, WW3DFormat format)
{
	RENDER_THREAD_ASSERT();
	Device_Assert();

	WW3DSurfaceHandle surface = nullptr;

	// Paletted surfaces not supported!
	WWASSERT(format!=WW3D_FORMAT_P8);	// compare with the WW3DFormat value, not the device format

	// Offscreen surface in system memory.
	surface = Backend->Create_Offscreen_Surface(width, height, WW3DFormat_To_D3DFormat(format));

	return surface;
}

WW3DSurfaceHandle RenderDevice::_Create_Device_Surface(const char *filename_)
{
	RENDER_THREAD_ASSERT();
	Device_Assert();

	// Loads the surface through TextureLoader. A missing .tga falls back to .dds; if neither
	// exists, the missing-texture surface is returned.
	WW3DSurfaceHandle surface = nullptr;

	{

		file_auto_ptr myfile(_TheFileFactory,filename_);
		// If file not found, create a surface with missing texture in it

		if (!myfile->Is_Available()) {
			// If file not found, try the dds format
			// else create a surface with missing texture in it
			char compressed_name[200];
			strncpy(compressed_name,filename_, 200);
			char *ext = strstr(compressed_name, ".");
			if ( (strlen(ext)==4) && 
				  ( (ext[1] == 't') || (ext[1] == 'T') ) && 
				  ( (ext[2] == 'g') || (ext[2] == 'G') ) && 
				  ( (ext[3] == 'a') || (ext[3] == 'A') ) ) {
				ext[1]='d';
				ext[2]='d';
				ext[3]='s';
			}
			file_auto_ptr myfile2(_TheFileFactory,compressed_name);
			if (!myfile2->Is_Available())
				return MissingTexture::_Create_Missing_Surface();
		}
	}

	StringClass filename_string(filename_,true);
	surface=TextureLoader::Load_Surface_Immediate(
		filename_string,
		WW3D_FORMAT_UNKNOWN,
		true);
	return surface;
}


/***********************************************************************************************
 * RenderDevice::_Update_Texture -- Copies a texture from system memory to video memory          *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   4/26/2001  hy : Created.                                                                  *
 *=============================================================================================*/
void RenderDevice::_Update_Texture(TextureClass *system, TextureClass *video)
{
	WWASSERT(system);
	WWASSERT(video);
	WWASSERT(system->Get_Pool()==TextureClass::POOL_SYSTEMMEM);
	WWASSERT(video->Get_Pool()==TextureClass::POOL_DEFAULT);
	Backend->Update_Texture(static_cast<WW3DTextureHandle>(system->Peek_D3D_Base_Texture()),static_cast<WW3DTextureHandle>(video->Peek_D3D_Base_Texture()));
}

void RenderDevice::Compute_Caps()
{
	RENDER_THREAD_ASSERT();
	Device_Assert();
	// The backend sets fixed values (feature level 11.0).
	Backend->Compute_Caps();
}


void RenderDevice::Set_Light(unsigned index, const WW3DLight* light)
{
	if (light) {
		render_state.Lights[index]=*light;
		render_state.LightEnable[index]=true;
	}
	else {
		render_state.LightEnable[index]=false;
	}
	render_state_changed|=(LIGHT0_CHANGED<<index);
}

void RenderDevice::Set_Light(unsigned index,const LightClass &light)
{
	WW3DLight dlight;
	Vector3 temp;
	memset(&dlight,0,sizeof(WW3DLight));

	switch (light.Get_Type())
	{
	case LightClass::POINT:
		{
			dlight.Type=WW3D_LIGHT_POINT;
		}
		break;
	case LightClass::DIRECTIONAL:
		{
			dlight.Type=WW3D_LIGHT_DIRECTIONAL;
		}
		break;
	case LightClass::SPOT:
		{
			dlight.Type=WW3D_LIGHT_SPOT;
		}
		break;
	}

	light.Get_Diffuse(&temp);
	temp*=light.Get_Intensity();
	dlight.Diffuse.r=temp.X;
	dlight.Diffuse.g=temp.Y;
	dlight.Diffuse.b=temp.Z;
	dlight.Diffuse.a=1.0f;

	light.Get_Specular(&temp);
	temp*=light.Get_Intensity();
	dlight.Specular.r=temp.X;
	dlight.Specular.g=temp.Y;
	dlight.Specular.b=temp.Z;
	dlight.Specular.a=1.0f;

	light.Get_Ambient(&temp);
	temp*=light.Get_Intensity();
	dlight.Ambient.r=temp.X;
	dlight.Ambient.g=temp.Y;
	dlight.Ambient.b=temp.Z;
	dlight.Ambient.a=1.0f;

	temp=light.Get_Position();
	dlight.Position=*reinterpret_cast<WW3DVector*>(&temp);

	light.Get_Spot_Direction(temp);
	dlight.Direction=*reinterpret_cast<WW3DVector*>(&temp);

	dlight.Range=light.Get_Attenuation_Range();
	dlight.Falloff=light.Get_Spot_Exponent();
	dlight.Theta=light.Get_Spot_Angle();
	dlight.Phi=light.Get_Spot_Angle();

	// Inverse linear light 1/(1+D)
	double a,b;
	light.Get_Far_Attenuation_Range(a,b);
	dlight.Attenuation0=1.0f;
	if (fabs(a-b)<1e-5)
		// if the attenuation range is too small assume uniform with cutoff
		dlight.Attenuation1=0.0f;
	else
		// this will cause the light to drop to half intensity at the first far attenuation
		dlight.Attenuation1=static_cast<float>(static_cast<float>(1.0)/a);
	dlight.Attenuation2=0.0f;

	Set_Light(index,&dlight);
}

//**********************************************************************************************
//! Set the light environment. This is a lighting model which used up to four
//! directional lights to produce the lighting.
/*! 5/27/02 KJM Added shader light environment support
*/
void RenderDevice::Set_Light_Environment(LightEnvironmentClass* light_env)
{
	// Shader light environment support															*
//	if (Light_Environment && light_env && (*Light_Environment)==(*light_env)) return;

	Light_Environment=light_env;

	if (light_env) 
	{
		int light_count = light_env->Get_Light_Count();
		unsigned int color=Convert_Color(light_env->Get_Equivalent_Ambient(),0.0f);
		if (RenderStates[WW3D_RS_AMBIENT]!=color)
		{
			Set_Render_State(WW3D_RS_AMBIENT,color);
//buggy Radeon 9700 driver doesn't apply new ambient unless the material also changes.
#if 1
			render_state_changed|=MATERIAL_CHANGED;
#endif
		}

		WW3DLight light;
		int l;
		for (l=0;l<light_count;++l) {
			
			::ZeroMemory(&light, sizeof(WW3DLight));
			
			light.Type=WW3D_LIGHT_DIRECTIONAL;
			reinterpret_cast<Vector3&>(light.Diffuse)=light_env->Get_Light_Diffuse(l);
			Vector3 dir=-light_env->Get_Light_Direction(l);
			light.Direction=reinterpret_cast<const WW3DVector&>(dir);

			// (gth) TODO: put specular into LightEnvironment?  Much work to be done on lights :-)'
			if (l==0) {
				light.Specular.r = light.Specular.g = light.Specular.b = 1.0f;
			}

			if (light_env->isPointLight(l)) {
				light.Type = WW3D_LIGHT_POINT;
				reinterpret_cast<Vector3&>(light.Diffuse)=light_env->getPointDiffuse(l);
				reinterpret_cast<Vector3&>(light.Ambient)=light_env->getPointAmbient(l);
				light.Position = reinterpret_cast<const WW3DVector&>(light_env->getPointCenter(l));
				light.Range = light_env->getPointOrad(l);
				
				// Inverse linear light 1/(1+D)
				double a,b;
				b = light_env->getPointOrad(l);
				a = light_env->getPointIrad(l);

//(gth) CNC3 Generals code for the attenuation factors is causing the lights to over-brighten
//I'm changing the Attenuation0 parameter to 1.0 to avoid this problem.				
				light.Attenuation0=1.0f;
				if (fabs(a-b)<1e-5)
					// if the attenuation range is too small assume uniform with cutoff
					light.Attenuation1=0.0f;
				else
					// this will cause the light to drop to half intensity at the first far attenuation
					light.Attenuation1=static_cast<float>(static_cast<float>(0.1)/a);
	
				light.Attenuation2=static_cast<float>(8.0f/(b*b));
			}

			Set_Light(l,&light);
		}

		for (;l<4;++l) {
			Set_Light(l,nullptr);
		}
	}
/*	else {
		for (int l=0;l<4;++l) {
			Set_Light(l,NULL);
		}
	}
*/
}

WW3DSurfaceHandle RenderDevice::_Get_Front_Buffer()
{
	RENDER_THREAD_ASSERT();
	return Backend->Get_Front_Buffer_Copy();
}

SurfaceClass * RenderDevice::_Get_Back_Buffer(unsigned int num)
{
	RENDER_THREAD_ASSERT();

	WW3DSurfaceHandle bb;
	SurfaceClass *surf=nullptr;
	bb = nullptr;
	Backend->Get_Back_Buffer(num, &bb);
	if (bb)
	{
		surf=NEW_REF(SurfaceClass,(bb));
		Release_Resource(bb);
	}

	return surf;
}


TextureClass *
RenderDevice::Create_Render_Target (int width, int height, WW3DFormat format)
{
	RENDER_THREAD_ASSERT();
	Device_Assert();
	number_of_API_calls++;

	// Use the current display format if format isn't specified
	if (format==WW3D_FORMAT_UNKNOWN) {
		unsigned mode_width=0, mode_height=0, mode_format=WW3D_FMT_UNKNOWN;
		Backend->Get_Adapter_Display_Mode(0, &mode_width, &mode_height, &mode_format);
		format=D3DFormat_To_WW3DFormat(mode_format);
	}

	// If render target format isn't supported return NULL
	if (!RenderCaps::Support_Render_To_Texture_Format(format)) {
		WWDEBUG_SAY(("RenderDevice - Render target format is not supported\r\n"));
		return nullptr;
	}

	//
	//	Note: We're going to force the width and height to be powers of two and equal
	//
	const unsigned max_texture_width  = RenderCaps::Get_Max_Texture_Width();
	const unsigned max_texture_height = RenderCaps::Get_Max_Texture_Height();
	float poweroftwosize = static_cast<float>(width);
	if (height > 0 && height < width) {
		poweroftwosize = static_cast<float>(height);
	}
	poweroftwosize = static_cast<float>(::Find_POT (static_cast<int>(poweroftwosize)));

	if (poweroftwosize>max_texture_width) {
		poweroftwosize=static_cast<float>(max_texture_width);
	}
	if (poweroftwosize>max_texture_height) {
		poweroftwosize=static_cast<float>(max_texture_height);
	}

	width = height = static_cast<int>(poweroftwosize);

	//
	//	Attempt to create the render target
	//
	TextureClass * tex = NEW_REF(TextureClass,(width,height,format,MIP_LEVELS_1,TextureClass::POOL_DEFAULT,true));

	// 3dfx drivers are lying in the CheckDeviceFormat call and claiming
	// that they support render targets!
	if (tex->Peek_D3D_Base_Texture() == nullptr) 
	{
		WWDEBUG_SAY(("RenderDevice - Render target creation failed!\r\n"));
		REF_PTR_RELEASE(tex);
	}

	return tex;
}

//**********************************************************************************************
//! Create render target with associated depth stencil buffer
/*! KJM
*/
void RenderDevice::Create_Render_Target
(
	int width, 
	int height, 
	WW3DFormat format,
	WW3DZFormat zformat,
	TextureClass** target,
	ZTextureClass** depth_buffer
)
{
	RENDER_THREAD_ASSERT();
	Device_Assert();
	number_of_API_calls++;

	// Use the current display format if format isn't specified
	if (format==WW3D_FORMAT_UNKNOWN) 
	{
		*target=nullptr;
		*depth_buffer=nullptr;
		return;
/*		unsigned mode_width=0, mode_height=0, mode_format=WW3D_FMT_UNKNOWN;
		Backend->Get_Adapter_Display_Mode(0, &mode_width, &mode_height, &mode_format);
		format=D3DFormat_To_WW3DFormat(mode_format);*/
	}

	// If render target format isn't supported return NULL
	if (!RenderCaps::Support_Render_To_Texture_Format(format) ||
		 !RenderCaps::Support_Depth_Stencil_Format(zformat)) 
	{
		WWDEBUG_SAY(("RenderDevice - Render target with depth format is not supported\r\n"));
		return;
	}

	//	Note: We're going to force the width and height to be powers of two and equal
	const unsigned max_texture_width  = RenderCaps::Get_Max_Texture_Width();
	const unsigned max_texture_height = RenderCaps::Get_Max_Texture_Height();
	float poweroftwosize = static_cast<float>(width);
	if (height > 0 && height < width) 
	{
		poweroftwosize = static_cast<float>(height);
	}
	poweroftwosize = static_cast<float>(::Find_POT (static_cast<int>(poweroftwosize)));

	if (poweroftwosize>max_texture_width) 
	{
		poweroftwosize=static_cast<float>(max_texture_width);
	}

	if (poweroftwosize>max_texture_height) 
	{
		poweroftwosize=static_cast<float>(max_texture_height);
	}

	width = height = static_cast<int>(poweroftwosize);

	//	Attempt to create the render target
	TextureClass* tex=NEW_REF(TextureClass,(width,height,format,MIP_LEVELS_1,TextureClass::POOL_DEFAULT,true));

	// 3dfx drivers are lying in the CheckDeviceFormat call and claiming
	// that they support render targets!
	if (tex->Peek_D3D_Base_Texture() == nullptr) 
	{
		WWDEBUG_SAY(("RenderDevice - Render target creation failed!\r\n"));
		REF_PTR_RELEASE(tex);
	}

	*target=tex;

	// attempt to create the depth stencil buffer
	*depth_buffer=NEW_REF
	(
		ZTextureClass,
		(
			width,
			height,
			zformat,
			MIP_LEVELS_1,
			TextureClass::POOL_DEFAULT
		)
	);
}

/*!
 * Set render target
 * KM Added optional custom z target
 */
void RenderDevice::Set_Render_Target_With_Z
(
	TextureClass* texture,
	ZTextureClass* ztexture
)
{
	WWASSERT(texture!=nullptr);
	WW3DSurfaceHandle d3d_surf = texture->Get_D3D_Surface_Level();
	WWASSERT(d3d_surf != nullptr);

	WW3DSurfaceHandle d3d_zbuf=nullptr;
	if (ztexture!=nullptr)
	{

		d3d_zbuf=ztexture->Get_D3D_Surface_Level();
		WWASSERT(d3d_zbuf!=nullptr);
		Set_Render_Target(d3d_surf,d3d_zbuf);
		Release_Resource(d3d_zbuf);
	}
	else
	{
		Set_Render_Target(d3d_surf,true);
	}
	Release_Resource(d3d_surf);

	IsRenderToTexture = true;
}

void
RenderDevice::Set_Render_Target(WW3DSwapChainHandle swap_chain)
{
	RENDER_THREAD_ASSERT();
	WWASSERT (swap_chain != nullptr);

	// The backend manages the swap chain itself.
	Backend->Set_Render_Target_Swap_Chain(swap_chain);
}

void
RenderDevice::Set_Render_Target(WW3DSurfaceHandle render_target, bool use_default_depth_buffer)
{
	RENDER_THREAD_ASSERT();
	Device_Assert();

	//
	//	Should we restore the default render target set a new one?
	//
	if (render_target == nullptr || render_target == DefaultRenderTarget) 
	{
		// If there is currently a custom render target, default must NOT be NULL.
		if (CurrentRenderTarget) 
		{
			WWASSERT(DefaultRenderTarget!=nullptr);
		}

		//
		//	Restore the default render target
		//
		if (DefaultRenderTarget != nullptr) 
		{
			Backend->Set_Render_Target_Surfaces(DefaultRenderTarget, DefaultDepthBuffer);
			Release_Resource(DefaultRenderTarget);
			DefaultRenderTarget = nullptr;
			if (DefaultDepthBuffer) 
			{
				Release_Resource(DefaultDepthBuffer);
				DefaultDepthBuffer = nullptr;
			}
		}

		//
		//	Release our hold on the "current" render target
		//
		if (CurrentRenderTarget != nullptr) 
		{
			Release_Resource(CurrentRenderTarget);
			CurrentRenderTarget = nullptr;
		}

		if (CurrentDepthBuffer!=nullptr)
		{
			Release_Resource(CurrentDepthBuffer);
			CurrentDepthBuffer=nullptr;
		}

	} 
	else if (render_target != CurrentRenderTarget) 
	{
		WWASSERT(DefaultRenderTarget==nullptr);

		//
		//	We'll need the depth buffer later...
		//
		if (DefaultDepthBuffer == nullptr) 
		{
//		WW3DSurfaceHandle depth_buffer = NULL;
			Backend->Get_Depth_Stencil_Surface(&DefaultDepthBuffer);
		}

		//
		//	Get a pointer to the default render target (if necessary)
		//
		if (DefaultRenderTarget == nullptr) 
		{
			Backend->Get_Render_Target(&DefaultRenderTarget);
		}

		//
		//	Release our hold on the old "current" render target
		//
		if (CurrentRenderTarget != nullptr) 
		{
			Release_Resource(CurrentRenderTarget);
			CurrentRenderTarget = nullptr;
		}

		if (CurrentDepthBuffer!=nullptr)
		{
			Release_Resource(CurrentDepthBuffer);
			CurrentDepthBuffer=nullptr;
		}

		//
		//	Keep a copy of the current render target (for housekeeping)
		//
		CurrentRenderTarget = render_target;
		WWASSERT (CurrentRenderTarget != nullptr);
		if (CurrentRenderTarget != nullptr) 
		{
			Add_Ref_Resource(CurrentRenderTarget);

			//
			//	Switch render targets
			//
			if (use_default_depth_buffer) 
			{
				// With an MSAA back buffer, Set_Render_Target_Surfaces substitutes a depth buffer that
			// matches the (never multisampled) texture target. Returning to the back buffer sets
			// DefaultDepthBuffer again.
			Backend->Set_Render_Target_Surfaces(CurrentRenderTarget, DefaultDepthBuffer);
			}
			else 
			{
			Backend->Set_Render_Target_Surfaces(CurrentRenderTarget, nullptr);
			}
		}
	}

	//
	//	Free our hold on the depth buffer
	//
//	if (depth_buffer != NULL) {
//		depth_buffer->Release ();
//		depth_buffer = NULL;
//	}

	IsRenderToTexture = false;
	return ;
}


//**********************************************************************************************
//! Set render target with depth stencil buffer
/*! KJM
*/
void RenderDevice::Set_Render_Target
(
	WW3DSurfaceHandle render_target, 
	WW3DSurfaceHandle depth_buffer 
)
{
	RENDER_THREAD_ASSERT();
	Device_Assert();

	//
	//	Should we restore the default render target set a new one?
	//
	if (render_target == nullptr || render_target == DefaultRenderTarget) 
	{
		// If there is currently a custom render target, default must NOT be NULL.
		if (CurrentRenderTarget) 
		{
			WWASSERT(DefaultRenderTarget!=nullptr);
		}

		//
		//	Restore the default render target
		//
		if (DefaultRenderTarget != nullptr) 
		{
			Backend->Set_Render_Target_Surfaces(DefaultRenderTarget, DefaultDepthBuffer);
			Release_Resource(DefaultRenderTarget);
			DefaultRenderTarget = nullptr;
			if (DefaultDepthBuffer) 
			{
				Release_Resource(DefaultDepthBuffer);
				DefaultDepthBuffer = nullptr;
			}
		}

		//
		//	Release our hold on the "current" render target
		//
		if (CurrentRenderTarget != nullptr) 
		{
			Release_Resource(CurrentRenderTarget);
			CurrentRenderTarget = nullptr;
		}

		if (CurrentDepthBuffer!=nullptr)
		{
			Release_Resource(CurrentDepthBuffer);
			CurrentDepthBuffer=nullptr;
		}
	}
	else if (render_target != CurrentRenderTarget) 
	{
		WWASSERT(DefaultRenderTarget==nullptr);

		//
		//	We'll need the depth buffer later...
		//
		if (DefaultDepthBuffer == nullptr) 
		{
//		WW3DSurfaceHandle depth_buffer = NULL;
			Backend->Get_Depth_Stencil_Surface(&DefaultDepthBuffer);
		}

		//
		//	Get a pointer to the default render target (if necessary)
		//
		if (DefaultRenderTarget == nullptr) 
		{
			Backend->Get_Render_Target(&DefaultRenderTarget);
		}

		//
		//	Release our hold on the old "current" render target
		//
		if (CurrentRenderTarget != nullptr) 
		{
			Release_Resource(CurrentRenderTarget);
			CurrentRenderTarget = nullptr;
		}

		if (CurrentDepthBuffer!=nullptr)
		{
			Release_Resource(CurrentDepthBuffer);
			CurrentDepthBuffer=nullptr;
		}

		//
		//	Keep a copy of the current render target (for housekeeping)
		//
		CurrentRenderTarget = render_target;
		CurrentDepthBuffer = depth_buffer;
		WWASSERT (CurrentRenderTarget != nullptr);
		if (CurrentRenderTarget != nullptr) 
		{
			Add_Ref_Resource(CurrentRenderTarget);
			Add_Ref_Resource(CurrentDepthBuffer);

			//
			//	Switch render targets
			//
			Backend->Set_Render_Target_Surfaces(CurrentRenderTarget, CurrentDepthBuffer);
		}
	}

	IsRenderToTexture=true;
}


WW3DSwapChainHandle 
RenderDevice::Create_Additional_Swap_Chain (HWND render_window)
{
	return Backend->Create_Additional_Swap_Chain(render_window);
}

void RenderDevice::Flush_Resource_Manager(unsigned int bytes)
{
	Device_Assert();
	// Nothing to do; resource management is automatic.
}

unsigned __int64 RenderDevice::Get_Free_Texture_RAM()
{
	return Backend->Get_Free_Texture_RAM();
}

// Converts a linear gamma ramp to one that is controlled by:
// Gamma - controls the curvature of the middle of the curve
// Bright - controls the minimum value of the curve
// Contrast - controls the difference between the maximum and the minimum of the curve
void RenderDevice::Set_Gamma(float gamma,float bright,float contrast,bool calibrate,bool uselimit)
{
	Backend->Set_Gamma(gamma,bright,contrast,calibrate,uselimit);	// ramp and caps check in the backend
}

//**********************************************************************************************
//! Resets render device to default state
/*!
*/
void RenderDevice::Apply_Default_State()
{
	SNAPSHOT_SAY(("RenderDevice::Apply_Default_State()\n"));
	
	// only set states used in game
	Set_Render_State(WW3D_RS_ZENABLE, TRUE);
//	Set_Render_State(WW3D_RS_FILLMODE, WW3D_FILL_SOLID);
	Set_Render_State(WW3D_RS_SHADEMODE, WW3D_SHADE_GOURAUD);
	//Set_Render_State(D3DRS_LINEPATTERN, 0);
	Set_Render_State(WW3D_RS_ZWRITEENABLE, TRUE);
	Set_Render_State(WW3D_RS_ALPHATESTENABLE, FALSE);
	//Set_Render_State(WW3D_RS_LASTPIXEL, FALSE);
	Set_Render_State(WW3D_RS_SRCBLEND, WW3D_BLEND_ONE);
	Set_Render_State(WW3D_RS_DESTBLEND, WW3D_BLEND_ZERO);
	Set_Render_State(WW3D_RS_CULLMODE, WW3D_CULL_CW);
	Set_Render_State(WW3D_RS_ZFUNC, WW3D_CMP_LESSEQUAL);
	Set_Render_State(WW3D_RS_ALPHAREF, 0);
	Set_Render_State(WW3D_RS_ALPHAFUNC, WW3D_CMP_LESSEQUAL);
	Set_Render_State(WW3D_RS_DITHERENABLE, FALSE);
	Set_Render_State(WW3D_RS_ALPHABLENDENABLE, FALSE);
	Set_Render_State(WW3D_RS_FOGENABLE, FALSE);
	Set_Render_State(WW3D_RS_SPECULARENABLE, FALSE);
//	Set_Render_State(D3DRS_ZVISIBLE, FALSE);
//	Set_Render_State(WW3D_RS_FOGCOLOR, 0);
//	Set_Render_State(WW3D_RS_FOGTABLEMODE, WW3D_FOG_NONE);
//	Set_Render_State(WW3D_RS_FOGSTART, 0);

//	Set_Render_State(WW3D_RS_FOGEND, WWMath::Float_As_Int(1.0f));
//	Set_Render_State(WW3D_RS_FOGDENSITY, WWMath::Float_As_Int(1.0f));

	//Set_Render_State(D3DRS_EDGEANTIALIAS, FALSE);
	Set_Render_State(WW3D_RS_DEPTHBIAS, 0);
//	Set_Render_State(WW3D_RS_RANGEFOGENABLE, FALSE);
	Set_Render_State(WW3D_RS_STENCILENABLE, FALSE);
	Set_Render_State(WW3D_RS_STENCILFAIL, WW3D_STENCILOP_KEEP);
	Set_Render_State(WW3D_RS_STENCILZFAIL, WW3D_STENCILOP_KEEP);
	Set_Render_State(WW3D_RS_STENCILPASS, WW3D_STENCILOP_KEEP);
	Set_Render_State(WW3D_RS_STENCILFUNC, WW3D_CMP_ALWAYS);
	Set_Render_State(WW3D_RS_STENCILREF, 0);
	Set_Render_State(WW3D_RS_STENCILMASK, 0xffffffff);
	Set_Render_State(WW3D_RS_STENCILWRITEMASK, 0xffffffff);
	Set_Render_State(WW3D_RS_TEXTUREFACTOR, 0);
/*	Set_Render_State(WW3D_RS_WRAP0, WW3D_WRAP_U| WW3D_WRAP_V);
	Set_Render_State(WW3D_RS_WRAP1, WW3D_WRAP_U| WW3D_WRAP_V);
	Set_Render_State(WW3D_RS_WRAP2, WW3D_WRAP_U| WW3D_WRAP_V);
	Set_Render_State(WW3D_RS_WRAP3, WW3D_WRAP_U| WW3D_WRAP_V);
	Set_Render_State(D3DRS_WRAP4, WW3D_WRAP_U| WW3D_WRAP_V);
	Set_Render_State(D3DRS_WRAP5, WW3D_WRAP_U| WW3D_WRAP_V);
	Set_Render_State(D3DRS_WRAP6, WW3D_WRAP_U| WW3D_WRAP_V);
	Set_Render_State(D3DRS_WRAP7, WW3D_WRAP_U| WW3D_WRAP_V);*/
	Set_Render_State(WW3D_RS_CLIPPING, TRUE);
	Set_Render_State(WW3D_RS_LIGHTING, FALSE);
	//Set_Render_State(WW3D_RS_AMBIENT, 0);
//	Set_Render_State(WW3D_RS_FOGVERTEXMODE, WW3D_FOG_NONE);
	Set_Render_State(WW3D_RS_COLORVERTEX, TRUE);
/*	Set_Render_State(WW3D_RS_LOCALVIEWER, TRUE);
	Set_Render_State(WW3D_RS_NORMALIZENORMALS, FALSE);
	Set_Render_State(WW3D_RS_DIFFUSEMATERIALSOURCE, WW3D_MCS_COLOR1);
	Set_Render_State(WW3D_RS_SPECULARMATERIALSOURCE, WW3D_MCS_COLOR2);
	Set_Render_State(WW3D_RS_AMBIENTMATERIALSOURCE, WW3D_MCS_MATERIAL);
	Set_Render_State(WW3D_RS_EMISSIVEMATERIALSOURCE, WW3D_MCS_MATERIAL);
	Set_Render_State(WW3D_RS_VERTEXBLEND, D3DVBF_DISABLE);*/
	//Set_Render_State(WW3D_RS_CLIPPLANEENABLE, 0);
	//Set_Render_State(WW3D_RS_POINTSIZE, 0x3f800000);
	//Set_Render_State(WW3D_RS_POINTSIZE_MIN, 0);
	//Set_Render_State(WW3D_RS_POINTSPRITEENABLE, FALSE);
	//Set_Render_State(WW3D_RS_POINTSCALEENABLE, FALSE);
	//Set_Render_State(WW3D_RS_POINTSCALE_A, 0);
	//Set_Render_State(WW3D_RS_POINTSCALE_B, 0);
	//Set_Render_State(WW3D_RS_POINTSCALE_C, 0);
	//Set_Render_State(WW3D_RS_MULTISAMPLEANTIALIAS, TRUE);
	//Set_Render_State(WW3D_RS_MULTISAMPLEMASK, 0xffffffff);
	//Set_Render_State(D3DRS_PATCHEDGESTYLE, D3DPATCHEDGE_DISCRETE);
	//Set_Render_State(WW3D_RS_PATCHSEGMENTS, 0x3f800000);
	//Set_Render_State(D3DRS_DEBUGMONITORTOKEN, D3DDMT_ENABLE);
	//Set_Render_State(WW3D_RS_POINTSIZE_MAX, Float_At_Int(64.0f));
	//Set_Render_State(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
	Set_Render_State(WW3D_RS_COLORWRITEENABLE, 0x0000000f);
	//Set_Render_State(D3DRS_TWEENFACTOR, 0);
	Set_Render_State(WW3D_RS_BLENDOP, WW3D_BLENDOP_ADD);
	//Set_Render_State(D3DRS_POSITIONORDER, D3DORDER_CUBIC);
	//Set_Render_State(D3DRS_NORMALORDER, D3DORDER_LINEAR);

	// disable TSS stages
	int i;
	for (i=0; i<RenderCaps::Get_Max_Textures_Per_Pass(); i++)
	{
		Set_Texture_Stage_State(i, WW3D_TSS_COLOROP, WW3D_TOP_DISABLE);
		Set_Texture_Stage_State(i, WW3D_TSS_COLORARG1, WW3D_TA_TEXTURE);
		Set_Texture_Stage_State(i, WW3D_TSS_COLORARG2, WW3D_TA_DIFFUSE);

		Set_Texture_Stage_State(i, WW3D_TSS_ALPHAOP, WW3D_TOP_DISABLE);
		Set_Texture_Stage_State(i, WW3D_TSS_ALPHAARG1, WW3D_TA_TEXTURE);
		Set_Texture_Stage_State(i, WW3D_TSS_ALPHAARG2, WW3D_TA_DIFFUSE);
	
		/*Set_Texture_Stage_State(i, WW3D_TSS_BUMPENVMAT00, 0);
		Set_Texture_Stage_State(i, WW3D_TSS_BUMPENVMAT01, 0);
		Set_Texture_Stage_State(i, WW3D_TSS_BUMPENVMAT10, 0);
		Set_Texture_Stage_State(i, WW3D_TSS_BUMPENVMAT11, 0);
		Set_Texture_Stage_State(i, WW3D_TSS_BUMPENVLSCALE, 0);
		Set_Texture_Stage_State(i, WW3D_TSS_BUMPENVLOFFSET, 0);*/

		Set_Texture_Stage_State(i, WW3D_TSS_TEXCOORDINDEX, i);
		

		// Addressing and border color are sampler state.
		Set_Sampler_State(i, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
		Set_Sampler_State(i, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);
		Set_Sampler_State(i, WW3D_SAMP_BORDERCOLOR, 0);
//		Set_Texture_Stage_State(i, D3DTSS_MAGFILTER, WW3D_TEXF_LINEAR);
//		Set_Texture_Stage_State(i, D3DTSS_MINFILTER, WW3D_TEXF_LINEAR);
//		Set_Texture_Stage_State(i, D3DTSS_MIPFILTER, WW3D_TEXF_LINEAR);
//		Set_Texture_Stage_State(i, D3DTSS_MIPMAPLODBIAS, 0);
//		Set_Texture_Stage_State(i, D3DTSS_MAXMIPLEVEL, 0);
//		Set_Texture_Stage_State(i, D3DTSS_MAXANISOTROPY, 1);
		//Set_Texture_Stage_State(i, D3DTSS_ADDRESSW, WW3D_TADDRESS_WRAP);
		//Set_Texture_Stage_State(i, WW3D_TSS_COLORARG0, WW3D_TA_CURRENT);
		//Set_Texture_Stage_State(i, WW3D_TSS_ALPHAARG0, WW3D_TA_CURRENT);
		//Set_Texture_Stage_State(i, WW3D_TSS_RESULTARG, WW3D_TA_CURRENT);

		Set_Texture_Stage_State(i, WW3D_TSS_TEXTURETRANSFORMFLAGS, WW3D_TTFF_DISABLE);
		Set_Texture(i,nullptr);
	}

//	RenderDevice::Set_Material(NULL);
	VertexMaterialClass::Apply_Null();

	for (unsigned index=0;index<4;++index) {
		SNAPSHOT_SAY(("Clearing light %d to NULL\n",index));
		Set_Device_Light(index,nullptr);
	}

	// set up simple default TSS 
	Vector4 vconst[MAX_VERTEX_SHADER_CONSTANTS];
	memset(vconst,0,sizeof(Vector4)*MAX_VERTEX_SHADER_CONSTANTS);
	Set_Vertex_Shader_Constant(0, vconst, MAX_VERTEX_SHADER_CONSTANTS);

	Vector4 pconst[MAX_PIXEL_SHADER_CONSTANTS];
	memset(pconst,0,sizeof(Vector4)*MAX_PIXEL_SHADER_CONSTANTS);
	Set_Pixel_Shader_Constant(0, pconst, MAX_PIXEL_SHADER_CONSTANTS);

	Set_Vertex_Shader(RENDER_FVF_XYZNDUV2);
	Set_Pixel_Shader(0);

	ShaderClass::Invalidate();
}

const char* RenderDevice::Get_Render_State_Name(WW3DRenderStateType state)
{
	switch (state) {
	case WW3D_RS_ZENABLE                       : return "WW3D_RS_ZENABLE";
	case WW3D_RS_FILLMODE                      : return "WW3D_RS_FILLMODE";
	case WW3D_RS_SHADEMODE                     : return "WW3D_RS_SHADEMODE";
	case WW3D_RS_ZWRITEENABLE                  : return "WW3D_RS_ZWRITEENABLE";
	case WW3D_RS_ALPHATESTENABLE               : return "WW3D_RS_ALPHATESTENABLE";
	case WW3D_RS_LASTPIXEL                     : return "WW3D_RS_LASTPIXEL";
	case WW3D_RS_SRCBLEND                      : return "WW3D_RS_SRCBLEND";
	case WW3D_RS_DESTBLEND                     : return "WW3D_RS_DESTBLEND";
	case WW3D_RS_CULLMODE                      : return "WW3D_RS_CULLMODE";
	case WW3D_RS_ZFUNC                         : return "WW3D_RS_ZFUNC";
	case WW3D_RS_ALPHAREF                      : return "WW3D_RS_ALPHAREF";
	case WW3D_RS_ALPHAFUNC                     : return "WW3D_RS_ALPHAFUNC";
	case WW3D_RS_DITHERENABLE                  : return "WW3D_RS_DITHERENABLE";
	case WW3D_RS_ALPHABLENDENABLE              : return "WW3D_RS_ALPHABLENDENABLE";
	case WW3D_RS_FOGENABLE                     : return "WW3D_RS_FOGENABLE";
	case WW3D_RS_SPECULARENABLE                : return "WW3D_RS_SPECULARENABLE";
	case WW3D_RS_FOGCOLOR                      : return "WW3D_RS_FOGCOLOR";
	case WW3D_RS_FOGTABLEMODE                  : return "WW3D_RS_FOGTABLEMODE";
	case WW3D_RS_FOGSTART                      : return "WW3D_RS_FOGSTART";
	case WW3D_RS_FOGEND                        : return "WW3D_RS_FOGEND";
	case WW3D_RS_FOGDENSITY                    : return "WW3D_RS_FOGDENSITY";
	case WW3D_RS_RANGEFOGENABLE                : return "WW3D_RS_RANGEFOGENABLE";
	case WW3D_RS_STENCILENABLE                 : return "WW3D_RS_STENCILENABLE";
	case WW3D_RS_STENCILFAIL                   : return "WW3D_RS_STENCILFAIL";
	case WW3D_RS_STENCILZFAIL                  : return "WW3D_RS_STENCILZFAIL";
	case WW3D_RS_STENCILPASS                   : return "WW3D_RS_STENCILPASS";
	case WW3D_RS_STENCILFUNC                   : return "WW3D_RS_STENCILFUNC";
	case WW3D_RS_STENCILREF                    : return "WW3D_RS_STENCILREF";
	case WW3D_RS_STENCILMASK                   : return "WW3D_RS_STENCILMASK";
	case WW3D_RS_STENCILWRITEMASK              : return "WW3D_RS_STENCILWRITEMASK";
	case WW3D_RS_TEXTUREFACTOR                 : return "WW3D_RS_TEXTUREFACTOR";
	case WW3D_RS_WRAP0                         : return "WW3D_RS_WRAP0";
	case WW3D_RS_WRAP1                         : return "WW3D_RS_WRAP1";
	case WW3D_RS_WRAP2                         : return "WW3D_RS_WRAP2";
	case WW3D_RS_WRAP3                         : return "WW3D_RS_WRAP3";
	case DBG_RS_WRAP4                         : return "D3DRS_WRAP4";
	case DBG_RS_WRAP5                         : return "D3DRS_WRAP5";
	case DBG_RS_WRAP6                         : return "D3DRS_WRAP6";
	case DBG_RS_WRAP7                         : return "D3DRS_WRAP7";
	case WW3D_RS_CLIPPING                      : return "WW3D_RS_CLIPPING";
	case WW3D_RS_LIGHTING                      : return "WW3D_RS_LIGHTING";
	case WW3D_RS_AMBIENT                       : return "WW3D_RS_AMBIENT";
	case WW3D_RS_FOGVERTEXMODE                 : return "WW3D_RS_FOGVERTEXMODE";
	case WW3D_RS_COLORVERTEX                   : return "WW3D_RS_COLORVERTEX";
	case WW3D_RS_LOCALVIEWER                   : return "WW3D_RS_LOCALVIEWER";
	case WW3D_RS_NORMALIZENORMALS              : return "WW3D_RS_NORMALIZENORMALS";
	case WW3D_RS_DIFFUSEMATERIALSOURCE         : return "WW3D_RS_DIFFUSEMATERIALSOURCE";
	case WW3D_RS_SPECULARMATERIALSOURCE        : return "WW3D_RS_SPECULARMATERIALSOURCE";
	case WW3D_RS_AMBIENTMATERIALSOURCE         : return "WW3D_RS_AMBIENTMATERIALSOURCE";
	case WW3D_RS_EMISSIVEMATERIALSOURCE        : return "WW3D_RS_EMISSIVEMATERIALSOURCE";
	case WW3D_RS_VERTEXBLEND                   : return "WW3D_RS_VERTEXBLEND";
	case WW3D_RS_CLIPPLANEENABLE               : return "WW3D_RS_CLIPPLANEENABLE";
	case WW3D_RS_POINTSIZE                     : return "WW3D_RS_POINTSIZE";
	case WW3D_RS_POINTSIZE_MIN                 : return "WW3D_RS_POINTSIZE_MIN";
	case WW3D_RS_POINTSPRITEENABLE             : return "WW3D_RS_POINTSPRITEENABLE";
	case WW3D_RS_POINTSCALEENABLE              : return "WW3D_RS_POINTSCALEENABLE";
	case WW3D_RS_POINTSCALE_A                  : return "WW3D_RS_POINTSCALE_A";
	case WW3D_RS_POINTSCALE_B                  : return "WW3D_RS_POINTSCALE_B";
	case WW3D_RS_POINTSCALE_C                  : return "WW3D_RS_POINTSCALE_C";
	case WW3D_RS_MULTISAMPLEANTIALIAS          : return "WW3D_RS_MULTISAMPLEANTIALIAS";
	case WW3D_RS_MULTISAMPLEMASK               : return "WW3D_RS_MULTISAMPLEMASK";
	case DBG_RS_PATCHEDGESTYLE                : return "D3DRS_PATCHEDGESTYLE";
	case DBG_RS_DEBUGMONITORTOKEN             : return "D3DRS_DEBUGMONITORTOKEN";
	case WW3D_RS_POINTSIZE_MAX                 : return "WW3D_RS_POINTSIZE_MAX";
	case DBG_RS_INDEXEDVERTEXBLENDENABLE      : return "D3DRS_INDEXEDVERTEXBLENDENABLE";
	case WW3D_RS_COLORWRITEENABLE              : return "WW3D_RS_COLORWRITEENABLE";
	case DBG_RS_TWEENFACTOR                   : return "D3DRS_TWEENFACTOR";
	case WW3D_RS_BLENDOP                       : return "WW3D_RS_BLENDOP";
//	case DBG_RS_POSITIONORDER                 : return "D3DRS_POSITIONORDER";
//	case DBG_RS_NORMALORDER                   : return "D3DRS_NORMALORDER";
	default											  : return "UNKNOWN";
	}
}

const char* RenderDevice::Get_Texture_Stage_State_Name(WW3DTextureStageStateType state)
{
	switch (state) {
	case WW3D_TSS_COLOROP                   : return "WW3D_TSS_COLOROP";
	case WW3D_TSS_COLORARG1                 : return "WW3D_TSS_COLORARG1";
	case WW3D_TSS_COLORARG2                 : return "WW3D_TSS_COLORARG2";
	case WW3D_TSS_ALPHAOP                   : return "WW3D_TSS_ALPHAOP";
	case WW3D_TSS_ALPHAARG1                 : return "WW3D_TSS_ALPHAARG1";
	case WW3D_TSS_ALPHAARG2                 : return "WW3D_TSS_ALPHAARG2";
	case WW3D_TSS_BUMPENVMAT00              : return "WW3D_TSS_BUMPENVMAT00";
	case WW3D_TSS_BUMPENVMAT01              : return "WW3D_TSS_BUMPENVMAT01";
	case WW3D_TSS_BUMPENVMAT10              : return "WW3D_TSS_BUMPENVMAT10";
	case WW3D_TSS_BUMPENVMAT11              : return "WW3D_TSS_BUMPENVMAT11";
	case WW3D_TSS_TEXCOORDINDEX             : return "WW3D_TSS_TEXCOORDINDEX";
	case WW3D_TSS_BUMPENVLSCALE             : return "WW3D_TSS_BUMPENVLSCALE";
	case WW3D_TSS_BUMPENVLOFFSET            : return "WW3D_TSS_BUMPENVLOFFSET";
	case WW3D_TSS_TEXTURETRANSFORMFLAGS     : return "WW3D_TSS_TEXTURETRANSFORMFLAGS";
	case WW3D_TSS_COLORARG0                 : return "WW3D_TSS_COLORARG0";
	case WW3D_TSS_ALPHAARG0                 : return "WW3D_TSS_ALPHAARG0";
	case WW3D_TSS_RESULTARG                 : return "WW3D_TSS_RESULTARG";
	default										  : return "UNKNOWN";
	}
}

void RenderDevice::Get_Render_State_Value_Name(StringClass& name, WW3DRenderStateType state, unsigned value)
{
	switch (state) {
	case WW3D_RS_ZENABLE:
		name=Get_ZBuffer_Type_Name(value);
		break;

	case WW3D_RS_FILLMODE:
		name=Get_Fill_Mode_Name(value);
		break;

	case WW3D_RS_SHADEMODE:
		name=Get_Shade_Mode_Name(value);
		break;

	case WW3D_RS_FOGCOLOR:
	case WW3D_RS_ALPHAREF:
	case WW3D_RS_STENCILMASK:
	case WW3D_RS_STENCILWRITEMASK:
	case WW3D_RS_TEXTUREFACTOR:
	case WW3D_RS_AMBIENT:
	case WW3D_RS_CLIPPLANEENABLE:
	case WW3D_RS_MULTISAMPLEMASK:
		name.Format("0x%x",value);
		break;

	case WW3D_RS_ZWRITEENABLE:
	case WW3D_RS_ALPHATESTENABLE:
	case WW3D_RS_LASTPIXEL:
	case WW3D_RS_DITHERENABLE:
	case WW3D_RS_ALPHABLENDENABLE:
	case WW3D_RS_FOGENABLE:
	case WW3D_RS_SPECULARENABLE:
	case WW3D_RS_STENCILENABLE:
	case WW3D_RS_RANGEFOGENABLE:
	case WW3D_RS_CLIPPING:
	case WW3D_RS_LIGHTING:
	case WW3D_RS_COLORVERTEX:
	case WW3D_RS_LOCALVIEWER:
	case WW3D_RS_NORMALIZENORMALS:
	case WW3D_RS_POINTSPRITEENABLE:
	case WW3D_RS_POINTSCALEENABLE:
	case WW3D_RS_MULTISAMPLEANTIALIAS:
	case DBG_RS_INDEXEDVERTEXBLENDENABLE:
		name=value ? "TRUE" : "FALSE";
		break;

	case WW3D_RS_SRCBLEND:
	case WW3D_RS_DESTBLEND:
		name=Get_Blend_Name(value);
		break;

	case WW3D_RS_CULLMODE:
		name=Get_Cull_Mode_Name(value);
		break;

	case WW3D_RS_ZFUNC:
	case WW3D_RS_ALPHAFUNC:
	case WW3D_RS_STENCILFUNC:
		name=Get_Cmp_Func_Name(value);
		break;

	case WW3D_RS_FOGTABLEMODE:
	case WW3D_RS_FOGVERTEXMODE:
		name=Get_Fog_Mode_Name(value);
		break;

	case WW3D_RS_FOGSTART:
	case WW3D_RS_FOGEND:
	case WW3D_RS_FOGDENSITY:
	case WW3D_RS_POINTSIZE:
	case WW3D_RS_POINTSIZE_MIN:
	case WW3D_RS_POINTSCALE_A:
	case WW3D_RS_POINTSCALE_B:
	case WW3D_RS_POINTSCALE_C:
	case WW3D_RS_POINTSIZE_MAX:
	case DBG_RS_TWEENFACTOR:
			name.Format("%f",*reinterpret_cast<float*>(&value));
		break;

	case WW3D_RS_STENCILREF:
		name.Format("%d",value);
		break;

	case WW3D_RS_STENCILFAIL:
	case WW3D_RS_STENCILZFAIL:
	case WW3D_RS_STENCILPASS:
		name=Get_Stencil_Op_Name(value);
		break;

	case WW3D_RS_WRAP0:
	case WW3D_RS_WRAP1:
	case WW3D_RS_WRAP2:
	case WW3D_RS_WRAP3:
	case DBG_RS_WRAP4:
	case DBG_RS_WRAP5:
	case DBG_RS_WRAP6:
	case DBG_RS_WRAP7:
		name="0";
		if (value&WW3D_WRAP_U) name+="|WW3D_WRAP_U";
		if (value&WW3D_WRAP_V) name+="|WW3D_WRAP_V";
		if (value&WW3D_WRAP_W) name+="|WW3D_WRAP_W";
		break;

	case WW3D_RS_DIFFUSEMATERIALSOURCE:
	case WW3D_RS_SPECULARMATERIALSOURCE:
	case WW3D_RS_AMBIENTMATERIALSOURCE:
	case WW3D_RS_EMISSIVEMATERIALSOURCE:
		name=Get_Material_Source_Name(value);
		break;

	case WW3D_RS_VERTEXBLEND:
		name=Get_Vertex_Blend_Flag_Name(value);
		break;

	case DBG_RS_PATCHEDGESTYLE:
		name=Get_Patch_Edge_Style_Name(value);
		break;

	case DBG_RS_DEBUGMONITORTOKEN:
		name=Get_Debug_Monitor_Token_Name(value);
		break;

	case WW3D_RS_COLORWRITEENABLE:
		name="0";
		if (value&WW3D_COLORWRITEENABLE_RED) name+="|WW3D_COLORWRITEENABLE_RED";
		if (value&WW3D_COLORWRITEENABLE_GREEN) name+="|WW3D_COLORWRITEENABLE_GREEN";
		if (value&WW3D_COLORWRITEENABLE_BLUE) name+="|WW3D_COLORWRITEENABLE_BLUE";
		if (value&WW3D_COLORWRITEENABLE_ALPHA) name+="|WW3D_COLORWRITEENABLE_ALPHA";
		break;
	case WW3D_RS_BLENDOP:
		name=Get_Blend_Op_Name(value);
		break;
	default:
		name.Format("UNKNOWN (%d)",value);
		break;
	}
}

void RenderDevice::Get_Texture_Stage_State_Value_Name(StringClass& name, WW3DTextureStageStateType state, unsigned value)
{
	switch (state) {
	case WW3D_TSS_COLOROP:
	case WW3D_TSS_ALPHAOP:
		name=Get_Texture_Op_Name(value);
		break;

	case WW3D_TSS_COLORARG0:
	case WW3D_TSS_COLORARG1:
	case WW3D_TSS_COLORARG2:
	case WW3D_TSS_ALPHAARG0:
	case WW3D_TSS_ALPHAARG1:
	case WW3D_TSS_ALPHAARG2:
	case WW3D_TSS_RESULTARG:
		name=Get_Texture_Arg_Name(value);
		break;

	case WW3D_TSS_TEXTURETRANSFORMFLAGS:
		name=Get_Texture_Transform_Flag_Name(value);
		break;

	// Floating point values
	case WW3D_TSS_BUMPENVMAT00:
	case WW3D_TSS_BUMPENVMAT01:
	case WW3D_TSS_BUMPENVMAT10:
	case WW3D_TSS_BUMPENVMAT11:
	case WW3D_TSS_BUMPENVLSCALE:
	case WW3D_TSS_BUMPENVLOFFSET:
		name.Format("%f",*reinterpret_cast<float*>(&value));
		break;

	case WW3D_TSS_TEXCOORDINDEX:
		if ((value&0xffff0000)==WW3D_TCI_CAMERASPACENORMAL) {
			name.Format("WW3D_TCI_CAMERASPACENORMAL|%d",value&0xffff);
		}
		else if ((value&0xffff0000)==WW3D_TCI_CAMERASPACEPOSITION) {
			name.Format("WW3D_TCI_CAMERASPACEPOSITION|%d",value&0xffff);
		}
		else if ((value&0xffff0000)==WW3D_TCI_CAMERASPACEREFLECTIONVECTOR) {
			name.Format("WW3D_TCI_CAMERASPACEREFLECTIONVECTOR|%d",value&0xffff);
		}
		else {
			name.Format("%d",value);
		}
		break;

	default:
		name.Format("UNKNOWN (%d)",value);
		break;
	}
}

const char* RenderDevice::Get_Texture_Op_Name(unsigned value)
{
	switch (value) {
	case WW3D_TOP_DISABLE                      : return "WW3D_TOP_DISABLE";
	case WW3D_TOP_SELECTARG1                   : return "WW3D_TOP_SELECTARG1";
	case WW3D_TOP_SELECTARG2                   : return "WW3D_TOP_SELECTARG2";
	case WW3D_TOP_MODULATE                     : return "WW3D_TOP_MODULATE";
	case WW3D_TOP_MODULATE2X                   : return "WW3D_TOP_MODULATE2X";
	case WW3D_TOP_MODULATE4X                   : return "WW3D_TOP_MODULATE4X";
	case WW3D_TOP_ADD                          : return "WW3D_TOP_ADD";
	case WW3D_TOP_ADDSIGNED                    : return "WW3D_TOP_ADDSIGNED";
	case WW3D_TOP_ADDSIGNED2X                  : return "WW3D_TOP_ADDSIGNED2X";
	case WW3D_TOP_SUBTRACT                     : return "WW3D_TOP_SUBTRACT";
	case WW3D_TOP_ADDSMOOTH                    : return "WW3D_TOP_ADDSMOOTH";
	case WW3D_TOP_BLENDDIFFUSEALPHA            : return "WW3D_TOP_BLENDDIFFUSEALPHA";
	case WW3D_TOP_BLENDTEXTUREALPHA            : return "WW3D_TOP_BLENDTEXTUREALPHA";
	case WW3D_TOP_BLENDFACTORALPHA             : return "WW3D_TOP_BLENDFACTORALPHA";
	case WW3D_TOP_BLENDTEXTUREALPHAPM          : return "WW3D_TOP_BLENDTEXTUREALPHAPM";
	case WW3D_TOP_BLENDCURRENTALPHA            : return "WW3D_TOP_BLENDCURRENTALPHA";
	case WW3D_TOP_PREMODULATE                  : return "WW3D_TOP_PREMODULATE";
	case WW3D_TOP_MODULATEALPHA_ADDCOLOR       : return "WW3D_TOP_MODULATEALPHA_ADDCOLOR";
	case WW3D_TOP_MODULATECOLOR_ADDALPHA       : return "WW3D_TOP_MODULATECOLOR_ADDALPHA";
	case WW3D_TOP_MODULATEINVALPHA_ADDCOLOR    : return "WW3D_TOP_MODULATEINVALPHA_ADDCOLOR";
	case WW3D_TOP_MODULATEINVCOLOR_ADDALPHA    : return "WW3D_TOP_MODULATEINVCOLOR_ADDALPHA";
	case WW3D_TOP_BUMPENVMAP                   : return "WW3D_TOP_BUMPENVMAP";
	case WW3D_TOP_BUMPENVMAPLUMINANCE          : return "WW3D_TOP_BUMPENVMAPLUMINANCE";
	case WW3D_TOP_DOTPRODUCT3                  : return "WW3D_TOP_DOTPRODUCT3";
	case WW3D_TOP_MULTIPLYADD                  : return "WW3D_TOP_MULTIPLYADD";
	case WW3D_TOP_LERP                         : return "WW3D_TOP_LERP";
	default										     : return "UNKNOWN";
	}
}

const char* RenderDevice::Get_Texture_Arg_Name(unsigned value)
{
	switch (value) {
	case WW3D_TA_CURRENT			: return "WW3D_TA_CURRENT";
	case WW3D_TA_DIFFUSE			: return "WW3D_TA_DIFFUSE";
	case DBG_TA_SELECTMASK		: return "D3DTA_SELECTMASK";
	case WW3D_TA_SPECULAR			: return "WW3D_TA_SPECULAR";
	case WW3D_TA_TEMP				: return "WW3D_TA_TEMP";
	case WW3D_TA_TEXTURE			: return "WW3D_TA_TEXTURE";
	case WW3D_TA_TFACTOR			: return "WW3D_TA_TFACTOR";
	case WW3D_TA_ALPHAREPLICATE	: return "WW3D_TA_ALPHAREPLICATE";
	case WW3D_TA_COMPLEMENT		: return "WW3D_TA_COMPLEMENT";
	default					      : return "UNKNOWN";
	}
}

const char* RenderDevice::Get_Texture_Filter_Name(unsigned value)
{
	switch (value) {
	case WW3D_TEXF_NONE				: return "WW3D_TEXF_NONE";
	case WW3D_TEXF_POINT			: return "WW3D_TEXF_POINT";
	case WW3D_TEXF_LINEAR			: return "WW3D_TEXF_LINEAR";
	case WW3D_TEXF_ANISOTROPIC	: return "WW3D_TEXF_ANISOTROPIC";
	default					      : return "UNKNOWN";
	}
}

const char* RenderDevice::Get_Texture_Address_Name(unsigned value)
{
	switch (value) {
	case WW3D_TADDRESS_WRAP		: return "WW3D_TADDRESS_WRAP";
	case WW3D_TADDRESS_MIRROR		: return "WW3D_TADDRESS_MIRROR";
	case WW3D_TADDRESS_CLAMP		: return "WW3D_TADDRESS_CLAMP";
	case WW3D_TADDRESS_BORDER		: return "WW3D_TADDRESS_BORDER";
	case WW3D_TADDRESS_MIRRORONCE: return "WW3D_TADDRESS_MIRRORONCE";
	default					      : return "UNKNOWN";
	}
}

const char* RenderDevice::Get_Texture_Transform_Flag_Name(unsigned value)
{
	switch (value) {
	case WW3D_TTFF_DISABLE			: return "WW3D_TTFF_DISABLE";
	case WW3D_TTFF_COUNT1			: return "WW3D_TTFF_COUNT1";
	case WW3D_TTFF_COUNT2			: return "WW3D_TTFF_COUNT2";
	case WW3D_TTFF_COUNT3			: return "WW3D_TTFF_COUNT3";
	case WW3D_TTFF_COUNT4			: return "WW3D_TTFF_COUNT4";
	case WW3D_TTFF_PROJECTED		: return "WW3D_TTFF_PROJECTED";
	default					      : return "UNKNOWN";
	}
}

const char* RenderDevice::Get_ZBuffer_Type_Name(unsigned value)
{
	switch (value) {
	case DBG_ZB_FALSE				: return "D3DZB_FALSE";
	case DBG_ZB_TRUE				: return "D3DZB_TRUE";
	case DBG_ZB_USEW				: return "D3DZB_USEW";
	default					      : return "UNKNOWN";
	}
}

const char* RenderDevice::Get_Fill_Mode_Name(unsigned value)
{
	switch (value) {
	case WW3D_FILL_POINT			: return "WW3D_FILL_POINT";
	case WW3D_FILL_WIREFRAME		: return "WW3D_FILL_WIREFRAME";
	case WW3D_FILL_SOLID			: return "WW3D_FILL_SOLID";
	default					      : return "UNKNOWN";
	}
}

const char* RenderDevice::Get_Shade_Mode_Name(unsigned value)
{
	switch (value) {
	case WW3D_SHADE_FLAT			: return "WW3D_SHADE_FLAT";
	case WW3D_SHADE_GOURAUD		: return "WW3D_SHADE_GOURAUD";
	case DBG_SHADE_PHONG			: return "D3DSHADE_PHONG";
	default							: return "UNKNOWN";
	}
}

const char* RenderDevice::Get_Blend_Name(unsigned value)
{
	switch (value) {
	case WW3D_BLEND_ZERO                : return "WW3D_BLEND_ZERO";
	case WW3D_BLEND_ONE                 : return "WW3D_BLEND_ONE";
	case WW3D_BLEND_SRCCOLOR            : return "WW3D_BLEND_SRCCOLOR";
	case WW3D_BLEND_INVSRCCOLOR         : return "WW3D_BLEND_INVSRCCOLOR";
	case WW3D_BLEND_SRCALPHA            : return "WW3D_BLEND_SRCALPHA";
	case WW3D_BLEND_INVSRCALPHA         : return "WW3D_BLEND_INVSRCALPHA";
	case WW3D_BLEND_DESTALPHA           : return "WW3D_BLEND_DESTALPHA";
	case WW3D_BLEND_INVDESTALPHA        : return "WW3D_BLEND_INVDESTALPHA";
	case WW3D_BLEND_DESTCOLOR           : return "WW3D_BLEND_DESTCOLOR";
	case WW3D_BLEND_INVDESTCOLOR        : return "WW3D_BLEND_INVDESTCOLOR";
	case WW3D_BLEND_SRCALPHASAT         : return "WW3D_BLEND_SRCALPHASAT";
	case DBG_BLEND_BOTHSRCALPHA        : return "D3DBLEND_BOTHSRCALPHA";
	case DBG_BLEND_BOTHINVSRCALPHA     : return "D3DBLEND_BOTHINVSRCALPHA";
	default									 : return "UNKNOWN";
	}
}

const char* RenderDevice::Get_Cull_Mode_Name(unsigned value)
{
	switch (value) {
	case WW3D_CULL_NONE				: return "WW3D_CULL_NONE";
	case WW3D_CULL_CW				: return "WW3D_CULL_CW";
	case WW3D_CULL_CCW				: return "WW3D_CULL_CCW";
	default							: return "UNKNOWN";
	}
}

const char* RenderDevice::Get_Cmp_Func_Name(unsigned value)
{
	switch (value) {
	case WW3D_CMP_NEVER          : return "WW3D_CMP_NEVER";
	case WW3D_CMP_LESS           : return "WW3D_CMP_LESS";
	case WW3D_CMP_EQUAL          : return "WW3D_CMP_EQUAL";
	case WW3D_CMP_LESSEQUAL      : return "WW3D_CMP_LESSEQUAL";
	case WW3D_CMP_GREATER        : return "WW3D_CMP_GREATER";
	case WW3D_CMP_NOTEQUAL       : return "WW3D_CMP_NOTEQUAL";
	case WW3D_CMP_GREATEREQUAL   : return "WW3D_CMP_GREATEREQUAL";
	case WW3D_CMP_ALWAYS         : return "WW3D_CMP_ALWAYS";
	default							: return "UNKNOWN";
	}
}

const char* RenderDevice::Get_Fog_Mode_Name(unsigned value)
{
	switch (value) {
	case WW3D_FOG_NONE				: return "WW3D_FOG_NONE";
	case WW3D_FOG_EXP				: return "WW3D_FOG_EXP";
	case WW3D_FOG_EXP2				: return "WW3D_FOG_EXP2";
	case WW3D_FOG_LINEAR			: return "WW3D_FOG_LINEAR";
	default							: return "UNKNOWN";
	}
}

const char* RenderDevice::Get_Stencil_Op_Name(unsigned value)
{
	switch (value) {
	case WW3D_STENCILOP_KEEP		: return "WW3D_STENCILOP_KEEP";
	case WW3D_STENCILOP_ZERO		: return "WW3D_STENCILOP_ZERO";
	case WW3D_STENCILOP_REPLACE	: return "WW3D_STENCILOP_REPLACE";
	case WW3D_STENCILOP_INCRSAT	: return "WW3D_STENCILOP_INCRSAT";
	case WW3D_STENCILOP_DECRSAT	: return "WW3D_STENCILOP_DECRSAT";
	case WW3D_STENCILOP_INVERT	: return "WW3D_STENCILOP_INVERT";
	case WW3D_STENCILOP_INCR		: return "WW3D_STENCILOP_INCR";
	case WW3D_STENCILOP_DECR		: return "WW3D_STENCILOP_DECR";
	default							: return "UNKNOWN";
	}
}

const char* RenderDevice::Get_Material_Source_Name(unsigned value)
{
	switch (value) {
	case WW3D_MCS_MATERIAL			: return "WW3D_MCS_MATERIAL";
	case WW3D_MCS_COLOR1			: return "WW3D_MCS_COLOR1";
	case WW3D_MCS_COLOR2			: return "WW3D_MCS_COLOR2";
	default							: return "UNKNOWN";
	}
}

const char* RenderDevice::Get_Vertex_Blend_Flag_Name(unsigned value)
{
	switch (value) {
	case DBG_VBF_DISABLE			: return "D3DVBF_DISABLE";
	case DBG_VBF_1WEIGHTS			: return "D3DVBF_1WEIGHTS";
	case DBG_VBF_2WEIGHTS			: return "D3DVBF_2WEIGHTS";
	case DBG_VBF_3WEIGHTS			: return "D3DVBF_3WEIGHTS";
	case DBG_VBF_TWEENING			: return "D3DVBF_TWEENING";
	case DBG_VBF_0WEIGHTS			: return "D3DVBF_0WEIGHTS";
	default							: return "UNKNOWN";
	}
}

const char* RenderDevice::Get_Patch_Edge_Style_Name(unsigned value)
{
	switch (value) {
	case DBG_PATCHEDGE_DISCRETE	: return "D3DPATCHEDGE_DISCRETE";
   case DBG_PATCHEDGE_CONTINUOUS:return "D3DPATCHEDGE_CONTINUOUS";
	default							: return "UNKNOWN";
	}
}

const char* RenderDevice::Get_Debug_Monitor_Token_Name(unsigned value)
{
	switch (value) {
	case DBG_DMT_ENABLE			: return "D3DDMT_ENABLE";
	case DBG_DMT_DISABLE			: return "D3DDMT_DISABLE";
	default							: return "UNKNOWN";
	}
}

const char* RenderDevice::Get_Blend_Op_Name(unsigned value)
{
	switch (value) {
	case WW3D_BLENDOP_ADD			: return "WW3D_BLENDOP_ADD";
	case WW3D_BLENDOP_SUBTRACT	: return "WW3D_BLENDOP_SUBTRACT";
	case WW3D_BLENDOP_REVSUBTRACT: return "WW3D_BLENDOP_REVSUBTRACT";
	case WW3D_BLENDOP_MIN			: return "WW3D_BLENDOP_MIN";
	case WW3D_BLENDOP_MAX			: return "WW3D_BLENDOP_MAX";
	default							: return "UNKNOWN";
	}
}


//============================================================================
// RenderDevice::getBackBufferFormat
//============================================================================

WW3DFormat	RenderDevice::getBackBufferFormat( void )
{
	return Backend->Get_Back_Buffer_Format();
}

// --- Device-facing functions --------------------------------------------------------------------
// Kept out of the header so that only the backend-neutral names from ww3dtypes.h are needed there.

void RenderDevice::Set_Vertex_Shader(DWORD vertex_shader)
{

	Vertex_Shader=vertex_shader;
	// The DWORD is an FVF code, not a shader: no shader object, a cached vertex declaration
	// derived from the FVF code instead.
	Backend->Set_Vertex_Shader(nullptr);
	Set_Vertex_Declaration(Get_Vertex_Declaration_For_FVF(Vertex_Shader));
}

void RenderDevice::Set_Pixel_Shader(DWORD pixel_shader)
{
	// may be incorrect if shaders are created and destroyed dynamically
	if (Pixel_Shader==pixel_shader) return;

	Pixel_Shader=pixel_shader;
	// Callers only ever pass 0 ("no pixel shader"), so NULL is enough.
	Backend->Set_Pixel_Shader(nullptr);
}

void RenderDevice::Set_Vertex_Shader_Constant(int reg, const void* data, int count)
{
	int memsize=sizeof(Vector4)*count;

	// may be incorrect if shaders are created and destroyed dynamically
	if (memcmp(data, &Vertex_Shader_Constants[reg],memsize)==0) return;

	memcpy(&Vertex_Shader_Constants[reg],data,memsize);
	Backend->Set_Vertex_Shader_Constant(reg,static_cast<const float*>(data),count);
}

void RenderDevice::Set_Pixel_Shader_Constant(int reg, const void* data, int count)
{
	// never write past the shadow array
	WWASSERT(reg >= 0 && count >= 0 && reg + count <= static_cast<int>(MAX_PIXEL_SHADER_CONSTANTS));
	if (reg < 0 || count <= 0 || reg + count > static_cast<int>(MAX_PIXEL_SHADER_CONSTANTS)) return;
	int memsize=sizeof(Vector4)*count;

	// may be incorrect if shaders are created and destroyed dynamically
	if (memcmp(data, &Pixel_Shader_Constants[reg],memsize)==0) return;

	memcpy(&Pixel_Shader_Constants[reg],data,memsize);
	Backend->Set_Pixel_Shader_Constant(reg,static_cast<const float*>(data),count);
}

void RenderDevice::_Set_Device_Transform(WW3DTransformType transform,const Matrix4x4& m)
{
	WWASSERT(transform<=WW3D_TS_WORLD);
	{
		DeviceTransforms[transform]=m;
		SNAPSHOT_SAY(("D3D - SetTransform %d [%f,%f,%f,%f][%f,%f,%f,%f][%f,%f,%f,%f][%f,%f,%f,%f]\n",transform,m[0][0],m[0][1],m[0][2],m[0][3],m[1][0],m[1][1],m[1][2],m[1][3],m[2][0],m[2][1],m[2][2],m[2][3],m[3][0],m[3][1],m[3][2],m[3][3]));
		WW3D_RECORD_MATRIX_CHANGE();
		Backend->Set_Transform(transform,reinterpret_cast<const WW3DMatrixRaw*>(&m));
	}
}

void RenderDevice::_Set_Device_Transform(WW3DTransformType transform,const Matrix3D& m)
{
	WWASSERT(transform<=WW3D_TS_WORLD);
	Matrix4x4 mtx(m);
	{
		DeviceTransforms[transform]=mtx;
		SNAPSHOT_SAY(("D3D - SetTransform %d [%f,%f,%f,%f][%f,%f,%f,%f][%f,%f,%f,%f]\n",transform,m[0][0],m[0][1],m[0][2],m[0][3],m[1][0],m[1][1],m[1][2],m[1][3],m[2][0],m[2][1],m[2][2],m[2][3]));
		WW3D_RECORD_MATRIX_CHANGE();
		Backend->Set_Transform(transform,reinterpret_cast<const WW3DMatrixRaw*>(&m));
	}
}

void RenderDevice::_Get_Device_Transform(WW3DTransformType transform, Matrix4x4& m)
{
	Backend->Get_Transform(transform,reinterpret_cast<WW3DMatrixRaw*>(&m));
}

// Projection for vertex shader constants (trees, water); use this instead of
// _Get_Device_Transform(WW3D_TS_PROJECTION). Same layout as _Get_Device_Transform: the Matrix4x4
// holds the D3D row matrix, not the Westwood convention.
void RenderDevice::_Get_Device_Shader_Projection(Matrix4x4& m)
{
	Backend->Get_Shader_Projection(reinterpret_cast<WW3DMatrixRaw*>(&m));
}

void RenderDevice::Set_Device_Material(const WW3DMaterial* mat)
{
	WW3D_RECORD_MATERIAL_CHANGE();
	WWASSERT(mat);
	SNAPSHOT_SAY(("D3D - SetMaterial\n"));
	Backend->Set_Material(mat);
}

void RenderDevice::Set_Device_Light(int index, WW3DLight* light)
{
	if (light) {
		WW3D_RECORD_LIGHT_CHANGE();
		Backend->Set_Light(index, light);
		CurrentDeviceLightEnables[index]=true;
		SNAPSHOT_SAY(("D3D - SetLight %d\n",index));
	}
	else if (CurrentDeviceLightEnables[index]) {
		WW3D_RECORD_LIGHT_CHANGE();
		CurrentDeviceLightEnables[index]=false;
		Backend->Set_Light(index, nullptr);
		SNAPSHOT_SAY(("D3D - DisableLight %d\n",index));
	}
}

void RenderDevice::Set_Render_State(WW3DRenderStateType state, unsigned value)
{
	// Can't monitor state changes because setShader call to GERD may change the states!
	if (RenderStates[state]==value) return;

#ifdef MESH_RENDER_SNAPSHOT_ENABLED
	if (WW3D::Is_Snapshot_Activated()) {
		StringClass value_name(0,true);
		Get_Render_State_Value_Name(value_name,state,value);
		SNAPSHOT_SAY(("D3D - SetRenderState(state: %s, value: %s)\n",
			Get_Render_State_Name(state),
			value_name.Peek_Buffer()));
	}
#endif

	RenderStates[state]=value;
	Backend->Set_Render_State(state, value);
	WW3D_RECORD_RENDER_STATE_CHANGE();
}

void RenderDevice::Set_Clip_Plane(DWORD Index, CONST float* pPlane)
{
	Backend->Set_Clip_Plane(Index, pPlane);
}

void RenderDevice::Set_Texture_Stage_State(unsigned stage, WW3DTextureStageStateType state, unsigned value)
{
	// All slots go through the redundancy cache.
	WWASSERT(stage < MAX_TEXTURE_SLOTS);

	// Can't monitor state changes because setShader call to GERD may change the states!
	if (TextureStageStates[stage][static_cast<unsigned int>(state)]==value) return;
#ifdef MESH_RENDER_SNAPSHOT_ENABLED
	if (WW3D::Is_Snapshot_Activated()) {
		StringClass value_name(0,true);
		Get_Texture_Stage_State_Value_Name(value_name,state,value);
		SNAPSHOT_SAY(("D3D - SetTextureStageState(stage: %d, state: %s, value: %s)\n",
			stage,
			Get_Texture_Stage_State_Name(state),
			value_name.Peek_Buffer()));
	}
#endif

	TextureStageStates[stage][static_cast<unsigned int>(state)]=value;
	Backend->Set_Texture_Stage_State(stage, state, value);
	WW3D_RECORD_TEXTURE_STAGE_STATE_CHANGE();
}

void RenderDevice::Set_Sampler_State(unsigned stage, WW3DSamplerStateType state, unsigned value)
{
	WWASSERT(stage < MAX_TEXTURE_SLOTS);

	if (SamplerStates[stage][static_cast<unsigned int>(state)]==value) return;

	SNAPSHOT_SAY(("D3D - SetSamplerState(%d,%d,%d)\n",stage,state,value));

	SamplerStates[stage][static_cast<unsigned int>(state)]=value;
	Backend->Set_Sampler_State(stage, state, value);
	WW3D_RECORD_TEXTURE_STAGE_STATE_CHANGE();
}

void RenderDevice::Set_Device_Texture(unsigned int stage, WW3DTextureHandle texture)
{
	WWASSERT(stage < MAX_TEXTURE_SLOTS);

	if (Textures[stage]==texture) return;

	SNAPSHOT_SAY(("D3D - SetTexture(%p) \n",texture));

	if (Textures[stage]) Release_Resource(Textures[stage]);
	Textures[stage] = texture;
	if (Textures[stage]) Add_Ref_Resource(Textures[stage]);
	Backend->Set_Texture(stage, texture);
	WW3D_RECORD_TEXTURE_CHANGE();
}

void RenderDevice::Set_Projection_Transform_With_Z_Bias(const Matrix4x4& matrix, float znear, float zfar)
{
	ZFar=zfar;
	ZNear=znear;
	ProjectionMatrix=matrix.Transpose();

	if (!RenderCaps::Support_ZBias() && ZNear!=ZFar) {
		Matrix4x4 tmp=ProjectionMatrix;
		float tmp_zbias=static_cast<float>(ZBias);
		tmp_zbias*=(1.0f/16.0f);
		tmp_zbias*=1.0f / (ZFar - ZNear);
		tmp[2][2]-=tmp_zbias*tmp[3][2];
		Backend->Set_Transform(WW3D_TS_PROJECTION,reinterpret_cast<const WW3DMatrixRaw*>(&tmp));
	}
	else {
		Backend->Set_Transform(WW3D_TS_PROJECTION,reinterpret_cast<const WW3DMatrixRaw*>(&ProjectionMatrix));
	}
}

void RenderDevice::Set_ZBias(int zbias)
{
	if (zbias==ZBias) return;
	if (zbias>15) zbias=15;
	if (zbias<0) zbias=0;
	ZBias=zbias;

	if (!RenderCaps::Support_ZBias() && ZNear!=ZFar) {
		Matrix4x4 tmp=ProjectionMatrix;
		float tmp_zbias=static_cast<float>(ZBias);
		tmp_zbias*=(1.0f/16.0f);
		tmp_zbias*=1.0f / (ZFar - ZNear);
		tmp[2][2]-=tmp_zbias*tmp[3][2];
		Backend->Set_Transform(WW3D_TS_PROJECTION,reinterpret_cast<const WW3DMatrixRaw*>(&tmp));
	}
	else {
		Set_Render_State (WW3D_RS_DEPTHBIAS, Convert_ZBias_Level(ZBias));
	}
}

void RenderDevice::Set_Transform(WW3DTransformType transform,const Matrix4x4& m)
{
	switch (static_cast<int>(transform)) {
	case WW3D_TS_WORLD:
		render_state.world=m.Transpose();
		render_state_changed|=static_cast<unsigned>(WORLD_CHANGED);
		render_state_changed&=~static_cast<unsigned>(WORLD_IDENTITY);
		break;
	case WW3D_TS_VIEW:
		render_state.view=m.Transpose();
		render_state_changed|=static_cast<unsigned>(VIEW_CHANGED);
		render_state_changed&=~static_cast<unsigned>(VIEW_IDENTITY);
		break;
	case WW3D_TS_PROJECTION:
		{
			Matrix4x4 projection=m.Transpose();
			ZFar=0.0f;
			ZNear=0.0f;
			Backend->Set_Transform(WW3D_TS_PROJECTION,reinterpret_cast<const WW3DMatrixRaw*>(&projection));
		}
		break;
	default:
		WW3D_RECORD_MATRIX_CHANGE();
		Matrix4x4 m2=m.Transpose();
		Backend->Set_Transform(transform,reinterpret_cast<const WW3DMatrixRaw*>(&m2));
		break;
	}
}

void RenderDevice::Set_Transform(WW3DTransformType transform,const Matrix3D& m)
{
	Matrix4x4 m2(m);
	switch (static_cast<int>(transform)) {
	case WW3D_TS_WORLD:
		render_state.world=m2.Transpose();
		render_state_changed|=static_cast<unsigned>(WORLD_CHANGED);
		render_state_changed&=~static_cast<unsigned>(WORLD_IDENTITY);
		break;
	case WW3D_TS_VIEW:
		render_state.view=m2.Transpose();
		render_state_changed|=static_cast<unsigned>(VIEW_CHANGED);
		render_state_changed&=~static_cast<unsigned>(VIEW_IDENTITY);
		break;
	default:
		WW3D_RECORD_MATRIX_CHANGE();
		m2=m2.Transpose();
		Backend->Set_Transform(transform,reinterpret_cast<const WW3DMatrixRaw*>(&m2));
		break;
	}
}

void RenderDevice::Get_Transform(WW3DTransformType transform, Matrix4x4& m)
{
	WW3DMatrixRaw mat;

	switch (static_cast<int>(transform)) {
	case WW3D_TS_WORLD:
		if (render_state_changed&WORLD_IDENTITY) m.Make_Identity();
		else m=render_state.world.Transpose();
		break;
	case WW3D_TS_VIEW:
		if (render_state_changed&VIEW_IDENTITY) m.Make_Identity();
		else m=render_state.view.Transpose();
		break;
	default:
		Backend->Get_Transform(transform,static_cast<WW3DMatrixRaw*>(&mat));
		m=*reinterpret_cast<Matrix4x4*>(&mat);
		m=m.Transpose();
		break;
	}
}

void RenderDevice::_Copy_Rects(
  WW3DSurfaceHandle pSourceSurface,
  CONST RECT* pSourceRectsArray,
  UINT cRects,
  WW3DSurfaceHandle pDestinationSurface,
  CONST POINT* pDestPointsArray
)
{
	Backend->Copy_Rects(pSourceSurface, pSourceRectsArray, cRects, pDestinationSurface, pDestPointsArray);
}

// Device state queries for game code.
bool RenderDevice::Has_Device(void)
{
	return D3DDevice != nullptr;
}

bool RenderDevice::Is_Device_Ready(void)
{
	return Get_Device_State() == static_cast<HRESULT>(WW3D_OK);
}

HRESULT RenderDevice::Get_Device_State(void)
{
	if (!Has_Device()) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	return Backend->Get_Device_State();
}

// Buffers, stream source and draw call behind the wrapper.
HRESULT RenderDevice::Create_Device_Vertex_Buffer(unsigned length, unsigned usage, unsigned fvf, unsigned pool, WW3DVertexBufferHandle * out)
{
	HRESULT hr = static_cast<HRESULT>(WW3D_OK);
	hr = Backend->Create_Vertex_Buffer(length, usage, fvf, pool, out);
	Device_ErrorCode(hr);
	return hr;
}

HRESULT RenderDevice::Create_Device_Index_Buffer(unsigned length, unsigned usage, WW3DDeviceFormat format, unsigned pool, WW3DIndexBufferHandle * out)
{
	HRESULT hr = static_cast<HRESULT>(WW3D_OK);
	hr = Backend->Create_Index_Buffer(length, usage, format, pool, out);
	Device_ErrorCode(hr);
	return hr;
}

void RenderDevice::Set_Stream_Source(unsigned stream, WW3DVertexBufferHandle vb, unsigned offset, unsigned stride)
{
	Backend->Set_Stream_Source(stream, vb, offset, stride);
}

// Resource management priority of a texture.
unsigned RenderDevice::Get_Texture_Priority(WW3DTextureHandle texture)
{
	return Backend->Get_Texture_Priority(texture);
}

unsigned RenderDevice::Set_Texture_Priority(WW3DTextureHandle texture, unsigned priority)
{
	return Backend->Set_Texture_Priority(texture, priority);
}

// Hardware mouse cursor (W3DMouse.cpp).
BOOL RenderDevice::Show_Cursor(BOOL show)
{
	if (!Has_Device()) return FALSE;
	return Backend->Show_Cursor(show);
}

HRESULT RenderDevice::Set_Cursor_Properties(UINT hot_x, UINT hot_y, WW3DSurfaceHandle bitmap)
{
	if (!Has_Device()) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	return Backend->Set_Cursor_Properties(hot_x, hot_y, bitmap);
}

void RenderDevice::Set_Cursor_Position(int x, int y, DWORD flags)
{
	if (!Has_Device()) return;
	Backend->Set_Cursor_Position(x, y, flags);
}

// Resource access behind the wrapper. The backend fills WW3DSurfaceDesc/WW3DLockedRect.
HRESULT RenderDevice::Get_Texture_Surface_Level(WW3DTextureHandle texture, unsigned level, WW3DSurfaceHandle * out)
{
	if (!texture || !out) return E_INVALIDARG;
	return Backend->Get_Texture_Surface_Level(texture, level, out);
}

HRESULT RenderDevice::Get_Texture_Level_Desc(WW3DTextureHandle texture, unsigned level, WW3DSurfaceDesc* desc)
{
	if (!texture || !desc) return E_INVALIDARG;
	return Backend->Get_Texture_Level_Desc(texture, level, desc);
}

HRESULT RenderDevice::Get_Surface_Desc(WW3DSurfaceHandle surface, WW3DSurfaceDesc* desc)
{
	if (!surface || !desc) return E_INVALIDARG;
	return Backend->Get_Surface_Desc(surface, desc);
}

HRESULT RenderDevice::Lock_Surface(WW3DSurfaceHandle surface, WW3DLockedRect* out, const RECT* rect, unsigned flags)
{
	if (!surface || !out) return E_INVALIDARG;
	return Backend->Lock_Surface(surface, out, rect, flags);
}

HRESULT RenderDevice::Unlock_Surface(WW3DSurfaceHandle surface)
{
	if (!surface) return E_INVALIDARG;
	return Backend->Unlock_Surface(surface);
}

HRESULT RenderDevice::Lock_Texture(WW3DTextureHandle texture, unsigned level, WW3DLockedRect* out, const RECT* rect, unsigned flags)
{
	if (!texture || !out) return E_INVALIDARG;
	return Backend->Lock_Texture(texture, level, out, rect, flags);
}

HRESULT RenderDevice::Unlock_Texture(WW3DTextureHandle texture, unsigned level)
{
	if (!texture) return E_INVALIDARG;
	return Backend->Unlock_Texture(texture, level);
}

unsigned RenderDevice::Get_Texture_Level_Count(WW3DTextureHandle texture)
{
	return texture ? Backend->Get_Texture_Level_Count(texture) : 0;
}

WW3DTextureHandle RenderDevice::Create_Team_Color_View(WW3DTextureHandle texture, unsigned color)
{
	return (texture && Backend) ? Backend->Create_Team_Color_View(texture, color) : nullptr;
}

void RenderDevice::Set_Texture_LOD(WW3DTextureHandle texture, unsigned lod)
{
	if (!texture) return;
	Backend->Set_Texture_LOD(texture, lod);
}

// Reference counting and locking for the opaque handles, implemented in the backend.
// The Backend check only covers the time before Init. After Shutdown the backend object stays
// alive (see Shutdown) so subsystems torn down later can still release their textures here;
// otherwise live resources remain and exit hangs.
void RenderDevice::Add_Ref_Resource(WW3DTextureHandle texture)   { if (Backend && texture) Backend->Add_Ref_Resource(texture); }
void RenderDevice::Add_Ref_Resource(WW3DSurfaceHandle surface)   { if (Backend && surface) Backend->Add_Ref_Resource(surface); }
void RenderDevice::Release_Resource(WW3DTextureHandle texture)   { if (Backend && texture) Backend->Release_Resource(texture); }
void RenderDevice::Release_Resource(WW3DSurfaceHandle surface)   { if (Backend && surface) Backend->Release_Resource(surface); }
void RenderDevice::Release_Resource(WW3DVertexShaderHandle s)    { if (Backend && s) Backend->Release_Resource(s); }
void RenderDevice::Release_Resource(WW3DPixelShaderHandle s)     { if (Backend && s) Backend->Release_Resource(s); }
void RenderDevice::Release_Resource(WW3DVertexDeclHandle d)      { if (Backend && d) Backend->Release_Resource(d); }
void RenderDevice::Release_Resource(WW3DVertexBufferHandle vb)   { if (Backend && vb) Backend->Release_Resource(vb); }
void RenderDevice::Release_Resource(WW3DIndexBufferHandle ib)    { if (Backend && ib) Backend->Release_Resource(ib); }

HRESULT RenderDevice::Lock_Vertex_Buffer(WW3DVertexBufferHandle vb, unsigned offset, unsigned size, void** data, unsigned flags)
{
	if (!vb) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	return Backend->Lock_Vertex_Buffer(vb, offset, size, data, flags);
}

HRESULT RenderDevice::Unlock_Vertex_Buffer(WW3DVertexBufferHandle vb)
{
	if (!vb) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	return Backend->Unlock_Vertex_Buffer(vb);
}

HRESULT RenderDevice::Lock_Index_Buffer(WW3DIndexBufferHandle ib, unsigned offset, unsigned size, void** data, unsigned flags)
{
	if (!ib) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	return Backend->Lock_Index_Buffer(ib, offset, size, data, flags);
}

HRESULT RenderDevice::Unlock_Index_Buffer(WW3DIndexBufferHandle ib)
{
	if (!ib) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	return Backend->Unlock_Index_Buffer(ib);
}

void RenderDevice::Set_Vertex_Shader_Object(WW3DVertexShaderHandle shader)
{
	Backend->Set_Vertex_Shader(shader);
}

void RenderDevice::Set_Pixel_Shader_Object(WW3DPixelShaderHandle shader)
{
	Backend->Set_Pixel_Shader(shader);
}

HRESULT RenderDevice::Get_Render_Target(WW3DSurfaceHandle * out)
{
	if (!out) return E_INVALIDARG;
	*out = nullptr;
	if (!Has_Device()) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	return Backend->Get_Render_Target(out);
}

HRESULT RenderDevice::Create_Device_Offscreen_Surface(unsigned width, unsigned height, WW3DDeviceFormat format, WW3DSurfaceHandle * out)
{
	if (!out) return E_INVALIDARG;
	*out = nullptr;
	if (!Has_Device()) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	*out = Backend->Create_Offscreen_Surface(width, height, format);
	return *out ? static_cast<HRESULT>(WW3D_OK) : static_cast<HRESULT>(E_FAIL);
}

void RenderDevice::Set_Indices(WW3DIndexBufferHandle ib)
{
	// The base vertex index is passed to Draw_Indexed_Primitive.
	Backend->Set_Indices(ib);
}

void RenderDevice::Draw_Indexed_Primitive(WW3DPrimitiveType type, int base_vertex, unsigned min_index,
                                          unsigned num_vertices, unsigned start_index, unsigned prim_count)
{
	WW3D_RECORD_DRAW_CALLS();
	Backend->Draw_Indexed_Primitive(type, base_vertex, min_index, num_vertices, start_index, prim_count);
}

HRESULT RenderDevice::Get_Depth_Stencil_Surface(WW3DSurfaceHandle * out)
{
	if (!out) return E_INVALIDARG;
	*out = nullptr;
	if (!Has_Device()) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	return Backend->Get_Depth_Stencil_Surface(out);
}

HRESULT RenderDevice::Set_Render_Target_Raw(WW3DSurfaceHandle render_target, WW3DSurfaceHandle depth_stencil)
{
	// No Set_Render_Target bookkeeping: just set target and depth buffer. The backend matches
	// multisampling between target and depth buffer.
	if (!Has_Device()) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	return Backend->Set_Render_Target_Surfaces(render_target, depth_stencil);
}

HRESULT RenderDevice::Create_Device_Render_Target_Texture(unsigned width, unsigned height, WW3DDeviceFormat format,
                                                          WW3DTextureHandle * out)
{
	if (!out) return E_INVALIDARG;
	*out = nullptr;
	if (!Has_Device()) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	// One level, no mip chain.
	return Backend->Create_Texture(width, height, 1, WW3D_USAGE_RENDERTARGET, format, WW3D_POOL_DEFAULT, out);
}

bool RenderDevice::Copy_Render_Target_To_Texture(WW3DTextureHandle dest)
{
	if (!dest || !Has_Device()) return false;
	return Backend->Copy_Render_Target_To_Texture(dest);
}

void RenderDevice::Set_Texture_Stage(unsigned stage, const TextureStageDesc& d)
{
	if (d.ColorOp   != WW3D_STAGE_KEEP) Set_Texture_Stage_State(stage, WW3D_TSS_COLOROP,   d.ColorOp);
	if (d.ColorArg1 != WW3D_STAGE_KEEP) Set_Texture_Stage_State(stage, WW3D_TSS_COLORARG1, d.ColorArg1);
	if (d.ColorArg2 != WW3D_STAGE_KEEP) Set_Texture_Stage_State(stage, WW3D_TSS_COLORARG2, d.ColorArg2);
	if (d.ColorArg0 != WW3D_STAGE_KEEP) Set_Texture_Stage_State(stage, WW3D_TSS_COLORARG0, d.ColorArg0);
	if (d.AlphaOp   != WW3D_STAGE_KEEP) Set_Texture_Stage_State(stage, WW3D_TSS_ALPHAOP,   d.AlphaOp);
	if (d.AlphaArg1 != WW3D_STAGE_KEEP) Set_Texture_Stage_State(stage, WW3D_TSS_ALPHAARG1, d.AlphaArg1);
	if (d.AlphaArg2 != WW3D_STAGE_KEEP) Set_Texture_Stage_State(stage, WW3D_TSS_ALPHAARG2, d.AlphaArg2);
	if (d.AlphaArg0 != WW3D_STAGE_KEEP) Set_Texture_Stage_State(stage, WW3D_TSS_ALPHAARG0, d.AlphaArg0);
	if (d.TexCoordIndex     != WW3D_STAGE_KEEP) Set_Texture_Stage_State(stage, WW3D_TSS_TEXCOORDINDEX,         d.TexCoordIndex);
	if (d.TexTransformFlags != WW3D_STAGE_KEEP) Set_Texture_Stage_State(stage, WW3D_TSS_TEXTURETRANSFORMFLAGS, d.TexTransformFlags);
}

void RenderDevice::Disable_Texture_Stage(unsigned stage)
{
	Set_Texture_Stage_State(stage, WW3D_TSS_COLOROP, WW3D_TOP_DISABLE);
	Set_Texture_Stage_State(stage, WW3D_TSS_ALPHAOP, WW3D_TOP_DISABLE);
}

