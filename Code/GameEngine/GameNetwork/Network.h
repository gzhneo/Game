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

#pragma once

// The network game of the local player: hands the game's commands to the connection manager and
// the commands of a complete frame back to the command list. Network.cpp sets it up,
// NetworkFrames.cpp runs the frames, NetworkQueries.cpp passes the rest on to the connection manager,
// NetworkReturn.cpp brings back a player dropped for not answering.

#include "Engine/RemovalVote.h"
#include "GameNetwork/NetworkInterface.h"

#include <memory>
#include <vector>

class GameWindow;

/**
 * The Network class is used to instantiate a singleton which 
 * implements the interface to all Network operations such as message stream processing and network communications.
 */
class Network : public NetworkInterface
{
public:
	//---------------------------------------------------------------------------------------
	// Setup / Teardown functions
	Network();
	~Network() override;
	void init( void ) override;																				///< Initialize or re-initialize the instance
	void reset( void ) override;																				///< Reinitialize the network
	void update( void ) override;																			///< Process command list
	void liteupdate( void ) override;																	///< Do a lightweight update to send packets and pass messages.
	Bool deinit( void );																			///< Shutdown connections, release memory

	void setLocalAddress(UnsignedInt ip, UnsignedInt port) override;
	inline UnsignedInt getRunAhead(void) override { return m_runAhead; }
	inline UnsignedInt getFrameRate(void) override { return m_frameRate; }
	UnsignedInt getPacketArrivalCushion(void) override;								///< Returns the smallest packet arrival cushion since this was last called.
	Bool isFrameDataReady( void ) override;
	void parseUserList( const GameInfo *game ) override;
	void startGame(void) override;																			///< Sets the network game frame counter to -1

	void sendChat(UnicodeString text, Int playerMask) override;
	void sendDisconnectChat(UnicodeString text) override;

	void sendFile(AsciiString path, UnsignedByte playerMask, UnsignedShort commandID) override;
	UnsignedShort sendFileAnnounce(AsciiString path, UnsignedByte playerMask) override;
	Int getFileTransferProgress(Int playerID, AsciiString path) override;
	Bool areAllQueuesEmpty(void) override;

	void quitGame() override;


	void voteForPlayerDisconnect(Int slot) override;
	Bool voteToRemovePlayer(Int slot) override;
	virtual Bool isPacketRouter( void ) override;
	UnsignedInt getPacketRouterSlot( void ) override { return m_conMgr ? m_conMgr->getPacketRouterSlot() : MAX_SLOTS; }

	// Bandwidth metrics
	Real getIncomingBytesPerSecond( void ) override;
	Real getIncomingPacketsPerSecond( void ) override;
	Real getOutgoingBytesPerSecond( void ) override;
	Real getOutgoingPacketsPerSecond( void ) override;
	Real getUnknownBytesPerSecond( void ) override;
	Real getUnknownPacketsPerSecond( void ) override;

	// Multiplayer Load Progress Functions
	void updateLoadProgress( Int percent ) override;
	void loadProgressComplete( void ) override;
	void sendTimeOutGameStart( void ) override;

#if defined(_DEBUG)
	// Disconnect screen testing
	virtual void toggleNetworkOn() override;
#endif

	// Exposing some info contained in the Connection Manager
	UnsignedInt getLocalPlayerID( void ) override;
	UnicodeString getPlayerName(Int playerNum) override;
	Int getNumPlayers(void ) override;

	Int getAverageFPS() override { return m_conMgr->getAverageFPS(); }
	Int getSlotAverageFPS(Int slot) override;

	void attachTransport(Transport *transport) override;
	void setLockstepEvents(LockstepEvents &events) override;
	void initTransport() override;

	void setSawCRCMismatch( void ) override;
	Bool sawCRCMismatch( void ) override { return m_sawCRCMismatch; }
	Bool isPlayerConnected( Int playerID ) override;
	Bool hasLocalPlayerLeft() override;

	void notifyOthersOfCurrentFrame() override;														///< Tells all the other players what frame we are on.
	void notifyOthersOfNewFrame(UnsignedInt frame) override;								///< Tells all the other players that we are on a new frame.

	Int  getExecutionFrame() override;																			///< Returns the next valid frame for simultaneous command execution.

	// A dropped player comes back (NetworkReturn.cpp). The host announces it; every machine takes it
	// in again in the frame of that command and waits for its commands from the return frame on. The
	// returning player plays the frames before from a feed (the host's replay), as fast as it can,
	// and takes over the host's network state at the return frame.
	void setReturnWindow(UnsignedInt seconds) override;
	Bool isReturnable(Int slot) override;
	UnsignedInt announcePlayerReturn(Int slot, const UnsignedByte *key, UnsignedShort port,
		const ::Engine::WireEndpoint &endpoint, UnsignedInt &prefill) override;
	Bool getReturnState(Int slot, ::Engine::SessionReturnState &out) override;
	void beginReturn(ReturnFeed *feed) override;
	void setReturnFrame(UnsignedInt frame, UnsignedInt prefill, UnsignedInt packetRouter) override;
	void applyReturnState(const ::Engine::SessionReturnState &state) override;
	Bool isReturning() override { return m_localStatus == NETLOCALSTATUS_RETURNING; }
	UnsignedInt getReturnFrame() override { return m_returnFrame; }
	Bool isCatchingUp() override;
	Bool isCRCPending(Int slot) override;
	void noteCRCSkipped(Int slot) override;

	// For disconnect blame assignment
	UnsignedInt getPingFrame() override;
	Int getPingsSent() override;
	Int getPingsRecieved() override;

protected:
	void GetCommandsFromCommandList();														///< Remove commands from TheCommandList and put them on the Network command list.
	void SendCommandsToConnectionManager();												///< Send the new commands to the ConnectionManager
	Bool AllCommandsReady(UnsignedInt frame);											///< Do we have all the commands for the given frame?
	void RelayCommandsToCommandList(UnsignedInt frame);						///< Put the commands for the given frame onto TheCommandList.
	Bool isTransferCommand(GameMessage *msg);											///< Is this a command that needs to be transfered to the other clients?
	Bool processCommand(GameMessage *msg);												///< Whatever needs to be done as a result of this command, do it now.
	void processFrameSynchronizedNetCommand(NetCommandRef *msg);	///< If there is a network command that needs to be executed at the same frame number on all clients, it happens here.
	void processRunAheadCommand(NetRunAheadCommandMsg *msg);			///< Do what needs to be done when we get a new run ahead command.
	void processDestroyPlayerCommand(NetDestroyPlayerCommandMsg *msg);	///< Do what needs to be done when we need to destroy a player.
	void processRemovePlayerVote(NetRemovePlayerVoteCommandMsg *msg);	///< count a vote; remove the player when all others agree
	void processPlayerReturn(NetPlayerReturnCommandMsg *msg);
	void captureReturnStates(UnsignedInt frame);			///< host: after the commands of `frame`
	Bool returningFrameReady(UnsignedInt frame);			///< returning player: the frame can run (and the lockstep takes over at the return frame)
	void setFrameGrouping();								///< from the run ahead and the frame rate
	void removePlayerByVote(Int slot);
	UnsignedByte playersInGame();				///< a bit for each player in the game
	UnsignedByte removalVoters();				///< of those, the ones who vote (not the observers)
	void destroyPlayerUnits(UnsignedInt playerIndex, Bool toAllies);	///< the player's units go to an ally or are destroyed
	void endOfGameCheck();																				///< Checks to see if its ok to leave this game.  If it is, send the apropriate command to the game logic.
	Bool timeForNewFrame();

	std::unique_ptr<ConnectionManager> m_conMgr;																	///< The connection manager object

	UnsignedInt m_lastFrame;																			///< The last game logic frame that was processed.

	NetLocalStatus m_localStatus;															///< My local status as a player in this game.

	Int m_runAhead;																						///< The current run ahead of the game.
	Int m_frameRate;
	Int m_lastExecutionFrame;																	///< The highest frame number that a command could have been executed on.
	Int m_lastFrameCompleted;
	Bool m_didSelfSlug;
	__int64 m_perfCountFreq;														///< The frequency of the performance counter.

	__int64 m_nextFrameTime;														///< When did we execute the last frame?  For slugging the GameLogic...

	Bool m_frameDataReady;																		///< Is the frame data for the next frame ready to be executed by TheGameLogic?

	// CRC info
	Bool m_checkCRCsThisFrame;
	Bool m_sawCRCMismatch;
	std::vector<UnsignedInt> m_CRC[MAX_SLOTS];
	GameWindow *m_messageWindow;
	::Engine::RemovalVote m_removalVote;

	// return of a dropped player
	ReturnFeed *m_returnFeed = nullptr;				///< returning player: the frames before the return frame
	UnsignedInt m_returnFrame = 0;					///< returning player: 0 until the host named it
	Bool m_returnStateApplied = FALSE;
	UnsignedByte m_crcPending = 0;					///< slots whose next CRC is skipped
	UnsignedByte m_crcSkipped = 0;					///< of those, skipped in the current frame
	UnsignedInt m_announcedReturn[MAX_SLOTS] = {};	///< host: the return frame per slot
	::Engine::SessionReturnState m_returnStates[MAX_SLOTS];
	UnsignedByte m_returnStateReady = 0;

#if defined(_DEBUG)
	Bool m_networkOn;
#endif
};
