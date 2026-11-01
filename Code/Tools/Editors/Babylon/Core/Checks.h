/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Checks.h -- errors and warnings of Generals.str. With errors the database must not be updated
// from the file: a label defined twice or a text over its length would end up in every language.

#pragma once

#include "Model/Database.h"
#include "Model/StrFile.h"

#include <string>
#include <vector>

namespace Babylon
{

struct Finding
{
	bool error = false;
	int line = 0;
	std::string message;
};

// Labels defined twice, labels with several texts (unless allowed), a text twice in a label, texts
// longer than their MAXLEN; plus the errors of reading the file.
std::vector<Finding> strErrors(const Database &str, const std::vector<StrProblem> &problems);
// Empty texts, a text in several labels (unless allowed), labels that differ only in case, lines
// the reader ignored.
std::vector<Finding> strWarnings(const Database &str, const std::vector<StrProblem> &problems);

std::string findingText(const Finding &finding);

} // namespace Babylon
