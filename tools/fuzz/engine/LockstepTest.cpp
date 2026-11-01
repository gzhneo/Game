/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Lockstep test: several players of one network game in one process, each with its own engine
// context (network, game logic frame, command list, game info), over real sockets on the loopback.
// Every player is started the way the lobby starts a game (socket opened before, handed over with
// attachTransport, sealed with the keys of the slot list). The test plays the part of the game
// loop: a frame tick and now and then a game command per frame, Network::update, and when the
// network says the frame is complete, the commands of that frame are taken off the command list
// and the logic frame advances.
//
// Checked for each scenario: every player reaches the last frame; all players execute the same
// commands in the same frames; every command sent is executed exactly once.
//
//   1. two players
//   2. three players, the network emulator dropping, duplicating, delaying and reordering packets
//   3. three players, one leaves during the game; the others play on
//   4. three players, one stops answering (crashed, cable pulled); the others wait for it, drop it
//      when it times out and play on
//   5. three players, the other two vote to remove one; it is removed in the same frame everywhere
//      and none of the commands it sent for later frames is executed
//   6. as 5, the one removed is the packet router
//   7. three players, one is away for 1.5 s (network gone) and comes back on another port (new
//      address after a router restart); the others wait, nobody is dropped, all play on alike
//   8. two players and an observer, the observer is away for 1.5 s: the players do not wait for it,
//      it catches up and executes what they execute; its own commands are executed nowhere
//   9. two players and an observer that stops answering: nobody waits, the players let it go
//  10. two players and an observer that leaves
//  11. as 8 with the host observing (slot 0): a player is the packet router
//  12. three players, one stops answering and is dropped; it comes back with a new key on another
//      port: the frames before its return come from the host's executed commands (the replay), from
//      the return frame on it plays in the lockstep again; nobody destroys its units
//  13. as 12 without coming back: once the return window has run out everybody closes it in the same frame
// In a game of two there is no vote (scenario 1 tries).
//
//   LockstepTest <seed dir> <ignored> <ignored>   (arguments as the harnesses, see run.sh)
//   LOCKSTEP_SCENARIO=<n>: only scenario n; HARNESS_VERBOSE=1: with the debug log
// Exit code 0 on success, 1 on a failed check.

#include "PreRTS.h"

// The test advances each player's logic frame itself; GameLogic keeps the counter private.
#define private public
#include "GameLogic/GameLogic.h"
#undef private

#include "Engine/EngineContext.h"
#include "Engine/NetAddressBook.h"
#include "Engine/PacketSeal.h"
#include "Engine/SessionHandshake.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/PlayerList.h"
#include "Common/Recorder.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/LANGameInfo.h"
#include "GameNetwork/LockstepEvents.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/networkutil.h"
#include "GameNetwork/Transport.h"

#include "Harness.h"
#include "NetHarness.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
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
constexpr UnsignedShort FIRST_PORT = 28500;
constexpr GameMessage::Type COMMAND_TYPE = GameMessage::MSG_DO_STOP;

struct Scenario
{
	const char *name;
	Int players;
	UnsignedInt lastFrame;			///< every player plays up to here
	UnsignedInt lastCommandFrame;	///< no commands after this frame (they must still be executed)
	Int leaver;						///< slot that leaves the game, -1 for none
	UnsignedInt leaveFrame;
	Bool hangs;						///< the leaver does not leave but stops answering
	Bool votedOut;					///< the leaver does not leave; the others vote it out from leaveFrame on
	Bool emulate;					///< loss, duplicates, latency and reordering on every socket
	Int timeoutSeconds;
	Int away = -1;					///< slot that is away for a while and comes back on another port
	UnsignedInt awayFrame = 0;
	Int observer = -1;				///< slot that watches instead of playing
	Bool comesBack = FALSE;			///< the hanging leaver comes back after the drop (new key, new port)
	Int returnWindowSeconds = 300;
};

constexpr Int AWAY_MS = 1500;

// What the network reports to the player, counted instead of shown.
class RecordedEvents final : public LockstepEvents
{
public:
	std::vector<Int> left;
	Int changes = 0;
	Int chats = 0;
	Int waits = 0;					///< times the waiting screen was shown
	Bool waiting = FALSE;
	std::set<Int> waitedFor;		///< players listed while waiting
	Int lowestCountdown = -1;

	void playerLeft( Int slot, const UnicodeString & ) override { left.push_back( slot ); }
	void playersChanged( void ) override { ++changes; }
	void chat( Int, UnsignedByte playerMask, Int localSlot, const UnicodeString & ) override
	{
		if ( playerMask & (1 << localSlot) )
			++chats;
	}
	void disconnectChat( const UnicodeString & ) override {}
	void fileNotSent( const UnicodeString & ) override {}
	void waitingForPlayers( Bool on ) override
	{
		if ( on && !waiting )
			++waits;
		waiting = on;
	}
	void waitingPlayer( Int slot, const UnicodeString &name, Int ) override
	{
		if ( name.getLength() > 0 )
			waitedFor.insert( slot );
	}
	void dropCountdown( Int, Int milliseconds ) override
	{
		if ( lowestCountdown < 0 || milliseconds < lowestCountdown )
			lowestCountdown = milliseconds;
	}
	void networkClosed( void ) override {}
	void removalVote( Int voter, Int target, Int votes, Int needed ) override
	{
		lastVote = { voter, target, votes, needed };
		++voteReports;
	}
	void removedByVote( Int slot, const UnicodeString & ) override { removed.push_back( slot ); }
	void playerReturned( Int slot, const UnicodeString & ) override { returned.push_back( slot ); }
	void returnClosed( Int slot ) override { closed.push_back( slot ); }
	std::vector<Int> returned;
	std::vector<Int> closed;

	struct Vote { Int voter = -1, target = -1, votes = 0, needed = 0; };
	Vote lastVote;
	Int voteReports = 0;
	std::vector<Int> removed;
};

constexpr UnsignedInt CHAT_FRAME = 50;

class Player;

/// The commands one player executed, frame by frame, as the replay of the host holds them.
struct Executed
{
	Int slot = 0;
	Int number = 0;
	Real real = 0.0f;
	Bool flag = FALSE;
};

/// A returning player's frames before its return frame: what the host executed, a few frames late.
class HostFeed final : public ReturnFeed
{
public:
	explicit HostFeed( const Player &host ) : m_host( host ) {}
	UnsignedInt completeFrames( void ) override;
	void appendFrame( UnsignedInt frame, CommandList *list ) override;
	UnsignedInt appended = 0;
private:
	const Player &m_host;
};

constexpr UnsignedInt FEED_LAG = 5;

class Player
{
public:
	/// `generation` 1: the same player back with a new key
	Player( Int slot, UnsignedShort port, Int generation = 0 ) : m_slot( slot ), m_port( port ), m_generation( generation )
	{
		m_sent = firstNumber();
		UnsignedByte secret[::Engine::SEAL_KEY_SIZE];
		for ( Int i = 0; i < ::Engine::SEAL_KEY_SIZE; ++i )
			secret[i] = static_cast<UnsignedByte>(0x40 + slot * 31 + i + generation * 7);
		m_keys.fromSecret( secret );
		m_identity = ::Engine::SessionHandshake::identityOf( m_keys.publicKey );
	}
	Player( const Player & ) = delete;
	Player &operator=( const Player & ) = delete;

	~Player()
	{
		::Engine::ScopedContext scope( m_context );
		delete TheNetwork;		// the connection manager deletes the transport and the disconnect screen
		::Engine::context().network = nullptr;
		::Engine::context().gameInfo = nullptr;
		delete m_game;
		delete TheCommandList;
		::Engine::context().commandList = nullptr;
		delete TheMessageStream;
		::Engine::context().messageStream = nullptr;
		// GameLogic is not initialised and not torn down; it only carries the frame
	}

	Int slot() const { return m_slot; }
	UnsignedShort port() const { return m_port; }
	UnsignedInt identity() const { return m_identity; }
	const UnsignedByte *publicKey() const { return m_keys.publicKey; }

	/// As the lobby does at the start: game socket already open, then the network for this game.
	void start( const std::vector<Player *> &all, Int observer, Int returnWindowSeconds )
	{
		m_context = *::Engine::g_currentContext;	// the shared stores; the game's own objects below
		::Engine::ScopedContext scope( m_context );
		::Engine::context().gameLogic = new GameLogic;
		::Engine::context().commandList = new CommandList;
		TheCommandList->init();
		::Engine::context().messageStream = new MessageStream;
		TheMessageStream->init();

		m_game = new LANGameInfo;
		m_game->enterGame();
		m_game->setSeed( 4711 );
		m_game->setLocalIP( m_identity );
		for ( const Player *p : all )
		{
			LANGameSlot slot;
			AsciiString name;
			name.format( "Player%d", p->slot() );
			UnicodeString uname;
			uname.translate( name );
			slot.setState( SLOT_PLAYER, uname );
			if ( p->slot() == observer )
				slot.setPlayerTemplate( PLAYERTEMPLATE_OBSERVER );
			slot.setIP( p->identity() );
			slot.setPort( p->port() );
			m_game->setSlot( p->slot(), slot );
			m_game->setPublicKey( p->slot(), p->publicKey() );
		}
		AsciiString map = TheMapCache->getMapDir();
		map.concat( "\\alpine\\alpine.map" );
		m_game->setMap( map );
		m_game->startGame( 0 );
		::Engine::context().gameInfo = m_game;

		Transport *socket = new Transport;
		m_socket = socket;
		const Bool open = socket->init( LOOPBACK, m_port );
		for ( const Player *p : all )
		{
			if ( p != this )	// as the probes of the lobby found them
				socket->bindPeer( p->identity(), ::Engine::NetAddress::fromIPv4( LOOPBACK, p->port() ) );
		}

		::Engine::context().network = NetworkInterface::createNetwork();
		TheNetwork->init();
		TheNetwork->setLockstepEvents( m_events );
		TheNetwork->setLocalAddress( m_identity, m_port );
		TheNetwork->attachTransport( socket );
		m_keysComplete = m_game->deriveSealKeys( m_keys );
		TheNetwork->parseUserList( m_game );
		TheNetwork->setReturnWindow( static_cast<UnsignedInt>(returnWindowSeconds) );
		m_started = open;
	}

	/// The same player back in the running game: the frames before the return from `feed`.
	void startReturning( const std::vector<Player *> &all, ReturnFeed &feed )
	{
		start( all, -1, 300 );
		::Engine::ScopedContext scope( m_context );
		TheNetwork->beginReturn( &feed );
		m_returning = TRUE;
	}

	// what the host's lobby does for a returning player
	UnsignedInt announceReturn( Int slot, const Player &back, UnsignedInt &prefill )
	{
		::Engine::ScopedContext scope( m_context );
		const ::Engine::WireEndpoint endpoint = ::Engine::WireEndpoint::ipv4( LOOPBACK, back.port() );
		return TheNetwork->announcePlayerReturn( slot, back.publicKey(), back.port(), endpoint, prefill );
	}
	Bool isReturnable( Int slot )
	{
		::Engine::ScopedContext scope( m_context );
		return TheNetwork->isReturnable( slot );
	}
	Bool isConnected( Int slot )
	{
		::Engine::ScopedContext scope( m_context );
		return TheNetwork->isPlayerConnected( slot );
	}
	Bool returnState( Int slot, ::Engine::SessionReturnState &state )
	{
		::Engine::ScopedContext scope( m_context );
		return TheNetwork->getReturnState( slot, state );
	}
	UnsignedInt packetRouter( void )
	{
		::Engine::ScopedContext scope( m_context );
		return TheNetwork->isPacketRouter() ? static_cast<UnsignedInt>(m_slot) : MAX_SLOTS;
	}
	// what the returning player's lobby does when the host tells it
	void setReturnFrame( UnsignedInt frame, UnsignedInt prefill, UnsignedInt router )
	{
		::Engine::ScopedContext scope( m_context );
		TheNetwork->setReturnFrame( frame, prefill, router );
	}
	void applyReturnState( const ::Engine::SessionReturnState &state )
	{
		::Engine::ScopedContext scope( m_context );
		TheNetwork->applyReturnState( state );
	}
	Bool stillReturning( void )
	{
		::Engine::ScopedContext scope( m_context );
		return TheNetwork->isReturning();
	}
	UnsignedInt firstOwnFrame( void ) const { return m_firstOwnFrame; }
	const std::map<UnsignedInt, std::vector<Executed>> &messages() const { return m_messages; }

	Bool started() const { return m_started; }
	Bool keysComplete() const { return m_keysComplete; }
	Bool active() const { return m_started && !m_left; }
	/// A hanging leaver stops at its frame: no more updates, no more packets.
	Bool hung( const Scenario &s ) const { return s.hangs && m_slot == s.leaver && m_generation == 0 && m_frame >= s.leaveFrame; }
	/// The numbers of a returning player's commands start here, after those it sent before its drop.
	UnsignedInt firstNumber() const { return m_generation > 0 ? 1000 : 0; }
	Bool left() const { return m_left; }
	UnsignedInt frame() const { return m_frame; }

	/// One pass of the game loop.
	void step( const Scenario &s )
	{
		if ( !active() || hung( s ) )
			return;
		if ( m_slot == s.away && m_frame == s.awayFrame && !m_wasAway )
		{
			m_wasAway = TRUE;
			m_back = std::chrono::steady_clock::now() + std::chrono::milliseconds( AWAY_MS );
		}
		if ( m_wasAway && !m_returned )
		{
			if ( std::chrono::steady_clock::now() < m_back )
				return;
			::Engine::ScopedContext scope( m_context );
			m_returned = m_socket->reopen( LOOPBACK, static_cast<UnsignedShort>(m_port + 5) );
		}
		::Engine::ScopedContext scope( m_context );

		TheCommandList->appendMessage( newInstance( GameMessage )( GameMessage::MSG_FRAME_TICK ) );
		// a returning player's own commands only count once it plays in the lockstep again
		const Bool commanding = !m_returning || !TheNetwork->isReturning();
		if ( commanding && m_returning && m_firstOwnFrame == 0 )
			m_firstOwnFrame = m_frame;
		if ( commanding && m_frame != m_lastCommandFrame && m_frame > 0 && m_frame <= s.lastCommandFrame && (m_frame + m_slot) % 3 == 0 )
		{
			// every third frame a command with arguments of several kinds
			GameMessage *msg = newInstance( GameMessage )( COMMAND_TYPE );
			msg->appendIntegerArgument( m_slot );
			msg->appendIntegerArgument( static_cast<Int>(m_sent) );
			msg->appendRealArgument( 0.5f * static_cast<Real>(m_sent) );
			msg->appendBooleanArgument( (m_sent & 1) != 0 );
			TheCommandList->appendMessage( msg );
			m_lastCommandFrame = m_frame;
			++m_sent;
		}
		if ( m_frame == CHAT_FRAME && !m_chatSent )
		{
			TheNetwork->sendChat( UnicodeString( L"hello" ), 0xff );
			m_chatSent = TRUE;
		}
		if ( s.votedOut && m_slot != s.leaver && m_frame == s.leaveFrame + 10 * m_slot && !m_voted )
		{
			m_voteAccepted = TheNetwork->voteToRemovePlayer( s.leaver );
			m_voted = TRUE;
		}
		if ( s.players == 2 && m_slot == 0 && m_frame == 60 && !m_voted )
		{
			m_voteAccepted = TheNetwork->voteToRemovePlayer( 1 );	// refused: two players
			m_voted = TRUE;
		}
		if ( m_slot == s.leaver && m_frame == s.leaveFrame && !m_leaving && !s.hangs && !s.votedOut )
		{
			TheCommandList->appendMessage( newInstance( GameMessage )( GameMessage::MSG_CLEAR_GAME_DATA ) );
			m_leaving = TRUE;
		}

		TheNetwork->update();

		if ( (m_leaving || s.votedOut) && TheMessageStream->containsMessageOfType( GameMessage::MSG_CLEAR_GAME_DATA ) )
		{
			m_left = TRUE;	// the network let us go: back to the shell
			return;
		}
		if ( !TheNetwork->isFrameDataReady() )
			return;

		// the logic: the commands of this frame, in their order
		std::vector<std::string> &executed = m_executed[m_frame];
		std::vector<Executed> &messages = m_messages[m_frame];
		for ( GameMessage *msg = TheCommandList->getFirstMessage(); msg != nullptr; msg = msg->next() )
		{
			if ( msg->getType() != COMMAND_TYPE || msg->getArgumentCount() != 4 )
				continue;
			messages.push_back( { msg->getArgument( 0 )->integer, msg->getArgument( 1 )->integer, msg->getArgument( 2 )->real,
				msg->getArgument( 3 )->boolean } );
			char text[64];
			std::snprintf( text, sizeof( text ), "%d/%d %.1f %d", msg->getArgument( 0 )->integer, msg->getArgument( 1 )->integer,
				msg->getArgument( 2 )->real, msg->getArgument( 3 )->boolean ? 1 : 0 );
			executed.emplace_back( text );
		}
		TheCommandList->reset();
		if ( s.leaver >= 0 && s.leaver != m_slot && m_leaverGoneAt == 0 && !TheNetwork->isPlayerConnected( s.leaver ) )
			m_leaverGoneAt = m_frame;
		++TheGameLogic->m_frame;
		m_frame = TheGameLogic->m_frame;
	}

	UnsignedInt sent() const { return m_sent; }
	Bool voted() const { return m_voted; }
	Bool returned() const { return m_returned; }
	Bool voteAccepted() const { return m_voteAccepted; }
	const RecordedEvents &events() const { return m_events; }
	/// Frame in which this player executed the leaving of the scenario's leaver (0: not yet).
	UnsignedInt leaverGoneAt() const { return m_leaverGoneAt; }
	const std::map<UnsignedInt, std::vector<std::string>> &executed() const { return m_executed; }

private:
	Int m_slot;
	UnsignedShort m_port;
	Int m_generation = 0;
	::Engine::SealKeyPair m_keys;
	UnsignedInt m_identity = 0;
	::Engine::EngineContext m_context;
	LANGameInfo *m_game = nullptr;
	Bool m_started = FALSE;
	Bool m_keysComplete = FALSE;
	Bool m_leaving = FALSE;
	Bool m_left = FALSE;
	UnsignedInt m_frame = 0;
	UnsignedInt m_lastCommandFrame = 0;
	UnsignedInt m_sent = 0;
	UnsignedInt m_leaverGoneAt = 0;
	Bool m_chatSent = FALSE;
	Bool m_voted = FALSE;
	Transport *m_socket = nullptr;		///< owned by the network
	Bool m_wasAway = FALSE;
	Bool m_returned = FALSE;
	std::chrono::steady_clock::time_point m_back;
	Bool m_voteAccepted = FALSE;
	Bool m_returning = FALSE;
	UnsignedInt m_firstOwnFrame = 0;
	RecordedEvents m_events;
	std::map<UnsignedInt, std::vector<std::string>> m_executed;
	std::map<UnsignedInt, std::vector<Executed>> m_messages;
};

UnsignedInt HostFeed::completeFrames( void )
{
	return m_host.frame() > FEED_LAG ? m_host.frame() - FEED_LAG : 0;
}

void HostFeed::appendFrame( UnsignedInt frame, CommandList *list )
{
	const auto it = m_host.messages().find( frame );
	if ( it == m_host.messages().end() )
		return;
	for ( const Executed &e : it->second )
	{
		GameMessage *msg = newInstance( GameMessage )( COMMAND_TYPE );
		msg->appendIntegerArgument( e.slot );
		msg->appendIntegerArgument( e.number );
		msg->appendRealArgument( e.real );
		msg->appendBooleanArgument( e.flag );
		list->appendMessage( msg );
		++appended;
	}
}

void runScenario( const Scenario &s, Int index )
{
	std::printf( "%s\n", s.name );
	TheNetAddressBook().clear();
	TheWritableGlobalData->m_netEmuLatency = s.emulate ? 40 : 0;
	TheWritableGlobalData->m_netEmuJitter = s.emulate ? 20 : 0;
	TheWritableGlobalData->m_netEmuLoss = s.emulate ? 3 : 0;
	TheWritableGlobalData->m_netEmuDuplicate = s.emulate ? 2 : 0;
	TheWritableGlobalData->m_netEmuReorder = s.emulate ? 2 : 0;
	TheWritableGlobalData->m_netEmuSeed = s.emulate ? 17 : 0;
	// short enough for a test, long enough not to drop a player of a lossy network
	TheWritableGlobalData->m_networkDisconnectTime = 500;
	TheWritableGlobalData->m_networkPlayerTimeoutTime = 3000;
	TheWritableGlobalData->m_networkDisconnectScreenNotifyTime = 1000;

	std::vector<std::unique_ptr<Player>> players;
	for ( Int i = 0; i < s.players; ++i )
		players.push_back( std::make_unique<Player>( i, static_cast<UnsignedShort>(FIRST_PORT + 10 * index + i) ) );
	std::vector<Player *> all;
	for ( const std::unique_ptr<Player> &p : players )
		all.push_back( p.get() );
	Bool allStarted = TRUE;
	Bool allKeys = TRUE;
	for ( const std::unique_ptr<Player> &p : players )
	{
		p->start( all, s.observer, s.returnWindowSeconds );
		allStarted = allStarted && p->started();
		allKeys = allKeys && p->keysComplete();
	}
	check( allStarted, "every player has its game socket" );
	check( allKeys, "every player has the keys of every other" );

	// the hanging leaver back with a new key (the host is slot 0 and plays its lobby's part)
	std::unique_ptr<Player> back;
	HostFeed feed( *players[0] );
	UnsignedInt returnFrame = 0;
	UnsignedInt prefill = 0;
	Bool frameTold = FALSE;
	Bool stateTold = FALSE;

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( s.timeoutSeconds );
	const auto finished = [&] {
		for ( const std::unique_ptr<Player> &p : players )
		{
			if ( p->slot() == s.leaver ? !(p->left() || p->hung( s )) : p->frame() < s.lastFrame )
				return false;
		}
		return !s.comesBack || (back && back->frame() >= s.lastFrame);
	};
	// The longest pass of the loop: a machine under load can stop the whole process for longer than the
	// disconnect time, and then every player shows the waiting screen without anybody missing.
	auto lastPass = std::chrono::steady_clock::now();
	long long longestPassMs = 0;
	while ( !finished() && std::chrono::steady_clock::now() < deadline )
	{
		const auto passStart = std::chrono::steady_clock::now();
		longestPassMs = std::max<long long>( longestPassMs,
			std::chrono::duration_cast<std::chrono::milliseconds>( passStart - lastPass ).count() );
		lastPass = passStart;
		// Everybody plays on until all are through, as the game loop does: a player past the last frame still
		// resends lost packets and answers the waiting ones, else they drop it as timed out. A leaver runs empty
		// frames after its leaving until the network lets it go; a hanging one stops by itself (step).
		for ( const std::unique_ptr<Player> &p : players )
			p->step( s );
		if ( s.comesBack )
		{
			if ( !back && players[0]->isReturnable( s.leaver ) && !players[1]->isConnected( s.leaver ) )
			{
				back = std::make_unique<Player>( s.leaver, static_cast<UnsignedShort>(FIRST_PORT + 10 * index + 7), 1 );
				std::vector<Player *> again = all;
				again[static_cast<size_t>(s.leaver)] = back.get();
				back->startReturning( again, feed );
				std::printf( "    back at host frame %u\n", players[0]->frame() );
			}
			if ( back )
			{
				back->step( s );
				// caught up with the stream: the host announces it
				if ( returnFrame == 0 && back->frame() > 0 && back->frame() + 10 >= feed.completeFrames() )
				{
					returnFrame = players[0]->announceReturn( s.leaver, *back, prefill );
					if ( returnFrame != 0 ) std::printf( "    announced at host frame %u, back frame %u: return frame %u\n", players[0]->frame(), back->frame(), returnFrame );
				}
				// the host executed the announcement: it tells the return frame
				if ( returnFrame != 0 && !frameTold && players[0]->isConnected( s.leaver ) )
				{
					back->setReturnFrame( returnFrame, prefill, players[0]->packetRouter() );
					frameTold = TRUE;
				}
				::Engine::SessionReturnState state;
				if ( frameTold && !stateTold && players[0]->returnState( s.leaver, state ) )
				{
					back->applyReturnState( state );
					stateTold = TRUE;
				}
			}
		}
		std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
	}

	char what[160];
	for ( const std::unique_ptr<Player> &p : players )
	{
		if ( p->slot() == s.leaver && s.hangs )
			std::snprintf( what, sizeof( what ), "player %d stops answering (at frame %u)", p->slot(), p->frame() );
		else if ( p->slot() == s.leaver )
			std::snprintf( what, sizeof( what ), "player %d leaves the game (at frame %u)", p->slot(), p->frame() );
		else
			std::snprintf( what, sizeof( what ), "player %d reaches frame %u (at %u)", p->slot(), s.lastFrame, p->frame() );
		check( p->slot() == s.leaver ? (p->left() || p->hung( s )) : p->frame() >= s.lastFrame, what );
	}
	if ( s.comesBack )
	{
		std::snprintf( what, sizeof( what ), "player %d comes back (return frame %u) and reaches frame %u (at %u)", s.leaver,
			returnFrame, s.lastFrame, back ? back->frame() : 0 );
		check( back && returnFrame != 0 && back->frame() >= s.lastFrame, what );
	}

	// the same commands in the same frames, as far as both played
	const Player &reference = *players[s.leaver == 0 ? 1 : 0];
	if ( s.leaver >= 0 && s.leaver != s.observer )	// an observer leaves on arrival, not in a frame
	{
		Bool sameFrame = TRUE;
		for ( const std::unique_ptr<Player> &p : players )
			if ( p->slot() != s.leaver && p->leaverGoneAt() != reference.leaverGoneAt() )
				sameFrame = FALSE;
		std::snprintf( what, sizeof( what ), "the others execute the leaving in the same frame (%u)", reference.leaverGoneAt() );
		check( sameFrame && reference.leaverGoneAt() != 0, what );
	}
	Bool same = TRUE;
	std::vector<const Player *> compared;
	for ( const std::unique_ptr<Player> &p : players )
		compared.push_back( p.get() );
	if ( back )
		compared.push_back( back.get() );
	for ( const Player *p : compared )
	{
		UnsignedInt upTo = std::min( p->frame(), reference.frame() );
		if ( p->slot() == s.leaver && p != back.get() )	// what it runs after its leaving is its own
			upTo = std::min( upTo, reference.leaverGoneAt() );
		for ( UnsignedInt f = 0; f < upTo && same; ++f )
		{
			const auto a = p->executed().find( f );
			const auto b = reference.executed().find( f );
			const std::vector<std::string> none;
			const std::vector<std::string> &mine = a == p->executed().end() ? none : a->second;
			const std::vector<std::string> &theirs = b == reference.executed().end() ? none : b->second;
			if ( mine != theirs )
			{
				std::printf( "    frame %u differs between player %d and player %d:", f, p->slot(), reference.slot() );
				for ( const std::string &c : mine )
					std::printf( " [%s]", c.c_str() );
				std::printf( " versus" );
				for ( const std::string &c : theirs )
					std::printf( " [%s]", c.c_str() );
				std::printf( "\n" );
				same = FALSE;
			}
		}
	}
	check( same, "all players execute the same commands in the same frames" );

	if ( s.leaver >= 0 )
	{
		// nothing of the leaver after its leaving, though a player voted out sent commands for later frames
		char prefix[8];
		std::snprintf( prefix, sizeof( prefix ), "%d/", s.leaver );
		Bool none = TRUE;
		for ( const auto &frame : reference.executed() )
			for ( const std::string &c : frame.second )
				if ( frame.first > reference.leaverGoneAt() && c.rfind( prefix, 0 ) == 0
					&& !(s.comesBack && frame.first >= returnFrame && std::atoi( c.c_str() + 2 ) >= 1000) )
					none = FALSE;
		check( none, s.comesBack ? "no command of the leaver between its drop and its return frame"
			: "no command of the leaver is executed after its leaving" );
	}

	// each command of the players that stayed exactly once; those of the leaver up to its leaving
	std::map<std::string, Int> counts;
	for ( const auto &frame : reference.executed() )
		for ( const std::string &c : frame.second )
			++counts[c.substr( 0, c.find( ' ' ) )];
	Bool once = TRUE;
	for ( const Player *p : compared )
	{
		for ( UnsignedInt n = p->firstNumber(); n < p->sent(); ++n )
		{
			char key[32];
			std::snprintf( key, sizeof( key ), "%d/%u", p->slot(), n );
			const auto it = counts.find( key );
			const Int count = it == counts.end() ? 0 : it->second;
			const Bool expected = p->slot() == s.observer ? count == 0
				: (count == 1 || (p->slot() == s.leaver && p != back.get() && count == 0));
			if ( !expected )
			{
				std::printf( "    command %s executed %d times\n", key, count );
				once = FALSE;
			}
		}
	}
	std::snprintf( what, sizeof( what ), "every command is executed exactly once (%zu commands)", counts.size() );
	check( once && !counts.empty(), what );

	// chat of every player reaches every player; a leaving is reported once, by those who stay
	Bool chatOk = TRUE;
	Bool leaveOk = TRUE;
	for ( const std::unique_ptr<Player> &p : players )
	{
		if ( p->slot() == s.leaver )
			continue;
		const std::vector<Int> expected = s.leaver >= 0 ? std::vector<Int>{ s.leaver } : std::vector<Int>{};
		if ( p->events().chats != s.players )
		{
			std::printf( "    player %d read %d chat lines\n", p->slot(), p->events().chats );
			chatOk = FALSE;
		}
		if ( p->events().left != expected || p->events().changes != (s.leaver >= 0 ? (s.comesBack ? 2 : 1) : 0) )
		{
			std::printf( "    player %d: %d changes of the players;", p->slot(), p->events().changes );
			std::printf( "    player %d was told of leavings:", p->slot() );
			for ( Int slot : p->events().left )
				std::printf( " %d", slot );
			std::printf( "\n" );
			leaveOk = FALSE;
		}
	}
	check( chatOk, "every player's chat reaches every player" );

	// waiting for a player that stopped answering, and only then
	const Bool stalled = longestPassMs > static_cast<long long>( TheGlobalData->m_networkDisconnectTime );
	if ( stalled )
		std::printf( "    the test loop stalled for %lld ms (machine under load): waiting alone is no failure\n", longestPassMs );
	Bool waitOk = TRUE;
	for ( const std::unique_ptr<Player> &p : players )
	{
		if ( p->slot() == s.leaver )
			continue;
		const RecordedEvents &e = p->events();
		// Only the packet router drops the one that hangs; the others may get its word a moment before their own
		// count reaches 0.
		Bool expected = s.hangs ? (e.waits >= 1 && !e.waiting && e.waitedFor.count( s.leaver ) == 1 && e.lowestCountdown >= 0
				&& e.lowestCountdown < TheGlobalData->m_networkPlayerTimeoutTime / 3)
			: (s.emulate || stalled || e.waits == 0);
		if ( s.away >= 0 )	// waited for the one away, and it came back in time
			expected = p->slot() == s.away || (e.waits >= 1 && !e.waiting && e.waitedFor.count( s.away ) == 1 && e.lowestCountdown > 0);
		if ( s.observer >= 0 )	// the players never wait for an observer (it may wait for them)
			expected = p->slot() == s.observer || stalled || e.waits == 0;
		if ( !expected )
		{
			std::printf( "    player %d: waiting shown %d times, still waiting %d, lowest countdown %d ms\n", p->slot(), e.waits,
				e.waiting ? 1 : 0, e.lowestCountdown );
			waitOk = FALSE;
		}
	}
	check( waitOk, s.observer >= 0 ? "nobody waits for the observer"
		: (s.hangs ? "the others wait for it, count down to 0 and play on"
		: (s.away >= 0 ? "the others wait for the one away until it is back" : "nobody waits for a player")) );
	if ( s.away >= 0 )
		check( players[s.away]->returned(), "the one away is back on another port" );

	if ( s.votedOut )
	{
		Bool votes = TRUE;
		for ( const std::unique_ptr<Player> &p : players )
		{
			const RecordedEvents &e = p->events();
			const Bool ok = (p->slot() == s.leaver || (p->voted() && p->voteAccepted())) && e.voteReports == 2
				&& e.lastVote.target == s.leaver && e.lastVote.votes == 2 && e.lastVote.needed == 2
				&& e.removed == std::vector<Int>{ s.leaver };
			if ( !ok )
			{
				std::printf( "    player %d: %d votes reported, last %d of %d against %d, %zu removals\n", p->slot(), e.voteReports,
					e.lastVote.votes, e.lastVote.needed, e.lastVote.target, e.removed.size() );
				votes = FALSE;
			}
		}
		check( votes, "every player counts both votes and reports the removal once" );
	}
	if ( s.players == 2 )
		check( players[0]->voted() && !players[0]->voteAccepted(), "in a game of two there is no vote" );
	check( leaveOk, s.leaver >= 0 ? "the others are told once that the leaver left" : "nobody is reported as gone" );

	if ( s.hangs && s.observer < 0 && (s.comesBack || s.returnWindowSeconds < 60) )
	{
		// back: reported once, in the game again everywhere, its units never destroyed; else the window closes
		Bool reported = TRUE;
		for ( const std::unique_ptr<Player> &p : players )
		{
			if ( p->slot() == s.leaver )
				continue;
			const std::vector<Int> back = s.comesBack ? std::vector<Int>{ s.leaver } : std::vector<Int>{};
			const std::vector<Int> closed = s.comesBack ? std::vector<Int>{} : std::vector<Int>{ s.leaver };
			const Bool connected = p->isConnected( s.leaver );
			if ( p->events().returned != back || p->events().closed != closed || connected != s.comesBack )
			{
				std::printf( "    player %d: %zu returns and %zu closed windows reported, player %d %s\n", p->slot(),
					p->events().returned.size(), p->events().closed.size(), s.leaver, connected ? "in the game" : "out" );
				reported = FALSE;
			}
		}
		check( reported, s.comesBack ? "the others take it in again and never destroy its units"
			: "the return window runs out: closed once everywhere, the player stays out" );
	}
	if ( s.comesBack && back )
	{
		std::snprintf( what, sizeof( what ), "its own commands start at the return frame (first at %u, %u sent, %u frames from the feed)",
			back->firstOwnFrame(), back->sent() - back->firstNumber(), feed.appended );
		check( back->firstOwnFrame() >= returnFrame && back->sent() > back->firstNumber() && feed.appended > 0, what );
	}
	back.reset();

	players.clear();
	TheWritableGlobalData->m_netEmuLatency = 0;
	TheWritableGlobalData->m_netEmuJitter = 0;
	TheWritableGlobalData->m_netEmuLoss = 0;
	TheWritableGlobalData->m_netEmuDuplicate = 0;
	TheWritableGlobalData->m_netEmuReorder = 0;
	TheWritableGlobalData->m_netEmuSeed = 0;
}

void createGameSubsystems( const char *seedDir )
{
	Harness::create( ::Engine::context().gameText, CreateGameTextInterface() );
	::Engine::context().mapCache = new MapCache;
	Harness::create( ::Engine::context().playerList, new PlayerList );	// game messages ask for their player
	::Engine::context().windowManager = new NetHarness::WindowManager;	// not initialised; no windows
	::Engine::context().inGameUI = new NetHarness::InGameUI;	// not initialised
	Harness::create( ::Engine::context().recorder, new RecorderClass );	// logs disconnects
	Harness::loadBase( seedDir );
	TheWritableGlobalData->m_framesPerSecondLimit = LOGICFRAMES_PER_SECOND;	// as GameData.ini sets it
}

} // namespace

int main( int argc, char **argv )
{
	Harness::installCrashHandlers( "current.lockstep" );
	if ( argc < 2 )
	{
		std::fprintf( stderr, "usage: LockstepTest <seed dir> [ignored] [ignored]\n" );
		return 2;
	}
	Harness::createSubsystems();
	createGameSubsystems( argv[1] );

	const Scenario scenarios[] = {
		{ "two players", 2, 300, 200, -1, 0, FALSE, FALSE, FALSE, 60 },
		{ "three players, lossy network (latency 40+-20 ms, loss 3%, duplicates 2%, reordering 2%)", 3, 300, 200, -1, 0, FALSE, FALSE, TRUE, 90 },
		{ "three players, one leaves", 3, 300, 200, 2, 150, FALSE, FALSE, FALSE, 60 },
		{ "three players, one stops answering", 3, 300, 200, 2, 150, TRUE, FALSE, FALSE, 60 },
		{ "three players, two vote to remove the third", 3, 300, 200, 2, 100, FALSE, TRUE, FALSE, 60 },
		{ "three players, two vote to remove the packet router", 3, 300, 200, 0, 100, FALSE, TRUE, FALSE, 60 },
		{ "three players, one is away for 1.5 s and comes back on another port", 3, 300, 200, -1, 0, FALSE, FALSE, FALSE, 60, 1, 150 },
		{ "two players and an observer, the observer is away for 1.5 s", 3, 300, 200, -1, 0, FALSE, FALSE, FALSE, 60, 2, 100, 2 },
		{ "two players and an observer that stops answering", 3, 300, 200, 2, 90, TRUE, FALSE, FALSE, 60, -1, 0, 2 },
		{ "two players and an observer that leaves", 3, 300, 200, 2, 150, FALSE, FALSE, FALSE, 60, -1, 0, 2 },
		{ "the host observes (slot 0, no packet router) and is away for 1.5 s", 3, 300, 200, -1, 0, FALSE, FALSE, FALSE, 60, 0, 100, 0 },
		{ "three players, one stops answering, is dropped and comes back with a new key", 3, 600, 500, 2, 100, TRUE, FALSE, FALSE, 90, -1, 0, -1, TRUE },
		{ "three players, one stops answering and its return window runs out", 3, 300, 200, 2, 100, TRUE, FALSE, FALSE, 60, -1, 0, -1, FALSE, 2 },
	};
	// LOCKSTEP_SCENARIO=<n> runs only scenario n (from 1), with HARNESS_VERBOSE=1 for its debug log
	const char *only = std::getenv( "LOCKSTEP_SCENARIO" );
	Int index = 0;
	for ( const Scenario &s : scenarios )
	{
		if ( only == nullptr || std::atoi( only ) == index + 1 )
			runScenario( s, index );
		++index;
	}

	std::printf( "LockstepTest: %d failures\n", s_failures );
	return s_failures == 0 ? 0 : 1;
}
