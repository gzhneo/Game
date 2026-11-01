/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// BuildListCommands.h -- a player's build list as the old WorldBuilder edits it (BuildList,
// BuildListTool, BaseBuildProps): the structures the player's AI builds, in order, with place,
// angle, height offset, built at the start, rebuilds, name, script, health and unsellable; the
// power balance up to an entry; and the skirmish build list export (SkirmishBuildList INI).
//
// Changes go through the players' step (EditSides), like every other change to the players.

#pragma once

#include "Model/Map.h"

#include <functional>
#include <string>

namespace MapEditorCore
{

// BuildListInfo::UNLIMITED_REBUILDS (0xFFFFFFFF) as the map stores it.
constexpr std::int32_t UNLIMITED_REBUILDS = -1;

MapModel::BuildListEntry newBuildEntry(const std::string &templateName, float x, float y, float angle);
// Appends to the player's list; returns the index, -1 if there is no such player.
int addBuildEntry(MapModel::Sides &sides, int side, MapModel::BuildListEntry entry);
bool removeBuildEntry(MapModel::Sides &sides, int side, int index);
// Swaps with the neighbour (delta -1 or 1).
bool moveBuildEntry(MapModel::Sides &sides, int side, int index, int delta);
// The player's entry nearest to the point within radius; -1 if none.
[[nodiscard]] int buildEntryAt(const MapModel::Sides &sides, int side, float x, float y, float radius);

// Power of the player (BuildList::OnSelchangeBuildList): what its placed objects and its build list
// up to and including an entry produce and use. used is the part of the production in use, 0 to 1.
struct BuildPower
{
	int production = 0;
	int consumption = 0;
	float used() const;
};
[[nodiscard]] BuildPower buildListPower(const MapModel::Map &map, int side, int upTo, const std::function<int(const std::string &templateName)> &energyOf);

// The build list as the game's SkirmishBuildList INI block (BuildList::OnExport).
[[nodiscard]] std::string skirmishBuildListIni(const MapModel::Side &side, const std::string &factionSide);

} // namespace MapEditorCore
