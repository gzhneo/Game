/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Model/Database.h"

#include "Model/Text.h"

namespace Babylon
{

const std::vector<LangInfo> &languages()
{
	static const std::vector<LangInfo> list = {
		{Lang::US, "US", "us", "e"},
		{Lang::UK, "UK", "uk", "e"},
		{Lang::German, "German", "ge", "g"},
		{Lang::French, "French", "fr", "f"},
		{Lang::Spanish, "Spanish", "sp", "s"},
		{Lang::Italian, "Italian", "it", "i"},
		{Lang::Japanese, "Japanese", "ja", "j"},
		{Lang::Korean, "Korean", "ko", "k"},
		{Lang::Chinese, "Chinese", "ch", "c"},
		{Lang::Jabber, "Jabberwockie", "jb", "e"},
	};
	return list;
}

const LangInfo *langInfo(Lang lang)
{
	for (const LangInfo &info : languages())
		if (info.id == lang)
			return &info;
	return nullptr;
}

const LangInfo *findLanguage(std::string_view nameOrInitials)
{
	for (const LangInfo &info : languages())
		if (equalNoCase(nameOrInitials, info.name) || equalNoCase(nameOrInitials, info.initials))
			return &info;
	return nullptr;
}

const char *langName(Lang lang)
{
	const LangInfo *info = langInfo(lang);
	return info ? info->name : "unknown";
}

Translation *Text::translation(Lang lang)
{
	for (Translation &t : translations)
		if (t.lang == lang)
			return &t;
	return nullptr;
}

const Translation *Text::translation(Lang lang) const
{
	for (const Translation &t : translations)
		if (t.lang == lang)
			return &t;
	return nullptr;
}

Translation &Text::addTranslation(Lang lang)
{
	if (Translation *t = translation(lang))
		return *t;
	Translation t;
	t.lang = lang;
	translations.push_back(std::move(t));
	return translations.back();
}

void Text::invalidateAllWaves()
{
	waveInfo.valid = false;
	for (Translation &t : translations)
		t.wave.valid = false;
}

void Database::clear()
{
	labels.clear();
	obsolete.clear();
	if (nextId != -1)
		nextId = FIRST_STRING_ID;
	modified = false;
}

void Database::assignIds()
{
	if (nextId == -1)
		return;
	for (Label &label : labels)
		for (Text &text : label.texts)
			if (text.id == -1)
				text.id = newId();
	for (Text &text : obsolete)
		if (text.id == -1)
			text.id = newId();
}

int Database::textCount() const
{
	int count = 0;
	for (const Label &label : labels)
		count += static_cast<int>(label.texts.size());
	return count;
}

Label *Database::findLabel(std::u16string_view name)
{
	for (Label &label : labels)
		if (label.name == name)
			return &label;
	return nullptr;
}

const Label *Database::findLabel(std::u16string_view name) const
{
	for (const Label &label : labels)
		if (label.name == name)
			return &label;
	return nullptr;
}

std::unordered_map<int, TextRef> indexById(Database &db)
{
	std::unordered_map<int, TextRef> index;
	for (Label &label : db.labels)
		for (Text &text : label.texts)
			if (text.id > 0)
				index.emplace(text.id, TextRef{&label, &text});
	for (Text &text : db.obsolete)
		if (text.id > 0)
			index.emplace(text.id, TextRef{nullptr, &text});
	return index;
}

} // namespace Babylon
