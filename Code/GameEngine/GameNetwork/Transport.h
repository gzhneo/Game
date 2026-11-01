/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// Transport.h ///////////////////////////////////////////////////////////////
// Transport layer - packet queues, sealing and relaying over a UdpSocket (IPv4 and IPv6).
// Author: Matthew D. Campbell, July 2001

#pragma once

#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include "Engine/NetAddressBook.h"
#include "Engine/NetEmulator.h"
#include "Engine/PacketSeal.h"
#include "Engine/RateLimiter.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/UdpSocket.h"

#include <vector>

/**
 * The transport layer handles a UDP port for the game or the lobby: game packets (framed with a CRC
 * header, m_inBuffer/m_outBuffer, sealed per peer once keys are set) and raw datagrams for the
 * session and rendezvous layers (sendRaw/sendSealed/takeRaw). The sender of a sealed packet is the
 * peer whose keys open it, wherever it comes from; the address book remembers where to answer.
 * Peers that cannot be reached directly are reached through a relay server.
 */
// we only ever allocate one of there, and it is quite large, so we really DON'T want
// it to be a MemoryPoolObject (srj)
class Transport //: public MemoryPoolObject
{
	//MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(Transport, "Transport")		
public:

	/// Largest raw datagram (rendezvous answers are longer than game packets).
	static const Int MAX_RAW_DATAGRAM = 1280;

	/// A datagram outside the game's packet format: session and rendezvous messages.
	struct RawDatagram
	{
		unsigned char data[MAX_RAW_DATAGRAM];
		Int length;
		::Engine::NetAddress from;	///< where it came from; the relay server for relayed ones
		UnsignedInt sender;			///< the peer whose keys opened it, else the identity of its address
		Bool authenticated;			///< opened with a peer's keys
		Bool relayed;
	};

	Transport();
	~Transport();

	Bool init( AsciiString ip, UnsignedShort port );
	Bool init( UnsignedInt ip, UnsignedShort port );
	void reset( void );
	Bool update( void );									///< Call this once a GameEngine tick, regardless of whether the frame advances.

	/// A new socket on `ip`:`port`; peers, keys, relay and what was learnt of their endpoints stay.
	/// Peers know us by our keys, so they follow when our packets come from elsewhere.
	Bool reopen( UnsignedInt ip, UnsignedShort port );
	/// The local address went away (network changed): a socket on every address, same port; at
	/// most once a second.
	void recoverLostAddress( void );

	Bool doRecv( void );		///< call this to service the receive packets
	Bool doSend( void );		///< call this to service the send queue.

	Bool queueSend(UnsignedInt addr, UnsignedShort port, const UnsignedByte *buf, Int len /*,
		NetMessageFlags flags, Int id */, Bool plain = FALSE);				///< Queue a packet for sending to the specified address and port.  This will be sent on the next update() call.

	/// Largest payload queueSend() and the raw calls accept (default MAX_PACKET_SIZE).
	void setMaxPacketSize( Int size );
	Int getMaxPacketSize( void ) const { return m_maxPacketSize; }

	/// Sends a datagram as it is to an endpoint (session handshake, probes, rendezvous server).
	Bool sendRaw( const ::Engine::NetAddress &to, const UnsignedByte *buf, Int len );
	/// Seals a datagram for a peer with keys and sends it where the peer is reached (or relayed).
	Bool sendSealed( UnsignedInt identity, UnsignedShort port, const UnsignedByte *buf, Int len );
	/// Seals a datagram for a peer with keys and sends it to this endpoint (hole punching).
	Bool sendSealedTo( UnsignedInt identity, const ::Engine::NetAddress &to, const UnsignedByte *buf, Int len );
	/// Tests: a datagram as if it had been received.
	void injectRawForTest( const unsigned char *data, Int length, const ::Engine::NetAddress &from, UnsignedInt sender,
		Bool authenticated ) { keepRaw(data, length, from, sender, authenticated, FALSE); }

	/// Takes the oldest received raw datagram; false if none waits.
	Bool takeRaw( RawDatagram &out );
	/// Packets from this endpoint belong to the identity; answers go there.
	void bindPeer( UnsignedInt identity, const ::Engine::NetAddress &endpoint );

	/// Relay server and our identity in its relay session; peers marked relayed are reached through it.
	void setRelay( const ::Engine::NetAddress &server, UnsignedInt64 relayId, UnsignedInt localIdentity );
	void clearRelay( void );
	Bool hasRelay( void ) const { return m_relayId != 0; }
	void setRelayed( UnsignedInt identity, Bool relayed );
	Bool isRelayed( UnsignedInt identity ) const;

	/// Seals packets to and from this peer (by identity); from the first call on, packets of other
	/// senders are rejected and packets to other destinations are not sent.
	void setPeerKeys( UnsignedInt addr, UnsignedShort port, const ::Engine::SealSessionKeys &keys );
	Bool hasIPv6( void ) const { return m_socket.hasIPv6(); }

	/// Players of the game keep their own packet budget however many other senders flood us.
	void setTrustedSender( UnsignedInt addr, Bool trusted );
	void clearTrustedSenders( void );
	Bool isSealed( void ) const { return m_numSealedPeers > 0; }
	Bool hasPeerKeys( UnsignedInt addr ) const;
	void removePeerKeys( UnsignedInt addr );
	/// Keeps the keys of these peers and forgets all others.
	void retainPeers( const UnsignedInt *addrs, Int count );
	/// Lobby: peers with keys are sealed, everyone else stays plain (game searches are broadcast);
	/// received packets say in `authenticated` which they were. Default: only peers, all sealed.
	void setSealingOptional( Bool optional ) { m_sealOptional = optional; }

	Bool allowBroadcasts( Bool val ) { return m_socket.allowBroadcasts(val); }

	// Bandwidth metrics
	Real getIncomingBytesPerSecond( void );
	Real getIncomingPacketsPerSecond( void );
	Real getOutgoingBytesPerSecond( void );
	Real getOutgoingPacketsPerSecond( void );
	Real getUnknownBytesPerSecond( void );
	Real getUnknownPacketsPerSecond( void );

	TransportMessage m_outBuffer[MAX_MESSAGES];
	TransportMessage m_inBuffer[MAX_MESSAGES];

	UnsignedShort m_port;
private:
	UdpSocket m_socket;
	Int m_maxPacketSize;

	// Bad-link emulation of received packets (-netzLatenz and related switches)
	::Engine::NetEmulator m_emulator;
	UnsignedInt m_emulatorLogTime;
	UnsignedInt m_lastRecovery = 0;

	// Packet budget per sender, so one flooding machine cannot crowd out the others
	::Engine::RateLimiter m_limiter;

	struct SealedPeer
	{
		UnsignedInt addr;
		UnsignedShort port;
		Bool used;
		::Engine::SealedChannel channel;
	};
	Bool m_sealOptional;
	/// the players, and on the host's session socket its spectators
	static constexpr Int MAX_SEALED_PEERS = MAX_SLOTS + 8;
	SealedPeer m_sealedPeers[MAX_SEALED_PEERS];
	Int m_numSealedPeers;
	UnsignedInt m_unsealableSends;			///< packets not sent: no keys for the destination
	SealedPeer *findSealedPeer( UnsignedInt addr );

	// raw datagrams (ring)
	std::vector<RawDatagram> m_raw;
	Int m_rawFirst;
	Int m_rawCount;
	void keepRaw( const unsigned char *data, Int length, const ::Engine::NetAddress &from, UnsignedInt sender,
		Bool authenticated, Bool relayed );

	// relay
	::Engine::NetAddress m_relayServer;
	UnsignedInt64 m_relayId;
	UnsignedInt m_relayLocalIdentity;
	std::vector<UnsignedInt> m_relayedPeers;

	/// To a peer: through the relay if marked so, else where the address book says.
	Bool sendToIdentity( UnsignedInt identity, UnsignedShort port, const unsigned char *wire, Int length );
	void clearSealedPeers( void );
	void deliverEmulated( UnsignedInt now );
	void logEmulatorStats( void );

	// Bandwidth metrics
	UnsignedInt m_incomingBytes[MAX_TRANSPORT_STATISTICS_SECONDS];
	UnsignedInt m_unknownBytes[MAX_TRANSPORT_STATISTICS_SECONDS];
	UnsignedInt m_outgoingBytes[MAX_TRANSPORT_STATISTICS_SECONDS];
	UnsignedInt m_incomingPackets[MAX_TRANSPORT_STATISTICS_SECONDS];
	UnsignedInt m_unknownPackets[MAX_TRANSPORT_STATISTICS_SECONDS];
	UnsignedInt m_outgoingPackets[MAX_TRANSPORT_STATISTICS_SECONDS];
	Int m_statisticsSlot;
	UnsignedInt m_lastSecond;

	Bool isGeneralsPacket( TransportMessage *msg );
};

#endif // _TRANSPORT_H_
