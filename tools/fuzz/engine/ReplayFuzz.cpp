/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Replay harness: reads mutated replays the way the replay menu and the playback read them, and
// feeds mutated game option strings to ParseAsciiStringToGameInfo, which also reads the options a
// LAN host sends.
//   - RecorderClass::readReplayHeader (the replay list),
//   - RecorderClass::analyzeReplay and update() until the end (header, game info, every command
//     with its arguments; the commands are read, not executed),
//   - ParseAsciiStringToGameInfo on the options string alone.
//
//   ReplayFuzz <seed dir> <iterations> <rng seed>   fuzz <seed dir>/replay/*.rep
//   ReplayFuzz --run <seed dir> <file>              read one replay with the debug log

#include "PreRTS.h"

#include "Common/GameMemory.h"
#include "Common/GameState.h"
#include "Common/MessageStream.h"
#include "Common/PlayerList.h"
#include "Common/Recorder.h"
#include "Common/version.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameInfo.h"

#include "Harness.h"

#include <cstdio>
#include <cstring>

using Harness::Rng;
using Harness::readFile;
using Harness::writeFile;

namespace
{

void createReplaySubsystems()
{
	Harness::create( ::Engine::context().gameText, CreateGameTextInterface() );
	::Engine::context().mapCache = new MapCache;
	::Engine::context().version = new Version;
	::Engine::context().gameState = new GameState;
	::Engine::context().commandList = new CommandList;
	TheCommandList->init();
	// only for getFrame() in the recorder's logging; not initialised
	::Engine::context().gameLogic = new GameLogic;
	Harness::create( ::Engine::context().playerList, new PlayerList );	// Money::deposit asks for the local player
	Harness::create( ::Engine::context().recorder, new RecorderClass );
}

// Where the options string starts: "GENREP", two time_t, frame duration, two Bools, the
// disconnect flags, then three unicode strings and fixed fields; found by its first key.
std::size_t findOptions( const std::string &d )
{
	const std::size_t at = d.find( "US=" );
	return at != std::string::npos ? at : d.find( "M=" );
}

std::string optionsOf( const std::string &d )
{
	const std::size_t at = findOptions( d );
	if ( at == std::string::npos )
		return std::string();
	const std::size_t end = d.find( '\0', at );
	return d.substr( at, end == std::string::npos ? std::string::npos : end - at );
}

void readReplay( const std::string &data )
{
	writeFile( Harness::workDir / "current.rep", data );
	const AsciiString path( (Harness::workDir / "current.rep").string().c_str() );

	// the replay list
	RecorderClass::ReplayHeader header;
	header.forPlayback = FALSE;
	header.filename = path;
	TheRecorder->readReplayHeader( header );

	// the whole file, command by command
	if ( TheRecorder->analyzeReplay( path ) )
	{
		for ( int frames = 0; TheRecorder->isAnalysisInProgress() && frames < 200000; ++frames )
			TheRecorder->update();
	}
	TheRecorder->stopPlayback();
	TheCommandList->reset();
	TheRecorder->reset();
}

void readOptions( const std::string &options )
{
	ReplayGameInfo game;
	game.reset();
	game.enterGame();
	ParseAsciiStringToGameInfo( &game, AsciiString( options.c_str() ) );
	game.endGame();
	game.reset();
}

// --- mutation ------------------------------------------------------------------------------------

const std::uint32_t s_ints[] = {
	0, 1, 2, 0x7F, 0x80, 0xFF, 0x100, 0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu, 1000, 1001,
};

const char *const s_tokens[] = {
	"", "-1", "0", "1", "7", "8", "9", "99", "2147483647", "-2147483648", "4294967296", "X", "H", "C",
	"CE", "CM", "CH", "O", "TT", "FF", "HObserver", "ffffffff", "Y", "N", "_", "%s%s%s%n", "\\", "/",
	"03maps/", "maps/../../x",
};

class ReplayMutator
{
public:
	ReplayMutator( std::vector<std::string> corpus, std::uint64_t seed ) : m_corpus( std::move( corpus ) ), m_rng{ seed }
	{
		for ( const std::string &d : m_corpus )
			m_options.push_back( optionsOf( d ) );
	}

	/// Inputs that reached new code; mutated like the seeds from now on.
	void addReplay( const std::string &data ) { m_corpus.push_back( data ); }
	void addOptions( const std::string &options ) { m_options.push_back( options ); }
	std::size_t size() const { return m_corpus.size() + m_options.size(); }

	std::string nextReplay()
	{
		std::string d = m_corpus[m_rng.below( m_corpus.size() )];
		const int count = 1 + static_cast<int>(m_rng.below( 6 ));
		for ( int i = 0; i < count; ++i )
		{
			switch ( m_rng.below( 5 ) )
			{
				case 0:	// the options string inside the file
				{
					const std::size_t at = findOptions( d );
					if ( at == std::string::npos )
						break;
					const std::size_t end = d.find( '\0', at );
					if ( end == std::string::npos )
						break;
					d.replace( at, end - at, mutateOptions( d.substr( at, end - at ) ) );
					break;
				}
				case 1:	// a 32-bit value somewhere (frame numbers, types, player indices, arguments)
					if ( d.size() >= 4 )
					{
						const std::uint32_t v = Harness::pickInt( m_rng, s_ints );
						std::memcpy( &d[m_rng.below( d.size() - 3 )], &v, 4 );
					}
					break;
				case 2:	// a byte (argument type and count bytes)
					if ( !d.empty() )
						d[m_rng.below( d.size() )] = static_cast<char>(m_rng.next());
					break;
				case 3:	// cut
					if ( !d.empty() )
						d.resize( m_rng.below( d.size() ) );
					break;
				default:	// copy a stretch to another place
				{
					if ( d.size() < 16 )
						break;
					const std::size_t from = m_rng.below( d.size() - 8 );
					const std::size_t len = 1 + m_rng.below( std::min<std::size_t>( 64, d.size() - from ) );
					d.insert( m_rng.below( d.size() ), d.substr( from, len ) );
					break;
				}
			}
		}
		return d;
	}

	std::string nextOptions()
	{
		std::string o = m_options[m_rng.below( m_options.size() )];
		const int count = 1 + static_cast<int>(m_rng.below( 4 ));
		for ( int i = 0; i < count; ++i )
			o = mutateOptions( o );
		return o;
	}

private:
	std::string mutateOptions( std::string o )
	{
		static const char seps[] = { ';', ':', ',', '=' };
		if ( o.empty() )
			return s_tokens[m_rng.below( std::size( s_tokens ) )];
		if ( Harness::dictionarySize() > 0 && m_rng.below( 8 ) == 0 )	// a constant the parser compares with
		{
			const std::size_t at = m_rng.below( o.size() + 1 );
			o.insert( at, std::to_string( static_cast<std::int32_t>(Harness::dictionaryValue( m_rng )) ) );
			return o;
		}
		switch ( m_rng.below( 5 ) )
		{
			case 0:	// replace the text between two separators
			{
				const std::size_t at = m_rng.below( o.size() );
				std::size_t from = o.find_last_of( ";:,=", at );
				from = from == std::string::npos ? 0 : from + 1;
				std::size_t to = o.find_first_of( ";:,=", at );
				if ( to == std::string::npos )
					to = o.size();
				if ( from <= to )
					o.replace( from, to - from, s_tokens[m_rng.below( std::size( s_tokens ) )] );
				break;
			}
			case 1:	// a separator in or out
				o.insert( m_rng.below( o.size() + 1 ), 1, seps[m_rng.below( std::size( seps ) )] );
				break;
			case 2:
				o.erase( m_rng.below( o.size() ), 1 + m_rng.below( 4 ) );
				break;
			case 3:	// a long run
				o.insert( m_rng.below( o.size() + 1 ), 200 + m_rng.below( 3000 ), "A,:"[m_rng.below( 3 )] );
				break;
			default:	// a byte
				o[m_rng.below( o.size() )] = static_cast<char>(m_rng.next() | 1);
				break;
		}
		// the string ends at the first NUL in the file
		const std::size_t nul = o.find( '\0' );
		if ( nul != std::string::npos )
			o.resize( nul );
		return o;
	}

	std::vector<std::string> m_corpus;
	std::vector<std::string> m_options;
	Rng m_rng;
};

}	// namespace

int main( int argc, char **argv )
{
	Harness::installCrashHandlers( "current.rep" );

	if ( argc == 4 && std::strcmp( argv[1], "--run" ) == 0 )
	{
		Harness::createSubsystems();
		createReplaySubsystems();
		Harness::loadBase( argv[2] );
		Harness::verbose = true;
		readReplay( readFile( argv[3] ) );
		std::printf( "%s: read, %lld assertions\n", argv[3], Harness::assertions );
		return 0;
	}
	if ( argc != 4 )
	{
		std::fprintf( stderr, "usage: ReplayFuzz <seed dir> <iterations> <rng seed> | --run <seed dir> <file>\n" );
		return 2;
	}
	const std::filesystem::path seedDir = argv[1];
	const long long iterations = std::atoll( argv[2] );
	const std::uint64_t rngSeed = std::strtoull( argv[3], nullptr, 10 );

	Harness::createSubsystems();
	createReplaySubsystems();
	Harness::loadBase( seedDir );

	std::vector<std::string> corpus;
	for ( const auto &entry : std::filesystem::directory_iterator( seedDir / "replay" ) )
	{
		const std::string data = readFile( entry.path() );
		Harness::resetCoverage();
		readReplay( data );
		Harness::newCoverage();
		corpus.push_back( data );
	}
	if ( corpus.empty() )
	{
		std::fprintf( stderr, "ReplayFuzz: no seeds in %s\n", (seedDir / "replay").string().c_str() );
		return 2;
	}

	Harness::resetDictionary();
	Harness::startWatchdog();
	ReplayMutator mutator( corpus, rngSeed );
	long long replays = 0, options = 0, added = 0;
	long long done = 0;
	for ( long long i = 0; i < iterations && !Harness::timeUp(); ++i, ++done )
	{
		Harness::iteration = i;
		Harness::resetCoverage();
		if ( i % 2 )
		{
			const std::string o = mutator.nextOptions();
			writeFile( Harness::workDir / "current.rep", o );	// kept on a crash
			readOptions( o );
			++options;
			if ( Harness::newCoverage() && mutator.size() < 10000 )
			{
				mutator.addOptions( o );
				++added;
			}
		}
		else
		{
			const std::string d = mutator.nextReplay();
			readReplay( d );
			++replays;
			if ( Harness::newCoverage() && mutator.size() < 10000 )
			{
				mutator.addReplay( d );
				++added;
			}
		}
		++Harness::progress;
	}
	TheMemoryPoolFactory->debugMemoryVerify();

	std::printf( "%lld inputs: %lld replays, %lld option strings; %lld assertions; %lld added to the corpus, %zu edges, %zu dictionary values\n",
		done, replays, options, Harness::assertions, added, Harness::coveredEdges(), Harness::dictionarySize() );
	return 0;
}
