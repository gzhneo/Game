/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// RoadCommands.cpp -- see RoadCommands.h.

#include "Core/RoadCommands.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace MapEditorCore
{

namespace
{

MapModel::MapObject roadPoint(const std::string &type, float x, float y, std::int32_t flags)
{
	MapModel::MapObject o;
	o.x = x;
	o.y = y;
	o.templateName = type;
	o.flags = flags;
	// Roads belong to the neutral team, as the old WorldBuilder sets them.
	o.properties.set("originalOwner", MapModel::PropertyValue(std::string("team")));
	return o;
}

} // namespace

bool isRoadOrBridgePoint(const MapModel::MapObject &o)
{
	return (o.flags & ROAD_AND_BRIDGE_FLAGS) != 0;
}

std::vector<std::int32_t> roadSegments(const MapModel::Map &map, SegmentKind kind)
{
	std::vector<std::int32_t> result;
	if (!map.objects)
		return result;
	const auto &objects = *map.objects;
	for (std::size_t i = 0; i + 1 < objects.size(); ++i)
	{
		const bool road = (objects[i].flags & ROAD_POINT1) && (objects[i + 1].flags & ROAD_POINT2);
		const bool bridge = (objects[i].flags & BRIDGE_POINT1) && (objects[i + 1].flags & BRIDGE_POINT2);
		if ((road && kind != SegmentKind::Bridges) || (bridge && kind != SegmentKind::Roads))
		{
			result.push_back(static_cast<std::int32_t>(i));
			++i;
		}
	}
	return result;
}

bool isBridgeSegment(const MapModel::Map &map, std::int32_t first)
{
	const std::vector<std::int32_t> bridges = roadSegments(map, SegmentKind::Bridges);
	return std::find(bridges.begin(), bridges.end(), first) != bridges.end();
}

std::int32_t roadEndAt(const MapModel::Map &map, float x, float y, float tolerance, float &endX, float &endY)
{
	std::int32_t best = -1;
	float bestDistance = tolerance;
	for (std::int32_t first : roadSegments(map))
	{
		for (std::int32_t i : {first, first + 1})
		{
			const MapModel::MapObject &o = (*map.objects)[static_cast<std::size_t>(i)];
			const float d = std::hypot(o.x - x, o.y - y);
			if (d <= bestDistance)
			{
				best = i;
				bestDistance = d;
				endX = o.x;
				endY = o.y;
			}
		}
	}
	return best;
}

std::int32_t roadSegmentAt(const MapModel::Map &map, float x, float y, float tolerance, float &onX, float &onY, SegmentKind kind)
{
	std::int32_t best = -1;
	float bestDistance = tolerance;
	for (std::int32_t first : roadSegments(map, kind))
	{
		const MapModel::MapObject &a = (*map.objects)[static_cast<std::size_t>(first)];
		const MapModel::MapObject &b = (*map.objects)[static_cast<std::size_t>(first) + 1];
		const float dx = b.x - a.x;
		const float dy = b.y - a.y;
		const float lengthSq = dx * dx + dy * dy;
		const float t = lengthSq > 0.0f ? std::clamp(((x - a.x) * dx + (y - a.y) * dy) / lengthSq, 0.0f, 1.0f) : 0.0f;
		const float px = a.x + t * dx;
		const float py = a.y + t * dy;
		const float d = std::hypot(px - x, py - y);
		if (d <= bestDistance)
		{
			best = first;
			bestDistance = d;
			onX = px;
			onY = py;
		}
	}
	return best;
}

std::vector<std::int32_t> roadPointsAt(const MapModel::Map &map, float x, float y)
{
	std::vector<std::int32_t> result;
	for (std::int32_t first : roadSegments(map))
	{
		for (std::int32_t i : {first, first + 1})
		{
			const MapModel::MapObject &o = (*map.objects)[static_cast<std::size_t>(i)];
			if (o.x == x && o.y == y)
				result.push_back(i);
		}
	}
	return result;
}

std::unique_ptr<Command> addRoad(const MapModel::Map &map, const std::string &type, float x1, float y1, float x2, float y2,
	std::int32_t style, std::int32_t split, std::int32_t splitEnd)
{
	constexpr std::int32_t STYLE_FLAGS = ROAD_CORNER_FLAGS | ROAD_JOIN;
	Clipboard clipboard;
	std::vector<std::int32_t> removed;
	const std::vector<std::int32_t> segments = roadSegments(map);
	// A cut segment becomes two with its type and flags, meeting at the cut.
	const auto cut = [&](std::int32_t segment, float x, float y) {
		if (segment < 0 || std::find(segments.begin(), segments.end(), segment) == segments.end()
			|| std::find(removed.begin(), removed.end(), segment) != removed.end())
			return;
		const MapModel::MapObject &a = (*map.objects)[static_cast<std::size_t>(segment)];
		const MapModel::MapObject &b = (*map.objects)[static_cast<std::size_t>(segment) + 1];
		const std::int32_t cutFlags = a.flags & STYLE_FLAGS;
		clipboard.objects.push_back(a);
		clipboard.objects.push_back(roadPoint(a.templateName, x, y, ROAD_POINT2 | cutFlags));
		clipboard.objects.push_back(roadPoint(a.templateName, x, y, ROAD_POINT1 | cutFlags));
		clipboard.objects.push_back(b);
		removed.push_back(segment);
	};
	cut(split, x1, y1);
	cut(splitEnd, x2, y2);
	clipboard.objects.push_back(roadPoint(type, x1, y1, ROAD_POINT1 | (style & STYLE_FLAGS)));
	clipboard.objects.push_back(roadPoint(type, x2, y2, ROAD_POINT2 | (style & STYLE_FLAGS)));
	std::vector<std::unique_ptr<Command>> parts;
	if (!removed.empty())
	{
		std::vector<std::int32_t> points;
		for (std::int32_t segment : removed)
			points.insert(points.end(), {segment, segment + 1});
		parts.push_back(std::make_unique<DeleteObjects>(map, std::move(points), "Delete road"));
	}
	parts.push_back(pasteObjects(map, clipboard, 0.0f, 0.0f, "Add road"));
	return std::make_unique<Batch>("Add road", std::move(parts));
}

std::unique_ptr<Command> addBridge(const MapModel::Map &map, const std::string &type, float x1, float y1, float x2, float y2)
{
	Clipboard clipboard;
	clipboard.objects.push_back(roadPoint(type, x1, y1, BRIDGE_POINT1));
	clipboard.objects.push_back(roadPoint(type, x2, y2, BRIDGE_POINT2));
	return pasteObjects(map, clipboard, 0.0f, 0.0f, "Add bridge");
}

std::unique_ptr<Command> deleteRoad(const MapModel::Map &map, std::int32_t first)
{
	const std::vector<std::int32_t> segments = roadSegments(map, SegmentKind::Both);
	if (std::find(segments.begin(), segments.end(), first) == segments.end())
		return nullptr;
	return std::make_unique<DeleteObjects>(map, std::vector<std::int32_t>{first, first + 1}, "Delete road");
}

std::unique_ptr<Command> changeRoad(const MapModel::Map &map, std::int32_t first, const std::string &type, std::int32_t style)
{
	const std::vector<std::int32_t> segments = roadSegments(map, SegmentKind::Both);
	if (std::find(segments.begin(), segments.end(), first) == segments.end())
		return nullptr;
	const bool bridge = isBridgeSegment(map, first);
	const std::int32_t styleFlags = bridge ? 0 : (ROAD_CORNER_FLAGS | ROAD_JOIN);
	std::vector<EditObjects::Edit> edits;
	for (std::int32_t i : {first, first + 1})
	{
		const MapModel::MapObject &o = (*map.objects)[static_cast<std::size_t>(i)];
		MapModel::MapObject after = o;
		after.templateName = type;
		after.flags = (o.flags & ~styleFlags) | (style & styleFlags);
		if (after != o)
			edits.push_back({i, o, std::move(after)});
	}
	if (edits.empty())
		return nullptr;
	return std::make_unique<EditObjects>("Change road", std::move(edits), true);
}

} // namespace MapEditorCore
