/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: InstallInfo.cpp /////////////////////////////////////////////////////////////////////////
// Installation data without the registry.
//
// Only the language (paths Data\<language>\...) and the version shown in the options menu are
// needed. The language comes, in this order, from
//   1. Options.ini in the user folder, entry "Language = german" (like the other options),
//   2. the archive names next to the exe: Generals keeps the language data in <language>.big
//      (German2.big), Zero Hour in <language>ZH.big (GermanZH.big) -- the same values EA's
//      installer wrote to the registry,
//   3. "english".
// The version is fixed: the last official patches (Generals 1.08, Zero Hour 1.04), the same
// numbers their installers wrote as "Version".
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/InstallInfo.h"
#include "Common/GameEdition.h"
#include "Common/GlobalData.h"
#include "Engine/Settings.h"

#include <string>
#include "Engine/UserDataFiles.h"

namespace
{

// Language names of the EA localizations (folder names under Data\).
const char *const s_knownLanguages[] =
{
	"english", "german", "german2", "french", "spanish", "italian", "korean", "chinese",
	"brazilian", "polish", "swedish", "norwegian", "dutch", "danish", "finnish", "russian",
	"japanese", "czech", "portuguese",
};

Bool isKnownLanguage( const char *s )
{
	for ( const char *language : s_knownLanguages )
		if ( _stricmp( s, language ) == 0 )
			return TRUE;
	return FALSE;
}

void copyString( char *target, size_t size, const char *source )
{
	strncpy( target, source, size - 1 );
	target[ size - 1 ] = 0;
	for ( char *p = target; *p; ++p )
		*p = static_cast<char>(tolower( static_cast<unsigned char>(*p) ));
}

// 1a. Settings.ini: [General] Language. Read here directly because the language is needed before
// the preference classes exist.
Bool fromSettingsIni( char *language, size_t size, Bool *haveSettings )
{
	*haveSettings = FALSE;
	if ( TheGlobalData == nullptr || TheGlobalData->getPath_UserData().isEmpty() )
		return FALSE;
	AsciiString path = TheGlobalData->getPath_UserData();
	path.concat( "Settings.ini" );
	FILE *file = ::Engine::openNarrowFile( path.str(), "rb" );
	if ( file == nullptr )
		return FALSE;
	*haveSettings = TRUE;
	std::string text;
	char buffer[ 4096 ];
	size_t got;
	while ( ( got = fread( buffer, 1, sizeof( buffer ), file ) ) > 0 )
		text.append( buffer, got );
	fclose( file );
	::Engine::Settings::Document document;
	document.parse( text );
	const std::string *value = document.find( "General", "Language" );
	if ( value == nullptr || value->empty() )
		return FALSE;
	copyString( language, size, value->c_str() );
	return TRUE;
}

// 1b. Options.ini, as long as there is no Settings.ini: "Language = <language>" (key = value). Without one in the
// user folder, the one of EA's original game, which the first start takes over.
Bool fromOptionsIni( char *language, size_t size )
{
	if ( TheGlobalData == nullptr || TheGlobalData->getPath_UserData().isEmpty() )
		return FALSE;
	AsciiString path = TheGlobalData->getPath_UserData();
	path.concat( "Options.ini" );
	FILE *file = ::Engine::openNarrowFile( path.str(), "r" );
	if ( file == nullptr && TheGlobalData->getPath_EAUserData().isNotEmpty() )
	{
		path = TheGlobalData->getPath_EAUserData();
		path.concat( "Options.ini" );
		file = ::Engine::openNarrowFile( path.str(), "r" );
	}
	if ( file == nullptr )
		return FALSE;
	Bool foundAny = FALSE;
	char lineText[ 256 ];
	while ( !foundAny && fgets( lineText, sizeof( lineText ), file ) )
	{
		char *equals = strchr( lineText, '=' );
		if ( equals == nullptr )
			continue;
		*equals = 0;
		char *key = lineText;
		char *value = equals + 1;
		while ( *key == ' ' || *key == '\t' ) ++key;
		for ( char *e = key + strlen( key ); e > key && ( e[-1] == ' ' || e[-1] == '\t' ); ) *--e = 0;
		while ( *value == ' ' || *value == '\t' ) ++value;
		for ( char *e = value + strlen( value ); e > value && ( e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n' ); ) *--e = 0;
		if ( _stricmp( key, "Language" ) == 0 && *value )
		{
			copyString( language, size, value );
			foundAny = TRUE;
		}
	}
	fclose( file );
	return foundAny;
}

// 2. Archive names in the working directory (= exe folder): <language>.big or <language>ZH.big.
Bool fromArchiveNames( char *language, size_t size )
{
	WIN32_FIND_DATAA found;
	HANDLE searchPattern = FindFirstFileA( "*.big", &found );
	if ( searchPattern == INVALID_HANDLE_VALUE )
		return FALSE;
	Bool foundAny = FALSE;
	do
	{
		char name[ MAX_PATH ];
		copyString( name, sizeof( name ), found.cFileName );
		size_t length = strlen( name );
		if ( length < 5 )
			continue;
		name[ length - 4 ] = 0;			// strip ".big"
		length -= 4;
		if ( length > 2 && strcmp( name + length - 2, "zh" ) == 0 )
			name[ length - 2 ] = 0;		// Zero Hour: GermanZH -> german
		if ( isKnownLanguage( name ) )
		{
			copyString( language, size, name );
			foundAny = TRUE;
		}
	} while ( !foundAny && FindNextFileA( searchPattern, &found ) );
	FindClose( searchPattern );
	return foundAny;
}

}	// namespace

AsciiString GetGameLanguage( void )
{
	// Fixed buffer instead of a static AsciiString, whose buffer would show up in the leak report.
	// Cached only once Options.ini could be queried, so an early call before GlobalData (splash
	// screen, crash message) does not hide a later setting.
	static Bool remembered = FALSE;
	static char rememberedLanguage[ 64 ] = "english";
	if ( remembered )
		return AsciiString( rememberedLanguage );

	char language[ 64 ] = "english";
	Bool haveSettings = FALSE;
	Bool fromOptions = fromSettingsIni( language, sizeof( language ), &haveSettings );
	if ( !fromOptions && !haveSettings )
		fromOptions = fromOptionsIni( language, sizeof( language ) );
	if ( !fromOptions )
		fromArchiveNames( language, sizeof( language ) );
	if ( TheGlobalData != nullptr )
	{
		copyString( rememberedLanguage, sizeof( rememberedLanguage ), language );
		remembered = TRUE;
		DEBUG_LOG(( "GetGameLanguage: %s (%s)\n", language, fromOptions ? ( haveSettings ? "Settings.ini" : "Options.ini" ) : "archive names/default" ));
	}
	return AsciiString( language );
}

UnsignedInt GetGameVersion( void )
{
	// Last official patches: Generals 1.08, Zero Hour 1.04 (registry value "Version" 65544 or 65540).
	return isGameEditionZeroHour() ? 0x00010004 : 0x00010008;
}
