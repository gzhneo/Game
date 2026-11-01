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

// GameLobby.h ///////////////////////////////////////////////////////////////
// GameLobby singleton class - lobby and game setup over the session layer (Engine::SessionMessage): games in
// the LAN, by address and by invitation over rendezvous servers.
// Author: Matthew D. Campbell, October 2001

#pragma once

#ifndef GAMENETWORK_GAMELOBBY_H
#define GAMENETWORK_GAMELOBBY_H

#include "Engine/LocalNetworks.h"
#include "Engine/PacketSeal.h"
#include "Engine/RateLimiter.h"
#include "Engine/Rendezvous.h"
#include "Engine/SessionMessage.h"
#include "Engine/WatchStream.h"
#include "GameNetwork/LANGameInfo.h"
#include "GameNetwork/LANPlayer.h"
#include "GameNetwork/Lobby/GameLinks.h"
#include "GameNetwork/Lobby/HostRoom.h"
#include "GameNetwork/Lobby/LobbyDirectory.h"
#include "GameNetwork/Lobby/LobbyWire.h"
#include "GameNetwork/Lobby/PublicGames.h"
#include "GameNetwork/Lobby/RendezvousServers.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/Transport.h"

#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

inline constexpr Int g_lanPlayerNameLength = 12;		///< short because names travel in the game options
inline constexpr Int g_lanLoginNameLength = 1;
inline constexpr Int g_lanHostNameLength = 1;
inline constexpr Int g_lanGameNameLength = 16;
inline constexpr Int g_lanGameNameReservedLength = 16;
inline constexpr Int g_lanMaxChatLength = 100;
inline constexpr Int m_lanMaxOptionsLength = MAX_PACKET_SIZE - ( 8 + (g_lanGameNameLength+1)*2 + 4 + (g_lanPlayerNameLength+1)*2
	+ (g_lanLoginNameLength+1) + (g_lanHostNameLength+1) );

/// Version of the session protocol; raised with every incompatible change. A host turns away
/// joiners with another version and says so.
inline constexpr UnsignedInt LAN_PROTOCOL_VERSION = 8;
inline constexpr UnsignedShort LAN_LOBBY_PORT = 8086;	///< session messages
AsciiString LANVersionText( void );	///< this program's version and build time
/// 16-bit summaries of the program and game data checksums for the game list; the join compares
/// the full checksums.
UnsignedShort LANProgramTag( void );
UnsignedShort LANDataTag( void );

/**
 * The GameLobby class is used to instantiate a singleton which 
 * implements the interface to all LAN broadcast communications.
 */
class GameLobbyInterface : public SubsystemInterface
{
public:

	virtual ~GameLobbyInterface() override { };

	virtual void init( void ) override = 0;															///< Initialize or re-initialize the instance
	virtual void reset( void ) override = 0;															///< reset the logic system
	virtual void update( void ) override = 0;														///< update the world

	virtual void setIsActive(Bool isActive ) = 0;								///< Tell TheLobby whether or not the app is active.
	/// Every pass of the main loop, also in a game: the lobby menus call update() instead.
	virtual void serviceInGame( void ) = 0;

	// Possible types of chat messages
	enum ChatType
	{
		LANCHAT_NORMAL = 0,
		LANCHAT_EMOTE,
		LANCHAT_SYSTEM,
		LANCHAT_MAX
	};

	// Request functions generate network traffic
	virtual void RequestLocations( void ) = 0;																				///< Request everybody to respond with where they are
	virtual void RequestGameJoin( LANGameInfo *game, UnsignedInt ip = 0 ) = 0;				///< Request to join a game
	virtual void RequestGameJoinDirectConnect( UnsignedInt ipaddress ) = 0;						///< Request to join a game at an IP address
	virtual void RequestGameLeave( void ) = 0;																				///< Tell everyone we're leaving
	virtual void RequestAccept( void ) = 0;																						///< Indicate we're OK with the game options
	virtual void RequestHasMap( void ) = 0;																						///< Send our map status
	virtual void RequestChat( UnicodeString message, ChatType format ) = 0;						///< Send a chat message
	virtual void RequestGameStart( void ) = 0;																				///< Tell everyone the game is starting
	virtual void RequestGameStartTimer( Int seconds ) = 0;
	virtual void RequestGameOptions( AsciiString gameOptions, Bool isPublic, UnsignedInt ip = 0 ) = 0;		///< Change the game options
	virtual void RequestGameCreate( UnicodeString gameName, Bool isDirectConnect ) = 0;	///< Try to host a game
	virtual void RequestGameAnnounce( void ) = 0;																			///< Sound out current game info if host
	virtual void RequestSetName( UnicodeString newName ) = 0;													///< Pick a new name
	virtual void RequestLobbyLeave( Bool forced ) = 0;																///< Announce that we're leaving the lobby
	virtual void ResetGameStartTimer( void ) = 0;

	// Possible result codes passed to On functions
	enum ReturnType
	{
		RET_OK,							// Any function
		RET_TIMEOUT,				// OnGameJoin/Leave/Start, etc
		RET_GAME_FULL,			// OnGameJoin
		RET_DUPLICATE_NAME,	// OnGameJoin
		RET_CRC_MISMATCH,		// OnGameJoin
		RET_SERIAL_DUPE,		// OnGameJoin
		RET_GAME_STARTED,		// OnGameJoin
		RET_GAME_EXISTS,		// OnGameCreate
		RET_GAME_GONE,			// OnGameJoin
		RET_BUSY,						// OnGameCreate/Join/etc if another action is in progress
		RET_UNKNOWN,				// Default message for oddity
		RET_VERSION_MISMATCH,	// OnGameJoin: other program version or network protocol
		RET_DATA_MISMATCH,	// OnGameJoin: same program, other game data (INI files, mods)
		RET_HOST_KEY_MISMATCH,	// OnGameJoin: the host's key does not match the game key typed with its address
		RET_NOT_FOUND,			// OnGameJoin: no rendezvous server knows the invitation
		RET_MAX
	};
	UnicodeString getErrorStringFromReturnType( ReturnType ret );

	// On functions are (generally) the result of network traffic
	virtual void OnGameList( LANGameInfo *gameList ) = 0;																							///< List of games
	virtual void OnPlayerList( LANPlayer *playerList ) = 0;																				///< List of players in the Lobby
	virtual void OnGameJoin( ReturnType ret, LANGameInfo *theGame ) = 0;															///< Did we get in the game?
	virtual void OnPlayerJoin( Int slot, UnicodeString playerName ) = 0;													///< Someone else joined our game (host only; joiners get a slotlist)
	virtual void OnHostLeave( void ) = 0;																													///< Host left the game
	virtual void OnPlayerLeave( UnicodeString player ) = 0;																				///< Someone left the game
	virtual void OnAccept( UnsignedInt playerIP, Bool status ) = 0;																///< Someone's accept status changed
	virtual void OnHasMap( UnsignedInt playerIP, Bool status ) = 0;																///< Someone's map status changed
	virtual void OnChat( UnicodeString player, UnsignedInt ip,
											 UnicodeString message, ChatType format ) = 0;														///< Chat message from someone
	virtual void OnGameStart( void ) = 0;																													///< The game is starting
	virtual void OnGameStartTimer( Int seconds ) = 0;
	virtual void OnGameOptions( UnsignedInt playerIP, Int playerSlot, AsciiString options ) = 0;	///< Someone sent game options
	virtual void OnGameCreate( ReturnType ret ) = 0;																							///< Your game is created
	virtual void OnNameChange( UnsignedInt IP, UnicodeString newName ) = 0;												///< Someone has morphed

	// Misc utility functions
	virtual LANGameInfo * LookupGame( UnicodeString gameName ) = 0;														///< return a pointer to a game we know about
	virtual LANGameInfo * LookupGameByListOffset( Int offset ) = 0;														///< return a pointer to a game we know about
	virtual Bool SetLocalIP( UnsignedInt localIP ) = 0;																		///< For multiple NIC machines
	virtual void SetLocalIP( AsciiString localIP ) = 0;																		///< For multiple NIC machines
	virtual Bool AmIHost( void ) = 0;																											///< Am I hosting a game?
	virtual inline UnicodeString GetMyName( void ) = 0;																		///< What's my name?
	virtual inline LANGameInfo *GetMyGame( void ) = 0;															          ///< What's my Game?
	virtual void checkMOTD( void ) = 0;
};


/**
 * The game lobby: finds games in the LAN, joins them by list, address or invitation code and sets
 * them up with the other players until the start. Built from the pieces in GameNetwork/Lobby:
 * the directory of games and players, the ways between the game sockets, the host's room at a
 * rendezvous server and the list of those servers. The members are split over four files:
 * GameLobby.cpp (lifecycle, update, menu requests), GameLobbyHost.cpp (hosting),
 * GameLobbyJoin.cpp (joining, member side), GameLobbyReceive.cpp (every received message).
 */
class GameLobby : public GameLobbyInterface
{
public:

	GameLobby();
	virtual ~GameLobby() override;
	GameLobby( const GameLobby & ) = delete;
	GameLobby &operator=( const GameLobby & ) = delete;

	virtual void init( void ) override;
	virtual void reset( void ) override;
	virtual void update( void ) override;

	virtual void setIsActive( Bool isActive ) override;	///< alt-tabbed out: the host takes back our readiness

	// Request functions generate network traffic
	virtual void RequestLocations( void ) override;
	virtual void RequestGameJoin( LANGameInfo *game, UnsignedInt ip = 0 ) override;
	virtual void RequestGameJoinDirectConnect( UnsignedInt ipaddress ) override;
	virtual void RequestGameLeave( void ) override;
	virtual void RequestAccept( void ) override;
	virtual void RequestHasMap( void ) override;
	virtual void RequestChat( UnicodeString message, ChatType format ) override;
	virtual void RequestGameStart( void ) override;
	virtual void RequestGameStartTimer( Int seconds ) override;
	virtual void RequestGameOptions( AsciiString gameOptions, Bool isPublic, UnsignedInt ip = 0 ) override;
	virtual void RequestGameCreate( UnicodeString gameName, Bool isDirectConnect ) override;
	virtual void RequestGameAnnounce( void ) override;
	virtual void RequestSetName( UnicodeString newName ) override;
	virtual void RequestLobbyLeave( Bool forced ) override;
	virtual void ResetGameStartTimer( void ) override;

	/// Joins the game hosted at this endpoint (lobby port); used by direct connect.
	void RequestGameJoinAt( const ::Engine::NetAddress &host );
	/// Joins by an invitation code ("K7Q2-DP941QG2@server"); false if the text is none.
	Bool RequestGameJoinInvite( AsciiString code );

	/// Every pass of the main loop: outside the lobby menus the host serves spectators of its running
	/// game, and a spectator receives the game it watches (GameLobbyWatch.cpp).
	virtual void serviceInGame( void ) override;
	/// Spectators of the game we host.
	Int spectatorCount( void ) const { return static_cast<Int>(m_watchHost.spectators.size()); }
	/// Whether we watch a game (from the welcome until the replay ends).
	Bool isWatching( void ) const { return m_watch.active; }
	/// Tests: the replay file the host streams, instead of the recorder's; empty for the recorder's.
	void setWatchSourceForTest( AsciiString path, UnsignedInt frames ) { m_watchHost.testPath = path; m_watchHost.testFrames = frames; }
	/// Tests: the bytes and frames a spectator holds, and whether it would start its replay.
	UnsignedInt watchedBytesForTest( void ) const { return m_watch.receiver.received(); }
	UnsignedInt watchedFramesForTest( void ) const { return m_watch.receiver.playableFrames(); }
	Bool watchStartedForTest( void ) const { return m_watch.started; }
	void setWatchPlaybackForTest( Bool startReplay ) { m_watch.startReplay = startReplay; }
	/// Frames the stream lags behind the players (anti cheating; 0: none).
	void setWatchDelayFrames( UnsignedInt frames ) { m_watchHost.delayFrames = frames; m_watchHost.delaySet = TRUE; }

	/// Back into the running game we were dropped from, with what Rejoin.ini keeps of it
	/// (GameLobbyReturn.cpp); false if nothing is kept.
	Bool RequestGameReturn( void );
	/// A game we were dropped from that may take us back (Rejoin.ini, not too old): its host's name.
	Bool hasReturnOffer( UnicodeString &hostName );
	/// Whether we are on the way back into a game (from the welcome until we play in the lockstep).
	Bool isReturning( void ) const { return m_return.active; }
	/// Tests: the return frame the host named (0: none yet) and whether we play in the lockstep again.
	UnsignedInt returnFrameForTest( void ) const { return m_return.returnFrame; }
	/// Tests: where Rejoin.ini is kept (empty: the user data folder).
	void setReturnTicketPathForTest( AsciiString path ) { m_returnTicketPath = path; }
	/// Tests: the game does not start; only the stream and the messages of the return are handled.
	void setReturnStartForTest( Bool startGame ) { m_returnStartGame = startGame; }

	// On functions are (generally) the result of network traffic (GameLobbyCallbacks.cpp)
	virtual void OnGameList( LANGameInfo *gameList ) override;
	virtual void OnPlayerList( LANPlayer *playerList ) override;
	virtual void OnGameJoin( ReturnType ret, LANGameInfo *theGame ) override;
	virtual void OnPlayerJoin( Int slot, UnicodeString playerName ) override;
	virtual void OnHostLeave( void ) override;
	virtual void OnPlayerLeave( UnicodeString player ) override;
	virtual void OnAccept( UnsignedInt playerIP, Bool status ) override;
	virtual void OnHasMap( UnsignedInt playerIP, Bool status ) override;
	virtual void OnChat( UnicodeString player, UnsignedInt ip, UnicodeString message, ChatType format ) override;
	virtual void OnGameStart( void ) override;
	virtual void OnGameStartTimer( Int seconds ) override;
	virtual void OnGameOptions( UnsignedInt playerIP, Int playerSlot, AsciiString options ) override;
	virtual void OnGameCreate( ReturnType ret ) override;
	virtual void OnNameChange( UnsignedInt IP, UnicodeString newName ) override;
	virtual void OnInActive( UnsignedInt IP );

	// Misc utility functions
	virtual LANGameInfo *LookupGame( UnicodeString gameName ) override { return m_directory.findGame(gameName); }
	virtual LANGameInfo *LookupGameByListOffset( Int offset ) override { return m_directory.gameAt(offset); }
	virtual LANPlayer *LookupPlayer( UnsignedInt playerIP ) { return m_directory.findPlayer(playerIP); }
	virtual Bool SetLocalIP( UnsignedInt localIP ) override;
	virtual void SetLocalIP( AsciiString localIP ) override;
	virtual Bool AmIHost( void ) override;
	virtual UnicodeString GetMyName( void ) override { return m_name; }
	virtual LANGameInfo *GetMyGame( void ) override { return m_currentGame; }
	virtual UnsignedInt GetLocalIP( void ) { return m_localIP; }	///< our identity in the current game, else the bound address
	Bool hasIPv6( void ) const { return m_transport && m_transport->hasIPv6(); }
	/// Direct connect: the game key the host must present ("K7Q2-9XPA"); empty for none.
	void setExpectedHostCode( AsciiString code ) { m_expectedHostCode = code; }
	virtual void checkMOTD( void ) override;

	/// Whether we could join the game as far as the host's announcement tells: RET_OK,
	/// RET_VERSION_MISMATCH (other program or protocol) or RET_DATA_MISMATCH.
	ReturnType compatibilityOf( LANGameInfo *game );

	/// Hands the game socket to the game at the start (the caller owns it then); nullptr if none is open.
	Transport *takeGameTransport( void );

	/// LAN: games and players of the local network (broadcasts). Internet: the games rendezvous
	/// servers list publicly; no broadcasts and no chat outside a game.
	enum class Mode : UnsignedByte { Lan, Internet };
	void setMode( Mode mode );
	/// The mode the next lobby screen opens in (main menu: Network or Internet).
	static void setMenuMode( Mode mode );
	static Mode menuMode( void );
	Mode mode( void ) const { return m_mode; }
	/// Whether the rendezvous server lists the game we host publicly (each new game starts private).
	void setListedPublicly( Bool listed );
	Bool listedPublicly( void ) const { return m_listPublicly; }
	/// Internet: what the lobby offers and the chat commands, as chat lines.
	void showModeHelp( void );

	// Tests: other ports than 8086/8088 and explicit discovery targets instead of broadcasts, so that
	// several instances run in one process on one address.
	void setPortsForTest( UnsignedShort lobbyPort, UnsignedShort gamePort ) { m_lobbyPort = lobbyPort; m_gamePort = gamePort; }
	void setDiscoveryTargetsForTest( const std::vector< ::Engine::NetAddress > &targets ) { m_discoveryTargets = targets; }
	/// Every peer's game socket through the relay, as if no direct way opened.
	void forceRelayForTest( Bool force ) { m_forceRelay = force; }
	/// The game socket is a transport without socket (nothing is sent or received).
	void setOfflineForTest( Bool offline ) { m_offline = offline; }
	Transport *getLobbyTransport( void ) { return m_transport.get(); }
	Transport *getGameTransport( void ) { return m_gameTransport.get(); }
	/// Rendezvous servers (in the order they are tried); fixed ones are not read from files.
	void setRendezvousServers( const std::vector<std::string> &servers ) { m_servers.setFixed(servers); }
	const std::vector<std::string> &getRendezvousServers( void ) const { return m_servers.list(); }
	AsciiString getInviteCode( void ) const { return m_inviteCode; }
	Bool isGameReachable( Int slot ) const { return m_links.reachable(slot); }
	Bool isGameRelayed( Int slot ) const { return m_links.relayed(slot); }
	UnsignedInt getMessagesBuiltForTest( void ) const { return m_messagesBuilt; }

protected:

	enum PendingActionType
	{
		ACT_NONE = 0,
		ACT_JOIN,
		ACT_JOINDIRECTCONNECT,
		ACT_LEAVE,
		ACT_MAX
	};

	/// A join in progress (joiner side).
	struct JoinAttempt
	{
		Bool active = FALSE;
		Bool challenged = FALSE;			///< CHALLENGE received, JOIN being sent
		::Engine::NetAddress candidates[::Engine::TREFF_MAX_ENDPOINTS + 1];	///< where the host may answer
		Int candidateCount = 0;
		::Engine::NetAddress host;			///< the endpoint that answered
		UnsignedInt64 sessionId = 0;		///< 0: whatever game the host runs (direct connect)
		UnsignedByte nonce[8] = {};
		UnsignedByte hostKey[::Engine::SEAL_KEY_SIZE] = {};
		UnsignedByte cookie[16] = {};
		AsciiString expectedCode;			///< fingerprint text the host key must match; empty for none
		Int returnSlot = -1;				///< SessionWants::RETURN: the slot we come back to
		UnsignedByte returnToken[::Engine::SESSION_TOKEN_SIZE] = {};
		Bool hasFingerprint = FALSE;		///< fingerprint from the game's announcement
		UnsignedByte fingerprint[5] = {};
		UnsignedInt64 gameSessionId = 0;	///< the list entry joined (0: none)
		UnsignedInt nextSend = 0;
		UnsignedInt deadline = 0;
		std::vector<UnsignedByte> joinMessage;	///< JOIN as sent, repeated until WELCOME
		std::vector<Transport::RawDatagram> early;	///< sealed messages of the host before WELCOME
		// invitation: servers still to ask
		std::string room;
		std::vector<std::string> servers;
		Int serverIndex = 0;
		::Engine::NetAddress server;
		UnsignedInt lookupTag = 0;
		UnsignedInt nextLookup = 0;
		Int lookups = 0;
		Bool found = FALSE;
		::Engine::SessionWants wants = ::Engine::SessionWants::PLAY;
	};

	/// A spectator of the game we host, or a player coming back into it (returnSlot >= 0): it gets
	/// the stream without delay to catch up, then the return frame and the state there.
	struct Spectator
	{
		UnsignedInt identity = 0;
		UnicodeString name;
		::Engine::WatchSender sender;
		UnsignedInt heard = 0;
		UnsignedInt lastSend = 0;
		// a returning player
		Int returnSlot = -1;
		UnsignedByte key[::Engine::SESSION_KEY_SIZE] = {};
		UnsignedShort gamePort = 0;
		::Engine::WireEndpoint gameEndpoint;		///< where its game socket is likely seen
		::Engine::SessionEndpoints endpoints;
		UnsignedInt returnFrame = 0;				///< announced
		UnsignedInt prefill = 0;
		Bool admitted = FALSE;						///< the lockstep took it in
		Bool done = FALSE;							///< it has the state of the return frame
		UnsignedInt lastReturnSend = 0;
	};

	/// Host: the running game's replay as it grows, and who watches it.
	struct WatchHost
	{
		std::vector<Spectator> spectators;
		::Engine::WatchHistory history;
		std::vector<UnsignedByte> log;		///< the replay file as read so far
		FILE *file = nullptr;
		Bool finished = FALSE;				///< the recording ended and everything is read
		UnsignedInt delayFrames = 0;
		Bool delaySet = FALSE;				///< delayFrames is set; else it comes from the settings
		AsciiString testPath;
		UnsignedInt testFrames = 0;
	};

	/// Spectator: the game we watch.
	struct WatchFeed
	{
		Bool active = FALSE;
		UnsignedInt hostIdentity = 0;
		::Engine::WatchReceiver receiver;
		FILE *file = nullptr;				///< the replay as it arrives
		Bool started = FALSE;				///< the replay plays
		Bool startReplay = TRUE;			///< tests: only receive
		Bool ended = FALSE;					///< the host's recording is complete
		UnsignedInt endBytes = 0;
		UnsignedInt hostHeard = 0;
		UnsignedInt lastAck = 0;
		Bool ackDue = FALSE;
	};

	/// What only the host of a game keeps.
	struct HostState
	{
		UnsignedByte cookieKey[32] = {};
		UnsignedByte oldCookieKey[32] = {};
		UnsignedInt cookieKeyTime = 0;
		UnsignedInt stateRevision = 0;
		AsciiString lastGameOptions;		///< what STATE carries
		UnsignedInt lastState = 0;
		UnsignedInt lastAnnounce = 0;
		UnsignedInt memberHeard[MAX_SLOTS] = {};
		Bool membersChanged = FALSE;		///< MEMBER and RELAY go out again
		UnsignedByte returnTokens[MAX_SLOTS][::Engine::SESSION_TOKEN_SIZE] = {};	///< what a dropped player comes back with
	};

	/// A player on the way back into a game it was dropped from: the host's stream to catch up with.
	struct ReturnState
	{
		Bool active = FALSE;
		UnsignedInt hostIdentity = 0;
		Int slot = -1;
		::Engine::WireEndpoint hostLobby;	///< where we reach the host
		::Engine::WatchReceiver receiver;
		FILE *file = nullptr;				///< the stream as it arrives
		FILE *read = nullptr;				///< the same file, read by the feed
		Bool headerRead = FALSE;
		AsciiString gameOptions;			///< the game as it started (replay header)
		long position = 0;					///< feed: the next command
		Bool havePending = FALSE;			///< feed: the frame number of the next command is read
		UnsignedInt pendingFrame = 0;
		Bool broken = FALSE;				///< the stream cannot be read
		Bool started = FALSE;				///< the game runs
		UnsignedInt returnFrame = 0;
		Bool stateApplied = FALSE;
		UnsignedInt hostHeard = 0;
		UnsignedInt lastAck = 0;
		Bool ackDue = FALSE;
		UnsignedInt lastReady = 0;
		UnsignedInt waitSince = 0;			///< before the start: since when the game is due
	};

	/// The returning player's frames before the return frame, from the stream.
	class StreamFeed final : public ReturnFeed
	{
	public:
		explicit StreamFeed( GameLobby &lobby ) : m_lobby( lobby ) {}
		UnsignedInt completeFrames() override;
		void appendFrame( UnsignedInt frame, CommandList *list ) override;
	private:
		GameLobby &m_lobby;
	};
	friend class StreamFeed;

	// who we are, our sockets
	UnicodeString m_name;
	AsciiString m_userName;
	AsciiString m_hostName;
	UnsignedInt m_localIP;					///< our identity: the bound address, or in a game the identity of our key
	UnsignedInt m_bindIP;					///< the local IPv4 address the sockets use; 0: every adapter
	std::vector< ::Engine::LocalNetwork > m_lanNetworks;	///< with m_bindIP 0: the networks of the adapters
	UnsignedInt m_lanNetworksTime = 0;
	UnsignedShort m_lobbyPort;
	UnsignedShort m_gamePort;
	std::unique_ptr<Transport> m_transport;		///< session socket (8086)
	std::unique_ptr<Transport> m_gameTransport;	///< game socket (8088), opened in a game, handed to the game at the start
	::Engine::SealKeyPair m_keyPair;		///< fresh for every game created or joined
	std::vector< ::Engine::NetAddress > m_discoveryTargets;	///< tests: instead of broadcasts
	Bool m_forceRelay;
	Bool m_offline;
	::Engine::WireEndpoint m_publicLobby;	///< how a rendezvous server sees our sockets
	::Engine::WireEndpoint m_publicGame;
	UnsignedInt m_observeTag;

	// the lobby
	Mode m_mode = Mode::Lan;
	Lobby::Directory m_directory;
	Lobby::RendezvousServers m_servers;
	Lobby::PublicGames m_publicGames;		///< Internet: what the servers list
	UnsignedInt m_publicRevision = 0;		///< of m_publicGames, as shown in the directory
	LANGameInfo *m_currentGame;				///< the game we host or joined (in the directory); nullptr in the lobby
	Bool m_inLobby;
	Bool m_isInLANMenu;
	PendingActionType m_pendingAction;
	UnsignedInt m_lastResendTime;
	UnsignedInt m_lastUpdate;
	Bool m_isActive;
	UnsignedInt m_gameStartTime;
	Int m_gameStartSeconds;
	AsciiString m_expectedHostCode;			///< direct connect: game key typed with the address
	Bool m_showGameCode;					///< tell the player the game key once the game menu is up
	UnicodeString m_joinDenyDetail;			///< versions named by the last version denial
	// Answers that go to everybody (broadcast) or reach the player's screen are limited overall, so
	// forged requests cannot make us flood the network or the chat.
	::Engine::RateLimiter m_replyLimiter;
	::Engine::RateLimiter m_chatLimiter;
	unsigned m_portMapperChanges;			///< the router state last reported in the chat
	Int m_portMapperReported;				///< PortMapper::State last reported
	Int m_pinholesReported;					///< PortMapper::PinholeState last reported
	Bool m_routerLogNamed;					///< the chat named the router log once
	UnsignedInt m_messagesBuilt;			///< session messages encoded for sending

	// the game, both sides
	UnsignedInt64 m_sessionId;
	Lobby::GameLinks m_links;

	// host
	HostState m_host;
	Bool m_listPublicly = FALSE;
	Lobby::HostRoom m_room;
	AsciiString m_inviteCode;

	// spectators: what we stream as host, what we receive as spectator
	WatchHost m_watchHost;
	WatchFeed m_watch;

	// a player coming back into a game, and what a member keeps to come back with
	ReturnState m_return;
	StreamFeed m_returnFeed{ *this };
	UnsignedByte m_returnToken[::Engine::SESSION_TOKEN_SIZE] = {};	///< from the host's WELCOME
	Bool m_hasReturnToken = FALSE;
	AsciiString m_joinedInvite;				///< the invitation code we joined with
	AsciiString m_returnTicketPath;
	Bool m_returnStartGame = TRUE;

	// joiner and member
	JoinAttempt m_join;
	UnsignedInt m_hostHeard;
	UnsignedInt m_nextPing;
	UnsignedInt m_lastStateRevision;

protected:
	// GameLobby.cpp: lifecycle and helpers
	void clearSession( void );
	Bool openLobbyTransport( void );
	Bool openGameTransport( void );
	void closeGameTransport( void );
	void linkGame( void );					///< the game links work with our identity, keys and game socket
	void newKeyPair( void );
	void addSelfToLobby( void );
	void leaveGame( Bool hostLeft );
	Int slotOf( UnsignedInt identity ) const;
	void ownEndpoints( ::Engine::SessionEndpoints &out ) const;
	Bool isOwnEndpoint( const ::Engine::NetAddress &from ) const;
	Bool isRendezvousServer( const ::Engine::NetAddress &from ) const;
	void systemChat( const UnicodeString &text );
	void showGameKey( void );
	void startPortMapping( void );
	void reportPortMapping( void );
	void reportPinholes( void );
	void writeRouterLog( void );
	/// The addresses players on the internet join with; the first goes to the clipboard.
	void showInternetAddress( Bool withIPv6 );
	void applyMemberKeys( void ) { if (m_currentGame) m_links.applyKeys(*m_currentGame); }

	// GameLobby.cpp: sending
	Bool encode( const ::Engine::SessionMessage &message, Lobby::Encoded &out );
	void sendPlain( const ::Engine::SessionMessage &message, const ::Engine::NetAddress &to );
	void sendToPeer( const ::Engine::SessionMessage &message, UnsignedInt identity );
	void sendToMembers( const ::Engine::SessionMessage &message, UnsignedInt except = 0 );
	void sendToHost( const ::Engine::SessionMessage &message ) { sendToPeer(message, m_links.member(0).identity); }
	void broadcast( const ::Engine::SessionMessage &message );
	void syncPublicGames( UnsignedInt now );
	void refreshLanNetworks( void );
	/// The address that names us on the LAN: the bound one, else the first of the adapters.
	UnsignedInt lanAddress( void ) const;
	void chatCommand( const UnicodeString &line );	///< Internet: the listed games into the directory
	void sendPresence( void );

	// GameLobbyHost.cpp
	void hostUpdate( UnsignedInt now );
	void sendAnnounce( void );
	void sendState( UnsignedInt to = 0 );
	void sendMembers( UnsignedInt to = 0 );
	void removeMember( Int slot, Bool tellThem );
	void syncMembers( void );
	void openRelay( void );
	void showInvite( void );
	void refuse( const ::Engine::NetAddress &to, UnsignedInt64 sessionId, ReturnType reason, std::size_t sizeLimit );
	void handleHello( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d );
	void handleJoin( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d );
	void hostReceives( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d, Int slot );
	void roomAnswer( const ::Engine::TreffMessage &m, const Transport::RawDatagram &d, Bool gameSocket );

	// GameLobbyJoin.cpp
	void startJoin( void );
	void joinUpdate( UnsignedInt now );
	void lookupInvite( UnsignedInt now );
	void sendHello( UnsignedInt now );
	void finishJoin( ReturnType result );
	void handleChallenge( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d );
	void handleRefuse( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d );
	void handleWelcome( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d );
	void joinAnswer( const ::Engine::TreffMessage &m, const Transport::RawDatagram &d, Bool gameSocket );
	void memberUpdate( UnsignedInt now );
	void memberReceives( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d );
	void handleMember( const ::Engine::SessionMessage &m );
	void handleRelay( const ::Engine::SessionMessage &m );

	// GameLobbyWatch.cpp
	Lobby::HostRoom::Refresh refreshRoom( UnsignedInt now );
	void receiveInGame( void );
	void acceptSpectator( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d, const UnicodeString &name, UnsignedInt identity );
	Int findSpectator( UnsignedInt identity ) const;
	void removeSpectator( Int index, Bool tellThem );
	void hostWatchUpdate( UnsignedInt now );
	void readReplayLog( void );
	void sendWatchChunk( Spectator &s, UnsignedInt offset, UnsignedInt length, const ::Engine::WatchMark &mark, UnsignedInt now );
	void spectatorSends( const ::Engine::SessionMessage &m, Int index );
	void startWatching( const ::Engine::SessionMessage &m );
	void watchReceives( const ::Engine::SessionMessage &m );
	void watchUpdate( UnsignedInt now );
	void stopWatching( Bool tellHost );
	void endWatchHost( void );
	void tellPlayers( const UnicodeString &text );
	Bool watchedMapPresent( void );

	// GameLobbyReturn.cpp
	void newReturnToken( Int slot );
	void keepReturnTicket( void );			///< at the start: what we come back with after a drop
	void clearReturnTicket( void );
	AsciiString returnTicketPath( void ) const;
	void acceptReturner( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d, const ::Engine::SessionJoinInfo &info,
		UnsignedInt identity );
	void hostReturnUpdate( Spectator &s, UnsignedInt now );
	void sendReturnWelcome( const Spectator &s );
	void startReturn( const ::Engine::SessionMessage &m );
	void returnReceives( const ::Engine::SessionMessage &m );
	void returnUpdate( UnsignedInt now );
	Bool startReturningGame( void );
	void stopReturn( Bool tellHost, const char *reason );

	// GameLobbyReceive.cpp
	void receive( Transport *transport, Bool gameSocket );
	void handleDatagram( const Transport::RawDatagram &d, Bool gameSocket );
	void handlePlain( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d, Bool gameSocket );
	void handleSealed( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d, Bool gameSocket );
	void handleTreff( const ::Engine::TreffMessage &m, const Transport::RawDatagram &d, Bool gameSocket );
	void handleQuery( const Transport::RawDatagram &d );
	void handleAnnounce( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d );
	void handlePresence( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d );
	void handleLobbyChat( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d );
	void handleBye( const Transport::RawDatagram &d );
	void handleProbe( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d );
	static ChatType chatTypeOf( std::uint8_t kind );
};

#endif // GAMENETWORK_GAMELOBBY_H
