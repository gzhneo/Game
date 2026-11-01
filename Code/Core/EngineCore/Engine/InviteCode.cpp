/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// InviteCode.cpp -- see InviteCode.h.

#include "Engine/InviteCode.h"

#include <format>

namespace Engine
{

namespace
{
	const char CROCKFORD[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

	char canonical(char c)
	{
		if (c >= 'a' && c <= 'z')
			c = static_cast<char>(c - 'a' + 'A');
		if (c == 'O')
			return '0';
		if (c == 'I' || c == 'L')
			return '1';
		for (const char *k = CROCKFORD; *k != 0; ++k)
		{
			if (*k == c)
				return c;
		}
		return 0;
	}

	std::string trim(const std::string &text)
	{
		std::size_t begin = 0;
		std::size_t end = text.size();
		while (begin < end && (text[begin] == ' ' || text[begin] == '\t'))
			++begin;
		while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t'))
			--end;
		return text.substr(begin, end - begin);
	}
}

std::string canonicalCrockford(const std::string &text, std::size_t length)
{
	if (text.size() != length)
		return std::string();
	std::string out;
	for (char c : text)
	{
		const char k = canonical(c);
		if (k == 0)
			return std::string();
		out += k;
	}
	return out;
}

bool parseInviteCode(const std::string &text, InviteCode &out)
{
	const std::string t = trim(text);
	if (t.size() > 300)
		return false;
	const std::size_t at = t.find('@');
	std::string code = t.substr(0, at);
	InviteCode result;
	if (at != std::string::npos)
	{
		result.server = parseNetTarget(t.substr(at + 1));
		if (result.server.kind == NetTarget::Kind::Invalid)
			return false;
	}
	const std::size_t dash = code.find('-');
	std::string room;
	std::string fingerprint;
	if (dash != std::string::npos)
	{
		room = code.substr(0, dash);
		fingerprint = code.substr(dash + 1);
		if (fingerprint.find('-') != std::string::npos)
		{
			// the fingerprint may be written with its own dash as the game key shows it
			const std::size_t second = fingerprint.find('-');
			if (second != 4 || fingerprint.find('-', second + 1) != std::string::npos)
				return false;
			fingerprint.erase(second, 1);
		}
	}
	else
	{
		if (code.size() != 12)
			return false;
		room = code.substr(0, 4);
		fingerprint = code.substr(4);
	}
	result.room = canonicalCrockford(room, 4);
	result.fingerprint = canonicalCrockford(fingerprint, 8);
	if (result.room.empty() || result.fingerprint.empty())
		return false;
	out = result;
	return true;
}

std::string formatInviteCode(const std::string &room, const std::string &fingerprint, const std::string &server,
	std::uint16_t port)
{
	std::string text = room + "-" + fingerprint;
	if (!server.empty())
	{
		text += "@";
		const bool ipv6 = server.find(':') != std::string::npos;
		if (ipv6 && port != RENDEZVOUS_DEFAULT_PORT && port != 0)
			text += "[" + server + "]";
		else
			text += server;
		if (port != RENDEZVOUS_DEFAULT_PORT && port != 0)
			text += std::format(":{}", port);
	}
	return text;
}

std::string inviteRoomFromBits(std::uint32_t bits)
{
	std::string room;
	for (int i = 0; i < 4; ++i)
		room += CROCKFORD[(bits >> (15 - 5 * i)) & 31];
	return room;
}

} // namespace Engine
