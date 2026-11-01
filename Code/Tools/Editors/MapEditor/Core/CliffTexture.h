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

// CliffTexture.h -- the cliff texture adjustment of the old WorldBuilder (WorldHeightMapEdit::
// doCliffAdjustment and its helpers): steep cells get their own UV mapping (cliff info) so the texture
// runs down the slope instead of being stretched; flat cells next to them are mapped to fit. Numbers
// and order of the old code are kept, so the game reads the same values.

#pragma once

#include "Model/Map.h"

namespace MapEditorCore
{

// True if cell (x, y) is steep enough for a cliff mapping; false for cells off the map or in the
// last row or column, which have no four corners.
[[nodiscard]] bool isCliffCell(const MapModel::HeightMap &heights, int x, int y);

// Maps the cliff region of the texture of cell (cellX, cellY): starting there, it walks through the
// cells of that texture class joined by their sides and gives each steep cell a new cliff info, the
// flat cells at its rim one that fits. False if the cell has no texture class, lies in the last row
// or column, or the arrays do not match the height map.
bool textureCliffRegion(MapModel::BlendTiles &tiles, const MapModel::HeightMap &heights, int cellX, int cellY);

// Runs textureCliffRegion from every steep cell without a cliff mapping, row by row. Returns the
// number of regions mapped.
int textureAllCliffs(MapModel::BlendTiles &tiles, const MapModel::HeightMap &heights);

// Gives the opaque flat cells of the rectangle that border mapped cliff cells of their own texture a
// mapping that continues the cliff's, so freshly painted cells next to a cliff show no seam. Returns
// the number of cells mapped.
int fitPaintedCells(MapModel::BlendTiles &tiles, const MapModel::HeightMap &heights, int x0, int y0, int x1, int y1);

// Drops all cliff infos and the cells' references to them. Returns the number of cells that had one.
int removeCliffMapping(MapModel::BlendTiles &tiles);

} // namespace MapEditorCore
