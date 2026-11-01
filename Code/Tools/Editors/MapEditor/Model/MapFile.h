/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapFile.h -- .map files on disk: compression and the chunk data.
//
// The game packs maps with its own compression (Compression/CompressionManager): RefPack by
// default, or what the map's WorldInfo key "compression" asks for. The old WorldBuilder saves the
// same way; saving an unchanged map therefore gives the same file.

#pragma once

#include "Model/MapIO.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace MapModel
{

// CompressionType values of the game (Compression.h).
enum class Compression : std::int32_t
{
	None = 0,
	RefPack = 1,
};

struct Unpacked
{
	std::vector<std::uint8_t> data;
	std::int32_t compression = 0;			// CompressionType of the packed data, 0 if none
	std::string error;

	bool ok() const { return error.empty(); }
};

Unpacked unpack(std::span<const std::uint8_t> file);
bool pack(std::span<const std::uint8_t> data, std::int32_t compression, std::vector<std::uint8_t> &out, std::string &error);

// The compression the old WorldBuilder would use for this map.
std::int32_t compressionForSave(const Map &map);

struct LoadedMap
{
	ReadResult result;
	std::int32_t compression = 0;
	std::vector<std::uint8_t> fileBytes;	// as on disk
	std::vector<std::uint8_t> chunkBytes;	// unpacked
};

bool readFile(const std::filesystem::path &path, std::vector<std::uint8_t> &out, std::string &error);
bool writeFile(const std::filesystem::path &path, std::span<const std::uint8_t> data, std::string &error);

LoadedMap loadMap(const std::filesystem::path &path);

// Writes the map packed like the old WorldBuilder. Writes to a temporary file first and replaces
// the target only when that succeeded.
bool saveMap(const std::filesystem::path &path, const Map &map, std::string &error);

} // namespace MapModel
