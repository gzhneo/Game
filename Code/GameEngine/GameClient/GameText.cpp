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

//----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright(C) 2001 - All Rights Reserved                  
//                                                                          
//----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: GameText.cpp
//
// Created:   11/07/01
//
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Includes                                                      
//----------------------------------------------------------------------------

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/GameText.h"
#include "Common/Language.h"
#include "Common/InstallInfo.h"
#include "GameClient/LanguageFilter.h"
#include "Common/Debug.h"
#include "Common/UnicodeString.h"
#include "Common/AsciiString.h"
#include "Common/GlobalData.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Engine/TextFiles.h"
#include "Lib/SafeString.h"

#include <algorithm>
#include <vector>



//----------------------------------------------------------------------------
//         Externals                                                     
//----------------------------------------------------------------------------

#if defined(_DEBUG)
Bool g_useStringFile = TRUE;
#endif


//----------------------------------------------------------------------------
//         Defines                                                         
//----------------------------------------------------------------------------

#define MAX_UITEXT_LENGTH (10*1024)
// the readers in TextFiles.cpp keep texts and names short enough for these buffers
static_assert( Engine::TEXT_FILE_MAX_TEXT < MAX_UITEXT_LENGTH && Engine::TEXT_FILE_MAX_LABEL < MAX_UITEXT_LENGTH );

//----------------------------------------------------------------------------
//         Private Types                                                     
//----------------------------------------------------------------------------

//===============================
// StringInfo 
//===============================

struct StringInfo
{
	AsciiString			label;
	UnicodeString		text;
	AsciiString			speech;
};

struct StringLookUp
{
	AsciiString		*label;
	StringInfo		*info;
};

//===============================
// struct NoString 
//===============================

struct NoString
{
	struct NoString *next;
	UnicodeString text;
};


//===============================
// GameTextManager 
//===============================

class GameTextManager : public GameTextInterface
{
	public:

		GameTextManager();
		virtual ~GameTextManager() override;

		virtual void					init( void ) override;						///< Initlaizes the text system
		virtual void					deinit( void );					///< De-initlaizes the text system
		virtual void					update( void ) override {};			///< update text manager
		virtual void					reset( void ) override;					///< Resets the text system

		virtual UnicodeString fetch( const Char *label, Bool *exists = nullptr ) override;		///< Returns the associated labeled unicode text
		virtual UnicodeString fetch( AsciiString label, Bool *exists = nullptr ) override;		///< Returns the associated labeled unicode text
		virtual AsciiStringVec& getStringsWithLabelPrefix(AsciiString label) override;

		virtual void					initMapStringFile( const AsciiString& filename ) override;

	protected:

		Int							m_textCount;
		Int							m_maxLabelLen;
		Char						m_buffer2[MAX_UITEXT_LENGTH];
		WideChar				m_tbuffer[MAX_UITEXT_LENGTH*2];
		
		StringInfo			*m_stringInfo;
		StringLookUp		*m_stringLUT;
		Bool						m_initialized;
#if defined(_DEBUG)
		Bool						m_jabberWockie;
		Bool						m_munkee;
#endif
		NoString				*m_noStringList;
		Int							m_useStringFile;
		LanguageID			m_language;
		UnicodeString		m_failed;

		StringInfo			*m_mapStringInfo;
		StringLookUp		*m_mapStringLUT;
		Int							m_mapTextCount;

		// Texts the project adds (ProjectData\Text\<language>.str); looked up after EA's table.
		StringInfo			*m_projectStringInfo;
		StringLookUp		*m_projectStringLUT;
		Int							m_projectTextCount;
		void						loadProjectStrings( void );

		/// m_asciiStringVec will be altered every time that getStringsWithLabelPrefix is called,
		/// so don't simply store a pointer to it.
		AsciiStringVec			m_asciiStringVec;

		void						stripSpaces ( WideChar *string );
		void						reverseWord ( Char *file, Char *lp );
		void						translateCopy( WideChar *outbuf, Char *inbuf );
		Bool						loadCSF( const Char *filename );
		Bool						loadStringFile( const Char *filename, StringInfo *&table, Int &count, Bool filterLanguage );
};

static int _cdecl			compareLUT ( const void *,  const void*);
//----------------------------------------------------------------------------
//         Private Data                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Public Data                                                      
//----------------------------------------------------------------------------


//----------------------------------------------------------------------------
//         Private Prototypes                                               
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Functions                                               
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Public Functions                                                
//----------------------------------------------------------------------------

//============================================================================
// CreateGameTextInterface
//============================================================================

GameTextInterface* CreateGameTextInterface( void )
{
	return NEW GameTextManager;
}


//============================================================================
// GameTextManager::GameTextManager
//============================================================================

GameTextManager::GameTextManager()
:	m_textCount(0),
	m_maxLabelLen(0),
	m_stringInfo(nullptr),
	m_stringLUT(nullptr),
	m_initialized(FALSE),
	m_noStringList(nullptr),
#if defined(_DEBUG)
	m_jabberWockie(FALSE),
	m_munkee(FALSE),
	m_useStringFile(g_useStringFile),
#else
	m_useStringFile(TRUE),
#endif
	m_mapStringInfo(nullptr),
	m_mapStringLUT(nullptr),
	m_projectStringInfo(nullptr),
	m_projectStringLUT(nullptr),
	m_projectTextCount(0),
	m_failed(L"***FATAL*** String Manager failed to initilaize properly")
{
	// Added By Sadullah Nader
	// Initializations missing and needed

	for(Int i=0; i < MAX_UITEXT_LENGTH; i++)
	{
		m_buffer2[i] = 0; 
	}
	//
}

//============================================================================
// GameTextManager::~GameTextManager
//============================================================================

GameTextManager::~GameTextManager()
{
	deinit();
}							

//============================================================================
// GameTextManager::init
//============================================================================

extern const Char *g_strFile;
extern const Char *g_csfFile;

void GameTextManager::init( void )
{
	AsciiString csfFile;
	csfFile.format(g_csfFile, GetGameLanguage().str());

	if ( m_initialized )
	{
		return;
	}

	m_initialized = TRUE;

	m_maxLabelLen = 0;
#if defined(_DEBUG)
	if(TheGlobalData)
	{
		m_jabberWockie = TheGlobalData->m_jabberOn;
		m_munkee = 	TheGlobalData->m_munkeeOn;
	}
#endif

	// The text form is used when it exists; a damaged one leaves the game without texts, as before.
	if ( m_useStringFile && TheFileSystem->doesFileExist( g_strFile ) )
	{
		if ( !loadStringFile( g_strFile, m_stringInfo, m_textCount, FALSE ) )
		{
			deinit();
			return;
		}
	}
	else if ( !loadCSF( csfFile.str() ) )
	{
		deinit();
		return;
	}

	if( m_textCount == 0 )
	{
		return;
	}

	m_stringLUT = NEW StringLookUp[m_textCount];

	StringLookUp *lut = m_stringLUT;
	StringInfo *info = m_stringInfo;

	for ( Int i = 0; i < m_textCount; i++ )
	{
		lut->info = info;
		lut->label = &info->label;
		lut++;
		info++;
	}

	qsort( m_stringLUT, m_textCount, sizeof(StringLookUp), compareLUT  );

	loadProjectStrings();

	// The window keeps the project title from WinMain, not EA's product name from the game text.
}

//============================================================================
// GameTextManager::deinit
//============================================================================

void GameTextManager::deinit( void )
{

	if( m_stringInfo != nullptr )
	{
		delete [] m_stringInfo;
		m_stringInfo = nullptr;
	}

	if( m_stringLUT != nullptr )
	{
		delete [] m_stringLUT;
		m_stringLUT = nullptr;
	}

	m_textCount = 0;

	delete [] m_projectStringInfo;
	m_projectStringInfo = nullptr;
	delete [] m_projectStringLUT;
	m_projectStringLUT = nullptr;
	m_projectTextCount = 0;

	NoString *noString = m_noStringList;

	DEBUG_LOG(("\n*** Missing strings ***\n"));
	while ( noString )
	{
		DEBUG_LOG(("*** %ls ***\n", noString->text.str()));
		NoString *next = noString->next;
		delete noString;
		noString = next;
	}
	DEBUG_LOG(("*** End missing strings ***\n\n"));

	m_noStringList = nullptr;

	m_initialized = FALSE;
}

//============================================================================
// GameTextManager::reset
//============================================================================

void GameTextManager::reset( void )
{
	if( m_mapStringInfo != nullptr )
	{
		delete [] m_mapStringInfo;
		m_mapStringInfo = nullptr;
	}

	if( m_mapStringLUT != nullptr )
	{
		delete [] m_mapStringLUT;
		m_mapStringLUT = nullptr;
	}
}


//============================================================================
// GameTextManager::stripSpaces 
//============================================================================

void GameTextManager::stripSpaces ( WideChar *string )
{
	WideChar *str, *ptr;
	WideChar ch, last = 0;
	Int skipall = TRUE;

	str = ptr = string;

	while ( (ch = *ptr++) != 0 )
	{
		if ( ch == ' '  )
		{
			if ( last == ' ' || skipall )
			{
				continue;
			}
		}

		if ( ch == '\n' || ch == '\t' )
		{
				// remove last space
				if ( last == ' ' )
				{
					str--;
				}

				skipall = TRUE;		// skip all spaces
				last = *str++ = ch;
				continue;
		}

		last = *str++ = ch;
		skipall = FALSE;
	}

	if ( last == ' ' )
	{
		str--;
	}

	*str = 0;
}

//============================================================================
// GameTextManager::reverseWord 
//============================================================================

void GameTextManager::reverseWord ( Char *file, Char *lp )
{
	Int first = TRUE;
	Char f, l;
	Int ok = TRUE	;

	while ( ok )
	{
		if ( file >= lp )
		{
			return;
		}

		f = *file;
		l = *lp;

		if ( first )
		{
			if ( f >= 'A' && f <= 'Z' )
			{
				if ( l >= 'a' && l <= 'z' )
				{
					f = (f - 'A') + 'a';
					l = (l - 'a') + 'A';
				}
			}

			first = FALSE;
		}

		*lp-- = f;
		*file++ = l;

	}

}

//============================================================================
// GameTextManager::translateCopy
//============================================================================

void GameTextManager::translateCopy( WideChar *outbuf, Char *inbuf )
{
	Int slash = FALSE;

#if defined(_DEBUG)
	if ( m_jabberWockie )
	{
		static Char buffer[MAX_UITEXT_LENGTH*2];
		Char *firstLetter = nullptr, *lastLetter;
		Char *b = buffer;
		Int formatWord = FALSE;
		Char ch;

		while ( (ch = *inbuf++) != 0 )
		{
			if ( ! (( ch >= 'a' && ch <= 'z') || ( ch >= 'A' && ch <= 'Z' )))
			{
				if ( firstLetter )
				{
					if ( !formatWord )
					{
						lastLetter = b-1;
						reverseWord ( firstLetter, lastLetter );
					}
					firstLetter = nullptr;
					formatWord = FALSE;
				}
				*b++ = ch;
				if ( ch == '\\' )
				{
					*b++ = *inbuf++;
				}
				if ( ch == '%' )
				{
					while ( (ch = *inbuf++) != 0 && !( (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')))
					{
						*b++ = ch;
					}
					*b++ = ch;
				}
			}
			else
			{
				if ( !firstLetter )
				{
					firstLetter = b;
				}

				*b++ = ch;

			}
		}

		if ( firstLetter )
		{
			lastLetter = b-1;
			reverseWord ( firstLetter, lastLetter );
		}

		*b++ = 0;
		inbuf = buffer;
	}
	else if( m_munkee )
	{
		wcscpy(outbuf, L"Munkee");
		return;
	}
#endif

	while( *inbuf != '\0' )
	{
		if( slash == TRUE )
		{
			slash = FALSE;

			switch( *inbuf )
			{
				// in case end of string is reached
				// should never happen!!!
				case '\0':
					return;

				case '\\':
					*outbuf++ = '\\';
					break;

				case '\'':
					*outbuf++ = '\'';
					break;

				case '\"':
					*outbuf++ = '\"';
					break;

				case '\?':
					*outbuf++ = '\?';
					break;

				case 't':
					*outbuf++ = '\t';
					break;

				case 'n':
					*outbuf++ = '\n';
					break;

				default:
					*outbuf++ = *inbuf & 0x00FF;
					break;
			}
		}
		else if( *inbuf != '\\' )
		{
			*outbuf++ = *inbuf & 0x00FF;
		}
		else
			slash = TRUE;

		inbuf++;
	}
	*outbuf= 0;
}

//============================================================================
// readWholeFile
//============================================================================

// FALSE if the file does not exist.
static Bool readWholeFile( const Char *filename, std::vector<char> &data )
{
	File *file = TheFileSystem->openFile( filename, File::READ | File::BINARY );
	if ( file == nullptr )
	{
		return FALSE;
	}
	const Int size = file->size();
	data.resize( size > 0 ? size : 0 );
	const Int got = data.empty() ? 0 : file->read( data.data(), size );
	data.resize( got > 0 ? got : 0 );
	file->close();
	return TRUE;
}

//============================================================================
// GameTextManager::loadCSF
//============================================================================

// Compiled string file; every length and count in it is checked by Engine::readCsf.
Bool GameTextManager::loadCSF( const Char *filename )
{
	DEBUG_LOG(("Looking in %s for compiled string file\n", filename));
	std::vector<char> data;
	if ( !readWholeFile( filename, data ) )
	{
		return FALSE;
	}

	int version = 0, language = -1;
	std::vector<Engine::CsfEntry> entries;
	const Engine::CsfError error = Engine::readCsf( reinterpret_cast<const unsigned char *>(data.data()), data.size(), version, language, entries );
	if ( error != Engine::CsfError::None )
	{
		DEBUG_CRASH(( "String file %s not used: %s", filename, Engine::csfErrorText( error ) ));
		return FALSE;
	}
	m_language = version >= 2 ? static_cast<LanguageID>(language) : LANGUAGE_ID_US;

	m_textCount = static_cast<Int>(entries.size());
	m_stringInfo = NEW StringInfo[m_textCount];
	for ( Int i = 0; i < m_textCount; i++ )
	{
		const Engine::CsfEntry &entry = entries[i];
		m_stringInfo[i].label = entry.label.c_str();
		m_maxLabelLen = std::max( m_maxLabelLen, static_cast<Int>(entry.label.size()) );

		// the reader limits a text to fewer characters than m_tbuffer holds
		size_t n = 0;
		for ( char16_t c : entry.text )
		{
			m_tbuffer[n++] = static_cast<WideChar>(c);
		}
		m_tbuffer[n] = 0;
		stripSpaces( m_tbuffer );
		m_stringInfo[i].text = m_tbuffer;
		m_stringInfo[i].speech = entry.speech.c_str();
	}
	return TRUE;
}

//============================================================================
// GameTextManager::loadStringFile
//============================================================================

// Text form (also map.str from maps); read by Engine::readStr. Returns FALSE if the file is
// missing or ends inside a label; table and count then hold what was read.
Bool GameTextManager::loadStringFile( const Char *filename, StringInfo *&table, Int &count, Bool filterLanguage )
{
	DEBUG_LOG(("Looking in %s for string file\n", filename));
	std::vector<char> data;
	std::vector<Engine::StrEntry> entries;
	const Bool exists = readWholeFile( filename, data );
	const Bool complete = exists && Engine::readStr( data.data(), data.size(), entries );
	DEBUG_ASSERTCRASH( !exists || complete, ("Unexpected end of string file %s", filename) );

	count = static_cast<Int>(entries.size());
	table = NEW StringInfo[count];
	for ( Int i = 0; i < count; i++ )
	{
		const Engine::StrEntry &entry = entries[i];
#if defined(_DEBUG)
		for ( Int j = 0; j < i; j++ )
		{
			DEBUG_ASSERTCRASH( stricmp( table[j].label.str(), entry.label.c_str() ) != 0, ("String label '%s' multiply defined!", entry.label.c_str()) );
		}
#endif
		DEBUG_ASSERTCRASH( entry.extraTexts == 0, ("String label '%s' has more than one string defined!", entry.label.c_str()) );
		table[i].label = entry.label.c_str();
		m_maxLabelLen = std::max( m_maxLabelLen, static_cast<Int>(entry.label.size()) );
		if ( !entry.hasText )
		{
			continue;
		}

		// the reader limits a text to fewer characters than m_buffer2 holds; translateCopy writes
		// at most one character per input character into m_tbuffer
		safe_strcpy( m_buffer2, entry.text.c_str() );
		translateCopy( m_tbuffer, m_buffer2 );
		stripSpaces( m_tbuffer );
		UnicodeString text( m_tbuffer );
		if ( filterLanguage && TheLanguageFilter )
		{
			TheLanguageFilter->filterLine( text );
		}
		table[i].text = text;
		table[i].speech = entry.speech.c_str();
	}
	return complete;
}

//============================================================================
// GameTextManager::loadProjectStrings
//============================================================================

// The game language may carry a digit (Generals: german2); the project files are named without it.
// A language without its own file falls back to English.
void GameTextManager::loadProjectStrings( void )
{
	AsciiString language = GetGameLanguage();
	language.toLower();
	while ( language.getLength() > 0 && isdigit( static_cast<unsigned char>( language.getCharAt( language.getLength() - 1 ) ) ) )
		language.removeLastChar();

	AsciiString file;
	file.format( "ProjectData\\Text\\%s.str", language.str() );
	if ( !TheFileSystem->doesFileExist( file.str() ) )
		file = "ProjectData\\Text\\english.str";
	if ( !TheFileSystem->doesFileExist( file.str() ) )
		return;

	loadStringFile( file.str(), m_projectStringInfo, m_projectTextCount, FALSE );
	m_projectStringLUT = NEW StringLookUp[m_projectTextCount];
	for ( Int i = 0; i < m_projectTextCount; i++ )
	{
		m_projectStringLUT[i].info = &m_projectStringInfo[i];
		m_projectStringLUT[i].label = &m_projectStringInfo[i].label;
	}
	qsort( m_projectStringLUT, m_projectTextCount, sizeof(StringLookUp), compareLUT );
}

//============================================================================
// GameTextManager::initMapStringFile
//============================================================================

void GameTextManager::initMapStringFile( const AsciiString& filename )
{
	m_mapTextCount = 0;
	loadStringFile( filename.str(), m_mapStringInfo, m_mapTextCount, TRUE );

	m_mapStringLUT = NEW StringLookUp[m_mapTextCount];

	StringLookUp *lut = m_mapStringLUT;
	StringInfo *info = m_mapStringInfo;

	for ( Int i = 0; i < m_mapTextCount; i++ )
	{
		lut->info = info;
		lut->label = &info->label;
		lut++;
		info++;
	}

	qsort( m_mapStringLUT, m_mapTextCount, sizeof(StringLookUp), compareLUT  );
}

//============================================================================
// *GameTextManager::fetch
//============================================================================

UnicodeString GameTextManager::fetch( const Char *label, Bool *exists )
{
	DEBUG_ASSERTCRASH ( m_initialized, ("String Manager has not been m_initialized") );

	if( m_stringInfo == nullptr )
	{
		if( exists )
			*exists = FALSE;
		return m_failed;
	}

	StringLookUp *lookUp;
	StringLookUp key;
	AsciiString lb;
	lb = label;
	key.info = nullptr;
	key.label = &lb;

	lookUp = static_cast<StringLookUp *>(bsearch( &key, reinterpret_cast<void*>(m_stringLUT), m_textCount, sizeof(StringLookUp), compareLUT ));

	if ( lookUp == nullptr && m_mapStringLUT && m_mapTextCount )
	{
		lookUp = static_cast<StringLookUp *>(bsearch( &key, reinterpret_cast<void*>(m_mapStringLUT), m_mapTextCount, sizeof(StringLookUp), compareLUT ));
	}

	if ( lookUp == nullptr && m_projectStringLUT && m_projectTextCount )
	{
		lookUp = static_cast<StringLookUp *>(bsearch( &key, reinterpret_cast<void*>(m_projectStringLUT), m_projectTextCount, sizeof(StringLookUp), compareLUT ));
	}

	if( lookUp == nullptr )
	{

		// string not found
		if( exists )
			*exists = FALSE;

		// See if we already have the missing string
		UnicodeString missingString;
		missingString.format(L"MISSING: '%hs'", label);

		NoString *noString = m_noStringList;

		while ( noString )
		{
			if (noString->text == missingString)
				return missingString;

			noString = noString->next;
		}

		//DEBUG_LOG(("*** MISSING:'%s' ***\n", label));
		// Remember file could have been altered at this point.
		noString = NEW NoString;
		noString->text = missingString;
		noString->next = m_noStringList;
		m_noStringList = noString;
		return noString->text;
	}
	if( exists )	
		*exists = TRUE;
	return lookUp->info->text;
}

//============================================================================
// *GameTextManager::fetch
//============================================================================

UnicodeString GameTextManager::fetch( AsciiString label, Bool *exists )
{
	return fetch(label.str(), exists);
}

//============================================================================
// GameTextManager::getStringsWithLabelPrefix
//============================================================================

AsciiStringVec& GameTextManager::getStringsWithLabelPrefix(AsciiString label)
{
	m_asciiStringVec.clear();
	if (m_stringLUT) {
		for (int i = 0; i < m_textCount; ++i) {		
			if (strstr(m_stringLUT[i].label->str(), label.str()) == m_stringLUT[i].label->str()) {
				m_asciiStringVec.push_back(*m_stringLUT[i].label);
			}
		}
	}
	if (m_mapStringLUT) {
		for (int i = 0; i < m_mapTextCount; ++i) {		
			if (strstr(m_mapStringLUT[i].label->str(), label.str()) == m_mapStringLUT[i].label->str()) {
				m_asciiStringVec.push_back(*m_mapStringLUT[i].label);
			}
		}
	}
	return m_asciiStringVec;
}

//============================================================================
// GameTextManager::compareLUT 
//============================================================================

static int __cdecl compareLUT ( const void *i1,  const void*i2)
{
	StringLookUp *lut1 = (StringLookUp*) i1;
	StringLookUp *lut2 = (StringLookUp*) i2;

	return stricmp( lut1->label->str(), lut2->label->str());
}
