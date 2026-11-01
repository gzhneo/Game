/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Core/GameImport.h"

#include "Engine/TextFiles.h"
#include "Model/Text.h"

#include <unordered_map>

namespace Babylon
{

namespace
{

// The text of a .csf in the form the string files use: line breaks, tabs, backslashes and quotes
// as escapes, so writing it as .str or .csf again gives the same characters.
std::u16string escaped(const std::u16string &text)
{
	std::u16string out;
	for (const char16_t c : text)
	{
		if (c == u'\\')
			out += u"\\\\";
		else if (c == u'"')
			out += u"\\\"";
		else if (c == u'\n')
			out += u"\\n";
		else if (c == u'\t')
			out += u"\\t";
		else if (c != u'\r')
			out.push_back(c);
	}
	return out;
}

bool isNumber(const std::u16string &s)
{
	if (s.empty())
		return false;
	for (const char16_t c : s)
		if (c < u'0' || c > u'9')
			return false;
	return true;
}

} // namespace

bool readGameFile(std::span<const std::uint8_t> data, std::vector<StrOutLabel> &labels, std::string &error)
{
	labels.clear();
	if (data.size() >= 4 && data[0] == ' ' && data[1] == 'F' && data[2] == 'S' && data[3] == 'C')
	{
		int version = 0;
		int language = 0;
		std::vector<Engine::CsfEntry> entries;
		const Engine::CsfError e = Engine::readCsf(data.data(), data.size(), version, language, entries);
		if (e != Engine::CsfError::None)
		{
			error = std::string("not a readable .csf file: ") + Engine::csfErrorText(e);
			return false;
		}
		for (const Engine::CsfEntry &entry : entries)
		{
			StrOutLabel label;
			label.name = fromLatin1(entry.label);
			label.texts.push_back({escaped(entry.text), fromLatin1(entry.speech)});
			labels.push_back(std::move(label));
		}
		return true;
	}
	Database str;
	std::vector<StrProblem> problems;
	const bool complete = readStrFile(std::string_view(reinterpret_cast<const char *>(data.data()), data.size()), str, problems);
	if (!complete)
	{
		error = "the .str file ends inside a label";
		return false;
	}
	for (const Label &l : str.labels)
	{
		StrOutLabel label;
		label.name = l.name;
		for (const Text &t : l.texts)
			label.texts.push_back({t.text, t.wave});
		labels.push_back(std::move(label));
	}
	return true;
}

GameImportResult importGameFile(Database &db, Lang lang, const std::vector<StrOutLabel> &labels)
{
	GameImportResult r;
	if (lang == Lang::US || !langInfo(lang))
	{
		r.log.push_back("US English is the master text (Generals.str); choose the language of the file");
		return r;
	}
	std::unordered_map<std::u16string, Label *> byName;
	for (Label &label : db.labels)
		byName.emplace(label.name, &label);
	for (const StrOutLabel &in : labels)
	{
		++r.labels;
		const auto it = byName.find(in.name);
		if (it == byName.end())
		{
			++r.unknown;
			continue;
		}
		Label &label = *it->second;
		// An empty text is a translation too: the game shows nothing there, not the string ID.
		if (label.texts.size() != 1 || in.texts.empty())
		{
			++r.skipped;
			continue;
		}
		Text &text = label.texts.front();
		// As the file has it: spacing around line breaks is part of the text the game shows.
		const std::u16string &translation = in.texts.front().text;
		// a file generated with "string ID for untranslated texts"
		if (isNumber(translation) && translation == toU16(text.id))
		{
			++r.skipped;
			continue;
		}
		if (translation == text.text)
			++r.sameAsEnglish;
		Translation *trans = text.translation(lang);
		if (trans && trans->text == translation && trans->revision == text.revision)
		{
			++r.unchanged;
			continue;
		}
		if (!trans)
		{
			trans = &text.addTranslation(lang);
			++r.added;
		}
		trans->text = translation;
		trans->revision = text.revision;
		trans->wave.valid = false;
		++r.updated;
		db.modified = true;
	}
	r.ok = true;
	r.log.push_back("Labels in file : " + std::to_string(r.labels));
	r.log.push_back("New            : " + std::to_string(r.added));
	r.log.push_back("Updates        : " + std::to_string(r.updated - r.added));
	if (r.unchanged)
		r.log.push_back("Unchanged      : " + std::to_string(r.unchanged));
	if (r.unknown)
		r.log.push_back("Not in database: " + std::to_string(r.unknown));
	if (r.skipped)
		r.log.push_back("Skipped        : " + std::to_string(r.skipped));
	if (r.sameAsEnglish)
		r.log.push_back("Same as English: " + std::to_string(r.sameAsEnglish) + " (taken; check them if the file was generated with English for untranslated texts)");
	return r;
}

} // namespace Babylon
