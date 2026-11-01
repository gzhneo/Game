/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// gzh-rendezvous -- rendezvous server for GZH Neo network games.
//
// Tells players how the internet sees them, keeps the rooms hosts register, introduces joiners to
// hosts so both can open their NATs, lists public rooms, names other servers and relays packets
// between players that cannot reach each other. Everything it decides is in Engine::RendezvousServer;
// this file is the socket loop, the settings and the log. Runs on Linux and Windows.

#include "Engine/Rendezvous.h"
#include "Engine/RendezvousServer.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
	#include <winsock2.h>
	#include <ws2tcpip.h>
	typedef SOCKET SocketHandle;
	typedef int SocketLength;
	static const SocketHandle NO_SOCKET = INVALID_SOCKET;
	static void closeSocket(SocketHandle s) { closesocket(s); }
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <poll.h>
	#include <sys/socket.h>
	#include <unistd.h>
	typedef int SocketHandle;
	typedef socklen_t SocketLength;
	static const SocketHandle NO_SOCKET = -1;
	static void closeSocket(SocketHandle s) { close(s); }
#endif

namespace
{
	const char VERSION[] = "gzh-rendezvous 1";

	std::atomic<bool> g_stop{ false };

	void onSignal(int)
	{
		g_stop = true;
	}

	struct Options
	{
		std::uint16_t port = 8090;
		std::string bind = "::";
		Engine::RendezvousServer::Config config;
		bool logAddresses = false;
		std::uint32_t statsSeconds = 300;
	};

	std::uint32_t nowMs()
	{
		return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	void logLine(const char *format, const std::string &a = std::string(), const std::string &b = std::string())
	{
		char stamp[32];
		const std::time_t t = std::time(nullptr);
		std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", std::gmtime(&t));
		std::printf("%s ", stamp);
		std::printf(format, a.c_str(), b.c_str());
		std::printf("\n");
		std::fflush(stdout);
	}

	std::string trim(const std::string &s)
	{
		std::size_t b = 0;
		std::size_t e = s.size();
		while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r'))
			++b;
		while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r'))
			--e;
		return s.substr(b, e - b);
	}

	bool yes(const std::string &v)
	{
		return v == "yes" || v == "ja" || v == "true" || v == "1" || v == "on";
	}

	bool number(const std::string &v, unsigned long long maxValue, unsigned long long &out)
	{
		if (v.empty() || v.size() > 12)
			return false;
		unsigned long long n = 0;
		for (char c : v)
		{
			if (c < '0' || c > '9')
				return false;
			n = n * 10 + static_cast<unsigned>(c - '0');
		}
		if (n > maxValue)
			return false;
		out = n;
		return true;
	}

	/// One setting; false with a message if key or value is unknown.
	bool apply(Options &o, const std::string &key, const std::string &value, std::string &error)
	{
		unsigned long long n = 0;
		Engine::RendezvousServer::Config &c = o.config;
		if (key == "port" && number(value, 65535, n) && n > 0)
			o.port = static_cast<std::uint16_t>(n);
		else if (key == "bind" && !value.empty())
			o.bind = value;
		else if (key == "max_rooms" && number(value, 1000000, n) && n > 0)
			c.maxRooms = static_cast<std::size_t>(n);
		else if (key == "rooms_per_address" && number(value, 1000, n) && n > 0)
			c.roomsPerAddress = static_cast<int>(n);
		else if (key == "room_ttl_seconds" && number(value, 3600, n) && n >= 20)
			c.roomTtlMs = static_cast<std::uint32_t>(n * 1000);
		else if (key == "relay")
			c.relay = yes(value);
		else if (key == "max_relays" && number(value, 100000, n))
			c.maxRelays = static_cast<std::size_t>(n);
		else if (key == "relay_kib_per_session" && number(value, 100000, n) && n > 0)
			c.relayBytesPerSecond = static_cast<std::uint32_t>(n * 1024);
		else if (key == "relay_mib_total" && number(value, 100000, n) && n > 0)
			c.relayTotalBytesPerSecond = n * 1024 * 1024;
		else if (key == "relay_ttl_seconds" && number(value, 3600, n) && n >= 30)
			c.relayTtlMs = static_cast<std::uint32_t>(n * 1000);
		else if (key == "knocks_per_second" && number(value, 1000, n) && n > 0)
			c.knocksPerRoomPerSecond = static_cast<std::uint32_t>(n);
		else if (key == "packets_per_second" && number(value, 100000, n) && n > 0)
			c.perSender.rate = static_cast<std::uint32_t>(n);
		else if (key == "packet_burst" && number(value, 100000, n) && n > 0)
			c.perSender.burst = static_cast<std::uint32_t>(n);
		else if (key == "peer" && !value.empty() && value.size() <= Engine::TREFF_MAX_SERVER_TEXT)
			c.peers.push_back(value);
		else if (key == "log_addresses")
			o.logAddresses = yes(value);
		else if (key == "stats_seconds" && number(value, 86400, n))
			o.statsSeconds = static_cast<std::uint32_t>(n);
		else
		{
			error = "unknown setting or bad value: " + key + " = " + value;
			return false;
		}
		return true;
	}

	bool loadConfig(const std::string &path, Options &o, std::string &error)
	{
		FILE *f = std::fopen(path.c_str(), "r");
		if (!f)
		{
			error = "cannot read " + path;
			return false;
		}
		char line[512];
		int number = 0;
		bool ok = true;
		while (ok && std::fgets(line, sizeof(line), f))
		{
			++number;
			std::string s = line;
			const std::size_t hash = s.find('#');
			if (hash != std::string::npos)
				s.erase(hash);
			s = trim(s);
			if (!s.empty() && s.back() == '\n')
				s.pop_back();
			s = trim(s);
			if (s.empty())
				continue;
			const std::size_t eq = s.find('=');
			if (eq == std::string::npos)
			{
				error = path + ":" + std::to_string(number) + ": expected key = value";
				ok = false;
				break;
			}
			if (!apply(o, trim(s.substr(0, eq)), trim(s.substr(eq + 1)), error))
			{
				error = path + ":" + std::to_string(number) + ": " + error;
				ok = false;
			}
		}
		std::fclose(f);
		return ok;
	}

	Engine::WireEndpoint endpointOf(const sockaddr_storage &a)
	{
		Engine::WireEndpoint e;
		if (a.ss_family == AF_INET)
		{
			const sockaddr_in &v4 = reinterpret_cast<const sockaddr_in &>(a);
			e = Engine::WireEndpoint::ipv4(ntohl(v4.sin_addr.s_addr), ntohs(v4.sin_port));
		}
		else if (a.ss_family == AF_INET6)
		{
			const sockaddr_in6 &v6 = reinterpret_cast<const sockaddr_in6 &>(a);
			const std::uint8_t *b = reinterpret_cast<const std::uint8_t *>(&v6.sin6_addr);
			static const std::uint8_t mapped[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF };
			if (std::memcmp(b, mapped, 12) == 0)
			{
				const std::uint32_t ip = (static_cast<std::uint32_t>(b[12]) << 24) | (static_cast<std::uint32_t>(b[13]) << 16)
					| (static_cast<std::uint32_t>(b[14]) << 8) | b[15];
				e = Engine::WireEndpoint::ipv4(ip, ntohs(v6.sin6_port));
			}
			else
			{
				e.family = 6;
				std::memcpy(e.address, b, 16);
				e.port = ntohs(v6.sin6_port);
			}
		}
		return e;
	}

	/// Socket address for an endpoint; an IPv4 endpoint on a dual-stack socket is IPv4-mapped.
	SocketLength addressOf(const Engine::WireEndpoint &e, bool ipv6Socket, sockaddr_storage &out)
	{
		std::memset(&out, 0, sizeof(out));
		if (!ipv6Socket)
		{
			sockaddr_in &v4 = reinterpret_cast<sockaddr_in &>(out);
			v4.sin_family = AF_INET;
			v4.sin_addr.s_addr = htonl(e.ipv4Address());
			v4.sin_port = htons(e.port);
			return static_cast<SocketLength>(sizeof(sockaddr_in));
		}
		sockaddr_in6 &v6 = reinterpret_cast<sockaddr_in6 &>(out);
		v6.sin6_family = AF_INET6;
		std::uint8_t *b = reinterpret_cast<std::uint8_t *>(&v6.sin6_addr);
		if (e.family == 4)
		{
			b[10] = 0xFF;
			b[11] = 0xFF;
			std::memcpy(b + 12, e.address, 4);
		}
		else
		{
			std::memcpy(b, e.address, 16);
		}
		v6.sin6_port = htons(e.port);
		return static_cast<SocketLength>(sizeof(sockaddr_in6));
	}

	/// Opens the server socket: dual stack on "::", otherwise the family of the bind address.
	SocketHandle openSocket(const std::string &bindAddress, std::uint16_t port, bool &ipv6Socket, std::uint16_t &boundPort)
	{
		in_addr v4{};
		in6_addr v6{};
		const bool isV4 = inet_pton(AF_INET, bindAddress.c_str(), &v4) == 1;
		const bool isV6 = !isV4 && inet_pton(AF_INET6, bindAddress.c_str(), &v6) == 1;
		if (!isV4 && !isV6)
			return NO_SOCKET;
		SocketHandle s = NO_SOCKET;
		if (isV6)
		{
			s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
			if (s != NO_SOCKET)
			{
				int off = 0;
				setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char *>(&off), sizeof(off));
				sockaddr_in6 a;
				std::memset(&a, 0, sizeof(a));
				a.sin6_family = AF_INET6;
				a.sin6_addr = v6;
				a.sin6_port = htons(port);
				if (bind(s, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0)
					ipv6Socket = true;
				else
				{
					closeSocket(s);
					s = NO_SOCKET;
				}
			}
			if (s == NO_SOCKET && bindAddress == "::")
			{
				// no IPv6 on this machine: IPv4 only
				std::memset(&v4, 0, sizeof(v4));
				return openSocket("0.0.0.0", port, ipv6Socket, boundPort);
			}
		}
		else
		{
			s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (s != NO_SOCKET)
			{
				sockaddr_in a;
				std::memset(&a, 0, sizeof(a));
				a.sin_family = AF_INET;
				a.sin_addr = v4;
				a.sin_port = htons(port);
				if (bind(s, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0)
				{
					closeSocket(s);
					s = NO_SOCKET;
				}
				ipv6Socket = false;
			}
		}
		if (s != NO_SOCKET)
		{
			sockaddr_storage a;
			SocketLength len = sizeof(a);
			getsockname(s, reinterpret_cast<sockaddr *>(&a), &len);
			boundPort = endpointOf(a).port;
		}
		return s;
	}

	bool readable(SocketHandle s, int timeoutMs)
	{
#if defined(_WIN32)
		fd_set set;
		FD_ZERO(&set);
		FD_SET(s, &set);
		timeval tv;
		tv.tv_sec = timeoutMs / 1000;
		tv.tv_usec = (timeoutMs % 1000) * 1000;
		return select(0, &set, nullptr, nullptr, &tv) > 0;
#else
		pollfd p;
		p.fd = s;
		p.events = POLLIN;
		p.revents = 0;
		return poll(&p, 1, timeoutMs) > 0;
#endif
	}

	/// The server loop until g_stop (or `stop`) is set.
	int serve(SocketHandle s, bool ipv6Socket, const Options &o, const std::atomic<bool> *stop = nullptr)
	{
		Engine::RendezvousServer server;
		server.configure(o.config);
		std::vector<Engine::RendezvousServer::Datagram> out;
		std::uint8_t buffer[2048];
		std::uint32_t lastExpire = nowMs();
		std::uint32_t lastStats = lastExpire;
		while (!g_stop && !(stop && *stop))
		{
			if (readable(s, 250))
			{
				for (int i = 0; i < 1000; ++i)
				{
					sockaddr_storage from;
					SocketLength fromLength = sizeof(from);
					const int n = static_cast<int>(recvfrom(s, reinterpret_cast<char *>(buffer), sizeof(buffer), 0,
						reinterpret_cast<sockaddr *>(&from), &fromLength));
					if (n <= 0)
						break;
					out.clear();
					const Engine::WireEndpoint sender = endpointOf(from);
					server.handle(sender, buffer, static_cast<std::size_t>(n), nowMs(), out);
					for (const auto &d : out)
					{
						sockaddr_storage to;
						const SocketLength toLength = addressOf(d.to, ipv6Socket, to);
						if (!ipv6Socket && d.to.family != 4)
							continue;
						sendto(s, reinterpret_cast<const char *>(d.data.data()), static_cast<int>(d.data.size()), 0,
							reinterpret_cast<const sockaddr *>(&to), toLength);
					}
					if (o.logAddresses && !out.empty())
						logLine("%s: %s", sender.text(), std::to_string(out.size()) + " answer(s)");
					if (!readable(s, 0))
						break;
				}
			}
			const std::uint32_t now = nowMs();
			if (static_cast<std::uint32_t>(now - lastExpire) >= 1000)
			{
				server.expire(now);
				lastExpire = now;
			}
			if (o.statsSeconds != 0 && static_cast<std::uint32_t>(now - lastStats) >= o.statsSeconds * 1000u)
			{
				lastStats = now;
				const Engine::RendezvousServer::Stats &st = server.stats();
				char text[256];
				std::snprintf(text, sizeof(text), "rooms %zu, relays %zu, received %llu, answered %llu, malformed %llu, limited %llu, relayed %llu (%llu bytes), relay dropped %llu",
					server.roomCount(), server.relayCount(), static_cast<unsigned long long>(st.received),
					static_cast<unsigned long long>(st.answered), static_cast<unsigned long long>(st.malformed),
					static_cast<unsigned long long>(st.limited), static_cast<unsigned long long>(st.relayed),
					static_cast<unsigned long long>(st.relayedBytes), static_cast<unsigned long long>(st.relayDropped));
				logLine("%s%s", text);
			}
		}
		return 0;
	}

	// --- self test -------------------------------------------------------------------------------

	bool exchange(SocketHandle client, std::uint16_t serverPort, const Engine::TreffMessage &request, std::size_t padTo,
		Engine::TreffMessage &answer)
	{
		Engine::TreffMessage m = request;
		if (padTo != 0)
			Engine::padTreffRequest(m, padTo);
		std::uint8_t buffer[2048];
		const std::size_t size = Engine::encodeTreff(m, buffer, sizeof(buffer));
		sockaddr_storage to;
		const SocketLength toLength = addressOf(Engine::WireEndpoint::ipv4(0x7F000001, serverPort), false, to);
		sendto(client, reinterpret_cast<const char *>(buffer), static_cast<int>(size), 0, reinterpret_cast<const sockaddr *>(&to), toLength);
		if (!readable(client, 2000))
			return false;
		const int n = static_cast<int>(recv(client, reinterpret_cast<char *>(buffer), sizeof(buffer), 0));
		return n > 0 && Engine::decodeTreff(buffer, static_cast<std::size_t>(n), answer);
	}

	int selfTest()
	{
		Options o;
		bool ipv6 = false;
		std::uint16_t port = 0;
		SocketHandle s = openSocket("127.0.0.1", 0, ipv6, port);
		if (s == NO_SOCKET)
		{
			std::printf("selftest: cannot open a socket\n");
			return 1;
		}
		std::atomic<bool> stop{ false };
		std::thread thread([&] { serve(s, ipv6, o, &stop); });
		bool dummy = false;
		std::uint16_t hostPort = 0;
		std::uint16_t joinerPort = 0;
		SocketHandle host = openSocket("127.0.0.1", 0, dummy, hostPort);
		SocketHandle joiner = openSocket("127.0.0.1", 0, dummy, joinerPort);
		int failures = 0;
		auto check = [&failures](bool ok, const char *what) {
			std::printf("  %s: %s\n", ok ? "ok" : "FAILED", what);
			failures += ok ? 0 : 1;
		};

		Engine::TreffMessage m;
		Engine::TreffMessage a;
		m.type = Engine::TreffType::OBSERVE;
		m.tag = 1;
		check(exchange(joiner, port, m, Engine::TREFF_OBSERVE_SIZE, a) && a.type == Engine::TreffType::OBSERVED
			&& a.observed.port == joinerPort, "observe");

		m = Engine::TreffMessage();
		m.type = Engine::TreffType::REGISTER;
		m.room = "K7Q2";
		m.owner[0] = 7;
		m.isPublic = true;
		m.game.gameName = "Selftest";
		check(exchange(host, port, m, 0, a) && a.type == Engine::TreffType::REGISTER_ACK && a.observed.port == hostPort, "register");

		m = Engine::TreffMessage();
		m.type = Engine::TreffType::LOOKUP;
		m.room = "K7Q2";
		check(exchange(joiner, port, m, Engine::TREFF_LOOKUP_SIZE, a) && a.type == Engine::TreffType::FOUND
			&& a.lobby[0].port == hostPort, "lookup");
		std::uint8_t buffer[2048];
		Engine::TreffMessage knock;
		const bool knocked = readable(host, 2000)
			&& Engine::decodeTreff(buffer, static_cast<std::size_t>(recv(host, reinterpret_cast<char *>(buffer), sizeof(buffer), 0)), knock);
		check(knocked && knock.type == Engine::TreffType::KNOCK && knock.lobby[0].port == joinerPort, "knock to the host");

		m = Engine::TreffMessage();
		m.type = Engine::TreffType::LIST;
		check(exchange(joiner, port, m, Engine::TREFF_LIST_SIZE, a) && a.type == Engine::TreffType::GAMES && a.games.size() == 1
			&& a.games[0].gameName == "Selftest", "public list");

		stop = true;
		thread.join();
		closeSocket(host);
		closeSocket(joiner);
		closeSocket(s);
		std::printf("selftest: %d failures\n", failures);
		return failures == 0 ? 0 : 1;
	}

	void usage()
	{
		std::printf("%s -- rendezvous server for GZH Neo network games\n\n"
			"  --config <file>      settings, one \"key = value\" per line (see rendezvous.conf)\n"
			"  --port <n>           UDP port (default 8090)\n"
			"  --bind <address>     local address (default :: = all, IPv6 and IPv4)\n"
			"  --peer <host[:port]> another server to name to clients (repeatable)\n"
			"  --no-relay           do not relay packets\n"
			"  --log-addresses      log client addresses (off by default)\n"
			"  --stats <seconds>    statistics interval, 0 = off (default 300)\n"
			"  --selftest           run a short self test on 127.0.0.1 and exit\n"
			"  --version, --help\n", VERSION);
	}
}

int main(int argc, char **argv)
{
#if defined(_WIN32)
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return 1;
#endif
	Options o;
	std::string error;
	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		const bool hasValue = i + 1 < argc;
		if (arg == "--help" || arg == "-h")
		{
			usage();
			return 0;
		}
		if (arg == "--version")
		{
			std::printf("%s\n", VERSION);
			return 0;
		}
		if (arg == "--selftest")
			return selfTest();
		bool ok = true;
		if (arg == "--config" && hasValue)
			ok = loadConfig(argv[++i], o, error);
		else if (arg == "--port" && hasValue)
			ok = apply(o, "port", argv[++i], error);
		else if (arg == "--bind" && hasValue)
			ok = apply(o, "bind", argv[++i], error);
		else if (arg == "--peer" && hasValue)
			ok = apply(o, "peer", argv[++i], error);
		else if (arg == "--stats" && hasValue)
			ok = apply(o, "stats_seconds", argv[++i], error);
		else if (arg == "--no-relay")
			o.config.relay = false;
		else if (arg == "--log-addresses")
			o.logAddresses = true;
		else
		{
			error = "unknown argument: " + arg;
			ok = false;
		}
		if (!ok)
		{
			std::fprintf(stderr, "%s\n", error.c_str());
			return 2;
		}
	}

	bool ipv6 = false;
	std::uint16_t port = 0;
	SocketHandle s = openSocket(o.bind, o.port, ipv6, port);
	if (s == NO_SOCKET)
	{
		std::fprintf(stderr, "cannot open UDP port %u on %s\n", static_cast<unsigned>(o.port), o.bind.c_str());
		return 1;
	}
	std::signal(SIGINT, onSignal);
	std::signal(SIGTERM, onSignal);
	const std::string shown = ipv6 ? "[" + o.bind + "]:" + std::to_string(port)
		: (o.bind == "::" ? "0.0.0.0:" + std::to_string(port) + " (IPv4 only, no IPv6 here)" : o.bind + ":" + std::to_string(port));
	logLine("%s listening on UDP %s", VERSION, shown
		+ (o.config.relay ? ", relay on" : ", relay off") + ", " + std::to_string(o.config.peers.size()) + " peer(s)");
	const int result = serve(s, ipv6, o);
	logLine("%s%s", "stopped");
	closeSocket(s);
	return result;
}
