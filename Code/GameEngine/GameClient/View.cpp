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

// View.cpp ///////////////////////////////////////////////////////////////////
// A "view", or window, into the World
// Author: Michael S. Booth, February 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameEngine.h"
#include "Common/Xfer.h"
#include "GameClient/View.h"
#include "GameClient/Drawable.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameInfo.h"
#include "Common/Recorder.h"
#include "formatinto.h"

UnsignedInt View::m_idNext = 1;

// the tactical view singleton


View::View( void )
{
	//Added By Sadullah Nader
	//Initialization(s) inserted
	m_currentHeightAboveGround = 0.0f;
	m_defaultAngle = 0.0f;
	m_defaultPitchAngle = 0.0f;
	m_heightAboveGround = 0.0f;
	m_lockDist = 0.0f;
	m_maxHeightAboveGround = 0.0f;
	m_maxZoom = 0.0f;
	m_minHeightAboveGround = 0.0f;
	m_minZoom = 0.0f;
	m_next = nullptr;
	m_okToAdjustHeight = TRUE;
	m_originX = 0;
	m_originY = 0;
	m_snapImmediate = FALSE;
	m_terrainHeightUnderCamera = 0.0f;
	m_zoom = 0.0f;
	//
	m_pos.x = 0;
	m_pos.y = 0;
	m_width = 0;
	m_height = 0;
	m_angle = 0.0f;
	m_pitchAngle = 0.0f;
	m_cameraLock = INVALID_ID;
	m_cameraLockDrawable = nullptr;
	m_zoomLimited = TRUE;

	// create unique view ID
	m_id = m_idNext++;

	// default field of view
	m_FOV = 50.0f * PI/180.0f;
	
	m_mouseLocked = FALSE;
	
	m_guardBandBias.x = 0.0f;
	m_guardBandBias.y = 0.0f;
}

View::~View()
{
}

void View::init( void )
{
	m_width = DEFAULT_VIEW_WIDTH;
	m_height = DEFAULT_VIEW_HEIGHT;
	m_originX = DEFAULT_VIEW_ORIGIN_X;
	m_originY = DEFAULT_VIEW_ORIGIN_Y;
	m_pos.x = 0;
	m_pos.y = 0;
	m_angle = 0.0f;
	m_cameraLock = INVALID_ID;
	m_cameraLockDrawable = nullptr;
	m_zoomLimited = TRUE;
	
	m_maxZoom = 1.3f;
	m_minZoom = 0.2f;
	m_zoom = m_maxZoom;
	m_maxHeightAboveGround = TheGlobalData->m_maxCameraHeight;
	m_minHeightAboveGround = TheGlobalData->m_minCameraHeight;
	m_okToAdjustHeight = FALSE;

	m_defaultAngle = 0.0f;
	m_defaultPitchAngle = 0.0f;
}

void View::reset( void )
{
	// Only fixing the reported bug.  Who knows what side effects resetting the rest could have.
	m_zoomLimited = TRUE;
}

/**
 * Prepend this view to the given list, return the new list.
 */
View *View::prependViewToList( View *list )
{
	m_next = list;
	return this;
}

/**
 * The zoom-out factor of the running game: the local setting (Settings.ini, -maxZoom). In network games
 * the host's value from the game options is the upper limit, so nobody sees more of the map than the
 * others; replays and spectators of such a game read it from the same options (the replay header).
 */
Real View::getGameZoomOutFactor( void )
{
	Real factor = TheGlobalData ? TheGlobalData->m_maxZoomFactor : 1.0f;
	if( factor <= 1.0f )
		return 1.0f;
	if( TheGameLogic == nullptr )
		return factor;
	Bool network = TheGameLogic->isInMultiplayerGame();
	if( TheGameLogic->getGameMode() == GAME_REPLAY && TheRecorder )
	{
		const Int original = TheRecorder->getGameMode();
		network = (original == GAME_LAN || original == GAME_INTERNET);
	}
	if( network )
	{
		const Real limit = TheGameInfo ? TheGameInfo->getMaxZoomFactor() : 1.0f;
		if( factor > limit )
		{
			// runner and debug log, once for each game options object and limit; none before a replay's
			// options are read (camera calls while it loads)
			static const GameInfo *s_loggedGame = nullptr;
			static Real s_loggedLimit = 0.0f;
			if( TheGameInfo != nullptr && (s_loggedGame != TheGameInfo || s_loggedLimit != limit) )
			{
				s_loggedGame = TheGameInfo;
				s_loggedLimit = limit;
				char line[96];
				WWFormat::Into( line, "ZOOMLIMIT local {:.2f} host {:.2f}, using {:.2f}", factor, limit, limit );
				GameEngine_RunnerLog( line );
				DEBUG_LOG(( "%s\n", line ));
			}
			factor = limit;
		}
	}
	return (factor > 1.0f) ? factor : 1.0f;
}

/**
 * Only the player's free camera limit grows, in every game mode except the shell map; script camera
 * moves and cutscenes are never scaled.
 */
Real View::getZoomOutFactor( void ) const
{
	if( TheGameLogic == nullptr )
		return 1.0f;
	const Int mode = TheGameLogic->getGameMode();
	if( mode == GAME_SHELL || mode == GAME_NONE )
		return 1.0f;
	return getGameZoomOutFactor();
}

/**
 * Zoom for sizing health bars and icons. Up to the normal maximum height it is the camera zoom; above
 * it overlays shrink gently, (normal / height)^0.6 of their size at the normal height and at least 70 %,
 * instead of linearly with the height, so they stay readable.
 */
Real View::getOverlayZoom( void )
{
	const Real zoom = getZoom();
	if( getZoomOutFactor() <= 1.0f )
		return zoom;
	const Real normalMax = TheGlobalData->m_maxCameraHeight;
	const Real cameraHeight = TheGlobalData->m_cameraHeight;
	const Real height = getCurrentHeightAboveGround();
	if( normalMax <= 0.0f || cameraHeight <= 0.0f || height <= normalMax )
		return zoom;
	const Real zoomAtNormal = zoom - (height - normalMax) / cameraHeight;
	Real grow = static_cast<Real>(pow( height / normalMax, 0.6 ));
	const Real MAX_GROW = 1.0f / 0.7f;
	if( grow > MAX_GROW )
		grow = MAX_GROW;
	return zoomAtNormal * grow;
}

/**
 * Height change per mouse wheel step: 10 up to the normal maximum height, above it proportional to the
 * height, so the extra range does not take many more steps.
 */
static Real zoomStep( View *view )
{
	const Real step = 10.0f;
	const Real normalMax = TheGlobalData->m_maxCameraHeight;
	const Real height = view->getHeightAboveGround();
	if( view->getZoomOutFactor() <= 1.0f || normalMax <= 0.0f || height <= normalMax )
		return step;
	return step * height / normalMax;
}

void View::zoomIn( void )
{
	setHeightAboveGround(getHeightAboveGround() - zoomStep(this));
}

void View::zoomOut( void )
{
	setHeightAboveGround(getHeightAboveGround() + zoomStep(this));
}

void View::zoomBy( Real notches )
{
	setHeightAboveGround(getHeightAboveGround() - notches * zoomStep(this));
}

/**
 * Center the view on the given coordinate.
 */
void View::lookAt( const Coord3D *o ) 
{ 

	/// @todo this needs to be changed to be 3D, this is still old 2D stuff
	Coord3D pos = *getPosition();
	pos.x = o->x - m_width * 0.5f; 
	pos.y = o->y - m_height * 0.5f; 
	setPosition(&pos);
}

/**
 * Shift the view by the given delta.
 */
void View::scrollBy( Coord2D *delta ) 
{ 
	// update view's world position
	m_pos.x += delta->x;
	m_pos.y += delta->y;
}

/**
 * Rotate the view around the up axis by the given angle.
 */
void View::setAngle( Real angle )
{
	m_angle = angle; 
}

/**
 * Rotate the view around the horizontal (X) axis to the given angle.
 */
void View::setPitch( Real angle )
{
	m_pitchAngle = angle;

	Real limit = PI/5.0f;

	if (m_pitchAngle < -limit)
		m_pitchAngle = -limit;
	else if (m_pitchAngle > limit)
		m_pitchAngle = limit;
}

/**
 * Set the view angle back to default
 */
void View::setAngleAndPitchToDefault( void )
{ 
	m_angle = m_defaultAngle;
	m_pitchAngle = m_defaultPitchAngle;
}

/**
 * write the view's current location in to the view location object
 */
void View::getLocation( ViewLocation *location )
{

	const Coord3D *pos = getPosition();
	location->init( pos->x, pos->y, pos->z, getAngle(), getPitch(), getZoom() );

}


/**
 * set the view's current location from to the view location object
 */
void View::setLocation( const ViewLocation *location )
{
	if ( location->m_valid )
	{
		setPosition(&location->m_pos);
		setAngle(location->m_angle);
		setPitch(location->m_pitch);
		setZoom(location->m_zoom);
		forceRedraw();
	}

}

//-------------------------------------------------------------------------------------------------
/** project the 4 corners of this view into the world and return each point as a parameter,
		the world points are at the requested Z */
//-------------------------------------------------------------------------------------------------
void View::getScreenCornerWorldPointsAtZ( Coord3D *topLeft, Coord3D *topRight,
																					Coord3D *bottomLeft, Coord3D *bottomRight,
																					Real z )
{
	ICoord2D screenTopLeft, screenTopRight, screenBottomLeft, screenBottomRight;
	ICoord2D origin;
	Int viewWidth = getWidth();
	Int viewHeight = getHeight();

	// sanity
	if( topLeft == nullptr || topRight == nullptr || bottomLeft == nullptr || bottomRight == nullptr )
		return;

	// setup the screen coords for the 4 corners of the viewable display
	getOrigin( &origin.x, &origin.y );
	screenTopLeft.x     = origin.x;								// upper left
	screenTopLeft.y     = origin.y;								// upper left
	screenTopRight.x    = origin.x + viewWidth;		// upper right
	screenTopRight.y    = origin.y;								// upper right
	screenBottomLeft.x  = origin.x + viewWidth;		// lower right
	screenBottomLeft.y  = origin.y + viewHeight;  // lower right
	screenBottomRight.x = origin.x;								// lower left
	screenBottomRight.y = origin.y + viewHeight;	// lower left

	// project
	screenToWorldAtZ( &screenTopLeft, topLeft, z );
	screenToWorldAtZ( &screenTopRight, topRight, z );
	screenToWorldAtZ( &screenBottomLeft, bottomLeft, z );
	screenToWorldAtZ( &screenBottomRight, bottomRight, z );

}  // end getScreenCornerWorldPointsAtZ

// ------------------------------------------------------------------------------------------------
/** Xfer method for a view */
// ------------------------------------------------------------------------------------------------
void View::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// camera angle
	Real angle = getAngle();
	xfer->xferReal( &angle );
	setAngle( angle );

	// view position
	Coord3D viewPos;
	getPosition( &viewPos );
	xfer->xferReal( &viewPos.x );
	xfer->xferReal( &viewPos.y );
	xfer->xferReal( &viewPos.z );
	lookAt( &viewPos );

}  // end xfer
