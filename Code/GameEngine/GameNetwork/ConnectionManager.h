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

/**
 * The connection manager maintains a collection of Connections to each player.  Messages are queued up
 * from here, as well as removed through here.
 */

#pragma once

#ifndef __CONNECTIONMANAGER_H
#define __CONNECTIONMANAGER_H

#include "GameNetwork/Connection.h"
#include "GameNetwork/NetCommandList.h"
#include "GameNetwork/Transport.h"
#include "GameNetwork/FrameDataManager.h"
#include "GameNetwork/FrameMetrics.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/DisconnectManager.h"
#include "GameNetwork/LockstepEvents.h"
#include "Engine/RecentIds.h"
#include "Engine/SessionMessage.h"
#include "Common/PoolPtr.h"

#include <array>
#include <memory>

class GameInfo;
class NetCommandWrapperList;

using FileCommandMap = std::map<UnsignedShort, AsciiString>;
using FileMaskMap = std::map<UnsignedShort, UnsignedByte>;
using FileProgressMap = std::map<UnsignedShort, Int>;

// The lockstep connections of one network game. Implemented by topic: ConnectionManager.cpp
// (lifetime, update, slot list), ...Receive.cpp, ...Send.cpp, ...Files.cpp, ...RunAhead.cpp,
// ...Players.cpp and ...Return.cpp.
class ConnectionManager final
{
public:
	ConnectionManager();
	~ConnectionManager();
	ConnectionManager(const ConnectionManager &) = delete;
	ConnectionManager &operator=(const ConnectionManager &) = delete;

	void init();				///< Initialize this instance.
	void update(Bool isInGame);			///< Service the Connections being managed by this instance.

	// End SubsystemInterface functions

	void updateRunAhead(Int oldRunAhead, Int frameRate, Bool didSelfSlug, Int nextExecutionFrame);	///< Update the run ahead value.  If we are the current packet router, issue the command.

	void attachTransport(Transport *transport);
	/// Where players leaving, chat and the map transfer are reported; the game's handler by default.
	void setEvents(LockstepEvents &events) { m_events = &events; }
	LockstepEvents &events() { return *m_events; }

	void parseUserList(const GameInfo *game);
	void sendChat(UnicodeString text, Int playerMask, UnsignedInt executionFrame);
	void sendDisconnectChat(UnicodeString text);
	void sendLocalCommand(NetCommandMsg *msg, UnsignedByte relay = 0xff);		///< Send command to the players specified in the relay, goes through packet router.
	void sendLocalCommandDirect(NetCommandMsg *msg, UnsignedByte relay);		///< Send command directly to the players specified, doesn't go through packet router.
	void sendLocalGameMessage(GameMessage *msg, UnsignedInt frame);
	void sendCommand(NetCommandMsg *msg);
	Bool allCommandsReady(UnsignedInt frame, Bool justTesting = FALSE);
	void handleAllCommandsReady(void);
	NetCommandList *getFrameCommandList(UnsignedInt frame);
//	void AddConnection(User *user, UnsignedInt slot);
	void zeroFrames(UnsignedInt startingFrame, UnsignedInt numFrames);
	void destroyGameMessages();
//	void createConnections(UnsignedInt numberOfPlayers, UnsignedInt localSlot);
	void setLocalAddress(UnsignedInt ip, UnsignedInt port);
	void initTransport();
	void processFrameTick(UnsignedInt frame);
	void handleLocalPlayerLeaving(UnsignedInt frame);

	void sendFile(AsciiString path, UnsignedByte playerMask, UnsignedShort commandID);
	UnsignedShort sendFileAnnounce(AsciiString path, UnsignedByte playerMask);
	Int getFileTransferProgress(Int playerID, AsciiString path);
	Bool areAllQueuesEmpty(void);

	UnsignedInt getLocalPlayerID();
	UnicodeString getPlayerName(Int playerNum);
	Int getNumPlayers();

	UnsignedInt getPacketRouterFallbackSlot(Int packetRouterNumber);	///< Returns the slot of the given packet router number in the fallback plan.
	UnsignedInt getPacketRouterSlot();																///< Returns the current packet router's slot.
	PlayerLeaveCode disconnectPlayer(Int slot);												///< Disconnect this player immediately.  This should only be called by the disconnect manager.
	void disconnectLocalPlayer();																			///< Does whatever is necessary to get TheNetwork to realize that it should be leaving the game now.
	void quitGame();																									///< Disconnect from the game RIGHT NOW!!  Tell everyone else we are disconnecting.

	void voteForPlayerDisconnect(Int slot);														///< Register a vote for a player to be disconnected.

	void resendPendingCommands();																			///< Resend the pending commands to the packet router.

	void setFrameGrouping(time_t frameGrouping);				///< Set the number of frames that are grouped together into packets.

	PlayerLeaveCode processPlayerLeave(NetPlayerLeaveCommandMsg *msg);
	PlayerLeaveCode processPlayerLeave(UnsignedByte playerID);			///< `playerID` leaves in the current frame, on every machine
	Bool canILeave();																///< Returns true if the local player is allowed to leave.

	// Bandwidth metrics
	Real getIncomingBytesPerSecond( void );
	Real getIncomingPacketsPerSecond( void );
	Real getOutgoingBytesPerSecond( void );
	Real getOutgoingPacketsPerSecond( void );
	Real getUnknownBytesPerSecond( void );
	Real getUnknownPacketsPerSecond( void );
	UnsignedInt getPacketArrivalCushion( void );

	UnsignedInt getMinimumCushion();

	void flushConnections();

	void processChat(NetChatCommandMsg *msg); // this actually needs to be public because it is frame-synchronized
	
	void updateLoadProgress( Int progress );
	void loadProgressComplete( void );
	void sendTimeOutGameStart( void );
	
	Bool isPacketRouter( void );

	Bool isPlayerConnected( Int playerID );
	Bool isObserver( Int slot ) const { return slot >= 0 && slot < MAX_SLOTS && ((m_observers >> slot) & 1) != 0; }

	void notifyOthersOfCurrentFrame(Int frame);
	void sendFrameDataToPlayer(UnsignedInt playerID, UnsignedInt startingFrame);
	void sendSingleFrameToPlayer(UnsignedInt playerID, UnsignedInt frame);
	void notifyOthersOfNewFrame(UnsignedInt frame);

	UnsignedInt getNextPacketRouterSlot(UnsignedInt playerID); ///< returns the packet router player that comes after the given player.

	// A player dropped for not answering may come back as long as its units are there
	// (ConnectionManagerReturn.cpp). Every machine drops it in the same frame and takes it in again in
	// the same frame; the packet router destroys its units once the return window has run out.
	void markReturnable(Int slot);					///< dropped now by the disconnect procedure
	Bool isReturnable(Int slot) const;
	UnsignedInt getDropFrame(Int slot) const;
	/// Returnable and not back yet: commands under its number are neither taken nor acknowledged, so
	/// that the packet router sends them again once the player is back here too.
	Bool isAwaitedReturn(Int slot) const;
	void setReturnWindow(UnsignedInt frames) { m_returnWindowFrames = frames; }
	UnsignedInt getReturnWindow() const { return m_returnWindowFrames; }
	/// In the frame of the command, on every machine: connection and frame data again, its commands
	/// waited for from the return frame on. `localTicked`: the last frame we sent our command count for.
	Bool admitReturningPlayer(NetPlayerReturnCommandMsg *msg, UnsignedInt localTicked);
	/// Its units were destroyed: it cannot come back any more.
	void closeReturn(Int slot);
	/// Packet router: destroys the units of players whose return window ran out.
	void expireReturns(UnsignedInt frame, UnsignedInt executionFrame);
	/// The state a returning player starts its return frame with.
	void captureReturnState(::Engine::SessionReturnState &out);

	// the returning player's side
	void restartFrameData();								///< no frame known: they come from the others
	void setPacketRouterSlot(UnsignedInt slot) { m_packetRouterSlot = slot; }
	void applyReturnState(const ::Engine::SessionReturnState &state);
	
	Int getAverageFPS( void );
	Int getSlotAverageFPS(Int slot);

#if defined(_DEBUG)
	void debugPrintConnectionCommands();
#endif

	// For disconnect blame assignment
	UnsignedInt getPingFrame();
	Int getPingsSent();
	Int getPingsRecieved();

private:
	void doRelay();
	Bool isRepeatedArrival(NetCommandMsg *msg);
	void doKeepAlive();
	void sendRemoteCommand(NetCommandRef *msg, Bool keep = TRUE);	///< relay, and keep it in the frame data unless `keep` is FALSE
	void observerLeft(NetCommandRef *ref);			///< an observer left: relay it and let it go now
	void dropLaggingObservers();					///< observers too far behind are let go here (players never wait for them)
	void ackCommand(NetCommandRef *ref, UnsignedInt localSlot);

	Bool processNetCommand(NetCommandRef *ref);
	void processAckStage1(NetCommandMsg *msg);
	void processAckStage2(NetCommandMsg *msg);
	void processAck(NetCommandMsg *msg);
	void processFrameInfo(NetFrameCommandMsg *msg);
	void processRunAheadMetrics(NetRunAheadMetricsCommandMsg *msg);
	void processDisconnectChat(NetDisconnectChatCommandMsg *msg);
	void processProgress( NetProgressCommandMsg *msg );
	void processLoadComplete( NetCommandMsg *msg );
	void processTimeOutGameStart( NetCommandMsg *msg );
	void processWrapper(NetCommandRef *ref);
	void processFrameResendRequest(NetFrameResendRequestCommandMsg *msg);

	void processFile(NetFileCommandMsg *ref);
	void processFileAnnounce(NetFileAnnounceCommandMsg *ref);
	void processFileProgress(NetFileProgressCommandMsg *ref);

	//	void doPerFrameMetrics(UnsignedInt frame);
	void getMinimumFps(Int &minFps, Int &minFpsPlayer);			///< Returns the smallest FPS in the m_fpsAverages list.
	Real getMaximumLatency(); ///< This actually sums the two biggest latencies in the m_latencyAverages list.

	void requestFrameDataResend(Int playerID, UnsignedInt frame); ///< request of this player that he send the specified frame's data.
	void sendReturnBacklog(Int slot, UnsignedInt returnFrame, UnsignedInt localTicked);	///< what we hold of its frames
	void sendPlayerDestruct(Int slot, UnsignedInt executionFrame);

	std::unique_ptr<Transport> m_transport;	///< before the connections: they keep a pointer to it

	// The connections are set up like the slot list; none for the local player and for slots
	// without a player.
	std::array<PoolPtr<Connection>, MAX_SLOTS> m_connections;
	UnsignedInt m_localSlot;
	UnsignedByte m_observers = 0;					///< a bit for each observer: players do not wait for them, their commands are not executed
	UnsignedInt m_observerFrameInfo[MAX_SLOTS] = {};	///< per observer: the last frame it sent a command count for
	Int m_currentRunAhead = 0;
	UnsignedInt m_packetRouterSlot;
	UnsignedInt m_packetRouterFallback[MAX_SLOTS];
	UnsignedInt m_localAddr;
	UnsignedInt m_localPort;
	PoolPtr<User> m_localUser;

	std::unique_ptr<DisconnectManager> m_disconnectManager;	///< waiting for overdue players, dropping them

	std::array<PoolPtr<FrameDataManager>, MAX_SLOTS> m_frameData;

	PoolPtr<NetCommandList> m_pendingCommands;
	PoolPtr<NetCommandList> m_relayedCommands;

	FrameMetrics m_frameMetrics;
	::Engine::RunAheadController m_runAheadController;	///< packet router: damps the changes of the run ahead

	PoolPtr<NetCommandWrapperList> m_netCommandWrapperList;

	// These variables are used to keep track of the other players' average fps and latency.
	// yup.
	Real m_latencyAverages[MAX_SLOTS];
	Int  m_fpsAverages[MAX_SLOTS];
	Int  m_minFpsPlayer;
	Int  m_minFps;
	UnsignedInt m_smallestPacketArrivalCushion;
	Bool m_didSelfSlug;

	// map transfer: file per command, its recipients, and the progress each player reported
	FileCommandMap m_fileCommands;
	FileMaskMap m_fileRecipients;
	FileProgressMap m_fileProgress[MAX_SLOTS];

	time_t m_keepAliveStart = 0;		///< keep-alives go to one slot per second from here
	Int m_keepAliveNext = 0;
	LockstepEvents *m_events = &LockstepEvents::game();
	std::array< ::Engine::RecentIds<128>, MAX_SLOTS > m_arrivedIds;	///< per sender: commands acted on when they arrived
	time_t m_lastRunAheadSent = 0;		///< packet router: when the run ahead was last set

	struct ReturnSlot
	{
		Bool returnable = FALSE;
		UnsignedInt dropFrame = 0;
		Bool destroySent = FALSE;		///< packet router: the end of its window is on the way
	};
	std::array<ReturnSlot, MAX_SLOTS> m_returns;
	UnsignedInt m_returnWindowFrames = 300 * LOGICFRAMES_PER_SECOND;
};

#endif
