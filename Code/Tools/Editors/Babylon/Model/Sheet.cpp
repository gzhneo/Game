/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Model/Sheet.h"

#include "Model/Text.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Babylon
{

namespace
{

std::string formatNumber(double v)
{
	char buf[64];
	if (std::isfinite(v) && std::floor(v) == v && std::fabs(v) < 1e15)
		std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
	else
		std::snprintf(buf, sizeof(buf), "%.15g", v);
	return buf;
}

std::string lower(std::string_view s)
{
	std::string out(s);
	for (char &c : out)
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	return out;
}

// Windows-1252 characters 0x80-0x9F; the rest of the code page is Latin-1.
const char16_t CP1252_HIGH[32] = {0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
	0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

std::string cp1252ToUtf8(std::string_view s)
{
	std::u16string w;
	w.reserve(s.size());
	for (const char c : s)
	{
		const auto b = static_cast<unsigned char>(c);
		w.push_back(b >= 0x80 && b < 0xA0 ? CP1252_HIGH[b - 0x80] : static_cast<char16_t>(b));
	}
	return toUtf8(w);
}

bool csvNeedsQuotes(const std::string &s)
{
	if (s.empty())
		return false;
	if (s.front() == ' ' || s.back() == ' ')
		return true;
	return s.find_first_of(",;\"\r\n\t") != std::string::npos;
}

} // namespace

Cell &Sheet::at(int row, int column)
{
	if (static_cast<int>(rows.size()) <= row)
		rows.resize(static_cast<std::size_t>(row) + 1);
	std::vector<Cell> &r = rows[static_cast<std::size_t>(row)];
	if (static_cast<int>(r.size()) <= column)
		r.resize(static_cast<std::size_t>(column) + 1);
	return r[static_cast<std::size_t>(column)];
}

const Cell *Sheet::get(int row, int column) const
{
	if (row < 0 || column < 0 || row >= static_cast<int>(rows.size()))
		return nullptr;
	const std::vector<Cell> &r = rows[static_cast<std::size_t>(row)];
	if (column >= static_cast<int>(r.size()))
		return nullptr;
	return &r[static_cast<std::size_t>(column)];
}

void Sheet::setText(int row, int column, std::string_view text)
{
	Cell &c = at(row, column);
	c = Cell();
	if (!text.empty())
	{
		c.kind = Cell::Kind::Text;
		c.text = std::string(text);
	}
}

void Sheet::setNumber(int row, int column, double value)
{
	Cell &c = at(row, column);
	c = Cell();
	c.kind = Cell::Kind::Number;
	c.number = value;
}

void Sheet::setFormula(int row, int column, std::string_view formula)
{
	Cell &c = at(row, column);
	c = Cell();
	c.kind = Cell::Kind::Formula;
	c.formula = std::string(formula);
}

std::string Sheet::text(int row, int column) const
{
	const Cell *c = get(row, column);
	if (!c)
		return {};
	switch (c->kind)
	{
	case Cell::Kind::Number:
		return formatNumber(c->number);
	case Cell::Kind::Text:
	case Cell::Kind::Formula:
		return c->text;
	case Cell::Kind::Empty:
		break;
	}
	return {};
}

int Sheet::integer(int row, int column, int fallback) const
{
	const Cell *c = get(row, column);
	if (!c)
		return fallback;
	double v = 0;
	if (c->kind == Cell::Kind::Number)
		v = c->number;
	else
	{
		std::string s = c->text;
		while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
			s.pop_back();
		std::size_t a = 0;
		while (a < s.size() && (s[a] == ' ' || s[a] == '\t'))
			++a;
		s = s.substr(a);
		if (s.empty())
			return fallback;
		char *end = nullptr;
		v = std::strtod(s.c_str(), &end);
		if (end == s.c_str() || *end != 0)
			return fallback;
	}
	if (!std::isfinite(v) || std::floor(v) != v || std::fabs(v) > 2147483647.0)
		return fallback;
	return static_cast<int>(v);
}

SheetFormat sheetFormatFromName(std::string_view fileName)
{
	const std::size_t dot = fileName.rfind('.');
	if (dot == std::string_view::npos)
		return SheetFormat::Unknown;
	const std::string ext = lower(fileName.substr(dot + 1));
	if (ext == "xlsx")
		return SheetFormat::Xlsx;
	if (ext == "ods")
		return SheetFormat::Ods;
	if (ext == "csv" || ext == "txt")
		return SheetFormat::Csv;
	return SheetFormat::Unknown;
}

SheetFormat sheetFormatFromData(std::span<const std::uint8_t> data)
{
	if (data.size() >= 4 && data[0] == 'P' && data[1] == 'K' && data[2] == 3 && data[3] == 4)
	{
		// OpenDocument puts its uncompressed mimetype first.
		const char *mime = "application/vnd.oasis.opendocument.spreadsheet";
		const std::size_t n = std::strlen(mime);
		for (std::size_t i = 30; i + n <= data.size() && i < 120; ++i)
			if (std::memcmp(data.data() + i, mime, n) == 0)
				return SheetFormat::Ods;
		return SheetFormat::Xlsx;
	}
	if (data.size() >= 8 && data[0] == 0xD0 && data[1] == 0xCF && data[2] == 0x11 && data[3] == 0xE0)
		return SheetFormat::Unknown;	// old binary .xls
	return SheetFormat::Csv;
}

const char *sheetFormatName(SheetFormat format)
{
	switch (format)
	{
	case SheetFormat::Xlsx:
		return "xlsx";
	case SheetFormat::Ods:
		return "ods";
	case SheetFormat::Csv:
		return "csv";
	case SheetFormat::Unknown:
		break;
	}
	return "unknown";
}

std::string columnName(int column)
{
	std::string name;
	int c = column + 1;
	while (c > 0)
	{
		const int rem = (c - 1) % 26;
		name.insert(name.begin(), static_cast<char>('A' + rem));
		c = (c - 1) / 26;
	}
	return name;
}

std::string toOpenFormula(std::string_view formula)
{
	std::string out = "of:=";
	bool inString = false;
	for (std::size_t i = 0; i < formula.size();)
	{
		const char c = formula[i];
		if (c == '"')
		{
			inString = !inString;
			out.push_back(c);
			++i;
			continue;
		}
		if (inString)
		{
			out.push_back(c);
			++i;
			continue;
		}
		if (c == ',')
		{
			out.push_back(';');
			++i;
			continue;
		}
		// A cell reference: letters followed by digits, not part of a name.
		if (c >= 'A' && c <= 'Z' && (i == 0 || !std::isalnum(static_cast<unsigned char>(formula[i - 1]))))
		{
			std::size_t j = i;
			while (j < formula.size() && formula[j] >= 'A' && formula[j] <= 'Z')
				++j;
			std::size_t k = j;
			while (k < formula.size() && formula[k] >= '0' && formula[k] <= '9')
				++k;
			if (k > j && (k >= formula.size() || !std::isalnum(static_cast<unsigned char>(formula[k]))))
			{
				out += "[." + std::string(formula.substr(i, k - i)) + "]";
				i = k;
				continue;
			}
			out += std::string(formula.substr(i, j - i));
			i = j;
			continue;
		}
		out.push_back(c);
		++i;
	}
	return out;
}

std::vector<std::uint8_t> writeCsv(const Sheet &sheet)
{
	std::string out = "\xEF\xBB\xBF";
	for (int r = 0; r < sheet.rowCount(); ++r)
	{
		const std::vector<Cell> &row = sheet.rows[static_cast<std::size_t>(r)];
		for (std::size_t c = 0; c < row.size(); ++c)
		{
			if (c)
				out.push_back(',');
			if (row[c].kind == Cell::Kind::Formula)
				continue;
			const std::string s = sheet.text(r, static_cast<int>(c));
			if (csvNeedsQuotes(s))
			{
				out.push_back('"');
				for (const char ch : s)
				{
					if (ch == '"')
						out.push_back('"');
					out.push_back(ch);
				}
				out.push_back('"');
			}
			else
				out += s;
		}
		out += "\r\n";
	}
	return std::vector<std::uint8_t>(out.begin(), out.end());
}

bool readCsv(std::span<const std::uint8_t> data, Sheet &sheet, std::string &error)
{
	sheet = Sheet();
	std::string text(reinterpret_cast<const char *>(data.data()), data.size());
	if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0)
		text.erase(0, 3);
	else
	{
		bool ok = true;
		(void)fromUtf8(text, &ok);
		if (!ok)
			text = cp1252ToUtf8(text);
	}
	if (text.find('\0') != std::string::npos)
	{
		error = "not a text file";
		return false;
	}

	// The separator that occurs most often in the first line outside quotes.
	char sep = ',';
	{
		int counts[3] = {0, 0, 0};
		const char seps[3] = {',', ';', '\t'};
		bool quoted = false;
		for (const char c : text)
		{
			if (c == '"')
				quoted = !quoted;
			else if (!quoted && (c == '\n' || c == '\r'))
				break;
			else if (!quoted)
				for (int k = 0; k < 3; ++k)
					if (c == seps[k])
						++counts[k];
		}
		int best = 0;
		for (int k = 1; k < 3; ++k)
			if (counts[k] > counts[best])
				best = k;
		sep = seps[best];
	}

	int row = 0;
	int col = 0;
	std::string field;
	bool quoted = false;
	bool wasQuoted = false;
	const auto endField = [&]() {
		if (!field.empty() || wasQuoted)
			sheet.setText(row, col, field);
		field.clear();
		wasQuoted = false;
		++col;
	};
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		const char c = text[i];
		if (quoted)
		{
			if (c == '"')
			{
				if (i + 1 < text.size() && text[i + 1] == '"')
				{
					field.push_back('"');
					++i;
				}
				else
					quoted = false;
			}
			else
				field.push_back(c);
			continue;
		}
		if (c == '"' && field.empty())
		{
			quoted = true;
			wasQuoted = true;
		}
		else if (c == sep)
			endField();
		else if (c == '\r' || c == '\n')
		{
			if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
				++i;
			endField();
			++row;
			col = 0;
		}
		else
			field.push_back(c);
	}
	if (quoted)
	{
		error = "a quoted field is not closed";
		return false;
	}
	if (!field.empty() || col > 0)
		endField();
	return true;
}

bool readSheet(std::span<const std::uint8_t> data, Sheet &sheet, std::string &error)
{
	switch (sheetFormatFromData(data))
	{
	case SheetFormat::Xlsx:
		return readXlsx(data, sheet, error);
	case SheetFormat::Ods:
		return readOds(data, sheet, error);
	case SheetFormat::Csv:
		return readCsv(data, sheet, error);
	case SheetFormat::Unknown:
		break;
	}
	error = "old binary Excel file (.xls): save it as .xlsx, .ods or .csv first";
	return false;
}

bool writeSheet(const Sheet &sheet, SheetFormat format, std::vector<std::uint8_t> &out)
{
	switch (format)
	{
	case SheetFormat::Xlsx:
		out = writeXlsx(sheet);
		return true;
	case SheetFormat::Ods:
		out = writeOds(sheet);
		return true;
	case SheetFormat::Csv:
		out = writeCsv(sheet);
		return true;
	case SheetFormat::Unknown:
		break;
	}
	return false;
}

} // namespace Babylon
