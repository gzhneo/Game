/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// TextureCommands.cpp -- see TextureCommands.h.

#include "Core/TextureCommands.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace MapEditorCore
{

namespace
{

constexpr int ATLAS_WIDTH = 2048;		// TEXTURE_WIDTH of the game
constexpr int TILE_PIXELS = 64;			// TILE_PIXEL_EXTENT
constexpr int TILE_GAP = 8;				// TILE_OFFSET
constexpr std::int32_t MAX_TILES = 1024;	// NUM_SOURCE_TILES

} // namespace

bool texturesFit(const std::vector<std::int32_t> &widths)
{
	// The packing of WorldHeightMap::updateTileTexturePositions: widest classes first, each into the
	// first row whose first free cell starts a free square of its width.
	constexpr int perRow = ATLAS_WIDTH / (TILE_PIXELS + TILE_GAP);
	std::vector<bool> used(static_cast<std::size_t>(perRow * perRow), false);
	const auto free = [&used](int row, int column) { return !used[static_cast<std::size_t>(row * perRow + column)]; };
	for (int tileWidth = perRow; tileWidth > 0; --tileWidth)
	{
		for (std::int32_t width : widths)
		{
			if (width != tileWidth)
				continue;
			bool found = false;
			int row = 0, column = 0;
			for (row = 0; row < perRow - width + 1 && !found; ++row)
			{
				for (column = 0; column < perRow - width + 1 && !found; ++column)
				{
					if (free(row, column))
					{
						bool open = true;
						for (int i = 0; i < width && open; ++i)
							for (int j = 0; j < width && open; ++j)
								open = free(row + j, column + i);
						found = open;
						break;
					}
				}
				if (found)
					break;
			}
			if (!found)
				return false;
			for (int i = 0; i < width; ++i)
				for (int j = 0; j < width; ++j)
					used[static_cast<std::size_t>((row + j) * perRow + column + i)] = true;
		}
	}
	return true;
}

int textureClassIndex(const MapModel::BlendTiles &tiles, const std::string &name)
{
	for (std::size_t i = 0; i < tiles.textureClasses.size(); ++i)
	{
		if (tiles.textureClasses[i].name == name)
			return static_cast<int>(i);
	}
	return -1;
}

int addTextureClass(MapModel::BlendTiles &tiles, const std::string &name, std::int32_t numTiles, std::int32_t width)
{
	if (numTiles < 1 || width < 1 || tiles.numBitmapTiles + numTiles > MAX_TILES)
		return -1;
	std::vector<std::int32_t> widths;
	for (const MapModel::TextureClass &c : tiles.textureClasses)
		widths.push_back(c.width);
	widths.push_back(width);
	if (!texturesFit(widths))
		return -1;
	MapModel::TextureClass added;
	added.firstTile = tiles.numBitmapTiles;
	added.numTiles = numTiles;
	added.width = width;
	added.name = name;
	tiles.textureClasses.push_back(added);
	tiles.numBitmapTiles += numTiles;
	return static_cast<int>(tiles.textureClasses.size()) - 1;
}

std::int16_t tileForClass(const MapModel::TextureClass &textureClass, int x, int y)
{
	// WorldHeightMapEdit::getTileNdxForClass: a tile covers 2 x 2 cells, the class repeats every width tiles.
	const int width = std::max(1, textureClass.width);
	const int tile = textureClass.firstTile + (x / 2) % width + width * ((y / 2) % width);
	return static_cast<std::int16_t>((tile << 2) + 2 * (y & 1) + (x & 1));
}

CellRect paintTexture(MapModel::BlendTiles &tiles, int width, int height, float cellX, float cellY, float radius, int classIndex)
{
	CellRect changed;
	const std::size_t cells = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
	if (classIndex < 0 || static_cast<std::size_t>(classIndex) >= tiles.textureClasses.size() || tiles.tiles.size() < cells
		|| tiles.blends.size() < cells || tiles.extraBlends.size() < cells || tiles.cliffInfoIndices.size() < cells)
		return changed;
	const MapModel::TextureClass &textureClass = tiles.textureClasses[static_cast<std::size_t>(classIndex)];
	// Half a cell more, so a size of 1 still paints the cell under the cursor.
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
			const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
			const std::int16_t tile = tileForClass(textureClass, x, y);
			if (tiles.tiles[i] == tile && tiles.blends[i] == 0 && tiles.extraBlends[i] == 0 && tiles.cliffInfoIndices[i] == 0)
				continue;
			// Opaque: no blend, no cliff mapping (as setTileNdx of the old WorldBuilder).
			tiles.tiles[i] = tile;
			tiles.blends[i] = 0;
			tiles.extraBlends[i] = 0;
			tiles.cliffInfoIndices[i] = 0;
			changed.unite(CellRect{x, y, x, y});
		}
	}
	return changed;
}

Changes EditTiles::changes() const
{
	Changes c;
	c.textures = true;
	// The view takes cliff mappings only with the whole map, like new texture classes.
	c.textureClasses = m_classesBefore.textureClasses != m_classesAfter.textureClasses || m_classesBefore.cliffInfos != m_classesAfter.cliffInfos;
	c.terrainRect = {m_rect.x0, m_rect.y0, m_rect.x1, m_rect.y1};
	return c;
}

void EditTiles::copy(MapModel::Map &map, const Cells &cells, const Classes &classes) const
{
	if (!map.blendTiles || !map.heightMap || m_rect.empty())
		return;
	MapModel::BlendTiles &bt = *map.blendTiles;
	bt.numBitmapTiles = classes.numBitmapTiles;
	bt.textureClasses = classes.textureClasses;
	bt.blendedTiles = classes.blendedTiles;
	bt.cliffInfos = classes.cliffInfos;
	const int width = map.heightMap->width;
	std::size_t n = 0;
	for (int y = m_rect.y0; y <= m_rect.y1; ++y)
	{
		for (int x = m_rect.x0; x <= m_rect.x1; ++x, ++n)
		{
			const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
			bt.tiles[i] = cells.tiles[n];
			bt.blends[i] = cells.blends[n];
			bt.extraBlends[i] = cells.extraBlends[n];
			bt.cliffInfoIndices[i] = cells.cliffs[n];
		}
	}
}

std::unique_ptr<EditTiles> tileStroke(const MapModel::Map &map, const MapModel::BlendTiles &before, CellRect rect, std::string name)
{
	if (!map.blendTiles || !map.heightMap || rect.empty())
		return nullptr;
	const MapModel::BlendTiles &now = *map.blendTiles;
	const int width = map.heightMap->width;
	EditTiles::Cells oldCells, newCells;
	bool changed = now.textureClasses != before.textureClasses || now.blendedTiles != before.blendedTiles || now.cliffInfos != before.cliffInfos;
	for (int y = rect.y0; y <= rect.y1; ++y)
	{
		for (int x = rect.x0; x <= rect.x1; ++x)
		{
			const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
			oldCells.tiles.push_back(before.tiles[i]);
			oldCells.blends.push_back(before.blends[i]);
			oldCells.extraBlends.push_back(before.extraBlends[i]);
			oldCells.cliffs.push_back(before.cliffInfoIndices[i]);
			newCells.tiles.push_back(now.tiles[i]);
			newCells.blends.push_back(now.blends[i]);
			newCells.extraBlends.push_back(now.extraBlends[i]);
			newCells.cliffs.push_back(now.cliffInfoIndices[i]);
			changed = changed || before.tiles[i] != now.tiles[i] || before.blends[i] != now.blends[i]
				|| before.extraBlends[i] != now.extraBlends[i] || before.cliffInfoIndices[i] != now.cliffInfoIndices[i];
		}
	}
	if (!changed)
		return nullptr;
	return std::make_unique<EditTiles>(std::move(name), rect, std::move(oldCells), std::move(newCells),
		EditTiles::Classes{before.numBitmapTiles, before.textureClasses, before.blendedTiles, before.cliffInfos},
		EditTiles::Classes{now.numBitmapTiles, now.textureClasses, now.blendedTiles, now.cliffInfos});
}

} // namespace MapEditorCore
