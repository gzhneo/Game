/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Office Open XML spreadsheets (.xlsx): written with shared strings, styles for the column kinds
// and an optional sheet protection without password; read from the first worksheet.

#include "Model/Sheet.h"
#include "Model/Xml.h"
#include "Model/Zip.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <unordered_map>

namespace Babylon
{

namespace
{

const char *NS_MAIN = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
const char *NS_REL = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

bool isHex(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool isEscapePattern(std::string_view s, std::size_t i)
{
	return i + 7 <= s.size() && s[i] == '_' && s[i + 1] == 'x' && isHex(s[i + 2]) && isHex(s[i + 3]) && isHex(s[i + 4]) && isHex(s[i + 5]) && s[i + 6] == '_';
}

// Office Open XML writes characters XML cannot carry as _xHHHH_; a literal "_xHHHH_" in the text is
// protected by escaping its underscore.
std::string encodeXString(std::string_view s)
{
	std::string out;
	for (std::size_t i = 0; i < s.size(); ++i)
	{
		const auto u = static_cast<unsigned char>(s[i]);
		char buf[16];
		if ((u < 0x20 && u != '\t' && u != '\n' && u != '\r') || isEscapePattern(s, i))
		{
			std::snprintf(buf, sizeof(buf), "_x%04X_", static_cast<unsigned>(u));
			out += buf;
		}
		else
			out.push_back(s[i]);
	}
	return xmlEscape(out);
}

std::string decodeXString(std::string_view s)
{
	std::string out;
	for (std::size_t i = 0; i < s.size(); ++i)
	{
		if (isEscapePattern(s, i))
		{
			const unsigned long c = std::strtoul(std::string(s.substr(i + 2, 4)).c_str(), nullptr, 16);
			if (c < 0x80)
				out.push_back(static_cast<char>(c));
			else if (c < 0x800)
			{
				out.push_back(static_cast<char>(0xC0 | (c >> 6)));
				out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
			}
			else
			{
				out.push_back(static_cast<char>(0xE0 | (c >> 12)));
				out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
			}
			i += 6;
		}
		else
			out.push_back(s[i]);
	}
	return out;
}

std::string num(double v)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%.6g", v);
	return buf;
}

// Index into cellXfs of styles.xml below.
int bodyStyle(const Column &c)
{
	if (c.editable)
		return c.wrap ? 3 : 7;
	if (c.number)
		return 4;
	return c.wrap ? 2 : 1;
}

const char *STYLES =
	"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
	"<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
	"<fonts count=\"2\"><font><sz val=\"10\"/><name val=\"Arial\"/><family val=\"2\"/></font>"
	"<font><b/><sz val=\"10\"/><name val=\"Arial\"/><family val=\"2\"/></font></fonts>"
	"<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill></fills>"
	"<borders count=\"3\"><border><left/><right/><top/><bottom/><diagonal/></border>"
	"<border><left style=\"thin\"/><right style=\"thin\"/><top/><bottom/><diagonal/></border>"
	"<border><left style=\"thin\"/><right style=\"thin\"/><top/><bottom style=\"double\"/><diagonal/></border></borders>"
	"<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
	"<cellXfs count=\"8\">"
	"<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
	"<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"left\" vertical=\"top\"/></xf>"
	"<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"left\" vertical=\"top\" wrapText=\"1\"/></xf>"
	"<xf numFmtId=\"49\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyBorder=\"1\" applyAlignment=\"1\" applyProtection=\"1\"><alignment horizontal=\"left\" vertical=\"top\" wrapText=\"1\"/><protection locked=\"0\"/></xf>"
	"<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"right\" vertical=\"top\"/></xf>"
	"<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"2\" xfId=\"0\" applyFont=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"bottom\" wrapText=\"1\"/></xf>"
	"<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"2\" xfId=\"0\" applyFont=\"1\" applyBorder=\"1\" applyAlignment=\"1\" applyProtection=\"1\"><alignment horizontal=\"center\" vertical=\"bottom\" wrapText=\"1\"/><protection locked=\"0\"/></xf>"
	"<xf numFmtId=\"49\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyBorder=\"1\" applyAlignment=\"1\" applyProtection=\"1\"><alignment horizontal=\"left\" vertical=\"top\"/><protection locked=\"0\"/></xf>"
	"</cellXfs>"
	"<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
	"</styleSheet>";

// Cell reference "G12" -> 0-based column and row; false if malformed.
bool parseRef(std::string_view ref, int &column, int &row)
{
	std::size_t i = 0;
	int c = 0;
	while (i < ref.size() && ref[i] >= 'A' && ref[i] <= 'Z')
	{
		c = c * 26 + (ref[i] - 'A' + 1);
		if (c > 16384)
			return false;
		++i;
	}
	if (i == 0 || i == ref.size())
		return false;
	int r = 0;
	for (; i < ref.size(); ++i)
	{
		if (ref[i] < '0' || ref[i] > '9')
			return false;
		r = r * 10 + (ref[i] - '0');
		if (r > 1048576)
			return false;
	}
	if (r == 0)
		return false;
	column = c - 1;
	row = r - 1;
	return true;
}

// Text of a shared or inline string: plain or rich-text runs, without phonetic hints.
std::string stringItemText(const XmlNode &si)
{
	std::string s;
	for (const XmlNode &c : si.children)
	{
		if (c.isText())
			continue;
		if (c.local() == "t")
			s += c.allText();
		else if (c.local() == "r")
			if (const XmlNode *t = c.child("t"))
				s += t->allText();
	}
	return decodeXString(s);
}

std::string folderOf(const std::string &path)
{
	const std::size_t slash = path.rfind('/');
	return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string relsOf(const std::string &path)
{
	const std::size_t slash = path.rfind('/');
	if (slash == std::string::npos)
		return "_rels/" + path + ".rels";
	return path.substr(0, slash + 1) + "_rels/" + path.substr(slash + 1) + ".rels";
}

// Resolves a relationship target against the folder of its source part.
std::string resolveTarget(const std::string &sourceFolder, std::string target)
{
	if (!target.empty() && target[0] == '/')
		return target.substr(1);
	std::string base = sourceFolder;
	while (target.compare(0, 3, "../") == 0)
	{
		target.erase(0, 3);
		if (!base.empty())
			base.pop_back();
		base = folderOf(base);
	}
	return base + target;
}

// Relationship id or type suffix -> target path.
std::map<std::string, std::pair<std::string, std::string>> readRels(const std::map<std::string, std::string> &files, const std::string &part)
{
	std::map<std::string, std::pair<std::string, std::string>> rels;
	const auto it = files.find(relsOf(part));
	if (it == files.end())
		return rels;
	XmlNode root;
	std::string error;
	if (!parseXml(it->second, root, error))
		return rels;
	for (const XmlNode &r : root.children)
	{
		if (r.isText() || r.local() != "Relationship")
			continue;
		const std::string *id = r.attribute("Id");
		const std::string *type = r.attribute("Type");
		const std::string *target = r.attribute("Target");
		if (id && target)
			rels[*id] = {type ? *type : std::string(), resolveTarget(folderOf(part), *target)};
	}
	return rels;
}

} // namespace

std::vector<std::uint8_t> writeXlsx(const Sheet &sheet)
{
	std::vector<std::string> strings;
	std::unordered_map<std::string, int> stringIndex;
	const auto sst = [&](const std::string &s) {
		const auto it = stringIndex.find(s);
		if (it != stringIndex.end())
			return it->second;
		const int i = static_cast<int>(strings.size());
		strings.push_back(s);
		stringIndex.emplace(s, i);
		return i;
	};

	std::string ws;
	ws += "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<worksheet xmlns=\"";
	ws += NS_MAIN;
	ws += "\" xmlns:r=\"";
	ws += NS_REL;
	ws += "\">";
	if (sheet.headerRow >= 0)
	{
		const std::string split = std::to_string(sheet.headerRow + 1);
		ws += "<sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"" + split + "\" topLeftCell=\"A" + std::to_string(sheet.headerRow + 2) +
			"\" activePane=\"bottomLeft\" state=\"frozen\"/></sheetView></sheetViews>";
	}
	ws += "<sheetFormatPr defaultRowHeight=\"13\"/>";
	if (!sheet.columns.empty())
	{
		ws += "<cols>";
		for (std::size_t c = 0; c < sheet.columns.size(); ++c)
		{
			const Column &col = sheet.columns[c];
			const std::string n = std::to_string(c + 1);
			ws += "<col min=\"" + n + "\" max=\"" + n + "\" width=\"" + num(col.width) + "\" customWidth=\"1\" style=\"" + std::to_string(bodyStyle(col)) + "\"";
			if (col.hidden)
				ws += " hidden=\"1\"";
			ws += "/>";
		}
		ws += "</cols>";
	}
	ws += "<sheetData>";
	for (int r = 0; r < sheet.rowCount(); ++r)
	{
		const std::vector<Cell> &row = sheet.rows[static_cast<std::size_t>(r)];
		ws += "<row r=\"" + std::to_string(r + 1) + "\">";
		for (std::size_t c = 0; c < row.size(); ++c)
		{
			const Cell &cell = row[c];
			if (cell.kind == Cell::Kind::Empty && r != sheet.headerRow)
				continue;
			int style = 0;
			if (r == sheet.headerRow)
				style = (c < sheet.columns.size() && sheet.columns[c].editable) ? 6 : 5;
			else if (r > sheet.headerRow && c < sheet.columns.size())
				style = bodyStyle(sheet.columns[c]);
			const std::string ref = columnName(static_cast<int>(c)) + std::to_string(r + 1);
			ws += "<c r=\"" + ref + "\" s=\"" + std::to_string(style) + "\"";
			switch (cell.kind)
			{
			case Cell::Kind::Text:
				ws += " t=\"s\"><v>" + std::to_string(sst(cell.text)) + "</v></c>";
				break;
			case Cell::Kind::Number:
			{
				char buf[40];
				std::snprintf(buf, sizeof(buf), "%.17g", cell.number);
				ws += "><v>" + std::string(buf) + "</v></c>";
				break;
			}
			case Cell::Kind::Formula:
				ws += "><f>" + xmlEscape(cell.formula) + "</f></c>";
				break;
			case Cell::Kind::Empty:
				ws += "/>";
				break;
			}
		}
		ws += "</row>";
	}
	ws += "</sheetData>";
	if (sheet.protect)
		ws += "<sheetProtection sheet=\"1\" objects=\"1\" scenarios=\"1\" formatColumns=\"0\" formatRows=\"0\"/>";
	ws += "<pageMargins left=\"0.75\" right=\"0.75\" top=\"1\" bottom=\"1\" header=\"0.5\" footer=\"0.5\"/>";
	ws += "</worksheet>";

	std::string ss = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<sst xmlns=\"";
	ss += NS_MAIN;
	ss += "\" count=\"" + std::to_string(strings.size()) + "\" uniqueCount=\"" + std::to_string(strings.size()) + "\">";
	for (const std::string &s : strings)
		ss += "<si><t xml:space=\"preserve\">" + encodeXString(s) + "</t></si>";
	ss += "</sst>";

	std::string wb = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<workbook xmlns=\"";
	wb += NS_MAIN;
	wb += "\" xmlns:r=\"";
	wb += NS_REL;
	wb += "\"><bookViews><workbookView/></bookViews><sheets><sheet name=\"" + xmlEscape(sheet.name) +
		"\" sheetId=\"1\" r:id=\"rId1\"/></sheets><calcPr fullCalcOnLoad=\"1\"/></workbook>";

	ZipWriter zip;
	zip.add("[Content_Types].xml",
		"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		"<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
		"<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
		"<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
		"<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
		"<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
		"<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
		"<Override PartName=\"/xl/sharedStrings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml\"/>"
		"<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>"
		"</Types>");
	zip.add("_rels/.rels",
		"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		"<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
		"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
		"<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" Target=\"docProps/app.xml\"/>"
		"</Relationships>");
	zip.add("docProps/app.xml",
		"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		"<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\"><Application>Babylon</Application></Properties>");
	zip.add("xl/workbook.xml", wb);
	zip.add("xl/_rels/workbook.xml.rels",
		"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		"<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
		"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
		"<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
		"<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings\" Target=\"sharedStrings.xml\"/>"
		"</Relationships>");
	zip.add("xl/worksheets/sheet1.xml", ws);
	zip.add("xl/styles.xml", STYLES);
	zip.add("xl/sharedStrings.xml", ss);
	return zip.finish();
}

bool readXlsx(std::span<const std::uint8_t> data, Sheet &sheet, std::string &error)
{
	sheet = Sheet();
	std::map<std::string, std::string> files;
	if (!readZip(data, files, error))
		return false;

	std::string workbookPath = "xl/workbook.xml";
	for (const auto &[id, rel] : readRels(files, ""))
		if (rel.first.ends_with("/officeDocument"))
			workbookPath = rel.second;
	const auto wbIt = files.find(workbookPath);
	if (wbIt == files.end())
	{
		error = "no workbook in the file (not an .xlsx spreadsheet)";
		return false;
	}
	XmlNode wb;
	if (!parseXml(wbIt->second, wb, error))
	{
		error = "workbook: " + error;
		return false;
	}
	const auto wbRels = readRels(files, workbookPath);
	std::string sheetPath;
	if (const XmlNode *sheets = wb.child("sheets"))
		if (const XmlNode *first = sheets->child("sheet"))
		{
			if (const std::string *name = first->attribute("name"))
				sheet.name = *name;
			if (const std::string *rid = first->attribute("id"))
				if (const auto it = wbRels.find(*rid); it != wbRels.end())
					sheetPath = it->second.second;
		}
	if (sheetPath.empty())
		sheetPath = folderOf(workbookPath) + "worksheets/sheet1.xml";
	std::string sharedPath = folderOf(workbookPath) + "sharedStrings.xml";
	for (const auto &[id, rel] : wbRels)
		if (rel.first.ends_with("/sharedStrings"))
			sharedPath = rel.second;

	std::vector<std::string> shared;
	if (const auto it = files.find(sharedPath); it != files.end())
	{
		XmlNode sst;
		if (!parseXml(it->second, sst, error))
		{
			error = "shared strings: " + error;
			return false;
		}
		for (const XmlNode &si : sst.children)
			if (!si.isText() && si.local() == "si")
				shared.push_back(stringItemText(si));
	}

	const auto wsIt = files.find(sheetPath);
	if (wsIt == files.end())
	{
		error = "worksheet " + sheetPath + " missing";
		return false;
	}
	XmlNode ws;
	if (!parseXml(wsIt->second, ws, error))
	{
		error = "worksheet: " + error;
		return false;
	}
	const XmlNode *sheetData = ws.child("sheetData");
	if (!sheetData)
		return true;
	int nextRow = 0;
	for (const XmlNode &row : sheetData->children)
	{
		if (row.isText() || row.local() != "row")
			continue;
		int r = nextRow;
		if (const std::string *ra = row.attribute("r"))
			r = std::atoi(ra->c_str()) - 1;
		if (r < 0 || r >= 1048576)
		{
			error = "row number out of range";
			return false;
		}
		nextRow = r + 1;
		int nextCol = 0;
		for (const XmlNode &c : row.children)
		{
			if (c.isText() || c.local() != "c")
				continue;
			int col = nextCol;
			int cellRow = r;
			if (const std::string *ref = c.attribute("r"))
				if (!parseRef(*ref, col, cellRow))
				{
					error = "bad cell reference " + *ref;
					return false;
				}
			nextCol = col + 1;
			const std::string *type = c.attribute("t");
			const XmlNode *v = c.child("v");
			const XmlNode *f = c.child("f");
			const std::string value = v ? v->allText() : std::string();
			Cell cell;
			if (type && *type == "s")
			{
				const int idx = std::atoi(value.c_str());
				if (idx < 0 || idx >= static_cast<int>(shared.size()))
				{
					error = "shared string index out of range";
					return false;
				}
				cell.kind = Cell::Kind::Text;
				cell.text = shared[static_cast<std::size_t>(idx)];
			}
			else if (type && *type == "inlineStr")
			{
				cell.kind = Cell::Kind::Text;
				if (const XmlNode *is = c.child("is"))
					cell.text = stringItemText(*is);
			}
			else if (type && (*type == "str" || *type == "e"))
			{
				cell.kind = Cell::Kind::Text;
				cell.text = decodeXString(value);
			}
			else if (type && *type == "b")
			{
				cell.kind = Cell::Kind::Text;
				cell.text = value == "1" ? "TRUE" : "FALSE";
			}
			else if (!value.empty())
			{
				char *end = nullptr;
				cell.number = std::strtod(value.c_str(), &end);
				cell.kind = (end && *end == 0) ? Cell::Kind::Number : Cell::Kind::Text;
				if (cell.kind == Cell::Kind::Text)
					cell.text = value;
			}
			if (f)
			{
				// keep the computed value as text so text() still answers
				if (cell.kind == Cell::Kind::Number)
				{
					Sheet tmp;
					tmp.setNumber(0, 0, cell.number);
					cell.text = tmp.text(0, 0);
				}
				cell.kind = Cell::Kind::Formula;
				cell.formula = f->allText();
			}
			if (cell.kind != Cell::Kind::Empty)
				sheet.at(cellRow, col) = std::move(cell);
		}
	}
	return true;
}

} // namespace Babylon
