/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapSize.cpp -- a new map and a new size for a map.

#include "Core/MapSize.h"

#include "Core/TextureBlend.h"
#include "Core/TextureCommands.h"
#include "Model/MapIO.h"

#include <algorithm>
#include <cmath>

namespace MapEditorCore
{

namespace
{

constexpr float CELL = 10.0f;	// world units per height sample

bool validSize(int width, int height, int border)
{
	return width >= MIN_MAP_CELLS && width <= MAX_MAP_CELLS && height >= MIN_MAP_CELLS && height <= MAX_MAP_CELLS
		&& border >= 0 && border <= MAX_MAP_BORDER;
}

// First old cell of the new map along one axis (WorldHeightMapEdit::resize).
int firstOldCell(int oldCells, int oldBorder, int newCells, int newBorder, Anchor anchor)
{
	const int sizeChange = (oldCells - 2 * oldBorder) - newCells;
	int offset = oldBorder - newBorder;
	if (anchor == Anchor::End)
		offset += sizeChange;
	else if (anchor == Anchor::Centre)
		offset += sizeChange / 2;
	return offset;
}

// The tile of the same texture at a cell outside the old map, continuing its pattern there.
std::int16_t continuedTile(const MapModel::BlendTiles &tiles, int tile, int oldX, int oldY)
{
	const int classIndex = classOfTile(tiles, tile);
	if (classIndex < 0)
		return static_cast<std::int16_t>(tile);
	const MapModel::TextureClass &c = tiles.textureClasses[static_cast<std::size_t>(classIndex)];
	const int period = 2 * std::max(1, c.width);
	auto wrap = [period](int v) { return ((v % period) + period) % period; };
	return tileForClass(c, wrap(oldX), wrap(oldY));
}

void offsetScripts(MapModel::ScriptList &list, float dx, float dy)
{
	auto calls = [&](std::vector<MapModel::ScriptCall> &cs) {
		for (MapModel::ScriptCall &call : cs)
			for (MapModel::ScriptParameter &p : call.parameters)
				if (p.type == MapModel::PARAMETER_COORD3D)
				{
					p.coord[0] += dx;
					p.coord[1] += dy;
				}
	};
	auto script = [&](MapModel::Script &s) {
		for (MapModel::OrCondition &o : s.conditions)
			calls(o.conditions);
		calls(s.actions);
		calls(s.falseActions);
	};
	for (MapModel::Script &s : list.scripts)
		script(s);
	for (MapModel::ScriptGroup &g : list.groups)
		for (MapModel::Script &s : g.scripts)
			script(s);
}

} // namespace

ResizeMap::Content mapContent(const MapModel::Map &map)
{
	ResizeMap::Content c;
	c.heightMap = map.heightMap;
	c.blendTiles = map.blendTiles;
	c.objects = map.objects;
	c.polygons = map.polygons;
	c.sides = map.sides;
	c.waypointLinks = map.waypointLinks;
	return c;
}

std::vector<std::array<float, 2>> startPositions(int width, int height, int players, StartLayout layout)
{
	std::vector<std::array<float, 2>> out;
	if (players < 1 || players > MAX_START_POSITIONS || width <= 0 || height <= 0)
		return out;
	const float w = static_cast<float>(width) * CELL;
	const float h = static_cast<float>(height) * CELL;
	// Keep the bases clear of the map edge: a sixth of the shorter side, at least 12 cells.
	const float inset = std::min(std::max(std::min(w, h) / 6.0f, 12.0f * CELL), std::min(w, h) / 2.0f);
	const float x0 = inset, x1 = w - inset, y0 = inset, y1 = h - inset;
	const float mx = w / 2.0f, my = h / 2.0f;
	switch (layout)
	{
	case StartLayout::Corners:
	{
		const std::array<std::array<float, 2>, MAX_START_POSITIONS> spots = {{
			{x0, y0}, {x1, y1}, {x0, y1}, {x1, y0}, {x0, my}, {x1, my}, {mx, y0}, {mx, y1}}};
		out.assign(spots.begin(), spots.begin() + players);
		break;
	}
	case StartLayout::Circle:
	{
		constexpr float PI = 3.14159265358979f;
		const float rx = mx - inset, ry = my - inset;
		// Every second player sits opposite the one before, so 1-2, 3-4, ... face each other.
		const int half = (players + 1) / 2;
		for (int i = 0; i < players; ++i)
		{
			const float step = players % 2 == 0 ? PI / static_cast<float>(half) : 2.0f * PI / static_cast<float>(players);
			const float a = players % 2 == 0
				? 1.25f * PI + step * static_cast<float>(i / 2) + (i % 2 == 1 ? PI : 0.0f)
				: 1.25f * PI + step * static_cast<float>(i);
			out.push_back({mx + rx * std::cos(a), my + ry * std::sin(a)});
		}
		break;
	}
	case StartLayout::TwoSides:
	{
		const int left = (players + 1) / 2;
		const int right = players / 2;
		for (int i = 0; i < players; ++i)
		{
			const bool onLeft = i % 2 == 0;
			const int count = onLeft ? left : right;
			const int index = i / 2;
			const float y = count == 1 ? my : y0 + (y1 - y0) * static_cast<float>(index) / static_cast<float>(count - 1);
			out.push_back({onLeft ? x0 : x1, y});
		}
		break;
	}
	}
	return out;
}

std::optional<MapModel::Map> newMap(const NewMapOptions &options)
{
	using namespace MapModel;
	if (!validSize(options.width, options.height, options.border) || options.texture.empty())
		return std::nullopt;
	const int w = options.width + 2 * options.border;
	const int h = options.height + 2 * options.border;
	const std::size_t cells = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
	Map map;

	HeightMap hm;
	hm.width = w;
	hm.height = h;
	hm.borderSize = options.border;
	hm.boundaries = {{options.width, options.height}};
	hm.heights.assign(cells, static_cast<std::uint8_t>(std::clamp(options.initialHeight, 0, 255)));
	map.heightMap = hm;

	BlendTiles bt;
	const int classIndex = addTextureClass(bt, options.texture, options.textureTiles, options.textureWidth);
	if (classIndex < 0)
		return std::nullopt;
	const TextureClass &c = bt.textureClasses[static_cast<std::size_t>(classIndex)];
	bt.tiles.resize(cells);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			bt.tiles[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] = tileForClass(c, x, y);
	bt.blends.assign(cells, 0);
	bt.extraBlends.assign(cells, 0);
	bt.cliffInfoIndices.assign(cells, 0);
	bt.cliffState.assign(cliffRowBytes(w) * static_cast<std::size_t>(h), 0);
	map.blendTiles = bt;

	PropertyMap world;
	world.set("weather", std::int32_t(0));
	map.worldInfo = world;

	// SidesList::addPlayerByTemplate with no template: the neutral player and its team.
	Sides sides;
	Side neutral;
	neutral.properties.set("playerName", std::string());
	neutral.properties.set("playerIsHuman", false);
	neutral.properties.set("playerDisplayName", std::u16string(u"Neutral"));
	neutral.properties.set("playerFaction", std::string());
	neutral.properties.set("playerAllies", std::string());
	neutral.properties.set("playerEnemies", std::string());
	sides.sides.push_back(neutral);
	PropertyMap team;
	team.set("teamName", std::string("team"));
	team.set("teamOwner", std::string());
	team.set("teamIsSingleton", true);
	sides.teams.push_back(team);
	map.sides = sides;

	std::vector<MapObject> objects;
	const std::vector<std::array<float, 2>> starts = startPositions(options.width, options.height, options.players, options.layout);
	for (std::size_t i = 0; i < starts.size(); ++i)
	{
		MapObject start;
		start.x = starts[i][0];
		start.y = starts[i][1];
		start.templateName = "*Waypoints/Waypoint";
		start.properties.set("originalOwner", PropertyValue(std::string("team")));
		start.properties.set("waypointID", static_cast<std::int32_t>(i + 1));
		start.properties.set("waypointName", PropertyValue("Player_" + std::to_string(i + 1) + "_Start"));
		objects.push_back(std::move(start));
	}
	map.objects = std::move(objects);
	std::vector<PolygonArea> areas;
	if (options.waterHeight)
	{
		PolygonArea water;
		water.name = "Default Water";
		water.id = 1;
		water.isWater = true;
		const std::int32_t x0 = -options.border * static_cast<std::int32_t>(CELL);
		const std::int32_t y0 = x0;
		const std::int32_t x1 = (options.width + options.border) * static_cast<std::int32_t>(CELL);
		const std::int32_t y1 = (options.height + options.border) * static_cast<std::int32_t>(CELL);
		const std::int32_t z = static_cast<std::int32_t>(std::lround(*options.waterHeight));
		water.points = {{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}};
		areas.push_back(water);
	}
	map.polygons = areas;
	map.lighting = options.lighting;
	map.waypointLinks = std::vector<WaypointLink>();
	return map;
}

ResizeOffset resizeOffset(const MapModel::HeightMap &hm, const ResizeOptions &options)
{
	const int ox = firstOldCell(hm.width, hm.borderSize, options.width, options.border, options.x);
	const int oy = firstOldCell(hm.height, hm.borderSize, options.height, options.border, options.y);
	ResizeOffset offset;
	offset.x = static_cast<float>(hm.borderSize - options.border - ox) * CELL;
	offset.y = static_cast<float>(hm.borderSize - options.border - oy) * CELL;
	return offset;
}

int objectsOutsideAfterResize(const MapModel::Map &map, const ResizeOptions &options)
{
	if (!map.heightMap || !map.objects)
		return 0;
	const ResizeOffset d = resizeOffset(*map.heightMap, options);
	const float maxX = static_cast<float>(options.width) * CELL;
	const float maxY = static_cast<float>(options.height) * CELL;
	int outside = 0;
	for (const MapModel::MapObject &o : *map.objects)
	{
		const float x = o.x + d.x;
		const float y = o.y + d.y;
		if (x < 0.0f || y < 0.0f || x > maxX || y > maxY)
			++outside;
	}
	return outside;
}

Changes ResizeMap::changes() const
{
	Changes c;
	c.size = true;
	c.reindexed = true;
	c.terrain = true;
	c.textures = true;
	c.areas = true;
	c.boundaries = true;
	if (m_after.heightMap)
		c.terrainRect = {0, 0, m_after.heightMap->width - 1, m_after.heightMap->height - 1};
	return c;
}

void ResizeMap::set(MapModel::Map &map, const Content &content)
{
	map.heightMap = content.heightMap;
	map.blendTiles = content.blendTiles;
	map.objects = content.objects;
	map.polygons = content.polygons;
	map.sides = content.sides;
	map.waypointLinks = content.waypointLinks;
}

std::unique_ptr<ResizeMap> resizeMap(const MapModel::Map &map, const ResizeOptions &options)
{
	if (!map.heightMap || !validSize(options.width, options.height, options.border))
		return nullptr;
	const MapModel::HeightMap &old = *map.heightMap;
	if (old.width < 2 || old.height < 2 || old.heights.size() < static_cast<std::size_t>(old.width) * static_cast<std::size_t>(old.height))
		return nullptr;
	const int oldPlayableX = old.width - 2 * old.borderSize;
	const int oldPlayableY = old.height - 2 * old.borderSize;
	if (oldPlayableX == options.width && oldPlayableY == options.height && old.borderSize == options.border)
		return nullptr;

	const int w = options.width + 2 * options.border;
	const int h = options.height + 2 * options.border;
	const std::size_t cells = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
	const int ox = firstOldCell(old.width, old.borderSize, options.width, options.border, options.x);
	const int oy = firstOldCell(old.height, old.borderSize, options.height, options.border, options.y);
	const ResizeOffset d = resizeOffset(old, options);

	ResizeMap::Content after = mapContent(map);
	MapModel::HeightMap &hm = *after.heightMap;
	hm.width = w;
	hm.height = h;
	hm.borderSize = options.border;
	hm.heights.assign(cells, 0);
	// Cells beyond the old map continue its edge rows.
	auto oldIndex = [&](int x, int y, bool &inside) {
		const int sx = x + ox;
		const int sy = y + oy;
		inside = sx >= 0 && sy >= 0 && sx < old.width && sy < old.height;
		const int cx = std::clamp(sx, 0, old.width - 1);
		const int cy = std::clamp(sy, 0, old.height - 1);
		return static_cast<std::size_t>(cy) * static_cast<std::size_t>(old.width) + static_cast<std::size_t>(cx);
	};
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			bool inside = false;
			hm.heights[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] = old.heights[oldIndex(x, y, inside)];
		}
	}
	// Playable areas start at the map's origin: the whole old area becomes the whole new one, the
	// rest keeps its size within the new one.
	for (MapModel::Point2 &b : hm.boundaries)
	{
		if (b.x == oldPlayableX && b.y == oldPlayableY)
			b = {options.width, options.height};
		else
			b = {std::min(b.x, options.width), std::min(b.y, options.height)};
	}

	if (after.blendTiles)
	{
		const MapModel::BlendTiles &src = *map.blendTiles;
		MapModel::BlendTiles &bt = *after.blendTiles;
		const std::size_t oldCells = static_cast<std::size_t>(old.width) * static_cast<std::size_t>(old.height);
		if (src.tiles.size() >= oldCells && src.blends.size() >= oldCells && src.extraBlends.size() >= oldCells
			&& src.cliffInfoIndices.size() >= oldCells)
		{
			bt.tiles.assign(cells, 0);
			bt.blends.assign(cells, 0);
			bt.extraBlends.assign(cells, 0);
			bt.cliffInfoIndices.assign(cells, 0);
			std::vector<std::uint8_t> computed(MapModel::cliffRowBytes(w) * static_cast<std::size_t>(h), 0);
			MapModel::updateCliffFlags(hm, computed, 0, 0, w - 1, h - 1);
			const bool keepFlags = src.cliffState.size() >= MapModel::cliffRowBytes(old.width) * static_cast<std::size_t>(old.height);
			bt.cliffState = computed;
			for (int y = 0; y < h; ++y)
			{
				for (int x = 0; x < w; ++x)
				{
					const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
					bool inside = false;
					const std::size_t o = oldIndex(x, y, inside);
					if (inside)
					{
						bt.tiles[i] = src.tiles[o];
						bt.blends[i] = src.blends[o];
						bt.extraBlends[i] = src.extraBlends[o];
						bt.cliffInfoIndices[i] = src.cliffInfoIndices[o];
						// The last old row and column had no cell beyond them; their flags come from the heights.
						if (keepFlags && x + ox < old.width - 1 && y + oy < old.height - 1)
							MapModel::setCliffFlag(bt.cliffState, w, x, y, MapModel::cliffFlag(src.cliffState, old.width, x + ox, y + oy));
					}
					else
						bt.tiles[i] = continuedTile(src, src.tiles[o], x + ox, y + oy);
				}
			}
		}
	}

	if (after.objects)
	{
		for (MapModel::MapObject &o : *after.objects)
		{
			o.x += d.x;
			o.y += d.y;
		}
	}
	if (after.polygons)
	{
		const std::int32_t ix = static_cast<std::int32_t>(std::lround(d.x));
		const std::int32_t iy = static_cast<std::int32_t>(std::lround(d.y));
		for (MapModel::PolygonArea &a : *after.polygons)
			for (auto &p : a.points)
			{
				p[0] += ix;
				p[1] += iy;
			}
	}
	if (after.sides)
	{
		for (MapModel::Side &side : after.sides->sides)
		{
			for (MapModel::BuildListEntry &b : side.buildList)
			{
				b.x += d.x;
				b.y += d.y;
			}
			offsetScripts(side.scripts, d.x, d.y);
		}
	}
	return std::make_unique<ResizeMap>(mapContent(map), std::move(after));
}

} // namespace MapEditorCore
