/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// DemoMap.cpp -- a small generated map for the self test.

#include "App/DemoMap.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace MapEditorApp
{

MapModel::Map makeDemoMap()
{
	using namespace MapModel;
	constexpr int W = 180;
	constexpr int H = 150;
	constexpr int BORDER = 20;
	Map map;

	HeightMap hm;
	hm.width = W;
	hm.height = H;
	hm.borderSize = BORDER;
	hm.boundaries = {{W - 2 * BORDER, H - 2 * BORDER}};
	hm.heights.resize(static_cast<std::size_t>(W * H));
	auto riverY = [](double x) { return 70.0 + 22.0 * std::sin(x / 23.0); };
	for (int y = 0; y < H; ++y)
	{
		for (int x = 0; x < W; ++x)
		{
			double h = 60.0 + 28.0 * std::sin(x / 17.0) * std::cos(y / 21.0) + 14.0 * std::sin((x + y) / 9.0);
			if (x > 120 && y > 90)
				h += (x - 120) * 1.6 + (y - 90) * 1.2;	// mountains in the north-east
			const double d = std::abs(y - riverY(x));
			if (d < 9.0)
				h -= (9.0 - d) * 4.0;					// river bed
			hm.heights[static_cast<std::size_t>(y * W + x)] = static_cast<std::uint8_t>(std::clamp(h, 2.0, 250.0));
		}
	}
	map.heightMap = hm;

	BlendTiles bt;
	bt.textureClasses = {{0, 16, 4, 0, "GrassMediumType8"}, {16, 16, 4, 0, "DirtRoadType1"},
		{32, 16, 4, 0, "CliffMediumType8"}, {48, 36, 6, 0, "SnowLargeType1"}, {84, 4, 2, 0, "SandType1"}};
	bt.numBitmapTiles = 88;
	const std::size_t cells = hm.heights.size();
	bt.tiles.resize(cells);
	bt.blends.assign(cells, 0);
	bt.extraBlends.assign(cells, 0);
	bt.cliffInfoIndices.assign(cells, 0);
	const std::size_t rowBytes = static_cast<std::size_t>((W + 7) / 8);
	bt.cliffState.assign(rowBytes * H, 0);
	for (int y = 0; y < H; ++y)
	{
		for (int x = 0; x < W; ++x)
		{
			const std::size_t i = static_cast<std::size_t>(y * W + x);
			const int h = hm.heights[i];
			const int nx = hm.heights[static_cast<std::size_t>(y * W + std::min(x + 1, W - 1))];
			const int ny = hm.heights[static_cast<std::size_t>(std::min(y + 1, H - 1) * W + x)];
			const bool steep = std::abs(nx - h) > 15 || std::abs(ny - h) > 15;
			int tile = 0;
			if (steep)
			{
				tile = 32;
				bt.cliffState[static_cast<std::size_t>(y) * rowBytes + static_cast<std::size_t>(x >> 3)] |= static_cast<std::uint8_t>(1 << (x & 7));
			}
			else if (h > 150)
				tile = 48;
			else if (std::abs(y - riverY(x)) < 11.0)
				tile = 84;
			else if ((x / 6 + y / 7) % 11 == 0)
				tile = 16;
			// Tile within the class from the cell position, like the game's tiling of 2 x 2 or larger classes.
			bt.tiles[i] = static_cast<std::int16_t>((tile + (x % 2) + 2 * (y % 2)) << 2);
		}
	}
	map.blendTiles = bt;

	PropertyMap world;
	world.set("mapName", std::string("Demo"));
	world.set("weather", std::int32_t(0));
	map.worldInfo = world;

	Sides sides;
	Side neutral;
	neutral.properties.set("playerName", std::string(""));
	sides.sides.push_back(neutral);
	map.sides = sides;

	std::vector<MapObject> objects;
	auto waypoint = [&](const char *name, float x, float y, int id) {
		MapObject o;
		o.x = x;
		o.y = y;
		o.templateName = "*Waypoints/Waypoint";
		o.properties.set("waypointID", std::int32_t(id));
		o.properties.set("waypointName", std::string(name));
		objects.push_back(o);
	};
	waypoint("Player_1_Start", 250.0f, 250.0f, 1);
	waypoint("Player_2_Start", 1150.0f, 900.0f, 2);
	for (int i = 0; i < 160; ++i)
	{
		MapObject tree;
		tree.x = static_cast<float>(100 + (i * 73) % 1200);
		tree.y = static_cast<float>(100 + (i * 131) % 950);
		tree.templateName = "TreePine";
		objects.push_back(tree);
	}
	map.objects = objects;

	PolygonArea river;
	river.name = "River";
	river.id = 1;
	river.isWater = true;
	river.isRiver = true;
	for (int x = -BORDER; x <= W - BORDER; x += 10)
		river.points.push_back({x * 10, static_cast<std::int32_t>((riverY(x + BORDER) - 7.0 - BORDER) * 10.0), 40});
	for (int x = W - BORDER; x >= -BORDER; x -= 10)
		river.points.push_back({x * 10, static_cast<std::int32_t>((riverY(x + BORDER) + 7.0 - BORDER) * 10.0), 40});
	map.polygons = std::vector<PolygonArea>{river};
	// Afternoon sun from the south-west, as in the game's default lighting; the other two lights stay off.
	GlobalLighting lighting;
	lighting.timeOfDay = 2;
	const Light sun{{0.38f, 0.38f, 0.42f}, {0.95f, 0.92f, 0.85f}, {-0.70f, 0.30f, -0.65f}};
	for (TimeOfDayLighting &t : lighting.times)
	{
		t.terrain[0] = sun;
		t.objects[0] = sun;
	}
	map.lighting = lighting;
	map.waypointLinks = std::vector<WaypointLink>{};
	return map;
}

} // namespace MapEditorApp
