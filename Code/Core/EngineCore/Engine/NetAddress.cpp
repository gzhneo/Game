/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// NetAddress.cpp -- see NetAddress.h.

#include "Engine/NetAddress.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <iterator>

namespace Engine
{

namespace
{
	bool isDigit(char c) { return c >= '0' && c <= '9'; }

	int hexValue(char c)
	{
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		if (c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		return -1;
	}

	std::string_view trim(std::string_view s)
	{
		while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
			s.remove_prefix(1);
		while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
			s.remove_suffix(1);
		return s;
	}

	// Decimal 1..65535, at most five digits.
	bool parsePort(std::string_view s, std::uint16_t &port)
	{
		if (s.empty() || s.size() > 5)
			return false;
		unsigned value = 0;
		for (char c : s)
		{
			if (!isDigit(c))
				return false;
			value = value * 10 + static_cast<unsigned>(c - '0');
		}
		if (value == 0 || value > 65535)
			return false;
		port = static_cast<std::uint16_t>(value);
		return true;
	}

	// Four decimal parts 0..255 of one to three digits; leading zeros are read as decimal.
	bool parseIPv4(std::string_view s, std::uint8_t out[4])
	{
		int part = 0;
		std::size_t i = 0;
		while (part < 4)
		{
			std::size_t digits = 0;
			unsigned value = 0;
			while (i < s.size() && isDigit(s[i]) && digits < 4)
			{
				value = value * 10 + static_cast<unsigned>(s[i] - '0');
				++digits;
				++i;
			}
			if (digits == 0 || digits > 3 || value > 255)
				return false;
			out[part++] = static_cast<std::uint8_t>(value);
			if (part < 4)
			{
				if (i >= s.size() || s[i] != '.')
					return false;
				++i;
			}
		}
		return i == s.size();
	}

	// Groups of one to four hex digits separated by ':'; one "::" stands for one or more zero groups;
	// an IPv4 address may take the place of the last two groups.
	bool parseIPv6(std::string_view s, std::uint8_t out[16])
	{
		std::uint16_t head[8];
		std::uint16_t tail[8];
		int headCount = 0;
		int tailCount = 0;
		bool compressed = false;
		std::size_t i = 0;

		if (s.size() >= 2 && s[0] == ':' && s[1] == ':')
		{
			compressed = true;
			i = 2;
		}
		else if (!s.empty() && s[0] == ':')
		{
			return false;
		}

		while (i < s.size())
		{
			// an embedded IPv4 address ends the text
			std::size_t end = i;
			while (end < s.size() && s[end] != ':')
				++end;
			const std::string_view token = s.substr(i, end - i);
			std::uint16_t *groups = compressed ? tail : head;
			int &count = compressed ? tailCount : headCount;

			if (token.find('.') != std::string_view::npos)
			{
				std::uint8_t v4[4];
				if (end != s.size() || count > 6 || !parseIPv4(token, v4))
					return false;
				groups[count++] = static_cast<std::uint16_t>((v4[0] << 8) | v4[1]);
				groups[count++] = static_cast<std::uint16_t>((v4[2] << 8) | v4[3]);
				i = end;
				break;
			}

			if (token.empty() || token.size() > 4 || count >= 8)
				return false;
			unsigned value = 0;
			for (char c : token)
			{
				const int h = hexValue(c);
				if (h < 0)
					return false;
				value = (value << 4) | static_cast<unsigned>(h);
			}
			groups[count++] = static_cast<std::uint16_t>(value);

			i = end;
			if (i == s.size())
				break;
			// at a ':'
			if (i + 1 < s.size() && s[i + 1] == ':')
			{
				if (compressed)
					return false;	// a second "::"
				compressed = true;
				i += 2;
				continue;
			}
			++i;
			if (i == s.size())
				return false;	// ends with a single ':'
		}

		const int total = headCount + tailCount;
		if (compressed ? total > 7 : total != 8)
			return false;

		std::uint16_t groups[8] = {};
		for (int g = 0; g < headCount; ++g)
			groups[g] = head[g];
		for (int g = 0; g < tailCount; ++g)
			groups[8 - tailCount + g] = tail[g];
		for (int g = 0; g < 8; ++g)
		{
			out[2 * g] = static_cast<std::uint8_t>(groups[g] >> 8);
			out[2 * g + 1] = static_cast<std::uint8_t>(groups[g] & 0xFF);
		}
		return true;
	}

	bool isV4Mapped(const std::uint8_t b[16])
	{
		for (int i = 0; i < 10; ++i)
			if (b[i] != 0)
				return false;
		return b[10] == 0xFF && b[11] == 0xFF;
	}

	NetAddress makeV6(const std::uint8_t b[16], std::uint16_t port)
	{
		NetAddress a;
		a.port = port;
		if (isV4Mapped(b))
		{
			a.family = NetAddress::Family::V4;
			std::memcpy(a.bytes, b + 12, 4);
		}
		else
		{
			a.family = NetAddress::Family::V6;
			std::memcpy(a.bytes, b, 16);
		}
		return a;
	}
}

NetAddress NetAddress::fromIPv4(std::uint32_t hostOrder, std::uint16_t port)
{
	NetAddress a;
	a.family = Family::V4;
	a.bytes[0] = static_cast<std::uint8_t>(hostOrder >> 24);
	a.bytes[1] = static_cast<std::uint8_t>(hostOrder >> 16);
	a.bytes[2] = static_cast<std::uint8_t>(hostOrder >> 8);
	a.bytes[3] = static_cast<std::uint8_t>(hostOrder);
	a.port = port;
	return a;
}

NetAddress NetAddress::fromIPv6(const std::uint8_t b[16], std::uint16_t port)
{
	return makeV6(b, port);
}

std::uint32_t NetAddress::ipv4() const
{
	if (family != Family::V4)
		return 0;
	return (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16)
		| (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
}

bool NetAddress::isLoopback() const
{
	if (family == Family::V4)
		return bytes[0] == 127;
	if (family == Family::V6)
	{
		for (int i = 0; i < 15; ++i)
			if (bytes[i] != 0)
				return false;
		return bytes[15] == 1;
	}
	return false;
}

bool NetAddress::isUnspecified() const
{
	const int n = family == Family::V4 ? 4 : family == Family::V6 ? 16 : 0;
	if (n == 0)
		return false;
	for (int i = 0; i < n; ++i)
		if (bytes[i] != 0)
			return false;
	return true;
}

std::string NetAddress::toString(bool withPort) const
{
	if (family == Family::V4)
	{
		std::string s = std::format("{}.{}.{}.{}", bytes[0], bytes[1], bytes[2], bytes[3]);
		if (withPort && port != 0)
			std::format_to(std::back_inserter(s), ":{}", port);
		return s;
	}
	if (family != Family::V6)
		return {};

	std::uint16_t groups[8];
	for (int g = 0; g < 8; ++g)
		groups[g] = static_cast<std::uint16_t>((bytes[2 * g] << 8) | bytes[2 * g + 1]);
	// longest run of at least two zero groups, the first one on a tie
	int bestStart = -1;
	int bestLength = 1;
	for (int g = 0; g < 8;)
	{
		if (groups[g] != 0)
		{
			++g;
			continue;
		}
		int end = g;
		while (end < 8 && groups[end] == 0)
			++end;
		if (end - g > bestLength)
		{
			bestStart = g;
			bestLength = end - g;
		}
		g = end;
	}
	std::string body;
	for (int g = 0; g < 8; ++g)
	{
		if (g == bestStart)
		{
			body += "::";
			g += bestLength - 1;
			continue;
		}
		if (!body.empty() && body.back() != ':')
			body += ':';
		std::format_to(std::back_inserter(body), "{:x}", groups[g]);
	}
	if (withPort && port != 0)
		return std::format("[{}]:{}", body, port);
	return body;
}

bool NetAddress::operator==(const NetAddress &o) const
{
	if (family != o.family || port != o.port)
		return false;
	const int n = family == Family::V4 ? 4 : family == Family::V6 ? 16 : 0;
	return std::memcmp(bytes, o.bytes, static_cast<std::size_t>(n)) == 0;
}

bool parseNetAddress(std::string_view text, NetAddress &out)
{
	out = NetAddress();
	const std::string_view s = trim(text);
	if (s.empty() || s.size() > 64)
		return false;

	std::uint16_t port = 0;
	if (s[0] == '[')
	{
		const std::size_t close = s.find(']');
		if (close == std::string_view::npos)
			return false;
		const std::string_view rest = s.substr(close + 1);
		if (!rest.empty() && (rest[0] != ':' || !parsePort(rest.substr(1), port)))
			return false;
		std::uint8_t b[16];
		if (!parseIPv6(s.substr(1, close - 1), b))
			return false;
		out = makeV6(b, port);
		return true;
	}

	const std::size_t colons = static_cast<std::size_t>(std::count(s.begin(), s.end(), ':'));
	if (colons <= 1)
	{
		std::string_view host = s;
		if (colons == 1)
		{
			const std::size_t c = s.find(':');
			if (!parsePort(s.substr(c + 1), port))
				return false;
			host = s.substr(0, c);
		}
		std::uint8_t b[4];
		if (!parseIPv4(host, b))
			return false;
		out.family = NetAddress::Family::V4;
		std::memcpy(out.bytes, b, 4);
		out.port = port;
		return true;
	}

	std::uint8_t b[16];
	if (!parseIPv6(s, b))
		return false;
	out = makeV6(b, 0);
	return true;
}

bool isValidHostName(std::string_view s)
{
	if (!s.empty() && s.back() == '.')
		s.remove_suffix(1);	// fully qualified form
	if (s.empty() || s.size() > 253)
		return false;
	bool onlyDigitsAndDots = true;
	std::size_t labelLength = 0;
	char previous = '.';
	for (char c : s)
	{
		if (c == '.')
		{
			if (labelLength == 0 || previous == '-')
				return false;
			labelLength = 0;
		}
		else
		{
			const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
			if (!letter && !isDigit(c) && c != '-')
				return false;
			if (c == '-' && labelLength == 0)
				return false;
			if (++labelLength > 63)
				return false;
			if (!isDigit(c))
				onlyDigitsAndDots = false;
		}
		previous = c;
	}
	return previous != '-' && !onlyDigitsAndDots;
}

NetTarget parseNetTarget(std::string_view text)
{
	NetTarget t;
	const std::string_view s = trim(text);
	if (parseNetAddress(s, t.address))
	{
		t.kind = NetTarget::Kind::Address;
		t.port = t.address.port;
		return t;
	}
	t.address = NetAddress();
	if (s.find('[') != std::string_view::npos)
		return t;
	std::string_view host = s;
	const std::size_t colon = s.find(':');
	if (colon != std::string_view::npos)
	{
		if (s.find(':', colon + 1) != std::string_view::npos || !parsePort(s.substr(colon + 1), t.port))
			return t;
		host = s.substr(0, colon);
	}
	if (!isValidHostName(host))
	{
		t.port = 0;
		return t;
	}
	t.kind = NetTarget::Kind::HostName;
	t.hostName.assign(host.data(), host.size());
	return t;
}

} // namespace Engine
