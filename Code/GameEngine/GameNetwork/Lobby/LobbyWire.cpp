/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/Lobby/LobbyWire.h"

#include "Engine/InviteCode.h"

#include "GameNetwork/GameLobby.h"
#include "GameNetwork/Transport.h"

#include <cstring>

namespace Lobby
{

std::string toUtf8( const UnicodeString &text )
{
	std::string out;
	const WideChar *s = text.str();
	for (Int i = 0; s != nullptr && s[i] != 0; ++i)
	{
		UnsignedInt c = static_cast<UnsignedInt>(s[i]);
		if (sizeof(WideChar) == 2)
		{
			c &= 0xFFFF;
			if (c >= 0xD800 && c <= 0xDBFF)
			{
				const UnsignedInt low = static_cast<UnsignedInt>(s[i + 1]) & 0xFFFF;
				if (low >= 0xDC00 && low <= 0xDFFF)
				{
					c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
					++i;
				}
				else
				{
					c = '?';
				}
			}
			else if (c >= 0xDC00 && c <= 0xDFFF)
			{
				c = '?';
			}
		}
		if (c > 0x10FFFF)
			c = '?';
		if (c < 0x80)
		{
			out += static_cast<char>(c);
		}
		else if (c < 0x800)
		{
			out += static_cast<char>(0xC0 | (c >> 6));
			out += static_cast<char>(0x80 | (c & 0x3F));
		}
		else if (c < 0x10000)
		{
			out += static_cast<char>(0xE0 | (c >> 12));
			out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (c & 0x3F));
		}
		else
		{
			out += static_cast<char>(0xF0 | (c >> 18));
			out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (c & 0x3F));
		}
	}
	return out;
}

UnicodeString fromUtf8( const std::string &text )
{
	UnicodeString out;
	const size_t n = text.size();
	size_t i = 0;
	while (i < n)
	{
		const UnsignedInt b = static_cast<UnsignedByte>(text[i]);
		UnsignedInt c = '?';
		size_t length = 1;
		UnsignedInt minimum = 0;
		if (b < 0x80)
		{
			c = b;
		}
		else if ((b & 0xE0) == 0xC0)
		{
			length = 2;
			c = b & 0x1F;
			minimum = 0x80;
		}
		else if ((b & 0xF0) == 0xE0)
		{
			length = 3;
			c = b & 0x0F;
			minimum = 0x800;
		}
		else if ((b & 0xF8) == 0xF0)
		{
			length = 4;
			c = b & 0x07;
			minimum = 0x10000;
		}
		else
		{
			length = 0;
		}
		Bool valid = length != 0 && i + length <= n;
		for (size_t k = 1; valid && k < length; ++k)
		{
			const UnsignedInt cont = static_cast<UnsignedByte>(text[i + k]);
			if ((cont & 0xC0) != 0x80)
				valid = FALSE;
			else
				c = (c << 6) | (cont & 0x3F);
		}
		if (valid && length > 1 && (c < minimum || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)))
			valid = FALSE;
		if (!valid)
		{
			c = '?';
			length = 1;
		}
		i += length;
		if (c < 0x20 || c == 0x7F)
			continue;
		if (c >= 0x10000 && sizeof(WideChar) == 2)
		{
			c -= 0x10000;
			out.concat(static_cast<WideChar>(0xD800 + (c >> 10)));
			out.concat(static_cast<WideChar>(0xDC00 + (c & 0x3FF)));
		}
		else
		{
			out.concat(static_cast<WideChar>(c));
		}
	}
	return out;
}

void fingerprintBytes( const UnsignedByte key[::Engine::SEAL_KEY_SIZE], UnsignedByte out[5] )
{
	static const char ALPHABET[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
	char code[10];
	::Engine::sealFingerprint(key, code);
	UnsignedInt64 bits = 0;
	Int count = 0;
	for (Int i = 0; code[i] != 0 && count < 8; ++i)
	{
		const char *p = strchr(ALPHABET, code[i]);
		if (p == nullptr)
			continue;
		bits = (bits << 5) | static_cast<UnsignedInt64>(p - ALPHABET);
		++count;
	}
	for (Int b = 0; b < 5; ++b)
		out[b] = static_cast<UnsignedByte>(bits >> (8 * (4 - b)));
}

UnicodeString playerName( const std::string &utf8 )
{
	UnicodeString name = fromUtf8(utf8);
	name.trim();
	while (name.getLength() > g_lanPlayerNameLength)
		name.removeLastChar();
	return name;
}

std::string fingerprintCode( const UnsignedByte key[::Engine::SEAL_KEY_SIZE] )
{
	char code[10];
	::Engine::sealFingerprint(key, code);
	std::string compact;
	for (const char *p = code; *p != 0; ++p)
		if (*p != '-')
			compact += *p;
	return compact;
}

std::string inviteCodeAt( const std::string &room, const std::string &fingerprint, const std::string &serverText )
{
	const ::Engine::NetTarget server = ::Engine::parseNetTarget(serverText);
	std::string host;
	if (server.kind == ::Engine::NetTarget::Kind::HostName)
		host = server.hostName;
	else if (server.kind == ::Engine::NetTarget::Kind::Address)
		host = server.address.isV6() ? "[" + server.address.toString(false) + "]" : server.address.toString(false);
	const std::uint16_t port = server.port != 0 ? server.port : ::Engine::RENDEZVOUS_DEFAULT_PORT;
	return ::Engine::formatInviteCode(room, fingerprint, host, port);
}

::Engine::WireEndpoint wireEndpoint( const ::Engine::NetAddress &a )
{
	::Engine::WireEndpoint w;
	if (a.isV4())
	{
		w.family = 4;
		memcpy(w.address, a.bytes, 4);
	}
	else if (a.isV6())
	{
		w.family = 6;
		memcpy(w.address, a.bytes, 16);
	}
	w.port = a.port;
	return w;
}

::Engine::NetAddress netAddress( const ::Engine::WireEndpoint &w )
{
	if (w.family == 4)
		return ::Engine::NetAddress::fromIPv4(w.ipv4Address(), w.port);
	if (w.family == 6)
		return ::Engine::NetAddress::fromIPv6(w.address, w.port);
	return ::Engine::NetAddress();
}

UnicodeString sessionGameName( UnsignedInt64 sessionId )
{
	UnicodeString s;
	s.format(L"%8.8X%8.8X", static_cast<UnsignedInt>(sessionId >> 32), static_cast<UnsignedInt>(sessionId));
	return s;
}

Bool addEndpoint( ::Engine::WireEndpoint (&list)[::Engine::SESSION_MAX_ENDPOINTS], const ::Engine::WireEndpoint &e )
{
	if (!e.isSet() || e.port == 0)
		return FALSE;
	for (const ::Engine::WireEndpoint &x : list)
		if (x == e)
			return FALSE;
	for (::Engine::WireEndpoint &x : list)
	{
		if (!x.isSet())
		{
			x = e;
			return TRUE;
		}
	}
	list[::Engine::SESSION_MAX_ENDPOINTS - 1] = e;
	return TRUE;
}

UnsignedInt randomTag( void )
{
	UnsignedInt value = 0;
	while (value == 0)
	{
		if (!::Engine::secureRandom(&value, sizeof(value)))
			value = timeGetTime() * 2654435761u + 1;
	}
	return value;
}

Bool encode( const ::Engine::SessionMessage &message, Encoded &out )
{
	const std::size_t n = ::Engine::encodeSession(message, out.data, sizeof(out.data));
	if (n == 0)
	{
		DEBUG_LOG(("Lobby::encode - session message of type %d does not fit\n", static_cast<Int>(message.type)));
		out.size = 0;
		return FALSE;
	}
	out.size = static_cast<Int>(n);
	return TRUE;
}

void sendTreff( Transport *transport, ::Engine::TreffMessage &message, const ::Engine::NetAddress &server, std::size_t padTo )
{
	if (transport == nullptr || !(server.isV4() || server.isV6()))
		return;
	if (padTo != 0)
		::Engine::padTreffRequest(message, padTo);
	UnsignedByte buf[::Engine::MAX_TREFF_MESSAGE];
	const std::size_t n = ::Engine::encodeTreff(message, buf, sizeof(buf));
	if (n == 0)
	{
		DEBUG_LOG(("Lobby::sendTreff - rendezvous message of type %d does not fit\n", static_cast<Int>(message.type)));
		return;
	}
	transport->sendRaw(server, buf, static_cast<Int>(n));
}

} // namespace Lobby
