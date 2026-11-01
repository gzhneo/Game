/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// AreaCommands.h -- trigger and water areas (Map::polygons): adding, changing and deleting them as
// one step each, and the geometry the area tool needs (points, edges, inside). Names and IDs follow
// the old WorldBuilder ("Area <id>", "Water Area <id>").

#pragma once

#include "Core/History.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MapEditorCore
{

// Replaces the list of areas; the command keeps the list before and after (areas are few and small).
class EditAreas : public Command
{
public:
	EditAreas(std::string name, std::optional<std::vector<MapModel::PolygonArea>> before,
		std::optional<std::vector<MapModel::PolygonArea>> after, std::int32_t area)
		: m_name(std::move(name)), m_before(std::move(before)), m_after(std::move(after)), m_area(area) {}

	virtual void apply(MapModel::Map &map) override { map.polygons = m_after; }
	virtual void revert(MapModel::Map &map) override { map.polygons = m_before; }
	virtual std::string name() const override { return m_name; }
	virtual Changes changes() const override;

	// Index of the area the step is about (-1 after deleting it).
	std::int32_t area() const { return m_area; }

private:
	std::string m_name;
	std::optional<std::vector<MapModel::PolygonArea>> m_before;
	std::optional<std::vector<MapModel::PolygonArea>> m_after;
	std::int32_t m_area = -1;
};

// Adds the area with the next free ID; an empty name becomes "Area <id>" or "Water Area <id>".
std::unique_ptr<EditAreas> addArea(const MapModel::Map &map, MapModel::PolygonArea area);
// Replaces area index by the changed one. name: menu text ("Move point", ...). Null if unchanged.
std::unique_ptr<EditAreas> changeArea(const MapModel::Map &map, std::int32_t index, const MapModel::PolygonArea &after,
	const std::string &name);
std::unique_ptr<EditAreas> deleteArea(const MapModel::Map &map, std::int32_t index);

// Index of the point of the area within tolerance of (x, y), the nearest; -1 if none.
[[nodiscard]] int areaPointAt(const MapModel::PolygonArea &area, float x, float y, float tolerance);
// Index where a point on an edge within tolerance of (x, y) is inserted (after the edge's first
// point); -1 if no edge is that near.
[[nodiscard]] int areaEdgeAt(const MapModel::PolygonArea &area, float x, float y, float tolerance);
[[nodiscard]] bool areaContains(const MapModel::PolygonArea &area, float x, float y);

// Height of the ground at the world position (height map, bilinear); 0 without a height map.
[[nodiscard]] float groundHeight(const MapModel::Map &map, float x, float y);

} // namespace MapEditorCore
