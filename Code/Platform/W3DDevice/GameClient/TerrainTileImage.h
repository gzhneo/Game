/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// TerrainTileImage.h -----------------------------------------------------------------------------
//
// Decodes a terrain tile file (TGA or DDS) into one BGRA picture for WorldHeightMap::readTiles.
// Free of engine headers so the render unit tests can link it alone.
//
// TGA: uncompressed or RLE true colour (image type 2 or 10), 24 or 32 bit, no colour map. Rows stay
// in file order (the origin flag is ignored, as the engine always did): the usual TGA with a lower
// left origin yields the bottom row first. Tile size 64 * s pixels, s from the ID field "ZHTS<s>"
// (s = 2 or 4), otherwise 1.
//
// DDS: a single 2D image in BC1, BC7, R8G8B8A8 or B8G8R8A8/X8 (UNORM or SRGB; the values are taken
// as stored, without colour space conversion), or BC4 (one channel, decoded to R). Mip level 0 is used unless the
// caller asks for another (texture package: the level of the chosen tier). Rows are flipped to bottom
// first, so a DDS (top-down) and a TGA with lower left origin of the same picture give the same
// result. The tile size is the image width divided by the tiles per row the caller expects, if
// that is 64, 128 or 256 pixels; otherwise 64.
// ------------------------------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <vector>

namespace TerrainTileImage
{
	constexpr int BASE_TILE_EXTENT = 64;

	// Enough bytes for readInfo: DDS magic, header and DX10 extension.
	constexpr std::size_t MAX_HEADER_BYTES = 148;

	enum class Format { TGA, DDS };

	struct Info
	{
		Format format = Format::TGA;
		int width = 0;
		int height = 0;
		int tileExtent = BASE_TILE_EXTENT;	// pixels per tile edge
		int mipLevels = 1;					// levels in the file (DDS)
		int mipLevel = 0;					// level width and height describe (DDS)
	};

	struct Image
	{
		Info info;
		std::vector<unsigned char> bgra;	// width * height pixels, bytes B G R A, rows as described above
	};

	bool isDds(const void *data, std::size_t size);

	// Tile size of a DDS of the given width: width / tilesPerRow if that is 64, 128 or 256, else 64.
	int ddsTileExtent(int width, int tilesPerRow);

	// Size and tile size of a DDS from its header (data may end after the header). tilesPerRow: tiles
	// per row the caller expects, 0 if unknown. mipLevel: level to describe, clamped to the last one in
	// the file. On failure error names the reason.
	bool readDdsInfo(const void *data, std::size_t size, int tilesPerRow, Info &info, const char **error, int mipLevel = 0);

	// Decodes a whole TGA or DDS file. tilesPerRow only affects DDS (see ddsTileExtent). mipLevel: the
	// DDS level to decode (see readDdsInfo); a TGA is halved that often with a box filter, but not below
	// tiles of 64 pixels.
	bool decode(const void *data, std::size_t size, int tilesPerRow, Image &image, const char **error, int mipLevel = 0);
}
