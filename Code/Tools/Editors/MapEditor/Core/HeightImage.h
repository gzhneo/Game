/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// HeightImage.h -- the height map as a grey image and back (export and import of heights). The image
// is plain pixel values; reading and writing image files is up to the caller.

#pragma once

#include "Core/TerrainCommands.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace MapEditorCore
{

struct GreyImage
{
	int width = 0;
	int height = 0;
	int bits = 8;						// 8 or 16 (1 to 16 accepted); values range over 0 .. 2^bits - 1
	// Row by row; row 0 is the top of the image, the northern row of the map.
	std::vector<std::uint16_t> pixels;
};

// 8 bit, one pixel per height sample, north up as the 2D overview shows it: image row 0 is the
// height map's row height - 1.
[[nodiscard]] GreyImage heightsToImage(const MapModel::HeightMap &hm);

// width x height samples (row 0 = y 0) from the image: resampled bilinearly with the corners on the
// corners, the full value range of the image mapped to low..high, rounded to nearest. Empty if the
// image or the size is empty or the image lacks pixels.
[[nodiscard]] std::vector<std::uint8_t> heightsFromImage(const GreyImage &image, int width, int height, int low = 0, int high = 255);

// Replaces all heights of the map with the image (at the map's size) and sets the cliff flags from
// them, as the height brush does. The map is changed already; the returned step is recorded with
// History::record. Null if the map has no height map, the image is empty or nothing changed.
std::unique_ptr<EditHeights> importHeights(MapModel::Map &map, const GreyImage &image, int low, int high);

} // namespace MapEditorCore
