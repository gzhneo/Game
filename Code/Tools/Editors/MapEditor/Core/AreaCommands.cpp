/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// AreaCommands.cpp -- see AreaCommands.h.

#include "Core/AreaCommands.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace MapEditorCore
{

namespace
{

constexpr float CELL = 10.0f;					// world units per height sample
constexpr float HEIGHT_SCALE = CELL / 16.0f;	// world units per height step

float distanceToSegment(float px, float py, float ax, float ay, float bx, float by)
{
	const float dx = bx - ax;
	const float dy = by - ay;
	const float lengthSq = dx * dx + dy * dy;
	float t = lengthSq > 0.0f ? ((px - ax) * dx + (py - ay) * dy) / lengthSq : 0.0f;
	t = std::clamp(t, 0.0f, 1.0f);
	const float cx = ax + t * dx - px;
	const float cy = ay + t * dy - py;
	return std::sqrt(cx * cx + cy * cy);
}

} // namespace

Changes EditAreas::changes() const
{
	Changes c;
	c.areas = true;
	return c;
}

std::unique_ptr<EditAreas> addArea(const MapModel::Map &map, MapModel::PolygonArea area)
{
	std::int32_t id = 1;
	if (map.polygons)
	{
		for (const MapModel::PolygonArea &a : *map.polygons)
			id = std::max(id, a.id + 1);
	}
	area.id = id;
	if (area.name.empty())
		area.name = (area.isWater ? "Water Area " : "Area ") + std::to_string(id);
	std::vector<MapModel::PolygonArea> after = map.polygons.value_or(std::vector<MapModel::PolygonArea>{});
	after.push_back(std::move(area));
	const std::int32_t index = static_cast<std::int32_t>(after.size()) - 1;
	return std::make_unique<EditAreas>("Add area", map.polygons, std::move(after), index);
}

std::unique_ptr<EditAreas> changeArea(const MapModel::Map &map, std::int32_t index, const MapModel::PolygonArea &changed,
	const std::string &name)
{
	if (!map.polygons || index < 0 || static_cast<std::size_t>(index) >= map.polygons->size())
		return nullptr;
	if ((*map.polygons)[static_cast<std::size_t>(index)] == changed)
		return nullptr;
	std::vector<MapModel::PolygonArea> after = *map.polygons;
	after[static_cast<std::size_t>(index)] = changed;
	return std::make_unique<EditAreas>(name, map.polygons, std::move(after), index);
}

std::unique_ptr<EditAreas> deleteArea(const MapModel::Map &map, std::int32_t index)
{
	if (!map.polygons || index < 0 || static_cast<std::size_t>(index) >= map.polygons->size())
		return nullptr;
	std::vector<MapModel::PolygonArea> after = *map.polygons;
	after.erase(after.begin() + index);
	return std::make_unique<EditAreas>("Delete area", map.polygons, std::move(after), -1);
}

int areaPointAt(const MapModel::PolygonArea &area, float x, float y, float tolerance)
{
	int best = -1;
	float bestDistance = tolerance;
	for (std::size_t i = 0; i < area.points.size(); ++i)
	{
		const float d = std::hypot(static_cast<float>(area.points[i][0]) - x, static_cast<float>(area.points[i][1]) - y);
		if (d <= bestDistance)
		{
			best = static_cast<int>(i);
			bestDistance = d;
		}
	}
	return best;
}

int areaEdgeAt(const MapModel::PolygonArea &area, float x, float y, float tolerance)
{
	const std::size_t n = area.points.size();
	if (n < 2)
		return -1;
	int best = -1;
	float bestDistance = tolerance;
	for (std::size_t i = 0; i < n; ++i)
	{
		const auto &a = area.points[i];
		const auto &b = area.points[(i + 1) % n];
		const float d = distanceToSegment(x, y, static_cast<float>(a[0]), static_cast<float>(a[1]), static_cast<float>(b[0]), static_cast<float>(b[1]));
		if (d <= bestDistance)
		{
			best = static_cast<int>(i) + 1;
			bestDistance = d;
		}
	}
	return best;
}

bool areaContains(const MapModel::PolygonArea &area, float x, float y)
{
	bool inside = false;
	const std::size_t n = area.points.size();
	for (std::size_t i = 0, j = n - 1; i < n; j = i++)
	{
		const float xi = static_cast<float>(area.points[i][0]);
		const float yi = static_cast<float>(area.points[i][1]);
		const float xj = static_cast<float>(area.points[j][0]);
		const float yj = static_cast<float>(area.points[j][1]);
		if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
			inside = !inside;
	}
	return n >= 3 && inside;
}

float groundHeight(const MapModel::Map &map, float x, float y)
{
	if (!map.heightMap || map.heightMap->width <= 0 || map.heightMap->height <= 0)
		return 0.0f;
	const MapModel::HeightMap &hm = *map.heightMap;
	const float cx = std::clamp(x / CELL + static_cast<float>(hm.borderSize), 0.0f, static_cast<float>(hm.width - 1));
	const float cy = std::clamp(y / CELL + static_cast<float>(hm.borderSize), 0.0f, static_cast<float>(hm.height - 1));
	const int x0 = static_cast<int>(cx);
	const int y0 = static_cast<int>(cy);
	const int x1 = std::min(x0 + 1, hm.width - 1);
	const int y1 = std::min(y0 + 1, hm.height - 1);
	const auto at = [&hm](int ix, int iy) {
		return static_cast<float>(hm.heights[static_cast<std::size_t>(iy) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(ix)]);
	};
	const float fx = cx - static_cast<float>(x0);
	const float fy = cy - static_cast<float>(y0);
	const float h = (at(x0, y0) * (1.0f - fx) + at(x1, y0) * fx) * (1.0f - fy) + (at(x0, y1) * (1.0f - fx) + at(x1, y1) * fx) * fy;
	return h * HEIGHT_SCALE;
}

} // namespace MapEditorCore
