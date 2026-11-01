/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowScripts.cpp -- the scripts window (Windows/ScriptsWindow) on the open map, and changing the
// scripts as steps of the history for it and the other parts (EditorContext).

#include "App/MainWindow.h"

#include "App/Language.h"
#include "App/Viewport3D.h"
#include "App/Windows/ScriptsWindow.h"

#include <QDir>
#include <QFileInfo>

namespace MapEditorApp
{

const MapModel::MapObject *MainWindow::firstSelectedObject() const
{
	if (!m_state.map || m_state.map->selection.empty() || !m_state.map->map().objects)
		return nullptr;
	const std::int32_t index = m_state.map->selection.objects.front();
	const std::vector<MapModel::MapObject> &objects = *m_state.map->map().objects;
	return index >= 0 && index < static_cast<std::int32_t>(objects.size()) ? &objects[static_cast<std::size_t>(index)] : nullptr;
}

bool MainWindow::editScripts(const char *name, const std::function<bool(MapModel::Sides &)> &change, bool mergeable, bool refresh)
{
	if (!m_state.map || !m_state.map->map().sides)
		return false;
	MapModel::Map &map = m_state.map->map();
	std::vector<MapModel::ScriptList> before = MapEditorCore::scriptLists(map);
	if (!change(*map.sides))
		return false;
	auto step = MapEditorCore::scriptsStep(map, std::move(before), name, mergeable);
	if (!step)
		return false;
	const MapEditorCore::Changes changes = step->changes();
	m_state.map->history.record(std::move(step));
	m_scriptsEditing = !refresh;
	applyChanges(changes);
	m_scriptsEditing = false;
	return true;
}

void MainWindow::scriptsChanged()
{
	fillSidesTree();
	if (m_scriptsWindow && !m_scriptsEditing)
		m_scriptsWindow->refresh();
	updateTitle();
}

ScriptsWindow *MainWindow::scriptsWindow()
{
	if (!m_scriptsWindow)
		m_scriptsWindow = new ScriptsWindow(*this, m_scriptCatalog, this);
	return m_scriptsWindow;
}

void MainWindow::showScripts(const MapEditorCore::ScriptPath *at)
{
	if (!m_state.map)
		return;
	if (!m_state.map->map().sides)
	{
		toast(T("The map has no players, so it has no scripts."));
		return;
	}
	ScriptsWindow *window = scriptsWindow();
	if (at)
		window->setPath(*at);
	window->refresh();
	window->show();
	window->raise();
}

OpenMap *MainWindow::openMap()
{
	return m_state.map.get();
}

QString MainWindow::mapFolder() const
{
	return !m_state.map || m_state.map->path.isEmpty() ? m_state.userMapFolder(m_state.game) : QFileInfo(m_state.map->path).dir().path();
}

bool MainWindow::hasView() const
{
	return m_viewport && m_viewport->hasDevice() && m_viewMapSerial == m_mapSerial;
}

void MainWindow::recordStep(std::unique_ptr<MapEditorCore::Command> step)
{
	if (!m_state.map || !step)
		return;
	const MapEditorCore::Changes changes = step->changes();
	m_state.map->history.record(std::move(step));
	applyChanges(changes);
}

} // namespace MapEditorApp
