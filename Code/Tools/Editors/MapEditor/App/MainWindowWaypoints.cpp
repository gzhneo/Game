/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowWaypoints.cpp -- the waypoint tool (the entry "Waypoint" of the catalog): clicks place
// waypoints along a path, a click on a waypoint links or unlinks it with the selected one; linking
// and unlinking the selected waypoints from the menu; the self test of all that.

#include "App/MainWindow.h"
#include "App/MainWindowParts.h"

#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/Viewport3D.h"
#include "Core/WaypointCommands.h"


#include <algorithm>
#include <string>

namespace MapEditorApp
{

namespace
{

} // namespace

bool waypointAt(const MapModel::Map &map, int index)
{
	return map.objects && index >= 0 && static_cast<std::size_t>(index) < map.objects->size()
		&& MapEditorCore::isWaypoint((*map.objects)[static_cast<std::size_t>(index)]);
}

bool MainWindow::runCommand(std::unique_ptr<MapEditorCore::Command> command)
{
	if (!m_state.map || !command)
		return false;
	MapEditorCore::Command *raw = command.get();
	m_state.map->history.execute(std::move(command), m_state.map->map());
	applyChanges(raw->changes());
	return true;
}

void MainWindow::placeWaypoint(float x, float y, int object, bool ctrl)
{
	const MapModel::Map &map = m_state.map->map();
	MapEditorCore::Selection &sel = m_state.map->selection;
	const int current = sel.objects.size() == 1 && waypointAt(map, sel.objects[0]) ? sel.objects[0] : -1;
	if (waypointAt(map, object))
	{
		if (!ctrl && current >= 0 && current != object)
			runCommand(MapEditorCore::toggleWaypointLink(map, current, object));
		// The clicked waypoint is where the path goes on.
		sel.set(object);
		selectionChanged();
		return;
	}
	runCommand(MapEditorCore::newWaypoint(map, x, y, ctrl ? -1 : current));
}

void MainWindow::linkSelectedWaypoints()
{
	if (!m_state.map)
		return;
	if (!runCommand(MapEditorCore::chainWaypoints(m_state.map->map(), m_state.map->selection.objects)))
		toast(T("Select at least two waypoints that are not linked yet."));
}

void MainWindow::unlinkSelectedWaypoints()
{
	if (!m_state.map)
		return;
	if (!runCommand(MapEditorCore::unlinkWaypoints(m_state.map->map(), m_state.map->selection.objects)))
		toast(T("The selected waypoints are not linked with each other."));
}

} // namespace MapEditorApp
