/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapEditorTests.cpp -- tests for the map model (Code/Tools/Editors/MapEditor/Model) and the editor core
// (Code/Tools/Editors/MapEditor/Core) of the new map editor.
// No framework, like Code/Tests/EngineTests. Needs no game data: the maps are built in code, and
// one reference file is assembled byte by byte the way the game's writer lays it out.

#include "Core/History.h"
#include "Core/AreaCommands.h"
#include "Core/BuildListCommands.h"
#include "Core/CliffTexture.h"
#include "Core/ObjectCommands.h"
#include "Core/ParticleIni.h"
#include "Core/RoadCommands.h"
#include "Core/TerrainCommands.h"
#include "Core/Contours.h"
#include "Core/GameLaunch.h"
#include "Core/GraphicsLevels.h"
#include "Core/HeightImage.h"
#include "Core/HeightShapes.h"
#include "Core/LayerCommands.h"
#include "Core/MapCheck.h"
#include "Core/MapCompare.h"
#include "Core/MapPreview.h"
#include "Core/MapSettingsCommands.h"
#include "Core/MapSize.h"
#include "Core/PlacementCommands.h"
#include "Core/ScriptCommands.h"
#include "Core/ScriptExchange.h"
#include "Core/SelectionTools.h"
#include "Core/SidesCommands.h"
#include "Core/Fairness.h"
#include "Core/References.h"
#include "Core/Symmetry.h"
#include "Core/Slope.h"
#include "Core/TextureBlend.h"
#include "Core/TextureCommands.h"
#include "Core/WaterLook.h"
#include "Core/WaypointCommands.h"
#include "Model/ByteIO.h"
#include "Model/Chunk.h"
#include "Model/MapDump.h"
#include "Model/MapFile.h"
#include "Model/MapIO.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace MapModel;

static int s_failures = 0;
static int s_checks = 0;

static void check(bool condition, const char *what, int line)
{
	++s_checks;
	if (!condition)
	{
		++s_failures;
		std::printf("  FAILED (line %d): %s\n", line, what);
	}
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// ------------------------------------------------------------------ helpers

static void putU32(std::vector<std::uint8_t> &b, std::uint32_t v)
{
	for (int i = 0; i < 4; ++i)
		b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

static void putU16(std::vector<std::uint8_t> &b, std::uint16_t v)
{
	b.push_back(static_cast<std::uint8_t>(v));
	b.push_back(static_cast<std::uint8_t>(v >> 8));
}

static void putText(std::vector<std::uint8_t> &b, const char *text)
{
	b.insert(b.end(), text, text + std::strlen(text));
}

static void putName(std::vector<std::uint8_t> &b, const char *name, std::uint32_t id)
{
	b.push_back(static_cast<std::uint8_t>(std::strlen(name)));
	putText(b, name);
	putU32(b, id);
}

static void putChunk(std::vector<std::uint8_t> &b, std::uint32_t id, std::uint16_t version, const std::vector<std::uint8_t> &data)
{
	putU32(b, id);
	putU16(b, version);
	putU32(b, static_cast<std::uint32_t>(data.size()));
	b.insert(b.end(), data.begin(), data.end());
}

static ScriptParameter textParam(std::int32_t type, const char *text)
{
	ScriptParameter p;
	p.type = type;
	p.text = text;
	return p;
}

// A map that uses every chunk and every field kind.
static Map makeTestMap()
{
	Map map;
	HeightMap hm;
	hm.width = 20;
	hm.height = 12;
	hm.borderSize = 2;
	hm.boundaries = {{16, 8}, {10, 6}};
	for (int i = 0; i < hm.width * hm.height; ++i)
		hm.heights.push_back(static_cast<std::uint8_t>(i * 7));
	map.heightMap = hm;

	BlendTiles bt;
	const std::size_t cells = hm.heights.size();
	for (std::size_t i = 0; i < cells; ++i)
	{
		bt.tiles.push_back(static_cast<std::int16_t>(i % 16));
		bt.blends.push_back(static_cast<std::int16_t>(i % 3));
		bt.extraBlends.push_back(0);
		bt.cliffInfoIndices.push_back(static_cast<std::int16_t>(i == 5 ? 1 : 0));
	}
	bt.cliffState.assign(static_cast<std::size_t>((hm.width + 7) / 8 * hm.height), 0);
	bt.cliffState[3] = 0x81;
	bt.numBitmapTiles = 16;
	bt.textureClasses = {{0, 8, 2, 0, "TEDesert1"}, {8, 8, 2, 0, "TEGrass2"}};
	bt.numEdgeTiles = 4;
	bt.edgeTextureClasses = {{0, 4, 2, "TERock"}};
	BlendedTile tile;
	tile.blendIndex = 9;
	tile.horizontal = 1;
	tile.longDiagonal = 1;
	tile.customBlendEdgeClass = 0;
	bt.blendedTiles = {tile, BlendedTile{}};
	CliffInfo ci;
	ci.tileIndex = 3;
	ci.uv = {0.0f, 0.25f, 0.5f, 0.25f, 0.5f, 0.75f, 0.0f, 0.75f};
	ci.flip = 1;
	bt.cliffInfos = {ci};
	map.blendTiles = bt;

	PropertyMap world;
	world.set("mapName", std::string("Test map"));
	world.set("weather", std::int32_t(0));
	world.set("compression", std::int32_t(1));
	map.worldInfo = world;

	Sides sides;
	for (int i = 0; i < 2; ++i)
	{
		Side side;
		side.properties.set("playerName", std::string(i == 0 ? "" : "Player2"));
		side.properties.set("playerIsHuman", i == 0);
		side.properties.set("playerDisplayName", std::u16string(u"Caf\u00E9 \u6F22"));
		if (i == 1)
		{
			BuildListEntry e;
			e.buildingName = "Base";
			e.templateName = "ChinaCommandCenter";
			e.x = 100.0f;
			e.y = 220.5f;
			e.angle = 1.5f;
			e.initiallyBuilt = true;
			e.numRebuilds = 2;
			e.script = "Build";
			e.health = 80;
			side.buildList.push_back(e);

			Script s;
			s.name = "Wave 1";
			s.comment = "first wave";
			s.isOneShot = false;
			s.delayEvaluationSeconds = 2;
			ScriptCall cond;
			cond.type = 12;
			cond.internalName = "TEAM_DESTROYED";
			cond.parameters.push_back(textParam(3, "Attack North"));
			ScriptCall timer;
			timer.type = 3;
			timer.internalName = "TIMER_EXPIRED";
			ScriptParameter seconds;
			seconds.type = 1;
			seconds.realValue = 600.0f;
			timer.parameters.push_back(seconds);
			s.conditions = {OrCondition{{cond}}, OrCondition{{timer}}};
			ScriptCall move;
			move.type = 40;
			move.internalName = "TEAM_FOLLOW_WAYPOINTS";
			move.parameters.push_back(textParam(3, "Attack North"));
			move.parameters.push_back(textParam(24, "Path North 1"));
			ScriptParameter pos;
			pos.type = PARAMETER_COORD3D;
			pos.coord = {10.0f, 20.0f, 0.5f};
			move.parameters.push_back(pos);
			s.actions = {move};
			ScriptCall noop;
			noop.type = 0;
			noop.internalName = "NO_OP";
			s.falseActions = {noop};
			side.scripts.scripts.push_back(s);

			ScriptGroup g;
			g.name = "Waves";
			g.isSubroutine = true;
			Script inner = s;
			inner.name = "Wave 2";
			g.scripts.push_back(inner);
			side.scripts.groups.push_back(g);
		}
		sides.sides.push_back(side);
	}
	PropertyMap team;
	team.set("teamName", std::string("Attack North"));
	team.set("teamOwner", std::string("Player2"));
	team.set("teamMaxInstances", std::int32_t(3));
	sides.teams.push_back(team);
	map.sides = sides;

	std::vector<MapObject> objects;
	MapObject tank;
	tank.x = 50.0f;
	tank.y = 60.0f;
	tank.angle = 0.25f;
	tank.flags = 0x100;
	tank.templateName = "AmericaTankCrusader";
	tank.properties.set("objectInitialHealth", std::int32_t(100));
	tank.properties.set("objectName", std::string("Tank1"));
	tank.properties.set("objectScale", 1.25f);
	objects.push_back(tank);
	MapObject waypoint;
	waypoint.templateName = "*Waypoints/Waypoint";
	waypoint.properties.set("waypointID", std::int32_t(1));
	waypoint.properties.set("waypointName", std::string("Player_1_Start"));
	objects.push_back(waypoint);
	map.objects = objects;

	PolygonArea area;
	area.name = "Base";
	area.layer = "Layer 1";
	area.id = 1;
	area.points = {{0, 0, 0}, {100, 0, 0}, {100, 100, 5}};
	PolygonArea river = area;
	river.name = "River";
	river.id = 2;
	river.isWater = true;
	river.isRiver = true;
	river.riverStart = 1;
	map.polygons = std::vector<PolygonArea>{area, river};

	GlobalLighting gl;
	gl.timeOfDay = 2;
	for (std::size_t t = 0; t < 4; ++t)
		for (std::size_t l = 0; l < 3; ++l)
		{
			gl.times[t].terrain[l].ambient = {0.1f * static_cast<float>(t), 0.2f, 0.3f};
			gl.times[t].objects[l].diffuse = {0.5f, 0.6f, 0.7f * static_cast<float>(l)};
			gl.times[t].terrain[l].position = {-0.8f, 0.4f, -0.4f};
		}
	gl.shadowColor = 0x7F000000u;
	map.lighting = gl;
	map.waypointLinks = std::vector<WaypointLink>{{1, 2}, {2, 3}};

	map.otherChunks.push_back(StoredChunk{"GZHNeoEditor", 1, {1, 2, 3, 4}});
	return map;
}

// ------------------------------------------------------------------ tests

static void testByteReader()
{
	std::printf("ByteReader\n");
	const std::vector<std::uint8_t> data = {1, 2, 3, 4, 5};
	ByteReader r(data);
	CHECK(r.u32() == 0x04030201u);
	CHECK(r.ok());
	CHECK(r.u32() == 0);
	CHECK(!r.ok());
	CHECK(r.u8() == 0);
	CHECK(r.atEnd());

	ByteReader c(data);
	CHECK(c.count(1000, 4, "things") == 0);
	CHECK(!c.ok());
	ByteReader n(data);
	CHECK(n.count(-1, 1, "things") == 0);
	CHECK(!n.ok());
}

static void testNameTable()
{
	std::printf("NameTable\n");
	NameTable t;
	CHECK(t.allocate("A") == 1);
	CHECK(t.allocate("B") == 2);
	CHECK(t.allocate("A") == 1);
	// Newest first, like DataChunkTableOfContents.
	CHECK(t.entries().size() == 2 && t.entries()[0].name == "B" && t.entries()[1].name == "A");
	ByteWriter w;
	t.write(w);
	ByteReader r(w.data());
	NameTable back;
	CHECK(back.read(r));
	CHECK(back.entries().size() == 2 && back.entries()[0].name == "B");
	CHECK(back.allocate("C") == 3);
	CHECK(back.name(1) != nullptr && *back.name(1) == "A");

	// Repeated ids or names make ids ambiguous; the game's writer never produces them.
	std::vector<std::uint8_t> repeated;
	putText(repeated, "CkMp");
	putU32(repeated, 2);
	putName(repeated, "A", 1);
	putName(repeated, "B", 1);
	ByteReader rr(repeated);
	NameTable twice;
	CHECK(!twice.read(rr));
	CHECK(!readMap(repeated).ok());

	// Ids must fit the 24 bits of a dictionary key (third fuzzer find: max id + 1 wrapped around).
	for (std::uint32_t id : {0u, 0x1000000u, 0xFFFFFFFFu})
	{
		std::vector<std::uint8_t> big;
		putText(big, "CkMp");
		putU32(big, 1);
		putName(big, "A", id);
		ByteReader br(big);
		NameTable outOfRange;
		CHECK(!outOfRange.read(br));
	}

	const std::vector<std::uint8_t> wrongTag = {'X', 'k', 'M', 'p', 0, 0, 0, 0};
	ByteReader bad(wrongTag);
	NameTable none;
	CHECK(!none.read(bad));
}

static void testDict()
{
	std::printf("Dict\n");
	PropertyMap m;
	m.set("b", true);
	m.set("i", std::int32_t(-7));
	m.set("r", 2.5f);
	m.set("a", std::string("text"));
	m.set("u", std::u16string(u"\u00E4\u00F6\u00FC"));
	m.set("i", std::int32_t(8));	// replaced in place
	CHECK(m.size() == 5 && m.pairs()[1].key == "i" && m.getInt("i") == 8);

	NameTable names;
	ByteWriter w;
	ChunkWriter cw(w, names);
	cw.dict(m);
	ByteReader r(w.data());
	const PropertyMap back = readDict(r, names);
	CHECK(r.ok() && r.atEnd());
	CHECK(back == m);
	CHECK(toUtf8(u"\u00E4") == "\xC3\xA4");
	CHECK(m.erase("r") && !m.erase("r") && m.size() == 4);
}

// A file assembled by hand in the game's layout: names in first-use order, newest first in the
// table, sizes patched after the data.
static void testReferenceBytes()
{
	std::printf("Reference bytes\n");
	std::vector<std::uint8_t> expected;
	putText(expected, "CkMp");
	putU32(expected, 3);
	putName(expected, "WaypointsList", 3);
	putName(expected, "mapName", 2);
	putName(expected, "WorldInfo", 1);
	std::vector<std::uint8_t> world;
	putU16(world, 1);
	putU32(world, (2u << 8) | 3u);
	putU16(world, 4);
	putText(world, "Test");
	putChunk(expected, 1, 1, world);
	std::vector<std::uint8_t> links;
	putU32(links, 1);
	putU32(links, 1);
	putU32(links, 2);
	putChunk(expected, 3, 1, links);

	Map map;
	PropertyMap info;
	info.set("mapName", std::string("Test"));
	map.worldInfo = info;
	map.waypointLinks = std::vector<WaypointLink>{{1, 2}};
	const WriteResult written = writeMap(map);
	CHECK(written.ok());
	CHECK(written.bytes == expected);

	const ReadResult read = readMap(expected);
	CHECK(read.ok());
	CHECK(read.map.worldInfo && read.map.worldInfo->getAscii("mapName") == "Test");
	CHECK(read.map.waypointLinks && read.map.waypointLinks->size() == 1 && (*read.map.waypointLinks)[0].to == 2);
}

static void testRoundTrip()
{
	std::printf("Round trip\n");
	const Map map = makeTestMap();
	const WriteResult first = writeMap(map);
	CHECK(first.ok());
	const ReadResult read = readMap(first.bytes);
	CHECK(read.ok());
	CHECK(read.warnings.empty());
	CHECK(sameContent(map, read.map));
	const WriteResult second = writeMap(read.map);
	CHECK(second.ok() && second.bytes == first.bytes);

	// PlayerScriptsList carries version 5 like the game's writer (not 1, despite the constant's name).
	{
		const auto id = read.map.names.find("PlayerScriptsList");
		CHECK(id.has_value());
		const std::uint8_t pattern[6] = {static_cast<std::uint8_t>(*id), static_cast<std::uint8_t>(*id >> 8),
			static_cast<std::uint8_t>(*id >> 16), static_cast<std::uint8_t>(*id >> 24), 5, 0};
		CHECK(std::search(first.bytes.begin(), first.bytes.end(), pattern, pattern + 6) != first.bytes.end());
	}

	// The model's order is the old WorldBuilder's; the editor chunk comes last.
	const std::vector<std::string> expectedOrder = {"HeightMapData", "BlendTileData", "WorldInfo", "SidesList",
		"ObjectsList", "PolygonTriggers", "GlobalLighting", "WaypointsList", "GZHNeoEditor"};
	CHECK(read.map.chunkOrder == expectedOrder);

	const std::string summary = dumpMap(read.map, DumpDetail::Summary);
	CHECK(summary.find("height map: 20 x 12 cells") != std::string::npos);
	CHECK(summary.find("objects: 2") != std::string::npos);
	const std::string full = dumpMap(read.map, DumpDetail::Full);
	CHECK(full.find("TEAM_FOLLOW_WAYPOINTS") != std::string::npos);
	CHECK(full.find("Caf\xC3\xA9") != std::string::npos);
}

static void testEditAndKeepIds()
{
	std::printf("Edit and keep ids\n");
	const WriteResult original = writeMap(makeTestMap());
	ReadResult read = readMap(original.bytes);
	CHECK(read.ok());
	const auto idBefore = read.map.names.find("objectName");

	// Edit: move an object, add a property never used before, add a polygon point.
	(*read.map.objects)[0].x = 75.0f;
	(*read.map.objects)[0].properties.set("objectEnabled", false);
	(*read.map.polygons)[0].points.push_back({0, 100, 0});
	const WriteResult edited = writeMap(read.map);
	CHECK(edited.ok());
	const ReadResult again = readMap(edited.bytes);
	CHECK(again.ok());
	CHECK((*again.map.objects)[0].x == 75.0f);
	CHECK((*again.map.objects)[0].properties.getBool("objectEnabled") == false);
	CHECK((*again.map.polygons)[0].points.size() == 4);
	CHECK(again.map.names.find("objectName") == idBefore);
	CHECK(again.map.names.entries().front().name == "objectEnabled");
}

// Older chunk versions: the fields they lack get the game's defaults and are written in the
// newest version.
static void testOldVersions()
{
	std::printf("Old versions\n");
	std::vector<std::uint8_t> file;
	putText(file, "CkMp");
	putU32(file, 3);
	putName(file, "GlobalLighting", 3);
	putName(file, "PolygonTriggers", 2);
	putName(file, "HeightMapData", 1);

	std::vector<std::uint8_t> hm;		// version 3: border, no boundary list
	putU32(hm, 6);
	putU32(hm, 4);
	putU32(hm, 1);
	putU32(hm, 24);
	for (int i = 0; i < 24; ++i)
		hm.push_back(static_cast<std::uint8_t>(i));
	putChunk(file, 1, 3, hm);

	std::vector<std::uint8_t> poly;		// version 1: no layer, no water, no river
	putU32(poly, 1);
	putU16(poly, 4);
	putText(poly, "Zone");
	putU32(poly, 7);
	putU32(poly, 2);
	for (std::uint32_t v : {1u, 2u, 3u, 4u, 5u, 6u})
		putU32(poly, v);
	putChunk(file, 2, 1, poly);

	std::vector<std::uint8_t> light;		// version 1: one terrain and one object light, no shadow color
	putU32(light, 1);
	for (int i = 0; i < 4 * 2 * 9; ++i)
		putU32(light, 0x3F800000u);	// 1.0f
	putChunk(file, 3, 1, light);

	const ReadResult read = readMap(file);
	CHECK(read.ok());
	CHECK(read.map.heightMap && read.map.heightMap->boundaries.size() == 1);
	CHECK(read.map.heightMap->boundaries[0] == (Point2{4, 2}));
	CHECK(read.map.polygons && (*read.map.polygons)[0].name == "Zone" && !(*read.map.polygons)[0].isWater);
	CHECK((*read.map.polygons)[0].points.size() == 2 && (*read.map.polygons)[0].points[1][2] == 6);
	CHECK(read.map.lighting && !read.map.lighting->shadowColor);
	CHECK(read.map.lighting->times[3].objects[0].position[2] == 1.0f);
	CHECK(read.map.lighting->times[3].objects[2].position[2] == -1.0f);

	const WriteResult written = writeMap(read.map);
	CHECK(written.ok());
	const ReadResult again = readMap(written.bytes);
	CHECK(again.ok() && again.map.heightMap == read.map.heightMap && again.map.polygons == read.map.polygons);
	CHECK(again.map.lighting == read.map.lighting);
}

// Blend tile version 7 stored the cliff flags with a row length of (width + 1) / 8 bytes.
static void testCliffStateVersion7()
{
	std::printf("Cliff flags, version 7\n");
	Map map = makeTestMap();			// width 20: correct rows 3 bytes, version 7 rows 2 bytes
	const WriteResult written = writeMap(map);
	ReadResult read = readMap(written.bytes);
	CHECK(read.ok());

	// Rebuild the BlendTileData chunk as version 7.
	BlendTiles &bt = *read.map.blendTiles;
	NameTable names;
	ByteWriter w;
	ChunkWriter cw(w, names);
	cw.begin("HeightMapData", 4);
	cw.out().i32(20);
	cw.out().i32(12);
	cw.out().i32(2);
	cw.out().i32(0);
	cw.out().i32(240);
	cw.out().bytes(map.heightMap->heights);
	cw.end();
	cw.begin("BlendTileData", 7);
	cw.out().i32(240);
	for (const auto *a : {&bt.tiles, &bt.blends, &bt.extraBlends, &bt.cliffInfoIndices})
		for (std::int16_t v : *a)
			cw.out().u16(static_cast<std::uint16_t>(v));
	for (int y = 0; y < 12; ++y)
	{
		cw.out().u8(static_cast<std::uint8_t>(y + 1));
		cw.out().u8(0xF0);
	}
	cw.out().i32(bt.numBitmapTiles);
	cw.out().i32(1);
	cw.out().i32(1);
	cw.out().i32(0);
	cw.out().i32(0);
	cw.out().i32(0);
	cw.end();
	ByteWriter file;
	names.write(file);
	file.bytes(w.data());
	const ReadResult old = readMap(file.data());
	CHECK(old.ok());
	CHECK(old.map.blendTiles && old.map.blendTiles->cliffState.size() == 36);
	CHECK(old.map.blendTiles->cliffState[0] == 1 && old.map.blendTiles->cliffState[1] == 0xF0 && old.map.blendTiles->cliffState[2] == 0);
	CHECK(old.map.blendTiles->cliffState[33] == 12 && old.map.blendTiles->cliffState[34] == 0xF0);
}

// Damaged files must fail cleanly: every truncation and a series of byte changes.
static void testDamagedFiles()
{
	std::printf("Damaged files\n");
	const WriteResult written = writeMap(makeTestMap());
	const std::vector<std::uint8_t> &good = written.bytes;

	// Offsets where a top-level chunk starts or the file ends: a cut there leaves a valid, shorter file.
	std::vector<std::size_t> boundaries;
	{
		ByteReader r(good);
		NameTable names;
		CHECK(names.read(r));
		std::size_t pos = r.position();
		while (pos + 10 <= good.size())
		{
			boundaries.push_back(pos);
			ByteReader size(std::span<const std::uint8_t>(good).subspan(pos + 6, 4));
			pos += 10 + size.u32();
		}
		boundaries.push_back(pos);
		CHECK(pos == good.size());
	}
	int wrongVerdicts = 0;
	for (std::size_t len = 0; len < good.size(); ++len)
	{
		const std::vector<std::uint8_t> cut(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(len));
		const bool atBoundary = std::find(boundaries.begin(), boundaries.end(), len) != boundaries.end();
		if (readMap(cut).ok() != atBoundary)
			++wrongVerdicts;
	}
	CHECK(wrongVerdicts == 0);

	// Extreme counts in the blend tile chunk (first fuzzer find: INT_MIN - 1 overflowed).
	{
		std::vector<std::uint8_t> bytes = good;
		ReadResult ok = readMap(bytes);
		CHECK(ok.ok());
		const std::size_t cells = ok.map.heightMap->heights.size();
		const std::size_t rowBytes = static_cast<std::size_t>((ok.map.heightMap->width + 7) / 8 * ok.map.heightMap->height);
		// Offset of numBlendedTiles: chunk header, cell count, four int16 arrays, cliff flags, numBitmapTiles.
		ByteReader r(good);
		NameTable names;
		CHECK(names.read(r));
		std::size_t pos = r.position();
		while (pos + 10 <= good.size())
		{
			ByteReader h(std::span<const std::uint8_t>(good).subspan(pos, 10));
			const std::uint32_t id = h.u32();
			h.u16();
			const std::uint32_t size = h.u32();
			if (names.name(id) != nullptr && *names.name(id) == "BlendTileData")
				break;
			pos += 10 + size;
		}
		const std::size_t field = pos + 10 + 4 + cells * 8 + rowBytes + 4;
		for (std::uint32_t v : {0x80000000u, 0xFFFFFFFFu, 0u, 0x7FFFFFFFu})
		{
			bytes = good;
			for (int i = 0; i < 4; ++i)
			{
				bytes[field + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
				bytes[field + 4 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
			}
			// Like the game's loops, zero and negative counts mean no entries; a huge count exceeds the data.
			const ReadResult extreme = readMap(bytes);
			CHECK(extreme.ok() == (v != 0x7FFFFFFFu));
		}
	}

	std::uint32_t seed = 12345;
	int survived = 0;
	for (int round = 0; round < 3000; ++round)
	{
		std::vector<std::uint8_t> bad = good;
		for (int k = 0; k < 4; ++k)
		{
			seed = seed * 1664525u + 1013904223u;
			bad[(seed >> 8) % bad.size()] = static_cast<std::uint8_t>(seed >> 24);
		}
		const ReadResult r = readMap(bad);
		if (r.ok())
		{
			++survived;
			(void)writeMap(r.map);
		}
	}
	CHECK(survived < 3000);	// most changes hit a size, id or count; none may crash (sanitizers)
}

static void testCompression()
{
	std::printf("Compression\n");
	Map map = makeTestMap();
	CHECK(compressionForSave(map) == 1);
	map.worldInfo->set("compression", std::int32_t(0));
	CHECK(compressionForSave(map) == 0);
	map.worldInfo->set("compression", std::int32_t(7));	// out of range: the game's default
	CHECK(compressionForSave(map) == static_cast<std::int32_t>(Compression::RefPack));
	map.worldInfo->erase("compression");
	CHECK(compressionForSave(map) == static_cast<std::int32_t>(Compression::RefPack));

	const WriteResult written = writeMap(map);
	std::vector<std::uint8_t> packed;
	std::string error;
	CHECK(pack(written.bytes, 1, packed, error));
	CHECK(packed.size() > 8 && std::memcmp(packed.data(), "EAR\0", 4) == 0);
	CHECK(packed.size() < written.bytes.size());
	const Unpacked unpacked = unpack(packed);
	CHECK(unpacked.ok() && unpacked.compression == 1 && unpacked.data == written.bytes);
	const Unpacked plain = unpack(written.bytes);
	CHECK(plain.ok() && plain.compression == 0 && plain.data == written.bytes);

	std::vector<std::uint8_t> broken = packed;
	broken.resize(broken.size() / 2);
	CHECK(!unpack(broken).ok());
}

static void testWriteErrors()
{
	std::printf("Write errors\n");
	Map map = makeTestMap();
	map.heightMap->heights.pop_back();
	CHECK(!writeMap(map).ok());

	Map noName = makeTestMap();
	noName.sides->sides[1].scripts.scripts[0].actions[0].internalName.clear();
	CHECK(!writeMap(noName).ok());
}

// Older versions are written in the newest one and content read back must match; the upgrade is
// reported so a round-trip check can tell it from a real difference.
static void testUpgrades()
{
	std::printf("Upgrades\n");
	// Height map with one steep cell, blend tiles v6 (no cliff flags), objects v2 with a height.
	HeightMap hm;
	hm.width = 10;
	hm.height = 3;
	hm.boundaries = {{10, 3}};
	hm.heights.assign(30, 10);
	hm.heights[1 * 10 + 4] = 40;			// (4,1): 30 * 0.625 = 18.75 > 9.8 for cells (3..4, 0..1)
	NameTable names;
	ByteWriter w;
	ChunkWriter cw(w, names);
	cw.begin("HeightMapData", 4);
	cw.out().i32(10);
	cw.out().i32(3);
	cw.out().i32(0);
	cw.out().i32(1);
	cw.out().i32(10);
	cw.out().i32(3);
	cw.out().i32(30);
	cw.out().bytes(hm.heights);
	cw.end();
	cw.begin("BlendTileData", 6);
	cw.out().i32(30);
	for (int array = 0; array < 4; ++array)			// tiles, blends, extra blends, cliff info indices
		for (int i = 0; i < 30; ++i)
			cw.out().u16(0);
	cw.out().i32(4);		// bitmap tiles
	cw.out().i32(1);		// blended tiles
	cw.out().i32(1);		// cliff infos
	cw.out().i32(0);		// texture classes
	cw.out().i32(0);		// edge tiles
	cw.out().i32(0);		// edge classes
	cw.end();
	cw.begin("ObjectsList", 2);
	cw.begin("Object", 2);
	cw.out().f32(15.0f);
	cw.out().f32(25.0f);
	cw.out().f32(33.0f);		// ignored by the game before version 3
	cw.out().f32(0.0f);
	cw.out().i32(0);
	cw.ascii("TreePine");
	cw.dict(PropertyMap{});
	cw.end();
	cw.end();
	ByteWriter file;
	names.write(file);
	file.bytes(w.data());

	const ReadResult read = readMap(file.data());
	CHECK(read.ok());
	CHECK(read.map.blendTiles && read.map.blendTiles->cliffState.size() == 6);
	// Row bytes 2; cliff cells x 3..4 in rows 0 and 1.
	CHECK(read.map.blendTiles->cliffState[0] == 0x18 && read.map.blendTiles->cliffState[2] == 0x18);
	CHECK(read.map.blendTiles->cliffState[4] == 0 && read.map.blendTiles->cliffState[1] == 0);
	CHECK(read.map.objects && (*read.map.objects)[0].z == 0.0f);
	CHECK(read.upgradedChunks == (std::vector<std::string>{"BlendTileData", "ObjectsList"}));
	CHECK(!read.upgrades.empty());

	const WriteResult written = writeMap(read.map);
	CHECK(written.ok() && written.bytes != file.data());
	const ReadResult again = readMap(written.bytes);
	CHECK(again.ok() && again.upgrades.empty() && sameContent(read.map, again.map));
}

// Conditions before version 4 and actions before version 2 carry no internal name; they are written
// back in their own version.
// Some of the game's own maps store 0 for an empty cliff info table; it must come back unchanged.
static void testEmptyTableCounts()
{
	std::printf("Empty table counts\n");
	Map map = makeTestMap();
	map.blendTiles->cliffInfos.clear();
	map.blendTiles->emptyCliffCount = 0;
	const WriteResult written = writeMap(map);
	CHECK(written.ok());
	const ReadResult read = readMap(written.bytes);
	CHECK(read.ok() && read.map.blendTiles->emptyCliffCount == 0 && read.map.blendTiles->cliffInfos.empty());
	const WriteResult again = writeMap(read.map);
	CHECK(again.ok() && again.bytes == written.bytes);
	// Adding an entry writes the real count again.
	Map edited = read.map;
	edited.blendTiles->cliffInfos.push_back(CliffInfo{});
	const ReadResult back = readMap(writeMap(edited).bytes);
	CHECK(back.ok() && back.map.blendTiles->cliffInfos.size() == 1);
}

static void testLegacyScriptCalls()
{
	std::printf("Legacy script calls\n");
	Map map = makeTestMap();
	ScriptCall &cond = map.sides->sides[1].scripts.scripts[0].conditions[0].conditions[0];
	cond.internalName.clear();
	cond.legacyVersion = 3;
	ScriptCall &action = map.sides->sides[1].scripts.scripts[0].actions[0];
	action.internalName.clear();
	action.legacyVersion = 1;
	const WriteResult written = writeMap(map);
	CHECK(written.ok());
	const ReadResult read = readMap(written.bytes);
	CHECK(read.ok() && read.upgrades.empty());
	const ScriptCall &c2 = read.map.sides->sides[1].scripts.scripts[0].conditions[0].conditions[0];
	CHECK(c2.legacyVersion == 3 && c2.internalName.empty() && c2.parameters == cond.parameters);
	CHECK(read.map.sides->sides[1].scripts.scripts[0].actions[0].legacyVersion == 1);
	CHECK(sameContent(map, read.map));
	const WriteResult again = writeMap(read.map);
	CHECK(again.ok() && again.bytes == written.bytes);

	// A name-less call claiming a version with name keys cannot be written.
	cond.legacyVersion = 4;
	CHECK(!writeMap(map).ok());
}

static void testFiles()
{
	std::printf("Files\n");
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "MapEditorTests";
	std::filesystem::create_directories(dir);
	const std::filesystem::path path = dir / "sample.map";
	const Map map = makeTestMap();
	std::string error;
	CHECK(saveMap(path, map, error));
	CHECK(error.empty());
	const LoadedMap loaded = loadMap(path);
	CHECK(loaded.result.ok());
	CHECK(loaded.compression == 1);
	CHECK(sameContent(map, loaded.result.map));
	CHECK(!std::filesystem::exists(dir / "sample.map.tmp"));

	// Saving the loaded map again gives the same file.
	const std::filesystem::path copy = dir / "copy.map";
	CHECK(saveMap(copy, loaded.result.map, error));
	std::vector<std::uint8_t> a;
	std::vector<std::uint8_t> b;
	CHECK(readFile(path, a, error) && readFile(copy, b, error) && a == b);

	CHECK(!loadMap(dir / "missing.map").result.ok());
	std::filesystem::remove_all(dir);
}

// Adds a value to a counter kept in the map's world info; merges with the next add.
class AddCommand : public MapEditorCore::Command
{
public:
	explicit AddCommand(std::int32_t amount) : m_amount(amount) {}
	virtual void apply(Map &map) override { setCounter(map, counter(map) + m_amount); }
	virtual void revert(Map &map) override { setCounter(map, counter(map) - m_amount); }
	virtual std::string name() const override { return "Add"; }
	virtual MapEditorCore::Changes changes() const override { return {}; }
	virtual bool mergeWith(const MapEditorCore::Command &next) override
	{
		const auto *add = dynamic_cast<const AddCommand *>(&next);
		if (!add)
			return false;
		m_amount += add->m_amount;
		return true;
	}
	static std::int32_t counter(const Map &map) { return map.worldInfo ? map.worldInfo->getInt("counter").value_or(0) : 0; }
	static void setCounter(Map &map, std::int32_t value)
	{
		if (!map.worldInfo)
			map.worldInfo.emplace();
		map.worldInfo->set("counter", value);
	}

private:
	std::int32_t m_amount;
};

static void testHistory()
{
	using MapEditorCore::History;
	Map map;
	History h;
	CHECK(!h.canUndo() && !h.canRedo() && !h.isModified());

	h.execute(std::make_unique<AddCommand>(2), map);
	CHECK(AddCommand::counter(map) == 2);
	CHECK(h.isModified() && h.undoName() == "Add");
	h.markSaved();
	CHECK(!h.isModified());

	// The saved step is not merged into; the next one merges with the step after it.
	h.execute(std::make_unique<AddCommand>(3), map);
	h.execute(std::make_unique<AddCommand>(4), map);
	CHECK(AddCommand::counter(map) == 9);
	h.undo(map);
	CHECK(AddCommand::counter(map) == 2);
	CHECK(!h.isModified());
	h.undo(map);
	CHECK(AddCommand::counter(map) == 0 && h.isModified() && !h.canUndo());
	h.redo(map);
	h.redo(map);
	CHECK(AddCommand::counter(map) == 9 && !h.canRedo());
	CHECK(h.undo(map).objects.empty());

	// A new command after undo drops the redo steps; the saved state stays reachable only by undo.
	h.undo(map);
	h.undo(map);
	CHECK(AddCommand::counter(map) == 0);
	h.execute(std::make_unique<AddCommand>(5), map);
	CHECK(AddCommand::counter(map) == 5 && !h.canRedo() && h.isModified());
	h.undo(map);
	CHECK(h.isModified());	// the saved state (2) is gone

	h.clear();
	CHECK(!h.canUndo() && !h.isModified());
}

static void testMoveObjects()
{
	using MapEditorCore::MoveObjects;
	Map map;
	map.objects = std::vector<MapObject>(3);
	(*map.objects)[1].x = 10.0f;
	(*map.objects)[1].y = 20.0f;
	(*map.objects)[1].z = 5.0f;

	MapEditorCore::History h;
	h.execute(std::make_unique<MoveObjects>(std::vector<MoveObjects::Move>{{1, 10.0f, 20.0f, 30.0f, 40.0f}, {7, 0, 0, 1, 1}}), map);
	const MapObject &o = (*map.objects)[1];
	CHECK(o.x == 30.0f && o.y == 40.0f && o.z == 5.0f);
	CHECK(h.undoName() == "Move objects");
	const MapEditorCore::Changes c = h.undo(map);
	CHECK(o.x == 10.0f && o.y == 20.0f);
	CHECK(c.objects.size() == 2 && c.objects[0] == 1 && c.objects[1] == 7);

	// A move after the last one is its own step (one drag, one undo).
	h.redo(map);
	h.execute(std::make_unique<MoveObjects>(std::vector<MoveObjects::Move>{{1, 30.0f, 40.0f, 50.0f, 60.0f}}), map);
	CHECK(o.x == 50.0f && h.undoName() == "Move object");
	h.undo(map);
	CHECK(o.x == 30.0f && o.y == 40.0f);

	MapEditorCore::Selection sel;
	CHECK(sel.empty());
	sel.set(2);
	CHECK(sel.contains(2) && !sel.contains(1));
	sel.add(1);
	sel.add(2);
	CHECK(sel.objects.size() == 2);
	sel.toggle(2);
	CHECK(!sel.contains(2) && sel.contains(1));
	sel.toggle(2);
	CHECK(sel.contains(2) && sel.objects.size() == 2);
	sel.clear();
	CHECK(sel.empty());
}

static void testEditObjects()
{
	using MapEditorCore::EditObjects;
	Map map;
	map.objects = std::vector<MapObject>(2);
	MapObject &o = (*map.objects)[0];
	o.templateName = "Tank";
	o.properties.set("objectInitialHealth", std::int32_t(100));
	MapObject changed = o;
	changed.z = 12.0f;
	changed.angle = 1.5f;
	changed.properties.set("objectInitialHealth", std::int32_t(50));

	MapEditorCore::History h;
	h.execute(std::make_unique<EditObjects>("Change object", std::vector<EditObjects::Edit>{{0, o, changed}}), map);
	CHECK(o.z == 12.0f && o.angle == 1.5f && o.properties.getInt("objectInitialHealth") == 50);
	CHECK(h.undoName() == "Change object");
	CHECK(h.undo(map).objects == std::vector<std::int32_t>{0});
	CHECK(o.z == 0.0f && o.angle == 0.0f && o.properties.getInt("objectInitialHealth") == 100);
	h.redo(map);
	CHECK(o == changed);
	CHECK(!h.undo(map).rebuild);
	h.execute(std::make_unique<EditObjects>("Change team", std::vector<EditObjects::Edit>{{0, o, changed}}, true), map);
	CHECK(h.undo(map).rebuild);
}

static void testDeleteObjects()
{
	using MapEditorCore::DeleteObjects;
	Map map;
	map.objects = std::vector<MapObject>(5);
	for (int i = 0; i < 5; ++i)
		(*map.objects)[static_cast<std::size_t>(i)].templateName = "T" + std::to_string(i);
	(*map.objects)[1].properties.set("waypointID", std::int32_t(11));
	(*map.objects)[4].properties.set("waypointID", std::int32_t(14));
	(*map.objects)[3].properties.set("waypointID", std::int32_t(13));
	map.waypointLinks = std::vector<WaypointLink>{{11, 13}, {13, 14}, {14, 11}};
	const std::vector<MapObject> originalObjects = *map.objects;
	const std::vector<WaypointLink> originalLinks = *map.waypointLinks;
	const auto unchanged = [&] { return *map.objects == originalObjects && *map.waypointLinks == originalLinks; };

	MapEditorCore::History h;
	h.execute(std::make_unique<DeleteObjects>(map, std::vector<std::int32_t>{3, 1, 3, 9}), map);
	CHECK(map.objects->size() == 3);
	CHECK((*map.objects)[0].templateName == "T0" && (*map.objects)[1].templateName == "T2" && (*map.objects)[2].templateName == "T4");
	CHECK(map.waypointLinks->size() == 0);
	CHECK(h.undoName() == "Delete objects");
	const MapEditorCore::Changes undone = h.undo(map);
	CHECK(unchanged());
	CHECK(undone.reindexed && undone.objects == (std::vector<std::int32_t>{1, 3}));
	const MapEditorCore::Changes redone = h.redo(map);
	CHECK(redone.reindexed && redone.objects.empty() && map.objects->size() == 3);

	// One waypoint: only its links go.
	h.undo(map);
	h.execute(std::make_unique<DeleteObjects>(map, std::vector<std::int32_t>{4}), map);
	CHECK(h.undoName() == "Delete object");
	CHECK(map.waypointLinks->size() == 1 && (*map.waypointLinks)[0].from == 11 && (*map.waypointLinks)[0].to == 13);
	h.undo(map);
	CHECK(unchanged());
}

static void testCopyPaste()
{
	using namespace MapEditorCore;
	Map map;
	map.objects = std::vector<MapObject>(4);
	auto &o = *map.objects;
	o[0].templateName = "Folder/TreePine";
	o[0].x = 10.0f;
	o[0].properties.set("uniqueID", std::string("TreePine 7"));
	o[0].properties.set("objectName", std::string("TheTree"));
	o[1].templateName = "Barracks";
	o[1].properties.set("uniqueID", std::string("Barracks 12"));
	o[2].templateName = "*Waypoints/Waypoint";
	o[2].properties.set("waypointID", std::int32_t(3));
	o[2].properties.set("waypointName", std::string("Player_1_Start"));
	o[3].templateName = "*Waypoints/Waypoint";
	o[3].properties.set("waypointID", std::int32_t(5));
	o[3].properties.set("waypointName", std::string("Waypoint 6"));
	map.waypointLinks = std::vector<WaypointLink>{{3, 5}, {5, 3}};

	const Clipboard clip = copyObjects(map, {0, 2, 3, 3, 99});
	CHECK(clip.objects.size() == 3 && clip.links.size() == 2);
	const Clipboard single = copyObjects(map, {2});
	CHECK(single.objects.size() == 1 && single.links.empty());

	History h;
	h.execute(pasteObjects(map, clip, 100.0f, 50.0f), map);
	CHECK(map.objects->size() == 7);
	const MapObject &tree = (*map.objects)[4];
	CHECK(tree.x == 110.0f && tree.y == 50.0f && tree.templateName == "Folder/TreePine");
	CHECK(tree.properties.getAscii("uniqueID") == std::string("TreePine 13"));
	CHECK(!tree.properties.find("objectName"));
	const MapObject &w1 = (*map.objects)[5];
	const MapObject &w2 = (*map.objects)[6];
	CHECK(w1.properties.getInt("waypointID") == 6 && w2.properties.getInt("waypointID") == 7);
	// "Waypoint 6" is taken, so the first copy is named "Waypoint 7"; the second takes the next free one.
	CHECK(w1.properties.getAscii("waypointName") == std::string("Waypoint 7"));
	CHECK(w2.properties.getAscii("waypointName") == std::string("Waypoint 8"));
	CHECK(map.waypointLinks->size() == 4 && (*map.waypointLinks)[2].from == 6 && (*map.waypointLinks)[2].to == 7);
	CHECK(h.undoName() == "Paste");
	const Changes undone = h.undo(map);
	CHECK(map.objects->size() == 4 && map.waypointLinks->size() == 2 && undone.reindexed && undone.objects.empty());
	const Changes redone = h.redo(map);
	CHECK(redone.objects == (std::vector<std::int32_t>{4, 5, 6}));

	// Cut: a delete under another name.
	h.execute(std::make_unique<DeleteObjects>(map, std::vector<std::int32_t>{0}, "Cut"), map);
	CHECK(h.undoName() == "Cut" && map.objects->size() == 6);
}

static void testWaypoints()
{
	using namespace MapEditorCore;
	Map map;
	map.objects = std::vector<MapObject>(2);
	(*map.objects)[0].templateName = "Barracks";
	(*map.objects)[1].templateName = WAYPOINT_TEMPLATE;
	(*map.objects)[1].properties.set("waypointID", std::int32_t(4));
	(*map.objects)[1].properties.set("waypointName", std::string("Waypoint 4"));
	(*map.objects)[1].properties.set("waypointPathLabel1", std::string("Attack"));
	History h;

	// A new waypoint linked from the existing one takes its path label.
	h.execute(newWaypoint(map, 50.0f, 60.0f, 1), map);
	CHECK(map.objects->size() == 3 && h.undoName() == "Add waypoint");
	const MapObject &w2 = (*map.objects)[2];
	CHECK(w2.templateName == WAYPOINT_TEMPLATE && w2.x == 50.0f && w2.y == 60.0f);
	CHECK(w2.properties.getInt("waypointID") == 5 && w2.properties.getAscii("waypointName") == std::string("Waypoint 5"));
	CHECK(w2.properties.getAscii("waypointPathLabel1") == std::string("Attack"));
	CHECK(w2.properties.getAscii("originalOwner") == std::string("team"));
	CHECK(map.waypointLinks && map.waypointLinks->size() == 1 && (*map.waypointLinks)[0] == (WaypointLink{4, 5}));
	CHECK(linkedWaypoints(map, 1) == std::vector<std::int32_t>{2} && waypointPath(map, 2).size() == 2);
	// Unlinked: no link, no label.
	h.execute(newWaypoint(map, 90.0f, 60.0f), map);
	CHECK(map.objects->size() == 4 && map.waypointLinks->size() == 1 && !(*map.objects)[3].properties.find("waypointPathLabel1"));
	CHECK(!newWaypoint(map, 0.0f, 0.0f, 0)->objects().empty());	// a non-waypoint links nothing

	// Toggle: link 5 -> 6 joins the paths (the label spreads), a second toggle removes it (either direction).
	h.execute(toggleWaypointLink(map, 2, 3), map);
	CHECK(h.undoName() == "Link waypoints" && map.waypointLinks->size() == 2);
	CHECK((*map.objects)[3].properties.getAscii("waypointPathLabel1") == std::string("Attack"));
	CHECK(waypointPath(map, 1).size() == 3);
	h.execute(toggleWaypointLink(map, 3, 2), map);
	CHECK(h.undoName() == "Unlink waypoints" && map.waypointLinks->size() == 1);
	CHECK(!toggleWaypointLink(map, 0, 1) && !toggleWaypointLink(map, 1, 1));
	h.undo(map);
	CHECK(map.waypointLinks->size() == 2);
	h.undo(map);
	CHECK(map.waypointLinks->size() == 1 && !(*map.objects)[3].properties.find("waypointPathLabel1"));

	// Path property on the whole path.
	h.execute(setPathProperty(map, 2, "waypointPathBiDirectional", MapModel::PropertyValue(true)), map);
	CHECK((*map.objects)[1].properties.getBool("waypointPathBiDirectional") == true);
	CHECK((*map.objects)[2].properties.getBool("waypointPathBiDirectional") == true);
	CHECK(!(*map.objects)[3].properties.find("waypointPathBiDirectional"));
	CHECK(!setPathProperty(map, 2, "waypointPathBiDirectional", MapModel::PropertyValue(true)));
	const Changes changed = h.undo(map);
	CHECK(changed.links && !(*map.objects)[1].properties.find("waypointPathBiDirectional"));

	// Chain in selection order, then unlink all of them.
	h.execute(chainWaypoints(map, {3, 1, 2, 0}), map);
	CHECK(map.waypointLinks->size() == 2 && (*map.waypointLinks)[1] == (WaypointLink{6, 4}));
	CHECK(!(*map.objects)[1].properties.find("waypointPathLabel1"));	// the first (no label) sets the path
	CHECK(!chainWaypoints(map, {3, 1, 2}));
	h.execute(unlinkWaypoints(map, {1, 2, 3}), map);
	CHECK(map.waypointLinks->empty() && !unlinkWaypoints(map, {1, 2}));
	while (h.canUndo())
		h.undo(map);
	CHECK(map.objects->size() == 2 && !map.waypointLinks && (*map.objects)[1].properties.getAscii("waypointPathLabel1") == std::string("Attack"));
}

static void testAreas()
{
	using namespace MapEditorCore;
	Map map;
	map.heightMap = HeightMap{};
	map.heightMap->width = 4;
	map.heightMap->height = 4;
	map.heightMap->borderSize = 1;
	map.heightMap->heights = std::vector<std::uint8_t>(16, 32);
	map.heightMap->heights[2 * 4 + 2] = 64;	// cell (2, 2) = world (10, 10)
	CHECK(groundHeight(map, 0.0f, 0.0f) == 20.0f && groundHeight(map, 10.0f, 10.0f) == 40.0f && groundHeight(map, 5.0f, 10.0f) == 30.0f);

	History h;
	PolygonArea square;
	square.points = {{{0, 0, 0}}, {{100, 0, 0}}, {{100, 100, 0}}, {{0, 100, 0}}};
	h.execute(addArea(map, square), map);
	CHECK(map.polygons && map.polygons->size() == 1 && h.undoName() == "Add area");
	const PolygonArea first = (*map.polygons)[0];
	CHECK(first.id == 1 && first.name == "Area 1");
	PolygonArea lake = square;
	lake.isWater = true;
	h.execute(addArea(map, lake), map);
	CHECK((*map.polygons)[1].id == 2 && (*map.polygons)[1].name == "Water Area 2");

	// Geometry of the tool.
	CHECK(areaPointAt(first, 103.0f, 98.0f, 5.0f) == 2 && areaPointAt(first, 50.0f, 50.0f, 5.0f) == -1);
	CHECK(areaEdgeAt(first, 50.0f, 2.0f, 5.0f) == 1 && areaEdgeAt(first, 2.0f, 50.0f, 5.0f) == 4 && areaEdgeAt(first, 50.0f, 50.0f, 5.0f) == -1);
	CHECK(areaContains(first, 50.0f, 50.0f) && !areaContains(first, 150.0f, 50.0f));

	PolygonArea moved = first;
	moved.points[2] = {{120, 110, 0}};
	CHECK(!changeArea(map, 0, first, "Move point"));
	h.execute(changeArea(map, 0, moved, "Move point"), map);
	CHECK((*map.polygons)[0].points[2][0] == 120 && h.undoName() == "Move point");
	const Changes changed = h.undo(map);
	CHECK(changed.areas && (*map.polygons)[0].points[2][0] == 100);
	h.redo(map);
	h.execute(deleteArea(map, 0), map);
	CHECK(map.polygons->size() == 1 && (*map.polygons)[0].isWater && !deleteArea(map, 5));
	while (h.canUndo())
		h.undo(map);
	CHECK(!map.polygons);
}

static void testRoads()
{
	using namespace MapEditorCore;
	Map map;
	map.objects = std::vector<MapObject>(1);
	(*map.objects)[0].templateName = "Barracks";
	History h;

	h.execute(addRoad(map, "TwoLane", 0.0f, 0.0f, 100.0f, 0.0f, ROAD_CORNER_ANGLED), map);
	CHECK(map.objects->size() == 3 && h.undoName() == "Add road" && roadSegments(map) == std::vector<std::int32_t>{1});
	const MapObject &p1 = (*map.objects)[1];
	const MapObject &p2 = (*map.objects)[2];
	CHECK(p1.templateName == "TwoLane" && p1.flags == (ROAD_POINT1 | ROAD_CORNER_ANGLED) && p2.flags == (ROAD_POINT2 | ROAD_CORNER_ANGLED));
	CHECK(p2.x == 100.0f && p1.properties.getAscii("originalOwner") == std::string("team") && isRoadOrBridgePoint(p1));

	// A second segment from the end; ends and points on the line are found.
	h.execute(addRoad(map, "TwoLane", 100.0f, 0.0f, 100.0f, 80.0f, 0), map);
	float ex = 0.0f, ey = 0.0f;
	CHECK(roadEndAt(map, 103.0f, 2.0f, 5.0f, ex, ey) >= 1 && ex == 100.0f && ey == 0.0f);
	CHECK(roadPointsAt(map, 100.0f, 0.0f).size() == 2);
	float ox = 0.0f, oy = 0.0f;
	CHECK(roadSegmentAt(map, 40.0f, 3.0f, 5.0f, ox, oy) == 1 && ox == 40.0f && oy == 0.0f);
	CHECK(roadSegmentAt(map, 40.0f, 30.0f, 5.0f, ox, oy) == -1);

	// Branching off the middle of the first segment cuts it in two (one step).
	h.execute(addRoad(map, "Dirt", 40.0f, 0.0f, 40.0f, -60.0f, 0, 1), map);
	const std::vector<std::int32_t> segments = roadSegments(map);
	CHECK(segments.size() == 4 && map.objects->size() == 9 && h.undoName() == "Add road");
	CHECK(roadPointsAt(map, 40.0f, 0.0f).size() == 3);
	bool keptAngle = true;
	for (const MapObject &o : *map.objects)
	{
		if ((o.flags & (ROAD_POINT1 | ROAD_POINT2)) && o.templateName == "TwoLane" && o.y == 0.0f && o.x != 100.0f)
			keptAngle = keptAngle && (o.flags & ROAD_CORNER_ANGLED);
	}
	CHECK(keptAngle);
	h.undo(map);
	CHECK(roadSegments(map).size() == 2 && map.objects->size() == 5);
	h.redo(map);

	// Change and delete a segment.
	const std::int32_t last = roadSegments(map).back();
	CHECK(!changeRoad(map, last, (*map.objects)[static_cast<std::size_t>(last)].templateName, 0));
	h.execute(changeRoad(map, last, "TwoLane", ROAD_CORNER_TIGHT), map);
	CHECK((*map.objects)[static_cast<std::size_t>(last)].flags == (ROAD_POINT1 | ROAD_CORNER_TIGHT) && h.undoName() == "Change road");
	h.execute(deleteRoad(map, last), map);
	CHECK(roadSegments(map).size() == 3 && !deleteRoad(map, 0));
	while (h.canUndo())
		h.undo(map);
	CHECK(map.objects->size() == 1);

	// A road ending on the middle of another makes a junction: both are cut there, one step.
	h.execute(addRoad(map, "TwoLane", 0.0f, 0.0f, 100.0f, 0.0f, 0), map);
	const std::int32_t crossed = roadSegmentAt(map, 50.0f, 2.0f, 5.0f, ox, oy);
	h.execute(addRoad(map, "TwoLane", 50.0f, 80.0f, ox, oy, ROAD_JOIN, -1, crossed), map);
	CHECK(roadSegments(map).size() == 3 && roadPointsAt(map, 50.0f, 0.0f).size() == 3 && h.undoName() == "Add road");
	CHECK(((*map.objects).back().flags & ROAD_JOIN) && ((*map.objects).back().flags & ROAD_POINT2));
	h.execute(changeRoad(map, roadSegments(map).back(), "TwoLane", 0), map);
	CHECK(!((*map.objects).back().flags & ROAD_JOIN));

	// Bridges: their own flags, no corners, found only as bridges.
	h.execute(addBridge(map, "WoodBridge", 200.0f, 0.0f, 300.0f, 0.0f), map);
	const std::int32_t bridge = static_cast<std::int32_t>(map.objects->size()) - 2;
	CHECK(h.undoName() == "Add bridge" && isBridgeSegment(map, bridge) && roadSegments(map).size() == 3);
	CHECK((*map.objects)[static_cast<std::size_t>(bridge)].flags == BRIDGE_POINT1 && (*map.objects).back().flags == BRIDGE_POINT2);
	CHECK(roadSegmentAt(map, 250.0f, 1.0f, 5.0f, ox, oy) == -1 && roadSegmentAt(map, 250.0f, 1.0f, 5.0f, ox, oy, SegmentKind::Both) == bridge);
	CHECK(!changeRoad(map, bridge, "WoodBridge", ROAD_CORNER_ANGLED));
	h.execute(changeRoad(map, bridge, "StoneBridge", 0), map);
	CHECK((*map.objects)[static_cast<std::size_t>(bridge)].templateName == "StoneBridge");
	h.execute(deleteRoad(map, bridge), map);
	CHECK(roadSegments(map, SegmentKind::Bridges).empty());
	while (h.canUndo())
		h.undo(map);
	CHECK(map.objects->size() == 1);
}

static void testClipboardBytes()
{
	using namespace MapEditorCore;
	Clipboard clip;
	clip.objects.resize(2);
	clip.objects[0].templateName = "TreePine";
	clip.objects[0].x = 12.5f;
	clip.objects[0].angle = 1.25f;
	clip.objects[0].properties.set("uniqueID", std::string("TreePine 3"));
	clip.objects[1].templateName = WAYPOINT_TEMPLATE;
	clip.objects[1].properties.set("waypointID", std::int32_t(4));
	clip.objects[1].properties.set("waypointName", std::string("Waypoint 4"));
	clip.links = {{4, 4}};
	const std::vector<std::uint8_t> bytes = clipboardToBytes(clip);
	CHECK(!bytes.empty());
	const std::optional<Clipboard> back = clipboardFromBytes(bytes);
	CHECK(back && back->objects == clip.objects && back->links == clip.links);
	const std::vector<std::uint8_t> junk = {1, 2, 3, 4, 5};
	CHECK(!clipboardFromBytes(junk) && !clipboardFromBytes({}));
}

static void testHeightBrush()
{
	using namespace MapEditorCore;
	Map map;
	map.heightMap = HeightMap{};
	HeightMap &hm = *map.heightMap;
	hm.width = 40;
	hm.height = 30;
	hm.borderSize = 5;
	hm.heights = std::vector<std::uint8_t>(40 * 30, 50);
	const auto at = [&hm](int x, int y) { return static_cast<int>(hm.heights[static_cast<std::size_t>(y * hm.width + x)]); };
	float cx = 0.0f, cy = 0.0f;
	worldToCell(hm, 100.0f, 50.0f, cx, cy);
	CHECK(cx == 15.0f && cy == 10.0f);

	const std::vector<std::uint8_t> before = hm.heights;
	Brush brush;
	brush.radius = 4.0f;
	brush.feather = 0.5f;
	brush.strength = 6.0f;
	CellRect stroke = applyBrush(hm, cx, cy, brush);
	stroke.unite(applyBrush(hm, cx + 1.0f, cy, brush));
	CHECK(at(15, 10) == 62 && at(15, 14) == 50 && at(11, 10) == 50 && at(13, 10) > 50 && at(13, 10) < 62);
	CHECK(!stroke.empty() && stroke.x0 >= 11 && stroke.x1 <= 20 && stroke.y0 >= 6 && stroke.y1 <= 14);

	History h;
	auto command = heightStroke(map, before, stroke, "Raise terrain");
	CHECK(command && command->changes().terrain && command->changes().terrainRect[0] == stroke.x0);
	h.record(std::move(command));
	h.undo(map);
	CHECK(hm.heights == before);
	h.redo(map);
	CHECK(at(15, 10) == 62);
	CHECK(!heightStroke(map, hm.heights, stroke, "Raise terrain"));

	brush.mode = BrushMode::Set;
	brush.height = 20;
	applyBrush(hm, 30.0f, 20.0f, brush);
	CHECK(at(30, 20) == 20 && at(28, 20) == 20);
	brush.mode = BrushMode::Lower;
	brush.strength = 30.0f;
	applyBrush(hm, 30.0f, 20.0f, brush);
	CHECK(at(30, 20) == 0);	// clamped at the lowest step
	brush.mode = BrushMode::Smooth;
	brush.strength = 4.0f;
	// Roughness: the sum of height differences between neighbours around the bump.
	const auto roughness = [&at]() {
		int sum = 0;
		for (int y = 5; y <= 15; ++y)
			for (int x = 9; x <= 21; ++x)
				sum += std::abs(at(x + 1, y) - at(x, y)) + std::abs(at(x, y + 1) - at(x, y));
		return sum;
	};
	const int rough = roughness();
	applyBrush(hm, 15.0f, 10.0f, brush);
	CHECK(roughness() < rough);
}

static void testTexturePaint()
{
	using namespace MapEditorCore;
	CHECK(texturesFit({10, 10, 10, 10}) && !texturesFit({10, 10, 10, 10, 10}) && texturesFit({4, 4, 2, 1}));

	Map map;
	map.heightMap = HeightMap{};
	map.heightMap->width = 20;
	map.heightMap->height = 20;
	map.heightMap->heights = std::vector<std::uint8_t>(400, 10);
	map.blendTiles = BlendTiles{};
	BlendTiles &bt = *map.blendTiles;
	bt.tiles = std::vector<std::int16_t>(400, 0);
	bt.blends = std::vector<std::int16_t>(400, 0);
	bt.extraBlends = std::vector<std::int16_t>(400, 0);
	bt.cliffInfoIndices = std::vector<std::int16_t>(400, 0);
	CHECK(addTextureClass(bt, "Grass", 16, 4) == 0 && bt.numBitmapTiles == 16);
	CHECK(textureClassIndex(bt, "Grass") == 0 && textureClassIndex(bt, "Sand") == -1);
	bt.blends[5 * 20 + 5] = 3;	// a blended cell becomes opaque when painted

	const BlendTiles before = bt;
	const int sand = addTextureClass(bt, "Sand", 4, 2);
	CHECK(sand == 1 && bt.textureClasses[1].firstTile == 16 && bt.numBitmapTiles == 20);
	CHECK(tileForClass(bt.textureClasses[1], 5, 6) == static_cast<std::int16_t>(((16 + 0 + 2 * 1) << 2) + 2 * 0 + 1));
	const CellRect rect = paintTexture(bt, 20, 20, 5.5f, 5.5f, 2.0f, sand);
	CHECK(!rect.empty() && rect.x0 >= 3 && rect.x1 <= 7);
	CHECK(bt.tiles[5 * 20 + 5] == tileForClass(bt.textureClasses[1], 5, 5) && bt.blends[5 * 20 + 5] == 0);
	CHECK(bt.tiles[0] == 0 && paintTexture(bt, 20, 20, 5.5f, 5.5f, 2.0f, sand).empty());

	History h;
	auto command = tileStroke(map, before, rect, "Paint texture");
	CHECK(command && command->changes().textures && command->changes().textureClasses);
	h.record(std::move(command));
	h.undo(map);
	CHECK(bt == before);
	h.redo(map);
	CHECK(bt.textureClasses.size() == 2 && bt.tiles[5 * 20 + 5] == tileForClass(bt.textureClasses[1], 5, 5));
	CHECK(addTextureClass(bt, "Huge", 2000, 10) == -1);
}

static void testTextureBlend()
{
	using namespace MapEditorCore;
	Map map;
	map.heightMap = HeightMap{};
	map.heightMap->width = 20;
	map.heightMap->height = 20;
	map.heightMap->heights = std::vector<std::uint8_t>(400, 10);
	map.blendTiles = BlendTiles{};
	BlendTiles &bt = *map.blendTiles;
	bt.tiles = std::vector<std::int16_t>(400, 0);
	bt.blends = std::vector<std::int16_t>(400, 0);
	bt.extraBlends = std::vector<std::int16_t>(400, 0);
	bt.cliffInfoIndices = std::vector<std::int16_t>(400, 0);
	const int grass = addTextureClass(bt, "Grass", 16, 4);
	const int sand = addTextureClass(bt, "Sand", 4, 2);
	const int dirt = addTextureClass(bt, "Dirt", 4, 2);
	for (int y = 0; y < 20; ++y)
		for (int x = 0; x < 20; ++x)
			bt.tiles[static_cast<std::size_t>(y * 20 + x)] = tileForClass(bt.textureClasses[static_cast<std::size_t>(grass)], x, y);
	const auto at = [](int x, int y) { return static_cast<std::size_t>(y * 20 + x); };
	const auto blendOf = [&bt](std::int16_t k) { return k > 0 ? &bt.blendedTiles[static_cast<std::size_t>(k) - 1] : nullptr; };
	CHECK(classOfTile(bt, bt.tiles[at(3, 3)]) == grass && cellClass(bt, 20, 20, 3, 3, false) == grass);

	// One edge by hand: from the right, then a second texture from below into the extra layer.
	const BlendTiles plain = bt;
	CHECK(!blendEdge(bt, 20, 20, 3, 3, 4, 3, sand).empty());
	const BlendedTile *first = blendOf(bt.blends[at(3, 3)]);
	CHECK(first && first->horizontal && first->inverted == 0 && classOfTile(bt, first->blendIndex) == sand);
	CHECK(cellClass(bt, 20, 20, 3, 3, false) == -1 && cellClass(bt, 20, 20, 3, 3, true) == grass);
	CHECK(!blendEdge(bt, 20, 20, 3, 3, 3, 2, dirt).empty());
	const BlendedTile *second = blendOf(bt.extraBlends[at(3, 3)]);
	CHECK(second && second->vertical && second->inverted == 1 && classOfTile(bt, second->blendIndex) == dirt);
	CHECK(blendEdge(bt, 20, 20, 5, 5, 5, 5, sand).empty());
	bt = plain;

	// Paint a disc of sand, then soften its edges.
	const BlendTiles before = bt;
	CellRect rect = paintTexture(bt, 20, 20, 10.0f, 10.0f, 3.0f, sand);
	const BlendTiles hard = bt;
	const CellRect edges = softenEdges(bt, 20, 20, sand, rect);
	CHECK(!edges.empty() && edges.x0 < rect.x0 && edges.x1 > rect.x1);
	rect.unite(edges);
	CHECK(!bt.blendedTiles.empty());
	int left = 0;
	while (left < 20 && cellClass(bt, 20, 20, left, 10, true) != sand)
		++left;
	CHECK(left > 1 && left < 10);
	const BlendedTile *edge = blendOf(bt.blends[at(left - 1, 10)]);
	CHECK(edge && edge->horizontal && edge->inverted == 0 && classOfTile(bt, edge->blendIndex) == sand);
	CHECK(bt.blends[at(10, 10)] == 0 && bt.blends[at(0, 0)] == 0 && bt.blends[at(left - 3, 10)] == 0);
	int blended = 0;
	for (std::size_t i = 0; i < 400; ++i)
		blended += bt.blends[i] != 0 ? 1 : 0;
	CHECK(blended >= 12);
	const BlendTiles soft = bt;
	softenEdges(bt, 20, 20, sand, rect);
	CHECK(bt == soft);

	// Painting over the edge and softening again leaves no blend toward sand inside the sand.
	const CellRect more = paintTexture(bt, 20, 20, 7.0f, 10.0f, 2.0f, sand);
	softenEdges(bt, 20, 20, sand, more);
	bool clean = true;
	for (int y = 0; y < 20; ++y)
		for (int x = 0; x < 20; ++x)
			if (cellClass(bt, 20, 20, x, y, true) == sand && bt.blends[at(x, y)] != 0)
				clean = false;
	CHECK(clean);

	// Auto edge from one sand cell over the whole region gives the same edges as softening.
	bt = hard;
	CHECK(!autoBlend(bt, 20, 20, {{10, 10}}).empty());
	bool same = true;
	for (std::size_t i = 0; i < 400; ++i)
	{
		const BlendedTile *a = blendOf(bt.blends[i]);
		const BlendedTile *b = soft.blends[i] > 0 ? &soft.blendedTiles[static_cast<std::size_t>(soft.blends[i]) - 1] : nullptr;
		if ((a == nullptr) != (b == nullptr) || (a && !(*a == *b)))
			same = false;
	}
	CHECK(same);
	CHECK(autoBlend(bt, 20, 20, {{left - 1, 10}}).empty());	// a blended cell starts nothing

	// The stroke with its blends is one step.
	bt = soft;
	History h;
	auto command = tileStroke(map, before, rect, "Paint texture");
	CHECK(command != nullptr);
	h.record(std::move(command));
	h.undo(map);
	CHECK(bt == before);
	h.redo(map);
	CHECK(bt.blendedTiles == soft.blendedTiles && bt.blends[at(left - 1, 10)] == soft.blends[at(left - 1, 10)]);
}

static void testHeightShapes()
{
	using namespace MapEditorCore;
	HeightMap hm;
	hm.width = 40;
	hm.height = 40;
	hm.borderSize = 2;
	hm.heights = std::vector<std::uint8_t>(1600, 16);
	const auto at = [&hm](int x, int y) { return static_cast<int>(hm.heights[static_cast<std::size_t>(y * 40 + x)]); };

	// A ramp from height 10 (16 steps) at x = 50 to height 60 (96 steps) at x = 250, 40 wide.
	const CellRect ramp = applyRamp(hm, 50.0f, 100.0f, 10.0f, 250.0f, 100.0f, 60.0f, 40.0f);
	CHECK(!ramp.empty() && ramp.x0 == 8 && ramp.x1 == 27 && ramp.y0 == 10 && ramp.y1 == 14);
	CHECK(at(7, 12) == 16 && at(27, 12) == 96 && at(17, 12) == 56);
	CHECK(at(17, 9) == 16 && at(17, 15) == 16);
	bool rising = true;
	for (int x = 8; x < 27; ++x)
		rising = rising && at(x + 1, 12) >= at(x, 12);
	CHECK(rising);
	CHECK(applyRamp(hm, 50.0f, 100.0f, 10.0f, 50.0f, 100.0f, 60.0f, 40.0f).empty());

	// Molds.
	const std::vector<Mold> molds = builtInMolds();
	CHECK(molds.size() == 5 && molds[0].name == "Hill" && molds[1].name == "Crater");
	const Mold &hill = molds[0];
	CHECK(std::abs(hill.sample(0.0f, 0.0f) - 30.0f) < 0.01f && std::isnan(hill.sample(70.0f, 0.0f)));
	CHECK(hill.sample(30.0f, 0.0f) > 10.0f && hill.sample(30.0f, 0.0f) < 20.0f);
	CHECK(std::abs(hill.radius() - std::hypot(60.0f, 60.0f)) < 0.01f);

	hm.heights = std::vector<std::uint8_t>(1600, 16);
	const std::vector<std::uint8_t> flat = hm.heights;
	MoldPlacement place;
	place.x = 180.0f;
	place.y = 180.0f;
	place.height = 10.0f;
	const CellRect foot = applyMold(hm, flat, hill, place);
	CHECK(!foot.empty() && foot.x0 == 12 && foot.x1 == 28);
	CHECK(at(20, 20) == 64 && at(20 + 3, 20) > 16 && at(20 + 3, 20) < 64 && at(12, 12) == 16);

	// Twice the size reaches twice as far and twice as high (clamped to 255).
	hm.heights = flat;
	place.scale = 2.0f;
	applyMold(hm, flat, hill, place);
	CHECK(at(20, 20) == 112 && at(20 + 9, 20) > 16);

	// A crater lowers the middle; raise-only keeps the middle and builds the rim.
	hm.heights = flat;
	place.scale = 1.0f;
	place.mode = MoldMode::RaiseOnly;
	applyMold(hm, flat, molds[1], place);
	CHECK(at(20, 20) == 16 && at(20 + 6, 20) > 16);
	place.mode = MoldMode::Both;
	applyMold(hm, flat, molds[1], place);
	CHECK(at(20, 20) < 16);

	// The ridge runs along x; turned by 90 degrees along y.
	hm.heights = flat;
	applyMold(hm, flat, molds[3], place);
	CHECK(at(20 + 5, 20) > 40 && at(20, 20 + 5) == 16);
	hm.heights = flat;
	place.angle = 90.0f;
	applyMold(hm, flat, molds[3], place);
	CHECK(at(20, 20 + 5) > 40 && at(20 + 5, 20) == 16);
}

static void testPassabilityAndFill()
{
	using namespace MapEditorCore;
	Map map;
	map.heightMap = HeightMap{};
	HeightMap &hm = *map.heightMap;
	hm.width = 20;
	hm.height = 20;
	hm.heights = std::vector<std::uint8_t>(400, 10);
	map.blendTiles = BlendTiles{};
	BlendTiles &bt = *map.blendTiles;
	bt.tiles = std::vector<std::int16_t>(400, 0);
	bt.blends = std::vector<std::int16_t>(400, 0);
	bt.extraBlends = std::vector<std::int16_t>(400, 0);
	bt.cliffInfoIndices = std::vector<std::int16_t>(400, 0);
	bt.cliffState = std::vector<std::uint8_t>(cliffRowBytes(20) * 20, 0);
	CHECK(cliffRowBytes(20) == 3 && cliffRowBytes(8) == 1);

	// A height step sets the cliff flags from the new heights and undo puts the old ones back.
	const std::vector<std::uint8_t> flat = hm.heights;
	hm.heights[static_cast<std::size_t>(10 * 20 + 10)] = 40;	// 30 steps, about 19 units: steeper than the limit
	auto step = heightStroke(map, flat, CellRect{10, 10, 10, 10}, "Raise terrain");
	CHECK(step && step->changes().passability);
	CHECK(cliffFlag(bt.cliffState, 20, 9, 9) && cliffFlag(bt.cliffState, 20, 10, 10) && cliffFlag(bt.cliffState, 20, 9, 10)
		&& cliffFlag(bt.cliffState, 20, 10, 9) && !cliffFlag(bt.cliffState, 20, 11, 10) && !cliffFlag(bt.cliffState, 20, 8, 9));
	const std::vector<std::uint8_t> steep = bt.cliffState;
	History h;
	h.record(std::move(step));
	h.undo(map);
	CHECK(hm.heights == flat && std::all_of(bt.cliffState.begin(), bt.cliffState.end(), [](std::uint8_t b) { return b == 0; }));
	h.redo(map);
	CHECK(bt.cliffState == steep);
	h.undo(map);

	// The passability brush.
	const std::vector<std::uint8_t> clear = bt.cliffState;
	const CellRect painted = paintPassability(bt.cliffState, 20, 20, 5.0f, 5.0f, 2.0f, true);
	CHECK(!painted.empty() && cliffFlag(bt.cliffState, 20, 4, 4) && cliffFlag(bt.cliffState, 20, 5, 5) && !cliffFlag(bt.cliffState, 20, 8, 5));
	CHECK(paintPassability(bt.cliffState, 20, 20, 5.0f, 5.0f, 2.0f, true).empty());
	auto cliffs = cliffStroke(map, clear, painted, "Mark impassable");
	CHECK(cliffs && cliffs->changes().passability && !cliffs->changes().terrain);
	h.record(std::move(cliffs));
	h.undo(map);
	CHECK(bt.cliffState == clear);
	CHECK(!cliffStroke(map, clear, painted, "Mark impassable"));

	// Fill: the region of grass that holds the cell, not the grass beyond the sand wall.
	const int grass = addTextureClass(bt, "Grass", 4, 2);
	const int sand = addTextureClass(bt, "Sand", 4, 2);
	const int rock = addTextureClass(bt, "Rock", 4, 2);
	const auto paintAll = [&](int c, int x0, int x1) {
		for (int y = 0; y < 20; ++y)
			for (int x = x0; x <= x1; ++x)
				bt.tiles[static_cast<std::size_t>(y * 20 + x)] = tileForClass(bt.textureClasses[static_cast<std::size_t>(c)], x, y);
	};
	paintAll(grass, 0, 19);
	paintAll(sand, 10, 10);
	softenEdges(bt, 20, 20, sand, CellRect{10, 0, 10, 19});
	CHECK(bt.blends[static_cast<std::size_t>(5 * 20 + 9)] != 0);
	const BlendTiles beforeFill = bt;
	const CellRect filled = fillTexture(bt, 20, 20, 3, 3, rock, false);
	CHECK(!filled.empty() && cellClass(bt, 20, 20, 0, 0, true) == rock && cellClass(bt, 20, 20, 9, 19, true) == rock);
	CHECK(cellClass(bt, 20, 20, 15, 5, true) == grass && cellClass(bt, 20, 20, 10, 5, true) == sand);
	CHECK(bt.blends[static_cast<std::size_t>(5 * 20 + 9)] == beforeFill.blends[static_cast<std::size_t>(5 * 20 + 9)]);
	CHECK(fillTexture(bt, 20, 20, 3, 3, rock, false).empty());
	// Replace every sand cell, and the blends of sand over the rock show rock now.
	bt = beforeFill;
	CHECK(!fillTexture(bt, 20, 20, 10, 5, rock, true).empty());
	const std::int16_t k = bt.blends[static_cast<std::size_t>(5 * 20 + 9)];
	CHECK(cellClass(bt, 20, 20, 10, 0, true) == rock && k > 0 && classOfTile(bt, bt.blendedTiles[static_cast<std::size_t>(k) - 1].blendIndex) == rock);

	// Optimize: sand is gone now, so the rock tiles move down and the table keeps what cells use.
	CHECK(optimizeTextures(bt, 20, 20));
	CHECK(bt.textureClasses.size() == 2 && bt.textureClasses[0].name == "Grass" && bt.textureClasses[1].name == "Rock"
		&& bt.textureClasses[1].firstTile == 4 && bt.numBitmapTiles == 8);
	CHECK(cellClass(bt, 20, 20, 10, 0, true) == 1 && cellClass(bt, 20, 20, 0, 0, true) == 0);
	const std::int16_t k2 = bt.blends[static_cast<std::size_t>(5 * 20 + 9)];
	CHECK(k2 > 0 && classOfTile(bt, bt.blendedTiles[static_cast<std::size_t>(k2) - 1].blendIndex) == 1);
	CHECK(!optimizeTextures(bt, 20, 20));
}

static void testPlacement()
{
	using namespace MapEditorCore;
	Map map;
	map.heightMap = HeightMap{};
	HeightMap &hm = *map.heightMap;
	hm.width = 60;
	hm.height = 60;
	hm.borderSize = 5;
	hm.heights = std::vector<std::uint8_t>(3600, 32);
	// A steep wall along x = 400 (cell 45) and a lake in the south west corner.
	for (int y = 0; y < 60; ++y)
		for (int x = 44; x < 60; ++x)
			hm.heights[static_cast<std::size_t>(y * 60 + x)] = static_cast<std::uint8_t>(std::min(255, 32 + (x - 43) * 40));
	map.polygons = std::vector<PolygonArea>{};
	PolygonArea lake;
	lake.isWater = true;
	lake.points = {{0, 0, 30}, {100, 0, 30}, {100, 100, 30}, {0, 100, 30}};
	map.polygons->push_back(lake);
	map.objects = std::vector<MapObject>{};

	CHECK(underWater(map, 50.0f, 50.0f) && !underWater(map, 150.0f, 150.0f));
	CHECK(tooSteepForTree(map, 395.0f, 200.0f) && !tooSteepForTree(map, 200.0f, 200.0f));

	GroveOptions options;
	CHECK(growGrove(map, 200.0f, 200.0f, options, *std::make_unique<std::mt19937>(1)).empty());
	options.types = {{"TreePine", 3}, {"TreeOak", 1}, {"", 0}};
	std::mt19937 random(7);
	const std::vector<MapObject> grove = growGrove(map, 200.0f, 200.0f, options, random);
	CHECK(grove.size() >= 3 && grove.size() <= 11);
	bool near = true, known = true;
	for (const MapObject &t : grove)
	{
		near = near && std::hypot(t.x - 200.0f, t.y - 200.0f) <= 3.0f * 60.0f + 1.0f;
		known = known && (t.templateName == "TreePine" || t.templateName == "TreeOak") && t.properties.getAscii("originalOwner") == "team";
	}
	CHECK(near && known && grove[0].x == 200.0f && grove[0].y == 200.0f);
	std::mt19937 again(7);
	CHECK(growGrove(map, 200.0f, 200.0f, options, again).size() == grove.size());

	// A rectangle over the lake, the wall and dry flat ground: trees only on the dry flat part.
	options.boxCount = 200;
	std::mt19937 box(3);
	const std::vector<MapObject> scattered = scatterGrove(map, 0.0f, 0.0f, 450.0f, 300.0f, options, box);
	bool dryAndFlat = true;
	for (const MapObject &t : scattered)
		dryAndFlat = dryAndFlat && !underWater(map, t.x, t.y) && !tooSteepForTree(map, t.x, t.y) && t.x <= 450.0f && t.y <= 300.0f;
	CHECK(scattered.size() > 100 && scattered.size() < 200 && dryAndFlat);
	options.allowInWater = true;
	options.allowOnSlopes = true;
	std::mt19937 box2(3);
	CHECK(scatterGrove(map, 0.0f, 0.0f, 450.0f, 300.0f, options, box2).size() == 200);

	// Fences: one piece every spacing along the line, facing along it.
	const std::vector<MapObject> fence = fencePieces("FenceWood", "team", 100.0f, 100.0f, 100.0f, 200.0f, 27.35f, 5.0f);
	CHECK(fence.size() == 3 && std::abs(fence[0].y - 105.0f) < 0.01f && std::abs(fence[1].y - 132.35f) < 0.01f
		&& std::abs(fence[2].angle - 1.5707963f) < 0.001f && fence[2].x == 100.0f);
	CHECK(fencePieces("FenceWood", "team", 0.0f, 0.0f, 0.0f, 0.0f, 20.0f, 0.0f).size() == 1);
	const std::vector<MapObject> stretched = fencePieces("FenceWood", "team", 0.0f, 0.0f, 100.0f, 0.0f, 30.0f, 0.0f, 4);
	CHECK(stretched.size() == 4 && std::abs(stretched[3].x - 75.0f) < 0.01f);
	CHECK(fencePieces("FenceWood", "team", 0.0f, 0.0f, 100000.0f, 0.0f, 1.0f, 0.0f).size() == 200);
	CHECK(fencePieces("", "team", 0.0f, 0.0f, 100.0f, 0.0f, 10.0f, 0.0f).empty());

	// Scorch marks and picking them.
	const MapObject scorch = scorchMark(300.0f, 300.0f, 9, 25.0f);
	CHECK(isScorch(scorch) && scorch.properties.getInt("scorchType") == 4 && scorch.properties.getReal("objectRadius") == 25.0f);
	map.objects->push_back(fence[0]);
	map.objects->push_back(scorch);
	map.objects->push_back(scorchMark(310.0f, 300.0f, 1, 20.0f));
	CHECK(scorchAt(map, 311.0f, 300.0f) == 2 && scorchAt(map, 304.0f, 300.0f) == 1 && scorchAt(map, 340.0f, 300.0f) == -1);
	CHECK(!isScorch(fence[0]));
}

static void testSelectionTools()
{
	using namespace MapEditorCore;
	CHECK(pickAllowed(2, 0) && pickAllowed(2, 2) && !pickAllowed(2, 3) && pickAllowed(0, 3));
	CHECK(pickAllowed(9, 0) && pickAllowed(9, 9) && !pickAllowed(9, 2) && !pickAllowed(13, 2) && pickAllowed(13, 13));

	Map map;
	map.objects = std::vector<MapObject>{};
	const auto add = [&map](const char *name, float x, float y, std::int32_t flags = 0, const char *owner = "team") {
		MapObject o;
		o.templateName = name;
		o.x = x;
		o.y = y;
		o.flags = flags;
		if (owner)
			o.properties.set("originalOwner", PropertyValue(std::string(owner)));
		map.objects->push_back(o);
	};
	add("Tree", 10.0f, 10.0f);						// 0
	add("Tree", 10.02f, 10.0f);						// 1 duplicate of 0
	add("Tree", 30.0f, 10.0f);						// 2
	add("Rock", 10.0f, 10.0f);						// 3 other template, same place
	add(WAYPOINT_TEMPLATE, 50.0f, 50.0f);			// 4
	add(WAYPOINT_TEMPLATE, 50.0f, 50.0f);			// 5 waypoints are never duplicates
	(*map.objects)[4].properties.set("waypointID", PropertyValue(std::int32_t{1}));
	(*map.objects)[5].properties.set("waypointID", PropertyValue(std::int32_t{2}));
	add("Road", 0.0f, 0.0f, 0x02);					// 6, 7 road segment
	add("Road", 100.0f, 0.0f, 0x04);
	add("Road", 0.0f, 0.0f, 0x02);					// 8, 9 the same segment again
	add("Road", 100.0f, 0.0f, 0x04);
	add("Road", 0.0f, 0.0f, 0x02);					// 10, 11 same start, other end
	add("Road", 0.0f, 100.0f, 0x04);
	add("Tank", 70.0f, 70.0f, 0, "teamPlayer1");	// 12 team the map lacks
	add("Tank", 80.0f, 70.0f, 0, nullptr);			// 13 no team at all

	CHECK(findDuplicates(map) == std::vector<std::int32_t>({1, 8, 9}));
	CHECK(findSimilar(map, {6, 2}) == std::vector<std::int32_t>({0, 1, 2}));
	CHECK(findSimilar(map, {}).empty() && findSimilar(map, {7}).empty());

	std::set<std::string> teams;
	map.sides = Sides{};
	PropertyMap neutral;
	neutral.set("teamName", PropertyValue(std::string("team")));
	map.sides->teams.push_back(neutral);
	CHECK(findInvalidTeams(map, teams) == std::vector<std::int32_t>({12, 13}) && teams == std::set<std::string>({"", "teamPlayer1"}));
	PropertyMap player;
	player.set("teamName", PropertyValue(std::string("teamPlayer1")));
	map.sides->teams.push_back(player);
	CHECK(findInvalidTeams(map, teams) == std::vector<std::int32_t>({13}) && teams == std::set<std::string>({""}));

	const SortingOf sorting = [](const std::string &name) { return name == "Tank" ? 3 : (name == "Tree" ? 4 : 0); };
	CHECK(objectSorting((*map.objects)[12], sorting) == 3 && objectSorting((*map.objects)[4], sorting) == 13
		&& objectSorting((*map.objects)[6], sorting) == 12 && objectSorting((*map.objects)[3], sorting) == 0);
}

static void testLayersAndGroups()
{
	using namespace MapEditorCore;
	Map map;
	map.objects = std::vector<MapObject>(5);
	(*map.objects)[1].properties.set("objectLayer", PropertyValue(std::string("Base")));
	(*map.objects)[2].properties.set("objectLayer", PropertyValue(std::string("Base")));
	(*map.objects)[3].properties.set("objectLayer", PropertyValue(std::string("")));
	map.polygons = std::vector<PolygonArea>(2);
	(*map.polygons)[1].layer = "Base";
	const Map original = map;

	std::vector<LayerInfo> layers = mapLayers(map);
	CHECK(layers.size() == 2 && layers[0].name.empty() && layers[0].objects == 3 && layers[0].areas == 1
		&& layers[1].name == "Base" && layers[1].objects == 2 && layers[1].areas == 1);
	CHECK(layerObjects(map, "Base") == std::vector<std::int32_t>({1, 2}));

	// Moving: an object without the property gets it, one moved back to default keeps it empty.
	History h;
	auto move = moveToLayer(map, {0, 1, 1}, {0}, "Trees");
	CHECK(move != nullptr && move->changes().rebuild && move->changes().areas && move->changes().objects.size() == 2);
	h.execute(std::move(move), map);
	CHECK(objectLayer((*map.objects)[0]) == "Trees" && objectLayer((*map.objects)[1]) == "Trees" && (*map.polygons)[0].layer == "Trees");
	CHECK(moveToLayer(map, {0}, {}, "Trees") == nullptr);
	h.undo(map);
	CHECK(*map.objects == *original.objects && *map.polygons == *original.polygons);
	auto back = moveToLayer(map, {1, 4}, {}, "");
	CHECK(back != nullptr);
	h.execute(std::move(back), map);
	CHECK((*map.objects)[1].properties.getAscii("objectLayer") == "" && !(*map.objects)[4].properties.find("objectLayer"));
	h.undo(map);

	// Rename and delete are merges.
	h.execute(mergeLayer(map, "Base", "Walls", "Rename layer"), map);
	CHECK(layerObjects(map, "Walls") == std::vector<std::int32_t>({1, 2}) && (*map.polygons)[1].layer == "Walls" && layerObjects(map, "Base").empty());
	h.execute(mergeLayer(map, "Walls", "", "Delete layer"), map);
	CHECK(mapLayers(map).size() == 1 && mapLayers(map)[0].objects == 5);
	CHECK(mergeLayer(map, "Nothing", "", "Delete layer") == nullptr && mergeLayer(map, "", "", "Delete layer") == nullptr);
	h.undo(map);
	h.undo(map);
	CHECK(*map.objects == *original.objects && *map.polygons == *original.polygons);

	// Groups.
	CHECK(groupObjects(map, {2}) == nullptr);
	auto group = groupObjects(map, {1, 3});
	CHECK(group != nullptr);
	h.execute(std::move(group), map);
	CHECK(objectGroup((*map.objects)[1]) == 1 && objectGroup((*map.objects)[3]) == 1 && objectGroup((*map.objects)[0]) == 0);
	h.execute(groupObjects(map, {0, 4}), map);
	CHECK(objectGroup((*map.objects)[4]) == 2);
	CHECK(withGroups(map, {3}) == std::vector<std::int32_t>({1, 3}) && withGroups(map, {2}) == std::vector<std::int32_t>({2})
		&& withGroups(map, {4, 1}) == std::vector<std::int32_t>({0, 1, 3, 4}));
	h.execute(ungroupObjects(map, {1, 3, 2}), map);
	CHECK(objectGroup((*map.objects)[1]) == 0 && ungroupObjects(map, {1, 2}) == nullptr && withGroups(map, {3}) == std::vector<std::int32_t>({3}));
	h.undo(map);
	h.undo(map);
	h.undo(map);
	CHECK(*map.objects == *original.objects && *map.polygons == *original.polygons);
}

static void testMapSettings()
{
	using namespace MapEditorCore;
	// Directions: the old editor's defaults (afternoon sun -0.96, 0.05, -0.29) round-trip through the angles.
	float azimuth = 0.0f, elevation = 0.0f;
	lightAngles({-0.96f, 0.05f, -0.29f}, azimuth, elevation);
	const std::array<float, 3> back = lightDirection(azimuth, elevation);
	const float length = std::sqrt(0.96f * 0.96f + 0.05f * 0.05f + 0.29f * 0.29f);
	CHECK(std::abs(back[0] + 0.96f / length) < 0.001f && std::abs(back[1] - 0.05f / length) < 0.001f && std::abs(back[2] + 0.29f / length) < 0.001f);
	const std::array<float, 3> down = lightDirection(0.0f, 90.0f);
	CHECK(std::abs(down[2] + 1.0f) < 0.001f && std::abs(down[0]) < 0.001f);

	Map map;
	map.heightMap = HeightMap{};
	map.heightMap->boundaries = {{100, 80}};
	map.worldInfo = PropertyMap{};
	map.worldInfo->set("mapName", PropertyValue(std::string("Old")));
	map.lighting = GlobalLighting{};
	map.lighting->timeOfDay = 2;
	const MapSettings before = mapSettings(map);
	CHECK(settingsStep(map, before, "Map name") == nullptr);

	// Edits in the same field fold into one step; undo puts everything back.
	History h;
	map.worldInfo->set("mapName", PropertyValue(std::string("New")));
	auto step = settingsStep(map, before, "Map name");
	CHECK(step != nullptr && step->changes().settings && !step->changes().boundaries);
	h.record(std::move(step));
	const MapSettings middle = mapSettings(map);
	map.worldInfo->set("mapName", PropertyValue(std::string("Newer")));
	h.record(settingsStep(map, middle, "Map name"));
	const MapSettings named = mapSettings(map);
	map.lighting->timeOfDay = 4;
	map.heightMap->boundaries.push_back({50, 50});
	auto both = settingsStep(map, named, "Time of day");
	CHECK(both->changes().settings && both->changes().boundaries);
	h.record(std::move(both));
	h.undo(map);
	CHECK(map.lighting->timeOfDay == 2 && map.heightMap->boundaries.size() == 1 && map.worldInfo->getAscii("mapName") == "Newer");
	h.undo(map);
	CHECK(mapSettings(map) == before && !h.canUndo());

	// Picking a playable area: corner, top edge, right edge, nothing.
	const std::vector<Point2> areas = {{100, 80}, {40, 40}};
	CHECK(boundaryAt(areas, 1003.0f, 798.0f, 20.0f).index == 0 && boundaryAt(areas, 1003.0f, 798.0f, 20.0f).part == BoundaryHit::Part::Corner);
	CHECK(boundaryAt(areas, 500.0f, 805.0f, 20.0f).part == BoundaryHit::Part::Top && boundaryAt(areas, 995.0f, 300.0f, 20.0f).part == BoundaryHit::Part::Right);
	CHECK(boundaryAt(areas, 402.0f, 399.0f, 20.0f).index == 1 && boundaryAt(areas, 200.0f, 600.0f, 20.0f).index == -1);
}

static void testMapPreview()
{
	using namespace MapEditorCore;
	Map map;
	map.heightMap = HeightMap{};
	HeightMap &hm = *map.heightMap;
	hm.width = 70;
	hm.height = 70;
	hm.borderSize = 3;
	hm.heights = std::vector<std::uint8_t>(4900, 40);
	for (int y = 0; y < 70; ++y)
		for (int x = 50; x < 70; ++x)
			hm.heights[static_cast<std::size_t>(y * 70 + x)] = 120;	// a plateau in the east
	map.blendTiles = BlendTiles{};
	BlendTiles &bt = *map.blendTiles;
	bt.tiles = std::vector<std::int16_t>(4900, 0);
	bt.blends = std::vector<std::int16_t>(4900, 0);
	bt.extraBlends = std::vector<std::int16_t>(4900, 0);
	bt.cliffInfoIndices = std::vector<std::int16_t>(4900, 0);
	addTextureClass(bt, "Grass", 4, 2);
	map.polygons = std::vector<PolygonArea>{};
	PolygonArea lake;
	lake.isWater = true;
	lake.points = {{0, 0, 60}, {150, 0, 60}, {150, 150, 60}, {0, 150, 60}};
	map.polygons->push_back(lake);

	const std::vector<std::uint32_t> pixels = mapPreview(map, {{0.2f, 0.6f, 0.2f}});
	CHECK(pixels.size() == 128u * 128u);
	const auto at = [&pixels](int x, int y) { return pixels[static_cast<std::size_t>(y * 128 + x)]; };
	const auto channel = [](std::uint32_t p, int shift) { return static_cast<int>((p >> shift) & 0xFF); };
	const std::uint32_t grass = at(40, 100), plateau = at(120, 100), water = at(5, 5);
	CHECK((grass >> 24) == 0xFF && channel(grass, 8) > channel(grass, 16) && channel(grass, 8) > channel(grass, 0));
	CHECK(channel(plateau, 8) > channel(grass, 8));	// higher is lighter
	CHECK(channel(water, 0) > channel(water, 16) && channel(water, 0) > 200);	// the lake in the south west is blue

	const std::vector<std::uint8_t> tga = previewTga(pixels);
	CHECK(tga.size() == 18u + 128u * 128u * 4u && tga[2] == 2 && tga[12] == 128 && tga[16] == 32 && tga[17] == 8);
	const std::size_t first = 18u + (100u * 128u + 40u) * 4u;
	CHECK(tga[first] == channel(grass, 0) && tga[first + 1] == channel(grass, 8) && tga[first + 2] == channel(grass, 16) && tga[first + 3] == 255);
}

static void testMapSize()
{
	using namespace MapEditorCore;
	NewMapOptions options;
	options.width = 100;
	options.height = 80;
	options.border = 10;
	options.initialHeight = 20;
	options.texture = "Grass";
	options.textureTiles = 16;
	options.textureWidth = 4;
	options.waterHeight = 7.0f;
	NewMapOptions bad = options;
	bad.width = MIN_MAP_CELLS - 1;
	CHECK(!newMap(bad).has_value());
	bad = options;
	bad.textureTiles = 0;
	CHECK(!newMap(bad).has_value());

	std::optional<Map> created = newMap(options);
	CHECK(created.has_value());
	if (!created)
		return;
	CHECK(created->heightMap->width == 120 && created->heightMap->height == 100 && created->heightMap->borderSize == 10);
	CHECK(created->heightMap->boundaries.size() == 1 && created->heightMap->boundaries[0] == (Point2{100, 80}));
	CHECK(created->heightMap->heights[5000] == 20);
	const BlendTiles &nbt = *created->blendTiles;
	CHECK(nbt.textureClasses.size() == 1 && nbt.numBitmapTiles == 16 && nbt.tiles.size() == 12000u);
	CHECK(nbt.tiles[0] == tileForClass(nbt.textureClasses[0], 0, 0) && nbt.tiles[121] == tileForClass(nbt.textureClasses[0], 1, 1));
	CHECK(nbt.cliffState.size() == 15u * 100u);
	CHECK(created->sides->sides.size() == 1 && created->sides->teams.size() == 1);
	CHECK(created->sides->sides[0].properties.getAscii("playerName") == std::string());
	CHECK(created->sides->teams[0].getAscii("teamName") == std::string("team") && created->sides->teams[0].getBool("teamIsSingleton") == true);
	CHECK(created->polygons->size() == 1 && (*created->polygons)[0].isWater && (*created->polygons)[0].points[0] == (std::array<std::int32_t, 3>{-100, -100, 7})
		&& (*created->polygons)[0].points[2] == (std::array<std::int32_t, 3>{1100, 900, 7}));
	{
		const WriteResult written = writeMap(*created);
		CHECK(written.ok());
		const ReadResult reread = readMap(written.bytes);
		CHECK(reread.ok() && sameContent(reread.map, *created));
	}

	// Resize: wider, anchored right and bottom, smaller border.
	Map map = *created;
	HeightMap &hm = *map.heightMap;
	hm.heights[static_cast<std::size_t>(10 * 120 + 10)] = 200;			// world 0, 0
	setCliffFlag(map.blendTiles->cliffState, 120, 20, 20, true);
	MapObject tree;
	tree.x = 50.0f;
	tree.y = 60.0f;
	tree.templateName = "Tree";
	map.objects->push_back(tree);
	BuildListEntry building;
	building.x = 30.0f;
	building.y = 40.0f;
	map.sides->sides[0].buildList.push_back(building);
	Script script;
	ScriptCall call;
	ScriptParameter where;
	where.type = PARAMETER_COORD3D;
	where.coord = {70.0f, 80.0f, 0.0f};
	call.parameters.push_back(where);
	script.actions.push_back(call);
	map.sides->sides[0].scripts.scripts.push_back(script);
	const Map before = map;

	ResizeOptions resize;
	resize.width = 120;
	resize.height = 80;
	resize.border = 5;
	resize.x = Anchor::End;
	resize.y = Anchor::Start;
	const ResizeOffset offset = resizeOffset(hm, resize);
	CHECK(offset.x == 200.0f && offset.y == 0.0f);
	auto step = resizeMap(map, resize);
	CHECK(step != nullptr && step->changes().size);
	if (!step)
		return;
	History history;
	history.execute(std::move(step), map);
	const HeightMap &after = *map.heightMap;
	CHECK(after.width == 130 && after.height == 90 && after.borderSize == 5);
	CHECK(after.boundaries[0] == (Point2{120, 80}));
	CHECK(after.heights[static_cast<std::size_t>(5 * 130 + 25)] == 200);	// old cell 10, 10 is now 25, 5
	CHECK(after.heights[0] == 20);
	const BlendTiles &bt = *map.blendTiles;
	CHECK(bt.tiles.size() == 130u * 90u && bt.cliffState.size() == cliffRowBytes(130) * 90u);
	CHECK(classOfTile(bt, bt.tiles[0]) == 0);
	// New cell 15, 0 is old cell 0, 5; cell 14 lies beyond the old map and continues the texture's pattern.
	CHECK(bt.tiles[15] == tileForClass(bt.textureClasses[0], 0, 5) && bt.tiles[14] == tileForClass(bt.textureClasses[0], 7, 5));
	CHECK(cliffFlag(bt.cliffState, 130, 35, 15));							// kept from old cell 20, 20
	CHECK(map.objects->back().x == 250.0f && map.objects->back().y == 60.0f);
	CHECK(map.sides->sides[0].buildList[0].x == 230.0f);
	CHECK(map.sides->sides[0].scripts.scripts[0].actions[0].parameters[0].coord[0] == 270.0f);
	CHECK((*map.polygons)[0].points[0][0] == 100);
	history.undo(map);
	CHECK(map.heightMap == before.heightMap && map.blendTiles == before.blendTiles && map.objects == before.objects
		&& map.sides == before.sides && map.polygons == before.polygons);

	// The same size is no step; shrinking to the centre leaves the tree outside.
	resize = ResizeOptions{100, 80, 10, Anchor::Centre, Anchor::Centre};
	CHECK(resizeMap(map, resize) == nullptr);
	resize = ResizeOptions{16, 16, 5, Anchor::Centre, Anchor::Centre};
	CHECK(objectsOutsideAfterResize(map, resize) == 1);
	resize.width = MAX_MAP_CELLS + 1;
	CHECK(resizeMap(map, resize) == nullptr);
}

static void testSidesCommands()
{
	using namespace MapEditorCore;
	Map map;
	map.sides = Sides{};
	map.objects = std::vector<MapObject>{};
	Sides &sides = *map.sides;
	CHECK(validateSides(sides) && sides.sides.size() == 1 && sides.teams.size() == 1 && teamName(sides.teams[0]) == "team");
	CHECK(!validateSides(sides));
	const EditSides::State start = sidesState(map);

	CHECK(validPlayerName("Bad name!") == "Bad_name_");
	const int usa = addPlayer(sides, "USA", u"America", "FactionAmerica", true);
	const int gla = addPlayer(sides, freePlayerName(sides), u"GLA", "FactionGLA", false);
	CHECK(usa == 1 && gla == 2 && playerName(sides.sides[2]) == "player0001");
	CHECK(addPlayer(sides, "USA", u"again", "FactionAmerica", true) == -1);
	CHECK(findTeam(sides, "teamUSA") >= 0 && isDefaultTeam(sides, sides.teams[static_cast<std::size_t>(findTeam(sides, "teamUSA"))]));

	setRelation(sides, usa, gla, Relation::Enemy);
	CHECK(relation(sides, usa, gla) == Relation::Enemy && relation(sides, gla, usa) == Relation::Neutral);
	setRelation(sides, usa, gla, Relation::Ally);
	CHECK(relation(sides, usa, gla) == Relation::Ally && sides.sides[1].properties.getAscii("playerEnemies") == std::string());

	const int t = addTeam(sides, "USA");
	CHECK(teamName(sides.teams[static_cast<std::size_t>(t)]) == "team0001" && teamOwner(sides.teams[static_cast<std::size_t>(t)]) == "USA"
		&& sides.teams[static_cast<std::size_t>(t)].getBool("teamIsSingleton") == false);
	const int c = copyTeam(sides, t);
	CHECK(c == t + 1 && teamName(sides.teams[static_cast<std::size_t>(c)]) == "team0001.01");
	CHECK(copyTeam(sides, findTeam(sides, "teamUSA")) == -1 && !removeTeam(sides, findTeam(sides, "teamUSA")));
	CHECK(moveTeam(sides, c, -1) && teamName(sides.teams[static_cast<std::size_t>(t)]) == "team0001.01");
	CHECK(!moveTeam(sides, t, -1) || !isDefaultTeam(sides, sides.teams[static_cast<std::size_t>(t - 1)]));

	MapObject tank;
	tank.templateName = "Tank";
	tank.properties.set("originalOwner", std::string("teamUSA"));
	map.objects->push_back(tank);
	MapObject scout = tank;
	scout.properties.set("originalOwner", std::string("team0001.01"));
	map.objects->push_back(scout);
	CHECK(playerObjectCount(map, usa) == 2 && teamObjects(map, "teamUSA") == std::vector<std::int32_t>{0});

	const EditSides::State beforeRename = sidesState(map);
	CHECK(renamePlayer(map, usa, "Yankees"));
	CHECK(findTeam(sides, "teamYankees") >= 0 && findTeam(sides, "teamUSA") < 0 && (*map.objects)[0].properties.getAscii("originalOwner") == std::string("teamYankees"));
	CHECK(teamOwner(sides.teams[static_cast<std::size_t>(findTeam(sides, "team0001"))]) == "Yankees");
	CHECK(!renamePlayer(map, 0, "Neutral") && !renamePlayer(map, gla, "Yankees"));
	CHECK(renameTeam(map, findTeam(sides, "team0001.01"), "Scouts") && (*map.objects)[1].properties.getAscii("originalOwner") == std::string("Scouts"));
	CHECK(!renameTeam(map, findTeam(sides, "Scouts"), "team player") && !renameTeam(map, findTeam(sides, "Scouts"), "teamplayer0001"));
	auto step = sidesStep(map, beforeRename, "Rename player");
	CHECK(step != nullptr && step->changes().sides);
	History history;
	history.record(std::move(step));
	history.undo(map);
	CHECK(map.sides == beforeRename.sides && map.objects == beforeRename.objects);
	history.redo(map);
	CHECK(findPlayer(*map.sides, "Yankees") == usa);

	// Removing a player takes its teams; the neutral player stays.
	CHECK(!removePlayer(*map.sides, 0));
	CHECK(removePlayer(*map.sides, usa) && findTeam(*map.sides, "teamYankees") < 0 && findTeam(*map.sides, "team0001") < 0);
	CHECK(addSkirmishPlayers(*map.sides, false) == 4 && addSkirmishPlayers(*map.sides, false) == 0);
	CHECK(findPlayer(*map.sides, "SkirmishChina") >= 0 && findTeam(*map.sides, "teamPlyrCivilian") >= 0);
	CHECK(addSkirmishPlayers(*map.sides, true) == 9 && static_cast<int>(map.sides->sides.size()) == 15);
	CHECK(addPlayer(*map.sides, "OneTooMany", u"x", "FactionGLA", true) == -1);

	// Typing in a field is one step.
	History typing;
	EditSides::State s0 = sidesState(map);
	map.sides->sides[1].properties.set("playerDisplayName", std::u16string(u"C"));
	typing.record(sidesStep(map, s0, "Display name", true));
	EditSides::State s1 = sidesState(map);
	map.sides->sides[1].properties.set("playerDisplayName", std::u16string(u"Ci"));
	typing.record(sidesStep(map, s1, "Display name", true));
	typing.undo(map);
	CHECK(map.sides == s0.sides && !typing.canUndo());
	CHECK(sidesStep(map, sidesState(map), "Nothing") == nullptr);
	(void)start;
}

static void testScriptCommands()
{
	using namespace MapEditorCore;
	ScriptTemplates templates;
	ScriptTemplate never;
	never.internalName = "CONDITION_FALSE";
	never.uiName = "Scripting_/False";
	never.uiStrings = {"False."};
	ScriptTemplate always = never;
	always.internalName = "CONDITION_TRUE";
	always.uiName = "Scripting_/True";
	always.uiStrings = {"True."};
	ScriptTemplate counter;
	counter.internalName = "COUNTER";
	counter.uiName = "Scripting_/Counter/Compare";
	counter.parameterTypes = {Param::COUNTER, Param::COMPARISON, Param::INT};
	counter.uiStrings = {"Counter ", " is ", ""};
	templates.conditions = {never, always, counter};
	ScriptTemplate noop;
	noop.internalName = "NO_OP";
	noop.uiName = "Scripting_/Null operation";
	noop.uiStrings = {"Null operation."};
	ScriptTemplate setCounter;
	setCounter.internalName = "SET_COUNTER";
	setCounter.uiName = "Scripting_/Counter/Set";
	setCounter.parameterTypes = {Param::COUNTER, Param::INT};
	setCounter.uiStrings = {"Set ", " to ", "."};
	ScriptTemplate moveTeam;
	moveTeam.internalName = "MOVE_TEAM_TO";
	moveTeam.uiName = "Team_/Move";
	moveTeam.parameterTypes = {Param::TEAM, Param::WAYPOINT};
	moveTeam.uiStrings = {"Move ", " to ", "."};
	templates.actions = {noop, setCounter, moveTeam};

	CHECK(valueKind(Param::KIND_OF_PARAM) == ValueKind::Int && valueKind(Param::ANGLE) == ValueKind::Real
		&& valueKind(Param::COORD3D) == ValueKind::Coord && valueKind(Param::TEAM) == ValueKind::Text);
	MapModel::ScriptCall call = makeCall(templates, false, 2);
	CHECK(call.internalName == "COUNTER" && call.parameters.size() == 3 && call.parameters[1].type == Param::COMPARISON);
	call.parameters[0].text = "Waves";
	call.parameters[1].intValue = 2;
	call.parameters[2].intValue = 5;
	const ValueNames names = [](std::int32_t type, std::int32_t value) {
		return type == Param::COMPARISON && value == 2 ? std::string("Equal To") : std::string();
	};
	CHECK(callText(call, callTemplate(templates, false, call), names) == "Counter 'Waves' is Equal To 5 ");
	MapModel::ScriptParameter angle;
	angle.type = Param::ANGLE;
	angle.realValue = 1.5707963f;
	CHECK(parameterText(angle, names) == "90.00 degrees");
	MapModel::ScriptCall legacy;
	legacy.type = 1;
	CHECK(callTemplate(templates, false, legacy) == &templates.conditions[1]);
	legacy.internalName = "GONE";
	CHECK(callTemplate(templates, false, legacy) == nullptr && callText(legacy, nullptr, names) == "[GONE]");
	MapModel::ScriptCall moved = makeCall(templates, true, 1);
	moved.parameters[0].text = "Waves";
	setCallTemplate(moved, templates, true, 2);
	CHECK(moved.internalName == "MOVE_TEAM_TO" && moved.parameters[0].type == Param::TEAM && moved.parameters[0].text.empty());

	Map map;
	map.sides = Sides{};
	map.sides->sides.resize(2);
	Sides &sides = *map.sides;
	const std::vector<ScriptList> start = scriptLists(map);
	const Script first = newScript(templates, freeScriptName(sides, "Script"));
	CHECK(first.name == "Script 1" && first.conditions.size() == 1 && first.conditions[0].conditions[0].internalName == "CONDITION_TRUE"
		&& first.actions.size() == 1 && first.actions[0].internalName == "NO_OP");
	const ScriptPath a = insertScript(sides, {1, -1, -1}, first);
	CHECK(a == (ScriptPath{1, -1, 0}) && freeScriptName(sides, "Script") == "Script 2");
	ScriptGroup group;
	group.name = "Wave control";
	const ScriptPath g = insertGroup(sides, {1, -1, -1}, group);
	const ScriptPath b = insertScript(sides, g, newScript(templates, freeScriptName(sides, "Script")));
	CHECK(b == (ScriptPath{1, 0, 0}) && scriptAt(sides, b)->name == "Script 2");
	const ScriptPath c = copyScript(sides, b);
	CHECK(c == (ScriptPath{1, 0, 1}) && scriptAt(sides, c)->name == "Script 2 C");
	scriptAt(sides, c)->isSubroutine = true;
	scriptAt(sides, c)->actions.push_back(makeCall(templates, true, 1));
	scriptAt(sides, c)->actions.back().parameters[0].text = "Waves";
	scriptAt(sides, a)->conditions[0].conditions.push_back(call);
	CHECK(usedNames(sides, templates, Param::COUNTER) == std::vector<std::string>{"Waves"});
	CHECK(usedNames(sides, templates, Param::COUNTER, {"MOVE_TEAM_TO"}).empty() && usedNames(sides, templates, Param::COUNTER, {"SET_COUNTER"}).size() == 1);
	CHECK(scriptNames(sides, true) == std::vector<std::string>{"Script 2 C"} && scriptNames(sides, false).size() == 4);

	// Drag and drop: a script into another player, a group before another.
	const ScriptPath moved1 = moveScriptOrGroup(sides, c, {0, -1, -1});
	CHECK(moved1 == (ScriptPath{0, -1, 0}) && sides.sides[1].scripts.groups[0].scripts.size() == 1 && sides.sides[0].scripts.scripts[0].name == "Script 2 C");
	ScriptGroup second;
	second.name = "Second";
	insertGroup(sides, {1, 0, -1}, second);
	const ScriptPath moved2 = moveScriptOrGroup(sides, {1, 1, -1}, {1, 0, -1});
	CHECK(moved2 == (ScriptPath{1, 0, -1}) && sides.sides[1].scripts.groups[0].name == "Second");
	const ScriptPath moved3 = moveScriptOrGroup(sides, {1, -1, 0}, {1, 1, 0});
	CHECK(moved3 == (ScriptPath{1, 1, 0}) && sides.sides[1].scripts.scripts.empty() && sides.sides[1].scripts.groups[1].scripts.size() == 2);
	CHECK(removeScriptOrGroup(sides, {1, 0, -1}) && sides.sides[1].scripts.groups.size() == 1 && !removeScriptOrGroup(sides, {1, 5, -1}));

	// One step with undo; typing folds into one.
	History history;
	history.record(scriptsStep(map, start, "Add script"));
	CHECK(history.canUndo());
	history.undo(map);
	CHECK(scriptLists(map) == start);
	history.redo(map);
	std::vector<ScriptList> s0 = scriptLists(map);
	sides.sides[0].scripts.scripts[0].comment = "W";
	auto t1 = scriptsStep(map, s0, "Change comment", true);
	CHECK(t1 != nullptr && t1->changes().scripts);
	history.record(std::move(t1));
	std::vector<ScriptList> s1 = scriptLists(map);
	sides.sides[0].scripts.scripts[0].comment = "Wa";
	history.record(scriptsStep(map, s1, "Change comment", true));
	history.undo(map);
	CHECK(sides.sides[0].scripts.scripts[0].comment.empty() && history.undoName() == "Add script");
	CHECK(scriptsStep(map, scriptLists(map), "Nothing") == nullptr);
}

static void testScriptExchange()
{
	using namespace MapEditorCore;
	ScriptTemplates templates;
	ScriptTemplate always;
	always.internalName = "CONDITION_TRUE";
	always.uiName = "Scripting_/True";
	ScriptTemplate flag;
	flag.internalName = "FLAG";
	flag.uiName = "Scripting_/Flag";
	flag.parameterTypes = {Param::FLAG, Param::BOOLEAN};
	templates.conditions = {always, flag};
	ScriptTemplate noop;
	noop.internalName = "NO_OP";
	noop.uiName = "Scripting_/Null";
	ScriptTemplate setFlag;
	setFlag.internalName = "SET_FLAG";
	setFlag.uiName = "Scripting_/Flags/Set";
	setFlag.parameterTypes = {Param::FLAG, Param::BOOLEAN};
	ScriptTemplate move;
	move.internalName = "MOVE_TEAM_TO";
	move.uiName = "Team_/Move";
	move.parameterTypes = {Param::TEAM, Param::WAYPOINT};
	ScriptTemplate spawn;
	spawn.internalName = "CREATE_OBJECT";
	spawn.uiName = "Unit_/Create";
	spawn.parameterTypes = {Param::OBJECT_TYPE, Param::TRIGGER_AREA};
	templates.actions = {noop, setFlag, move, spawn};

	// A map with two players, a team, waypoints A-B linked and C alone, an area, a unit of the team.
	Map map;
	map.sides = Sides{};
	validateSides(*map.sides);
	addPlayer(*map.sides, "Red", u"Red", "FactionChina", true);
	const int attackers = addTeam(*map.sides, "Red");
	map.sides->teams[static_cast<std::size_t>(attackers)].set("teamName", std::string("Attackers"));
	map.objects = std::vector<MapObject>{};
	auto waypoint = [&map](const char *name, int id, float x) {
		MapObject w;
		w.x = x;
		w.templateName = "*Waypoints/Waypoint";
		w.properties.set("waypointID", std::int32_t(id));
		w.properties.set("waypointName", std::string(name));
		map.objects->push_back(w);
	};
	waypoint("A", 1, 10.0f);
	waypoint("B", 2, 20.0f);
	waypoint("C", 3, 30.0f);
	MapObject tank;
	tank.x = 50.0f;
	tank.templateName = "Tank";
	tank.properties.set("originalOwner", std::string("Attackers"));
	map.objects->push_back(tank);
	map.waypointLinks = std::vector<WaypointLink>{{1, 2}};
	PolygonArea area;
	area.name = "Drop";
	area.id = 7;
	area.points = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}};
	PolygonArea lake = area;
	lake.name = "Lake";
	lake.isWater = true;
	map.polygons = std::vector<PolygonArea>{area, lake};
	map.heightMap = HeightMap{};
	map.heightMap->boundaries = {{10, 10}};

	Script attack = newScript(templates, "Attack");
	MapModel::ScriptCall go = makeCall(templates, true, 2);
	go.parameters[0].text = "Attackers";
	go.parameters[1].text = "A";
	attack.actions.push_back(go);
	MapModel::ScriptCall ready = makeCall(templates, false, 1);
	ready.parameters[0].text = "Ready";
	attack.conditions[0].conditions.push_back(ready);
	MapModel::ScriptCall create = makeCall(templates, true, 3);
	create.parameters[0].text = "GC_Tank";
	create.parameters[1].text = "Drop";
	attack.falseActions.push_back(create);
	const int red = findPlayer(*map.sides, "Red");
	map.sides->sides[static_cast<std::size_t>(red)].scripts.scripts.push_back(attack);

	// Checking: the flag is never set, the object type is unknown; "Patch GC" fixes the second.
	const auto gameNames = [](std::int32_t type) {
		return type == Param::OBJECT_TYPE ? std::vector<std::string>{"Tank"} : std::vector<std::string>{};
	};
	ScriptCheck sc = scriptCheck(map, templates, gameNames);
	std::vector<ScriptProblem> problems = scriptProblems(*map.sides, templates, sc);
	CHECK(problems.size() == 2 && problems[0].text == "Attack: Flag 'Ready' is never initialized." && problems[1].text == "Attack: Object type 'GC_Tank' does not exist.");
	CHECK(problems[0].path == (ScriptPath{red, -1, 0}));
	CHECK(patchGcObjectTypes(*map.sides, sc) == 1 && map.sides->sides[static_cast<std::size_t>(red)].scripts.scripts[0].falseActions[0].parameters[0].text == "Tank");
	MapModel::ScriptCall raise = makeCall(templates, true, 1);
	raise.parameters[0].text = "Ready";
	map.sides->sides[0].scripts.scripts.push_back(newScript(templates, "Raise"));
	map.sides->sides[0].scripts.scripts[0].actions.push_back(raise);
	sc = scriptCheck(map, templates, gameNames);
	CHECK(scriptProblems(*map.sides, templates, sc).empty());
	MapModel::ScriptParameter border;
	border.type = Param::BOUNDARY;
	border.intValue = 1;
	CHECK(parameterWarning(border, true, sc) == "Border 2 does not exist.");
	MapModel::ScriptParameter team;
	team.type = Param::TEAM;
	team.text = "team";
	CHECK(parameterWarning(team, true, sc) == "Team 'team' does not exist.");
	team.text = THIS_TEAM;
	CHECK(parameterWarning(team, true, sc).empty());

	// Export of one script: what it names goes along (A with B linked to it, the team with its tank, the area).
	ScriptExport options;
	options.allScripts = false;
	options.sides = false;
	options.selection = {red, -1, 0};
	std::optional<ScriptFile> file = exportScripts(map, options);
	CHECK(file && file->lists.size() == 1 && file->lists[0].scripts.size() == 1 && file->playerNames == std::vector<std::string>{SCRIPT_FILE_SELECTION});
	CHECK(file && file->objects.size() == 3 && file->waypointLinks.size() == 1 && file->polygons.size() == 1 && file->teams.size() == 1);
	options.selection = {9, -1, -1};
	CHECK(!exportScripts(map, options).has_value());

	// The file round trip, bytes the old editor reads (areas version 3).
	{
		const WriteResult written = writeScriptFile(*file);
		CHECK(written.ok());
		const ScriptFileResult reread = readScriptFile(written.bytes);
		CHECK(reread.ok() && reread.file == *file);
	}

	// Import into a map without them: new ids after the map's, link kept, team and area added.
	Map target;
	target.sides = Sides{};
	validateSides(*target.sides);
	addPlayer(*target.sides, "Red", u"Red", "FactionChina", true);
	target.objects = std::vector<MapObject>{};
	MapObject existingA;
	existingA.x = 10.0f;
	existingA.templateName = "*Waypoints/Waypoint";
	existingA.properties.set("waypointID", std::int32_t(5));
	existingA.properties.set("waypointName", std::string("A"));
	target.objects->push_back(existingA);
	MapObject otherB = existingA;
	otherB.x = 99.0f;
	otherB.properties.set("waypointID", std::int32_t(6));
	otherB.properties.set("waypointName", std::string("B"));
	target.objects->push_back(otherB);
	const EditMapParts::Parts before = mapParts(target);
	ScriptImportReport report = importScripts(target, *file, 1);
	CHECK(report.scripts == 1 && report.objects == 2 && report.teams == 1 && report.areas == 1);
	// A is the map's own (same place): the link goes from it to the imported B, which is renamed.
	CHECK(target.objects->size() == 4 && target.waypointLinks->size() == 1 && (*target.waypointLinks)[0] == (WaypointLink{5, 7}));
	CHECK((*target.objects)[2].properties.getAscii("waypointName") == std::string("B-imp") && report.messages.size() == 1);
	CHECK(target.sides->sides[1].scripts.scripts.size() == 1 && target.sides->sides[1].scripts.scripts[0].name == "Attack");
	CHECK(findTeam(*target.sides, "Attackers") >= 0 && (*target.polygons)[0].id == 1);
	auto step = mapPartsStep(target, before, "Import scripts");
	CHECK(step != nullptr && step->changes().scripts);
	History history;
	history.record(std::move(step));
	history.undo(target);
	CHECK(target.objects == before.objects && target.sides == before.sides && !target.polygons);

	// All scripts with the players: each list goes to the player of the same name, missing players come along.
	ScriptExport all;
	file = exportScripts(map, all);
	CHECK(file && file->lists.size() == 2 && file->players.size() == 2 && file->objects.size() == 4 && file->polygons.size() == 1);
	Map fresh;
	report = importScripts(fresh, *file, 0);
	CHECK(report.players == 2 && fresh.sides->sides.size() == 2 && findPlayer(*fresh.sides, "Red") >= 0);
	CHECK(fresh.sides->sides[static_cast<std::size_t>(findPlayer(*fresh.sides, "Red"))].scripts.scripts.size() == 1
		&& fresh.sides->sides[static_cast<std::size_t>(findPlayer(*fresh.sides, ""))].scripts.scripts[0].name == "Raise");
}

static void testBuildList()
{
	using namespace MapEditorCore;
	Map map;
	map.sides = Sides{};
	validateSides(*map.sides);
	const int red = addPlayer(*map.sides, "Red", u"Red", "FactionChina", false);
	Sides &sides = *map.sides;
	CHECK(addBuildEntry(sides, red, newBuildEntry("CommandCenter", 100.0f, 100.0f, 0.0f)) == 0);
	CHECK(addBuildEntry(sides, red, newBuildEntry("PowerPlant", 200.0f, 100.0f, 1.5707964f)) == 1);
	CHECK(addBuildEntry(sides, red, newBuildEntry("Barracks", 300.0f, 100.0f, 0.0f)) == 2);
	CHECK(addBuildEntry(sides, 9, newBuildEntry("X", 0.0f, 0.0f, 0.0f)) == -1);
	const BuildListEntry &e = sides.sides[static_cast<std::size_t>(red)].buildList[1];
	CHECK(e.health == 100 && e.numRebuilds == 0 && !e.initiallyBuilt && e.whiner && e.repairable && !e.unsellable && e.z == 0.0f);
	CHECK(moveBuildEntry(sides, red, 2, -1) && sides.sides[static_cast<std::size_t>(red)].buildList[1].templateName == "Barracks");
	CHECK(!moveBuildEntry(sides, red, 0, -1) && !moveBuildEntry(sides, red, 2, 1));
	CHECK(buildEntryAt(sides, red, 205.0f, 95.0f, 20.0f) == 2 && buildEntryAt(sides, red, 150.0f, 150.0f, 20.0f) == -1);

	// Power: a placed plant of the player counts, and the list up to the entry.
	map.objects = std::vector<MapObject>{};
	MapObject plant;
	plant.templateName = "PowerPlant";
	plant.properties.set("originalOwner", std::string("teamRed"));
	map.objects->push_back(plant);
	MapObject foreign = plant;
	foreign.properties.set("originalOwner", std::string("team"));
	map.objects->push_back(foreign);
	const auto energy = [](const std::string &t) { return t == "PowerPlant" ? 5 : (t == "CommandCenter" ? -1 : (t == "Barracks" ? -2 : 0)); };
	BuildPower power = buildListPower(map, red, 1, energy);
	CHECK(power.production == 5 && power.consumption == 3 && std::abs(power.used() - 0.6f) < 0.001f);
	power = buildListPower(map, red, 2, energy);
	CHECK(power.production == 10 && power.consumption == 3);
	const BuildPower onlyUse{0, 4};
	CHECK(onlyUse.used() == 1.0f && BuildPower{}.used() == 0.0f);

	sides.sides[static_cast<std::size_t>(red)].buildList[0].buildingName = "HQ";
	sides.sides[static_cast<std::size_t>(red)].buildList[0].numRebuilds = UNLIMITED_REBUILDS;
	sides.sides[static_cast<std::size_t>(red)].buildList[0].initiallyBuilt = true;
	const std::string ini = skirmishBuildListIni(sides.sides[static_cast<std::size_t>(red)], "China");
	CHECK(ini.rfind(";Skirmish AI Build List\nSkirmishBuildList China\n  Structure CommandCenter\n    Name = HQ\n    Location = X:100.00 Y:100.00\n    Rebuilds = -1\n    Angle = 0.00\n    InitiallyBuilt = Yes\n", 0) == 0);
	CHECK(ini.find("    Angle = 90.00\n") != std::string::npos && ini.size() > 20 && ini.substr(ini.size() - 29) == "END ;SkirmishBuildList China\n");
	CHECK(removeBuildEntry(sides, red, 0) && sides.sides[static_cast<std::size_t>(red)].buildList.size() == 2 && !removeBuildEntry(sides, red, 5));
}

static void testContours()
{
	using namespace MapEditorCore;
	HeightMap hm;
	hm.width = 3;
	hm.height = 3;
	hm.heights.assign(9, 0);
	hm.heights[4] = 20;
	// Levels 10 and 20 cross each of the four cells once.
	std::vector<float> lines = contourLines(hm, 10, 0);
	CHECK(lines.size() == 32);
	// Level 10 lies halfway between the corner (0) and the middle (20).
	CHECK(lines.size() >= 4 && lines[0] == 1.0f && lines[1] == 0.5f && lines[2] == 0.5f && lines[3] == 1.0f);
	// Offset 5: levels 5 and 15, both inside.
	lines = contourLines(hm, 10, 5);
	CHECK(lines.size() == 32 && std::abs(lines[1] - 0.25f) < 1e-6f);
	CHECK(contourLines(hm, 30, 0).empty() && contourLines(hm, 0, 0).empty());
	hm.heights.assign(9, 7);
	CHECK(contourLines(hm, 1, 0).empty());
}

static void testMapCheck()
{
	using namespace MapEditorCore;
	Map map;
	map.heightMap = HeightMap{};
	map.heightMap->width = 10;
	map.heightMap->height = 10;
	map.heightMap->borderSize = 2;
	map.heightMap->heights.assign(100, 0);
	map.sides = Sides{};
	validateSides(*map.sides);
	Sides &sides = *map.sides;
	const int red = addPlayer(sides, "Red", u"Red", "FactionChina", false);
	const int blue = addPlayer(sides, "Blue", u"Blue", "FactionFundamentalist", false);
	CHECK(red > 0 && blue > red);
	const int orphan = addTeam(sides, "Red");
	sides.teams[static_cast<std::size_t>(orphan)].set("teamOwner", std::string("Nobody"));
	sides.teams[static_cast<std::size_t>(orphan)].set("teamUnitType1", std::string("GC_Tank"));
	sides.teams.push_back(sides.teams[static_cast<std::size_t>(findTeam(sides, "teamRed"))]);
	const int copy = static_cast<int>(sides.teams.size()) - 1;
	addBuildEntry(sides, red, newBuildEntry("FundamentalistBarracks", 0.0f, 0.0f, 0.0f));

	map.objects = std::vector<MapObject>{};
	const auto object = [&map](const char *name, float x, float y, const char *team) {
		MapObject o;
		o.templateName = name;
		o.x = x;
		o.y = y;
		o.properties.set("originalOwner", std::string(team));
		map.objects->push_back(o);
	};
	object("Tank", 10.0f, 10.0f, "teamRed");
	object("GC_Tank", 20.0f, 20.0f, "teamRed");
	object("Bush", 30.0f, 30.0f, "teamGone");
	object("Tank", 10.02f, 10.0f, "teamRed");
	object("Tank", 100.0f, 10.0f, "teamRed");
	MapObject waypoint;
	waypoint.templateName = WAYPOINT_TEMPLATE;
	waypoint.properties.set("waypointID", std::int32_t(1));
	map.objects->push_back(waypoint);
	map.waypointLinks = std::vector<WaypointLink>{{1, 2}};

	MapCheckInput input;
	input.templateKnown = [](const std::string &t) { return t == "Tank" || t == "Bush" || t == "GLABarracks"; };
	input.factionKnown = [](const std::string &f) { return f == "FactionChina" || f == "FactionGLA"; };
	std::vector<Problem> problems = checkMap(map, input);
	const auto count = [&problems](ProblemKind kind) {
		return std::count_if(problems.begin(), problems.end(), [kind](const Problem &p) { return p.kind == kind; });
	};
	CHECK(problems.size() == 9);
	CHECK(count(ProblemKind::UnknownTemplate) == 2 && count(ProblemKind::UnknownFaction) == 1 && count(ProblemKind::TeamOwner) == 1);
	CHECK(count(ProblemKind::DuplicateTeam) == 1 && count(ProblemKind::ObjectTeam) == 1 && count(ProblemKind::DuplicateObject) == 1);
	CHECK(count(ProblemKind::OffTerrain) == 1 && count(ProblemKind::WaypointLink) == 1 && count(ProblemKind::Script) == 0);
	const Problem &barracks = problems[0];
	CHECK(barracks.subject == "FundamentalistBarracks" && barracks.buildEntries == 1 && barracks.suggestion == std::string("GLABarracks"));
	const Problem &gc = problems[1];
	CHECK(gc.subject == "GC_Tank" && gc.objects == std::vector<std::int32_t>{1} && gc.teamUnits == 1 && gc.suggestion == std::string("Tank"));
	CHECK(problems[2].kind == ProblemKind::UnknownFaction && problems[2].side == blue && problems[2].suggestion == std::string("FactionGLA"));
	CHECK(problems[3].kind == ProblemKind::TeamOwner && problems[3].team == orphan);
	CHECK(problems[4].kind == ProblemKind::DuplicateTeam && problems[4].team == copy && problems[4].subject == "teamRed");
	CHECK(problems[5].kind == ProblemKind::ObjectTeam && problems[5].subject == "teamGone" && problems[5].objects == std::vector<std::int32_t>{2});
	CHECK(problems[6].objects == std::vector<std::int32_t>{3} && problems[7].objects == std::vector<std::int32_t>{4});
	CHECK(problems[8].waypoint == 2);
	CHECK(problemFixable(ProblemKind::OffTerrain) && !problemFixable(ProblemKind::Script));
	// Without the engine nothing is known, so templates and factions are not checked.
	CHECK(checkMap(map, MapCheckInput{}).size() == 6);

	CHECK(!replacementName("GC_Jeep", input.templateKnown).has_value() && !replacementName("Tank", input.templateKnown).has_value());
	CHECK(replaceTemplate(map, "GC_Tank", "Tank") == 2 && (*map.objects)[1].templateName == "Tank");
	CHECK(sides.teams[static_cast<std::size_t>(orphan)].getAscii("teamUnitType1") == std::string("Tank"));
	CHECK(replaceTemplate(map, "FundamentalistBarracks", "GLABarracks") == 1 && replaceTemplate(map, "Tank", "Tank") == 0);
	CHECK(setFaction(sides, blue, "FactionGLA") == 1 && setFaction(sides, blue, "FactionGLA") == 0);
	CHECK(setObjectsTeam(map, {2}, "Blue") == 1 && (*map.objects)[2].properties.getAscii("originalOwner") == std::string("teamBlue"));
	CHECK(setTeamOwner(sides, orphan, "Nobody") == 0 && setTeamOwner(sides, orphan, "Red") == 1);
	CHECK(removeTeamAt(sides, findTeam(sides, "teamRed")) == 0 && removeTeamAt(sides, copy) == 1);
	CHECK(removeObjects(map, {4, 3, 4}) == 2 && map.objects->size() == 4);
	CHECK(removeWaypointLinks(map, 2) == 1 && map.waypointLinks->empty());
	problems = checkMap(map, input);
	CHECK(problems.empty());

	// Removing a template takes objects and build entries away and empties team slots.
	sides.teams[static_cast<std::size_t>(orphan)].set("teamUnitType2", std::string("Junk"));
	addBuildEntry(sides, blue, newBuildEntry("Junk", 0.0f, 0.0f, 0.0f));
	object("Junk", 0.0f, 0.0f, "teamRed");
	CHECK(removeTemplate(map, "Junk") == 3 && map.objects->size() == 4 && sides.sides[static_cast<std::size_t>(blue)].buildList.empty());
	CHECK(sides.teams[static_cast<std::size_t>(orphan)].getAscii("teamUnitType2") == std::string() && sides.teams[static_cast<std::size_t>(orphan)].getInt("teamUnitMaxCount2") == 0);
	// A waypoint removed takes its links along.
	MapObject second = waypoint;
	second.properties.set("waypointID", std::int32_t(2));
	map.objects->push_back(second);
	map.waypointLinks->push_back({1, 2});
	CHECK(removeObjects(map, {static_cast<std::int32_t>(map.objects->size()) - 1}) == 1 && map.waypointLinks->empty());
}

static void testGameLaunch()
{
	using namespace MapEditorCore;
	CHECK(gameFileArgument("C:\\Users\\A\\Documents\\Maps\\Desert\\Desert.map") == std::string("C:\\Users\\A\\Documents\\Maps\\Desert.map"));
	CHECK(gameFileArgument("C:/Maps/Desert/desert.MAP") == std::string("C:\\Maps\\desert.MAP"));
	CHECK(!gameFileArgument("C:\\Maps\\Other\\Desert.map").has_value());
	CHECK(!gameFileArgument("Desert.map").has_value() && !gameFileArgument("C:\\Maps\\Desert\\Desert.txt").has_value());
	CHECK(gameArguments("X.map") == (std::vector<std::string>{"-win", "-file", "X.map"}));
}

static void testCliffTexture()
{
	using namespace MapEditorCore;
	// 8 x 8 heights: flat rows 0..2, a ramp of 30 per row up to row 5, flat above.
	constexpr int W = 8;
	HeightMap hm;
	hm.width = W;
	hm.height = W;
	const std::uint8_t rows[W] = {0, 0, 0, 30, 60, 90, 90, 90};
	for (int y = 0; y < W; ++y)
		for (int x = 0; x < W; ++x)
			hm.heights.push_back(rows[y]);
	BlendTiles bt;
	const std::size_t cells = static_cast<std::size_t>(W * W);
	bt.tiles = std::vector<std::int16_t>(cells, 0);
	bt.blends = std::vector<std::int16_t>(cells, 0);
	bt.extraBlends = std::vector<std::int16_t>(cells, 0);
	bt.cliffInfoIndices = std::vector<std::int16_t>(cells, 0);
	bt.textureClasses.push_back(TextureClass{0, 4, 2, 0, "Rock"});
	bt.textureClasses.push_back(TextureClass{4, 4, 2, 0, "Other"});
	const auto at = [](int x, int y) { return static_cast<std::size_t>(y * W + x); };
	for (int y = 0; y < W; ++y)
		for (int x = 0; x < W; ++x)
			bt.tiles[at(x, y)] = static_cast<std::int16_t>(((y % 2) * 2 + x % 2) << 2);
	bt.tiles[at(6, 3)] = static_cast<std::int16_t>(4 << 2);		// a steep cell of the other class
	const float extent = 2.0f * 64.0f / 2048.0f;

	CHECK(isCliffCell(hm, 2, 2) && isCliffCell(hm, 2, 3) && isCliffCell(hm, 2, 4));
	CHECK(!isCliffCell(hm, 2, 1) && !isCliffCell(hm, 2, 5) && !isCliffCell(hm, 7, 3) && !isCliffCell(hm, -1, 3));

	const BlendTiles plain = bt;
	CHECK(!textureCliffRegion(bt, hm, 7, 3) && !textureCliffRegion(bt, hm, 8, 3) && bt == plain);
	BlendTiles noClass = bt;
	noClass.tiles[at(3, 3)] = static_cast<std::int16_t>(50 << 2);
	CHECK(!textureCliffRegion(noClass, hm, 3, 3) && noClass.cliffInfos.empty());
	BlendTiles badArrays = bt;
	badArrays.cliffInfoIndices.pop_back();
	CHECK(!textureCliffRegion(badArrays, hm, 3, 3) && textureAllCliffs(badArrays, hm) == 0);

	CHECK(textureCliffRegion(bt, hm, 3, 3));
	const auto info = [&bt, &at](int x, int y) -> const CliffInfo * {
		const int k = bt.cliffInfoIndices[at(x, y)];
		return k > 0 && static_cast<std::size_t>(k) <= bt.cliffInfos.size() ? &bt.cliffInfos[static_cast<std::size_t>(k) - 1] : nullptr;
	};
	bool cliffsMapped = true, farFlatFree = true, rimMapped = true, uInRange = true, vFollows = true, tileOk = true;
	for (int y = 0; y < W - 1; ++y)
	{
		for (int x = 0; x < W - 1; ++x)
		{
			const CliffInfo *c = info(x, y);
			if (x == 6 && y == 3)
				continue;
			if (y >= 2 && y <= 4 && !c)
				cliffsMapped = false;
			if ((y == 1 || y == 5) && !c)
				rimMapped = false;
			if ((y == 0 || y == 6) && c)
				farFlatFree = false;
			if (!c)
				continue;
			if (c->tileIndex != 0)
				tileOk = false;
			if (!c->mutant)
				for (int i = 0; i < 8; i += 2)
					if (c->uv[static_cast<std::size_t>(i)] < -1e-4f || c->uv[static_cast<std::size_t>(i)] > extent + 1e-4f)
						uInRange = false;
			// Corners 3 and 2 lie one row above 0 and 1: on the ramp v falls as the height rises.
			if (y >= 2 && y <= 4 && !(c->uv[7] < c->uv[1] && c->uv[5] < c->uv[3]))
				vFollows = false;
		}
	}
	CHECK(cliffsMapped);
	CHECK(rimMapped);
	CHECK(farFlatFree);
	CHECK(uInRange);
	CHECK(vFollows);
	CHECK(tileOk);
	CHECK(bt.cliffInfoIndices[at(6, 3)] == 0);
	// Neighbours up the slope share their edge: the lower cell's top is the upper cell's bottom.
	const CliffInfo *low = info(3, 2);
	const CliffInfo *high = info(3, 3);
	CHECK(low && high && std::fabs(low->uv[7] - high->uv[1]) < 1e-5f && std::fabs(low->uv[5] - high->uv[3]) < 1e-5f);
	CHECK(low && high && low->uv[1] > high->uv[1]);
	// The v range of the region is centred on minus half the texture extent.
	float loV = 1.0f, hiV = -1.0f;
	for (int y = 2; y <= 4; ++y)
		for (int x = 0; x < W - 1; ++x)
			if (const CliffInfo *c = info(x, y); c && !(x == 6 && y == 3))
				for (int i = 1; i < 8; i += 2)
				{
					loV = std::min(loV, c->uv[static_cast<std::size_t>(i)]);
					hiV = std::max(hiV, c->uv[static_cast<std::size_t>(i)]);
				}
	CHECK(std::fabs((loV + hiV) / 2.0f + extent / 2.0f) < 1e-5f);

	// Removing clears the table and every cell.
	const std::size_t mapped = static_cast<std::size_t>(std::count_if(bt.cliffInfoIndices.begin(), bt.cliffInfoIndices.end(),
		[](std::int16_t k) { return k != 0; }));
	CHECK(mapped > 0 && removeCliffMapping(bt) == static_cast<int>(mapped));
	CHECK(bt.cliffInfos.empty() && std::all_of(bt.cliffInfoIndices.begin(), bt.cliffInfoIndices.end(), [](std::int16_t k) { return k == 0; }));
	CHECK(removeCliffMapping(bt) == 0);

	// All cliffs: the rock region and the lone cell of the other class, which gets its class's tile.
	CHECK(textureAllCliffs(bt, hm) >= 1);
	CHECK(info(3, 3) != nullptr && info(0, 2) != nullptr);
	const CliffInfo *other = info(6, 3);
	CHECK(other && other->tileIndex == (4 << 2));
	const std::size_t entries = bt.cliffInfos.size();
	CHECK(textureAllCliffs(bt, hm) == 0 && bt.cliffInfos.size() == entries);

	// Painting over flat cells at the foot of the cliff: a cell of the cliff's texture continues its
	// mapping, one of another texture or away from the cliff stays unmapped.
	{
		BlendTiles painted = bt;
		const auto cellInfo = [&painted, &at](int x, int y) -> const CliffInfo * {
			const int k = painted.cliffInfoIndices[at(x, y)];
			return k > 0 && static_cast<std::size_t>(k) <= painted.cliffInfos.size() ? &painted.cliffInfos[static_cast<std::size_t>(k) - 1] : nullptr;
		};
		painted.cliffInfoIndices[at(3, 1)] = 0;
		painted.cliffInfoIndices[at(4, 1)] = 0;
		painted.tiles[at(4, 1)] = static_cast<std::int16_t>(4 << 2);
		painted.cliffInfoIndices[at(3, 0)] = 0;
		CHECK(fitPaintedCells(painted, hm, 3, 0, 4, 1) == 1);
		const CliffInfo *foot = cellInfo(3, 1);
		const CliffInfo *above = cellInfo(3, 2);
		CHECK(foot && above && std::fabs(foot->uv[6] - above->uv[0]) < 1e-5f && std::fabs(foot->uv[7] - above->uv[1]) < 1e-5f
			&& foot->tileIndex == 0);
		CHECK(cellInfo(4, 1) == nullptr && cellInfo(3, 0) == nullptr);
		CHECK(fitPaintedCells(painted, hm, 3, 0, 4, 1) == 0);
	}

	// A full table: one entry left (32384 including entry 0), then cells get no mapping.
	removeCliffMapping(bt);
	bt.cliffInfos.assign(32382, CliffInfo{});
	CHECK(textureCliffRegion(bt, hm, 3, 3));
	CHECK(bt.cliffInfos.size() == 32383 && bt.cliffInfoIndices[at(3, 3)] == 32383);
	CHECK(bt.cliffInfoIndices[at(4, 3)] == 0 && bt.cliffInfoIndices[at(2, 3)] == 0);
}

static void testMapCompare()
{
	using namespace MapEditorCore;
	using D = MapDifference::Kind;
	Map map;
	map.heightMap = HeightMap{};
	map.heightMap->width = 4;
	map.heightMap->height = 4;
	map.heightMap->borderSize = 1;
	map.heightMap->heights.assign(16, 10);
	map.blendTiles = BlendTiles{};
	TextureClass grass;
	grass.firstTile = 0;
	grass.numTiles = 4;
	grass.name = "Grass";
	TextureClass sand = grass;
	sand.firstTile = 4;
	sand.name = "Sand";
	map.blendTiles->textureClasses = {grass, sand};
	map.blendTiles->tiles.assign(16, static_cast<std::int16_t>(1 << 2));	// grass
	map.blendTiles->tiles[5] = static_cast<std::int16_t>(5 << 2);			// sand
	map.objects = std::vector<MapObject>{};
	const auto object = [&map](const char *name, float x, float y, const char *id) {
		MapObject o;
		o.templateName = name;
		o.x = x;
		o.y = y;
		if (id)
			o.properties.set("uniqueID", std::string(id));
		map.objects->push_back(o);
	};
	object("Tank", 10.0f, 10.0f, "Tank 1");
	object("Tank", 20.0f, 10.0f, "Tank 2");
	object("Bush", 30.0f, 30.0f, nullptr);
	object("Bush", 30.0f, 30.0f, nullptr);
	object("Tree", 40.0f, 40.0f, "Tree 1");
	MapObject waypoint;
	waypoint.templateName = WAYPOINT_TEMPLATE;
	waypoint.properties.set("waypointID", std::int32_t(1));
	waypoint.properties.set("waypointName", std::string("Start"));
	map.objects->push_back(waypoint);
	map.waypointLinks = std::vector<WaypointLink>{{1, 2}};
	map.polygons = std::vector<PolygonArea>{};
	PolygonArea area;
	area.name = "Lake";
	area.id = 1;
	area.points = {{{0, 0, 0}}, {{10, 0, 0}}, {{10, 10, 0}}};
	map.polygons->push_back(area);
	map.sides = Sides{};
	validateSides(*map.sides);
	addPlayer(*map.sides, "Red", u"Red", "FactionChina", false);
	Script script;
	script.name = "Intro";
	map.sides->sides[0].scripts.scripts.push_back(script);
	map.lighting = GlobalLighting{};
	map.worldInfo = PropertyMap{};
	map.worldInfo->set("weather", std::int32_t(0));

	CHECK(compareMaps(map, map).empty());

	Map other = map;
	// The other map lists the texture classes the other way round: the same textures.
	std::swap(other.blendTiles->textureClasses[0].name, other.blendTiles->textureClasses[1].name);
	for (std::int16_t &t : other.blendTiles->tiles)
		t = static_cast<std::int16_t>((t >> 2) < 4 ? 5 << 2 : 1 << 2);
	CHECK(compareMaps(map, other).empty());

	map.heightMap->heights[3] = 11;
	map.heightMap->heights[4] = 12;
	map.blendTiles->tiles[0] = static_cast<std::int16_t>(6 << 2);
	(*map.objects)[1].x = 25.0f;								// moved
	(*map.objects)[4].properties.set("objectName", std::string("Oak"));	// changed
	(*map.objects)[4].flags = 1;
	(*map.objects)[3].x = 30.02f;							// no ID: paired by position, moved
	object("Tank", 50.0f, 50.0f, "Tank 3");					// added (index 6)
	MapObject gone;
	gone.templateName = "Wall";
	gone.properties.set("uniqueID", std::string("Wall 1"));
	other.objects->push_back(gone);							// removed
	(*other.objects)[5].x = 5.0f;							// waypoint paired by ID
	map.waypointLinks->push_back({2, 3});
	map.polygons->front().isWater = true;
	PolygonArea river;
	river.id = 7;
	map.polygons->push_back(river);
	addPlayer(*map.sides, "Blue", u"Blue", "FactionAmerica", false);
	other.sides->teams[static_cast<std::size_t>(findTeam(*other.sides, "teamRed"))].set("teamIsAIRecruitable", true);
	map.sides->sides[0].scripts.scripts[0].isActive = false;
	script.name = "Outro";
	other.sides->sides[1].scripts.scripts.push_back(script);
	map.lighting->timeOfDay = 2;
	map.worldInfo->set("weather", std::int32_t(1));

	const std::vector<MapDifference> diff = compareMaps(map, other);
	std::vector<D> kinds;
	for (const MapDifference &d : diff)
		kinds.push_back(d.kind);
	const std::vector<D> expected = {D::Heights, D::Textures, D::ObjectMoved, D::ObjectMoved, D::ObjectChanged, D::ObjectMoved,
		D::ObjectAdded, D::ObjectRemoved, D::WaypointLinks, D::AreaChanged, D::AreaAdded, D::PlayerAdded, D::TeamChanged,
		D::TeamAdded, D::ScriptChanged, D::ScriptRemoved, D::Lighting, D::WorldInfo};
	CHECK(kinds == expected);
	if (kinds == expected)
	{
		CHECK(diff[0].count == 2 && diff[1].count == 1);
		CHECK(diff[2].index == 1 && diff[2].subject == "Tank (Tank 2)" && diff[3].index == 3 && diff[5].index == 5);
		CHECK(diff[4].index == 4 && diff[4].count == 2 && diff[4].detail == "flags, objectName");
		CHECK(diff[6].index == 6 && diff[7].index == -1 && diff[7].subject == "Wall (Wall 1)");
		CHECK(diff[8].count == 1);
		CHECK(diff[9].subject == "Lake" && diff[9].index == 0 && diff[9].detail == "water" && diff[10].subject == "#7" && diff[10].index == 1);
		CHECK(diff[11].subject == "Blue" && diff[13].subject == "teamBlue");
		CHECK(diff[12].subject == "teamRed" && diff[12].detail == "teamIsAIRecruitable");
		CHECK(diff[14].subject == "Intro" && diff[15].subject == "Outro" && diff[15].detail == "Red");
		CHECK(diff[17].detail == "weather");
	}

	// A different size leaves heights and textures out.
	other.heightMap->width = 5;
	const std::vector<MapDifference> sized = compareMaps(map, other);
	CHECK(!sized.empty() && sized[0].kind == D::Size);
	CHECK(std::none_of(sized.begin(), sized.end(), [](const MapDifference &d) { return d.kind == D::Heights || d.kind == D::Textures; }));

	// Large maps: 5000 objects with IDs and 5000 without, in another order, some moved.
	Map big, bigOther;
	big.objects = std::vector<MapObject>{};
	for (int i = 0; i < 5000; ++i)
	{
		MapObject o;
		o.templateName = (i % 2) ? "Tank" : "Tree";
		o.x = static_cast<float>(i % 100) * 7.0f;
		o.y = static_cast<float>(i / 100) * 7.0f;
		o.properties.set("uniqueID", o.templateName + " " + std::to_string(i));
		big.objects->push_back(o);
		o.properties = PropertyMap{};
		o.x += 3.0f;
		big.objects->push_back(o);
	}
	bigOther.objects = big.objects;
	std::reverse(bigOther.objects->begin(), bigOther.objects->end());
	for (std::size_t i = 0; i < bigOther.objects->size(); i += 100)
		(*bigOther.objects)[i].z = 1.0f;
	const auto start = std::chrono::steady_clock::now();
	const std::vector<MapDifference> bigDiff = compareMaps(big, bigOther);
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	CHECK(bigDiff.size() == 100);
	CHECK(std::all_of(bigDiff.begin(), bigDiff.end(), [](const MapDifference &d) { return d.kind == D::ObjectMoved; }));
	std::printf("  compareMaps, 10000 objects: %.1f ms\n", ms);
}

static void testHeightImage()
{
	using namespace MapEditorCore;
	HeightMap hm;
	hm.width = 5;
	hm.height = 3;
	for (int i = 0; i < 15; ++i)
		hm.heights.push_back(static_cast<std::uint8_t>(i * 17));
	const GreyImage image = heightsToImage(hm);
	CHECK(image.width == 5 && image.height == 3 && image.bits == 8 && image.pixels.size() == 15);
	// North up: the first image row is the last height map row.
	CHECK(image.pixels[0] == hm.heights[10] && image.pixels[4] == hm.heights[14] && image.pixels[10] == hm.heights[0]);
	CHECK(heightsFromImage(image, 5, 3) == hm.heights);

	// 16 bit: the full range maps to low..high.
	GreyImage deep;
	deep.width = 2;
	deep.height = 1;
	deep.bits = 16;
	deep.pixels = {0, 65535};
	CHECK((heightsFromImage(deep, 2, 1, 10, 200) == std::vector<std::uint8_t>{10, 200}));
	CHECK((heightsFromImage(deep, 3, 1, 0, 255) == std::vector<std::uint8_t>{0, 128, 255}));

	// 2x2 to 3x3: corners stay, the rest are the bilinear means. Image row 0 is y 2.
	GreyImage small;
	small.width = 2;
	small.height = 2;
	small.pixels = {0, 100, 200, 40};	// top: 0 100, bottom: 200 40
	const std::vector<std::uint8_t> up = heightsFromImage(small, 3, 3);
	CHECK((up == std::vector<std::uint8_t>{200, 120, 40, 100, 85, 70, 0, 50, 100}));
	CHECK(heightsFromImage(GreyImage{}, 3, 3).empty() && heightsFromImage(small, 0, 3).empty());

	// Import: one step, undone and redone as a whole, with the cliff flags from the new heights.
	Map map;
	map.heightMap = HeightMap{};
	map.heightMap->width = 4;
	map.heightMap->height = 4;
	map.heightMap->heights.assign(16, 0);
	map.blendTiles = BlendTiles{};
	map.blendTiles->cliffState.assign(cliffRowBytes(4) * 4, 0);
	GreyImage cliff;
	cliff.width = 2;
	cliff.height = 2;
	cliff.pixels = {255, 255, 0, 0};	// north high, south low
	History history;
	auto step = importHeights(map, cliff, 0, 255);
	CHECK(step && step->name() == "Import heights" && step->changes().terrain && step->changes().passability);
	CHECK(map.heightMap->heights[0] == 0 && map.heightMap->heights[15] == 255 && map.heightMap->heights[4] == 85);
	CHECK(cliffFlag(map.blendTiles->cliffState, 4, 0, 1));
	const std::vector<std::uint8_t> imported = map.heightMap->heights;
	history.record(std::move(step));
	history.undo(map);
	CHECK(map.heightMap->heights == std::vector<std::uint8_t>(16, 0) && !cliffFlag(map.blendTiles->cliffState, 4, 0, 1));
	history.redo(map);
	CHECK(map.heightMap->heights == imported && cliffFlag(map.blendTiles->cliffState, 4, 0, 1));
	CHECK(!importHeights(map, cliff, 0, 255));
	CHECK(!importHeights(map, GreyImage{}, 0, 255) && map.heightMap->heights == imported);
}

static void testWaterLook()
{
	using namespace MapEditorCore;
	const WaterLook base;
	const std::string ini = "; own water\r\nWeapon Test\r\n  Damage = 5\r\nEnd\r\nWaterTransparency\r\n  TurbidityDepth = 25.0 ; clear\r\n  TurbidityColor = R:10 G:20 B:30\r\n"
		"  AdditiveBlending = Yes\r\nEnd\r\n";
	const WaterLook read = readWaterLook(ini, base);
	CHECK(read.turbidityDepth == 25.0f && read.turbidityColor == (std::array<int, 3>{10, 20, 30}) && read.additive && read.refraction == 1.0f);
	CHECK(readWaterLook("", base) == base);
	// Written back: the other blocks stay, the water block holds only what differs from the game data.
	WaterLook changed = read;
	changed.additive = false;
	changed.reflection = 0.5f;
	const std::string out = writeWaterLook(ini, changed, base);
	CHECK(out.find("Weapon Test\r\n  Damage = 5\r\nEnd\r\n") != std::string::npos && out.find("AdditiveBlending") == std::string::npos
		&& out.find("  ReflectionStrength = 0.50\r\n") != std::string::npos && out.find("  TurbidityDepth = 25.00\r\n") != std::string::npos);
	CHECK(readWaterLook(out, base) == changed);
	// Back to the game data: no block at all.
	const std::string plain = writeWaterLook(out, base, base);
	CHECK(plain.find("WaterTransparency") == std::string::npos && plain.find("Weapon Test") != std::string::npos);
	CHECK(writeWaterLook("", changed, base).rfind("WaterTransparency\n", 0) == 0);
}

static void testReferences()
{
	using namespace MapEditorCore;
	MapModel::Map map;
	map.sides.emplace();
	MapModel::Sides &sides = *map.sides;
	sides.sides.resize(2);
	MapModel::Script s;
	s.name = "Attack Base";
	s.comment = "sends Team Alpha";
	MapModel::ScriptCall call;
	MapModel::ScriptParameter team;
	team.type = 3;	// a team name
	team.text = "Team Alpha";
	MapModel::ScriptParameter number;
	number.type = 0;	// an int
	call.parameters = {number, team};
	s.actions.push_back(call);
	MapModel::OrCondition any;
	any.conditions.push_back(call);
	s.conditions.push_back(any);
	sides.sides[1].scripts.scripts.push_back(s);
	MapModel::ScriptGroup group;
	group.name = "Alpha group";
	sides.sides[0].scripts.groups.push_back(group);
	MapModel::PropertyMap t;
	t.set("teamName", MapModel::PropertyValue(std::string("Team Alpha")));
	sides.teams.push_back(t);
	MapModel::BuildListEntry entry;
	entry.buildingName = "Alpha HQ";
	sides.sides[1].buildList.push_back(entry);
	map.objects.emplace();
	MapModel::MapObject o;
	o.templateName = "AmericaTankCrusader";
	o.properties.set("originalOwner", MapModel::PropertyValue(std::string("Team Alpha")));
	map.objects->push_back(o);
	map.polygons.emplace();
	MapModel::PolygonArea area;
	area.name = "Alpha zone";
	map.polygons->push_back(area);

	SearchOptions whole;
	whole.wholeValue = true;
	const std::vector<Reference> exact = findReferences(map, "team alpha", whole);
	// Condition and action parameter, team, object; the comment only contains it.
	CHECK(exact.size() == 4);
	int params = 0;
	for (const Reference &r : exact)
		params += r.kind == Reference::Kind::ScriptParameter ? 1 : 0;
	CHECK(params == 2);
	CHECK(exact[0].path == (ScriptPath{1, -1, 0}) && exact[0].part == Reference::Part::Condition && exact[0].parameter == 1);
	SearchOptions caseSensitive;
	caseSensitive.matchCase = true;
	CHECK(findReferences(map, "alpha", caseSensitive).empty());
	// Contained anywhere: group, comment, two parameters, team, build list, object, area.
	CHECK(findReferences(map, "Alpha", SearchOptions{}).size() == 8);
	CHECK(findReferences(map, "", SearchOptions{}).empty());
	CHECK(findReferences(map, "Crusader", SearchOptions{}).size() == 1);

	// Renamed in the scripts: parameters and the comment, the team itself is left to its own dialog.
	CHECK(replaceInScripts(sides, "Team Alpha", "Team Bravo", SearchOptions{}) == 3);
	const MapModel::Script &after = sides.sides[1].scripts.scripts[0];
	CHECK(after.actions[0].parameters[1].text == "Team Bravo" && after.comment == "sends Team Bravo" && after.actions[0].parameters[0].text.empty());
	CHECK(replaceInScripts(sides, "Bravo", "X", whole) == 0);
	CHECK(replaceInScripts(sides, "a", "aa", SearchOptions{}) > 0 && sides.sides[0].scripts.groups[0].name == "aalphaa group");
}

static void testFairness()
{
	using namespace MapEditorCore;
	CHECK(startNumber("Player_3_Start") == 3 && startNumber("Player_9_Start") == 0 && startNumber("Player_1_Starts") == 0);
	CHECK(resourceKind("AmericaSupplyDock") == ResourceKind::Supply && resourceKind("TechOilDerrick") == ResourceKind::Tech
		&& resourceKind("AmericaTankCrusader") == ResourceKind::None);

	// 40 x 20 cells with a border of 2; starts left and right, a supply dock near each.
	MapModel::Map map;
	map.heightMap.emplace();
	MapModel::HeightMap &hm = *map.heightMap;
	hm.width = 40;
	hm.height = 20;
	hm.borderSize = 2;
	hm.heights.assign(800, 16);
	map.objects.emplace();
	const auto waypoint = [&map](const char *name, float x, float y) {
		MapModel::MapObject o;
		o.templateName = "*Waypoints/Waypoint";
		o.x = x;
		o.y = y;
		o.properties.set("waypointID", MapModel::PropertyValue(static_cast<std::int32_t>(map.objects->size() + 1)));
		o.properties.set("waypointName", MapModel::PropertyValue(std::string(name)));
		map.objects->push_back(o);
	};
	const auto building = [&map](const char *templateName, float x, float y) {
		MapModel::MapObject o;
		o.templateName = templateName;
		o.x = x;
		o.y = y;
		map.objects->push_back(o);
	};
	waypoint("Player_1_Start", 45.0f, 75.0f);
	waypoint("Player_2_Start", 315.0f, 75.0f);
	building("AmericaSupplyDock", 95.0f, 75.0f);
	building("AmericaSupplyDock", 265.0f, 75.0f);
	building("TechOilDerrick", 185.0f, 75.0f);

	FairnessReport even = fairnessReport(map);
	CHECK(even.starts.size() == 2 && even.resources.size() == 3 && even.starts[0].number == 1);
	CHECK(std::abs(even.starts[0].nearestSupply - 50.0f) < 0.5f && std::abs(even.starts[1].nearestSupply - 50.0f) < 0.5f);
	CHECK(even.supplySpread < 0.01f && even.starts[0].suppliesFirst == 1 && even.starts[1].suppliesFirst == 1);
	// The derrick in the middle is contested and counts for no one; the starts are 270 apart.
	CHECK(even.resources[2].contested && even.starts[0].techFirst == 0 && std::abs(even.starts[0].nearestEnemy - 270.0f) < 0.5f);
	const auto territoryAt = [&even](int x, int y) { return even.territory[static_cast<std::size_t>(y) * 40 + static_cast<std::size_t>(x)]; };
	CHECK(territoryAt(5, 9) == 0 && territoryAt(34, 9) == 1 && territoryAt(20, 9) == -2 && territoryAt(0, 0) == -1);

	// A cliff wall across the map with a gap at the top: the path goes round.
	map.blendTiles.emplace();
	map.blendTiles->cliffState.assign(static_cast<std::size_t>(20) * 5, 0);
	for (int y = 2; y < 16; ++y)
		MapModel::setCliffFlag(map.blendTiles->cliffState, 40, 12, y, true);
	FairnessReport wall = fairnessReport(map);
	CHECK(wall.starts[1].nearestEnemy > 300.0f && wall.starts[0].nearestSupply < 60.0f);
	// Closed completely: the far supply and the derrick cannot be reached from start 1.
	for (int y = 0; y < 20; ++y)
		MapModel::setCliffFlag(map.blendTiles->cliffState, 40, 12, y, true);
	FairnessReport closed = fairnessReport(map);
	CHECK(closed.starts[0].unreachable == 2 && closed.starts[0].nearestEnemy < 0.0f && closed.resources[1].distance[0] < 0.0f);

	// A dock moved away from start 2: uneven.
	map.blendTiles.reset();
	(*map.objects)[3].x = 205.0f;
	FairnessReport uneven = fairnessReport(map);
	CHECK(uneven.supplySpread > 0.5f && std::abs(uneven.starts[1].nearestSupply - 110.0f) < 0.5f);
}

static void testStartPositions()
{
	using namespace MapEditorCore;
	CHECK(startPositions(100, 80, 0, StartLayout::Corners).empty() && startPositions(100, 80, 9, StartLayout::Circle).empty());
	const auto inside = [](const std::vector<std::array<float, 2>> &p, float w, float h) {
		for (const auto &q : p)
			if (q[0] < 100.0f || q[1] < 100.0f || q[0] > w - 100.0f || q[1] > h - 100.0f)
				return false;
		return true;
	};
	const auto distinct = [](const std::vector<std::array<float, 2>> &p) {
		for (std::size_t i = 0; i < p.size(); ++i)
			for (std::size_t j = i + 1; j < p.size(); ++j)
				if (std::hypot(p[i][0] - p[j][0], p[i][1] - p[j][1]) < 200.0f)
					return false;
		return true;
	};
	for (StartLayout layout : {StartLayout::Corners, StartLayout::Circle, StartLayout::TwoSides})
		for (int n = 1; n <= MAX_START_POSITIONS; ++n)
		{
			const std::vector<std::array<float, 2>> p = startPositions(200, 150, n, layout);
			CHECK(static_cast<int>(p.size()) == n && inside(p, 2000.0f, 1500.0f) && distinct(p));
		}
	// Two players: corners and circle diagonal, opposite through the middle; two sides left against right.
	for (StartLayout layout : {StartLayout::Corners, StartLayout::Circle})
	{
		const std::vector<std::array<float, 2>> p = startPositions(200, 200, 2, layout);
		CHECK(p[0][0] < 1000.0f && p[0][1] < 1000.0f && std::fabs(p[0][0] + p[1][0] - 2000.0f) < 1.0f && std::fabs(p[0][1] + p[1][1] - 2000.0f) < 1.0f);
	}
	const std::vector<std::array<float, 2>> teams = startPositions(200, 200, 4, StartLayout::TwoSides);
	CHECK(teams[0][0] < 1000.0f && teams[2][0] < 1000.0f && teams[1][0] > 1000.0f && teams[3][0] > 1000.0f);
	// Circle with four: each pair faces the other through the middle.
	const std::vector<std::array<float, 2>> ring = startPositions(200, 200, 4, StartLayout::Circle);
	CHECK(std::fabs(ring[2][0] + ring[3][0] - 2000.0f) < 1.0f && std::fabs(ring[2][1] + ring[3][1] - 2000.0f) < 1.0f);

	// The new map carries them as waypoints Player_n_Start and keeps them through writing.
	NewMapOptions options;
	options.width = 120;
	options.height = 120;
	options.border = 10;
	options.texture = "Grass";
	options.textureTiles = 16;
	options.textureWidth = 4;
	options.players = 4;
	options.layout = StartLayout::Corners;
	std::optional<Map> created = newMap(options);
	CHECK(created && created->objects && created->objects->size() == 4);
	if (!created || !created->objects || created->objects->size() != 4)
		return;
	const MapObject &third = (*created->objects)[2];
	CHECK(third.templateName == "*Waypoints/Waypoint" && third.properties.getAscii("waypointName") == std::string("Player_3_Start")
		&& third.properties.getInt("waypointID") == 3);
	const WriteResult written = writeMap(*created);
	const ReadResult reread = readMap(written.bytes);
	CHECK(written.ok() && reread.ok() && sameContent(reread.map, *created));
}

static void testWaterRaster()
{
	using namespace MapEditorCore;
	// A concave lake: the rasterised water must match a direct point-in-polygon test of every cell centre.
	MapModel::Map map;
	map.heightMap.emplace();
	map.heightMap->width = 60;
	map.heightMap->height = 50;
	map.heightMap->borderSize = 3;
	map.heightMap->heights.assign(3000, 16);
	MapModel::PolygonArea lake;
	lake.isWater = true;
	lake.points = {{40, 30, 0}, {300, 55, 0}, {180, 160, 0}, {330, 330, 0}, {95, 290, 0}, {130, 150, 0}};
	map.polygons = std::vector<MapModel::PolygonArea>{lake};
	const std::vector<std::uint8_t> passable = passableCells(map);
	const auto inside = [&lake](float x, float y) {
		bool in = false;
		const std::size_t n = lake.points.size();
		for (std::size_t i = 0, j = n - 1; i < n; j = i++)
		{
			const float xi = static_cast<float>(lake.points[i][0]), yi = static_cast<float>(lake.points[i][1]);
			const float xj = static_cast<float>(lake.points[j][0]), yj = static_cast<float>(lake.points[j][1]);
			if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
				in = !in;
		}
		return in;
	};
	int wrong = 0, wet = 0;
	for (int y = 3; y < 47; ++y)
		for (int x = 3; x < 57; ++x)
		{
			const bool water = inside((static_cast<float>(x - 3) + 0.5f) * 10.0f, (static_cast<float>(y - 3) + 0.5f) * 10.0f);
			wet += water ? 1 : 0;
			wrong += (passable[static_cast<std::size_t>(y * 60 + x)] == 0) != water ? 1 : 0;
		}
	CHECK(wet > 300 && wrong == 0);

	// Time of the full slope tint and of the fairness report on the largest map size the game allows.
	MapModel::Map big;
	big.heightMap.emplace();
	big.heightMap->width = 480;
	big.heightMap->height = 480;
	big.heightMap->borderSize = 30;
	for (int i = 0; i < 480 * 480; ++i)
		big.heightMap->heights.push_back(static_cast<std::uint8_t>((i * 7 + (i / 480) * 13) % 200));
	auto start = std::chrono::steady_clock::now();
	const std::vector<std::uint32_t> bigTint = terrainTint(big, 30.0f, false);
	const double tintMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	std::vector<MapModel::PolygonArea> lakes(40, lake);
	for (std::size_t i = 0; i < lakes.size(); ++i)
		for (auto &p : lakes[i].points)
			p[0] += static_cast<std::int32_t>(i) * 90;
	big.polygons = lakes;
	start = std::chrono::steady_clock::now();
	const std::vector<std::uint8_t> bigPassable = passableCells(big);
	const double passMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	CHECK(bigTint.size() == 480u * 480u && bigPassable.size() == 480u * 480u);
	std::printf("  terrainTint, 480 x 480: %.1f ms; passableCells with 40 lakes: %.1f ms\n", tintMs, passMs);
	CHECK(passable[0] == 0 && passable[static_cast<std::size_t>(10 * 60 + 50)] == 1);
}

static void testSymmetry()
{
	using namespace MapEditorCore;
	float sx = 0.0f, sy = 0.0f;
	CHECK(symmetrySource(SymmetryMode::MirrorLeftRight, false, 100.0f, 100.0f, 80.0f, 30.0f, sx, sy) && sx == 20.0f && sy == 30.0f);
	CHECK(!symmetrySource(SymmetryMode::MirrorLeftRight, false, 100.0f, 100.0f, 20.0f, 30.0f, sx, sy));
	CHECK(!symmetrySource(SymmetryMode::MirrorLeftRight, false, 100.0f, 100.0f, 50.0f, 30.0f, sx, sy));
	CHECK(symmetrySource(SymmetryMode::MirrorLeftRight, true, 100.0f, 100.0f, 20.0f, 30.0f, sx, sy) && sx == 80.0f);
	CHECK(symmetrySource(SymmetryMode::Rotate180, false, 100.0f, 100.0f, 50.0f, 70.0f, sx, sy) && sx == 50.0f && sy == 30.0f);
	CHECK(symmetrySource(SymmetryMode::FourQuadrants, false, 100.0f, 100.0f, 30.0f, 90.0f, sx, sy) && sx == 30.0f && sy == 10.0f);

	// 11 x 11 grid points without border: 100 x 100 world units.
	MapModel::Map map;
	map.heightMap.emplace();
	MapModel::HeightMap &hm = *map.heightMap;
	hm.width = 11;
	hm.height = 11;
	for (int y = 0; y < 11; ++y)
		for (int x = 0; x < 11; ++x)
			hm.heights.push_back(static_cast<std::uint8_t>(x * 10 + y));
	map.blendTiles.emplace();
	MapModel::BlendTiles &bt = *map.blendTiles;
	bt.textureClasses.resize(2);
	bt.textureClasses[0].firstTile = 0;
	bt.textureClasses[0].numTiles = 4;
	bt.textureClasses[0].width = 2;
	bt.textureClasses[1].firstTile = 4;
	bt.textureClasses[1].numTiles = 4;
	bt.textureClasses[1].width = 2;
	bt.tiles.assign(121, 0);
	bt.blends.assign(121, 0);
	bt.extraBlends.assign(121, 0);
	bt.cliffInfoIndices.assign(121, 0);
	bt.cliffState.assign(22, 0);
	for (int y = 0; y < 11; ++y)
		for (int x = 0; x < 11; ++x)
			bt.tiles[static_cast<std::size_t>(y * 11 + x)] = tileForClass(bt.textureClasses[x < 2 ? 1 : 0], x, y);
	MapModel::setCliffFlag(bt.cliffState, 11, 1, 4, true);
	map.objects.emplace();
	MapModel::MapObject tree;
	tree.templateName = "TreePine";
	tree.x = 20.0f;
	tree.y = 30.0f;
	tree.angle = 0.3f;
	tree.properties.set("objectName", MapModel::PropertyValue(std::string("Tree")));
	map.objects->push_back(tree);
	MapModel::MapObject start;
	start.templateName = "*Waypoints/Waypoint";
	start.x = 10.0f;
	start.y = 50.0f;
	start.properties.set("waypointID", MapModel::PropertyValue(std::int32_t(1)));
	start.properties.set("waypointName", MapModel::PropertyValue(std::string("Player_1_Start")));
	map.objects->push_back(start);
	MapModel::MapObject path = start;
	path.x = 30.0f;
	path.properties.set("waypointID", MapModel::PropertyValue(std::int32_t(2)));
	path.properties.set("waypointName", MapModel::PropertyValue(std::string("Rally")));
	map.objects->push_back(path);
	map.waypointLinks = std::vector<MapModel::WaypointLink>{{1, 2}};
	MapModel::MapObject stray = tree;
	stray.x = 70.0f;
	stray.y = 70.0f;
	map.objects->push_back(stray);

	const std::vector<float> quarterLines = symmetryLines(hm, SymmetryMode::FourQuadrants);
	CHECK(quarterLines.size() == 8 && quarterLines[0] == 50.0f && quarterLines[3] == 100.0f && quarterLines[5] == 50.0f
		&& symmetryLines(hm, SymmetryMode::MirrorBottomTop).size() == 4);
	SymmetryOptions mirror;
	std::unique_ptr<ResizeMap> step = symmetryStep(map, mirror);
	CHECK(step && step->name() == "Symmetry");
	MapModel::Map before = map;
	step->apply(map);
	CHECK(hm.heights[3 * 11 + 8] == hm.heights[3 * 11 + 2] && hm.heights[3 * 11 + 8] == 23 && hm.heights[3 * 11 + 5] == 53);
	// The class of cells 0 and 1 is mirrored to cells 9 and 8, with the tile of the target's place.
	CHECK(classOfTile(bt, bt.tiles[4 * 11 + 9]) == 1 && bt.tiles[4 * 11 + 9] == tileForClass(bt.textureClasses[1], 9, 4)
		&& classOfTile(bt, bt.tiles[4 * 11 + 7]) == 0 && MapModel::cliffFlag(bt.cliffState, 11, 8, 4));
	const std::vector<MapModel::MapObject> &objects = *map.objects;
	CHECK(objects.size() == 6);
	const auto find = [&objects](const char *key, const char *value) -> const MapModel::MapObject * {
		for (const MapModel::MapObject &o : objects)
			if (o.properties.getAscii(key) == std::optional<std::string>(value))
				return &o;
		return nullptr;
	};
	const MapModel::MapObject *treeCopy = find("objectName", "Tree_2");
	const MapModel::MapObject *startCopy = find("waypointName", "Player_2_Start");
	const MapModel::MapObject *rallyCopy = find("waypointName", "Rally_2");
	CHECK(treeCopy && treeCopy->x == 80.0f && treeCopy->y == 30.0f && std::abs(treeCopy->angle - (3.14159265f - 0.3f)) < 0.001f);
	CHECK(startCopy && rallyCopy && startCopy->x == 90.0f && rallyCopy->x == 70.0f && find("objectName", "Tree") != nullptr);
	// The stray object on the right is gone; the copied path keeps its link.
	CHECK(std::none_of(objects.begin(), objects.end(), [](const MapModel::MapObject &o) { return o.x == 70.0f && o.y == 70.0f; }));
	const std::int32_t a = startCopy ? startCopy->properties.getInt("waypointID").value_or(0) : 0;
	const std::int32_t b = rallyCopy ? rallyCopy->properties.getInt("waypointID").value_or(0) : 0;
	CHECK(map.waypointLinks && map.waypointLinks->size() == 2 && (*map.waypointLinks)[1].from == a && (*map.waypointLinks)[1].to == b && a > 2);
	step->revert(map);
	CHECK(map.objects == before.objects && map.heightMap == before.heightMap && map.blendTiles == before.blendTiles && map.waypointLinks == before.waypointLinks);
	// A symmetric map stays as it is.
	step->apply(map);
	MapModel::Map once = map;
	std::unique_ptr<ResizeMap> again = symmetryStep(map, mirror);
	if (again)
		again->apply(map);
	CHECK(map.heightMap == once.heightMap);

	// Four quarters: three copies of the tree, each turned.
	map = before;
	SymmetryOptions quarters;
	quarters.mode = SymmetryMode::FourQuadrants;
	quarters.terrain = false;
	symmetryStep(map, quarters)->apply(map);
	int trees = 0;
	for (const MapModel::MapObject &o : *map.objects)
		trees += o.templateName == "TreePine" ? 1 : 0;
	CHECK(trees == 4 && map.heightMap == before.heightMap);
}

static void testSlope()
{
	using namespace MapEditorCore;
	MapModel::HeightMap hm;
	hm.width = 4;
	hm.height = 4;
	hm.heights.assign(16, 16);	// 10 world units high everywhere
	CHECK(std::abs(cellSlope(hm, 1, 1)) < 0.001f);
	// One step of 16 over a cell (10 units up over 10 across) is 45 degrees.
	hm.heights[1 * 4 + 2] = 32;
	CHECK(std::abs(cellSlope(hm, 1, 1) - 45.0f) < 0.01f && std::abs(cellSlope(hm, 2, 1) - 45.0f) < 0.01f);
	// Outside the map the height is 0, as in the game: the last row and column rise at the edge.
	CHECK(cellSlope(hm, 3, 0) > 44.0f && slopeMap(hm).size() == 16);
	const SlopeColor flat = slopeColor(0.0f, 45.0f);
	const SlopeColor half = slopeColor(22.5f, 45.0f);
	const SlopeColor over = slopeColor(50.0f, 45.0f);
	CHECK(flat.r == 0 && flat.g == 200 && half.r == 255 && half.g == 200 && over.r == 140 && over.g == 0);

	// The tint of the 3D view: steep cells in the slope colours, flat ones clear, cliff flags red.
	MapModel::Map map;
	map.heightMap = hm;
	CHECK(terrainTint(map, 0.0f, false).empty() && terrainTint(map, 45.0f, false).size() == 16);
	const std::vector<std::uint32_t> tint = terrainTint(map, 45.0f, false);
	CHECK(tint[1 * 4 + 1] == 0xb08c0000u && tint[2 * 4 + 0] == 0u);
	map.blendTiles.emplace();
	map.blendTiles->cliffState.assign(4, 0);
	map.blendTiles->cliffState[2] = 1;	// cell 0, 2
	const std::vector<std::uint32_t> flags = terrainTint(map, 0.0f, true);
	CHECK(flags.size() == 16 && flags[2 * 4 + 0] == 0x90e03030u && flags[1 * 4 + 1] == 0u);
}

static void testGraphicsLevels()
{
	using namespace MapEditorCore;
	const std::string text = "; head\nStaticGameLOD = Low\n  WaterQuality = 0\n  D3D11Effects = fxaa,sharpen ; comment\n  TerrainAnisotropy = 4\nEnd\n"
		"StaticGameLOD = High\n  D3D11Effects = fxaa,ssao,shadowmap=2048+terrainrock\n  WaterQuality = 2\nEnd\n";
	const std::vector<GraphicsLevel> levels = parseGraphicsLevels(text);
	CHECK(levels.size() == 2 && levels[0].name == "Low" && levels[0].effects == "fxaa,sharpen" && levels[0].terrainAnisotropy == 4
		&& levels[1].waterQuality == 2 && levels[1].terrainAnisotropy == 0);
	const std::vector<std::string> names = effectNames(levels[1].effects);
	CHECK(names.size() == 4 && names[2] == "shadowmap" && names[3] == "terrainrock");
	CHECK(effectsWithout(levels[1].effects, {"shadowmap", "fxaa"}) == "ssao,terrainrock" && effectsWithout("", {"x"}).empty());
	// The project's own file, which the editor reads as it is: four levels, the later ones with shadow maps.
	std::ifstream in(std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".." / "Data" / "INI" / "GameLODOverride.ini");
	if (in)
	{
		const std::string file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		const std::vector<GraphicsLevel> project = parseGraphicsLevels(file);
		CHECK(project.size() == 4 && project[0].name == "Low" && project[3].name == "VeryHigh");
		const std::vector<std::string> top = project.size() == 4 ? effectNames(project[3].effects) : std::vector<std::string>();
		CHECK(project.size() == 4 && effectsWithout(project[3].effects, {}) == project[3].effects
			&& std::count(top.begin(), top.end(), std::string("shadowmap")) == 1);
	}
}

static void testParticleIni()
{
	using namespace MapEditorCore;
	const std::string text = "; comment\nParticleSystem SmokePuff\n  Priority = WEAPON_EXPLOSION\n  IsOneShot = No\n  Color1 = R:255 G:128 B:0 0 ; hot\n  Alpha1 = 0.00 1.00 30\nEnd\n";
	std::optional<ParticleBlock> block = parseParticleBlock(text);
	CHECK(block && block->name == "SmokePuff" && block->fields.size() == 4);
	CHECK(block && block->find("Priority") && *block->find("Priority") == "WEAPON_EXPLOSION" && !block->find("Missing"));
	CHECK(block && *block->find("Color1") == "R:255 G:128 B:0 0");
	if (block)
	{
		block->set("IsOneShot", "Yes");
		block->set("Lifetime", "10.00 20.00");
		const std::string out = writeParticleBlock(*block);
		CHECK(out.rfind("ParticleSystem SmokePuff\n  Priority = WEAPON_EXPLOSION\n  IsOneShot = Yes\n", 0) == 0);
		CHECK(out.size() > 29 && out.substr(out.size() - 29) == "  Lifetime = 10.00 20.00\nEnd\n");
		std::optional<ParticleBlock> again = parseParticleBlock(out);
		CHECK(again && again->fields.size() == 5 && *again->find("IsOneShot") == "Yes");
	}
	CHECK(!parseParticleBlock("ParticleSystem Broken\n  Priority = X\n").has_value());
	const std::optional<ColorKey> color = parseColorKey("R:10 G:20 B:30 40");
	CHECK(color && color->r == 10 && color->g == 20 && color->b == 30 && color->frame == 40 && writeColorKey(*color) == "R:10 G:20 B:30 40");
	const std::optional<AlphaKey> alpha = parseAlphaKey("0.25 0.75 12");
	CHECK(alpha && alpha->high == 0.75f && alpha->frame == 12 && writeAlphaKey(*alpha) == "0.25 0.75 12");
	CHECK(!parseColorKey("12 34").has_value() && particleChoices("Shader").size() == 4 && particleChoices("IsHollow").size() == 2 && particleChoices("Size").empty());

	// Emission volume outlines around the system at (100, 200, 10).
	ParticleBlock volume;
	volume.name = "Volume";
	CHECK(emissionOutline(volume, 100.0f, 200.0f, 10.0f).size() == 3);	// no type: point cross
	volume.set("VolumeType", "LINE");
	volume.set("VolLineStart", "X:-5.00 Y:0.00 Z:1.00");
	volume.set("VolLineEnd", "X:5.00 Y:0.00 Z:1.00");
	std::vector<ParticleOutline> line = emissionOutline(volume, 100.0f, 200.0f, 10.0f);
	CHECK(line.size() == 1 && line[0].points.size() == 6 && line[0].points[0] == 95.0f && line[0].points[3] == 105.0f && line[0].points[5] == 11.0f && !line[0].closed);
	volume.set("VolumeType", "BOX");
	volume.set("VolBoxHalfSize", "X:2.00 Y:3.00 Z:4.00");
	std::vector<ParticleOutline> box = emissionOutline(volume, 0.0f, 0.0f, 0.0f);
	CHECK(box.size() == 6 && box[0].closed && box[0].points.size() == 12 && box[0].points[2] == -4.0f && box[1].points[2] == 4.0f && box[0].points[4] == -3.0f);
	volume.set("VolumeType", "SPHERE");
	volume.set("VolSphereRadius", "8.00");
	std::vector<ParticleOutline> sphere = emissionOutline(volume, 0.0f, 0.0f, 0.0f);
	bool onSphere = sphere.size() == 3;
	for (const ParticleOutline &o : sphere)
		for (std::size_t i = 0; i + 2 < o.points.size(); i += 3)
			onSphere = onSphere && std::abs(std::sqrt(o.points[i] * o.points[i] + o.points[i + 1] * o.points[i + 1] + o.points[i + 2] * o.points[i + 2]) - 8.0f) < 0.001f;
	CHECK(onSphere);
	volume.set("VolumeType", "CYLINDER");
	volume.set("VolCylinderRadius", "5.00");
	volume.set("VolCylinderLength", "20.00");
	std::vector<ParticleOutline> cylinder = emissionOutline(volume, 0.0f, 0.0f, 0.0f);
	CHECK(cylinder.size() == 6 && cylinder[0].points[2] == -10.0f && cylinder[1].points[2] == 10.0f && cylinder[2].points[0] == 5.0f);

	// Key frames as the game uses them: the list ends at the first later key with frame 0.
	ParticleBlock keys;
	keys.name = "Keys";
	keys.set("Alpha1", "0.00 0.50 0");
	keys.set("Alpha2", "1.00 1.00 10");
	keys.set("Alpha3", "0.00 0.00 0");
	keys.set("Alpha4", "0.50 0.50 40");
	const std::vector<AlphaKey> alphas = usedAlphaKeys(keys);
	CHECK(alphas.size() == 2 && alphas[1].frame == 10 && alphas[0].high == 0.5f);
	// Written back sorted, strictly increasing, the rest cleared as the game writes it.
	setAlphaKeys(keys, {AlphaKey{0.0f, 0.0f, 0}, AlphaKey{1.0f, 1.0f, 30}, AlphaKey{0.5f, 0.5f, 10}, AlphaKey{0.2f, 0.2f, 10}});
	const std::vector<AlphaKey> sorted = usedAlphaKeys(keys);
	CHECK(sorted.size() == 4 && sorted[1].frame == 10 && sorted[2].frame == 11 && sorted[3].frame == 30 && sorted[1].high == 0.5f);
	CHECK(*keys.find("Alpha5") == "0.00 0.00 0" && *keys.find("Alpha8") == "0.00 0.00 0");
	setAlphaKeys(keys, {AlphaKey{1.0f, 1.0f, 0}, AlphaKey{0.0f, 0.0f, 0}});
	CHECK(usedAlphaKeys(keys).size() == 2 && usedAlphaKeys(keys)[1].frame == 1);	// frame 0 would end the list
	std::vector<AlphaKey> many(12, AlphaKey{0.5f, 0.5f, 5});
	setAlphaKeys(keys, many);
	CHECK(usedAlphaKeys(keys).size() == 8);
	setColorKeys(keys, {ColorKey{255, 0, 0, 0}, ColorKey{0, 0, 255, 20}});
	const std::vector<ColorKey> colors = usedColorKeys(keys);
	CHECK(colors.size() == 2 && *keys.find("Color3") == "R:0 G:0 B:0 0");
	const ColorKey middle = colorAt(colors, 10);
	CHECK(middle.r == 128 && middle.b == 128 && middle.g == 0 && colorAt(colors, 50).b == 255 && colorAt(colors, 0).r == 255);

	// Differences between two blocks, as the editor lists them against the game data.
	ParticleBlock original;
	original.name = "Cmp";
	original.set("Shader", "ALPHA");
	original.set("Size", "1.00 2.00");
	original.set("Gone", "1");
	ParticleBlock edited = original;
	edited.set("Shader", "ADDITIVE");
	edited.fields.erase(edited.fields.begin() + 2);
	edited.set("New", "2");
	const std::vector<ParticleChange> changes = compareParticleBlocks(original, edited);
	CHECK(changes.size() == 3 && changes[0].key == "Shader" && changes[0].before == "ALPHA" && changes[0].after == "ADDITIVE"
		&& changes[1].key == "New" && changes[1].before.empty() && changes[2].key == "Gone" && changes[2].after.empty());
	CHECK(compareParticleBlocks(original, original).empty());
	const std::vector<std::string> names = particleBlockNames("; head\nParticleSystem A\n  Size = 1\nEnd\n  particlesystem B ; second\nEnd\nParticleSystem A\nEnd\n");
	CHECK(names.size() == 2 && names[0] == "A" && names[1] == "B");
}

int main(int argc, char **argv)
{
	// Writes the test map, uncompressed, as seed for tools/fuzz (FuzzMapReader).
	if (argc == 3 && std::strcmp(argv[1], "--write-sample") == 0)
	{
		const WriteResult written = writeMap(makeTestMap());
		std::string error;
		if (!written.ok() || !writeFile(argv[2], written.bytes, error))
			return 1;
		return 0;
	}

	std::printf("MapEditorTests\n\n");
	testByteReader();
	testNameTable();
	testDict();
	testReferenceBytes();
	testRoundTrip();
	testEditAndKeepIds();
	testOldVersions();
	testCliffStateVersion7();
	testDamagedFiles();
	testCompression();
	testWriteErrors();
	testFiles();
	testUpgrades();
	testLegacyScriptCalls();
	testEmptyTableCounts();
	testHistory();
	testMoveObjects();
	testEditObjects();
	testDeleteObjects();
	testCopyPaste();
	testWaypoints();
	testAreas();
	testRoads();
	testClipboardBytes();
	testHeightBrush();
	testTexturePaint();
	testTextureBlend();
	testHeightShapes();
	testPassabilityAndFill();
	testPlacement();
	testSelectionTools();
	testLayersAndGroups();
	testMapSettings();
	testMapPreview();
	testMapSize();
	testSidesCommands();
	testScriptCommands();
	testScriptExchange();
	testBuildList();
	testGameLaunch();
	testMapCheck();
	testContours();
	testCliffTexture();
	testMapCompare();
	testHeightImage();
	testParticleIni();
	testGraphicsLevels();
	testSlope();
	testWaterLook();
	testReferences();
	testFairness();
	testStartPositions();
	testWaterRaster();
	testSymmetry();
	std::printf("\n%d checks, %d failures\n", s_checks, s_failures);
	return (s_failures == 0) ? 0 : 1;
}
