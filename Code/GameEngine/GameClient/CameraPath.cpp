/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: CameraPath.cpp ///////////////////////////////////////////////////////////////////////////
// Camera path for image comparisons.
//
// File format, one line per key, '#' starts a comment:
//   frame  x  y  zoom  angle  pitch
// frame    logic frame, ascending
// x, y     map-relative 0..1 over TheTerrainLogic->getExtent()
// zoom     engine zoom (0.2 near .. 1.3 far, the engine clamps), 'standard', or 'hoehe<k>': k times the
//          normal maximum height above ground (needs -maxZoom k or more, otherwise clamped like the player)
// angle    degrees relative to the map's default angle, or 'standard' (= 0)
// pitch    degrees (the engine clamps to +-36), or 'standard' (map default)
// Values are interpolated linearly between two keys; before the first and after the last key that
// key's pose applies. The angle is not normalized: 0 -> 360 is a full turn.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "GameClient/CameraPath.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/TerrainLogic.h"

namespace
{

/// A value from the file: a number or 'standard' (engine default, only known at runtime)
struct PathValue
{
	Bool isDefault;
	Real value;
	Real heightFactor;	///< 'hoehe<k>': k, else 0
};

struct PathKey
{
	UnsignedInt frame;
	Real x;
	Real y;
	PathValue zoom;
	PathValue angle;
	PathValue pitch;
};

enum class PathState
{
	PATH_UNLOADED,
	PATH_READY,
	PATH_FAILED
};

// Fixed array instead of std::vector: a static vector frees its memory only after the engine's leak
// report and would show up there as a leak.
enum { MAX_PATH_KEYS = 256 };

PathState s_state = PathState::PATH_UNLOADED;
PathKey s_keys[MAX_PATH_KEYS];
Int s_keyCount = 0;

void runnerLog( const char *fmt, ... )
{
	char line[MAX_PATH + 96];
	va_list args;
	va_start( args, fmt );
	_vsnprintf( line, sizeof(line) - 1, fmt, args );
	va_end( args );
	line[sizeof(line) - 1] = 0;
	GameEngine_RunnerLog( line );
	DEBUG_LOG(( "%s\n", line ));
}

Bool parseValue( const char *tok, PathValue *out )
{
	out->heightFactor = 0.0f;
	if( _stricmp( tok, "standard" ) == 0 )
	{
		out->isDefault = TRUE;
		out->value = 0.0f;
		return TRUE;
	}
	if( _strnicmp( tok, "hoehe", 5 ) == 0 )
	{
		char *end = nullptr;
		out->isDefault = TRUE;
		out->value = 0.0f;
		out->heightFactor = static_cast<Real>(strtod( tok + 5, &end ));
		return end != tok + 5 && *end == 0 && out->heightFactor > 0.0f;
	}
	char *end = nullptr;
	out->isDefault = FALSE;
	out->value = static_cast<Real>(strtod( tok, &end ));
	return end != tok && *end == 0;
}

Bool parseReal( const char *tok, Real *out )
{
	char *end = nullptr;
	*out = static_cast<Real>(strtod( tok, &end ));
	return end != tok && *end == 0;
}

/// Read the file; on an error it reports file and line and the camera path stays off.
Bool loadPath( const char *file )
{
	FILE *fp = fopen( file, "r" );
	if( fp == nullptr )
	{
		runnerLog( "CAMERAPATH ERROR: %s not readable", file );
		return FALSE;
	}

	char buf[512];
	Int lineNo = 0;
	Bool ok = TRUE;
	while( ok && fgets( buf, sizeof(buf), fp ) )
	{
		++lineNo;
		char *hash = strchr( buf, '#' );
		if( hash )
			*hash = 0;

		const char *tok[6];
		Int count = 0;
		for( char *t = strtok( buf, " \t\r\n" ); t != nullptr; t = strtok( nullptr, " \t\r\n" ) )
		{
			if( count == 6 )
			{
				count = 7;
				break;
			}
			tok[count++] = t;
		}
		if( count == 0 )
			continue;

		PathKey key;
		char *end = nullptr;
		const long frame = strtol( tok[0], &end, 10 );
		ok = count == 6 && end != tok[0] && *end == 0 && frame >= 0
			&& parseReal( tok[1], &key.x ) && parseReal( tok[2], &key.y )
			&& parseValue( tok[3], &key.zoom ) && parseValue( tok[4], &key.angle ) && parseValue( tok[5], &key.pitch );
		key.frame = static_cast<UnsignedInt>(frame);
		if( ok && s_keyCount > 0 && key.frame <= s_keys[s_keyCount - 1].frame )
			ok = FALSE;	// frames must be strictly ascending
		if( ok && s_keyCount == MAX_PATH_KEYS )
			ok = FALSE;	// too many keys
		if( ok )
			s_keys[s_keyCount++] = key;
		else
			runnerLog( "CAMERAPATH ERROR: %s line %d", file, lineNo );
	}
	fclose( fp );

	if( ok && s_keyCount == 0 )
	{
		runnerLog( "CAMERAPATH ERROR: %s contains no key point", file );
		ok = FALSE;
	}
	if( ok )
		runnerLog( "CAMERAPATH %s with %d key points", file, s_keyCount );
	else
		s_keyCount = 0;
	return ok;
}

Real resolve( const PathValue &v, Real engineDefault )
{
	return v.isDefault ? engineDefault : v.value;
}

}  // namespace

//-------------------------------------------------------------------------------------------------
Bool CameraPath_GetFrameRange( UnsignedInt *first, UnsignedInt *last )
{
	if( s_state != PathState::PATH_READY )
		return FALSE;
	*first = s_keys[0].frame;
	*last = s_keys[s_keyCount - 1].frame;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void CameraPath_Update( void )
{
	if( TheGlobalData == nullptr || TheGlobalData->m_cameraPathFile.isEmpty() || s_state == PathState::PATH_FAILED )
		return;
	if( TheGameLogic == nullptr || !TheGameLogic->isInGame() || TheTacticalView == nullptr || TheTerrainLogic == nullptr )
		return;

	if( s_state == PathState::PATH_UNLOADED )
	{
		s_state = loadPath( TheGlobalData->m_cameraPathFile.str() ) ? PathState::PATH_READY : PathState::PATH_FAILED;
		if( s_state == PathState::PATH_FAILED )
			return;
	}

	// find the keys around the current frame
	const UnsignedInt frame = TheGameLogic->getFrame();
	Int next = 0;
	while( next < s_keyCount && s_keys[next].frame <= frame )
		++next;
	const PathKey &a = s_keys[next > 0 ? next - 1 : 0];
	const PathKey &b = s_keys[next < s_keyCount ? next : s_keyCount - 1];
	Real t = 0.0f;
	if( b.frame > a.frame && frame > a.frame )
		t = static_cast<Real>(frame - a.frame) / static_cast<Real>(b.frame - a.frame);

	Region3D extent;
	TheTerrainLogic->getExtent( &extent );
	if( extent.hi.x <= extent.lo.x || extent.hi.y <= extent.lo.y )
		return;	// no map loaded yet
	Coord3D pos;
	pos.x = extent.lo.x + (a.x + (b.x - a.x) * t) * (extent.hi.x - extent.lo.x);
	pos.y = extent.lo.y + (a.y + (b.y - a.y) * t) * (extent.hi.y - extent.lo.y);
	pos.z = 0.0f;

	// Get the engine defaults for this position, then set the pose. The setters also end running
	// script camera moves, so the camera path has the last word.
	View *view = TheTacticalView;
	view->lookAt( &pos );
	view->setAngleAndPitchToDefault();
	const Real defAngle = view->getAngle();
	const Real defPitch = view->getPitch();
	view->setZoomToDefault();
	const Real defZoom = view->getZoom();

	const Real degToRad = PI / 180.0f;
	const Real angleA = resolve( a.angle, 0.0f ), angleB = resolve( b.angle, 0.0f );
	const Real pitchA = a.pitch.isDefault ? defPitch : a.pitch.value * degToRad;
	const Real pitchB = b.pitch.isDefault ? defPitch : b.pitch.value * degToRad;
	const Real zoomA = resolve( a.zoom, defZoom ), zoomB = resolve( b.zoom, defZoom );

	view->setAngle( defAngle + (angleA + (angleB - angleA) * t) * degToRad );
	view->setPitch( pitchA + (pitchB - pitchA) * t );
	if( a.zoom.heightFactor > 0.0f || b.zoom.heightFactor > 0.0f )
	{
		// Height factor: the player's height above ground (clamped by the zoom-out limit) and the zoom that
		// reaches it; setZoom clamps as well, the height settling of the view does the rest.
		const Real hA = a.zoom.heightFactor > 0.0f ? a.zoom.heightFactor : 1.0f;
		const Real hB = b.zoom.heightFactor > 0.0f ? b.zoom.heightFactor : 1.0f;
		const Real k = hA + (hB - hA) * t;
		const Real normalHeight = view->getHeightAboveGround();
		view->setHeightAboveGround( normalHeight * k );
		const Real cameraHeight = TheGlobalData->m_cameraHeight > 0.0f ? TheGlobalData->m_cameraHeight : 1.0f;
		view->setZoom( defZoom + (view->getHeightAboveGround() - normalHeight) / cameraHeight );
	}
	else
		view->setZoom( zoomA + (zoomB - zoomA) * t );
}
