/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Save game harness: reads mutated save games the way the game reads files from the save folder,
// which may come from someone else:
//   - GameState::getSaveGameInfoFromFile, which the save/load menu runs for every .sav in the
//     folder (header and the game state block),
//   - the block loop of GameState::xferSaveData with the blocks that load without a running game
//     (game state, campaign with the challenge game info); the other blocks are skipped as the
//     loader skips unknown ones.
// The seeds are written by the harness with the engine's own save code (XferSave).
//
//   SaveFuzz <seed dir> <iterations> <rng seed>   fuzz the generated seeds
//   SaveFuzz --run <seed dir> <file>              read one file with the debug log

#include "PreRTS.h"

#include "Common/GameMemory.h"
#include "Common/GameState.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "Common/version.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/GameInfo.h"

#include "Harness.h"

#include <cstdio>
#include <cstring>

using Harness::Rng;
using Harness::readFile;
using Harness::writeFile;

namespace
{

std::filesystem::path s_saveFile;

void createSaveSubsystems()
{
	Harness::create( ::Engine::context().gameText, CreateGameTextInterface() );
	::Engine::context().mapCache = new MapCache;
	::Engine::context().version = new Version;
	::Engine::context().campaignManager = new CampaignManager;	// no Campaign.ini: no campaigns known
	::Engine::context().challengeGenerals = createChallengeGenerals();	// created with the game client
	TheChallengeGenerals->init();
	::Engine::context().gameState = new GameState;
	TheGameState->init();	// the block names; the snapshots of a running game stay null

	const std::filesystem::path dir( TheGameState->getSaveDirectory().str() );
	std::filesystem::create_directories( dir );
	s_saveFile = dir / "fuzz.sav";
}

// --- seeds ---------------------------------------------------------------------------------------

void writeHeader( XferSave &xfer )
{
	AsciiString marker( "ZHSAVE" );
	xfer.xferAsciiString( &marker );
	Int version = 1;
	xfer.xferInt( &version );
	AsciiString build( "harness" );
	xfer.xferAsciiString( &build );
}

void writeCampaignBlock( XferSave &xfer, Bool challenge )
{
	AsciiString token( "CHUNK_Campaign" );
	xfer.xferAsciiString( &token );
	xfer.beginBlock();
	XferVersion version = 5;
	xfer.xferVersion( &version, 5 );
	AsciiString campaign( "usa" ), mission( "mission01" );
	xfer.xferAsciiString( &campaign );
	xfer.xferAsciiString( &mission );
	Int rankPoints = 3;
	xfer.xferInt( &rankPoints );
	GameDifficulty difficulty = DIFFICULTY_NORMAL;
	xfer.xferUser( &difficulty, sizeof( difficulty ) );
	xfer.xferBool( &challenge );
	if ( challenge )
	{
		SkirmishGameInfo game;
		game.init();
		game.clearSlotList();
		game.reset();
		game.getSlot( 0 )->setState( SLOT_PLAYER, UnicodeString( L"Player" ) );
		game.getSlot( 1 )->setState( SLOT_MED_AI );
		AsciiString map = TheMapCache->getMapDir();
		map.concat( "\\alpine\\alpine.map" );
		game.setMap( map );
		xfer.xferSnapshot( &game );
	}
	Int playerTemplate = 0;
	xfer.xferInt( &playerTemplate );
	xfer.endBlock();
}

std::string makeSeed( Bool challenge )
{
	const std::filesystem::path path = Harness::workDir / "seed.sav";
	{
		XferSave xfer;
		xfer.open( AsciiString( path.string().c_str() ) );
		writeHeader( xfer );

		AsciiString token( "CHUNK_GameState" );
		xfer.xferAsciiString( &token );
		xfer.beginBlock();
		TheGameState->getSaveGameInfo()->description = L"Harness";
		xfer.xferSnapshot( TheGameState );
		xfer.endBlock();

		writeCampaignBlock( xfer, challenge );

		// a block of a running game: skipped by both readers here
		token = "CHUNK_GameLogic";
		xfer.xferAsciiString( &token );
		xfer.beginBlock();
		Int filler[4] = { 1, 2, 3, 4 };
		xfer.xferUser( filler, sizeof( filler ) );
		xfer.endBlock();

		token = "SG_EOF";
		xfer.xferAsciiString( &token );
		xfer.close();
	}
	return readFile( path );
}

// --- reading -------------------------------------------------------------------------------------

void readSave( const std::string &data )
{
	writeFile( Harness::workDir / "current.sav", data );	// kept on a crash
	writeFile( s_saveFile, data );
	const AsciiString path( s_saveFile.string().c_str() );

	// the save/load menu (addGameToAvailableList catches everything)
	try
	{
		SaveGameInfo info;
		TheGameState->getSaveGameInfoFromFile( path, &info );
	}
	catch ( ... )
	{
	}

	// the block loop of the loader, with the blocks that load without a running game
	try
	{
		XferLoad xfer;
		xfer.open( path );
		xfer.setOptions( XO_NO_POST_PROCESSING );
		try
		{
			AsciiString marker, build, token;
			xfer.xferAsciiString( &marker );
			Int version = 0;
			xfer.xferInt( &version );
			xfer.xferAsciiString( &build );
			for ( ;; )	// unbounded like the game's loader, so a loop in the file shows as a hang
			{
				xfer.xferAsciiString( &token );
				if ( token.compareNoCase( "SG_EOF" ) == 0 )
					break;
				const Int size = xfer.beginBlock();
				if ( token.compareNoCase( "CHUNK_GameState" ) == 0 )
				{
					GameState state;
					xfer.xferSnapshot( &state );
				}
				else if ( token.compareNoCase( "CHUNK_Campaign" ) == 0 )
					xfer.xferSnapshot( TheCampaignManager );
				else
					xfer.skip( size );
				xfer.endBlock();
			}
		}
		catch ( ... )
		{
			xfer.close();
			throw;
		}
		xfer.close();
	}
	catch ( ... )
	{
	}
	TheCampaignManager->setCampaignAndMission( AsciiString::TheEmptyString, AsciiString::TheEmptyString );
	if ( TheChallengeGameInfo )
	{
		delete TheChallengeGameInfo;
		::Engine::context().challengeGameInfo = nullptr;
	}
}

// --- mutation ------------------------------------------------------------------------------------

const std::uint32_t s_ints[] = {
	0, 1, 2, 5, 6, 7, 8, 0x7F, 0x80, 0xFF, 0x100, 0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu,
	0xFFFFFFF0u, 0xFFFFFF00u,
};

const char *const s_strings[] = {
	"", "ZHSAVE", "SG_EOF", "CHUNK_GameState", "CHUNK_Campaign", "CHUNK_GameStateMap", "CHUNK_Players",
	"save\\x.map", "maps\\x\\x.map", "userdata\\maps\\x\\x.map", "save\\..\\..\\x.map", "save\\../../x.map",
	"%s%s%s%n", "usa", "mission01",
};

class SaveMutator
{
public:
	SaveMutator( std::vector<std::string> corpus, std::uint64_t seed ) : m_corpus( std::move( corpus ) ), m_rng{ seed } {}

	void add( const std::string &data ) { m_corpus.push_back( data ); }
	std::size_t size() const { return m_corpus.size(); }

	std::string next()
	{
		std::string d = m_corpus[m_rng.below( m_corpus.size() )];
		const int count = 1 + static_cast<int>(m_rng.below( 5 ));
		for ( int i = 0; i < count; ++i )
		{
			switch ( m_rng.below( 6 ) )
			{
				case 0:	// a 32-bit value (block sizes, counts, slot values)
					if ( d.size() >= 4 )
					{
						const std::uint32_t v = Harness::pickInt( m_rng, s_ints );
						std::memcpy( &d[m_rng.below( d.size() - 3 )], &v, 4 );
					}
					break;
				case 1:	// a byte (versions, string lengths, Bools)
					if ( !d.empty() )
						d[m_rng.below( d.size() )] = static_cast<char>(m_rng.next());
					break;
				case 2:	// a length-prefixed string
				{
					const std::string s = s_strings[m_rng.below( std::size( s_strings ) )];
					std::string item( 1, static_cast<char>(s.size()) );
					item += s;
					d.insert( m_rng.below( d.size() + 1 ), item );
					break;
				}
				case 3:	// cut
					if ( !d.empty() )
						d.resize( m_rng.below( d.size() ) );
					break;
				case 4:	// remove a stretch
					if ( !d.empty() )
						d.erase( m_rng.below( d.size() ), 1 + m_rng.below( 16 ) );
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

private:
	std::vector<std::string> m_corpus;
	Rng m_rng;
};

}	// namespace

int main( int argc, char **argv )
{
	Harness::installCrashHandlers( "current.sav" );

	if ( argc == 4 && std::strcmp( argv[1], "--run" ) == 0 )
	{
		Harness::createSubsystems();
		createSaveSubsystems();
		Harness::loadBase( argv[2] );
		Harness::verbose = true;
		readSave( readFile( argv[3] ) );
		std::printf( "%s: read, %lld assertions\n", argv[3], Harness::assertions );
		return 0;
	}
	if ( argc != 4 )
	{
		std::fprintf( stderr, "usage: SaveFuzz <seed dir> <iterations> <rng seed> | --run <seed dir> <file>\n" );
		return 2;
	}
	const long long iterations = std::atoll( argv[2] );
	const std::uint64_t rngSeed = std::strtoull( argv[3], nullptr, 10 );

	Harness::createSubsystems();
	createSaveSubsystems();
	Harness::loadBase( argv[1] );

	std::vector<std::string> corpus = { makeSeed( FALSE ), makeSeed( TRUE ) };
	long long badSeeds = 0;
	for ( const std::string &seed : corpus )
	{
		const long long before = Harness::assertions;
		Harness::resetCoverage();
		readSave( seed );
		Harness::newCoverage();
		if ( Harness::assertions != before )
			++badSeeds;
		writeFile( Harness::workDir / ("seed-" + std::to_string( &seed - corpus.data() ) + ".sav"), seed );
	}

	Harness::resetDictionary();
	Harness::startWatchdog();
	SaveMutator mutator( corpus, rngSeed );
	long long added = 0;
	long long done = 0;
	for ( long long i = 0; i < iterations && !Harness::timeUp(); ++i, ++done )
	{
		Harness::iteration = i;
		Harness::resetCoverage();
		const std::string d = mutator.next();
		readSave( d );
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
