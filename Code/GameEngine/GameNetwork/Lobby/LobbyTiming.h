/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// LobbyTiming.h -- every interval, timeout and limit of the game lobby in one place.
// Times are milliseconds of timeGetTime(), a wrapping clock: compare with due() and silentFor().

#pragma once

#ifndef GAMENETWORK_LOBBY_LOBBYTIMING_H
#define GAMENETWORK_LOBBY_LOBBYTIMING_H

#include "Lib/BaseType.h"

#include <cstddef>

namespace Lobby
{

namespace Timing
{
	inline constexpr UnsignedInt UPDATE = 50;				///< the lobby runs at most this often
	inline constexpr UnsignedInt PRESENCE = 10000;			///< a player in the LAN lobby tells the others
	inline constexpr UnsignedInt ANNOUNCE = 5000;			///< a host tells the LAN about its game
	inline constexpr UnsignedInt STATE = 5000;				///< the host repeats options and members
	inline constexpr UnsignedInt PING = 2000;				///< a member tells the host it is there
	inline constexpr UnsignedInt SILENCE = 15000;			///< a member or the host counts as gone
	inline constexpr UnsignedInt GAME_LIST = 20000;			///< a listed game not announced for this long goes
	inline constexpr UnsignedInt PLAYER_LIST = 20000;
	inline constexpr UnsignedInt JOIN_LAN = 8000;			///< a join by list or address gives up
	inline constexpr UnsignedInt JOIN_INVITE = 15000;		///< a join by invitation gives up
	inline constexpr UnsignedInt HELLO_RESEND = 300;
	inline constexpr UnsignedInt JOIN_RESEND = 500;
	inline constexpr UnsignedInt LOOKUP_RESEND = 1000;
	inline constexpr UnsignedInt COOKIE_ROTATION = 60000;	///< a cookie stays valid for one to two rotations
	inline constexpr UnsignedInt ROOM_REFRESH = 15000;		///< servers drop rooms after 60 s without one
	inline constexpr UnsignedInt ROOM_RETRY = 1000;
	inline constexpr UnsignedInt RELAY_BIND_RETRY = 1000;
	inline constexpr UnsignedInt RELAY_BIND_REFRESH = 15000;	///< keeps the router's mapping towards the server
	inline constexpr UnsignedInt KNOCK_PROBES = 3000;		///< the host probes a joiner the server announced
	inline constexpr UnsignedInt KNOCK_PROBE_INTERVAL = 300;
	inline constexpr UnsignedInt LIST_ROUND = 5000; 		///< every server is asked for its public games once per round
	inline constexpr UnsignedInt LIST_ASK_GAP = 200;		///< at most one list request this often
	inline constexpr UnsignedInt LIST_ANSWER = 5000;		///< an unanswered list request is forgotten
	inline constexpr UnsignedInt LIST_KEEP = 30000;			///< a listed game no server named for this long goes
	inline constexpr UnsignedInt LAN_NETWORKS = 30000;		///< the adapters are looked at again after this
	inline constexpr UnsignedInt WATCH_MARK = 250;			///< the host tells spectators how far the game is
	inline constexpr UnsignedInt WATCH_ACK = 250;			///< a spectator confirms at least this often
	inline constexpr UnsignedInt RETURN_RESEND = 300;		///< the steps of a return are repeated until answered
	inline constexpr UnsignedInt RETURN_TICKET_AGE = 30 * 60;	///< seconds: an older Rejoin.ini is not offered
}

namespace Limit
{
	inline constexpr Int LOOKUP_ATTEMPTS = 3;		///< per rendezvous server, then the next one
	inline constexpr Int ROOM_ATTEMPTS = 3;
	inline constexpr size_t KNOCKS = 12;
	inline constexpr size_t EARLY_MESSAGES = 8;		///< sealed messages of the host kept until WELCOME
	inline constexpr size_t RESOLVED_SERVERS = 64;
	inline constexpr size_t PUBLIC_GAMES = 256;		///< listed games kept from all servers together
	inline constexpr size_t SPECTATORS = 8;			///< per hosted game
	inline constexpr Int WATCH_CHUNKS = 24;			///< replay chunks to one spectator per lobby update
}

/// `when` has come (wrapping clock).
inline Bool due( UnsignedInt now, UnsignedInt when )
{
	return static_cast<Int>(now - when) >= 0;
}

/// Nothing heard for longer than `limit`; a message received after `now` was taken is no silence.
inline Bool silentFor( UnsignedInt now, UnsignedInt heard, UnsignedInt limit )
{
	return static_cast<Int>(now - heard) > static_cast<Int>(limit);
}

} // namespace Lobby

#endif // GAMENETWORK_LOBBY_LOBBYTIMING_H
