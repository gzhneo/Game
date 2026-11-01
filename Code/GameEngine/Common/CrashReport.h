/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// CrashReport.h -- one folder of files per crash.
//
// `WriteCrashReport()` creates a folder per crash:
//
//     Crashes\YYYY-MM-DD_HHMMSS\
//         CrashDump.dmp       minidump with all threads (including thread names)
//         Info.txt            reason, build, architecture, map, logic frame, game mode, time
//         EngineLog.txt       ring buffer of the last 2,000 log lines, also in Release
//         Replay.rep          the running game up to the crash, if the recorder was active
//         Stack.txt           call stack of the faulting thread with symbols (needs the pdb)
//
// Called from the exception filter (StackDump.cpp) and from the watchdog when a thread hangs, so
// it runs in a process that cannot be trusted. Hence the order: the minidump first. It needs only
// Windows calls, no heap and no engine singletons, so it is written even with a corrupted heap.
// Only then come the steps that read engine state (Info.txt, ring buffer) or allocate (replay
// copy: the recorder paths are AsciiStrings). Each step runs in its own __try/__except, so if one
// fails the earlier files are already complete on disk.

#pragma once

#ifndef __CRASHREPORT_H_
#define __CRASHREPORT_H_

#include "Lib/BaseType.h"

struct _EXCEPTION_POINTERS;

/// Writes a crash report. `reason` describes the cause (e.g. an exception code or a hung watchdog
/// thread). `exceptionInfo` may be NULL (watchdog, assert). Returns TRUE if the folder could be
/// created; its path is then in `outFolder` (if given).
Bool WriteCrashReport( const char *reason, struct _EXCEPTION_POINTERS *exceptionInfo,
	char *outFolder = nullptr, int outFolderSize = 0 );

#endif // __CRASHREPORT_H_
