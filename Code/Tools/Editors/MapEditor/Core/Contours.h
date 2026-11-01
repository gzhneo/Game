/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Contours.h -- contour lines of the height map for the 2D overview (the old WorldBuilder's contour
// display with its step and offset, ContourOptions).

#pragma once

#include "Model/Map.h"

#include <vector>

namespace MapEditorCore
{

// Lines at the heights offset + k * step (height map units, 0 to 255), as segments x1, y1, x2, y2 in
// sample coordinates (sample (0, 0) is the corner of the height map). Empty for a step below 1.
[[nodiscard]] std::vector<float> contourLines(const MapModel::HeightMap &heightMap, int step, int offset);

} // namespace MapEditorCore
