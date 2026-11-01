/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapDump.h -- readable text form of a map, for reviews, diffs and the command line.

#pragma once

#include "Model/Map.h"

#include <string>

namespace MapModel
{

enum class DumpDetail
{
	Summary,		// sizes, counts, world info, sides
	Full			// additionally every object, polygon, team and script
};

std::string dumpMap(const Map &map, DumpDetail detail);

std::string toUtf8(const std::u16string &text);
std::string formatValue(const PropertyValue &value);

} // namespace MapModel
