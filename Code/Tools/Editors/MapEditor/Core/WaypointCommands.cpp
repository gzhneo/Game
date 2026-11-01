/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WaypointCommands.cpp -- see WaypointCommands.h.

#include "Core/WaypointCommands.h"

#include <algorithm>
#include <map>
#include <utility>

namespace MapEditorCore
{

bool isWaypoint(const MapModel::MapObject &o)
{
	return o.properties.getInt("waypointID").has_value();
}

void EditWaypoints::apply(MapModel::Map &map)
{
	map.waypointLinks = m_linksAfter;
	for (const EditObjects::Edit &e : m_edits)
	{
		if (map.objects && e.index >= 0 && static_cast<std::size_t>(e.index) < map.objects->size())
			(*map.objects)[static_cast<std::size_t>(e.index)] = e.after;
	}
}

void EditWaypoints::revert(MapModel::Map &map)
{
	map.waypointLinks = m_linksBefore;
	for (const EditObjects::Edit &e : m_edits)
	{
		if (map.objects && e.index >= 0 && static_cast<std::size_t>(e.index) < map.objects->size())
			(*map.objects)[static_cast<std::size_t>(e.index)] = e.before;
	}
}

Changes EditWaypoints::changes() const
{
	Changes c;
	c.links = true;
	for (const EditObjects::Edit &e : m_edits)
		c.objects.push_back(e.index);
	return c;
}

namespace
{

const MapModel::MapObject *waypointAt(const MapModel::Map &map, std::int32_t index)
{
	if (!map.objects || index < 0 || static_cast<std::size_t>(index) >= map.objects->size())
		return nullptr;
	const MapModel::MapObject &o = (*map.objects)[static_cast<std::size_t>(index)];
	return isWaypoint(o) ? &o : nullptr;
}

std::int32_t idOf(const MapModel::MapObject &o)
{
	return o.properties.getInt("waypointID").value_or(-1);
}

bool linked(const std::vector<MapModel::WaypointLink> &links, std::int32_t a, std::int32_t b)
{
	return std::any_of(links.begin(), links.end(), [a, b](const MapModel::WaypointLink &l) {
		return (l.from == a && l.to == b) || (l.from == b && l.to == a);
	});
}

// Waypoints on the path of index under the given links (index first).
std::vector<std::int32_t> pathOf(const MapModel::Map &map, const std::vector<MapModel::WaypointLink> &links, std::int32_t index)
{
	std::map<std::int32_t, std::int32_t> indexById;
	for (std::size_t i = 0; map.objects && i < map.objects->size(); ++i)
	{
		if (isWaypoint((*map.objects)[i]))
			indexById[idOf((*map.objects)[i])] = static_cast<std::int32_t>(i);
	}
	std::vector<std::int32_t> path;
	const MapModel::MapObject *start = waypointAt(map, index);
	if (!start)
		return path;
	std::vector<std::int32_t> ids{idOf(*start)};
	for (std::size_t n = 0; n < ids.size(); ++n)
	{
		for (const MapModel::WaypointLink &l : links)
		{
			const std::int32_t other = l.from == ids[n] ? l.to : (l.to == ids[n] ? l.from : -1);
			if (other >= 0 && indexById.count(other) && std::find(ids.begin(), ids.end(), other) == ids.end())
				ids.push_back(other);
		}
	}
	for (std::int32_t id : ids)
		path.push_back(indexById[id]);
	return path;
}

MapModel::MapObject withPathProperties(MapModel::MapObject o, const MapModel::MapObject &source)
{
	for (const char *key : PATH_KEYS)
	{
		if (const MapModel::PropertyValue *value = source.properties.find(key))
			o.properties.set(key, *value);
		else
			o.properties.erase(key);
	}
	return o;
}

// Every waypoint on the path of index (under links) takes the path properties of source.
std::vector<EditObjects::Edit> copyPathProperties(const MapModel::Map &map, const std::vector<MapModel::WaypointLink> &links,
	std::int32_t index, const MapModel::MapObject &source)
{
	std::vector<EditObjects::Edit> edits;
	for (std::int32_t i : pathOf(map, links, index))
	{
		const MapModel::MapObject &o = (*map.objects)[static_cast<std::size_t>(i)];
		MapModel::MapObject after = withPathProperties(o, source);
		if (after != o)
			edits.push_back({i, o, std::move(after)});
	}
	return edits;
}

} // namespace

std::vector<std::int32_t> linkedWaypoints(const MapModel::Map &map, std::int32_t index)
{
	std::vector<std::int32_t> result;
	const MapModel::MapObject *w = waypointAt(map, index);
	if (!w || !map.waypointLinks)
		return result;
	const std::int32_t id = idOf(*w);
	for (std::size_t i = 0; i < map.objects->size(); ++i)
	{
		const MapModel::MapObject &o = (*map.objects)[i];
		if (static_cast<std::int32_t>(i) != index && isWaypoint(o) && linked(*map.waypointLinks, id, idOf(o)))
			result.push_back(static_cast<std::int32_t>(i));
	}
	return result;
}

std::vector<std::int32_t> waypointPath(const MapModel::Map &map, std::int32_t index)
{
	return pathOf(map, map.waypointLinks.value_or(std::vector<MapModel::WaypointLink>{}), index);
}

std::unique_ptr<InsertObjects> newWaypoint(const MapModel::Map &map, float x, float y, std::int32_t linkFrom)
{
	// pasteObjects gives the waypoint its ID and name; -1 stands for the new ID in the link.
	MapModel::MapObject w;
	w.x = x;
	w.y = y;
	w.templateName = WAYPOINT_TEMPLATE;
	w.properties.set("originalOwner", MapModel::PropertyValue(std::string("team")));
	w.properties.set("waypointID", std::int32_t(-1));
	w.properties.set("waypointName", MapModel::PropertyValue(std::string()));
	Clipboard clipboard;
	if (const MapModel::MapObject *from = waypointAt(map, linkFrom))
	{
		w = withPathProperties(std::move(w), *from);
		clipboard.links.push_back({idOf(*from), -1});
	}
	clipboard.objects.push_back(std::move(w));
	return pasteObjects(map, clipboard, 0.0f, 0.0f, "Add waypoint");
}

std::unique_ptr<EditWaypoints> toggleWaypointLink(const MapModel::Map &map, std::int32_t from, std::int32_t to)
{
	const MapModel::MapObject *a = waypointAt(map, from);
	const MapModel::MapObject *b = waypointAt(map, to);
	if (!a || !b || from == to)
		return nullptr;
	const std::int32_t idA = idOf(*a);
	const std::int32_t idB = idOf(*b);
	std::vector<MapModel::WaypointLink> links = map.waypointLinks.value_or(std::vector<MapModel::WaypointLink>{});
	if (linked(links, idA, idB))
	{
		std::erase_if(links, [idA, idB](const MapModel::WaypointLink &l) {
			return (l.from == idA && l.to == idB) || (l.from == idB && l.to == idA);
		});
		return std::make_unique<EditWaypoints>("Unlink waypoints", map.waypointLinks, std::move(links), std::vector<EditObjects::Edit>{});
	}
	links.push_back({idA, idB});
	std::vector<EditObjects::Edit> edits = copyPathProperties(map, links, to, *a);
	return std::make_unique<EditWaypoints>("Link waypoints", map.waypointLinks, std::move(links), std::move(edits));
}

std::unique_ptr<EditWaypoints> chainWaypoints(const MapModel::Map &map, const std::vector<std::int32_t> &indices)
{
	std::vector<std::int32_t> waypoints;
	for (std::int32_t i : indices)
	{
		if (waypointAt(map, i) && std::find(waypoints.begin(), waypoints.end(), i) == waypoints.end())
			waypoints.push_back(i);
	}
	if (waypoints.size() < 2)
		return nullptr;
	std::vector<MapModel::WaypointLink> links = map.waypointLinks.value_or(std::vector<MapModel::WaypointLink>{});
	bool added = false;
	for (std::size_t n = 1; n < waypoints.size(); ++n)
	{
		const std::int32_t a = idOf(*waypointAt(map, waypoints[n - 1]));
		const std::int32_t b = idOf(*waypointAt(map, waypoints[n]));
		if (!linked(links, a, b))
		{
			links.push_back({a, b});
			added = true;
		}
	}
	if (!added)
		return nullptr;
	std::vector<EditObjects::Edit> edits = copyPathProperties(map, links, waypoints[0], *waypointAt(map, waypoints[0]));
	return std::make_unique<EditWaypoints>("Link waypoints", map.waypointLinks, std::move(links), std::move(edits));
}

std::unique_ptr<EditWaypoints> unlinkWaypoints(const MapModel::Map &map, const std::vector<std::int32_t> &indices)
{
	if (!map.waypointLinks)
		return nullptr;
	std::vector<std::int32_t> ids;
	for (std::int32_t i : indices)
	{
		if (const MapModel::MapObject *w = waypointAt(map, i))
			ids.push_back(idOf(*w));
	}
	const auto among = [&ids](std::int32_t id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
	std::vector<MapModel::WaypointLink> links = *map.waypointLinks;
	if (std::erase_if(links, [&among](const MapModel::WaypointLink &l) { return among(l.from) && among(l.to); }) == 0)
		return nullptr;
	return std::make_unique<EditWaypoints>("Unlink waypoints", map.waypointLinks, std::move(links), std::vector<EditObjects::Edit>{});
}

std::unique_ptr<EditWaypoints> setPathProperty(const MapModel::Map &map, std::int32_t index, const std::string &key,
	const std::optional<MapModel::PropertyValue> &value)
{
	std::vector<EditObjects::Edit> edits;
	for (std::int32_t i : waypointPath(map, index))
	{
		const MapModel::MapObject &o = (*map.objects)[static_cast<std::size_t>(i)];
		MapModel::MapObject after = o;
		if (value)
			after.properties.set(key, *value);
		else
			after.properties.erase(key);
		if (after != o)
			edits.push_back({i, o, std::move(after)});
	}
	if (edits.empty())
		return nullptr;
	return std::make_unique<EditWaypoints>("Change path", map.waypointLinks, map.waypointLinks, std::move(edits));
}

} // namespace MapEditorCore
