/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Core/Exchange.h"

#include "Model/Text.h"

namespace Babylon
{

namespace
{

// 0-based columns of the sheet.
enum Col
{
	COL_WAVE = 0,
	COL_LABEL,
	COL_SPEAKER,
	COL_LISTENER,
	COL_ID,
	COL_ENGLISH,
	COL_LOCALIZED,
	COL_CONTEXT,
	COL_COMMENT,
	COL_MAXLEN,
	COL_STRLEN,
	COL_LENCHECK,
	COL_REVISION,
	COL_COUNT
};

const int ROW_HEADER = 1;
const int ROW_FIRST = 2;

struct Picked
{
	const Label *label;
	const Text *text;
	bool tooLong;
	bool badFormat;
};

bool confirmedNow(const Text &text, Lang lang, const DialogFiles &files)
{
	const LangInfo *info = langInfo(lang);
	const WaveInfo *w = nullptr;
	if (lang == Lang::US)
		w = &text.waveInfo;
	else if (const Translation *t = text.translation(lang))
		w = &t->wave;
	if (!info || !w || !w->valid)
		return false;
	const auto s = files.size(text, *info);
	return s && static_cast<std::uint32_t>(*s & 0xFFFFFFFFu) == w->lo && static_cast<std::uint32_t>(*s >> 32) == w->hi;
}

std::vector<Picked> pick(const Database &db, Lang lang, const ExportOptions &options, const DialogFiles &files)
{
	std::vector<Picked> out;
	const LangInfo *info = langInfo(lang);
	for (const Label &label : db.labels)
	{
		for (const Text &text : label.texts)
		{
			const Translation *trans = text.translation(lang);
			bool doExport = false;
			bool tooLong = false;
			bool badFormat = false;
			switch (options.filter)
			{
			case ExportFilter::Unsent:
				doExport = !text.sent;
				break;
			case ExportFilter::NonDialog:
				doExport = !text.isDialog();
				break;
			case ExportFilter::Unverified:
				doExport = text.isDialog() && info && files.size(text, *info) && !confirmedNow(text, lang, files);
				break;
			case ExportFilter::MissingDialog:
				doExport = text.isDialog() && info && !files.size(text, *info);
				break;
			case ExportFilter::Dialog:
				doExport = text.isDialog();
				break;
			case ExportFilter::All:
			case ExportFilter::Sample:
				doExport = true;
				break;
			case ExportFilter::Changes:
				if (!trans || text.revision > trans->revision)
					doExport = true;
				else if (label.maxLen && static_cast<int>(trans->text.size()) > label.maxLen)
					doExport = tooLong = true;
				else if (!sameFormat(trans->text, text.text))
					doExport = badFormat = true;
				break;
			}
			if (doExport && !text.text.empty())
				out.push_back({&label, &text, tooLong, badFormat});
		}
		if (options.filter == ExportFilter::Sample && out.size() > 50)
			break;
	}
	return out;
}

std::string upper(std::string s)
{
	for (char &c : s)
		if (c >= 'a' && c <= 'z')
			c = static_cast<char>(c - 'a' + 'A');
	return s;
}

// A line break or tab typed into a cell is stored as the escape the string files use.
std::u16string cellToText(const std::string &cell)
{
	std::u16string in = fromUtf8(cell);
	std::u16string out;
	for (std::size_t i = 0; i < in.size(); ++i)
	{
		if (in[i] == u'\r')
		{
			if (i + 1 < in.size() && in[i + 1] == u'\n')
				++i;
			out += u"\\n";
		}
		else if (in[i] == u'\n')
			out += u"\\n";
		else if (in[i] == u'\t')
			out += u"\\t";
		else
			out.push_back(in[i]);
	}
	return out;
}

ImportResult readSheetRows(Database &db, const Sheet &sheet, ImportVerifier &verifier, bool sentMode)
{
	ImportResult r;
	// A1 without surrounding spaces or a byte order mark some programs leave in a .csv
	std::string a1 = sheet.text(0, 0);
	if (a1.compare(0, 3, "\xEF\xBB\xBF") == 0)
		a1.erase(0, 3);
	while (!a1.empty() && (a1.back() == ' ' || a1.back() == '\t'))
		a1.pop_back();
	while (!a1.empty() && (a1.front() == ' ' || a1.front() == '\t'))
		a1.erase(0, 1);
	const LangInfo *info = findLanguage(a1);
	if (!info)
	{
		r.log.push_back("The sheet is of an unknown language or no translation sheet (A1: \"" + sheet.text(0, 0) + "\")");
		return r;
	}
	r.lang = info->id;
	auto index = indexById(db);
	int changed = 0;

	for (int row = ROW_FIRST; row < sheet.rowCount(); ++row)
	{
		const int id = sheet.integer(row, COL_ID, 0);
		if (id == -1)
			break;
		if (id == 0)
			continue;
		++r.found;
		const auto it = index.find(id);
		if (it == index.end())
		{
			++r.unmatched;
			continue;
		}
		Text &text = *it->second.text;
		const int revision = sheet.integer(row, COL_REVISION, 0);
		if (text.revision > revision)
		{
			if (sentMode)
				++changed;
			else
				++r.unmatched;
			continue;
		}
		if (text.revision < revision)
		{
			r.log.push_back("ERROR: expecting revision " + std::to_string(text.revision) + " for string ID " + std::to_string(id) + " but found revision " +
				std::to_string(revision) + ". Possible bad ID!");
			++r.errors;
			continue;
		}
		const std::u16string translation = cellToText(sheet.text(row, COL_LOCALIZED));
		if (!sentMode && translation.empty())
		{
			++r.missing;
			continue;
		}

		const std::u16string english = cellToText(sheet.text(row, COL_ENGLISH));
		if (english != text.text)
		{
			// Either the text was corrected without a new revision, or the IDs are wrong. The label
			// of the row tells: the nearest label at or above the row.
			std::string sheetLabel;
			for (int n = row; n >= ROW_FIRST && sheetLabel.empty(); --n)
				sheetLabel = sheet.text(n, COL_LABEL);
			const Label *label = it->second.label;
			bool bad = false;
			if (!label || sheetLabel.empty() || fromUtf8(sheetLabel) != label->name)
				bad = !verifier.sameText(english, text.text, id);
			else
				for (const Text &other : label->texts)
					if (&other != &text && other.text == english)
						bad = true;
			if (bad)
			{
				r.badIds = true;
				r.log.push_back("Aborting import: bad string IDs (row " + std::to_string(row + 1) + ", ID " + std::to_string(id) + ")");
				return r;
			}
			if (sentMode)
				continue;
		}
		else if (sentMode)
		{
			text.sent = true;
			++r.matched;
			continue;
		}

		Translation *trans = text.translation(info->id);
		if (!trans)
		{
			++r.added;
			trans = &text.addTranslation(info->id);
		}
		if (trans->revision == revision && trans->text == translation)
			continue;
		trans->text = translation;
		trans->wave.valid = false;
		trans->revision = revision;
		++r.updated;
		db.modified = true;
	}
	r.ok = true;
	r.log.push_back("Total found : " + std::to_string(r.found));
	if (sentMode)
	{
		r.log.push_back("Matched     : " + std::to_string(r.matched));
		r.log.push_back("Unmatched   : " + std::to_string(r.unmatched));
		if (changed)
			r.log.push_back("Changed     : " + std::to_string(changed));
		r.unmatched += changed;
	}
	else
	{
		r.log.push_back("New         : " + std::to_string(r.added));
		r.log.push_back("Updates     : " + std::to_string(r.updated - r.added));
		if (r.missing)
			r.log.push_back("Missing     : " + std::to_string(r.missing));
		if (r.unmatched)
			r.log.push_back("Unmatched   : " + std::to_string(r.unmatched));
	}
	if (r.errors)
		r.log.push_back("Errors      : " + std::to_string(r.errors));
	return r;
}

} // namespace

const char *exportFilterName(ExportFilter filter)
{
	switch (filter)
	{
	case ExportFilter::Changes:
		return "changes";
	case ExportFilter::All:
		return "all";
	case ExportFilter::Dialog:
		return "dialog";
	case ExportFilter::NonDialog:
		return "nondialog";
	case ExportFilter::Sample:
		return "sample";
	case ExportFilter::MissingDialog:
		return "missing-dialog";
	case ExportFilter::Unverified:
		return "unverified";
	case ExportFilter::Unsent:
		return "unsent";
	}
	return "changes";
}

bool parseExportFilter(const std::string &name, ExportFilter &filter)
{
	for (const ExportFilter f : {ExportFilter::Changes, ExportFilter::All, ExportFilter::Dialog, ExportFilter::NonDialog, ExportFilter::Sample,
			 ExportFilter::MissingDialog, ExportFilter::Unverified, ExportFilter::Unsent})
		if (name == exportFilterName(f))
		{
			filter = f;
			return true;
		}
	return false;
}

int exportCount(const Database &db, Lang lang, const ExportOptions &options, const DialogFiles &files)
{
	return static_cast<int>(pick(db, lang, options, files).size());
}

Sheet exportSheet(const Database &db, Lang lang, const ExportOptions &options, const DialogFiles &files)
{
	const std::vector<Picked> picked = pick(db, lang, options, files);
	const LangInfo *info = langInfo(lang);
	const std::string name = langName(lang);

	Sheet sheet;
	sheet.name = name;
	sheet.headerRow = ROW_HEADER;
	sheet.protect = options.protect;
	struct ColumnSpec
	{
		double width;
		bool wrap;
		bool number;
		const char *heading;
	};
	const ColumnSpec specs[COL_COUNT] = {
		{22.4, false, false, "Speech filename"},
		{34.6, false, false, "Label"},
		{20.6, true, false, "Person Speaking"},
		{20.1, true, false, "Person(s) listening"},
		{9.3, false, true, "String ID"},
		{48.0, true, false, "US English Text"},
		{47.6, true, false, ""},
		{48.6, true, false, "Context of the string"},
		{48.0, true, false, "Comments, direction of actors (if applicable)"},
		{8.3, false, true, "Max allowed length"},
		{7.9, false, true, "Current length"},
		{12.6, false, false, "Errors"},
		{7.7, false, true, "String Version"},
	};
	for (int c = 0; c < COL_COUNT; ++c)
	{
		Column col;
		col.width = specs[c].width;
		col.wrap = specs[c].wrap;
		col.number = specs[c].number;
		col.editable = c == COL_LOCALIZED;
		col.hidden = c == COL_REVISION;
		sheet.columns.push_back(col);
		sheet.setText(ROW_HEADER, c, c == COL_LOCALIZED ? name + " Translation" : specs[c].heading);
	}
	sheet.setText(0, 0, name);
	sheet.setNumber(0, 1, static_cast<double>(picked.size()));

	int row = ROW_FIRST;
	for (const Picked &p : picked)
	{
		const Label &label = *p.label;
		const Text &text = *p.text;
		const std::string r = std::to_string(row + 1);
		sheet.setText(row, COL_LABEL, toUtf8(label.name));
		sheet.setText(row, COL_SPEAKER, toUtf8(label.speaker));
		sheet.setText(row, COL_LISTENER, toUtf8(label.listener));
		sheet.setNumber(row, COL_ID, text.id);
		sheet.setText(row, COL_ENGLISH, toUtf8(text.text));
		sheet.setText(row, COL_CONTEXT, toUtf8(label.context));
		sheet.setText(row, COL_COMMENT, toUtf8(label.comment));
		sheet.setNumber(row, COL_MAXLEN, label.maxLen ? label.maxLen : 10000);
		sheet.setFormula(row, COL_STRLEN, "LEN(" + columnName(COL_LOCALIZED) + r + ")");
		sheet.setFormula(row, COL_LENCHECK, "IF(" + columnName(COL_STRLEN) + r + ">" + columnName(COL_MAXLEN) + r + ",\"Too long!\",\" \")");
		sheet.setNumber(row, COL_REVISION, text.revision);
		if (text.isDialog() && info)
			sheet.setText(row, COL_WAVE, upper(toUtf8(text.wave) + info->character + ".wav"));

		const Translation *trans = text.translation(lang);
		if (lang == Lang::Jabber)
			sheet.setText(row, COL_LOCALIZED, toUtf8(jabber(text.text)));
		else if (trans && (options.includeTranslations || p.tooLong || p.badFormat))
		{
			sheet.setText(row, COL_LOCALIZED, toUtf8(trans->text));
			if (p.tooLong || p.badFormat)
			{
				std::string error = "ERROR: ";
				if (p.tooLong)
					error += p.badFormat ? "too long and bad format" : "too long";
				else
					error += "bad format";
				sheet.setText(row, COL_COMMENT, error);
			}
		}
		++row;
	}
	sheet.setNumber(row, COL_ID, -1);
	return sheet;
}

ImportResult importSheet(Database &db, const Sheet &sheet, ImportVerifier &verifier)
{
	return readSheetRows(db, sheet, verifier, false);
}

ImportResult markSent(Database &db, const Sheet &sheet, ImportVerifier &verifier)
{
	return readSheetRows(db, sheet, verifier, true);
}

} // namespace Babylon
