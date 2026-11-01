/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapIO.h -- reading and writing the uncompressed chunk data of a .map file.
//
// Compression is a separate step (MapFile.h), so this part has no dependency at all and is tested
// on every platform.

#pragma once

#include "Model/Map.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace MapModel
{

struct ReadResult
{
	Map map;
	std::string error;						// empty on success
	std::vector<std::string> warnings;		// data the game would also ignore or repair
	// Chunks read in an older version than the one written, as "name vX -> vY (n)". Writing such a
	// map changes these chunks; upgradedChunks names the top-level chunks affected.
	std::vector<std::string> upgrades;
	std::vector<std::string> upgradedChunks;

	bool ok() const { return error.empty(); }
};

struct WriteResult
{
	std::vector<std::uint8_t> bytes;
	std::string error;

	bool ok() const { return error.empty(); }
};

ReadResult readMap(std::span<const std::uint8_t> data);

// Writes with the map's name table: ids of the file read stay, new names get new ids.
WriteResult writeMap(const Map &map);

// True if both maps hold the same content (all interpreted chunks and the stored ones); the name
// table and the chunk order are not compared.
bool sameContent(const Map &a, const Map &b);

// Cliff flags (BlendTiles::cliffState, one bit per cell): bytes per row, one cell's flag, and the flags of
// cells x0..x1, y0..y1 from the heights as the game computes them (a cell whose corner heights span more
// than the pathfinder's slope limit is a cliff).
std::size_t cliffRowBytes(std::int32_t width);
bool cliffFlag(const std::vector<std::uint8_t> &flags, std::int32_t width, std::int32_t x, std::int32_t y);
void setCliffFlag(std::vector<std::uint8_t> &flags, std::int32_t width, std::int32_t x, std::int32_t y, bool cliff);
void updateCliffFlags(const HeightMap &hm, std::vector<std::uint8_t> &flags, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1);

// A script file (.scb) as the old WorldBuilder's script export writes it (ScriptDialog::OnSave): the
// script lists, the players' names (with their properties if exported), and the objects, areas, teams
// and waypoint links the scripts name.
struct ScriptFile
{
	std::vector<ScriptList> lists;
	std::vector<std::string> playerNames;	// "**SELECTION**" alone when only a selection was exported
	std::vector<PropertyMap> players;		// the players' properties, if exported with them
	std::vector<MapObject> objects;
	std::vector<PolygonArea> polygons;
	std::vector<PropertyMap> teams;
	std::vector<WaypointLink> waypointLinks;

	bool operator==(const ScriptFile &) const = default;
};

// The name the export writes instead of the players when it holds a selection.
constexpr const char *SCRIPT_FILE_SELECTION = "**SELECTION**";

struct ScriptFileResult
{
	ScriptFile file;
	std::string error;
	std::vector<std::string> warnings;

	bool ok() const { return error.empty(); }
};

ScriptFileResult readScriptFile(std::span<const std::uint8_t> data);
// Areas are written in version 3 (without layer), which the old WorldBuilder reads.
WriteResult writeScriptFile(const ScriptFile &file);

// Names of the chunks the model interprets, in the order the old WorldBuilder writes them.
const std::vector<std::string> &knownChunkNames();

} // namespace MapModel
