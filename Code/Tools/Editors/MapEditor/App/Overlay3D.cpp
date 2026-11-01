/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Overlay3D.cpp -- shapes of the 3D overlay from the map model.

#include "App/Common.h"
#include "App/Overlay3D.h"

#include "Core/PlacementCommands.h"
#include "Core/RoadCommands.h"
#include "Core/WaypointCommands.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>

namespace MapEditorApp
{

namespace
{

constexpr std::uint32_t BOUNDARY_COLOR = 0xD0FFFFFFu;
constexpr std::uint32_t WAYPOINT_COLOR = 0xE0B58CFFu;
constexpr std::uint32_t LINK_COLOR = 0xB0B58CFFu;
constexpr std::uint32_t START_COLOR = 0xFFFFD24Au;
constexpr std::uint32_t WATER_COLOR = 0xE04FA3D8u;
constexpr std::uint32_t AREA_COLOR = 0xD0F2C94Cu;
constexpr std::uint32_t SELECTION_COLOR = 0xFF45C8FFu;
constexpr std::uint32_t SOUND_COLOR = 0xE07ED67Eu;
constexpr std::uint32_t SOUND_NEAR_COLOR = 0x907ED67Eu;
constexpr std::uint32_t BOUNDS_COLOR = 0xFFAA00AAu;
constexpr std::uint32_t SIGHT_COLOR = 0xFFF0F0F0u;
constexpr std::uint32_t WEAPON_COLORS[3] = {0xFF00FF00u, 0xFFE0F00Au, 0xFFFF0000u};
constexpr std::uint32_t TEST_ART_COLOR = 0xFFA000A0u;
constexpr std::uint32_t LIGHT_COLOR = 0xE0FFE08Au;
constexpr float TEST_ART_RADIUS = 30.0f;

class Builder
{
public:
	OverlayShapes result;

	void addShape(std::int32_t kind, std::uint32_t color, std::int32_t first, float size)
	{
		const std::int32_t count = static_cast<std::int32_t>(result.points.size() / 3) - first;
		if (count > 0)
			result.shapes.push_back(MeOverlayShape{kind, color, first, count, size});
	}
	std::int32_t mark() const { return static_cast<std::int32_t>(result.points.size() / 3); }
	void point(float x, float y, float z = ME_OVERLAY_ON_TERRAIN)
	{
		result.points.push_back(x);
		result.points.push_back(y);
		result.points.push_back(z);
	}
};

bool isStartPosition(const std::string &name)
{
	return name.rfind("Player_", 0) == 0 && name.find("_Start") != std::string::npos;
}

} // namespace

OverlayShapes buildOverlay(const MapModel::Map &map, const OverlayOptions &options, const std::vector<SelectionMark> &selection,
	const AreaMarks &areas, const SoundRangeOf &soundRange, const TemplateOf &templateOf)
{
	Builder b;
	const auto circle = [&b](float x, float y, float r, std::uint32_t color, float width) {
		constexpr int SEGMENTS = 40;
		const std::int32_t first = b.mark();
		for (int i = 0; i < SEGMENTS; ++i)
		{
			const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(SEGMENTS);
			b.point(x + r * std::cos(a), y + r * std::sin(a));
		}
		b.addShape(ME_OVERLAY_LOOP, color, first, width);
	};

	const auto areaPoint = [&b](const MapModel::PolygonArea &area, const std::array<std::int32_t, 3> &p) {
		// Water areas lie on their water level, which may be above the ground.
		b.point(static_cast<float>(p[0]), static_cast<float>(p[1]), area.isWater ? static_cast<float>(p[2]) : ME_OVERLAY_ON_TERRAIN);
	};
	const auto areaHandles = [&](const MapModel::PolygonArea &area, int selectedPoint) {
		for (std::size_t i = 0; i < area.points.size(); ++i)
		{
			const std::int32_t first = b.mark();
			areaPoint(area, area.points[i]);
			b.addShape(ME_OVERLAY_MARKER, SELECTION_COLOR, first, static_cast<int>(i) == selectedPoint ? 7.0f : 4.0f);
		}
	};
	// The area of the tool is shown whatever the options say.
	for (std::size_t index = 0; map.polygons && index < map.polygons->size(); ++index)
	{
		const MapModel::PolygonArea &area = (*map.polygons)[index];
		if (static_cast<int>(index) != areas.area || area.points.size() < 2)
			continue;
		const std::int32_t first = b.mark();
		for (const auto &p : area.points)
			areaPoint(area, p);
		b.addShape(ME_OVERLAY_LOOP, SELECTION_COLOR, first, 3.0f);
		areaHandles(area, areas.point);
	}
	for (std::size_t i = 0; i + 3 < areas.lines.size(); i += 4)
	{
		const std::int32_t first = b.mark();
		b.point(areas.lines[i], areas.lines[i + 1]);
		b.point(areas.lines[i + 2], areas.lines[i + 3]);
		b.addShape(ME_OVERLAY_PATH, SELECTION_COLOR, first, 3.0f);
		for (std::size_t end = 0; end < 2; ++end)
		{
			const std::int32_t marker = b.mark();
			b.point(areas.lines[i + end * 2], areas.lines[i + end * 2 + 1]);
			b.addShape(ME_OVERLAY_MARKER, SELECTION_COLOR, marker, 5.0f);
		}
	}
	if (areas.circle.size() >= 6)
	{
		const std::int32_t first = b.mark();
		for (std::size_t i = 0; i + 1 < areas.circle.size(); i += 2)
			b.point(areas.circle[i], areas.circle[i + 1]);
		b.addShape(ME_OVERLAY_LOOP, SELECTION_COLOR, first, 2.0f);
	}
	if (areas.draft && !areas.draft->points.empty())
	{
		const std::int32_t first = b.mark();
		for (const auto &p : areas.draft->points)
			areaPoint(*areas.draft, p);
		b.addShape(ME_OVERLAY_PATH, SELECTION_COLOR, first, 2.5f);
		areaHandles(*areas.draft, -1);
	}
	if (options.areas && map.polygons)
	{
		for (std::size_t index = 0; index < map.polygons->size(); ++index)
		{
			const MapModel::PolygonArea &area = (*map.polygons)[index];
			if (area.points.size() < 2 || static_cast<int>(index) == areas.area)
				continue;
			const std::int32_t first = b.mark();
			for (const auto &p : area.points)
			{
				// Water areas lie on their water level, which may be above the ground.
				b.point(static_cast<float>(p[0]), static_cast<float>(p[1]),
					area.isWater ? static_cast<float>(p[2]) : ME_OVERLAY_ON_TERRAIN);
			}
			b.addShape(ME_OVERLAY_LOOP, area.isWater ? WATER_COLOR : AREA_COLOR, first, 2.0f);
		}
	}

	if (options.boundaries && map.heightMap)
	{
		for (const MapModel::Point2 &corner : map.heightMap->boundaries)
		{
			if (corner.x <= 0 || corner.y <= 0)
				continue;
			const float w = static_cast<float>(corner.x) * CELL;
			const float h = static_cast<float>(corner.y) * CELL;
			const std::int32_t first = b.mark();
			b.point(0.0f, 0.0f);
			b.point(w, 0.0f);
			b.point(w, h);
			b.point(0.0f, h);
			b.addShape(ME_OVERLAY_LOOP, BOUNDARY_COLOR, first, 2.5f);
		}
	}

	if (options.waypoints && map.objects)
	{
		std::map<std::int32_t, const MapModel::MapObject *> byId;
		for (const MapModel::MapObject &o : *map.objects)
		{
			if (const auto id = o.properties.getInt("waypointID"))
				byId[*id] = &o;
		}
		if (map.waypointLinks)
		{
			for (const MapModel::WaypointLink &link : *map.waypointLinks)
			{
				const auto from = byId.find(link.from);
				const auto to = byId.find(link.to);
				if (from == byId.end() || to == byId.end())
					continue;
				const std::int32_t first = b.mark();
				b.point(from->second->x, from->second->y);
				b.point(to->second->x, to->second->y);
				b.addShape(ME_OVERLAY_PATH, LINK_COLOR, first, 2.0f);
				// Arrow halfway in the direction of the link, sized to the link (world units).
				const float dx = to->second->x - from->second->x;
				const float dy = to->second->y - from->second->y;
				const float length = std::sqrt(dx * dx + dy * dy);
				if (length < 8.0f)
					continue;
				const float ux = dx / length;
				const float uy = dy / length;
				const float wing = std::clamp(length * 0.12f, 4.0f, 14.0f);
				const float tipX = from->second->x + dx * 0.55f;
				const float tipY = from->second->y + dy * 0.55f;
				const std::int32_t arrow = b.mark();
				b.point(tipX - ux * wing - uy * wing * 0.7f, tipY - uy * wing + ux * wing * 0.7f);
				b.point(tipX, tipY);
				b.point(tipX - ux * wing + uy * wing * 0.7f, tipY - uy * wing - ux * wing * 0.7f);
				b.addShape(ME_OVERLAY_PATH, LINK_COLOR, arrow, 2.0f);
			}
		}
		for (const auto &[id, o] : byId)
		{
			const bool start = isStartPosition(o->properties.getAscii("waypointName").value_or(""));
			const std::int32_t first = b.mark();
			b.point(o->x, o->y);
			b.addShape(ME_OVERLAY_MARKER, start ? START_COLOR : WAYPOINT_COLOR, first, start ? 10.0f : 5.0f);
		}
	}
	// Footprints, sight and weapon ranges from the object's template; objects the game lacks.
	const bool perObject = options.boundingBoxes || options.sightRanges || options.weaponRanges || options.testArt;
	if (perObject && templateOf && map.objects)
	{
		for (const MapModel::MapObject &o : *map.objects)
		{
			if (o.templateName.empty() || o.templateName[0] == '*' || MapEditorCore::isWaypoint(o) || MapEditorCore::isRoadOrBridgePoint(o)
				|| MapEditorCore::isScorch(o))
				continue;
			const MeTemplateInfo *info = templateOf(o.templateName);
			if (!info)
			{
				if (options.testArt)
				{
					circle(o.x, o.y, TEST_ART_RADIUS, TEST_ART_COLOR, 2.0f);
					const std::int32_t first = b.mark();
					b.point(o.x, o.y);
					b.addShape(ME_OVERLAY_MARKER, TEST_ART_COLOR, first, 6.0f);
				}
				continue;
			}
			if (options.boundingBoxes && info->majorRadius > 0.0f)
			{
				if (info->geometry == 2)
				{
					// Major radius along the facing, minor across it.
					const float c = std::cos(o.angle);
					const float s = std::sin(o.angle);
					const float ax = info->majorRadius * c, ay = info->majorRadius * s;
					const float bx = -info->minorRadius * s, by = info->minorRadius * c;
					const std::int32_t first = b.mark();
					b.point(o.x + ax + bx, o.y + ay + by);
					b.point(o.x - ax + bx, o.y - ay + by);
					b.point(o.x - ax - bx, o.y - ay - by);
					b.point(o.x + ax - bx, o.y + ay - by);
					b.addShape(ME_OVERLAY_LOOP, BOUNDS_COLOR, first, 1.5f);
				}
				else
					circle(o.x, o.y, info->majorRadius, BOUNDS_COLOR, 1.5f);
			}
			if (options.sightRanges && info->visionRange > 0.0f)
				circle(o.x, o.y, info->visionRange, SIGHT_COLOR, 1.0f);
			if (options.weaponRanges)
			{
				for (int slot = 0; slot < 3; ++slot)
					if (info->weaponRange[slot] > 0.0f)
						circle(o.x, o.y, info->weaponRange[slot], WEAPON_COLORS[slot], 1.0f);
			}
		}
	}
	// Ambient sounds: full volume inside the inner circle, silent beyond the outer one.
	if (options.sounds && soundRange && map.objects)
	{
		for (const MapModel::MapObject &o : *map.objects)
		{
			float minRange = 0.0f;
			float maxRange = 0.0f;
			if (!soundRange(o, minRange, maxRange))
				continue;
			const std::int32_t first = b.mark();
			b.point(o.x, o.y);
			b.addShape(ME_OVERLAY_MARKER, SOUND_COLOR, first, 5.0f);
			if (maxRange > 0.0f)
				circle(o.x, o.y, maxRange, SOUND_COLOR, 1.5f);
			if (minRange > 0.0f && minRange < maxRange)
				circle(o.x, o.y, minRange, SOUND_NEAR_COLOR, 1.0f);
		}
	}
	// Selection: a ring around each selected object and a line in its facing, drawn last so they lie
	// on top.
	for (const SelectionMark &mark : selection)
	{
		if (!map.objects || mark.object < 0 || static_cast<std::size_t>(mark.object) >= map.objects->size())
			continue;
		const MapModel::MapObject &o = (*map.objects)[static_cast<std::size_t>(mark.object)];
		const float r = mark.radius * 1.1f + 2.0f;
		circle(o.x, o.y, r, SELECTION_COLOR, 3.0f);
		// A light: full brightness inside, dark beyond the outer circle.
		if (o.properties.getReal("lightHeightAboveTerrain"))
		{
			const float inner = o.properties.getReal("lightInnerRadius").value_or(0.0f);
			const float outer = o.properties.getReal("lightOuterRadius").value_or(0.0f);
			if (outer > 0.0f)
				circle(o.x, o.y, outer, LIGHT_COLOR, 1.5f);
			if (inner > 0.0f && inner < outer)
				circle(o.x, o.y, inner, LIGHT_COLOR, 1.0f);
		}
		// Facing: models look along +x at angle 0.
		const float length = r + std::max(6.0f, r * 0.5f);
		const std::int32_t facing = b.mark();
		b.point(o.x, o.y);
		b.point(o.x + length * std::cos(o.angle), o.y + length * std::sin(o.angle));
		b.addShape(ME_OVERLAY_PATH, SELECTION_COLOR, facing, 3.0f);
	}
	return b.result;
}

} // namespace MapEditorApp
