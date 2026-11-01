/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Exchange.h -- the translation sheets translators fill in.
//
// The layout is the one of the original tool (template BABYLON.XLT), so sheets from both match:
//   A1 language name, B1 number of texts, row 2 headings, from row 3 one text per row:
//   A speech file, B label, C speaker, D listener, E string ID, F English, G translation,
//   H context, I comment, J maximum length, K current length (formula), L "Too long!" (formula),
//   M revision (hidden); the row after the last text has -1 as string ID.
// Only column G can be edited in the protected sheet. On import string ID and revision tie a row to
// its text; a row whose English differs from the database is checked before it is taken.

#pragma once

#include "Core/Reports.h"
#include "Model/Database.h"
#include "Model/Sheet.h"

#include <string>
#include <vector>

namespace Babylon
{

enum class ExportFilter
{
	Changes,		// texts that need work: no translation, outdated, too long, other format
	All,
	Dialog,			// texts with speech
	NonDialog,
	Sample,			// the first labels up to about 50 texts
	MissingDialog,	// speech file of the language missing
	Unverified,		// speech file present but not confirmed
	Unsent			// not yet marked as sent in this session
};

const char *exportFilterName(ExportFilter filter);
// "changes", "all", "dialog", "nondialog", "sample", "missing-dialog", "unverified", "unsent".
bool parseExportFilter(const std::string &name, ExportFilter &filter);

struct ExportOptions
{
	ExportFilter filter = ExportFilter::Changes;
	bool includeTranslations = false;	// fill G with the current translation
	bool protect = true;
};

// Number of texts the export would contain.
int exportCount(const Database &db, Lang lang, const ExportOptions &options, const DialogFiles &files);
Sheet exportSheet(const Database &db, Lang lang, const ExportOptions &options, const DialogFiles &files);

// Answers what the sheet cannot: does an English text of the sheet still mean the database text?
class ImportVerifier
{
public:
	virtual ~ImportVerifier() = default;
	// True if the two texts are the same text (the row may be taken); false stops the import
	// because the string IDs of the sheet cannot be trusted.
	virtual bool sameText(const std::u16string &sheetEnglish, const std::u16string &dbEnglish, int stringId) = 0;
};

class FixedVerifier : public ImportVerifier
{
public:
	explicit FixedVerifier(bool answer) : m_answer(answer) {}
	bool sameText(const std::u16string &, const std::u16string &, int) override { return m_answer; }

private:
	bool m_answer;
};

struct ImportResult
{
	bool ok = false;		// false: not a translation sheet, or stopped because of bad string IDs
	Lang lang = Lang::Unknown;
	int found = 0;
	int added = 0;			// new translations
	int updated = 0;		// changed translations (including new ones)
	int missing = 0;		// rows without translation
	int unmatched = 0;		// string ID unknown or row made for an older revision
	int errors = 0;			// row made for a newer revision than the database knows
	int matched = 0;		// "sent" mode: rows whose English still matches
	bool badIds = false;
	std::vector<std::string> log;
};

// Takes the translations of the sheet into db.
ImportResult importSheet(Database &db, const Sheet &sheet, ImportVerifier &verifier);
// Marks the texts of a sheet that went out to translators as sent (filter "unsent").
ImportResult markSent(Database &db, const Sheet &sheet, ImportVerifier &verifier);

} // namespace Babylon
