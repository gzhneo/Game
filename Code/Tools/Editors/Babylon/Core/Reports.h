/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Reports.h -- state of the translations and of the recorded speech per language.
//
// A speech file of a language lives in <dialog folder>/<letter>/<name><letter>.wav (US: e/abc01e.wav).
// Once someone has listened to it and confirmed that it matches the text, its size is stored; if the
// file changes size later it counts as unverified again.

#pragma once

#include "Model/Database.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Babylon
{

struct TranslationReport
{
	int labels = 0;
	int strings = 0;		// non-empty English texts
	int missing = 0;		// no translation
	int retranslate = 0;	// translation made for an older revision
	int tooLong = 0;
	int badFormat = 0;		// other printf conversions than the English text
	int translated = 0;
	int errors = 0;			// tooLong + badFormat
	[[nodiscard]] int problems() const { return missing + tooLong + retranslate + badFormat; }
};

struct DialogReport
{
	int dialog = 0;
	int missing = 0;		// no speech file
	int unresolved = 0;		// file present, not confirmed (or changed since)
	int resolved = 0;
	[[nodiscard]] int problems() const { return missing + unresolved; }
};

using LineSink = std::function<void(const std::string &)>;

// Lines per problem ("10023: needs re-translation") go to detail if given.
TranslationReport reportTranslations(const Database &db, Lang lang, const LineSink &detail = {});

// Finds speech files by name in any letter case, so a folder copied from Windows works on Linux.
class DialogFiles
{
public:
	explicit DialogFiles(std::filesystem::path root = {}) : m_root(std::move(root)) {}
	[[nodiscard]] const std::filesystem::path &root() const { return m_root; }
	// The path the file would have (for messages and playing it).
	[[nodiscard]] std::filesystem::path path(const Text &text, const LangInfo &lang) const;
	// Size of the file if it exists.
	[[nodiscard]] std::optional<std::uint64_t> size(const Text &text, const LangInfo &lang) const;
	void forget() { m_listings.clear(); }

private:
	std::filesystem::path m_root;
	mutable std::map<std::string, std::map<std::string, std::string>> m_listings;
};

// The stored confirmation of a speech file (US: the text's own, otherwise the translation's).
WaveInfo *waveInfo(Text &text, Lang lang);
// True if the file is confirmed; with recheck the stored size is compared with the file and the
// confirmation dropped if it differs.
bool dialogConfirmed(Text &text, Lang lang, const DialogFiles &files, bool recheck);
// Stores the current size of the file as confirmed; false if there is no file or translation.
bool confirmDialog(Text &text, Lang lang, const DialogFiles &files);

// Rechecks all stored confirmations of a language against the files.
void recheckDialog(Database &db, Lang lang, const DialogFiles &files);
// Drops all confirmations of a language.
void invalidateDialog(Database &db, Lang lang);
DialogReport reportDialog(const Database &db, Lang lang, const DialogFiles &files, const LineSink &detail = {});

// The lines the tool logs for "verify translations" of a language.
std::vector<std::string> translationSummary(const TranslationReport &r, Lang lang);
std::vector<std::string> dialogSummary(const DialogReport &r);

struct ReportOptions
{
	bool translations = true;
	bool dialog = false;
	int limit = 100;	// details only if there are fewer problems than this
};

// The report file of the original tool: per language a summary and, below the limit, details.
std::string reportText(const Database &db, const std::vector<Lang> &languages, const ReportOptions &options, const DialogFiles &files, const std::string &stamp);

} // namespace Babylon
