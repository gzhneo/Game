/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// LAN lobby harness: feeds session and rendezvous messages to GameLobby::update as any machine on the
// network (or a rendezvous server) can send them. The handlers (GameLobbyReceive.cpp) and the game
// option callback run as in the game; the callbacks that only update menus, write preferences or
// start the game are no-ops, and no socket is opened, so nothing is sent. Each input starts a fresh
// GameLobby in one of five roles:
//   0 in the lobby, 1 host of a game, 2 player joining the first game it knows (after the first
//   record, as the join button does), 3 player joining sender 1 by direct connect, 4 player joining
//   by invitation code over the rendezvous server.
// Input: role byte, then records of flags (1), length (2, little endian) and the datagram. Flags:
//   bits 0-1 sender (four addresses, each with a fixed key), bit 2 sealed (arrives authenticated
//   from the identity of the sender's key), bit 3 fix up (the harness fills in what only a real
//   peer could know: cookie, nonce, tags, sealed JOIN block), bit 4 game socket, bit 5 from the
//   rendezvous server.
//
//   LanFuzz <seed dir> <iterations> <rng seed>   fuzz generated seeds
//   LanFuzz --run <seed dir> <file>              replay one input with the debug log

#include "PreRTS.h"

#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/PlayerList.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/GUIUtil.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GameLobby.h"
#include "GameNetwork/GameLobbyCallbacks.h"
#include "GameNetwork/LANGameInfo.h"
#include "GameNetwork/Transport.h"

#include "Engine/InviteCode.h"
#include "Engine/NetAddressBook.h"
#include "Engine/Rendezvous.h"
#include "Engine/SessionHandshake.h"
#include "Engine/SessionMessage.h"
#include "GameLogic/GameLogic.h"

#include "Harness.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

extern Bool LANbuttonPushed;	// LanLobbyMenu.cpp

using Harness::Rng;
using Harness::readFile;
using Harness::writeFile;

namespace
{

constexpr UnsignedInt LOCAL_IP = 0x0A000001;
constexpr UnsignedInt SERVER_IP = 0x0A090909;
constexpr int ROLES = 5;
constexpr std::size_t MAX_RECORD = 1400;

enum : unsigned char
{
	F_SEALED = 4,
	F_FIXUP = 8,
	F_GAME = 16,
	F_SERVER = 32,
};

UnsignedInt remoteIp( unsigned char sender ) { return 0x0A000002 + (sender & 3); }

::Engine::SealKeyPair s_keys[4];	///< the fixed key of each sender

UnsignedInt identityOfSender( unsigned char sender )
{
	return ::Engine::SessionHandshake::identityOf( s_keys[sender & 3].publicKey );
}

std::string fingerprintText( const UnsignedByte *key )
{
	char code[10];
	::Engine::sealFingerprint( key, code );
	std::string compact;
	for ( const char *p = code; *p; ++p )
		if ( *p != '-' )
			compact += *p;
	return compact;
}

class HarnessLAN : public GameLobby
{
public:
	HarnessLAN()
	{
		m_localIP = LOCAL_IP;
		m_bindIP = LOCAL_IP;
		m_name = L"Harness";
		m_userName = "h";
		m_hostName = "h";
		m_inLobby = TRUE;
		m_isInLANMenu = TRUE;
		m_currentGame = nullptr;
		m_pendingAction = ACT_NONE;
		setOfflineForTest( TRUE );
		std::vector<std::string> servers;
		servers.push_back( "10.9.9.9:8090" );
		setRendezvousServers( servers );
	}

	/// One datagram, as a transport would have received it.
	void deliver( unsigned char flags, const unsigned char *data, std::size_t size )
	{
		unsigned char buffer[Transport::MAX_RAW_DATAGRAM];
		if ( size > sizeof( buffer ) )
			size = sizeof( buffer );
		std::memcpy( buffer, data, size );
		const unsigned char sender = flags & 3;
		const Bool game = (flags & F_GAME) != 0;
		::Engine::NetAddress from = ::Engine::NetAddress::fromIPv4( remoteIp( sender ), game ? 8088 : 8086 );
		if ( flags & F_SERVER )
			from = ::Engine::NetAddress::fromIPv4( SERVER_IP, 8090 );
		if ( flags & F_FIXUP )
			size = fixUp( buffer, size, sender, from );
		const Bool sealed = (flags & F_SEALED) != 0;
		const UnsignedInt identity = sealed ? identityOfSender( sender ) : from.ipv4();
		Transport *t = game ? m_gameTransport.get() : m_transport.get();
		if ( t != nullptr && size > 0 )
			t->injectRawForTest( buffer, static_cast<Int>(size), from, identity, sealed );
		m_lastUpdate = 0;	// update() runs at most every 50 ms
		update();
	}

	Int gameCount() const { return static_cast<Int>( m_directory.gameCount() ); }
	Int playerCount() const { return static_cast<Int>( m_directory.playerCount() ); }

	Int m_chats = 0;		///< chat lines that reached the screen
	ReturnType m_joinResult = RET_MAX;

	/// Host a game with a fixed seed and map, so that inputs replay the same way.
	void hostGame()
	{
		RequestGameCreate( UnicodeString( L"FuzzGame" ), FALSE );
		if ( m_currentGame )
		{
			m_currentGame->setSeed( 1 );
			AsciiString map = TheMapCache->getMapDir();
			map.concat( "\\alpine\\alpine.map" );
			m_currentGame->setMap( map );
			// the game menu opens the free slots; closed ones turn every joiner away as "game full"
			for ( Int i = 1; i < MAX_SLOTS; ++i )
				m_currentGame->getSlot( i )->setState( SLOT_OPEN );
		}
	}

	void joinFirstGame()
	{
		if ( m_directory.games() && m_pendingAction == ACT_NONE && !m_join.active )
			RequestGameJoin( m_directory.games() );
	}

	void joinByInvite()
	{
		std::string code = "K7Q2-" + fingerprintText( s_keys[1].publicKey ) + "@10.9.9.9";
		RequestGameJoinInvite( AsciiString( code.c_str() ) );
	}

	LANGameInfo *currentGame() { return m_currentGame; }
	Bool joining() const { return m_join.active; }
	Bool joinSent() const { return !m_join.joinMessage.empty(); }
	void ageMember( Int slot, UnsignedInt ms ) { m_host.memberHeard[slot] -= ms; }
	UnsignedInt ownIdentity() const { return ::Engine::SessionHandshake::identityOf( m_keyPair.publicKey ); }

	// menus, preferences, the shell and the game start are not part of the harness
	virtual void OnGameList( LANGameInfo * ) override {}
	virtual void OnPlayerList( LANPlayer * ) override {}
	virtual void OnGameJoin( ReturnType ret, LANGameInfo * ) override { m_joinResult = ret; }
	virtual void OnPlayerJoin( Int, UnicodeString ) override {}
	virtual void OnHostLeave( void ) override {}
	virtual void OnPlayerLeave( UnicodeString ) override {}
	virtual void OnAccept( UnsignedInt, Bool ) override {}
	virtual void OnHasMap( UnsignedInt, Bool ) override {}
	virtual void OnChat( UnicodeString, UnsignedInt, UnicodeString, ChatType ) override { ++m_chats; }
	virtual void OnGameStart( void ) override {}
	virtual void OnGameStartTimer( Int ) override {}
	virtual void OnGameCreate( ReturnType ) override {}
	virtual void OnNameChange( UnsignedInt, UnicodeString ) override {}
	virtual void OnInActive( UnsignedInt ) override {}
	virtual void checkMOTD( void ) override {}

private:
	/// Fills in what only the real peer or server knows, so that inputs get past the checks.
	std::size_t fixUp( unsigned char *buffer, std::size_t size, unsigned char sender, const ::Engine::NetAddress &from )
	{
		if ( ::Engine::looksLikeTreff( buffer, size ) )
		{
			::Engine::TreffMessage t;
			if ( !::Engine::decodeTreff( buffer, size, t ) )
				return size;
			switch ( t.type )
			{
				case ::Engine::TreffType::OBSERVED: t.tag = m_observeTag; break;
				case ::Engine::TreffType::REGISTER_ACK: t.tag = m_room.tag(); t.room = m_room.code(); break;
				case ::Engine::TreffType::KNOCK: t.room = m_room.code(); break;
				case ::Engine::TreffType::FOUND: t.tag = m_join.lookupTag; t.room = m_join.room; break;
				case ::Engine::TreffType::RELAY_OPENED: t.tag = m_room.relayTag(); t.relayId = m_room.relayId(); break;
				case ::Engine::TreffType::RELAY_BOUND: t.tag = m_links.relayBindTag(); t.relayId = m_links.relayId(); break;
				case ::Engine::TreffType::SERVER_LIST: t.tag = m_room.serversTag(); break;
				case ::Engine::TreffType::REFUSED: t.tag = m_join.active ? m_join.lookupTag : m_room.tag(); break;
				default: break;
			}
			const std::size_t n = ::Engine::encodeTreff( t, buffer, Transport::MAX_RAW_DATAGRAM );
			return n != 0 ? n : size;
		}
		::Engine::SessionMessage m;
		if ( !::Engine::decodeSession( buffer, size, m ) )
			return size;
		const ::Engine::SealKeyPair &key = s_keys[sender];
		switch ( m.type )
		{
			case ::Engine::SessionType::ANNOUNCE:
				Lobby::fingerprintBytes( key.publicKey, m.fingerprint );
				break;
			case ::Engine::SessionType::HELLO:
				std::memcpy( m.key, key.publicKey, sizeof( m.key ) );
				m.sessionId = m_sessionId;
				break;
			case ::Engine::SessionType::JOIN:
			{
				// the sealed block of the record is the plain join information
				std::memcpy( m.key, key.publicKey, sizeof( m.key ) );
				m.sessionId = m_sessionId;
				::Engine::SessionHandshake::cookie( m_host.cookieKey, m_sessionId, m.key, m.nonce, Lobby::wireEndpoint( from ), m.cookie );
				std::vector<std::uint8_t> sealed;
				if ( ::Engine::SessionHandshake::sealJoin( key, m_keyPair.publicKey, m.nonce, m.sealed.data(), m.sealed.size(), sealed ) )
					m.sealed = sealed;
				break;
			}
			case ::Engine::SessionType::CHALLENGE:
				std::memcpy( m.key, key.publicKey, sizeof( m.key ) );
				std::memcpy( m.nonce, m_join.nonce, sizeof( m.nonce ) );
				if ( m_join.sessionId != 0 )
					m.sessionId = m_join.sessionId;
				break;
			case ::Engine::SessionType::REFUSE:
				m.sessionId = m_join.sessionId;
				break;
			case ::Engine::SessionType::WELCOME:
				m.sessionId = m_join.sessionId;
				m.identity = ::Engine::SessionHandshake::identityOf( m_keyPair.publicKey );
				break;
			case ::Engine::SessionType::MEMBER:
				std::memcpy( m.member.key, s_keys[m.member.slot & 3].publicKey, sizeof( m.member.key ) );
				m.member.identity = ::Engine::SessionHandshake::identityOf( m.member.key );
				break;
			case ::Engine::SessionType::PROBE:
			case ::Engine::SessionType::PROBE_ACK:
				m.sessionId = m_sessionId;
				m.identity = identityOfSender( sender );
				if ( m.type == ::Engine::SessionType::PROBE_ACK )
				{
					const Int slot = m_links.slotOf( m.identity );
					if ( slot >= 0 )
						m.value = m_links.member( slot ).link.nonce();
				}
				break;
			default:
				break;
		}
		const std::size_t n = ::Engine::encodeSession( m, buffer, ::Engine::MAX_SESSION_MESSAGE );
		return n != 0 ? n : size;
	}
};

void createLanSubsystems()
{
	Harness::create( ::Engine::context().gameText, CreateGameTextInterface() );
	::Engine::context().mapCache = new MapCache;
	Harness::create( ::Engine::context().playerList, new PlayerList );	// the starting cash of game options asks for the local player
	::Engine::context().gameLogic = new GameLogic;	// the starting cash of game options books income at the current frame; not initialised
	EnableSlotListUpdates( FALSE );	// no menus to update
	// the join check compares these; fixed so that kept inputs replay with another build
	TheWritableGlobalData->m_exeCRC = 0x12345678;
	TheWritableGlobalData->m_iniCRC = 0x9ABCDEF0;
	for ( int s = 0; s < 4; ++s )
	{
		std::uint8_t secret[::Engine::SEAL_KEY_SIZE];
		std::memset( secret, 0x40 + s, sizeof( secret ) );
		s_keys[s].fromSecret( secret );
	}
}

struct Record
{
	unsigned char flags;
	std::string data;
};

std::vector<Record> parse( const std::string &input )
{
	std::vector<Record> records;
	std::size_t pos = 1;
	while ( pos + 3 <= input.size() && records.size() < 32 )
	{
		const std::size_t length = static_cast<unsigned char>(input[pos + 1]) | (static_cast<std::size_t>(static_cast<unsigned char>(input[pos + 2])) << 8);
		if ( length > MAX_RECORD || pos + 3 + length > input.size() )
			break;
		records.push_back( { static_cast<unsigned char>(input[pos]), input.substr( pos + 3, length ) } );
		pos += 3 + length;
	}
	return records;
}

std::string build( int role, const std::vector<Record> &records )
{
	std::string d( 1, static_cast<char>(role) );
	for ( const Record &r : records )
	{
		d += static_cast<char>(r.flags);
		d += static_cast<char>(r.data.size() & 0xFF);
		d += static_cast<char>(r.data.size() >> 8);
		d += r.data;
	}
	return d;
}

void runInput( const std::string &data )
{
	writeFile( Harness::workDir / "current.lan", data );	// kept on a crash
	if ( data.empty() )
		return;
	HarnessLAN *lan = new HarnessLAN;
	::Engine::context().lobby = lan;
	LANbuttonPushed = false;	// set when the player leaves a menu; update() then ignores everything
	const int role = static_cast<unsigned char>(data[0]) % ROLES;
	if ( role == 1 )
		lan->hostGame();
	else if ( role == 3 )
		lan->RequestGameJoinDirectConnect( remoteIp( 1 ) );	// the direct connect menu
	else if ( role == 4 )
		lan->joinByInvite();

	const std::vector<Record> records = parse( data );
	for ( std::size_t n = 0; n < records.size(); ++n )
	{
		lan->deliver( records[n].flags, reinterpret_cast<const unsigned char *>(records[n].data.data()), records[n].data.size() );
		if ( role == 2 && n == 0 )
			lan->joinFirstGame();
	}
	::Engine::context().lobby = nullptr;
	delete lan;
}

// --- seeds ---------------------------------------------------------------------------------------

std::string encoded( const ::Engine::SessionMessage &m )
{
	std::uint8_t buf[::Engine::MAX_SESSION_MESSAGE];
	const std::size_t n = ::Engine::encodeSession( m, buf, sizeof( buf ) );
	return std::string( reinterpret_cast<const char *>(buf), n );
}

std::string encoded( const ::Engine::TreffMessage &m )
{
	std::uint8_t buf[::Engine::MAX_TREFF_MESSAGE];
	const std::size_t n = ::Engine::encodeTreff( m, buf, sizeof( buf ) );
	return std::string( reinterpret_cast<const char *>(buf), n );
}

::Engine::SessionMessage msg( ::Engine::SessionType type )
{
	::Engine::SessionMessage m;
	m.type = type;
	m.protocol = LAN_PROTOCOL_VERSION;
	return m;
}

::Engine::TreffMessage treff( ::Engine::TreffType type )
{
	::Engine::TreffMessage m;
	m.type = type;
	return m;
}

/// JOIN whose sealed block is the plain join information (the harness seals it).
::Engine::SessionMessage joinMessage( const char *name, UnsignedInt protocol )
{
	::Engine::SessionJoinInfo info;
	info.name = name;
	info.protocol = protocol;
	info.programCRC = TheGlobalData->m_exeCRC;
	info.dataCRC = TheGlobalData->m_iniCRC;
	info.versionText = "harness";
	info.endpoints.lobby[0] = ::Engine::WireEndpoint::ipv4( 0xC0A80002, 8086 );
	info.endpoints.game[0] = ::Engine::WireEndpoint::ipv4( 0xC0A80002, 8088 );
	std::uint8_t plain[::Engine::SESSION_MAX_SEALED];
	const std::size_t n = ::Engine::encodeJoinInfo( info, plain, sizeof( plain ) );
	::Engine::SessionMessage m = msg( ::Engine::SessionType::JOIN );
	m.nonce[0] = 1;
	m.sealed.assign( plain, plain + n );
	return m;
}

/// The game options of a hosted game as the host sends them.
std::string hostOptions()
{
	HarnessLAN lan;
	::Engine::context().lobby = &lan;
	lan.hostGame();
	std::string options;
	if ( lan.currentGame() )
		options = GameInfoToAsciiString( lan.currentGame() ).str();
	::Engine::context().lobby = nullptr;
	return options;
}

::Engine::SessionMessage announce( const std::string &options, UnsignedInt64 session )
{
	::Engine::SessionMessage m = msg( ::Engine::SessionType::ANNOUNCE );
	m.sessionId = session;
	m.programCRC = TheGlobalData->m_exeCRC;
	m.dataCRC = TheGlobalData->m_iniCRC;
	m.gameName = "Game";
	m.hostName = "Bob";
	m.players = 1;
	m.maxPlayers = 8;
	m.options = options;
	return m;
}

std::vector<std::string> makeSeeds()
{
	const std::string options = hostOptions();
	std::vector<std::string> seeds;
	using ST = ::Engine::SessionType;
	using TT = ::Engine::TreffType;

	// lobby: somebody asks, announces himself and a game, chats, leaves
	{
		std::vector<Record> r;
		r.push_back( { 0, encoded( msg( ST::QUERY ) ) } );
		::Engine::SessionMessage presence = msg( ST::PRESENCE );
		presence.name = "Alice";
		r.push_back( { 0, encoded( presence ) } );
		r.push_back( { 1 | F_FIXUP, encoded( announce( options, 0x1122334455667788ull ) ) } );
		::Engine::SessionMessage chat = msg( ST::LOBBY_CHAT );
		chat.name = "Alice";
		chat.text = "hello \xC3\xA4";
		r.push_back( { 0, encoded( chat ) } );
		r.push_back( { 0, encoded( msg( ST::BYE ) ) } );
		seeds.push_back( build( 0, r ) );
	}
	// host: a player of another version knocks, one joins, sends wishes, is ready, chats, probes,
	// the rendezvous server takes the room, a joiner knocks, the relay opens, the player leaves
	{
		std::vector<Record> r;
		r.push_back( { 0 | F_FIXUP, encoded( msg( ST::HELLO ) ) } );
		r.push_back( { 2 | F_FIXUP, encoded( joinMessage( "Olga", LAN_PROTOCOL_VERSION + 1 ) ) } );
		r.push_back( { 0 | F_FIXUP, encoded( joinMessage( "Alice", LAN_PROTOCOL_VERSION ) ) } );
		for ( const char *o : { "Color=2", "PlayerTemplate=0", "StartPos=1", "Team=0", "User=alice", "NAT=1" } )
		{
			::Engine::SessionMessage wish = msg( ST::WISH );
			wish.text = o;
			r.push_back( { 0 | F_SEALED, encoded( wish ) } );
		}
		::Engine::SessionMessage accept = msg( ST::ACCEPT );
		accept.flag = true;
		r.push_back( { 0 | F_SEALED, encoded( accept ) } );
		::Engine::SessionMessage map = msg( ST::HAS_MAP );
		map.flag = true;
		r.push_back( { 0 | F_SEALED, encoded( map ) } );
		::Engine::SessionMessage chat = msg( ST::CHAT );
		chat.text = "gg";
		r.push_back( { 0 | F_SEALED, encoded( chat ) } );
		r.push_back( { 0 | F_SEALED, encoded( msg( ST::PING ) ) } );
		r.push_back( { 0 | F_SEALED | F_GAME | F_FIXUP, encoded( msg( ST::PROBE ) ) } );
		::Engine::TreffMessage ack = treff( TT::REGISTER_ACK );
		ack.observed = ::Engine::WireEndpoint::ipv4( 0xCB007107, 40000 );
		r.push_back( { F_SERVER | F_FIXUP, encoded( ack ) } );
		::Engine::TreffMessage knock = treff( TT::KNOCK );
		knock.lobby[0] = ::Engine::WireEndpoint::ipv4( 0xCB007108, 50000 );
		r.push_back( { F_SERVER | F_FIXUP, encoded( knock ) } );
		r.push_back( { F_SERVER | F_FIXUP, encoded( treff( TT::RELAY_OPENED ) ) } );
		::Engine::TreffMessage bound = treff( TT::RELAY_BOUND );
		bound.socket = ::Engine::TreffSocket::GAME;
		r.push_back( { F_SERVER | F_GAME | F_FIXUP, encoded( bound ) } );
		::Engine::TreffMessage list = treff( TT::SERVER_LIST );
		list.servers.push_back( "meet.example.org" );
		r.push_back( { F_SERVER | F_FIXUP, encoded( list ) } );
		r.push_back( { 0 | F_SEALED, encoded( msg( ST::LEAVE ) ) } );
		seeds.push_back( build( 1, r ) );
	}
	// joiner: a host announces, answers, welcomes us, sends state, members and relay, counts down,
	// starts and leaves
	{
		std::vector<Record> r;
		r.push_back( { 1 | F_FIXUP, encoded( announce( options, 0x0123456789ABCDEFull ) ) } );
		r.push_back( { 1 | F_FIXUP, encoded( msg( ST::CHALLENGE ) ) } );
		::Engine::SessionMessage state = msg( ST::STATE );
		state.revision = 1;
		state.options = options;
		r.push_back( { 1 | F_SEALED, encoded( state ) } );	// before WELCOME: kept until then
		::Engine::SessionMessage welcome = msg( ST::WELCOME );
		welcome.slot = 1;
		r.push_back( { 1 | F_SEALED | F_FIXUP, encoded( welcome ) } );
		::Engine::SessionMessage member = msg( ST::MEMBER );
		member.member.slot = 2;
		member.member.endpoints.game[0] = ::Engine::WireEndpoint::ipv4( 0x0A000004, 8088 );
		r.push_back( { 1 | F_SEALED | F_FIXUP, encoded( member ) } );
		r.push_back( { 2 | F_SEALED | F_GAME | F_FIXUP, encoded( msg( ST::PROBE_ACK ) ) } );
		::Engine::SessionMessage relay = msg( ST::RELAY );
		relay.relayServer = "10.9.9.9:8090";
		relay.relayId = 77;
		r.push_back( { 1 | F_SEALED, encoded( relay ) } );
		::Engine::SessionMessage accept = msg( ST::ACCEPT );
		accept.flag = true;
		r.push_back( { 1 | F_SEALED, encoded( accept ) } );
		::Engine::SessionMessage chat = msg( ST::CHAT );
		chat.slot = 0;
		chat.text = "hi";
		r.push_back( { 1 | F_SEALED, encoded( chat ) } );
		::Engine::SessionMessage timer = msg( ST::TIMER );
		timer.value = 5;
		r.push_back( { 1 | F_SEALED, encoded( timer ) } );
		r.push_back( { 1 | F_SEALED, encoded( msg( ST::START ) ) } );
		r.push_back( { 1 | F_SEALED, encoded( msg( ST::LEAVE ) ) } );
		::Engine::SessionMessage refuse = msg( ST::REFUSE );
		refuse.reason = GameLobbyInterface::RET_GAME_FULL;
		r.push_back( { 1 | F_FIXUP, encoded( refuse ) } );
		seeds.push_back( build( 2, r ) );
		seeds.push_back( build( 3, r ) );	// the same, joining by direct connect

		// by invitation: the server finds the room first
		std::vector<Record> invite;
		::Engine::TreffMessage observed = treff( TT::OBSERVED );
		observed.socket = ::Engine::TreffSocket::GAME;
		observed.observed = ::Engine::WireEndpoint::ipv4( 0xCB007109, 41000 );
		invite.push_back( { F_SERVER | F_GAME | F_FIXUP, encoded( observed ) } );
		::Engine::TreffMessage found = treff( TT::FOUND );
		found.lobby[0] = ::Engine::WireEndpoint::ipv4( remoteIp( 1 ), 8086 );
		found.gameSocket[0] = ::Engine::WireEndpoint::ipv4( remoteIp( 1 ), 8088 );
		invite.push_back( { F_SERVER | F_FIXUP, encoded( found ) } );
		for ( std::size_t i = 1; i < r.size(); ++i )
			invite.push_back( r[i] );
		seeds.push_back( build( 4, invite ) );
		// a server that does not know the room
		std::vector<Record> unknown;
		::Engine::TreffMessage refused = treff( TT::REFUSED );
		refused.reason = ::Engine::TreffRefusal::NO_ROOM;
		unknown.push_back( { F_SERVER | F_FIXUP, encoded( refused ) } );
		seeds.push_back( build( 4, unknown ) );
	}
	return seeds;
}

// --- fixed checks --------------------------------------------------------------------------------

int s_checkFailures = 0;

void expect( bool ok, const char *what )
{
	std::printf( "  %s: %s\n", ok ? "ok  " : "FAIL", what );
	if ( !ok )
		++s_checkFailures;
}

void send( HarnessLAN &lan, unsigned char flags, const ::Engine::SessionMessage &m )
{
	const std::string d = encoded( m );
	lan.deliver( flags, reinterpret_cast<const unsigned char *>(d.data()), d.size() );
}

/// What the lobby must do under forged traffic; returns the number of failures.
int lobbyChecks( const std::string &options )
{
	using ST = ::Engine::SessionType;
	std::printf( "lobby checks\n" );
	s_checkFailures = 0;
	{
		HarnessLAN lan;
		::Engine::context().lobby = &lan;
		for ( int i = 0; i < 300; ++i )
		{
			// forged senders: the transport names them by their address
			const std::string game = encoded( announce( options, 0x1000 + static_cast<UnsignedInt64>(i) ) );
			const ::Engine::NetAddress from = ::Engine::NetAddress::fromIPv4( 0x0B000000 + i, 8086 );
			lan.getLobbyTransport()->injectRawForTest( reinterpret_cast<const unsigned char *>(game.data()),
				static_cast<Int>(game.size()), from, from.ipv4(), FALSE );
			::Engine::SessionMessage presence = msg( ST::PRESENCE );
			presence.name = "Alice";
			const std::string p = encoded( presence );
			const ::Engine::NetAddress who = ::Engine::NetAddress::fromIPv4( 0x0C000000 + i, 8086 );
			lan.getLobbyTransport()->injectRawForTest( reinterpret_cast<const unsigned char *>(p.data()),
				static_cast<Int>(p.size()), who, who.ipv4(), FALSE );
			lan.deliver( 0, nullptr, 0 );
		}
		expect( lan.gameCount() == 64, "the game list keeps at most 64 games" );
		expect( lan.playerCount() == 128, "the lobby keeps at most 128 players" );

		const UnsignedInt sentBefore = lan.getMessagesBuiltForTest();
		for ( int i = 0; i < 100; ++i )
			send( lan, static_cast<unsigned char>(i & 3), msg( ST::QUERY ) );
		expect( lan.getMessagesBuiltForTest() - sentBefore <= 12, "questions for games are answered at a limited rate" );
		::Engine::context().lobby = nullptr;
	}
	{
		HarnessLAN lan;
		::Engine::context().lobby = &lan;
		::Engine::SessionMessage presence = msg( ST::PRESENCE );
		presence.name = "Spammer";
		send( lan, 1, presence );
		for ( int i = 0; i < 100; ++i )
		{
			::Engine::SessionMessage chat = msg( ST::LOBBY_CHAT );
			chat.name = "Spammer";
			chat.text = "spam";
			send( lan, 1, chat );
		}
		expect( lan.m_chats >= 1 && lan.m_chats <= 12, "chat of one sender reaches the screen at a limited rate" );
		::Engine::context().lobby = nullptr;
	}
	{
		// the game list tells games we cannot join; checksums count as in a release build
		HarnessLAN lan;
		::Engine::context().lobby = &lan;
		const Int minPlayers = TheGlobalData->m_netMinPlayers;
		TheWritableGlobalData->m_netMinPlayers = 1;
		auto listed = [&]( unsigned char sender, UnsignedInt64 session, UnsignedInt protocol, UnsignedInt program, UnsignedInt data ) {
			::Engine::SessionMessage m = announce( options, session );
			m.protocol = protocol;
			m.programCRC = program;
			m.dataCRC = data;
			send( lan, sender, m );
			return lan.LookupGame( Lobby::sessionGameName( session ) );
		};
		const UnsignedInt program = TheGlobalData->m_exeCRC;
		const UnsignedInt data = TheGlobalData->m_iniCRC;
		LANGameInfo *same = listed( 0, 1, LAN_PROTOCOL_VERSION, program, data );
		LANGameInfo *otherData = listed( 1, 2, LAN_PROTOCOL_VERSION, program, data ^ 1 );
		LANGameInfo *otherProgram = listed( 2, 3, LAN_PROTOCOL_VERSION, program ^ 1, data );
		LANGameInfo *otherProtocol = listed( 3, 4, LAN_PROTOCOL_VERSION + 1, program, data );
		expect( same != nullptr && lan.compatibilityOf( same ) == GameLobbyInterface::RET_OK, "a game of our version can be joined" );
		expect( otherData != nullptr && lan.compatibilityOf( otherData ) == GameLobbyInterface::RET_DATA_MISMATCH, "other game data are shown" );
		expect( otherProgram != nullptr && lan.compatibilityOf( otherProgram ) == GameLobbyInterface::RET_VERSION_MISMATCH, "another program is shown" );
		expect( otherProtocol != nullptr && lan.compatibilityOf( otherProtocol ) == GameLobbyInterface::RET_VERSION_MISMATCH, "another protocol is shown" );
		::Engine::SessionMessage claim = announce( options, 1 );	// another endpoint claims the same game
		claim.hostName = "Mallory";
		send( lan, 1, claim );
		expect( same != nullptr && same->getHostEndpoint().ipv4() == remoteIp( 0 ), "a listed game keeps the endpoint of its host" );
		TheWritableGlobalData->m_netMinPlayers = minPlayers;
		::Engine::context().lobby = nullptr;
	}
	{
		// host: only a joiner that returns the cookie gets a slot; messages count only sealed
		HarnessLAN lan;
		::Engine::context().lobby = &lan;
		lan.hostGame();
		LANGameInfo *game = lan.currentGame();
		const UnsignedInt alice = identityOfSender( 0 );
		::Engine::SessionMessage join = joinMessage( "Alice", LAN_PROTOCOL_VERSION );
		send( lan, 0, join );	// no fix up: wrong cookie, block not sealed
		expect( game != nullptr && !game->getLANSlot( 1 )->isHuman(), "a JOIN without the cookie gets no slot" );
		send( lan, 0 | F_FIXUP, joinMessage( "Olga", LAN_PROTOCOL_VERSION + 1 ) );
		expect( game != nullptr && !game->getLANSlot( 1 )->isHuman(), "a JOIN of another protocol gets no slot" );
		send( lan, 0 | F_FIXUP, join );
		expect( game != nullptr && game->getLANSlot( 1 )->isHuman() && game->getIP( 1 ) == alice,
			"a JOIN with the cookie gets a slot under the identity of its key" );
		send( lan, 0 | F_FIXUP, join );
		expect( game != nullptr && !game->getLANSlot( 2 )->isHuman(), "a repeated JOIN takes no second slot" );
		send( lan, 2 | F_FIXUP, joinMessage( "Alice", LAN_PROTOCOL_VERSION ) );
		expect( game != nullptr && !game->getLANSlot( 2 )->isHuman(), "a second player of the same name is turned away" );

		::Engine::SessionMessage wish = msg( ST::WISH );
		wish.text = "Team=1";
		send( lan, 0, wish );
		expect( game != nullptr && game->getLANSlot( 1 )->getTeamNumber() != 1, "a plain wish counts for nothing" );
		send( lan, 1 | F_SEALED, wish );
		expect( game != nullptr && game->getLANSlot( 1 )->getTeamNumber() != 1, "a sealed wish of somebody else counts for nothing" );
		send( lan, 0 | F_SEALED, wish );
		expect( game != nullptr && game->getLANSlot( 1 )->getTeamNumber() == 1, "a sealed wish of the player counts" );

		lan.ageMember( 1, 60 * 1000 );
		send( lan, 3, msg( ST::QUERY ) );
		expect( game != nullptr && !game->getLANSlot( 1 )->isHuman(), "a silent player loses its slot" );
		::Engine::context().lobby = nullptr;
	}
	{
		// joiner: the host key must match the announcement; the game begins with WELCOME
		HarnessLAN lan;
		::Engine::context().lobby = &lan;
		send( lan, 1 | F_FIXUP, announce( options, 0x42 ) );
		lan.joinFirstGame();
		send( lan, 2 | F_FIXUP, msg( ST::CHALLENGE ) );	// another endpoint: ignored
		expect( lan.joining() && !lan.joinSent(), "an answer from another endpoint is ignored" );
		::Engine::SessionMessage challenge = msg( ST::CHALLENGE );
		challenge.sessionId = 0x42;
		send( lan, 1, challenge );	// right endpoint, wrong nonce and key
		expect( lan.joining() && !lan.joinSent(), "an answer without our nonce is ignored" );
		send( lan, 1 | F_FIXUP, msg( ST::CHALLENGE ) );
		expect( lan.joining() && lan.joinSent(), "the host's answer leads to JOIN" );
		::Engine::SessionMessage welcome = msg( ST::WELCOME );
		welcome.slot = 1;
		send( lan, 2 | F_SEALED | F_FIXUP, welcome );
		expect( lan.joining(), "WELCOME of somebody else is ignored" );
		send( lan, 1 | F_SEALED | F_FIXUP, welcome );
		expect( !lan.joining() && lan.currentGame() != nullptr && lan.m_joinResult == GameLobbyInterface::RET_OK
			&& lan.currentGame()->getIP( 1 ) == lan.ownIdentity(), "WELCOME puts us into the slot under our identity" );
		::Engine::context().lobby = nullptr;
	}
	{
		// a host with another key than the announced one
		HarnessLAN lan;
		::Engine::context().lobby = &lan;
		::Engine::SessionMessage game = announce( options, 0x43 );
		Lobby::fingerprintBytes( s_keys[3].publicKey, game.fingerprint );
		send( lan, 1, game );
		lan.joinFirstGame();
		send( lan, 1 | F_FIXUP, msg( ST::CHALLENGE ) );
		expect( !lan.joining() && lan.m_joinResult == GameLobbyInterface::RET_HOST_KEY_MISMATCH, "a host key other than announced ends the join" );
		::Engine::context().lobby = nullptr;
	}
	std::printf( "lobby checks: %d failed\n", s_checkFailures );
	return s_checkFailures;
}

// --- mutation ------------------------------------------------------------------------------------

const std::uint32_t s_ints[] = {
	0, 1, 2, 7, 8, 9, 15, 16, 0x7F, 0x80, 0xFF, 0xFFFF, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu,
	LOCAL_IP, 0x0A000002, 0x0A000003, 0xF0000000u, 0xFEFFFFFFu, GameLobbyInterface::RET_MAX, LAN_PROTOCOL_VERSION,
};

const char *const s_texts[] = {
	"", "=", ";", "Color=-2", "Color=99", "PlayerTemplate=-3", "PlayerTemplate=9999", "StartPos=8",
	"StartPos=-5", "Team=4", "Team=-7", "User=", "Host=", "NAT=9", "S=", "M=", "SD=", "SC=", "SR=",
	"S=H,0,0,X,1,-1,-1,-1,-1:X:X:X:X:X:X:X:;", "US=1;M=00../../x;MC=1;MS=1;SD=1;C=1;SR=0;SC=0;O=N;S=;",
	"\xC3", "\xE2\x82", "\xF4\x90\x80\x80", "\xED\xA0\x80", "\x01\x02\x7F", "meet.example.org", "[::1]:1",
};

class LanMutator
{
public:
	LanMutator( std::vector<std::string> corpus, std::uint64_t seed ) : m_corpus( std::move( corpus ) ), m_rng{ seed } {}
	void add( const std::string &d ) { m_corpus.push_back( d ); }
	std::size_t size() const { return m_corpus.size(); }

	std::string next()
	{
		std::string d = m_corpus[m_rng.below( m_corpus.size() )];
		std::vector<Record> r = parse( d );
		int role = d.empty() ? 0 : static_cast<unsigned char>(d[0]);
		const int count = 1 + static_cast<int>(m_rng.below( 4 ));
		for ( int i = 0; i < count; ++i )
			mutate( r, role );
		return build( role, r );
	}

private:
	std::string text()
	{
		if ( m_rng.below( 2 ) )
			return s_texts[m_rng.below( std::size( s_texts ) )];
		std::string s( m_rng.below( 300 ), 'A' );
		for ( char &c : s )
			c = static_cast<char>('A' + m_rng.below( 26 ));
		return s;
	}

	std::uint32_t value() { return Harness::pickInt( m_rng, s_ints ); }

	/// A field of a well-formed message, so that it still gets past the reader.
	void mutateField( std::string &data )
	{
		::Engine::SessionMessage m;
		if ( !::Engine::decodeSession( reinterpret_cast<const std::uint8_t *>(data.data()), data.size(), m ) )
			return;
		switch ( m_rng.below( 12 ) )
		{
			case 0: m.text = text(); break;
			case 1: m.options = text(); break;
			case 2: m.name = text(); break;
			case 3: m.slot = static_cast<std::uint8_t>(value()); m.member.slot = m.slot; break;
			case 4: m.value = value(); m.revision = value(); break;
			case 5: m.identity = value(); m.member.identity = value(); break;
			case 6: m.reason = static_cast<std::uint8_t>(value()); m.chatKind = static_cast<std::uint8_t>(value()); break;
			case 7: m.protocol = value(); m.programCRC = value(); break;
			case 8: m.sessionId = value(); break;
			case 9: m.relayServer = text(); m.relayId = value(); break;
			case 10: m.flag = !m.flag; m.mapCRC = value(); break;
			default: m.type = static_cast<::Engine::SessionType>(m_rng.below( 2 ) ? 1 + m_rng.below( 11 ) : 32 + m_rng.below( 13 )); break;
		}
		std::uint8_t buf[::Engine::MAX_SESSION_MESSAGE];
		const std::size_t n = ::Engine::encodeSession( m, buf, sizeof( buf ) );
		if ( n != 0 )
			data.assign( reinterpret_cast<const char *>(buf), n );
	}

	void mutate( std::vector<Record> &r, int &role )
	{
		if ( r.empty() )
		{
			r = parse( m_corpus[0] );
			return;
		}
		const std::size_t i = m_rng.below( r.size() );
		std::string &data = r[i].data;
		switch ( m_rng.below( 8 ) )
		{
			case 0:
			case 1:
				mutateField( data );
				break;
			case 2:	// a byte
				if ( !data.empty() )
					data[m_rng.below( data.size() )] = static_cast<char>(m_rng.next());
				break;
			case 3:	// cut or grow
				if ( m_rng.below( 2 ) && !data.empty() )
					data.resize( m_rng.below( data.size() ) );
				else
					data.append( m_rng.below( 16 ), static_cast<char>(m_rng.next()) );
				break;
			case 4:	// flags: sender, sealed, fix up, socket, server
				r[i].flags = static_cast<unsigned char>(m_rng.next() & 0x3F);
				break;
			case 5:	// repeat a record
				if ( r.size() < 32 )
					r.insert( r.begin() + static_cast<std::ptrdiff_t>(m_rng.below( r.size() + 1 )), r[i] );
				break;
			case 6:	// drop a record
				if ( r.size() > 1 )
					r.erase( r.begin() + static_cast<std::ptrdiff_t>(i) );
				break;
			default:	// role
				role = static_cast<int>(m_rng.below( ROLES ));
				break;
		}
	}

	std::vector<std::string> m_corpus;
	Rng m_rng;
};

}	// namespace

int main( int argc, char **argv )
{
	Harness::installCrashHandlers( "current.lan" );

	if ( argc == 4 && std::strcmp( argv[1], "--run" ) == 0 )
	{
		Harness::createSubsystems();
		createLanSubsystems();
		Harness::loadBase( argv[2] );
		Harness::verbose = true;
		runInput( readFile( argv[3] ) );
		std::printf( "%s: read, %lld assertions\n", argv[3], Harness::assertions );
		return 0;
	}
	if ( argc != 4 )
	{
		std::fprintf( stderr, "usage: LanFuzz <seed dir> <iterations> <rng seed> | --run <seed dir> <file>\n" );
		return 2;
	}
	const long long iterations = std::atoll( argv[2] );
	const std::uint64_t rngSeed = std::strtoull( argv[3], nullptr, 10 );

	Harness::createSubsystems();
	createLanSubsystems();
	Harness::loadBase( argv[1] );

	// the project texts (ProjectData\Text, copied by run.sh) reach the text manager
	Bool projectText = FALSE;
	TheGameText->fetch( "GZH:LanErrorVersion", &projectText );
	if ( !projectText )
	{
		std::fprintf( stderr, "project texts not loaded (ProjectData\\Text)\n" );
		return 1;
	}

	if ( lobbyChecks( hostOptions() ) != 0 )
		return 1;

	std::vector<std::string> corpus = makeSeeds();
	long long badSeeds = 0;
	for ( std::size_t k = 0; k < corpus.size(); ++k )
	{
		const long long before = Harness::assertions;
		Harness::resetCoverage();
		runInput( corpus[k] );
		Harness::newCoverage();
		badSeeds += Harness::assertions != before;
		writeFile( Harness::workDir / ("seed-" + std::to_string( k ) + ".lan"), corpus[k] );
	}

	Harness::resetDictionary();
	Harness::startWatchdog();
	LanMutator mutator( corpus, rngSeed );
	long long added = 0;
	long long done = 0;
	for ( long long i = 0; i < iterations && !Harness::timeUp(); ++i, ++done )
	{
		Harness::iteration = i;
		Harness::resetCoverage();
		const std::string d = mutator.next();
		runInput( d );
		if ( Harness::newCoverage() && mutator.size() < 10000 )
		{
			mutator.add( d );
			++added;
		}
		++Harness::progress;
	}
	TheMemoryPoolFactory->debugMemoryVerify();

	std::printf( "%lld inputs; %lld assertions; %lld seeds not clean; %lld added to the corpus, %zu edges, %zu dictionary values\n",
		done, Harness::assertions, badSeeds, added, Harness::coveredEdges(), Harness::dictionarySize() );
	return badSeeds ? 1 : 0;
}
