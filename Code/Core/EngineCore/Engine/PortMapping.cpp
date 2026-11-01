/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// PortMapping.cpp -- see PortMapping.h.

#include "Engine/PortMapping.h"

#include <cstring>
#include <format>

namespace Engine
{
namespace PortMapping
{

namespace
{
	constexpr std::size_t MAX_LOCATION = 512;
	constexpr std::size_t MAX_FIELD = 256;

	void putBE16(std::uint8_t *p, std::uint16_t v)
	{
		p[0] = static_cast<std::uint8_t>(v >> 8);
		p[1] = static_cast<std::uint8_t>(v);
	}

	void putBE32(std::uint8_t *p, std::uint32_t v)
	{
		for (int i = 0; i < 4; ++i)
			p[i] = static_cast<std::uint8_t>(v >> (24 - 8 * i));
	}

	std::uint16_t getBE16(const std::uint8_t *p)
	{
		return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
	}

	std::uint32_t getBE32(const std::uint8_t *p)
	{
		return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
			| (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
	}

	char lower(char c)
	{
		return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
	}

	bool startsWithNoCase(const std::string &text, std::size_t at, const char *prefix)
	{
		for (std::size_t i = 0; prefix[i] != 0; ++i)
		{
			if (at + i >= text.size() || lower(text[at + i]) != lower(prefix[i]))
				return false;
		}
		return true;
	}

	std::string trim(const std::string &text)
	{
		std::size_t begin = 0;
		std::size_t end = text.size();
		while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' || text[begin] == '\n'))
			++begin;
		while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n'))
			--end;
		return text.substr(begin, end - begin);
	}

	/// Characters that may go into a request line or header unchanged.
	bool isPlainVisible(const std::string &text)
	{
		for (char c : text)
		{
			const unsigned char u = static_cast<unsigned char>(c);
			if (u < 0x21 || u > 0x7E)
				return false;
		}
		return true;
	}

	bool parseDecimal(const std::string &text, std::uint32_t maxValue, std::uint32_t &out)
	{
		if (text.empty() || text.size() > 10)
			return false;
		std::uint64_t value = 0;
		for (char c : text)
		{
			if (c < '0' || c > '9')
				return false;
			value = value * 10 + static_cast<std::uint64_t>(c - '0');
		}
		if (value > maxValue)
			return false;
		out = static_cast<std::uint32_t>(value);
		return true;
	}

	bool parseIPv4(const std::string &text, std::uint32_t &out)
	{
		std::uint32_t ip = 0;
		std::size_t pos = 0;
		for (int part = 0; part < 4; ++part)
		{
			const std::size_t end = part < 3 ? text.find('.', pos) : text.size();
			if (end == std::string::npos || end == pos || end - pos > 3)
				return false;
			std::uint32_t value;
			if (!parseDecimal(text.substr(pos, end - pos), 255, value))
				return false;
			ip = (ip << 8) | value;
			pos = end + 1;
		}
		out = ip;
		return true;
	}

	/// The next element <tag ...>...</tag> within [from, to): its content [contentStart, contentEnd)
	/// and the position after its end tag.
	bool findElement(const std::string &xml, std::size_t from, std::size_t to, const char *tag,
		std::size_t &contentStart, std::size_t &contentEnd, std::size_t &after)
	{
		const std::string open = std::string("<") + tag;
		const std::string close = std::string("</") + tag + ">";
		std::size_t at = from;
		while (true)
		{
			at = xml.find(open, at);
			if (at == std::string::npos || at >= to)
				return false;
			const std::size_t next = at + open.size();
			if (next < xml.size() && (xml[next] == '>' || xml[next] == ' ' || xml[next] == '\t' || xml[next] == '\r' || xml[next] == '\n'))
				break;
			at = next;	// another tag with the same beginning (<serviceList>, <serviceType>)
		}
		const std::size_t start = xml.find('>', at);
		if (start == std::string::npos || start >= to)
			return false;
		const std::size_t end = xml.find(close, start);
		if (end == std::string::npos || end + close.size() > to)
			return false;
		contentStart = start + 1;
		contentEnd = end;
		after = end + close.size();
		return true;
	}

	/// The trimmed text of the next element <tag> within [from, to); bounded in length.
	bool elementText(const std::string &xml, std::size_t from, std::size_t to, const char *tag, std::string &out)
	{
		std::size_t start, end, after;
		if (!findElement(xml, from, to, tag, start, end, after) || end - start > MAX_LOCATION)
			return false;
		out = trim(xml.substr(start, end - start));
		return true;
	}

	std::string xmlEscape(const std::string &text)
	{
		std::string out;
		for (char c : text)
		{
			switch (c)
			{
				case '&': out += "&amp;"; break;
				case '<': out += "&lt;"; break;
				case '>': out += "&gt;"; break;
				case '"': out += "&quot;"; break;
				default: out += c; break;
			}
		}
		return out;
	}

	std::string authority(const HttpUrl &url)
	{
		return std::format("{}:{}", url.host, url.port);
	}

	std::string soapEnvelope(const std::string &serviceType, const std::string &action, const std::string &arguments)
	{
		return "<?xml version=\"1.0\"?>\r\n"
			"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
			"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
			"<s:Body><u:" + action + " xmlns:u=\"" + serviceType + "\">" + arguments + "</u:" + action + ">"
			"</s:Body></s:Envelope>\r\n";
	}

	std::string argument(const char *name, const std::string &value)
	{
		return std::string("<") + name + ">" + xmlEscape(value) + "</" + name + ">";
	}

	/// A UPnP boolean ("1", "0", "true", "false", "yes", "no"): 1 or 0, -1 for anything else.
	int upnpBoolean(const std::string &text)
	{
		std::string t = trim(text);
		for (char &c : t)
			c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
		if (t == "1" || t == "true" || t == "yes")
			return 1;
		if (t == "0" || t == "false" || t == "no")
			return 0;
		return -1;
	}

	std::string number(std::uint32_t value)
	{
		return std::to_string(value);
	}
}

// --- NAT-PMP ----------------------------------------------------------------------------------

std::size_t natpmpAddressRequest(std::uint8_t out[NATPMP_ADDRESS_REQUEST_SIZE])
{
	out[0] = 0;	// version
	out[1] = 0;	// op: external address
	return NATPMP_ADDRESS_REQUEST_SIZE;
}

std::size_t natpmpMapUdpRequest(std::uint16_t internalPort, std::uint16_t externalPort, std::uint32_t lifetimeSeconds,
	std::uint8_t out[NATPMP_MAP_REQUEST_SIZE])
{
	out[0] = 0;
	out[1] = 1;	// op: map UDP
	out[2] = 0;
	out[3] = 0;
	putBE16(out + 4, internalPort);
	putBE16(out + 6, externalPort);
	putBE32(out + 8, lifetimeSeconds);
	return NATPMP_MAP_REQUEST_SIZE;
}

bool natpmpParse(const void *data, std::size_t size, NatPmpResponse &out)
{
	if (data == nullptr || size < 8)
		return false;
	const std::uint8_t *p = static_cast<const std::uint8_t *>(data);
	if (p[0] != 0 || p[1] < 128)
		return false;
	NatPmpResponse r;
	r.op = p[1];
	r.result = getBE16(p + 2);
	r.epoch = getBE32(p + 4);
	if (r.op == 128)
	{
		if (size < 12)
		{
			// an error answer may stop after the epoch
			if (r.result == 0)
				return false;
			out = r;
			return true;
		}
		r.externalIP = getBE32(p + 8);
	}
	else if (r.op == 129 || r.op == 130)
	{
		if (size < 16)
		{
			// an error answer may stop after the epoch
			if (r.result == 0)
				return false;
			out = r;
			return true;
		}
		r.internalPort = getBE16(p + 8);
		r.externalPort = getBE16(p + 10);
		r.lifetime = getBE32(p + 12);
	}
	else
	{
		return false;
	}
	out = r;
	return true;
}

// --- SSDP -------------------------------------------------------------------------------------

const char *ssdpSearchTarget(int index)
{
	static const char *const TARGETS[SSDP_SEARCH_TARGET_COUNT] = {
		"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
		"urn:schemas-upnp-org:device:InternetGatewayDevice:2",
		"urn:schemas-upnp-org:service:WANIPConnection:1",
		"urn:schemas-upnp-org:service:WANPPPConnection:1",
	};
	return index >= 0 && index < SSDP_SEARCH_TARGET_COUNT ? TARGETS[index] : TARGETS[0];
}

std::string ssdpSearchRequest(const char *target)
{
	return std::string("M-SEARCH * HTTP/1.1\r\n"
		"HOST: 239.255.255.250:1900\r\n"
		"MAN: \"ssdp:discover\"\r\n"
		"MX: 1\r\n"
		"ST: ") + (target != nullptr ? target : ssdpSearchTarget(0)) + "\r\n"
		"\r\n";
}

bool ssdpHeader(const char *response, std::size_t size, const char *name, std::string &value)
{
	if (response == nullptr || name == nullptr)
		return false;
	const std::string text(response, size);
	if (!(startsWithNoCase(text, 0, "HTTP/1.1 200") || startsWithNoCase(text, 0, "HTTP/1.0 200")))
		return false;
	const std::size_t nameLength = std::strlen(name);
	std::size_t line = text.find('\n');
	while (line != std::string::npos && line + 1 < text.size())
	{
		const std::size_t start = line + 1;
		std::size_t end = text.find('\n', start);
		if (end == std::string::npos)
			end = text.size();
		if (startsWithNoCase(text, start, name))
		{
			const std::string found = trim(text.substr(start + nameLength, end - start - nameLength));
			if (found.empty() || found.size() > MAX_LOCATION)
				return false;
			value = found;
			return true;
		}
		line = end < text.size() ? end : std::string::npos;
	}
	return false;
}

KnownDescription knownDescription(int index)
{
	static const KnownDescription KNOWN[KNOWN_DESCRIPTION_COUNT] = {
		{ 49000, "/igddesc.xml" },		// AVM FRITZ!Box
		{ 49000, "/igd2desc.xml" },		// AVM FRITZ!Box, IGD version 2
		{ 5000, "/rootDesc.xml" },		// miniupnpd (OpenWrt, ASUS and many others)
		{ 5555, "/rootDesc.xml" },		// miniupnpd on another port
		{ 1900, "/igd.xml" },			// TP-Link
		{ 1900, "/gatedesc.xml" },		// Linksys, Netgear
		{ 1900, "/rootDesc.xml" },
		{ 52869, "/picsdesc.xml" },		// Realtek-based routers (D-Link and others)
		{ 2048, "/etc/linuxigd/gatedesc.xml" },	// linux-igd
		{ 80, "/igd.xml" },
	};
	return index >= 0 && index < KNOWN_DESCRIPTION_COUNT ? KNOWN[index] : KNOWN[0];
}

bool ssdpLocation(const char *response, std::size_t size, std::string &location)
{
	return ssdpHeader(response, size, "LOCATION:", location);
}

// --- HTTP -------------------------------------------------------------------------------------

bool parseHttpUrl(const std::string &url, HttpUrl &out)
{
	if (url.size() > MAX_LOCATION || !startsWithNoCase(url, 0, "http://") || !isPlainVisible(url))
		return false;
	const std::size_t hostStart = 7;
	std::size_t pathStart = url.find('/', hostStart);
	if (pathStart == std::string::npos)
		pathStart = url.size();
	const std::string hostPort = url.substr(hostStart, pathStart - hostStart);
	if (hostPort.empty() || hostPort.find('@') != std::string::npos || hostPort.find('[') != std::string::npos)
		return false;
	HttpUrl result;
	const std::size_t colon = hostPort.find(':');
	result.host = hostPort.substr(0, colon);
	if (colon != std::string::npos)
	{
		std::uint32_t port;
		if (!parseDecimal(hostPort.substr(colon + 1), 65535, port) || port == 0)
			return false;
		result.port = static_cast<std::uint16_t>(port);
	}
	if (result.host.empty() || result.host.size() > 253)
		return false;
	for (char c : result.host)
	{
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
		if (!ok)
			return false;
	}
	result.path = pathStart < url.size() ? url.substr(pathStart) : "/";
	out = result;
	return true;
}

std::string resolveUrl(const std::string &base, const std::string &reference)
{
	const std::string ref = trim(reference);
	HttpUrl url;
	if (startsWithNoCase(ref, 0, "http://"))
		return parseHttpUrl(ref, url) ? ref : std::string();
	if (ref.find("://") != std::string::npos || !parseHttpUrl(base, url))
		return std::string();
	std::string path;
	if (!ref.empty() && ref[0] == '/')
	{
		path = ref;
	}
	else
	{
		std::string dir = url.path;
		const std::size_t query = dir.find('?');
		if (query != std::string::npos)
			dir.erase(query);
		dir.erase(dir.rfind('/') + 1);
		path = dir + ref;
	}
	const std::string result = "http://" + authority(url) + path;
	return parseHttpUrl(result, url) ? result : std::string();
}

std::string httpGetRequest(const HttpUrl &url)
{
	return "GET " + url.path + " HTTP/1.1\r\n"
		"Host: " + authority(url) + "\r\n"
		"Connection: close\r\n"
		"\r\n";
}

std::string httpSoapRequest(const HttpUrl &url, const std::string &serviceType, const std::string &action,
	const std::string &body)
{
	return "POST " + url.path + " HTTP/1.1\r\n"
		"Host: " + authority(url) + "\r\n"
		"Content-Type: text/xml; charset=\"utf-8\"\r\n"
		"SOAPAction: \"" + serviceType + "#" + action + "\"\r\n"
		"Content-Length: " + number(static_cast<std::uint32_t>(body.size())) + "\r\n"
		"Connection: close\r\n"
		"\r\n" + body;
}

bool parseHttpResponse(const std::string &raw, HttpResponse &out)
{
	if (raw.size() > MAX_HTTP_RESPONSE || !(startsWithNoCase(raw, 0, "HTTP/1.1 ") || startsWithNoCase(raw, 0, "HTTP/1.0 ")))
		return false;
	std::uint32_t status = 0;
	if (raw.size() < 12 || !parseDecimal(raw.substr(9, 3), 999, status))
		return false;
	std::size_t headerEnd = raw.find("\r\n\r\n");
	std::size_t bodyStart = headerEnd + 4;
	if (headerEnd == std::string::npos)
	{
		headerEnd = raw.find("\n\n");
		if (headerEnd == std::string::npos)
			return false;
		bodyStart = headerEnd + 2;
	}
	bool chunked = false;
	bool hasLength = false;
	std::uint32_t length = 0;
	std::size_t line = raw.find('\n');
	while (line != std::string::npos && line < headerEnd)
	{
		const std::size_t start = line + 1;
		std::size_t end = raw.find('\n', start);
		if (end == std::string::npos || end > headerEnd + 2)
			end = headerEnd;
		if (startsWithNoCase(raw, start, "Content-Length:"))
		{
			if (!parseDecimal(trim(raw.substr(start + 15, end - start - 15)), static_cast<std::uint32_t>(MAX_HTTP_RESPONSE), length))
				return false;
			hasLength = true;
		}
		else if (startsWithNoCase(raw, start, "Transfer-Encoding:"))
		{
			const std::string value = trim(raw.substr(start + 18, end - start - 18));
			chunked = startsWithNoCase(value, 0, "chunked");
		}
		line = raw.find('\n', start);
	}

	HttpResponse result;
	result.status = static_cast<int>(status);
	if (chunked)
	{
		std::size_t pos = bodyStart;
		while (true)
		{
			const std::size_t sizeEnd = raw.find('\n', pos);
			if (sizeEnd == std::string::npos)
				return false;
			std::string sizeText = trim(raw.substr(pos, sizeEnd - pos));
			const std::size_t extension = sizeText.find(';');
			if (extension != std::string::npos)
				sizeText = trim(sizeText.substr(0, extension));
			if (sizeText.empty() || sizeText.size() > 6)
				return false;
			std::size_t chunk = 0;
			for (char c : sizeText)
			{
				const char l = lower(c);
				int digit;
				if (l >= '0' && l <= '9')
					digit = l - '0';
				else if (l >= 'a' && l <= 'f')
					digit = l - 'a' + 10;
				else
					return false;
				chunk = chunk * 16 + static_cast<std::size_t>(digit);
			}
			pos = sizeEnd + 1;
			if (chunk == 0)
				break;
			if (chunk > raw.size() - pos)
				return false;
			result.body.append(raw, pos, chunk);
			pos += chunk;
			if (pos < raw.size() && raw[pos] == '\r')
				++pos;
			if (pos < raw.size() && raw[pos] == '\n')
				++pos;
		}
	}
	else if (hasLength)
	{
		if (length > raw.size() - bodyStart)
			return false;
		result.body = raw.substr(bodyStart, length);
	}
	else
	{
		result.body = raw.substr(bodyStart);
	}
	result.framed = chunked || hasLength;
	out = result;
	return true;
}

// --- IGD --------------------------------------------------------------------------------------

std::vector<IgdControl> igdFindControls(const std::string &xml, const std::string &location)
{
	std::string base = location;
	std::string urlBase;
	if (elementText(xml, 0, xml.size(), "URLBase", urlBase) && !urlBase.empty())
		base = urlBase;

	static const char *const PREFERRED[] = {
		"urn:schemas-upnp-org:service:WANIPConnection:2",
		"urn:schemas-upnp-org:service:WANIPConnection:1",
		"urn:schemas-upnp-org:service:WANPPPConnection:1",
	};
	constexpr int RANKS = static_cast<int>(sizeof(PREFERRED) / sizeof(PREFERRED[0]));
	std::vector<IgdControl> byRank[RANKS];
	std::size_t at = 0;
	std::size_t start, end, after;
	while (findElement(xml, at, xml.size(), "service", start, end, after))
	{
		std::string type;
		std::string control;
		if (elementText(xml, start, end, "serviceType", type) && elementText(xml, start, end, "controlURL", control))
		{
			for (int rank = 0; rank < RANKS; ++rank)
			{
				if (type != PREFERRED[rank])
					continue;
				IgdControl found;
				found.serviceType = type;
				found.controlUrl = resolveUrl(base, control);
				if (!found.controlUrl.empty())
					byRank[rank].push_back(found);
				break;
			}
		}
		at = after;
	}
	std::vector<IgdControl> out;
	for (const std::vector<IgdControl> &rank : byRank)
		out.insert(out.end(), rank.begin(), rank.end());
	return out;
}

bool igdFindControl(const std::string &xml, const std::string &location, IgdControl &out)
{
	const std::vector<IgdControl> found = igdFindControls(xml, location);
	if (found.empty())
		return false;
	out = found.front();
	return true;
}

bool igdFindFirewallControl(const std::string &xml, const std::string &location, IgdControl &out)
{
	std::string base = location;
	std::string urlBase;
	if (elementText(xml, 0, xml.size(), "URLBase", urlBase) && !urlBase.empty())
		base = urlBase;
	std::size_t at = 0;
	std::size_t start, end, after;
	while (findElement(xml, at, xml.size(), "service", start, end, after))
	{
		std::string type;
		std::string control;
		if (elementText(xml, start, end, "serviceType", type) && elementText(xml, start, end, "controlURL", control)
			&& type == "urn:schemas-upnp-org:service:WANIPv6FirewallControl:1")
		{
			const std::string resolved = resolveUrl(base, control);
			if (!resolved.empty())
			{
				out.serviceType = type;
				out.controlUrl = resolved;
				return true;
			}
		}
		at = after;
	}
	return false;
}

// --- SOAP -------------------------------------------------------------------------------------

std::string soapGetExternalIPAddress(const std::string &serviceType)
{
	return soapEnvelope(serviceType, "GetExternalIPAddress", "");
}

std::string soapAddPortMapping(const std::string &serviceType, std::uint16_t externalPort, std::uint16_t internalPort,
	const std::string &internalClient, const std::string &description, std::uint32_t leaseSeconds)
{
	const std::string arguments = argument("NewRemoteHost", "")
		+ argument("NewExternalPort", number(externalPort))
		+ argument("NewProtocol", "UDP")
		+ argument("NewInternalPort", number(internalPort))
		+ argument("NewInternalClient", internalClient)
		+ argument("NewEnabled", "1")
		+ argument("NewPortMappingDescription", description)
		+ argument("NewLeaseDuration", number(leaseSeconds));
	return soapEnvelope(serviceType, "AddPortMapping", arguments);
}

std::string soapDeletePortMapping(const std::string &serviceType, std::uint16_t externalPort)
{
	const std::string arguments = argument("NewRemoteHost", "")
		+ argument("NewExternalPort", number(externalPort))
		+ argument("NewProtocol", "UDP");
	return soapEnvelope(serviceType, "DeletePortMapping", arguments);
}

std::string soapGetFirewallStatus(const std::string &serviceType)
{
	return soapEnvelope(serviceType, "GetFirewallStatus", "");
}

std::string soapAddPinhole(const std::string &serviceType, const std::string &internalClient, std::uint16_t internalPort,
	std::uint32_t leaseSeconds)
{
	if (leaseSeconds == 0 || leaseSeconds > PINHOLE_MAX_LEASE)
		leaseSeconds = PINHOLE_MAX_LEASE;
	const std::string arguments = argument("RemoteHost", "")
		+ argument("RemotePort", "0")
		+ argument("InternalClient", internalClient)
		+ argument("InternalPort", number(internalPort))
		+ argument("Protocol", "17")
		+ argument("LeaseTime", number(leaseSeconds));
	return soapEnvelope(serviceType, "AddPinhole", arguments);
}

std::string soapUpdatePinhole(const std::string &serviceType, std::uint16_t uniqueId, std::uint32_t leaseSeconds)
{
	if (leaseSeconds == 0 || leaseSeconds > PINHOLE_MAX_LEASE)
		leaseSeconds = PINHOLE_MAX_LEASE;
	return soapEnvelope(serviceType, "UpdatePinhole", argument("UniqueID", number(uniqueId)) + argument("NewLeaseTime", number(leaseSeconds)));
}

std::string soapDeletePinhole(const std::string &serviceType, std::uint16_t uniqueId)
{
	return soapEnvelope(serviceType, "DeletePinhole", argument("UniqueID", number(uniqueId)));
}

bool parseSoapResponse(const HttpResponse &response, SoapResult &out)
{
	if (response.status == 0)
		return false;
	SoapResult result;
	result.ok = response.status == 200;
	std::string text;
	std::uint32_t code = 0;
	if (elementText(response.body, 0, response.body.size(), "errorCode", text) && parseDecimal(text, 9999, code))
		result.errorCode = static_cast<int>(code);
	std::uint32_t ip = 0;
	if (elementText(response.body, 0, response.body.size(), "NewExternalIPAddress", text) && parseIPv4(text, ip))
		result.externalIP = ip;
	std::uint32_t value = 0;
	if (elementText(response.body, 0, response.body.size(), "UniqueID", text) && parseDecimal(trim(text), 65535, value))
		result.uniqueId = static_cast<int>(value);
	if (elementText(response.body, 0, response.body.size(), "FirewallEnabled", text))
		result.firewallEnabled = upnpBoolean(text);
	if (elementText(response.body, 0, response.body.size(), "InboundPinholeAllowed", text))
		result.inboundPinholeAllowed = upnpBoolean(text);
	out = result;
	return true;
}

// --- helpers ----------------------------------------------------------------------------------

std::string ipv4Text(std::uint32_t ip)
{
	return std::format("{}.{}.{}.{}", (ip >> 24) & 255u, (ip >> 16) & 255u, (ip >> 8) & 255u, ip & 255u);
}

bool isPrivateIPv4(std::uint32_t ip)
{
	return (ip >> 24) == 10
		|| (ip >> 20) == 0xAC1
		|| (ip >> 16) == 0xC0A8
		|| (ip >> 22) == (0x64400000u >> 22)
		|| (ip >> 16) == 0xA9FE;
}

bool isPublicIPv4(std::uint32_t ip)
{
	return ip != 0 && !isPrivateIPv4(ip) && (ip >> 8) != 0xC00000u && (ip >> 24) != 127 && (ip >> 28) < 0xE;
}

} // namespace PortMapping
} // namespace Engine
