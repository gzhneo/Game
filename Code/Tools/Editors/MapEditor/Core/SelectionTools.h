/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SelectionTools.h -- the selection helpers of the old WorldBuilder: the pick filter by editor sorting
// (Ctrl+0..9), and selecting duplicates, similar objects and objects whose team the map lacks
// (WorldHeightMapEdit::selectDuplicates, selectSimilar, selectInvalidTeam).

#pragma once

#include "Model/Map.h"

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace MapEditorCore
{

// Editor sorting of a template (as the engine's catalog gives it: 0 none, 1 structure, ... 9 audio,
// 12 road, 13 waypoint); 0 for templates the catalog does not know.
using SortingOf = std::function<int(const std::string &templateName)>;

// Sorting codes of the pick filter; 0 picks anything.
constexpr int PICK_ANYTHING = 0;
constexpr int SORTING_AUDIO = 9;
constexpr int SORTING_ROAD = 12;
constexpr int SORTING_WAYPOINT = 13;

// The editor sorting of a map object: waypoints and road points by their kind, others by template.
[[nodiscard]] int objectSorting(const MapModel::MapObject &object, const SortingOf &sortingOf);

// True if the filter lets the object be picked, as the old pointer tool decides: sounds only with
// "anything" or "sounds", objects without a sorting always, others if the sorting matches.
[[nodiscard]] bool pickAllowed(int sorting, int filter);

// Objects that lie on an earlier object of the same template (within 0.05 units); road segments count
// if both ends match. Waypoints are never duplicates.
[[nodiscard]] std::vector<std::int32_t> findDuplicates(const MapModel::Map &map);

// Every object of the template of the first selected object (roads left out); empty if nothing is
// selected.
[[nodiscard]] std::vector<std::int32_t> findSimilar(const MapModel::Map &map, const std::vector<std::int32_t> &selection);

// Objects whose team (originalOwner) the map does not have, or which have none; roads and waypoints
// left out. teams gets the names that were not found ("" for a missing team).
std::vector<std::int32_t> findInvalidTeams(const MapModel::Map &map, std::set<std::string> &teams);

} // namespace MapEditorCore
