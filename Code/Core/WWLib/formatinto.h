/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Bounded, type-checked replacement for snprintf into a character buffer: the format string is
// checked at compile time and the result is truncated and terminated like snprintf's.
// Use "{:d}" for integers (also prints bool as 0/1) and .str() for string classes.

#pragma once

#include <cstddef>
#include <format>
#include <utility>

namespace WWFormat
{

// Writes into dst[0..size), always terminates (size > 0) and returns the length the full result
// would have had, like snprintf.
template <class... Args>
int Into(char * dst, std::size_t size, std::format_string<Args...> fmt, Args &&... args)
{
	if (size == 0) {
		return static_cast<int>(std::formatted_size(fmt, std::forward<Args>(args)...));
	}
	const auto result = std::format_to_n(dst, static_cast<std::ptrdiff_t>(size - 1), fmt, std::forward<Args>(args)...);
	*result.out = '\0';
	return static_cast<int>(result.size);
}

template <std::size_t N, class... Args>
int Into(char (&dst)[N], std::format_string<Args...> fmt, Args &&... args)
{
	return Into(dst, N, fmt, std::forward<Args>(args)...);
}

}
