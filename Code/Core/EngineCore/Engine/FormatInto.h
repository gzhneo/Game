/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FormatInto.h -- bounded, type-checked replacement for snprintf into a character buffer.
//
// The format string is checked at compile time; the result is truncated and terminated like
// snprintf's and the return value is the length the full text would have had. Writes into the
// caller's buffer without allocating, so it is usable on crash paths.
//
// No engine header: tested by EngineTests.

#pragma once

#ifndef ENGINE_FORMAT_INTO_H
#define ENGINE_FORMAT_INTO_H

#include <cstddef>
#include <format>
#include <utility>

namespace Engine
{

template <class... Args>
int formatInto(char *dst, std::size_t size, std::format_string<Args...> fmt, Args &&...args)
{
	if (dst == nullptr || size == 0)
		return static_cast<int>(std::formatted_size(fmt, std::forward<Args>(args)...));
	const auto result = std::format_to_n(dst, static_cast<std::ptrdiff_t>(size - 1), fmt, std::forward<Args>(args)...);
	*result.out = '\0';
	return static_cast<int>(result.size);
}

template <std::size_t N, class... Args>
int formatInto(char (&dst)[N], std::format_string<Args...> fmt, Args &&...args)
{
	return formatInto(dst, N, fmt, std::forward<Args>(args)...);
}

}

#endif
