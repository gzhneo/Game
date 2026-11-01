/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// PrefabsTests.cpp -- the self test of MainWindowPrefabs.cpp (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "App/Language.h"
#include "Core/ObjectCommands.h"
#include <QDir>
#include <span>

namespace MapEditorApp
{

bool MainWindow::selfTestPrefabs(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "prefabs");
	if (!m_state.map || !m_state.map->map().objects || m_state.map->map().objects->size() < 2)
		return true;
	m_prefabFolder = QDir(shotDir).filePath(QStringLiteral("prefabs"));
	QDir(m_prefabFolder).removeRecursively();
	auto &objects = *m_state.map->map().objects;
	const std::size_t n = objects.size();
	MapEditorCore::History &history = m_state.map->history;
	m_state.map->selection.set(0);
	m_state.map->selection.add(1);
	selectionChanged();
	QString error;
	const bool saved = savePrefab(QStringLiteral("Self test: camp"), &error);
	step(saved && prefabNames() == QStringList{QStringLiteral("Self test_ camp")}, QStringLiteral("save two objects as a prefab (%1)").arg(prefabNames().join(QStringLiteral(", "))));
	const bool placed = saved && placePrefab(prefabNames().value(0), 500.0f, 400.0f);
	float cx = 0.0f;
	float cy = 0.0f;
	if (placed && objects.size() == n + 2)
	{
		cx = (objects[n].x + objects[n + 1].x) * 0.5f;
		cy = (objects[n].y + objects[n + 1].y) * 0.5f;
	}
	step(placed && objects.size() == n + 2 && std::abs(cx - 500.0f) < 0.01f && std::abs(cy - 400.0f) < 0.01f
			&& objects[n].templateName == objects[0].templateName && history.undoName() == "Place prefab",
		QStringLiteral("place it: two copies around the click"));
	if (placed)
		undo();
	step(objects.size() == n, QStringLiteral("undo removes the placed prefab"));
	m_state.map->selection.clear();
	selectionChanged();
	QDir(m_prefabFolder).removeRecursively();
	m_prefabFolder.clear();
	return step.ok();
}

} // namespace MapEditorApp
