/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ObjectCommands.cpp -- commands on map objects.

#include "Core/ObjectCommands.h"

#include "Model/MapIO.h"

#include <algorithm>

namespace MapEditorCore
{

namespace
{

MapModel::MapObject *objectAt(MapModel::Map &map, std::int32_t index)
{
	if (!map.objects || index < 0 || static_cast<std::size_t>(index) >= map.objects->size())
		return nullptr;
	return &(*map.objects)[static_cast<std::size_t>(index)];
}

} // namespace

void MoveObjects::apply(MapModel::Map &map)
{
	for (const Move &m : m_moves)
	{
		if (MapModel::MapObject *o = objectAt(map, m.index))
		{
			o->x = m.toX;
			o->y = m.toY;
		}
	}
}

void MoveObjects::revert(MapModel::Map &map)
{
	for (const Move &m : m_moves)
	{
		if (MapModel::MapObject *o = objectAt(map, m.index))
		{
			o->x = m.fromX;
			o->y = m.fromY;
		}
	}
}

std::string MoveObjects::name() const
{
	return m_moves.size() == 1 ? "Move object" : "Move objects";
}

Changes MoveObjects::changes() const
{
	Changes c;
	for (const Move &m : m_moves)
		c.objects.push_back(m.index);
	return c;
}

void EditObjects::apply(MapModel::Map &map)
{
	for (const Edit &e : m_edits)
	{
		if (MapModel::MapObject *o = objectAt(map, e.index))
			*o = e.after;
	}
}

void EditObjects::revert(MapModel::Map &map)
{
	for (const Edit &e : m_edits)
	{
		if (MapModel::MapObject *o = objectAt(map, e.index))
			*o = e.before;
	}
}

Changes EditObjects::changes() const
{
	Changes c;
	c.rebuild = m_rebuildView;
	for (const Edit &e : m_edits)
		c.objects.push_back(e.index);
	return c;
}

DeleteObjects::DeleteObjects(const MapModel::Map &map, std::vector<std::int32_t> indices, std::string name)
	: m_name(std::move(name))
{
	std::sort(indices.begin(), indices.end());
	indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
	std::vector<std::int32_t> waypointIds;
	for (std::int32_t index : indices)
	{
		if (!map.objects || index < 0 || static_cast<std::size_t>(index) >= map.objects->size())
			continue;
		const MapModel::MapObject &o = (*map.objects)[static_cast<std::size_t>(index)];
		m_removed.push_back({index, o});
		if (const auto id = o.properties.getInt("waypointID"))
			waypointIds.push_back(*id);
	}
	if (map.waypointLinks)
	{
		const auto &links = *map.waypointLinks;
		for (std::size_t i = 0; i < links.size(); ++i)
		{
			const bool gone = std::find(waypointIds.begin(), waypointIds.end(), links[i].from) != waypointIds.end()
				|| std::find(waypointIds.begin(), waypointIds.end(), links[i].to) != waypointIds.end();
			if (gone)
			{
				m_links.push_back(links[i]);
				m_linkPositions.push_back(i);
			}
		}
	}
}

void DeleteObjects::apply(MapModel::Map &map)
{
	if (!map.objects)
		return;
	auto &objects = *map.objects;
	// From the back, so the earlier indices stay valid.
	for (auto it = m_removed.rbegin(); it != m_removed.rend(); ++it)
	{
		if (static_cast<std::size_t>(it->index) < objects.size())
			objects.erase(objects.begin() + it->index);
	}
	if (map.waypointLinks)
	{
		auto &links = *map.waypointLinks;
		for (auto it = m_linkPositions.rbegin(); it != m_linkPositions.rend(); ++it)
		{
			if (*it < links.size())
				links.erase(links.begin() + static_cast<std::ptrdiff_t>(*it));
		}
	}
	m_applied = true;
}

void DeleteObjects::revert(MapModel::Map &map)
{
	if (!map.objects)
		map.objects.emplace();
	auto &objects = *map.objects;
	// From the front: each object goes back to its old index.
	for (const Removed &r : m_removed)
	{
		const std::size_t at = std::min(static_cast<std::size_t>(r.index), objects.size());
		objects.insert(objects.begin() + static_cast<std::ptrdiff_t>(at), r.object);
	}
	if (!m_links.empty())
	{
		if (!map.waypointLinks)
			map.waypointLinks.emplace();
		auto &links = *map.waypointLinks;
		for (std::size_t i = 0; i < m_links.size(); ++i)
		{
			const std::size_t at = std::min(m_linkPositions[i], links.size());
			links.insert(links.begin() + static_cast<std::ptrdiff_t>(at), m_links[i]);
		}
	}
	m_applied = false;
}

std::string DeleteObjects::name() const
{
	if (!m_name.empty())
		return m_name;
	return m_removed.size() == 1 ? "Delete object" : "Delete objects";
}

Changes DeleteObjects::changes() const
{
	Changes c;
	c.reindexed = true;
	if (!m_applied)
	{
		for (const Removed &r : m_removed)
			c.objects.push_back(r.index);
	}
	return c;
}

Clipboard copyObjects(const MapModel::Map &map, const std::vector<std::int32_t> &indices)
{
	Clipboard c;
	if (!map.objects)
		return c;
	std::vector<std::int32_t> sorted = indices;
	std::sort(sorted.begin(), sorted.end());
	sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
	std::vector<std::int32_t> ids;
	for (std::int32_t index : sorted)
	{
		if (index < 0 || static_cast<std::size_t>(index) >= map.objects->size())
			continue;
		const MapModel::MapObject &o = (*map.objects)[static_cast<std::size_t>(index)];
		c.objects.push_back(o);
		if (const auto id = o.properties.getInt("waypointID"))
			ids.push_back(*id);
	}
	if (map.waypointLinks)
	{
		for (const MapModel::WaypointLink &link : *map.waypointLinks)
		{
			if (std::find(ids.begin(), ids.end(), link.from) != ids.end() && std::find(ids.begin(), ids.end(), link.to) != ids.end())
				c.links.push_back(link);
		}
	}
	return c;
}

void InsertObjects::apply(MapModel::Map &map)
{
	m_createdObjects = !map.objects;
	if (!map.objects)
		map.objects.emplace();
	m_first = map.objects->size();
	map.objects->insert(map.objects->end(), m_objects.begin(), m_objects.end());
	if (!m_links.empty())
	{
		m_createdLinks = !map.waypointLinks;
		if (!map.waypointLinks)
			map.waypointLinks.emplace();
		map.waypointLinks->insert(map.waypointLinks->end(), m_links.begin(), m_links.end());
	}
	m_applied = true;
}

void InsertObjects::revert(MapModel::Map &map)
{
	// Undo runs in order, so the inserted objects and links are the last ones.
	if (map.objects && map.objects->size() >= m_objects.size())
		map.objects->resize(map.objects->size() - m_objects.size());
	if (map.waypointLinks && map.waypointLinks->size() >= m_links.size())
		map.waypointLinks->resize(map.waypointLinks->size() - m_links.size());
	// Lists the insert created go again, so undo gives back the map as it was.
	if (m_createdObjects && map.objects && map.objects->empty())
		map.objects.reset();
	if (m_createdLinks && map.waypointLinks && map.waypointLinks->empty())
		map.waypointLinks.reset();
	m_applied = false;
}

Changes InsertObjects::changes() const
{
	Changes c;
	c.reindexed = true;
	if (m_applied)
	{
		for (std::size_t i = 0; i < m_objects.size(); ++i)
			c.objects.push_back(static_cast<std::int32_t>(m_first + i));
	}
	return c;
}

namespace
{

// Trailing number of a uniqueID ("TreePine 17" -> 17), or -1.
int trailingNumber(const std::string &id)
{
	const std::size_t space = id.rfind(' ');
	if (space == std::string::npos || space + 1 >= id.size())
		return -1;
	int n = 0;
	for (std::size_t i = space + 1; i < id.size(); ++i)
	{
		if (id[i] < '0' || id[i] > '9')
			return -1;
		n = n * 10 + (id[i] - '0');
	}
	return n;
}

// Template name without its folder ("Folder/TreePine" -> "TreePine").
std::string shortName(const std::string &name)
{
	const std::size_t slash = name.rfind('/');
	return slash == std::string::npos ? name : name.substr(slash + 1);
}

} // namespace

std::unique_ptr<InsertObjects> pasteObjects(const MapModel::Map &map, const Clipboard &clipboard, float dx, float dy, std::string name)
{
	int nextNumber = 0;
	std::int32_t nextWaypoint = 1;
	std::vector<std::string> waypointNames;
	if (map.objects)
	{
		for (const MapModel::MapObject &o : *map.objects)
		{
			if (const auto id = o.properties.getInt("waypointID"))
			{
				nextWaypoint = std::max(nextWaypoint, *id + 1);
				waypointNames.push_back(o.properties.getAscii("waypointName").value_or(""));
			}
			else if (const auto unique = o.properties.getAscii("uniqueID"))
				nextNumber = std::max(nextNumber, trailingNumber(*unique) + 1);
		}
	}
	std::vector<MapModel::MapObject> objects;
	std::vector<std::pair<std::int32_t, std::int32_t>> newIds;	// old waypoint ID -> new one
	for (MapModel::MapObject o : clipboard.objects)
	{
		o.x += dx;
		o.y += dy;
		o.properties.erase("objectName");
		if (const auto oldId = o.properties.getInt("waypointID"))
		{
			const std::int32_t id = nextWaypoint++;
			std::string waypointName = "Waypoint " + std::to_string(id);
			for (std::int32_t n = id; std::find(waypointNames.begin(), waypointNames.end(), waypointName) != waypointNames.end();)
				waypointName = "Waypoint " + std::to_string(++n);
			waypointNames.push_back(waypointName);
			newIds.emplace_back(*oldId, id);
			o.properties.set("waypointID", id);
			o.properties.set("waypointName", waypointName);
			if (o.properties.find("uniqueID"))
				o.properties.set("uniqueID", waypointName);
		}
		else
			o.properties.set("uniqueID", shortName(o.templateName) + " " + std::to_string(nextNumber++));
		objects.push_back(std::move(o));
	}
	const auto mapId = [&newIds](std::int32_t old) {
		for (const auto &[from, to] : newIds)
		{
			if (from == old)
				return to;
		}
		return old;
	};
	std::vector<MapModel::WaypointLink> links;
	for (const MapModel::WaypointLink &link : clipboard.links)
		links.push_back({mapId(link.from), mapId(link.to)});
	return std::make_unique<InsertObjects>(std::move(name), std::move(objects), std::move(links));
}

std::vector<std::uint8_t> clipboardToBytes(const Clipboard &clipboard)
{
	MapModel::Map map;
	map.objects = clipboard.objects;
	if (!clipboard.links.empty())
		map.waypointLinks = clipboard.links;
	MapModel::WriteResult written = MapModel::writeMap(map);
	return written.ok() ? std::move(written.bytes) : std::vector<std::uint8_t>{};
}

std::optional<Clipboard> clipboardFromBytes(std::span<const std::uint8_t> bytes)
{
	if (bytes.empty())
		return std::nullopt;
	MapModel::ReadResult read = MapModel::readMap(bytes);
	if (!read.ok() || !read.map.objects || read.map.objects->empty())
		return std::nullopt;
	Clipboard clipboard;
	clipboard.objects = std::move(*read.map.objects);
	if (read.map.waypointLinks)
		clipboard.links = std::move(*read.map.waypointLinks);
	return clipboard;
}

} // namespace MapEditorCore
