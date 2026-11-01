/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SymmetryTests.cpp -- the self test of making the map symmetric (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "Core/Symmetry.h"

namespace MapEditorApp
{

bool MainWindow::selfTestSymmetry(QStringList &report)
{
	TestSteps step(report, "symmetry");
	if (!m_state.map || !m_state.map->map().heightMap || !m_state.map->map().objects)
		return true;
	const MapModel::Map before = m_state.map->map();
	const MapModel::HeightMap &hm = *m_state.map->map().heightMap;
	showSymmetryLines(MapEditorCore::symmetryLines(hm, MapEditorCore::SymmetryMode::FourQuadrants));
	step(toolLines().size() == 8, QStringLiteral("the views show the two mirror lines of the quarters"));
	showSymmetryLines({});
	const std::size_t undoBefore = m_state.map->history.undoCount();
	MapEditorCore::SymmetryOptions options;
	const bool applied = applySymmetry(options);
	bool mirrored = applied;
	for (int y = 0; y < hm.height && mirrored; ++y)
		for (int x = 0; x < hm.width && mirrored; ++x)
			mirrored = hm.heights[static_cast<std::size_t>(y * hm.width + x)] == hm.heights[static_cast<std::size_t>(y * hm.width + hm.width - 1 - x)];
	step(applied && mirrored && m_state.map->history.undoCount() == undoBefore + 1 && m_state.map->history.undoName() == "Symmetry",
		QStringLiteral("left onto right: the heights are mirrored, one undo step (%1 objects before, %2 after)")
			.arg(before.objects->size()).arg(m_state.map->map().objects->size()));
	undo();
	step(m_state.map->map().heightMap == before.heightMap && m_state.map->map().objects == before.objects && m_state.map->map().blendTiles == before.blendTiles,
		QStringLiteral("undo brings the map back"));
	return step.ok();
}

} // namespace MapEditorApp
