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
 *                 Project Name : dx8 caps                                                     *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/rendercaps.cpp                            $*
 *                                                                                             *
 *              Original Author:: Hector Yee                                                   *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 31                                                          $*
 *                                                                                             *
 * 06/27/02 KM Z Format support																						*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "always.h"
#include "rendercaps.h"
#include "renderdevice.h"

// The D3D11 backend sets the capabilities as fixed values via Set_Fixed_Caps/Set_Texture_Format_Supported
// (feature level 11.0 guarantees everything the engine queries); the queries below read these fields.

// Static fields -- there is exactly one active device.
bool RenderCaps::SupportTnL;
bool RenderCaps::SupportDXTC;
bool RenderCaps::supportGamma;
bool RenderCaps::SupportNPatches;
bool RenderCaps::SupportBumpEnvmap;
bool RenderCaps::SupportBumpEnvmapLuminance;
bool RenderCaps::SupportTextureFormat[WW3D_FORMAT_COUNT];
bool RenderCaps::SupportRenderToTextureFormat[WW3D_FORMAT_COUNT];
bool RenderCaps::SupportDepthStencilFormat[WW3D_ZFORMAT_COUNT];
bool RenderCaps::SupportZBias;
bool RenderCaps::SupportAnisotropicFiltering;
unsigned RenderCaps::MaxAnisotropy;
bool RenderCaps::SupportModAlphaAddClr;
bool RenderCaps::SupportPointSprites;
bool RenderCaps::SupportCubemaps;
bool RenderCaps::CanDoMultiPass;
bool RenderCaps::IsFogAllowed;
int RenderCaps::MaxTexturesPerPass;
int RenderCaps::VertexShaderVersion;
int RenderCaps::PixelShaderVersion;
int RenderCaps::MaxSimultaneousTextures;
unsigned RenderCaps::TextureOpCaps;
unsigned RenderCaps::PrimitiveMiscCaps;
unsigned RenderCaps::RasterCaps;
unsigned RenderCaps::TextureFilterCaps;
unsigned RenderCaps::MaxTextureWidth;
unsigned RenderCaps::MaxTextureHeight;

//Don't really need this but I added this function to free static variables so
//they don't show up in our memory manager as a leak. -MW 7-22-03
// Nothing to free at the moment.
void RenderCaps::Shutdown(void)
{
}

// --- fixed capabilities (Direct3D 11) ------------------------------------------------------------
void RenderCaps::Set_Fixed_Caps(bool dxtc, bool gamma, bool npatches, bool dot3,
							 bool bump_envmap, bool bump_envmap_luminance,
							 int vertex_shader_version, int pixel_shader_version,
							 int max_simultaneous_textures,
							 unsigned max_texture_width, unsigned max_texture_height,
							 unsigned max_anisotropy,
							 bool anisotropic_min, bool anisotropic_mag,
							 bool linear_min, bool linear_mag, bool linear_mip,
							 bool fog_range,
							 unsigned texture_op_caps, unsigned primitive_misc_caps)
{
	TextureOpCaps          = texture_op_caps;
	PrimitiveMiscCaps      = primitive_misc_caps;
	RasterCaps             = WW3D_PRASTERCAPS_DEPTHBIAS | (fog_range ? WW3D_PRASTERCAPS_FOGRANGE : 0);
	TextureFilterCaps      = WW3D_PTFILTERCAPS_MINFPOINT | WW3D_PTFILTERCAPS_MAGFPOINT | WW3D_PTFILTERCAPS_MIPFPOINT
							| (linear_min ? WW3D_PTFILTERCAPS_MINFLINEAR : 0)
							| (linear_mag ? WW3D_PTFILTERCAPS_MAGFLINEAR : 0)
							| (linear_mip ? WW3D_PTFILTERCAPS_MIPFLINEAR : 0)
							| (anisotropic_min ? WW3D_PTFILTERCAPS_MINFANISOTROPIC : 0)
							| (anisotropic_mag ? WW3D_PTFILTERCAPS_MAGFANISOTROPIC : 0);
	MaxTextureWidth        = max_texture_width;
	MaxTextureHeight       = max_texture_height;

	(void)dot3;
	SupportTnL                  = true;
	SupportDXTC                 = dxtc;
	supportGamma                = gamma;
	SupportNPatches             = npatches;
	SupportBumpEnvmap           = bump_envmap;
	SupportBumpEnvmapLuminance  = bump_envmap_luminance;
	SupportZBias                = true;
	SupportAnisotropicFiltering = anisotropic_min && anisotropic_mag;
	MaxAnisotropy               = max_anisotropy;
	SupportModAlphaAddClr       = (texture_op_caps & WW3D_TEXOPCAPS_MODULATEALPHA_ADDCOLOR) != 0;
	// The backend has no point sprites and no cube maps. W3DSnow draws quads; the water reflection
	// works without a cube map.
	SupportPointSprites         = false;
	SupportCubemaps             = false;
	CanDoMultiPass              = true;
	IsFogAllowed                = true;
	MaxTexturesPerPass          = max_simultaneous_textures;
	VertexShaderVersion         = vertex_shader_version;
	PixelShaderVersion          = pixel_shader_version;
	MaxSimultaneousTextures     = max_simultaneous_textures;

	// Depth buffers: the backend only creates D24S8 and maps the others onto it.
	for (int i = 0; i < WW3D_ZFORMAT_COUNT; ++i) SupportDepthStencilFormat[i] = false;
	SupportDepthStencilFormat[WW3D_ZFORMAT_D24S8] = true;
	SupportDepthStencilFormat[WW3D_ZFORMAT_D24X8] = true;
	SupportDepthStencilFormat[WW3D_ZFORMAT_D16]   = true;
}

void RenderCaps::Set_Texture_Format_Supported(WW3DFormat format, bool supported)
{
	if (format < WW3D_FORMAT_COUNT) {
		SupportTextureFormat[format] = supported;
		// Every supported format also works as a render target (scene target, shadow textures).
		SupportRenderToTextureFormat[format] = supported;
	}
}

// --- capability queries used by the rest of the engine -------------------------------------------
unsigned RenderCaps::Get_Texture_Op_Caps()     { return TextureOpCaps; }
unsigned RenderCaps::Get_Primitive_Misc_Caps() { return PrimitiveMiscCaps; }
unsigned RenderCaps::Get_Raster_Caps()         { return RasterCaps; }
unsigned RenderCaps::Get_Texture_Filter_Caps() { return TextureFilterCaps; }
unsigned RenderCaps::Get_Max_Texture_Width()   { return MaxTextureWidth; }
unsigned RenderCaps::Get_Max_Texture_Height()  { return MaxTextureHeight; }
