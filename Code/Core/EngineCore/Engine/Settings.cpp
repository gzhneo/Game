/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Settings.cpp -- file format, settings object and import of the old files, see Settings.h.

#include "Engine/Settings.h"
#include "Engine/PreferenceFile.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <set>
#include <unordered_map>

namespace Engine
{
namespace Settings
{

namespace
{

const char *const SPACE = " \t\r\n\v\f";

std::string_view trimmed(std::string_view text)
{
	const std::size_t first = text.find_first_not_of(SPACE);
	if (first == std::string_view::npos)
		return {};
	return text.substr(first, text.find_last_not_of(SPACE) - first + 1);
}

char lowerAscii(char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lower(std::string_view text)
{
	std::string result(text);
	for (char &c : result)
		c = lowerAscii(c);
	return result;
}

bool equalsNoCase(std::string_view a, std::string_view b)
{
	if (a.size() != b.size())
		return false;
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		if (lowerAscii(a[i]) != lowerAscii(b[i]))
			return false;
	}
	return true;
}

/// "Section.Key" -> section, key.
void splitId(std::string_view id, std::string_view &section, std::string_view &key)
{
	const std::size_t dot = id.find('.');
	section = id.substr(0, dot);
	key = dot == std::string_view::npos ? std::string_view() : id.substr(dot + 1);
}

const char *fileName(File file)
{
	return file == File::Settings ? "Settings.ini" : "State.ini";
}

const char *legacyName(Legacy legacy)
{
	switch (legacy)
	{
		case Legacy::Options:	return "Options.ini";
		case Legacy::Network:	return "Network.ini";
		case Legacy::Skirmish:	return "Skirmish.ini";
		case Legacy::None:		break;
	}
	return "";
}

bool parseLong(std::string_view text, long long &value)
{
	text = trimmed(text);
	if (!text.empty() && text.front() == '+')
		text.remove_prefix(1);
	const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
	return result.ec == std::errc() && result.ptr == text.data() + text.size() && !text.empty();
}

bool parseDouble(std::string_view text, double &value)
{
	text = trimmed(text);
	if (!text.empty() && text.front() == '+')
		text.remove_prefix(1);
	const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
	return result.ec == std::errc() && result.ptr == text.data() + text.size() && !text.empty() && std::isfinite(value);
}

bool hasRange(const Definition &definition)
{
	return definition.minimum < definition.maximum;
}

void appendUtf8(std::string &out, unsigned codePoint)
{
	if (codePoint < 0x80)
		out += static_cast<char>(codePoint);
	else if (codePoint < 0x800)
	{
		out += static_cast<char>(0xC0 | (codePoint >> 6));
		out += static_cast<char>(0x80 | (codePoint & 0x3F));
	}
	else if (codePoint < 0x10000)
	{
		out += static_cast<char>(0xE0 | (codePoint >> 12));
		out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (codePoint & 0x3F));
	}
	else
	{
		out += static_cast<char>(0xF0 | (codePoint >> 18));
		out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (codePoint & 0x3F));
	}
}

int hexDigit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return 0;	// like QuotedPrintable.cpp
}

/// Bytes of the engine's quoted-printable form: '_' and two hex digits, see QuotedPrintable.cpp.
std::string decodeQuotedBytes(std::string_view text)
{
	std::string out;
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] != '_')
		{
			out += text[i];
			continue;
		}
		if (i + 1 >= text.size())
			break;		// ends with the escape character
		int value = hexDigit(text[++i]);
		if (i + 1 < text.size())
			value = (value << 4) | hexDigit(text[++i]);
		out += static_cast<char>(value);
	}
	return out;
}

/// The engine writes quoted-printable with letters, digits and '_' only; anything else means the
/// value was written as it is (Skirmish.ini wrote the map that way before).
bool looksQuoted(std::string_view text)
{
	return std::all_of(text.begin(), text.end(), [](char c)
	{
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
	});
}

}	// namespace

// ---------------------------------------------------------------------------------------------
// Schema lookup and values

const Definition *findDefinition(std::string_view id)
{
	for (const Definition &definition : schema())
	{
		if (equalsNoCase(definition.id, id))
			return &definition;
	}
	return nullptr;
}

bool parseBool(std::string_view text, bool &value)
{
	text = trimmed(text);
	if (equalsNoCase(text, "yes") || equalsNoCase(text, "true") || equalsNoCase(text, "on") || text == "1")
		value = true;
	else if (equalsNoCase(text, "no") || equalsNoCase(text, "false") || equalsNoCase(text, "off") || text == "0")
		value = false;
	else
		return false;
	return true;
}

bool normalizeValue(const Definition &definition, std::string_view text, std::string &normalized, bool *clamped)
{
	if (clamped != nullptr)
		*clamped = false;
	text = trimmed(text);
	switch (definition.type)
	{
		case Type::Bool:
		{
			bool value = false;
			if (!parseBool(text, value))
				return false;
			normalized = value ? "yes" : "no";
			return true;
		}
		case Type::Int:
		{
			long long value = 0;
			if (!parseLong(text, value))
				return false;
			if (hasRange(definition))
			{
				const long long low = static_cast<long long>(definition.minimum);
				const long long high = static_cast<long long>(definition.maximum);
				const long long kept = std::clamp(value, low, high);
				if (kept != value && clamped != nullptr)
					*clamped = true;
				value = kept;
			}
			normalized = std::to_string(value);
			return true;
		}
		case Type::Float:
		{
			double value = 0.0;
			if (!parseDouble(text, value))
				return false;
			if (hasRange(definition))
			{
				const double kept = std::clamp(value, definition.minimum, definition.maximum);
				if (kept != value && clamped != nullptr)
					*clamped = true;
				value = kept;
			}
			char buffer[64];
			const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
			normalized.assign(buffer, result.ptr);
			return true;
		}
		case Type::Enum:
		{
			std::string_view list = definition.choices != nullptr ? definition.choices : "";
			while (!list.empty())
			{
				const std::size_t bar = list.find('|');
				const std::string_view choice = list.substr(0, bar);
				if (equalsNoCase(choice, text))
				{
					normalized = std::string(choice);
					return true;
				}
				list = bar == std::string_view::npos ? std::string_view() : list.substr(bar + 1);
			}
			return false;
		}
		case Type::Text:
			normalized = std::string(text);
			return true;
		case Type::Resolution:
		{
			const std::size_t gap = text.find_first_of(" \tx");
			long long width = 0, height = 0;
			if (gap == std::string_view::npos || !parseLong(text.substr(0, gap), width)
				|| !parseLong(text.substr(gap + 1), height) || width < 1 || height < 1
				|| width > 65535 || height > 65535)
				return false;
			normalized = std::to_string(width) + " " + std::to_string(height);
			return true;
		}
		case Type::List:
		{
			normalized.clear();
			std::size_t pos = 0;
			while (pos <= text.size())
			{
				std::size_t end = text.find(';', pos);
				if (end == std::string_view::npos)
					end = text.size();
				const std::string_view entry = trimmed(text.substr(pos, end - pos));
				if (!entry.empty())
				{
					if (!normalized.empty())
						normalized += ';';
					normalized += entry;
				}
				pos = end + 1;
			}
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Document

void Document::parse(std::string_view text, std::vector<Problem> *problems)
{
	m_lines.clear();
	if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB
		&& static_cast<unsigned char>(text[2]) == 0xBF)
		text.remove_prefix(3);
	if (text.find('\n') != std::string_view::npos)
		m_newline = text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";

	std::string section;
	std::size_t pos = 0;
	int number = 0;
	while (pos < text.size())
	{
		std::size_t end = text.find('\n', pos);
		if (end == std::string_view::npos)
			end = text.size();
		std::string_view raw = text.substr(pos, end - pos);
		pos = end + 1;
		++number;
		if (!raw.empty() && raw.back() == '\r')
			raw.remove_suffix(1);

		Line line;
		line.raw = std::string(raw);
		const std::string_view body = trimmed(raw);
		if (body.empty())
			line.kind = Kind::Blank;
		else if (body.front() == ';' || body.front() == '#')
			line.kind = Kind::Comment;
		else if (body.front() == '[' && body.back() == ']')
		{
			line.kind = Kind::Section;
			section = std::string(trimmed(body.substr(1, body.size() - 2)));
			line.section = section;
		}
		else if (body.find('=') != std::string_view::npos)
		{
			const std::size_t equals = body.find('=');
			line.kind = Kind::Key;
			line.section = section;
			line.key = std::string(trimmed(body.substr(0, equals)));
			line.value = std::string(trimmed(body.substr(equals + 1)));
			if (line.key.empty())
				line.kind = Kind::Other;
		}
		else
			line.kind = Kind::Other;

		if (line.kind == Kind::Other && problems != nullptr)
			problems->push_back({number, line.raw});
		m_lines.push_back(std::move(line));
	}
}

const std::string *Document::find(std::string_view section, std::string_view key) const
{
	const std::string *found = nullptr;		// the last one counts, as in the old files
	for (const Line &line : m_lines)
	{
		if (line.kind == Kind::Key && equalsNoCase(line.section, section) && equalsNoCase(line.key, key))
			found = &line.value;
	}
	return found;
}

void Document::set(std::string_view section, std::string_view key, std::string_view value, std::string_view comment)
{
	for (auto it = m_lines.rbegin(); it != m_lines.rend(); ++it)
	{
		if (it->kind == Kind::Key && equalsNoCase(it->section, section) && equalsNoCase(it->key, key))
		{
			it->value = std::string(value);
			it->raw = it->key + " = " + it->value;
			return;
		}
	}

	Line keyLine;
	keyLine.kind = Kind::Key;
	keyLine.section = std::string(section);
	keyLine.key = std::string(key);
	keyLine.value = std::string(value);
	keyLine.raw = keyLine.key + " = " + keyLine.value;
	std::vector<Line> inserted;
	if (!comment.empty())
	{
		Line commentLine;
		commentLine.kind = Kind::Comment;
		commentLine.raw = "; " + std::string(comment);
		inserted.push_back(std::move(commentLine));
	}
	inserted.push_back(std::move(keyLine));

	// end of the section: after its last line that is not blank
	std::size_t header = m_lines.size();
	for (std::size_t i = 0; i < m_lines.size(); ++i)
	{
		if (m_lines[i].kind == Kind::Section && equalsNoCase(m_lines[i].section, section))
			header = i;
	}
	if (header < m_lines.size())
	{
		std::size_t last = header;
		for (std::size_t i = header + 1; i < m_lines.size() && m_lines[i].kind != Kind::Section; ++i)
		{
			if (m_lines[i].kind != Kind::Blank)
				last = i;
		}
		for (Line &line : inserted)
			line.section = std::string(section);
		m_lines.insert(m_lines.begin() + static_cast<std::ptrdiff_t>(last + 1), inserted.begin(), inserted.end());
		return;
	}

	if (!m_lines.empty() && m_lines.back().kind != Kind::Blank)
		m_lines.push_back(Line());
	Line sectionLine;
	sectionLine.kind = Kind::Section;
	sectionLine.section = std::string(section);
	sectionLine.raw = "[" + sectionLine.section + "]";
	m_lines.push_back(std::move(sectionLine));
	for (Line &line : inserted)
	{
		line.section = std::string(section);
		m_lines.push_back(std::move(line));
	}
}

void Document::remove(std::string_view section, std::string_view key, std::string_view comment)
{
	const std::string ownComment = comment.empty() ? std::string() : "; " + std::string(comment);
	for (std::size_t i = m_lines.size(); i-- > 0;)
	{
		const Line &line = m_lines[i];
		if (line.kind != Kind::Key || !equalsNoCase(line.section, section) || !equalsNoCase(line.key, key))
			continue;
		m_lines.erase(m_lines.begin() + static_cast<std::ptrdiff_t>(i));
		if (!ownComment.empty() && i > 0 && m_lines[i - 1].kind == Kind::Comment && m_lines[i - 1].raw == ownComment)
		{
			m_lines.erase(m_lines.begin() + static_cast<std::ptrdiff_t>(i - 1));
			--i;
		}
	}
}

void Document::addComment(std::string_view comment)
{
	Line line;
	line.kind = Kind::Comment;
	line.raw = "; " + std::string(comment);
	m_lines.push_back(std::move(line));
}

std::vector<std::vector<std::string>> Document::entries() const
{
	std::vector<std::vector<std::string>> result;
	for (const Line &line : m_lines)
	{
		if (line.kind == Kind::Key)
			result.push_back({line.section, line.key, line.value});
	}
	return result;
}

std::string Document::text() const
{
	std::string out;
	for (const Line &line : m_lines)
	{
		out += line.raw;
		out += m_newline;
	}
	return out;
}

// ---------------------------------------------------------------------------------------------
// Store

Store::Store()
{
	for (const Definition &definition : schema())
	{
		Value &value = m_values[lower(definition.id)];
		if (definition.defaultValue != nullptr)
		{
			normalizeValue(definition, definition.defaultValue, value.defaultValue);
			value.hasDefault = true;
		}
	}
}

const Definition *Store::definition(std::string_view id) const
{
	return findDefinition(id);
}

void Store::setDefault(std::string_view id, std::string_view text)
{
	const Definition *def = definition(id);
	std::string normalized;
	if (def == nullptr || !normalizeValue(*def, text, normalized))
		return;
	const std::string before = this->text(id);
	Value &value = m_values[lower(def->id)];
	value.defaultValue = normalized;
	value.hasDefault = true;
	changed(*def, before);
}

void Store::loadFile(File file, std::string_view text, Report *report)
{
	Document &document = m_documents[static_cast<int>(file)];
	std::vector<Document::Problem> problems;
	document.parse(text, &problems);
	if (report != nullptr)
	{
		for (const Document::Problem &problem : problems)
			report->lines.push_back(std::string(fileName(file)) + " line " + std::to_string(problem.line)
				+ ": not understood, kept: " + problem.text);
	}

	std::set<std::string> known;
	for (const Definition &def : schema())
	{
		if (def.file != file)
			continue;
		std::string_view section, key;
		splitId(def.id, section, key);
		known.insert(lower(def.id));
		const std::string before = this->text(def.id);
		Value &value = m_values[lower(def.id)];
		value.hasFile = false;
		value.fileValue.clear();
		const std::string *raw = document.find(section, key);
		if (raw != nullptr)
		{
			std::string normalized;
			bool clamped = false;
			if (normalizeValue(def, *raw, normalized, &clamped))
			{
				value.fileValue = normalized;
				value.hasFile = true;
				if (clamped && report != nullptr)
					report->lines.push_back(std::string(fileName(file)) + " " + def.id + " = " + *raw
						+ ": out of range, using " + normalized);
			}
			else if (report != nullptr)
				report->lines.push_back(std::string(fileName(file)) + " " + def.id + " = " + *raw + ": invalid, ignored");
		}
		changed(def, before);
	}
	if (report != nullptr)
	{
		for (const std::vector<std::string> &entry : document.entries())
		{
			if (known.find(lower(entry[0] + "." + entry[1])) == known.end())
				report->lines.push_back(std::string(fileName(file)) + " [" + entry[0] + "] " + entry[1]
					+ ": unknown, kept");
		}
	}
	m_dirty[static_cast<int>(file)] = false;
}

bool Store::setCommandLine(std::string_view id, std::string_view text, Report *report)
{
	const Definition *def = definition(id);
	std::string normalized;
	bool clamped = false;
	if (def == nullptr || !normalizeValue(*def, text, normalized, &clamped))
	{
		if (report != nullptr)
			report->lines.push_back("command line " + std::string(id) + " = " + std::string(text) + ": invalid, ignored");
		return false;
	}
	if (clamped && report != nullptr)
		report->lines.push_back("command line " + std::string(id) + " = " + std::string(text) + ": out of range, using "
			+ normalized);
	const std::string before = this->text(id);
	Value &value = m_values[lower(def->id)];
	value.commandLineValue = normalized;
	value.hasCommandLine = true;
	changed(*def, before);
	return true;
}

std::string Store::text(std::string_view id) const
{
	const auto found = m_values.find(lower(id));
	if (found == m_values.end())
		return {};
	const Value &value = found->second;
	if (value.hasCommandLine)
		return value.commandLineValue;
	if (value.hasFile)
		return value.fileValue;
	if (value.hasDefault)
		return value.defaultValue;
	return {};
}

bool Store::getBool(std::string_view id) const
{
	bool value = false;
	return parseBool(text(id), value) && value;
}

int Store::getInt(std::string_view id) const
{
	long long value = 0;
	if (!parseLong(text(id), value))
		return 0;
	return static_cast<int>(std::clamp<long long>(value, -2147483647LL - 1, 2147483647LL));
}

double Store::getFloat(std::string_view id) const
{
	double value = 0.0;
	return parseDouble(text(id), value) ? value : 0.0;
}

Source Store::sourceOf(std::string_view id) const
{
	const auto found = m_values.find(lower(id));
	if (found == m_values.end())
		return Source::None;
	const Value &value = found->second;
	if (value.hasCommandLine)
		return Source::CommandLine;
	if (value.hasFile)
		return Source::File;
	return value.hasDefault ? Source::Default : Source::None;
}

bool Store::set(std::string_view id, std::string_view text)
{
	const Definition *def = definition(id);
	std::string normalized;
	if (def == nullptr || !normalizeValue(*def, text, normalized))
		return false;

	const std::string before = this->text(id);
	Value &value = m_values[lower(def->id)];
	const int file = static_cast<int>(def->file);
	if (value.hasFile && value.fileValue == normalized)
		return true;
	value.fileValue = normalized;
	value.hasFile = true;

	std::string_view section, key;
	splitId(def->id, section, key);
	Document &document = m_documents[file];
	if (document.entries().empty() && document.text().empty())
	{
		document.addComment(def->file == File::Settings
			? "GZH Neo settings. Written by the game; your own comments and unknown keys are kept."
			: "GZH Neo state: what the game remembers between starts. Written by the game.");
	}
	document.set(section, key, normalized, def->description != nullptr ? def->description : "");
	m_dirty[file] = true;
	changed(*def, before);
	return true;
}

const std::string *Store::fileValue(std::string_view id) const
{
	const auto found = m_values.find(lower(id));
	return (found == m_values.end() || !found->second.hasFile) ? nullptr : &found->second.fileValue;
}

void Store::unset(std::string_view id)
{
	const Definition *def = definition(id);
	if (def == nullptr)
		return;
	Value &value = m_values[lower(def->id)];
	if (!value.hasFile)
		return;
	const std::string before = text(id);
	value.hasFile = false;
	value.fileValue.clear();
	std::string_view section, key;
	splitId(def->id, section, key);
	m_documents[static_cast<int>(def->file)].remove(section, key, def->description != nullptr ? def->description : "");
	m_dirty[static_cast<int>(def->file)] = true;
	changed(*def, before);
}

std::string Store::fileText(File file) const
{
	return m_documents[static_cast<int>(file)].text();
}

bool Store::dirty(File file) const
{
	return m_dirty[static_cast<int>(file)];
}

void Store::markSaved(File file)
{
	m_dirty[static_cast<int>(file)] = false;
}

std::shared_ptr<void> Store::observe(std::string_view id, Observer observer)
{
	auto holder = std::make_shared<Observer>(std::move(observer));
	m_observers.emplace(lower(id), holder);
	return holder;
}

void Store::changed(const Definition &definition, const std::string &before)
{
	if (text(definition.id) == before)
		return;
	const auto range = m_observers.equal_range(lower(definition.id));
	for (auto it = range.first; it != range.second;)
	{
		if (std::shared_ptr<Observer> observer = it->second.lock())
		{
			(*observer)();
			++it;
		}
		else
			it = m_observers.erase(it);
	}
}

// ---------------------------------------------------------------------------------------------
// Import

std::string cp1252ToUtf8(std::string_view text)
{
	// 0x80..0x9F of Windows-1252; the five unused codes map to the C1 control characters.
	static const unsigned short high[32] = {
		0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
		0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
		0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
		0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178 };
	std::string out;
	for (char c : text)
	{
		const unsigned char byte = static_cast<unsigned char>(c);
		if (byte < 0x80)
			out += c;
		else if (byte < 0xA0)
			appendUtf8(out, high[byte - 0x80]);
		else
			appendUtf8(out, byte);
	}
	return out;
}

std::string decodeQuotedAscii(std::string_view text)
{
	std::string bytes = decodeQuotedBytes(text);
	const std::size_t end = bytes.find('\0');
	if (end != std::string::npos)
		bytes.resize(end);
	return bytes;
}

std::string decodeQuotedUnicode(std::string_view text)
{
	const std::string bytes = decodeQuotedBytes(text);
	std::string out;
	for (std::size_t i = 0; i < bytes.size(); i += 2)
	{
		unsigned unit = static_cast<unsigned char>(bytes[i]);
		if (i + 1 < bytes.size())
			unit |= static_cast<unsigned>(static_cast<unsigned char>(bytes[i + 1])) << 8;
		if (unit == 0)
			break;
		if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < bytes.size())
		{
			const unsigned next = static_cast<unsigned char>(bytes[i + 2])
				| (static_cast<unsigned>(static_cast<unsigned char>(bytes[i + 3])) << 8);
			if (next >= 0xDC00 && next <= 0xDFFF)
			{
				appendUtf8(out, 0x10000 + ((unit - 0xD800) << 10) + (next - 0xDC00));
				i += 2;
				continue;
			}
		}
		if (unit >= 0xD800 && unit <= 0xDFFF)
			unit = 0xFFFD;	// a lone surrogate
		appendUtf8(out, unit);
	}
	return out;
}

namespace
{

/// CP1252 byte for a code point, or 0 if there is none.
unsigned char cp1252Byte(unsigned codePoint)
{
	if (codePoint < 0x80 || (codePoint >= 0xA0 && codePoint <= 0xFF))
		return static_cast<unsigned char>(codePoint);
	static const unsigned short high[32] = {
		0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
		0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
		0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
		0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178 };
	for (unsigned i = 0; i < 32; ++i)
	{
		if (high[i] == codePoint)
			return static_cast<unsigned char>(0x80 + i);
	}
	return 0;
}

/// Code points of UTF-8 text; a broken sequence gives U+FFFD.
std::vector<unsigned> codePoints(std::string_view text)
{
	std::vector<unsigned> result;
	for (std::size_t i = 0; i < text.size();)
	{
		const unsigned char lead = static_cast<unsigned char>(text[i]);
		int extra = lead < 0x80 ? 0 : (lead >> 5) == 0x6 ? 1 : (lead >> 4) == 0xE ? 2 : (lead >> 3) == 0x1E ? 3 : -1;
		if (extra < 0 || i + static_cast<std::size_t>(extra) >= text.size() + (extra == 0 ? 1 : 0))
		{
			result.push_back(0xFFFD);
			++i;
			continue;
		}
		unsigned value = extra == 0 ? lead : (lead & (0x3Fu >> extra));
		bool valid = true;
		for (int k = 1; k <= extra; ++k)
		{
			const unsigned char next = static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]);
			if ((next & 0xC0) != 0x80)
				valid = false;
			value = (value << 6) | (next & 0x3Fu);
		}
		if (!valid)
		{
			result.push_back(0xFFFD);
			++i;
			continue;
		}
		result.push_back(value);
		i += static_cast<std::size_t>(extra) + 1;
	}
	return result;
}

void appendQuotedByte(std::string &out, unsigned char byte)
{
	const char *const hex = "0123456789ABCDEF";
	if ((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z'))
		out += static_cast<char>(byte);
	else
	{
		out += '_';
		out += hex[byte >> 4];
		out += hex[byte & 0xF];
	}
}

/// An old value converted to the new form (UTF-8, decoded), per the coding of the definition.
std::string fromLegacy(const Definition &def, const std::string &raw)
{
	switch (def.legacyCoding)
	{
		case LegacyCoding::QuotedAscii:
			return cp1252ToUtf8(looksQuoted(raw) ? decodeQuotedAscii(raw) : raw);
		case LegacyCoding::QuotedUnicode:
			return looksQuoted(raw) ? decodeQuotedUnicode(raw) : cp1252ToUtf8(raw);
		default:
			return cp1252ToUtf8(raw);
	}
}

std::string numberedCountKey(const Definition &def)
{
	return std::string("Num") + def.legacyKey + "s";
}

/// Entries of a numbered list (Key0, Key1, ...; the count in NumKeys, if present), in the new form.
std::string numberedList(const Definition &def, const PreferenceValues &old, std::set<std::string> *consumed)
{
	const std::string countKey = numberedCountKey(def);
	if (consumed != nullptr)
		consumed->insert(countKey);
	long long count = -1;
	const auto countEntry = old.find(countKey);
	if (countEntry != old.end())
		parseLong(countEntry->second, count);
	std::string list;
	for (int i = 0; i < 100 && (count < 0 || i < count); ++i)
	{
		const std::string key = def.legacyKey + std::to_string(i);
		const auto entry = old.find(key);
		if (entry == old.end() || entry->second.empty())
		{
			if (count < 0)
				break;
			continue;
		}
		if (consumed != nullptr)
			consumed->insert(key);
		if (!list.empty())
			list += ';';
		list += cp1252ToUtf8(entry->second);
	}
	return list;
}

}	// namespace

std::string utf8ToCp1252(std::string_view text)
{
	std::string out;
	for (unsigned codePoint : codePoints(text))
	{
		const unsigned char byte = cp1252Byte(codePoint);
		out += (byte != 0 || codePoint == 0) ? static_cast<char>(byte) : '?';
	}
	return out;
}

std::string encodeQuotedAscii(std::string_view cp1252)
{
	std::string out;
	for (char c : cp1252)
		appendQuotedByte(out, static_cast<unsigned char>(c));
	return out;
}

std::string encodeQuotedUnicode(std::string_view utf8)
{
	std::string out;
	for (unsigned codePoint : codePoints(utf8))
	{
		auto unit = [&out](unsigned value)
		{
			appendQuotedByte(out, static_cast<unsigned char>(value & 0xFF));
			appendQuotedByte(out, static_cast<unsigned char>((value >> 8) & 0xFF));
		};
		if (codePoint >= 0x10000)
		{
			const unsigned rest = codePoint - 0x10000;
			unit(0xD800 + (rest >> 10));
			unit(0xDC00 + (rest & 0x3FF));
		}
		else
			unit(codePoint);
	}
	return out;
}

void importLegacy(Store &store, const LegacyFiles &files, Report &report)
{
	PreferenceValues values[4];
	const std::string *texts[4] = { nullptr, &files.options, &files.network, &files.skirmish };
	for (int f = 1; f < 4; ++f)
	{
		std::size_t pos = 0;
		const std::string &text = *texts[f];
		std::string key, value;
		while (pos < text.size())
		{
			std::size_t end = text.find('\n', pos);
			if (end == std::string::npos)
				end = text.size();
			if (parsePreferenceLine(std::string_view(text).substr(pos, end - pos), key, value))
				values[f][key] = value;
			pos = end + 1;
		}
	}

	std::set<std::string> consumed[4];
	for (const Definition &def : schema())
	{
		if (def.legacyFile == Legacy::None || def.legacyKey == nullptr)
			continue;
		const int f = static_cast<int>(def.legacyFile);
		const PreferenceValues &old = values[f];
		std::string imported;

		if (def.legacyCoding == LegacyCoding::NumberedList)
		{
			imported = numberedList(def, old, &consumed[f]);
			if (imported.empty())
				continue;
		}
		else
		{
			const auto entry = old.find(def.legacyKey);
			if (entry == old.end())
				continue;
			consumed[f].insert(def.legacyKey);
			imported = fromLegacy(def, entry->second);
		}

		const std::string from = std::string(legacyName(def.legacyFile)) + " " + def.legacyKey;
		std::string normalized;
		bool clamped = false;
		if (!normalizeValue(def, imported, normalized, &clamped))
		{
			report.lines.push_back(from + " = " + imported + ": invalid, not imported");
			continue;
		}
		store.set(def.id, normalized);
		report.lines.push_back(from + " -> " + fileName(def.file) + " " + def.id + " = " + normalized
			+ (clamped ? " (out of range, limited)" : ""));
	}

	const std::span<const char *const> dropped = droppedLegacyKeys();
	for (int f = 1; f < 4; ++f)
	{
		for (const auto &entry : values[f])
		{
			if (consumed[f].count(entry.first) != 0)
				continue;
			const bool noReader = std::any_of(dropped.begin(), dropped.end(),
				[&entry](const char *key) { return entry.first == key; });
			report.lines.push_back(std::string(legacyName(static_cast<Legacy>(f))) + " " + entry.first
				+ (noReader ? ": no reader any more, not imported" : ": unknown, not imported"));
		}
	}
}

}	// namespace Settings
}	// namespace Engine
