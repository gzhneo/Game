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

// FILE: Water.h //////////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, December 2001
// Desc:   Water settings
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __WATER_H_
#define __WATER_H_

// INLCLUDES //////////////////////////////////////////////////////////////////////////////////////
#include "Common/GameType.h"
#include "Common/Overridable.h"
#include "Common/Override.h"

//-------------------------------------------------------------------------------------------------
struct FieldParse;

//-------------------------------------------------------------------------------------------------
/** This structures keeps the settings for how our water will look */
//-------------------------------------------------------------------------------------------------
class WaterSetting
{

public:

	WaterSetting( void );
	virtual ~WaterSetting( void );

	/// Get the INI parsing table for loading
	const FieldParse *getFieldParse( void ) { return m_waterSettingFieldParseTable; }

	static const FieldParse m_waterSettingFieldParseTable[];		///< the parse table for INI definition
	AsciiString m_skyTextureFile;
	AsciiString m_waterTextureFile;
	Int m_waterRepeatCount;
	Real m_skyTexelsPerUnit;	//texel density of sky plane (higher value repeats texture more).
	RGBAColorInt m_vertex00Diffuse;
	RGBAColorInt m_vertex10Diffuse;
	RGBAColorInt m_vertex11Diffuse;
	RGBAColorInt m_vertex01Diffuse;
	RGBAColorInt m_waterDiffuseColor;
	RGBAColorInt m_transparentWaterDiffuse;
	Real m_uScrollPerMs;		
	Real m_vScrollPerMs;

};

//-------------------------------------------------------------------------------------------------
/** This structure keeps the transparency and vertex settings, which are the same regardless of the
		time of day. They can be overridden on a per-map basis. */
//-------------------------------------------------------------------------------------------------
class WaterTransparencySetting : public Overridable
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( WaterTransparencySetting, "WaterTransparencySetting"  )

	public:
		Real m_transparentWaterDepth;
		Real m_minWaterOpacity;
		RGBColor m_standingWaterColor;
		RGBColor m_radarColor;
		Bool m_additiveBlend;
		AsciiString m_standingWaterTexture;
		
		AsciiString m_skyboxTextureN;
		AsciiString m_skyboxTextureE;
		AsciiString m_skyboxTextureS;
		AsciiString m_skyboxTextureW;
		AsciiString m_skyboxTextureT;

		// Settings of the realistic water (detail level WaterQuality >= 1). Like the values above they can
		// be overridden per map in map.ini. The original game data do not set them, so the constructor
		// defaults apply.
		Real m_turbidityDepth;			///< visibility depth in world units; 0 = classic water on this map
		RGBColor m_turbidityColor;		///< transmission per color channel after m_turbidityDepth (tint of the ground)
		Real m_refractionStrength;		///< refraction strength (1 = default, 0 = off)
		Real m_reflectionStrength;		///< Fresnel sky reflection strength (1 = default, 0 = off)

	public:
		WaterTransparencySetting()
		{
			m_transparentWaterDepth = 3.0f;
			m_minWaterOpacity = 1.0f;
			m_standingWaterColor.red = 1.0f;
			m_standingWaterColor.green = 1.0f;
			m_standingWaterColor.blue = 1.0f;
			// RGBColor is in the range 0..1 as in INI::parseRGBColor. The Zero Hour Water.ini sets
			// RadarWaterColor, the Generals Water.ini does not, so this default is used there.
			m_radarColor.red = 140.0f / 255.0f;
			m_radarColor.green = 140.0f / 255.0f;
			m_radarColor.blue = 1.0f;
			m_standingWaterTexture = "TWWater01.tga";
			m_additiveBlend = FALSE;

			m_skyboxTextureN = "TSMorningN.tga";
			m_skyboxTextureE = "TSMorningE.tga";
			m_skyboxTextureS = "TSMorningS.tga";
			m_skyboxTextureW = "TSMorningW.tga";
			m_skyboxTextureT = "TSMorningT.tga";

			// defaults for a turbid inland water
			m_turbidityDepth = 10.0f;
			m_turbidityColor.red = 70.0f / 255.0f;
			m_turbidityColor.green = 115.0f / 255.0f;
			m_turbidityColor.blue = 120.0f / 255.0f;
			m_refractionStrength = 1.0f;
			m_reflectionStrength = 1.0f;
		}

		static const FieldParse m_waterTransparencySettingFieldParseTable[];		///< the parse table for INI definition

		/// Get the INI parsing table for loading
		const FieldParse *getFieldParse( void ) const { return m_waterTransparencySettingFieldParseTable; }
};

EMPTY_DTOR(WaterTransparencySetting)

// EXTERNAL ///////////////////////////////////////////////////////////////////////////////////////
extern WaterSetting WaterSettings[ TIME_OF_DAY_COUNT ];

extern OVERRIDE<WaterTransparencySetting> TheWaterTransparency;

#endif // __WATER_H_

