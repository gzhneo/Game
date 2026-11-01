/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FuzzSession.cpp -- fuzz target for the session and rendezvous messages (Engine/SessionMessage.cpp,
// Rendezvous.cpp) and for the rendezvous server (RendezvousServer.cpp).
//
// Every datagram a player or a server receives from the internet goes through these readers. What a
// reader accepts must encode back to the same bytes (one form per message), and the server must
// digest any sequence of datagrams. Run as libFuzzer target or as a plain program with input files as
// arguments; "--seeds <dir>" writes one sample of every message type there.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Engine/InviteCode.h"
#include "Engine/Rendezvous.h"
#include "Engine/RendezvousServer.h"
#include "Engine/SessionMessage.h"

namespace
{
	Engine::RendezvousServer &server()
	{
		static Engine::RendezvousServer s;
		return s;
	}
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size > 4096)
		return 0;
	uint8_t again[4096];

	Engine::SessionMessage session;
	if (Engine::decodeSession(data, size, session))
	{
		const size_t n = Engine::encodeSession(session, again, sizeof(again));
		if (n != size || std::memcmp(again, data, size) != 0)
			std::abort();
	}
	Engine::SessionJoinInfo info;
	if (Engine::decodeJoinInfo(data, size, info))
	{
		const size_t n = Engine::encodeJoinInfo(info, again, sizeof(again));
		if (n != size || std::memcmp(again, data, size) != 0)
			std::abort();
	}
	Engine::TreffMessage treff;
	if (Engine::decodeTreff(data, size, treff))
	{
		const size_t n = Engine::encodeTreff(treff, again, sizeof(again));
		if (n != size || std::memcmp(again, data, size) != 0)
			std::abort();
	}
	Engine::InviteCode code;
	Engine::parseInviteCode(std::string(reinterpret_cast<const char *>(data), size < 300 ? size : 300), code);

	// the server: the sender comes from the first byte, the time from the second
	if (size >= 2)
	{
		static uint32_t now = 1000;
		now += data[1] * 50u;
		const Engine::WireEndpoint from = Engine::WireEndpoint::ipv4(0x0A000000u + data[0], static_cast<uint16_t>(40000 + (data[0] & 3)));
		std::vector<Engine::RendezvousServer::Datagram> out;
		server().handle(from, data + 2, size - 2, now, out);
		for (const auto &d : out)
		{
			if (d.data.size() > Engine::MAX_TREFF_MESSAGE + Engine::RELAY_HEADER_SIZE || !d.to.isSet())
				std::abort();
		}
		server().expire(now);
	}
	return 0;
}

#ifndef FUZZ_LIBFUZZER_MAIN
namespace
{
	void write(const std::string &dir, const std::string &name, const uint8_t *data, size_t size)
	{
		const std::string path = dir + "/" + name;
		FILE *f = std::fopen(path.c_str(), "wb");
		if (f)
		{
			std::fwrite(data, 1, size, f);
			std::fclose(f);
		}
	}

	void seeds(const std::string &dir)
	{
		uint8_t buf[4096];
		for (uint8_t t = 1; t < 64; ++t)
		{
			if (!Engine::isKnownSessionType(t))
				continue;
			Engine::SessionMessage m;
			m.type = static_cast<Engine::SessionType>(t);
			m.name = "Alice";
			m.text = "hi";
			m.gameName = "Game";
			m.options = "M=maps\\x;";
			m.sealed.assign(20, 1);
			m.member.endpoints.game[0] = Engine::WireEndpoint::ipv4(0x0A000002, 8088);
			const size_t n = Engine::encodeSession(m, buf, sizeof(buf));
			write(dir, "session-" + std::to_string(t), buf, n);
		}
		for (uint8_t t = 1; t <= 17; ++t)
		{
			Engine::TreffMessage m;
			m.type = static_cast<Engine::TreffType>(t);
			m.room = "K7Q2";
			m.observed = Engine::WireEndpoint::ipv4(0xCB007107, 40000);
			m.lobby[0] = m.observed;
			m.relayId = 7;
			m.padding = t == 6 ? 200 : (t == 9 || t == 11 ? 1050 : 40);
			const size_t n = Engine::encodeTreff(m, buf + 2, sizeof(buf) - 2);
			buf[0] = 1;
			buf[1] = 0;
			write(dir, "treff-" + std::to_string(t), buf, n + 2);
		}
		const char invite[] = "K7Q2-DP941QG2@meet.example.org:8090";
		write(dir, "invite", reinterpret_cast<const uint8_t *>(invite), sizeof(invite) - 1);
	}
}

int main(int argc, char **argv)
{
	if (argc == 3 && std::strcmp(argv[1], "--seeds") == 0)
	{
		seeds(argv[2]);
		return 0;
	}
	for (int i = 1; i < argc; ++i)
	{
		FILE *f = std::fopen(argv[i], "rb");
		if (!f) { std::printf("nicht lesbar: %s\n", argv[i]); return 2; }
		std::vector<uint8_t> buf;
		uint8_t tmp[65536];
		size_t n;
		while ((n = std::fread(tmp, 1, sizeof(tmp), f)) > 0) buf.insert(buf.end(), tmp, tmp + n);
		std::fclose(f);
		LLVMFuzzerTestOneInput(buf.data(), buf.size());
	}
	return 0;
}
#endif
