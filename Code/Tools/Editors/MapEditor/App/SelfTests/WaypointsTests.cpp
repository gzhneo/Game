/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WaypointsTests.cpp -- the self test of MainWindowWaypoints.cpp (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/MainWindowParts.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/Viewport3D.h"
#include "Core/WaypointCommands.h"
#include <QApplication>
#include <QDir>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <algorithm>
#include <string>

namespace MapEditorApp
{

namespace
{

std::size_t linkCount(const MapModel::Map &map)
{
	return map.waypointLinks ? map.waypointLinks->size() : 0;
}

} // namespace

bool MainWindow::selfTestWaypoints(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "waypoints");
	MapEditorCore::History &history = m_state.map->history;
	MapEditorCore::Selection &sel = m_state.map->selection;
	const MapModel::Map &map = m_state.map->map();
	const std::size_t objectsBefore = map.objects->size();
	const std::size_t linksBefore = linkCount(map);
	const QString waypoint = QString::fromLatin1(MapEditorCore::WAYPOINT_TEMPLATE);
	const auto chooseTool = [this, &waypoint] {
		if (!m_catalogTree)
			return false;
		for (QTreeWidgetItemIterator it(m_catalogTree); *it; ++it)
		{
			if ((*it)->data(0, Qt::UserRole).toString() == waypoint)
			{
				m_catalogTree->setCurrentItem(*it);
				return m_placeTemplate == waypoint;
			}
		}
		return false;
	};

	m_workMode = WorkMode::Objects;
	m_show3D = false;
	rebuild();
	QApplication::processEvents();
	selectNone();
	step(chooseTool(), QStringLiteral("the catalog has the waypoint tool"));
	MapOverview *view = m_overview;
	if (!view || m_placeTemplate != waypoint)
		return false;
	view->fit();
	const auto click = [view](QPointF pos, Qt::KeyboardModifiers mods) {
		QMouseEvent press(QEvent::MouseButtonPress, pos, view->mapToGlobal(pos), Qt::LeftButton, Qt::LeftButton, mods);
		QApplication::sendEvent(view, &press);
		QMouseEvent release(QEvent::MouseButtonRelease, pos, view->mapToGlobal(pos), Qt::LeftButton, Qt::NoButton, mods);
		QApplication::sendEvent(view, &release);
	};
	// Free places near the middle of the map: a click there must not hit an object.
	std::vector<QPointF> spots;
	const QPointF middle(view->width() * 0.5, view->height() * 0.5);
	for (int ring = 0; ring < 40 && spots.size() < 4; ++ring)
	{
		const QPointF p = middle + QPointF((ring % 8) * 23.0 - 80.0, (ring / 8) * 29.0 - 60.0);
		bool clear = view->objectAt(p) < 0;
		for (const QPointF &q : spots)
			clear = clear && std::abs(q.x() - p.x()) + std::abs(q.y() - p.y()) > 20.0;
		if (clear)
			spots.push_back(p);
	}
	if (spots.size() < 4)
	{
		step(false, QStringLiteral("no free places for waypoints"));
		return false;
	}

	click(spots[0], Qt::NoModifier);
	const int w1 = static_cast<int>(objectsBefore);
	step(map.objects->size() == objectsBefore + 1 && waypointAt(map, w1) && linkCount(map) == linksBefore
			&& sel.objects == std::vector<std::int32_t>{w1} && history.undoName() == "Add waypoint",
		QStringLiteral("a click places a waypoint and selects it"));
	click(spots[1], Qt::NoModifier);
	click(spots[2], Qt::NoModifier);
	const int w2 = w1 + 1;
	const int w3 = w1 + 2;
	step(map.objects->size() == objectsBefore + 3 && linkCount(map) == linksBefore + 2 && MapEditorCore::waypointPath(map, w1).size() == 3
			&& sel.objects == std::vector<std::int32_t>{w3},
		QStringLiteral("two more clicks continue the path (%1 links)").arg(linkCount(map) - linksBefore));

	// Path label from the panel: the whole path takes it.
	QLineEdit *labelField = m_selectionBox ? m_selectionBox->findChild<QLineEdit *>(QStringLiteral("PathLabel1")) : nullptr;
	if (labelField)
	{
		labelField->setText(QStringLiteral("Flank"));
		emit labelField->editingFinished();
	}
	bool labelled = true;
	for (int w : {w1, w2, w3})
		labelled = labelled && (*map.objects)[static_cast<std::size_t>(w)].properties.getAscii("waypointPathLabel1") == std::string("Flank");
	step(labelField && labelled && history.undoName() == "Change path", QStringLiteral("a path label in the panel names the whole path"));

	// A click on the first links the last to it; clicking the last again removes that link.
	click(view->worldToWidget((*map.objects)[static_cast<std::size_t>(w1)].x, (*map.objects)[static_cast<std::size_t>(w1)].y), Qt::NoModifier);
	step(linkCount(map) == linksBefore + 3 && sel.objects == std::vector<std::int32_t>{w1} && history.undoName() == "Link waypoints",
		QStringLiteral("a click on a waypoint links the selected one to it"));
	click(view->worldToWidget((*map.objects)[static_cast<std::size_t>(w3)].x, (*map.objects)[static_cast<std::size_t>(w3)].y), Qt::NoModifier);
	step(linkCount(map) == linksBefore + 2 && history.undoName() == "Unlink waypoints", QStringLiteral("a second click removes the link"));

	// Ctrl+click starts a new path; the menu command links the selection in its order.
	click(spots[3], Qt::ControlModifier);
	const int w4 = w1 + 3;
	step(map.objects->size() == objectsBefore + 4 && linkCount(map) == linksBefore + 2 && MapEditorCore::waypointPath(map, w4).size() == 1,
		QStringLiteral("Ctrl+click starts a new path"));
	sel.objects = {w4, w1};
	selectionChanged();
	linkSelectedWaypoints();
	step(linkCount(map) == linksBefore + 3 && MapEditorCore::waypointPath(map, w4).size() == 4
			&& (*map.objects)[static_cast<std::size_t>(w1)].properties.getAscii("waypointPathLabel1") == std::nullopt,
		QStringLiteral("Ctrl+L links the selection; the path takes the labels of the first"));
	unlinkSelectedWaypoints();
	step(linkCount(map) == linksBefore + 2 && history.undoName() == "Unlink waypoints", QStringLiteral("Ctrl+Shift+L unlinks them"));
	const QString shot = QDir(shotDir).filePath(QStringLiteral("waypoints_2d.png"));
	sel.objects = {w1, w2, w3};
	selectionChanged();
	step(view->grab().save(shot), QStringLiteral("picture %1").arg(shot));

	// 3D: a click places a waypoint the engine knows as a marker.
	m_show3D = true;
	rebuild();
	QApplication::processEvents();
	if (m_viewport->hasDevice())
	{
		EngineClient &engine = EngineClient::instance();
		MePickResult info{};
		engine.objectInfo(w1, &info);
		MeCamera c{};
		c.targetX = info.x;
		c.targetY = info.y;
		c.targetZ = info.z;
		c.pitch = 1.45f;
		c.distance = 400.0f;
		engine.setCamera(c);
		selectNone();
		const QPointF at(m_viewport->width() * 0.5 + 60.0, m_viewport->height() * 0.5 + 40.0);
		QMouseEvent press(QEvent::MouseButtonPress, at, m_viewport->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(m_viewport, &press);
		QMouseEvent release(QEvent::MouseButtonRelease, at, m_viewport->mapToGlobal(at), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(m_viewport, &release);
		const int w5 = w1 + 4;
		MePickResult marker{};
		step(map.objects->size() == objectsBefore + 5 && engine.objectInfo(w5, &marker) && marker.kind == ME_OBJECT_MARKER,
			QStringLiteral("3D: a click places a waypoint the view knows"));
	}
	else
		step(false, QStringLiteral("3D: no view"));

	QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
	QApplication::sendEvent(m_viewport, &esc);
	step(m_placeTemplate.isEmpty(), QStringLiteral("Esc ends the tool"));
	while (history.isModified() && history.canUndo())
		undo();
	step(map.objects->size() == objectsBefore && linkCount(map) == linksBefore && !history.isModified(), QStringLiteral("undo all, map unmodified"));

	m_workMode = WorkMode::View;
	rebuild();
	QApplication::processEvents();
	return step.ok();
}

} // namespace MapEditorApp
