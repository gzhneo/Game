/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Update.h -- brings the translation database in line with Generals.str.
//
// Labels and texts are matched by name and text. New labels and texts are added with new string IDs,
// texts and labels no longer in Generals.str become obsolete. A changed text needs a decision the
// file cannot give: is it a new meaning (the revision grows, every translation must be redone) or a
// small correction (the translations stay valid)? The UpdateResolver answers it -- a dialog in the
// application, an option on the command line.

#pragma once

#include "Model/Database.h"

#include <string>
#include <vector>

namespace Babylon
{

enum class ChangeDecision
{
	Retranslate,	// new meaning: revision + 1
	Keep,			// correction: same revision, translations stay valid
	SkipLabel,		// leave this label as it is for now
	SkipAll			// leave all further labels with such a decision as they are
};

struct MatchDecision
{
	ChangeDecision decision = ChangeDecision::SkipLabel;
	int match = -1;		// index into the label's texts, -1: no old text, the text is new
};

class UpdateResolver
{
public:
	virtual ~UpdateResolver() = default;
	// A label with one text whose text changed.
	virtual ChangeDecision changedText(const Label &label, const Text &newText, const Text &oldText) = 0;
	// A label with several texts (only if the database allows them): which of the unmatched old
	// texts (candidates, indices into label.texts) the new text replaces.
	virtual MatchDecision matchText(const Label &label, const Text &newText, const std::vector<int> &candidates);
};

// Fixed answer, for the command line.
class FixedResolver : public UpdateResolver
{
public:
	explicit FixedResolver(ChangeDecision answer) : m_answer(answer) {}
	ChangeDecision changedText(const Label &label, const Text &newText, const Text &oldText) override;
	std::vector<std::string> asked;		// labels the resolver was asked about

private:
	ChangeDecision m_answer;
};

struct UpdateCounts
{
	int newLabels = 0;
	int deletedLabels = 0;
	int modifiedLabels = 0;
	int newStrings = 0;
	int deletedStrings = 0;
	int modifiedStrings = 0;
	int skippedLabels = 0;
	int updatedComments = 0;
	int updatedContexts = 0;
	int updatedSpeakers = 0;
	int updatedListeners = 0;
	int updatedMaxLen = 0;
	int updatedWaves = 0;
	int changes = 0;		// 0: the database matches Generals.str
	bool aborted = false;	// "skip all" was chosen
};

// apply false only counts the differences (the resolver is not asked; labels that would need a
// decision count as skipped). str is the database read from Generals.str.
UpdateCounts updateDatabase(const Database &str, Database &db, UpdateResolver *resolver, bool apply);

// The log lines of an update ("Added 3 new labels", ...).
std::vector<std::string> updateSummary(const UpdateCounts &counts);

} // namespace Babylon
