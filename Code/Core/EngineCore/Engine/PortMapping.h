/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// PortMapping.h -- the messages that ask a home router to forward a port to this computer.
//
// Two protocols are common on home routers:
//  - NAT-PMP (RFC 6886): a few bytes over UDP to port 5351 of the gateway.
//  - UPnP IGD: a search over SSDP (UDP multicast 239.255.255.250:1900), the router answers with the
//    address of its device description (XML over HTTP), which names the control address of its
//    WANIPConnection or WANPPPConnection service; mappings are SOAP calls to that address.
//
// This file builds and parses those messages; sockets, timeouts and the order of attempts are the
// caller's (GameNetwork/PortMapper). Everything here takes untrusted input from the network: sizes
// are checked, texts are bounded.
//
// No engine header, no sockets: tested by EngineTests.

#pragma once

#ifndef ENGINE_PORTMAPPING_H
#define ENGINE_PORTMAPPING_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Engine
{
namespace PortMapping
{

// --- NAT-PMP ----------------------------------------------------------------------------------

constexpr std::uint16_t NATPMP_PORT = 5351;
constexpr std::size_t NATPMP_ADDRESS_REQUEST_SIZE = 2;
constexpr std::size_t NATPMP_MAP_REQUEST_SIZE = 12;

/// Asks for the external address.
std::size_t natpmpAddressRequest(std::uint8_t out[NATPMP_ADDRESS_REQUEST_SIZE]);

/// Asks for a UDP mapping; lifetime 0 removes it.
std::size_t natpmpMapUdpRequest(std::uint16_t internalPort, std::uint16_t externalPort, std::uint32_t lifetimeSeconds,
	std::uint8_t out[NATPMP_MAP_REQUEST_SIZE]);

struct NatPmpResponse
{
	std::uint8_t op = 0;				///< request op + 128
	std::uint16_t result = 0;			///< 0 = success
	std::uint32_t epoch = 0;			///< seconds since the gateway started its mapping table
	std::uint32_t externalIP = 0;		///< address response, host byte order
	std::uint16_t internalPort = 0;		///< mapping response
	std::uint16_t externalPort = 0;
	std::uint32_t lifetime = 0;
};

/// False for anything that is no NAT-PMP response of version 0 in the size of its kind.
bool natpmpParse(const void *data, std::size_t size, NatPmpResponse &out);

// --- SSDP -------------------------------------------------------------------------------------

constexpr std::uint16_t SSDP_PORT = 1900;
constexpr std::uint32_t SSDP_MULTICAST = 0xEFFFFFFAu;	///< 239.255.255.250

/// What a search asks for: the gateway device in both versions and its two connection services.
/// Some routers only answer a search for exactly their own type.
constexpr int SSDP_SEARCH_TARGET_COUNT = 4;
const char *ssdpSearchTarget(int index);

/// The search for `target` (an Internet gateway device if none is given).
std::string ssdpSearchRequest(const char *target = nullptr);

/// The LOCATION of a search response ("HTTP/1.1 200" with a LOCATION header). False otherwise.
bool ssdpLocation(const char *response, std::size_t size, std::string &location);

/// Any header of a search response (`name` with its colon, case ignored), trimmed and bounded. False if
/// the response has none or is no "200".
bool ssdpHeader(const char *response, std::size_t size, const char *name, std::string &value);

/// Where common routers keep their device description, for asking the gateway directly when the search
/// stays silent: a router that answers there has UPnP on, so the search answers were dropped on this
/// computer (a firewall). Port and path on the gateway.
struct KnownDescription
{
	std::uint16_t port;
	const char *path;
};
constexpr int KNOWN_DESCRIPTION_COUNT = 10;
KnownDescription knownDescription(int index);

// --- HTTP -------------------------------------------------------------------------------------

struct HttpUrl
{
	std::string host;			///< as written: IPv4 literal or name
	std::uint16_t port = 80;
	std::string path = "/";		///< with query, never empty
};

/// Only "http://host[:port][/path]"; false for other schemes, user info, bad ports, IPv6 literals.
bool parseHttpUrl(const std::string &url, HttpUrl &out);

/// A reference inside a device description, relative to `base` (the URL of the description or
/// its URLBase): absolute "http://...", "/path" or "path". Empty if it cannot be resolved.
std::string resolveUrl(const std::string &base, const std::string &reference);

/// Upper bound for responses the caller reads; a router has no reason to send more.
constexpr std::size_t MAX_HTTP_RESPONSE = 64 * 1024;

std::string httpGetRequest(const HttpUrl &url);
std::string httpSoapRequest(const HttpUrl &url, const std::string &serviceType, const std::string &action,
	const std::string &body);

struct HttpResponse
{
	int status = 0;
	std::string body;
	bool framed = false;	///< the length was known (Content-Length or chunked): the response is complete
};

/// A complete response as read up to the closed connection: status line, headers, body by
/// Content-Length, chunked or up to the end. False if it is no HTTP/1.x response or cut short.
/// With `framed` set the response is complete before the connection closes: some routers keep it
/// open despite "Connection: close", so a reader stops there instead of waiting for the end.
bool parseHttpResponse(const std::string &raw, HttpResponse &out);

// --- IGD --------------------------------------------------------------------------------------

struct IgdControl
{
	std::string serviceType;	///< urn:schemas-upnp-org:service:WANIPConnection:1 or similar
	std::string controlUrl;		///< absolute
};

/// All connection services in a device description fetched from `location`, the preferred first:
/// WANIPConnection version 2, then 1, then WANPPPConnection 1. A gateway may list one that is not in
/// use (the PPP service of a cable connection), so callers try them in turn.
std::vector<IgdControl> igdFindControls(const std::string &xml, const std::string &location);

/// The first of igdFindControls. False if the description has none.
bool igdFindControl(const std::string &xml, const std::string &location, IgdControl &out);

/// The IPv6 firewall service of an IGD version 2 (WANIPv6FirewallControl:1): it opens pinholes,
/// so that the internet reaches an IPv6 address behind the router. False if the description has none.
bool igdFindFirewallControl(const std::string &xml, const std::string &location, IgdControl &out);

// --- SOAP -------------------------------------------------------------------------------------

std::string soapGetExternalIPAddress(const std::string &serviceType);
std::string soapAddPortMapping(const std::string &serviceType, std::uint16_t externalPort, std::uint16_t internalPort,
	const std::string &internalClient, const std::string &description, std::uint32_t leaseSeconds);
std::string soapDeletePortMapping(const std::string &serviceType, std::uint16_t externalPort);

// IPv6 pinholes: from any remote address and port to `internalClient` (IPv6 text), UDP. The lease is
// 1 to 86400 seconds (no pinhole without end); the router names each pinhole by a UniqueID.
constexpr std::uint32_t PINHOLE_MAX_LEASE = 86400;
std::string soapGetFirewallStatus(const std::string &serviceType);
std::string soapAddPinhole(const std::string &serviceType, const std::string &internalClient, std::uint16_t internalPort,
	std::uint32_t leaseSeconds);
std::string soapUpdatePinhole(const std::string &serviceType, std::uint16_t uniqueId, std::uint32_t leaseSeconds);
std::string soapDeletePinhole(const std::string &serviceType, std::uint16_t uniqueId);

/// UPnP error codes a caller reacts to.
constexpr int UPNP_ACTION_NOT_AUTHORIZED = 606;	///< e.g. a FRITZ!Box that does not let this computer open ports
constexpr int UPNP_CONFLICT_IN_MAPPING = 718;
constexpr int UPNP_ONLY_PERMANENT_LEASES = 725;
constexpr int UPNP_FIREWALL_DISABLED = 702;			///< no pinhole needed: the IPv6 firewall lets everything in
constexpr int UPNP_INBOUND_PINHOLE_NOT_ALLOWED = 704;

struct SoapResult
{
	bool ok = false;			///< status 200
	int errorCode = 0;			///< UPnP errorCode of a fault, 0 if none given
	std::uint32_t externalIP = 0;	///< NewExternalIPAddress, host byte order, 0 if absent or invalid
	int uniqueId = -1;				///< UniqueID of AddPinhole, -1 if absent or invalid
	int firewallEnabled = -1;		///< GetFirewallStatus: 0 or 1, -1 if absent
	int inboundPinholeAllowed = -1;	///< GetFirewallStatus: 0 or 1, -1 if absent
};

bool parseSoapResponse(const HttpResponse &response, SoapResult &out);

// --- helpers ----------------------------------------------------------------------------------

/// "a.b.c.d" of an address in host byte order.
std::string ipv4Text(std::uint32_t ip);

/// True for addresses behind a NAT of the own network: 10/8, 172.16/12, 192.168/16, 100.64/10
/// (carrier-grade NAT) and 169.254/16.
bool isPrivateIPv4(std::uint32_t ip);

/// True for an external address of a router that the internet can reach. False for private and
/// carrier-grade NAT addresses, 192.0.0.0/24 (DS-Lite: the router has no public IPv4 address of its
/// own), loopback and 0: forwarding a port there leads nowhere.
bool isPublicIPv4(std::uint32_t ip);

} // namespace PortMapping
} // namespace Engine

#endif // ENGINE_PORTMAPPING_H
