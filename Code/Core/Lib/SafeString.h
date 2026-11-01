/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: SafeString.h /////////////////////////////////////////////////////////
//
// Bounded replacements for strcpy/strcat.
//
// Both take the destination as an array char[N], so the size comes from the type. A pointer
// destination does not compile on purpose: bounding is only done where the size is known.
//
// If the text fits, the result is the same as strcpy/strcat. Otherwise it is truncated and always
// terminated. Overlapping source and destination (e.g. strcpy(buf, buf + n)) is handled by memmove.
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef SAFE_STRING_H
#define SAFE_STRING_H

#include <cstddef>
#include <cstring>
#include <cwchar>

// strcpy with the size taken from the array type: copies at most N-1 chars, always terminates.
// A null source counts as an empty string in all four functions (parsers pass strtok results).
template <std::size_t N>
inline char *safe_strcpy(char (&dest)[N], const char *src)
{
	static_assert(N > 0, "safe_strcpy: empty array");
	const std::size_t len = src ? strnlen(src, N - 1) : 0;
	std::memmove(dest, src, len);
	dest[len] = '\0';
	return dest;
}

// strcat with the size taken from the array type: appends while there is room, always terminates.
// If the destination is not terminated within N chars, it is left unchanged.
template <std::size_t N>
inline char *safe_strcat(char (&dest)[N], const char *src)
{
	static_assert(N > 0, "safe_strcat: empty array");
	const std::size_t used = strnlen(dest, N);
	if (used >= N)
		return dest;
	const std::size_t len = src ? strnlen(src, N - 1 - used) : 0;
	std::memmove(dest + used, src, len);
	dest[used + len] = '\0';
	return dest;
}

// wcscpy/wcscat counterparts of the two functions above.
template <std::size_t N>
inline wchar_t *safe_wcscpy(wchar_t (&dest)[N], const wchar_t *src)
{
	static_assert(N > 0, "safe_wcscpy: empty array");
	const std::size_t len = src ? wcsnlen(src, N - 1) : 0;
	std::wmemmove(dest, src, len);
	dest[len] = L'\0';
	return dest;
}

template <std::size_t N>
inline wchar_t *safe_wcscat(wchar_t (&dest)[N], const wchar_t *src)
{
	static_assert(N > 0, "safe_wcscat: empty array");
	const std::size_t used = wcsnlen(dest, N);
	if (used >= N)
		return dest;
	const std::size_t len = src ? wcsnlen(src, N - 1 - used) : 0;
	std::wmemmove(dest + used, src, len);
	dest[used + len] = L'\0';
	return dest;
}

#endif // SAFE_STRING_H
