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

// FILE: INI.cpp //////////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, November 2001
// Desc:   INI Reader
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "formatinto.h"
#define DEFINE_DEATH_NAMES

#include "Common/INI.h"
#include "Common/INIException.h"

#include "Common/DamageFX.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/GameAudio.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"

#include "GameClient/Anim2D.h"
#include "GameClient/Color.h"
#include "GameClient/FXList.h"
#include "GameClient/GameText.h"
#include "GameClient/Image.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/Armor.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/FPUControl.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Weapon.h"
#include "Lib/SafeString.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

static Xfer *s_xfer = nullptr;

//-------------------------------------------------------------------------------------------------
/** This is the table of data types we can have in INI files.  To add a new data type
	* block make a new entry in this table and add an appropriate parsing function */
//-------------------------------------------------------------------------------------------------
extern void parseReallyLowMHz( INI* ini);		// yeah, so sue me (srj)
struct BlockParse
{
	const char *token;
	INIBlockParse parse;
};
static const BlockParse theTypeTable[] =
{
	{ "AIData",							INI::parseAIDataDefinition },
	{ "Animation",					INI::parseAnim2DDefinition },
	{ "Armor",							INI::parseArmorDefinition },
	{ "AudioEvent",					INI::parseAudioEventDefinition },
	{ "AudioSettings",			INI::parseAudioSettingsDefinition },
	{ "Bridge",							INI::parseTerrainBridgeDefinition },
	{ "Campaign",						INI::parseCampaignDefinition },
 	{ "ChallengeGenerals",				INI::parseChallengeModeDefinition },
	{ "CommandButton",			INI::parseCommandButtonDefinition },
	{ "CommandMap",					INI::parseMetaMapDefinition },
	{ "CommandSet",					INI::parseCommandSetDefinition },
	{ "ControlBarScheme",		INI::parseControlBarSchemeDefinition },
	{ "ControlBarResizer",	INI::parseControlBarResizerDefinition },
	{ "CrateData",					INI::parseCrateTemplateDefinition },
	{ "Credits",						INI::parseCredits},
	{ "WindowTransition",		INI::parseWindowTransitions},
	{ "DamageFX",						INI::parseDamageFXDefinition },
	{ "DialogEvent",				INI::parseDialogDefinition },
	{ "DrawGroupInfo",		INI::parseDrawGroupNumberDefinition },
	{ "EvaEvent",						INI::parseEvaEvent },
	{ "FXList",							INI::parseFXListDefinition },
	{ "GameData",						INI::parseGameDataDefinition },
	{ "InGameUI",						INI::parseInGameUIDefinition },
	{ "Locomotor",					INI::parseLocomotorTemplateDefinition },
	{ "Language",						INI::parseLanguageDefinition },
	{ "MapCache",						INI::parseMapCacheDefinition },
	{ "MapData",						INI::parseMapDataDefinition },
	{ "MappedImage",				INI::parseMappedImageDefinition },
	{ "MiscAudio",					INI::parseMiscAudio},
	{ "Mouse",							INI::parseMouseDefinition },
	{ "MouseCursor",				INI::parseMouseCursorDefinition },
	{ "MultiplayerColor",		INI::parseMultiplayerColorDefinition },
  { "MultiplayerStartingMoneyChoice",		INI::parseMultiplayerStartingMoneyChoiceDefinition },
	{ "OnlineChatColors",		INI::parseOnlineChatColorDefinition },
	{ "MultiplayerSettings",INI::parseMultiplayerSettingsDefinition },
	{ "MusicTrack",					INI::parseMusicTrackDefinition },
	{ "Object",							INI::parseObjectDefinition },
	{ "ObjectCreationList",	INI::parseObjectCreationListDefinition },
	{ "ObjectReskin",				INI::parseObjectReskinDefinition },
	{ "ParticleSystem",			INI::parseParticleSystemDefinition },
	{ "PlayerTemplate",			INI::parsePlayerTemplateDefinition },
	{ "Road",								INI::parseTerrainRoadDefinition },
	{ "Science",						INI::parseScienceDefinition },
	{ "Rank",								INI::parseRankDefinition },
	{ "SpecialPower",				INI::parseSpecialPowerDefinition },
	{ "ShellMenuScheme",		INI::parseShellMenuSchemeDefinition },
	{ "Terrain",						INI::parseTerrainDefinition },
	{ "Upgrade",						INI::parseUpgradeDefinition },
	{ "Video",							INI::parseVideoDefinition },
	{ "WaterSet",						INI::parseWaterSettingDefinition },
	{ "WaterTransparency",	INI::parseWaterTransparencyDefinition},
	{ "Weather",	INI::parseWeatherDefinition},
	{ "Weapon",							INI::parseWeaponTemplateDefinition },
	{ "WebpageURL",					INI::parseWebpageURLDefinition },
	{ "HeaderTemplate",			INI::parseHeaderTemplateDefinition },
	{ "StaticGameLOD",			INI::parseStaticGameLODDefinition },
	{ "DynamicGameLOD",			INI::parseDynamicGameLODDefinition },
	{ "LODPreset",					INI::parseLODPreset },
	{	"BenchProfile",				INI::parseBenchProfile },
	{	"ReallyLowMHz",				parseReallyLowMHz },
	{	"ScriptAction",				ScriptEngine::parseScriptAction },
	{	"ScriptCondition",		ScriptEngine::parseScriptCondition },
	
	{ nullptr,									nullptr },		// keep this last!
};


///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
Bool INI::isValidINIFilename( const char *filename )
{
	if( filename == nullptr )
		return FALSE;

	Int len = static_cast<Int>(strlen( filename ));
	if( len < 3 )
		return FALSE;

	if( filename[ len - 1 ] != 'I' && filename[ len - 1 ] != 'i' )
		return FALSE;

	if( filename[ len - 2 ] != 'N' && filename[ len - 2 ] != 'n' )
		return FALSE;

	if( filename[ len - 3 ] != 'I' && filename[ len - 3 ] != 'i' )
		return FALSE;

	return TRUE;

} 

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
INI::INI( void )
{

	m_file							= nullptr;
  m_readBufferNext=m_readBufferUsed=0;
	m_filename					= "None";
	m_loadType					= INILoadType::INI_LOAD_INVALID;
	m_lineNum						= 0;
	m_seps							= " \n\r\t=";			///< make sure you update m_sepsPercent/m_sepsColon as well
	m_sepsPercent				= " \n\r\t=%%";
	m_sepsColon					= " \n\r\t=:";
	m_sepsQuote					= "\"\n=";				///< stop at " = EOL
	m_blockEndToken			= "END";
	m_endOfFile					= FALSE;
	m_buffer[0]					= 0;
#if defined(_DEBUG)
	m_curBlockStart[0]	= 0;
#endif

}  // end INI

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
INI::~INI( void )
{

}  // end ~INI

//-------------------------------------------------------------------------------------------------
/** Load all INI files in the specified directory (and subdirectories if indicated).
	* If we are to load subdirectories, we will load them *after* we load all the
	* files in the current directory */
//-------------------------------------------------------------------------------------------------
void INI::loadDirectory( AsciiString dirName, Bool subdirs, INILoadType loadType, Xfer *pXfer )
{
	// sanity
	if( dirName.isEmpty() )
		throw INI_INVALID_DIRECTORY;

	try
	{
		FilenameList filenameList;
		dirName.concat('\\');
		TheFileSystem->getFileListInDirectory(dirName, "*.ini", filenameList, TRUE);
		// Load the INI files in the dir now, in a sorted order.  This keeps things the same between machines
		// in a network game.
		FilenameList::const_iterator it = filenameList.begin();
		while (it != filenameList.end())
		{
			AsciiString tempname;
			tempname = (*it).str() + dirName.getLength();

			if ((tempname.find('\\') == nullptr) && (tempname.find('/') == nullptr)) {
				// this file doesn't reside in a subdirectory, load it first.
				load( *it, loadType, pXfer );
			}
			++it;
		}

		it = filenameList.begin();
		while (it != filenameList.end())
		{
			AsciiString tempname;
			tempname = (*it).str() + dirName.getLength();

			if ((tempname.find('\\') != nullptr) || (tempname.find('/') != nullptr)) {
				load( *it, loadType, pXfer );
			}
			++it;
		}
	} 
	catch (...) 
	{
		// propagate the exception
		throw;
	}

}  // end loadDirectory

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void INI::prepFile( AsciiString filename, INILoadType loadType )
{
	// if we have a file open already -- we can't do another one
	if( m_file != nullptr )
	{

		DEBUG_CRASH(( "INI::load, cannot open file '%s', file already open\n", filename.str() ));
		throw INI_FILE_ALREADY_OPEN;

	}  // end if

	// open the file
	m_file = TheFileSystem->openFile(filename.str(), File::READ);
	if( m_file == nullptr )
	{

		DEBUG_CRASH(( "INI::load, cannot open file '%s'\n", filename.str() ));
		throw INI_CANT_OPEN_FILE;

	}  // end if

	m_file = m_file->convertToRAMFile();

	// save our filename
	m_filename = filename;

	// save our load time
	m_loadType = loadType;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void INI::unPrepFile()
{
	// close the file
	m_file->close();
	m_file = nullptr;
  m_readBufferUsed=m_readBufferNext=0;
	m_filename = "None";
	m_loadType = INILoadType::INI_LOAD_INVALID;
	m_lineNum = 0;
	m_endOfFile = FALSE;
	s_xfer = nullptr;
}

//-------------------------------------------------------------------------------------------------
static INIBlockParse findBlockParse(const char* token)
{
	for (const BlockParse* parse = theTypeTable; parse->token; ++parse)
	{
		if (strcmp( parse->token, token ) == 0)
		{
			return parse->parse;
		}
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
static INIFieldParseProc findFieldParse(const FieldParse* parseTable, const char* token, int& offset, const void*& userData)
{
	const FieldParse* parse;
	for (parse = parseTable; parse->token; ++parse)
	{
		if (strcmp( parse->token, token ) == 0)
		{
			offset = parse->offset;
			userData = parse->userData;
			return parse->parse;
		}
	}

	if (!parse->token && parse->parse) 
	{
		offset = parse->offset;
		userData = token;
		return parse->parse;
	}
	else
	{
		return nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
/** Load and parse an INI file */
//-------------------------------------------------------------------------------------------------
void INI::load( AsciiString filename, INILoadType loadType, Xfer *pXfer )
{
	setFPMode(); // so we have consistent Real values for GameLogic -MDC

	s_xfer = pXfer;
	prepFile(filename, loadType);

	try
	{

		// read all lines in the file
		DEBUG_ASSERTCRASH( m_endOfFile == FALSE, ("INI::load, EOF at the beginning!\n") );
		while( m_endOfFile == FALSE )
		{
			// read this line
			readLine();

			AsciiString currentLine = m_buffer;

			// the first word is the type of data we're processing
			const char *token = strtok( m_buffer, m_seps );
			if( token )
			{
				INIBlockParse parse = findBlockParse(token);
				if (parse)
				{
					#if defined(_DEBUG)
					safe_strcpy(m_curBlockStart, m_buffer);
					#endif
					try {
						(*parse)( this );

					} catch (...) {
						DEBUG_CRASH(("Error parsing block '%s' in INI file '%s'\n", token, m_filename.str()) );
						char buff[1024];
						WWFormat::Into(buff, "Error parsing INI file '{}' (Line: '{}')\n", m_filename.str(), currentLine.str());

						throw INIException(buff);
					}
					#if defined(_DEBUG)
						safe_strcpy(m_curBlockStart, "NO_BLOCK");
					#endif
				}
				else
				{
					DEBUG_ASSERTCRASH( 0, ("[LINE: %d - FILE: '%s'] Unknown block '%s'\n",
														 getLineNum(), getFilename().str(), token ) );
					throw INI_UNKNOWN_TOKEN;
				}
				
			}  // end if 
				
		}  // end while
	}
	catch (...)
	{
		unPrepFile();

		// propagate the exception.
		throw;
	}

	unPrepFile();

}  // end load

//-------------------------------------------------------------------------------------------------
/** Read a line from the already open file.  Any comments will be remved and
	* therefore ignored from any given line */
//-------------------------------------------------------------------------------------------------
void INI::readLine( void )
{
	// sanity
	DEBUG_ASSERTCRASH( m_file, ("readLine(), file pointer is NULL\n") );

  if (m_endOfFile)
    *m_buffer=0;
  else
  {
    char *p=m_buffer;
    while (p!=m_buffer+INI_MAX_CHARS_PER_LINE)
    {
      // get next character
      if (m_readBufferNext==m_readBufferUsed)
      {
        // refill buffer
        m_readBufferNext=0;
        m_readBufferUsed=m_file->read(m_readBuffer,INI_READ_BUFFER);

        // EOF?
        if (!m_readBufferUsed)
        {
          m_endOfFile=true;
          *p=0;
          break;
        }
      }
      *p=m_readBuffer[m_readBufferNext++];

      // CR?
      if (*p=='\n')
      {
        *p=0;
        break;
      }

      DEBUG_ASSERTCRASH(*p != '\t', ("tab characters are not allowed in INI files (%s). please check your editor settings. Line Number %d\n",m_filename.str(), getLineNum()));

      // comment?
      if (*p==';')
        *p=0;
      // whitespace?
      else if (*p>0&&*p<32)
        *p=' ';
      p++;
    }
    *p=0;

		// increase our line count
		m_lineNum++;

		// check for at the max
		if ( p == m_buffer+INI_MAX_CHARS_PER_LINE )
		{

			DEBUG_ASSERTCRASH( 0, ("Buffer too small (%d) and was truncated, increase INI_MAX_CHARS_PER_LINE\n", 
														 INI_MAX_CHARS_PER_LINE) );

		}  // end if
  }

	if (s_xfer)
	{
		s_xfer->xferUser( m_buffer, static_cast<Int>(sizeof( char ) * strlen( m_buffer )) );
		//DEBUG_LOG(("Xfer val is now 0x%8.8X in %s, line %s\n", ((XferCRC *)s_xfer)->getCRC(),
			//m_filename.str(), m_buffer));
	}
}

//-------------------------------------------------------------------------------------------------
/** Parse UnsignedByte from buffer and assign at location 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseUnsignedByte( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();
	Int value = scanInt(token);
	if (value < 0 || value > 255)
	{
		DEBUG_CRASH(("Bad value INI::parseUnsignedByte"));
		throw ERROR_BUG;
	}
	*static_cast<Byte *>(store) = static_cast<Byte>(value);
} 

//-------------------------------------------------------------------------------------------------
/** Parse signed short from buffer and assign at location 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseShort( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();
	Int value = scanInt(token);
	if (value < -32768 || value > 32767)
	{
		DEBUG_CRASH(("Bad value INI::parseShort"));
		throw ERROR_BUG;
	}
	*static_cast<Short *>(store) = static_cast<Short>(value);
} 

//-------------------------------------------------------------------------------------------------
/** Parse unsigned short from buffer and assign at location 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseUnsignedShort( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();
	Int value = scanInt(token);
	if (value < 0 || value > 65535)
	{
		DEBUG_CRASH(("Bad value INI::parseUnsignedShort"));
		throw ERROR_BUG;
	}
	*static_cast<UnsignedShort *>(store) = static_cast<UnsignedShort>(value);
} 

//-------------------------------------------------------------------------------------------------
/** Parse integer from buffer and assign at location 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseInt( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();
	*static_cast<Int *>(store) = scanInt(token);

} 

//-------------------------------------------------------------------------------------------------
/** Parse unsigned integer from buffer and assign at location 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseUnsignedInt( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();
	*static_cast<UnsignedInt *>(store) = scanUnsignedInt(token);

}

//-------------------------------------------------------------------------------------------------
/** Parse real from buffer and assign at location 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseReal( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();
	*static_cast<Real *>(store) = scanReal(token);

} 

//-------------------------------------------------------------------------------------------------
/** Parse real from buffer and assign at location 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parsePositiveNonZeroReal( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();
	*static_cast<Real *>(store) = scanReal(token);
	if (*static_cast<Real *>(store) <= 0.0f)
	{
		DEBUG_CRASH(("invalid Real value %f -- expected > 0\n",*(Real*)store));
		throw INI_INVALID_DATA;
	}

}

//-------------------------------------------------------------------------------------------------
/** Parse a degree value (0 to 360) and store the radian value of that degree
	* in a Real */
//-------------------------------------------------------------------------------------------------
void INI::parseAngleReal( INI *ini, void * /*instance*/, 
																			void *store, const void *userData )
{
	const char *token = ini->getNextToken();

	const Real RADS_PER_DEGREE = PI / 180.0f;
	*static_cast<Real *>(store) = scanReal( token ) * RADS_PER_DEGREE;

}

//-------------------------------------------------------------------------------------------------
/** Parse an angular velocity in degrees-per-sec and store the rads-per-frame value of that degree
	* in a Real */
//-------------------------------------------------------------------------------------------------
void INI::parseAngularVelocityReal( INI *ini, void * /*instance*/, 
																			void *store, const void *userData )
{
	const char *token = ini->getNextToken();

	// scan the int and convert to radian and store as a real
	*static_cast<Real *>(store) = ConvertAngularVelocityInDegreesPerSecToRadsPerFrame(scanReal( token ));

}

//-------------------------------------------------------------------------------------------------
/** Parse Bool from buffer and assign at location 'store'.  The buffer token must
	* be in the form of a string "Yes" or "No" (case is ignored) */
//-------------------------------------------------------------------------------------------------
void INI::parseBool( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	*static_cast<Bool*>(store) = INI::scanBool(ini->getNextToken());
}

//-------------------------------------------------------------------------------------------------
/** Parse Bool from buffer; if true, or in MASK, otherwise and out MASK. The buffer token must
	* be in the form of a string "Yes" or "No" (case is ignored) */
//-------------------------------------------------------------------------------------------------
void INI::parseBitInInt32( INI *ini, void *instance, void *store, const void* userData )
{
	UnsignedInt* s = static_cast<UnsignedInt*>(store);
	UnsignedInt mask = static_cast<UnsignedInt>(reinterpret_cast<size_t>(userData));

	if (INI::scanBool(ini->getNextToken()))
		*s |= mask;
	else
		*s &= ~mask;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/*static*/ Bool INI::scanBool(const char* token)
{
	// translate string yes/no into TRUE/FALSE
	if( stricmp( token, "yes" ) == 0 )
		return TRUE;
	else if( stricmp( token, "no" ) == 0 )
		return FALSE;
	else
	{
		DEBUG_CRASH(("invalid boolean token %s -- expected Yes or No\n",token));
		throw INI_INVALID_DATA;
	}

}

//-------------------------------------------------------------------------------------------------
/** Parse an *ASCII* string from buffer and assign at location 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseAsciiString( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	AsciiString* asciiString = static_cast<AsciiString *>(store);
	*asciiString = ini->getNextAsciiString();
}

//-------------------------------------------------------------------------------------------------
/** Parse an *ASCII* string from buffer and assign at location 'store'. Has better support for quoted strings.
We don't really need this function, but parseString() is broken and we want to leave it broken to
maintain existing code.
 */
//-------------------------------------------------------------------------------------------------
void INI::parseQuotedAsciiString( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	AsciiString* asciiString = static_cast<AsciiString *>(store);
	*asciiString = ini->getNextQuotedAsciiString();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void INI::parseAsciiStringVector( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	std::vector<AsciiString>* asv = static_cast<std::vector<AsciiString>*>(store);
	asv->clear();
	for (const char *token = ini->getNextTokenOrNull(); token != nullptr; token = ini->getNextTokenOrNull())
	{
		asv->push_back(token);
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void INI::parseAsciiStringVectorAppend( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	std::vector<AsciiString>* asv = static_cast<std::vector<AsciiString>*>(store);
	// nope, don't clear. duh.
	// asv->clear();
	for (const char *token = ini->getNextTokenOrNull(); token != nullptr; token = ini->getNextTokenOrNull())
	{
		asv->push_back(token);
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/* static */void INI::parseScienceVector( INI *ini, void * /*instance*/, void *store, const void *userData )
{
	ScienceVec* asv = static_cast<ScienceVec*>(store);
	asv->clear();
	for (const char *token = ini->getNextTokenOrNull(); token != nullptr; token = ini->getNextTokenOrNull())
	{
		if (stricmp(token, "None") == 0)
		{
			asv->clear();
			return;
		}
		asv->push_back(INI::scanScience( token ));
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AsciiString INI::getNextQuotedAsciiString()
{
	AsciiString result;
	char buff[INI_MAX_CHARS_PER_LINE];
	// If the token is a lone quote the strcpy below is skipped; buff must still be a valid string
	// for strcat/strlen (C6001/C6054).
	buff[0] = '\0';

	const char *token = getNextTokenOrNull();	// if null, just leave an empty string
	if (token != nullptr)
	{
		if (token[0] != '\"') 
		{	
			// if token is simply "
			result.set( token );	// Start following the "
		}
		else
		{	int strLen=0;
			Bool done=FALSE;
			if ((strLen=static_cast<Int>(strlen(token))) > 1)
			{
				safe_strcpy(buff, &token[1]);	//skip the starting quote
				//Check for end of quoted string.  Checking here fixes cases where quoted string on same line with other data.
				if (buff[strLen-2]=='"')	//skip ending quote if present
				{	buff[strLen-2]='\0';
					done=TRUE;
				}
			}

			if (!done)
			{
				token = getNextToken(getSepsQuote());
				
				if (strlen(token) > 1 && token[1] != '\t')
				{
					safe_strcat(buff, " ");
					safe_strcat(buff, token);
				}
				else
				{	Int buflen=static_cast<Int>(strlen(buff));
					if (buflen > 0 && buff[buflen-1]=='\"')
						buff[buflen-1]='\0';
				}
			}
			result.set(buff);
		}
	}
	return result;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AsciiString INI::getNextAsciiString()
{
	AsciiString result;

	const char *token = getNextTokenOrNull();	// if null, just leave an empty string
	if (token != nullptr)
	{
		if (token[0] != '\"') 
		{	
			// if token is simply "
			result.set( token );	// Start following the "
		}
		else
		{
			static char buff[INI_MAX_CHARS_PER_LINE];
			buff[0] = 0;
			if (strlen(token) > 1)
			{
				safe_strcpy(buff, &token[1]);
			} 

			token = getNextTokenOrNull(getSepsQuote());
			if (token) {
				if (strlen(token) > 1 && token[1] != '\t')
				{
					safe_strcat(buff, " ");
				}
				safe_strcat(buff, token);
				result.set(buff);
			} else {
				Int len = static_cast<Int>(strlen(buff));
				if (len && buff[len-1] == '"') { // strip off trailing quote jba. [2/12/2003]
					buff[len-1] = 0;
				}
				result.set(buff);
			}
		}
	}
	return result;
}

//-------------------------------------------------------------------------------------------------
/** Parse a string label, get the *translated* actual text from the label and store
	* into a *UNICODE* string. */
//-------------------------------------------------------------------------------------------------
void INI::parseAndTranslateLabel( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();

	// translate
	UnicodeString translated = TheGameText->fetch( token );
	if( translated.isEmpty() )
		throw INI_INVALID_DATA;

	// save the translated text
	UnicodeString *theString = static_cast<UnicodeString *>(store);
	theString->set( translated.str() );

}  // end parseAndTranslateLabel

//-------------------------------------------------------------------------------------------------
/** Parse a string label assumed as an image as part of the image collection.  Translate
	* to an image pointer for storage */
//-------------------------------------------------------------------------------------------------
void INI::parseMappedImage( INI *ini, void * /*instance*/, void *store, const void *userData )
{
	const char *token = ini->getNextToken();

	if( TheMappedImageCollection )
	{
		using ConstImagePtr = const Image*;
		*static_cast<ConstImagePtr*>(store) = TheMappedImageCollection->findImageByName( AsciiString( token ) );
	}
	
	//KM: If we are in the worldbuilder, we want to parse commandbuttons for informational purposes,
	//but we don't care about the images -- because we never access them. In RTS/GUIEdit, they always
	//exist -- and in those cases, it will never call this code anyways because it'll throw long before.
	//else
	//	throw INI_UNKNOWN_ERROR;

}  // end parseMappedImage

// ------------------------------------------------------------------------------------------------
/** Parse a string label assumed as a Anim2D template name.  Translate that name to an 
	* actual template pointer for storage */
// ------------------------------------------------------------------------------------------------
/*static*/ void INI::parseAnim2DTemplate( INI *ini, void *instance, void *store, const void *userData )
{
	const char *token = ini->getNextToken();

	if( TheAnim2DCollection )
	{
		Anim2DTemplate **anim2DTemplate = static_cast<Anim2DTemplate **>(store);
		*anim2DTemplate = TheAnim2DCollection->findTemplate( AsciiString( token ) );
	}  // end if
	else
	{

		DEBUG_CRASH(( "INI::parseAnim2DTemplate - TheAnim2DCollection is NULL\n" ));
		throw INI_UNKNOWN_ERROR;

	}  // end else

}  // end parseAnim2DTemplate

//-------------------------------------------------------------------------------------------------
/** Parse a percent in int or real form such as "23%" or "95.4%" and assign
	* to location 'store' as a number from 0.0 to 1.0 */
//-------------------------------------------------------------------------------------------------
void INI::parsePercentToReal( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken(ini->getSepsPercent());
	Real *theReal = static_cast<Real *>(store);
	*theReal = scanPercentToReal(token);

}  // end parsePercentToReal

//-------------------------------------------------------------------------------------------------
/** 'store' points to an 32 bit unsigned integer.  We will zero that integer, parse each token
	* in the buffer, if the token is in the userData table of strings, we will set the
	* according bit flag for it */
//-------------------------------------------------------------------------------------------------
void INI::parseBitString8( INI* ini, void * /*instance*/, void *store, const void* userData )
{
	UnsignedInt tmp;
	INI::parseBitString32(ini, nullptr, &tmp, userData);
	if (tmp & 0xffffff00)
	{
		DEBUG_CRASH(("Bad bitstring list INI::parseBitString8"));
		throw ERROR_BUG;
	}
	*static_cast<Byte*>(store) = static_cast<Byte>(tmp);
}

//-------------------------------------------------------------------------------------------------
/** 'store' points to an 32 bit unsigned integer.  We will zero that integer, parse each token
	* in the buffer, if the token is in the userData table of strings, we will set the
	* according bit flag for it */
//-------------------------------------------------------------------------------------------------
void INI::parseBitString32( INI* ini, void * /*instance*/, void *store, const void* userData )
{
	ConstCharPtrArray flagList = static_cast<ConstCharPtrArray>(userData);
	UnsignedInt *bits = static_cast<UnsignedInt *>(store);

	if( flagList == nullptr || flagList[ 0 ] == nullptr)
	{
		DEBUG_ASSERTCRASH( flagList, ("INTERNAL ERROR! parseBitString32: No flag list provided!\n") );
		throw INI_INVALID_NAME_LIST;
	}

	Bool foundNormal = false;
	Bool foundAddOrSub = false;

	// loop through all tokens
	for (const char *token = ini->getNextTokenOrNull(); token != nullptr; token = ini->getNextTokenOrNull())
	{
		if (stricmp(token, "NONE") == 0)
		{
			if (foundNormal || foundAddOrSub)
			{
				DEBUG_CRASH(("you may not mix normal and +- ops in bitstring lists"));
				throw INI_INVALID_NAME_LIST;
			}
			*bits = 0;
			break;
		}

		if (token[0] == '+')
		{
			if (foundNormal)
			{
				DEBUG_CRASH(("you may not mix normal and +- ops in bitstring lists"));
				throw INI_INVALID_NAME_LIST;
			}
			Int bitIndex = INI::scanIndexList(token+1, flagList);	// this throws if the token is not found
			*bits |= (1 << bitIndex);
			foundAddOrSub = true;
		}
		else if (token[0] == '-')
		{
			if (foundNormal)
			{
				DEBUG_CRASH(("you may not mix normal and +- ops in bitstring lists"));
				throw INI_INVALID_NAME_LIST;
			}
			Int bitIndex = INI::scanIndexList(token+1, flagList);	// this throws if the token is not found
			*bits &= ~(1 << bitIndex);
			foundAddOrSub = true;
		}
		else
		{
			if (foundAddOrSub)
			{
				DEBUG_CRASH(("you may not mix normal and +- ops in bitstring lists"));
				throw INI_INVALID_NAME_LIST;
			}

			if (!foundNormal)
				*bits = 0;

			Int bitIndex = INI::scanIndexList(token, flagList);	// this throws if the token is not found
			*bits |= (1 << bitIndex);
			foundNormal = true;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Parse a color in the form of
	*
	* RGB_COLOR = R:100 G:114 B:245
	* and store in "RGBColor" structure pointed to by 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseRGBColor( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char* names[3] = { "R", "G", "B" };
	Int colors[3];
	for( Int i = 0; i < 3; i++ )
	{
		colors[i] = scanInt(ini->getNextSubToken(names[i]));
		if( colors[ i ] < 0 )
			throw INI_INVALID_DATA;
		if( colors[ i ] > 255 )
			throw INI_INVALID_DATA;
	}

	// assign the color components to the "RGBColor" pointer at 'store'
	RGBColor *theColor = static_cast<RGBColor *>(store);
	theColor->red		= static_cast<Real>(colors[ 0 ]) / 255.0f;
	theColor->green = static_cast<Real>(colors[ 1 ]) / 255.0f;
	theColor->blue	= static_cast<Real>(colors[ 2 ]) / 255.0f;

}

//-------------------------------------------------------------------------------------------------
/** Parse a color in the form of
	*
	* RGB_COLOR = R:100 G:114 B:245 [A:233]
	* and store in "RGBAColorInt" structure pointed to by 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseRGBAColorInt( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char* names[4] = { "R", "G", "B", "A" };
	Int colors[4];
	for( Int i = 0; i < 4; i++ )
	{
		const char* token = ini->getNextTokenOrNull(ini->getSepsColon());
		if (token == nullptr)
		{
			if (i < 3)
			{
				throw INI_INVALID_DATA;
			}
			else
			{
				// it's ok for A to be omitted.
				colors[i] = 255;
			}
		}
		else
		{
			// if present, the token must match.
			if (stricmp(token, names[i]) != 0)
			{
				throw INI_INVALID_DATA;				
			}
			colors[i] = scanInt(ini->getNextToken(ini->getSepsColon()));
		}
		if( colors[ i ] < 0 )
			throw INI_INVALID_DATA;
		if( colors[ i ] > 255 )
			throw INI_INVALID_DATA;
	}

	//
	// assign the color components to the "RGBColorInt" pointer at 'store', keep
	// the numbers as between 0 and 255
	//
	RGBAColorInt *theColor = static_cast<RGBAColorInt *>(store);
	theColor->red		= colors[ 0 ];
	theColor->green = colors[ 1 ];
	theColor->blue	= colors[ 2 ];
	theColor->alpha = colors[ 3 ];

}  // end parseRGBAColorInt

//-------------------------------------------------------------------------------------------------
/** Parse a color in the form of
	*
	* RGB_COLOR = R:100 G:114 B:245 [A:233]
	* and store in "Color" structure pointed to by 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseColorInt( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char* names[4] = { "R", "G", "B", "A" };
	Int colors[4];
	for( Int i = 0; i < 4; i++ )
	{
		const char* token = ini->getNextTokenOrNull(ini->getSepsColon());
		if (token == nullptr)
		{
			if (i < 3)
			{
				throw INI_INVALID_DATA;
			}
			else
			{
				// it's ok for A to be omitted.
				colors[i] = 255;
			}
		}
		else
		{
			// if present, the token must match.
			if (stricmp(token, names[i]) != 0)
			{
				throw INI_INVALID_DATA;				
			}
			colors[i] = scanInt(ini->getNextToken(ini->getSepsColon()));
		}
		if( colors[ i ] < 0 )
			throw INI_INVALID_DATA;
		if( colors[ i ] > 255 )
			throw INI_INVALID_DATA;
	}

	//
	// assign the color components to the "Color" pointer at 'store', keep
	// the numbers as between 0 and 255
	//
	Color *theColor = static_cast<Color *>(store);
	*theColor = GameMakeColor(static_cast<UnsignedByte>(colors[0]), static_cast<UnsignedByte>(colors[1]), static_cast<UnsignedByte>(colors[2]), static_cast<UnsignedByte>(colors[3]));

}  // end parseColorInt

//-------------------------------------------------------------------------------------------------
/** Parse a 3D coordinate of reals in the form of:
	* FIELD_NAME = X:400 Y:-214.3 Z:8.6 */
//-------------------------------------------------------------------------------------------------
void INI::parseCoord3D( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	Coord3D *theCoord = static_cast<Coord3D *>(store);

	theCoord->x = scanReal(ini->getNextSubToken("X"));
	theCoord->y = scanReal(ini->getNextSubToken("Y"));
	theCoord->z = scanReal(ini->getNextSubToken("Z"));

}  // end parseCoord3D

//-------------------------------------------------------------------------------------------------
/** Parse a 2D coordinate of reals in the form of:
	* FIELD_NAME = X:400 Y:-214.3 */
//-------------------------------------------------------------------------------------------------
void INI::parseCoord2D( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	Coord2D *theCoord = static_cast<Coord2D *>(store);

	theCoord->x = scanReal(ini->getNextSubToken("X"));
	theCoord->y = scanReal(ini->getNextSubToken("Y"));

}  // end parseCoord2D

//-------------------------------------------------------------------------------------------------
/** Parse a 2D coordinate of Ints in the form of:
	* FIELD_NAME = X:400 Y:-214 */
//-------------------------------------------------------------------------------------------------
void INI::parseICoord2D( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	ICoord2D *theCoord = static_cast<ICoord2D *>(store);

	theCoord->x = scanInt(ini->getNextSubToken("X"));
	theCoord->y = scanInt(ini->getNextSubToken("Y"));

}  // end parseICoord2D

//-------------------------------------------------------------------------------------------------
/** Parse an audio event and assign to the 'AudioEventRTS*' at store */
//-------------------------------------------------------------------------------------------------
void INI::parseDynamicAudioEventRTS( INI *ini, void * /*instance*/, void *store, const void* userData )
{
	const char *token = ini->getNextToken();
	DynamicAudioEventRTS** theSound = static_cast<DynamicAudioEventRTS**>(store);
	
	// translate the string into a sound
	if (stricmp(token, "NoSound") == 0) 
	{
		if (*theSound)
		{
			deleteInstanceOf((*theSound));
			*theSound = nullptr;
		}
	}
	else
	{
		if (*theSound == nullptr)
			*theSound = newInstance(DynamicAudioEventRTS);
		(*theSound)->m_event.setEventName(AsciiString(token));
	}
	
	if (*theSound)
		TheAudio->getInfoForAudioEvent(&(*theSound)->m_event);
}

//-------------------------------------------------------------------------------------------------
/** Parse an audio event and assign to the 'AudioEventRTS*' at store */
//-------------------------------------------------------------------------------------------------
void INI::parseAudioEventRTS( INI *ini, void * /*instance*/, void *store, const void* userData )
{
	const char *token = ini->getNextToken();

	AudioEventRTS *theSound = static_cast<AudioEventRTS*>(store);
	
	// translate the string into a sound
	if (stricmp(token, "NoSound") != 0) {
		theSound->setEventName(AsciiString(token));
	}

	TheAudio->getInfoForAudioEvent(theSound);
}

//-------------------------------------------------------------------------------------------------
/** Parse an ThingTemplate and assign to the 'ThingTemplate *' at store */
//-------------------------------------------------------------------------------------------------
void INI::parseThingTemplate( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();

	if (!TheThingFactory)
	{
		DEBUG_CRASH(("TheThingFactory not inited yet"));
		throw ERROR_BUG;
	}

	using ConstThingTemplatePtr = const ThingTemplate *;
	ConstThingTemplatePtr* theThingTemplate = static_cast<ConstThingTemplatePtr*>(store);		

	if (stricmp(token, "None") == 0)
	{
		*theThingTemplate = nullptr;
	}
	else
	{
		const ThingTemplate *tt = TheThingFactory->findTemplate(token);	// could be null!
		DEBUG_ASSERTCRASH(tt, ("ThingTemplate %s not found!\n",token));
		// assign it, even if null!
		*theThingTemplate = tt;
	}

} 

//-------------------------------------------------------------------------------------------------
/** Parse an ArmorTemplate and assign to the 'ArmorTemplate *' at store */
//-------------------------------------------------------------------------------------------------
void INI::parseArmorTemplate( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();

	using ConstArmorTemplatePtr = const ArmorTemplate *;
	ConstArmorTemplatePtr* theArmorTemplate = static_cast<ConstArmorTemplatePtr*>(store);		

	if (stricmp(token, "None") == 0)
	{
		*theArmorTemplate = nullptr;
	}
	else
	{
		const ArmorTemplate *tt = TheArmorStore->findArmorTemplate(token);	// could be null!
		DEBUG_ASSERTCRASH(tt, ("ArmorTemplate %s not found!\n",token));
		// assign it, even if null!
		*theArmorTemplate = tt;
	}

} 

//-------------------------------------------------------------------------------------------------
/** Parse an WeaponTemplate and assign to the 'WeaponTemplate *' at store */
//-------------------------------------------------------------------------------------------------
void INI::parseWeaponTemplate( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();

	using ConstWeaponTemplatePtr = const WeaponTemplate *;
	ConstWeaponTemplatePtr* theWeaponTemplate = static_cast<ConstWeaponTemplatePtr*>(store);		

	const WeaponTemplate *tt = TheWeaponStore->findWeaponTemplate(token);	// could be null!
	DEBUG_ASSERTCRASH(tt || stricmp(token, "None") == 0, ("WeaponTemplate %s not found!\n",token));
	// assign it, even if null!
	*theWeaponTemplate = tt;

} 

//-------------------------------------------------------------------------------------------------
/** Parse an FXList and assign to the 'FXList *' at store */
//-------------------------------------------------------------------------------------------------
void INI::parseFXList( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();

	using ConstFXListPtr = const FXList *;
	ConstFXListPtr* theFXList = static_cast<ConstFXListPtr*>(store);		

	const FXList *fxl = TheFXListStore->findFXList(token);	// could be null!
	DEBUG_ASSERTCRASH(fxl != nullptr || stricmp(token, "None") == 0, ("FXList %s not found!\n",token));
	// assign it, even if null!
	*theFXList = fxl;

} 

//-------------------------------------------------------------------------------------------------
/** Parse a particle system and assign to 'ParticleSystemTemplate *' at store */
//-------------------------------------------------------------------------------------------------
void INI::parseParticleSystemTemplate( INI *ini, void * /*instance*/, void *store, const void *userData )
{
	const char *token = ini->getNextToken();

	const ParticleSystemTemplate *pSystemT = TheParticleSystemManager->findTemplate( AsciiString( token ) );
	DEBUG_ASSERTCRASH( pSystemT || stricmp( token, "None" ) == 0, ("ParticleSystem %s not found!\n",token) );

	using ConstParticleSystemTemplatePtr = const ParticleSystemTemplate*;
	ConstParticleSystemTemplatePtr* theParticleSystemTemplate = static_cast<ConstParticleSystemTemplatePtr*>(store);		

	*theParticleSystemTemplate = pSystemT;

}  // end parseParticleSystemTemplate

//-------------------------------------------------------------------------------------------------
/** Parse an DamageFX and assign to the 'DamageFX *' at store */
//-------------------------------------------------------------------------------------------------
void INI::parseDamageFX( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();

	using ConstDamageFXPtr = const DamageFX *;
	ConstDamageFXPtr* theDamageFX = static_cast<ConstDamageFXPtr*>(store);		

	if (stricmp(token, "None") == 0)
	{
		*theDamageFX = nullptr;
	}
	else
	{
		const DamageFX *fxl = TheDamageFXStore->findDamageFX(token);	// could be null!
		DEBUG_ASSERTCRASH(fxl, ("DamageFX %s not found!\n",token));
		// assign it, even if null!
		*theDamageFX = fxl;
	}

} 

//-------------------------------------------------------------------------------------------------
/** Parse an ObjectCreationList and assign to the 'ObjectCreationList *' at store */
//-------------------------------------------------------------------------------------------------
void INI::parseObjectCreationList( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();

	using ConstObjectCreationListPtr = const ObjectCreationList *;
	ConstObjectCreationListPtr* theObjectCreationList = static_cast<ConstObjectCreationListPtr*>(store);		

	const ObjectCreationList *ocl = TheObjectCreationListStore->findObjectCreationList(token);	// could be null!
	DEBUG_ASSERTCRASH(ocl || stricmp(token, "None") == 0, ("ObjectCreationList %s not found!\n",token));
	// assign it, even if null!
	*theObjectCreationList = ocl;

} 

//-------------------------------------------------------------------------------------------------
/** Parse a upgrade template string and store as template pointer */
//-------------------------------------------------------------------------------------------------
void INI::parseUpgradeTemplate( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();

	if (!TheUpgradeCenter)
	{
		DEBUG_CRASH(("TheUpgradeCenter not inited yet"));
		throw ERROR_BUG;
	}

	const UpgradeTemplate *uu = TheUpgradeCenter->findUpgrade( AsciiString( token ) );
	DEBUG_ASSERTCRASH( uu || stricmp( token, "None" ) == 0, ("Upgrade %s not found!\n",token) );

	using ConstUpgradeTemplatePtr = const UpgradeTemplate*;
	ConstUpgradeTemplatePtr* theUpgradeTemplate = static_cast<ConstUpgradeTemplatePtr *>(store);		
	*theUpgradeTemplate = uu;
} 

//-------------------------------------------------------------------------------------------------
/** Parse a special power template string and store as template pointer */
//-------------------------------------------------------------------------------------------------
void INI::parseSpecialPowerTemplate( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();

	if (!TheSpecialPowerStore)
	{
		DEBUG_CRASH(("TheSpecialPowerStore not inited yet"));
		throw ERROR_BUG;
	}

	const SpecialPowerTemplate *sPowerT = TheSpecialPowerStore->findSpecialPowerTemplate( AsciiString( token ) );
	if( !sPowerT && stricmp( token, "None" ) != 0 )
	{
		DEBUG_CRASH( ("[LINE: %d in '%s'] Specialpower %s not found!\n", ini->getLineNum(), ini->getFilename().str(), token) );
	}

	using ConstSpecialPowerTemplatePtr = const SpecialPowerTemplate*;
	ConstSpecialPowerTemplatePtr* theSpecialPowerTemplate = static_cast<ConstSpecialPowerTemplatePtr *>(store);		
	*theSpecialPowerTemplate = sPowerT;
} 

//-------------------------------------------------------------------------------------------------
/** Parse a science string and store as science type */
//-------------------------------------------------------------------------------------------------
/* static */void INI::parseScience( INI *ini, void * /*instance*/, void *store, const void *userData )
{
	const char *token = ini->getNextToken();

	if (!TheScienceStore)
	{
		DEBUG_CRASH(("TheScienceStore not inited yet"));
		throw ERROR_BUG;
	}

	*(static_cast<ScienceType *>(store)) = INI::scanScience(token);

}

//-------------------------------------------------------------------------------------------------
/** Parse a single string token, check for that token in the index list
	* of names provided and store the index into that list.
	*
	* NOTE: Is is assumed that we are going to store the index into
	*				a 4 byte integer.  This works well for INT and ENUM definitions */
//-------------------------------------------------------------------------------------------------
void INI::parseIndexList( INI* ini, void * /*instance*/, void *store, const void* userData )
{
	ConstCharPtrArray nameList = static_cast<ConstCharPtrArray>(userData);
	*static_cast<Int *>(store) = scanIndexList(ini->getNextToken(), nameList);
} 

//-------------------------------------------------------------------------------------------------
/** Parse a single string token, check for that token in the index list
	* of names provided and store the index into that list.
	*
	* NOTE: Is is assumed that we are going to store the index into
	*				a 4 byte integer.  This works well for INT and ENUM definitions */
//-------------------------------------------------------------------------------------------------
void INI::parseByteSizedIndexList( INI* ini, void * /*instance*/, void *store, const void* userData )
{
	ConstCharPtrArray nameList = static_cast<ConstCharPtrArray>(userData);
	Int value = scanIndexList(ini->getNextToken(), nameList);
	if (value < 0 || value > 255)
	{
		DEBUG_CRASH(("Bad index list INI::parseByteSizedIndexList"));
		throw ERROR_BUG;
	}
	*static_cast<Byte *>(store) = static_cast<Byte>(value);
} 

//-------------------------------------------------------------------------------------------------
/** Parse a single string token, check for that token in the index list
	* of names provided and store the associated value into that list.
	*
	* NOTE: Is is assumed that we are going to store the index into
	*				a 4 byte integer.  This works well for INT and ENUM definitions */
//-------------------------------------------------------------------------------------------------
void INI::parseLookupList( INI* ini, void * /*instance*/, void *store, const void* userData )
{
	ConstLookupListRecArray lookupList = static_cast<ConstLookupListRecArray>(userData);
	*static_cast<Int *>(store) = scanLookupList(ini->getNextToken(), lookupList);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

	
//-------------------------------------------------------------------------------------------------
void MultiIniFieldParse::add(const FieldParse* f, UnsignedInt e)
{
	if (m_count < MAX_MULTI_FIELDS)
	{
		m_fieldParse[m_count] = f;
		m_extraOffset[m_count] = e;
		++m_count;
	}
	else
	{
		DEBUG_CRASH(("too many multi-fields in INI::initFromINIMultiProc"));
		throw ERROR_BUG;
	}
}

//-------------------------------------------------------------------------------------------------
void INI::initFromINI( void *what, const FieldParse* parseTable )
{
	MultiIniFieldParse p;
	// without a table (modules without data) every field is unknown; only the end token is accepted
	if (parseTable)
		p.add(parseTable);
	initFromINIMulti(what, p);
}

//-------------------------------------------------------------------------------------------------
void INI::initFromINIMultiProc( void *what, BuildMultiIniFieldProc proc )
{
	MultiIniFieldParse p;
	(*proc)(p);
	initFromINIMulti(what, p);
}

//-------------------------------------------------------------------------------------------------
void INI::initFromINIMulti( void *what, const MultiIniFieldParse& parseTableList )
{
	Bool done = FALSE;

	if( what == nullptr )
	{
		DEBUG_ASSERTCRASH( 0, ("INI::initFromINI - Invalid parameters supplied!\n") );
		throw INI_INVALID_PARAMS;
	}

	// read each of the data fields
	while( !done )
	{

		// read next line
		readLine();

		// check for end token
		const char* field = strtok( m_buffer, INI::getSeps() );
		if( field )
		{

			if( stricmp( field, m_blockEndToken ) == 0 )
			{
				done = TRUE;
			}
			else
			{
				Bool found = false;
				for (int ptIdx = 0; ptIdx < parseTableList.getCount(); ++ptIdx)
				{
					int offset = 0;
					const void* userData = 0;
					INIFieldParseProc parse = findFieldParse(parseTableList.getNthFieldParse(ptIdx), field, offset, userData);
					if (parse)
					{
						// parse this block and check for parse errors
						try {

						(*parse)( this, what, static_cast<char *>(what) + offset + parseTableList.getNthExtraOffset(ptIdx), userData );

						} catch (...) {
							DEBUG_CRASH( ("[LINE: %d - FILE: '%s'] Error reading field '%s' of block '%s'\n",
																 INI::getLineNum(), INI::getFilename().str(), field, m_curBlockStart) );


							char buff[1024];
							WWFormat::Into(buff, "[LINE: {:d} - FILE: '{}'] Error reading field '{}'\n", INI::getLineNum(), INI::getFilename().str(), field);
							throw INIException(buff);
						}
						
						found = true;
						break;
						
					}
				}

				if (!found)
				{
					DEBUG_ASSERTCRASH( 0, ("[LINE: %d - FILE: '%s'] Unknown field '%s' in block '%s'\n",
														 INI::getLineNum(), INI::getFilename().str(), field, m_curBlockStart) );
					throw INI_UNKNOWN_TOKEN;
				}

			}  // end else

		}  // end if

		// sanity check for reaching end of file with no closing end token
		if( done == FALSE && INI::isEOF() == TRUE )
		{

			done = TRUE;
			DEBUG_ASSERTCRASH( 0, ("Error parsing block '%s', in INI file '%s'.  Missing '%s' token\n",
												 m_curBlockStart, getFilename().str(), m_blockEndToken) );
			throw INI_MISSING_END_TOKEN;

		}  // end if

	}  // end while

}

//-------------------------------------------------------------------------------------------------
/*static*/ const char* INI::getNextToken(const char* seps)
{
	if (!seps) seps = getSeps();
	const char *token = ::strtok(nullptr, seps);
	if (!token) 
		throw INI_INVALID_DATA;
	return token;
}

//-------------------------------------------------------------------------------------------------
/*static*/ const char* INI::getNextTokenOrNull(const char* seps)
{
	if (!seps) seps = getSeps();
	const char *token = ::strtok(nullptr, seps);
	return token;
}

//-------------------------------------------------------------------------------------------------
/*static*/ ScienceType INI::scanScience(const char* token)
{
	return TheScienceStore->friend_lookupScience( token );
}

//-------------------------------------------------------------------------------------------------
/*static*/ Int INI::scanInt(const char* token)
{
	Int value;
	if (sscanf( token, "%d", &value ) != 1)
		throw INI_INVALID_DATA;
	return value;
}

//-------------------------------------------------------------------------------------------------
/*static*/ Int INI::scanObjectCount(const char* token)
{
	const Int value = scanInt(token);
	if (value < 0 || value > MAX_OBJECT_COUNT)
	{
		DEBUG_CRASH(("object count %d out of range 0..%d\n", value, MAX_OBJECT_COUNT));
		throw INI_INVALID_DATA;
	}
	return value;
}

//-------------------------------------------------------------------------------------------------
void INI::parseObjectCount( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	*static_cast<Int *>(store) = scanObjectCount(ini->getNextToken());
}

//-------------------------------------------------------------------------------------------------
/*static*/ UnsignedInt INI::scanUnsignedInt(const char* token)
{
	UnsignedInt value;
	if (sscanf( token, "%u", &value ) != 1)	// unsigned int is %u, not %d
		throw INI_INVALID_DATA;
	return value;
}

//-------------------------------------------------------------------------------------------------
/*static*/ Real INI::scanReal(const char* token)
{
	Real value;
	if (sscanf( token, "%f", &value ) != 1)
		throw INI_INVALID_DATA;
	return value;
}

//-------------------------------------------------------------------------------------------------
/*static*/ Real INI::scanPercentToReal(const char* token)
{
	Real value;
	if (sscanf( token, "%f", &value ) != 1)
		throw INI_INVALID_DATA;
	return value / 100.0f;
}

//-------------------------------------------------------------------------------------------------
/*static*/ Int INI::scanIndexList(const char* token, ConstCharPtrArray nameList)
{
	if( nameList == nullptr || nameList[ 0 ] == nullptr )
	{

		DEBUG_ASSERTCRASH( 0, ("INTERNAL ERROR! scanIndexList, invalid name list\n") );
		throw INI_INVALID_NAME_LIST;

	}

	// search for matching name
	Int count = 0;
	for(ConstCharPtrArray name = nameList; *name; name++, count++ )
	{
		if( stricmp( *name, token ) == 0 )
		{
			return count;
		}
	}

	DEBUG_CRASH(("token %s is not a valid member of the index list\n",token));
	throw INI_INVALID_DATA;

}
//-------------------------------------------------------------------------------------------------
/*static*/ Int INI::scanLookupList(const char* token, ConstLookupListRecArray lookupList)
{
	if( lookupList == nullptr || lookupList[ 0 ].name == nullptr )
	{
		DEBUG_ASSERTCRASH( 0, ("INTERNAL ERROR! scanLookupList, invalid name list\n") );
		throw INI_INVALID_NAME_LIST;
	}

	// search for matching name
	for( const LookupListRec* lookup = &lookupList[0]; lookup->name; lookup++ )
	{
		if( stricmp( lookup->name, token ) == 0 )
			return lookup->value;
	}

	DEBUG_CRASH(("token %s is not a valid member of the lookup list\n",token));
	throw INI_INVALID_DATA;

}

//-------------------------------------------------------------------------------------------------
const char* INI::getNextSubToken(const char* expected)
{
	const char* token = getNextToken(getSepsColon());
	if (stricmp(token, expected) != 0)
		throw INI_INVALID_DATA;
	return getNextToken(getSepsColon());
}

//-------------------------------------------------------------------------------------------------
/**
 * Parse a "random variable".
 * The format is "FIELD = low high [distribution]".
 */
void INI::parseGameClientRandomVariable( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	GameClientRandomVariable *var = static_cast<GameClientRandomVariable *>(store);

	const char* token;

	token = ini->getNextToken();
	Real low = INI::scanReal(token);

	token = ini->getNextToken();
	Real high = INI::scanReal(token);

	// if omitted, assume uniform
	GameClientRandomVariable::DistributionType type = GameClientRandomVariable::UNIFORM;
	token = ini->getNextTokenOrNull();
	if (token)
		type = static_cast<GameClientRandomVariable::DistributionType>(INI::scanIndexList(token, GameClientRandomVariable::DistributionTypeNames));

	// set the range of the random variable
	var->setRange( low, high, type );
}

//-------------------------------------------------------------------------------------------------
// parse a duration in msec and convert to duration in frames
void INI::parseDurationReal( INI *ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	Real val = scanReal(ini->getNextToken());
	*static_cast<Real *>(store) = ConvertDurationFromMsecsToFrames(val);
}

//-------------------------------------------------------------------------------------------------
// parse a duration in msec and convert to duration in integral number of frames, (unsignedint) rounding UP
void INI::parseDurationUnsignedInt( INI *ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	UnsignedInt val = scanUnsignedInt(ini->getNextToken());
	*static_cast<UnsignedInt *>(store) = static_cast<UnsignedInt>(ceilf(ConvertDurationFromMsecsToFrames(static_cast<Real>(val))));
}

// ------------------------------------------------------------------------------------------------
// parse a duration in msec and convert to duration in integral number of frames, (unsignedshort) rounding UP
void INI::parseDurationUnsignedShort( INI *ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	UnsignedInt val = scanUnsignedInt(ini->getNextToken());
	*static_cast<UnsignedShort *>(store) = static_cast<UnsignedShort>(ceilf(ConvertDurationFromMsecsToFrames(static_cast<Real>(val))));
}

//-------------------------------------------------------------------------------------------------
// parse acceleration in (dist/sec) and convert to (dist/frame)
void INI::parseVelocityReal( INI *ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();
	Real val = scanReal(token);
	*static_cast<Real *>(store) = ConvertVelocityInSecsToFrames(val);
}

//-------------------------------------------------------------------------------------------------
// parse acceleration in (dist/sec^2) and convert to (dist/frame^2)
void INI::parseAccelerationReal( INI *ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();
	Real val = scanReal(token);
	*static_cast<Real *>(store) = ConvertAccelerationInSecsToFrames(val);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void INI::parseVeterancyLevelFlags(INI* ini, void* /*instance*/, void* store, const void* /*userData*/)
{
	VeterancyLevelFlags flags = VETERANCY_LEVEL_FLAGS_ALL;
	for (const char* token = ini->getNextToken(); token; token = ini->getNextTokenOrNull())
	{
		if (stricmp(token, "ALL") == 0)
		{
			flags = VETERANCY_LEVEL_FLAGS_ALL;
			continue;
		}
		else if (stricmp(token, "NONE") == 0)
		{
			flags = VETERANCY_LEVEL_FLAGS_NONE;
			continue;
		}
		else if (token[0] == '+')
		{
			VeterancyLevel dt = static_cast<VeterancyLevel>(INI::scanIndexList(token+1, TheVeterancyNames));
			flags = setVeterancyLevelFlag(flags, dt);
			continue;
		}
		else if (token[0] == '-')
		{
			VeterancyLevel dt = static_cast<VeterancyLevel>(INI::scanIndexList(token+1, TheVeterancyNames));
			flags = clearVeterancyLevelFlag(flags, dt);
			continue;
		}
		else
		{
			throw INI_UNKNOWN_TOKEN;
		}
	}
	*static_cast<VeterancyLevelFlags*>(store) = flags;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void INI::parseSoundsList( INI* ini, void *instance, void *store, const void* /*userData*/ )
{
	std::vector<AsciiString> *vec = static_cast<std::vector<AsciiString>*>(store);
	vec->clear();

	const char* SEPS = " \t,=";
	const char *c = ini->getNextTokenOrNull(SEPS);
	while ( c )
	{
		vec->push_back( c );
		c = ini->getNextTokenOrNull(SEPS);
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void INI::parseDamageTypeFlags(INI* ini, void* /*instance*/, void* store, const void* /*userData*/)
{
	DamageTypeFlags flags = DAMAGE_TYPE_FLAGS_NONE;
	flags.flip();

	for (const char* token = ini->getNextToken(); token; token = ini->getNextTokenOrNull())
	{
		if (stricmp(token, "ALL") == 0)
		{
			flags = DAMAGE_TYPE_FLAGS_NONE;
			flags.flip();
			continue;
		}
		if (stricmp(token, "NONE") == 0)
		{
			flags = DAMAGE_TYPE_FLAGS_NONE;
			continue;
		}
		if (token[0] == '+')
		{
			DamageType dt = static_cast<DamageType>(DamageTypeFlags::getSingleBitFromName(token+1));
			flags = setDamageTypeFlag(flags, dt);
			continue;
		}
		if (token[0] == '-')
		{
			DamageType dt = static_cast<DamageType>(DamageTypeFlags::getSingleBitFromName(token+1));
			flags = clearDamageTypeFlag(flags, dt);
			continue;
		}
		throw INI_UNKNOWN_TOKEN;
	}
	*static_cast<DamageTypeFlags*>(store) = flags;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void INI::parseDeathTypeFlags(INI* ini, void* /*instance*/, void* store, const void* /*userData*/)
{
	DeathTypeFlags flags = DEATH_TYPE_FLAGS_ALL;
	for (const char* token = ini->getNextToken(); token; token = ini->getNextTokenOrNull())
	{
		if (stricmp(token, "ALL") == 0)
		{
			flags = DEATH_TYPE_FLAGS_ALL;
			continue;
		}
		if (stricmp(token, "NONE") == 0)
		{
			flags = DEATH_TYPE_FLAGS_NONE;
			continue;
		}
		if (token[0] == '+')
		{
			DeathType dt = static_cast<DeathType>(INI::scanIndexList(token+1, TheDeathNames));
			flags = setDeathTypeFlag(flags, dt);
			continue;
		}
		if (token[0] == '-')
		{
			DeathType dt = static_cast<DeathType>(INI::scanIndexList(token+1, TheDeathNames));
			flags = clearDeathTypeFlag(flags, dt);
			continue;
		}
		throw INI_UNKNOWN_TOKEN;
	}
	*static_cast<DeathTypeFlags*>(store) = flags;
}

//-------------------------------------------------------------------------------------------------
// parse the line and return whether the given line is a Block declaration of the form
// [whitespace] blockType [whitespace] blockName [EOL]
// both blockType and blockName are case insensitive
Bool INI::isDeclarationOfType( AsciiString blockType, AsciiString blockName, char *bufferToCheck )
{
	Bool retVal = true;
	if (!bufferToCheck || blockType.isEmpty() || blockName.isEmpty()) {
		return false;
	}
	// DO NOT RETURN EARLY FROM THIS FUNCTION. (beyond this point)
	// we have to restore the bufferToCheck to its previous state before returning, so 
	// it is important to get through all the checks.
	
	char restoreChar;
	char *tempBuff = bufferToCheck;
	int blockTypeLength = blockType.getLength();
	int blockNameLength = blockName.getLength();

	while (isspace(static_cast<unsigned char>(*tempBuff))) {
		++tempBuff;
	}
	
	if (strlen(tempBuff) > blockTypeLength) {
		restoreChar = tempBuff[blockTypeLength];
		tempBuff[blockTypeLength] = 0;
		
		if (stricmp(blockType.str(), tempBuff) != 0) {
			retVal = false;
		}

		tempBuff[blockTypeLength] = restoreChar;
		tempBuff = tempBuff + blockTypeLength;
	} else {
		retVal = false;
	}

	while (isspace(static_cast<unsigned char>(*tempBuff))) {
		++tempBuff;
	}

	if (strlen(tempBuff) > blockNameLength) {
		restoreChar = tempBuff[blockNameLength];
		tempBuff[blockNameLength] = 0;
		
		if (stricmp(blockName.str(), tempBuff) != 0) {
			retVal = false;
		}

		tempBuff[blockNameLength] = restoreChar;
		tempBuff = tempBuff + blockNameLength;
	} else {
		retVal = false;
	}

	while (strlen(tempBuff)) {
		retVal = retVal && isspace(static_cast<unsigned char>(tempBuff[0]));
		++tempBuff;
	}

	return retVal;
}

//-------------------------------------------------------------------------------------------------
// parse the line and return whether the given line is a Block declaration of the form
// [whitespace] end [EOL]
Bool INI::isEndOfBlock( char *bufferToCheck )
{
	Bool retVal = true;
	if (!bufferToCheck) {
		return false;
	}

	// DO NOT RETURN EARLY FROM THIS FUNCTION (beyond this point)
	// we have to restore the bufferToCheck to its previous state before returning, so 
	// it is important to get through all the checks.
	
	static const char* endString = "End";
	int endStringLength = static_cast<Int>(strlen(endString));
	char restoreChar;
	char *tempBuff = bufferToCheck;
	

	while (isspace(static_cast<unsigned char>(*tempBuff))) {
		++tempBuff;
	}
	
	if (strlen(tempBuff) > endStringLength) {
		restoreChar = tempBuff[endStringLength];
		tempBuff[endStringLength] = 0;
		
		if (stricmp(endString, tempBuff) != 0) {
			retVal = false;
		}

		tempBuff[endStringLength] = restoreChar;
		tempBuff = tempBuff + endStringLength;
	} else {
		retVal = false;
	}

	while (strlen(tempBuff)) {
		retVal = retVal && isspace(static_cast<unsigned char>(tempBuff[0]));
		++tempBuff;
	}

	return retVal;
}
