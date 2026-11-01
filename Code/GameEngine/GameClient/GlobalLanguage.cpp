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

// FILE: GlobalLanguage.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Aug 2002
//
//	Filename: 	GlobalLanguage.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	Contains the member functions for the language munkee
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"

#include "Common/INI.h"
#include "Common/InstallInfo.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Display.h"	// layout scale for adjustFontSize
#include "Common/FileSystem.h"

//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

static const FieldParse TheGlobalLanguageDataFieldParseTable[] = 
{
	{ "UnicodeFontName",									INI::parseAsciiString,nullptr,									offsetof( GlobalLanguage, m_unicodeFontName ) },
	//{	"UnicodeFontFileName",							INI::parseAsciiString,NULL,									offsetof( GlobalLanguage, m_unicodeFontFileName ) },
	{ "LocalFontFile",										GlobalLanguage::parseFontFileName,					nullptr,			0},
	{ "MilitaryCaptionSpeed",						INI::parseInt,					nullptr,		offsetof( GlobalLanguage, m_militaryCaptionSpeed ) },
	{ "UseHardWordWrap",						INI::parseBool,					nullptr,		offsetof( GlobalLanguage, m_useHardWrap) },
	{ "ResolutionFontAdjustment",						INI::parseReal,					nullptr,		offsetof( GlobalLanguage, m_resolutionFontSizeAdjustment) },
	
	{ "CopyrightFont",					GlobalLanguage::parseFontDesc,	nullptr,	offsetof( GlobalLanguage, m_copyrightFont ) },
	{ "MessageFont",					GlobalLanguage::parseFontDesc,	nullptr,	offsetof( GlobalLanguage, m_messageFont) },					
	{ "MilitaryCaptionTitleFont",		GlobalLanguage::parseFontDesc,	nullptr,	offsetof( GlobalLanguage, m_militaryCaptionTitleFont) },
	{ "MilitaryCaptionDelayMS",					INI::parseInt,					nullptr,		offsetof( GlobalLanguage, m_militaryCaptionDelayMS ) },
	{ "MilitaryCaptionFont",			GlobalLanguage::parseFontDesc,	nullptr,	offsetof( GlobalLanguage, m_militaryCaptionFont) },
	{ "SuperweaponCountdownNormalFont",	GlobalLanguage::parseFontDesc,	nullptr,	offsetof( GlobalLanguage, m_superweaponCountdownNormalFont) },
	{ "SuperweaponCountdownReadyFont",	GlobalLanguage::parseFontDesc,	nullptr,	offsetof( GlobalLanguage, m_superweaponCountdownReadyFont) },
	{ "NamedTimerCountdownNormalFont",	GlobalLanguage::parseFontDesc,	nullptr,	offsetof( GlobalLanguage, m_namedTimerCountdownNormalFont) },
	{ "NamedTimerCountdownReadyFont",	GlobalLanguage::parseFontDesc,	nullptr,	offsetof( GlobalLanguage, m_namedTimerCountdownReadyFont) },
	{ "DrawableCaptionFont",			GlobalLanguage::parseFontDesc,	nullptr,	offsetof( GlobalLanguage, m_drawableCaptionFont) },
	{ "DefaultWindowFont",				GlobalLanguage::parseFontDesc,	nullptr,	offsetof( GlobalLanguage, m_defaultWindowFont) },
	{ "DefaultDisplayStringFont",		GlobalLanguage::parseFontDesc,	nullptr,	offsetof( GlobalLanguage, m_defaultDisplayStringFont) },
	{ "TooltipFontName",				GlobalLanguage::parseFontDesc,	nullptr,	offsetof( GlobalLanguage, m_tooltipFontName) },
	{ "NativeDebugDisplay",				GlobalLanguage::parseFontDesc,	nullptr,	offsetof( GlobalLanguage, m_nativeDebugDisplay) },
	{ "DrawGroupInfoFont",				GlobalLanguage::parseFontDesc,	nullptr,	offsetof( GlobalLanguage, m_drawGroupInfoFont) },
	{ "CreditsTitleFont",				GlobalLanguage::parseFontDesc,	nullptr,	offsetof( GlobalLanguage, m_creditsTitleFont) },
	{ "CreditsMinorTitleFont",				GlobalLanguage::parseFontDesc,	nullptr,	offsetof( GlobalLanguage, m_creditsPositionFont) },
	{ "CreditsNormalFont",				GlobalLanguage::parseFontDesc,	nullptr,	offsetof( GlobalLanguage, m_creditsNormalFont) },

	{ nullptr,					nullptr,						nullptr,						0 }  // keep this last
};

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
void INI::parseLanguageDefinition( INI *ini )
{
	if( !TheGlobalLanguageData )
	{
		DEBUG_ASSERTCRASH(TheGlobalLanguageData, ("INI::parseLanguageDefinition - TheGlobalLanguage Data is not around, please create it before trying to parse the ini file."));
		return;
	}  // end if
	// parse the ini weapon definition
	ini->initFromINI( TheGlobalLanguageData, TheGlobalLanguageDataFieldParseTable );
}

GlobalLanguage::GlobalLanguage()
{
	m_unicodeFontName.clear();
	//Added By Sadullah Nader
	//Initializations missing and needed
	m_unicodeFontFileName.clear();
	m_unicodeFontName.clear();
	m_militaryCaptionSpeed = 0;
	m_useHardWrap = FALSE;
	m_resolutionFontSizeAdjustment = 0.7f;
	m_militaryCaptionDelayMS = 750;
	//End Add
}

GlobalLanguage::~GlobalLanguage()
{
	StringListIt it = m_localFonts.begin();
	while( it != m_localFonts.end())
	{
		AsciiString font = *it;
		RemoveFontResource(font.str());
		//SendMessage( HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
		++it;
	}
}

void GlobalLanguage::init( void ) 
{

	INI ini;
	AsciiString fname;
	fname.format("Data\\%s\\Language.ini", GetGameLanguage().str());

	// Language9x.ini was only used on Windows 9x. The lookup stays because it keeps the
	// NameKeyGenerator sequence unchanged.
	AsciiString tempName;
	tempName.format("Data\\%s\\Language9x.ini", GetGameLanguage().str());
	TheFileSystem->doesFileExist(tempName.str());

	ini.load( fname, INILoadType::INI_LOAD_OVERWRITE, nullptr );
	StringListIt it = m_localFonts.begin();
	while( it != m_localFonts.end())
	{
		AsciiString font = *it;
		if(AddFontResource(font.str()) == 0)
		{
			DEBUG_ASSERTCRASH(FALSE,("GlobalLanguage::init Failed to add font %s", font.str()));
		}
		else
		{
			//SendMessage( HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
		}
		++it;
	}

	
}
void GlobalLanguage::reset( void ) {}


void GlobalLanguage::parseFontDesc(INI *ini, void *instance, void *store, const void* userData)
{
	FontDesc *fontDesc = static_cast<FontDesc *>(store);
	fontDesc->name = ini->getNextQuotedAsciiString();
	fontDesc->size = ini->scanInt(ini->getNextToken());
	fontDesc->bold = ini->scanBool(ini->getNextToken());
}

void GlobalLanguage::parseFontFileName( INI *ini, void * instance, void *store, const void* userData )
{
	GlobalLanguage *monkey = static_cast<GlobalLanguage *>(instance);
	AsciiString asciiString = ini->getNextAsciiString();
	monkey->m_localFonts.push_front(asciiString);
}	 

Int GlobalLanguage::adjustFontSize(Int theFontSize)
{
	// The 800x600 layout is scaled uniformly by height on wide displays, so fonts must follow the same
	// factor; scaling with the width (xResolution/800) would make them too large for the windows. At
	// 4:3/5:4 the layout factor equals xResolution/800.
	Real adjustFactor = TheGlobalData->m_xResolution/800.0f;
	if (TheDisplay && TheDisplay->getWidth() > 0)
	{
		Real sx, sy;
		Int ox, oy;
		TheDisplay->getUILayoutTransform(800, 600, &sx, &sy, &ox, &oy);
		adjustFactor = sx;
	}
	adjustFactor = 1.0f + (adjustFactor-1.0f) * m_resolutionFontSizeAdjustment;
	if (adjustFactor<1.0f) adjustFactor = 1.0f;
	// Cap at 4.0 so fonts keep their size relative to the windows at high resolutions (e.g. factor 3.6
	// at 2160 pixels height). Fonts are rasterized at target size at runtime; the glyph textures hold
	// lines up to 255 pixels.
	if (adjustFactor>4.0f) adjustFactor = 4.0f;
	Int pointSize = REAL_TO_INT_FLOOR(theFontSize*adjustFactor);
	return pointSize;
}

FontDesc::FontDesc(void)
{
	name = "Arial Unicode MS";	///<name of font
	size = 12;			///<point size
	bold = FALSE;			///<is bold?
}
//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

