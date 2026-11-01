/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// HeightShapes.h -- ramps and mesh molds on the height map, after the old WorldBuilder's RampTool and
// MeshMoldTool. A mold is a height field (sampled from a mesh by the engine, or one of the built-in
// shapes); placed with a position, height, scale and angle it sets the ground below it.

#pragma once

#include "Core/TerrainCommands.h"

#include <string>
#include <vector>

namespace MapEditorCore
{

// Sets the ground between two points (world x, y and height z) to a straight slope, width wide.
// Returns the changed cells.
CellRect applyRamp(MapModel::HeightMap &hm, float x1, float y1, float z1, float x2, float y2, float z2, float width);

// A height field in the mold's own units: cols x rows samples, step apart, the first at (x0, y0); a
// sample off the mold is NaN.
struct Mold
{
	std::string name;
	float x0 = 0.0f;
	float y0 = 0.0f;
	float step = 1.0f;
	int cols = 0;
	int rows = 0;
	std::vector<float> z;

	// The height at a point, NaN off the mold.
	float sample(float x, float y) const;
	// Distance of the farthest sample from the origin.
	float radius() const;
};

// The shapes the editor has without mold files: Hill, Crater, Plateau, Ridge, Trench.
[[nodiscard]] std::vector<Mold> builtInMolds();

enum class MoldMode
{
	Both,
	RaiseOnly,
	LowerOnly
};

struct MoldPlacement
{
	float x = 0.0f;			// world position of the mold's origin
	float y = 0.0f;
	float height = 0.0f;	// world height of the mold's zero
	float scale = 1.0f;
	float angle = 0.0f;		// degrees, counter-clockwise
	MoldMode mode = MoldMode::Both;
};

// The cells the mold can reach at the placement.
[[nodiscard]] CellRect moldFootprint(const MapModel::HeightMap &hm, const Mold &mold, const MoldPlacement &placement);

// Sets the cells below the mold, compared with the heights in original (the whole height map, for
// the raise-only and lower-only modes). Returns the footprint.
CellRect applyMold(MapModel::HeightMap &hm, const std::vector<std::uint8_t> &original, const Mold &mold, const MoldPlacement &placement);

} // namespace MapEditorCore
