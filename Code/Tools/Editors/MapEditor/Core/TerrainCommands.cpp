/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// TerrainCommands.cpp -- see TerrainCommands.h.

#include "Core/TerrainCommands.h"

#include "Model/MapIO.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace MapEditorCore
{

namespace
{

constexpr float CELL = 10.0f;	// world units per height sample

// 1 inside the solid part, fading smoothly to 0 at the radius.
float falloff(float distance, const Brush &brush)
{
	const float d = distance / std::max(brush.radius, 0.5f);
	if (d >= 1.0f)
		return 0.0f;
	const float solid = 1.0f - std::clamp(brush.feather, 0.0f, 1.0f);
	if (d <= solid)
		return 1.0f;
	const float t = (d - solid) / (1.0f - solid);
	return 1.0f - t * t * (3.0f - 2.0f * t);
}

} // namespace

void CellRect::unite(const CellRect &other)
{
	if (other.empty())
		return;
	if (empty())
	{
		*this = other;
		return;
	}
	x0 = std::min(x0, other.x0);
	y0 = std::min(y0, other.y0);
	x1 = std::max(x1, other.x1);
	y1 = std::max(y1, other.y1);
}

void worldToCell(const MapModel::HeightMap &hm, float x, float y, float &cellX, float &cellY)
{
	cellX = x / CELL + static_cast<float>(hm.borderSize);
	cellY = y / CELL + static_cast<float>(hm.borderSize);
}

CellRect applyBrush(MapModel::HeightMap &hm, float cellX, float cellY, const Brush &brush)
{
	CellRect changed;
	if (hm.width <= 0 || hm.height <= 0 || hm.heights.size() < static_cast<std::size_t>(hm.width) * static_cast<std::size_t>(hm.height))
		return changed;
	const int reach = static_cast<int>(std::ceil(brush.radius));
	const int x0 = std::max(0, static_cast<int>(std::floor(cellX)) - reach);
	const int y0 = std::max(0, static_cast<int>(std::floor(cellY)) - reach);
	const int x1 = std::min(hm.width - 1, static_cast<int>(std::ceil(cellX)) + reach);
	const int y1 = std::min(hm.height - 1, static_cast<int>(std::ceil(cellY)) + reach);
	if (x1 < x0 || y1 < y0)
		return changed;
	const auto at = [&hm](int x, int y) -> std::uint8_t & { return hm.heights[static_cast<std::size_t>(y) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(x)]; };
	// Smoothing reads the heights as they were before this application.
	std::vector<std::uint8_t> snapshot;
	if (brush.mode == BrushMode::Smooth)
		snapshot = hm.heights;
	const auto old = [&](int x, int y) {
		x = std::clamp(x, 0, hm.width - 1);
		y = std::clamp(y, 0, hm.height - 1);
		return static_cast<float>(snapshot[static_cast<std::size_t>(y) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(x)]);
	};
	for (int y = y0; y <= y1; ++y)
	{
		for (int x = x0; x <= x1; ++x)
		{
			const float f = falloff(std::hypot(static_cast<float>(x) - cellX, static_cast<float>(y) - cellY), brush);
			if (f <= 0.0f)
				continue;
			std::uint8_t &cell = at(x, y);
			const float h = static_cast<float>(cell);
			float target = h;
			switch (brush.mode)
			{
			case BrushMode::Raise: target = h + brush.strength * f; break;
			case BrushMode::Lower: target = h - brush.strength * f; break;
			case BrushMode::Set: target = h + (static_cast<float>(brush.height) - h) * f; break;
			case BrushMode::Smooth:
			{
				float sum = 0.0f;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
						sum += old(x + dx, y + dy);
				target = h + (sum / 9.0f - h) * f * std::clamp(brush.strength / 4.0f, 0.1f, 1.0f);
				break;
			}
			}
			const std::uint8_t value = static_cast<std::uint8_t>(std::clamp(std::lround(target), 0L, 255L));
			if (value != cell)
			{
				cell = value;
				changed.unite(CellRect{x, y, x, y});
			}
		}
	}
	return changed;
}

Changes EditHeights::changes() const
{
	Changes c;
	c.terrain = true;
	c.passability = !m_cliffsAfter.empty();
	c.terrainRect = {m_rect.x0, m_rect.y0, m_rect.x1, m_rect.y1};
	return c;
}

void EditHeights::copy(MapModel::Map &map, const std::vector<std::uint8_t> &source, const std::vector<std::uint8_t> &cliffs) const
{
	if (!map.heightMap || m_rect.empty())
		return;
	if (!cliffs.empty() && map.blendTiles && map.blendTiles->cliffState.size() == cliffs.size())
		map.blendTiles->cliffState = cliffs;
	MapModel::HeightMap &hm = *map.heightMap;
	const std::size_t rowLength = static_cast<std::size_t>(m_rect.x1 - m_rect.x0 + 1);
	for (int y = m_rect.y0; y <= m_rect.y1; ++y)
	{
		const std::size_t from = static_cast<std::size_t>(y - m_rect.y0) * rowLength;
		std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(from), rowLength,
			hm.heights.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(m_rect.x0)));
	}
}

std::unique_ptr<EditHeights> heightStroke(MapModel::Map &map, const std::vector<std::uint8_t> &before, CellRect rect,
	std::string name)
{
	if (!map.heightMap || rect.empty() || before.size() != map.heightMap->heights.size())
		return nullptr;
	const MapModel::HeightMap &hm = *map.heightMap;
	std::vector<std::uint8_t> oldCells, newCells;
	bool changed = false;
	for (int y = rect.y0; y <= rect.y1; ++y)
	{
		for (int x = rect.x0; x <= rect.x1; ++x)
		{
			const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(x);
			oldCells.push_back(before[i]);
			newCells.push_back(hm.heights[i]);
			changed = changed || before[i] != hm.heights[i];
		}
	}
	if (!changed)
		return nullptr;
	auto command = std::make_unique<EditHeights>(std::move(name), rect, std::move(oldCells), std::move(newCells));
	// A cell's flag depends on its four corners: the cells left of and below the rectangle change too.
	if (map.blendTiles && !map.blendTiles->cliffState.empty())
	{
		std::vector<std::uint8_t> &flags = map.blendTiles->cliffState;
		std::vector<std::uint8_t> old = flags;
		MapModel::updateCliffFlags(hm, flags, rect.x0 - 1, rect.y0 - 1, rect.x1, rect.y1);
		if (old != flags)
			command->setCliffs(std::move(old), flags);
	}
	return command;
}

CellRect paintPassability(std::vector<std::uint8_t> &flags, int width, int height, float cellX, float cellY, float radius, bool impassable)
{
	CellRect changed;
	if (width < 1 || height < 1 || flags.size() < MapModel::cliffRowBytes(width) * static_cast<std::size_t>(height))
		return changed;
	const float reach = std::max(radius, 0.5f);
	const int x0 = std::max(0, static_cast<int>(std::floor(cellX - reach)));
	const int y0 = std::max(0, static_cast<int>(std::floor(cellY - reach)));
	const int x1 = std::min(width - 1, static_cast<int>(std::ceil(cellX + reach)));
	const int y1 = std::min(height - 1, static_cast<int>(std::ceil(cellY + reach)));
	for (int y = y0; y <= y1; ++y)
	{
		for (int x = x0; x <= x1; ++x)
		{
			if (std::hypot(static_cast<float>(x) + 0.5f - cellX, static_cast<float>(y) + 0.5f - cellY) > reach)
				continue;
			if (MapModel::cliffFlag(flags, width, x, y) == impassable)
				continue;
			MapModel::setCliffFlag(flags, width, x, y, impassable);
			changed.unite(CellRect{x, y, x, y});
		}
	}
	return changed;
}

void EditCliffs::apply(MapModel::Map &map)
{
	if (map.blendTiles && map.blendTiles->cliffState.size() == m_after.size())
		map.blendTiles->cliffState = m_after;
}

void EditCliffs::revert(MapModel::Map &map)
{
	if (map.blendTiles && map.blendTiles->cliffState.size() == m_before.size())
		map.blendTiles->cliffState = m_before;
}

Changes EditCliffs::changes() const
{
	Changes c;
	c.passability = true;
	c.terrainRect = {m_rect.x0, m_rect.y0, m_rect.x1, m_rect.y1};
	return c;
}

std::unique_ptr<EditCliffs> cliffStroke(const MapModel::Map &map, const std::vector<std::uint8_t> &before, CellRect rect, std::string name)
{
	if (!map.blendTiles || rect.empty() || before == map.blendTiles->cliffState)
		return nullptr;
	return std::make_unique<EditCliffs>(std::move(name), rect, before, map.blendTiles->cliffState);
}

} // namespace MapEditorCore
