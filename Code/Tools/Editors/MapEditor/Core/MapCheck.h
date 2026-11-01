/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapCheck.h -- the map check behind the problem list. It gathers what the old WorldBuilder checked
// on loading (CWorldBuilderDoc::validate: missing object templates with the replace dialog, factions
// and build lists of the beta "Fundamentalist" names) and with "Fix Teams" (WbView::OnValidationFixTeams:
// duplicate teams, teams without owner, objects without a valid team), plus what it left to the
// user: duplicate objects, objects off the terrain, waypoint links to missing waypoints and the
// warnings of the scripts. Each problem has its fix here; the caller wraps it into one undo step.

#pragma once

#include "Core/ScriptCommands.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace MapEditorCore
{

enum class ProblemKind
{
	UnknownTemplate,	// objects, build list entries or team units of a template the game lacks
	ObjectTeam,			// object whose team is missing, or whose team has no valid owner
	TeamOwner,			// team whose owner is no player
	DuplicateTeam,		// a later team with the name of an earlier one
	UnknownFaction,		// player with a faction the game lacks
	DuplicateObject,	// object on an earlier one of the same template
	OffTerrain,			// object outside the height map
	WaypointLink,		// link to a waypoint ID no waypoint has
	Script				// warning of a script (no fix here; the script editor shows it)
};

struct Problem
{
	ProblemKind kind = ProblemKind::Script;
	// Template, team, player or faction the problem is about; the script text for Script.
	std::string subject;
	std::vector<std::int32_t> objects;
	int buildEntries = 0;			// UnknownTemplate: entries in build lists
	int teamUnits = 0;				// UnknownTemplate: unit slots of teams
	int side = -1;					// UnknownFaction
	int team = -1;					// TeamOwner, DuplicateTeam
	std::int32_t waypoint = 0;		// WaypointLink: the missing ID
	ScriptPath script;				// Script
	// A replacement the game has (template or faction), as the old editor found it for beta names.
	std::optional<std::string> suggestion;
};

struct MapCheckInput
{
	// Null leaves the check out (no engine: nothing is known).
	std::function<bool(const std::string &templateName)> templateKnown;
	std::function<bool(const std::string &faction)> factionKnown;
	const ScriptTemplates *scriptTemplates = nullptr;
	const ScriptCheck *scriptCheck = nullptr;
};

[[nodiscard]] std::vector<Problem> checkMap(const MapModel::Map &map, const MapCheckInput &input);
// True if the problem can be fixed from the list (all but script warnings).
[[nodiscard]] bool problemFixable(ProblemKind kind);

// The name the old editor swapped a missing one for: "Fundamentalist<x>" becomes "GLA<x>", "GC_<x>"
// becomes "<x>", "FactionFundamentalist<x>" becomes "FactionGLA<x>"; only if known has it.
[[nodiscard]] std::optional<std::string> replacementName(const std::string &name, const std::function<bool(const std::string &)> &known);

// Fixes. Each returns how many things it changed.
// Every use of a template (objects, build lists, team unit types) takes the other name.
int replaceTemplate(MapModel::Map &map, const std::string &from, const std::string &to);
// Objects and build list entries of the template go; team unit slots with it are cleared.
int removeTemplate(MapModel::Map &map, const std::string &templateName);
// The objects go to the default team of the player ("team<player>", "team" for the neutral one).
int setObjectsTeam(MapModel::Map &map, const std::vector<std::int32_t> &objects, const std::string &player);
int setTeamOwner(MapModel::Sides &sides, int team, const std::string &player);
// Removes a team that is no default team; objects on it keep the name (a later check shows them).
int removeTeamAt(MapModel::Sides &sides, int team);
int setFaction(MapModel::Sides &sides, int side, const std::string &faction);
int removeObjects(MapModel::Map &map, std::vector<std::int32_t> objects);
// Removes the links from and to a waypoint ID.
int removeWaypointLinks(MapModel::Map &map, std::int32_t waypoint);

} // namespace MapEditorCore
