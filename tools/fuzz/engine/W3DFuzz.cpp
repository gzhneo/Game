/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// W3D harness: reads mutated model files and DDS textures the way the game reads them from a mod or
// a transferred map's folder.
//   - .w3d: WW3DAssetManager::Load_3D_Assets (meshes with materials, skins and AABB trees,
//     hierarchies, classic and compressed animations, HLODs, boxes, null objects), then every
//     prototype is instantiated and every animation is applied to every model with bones,
//   - .dds: DDSFileClass with reduction 0 to 3 (header, DX10 header, mip chain), every level is read
//     and copied into an A8R8G8B8 buffer as the texture loader does.
// No render device: textures named by the models are not created (load on demand stays off).
//
//   W3DFuzz <seed dir> <iterations> <rng seed>   fuzz <seed dir>/w3d/*.w3d and *.dds
//   W3DFuzz --run <seed dir> <file>              read one file with the debug log

#include "PreRTS.h"

#include "WW3D2/System/assetmgr.h"
#include "WW3D2/Material/ddsfile.h"
#include "WW3D2/Animation/hanim.h"
#include "WW3D2/Animation/htree.h"
#include "WW3D2/Mesh/rendobj.h"
#include "WWLib/RAWFILE.H"

#include "Harness.h"

#include <cstdio>
#include <cstring>

using Harness::Rng;
using Harness::readFile;
using Harness::writeFile;

namespace
{

bool isDds( const std::string &data )
{
	return data.size() >= 4 && data.compare( 0, 4, "DDS " ) == 0;
}

// --- reading -------------------------------------------------------------------------------------

std::vector<std::string> assetNames( AssetIterator *it )
{
	std::vector<std::string> names;
	if ( it == nullptr )
		return names;
	for ( it->First(); !it->Is_Done(); it->Next() )
		names.emplace_back( it->Current_Item_Name() );
	return names;
}

void readW3D( const std::string &data )
{
	writeFile( Harness::workDir / "current.w3d", data );
	WW3DAssetManager *assets = WW3DAssetManager::Get_Instance();
	{
		RawFileClass file( "current.w3d" );
		assets->Load_3D_Assets( file );
	}

	RenderObjIterator *objects = assets->Create_Render_Obj_Iterator();
	const std::vector<std::string> models = assetNames( objects );
	assets->Release_Render_Obj_Iterator( objects );
	AssetIterator *anims = assets->Create_HAnim_Iterator();
	const std::vector<std::string> animNames = assetNames( anims );
	delete anims;
	if ( Harness::verbose )
	{
		// what a reproduced input loaded (a seed that loads nothing tests nothing)
		std::printf( "models:" );
		for ( const std::string &name : models )
			std::printf( " %s", name.c_str() );
		std::printf( "\nanimations:" );
		for ( const std::string &name : animNames )
			std::printf( " %s", name.c_str() );
		std::printf( "\n" );
	}

	for ( const std::string &name : models )
	{
		RenderObjClass *obj = assets->Create_Render_Obj( name.c_str() );
		if ( obj == nullptr )
			continue;
		AABoxClass box;
		obj->Get_Obj_Space_Bounding_Box( box );
		const int bones = obj->Get_Num_Bones();
		for ( const std::string &animName : animNames )
		{
			HAnimClass *anim = assets->Get_HAnim( animName.c_str() );
			if ( anim == nullptr )
				continue;
			// a model takes an animation of another hierarchy as long as it has bones; the game
			// relies on the INI to pair them, a mod can pair them wrongly
			if ( bones > 0 )
			{
				const int frames = anim->Get_Num_Frames();
				const float probe[] = { 0.0f, 0.5f, static_cast<float>(frames / 2), static_cast<float>(frames > 0 ? frames - 1 : 0),
					static_cast<float>(frames) + 3.0f };
				for ( float frame : probe )
				{
					obj->Set_Animation( anim, frame );
					for ( int b = 0; b < bones; ++b )
						obj->Get_Bone_Transform( b );
				}
				obj->Set_Animation();
			}
			anim->Release_Ref();
		}
		for ( int b = 0; b < bones; ++b )
			obj->Get_Bone_Index( obj->Get_Bone_Name( b ) );
		obj->Release_Ref();
	}
	assets->Free_Assets();
}

void readDds( const std::string &data )
{
	writeFile( Harness::workDir / "current.w3d", data );	// kept on a crash (the type is in the data)
	writeFile( Harness::workDir / "current.dds", data );
	std::vector<unsigned char> surface;
	for ( unsigned reduction = 0; reduction < 4; ++reduction )
	{
		DDSFileClass dds( "current.dds", reduction );
		if ( !dds.Is_Available() || !dds.Load() )
			continue;
		const unsigned levels = dds.Get_Mip_Level_Count();
		for ( unsigned level = 0; level < levels; ++level )
		{
			const unsigned w = dds.Get_Width( level );
			const unsigned h = dds.Get_Height( level );
			dds.Get_Level_Size( level );
			if ( dds.Get_Memory_Pointer( level ) == nullptr || w == 0 || h == 0 || w > 8192 || h > 8192 )
				continue;
			if ( dds.Get_Type() == DDS_TEXTURE )
			{
				surface.assign( static_cast<std::size_t>(w) * h * 4, 0 );
				dds.Copy_Level_To_Surface( level, WW3D_FORMAT_A8R8G8B8, w, h, surface.data(), w * 4 );
				dds.Get_Pixel( level, 0, 0 );
				dds.Get_Pixel( level, w - 1, h - 1 );
			}
		}
	}
}

void readInput( const std::string &data )
{
	if ( isDds( data ) )
		readDds( data );
	else
		readW3D( data );
}

// --- chunk-aware mutation ------------------------------------------------------------------------

struct ChunkPos
{
	std::size_t header;		///< offset of the 8-byte header (id, size with the "has children" bit)
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

bool scanChunks( const std::string &d, std::size_t from, std::size_t to, int parent, std::vector<ChunkPos> &out, int depth )
{
	const std::size_t first = out.size();
	std::size_t pos = from;
	while ( pos + 8 <= to )
	{
		const std::uint32_t raw = get32( d, pos + 4 );
		const std::uint32_t size = raw & 0x7FFFFFFFu;
		if ( size > to - pos - 8 )
			break;
		const int index = static_cast<int>(out.size());
		out.push_back( { pos, size, parent, true } );
		if ( (raw & 0x80000000u) && depth < 16 )
		{
			const std::size_t before = out.size();
			if ( scanChunks( d, pos + 8, pos + 8 + size, index, out, depth + 1 ) )
				out[index].leaf = false;
			else
				out.resize( before );
		}
		pos += 8 + size;
	}
	if ( pos != to )
	{
		out.resize( first );
		return false;
	}
	return true;
}

const std::uint32_t s_ints[] = {
	0, 1, 2, 3, 4, 7, 8, 15, 16, 0x7F, 0x80, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x7FFFFFFF,
	0x80000000u, 0x80000001u, 0xFFFFFFFFu, 0xFFFFFFFEu, 1000, 100000, 0x00040002u /* mesh version 4.2 */,
	0x7F800000u /* +inf */, 0xFF800000u /* -inf */, 0x7FC00000u /* NaN */, 0x4E6E6B28u /* 1e9f */,
	0x20434244u /* "DBC " */, 0x31545844u /* "DXT1" */, 0x35545844u /* "DXT5" */, 0x30315844u /* "DX10" */, 98, 71, 77,
};

class W3DMutator
{
public:
	W3DMutator( std::vector<std::string> corpus, std::uint64_t seed ) : m_corpus( std::move( corpus ) ), m_rng{ seed } {}

	void add( const std::string &data ) { m_corpus.push_back( data ); }
	std::size_t size() const { return m_corpus.size(); }

	std::string next()
	{
		std::string d = m_corpus[m_rng.below( m_corpus.size() )];
		// now and then splice a chunk of another model in (a mesh into a file with a hierarchy)
		if ( m_rng.below( 16 ) == 0 )
			splice( d, m_corpus[m_rng.below( m_corpus.size() )] );
		const int count = 1 + static_cast<int>(m_rng.below( 4 ));
		for ( int i = 0; i < count; ++i )
			mutate( d );
		return d;
	}

private:
	void adjustParents( std::string &d, const std::vector<ChunkPos> &chunks, int parent, long long delta )
	{
		for ( int p = parent; p >= 0; p = chunks[p].parent )
		{
			const std::uint32_t raw = get32( d, chunks[p].header + 4 );
			const std::uint32_t size = static_cast<std::uint32_t>(static_cast<long long>(raw & 0x7FFFFFFFu) + delta);
			put32( d, chunks[p].header + 4, (raw & 0x80000000u) | (size & 0x7FFFFFFFu) );
		}
	}

	void splice( std::string &d, const std::string &other )
	{
		if ( isDds( d ) || isDds( other ) )
			return;
		std::vector<ChunkPos> chunks;
		scanChunks( other, 0, other.size(), -1, chunks, 0 );
		if ( chunks.empty() )
			return;
		const ChunkPos &c = chunks[m_rng.below( chunks.size() )];
		if ( c.parent < 0 )
			d += other.substr( c.header, 8 + c.size );
	}

	void mutate( std::string &d )
	{
		if ( isDds( d ) )
		{
			mutateDds( d );
			return;
		}
		std::vector<ChunkPos> chunks;
		scanChunks( d, 0, d.size(), -1, chunks, 0 );
		if ( chunks.empty() || m_rng.below( 10 ) == 0 )
		{
			rawBytes( d );
			return;
		}
		const ChunkPos &c = chunks[m_rng.below( chunks.size() )];
		const std::size_t data = c.header + 8;
		switch ( m_rng.below( 9 ) )
		{
			case 0:
			case 1:
			case 2:	// a value inside a leaf chunk (counts, indices, versions, floats)
			{
				if ( !c.leaf || c.size == 0 )
					break;
				if ( c.size >= 4 && m_rng.below( 3 ) )
					put32( d, data + (m_rng.below( 2 ) ? (m_rng.below( c.size / 4 ) * 4) : m_rng.below( c.size - 3 )),
						Harness::pickInt( m_rng, s_ints ) );
				else if ( c.size >= 2 && m_rng.below( 2 ) )
				{
					const std::uint16_t v = static_cast<std::uint16_t>(Harness::pickInt( m_rng, s_ints ));
					std::memcpy( &d[data + m_rng.below( c.size - 1 )], &v, 2 );
				}
				else
					d[data + m_rng.below( c.size )] = static_cast<char>(m_rng.next());
				break;
			}
			case 3:	// chunk id
				put32( d, c.header, m_rng.below( 2 ) ? Harness::pickInt( m_rng, s_ints ) : get32( d, c.header ) + static_cast<std::uint32_t>(m_rng.below( 5 )) - 2 );
				break;
			case 4:	// chunk size, without fixing the parents
			{
				const std::uint32_t raw = get32( d, c.header + 4 );
				const std::uint32_t size = m_rng.below( 2 ) ? Harness::pickInt( m_rng, s_ints )
					: static_cast<std::uint32_t>(c.size + m_rng.below( 17 ) - 8);
				put32( d, c.header + 4, (raw & 0x80000000u) | (size & 0x7FFFFFFFu) );
				break;
			}
			case 5:	// duplicate the chunk right after itself
			{
				const std::string copy = d.substr( c.header, 8 + c.size );
				d.insert( c.header + 8 + c.size, copy );
				adjustParents( d, chunks, c.parent, static_cast<long long>(copy.size()) );
				break;
			}
			case 6:	// remove the chunk
				d.erase( c.header, 8 + c.size );
				adjustParents( d, chunks, c.parent, -static_cast<long long>(8 + c.size) );
				break;
			case 7:	// shrink or grow a leaf chunk and its parents
			{
				if ( !c.leaf )
					break;
				if ( m_rng.below( 2 ) && c.size > 0 )
				{
					const std::size_t cut = 1 + m_rng.below( std::min<std::size_t>( c.size, 64 ) );
					d.erase( data + c.size - cut, cut );
					put32( d, c.header + 4, static_cast<std::uint32_t>(c.size - cut) );
					adjustParents( d, chunks, c.parent, -static_cast<long long>(cut) );
				}
				else
				{
					const std::size_t add = 1 + m_rng.below( 64 );
					d.insert( data + c.size, std::string( add, static_cast<char>(m_rng.next()) ) );
					put32( d, c.header + 4, static_cast<std::uint32_t>(c.size + add) );
					adjustParents( d, chunks, c.parent, static_cast<long long>(add) );
				}
				break;
			}
			default:	// cut the file inside or after this chunk
				d.resize( std::min( d.size(), data + m_rng.below( c.size + 1 ) ) );
				break;
		}
	}

	// Header fields (sizes, mip count, pixel format, DX10 format and dimension), then the data.
	void mutateDds( std::string &d )
	{
		static const std::size_t fields[] = { 8, 12, 16, 20, 24, 28, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 128, 132, 136, 140 };
		switch ( m_rng.below( 4 ) )
		{
			case 0:
			case 1:
			{
				const std::size_t at = fields[m_rng.below( std::size( fields ) )];
				if ( at + 4 <= d.size() )
					put32( d, at, m_rng.below( 3 ) ? Harness::pickInt( m_rng, s_ints ) : get32( d, at ) + static_cast<std::uint32_t>(m_rng.below( 5 )) - 2 );
				break;
			}
			case 2:
				d.resize( m_rng.below( d.size() + 1 ) );
				break;
			default:
				rawBytes( d );
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

void createAssetManager()
{
	WW3DAssetManager *assets = new WW3DAssetManager;
	assets->Set_WW3D_Load_On_Demand( false );
}

}	// namespace

int main( int argc, char **argv )
{
	Harness::installCrashHandlers( "current.w3d" );

	if ( argc == 4 && std::strcmp( argv[1], "--run" ) == 0 )
	{
		Harness::createSubsystems();
		Harness::loadBase( argv[2] );
		createAssetManager();
		Harness::verbose = true;
		readInput( readFile( argv[3] ) );
		std::printf( "%s: read, %lld assertions\n", argv[3], Harness::assertions );
		return 0;
	}
	if ( argc != 4 )
	{
		std::fprintf( stderr, "usage: W3DFuzz <seed dir> <iterations> <rng seed> | --run <seed dir> <file>\n" );
		return 2;
	}
	const std::filesystem::path seedDir = argv[1];
	const long long iterations = std::atoll( argv[2] );
	const std::uint64_t rngSeed = std::strtoull( argv[3], nullptr, 10 );

	Harness::createSubsystems();
	Harness::loadBase( seedDir );
	createAssetManager();

	std::vector<std::string> corpus;
	long long badSeeds = 0;
	for ( const auto &entry : std::filesystem::directory_iterator( seedDir / "w3d" ) )
	{
		const std::string data = readFile( entry.path() );
		const long long before = Harness::assertions;
		readInput( data );
		if ( Harness::assertions != before )
		{
			std::fprintf( stderr, "W3DFuzz: seed %s raises %lld assertions\n", entry.path().filename().string().c_str(), Harness::assertions - before );
			++badSeeds;
		}
		corpus.push_back( data );
	}
	if ( corpus.empty() )
	{
		std::fprintf( stderr, "W3DFuzz: no seeds in %s\n", (seedDir / "w3d").string().c_str() );
		return 2;
	}

	for ( const std::string &data : corpus )
	{
		Harness::resetCoverage();
		readInput( data );
		Harness::newCoverage();
	}

	Harness::resetDictionary();
	Harness::startWatchdog();
	W3DMutator mutator( corpus, rngSeed );
	long long added = 0, models = 0, textures = 0;
	long long done = 0;
	for ( long long i = 0; i < iterations && !Harness::timeUp(); ++i, ++done )
	{
		Harness::iteration = i;
		const std::string data = mutator.next();
		Harness::resetCoverage();
		if ( isDds( data ) )
			++textures;
		else
			++models;
		readInput( data );
		if ( Harness::newCoverage() && mutator.size() < 5000 )
		{
			mutator.add( data );
			++added;
		}
		++Harness::progress;
	}

	std::printf( "%lld inputs: %lld models, %lld textures; %lld assertions; %lld seeds not clean; %lld added to the corpus, %zu edges, %zu dictionary values\n",
		done, models, textures, Harness::assertions, badSeeds, added, Harness::coveredEdges(), Harness::dictionarySize() );
	return badSeeds ? 1 : 0;
}
