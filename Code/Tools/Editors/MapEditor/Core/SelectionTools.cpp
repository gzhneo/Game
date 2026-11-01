/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SelectionTools.cpp -- see SelectionTools.h.

#include "Core/SelectionTools.h"

#include "Core/WaypointCommands.h"

#include <cmath>

namespace MapEditorCore
{

namespace
{

constexpr std::int32_t ROAD_FLAGS = 0x36;		// road and bridge points
constexpr std::int32_t FIRST_POINT = 0x12;		// first point of a road or bridge segment
constexpr std::int32_t SECOND_POINT = 0x24;
constexpr float SAME_PLACE = 0.05f;

bool isRoad(const MapModel::MapObject &o)
{
	return (o.flags & ROAD_FLAGS) != 0;
}

bool samePlace(const MapModel::MapObject &a, const MapModel::MapObject &b)
{
	return std::abs(a.x - b.x) <= SAME_PLACE && std::abs(a.y - b.y) <= SAME_PLACE;
}

} // namespace

int objectSorting(const MapModel::MapObject &object, const SortingOf &sortingOf)
{
	if (isWaypoint(object))
		return SORTING_WAYPOINT;
	if (isRoad(object))
		return SORTING_ROAD;
	return sortingOf ? sortingOf(object.templateName) : 0;
}

bool pickAllowed(int sorting, int filter)
{
	if (sorting == SORTING_AUDIO)
		return filter == PICK_ANYTHING || filter == SORTING_AUDIO;
	return filter == PICK_ANYTHING || sorting == 0 || sorting == filter;
}

std::vector<std::int32_t> findDuplicates(const MapModel::Map &map)
{
	std::vector<std::int32_t> found;
	if (!map.objects)
		return found;
	const std::vector<MapModel::MapObject> &objects = *map.objects;
	for (std::size_t i = 0; i < objects.size(); ++i)
	{
		const MapModel::MapObject &o = objects[i];
		if (isWaypoint(o))
			continue;
		if (isRoad(o))
		{
			// A segment is its two points in a row; both ends must lie on an earlier segment's ends.
			if ((o.flags & FIRST_POINT) == 0 || i + 1 >= objects.size() || (objects[i + 1].flags & SECOND_POINT) == 0)
				continue;
			for (std::size_t j = 0; j + 1 < i; ++j)
			{
				const MapModel::MapObject &p = objects[j];
				if (p.templateName == o.templateName && (p.flags & FIRST_POINT) != 0 && (objects[j + 1].flags & SECOND_POINT) != 0
					&& samePlace(p, o) && samePlace(objects[j + 1], objects[i + 1]))
				{
					found.push_back(static_cast<std::int32_t>(i));
					found.push_back(static_cast<std::int32_t>(i + 1));
					break;
				}
			}
			++i;
			continue;
		}
		for (std::size_t j = 0; j < i; ++j)
		{
			if (objects[j].templateName == o.templateName && !isRoad(objects[j]) && samePlace(objects[j], o))
			{
				found.push_back(static_cast<std::int32_t>(i));
				break;
			}
		}
	}
	return found;
}

std::vector<std::int32_t> findSimilar(const MapModel::Map &map, const std::vector<std::int32_t> &selection)
{
	std::vector<std::int32_t> found;
	if (!map.objects)
		return found;
	const std::vector<MapModel::MapObject> &objects = *map.objects;
	const MapModel::MapObject *first = nullptr;
	for (std::int32_t index : selection)
	{
		if (index >= 0 && static_cast<std::size_t>(index) < objects.size() && !isRoad(objects[static_cast<std::size_t>(index)]))
		{
			first = &objects[static_cast<std::size_t>(index)];
			break;
		}
	}
	if (!first)
		return found;
	for (std::size_t i = 0; i < objects.size(); ++i)
	{
		if (!isRoad(objects[i]) && objects[i].templateName == first->templateName)
			found.push_back(static_cast<std::int32_t>(i));
	}
	return found;
}

std::vector<std::int32_t> findInvalidTeams(const MapModel::Map &map, std::set<std::string> &teams)
{
	std::vector<std::int32_t> found;
	teams.clear();
	if (!map.objects)
		return found;
	std::set<std::string> valid;
	if (map.sides)
	{
		for (const MapModel::PropertyMap &team : map.sides->teams)
			valid.insert(team.getAscii("teamName").value_or(""));
	}
	const std::vector<MapModel::MapObject> &objects = *map.objects;
	for (std::size_t i = 0; i < objects.size(); ++i)
	{
		const MapModel::MapObject &o = objects[i];
		if (isRoad(o) || isWaypoint(o))
			continue;
		const std::optional<std::string> owner = o.properties.getAscii("originalOwner");
		if (!owner || valid.count(*owner) == 0)
		{
			teams.insert(owner.value_or(""));
			found.push_back(static_cast<std::int32_t>(i));
		}
	}
	return found;
}

} // namespace MapEditorCore
