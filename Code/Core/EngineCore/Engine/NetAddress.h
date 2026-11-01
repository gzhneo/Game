/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// NetAddress.h -- network addresses as typed by players: IPv4 and IPv6 literals with an optional
// port, and host names.
//
//     1.2.3.4   1.2.3.4:8088   ::1   [::1]   [2001:db8::7]:8088   ::ffff:1.2.3.4   gzh.example.org
//
// Parsing is strict (no octal or shortened IPv4 forms, every IPv4 part 0..255, at most one "::",
// no zone index) and never reads outside the given text. IPv4-mapped IPv6 addresses count as IPv4.
// Formatting follows RFC 5952 (lower case, the longest run of zero groups shortened).
//
// No engine header, no sockets: tested by EngineTests. Name resolution lives in the network code.

#pragma once

#ifndef ENGINE_NETADDRESS_H
#define ENGINE_NETADDRESS_H

#include <cstdint>
#include <string>
#include <string_view>

namespace Engine
{

struct NetAddress
{
	enum class Family : std::uint8_t { None, V4, V6 };

	Family family = Family::None;
	std::uint8_t bytes[16] = {};	///< network byte order; IPv4 uses the first four
	std::uint16_t port = 0;			///< 0: none given

	static NetAddress fromIPv4(std::uint32_t hostOrder, std::uint16_t port = 0);
	/// From the 16 bytes of an IPv6 socket address; IPv4-mapped addresses become IPv4.
	static NetAddress fromIPv6(const std::uint8_t bytes[16], std::uint16_t port = 0);

	bool isV4() const { return family == Family::V4; }
	bool isV6() const { return family == Family::V6; }
	/// IPv4 address in host byte order (as the game stores it); 0 for other families.
	std::uint32_t ipv4() const;
	bool isLoopback() const;
	bool isUnspecified() const;

	/// "1.2.3.4", "1.2.3.4:8088", "2001:db8::7", "[2001:db8::7]:8088"; empty for Family::None.
	std::string toString(bool withPort = true) const;

	bool operator==(const NetAddress &o) const;
	bool operator!=(const NetAddress &o) const { return !(*this == o); }
};

/// An IPv4 or IPv6 literal, optionally with a port ("1.2.3.4:5", "[::1]:5"). False for anything
/// else, including host names.
bool parseNetAddress(std::string_view text, NetAddress &out);

/// Host name after RFC 1123: labels of letters, digits and '-', not starting or ending with '-',
/// at most 63 characters each and 253 in total, and not all digits (that would be a broken IPv4).
bool isValidHostName(std::string_view text);

/// What a player typed in the address field.
struct NetTarget
{
	enum class Kind : std::uint8_t { Invalid, Address, HostName };

	Kind kind = Kind::Invalid;
	NetAddress address;		///< Kind::Address
	std::string hostName;	///< Kind::HostName, without the port
	std::uint16_t port = 0;	///< 0: none given
};

/// Classifies typed text: an address literal, a host name with an optional ":port", or invalid.
/// Surrounding spaces are ignored.
NetTarget parseNetTarget(std::string_view text);

} // namespace Engine

#endif // ENGINE_NETADDRESS_H
