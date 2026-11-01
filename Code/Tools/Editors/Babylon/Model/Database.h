/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Database.h -- the translation database: labels of Generals.str with their English texts and the
// translations of every text.
//
// Each English text has a string ID (from 10000 on) that stays with it for good, and a revision
// that grows when the text changes so much that the translations must be redone. A translation
// remembers the revision it was made for. Texts of labels that left Generals.str stay as obsolete
// texts, so their IDs are never handed out again.
//
// Plain values: the operations in Core work on copies or in place and set 'modified'.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Babylon
{

// The numbers are stored in main.db and in the .csf header; keep them.
enum class Lang : int
{
	US = 0,
	UK = 1,
	German = 2,
	French = 3,
	Spanish = 4,
	Italian = 5,
	Japanese = 6,
	Jabber = 7,
	Korean = 8,
	Chinese = 9,
	Unknown = 10
};

struct LangInfo
{
	Lang id;
	const char *name;		// as in A1 of a translation sheet ("German")
	const char *initials;	// file name suffix of generated files ("ge")
	const char *character;	// speech file suffix and folder ("g")
};

// In the order the tool offers them (US first, the test language Jabberwockie last).
const std::vector<LangInfo> &languages();
const LangInfo *langInfo(Lang lang);
// By name ("German") or initials ("ge"), case-insensitive; null if unknown.
const LangInfo *findLanguage(std::string_view nameOrInitials);
const char *langName(Lang lang);

// Size of a speech file when it was last confirmed to match its text.
struct WaveInfo
{
	bool valid = false;
	std::uint32_t lo = 0;
	std::uint32_t hi = 0;
};

struct Translation
{
	Lang lang = Lang::Unknown;
	std::u16string text;
	std::u16string comment;
	int revision = 0;		// revision of the English text it translates
	WaveInfo wave;
	bool sent = false;		// not stored: marks translations a translator has received this session
};

struct Text
{
	int id = -1;
	int revision = 1;
	std::u16string text;		// as in Generals.str after formatMetaString (escapes still in)
	std::u16string wave;		// speech file name without language suffix; empty for no speech
	int line = -1;				// line in Generals.str (not stored)
	std::vector<Translation> translations;
	WaveInfo waveInfo;			// of the US speech file
	bool sent = false;			// not stored

	[[nodiscard]] bool isDialog() const { return !wave.empty(); }
	[[nodiscard]] Translation *translation(Lang lang);
	[[nodiscard]] const Translation *translation(Lang lang) const;
	// The translation for lang, created with revision 0 if missing.
	Translation &addTranslation(Lang lang);
	// Speech file sizes no longer confirmed (text or file name changed).
	void invalidateAllWaves();
};

struct Label
{
	std::u16string name;
	std::u16string comment;
	std::u16string context;
	std::u16string speaker;
	std::u16string listener;
	int maxLen = 0;				// 0: no limit
	int line = -1;				// line in Generals.str (not stored)
	std::vector<Text> texts;
};

const int FIRST_STRING_ID = 10000;

class Database
{
public:
	std::vector<Label> labels;
	std::vector<Text> obsolete;
	// Next string ID to hand out; -1 for a database without IDs (the one read from Generals.str).
	int nextId = FIRST_STRING_ID;
	// Several labels may carry the same text (a warning only if false).
	bool allowDuplicateText = true;
	// A label may have more than one text (an error if false, as in Generals.str today).
	bool allowMultiText = false;
	// Changed since loading or saving.
	bool modified = false;

	void clear();
	int newId() { return nextId == -1 ? -1 : nextId++; }
	// Gives every text without an ID a new one.
	void assignIds();
	[[nodiscard]] int textCount() const;

	[[nodiscard]] Label *findLabel(std::u16string_view name);
	[[nodiscard]] const Label *findLabel(std::u16string_view name) const;
};

// Look-ups for operations that touch many texts; invalid after labels or texts are added or removed.
struct TextRef
{
	Label *label = nullptr;		// null for an obsolete text
	Text *text = nullptr;
};
std::unordered_map<int, TextRef> indexById(Database &db);

} // namespace Babylon
