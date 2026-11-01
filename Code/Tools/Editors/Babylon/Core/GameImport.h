/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// GameImport.h -- a database from the files the game ships: the English .csf (or .str) becomes the
// master Generals.str, the .csf or .str of another language its translations, matched by label.
// So the tool starts from the installed game when neither Generals.str nor main.db exist.

#pragma once

#include "Model/Database.h"
#include "Model/StrFile.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Babylon
{

// Labels with their texts from a .csf (compiled) or .str file (by content). Texts come in the form
// of Generals.str: line breaks and tabs as \n and \t escapes; speech names with their language letter.
bool readGameFile(std::span<const std::uint8_t> data, std::vector<StrOutLabel> &labels, std::string &error);

struct GameImportResult
{
	bool ok = false;
	int labels = 0;			// labels in the file
	int added = 0;			// new translations
	int updated = 0;		// changed translations (including new ones)
	int unchanged = 0;
	int unknown = 0;		// label not in the database
	int skipped = 0;		// empty, a string ID instead of a text, or a label with several texts
	int sameAsEnglish = 0;	// taken, but identical to the English text
	std::vector<std::string> log;
};

// Takes the texts of the file as translations of lang (made for the current revision of each text).
GameImportResult importGameFile(Database &db, Lang lang, const std::vector<StrOutLabel> &labels);

} // namespace Babylon
