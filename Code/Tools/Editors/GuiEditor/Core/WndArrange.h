/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WndArrange.h -- arranging several windows: align their edges or centres, spread them evenly, give them the
// same size, and the guides a dragged window snaps to (edges and centres of the other windows). Windows move
// with their children. Without the engine and Qt.

#pragma once

#include "Core/WndEdit.h"

#include <optional>
#include <vector>

namespace WndModel
{

enum class Align
{
	Left,
	Right,
	Top,
	Bottom,
	CenterX,	// centres on one vertical line
	CenterY,	// centres on one horizontal line
};

// Aligns the windows to the last one (the one the properties show). Returns the number of windows moved.
int alignWindows(Layout &layout, const std::vector<WindowPath> &paths, Align align);
// Spreads the windows so the gaps between them are equal; the outermost two stay. Needs three windows.
int distributeWindows(Layout &layout, const std::vector<WindowPath> &paths, bool horizontal);
// Gives the windows the width, the height or both of the last one; the top left corner stays.
int sameSize(Layout &layout, const std::vector<WindowPath> &paths, bool width, bool height);

// Where a moved rectangle snaps to the others: the shift to add and the line it snapped to (layout pixels).
struct GuideSnap
{
	int dx = 0;
	int dy = 0;
	std::optional<int> lineX;	// vertical guide at this x
	std::optional<int> lineY;	// horizontal guide at this y
};
// Compares left, centre and right (top, centre, bottom) of the rectangle with those of the others and snaps
// the nearest pair closer than tolerance.
[[nodiscard]] GuideSnap snapToGuides(const Rect &moving, const std::vector<Rect> &others, int tolerance);

} // namespace WndModel
