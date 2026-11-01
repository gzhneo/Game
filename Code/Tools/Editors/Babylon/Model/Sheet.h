/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Sheet.h -- one table of a spreadsheet, as the translation exchange needs it, and its files.
//
// Translators get a sheet to fill in and send it back. Babylon writes and reads the open formats
// every current office suite handles (Office Open XML .xlsx, OpenDocument .ods) and plain .csv, by
// itself: no office program has to be installed or remote-controlled.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Babylon
{

struct Cell
{
	enum class Kind
	{
		Empty,
		Text,
		Number,
		Formula
	};
	Kind kind = Kind::Empty;
	std::string text;		// UTF-8; for a formula read from a file its last computed value
	double number = 0;
	std::string formula;	// A1 syntax without '=' and with ',' between arguments: LEN(G3)
};

struct Column
{
	double width = 10;		// in characters
	bool hidden = false;
	bool editable = false;	// stays writable in a protected sheet
	bool wrap = false;
	bool number = false;	// right-aligned
};

struct Sheet
{
	std::string name = "Sheet1";
	std::vector<std::vector<Cell>> rows;
	std::vector<Column> columns;
	int headerRow = -1;		// 0-based; bold, frozen at the top
	bool protect = false;	// only editable columns can be changed (no password)

	Cell &at(int row, int column);
	[[nodiscard]] const Cell *get(int row, int column) const;
	void setText(int row, int column, std::string_view text);
	void setNumber(int row, int column, double value);
	void setFormula(int row, int column, std::string_view formula);

	// The value as text: texts as they are, whole numbers without decimals.
	[[nodiscard]] std::string text(int row, int column) const;
	// Whole number of a cell (also a number written as text); fallback if empty or no number.
	[[nodiscard]] int integer(int row, int column, int fallback) const;
	[[nodiscard]] int rowCount() const { return static_cast<int>(rows.size()); }
};

enum class SheetFormat
{
	Xlsx,
	Ods,
	Csv,
	Unknown
};

// From the file name extension.
SheetFormat sheetFormatFromName(std::string_view fileName);
// From the content: ZIP with an OpenDocument mimetype, other ZIP, or text.
SheetFormat sheetFormatFromData(std::span<const std::uint8_t> data);
const char *sheetFormatName(SheetFormat format);

std::vector<std::uint8_t> writeXlsx(const Sheet &sheet);
std::vector<std::uint8_t> writeOds(const Sheet &sheet);
// UTF-8 with byte order mark, ',' between fields, CRLF; formulas are left empty.
std::vector<std::uint8_t> writeCsv(const Sheet &sheet);

// The first table of the file; false with a message if it cannot be read.
bool readXlsx(std::span<const std::uint8_t> data, Sheet &sheet, std::string &error);
bool readOds(std::span<const std::uint8_t> data, Sheet &sheet, std::string &error);
// UTF-8 (with or without byte order mark) or, if not valid UTF-8, Windows-1252; the separator
// (',', ';' or tab) is taken from the first line.
bool readCsv(std::span<const std::uint8_t> data, Sheet &sheet, std::string &error);
// By content, so a file with the wrong extension still reads.
bool readSheet(std::span<const std::uint8_t> data, Sheet &sheet, std::string &error);
bool writeSheet(const Sheet &sheet, SheetFormat format, std::vector<std::uint8_t> &out);

// Formula in OpenFormula syntax for .ods: of:=LEN([.G3])
std::string toOpenFormula(std::string_view formula);
// "G" for column 6 (0-based), "AA" for 26.
std::string columnName(int column);

} // namespace Babylon
