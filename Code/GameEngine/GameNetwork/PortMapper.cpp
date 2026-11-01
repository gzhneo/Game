/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// PortMapper.cpp -- see PortMapper.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/PortMapper.h"
#include "GameNetwork/SecuritySoftware.h"
#include "Engine/PortMapping.h"

#include <ws2tcpip.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <vector>

namespace PM = ::Engine::PortMapping;

namespace
{
	const char MAPPING_DESCRIPTION[] = "GZH Neo";
	const std::uint32_t REMOVE_TIMEOUT_MS = 1000;
	const std::uint32_t POLL_SLICE_MS = 100;	///< how quickly a blocking step notices stop()
	const int MAX_LOCATIONS = 8;

	struct WinsockScope
	{
		bool ok;
		WinsockScope() { WSADATA data; ok = WSAStartup(MAKEWORD(2, 2), &data) == 0; }
		~WinsockScope() { if (ok) WSACleanup(); }
	};

	struct Socket
	{
		SOCKET s = INVALID_SOCKET;
		~Socket() { if (s != INVALID_SOCKET) closesocket(s); }
	};

	sockaddr_in makeAddress(std::uint32_t ip, std::uint16_t port)
	{
		sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(ip);
		addr.sin_port = htons(port);
		return addr;
	}

	std::uint32_t nowMs()
	{
		return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	bool openSocket(Socket &sock, int type, std::uint32_t localIP)
	{
		sock.s = socket(AF_INET, type, type == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP);
		if (sock.s == INVALID_SOCKET)
			return false;
		sockaddr_in local = makeAddress(localIP, 0);	// leave through the interface of the game
		u_long nonBlocking = 1;
		return bind(sock.s, reinterpret_cast<sockaddr *>(&local), static_cast<int>(sizeof(local))) == 0
			&& ioctlsocket(sock.s, FIONBIO, &nonBlocking) == 0;
	}

	/// Waits until the socket is readable (or writable), the time is up or `stop` is set.
	bool waitSocket(SOCKET s, bool forWrite, std::uint32_t deadline, const std::atomic<bool> &stop)
	{
		while (!stop.load())
		{
			const std::int32_t left = static_cast<std::int32_t>(deadline - nowMs());
			if (left <= 0)
				return false;
			fd_set set;
			FD_ZERO(&set);
			FD_SET(s, &set);
			fd_set error;
			FD_ZERO(&error);
			FD_SET(s, &error);
			const std::uint32_t slice = static_cast<std::uint32_t>(left) < POLL_SLICE_MS ? static_cast<std::uint32_t>(left) : POLL_SLICE_MS;
			timeval tv;
			tv.tv_sec = 0;
			tv.tv_usec = static_cast<long>(slice * 1000);
			const int ready = select(0, forWrite ? nullptr : &set, forWrite ? &set : nullptr, &error, &tv);
			if (ready < 0)
				return false;
			if (ready > 0)
				return FD_ISSET(s, &set) != 0;
		}
		return false;
	}

	/// An IPv4 literal or a host name (some routers name themselves, e.g. fritz.box). Callers compare
	/// the result with the router that answered the search, so a name leading elsewhere is refused.
	std::uint32_t resolveIPv4(const std::string &host)
	{
		in_addr literal;
		if (inet_pton(AF_INET, host.c_str(), &literal) == 1)
			return ntohl(literal.s_addr);
		if (host.empty() || host.size() > 253)
			return 0;
		addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		addrinfo *found = nullptr;
		if (getaddrinfo(host.c_str(), nullptr, &hints, &found) != 0 || found == nullptr)
			return 0;
		std::uint32_t ip = 0;
		if (found->ai_family == AF_INET && found->ai_addr != nullptr && found->ai_addrlen >= sizeof(sockaddr_in))
			ip = ntohl(reinterpret_cast<const sockaddr_in *>(found->ai_addr)->sin_addr.s_addr);
		freeaddrinfo(found);
		return ip;
	}

	/// One HTTP exchange. Ends when the response is complete by its length, or when the router closes
	/// the connection: some keep it open despite "Connection: close". `why` names a failure.
	bool httpExchange(std::uint32_t localIP, const PM::HttpUrl &url, const std::string &request, std::string &response,
		std::uint32_t timeoutMs, const std::atomic<bool> &stop, const char **why = nullptr)
	{
		const char *ignored = nullptr;
		const char *&reason = why != nullptr ? *why : ignored;
		reason = nullptr;
		const std::uint32_t ip = resolveIPv4(url.host);
		if (ip == 0)
		{
			reason = "host name not resolved";
			return false;
		}
		Socket sock;
		if (!openSocket(sock, SOCK_STREAM, localIP))
		{
			reason = "no socket";
			return false;
		}
		const std::uint32_t deadline = nowMs() + timeoutMs;
		sockaddr_in remote = makeAddress(ip, url.port);
		if (connect(sock.s, reinterpret_cast<sockaddr *>(&remote), static_cast<int>(sizeof(remote))) != 0
			&& WSAGetLastError() != WSAEWOULDBLOCK)
		{
			reason = "connection refused";
			return false;
		}
		if (!waitSocket(sock.s, true, deadline, stop))
		{
			reason = "no connection within the time";
			return false;
		}
		int error = 0;
		int errorSize = static_cast<int>(sizeof(error));
		if (getsockopt(sock.s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &errorSize) != 0 || error != 0)
		{
			reason = "connection refused";
			return false;
		}
		std::size_t sent = 0;
		while (sent < request.size())
		{
			const int n = send(sock.s, request.data() + sent, static_cast<int>(request.size() - sent), 0);
			if (n > 0)
				sent += static_cast<std::size_t>(n);
			else if (WSAGetLastError() != WSAEWOULDBLOCK || !waitSocket(sock.s, true, deadline, stop))
			{
				reason = "request not sent";
				return false;
			}
		}
		response.clear();
		char buffer[4096];
		while (response.size() < PM::MAX_HTTP_RESPONSE)
		{
			const int n = recv(sock.s, buffer, static_cast<int>(sizeof(buffer)), 0);
			if (n > 0)
			{
				response.append(buffer, static_cast<std::size_t>(n));
				PM::HttpResponse parsed;
				if (PM::parseHttpResponse(response, parsed) && parsed.framed)
					return true;	// complete by its length; the router may keep the connection open
			}
			else if (n == 0)
				return true;	// closed: complete
			else if (WSAGetLastError() != WSAEWOULDBLOCK || !waitSocket(sock.s, false, deadline, stop))
			{
				reason = response.empty() ? "no answer within the time" : "answer cut short";
				return false;
			}
		}
		reason = "answer too large";
		return false;
	}

	// GetBestRoute from iphlpapi, loaded at run time so no target needs another import library. The
	// row is MIB_IPFORWARDROW: 14 DWORDs, the next hop is the fourth (network byte order).
	std::uint32_t defaultGateway(std::uint32_t localIP)
	{
		typedef DWORD (WINAPI *GetBestRouteFn)(DWORD, DWORD, void *);
		static GetBestRouteFn s_getBestRoute = []() -> GetBestRouteFn {
			HMODULE library = ::LoadLibraryA("iphlpapi.dll");
			return library ? reinterpret_cast<GetBestRouteFn>(reinterpret_cast<void *>(::GetProcAddress(library, "GetBestRoute"))) : nullptr;
		}();
		if (s_getBestRoute == nullptr)
			return 0;
		DWORD row[14] = {};
		const DWORD someInternetAddress = htonl(0x08080808u);	// only the route towards it is looked up
		if (s_getBestRoute(someInternetAddress, htonl(localIP), row) != NO_ERROR)
			return 0;
		const std::uint32_t nextHop = ntohl(row[3]);
		return (nextHop != 0 && nextHop != localIP) ? nextHop : 0;
	}
}

// On the heap so that ReleasePortMapper can give everything back before the leak check at exit
// (a static object would only be destroyed after it).
static PortMapper *s_mapper = nullptr;

PortMapper &ThePortMapper()
{
	if (s_mapper == nullptr)
		s_mapper = new PortMapper;
	return *s_mapper;
}

void ReleasePortMapper()
{
	delete s_mapper;	// the destructor stops the worker thread
	s_mapper = nullptr;
}

PortMapper::~PortMapper()
{
	stop();
}

void PortMapper::start(const Settings &settings)
{
	if (m_thread.joinable())
	{
		if (settings.localIP == m_settings.localIP)
			return;
		stop();	// another interface: map for that one instead
	}
	m_settings = settings;
	if (m_settings.portCount > MAX_PORTS)
		m_settings.portCount = MAX_PORTS;
	{
		std::lock_guard<std::mutex> lock(m_logMutex);
		m_log.clear();
	}
	note("this computer %s, UDP ports %u and %u, IPv6 %s", PM::ipv4Text(m_settings.localIP).c_str(),
		m_settings.portCount > 0 ? m_settings.ports[0] : 0, m_settings.portCount > 1 ? m_settings.ports[1] : 0,
		m_settings.localIPv6.empty() ? "none" : m_settings.localIPv6.c_str());
	m_stop.store(false);
	m_method.store(METHOD_NONE);
	m_externalIP.store(0);
	m_failure.store(FAILURE_NONE);
	m_refusalCode.store(0);
	m_routerAnswered = false;
	m_upnpDiscovered = false;
	m_upnpDevices.clear();
	m_firewallServiceType.clear();
	m_firewallControlUrl.clear();
	m_pinholeCount = 0;
	m_pinholeRefusal.store(0);
	m_searchOutcome.store(SEARCH_NOT_RUN);
	m_pinholeState.store(m_settings.localIPv6.empty() ? PINHOLE_OFF : PINHOLE_WORKING);
	m_upnpServiceType.clear();
	m_upnpControlUrl.clear();
	m_upnpPermanent = false;
	m_gateway = 0;
	setState(STATE_WORKING);
	m_thread = std::thread(&PortMapper::run, this);
}

void PortMapper::stop()
{
	if (!m_thread.joinable())
		return;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop.store(true);
	}
	m_wake.notify_all();
	m_thread.join();
	m_pinholeState.store(PINHOLE_OFF);
	setState(STATE_OFF);
}

void PortMapper::setState(State state)
{
	m_state.store(state);
	m_changes.fetch_add(1);
}

bool PortMapper::waitStop(std::uint32_t ms)
{
	std::unique_lock<std::mutex> lock(m_mutex);
	return m_wake.wait_for(lock, std::chrono::milliseconds(ms), [this] { return m_stop.load(); });
}

void PortMapper::note(const char *format, ...)
{
	char line[400];
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	DEBUG_LOG(("PortMapper: %s\n", line));
	std::lock_guard<std::mutex> lock(m_logMutex);
	if (m_log.size() < 200)
		m_log.push_back(line);
}

std::vector<std::string> PortMapper::log() const
{
	std::lock_guard<std::mutex> lock(m_logMutex);
	return m_log;
}

void PortMapper::setPinholeState(PinholeState state, int refusal)
{
	if (state == m_pinholeState.load() && refusal == m_pinholeRefusal.load())
		return;	// callers report each change once
	m_pinholeRefusal.store(refusal);
	m_pinholeState.store(state);
	m_changes.fetch_add(1);
}

void PortMapper::refused(int code)
{
	m_routerAnswered = true;
	m_refusalCode.store(code);
}

void PortMapper::run()
{
	WinsockScope winsock;
	if (!winsock.ok)
	{
		setState(STATE_FAILED);
		return;
	}
	const std::uint32_t lifetime = m_settings.lifetimeSeconds;
	if (m_settings.tryNatPmp && mapNatPmp(lifetime))
		m_method.store(METHOD_NATPMP);
	else if (!stopping() && m_settings.tryUpnp && mapUpnp(lifetime))
		m_method.store(METHOD_UPNP);

	const bool mapped = m_method.load() != METHOD_NONE;
	if (mapped)
		note("IPv4: ports forwarded by %s, external address %s", m_method.load() == METHOD_NATPMP ? "NAT-PMP" : "UPnP",
			PM::ipv4Text(m_externalIP.load()).c_str());
	else
		note("IPv4: no forwarding (%s, code %d)", m_routerAnswered ? "refused" : "no router answered", m_refusalCode.load());
	const bool pinholes = !stopping() && !m_settings.localIPv6.empty() && m_settings.tryUpnp && openPinholes(lifetime);

	// the firewalls of this computer, while the log is still being written (the lobby writes it
	// once the IPv4 state is final)
	const PinholeState holes = m_pinholeState.load();
	const bool failedSomewhere = !mapped || holes == PINHOLE_REFUSED || holes == PINHOLE_NO_SERVICE
		|| (m_settings.tryUpnp && m_searchOutcome.load() != SEARCH_ANSWERED && m_searchOutcome.load() != SEARCH_NOT_RUN);
	if (failedSomewhere && m_settings.describeSecuritySoftware && !stopping())
	{
		for (const std::string &line : DescribeSecuritySoftware())
			note("%s", line.c_str());
	}

	if (!mapped)
	{
		m_failure.store(m_routerAnswered ? FAILURE_REFUSED : FAILURE_NO_ROUTER);
		if (!stopping())
			setState(STATE_FAILED);
	}
	else
	{
		setState(STATE_MAPPED);
	}
	if (!mapped && !pinholes)
		return;

	const std::uint32_t renewMs = (lifetime > 120 ? lifetime / 2 : 60) * 1000u;
	while (!waitStop(renewMs))
	{
		if (m_method.load() != METHOD_NONE)
		{
			const bool renewed = renew();
			if (renewed != (m_state.load() == STATE_MAPPED))
			{
				note("renewing the forwarding %s", renewed ? "works again" : "failed");
				m_failure.store(renewed ? FAILURE_NONE : FAILURE_REFUSED);
				setState(renewed ? STATE_MAPPED : STATE_FAILED);
			}
		}
		if (m_pinholeCount > 0)
		{
			if (!renewPinholes(lifetime) && !stopping())
				openPinholes(lifetime);	// the router lost them (restart): ask again
		}
	}
	removeMappings();
	removePinholes(m_settings.timeoutMs < REMOVE_TIMEOUT_MS ? m_settings.timeoutMs : REMOVE_TIMEOUT_MS);
}

// --- NAT-PMP ----------------------------------------------------------------------------------

namespace
{
	/// Sends a request to the gateway and waits for the response of the expected kind, repeating
	/// with doubled waits (250, 500, 1000 ms) within the time given.
	bool natpmpExchange(SOCKET s, std::uint32_t gateway, std::uint16_t port, const std::uint8_t *request, std::size_t size,
		std::uint8_t expectedOp, std::uint16_t internalPort, PM::NatPmpResponse &out, std::uint32_t timeoutMs,
		const std::atomic<bool> &stop)
	{
		const std::uint32_t end = nowMs() + timeoutMs;
		std::uint32_t wait = 250;
		while (!stop.load() && static_cast<std::int32_t>(end - nowMs()) > 0)
		{
			sockaddr_in to = makeAddress(gateway, port);
			sendto(s, reinterpret_cast<const char *>(request), static_cast<int>(size), 0, reinterpret_cast<sockaddr *>(&to), static_cast<int>(sizeof(to)));
			std::uint32_t deadline = nowMs() + wait;
			if (static_cast<std::int32_t>(deadline - end) > 0)
				deadline = end;
			while (waitSocket(s, false, deadline, stop))
			{
				std::uint8_t answer[64];
				sockaddr_in from;
				int fromSize = static_cast<int>(sizeof(from));
				const int n = recvfrom(s, reinterpret_cast<char *>(answer), static_cast<int>(sizeof(answer)), 0, reinterpret_cast<sockaddr *>(&from), &fromSize);
				if (n <= 0)
					continue;
				// only the gateway answers; anybody else on the network could claim a mapping
				if (ntohl(from.sin_addr.s_addr) != gateway || ntohs(from.sin_port) != port)
					continue;
				PM::NatPmpResponse r;
				if (PM::natpmpParse(answer, static_cast<std::size_t>(n), r) && r.op == expectedOp
					&& (expectedOp == 128 || r.result != 0 || r.internalPort == internalPort))
				{
					out = r;
					return true;
				}
			}
			wait *= 2;
		}
		return false;
	}
}

bool PortMapper::mapNatPmp(std::uint32_t lifetime)
{
	if (m_gateway == 0)
		m_gateway = m_settings.gateway != 0 ? m_settings.gateway : defaultGateway(m_settings.localIP);
	if (m_gateway == 0)
	{
		note("NAT-PMP: no default gateway for this address");
		return false;
	}
	Socket sock;
	if (!openSocket(sock, SOCK_DGRAM, m_settings.localIP))
		return false;

	std::uint8_t request[PM::NATPMP_MAP_REQUEST_SIZE];
	PM::NatPmpResponse r;
	const std::size_t size = PM::natpmpAddressRequest(request);
	if (!natpmpExchange(sock.s, m_gateway, m_settings.natpmpPort, request, size, 128, 0, r, m_settings.timeoutMs, m_stop))
	{
		note("NAT-PMP: gateway %s does not answer (most routers only speak UPnP)", PM::ipv4Text(m_gateway).c_str());
		return false;
	}
	m_routerAnswered = true;
	note("NAT-PMP: gateway %s answers, result %u, external address %s", PM::ipv4Text(m_gateway).c_str(), r.result,
		PM::ipv4Text(r.externalIP).c_str());
	if (r.result == 0 && r.externalIP != 0)
		m_externalIP.store(r.externalIP);

	for (int i = 0; i < m_settings.portCount; ++i)
	{
		const std::uint16_t port = m_settings.ports[i];
		PM::natpmpMapUdpRequest(port, port, lifetime, request);
		const bool mapped = natpmpExchange(sock.s, m_gateway, m_settings.natpmpPort, request, PM::NATPMP_MAP_REQUEST_SIZE, 129,
				port, r, m_settings.timeoutMs, m_stop)
			&& r.result == 0 && r.externalPort == port;
		if (!mapped)
		{
			note("NAT-PMP: port %u not forwarded (result %u, external port %u)", port, r.result, r.externalPort);
			refused(r.result);
			// a mapping to another external port is of no use: the other players send to ours
			for (int k = 0; k <= i; ++k)
			{
				PM::natpmpMapUdpRequest(m_settings.ports[k], 0, 0, request);
				natpmpExchange(sock.s, m_gateway, m_settings.natpmpPort, request, PM::NATPMP_MAP_REQUEST_SIZE, 129,
					m_settings.ports[k], r, 250, m_stop);
			}
			return false;
		}
	}
	return true;
}

// --- UPnP -------------------------------------------------------------------------------------

namespace
{
	/// The description and the control address must be on the router that answered the search;
	/// anybody on the network can answer and would otherwise send us anywhere.
	bool isOnHost(const std::string &url, std::uint32_t host)
	{
		PM::HttpUrl parsed;
		return PM::parseHttpUrl(url, parsed) && resolveIPv4(parsed.host) == host;
	}
}

bool PortMapper::soapCall(const std::string &controlUrl, const std::string &serviceType, const std::string &action,
	const std::string &body, std::uint32_t timeoutMs, const std::atomic<bool> &stop, PM::SoapResult &result)
{
	result = PM::SoapResult();
	PM::HttpUrl url;
	std::string raw;
	PM::HttpResponse response;
	if (!PM::parseHttpUrl(controlUrl, url))
		return false;
	const std::string request = PM::httpSoapRequest(url, serviceType, action, body);
	const char *why = nullptr;
	if (!httpExchange(m_settings.localIP, url, request, raw, timeoutMs, stop, &why))
	{
		note("%s: %s", action.c_str(), why != nullptr ? why : "failed");
		return false;
	}
	if (!PM::parseHttpResponse(raw, response) || !PM::parseSoapResponse(response, result))
	{
		note("%s: unreadable answer", action.c_str());
		return false;
	}
	note("%s: HTTP %d%s", action.c_str(), response.status, result.errorCode != 0 ? (", UPnP error " + std::to_string(result.errorCode)).c_str() : "");
	return true;
}

bool PortMapper::upnpRequest(const std::string &action, const std::string &body, std::string &response)
{
	PM::HttpUrl url;
	if (!PM::parseHttpUrl(m_upnpControlUrl, url))
		return false;
	const std::string request = PM::httpSoapRequest(url, m_upnpServiceType, action, body);
	return httpExchange(m_settings.localIP, url, request, response, m_settings.timeoutMs, m_stop);
}

// Searches once per start(): the IPv4 mapping and the IPv6 pinholes use the same gateways.
bool PortMapper::discoverUpnp()
{
	if (m_upnpDiscovered)
		return !m_upnpDevices.empty();
	m_upnpDiscovered = true;

	Socket sock;
	if (!openSocket(sock, SOCK_DGRAM, m_settings.localIP))
		return false;
	in_addr outgoing;
	outgoing.s_addr = htonl(m_settings.localIP);
	setsockopt(sock.s, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char *>(&outgoing), static_cast<int>(sizeof(outgoing)));
	const DWORD ttl = 2;
	setsockopt(sock.s, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char *>(&ttl), static_cast<int>(sizeof(ttl)));
	sockaddr_in to = makeAddress(m_settings.ssdpTarget, m_settings.ssdpPort);
	for (int i = 0; i < PM::SSDP_SEARCH_TARGET_COUNT; ++i)
	{
		const std::string search = PM::ssdpSearchRequest(PM::ssdpSearchTarget(i));
		sendto(sock.s, search.data(), static_cast<int>(search.size()), 0, reinterpret_cast<sockaddr *>(&to), static_cast<int>(sizeof(to)));
	}

	// every answer within the time is a candidate, in the order they come
	std::vector<std::pair<std::string, std::uint32_t>> locations;
	int answers = 0;
	const std::uint32_t deadline = nowMs() + m_settings.timeoutMs;
	while (static_cast<int>(locations.size()) < MAX_LOCATIONS && waitSocket(sock.s, false, deadline, m_stop))
	{
		char answer[1500];
		sockaddr_in from;
		int fromSize = static_cast<int>(sizeof(from));
		const int n = recvfrom(sock.s, answer, static_cast<int>(sizeof(answer)), 0, reinterpret_cast<sockaddr *>(&from), &fromSize);
		std::string location;
		if (n <= 0 || !PM::ssdpLocation(answer, static_cast<std::size_t>(n), location))
			continue;
		++answers;
		const std::uint32_t sender = ntohl(from.sin_addr.s_addr);
		bool known = false;
		for (const auto &entry : locations)
			known = known || entry.first == location;
		if (known)
			continue;
		std::string server;
		PM::ssdpHeader(answer, static_cast<std::size_t>(n), "SERVER:", server);
		const bool onHost = isOnHost(location, sender);
		note("UPnP search: answer from %s, %s, router \"%s\"%s", PM::ipv4Text(sender).c_str(), location.c_str(), server.c_str(),
			onHost ? "" : " -- ignored: the description is on another computer");
		if (onHost)
			locations.push_back(std::make_pair(location, sender));
	}
	if (answers == 0)
		note("UPnP search: no answer within %u ms", m_settings.timeoutMs);

	for (const auto &entry : locations)
	{
		PM::HttpUrl url;
		std::string raw;
		PM::HttpResponse description;
		if (stopping() || !PM::parseHttpUrl(entry.first, url))
			continue;
		const char *why = nullptr;
		if (!httpExchange(m_settings.localIP, url, PM::httpGetRequest(url), raw, m_settings.timeoutMs, m_stop, &why))
		{
			note("description %s: %s", entry.first.c_str(), why != nullptr ? why : "failed");
			continue;
		}
		if (!PM::parseHttpResponse(raw, description) || description.status != 200)
		{
			note("description %s: HTTP %d", entry.first.c_str(), description.status);
			continue;
		}
		UpnpDevice device;
		device.location = entry.first;
		device.host = entry.second;
		device.description = description.body;
		const std::vector<PM::IgdControl> services = PM::igdFindControls(device.description, device.location);
		PM::IgdControl firewall;
		const bool hasFirewall = PM::igdFindFirewallControl(device.description, device.location, firewall);
		note("description %s: %u connection services%s", entry.first.c_str(), static_cast<unsigned>(services.size()),
			hasFirewall ? ", IPv6 firewall service" : ", no IPv6 firewall service");
		for (const PM::IgdControl &control : services)
			note("  service %s at %s", control.serviceType.c_str(), control.controlUrl.c_str());
		m_upnpDevices.push_back(device);
	}
	if (answers > 0)
		m_searchOutcome.store(SEARCH_ANSWERED);
	else if (!stopping())
		m_searchOutcome.store(askGatewayDirectly() ? SEARCH_FILTERED : SEARCH_SILENT);
	return !m_upnpDevices.empty();
}

// The search stayed silent: either the router has UPnP off, or something on this computer dropped
// its answers (they come back as unsolicited UDP). A plain outgoing TCP connection to where routers
// keep their description tells the two apart, and finds the router in the second case.
bool PortMapper::askGatewayDirectly()
{
	if (m_gateway == 0)
		m_gateway = m_settings.gateway != 0 ? m_settings.gateway : defaultGateway(m_settings.localIP);
	if (m_gateway == 0)
	{
		note("UPnP: no default gateway to ask directly");
		return false;
	}
	std::vector<std::pair<std::uint16_t, std::string>> candidates = m_settings.directDescriptions;
	if (candidates.empty())
	{
		for (int i = 0; i < PM::KNOWN_DESCRIPTION_COUNT; ++i)
			candidates.push_back(std::make_pair(PM::knownDescription(i).port, std::string(PM::knownDescription(i).path)));
	}
	note("UPnP: asking the gateway %s directly where routers keep their description", PM::ipv4Text(m_gateway).c_str());
	const std::uint32_t probeMs = m_settings.timeoutMs < 600 ? m_settings.timeoutMs : 600;
	for (const auto &candidate : candidates)
	{
		if (stopping())
			return false;
		const std::string location = "http://" + PM::ipv4Text(m_gateway) + ":" + std::to_string(candidate.first) + candidate.second;
		PM::HttpUrl url;
		std::string raw;
		PM::HttpResponse description;
		if (!PM::parseHttpUrl(location, url) || !httpExchange(m_settings.localIP, url, PM::httpGetRequest(url), raw, probeMs, m_stop)
			|| !PM::parseHttpResponse(raw, description) || description.status != 200)
		{
			continue;
		}
		UpnpDevice device;
		device.location = location;
		device.host = m_gateway;
		device.description = description.body;
		PM::IgdControl firewall;
		const std::vector<PM::IgdControl> services = PM::igdFindControls(device.description, device.location);
		if (services.empty() && !PM::igdFindFirewallControl(device.description, device.location, firewall))
			continue;
		note("  %s answers: UPnP is on in the router, but its search answers did not arrive -- most likely a firewall on this computer"
			" drops them", location.c_str());
		for (const PM::IgdControl &control : services)
			note("  service %s at %s", control.serviceType.c_str(), control.controlUrl.c_str());
		m_upnpDevices.push_back(device);
		return true;
	}
	note("  no answer at the usual addresses either: UPnP is probably off in the router (or it keeps its description elsewhere)");
	return false;
}

bool PortMapper::mapUpnp(std::uint32_t lifetime)
{
	if (!m_upnpControlUrl.empty())
		return mapUpnpService(lifetime);	// renewal: the service that worked
	if (!discoverUpnp())
		return false;

	// every connection service of every gateway, until one maps the ports
	std::vector<std::string> tried;
	for (const UpnpDevice &device : m_upnpDevices)
	{
		for (const PM::IgdControl &control : PM::igdFindControls(device.description, device.location))
		{
			const std::string key = control.serviceType + " " + control.controlUrl;
			bool seen = false;
			for (const std::string &t : tried)
				seen = seen || t == key;
			if (seen || stopping() || !isOnHost(control.controlUrl, device.host))
				continue;
			tried.push_back(key);
			m_upnpServiceType = control.serviceType;
			m_upnpControlUrl = control.controlUrl;
			m_upnpPermanent = false;
			note("UPnP: trying %s", m_upnpControlUrl.c_str());
			if (mapUpnpService(lifetime))
				return true;
		}
	}
	m_upnpServiceType.clear();
	m_upnpControlUrl.clear();
	return false;
}

bool PortMapper::mapUpnpService(std::uint32_t lifetime)
{
	PM::SoapResult result;
	if (soapCall(m_upnpControlUrl, m_upnpServiceType, "GetExternalIPAddress", PM::soapGetExternalIPAddress(m_upnpServiceType),
			m_settings.timeoutMs, m_stop, result) && result.ok && result.externalIP != 0)
	{
		m_externalIP.store(result.externalIP);
		note("  external address %s", PM::ipv4Text(result.externalIP).c_str());
	}

	const std::string client = PM::ipv4Text(m_settings.localIP);
	for (int i = 0; i < m_settings.portCount; ++i)
	{
		const std::uint16_t port = m_settings.ports[i];
		bool mapped = false;
		bool answered = false;
		for (int attempt = 0; attempt < 2 && !mapped && !stopping(); ++attempt)
		{
			const std::uint32_t lease = m_upnpPermanent ? 0 : lifetime;
			answered = soapCall(m_upnpControlUrl, m_upnpServiceType, "AddPortMapping",
				PM::soapAddPortMapping(m_upnpServiceType, port, port, client, MAPPING_DESCRIPTION, lease), m_settings.timeoutMs, m_stop, result);
			mapped = answered && result.ok;
			if (!mapped && result.errorCode == PM::UPNP_ONLY_PERMANENT_LEASES && !m_upnpPermanent)
				m_upnpPermanent = true;	// try again without an end; stop() removes it
			else
				break;
		}
		if (!mapped)
		{
			note("  port %u not forwarded (UPnP error %d)", port, result.errorCode);
			if (answered)
				refused(result.errorCode);
			std::string raw;
			for (int k = 0; k < i; ++k)
				upnpRequest("DeletePortMapping", PM::soapDeletePortMapping(m_upnpServiceType, m_settings.ports[k]), raw);
			return false;
		}
	}
	return true;
}

// --- IPv6 pinholes ---------------------------------------------------------------------------

bool PortMapper::openPinholes(std::uint32_t lifetime)
{
	removePinholes(m_settings.timeoutMs);
	if (m_firewallControlUrl.empty() && discoverUpnp())
	{
		for (const UpnpDevice &device : m_upnpDevices)
		{
			PM::IgdControl control;
			if (PM::igdFindFirewallControl(device.description, device.location, control) && isOnHost(control.controlUrl, device.host))
			{
				m_firewallServiceType = control.serviceType;
				m_firewallControlUrl = control.controlUrl;
				break;
			}
		}
	}
	if (m_firewallControlUrl.empty())
	{
		note("IPv6: no router offers pinholes over UPnP");
		setPinholeState(PINHOLE_NO_SERVICE);
		return false;
	}

	PM::SoapResult status;
	if (soapCall(m_firewallControlUrl, m_firewallServiceType, "GetFirewallStatus", PM::soapGetFirewallStatus(m_firewallServiceType),
		m_settings.timeoutMs, m_stop, status) && status.ok)
	{
		note("IPv6 firewall: %s, incoming pinholes %s", status.firewallEnabled == 0 ? "off" : "on",
			status.inboundPinholeAllowed == 0 ? "not allowed" : "allowed");
		if (status.firewallEnabled == 0)
		{
			setPinholeState(PINHOLE_NOT_NEEDED);
			return false;
		}
		if (status.inboundPinholeAllowed == 0)
		{
			setPinholeState(PINHOLE_REFUSED, PM::UPNP_INBOUND_PINHOLE_NOT_ALLOWED);
			return false;
		}
	}

	for (int i = 0; i < m_settings.portCount && !stopping(); ++i)
	{
		PM::SoapResult result;
		const bool added = soapCall(m_firewallControlUrl, m_firewallServiceType, "AddPinhole",
			PM::soapAddPinhole(m_firewallServiceType, m_settings.localIPv6, m_settings.ports[i], lifetime),
			m_settings.timeoutMs, m_stop, result) && result.ok && result.uniqueId >= 0;
		if (!added)
		{
			note("IPv6: no pinhole for port %u (UPnP error %d)", m_settings.ports[i], result.errorCode);
			removePinholes(m_settings.timeoutMs);
			if (result.errorCode == PM::UPNP_FIREWALL_DISABLED)
				setPinholeState(PINHOLE_NOT_NEEDED);
			else
				setPinholeState(PINHOLE_REFUSED, result.errorCode);
			return false;
		}
		m_pinholeIds[m_pinholeCount++] = result.uniqueId;
	}
	if (stopping())
		return false;
	note("IPv6: pinholes open for %s", m_settings.localIPv6.c_str());
	setPinholeState(PINHOLE_OPEN);
	return true;
}

bool PortMapper::renewPinholes(std::uint32_t lifetime)
{
	if (m_pinholeCount == 0)
		return false;
	for (int i = 0; i < m_pinholeCount; ++i)
	{
		PM::SoapResult result;
		if (!soapCall(m_firewallControlUrl, m_firewallServiceType, "UpdatePinhole",
				PM::soapUpdatePinhole(m_firewallServiceType, static_cast<std::uint16_t>(m_pinholeIds[i]), lifetime),
				m_settings.timeoutMs, m_stop, result) || !result.ok)
		{
			return false;
		}
	}
	if (m_pinholeState.load() != PINHOLE_OPEN)
		setPinholeState(PINHOLE_OPEN);
	return true;
}

void PortMapper::removePinholes(std::uint32_t timeoutMs)
{
	const std::atomic<bool> never{false};	// also while stop() is under way
	for (int i = 0; i < m_pinholeCount; ++i)
	{
		PM::SoapResult result;
		soapCall(m_firewallControlUrl, m_firewallServiceType, "DeletePinhole",
			PM::soapDeletePinhole(m_firewallServiceType, static_cast<std::uint16_t>(m_pinholeIds[i])), timeoutMs, never, result);
	}
	m_pinholeCount = 0;
}

// --- renewal and removal ----------------------------------------------------------------------

bool PortMapper::renew()
{
	switch (m_method.load())
	{
		case METHOD_NATPMP: return mapNatPmp(m_settings.lifetimeSeconds);
		case METHOD_UPNP: return mapUpnp(m_settings.lifetimeSeconds);
		default: return false;
	}
}

void PortMapper::removeMappings()
{
	const std::uint32_t timeout = m_settings.timeoutMs < REMOVE_TIMEOUT_MS ? m_settings.timeoutMs : REMOVE_TIMEOUT_MS;
	if (m_method.load() == METHOD_NATPMP)
	{
		Socket sock;
		if (!openSocket(sock, SOCK_DGRAM, m_settings.localIP))
			return;
		const std::atomic<bool> never{false};	// stop() is under way; removal runs to its end
		for (int i = 0; i < m_settings.portCount; ++i)
		{
			std::uint8_t request[PM::NATPMP_MAP_REQUEST_SIZE];
			PM::NatPmpResponse r;
			PM::natpmpMapUdpRequest(m_settings.ports[i], 0, 0, request);
			natpmpExchange(sock.s, m_gateway, m_settings.natpmpPort, request, PM::NATPMP_MAP_REQUEST_SIZE, 129,
				m_settings.ports[i], r, timeout / 2, never);
		}
	}
	else if (m_method.load() == METHOD_UPNP)
	{
		const std::atomic<bool> never{false};
		PM::HttpUrl url;
		if (!PM::parseHttpUrl(m_upnpControlUrl, url))
			return;
		for (int i = 0; i < m_settings.portCount; ++i)
		{
			std::string raw;
			const std::string request = PM::httpSoapRequest(url, m_upnpServiceType, "DeletePortMapping",
				PM::soapDeletePortMapping(m_upnpServiceType, m_settings.ports[i]));
			httpExchange(m_settings.localIP, url, request, raw, timeout, never);
		}
	}
	DEBUG_LOG(("PortMapper: mappings removed\n"));
}
