/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/UdpSocket.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#include <cstring>

namespace
{
	constexpr int SOCKET_BUFFER = 256 * 1024;
	constexpr UnsignedInt BIND_RETRY_MS = 1000;

	SOCKET native( UnsignedInt64 s ) { return static_cast<SOCKET>(s); }

	Bool makeNonBlocking( SOCKET s )
	{
		unsigned long nonBlocking = 1;
		return ioctlsocket(s, FIONBIO, &nonBlocking) == 0;
	}
}

UdpSocket::~UdpSocket()
{
	close();
}

Bool UdpSocket::startWinsock( void )
{
	if (m_winsock)
		return TRUE;
	WSADATA data;
	if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
		return FALSE;
	if (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 2)
	{
		WSACleanup();
		return FALSE;
	}
	m_winsock = TRUE;
	return TRUE;
}

Bool UdpSocket::open( UnsignedInt ipv4, UnsignedShort port, Bool withIPv6 )
{
	close();
	if (!startWinsock())
		return FALSE;
	// the port may still be held by a socket closed a moment ago
	Bool bound = FALSE;
	const UnsignedInt start = timeGetTime();
	while (!bound && timeGetTime() - start < BIND_RETRY_MS)
		bound = openIPv4(ipv4, port);
	if (!bound)
	{
		DEBUG_LOG(("UdpSocket::open - cannot bind 0x%8.8X:%d, error %d\n", ipv4, port, WSAGetLastError()));
		return FALSE;
	}
	m_port = port;
	m_addressLost = FALSE;
	if (withIPv6)
		openIPv6(port);
	return TRUE;
}

void UdpSocket::close( void )
{
	if (m_socket4 != NONE)
		closesocket(native(m_socket4));
	if (m_socket6 != NONE)
		closesocket(native(m_socket6));
	m_socket4 = NONE;
	m_socket6 = NONE;
	m_receiveMessage = nullptr;
	for (ReplyRoute &route : m_replyRoutes)
		route = ReplyRoute();
	m_nextReplyRoute = 0;
	if (m_winsock)
	{
		WSACleanup();
		m_winsock = FALSE;
	}
}

Bool UdpSocket::openIPv4( UnsignedInt ipv4, UnsignedShort port )
{
	const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET)
		return FALSE;
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(ipv4);
	addr.sin_port = htons(port);
	if (bind(s, reinterpret_cast<const sockaddr *>(&addr), static_cast<int>(sizeof(addr))) != 0 || !makeNonBlocking(s))
	{
		closesocket(s);
		return FALSE;
	}
	const int buffer = SOCKET_BUFFER;
	setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&buffer), static_cast<int>(sizeof(buffer)));
	setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char *>(&buffer), static_cast<int>(sizeof(buffer)));
	m_socket4 = static_cast<UnsignedInt64>(s);
	return TRUE;
}

// [::]:port for IPv6 only; IPv4 stays on its own socket and address. Without IPv6 on the system
// the port works with IPv4 alone.
void UdpSocket::openIPv6( UnsignedShort port )
{
	const SOCKET s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET)
	{
		DEBUG_LOG(("UdpSocket::openIPv6 - no IPv6 on this system\n"));
		return;
	}
	const DWORD v6only = 1;
	setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char *>(&v6only), static_cast<int>(sizeof(v6only)));
	sockaddr_in6 addr{};
	addr.sin6_family = AF_INET6;
	addr.sin6_addr = in6addr_any;
	addr.sin6_port = htons(port);
	if (bind(s, reinterpret_cast<const sockaddr *>(&addr), static_cast<int>(sizeof(addr))) != 0 || !makeNonBlocking(s))
	{
		DEBUG_LOG(("UdpSocket::openIPv6 - cannot bind [::]:%d, error %d\n", port, WSAGetLastError()));
		closesocket(s);
		return;
	}
	m_socket6 = static_cast<UnsignedInt64>(s);

	// the address each datagram arrived at; without it answers leave from the system's choice
	const DWORD on = 1;
	if (setsockopt(s, IPPROTO_IPV6, IPV6_PKTINFO, reinterpret_cast<const char *>(&on), static_cast<int>(sizeof(on))) == 0)
	{
		GUID id = WSAID_WSARECVMSG;
		LPFN_WSARECVMSG function = nullptr;
		DWORD bytes = 0;
		if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &id, sizeof(id), &function, sizeof(function), &bytes, nullptr, nullptr) == 0)
			m_receiveMessage = reinterpret_cast<void *>(function);
	}
}

void UdpSocket::rememberReplyRoute( const ::Engine::NetAddress &peer, const std::uint8_t local[16], UnsignedInt interfaceIndex )
{
	for (ReplyRoute &route : m_replyRoutes)
	{
		if (route.peer == peer)
		{
			std::memcpy(route.local, local, 16);
			route.interfaceIndex = interfaceIndex;
			return;
		}
	}
	ReplyRoute &route = m_replyRoutes[m_nextReplyRoute];
	m_nextReplyRoute = (m_nextReplyRoute + 1) % REPLY_ROUTES;
	route.peer = peer;
	std::memcpy(route.local, local, 16);
	route.interfaceIndex = interfaceIndex;
}

Int UdpSocket::receiveIPv6( unsigned char *buffer, Int capacity, ::Engine::NetAddress &from )
{
	sockaddr_in6 from6{};
	if (m_receiveMessage == nullptr)
	{
		int fromLength = static_cast<int>(sizeof(from6));
		const int got = recvfrom(native(m_socket6), reinterpret_cast<char *>(buffer), capacity, 0, reinterpret_cast<sockaddr *>(&from6), &fromLength);
		if (got <= 0 || fromLength < static_cast<int>(sizeof(from6)))
			return 0;
		from = ::Engine::NetAddress::fromIPv6(reinterpret_cast<const std::uint8_t *>(&from6.sin6_addr), ntohs(from6.sin6_port));
		return got;
	}
	WSABUF data{};
	data.len = static_cast<ULONG>(capacity);
	data.buf = reinterpret_cast<char *>(buffer);
	char control[WSA_CMSG_SPACE(sizeof(IN6_PKTINFO))] = {};
	WSAMSG message{};
	message.name = reinterpret_cast<LPSOCKADDR>(&from6);
	message.namelen = static_cast<INT>(sizeof(from6));
	message.lpBuffers = &data;
	message.dwBufferCount = 1;
	message.Control.buf = control;
	message.Control.len = static_cast<ULONG>(sizeof(control));
	DWORD got = 0;
	const LPFN_WSARECVMSG receiveMessage = reinterpret_cast<LPFN_WSARECVMSG>(m_receiveMessage);
	if (receiveMessage(native(m_socket6), &message, &got, nullptr, nullptr) != 0 || got == 0
		|| message.namelen < static_cast<INT>(sizeof(from6)) || (message.dwFlags & MSG_TRUNC) != 0)
	{
		return 0;
	}
	from = ::Engine::NetAddress::fromIPv6(reinterpret_cast<const std::uint8_t *>(&from6.sin6_addr), ntohs(from6.sin6_port));
	for (WSACMSGHDR *header = WSA_CMSG_FIRSTHDR(&message); header != nullptr; header = WSA_CMSG_NXTHDR(&message, header))
	{
		if (header->cmsg_level == IPPROTO_IPV6 && header->cmsg_type == IPV6_PKTINFO)
		{
			IN6_PKTINFO info{};
			std::memcpy(&info, WSA_CMSG_DATA(header), sizeof(info));
			rememberReplyRoute(from, reinterpret_cast<const std::uint8_t *>(&info.ipi6_addr), info.ipi6_ifindex);
		}
	}
	return static_cast<Int>(got);
}

Bool UdpSocket::allowBroadcasts( Bool on )
{
	if (m_socket4 == NONE)
		return FALSE;
	const BOOL value = on ? TRUE : FALSE;
	return setsockopt(native(m_socket4), SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char *>(&value), static_cast<int>(sizeof(value))) == 0;
}

Int UdpSocket::receive( unsigned char *buffer, Int capacity, ::Engine::NetAddress &from )
{
	Int result = 0;
	if (m_socket4 != NONE)
	{
		sockaddr_in from4{};
		int fromLength = static_cast<int>(sizeof(from4));
		const int got = recvfrom(native(m_socket4), reinterpret_cast<char *>(buffer), capacity, 0, reinterpret_cast<sockaddr *>(&from4), &fromLength);
		if (got > 0)
		{
			from = ::Engine::NetAddress::fromIPv4(ntohl(from4.sin_addr.s_addr), ntohs(from4.sin_port));
			return got;
		}
		if (got < 0)
		{
			const int error = WSAGetLastError();
			// a datagram larger than the buffer is dropped; ICMP "port unreachable" shows up as a reset
			if (error != WSAEWOULDBLOCK && error != WSAEMSGSIZE && error != WSAECONNRESET)
			{
				m_addressLost = error == WSAEADDRNOTAVAIL;
				result = -1;
			}
		}
	}
	if (m_socket6 != NONE)
	{
		const Int got = receiveIPv6(buffer, capacity, from);
		if (got > 0)
			return got;
	}
	return result;
}

Int UdpSocket::send( const unsigned char *data, Int length, const ::Engine::NetAddress &to )
{
	if (to.isV4())
	{
		if (m_socket4 == NONE)
			return -1;
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(to.ipv4());
		addr.sin_port = htons(to.port);
		const int sent = sendto(native(m_socket4), reinterpret_cast<const char *>(data), length, 0, reinterpret_cast<const sockaddr *>(&addr), static_cast<int>(sizeof(addr)));
		if (sent < 0 && WSAGetLastError() == WSAEADDRNOTAVAIL)
			m_addressLost = TRUE;
		return sent > 0 ? sent : -1;
	}
	if (!to.isV6() || m_socket6 == NONE)
		return -1;
	sockaddr_in6 addr{};
	addr.sin6_family = AF_INET6;
	std::memcpy(&addr.sin6_addr, to.bytes, 16);
	addr.sin6_port = htons(to.port);
	for (const ReplyRoute &route : m_replyRoutes)
	{
		if (route.peer != to)
			continue;
		WSABUF buffer{};
		buffer.len = static_cast<ULONG>(length);
		buffer.buf = const_cast<char *>(reinterpret_cast<const char *>(data));
		char control[WSA_CMSG_SPACE(sizeof(IN6_PKTINFO))] = {};
		WSAMSG message{};
		message.name = reinterpret_cast<LPSOCKADDR>(&addr);
		message.namelen = static_cast<INT>(sizeof(addr));
		message.lpBuffers = &buffer;
		message.dwBufferCount = 1;
		message.Control.buf = control;
		message.Control.len = static_cast<ULONG>(sizeof(control));
		WSACMSGHDR *header = WSA_CMSG_FIRSTHDR(&message);
		header->cmsg_level = IPPROTO_IPV6;
		header->cmsg_type = IPV6_PKTINFO;
		header->cmsg_len = WSA_CMSG_LEN(sizeof(IN6_PKTINFO));
		IN6_PKTINFO info{};
		std::memcpy(&info.ipi6_addr, route.local, 16);
		info.ipi6_ifindex = route.interfaceIndex;
		std::memcpy(WSA_CMSG_DATA(header), &info, sizeof(info));
		DWORD sent = 0;
		if (WSASendMsg(native(m_socket6), &message, 0, &sent, nullptr, nullptr) == 0 && sent > 0)
			return static_cast<Int>(sent);
		break;	// the address may be gone: let the system choose
	}
	const int sent = sendto(native(m_socket6), reinterpret_cast<const char *>(data), length, 0, reinterpret_cast<const sockaddr *>(&addr), static_cast<int>(sizeof(addr)));
	return sent > 0 ? sent : -1;
}
