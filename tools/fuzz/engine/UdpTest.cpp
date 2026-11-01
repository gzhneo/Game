/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Transport test over real UDP sockets on loopback addresses (not a fuzzer; built like the
// harnesses so it uses the engine's Transport as the game does):
//   1. two transports; every packet arrives once, in order, with sender address and port
//
// All transports use 127.0.0.1 with their own ports. The loopback addresses of -netzLokal
// (127.0.0.N, same port) cannot be tested under Wine 9: it refuses a second loopback address on a
// port already bound and reports every loopback sender as 127.0.0.1 (Windows and Linux do neither).
//   2. the network emulator on the receiving side: nothing before the latency, loss, duplicates
//      and reordering close to the settings
//   4. sessions: three lobbies find a game, join, reach each other's game sockets, chat and start;
//      then by invitation over a rendezvous server on the loopback, with every way relayed
//   3. sealed traffic: packets between peers with session keys arrive; a third transport sending
//      from another address or with its own keys, plain packets and replayed packets do not
//
//   UdpTest <seed dir> <ignored> <ignored>   (arguments as the harnesses, see run.sh)
// Exit code 0 on success, 1 on a failed check.

#include "PreRTS.h"

#include "Common/GlobalData.h"
#include "GameNetwork/networkutil.h"
#include "GameNetwork/Transport.h"
#include "GameNetwork/UdpSocket.h"
#include "GameNetwork/PortMapper.h"
#include "Engine/NetAddressBook.h"
#include "Engine/PortMapping.h"
#include "Engine/RendezvousServer.h"
#include "Common/PlayerList.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GUIUtil.h"
#include "GameNetwork/GameLobby.h"
#include "GameNetwork/LANGameInfo.h"
#include "Common/Recorder.h"
#include "GameNetwork/NetworkInterface.h"

#include <ws2tcpip.h>

#include "Harness.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace
{

int s_failures = 0;

void check( bool condition, const char *what )
{
	std::printf( "  %s: %s\n", condition ? "ok" : "FAILED", what );
	if ( !condition )
		++s_failures;
}

constexpr UnsignedInt LOOPBACK = 0x7F000001;
constexpr UnsignedShort PORT = 28088;
constexpr UnsignedShort PORT_B = 28089;

struct Received
{
	UnsignedInt index;
	UnsignedInt sentAt;
	UnsignedInt arrivedAt;
	UnsignedInt addr;
	UnsignedShort port;
	Bool authenticated;
};

// Payload: index and send time, padded like a small game packet.
void sendIndexed( Transport &from, UnsignedInt toAddr, UnsignedInt index, UnsignedShort toPort = PORT_B, Bool plain = FALSE )
{
	UnsignedByte buf[64];
	std::memset( buf, 0xA5, sizeof( buf ) );
	const UnsignedInt now = timeGetTime();
	std::memcpy( buf, &index, 4 );
	std::memcpy( buf + 4, &now, 4 );
	from.queueSend( toAddr, toPort, buf, sizeof( buf ), plain );
}

void collect( Transport &t, std::vector<Received> &out )
{
	t.update();
	for ( int i = 0; i < MAX_MESSAGES; ++i )
	{
		TransportMessage &m = t.m_inBuffer[i];
		if ( m.length == 0 )
			continue;
		Received r;
		std::memcpy( &r.index, m.data, 4 );
		std::memcpy( &r.sentAt, m.data + 4, 4 );
		r.arrivedAt = timeGetTime();
		r.addr = m.addr;
		r.port = m.port;
		r.authenticated = m.authenticated != 0;
		out.push_back( r );
		m.length = 0;
	}
}

void pump( Transport &a, Transport &b, std::vector<Received> &atB, UnsignedInt ms )
{
	const UnsignedInt start = timeGetTime();
	while ( timeGetTime() - start < ms )
	{
		a.update();
		collect( b, atB );
		Sleep( 1 );
	}
}

void testPlainLoopback()
{
	TheNetAddressBook().clear();	// all loopback senders share 127.0.0.1 under Wine
	std::printf( "plain transport\n" );
	Transport a;
	Transport b;
	const UnsignedInt ipA = LOOPBACK;
	const UnsignedInt ipB = LOOPBACK;
	check( a.init( ipA, PORT ) == TRUE, "bind sender" );
	check( b.init( ipB, PORT_B ) == TRUE, "bind receiver" );

	std::vector<Received> atB;
	const UnsignedInt N = 300;
	for ( UnsignedInt i = 0; i < N; ++i )
	{
		sendIndexed( a, ipB, i );
		if ( i % 50 == 49 )
			pump( a, b, atB, 5 );
	}
	pump( a, b, atB, 200 );

	bool inOrder = true;
	bool sender = true;
	for ( std::size_t i = 0; i < atB.size(); ++i )
	{
		inOrder = inOrder && atB[i].index == i;
		sender = sender && atB[i].addr == ipA && atB[i].port == PORT;
	}
	check( atB.size() == N, "all packets arrived" );
	check( inOrder, "in order, none twice" );
	check( sender, "sender address and port" );
}

void testEmulator()
{
	TheNetAddressBook().clear();	// all loopback senders share 127.0.0.1 under Wine
	std::printf( "network emulator on the receiving transport\n" );
	TheWritableGlobalData->m_netEmuLatency = 80;
	TheWritableGlobalData->m_netEmuJitter = 20;
	TheWritableGlobalData->m_netEmuLoss = 20;
	TheWritableGlobalData->m_netEmuDuplicate = 10;
	TheWritableGlobalData->m_netEmuReorder = 10;
	TheWritableGlobalData->m_netEmuSeed = 99;

	Transport b;	// emulator on
	const UnsignedInt ipA = LOOPBACK;
	const UnsignedInt ipB = LOOPBACK;
	check( b.init( ipB, PORT_B ) == TRUE, "bind receiver" );

	TheWritableGlobalData->m_netEmuLatency = 0;
	TheWritableGlobalData->m_netEmuJitter = 0;
	TheWritableGlobalData->m_netEmuLoss = 0;
	TheWritableGlobalData->m_netEmuDuplicate = 0;
	TheWritableGlobalData->m_netEmuReorder = 0;
	Transport a;	// emulator off
	check( a.init( ipA, PORT ) == TRUE, "bind sender" );

	std::vector<Received> atB;
	const UnsignedInt N = 1000;
	for ( UnsignedInt i = 0; i < N; ++i )
	{
		sendIndexed( a, ipB, i );
		if ( i % 20 == 19 )
			pump( a, b, atB, 2 );
	}
	pump( a, b, atB, 600 );

	std::map<UnsignedInt, int> count;
	UnsignedInt minDelay = 0xFFFFFFFF;
	int inversions = 0;
	for ( std::size_t i = 0; i < atB.size(); ++i )
	{
		++count[atB[i].index];
		const UnsignedInt d = atB[i].arrivedAt - atB[i].sentAt;
		if ( d < minDelay )
			minDelay = d;
		if ( i > 0 && atB[i].index < atB[i - 1].index )
			++inversions;
	}
	int twice = 0;
	for ( const auto &c : count )
		twice += c.second > 1;
	const int lost = static_cast<int>(N) - static_cast<int>(count.size());
	std::printf( "  %zu packets arrived, %d distinct lost, %d duplicated, %d reordered, minimum delay %u ms\n",
		atB.size(), lost, twice, inversions, minDelay );
	check( minDelay >= 80, "no packet before the latency" );
	check( lost > 140 && lost < 260, "loss close to 20 percent" );
	check( twice > 40 && twice < 130, "duplicates close to 10 percent" );
	check( inversions > 0, "some packets reordered" );
}

// Sends raw bytes as they are on the wire (for forged and replayed packets).
class RawSender
{
public:
	Transport transport;
	bool bind( UnsignedInt ip, UnsignedShort port ) { return transport.init( ip, port ) != FALSE; }
	void send( UnsignedInt to, UnsignedShort port, const void *data, int len )
	{
		transport.sendRaw( ::Engine::NetAddress::fromIPv4( to, port ), static_cast<const UnsignedByte *>(data), len );
	}
};

void testSealed()
{
	TheNetAddressBook().clear();	// all loopback senders share 127.0.0.1 under Wine
	std::printf( "sealed traffic between two peers\n" );
	TheWritableGlobalData->m_netEmuLatency = 0;
	TheWritableGlobalData->m_netEmuJitter = 0;
	TheWritableGlobalData->m_netEmuLoss = 0;
	TheWritableGlobalData->m_netEmuDuplicate = 0;
	TheWritableGlobalData->m_netEmuReorder = 0;

	const UnsignedInt ipA = LOOPBACK;
	const UnsignedInt ipB = LOOPBACK;
	const UnsignedInt ipM = LOOPBACK;
	const UnsignedShort PORT_M = 28090;
	Transport a;
	Transport b;
	Transport m;
	check( a.init( ipA, PORT ) && b.init( ipB, PORT_B ) && m.init( ipM, PORT_M ), "bind three transports" );

	::Engine::SealKeyPair ka;
	::Engine::SealKeyPair kb;
	::Engine::SealKeyPair km;
	check( ka.generate() && kb.generate() && km.generate(), "key pairs" );
	const char ctx[] = "udp test";
	::Engine::SealSessionKeys ab;
	::Engine::SealSessionKeys ba;
	::Engine::SealSessionKeys mb;
	check( ::Engine::deriveSessionKeys( ka, kb.publicKey, ctx, sizeof( ctx ), ab )
		&& ::Engine::deriveSessionKeys( kb, ka.publicKey, ctx, sizeof( ctx ), ba )
		&& ::Engine::deriveSessionKeys( km, kb.publicKey, ctx, sizeof( ctx ), mb ), "session keys" );
	a.setPeerKeys( ipB, PORT_B, ab );
	b.setPeerKeys( ipA, PORT, ba );
	m.setPeerKeys( ipB, PORT_B, mb );	// the third party has keys, but not B's keys for it

	std::vector<Received> atB;
	for ( UnsignedInt i = 0; i < 50; ++i )
		sendIndexed( a, ipB, i );
	pump( a, b, atB, 200 );
	bool fromA = atB.size() == 50;
	for ( std::size_t i = 0; i < atB.size(); ++i )
		fromA = fromA && atB[i].index == i && atB[i].addr == ipA && atB[i].port == PORT;
	check( fromA, "all packets of the peer arrive, in order" );

	// the peer's packets to an address without keys are not sent
	sendIndexed( a, ipM, 1000, PORT_M );
	std::vector<Received> atM;
	pump( a, m, atM, 100 );
	check( atM.empty(), "nothing sent to an address without keys" );

	// the third party: sealed with its own keys, and plain packets as the old protocol sends them
	atB.clear();
	for ( UnsignedInt i = 0; i < 20; ++i )
		sendIndexed( m, ipB, 2000 + i );
	pump( m, b, atB, 100 );
	Transport plain;
	check( plain.init( LOOPBACK, 28091 ), "bind plain sender" );
	for ( UnsignedInt i = 0; i < 20; ++i )
		sendIndexed( plain, ipB, 3000 + i );
	pump( plain, b, atB, 100 );
	check( atB.empty(), "packets of a third party are rejected" );

	// the same bytes sent again (replay) from the peer's address are rejected
	::Engine::SealedChannel replayChannel;
	replayChannel.setKeys( ab );
	unsigned char inner[64 + sizeof( TransportMessageHeader )];
	std::memset( inner, 0, sizeof( inner ) );
	unsigned char wire[sizeof( inner ) + ::Engine::SEAL_OVERHEAD];
	const std::size_t n = replayChannel.seal( inner, sizeof( inner ), wire );	// counter 1, used by a already
	a.reset();	// frees the peer's address for the raw sender
	RawSender raw;
	check( raw.bind( ipA, PORT ), "raw sender on the peer's address" );
	raw.send( ipB, PORT_B, wire, static_cast<int>( n ) );
	pump( plain, b, atB, 100 );
	check( atB.empty(), "replayed packet is rejected" );
}

void testResolve()
{
	TheNetAddressBook().clear();	// all loopback senders share 127.0.0.1 under Wine
	std::printf( "host names\n" );
	UnsignedInt ip = 0;
	check( ResolveHostIPv4( "localhost", ip ) && (ip >> 24) == 127, "localhost resolves to a loopback address" );
	check( !ResolveHostIPv4( "", ip ) && ip == 0, "an empty name does not resolve" );
}

// NAT: a peer's packets come from another port than its slot port; replies must go there.
void testNatPort()
{
	TheNetAddressBook().clear();
	std::printf( "replies follow the port a peer's packets come from\n" );
	Transport host;
	Transport joiner;	// its slot port is PORT_B, the NAT maps it to 28095
	check( host.init( LOOPBACK, PORT ) && joiner.init( LOOPBACK, 28095 ), "bind host and joiner" );

	std::vector<Received> atHost;
	sendIndexed( joiner, LOOPBACK, 1, PORT );
	pump( joiner, host, atHost, 100 );
	check( atHost.size() == 1 && atHost[0].port == 28095, "the host sees the mapped port" );

	std::vector<Received> atJoiner;
	sendIndexed( host, LOOPBACK, 2, PORT_B );	// to the slot port
	pump( host, joiner, atJoiner, 100 );
	check( atJoiner.size() == 1 && atJoiner[0].index == 2, "the reply reaches the mapped port" );
	TheNetAddressBook().clear();
}

// A flood from one sender: the receiver takes its budget (burst plus rate), drops the rest and
// does not read more than a bounded number of datagrams per call.
void testFlood()
{
	TheNetAddressBook().clear();
	std::printf( "flood from one sender\n" );
	Transport flooder;
	Transport target;
	check( flooder.init( LOOPBACK, 28096 ) && target.init( LOOPBACK, 28097 ), "bind flooder and target" );

	std::vector<Received> got;
	const UnsignedInt start = timeGetTime();
	UnsignedInt sent = 0;
	for ( int round = 0; round < 40; ++round )
	{
		for ( int k = 0; k < 100; ++k )
		{
			sendIndexed( flooder, LOOPBACK, sent++, 28097 );
			if ( k % MAX_MESSAGES == MAX_MESSAGES - 1 )
				flooder.update();
		}
		flooder.update();
		collect( target, got );
	}
	pump( flooder, target, got, 200 );
	const UnsignedInt seconds = (timeGetTime() - start + 999) / 1000;
	std::printf( "  %u sent, %zu accepted in about %u s\n", sent, got.size(), seconds );
	check( got.size() >= 1000, "the burst arrives" );
	check( got.size() <= 1000 + 500 * seconds + 50, "no more than the budget" );
	TheNetAddressBook().clear();
}

// The lobby: sealed where keys exist, plain for everyone else, and the receiver can tell which.
void testLobbySealing()
{
	TheNetAddressBook().clear();
	std::printf( "lobby: sealed with peers, plain with others\n" );
	Transport a;
	Transport b;
	Transport stranger;
	for ( Transport *t : { &a, &b, &stranger } )
		t->setSealingOptional( TRUE );
	check( a.init( LOOPBACK, 28101 ) && b.init( LOOPBACK, 28102 ) && stranger.init( LOOPBACK, 28103 ), "bind" );
	::Engine::SealKeyPair ka;
	::Engine::SealKeyPair kb;
	check( ka.generate() && kb.generate(), "key pairs" );
	const char ctx[] = "lobby test";
	::Engine::SealSessionKeys ab;
	::Engine::SealSessionKeys ba;
	::Engine::deriveSessionKeys( ka, kb.publicKey, ctx, sizeof( ctx ), ab );
	::Engine::deriveSessionKeys( kb, ka.publicKey, ctx, sizeof( ctx ), ba );
	a.setPeerKeys( LOOPBACK, 28102, ab );	// under Wine every loopback sender is 127.0.0.1
	b.setPeerKeys( LOOPBACK, 28101, ba );

	std::vector<Received> atB;
	sendIndexed( a, LOOPBACK, 1, 28102 );
	pump( a, b, atB, 100 );
	check( atB.size() == 1 && atB[0].authenticated, "sealed to a peer, authenticated there" );

	atB.clear();
	sendIndexed( a, LOOPBACK, 2, 28102, TRUE );
	pump( a, b, atB, 100 );
	check( atB.size() == 1 && !atB[0].authenticated && atB[0].index == 2, "forced plain (game search) arrives plain" );

	atB.clear();
	sendIndexed( stranger, LOOPBACK, 3, 28102 );
	pump( stranger, b, atB, 100 );
	check( atB.size() == 1 && !atB[0].authenticated, "a sender without keys arrives plain" );

	// bytes that only look sealed are dropped
	atB.clear();
	RawSender raw;
	check( raw.bind( LOOPBACK, 28104 ), "raw sender" );
	unsigned char forged[100];
	std::memset( forged, 0x42, sizeof( forged ) );
	forged[0] = static_cast<unsigned char>( ::Engine::SEAL_MAGIC & 0xFF );
	forged[1] = static_cast<unsigned char>( (::Engine::SEAL_MAGIC >> 8) & 0xFF );
	forged[2] = static_cast<unsigned char>( (::Engine::SEAL_MAGIC >> 16) & 0xFF );
	forged[3] = static_cast<unsigned char>( ::Engine::SEAL_MAGIC >> 24 );
	raw.send( LOOPBACK, 28102, forged, sizeof( forged ) );
	pump( stranger, b, atB, 100 );
	check( atB.empty(), "a forged sealed packet is dropped" );
	TheNetAddressBook().clear();
}

// --- a router on the loopback for the port mapper ---------------------------------------------

constexpr UnsignedShort ROUTER_NATPMP = 25351;
constexpr UnsignedShort ROUTER_SSDP = 21900;
constexpr UnsignedShort ROUTER_HTTP = 24900;
constexpr UnsignedInt ROUTER_EXTERNAL = 0xCB007107;	// 203.0.113.7

/// Answers NAT-PMP, SSDP and the UPnP control calls like a home router, and records the mappings.
class FakeRouter
{
public:
	bool natpmp = true;
	bool upnp = true;
	bool permanentOnly = false;			///< answers leases with an end by error 725
	int refuseCode = 0;					///< answers every AddPortMapping by this error (606: not authorized)
	bool unusedServiceFirst = false;	///< lists a WANIPConnection:2 first that refuses (not the connection in use)
	std::string externalAddress = "203.0.113.7";
	std::string mappedAt;				///< control path of the last mapping
	bool firewallService = false;		///< offers WANIPv6FirewallControl (IGD 2)
	bool firewallEnabled = true;
	int pinholeRefuseCode = 0;			///< answers every AddPinhole by this error
	struct Pinhole { std::string client; std::string port; std::string protocol; UnsignedInt lease = 0; };
	std::map<int, Pinhole> pinholes;	///< UniqueID -> pinhole
	int nextPinholeId = 7;
	bool keepAlive = false;				///< answers with Content-Length and leaves the connection open, as some routers do
	std::string location = "http://127.0.0.1:24900/desc.xml";

	std::map<UnsignedShort, UnsignedInt> natpmpMappings;	///< port -> lifetime
	std::map<UnsignedShort, UnsignedInt> upnpMappings;		///< port -> lease
	std::string upnpClient;
	int httpRequests = 0;

	bool start()
	{
		m_udpNat = openUdp( ROUTER_NATPMP );
		m_udpSsdp = openUdp( ROUTER_SSDP );
		m_tcp = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
		sockaddr_in addr = loopback( ROUTER_HTTP );
		BOOL reuse = TRUE;
		setsockopt( m_tcp, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof( reuse ) );
		if ( m_udpNat == INVALID_SOCKET || m_udpSsdp == INVALID_SOCKET || m_tcp == INVALID_SOCKET
			|| bind( m_tcp, reinterpret_cast<sockaddr *>(&addr), sizeof( addr ) ) != 0 || listen( m_tcp, 4 ) != 0 )
			return false;
		m_thread = std::thread( [this] { serve(); } );
		return true;
	}

	void stop()
	{
		m_stop = true;
		if ( m_thread.joinable() )
			m_thread.join();
		closesocket( m_udpNat );
		closesocket( m_udpSsdp );
		closesocket( m_tcp );
		for ( SOCKET c : m_open )
			closesocket( c );
		m_open.clear();
	}

private:
	static sockaddr_in loopback( UnsignedShort port )
	{
		sockaddr_in addr;
		std::memset( &addr, 0, sizeof( addr ) );
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl( LOOPBACK );
		addr.sin_port = htons( port );
		return addr;
	}

	static SOCKET openUdp( UnsignedShort port )
	{
		SOCKET s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
		sockaddr_in addr = loopback( port );
		if ( s != INVALID_SOCKET && bind( s, reinterpret_cast<sockaddr *>(&addr), sizeof( addr ) ) != 0 )
		{
			closesocket( s );
			return INVALID_SOCKET;
		}
		return s;
	}

	void serve()
	{
		while ( !m_stop )
		{
			fd_set set;
			FD_ZERO( &set );
			FD_SET( m_udpNat, &set );
			FD_SET( m_udpSsdp, &set );
			FD_SET( m_tcp, &set );
			timeval tv = { 0, 50000 };
			if ( select( 0, &set, nullptr, nullptr, &tv ) <= 0 )
				continue;
			if ( FD_ISSET( m_udpNat, &set ) )
				answerNatPmp();
			if ( FD_ISSET( m_udpSsdp, &set ) )
				answerSsdp();
			if ( FD_ISSET( m_tcp, &set ) )
				answerHttp();
		}
	}

	void answerNatPmp()
	{
		unsigned char in[64];
		sockaddr_in from;
		int fromSize = sizeof( from );
		const int n = recvfrom( m_udpNat, reinterpret_cast<char *>(in), sizeof( in ), 0, reinterpret_cast<sockaddr *>(&from), &fromSize );
		if ( n < 2 || !natpmp )
			return;
		unsigned char out[16] = {};
		int size = 0;
		out[1] = static_cast<unsigned char>(in[1] + 128);
		if ( in[1] == 0 )
		{
			put32( out + 8, ROUTER_EXTERNAL );
			size = 12;
		}
		else if ( in[1] == 1 && n >= 12 )
		{
			const UnsignedShort internal = static_cast<UnsignedShort>((in[4] << 8) | in[5]);
			const UnsignedInt lifetime = (UnsignedInt(in[8]) << 24) | (UnsignedInt(in[9]) << 16) | (UnsignedInt(in[10]) << 8) | in[11];
			if ( lifetime == 0 )
				natpmpMappings.erase( internal );
			else
				natpmpMappings[internal] = lifetime;
			out[8] = in[4];
			out[9] = in[5];
			out[10] = lifetime == 0 ? 0 : in[4];
			out[11] = lifetime == 0 ? 0 : in[5];
			put32( out + 12, lifetime );
			size = 16;
		}
		sendto( m_udpNat, reinterpret_cast<char *>(out), size, 0, reinterpret_cast<sockaddr *>(&from), fromSize );
	}

	void answerSsdp()
	{
		char in[1500];
		sockaddr_in from;
		int fromSize = sizeof( from );
		const int n = recvfrom( m_udpSsdp, in, sizeof( in ) - 1, 0, reinterpret_cast<sockaddr *>(&from), &fromSize );
		if ( n <= 0 || !upnp || std::strncmp( in, "M-SEARCH", 8 ) != 0 )
			return;
		const std::string answer = "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=1800\r\nLOCATION: " + location
			+ "\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n";
		sendto( m_udpSsdp, answer.data(), static_cast<int>(answer.size()), 0, reinterpret_cast<sockaddr *>(&from), fromSize );
	}

	void answerHttp()
	{
		SOCKET c = accept( m_tcp, nullptr, nullptr );
		if ( c == INVALID_SOCKET )
			return;
		++httpRequests;
		std::string request;
		char buffer[2048];
		for ( int rounds = 0; rounds < 100; ++rounds )
		{
			const int n = recv( c, buffer, sizeof( buffer ), 0 );
			if ( n <= 0 )
				break;
			request.append( buffer, n );
			const std::size_t headerEnd = request.find( "\r\n\r\n" );
			if ( headerEnd == std::string::npos )
				continue;
			const std::size_t lengthAt = request.find( "Content-Length: " );
			const std::size_t length = lengthAt == std::string::npos ? 0 : std::strtoul( request.c_str() + lengthAt + 16, nullptr, 10 );
			if ( request.size() >= headerEnd + 4 + length )
				break;
		}
		std::string status = "200 OK";
		std::string body;
		auto value = [&request]( const char *tag ) {
			const std::string open = std::string( "<" ) + tag + ">";
			const std::size_t a = request.find( open );
			const std::size_t b = request.find( std::string( "</" ) + tag + ">" );
			return a == std::string::npos || b == std::string::npos ? std::string() : request.substr( a + open.size(), b - a - open.size() );
		};
		if ( request.compare( 0, 13, "GET /desc.xml" ) == 0 )
		{
			body = std::string( "<?xml version=\"1.0\"?><root><device><serviceList>" )
				+ (unusedServiceFirst ? "<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:2</serviceType>"
					"<controlURL>/ctl/Unused</controlURL></service>" : "")
				+ "<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
				"<controlURL>/ctl/IPConn</controlURL></service>"
				+ (firewallService ? "<service><serviceType>urn:schemas-upnp-org:service:WANIPv6FirewallControl:1</serviceType>"
					"<controlURL>/ctl/IP6FC</controlURL></service>" : "")
				+ "</serviceList></device></root>";
		}
		else if ( request.find( "#GetExternalIPAddress\"" ) != std::string::npos )
		{
			body = "<s:Envelope><s:Body><u:GetExternalIPAddressResponse><NewExternalIPAddress>" + externalAddress
				+ "</NewExternalIPAddress></u:GetExternalIPAddressResponse></s:Body></s:Envelope>";
		}
		else if ( request.find( "#AddPortMapping\"" ) != std::string::npos )
		{
			const UnsignedInt lease = static_cast<UnsignedInt>(std::strtoul( value( "NewLeaseDuration" ).c_str(), nullptr, 10 ));
			const bool unused = request.compare( 0, 16, "POST /ctl/Unused" ) == 0;
			if ( refuseCode != 0 || unused )
			{
				status = "500 Internal Server Error";
				body = "<s:Fault><detail><UPnPError><errorCode>" + std::to_string( unused ? 501 : refuseCode )
					+ "</errorCode></UPnPError></detail></s:Fault>";
			}
			else if ( permanentOnly && lease != 0 )
			{
				status = "500 Internal Server Error";
				body = "<s:Fault><detail><UPnPError><errorCode>725</errorCode></UPnPError></detail></s:Fault>";
			}
			else
			{
				upnpMappings[static_cast<UnsignedShort>(std::atoi( value( "NewExternalPort" ).c_str() ))] = lease;
				upnpClient = value( "NewInternalClient" );
				mappedAt = request.substr( 5, request.find( ' ', 5 ) - 5 );
				body = "<s:Envelope><s:Body><u:AddPortMappingResponse/></s:Body></s:Envelope>";
			}
		}
		else if ( request.find( "#GetFirewallStatus\"" ) != std::string::npos )
		{
			body = std::string( "<s:Envelope><s:Body><u:GetFirewallStatusResponse><FirewallEnabled>" ) + (firewallEnabled ? "1" : "0")
				+ "</FirewallEnabled><InboundPinholeAllowed>true</InboundPinholeAllowed></u:GetFirewallStatusResponse></s:Body></s:Envelope>";
		}
		else if ( request.find( "#AddPinhole\"" ) != std::string::npos )
		{
			if ( pinholeRefuseCode != 0 )
			{
				status = "500 Internal Server Error";
				body = "<s:Fault><detail><UPnPError><errorCode>" + std::to_string( pinholeRefuseCode )
					+ "</errorCode></UPnPError></detail></s:Fault>";
			}
			else
			{
				Pinhole hole;
				hole.client = value( "InternalClient" );
				hole.port = value( "InternalPort" );
				hole.protocol = value( "Protocol" );
				hole.lease = static_cast<UnsignedInt>(std::strtoul( value( "LeaseTime" ).c_str(), nullptr, 10 ));
				const int id = nextPinholeId++;
				pinholes[id] = hole;
				body = "<s:Envelope><s:Body><u:AddPinholeResponse><UniqueID>" + std::to_string( id )
					+ "</UniqueID></u:AddPinholeResponse></s:Body></s:Envelope>";
			}
		}
		else if ( request.find( "#DeletePinhole\"" ) != std::string::npos )
		{
			pinholes.erase( std::atoi( value( "UniqueID" ).c_str() ) );
			body = "<s:Envelope><s:Body><u:DeletePinholeResponse/></s:Body></s:Envelope>";
		}
		else if ( request.find( "#DeletePortMapping\"" ) != std::string::npos )
		{
			upnpMappings.erase( static_cast<UnsignedShort>(std::atoi( value( "NewExternalPort" ).c_str() )) );
			body = "<s:Envelope><s:Body><u:DeletePortMappingResponse/></s:Body></s:Envelope>";
		}
		else
		{
			status = "404 Not Found";
		}
		if ( keepAlive )
		{
			const std::string response = "HTTP/1.1 " + status + "\r\nContent-Length: " + std::to_string( body.size() ) + "\r\n\r\n" + body;
			send( c, response.data(), static_cast<int>(response.size()), 0 );
			m_open.push_back( c );	// closed only when the router stops
			return;
		}
		// chunked, as some routers answer
		char chunk[16];
		std::snprintf( chunk, sizeof( chunk ), "%x\r\n", static_cast<unsigned>(body.size()) );
		const std::string response = "HTTP/1.1 " + status + "\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
			+ chunk + body + "\r\n0\r\n\r\n";
		send( c, response.data(), static_cast<int>(response.size()), 0 );
		shutdown( c, SD_SEND );
		closesocket( c );
	}

	static void put32( unsigned char *p, UnsignedInt v )
	{
		p[0] = static_cast<unsigned char>(v >> 24);
		p[1] = static_cast<unsigned char>(v >> 16);
		p[2] = static_cast<unsigned char>(v >> 8);
		p[3] = static_cast<unsigned char>(v);
	}

	SOCKET m_udpNat = INVALID_SOCKET;
	SOCKET m_udpSsdp = INVALID_SOCKET;
	SOCKET m_tcp = INVALID_SOCKET;
	std::atomic<bool> m_stop{ false };
	std::thread m_thread;
	std::vector<SOCKET> m_open;
};

PortMapper::State waitMapper( PortMapper &mapper, UnsignedInt ms )
{
	const UnsignedInt start = timeGetTime();
	while ( mapper.state() == PortMapper::STATE_WORKING && timeGetTime() - start < ms )
		Sleep( 20 );
	return mapper.state();
}

PortMapper::PinholeState waitPinholes( PortMapper &mapper, UnsignedInt ms )
{
	const UnsignedInt start = timeGetTime();
	while ( mapper.pinholeState() == PortMapper::PINHOLE_WORKING && timeGetTime() - start < ms )
		Sleep( 20 );
	return mapper.pinholeState();
}

PortMapper::Settings routerSettings()
{
	PortMapper::Settings s;
	s.localIP = LOOPBACK;
	s.ports[0] = 28186;
	s.ports[1] = 28188;
	s.portCount = 2;
	s.gateway = LOOPBACK;
	s.natpmpPort = ROUTER_NATPMP;
	s.ssdpTarget = LOOPBACK;
	s.ssdpPort = ROUTER_SSDP;
	s.timeoutMs = 800;
	s.describeSecuritySoftware = false;	// one test below asks for it
	return s;
}

void testPortMapper()
{
	std::printf( "port mapping with a router on the loopback\n" );
	WSADATA wsa;
	WSAStartup( MAKEWORD( 2, 2 ), &wsa );	// the router's sockets; the transports may have cleaned up
	{
		FakeRouter router;
		check( router.start(), "the router listens" );
		PortMapper mapper;
		mapper.start( routerSettings() );
		check( waitMapper( mapper, 5000 ) == PortMapper::STATE_MAPPED && mapper.method() == PortMapper::METHOD_NATPMP,
			"NAT-PMP maps the ports" );
		check( mapper.externalIP() == ROUTER_EXTERNAL, "the external address comes from the router" );
		check( router.natpmpMappings.size() == 2 && router.natpmpMappings[28186] == 3600 && router.natpmpMappings[28188] == 3600,
			"both ports are forwarded for an hour" );
		mapper.stop();
		check( router.natpmpMappings.empty() && mapper.state() == PortMapper::STATE_OFF, "stop removes the NAT-PMP mappings" );
		router.stop();
	}
	{
		FakeRouter router;
		router.natpmp = false;
		router.start();
		PortMapper mapper;
		mapper.start( routerSettings() );
		check( waitMapper( mapper, 8000 ) == PortMapper::STATE_MAPPED && mapper.method() == PortMapper::METHOD_UPNP,
			"without NAT-PMP, UPnP maps the ports" );
		check( mapper.externalIP() == ROUTER_EXTERNAL && router.upnpClient == "127.0.0.1"
			&& router.upnpMappings.size() == 2 && router.upnpMappings[28188] == 3600, "UPnP: address, client and lease" );
		mapper.stop();
		check( router.upnpMappings.empty(), "stop removes the UPnP mappings" );
		router.stop();
	}
	{
		FakeRouter router;
		router.natpmp = false;
		router.permanentOnly = true;
		router.start();
		PortMapper mapper;
		mapper.start( routerSettings() );
		check( waitMapper( mapper, 8000 ) == PortMapper::STATE_MAPPED && router.upnpMappings.size() == 2 && router.upnpMappings[28186] == 0,
			"a router with permanent leases only gets one without end" );
		mapper.stop();
		check( router.upnpMappings.empty(), "and it is removed at the end" );
		router.stop();
	}
	{
		FakeRouter router;
		router.natpmp = false;
		router.location = "http://127.0.0.2:24900/desc.xml";	// not the router that answered
		router.start();
		PortMapper mapper;
		mapper.start( routerSettings() );
		check( waitMapper( mapper, 8000 ) == PortMapper::STATE_FAILED && router.httpRequests == 0,
			"a description on another host is not fetched" );
		mapper.stop();
		router.stop();
	}
	{
		FakeRouter router;
		router.natpmp = false;
		router.upnp = false;
		router.start();
		PortMapper mapper;
		mapper.start( routerSettings() );
		check( waitMapper( mapper, 12000 ) == PortMapper::STATE_FAILED && mapper.method() == PortMapper::METHOD_NONE
			&& mapper.failure() == PortMapper::FAILURE_NO_ROUTER, "without NAT-PMP and UPnP the mapper gives up: no router" );
		check( mapper.searchOutcome() == PortMapper::SEARCH_SILENT, "silent search and no description at the usual addresses: UPnP off" );
		mapper.stop();
		router.stop();
	}
	{
		FakeRouter router;
		router.natpmp = false;
		router.unusedServiceFirst = true;
		router.start();
		PortMapper mapper;
		mapper.start( routerSettings() );
		check( waitMapper( mapper, 8000 ) == PortMapper::STATE_MAPPED && router.upnpMappings.size() == 2 && router.mappedAt == "/ctl/IPConn",
			"a service that refuses is passed over for the next one" );
		mapper.stop();
		check( router.upnpMappings.empty(), "and the mappings of that one are removed" );
		router.stop();
	}
	{
		FakeRouter router;
		router.natpmp = false;
		router.refuseCode = 606;
		router.externalAddress = "192.0.0.2";	// DS-Lite: the router has no public address
		router.start();
		PortMapper mapper;
		mapper.start( routerSettings() );
		check( waitMapper( mapper, 8000 ) == PortMapper::STATE_FAILED && mapper.failure() == PortMapper::FAILURE_REFUSED
			&& mapper.refusalCode() == 606, "a refusal is told apart from silence, with its code" );
		check( mapper.externalIP() == 0xC0000002u && !::Engine::PortMapping::isPublicIPv4( mapper.externalIP() ),
			"the external address shows that IPv4 cannot be reached" );
		mapper.stop();
		router.stop();
	}
	{
		FakeRouter router;	// UPnP on, but its search answers never arrive (as if a firewall here dropped them)
		router.natpmp = false;
		router.upnp = false;
		router.start();
		PortMapper mapper;
		PortMapper::Settings settings = routerSettings();
		settings.directDescriptions.push_back( std::make_pair( ROUTER_HTTP, std::string( "/desc.xml" ) ) );
		mapper.start( settings );
		check( waitMapper( mapper, 8000 ) == PortMapper::STATE_MAPPED && mapper.method() == PortMapper::METHOD_UPNP
			&& router.upnpMappings.size() == 2, "a silent search: the gateway asked directly forwards the ports" );
		check( mapper.searchOutcome() == PortMapper::SEARCH_FILTERED, "and the silence is put down to this computer" );
		mapper.stop();
		router.stop();
	}
	{
		FakeRouter router;	// failing, with the security software in the log (COM under Wine)
		router.natpmp = false;
		router.upnp = false;
		router.start();
		PortMapper mapper;
		PortMapper::Settings settings = routerSettings();
		settings.describeSecuritySoftware = true;
		settings.directDescriptions.push_back( std::make_pair( static_cast<UnsignedShort>(1), std::string( "/none.xml" ) ) );
		const UnsignedInt start = timeGetTime();
		mapper.start( settings );
		check( waitMapper( mapper, 20000 ) == PortMapper::STATE_FAILED, "the security software does not hold the mapper up" );
		bool named = false;
		for ( const std::string &line : mapper.log() )
			named = named || line.find( "Windows Firewall" ) != std::string::npos || line.find( "security software" ) != std::string::npos;
		check( named, "the log names the firewall of this computer" );
		std::printf( "  (security software described in %u ms)\n", timeGetTime() - start );
		mapper.stop();
		router.stop();
	}
	{
		FakeRouter router;	// keeps every connection open; the mapper must not wait for the end
		router.natpmp = false;
		router.keepAlive = true;
		router.location = "http://localhost:24900/desc.xml";	// names itself rather than its address
		router.start();
		PortMapper mapper;
		const UnsignedInt start = timeGetTime();
		mapper.start( routerSettings() );
		check( waitMapper( mapper, 8000 ) == PortMapper::STATE_MAPPED && router.upnpMappings.size() == 2,
			"a router that keeps the connection open and names itself" );
		check( timeGetTime() - start < 2500, "and the answers count as they come, not at the time limit" );
		bool logged = false;
		for ( const std::string &line : mapper.log() )
			logged = logged || line.find( "UPnP search: answer from 127.0.0.1, http://localhost:24900/desc.xml" ) != std::string::npos;
		check( logged, "the log names the router that answered" );
		mapper.stop();
		router.stop();
	}
	{
		FakeRouter router;
		router.natpmp = false;
		router.upnp = false;
		router.start();
		PortMapper mapper;
		mapper.start( routerSettings() );
		waitMapper( mapper, 8000 );
		bool told = false;
		for ( const std::string &line : mapper.log() )
			told = told || line.find( "UPnP search: no answer" ) != std::string::npos;
		check( told, "the log says when no router answers the search" );
		mapper.stop();
		router.stop();
	}
	{
		FakeRouter router;
		router.natpmp = false;
		router.firewallService = true;
		router.start();
		PortMapper mapper;
		PortMapper::Settings settings = routerSettings();
		settings.localIPv6 = "2001:db8::5";
		mapper.start( settings );
		check( waitMapper( mapper, 8000 ) == PortMapper::STATE_MAPPED && waitPinholes( mapper, 8000 ) == PortMapper::PINHOLE_OPEN,
			"IPv4 forwarded and IPv6 pinholes open" );
		bool right = router.pinholes.size() == 2;
		for ( const auto &hole : router.pinholes )
			right = right && hole.second.client == "2001:db8::5" && hole.second.protocol == "17" && hole.second.lease == 3600
				&& (hole.second.port == "28186" || hole.second.port == "28188");
		check( right, "a pinhole per port: UDP to the address given, for an hour" );
		mapper.stop();
		check( router.pinholes.empty() && mapper.pinholeState() == PortMapper::PINHOLE_OFF, "stop closes the pinholes" );
		router.stop();
	}
	{
		FakeRouter router;	// NAT-PMP for IPv4, UPnP only for the pinholes
		router.firewallService = true;
		router.start();
		PortMapper mapper;
		PortMapper::Settings settings = routerSettings();
		settings.localIPv6 = "2001:db8::5";
		mapper.start( settings );
		check( waitMapper( mapper, 8000 ) == PortMapper::STATE_MAPPED && mapper.method() == PortMapper::METHOD_NATPMP
			&& waitPinholes( mapper, 8000 ) == PortMapper::PINHOLE_OPEN && router.pinholes.size() == 2,
			"pinholes also when NAT-PMP forwarded IPv4" );
		mapper.stop();
		router.stop();
	}
	{
		FakeRouter router;	// DS-Lite: no IPv4 forwarding at all, but the IPv6 firewall opens
		router.natpmp = false;
		router.refuseCode = 606;
		router.firewallService = true;
		router.start();
		PortMapper mapper;
		PortMapper::Settings settings = routerSettings();
		settings.localIPv6 = "2001:db8::5";
		mapper.start( settings );
		check( waitMapper( mapper, 8000 ) == PortMapper::STATE_FAILED && waitPinholes( mapper, 8000 ) == PortMapper::PINHOLE_OPEN,
			"pinholes without an IPv4 mapping" );
		mapper.stop();
		check( router.pinholes.empty(), "and they are closed at the end" );
		router.stop();
	}
	{
		FakeRouter router;
		router.natpmp = false;
		router.firewallService = true;
		router.pinholeRefuseCode = 606;
		router.start();
		PortMapper mapper;
		PortMapper::Settings settings = routerSettings();
		settings.localIPv6 = "2001:db8::5";
		mapper.start( settings );
		check( waitMapper( mapper, 8000 ) == PortMapper::STATE_MAPPED && waitPinholes( mapper, 8000 ) == PortMapper::PINHOLE_REFUSED
			&& mapper.pinholeRefusalCode() == 606 && router.pinholes.empty(), "a refused pinhole is told with its code" );
		mapper.stop();
		router.stop();
	}
	{
		FakeRouter router;
		router.natpmp = false;
		router.firewallService = true;
		router.firewallEnabled = false;
		router.start();
		PortMapper mapper;
		PortMapper::Settings settings = routerSettings();
		settings.localIPv6 = "2001:db8::5";
		mapper.start( settings );
		check( waitPinholes( mapper, 8000 ) == PortMapper::PINHOLE_NOT_NEEDED && router.pinholes.empty(),
			"no pinholes with the IPv6 firewall off" );
		mapper.stop();
		router.stop();
	}
	{
		FakeRouter router;
		router.natpmp = false;
		router.start();
		PortMapper mapper;
		PortMapper::Settings settings = routerSettings();
		settings.localIPv6 = "2001:db8::5";
		mapper.start( settings );
		check( waitPinholes( mapper, 8000 ) == PortMapper::PINHOLE_NO_SERVICE, "a router without the IPv6 firewall service" );
		mapper.stop();
		router.stop();
	}
	WSACleanup();
}

// Answers leave from the address the peer wrote to (IPV6_PKTINFO), on the loopback the only one.
void testIPv6Reply()
{
	std::printf( "IPv6 answers from the address written to\n" );
	UdpSocket a;
	UdpSocket b;
	if ( !a.open( LOOPBACK, 28120, TRUE ) || !b.open( LOOPBACK, 28121, TRUE ) || !a.hasIPv6() || !b.hasIPv6() )
	{
		std::printf( "  skipped: no IPv6 here\n" );
		return;
	}
	const std::uint8_t loopback6[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
	const unsigned char ping[] = "ping";
	check( a.send( ping, 4, ::Engine::NetAddress::fromIPv6( loopback6, 28121 ) ) == 4, "send over IPv6" );
	unsigned char buffer[64];
	::Engine::NetAddress from;
	Int got = 0;
	for ( UnsignedInt start = timeGetTime(); got <= 0 && timeGetTime() - start < 2000; Sleep( 5 ) )
		got = b.receive( buffer, sizeof( buffer ), from );
	check( got == 4 && from.isV6() && from.port == 28120, "received with the sender's address" );
	const unsigned char pong[] = "pong";
	check( b.send( pong, 4, from ) == 4, "the answer goes out from the address written to" );
	got = 0;
	for ( UnsignedInt start = timeGetTime(); got <= 0 && timeGetTime() - start < 2000; Sleep( 5 ) )
		got = a.receive( buffer, sizeof( buffer ), from );
	check( got == 4 && from == ::Engine::NetAddress::fromIPv6( loopback6, 28121 ), "and arrives from it" );
}

// --- sessions: lobby, join, invitation and relay over real sockets ---------------------------

/// A lobby instance without menus; several run in one process on their own ports.
class TestLAN : public GameLobby
{
public:
	TestLAN( const wchar_t *name, UnsignedShort lobbyPort, UnsignedShort gamePort )
	{
		setPortsForTest( lobbyPort, gamePort );
		::Engine::context().lobby = this;
		init();
		SetLocalIP( LOOPBACK );
		m_name = name;
		m_localIP = LOOPBACK;
		addSelfToLobby();
	}

	void step()
	{
		::Engine::context().lobby = this;
		m_lastUpdate = 0;
		if ( m_isInLANMenu )
			update();
		m_lastUpdate = 0;
		serviceInGame();
	}

	/// As after OnGameStart: the game runs, the lobby menus are gone.
	void enterGameForTest()
	{
		if ( m_currentGame )
			m_currentGame->startGame( 0 );
		m_isInLANMenu = FALSE;
	}
	Bool watchCompleteForTest() const { return m_watch.active && m_watch.ended && m_watch.receiver.received() >= m_watch.endBytes; }
	void keepTicketForTest() { ::Engine::context().lobby = this; keepReturnTicket(); }
	UnsignedInt returnBytesForTest() const { return m_return.receiver.received(); }
	Bool returnHeaderForTest() const { return m_return.headerRead; }
	Bool returnStateForTest() const { return m_return.stateApplied; }
	ReturnFeed &returnFeedForTest() { return m_returnFeed; }
	const UnsignedByte *publicKeyForTest() const { return m_keyPair.publicKey; }
	Bool returnDoneForTest( Int slot ) const
	{
		for ( const Spectator &s : m_watchHost.spectators )
			if ( s.returnSlot == slot )
				return s.done;
		return FALSE;
	}

	Bool joining() const { return m_join.active; }
	/// Players in the lobby other than us (the test lobby also listed itself under the loopback).
	Int otherLobbyPlayers() const
	{
		Int n = 0;
		for ( LANPlayer *p = m_directory.players(); p != nullptr; p = p->getNext() )
			if ( p->getIP() != LOOPBACK && p->getIP() != m_localIP )
				++n;
		return n;
	}
	UnsignedInt lanAddressForTest() const { return lanAddress(); }
	Int localSlot() { ::Engine::context().lobby = this; return m_currentGame ? m_currentGame->getLocalSlotNum() : -1; }

	Int m_chats = 0;
	Int m_starts = 0;
	ReturnType m_joinResult = RET_MAX;

	virtual void OnGameList( LANGameInfo * ) override {}
	virtual void OnPlayerList( LANPlayer * ) override {}
	virtual void OnGameJoin( ReturnType ret, LANGameInfo * ) override { m_joinResult = ret; }
	virtual void OnPlayerJoin( Int, UnicodeString ) override
	{
		if ( m_currentGame )
			RequestGameOptions( GenerateGameOptionsString(), true );
	}
	virtual void OnHostLeave( void ) override {}
	virtual void OnPlayerLeave( UnicodeString ) override {}
	virtual void OnAccept( UnsignedInt, Bool ) override {}
	virtual void OnHasMap( UnsignedInt, Bool ) override {}
	virtual void OnChat( UnicodeString, UnsignedInt, UnicodeString, ChatType format ) override
	{
		if ( format != LANCHAT_SYSTEM )
			++m_chats;
	}
	virtual void OnGameStart( void ) override { ++m_starts; }
	virtual void OnGameStartTimer( Int ) override {}
	virtual void OnGameCreate( ReturnType ) override {}
	virtual void OnNameChange( UnsignedInt, UnicodeString ) override {}
	virtual void OnInActive( UnsignedInt ) override {}
	virtual void checkMOTD( void ) override {}
};

/// A rendezvous server on the loopback, serviced from the test loop.
class LoopServer
{
public:
	::Engine::RendezvousServer server;
	SOCKET s = INVALID_SOCKET;

	bool start( UnsignedShort port )
	{
		WSADATA wsa;
		WSAStartup( MAKEWORD( 2, 2 ), &wsa );	// the transports clean up after themselves
		s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
		sockaddr_in a;
		std::memset( &a, 0, sizeof( a ) );
		a.sin_family = AF_INET;
		a.sin_addr.s_addr = htonl( LOOPBACK );
		a.sin_port = htons( port );
		unsigned long nonBlocking = 1;
		return s != INVALID_SOCKET && bind( s, reinterpret_cast<sockaddr *>(&a), sizeof( a ) ) == 0 && ioctlsocket( s, FIONBIO, &nonBlocking ) == 0;
	}

	void service()
	{
		unsigned char buf[2048];
		for ( int n = 0; n < 256; ++n )
		{
			sockaddr_in from;
			int fromLen = sizeof( from );
			const int got = recvfrom( s, reinterpret_cast<char *>(buf), sizeof( buf ), 0, reinterpret_cast<sockaddr *>(&from), &fromLen );
			if ( got <= 0 )
				break;
			std::vector<::Engine::RendezvousServer::Datagram> out;
			server.handle( ::Engine::WireEndpoint::ipv4( ntohl( from.sin_addr.s_addr ), ntohs( from.sin_port ) ),
				buf, static_cast<std::size_t>(got), timeGetTime(), out );
			for ( const auto &d : out )
			{
				sockaddr_in to;
				std::memset( &to, 0, sizeof( to ) );
				to.sin_family = AF_INET;
				to.sin_addr.s_addr = htonl( d.to.ipv4Address() );
				to.sin_port = htons( d.to.port );
				sendto( s, reinterpret_cast<const char *>(d.data.data()), static_cast<int>(d.data.size()), 0, reinterpret_cast<sockaddr *>(&to), sizeof( to ) );
			}
		}
		server.expire( timeGetTime() );
	}

	~LoopServer()
	{
		if ( s != INVALID_SOCKET )
			closesocket( s );
		WSACleanup();
	}
};

template <typename Condition>
bool runUntil( std::vector<TestLAN *> &lans, LoopServer *server, UnsignedInt ms, Condition done )
{
	const UnsignedInt start = timeGetTime();
	while ( timeGetTime() - start < ms )
	{
		for ( TestLAN *l : lans )
			l->step();
		if ( server )
			server->service();
		if ( done() )
			return true;
		Sleep( 5 );
	}
	return done();
}

void hostGame( TestLAN &host )
{
	::Engine::context().lobby = &host;
	host.RequestGameCreate( UnicodeString( L"Test" ), FALSE );
	LANGameInfo *game = host.GetMyGame();
	if ( game == nullptr )
		return;
	AsciiString map = TheMapCache->getMapDir();
	map.concat( "\\alpine\\alpine.map" );
	game->setMap( map );
	for ( Int i = 1; i < MAX_SLOTS; ++i )
		game->getSlot( i )->setState( SLOT_OPEN );
	host.RequestGameOptions( GenerateGameOptionsString(), true );
}

/// Every member reaches the game socket of every other one (directly or relayed).
bool allReach( std::vector<TestLAN *> &lans, bool relayed )
{
	for ( TestLAN *l : lans )
	{
		LANGameInfo *game = l->GetMyGame();
		if ( game == nullptr )
			return false;
		for ( Int s = 0; s < MAX_SLOTS; ++s )
		{
			if ( s == l->localSlot() || !game->getConstSlot( s )->isHuman() )
				continue;
			if ( relayed ? !l->isGameRelayed( s ) : !l->isGameReachable( s ) )
				return false;
		}
	}
	return true;
}

void testLanSession()
{
	TheNetAddressBook().clear();
	std::printf( "LAN session: search, join, chat, start\n" );
	TestLAN host( L"Host", 28201, 28202 );
	TestLAN alice( L"Alice", 28211, 28212 );
	TestLAN bob( L"Bob", 28221, 28222 );
	std::vector<TestLAN *> lans = { &host, &alice, &bob };
	std::vector<::Engine::NetAddress> targets;
	for ( UnsignedShort p : { 28201, 28211, 28221 } )
		targets.push_back( ::Engine::NetAddress::fromIPv4( LOOPBACK, p ) );
	for ( TestLAN *l : lans )
	{
		l->setDiscoveryTargetsForTest( targets );
		l->setRendezvousServers( std::vector<std::string>() );
	}
	hostGame( host );
	check( host.GetMyGame() != nullptr && host.getGameTransport() != nullptr, "the host opens a game and its game socket" );
	::Engine::context().lobby = &alice;
	alice.RequestLocations();
	::Engine::context().lobby = &bob;
	bob.RequestLocations();
	check( runUntil( lans, nullptr, 3000, [&] { return alice.LookupGameByListOffset( 0 ) && bob.LookupGameByListOffset( 0 ); } ),
		"the others list the game" );
	LANGameInfo *listed = alice.LookupGameByListOffset( 0 );
	check( listed != nullptr && listed->getConstSlot( 0 )->isHuman() && listed->getPlayerName( 0 ) == UnicodeString( L"Host" ),
		"the list shows the host (options in the announcement)" );

	::Engine::context().lobby = &alice;
	alice.RequestGameJoin( alice.LookupGameByListOffset( 0 ) );
	check( runUntil( lans, nullptr, 5000, [&] { return !alice.joining(); } ) && alice.m_joinResult == GameLobbyInterface::RET_OK,
		"Alice joins" );
	::Engine::context().lobby = &bob;
	bob.RequestGameJoin( bob.LookupGameByListOffset( 0 ) );
	check( runUntil( lans, nullptr, 5000, [&] { return !bob.joining(); } ) && bob.m_joinResult == GameLobbyInterface::RET_OK,
		"Bob joins" );
	LANGameInfo *game = host.GetMyGame();
	check( game != nullptr && game->getConstSlot( 1 )->isHuman() && game->getConstSlot( 2 )->isHuman()
		&& ::Engine::NetAddressBook::isHandle( game->getIP( 1 ) ) && game->getIP( 1 ) != game->getIP( 2 ),
		"the host lists both, under the identities of their keys" );
	check( runUntil( lans, nullptr, 4000, [&] { return alice.GetMyGame() && alice.GetMyGame()->getConstSlot( 2 )->isHuman()
		&& bob.GetMyGame() && bob.GetMyGame()->getConstSlot( 1 )->isHuman(); } ), "the state reaches both" );
	check( runUntil( lans, nullptr, 5000, [&] { return allReach( lans, false ); } ), "every game socket reaches every other directly" );

	::Engine::context().lobby = &alice;
	alice.RequestChat( UnicodeString( L"hi" ), GameLobbyInterface::LANCHAT_NORMAL );
	check( runUntil( lans, nullptr, 2000, [&] { return bob.m_chats == 1 && host.m_chats == 1; } ), "chat goes through the host to all" );

	::Engine::context().lobby = &host;
	host.RequestGameStart();
	check( runUntil( lans, nullptr, 2000, [&] { return alice.m_starts >= 1 && bob.m_starts >= 1; } ), "the start reaches all" );

	::Engine::context().lobby = &bob;
	bob.RequestGameLeave();
	check( runUntil( lans, nullptr, 2000, [&] { return game && !game->getConstSlot( 2 )->isHuman(); } ), "a player leaves" );
	for ( TestLAN *l : lans )
	{
		::Engine::context().lobby = l;
		l->reset();
	}
	::Engine::context().lobby = nullptr;
}

// The games the lobbies of the Internet mode list: those the servers list publicly.
Int listedGames( TestLAN &lan, LANGameInfo **first = nullptr )
{
	::Engine::context().lobby = &lan;
	Int n = 0;
	while ( LANGameInfo *game = lan.LookupGameByListOffset( n ) )
	{
		if ( n == 0 && first )
			*first = game;
		++n;
	}
	return n;
}

void testInternetSession()
{
	TheNetAddressBook().clear();
	std::printf( "internet lobby: the public games of the rendezvous servers\n" );
	LoopServer server;
	check( server.start( 28390 ), "the rendezvous server listens" );
	TestLAN host( L"Host", 28331, 28332 );
	TestLAN quiet( L"Quiet", 28341, 28342 );
	TestLAN alice( L"Alice", 28351, 28352 );
	TestLAN carl( L"Carl", 28361, 28362 );
	std::vector<TestLAN *> lans = { &host, &quiet, &alice, &carl };
	std::vector<std::string> servers;
	servers.push_back( "127.0.0.1:28390" );
	for ( TestLAN *l : lans )
	{
		::Engine::context().lobby = l;
		l->setRendezvousServers( servers );
		l->setMode( GameLobby::Mode::Internet );
	}
	hostGame( host );
	host.setListedPublicly( TRUE );
	hostGame( quiet );	// private: joined by code only
	check( runUntil( lans, &server, 3000, [&] { return !host.getInviteCode().isEmpty() && !quiet.getInviteCode().isEmpty(); } ),
		"both hosts have a room" );

	LANGameInfo *listed = nullptr;
	check( runUntil( lans, &server, 12000, [&] { return listedGames( alice ) == 1; } ) && listedGames( alice, &listed ) == 1,
		"the public game is listed, the private one is not" );
	check( listed && listed->getPlayerName( 0 ) == UnicodeString( L"Host" ) && listed->getListedCode() == host.getInviteCode(),
		"the listing names the host and joins by its invitation code" );

	::Engine::context().lobby = &alice;
	alice.RequestGameJoin( listed );
	check( runUntil( lans, &server, 8000, [&] { return !alice.joining(); } ) && alice.m_joinResult == GameLobbyInterface::RET_OK,
		"Alice joins the listed game" );

	check( listedGames( carl ) == 1, "Carl sees the game as well" );
	::Engine::context().lobby = &host;
	host.setListedPublicly( FALSE );
	check( runUntil( lans, &server, 12000, [&] { return listedGames( carl ) == 0; } ), "a game no longer listed goes from the list" );

	::Engine::context().lobby = &carl;
	carl.RequestChat( UnicodeString( L"anybody?" ), GameLobbyInterface::LANCHAT_NORMAL );
	check( carl.m_chats == 0, "no chat outside a game on the internet (only a notice)" );

	carl.RequestChat( UnicodeString( L"/server add 192.0.2.7:9000" ), GameLobbyInterface::LANCHAT_NORMAL );
	const std::vector<std::string> &known = carl.getRendezvousServers();
	check( known.size() == 2 && known.back() == "192.0.2.7:9000", "/server add puts a server on the list" );
	carl.RequestChat( UnicodeString( L"/server add not a host!" ), GameLobbyInterface::LANCHAT_NORMAL );
	carl.RequestChat( UnicodeString( L"/server remove 192.0.2.7:9000" ), GameLobbyInterface::LANCHAT_NORMAL );
	check( carl.getRendezvousServers().size() == 1, "/server remove takes it off again, nonsense is refused" );
	::Engine::context().lobby = &host;
	host.RequestChat( UnicodeString( L"/public" ), GameLobbyInterface::LANCHAT_NORMAL );
	check( host.listedPublicly() && host.m_chats == 0, "/public lists the host's game, as a command, not as chat" );
	::Engine::context().lobby = &carl;
	carl.RequestChat( UnicodeString( L"/public" ), GameLobbyInterface::LANCHAT_NORMAL );
	check( !carl.listedPublicly(), "/public outside a hosted game changes nothing" );

	for ( TestLAN *l : lans )
	{
		::Engine::context().lobby = l;
		l->reset();
	}
	::Engine::context().lobby = nullptr;
}

// Bound to every adapter (no address chosen): the adapters' networks are searched, our own
// announcements coming back are no other player.
void testEveryAdapter()
{
	std::printf( "LAN lobby on every adapter\n" );
	const std::vector<::Engine::LocalNetwork> networks = LocalIPv4Networks();
	for ( const ::Engine::LocalNetwork &n : networks )
		std::printf( "  adapter %u.%u.%u.%u/%u\n", n.address >> 24, (n.address >> 16) & 0xFF, (n.address >> 8) & 0xFF, n.address & 0xFF,
			static_cast<unsigned>(n.prefixLength) );
	check( std::all_of( networks.begin(), networks.end(), []( const ::Engine::LocalNetwork &n ) { return (n.address >> 24) != 127; } ),
		"the adapters are listed without loopback" );
	TestLAN lan( L"Solo", 28471, 28472 );
	::Engine::context().lobby = &lan;
	check( lan.SetLocalIP( 0 ), "the lobby opens on every adapter" );
	check( lan.lanAddressForTest() == (networks.empty() ? 0 : networks.front().address), "the first adapter names us" );
	std::vector<TestLAN *> lans = { &lan };
	for ( int i = 0; i < 3; ++i )
	{
		lan.RequestSetName( UnicodeString( L"Solo" ) );	// announces itself
		runUntil( lans, nullptr, 300, [] { return false; } );
	}
	check( lan.otherLobbyPlayers() == 0, "our own announcements are no other player" );
	lan.reset();
	::Engine::context().lobby = nullptr;
}

void testInviteSession()
{
	TheNetAddressBook().clear();
	std::printf( "session by invitation over a rendezvous server, relayed\n" );
	LoopServer server;
	check( server.start( 28290 ), "the rendezvous server listens" );
	TestLAN host( L"Host", 28231, 28232 );
	TestLAN alice( L"Alice", 28241, 28242 );
	TestLAN bob( L"Bob", 28251, 28252 );
	std::vector<TestLAN *> lans = { &host, &alice, &bob };
	std::vector<::Engine::NetAddress> none;
	none.push_back( ::Engine::NetAddress::fromIPv4( LOOPBACK, 9 ) );	// no LAN search
	std::vector<std::string> servers;
	servers.push_back( "127.0.0.1:28290" );
	for ( TestLAN *l : lans )
	{
		l->setDiscoveryTargetsForTest( none );
		l->setRendezvousServers( servers );
		l->forceRelayForTest( TRUE );
	}
	hostGame( host );
	check( runUntil( lans, &server, 3000, [&] { return !host.getInviteCode().isEmpty(); } ), "the host gets an invitation code" );
	std::printf( "  code %s\n", host.getInviteCode().str() );

	::Engine::context().lobby = &alice;
	check( alice.RequestGameJoinInvite( host.getInviteCode() ), "the code is read" );
	check( runUntil( lans, &server, 8000, [&] { return !alice.joining(); } ) && alice.m_joinResult == GameLobbyInterface::RET_OK,
		"Alice joins by the code" );
	// the code without server: the known servers are asked in turn
	AsciiString shortCode = host.getInviteCode();
	const char *at = strchr( shortCode.str(), '@' );
	AsciiString bare;
	for ( const char *p = shortCode.str(); p != at && *p; ++p )
		bare.concat( *p );
	::Engine::context().lobby = &bob;
	check( bob.RequestGameJoinInvite( bare ), "the code without server is read" );
	check( runUntil( lans, &server, 8000, [&] { return !bob.joining(); } ) && bob.m_joinResult == GameLobbyInterface::RET_OK,
		"Bob joins by the code without server" );

	check( runUntil( lans, &server, 8000, [&] { return allReach( lans, true ); } ), "every game socket is bound to the relay" );
	check( server.server.relayCount() == 1, "one relay session at the server" );

	// a game packet from Alice to Bob goes through the relay
	LANGameInfo *game = alice.GetMyGame();
	const UnsignedInt bobIdentity = game ? game->getIP( bob.localSlot() ) : 0;
	const UnsignedInt aliceIdentity = game ? game->getIP( alice.localSlot() ) : 0;
	unsigned char payload[40];
	std::memset( payload, 0x5A, sizeof( payload ) );
	const std::uint64_t relayedBefore = server.server.stats().relayed;
	if ( alice.getGameTransport() )
	{
		alice.getGameTransport()->queueSend( bobIdentity, 28252, payload, sizeof( payload ) );
		alice.getGameTransport()->update();
	}
	bool arrived = runUntil( lans, &server, 2000, [&] {
		Transport *t = bob.getGameTransport();
		for ( Int i = 0; t && i < MAX_MESSAGES; ++i )
			if ( t->m_inBuffer[i].length > 0 && t->m_inBuffer[i].addr == aliceIdentity )
				return true;
		return false;
	} );
	check( arrived && server.server.stats().relayed > relayedBefore, "a game packet arrives through the relay, from Alice's identity" );

	// an unknown room
	TestLAN carl( L"Carl", 28261, 28262 );
	carl.setDiscoveryTargetsForTest( none );
	carl.setRendezvousServers( servers );
	lans.push_back( &carl );
	::Engine::context().lobby = &carl;
	carl.RequestGameJoinInvite( AsciiString( "ZZZZ-00000000@127.0.0.1:28290" ) );
	check( runUntil( lans, &server, 8000, [&] { return !carl.joining(); } ) && carl.m_joinResult == GameLobbyInterface::RET_NOT_FOUND,
		"an unknown code ends with 'not found'" );
	for ( TestLAN *l : lans )
	{
		::Engine::context().lobby = l;
		l->reset();
	}
	::Engine::context().lobby = nullptr;
}

// Spectators: a running game in the LAN list is watched; the host streams its growing replay file.
void testWatchSession()
{
	TheNetAddressBook().clear();
	std::printf( "spectator of a running game\n" );
	TestLAN host( L"Host", 28301, 28302 );
	TestLAN alice( L"Alice", 28311, 28312 );
	TestLAN carl( L"Carl", 28321, 28322 );
	TestLAN dave( L"Dave", 28331, 28332 );
	std::vector<TestLAN *> lans = { &host, &alice, &carl, &dave };
	std::vector<::Engine::NetAddress> targets;
	for ( UnsignedShort p : { 28301, 28311, 28321, 28331 } )
		targets.push_back( ::Engine::NetAddress::fromIPv4( LOOPBACK, p ) );
	for ( TestLAN *l : lans )
	{
		l->setDiscoveryTargetsForTest( targets );
		l->setRendezvousServers( std::vector<std::string>() );
		l->setWatchPlaybackForTest( FALSE );
	}

	// the "replay" the host records: random bytes, growing
	const char *source = "watch_source.rep";
	std::vector<unsigned char> replay( 30000 );
	for ( size_t i = 0; i < replay.size(); ++i )
		replay[i] = static_cast<unsigned char>( ( i * 131 + 17 ) ^ ( i >> 7 ) );
	FILE *out = std::fopen( source, "wb" );
	std::fwrite( replay.data(), 1, replay.size(), out );
	std::fflush( out );

	hostGame( host );
	::Engine::context().lobby = &alice;
	alice.RequestLocations();
	check( runUntil( lans, nullptr, 3000, [&] { return alice.LookupGameByListOffset( 0 ) != nullptr; } ), "Alice lists the game" );
	alice.RequestGameJoin( alice.LookupGameByListOffset( 0 ) );
	check( runUntil( lans, nullptr, 5000, [&] { return !alice.joining(); } ) && alice.m_joinResult == GameLobbyInterface::RET_OK, "Alice joins as player" );
	::Engine::context().lobby = &host;
	host.RequestGameStart();
	host.enterGameForTest();
	host.setWatchSourceForTest( AsciiString( source ), 100 );
	::Engine::context().lobby = &alice;
	alice.enterGameForTest();

	// Carl finds the running game and watches it
	::Engine::context().lobby = &carl;
	carl.RequestLocations();
	check( runUntil( lans, nullptr, 8000, [&] {
		LANGameInfo *g = carl.LookupGameByListOffset( 0 );
		return g != nullptr && g->isGameInProgress(); } ), "the running game is listed as running" );
	carl.RequestGameJoin( carl.LookupGameByListOffset( 0 ) );
	check( runUntil( lans, nullptr, 5000, [&] { return carl.isWatching(); } ), "Carl watches" );
	check( host.spectatorCount() == 1 && carl.m_joinResult == GameLobbyInterface::RET_MAX, "the host has one spectator, no slot taken" );
	check( runUntil( lans, nullptr, 5000, [&] { return carl.watchedBytesForTest() == replay.size() && carl.watchedFramesForTest() == 100; } ),
		"the replay arrives with its mark" );

	// the game goes on: more bytes, more frames
	std::vector<unsigned char> more( 7000, 0x77 );
	std::fwrite( more.data(), 1, more.size(), out );
	std::fflush( out );
	host.setWatchSourceForTest( AsciiString( source ), 160 );
	check( runUntil( lans, nullptr, 5000, [&] { return carl.watchedBytesForTest() == replay.size() + more.size() && carl.watchedFramesForTest() == 160; } ),
		"new bytes and frames follow" );

	// a delay holds the newest frames back
	host.setWatchDelayFrames( 50 );
	std::fwrite( more.data(), 1, 100, out );
	std::fflush( out );
	host.setWatchSourceForTest( AsciiString( source ), 300 );
	runUntil( lans, nullptr, 600, [] { return false; } );
	check( carl.watchedBytesForTest() == replay.size() + more.size() && carl.watchedFramesForTest() == 160, "with a delay the new part waits" );
	host.setWatchDelayFrames( 0 );
	check( runUntil( lans, nullptr, 3000, [&] { return carl.watchedFramesForTest() == 300; } ), "without the delay it follows" );

	// the written copy is the host's file (one spectator per process: the next one starts the file anew)
	AsciiString copy = TheRecorder->getReplayDir();
	copy.concat( "WatchedGame" );
	copy.concat( TheRecorder->getReplayExtention() );
	FILE *in = std::fopen( copy.str(), "rb" );
	std::vector<unsigned char> got( 64000 );
	const size_t n = in ? std::fread( got.data(), 1, got.size(), in ) : 0;
	if ( in )
		std::fclose( in );
	check( n == replay.size() + more.size() + 100 && std::memcmp( got.data(), replay.data(), replay.size() ) == 0
		&& got[replay.size()] == 0x77, "the spectator's file holds the host's bytes" );

	// Dave connects by address while the game runs: he becomes a spectator, not a player
	::Engine::context().lobby = &dave;
	dave.RequestGameJoinAt( ::Engine::NetAddress::fromIPv4( LOOPBACK, 28301 ) );
	check( runUntil( lans, nullptr, 5000, [&] { return dave.isWatching(); } ) && host.spectatorCount() == 2, "a direct connect to a running game watches" );

	// the recording ends: the stream is complete
	std::fclose( out );
	host.setWatchSourceForTest( AsciiString(), 300 );
	check( runUntil( lans, nullptr, 5000, [&] { return carl.watchCompleteForTest(); } ), "the end of the recording reaches the spectator" );

	// a spectator leaves
	::Engine::context().lobby = &dave;
	dave.reset();
	check( runUntil( lans, nullptr, 3000, [&] { return host.spectatorCount() == 1; } ), "the host drops a spectator that leaves" );

	for ( TestLAN *l : lans )
	{
		::Engine::context().lobby = l;
		l->reset();
	}
	::Engine::context().lobby = nullptr;
	std::remove( source );
}

// The lockstep as far as the lobby's part of a return asks it; both the host and the returning player
// talk to this one (one network per process).
class ReturnTestNetwork final : public NetworkInterface
{
public:
	Int returnable = 1;
	Int announced = -1;
	UnsignedByte announcedKey[32] = {};
	::Engine::WireEndpoint announcedEndpoint;
	UnsignedInt toldFrame = 0;
	UnsignedInt toldPrefill = 0;
	UnsignedInt toldRouter = 99;
	UnsignedInt appliedFrame = 0;

	void init( void ) override {}
	void reset( void ) override {}
	void update( void ) override {}
	void liteupdate( void ) override {}
	void setLocalAddress( UnsignedInt, UnsignedInt ) override {}
	Bool isFrameDataReady( void ) override { return FALSE; }
	void parseUserList( const GameInfo * ) override {}
	void startGame( void ) override {}
	UnsignedInt getRunAhead( void ) override { return 10; }
	UnsignedInt getFrameRate( void ) override { return 30; }
	UnsignedInt getPacketArrivalCushion( void ) override { return 0; }
	void sendChat( UnicodeString, Int ) override {}
	void sendDisconnectChat( UnicodeString ) override {}
	void sendFile( AsciiString, UnsignedByte, UnsignedShort ) override {}
	UnsignedShort sendFileAnnounce( AsciiString, UnsignedByte ) override { return 0; }
	Int getFileTransferProgress( Int, AsciiString ) override { return 0; }
	Bool areAllQueuesEmpty( void ) override { return TRUE; }
	void quitGame( void ) override {}
	void voteForPlayerDisconnect( Int ) override {}
	Bool voteToRemovePlayer( Int ) override { return FALSE; }
	Bool isPacketRouter( void ) override { return TRUE; }
	UnsignedInt getPacketRouterSlot( void ) override { return 0; }
	Real getIncomingBytesPerSecond( void ) override { return 0.0f; }
	Real getIncomingPacketsPerSecond( void ) override { return 0.0f; }
	Real getOutgoingBytesPerSecond( void ) override { return 0.0f; }
	Real getOutgoingPacketsPerSecond( void ) override { return 0.0f; }
	Real getUnknownBytesPerSecond( void ) override { return 0.0f; }
	Real getUnknownPacketsPerSecond( void ) override { return 0.0f; }
	void updateLoadProgress( Int ) override {}
	void loadProgressComplete( void ) override {}
	void sendTimeOutGameStart( void ) override {}
	UnsignedInt getLocalPlayerID( void ) override { return 0; }
	UnicodeString getPlayerName( Int ) override { return UnicodeString::TheEmptyString; }
	Int getNumPlayers( void ) override { return 2; }
	Int getAverageFPS( void ) override { return 30; }
	Int getSlotAverageFPS( Int ) override { return 30; }
	void attachTransport( Transport *transport ) override { delete transport; }
	void setLockstepEvents( LockstepEvents & ) override {}
	void initTransport( void ) override {}
	Bool sawCRCMismatch( void ) override { return FALSE; }
	void setSawCRCMismatch( void ) override {}
	Bool isPlayerConnected( Int slot ) override { return slot == 0 || slot == announced; }
	void notifyOthersOfCurrentFrame( void ) override {}
	void notifyOthersOfNewFrame( UnsignedInt ) override {}
	Int getExecutionFrame( void ) override { return 0; }
	void toggleNetworkOn( void ) override {}
	UnsignedInt getPingFrame( void ) override { return 0; }
	Int getPingsSent( void ) override { return 0; }
	Int getPingsRecieved( void ) override { return 0; }

	void setReturnWindow( UnsignedInt ) override {}
	Bool isReturnable( Int slot ) override { return slot == returnable && announced < 0; }
	UnsignedInt announcePlayerReturn( Int slot, const UnsignedByte *key, UnsignedShort, const ::Engine::WireEndpoint &endpoint,
		UnsignedInt &prefill ) override
	{
		announced = slot;
		memcpy( announcedKey, key, sizeof( announcedKey ) );
		announcedEndpoint = endpoint;
		prefill = 40;
		return 500;
	}
	Bool getReturnState( Int slot, ::Engine::SessionReturnState &out ) override
	{
		if ( slot != announced )
			return FALSE;
		out = ::Engine::SessionReturnState();
		out.frame = 500;
		out.connected = 0x03;
		out.fallback[0] = 0;
		out.fallback[1] = 1;
		out.runAhead = 12;
		out.frameRate = 30;
		return TRUE;
	}
	void beginReturn( ReturnFeed * ) override {}
	void setReturnFrame( UnsignedInt frame, UnsignedInt prefill, UnsignedInt router ) override
	{
		toldFrame = frame;
		toldPrefill = prefill;
		toldRouter = router;
	}
	void applyReturnState( const ::Engine::SessionReturnState &state ) override { appliedFrame = state.frame; }
	Bool isReturning( void ) override { return TRUE; }
	UnsignedInt getReturnFrame( void ) override { return toldFrame; }
	Bool isCatchingUp( void ) override { return FALSE; }
	Bool isCRCPending( Int ) override { return FALSE; }
	void noteCRCSkipped( Int ) override {}
};

/// A replay as the recorder writes it: header, then commands (frame, type, player, argument types,
/// arguments). Two stop commands in frame 10, one in frame 20.
std::vector<unsigned char> returnReplay()
{
	std::vector<unsigned char> out;
	const auto put = [&out]( const void *data, size_t size ) {
		const unsigned char *bytes = static_cast<const unsigned char *>( data );
		out.insert( out.end(), bytes, bytes + size );
	};
	const auto wide = [&put]( const wchar_t *text ) {
		for ( ; *text != 0; ++text )
			put( text, sizeof( wchar_t ) );
		const wchar_t zero = 0;
		put( &zero, sizeof( zero ) );
	};
	put( "GENREP", 6 );
	const time_t t = 0;
	put( &t, sizeof( t ) );
	put( &t, sizeof( t ) );
	const UnsignedInt frames = 0;
	put( &frames, sizeof( frames ) );
	const Bool flags[2 + MAX_SLOTS] = {};
	put( flags, sizeof( flags ) );
	wide( L"Last Replay" );
	SYSTEMTIME when = {};
	put( &when, sizeof( when ) );
	wide( L"1.0" );
	wide( L"today" );
	const UnsignedInt numbers[3] = { 1, 2, 3 };
	put( numbers, sizeof( numbers ) );
	const char options[] = "M=00test;SD=4711;";
	put( options, sizeof( options ) );
	put( "1", 2 );
	const Int rest[4] = { 0, GAME_LAN, 0, 30 };
	put( rest, sizeof( rest ) );
	const UnsignedInt commandFrames[3] = { 10, 10, 20 };
	for ( Int i = 0; i < 3; ++i )
	{
		put( &commandFrames[i], sizeof( UnsignedInt ) );
		const GameMessage::Type type = GameMessage::MSG_DO_STOP;
		put( &type, sizeof( type ) );
		const Int player = i;
		put( &player, sizeof( player ) );
		const UnsignedByte argTypes[3] = { 1, static_cast<UnsignedByte>( ARGUMENTDATATYPE_INTEGER ), 1 };
		put( argTypes, sizeof( argTypes ) );
		const Int argument = 100 + i;
		put( &argument, sizeof( argument ) );
	}
	return out;
}

void testReturnSession()
{
	TheNetAddressBook().clear();
	std::printf( "a dropped player comes back into the running game\n" );
	TestLAN host( L"Host", 28401, 28402 );
	TestLAN alice( L"Alice", 28411, 28412 );
	std::vector<TestLAN *> lans = { &host, &alice };
	std::vector<::Engine::NetAddress> targets;
	for ( UnsignedShort p : { 28401, 28411 } )
		targets.push_back( ::Engine::NetAddress::fromIPv4( LOOPBACK, p ) );
	const AsciiString ticket( "return_ticket.ini" );
	std::remove( ticket.str() );
	for ( TestLAN *l : lans )
	{
		l->setDiscoveryTargetsForTest( targets );
		l->setRendezvousServers( std::vector<std::string>() );
		l->setReturnTicketPathForTest( ticket );
		l->setReturnStartForTest( FALSE );
	}

	const char *source = "return_source.rep";
	const std::vector<unsigned char> replay = returnReplay();
	FILE *out = std::fopen( source, "wb" );
	std::fwrite( replay.data(), 1, replay.size(), out );
	std::fclose( out );

	hostGame( host );
	::Engine::context().lobby = &alice;
	alice.RequestLocations();
	check( runUntil( lans, nullptr, 3000, [&] { return alice.LookupGameByListOffset( 0 ) != nullptr; } ), "Alice lists the game" );
	alice.RequestGameJoin( alice.LookupGameByListOffset( 0 ) );
	check( runUntil( lans, nullptr, 5000, [&] { return !alice.joining(); } ) && alice.m_joinResult == GameLobbyInterface::RET_OK, "Alice joins as player" );
	::Engine::context().lobby = &host;
	host.RequestGameStart();
	host.enterGameForTest();
	host.setWatchSourceForTest( AsciiString( source ), 100 );
	::Engine::context().lobby = &alice;
	alice.enterGameForTest();
	alice.keepTicketForTest();
	UnicodeString offeredBy;
	check( alice.hasReturnOffer( offeredBy ) && offeredBy.compare( L"Host" ) == 0, "Alice keeps what she needs to come back" );

	// Alice's game crashed; she starts anew (another lobby, new keys) and comes back
	ReturnTestNetwork network;
	::Engine::context().network = &network;
	alice.reset();
	TestLAN back( L"Alice", 28421, 28422 );
	back.setDiscoveryTargetsForTest( targets );
	back.setRendezvousServers( std::vector<std::string>() );
	back.setReturnTicketPathForTest( ticket );
	back.setReturnStartForTest( FALSE );
	lans = { &host, &back };
	// a wrong token is turned away
	FILE *t = std::fopen( ticket.str(), "r" );
	std::string text;
	char line[256];
	while ( t != nullptr && std::fgets( line, sizeof( line ), t ) != nullptr )
		text += std::strncmp( line, "Token=", 6 ) == 0 ? std::string( "Token=00000000000000000000000000000000\n" ) : std::string( line );
	if ( t != nullptr )
		std::fclose( t );
	TestLAN mallory( L"Mallory", 28431, 28432 );
	mallory.setDiscoveryTargetsForTest( targets );
	mallory.setRendezvousServers( std::vector<std::string>() );
	const AsciiString forged( "return_forged.ini" );
	FILE *f = std::fopen( forged.str(), "w" );
	std::fputs( text.c_str(), f );
	std::fclose( f );
	mallory.setReturnTicketPathForTest( forged );
	mallory.setReturnStartForTest( FALSE );
	lans = { &host, &back, &mallory };
	::Engine::context().lobby = &mallory;
	check( mallory.RequestGameReturn(), "a forged ticket is read" );
	check( runUntil( lans, nullptr, 5000, [&] { return !mallory.joining(); } ) && mallory.m_joinResult == GameLobbyInterface::RET_GAME_GONE
		&& !mallory.isReturning() && network.announced < 0, "a wrong token is turned away" );
	lans = { &host, &back };
	::Engine::context().lobby = &mallory;
	mallory.reset();


	::Engine::context().lobby = &back;
	check( back.RequestGameReturn(), "the ticket names a game to go back to" );
	check( runUntil( lans, nullptr, 5000, [&] { return back.isReturning(); } ), "the host takes her back into her slot" );
	check( runUntil( lans, nullptr, 5000, [&] { return back.returnBytesForTest() == replay.size() && back.returnHeaderForTest(); } ),
		"the game's replay arrives, its header is read" );
	check( runUntil( lans, nullptr, 5000, [&] { return network.announced == 1; } )
		&& std::memcmp( network.announcedKey, back.publicKeyForTest(), sizeof( network.announcedKey ) ) == 0
		&& network.announcedEndpoint.isSet(), "caught up, she is announced to the lockstep with her new key" );
	check( runUntil( lans, nullptr, 5000, [&] { return back.returnFrameForTest() == 500 && back.returnStateForTest(); } )
		&& network.toldFrame == 500 && network.toldPrefill == 40 && network.toldRouter == 0 && network.appliedFrame == 500,
		"she hears the return frame and takes over the state there" );
	check( runUntil( lans, nullptr, 3000, [&] { return host.returnDoneForTest( 1 ); } ), "the host hears that she is back" );

	// the frames before the return come from the stream
	CommandList list;
	list.init();
	ReturnFeed &feed = back.returnFeedForTest();
	Int perFrame[3] = {};
	for ( UnsignedInt frame = 0; frame < 30 && frame < feed.completeFrames(); ++frame )
	{
		feed.appendFrame( frame, &list );
		Int n = 0;
		for ( GameMessage *msg = list.getFirstMessage(); msg != nullptr; msg = msg->next() )
			++n;
		if ( frame == 10 )
			perFrame[0] = n;
		if ( frame == 15 )
			perFrame[1] = n;
		if ( frame == 20 )
			perFrame[2] = n;
	}
	GameMessage *last = list.getFirstMessage();
	while ( last != nullptr && last->next() != nullptr )
		last = last->next();
	check( feed.completeFrames() == 100 && perFrame[0] == 2 && perFrame[1] == 2 && perFrame[2] == 3 && last != nullptr
		&& last->getType() == GameMessage::MSG_DO_STOP && last->getArgumentCount() == 1 && last->getArgument( 0 )->integer == 102,
		"the feed hands out the commands of each frame in their frame" );
	list.reset();

	for ( TestLAN *l : lans )
	{
		::Engine::context().lobby = l;
		l->reset();
	}
	::Engine::context().lobby = nullptr;
	::Engine::context().network = nullptr;
	std::remove( source );
	std::remove( ticket.str() );
	std::remove( forged.str() );
}

void createSessionSubsystems( const char *seedDir )
{
	Harness::create( ::Engine::context().gameText, CreateGameTextInterface() );
	::Engine::context().mapCache = new MapCache;
	Harness::create( ::Engine::context().playerList, new PlayerList );
	::Engine::context().gameLogic = new GameLogic;
	::Engine::context().recorder = createRecorder();
	EnableSlotListUpdates( FALSE );
	Harness::loadBase( seedDir );
}

} // namespace

int main( int argc, char **argv )
{
	Harness::installCrashHandlers( "current.udp" );
	Harness::createSubsystems();

	testPlainLoopback();
	testEmulator();
	testSealed();
	testResolve();
	testNatPort();
	testFlood();
	testLobbySealing();
	testPortMapper();
	testIPv6Reply();
	if ( argc > 1 )
	{
		createSessionSubsystems( argv[1] );
		testLanSession();
		testInviteSession();
		testInternetSession();
		testEveryAdapter();
		testWatchSession();
		testReturnSession();
	}

	std::printf( "UdpTest: %d failures\n", s_failures );
	return s_failures == 0 ? 0 : 1;
}
