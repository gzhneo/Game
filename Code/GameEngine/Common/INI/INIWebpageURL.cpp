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

// FILE: INIWebpageURL.cpp /////////////////////////////////////////////////////////////////////////////
// Author: Bryan Cleveland, November 2001
// Desc:   Parsing Webpage URL INI entries
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/INI.h"
#include "Common/InstallInfo.h"
// #include "GameNetwork/WOLBrowser/WebBrowser.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

AsciiString encodeURL(AsciiString source)
{
	if (source.isEmpty())
	{
		return AsciiString::TheEmptyString;
	}

	AsciiString target;
	AsciiString allowedChars = "$-_.+!*'(),\\";
	const char *ptr = source.str();
	while (*ptr)
	{
		if (isalnum(*ptr) || allowedChars.find(*ptr))
		{
			target.concat(*ptr);
		}
		else
		{
			AsciiString tmp;
			target.concat('%');
			tmp.format("%2.2x", (static_cast<int>(*ptr)));
			target.concat(tmp);
		}
		++ptr;
	}

	return target;
}

//-------------------------------------------------------------------------------------------------
/** Parse a "WebpageURL <Name> ... End" INI block. Its consumer (WOLBrowser, an online-only
	feature) is not part of this build, but real installations' Data\INI\Webpages.ini still
	contains these blocks. Reading only the block name would leave the parser desynced on the
	"URL" line: initFromINI() throws INI_UNKNOWN_TOKEN for any field not in its FieldParse table.
	So the block is parsed into a throwaway local struct. */
//-------------------------------------------------------------------------------------------------
struct DiscardedWebpageURL
{
	AsciiString url;
};
static const FieldParse TheWebpageURLFieldParse[] =
{
	{ "URL",	INI::parseAsciiString,	nullptr,	offsetof( DiscardedWebpageURL, url ) },
	{ nullptr,		nullptr,										nullptr,	0 }  // keep this last
};
void INI::parseWebpageURLDefinition( INI* ini )
{
	// consume the block's name (e.g. "Sample" in "WebpageURL Sample ... End") -- this is what the
	// original buggy version's single getNextToken() call actually did; it just never went on to
	// consume the "URL = ..." field and the closing "End" below.
	ini->getNextToken();

	DiscardedWebpageURL discarded;
	ini->initFromINI( &discarded, TheWebpageURLFieldParse );
}  // end parseWebpageURLDefinition

// Like WebpageURL, "OnlineChatColors" is a data block of the unsupported online features that is
// only read and discarded. The block type is hard-wired in INI.cpp's block table and every real
// installation has it in Data\INI\Multiplayer.ini. An empty FieldParse table is not enough:
// INI::initFromINI throws on every unknown field (INI_UNKNOWN_TOKEN), loading Multiplayer.ini
// fails and the game exits at startup. So all 27 color fields are parsed into a discarded array.
static Color TheDiscardedOnlineChatColors[27];
#define ONLINECHATCOLOR_OFFSET(x) (sizeof(Color) * (x))
static const FieldParse TheOnlineChatColorFieldParse[] =
{
	{ "Default",						INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(0) },
	{ "CurrentRoom",				INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(1) },
	{ "ChatRoom",						INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(2) },
	{ "Game",								INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(3) },
	{ "GameFull",						INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(4) },
	{ "GameCRCMismatch",		INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(5) },
	{ "PlayerNormal",				INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(6) },
	{ "PlayerOwner",				INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(7) },
	{ "PlayerBuddy",				INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(8) },
	{ "PlayerSelf",					INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(9) },
	{ "PlayerIgnored",			INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(10) },
	{ "ChatNormal",					INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(11) },
	{ "ChatEmote",					INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(12) },
	{ "ChatOwner",					INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(13) },
	{ "ChatOwnerEmote",			INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(14) },
	{ "ChatPriv",						INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(15) },
	{ "ChatPrivEmote",			INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(16) },
	{ "ChatPrivOwner",			INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(17) },
	{ "ChatPrivOwnerEmote",	INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(18) },
	{ "ChatBuddy",					INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(19) },
	{ "ChatSelf",						INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(20) },
	{ "AcceptTrue",					INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(21) },
	{ "AcceptFalse",				INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(22) },
	{ "MapSelected",				INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(23) },
	{ "MapUnselected",			INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(24) },
	{ "MOTD",								INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(25) },
	{ "MOTDHeading",				INI::parseColorInt,	nullptr,	ONLINECHATCOLOR_OFFSET(26) },
	{ nullptr,					nullptr,						nullptr,						0 }  // keep this last
};

void INI::parseOnlineChatColorDefinition( INI *ini )
{
	ini->initFromINI( TheDiscardedOnlineChatColors, TheOnlineChatColorFieldParse );
}
