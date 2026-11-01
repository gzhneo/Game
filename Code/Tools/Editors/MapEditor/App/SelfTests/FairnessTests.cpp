/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FairnessTests.cpp -- the self test of the fairness window (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "App/Windows/FairnessWindow.h"


#include <algorithm>

namespace MapEditorApp
{

bool MainWindow::selfTestFairness(QStringList &report)
{
	TestSteps step(report, "fairness");
	if (!m_state.map || !m_state.map->map().heightMap)
		return true;
	showFairness();
	const MapEditorCore::FairnessReport &fair = m_fairnessWindow->report();
	step(m_fairnessWindow->startsView()->topLevelItemCount() == static_cast<int>(fair.starts.size())
			&& m_fairnessWindow->resourcesView()->topLevelItemCount() == static_cast<int>(fair.resources.size()) && !m_fairnessWindow->summary().isEmpty(),
		QStringLiteral("the report lists %1 starts and %2 buildings: %3").arg(fair.starts.size()).arg(fair.resources.size()).arg(m_fairnessWindow->summary()));
	m_fairnessWindow->setTerritoriesShown(true);
	const bool coloured = !fair.starts.empty()
		? std::any_of(m_territoryTint.begin(), m_territoryTint.end(), [](std::uint32_t c) { return (c >> 24) != 0; })
		: true;
	step(coloured, QStringLiteral("the territories colour the views"));
	m_fairnessWindow->close();
	step(m_territoryTint.empty(), QStringLiteral("closing the report takes the territories away"));
	return step.ok();
}

} // namespace MapEditorApp
