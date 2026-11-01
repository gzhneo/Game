/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapPreview.h -- the map's preview picture, as the old WorldBuilder's MapPreview makes it on every
// save: 128 x 128 pixels over the playable area, each the colour of the ground texture there made
// lighter above and darker below the average height, water a light blue, all averaged over the pixel's
// neighbours. The game shows it in the map lists (the .tga next to the .map).

#pragma once

#include "Model/Map.h"

#include <array>
#include <cstdint>
#include <vector>

namespace MapEditorCore
{

constexpr int PREVIEW_SIZE = 128;

// Colours 0 to 1 per texture class of the map (index as in BlendTiles::textureClasses); a class
// without one is grey.
using ClassColours = std::vector<std::array<float, 3>>;

// PREVIEW_SIZE x PREVIEW_SIZE pixels 0xAARRGGBB, the first row at the map's bottom (world y 0).
[[nodiscard]] std::vector<std::uint32_t> mapPreview(const MapModel::Map &map, const ClassColours &colours);

// An uncompressed 32-bit TGA of the pixels (rows bottom up, as stored).
[[nodiscard]] std::vector<std::uint8_t> previewTga(const std::vector<std::uint32_t> &pixels, int size = PREVIEW_SIZE);

} // namespace MapEditorCore
