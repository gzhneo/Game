/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Minimal stub for DebugCrash()/TheCurrentIgnoreCrashPtr.
//
// WWMath/WWLib/WWSaveLoad call DebugCrash() through the DEBUG_ASSERTCRASH/DEBUG_CRASH macros
// (Common/Debug.h). The only real implementation is in GameEngine/Common/System/Debug.cpp,
// which includes GameClient/GameText.h/Keyboard.h/Mouse.h and thus pulls in nearly the whole
// engine (see CMakeLists.txt). For a fast numeric test target a simple stub suffices: a real call
// is a test failure anyway, reported loudly on stderr instead of a modal dialog.
#include "PreRTS.h"
#include "Common/Debug.h"

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <new>

// The debug allocator overloads (new/delete with file/line) normally live in
// Common/System/GameMemory.cpp (the memory pool allocator), which pulls in the whole engine via
// Common/GlobalData.h/GameClient/ClientRandomValue.h. The unit test only needs a pass-through to
// the global allocator; file/line are ignored, which is enough for numeric tests without leak
// diagnostics.
void* operator new(size_t nSize, const char *, int)
{
	return ::operator new(nSize);
}

void operator delete(void *p, const char *, int)
{
	::operator delete(p);
}

void* operator new[](size_t nSize, const char *, int)
{
	return ::operator new[](nSize);
}

void operator delete[](void *p, const char *, int)
{
	::operator delete[](p);
}

DEBUG_EXTERN_C void DebugLog(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stdout, format, args);
	va_end(args);
}

DEBUG_EXTERN_C char* TheCurrentIgnoreCrashPtr = nullptr;

DEBUG_EXTERN_C void DebugCrash(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	fprintf(stderr, "DebugCrash raised during a test: ");
	vfprintf(stderr, format, args);
	fprintf(stderr, "\n");
	va_end(args);

	if (TheCurrentIgnoreCrashPtr != nullptr) {
		// The caller expects us to mark "ignore" instead of aborting -- see Common/Debug.h.
		*TheCurrentIgnoreCrashPtr = 1;
		return;
	}

	// Without an ignore pointer a DebugCrash in a unit test always fails the whole run (ctest
	// treats a non-zero exit code as failure).
	exit(1);
}
