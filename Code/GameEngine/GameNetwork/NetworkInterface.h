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

// NetworkInterface.h ///////////////////////////////////////////////////////////////
// Network singleton class - defines interface to Network methods
// Author: Matthew D. Campbell, July 2001

#pragma once

#ifndef _NETWORK_INTERFACE_H_
#define _NETWORK_INTERFACE_H_

#include "Common/MessageStream.h"
#include "Engine/SessionMessage.h"
#include "Engine/Wire.h"
#include "GameNetwork/ConnectionManager.h"
#include "GameNetwork/User.h"
#include "GameNetwork/NetworkDefs.h"

// forward declarations

struct CommandPacket;
class GameMessage;
class GameInfo;

void ClearCommandPacket(UnsignedInt frame);										///< ClearCommandPacket clears the command packet at the start of the frame.
CommandPacket *GetCommandPacket(void);											///< TheNetwork calls GetCommandPacket to get commands to send.



/**
 * Interface definition for the Network.
 */
class LockstepEvents;

/// Where a returning player's frames before its return frame come from (the host's replay).
class ReturnFeed
{
public:
	virtual ~ReturnFeed() = default;
	/// Every command of the frames below this is there.
	virtual UnsignedInt completeFrames() = 0;
	/// The commands of `frame` onto the command list, in their order; frames come one after the other.
	virtual void appendFrame(UnsignedInt frame, CommandList *list) = 0;
};

class NetworkInterface : public SubsystemInterface
{
protected:

public:
	virtual ~NetworkInterface() override { };

	static NetworkInterface * createNetwork( void );

	//---------------------------------------------------------------------------------------
	// SubsystemInterface functions
	virtual void init( void ) override = 0;																		///< Initialize the network
	virtual void reset( void ) override = 0;																		///< Re-initialize the network
	virtual void update( void ) override = 0;																	///< Updates the network
	virtual void liteupdate( void ) = 0;															///< does a lightweight update for passing messages around.

	virtual void setLocalAddress(UnsignedInt ip, UnsignedInt port) = 0;	///< Tell the network what local ip and port to bind to.
	virtual Bool isFrameDataReady( void ) = 0;												///< Are the commands for the next frame available?
	virtual void parseUserList( const GameInfo *game ) = 0;						///< Parse a userlist, creating connections
	virtual void startGame(void) = 0;																	///< Sets the network game frame counter to -1
	virtual UnsignedInt getRunAhead(void) = 0;												///< Get the current RunAhead value
	virtual UnsignedInt getFrameRate(void) = 0;												///< Get the current allowed frame rate.
	virtual UnsignedInt getPacketArrivalCushion(void) = 0;						///< Get the smallest packet arrival cushion since this was last called.

	// Chat functions
	virtual void sendChat(UnicodeString text, Int playerMask) = 0;		///< Send a chat line using the normal system.
	virtual void sendDisconnectChat(UnicodeString text) = 0;					///< Send a chat line using the disconnect manager.

	virtual void sendFile(AsciiString path, UnsignedByte playerMask, UnsignedShort commandID) = 0;
	virtual UnsignedShort sendFileAnnounce(AsciiString path, UnsignedByte playerMask) = 0;
	virtual Int getFileTransferProgress(Int playerID, AsciiString path) = 0;
	virtual Bool areAllQueuesEmpty(void) = 0;

	virtual void quitGame() = 0;																			///< Quit the game right now.
	

	virtual void voteForPlayerDisconnect(Int slot) = 0;								///< register a vote towards this player's disconnect.
	virtual Bool voteToRemovePlayer(Int slot) = 0;										///< vote to remove `slot` (all other players have to agree); FALSE if there is no such vote
	virtual Bool isPacketRouter( void ) = 0;
	virtual UnsignedInt getPacketRouterSlot( void ) = 0;

	// Bandwidth metrics
	virtual Real getIncomingBytesPerSecond( void ) = 0;
	virtual Real getIncomingPacketsPerSecond( void ) = 0;
	virtual Real getOutgoingBytesPerSecond( void ) = 0;
	virtual Real getOutgoingPacketsPerSecond( void ) = 0;
	virtual Real getUnknownBytesPerSecond( void ) = 0;
	virtual Real getUnknownPacketsPerSecond( void ) = 0;

	virtual void updateLoadProgress( Int percent ) = 0;
	virtual void loadProgressComplete( void ) = 0;
	virtual void sendTimeOutGameStart( void ) = 0;
	virtual UnsignedInt getLocalPlayerID()= 0;
	virtual UnicodeString getPlayerName(Int playerNum)= 0;
	virtual Int getNumPlayers() = 0;

	virtual Int getAverageFPS() = 0;
	virtual Int getSlotAverageFPS(Int slot) = 0;

	virtual void attachTransport(Transport *transport) = 0;
	virtual void setLockstepEvents(LockstepEvents &events) = 0;	///< where players leaving and chat are reported
	virtual void initTransport() = 0;
	virtual Bool sawCRCMismatch() = 0;
	virtual void setSawCRCMismatch() = 0;

	virtual Bool isPlayerConnected(Int playerID) = 0;
	/// The local player has left the running game: the frames it still runs until the game is cleared are its own.
	virtual Bool hasLocalPlayerLeft() { return FALSE; }

	virtual void notifyOthersOfCurrentFrame() = 0;					///< Tells all the other players what frame we are on.
	virtual void notifyOthersOfNewFrame(UnsignedInt frame) = 0;							///< Tells all the other players that we are on a new frame.

	virtual Int  getExecutionFrame() = 0;																			///< Returns the next valid frame for simultaneous command execution.

	// A player dropped for not answering comes back into the running game.
	virtual void setReturnWindow(UnsignedInt seconds) = 0;					///< packet router: how long the units of a dropped player wait
	virtual Bool isReturnable(Int slot) = 0;												///< dropped, units still there, and time left to come back
	/// Host: takes the player back in with its new key; returns the return frame (0: not possible) and
	/// in `prefill` how many frames from there on the player counts ahead at once.
	virtual UnsignedInt announcePlayerReturn(Int slot, const UnsignedByte *key, UnsignedShort port,
		const ::Engine::WireEndpoint &endpoint, UnsignedInt &prefill) = 0;
	/// Host: the network state at the start of the return frame, once the game got there.
	virtual Bool getReturnState(Int slot, ::Engine::SessionReturnState &out) = 0;
	/// Returning player: plays the frames of `feed` until the return frame (not owned).
	virtual void beginReturn(ReturnFeed *feed) = 0;
	virtual void setReturnFrame(UnsignedInt frame, UnsignedInt prefill, UnsignedInt packetRouter) = 0;
	virtual void applyReturnState(const ::Engine::SessionReturnState &state) = 0;
	virtual Bool isReturning() = 0;
	virtual UnsignedInt getReturnFrame() = 0;												///< 0 until the host named it
	virtual Bool isCatchingUp() = 0;																///< frames run without the frame rate
	/// A player back after a drop: its first CRC is not compared (it may fall into another frame).
	virtual Bool isCRCPending(Int slot) = 0;
	virtual void noteCRCSkipped(Int slot) = 0;

#if defined(_DEBUG)
	virtual void toggleNetworkOn( void ) = 0;													///< toggle whether or not to send network traffic.
#endif

	// For disconnect blame assignment
	virtual UnsignedInt getPingFrame() = 0;
	virtual Int getPingsSent() = 0;
	virtual Int getPingsRecieved() = 0;
};


/**
 * ResolveIP turns a string ("games2.westwood.com", or "192.168.0.1") into
 * a 32-bit unsigned integer.
 */
UnsignedInt ResolveIP(AsciiString host);

#endif // _NETWORK_INTERFACE_H_
