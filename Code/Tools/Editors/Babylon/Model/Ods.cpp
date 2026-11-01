/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// OpenDocument spreadsheets (.ods): written with column and cell styles and an optional table
// protection without password; read from the first table, with repeated rows and cells expanded
// only where they hold something.

#include "Model/Sheet.h"
#include "Model/Xml.h"
#include "Model/Zip.h"

#include <cstdio>
#include <cstdlib>
#include <map>

namespace Babylon
{

namespace
{

const int MAX_ROWS = 1048576;
const int MAX_COLUMNS = 16384;

std::string cm(double characters)
{
	// about 0.19 cm per character of a 10 pt font
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%.3fcm", characters * 0.19 + 0.1);
	return buf;
}

// Text of a paragraph: spaces, tabs and line breaks are elements in OpenDocument.
void paragraphText(const XmlNode &node, std::string &out)
{
	for (const XmlNode &c : node.children)
	{
		if (c.isText())
		{
			out += c.text;
			continue;
		}
		const std::string_view n = c.local();
		if (n == "s")
		{
			int count = 1;
			if (const std::string *a = c.attribute("c"))
				count = std::atoi(a->c_str());
			if (count < 1 || count > 10000)
				count = 1;
			out.append(static_cast<std::size_t>(count), ' ');
		}
		else if (n == "tab")
			out.push_back('\t');
		else if (n == "line-break")
			out.push_back('\n');
		else if (n == "annotation" || n == "note")
			continue;
		else
			paragraphText(c, out);
	}
}

// Writes text as paragraphs; runs of spaces after the first and at the start become <text:s/>.
std::string paragraphs(const std::string &text)
{
	std::string out;
	std::size_t start = 0;
	for (;;)
	{
		const std::size_t nl = text.find('\n', start);
		const std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
		out += "<text:p>";
		std::string esc;
		for (std::size_t i = 0; i < line.size();)
		{
			if (line[i] == ' ')
			{
				std::size_t j = i;
				while (j < line.size() && line[j] == ' ')
					++j;
				const std::size_t run = j - i;
				const bool atStart = i == 0;
				if (atStart)
					esc += "<text:s text:c=\"" + std::to_string(run) + "\"/>";
				else
				{
					esc.push_back(' ');
					if (run > 1)
						esc += "<text:s text:c=\"" + std::to_string(run - 1) + "\"/>";
				}
				i = j;
			}
			else if (line[i] == '\t')
			{
				esc += "<text:tab/>";
				++i;
			}
			else
			{
				std::size_t j = i;
				while (j < line.size() && line[j] != ' ' && line[j] != '\t')
					++j;
				esc += xmlEscape(line.substr(i, j - i));
				i = j;
			}
		}
		out += esc + "</text:p>";
		if (nl == std::string::npos)
			break;
		start = nl + 1;
	}
	return out;
}

const char *cellStyleName(const Column &c)
{
	if (c.editable)
		return c.wrap ? "ceEditWrap" : "ceEdit";
	if (c.number)
		return "ceNumber";
	return c.wrap ? "ceWrap" : "ceText";
}

struct Reader
{
	Sheet &sheet;
	std::string &error;
	int row = 0;

	bool readRows(const XmlNode &parent)
	{
		for (const XmlNode &n : parent.children)
		{
			if (n.isText())
				continue;
			const std::string_view name = n.local();
			if (name == "table-row")
			{
				int repeat = 1;
				if (const std::string *a = n.attribute("number-rows-repeated"))
					repeat = std::atoi(a->c_str());
				if (repeat < 1)
					repeat = 1;
				const bool hasContent = rowHasContent(n);
				if (!hasContent)
				{
					row += repeat;
					if (row > MAX_ROWS)
						row = MAX_ROWS;
					continue;
				}
				if (repeat > 1000)
				{
					error = "too many repeated rows with content";
					return false;
				}
				for (int k = 0; k < repeat; ++k)
				{
					if (row >= MAX_ROWS)
					{
						error = "too many rows";
						return false;
					}
					if (!readCells(n))
						return false;
					++row;
				}
			}
			else if (name == "table-row-group" || name == "table-header-rows" || name == "table-rows")
			{
				if (!readRows(n))
					return false;
			}
		}
		return true;
	}

	static bool cellHasContent(const XmlNode &c)
	{
		return c.attribute("value-type") || c.attribute("formula") || c.child("p");
	}

	static bool rowHasContent(const XmlNode &row)
	{
		for (const XmlNode &c : row.children)
			if (!c.isText() && (c.local() == "table-cell" || c.local() == "covered-table-cell") && cellHasContent(c))
				return true;
		return false;
	}

	bool readCells(const XmlNode &rowNode)
	{
		int col = 0;
		for (const XmlNode &c : rowNode.children)
		{
			if (c.isText() || (c.local() != "table-cell" && c.local() != "covered-table-cell"))
				continue;
			int repeat = 1;
			if (const std::string *a = c.attribute("number-columns-repeated"))
				repeat = std::atoi(a->c_str());
			if (repeat < 1)
				repeat = 1;
			if (!cellHasContent(c))
			{
				col += repeat;
				if (col > MAX_COLUMNS)
					col = MAX_COLUMNS;
				continue;
			}
			const Cell cell = readCell(c);
			if (repeat > 1000)
			{
				error = "too many repeated cells with content";
				return false;
			}
			for (int k = 0; k < repeat; ++k)
			{
				if (col >= MAX_COLUMNS)
				{
					error = "too many columns";
					return false;
				}
				if (cell.kind != Cell::Kind::Empty)
					sheet.at(row, col) = cell;
				++col;
			}
		}
		return true;
	}

	static Cell readCell(const XmlNode &c)
	{
		Cell cell;
		std::string text;
		bool first = true;
		for (const XmlNode &p : c.children)
		{
			if (p.isText() || p.local() != "p")
				continue;
			if (!first)
				text.push_back('\n');
			first = false;
			paragraphText(p, text);
		}
		const std::string *type = c.attribute("value-type");
		if (type && (*type == "float" || *type == "percentage" || *type == "currency"))
		{
			if (const std::string *v = c.attribute("value"))
			{
				char *end = nullptr;
				const double d = std::strtod(v->c_str(), &end);
				if (end && *end == 0)
				{
					cell.kind = Cell::Kind::Number;
					cell.number = d;
				}
			}
		}
		if (cell.kind == Cell::Kind::Empty && !text.empty())
		{
			cell.kind = Cell::Kind::Text;
			cell.text = text;
		}
		if (const std::string *f = c.attribute("formula"))
		{
			if (cell.kind == Cell::Kind::Number)
			{
				Sheet tmp;
				tmp.setNumber(0, 0, cell.number);
				cell.text = tmp.text(0, 0);
			}
			cell.kind = Cell::Kind::Formula;
			cell.formula = *f;
		}
		return cell;
	}
};

const XmlNode *findFirstTable(const XmlNode &node)
{
	for (const XmlNode &c : node.children)
	{
		if (c.isText())
			continue;
		if (c.local() == "table")
			return &c;
		if (const XmlNode *t = findFirstTable(c))
			return t;
	}
	return nullptr;
}

} // namespace

std::vector<std::uint8_t> writeOds(const Sheet &sheet)
{
	std::string x;
	x += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		 "<office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
		 "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
		 "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" "
		 "xmlns:of=\"urn:oasis:names:tc:opendocument:xmlns:of:1.2\" office:version=\"1.2\">"
		 "<office:font-face-decls><style:font-face style:name=\"Arial\" svg:font-family=\"Arial\" xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\"/></office:font-face-decls>"
		 "<office:automatic-styles>";
	for (std::size_t c = 0; c < sheet.columns.size(); ++c)
		x += "<style:style style:name=\"co" + std::to_string(c + 1) + "\" style:family=\"table-column\"><style:table-column-properties style:column-width=\"" +
			cm(sheet.columns[c].width) + "\"/></style:style>";
	const auto cellStyle = [&x](const char *name, const char *align, bool wrap, bool editable, bool bold, bool header) {
		x += std::string("<style:style style:name=\"") + name + "\" style:family=\"table-cell\"><style:table-cell-properties style:vertical-align=\"" +
			(header ? "bottom" : "top") + "\"" + (wrap ? " fo:wrap-option=\"wrap\"" : "") + (editable ? " style:cell-protect=\"none\"" : " style:cell-protect=\"protected\"") +
			(header ? " fo:border-bottom=\"1.1pt double #000000\"" : " fo:border-left=\"0.06pt solid #000000\" fo:border-right=\"0.06pt solid #000000\"") +
			"/><style:paragraph-properties fo:text-align=\"" + align + "\"/>" +
			"<style:text-properties style:font-name=\"Arial\" fo:font-size=\"10pt\"" + (bold ? " fo:font-weight=\"bold\"" : "") + "/></style:style>";
	};
	cellStyle("ceText", "start", false, false, false, false);
	cellStyle("ceWrap", "start", true, false, false, false);
	cellStyle("ceEdit", "start", false, true, false, false);
	cellStyle("ceEditWrap", "start", true, true, false, false);
	cellStyle("ceNumber", "end", false, false, false, false);
	cellStyle("ceHead", "center", true, false, true, true);
	cellStyle("ceHeadEdit", "center", true, true, true, true);
	x += "<style:style style:name=\"ta1\" style:family=\"table\"><style:table-properties table:display=\"true\"/></style:style>";
	x += "</office:automatic-styles><office:body><office:spreadsheet>";
	x += "<table:table table:name=\"" + xmlEscape(sheet.name) + "\" table:style-name=\"ta1\"";
	if (sheet.protect)
		x += " table:protected=\"true\"";
	x += ">";
	for (std::size_t c = 0; c < sheet.columns.size(); ++c)
	{
		x += "<table:table-column table:style-name=\"co" + std::to_string(c + 1) + "\" table:default-cell-style-name=\"" + cellStyleName(sheet.columns[c]) + "\"";
		if (sheet.columns[c].hidden)
			x += " table:visibility=\"collapse\"";
		x += "/>";
	}
	for (int r = 0; r < sheet.rowCount(); ++r)
	{
		if (r == sheet.headerRow)
			x += "<table:table-header-rows>";
		x += "<table:table-row>";
		const std::vector<Cell> &row = sheet.rows[static_cast<std::size_t>(r)];
		for (std::size_t c = 0; c < row.size(); ++c)
		{
			const Cell &cell = row[c];
			std::string style;
			if (r == sheet.headerRow)
				style = (c < sheet.columns.size() && sheet.columns[c].editable) ? "ceHeadEdit" : "ceHead";
			else if (r > sheet.headerRow && c < sheet.columns.size())
				style = cellStyleName(sheet.columns[c]);
			x += "<table:table-cell";
			if (!style.empty())
				x += " table:style-name=\"" + style + "\"";
			switch (cell.kind)
			{
			case Cell::Kind::Text:
				x += " office:value-type=\"string\">" + paragraphs(cell.text) + "</table:table-cell>";
				break;
			case Cell::Kind::Number:
			{
				char buf[40];
				std::snprintf(buf, sizeof(buf), "%.17g", cell.number);
				x += std::string(" office:value-type=\"float\" office:value=\"") + buf + "\">" + paragraphs(sheet.text(r, static_cast<int>(c))) + "</table:table-cell>";
				break;
			}
			case Cell::Kind::Formula:
				x += " table:formula=\"" + xmlEscape(toOpenFormula(cell.formula)) + "\"/>";
				break;
			case Cell::Kind::Empty:
				x += "/>";
				break;
			}
		}
		x += "</table:table-row>";
		if (r == sheet.headerRow)
			x += "</table:table-header-rows>";
	}
	x += "</table:table></office:spreadsheet></office:body></office:document-content>";

	std::string settings;
	if (sheet.headerRow >= 0)
	{
		const std::string split = std::to_string(sheet.headerRow + 1);
		settings = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
				   "<office:document-settings xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
				   "xmlns:config=\"urn:oasis:names:tc:opendocument:xmlns:config:1.0\" office:version=\"1.2\"><office:settings>"
				   "<config:config-item-set config:name=\"ooo:view-settings\"><config:config-item-map-indexed config:name=\"Views\">"
				   "<config:config-item-map-entry><config:config-item config:name=\"ViewId\" config:type=\"string\">view1</config:config-item>"
				   "<config:config-item-map-named config:name=\"Tables\"><config:config-item-map-entry config:name=\"" +
			xmlEscape(sheet.name) +
			"\">"
			"<config:config-item config:name=\"VerticalSplitMode\" config:type=\"short\">2</config:config-item>"
			"<config:config-item config:name=\"VerticalSplitPosition\" config:type=\"int\">" +
			split +
			"</config:config-item>"
			"<config:config-item config:name=\"ActiveSplitRange\" config:type=\"short\">2</config:config-item>"
			"<config:config-item config:name=\"PositionTop\" config:type=\"int\">0</config:config-item>"
			"<config:config-item config:name=\"PositionBottom\" config:type=\"int\">" +
			split +
			"</config:config-item>"
			"</config:config-item-map-entry></config:config-item-map-named></config:config-item-map-entry>"
			"</config:config-item-map-indexed></config:config-item-set></office:settings></office:document-settings>";
	}

	ZipWriter zip;
	zip.add("mimetype", "application/vnd.oasis.opendocument.spreadsheet", true);
	std::string manifest =
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.2\">"
		"<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.2\" manifest:media-type=\"application/vnd.oasis.opendocument.spreadsheet\"/>"
		"<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
		"<manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>"
		"<manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>";
	if (!settings.empty())
		manifest += "<manifest:file-entry manifest:full-path=\"settings.xml\" manifest:media-type=\"text/xml\"/>";
	manifest += "</manifest:manifest>";
	zip.add("META-INF/manifest.xml", manifest);
	zip.add("content.xml", x);
	zip.add("styles.xml",
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<office:document-styles xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
		"xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" office:version=\"1.2\">"
		"<office:styles><style:default-style style:family=\"table-cell\"><style:text-properties fo:font-size=\"10pt\"/></style:default-style></office:styles>"
		"</office:document-styles>");
	zip.add("meta.xml",
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<office:document-meta xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
		"xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" office:version=\"1.2\">"
		"<office:meta><meta:generator>Babylon</meta:generator></office:meta></office:document-meta>");
	if (!settings.empty())
		zip.add("settings.xml", settings);
	return zip.finish();
}

bool readOds(std::span<const std::uint8_t> data, Sheet &sheet, std::string &error)
{
	sheet = Sheet();
	std::map<std::string, std::string> files;
	if (!readZip(data, files, error))
		return false;
	const auto it = files.find("content.xml");
	if (it == files.end())
	{
		error = "no content.xml (not an .ods spreadsheet)";
		return false;
	}
	XmlNode root;
	if (!parseXml(it->second, root, error))
	{
		error = "content.xml: " + error;
		return false;
	}
	const XmlNode *table = findFirstTable(root);
	if (!table)
	{
		error = "the file holds no table";
		return false;
	}
	if (const std::string *name = table->attribute("name"))
		sheet.name = *name;
	Reader reader{sheet, error};
	return reader.readRows(*table);
}

} // namespace Babylon
