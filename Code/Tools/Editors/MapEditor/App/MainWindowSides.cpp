/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowSides.cpp -- the players and teams window (Windows/SidesWindow) on the open map, changing
// the players and teams as steps of the history, and the tree of players and teams in the side panel.

#include "App/MainWindow.h"

#include "App/Language.h"
#include "App/Widgets.h"
#include "App/Windows/ScriptsWindow.h"
#include "App/Windows/SidesWindow.h"
#include "Core/SidesCommands.h"

#include <QTreeWidget>

namespace MapEditorApp
{

namespace
{

constexpr int ROLE_INDEX = Qt::UserRole + 1;
constexpr int ROLE_SCRIPTS = Qt::UserRole + 2;

} // namespace

bool MainWindow::editSides(const char *name, const std::function<bool(MapModel::Map &)> &change, bool mergeable, bool refresh)
{
	if (!m_state.map)
		return false;
	MapModel::Map &map = m_state.map->map();
	const MapEditorCore::EditSides::State before = MapEditorCore::sidesState(map);
	if (!map.sides)
	{
		map.sides = MapModel::Sides{};
		MapEditorCore::validateSides(*map.sides);
	}
	if (!change(map))
	{
		map.sides = before.sides;
		return false;
	}
	auto step = MapEditorCore::sidesStep(map, before, name, mergeable);
	if (!step)
		return false;
	const MapEditorCore::Changes changes = step->changes();
	m_state.map->history.record(std::move(step));
	m_sidesEditing = !refresh;
	applyChanges(changes);
	m_sidesEditing = false;
	return true;
}

void MainWindow::sidesChanged()
{
	fillSidesTree();
	if (m_sidesWindow && !m_sidesEditing)
		m_sidesWindow->refresh();
	if (m_scriptsWindow)
		m_scriptsWindow->refresh();
}

void MainWindow::fillSidesTree()
{
	if (!m_sidesTree || !m_state.map)
		return;
	QTreeWidget *tree = m_sidesTree;
	tree->clear();
	const MapModel::Map &map = m_state.map->map();
	if (!map.sides)
		return;
	std::vector<QTreeWidgetItem *> players;
	for (std::size_t i = 0; i < map.sides->sides.size(); ++i)
	{
		const MapModel::Side &side = map.sides->sides[i];
		QString text = playerLabel(side);
		const QString display = displayName(side);
		if (!MapEditorCore::playerName(side).empty() && !display.isEmpty() && display != text)
			text += QStringLiteral(" \u2013 ") + display;
		auto *item = new QTreeWidgetItem(tree, {text});
		item->setData(0, ROLE_INDEX, static_cast<int>(i));
		std::size_t scripts = side.scripts.scripts.size();
		for (const auto &g : side.scripts.groups)
			scripts += g.scripts.size();
		if (!side.buildList.empty())
			new QTreeWidgetItem(item, {T("Build list: %1").arg(side.buildList.size())});
		if (scripts > 0)
		{
			auto *scriptItem = new QTreeWidgetItem(item, {T("Scripts: %1").arg(scripts)});
			scriptItem->setData(0, ROLE_SCRIPTS, static_cast<int>(i));
		}
		players.push_back(item);
	}
	for (std::size_t t = 0; t < map.sides->teams.size(); ++t)
	{
		const MapModel::PropertyMap &team = map.sides->teams[t];
		const int owner = MapEditorCore::findPlayer(*map.sides, MapEditorCore::teamOwner(team));
		QTreeWidgetItem *parent = owner >= 0 ? players[static_cast<std::size_t>(owner)] : nullptr;
		auto *item = parent ? new QTreeWidgetItem(parent, {T("Team %1").arg(utf8(MapEditorCore::teamName(team)))})
							: new QTreeWidgetItem(tree, {T("Team %1").arg(utf8(MapEditorCore::teamName(team)))});
		item->setData(0, ROLE_INDEX, -2 - static_cast<int>(t));
	}
	tree->expandAll();
}

void MainWindow::selectTeamMembers(int teamIndex)
{
	const MapModel::Map &map = m_state.map->map();
	if (!map.sides || teamIndex < 0 || teamIndex >= static_cast<int>(map.sides->teams.size()))
		return;
	const std::string team = MapEditorCore::teamName(map.sides->teams[static_cast<std::size_t>(teamIndex)]);
	m_state.map->selection.objects = MapEditorCore::teamObjects(map, team);
	selectionChanged();
	if (!m_state.map->selection.empty())
		centerOnSelection();
	toast(T("%1 objects of team %2 selected").arg(m_state.map->selection.objects.size()).arg(utf8(team)));
}

SidesWindow *MainWindow::sidesWindow()
{
	if (!m_sidesWindow)
		m_sidesWindow = new SidesWindow(*this, this);
	return m_sidesWindow;
}

void MainWindow::showSides(int tab)
{
	if (!m_state.map)
		return;
	SidesWindow *window = sidesWindow();
	if (tab >= 0)
		window->setTab(tab);
	window->refresh();
	window->show();
	window->raise();
}

void MainWindow::selectTeam(int teamIndex)
{
	selectTeamMembers(teamIndex);
}

Game MainWindow::game() const
{
	return m_state.game;
}

} // namespace MapEditorApp
