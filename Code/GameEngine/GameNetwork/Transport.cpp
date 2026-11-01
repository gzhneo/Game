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


#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/crc.h"
#include "Engine/Rendezvous.h"
#include "Engine/SessionMessage.h"
#include "GameNetwork/Transport.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/networkutil.h"

//--------------------------------------------------------------------------
// Packet-level encryption is an XOR operation, for speed reasons.  To get
// the max throughput, we only XOR whole 4-byte words, so the last bytes
// can be non-XOR'd.

// This assumes the buf is a multiple of 4 bytes.  Extra is not encrypted.
static inline void encryptBuf( unsigned char *buf, Int len )
{
	UnsignedInt mask = 0x0000Fade;

	UnsignedInt *uintPtr = reinterpret_cast<UnsignedInt *>(buf);

	for (int i=0 ; i<len/4 ; i++) {
		*uintPtr = (*uintPtr) ^ mask;
		*uintPtr = htonl(*uintPtr);
		uintPtr++;
		mask += 0x00000321; // just for fun
	}
}

// This assumes the buf is a multiple of 4 bytes.  Extra is not encrypted.
static inline void decryptBuf( unsigned char *buf, Int len )
{
	UnsignedInt mask = 0x0000Fade;

	UnsignedInt *uintPtr = reinterpret_cast<UnsignedInt *>(buf);

	for (int i=0 ; i<len/4 ; i++) {
		*uintPtr = htonl(*uintPtr);
		*uintPtr = (*uintPtr) ^ mask;
		uintPtr++;
		mask += 0x00000321; // just for fun
	}
}

static UnsignedInt nonNegative( Int value )
{
	return value > 0 ? static_cast<UnsignedInt>(value) : 0;
}

//--------------------------------------------------------------------------

namespace
{
	const Int RAW_SLOTS = 64;

	Bool looksSealed( const unsigned char *data, Int length )
	{
		return length >= static_cast<Int>(::Engine::SEAL_OVERHEAD)
			&& data[0] == (::Engine::SEAL_MAGIC & 0xFF) && data[1] == ((::Engine::SEAL_MAGIC >> 8) & 0xFF)
			&& data[2] == ((::Engine::SEAL_MAGIC >> 16) & 0xFF) && data[3] == (::Engine::SEAL_MAGIC >> 24);
	}
}

Transport::Transport(void)
{
	m_maxPacketSize = MAX_PACKET_SIZE;
	m_port = 0;
	m_emulatorLogTime = 0;
	m_numSealedPeers = 0;
	m_unsealableSends = 0;
	m_sealOptional = FALSE;
	m_rawFirst = 0;
	m_rawCount = 0;
	m_relayId = 0;
	m_relayLocalIdentity = 0;
	for (Int i = 0; i < MAX_SEALED_PEERS; ++i)
		m_sealedPeers[i].used = FALSE;
	m_raw.assign(RAW_SLOTS, RawDatagram());
}

void Transport::clearSealedPeers( void )
{
	for (Int i = 0; i < MAX_SEALED_PEERS; ++i)
	{
		m_sealedPeers[i].channel.clear();
		m_sealedPeers[i].used = FALSE;
	}
	m_numSealedPeers = 0;
	m_unsealableSends = 0;
}

// By identity alone: behind NAT a peer's packets arrive from another port than its slot port.
Transport::SealedPeer *Transport::findSealedPeer( UnsignedInt addr )
{
	for (Int i = 0; i < MAX_SEALED_PEERS; ++i)
		if (m_sealedPeers[i].used && m_sealedPeers[i].addr == addr)
			return &m_sealedPeers[i];
	return nullptr;
}

Bool Transport::hasPeerKeys( UnsignedInt addr ) const
{
	for (Int i = 0; i < MAX_SEALED_PEERS; ++i)
		if (m_sealedPeers[i].used && m_sealedPeers[i].addr == addr)
			return TRUE;
	return FALSE;
}

void Transport::removePeerKeys( UnsignedInt addr )
{
	SealedPeer *peer = findSealedPeer(addr);
	if (peer == nullptr)
		return;
	peer->channel.clear();
	peer->used = FALSE;
	--m_numSealedPeers;
	m_limiter.setTrusted(addr, false, timeGetTime());
}

void Transport::retainPeers( const UnsignedInt *addrs, Int count )
{
	for (Int i = 0; i < MAX_SEALED_PEERS; ++i)
	{
		if (!m_sealedPeers[i].used)
			continue;
		Bool keep = FALSE;
		for (Int k = 0; k < count && !keep; ++k)
			keep = addrs[k] == m_sealedPeers[i].addr;
		if (!keep)
			removePeerKeys(m_sealedPeers[i].addr);
	}
}

void Transport::setPeerKeys( UnsignedInt addr, UnsignedShort port, const ::Engine::SealSessionKeys &keys )
{
	SealedPeer *peer = findSealedPeer(addr);
	if (!peer)
	{
		for (Int i = 0; i < MAX_SEALED_PEERS && peer == nullptr; ++i)
			if (!m_sealedPeers[i].used)
				peer = &m_sealedPeers[i];
		if (peer == nullptr)
			return;
		peer->used = TRUE;
		peer->addr = addr;
		peer->port = port;
		++m_numSealedPeers;
	}
	peer->channel.setKeys(keys);
	m_limiter.setTrusted(addr, true, timeGetTime());
}

void Transport::setTrustedSender( UnsignedInt addr, Bool trusted )
{
	m_limiter.setTrusted(addr, trusted != FALSE, timeGetTime());
}

void Transport::clearTrustedSenders( void )
{
	m_limiter.clearTrusted();
}

void Transport::setMaxPacketSize( Int size )
{
	const Int limit = MAX_MESSAGE_LEN;
	m_maxPacketSize = size < 1 ? MAX_PACKET_SIZE : (size > limit ? limit : size);
}

void Transport::bindPeer( UnsignedInt identity, const ::Engine::NetAddress &endpoint )
{
	if (identity != 0 && (endpoint.isV4() || endpoint.isV6()))
		TheNetAddressBook().observe(identity, m_port, endpoint);
}

void Transport::setRelay( const ::Engine::NetAddress &server, UnsignedInt64 relayId, UnsignedInt localIdentity )
{
	m_relayServer = server;
	m_relayId = relayId;
	m_relayLocalIdentity = localIdentity;
}

void Transport::clearRelay( void )
{
	m_relayServer = ::Engine::NetAddress();
	m_relayId = 0;
	m_relayLocalIdentity = 0;
	m_relayedPeers.clear();
}

void Transport::setRelayed( UnsignedInt identity, Bool relayed )
{
	for (size_t i = 0; i < m_relayedPeers.size(); ++i)
	{
		if (m_relayedPeers[i] == identity)
		{
			if (!relayed)
				m_relayedPeers.erase(m_relayedPeers.begin() + static_cast<std::ptrdiff_t>(i));
			return;
		}
	}
	if (relayed && m_relayedPeers.size() < MAX_SLOTS)
		m_relayedPeers.push_back(identity);
}

Bool Transport::isRelayed( UnsignedInt identity ) const
{
	if (m_relayId == 0)
		return FALSE;
	for (UnsignedInt peer : m_relayedPeers)
		if (peer == identity)
			return TRUE;
	return FALSE;
}

Transport::~Transport(void)
{
	reset();
}

Bool Transport::init( AsciiString ip, UnsignedShort port )
{
	return init(ResolveIP(ip), port);
}

Bool Transport::init( UnsignedInt ip, UnsignedShort port )
{
	// Loopback instances on one machine (-netzLokal) share [::]:port, so they stay on IPv4.
	if (!m_socket.open(ip, port, TheGlobalData->m_netLocalInstance == 0))
	{
		DEBUG_CRASH(("Could not bind to 0x%8.8X:%d\n", ip, port));
		return false;
	}

	// ------- Clear buffers --------
	int i;
	for (i=0; i<MAX_MESSAGES; ++i)
	{
		m_outBuffer[i].length = 0;
		m_inBuffer[i].length = 0;
	}
	for (i=0; i<MAX_TRANSPORT_STATISTICS_SECONDS; ++i)
	{
		m_incomingBytes[i] = 0;
		m_outgoingBytes[i] = 0;
		m_unknownBytes[i] = 0;
		m_incomingPackets[i] = 0;
		m_outgoingPackets[i] = 0;
		m_unknownPackets[i] = 0;
	}
	m_statisticsSlot = 0;
	m_lastSecond = timeGetTime();
	m_rawFirst = 0;
	m_rawCount = 0;

	m_port = port;

	::Engine::NetEmulatorSettings emu;
	emu.latencyMs = nonNegative(TheGlobalData->m_netEmuLatency);
	emu.jitterMs = nonNegative(TheGlobalData->m_netEmuJitter);
	emu.lossPercent = nonNegative(TheGlobalData->m_netEmuLoss);
	emu.duplicatePercent = nonNegative(TheGlobalData->m_netEmuDuplicate);
	emu.reorderPercent = nonNegative(TheGlobalData->m_netEmuReorder);
	// Different instances on one machine get different decisions unless a seed is given.
	emu.seed = TheGlobalData->m_netEmuSeed != 0 ? TheGlobalData->m_netEmuSeed : (static_cast<UnsignedInt>(::GetCurrentProcessId()) ^ port);
	m_emulator.configure(emu);
	m_emulatorLogTime = timeGetTime();

	// Far above what the game and the lobby send (a few dozen packets a second per peer, file
	// transfers in bursts), far below what fills the receive buffer every frame.
	::Engine::RateLimit perSender;
	perSender.rate = 500;
	perSender.burst = 1000;
	::Engine::RateLimit shared;
	shared.rate = 200;
	shared.burst = 400;
	m_limiter.configure(perSender, shared);

	if (m_emulator.active())
	{
		[[maybe_unused]] const ::Engine::NetEmulatorSettings &s = m_emulator.settings();
		DEBUG_LOG(("Transport::init - network emulator on port %d: latency %u ms, jitter %u ms, loss %u%%, duplicate %u%%, reorder %u%%, seed %u\n",
			port, s.latencyMs, s.jitterMs, s.lossPercent, s.duplicatePercent, s.reorderPercent, s.seed));
	}

	return true;
}

Bool Transport::sendToIdentity( UnsignedInt identity, UnsignedShort port, const unsigned char *wire, Int length )
{
	if (isRelayed(identity))
	{
		unsigned char frame[::Engine::RELAY_HEADER_SIZE + sizeof(TransportMessageHeader) + MAX_MESSAGE_LEN + ::Engine::SEAL_OVERHEAD];
		if (length < 0 || static_cast<size_t>(length) > sizeof(frame) - ::Engine::RELAY_HEADER_SIZE)
			return FALSE;
		::Engine::RelayHeader header;
		header.relayId = m_relayId;
		header.from = m_relayLocalIdentity;
		header.to = identity;
		::Engine::writeRelayHeader(header, frame);
		memcpy(frame + ::Engine::RELAY_HEADER_SIZE, wire, static_cast<size_t>(length));
		return m_socket.send(frame, length + static_cast<Int>(::Engine::RELAY_HEADER_SIZE), m_relayServer) > 0;
	}
	// The slot list names peers by identity; the address book knows where they are reached.
	::Engine::NetAddress to;
	if (!TheNetAddressBook().route(identity, port, m_port, to) || (to.isV6() && !hasIPv6()))
	{
		DEBUG_LOG(("Transport - no route to %X:%d, packet dropped\n", identity, port));
		return FALSE;
	}
	return m_socket.send(wire, length, to) > 0;
}

Bool Transport::sendRaw( const ::Engine::NetAddress &to, const UnsignedByte *buf, Int len )
{
	if (buf == nullptr || len < 1 || len > MAX_RAW_DATAGRAM)
		return FALSE;
	const Bool sent = m_socket.send(buf, len, to) > 0;
	if (sent)
	{
		m_outgoingPackets[m_statisticsSlot]++;
		m_outgoingBytes[m_statisticsSlot] += len;
	}
	return sent;
}

Bool Transport::sendSealedTo( UnsignedInt identity, const ::Engine::NetAddress &to, const UnsignedByte *buf, Int len )
{
	if (buf == nullptr || len < 1 || len > MAX_MESSAGE_LEN)
		return FALSE;
	SealedPeer *peer = findSealedPeer(identity);
	unsigned char sealed[MAX_MESSAGE_LEN + ::Engine::SEAL_OVERHEAD];
	const size_t sealedLength = peer ? peer->channel.seal(buf, static_cast<size_t>(len), sealed) : 0;
	if (sealedLength == 0 || m_socket.send(sealed, static_cast<Int>(sealedLength), to) <= 0)
		return FALSE;
	m_outgoingPackets[m_statisticsSlot]++;
	m_outgoingBytes[m_statisticsSlot] += static_cast<UnsignedInt>(sealedLength);
	return TRUE;
}

Bool Transport::sendSealed( UnsignedInt identity, UnsignedShort port, const UnsignedByte *buf, Int len )
{
	if (buf == nullptr || len < 1 || len > MAX_MESSAGE_LEN)
		return FALSE;
	SealedPeer *peer = findSealedPeer(identity);
	unsigned char sealed[MAX_MESSAGE_LEN + ::Engine::SEAL_OVERHEAD];
	const size_t sealedLength = peer ? peer->channel.seal(buf, static_cast<size_t>(len), sealed) : 0;
	if (sealedLength == 0)
	{
		if (m_unsealableSends++ == 0)
			DEBUG_LOG(("Transport::sendSealed - no keys for %X, message not sent\n", identity));
		return FALSE;
	}
	const Bool sent = sendToIdentity(identity, port, sealed, static_cast<Int>(sealedLength));
	if (sent)
	{
		m_outgoingPackets[m_statisticsSlot]++;
		m_outgoingBytes[m_statisticsSlot] += static_cast<UnsignedInt>(sealedLength);
	}
	return sent;
}

void Transport::keepRaw( const unsigned char *data, Int length, const ::Engine::NetAddress &from, UnsignedInt sender,
	Bool authenticated, Bool relayed )
{
	if (m_raw.empty() || length < 1 || length > MAX_RAW_DATAGRAM)
		return;
	if (m_rawCount == RAW_SLOTS)
	{
		// the oldest goes: nobody reads raw datagrams on a game socket once the game runs
		m_rawFirst = (m_rawFirst + 1) % RAW_SLOTS;
		--m_rawCount;
	}
	RawDatagram &d = m_raw[static_cast<size_t>((m_rawFirst + m_rawCount) % RAW_SLOTS)];
	memcpy(d.data, data, static_cast<size_t>(length));
	d.length = length;
	d.from = from;
	d.sender = sender;
	d.authenticated = authenticated;
	d.relayed = relayed;
	++m_rawCount;
}

Bool Transport::takeRaw( RawDatagram &out )
{
	if (m_rawCount == 0)
		return FALSE;
	out = m_raw[static_cast<size_t>(m_rawFirst)];
	m_rawFirst = (m_rawFirst + 1) % RAW_SLOTS;
	--m_rawCount;
	return TRUE;
}

Bool Transport::reopen( UnsignedInt ip, UnsignedShort port )
{
	m_socket.close();
	if (!m_socket.open(ip, port, TheGlobalData->m_netLocalInstance == 0))
	{
		DEBUG_LOG(("Transport::reopen - could not bind to 0x%8.8X:%d\n", ip, port));
		return FALSE;
	}
	TheNetAddressBook().moveLocalPort(m_port, port);
	m_port = port;
	DEBUG_LOG(("Transport::reopen - socket now on 0x%8.8X:%d\n", ip, port));
	return TRUE;
}

void Transport::recoverLostAddress( void )
{
	if (!m_socket.addressLost())
		return;
	const UnsignedInt now = timeGetTime();
	if (now - m_lastRecovery < 1000)
		return;
	m_lastRecovery = now;
	reopen(0, m_port);
}

void Transport::reset( void )
{
	m_emulator.clear();
	clearSealedPeers();
	clearRelay();
	m_socket.close();
	m_rawFirst = 0;
	m_rawCount = 0;
}

Bool Transport::update( void )
{
	Bool retval = TRUE;
	if (doRecv() == FALSE && m_socket.addressLost())
	{
		retval = FALSE;
	}
	if (doSend() == FALSE && m_socket.addressLost())
	{
		retval = FALSE;
	}
	return retval;
}

Bool Transport::doSend() {
	if (!m_socket.isOpen())
	{
		DEBUG_LOG(("Transport::doSend() - no socket!\n"));
		return FALSE;
	}

	Bool retval = TRUE;

	// Statistics gathering
	UnsignedInt now = timeGetTime();
	if (m_lastSecond + 1000 < now)
	{
		m_lastSecond = now;
		m_statisticsSlot = (m_statisticsSlot + 1) % MAX_TRANSPORT_STATISTICS_SECONDS;
		m_outgoingPackets[m_statisticsSlot] = 0;
		m_outgoingBytes[m_statisticsSlot] = 0;
		m_incomingPackets[m_statisticsSlot] = 0;
		m_incomingBytes[m_statisticsSlot] = 0;
		m_unknownPackets[m_statisticsSlot] = 0;
		m_unknownBytes[m_statisticsSlot] = 0;
	}

	// Send all messages
	int i;
	for (i=0; i<MAX_MESSAGES; ++i)
	{
		if (m_outBuffer[i].length != 0)
		{
			unsigned char *wire = reinterpret_cast<unsigned char *>(&m_outBuffer[i]);
			Int wireLength = m_outBuffer[i].length + sizeof(TransportMessageHeader);
			unsigned char sealed[sizeof(TransportMessageHeader) + MAX_MESSAGE_LEN + ::Engine::SEAL_OVERHEAD];
			SealedPeer *peer = (isSealed() && m_outBuffer[i].authenticated == 0) ? findSealedPeer(m_outBuffer[i].addr) : nullptr;
			const Bool mayGoPlain = m_sealOptional && peer == nullptr;
			if (isSealed() && !mayGoPlain)
			{
				const size_t sealedLength = peer ? peer->channel.seal(wire, static_cast<size_t>(wireLength), sealed) : 0;
				if (sealedLength == 0)
				{
					if (m_unsealableSends++ == 0)
						DEBUG_LOG(("Transport::doSend - no keys for %X:%d, packet not sent\n", m_outBuffer[i].addr, m_outBuffer[i].port));
					m_outBuffer[i].length = 0;
					continue;
				}
				wire = sealed;
				wireLength = static_cast<Int>(sealedLength);
			}
			if (sendToIdentity(m_outBuffer[i].addr, m_outBuffer[i].port, wire, wireLength))
			{
				m_outgoingPackets[m_statisticsSlot]++;
				m_outgoingBytes[m_statisticsSlot] += wireLength;
				m_outBuffer[i].length = 0;  // Remove from queue
			}
			else if (m_socket.addressLost())
			{
				retval = FALSE;
			}
			else
			{
				m_outBuffer[i].length = 0;	// no route or the peer is gone: do not retry forever
			}
		}
	} // for (i=0; i<MAX_MESSAGES; ++i)

	return retval;
}

Bool Transport::doRecv() 
{
	if (!m_socket.isOpen())
	{
		DEBUG_LOG(("Transport::doRecv() - no socket!\n"));
		return FALSE;
	}

	Bool retval = TRUE;

	::Engine::NetAddress from;
	const UnsignedInt now = timeGetTime();

	TransportMessage incomingMessage;
	unsigned char *buf = reinterpret_cast<unsigned char *>(&incomingMessage);
	// relay header + seal envelope + transport header + payload; rendezvous answers are raw and longer
	unsigned char wire[::Engine::RELAY_HEADER_SIZE + ::Engine::SEAL_OVERHEAD + sizeof(TransportMessageHeader) + MAX_RAW_DATAGRAM];

	int len = 0;
	// A flood must not hold the frame: what is left stays in the socket for the next call.
	Int datagramsLeft = 1024;
	while (datagramsLeft-- > 0 && (len = m_socket.receive(wire, static_cast<Int>(sizeof(wire)), from)) > 0)
	{
		unsigned char *data = wire;
		Bool relayed = FALSE;
		UnsignedInt sender = 0;

		// relayed: the server forwards what a peer sent us; the header names the peer
		if (m_relayId != 0 && from == m_relayServer && ::Engine::looksLikeRelayFrame(wire, static_cast<size_t>(len)))
		{
			::Engine::RelayHeader header;
			if (!::Engine::readRelayHeader(wire, static_cast<size_t>(len), header) || header.relayId != m_relayId
				|| header.to != m_relayLocalIdentity || header.from == 0)
			{
				m_unknownPackets[m_statisticsSlot]++;
				m_unknownBytes[m_statisticsSlot] += len;
				continue;
			}
			data = wire + ::Engine::RELAY_HEADER_SIZE;
			len -= static_cast<int>(::Engine::RELAY_HEADER_SIZE);
			sender = header.from;
			relayed = TRUE;
		}
		else
		{
			sender = TheNetAddressBook().identityOf(from, m_port);
		}

		// before any decryption, so forged packets cost little
		if (sender == 0 || !m_limiter.allow(sender, now))
		{
			m_unknownPackets[m_statisticsSlot]++;
			m_unknownBytes[m_statisticsSlot] += len;
			continue;
		}

		// answers of rendezvous servers
		if (!relayed && ::Engine::looksLikeTreff(data, static_cast<size_t>(len)))
		{
			keepRaw(data, len, from, sender, FALSE, FALSE);
			continue;
		}

		Bool authenticated = FALSE;
		if (looksSealed(data, len))
		{
			// The keys that open the packet name its sender: behind NAT or after a port change the
			// endpoint is new, so every peer's keys are tried (at most seven) before it is dropped.
			unsigned char plainBuffer[sizeof(TransportMessageHeader) + MAX_MESSAGE_LEN];
			// open() writes the whole content: longer envelopes would not fit the buffer
			if (static_cast<size_t>(len) > sizeof(plainBuffer) + ::Engine::SEAL_OVERHEAD)
			{
				m_unknownPackets[m_statisticsSlot]++;
				m_unknownBytes[m_statisticsSlot] += len;
				continue;
			}

			SealedPeer *peer = findSealedPeer(sender);
			long plain = peer ? peer->channel.open(data, static_cast<size_t>(len), plainBuffer) : -1;
			if (plain < 0 && !relayed)
			{
				for (Int p = 0; p < MAX_SEALED_PEERS && plain < 0; ++p)
				{
					if (!m_sealedPeers[p].used || &m_sealedPeers[p] == peer)
						continue;
					// trials with the keys of others are no rejections of theirs
					plain = m_sealedPeers[p].channel.open(data, static_cast<size_t>(len), plainBuffer, ::Engine::SealedChannel::Rejects::Ignore);
					if (plain >= 0)
						peer = &m_sealedPeers[p];
				}
			}
			if (plain <= 0 || plain > static_cast<long>(sizeof(plainBuffer)) || peer == nullptr)
			{
				m_unknownPackets[m_statisticsSlot]++;
				m_unknownBytes[m_statisticsSlot] += len;
				continue;
			}
			sender = peer->addr;
			authenticated = TRUE;
			// Replies go where the peer's packets come from (NAT); only after the tag held.
			if (!relayed)
				TheNetAddressBook().observe(sender, m_port, from);
			if (::Engine::looksLikeSession(plainBuffer, static_cast<size_t>(plain)))
			{
				keepRaw(plainBuffer, static_cast<Int>(plain), from, sender, TRUE, relayed);
				continue;
			}
			memcpy(buf, plainBuffer, static_cast<size_t>(plain));
			len = static_cast<int>(plain);
		}
		else
		{
			if (::Engine::looksLikeSession(data, static_cast<size_t>(len)))
			{
				// plain session messages: discovery, handshake, probes; the session layer decides
				keepRaw(data, len, from, sender, FALSE, relayed);
				continue;
			}
			if ((isSealed() && !m_sealOptional) || len > static_cast<int>(sizeof(TransportMessageHeader) + MAX_MESSAGE_LEN))
			{
				m_unknownPackets[m_statisticsSlot]++;
				m_unknownBytes[m_statisticsSlot] += len;
				continue;
			}
			memcpy(buf, data, static_cast<size_t>(len));
		}

		decryptBuf(buf, len);

		incomingMessage.length = len - sizeof(TransportMessageHeader);

		if (len <= static_cast<int>(sizeof(TransportMessageHeader)) || !isGeneralsPacket( &incomingMessage ))
		{
			m_unknownPackets[m_statisticsSlot]++;
			m_unknownBytes[m_statisticsSlot] += len;
			continue;
		}

		if (!authenticated && !relayed)
			TheNetAddressBook().observe(sender, m_port, from);

		// Something there; stick it somewhere
		m_incomingPackets[m_statisticsSlot]++;
		m_incomingBytes[m_statisticsSlot] += len;

		if (m_emulator.active())
		{
			m_emulator.push(buf, len, sender, from.port, now, authenticated ? 1 : 0);
			continue;
		}

		for (int i=0; i<MAX_MESSAGES; ++i)
		{
			if (m_inBuffer[i].length == 0)
			{
				// Empty slot; use it
				m_inBuffer[i].length = incomingMessage.length;
				m_inBuffer[i].addr = sender;
				m_inBuffer[i].port = from.port;
				m_inBuffer[i].authenticated = authenticated ? 1 : 0;
				memcpy(&m_inBuffer[i], buf, len);
				break;
			}
		}
	}

	if (m_emulator.active())
		deliverEmulated(now);
	// Periodically rather than on reset: the transport can outlive the debug log at shutdown.
	if ((m_emulator.active() || isSealed() || m_limiter.stats().dropped != 0 || m_limiter.stats().droppedShared != 0)
		&& now - m_emulatorLogTime >= 10000)
	{
		m_emulatorLogTime = now;
		logEmulatorStats();
	}

	if (len == -1) {
		// there was a socket error trying to perform a read.
		retval = FALSE;
	}

	return retval;
}

// Moves the emulated packets that are due into free slots of the in buffer; the rest waits.
void Transport::deliverEmulated( UnsignedInt now )
{
	::Engine::NetEmulator::Packet packet;
	for (int i=0; i<MAX_MESSAGES; ++i)
	{
		if (m_inBuffer[i].length != 0)
			continue;
		if (!m_emulator.pop(now, packet))
			break;
		// push() only sees packets that passed isGeneralsPacket, so the size fits.
		memcpy(&m_inBuffer[i], packet.data.data(), packet.data.size());
		m_inBuffer[i].length = static_cast<Int>(packet.data.size() - sizeof(TransportMessageHeader));
		m_inBuffer[i].addr = packet.addr;
		m_inBuffer[i].port = packet.port;
		m_inBuffer[i].authenticated = packet.tag;
	}
}

void Transport::logEmulatorStats( void )
{
	const ::Engine::RateLimiterStats &limits = m_limiter.stats();
	if (limits.dropped != 0 || limits.droppedShared != 0)
	{
		DEBUG_LOG(("Transport - port %d: %llu packets over a sender's budget, %llu over the shared budget dropped\n",
			m_port, static_cast<unsigned long long>(limits.dropped), static_cast<unsigned long long>(limits.droppedShared)));
	}
	for (Int i = 0; i < MAX_SEALED_PEERS; ++i)
	{
		if (!m_sealedPeers[i].used)
			continue;
		[[maybe_unused]] const ::Engine::SealStats &s = m_sealedPeers[i].channel.stats();
		DEBUG_LOG(("Transport - sealed traffic with %X:%d: %llu sent, %llu accepted, rejected %llu format, %llu tag, %llu replay; %u unsealable sends\n",
			m_sealedPeers[i].addr, m_sealedPeers[i].port, static_cast<unsigned long long>(s.sealed), static_cast<unsigned long long>(s.opened),
			static_cast<unsigned long long>(s.rejectedFormat), static_cast<unsigned long long>(s.rejectedTag),
			static_cast<unsigned long long>(s.rejectedReplay), m_unsealableSends));
	}
	if (!m_emulator.active())
		return;
	[[maybe_unused]] const ::Engine::NetEmulatorStats &s = m_emulator.stats();
	DEBUG_LOG(("Transport - network emulator on port %d: %llu received, %llu dropped, %llu overflowed, %llu duplicated, %llu reordered, %llu delivered\n",
		m_port, static_cast<unsigned long long>(s.received), static_cast<unsigned long long>(s.dropped),
		static_cast<unsigned long long>(s.overflowed), static_cast<unsigned long long>(s.duplicated),
		static_cast<unsigned long long>(s.reordered), static_cast<unsigned long long>(s.delivered)));
}

Bool Transport::queueSend(UnsignedInt addr, UnsignedShort port, const UnsignedByte *buf, Int len /*,
						  NetMessageFlags flags, Int id */, Bool plain)
{
	int i;

	if (len < 1 || len > m_maxPacketSize)
	{
		return false;
	}

	for (i=0; i<MAX_MESSAGES; ++i)
	{
		if (m_outBuffer[i].length == 0)
		{
			// Insert data here
			m_outBuffer[i].length = len;
			memcpy(m_outBuffer[i].data, buf, len);
			m_outBuffer[i].addr = addr;
			m_outBuffer[i].port = port;
			m_outBuffer[i].authenticated = plain ? 1 : 0;
			m_outBuffer[i].header.magic = GENERALS_MAGIC_NUMBER;

			CRC crc;
			crc.computeCRC( reinterpret_cast<unsigned char *>(&(m_outBuffer[i].header.magic)), m_outBuffer[i].length + sizeof(TransportMessageHeader) - sizeof(UnsignedInt) );
			m_outBuffer[i].header.crc = crc.get();

			// Encrypt packet
			encryptBuf(reinterpret_cast<unsigned char *>(&m_outBuffer[i]), len + sizeof(TransportMessageHeader));

			return true;
		}
	}
	return false;
}

Bool Transport::isGeneralsPacket( TransportMessage *msg )
{
	if (!msg)
		return false;

	if (msg->length < 0 || msg->length > MAX_MESSAGE_LEN)
		return false;

	CRC crc;
	crc.computeCRC( reinterpret_cast<unsigned char *>(&(msg->header.magic)), msg->length + sizeof(TransportMessageHeader) - sizeof(UnsignedInt) );

	if (crc.get() != msg->header.crc)
		return false;

	if (msg->header.magic != GENERALS_MAGIC_NUMBER)
		return false;

	return true;
}

// Statistics ---------------------------------------------------
Real Transport::getIncomingBytesPerSecond( void )
{
	Real val = 0.0;
	for (int i=0; i<MAX_TRANSPORT_STATISTICS_SECONDS; ++i)
	{
		if (i != m_statisticsSlot)
			val += m_incomingBytes[i];
	}
	return val / (MAX_TRANSPORT_STATISTICS_SECONDS-1);
}

Real Transport::getIncomingPacketsPerSecond( void )
{
	Real val = 0.0;
	for (int i=0; i<MAX_TRANSPORT_STATISTICS_SECONDS; ++i)
	{
		if (i != m_statisticsSlot)
			val += m_incomingPackets[i];
	}
	return val / (MAX_TRANSPORT_STATISTICS_SECONDS-1);
}

Real Transport::getOutgoingBytesPerSecond( void )
{
	Real val = 0.0;
	for (int i=0; i<MAX_TRANSPORT_STATISTICS_SECONDS; ++i)
	{
		if (i != m_statisticsSlot)
			val += m_outgoingBytes[i];
	}
	return val / (MAX_TRANSPORT_STATISTICS_SECONDS-1);
}

Real Transport::getOutgoingPacketsPerSecond( void )
{
	Real val = 0.0;
	for (int i=0; i<MAX_TRANSPORT_STATISTICS_SECONDS; ++i)
	{
		if (i != m_statisticsSlot)
			val += m_outgoingPackets[i];
	}
	return val / (MAX_TRANSPORT_STATISTICS_SECONDS-1);
}

Real Transport::getUnknownBytesPerSecond( void )
{
	Real val = 0.0;
	for (int i=0; i<MAX_TRANSPORT_STATISTICS_SECONDS; ++i)
	{
		if (i != m_statisticsSlot)
			val += m_unknownBytes[i];
	}
	return val / (MAX_TRANSPORT_STATISTICS_SECONDS-1);
}

Real Transport::getUnknownPacketsPerSecond( void )
{
	Real val = 0.0;
	for (int i=0; i<MAX_TRANSPORT_STATISTICS_SECONDS; ++i)
	{
		if (i != m_statisticsSlot)
			val += m_unknownPackets[i];
	}
	return val / (MAX_TRANSPORT_STATISTICS_SECONDS-1);
}



