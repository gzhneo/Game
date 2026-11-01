/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowHistory.cpp -- the undo history window (Windows/HistoryWindow) on the open map: a click
// goes to that state in one go, undoing or redoing as often as needed.

#include "App/MainWindow.h"
#include "App/Windows/HistoryWindow.h"

#include <algorithm>

namespace MapEditorApp
{

void MainWindow::showHistory()
{
	if (!m_historyWindow)
	{
		m_historyWindow = new HistoryWindow([](const std::string &name) { return stepName(name); }, this);
		connect(m_historyWindow, &HistoryWindow::goTo, this, [this](int position) { goToHistory(position); });
	}
	refreshHistoryWindow();
	m_historyWindow->show();
	m_historyWindow->raise();
}

void MainWindow::refreshHistoryWindow()
{
	if (m_historyWindow)
		m_historyWindow->setHistory(m_state.map ? &m_state.map->history : nullptr);
}

void MainWindow::goToHistory(int position)
{
	if (!m_state.map || position < 0)
		return;
	finishMold(false);
	MapEditorCore::History &history = m_state.map->history;
	const std::size_t target = (std::min)(static_cast<std::size_t>(position), history.names().size());
	// undo() and redo() refuse while a drag runs; the loop then stops where it is.
	while (history.undoCount() > target)
	{
		const std::size_t before = history.undoCount();
		undo();
		if (history.undoCount() == before)
			break;
	}
	while (history.undoCount() < target)
	{
		const std::size_t before = history.undoCount();
		redo();
		if (history.undoCount() == before)
			break;
	}
	refreshHistoryWindow();
}

} // namespace MapEditorApp
