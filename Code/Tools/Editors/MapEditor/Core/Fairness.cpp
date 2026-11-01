/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Core/Fairness.h"

#include "Core/WaypointCommands.h"
#include "Model/MapIO.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <utility>

namespace MapEditorCore
{

namespace
{

constexpr float CELL = 10.0f;
constexpr float CONTESTED = 0.1f;

bool contains(const std::string &s, const char *part)
{
	return s.find(part) != std::string::npos;
}

// Marks the cells whose centre lies in the area (even-odd rule), one row at a time: the centres between
// the first and second crossing of the row with the outline, the third and fourth, ...
void markInside(const MapModel::PolygonArea &area, const MapModel::HeightMap &hm, std::vector<std::uint8_t> &marks)
{
	const std::size_t n = area.points.size();
	float lowY = static_cast<float>(area.points[0][1]), highY = lowY;
	for (const auto &p : area.points)
	{
		lowY = std::min(lowY, static_cast<float>(p[1]));
		highY = std::max(highY, static_cast<float>(p[1]));
	}
	const int firstRow = std::max(0, static_cast<int>(std::floor(lowY / CELL - 0.5f)) + hm.borderSize);
	const int lastRow = std::min(hm.height - 1, static_cast<int>(std::ceil(highY / CELL - 0.5f)) + hm.borderSize);
	std::vector<float> crossings;
	for (int y = firstRow; y <= lastRow; ++y)
	{
		const float wy = (static_cast<float>(y - hm.borderSize) + 0.5f) * CELL;
		crossings.clear();
		for (std::size_t i = 0, j = n - 1; i < n; j = i++)
		{
			const float xi = static_cast<float>(area.points[i][0]), yi = static_cast<float>(area.points[i][1]);
			const float xj = static_cast<float>(area.points[j][0]), yj = static_cast<float>(area.points[j][1]);
			if ((yi > wy) != (yj > wy))
				crossings.push_back((xj - xi) * (wy - yi) / (yj - yi) + xi);
		}
		std::sort(crossings.begin(), crossings.end());
		for (std::size_t k = 0; k + 1 < crossings.size(); k += 2)
		{
			// Centres x with crossings[k] <= wx < crossings[k + 1].
			const int from = std::max(0, static_cast<int>(std::ceil(crossings[k] / CELL - 0.5f)) + hm.borderSize);
			const int to = std::min(hm.width - 1, static_cast<int>(std::ceil(crossings[k + 1] / CELL - 0.5f)) + hm.borderSize - 1);
			for (int x = from; x <= to; ++x)
				marks[static_cast<std::size_t>(y) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(x)] = 1;
		}
	}
}

// The passable cell nearest to (cx, cy) within a few cells: buildings often stand on flagged ground.
bool nearestPassable(const std::vector<std::uint8_t> &passable, int width, int height, int cx, int cy, int &outX, int &outY)
{
	for (int r = 0; r <= 4; ++r)
	{
		for (int dy = -r; dy <= r; ++dy)
		{
			for (int dx = -r; dx <= r; ++dx)
			{
				if (std::max(std::abs(dx), std::abs(dy)) != r)
					continue;
				const int x = cx + dx, y = cy + dy;
				if (x >= 0 && y >= 0 && x < width && y < height && passable[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)])
				{
					outX = x;
					outY = y;
					return true;
				}
			}
		}
	}
	return false;
}

float distanceAt(const std::vector<float> &field, int width, int height, int x, int y)
{
	if (x < 0 || y < 0 || x >= width || y >= height)
		return -1.0f;
	return field[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
}

} // namespace

ResourceKind resourceKind(const std::string &templateName)
{
	if (contains(templateName, "SupplyDock") || contains(templateName, "SupplyPile") || contains(templateName, "SupplyWarehouse"))
		return ResourceKind::Supply;
	if (contains(templateName, "Tech"))
		return ResourceKind::Tech;
	return ResourceKind::None;
}

int startNumber(const std::string &waypointName)
{
	if (waypointName.size() != 14 || waypointName.compare(0, 7, "Player_") != 0 || waypointName.compare(8, 6, "_Start") != 0)
		return 0;
	const int n = waypointName[7] - '0';
	return n >= 1 && n <= 8 ? n : 0;
}

std::vector<std::uint8_t> passableCells(const MapModel::Map &map)
{
	if (!map.heightMap)
		return {};
	const MapModel::HeightMap &hm = *map.heightMap;
	const std::size_t cells = static_cast<std::size_t>(hm.width) * static_cast<std::size_t>(hm.height);
	// Water areas are rasterised once instead of testing every cell against every area.
	std::vector<std::uint8_t> water(cells, 0);
	if (map.polygons)
		for (const MapModel::PolygonArea &a : *map.polygons)
			if ((a.isWater || a.isRiver) && a.points.size() >= 3)
				markInside(a, hm, water);
	std::vector<std::uint8_t> out(cells, 0);
	for (int y = hm.borderSize; y < hm.height - hm.borderSize; ++y)
	{
		for (int x = hm.borderSize; x < hm.width - hm.borderSize; ++x)
		{
			const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(x);
			const bool cliff = map.blendTiles && MapModel::cliffFlag(map.blendTiles->cliffState, hm.width, x, y);
			out[i] = !cliff && !water[i] ? 1 : 0;
		}
	}
	return out;
}

std::vector<float> pathDistances(const std::vector<std::uint8_t> &passable, int width, int height, int fromX, int fromY)
{
	std::vector<float> dist(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), -1.0f);
	const auto index = [width](int x, int y) { return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x); };
	if (fromX < 0 || fromY < 0 || fromX >= width || fromY >= height || !passable[index(fromX, fromY)])
		return dist;
	using Entry = std::pair<float, std::size_t>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
	dist[index(fromX, fromY)] = 0.0f;
	open.push({0.0f, index(fromX, fromY)});
	constexpr float DIAGONAL = 1.41421356f;
	while (!open.empty())
	{
		const auto [d, at] = open.top();
		open.pop();
		if (d > dist[at])
			continue;
		const int x = static_cast<int>(at % static_cast<std::size_t>(width));
		const int y = static_cast<int>(at / static_cast<std::size_t>(width));
		for (int dy = -1; dy <= 1; ++dy)
		{
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (!dx && !dy)
					continue;
				const int nx = x + dx, ny = y + dy;
				if (nx < 0 || ny < 0 || nx >= width || ny >= height || !passable[index(nx, ny)])
					continue;
				// A diagonal step needs both side cells free.
				if (dx && dy && (!passable[index(x + dx, y)] || !passable[index(x, y + dy)]))
					continue;
				const float nd = d + (dx && dy ? DIAGONAL : 1.0f);
				float &old = dist[index(nx, ny)];
				if (old < 0.0f || nd < old)
				{
					old = nd;
					open.push({nd, index(nx, ny)});
				}
			}
		}
	}
	return dist;
}

FairnessReport fairnessReport(const MapModel::Map &map)
{
	FairnessReport report;
	if (!map.heightMap || !map.objects)
		return report;
	const MapModel::HeightMap &hm = *map.heightMap;
	report.width = hm.width;
	report.height = hm.height;
	const std::vector<std::uint8_t> passable = passableCells(map);
	const auto cellOf = [&hm](float x, float y, int &cx, int &cy) {
		cx = static_cast<int>(std::floor(x / CELL)) + hm.borderSize;
		cy = static_cast<int>(std::floor(y / CELL)) + hm.borderSize;
	};

	for (const MapModel::MapObject &o : *map.objects)
	{
		if (isWaypoint(o))
		{
			const int n = startNumber(o.properties.getAscii("waypointName").value_or(std::string()));
			if (n > 0 && std::none_of(report.starts.begin(), report.starts.end(), [n](const StartFairness &s) { return s.number == n; }))
			{
				StartFairness s;
				s.number = n;
				s.x = o.x;
				s.y = o.y;
				report.starts.push_back(s);
			}
			continue;
		}
		const ResourceKind kind = resourceKind(o.templateName);
		if (kind == ResourceKind::None)
			continue;
		ResourceFairness r;
		r.templateName = o.templateName;
		r.kind = kind;
		r.x = o.x;
		r.y = o.y;
		report.resources.push_back(r);
	}
	std::sort(report.starts.begin(), report.starts.end(), [](const StartFairness &a, const StartFairness &b) { return a.number < b.number; });

	// One distance field per start.
	std::vector<std::vector<float>> fields;
	for (const StartFairness &s : report.starts)
	{
		int cx = 0, cy = 0, px = 0, py = 0;
		cellOf(s.x, s.y, cx, cy);
		fields.push_back(nearestPassable(passable, hm.width, hm.height, cx, cy, px, py)
			? pathDistances(passable, hm.width, hm.height, px, py)
			: std::vector<float>(passable.size(), -1.0f));
	}

	for (ResourceFairness &r : report.resources)
	{
		int cx = 0, cy = 0, px = 0, py = 0;
		cellOf(r.x, r.y, cx, cy);
		const bool onGround = nearestPassable(passable, hm.width, hm.height, cx, cy, px, py);
		float best = -1.0f, second = -1.0f;
		for (std::size_t s = 0; s < fields.size(); ++s)
		{
			const float d = onGround ? distanceAt(fields[s], hm.width, hm.height, px, py) : -1.0f;
			r.distance.push_back(d < 0.0f ? -1.0f : d * CELL);
			if (d < 0.0f)
			{
				++report.starts[s].unreachable;
				continue;
			}
			if (best < 0.0f || d < best)
			{
				second = best;
				best = d;
				r.first = static_cast<int>(s);
			}
			else if (second < 0.0f || d < second)
				second = d;
		}
		r.contested = best >= 0.0f && second >= 0.0f && second - best <= CONTESTED * std::max(best, 1.0f);
		if (r.first >= 0 && !r.contested)
		{
			if (r.kind == ResourceKind::Supply)
				++report.starts[static_cast<std::size_t>(r.first)].suppliesFirst;
			else
				++report.starts[static_cast<std::size_t>(r.first)].techFirst;
		}
		if (r.kind == ResourceKind::Supply)
			for (std::size_t s = 0; s < fields.size(); ++s)
			{
				float &nearest = report.starts[s].nearestSupply;
				if (r.distance[s] >= 0.0f && (nearest < 0.0f || r.distance[s] < nearest))
					nearest = r.distance[s];
			}
	}

	for (std::size_t s = 0; s < report.starts.size(); ++s)
	{
		for (std::size_t t = 0; t < report.starts.size(); ++t)
		{
			if (s == t)
				continue;
			int cx = 0, cy = 0, px = 0, py = 0;
			cellOf(report.starts[t].x, report.starts[t].y, cx, cy);
			if (!nearestPassable(passable, hm.width, hm.height, cx, cy, px, py))
				continue;
			const float d = distanceAt(fields[s], hm.width, hm.height, px, py);
			float &nearest = report.starts[s].nearestEnemy;
			if (d >= 0.0f && (nearest < 0.0f || d * CELL < nearest))
				nearest = d * CELL;
		}
	}

	report.territory.assign(passable.size(), -1);
	for (std::size_t i = 0; i < passable.size(); ++i)
	{
		float best = -1.0f, second = -1.0f;
		int first = -1;
		for (std::size_t s = 0; s < fields.size(); ++s)
		{
			const float d = fields[s][i];
			if (d < 0.0f)
				continue;
			if (best < 0.0f || d < best)
			{
				second = best;
				best = d;
				first = static_cast<int>(s);
			}
			else if (second < 0.0f || d < second)
				second = d;
		}
		if (first >= 0)
			report.territory[i] = second >= 0.0f && second - best <= CONTESTED * std::max(best, 1.0f) ? -2 : first;
	}

	float lo = -1.0f, hi = -1.0f, sum = 0.0f;
	int counted = 0;
	for (const StartFairness &s : report.starts)
	{
		if (s.nearestSupply < 0.0f)
			continue;
		lo = lo < 0.0f ? s.nearestSupply : std::min(lo, s.nearestSupply);
		hi = std::max(hi, s.nearestSupply);
		sum += s.nearestSupply;
		++counted;
	}
	if (counted >= 2 && sum > 0.0f)
		report.supplySpread = (hi - lo) / (sum / static_cast<float>(counted));
	return report;
}

} // namespace MapEditorCore
