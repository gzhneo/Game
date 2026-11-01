/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// PeerLink.h -- how the game socket reaches one other player: directly or through the relay.
//
// Probes go out quickly at first, then slowly as a keepalive. An answer carrying the probe's nonce
// proves the direct way in both directions. A player that stays silent past `relayAfter` is
// reached through the relay once one is bound; an answer later switches back to the direct way.
// All times are milliseconds of a wrapping clock.
//
// No engine header: tested by EngineTests.

#pragma once

#ifndef ENGINE_PEERLINK_H
#define ENGINE_PEERLINK_H

#include <cstdint>

namespace Engine
{

struct PeerLinkTimings
{
	std::uint32_t probeFast = 500;
	std::uint32_t fastPhase = 10000;	///< fast probes while nothing answered
	std::uint32_t probeSlow = 5000;
	std::uint32_t relayAfter = 6000;
};

class PeerLink
{
public:
	enum class Change : std::uint8_t { None, UseRelay, UseDirect };

	PeerLink() = default;
	PeerLink(std::uint32_t identity, std::uint32_t nonce, std::uint32_t nowMs, const PeerLinkTimings &timings = PeerLinkTimings());

	std::uint32_t identity() const { return m_identity; }
	std::uint32_t nonce() const { return m_nonce; }
	bool direct() const { return m_direct; }
	bool relayed() const { return m_relayed; }

	/// True when a probe is due; schedules the next one.
	bool takeProbe(std::uint32_t nowMs);
	/// An answer to our probe. UseDirect if it ends relaying (unless relaying is forced).
	Change answered(std::uint32_t value, bool forceRelay);
	/// UseRelay once the player stayed silent too long (or relaying is forced) and a relay is bound.
	Change update(std::uint32_t nowMs, bool relayBound, bool forceRelay);

private:
	static bool reached(std::uint32_t nowMs, std::uint32_t when) { return static_cast<std::int32_t>(nowMs - when) >= 0; }

	PeerLinkTimings m_timings;
	std::uint32_t m_identity = 0;
	std::uint32_t m_nonce = 0;
	std::uint32_t m_since = 0;
	std::uint32_t m_nextProbe = 0;
	bool m_direct = false;
	bool m_relayed = false;
};

} // namespace Engine

#endif // ENGINE_PEERLINK_H
