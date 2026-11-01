/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// CsfFile.h -- the compiled string file the game loads (generals.csf), version 3.
//
// Little-endian: a 24-byte header (" FSC", version, label count, string count, 0, language), then
// per label "LBL ", string count, name length and name, per string "STR " or "STRW" (with speech
// file), length in characters and the UTF-16 text with every bit inverted. The game reads it with
// Engine::readCsf (TextFiles.h).

#pragma once

#include "Model/Database.h"
#include "Model/StrFile.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Babylon
{

// Texts as stored in the database; the writer removes surplus spaces and turns the escapes into
// line breaks and tabs. A speech file name gets the language letter appended. False with a message
// for a label without a name (the original refused those as well).
bool writeCsfFile(const std::vector<StrOutLabel> &labels, const LangInfo &lang, std::vector<std::uint8_t> &out, std::string &error);

} // namespace Babylon
