/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowFairness.cpp -- the fairness report (Windows/FairnessWindow) on the open map; its territory
// colours lie over the 2D overview and the 3D view.

#include "App/MainWindow.h"

#include "App/MapOverview.h"
#include "App/Windows/FairnessWindow.h"

namespace MapEditorApp
{

void MainWindow::showTerritories(const std::vector<std::uint32_t> &tint)
{
	m_territoryTint = tint;
	if (m_overview)
		m_overview->setTint(m_territoryTint);
	updateImpassableView();
}

void MainWindow::showFairness()
{
	if (!m_state.map)
		return;
	if (!m_fairnessWindow)
	{
		m_fairnessWindow = new FairnessWindow(this);
		connect(m_fairnessWindow, &FairnessWindow::centerOn, this, [this](float x, float y) { centerOn(x, y); });
		connect(m_fairnessWindow, &FairnessWindow::territoriesChanged, this, [this](const std::vector<std::uint32_t> &tint) { showTerritories(tint); });
	}
	m_fairnessWindow->setMap(&m_state.map->map());
	m_fairnessWindow->measure();
	m_fairnessWindow->show();
	m_fairnessWindow->raise();
}

} // namespace MapEditorApp
