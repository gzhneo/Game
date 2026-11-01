/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Map harness: loads mutated .map files through the readers of the game logic, the way a map from
// another player reaches them:
//   - MapCache::updateCache, which lists a new or transferred map (size, WorldInfo, objects,
//     waypoints) and writes MapCache.ini, read back in a second pass,
//   - the logical map data the game reads at map start, as W3DTerrainLogic::loadMap reads it
//     (WorldHeightMap with logicalDataOnly: size, WorldInfo, objects, PolygonTriggers, SidesList with
//     players, teams, build lists and scripts), then SidesList::prepareForMP_or_Skirmish and
//     validateSides,
//   - TerrainLogic::loadMap with query set (waypoint links),
//   - the terrain as W3DTerrainVisual::load reads it (WorldHeightMap: height map, blend tiles,
//     cliff info, lighting); textures are not created.
//
//   MapFuzz <seed dir> <iterations> <rng seed>   fuzz <seed dir>/map/*.map
//   MapFuzz --run <seed dir> <file>              load one file with the debug log (reproduce)

#include "PreRTS.h"

#include "Common/DataChunk.h"
#include "Common/FileSystem.h"
#include "Common/GameMemory.h"
#include "Common/MapObject.h"
#include "Common/MapReaderWriterInfo.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/TerrainLogic.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"

#include "Harness.h"

#include <cstdio>
#include <cstring>

using Harness::Rng;
using Harness::readFile;
using Harness::writeFile;

namespace
{

void createMapSubsystems()
{
	Harness::create( ::Engine::context().gameText, CreateGameTextInterface() );
	Harness::create( ::Engine::context().scriptEngine, new ScriptEngine );
	Harness::create( ::Engine::context().sidesList, new SidesList );
	Harness::create( ::Engine::context().terrainLogic, new TerrainLogic );
	::Engine::context().mapCache = new MapCache;
}

void freeMapObjects()
{
	if ( MapObject::TheMapObjectListPtr )
		MapObject::TheMapObjectListPtr->deleteInstance();
	MapObject::TheMapObjectListPtr = nullptr;
	MapObject::getWorldDict()->clear();
}

enum class LoadResult { Loaded, Rejected };

// WorldHeightMap as the game creates it: logicalDataOnly for the game logic (W3DTerrainLogic), the
// whole terrain for the display (W3DTerrainVisual). A damaged file sets isLoadFailed.
void readHeightMap( const char *path, Bool logicalDataOnly )
{
	CachedFileInputStream stream;
	if ( !stream.open( AsciiString( path ) ) )
		throw ERROR_CORRUPT_FILE_FORMAT;
	WorldHeightMap *map = NEW WorldHeightMap( &stream, logicalDataOnly );
	const Bool failed = map->isLoadFailed();
	if ( !failed )
	{
		// the height range, as W3DTerrainLogic::loadMap computes it
		Int lo = map->getMaxHeightValue(), hi = 0;
		for ( Int y = 0; y < map->getYExtent(); ++y )
			for ( Int x = 0; x < map->getXExtent(); ++x )
			{
				lo = std::min<Int>( lo, map->getHeight( x, y ) );
				hi = std::max<Int>( hi, map->getHeight( x, y ) );
			}
	}
	REF_PTR_RELEASE( map );
	if ( failed )
		throw ERROR_CORRUPT_FILE_FORMAT;
}

// Transferred and downloaded maps lie in <user data>\Maps\<name>\<name>.map; the map list finds
// them there (MapCache::loadUserMaps).
std::filesystem::path s_mapFile;

void setUpUserMapDir()
{
	const std::filesystem::path dir = std::filesystem::path( TheGlobalData->getPath_UserData().str() ) / "Maps" / "fuzzmap";
	std::filesystem::create_directories( dir );
	s_mapFile = dir / "fuzzmap.map";
}

LoadResult loadMap( const std::string &data )
{
	LoadResult result = LoadResult::Loaded;
	writeFile( Harness::workDir / "current.map", data );	// kept on a crash
	writeFile( s_mapFile, data );
	const std::string pathString = s_mapFile.string();
	const char *path = pathString.c_str();

	// The map list, as the game refreshes it: reads the map (a damaged one is left out) and writes
	// MapCache.ini; the second pass reads that file back, as the next start of the game would.
	std::error_code ec;
	std::filesystem::remove( s_mapFile.parent_path().parent_path() / "MapCache.ini", ec );
	TheMapCache->clear();
	TheMapCache->updateCache();
	if ( TheMapCache->empty() )
		result = LoadResult::Rejected;
	TheMapCache->clear();
	TheMapCache->updateCache();
	TheMapCache->clear();

	try
	{
		readHeightMap( path, TRUE );
		TheSidesList->prepareForMP_or_Skirmish();
		TheSidesList->validateSides();
		TheTerrainLogic->loadMap( AsciiString( path ), TRUE );
		readHeightMap( path, FALSE );
	}
	catch ( ... )
	{
		// the game does not start the map; for the harness it is a rejected file
		result = LoadResult::Rejected;
	}

	PolygonTrigger::deleteTriggers();
	freeMapObjects();
	Harness::resetAll();
	return result;
}

// --- chunk-aware mutation ------------------------------------------------------------------------

struct ChunkPos
{
	std::size_t header;		///< offset of the 10-byte header (id, version, size)
	std::size_t size;		///< data size from the header
	int parent;				///< index of the enclosing chunk, -1 at top level
	bool leaf;				///< data does not parse as child chunks
};

std::uint32_t get32( const std::string &d, std::size_t at )
{
	std::uint32_t v;
	std::memcpy( &v, d.data() + at, 4 );
	return v;
}

void put32( std::string &d, std::size_t at, std::uint32_t v )
{
	std::memcpy( &d[at], &v, 4 );
}

// Table of contents: "CkMp", count, then per name a length byte, the name and a 4-byte id.
bool readToc( const std::string &d, std::size_t &end, std::uint32_t &maxId )
{
	if ( d.size() < 8 || d.compare( 0, 4, "CkMp" ) != 0 )
		return false;
	const std::uint32_t count = get32( d, 4 );
	std::size_t pos = 8;
	maxId = 0;
	for ( std::uint32_t i = 0; i < count; ++i )
	{
		if ( pos + 1 > d.size() )
			return false;
		const std::size_t len = static_cast<unsigned char>(d[pos]);
		if ( pos + 1 + len + 4 > d.size() )
			return false;
		maxId = std::max( maxId, get32( d, pos + 1 + len ) );
		pos += 1 + len + 4;
	}
	end = pos;
	return true;
}

// Chunks between from and to; true if they cover the range exactly.
bool scanChunks( const std::string &d, std::size_t from, std::size_t to, std::uint32_t maxId, int parent, std::vector<ChunkPos> &out, int depth )
{
	const std::size_t first = out.size();
	std::size_t pos = from;
	while ( pos + 10 <= to )
	{
		const std::uint32_t id = get32( d, pos );
		const std::uint32_t size = get32( d, pos + 6 );
		if ( id == 0 || id > maxId || size > to - pos - 10 )
			break;
		const int index = static_cast<int>(out.size());
		out.push_back( { pos, size, parent, true } );
		if ( depth < 16 )
		{
			std::vector<ChunkPos> children;
			const std::size_t before = out.size();
			if ( size >= 10 && scanChunks( d, pos + 10, pos + 10 + size, maxId, index, out, depth + 1 ) )
				out[index].leaf = false;
			else
				out.resize( before );
		}
		pos += 10 + size;
	}
	if ( pos != to )
	{
		out.resize( first );
		return false;
	}
	return true;
}

const std::uint32_t s_ints[] = {
	0, 1, 2, 3, 0x7F, 0x80, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000u,
	0xFFFFFFFFu, 0xFFFFFFFEu, 1000, 10000, 1000000,
	0x7F800000u /* +inf */, 0xFF800000u /* -inf */, 0x7FC00000u /* NaN */, 0x4E6E6B28u /* 1e9f */,
};

class MapMutator
{
public:
	MapMutator( std::vector<std::string> corpus, std::uint64_t seed ) : m_corpus( std::move( corpus ) ), m_rng{ seed } {}

	/// An input that reached new code; mutated like the seeds from now on.
	void add( const std::string &data ) { m_corpus.push_back( data ); }
	std::size_t size() const { return m_corpus.size(); }

	std::string next()
	{
		std::string d = m_corpus[m_rng.below( m_corpus.size() )];
		const int count = 1 + static_cast<int>(m_rng.below( 4 ));
		for ( int i = 0; i < count; ++i )
			mutate( d );
		return d;
	}

private:
	void adjustParents( std::string &d, const std::vector<ChunkPos> &chunks, int parent, long long delta )
	{
		for ( int p = parent; p >= 0; p = chunks[p].parent )
			put32( d, chunks[p].header + 6, static_cast<std::uint32_t>(static_cast<long long>(get32( d, chunks[p].header + 6 )) + delta) );
	}

	void mutate( std::string &d )
	{
		std::size_t tocEnd = 0;
		std::uint32_t maxId = 0;
		std::vector<ChunkPos> chunks;
		if ( readToc( d, tocEnd, maxId ) )
			scanChunks( d, tocEnd, d.size(), maxId, -1, chunks, 0 );
		if ( chunks.empty() || m_rng.below( 10 ) == 0 )
		{
			rawBytes( d );
			return;
		}
		const ChunkPos &c = chunks[m_rng.below( chunks.size() )];
		const std::size_t data = c.header + 10;
		switch ( m_rng.below( 8 ) )
		{
			case 0:
			case 1:	// a value inside a leaf chunk
			{
				if ( !c.leaf || c.size == 0 )
					break;
				if ( c.size >= 4 && m_rng.below( 2 ) )
					put32( d, data + m_rng.below( c.size - 3 ), Harness::pickInt( m_rng, s_ints ) );
				else
					d[data + m_rng.below( c.size )] = static_cast<char>(m_rng.next());
				break;
			}
			case 2:	// a 16-bit length inside a leaf chunk (AsciiString, Dict)
			{
				if ( !c.leaf || c.size < 2 )
					break;
				const std::uint16_t v = static_cast<std::uint16_t>(Harness::pickInt( m_rng, s_ints ));
				std::memcpy( &d[data + m_rng.below( c.size - 1 )], &v, 2 );
				break;
			}
			case 3:	// chunk version
			{
				static const std::uint16_t versions[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 99, 0xFFFF };
				std::memcpy( &d[c.header + 4], &versions[m_rng.below( std::size( versions ) )], 2 );
				break;
			}
			case 4:	// chunk size, without fixing the parents
				put32( d, c.header + 6, m_rng.below( 2 ) ? Harness::pickInt( m_rng, s_ints )
					: static_cast<std::uint32_t>(c.size + m_rng.below( 9 ) - 4) );
				break;
			case 5:	// duplicate the chunk right after itself
			{
				const std::string copy = d.substr( c.header, 10 + c.size );
				d.insert( c.header + 10 + c.size, copy );
				adjustParents( d, chunks, c.parent, static_cast<long long>(copy.size()) );
				break;
			}
			case 6:	// remove the chunk
				d.erase( c.header, 10 + c.size );
				adjustParents( d, chunks, c.parent, -static_cast<long long>(10 + c.size) );
				break;
			default:	// cut the file inside or after this chunk
				d.resize( std::min( d.size(), data + m_rng.below( c.size + 1 ) ) );
				break;
		}
	}

	void rawBytes( std::string &d )
	{
		if ( d.empty() )
			return;
		if ( m_rng.below( 4 ) == 0 )
		{
			d.resize( m_rng.below( d.size() ) );
			return;
		}
		const int count = 1 + static_cast<int>(m_rng.below( 8 ));
		for ( int i = 0; i < count; ++i )
			d[m_rng.below( d.size() )] = static_cast<char>(m_rng.next());
	}

	std::vector<std::string> m_corpus;
	Rng m_rng;
};

int runOne( const char *file )
{
	Harness::verbose = true;
	const LoadResult r = loadMap( readFile( file ) );
	std::printf( "%s: %s, %lld assertions\n", file, r == LoadResult::Loaded ? "loaded" : "rejected", Harness::assertions );
	return 0;
}

}	// namespace

int main( int argc, char **argv )
{
	Harness::installCrashHandlers( "current.map" );

	if ( argc == 4 && std::strcmp( argv[1], "--run" ) == 0 )
	{
		Harness::createSubsystems();
		createMapSubsystems();
		Harness::loadBase( argv[2] );
		setUpUserMapDir();
		return runOne( argv[3] );
	}
	if ( argc != 4 )
	{
		std::fprintf( stderr, "usage: MapFuzz <seed dir> <iterations> <rng seed> | --run <seed dir> <file>\n" );
		return 2;
	}
	const std::filesystem::path seedDir = argv[1];
	const long long iterations = std::atoll( argv[2] );
	const std::uint64_t rngSeed = std::strtoull( argv[3], nullptr, 10 );

	Harness::createSubsystems();
	createMapSubsystems();
	Harness::loadBase( seedDir );
	setUpUserMapDir();

	std::vector<std::string> corpus;
	int badSeeds = 0;
	for ( const auto &entry : std::filesystem::directory_iterator( seedDir / "map" ) )
	{
		const std::string data = readFile( entry.path() );
		// seeds must load; assertions are counted, not required to be absent (the editor's sample
		// map lacks keys that maps from the original editor have, such as playerAllies)
		if ( loadMap( data ) != LoadResult::Loaded )
		{
			std::fprintf( stderr, "MapFuzz: seed %s does not load cleanly\n", entry.path().filename().string().c_str() );
			++badSeeds;
		}
		corpus.push_back( data );
	}
	if ( corpus.empty() )
	{
		std::fprintf( stderr, "MapFuzz: no seeds in %s\n", (seedDir / "map").string().c_str() );
		return 2;
	}

	for ( const std::string &data : corpus )
	{
		Harness::resetCoverage();
		loadMap( data );
		Harness::newCoverage();
	}

	Harness::resetDictionary();
	Harness::startWatchdog();
	MapMutator mutator( corpus, rngSeed );
	long long loaded = 0, rejected = 0, added = 0;
	long long done = 0;
	for ( long long i = 0; i < iterations && !Harness::timeUp(); ++i, ++done )
	{
		Harness::iteration = i;
		const std::string data = mutator.next();
		Harness::resetCoverage();
		if ( loadMap( data ) == LoadResult::Loaded )
			++loaded;
		else
			++rejected;
		if ( Harness::newCoverage() && mutator.size() < 5000 )
		{
			mutator.add( data );
			++added;
		}
		++Harness::progress;
	}
	TheMemoryPoolFactory->debugMemoryVerify();

	std::printf( "%lld inputs: %lld loaded, %lld rejected; %lld assertions; %d seeds not clean; %lld added to the corpus, %zu edges, %zu dictionary values\n",
		done, loaded, rejected, Harness::assertions, badSeeds, added, Harness::coveredEdges(), Harness::dictionarySize() );
	// the last input is a damaged map in the user map folder; other harnesses sharing the prefix would list it
	std::error_code ec;
	std::filesystem::remove_all( s_mapFile.parent_path(), ec );
	return badSeeds ? 1 : 0;
}
