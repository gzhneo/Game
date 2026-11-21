/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// CrashReport.cpp -- see CrashReport.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "formatinto.h"

#include "Common/CrashReport.h"
#include "Engine/Clock.h"
#include "Engine/CpuTopology.h"
#include "Engine/EngineLog.h"
#include "Common/GlobalData.h"
#include "Common/GameLOD.h"
#include "Common/Recorder.h"
#include "Common/StackDump.h"
#include "Lib/SafeString.h"
#include <shellapi.h>
#include "Common/UserSettings.h"
#include "Common/version.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/GameLogic.h"

// <imagehlp.h>, not <dbghelp.h>: the precompiled header (PreRTS.h) already includes imagehlp.h,
// and dbghelp.h would define the same types again (C2011/C2365). imagehlp.h includes
// minidumpapiset.h (MiniDumpWriteDump).
#include <imagehlp.h>
#include <psapi.h>

// A failing step of the report must not take the others down (see WriteCrashReport below).
// Structured exception handling exists only with MSVC; elsewhere (g++ syntax checks) the macros
// are empty.
#if defined(_MSC_VER)
	#define CRASHREPORT_TRY		__try
	#define CRASHREPORT_CATCH	__except( EXCEPTION_EXECUTE_HANDLER ) { }
#else
	#define CRASHREPORT_TRY
	#define CRASHREPORT_CATCH
#endif

//-------------------------------------------------------------------------------------------------
/** Folder name from the current time: Crashes\YYYY-MM-DD_HHMMSS */
//-------------------------------------------------------------------------------------------------
static void buildCrashFolderName( char *buffer, int bufferSize )
{
	SYSTEMTIME now;
	::GetLocalTime( &now );
	WWFormat::Into(buffer, bufferSize, "Crashes\\{:04d}-{:02d}-{:02d}_{:02d}{:02d}{:02d}", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
	buffer[bufferSize - 1] = 0;
}

//-------------------------------------------------------------------------------------------------
/** Minidump with all threads. MiniDumpWithThreadInfo includes the thread names set with
	* SetThreadDescription, so the debugger shows "Sim", "Job worker 3" instead of a number. */
//-------------------------------------------------------------------------------------------------
static Bool writeMiniDump( const char *path, struct _EXCEPTION_POINTERS *exceptionInfo )
{
	HANDLE file = ::CreateFile( path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
	if( file == INVALID_HANDLE_VALUE )
		return FALSE;

	MINIDUMP_EXCEPTION_INFORMATION info;
	info.ThreadId = ::GetCurrentThreadId();
	info.ExceptionPointers = static_cast<EXCEPTION_POINTERS *>(exceptionInfo);
	info.ClientPointers = FALSE;

	const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithHandleData);
	const BOOL ok = ::MiniDumpWriteDump( ::GetCurrentProcess(), ::GetCurrentProcessId(), file, type,
		(exceptionInfo != nullptr) ? &info : nullptr, nullptr, nullptr );

	::CloseHandle( file );
	return ok ? TRUE : FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Info.txt: everything needed to reproduce the crash. No engine calls that could crash again;
	* only reads, each with a NULL check. */
//-------------------------------------------------------------------------------------------------
static void writeInfoFile( const char *path, const char *reason )
{
	FILE *out = fopen( path, "w" );
	if( out == nullptr )
		return;

	SYSTEMTIME now;
	::GetLocalTime( &now );
	fprintf( out, "Crash report\n" );
	fprintf( out, "Time:        %04d-%02d-%02d %02d:%02d:%02d\n",
		now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond );
	fprintf( out, "Reason:      %s\n", (reason != nullptr) ? reason : "unknown" );

	fprintf( out, "Arch:        x64\n" );
#if defined(_DEBUG)
	fprintf( out, "Build:       Debug\n" );
#else
	fprintf( out, "Build:       Release\n" );
#endif
	fprintf( out, "Compiled:    %s %s\n", __DATE__, __TIME__ );

	if( TheVersion != nullptr )
		fprintf( out, "Version:     %s\n", TheVersion->getAsciiVersion().str() );

	{
		char text[256];
		::Engine::CpuTopology::get().describe( text, sizeof(text) );
		fprintf( out, "CPU:         %s\n", text );
	}

	if( TheGlobalData != nullptr )
	{
		if( TheGlobalData->m_graphicsCard.isNotEmpty() )
			fprintf( out, "Graphics:    %s\n", TheGlobalData->m_graphicsCard.str() );
		fprintf( out, "Map:         %s\n", TheGlobalData->m_mapName.str() );
		fprintf( out, "Worker:      %d\n", TheGlobalData->m_workerThreads );
		fprintf( out, "Backend:     %s\n", UserSettings::useVulkanRenderer() ? "Vulkan" : "Direct3D 11" );
		fprintf( out, "Resolution:  %d x %d (%s)\n", TheGlobalData->m_xResolution, TheGlobalData->m_yResolution,
			TheGlobalData->m_windowed ? "windowed" : "fullscreen" );
		fprintf( out, "Effects:     0x%08x, MSAA %d, texture tier %d\n", static_cast<unsigned>(TheGlobalData->m_renderEffects),
			static_cast<int>(TheGlobalData->m_renderEffectMsaa), static_cast<int>(TheGlobalData->m_textureQuality) );
	}
	if( TheGameLODManager != nullptr )
		fprintf( out, "Detail:      static level %d, dynamic level %d\n",
			static_cast<Int>(TheGameLODManager->getStaticLODLevel()), static_cast<Int>(TheGameLODManager->getDynamicLODLevel()) );
	if( TheGameLogic != nullptr )
	{
		fprintf( out, "Logic frame: %u\n", TheGameLogic->getFrame() );
		fprintf( out, "Game mode:   %d (in game %d, shell %d)\n",
			static_cast<Int>(TheGameLogic->getGameMode()), TheGameLogic->isInGame() ? 1 : 0,
			TheGameLogic->isInShellGame() ? 1 : 0 );
	}
	if( TheRecorder != nullptr )
	{
		fprintf( out, "Recorder:    mode %d\n", static_cast<Int>(TheRecorder->getMode()) );
		// Single player games (campaign, challenge) are not recorded: their scripts use the local player,
		// so a replay would not reproduce the game. The map and logic frame above are then all there is.
		if( TheGameLogic != nullptr && TheGameLogic->getGameMode() == GAME_SINGLE_PLAYER )
			fprintf( out, "Replay:      none (single player games are not recorded)\n" );
	}
	if( TheParticleSystemManager != nullptr )
		fprintf( out, "Particles:   %u\n", static_cast<unsigned>(TheParticleSystemManager->getParticleCount()) );
	if( TheGameLogic != nullptr )
		fprintf( out, "Objects:     %u\n", static_cast<unsigned>(TheGameLogic->getObjectCount()) );
	{
		PROCESS_MEMORY_COUNTERS counters;
		counters.cb = sizeof(counters);
		if( ::GetProcessMemoryInfo( ::GetCurrentProcess(), &counters, sizeof(counters) ) )
			fprintf( out, "Memory:      working set %llu MB, peak %llu MB, committed %llu MB\n",
				static_cast<unsigned long long>(counters.WorkingSetSize >> 20), static_cast<unsigned long long>(counters.PeakWorkingSetSize >> 20),
				static_cast<unsigned long long>(counters.PagefileUsage >> 20) );
	}

	fclose( out );
}

//-------------------------------------------------------------------------------------------------
/** Saves the running game while the recorder is recording: the replay up to the crash is the only
	* reliable way to reproduce the fault. */
//-------------------------------------------------------------------------------------------------
static void copyCurrentReplay( const char *folder )
{
	if( TheRecorder == nullptr || TheRecorder->getMode() != RecorderModeType::RECORDERMODETYPE_RECORD )
		return;

	AsciiString source = TheRecorder->getReplayDir();
	source.concat( TheRecorder->getLastReplayFileName() );
	source.concat( TheRecorder->getReplayExtention() );

	char target[MAX_PATH];
	WWFormat::Into(target, "{}\\Replay.rep", folder);
	target[sizeof(target) - 1] = 0;
	::CopyFile( source.str(), target, FALSE );
}

//-------------------------------------------------------------------------------------------------
/** Call stack of the faulting thread as text (symbols from the pdb next to the exe), for reports that
	* have no debugger at hand. */
//-------------------------------------------------------------------------------------------------
static FILE *s_stackFile = nullptr;
static void writeStackLine( const char *line )
{
	if( s_stackFile != nullptr )
		fputs( line, s_stackFile );
}

//-------------------------------------------------------------------------------------------------
Bool WriteCrashReport( const char *reason, struct _EXCEPTION_POINTERS *exceptionInfo,
	char *outFolder, int outFolderSize )
{
	char folder[MAX_PATH];
	buildCrashFolderName( folder, sizeof(folder) );

	::CreateDirectory( "Crashes", nullptr );
	if( ::CreateDirectory( folder, nullptr ) == FALSE && ::GetLastError() != ERROR_ALREADY_EXISTS )
		return FALSE;

	char path[MAX_PATH];

	// Minidump first: only Windows calls, no heap, no engine. It is written even with corrupted
	// memory, and it is the most important part of the report.
	WWFormat::Into(path, "{}\\CrashDump.dmp", folder);
	path[sizeof(path) - 1] = 0;
	writeMiniDump( path, exceptionInfo );

	// Everything else reads engine state or allocates (the recorder paths are AsciiStrings from the
	// memory pool). In a process that just died from a corrupted heap this can crash again, so each
	// step is guarded on its own: if one fails, the earlier files are still complete on disk.
	CRASHREPORT_TRY
	{
		WWFormat::Into(path, "{}\\Info.txt", folder);
		path[sizeof(path) - 1] = 0;
		writeInfoFile( path, reason );
	}
	CRASHREPORT_CATCH

	// Ring buffer of the last log lines; also in the Release build, which has no debug log.
	CRASHREPORT_TRY
	{
		WWFormat::Into(path, "{}\\EngineLog.txt", folder);
		path[sizeof(path) - 1] = 0;
		::Engine::Log::writeRingBufferTo( path );
	}
	CRASHREPORT_CATCH

	CRASHREPORT_TRY
	{
		copyCurrentReplay( folder );
	}
	CRASHREPORT_CATCH

	if( exceptionInfo != nullptr && exceptionInfo->ContextRecord != nullptr )
	{
		CRASHREPORT_TRY
		{
			WWFormat::Into(path, "{}\\Stack.txt", folder);
			path[sizeof(path) - 1] = 0;
			s_stackFile = fopen( path, "w" );
			if( s_stackFile != nullptr )
			{
				StackDumpFromContext( exceptionInfo->ContextRecord, writeStackLine );
				fclose( s_stackFile );
				s_stackFile = nullptr;
			}
		}
		CRASHREPORT_CATCH
	}

	// The next start tells the player where the report is (CrashReportAnnouncePrevious). A watchdog report
	// (no exception) is no crash: the game keeps running.
	if( exceptionInfo != nullptr )
	{
		CRASHREPORT_TRY
		{
			FILE *marker = fopen( "Crashes\\Pending.txt", "w" );
			if( marker != nullptr )
			{
				fputs( folder, marker );
				fclose( marker );
			}
		}
		CRASHREPORT_CATCH
	}

	if( outFolder != nullptr && outFolderSize > 0 )
	{
		strncpy( outFolder, folder, outFolderSize - 1 );
		outFolder[outFolderSize - 1] = 0;
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void CrashReportAnnouncePrevious( Bool interactive )
{
	FILE *marker = fopen( "Crashes\\Pending.txt", "r" );
	if( marker == nullptr )
		return;
	char folder[MAX_PATH] = "";
	if( fgets( folder, sizeof(folder), marker ) == nullptr )
		folder[0] = 0;
	fclose( marker );

	for( size_t n = strlen( folder ); n > 0 && (folder[n - 1] == '\n' || folder[n - 1] == '\r'); --n )
		folder[n - 1] = 0;
	if( folder[0] == 0 )
	{
		remove( "Crashes\\Pending.txt" );
		return;
	}

	char full[MAX_PATH];
	if( ::GetFullPathName( folder, sizeof(full), full, nullptr ) == 0 )
		safe_strcpy( full, folder );

	ENGINE_LOG_WARNING( ::Engine::LOG_CAT_ENGINE, "The previous run ended with a crash; report in %s", full );
	DEBUG_LOG(( "previous run crashed, report in %s\n", full ));

	// Test and tool runs only log it and leave the note for the player's next start.
	if( !interactive )
		return;
	remove( "Crashes\\Pending.txt" );

	char text[MAX_PATH + 256];
	WWFormat::Into( text, "The game did not end properly last time.\n\nA crash report was saved in:\n{}\n\n"
		"Please send this folder along with your bug report. Open it now?", full );
	text[sizeof(text) - 1] = 0;
	if( ::MessageBoxA( nullptr, text, "Crash report", MB_YESNO | MB_ICONINFORMATION | MB_TASKMODAL ) == IDYES )
		::ShellExecuteA( nullptr, "open", full, nullptr, nullptr, SW_SHOWNORMAL );
}

//-------------------------------------------------------------------------------------------------
void CrashReportLogCounters( void )
{
	static UnsignedInt lastMs = 0;
	const UnsignedInt nowMs = ::Engine::Clock::milliseconds();
	if( nowMs - lastMs < 1000 )
		return;
	lastMs = nowMs;

	unsigned long long workingMB = 0;
	PROCESS_MEMORY_COUNTERS counters;
	counters.cb = sizeof(counters);
	if( ::GetProcessMemoryInfo( ::GetCurrentProcess(), &counters, sizeof(counters) ) )
		workingMB = counters.WorkingSetSize >> 20;

	ENGINE_LOG_INFO( ::Engine::LOG_CAT_ENGINE, "counters: frame %u, objects %u, particles %u, memory %llu MB",
		TheGameLogic != nullptr ? static_cast<unsigned>(TheGameLogic->getFrame()) : 0u,
		TheGameLogic != nullptr ? static_cast<unsigned>(TheGameLogic->getObjectCount()) : 0u,
		TheParticleSystemManager != nullptr ? static_cast<unsigned>(TheParticleSystemManager->getParticleCount()) : 0u,
		workingMB );
}
