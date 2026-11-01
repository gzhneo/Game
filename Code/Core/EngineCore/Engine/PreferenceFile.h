/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// PreferenceFile.h -- the "key = value" files of the user data folder (Options.ini, Network.ini,
// Skirmish.ini): reading one line and merging the changes of one reader into the file. Needs no
// engine header, so EngineTests test it; SkirmishBattleHonors uses it.

#pragma once

#ifndef ENGINE_PREFERENCEFILE_H
#define ENGINE_PREFERENCEFILE_H

#include <map>
#include <string>
#include <string_view>

namespace Engine
{

using PreferenceValues = std::map<std::string, std::string>;

/// Splits a line at the first '=' and trims both sides (ASCII white space). Returns false for a
/// line without '=' or with an empty key or value; such lines are skipped.
bool parsePreferenceLine(std::string_view line, std::string &key, std::string &value);

/// Several readers of one file can be alive at once. Applies what one reader changed since it read
/// the file (loaded -> current) to what the file holds now (onDisk). An empty value counts as not
/// set: lookups through operator[] leave such entries behind and must not erase a value.
void mergePreferenceChanges(const PreferenceValues &loaded, const PreferenceValues &current,
	PreferenceValues &onDisk);

}	// namespace Engine

#endif	// ENGINE_PREFERENCEFILE_H
