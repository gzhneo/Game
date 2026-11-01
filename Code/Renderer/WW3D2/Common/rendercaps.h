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
 *                 Project Name : DX8 Caps                                                     *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/rendercaps.h                              $*
 *                                                                                             *
 *              Original Author:: Hector Yee                                                   *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 24                                                          $*
 *                                                                                             *
 * 06/27/02 KM Z Format support																						*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#ifndef RENDERCAPS_H
#define RENDERCAPS_H

#include "always.h"
#include "ww3dformat.h"
#include "ww3dtypes.h"



// Capabilities of the one active device, hence static. The D3D11 backend sets the fields as
// fixed values (Set_Fixed_Caps).
class RenderCaps
{
public:
	static void Shutdown(void);

	static bool Support_TnL()						{ return SupportTnL; }
	static bool Support_DXTC()						{ return SupportDXTC; }
	static bool Support_Gamma()						{ return supportGamma; }
	static bool Support_NPatches()					{ return SupportNPatches; }
	static bool Support_Bump_Envmap()				{ return SupportBumpEnvmap; }
	static bool Support_Bump_Envmap_Luminance()		{ return SupportBumpEnvmapLuminance; }
	static bool Support_ZBias()						{ return SupportZBias; }
	static bool Support_Anisotropic_Filtering()		{ return SupportAnisotropicFiltering; }
	static unsigned Get_Max_Anisotropy()			{ return MaxAnisotropy; }
	static bool Support_ModAlphaAddClr()			{ return SupportModAlphaAddClr; }
	static bool Support_PointSprites()				{ return SupportPointSprites; }
	static bool Support_Cubemaps()					{ return SupportCubemaps; }
	static bool Can_Do_Multi_Pass()					{ return CanDoMultiPass; }
	static bool Is_Fog_Allowed()					{ return IsFogAllowed; }
	static int Get_Max_Textures_Per_Pass()			{ return MaxTexturesPerPass; }

	// -------------------------------------------------------------------------
	//
	// Vertex shader support. Version number is split in major and minor, such that 1.0 would
	// have 1 as major and 0 as minor version number.
	//
	// -------------------------------------------------------------------------

	static int Get_Vertex_Shader_Major_Version()	{ return 0xff&(VertexShaderVersion>>8); }
	static int Get_Vertex_Shader_Minor_Version()	{ return 0xff&(VertexShaderVersion); }
	static int Get_Pixel_Shader_Major_Version()		{ return 0xff&(PixelShaderVersion>>8); }
	static int Get_Pixel_Shader_Minor_Version()		{ return 0xff&(PixelShaderVersion); }
	static int Get_Max_Simultaneous_Textures()		{ return MaxSimultaneousTextures; }

	static bool Support_Texture_Format(WW3DFormat format)			{ return SupportTextureFormat[format]; }
	static bool Support_Render_To_Texture_Format(WW3DFormat format)	{ return SupportRenderToTextureFormat[format]; }
	static bool Support_Depth_Stencil_Format(WW3DZFormat format)	{ return SupportDepthStencilFormat[format]; }

	// The raw capability bits the engine reads.
	static unsigned Get_Texture_Op_Caps();			///< WW3D_TEXOPCAPS_*
	static unsigned Get_Primitive_Misc_Caps();		///< WW3D_PMISCCAPS_*
	static unsigned Get_Raster_Caps();				///< WW3D_PRASTERCAPS_*
	static unsigned Get_Texture_Filter_Caps();		///< WW3D_PTFILTERCAPS_*
	static unsigned Get_Max_Texture_Width();
	static unsigned Get_Max_Texture_Height();

	// Set directly by the backend (feature level 11.0 guarantees the relevant capabilities).
	static void Set_Fixed_Caps(bool dxtc, bool gamma, bool npatches, bool dot3,
							   bool bump_envmap, bool bump_envmap_luminance,
							   int vertex_shader_version, int pixel_shader_version,
							   int max_simultaneous_textures,
							   unsigned max_texture_width, unsigned max_texture_height,
							   unsigned max_anisotropy,
							   bool anisotropic_min, bool anisotropic_mag,
							   bool linear_min, bool linear_mag, bool linear_mip,
							   bool fog_range,
							   unsigned texture_op_caps, unsigned primitive_misc_caps);
	static void Set_Texture_Format_Supported(WW3DFormat format, bool supported);

private:
	static unsigned TextureOpCaps;
	static unsigned PrimitiveMiscCaps;
	static unsigned RasterCaps;
	static unsigned TextureFilterCaps;
	static unsigned MaxTextureWidth;
	static unsigned MaxTextureHeight;
	static bool SupportTnL;
	static bool SupportDXTC;
	static bool supportGamma;
	static bool SupportNPatches;
	static bool SupportBumpEnvmap;
	static bool SupportBumpEnvmapLuminance;
	static bool SupportTextureFormat[WW3D_FORMAT_COUNT];
	static bool SupportRenderToTextureFormat[WW3D_FORMAT_COUNT];
	static bool SupportDepthStencilFormat[WW3D_ZFORMAT_COUNT];
	static bool SupportZBias;
	static bool SupportAnisotropicFiltering;
	static unsigned MaxAnisotropy;
	static bool SupportModAlphaAddClr;
	static bool SupportPointSprites;
	static bool SupportCubemaps;
	static bool CanDoMultiPass;
	static bool IsFogAllowed;
	static int MaxTexturesPerPass;
	static int VertexShaderVersion;
	static int PixelShaderVersion;
	static int MaxSimultaneousTextures;
};


#endif
