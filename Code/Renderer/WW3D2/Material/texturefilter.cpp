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
 *                     $Archive:: ww3d2/texturefilter.cpp												$*
 *                                                                                             *
 *                  $Org Author:: Kenny Mitchell                                              $*
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 08/05/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 1                                                          $*
 *                                                                                             *
 * 08/05/02 KM Texture filter class abstraction																			*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "texturefilter.h"
#include "WW3D2/Common/renderdevice.h"

unsigned _MinTextureFilters[MAX_TEXTURE_STAGES][TextureFilterClass::FILTER_TYPE_COUNT];
unsigned _MagTextureFilters[MAX_TEXTURE_STAGES][TextureFilterClass::FILTER_TYPE_COUNT];
unsigned _MipMapFilters[MAX_TEXTURE_STAGES][TextureFilterClass::FILTER_TYPE_COUNT];

/*************************************************************************
**                             TextureFilterClass
*************************************************************************/
TextureFilterClass::TextureFilterClass(MipCountType mip_level_count)
:	TextureMinFilter(FILTER_TYPE_DEFAULT),
	TextureMagFilter(FILTER_TYPE_DEFAULT),
	UAddressMode(TEXTURE_ADDRESS_REPEAT),
	VAddressMode(TEXTURE_ADDRESS_REPEAT)
{
	if (mip_level_count!=MIP_LEVELS_1)
	{
		MipMapFilter=FILTER_TYPE_DEFAULT;
	}
	else
	{
		MipMapFilter=FILTER_TYPE_NONE;
	}
}

//**********************************************************************************************
//! Apply filters (legacy)
/*!
*/
void TextureFilterClass::Apply(unsigned int stage)
{
	// Texture filtering is sampler state (WW3DSamplerStateType), not texture stage state.
	RenderDevice::Set_Sampler_State(stage,WW3D_SAMP_MINFILTER,_MinTextureFilters[stage][TextureMinFilter]);
	RenderDevice::Set_Sampler_State(stage,WW3D_SAMP_MAGFILTER,_MagTextureFilters[stage][TextureMagFilter]);
	RenderDevice::Set_Sampler_State(stage,WW3D_SAMP_MIPFILTER,_MipMapFilters[stage][MipMapFilter]);

	// Addressing is sampler state as well (WW3D_SAMP_ADDRESSU/V).
	switch (Get_U_Addr_Mode())
	{
	case TEXTURE_ADDRESS_REPEAT:
		RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
		break;

	case TEXTURE_ADDRESS_CLAMP:
		RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_CLAMP);
		break;
	}

	switch (Get_V_Addr_Mode())
	{
	case TEXTURE_ADDRESS_REPEAT:
		RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);
		break;

	case TEXTURE_ADDRESS_CLAMP:
		RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_CLAMP);
		break;
	}
}

//**********************************************************************************************
//! Init filters (legacy)
/*!
*/
void TextureFilterClass::_Init_Filters(TextureFilterMode filter_type)
{
	const unsigned filter_caps = RenderCaps::Get_Texture_Filter_Caps();

   	_MinTextureFilters[0][FILTER_TYPE_NONE]=WW3D_TEXF_POINT;
   	_MagTextureFilters[0][FILTER_TYPE_NONE]=WW3D_TEXF_POINT;
   	_MipMapFilters[0][FILTER_TYPE_NONE]=WW3D_TEXF_NONE;
   
   	_MinTextureFilters[0][FILTER_TYPE_FAST]=WW3D_TEXF_LINEAR;
   	_MagTextureFilters[0][FILTER_TYPE_FAST]=WW3D_TEXF_LINEAR;
   	_MipMapFilters[0][FILTER_TYPE_FAST]=WW3D_TEXF_POINT;
   
   	_MagTextureFilters[0][FILTER_TYPE_BEST]=WW3D_TEXF_POINT;
   	_MinTextureFilters[0][FILTER_TYPE_BEST]=WW3D_TEXF_POINT;
   	_MipMapFilters[0][FILTER_TYPE_BEST]=WW3D_TEXF_POINT;

	if (filter_caps&WW3D_PTFILTERCAPS_MAGFLINEAR) _MagTextureFilters[0][FILTER_TYPE_BEST]=WW3D_TEXF_LINEAR;
	if (filter_caps&WW3D_PTFILTERCAPS_MINFLINEAR) _MinTextureFilters[0][FILTER_TYPE_BEST]=WW3D_TEXF_LINEAR;

	// Set anisotropic filtering only if requested and available
	if (filter_type==TEXTURE_FILTER_ANISOTROPIC) {
		if (filter_caps&WW3D_PTFILTERCAPS_MAGFANISOTROPIC) _MagTextureFilters[0][FILTER_TYPE_BEST]=WW3D_TEXF_ANISOTROPIC;
		if (filter_caps&WW3D_PTFILTERCAPS_MINFANISOTROPIC) _MinTextureFilters[0][FILTER_TYPE_BEST]=WW3D_TEXF_ANISOTROPIC;
	}

	// Set linear mip filter only if requested trilinear or anisotropic, and linear available
	if (filter_type==TEXTURE_FILTER_ANISOTROPIC || filter_type==TEXTURE_FILTER_TRILINEAR) {
		if (filter_caps&WW3D_PTFILTERCAPS_MIPFLINEAR) _MipMapFilters[0][FILTER_TYPE_BEST]=WW3D_TEXF_LINEAR;
	}

	// For stages above zero, set best filter to the same as the stage zero, except if anisotropic
	int i;
	for (i=1;i<MAX_TEXTURE_STAGES;++i) {
/*		_MinTextureFilters[i][FILTER_TYPE_NONE]=WW3D_TEXF_POINT;
		_MagTextureFilters[i][FILTER_TYPE_NONE]=WW3D_TEXF_POINT;
		_MipMapFilters[i][FILTER_TYPE_NONE]=WW3D_TEXF_NONE;

		_MinTextureFilters[i][FILTER_TYPE_FAST]=WW3D_TEXF_LINEAR;
		_MagTextureFilters[i][FILTER_TYPE_FAST]=WW3D_TEXF_LINEAR;
		_MipMapFilters[i][FILTER_TYPE_FAST]=WW3D_TEXF_POINT;

		_MagTextureFilters[i][FILTER_TYPE_BEST]=WW3D_TEXF_LINEAR;
		_MinTextureFilters[i][FILTER_TYPE_BEST]=WW3D_TEXF_LINEAR;
		_MipMapFilters[i][FILTER_TYPE_BEST]=WW3D_TEXF_POINT;
*/
		_MinTextureFilters[i][FILTER_TYPE_NONE]=_MinTextureFilters[i-1][FILTER_TYPE_NONE];
		_MagTextureFilters[i][FILTER_TYPE_NONE]=_MagTextureFilters[i-1][FILTER_TYPE_NONE];
		_MipMapFilters[i][FILTER_TYPE_NONE]=_MipMapFilters[i-1][FILTER_TYPE_NONE];

		_MinTextureFilters[i][FILTER_TYPE_FAST]=_MinTextureFilters[i-1][FILTER_TYPE_FAST];
		_MagTextureFilters[i][FILTER_TYPE_FAST]=_MagTextureFilters[i-1][FILTER_TYPE_FAST];
		_MipMapFilters[i][FILTER_TYPE_FAST]=_MipMapFilters[i-1][FILTER_TYPE_FAST];

		if (_MagTextureFilters[i-1][FILTER_TYPE_BEST]==WW3D_TEXF_ANISOTROPIC) {
			_MagTextureFilters[i][FILTER_TYPE_BEST]=WW3D_TEXF_LINEAR;
		}
		else {
			_MagTextureFilters[i][FILTER_TYPE_BEST]=_MagTextureFilters[i-1][FILTER_TYPE_BEST];
		}

		if (_MinTextureFilters[i-1][FILTER_TYPE_BEST]==WW3D_TEXF_ANISOTROPIC) {
			_MinTextureFilters[i][FILTER_TYPE_BEST]=WW3D_TEXF_LINEAR;
		}
		else {
			_MinTextureFilters[i][FILTER_TYPE_BEST]=_MinTextureFilters[i-1][FILTER_TYPE_BEST];
		}
		_MipMapFilters[i][FILTER_TYPE_BEST]=_MipMapFilters[i-1][FILTER_TYPE_BEST];


	}

	// 16x anisotropy is practically free on current hardware; clamp to the caps maximum.
	unsigned maxAnisotropy = RenderCaps::Get_Max_Anisotropy();
	if (maxAnisotropy > 16) maxAnisotropy = 16;
	if (maxAnisotropy < 1) maxAnisotropy = 1;

	// Set default to best. The level of best filter mode is controlled by the input parameter.
	for (i=0;i<MAX_TEXTURE_STAGES;++i) {
		_MinTextureFilters[i][FILTER_TYPE_DEFAULT]=_MinTextureFilters[i][FILTER_TYPE_BEST];
		_MagTextureFilters[i][FILTER_TYPE_DEFAULT]=_MagTextureFilters[i][FILTER_TYPE_BEST];
		_MipMapFilters[i][FILTER_TYPE_DEFAULT]=_MipMapFilters[i][FILTER_TYPE_BEST];

		RenderDevice::Set_Sampler_State(i,WW3D_SAMP_MAXANISOTROPY,maxAnisotropy);
	}

}


//**********************************************************************************************
//! Set mip mapping filter (legacy)
/*!
*/
void TextureFilterClass::Set_Mip_Mapping(FilterType mipmap)
{
//	if (mipmap != FILTER_TYPE_NONE && Get_Mip_Level_Count() <= 1 && Is_Initialized()) 
//	{
//		WWASSERT_PRINT(0, "Trying to enable MipMapping on texture w/o Mip levels!\n");
//		return;
//	}
	MipMapFilter=mipmap;
}

//**********************************************************************************************
//! Set default min filter (legacy)
/*!
*/
void TextureFilterClass::_Set_Default_Min_Filter(FilterType filter)
{
	for (int i=0;i<MAX_TEXTURE_STAGES;++i) 
	{
		_MinTextureFilters[i][FILTER_TYPE_DEFAULT]=_MinTextureFilters[i][filter];
	}
}


//**********************************************************************************************
//! Set default mag filter (legacy)
/*!
*/
void TextureFilterClass::_Set_Default_Mag_Filter(FilterType filter)
{
	for (int i=0;i<MAX_TEXTURE_STAGES;++i) 
	{
		_MagTextureFilters[i][FILTER_TYPE_DEFAULT]=_MagTextureFilters[i][filter];
	}
}

//**********************************************************************************************
//! Set default mip filter (legacy)
/*!
*/
void TextureFilterClass::_Set_Default_Mip_Filter(FilterType filter)
{
	for (int i=0;i<MAX_TEXTURE_STAGES;++i) 
	{
		_MipMapFilters[i][FILTER_TYPE_DEFAULT]=_MipMapFilters[i][filter];
	}
}
