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

// FILE: GameClient.cpp ////////////////////////////////////////////////////
// Implementation of GameClient singleton
// Author: Michael S. Booth, March 2001
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "GameClient/GameClient.h"

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "Common/ActionManager.h"
#include "Common/OwnedSubsystem.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/TestAids.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "Common/GameLOD.h"
#include "GameClient/Anim2D.h"
#include "GameClient/CameraPath.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/CommandXlat.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Diplomacy.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawGroupInfo.h"
#include "GameClient/Eva.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/GUICommandTranslator.h"
#include "GameClient/HeaderTemplate.h"
#include "GameClient/HintSpy.h"
#include "GameClient/HotKey.h"
#include "GameClient/IMEManager.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Keyboard.h"
#include "GameClient/LanguageFilter.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/Mouse.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/PlaceEventTranslator.h"
#include "GameClient/RayEffect.h"
#include "GameClient/SelectionXlat.h"
#include "GameClient/Shell.h"
#include "GameClient/Snow.h"
#include "GameClient/TerrainVisual.h"
#include "GameClient/View.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/WindowXlat.h"
#include "GameLogic/FPUControl.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GhostObject.h"
#include "GameLogic/Object.h"
#include "GameLogic/ScriptEngine.h"		// For TheScriptEngine - jkmcd
#include "GameNetwork/GameInfo.h"	// TheChallengeGameInfo
#include "GameNetwork/GameLobbyCallbacks.h"	// TheLobby
#include "Engine/ThreadRole.h"

constexpr auto DRAWABLE_HASH_SIZE = 8192;

/// The GameClient singleton instance

//-------------------------------------------------------------------------------------------------
GameClient::GameClient()
{

	// zero our translator list
	for( Int i = 0; i < MAX_CLIENT_TRANSLATORS; i++ )
		m_translators[ i ] = static_cast<TranslatorID>(TRANSLATOR_ID_INVALID);
	m_numTranslators = 0;
	m_commandTranslator = nullptr;
	
	// Added By Sadullah Nader
	// Initializations missing and needed
	m_drawableTOC.clear();
	//
	m_textBearingDrawableList.clear();

	m_frame = 0;
	m_tickSerial = 0;
	m_renderAlpha = 1.0f;
	m_renderOnlyFrame = FALSE;
	m_renderInterpolated = FALSE;
	m_drawPhase = DrawPhase::DRAW_PHASE_FULL;
	m_deferTickDraw = FALSE;
	m_deferredTickDrawPending = FALSE;

	m_drawableList = nullptr;
	
	m_nextDrawableID = static_cast<DrawableID>(1);
}

//std::vector<std::string>	preloadTextureNamesGlobalHack;
//std::vector<std::string>	preloadTextureNamesGlobalHack2;

//-------------------------------------------------------------------------------------------------
GameClient::~GameClient()
{

	destroySubsystem(m_own.drawGroupInfo, ::Engine::context().drawGroupInfo);

	// clear any drawable TOC we might have
	m_drawableTOC.clear();

	//DEBUG_LOG(("Preloaded texture files ------------------------------------------\n"));
	//for (Int oog=0; oog<preloadTextureNamesGlobalHack2.size(); ++oog)
	//{
	//	DEBUG_LOG(("%s\n", preloadTextureNamesGlobalHack2[oog]));
	//}
	//DEBUG_LOG(("------------------------------------------------------------------\n"));
	//for (oog=0; oog<preloadTextureNamesGlobalHack.size(); ++oog)
	//{
	//	DEBUG_LOG(("%s\n", preloadTextureNamesGlobalHack[oog]));
	//}
	//DEBUG_LOG(("End Texture files ------------------------------------------------\n"));
	destroySubsystem(m_own.campaignManager, ::Engine::context().campaignManager);

	// destroy all Drawables
	Drawable *draw, *nextDraw;
	for( draw = m_drawableList; draw; draw = nextDraw )
	{
		nextDraw = draw->getNextDrawable();
		destroyDrawable( draw );
	}
	m_drawableList = nullptr;

	// delete the ray effects
	destroySubsystem(m_own.rayEffects, ::Engine::context().rayEffects);

	// delete the hot key manager
	destroySubsystem(m_own.hotKeyManager, ::Engine::context().hotKeyManager);

	// destroy the in-game user interface
	destroySubsystem(m_own.inGameUI, ::Engine::context().inGameUI);

	destroySubsystem(m_own.challengeGenerals, ::Engine::context().challengeGenerals);

	// delete the shell
	delete TheShell;
	::Engine::context().shell = nullptr;

	// Free the LAN interface if the program ends in the LAN lobby, the LAN game menu or a LAN game.
	// Only after the shell is gone: its shutdown (LanLobbyMenuShutdown) still unregisters from it.
	delete TheLobby;
	::Engine::context().lobby = nullptr;

	// ChallengeMenuShutdown frees the challenge game info only when the menu is left with its animation.
	// Free it here in case the program ends during a challenge or between two opponents.
	delete TheChallengeGameInfo;
	::Engine::context().challengeGameInfo = nullptr;

	destroySubsystem(m_own.imeManager, ::Engine::context().imeManager);

	// delete window manager
	destroySubsystem(m_own.windowManager, ::Engine::context().windowManager);

	// delete the font library
	if (TheFontLibrary)
		TheFontLibrary->reset();
	destroySubsystem(m_own.fontLibrary, ::Engine::context().fontLibrary);

	destroySubsystem(m_own.mouse, ::Engine::context().mouse);

	///@todo :  TheTerrainVisual used to be the first thing destroyed.
	//I had to put in here so that drawables free their track marks before
	//the terrain visual deletes the track laying system. MW

	// destroy the terrain visual representation
	destroySubsystem(m_own.terrainVisual, ::Engine::context().terrainVisual);

	// destroy the display
	destroySubsystem(m_own.display, ::Engine::context().display);

	destroySubsystem(m_own.headerTemplateManager, ::Engine::context().headerTemplateManager);
	
	destroySubsystem(m_own.languageFilter, ::Engine::context().languageFilter);

	destroySubsystem(m_own.videoPlayer, ::Engine::context().videoPlayer);

	// destroy all translators
	for( Int i = 0; static_cast<UnsignedInt>(i) < m_numTranslators; i++ )
		TheMessageStream->removeTranslator( m_translators[ i ] );	
	m_numTranslators = 0;
	m_commandTranslator = nullptr;

	destroySubsystem(m_own.anim2DCollection, ::Engine::context().anim2DCollection);

	destroySubsystem(m_own.mappedImageCollection, ::Engine::context().mappedImageCollection);
	
	destroySubsystem(m_own.keyboard, ::Engine::context().keyboard);

	destroySubsystem(m_own.displayStringManager, ::Engine::context().displayStringManager);

	destroySubsystem(m_own.eva, ::Engine::context().eva);

	destroySubsystem(m_own.snowManager, ::Engine::context().snowManager);

}  // end ~GameClient

//-------------------------------------------------------------------------------------------------
/** Initialize resources for the game client */
//-------------------------------------------------------------------------------------------------
void GameClient::init( void )
{

	setFrameRate(MSEC_PER_LOGICFRAME_REAL);		// from GameCommon.h... tell W3D what our expected framerate is

	// created here rather than in the constructor: a client that is never initialised (the map
	// editor's stand-in) leaves the draw group info of its host alone
	::Engine::context().drawGroupInfo = adoptSubsystem(m_own.drawGroupInfo, new DrawGroupInfo);

	INI ini;
	// Load the DrawGroupInfo here, before the Display Manager is loaded.
	ini.load("Data\\INI\\DrawGroupInfo.ini", INILoadType::INI_LOAD_OVERWRITE, nullptr);
	
	// Override the ini values with localized versions:
	if (TheGlobalLanguageData && TheGlobalLanguageData->m_drawGroupInfoFont.name.isNotEmpty())
	{
		TheDrawGroupInfo->m_fontName = TheGlobalLanguageData->m_drawGroupInfoFont.name;
		TheDrawGroupInfo->m_fontSize = TheGlobalLanguageData->m_drawGroupInfoFont.size;
		TheDrawGroupInfo->m_fontIsBold = TheGlobalLanguageData->m_drawGroupInfoFont.bold;
	}

	// create the display string factory
	::Engine::context().displayStringManager = adoptSubsystem(m_own.displayStringManager, createDisplayStringManager());
	if( TheDisplayStringManager )	{
		TheDisplayStringManager->init();
		TheDisplayStringManager->setName("TheDisplayStringManager");
	}
	
	// create the keyboard
	::Engine::context().keyboard = adoptSubsystem(m_own.keyboard, createKeyboard());
	TheKeyboard->init();
	TheKeyboard->setName("TheKeyboard");

	// allocate and load image collection for the GUI and just load the 256x256 ones for now
	::Engine::context().mappedImageCollection = adoptSubsystem(m_own.mappedImageCollection, MSGNEW("GameClientSubsystem") ImageCollection);
	TheMappedImageCollection->load( 512 );

	// now that we have all the images loaded ... load any animation definitions from those images
	::Engine::context().anim2DCollection = adoptSubsystem(m_own.anim2DCollection, MSGNEW("GameClientSubsystem") Anim2DCollection);
	TheAnim2DCollection->init();
 	TheAnim2DCollection->setName("TheAnim2DCollection");

	// register message translators
	if( TheMessageStream )
	{

		//
		// NOTE: Make sure m_translators[] is large enough to accomodate all the translators you
		// are loading here.  See MAX_CLIENT_TRANSLATORS
		//

		// since we only allocate one of each, don't bother pooling 'em
		m_translators[ m_numTranslators++ ] =	TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") WindowTranslator,     10 );
		m_translators[ m_numTranslators++ ] =	TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") MetaEventTranslator,	20 );
		m_translators[ m_numTranslators++ ] =	TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") HotKeyTranslator,	25 );
		m_translators[ m_numTranslators++ ] =	TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") PlaceEventTranslator,	30 );
		m_translators[ m_numTranslators++ ] = TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") GUICommandTranslator, 40 );
		m_translators[ m_numTranslators++ ] =	TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") SelectionTranslator,	50 );
		m_translators[ m_numTranslators++ ] =	TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") LookAtTranslator,			60 );
		m_translators[ m_numTranslators ] =	TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") CommandTranslator,		70 );
		// we keep a pointer to the command translator because it's useful
		m_commandTranslator = static_cast<CommandTranslator *>(TheMessageStream->findTranslator( m_translators[ m_numTranslators++ ] ));
		m_translators[ m_numTranslators++ ] =	TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") HintSpyTranslator,		100 );

		//
		// the client message translator should probably remain as the last reaction of the
		// client before the messages are given to the network for processing.  This
		// lets all systems in the client give events that can be processed by the
		// client message translator
		//
		m_translators[ m_numTranslators++ ] =	TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") GameClientMessageDispatcher, 999999999 );

	}  

	// create the font library
	::Engine::context().fontLibrary = adoptSubsystem(m_own.fontLibrary, createFontLibrary());
	if( TheFontLibrary )
		TheFontLibrary->init();

	// create the mouse
	::Engine::context().mouse = adoptSubsystem(m_own.mouse, createMouse());
	TheMouse->parseIni();
	TheMouse->initCursorResources();
 	TheMouse->setName("TheMouse");

	// instantiate the display
	::Engine::context().display = adoptSubsystem(m_own.display, createGameDisplay());
	if( TheDisplay ) {
		TheDisplay->init();
 		TheDisplay->setName("TheDisplay");
	}
	
	::Engine::context().headerTemplateManager = adoptSubsystem(m_own.headerTemplateManager, MSGNEW("GameClientSubsystem") HeaderTemplateManager);
	if(TheHeaderTemplateManager){
		TheHeaderTemplateManager->init();
	}

	// create the window manager
	::Engine::context().windowManager = adoptSubsystem(m_own.windowManager, createWindowManager());
	if( TheWindowManager )
	{

		TheWindowManager->init();
 		TheWindowManager->setName("TheWindowManager");

	}  // end if

	// create the IME manager
	::Engine::context().imeManager = adoptSubsystem(m_own.imeManager, CreateIMEManagerInterface());
	if ( TheIMEManager )
	{
		TheIMEManager->init();
 		TheIMEManager->setName("TheIMEManager");
	}

	// create the shell
	::Engine::context().shell = MSGNEW("GameClientSubsystem") Shell;
	if( TheShell ) {
		TheShell->init();
 		TheShell->setName("TheShell");
	}

	// instantiate the in-game user interface
	::Engine::context().inGameUI = adoptSubsystem(m_own.inGameUI, createInGameUI());
	if( TheInGameUI ) {
		TheInGameUI->init();
 		TheInGameUI->setName("TheInGameUI");
	}

 	::Engine::context().challengeGenerals = adoptSubsystem(m_own.challengeGenerals, createChallengeGenerals());
 	if( TheChallengeGenerals ) {
 		TheChallengeGenerals->init();
 	}

	::Engine::context().hotKeyManager = adoptSubsystem(m_own.hotKeyManager, MSGNEW("GameClientSubsystem") HotKeyManager);
	if( TheHotKeyManager ) {
		TheHotKeyManager->init();
 		TheHotKeyManager->setName("TheHotKeyManager");
	}

	// instantiate the terrain visual display
	::Engine::context().terrainVisual = adoptSubsystem(m_own.terrainVisual, createTerrainVisual());
	if( TheTerrainVisual ) {
		TheTerrainVisual->init();
 		TheTerrainVisual->setName("TheTerrainVisual");
	}

	// allocate the ray effects manager
	::Engine::context().rayEffects = adoptSubsystem(m_own.rayEffects, MSGNEW("GameClientSubsystem") RayEffectSystem);
	if( TheRayEffects )	{
		TheRayEffects->init();
 		TheRayEffects->setName("TheRayEffects");
	}

	TheMouse->init();	//finish initializing the mouse.

	// set the limits of the mouse now that we've created the display and such
	if( TheMouse )
	{
		TheMouse->setPosition( 0, 0 );
		TheMouse->setMouseLimits();
 		TheMouse->setName("TheMouse");
	}  // end if

	// create the video player
	::Engine::context().videoPlayer = adoptSubsystem(m_own.videoPlayer, createVideoPlayer());
	if ( TheVideoPlayer )
	{
		TheVideoPlayer->init();
 		TheVideoPlayer->setName("TheVideoPlayer");
	}

	// create the language filter.
	::Engine::context().languageFilter = adoptSubsystem(m_own.languageFilter, createLanguageFilter());
	if (TheLanguageFilter)
	{
		TheLanguageFilter->init();
 		TheLanguageFilter->setName("TheLanguageFilter");
	}

	::Engine::context().campaignManager = adoptSubsystem(m_own.campaignManager, MSGNEW("GameClientSubsystem") CampaignManager);
	TheCampaignManager->init();

	::Engine::context().eva = adoptSubsystem(m_own.eva, MSGNEW("GameClientSubsystem") Eva);
	TheEva->init();
 	TheEva->setName("TheEva");

	TheDisplayStringManager->postProcessLoad();

	::Engine::context().snowManager = adoptSubsystem(m_own.snowManager, createSnowManager());
	if (TheSnowManager)
	{
		TheSnowManager->init();
		TheSnowManager->setName("TheSnowManager");
	}


}  // end init

//-------------------------------------------------------------------------------------------------
/** Reset the game client for a new game */
void GameClient::reset( void )
{
	Drawable *draw, *nextDraw;
//	m_drawableHash.clear();
//	m_drawableHash.resize(DRAWABLE_HASH_SIZE);

	// need to reset the in game UI to clear drawables before they are destroyed
	TheInGameUI->reset();

	// destroy all Drawables
	for( draw = m_drawableList; draw; draw = nextDraw )
	{
		nextDraw = draw->getNextDrawable();
		destroyDrawable( draw );
	}
	m_drawableList = nullptr;

	// Clear the lookup table only after all drawables are destroyed. m_nextDrawableID keeps growing
	// over the session, so the table grows far beyond DRAWABLE_HASH_SIZE; shrinking it first would make
	// removeDrawableFromLookupTable() write past size().
	m_drawableVector.clear();
	m_drawableVector.resize(DRAWABLE_HASH_SIZE, nullptr);

	TheDisplay->reset();
	TheTerrainVisual->reset();
	TheRayEffects->reset();
	TheVideoPlayer->reset();
	TheEva->reset();
	if (TheSnowManager)
		TheSnowManager->reset();

	// clear any drawable TOC we might have
	m_drawableTOC.clear();

}  // end reset

/** -----------------------------------------------------------------------------------------------
 * Return a new unique object id.
 */
DrawableID GameClient::allocDrawableID( void )
{
	/// @todo Find unused value in current set
	DrawableID ret = m_nextDrawableID;
	m_nextDrawableID = static_cast<DrawableID>(static_cast<UnsignedInt>(m_nextDrawableID) + 1);
	return ret;
}

/** -----------------------------------------------------------------------------------------------
 * Given a drawable, register it with the GameClient and give it a unique ID.
 */
void GameClient::registerDrawable( Drawable *draw ) 
{

	// assign this drawable a unique ID, this will add it to the fast lookup table too
	draw->setID( allocDrawableID() );

	// add the drawable to the master list
	draw->prependToList( &m_drawableList );

}  // end registerDrawable

/** -----------------------------------------------------------------------------------------------
 * Redraw all views, update the GUI, play sound effects, etc.
 */
DECLARE_PERF_TIMER(GameClient_update)
DECLARE_PERF_TIMER(GameClient_draw)
void GameClient::update( void )
{
	ASSERT_MAIN_THREAD();
	USE_PERF_TIMER(GameClient_update)

	++m_tickSerial;	// new engine tick
	// create the FRAME_TICK message
	GameMessage *frameMsg = TheMessageStream->appendMessage( GameMessage::MSG_FRAME_TICK );
	frameMsg->appendTimestampArgument( getFrame() );
	static Bool playSizzle = FALSE;
	// We need to show the movie first.
	if(TheGlobalData->m_playIntro && !TheDisplay->isMoviePlaying())
	{
		TheDisplay->playLogoMovie("EALogoMovie", 5000, 3000);
		TheWritableGlobalData->m_playIntro = FALSE;
		TheWritableGlobalData->m_afterIntro = TRUE;
		playSizzle = TRUE;
	}

	//Initial Game Codition.  We must show the movie first and then we can display the shell	
	if(TheGlobalData->m_afterIntro && !TheDisplay->isMoviePlaying())
	{
		if( playSizzle && TheGlobalData->m_playSizzle )
		{
			TheWritableGlobalData->m_allowExitOutOfMovies = TRUE;
			TheDisplay->playMovie("Sizzle");
			playSizzle = FALSE;
		}
		else
		{
			TheWritableGlobalData->m_breakTheMovie = TRUE;
			TheWritableGlobalData->m_allowExitOutOfMovies = TRUE;

			TheShell->showShellMap(TRUE);
			TheShell->showShell();
			TheWritableGlobalData->m_afterIntro = FALSE;
			// m_breakTheMovie stops W3DDisplay::draw and is otherwise only reset by the main menu
			// (MainMenuInit) or the first load screen. With a start file (-skirmish, map, -replay) there is no
			// main menu, so reset it here.
			if (TheGlobalData->m_initialFile.isNotEmpty())
				TheWritableGlobalData->m_breakTheMovie = FALSE;
		}
	}

	//Update snow particles.
	if (TheSnowManager)
		TheSnowManager->UPDATE();

	// update animation 2d collection
	TheAnim2DCollection->UPDATE();

	// update the keyboard
	if( TheKeyboard )
	{
		TheKeyboard->UPDATE();
		TheKeyboard->createStreamMessages();

	}  // end if

	// Update the Eva stuff
	TheEva->UPDATE();

	// update the mouse
	if( TheMouse )
	{
		TheMouse->UPDATE();
		TheMouse->createStreamMessages();

	}  // end if
	

  if (TheInGameUI->isCameraTrackingDrawable())
  {
    Drawable *draw = TheInGameUI->getFirstSelectedDrawable();
    if ( draw )
    {
      const Coord3D *pos = draw->getPosition();
      TheTacticalView->lookAt( pos );
    }
    else
      TheInGameUI->setCameraTrackingDrawable( FALSE );
  }

	if(TheGlobalData->m_playIntro || TheGlobalData->m_afterIntro)
	{
		// redraw all views, update the GUI
		{
			TheDisplay->DRAW();
		}
		{
			TheDisplay->UPDATE();
		}
		return;
	}

	// update the window system itself
	{
		TheWindowManager->UPDATE();
	}

	// Test helpers (script file, side panel, quick load); before the no-draw exit (-runner) so the
	// test helper replay test runs there too
	TestAids_Update();

	// update the video player
	{
		TheVideoPlayer->UPDATE();
	}

	Bool freezeTime = TheTacticalView->isTimeFrozen() && !TheTacticalView->isCameraMovementFinished();
	freezeTime = freezeTime || TheScriptEngine->isTimeFrozenDebug();
	freezeTime = freezeTime || TheScriptEngine->isTimeFrozenScript();
	freezeTime = freezeTime || TheGameLogic->isGamePaused();
	Int localPlayerIndex = ThePlayerList ? ThePlayerList->getLocalPlayer()->getPlayerIndex() : 0;

	// hack to let client spin fast in network games but still do effects at the same pace. -MDC
	static UnsignedInt lastFrame = ~0u;
	freezeTime = freezeTime || (lastFrame == m_frame);
	lastFrame = m_frame;

	if (!freezeTime)
	{
#if defined(_DEBUG)
		if (TheGlobalData->m_shroudOn)
#else
		if (true)
#endif
		{	
			//localPlayerIndex=TheGhostObjectManager->getLocalPlayerIndex();	//always use the first local player set since normally can't change.  Doesn't work with debug "CTRL_SHIFT_SPACE"
#ifdef DEBUG_FOG_MEMORY
			//Find indices of all active players
			Int numPlayers=ThePlayerList->getPlayerCount();
			Int numNonLocalPlayers=0;
			Int nonLocalPlayerIndices[MAX_PLAYER_COUNT];
			for (Int i=0; i<numPlayers; i++)
			{	Player *player=ThePlayerList->getNthPlayer(i);
				//if (player->getPlayerType == PLAYER_HUMAN)
				if (player->getPlayerIndex() != localPlayerIndex)
					nonLocalPlayerIndices[numNonLocalPlayers++]=player->getPlayerIndex();
			}
			//update ghostObjects which don't have drawables or objects.
			TheGhostObjectManager->updateOrphanedObjects(nonLocalPlayerIndices,numNonLocalPlayers);
#else
			TheGhostObjectManager->updateOrphanedObjects(nullptr,0);
#endif
		}


		// call the update for all client drawables
		Drawable* draw = firstDrawable();
		while (draw)
		{	// update() could free the Drawable, so go ahead and grab 'next'
			Drawable* next = draw->getNextDrawable();
#if defined(_DEBUG)
			if (TheGlobalData->m_shroudOn)
#else
			if (true)
#endif
			{	//immobile objects need to take snapshots whenever they become fogged
				//so need to refresh their status.  We can't rely on external calls
				//to getShroudStatus() because they are only made for visible on-screen
				//objects.
				Object *object=draw->getObject();
				if (object)
				{
	#ifdef DEBUG_FOG_MEMORY
					Int *playerIndex=nonLocalPlayerIndices;
					for (i=0; i<numNonLocalPlayers; i++, playerIndex++)
						object->getShroudedStatus(*playerIndex);
	#endif
					ObjectShroudStatus ss=object->getShroudedStatus(localPlayerIndex);
					if (ss >= OBJECTSHROUD_FOGGED && draw->getShroudClearFrame()!=0) {
						UnsignedInt limit = 2*LOGICFRAMES_PER_SECOND;
						if (object->isEffectivelyDead()) {
							// extend the time, so we can see the dead plane blow up & crash.
							limit += 3*LOGICFRAMES_PER_SECOND;
						}
						if (TheGameLogic->getFrame() < limit + draw->getShroudClearFrame()) {
							// It's been less than 2 seconds since we could see them clear, so keep showing them.
							ss = OBJECTSHROUD_CLEAR;
						}
					}
					draw->setFullyObscuredByShroud(ss >= OBJECTSHROUD_FOGGED);
				}
			}
			draw->updateDrawable();
			draw = next;
		}
	}

	// need to draw the first frame, then don't draw again until TheGlobalData->m_noDraw
	// Also active in release and profile builds for -runner, which would otherwise draw every tick and
	// wait on Present/VSync. Only presentation is skipped (particles, terrain graphics, display); logic
	// and CRC are unaffected. With -testablauf the menu needs the shell update below, so drawing is
	// only skipped inside a game (not on the shell map).
	const Bool testScriptInMenu = TheGlobalData->m_testScriptFile.isNotEmpty()
		&& !(TheGameLogic->isInGame() && !TheGameLogic->isInShellGame());
#if !defined(_DEBUG)
	if (TheGlobalData->m_runnerMode)
#endif
	if (TheGlobalData->m_noDraw > TheGameLogic->getFrame() && TheGameLogic->getFrame() > 0 && !testScriptInMenu)
	{
		// The particle update normally runs in W3DDisplay::draw (so systems attached to drawable bones see
		// the client transform). Without drawing it must run here, or systems never expire and pile up.
		// update() is idempotent per logic frame and reads only objects, drawables and the client random
		// generator, so the CRC is unaffected.
		if( !freezeTime )
		{
			TheParticleSystemManager->setLocalPlayerIndex(localPlayerIndex);
			TheParticleSystemManager->update();
		}
		return;
	}

	// update all particle systems
	if( !freezeTime )
	{
		// update particle systems
		TheParticleSystemManager->setLocalPlayerIndex(localPlayerIndex);
//		TheParticleSystemManager->update();

	}  // end if

	// update the terrain visuals
	{
		TheTerrainVisual->UPDATE();
	}

	// Camera path for image comparisons (-cameraPath); before drawing and before the tick camera is
	// stored for interpolation (W3DView::updateView)
	CameraPath_Update();

	// update display
	{
		TheDisplay->UPDATE();
	}

	{
		USE_PERF_TIMER(GameClient_draw)
			
	// redraw all views, update the GUI
	//if(TheGameLogic->getFrame() >= 2)
		
		// In decoupled mode the tick only advances state here (camera, particles, animation time);
		// GameEngine::renderTickFrame() draws its frame after the logic, interpolated like the in-between
		// frames. Drawing it here would show the camera on the new tick while the next in-between frame is
		// a tick behind.
		if (m_deferTickDraw)
		{
			m_deferTickDraw = FALSE;	// only this tick's draw; load screens of the logic draw normally
			m_drawPhase = DrawPhase::DRAW_PHASE_UPDATE;
			TheDisplay->DRAW();
			m_drawPhase = DrawPhase::DRAW_PHASE_FULL;
			m_deferredTickDrawPending = TRUE;
		}
		else
			TheDisplay->DRAW();
	}

	{
		// let display string factory handle its update
		TheDisplayStringManager->update();
	}

	{
		// update the shell
		TheShell->UPDATE();
	}

	{
		// update the in game UI 
		TheInGameUI->UPDATE();
	}
}  // end update

/** -----------------------------------------------------------------------------------------------
 * Call the given callback function for each object contained within the given region.
 */
void GameClient::iterateDrawablesInRegion( Region3D *region, GameClientFuncPtr userFunc, void *userData )
{
	Drawable *draw, *nextDrawable;

	for( draw = m_drawableList; draw; draw=nextDrawable )
	{
		nextDrawable = draw->getNextDrawable();

		Coord3D pos = *draw->getPosition();
		if( region == nullptr ||
			  (pos.x >= region->lo.x && pos.x <= region->hi.x &&
			   pos.y >= region->lo.y && pos.y <= region->hi.y &&
				 pos.z >= region->lo.z && pos.z <= region->hi.z) )
		{
			(*userFunc)( draw, userData );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Copy the object values that drawing reads (icons over the units) into every drawable bound to an
	* object. Runs after the logic tick, so the render-only frames until the next tick read the copy. */
//-------------------------------------------------------------------------------------------------
void GameClient::captureObjectViews( void )
{
	for( Drawable *draw = m_drawableList; draw; draw = draw->getNextDrawable() )
		draw->captureObjectView();
}

/**Helper function to update fake GLA structures to become visible to certain players.
We should only call this during critical moments, such as changing teams, changing to
observer, etc.*/
void GameClient::updateFakeDrawables(void)
{
	for( Drawable *draw = getDrawableList(); draw; draw = draw->getNextDrawable() )
	{
		const Object *object=draw->getObject();

		if( object && object->isKindOf( KINDOF_FS_FAKE ) )
		{	
			Relationship rel=ThePlayerList->getLocalPlayer()->getRelationship(object->getTeam());
			if (rel == ALLIES || rel == NEUTRAL)
				draw->setTerrainDecal(TERRAIN_DECAL_SHADOW_TEXTURE);
			else
				draw->setTerrainDecal(TERRAIN_DECAL_NONE);
		}
	}
}

/** -----------------------------------------------------------------------------------------------
 * Destroy the drawable immediately.
 */
void GameClient::destroyDrawable( Drawable *draw )
{
	ASSERT_MAIN_THREAD();	// the logic still calls this directly; must move to the sync point

	// remove any notion of the Drawable in the in-game user interface
	TheInGameUI->disregardDrawable( draw );

	// detach this Drawable from any particle system that may be using it
	draw->detachFromParticleSystem();

	// remove from the master list
	draw->removeFromList(&m_drawableList);

	//
	// because drawables and objects are tightly coupled, not only MUST we maintain
	// our links in all instances, but it is NECESSARY for the client to actually
	// modify data in the logic, that is the pointer in an object to *this* drawable
	//
	Object *obj = draw->getObject();
	if( obj )
	{

		DEBUG_ASSERTCRASH( obj->getDrawable() == draw, ("Object/Drawable pointer mismatch!\n") );
		obj->friend_bindToDrawable( nullptr );

	}  // end if

	// remove the drawable from our hash of drawables
	removeDrawableFromLookupTable( draw );

	// free storage
	deleteInstanceOf(draw);

}

// ------------------------------------------------------------------------------------------------
/** Add drawable to lookup table for fast id searching */
// ------------------------------------------------------------------------------------------------
void GameClient::addDrawableToLookupTable(Drawable *draw )
{

	// sanity
	if( draw == nullptr )
		return;

	// add to lookup
//	m_drawableHash[ draw->getID() ] = draw;
	DrawableID newID = draw->getID();
	// Doubling needs a lower bound: with an empty vector (before the first reset()) size() * 2 stays 0.
	while( newID >= m_drawableVector.size() ) // Fail case is hella rare, so faster to double up on size() call
		m_drawableVector.resize((m_drawableVector.size() > 0) ? (m_drawableVector.size() * 2) : DRAWABLE_HASH_SIZE, nullptr);

	m_drawableVector[ newID ] = draw;

}  // end addDrawableToLookupTable

// ------------------------------------------------------------------------------------------------
/** Remove drawable from lookup table of fast id searching */
// ------------------------------------------------------------------------------------------------
void GameClient::removeDrawableFromLookupTable( Drawable *draw )
{

	// sanity
	if( draw == nullptr )
		return;

	// remove from table
//	m_drawableHash.erase( draw->getID() );
	// Bounds check as in findDrawableByID(): reset() shrinks the table, so a drawable with a larger ID
	// must not write past its end.
	if( static_cast<UnsignedInt>(draw->getID()) < m_drawableVector.size() )
		m_drawableVector[ draw->getID() ] = nullptr;

}  // end removeDrawableFromLookupTable

//-------------------------------------------------------------------------------------------------
/** Load a map into the game interface */
Bool GameClient::loadMap( AsciiString mapName )
{

	// sanity
	if( mapName.isEmpty() )
		return false;

	assert( 0 );  // who calls this?

	return TRUE;

}  // end loadMap

//-------------------------------------------------------------------------------------------------
/** Unload a map from the game interface */
void GameClient::unloadMap( AsciiString mapName )
{

	assert( 0 );  // who calls this?

}  // end unloadMap

//-------------------------------------------------------------------------------------------------
void GameClient::setTimeOfDay( TimeOfDay tod )
{
	Drawable *draw = firstDrawable();

	while( draw )
	{
		draw->setTimeOfDay( tod );

		draw = draw->getNextDrawable();
	}
}

//-------------------------------------------------------------------------------------------------
void GameClient::assignSelectedDrawablesToGroup( Int group )
{
/*
	Drawable *draw = firstDrawable();
	while( draw )
	{
	
		if( draw->isSelected() && draw->getObject()->isLocallyControlled())
		{
			draw->setDrawableGroup( group );
		}
		else if( draw->getDrawableGroup() == group )
		{
			draw->setDrawableGroup( 0 );
		}

		draw = draw->getNextDrawable();

	}
*/
}

//-------------------------------------------------------------------------------------------------
void GameClient::selectDrawablesInGroup( Int group )
{
/*
	Drawable *draw = firstDrawable();

	// create a message that will assign a group ID to all the selected drawables, this
	// way when we do things with this current selected group of units, we only have
	// to refer to the group ID and not each individual object ID
	GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

	//We are creating a new group from scratch.
	teamMsg->appendBooleanArgument( true );

	while( draw )
	{
		int counter = 0;
	
		const Object *object = draw->getObject();

		if( object && draw->getDrawableGroup() == group && object->isLocallyControlled() && !object->isContained() )
		{
			//Only select the object if it is locally controlled and not contained by anything.
			TheInGameUI->selectDrawable(draw);
			teamMsg->appendObjectIDArgument( draw->getObject()->getID() );
		}
		else
		{
			TheInGameUI->deselectDrawable(draw);
		}

		draw = draw->getNextDrawable();
		counter++;
	}

*/
}

// ------------------------------------------------------------------------------------------------
void GameClient::addTextBearingDrawable( Drawable *tbd )
{
	if ( tbd != nullptr )
		m_textBearingDrawableList.push_back( tbd );
}
// ------------------------------------------------------------------------------------------------
void GameClient::flushTextBearingDrawables( void )
{

	/////////////////////////////
	// WALK THIS LIST AND CALL EACH DRAWABLES TEXTY STUFF
	/////////////////////////////

	for( TextBearingDrawableListIterator it = m_textBearingDrawableList.begin(); it != m_textBearingDrawableList.end(); ++it )
	  (*it)->drawUIText();

	m_textBearingDrawableList.clear();
}

// ------------------------------------------------------------------------------------------------
GameMessage::Type GameClient::evaluateContextCommand( Drawable *draw,
																											const Coord3D *pos,
																											CommandTranslator::CommandEvaluateType cmdType )
{

	if( m_commandTranslator )
		return m_commandTranslator->evaluateContextCommand( draw, pos, cmdType );
	else
		return GameMessage::MSG_INVALID;

}  // end evaluateContextCommand

//-------------------------------------------------------------------------------------------------
/** Get the ray effect data for a drawable */
void GameClient::getRayEffectData( Drawable *draw, RayEffectData *effectData )
{

	TheRayEffects->getRayEffectData( draw, effectData );

}  // end getRayEffectData

//-------------------------------------------------------------------------------------------------
/** remove the drawble from the ray effects sytem if present */
void GameClient::removeFromRayEffects( Drawable *draw )
{

	TheRayEffects->deleteRayEffect( draw );

}  // end removeFromRayEffects

/** frees all shadow resources used by this module - used by Options screen.*/
void GameClient::releaseShadows(void)
{
	Drawable *draw;
	for( draw = firstDrawable(); draw; draw = draw->getNextDrawable() )
		draw->releaseShadows();
}

/** create shadow resources if not already present. Used by Options screen.*/
void GameClient::allocateShadows(void)
{
	Drawable *draw;
	for( draw = firstDrawable(); draw; draw = draw->getNextDrawable() )
		draw->allocateShadows();
}

//-------------------------------------------------------------------------------------------------
/** Preload assets for the currently loaded map.  Those assets include all the damage states
	* for every building loaded, as well as any faction units/structures we can build and
	* all their damage states */
//-------------------------------------------------------------------------------------------------
void GameClient::preloadAssets( TimeOfDay timeOfDay )
{

	MEMORYSTATUS before, after;
	GlobalMemoryStatus(&before);

	// first, for every drawable in the map load the assets for all states we care about
	Drawable *draw;
	for( draw = firstDrawable(); draw; draw = draw->getNextDrawable() )
		draw->preloadAssets( timeOfDay );

	//
	// now create a temporary drawble for each of the faction things we can create, preload
	// their assets, and dump the drawable
	//
	AsciiString side;
	const ThingTemplate *tTemplate;
	for( tTemplate = TheThingFactory->firstTemplate();
			 tTemplate;
			 tTemplate = tTemplate->friend_getNextTemplate() )
	{
			
		// if this isn't one of the objects that can be preloaded ignore it
		if( tTemplate->isKindOf( KINDOF_PRELOAD ) == FALSE && !TheGlobalData->m_preloadEverything )
			continue;

		// create the drawable and do the preloading
		draw = TheThingFactory->newDrawable( tTemplate );
		if( draw )
		{

			// preload the assets
			draw->preloadAssets( timeOfDay );

			// destroy the drawable
			TheGameClient->destroyDrawable( draw );

		}  // end if

	}  // end for
	GlobalMemoryStatus(&after);

	DEBUG_LOG(("Preloading memory dwAvailPageFile %zu --> %zu : %zd\n",
		before.dwAvailPageFile, after.dwAvailPageFile, before.dwAvailPageFile - after.dwAvailPageFile));
	DEBUG_LOG(("Preloading memory dwAvailPhys     %zu --> %zu : %zd\n",
		before.dwAvailPhys, after.dwAvailPhys, before.dwAvailPhys - after.dwAvailPhys));
	DEBUG_LOG(("Preloading memory dwAvailVirtual  %zu --> %zu : %zd\n",
		before.dwAvailVirtual, after.dwAvailVirtual, before.dwAvailVirtual - after.dwAvailVirtual));
	/*
	DEBUG_LOG(("Preloading memory dwLength        %zu --> %zu : %zd\n",
		before.dwLength, after.dwLength, before.dwLength - after.dwLength));
	DEBUG_LOG(("Preloading memory dwMemoryLoad    %zu --> %zu : %zd\n",
		before.dwMemoryLoad, after.dwMemoryLoad, before.dwMemoryLoad - after.dwMemoryLoad));
	DEBUG_LOG(("Preloading memory dwTotalPageFile %zu --> %zu : %zd\n",
		before.dwTotalPageFile, after.dwTotalPageFile, before.dwTotalPageFile - after.dwTotalPageFile));
	DEBUG_LOG(("Preloading memory dwTotalPhys     %zu --> %zu : %zd\n",
		before.dwTotalPhys , after.dwTotalPhys, before.dwTotalPhys - after.dwTotalPhys));
	DEBUG_LOG(("Preloading memory dwTotalVirtual  %zu --> %zu : %zd\n",
		before.dwTotalVirtual , after.dwTotalVirtual, before.dwTotalVirtual - after.dwTotalVirtual));
	*/

	GlobalMemoryStatus(&before);
	extern std::vector<AsciiString>	debrisModelNamesGlobalHack;
	Int i;
	for (i=0; i<debrisModelNamesGlobalHack.size(); ++i)
	{
		TheDisplay->preloadModelAssets(debrisModelNamesGlobalHack[i]);
	}
	GlobalMemoryStatus(&after);
	debrisModelNamesGlobalHack.clear();

	DEBUG_LOG(("Preloading memory dwAvailPageFile %zu --> %zu : %zd\n",
		before.dwAvailPageFile, after.dwAvailPageFile, before.dwAvailPageFile - after.dwAvailPageFile));
	DEBUG_LOG(("Preloading memory dwAvailPhys     %zu --> %zu : %zd\n",
		before.dwAvailPhys, after.dwAvailPhys, before.dwAvailPhys - after.dwAvailPhys));
	DEBUG_LOG(("Preloading memory dwAvailVirtual  %zu --> %zu : %zd\n",
		before.dwAvailVirtual, after.dwAvailVirtual, before.dwAvailVirtual - after.dwAvailVirtual));

	TheControlBar->preloadAssets( timeOfDay );

	GlobalMemoryStatus(&before);
	TheParticleSystemManager->preloadAssets( timeOfDay );
	GlobalMemoryStatus(&after);

	DEBUG_LOG(("Preloading memory dwAvailPageFile %zu --> %zu : %zd\n",
		before.dwAvailPageFile, after.dwAvailPageFile, before.dwAvailPageFile - after.dwAvailPageFile));
	DEBUG_LOG(("Preloading memory dwAvailPhys     %zu --> %zu : %zd\n",
		before.dwAvailPhys, after.dwAvailPhys, before.dwAvailPhys - after.dwAvailPhys));
	DEBUG_LOG(("Preloading memory dwAvailVirtual  %zu --> %zu : %zd\n",
		before.dwAvailVirtual, after.dwAvailVirtual, before.dwAvailVirtual - after.dwAvailVirtual));

	const char *textureNames[] = {
		"ptspruce01.tga",
		"exrktflame.tga",
		"cvlimo3_d2.tga",
		"exfthrowerstream.tga",
		"uvrockbug_d1.tga",
		"arcbackgroundc.tga",
		"grade3.tga",
		"framebasec.tga",
		"gradec.tga",
		"frametopc.tga",
		"arcbackgrounda.tga",
		"arcglow2.tga",
		"framebasea.tga",
		"gradea.tga",
		"frametopa.tga",
		"sauserinterface256_002.tga",
		"sauserinterface256_001.tga",
		"unitbackgrounda.tga",
		"sauserinterface256_004.tga",
		"sagentank.tga",
		"sauserinterface256_005.tga",
		"sagenair.tga",
		"sauserinterface256_003.tga",
		"sagenspec.tga",
		"snuserinterface256_003.tga",
		"snuserinterface256_002.tga",
		"unitbackgroundc.tga",
		"snuserinterface256_004.tga",
		"sngenredarm.tga",
		"snuserinterface256_001.tga",
		"sngenspewea.tga",
		"sngensecpol.tga",
		"ciburn.tga",
		"ptmaple02.tga",
		"scuserinterface256_005.tga",
		"scuserinterface256_002.tga",
		"sauserinterface256_006.tga",
		"pmcrates.tga",
		""
	};

	GlobalMemoryStatus(&before);
	for (i=0; *textureNames[i]; ++i)
		TheDisplay->preloadTextureAssets(textureNames[i]);
	GlobalMemoryStatus(&after);

	DEBUG_LOG(("Preloading memory dwAvailPageFile %zu --> %zu : %zd\n",
		before.dwAvailPageFile, after.dwAvailPageFile, before.dwAvailPageFile - after.dwAvailPageFile));
	DEBUG_LOG(("Preloading memory dwAvailPhys     %zu --> %zu : %zd\n",
		before.dwAvailPhys, after.dwAvailPhys, before.dwAvailPhys - after.dwAvailPhys));
	DEBUG_LOG(("Preloading memory dwAvailVirtual  %zu --> %zu : %zd\n",
		before.dwAvailVirtual, after.dwAvailVirtual, before.dwAvailVirtual - after.dwAvailVirtual));

//	preloadTextureNamesGlobalHack2 = preloadTextureNamesGlobalHack;
//	preloadTextureNamesGlobalHack.clear();

}  // end preloadAssets

// ------------------------------------------------------------------------------------------------
/** Given a string name, find the drawable TOC entry (if any) associated with it */
// ------------------------------------------------------------------------------------------------
GameClient::DrawableTOCEntry *GameClient::findTOCEntryByName( AsciiString name )
{

	for( DrawableTOCListIterator it = m_drawableTOC.begin(); it != m_drawableTOC.end(); ++it )
		if( (*it).name == name )
			return &(*it);

	return nullptr;

}  // end findTOCEntryByname

// ------------------------------------------------------------------------------------------------
/** Given a drawable TOC identifier, find the drawable TOC if any */
// ------------------------------------------------------------------------------------------------
GameClient::DrawableTOCEntry *GameClient::findTOCEntryById( UnsignedShort id )
{

	for( DrawableTOCListIterator it = m_drawableTOC.begin(); it != m_drawableTOC.end(); ++it )
		if( (*it).id == id )
			return &(*it);

	return nullptr;

}  // end findTOCEntryById

// ------------------------------------------------------------------------------------------------
/** Add an drawable TOC entry */
// ------------------------------------------------------------------------------------------------
void GameClient::addTOCEntry( AsciiString name, UnsignedShort id )
{

	DrawableTOCEntry tocEntry;
	tocEntry.name = name;
	tocEntry.id = id;
	m_drawableTOC.push_back( tocEntry );

}  // end addTOCEntry

// ------------------------------------------------------------------------------------------------
static Bool shouldSaveDrawable(const Drawable* draw)
{
	if (draw->testDrawableStatus(DRAWABLE_STATUS_NO_SAVE))
	{
		if (draw->getObject() == nullptr)
		{
			return false;
		}
		else
		{
			DEBUG_CRASH(("You should not ever set DRAWABLE_STATUS_NO_SAVE for a Drawable with an object. (%s)\n",draw->getTemplate()->getName().str()));
		}
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
/** Xfer drawable table of contents */
// ------------------------------------------------------------------------------------------------
void GameClient::xferDrawableTOC( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// clear our current table of contents
	m_drawableTOC.clear();

	// xfer the table
	UnsignedInt tocCount = 0;
	if( xfer->getXferMode() == XFER_SAVE )
	{
		AsciiString templateName;

		// generate a new TOC based on the drawables that are in the map
		for( Drawable *draw = getDrawableList(); draw; draw = draw->getNextDrawable() )
		{
			if (!shouldSaveDrawable(draw))
				continue;

			// get the name we're working with
			templateName = draw->getTemplate()->getName();

			// if is this drawable name already in the TOC, skip it
			if( findTOCEntryByName( templateName ) != nullptr )
				continue;
			
			// add this entry to the TOC
			addTOCEntry( draw->getTemplate()->getName(), static_cast<UnsignedShort>(++tocCount) );

		}  // end for obj

		// xfer entries in the TOC
		xfer->xferUnsignedInt( &tocCount );

		// xfer each TOC entry
		DrawableTOCListIterator it;
		DrawableTOCEntry *tocEntry;
		for( it = m_drawableTOC.begin(); it != m_drawableTOC.end(); ++it )
		{

			// get this toc entry
			tocEntry = &(*it);

			// xfer the name
			xfer->xferAsciiString( &tocEntry->name );

			// xfer the paired id
			xfer->xferUnsignedShort( &tocEntry->id );

		}  // end for

	}  // end if
	else
	{
		AsciiString templateName;
		UnsignedShort id;

		// how many entries are we going to read
		xfer->xferUnsignedInt( &tocCount );

		// read all the entries
		for( UnsignedInt i = 0; i < tocCount; ++i )
		{

			// read the name
			xfer->xferAsciiString( &templateName );

			// read the id
			xfer->xferUnsignedShort( &id );
			
			// add this to the TOC
			addTOCEntry( templateName, id );
					
		}  // end for i

	}  // end else

}  // end xferDrawableTOC

// ------------------------------------------------------------------------------------------------
/** Xfer method for Game Client
  * Version History:
  * 1: Initial
  * 2: Adding mission briefing history
	* 3: Added block markers around drawable data, no version checking is done and therefore
	*		 this version breaks compatibility with previous versions. (CBD)
 */
// ------------------------------------------------------------------------------------------------
void GameClient::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// client frame number
	xfer->xferUnsignedInt( &m_frame );

	//
	// note that we do not do the id counter here, we did it in the game state block because
	// it's important to do that part very early in the load process
	//
	// !!!DON'T DO THIS!!! ----> xfer->xferDrawableID( &m_nextDrawableID ); <---- !!!DON'T DO THIS!!! 

	//
	// xfer a table of contents that contain thing template and indentifier pairs.  this
	// table of contents is good for this save file only as unique numbers are only
	// generated and stored for the actual things that are on this map
	//
	xferDrawableTOC( xfer );

	// drawable count
	Drawable *draw;
	UnsignedShort drawableCount = 0;
	for( draw = getDrawableList(); draw; draw = draw->getNextDrawable() )
	{
		if (xfer->getXferMode() == XFER_SAVE && !shouldSaveDrawable(draw))
			continue;
		drawableCount++;
	}
	xfer->xferUnsignedShort( &drawableCount );

	// drawable data
	DrawableTOCEntry *tocEntry;
	ObjectID objectID;
	if( xfer->getXferMode() == XFER_SAVE )
	{
	
		// iterate all drawables
		for( draw = getDrawableList(); draw; draw = draw->getNextDrawable() )
		{
			if (!shouldSaveDrawable(draw))
				continue;

			// get TOC entry for this drawable
			tocEntry = findTOCEntryByName( draw->getTemplate()->getName() );
			if( tocEntry == nullptr )
			{

				DEBUG_CRASH(( "GameClient::xfer - Drawable TOC entry not found for '%s'\n", draw->getTemplate()->getName().str() ));
				throw SaveCode::SC_INVALID_DATA;

			}  // end if

			// xfer toc id entry
			xfer->xferUnsignedShort( &tocEntry->id );

			// begin data block
			xfer->beginBlock();

			// write the object ID this drawable is attached to
			objectID = draw->getObject() ? draw->getObject()->getID() : INVALID_ID;
			xfer->xferObjectID( &objectID );

			// write snapshot data
			xfer->xferSnapshot( draw );

			// end data block
			xfer->endBlock();

		}  // end for, draw

	}  // end if, save
	else
	{
		UnsignedShort tocID;
		const ThingTemplate *thingTemplate;
		Int dataSize;

		// read all entries
		for( UnsignedShort i = 0; i < drawableCount; ++i )
		{

			// read toc id entry
			xfer->xferUnsignedShort( &tocID );

			// find TOC entry with this identifier
			tocEntry = findTOCEntryById( tocID );
			if( tocEntry == nullptr )
			{

				DEBUG_CRASH(( "GameClient::xfer - No TOC entry match for id '%d'\n", tocID ));
				throw SaveCode::SC_INVALID_DATA;

			}  // end if

			// read data block size
			dataSize = xfer->beginBlock();

			// find matching thing template
			thingTemplate = TheThingFactory->findTemplate( tocEntry->name );
			if( thingTemplate == nullptr )
			{

				DEBUG_CRASH(( "GameClient::xfer - Unrecognized thing template '%s', skipping.  ENGINEERS - Are you *sure* it's OK to be ignoring this object from the save file???  Think hard about it!\n",
											tocEntry->name.str() ));
				xfer->skip( dataSize );
				continue;

			}  // end if

			// read the object ID this drawable is attached to (if any)
			xfer->xferObjectID( &objectID );

			//
			// if we have an attached object ID, we won't create a new drawable, we'll use the
			// one that has been created and attached to the object already
			//
			if( objectID != INVALID_ID )
			{
				Object *object = TheGameLogic->findObjectByID( objectID );

				// sanity
				if( object == nullptr )
				{

					DEBUG_CRASH(( "GameClient::xfer - Cannot find object '%d' that is supposed to be attached to this drawable '%s'\n",
												objectID, thingTemplate->getName().str() ));
					throw SaveCode::SC_INVALID_DATA;

				}  // end if

				// get the drawable from the object
				draw = object->getDrawable();
				if( draw == nullptr )
				{

					DEBUG_CRASH(( "GameClient::xfer - There is no drawable attached to the object '%s' (%d) and there should be\n",
												object->getTemplate()->getName().str(), object->getID() ));
					throw SaveCode::SC_INVALID_DATA;

				}  // end if

				// srj sez: some objects (eg, diguised bombtrucks) may have an "abnormal" drawable. so check.
				//
				// note carefully: we do NOT want to use isEquivalentTo() here, because different object reskins
				// SHOULD count as different templates for our purposes here (which are purely visual). however, we
				// do need to compare getFinalOverride, because draw->getTemplate() is always gonna return the final
				// override, while TheThingFactory->findTemplate does not.
				//
				const ThingTemplate* drawTemplate = draw->getTemplate();
				if (drawTemplate->getFinalOverride() != thingTemplate->getFinalOverride())
				{
					TheGameClient->destroyDrawable( draw );
					draw = TheThingFactory->newDrawable( thingTemplate );
					TheGameLogic->bindObjectAndDrawable(object, draw);
				}

			}  // end if
			else
			{

				//
				// there was no object attached to this drawable when we saved, we need to create a
				// whole brand new drawable now
				//
				draw = TheThingFactory->newDrawable( thingTemplate );
				
				// sanity
				if( draw == nullptr )
				{

					DEBUG_CRASH(( "GameClient::xfer - Unable to create drawable for '%s'\n",
												thingTemplate->getName().str() ));
					throw SaveCode::SC_INVALID_DATA;

				}  // end if

			}  // end else

			// xfer the drawable data
			xfer->xferSnapshot( draw );

			// end block (not necessary since this is a no-op but symettrically nice)
			xfer->endBlock();

		}  // end for, i

	}  // end else, load
	
	// xfer the in-game mission briefing history list
	if (version >= 2)
	{
		if( xfer->getXferMode() == XFER_SAVE )
		{
			BriefingList *bList = GetBriefingTextList();
			Int numEntries = static_cast<Int>(bList->size());
			xfer->xferInt(&numEntries);
			DEBUG_LOG(("Saving %d briefing lines\n", numEntries));
			for (BriefingList::const_iterator bIt = bList->begin(); bIt != bList->end(); ++bIt)
			{
				AsciiString tempStr = *bIt;
				DEBUG_LOG(("'%s'\n", tempStr.str()));
				xfer->xferAsciiString(&tempStr);
			}
		}
		else // XFER_LOAD
		{
			Int numEntries = 0;
			xfer->xferInt(&numEntries);
			DEBUG_LOG(("Loading %d briefing lines\n", numEntries));
			UpdateDiplomacyBriefingText(AsciiString::TheEmptyString, TRUE); // clear out briefing list first
			while (numEntries-- > 0)
			{
				AsciiString tempStr;
				xfer->xferAsciiString(&tempStr);
				DEBUG_LOG(("'%s'\n", tempStr.str()));
				UpdateDiplomacyBriefingText(tempStr, FALSE);
			}
		}
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void GameClient::loadPostProcess( void )
{

	//
	// due to the fact that during the load process we have called newDrawable for drawables
	// without objects, and then overwrote their ids with data from the save file, our allocater
	// id may be far higher than it needs to be.  We'll pull it back down as low as we can
	//
	Drawable *draw;
	for( draw = getDrawableList(); draw; draw = draw->getNextDrawable() )
		if( draw->getID() >= m_nextDrawableID )
			m_nextDrawableID = static_cast<DrawableID>(static_cast<UnsignedInt>(draw->getID()) + 1);

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void GameClient::crc( Xfer *xfer )
{

}  // end crc
