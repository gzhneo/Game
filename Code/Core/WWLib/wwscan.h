/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Bounds-checked replacements for the few sscanf formats the INI reader used: "%d", "%x", "%f" and
// comma separated lists of them. A value that is not there leaves the target untouched (like a failed
// sscanf conversion), and reading stops at the first failure.

#pragma once

#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <initializer_list>

namespace WWScan
{

inline void SkipSpace(const char *& p)
{
	while (*p != '\0' && std::isspace(static_cast<unsigned char>(*p))) {
		++p;
	}
}

// "%d": leading white space, optional sign, decimal digits.
inline bool Int(const char *& p, int & out)
{
	const char * q = p;
	SkipSpace(q);
	bool negative = false;
	if (*q == '-' || *q == '+') {
		negative = (*q == '-');
		++q;
	}
	unsigned value = 0;
	const auto result = std::from_chars(q, q + std::strlen(q), value, 10);
	if (result.ec != std::errc() || (!negative && value > 0x7FFFFFFFu) || (negative && value > 0x80000000u)) {
		return false;
	}
	out = negative ? static_cast<int>(0u - value) : static_cast<int>(value);
	p = result.ptr;
	return true;
}

// "%x": leading white space, optional sign, optional 0x, hexadecimal digits (wraps like unsigned).
inline bool Hex(const char *& p, int & out)
{
	const char * q = p;
	SkipSpace(q);
	bool negative = false;
	if (*q == '-' || *q == '+') {
		negative = (*q == '-');
		++q;
	}
	if (q[0] == '0' && (q[1] == 'x' || q[1] == 'X') && std::isxdigit(static_cast<unsigned char>(q[2]))) {
		q += 2;
	}
	unsigned value = 0;
	const auto result = std::from_chars(q, q + std::strlen(q), value, 16);
	if (result.ec != std::errc()) {
		return false;
	}
	out = static_cast<int>(negative ? 0u - value : value);
	p = result.ptr;
	return true;
}

// "%f": leading white space, optional sign, decimal or exponent notation.
inline bool Float(const char *& p, float & out)
{
	const char * q = p;
	SkipSpace(q);
	const char * start = q;
	if (*q == '+') {
		start = ++q;
	}
	float value = 0.0f;
	const auto result = std::from_chars(start, start + std::strlen(start), value, std::chars_format::general);
	if (result.ec != std::errc()) {
		return false;
	}
	out = value;
	p = result.ptr;
	return true;
}

inline bool Literal(const char *& p, char c)
{
	if (*p != c) {
		return false;
	}
	++p;
	return true;
}

// "%d,%d,...": returns the number of values read.
inline int Ints(const char * text, std::initializer_list<int *> targets)
{
	const char * p = text;
	int count = 0;
	for (int * target : targets) {
		if (count > 0 && !Literal(p, ',')) {
			break;
		}
		if (!Int(p, *target)) {
			break;
		}
		++count;
	}
	return count;
}

// "%f,%f,...": returns the number of values read.
inline int Floats(const char * text, std::initializer_list<float *> targets)
{
	const char * p = text;
	int count = 0;
	for (float * target : targets) {
		if (count > 0 && !Literal(p, ',')) {
			break;
		}
		if (!Float(p, *target)) {
			break;
		}
		++count;
	}
	return count;
}

}
