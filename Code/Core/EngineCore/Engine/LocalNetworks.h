/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// LocalNetworks.h -- the IPv4 networks of this machine's adapters, as the LAN lobby uses them.
//
// The lobby listens on every adapter and searches each local network with a directed broadcast,
// so nobody has to pick an adapter. The first usable address names us where one address is needed
// (identity in the lobby, port forwarding at the router): the one of the adapter that leads to the
// internet, since virtual adapters (Hyper-V, WSL, VirtualBox, VPN) have private addresses as well.
//
// No engine header: tested by EngineTests.

#pragma once

#include "Engine/PortMapping.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Engine
{

struct LocalNetwork
{
	std::uint32_t address = 0;		///< host order
	std::uint8_t prefixLength = 0;
};

/// The directed broadcast address of the network; 0 for /31, /32 and nonsense.
inline std::uint32_t directedBroadcast(const LocalNetwork &network)
{
	if (network.prefixLength == 0 || network.prefixLength > 30)
		return 0;
	const std::uint32_t hostBits = 0xFFFFFFFFu >> network.prefixLength;
	return network.address | hostBits;
}

/// Without loopback, link-local (169.254/16) and unspecified addresses, one entry per address;
/// private addresses (the LAN) first, in the order given, and among them `towardsInternet` (the
/// address of the adapter with the default route, 0 if unknown) first of all.
inline std::vector<LocalNetwork> usableLanNetworks(std::vector<LocalNetwork> networks, std::uint32_t towardsInternet = 0)
{
	std::vector<LocalNetwork> out;
	for (const LocalNetwork &n : networks)
	{
		const bool loopback = (n.address >> 24) == 127;
		const bool linkLocal = (n.address >> 16) == 0xA9FE;
		const bool known = std::any_of(out.begin(), out.end(), [&n](const LocalNetwork &o) { return o.address == n.address; });
		if (n.address != 0 && !loopback && !linkLocal && !known)
			out.push_back(n);
	}
	std::stable_partition(out.begin(), out.end(), [](const LocalNetwork &n) { return PortMapping::isPrivateIPv4(n.address); });
	std::stable_partition(out.begin(), out.end(), [towardsInternet](const LocalNetwork &n) {
		return towardsInternet != 0 && n.address == towardsInternet && PortMapping::isPrivateIPv4(n.address);
	});
	return out;
}

} // namespace Engine
