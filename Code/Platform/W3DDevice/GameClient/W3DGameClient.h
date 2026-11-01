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

// FILE: W3DGameClient.h ///////////////////////////////////////////////////
//
// W3D implementation of the game interface.  The GameClient is 
// responsible for maintaining our drawbles, handling our GUI, and creating
// the display ... basically the Client if this were a Client/Server game.
//
// Author: Colin Day, April 2001
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __W3DGAMEINTERFACE_H_
#define __W3DGAMEINTERFACE_H_

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "GameClient/GameClient.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DInGameUI.h"
#include "W3DDevice/GameClient/W3DTerrainVisual.h"
#include "W3DDevice/GameClient/W3DGameWindowManager.h"
#include "W3DDevice/GameClient/W3DGameFont.h"
#include "W3DDevice/GameClient/W3DDisplayStringManager.h"
#if defined(GZH_NULL_MEDIA)
#include "NullMedia/NullVideoPlayer.h"
#else
#include "VideoDevice/FFmpeg/FFmpegVideoPlayer.h"
#endif
#include "Win32Device/GameClient/Win32Keyboard.h"
#include "Win32Device/GameClient/Win32Mouse.h"
#include "W3DDevice/GameClient/W3DMouse.h"
#include "W3DDevice/GameClient/W3DSnow.h"

class ThingTemplate;

#include "Engine/EngineContext.h"

///////////////////////////////////////////////////////////////////////////////
// PROTOTYPES /////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// W3DGameClient -----------------------------------------------------------
/** The W3DGameClient singleton */
//-----------------------------------------------------------------------------
class W3DGameClient : public GameClient
{

public:

	W3DGameClient();
	virtual ~W3DGameClient() override;

	/// given a type, create a drawable
	virtual Drawable *friend_createDrawable( const ThingTemplate *thing, DrawableStatus statusBits = DRAWABLE_STATUS_NONE ) override;

	virtual void init( void ) override;		///< initialize resources
	virtual void update( void ) override;  ///< per frame update
	virtual void reset( void ) override;   ///< reset system
		
	virtual void addScorch(const Coord3D *pos, Real radius, Scorches type) override;
	virtual void createRayEffectByTemplate( const Coord3D *start, const Coord3D *end, const ThingTemplate* tmpl ) override;  ///< create effect needing start and end location
	//virtual Bool getBonePos(Drawable *draw, AsciiString boneName, Coord3D* pos, Matrix3D* transform) const;

	virtual void setTimeOfDay( TimeOfDay tod ) override;							///< Tell all the drawables what time of day it is now

	//---------------------------------------------------------------------------
	virtual void setTeamColor( Int red, Int green, Int blue ) override;  ///< @todo superhack for demo, remove!!!
	virtual void adjustLOD( Int adj ) override; ///< @todo hack for evaluation, remove.
	virtual void notifyTerrainObjectMoved(Object *obj) override;

protected:

	virtual Keyboard *createKeyboard( void ) override;								///< factory for the keyboard
	virtual Mouse *createMouse( void ) override;											///< factory for the mouse

	/// factory for creating TheDisplay
	virtual Display *createGameDisplay( void ) override { return NEW W3DDisplay; }

	/// factory for creating TheInGameUI
	virtual InGameUI *createInGameUI( void ) override { return NEW W3DInGameUI; }	

	/// factory for creating the window manager
	virtual GameWindowManager *createWindowManager( void ) override { return NEW W3DGameWindowManager; }

	/// factory for creating the font library
	virtual FontLibrary *createFontLibrary( void ) override { return NEW W3DFontLibrary; }

  /// Manager for display strings
	virtual DisplayStringManager *createDisplayStringManager( void ) override { return NEW W3DDisplayStringManager; }

#if defined(GZH_NULL_MEDIA)
	// Build without FFmpeg (MinGW/Wine): no video plays.
	virtual VideoPlayerInterface *createVideoPlayer( void ) override { return NEW NullVideoPlayer; }
#else
	virtual VideoPlayerInterface *createVideoPlayer( void ) override { return NEW FFmpegVideoPlayer; }
#endif
	/// factory for creating the TerrainVisual
	virtual TerrainVisual *createTerrainVisual( void ) override { return NEW W3DTerrainVisual; }

	/// factory for creating the snow manager
	virtual SnowManager *createSnowManager( void ) override { return NEW W3DSnowManager; }

	virtual void setFrameRate(Real msecsPerFrame) override { TheW3DFrameLengthInMsec = static_cast<Int>(msecsPerFrame); }

};  // end class W3DGameClient

inline Keyboard *W3DGameClient::createKeyboard( void ) { return NEW Win32Keyboard; }
inline Mouse *W3DGameClient::createMouse( void )
{
	//return new DirectInputMouse;
	Win32Mouse * mouse = NEW W3DMouse;
	::Engine::context().win32Mouse = mouse;   ///< global cheat for the WndProc()
	return mouse;
}

#endif  // end __W3DGAMEINTERFACE_H_
