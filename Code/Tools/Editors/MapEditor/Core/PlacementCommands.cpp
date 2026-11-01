/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// PlacementCommands.cpp -- see PlacementCommands.h.

#include "Core/PlacementCommands.h"

#include "Core/AreaCommands.h"

#include <algorithm>
#include <cmath>

namespace MapEditorCore
{

namespace
{

constexpr float CELL = 10.0f;					// world units per height sample
constexpr float PI = 3.14159265358979f;
constexpr float FLAT_TOLERANCE = 0.8f;			// a tree needs a ground normal with at least this z
constexpr int MAX_FENCE_PIECES = 200;

float uniform(std::mt19937 &random, float lo, float hi)
{
	return std::uniform_real_distribution<float>(lo, hi)(random);
}

// Playable extent of the map in world units (the border lies outside 0 .. extent).
void playable(const MapModel::Map &map, float &width, float &height)
{
	width = 0.0f;
	height = 0.0f;
	if (!map.heightMap)
		return;
	width = static_cast<float>(map.heightMap->width - 2 * map.heightMap->borderSize) * CELL;
	height = static_cast<float>(map.heightMap->height - 2 * map.heightMap->borderSize) * CELL;
}

// A tree of the options at the point, facing a random way; empty template if none was chosen.
MapModel::MapObject tree(const GroveOptions &options, float x, float y, std::mt19937 &random)
{
	MapModel::MapObject object;
	int total = 0;
	for (const auto &[name, weight] : options.types)
		total += std::max(weight, 0);
	if (total <= 0)
		return object;
	int pick = std::uniform_int_distribution<int>(0, total - 1)(random);
	for (const auto &[name, weight] : options.types)
	{
		pick -= std::max(weight, 0);
		if (pick < 0)
		{
			object.templateName = name;
			break;
		}
	}
	object.x = x;
	object.y = y;
	object.angle = uniform(random, 0.0f, 2.0f * PI);
	object.properties.set("originalOwner", MapModel::PropertyValue(options.owner));
	return object;
}

void grow(const MapModel::Map &map, float x, float y, float dirX, float dirY, float baseHeight, int level, const GroveOptions &options,
	std::mt19937 &random, std::vector<MapModel::MapObject> &out)
{
	// The level before the last was meant for shrubs in the old tool, which never planted any.
	if (level != 1)
	{
		MapModel::MapObject planted = tree(options, x, y, random);
		if (!planted.templateName.empty())
			out.push_back(std::move(planted));
	}
	if (level == 0)
		return;
	float width = 0.0f, height = 0.0f;
	playable(map, width, height);
	for (int child = 0; child < 2; ++child)
	{
		for (int attempt = 0; attempt < 5; ++attempt)
		{
			const float angle = uniform(random, 0.0f, 2.0f * PI);
			const float spread = uniform(random, 3.0f * CELL, 6.0f * CELL);
			const float cx = x + spread * std::cos(angle);
			const float cy = y + spread * std::sin(angle);
			const float dx = cx - x;
			const float dy = cy - y;
			// Not back towards the parent, not on slopes, not much higher or lower, on the map, dry.
			if (dx * dirX + dy * dirY >= 0.0f && !tooSteepForTree(map, cx, cy) && std::abs(groundHeight(map, cx, cy) - baseHeight) < 1.5f * CELL
				&& cx > 0.0f && cy > 0.0f && cx < width && cy < height && !underWater(map, cx, cy))
			{
				grow(map, cx, cy, dx, dy, baseHeight, level - 1, options, random, out);
				break;
			}
		}
	}
}

} // namespace

bool underWater(const MapModel::Map &map, float x, float y)
{
	if (!map.polygons)
		return false;
	for (const MapModel::PolygonArea &area : *map.polygons)
	{
		if (!area.isWater || area.points.empty() || !areaContains(area, x, y))
			continue;
		return groundHeight(map, x, y) < static_cast<float>(area.points[0][2]);
	}
	return false;
}

bool tooSteepForTree(const MapModel::Map &map, float x, float y)
{
	const float h = CELL * 0.5f;
	const float gx = (groundHeight(map, x + h, y) - groundHeight(map, x - h, y)) / CELL;
	const float gy = (groundHeight(map, x, y + h) - groundHeight(map, x, y - h)) / CELL;
	return 1.0f / std::sqrt(1.0f + gx * gx + gy * gy) <= FLAT_TOLERANCE;
}

std::vector<MapModel::MapObject> growGrove(const MapModel::Map &map, float x, float y, const GroveOptions &options, std::mt19937 &random)
{
	std::vector<MapModel::MapObject> trees;
	grow(map, x, y, 0.0f, 0.0f, groundHeight(map, x, y), 3, options, random, trees);
	return trees;
}

std::vector<MapModel::MapObject> scatterGrove(const MapModel::Map &map, float x1, float y1, float x2, float y2,
	const GroveOptions &options, std::mt19937 &random)
{
	std::vector<MapModel::MapObject> trees;
	const float lx = std::min(x1, x2), hx = std::max(x1, x2);
	const float ly = std::min(y1, y2), hy = std::max(y1, y2);
	for (int i = 0; i < options.boxCount; ++i)
	{
		const float x = lx + (hx - lx) * uniform(random, 0.0f, 1.0f);
		const float y = ly + (hy - ly) * uniform(random, 0.0f, 1.0f);
		if ((!options.allowInWater && underWater(map, x, y)) || (!options.allowOnSlopes && tooSteepForTree(map, x, y)))
			continue;
		MapModel::MapObject planted = tree(options, x, y, random);
		if (!planted.templateName.empty())
			trees.push_back(std::move(planted));
	}
	return trees;
}

std::vector<MapModel::MapObject> fencePieces(const std::string &templateName, const std::string &owner, float x1, float y1,
	float x2, float y2, float spacing, float offset, int count)
{
	std::vector<MapModel::MapObject> pieces;
	if (templateName.empty() || spacing <= 0.0f)
		return pieces;
	const float dx = x2 - x1;
	const float dy = y2 - y1;
	const float length = std::hypot(dx, dy);
	// Along the line; a click without a drag faces east.
	const float ux = length > 0.0f ? dx / length : 1.0f;
	const float uy = length > 0.0f ? dy / length : 0.0f;
	const float angle = length > 0.0f ? std::atan2(dy, dx) : 0.0f;
	float step = spacing;
	if (count > 0)
	{
		count = std::min(count, MAX_FENCE_PIECES);
		step = length / static_cast<float>(count);
	}
	else
	{
		count = std::clamp(static_cast<int>(std::floor(length / spacing)), 1, MAX_FENCE_PIECES);
	}
	for (int i = 0; i < count; ++i)
	{
		MapModel::MapObject piece;
		piece.templateName = templateName;
		piece.x = x1 + (step * static_cast<float>(i) + offset) * ux;
		piece.y = y1 + (step * static_cast<float>(i) + offset) * uy;
		piece.angle = angle;
		piece.properties.set("originalOwner", MapModel::PropertyValue(owner));
		pieces.push_back(std::move(piece));
	}
	return pieces;
}

MapModel::MapObject scorchMark(float x, float y, int type, float radius)
{
	MapModel::MapObject scorch;
	scorch.templateName = "Scorch";
	scorch.x = x;
	scorch.y = y;
	scorch.properties.set("originalOwner", MapModel::PropertyValue(std::string("team")));
	scorch.properties.set("objectRadius", MapModel::PropertyValue(std::max(radius, 1.0f)));
	scorch.properties.set("scorchType", MapModel::PropertyValue(static_cast<std::int32_t>(std::clamp(type, 0, SCORCH_TYPES - 1))));
	return scorch;
}

bool isScorch(const MapModel::MapObject &object)
{
	return object.templateName == "Scorch" && object.properties.getInt("scorchType").has_value();
}

int scorchAt(const MapModel::Map &map, float x, float y)
{
	if (!map.objects)
		return -1;
	// A tight look first, as the old tool: within half a cell, then within one and a half.
	for (float reach : {0.5f * CELL, 1.5f * CELL})
	{
		int best = -1;
		float bestDistance = reach;
		for (std::size_t i = 0; i < map.objects->size(); ++i)
		{
			const MapModel::MapObject &o = (*map.objects)[i];
			if (!isScorch(o))
				continue;
			const float d = std::hypot(o.x - x, o.y - y);
			if (d < bestDistance)
			{
				bestDistance = d;
				best = static_cast<int>(i);
			}
		}
		if (best >= 0)
			return best;
	}
	return -1;
}

} // namespace MapEditorCore
