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

// TextureBlend.h -- soft edges between terrain textures, as the old WorldBuilder's WorldHeightMapEdit
// makes them (blendTile, autoBlendOut): a cell keeps its tile and gets an alpha blend of the
// neighbouring texture on top, from one side, a corner or two sides; a second blend goes into the
// extra layer (three-way blends). Custom blend edge textures and the cliff UV fix-up of neighbouring
// cells are not done.

#pragma once

#include "Core/TerrainCommands.h"

#include <utility>
#include <vector>

namespace MapEditorCore
{

// The texture class (index into textureClasses) holding the tile index (tile * 4 + quarter), -1 if none.
[[nodiscard]] int classOfTile(const MapModel::BlendTiles &tiles, int tileIndex);

// The texture class of a cell; -1 if the cell is blended, unless base asks for its own tile's class.
[[nodiscard]] int cellClass(const MapModel::BlendTiles &tiles, int width, int height, int x, int y, bool base);

// Blends the texture of cell (srcX, srcY) -- or the class classIndex if not -1 -- over cell (x, y), as
// the old blend edge tool does; only the direction counts, not the distance. Returns the changed cells.
CellRect blendEdge(MapModel::BlendTiles &tiles, int width, int height, int x, int y, int srcX, int srcY, int classIndex = -1);

// Blends the edges of the texture regions that hold the seed cells out into the textures around them
// (auto edge). With a limit only the cells inside it change. Returns the changed cells.
CellRect autoBlend(MapModel::BlendTiles &tiles, int width, int height, const std::vector<std::pair<int, int>> &seeds,
	const CellRect *limit = nullptr);

// Soft edges after painting the class into rect: blends toward the class in the rectangle and one
// cell around it are removed and made anew from all of the class's cells there.
CellRect softenEdges(MapModel::BlendTiles &tiles, int width, int height, int classIndex, CellRect rect);

// Fills the texture region that holds cell (x, y) -- cells of its texture joined by their sides --
// with the class, or with replaceAll every cell of that texture on the map; blends of the old texture
// show the new one (old WorldBuilder's flood fill). Returns the changed cells.
CellRect fillTexture(MapModel::BlendTiles &tiles, int width, int height, int x, int y, int classIndex, bool replaceAll);

// Drops the texture classes no cell uses, packs the rest from tile 0 and rebuilds the blend table
// with only the entries in use (old WorldBuilder's optimizeTiles). Returns true if anything changed.
bool optimizeTextures(MapModel::BlendTiles &tiles, int width, int height);

} // namespace MapEditorCore
