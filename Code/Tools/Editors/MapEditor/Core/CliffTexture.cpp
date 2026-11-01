/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// CliffTexture.cpp -- see CliffTexture.h.
//
// All arithmetic is float as in the old code, with the few double steps it had (literals 0.5, 0.7),
// so the stored UVs match what the old WorldBuilder wrote.

#include "Core/CliffTexture.h"

#include "Core/TextureBlend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <vector>

namespace MapEditorCore
{

namespace
{

constexpr float MAP_XY_FACTOR = 10.0f;
constexpr float MAP_HEIGHT_SCALE = MAP_XY_FACTOR / 16.0f;
constexpr float HEIGHT_SCALE = MAP_HEIGHT_SCALE / MAP_XY_FACTOR;
constexpr float STRETCH_LIMIT = 1.5f;					// stretching less than this is left alone
constexpr int TEXTURE_WIDTH = 2048;
constexpr int TILE_PIXEL_EXTENT = 64;
constexpr float TEX_PER_CELL = 32.0f / TEXTURE_WIDTH;	// 32 texels per cell
constexpr float MIN_U_SPAN = TEX_PER_CELL * 0.4f;
constexpr std::size_t NUM_CLIFF_INFO = 32384;			// entry 0 included

// TCliffInfo of the engine: corners 0..3 are lower left, lower right, upper right, upper left of the cell.
struct Cliff
{
	float u0 = 0.0f, v0 = 0.0f;
	float u1 = 0.0f, v1 = 0.0f;
	float u2 = 0.0f, v2 = 0.0f;
	float u3 = 0.0f, v3 = 0.0f;
	bool flip = false;
	bool mutant = false;		// distorted to fit its neighbours; not propagated further
	int tileIndex = 0;

	static Cliff from(const MapModel::CliffInfo &ci)
	{
		Cliff c;
		c.u0 = ci.uv[0];
		c.v0 = ci.uv[1];
		c.u1 = ci.uv[2];
		c.v1 = ci.uv[3];
		c.u2 = ci.uv[4];
		c.v2 = ci.uv[5];
		c.u3 = ci.uv[6];
		c.v3 = ci.uv[7];
		c.flip = ci.flip != 0;
		c.mutant = ci.mutant != 0;
		c.tileIndex = ci.tileIndex;
		return c;
	}

	MapModel::CliffInfo model() const
	{
		MapModel::CliffInfo ci;
		ci.tileIndex = tileIndex;
		ci.uv = {u0, v0, u1, v1, u2, v2, u3, v3};
		ci.flip = flip ? 1 : 0;
		ci.mutant = mutant ? 1 : 0;
		return ci;
	}
};

struct Vec2
{
	float x = 0.0f;
	float y = 0.0f;

	bool zero() const { return x == 0.0f && y == 0.0f; }

	// Vector2::Normalize of WWMath: a zero vector stays zero.
	void normalize()
	{
		const float len2 = x * x + y * y;
		if (len2 != 0.0f)
		{
			const float oolen = 1.0f / std::sqrt(len2);
			x *= oolen;
			y *= oolen;
		}
	}
};

struct Node
{
	int x = 0;
	int y = 0;
	float len = 0.0f;
};

void extend(float &lo, float &hi, float u)
{
	if (lo > u)
		lo = u;
	if (hi < u)
		hi = u;
}

bool narrow(float a, float b, float c)
{
	float lo = a;
	float hi = a;
	extend(lo, hi, b);
	extend(lo, hi, c);
	return (hi - lo) < MIN_U_SPAN;
}

// A triangle of either diagonal spans less than MIN_U_SPAN.
bool anyNarrow(const Cliff &c)
{
	bool tooSmall = false;
	if (narrow(c.u0, c.u1, c.u3))
		tooSmall = true;
	if (narrow(c.u1, c.u2, c.u3))
		tooSmall = true;
	if (narrow(c.u0, c.u1, c.u2))
		tooSmall = true;
	if (narrow(c.u0, c.u2, c.u3))
		tooSmall = true;
	return tooSmall;
}

void uRange(const Cliff &c, float &minU, float &maxU)
{
	minU = c.u0;
	maxU = minU;
	extend(minU, maxU, c.u1);
	extend(minU, maxU, c.u2);
	extend(minU, maxU, c.u3);
}

void shiftU(Cliff &c, float by)
{
	c.u0 += by;
	c.u1 += by;
	c.u2 += by;
	c.u3 += by;
}

bool usMatch(float u1, float u2)
{
	if (std::fabs(u1 - u2) < TEX_PER_CELL / 4)
		return true;
	if (u1 == 0 && u2 > 3 * TEX_PER_CELL)
		return true;
	if (u2 == 0 && u1 > 3 * TEX_PER_CELL)
		return true;
	return false;
}

// Moves the u range into [0, textureWidth] where the texture tiles; a cell straddling the seam is
// clamped to it, or marked mutant if clamping leaves a triangle too narrow. Returns true if it fits.
bool adjustForTiling(Cliff &c, float textureWidth)
{
	const float delta = TEX_PER_CELL / 100;
	float minU = 0.0f;
	float maxU = 0.0f;
	uRange(c, minU, maxU);

	if (static_cast<double>(maxU - minU) > static_cast<double>(textureWidth) * 0.5)
	{
		const float mid = (minU + maxU) / 2.0f;
		if (-minU > maxU - textureWidth)
		{
			// biased negative
			if (c.u0 < mid)
				c.u0 += textureWidth;
			if (c.u1 < mid)
				c.u1 += textureWidth;
			if (c.u2 < mid)
				c.u2 += textureWidth;
			if (c.u3 < mid)
				c.u3 += textureWidth;
		}
		else
		{
			// biased past the texture width
			if (c.u0 > mid)
				c.u0 -= textureWidth;
			if (c.u1 > mid)
				c.u1 -= textureWidth;
			if (c.u2 > mid)
				c.u2 -= textureWidth;
			if (c.u3 > mid)
				c.u3 -= textureWidth;
		}
		uRange(c, minU, maxU);
	}

	if (minU >= textureWidth - delta)
	{
		shiftU(c, -textureWidth);
		minU -= textureWidth;
		maxU -= textureWidth;
	}
	if (maxU < delta)
	{
		shiftU(c, textureWidth);
		minU += textureWidth;
		maxU += textureWidth;
	}

	const float uBorder = static_cast<float>(static_cast<double>(TEX_PER_CELL) * 0.5);
	if (minU >= uBorder && maxU < textureWidth - uBorder)
		return true;
	if (minU >= -delta && maxU <= textureWidth + delta)
	{
		if (c.u0 < uBorder)
			c.u0 = 0;
		if (c.u1 < uBorder)
			c.u1 = 0;
		if (c.u2 < uBorder)
			c.u2 = 0;
		if (c.u3 < uBorder)
			c.u3 = 0;
		if (c.u0 > textureWidth - uBorder)
			c.u0 = textureWidth;
		if (c.u1 > textureWidth - uBorder)
			c.u1 = textureWidth;
		if (c.u2 > textureWidth - uBorder)
			c.u2 = textureWidth;
		if (c.u3 > textureWidth - uBorder)
			c.u3 = textureWidth;
		return true;
	}

	if (minU < textureWidth - delta && maxU > textureWidth + delta)
	{
		shiftU(c, -textureWidth);
		minU -= textureWidth;
		maxU -= textureWidth;
	}
	const Cliff before = c;
	float offset = 0.0f;

	// Straddles u = 0.
	if (maxU > -minU)
	{
		offset = -minU;
		if (c.u0 < 0)
			c.u0 = 0;
		if (c.u1 < 0)
			c.u1 = 0;
		if (c.u2 < 0)
			c.u2 = 0;
		if (c.u3 < 0)
			c.u3 = 0;
	}
	else
	{
		offset = -maxU;
		if (c.u0 > 0)
			c.u0 = 0;
		if (c.u1 > 0)
			c.u1 = 0;
		if (c.u2 > 0)
			c.u2 = 0;
		if (c.u3 > 0)
			c.u3 = 0;
		shiftU(c, textureWidth);
	}
	if (anyNarrow(c))
	{
		c.u0 = before.u0 + offset;
		c.u1 = before.u1 + offset;
		c.u2 = before.u2 + offset;
		c.u3 = before.u3 + offset;
		if (maxU < -minU)
			shiftU(c, textureWidth);
		c.mutant = true;
	}
	return false;
}

// Largest height step along the four edges of the cell at ndx (lower left corner).
int maxEdgeDelta(const MapModel::HeightMap &hm, std::size_t ndx)
{
	const std::size_t w = static_cast<std::size_t>(hm.width);
	const int h0 = hm.heights[ndx];
	const int h1 = hm.heights[ndx + 1];
	const int h2 = hm.heights[ndx + w + 1];
	const int h3 = hm.heights[ndx + w];
	int maxH = std::abs(h0 - h1);
	int deltaH = std::abs(h1 - h2);
	if (deltaH > maxH)
		maxH = deltaH;
	deltaH = std::abs(h2 - h3);
	if (deltaH > maxH)
		maxH = deltaH;
	deltaH = std::abs(h3 - h0);
	if (deltaH > maxH)
		maxH = deltaH;
	return maxH;
}

bool steep(int maxH)
{
	return static_cast<float>(maxH) * HEIGHT_SCALE > STRETCH_LIMIT / 2.0f;
}

bool heightsValid(const MapModel::HeightMap &hm)
{
	return hm.width >= 2 && hm.height >= 2
		&& hm.heights.size() == static_cast<std::size_t>(hm.width) * static_cast<std::size_t>(hm.height);
}

class CliffMapper
{
public:
	CliffMapper(MapModel::BlendTiles &tiles, const MapModel::HeightMap &heights)
		: bt(tiles), hm(heights), width(heights.width), height(heights.height)
	{
	}

	bool valid() const
	{
		if (!heightsValid(hm))
			return false;
		const std::size_t cells = hm.heights.size();
		return bt.tiles.size() == cells && bt.blends.size() == cells && bt.extraBlends.size() == cells
			&& bt.cliffInfoIndices.size() == cells;
	}

	bool run(int xIndex, int yIndex);
	// A freshly painted flat cell next to mapped cliff cells of its texture gets a mapping that
	// continues theirs (setTileNdx with a single tile in the old editor).
	bool fitFlatCell(int xIndex, int yIndex);

private:
	MapModel::BlendTiles &bt;
	const MapModel::HeightMap &hm;
	int width;
	int height;
	std::vector<std::uint8_t> processed;
	bool painting = false;		// fitFlatCell: neighbours count by texture class, not by processed

	std::size_t index(int x, int y) const
	{
		return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
	}

	// Cliff table entry k (1..n), null for 0 or a bad index.
	const MapModel::CliffInfo *entry(int k) const
	{
		if (k < 1 || static_cast<std::size_t>(k) > bt.cliffInfos.size())
			return nullptr;
		return &bt.cliffInfos[static_cast<std::size_t>(k) - 1];
	}

	const MapModel::BlendedTile *blendEntry(int k) const
	{
		if (k < 1 || static_cast<std::size_t>(k) > bt.blendedTiles.size())
			return nullptr;
		return &bt.blendedTiles[static_cast<std::size_t>(k) - 1];
	}

	// The table is full at NUM_CLIFF_INFO entries; then the cell gets 0 (no mapping).
	std::int16_t addCliffInfo(const Cliff &c)
	{
		const std::size_t next = bt.cliffInfos.size() + 1;
		if (next >= NUM_CLIFF_INFO)
			return 0;
		bt.cliffInfos.push_back(c.model());
		return static_cast<std::int16_t>(next);
	}

	void updateForAdjacentCliffs(int xIndex, int yIndex, Cliff &cliffInfo) const;
	void updateFlatCellForAdjacentCliffs(int xIndex, int yIndex, int curTileClass);
};

// Takes over the shared corners of processed neighbours; the free corners keep the cell's own shape
// relative to them. Mutant neighbours count only if no other neighbour locks a corner.
void CliffMapper::updateForAdjacentCliffs(int xIndex, int yIndex, Cliff &cliffInfo) const
{
	Cliff tmpCliff = cliffInfo;
	for (int useMutant = 0; useMutant < 2; useMutant++)
	{
		bool lock0 = false;
		bool lock1 = false;
		bool lock2 = false;
		bool lock3 = false;
		for (int i = xIndex - 1; i < xIndex + 2; i += 1)
		{
			if (i < 0)
				continue;
			if (i >= width - 1)
				continue;
			for (int j = yIndex - 1; j < yIndex + 2; j += 1)
			{
				if (i == xIndex && j == yIndex)
					continue;
				if (i != xIndex && j != yIndex)
					continue;
				if (j < 0)
					continue;
				if (j >= height - 1)
					continue;
				const std::size_t ndx = index(i, j);
				if (!processed[ndx])
					continue;
				const MapModel::CliffInfo *stored = entry(bt.cliffInfoIndices[ndx]);
				if (!stored)
					continue;
				const Cliff info = Cliff::from(*stored);
				if (info.mutant && !useMutant)
					continue;
				bool shifted = false;
				if (i == xIndex)
				{
					if (j < yIndex)
					{
						// below
						if (lock0 && !usMatch(tmpCliff.u0, info.u3))
							shifted = true;
						if (!shifted)
						{
							tmpCliff.u0 = info.u3;
							tmpCliff.u1 = info.u2;
							tmpCliff.v0 = info.v3;
							tmpCliff.v1 = info.v2;
							lock0 = lock1 = true;
						}
					}
					else
					{
						// above
						if (lock3 && !usMatch(tmpCliff.u3, info.u0))
							shifted = true;
						if (!shifted)
						{
							tmpCliff.u3 = info.u0;
							tmpCliff.u2 = info.u1;
							tmpCliff.v3 = info.v0;
							tmpCliff.v2 = info.v1;
							lock2 = lock3 = true;
						}
					}
				}
				else if (j == yIndex)
				{
					if (i < xIndex)
					{
						// left
						tmpCliff.u0 = info.u1;
						tmpCliff.u3 = info.u2;
						tmpCliff.v0 = info.v1;
						tmpCliff.v3 = info.v2;
						lock0 = lock3 = true;
					}
					else
					{
						// right
						if (lock1 && !usMatch(tmpCliff.u1, info.u0))
							shifted = true;
						if (lock2 && !usMatch(tmpCliff.u2, info.u3))
							shifted = true;
						if (!shifted)
						{
							tmpCliff.u1 = info.u0;
							tmpCliff.u2 = info.u3;
							tmpCliff.v1 = info.v0;
							tmpCliff.v2 = info.v3;
							lock2 = lock1 = true;
						}
					}
				}
				if (shifted)
					tmpCliff.mutant = true;
			}
		}
		const int lockCount = int(lock0) + int(lock1) + int(lock2) + int(lock3);
		if (lockCount == 0)
			continue;
		if (lockCount == 4)
		{
			cliffInfo = tmpCliff;
			return;
		}
		// Free corners follow a locked neighbour corner with the cell's own offset. Without lock1 the
		// first corner follows corner 3 even if that one is free too.
		if (!lock0)
		{
			if (lock1)
			{
				const float dUa = cliffInfo.u0 - cliffInfo.u1;
				tmpCliff.u0 = tmpCliff.u1 + dUa;
				tmpCliff.v0 = tmpCliff.v1 + cliffInfo.v0 - cliffInfo.v1;
			}
			else
			{
				const float dUa = cliffInfo.u0 - cliffInfo.u3;
				tmpCliff.u0 = tmpCliff.u3 + dUa;
				tmpCliff.v0 = tmpCliff.v3 + cliffInfo.v0 - cliffInfo.v3;
			}
		}
		if (!lock1)
		{
			if (lock2)
			{
				const float dUa = cliffInfo.u1 - cliffInfo.u2;
				tmpCliff.u1 = tmpCliff.u2 + dUa;
				tmpCliff.v1 = tmpCliff.v2 + cliffInfo.v1 - cliffInfo.v2;
			}
			if (lock0)
			{
				const float dUa = cliffInfo.u1 - cliffInfo.u0;
				tmpCliff.u1 = tmpCliff.u0 + dUa;
				tmpCliff.v1 = tmpCliff.v0 + cliffInfo.v1 - cliffInfo.v0;
			}
		}
		if (!lock2)
		{
			if (lock1)
			{
				const float dUa = cliffInfo.u2 - cliffInfo.u1;
				tmpCliff.u2 = tmpCliff.u1 + dUa;
				tmpCliff.v2 = tmpCliff.v1 + cliffInfo.v2 - cliffInfo.v1;
			}
			if (lock3)
			{
				const float dUa = cliffInfo.u2 - cliffInfo.u3;
				tmpCliff.u2 = tmpCliff.u3 + dUa;
				tmpCliff.v2 = tmpCliff.v3 + cliffInfo.v2 - cliffInfo.v3;
			}
		}
		if (!lock3)
		{
			if (lock2)
			{
				const float dUa = cliffInfo.u3 - cliffInfo.u2;
				tmpCliff.u3 = tmpCliff.u2 + dUa;
				tmpCliff.v3 = tmpCliff.v2 + cliffInfo.v3 - cliffInfo.v2;
			}
			if (lock0)
			{
				const float dUa = cliffInfo.u3 - cliffInfo.u0;
				tmpCliff.u3 = tmpCliff.u0 + dUa;
				tmpCliff.v3 = tmpCliff.v0 + cliffInfo.v3 - cliffInfo.v0;
			}
		}

		if (lockCount == 3 && anyNarrow(tmpCliff))
		{
			// Rebuild the cell's own shape around the corner opposite the free one.
			if (!lock0)
			{
				tmpCliff.u1 = tmpCliff.u2 + cliffInfo.u1 - cliffInfo.u2;
				tmpCliff.v1 = tmpCliff.v2 + cliffInfo.v1 - cliffInfo.v2;
				tmpCliff.u3 = tmpCliff.u2 + cliffInfo.u3 - cliffInfo.u2;
				tmpCliff.v3 = tmpCliff.v2 + cliffInfo.v3 - cliffInfo.v2;
				tmpCliff.u0 = tmpCliff.u2 + cliffInfo.u0 - cliffInfo.u2;
				tmpCliff.v0 = tmpCliff.v2 + cliffInfo.v0 - cliffInfo.v2;
			}
			if (!lock1)
			{
				tmpCliff.u1 = tmpCliff.u3 + cliffInfo.u1 - cliffInfo.u3;
				tmpCliff.v1 = tmpCliff.v3 + cliffInfo.v1 - cliffInfo.v3;
				tmpCliff.u2 = tmpCliff.u3 + cliffInfo.u2 - cliffInfo.u3;
				tmpCliff.v2 = tmpCliff.v3 + cliffInfo.v2 - cliffInfo.v3;
				tmpCliff.u0 = tmpCliff.u3 + cliffInfo.u0 - cliffInfo.u3;
				tmpCliff.v0 = tmpCliff.v3 + cliffInfo.v0 - cliffInfo.v3;
			}
			if (!lock2)
			{
				tmpCliff.u1 = tmpCliff.u0 + cliffInfo.u1 - cliffInfo.u0;
				tmpCliff.v1 = tmpCliff.v0 + cliffInfo.v1 - cliffInfo.v0;
				tmpCliff.u3 = tmpCliff.u0 + cliffInfo.u3 - cliffInfo.u0;
				tmpCliff.v3 = tmpCliff.v0 + cliffInfo.v3 - cliffInfo.v0;
				tmpCliff.u2 = tmpCliff.u0 + cliffInfo.u2 - cliffInfo.u0;
				tmpCliff.v2 = tmpCliff.v0 + cliffInfo.v2 - cliffInfo.v0;
			}
			if (!lock3)
			{
				tmpCliff.u0 = tmpCliff.u1 + cliffInfo.u0 - cliffInfo.u1;
				tmpCliff.v0 = tmpCliff.v1 + cliffInfo.v0 - cliffInfo.v1;
				tmpCliff.u3 = tmpCliff.u1 + cliffInfo.u3 - cliffInfo.u1;
				tmpCliff.v3 = tmpCliff.v1 + cliffInfo.v3 - cliffInfo.v1;
				tmpCliff.u2 = tmpCliff.u1 + cliffInfo.u2 - cliffInfo.u1;
				tmpCliff.v2 = tmpCliff.v1 + cliffInfo.v2 - cliffInfo.v1;
			}
			tmpCliff.mutant = true;
		}

		// Nearly vertical free edges keep a minimum u distance to the locked side. The unit step
		// divides by the u difference and is not a number if that is zero.
		const float smallU = TEX_PER_CELL / 10.0f;
		const float minUDelta = static_cast<float>(static_cast<double>(TEX_PER_CELL) * 0.7);
		if (lockCount == 2)
		{
			if (!lock0 && !lock1)
			{
				if (std::fabs(cliffInfo.u0 - cliffInfo.u1) < smallU)
				{
					const float cliffAvgU = (cliffInfo.u0 + cliffInfo.u1) / 2;
					const float tmpAvgU = (tmpCliff.u0 + tmpCliff.u1) / 2;
					tmpCliff.u0 = tmpAvgU + cliffInfo.u0 - cliffAvgU;
					tmpCliff.u1 = tmpAvgU + cliffInfo.u1 - cliffAvgU;

					float uDelta = cliffInfo.u0 - cliffInfo.u3;
					uDelta /= std::fabs(uDelta);
					uDelta *= TEX_PER_CELL;
					if (std::fabs(tmpCliff.u0 - tmpCliff.u3) < minUDelta)
						tmpCliff.u3 = tmpCliff.u0 - uDelta;
					if (std::fabs(tmpCliff.u1 - tmpCliff.u2) < minUDelta)
						tmpCliff.u2 = tmpCliff.u1 - uDelta;
				}
			}
			else if (!lock1 && !lock2)
			{
				if (std::fabs(cliffInfo.u1 - cliffInfo.u2) < smallU)
				{
					const float cliffAvgU = (cliffInfo.u1 + cliffInfo.u2) / 2;
					const float tmpAvgU = (tmpCliff.u1 + tmpCliff.u2) / 2;
					tmpCliff.u1 = tmpAvgU + cliffInfo.u1 - cliffAvgU;
					tmpCliff.u2 = tmpAvgU + cliffInfo.u2 - cliffAvgU;

					float uDelta = cliffInfo.u1 - cliffInfo.u0;
					uDelta /= std::fabs(uDelta);
					uDelta *= TEX_PER_CELL;
					if (std::fabs(tmpCliff.u1 - tmpCliff.u0) < minUDelta)
						tmpCliff.u0 = tmpCliff.u1 - uDelta;
					if (std::fabs(tmpCliff.u2 - tmpCliff.u3) < minUDelta)
						tmpCliff.u3 = tmpCliff.u2 - uDelta;
				}
			}
			else if (!lock2 && !lock3)
			{
				if (std::fabs(cliffInfo.u2 - cliffInfo.u3) < smallU)
				{
					const float cliffAvgU = (cliffInfo.u2 + cliffInfo.u3) / 2;
					const float tmpAvgU = (tmpCliff.u2 + tmpCliff.u3) / 2;
					tmpCliff.u2 = tmpAvgU + cliffInfo.u2 - cliffAvgU;
					tmpCliff.u3 = tmpAvgU + cliffInfo.u3 - cliffAvgU;

					float uDelta = cliffInfo.u3 - cliffInfo.u0;
					uDelta /= std::fabs(uDelta);
					uDelta *= TEX_PER_CELL;
					if (std::fabs(tmpCliff.u3 - tmpCliff.u0) < minUDelta)
						tmpCliff.u0 = tmpCliff.u3 - uDelta;
					if (std::fabs(tmpCliff.u2 - tmpCliff.u1) < minUDelta)
						tmpCliff.u1 = tmpCliff.u2 - uDelta;
				}
			}
			else if (!lock3 && !lock0)
			{
				if (std::fabs(cliffInfo.u3 - cliffInfo.u0) < smallU)
				{
					const float cliffAvgU = (cliffInfo.u3 + cliffInfo.u0) / 2;
					const float tmpAvgU = (tmpCliff.u3 + tmpCliff.u0) / 2;
					tmpCliff.u3 = tmpAvgU + cliffInfo.u3 - cliffAvgU;
					tmpCliff.u0 = tmpAvgU + cliffInfo.u0 - cliffAvgU;

					float uDelta = cliffInfo.u0 - cliffInfo.u1;
					uDelta /= std::fabs(uDelta);
					uDelta *= TEX_PER_CELL;
					if (std::fabs(tmpCliff.u0 - tmpCliff.u1) < minUDelta)
						tmpCliff.u1 = tmpCliff.u0 - uDelta;
					// Follows u1, not u3, as the other three cases would suggest.
					if (std::fabs(tmpCliff.u3 - tmpCliff.u2) < minUDelta)
						tmpCliff.u2 = tmpCliff.u1 - uDelta;
				}
			}
		}
	}
	cliffInfo = tmpCliff;
}

// A flat cell between mapped cells gets a mapping that continues theirs (at least one side needed).
void CliffMapper::updateFlatCellForAdjacentCliffs(int xIndex, int yIndex, int curTileClass)
{
	Cliff tmpCliff;
	bool lock0 = false;
	bool lock1 = false;
	bool lock2 = false;
	bool lock3 = false;
	if (painting)
	{
		const MapModel::CliffInfo *own = entry(bt.cliffInfoIndices[index(xIndex, yIndex)]);
		if (own && classOfTile(bt, own->tileIndex) != curTileClass)
			return;
	}
	else if (processed[index(xIndex, yIndex)])
		return;
	float textureClassExtent = static_cast<float>(bt.textureClasses[static_cast<std::size_t>(curTileClass)].width * TILE_PIXEL_EXTENT);
	textureClassExtent /= static_cast<float>(TEXTURE_WIDTH);
	for (int i = xIndex - 1; i < xIndex + 2; i += 1)
	{
		if (i < 0)
			continue;
		if (i >= width - 1)
			continue;
		for (int j = yIndex - 1; j < yIndex + 2; j += 1)
		{
			if (i == xIndex && j == yIndex)
				continue;
			if (i != xIndex && j != yIndex)
				continue;
			if (j < 0)
				continue;
			if (j >= height - 1)
				continue;
			const std::size_t ndx = index(i, j);
			const MapModel::CliffInfo *stored = entry(bt.cliffInfoIndices[ndx]);
			if (!stored)
				continue;
			const Cliff info = Cliff::from(*stored);
			// When painting, only steep neighbours count: fitted flat cells would spread the mapping stroke by stroke.
			if (info.mutant || (painting ? classOfTile(bt, stored->tileIndex) != curTileClass || !isCliffCell(hm, i, j) : !processed[ndx]))
				continue;
			bool shifted = false;
			if (i == xIndex)
			{
				if (j < yIndex)
				{
					// below
					if (lock0 && !usMatch(tmpCliff.u0, info.u3))
						shifted = true;
					if (!shifted)
					{
						tmpCliff.u0 = info.u3;
						tmpCliff.u1 = info.u2;
						tmpCliff.v0 = info.v3;
						tmpCliff.v1 = info.v2;
						lock0 = lock1 = true;
					}
				}
				else
				{
					// above
					if (lock3 && !usMatch(tmpCliff.u3, info.u0))
						shifted = true;
					if (!shifted)
					{
						tmpCliff.u3 = info.u0;
						tmpCliff.u2 = info.u1;
						tmpCliff.v3 = info.v0;
						tmpCliff.v2 = info.v1;
						lock2 = lock3 = true;
					}
				}
			}
			else if (j == yIndex)
			{
				if (i < xIndex)
				{
					// left
					tmpCliff.u0 = info.u1;
					tmpCliff.u3 = info.u2;
					tmpCliff.v0 = info.v1;
					tmpCliff.v3 = info.v2;
					lock0 = lock3 = true;
				}
				else
				{
					// right
					if (lock1 && !usMatch(tmpCliff.u1, info.u0))
						shifted = true;
					if (lock2 && !usMatch(tmpCliff.u2, info.u3))
						shifted = true;
					if (!shifted)
					{
						tmpCliff.u1 = info.u0;
						tmpCliff.u2 = info.u3;
						tmpCliff.v1 = info.v0;
						tmpCliff.v2 = info.v3;
						lock2 = lock1 = true;
					}
				}
			}
		}
	}
	int lockCount = int(lock0) + int(lock1) + int(lock2) + int(lock3);
	if (lockCount < 2)
		return;

	if (lockCount == 4)
	{
		float minU = 0.0f;
		float maxU = 0.0f;
		uRange(tmpCliff, minU, maxU);
		const float delta = TEX_PER_CELL / 100;
		if (maxU - minU < delta)
		{
			// Degenerate: keep the side with the larger v step and rebuild the rest from it.
			tmpCliff.mutant = true;
			if (tmpCliff.v3 - tmpCliff.v0 > tmpCliff.v2 - tmpCliff.v0)
				lock1 = lock2 = false;
			else
				lock3 = lock2 = false;
			lockCount = 2;
		}
	}

	const std::int32_t tileIndex = bt.textureClasses[static_cast<std::size_t>(curTileClass)].firstTile << 2;
	if (lockCount == 4)
	{
		adjustForTiling(tmpCliff, textureClassExtent);
		tmpCliff.tileIndex = tileIndex;
		bt.cliffInfoIndices[index(xIndex, yIndex)] = addCliffInfo(tmpCliff);
		return;
	}

	Vec2 xVec;
	Vec2 yVec;
	bool gotXVec = false;
	bool gotYVec = false;
	if (lock0 && lock1)
	{
		gotXVec = true;
		xVec.x = tmpCliff.u1 - tmpCliff.u0;
		xVec.y = tmpCliff.v1 - tmpCliff.v0;
	}
	if (lock1 && lock2)
	{
		gotYVec = true;
		yVec.x = tmpCliff.u2 - tmpCliff.u1;
		yVec.y = tmpCliff.v2 - tmpCliff.v1;
	}
	if (lock2 && lock3)
	{
		gotXVec = true;
		xVec.x = tmpCliff.u2 - tmpCliff.u3;
		xVec.y = tmpCliff.v2 - tmpCliff.v3;
	}
	if (lock3 && lock0)
	{
		gotYVec = true;
		yVec.x = tmpCliff.u3 - tmpCliff.u0;
		yVec.y = tmpCliff.v3 - tmpCliff.v0;
	}
	if (!gotXVec && !gotYVec)
		return;
	if (gotXVec && !gotYVec)
	{
		yVec.y = -xVec.x;
		yVec.x = xVec.y;
	}
	if (gotYVec && !gotXVec)
	{
		xVec.y = yVec.x;
		xVec.x = -yVec.y;
	}

	if (!lock0)
	{
		if (lock1)
		{
			tmpCliff.u0 = tmpCliff.u1 - xVec.x;
			tmpCliff.v0 = tmpCliff.v1 - xVec.y;
		}
		else if (lock3)
		{
			tmpCliff.u0 = tmpCliff.u3 - yVec.x;
			tmpCliff.v0 = tmpCliff.v3 - yVec.y;
		}
	}
	if (!lock1)
	{
		if (lock0)
		{
			tmpCliff.u1 = tmpCliff.u0 + xVec.x;
			tmpCliff.v1 = tmpCliff.v0 + xVec.y;
		}
		else if (lock2)
		{
			tmpCliff.u1 = tmpCliff.u2 - yVec.x;
			tmpCliff.v1 = tmpCliff.v2 - yVec.y;
		}
	}
	if (!lock2)
	{
		if (lock3)
		{
			tmpCliff.u2 = tmpCliff.u3 + xVec.x;
			tmpCliff.v2 = tmpCliff.v3 + xVec.y;
		}
		else if (lock1)
		{
			tmpCliff.u2 = tmpCliff.u1 + yVec.x;
			tmpCliff.v2 = tmpCliff.v1 + yVec.y;
		}
	}
	if (!lock3)
	{
		if (lock2)
		{
			tmpCliff.u3 = tmpCliff.u2 - xVec.x;
			tmpCliff.v3 = tmpCliff.v2 - xVec.y;
		}
		else if (lock0)
		{
			tmpCliff.u3 = tmpCliff.u0 + yVec.x;
			tmpCliff.v3 = tmpCliff.v0 + yVec.y;
		}
	}
	tmpCliff.mutant = false;
	tmpCliff.tileIndex = tileIndex;
	adjustForTiling(tmpCliff, textureClassExtent);
	bt.cliffInfoIndices[index(xIndex, yIndex)] = addCliffInfo(tmpCliff);
}

bool CliffMapper::run(int xIndex, int yIndex)
{
	const int curTileClass = cellClass(bt, width, height, xIndex, yIndex, true);
	if (curTileClass < 0)
		return false;
	// The cell needs its upper right neighbour corner.
	if (xIndex >= width - 1 || yIndex >= height - 1)
		return false;
	const MapModel::TextureClass &textureClass = bt.textureClasses[static_cast<std::size_t>(curTileClass)];
	float textureClassExtent = static_cast<float>(textureClass.width * TILE_PIXEL_EXTENT);
	textureClassExtent /= static_cast<float>(TEXTURE_WIDTH);
	const float startU = textureClassExtent / 2;	// centred in the texture
	const float startV = 0;							// the v values are centred at the end
	const std::int32_t tileIndex = textureClass.firstTile << 2;
	const int minHeightDelta = static_cast<int>(0.7f / HEIGHT_SCALE);

	processed.assign(hm.heights.size(), 0);
	// Cells to visit in order; neighbours of mutant cells wait until the queue runs dry.
	std::deque<Node> toProcess;
	std::vector<Node> mutantNodes;
	std::vector<Node> unCliffyNodes;
	int nodesProcessed = 0;
	float loU = 1, loV = 1, hiU = -1, hiV = -1;

	toProcess.push_back(Node{xIndex, yIndex, 0.0f});
	while (!toProcess.empty())
	{
		const Node cur = toProcess.front();
		toProcess.pop_front();
		if (toProcess.empty() && !mutantNodes.empty())
		{
			toProcess.push_back(mutantNodes.back());
			mutantNodes.pop_back();
		}

		const std::size_t ndx = index(cur.x, cur.y);
		bool skip = processed[ndx] != 0;
		bool classMatch = cellClass(bt, width, height, cur.x, cur.y, true) == curTileClass;
		if (const MapModel::BlendedTile *blend = blendEntry(bt.blends[ndx]))
		{
			if (classOfTile(bt, blend->blendIndex) == curTileClass)
				classMatch = true;
		}
		if (!classMatch)
			skip = true;
		if (skip)
			continue;

		const std::size_t w = static_cast<std::size_t>(width);
		int h0 = hm.heights[ndx];
		int h1 = hm.heights[ndx + 1];
		int h2 = hm.heights[ndx + w + 1];
		int h3 = hm.heights[ndx + w];
		const bool isCliff = steep(maxEdgeDelta(hm, ndx));
		const bool flip = std::abs(h2 - h0) >= std::abs(h3 - h1);

		// Heights relative to corner 0.
		h1 -= h0;
		h2 -= h0;
		h3 -= h0;
		h0 = 0;

		if (!isCliff)
		{
			unCliffyNodes.push_back(cur);
			continue;
		}

		// u runs across the slope, v down it; corners too close in height are pulled apart a little
		// so the texture is not squeezed to a line.
		Cliff cliffInfo;
		Vec2 uVec;
		Vec2 uVec2;
		if (flip)
		{
			uVec = Vec2{static_cast<float>(h3 - h0), static_cast<float>(-(h1 - h0))};
			uVec2 = Vec2{static_cast<float>(h2 - h1), static_cast<float>(-(h2 - h3))};
			if (uVec.zero())
				uVec = uVec2;
			if (uVec2.zero())
				uVec2 = uVec;

			if (std::abs(h2 - h1) < minHeightDelta && std::abs(h2 - h3) < minHeightDelta)
			{
				int delta = std::abs(minHeightDelta - std::abs(h2 - h1));
				const int delta2 = std::abs(minHeightDelta - std::abs(h2 - h3));
				if (delta > delta2)
					delta = delta2;
				if (h2 < h0)
					delta = -delta;
				h2 += delta;
			}
			if (std::abs(h0 - h1) < minHeightDelta && std::abs(h0 - h3) < minHeightDelta)
			{
				int delta = std::abs(minHeightDelta - std::abs(h0 - h1));
				const int delta2 = std::abs(minHeightDelta - std::abs(h0 - h3));
				if (delta > delta2)
					delta = delta2;
				if (h0 < h2)
					delta = -delta;
				h0 += delta;
			}
		}
		else
		{
			uVec = Vec2{static_cast<float>(h2 - h1), static_cast<float>(-(h1 - h0))};
			uVec2 = Vec2{static_cast<float>(h3 - h0), static_cast<float>(-(h2 - h3))};
			if (uVec.zero())
				uVec = uVec2;
			if (uVec2.zero())
				uVec2 = uVec;

			if (std::abs(h1 - h0) < minHeightDelta && std::abs(h1 - h2) < minHeightDelta)
			{
				int delta = std::abs(minHeightDelta - std::abs(h1 - h0));
				const int delta2 = std::abs(minHeightDelta - std::abs(h1 - h2));
				if (delta > delta2)
					delta = delta2;
				if (h1 < h3)
					delta = -delta;
				h1 += delta;
			}
			if (std::abs(h3 - h0) < minHeightDelta && std::abs(h3 - h2) < minHeightDelta)
			{
				int delta = std::abs(minHeightDelta - std::abs(h3 - h0));
				const int delta2 = std::abs(minHeightDelta - std::abs(h3 - h2));
				if (delta > delta2)
					delta = delta2;
				if (h3 < h0)
					delta = -delta;
				h3 += delta;
			}
		}
		uVec.normalize();
		uVec.x *= TEX_PER_CELL;
		uVec.y *= TEX_PER_CELL;
		uVec2.normalize();
		uVec2.x *= TEX_PER_CELL;
		uVec2.y *= TEX_PER_CELL;
		cliffInfo.u0 = startU;
		cliffInfo.v0 = startV - (static_cast<float>(h0) * HEIGHT_SCALE * TEX_PER_CELL);
		cliffInfo.u1 = startU + uVec.x;
		cliffInfo.v1 = startV - (static_cast<float>(h1) * HEIGHT_SCALE * TEX_PER_CELL);
		cliffInfo.v2 = startV - (static_cast<float>(h2) * HEIGHT_SCALE * TEX_PER_CELL);
		cliffInfo.v3 = startV - (static_cast<float>(h3) * HEIGHT_SCALE * TEX_PER_CELL);
		if (flip)
		{
			cliffInfo.u2 = startU + uVec.x + uVec2.y;
			cliffInfo.u3 = startU + uVec.y;
		}
		else
		{
			cliffInfo.u2 = startU + uVec.x + uVec.y;
			cliffInfo.u3 = startU + uVec2.y;
		}
		cliffInfo.flip = flip;
		cliffInfo.mutant = false;
		cliffInfo.tileIndex = tileIndex;

		updateForAdjacentCliffs(cur.x, cur.y, cliffInfo);
		adjustForTiling(cliffInfo, textureClassExtent);

		bt.cliffInfoIndices[ndx] = addCliffInfo(cliffInfo);
		processed[ndx] = 1;
		for (const float u : {cliffInfo.u0, cliffInfo.u1, cliffInfo.u2, cliffInfo.u3})
			extend(loU, hiU, u);
		for (const float v : {cliffInfo.v0, cliffInfo.v1, cliffInfo.v2, cliffInfo.v3})
			extend(loV, hiV, v);

		// Unprocessed side neighbours, queued with the longest shared edge in texture space first.
		// The old code had no branch for the upper neighbour; it reuses the edge of the neighbour
		// before it (lower, else left, else none).
		std::array<Node, 4> pending{};
		std::array<bool, 4> present{};
		int k = 0;
		float dx = 0.0f;
		float dy = 0.0f;
		for (int i = cur.x - 1; i < cur.x + 2; i++)
		{
			if (i < 0)
				continue;
			if (i >= width - 1)
				continue;
			for (int j = cur.y - 1; j < cur.y + 2; j++)
			{
				if (i == cur.x && j == cur.y)
					continue;
				if (i != cur.x && j != cur.y)
					continue;
				if (j < 0)
					continue;
				if (j >= height - 1)
					continue;
				if (processed[index(i, j)])
					continue;
				if (i < cur.x)
				{
					// left
					dx = cliffInfo.u3 - cliffInfo.u0;
					dy = cliffInfo.v3 - cliffInfo.v0;
				}
				else if (i > cur.x)
				{
					// right
					dx = cliffInfo.u2 - cliffInfo.u1;
					dy = cliffInfo.v2 - cliffInfo.v1;
				}
				else if (j < cur.y)
				{
					// below
					dx = cliffInfo.u1 - cliffInfo.u0;
					dy = cliffInfo.v1 - cliffInfo.v0;
				}
				const std::size_t slot = static_cast<std::size_t>(k++);
				pending[slot] = Node{i, j, dx * dx + dy * dy};
				present[slot] = true;
			}
		}
		while (k)
		{
			float maxLen = -1;
			int curNdx = -1;
			for (int i = 0; i < k; i++)
			{
				const std::size_t slot = static_cast<std::size_t>(i);
				if (present[slot] && pending[slot].len > maxLen)
				{
					curNdx = i;
					maxLen = pending[slot].len;
				}
			}
			if (curNdx < 0)
				break;
			const std::size_t slot = static_cast<std::size_t>(curNdx);
			if (cliffInfo.mutant)
				mutantNodes.push_back(pending[slot]);
			else
				toProcess.push_back(pending[slot]);
			present[slot] = false;
		}
		nodesProcessed++;
	}

	// Flat cells last, newest first, so they can fit to all cliff cells around them.
	for (auto it = unCliffyNodes.rbegin(); it != unCliffyNodes.rend(); ++it)
	{
		const std::size_t ndx = index(it->x, it->y);
		if (!processed[ndx])
		{
			bt.cliffInfoIndices[ndx] = 0;
			updateFlatCellForAdjacentCliffs(it->x, it->y, curTileClass);
			processed[ndx] = 1;
		}
	}

	// Centre the v range of the cliff cells in the texture.
	if (nodesProcessed)
	{
		const float vDelta = (-textureClassExtent / 2) - (hiV + loV) / 2;
		for (std::size_t ndx = 0; ndx < processed.size(); ++ndx)
		{
			if (!processed[ndx] || bt.cliffInfoIndices[ndx] < 1)
				continue;
			const std::size_t k = static_cast<std::size_t>(bt.cliffInfoIndices[ndx]);
			if (k > bt.cliffInfos.size())
				continue;
			std::array<float, 8> &uv = bt.cliffInfos[k - 1].uv;
			uv[1] += vDelta;
			uv[3] += vDelta;
			uv[5] += vDelta;
			uv[7] += vDelta;
		}
	}
	return true;
}

} // namespace

bool isCliffCell(const MapModel::HeightMap &heights, int x, int y)
{
	if (!heightsValid(heights) || x < 0 || y < 0 || x >= heights.width - 1 || y >= heights.height - 1)
		return false;
	const std::size_t ndx = static_cast<std::size_t>(y) * static_cast<std::size_t>(heights.width) + static_cast<std::size_t>(x);
	return steep(maxEdgeDelta(heights, ndx));
}

bool textureCliffRegion(MapModel::BlendTiles &tiles, const MapModel::HeightMap &heights, int cellX, int cellY)
{
	CliffMapper mapper(tiles, heights);
	if (!mapper.valid() || cellX < 0 || cellY < 0 || cellX >= heights.width || cellY >= heights.height)
		return false;
	return mapper.run(cellX, cellY);
}

bool CliffMapper::fitFlatCell(int xIndex, int yIndex)
{
	if (!valid() || xIndex < 0 || yIndex < 0 || xIndex >= width - 1 || yIndex >= height - 1)
		return false;
	const std::size_t ndx = index(xIndex, yIndex);
	const int curTileClass = classOfTile(bt, bt.tiles[ndx]);
	if (curTileClass < 0 || static_cast<std::size_t>(curTileClass) >= bt.textureClasses.size())
		return false;
	const std::int16_t before = bt.cliffInfoIndices[ndx];
	painting = true;
	updateFlatCellForAdjacentCliffs(xIndex, yIndex, curTileClass);
	painting = false;
	return bt.cliffInfoIndices[ndx] != before;
}

int fitPaintedCells(MapModel::BlendTiles &tiles, const MapModel::HeightMap &heights, int x0, int y0, int x1, int y1)
{
	CliffMapper mapper(tiles, heights);
	if (!mapper.valid())
		return 0;
	int fitted = 0;
	for (int y = std::max(0, y0); y <= std::min(y1, heights.height - 2); ++y)
		for (int x = std::max(0, x0); x <= std::min(x1, heights.width - 2); ++x)
		{
			const std::size_t ndx = static_cast<std::size_t>(y) * static_cast<std::size_t>(heights.width) + static_cast<std::size_t>(x);
			// Only opaque flat cells: blends and existing mappings stay as they are.
			if (tiles.cliffInfoIndices[ndx] != 0 || tiles.blends[ndx] != 0 || tiles.extraBlends[ndx] != 0 || isCliffCell(heights, x, y))
				continue;
			if (mapper.fitFlatCell(x, y))
				++fitted;
		}
	return fitted;
}

int textureAllCliffs(MapModel::BlendTiles &tiles, const MapModel::HeightMap &heights)
{
	CliffMapper mapper(tiles, heights);
	if (!mapper.valid())
		return 0;
	int regions = 0;
	for (int y = 0; y < heights.height - 1; ++y)
	{
		for (int x = 0; x < heights.width - 1; ++x)
		{
			// A full table leaves further cells unmapped; retrying them would change nothing.
			if (tiles.cliffInfos.size() + 1 >= NUM_CLIFF_INFO)
				return regions;
			const std::size_t ndx = static_cast<std::size_t>(y) * static_cast<std::size_t>(heights.width) + static_cast<std::size_t>(x);
			if (tiles.cliffInfoIndices[ndx] != 0 || !isCliffCell(heights, x, y))
				continue;
			if (mapper.run(x, y))
				++regions;
		}
	}
	return regions;
}

int removeCliffMapping(MapModel::BlendTiles &tiles)
{
	int cells = 0;
	for (std::int16_t &k : tiles.cliffInfoIndices)
	{
		if (k != 0)
			++cells;
		k = 0;
	}
	tiles.cliffInfos.clear();
	return cells;
}

} // namespace MapEditorCore
