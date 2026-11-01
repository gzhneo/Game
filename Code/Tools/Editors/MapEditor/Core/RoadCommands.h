/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// RoadCommands.h -- roads as the map stores them: a segment is two map objects in a row, the first
// flagged ROAD_POINT1, the second ROAD_POINT2, both named after the road type (Roads.ini). Segments
// join where their ends lie on the same spot; the terrain builds the road surface from them.
// Adding, splitting, changing and deleting segments follows the old WorldBuilder's RoadTool.

#pragma once

#include "Core/ObjectCommands.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace MapEditorCore
{

// Object flags of road and bridge points (MapObject.h of the game).
inline constexpr std::int32_t ROAD_POINT1 = 0x02;
inline constexpr std::int32_t ROAD_POINT2 = 0x04;
inline constexpr std::int32_t ROAD_CORNER_ANGLED = 0x08;
inline constexpr std::int32_t BRIDGE_POINT1 = 0x10;
inline constexpr std::int32_t BRIDGE_POINT2 = 0x20;
inline constexpr std::int32_t ROAD_CORNER_TIGHT = 0x40;
inline constexpr std::int32_t ROAD_JOIN = 0x80;
inline constexpr std::int32_t ROAD_CORNER_FLAGS = ROAD_CORNER_ANGLED | ROAD_CORNER_TIGHT;
inline constexpr std::int32_t ROAD_AND_BRIDGE_FLAGS = ROAD_POINT1 | ROAD_POINT2 | BRIDGE_POINT1 | BRIDGE_POINT2;

[[nodiscard]] bool isRoadOrBridgePoint(const MapModel::MapObject &o);

enum class SegmentKind
{
	Roads,
	Bridges,
	Both
};

// Index of the first point of every segment of the kind (bridges: BRIDGE_POINT1, BRIDGE_POINT2).
[[nodiscard]] std::vector<std::int32_t> roadSegments(const MapModel::Map &map, SegmentKind kind = SegmentKind::Roads);
[[nodiscard]] bool isBridgeSegment(const MapModel::Map &map, std::int32_t first);

// The road end nearest to (x, y) within tolerance; its position goes to (endX, endY). -1 if none.
std::int32_t roadEndAt(const MapModel::Map &map, float x, float y, float tolerance, float &endX, float &endY);
// The segment whose centre line passes within tolerance of (x, y), the nearest; the nearest point
// on it goes to (onX, onY). -1 if none.
std::int32_t roadSegmentAt(const MapModel::Map &map, float x, float y, float tolerance, float &onX, float &onY,
	SegmentKind kind = SegmentKind::Roads);
// All road points exactly at (x, y): the ends that meet there.
[[nodiscard]] std::vector<std::int32_t> roadPointsAt(const MapModel::Map &map, float x, float y);

// A segment of the road type from (x1, y1) to (x2, y2) with the style flags (corners, join) on both
// points. With split, that segment is cut in two at (x1, y1) first, so the new road branches off
// it; with splitEnd, that one is cut at (x2, y2), so the new road ends in a junction. The whole is
// one step ("Add road").
std::unique_ptr<Command> addRoad(const MapModel::Map &map, const std::string &type, float x1, float y1, float x2, float y2,
	std::int32_t style, std::int32_t split = -1, std::int32_t splitEnd = -1);
// A bridge segment of the bridge type ("Add bridge"); bridges neither snap nor cut roads.
std::unique_ptr<Command> addBridge(const MapModel::Map &map, const std::string &type, float x1, float y1, float x2, float y2);
// Deletes a road or bridge segment.
std::unique_ptr<Command> deleteRoad(const MapModel::Map &map, std::int32_t first);
// Type and style flags (corners, join; ignored for bridges) of the segment; null if nothing changes.
std::unique_ptr<Command> changeRoad(const MapModel::Map &map, std::int32_t first, const std::string &type, std::int32_t style);

} // namespace MapEditorCore
