/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// gpuadapter.h -- which graphics card a stored choice means.
//
// The choice (Settings.ini [Display] Adapter, -adapter) is not an index: the order of the adapters changes with
// drivers and docking stations. It is stored as "VVVV:DDDD name" (hexadecimal vendor and device identifier, then
// the name for the reader). Empty means automatic. For tests and by hand the choice may also be the number of the
// adapter in the list or part of its name. No device, no API: tested by RenderUnitTests.

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef GPUADAPTER_H
#define GPUADAPTER_H

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

namespace GpuAdapter
{

struct Candidate
{
	unsigned	vendor_id;
	unsigned	device_id;
	const char*	name;
};

/// "VVVV:DDDD name", the form that is stored.
inline std::string Spec( unsigned vendor_id, unsigned device_id, const char* name )
{
	char text[320];
	snprintf( text, sizeof( text ), "%04X:%04X %s", vendor_id & 0xFFFFu, device_id & 0xFFFFu, name ? name : "" );
	return std::string( text );
}

inline bool Contains_No_Case( const char* text, const char* part )
{
	const size_t length = strlen( part );
	if( length == 0 )
		return false;
	for( ; *text; ++text )
	{
		size_t i = 0;
		while( i < length && text[i] && tolower( static_cast<unsigned char>( text[i] ) ) == tolower( static_cast<unsigned char>( part[i] ) ) )
			++i;
		if( i == length )
			return true;
	}
	return false;
}

/// Index of the adapter the choice means, -1 for automatic (empty choice) or an adapter that is not there.
inline int Choose( const char* choice, const Candidate* list, unsigned count )
{
	if( choice == nullptr || list == nullptr )
		return -1;
	while( *choice == ' ' || *choice == '\t' )
		++choice;
	if( *choice == 0 )
		return -1;

	// identifiers: "VVVV:DDDD", followed by nothing or a blank and the name
	unsigned vendor = 0, device = 0;
	int used = 0;
	if( sscanf( choice, "%x:%x%n", &vendor, &device, &used ) == 2 && used > 0 &&
		( choice[used] == 0 || choice[used] == ' ' ) )
	{
		for( unsigned i = 0; i < count; ++i )
		{
			if( list[i].vendor_id == vendor && list[i].device_id == device )
				return static_cast<int>( i );
		}
		return -1;
	}

	// a number: position in the list
	bool digits = true;
	for( const char* c = choice; *c; ++c )
		digits = digits && isdigit( static_cast<unsigned char>( *c ) );
	if( digits )
	{
		const unsigned long index = strtoul( choice, nullptr, 10 );
		return index < count ? static_cast<int>( index ) : -1;
	}

	// part of the name
	for( unsigned i = 0; i < count; ++i )
	{
		if( list[i].name && Contains_No_Case( list[i].name, choice ) )
			return static_cast<int>( i );
	}
	return -1;
}

}  // namespace GpuAdapter

#endif // GPUADAPTER_H
