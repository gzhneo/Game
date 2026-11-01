/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ScriptExchange.cpp -- script export and import.

#include "Core/ScriptExchange.h"

#include "Core/SidesCommands.h"

#include <algorithm>
#include <map>
#include <set>

namespace MapEditorCore
{

namespace
{

std::optional<std::int32_t> waypointId(const MapModel::MapObject &o)
{
	return o.properties.getInt("waypointID");
}

// What the exported scripts name (ScriptDialog::scanParmForWaypointsAndTeams).
struct Marks
{
	std::set<std::size_t> objects;
	std::set<std::string> teams;
	std::set<std::string> areas;
};

void markWaypoint(const MapModel::Map &map, std::size_t index, Marks &marks)
{
	if (!marks.objects.insert(index).second)
		return;
	// Waypoints linked to it go along, so paths stay whole.
	const auto id = waypointId((*map.objects)[index]);
	if (!id || !map.waypointLinks)
		return;
	for (const MapModel::WaypointLink &l : *map.waypointLinks)
	{
		if (l.from != *id && l.to != *id)
			continue;
		const std::int32_t other = l.from == *id ? l.to : l.from;
		for (std::size_t i = 0; i < map.objects->size(); ++i)
			if (waypointId((*map.objects)[i]) == other)
				markWaypoint(map, i, marks);
	}
}

void scanParameter(const MapModel::Map &map, const MapModel::ScriptParameter &p, const ScriptExport &options, Marks &marks)
{
	if (!map.objects)
		return;
	const std::vector<MapModel::MapObject> &objects = *map.objects;
	if (p.type == Param::WAYPOINT && options.waypoints)
	{
		for (std::size_t i = 0; i < objects.size(); ++i)
			if (waypointId(objects[i]) && objects[i].properties.getAscii("waypointName") == p.text)
				markWaypoint(map, i, marks);
	}
	else if (p.type == Param::WAYPOINT_PATH && options.waypoints)
	{
		for (std::size_t i = 0; i < objects.size(); ++i)
		{
			if (!waypointId(objects[i]))
				continue;
			for (const char *key : {"waypointPathLabel1", "waypointPathLabel2", "waypointPathLabel3"})
				if (objects[i].properties.getAscii(key) == p.text)
					markWaypoint(map, i, marks);
		}
	}
	else if (p.type == Param::TEAM)
	{
		marks.teams.insert(p.text);
		if (options.units)
			for (std::size_t i = 0; i < objects.size(); ++i)
				if (objects[i].properties.getAscii("originalOwner") == p.text)
					marks.objects.insert(i);
	}
	else if (p.type == Param::UNIT && options.units)
	{
		for (std::size_t i = 0; i < objects.size(); ++i)
			if (objects[i].properties.getAscii("objectName") == p.text)
			{
				marks.objects.insert(i);
				marks.teams.insert(objects[i].properties.getAscii("originalOwner").value_or(""));
			}
	}
	else if (p.type == Param::TRIGGER_AREA && options.triggers)
		marks.areas.insert(p.text);
}

void scanList(const MapModel::Map &map, const MapModel::ScriptList &list, const ScriptExport &options, Marks &marks)
{
	const auto script = [&](const MapModel::Script &s) {
		for (const MapModel::OrCondition &o : s.conditions)
			for (const MapModel::ScriptCall &c : o.conditions)
				for (const MapModel::ScriptParameter &p : c.parameters)
					scanParameter(map, p, options, marks);
		for (const auto *calls : {&s.actions, &s.falseActions})
			for (const MapModel::ScriptCall &c : *calls)
				for (const MapModel::ScriptParameter &p : c.parameters)
					scanParameter(map, p, options, marks);
	};
	for (const MapModel::Script &s : list.scripts)
		script(s);
	for (const MapModel::ScriptGroup &g : list.groups)
		for (const MapModel::Script &s : g.scripts)
			script(s);
}

std::string uniqueWaypointName(const MapModel::Map &map, const std::string &base)
{
	std::set<std::string> used;
	if (map.objects)
		for (const MapModel::MapObject &o : *map.objects)
			if (waypointId(o))
				used.insert(o.properties.getAscii("waypointName").value_or(""));
	std::string name = base + "-imp";
	for (int n = 2; used.count(name); ++n)
		name = base + "-imp" + std::to_string(n);
	return name;
}

} // namespace

std::optional<MapModel::ScriptFile> exportScripts(const MapModel::Map &map, const ScriptExport &options)
{
	MapModel::ScriptFile file;
	const MapModel::Sides empty;
	const MapModel::Sides &sides = map.sides ? *map.sides : empty;
	Marks marks;
	if (options.allScripts)
	{
		for (const MapModel::Side &side : sides.sides)
			file.lists.push_back(side.scripts);
		// Everything goes: every waypoint, every area but water, every team.
		if (map.objects && options.waypoints)
			for (std::size_t i = 0; i < map.objects->size(); ++i)
				if (waypointId((*map.objects)[i]))
					marks.objects.insert(i);
		if (map.polygons && options.triggers)
			for (const MapModel::PolygonArea &a : *map.polygons)
				marks.areas.insert(a.name);
		for (const MapModel::PropertyMap &team : sides.teams)
			marks.teams.insert(teamName(team));
	}
	else
	{
		const ScriptPath &at = options.selection;
		MapModel::ScriptList list;
		if (const MapModel::Script *s = scriptAt(sides, at))
			list.scripts.push_back(*s);
		else if (const MapModel::ScriptGroup *g = groupAt(sides, at))
			list.groups.push_back(*g);
		else if (at.side >= 0 && at.side < static_cast<int>(sides.sides.size()))
			list = sides.sides[static_cast<std::size_t>(at.side)].scripts;
		else
			return std::nullopt;
		file.lists.push_back(std::move(list));
	}
	for (const MapModel::ScriptList &l : file.lists)
		scanList(map, l, options, marks);

	if (options.allScripts || options.sides)
	{
		for (const MapModel::Side &side : sides.sides)
		{
			file.playerNames.push_back(playerName(side));
			if (options.sides)
				file.players.push_back(side.properties);
		}
	}
	else
		file.playerNames.push_back(MapModel::SCRIPT_FILE_SELECTION);

	std::set<std::int32_t> exportedIds;
	if (map.objects)
		for (std::size_t i : marks.objects)
		{
			file.objects.push_back((*map.objects)[i]);
			if (const auto id = waypointId((*map.objects)[i]))
				exportedIds.insert(*id);
		}
	if (map.polygons)
		for (const MapModel::PolygonArea &a : *map.polygons)
			if (!a.isWater && marks.areas.count(a.name))
				file.polygons.push_back(a);
	for (const MapModel::PropertyMap &team : sides.teams)
		if (marks.teams.count(teamName(team)))
			file.teams.push_back(team);
	if (map.waypointLinks)
		for (const MapModel::WaypointLink &l : *map.waypointLinks)
			if (exportedIds.count(l.from) && exportedIds.count(l.to))
				file.waypointLinks.push_back(l);
	return file;
}

ScriptImportReport importScripts(MapModel::Map &map, const MapModel::ScriptFile &file, int selectedSide)
{
	ScriptImportReport report;
	if (!map.sides)
		map.sides = MapModel::Sides{};
	MapModel::Sides &sides = *map.sides;
	if (!map.objects)
		map.objects = std::vector<MapModel::MapObject>{};
	if (!map.polygons)
		map.polygons = std::vector<MapModel::PolygonArea>{};
	if (!map.waypointLinks)
		map.waypointLinks = std::vector<MapModel::WaypointLink>{};

	// Players first, so their teams and scripts find them.
	for (std::size_t i = 0; i < file.players.size() && i < file.playerNames.size(); ++i)
	{
		const std::string &name = file.playerNames[i];
		if (findPlayer(sides, name) >= 0)
			continue;
		if (static_cast<int>(sides.sides.size()) >= MAX_PLAYERS - 1)
		{
			report.messages.push_back("No room for player " + name + ".");
			continue;
		}
		MapModel::Side side;
		side.properties = file.players[i];
		sides.sides.push_back(std::move(side));
		++report.players;
	}
	validateSides(sides);

	// Objects: waypoints get new ids after the map's; one at the same place with the same template is there already.
	std::int32_t base = 1;
	for (const MapModel::MapObject &o : *map.objects)
		if (const auto id = waypointId(o))
			base = std::max(base, *id + 1);
	std::map<std::int32_t, std::int32_t> ids;
	std::int32_t next = base;
	for (const MapModel::MapObject &read : file.objects)
	{
		MapModel::MapObject o = read;
		const auto oldId = waypointId(o);
		const MapModel::MapObject *same = nullptr;
		for (const MapModel::MapObject &existing : *map.objects)
			if (existing.x == o.x && existing.y == o.y && existing.templateName == o.templateName && waypointId(existing).has_value() == oldId.has_value())
			{
				same = &existing;
				break;
			}
		if (same)
		{
			if (oldId)
				ids[*oldId] = *waypointId(*same);
			continue;
		}
		if (oldId)
		{
			ids[*oldId] = next;
			o.properties.set("waypointID", next++);
			const std::string name = o.properties.getAscii("waypointName").value_or("");
			const bool taken = std::any_of(map.objects->begin(), map.objects->end(), [&name](const MapModel::MapObject &e) {
				return waypointId(e) && e.properties.getAscii("waypointName") == name;
			});
			if (taken)
			{
				const std::string renamed = uniqueWaypointName(map, name);
				report.messages.push_back("Waypoint " + name + " exists already; the imported one is " + renamed + ".");
				o.properties.set("waypointName", renamed);
			}
		}
		map.objects->push_back(std::move(o));
		++report.objects;
	}
	for (const MapModel::WaypointLink &l : file.waypointLinks)
	{
		const auto from = ids.find(l.from);
		const auto to = ids.find(l.to);
		if (from == ids.end() || to == ids.end())
			continue;
		const MapModel::WaypointLink link{from->second, to->second};
		if (std::find(map.waypointLinks->begin(), map.waypointLinks->end(), link) == map.waypointLinks->end())
			map.waypointLinks->push_back(link);
	}

	// Areas with a name the map has are dropped; the others get new ids.
	std::int32_t areaId = 1;
	for (const MapModel::PolygonArea &a : *map.polygons)
		areaId = std::max(areaId, a.id + 1);
	for (const MapModel::PolygonArea &read : file.polygons)
	{
		if (std::any_of(map.polygons->begin(), map.polygons->end(), [&read](const MapModel::PolygonArea &a) { return a.name == read.name; }))
		{
			report.messages.push_back("Area " + read.name + " exists already; the imported one is dropped.");
			continue;
		}
		MapModel::PolygonArea a = read;
		a.id = areaId++;
		map.polygons->push_back(std::move(a));
		++report.areas;
	}

	// Teams the map has stay as they are; a team whose owner is missing goes to the neutral player.
	for (const MapModel::PropertyMap &read : file.teams)
	{
		if (findTeam(sides, teamName(read)) >= 0)
			continue;
		MapModel::PropertyMap team = read;
		if (findPlayer(sides, teamOwner(team)) < 0)
		{
			report.messages.push_back("Team " + teamName(team) + ": player " + teamOwner(team) + " does not exist; the team goes to the neutral player.");
			team.set("teamOwner", std::string());
		}
		sides.teams.push_back(std::move(team));
		++report.teams;
	}
	validateSides(sides);

	// Scripts: a selection into the chosen player, lists of several players into the player of the same name.
	const bool selection = file.lists.size() == 1 && (file.playerNames.empty() || file.playerNames[0] == MapModel::SCRIPT_FILE_SELECTION);
	for (std::size_t i = 0; i < file.lists.size(); ++i)
	{
		const MapModel::ScriptList &list = file.lists[i];
		if (list.scripts.empty() && list.groups.empty())
			continue;
		int target = -1;
		if (selection)
			target = std::clamp(selectedSide, 0, static_cast<int>(sides.sides.size()) - 1);
		else if (i < file.playerNames.size())
			target = findPlayer(sides, file.playerNames[i]);
		if (target < 0)
		{
			report.messages.push_back("Player " + (i < file.playerNames.size() ? file.playerNames[i] : std::to_string(i)) +
				" does not exist; its scripts are left out.");
			continue;
		}
		MapModel::ScriptList &into = sides.sides[static_cast<std::size_t>(target)].scripts;
		into.scripts.insert(into.scripts.begin(), list.scripts.begin(), list.scripts.end());
		into.groups.insert(into.groups.begin(), list.groups.begin(), list.groups.end());
		report.scripts += static_cast<int>(list.scripts.size());
		for (const MapModel::ScriptGroup &g : list.groups)
			report.scripts += static_cast<int>(g.scripts.size());
		report.groups += static_cast<int>(list.groups.size());
	}
	return report;
}

Changes EditMapParts::changes() const
{
	Changes c;
	c.sides = true;
	c.scripts = true;
	c.reindexed = true;
	c.areas = true;
	c.links = true;
	return c;
}

void EditMapParts::set(MapModel::Map &map, const Parts &parts)
{
	map.sides = parts.sides;
	map.objects = parts.objects;
	map.polygons = parts.polygons;
	map.waypointLinks = parts.waypointLinks;
}

EditMapParts::Parts mapParts(const MapModel::Map &map)
{
	return {map.sides, map.objects, map.polygons, map.waypointLinks};
}

std::unique_ptr<EditMapParts> mapPartsStep(const MapModel::Map &map, EditMapParts::Parts before, std::string name)
{
	EditMapParts::Parts after = mapParts(map);
	if (after.sides == before.sides && after.objects == before.objects && after.polygons == before.polygons && after.waypointLinks == before.waypointLinks)
		return nullptr;
	return std::make_unique<EditMapParts>(std::move(name), std::move(before), std::move(after));
}

} // namespace MapEditorCore
