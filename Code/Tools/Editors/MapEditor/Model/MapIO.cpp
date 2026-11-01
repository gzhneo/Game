/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapIO.cpp -- reading and writing the uncompressed chunk data of a .map file.
//
// Field order and version handling follow the game's readers (WorldHeightMap.cpp, SidesList.cpp,
// Scripts.cpp, PolygonTrigger.cpp, TerrainLogic.cpp) and the old WorldBuilder's writer
// (WHeightMapEdit.cpp, WorldBuilderDoc.cpp).

#include "Model/MapIO.h"

#include <algorithm>
#include <map>
#include <set>

namespace MapModel
{

namespace
{

// Marker after each blended tile (WorldHeightMap.h).
constexpr std::int32_t BLEND_TILE_FLAG = 0x7ADA0000;

// Cliff flags of files without them, as WorldHeightMap::initCliffFlagsFromHeights computes them: a cell
// is a cliff if its corner heights span more than the pathfinder's slope limit. The last row and
// column stay clear.
constexpr float HEIGHT_SCALE = 10.0f / 16.0f;		// MAP_HEIGHT_SCALE
constexpr float CLIFF_RANGE = 9.8f;					// PATHFIND_CLIFF_SLOPE_LIMIT_F
constexpr std::uint16_t CONDITION_NAME_VERSION = 4;
constexpr std::uint16_t ACTION_NAME_VERSION = 2;

const char *const CHUNK_HEIGHT_MAP = "HeightMapData";
const char *const CHUNK_BLEND_TILES = "BlendTileData";
const char *const CHUNK_WORLD_INFO = "WorldInfo";
const char *const CHUNK_SIDES = "SidesList";
const char *const CHUNK_OBJECTS = "ObjectsList";
const char *const CHUNK_OBJECT = "Object";
const char *const CHUNK_TRIGGERS = "PolygonTriggers";
const char *const CHUNK_LIGHTING = "GlobalLighting";
const char *const CHUNK_WAYPOINTS = "WaypointsList";
const char *const CHUNK_PLAYER_SCRIPTS = "PlayerScriptsList";
const char *const CHUNK_SCRIPT_LIST = "ScriptList";
const char *const CHUNK_SCRIPT_GROUP = "ScriptGroup";
const char *const CHUNK_SCRIPT = "Script";
const char *const CHUNK_OR_CONDITION = "OrCondition";
const char *const CHUNK_CONDITION = "Condition";
const char *const CHUNK_ACTION = "ScriptAction";
const char *const CHUNK_FALSE_ACTION = "ScriptActionFalse";

std::vector<std::uint8_t> cliffFlagsFromHeights(const HeightMap &hm)
{
	std::vector<std::uint8_t> flags(cliffRowBytes(hm.width) * static_cast<std::size_t>(hm.height), 0);
	updateCliffFlags(hm, flags, 0, 0, hm.width - 1, hm.height - 1);
	return flags;
}

// ================================================================================================
// Reading

class Reader
{
public:
	Reader(const NameTable &names, std::vector<std::string> &warnings) : m_names(names), m_warnings(warnings) {}

	const std::string &error() const { return m_error; }
	bool failed() const { return !m_error.empty(); }

	void fail(const std::string &where, const std::string &message)
	{
		if (m_error.empty())
			m_error = where + ": " + message;
	}

	void warn(const std::string &message) { m_warnings.push_back(message); }

	// Top-level chunk being read, for the upgrade list.
	void setTop(const std::string &name) { m_top = name; }

	// Records a chunk read in another version than the one written.
	void version(const std::string &chunk, std::uint16_t have, std::uint16_t written)
	{
		if (have > written)
		{
			const std::string note = chunk + " v" + std::to_string(have) + " is newer than known (read as v" + std::to_string(written) + ")";
			if (std::find(m_warnings.begin(), m_warnings.end(), note) == m_warnings.end())
				warn(note);
		}
		else if (have < written)
		{
			++m_upgrades[chunk + " v" + std::to_string(have) + " -> v" + std::to_string(written)];
			m_upgradedTop.insert(m_top);
		}
	}

	void upgradeNote(const std::string &note)
	{
		++m_upgrades[note];
		m_upgradedTop.insert(m_top);
	}

	void report(ReadResult &result) const
	{
		for (const auto &[note, n] : m_upgrades)
			result.upgrades.push_back(note + " (" + std::to_string(n) + "x)");
		result.upgradedChunks.assign(m_upgradedTop.begin(), m_upgradedTop.end());
	}

	// Returns false and records the error if the reader failed.
	bool check(const ByteReader &r, const std::string &where)
	{
		if (!r.ok())
			fail(where, r.error());
		return r.ok();
	}

	void expectEnd(const ByteReader &r, const std::string &where)
	{
		if (r.ok() && !r.atEnd())
			warn(where + ": " + std::to_string(r.remaining()) + " bytes left over (ignored)");
	}

	bool children(std::span<const std::uint8_t> data, const std::string &where, std::vector<ChunkView> &out)
	{
		std::string error;
		if (!splitChunks(data, m_names, out, error))
		{
			fail(where, error);
			return false;
		}
		return true;
	}

	std::optional<HeightMap> heightMap(const ChunkView &c);
	std::optional<BlendTiles> blendTiles(const ChunkView &c, const HeightMap *heightMap);
	std::optional<std::vector<MapObject>> objects(const ChunkView &c);
	std::optional<std::vector<PolygonArea>> polygons(const ChunkView &c);
	std::optional<GlobalLighting> lighting(const ChunkView &c);
	std::optional<std::vector<WaypointLink>> waypoints(const ChunkView &c);
	std::optional<Sides> sides(const ChunkView &c);
	bool scriptLists(const ChunkView &c, std::vector<ScriptList> &out);

private:
	bool scriptList(const ChunkView &c, ScriptList &out);
	bool scriptGroup(const ChunkView &c, ScriptGroup &out);
	bool script(const ChunkView &c, Script &out);
	bool scriptCall(const ChunkView &c, bool hasName, ScriptCall &out);
	ScriptParameter parameter(ByteReader &r);

	const NameTable &m_names;
	std::vector<std::string> &m_warnings;
	std::string m_error;
	std::string m_top;
	std::map<std::string, int> m_upgrades;
	std::set<std::string> m_upgradedTop;
};

std::optional<HeightMap> Reader::heightMap(const ChunkView &c)
{
	ByteReader r(c.data);
	HeightMap hm;
	version(CHUNK_HEIGHT_MAP, c.version, Version::HEIGHT_MAP);
	if (c.version < 2)
	{
		fail(CHUNK_HEIGHT_MAP, "version " + std::to_string(c.version) + " (pre-release half-size cells) is not supported");
		return std::nullopt;
	}
	hm.width = r.i32();
	hm.height = r.i32();
	hm.borderSize = c.version >= 3 ? r.i32() : 0;
	if (c.version >= 4)
	{
		const std::size_t n = r.count(r.i32(), 8, "boundaries");
		for (std::size_t i = 0; i < n && r.ok(); ++i)
		{
			Point2 p;
			p.x = r.i32();
			p.y = r.i32();
			hm.boundaries.push_back(p);
		}
	}
	else
	{
		// Before version 4 the boundary is the map without its border; a border wider than half the map is corrupt.
		if (r.ok() && (hm.borderSize < 0 || hm.borderSize > hm.width / 2 || hm.borderSize > hm.height / 2))
		{
			fail(CHUNK_HEIGHT_MAP, "border " + std::to_string(hm.borderSize) + " does not fit " + std::to_string(hm.width) + " x " + std::to_string(hm.height));
			return std::nullopt;
		}
		hm.boundaries.push_back({hm.width - 2 * hm.borderSize, hm.height - 2 * hm.borderSize});
	}
	const std::int32_t dataSize = r.i32();
	if (r.ok() && (hm.width <= 0 || hm.height <= 0 || static_cast<std::int64_t>(hm.width) * hm.height != dataSize))
	{
		fail(CHUNK_HEIGHT_MAP, "size " + std::to_string(dataSize) + " does not match " + std::to_string(hm.width) + " x " + std::to_string(hm.height));
		return std::nullopt;
	}
	const auto heights = r.bytes(static_cast<std::size_t>(std::max(dataSize, 0)));
	hm.heights.assign(heights.begin(), heights.end());
	if (!check(r, CHUNK_HEIGHT_MAP))
		return std::nullopt;
	expectEnd(r, CHUNK_HEIGHT_MAP);
	return hm;
}

std::optional<BlendTiles> Reader::blendTiles(const ChunkView &c, const HeightMap *heightMap)
{
	if (heightMap == nullptr)
	{
		fail(CHUNK_BLEND_TILES, "comes before HeightMapData");
		return std::nullopt;
	}
	if (c.version < 2)
	{
		fail(CHUNK_BLEND_TILES, "version " + std::to_string(c.version) + " (pre-release half-size cells) is not supported");
		return std::nullopt;
	}
	version(CHUNK_BLEND_TILES, c.version, Version::BLEND_TILE);
	ByteReader r(c.data);
	BlendTiles bt;
	const std::size_t cells = heightMap->heights.size();
	const std::int32_t len = r.i32();
	if (r.ok() && static_cast<std::size_t>(len) != cells)
	{
		fail(CHUNK_BLEND_TILES, "cell count " + std::to_string(len) + " does not match the height map");
		return std::nullopt;
	}
	auto shorts = [&](std::vector<std::int16_t> &out) {
		const auto raw = r.bytes(cells * 2);
		out.resize(raw.size() / 2);
		for (std::size_t i = 0; i < out.size(); ++i)
			out[i] = static_cast<std::int16_t>(raw[2 * i] | (raw[2 * i + 1] << 8));
	};
	shorts(bt.tiles);
	shorts(bt.blends);
	if (c.version >= 6)
		shorts(bt.extraBlends);
	else
		bt.extraBlends.assign(cells, 0);
	if (c.version >= 5)
		shorts(bt.cliffInfoIndices);
	else
		bt.cliffInfoIndices.assign(cells, 0);

	const std::size_t rowBytes = cliffRowBytes(heightMap->width);
	const std::size_t rows = static_cast<std::size_t>(heightMap->height);
	if (c.version == 7)
	{
		// Version 7 was saved with a row length one byte too short for some widths.
		const std::size_t oldRow = static_cast<std::size_t>((heightMap->width + 1) / 8);
		const auto raw = r.bytes(rows * oldRow);
		bt.cliffState.assign(rows * rowBytes, 0);
		if (!raw.empty())
			for (std::size_t y = 0; y < rows; ++y)
				std::copy_n(raw.subspan(y * oldRow).begin(), oldRow, bt.cliffState.data() + y * rowBytes);
	}
	else if (c.version >= 8)
	{
		const auto raw = r.bytes(rows * rowBytes);
		bt.cliffState.assign(raw.begin(), raw.end());
	}
	else
	{
		bt.cliffState = cliffFlagsFromHeights(*heightMap);
		upgradeNote(std::string(CHUNK_BLEND_TILES) + ": cliff flags computed from the heights, like the game");
	}

	bt.numBitmapTiles = r.i32();
	const std::int32_t numBlended = r.i32();
	const std::int32_t numCliff = c.version >= 5 ? r.i32() : 1;

	const std::size_t numClasses = r.count(r.i32(), 4 * 4 + 2, "texture classes");
	for (std::size_t i = 0; i < numClasses && r.ok(); ++i)
	{
		TextureClass tc;
		tc.firstTile = r.i32();
		tc.numTiles = r.i32();
		tc.width = r.i32();
		tc.unused = r.i32();
		tc.name = readAscii(r);
		bt.textureClasses.push_back(std::move(tc));
	}
	if (c.version >= 4)
	{
		bt.numEdgeTiles = r.i32();
		const std::size_t numEdge = r.count(r.i32(), 3 * 4 + 2, "edge texture classes");
		for (std::size_t i = 0; i < numEdge && r.ok(); ++i)
		{
			EdgeTextureClass ec;
			ec.firstTile = r.i32();
			ec.numTiles = r.i32();
			ec.width = r.i32();
			ec.name = readAscii(r);
			bt.edgeTextureClasses.push_back(std::move(ec));
		}
	}
	if (numBlended <= 1)
		bt.emptyBlendedCount = numBlended;
	if (numCliff <= 1)
		bt.emptyCliffCount = numCliff;
	const std::size_t blendedEntries = r.count(numBlended > 0 ? numBlended - 1 : 0, 4 + 5 + 4, "blended tiles");
	for (std::size_t i = 0; i < blendedEntries && r.ok(); ++i)
	{
		BlendedTile t;
		t.blendIndex = r.i32();
		t.horizontal = r.u8();
		t.vertical = r.u8();
		t.rightDiagonal = r.u8();
		t.leftDiagonal = r.u8();
		t.inverted = r.u8();
		t.longDiagonal = c.version >= 3 ? r.u8() : 0;
		t.customBlendEdgeClass = c.version >= 4 ? r.i32() : -1;
		if (r.i32() != BLEND_TILE_FLAG && r.ok())
		{
			fail(CHUNK_BLEND_TILES, "blended tile " + std::to_string(i + 1) + " has a bad end marker");
			return std::nullopt;
		}
		bt.blendedTiles.push_back(t);
	}
	if (c.version >= 5)
	{
		const std::size_t cliffEntries = r.count(numCliff > 0 ? numCliff - 1 : 0, 4 + 8 * 4 + 2, "cliff entries");
		for (std::size_t i = 0; i < cliffEntries && r.ok(); ++i)
		{
			CliffInfo ci;
			ci.tileIndex = r.i32();
			for (float &v : ci.uv)
				v = r.f32();
			ci.flip = r.u8();
			ci.mutant = r.u8();
			bt.cliffInfos.push_back(ci);
		}
	}
	if (!check(r, CHUNK_BLEND_TILES))
		return std::nullopt;
	expectEnd(r, CHUNK_BLEND_TILES);
	return bt;
}

std::optional<std::vector<MapObject>> Reader::objects(const ChunkView &c)
{
	std::vector<ChunkView> list;
	if (!children(c.data, CHUNK_OBJECTS, list))
		return std::nullopt;
	version(CHUNK_OBJECTS, c.version, Version::OBJECTS);
	std::vector<MapObject> result;
	result.reserve(list.size());
	for (const ChunkView &o : list)
	{
		if (o.name != CHUNK_OBJECT)
		{
			warn(std::string(CHUNK_OBJECTS) + ": ignored chunk " + o.name);
			continue;
		}
		ByteReader r(o.data);
		MapObject obj;
		obj.x = r.f32();
		obj.y = r.f32();
		obj.z = r.f32();
		obj.angle = r.f32();
		obj.flags = r.i32();
		obj.templateName = readAscii(r);
		if (o.version >= 2)
			obj.properties = readDict(r, m_names);
		version(CHUNK_OBJECT, o.version, Version::OBJECTS);
		// Up to version 2 the game places every object on the ground and ignores the stored height.
		if (o.version <= 2)
			obj.z = 0.0f;
		if (!check(r, CHUNK_OBJECT))
			return std::nullopt;
		expectEnd(r, CHUNK_OBJECT);
		result.push_back(std::move(obj));
	}
	return result;
}

std::optional<std::vector<PolygonArea>> Reader::polygons(const ChunkView &c)
{
	version(CHUNK_TRIGGERS, c.version, Version::TRIGGERS);
	if (c.version == 1)
		warn(std::string(CHUNK_TRIGGERS) + " v1: the game adds a default water area from GameData.ini; not reproduced");
	ByteReader r(c.data);
	std::vector<PolygonArea> result;
	const std::size_t count = r.count(r.i32(), 2 + 4 + 4, "polygon triggers");
	for (std::size_t i = 0; i < count && r.ok(); ++i)
	{
		PolygonArea p;
		p.name = readAscii(r);
		if (c.version >= 4)
			p.layer = readAscii(r);
		p.id = r.i32();
		if (c.version >= 2)
			p.isWater = r.boolean();
		if (c.version >= 3)
		{
			p.isRiver = r.boolean();
			p.riverStart = r.i32();
		}
		const std::size_t points = r.count(r.i32(), 12, "polygon points");
		for (std::size_t k = 0; k < points && r.ok(); ++k)
		{
			std::array<std::int32_t, 3> pt{};
			pt[0] = r.i32();
			pt[1] = r.i32();
			pt[2] = r.i32();
			p.points.push_back(pt);
		}
		result.push_back(std::move(p));
	}
	if (!check(r, CHUNK_TRIGGERS))
		return std::nullopt;
	expectEnd(r, CHUNK_TRIGGERS);
	return result;
}

std::optional<GlobalLighting> Reader::lighting(const ChunkView &c)
{
	version(CHUNK_LIGHTING, c.version, Version::LIGHTING);
	ByteReader r(c.data);
	GlobalLighting gl;
	// Missing lights start like the game's defaults: black, pointing straight down.
	for (TimeOfDayLighting &t : gl.times)
	{
		for (Light &l : t.terrain)
			l.position = {0.0f, 0.0f, -1.0f};
		for (Light &l : t.objects)
			l.position = {0.0f, 0.0f, -1.0f};
	}
	auto light = [&](Light &l) {
		for (float &v : l.ambient)
			v = r.f32();
		for (float &v : l.diffuse)
			v = r.f32();
		for (float &v : l.position)
			v = r.f32();
	};
	gl.timeOfDay = r.i32();
	for (TimeOfDayLighting &t : gl.times)
	{
		light(t.terrain[0]);
		light(t.objects[0]);
		if (c.version >= 2)
		{
			light(t.objects[1]);
			light(t.objects[2]);
		}
		if (c.version >= 3)
		{
			light(t.terrain[1]);
			light(t.terrain[2]);
		}
	}
	if (r.ok() && !r.atEnd())
		gl.shadowColor = r.u32();
	if (!check(r, CHUNK_LIGHTING))
		return std::nullopt;
	expectEnd(r, CHUNK_LIGHTING);
	return gl;
}

std::optional<std::vector<WaypointLink>> Reader::waypoints(const ChunkView &c)
{
	version(CHUNK_WAYPOINTS, c.version, Version::WAYPOINTS);
	ByteReader r(c.data);
	std::vector<WaypointLink> links;
	const std::size_t count = r.count(r.i32(), 8, "waypoint links");
	for (std::size_t i = 0; i < count && r.ok(); ++i)
	{
		WaypointLink l;
		l.from = r.i32();
		l.to = r.i32();
		links.push_back(l);
	}
	if (!check(r, CHUNK_WAYPOINTS))
		return std::nullopt;
	expectEnd(r, CHUNK_WAYPOINTS);
	return links;
}

ScriptParameter Reader::parameter(ByteReader &r)
{
	ScriptParameter p;
	p.type = r.i32();
	if (p.type == PARAMETER_COORD3D)
	{
		for (float &v : p.coord)
			v = r.f32();
	}
	else
	{
		p.intValue = r.i32();
		p.realValue = r.f32();
		p.text = readAscii(r);
	}
	return p;
}

bool Reader::scriptCall(const ChunkView &c, bool hasName, ScriptCall &out)
{
	ByteReader r(c.data);
	out.type = r.i32();
	if (hasName)
		out.internalName = readNameKey(r, m_names);
	else
		out.legacyVersion = c.version;
	const std::size_t count = r.count(r.i32(), 4 + 4 + 4, "script parameters");
	for (std::size_t i = 0; i < count && r.ok(); ++i)
		out.parameters.push_back(parameter(r));
	if (!check(r, c.name))
		return false;
	expectEnd(r, c.name);
	return true;
}

bool Reader::script(const ChunkView &c, Script &out)
{
	version(CHUNK_SCRIPT, c.version, Version::SCRIPT);
	ByteReader r(c.data);
	out.name = readAscii(r);
	out.comment = readAscii(r);
	out.conditionComment = readAscii(r);
	out.actionComment = readAscii(r);
	out.isActive = r.boolean();
	out.isOneShot = r.boolean();
	out.easy = r.boolean();
	out.normal = r.boolean();
	out.hard = r.boolean();
	out.isSubroutine = r.boolean();
	if (c.version >= 2)
		out.delayEvaluationSeconds = r.i32();
	if (!check(r, CHUNK_SCRIPT))
		return false;
	std::vector<ChunkView> parts;
	if (!children(r.rest(), CHUNK_SCRIPT, parts))
		return false;
	for (const ChunkView &p : parts)
	{
		if (p.name == CHUNK_OR_CONDITION)
		{
			version(CHUNK_OR_CONDITION, p.version, Version::OR_CONDITION);
			std::vector<ChunkView> conditions;
			if (!children(p.data, CHUNK_OR_CONDITION, conditions))
				return false;
			OrCondition oc;
			for (const ChunkView &cond : conditions)
			{
				if (cond.name != CHUNK_CONDITION)
				{
					warn(std::string(CHUNK_OR_CONDITION) + ": ignored chunk " + cond.name);
					continue;
				}
				if (cond.version >= CONDITION_NAME_VERSION)
					version(CHUNK_CONDITION, cond.version, Version::CONDITION);
				ScriptCall call;
				if (!scriptCall(cond, cond.version >= CONDITION_NAME_VERSION, call))
					return false;
				oc.conditions.push_back(std::move(call));
			}
			out.conditions.push_back(std::move(oc));
		}
		else if (p.name == CHUNK_ACTION || p.name == CHUNK_FALSE_ACTION)
		{
			if (p.version >= ACTION_NAME_VERSION)
				version(p.name, p.version, Version::ACTION);
			ScriptCall call;
			if (!scriptCall(p, p.version >= ACTION_NAME_VERSION, call))
				return false;
			(p.name == CHUNK_ACTION ? out.actions : out.falseActions).push_back(std::move(call));
		}
		else
		{
			warn("script \"" + out.name + "\": ignored chunk " + p.name);
		}
	}
	return true;
}

bool Reader::scriptGroup(const ChunkView &c, ScriptGroup &out)
{
	version(CHUNK_SCRIPT_GROUP, c.version, Version::SCRIPT_GROUP);
	ByteReader r(c.data);
	out.name = readAscii(r);
	out.isActive = r.boolean();
	if (c.version >= 2)
		out.isSubroutine = r.boolean();
	if (!check(r, CHUNK_SCRIPT_GROUP))
		return false;
	std::vector<ChunkView> parts;
	if (!children(r.rest(), CHUNK_SCRIPT_GROUP, parts))
		return false;
	for (const ChunkView &p : parts)
	{
		if (p.name != CHUNK_SCRIPT)
		{
			warn("script group \"" + out.name + "\": ignored chunk " + p.name);
			continue;
		}
		Script s;
		if (!script(p, s))
			return false;
		out.scripts.push_back(std::move(s));
	}
	return true;
}

bool Reader::scriptList(const ChunkView &c, ScriptList &out)
{
	version(CHUNK_SCRIPT_LIST, c.version, Version::SCRIPT_LIST);
	std::vector<ChunkView> parts;
	if (!children(c.data, CHUNK_SCRIPT_LIST, parts))
		return false;
	for (const ChunkView &p : parts)
	{
		if (p.name == CHUNK_SCRIPT)
		{
			Script s;
			if (!script(p, s))
				return false;
			out.scripts.push_back(std::move(s));
		}
		else if (p.name == CHUNK_SCRIPT_GROUP)
		{
			ScriptGroup g;
			if (!scriptGroup(p, g))
				return false;
			out.groups.push_back(std::move(g));
		}
		else
		{
			warn(std::string(CHUNK_SCRIPT_LIST) + ": ignored chunk " + p.name);
		}
	}
	return true;
}

bool Reader::scriptLists(const ChunkView &c, std::vector<ScriptList> &out)
{
	version(CHUNK_PLAYER_SCRIPTS, c.version, Version::PLAYER_SCRIPTS);
	std::vector<ChunkView> lists;
	if (!children(c.data, CHUNK_PLAYER_SCRIPTS, lists))
		return false;
	for (const ChunkView &l : lists)
	{
		if (l.name != CHUNK_SCRIPT_LIST)
		{
			warn(std::string(CHUNK_PLAYER_SCRIPTS) + ": ignored chunk " + l.name);
			continue;
		}
		ScriptList sl;
		if (!scriptList(l, sl))
			return false;
		out.push_back(std::move(sl));
	}
	return true;
}

std::optional<Sides> Reader::sides(const ChunkView &c)
{
	version(CHUNK_SIDES, c.version, Version::SIDES);
	ByteReader r(c.data);
	Sides result;
	const std::size_t numSides = r.count(r.i32(), 2 + 4, "sides");
	for (std::size_t i = 0; i < numSides && r.ok(); ++i)
	{
		Side side;
		side.properties = readDict(r, m_names);
		const std::size_t numBuild = r.count(r.i32(), 2 + 2 + 4 * 4 + 1 + 4, "build list");
		for (std::size_t k = 0; k < numBuild && r.ok(); ++k)
		{
			BuildListEntry e;
			e.buildingName = readAscii(r);
			e.templateName = readAscii(r);
			e.x = r.f32();
			e.y = r.f32();
			e.z = r.f32();
			e.angle = r.f32();
			e.initiallyBuilt = r.boolean();
			e.numRebuilds = r.i32();
			if (c.version >= 3)
			{
				e.script = readAscii(r);
				e.health = r.i32();
				e.whiner = r.boolean();
				e.unsellable = r.boolean();
				e.repairable = r.boolean();
			}
			side.buildList.push_back(std::move(e));
		}
		result.sides.push_back(std::move(side));
	}
	if (c.version >= 2)
	{
		const std::size_t numTeams = r.count(r.i32(), 2, "teams");
		for (std::size_t i = 0; i < numTeams && r.ok(); ++i)
			result.teams.push_back(readDict(r, m_names));
	}
	if (!check(r, CHUNK_SIDES))
		return std::nullopt;

	std::vector<ChunkView> parts;
	if (!children(r.rest(), CHUNK_SIDES, parts))
		return std::nullopt;
	std::vector<ScriptList> lists;
	result.hasScripts = false;
	for (const ChunkView &p : parts)
	{
		if (p.name != CHUNK_PLAYER_SCRIPTS)
		{
			warn(std::string(CHUNK_SIDES) + ": ignored chunk " + p.name);
			continue;
		}
		result.hasScripts = true;
		if (!scriptLists(p, lists))
			return std::nullopt;
	}
	for (std::size_t i = 0; i < lists.size(); ++i)
	{
		if (i < result.sides.size())
			result.sides[i].scripts = std::move(lists[i]);
		else
			warn("script list " + std::to_string(i) + " has no side (dropped, like the game does)");
	}
	return result;
}

// ================================================================================================
// Writing

class Writer
{
public:
	Writer(ByteWriter &out, NameTable &names) : m_w(out, names) {}

	ChunkWriter &w() { return m_w; }
	std::string error;

	void fail(const std::string &message)
	{
		if (error.empty())
			error = message;
	}

	void heightMap(const HeightMap &hm);
	void blendTiles(const BlendTiles &bt, const HeightMap *hm);
	void worldInfo(const PropertyMap &dict);
	void sides(const Sides &s);
	void objects(const std::vector<MapObject> &list);
	void polygons(const std::vector<PolygonArea> &list);
	void lighting(const GlobalLighting &gl);
	void waypoints(const std::vector<WaypointLink> &links);
	void scriptLists(const std::vector<const ScriptList *> &lists);
	// The layout before layer names (version 3), as the old script export writes it.
	void polygonsV3(const std::vector<PolygonArea> &list);

private:
	void script(const Script &s);
	void call(const char *chunk, std::uint16_t version, std::uint16_t nameVersion, const ScriptCall &c);

	ChunkWriter m_w;
};

void Writer::heightMap(const HeightMap &hm)
{
	if (static_cast<std::int64_t>(hm.width) * hm.height != static_cast<std::int64_t>(hm.heights.size()))
		fail("HeightMapData: heights do not match width x height");
	ByteWriter &o = m_w.out();
	m_w.begin(CHUNK_HEIGHT_MAP, Version::HEIGHT_MAP);
	o.i32(hm.width);
	o.i32(hm.height);
	o.i32(hm.borderSize);
	o.i32(static_cast<std::int32_t>(hm.boundaries.size()));
	for (const Point2 &p : hm.boundaries)
	{
		o.i32(p.x);
		o.i32(p.y);
	}
	o.i32(static_cast<std::int32_t>(hm.heights.size()));
	o.bytes(hm.heights);
	m_w.end();
}

void Writer::blendTiles(const BlendTiles &bt, const HeightMap *hm)
{
	if (hm == nullptr)
	{
		fail("BlendTileData needs a height map");
		return;
	}
	const std::size_t cells = hm->heights.size();
	if (bt.tiles.size() != cells || bt.blends.size() != cells || bt.extraBlends.size() != cells || bt.cliffInfoIndices.size() != cells)
	{
		fail("BlendTileData: tile arrays do not match the height map");
		return;
	}
	const std::size_t cliffBytes = cliffRowBytes(hm->width) * static_cast<std::size_t>(hm->height);
	if (!bt.cliffState.empty() && bt.cliffState.size() != cliffBytes)
	{
		fail("BlendTileData: cliff flags do not match the height map");
		return;
	}
	ByteWriter &o = m_w.out();
	m_w.begin(CHUNK_BLEND_TILES, Version::BLEND_TILE);
	o.i32(static_cast<std::int32_t>(cells));
	for (const auto *array : {&bt.tiles, &bt.blends, &bt.extraBlends, &bt.cliffInfoIndices})
		for (std::int16_t v : *array)
			o.u16(static_cast<std::uint16_t>(v));
	if (bt.cliffState.empty())
		for (std::size_t i = 0; i < cliffBytes; ++i)
			o.u8(0);
	else
		o.bytes(bt.cliffState);
	o.i32(bt.numBitmapTiles);
	o.i32(bt.blendedTiles.empty() ? bt.emptyBlendedCount : static_cast<std::int32_t>(bt.blendedTiles.size() + 1));
	o.i32(bt.cliffInfos.empty() ? bt.emptyCliffCount : static_cast<std::int32_t>(bt.cliffInfos.size() + 1));
	o.i32(static_cast<std::int32_t>(bt.textureClasses.size()));
	for (const TextureClass &tc : bt.textureClasses)
	{
		o.i32(tc.firstTile);
		o.i32(tc.numTiles);
		o.i32(tc.width);
		o.i32(tc.unused);
		m_w.ascii(tc.name);
	}
	o.i32(bt.numEdgeTiles);
	o.i32(static_cast<std::int32_t>(bt.edgeTextureClasses.size()));
	for (const EdgeTextureClass &ec : bt.edgeTextureClasses)
	{
		o.i32(ec.firstTile);
		o.i32(ec.numTiles);
		o.i32(ec.width);
		m_w.ascii(ec.name);
	}
	for (const BlendedTile &t : bt.blendedTiles)
	{
		o.i32(t.blendIndex);
		o.u8(t.horizontal);
		o.u8(t.vertical);
		o.u8(t.rightDiagonal);
		o.u8(t.leftDiagonal);
		o.u8(t.inverted);
		o.u8(t.longDiagonal);
		o.i32(t.customBlendEdgeClass);
		o.i32(BLEND_TILE_FLAG);
	}
	for (const CliffInfo &ci : bt.cliffInfos)
	{
		o.i32(ci.tileIndex);
		for (float v : ci.uv)
			o.f32(v);
		o.u8(ci.flip);
		o.u8(ci.mutant);
	}
	m_w.end();
}

void Writer::worldInfo(const PropertyMap &dict)
{
	m_w.begin(CHUNK_WORLD_INFO, Version::WORLD_INFO);
	m_w.dict(dict);
	m_w.end();
}

void Writer::call(const char *chunk, std::uint16_t version, std::uint16_t nameVersion, const ScriptCall &c)
{
	// Without a name the call keeps its old chunk version (see ScriptCall::legacyVersion).
	const bool named = !c.internalName.empty();
	if (!named && (c.legacyVersion == 0 || c.legacyVersion >= nameVersion))
		fail(std::string(chunk) + " " + std::to_string(c.type) + " has no internal name");
	ByteWriter &o = m_w.out();
	m_w.begin(chunk, named ? version : c.legacyVersion);
	o.i32(c.type);
	if (named)
		m_w.nameKey(c.internalName);
	o.i32(static_cast<std::int32_t>(c.parameters.size()));
	for (const ScriptParameter &p : c.parameters)
	{
		o.i32(p.type);
		if (p.type == PARAMETER_COORD3D)
		{
			for (float v : p.coord)
				o.f32(v);
		}
		else
		{
			o.i32(p.intValue);
			o.f32(p.realValue);
			m_w.ascii(p.text);
		}
	}
	m_w.end();
}

void Writer::script(const Script &s)
{
	ByteWriter &o = m_w.out();
	m_w.begin(CHUNK_SCRIPT, Version::SCRIPT);
	m_w.ascii(s.name);
	m_w.ascii(s.comment);
	m_w.ascii(s.conditionComment);
	m_w.ascii(s.actionComment);
	o.boolean(s.isActive);
	o.boolean(s.isOneShot);
	o.boolean(s.easy);
	o.boolean(s.normal);
	o.boolean(s.hard);
	o.boolean(s.isSubroutine);
	o.i32(s.delayEvaluationSeconds);
	for (const OrCondition &oc : s.conditions)
	{
		m_w.begin(CHUNK_OR_CONDITION, Version::OR_CONDITION);
		for (const ScriptCall &c : oc.conditions)
			call(CHUNK_CONDITION, Version::CONDITION, CONDITION_NAME_VERSION, c);
		m_w.end();
	}
	for (const ScriptCall &a : s.actions)
		call(CHUNK_ACTION, Version::ACTION, ACTION_NAME_VERSION, a);
	for (const ScriptCall &a : s.falseActions)
		call(CHUNK_FALSE_ACTION, Version::ACTION, ACTION_NAME_VERSION, a);
	m_w.end();
}

void Writer::sides(const Sides &s)
{
	ByteWriter &o = m_w.out();
	m_w.begin(CHUNK_SIDES, Version::SIDES);
	o.i32(static_cast<std::int32_t>(s.sides.size()));
	for (const Side &side : s.sides)
	{
		m_w.dict(side.properties);
		o.i32(static_cast<std::int32_t>(side.buildList.size()));
		for (const BuildListEntry &e : side.buildList)
		{
			m_w.ascii(e.buildingName);
			m_w.ascii(e.templateName);
			o.f32(e.x);
			o.f32(e.y);
			o.f32(e.z);
			o.f32(e.angle);
			o.boolean(e.initiallyBuilt);
			o.i32(e.numRebuilds);
			m_w.ascii(e.script);
			o.i32(e.health);
			o.boolean(e.whiner);
			o.boolean(e.unsellable);
			o.boolean(e.repairable);
		}
	}
	o.i32(static_cast<std::int32_t>(s.teams.size()));
	for (const PropertyMap &team : s.teams)
		m_w.dict(team);
	if (s.hasScripts)
	{
		std::vector<const ScriptList *> lists;
		for (const Side &side : s.sides)
			lists.push_back(&side.scripts);
		scriptLists(lists);
	}
	m_w.end();
}

void Writer::scriptLists(const std::vector<const ScriptList *> &lists)
{
	ByteWriter &o = m_w.out();
	m_w.begin(CHUNK_PLAYER_SCRIPTS, Version::PLAYER_SCRIPTS);
	for (const ScriptList *list : lists)
	{
		m_w.begin(CHUNK_SCRIPT_LIST, Version::SCRIPT_LIST);
		for (const Script &sc : list->scripts)
			script(sc);
		for (const ScriptGroup &g : list->groups)
		{
			m_w.begin(CHUNK_SCRIPT_GROUP, Version::SCRIPT_GROUP);
			m_w.ascii(g.name);
			o.boolean(g.isActive);
			o.boolean(g.isSubroutine);
			for (const Script &sc : g.scripts)
				script(sc);
			m_w.end();
		}
		m_w.end();
	}
	m_w.end();
}

void Writer::polygonsV3(const std::vector<PolygonArea> &list)
{
	ByteWriter &o = m_w.out();
	m_w.begin(CHUNK_TRIGGERS, 3);
	o.i32(static_cast<std::int32_t>(list.size()));
	for (const PolygonArea &p : list)
	{
		m_w.ascii(p.name);
		o.i32(p.id);
		o.boolean(p.isWater);
		o.boolean(p.isRiver);
		o.i32(p.riverStart);
		o.i32(static_cast<std::int32_t>(p.points.size()));
		for (const auto &pt : p.points)
		{
			o.i32(pt[0]);
			o.i32(pt[1]);
			o.i32(pt[2]);
		}
	}
	m_w.end();
}

void Writer::objects(const std::vector<MapObject> &list)
{
	ByteWriter &o = m_w.out();
	m_w.begin(CHUNK_OBJECTS, Version::OBJECTS);
	for (const MapObject &obj : list)
	{
		m_w.begin(CHUNK_OBJECT, Version::OBJECTS);
		o.f32(obj.x);
		o.f32(obj.y);
		o.f32(obj.z);
		o.f32(obj.angle);
		o.i32(obj.flags);
		m_w.ascii(obj.templateName);
		m_w.dict(obj.properties);
		m_w.end();
	}
	m_w.end();
}

void Writer::polygons(const std::vector<PolygonArea> &list)
{
	ByteWriter &o = m_w.out();
	m_w.begin(CHUNK_TRIGGERS, Version::TRIGGERS);
	o.i32(static_cast<std::int32_t>(list.size()));
	for (const PolygonArea &p : list)
	{
		m_w.ascii(p.name);
		m_w.ascii(p.layer);
		o.i32(p.id);
		o.boolean(p.isWater);
		o.boolean(p.isRiver);
		o.i32(p.riverStart);
		o.i32(static_cast<std::int32_t>(p.points.size()));
		for (const auto &pt : p.points)
			for (std::int32_t v : pt)
				o.i32(v);
	}
	m_w.end();
}

void Writer::lighting(const GlobalLighting &gl)
{
	ByteWriter &o = m_w.out();
	auto light = [&](const Light &l) {
		for (float v : l.ambient)
			o.f32(v);
		for (float v : l.diffuse)
			o.f32(v);
		for (float v : l.position)
			o.f32(v);
	};
	m_w.begin(CHUNK_LIGHTING, Version::LIGHTING);
	o.i32(gl.timeOfDay);
	for (const TimeOfDayLighting &t : gl.times)
	{
		light(t.terrain[0]);
		light(t.objects[0]);
		light(t.objects[1]);
		light(t.objects[2]);
		light(t.terrain[1]);
		light(t.terrain[2]);
	}
	if (gl.shadowColor)
		o.u32(*gl.shadowColor);
	m_w.end();
}

void Writer::waypoints(const std::vector<WaypointLink> &links)
{
	ByteWriter &o = m_w.out();
	m_w.begin(CHUNK_WAYPOINTS, Version::WAYPOINTS);
	o.i32(static_cast<std::int32_t>(links.size()));
	for (const WaypointLink &l : links)
	{
		o.i32(l.from);
		o.i32(l.to);
	}
	m_w.end();
}

} // namespace

// ================================================================================================

const std::vector<std::string> &knownChunkNames()
{
	static const std::vector<std::string> names = {
		CHUNK_HEIGHT_MAP, CHUNK_BLEND_TILES, CHUNK_WORLD_INFO, CHUNK_SIDES,
		CHUNK_OBJECTS, CHUNK_TRIGGERS, CHUNK_LIGHTING, CHUNK_WAYPOINTS};
	return names;
}

std::size_t cliffRowBytes(std::int32_t width)
{
	return static_cast<std::size_t>((std::max(width, 0) + 7) / 8);
}

bool cliffFlag(const std::vector<std::uint8_t> &flags, std::int32_t width, std::int32_t x, std::int32_t y)
{
	const std::size_t i = static_cast<std::size_t>(y) * cliffRowBytes(width) + static_cast<std::size_t>(x >> 3);
	return x >= 0 && y >= 0 && x < width && i < flags.size() && (flags[i] & (1 << (x & 7))) != 0;
}

void setCliffFlag(std::vector<std::uint8_t> &flags, std::int32_t width, std::int32_t x, std::int32_t y, bool cliff)
{
	const std::size_t i = static_cast<std::size_t>(y) * cliffRowBytes(width) + static_cast<std::size_t>(x >> 3);
	if (x < 0 || y < 0 || x >= width || i >= flags.size())
		return;
	const std::uint8_t bit = static_cast<std::uint8_t>(1 << (x & 7));
	flags[i] = static_cast<std::uint8_t>(cliff ? (flags[i] | bit) : (flags[i] & ~bit));
}

void updateCliffFlags(const HeightMap &hm, std::vector<std::uint8_t> &flags, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1)
{
	if (hm.width < 2 || hm.height < 2 || hm.heights.size() < static_cast<std::size_t>(hm.width) * static_cast<std::size_t>(hm.height))
		return;
	auto height = [&](std::int32_t x, std::int32_t y) {
		return static_cast<float>(hm.heights[static_cast<std::size_t>(y) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(x)]) * HEIGHT_SCALE;
	};
	// The last row and column have no cell beyond them and keep their flags.
	x0 = std::max(x0, 0);
	y0 = std::max(y0, 0);
	x1 = std::min(x1, hm.width - 2);
	y1 = std::min(y1, hm.height - 2);
	for (std::int32_t y = y0; y <= y1; ++y)
	{
		for (std::int32_t x = x0; x <= x1; ++x)
		{
			const float h1 = height(x, y);
			const float h2 = height(x + 1, y);
			const float h3 = height(x, y + 1);
			const float h4 = height(x + 1, y + 1);
			const float lo = std::min(std::min(h1, h2), std::min(h3, h4));
			const float hi = std::max(std::max(h1, h2), std::max(h3, h4));
			setCliffFlag(flags, hm.width, x, y, hi - lo > CLIFF_RANGE);
		}
	}
}

ReadResult readMap(std::span<const std::uint8_t> data)
{
	ReadResult result;
	Map &map = result.map;
	ByteReader header(data);
	if (!map.names.read(header))
	{
		result.error = header.ok() ? "not a chunk file (no CkMp tag); pre-chunk maps are not supported" : "name table: " + header.error();
		return result;
	}
	Reader reader(map.names, result.warnings);
	std::vector<ChunkView> top;
	if (!reader.children(header.rest(), "file", top))
	{
		result.error = reader.error();
		return result;
	}
	for (const ChunkView &c : top)
	{
		const auto &known = knownChunkNames();
		const bool isKnown = std::find(known.begin(), known.end(), c.name) != known.end();
		auto duplicate = [&](bool present) {
			if (present)
				reader.warn("second " + c.name + " chunk kept as data");
			return present;
		};
		if (!isKnown
			|| (c.name == CHUNK_HEIGHT_MAP && duplicate(map.heightMap.has_value()))
			|| (c.name == CHUNK_BLEND_TILES && duplicate(map.blendTiles.has_value()))
			|| (c.name == CHUNK_WORLD_INFO && duplicate(map.worldInfo.has_value()))
			|| (c.name == CHUNK_SIDES && duplicate(map.sides.has_value()))
			|| (c.name == CHUNK_OBJECTS && duplicate(map.objects.has_value()))
			|| (c.name == CHUNK_TRIGGERS && duplicate(map.polygons.has_value()))
			|| (c.name == CHUNK_LIGHTING && duplicate(map.lighting.has_value()))
			|| (c.name == CHUNK_WAYPOINTS && duplicate(map.waypointLinks.has_value())))
		{
			map.otherChunks.push_back(StoredChunk{c.name, c.version, std::vector<std::uint8_t>(c.data.begin(), c.data.end())});
			map.chunkOrder.push_back(c.name);
			continue;
		}
		reader.setTop(c.name);
		if (c.name == CHUNK_HEIGHT_MAP)
			map.heightMap = reader.heightMap(c);
		else if (c.name == CHUNK_BLEND_TILES)
			map.blendTiles = reader.blendTiles(c, map.heightMap ? &*map.heightMap : nullptr);
		else if (c.name == CHUNK_WORLD_INFO)
		{
			reader.version(CHUNK_WORLD_INFO, c.version, Version::WORLD_INFO);
			ByteReader r(c.data);
			PropertyMap dict = readDict(r, map.names);
			if (reader.check(r, CHUNK_WORLD_INFO))
			{
				reader.expectEnd(r, CHUNK_WORLD_INFO);
				map.worldInfo = std::move(dict);
			}
		}
		else if (c.name == CHUNK_SIDES)
			map.sides = reader.sides(c);
		else if (c.name == CHUNK_OBJECTS)
			map.objects = reader.objects(c);
		else if (c.name == CHUNK_TRIGGERS)
			map.polygons = reader.polygons(c);
		else if (c.name == CHUNK_LIGHTING)
			map.lighting = reader.lighting(c);
		else if (c.name == CHUNK_WAYPOINTS)
			map.waypointLinks = reader.waypoints(c);
		if (reader.failed())
		{
			result.error = reader.error();
			return result;
		}
		map.chunkOrder.push_back(c.name);
	}
	reader.report(result);
	return result;
}

bool sameContent(const Map &a, const Map &b)
{
	return a.heightMap == b.heightMap && a.blendTiles == b.blendTiles && a.worldInfo == b.worldInfo && a.sides == b.sides
		&& a.objects == b.objects && a.polygons == b.polygons && a.lighting == b.lighting && a.waypointLinks == b.waypointLinks
		&& a.otherChunks == b.otherChunks;
}

WriteResult writeMap(const Map &map)
{
	WriteResult result;
	NameTable names = map.names;
	ByteWriter body;
	Writer writer(body, names);

	// File order first, then whatever the file did not have, in the old WorldBuilder's order.
	std::vector<std::string> order = map.chunkOrder;
	auto present = [&](const std::string &name) {
		if (name == CHUNK_HEIGHT_MAP) return map.heightMap.has_value();
		if (name == CHUNK_BLEND_TILES) return map.blendTiles.has_value();
		if (name == CHUNK_WORLD_INFO) return map.worldInfo.has_value();
		if (name == CHUNK_SIDES) return map.sides.has_value();
		if (name == CHUNK_OBJECTS) return map.objects.has_value();
		if (name == CHUNK_TRIGGERS) return map.polygons.has_value();
		if (name == CHUNK_LIGHTING) return map.lighting.has_value();
		if (name == CHUNK_WAYPOINTS) return map.waypointLinks.has_value();
		return false;
	};
	for (const std::string &name : knownChunkNames())
		if (present(name) && std::find(order.begin(), order.end(), name) == order.end())
			order.push_back(name);

	std::map<std::string, std::size_t> storedUsed;
	std::map<std::string, bool> knownWritten;
	auto nextStored = [&](const std::string &name) -> const StoredChunk * {
		std::size_t &skip = storedUsed[name];
		std::size_t seen = 0;
		for (const StoredChunk &c : map.otherChunks)
			if (c.name == name && seen++ == skip)
			{
				++skip;
				return &c;
			}
		return nullptr;
	};
	for (const std::string &name : order)
	{
		const bool known = present(name) && !knownWritten[name];
		if (!known)
		{
			if (const StoredChunk *c = nextStored(name))
				writer.w().stored(*c);
			continue;
		}
		knownWritten[name] = true;
		const HeightMap *hm = map.heightMap ? &*map.heightMap : nullptr;
		if (name == CHUNK_HEIGHT_MAP) writer.heightMap(*map.heightMap);
		else if (name == CHUNK_BLEND_TILES) writer.blendTiles(*map.blendTiles, hm);
		else if (name == CHUNK_WORLD_INFO) writer.worldInfo(*map.worldInfo);
		else if (name == CHUNK_SIDES) writer.sides(*map.sides);
		else if (name == CHUNK_OBJECTS) writer.objects(*map.objects);
		else if (name == CHUNK_TRIGGERS) writer.polygons(*map.polygons);
		else if (name == CHUNK_LIGHTING) writer.lighting(*map.lighting);
		else if (name == CHUNK_WAYPOINTS) writer.waypoints(*map.waypointLinks);
	}
	// Stored chunks the order list does not name (added by code, not read from a file).
	for (const StoredChunk &c : map.otherChunks)
		if (std::count(order.begin(), order.end(), c.name) == 0)
			writer.w().stored(c);

	if (!writer.error.empty())
		result.error = writer.error;
	else if (!writer.w().error().empty())
		result.error = writer.w().error();
	if (!result.ok())
		return result;

	ByteWriter file;
	names.write(file);
	file.bytes(body.data());
	result.bytes = file.take();
	return result;
}

namespace
{

const char *const CHUNK_SCRIPT_PLAYERS = "ScriptsPlayers";
const char *const CHUNK_SCRIPT_TEAMS = "ScriptTeams";
constexpr std::uint16_t SCRIPT_PLAYERS_VERSION = 2;
constexpr std::uint16_t SCRIPT_TEAMS_VERSION = 1;

} // namespace

ScriptFileResult readScriptFile(std::span<const std::uint8_t> data)
{
	ScriptFileResult result;
	ScriptFile &file = result.file;
	NameTable names;
	ByteReader header(data);
	if (!names.read(header))
	{
		result.error = header.ok() ? "not a chunk file (no CkMp tag)" : "name table: " + header.error();
		return result;
	}
	Reader reader(names, result.warnings);
	std::vector<ChunkView> top;
	if (!reader.children(header.rest(), "file", top))
	{
		result.error = reader.error();
		return result;
	}
	for (const ChunkView &c : top)
	{
		reader.setTop(c.name);
		if (c.name == CHUNK_PLAYER_SCRIPTS)
			reader.scriptLists(c, file.lists);
		else if (c.name == CHUNK_OBJECTS)
		{
			if (auto objects = reader.objects(c))
				file.objects = std::move(*objects);
		}
		else if (c.name == CHUNK_TRIGGERS)
		{
			if (auto polygons = reader.polygons(c))
				file.polygons = std::move(*polygons);
		}
		else if (c.name == CHUNK_WAYPOINTS)
		{
			if (auto links = reader.waypoints(c))
				file.waypointLinks = std::move(*links);
		}
		else if (c.name == CHUNK_SCRIPT_TEAMS)
		{
			ByteReader r(c.data);
			while (r.ok() && !r.atEnd())
				file.teams.push_back(readDict(r, names));
			reader.check(r, CHUNK_SCRIPT_TEAMS);
		}
		else if (c.name == CHUNK_SCRIPT_PLAYERS)
		{
			ByteReader r(c.data);
			const bool withDicts = c.version >= 2 && r.i32() != 0;
			const std::size_t count = r.count(r.i32(), 2, "players");
			for (std::size_t i = 0; i < count && r.ok(); ++i)
			{
				file.playerNames.push_back(readAscii(r));
				if (withDicts)
					file.players.push_back(readDict(r, names));
			}
			if (reader.check(r, CHUNK_SCRIPT_PLAYERS))
				reader.expectEnd(r, CHUNK_SCRIPT_PLAYERS);
		}
		else
			reader.warn("ignored chunk " + c.name);
		if (reader.failed())
		{
			result.error = reader.error();
			return result;
		}
	}
	return result;
}

WriteResult writeScriptFile(const ScriptFile &file)
{
	WriteResult result;
	NameTable names;
	ByteWriter body;
	Writer writer(body, names);
	ChunkWriter &w = writer.w();
	ByteWriter &o = w.out();
	std::vector<const ScriptList *> lists;
	for (const ScriptList &l : file.lists)
		lists.push_back(&l);
	writer.scriptLists(lists);
	const bool withDicts = !file.players.empty() && file.players.size() == file.playerNames.size();
	w.begin(CHUNK_SCRIPT_PLAYERS, SCRIPT_PLAYERS_VERSION);
	o.i32(withDicts ? 1 : 0);
	o.i32(static_cast<std::int32_t>(file.playerNames.size()));
	for (std::size_t i = 0; i < file.playerNames.size(); ++i)
	{
		w.ascii(file.playerNames[i]);
		if (withDicts)
			w.dict(file.players[i]);
	}
	w.end();
	writer.objects(file.objects);
	writer.polygonsV3(file.polygons);
	w.begin(CHUNK_SCRIPT_TEAMS, SCRIPT_TEAMS_VERSION);
	for (const PropertyMap &team : file.teams)
		w.dict(team);
	w.end();
	writer.waypoints(file.waypointLinks);
	if (!writer.error.empty())
		result.error = writer.error;
	else if (!w.error().empty())
		result.error = w.error();
	if (!result.ok())
		return result;
	ByteWriter out;
	names.write(out);
	out.bytes(body.data());
	result.bytes = out.take();
	return result;
}

} // namespace MapModel
