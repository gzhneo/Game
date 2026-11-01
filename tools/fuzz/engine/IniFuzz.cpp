/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// INI harness: loads mutated map.ini files into the game's data stores the way GameLogic does at
// map start (INI_LOAD_CREATE_OVERRIDES, then WeaponStore::postProcessLoad, reset at game end).
//
//   IniFuzz <seed dir> <iterations> <rng seed>   fuzz
//   IniFuzz --run <seed dir> <file>              load one file with the debug log (reproduce)
//
// Only block types whose stores the harness creates are fuzzed (see s_skippedBlocks).

#include "PreRTS.h"

#include "Common/DamageFX.h"
#include "Common/GameMemory.h"
#include "Common/INI.h"
#include "Common/ModuleFactory.h"
#include "Common/Upgrade.h"
#include "GameClient/FXList.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/Armor.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Weapon.h"

#include "Harness.h"

#include <cstdio>
#include <cstring>
#include <sstream>

using Harness::Rng;
using Harness::readFile;
using Harness::writeFile;

namespace
{

// Blocks whose store the harness does not create (audio, windows, video, the UI); a mutated file
// naming one of them is not loaded.
const char *const s_skippedBlocks[] = {
	"AudioEvent", "AudioSettings", "Campaign", "ChallengeGenerals", "CommandButton", "CommandMap",
	"CommandSet", "ControlBarScheme", "ControlBarResizer", "Credits", "WindowTransition",
	"DialogEvent", "DrawGroupInfo", "EvaEvent", "InGameUI", "Language", "MapCache", "MapData",
	"MiscAudio", "Mouse", "MouseCursor", "MusicTrack", "OnlineChatColors", "ShellMenuScheme",
	"Video", "WaterSet", "WaterTransparency", "Weather", "WebpageURL", "HeaderTemplate",
	"StaticGameLOD", "DynamicGameLOD", "LODPreset", "BenchProfile", "ReallyLowMHz",
	"ScriptAction", "ScriptCondition",
};

enum class LoadResult { Loaded, Rejected };

LoadResult loadMapIni( const char *path )
{
	TheModuleFactory->beginMapModuleData();
	LoadResult result = LoadResult::Loaded;
	try
	{
		INI ini;
		ini.load( AsciiString( path ), INILoadType::INI_LOAD_CREATE_OVERRIDES, nullptr );
	}
	catch ( ... )
	{
		// the game does not start the map (GameLogic::abortNewGame); for the harness it is a
		// rejected file
		result = LoadResult::Rejected;
	}
	TheWeaponStore->postProcessLoad();
	return result;
}

// --- mutation ------------------------------------------------------------------------------------

const char *const s_values[] = {
	"0", "1", "-1", "2", "255", "256", "65535", "65536", "-2147483648", "2147483647", "4294967295",
	"99999999999999999999999", "1e39", "-1e39", "3.4e38", "1e-45", "nan", "inf", "-inf", "0.0",
	"100%", "-100%", "1000000%", "%", "", " ", "=", "\"", "\"\"", "Yes", "No", "NONE", "ALL",
	"X:0", "Y:-1", "Z:1e30", "R:300 G:-5 B:99999", "MinX:1 MaxX:-1", "0x7fffffff", ":", ";", "//",
	"End", "END", "Object", "Weapon", "Default", "-", "+", "*", "\t",
};

std::string longToken( Rng &rng )
{
	static const char *const fills[] = { "A", "9", "%", ":", " ", "\"" };
	return std::string( 256 + rng.below( 20000 ), fills[rng.below( 6 )][0] );
}

std::vector<std::string> splitLines( const std::string &text )
{
	std::vector<std::string> lines;
	std::string line;
	std::istringstream in( text );
	while ( std::getline( in, line ) )
		lines.push_back( line );
	return lines;
}

std::string joinLines( const std::vector<std::string> &lines )
{
	std::string out;
	for ( const std::string &l : lines )
		out += l + "\n";
	return out;
}

std::vector<std::string> tokensOf( const std::string &line )
{
	std::vector<std::string> out;
	std::string cur;
	for ( char c : line )
	{
		if ( c == ' ' || c == '\t' || c == '=' || c == '\r' )
		{
			if ( !cur.empty() )
				out.push_back( cur );
			cur.clear();
		}
		else
			cur += c;
	}
	if ( !cur.empty() )
		out.push_back( cur );
	return out;
}

class Mutator
{
public:
	Mutator( std::vector<std::string> corpus, std::uint64_t seed ) : m_corpus( std::move( corpus ) ), m_rng{ seed }
	{
		for ( const std::string &text : m_corpus )
			for ( const std::string &line : splitLines( text ) )
			{
				m_lines.push_back( line );
				for ( const std::string &t : tokensOf( line ) )
					m_tokens.push_back( t );
			}
	}

	/// An input that reached new code; mutated like the seeds from now on.
	void add( const std::string &text )
	{
		m_corpus.push_back( text );
		for ( const std::string &line : splitLines( text ) )
		{
			if ( m_lines.size() < 100000 )
				m_lines.push_back( line );
			for ( const std::string &t : tokensOf( line ) )
				if ( m_tokens.size() < 100000 && t.size() < 256 )
					m_tokens.push_back( t );
		}
	}

	std::size_t size() const { return m_corpus.size(); }

	std::string next()
	{
		std::vector<std::string> lines = splitLines( m_corpus[m_rng.below( m_corpus.size() )] );
		const int count = 1 + static_cast<int>(m_rng.below( 6 ));
		for ( int i = 0; i < count; ++i )
			mutateLines( lines );
		std::string text = joinLines( lines );
		if ( m_rng.below( 8 ) == 0 )
			mutateBytes( text );
		return text;
	}

private:
	std::string pickValue()
	{
		switch ( m_rng.below( 4 ) )
		{
			case 0: return m_tokens.empty() ? std::string( "0" ) : m_tokens[m_rng.below( m_tokens.size() )];
			case 1: return longToken( m_rng );
			case 2:	// a constant the engine compares with
				if ( Harness::dictionarySize() > 0 && m_rng.below( 2 ) == 0 )
					return std::to_string( static_cast<std::int32_t>(Harness::dictionaryValue( m_rng )) );
				[[fallthrough]];
			default: return s_values[m_rng.below( std::size( s_values ) )];
		}
	}

	void mutateLines( std::vector<std::string> &lines )
	{
		if ( lines.empty() )
		{
			lines.push_back( m_lines.empty() ? std::string() : m_lines[m_rng.below( m_lines.size() )] );
			return;
		}
		std::string &line = lines[m_rng.below( lines.size() )];
		switch ( m_rng.below( 9 ) )
		{
			case 0:	// new value after '='
			{
				const std::size_t eq = line.find( '=' );
				if ( eq != std::string::npos )
					line = line.substr( 0, eq + 1 ) + " " + pickValue();
				break;
			}
			case 1:	// replace one token
			{
				std::vector<std::string> tokens = tokensOf( line );
				if ( tokens.empty() )
					break;
				const std::string &victim = tokens[m_rng.below( tokens.size() )];
				const std::size_t at = line.find( victim );
				line.replace( at, victim.size(), pickValue() );
				break;
			}
			case 2:	// add a value to the line
				line += " " + pickValue();
				break;
			case 3:
				lines.erase( lines.begin() + static_cast<std::ptrdiff_t>(m_rng.below( lines.size() )) );
				break;
			case 4:
			{
				const std::string copy = lines[m_rng.below( lines.size() )];
				lines.insert( lines.begin() + static_cast<std::ptrdiff_t>(m_rng.below( lines.size() + 1 )), copy );
				break;
			}
			case 5:	// line from another seed
				if ( !m_lines.empty() )
					lines.insert( lines.begin() + static_cast<std::ptrdiff_t>(m_rng.below( lines.size() + 1 )), m_lines[m_rng.below( m_lines.size() )] );
				break;
			case 6:	// splice a block of another seed
			{
				std::vector<std::string> other = splitLines( m_corpus[m_rng.below( m_corpus.size() )] );
				if ( other.empty() )
					break;
				const std::size_t from = m_rng.below( other.size() );
				const std::size_t len = 1 + m_rng.below( other.size() - from );
				lines.insert( lines.begin() + static_cast<std::ptrdiff_t>(m_rng.below( lines.size() + 1 )),
					other.begin() + static_cast<std::ptrdiff_t>(from), other.begin() + static_cast<std::ptrdiff_t>(from + len) );
				break;
			}
			case 7:	// cut the file
				lines.resize( m_rng.below( lines.size() ) + 1 );
				if ( m_rng.below( 2 ) )
					lines.back() = lines.back().substr( 0, m_rng.below( lines.back().size() + 1 ) );
				break;
			default:	// swap two lines
				std::swap( line, lines[m_rng.below( lines.size() )] );
				break;
		}
	}

	void mutateBytes( std::string &text )
	{
		static const char special[] = { '\0', '\t', '\r', '\n', '"', '=', ';', ':', '%', '-', char(0xFF), char(0x80) };
		if ( text.empty() )
			return;
		const int count = 1 + static_cast<int>(m_rng.below( 4 ));
		for ( int i = 0; i < count; ++i )
		{
			char &c = text[m_rng.below( text.size() )];
			c = m_rng.below( 2 ) ? special[m_rng.below( sizeof( special ) )] : static_cast<char>(m_rng.next());
		}
	}

	std::vector<std::string> m_corpus;
	std::vector<std::string> m_lines;
	std::vector<std::string> m_tokens;
	Rng m_rng;
};

bool namesSkippedBlock( const std::string &text )
{
	for ( const std::string &line : splitLines( text ) )
	{
		const std::vector<std::string> tokens = tokensOf( line );
		if ( tokens.empty() )
			continue;
		for ( const char *block : s_skippedBlocks )
			if ( _stricmp( tokens[0].c_str(), block ) == 0 )
				return true;
	}
	return false;
}

// Loads text as map.ini; true if it loaded without exception and without assertion.
bool loadsCleanly( const std::string &text )
{
	writeFile( Harness::workDir / "current.ini", text );
	const long long before = Harness::assertions;
	const bool ok = loadMapIni( "current.ini" ) == LoadResult::Loaded && Harness::assertions == before;
	Harness::resetAll();
	return ok;
}

// A generated seed marks candidate field lines with '?'; keep each candidate with which the file
// still loads cleanly. Returns false if even the file without candidates does not load.
bool buildGeneratedSeed( const std::string &generated, std::string &seed, int &kept, int &dropped )
{
	std::vector<std::string> fixed;		// lines of the result, candidates included once accepted
	std::vector<std::pair<std::size_t, std::string>> candidates;	// position in fixed, line
	for ( const std::string &line : splitLines( generated ) )
	{
		if ( !line.empty() && line[0] == '?' )
			candidates.emplace_back( fixed.size(), line.substr( 1 ) );
		else
			fixed.push_back( line );
	}
	if ( !loadsCleanly( joinLines( fixed ) ) )
		return false;
	// insert from the back so the recorded positions stay valid for the earlier candidates
	std::vector<std::string> result = fixed;
	for ( auto it = candidates.rbegin(); it != candidates.rend(); ++it )
	{
		std::vector<std::string> trial = result;
		trial.insert( trial.begin() + static_cast<std::ptrdiff_t>(it->first), it->second );
		if ( loadsCleanly( joinLines( trial ) ) )
		{
			result = trial;
			++kept;
		}
		else
			++dropped;
	}
	seed = joinLines( result );
	return true;
}

// A map INI must not outlive its game: what it redefined is back at the end of the game, what it
// created is gone. Checked on the base data (seeds/base) before fuzzing.
const char s_resetCheckIni[] =
	"Weapon TestGun\n  PrimaryDamage = 1.0\nEnd\n"
	"Weapon MapGunResetCheck\n  PrimaryDamage = 5.0\n  AttackRange = 100.0\n  DelayBetweenShots = 100\nEnd\n"
	"Armor TestArmor\n  Armor = DEFAULT 10%\nEnd\n"
	"Armor MapArmorResetCheck\n  Armor = DEFAULT 20%\nEnd\n"
	"ObjectCreationList OCL_Test\n  CreateObject\n    ObjectNames = TestProjectile\n    Count = 2\n  End\n"
	"  CreateObject\n    ObjectNames = TestProjectile\n    Count = 3\n  End\nEnd\n"
	"ObjectCreationList OCL_MapResetCheck\n  CreateObject\n    ObjectNames = TestProjectile\n  End\nEnd\n"
	"FXList FX_Test\n  ViewShake\n    Type = NORMAL\n  End\n  ViewShake\n    Type = STRONG\n  End\nEnd\n"
	"FXList FX_MapResetCheck\n  ViewShake\n    Type = NORMAL\n  End\nEnd\n"
	"DamageFX DFX_Test\n  ThrottleTime = Default 900\nEnd\n"
	"DamageFX DFX_MapResetCheck\n  ThrottleTime = Default 100\nEnd\n"
	"Upgrade Upgrade_Test\n  BuildCost = 7\nEnd\n"
	"Upgrade Upgrade_MapResetCheck\n  Type = OBJECT\n  BuildTime = 1.0\n  BuildCost = 1\nEnd\n"
	"ParticleSystem PS_Test\n  IsOneShot = Yes\n  Shader = ADDITIVE\nEnd\n"
	"ParticleSystem PS_MapResetCheck\n  Type = PARTICLE\n  ParticleName = map.tga\nEnd\n";

int checkMapReset()
{
	const NameKeyType mapGun = TheNameKeyGenerator->nameToKey( "MapGunResetCheck" );
	const ArmorTemplate *armor = TheArmorStore->findArmorTemplate( "TestArmor" );
	const ObjectCreationList *ocl = TheObjectCreationListStore->findObjectCreationList( "OCL_Test" );
	const FXList *fx = TheFXListStore->findFXList( "FX_Test" );
	const DamageFX *dfx = TheDamageFXStore->findDamageFX( "DFX_Test" );
	const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade( "Upgrade_Test" );
	const ParticleSystemTemplate *particles = TheParticleSystemManager->findTemplate( "PS_Test" );
	if ( armor == nullptr || ocl == nullptr || fx == nullptr || dfx == nullptr || upgrade == nullptr || particles == nullptr || TheWeaponStore->findWeaponTemplate( "TestGun" ) == nullptr )
	{
		std::fprintf( stderr, "IniFuzz: base data for the map reset check missing\n" );
		return 1;
	}
	const Real armorBefore = armor->adjustDamage( DAMAGE_EXPLOSION, 100.0f );
	const Real damageBefore = TheWeaponStore->findWeaponTemplate( "TestGun" )->getPrimaryDamage( WeaponBonus() );
	const size_t nuggetsBefore = ocl->getNuggetCount();
	const size_t fxBefore = fx->getNuggetCount();
	const UnsignedInt throttleBefore = dfx->getDamageFXThrottleTime( DAMAGE_EXPLOSION, nullptr );
	const Int costBefore = upgrade->calcCostToBuild( nullptr );
	const UpgradeMaskType maskBefore = upgrade->getUpgradeMask();
	UpgradeMaskType firstMapMask;
	const Bool oneShotBefore = particles->getInfo().m_isOneShot;
	const auto shaderBefore = particles->getInfo().m_shaderType;

	int failures = 0;
	auto check = [&failures]( bool ok, const char *what )
	{
		if ( !ok )
		{
			std::fprintf( stderr, "IniFuzz: map reset check failed: %s\n", what );
			++failures;
		}
	};
	for ( int round = 0; round < 2; ++round )
	{
		writeFile( Harness::workDir / "current.ini", s_resetCheckIni );
		check( loadMapIni( "current.ini" ) == LoadResult::Loaded, "check file loads" );
		check( armor->adjustDamage( DAMAGE_EXPLOSION, 100.0f ) != armorBefore, "map changes the armor" );
		check( TheArmorStore->findArmorTemplate( "MapArmorResetCheck" ) != nullptr, "map creates an armor" );
		check( TheWeaponStore->findWeaponTemplateByNameKey( mapGun ) != nullptr, "map creates a weapon" );
		check( TheObjectCreationListStore->findObjectCreationList( "OCL_MapResetCheck" ) != nullptr, "map creates an OCL" );
		check( ocl->getNuggetCount() == 2, "map redefines the OCL" );
		check( fx->getNuggetCount() == 2, "map redefines the FX list" );
		check( TheFXListStore->findFXList( "FX_MapResetCheck" ) != nullptr, "map creates an FX list" );
		check( dfx->getDamageFXThrottleTime( DAMAGE_EXPLOSION, nullptr ) != throttleBefore, "map changes the damage FX" );
		check( TheDamageFXStore->findDamageFX( "DFX_MapResetCheck" ) != nullptr, "map creates a damage FX" );
		check( upgrade->calcCostToBuild( nullptr ) == 7, "map changes the upgrade" );
		check( particles->getInfo().m_isOneShot != oneShotBefore && particles->getInfo().m_shaderType != shaderBefore,
			"map changes the particle system" );
		check( TheParticleSystemManager->findTemplate( "PS_MapResetCheck" ) != nullptr, "map creates a particle system" );
		const UpgradeTemplate *mapUpgrade = TheUpgradeCenter->findUpgrade( "Upgrade_MapResetCheck" );
		check( mapUpgrade != nullptr, "map creates an upgrade" );
		// the second map gets the same mask bit again: the bits of a map's upgrades are free after its game
		if( mapUpgrade != nullptr && round == 0 )
			firstMapMask = mapUpgrade->getUpgradeMask();
		else if( mapUpgrade != nullptr )
			check( mapUpgrade->getUpgradeMask() == firstMapMask, "mask bit of a map upgrade reused" );
		Harness::resetAll();
		check( TheArmorStore->findArmorTemplate( "TestArmor" ) == armor, "armor stays at its address" );
		check( armor->adjustDamage( DAMAGE_EXPLOSION, 100.0f ) == armorBefore, "armor restored" );
		check( TheArmorStore->findArmorTemplate( "MapArmorResetCheck" ) == nullptr, "map armor removed" );
		check( TheWeaponStore->findWeaponTemplateByNameKey( mapGun ) == nullptr, "map weapon removed" );
		check( TheWeaponStore->findWeaponTemplate( "TestGun" )->getPrimaryDamage( WeaponBonus() ) == damageBefore, "weapon restored" );
		check( TheObjectCreationListStore->findObjectCreationList( "OCL_Test" ) == ocl, "OCL stays at its address" );
		check( ocl->getNuggetCount() == nuggetsBefore, "OCL restored" );
		check( TheObjectCreationListStore->findObjectCreationList( "OCL_MapResetCheck" ) == nullptr, "map OCL removed" );
		check( TheFXListStore->findFXList( "FX_Test" ) == fx && fx->getNuggetCount() == fxBefore, "FX list restored in place" );
		check( TheFXListStore->findFXList( "FX_MapResetCheck" ) == nullptr, "map FX list removed" );
		check( TheDamageFXStore->findDamageFX( "DFX_Test" ) == dfx
			&& dfx->getDamageFXThrottleTime( DAMAGE_EXPLOSION, nullptr ) == throttleBefore, "damage FX restored in place" );
		check( TheDamageFXStore->findDamageFX( "DFX_MapResetCheck" ) == nullptr, "map damage FX removed" );
		check( TheUpgradeCenter->findUpgrade( "Upgrade_Test" ) == upgrade && upgrade->calcCostToBuild( nullptr ) == costBefore
			&& upgrade->getUpgradeMask() == maskBefore, "upgrade restored in place" );
		check( TheUpgradeCenter->findUpgrade( "Upgrade_MapResetCheck" ) == nullptr, "map upgrade removed" );
		check( TheParticleSystemManager->findTemplate( "PS_Test" ) == particles && particles->getInfo().m_isOneShot == oneShotBefore
			&& particles->getInfo().m_shaderType == shaderBefore, "particle system restored in place" );
		check( TheParticleSystemManager->findTemplate( "PS_MapResetCheck" ) == nullptr, "map particle system removed" );
	}
	std::printf( "map reset check: %d failures\n", failures );
	std::fflush( stdout );
	return failures;
}

int runOne( const char *file )
{
	Harness::verbose = true;
	const LoadResult r = loadMapIni( file );
	Harness::resetAll();
	std::printf( "%s: %s, %lld assertions\n", file, r == LoadResult::Loaded ? "loaded" : "rejected", Harness::assertions );
	return 0;
}

}	// namespace

int main( int argc, char **argv )
{
	Harness::installCrashHandlers( "current.ini" );

	if ( argc == 4 && std::strcmp( argv[1], "--run" ) == 0 )
	{
		Harness::createSubsystems();
		Harness::loadBase( argv[2] );
		return runOne( argv[3] );
	}
	if ( argc != 4 )
	{
		std::fprintf( stderr, "usage: IniFuzz <seed dir> <iterations> <rng seed> | --run <seed dir> <file>\n" );
		return 2;
	}
	const std::filesystem::path seedDir = argv[1];
	const long long iterations = std::atoll( argv[2] );
	const std::uint64_t rngSeed = std::strtoull( argv[3], nullptr, 10 );

	Harness::createSubsystems();
	Harness::loadBase( seedDir );
	const int resetFailures = checkMapReset();

	std::vector<std::string> corpus;
	for ( const auto &entry : std::filesystem::directory_iterator( seedDir / "ini" / "map" ) )
		corpus.push_back( readFile( entry.path() ) );
	if ( corpus.empty() )
	{
		std::fprintf( stderr, "IniFuzz: no seeds in %s\n", (seedDir / "ini" / "map").string().c_str() );
		return 2;
	}

	// Generated seeds (one object per module, seedgen.py)
	if ( std::filesystem::exists( seedDir / "gen" ) )
	{
		int built = 0, failed = 0, kept = 0, dropped = 0;
		for ( const auto &entry : std::filesystem::directory_iterator( seedDir / "gen" ) )
		{
			std::string seed;
			if ( buildGeneratedSeed( readFile( entry.path() ), seed, kept, dropped ) )
			{
				corpus.push_back( seed );
				++built;
			}
			else
				++failed;
		}
		std::printf( "generated seeds: %d usable, %d not loadable; %d fields kept, %d dropped\n", built, failed, kept, dropped );
		std::fflush( stdout );
	}

	// The seeds themselves must load cleanly.
	int badSeeds = 0;
	for ( const auto &entry : std::filesystem::directory_iterator( seedDir / "ini" / "map" ) )
	{
		writeFile( Harness::workDir / "current.ini", readFile( entry.path() ) );
		const long long before = Harness::assertions;
		if ( loadMapIni( "current.ini" ) != LoadResult::Loaded || Harness::assertions != before )
		{
			std::fprintf( stderr, "IniFuzz: seed %s does not load cleanly\n", entry.path().filename().string().c_str() );
			++badSeeds;
		}
		Harness::resetAll();
	}

	// coverage of the corpus as it is, so only inputs beyond it count as new
	for ( const std::string &text : corpus )
	{
		writeFile( Harness::workDir / "current.ini", text );
		Harness::resetCoverage();
		loadMapIni( "current.ini" );
		Harness::resetAll();
		Harness::newCoverage();
	}

	Harness::resetDictionary();
	Harness::startWatchdog();
	Mutator mutator( corpus, rngSeed );
	long long loaded = 0, rejected = 0, skipped = 0, added = 0;
	long long done = 0;
	for ( long long i = 0; i < iterations && !Harness::timeUp(); ++i, ++done )
	{
		Harness::iteration = i;
		const std::string text = mutator.next();
		if ( namesSkippedBlock( text ) )
		{
			++skipped;
			continue;
		}
		writeFile( Harness::workDir / "current.ini", text );
		Harness::resetCoverage();
		if ( loadMapIni( "current.ini" ) == LoadResult::Loaded )
			++loaded;
		else
			++rejected;
		Harness::resetAll();
		if ( Harness::newCoverage() && mutator.size() < 20000 )
		{
			mutator.add( text );
			++added;
		}
		++Harness::progress;
	}
	TheMemoryPoolFactory->debugMemoryVerify();

	std::printf( "%lld inputs: %lld loaded, %lld rejected, %lld skipped (block without store); %lld assertions; %d seeds not clean; "
		"%lld added to the corpus, %zu edges, %zu dictionary values\n",
		done, loaded, rejected, skipped, Harness::assertions, badSeeds, added, Harness::coveredEdges(), Harness::dictionarySize() );
	return ( badSeeds || resetFailures ) ? 1 : 0;
}
