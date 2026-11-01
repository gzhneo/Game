/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapImage.h -- top-down picture of a map drawn from the map model alone (no engine, no game data):
// ground colour from the texture class names, hill shading from the heights, cliffs, water areas,
// the playable boundary and, on request, objects, waypoints and start positions.
//
// Used for the previews on the start page and the overview in the viewer until the Direct3D view
// of the engine is embedded.

#pragma once

#include "Model/Map.h"

#include <QImage>
#include <QPointF>

namespace MapEditorApp
{

struct MapImageOptions
{
	bool objects = false;			// objects as small dots, waypoints, start positions
	bool boundary = true;
	bool waterAreas = true;			// water areas filled (the overview draws them itself)
	int maxSize = 0;				// longest side in pixels, 0 = one pixel per cell
};

QImage renderMapImage(const MapModel::Map &map, const MapImageOptions &options = {});

// Height map cell under a world position (x, y in world units), and the reverse. The image shows
// north up: row 0 is the highest cell row.
QPointF worldToCell(const MapModel::HeightMap &hm, float x, float y);

} // namespace MapEditorApp
