/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// HeightImage.cpp -- see HeightImage.h.

#include "Core/HeightImage.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace MapEditorCore
{

GreyImage heightsToImage(const MapModel::HeightMap &hm)
{
	GreyImage image;
	if (hm.width <= 0 || hm.height <= 0 || hm.heights.size() < static_cast<std::size_t>(hm.width) * static_cast<std::size_t>(hm.height))
		return image;
	image.width = hm.width;
	image.height = hm.height;
	image.bits = 8;
	const std::size_t w = static_cast<std::size_t>(hm.width);
	image.pixels.resize(w * static_cast<std::size_t>(hm.height));
	for (std::size_t row = 0; row < static_cast<std::size_t>(hm.height); ++row)
	{
		const std::size_t y = static_cast<std::size_t>(hm.height) - 1 - row;
		std::copy_n(hm.heights.begin() + static_cast<std::ptrdiff_t>(y * w), w, image.pixels.begin() + static_cast<std::ptrdiff_t>(row * w));
	}
	return image;
}

std::vector<std::uint8_t> heightsFromImage(const GreyImage &image, int width, int height, int low, int high)
{
	std::vector<std::uint8_t> heights;
	if (width <= 0 || height <= 0 || image.width <= 0 || image.height <= 0 ||
		image.pixels.size() < static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height))
		return heights;
	const double top = std::ldexp(1.0, std::clamp(image.bits, 1, 16)) - 1.0;
	const double lo = std::clamp(low, 0, 255);
	const double hi = std::clamp(high, 0, 255);
	const auto pixel = [&image](int x, int y) {
		return static_cast<double>(image.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)]);
	};
	// Position in the image of output sample i of n: first and last sample on the first and last pixel.
	const auto source = [](int i, int n, int size, int &i0, int &i1, double &t) {
		const double s = n > 1 ? static_cast<double>(i) * static_cast<double>(size - 1) / static_cast<double>(n - 1) : 0.0;
		i0 = std::min(static_cast<int>(std::floor(s)), size - 1);
		i1 = std::min(i0 + 1, size - 1);
		t = s - static_cast<double>(i0);
	};
	heights.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
	for (int y = 0; y < height; ++y)
	{
		int r0 = 0, r1 = 0;
		double ty = 0.0;
		source(height - 1 - y, height, image.height, r0, r1, ty);
		for (int x = 0; x < width; ++x)
		{
			int c0 = 0, c1 = 0;
			double tx = 0.0;
			source(x, width, image.width, c0, c1, tx);
			const double upper = pixel(c0, r0) + (pixel(c1, r0) - pixel(c0, r0)) * tx;
			const double lower = pixel(c0, r1) + (pixel(c1, r1) - pixel(c0, r1)) * tx;
			const double value = std::clamp((upper + (lower - upper) * ty) / top, 0.0, 1.0);
			const long h = std::lround(lo + value * (hi - lo));
			heights[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
				static_cast<std::uint8_t>(std::clamp(h, 0L, 255L));
		}
	}
	return heights;
}

std::unique_ptr<EditHeights> importHeights(MapModel::Map &map, const GreyImage &image, int low, int high)
{
	if (!map.heightMap)
		return nullptr;
	MapModel::HeightMap &hm = *map.heightMap;
	std::vector<std::uint8_t> heights = heightsFromImage(image, hm.width, hm.height, low, high);
	if (heights.empty() || heights.size() != hm.heights.size())
		return nullptr;
	std::vector<std::uint8_t> before = std::exchange(hm.heights, std::move(heights));
	return heightStroke(map, before, CellRect{0, 0, hm.width - 1, hm.height - 1}, "Import heights");
}

} // namespace MapEditorCore
