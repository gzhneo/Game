/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// References.h -- where a name is used in the map (scripts, objects, teams, build lists, areas) and
// search and replace in the scripts' texts.

#pragma once

#include "Core/ScriptCommands.h"
#include "Model/Map.h"

#include <cstdint>
#include <string>
#include <vector>

namespace MapEditorCore
{

struct SearchOptions
{
	bool wholeValue = false;	// the value must equal the text, not only contain it
	bool matchCase = false;
};

// True if value matches text under the options; an empty text matches nothing.
[[nodiscard]] bool textMatches(const std::string &value, const std::string &text, const SearchOptions &options);

// Where a match sits. Script: path, part and call/parameter index; others: the index of the object,
// team, area or the player with the build list entry (entry).
struct Reference
{
	enum class Kind
	{
		ScriptName,			// the script's or group's name
		ScriptComment,		// one of the script's comments
		ScriptParameter,	// a text parameter of a condition or action
		Object,				// a text property of an object (name, team, waypoint ...)
		Team,				// a text property of a team
		BuildList,			// a build list entry (building name, script)
		Area				// the name of a trigger or water area
	};
	enum class Part
	{
		None,
		Condition,
		Action,
		FalseAction
	};
	Kind kind = Kind::ScriptName;
	ScriptPath path;			// scripts
	Part part = Part::None;
	int orIndex = -1;			// conditions: the OR block
	int call = -1;				// index of the condition or action
	int parameter = -1;
	int index = -1;				// object, team, area or player
	int entry = -1;				// build list entry
	std::string key;			// property key of an object or team
	std::string value;			// the whole matching value
};

[[nodiscard]] std::vector<Reference> findReferences(const MapModel::Map &map, const std::string &text, const SearchOptions &options);

// Replaces the text in the scripts' text parameters, names and comments (whole values or every
// occurrence); returns the number of values changed.
int replaceInScripts(MapModel::Sides &sides, const std::string &text, const std::string &with, const SearchOptions &options);

} // namespace MapEditorCore
