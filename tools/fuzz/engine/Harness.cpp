/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// See Harness.h.

#include "PreRTS.h"

#include "Common/DamageFX.h"
#include "Common/FileSystem.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/INI.h"
#include "Common/LocalFileSystem.h"
#include "Common/ModuleFactory.h"
#include "Common/MultiplayerSettings.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PlayerTemplate.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/TerrainTypes.h"
#include "Common/ThingFactory.h"
#include "Common/Upgrade.h"
#include "GameClient/Anim2D.h"
#include "GameClient/FXList.h"
#include "GameClient/Image.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/TerrainRoads.h"
#include "GameLogic/AI.h"
#include "GameLogic/Armor.h"
#include "GameLogic/CrateSystem.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/Weapon.h"
#include "Common/ArchiveFileSystem.h"
#include "W3DDevice/Common/W3DModuleFactory.h"
#include "Win32Device/Common/Win32BIGFileSystem.h"
#include "Win32Device/Common/Win32LocalFileSystem.h"

#include "Harness.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace Harness
{

std::filesystem::path workDir;
std::atomic<long long> iteration{ 0 };
std::atomic<long long> progress{ 0 };

namespace
{
constexpr std::size_t COVERAGE_MAP = 1 << 16;
unsigned char s_trace[COVERAGE_MAP];
unsigned char s_seen[COVERAGE_MAP];		///< hit-count classes reached so far, one bit each
std::uintptr_t s_previous = 0;

unsigned char hitClass( unsigned char count )
{
	if ( count >= 128 ) return 128;
	if ( count >= 32 ) return 64;
	if ( count >= 16 ) return 32;
	if ( count >= 8 ) return 16;
	if ( count >= 4 ) return 8;
	if ( count == 3 ) return 4;
	return count;	// 1 or 2
}
}	// namespace

void resetCoverage()
{
	std::memset( s_trace, 0, sizeof( s_trace ) );
	s_previous = 0;
}

bool newCoverage()
{
	bool found = false;
	for ( std::size_t i = 0; i < COVERAGE_MAP; ++i )
	{
		if ( s_trace[i] == 0 )
			continue;
		const unsigned char c = hitClass( s_trace[i] );
		if ( c & ~s_seen[i] )
		{
			s_seen[i] |= c;
			found = true;
		}
	}
	return found;
}

namespace
{
constexpr std::size_t DICTIONARY_SLOTS = 1 << 13;	///< open addressing, at most half full
std::uint64_t s_dictionary[DICTIONARY_SLOTS / 2];
std::size_t s_dictionaryCount = 0;
std::uint64_t s_dictionarySet[DICTIONARY_SLOTS];	///< value + 1, 0 is empty

void remember( std::uint64_t value )
{
	if ( s_dictionaryCount >= DICTIONARY_SLOTS / 2 )
		return;
	std::size_t slot = static_cast<std::size_t>((value * 0x9E3779B97F4A7C15ull) >> 51) & (DICTIONARY_SLOTS - 1);
	while ( s_dictionarySet[slot] != 0 )
	{
		if ( s_dictionarySet[slot] == value + 1 )
			return;
		slot = (slot + 1) & (DICTIONARY_SLOTS - 1);
	}
	s_dictionarySet[slot] = value + 1;
	s_dictionary[s_dictionaryCount++] = value;
}
}	// namespace

void resetDictionary()
{
	std::memset( s_dictionarySet, 0, sizeof( s_dictionarySet ) );
	s_dictionaryCount = 0;
}

std::size_t dictionarySize()
{
	return s_dictionaryCount;
}

std::uint64_t dictionaryValue( Rng &rng )
{
	return s_dictionaryCount ? s_dictionary[rng.below( s_dictionaryCount )] : 0;
}

std::size_t coveredEdges()
{
	std::size_t n = 0;
	for ( unsigned char c : s_seen )
		n += c != 0;
	return n;
}

namespace
{

// ParticleSystemManager without rendering; the harness only parses particle templates.
class HarnessParticleSystemManager : public ParticleSystemManager
{
public:
	virtual Int getOnScreenParticleCount( void ) override { return 0; }
	virtual void doParticles( RenderInfoClass & ) override {}
	virtual void queueParticleRender() override {}
};

// AudioManager without a device: field parsers look up audio events by name (getInfoForAudioEvent).
// It is not initialised, its init() would load the game's audio INI files.
class HarnessAudioManager : public AudioManager
{
public:
	virtual void audioDebugDisplay( DebugDisplayInterface *, void *, FILE * ) override {}
	virtual void stopAudio( AudioAffect ) override {}
	virtual void pauseAudio( AudioAffect ) override {}
	virtual void resumeAudio( AudioAffect ) override {}
	virtual void pauseAmbient( Bool ) override {}
	virtual void killAudioEventImmediately( AudioHandle ) override {}
	virtual void nextMusicTrack( void ) override {}
	virtual void prevMusicTrack( void ) override {}
	virtual Bool isMusicPlaying( void ) const override { return FALSE; }
	virtual Bool hasMusicTrackCompleted( const AsciiString &, Int ) const override { return FALSE; }
	virtual AsciiString getMusicTrackName( void ) const override { return AsciiString(); }
	virtual void openDevice( void ) override {}
	virtual void closeDevice( void ) override {}
	virtual void *getDevice( void ) override { return nullptr; }
	virtual void notifyOfAudioCompletion( UnsignedInt, UnsignedInt ) override {}
	virtual UnsignedInt getProviderCount( void ) const override { return 0; }
	virtual AsciiString getProviderName( UnsignedInt ) const override { return AsciiString(); }
	virtual UnsignedInt getProviderIndex( AsciiString ) const override { return 0; }
	virtual void selectProvider( UnsignedInt ) override {}
	virtual void unselectProvider( void ) override {}
	virtual UnsignedInt getSelectedProvider( void ) const override { return 0; }
	virtual void setSpeakerType( UnsignedInt ) override {}
	virtual UnsignedInt getSpeakerType( void ) override { return 0; }
	virtual UnsignedInt getNum2DSamples( void ) const override { return 0; }
	virtual UnsignedInt getNum3DSamples( void ) const override { return 0; }
	virtual UnsignedInt getNumStreams( void ) const override { return 0; }
	virtual Bool doesViolateLimit( AudioEventRTS * ) const override { return FALSE; }
	virtual Bool isPlayingLowerPriority( AudioEventRTS * ) const override { return FALSE; }
	virtual Bool isPlayingAlready( AudioEventRTS * ) const override { return FALSE; }
	virtual Bool isObjectPlayingVoice( UnsignedInt ) const override { return FALSE; }
	virtual void adjustVolumeOfPlayingAudio( AsciiString, Real ) override {}
	virtual void removePlayingAudio( AsciiString ) override {}
	virtual void removeAllDisabledAudio() override {}
	virtual Bool has3DSensitiveStreamsPlaying( void ) const override { return FALSE; }
	virtual void *getHandleForBink( void ) override { return nullptr; }
	virtual void releaseHandleForBink( void ) override {}
	virtual void friend_forcePlayAudioEventRTS( const AudioEventRTS * ) override {}
	virtual void setPreferredProvider( AsciiString ) override {}
	virtual void setPreferredSpeaker( AsciiString ) override {}
	virtual Real getFileLengthMS( AsciiString ) const override { return 0.0f; }
	virtual void closeAnySamplesUsingFile( const void * ) override {}
	virtual void setDeviceListenerPosition( void ) override {}
};

std::vector<SubsystemInterface *> s_subsystems;
std::string s_current = "current.dat";

// Return addresses from the faulting context, for addr2line (run.sh resolves them).
void printStack( const CONTEXT *faulting )
{
	CONTEXT context = *faulting;
	std::fprintf( stderr, "Harness: stack" );
	for ( int frame = 0; frame < 40 && context.Rip != 0; ++frame )
	{
		std::fprintf( stderr, " 0x%llx", static_cast<unsigned long long>(context.Rip) );
		DWORD64 imageBase = 0;
		PRUNTIME_FUNCTION function = RtlLookupFunctionEntry( context.Rip, &imageBase, nullptr );
		if ( function == nullptr )
		{
			// leaf function: the return address is on top of the stack
			context.Rip = *reinterpret_cast<DWORD64 *>(context.Rsp);
			context.Rsp += 8;
			continue;
		}
		void *handlerData = nullptr;
		DWORD64 establisher = 0;
		RtlVirtualUnwind( UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher, nullptr );
	}
	std::fprintf( stderr, "\n" );
}

void keepInput( const char *kind )
{
	const std::filesystem::path current = workDir / s_current;
	char name[96];
	std::snprintf( name, sizeof( name ), "%s-%lld%s", kind, iteration.load(), current.extension().string().c_str() );
	std::error_code ec;
	std::filesystem::copy_file( current, workDir / name, std::filesystem::copy_options::overwrite_existing, ec );
	std::fprintf( stderr, "Harness: %s at iteration %lld, input kept as %s\n", kind, iteration.load(), (workDir / name).string().c_str() );
	std::fflush( stderr );
}

LONG WINAPI onCrash( EXCEPTION_POINTERS *info )
{
	std::fprintf( stderr, "Harness: exception 0x%08lx at %p\n", info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress );
	printStack( info->ContextRecord );
	keepInput( "crash" );
	TerminateProcess( GetCurrentProcess(), 3 );
	return EXCEPTION_EXECUTE_HANDLER;
}

// An exception nobody catches: GCC calls terminate before unwinding, so the stack still shows the
// throw site.
void onTerminate()
{
	void *frames[40];
	const USHORT n = RtlCaptureStackBackTrace( 1, 40, frames, nullptr );
	std::fprintf( stderr, "Harness: uncaught exception\nHarness: stack" );
	for ( USHORT i = 0; i < n; ++i )
		std::fprintf( stderr, " %p", frames[i] );
	std::fprintf( stderr, "\n" );
	keepInput( "crash" );
	TerminateProcess( GetCurrentProcess(), 3 );
}

// abort(), also from a failed C assert: the call chain of the abort.
void onAbort( int )
{
	void *frames[40];
	const USHORT n = RtlCaptureStackBackTrace( 1, 40, frames, nullptr );
	std::fprintf( stderr, "Harness: abort\nHarness: stack" );
	for ( USHORT i = 0; i < n; ++i )
		std::fprintf( stderr, " %p", frames[i] );
	std::fprintf( stderr, "\n" );
	keepInput( "crash" );
	TerminateProcess( GetCurrentProcess(), 3 );
}

HANDLE s_mainThread = nullptr;

// The stalled thread may hold a lock the report needs (stdio, heap): end the process in any case.
DWORD WINAPI lastResort( void * )
{
	Sleep( 30000 );
	TerminateProcess( GetCurrentProcess(), 4 );
	return 0;
}

DWORD WINAPI watchdog( void * )
{
	long long last = -1;
	for ( ;; )
	{
		Sleep( 20000 );
		const long long now = progress.load();
		if ( now == last )
		{
			CreateThread( nullptr, 0, lastResort, nullptr, 0, nullptr );
			// where the main thread is stuck
			CONTEXT context = {};
			context.ContextFlags = CONTEXT_FULL;
			if ( s_mainThread && SuspendThread( s_mainThread ) != static_cast<DWORD>(-1) && GetThreadContext( s_mainThread, &context ) )
				printStack( &context );
			keepInput( "hang" );
			TerminateProcess( GetCurrentProcess(), 4 );
		}
		last = now;
	}
}

}	// namespace

void addSubsystem( SubsystemInterface *subsystem )
{
	subsystem->init();
	s_subsystems.push_back( subsystem );
}

void createSubsystems()
{
	initMemoryManager();
	::Engine::context().nameKeyGenerator = new NameKeyGenerator;
	TheNameKeyGenerator->init();
	::Engine::context().fileSystem = new FileSystem;
	create( ::Engine::context().localFileSystem, static_cast<LocalFileSystem *>(new Win32LocalFileSystem) );
	// no .big files in the working directory: an empty archive file system, as FileSystem expects one
	create( ::Engine::context().archiveFileSystem, static_cast<ArchiveFileSystem *>(new Win32BIGFileSystem) );
	create( ::Engine::context().writableGlobalData, new GlobalData );
	create( ::Engine::context().scienceStore, new ScienceStore );
	create( ::Engine::context().multiplayerSettings, new MultiplayerSettings );
	create( ::Engine::context().terrainTypes, new TerrainTypeCollection );
	create( ::Engine::context().terrainRoads, new TerrainRoadCollection );
	// with the W3D draw modules, so Draw blocks of map.ini objects are read as in the game
	create( ::Engine::context().moduleFactory, static_cast<ModuleFactory *>(new W3DModuleFactory) );
	create( ::Engine::context().rankInfoStore, new RankInfoStore );
	create( ::Engine::context().playerTemplateStore, new PlayerTemplateStore );
	::Engine::context().audio = new HarnessAudioManager;
	create( ::Engine::context().particleSystemManager, static_cast<ParticleSystemManager *>(new HarnessParticleSystemManager) );
	create( ::Engine::context().anim2DCollection, new Anim2DCollection );
	create( ::Engine::context().mappedImageCollection, new ImageCollection );
	create( ::Engine::context().fxListStore, new FXListStore );
	create( ::Engine::context().weaponStore, new WeaponStore );
	create( ::Engine::context().objectCreationListStore, new ObjectCreationListStore );
	create( ::Engine::context().locomotorStore, new LocomotorStore );
	create( ::Engine::context().specialPowerStore, new SpecialPowerStore );
	create( ::Engine::context().damageFXStore, new DamageFXStore );
	create( ::Engine::context().armorStore, new ArmorStore );
	create( ::Engine::context().thingFactory, new ThingFactory );
	create( ::Engine::context().upgradeCenter, new UpgradeCenter );
	create( ::Engine::context().ai, new AI );
	create( ::Engine::context().crateSystem, new CrateSystem );
}

void postProcessAll()
{
	for ( SubsystemInterface *s : s_subsystems )
		s->postProcessLoad();
}

void resetAll()
{
	for ( auto it = s_subsystems.rbegin(); it != s_subsystems.rend(); ++it )
		(*it)->reset();
}

void loadBase( const std::filesystem::path &seedDir )
{
	for ( const auto &entry : std::filesystem::directory_iterator( seedDir / "base" ) )
	{
		try
		{
			INI ini;
			ini.load( AsciiString( entry.path().string().c_str() ), INILoadType::INI_LOAD_OVERWRITE, nullptr );
		}
		catch ( ... )
		{
			std::fprintf( stderr, "Harness: base data %s does not load (HARNESS_VERBOSE=1 shows why)\n", entry.path().string().c_str() );
			std::exit( 2 );
		}
	}
	postProcessAll();
}

std::string readFile( const std::filesystem::path &p )
{
	std::ifstream in( p, std::ios::binary );
	return std::string( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
}

void writeFile( const std::filesystem::path &p, const std::string &data )
{
	std::ofstream out( p, std::ios::binary | std::ios::trunc );
	out << data;
}

bool timeUp()
{
	static const long long limit = std::getenv( "HARNESS_SECONDS" ) ? std::atoll( std::getenv( "HARNESS_SECONDS" ) ) : 0;
	static const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	return limit > 0 && std::chrono::steady_clock::now() - start >= std::chrono::seconds( limit );
}

void installCrashHandlers( const char *current )
{
	s_current = current;
	workDir = std::filesystem::current_path();
	verbose = std::getenv( "HARNESS_VERBOSE" ) != nullptr;
	// Room for the crash report after a stack overflow (deep recursion on a malformed input).
	ULONG guarantee = 256 * 1024;
	SetThreadStackGuarantee( &guarantee );
	SetUnhandledExceptionFilter( onCrash );
	std::signal( SIGABRT, onAbort );
	std::set_terminate( onTerminate );
}

void startWatchdog()
{
	DuplicateHandle( GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &s_mainThread, 0, FALSE, DUPLICATE_SAME_ACCESS );
	CreateThread( nullptr, 0, watchdog, nullptr, 0, nullptr );
}

}	// namespace Harness

// Called by GCC for comparisons in the instrumented engine code: the constant operand goes into
// the dictionary; comparisons of two variables are not recorded.
extern "C" void __sanitizer_cov_trace_const_cmp1( std::uint8_t constant, std::uint8_t ) { Harness::remember( constant ); }
extern "C" void __sanitizer_cov_trace_const_cmp2( std::uint16_t constant, std::uint16_t ) { Harness::remember( constant ); }
extern "C" void __sanitizer_cov_trace_const_cmp4( std::uint32_t constant, std::uint32_t ) { Harness::remember( constant ); }
extern "C" void __sanitizer_cov_trace_const_cmp8( std::uint64_t constant, std::uint64_t ) { Harness::remember( constant ); }
extern "C" void __sanitizer_cov_trace_cmp1( std::uint8_t, std::uint8_t ) {}
extern "C" void __sanitizer_cov_trace_cmp2( std::uint16_t, std::uint16_t ) {}
extern "C" void __sanitizer_cov_trace_cmp4( std::uint32_t, std::uint32_t ) {}
extern "C" void __sanitizer_cov_trace_cmp8( std::uint64_t, std::uint64_t ) {}
extern "C" void __sanitizer_cov_trace_cmpf( float, float ) {}
extern "C" void __sanitizer_cov_trace_cmpd( double, double ) {}

// cases[0] is the number of cases, cases[1] the width in bits, then the case values.
extern "C" void __sanitizer_cov_trace_switch( std::uint64_t, std::uint64_t *cases )
{
	for ( std::uint64_t i = 0; i < cases[0]; ++i )
		Harness::remember( cases[2 + i] );
}

// Called by GCC at every basic block of the instrumented engine code.
extern "C" void __sanitizer_cov_trace_pc( void )
{
	const std::uintptr_t pc = reinterpret_cast<std::uintptr_t>( __builtin_return_address( 0 ) );
	const std::uintptr_t current = (pc ^ (pc >> 12)) & (Harness::COVERAGE_MAP - 1);
	unsigned char &count = Harness::s_trace[current ^ Harness::s_previous];
	if ( count != 255 )
		++count;
	Harness::s_previous = current >> 1;
}
