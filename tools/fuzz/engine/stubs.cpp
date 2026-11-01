/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Stand-ins for what the engine harnesses link without: the program (WinMain.cpp), the device layer
// (W3D), Debug.cpp and the thread/CPU helpers the MinGW headers cannot build. Nothing here is
// reached while INI files load; functions that should never run abort loudly.

#include "PreRTS.h"

#include "Common/WellKnownKeys.h"
#include "Common/Debug.h"
#include "Common/MapObject.h"
#include "Common/ThingTemplate.h"
#include "Engine/CpuTopology.h"
#include "Engine/ThreadRole.h"
#include "GameClient/DebugDisplay.h"
#include "GameClient/Display.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/Shadow.h"
#include "WW3D2/Common/renderbackend.h"
#include "WW3D2/Material/texutil.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "HarnessHooks.h"

[[noreturn]] static void notInHarness(const char *what)
{
	std::fprintf(stderr, "Harness: %s is not available in the harness\n", what);
	std::abort();
}

// --- Debug.cpp ----------------------------------------------------------------------------------

unsigned int DebugLevelMask = 0;
const char *TheDebugLevels[DEBUG_LEVEL_MAX] = {};
char *TheCurrentIgnoreCrashPtr = nullptr;
static int s_debugFlags = 0;

void DebugInit(int flags) { s_debugFlags = flags; }
void DebugShutdown() {}
int DebugGetFlags() { return s_debugFlags; }
void DebugSetFlags(int flags) { s_debugFlags = flags; }
void DebugLatchIgnoreAsserts() {}

void DebugLog(const char *format, ...)
{
	if (!Harness::verbose)
		return;
	va_list args;
	va_start(args, format);
	std::vfprintf(stderr, format, args);
	va_end(args);
}

// An assertion is a finding of the INI data, not of the harness: count it, show it in verbose mode.
void DebugCrash(const char *format, ...)
{
	++Harness::assertions;
	if (!Harness::verbose)
		return;
	std::fputs("ASSERTION: ", stderr);
	va_list args;
	va_start(args, format);
	std::vfprintf(stderr, format, args);
	va_end(args);
	std::fputc('\n', stderr);
	// with HARNESS_ASSERT_STACK set, the call chain of the assertion (run.sh resolves the first one)
	if (std::getenv("HARNESS_ASSERT_STACK"))
	{
		void *frames[32];
		const USHORT n = RtlCaptureStackBackTrace(1, 32, frames, nullptr);
		std::fputs("Harness: stack", stderr);
		for (USHORT i = 0; i < n; ++i)
			std::fprintf(stderr, " %p", frames[i]);
		std::fputc('\n', stderr);
	}
}

void ReleaseCrash(const char *reason)
{
	std::fprintf(stderr, "Harness: RELEASE_CRASH: %s\n", reason ? reason : "");
	std::abort();
}

void ReleaseCrashLocalized(const AsciiString &p, const AsciiString &m)
{
	std::fprintf(stderr, "Harness: RELEASE_CRASH: %s %s\n", p.str(), m.str());
	std::abort();
}

// --- WinMain.cpp --------------------------------------------------------------------------------

HWND ApplicationHWnd = nullptr;
const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";

// StackDump.cpp prints the address of WinMain; the harness starts at main().
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { notInHarness("WinMain"); }

// --- device layer -------------------------------------------------------------------------------


void ReloadAllTextures() {}
void addIcon(const Coord3D *, Real, Int, RGBColor) {}
void WWDebug_Set_Thread_Name(const char *) {}
void AudioDebugDisplay(DebugDisplayInterface *, void *, FILE *) {}
WindowMsgHandledType MOTDSystem(GameWindow *, UnsignedInt, WindowMsgData, WindowMsgData) { return WindowMsgHandledType::MSG_IGNORED; }

// texutil.cpp needs DirectXTex, which the harness does not link; it never creates a texture surface.
namespace TexUtil
{
HRESULT Copy_Surface(WW3DSurfaceHandle, const RECT *, WW3DSurfaceHandle, const RECT *, Filter) { return E_NOTIMPL; }
HRESULT Generate_Mip_Levels(WW3DTextureHandle, Filter) { return E_NOTIMPL; }
}

// --- CpuTopology.cpp, ThreadRole.cpp ------------------------------------------------------------

namespace Engine
{
const CpuTopology &CpuTopology::get()
{
	static CpuTopology topology;
	return topology;
}
void CpuTopology::describe(char *buffer, unsigned bufferSize) const
{
	if (bufferSize > 0)
		buffer[0] = 0;
}
bool logCpuTopologyAtStartup(bool) { return false; }

void setThreadRole(ThreadRole, const char *) {}
ThreadRole threadRole() { return ThreadRole{}; }
const char *threadRoleName(ThreadRole) { return "harness"; }
bool threadHasRole(ThreadRole) { return true; }
bool threadIsWorker() { return false; }
bool reportThreadRoleViolation(const char *, int, const char *, const char *) { return false; }
std::size_t threadRoleViolationSiteCount() { return 0; }
bool writeThreadRoleViolationsTo(const char *) { return false; }
void clearThreadRoleViolations() {}
}	// namespace Engine
