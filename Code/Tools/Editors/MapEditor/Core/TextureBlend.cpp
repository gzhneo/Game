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

// TextureBlend.cpp -- see TextureBlend.h.

#include "Core/TextureBlend.h"

#include "Core/TextureCommands.h"

#include <algorithm>
#include <cstdint>

namespace MapEditorCore
{

namespace
{

constexpr std::size_t MAX_BLENDED_TILES = 16192;	// NUM_BLEND_TILES, entry 0 included
constexpr std::uint8_t INVERTED = 0x1;
constexpr std::uint8_t FLIPPED = 0x2;				// forced flip of a horizontal or vertical blend

struct Blender
{
	MapModel::BlendTiles &bt;
	int width;
	int height;
	CellRect changed;

	std::size_t index(int x, int y) const { return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x); }
	bool onMap(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }
	void touch(int x, int y) { changed.unite(CellRect{x, y, x, y}); }

	// The blend table entry k (1..n), null for 0 or a bad index.
	const MapModel::BlendedTile *entry(int k) const
	{
		if (k < 1 || static_cast<std::size_t>(k) > bt.blendedTiles.size())
			return nullptr;
		return &bt.blendedTiles[static_cast<std::size_t>(k) - 1];
	}

	int cls(int x, int y, bool base) const { return cellClass(bt, width, height, x, y, base); }

	// WorldHeightMapEdit::getTexClassNeighbors: cells off the map count as the nearest edge cell.
	void neighbours(int x, int y, int textureClass, int &sides, int &total) const
	{
		sides = 0;
		total = 0;
		for (int i = x - 1; i <= x + 1; ++i)
		{
			for (int j = y - 1; j <= y + 1; ++j)
			{
				if (i == x && j == y)
					continue;
				const int cx = std::clamp(i, 0, width - 1);
				const int cy = std::clamp(j, 0, height - 1);
				if (cls(cx, cy, false) == textureClass)
				{
					++total;
					if (i == x || j == y)
						++sides;
				}
			}
		}
	}

	int findOrCreate(const MapModel::BlendedTile &info)
	{
		const int source = info.blendIndex >> 2;
		if (source < 0 || source >= bt.numBitmapTiles)
			return -1;
		for (std::size_t k = 0; k < bt.blendedTiles.size(); ++k)
		{
			if (bt.blendedTiles[k] == info)
				return static_cast<int>(k) + 1;
		}
		if (bt.blendedTiles.size() + 1 >= MAX_BLENDED_TILES)
			return -1;
		bt.blendedTiles.push_back(info);
		return static_cast<int>(bt.blendedTiles.size());
	}

	// WorldHeightMapEdit::blendSpecificTiles with three-way blends on (the game's default).
	void blendSpecific(int x, int y, int srcX, int srcY, int curTile, int blendTile, bool longDiagonal)
	{
		MapModel::BlendedTile info;
		info.blendIndex = blendTile;
		info.longDiagonal = longDiagonal ? 1 : 0;
		info.customBlendEdgeClass = -1;

		const std::size_t i = index(x, y);
		// A cell with a blend gets the new one in its extra layer; both must agree on the diagonal's flip.
		bool hasBase = false;
		MapModel::BlendedTile base;
		bool baseNeedsFlip = false;
		bool baseIsDiagonal = false;
		if (const MapModel::BlendedTile *b = entry(bt.blends[i]))
		{
			hasBase = true;
			base = *b;
			baseIsDiagonal = base.rightDiagonal || base.leftDiagonal;
			baseNeedsFlip = (base.rightDiagonal && !base.inverted) || (base.leftDiagonal && base.inverted);
		}

		bool flipped = false;
		if (srcY == y)
		{
			info.horizontal = 1;
			info.inverted = srcX < x ? INVERTED : 0;
			if (hasBase && baseNeedsFlip)
				info.inverted |= FLIPPED;
		}
		else if (srcX == x)
		{
			info.vertical = 1;
			info.inverted = srcY < y ? INVERTED : 0;
			if (hasBase && baseNeedsFlip)
				info.inverted |= FLIPPED;
		}
		else
		{
			if (srcX > x)
				info.rightDiagonal = 1;
			else
				info.leftDiagonal = 1;
			info.inverted = srcY < y ? INVERTED : 0;
			if (longDiagonal)
			{
				info.inverted = info.inverted ? 0 : INVERTED;
				info.rightDiagonal = info.rightDiagonal ? 0 : 1;
				info.leftDiagonal = info.leftDiagonal ? 0 : 1;
			}
			flipped = (info.rightDiagonal && !info.inverted) || (info.leftDiagonal && info.inverted);
			if (hasBase && baseIsDiagonal && baseNeedsFlip != flipped)
				return;
		}
		if (hasBase && base.blendIndex == blendTile)
			return;

		const int added = findOrCreate(info);
		if (added < 0)
			return;
		bt.tiles[i] = static_cast<std::int16_t>(curTile);
		if (hasBase)
		{
			bt.extraBlends[i] = static_cast<std::int16_t>(added);
			if (flipped && !baseIsDiagonal)
			{
				// A flipped copy of the base blend, so other cells that use it stay as they are.
				MapModel::BlendedTile flippedBase = base;
				flippedBase.inverted |= FLIPPED;
				const int k = findOrCreate(flippedBase);
				if (k >= 0)
					bt.blends[i] = static_cast<std::int16_t>(k);
			}
		}
		else
		{
			bt.blends[i] = static_cast<std::int16_t>(added);
		}
		touch(x, y);
	}

	// WorldHeightMapEdit::blendTile.
	void blend(int x, int y, int srcX, int srcY, int textureClass)
	{
		const std::size_t i = index(x, y);
		int blendTile = bt.tiles[index(srcX, srcY)];
		const int curTile = bt.tiles[i];
		if (textureClass < 0)
			textureClass = cls(srcX, srcY, false);
		if (textureClass >= 0)
			blendTile = tileForClass(bt.textureClasses[static_cast<std::size_t>(textureClass)], x, y);
		if (curTile == blendTile)
		{
			if (bt.blends[i] != 0 || bt.extraBlends[i] != 0)
				touch(x, y);
			bt.blends[i] = 0;
			bt.extraBlends[i] = 0;
			return;
		}
		blendSpecific(x, y, srcX, srcY, curTile, blendTile, false);
	}

	// WorldHeightMapEdit::blendToThisClass: the cell is edged into by neighbours of the class.
	void blendToClass(int x, int y, int textureClass)
	{
		int sides = 0, total = 0;
		neighbours(x, y, textureClass, sides, total);
		if (total < 1 || sides > 2)
			return;
		if (sides < 2)
		{
			// One side, or else one corner.
			for (int i = x - 1; i <= x + 1; ++i)
			{
				for (int j = y - 1; j <= y + 1; ++j)
				{
					if (!onMap(i, j) || (i == x && j == y) || (sides == 1 && i != x && j != y))
						continue;
					if (cls(i, j, false) == textureClass)
					{
						blend(x, y, i, j, -1);
						return;
					}
				}
			}
			return;
		}
		// Two sides: a long diagonal blend, on purpose the way the old editor sets it up.
		bool top = false, bottom = false, left = false, right = false;
		for (int i = x - 1; i <= x + 1; ++i)
		{
			for (int j = y - 1; j <= y + 1; ++j)
			{
				if (!onMap(i, j) || (i == x && j == y) || (i != x && j != y))
					continue;
				if (cls(i, j, false) != textureClass)
					continue;
				if (i == x)
					(j > y ? top : bottom) = true;
				else
					(i < x ? left : right) = true;
			}
		}
		const int blendTile = tileForClass(bt.textureClasses[static_cast<std::size_t>(textureClass)], x, y);
		int srcX = x, srcY = y;
		if (top)
			--srcY;
		if (bottom)
			++srcY;
		if (left)
			++srcX;
		if (right)
			--srcX;
		blendSpecific(x, y, srcX, srcY, bt.tiles[index(x, y)], blendTile, true);
	}

	// WorldHeightMapEdit::autoBlendOut for all seeds of one class.
	void autoBlendClass(int textureClass, const std::vector<std::pair<int, int>> &seeds, const CellRect *limit)
	{
		const auto inside = [&](int x, int y)
		{
			return onMap(x, y) && (!limit || (x >= limit->x0 && x <= limit->x1 && y >= limit->y0 && y <= limit->y1));
		};
		std::vector<std::uint8_t> processed(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
		std::vector<std::pair<int, int>> open, edge;
		for (const auto &[x, y] : seeds)
		{
			if (!inside(x, y) || processed[index(x, y)])
				continue;
			processed[index(x, y)] = 1;
			open.push_back({x, y});
		}
		// The region of the class; a foreign cell mostly surrounded by it joins it.
		while (!open.empty())
		{
			const auto [nx, ny] = open.back();
			open.pop_back();
			for (int i = nx - 1; i <= nx + 1; ++i)
			{
				for (int j = ny - 1; j <= ny + 1; ++j)
				{
					if (!inside(i, j))
						continue;
					const std::size_t c = index(i, j);
					if (processed[c])
						continue;
					if (cls(i, j, true) != textureClass)
					{
						int sides = 0, total = 0;
						neighbours(i, j, textureClass, sides, total);
						if (sides <= 2 && total <= 5)
							continue;
						bt.tiles[c] = tileForClass(bt.textureClasses[static_cast<std::size_t>(textureClass)], i, j);
						bt.blends[c] = 0;
						bt.extraBlends[c] = 0;
						touch(i, j);
					}
					else if (bt.blends[c] > 0)
					{
						continue;
					}
					processed[c] = 1;
					open.push_back({i, j});
				}
			}
			int sides = 0, total = 0;
			neighbours(nx, ny, textureClass, sides, total);
			if (total < 8)
				edge.push_back({nx, ny});
		}
		// Blend the class into the foreign cells along the region's edge.
		std::fill(processed.begin(), processed.end(), std::uint8_t{0});
		for (const auto &[nx, ny] : edge)
		{
			for (int i = nx - 1; i <= nx + 1; ++i)
			{
				for (int j = ny - 1; j <= ny + 1; ++j)
				{
					if (!inside(i, j))
						continue;
					const std::size_t c = index(i, j);
					if (processed[c])
						continue;
					if (const MapModel::BlendedTile *b = entry(bt.blends[c]))
					{
						if (classOfTile(bt, b->blendIndex) == textureClass)
							continue;
					}
					if (cls(i, j, true) != textureClass)
						blendToClass(i, j, textureClass);
					processed[c] = 1;
				}
			}
		}
	}
};

bool validArrays(const MapModel::BlendTiles &tiles, int width, int height)
{
	if (width < 1 || height < 1)
		return false;
	const std::size_t cells = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
	return tiles.tiles.size() >= cells && tiles.blends.size() >= cells && tiles.extraBlends.size() >= cells;
}

} // namespace

int classOfTile(const MapModel::BlendTiles &tiles, int tileIndex)
{
	const int tile = tileIndex >> 2;
	for (std::size_t i = 0; i < tiles.textureClasses.size(); ++i)
	{
		const MapModel::TextureClass &c = tiles.textureClasses[i];
		if (c.firstTile >= 0 && tile >= c.firstTile && tile < c.firstTile + c.numTiles)
			return static_cast<int>(i);
	}
	return -1;
}

int cellClass(const MapModel::BlendTiles &tiles, int width, int height, int x, int y, bool base)
{
	if (x < 0 || y < 0 || x >= width || y >= height || !validArrays(tiles, width, height))
		return -1;
	const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
	if (!base && (tiles.blends[i] != 0 || tiles.extraBlends[i] != 0))
		return -1;
	return classOfTile(tiles, tiles.tiles[i]);
}

CellRect blendEdge(MapModel::BlendTiles &tiles, int width, int height, int x, int y, int srcX, int srcY, int classIndex)
{
	Blender b{tiles, width, height, {}};
	if (!validArrays(tiles, width, height) || !b.onMap(x, y) || !b.onMap(srcX, srcY) || (x == srcX && y == srcY)
		|| classIndex >= static_cast<int>(tiles.textureClasses.size()))
		return b.changed;
	b.blend(x, y, srcX, srcY, classIndex);
	return b.changed;
}

CellRect autoBlend(MapModel::BlendTiles &tiles, int width, int height, const std::vector<std::pair<int, int>> &seeds,
	const CellRect *limit)
{
	Blender b{tiles, width, height, {}};
	if (!validArrays(tiles, width, height) || seeds.empty())
		return b.changed;
	const int textureClass = b.cls(seeds.front().first, seeds.front().second, false);
	if (textureClass < 0)
		return b.changed;
	std::vector<std::pair<int, int>> same;
	for (const auto &[x, y] : seeds)
	{
		if (b.cls(x, y, false) == textureClass)
			same.push_back({x, y});
	}
	b.autoBlendClass(textureClass, same, limit);
	return b.changed;
}

CellRect softenEdges(MapModel::BlendTiles &tiles, int width, int height, int classIndex, CellRect rect)
{
	Blender b{tiles, width, height, {}};
	if (!validArrays(tiles, width, height) || rect.empty() || classIndex < 0
		|| classIndex >= static_cast<int>(tiles.textureClasses.size()))
		return b.changed;
	const CellRect limit{std::max(0, rect.x0 - 1), std::max(0, rect.y0 - 1), std::min(width - 1, rect.x1 + 1), std::min(height - 1, rect.y1 + 1)};
	const auto toward = [&](std::int16_t k)
	{
		const MapModel::BlendedTile *e = b.entry(k);
		return e && classOfTile(tiles, e->blendIndex) == classIndex;
	};
	std::vector<std::pair<int, int>> seeds;
	for (int y = limit.y0; y <= limit.y1; ++y)
	{
		for (int x = limit.x0; x <= limit.x1; ++x)
		{
			const std::size_t i = b.index(x, y);
			if (b.cls(x, y, true) != classIndex)
			{
				if (toward(tiles.extraBlends[i]))
				{
					tiles.extraBlends[i] = 0;
					b.touch(x, y);
				}
				if (toward(tiles.blends[i]))
				{
					tiles.blends[i] = tiles.extraBlends[i];
					tiles.extraBlends[i] = 0;
					b.touch(x, y);
				}
			}
			else if (tiles.blends[i] == 0 && tiles.extraBlends[i] == 0)
			{
				seeds.push_back({x, y});
			}
		}
	}
	b.autoBlendClass(classIndex, seeds, &limit);
	return b.changed;
}

CellRect fillTexture(MapModel::BlendTiles &tiles, int width, int height, int x, int y, int classIndex, bool replaceAll)
{
	Blender b{tiles, width, height, {}};
	if (!validArrays(tiles, width, height) || !b.onMap(x, y) || classIndex < 0 || classIndex >= static_cast<int>(tiles.textureClasses.size())
		|| tiles.cliffInfoIndices.size() < tiles.tiles.size())
		return b.changed;
	const int current = b.cls(x, y, true);
	if (current < 0 || current == classIndex)
		return b.changed;
	const MapModel::TextureClass &target = tiles.textureClasses[static_cast<std::size_t>(classIndex)];
	// A cell of the old texture gets the new one and keeps its blend; a blend of the old texture over
	// another cell shows the new one.
	const auto recolour = [&](int i, int j) {
		const std::size_t c = b.index(i, j);
		if (b.cls(i, j, true) == current)
		{
			tiles.tiles[c] = tileForClass(target, i, j);
			tiles.cliffInfoIndices[c] = 0;
			b.touch(i, j);
			return true;
		}
		for (std::int16_t *layer : {&tiles.blends[c], &tiles.extraBlends[c]})
		{
			const MapModel::BlendedTile *e = b.entry(*layer);
			if (!e || classOfTile(tiles, e->blendIndex) != current)
				continue;
			MapModel::BlendedTile info = *e;
			info.blendIndex = tileForClass(target, i, j);
			const int k = b.findOrCreate(info);
			if (k < 0)
				continue;
			*layer = static_cast<std::int16_t>(k);
			tiles.cliffInfoIndices[c] = 0;
			b.touch(i, j);
		}
		return false;
	};
	if (replaceAll)
	{
		for (int j = 0; j < height; ++j)
			for (int i = 0; i < width; ++i)
				recolour(i, j);
		return b.changed;
	}
	// The region of the old texture that holds the cell, across sides, not corners.
	std::vector<std::uint8_t> seen(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
	std::vector<std::pair<int, int>> open{{x, y}};
	seen[b.index(x, y)] = 1;
	while (!open.empty())
	{
		const auto [nx, ny] = open.back();
		open.pop_back();
		recolour(nx, ny);
		for (int i = nx - 1; i <= nx + 1; ++i)
		{
			for (int j = ny - 1; j <= ny + 1; ++j)
			{
				if (!b.onMap(i, j) || seen[b.index(i, j)])
					continue;
				if (b.cls(i, j, true) != current)
				{
					recolour(i, j);
					continue;
				}
				if (i != nx && j != ny)
					continue;
				seen[b.index(i, j)] = 1;
				open.push_back({i, j});
			}
		}
	}
	return b.changed;
}

bool optimizeTextures(MapModel::BlendTiles &tiles, int width, int height)
{
	if (!validArrays(tiles, width, height))
		return false;
	const std::size_t cells = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
	const std::size_t classCount = tiles.textureClasses.size();
	std::vector<bool> used(classCount, false);
	const auto use = [&](int tile) {
		const int c = classOfTile(tiles, tile);
		if (c >= 0)
			used[static_cast<std::size_t>(c)] = true;
	};
	const auto entry = [&tiles](int k) -> const MapModel::BlendedTile * {
		return k >= 1 && static_cast<std::size_t>(k) <= tiles.blendedTiles.size() ? &tiles.blendedTiles[static_cast<std::size_t>(k) - 1] : nullptr;
	};
	for (std::size_t i = 0; i < cells; ++i)
	{
		use(tiles.tiles[i]);
		for (int k : {static_cast<int>(tiles.blends[i]), static_cast<int>(tiles.extraBlends[i])})
			if (const MapModel::BlendedTile *e = entry(k))
				use(e->blendIndex);
	}
	for (const MapModel::CliffInfo &ci : tiles.cliffInfos)
		use(ci.tileIndex);

	// The classes still in use, packed from tile 0 in their old order.
	std::vector<int> newFirst(classCount, -1);
	std::vector<MapModel::TextureClass> classes;
	std::int32_t next = 0;
	for (std::size_t c = 0; c < classCount; ++c)
	{
		if (!used[c])
			continue;
		MapModel::TextureClass moved = tiles.textureClasses[c];
		newFirst[c] = next;
		moved.firstTile = next;
		next += moved.numTiles;
		classes.push_back(moved);
	}
	const auto remap = [&](int tile) {
		const int c = classOfTile(tiles, tile);
		if (c < 0)
			return tile;
		const int offset = (tile >> 2) - tiles.textureClasses[static_cast<std::size_t>(c)].firstTile;
		return ((newFirst[static_cast<std::size_t>(c)] + offset) << 2) | (tile & 3);
	};

	MapModel::BlendTiles result = tiles;
	result.blendedTiles.clear();
	Blender b{result, width, height, {}};
	for (std::size_t i = 0; i < cells; ++i)
	{
		result.tiles[i] = static_cast<std::int16_t>(remap(tiles.tiles[i]));
		// Only the blend entries cells use, each once; forced flips only where a second layer needs them.
		int base = 0;
		if (const MapModel::BlendedTile *e = entry(tiles.blends[i]))
		{
			MapModel::BlendedTile info = *e;
			info.blendIndex = remap(info.blendIndex);
			if (tiles.extraBlends[i] == 0)
				info.inverted = static_cast<std::uint8_t>(info.inverted & ~FLIPPED);
			base = info.blendIndex == result.tiles[i] ? 0 : std::max(0, b.findOrCreate(info));
		}
		result.blends[i] = static_cast<std::int16_t>(base);
		int extra = 0;
		if (const MapModel::BlendedTile *e = entry(tiles.extraBlends[i]); e && base > 0)
		{
			MapModel::BlendedTile info = *e;
			info.blendIndex = remap(info.blendIndex);
			if (info.blendIndex != result.tiles[i] && result.blendedTiles[static_cast<std::size_t>(base) - 1].blendIndex != info.blendIndex)
				extra = std::max(0, b.findOrCreate(info));
		}
		result.extraBlends[i] = static_cast<std::int16_t>(extra);
	}
	for (MapModel::CliffInfo &ci : result.cliffInfos)
		ci.tileIndex = remap(ci.tileIndex);
	result.textureClasses = std::move(classes);
	result.numBitmapTiles = next;
	if (result.blendedTiles.empty() && tiles.blendedTiles.empty())
		result.emptyBlendedCount = tiles.emptyBlendedCount;
	if (result == tiles)
		return false;
	tiles = std::move(result);
	return true;
}

} // namespace MapEditorCore
