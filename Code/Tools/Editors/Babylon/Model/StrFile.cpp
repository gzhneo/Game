/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Model/StrFile.h"

#include "Model/Text.h"

#include <cstdlib>

namespace Babylon
{

namespace
{

bool isSpace(int c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// The file as the original read it in text mode: CR LF becomes LF.
class Stream
{
public:
	explicit Stream(std::string_view data) : m_data(data) {}

	int get()
	{
		if (m_pos >= m_data.size())
			return -1;
		const char c = m_data[m_pos++];
		if (c == '\r' && m_pos < m_data.size() && m_data[m_pos] == '\n')
			return m_data[m_pos++];
		return static_cast<unsigned char>(c);
	}

	// One line without its line break; false at the end of the file.
	bool line(std::string &out)
	{
		if (m_pos >= m_data.size())
			return false;
		out.clear();
		for (int c = get(); c != -1 && c != '\n'; c = get())
			out.push_back(static_cast<char>(c));
		return true;
	}

private:
	std::string_view m_data;
	std::size_t m_pos = 0;
};

std::string trim(const std::string &s)
{
	std::size_t a = 0;
	while (a < s.size() && isSpace(static_cast<unsigned char>(s[a])))
		++a;
	std::size_t b = s.size();
	while (b > a && isSpace(static_cast<unsigned char>(s[b - 1])))
		--b;
	return s.substr(a, b - a);
}

struct Info
{
	std::u16string comment;
	std::u16string context;
	std::u16string speaker;
	std::u16string listener;
	std::u16string wave;
	int maxLen = 0;
};

// "// TOKEN: value": the token ends at a space or colon, the value starts after the next colon.
// A comment without a colon has no value; any line whose first word is a keyword counts.
void parseComment(const std::string &line, Info &info)
{
	std::size_t i = 0;
	while (i < line.size() && (line[i] == '/' || isSpace(static_cast<unsigned char>(line[i]))))
		++i;
	std::string token;
	while (i < line.size() && !isSpace(static_cast<unsigned char>(line[i])) && line[i] != ':' && token.size() < 255)
		token.push_back(line[i++]);
	while (i < line.size() && line[i] != ':')
		++i;
	if (i < line.size())
		++i;
	while (i < line.size() && isSpace(static_cast<unsigned char>(line[i])))
		++i;
	const std::string arg = line.substr(i);

	const auto value = [&arg]() {
		std::u16string s = fromLatin1(arg.substr(0, 2048));
		convertMetaChars(s);
		stripSpaces(s);
		return s;
	};
	if (equalNoCase(token, "COMMENT"))
		info.comment = value();
	else if (equalNoCase(token, "CONTEXT"))
		info.context = value();
	else if (equalNoCase(token, "SPEAKER"))
		info.speaker = value();
	else if (equalNoCase(token, "LISTENER"))
		info.listener = value();
	else if (equalNoCase(token, "MAXLEN"))
		info.maxLen = std::atoi(arg.c_str());
	else if (equalNoCase(token, "WAVE"))
		info.wave = value();
}

bool isComment(const std::string &line)
{
	return line.empty() || (line.size() >= 2 && line[0] == '/' && line[1] == '/');
}

struct QuoteResult
{
	std::string text;
	std::string wave;
	int newLines = 0;
	bool closed = true;
	bool strayQuote = false;	// another quote behind the closing one: a quote in the text without backslash
};

// Reads a text from behind its opening quote up to the closing one, continuing on the following
// lines of the file, then the speech file name up to the end of that line.
QuoteResult readQuote(Stream &file, const std::string &restOfLine)
{
	QuoteResult r;
	std::string in = restOfLine + "\n";
	std::size_t inPos = 0;
	bool fromLine = true;
	const auto next = [&]() -> int {
		if (fromLine)
		{
			if (inPos < in.size())
				return static_cast<unsigned char>(in[inPos++]);
			fromLine = false;
		}
		return file.get();
	};

	bool slash = false;
	bool lineStart = false;
	for (;;)
	{
		int ch = next();
		if (ch == -1)
		{
			r.closed = false;
			return r;
		}
		if (ch == '\n')
		{
			lineStart = true;
			if (!fromLine)
				++r.newLines;
			slash = false;
			ch = ' ';
		}
		else if (lineStart && (ch == '/' || isSpace(ch)))
			continue;
		else if (ch == '\\')
			slash = !slash;
		else if (ch == '"' && !slash)
			break;
		else
			slash = false;

		if (isSpace(ch))
			ch = ' ';
		else
			lineStart = false;
		r.text.push_back(static_cast<char>(ch));
	}

	int state = 0;
	for (;;)
	{
		const int ch = next();
		if (ch == '\n' || ch == -1)
		{
			if (!fromLine && ch == '\n')
				++r.newLines;
			break;
		}
		if (ch == '"')
			r.strayQuote = true;
		if (state == 0)
		{
			if (isSpace(ch) || ch == '=')
				continue;
			state = 1;
		}
		if (state == 1)
		{
			if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_')
				r.wave.push_back(static_cast<char>(ch));
			else
				state = 2;
		}
	}
	// The last letter names the language of the recording; it is added again per language.
	if (!r.wave.empty() && (r.wave.back() < '0' || r.wave.back() > '9'))
		r.wave.pop_back();
	return r;
}

} // namespace

bool readStrFile(std::string_view data, Database &db, std::vector<StrProblem> &problems)
{
	db.clear();
	db.nextId = -1;
	Stream file(data);
	Info global;
	int lineNumber = 0;
	std::string raw;

	while (file.line(raw))
	{
		++lineNumber;
		std::string line = trim(raw);
		if (isComment(line))
		{
			parseComment(line, global);
			continue;
		}

		Label label;
		label.name = fromLatin1(line);
		label.line = lineNumber;
		Info local = global;
		local.wave.clear();
		bool ended = false;
		while (!ended)
		{
			if (!file.line(raw))
			{
				db.labels.push_back(std::move(label));
				problems.push_back({lineNumber, true, "unexpected end of file: label without END"});
				return false;
			}
			++lineNumber;
			line = trim(raw);
			if (equalNoCase(line, "END"))
				ended = true;
			else if (isComment(line))
				parseComment(line, local);
			else if (line[0] == '"')
			{
				const int textLine = lineNumber;
				QuoteResult q = readQuote(file, line.substr(1));
				lineNumber += q.newLines;
				if (!q.closed)
				{
					db.labels.push_back(std::move(label));
					problems.push_back({textLine, true, "missing terminating quote"});
					return false;
				}
				if (q.strayQuote)
					problems.push_back({textLine, false, "quote inside the text without backslash: the game ends the text there (write \\\")"});
				Text text;
				text.text = fromLatin1(q.text);
				formatMetaString(text.text);
				text.wave = q.wave.empty() ? local.wave : fromLatin1(q.wave);
				text.line = textLine;
				label.texts.push_back(std::move(text));
			}
			else
				problems.push_back({lineNumber, false, "line ignored (neither text, comment nor END): " + line.substr(0, 60)});
		}
		label.comment = local.comment;
		label.context = local.context;
		label.speaker = local.speaker;
		label.listener = local.listener;
		label.maxLen = local.maxLen;
		db.labels.push_back(std::move(label));
	}
	db.modified = false;
	return true;
}

namespace
{

// A quote in the text must be escaped, or the game's reader ends the text there.
std::string quoted(const std::u16string &text, int *lost)
{
	std::string out = "\"";
	bool slash = false;
	for (const char c : toLatin1(text, lost))
	{
		if (c == '"' && !slash)
			out += '\\';
		slash = c == '\\' && !slash;
		out += c;
	}
	// a backslash at the end would escape the closing quote
	if (slash)
		out += '\\';
	return out + "\"";
}

} // namespace

std::string writeStrFile(const std::vector<StrOutLabel> &labels, std::string_view generatedBy, std::string_view generatedOn, int *lost)
{
	std::string out;
	out += "// Generated by ";
	out += generatedBy;
	out += "\r\n// Generated on ";
	out += generatedOn;
	out += "\r\n\r\n\r\n";
	for (const StrOutLabel &label : labels)
	{
		out += "\r\n\r\n" + toLatin1(label.name, lost) + "\r\n";
		for (const StrOutText &text : label.texts)
			out += quoted(text.text, lost) + " " + toLatin1(text.wave, lost) + "\r\n";
		out += "END\r\n";
	}
	return out;
}

} // namespace Babylon
