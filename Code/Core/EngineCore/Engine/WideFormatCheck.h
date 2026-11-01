/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WideFormatCheck.h -- checks a constant wide printf format string against the argument types while
// compiling. GCC and clang have no format attribute for wchar_t formats, and MSVC checks none, so
// UnicodeString::format with a literal format goes through Engine::CheckedWideFormat; the output
// still comes from _vsnwprintf.
//
// The rules are those of the Microsoft wide functions (legacy, also in the UCRT): %s and %ls take a
// wchar_t string, %hs and %S a char string, %c a character; d i u o x X c take an integer of at most
// 32 bits (ll and I64 one of 64 bits), f e g a (also capitalised) a floating point value, p any
// pointer, '*' as width or precision an int. %n is rejected.
//
// A mismatch stops the compilation in the constructor; the message names one of the functions
// below. Formats that are only known at run time (texts from the string tables) are not checked.

#pragma once

#ifndef ENGINE_WIDE_FORMAT_CHECK_H
#define ENGINE_WIDE_FORMAT_CHECK_H

#include <cstddef>
#include <type_traits>

namespace Engine
{
	namespace WideFormat
	{
		enum class Kind { Int, Int64, Double, WideString, NarrowString, Pointer, Other };

		template <typename T>
		consteval Kind kindOf()
		{
			using U = std::remove_cv_t<T>;
			if constexpr (std::is_same_v<U, bool> || std::is_integral_v<U> || std::is_enum_v<U>)
				return sizeof(U) <= 4 ? Kind::Int : Kind::Int64;
			else if constexpr (std::is_floating_point_v<U>)
				return Kind::Double;
			else if constexpr (std::is_null_pointer_v<U>)
				return Kind::Pointer;
			else if constexpr (std::is_pointer_v<U>)
			{
				using P = std::remove_cv_t<std::remove_pointer_t<U>>;
				if constexpr (std::is_same_v<P, wchar_t>)
					return Kind::WideString;
				else if constexpr (std::is_same_v<P, char>)
					return Kind::NarrowString;
				else
					return Kind::Pointer;
			}
			else
				return Kind::Other;
		}

		enum class Result { Ok, TooFewArguments, TooManyArguments, WrongType, UnknownConversion };

		/// Walks the format and compares each conversion with the next argument kind.
		consteval Result check(const wchar_t *format, const Kind *kinds, std::size_t count)
		{
			std::size_t next = 0;
			auto take = [&](bool fits) -> Result {
				if (next >= count)
					return Result::TooFewArguments;
				if (!fits)
					return Result::WrongType;
				++next;
				return Result::Ok;
			};
			for (const wchar_t *p = format; *p; ++p)
			{
				if (*p != L'%')
					continue;
				++p;
				if (*p == L'%')
					continue;
				while (*p == L'-' || *p == L'+' || *p == L' ' || *p == L'#' || *p == L'0')
					++p;
				if (*p == L'*')
				{
					Result r = take(next < count && kinds[next] == Kind::Int);
					if (r != Result::Ok)
						return r;
					++p;
				}
				while (*p >= L'0' && *p <= L'9')
					++p;
				if (*p == L'.')
				{
					++p;
					if (*p == L'*')
					{
						Result r = take(next < count && kinds[next] == Kind::Int);
						if (r != Result::Ok)
							return r;
						++p;
					}
					while (*p >= L'0' && *p <= L'9')
						++p;
				}
				// length: h hh l ll w I I32 I64 z
				bool narrow = false, wide = false, wide64 = false;
				if (*p == L'h') { narrow = true; ++p; if (*p == L'h') ++p; }
				else if (*p == L'l') { wide = true; ++p; if (*p == L'l') { wide64 = true; ++p; } }
				else if (*p == L'w') { wide = true; ++p; }
				else if (*p == L'z') { wide64 = true; ++p; }
				else if (*p == L'I')
				{
					++p;
					if (p[0] == L'6' && p[1] == L'4') { wide64 = true; p += 2; }
					else if (p[0] == L'3' && p[1] == L'2') { p += 2; }
					else wide64 = true;	// I alone: pointer-sized, 64 bits on x64
				}
				const Kind kind = next < count ? kinds[next] : Kind::Other;
				Result r = Result::Ok;
				switch (*p)
				{
					case L'd': case L'i': case L'u': case L'o': case L'x': case L'X': case L'c': case L'C':
						r = take(kind == (wide64 ? Kind::Int64 : Kind::Int));
						break;
					case L'f': case L'F': case L'e': case L'E': case L'g': case L'G': case L'a': case L'A':
						r = take(kind == Kind::Double);
						break;
					case L's':
						r = take(kind == (narrow ? Kind::NarrowString : Kind::WideString));
						break;
					case L'S':
						r = take(kind == (wide ? Kind::WideString : Kind::NarrowString));
						break;
					case L'p':
						r = take(kind == Kind::Pointer || kind == Kind::WideString || kind == Kind::NarrowString);
						break;
					default:
						return Result::UnknownConversion;
				}
				if (r != Result::Ok)
					return r;
				if (!*p)
					break;
			}
			return next == count ? Result::Ok : Result::TooManyArguments;
		}

		template <typename... Args>
		consteval Result checkFor(const wchar_t *format)
		{
			constexpr Kind kinds[sizeof...(Args) + 1] = { kindOf<Args>()..., Kind::Other };
			return check(format, kinds, sizeof...(Args));
		}

		// Not constexpr on purpose: calling one of them while compiling stops with its name.
		void wide_format_has_too_few_arguments();
		void wide_format_has_too_many_arguments();
		void wide_format_argument_does_not_match_its_conversion();
		void wide_format_has_an_unknown_conversion();
	}

	/// A wide format string checked against Args while compiling; only constant formats convert.
	template <typename... Args>
	struct CheckedWideFormat
	{
		const wchar_t *str;

		consteval CheckedWideFormat(const wchar_t *format) : str(format)
		{
			switch (WideFormat::checkFor<Args...>(format))
			{
				case WideFormat::Result::Ok: break;
				case WideFormat::Result::TooFewArguments: WideFormat::wide_format_has_too_few_arguments(); break;
				case WideFormat::Result::TooManyArguments: WideFormat::wide_format_has_too_many_arguments(); break;
				case WideFormat::Result::WrongType: WideFormat::wide_format_argument_does_not_match_its_conversion(); break;
				case WideFormat::Result::UnknownConversion: WideFormat::wide_format_has_an_unknown_conversion(); break;
			}
		}
	};
}

#endif
