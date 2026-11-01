/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FormatCheck.h -- marks printf-style functions so the compiler checks the arguments against the
// format string.
//
// ENGINE_PRINTF_FORMAT(f, a) goes behind the declaration: f is the position of the format string,
// a the position of the first variadic argument (member functions count 'this' as 1). GCC and
// clang check every call (-Wformat); tools/syntaxcheck/befunde.sh (part of tools/gate.sh --full)
// runs clang with it over the whole engine. MSVC ignores the macro. GCC uses gnu_printf: the runtime behind the
// engine's formatting is the C99-conforming UCRT, not the old msvcrt that 'printf' means on MinGW.
//
// No engine header: also usable from C and from the stand-alone building blocks.

#pragma once

#ifndef ENGINE_FORMAT_CHECK_H
#define ENGINE_FORMAT_CHECK_H

#if defined(__clang__)
	#define ENGINE_PRINTF_FORMAT(formatIndex, firstArgument) __attribute__((format(printf, formatIndex, firstArgument)))
#elif defined(__GNUC__)
	#define ENGINE_PRINTF_FORMAT(formatIndex, firstArgument) __attribute__((format(gnu_printf, formatIndex, firstArgument)))
#else
	#define ENGINE_PRINTF_FORMAT(formatIndex, firstArgument)
#endif

#endif
