/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Map.h -- the content of a .map file as plain values, without engine types.
//
// The model keeps what the file stores, not what the game makes of it: the game's reader fixes up
// old versions and bad data (objects below the terrain, build list heights, renamed script
// templates); that is left to the engine bridge. Every chunk is read in all versions the game reads
// and written in the newest one, in the order the old WorldBuilder writes them, so a map it saved
// comes back byte for byte.

#pragma once

#include "Model/Chunk.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace MapModel
{

// Chunk versions written (Common/MapReaderWriterInfo.h, SidesList.cpp, Scripts.cpp).
namespace Version
{
constexpr std::uint16_t HEIGHT_MAP = 4;
constexpr std::uint16_t BLEND_TILE = 8;
constexpr std::uint16_t WORLD_INFO = 1;
constexpr std::uint16_t SIDES = 3;
constexpr std::uint16_t OBJECTS = 3;
constexpr std::uint16_t TRIGGERS = 4;
constexpr std::uint16_t LIGHTING = 3;
constexpr std::uint16_t WAYPOINTS = 1;
// K_SCRIPTS_DATA_VERSION_1 in Scripts.cpp has no value of its own and follows
// K_SCRIPT_CONDITION_VERSION_4, so the game writes 5 (Generals maps have 4). Its reader ignores it.
constexpr std::uint16_t PLAYER_SCRIPTS = 5;
constexpr std::uint16_t SCRIPT_LIST = 1;
constexpr std::uint16_t SCRIPT_GROUP = 2;
constexpr std::uint16_t SCRIPT = 2;
constexpr std::uint16_t OR_CONDITION = 1;
constexpr std::uint16_t CONDITION = 4;
constexpr std::uint16_t ACTION = 2;
}

struct Point2
{
	std::int32_t x = 0;
	std::int32_t y = 0;

	bool operator==(const Point2 &) const = default;
};

struct HeightMap
{
	std::int32_t width = 0;
	std::int32_t height = 0;
	std::int32_t borderSize = 0;
	std::vector<Point2> boundaries;			// top-right corner of each playable area
	std::vector<std::uint8_t> heights;		// width * height, row by row

	bool operator==(const HeightMap &) const = default;
};

struct TextureClass
{
	std::int32_t firstTile = 0;
	std::int32_t numTiles = 0;
	std::int32_t width = 0;
	std::int32_t unused = 0;				// written by the game, never read
	std::string name;

	bool operator==(const TextureClass &) const = default;
};

struct EdgeTextureClass
{
	std::int32_t firstTile = 0;
	std::int32_t numTiles = 0;
	std::int32_t width = 0;
	std::string name;

	bool operator==(const EdgeTextureClass &) const = default;
};

struct BlendedTile
{
	std::int32_t blendIndex = 0;
	std::uint8_t horizontal = 0;
	std::uint8_t vertical = 0;
	std::uint8_t rightDiagonal = 0;
	std::uint8_t leftDiagonal = 0;
	std::uint8_t inverted = 0;
	std::uint8_t longDiagonal = 0;
	std::int32_t customBlendEdgeClass = -1;

	bool operator==(const BlendedTile &) const = default;
};

struct CliffInfo
{
	std::int32_t tileIndex = 0;
	std::array<float, 8> uv{};				// u0 v0 u1 v1 u2 v2 u3 v3
	std::uint8_t flip = 0;
	std::uint8_t mutant = 0;

	bool operator==(const CliffInfo &) const = default;
};

struct BlendTiles
{
	// One entry per height map cell.
	std::vector<std::int16_t> tiles;
	std::vector<std::int16_t> blends;
	std::vector<std::int16_t> extraBlends;
	std::vector<std::int16_t> cliffInfoIndices;
	// One bit per cell, rows of (width + 7) / 8 bytes. Empty if the file predates cliff flags;
	// the game then derives them from the heights.
	std::vector<std::uint8_t> cliffState;

	std::int32_t numBitmapTiles = 0;
	std::vector<TextureClass> textureClasses;
	std::int32_t numEdgeTiles = 0;
	std::vector<EdgeTextureClass> edgeTextureClasses;
	// Entry 0 of both tables is implicit in the file; these hold entries 1..n.
	std::vector<BlendedTile> blendedTiles;
	std::vector<CliffInfo> cliffInfos;
	// Count stored for an empty table. Normally 1 (entry 0 only), but some of the game's own maps store
	// 0; the game reads and writes either unchanged.
	std::int32_t emptyBlendedCount = 1;
	std::int32_t emptyCliffCount = 1;

	bool operator==(const BlendTiles &) const = default;
};

struct MapObject
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float angle = 0.0f;
	std::int32_t flags = 0;
	std::string templateName;
	PropertyMap properties;

	bool operator==(const MapObject &) const = default;
};

struct PolygonArea
{
	std::string name;
	std::string layer;
	std::int32_t id = 0;
	bool isWater = false;
	bool isRiver = false;
	std::int32_t riverStart = 0;
	std::vector<std::array<std::int32_t, 3>> points;

	bool operator==(const PolygonArea &) const = default;
};

struct Light
{
	std::array<float, 3> ambient{};
	std::array<float, 3> diffuse{};
	std::array<float, 3> position{};

	bool operator==(const Light &) const = default;
};

struct TimeOfDayLighting
{
	std::array<Light, 3> terrain{};
	std::array<Light, 3> objects{};

	bool operator==(const TimeOfDayLighting &) const = default;
};

struct GlobalLighting
{
	std::int32_t timeOfDay = 0;
	std::array<TimeOfDayLighting, 4> times{};	// morning, afternoon, evening, night
	std::optional<std::uint32_t> shadowColor;

	bool operator==(const GlobalLighting &) const = default;
};

struct WaypointLink
{
	std::int32_t from = 0;
	std::int32_t to = 0;

	bool operator==(const WaypointLink &) const = default;
};

// Scripts ---------------------------------------------------------------------------------------

// Parameter::ParameterType value for positions; all other types store int, real and string.
constexpr std::int32_t PARAMETER_COORD3D = 16;

struct ScriptParameter
{
	std::int32_t type = 0;
	std::int32_t intValue = 0;
	float realValue = 0.0f;
	std::string text;
	std::array<float, 3> coord{};

	bool operator==(const ScriptParameter &) const = default;
};

// A condition or action: the template index and, since condition version 4 and action version 2,
// its internal name, which the game uses to find the template again when the list changed.
struct ScriptCall
{
	std::int32_t type = 0;
	std::string internalName;
	std::vector<ScriptParameter> parameters;
	// Chunk version if older than the name keys (condition < 4, action < 2), else 0. Such calls are
	// written back in their version: the game reads them as before, including its fix-ups of very
	// old parameter lists, and the model cannot supply the name without the script templates.
	std::uint16_t legacyVersion = 0;

	bool operator==(const ScriptCall &) const = default;
};

struct OrCondition
{
	std::vector<ScriptCall> conditions;		// all must hold

	bool operator==(const OrCondition &) const = default;
};

struct Script
{
	std::string name;
	std::string comment;
	std::string conditionComment;
	std::string actionComment;
	bool isActive = true;
	bool isOneShot = true;
	bool easy = true;
	bool normal = true;
	bool hard = true;
	bool isSubroutine = false;
	std::int32_t delayEvaluationSeconds = 0;
	std::vector<OrCondition> conditions;	// any may hold
	std::vector<ScriptCall> actions;
	std::vector<ScriptCall> falseActions;

	bool operator==(const Script &) const = default;
};

struct ScriptGroup
{
	std::string name;
	bool isActive = true;
	bool isSubroutine = false;
	std::vector<Script> scripts;

	bool operator==(const ScriptGroup &) const = default;
};

struct ScriptList
{
	std::vector<Script> scripts;
	std::vector<ScriptGroup> groups;

	bool operator==(const ScriptList &) const = default;
};

// Players and teams -----------------------------------------------------------------------------

struct BuildListEntry
{
	std::string buildingName;
	std::string templateName;
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float angle = 0.0f;
	bool initiallyBuilt = false;
	std::int32_t numRebuilds = 0;
	std::string script;
	std::int32_t health = 100;
	bool whiner = true;
	bool unsellable = false;
	bool repairable = true;

	bool operator==(const BuildListEntry &) const = default;
};

struct Side
{
	PropertyMap properties;
	std::vector<BuildListEntry> buildList;
	ScriptList scripts;

	bool operator==(const Side &) const = default;
};

struct Sides
{
	std::vector<Side> sides;
	std::vector<PropertyMap> teams;
	bool hasScripts = true;					// the PlayerScriptsList chunk was present

	bool operator==(const Sides &) const = default;
};

// The whole map -----------------------------------------------------------------------------------

struct Map
{
	std::optional<HeightMap> heightMap;
	std::optional<BlendTiles> blendTiles;
	std::optional<PropertyMap> worldInfo;
	std::optional<Sides> sides;
	std::optional<std::vector<MapObject>> objects;
	std::optional<std::vector<PolygonArea>> polygons;
	std::optional<GlobalLighting> lighting;
	std::optional<std::vector<WaypointLink>> waypointLinks;

	// Top-level chunks the model does not interpret, kept as bytes.
	std::vector<StoredChunk> otherChunks;
	// Top-level chunk names in file order; the writer follows it and appends missing ones.
	std::vector<std::string> chunkOrder;
	// Name table of the file read; writing with it keeps all ids of the file.
	NameTable names;
};

} // namespace MapModel
