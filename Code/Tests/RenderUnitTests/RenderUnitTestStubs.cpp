/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// RenderUnitTestStubs.cpp --------------------------------------------------------------------------
//
// renderfvf.cpp pulls in the class pool of FVFInfoClass via W3DMPO_GLUE (always.h), and
// StringClass (WWLib) calls DebugCrash() on errors. Both are implemented in GameEngine
// (GameMemory.cpp, Debug.cpp), which this target deliberately does not link (see CMakeLists.txt).
// As in UnitTests/UnitTestStubs.cpp, stubs suffice: the pool passes through to malloc/free, and a
// DebugCrash is a test failure.
// ------------------------------------------------------------------------------------------------

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <new>

// Debug allocator overloads (new/delete with file/line), otherwise in GameMemory.cpp: pass-through.
void* operator new(size_t nSize, const char *, int) { return ::operator new(nSize); }
void operator delete(void *p, const char *, int) { ::operator delete(p); }
void* operator new[](size_t nSize, const char *, int) { return ::operator new[](nSize); }
void operator delete[](void *p, const char *, int) { ::operator delete[](p); }

void* createW3DMemPool(const char * /*poolName*/, int /*allocationSize*/)
{
	return nullptr;	// no real pool; allocateFromW3DMemPool ignores the pointer
}

void* allocateFromW3DMemPool(void * /*pool*/, int allocationSize)
{
	return malloc(allocationSize);
}

void* allocateFromW3DMemPool(void * /*pool*/, int allocationSize, const char * /*msg*/, int /*unused*/)
{
	return malloc(allocationSize);
}

void freeFromW3DMemPool(void * /*pool*/, void *p)
{
	free(p);
}

extern "C" char* TheCurrentIgnoreCrashPtr = nullptr;

extern "C" void DebugLog(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stdout, format, args);
	va_end(args);
}

extern "C" void DebugCrash(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	fprintf(stderr, "DebugCrash raised during a test: ");
	vfprintf(stderr, format, args);
	fprintf(stderr, "\n");
	va_end(args);
	exit(1);
}
