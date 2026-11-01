/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowSymmetry.cpp -- makes the map symmetric from one part (Core/Symmetry, Windows/SymmetryDialog):
// while the dialog is open, the 2D overview and the 3D view show the lines it mirrors at.

#include "App/MainWindow.h"

#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/Windows/SymmetryDialog.h"

namespace MapEditorApp
{

void MainWindow::showSymmetryLines(std::vector<float> lines)
{
	m_symmetryLines = std::move(lines);
	if (m_overview)
		m_overview->setToolLines(toolLines());
	applyOverlay();
}

bool MainWindow::applySymmetry(const MapEditorCore::SymmetryOptions &options)
{
	if (!m_state.map)
		return false;
	auto step = MapEditorCore::symmetryStep(m_state.map->map(), options);
	if (!step)
	{
		toast(T("The map is symmetric already."));
		return false;
	}
	return runCommand(std::move(step));
}

void MainWindow::showSymmetry()
{
	if (!m_state.map || !m_state.map->map().heightMap)
		return;
	const MapModel::HeightMap &hm = *m_state.map->map().heightMap;
	SymmetryDialog dialog(m_symmetryMode, this);
	connect(&dialog, &SymmetryDialog::modeChanged, this,
		[this, &hm](MapEditorCore::SymmetryMode mode) { showSymmetryLines(MapEditorCore::symmetryLines(hm, mode)); });
	showSymmetryLines(MapEditorCore::symmetryLines(hm, m_symmetryMode));
	const bool accepted = dialog.exec() == QDialog::Accepted;
	showSymmetryLines({});
	if (!accepted)
		return;
	m_symmetryMode = dialog.options().mode;
	applySymmetry(dialog.options());
}

} // namespace MapEditorApp
