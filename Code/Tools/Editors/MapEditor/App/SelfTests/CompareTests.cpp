/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// CompareTests.cpp -- the self test of MainWindowCompare.cpp (MapEditor --selftest).

#include "App/Common.h"
#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "App/Language.h"
#include "App/Widgets.h"
#include "Core/HeightImage.h"
#include "Core/MapCompare.h"
#include "Model/MapFile.h"
#include <QDir>
#include <QImage>
#include <filesystem>

namespace MapEditorApp
{

bool MainWindow::selfTestCompare(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "compare");
	if (!m_state.map || !m_state.map->map().heightMap || !m_state.map->map().objects || m_state.map->map().objects->empty())
		return true;
	MapEditorCore::History &history = m_state.map->history;

	// Height map out and in again: the same picture changes nothing, an inverted one everything.
	const QString picture = QDir(shotDir).filePath(QStringLiteral("heights.png"));
	QString error;
	const bool exported = exportHeightmap(picture, &error);
	const bool same = exported && !importHeightmap(picture, 0, 255, &error);
	QImage inverted(picture);
	inverted.invertPixels();
	const QString invertedPath = QDir(shotDir).filePath(QStringLiteral("heights_inverted.png"));
	inverted.save(invertedPath);
	const std::vector<std::uint8_t> before = m_state.map->map().heightMap->heights;
	const bool imported = importHeightmap(invertedPath, 0, 255, &error);
	const std::vector<std::uint8_t> &after = m_state.map->map().heightMap->heights;
	const bool flipped = imported && after.size() == before.size() && !after.empty() && after[0] == 255 - before[0];
	if (imported)
		undo();
	step(exported && same && flipped && m_state.map->map().heightMap->heights == before,
		QStringLiteral("height map as a picture: the same picture changes nothing, the inverted one inverts the heights, undo"));

	// Compare: the map saved as it is, then one object moved.
	const QString copy = QDir(shotDir).filePath(QStringLiteral("compare.map"));
	std::string saveError;
	if (!MapModel::saveMap(toFsPath(copy), m_state.map->map(), saveError))
	{
		step(false, QStringLiteral("compare: cannot write %1").arg(copy));
		return step.ok();
	}
	const bool unchanged = compareWithFile(copy, &error).empty();
	m_state.map->selection.set(0);
	selectionChanged();
	dragSelection(40.0f, 0.0f);
	endDragSelection(false);
	const std::vector<MapEditorCore::MapDifference> differences = compareWithFile(copy, &error);
	const bool moved = differences.size() == 1 && differences[0].kind == MapEditorCore::MapDifference::Kind::ObjectMoved && differences[0].index == 0;
	if (history.undoName() == "Move object" || history.undoName() == "Move objects")
		undo();
	m_state.map->selection.clear();
	selectionChanged();
	step(unchanged && moved, QStringLiteral("compare with a saved copy: none, then one moved object (%1)")
		.arg(differences.empty() ? QString() : differenceText(differences[0])));
	return step.ok();
}

} // namespace MapEditorApp
