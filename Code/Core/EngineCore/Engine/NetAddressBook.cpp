/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// NetAddressBook.cpp -- see NetAddressBook.h.

#include "Engine/NetAddressBook.h"

#include <cstring>

namespace Engine
{

namespace
{
	// Anyone on the network can send packets; the tables stay small.
	constexpr std::size_t MAX_REGISTERED = 256;
	constexpr std::size_t MAX_OBSERVED = 256;
}

bool NetAddressBook::sameHost(const NetAddress &a, const NetAddress &b)
{
	if (a.family != b.family)
		return false;
	const std::size_t n = a.family == NetAddress::Family::V4 ? 4 : a.family == NetAddress::Family::V6 ? 16 : 0;
	return n != 0 && std::memcmp(a.bytes, b.bytes, n) == 0;
}

std::uint32_t NetAddressBook::handleOf(const NetAddress &address)
{
	if (address.isV4())
		return address.ipv4();
	if (!address.isV6())
		return 0;
	std::uint32_t h = 2166136261u;	// FNV-1a
	for (int i = 0; i < 16; ++i)
	{
		h ^= address.bytes[i];
		h *= 16777619u;
	}
	return HANDLE_FIRST + h % (HANDLE_LAST - HANDLE_FIRST + 1u);
}

std::uint32_t NetAddressBook::registerAddress(const NetAddress &address)
{
	if (!address.isV6())
		return handleOf(address);
	const std::uint32_t handle = handleOf(address);
	for (const Registered &r : m_registered)
	{
		if (r.handle == handle)
			return sameHost(r.address, address) ? handle : 0;
	}
	if (m_registered.size() >= MAX_REGISTERED)
		m_registered.erase(m_registered.begin());
	Registered r;
	r.handle = handle;
	r.address = address;
	r.address.port = 0;
	m_registered.push_back(r);
	return handle;
}

std::uint32_t NetAddressBook::identityOf(const NetAddress &sender)
{
	return registerAddress(sender);
}

std::uint32_t NetAddressBook::identityOf(const NetAddress &sender, std::uint16_t localPort)
{
	for (const Observed &o : m_observed)
	{
		if (o.localPort == localPort && o.from == sender)
			return o.identity;
	}
	return registerAddress(sender);
}

void NetAddressBook::observe(std::uint32_t identity, std::uint16_t localPort, const NetAddress &from)
{
	for (Observed &o : m_observed)
	{
		if (o.identity == identity && o.localPort == localPort)
		{
			o.from = from;
			return;
		}
	}
	if (m_observed.size() >= MAX_OBSERVED)
		m_observed.erase(m_observed.begin());
	Observed o;
	o.identity = identity;
	o.localPort = localPort;
	o.from = from;
	m_observed.push_back(o);
}

void NetAddressBook::moveLocalPort(std::uint16_t from, std::uint16_t to)
{
	if (from == to)
		return;
	std::vector<Observed> moved;
	for (const Observed &o : m_observed)
	{
		if (o.localPort == from)
			moved.push_back(o);
	}
	for (Observed &o : moved)
		observe(o.identity, to, o.from);
}

bool NetAddressBook::addressOf(std::uint32_t identity, NetAddress &out) const
{
	if (isHandle(identity))
	{
		for (const Registered &r : m_registered)
		{
			if (r.handle == identity)
			{
				out = r.address;
				return true;
			}
		}
		return false;
	}
	out = NetAddress::fromIPv4(identity);
	return true;
}

bool NetAddressBook::route(std::uint32_t identity, std::uint16_t port, std::uint16_t localPort, NetAddress &out) const
{
	for (const Observed &o : m_observed)
	{
		if (o.identity == identity && o.localPort == localPort)
		{
			out = o.from;
			return true;
		}
	}
	if (!addressOf(identity, out))
		return false;
	out.port = port;
	return true;
}

void NetAddressBook::clear()
{
	// swap, so the memory is returned as well (the book is a function-local static that outlives the leak check)
	std::vector<Registered>().swap(m_registered);
	std::vector<Observed>().swap(m_observed);
}

} // namespace Engine
