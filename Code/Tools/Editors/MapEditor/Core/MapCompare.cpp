/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapCompare.cpp -- see MapCompare.h.

#include "Core/MapCompare.h"

#include "Core/ScriptCommands.h"
#include "Core/SidesCommands.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>

namespace MapEditorCore
{

namespace
{

using Kind = MapDifference::Kind;
using Key = std::optional<std::string>;

// Objects without an ID count as the same within this distance (world units).
constexpr float POSITION_TOLERANCE = 0.05f;
// Names listed in a detail before it ends with "...".
constexpr std::size_t MAX_DETAIL_NAMES = 4;

MapDifference difference(Kind kind, std::string subject, int count = 0, std::int32_t index = -1, std::string detail = {})
{
	MapDifference d;
	d.kind = kind;
	d.subject = std::move(subject);
	d.count = count;
	d.index = index;
	d.detail = std::move(detail);
	return d;
}

std::string joinNames(const std::vector<std::string> &names)
{
	std::string text;
	for (std::size_t i = 0; i < names.size() && i < MAX_DETAIL_NAMES; ++i)
	{
		if (i > 0)
			text += ", ";
		text += names[i];
	}
	if (names.size() > MAX_DETAIL_NAMES)
		text += ", ...";
	return text;
}

// Keys whose value differs or that only one map has; in the order of a, then those only in b.
std::vector<std::string> changedKeys(const MapModel::PropertyMap &a, const MapModel::PropertyMap &b)
{
	std::vector<std::string> keys;
	for (const MapModel::PropertyMap::Pair &p : a.pairs())
	{
		const MapModel::PropertyValue *v = b.find(p.key);
		if (!v || !(*v == p.value))
			keys.push_back(p.key);
	}
	for (const MapModel::PropertyMap::Pair &p : b.pairs())
		if (!a.find(p.key))
			keys.push_back(p.key);
	return keys;
}

// Pairs the still unpaired entries of a and b with equal keys, first with first; entries without a
// key stay unpaired. match[i] is the partner's index or -1.
void pairByKey(std::size_t countA, std::size_t countB, const std::function<Key(std::size_t)> &keyA,
	const std::function<Key(std::size_t)> &keyB, std::vector<int> &matchA, std::vector<int> &matchB)
{
	std::unordered_map<std::string, std::pair<std::vector<std::size_t>, std::size_t>> waiting;
	for (std::size_t j = 0; j < countB; ++j)
		if (matchB[j] < 0)
			if (Key k = keyB(j))
				waiting[*k].first.push_back(j);
	if (waiting.empty())
		return;
	for (std::size_t i = 0; i < countA; ++i)
	{
		if (matchA[i] >= 0)
			continue;
		const Key k = keyA(i);
		if (!k)
			continue;
		const auto it = waiting.find(*k);
		if (it == waiting.end() || it->second.second >= it->second.first.size())
			continue;
		const std::size_t j = it->second.first[it->second.second++];
		matchA[i] = static_cast<int>(j);
		matchB[j] = static_cast<int>(i);
	}
}

std::int64_t gridCell(float v)
{
	return static_cast<std::int64_t>(std::floor(v / POSITION_TOLERANCE));
}

std::uint64_t gridKey(std::int64_t cx, std::int64_t cy)
{
	return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32) | static_cast<std::uint32_t>(cy);
}

// Pairs the unpaired objects by template and position: each object of a takes the nearest unpaired
// object of b within the tolerance. A grid of tolerance-sized cells keeps it linear.
void pairByPosition(const std::vector<MapModel::MapObject> &a, const std::vector<MapModel::MapObject> &b,
	std::vector<int> &matchA, std::vector<int> &matchB)
{
	std::unordered_map<std::string, std::unordered_map<std::uint64_t, std::vector<std::size_t>>> grid;
	for (std::size_t j = 0; j < b.size(); ++j)
		if (matchB[j] < 0)
			grid[b[j].templateName][gridKey(gridCell(b[j].x), gridCell(b[j].y))].push_back(j);
	if (grid.empty())
		return;
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		if (matchA[i] >= 0)
			continue;
		const auto cells = grid.find(a[i].templateName);
		if (cells == grid.end())
			continue;
		const std::int64_t cx = gridCell(a[i].x);
		const std::int64_t cy = gridCell(a[i].y);
		int best = -1;
		float bestDistance = 0.0f;
		for (std::int64_t dy = -1; dy <= 1; ++dy)
		{
			for (std::int64_t dx = -1; dx <= 1; ++dx)
			{
				const auto cell = cells->second.find(gridKey(cx + dx, cy + dy));
				if (cell == cells->second.end())
					continue;
				for (const std::size_t j : cell->second)
				{
					if (matchB[j] >= 0)
						continue;
					const float distance = std::hypot(a[i].x - b[j].x, a[i].y - b[j].y);
					if (distance > POSITION_TOLERANCE * 1.001f)
						continue;
					if (best < 0 || distance < bestDistance || (distance == bestDistance && static_cast<int>(j) < best))
					{
						best = static_cast<int>(j);
						bestDistance = distance;
					}
				}
			}
		}
		if (best >= 0)
		{
			matchA[i] = best;
			matchB[static_cast<std::size_t>(best)] = static_cast<int>(i);
		}
	}
}

std::string objectSubject(const MapModel::MapObject &o)
{
	const std::optional<std::string> id = o.properties.getAscii("uniqueID");
	return id && !id->empty() ? o.templateName + " (" + *id + ")" : o.templateName;
}

std::string formatPlace(const MapModel::MapObject &o)
{
	char text[96];
	std::snprintf(text, sizeof(text), "%.1f, %.1f, %.1f, %.2f", static_cast<double>(o.x), static_cast<double>(o.y),
		static_cast<double>(o.z), static_cast<double>(o.angle));
	return text;
}

void compareTerrain(const MapModel::Map &current, const MapModel::Map &other, std::vector<MapDifference> &out)
{
	const MapModel::HeightMap empty;
	const MapModel::HeightMap &a = current.heightMap ? *current.heightMap : empty;
	const MapModel::HeightMap &b = other.heightMap ? *other.heightMap : empty;
	if (a.width != b.width || a.height != b.height || a.borderSize != b.borderSize)
	{
		out.push_back(difference(Kind::Size, {}, 0, -1,
			std::to_string(a.width) + " x " + std::to_string(a.height) + " (border " + std::to_string(a.borderSize) + "), other " +
			std::to_string(b.width) + " x " + std::to_string(b.height) + " (border " + std::to_string(b.borderSize) + ")"));
		return;
	}
	if (!current.heightMap && !other.heightMap)
		return;

	const std::size_t samples = std::max(a.heights.size(), b.heights.size());
	std::size_t heights = samples - std::min(a.heights.size(), b.heights.size());
	for (std::size_t i = 0; i < std::min(a.heights.size(), b.heights.size()); ++i)
		heights += a.heights[i] != b.heights[i] ? 1u : 0u;
	if (heights > 0)
		out.push_back(difference(Kind::Heights, {}, static_cast<int>(heights)));

	const std::size_t cellsA = current.blendTiles ? current.blendTiles->tiles.size() : 0;
	const std::size_t cellsB = other.blendTiles ? other.blendTiles->tiles.size() : 0;
	std::size_t textures = 0;
	if (cellsA != cellsB)
		textures = std::max(cellsA, cellsB);
	else if (cellsA > 0)
	{
		// Class of each tile as an index into the current map's classes, found by name for the other
		// map; -1 outside every class, -2 for a class the current map lacks.
		const auto tileClasses = [](const MapModel::BlendTiles &bt, const std::function<int(std::size_t)> &classId) {
			std::vector<int> table;
			for (std::size_t c = 0; c < bt.textureClasses.size(); ++c)
			{
				const MapModel::TextureClass &tc = bt.textureClasses[c];
				if (tc.firstTile < 0 || tc.numTiles <= 0)
					continue;
				const std::size_t end = static_cast<std::size_t>(std::min<std::int64_t>(
					static_cast<std::int64_t>(tc.firstTile) + tc.numTiles, 1 << 16));
				if (table.size() < end)
					table.resize(end, -1);
				for (std::size_t t = static_cast<std::size_t>(tc.firstTile); t < end; ++t)
					table[t] = classId(c);
			}
			return table;
		};
		const MapModel::BlendTiles &ta = *current.blendTiles;
		const MapModel::BlendTiles &tb = *other.blendTiles;
		std::unordered_map<std::string, int> byName;
		for (std::size_t c = ta.textureClasses.size(); c-- > 0;)
			byName[ta.textureClasses[c].name] = static_cast<int>(c);
		const std::vector<int> tableA = tileClasses(ta, [](std::size_t c) { return static_cast<int>(c); });
		const std::vector<int> tableB = tileClasses(tb, [&](std::size_t c) {
			const auto it = byName.find(tb.textureClasses[c].name);
			return it == byName.end() ? -2 : it->second;
		});
		const auto classOf = [](const std::vector<int> &table, std::int16_t tile) {
			const int t = tile >> 2;
			return t >= 0 && static_cast<std::size_t>(t) < table.size() ? table[static_cast<std::size_t>(t)] : -1;
		};
		for (std::size_t i = 0; i < cellsA; ++i)
		{
			const int ca = classOf(tableA, ta.tiles[i]);
			const int cb = classOf(tableB, tb.tiles[i]);
			if (ca == cb)
				continue;
			// Two classes of the current map with one name count as the same.
			if (ca >= 0 && cb >= 0 && ta.textureClasses[static_cast<std::size_t>(ca)].name == ta.textureClasses[static_cast<std::size_t>(cb)].name)
				continue;
			++textures;
		}
	}
	if (textures > 0)
		out.push_back(difference(Kind::Textures, {}, static_cast<int>(textures)));
}

void compareObjects(const MapModel::Map &current, const MapModel::Map &other, std::vector<MapDifference> &out)
{
	static const std::vector<MapModel::MapObject> none;
	const std::vector<MapModel::MapObject> &a = current.objects ? *current.objects : none;
	const std::vector<MapModel::MapObject> &b = other.objects ? *other.objects : none;
	std::vector<int> matchA(a.size(), -1), matchB(b.size(), -1);

	const auto waypointKey = [](const std::vector<MapModel::MapObject> &objects) {
		return [&objects](std::size_t i) -> Key {
			if (const std::optional<std::int32_t> id = objects[i].properties.getInt("waypointID"))
				return std::to_string(*id);
			return std::nullopt;
		};
	};
	const auto uniqueKey = [](const std::vector<MapModel::MapObject> &objects) {
		return [&objects](std::size_t i) -> Key {
			std::optional<std::string> id = objects[i].properties.getAscii("uniqueID");
			if (id && !id->empty())
				return id;
			return std::nullopt;
		};
	};
	pairByKey(a.size(), b.size(), waypointKey(a), waypointKey(b), matchA, matchB);
	pairByKey(a.size(), b.size(), uniqueKey(a), uniqueKey(b), matchA, matchB);
	// The rest, including objects whose uniqueID found no partner (renumbered on another save).
	pairByPosition(a, b, matchA, matchB);

	for (std::size_t i = 0; i < a.size(); ++i)
	{
		const std::int32_t index = static_cast<std::int32_t>(i);
		if (matchA[i] < 0)
		{
			out.push_back(difference(Kind::ObjectAdded, objectSubject(a[i]), 0, index));
			continue;
		}
		const MapModel::MapObject &o = a[i];
		const MapModel::MapObject &p = b[static_cast<std::size_t>(matchA[i])];
		if (o.x != p.x || o.y != p.y || o.z != p.z || o.angle != p.angle)
			out.push_back(difference(Kind::ObjectMoved, objectSubject(o), 0, index, formatPlace(p) + " -> " + formatPlace(o)));
		std::vector<std::string> changed;
		if (o.templateName != p.templateName)
			changed.push_back("template");
		if (o.flags != p.flags)
			changed.push_back("flags");
		const std::vector<std::string> keys = changedKeys(o.properties, p.properties);
		changed.insert(changed.end(), keys.begin(), keys.end());
		if (!changed.empty())
			out.push_back(difference(Kind::ObjectChanged, objectSubject(o), static_cast<int>(changed.size()), index, joinNames(changed)));
	}
	for (std::size_t j = 0; j < b.size(); ++j)
		if (matchB[j] < 0)
			out.push_back(difference(Kind::ObjectRemoved, objectSubject(b[j])));
}

void compareLinks(const MapModel::Map &current, const MapModel::Map &other, std::vector<MapDifference> &out)
{
	const auto linkSet = [](const MapModel::Map &map) {
		std::set<std::pair<std::int32_t, std::int32_t>> links;
		if (map.waypointLinks)
			for (const MapModel::WaypointLink &l : *map.waypointLinks)
				links.insert({l.from, l.to});
		return links;
	};
	const auto a = linkSet(current);
	const auto b = linkSet(other);
	std::size_t count = 0;
	for (const auto &l : a)
		count += b.count(l) ? 0u : 1u;
	for (const auto &l : b)
		count += a.count(l) ? 0u : 1u;
	if (count > 0)
		out.push_back(difference(Kind::WaypointLinks, {}, static_cast<int>(count)));
}

void compareAreas(const MapModel::Map &current, const MapModel::Map &other, std::vector<MapDifference> &out)
{
	static const std::vector<MapModel::PolygonArea> none;
	const std::vector<MapModel::PolygonArea> &a = current.polygons ? *current.polygons : none;
	const std::vector<MapModel::PolygonArea> &b = other.polygons ? *other.polygons : none;
	const auto areaName = [](const MapModel::PolygonArea &area) {
		return area.name.empty() ? "#" + std::to_string(area.id) : area.name;
	};
	std::vector<int> matchA(a.size(), -1), matchB(b.size(), -1);
	pairByKey(a.size(), b.size(), [&](std::size_t i) -> Key { return areaName(a[i]); },
		[&](std::size_t j) -> Key { return areaName(b[j]); }, matchA, matchB);
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		const std::int32_t index = static_cast<std::int32_t>(i);
		if (matchA[i] < 0)
		{
			out.push_back(difference(Kind::AreaAdded, areaName(a[i]), 0, index));
			continue;
		}
		const MapModel::PolygonArea &p = b[static_cast<std::size_t>(matchA[i])];
		if (a[i] == p)
			continue;
		std::vector<std::string> changed;
		if (a[i].points != p.points)
			changed.push_back("points");
		if (a[i].isWater != p.isWater || a[i].isRiver != p.isRiver || a[i].riverStart != p.riverStart)
			changed.push_back("water");
		if (a[i].layer != p.layer)
			changed.push_back("layer");
		if (a[i].id != p.id)
			changed.push_back("id");
		if (a[i].name != p.name)
			changed.push_back("name");
		out.push_back(difference(Kind::AreaChanged, areaName(a[i]), static_cast<int>(changed.size()), index, joinNames(changed)));
	}
	for (std::size_t j = 0; j < b.size(); ++j)
		if (matchB[j] < 0)
			out.push_back(difference(Kind::AreaRemoved, areaName(b[j])));
}

void compareSides(const MapModel::Map &current, const MapModel::Map &other, std::vector<MapDifference> &out)
{
	static const MapModel::Sides none;
	const MapModel::Sides &a = current.sides ? *current.sides : none;
	const MapModel::Sides &b = other.sides ? *other.sides : none;

	std::vector<int> matchA(a.sides.size(), -1), matchB(b.sides.size(), -1);
	pairByKey(a.sides.size(), b.sides.size(), [&](std::size_t i) -> Key { return playerName(a.sides[i]); },
		[&](std::size_t j) -> Key { return playerName(b.sides[j]); }, matchA, matchB);
	for (std::size_t i = 0; i < a.sides.size(); ++i)
		if (matchA[i] < 0)
			out.push_back(difference(Kind::PlayerAdded, playerName(a.sides[i])));
	for (std::size_t j = 0; j < b.sides.size(); ++j)
		if (matchB[j] < 0)
			out.push_back(difference(Kind::PlayerRemoved, playerName(b.sides[j])));

	matchA.assign(a.teams.size(), -1);
	matchB.assign(b.teams.size(), -1);
	pairByKey(a.teams.size(), b.teams.size(), [&](std::size_t i) -> Key { return teamName(a.teams[i]); },
		[&](std::size_t j) -> Key { return teamName(b.teams[j]); }, matchA, matchB);
	for (std::size_t i = 0; i < a.teams.size(); ++i)
	{
		if (matchA[i] < 0)
		{
			out.push_back(difference(Kind::TeamAdded, teamName(a.teams[i])));
			continue;
		}
		const std::vector<std::string> keys = changedKeys(a.teams[i], b.teams[static_cast<std::size_t>(matchA[i])]);
		if (!keys.empty())
			out.push_back(difference(Kind::TeamChanged, teamName(a.teams[i]), static_cast<int>(keys.size()), -1, joinNames(keys)));
	}
	for (std::size_t j = 0; j < b.teams.size(); ++j)
		if (matchB[j] < 0)
			out.push_back(difference(Kind::TeamRemoved, teamName(b.teams[j])));

	struct Entry
	{
		std::string player;
		const MapModel::Script *script;
	};
	const auto collect = [](const MapModel::Sides &sides) {
		std::vector<Entry> entries;
		forEachScript(sides, [&](const ScriptPath &path, const MapModel::Script &s) {
			entries.push_back({playerName(sides.sides[static_cast<std::size_t>(path.side)]), &s});
		});
		return entries;
	};
	const std::vector<Entry> sa = collect(a);
	const std::vector<Entry> sb = collect(b);
	// Player and script name, separated by a character neither can contain.
	const auto scriptKey = [](const Entry &e) -> Key { return e.player + '\n' + e.script->name; };
	matchA.assign(sa.size(), -1);
	matchB.assign(sb.size(), -1);
	pairByKey(sa.size(), sb.size(), [&](std::size_t i) { return scriptKey(sa[i]); }, [&](std::size_t j) { return scriptKey(sb[j]); },
		matchA, matchB);
	for (std::size_t i = 0; i < sa.size(); ++i)
	{
		if (matchA[i] < 0)
			out.push_back(difference(Kind::ScriptAdded, sa[i].script->name, 0, -1, sa[i].player));
		else if (!(*sa[i].script == *sb[static_cast<std::size_t>(matchA[i])].script))
			out.push_back(difference(Kind::ScriptChanged, sa[i].script->name, 0, -1, sa[i].player));
	}
	for (std::size_t j = 0; j < sb.size(); ++j)
		if (matchB[j] < 0)
			out.push_back(difference(Kind::ScriptRemoved, sb[j].script->name, 0, -1, sb[j].player));
}

} // namespace

std::vector<MapDifference> compareMaps(const MapModel::Map &current, const MapModel::Map &other)
{
	std::vector<MapDifference> out;
	compareTerrain(current, other, out);
	compareObjects(current, other, out);
	compareLinks(current, other, out);
	compareAreas(current, other, out);
	compareSides(current, other, out);
	if (current.lighting != other.lighting)
		out.push_back(difference(Kind::Lighting, {}));
	if (current.worldInfo != other.worldInfo)
	{
		const MapModel::PropertyMap empty;
		const std::vector<std::string> keys = changedKeys(current.worldInfo ? *current.worldInfo : empty,
			other.worldInfo ? *other.worldInfo : empty);
		out.push_back(difference(Kind::WorldInfo, {}, static_cast<int>(keys.size()), -1, joinNames(keys)));
	}
	return out;
}

} // namespace MapEditorCore
