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

// FILE: Win32GameEngine.h ////////////////////////////////////////////////////////////////////////
// Author: Colin Day, April 2001
// Description: 
//   Device implementation of the game engine ... this is, of course, the 
//   highest level of the game that creates the necessary interfaces to the 
//   devices we need
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __WIN32GAMEENGINE_H_
#define __WIN32GAMEENGINE_H_

#include "Common/GameEngine.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/NetworkInterface.h"
#if defined(GZH_NULL_MEDIA)
#include "NullMedia/NullAudioManager.h"
#else
#include "XAudio2Device/XAudio2AudioManager.h"
#endif
#include "Win32Device/Common/Win32BIGFileSystem.h"
#include "Win32Device/Common/Win32LocalFileSystem.h"
#include "W3DDevice/Common/W3DModuleFactory.h"
#include "W3DDevice/GameLogic/W3DGameLogic.h"
#include "W3DDevice/GameClient/W3DGameClient.h"
#include "W3DDevice/Common/W3DFunctionLexicon.h"
#include "W3DDevice/Common/W3DRadar.h"
#include "W3DDevice/Common/W3DFunctionLexicon.h"
#include "W3DDevice/Common/W3DThingFactory.h"




//-------------------------------------------------------------------------------------------------
/** Class declaration for the Win32 game engine */
//-------------------------------------------------------------------------------------------------
class Win32GameEngine : public GameEngine
{

public:

	Win32GameEngine();
	virtual ~Win32GameEngine() override;

	virtual void init( void ) override;															///< initialization 
	virtual void reset( void ) override;															///< reset engine
	virtual void update( void ) override;														///< update the game engine
	virtual void serviceWindowsOS( void ) override;									///< allow windows maintenance in background

protected:

	virtual GameLogic *createGameLogic( void ) override;							///< factory for game logic
 	virtual GameClient *createGameClient( void ) override;						///< factory for game client
	virtual ModuleFactory *createModuleFactory( void ) override;			///< factory for creating modules
	virtual ThingFactory *createThingFactory( void ) override;				///< factory for the thing factory
	virtual FunctionLexicon *createFunctionLexicon( void ) override; ///< factory for function lexicon
	virtual LocalFileSystem *createLocalFileSystem( void ) override; ///< factory for local file system
	virtual ArchiveFileSystem *createArchiveFileSystem( void ) override;	///< factory for archive file system
	virtual NetworkInterface *createNetwork( void );				///< Factory for the network
	virtual Radar *createRadar( void ) override;											///< Factory for radar
	virtual WebBrowser *createWebBrowser( void ) override;						///< Factory for embedded browser
	virtual AudioManager *createAudioManager( void ) override;				///< Factory for audio device
	virtual ParticleSystemManager* createParticleSystemManager( void ) override;


protected:
	UINT m_previousErrorMode;
};  // end Win32GameEngine

// INLINE -----------------------------------------------------------------------------------------
inline GameLogic *Win32GameEngine::createGameLogic( void ) { return NEW W3DGameLogic; }
inline GameClient *Win32GameEngine::createGameClient( void ) { return NEW W3DGameClient; }
inline ModuleFactory *Win32GameEngine::createModuleFactory( void ) { return NEW W3DModuleFactory; }
inline ThingFactory *Win32GameEngine::createThingFactory( void ) { return NEW W3DThingFactory; }
inline FunctionLexicon *Win32GameEngine::createFunctionLexicon( void ) { return NEW W3DFunctionLexicon; }
inline LocalFileSystem *Win32GameEngine::createLocalFileSystem( void ) { return NEW Win32LocalFileSystem; }
inline ArchiveFileSystem *Win32GameEngine::createArchiveFileSystem( void ) { return NEW Win32BIGFileSystem; }
inline ParticleSystemManager* Win32GameEngine::createParticleSystemManager( void ) { return NEW W3DParticleSystemManager; }

inline NetworkInterface *Win32GameEngine::createNetwork( void ) { return NetworkInterface::createNetwork(); }
inline Radar *Win32GameEngine::createRadar( void ) { return NEW W3DRadar; }
// There is no embedded web browser; the factory stays as a stub.
inline WebBrowser *Win32GameEngine::createWebBrowser( void ) { return nullptr; }
#if defined(GZH_NULL_MEDIA)
// Build without XAudio2 (MinGW/Wine): a silent audio manager.
inline AudioManager *Win32GameEngine::createAudioManager( void ) { return NEW NullAudioManager; }
#else
inline AudioManager *Win32GameEngine::createAudioManager( void ) { return NEW XAudio2AudioManager; }
#endif
 
#endif  // end __WIN32GAMEENGINE_H_
