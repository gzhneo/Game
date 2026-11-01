/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ScriptExchange.h -- script export and import (.scb) as the old WorldBuilder does them
// (ScriptDialog::OnSave, OnLoad, ExportScriptsOptions): all scripts or a selection, with the
// waypoints (and the waypoints linked to them), areas, units and teams the scripts name, and the
// players with their settings. Import adds what the map does not have yet.
//
// Differences to the old editor: an import from several players puts each list into the player of
// the same name (the old loop compared the wrong player), links to waypoints the map already had
// stay linked, a renamed duplicate waypoint name is really renamed, false actions count as
// references too, and the objects of the map are not marked with exportWithScript.

#pragma once

#include "Core/ScriptCommands.h"
#include "Model/MapIO.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MapEditorCore
{

struct ScriptExport
{
	bool allScripts = true;			// false: only the selection (a player's list, a group or a script)
	bool waypoints = true;
	bool triggers = true;
	bool units = true;
	bool sides = true;				// the players with their settings
	ScriptPath selection;
};

// nullopt if a selection export has nothing selected.
[[nodiscard]] std::optional<MapModel::ScriptFile> exportScripts(const MapModel::Map &map, const ScriptExport &options);

struct ScriptImportReport
{
	int scripts = 0;
	int groups = 0;
	int objects = 0;
	int areas = 0;
	int teams = 0;
	int players = 0;
	std::vector<std::string> messages;
};

// Adds the file's content to the map; a selection export goes to selectedSide.
ScriptImportReport importScripts(MapModel::Map &map, const MapModel::ScriptFile &file, int selectedSide);

// Players, teams, scripts, objects, areas and waypoint links before and after, as one step.
class EditMapParts : public Command
{
public:
	struct Parts
	{
		std::optional<MapModel::Sides> sides;
		std::optional<std::vector<MapModel::MapObject>> objects;
		std::optional<std::vector<MapModel::PolygonArea>> polygons;
		std::optional<std::vector<MapModel::WaypointLink>> waypointLinks;
	};

	EditMapParts(std::string name, Parts before, Parts after) : m_name(std::move(name)), m_before(std::move(before)), m_after(std::move(after)) {}

	virtual void apply(MapModel::Map &map) override { set(map, m_after); }
	virtual void revert(MapModel::Map &map) override { set(map, m_before); }
	virtual std::string name() const override { return m_name; }
	virtual Changes changes() const override;

private:
	static void set(MapModel::Map &map, const Parts &parts);

	std::string m_name;
	Parts m_before;
	Parts m_after;
};

EditMapParts::Parts mapParts(const MapModel::Map &map);
// Null if nothing changed.
std::unique_ptr<EditMapParts> mapPartsStep(const MapModel::Map &map, EditMapParts::Parts before, std::string name);

} // namespace MapEditorCore
