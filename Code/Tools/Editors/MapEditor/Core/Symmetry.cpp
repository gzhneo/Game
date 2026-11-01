/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Core/Symmetry.h"

#include "Core/Fairness.h"
#include "Core/TextureBlend.h"
#include "Core/TextureCommands.h"
#include "Core/WaypointCommands.h"
#include "Model/MapIO.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace MapEditorCore
{

namespace
{

constexpr float CELL = 10.0f;
constexpr float PI = 3.14159265358979f;
constexpr float ON_LINE = 0.01f;

// Which side of the middle line u lies on: 1 past it, -1 before it, 0 on it.
int side(float u, float length)
{
	const float d = u - length * 0.5f;
	return d > ON_LINE ? 1 : d < -ON_LINE ? -1 : 0;
}

struct Transform
{
	bool flipX = false;
	bool flipY = false;
};

// The images of the source part: one for the halves, three for the quarters.
std::vector<Transform> transforms(SymmetryMode mode)
{
	switch (mode)
	{
		case SymmetryMode::MirrorLeftRight: return {{true, false}};
		case SymmetryMode::MirrorBottomTop: return {{false, true}};
		case SymmetryMode::Rotate180: return {{true, true}};
		case SymmetryMode::FourQuadrants: return {{true, false}, {false, true}, {true, true}};
	}
	return {};
}

float turned(float angle, const Transform &t)
{
	float a = angle;
	if (t.flipX)
		a = PI - a;
	if (t.flipY)
		a = -a;
	while (a > PI)
		a -= 2.0f * PI;
	while (a <= -PI)
		a += 2.0f * PI;
	return a;
}

std::size_t at(int width, int x, int y)
{
	return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
}

} // namespace

bool symmetrySource(SymmetryMode mode, bool fromOtherSide, float width, float height, float x, float y, float &srcX, float &srcY)
{
	const int past = fromOtherSide ? -1 : 1;
	const int sx = side(x, width);
	const int sy = side(y, height);
	bool target = false;
	bool flipX = false;
	bool flipY = false;
	switch (mode)
	{
		case SymmetryMode::MirrorLeftRight:
			target = flipX = sx == past;
			break;
		case SymmetryMode::MirrorBottomTop:
			target = flipY = sy == past;
			break;
		case SymmetryMode::Rotate180:
			target = flipX = flipY = sx == past || (sx == 0 && sy == past);
			break;
		case SymmetryMode::FourQuadrants:
			flipX = sx == past;
			flipY = sy == past;
			target = flipX || flipY;
			break;
	}
	srcX = flipX ? width - x : x;
	srcY = flipY ? height - y : y;
	return target;
}

std::vector<float> symmetryLines(const MapModel::HeightMap &hm, SymmetryMode mode)
{
	const float w = static_cast<float>(hm.width - 1 - 2 * hm.borderSize) * CELL;
	const float h = static_cast<float>(hm.height - 1 - 2 * hm.borderSize) * CELL;
	std::vector<float> lines;
	if (mode != SymmetryMode::MirrorBottomTop)
		lines.insert(lines.end(), {w * 0.5f, 0.0f, w * 0.5f, h});
	if (mode == SymmetryMode::MirrorBottomTop || mode == SymmetryMode::FourQuadrants)
		lines.insert(lines.end(), {0.0f, h * 0.5f, w, h * 0.5f});
	return lines;
}

std::unique_ptr<ResizeMap> symmetryStep(const MapModel::Map &map, const SymmetryOptions &options)
{
	if (!map.heightMap || map.heightMap->width < 4 || map.heightMap->height < 4)
		return nullptr;
	const ResizeMap::Content before = mapContent(map);
	ResizeMap::Content after = before;
	const int w = map.heightMap->width;
	const int h = map.heightMap->height;
	const int border = map.heightMap->borderSize;

	if (options.terrain)
	{
		// Heights per grid point, 0 .. width - 1.
		MapModel::HeightMap &hm = *after.heightMap;
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				float sx = 0.0f, sy = 0.0f;
				if (symmetrySource(options.mode, options.fromOtherSide, static_cast<float>(w - 1), static_cast<float>(h - 1), static_cast<float>(x), static_cast<float>(y), sx, sy))
					hm.heights[at(w, x, y)] = before.heightMap->heights[at(w, static_cast<int>(std::lround(sx)), static_cast<int>(std::lround(sy)))];
			}
		// Textures and cliff flags per cell, 0 .. width - 2: the class of the source cell with the tile
		// of the target's place in the class's pattern; blends are made anew below.
		if (after.blendTiles && after.blendTiles->tiles.size() >= at(w, 0, h))
		{
			MapModel::BlendTiles &bt = *after.blendTiles;
			const MapModel::BlendTiles &old = *before.blendTiles;
			std::set<int> classes;
			CellRect changed;
			for (int y = 0; y + 1 < h; ++y)
				for (int x = 0; x + 1 < w; ++x)
				{
					float sx = 0.0f, sy = 0.0f;
					if (!symmetrySource(options.mode, options.fromOtherSide, static_cast<float>(w - 2), static_cast<float>(h - 2), static_cast<float>(x), static_cast<float>(y), sx, sy))
						continue;
					const int srcX = static_cast<int>(std::lround(sx));
					const int srcY = static_cast<int>(std::lround(sy));
					const int textureClass = classOfTile(old, old.tiles[at(w, srcX, srcY)]);
					if (textureClass >= 0)
					{
						bt.tiles[at(w, x, y)] = tileForClass(bt.textureClasses[static_cast<std::size_t>(textureClass)], x, y);
						classes.insert(textureClass);
					}
					if (bt.blends.size() > at(w, x, y))
						bt.blends[at(w, x, y)] = 0;
					if (bt.extraBlends.size() > at(w, x, y))
						bt.extraBlends[at(w, x, y)] = 0;
					if (bt.cliffInfoIndices.size() > at(w, x, y))
						bt.cliffInfoIndices[at(w, x, y)] = 0;
					if (!old.cliffState.empty())
						MapModel::setCliffFlag(bt.cliffState, w, x, y, MapModel::cliffFlag(old.cliffState, w, srcX, srcY));
					changed.unite(CellRect{x, y, x, y});
				}
			for (int textureClass : classes)
				softenEdges(bt, w, h, textureClass, changed);
		}
	}

	if (options.objects && before.objects)
	{
		const float lengthX = static_cast<float>(w - 1 - 2 * border) * CELL;
		const float lengthY = static_cast<float>(h - 1 - 2 * border) * CELL;
		const std::vector<MapModel::MapObject> &objects = *before.objects;
		std::vector<MapModel::MapObject> kept;
		std::set<std::int32_t> removedIds;
		std::int32_t nextId = 0;
		std::set<int> starts;
		for (const MapModel::MapObject &o : objects)
			nextId = std::max(nextId, o.properties.getInt("waypointID").value_or(0));
		for (const MapModel::MapObject &o : objects)
		{
			float sx = 0.0f, sy = 0.0f;
			if (symmetrySource(options.mode, options.fromOtherSide, lengthX, lengthY, o.x, o.y, sx, sy))
			{
				if (isWaypoint(o))
					removedIds.insert(o.properties.getInt("waypointID").value_or(-1));
				continue;
			}
			kept.push_back(o);
			if (isWaypoint(o))
				if (const int n = startNumber(o.properties.getAscii("waypointName").value_or(std::string())))
					starts.insert(n);
		}
		std::vector<MapModel::MapObject> result = kept;
		std::vector<MapModel::WaypointLink> links;
		if (before.waypointLinks)
			for (const MapModel::WaypointLink &l : *before.waypointLinks)
				if (!removedIds.count(l.from) && !removedIds.count(l.to))
					links.push_back(l);
		const std::vector<Transform> images = transforms(options.mode);
		for (std::size_t k = 0; k < images.size(); ++k)
		{
			const Transform &t = images[k];
			std::map<std::int32_t, std::int32_t> newIds;
			const std::string suffix = "_" + std::to_string(k + 2);
			for (const MapModel::MapObject &o : kept)
			{
				MapModel::MapObject copy = o;
				copy.x = t.flipX ? lengthX - o.x : o.x;
				copy.y = t.flipY ? lengthY - o.y : o.y;
				float sx = 0.0f, sy = 0.0f;
				if (!symmetrySource(options.mode, options.fromOtherSide, lengthX, lengthY, copy.x, copy.y, sx, sy))
					continue;
				copy.angle = turned(o.angle, t);
				if (isWaypoint(o))
				{
					const std::int32_t id = ++nextId;
					newIds[o.properties.getInt("waypointID").value_or(-1)] = id;
					copy.properties.set("waypointID", MapModel::PropertyValue(id));
					const std::string name = o.properties.getAscii("waypointName").value_or(std::string());
					if (startNumber(name))
					{
						int n = 1;
						while (n <= 8 && starts.count(n))
							++n;
						if (n <= 8)
						{
							starts.insert(n);
							copy.properties.set("waypointName", MapModel::PropertyValue("Player_" + std::to_string(n) + "_Start"));
						}
						else
							copy.properties.set("waypointName", MapModel::PropertyValue(name + suffix));
					}
					else if (!name.empty())
						copy.properties.set("waypointName", MapModel::PropertyValue(name + suffix));
				}
				const std::string objectName = o.properties.getAscii("objectName").value_or(std::string());
				if (!objectName.empty())
					copy.properties.set("objectName", MapModel::PropertyValue(objectName + suffix));
				result.push_back(copy);
			}
			if (before.waypointLinks)
				for (const MapModel::WaypointLink &l : *before.waypointLinks)
				{
					const auto from = newIds.find(l.from);
					const auto to = newIds.find(l.to);
					if (from != newIds.end() && to != newIds.end())
						links.push_back(MapModel::WaypointLink{from->second, to->second});
				}
		}
		after.objects = std::move(result);
		if (before.waypointLinks || !links.empty())
			after.waypointLinks = std::move(links);
	}

	if (after.heightMap == before.heightMap && after.blendTiles == before.blendTiles && after.objects == before.objects
		&& after.waypointLinks == before.waypointLinks)
		return nullptr;
	return std::make_unique<ResizeMap>(before, std::move(after), "Symmetry");
}

} // namespace MapEditorCore
