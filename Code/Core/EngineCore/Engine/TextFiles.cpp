/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// TextFiles.cpp -- .csf and .str readers, see TextFiles.h.
//
// The .str reader follows the original GameTextManager parser step by step (line reads, quote
// handling, speech name after the quote), so valid files give the same strings; it reads from
// memory and stops at the end of the data instead of looping or writing past its buffers.

#include "Engine/TextFiles.h"

#include <cstring>

namespace Engine
{

namespace
{

// The original parser tested with iswspace on a signed char, which is true only for ASCII
// whitespace; bytes from 0x80 on are never spaces.
bool isSpace(int c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

std::uint32_t tag(char a, char b, char c, char d)
{
	return (std::uint32_t(std::uint8_t(a)) << 24) | (std::uint32_t(std::uint8_t(b)) << 16) | (std::uint32_t(std::uint8_t(c)) << 8) | std::uint8_t(d);
}

class CsfReader
{
public:
	CsfReader(const unsigned char *data, std::size_t size) : m_data(data), m_size(size) {}

	bool get32(std::uint32_t &value)
	{
		if (m_size - m_pos < 4)
			return false;
		value = std::uint32_t(m_data[m_pos]) | (std::uint32_t(m_data[m_pos + 1]) << 8) | (std::uint32_t(m_data[m_pos + 2]) << 16) | (std::uint32_t(m_data[m_pos + 3]) << 24);
		m_pos += 4;
		return true;
	}

	bool has(std::uint64_t bytes) const { return bytes <= m_size - m_pos; }
	const unsigned char *here() const { return m_data + m_pos; }
	void skip(std::size_t bytes) { m_pos += bytes; }
	std::size_t left() const { return m_size - m_pos; }

private:
	const unsigned char *m_data;
	std::size_t m_size;
	std::size_t m_pos = 0;
};

// Line and character source for the .str reader, like the File reads of the original.
class StrSource
{
public:
	StrSource(const char *data, std::size_t size) : m_data(data), m_size(size) {}

	int readChar() { return m_pos < m_size ? static_cast<unsigned char>(m_data[m_pos++]) : -1; }

	// Up to max characters up to (and without) the next '\n'; false if nothing was left to read.
	// A NUL ends the line as it ended the C string of the original.
	bool readLine(std::string &line, std::size_t max)
	{
		line.clear();
		bool any = false;
		bool ended = false;
		while (max > 0 && m_pos < m_size)
		{
			any = true;
			const char c = m_data[m_pos++];
			if (c == '\n')
				break;
			if (c == 0)
				ended = true;
			if (!ended)
				line.push_back(c);
			--max;
		}
		return any;
	}

private:
	const char *m_data;
	std::size_t m_size;
	std::size_t m_pos = 0;
};

void trim(std::string &s)
{
	std::size_t first = 0;
	while (first < s.size() && isSpace(static_cast<unsigned char>(s[first])))
		++first;
	std::size_t last = s.size();
	while (last > first && isSpace(static_cast<unsigned char>(s[last - 1])))
		--last;
	s = s.substr(first, last - first);
}

// Reads a quoted string whose opening quote is already consumed: first the rest of the current line
// (in), then further lines from the file, up to the closing quote. Newlines and other whitespace
// become spaces, escapes stay for translateCopy. After the quote the rest of the line may name a
// speech file ("= name"); a name ending in a digit gets an 'e' appended, as the original did.
void readToEndOfQuote(StrSource &file, const std::string &in, std::string &out, std::string &speech)
{
	std::size_t inPos = 0;
	bool inActive = true;
	auto next = [&]() -> int
	{
		if (inActive)
		{
			if (inPos < in.size() && in[inPos] != 0)
				return static_cast<unsigned char>(in[inPos++]);
			inActive = false;
		}
		return file.readChar();
	};

	out.clear();
	speech.clear();
	bool slash = false;
	bool outEnded = false;	// a NUL from the file ended the text of the original
	for (std::size_t count = TEXT_FILE_MAX_TEXT; count > 0; --count)
	{
		int ch = next();
		if (ch < 0)
			return;
		if (ch == '\n')
		{
			slash = false;
			ch = ' ';
		}
		else if (ch == '\\' && !slash)
			slash = true;
		else if (ch == '\\' && slash)
			slash = false;
		else if (ch == '"' && !slash)
			break;
		else
			slash = false;

		if (isSpace(ch))
			ch = ' ';
		if (ch == 0)
			outEnded = true;
		if (!outEnded)
			out.push_back(static_cast<char>(ch));
	}

	int state = 0;
	for (;;)
	{
		const int ch = next();
		if (ch == '\n' || ch < 0)
			break;
		if (state == 0)
		{
			if (isSpace(ch) || ch == '=')
				continue;
			state = 1;
		}
		if (state == 1)
		{
			if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_')
			{
				if (speech.size() < TEXT_FILE_MAX_LABEL - 1)
					speech.push_back(static_cast<char>(ch));
				continue;
			}
			state = 2;
		}
	}
	if (!speech.empty() && speech.back() >= '0' && speech.back() <= '9')
		speech.push_back('e');
}

}	// namespace

CsfError readCsf(const unsigned char *data, std::size_t size, int &version, int &language, std::vector<CsfEntry> &entries)
{
	entries.clear();
	version = 0;
	language = -1;
	CsfReader r(data, size);

	std::uint32_t id, ver, numLabels, numStrings, skip, langid;
	if (!r.get32(id) || !r.get32(ver) || !r.get32(numLabels) || !r.get32(numStrings) || !r.get32(skip) || !r.get32(langid))
		return size >= 4 && std::memcmp(data, " FSC", 4) != 0 ? CsfError::NotCsf : CsfError::TruncatedHeader;
	if (id != tag('C', 'S', 'F', ' '))
		return CsfError::NotCsf;
	version = static_cast<int>(ver);
	if (version >= 2)
		language = static_cast<int>(langid);
	// a label takes at least 12 bytes
	if (numLabels > 0x7FFFFFFF || numLabels > r.left() / 12)
		return CsfError::BadLabelCount;
	entries.reserve(numLabels);

	std::uint32_t block;
	while (r.get32(block))
	{
		if (block != tag('L', 'B', 'L', ' '))
			return CsfError::UnknownBlock;
		if (entries.size() >= numLabels)
			return CsfError::BadLabelCount;

		std::uint32_t strings, len;
		if (!r.get32(strings) || !r.get32(len))
			return CsfError::Truncated;
		if (len > TEXT_FILE_MAX_LABEL)
			return CsfError::TooLong;
		if (!r.has(len))
			return CsfError::Truncated;
		CsfEntry entry;
		entry.label.assign(reinterpret_cast<const char *>(r.here()), len);
		r.skip(len);
		// the label ended at its first NUL in the original
		entry.label.resize(std::strlen(entry.label.c_str()));

		for (std::uint32_t n = 0; n < strings; ++n)
		{
			std::uint32_t kind, chars;
			if (!r.get32(kind))
				return CsfError::Truncated;
			if (kind != tag('S', 'T', 'R', ' ') && kind != tag('S', 'T', 'R', 'W'))
				return CsfError::UnknownBlock;
			if (!r.get32(chars))
				return CsfError::Truncated;
			if (chars > TEXT_FILE_MAX_TEXT * 2)
				return CsfError::TooLong;
			if (!r.has(std::uint64_t(chars) * 2))
				return CsfError::Truncated;
			if (n == 0)
			{
				// stored inverted; the text ends at the first stored 0
				const unsigned char *p = r.here();
				for (std::uint32_t c = 0; c < chars; ++c)
				{
					const char16_t raw = static_cast<char16_t>(p[2 * c] | (p[2 * c + 1] << 8));
					if (raw == 0)
						break;
					entry.text.push_back(static_cast<char16_t>(~raw));
				}
			}
			r.skip(std::size_t(chars) * 2);

			if (kind == tag('S', 'T', 'R', 'W'))
			{
				std::uint32_t speechLen;
				if (!r.get32(speechLen))
					return CsfError::Truncated;
				if (speechLen > TEXT_FILE_MAX_LABEL)
					return CsfError::TooLong;
				if (!r.has(speechLen))
					return CsfError::Truncated;
				if (n == 0)
				{
					entry.speech.assign(reinterpret_cast<const char *>(r.here()), speechLen);
					entry.speech.resize(std::strlen(entry.speech.c_str()));
				}
				r.skip(speechLen);
			}
		}
		entries.push_back(std::move(entry));
	}
	// up to three bytes after the last label were ignored by the original as well
	return CsfError::None;
}

const char *csfErrorText(CsfError error)
{
	switch (error)
	{
		case CsfError::None: return "no error";
		case CsfError::NotCsf: return "not a CSF file";
		case CsfError::TruncatedHeader: return "header cut off";
		case CsfError::BadLabelCount: return "label count does not match the file";
		case CsfError::UnknownBlock: return "unknown block";
		case CsfError::Truncated: return "file cut off";
		case CsfError::TooLong: return "label, text or speech name too long";
	}
	return "unknown error";
}

bool readStr(const char *data, std::size_t size, std::vector<StrEntry> &entries)
{
	entries.clear();
	StrSource file(data, size);
	std::string line;
	// room for the '\n' the original appended to a quoted line
	const std::size_t maxLine = TEXT_FILE_MAX_TEXT - 1;

	while (file.readLine(line, maxLine))
	{
		trim(line);
		if (line.empty() || line.compare(0, 2, "//") == 0)
			continue;

		StrEntry entry;
		entry.label = line;
		for (;;)
		{
			if (!file.readLine(line, maxLine))
			{
				entries.push_back(std::move(entry));
				return false;
			}
			trim(line);
			if (!line.empty() && line[0] == '"')
			{
				std::string text, speech;
				readToEndOfQuote(file, line.substr(1) + '\n', text, speech);
				if (entry.hasText)
					++entry.extraTexts;
				else
				{
					entry.text = text;
					entry.speech = speech;
					entry.hasText = true;
				}
			}
			else if (line.size() == 3 && (line[0] | 0x20) == 'e' && (line[1] | 0x20) == 'n' && (line[2] | 0x20) == 'd')
				break;
		}
		entries.push_back(std::move(entry));
	}
	return true;
}

}	// namespace Engine
