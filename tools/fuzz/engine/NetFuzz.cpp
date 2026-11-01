/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Game network harness: feeds packets of other players to ConnectionManager::update, the way they
// arrive during a network game (before and after the game starts): acks, frame info, run ahead,
// chat, load progress, file transfer, disconnect votes, player leave, wrapped commands, game
// commands. Three players: slot 0 host and packet router, slot 1 us, slot 2 another player. A fresh
// ConnectionManager per input, with a transport that has no socket (nothing is sent).
// Input: flags byte (bit 0: in game), then packets of a sender byte, a 16-bit length and the data.
//
//   NetFuzz <seed dir> <iterations> <rng seed>   fuzz generated seeds
//   NetFuzz --run <seed dir> <file>              replay one input with the debug log

#include "PreRTS.h"

#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/PlayerList.h"
#include "Common/Recorder.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/ConnectionManager.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/NetCommandMsg.h"
#include "GameNetwork/NetCommandList.h"
#include "GameNetwork/NetCommandRef.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/Transport.h"

#include "Harness.h"
#include "NetHarness.h"

#include <cstdio>
#include <cstring>

using Harness::Rng;
using Harness::readFile;
using Harness::writeFile;

namespace
{

constexpr UnsignedInt LOCAL_IP = 0x0A000001;
constexpr UnsignedInt SLOT_IP[3] = { 0x0A000002, LOCAL_IP, 0x0A000003 };
constexpr UnsignedInt STRANGER_IP = 0x0A000009;

UnsignedInt senderIp( unsigned char sender )
{
	switch ( sender & 3 )
	{
		case 0: return SLOT_IP[0];
		case 1: return SLOT_IP[2];
		case 2: return STRANGER_IP;
		default: return SLOT_IP[0];
	}
}

SkirmishGameInfo *s_game = nullptr;

// The disconnect manager asks the network for frames and to notify the others; the rest of the
// network is not part of the harness.
class HarnessNetwork : public NetworkInterface
{
public:
	ConnectionManager *m_cm = nullptr;

	virtual void init( void ) override {}
	virtual void reset( void ) override {}
	virtual void update( void ) override {}
	virtual void liteupdate( void ) override {}
	virtual void setLocalAddress( UnsignedInt, UnsignedInt ) override {}
	virtual Bool isFrameDataReady( void ) override { return FALSE; }
	virtual void parseUserList( const GameInfo * ) override {}
	virtual void startGame( void ) override {}
	virtual UnsignedInt getRunAhead( void ) override { return 10; }
	virtual UnsignedInt getFrameRate( void ) override { return 30; }
	virtual UnsignedInt getPacketArrivalCushion( void ) override { return 0; }
	virtual void sendChat( UnicodeString, Int ) override {}
	virtual void sendDisconnectChat( UnicodeString ) override {}
	virtual void sendFile( AsciiString, UnsignedByte, UnsignedShort ) override {}
	virtual UnsignedShort sendFileAnnounce( AsciiString, UnsignedByte ) override { return 0; }
	virtual Int getFileTransferProgress( Int, AsciiString ) override { return 0; }
	virtual Bool areAllQueuesEmpty( void ) override { return FALSE; }
	virtual void quitGame( void ) override {}
	virtual Bool voteToRemovePlayer( Int ) override { return FALSE; }
	virtual void voteForPlayerDisconnect( Int ) override {}
	virtual Bool isPacketRouter( void ) override { return FALSE; }
	virtual Real getIncomingBytesPerSecond( void ) override { return 0.0f; }
	virtual Real getIncomingPacketsPerSecond( void ) override { return 0.0f; }
	virtual Real getOutgoingBytesPerSecond( void ) override { return 0.0f; }
	virtual Real getOutgoingPacketsPerSecond( void ) override { return 0.0f; }
	virtual Real getUnknownBytesPerSecond( void ) override { return 0.0f; }
	virtual Real getUnknownPacketsPerSecond( void ) override { return 0.0f; }
	virtual void updateLoadProgress( Int ) override {}
	virtual void loadProgressComplete( void ) override {}
	virtual void sendTimeOutGameStart( void ) override {}
	virtual UnsignedInt getLocalPlayerID( void ) override { return m_cm ? m_cm->getLocalPlayerID() : 0; }
	virtual UnicodeString getPlayerName( Int ) override { return UnicodeString::TheEmptyString; }
	virtual Int getNumPlayers( void ) override { return 0; }
	virtual Int getAverageFPS( void ) override { return 0; }
	virtual Int getSlotAverageFPS( Int ) override { return 0; }
	virtual void attachTransport( Transport * ) override {}
	virtual void setLockstepEvents( LockstepEvents & ) override {}
	virtual void initTransport( void ) override {}
	virtual Bool sawCRCMismatch( void ) override { return FALSE; }
	virtual void setSawCRCMismatch( void ) override {}
	virtual Bool isPlayerConnected( Int playerID ) override { return m_cm ? m_cm->isPlayerConnected( playerID ) : FALSE; }
	virtual void notifyOthersOfCurrentFrame( void ) override { if ( m_cm ) m_cm->notifyOthersOfCurrentFrame( TheGameLogic->getFrame() ); }
	virtual void notifyOthersOfNewFrame( UnsignedInt frame ) override { if ( m_cm ) m_cm->notifyOthersOfCurrentFrame( frame ); }
	virtual Int getExecutionFrame( void ) override { return TheGameLogic->getFrame() + 1; }
	virtual void toggleNetworkOn( void ) override {}
	virtual UnsignedInt getPingFrame( void ) override { return 0; }
	virtual Int getPingsSent( void ) override { return 0; }
	virtual Int getPingsRecieved( void ) override { return 0; }
	virtual UnsignedInt getPacketRouterSlot( void ) override { return m_cm ? m_cm->getPacketRouterSlot() : 0; }
	virtual void setReturnWindow( UnsignedInt ) override {}
	virtual Bool isReturnable( Int ) override { return FALSE; }
	virtual UnsignedInt announcePlayerReturn( Int, const UnsignedByte *, UnsignedShort, const ::Engine::WireEndpoint &,
		UnsignedInt &prefill ) override { prefill = 0; return 0; }
	virtual Bool getReturnState( Int, ::Engine::SessionReturnState & ) override { return FALSE; }
	virtual void beginReturn( ReturnFeed * ) override {}
	virtual void setReturnFrame( UnsignedInt, UnsignedInt, UnsignedInt ) override {}
	virtual void applyReturnState( const ::Engine::SessionReturnState & ) override {}
	virtual Bool isReturning( void ) override { return FALSE; }
	virtual UnsignedInt getReturnFrame( void ) override { return 0; }
	virtual Bool isCatchingUp( void ) override { return FALSE; }
	virtual Bool isCRCPending( Int ) override { return FALSE; }
	virtual void noteCRCSkipped( Int ) override {}
};

HarnessNetwork *s_network = nullptr;
long long s_framesRun = 0;	///< complete frames, to see that the frame path is reached

void createNetSubsystems()
{
	Harness::create( ::Engine::context().gameText, CreateGameTextInterface() );
	::Engine::context().mapCache = new MapCache;
	::Engine::context().gameLogic = new GameLogic;	// load progress goes there; not initialised
	Harness::create( ::Engine::context().playerList, new PlayerList );	// game messages ask for the local player
	::Engine::context().windowManager = new NetHarness::WindowManager;	// not initialised; no windows
	::Engine::context().inGameUI = new NetHarness::InGameUI;	// not initialised
	Harness::create( ::Engine::context().recorder, new RecorderClass );	// logs disconnects
	s_network = new HarnessNetwork;
	::Engine::context().network = s_network;
}

// The game of each input: disconnects mark its slots, so every input starts from a fresh one.
void newGame()
{
	::Engine::context().gameInfo = nullptr;
	delete s_game;
	s_game = new SkirmishGameInfo;
	s_game->init();
	s_game->clearSlotList();
	s_game->reset();
	s_game->enterGame();
	const wchar_t *names[3] = { L"Host", L"Harness", L"Carl" };
	for ( Int i = 0; i < 3; ++i )
	{
		GameSlot slot;
		slot.setState( SLOT_PLAYER, UnicodeString( names[i] ) );
		slot.setIP( SLOT_IP[i] );
		slot.setPort( 8088 );
		s_game->setSlot( i, slot );
	}
	s_game->setLocalIP( LOCAL_IP );
	AsciiString map = TheMapCache->getMapDir();
	map.concat( "\\alpine\\alpine.map" );
	s_game->setMap( map );
	::Engine::context().gameInfo = s_game;
}

// What Network::update does with a complete frame: game commands become game messages (dropped
// here), a player leaving is processed (Network::processFrameSynchronizedNetCommand).
void runFrames( ConnectionManager *cm, UnsignedInt &frame )
{
	for ( int ready = 0; ready < 4; ++ready )
	{
		// our frame info, as ConnectionManager::processFrameTick sends it (without its frame rate
		// measurement, which needs a display); no local commands, so the count is 0
		NetFrameCommandMsg *tick = newInstance( NetFrameCommandMsg );
		tick->setExecutionFrame( frame );
		tick->setCommandCount( 0 );
		tick->setPlayerID( cm->getLocalPlayerID() );
		cm->sendLocalCommand( tick, 0xff & ~(1 << cm->getLocalPlayerID()) );
		tick->detach();
		if ( !cm->allCommandsReady( frame ) )
			return;
		cm->handleAllCommandsReady();
		NetCommandList *list = cm->getFrameCommandList( frame );
		for ( NetCommandRef *ref = list->getFirstMessage(); ref != nullptr; ref = ref->getNext() )
		{
			NetCommandMsg *msg = ref->getCommand();
			if ( msg->getNetCommandType() == NETCOMMANDTYPE_GAMECOMMAND )
				static_cast<NetGameCommandMsg *>(msg)->constructGameMessage()->deleteInstance();
			else if ( msg->getNetCommandType() == NETCOMMANDTYPE_PLAYERLEAVE )
				cm->processPlayerLeave( static_cast<NetPlayerLeaveCommandMsg *>(msg) );
		}
		list->deleteInstance();
		++frame;
		++s_framesRun;
	}
}

void runInput( const std::string &data )
{
	writeFile( Harness::workDir / "current.net", data );	// kept on a crash
	if ( data.empty() )
		return;
	const Bool inGame = (data[0] & 1) != 0;
	newGame();

	static NetHarness::QuietEvents quiet;
	ConnectionManager *cm = new ConnectionManager;
	cm->setEvents( quiet );
	cm->init();
	cm->setLocalAddress( LOCAL_IP, 8088 );
	Transport *transport = new Transport;	// no socket: nothing is received or sent but what we put in
	cm->attachTransport( transport );	// owned by the connection manager
	cm->parseUserList( s_game );
	s_network->m_cm = cm;

	UnsignedInt frame = 5;	// the seeds' commands start there
	std::size_t pos = 1;
	for ( int n = 0; pos + 3 <= data.size() && n < 32; ++n )
	{
		const unsigned char sender = static_cast<unsigned char>(data[pos]);
		std::size_t length = static_cast<unsigned char>(data[pos + 1]) | (static_cast<unsigned char>(data[pos + 2]) << 8);
		pos += 3;
		length = std::min<std::size_t>( { length, data.size() - pos, static_cast<std::size_t>(MAX_MESSAGE_LEN) } );
		TransportMessage &in = transport->m_inBuffer[0];
		std::memcpy( in.data, data.data() + pos, length );
		in.length = static_cast<Int>(length);
		in.addr = senderIp( sender );
		in.port = 8088;
		cm->update( inGame );
		if ( inGame )
			runFrames( cm, frame );
		for ( Int i = 0; i < MAX_MESSAGES; ++i )
		{
			transport->m_inBuffer[i].length = 0;
			transport->m_outBuffer[i].length = 0;
		}
		pos += length;
	}
	s_network->m_cm = nullptr;
	delete cm;
}

// --- seeds ---------------------------------------------------------------------------------------

template <typename T>
T *command( T *msg, NetCommandType type, UnsignedInt player, UnsignedShort id, UnsignedInt frame )
{
	msg->setNetCommandType( type );
	msg->setPlayerID( player );
	msg->setID( id );
	msg->setExecutionFrame( frame );
	return msg;
}

std::string packetOf( std::vector<NetCommandMsg *> msgs )
{
	NetPacket *packet = newInstance( NetPacket );
	for ( NetCommandMsg *m : msgs )
	{
		NetCommandRef *ref = NEW_NETCOMMANDREF( m );
		ref->setRelay( 0xff );
		packet->addCommand( ref );
		ref->deleteInstance();
	}
	std::string out( reinterpret_cast<const char *>(packet->getData()), packet->getLength() );
	packet->deleteInstance();
	return out;
}

void appendPacket( std::string &d, unsigned char sender, const std::string &packet )
{
	d += static_cast<char>(sender);
	d += static_cast<char>(packet.size() & 0xff);
	d += static_cast<char>(packet.size() >> 8);
	d += packet;
}

std::vector<std::string> makeSeeds()
{
	static UnsignedByte payload[64];
	for ( Int i = 0; i < 64; ++i )
		payload[i] = static_cast<UnsignedByte>(i * 7 + 1);

	std::vector<std::vector<NetCommandMsg *>> packets;
	// before the game: progress, file transfer, load complete
	{
		NetProgressCommandMsg *p = command( newInstance( NetProgressCommandMsg ), NETCOMMANDTYPE_PROGRESS, 0, 0, 0 );
		p->setPercentage( 50 );
		NetFileAnnounceCommandMsg *a = command( newInstance( NetFileAnnounceCommandMsg ), NETCOMMANDTYPE_FILEANNOUNCE, 0, 10, 0 );
		a->setPortableFilename( AsciiString( "userdata\\maps\\test\\test.map" ) );
		a->setFileID( 3 );
		a->setPlayerMask( 0x06 );
		NetFileCommandMsg *f = command( newInstance( NetFileCommandMsg ), NETCOMMANDTYPE_FILE, 0, 11, 0 );
		f->setPortableFilename( AsciiString( "userdata\\maps\\test\\test.map" ) );
		f->setFileData( payload, sizeof( payload ) );
		NetFileProgressCommandMsg *fp = command( newInstance( NetFileProgressCommandMsg ), NETCOMMANDTYPE_FILEPROGRESS, 2, 12, 0 );
		fp->setFileID( 3 );
		fp->setProgress( 40 );
		packets.push_back( { p, a } );
		packets.push_back( { f, fp } );
		packets.push_back( { command( newInstance( NetCommandMsg ), NETCOMMANDTYPE_LOADCOMPLETE, 0, 13, 0 ),
			command( newInstance( NetCommandMsg ), NETCOMMANDTYPE_TIMEOUTSTART, 0, 14, 0 ) } );
	}
	// in the game: frames, run ahead, chat, acks, keep alive
	{
		NetFrameCommandMsg *fr = command( newInstance( NetFrameCommandMsg ), NETCOMMANDTYPE_FRAMEINFO, 0, 20, 5 );
		fr->setCommandCount( 1 );
		NetGameCommandMsg *game = command( newInstance( NetGameCommandMsg ), NETCOMMANDTYPE_GAMECOMMAND, 0, 21, 5 );
		game->setGameMessageType( static_cast<GameMessage::Type>(1001) );
		GameMessageArgumentType arg;
		std::memset( &arg, 0, sizeof( arg ) );
		arg.boolean = TRUE;
		game->addArgument( ARGUMENTDATATYPE_BOOLEAN, arg );
		arg.objectID = static_cast<ObjectID>(7);
		game->addArgument( ARGUMENTDATATYPE_OBJECTID, arg );
		NetRunAheadCommandMsg *ra = command( newInstance( NetRunAheadCommandMsg ), NETCOMMANDTYPE_RUNAHEAD, 0, 22, 6 );
		ra->setRunAhead( 12 );
		ra->setFrameRate( 30 );
		NetRunAheadMetricsCommandMsg *rm = command( newInstance( NetRunAheadMetricsCommandMsg ), NETCOMMANDTYPE_RUNAHEADMETRICS, 2, 23, 0 );
		rm->setAverageLatency( 0.2f );
		rm->setAverageFps( 30 );
		NetChatCommandMsg *chat = command( newInstance( NetChatCommandMsg ), NETCOMMANDTYPE_CHAT, 2, 24, 7 );
		chat->setText( UnicodeString( L"hello" ) );
		chat->setPlayerMask( 0x07 );
		NetAckBothCommandMsg *ack = command( newInstance( NetAckBothCommandMsg ), NETCOMMANDTYPE_ACKBOTH, 0, 0, 0 );
		ack->setCommandID( 1 );
		ack->setOriginalPlayerID( 1 );
		packets.push_back( { fr, game } );
		packets.push_back( { ra, rm, chat, ack } );
		packets.push_back( { command( newInstance( NetKeepAliveCommandMsg ), NETCOMMANDTYPE_KEEPALIVE, 2, 0, 0 ),
			command( newInstance( NetPacketRouterQueryCommandMsg ), NETCOMMANDTYPE_PACKETROUTERQUERY, 2, 25, 0 ) } );
	}
	// disconnects and leaving
	{
		NetDisconnectPlayerCommandMsg *dp = command( newInstance( NetDisconnectPlayerCommandMsg ), NETCOMMANDTYPE_DISCONNECTPLAYER, 0, 30, 0 );
		dp->setDisconnectSlot( 2 );
		dp->setDisconnectFrame( 0 );	// the frame the harness is in
		NetDisconnectVoteCommandMsg *vote = command( newInstance( NetDisconnectVoteCommandMsg ), NETCOMMANDTYPE_DISCONNECTVOTE, 2, 31, 0 );
		vote->setSlot( 0 );
		vote->setVoteFrame( 0 );
		NetDisconnectChatCommandMsg *dc = command( newInstance( NetDisconnectChatCommandMsg ), NETCOMMANDTYPE_DISCONNECTCHAT, 2, 0, 0 );
		dc->setText( UnicodeString( L"lag" ) );
		NetDisconnectFrameCommandMsg *df = command( newInstance( NetDisconnectFrameCommandMsg ), NETCOMMANDTYPE_DISCONNECTFRAME, 0, 32, 0 );
		df->setDisconnectFrame( 0 );
		NetDisconnectScreenOffCommandMsg *so = command( newInstance( NetDisconnectScreenOffCommandMsg ), NETCOMMANDTYPE_DISCONNECTSCREENOFF, 0, 33, 0 );
		so->setNewFrame( 101 );
		NetPlayerLeaveCommandMsg *leave = command( newInstance( NetPlayerLeaveCommandMsg ), NETCOMMANDTYPE_PLAYERLEAVE, 2, 34, 8 );
		leave->setLeavingPlayerID( 2 );
		NetFrameResendRequestCommandMsg *resend = command( newInstance( NetFrameResendRequestCommandMsg ), NETCOMMANDTYPE_FRAMERESENDREQUEST, 2, 35, 0 );
		resend->setFrameToResend( 3 );
		NetWrapperCommandMsg *wrapper = command( newInstance( NetWrapperCommandMsg ), NETCOMMANDTYPE_WRAPPER, 0, 36, 0 );
		wrapper->setData( payload, sizeof( payload ) );
		wrapper->setChunkNumber( 0 );
		wrapper->setNumChunks( 1 );
		wrapper->setTotalDataLength( sizeof( payload ) );
		wrapper->setDataOffset( 0 );
		wrapper->setWrappedCommandID( 0x2222 );
		packets.push_back( { dp, vote, dc } );
		packets.push_back( { df, so, command( newInstance( NetDisconnectKeepAliveCommandMsg ), NETCOMMANDTYPE_DISCONNECTKEEPALIVE, 0, 0, 0 ) } );
		packets.push_back( { leave, resend, wrapper } );
	}

	std::vector<std::string> bytes;
	for ( auto &p : packets )
	{
		bytes.push_back( packetOf( p ) );
		for ( NetCommandMsg *m : p )
			m->detach();
	}

	std::vector<std::string> seeds;
	for ( int inGame = 0; inGame < 2; ++inGame )
	{
		std::string d( 1, static_cast<char>(inGame) );
		for ( std::size_t i = 0; i < bytes.size(); ++i )
			appendPacket( d, i % 3 == 2 ? 1 : 0, bytes[i] );
		seeds.push_back( d );
	}
	for ( const std::string &b : bytes )	// each packet on its own
	{
		std::string d( 1, 1 );
		appendPacket( d, 0, b );
		seeds.push_back( d );
	}
	return seeds;
}

// --- mutation ------------------------------------------------------------------------------------

const std::uint32_t s_ints[] = {
	0, 1, 2, 3, 7, 8, 9, 0x7F, 0x80, 0xFF, 0x100, 0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu,
};

struct Packet
{
	std::size_t header;		///< offset of the sender byte
	std::size_t data;
	std::size_t length;
};

std::vector<Packet> packetsOf( const std::string &d )
{
	std::vector<Packet> out;
	std::size_t pos = 1;
	while ( pos + 3 <= d.size() )
	{
		std::size_t length = static_cast<unsigned char>(d[pos + 1]) | (static_cast<unsigned char>(d[pos + 2]) << 8);
		length = std::min( length, d.size() - pos - 3 );
		out.push_back( { pos, pos + 3, length } );
		pos += 3 + length;
	}
	return out;
}

void setLength( std::string &d, const Packet &p, std::size_t length )
{
	d[p.header + 1] = static_cast<char>(length & 0xff);
	d[p.header + 2] = static_cast<char>(length >> 8);
}

class NetMutator
{
public:
	NetMutator( std::vector<std::string> corpus, std::uint64_t seed ) : m_corpus( std::move( corpus ) ), m_rng{ seed } {}
	void add( const std::string &d ) { m_corpus.push_back( d ); }
	std::size_t size() const { return m_corpus.size(); }

	std::string next()
	{
		std::string d = m_corpus[m_rng.below( m_corpus.size() )];
		const int count = 1 + static_cast<int>(m_rng.below( 5 ));
		for ( int i = 0; i < count; ++i )
			mutate( d );
		return d;
	}

private:
	void mutate( std::string &d )
	{
		std::vector<Packet> ps = packetsOf( d );
		if ( ps.empty() )
		{
			d = m_corpus[0];
			return;
		}
		const Packet p = ps[m_rng.below( ps.size() )];
		switch ( m_rng.below( 9 ) )
		{
			case 0:	// a byte (tags, types, player IDs, lengths)
				if ( p.length )
					d[p.data + m_rng.below( p.length )] = static_cast<char>(m_rng.next());
				break;
			case 1:	// a 32-bit value (frames, counts, IDs, sizes)
				if ( p.length >= 4 )
				{
					const std::uint32_t v = Harness::pickInt( m_rng, s_ints );
					std::memcpy( &d[p.data + m_rng.below( p.length - 3 )], &v, 4 );
				}
				break;
			case 2:	// a 16-bit value (command IDs, counts)
				if ( p.length >= 2 )
				{
					const std::uint16_t v = static_cast<std::uint16_t>(Harness::pickInt( m_rng, s_ints ));
					std::memcpy( &d[p.data + m_rng.below( p.length - 1 )], &v, 2 );
				}
				break;
			case 3:	// cut the packet
				if ( p.length )
				{
					const std::size_t keep = m_rng.below( p.length );
					d.erase( p.data + keep, p.length - keep );
					setLength( d, p, keep );
				}
				break;
			case 4:	// copy a stretch of this packet to another place in it
				if ( p.length >= 8 && p.length < MAX_MESSAGE_LEN - 64 )
				{
					const std::size_t from = m_rng.below( p.length - 4 );
					const std::size_t len = 1 + m_rng.below( std::min<std::size_t>( 32, p.length - from ) );
					d.insert( p.data + m_rng.below( p.length ), d.substr( p.data + from, len ) );
					setLength( d, p, p.length + len );
				}
				break;
			case 5:	// sender
				d[p.header] = static_cast<char>(m_rng.next());
				break;
			case 6:	// repeat a packet
				if ( ps.size() < 32 )
					d.insert( ps[m_rng.below( ps.size() )].header, d.substr( p.header, 3 + p.length ) );
				break;
			case 7:	// a packet from another input
			{
				const std::string &other = m_corpus[m_rng.below( m_corpus.size() )];
				std::vector<Packet> ops = packetsOf( other );
				if ( !ops.empty() && ps.size() < 32 )
				{
					const Packet &o = ops[m_rng.below( ops.size() )];
					d.insert( p.header, other.substr( o.header, 3 + o.length ) );
				}
				break;
			}
			default:	// before or in the game
				d[0] = static_cast<char>(d[0] ^ 1);
				break;
		}
	}

	std::vector<std::string> m_corpus;
	Rng m_rng;
};

}	// namespace

int main( int argc, char **argv )
{
	Harness::installCrashHandlers( "current.net" );

	if ( argc == 4 && std::strcmp( argv[1], "--run" ) == 0 )
	{
		Harness::createSubsystems();
		createNetSubsystems();
		Harness::loadBase( argv[2] );
		Harness::verbose = true;
		runInput( readFile( argv[3] ) );
		std::printf( "%s: read, %lld assertions\n", argv[3], Harness::assertions );
		return 0;
	}
	if ( argc != 4 )
	{
		std::fprintf( stderr, "usage: NetFuzz <seed dir> <iterations> <rng seed> | --run <seed dir> <file>\n" );
		return 2;
	}
	const long long iterations = std::atoll( argv[2] );
	const std::uint64_t rngSeed = std::strtoull( argv[3], nullptr, 10 );

	Harness::createSubsystems();
	createNetSubsystems();
	Harness::loadBase( argv[1] );

	std::vector<std::string> corpus = makeSeeds();
	long long badSeeds = 0;
	for ( std::size_t k = 0; k < corpus.size(); ++k )
	{
		const long long before = Harness::assertions;
		Harness::resetCoverage();
		runInput( corpus[k] );
		Harness::newCoverage();
		badSeeds += Harness::assertions != before;
		writeFile( Harness::workDir / ("seed-" + std::to_string( k ) + ".net"), corpus[k] );
	}

	Harness::resetDictionary();
	Harness::startWatchdog();
	NetMutator mutator( corpus, rngSeed );
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

	std::printf( "%lld inputs; %lld frames run; %lld assertions; %lld seeds not clean; %lld added to the corpus, %zu edges, %zu dictionary values\n",
		done, s_framesRun, Harness::assertions, badSeeds, added, Harness::coveredEdges(), Harness::dictionarySize() );
	// seeds with assertions only report: the audio events of chat are not in the test data
	return 0;
}
