/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Model/CsfFile.h"

#include "Model/Text.h"

namespace Babylon
{

namespace
{

constexpr std::uint32_t tag(char a, char b, char c, char d)
{
	return (static_cast<std::uint32_t>(static_cast<unsigned char>(a)) << 24) | (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 16) |
		(static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 8) | static_cast<std::uint32_t>(static_cast<unsigned char>(d));
}

const int CSF_VERSION = 3;

void put32(std::vector<std::uint8_t> &out, std::uint32_t v)
{
	for (int k = 0; k < 4; ++k)
		out.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
}

void set32(std::vector<std::uint8_t> &out, std::size_t at, std::uint32_t v)
{
	for (int k = 0; k < 4; ++k)
		out[at + static_cast<std::size_t>(k)] = static_cast<std::uint8_t>(v >> (8 * k));
}

} // namespace

bool writeCsfFile(const std::vector<StrOutLabel> &labels, const LangInfo &lang, std::vector<std::uint8_t> &out, std::string &error)
{
	out.clear();
	put32(out, tag('C', 'S', 'F', ' '));
	put32(out, CSF_VERSION);
	put32(out, 0);
	put32(out, 0);
	put32(out, 0);
	put32(out, static_cast<std::uint32_t>(lang.id));
	std::uint32_t numLabels = 0;
	std::uint32_t numStrings = 0;

	for (const StrOutLabel &label : labels)
	{
		const std::string name = toLatin1(label.name);
		if (name.empty())
		{
			error = "a label has no name";
			out.clear();
			return false;
		}
		put32(out, tag('L', 'B', 'L', ' '));
		put32(out, static_cast<std::uint32_t>(label.texts.size()));
		put32(out, static_cast<std::uint32_t>(name.size()));
		out.insert(out.end(), name.begin(), name.end());
		++numLabels;

		for (const StrOutText &text : label.texts)
		{
			std::u16string s = text.text;
			stripSpaces(s);
			convertMetaChars(s);
			const std::string wave = toLatin1(text.wave);
			put32(out, wave.empty() ? tag('S', 'T', 'R', ' ') : tag('S', 'T', 'R', 'W'));
			put32(out, static_cast<std::uint32_t>(s.size()));
			for (const char16_t c : s)
			{
				const auto inv = static_cast<char16_t>(~c);
				out.push_back(static_cast<std::uint8_t>(inv & 0xFF));
				out.push_back(static_cast<std::uint8_t>(inv >> 8));
			}
			if (!wave.empty())
			{
				put32(out, static_cast<std::uint32_t>(wave.size() + 1));
				out.insert(out.end(), wave.begin(), wave.end());
				out.push_back(static_cast<std::uint8_t>(lang.character[0]));
			}
			++numStrings;
		}
	}
	set32(out, 8, numLabels);
	set32(out, 12, numStrings);
	return true;
}

} // namespace Babylon
