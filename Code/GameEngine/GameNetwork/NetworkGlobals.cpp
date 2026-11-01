/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// What the network code shares between all transports: the address book and name lookups.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Engine/NetAddressBook.h"
#include "GameNetwork/Connection.h"
#include "GameNetwork/networkutil.h"

#include <ws2tcpip.h>
#include <iphlpapi.h>

#include "GameNetwork/PortMapper.h"

// On the heap so that ReleaseNetworkStatics can give everything back before the leak check at exit
// (a static object would only be destroyed after it).
static ::Engine::NetAddressBook *s_netAddressBook = nullptr;

::Engine::NetAddressBook &TheNetAddressBook()
{
	if (s_netAddressBook == nullptr)
		s_netAddressBook = new ::Engine::NetAddressBook;
	return *s_netAddressBook;
}

void ReleaseNetworkStatics()
{
	Connection::releaseSharedPacket();
	ReleasePortMapper();
	delete s_netAddressBook;
	s_netAddressBook = nullptr;
}

Bool ResolveHostIPv6(const char *host, UnsignedInt &handle)
{
	handle = 0;
	if (host == nullptr || *host == 0)
		return FALSE;
	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;
	addrinfo *result = nullptr;
	if (getaddrinfo(host, nullptr, &hints, &result) != 0 || result == nullptr)
		return FALSE;
	for (const addrinfo *r = result; r != nullptr && handle == 0; r = r->ai_next)
	{
		if (r->ai_family == AF_INET6 && r->ai_addr != nullptr && r->ai_addrlen >= sizeof(sockaddr_in6))
		{
			const sockaddr_in6 *a = reinterpret_cast<const sockaddr_in6 *>(r->ai_addr);
			handle = TheNetAddressBook().registerAddress(::Engine::NetAddress::fromIPv6(reinterpret_cast<const std::uint8_t *>(&a->sin6_addr)));
		}
	}
	freeaddrinfo(result);
	return handle != 0;
}

// The interface the system sends internet traffic through (GetBestInterface towards a public
// address; nothing is sent). 0 if unknown.
static DWORD internetInterface()
{
	typedef DWORD (WINAPI *GetBestInterfaceFn)(DWORD, PDWORD);
	static GetBestInterfaceFn s_getBestInterface = []() -> GetBestInterfaceFn {
		HMODULE library = ::LoadLibraryA("iphlpapi.dll");
		return library ? reinterpret_cast<GetBestInterfaceFn>(reinterpret_cast<void *>(::GetProcAddress(library, "GetBestInterface"))) : nullptr;
	}();
	DWORD index = 0;
	if (s_getBestInterface == nullptr || s_getBestInterface(htonl(0x08080808u), &index) != NO_ERROR)
		return 0;
	return index;
}

typedef ULONG (WINAPI *GetAdaptersAddressesFn)(ULONG, ULONG, PVOID, PIP_ADAPTER_ADDRESSES, PULONG);

// GetAdaptersAddresses from iphlpapi, loaded at run time so no target needs another import library.
static GetAdaptersAddressesFn getAdaptersAddresses()
{
	static GetAdaptersAddressesFn s_function = []() -> GetAdaptersAddressesFn {
		HMODULE library = ::LoadLibraryA("iphlpapi.dll");
		return library ? reinterpret_cast<GetAdaptersAddressesFn>(reinterpret_cast<void *>(::GetProcAddress(library, "GetAdaptersAddresses"))) : nullptr;
	}();
	return s_function;
}

::Engine::NetAddress PublishedIPv6Address()
{
	::Engine::NetAddress best;
	const GetAdaptersAddressesFn get = getAdaptersAddresses();
	if (get == nullptr)
		return best;
	const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
	ULONG size = 16 * 1024;
	std::vector<unsigned char> buffer;
	for (int attempt = 0; attempt < 3; ++attempt)
	{
		buffer.resize(size);
		PIP_ADAPTER_ADDRESSES first = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
		const ULONG result = get(AF_INET6, flags, nullptr, first, &size);
		if (result == ERROR_BUFFER_OVERFLOW)
			continue;
		if (result != NO_ERROR)
			break;
		int bestRank = -1;
		ULONG bestLifetime = 0;
		for (PIP_ADAPTER_ADDRESSES adapter = first; adapter != nullptr; adapter = adapter->Next)
		{
			if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK
				|| adapter->IfType == IF_TYPE_TUNNEL)
			{
				continue;
			}
			for (PIP_ADAPTER_UNICAST_ADDRESS unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next)
			{
				const sockaddr *address = unicast->Address.lpSockaddr;
				if (address == nullptr || address->sa_family != AF_INET6 || unicast->DadState != IpDadStatePreferred)
					continue;
				const std::uint8_t *bytes = reinterpret_cast<const std::uint8_t *>(&reinterpret_cast<const sockaddr_in6 *>(address)->sin6_addr);
				if ((bytes[0] & 0xE0) != 0x20)
					continue;	// only global unicast (2000::/3)
				// temporary addresses have random suffixes and the shortest lifetimes
				const int rank = unicast->SuffixOrigin == IpSuffixOriginRandom ? 0 : 1;
				if (rank > bestRank || (rank == bestRank && unicast->PreferredLifetime > bestLifetime))
				{
					bestRank = rank;
					bestLifetime = unicast->PreferredLifetime;
					best = ::Engine::NetAddress::fromIPv6(bytes);
				}
			}
		}
		break;
	}
	return best;
}

std::vector<::Engine::LocalNetwork> LocalIPv4Networks()
{
	const GetAdaptersAddressesFn s_getAdaptersAddresses = getAdaptersAddresses();
	std::vector<::Engine::LocalNetwork> networks;
	std::uint32_t towardsInternet = 0;
	if (s_getAdaptersAddresses == nullptr)
		return networks;
	const DWORD internet = internetInterface();
	const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
	ULONG size = 16 * 1024;
	std::vector<unsigned char> buffer;
	for (int attempt = 0; attempt < 3; ++attempt)
	{
		buffer.resize(size);
		PIP_ADAPTER_ADDRESSES first = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
		const ULONG result = s_getAdaptersAddresses(AF_INET, flags, nullptr, first, &size);
		if (result == ERROR_BUFFER_OVERFLOW)
			continue;	// the size it needs is in `size` now
		if (result != NO_ERROR)
			break;
		for (PIP_ADAPTER_ADDRESSES adapter = first; adapter != nullptr; adapter = adapter->Next)
		{
			if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
				continue;
			for (PIP_ADAPTER_UNICAST_ADDRESS unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next)
			{
				const sockaddr *address = unicast->Address.lpSockaddr;
				if (address == nullptr || address->sa_family != AF_INET)
					continue;
				::Engine::LocalNetwork network;
				network.address = ntohl(reinterpret_cast<const sockaddr_in *>(address)->sin_addr.s_addr);
				network.prefixLength = unicast->OnLinkPrefixLength;
				networks.push_back(network);
				if (internet != 0 && adapter->IfIndex == internet && towardsInternet == 0)
					towardsInternet = network.address;
			}
		}
		break;
	}
	return ::Engine::usableLanNetworks(std::move(networks), towardsInternet);
}
