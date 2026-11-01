/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// HeightShapes.cpp -- see HeightShapes.h.

#include "Core/HeightShapes.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace MapEditorCore
{

namespace
{

constexpr float CELL = 10.0f;					// world units per height sample
constexpr float HEIGHT_SCALE = CELL / 16.0f;	// world units per height step
constexpr float PI = 3.14159265358979f;
constexpr float NaN = std::numeric_limits<float>::quiet_NaN();

bool validMap(const MapModel::HeightMap &hm)
{
	return hm.width > 0 && hm.height > 0 && hm.heights.size() >= static_cast<std::size_t>(hm.width) * static_cast<std::size_t>(hm.height);
}

std::uint8_t toHeight(float z)
{
	return static_cast<std::uint8_t>(std::clamp(std::floor(z / HEIGHT_SCALE + 0.5f), 0.0f, 255.0f));
}

// A mold of the given half extents from a height function; NaN marks points off the mold.
Mold makeMold(std::string name, float halfX, float halfY, const std::function<float(float, float)> &shape)
{
	Mold m;
	m.name = std::move(name);
	m.step = 2.5f;
	m.x0 = -halfX;
	m.y0 = -halfY;
	m.cols = static_cast<int>(std::round(2.0f * halfX / m.step)) + 1;
	m.rows = static_cast<int>(std::round(2.0f * halfY / m.step)) + 1;
	m.z.resize(static_cast<std::size_t>(m.cols) * static_cast<std::size_t>(m.rows));
	for (int r = 0; r < m.rows; ++r)
		for (int c = 0; c < m.cols; ++c)
			m.z[static_cast<std::size_t>(r) * static_cast<std::size_t>(m.cols) + static_cast<std::size_t>(c)]
				= shape(m.x0 + static_cast<float>(c) * m.step, m.y0 + static_cast<float>(r) * m.step);
	return m;
}

float bump(float t)
{
	return 0.5f * (1.0f + std::cos(PI * std::clamp(t, 0.0f, 1.0f)));
}

} // namespace

CellRect applyRamp(MapModel::HeightMap &hm, float x1, float y1, float z1, float x2, float y2, float z2, float width)
{
	CellRect changed;
	const float dx = x2 - x1;
	const float dy = y2 - y1;
	const float length2 = dx * dx + dy * dy;
	if (!validMap(hm) || length2 < 1.0f || width <= 0.0f)
		return changed;
	const float half = width * 0.5f;
	const float border = static_cast<float>(hm.borderSize);
	const int cx0 = std::max(0, static_cast<int>(std::floor((std::min(x1, x2) - half) / CELL + border)));
	const int cy0 = std::max(0, static_cast<int>(std::floor((std::min(y1, y2) - half) / CELL + border)));
	const int cx1 = std::min(hm.width - 1, static_cast<int>(std::ceil((std::max(x1, x2) + half) / CELL + border)));
	const int cy1 = std::min(hm.height - 1, static_cast<int>(std::ceil((std::max(y1, y2) + half) / CELL + border)));
	const float length = std::sqrt(length2);
	for (int j = cy0; j <= cy1; ++j)
	{
		for (int i = cx0; i <= cx1; ++i)
		{
			const float px = (static_cast<float>(i) - border) * CELL - x1;
			const float py = (static_cast<float>(j) - border) * CELL - y1;
			const float u = (px * dx + py * dy) / length2;
			const float across = std::abs(px * dy - py * dx) / length;
			// Inside the rectangle along the segment, with the ends included.
			if (u < -0.001f || u > 1.001f || across > half + 0.001f)
				continue;
			const std::uint8_t h = toHeight(z1 + std::clamp(u, 0.0f, 1.0f) * (z2 - z1));
			std::uint8_t &cell = hm.heights[static_cast<std::size_t>(j) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(i)];
			if (cell == h)
				continue;
			cell = h;
			changed.unite(CellRect{i, j, i, j});
		}
	}
	return changed;
}

float Mold::sample(float x, float y) const
{
	if (cols < 1 || rows < 1 || step <= 0.0f)
		return NaN;
	const float fx = (x - x0) / step;
	const float fy = (y - y0) / step;
	if (fx < 0.0f || fy < 0.0f || fx > static_cast<float>(cols - 1) || fy > static_cast<float>(rows - 1))
		return NaN;
	const int c0 = static_cast<int>(fx);
	const int r0 = static_cast<int>(fy);
	const int c1 = std::min(c0 + 1, cols - 1);
	const int r1 = std::min(r0 + 1, rows - 1);
	const auto at = [this](int c, int r) { return z[static_cast<std::size_t>(r) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(c)]; };
	const float a = at(c0, r0), b = at(c1, r0), c = at(c0, r1), d = at(c1, r1);
	const float tx = fx - static_cast<float>(c0);
	const float ty = fy - static_cast<float>(r0);
	if (std::isnan(a) || std::isnan(b) || std::isnan(c) || std::isnan(d))
	{
		// At the mold's rim: the nearest sample decides.
		return at(tx < 0.5f ? c0 : c1, ty < 0.5f ? r0 : r1);
	}
	return (a * (1.0f - tx) + b * tx) * (1.0f - ty) + (c * (1.0f - tx) + d * tx) * ty;
}

float Mold::radius() const
{
	float r = 0.0f;
	for (float x : {x0, x0 + static_cast<float>(cols - 1) * step})
		for (float y : {y0, y0 + static_cast<float>(rows - 1) * step})
			r = std::max(r, std::hypot(x, y));
	return r;
}

std::vector<Mold> builtInMolds()
{
	std::vector<Mold> molds;
	molds.push_back(makeMold("Hill", 60.0f, 60.0f, [](float x, float y) {
		const float r = std::hypot(x, y);
		return r > 60.0f ? NaN : 30.0f * bump(r / 60.0f);
	}));
	molds.push_back(makeMold("Crater", 80.0f, 80.0f, [](float x, float y) {
		const float r = std::hypot(x, y);
		if (r > 80.0f)
			return NaN;
		if (r <= 55.0f)
			return -20.0f + 32.0f * (r / 55.0f) * (r / 55.0f);
		return 12.0f * bump((r - 55.0f) / 25.0f);
	}));
	molds.push_back(makeMold("Plateau", 80.0f, 80.0f, [](float x, float y) {
		const float r = std::hypot(x, y);
		if (r > 80.0f)
			return NaN;
		return r <= 50.0f ? 0.0f : -30.0f * (1.0f - bump((r - 50.0f) / 30.0f));
	}));
	const auto ridge = [](float x, float y) {
		if (std::abs(x) > 80.0f || std::abs(y) > 25.0f)
			return NaN;
		const float along = std::abs(x) > 60.0f ? bump((std::abs(x) - 60.0f) / 20.0f) : 1.0f;
		return 25.0f * bump(std::abs(y) / 25.0f) * along;
	};
	molds.push_back(makeMold("Ridge", 80.0f, 25.0f, ridge));
	molds.push_back(makeMold("Trench", 80.0f, 25.0f, [ridge](float x, float y) { return -ridge(x, y); }));
	return molds;
}

CellRect moldFootprint(const MapModel::HeightMap &hm, const Mold &mold, const MoldPlacement &placement)
{
	CellRect rect;
	if (!validMap(hm) || mold.cols < 1 || mold.rows < 1 || placement.scale <= 0.0f)
		return rect;
	const float reach = mold.radius() * placement.scale;
	const float border = static_cast<float>(hm.borderSize);
	rect.x0 = std::max(0, static_cast<int>(std::ceil((placement.x - reach) / CELL + border)));
	rect.y0 = std::max(0, static_cast<int>(std::ceil((placement.y - reach) / CELL + border)));
	rect.x1 = std::min(hm.width - 1, static_cast<int>(std::floor((placement.x + reach) / CELL + border)));
	rect.y1 = std::min(hm.height - 1, static_cast<int>(std::floor((placement.y + reach) / CELL + border)));
	return rect;
}

CellRect applyMold(MapModel::HeightMap &hm, const std::vector<std::uint8_t> &original, const Mold &mold, const MoldPlacement &placement)
{
	const CellRect rect = moldFootprint(hm, mold, placement);
	if (rect.empty() || original.size() != hm.heights.size())
		return {};
	const float border = static_cast<float>(hm.borderSize);
	const float turn = -placement.angle * PI / 180.0f;
	const float cs = std::cos(turn);
	const float sn = std::sin(turn);
	for (int j = rect.y0; j <= rect.y1; ++j)
	{
		for (int i = rect.x0; i <= rect.x1; ++i)
		{
			const float wx = ((static_cast<float>(i) - border) * CELL - placement.x) / placement.scale;
			const float wy = ((static_cast<float>(j) - border) * CELL - placement.y) / placement.scale;
			const float z = mold.sample(wx * cs - wy * sn, wx * sn + wy * cs);
			if (std::isnan(z))
				continue;
			const std::size_t n = static_cast<std::size_t>(j) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(i);
			const std::uint8_t h = toHeight(z * placement.scale + placement.height);
			if ((placement.mode == MoldMode::RaiseOnly && h <= original[n]) || (placement.mode == MoldMode::LowerOnly && h >= original[n]))
				continue;
			hm.heights[n] = h;
		}
	}
	return rect;
}

} // namespace MapEditorCore
