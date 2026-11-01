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
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/Network.h"

#include "Common/CRCDebug.h"
#include "Common/RandomValue.h"
#include "Common/Recorder.h"
#include "GameClient/GameWindowManager.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptActions.h"
#include "GameLogic/ScriptEngine.h"

// Network: creation, set-up for a game and the CRC mismatch.

#if defined(DEBUG_CRC)
Int NET_CRC_INTERVAL = 1;
#else
Int NET_CRC_INTERVAL = 100;
#endif

/**
 * This creates a network object and returns it.
 */
NetworkInterface *NetworkInterface::createNetwork() 
{
	return NEW Network;
}

////////////////////////////////////////////////////////////////////////////////

/**
 * The constructor.
 */
Network::Network()
{
	//Added By Sadullah Nader
	//Initializations inserted
	m_checkCRCsThisFrame = FALSE;
	m_didSelfSlug = FALSE;
	m_frameDataReady = FALSE;
	m_sawCRCMismatch = FALSE;
	//
	
	m_messageWindow = nullptr;

#if defined(_DEBUG)
	m_networkOn = TRUE;
#endif
}

/**
 * The destructor.
 */
Network::~Network()
{
	deinit();
}

/**
 * This basically releases all the memory.
 */
Bool Network::deinit( void )
{
	if (m_conMgr)
	{
		m_conMgr->destroyGameMessages();
		m_conMgr.reset();
	}
	if (m_messageWindow) {
		TheWindowManager->winDestroy(m_messageWindow);
		m_messageWindow = nullptr;
	}

	return true;
}

/**
 * Takes the network back to the initial state.
 */
void Network::reset() {
	init();
}

/**
 * Initializes all the network subsystems.
 */
void Network::init()
{
	if (!deinit())
	{
		DEBUG_LOG(("Could not deinit network prior to init!\n"));
		return;
	}

	m_conMgr = std::make_unique<ConnectionManager>();
	m_conMgr->init();

	m_lastFrame = 0;
	m_runAhead = min(max(30, MIN_RUNAHEAD), MAX_FRAMES_AHEAD/2); ///< @todo: don't hard-code the run-ahead.
	m_frameRate = 30;
	m_lastExecutionFrame = m_runAhead - 1; // subtract 1 since we're starting on frame 0
	m_lastFrameCompleted = m_runAhead - 1; // subtract 1 since we're starting on frame 0
	m_frameDataReady = FALSE;
	m_didSelfSlug = FALSE;

	m_localStatus = NETLOCALSTATUS_PREGAME;

	QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER *>(&m_perfCountFreq));
	m_nextFrameTime = 0;
	m_sawCRCMismatch = FALSE;
	m_checkCRCsThisFrame = FALSE;

	DEBUG_LOG(("Network timing values:\n"));
	DEBUG_LOG(("NetworkFPSHistoryLength: %d\n", TheGlobalData->m_networkFPSHistoryLength));
	DEBUG_LOG(("NetworkLatencyHistoryLength: %d\n", TheGlobalData->m_networkLatencyHistoryLength));
	DEBUG_LOG(("NetworkRunAheadMetricsTime: %d\n", TheGlobalData->m_networkRunAheadMetricsTime));
	DEBUG_LOG(("NetworkCushionHistoryLength: %d\n", TheGlobalData->m_networkCushionHistoryLength));
	DEBUG_LOG(("NetworkRunAheadSlack: %d\n", TheGlobalData->m_networkRunAheadSlack));
	DEBUG_LOG(("NetworkKeepAliveDelay: %d\n", TheGlobalData->m_networkKeepAliveDelay));
	DEBUG_LOG(("NetworkDisconnectTime: %d\n", TheGlobalData->m_networkDisconnectTime));
	DEBUG_LOG(("NetworkPlayerTimeoutTime: %d\n", TheGlobalData->m_networkPlayerTimeoutTime));
	DEBUG_LOG(("NetworkDisconnectScreenNotifyTime: %d\n", TheGlobalData->m_networkDisconnectScreenNotifyTime));
	DEBUG_LOG(("Other network stuff:\n"));
	DEBUG_LOG(("FRAME_DATA_LENGTH = %d\n", FRAME_DATA_LENGTH));
	DEBUG_LOG(("FRAMES_TO_KEEP = %d\n", FRAMES_TO_KEEP));


#if defined(_DEBUG)
	m_networkOn = TRUE;
#endif

	return;
}

void Network::setSawCRCMismatch( void )
{
	m_sawCRCMismatch = TRUE;

	TheScriptActions->closeWindows( TRUE );
	m_messageWindow = TheWindowManager->winCreateFromScript("Menus/CRCMismatch.wnd");
	TheScriptEngine->startEndGameTimer();

	TheRecorder->logCRCMismatch();

	// dump GameLogic random seed
	DEBUG_LOG(("GameLogic frame = %d\n", TheGameLogic->getFrame()));
	DEBUG_LOG(("GetGameLogicRandomSeedCRC() = %d\n", GetGameLogicRandomSeedCRC()));

	// dump CRCs
	{
		DEBUG_LOG(("--- GameState Dump ---\n"));
#ifdef DEBUG_CRC
		outputCRCDumpLines();
#endif
		DEBUG_LOG(("------ End Dump ------\n"));
	}
	{
		DEBUG_LOG(("--- DebugInfo Dump ---\n"));
#ifdef DEBUG_CRC
		outputCRCDebugLines();
#endif
		DEBUG_LOG(("------ End Dump ------\n"));
	}
}

/**
 * Take a user list and build the connection queues and player lists and stuff like that.
 */
void Network::parseUserList( const GameInfo *game )
{
	if (!game)
	{
		DEBUG_LOG(("FAILED parseUserList with a NULL game\n"));
		return;
	}

	m_conMgr->parseUserList(game);

	// Now that we have the players in this game, we need to reset the FrameData stuff.
	m_conMgr->destroyGameMessages();
	m_conMgr->zeroFrames(1, m_runAhead-1); ///< we zero out m_runAhead frames +1 because the game actually starts at frame 1.
}

/**
 * Guess what, we're starting a game!
 */
void Network::startGame() {
}

/**
 * Tell the network which ip address the user has chosen to use.  Well ok, they probably didn't choose
 * it explicitly, but regardless, this is the one we're going to use.
 */
void Network::setLocalAddress(UnsignedInt ip, UnsignedInt port) {
	DEBUG_ASSERTCRASH(m_conMgr != nullptr, ("Connection manager does not exist."));
	if (m_conMgr != nullptr) {
		m_conMgr->setLocalAddress(ip, port);
	}
}

/**
 * Tell the network to initialize the transport object
 */
void Network::initTransport() {
	DEBUG_ASSERTCRASH(m_conMgr != nullptr, ("Connection manager does not exist."));
	if (m_conMgr != nullptr) {
		m_conMgr->initTransport();
	}
}

void Network::setLockstepEvents(LockstepEvents &events) {
	if (m_conMgr != nullptr) {
		m_conMgr->setEvents(events);
	}
}

void Network::attachTransport(Transport *transport) {
	DEBUG_ASSERTCRASH(m_conMgr != nullptr, ("Connection manager does not exist."));
	if (m_conMgr != nullptr) {
		m_conMgr->attachTransport(transport);
	}
}
