/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// UdpSocket.h -- one UDP port on IPv4 and, where the system has it, IPv6.
//
// The IPv4 socket binds the chosen local address, the IPv6 socket [::] (IPv6 only). Both are
// non-blocking. Owning: the sockets close and Winsock is released when the object goes.
//
// A computer usually has several global IPv6 addresses (a stable one and temporary ones), and the
// system picks the source of an unbound send by its own rules. A peer only accepts answers from
// the address it wrote to, so the IPv6 socket answers each peer from the address that peer's last
// datagram arrived at.

#pragma once

#ifndef GAMENETWORK_UDPSOCKET_H
#define GAMENETWORK_UDPSOCKET_H

#include "Engine/NetAddress.h"
#include "Lib/BaseType.h"

class UdpSocket
{
public:
	UdpSocket() = default;
	~UdpSocket();
	UdpSocket( const UdpSocket & ) = delete;
	UdpSocket &operator=( const UdpSocket & ) = delete;

	/// Binds ipv4:port (retrying for up to a second while the port is still held) and, with
	/// `withIPv6`, [::]:port. False if the IPv4 socket cannot be bound.
	[[nodiscard]] Bool open( UnsignedInt ipv4, UnsignedShort port, Bool withIPv6 );
	void close( void );

	Bool isOpen( void ) const { return m_socket4 != NONE; }
	Bool hasIPv6( void ) const { return m_socket6 != NONE; }
	UnsignedShort port( void ) const { return m_port; }

	/// One waiting datagram (IPv4 first). 0: nothing waiting, -1: the socket failed.
	Int receive( unsigned char *buffer, Int capacity, ::Engine::NetAddress &from );
	/// Bytes sent, or -1.
	Int send( const unsigned char *data, Int length, const ::Engine::NetAddress &to );

	Bool allowBroadcasts( Bool on );
	/// The bound address went away (network change); cleared by the next open().
	Bool addressLost( void ) const { return m_addressLost; }

private:
	static constexpr UnsignedInt64 NONE = ~static_cast<UnsignedInt64>(0);

	Bool startWinsock( void );
	Bool openIPv4( UnsignedInt ipv4, UnsignedShort port );
	void openIPv6( UnsignedShort port );
	Int receiveIPv6( unsigned char *buffer, Int capacity, ::Engine::NetAddress &from );
	void rememberReplyRoute( const ::Engine::NetAddress &peer, const std::uint8_t local[16], UnsignedInt interfaceIndex );

	/// The local address a peer wrote to, so that answers leave from it.
	struct ReplyRoute
	{
		::Engine::NetAddress peer;
		std::uint8_t local[16] = {};
		UnsignedInt interfaceIndex = 0;
	};
	static constexpr Int REPLY_ROUTES = 32;
	ReplyRoute m_replyRoutes[REPLY_ROUTES];
	Int m_nextReplyRoute = 0;
	void *m_receiveMessage = nullptr;	///< WSARecvMsg, found at run time

	UnsignedInt64 m_socket4 = NONE;
	UnsignedInt64 m_socket6 = NONE;
	UnsignedShort m_port = 0;
	Bool m_winsock = FALSE;
	Bool m_addressLost = FALSE;
};

#endif // GAMENETWORK_UDPSOCKET_H
