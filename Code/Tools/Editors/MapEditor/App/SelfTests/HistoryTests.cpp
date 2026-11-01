/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// HistoryTests.cpp -- the self test of the history window (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "App/Windows/HistoryWindow.h"
#include "Core/ObjectCommands.h"


#include <memory>
#include <QListView>

namespace MapEditorApp
{

bool MainWindow::selfTestHistory(QStringList &report)
{
	TestSteps step(report, "history");
	if (!m_state.map || !m_state.map->map().objects || m_state.map->map().objects->empty())
		return true;
	MapEditorCore::History &history = m_state.map->history;
	std::vector<MapModel::MapObject> &objects = *m_state.map->map().objects;
	const std::size_t start = history.undoCount();
	const float x0 = objects[0].x;
	// Three moves of the first object, so the list has three new steps.
	for (int i = 1; i <= 3; ++i)
	{
		MapEditorCore::EditObjects::Edit edit;
		edit.before = objects[0];
		edit.after = objects[0];
		edit.after.x = x0 + 10.0f * static_cast<float>(i);
		runCommand(std::make_unique<MapEditorCore::EditObjects>("Move object", std::vector<MapEditorCore::EditObjects::Edit>{edit}));
	}
	showHistory();
	const auto rows = [this] { return m_historyWindow->model()->rowCount(); };
	const auto current = [this] { return m_historyWindow->view()->currentIndex().row(); };
	step(rows() == static_cast<int>(history.names().size()) + 1 && current() == static_cast<int>(start) + 3,
		QStringLiteral("the list shows %1 rows, the current one is row %2").arg(rows()).arg(current()));
	goToHistory(static_cast<int>(start) + 1);
	step(history.undoCount() == start + 1 && objects[0].x == x0 + 10.0f && history.canRedo(),
		QStringLiteral("a click on the first move undoes the two later ones in one go"));
	goToHistory(static_cast<int>(start) + 3);
	step(history.undoCount() == start + 3 && objects[0].x == x0 + 30.0f, QStringLiteral("a click on the last move redoes both"));
	goToHistory(static_cast<int>(start));
	step(history.undoCount() == start && objects[0].x == x0 && current() == static_cast<int>(start),
		QStringLiteral("back to the state before the moves"));
	m_historyWindow->close();
	return step.ok();
}

} // namespace MapEditorApp
