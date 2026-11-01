/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// CrashReport.cpp -- see CrashReport.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "formatinto.h"

#include "Common/CrashReport.h"
#include "Engine/CpuTopology.h"
#include "Engine/EngineLog.h"
#include "Common/GlobalData.h"
#include "Common/Recorder.h"
#include "Common/StackDump.h"
#include "Common/version.h"
#include "GameLogic/GameLogic.h"

// <imagehlp.h>, not <dbghelp.h>: the precompiled header (PreRTS.h) already includes imagehlp.h,
// and dbghelp.h would define the same types again (C2011/C2365). imagehlp.h includes
// minidumpapiset.h (MiniDumpWriteDump).
#include <imagehlp.h>

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
		fprintf( out, "Map:         %s\n", TheGlobalData->m_mapName.str() );
		fprintf( out, "Worker:      %d\n", TheGlobalData->m_workerThreads );
	}
	if( TheGameLogic != nullptr )
	{
		fprintf( out, "Logic frame: %u\n", TheGameLogic->getFrame() );
		fprintf( out, "Game mode:   %d (in game %d, shell %d)\n",
			static_cast<Int>(TheGameLogic->getGameMode()), TheGameLogic->isInGame() ? 1 : 0,
			TheGameLogic->isInShellGame() ? 1 : 0 );
	}
	if( TheRecorder != nullptr )
		fprintf( out, "Recorder:    mode %d\n", static_cast<Int>(TheRecorder->getMode()) );

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

	if( outFolder != nullptr && outFolderSize > 0 )
	{
		strncpy( outFolder, folder, outFolderSize - 1 );
		outFolder[outFolderSize - 1] = 0;
	}
	return TRUE;
}
