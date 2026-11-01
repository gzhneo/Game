/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// wwtracy.h -- thin profiling layer over Tracy.
//
// Only the profile build (preset x64-profile, CMake ENABLE_TRACY=ON) defines TRACY_ENABLE; in all
// other builds the macros are empty and cost nothing. Lives in WWDebug, the lowest library, so
// WWLib/WW3D2 and the GameEngine can use the same macros.
//
//   PROF_ZONE("Name")          timed zone until the end of the enclosing block (name must be a literal)
//   PROF_FRAME()               end of a rendered frame (main frame bar in Tracy)
//   PROF_FRAME_NAMED("Name")   end of a named section with its own rate (e.g. logic tick)
//   PROF_PLOT("Name", value)   value over time (e.g. object count)
//   PROF_MESSAGE("Text")       marker in the timeline (literal)
//
// WWDebug_Set_Thread_Name() always sets the thread name (Windows SetThreadDescription, visible in
// the debugger and in crash dumps) and additionally for Tracy in the profile build.

#pragma once

#ifndef WWTRACY_H
#define WWTRACY_H

#ifdef TRACY_ENABLE

#include <tracy/Tracy.hpp>

#define PROF_ZONE(name)					ZoneScopedN(name)
#define PROF_FRAME()						FrameMark
#define PROF_FRAME_NAMED(name)	FrameMarkNamed(name)
#define PROF_PLOT(name, value)	TracyPlot(name, value)
#define PROF_MESSAGE(text)			TracyMessageL(text)

#else

#define PROF_ZONE(name)
#define PROF_FRAME()
#define PROF_FRAME_NAMED(name)
#define PROF_PLOT(name, value)
#define PROF_MESSAGE(text)

#endif // TRACY_ENABLE

void WWDebug_Set_Thread_Name(const char *name);

#endif // WWTRACY_H
