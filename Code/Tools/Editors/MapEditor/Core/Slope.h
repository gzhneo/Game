/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Slope.h -- how steep the ground is, cell by cell, measured as the game measures visible cliffs
// (BaseHeightMapRenderObjClass::evaluateAsVisibleCliff), and the colours of the slope map.

#pragma once

#include "Model/Map.h"

#include <cstdint>
#include <vector>

namespace MapEditorCore
{

// The steepest slope from the cell's corner (x, y) to its right, diagonal and upper corner, in
// degrees. Heights outside the map count as 0, as in the game.
[[nodiscard]] float cellSlope(const MapModel::HeightMap &hm, int x, int y);
// The slope of every cell, row by row.
[[nodiscard]] std::vector<float> slopeMap(const MapModel::HeightMap &hm);

struct SlopeColor
{
	std::uint8_t r = 0, g = 0, b = 0;
};
// Green on flat ground, through yellow at half the limit to red at the limit; steeper than the
// limit a dark red, so the cells the view marks stand out.
[[nodiscard]] SlopeColor slopeColor(float degrees, float limit);

// Colours over the terrain in the 3D view, one 0xAARRGGBB per height map cell (alpha 0: none): the
// cells at least half the limit steep in the slope colours (limit > 0), the cells units cannot
// cross red (cliffFlags). Empty if neither is asked for.
[[nodiscard]] std::vector<std::uint32_t> terrainTint(const MapModel::Map &map, float slopeLimit, bool cliffFlags);

} // namespace MapEditorCore
