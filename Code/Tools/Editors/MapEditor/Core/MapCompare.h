/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapCompare.h -- the differences between the open map and another one, for the "Compare with..."
// window. Objects are paired by what identifies them across saves (waypoint ID, uniqueID, else
// template and position), areas by name, players and teams by name, scripts by player and name.

#pragma once

#include "Model/Map.h"

#include <cstdint>
#include <string>
#include <vector>

namespace MapEditorCore
{

struct MapDifference
{
	enum class Kind
	{
		Size,			// width, height or border differ; heights and textures are then not compared
		Heights,		// count: samples that differ
		Textures,		// count: cells whose texture class differs
		ObjectAdded,	// only in the open map; subject: template (uniqueID)
		ObjectRemoved,	// only in the other map
		ObjectMoved,	// position or angle differ; detail: "x, y, z, angle" of the other map -> the open one
		ObjectChanged,	// template, flags or properties differ; detail: what, count: how many
		WaypointLinks,	// count: links in one map only
		AreaAdded,
		AreaRemoved,
		AreaChanged,
		PlayerAdded,
		PlayerRemoved,
		TeamAdded,
		TeamRemoved,
		TeamChanged,	// detail: the properties that differ
		ScriptAdded,	// subject: script, detail: player
		ScriptRemoved,
		ScriptChanged,
		Lighting,
		WorldInfo		// detail: the properties that differ
	};

	Kind kind = Kind::Size;
	std::string subject;
	int count = 0;
	// Object or area index in the open map (current), -1 if none.
	std::int32_t index = -1;
	std::string detail;
};

// In order: size and terrain, objects (by index in the open map, then those only in the other one),
// waypoint links, areas, players, teams, scripts, lighting, world info. Roughly linear in the number
// of objects.
[[nodiscard]] std::vector<MapDifference> compareMaps(const MapModel::Map &current, const MapModel::Map &other);

} // namespace MapEditorCore
