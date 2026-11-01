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

// FILE: GameEngine.h /////////////////////////////////////////////////////////
// The Game engine interface
// Author: Michael S. Booth, April 2001

#pragma once

#ifndef _GAME_ENGINE_H_
#define _GAME_ENGINE_H_

#include "Common/SubsystemInterface.h"
#include "Common/GameType.h"

#include <memory>

constexpr auto DEFAULT_MAX_FPS = 45;

// forward declarations
class AudioManager;
class GameLogic;
class GameClient;
class MessageStream;															///< @todo Create a MessageStreamInterface abstract class
class FileSystem;
class Keyboard;
class LocalFileSystem;
class ArchiveFileSystem;
class FileSystem;
class Mouse;
class NetworkInterface;
class ModuleFactory;
class ThingFactory;
class FunctionLexicon;
class Radar;
class WebBrowser;
class ParticleSystemManager;
class CommandList;
class GameLODManager;
class LogicAudio;
class LogicToClient;
class MapCache;
class NameKeyGenerator;
class SubsystemInterfaceList;

/**
 * The implementation of the game engine
 */
class GameEngine : public SubsystemInterface
{

public:

	GameEngine( void );
	virtual ~GameEngine() override;

	virtual void init( void ) override;								///< Init engine by creating client and logic
	virtual void init( int argc, char *argv[] );			///< Init engine by creating client and logic
	virtual void reset( void ) override;								///< reset system to starting state
	virtual void update( void ) override;							///< per frame update

	virtual void execute( void );											/**< The "main loop" of the game engine.
																								 It will not return until the game exits. */
	// Render-only frame between two logic ticks. alpha = fraction of the tick length since the
	// last tick (0..1); only draws (drawables, camera and animation time are interpolated).
	virtual void renderInterpolatedFrame( Real alpha );
	virtual void setFramesPerSecondLimit( Int fps );	///< Set the maximum rate engine updates are allowed to occur
	virtual Int  getFramesPerSecondLimit( void );			///< Get maxFPS.  Not inline since it is called from another lib.
	virtual void setQuitting( Bool quitting );				///< set quitting status
	virtual Bool getQuitting(void);						///< is app getting ready to quit.
	void setRenderFpsLimit( Int fps ) { m_renderFpsLimit = fps; }	///< cap for render-only frames, 0 = none (tests)

	virtual Bool isMultiplayerSession( void );
	virtual void serviceWindowsOS(void) {};		///< service the native OS
	virtual Bool isActive(void) {return m_isActive;}	///< returns whether app has OS focus.
	virtual void setIsActive(Bool isActive) { m_isActive = isActive; };

protected:

	virtual FileSystem *createFileSystem( void );								///< Factory for FileSystem classes
	virtual LocalFileSystem *createLocalFileSystem( void ) = 0;	///< Factory for LocalFileSystem classes
	virtual ArchiveFileSystem *createArchiveFileSystem( void ) = 0;	///< Factory for ArchiveFileSystem classes
	virtual GameLogic *createGameLogic( void ) = 0;							///< Factory for GameLogic classes.
	virtual GameClient *createGameClient( void ) = 0;						///< Factory for GameClient classes.
	virtual MessageStream *createMessageStream( void );					///< Factory for the message stream
	virtual ModuleFactory *createModuleFactory( void ) = 0;			///< Factory for modules
	virtual ThingFactory *createThingFactory( void ) = 0;				///< Factory for the thing factory
	virtual FunctionLexicon *createFunctionLexicon( void ) = 0;	///< Factory for Function Lexicon
	virtual Radar *createRadar( void ) = 0;											///< Factory for radar
	virtual WebBrowser *createWebBrowser( void ) = 0;						///< Factory for embedded browser
	virtual ParticleSystemManager* createParticleSystemManager( void ) = 0;
	virtual AudioManager *createAudioManager( void ) = 0;				///< Factory for Audio Manager
	void runTick( void );																				///< One engine tick (update() with exception handling)
	void renderTickFrame( Real alpha );				///< Tick frame drawn after the logic, interpolated
	Bool canDeferTickDraw( void ) const;			///< May the tick draw its frame after the logic?

	Int m_maxFPS;																									///< Maximum frames per second allowed
	Int m_renderFpsLimit;																			///< Cap for render-only frames (Options.ini RenderFPSLimit, 0 = none)
  Bool m_quitting;  ///< true when we need to quit the game
	Bool m_isActive;	///< app has OS focus.

private:

	/// The subsystems the engine creates in init and frees in its destructor, outside the subsystem
	/// list; the global names (TheFileSystem, TheNameKeyGenerator, ...) point to them. The destructor
	/// frees them one by one in the order they need each other, so the order of these members does
	/// not matter.
	struct OwnedSubsystems
	{
		std::unique_ptr<SubsystemInterfaceList> subsystemList;
		std::unique_ptr<FileSystem> fileSystem;
		std::unique_ptr<NameKeyGenerator> nameKeyGenerator;
		std::unique_ptr<CommandList> commandList;
		std::unique_ptr<GameLODManager> gameLODManager;
		std::unique_ptr<LogicAudio> logicAudio;
		std::unique_ptr<LogicToClient> logicToClient;
		std::unique_ptr<MapCache> mapCache;
	};
	OwnedSubsystems m_own;
};
inline void GameEngine::setQuitting( Bool quitting ) { m_quitting = quitting; }
inline Bool GameEngine::getQuitting(void) { return m_quitting; }

// the game engine singleton
#include "Engine/EngineContext.h"

// Process exit code (WinMain) and log for runner runs (-crcLog). 0 = ok, 2 = replay desync,
// 3 = replay/map not found, 4 = test sequence failed (-testablauf).
enum { GAME_EXIT_OK = 0, GAME_EXIT_DESYNC = 2, GAME_EXIT_NOT_FOUND = 3, GAME_EXIT_TEST_SCRIPT = 4, GAME_EXIT_BAD_SETUP = 5 };
extern Int TheGameExitCode;
void GameEngine_RunnerLog( const char *line );

/// Below the recommended CPU the main menu shows a hint once: TRUE while it is to be shown, with the
/// cores and logical processors of this computer. The menu clears it after showing it.
Bool GameEngine_PendingHardwareHint( Int *cores, Int *threads );
void GameEngine_ClearHardwareHint( void );

/// The game quits and WinMain starts it again with the same switches (without -mod), once this process
/// has given up its single-instance lock (a changed mod list needs a new start).
void GameEngine_RequestRestart( void );
Bool GameEngine_RestartRequested( void );
// Runner log line "ACTION <frame> <kind> <name> <x> <y> <z>" when -logActions is set (world coordinates).
void GameEngine_LogAction( UnsignedInt frame, const char *kind, const char *name, Real x, Real y, Real z );

// Clock for visible real-time animations (water, shroud, terrain clouds, menu). Normally timeGetTime();
// with -fixedTimeStep a virtual clock that advances by one tick length per main loop pass, so two runs
// show the same frames.
UnsignedInt GameEngine_RenderTimeMs( void );

// Client animations that advance with each drawn frame use this many steps per frame instead of one: render time
// since the previous drawn frame over 33 ms, so they keep the pace of 30 frames per second at any frame rate. With
// -fixedTimeStep exactly 1 per drawn frame (the virtual clock advances 33 ms per pass). The display calls
// GameEngine_BeginRenderFrameSteps once at the start of each drawn frame.
void GameEngine_BeginRenderFrameSteps( void );
Real GameEngine_RenderFrameSteps( void );

/// This function creates a new game engine instance, and is device specific
extern GameEngine *CreateGameEngine( void );

/// The entry point for the game system
extern void GameMain( int argc, char *argv[] );

#endif // _GAME_ENGINE_H_
