/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Symmetry.h -- makes a map symmetric from one part: the heights, textures, cliff flags and objects of
// the source part are copied mirrored or turned onto the rest, as one undo step. Start waypoints get
// the next free Player_<n>_Start, other names a suffix, so scripts can tell the copies apart.

#pragma once

#include "Core/MapSize.h"
#include "Model/Map.h"

#include <memory>
#include <vector>

namespace MapEditorCore
{

enum class SymmetryMode
{
	MirrorLeftRight,	// the left half onto the right, mirrored at the vertical middle line
	MirrorBottomTop,	// the bottom half onto the top, mirrored at the horizontal middle line
	Rotate180,			// the left half onto the right, turned by 180 degrees about the centre
	FourQuadrants		// the bottom left quarter onto the other three, mirrored at both lines
};

struct SymmetryOptions
{
	SymmetryMode mode = SymmetryMode::MirrorLeftRight;
	bool fromOtherSide = false;		// source is the right (top) part instead of the left (bottom)
	bool terrain = true;			// heights, textures, cliff flags
	bool objects = true;			// objects and waypoints with their links
};

// World position of the grid point mirrored into the source part: the point (x, y) of the target
// takes its values from the returned point. False if (x, y) lies in the source part or on the line.
bool symmetrySource(SymmetryMode mode, bool fromOtherSide, float width, float height, float x, float y, float &srcX, float &srcY);

// The lines the mode mirrors at, in world units: x0, y0, x1, y1 per line (one line for the halves,
// two for the quarters).
[[nodiscard]] std::vector<float> symmetryLines(const MapModel::HeightMap &hm, SymmetryMode mode);

// The step; null if the map has no terrain or nothing changed.
std::unique_ptr<ResizeMap> symmetryStep(const MapModel::Map &map, const SymmetryOptions &options);

} // namespace MapEditorCore
