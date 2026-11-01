/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// NetAddressBook.h -- player identities versus network addresses.
//
// The lobby and the game know every player by a 32-bit identity: the IPv4 address the host sees
// the player at, as written in the slot list. IPv6 addresses do not fit, and behind NAT the port a
// peer's packets come from differs from its slot port:
//
//   - IPv6 addresses get a 32-bit handle in 240.0.0.0 to 254.255.255.255 (never assigned to hosts;
//     255.255.255.255 is left out). The handle is a hash, so every machine computes the same handle
//     for the same address; the host tells the others the addresses behind the handles.
//   - Observed endpoints remember, per local port, the address and port a peer's packets arrive
//     from, so replies pass NAT port mappings.
//
// The transport translates at the socket: received packets carry identities, packets to an identity
// go to its actual endpoint. Pure logic without sockets: tested by EngineTests.

#pragma once

#ifndef ENGINE_NETADDRESSBOOK_H
#define ENGINE_NETADDRESSBOOK_H

#include "Engine/NetAddress.h"

#include <cstdint>
#include <vector>

namespace Engine
{

class NetAddressBook
{
public:
	static constexpr std::uint32_t HANDLE_FIRST = 0xF0000000u;
	static constexpr std::uint32_t HANDLE_LAST = 0xFEFFFFFFu;

	/// IPv4: the address itself (host order). IPv6: a handle in HANDLE_FIRST..HANDLE_LAST. 0 otherwise.
	static std::uint32_t handleOf(const NetAddress &address);
	static bool isHandle(std::uint32_t identity) { return identity >= HANDLE_FIRST && identity <= HANDLE_LAST; }

	/// Remembers an IPv6 address under its handle and returns the handle; IPv4 returns the address.
	/// Keeps the first address if two collide (then 0 is returned for the second).
	std::uint32_t registerAddress(const NetAddress &address);

	/// Identity of a packet's sender (handle or IPv4 address; 0 on a handle collision). Registers
	/// IPv6 senders.
	std::uint32_t identityOf(const NetAddress &sender);
	/// As above, but an endpoint remembered for an identity at our local port (observe) names that
	/// identity: players known by their key reach us from addresses that are not their identity.
	std::uint32_t identityOf(const NetAddress &sender, std::uint16_t localPort);

	/// Remembers where a peer's packets to our local port come from (address and port).
	void observe(std::uint32_t identity, std::uint16_t localPort, const NetAddress &from);
	/// Our socket moved to another local port: what was observed at the old one holds for the new one.
	void moveLocalPort(std::uint16_t from, std::uint16_t to);

	/// Where to send a packet for `identity` at `port` from our local port: the observed endpoint,
	/// else the registered or IPv4 address with `port`. False for an unknown IPv6 handle.
	bool route(std::uint32_t identity, std::uint16_t port, std::uint16_t localPort, NetAddress &out) const;

	/// Address known for an identity (registered IPv6, or the IPv4 identity itself).
	bool addressOf(std::uint32_t identity, NetAddress &out) const;

	void clear();

private:
	struct Registered { std::uint32_t handle; NetAddress address; };
	struct Observed { std::uint32_t identity; std::uint16_t localPort; NetAddress from; };

	static bool sameHost(const NetAddress &a, const NetAddress &b);

	std::vector<Registered> m_registered;
	std::vector<Observed> m_observed;
};

} // namespace Engine

#endif // ENGINE_NETADDRESSBOOK_H
