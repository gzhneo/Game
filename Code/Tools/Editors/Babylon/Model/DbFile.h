/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// DbFile.h -- main.db, the file of the translation database, in the format of the original tool.
//
// An IFF file (sizes big-endian) of forms one after the other: NXDB (label count, next string ID),
// the obsolete texts, then per label an NLBL form followed by an NTXT form per text and an NTRN form
// per translation. The records inside the INFO chunks and the UTF-16 strings are little-endian.
// A database written here reads in the original tool and the other way round; one written by the
// original tool comes out byte for byte the same.

#pragma once

#include "Model/Database.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Babylon
{

// Replaces db; false with a message if the file is no database or broken (db is then empty).
bool readDatabase(std::span<const std::uint8_t> data, Database &db, std::string &error);
std::vector<std::uint8_t> writeDatabase(const Database &db);

} // namespace Babylon
