/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Contours.cpp -- contour lines by marching squares over the height samples.

#include "Core/Contours.h"

#include <algorithm>
#include <array>

namespace MapEditorCore
{

std::vector<float> contourLines(const MapModel::HeightMap &heightMap, int step, int offset)
{
	std::vector<float> lines;
	const int w = heightMap.width;
	const int h = heightMap.height;
	if (step < 1 || w < 2 || h < 2 || heightMap.heights.size() < static_cast<std::size_t>(w) * static_cast<std::size_t>(h))
		return lines;
	offset = ((offset % step) + step) % step;
	const auto at = [&heightMap, w](int x, int y) { return static_cast<int>(heightMap.heights[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)]); };
	for (int y = 0; y + 1 < h; ++y)
	{
		for (int x = 0; x + 1 < w; ++x)
		{
			// Corners counter-clockwise from (x, y); edges between neighbouring corners.
			const std::array<int, 4> cx = {x, x + 1, x + 1, x};
			const std::array<int, 4> cy = {y, y, y + 1, y + 1};
			std::array<int, 4> v{};
			for (int i = 0; i < 4; ++i)
				v[static_cast<std::size_t>(i)] = at(cx[static_cast<std::size_t>(i)], cy[static_cast<std::size_t>(i)]);
			const int lo = *std::min_element(v.begin(), v.end());
			const int hi = *std::max_element(v.begin(), v.end());
			// The first level above the lowest corner; a corner counts as above at its own height.
			int level = lo - ((lo - offset) % step + step) % step;
			if (level <= lo)
				level += step;
			for (; level <= hi; level += step)
			{
				float points[8];
				int count = 0;
				for (int i = 0; i < 4; ++i)
				{
					const std::size_t a = static_cast<std::size_t>(i);
					const std::size_t b = static_cast<std::size_t>((i + 1) % 4);
					if ((v[a] >= level) == (v[b] >= level))
						continue;
					const float t = static_cast<float>(level - v[a]) / static_cast<float>(v[b] - v[a]);
					points[count * 2] = static_cast<float>(cx[a]) + t * static_cast<float>(cx[b] - cx[a]);
					points[count * 2 + 1] = static_cast<float>(cy[a]) + t * static_cast<float>(cy[b] - cy[a]);
					++count;
				}
				lines.insert(lines.end(), points, points + count * 2);
			}
		}
	}
	return lines;
}

} // namespace MapEditorCore
