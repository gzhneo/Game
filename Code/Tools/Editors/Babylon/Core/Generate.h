/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Generate.h -- the string files of the game from the database: <prefix>_<xx>.csf (what the game
// loads) or <prefix>_<xx>.str (text form), one per language.

#pragma once

#include "Core/Reports.h"
#include "Model/Database.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Babylon
{

enum class GameFileFormat
{
	Csf,
	Str
};

enum class Untranslated
{
	UseIds,			// the string ID, so missing translations stand out in the game
	UseOriginal		// the English text
};

struct GenerateOptions
{
	GameFileFormat format = GameFileFormat::Csf;
	Untranslated untranslated = Untranslated::UseIds;
	std::string generatedBy = "Babylon";
	std::string generatedOn;	// date line of .str files
};

struct GeneratedFile
{
	Lang lang = Lang::Unknown;
	std::string fileName;			// <prefix>_<xx>.csf, lower case
	std::vector<std::uint8_t> data;
	bool ok = false;
	std::vector<std::string> warnings;	// translations missing, too long, ...; speech files missing
	std::string error;
};

// The file name for a prefix ("Generals" -> "generals_ge.csf"); folders in the prefix keep their case.
std::string gameFileName(const std::string &prefix, Lang lang, GameFileFormat format);
GeneratedFile generateGameFile(const Database &db, const std::string &prefix, Lang lang, const GenerateOptions &options, const DialogFiles &files);

} // namespace Babylon
