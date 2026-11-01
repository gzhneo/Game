/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapPreview.cpp -- see MapPreview.h.

#include "Core/MapPreview.h"

#include "Core/PlacementCommands.h"
#include "Core/TextureBlend.h"

#include <algorithm>
#include <cmath>

namespace MapEditorCore
{

namespace
{

constexpr float CELL = 10.0f;
constexpr std::array<float, 3> WATER = {0.55f, 0.55f, 1.0f};
constexpr std::array<float, 3> GREY = {0.5f, 0.48f, 0.4f};

// Lighter above the middle height, darker below (MapPreview::interpolateColorForHeight).
std::array<float, 3> shade(std::array<float, 3> colour, float height, float hi, float mid, float lo)
{
	const float bright = 0.30f;
	const float dark = 0.60f;
	if (hi == mid)
		hi = mid + 0.1f;
	if (mid == lo)
		lo = mid - 0.1f;
	float t = 0.0f;
	std::array<float, 3> target{};
	if (height >= mid)
	{
		t = (height - mid) / (hi - mid);
		for (int c = 0; c < 3; ++c)
			target[static_cast<std::size_t>(c)] = colour[static_cast<std::size_t>(c)] + (1.0f - colour[static_cast<std::size_t>(c)]) * bright;
	}
	else
	{
		t = (mid - height) / (mid - lo);
		for (int c = 0; c < 3; ++c)
			target[static_cast<std::size_t>(c)] = colour[static_cast<std::size_t>(c)] * (1.0f - dark);
	}
	for (int c = 0; c < 3; ++c)
	{
		float &v = colour[static_cast<std::size_t>(c)];
		v = std::clamp(v + (target[static_cast<std::size_t>(c)] - v) * t, 0.0f, 1.0f);
	}
	return colour;
}

} // namespace

std::vector<std::uint32_t> mapPreview(const MapModel::Map &map, const ClassColours &colours)
{
	std::vector<std::uint32_t> pixels(static_cast<std::size_t>(PREVIEW_SIZE * PREVIEW_SIZE), 0xFF000000u);
	if (!map.heightMap || map.heightMap->width < 2 || map.heightMap->height < 2)
		return pixels;
	const MapModel::HeightMap &hm = *map.heightMap;
	const float border = static_cast<float>(hm.borderSize);
	const float stepX = static_cast<float>(hm.width - 2 * hm.borderSize) / PREVIEW_SIZE;
	const float stepY = static_cast<float>(hm.height - 2 * hm.borderSize) / PREVIEW_SIZE;
	// The cell a preview pixel stands for, and its height and colour.
	const auto cellOf = [&](int px, int py, int &cx, int &cy) {
		cx = std::clamp(static_cast<int>(border + static_cast<float>(px) * stepX), 0, hm.width - 1);
		cy = std::clamp(static_cast<int>(border + static_cast<float>(py) * stepY), 0, hm.height - 1);
	};
	const auto heightAt = [&](int cx, int cy) {
		return static_cast<float>(hm.heights[static_cast<std::size_t>(cy) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(cx)]);
	};
	const auto colourAt = [&](int cx, int cy) {
		if (!map.blendTiles)
			return GREY;
		const int c = cellClass(*map.blendTiles, hm.width, hm.height, cx, cy, true);
		return c >= 0 && static_cast<std::size_t>(c) < colours.size() ? colours[static_cast<std::size_t>(c)] : GREY;
	};
	const auto wet = [&](int cx, int cy) {
		return underWater(map, (static_cast<float>(cx) - border) * CELL, (static_cast<float>(cy) - border) * CELL);
	};
	float hi = 0.0f, lo = 255.0f, sum = 0.0f;
	for (int py = 0; py < PREVIEW_SIZE; ++py)
	{
		for (int px = 0; px < PREVIEW_SIZE; ++px)
		{
			int cx = 0, cy = 0;
			cellOf(px, py, cx, cy);
			const float h = heightAt(cx, cy);
			hi = std::max(hi, h);
			lo = std::min(lo, h);
			sum += h;
		}
	}
	const float mid = sum / static_cast<float>(PREVIEW_SIZE * PREVIEW_SIZE);
	for (int py = 0; py < PREVIEW_SIZE; ++py)
	{
		for (int px = 0; px < PREVIEW_SIZE; ++px)
		{
			int cx = 0, cy = 0;
			cellOf(px, py, cx, cy);
			const float z = heightAt(cx, cy);
			const bool water = wet(cx, cy);
			std::array<float, 3> total{};
			int samples = 0;
			// The pixel and its neighbours, of the same kind (water or ground).
			for (int ny = py - 1; ny <= py + 1; ++ny)
			{
				for (int nx = px - 1; nx <= px + 1; ++nx)
				{
					if (nx < 0 || ny < 0 || nx >= PREVIEW_SIZE || ny >= PREVIEW_SIZE)
						continue;
					int sx = 0, sy = 0;
					cellOf(nx, ny, sx, sy);
					std::array<float, 3> colour;
					if (water)
					{
						if (!wet(sx, sy))
							continue;
						colour = shade(WATER, heightAt(sx, sy), 255.0f, mid, 0.0f);
					}
					else
						colour = shade(colourAt(sx, sy), z, hi, mid, lo);
					for (int c = 0; c < 3; ++c)
						total[static_cast<std::size_t>(c)] += colour[static_cast<std::size_t>(c)];
					++samples;
				}
			}
			samples = std::max(samples, 1);
			std::uint32_t argb = 0xFF000000u;
			for (int c = 0; c < 3; ++c)
			{
				const float v = total[static_cast<std::size_t>(c)] / static_cast<float>(samples);
				argb |= static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)) << (16 - 8 * c);
			}
			pixels[static_cast<std::size_t>(py * PREVIEW_SIZE + px)] = argb;
		}
	}
	return pixels;
}

std::vector<std::uint8_t> previewTga(const std::vector<std::uint32_t> &pixels, int size)
{
	std::vector<std::uint8_t> tga(18, 0);
	tga[2] = 2;		// uncompressed true colour
	tga[12] = static_cast<std::uint8_t>(size & 0xFF);
	tga[13] = static_cast<std::uint8_t>(size >> 8);
	tga[14] = static_cast<std::uint8_t>(size & 0xFF);
	tga[15] = static_cast<std::uint8_t>(size >> 8);
	tga[16] = 32;
	tga[17] = 8;	// eight alpha bits, origin bottom left
	for (std::uint32_t p : pixels)
	{
		tga.push_back(static_cast<std::uint8_t>(p & 0xFF));			// blue
		tga.push_back(static_cast<std::uint8_t>((p >> 8) & 0xFF));	// green
		tga.push_back(static_cast<std::uint8_t>((p >> 16) & 0xFF));	// red
		tga.push_back(static_cast<std::uint8_t>(p >> 24));			// alpha
	}
	return tga;
}

} // namespace MapEditorCore
