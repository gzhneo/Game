/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Slope.cpp -- slopes of the height map.

#include "Core/Slope.h"

#include "Model/MapIO.h"

#include <algorithm>
#include <cmath>

namespace MapEditorCore
{

namespace
{

// World units per cell and per height step, as the game's MAP_XY_FACTOR and MAP_HEIGHT_SCALE.
constexpr float CELL_SIZE = 10.0f;
constexpr float HEIGHT_STEP = CELL_SIZE / 16.0f;
constexpr float DEGREES = 57.29577951f;

float heightAt(const MapModel::HeightMap &hm, int x, int y)
{
	if (x < 0 || y < 0 || x >= hm.width || y >= hm.height)
		return 0.0f;
	return static_cast<float>(hm.heights[static_cast<std::size_t>(y) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(x)]) * HEIGHT_STEP;
}

} // namespace

float cellSlope(const MapModel::HeightMap &hm, int x, int y)
{
	const float h0 = heightAt(hm, x, y);
	const float rise = std::max({std::abs(heightAt(hm, x + 1, y) - h0) / CELL_SIZE, std::abs(heightAt(hm, x + 1, y + 1) - h0) / (CELL_SIZE * std::sqrt(2.0f)),
		std::abs(heightAt(hm, x, y + 1) - h0) / CELL_SIZE});
	return std::atan(rise) * DEGREES;
}

std::vector<float> slopeMap(const MapModel::HeightMap &hm)
{
	std::vector<float> slopes;
	if (hm.width <= 0 || hm.height <= 0 || hm.heights.size() < static_cast<std::size_t>(hm.width) * static_cast<std::size_t>(hm.height))
		return slopes;
	slopes.reserve(static_cast<std::size_t>(hm.width) * static_cast<std::size_t>(hm.height));
	for (int y = 0; y < hm.height; ++y)
		for (int x = 0; x < hm.width; ++x)
			slopes.push_back(cellSlope(hm, x, y));
	return slopes;
}

SlopeColor slopeColor(float degrees, float limit)
{
	if (limit <= 0.0f || degrees >= limit)
		return SlopeColor{140, 0, 0};
	const float f = std::clamp(degrees / limit, 0.0f, 1.0f);
	// Green to yellow in the first half, yellow to red in the second.
	const auto channel = [](float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
	return f < 0.5f ? SlopeColor{channel(f * 2.0f), 200, 0} : SlopeColor{255, channel((1.0f - f) * 2.0f * 200.0f / 255.0f), 0};
}

std::vector<std::uint32_t> terrainTint(const MapModel::Map &map, float slopeLimit, bool cliffFlags)
{
	if (!map.heightMap || (slopeLimit <= 0.0f && !cliffFlags))
		return {};
	const MapModel::HeightMap &hm = *map.heightMap;
	std::vector<std::uint32_t> out(static_cast<std::size_t>(hm.width) * static_cast<std::size_t>(hm.height), 0u);
	if (slopeLimit > 0.0f)
	{
		const std::vector<float> slopes = slopeMap(hm);
		for (std::size_t i = 0; i < out.size() && i < slopes.size(); ++i)
		{
			const float f = slopes[i] / slopeLimit;
			if (f < 0.5f)
				continue;
			const SlopeColor c = slopeColor(slopes[i], slopeLimit);
			// Steeper cells cover the ground more.
			const std::uint32_t alpha = f >= 1.0f ? 0xb0u : 0x50u + static_cast<std::uint32_t>(0x40 * (f - 0.5f) * 2.0f);
			out[i] = (alpha << 24) | (static_cast<std::uint32_t>(c.r) << 16) | (static_cast<std::uint32_t>(c.g) << 8) | c.b;
		}
	}
	if (cliffFlags && map.blendTiles)
	{
		for (int y = 0; y < hm.height; ++y)
			for (int x = 0; x < hm.width; ++x)
				if (MapModel::cliffFlag(map.blendTiles->cliffState, hm.width, x, y))
					out[static_cast<std::size_t>(y) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(x)] = 0x90e03030u;
	}
	return out;
}

} // namespace MapEditorCore
