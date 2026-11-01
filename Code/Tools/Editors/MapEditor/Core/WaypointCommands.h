/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WaypointCommands.h -- waypoints: new ones, links between them, and the path properties (path
// labels, both directions) that all waypoints of a connected path share, as the old WorldBuilder
// keeps them (CWorldBuilderDoc::updateLinkedWaypointLabels).

#pragma once

#include "Core/ObjectCommands.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MapEditorCore
{

// Template name the game and the old WorldBuilder give waypoints.
inline constexpr const char *WAYPOINT_TEMPLATE = "*Waypoints/Waypoint";

// The properties a path shares.
inline constexpr const char *PATH_KEYS[] = {"waypointPathLabel1", "waypointPathLabel2", "waypointPathLabel3", "waypointPathBiDirectional"};

[[nodiscard]] bool isWaypoint(const MapModel::MapObject &o);

// Changes the link list and waypoint properties in one step; undo restores both.
class EditWaypoints : public Command
{
public:
	EditWaypoints(std::string name, std::optional<std::vector<MapModel::WaypointLink>> linksBefore,
		std::optional<std::vector<MapModel::WaypointLink>> linksAfter, std::vector<EditObjects::Edit> edits)
		: m_name(std::move(name)), m_linksBefore(std::move(linksBefore)), m_linksAfter(std::move(linksAfter)), m_edits(std::move(edits)) {}

	virtual void apply(MapModel::Map &map) override;
	virtual void revert(MapModel::Map &map) override;
	virtual std::string name() const override { return m_name; }
	virtual Changes changes() const override;

private:
	std::string m_name;
	std::optional<std::vector<MapModel::WaypointLink>> m_linksBefore;
	std::optional<std::vector<MapModel::WaypointLink>> m_linksAfter;
	std::vector<EditObjects::Edit> m_edits;
};

// Indices of the waypoints linked (either direction) to the waypoint, without itself.
[[nodiscard]] std::vector<std::int32_t> linkedWaypoints(const MapModel::Map &map, std::int32_t index);
// Indices of all waypoints on the connected path of the waypoint, itself included.
[[nodiscard]] std::vector<std::int32_t> waypointPath(const MapModel::Map &map, std::int32_t index);

// A new waypoint at (x, y) with the next free ID and name ("Waypoint <id>"), neutral. With a
// waypoint linkFrom (index), it is linked from there and takes over its path properties.
std::unique_ptr<InsertObjects> newWaypoint(const MapModel::Map &map, float x, float y, std::int32_t linkFrom = -1);

// Links from -> to, or removes the link between them (either direction) if there is one. When the
// link joins two paths, the path of to takes over the path properties of from. Null if either is
// not a waypoint or both are the same.
std::unique_ptr<EditWaypoints> toggleWaypointLink(const MapModel::Map &map, std::int32_t from, std::int32_t to);

// Links the waypoints in this order where they are not linked yet; the path takes over the path
// properties of the first. Null if nothing is new.
std::unique_ptr<EditWaypoints> chainWaypoints(const MapModel::Map &map, const std::vector<std::int32_t> &indices);

// Removes every link between the given waypoints (both ends among them). Null if there is none.
std::unique_ptr<EditWaypoints> unlinkWaypoints(const MapModel::Map &map, const std::vector<std::int32_t> &indices);

// Sets (or with no value removes) a path property on every waypoint of the path of the waypoint.
// Null if nothing changes.
std::unique_ptr<EditWaypoints> setPathProperty(const MapModel::Map &map, std::int32_t index, const std::string &key,
	const std::optional<MapModel::PropertyValue> &value);

} // namespace MapEditorCore
