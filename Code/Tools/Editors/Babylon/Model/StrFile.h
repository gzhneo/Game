/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// StrFile.h -- Generals.str, the English master of all texts, and the generated .str files.
//
// A label line is followed by quoted texts and "END". A text may run over several lines (each
// continuation line loses its leading spaces and slashes); after the closing quote an optional
// "= name" gives the speech file, whose last letter (the language) is dropped. Comment lines of the
// form "// KEYWORD: value" set COMMENT, CONTEXT, SPEAKER, LISTENER, MAXLEN and WAVE: before a label
// for all following labels, inside a label for that label only. The file is Latin-1.

#pragma once

#include "Model/Database.h"

#include <string>
#include <string_view>
#include <vector>

namespace Babylon
{

struct StrProblem
{
	int line = 0;
	bool error = false;		// false: a warning
	std::string message;
};

// Reads Generals.str into db (without string IDs). Returns false if the file ends inside a label
// or inside a text; db then holds everything up to there. Lines the format does not know are
// reported as warnings.
bool readStrFile(std::string_view data, Database &db, std::vector<StrProblem> &problems);

// A label of a generated .str file: its texts with their speech file names.
struct StrOutText
{
	std::u16string text;
	std::u16string wave;
};
struct StrOutLabel
{
	std::u16string name;
	std::vector<StrOutText> texts;
};

// The layout of the original tool: two comment lines, then per label a blank line, the name, the
// texts and END. Characters beyond Latin-1 become '?' and are counted in *lost.
std::string writeStrFile(const std::vector<StrOutLabel> &labels, std::string_view generatedBy, std::string_view generatedOn, int *lost = nullptr);

} // namespace Babylon
