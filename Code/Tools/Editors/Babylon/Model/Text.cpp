/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Model/Text.h"

#include <cstdint>

namespace Babylon
{

namespace
{

const char16_t NO_CHAR = 0xFFFF;

void appendUtf8(std::string &out, char32_t c)
{
	if (c < 0x80)
		out.push_back(static_cast<char>(c));
	else if (c < 0x800)
	{
		out.push_back(static_cast<char>(0xC0 | (c >> 6)));
		out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
	}
	else if (c < 0x10000)
	{
		out.push_back(static_cast<char>(0xE0 | (c >> 12)));
		out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
	}
	else
	{
		out.push_back(static_cast<char>(0xF0 | (c >> 18)));
		out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
	}
}

void appendUtf16(std::u16string &out, char32_t c)
{
	if (c < 0x10000)
		out.push_back(static_cast<char16_t>(c));
	else
	{
		c -= 0x10000;
		out.push_back(static_cast<char16_t>(0xD800 + (c >> 10)));
		out.push_back(static_cast<char16_t>(0xDC00 + (c & 0x3FF)));
	}
}

char16_t fold(char16_t c)
{
	return (c >= u'A' && c <= u'Z') ? static_cast<char16_t>(c - u'A' + u'a') : c;
}

bool isFormatTypeChar(char16_t c)
{
	for (const char *p = "cCdiouxXeEfgGnpsS"; *p; ++p)
		if (static_cast<char16_t>(*p) == c)
			return true;
	return false;
}

bool isLetter(char16_t c)
{
	return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}

// Reverses [first, last] and swaps the case of the ends if the word started with a capital.
void reverseWord(std::u16string &s, std::size_t first, std::size_t last)
{
	bool firstPair = true;
	while (first < last)
	{
		char16_t f = s[first];
		char16_t l = s[last];
		if (firstPair)
		{
			if (f >= u'A' && f <= u'Z' && l >= u'a' && l <= u'z')
			{
				f = static_cast<char16_t>(f - u'A' + u'a');
				l = static_cast<char16_t>(l - u'a' + u'A');
			}
			firstPair = false;
		}
		s[last--] = f;
		s[first++] = l;
	}
}

} // namespace

std::string toUtf8(std::u16string_view text)
{
	std::string out;
	out.reserve(text.size());
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		char32_t c = text[i];
		if (c >= 0xD800 && c < 0xDC00 && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000)
		{
			c = 0x10000 + ((c - 0xD800) << 10) + (text[i + 1] - 0xDC00);
			++i;
		}
		else if (c >= 0xD800 && c < 0xE000)
			c = 0xFFFD;
		appendUtf8(out, c);
	}
	return out;
}

std::u16string fromUtf8(std::string_view text, bool *ok)
{
	std::u16string out;
	out.reserve(text.size());
	bool good = true;
	std::size_t i = 0;
	while (i < text.size())
	{
		const auto b = static_cast<std::uint8_t>(text[i]);
		int extra = 0;
		char32_t c = 0;
		if (b < 0x80)
			c = b;
		else if ((b & 0xE0) == 0xC0)
		{
			extra = 1;
			c = b & 0x1Fu;
		}
		else if ((b & 0xF0) == 0xE0)
		{
			extra = 2;
			c = b & 0x0Fu;
		}
		else if ((b & 0xF8) == 0xF0)
		{
			extra = 3;
			c = b & 0x07u;
		}
		else
		{
			good = false;
			out.push_back(0xFFFD);
			++i;
			continue;
		}
		if (i + static_cast<std::size_t>(extra) >= text.size())
		{
			good = false;
			out.push_back(0xFFFD);
			break;
		}
		bool valid = true;
		for (int k = 1; k <= extra; ++k)
		{
			const auto t = static_cast<std::uint8_t>(text[i + static_cast<std::size_t>(k)]);
			if ((t & 0xC0) != 0x80)
			{
				valid = false;
				break;
			}
			c = (c << 6) | (t & 0x3Fu);
		}
		const bool overlong = (extra == 1 && c < 0x80) || (extra == 2 && c < 0x800) || (extra == 3 && c < 0x10000);
		if (!valid || overlong || c > 0x10FFFF || (c >= 0xD800 && c < 0xE000))
		{
			good = false;
			out.push_back(0xFFFD);
			++i;
			continue;
		}
		appendUtf16(out, c);
		i += static_cast<std::size_t>(extra) + 1;
	}
	if (ok)
		*ok = good;
	return out;
}

std::u16string fromLatin1(std::string_view text)
{
	std::u16string out;
	out.reserve(text.size());
	for (const char c : text)
		out.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
	return out;
}

std::string toLatin1(std::u16string_view text, int *lost)
{
	std::string out;
	out.reserve(text.size());
	for (const char16_t c : text)
	{
		if (c > 0xFF)
		{
			out.push_back('?');
			if (lost)
				++*lost;
		}
		else
			out.push_back(static_cast<char>(c));
	}
	return out;
}

std::u16string ascii(std::string_view text)
{
	return fromLatin1(text);
}

int compareNoCase(std::u16string_view a, std::u16string_view b)
{
	const std::size_t n = a.size() < b.size() ? a.size() : b.size();
	for (std::size_t i = 0; i < n; ++i)
	{
		const char16_t x = fold(a[i]);
		const char16_t y = fold(b[i]);
		if (x != y)
			return x < y ? -1 : 1;
	}
	if (a.size() == b.size())
		return 0;
	return a.size() < b.size() ? -1 : 1;
}

bool equalNoCase(std::string_view a, std::string_view b)
{
	return compareNoCase(fromLatin1(a), fromLatin1(b)) == 0;
}

void stripSpaces(std::u16string &text)
{
	std::u16string out;
	out.reserve(text.size());
	char16_t last = NO_CHAR;
	bool skipAll = true;
	for (const char16_t ch : text)
	{
		if (ch == u' ' && (last == u' ' || skipAll))
			continue;
		if (ch == u'\n' || ch == u'\t')
		{
			if (last == u' ')
				out.pop_back();
			skipAll = true;
			out.push_back(ch);
			last = ch;
			continue;
		}
		out.push_back(ch);
		last = ch;
		skipAll = false;
	}
	if (last == u' ' && !out.empty())
		out.pop_back();
	text = std::move(out);
}

void convertMetaChars(std::u16string &text)
{
	std::u16string out;
	out.reserve(text.size());
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		char16_t ch = text[i];
		if (ch == u'\\')
		{
			if (i + 1 >= text.size())
				break;
			ch = text[++i];
			if (ch == u'n' || ch == u'N')
				ch = u'\n';
			else if (ch == u't' || ch == u'T')
				ch = u'\t';
		}
		out.push_back(ch);
	}
	text = std::move(out);
}

void formatMetaString(std::u16string &text)
{
	if (text.empty())
		return;
	std::u16string out;
	out.reserve(text.size() * 2);
	char16_t last = NO_CHAR;
	bool slash = false;
	for (std::size_t i = 0; i < text.size();)
	{
		const char16_t ch = text[i++];
		if (ch == u' ' && last == u' ')
			continue;
		if (ch == u'\\')
		{
			slash = !slash;
			// The original keeps 'slash' set after an escaped line break, so a backslash right
			// behind it counts as an ordinary character.
			if (slash && i < text.size() && (text[i] == u'n' || text[i] == u't'))
			{
				const char16_t esc = text[i++];
				if (last != u' ')
					out.push_back(u' ');
				out.push_back(u'\\');
				out.push_back(esc);
				out.push_back(u' ');
				last = u' ';
				continue;
			}
		}
		else
			slash = false;
		out.push_back(ch);
		last = ch;
	}
	if (last == u' ' && !out.empty())
		out.pop_back();
	text = std::move(out);
}

std::vector<std::u16string> formatSpecifiers(std::u16string_view text)
{
	std::vector<std::u16string> specs;
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] == u'\\')
		{
			++i;
			continue;
		}
		if (text[i] != u'%')
			continue;
		if (i + 1 < text.size() && text[i + 1] == u'%')
		{
			++i;
			continue;
		}
		std::size_t j = i + 1;
		// flags, width, precision and length end at the conversion; a bare '%' at the end or before
		// a space stays a conversion of its own so a lost or added one is still noticed
		while (j < text.size() && !isFormatTypeChar(text[j]) && text[j] != u' ' && text[j] != u'%' && j - i < 16)
			++j;
		const std::size_t end = (j < text.size() && isFormatTypeChar(text[j])) ? j + 1 : j;
		specs.emplace_back(text.substr(i, end - i));
		i = end - 1;
	}
	return specs;
}

bool sameFormat(std::u16string_view translation, std::u16string_view original)
{
	return formatSpecifiers(translation) == formatSpecifiers(original);
}

std::u16string jabber(std::u16string_view in)
{
	std::u16string b;
	b.reserve(in.size());
	std::size_t firstLetter = std::u16string::npos;
	std::size_t i = 0;
	while (i < in.size())
	{
		char16_t ch = in[i++];
		if (!isLetter(ch))
		{
			if (firstLetter != std::u16string::npos)
			{
				reverseWord(b, firstLetter, b.size() - 1);
				firstLetter = std::u16string::npos;
			}
			b.push_back(ch);
			if (ch == u'\\' && i < in.size())
				b.push_back(in[i++]);
			if (ch == u'%')
			{
				while (i < in.size())
				{
					ch = in[i++];
					b.push_back(ch);
					if (isFormatTypeChar(ch) || ch == u'%')
						break;
				}
			}
		}
		else
		{
			if (firstLetter == std::u16string::npos)
				firstLetter = b.size();
			b.push_back(ch);
		}
	}
	if (firstLetter != std::u16string::npos)
		reverseWord(b, firstLetter, b.size() - 1);
	return b;
}

std::u16string toU16(int value)
{
	return ascii(std::to_string(value));
}

} // namespace Babylon
