/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// RoadsTests.cpp -- the self test of MainWindowRoads.cpp (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/ToolEvent.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/RoadCommands.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace MapEditorApp
{

bool MainWindow::selfTestRoads(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "roads");
	MapEditorCore::History &history = m_state.map->history;
	const MapModel::Map &map = m_state.map->map();
	const std::size_t segmentsBefore = MapEditorCore::roadSegments(map).size();

	m_workMode = WorkMode::Objects;
	m_show3D = false;
	rebuild();
	QApplication::processEvents();
	selectNone();
	// The first road type of the catalog.
	QString tool;
	for (QTreeWidgetItemIterator it(m_catalogTree); m_catalogTree && *it; ++it)
	{
		if ((*it)->data(0, Qt::UserRole).toString().startsWith(QLatin1String(ROAD_TOOL_PREFIX)))
		{
			m_catalogTree->setCurrentItem(*it);
			tool = (*it)->data(0, Qt::UserRole).toString();
			break;
		}
	}
	step(!tool.isEmpty() && roadTool(), QStringLiteral("the catalog has road types (%1)").arg(tool));
	MapOverview *view = m_overview;
	if (!view || !roadTool())
		return false;
	view->fit();
	const auto send = [view](QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods = Qt::NoModifier) {
		QMouseEvent event(type, pos, view->mapToGlobal(pos), button, buttons, mods);
		QApplication::sendEvent(view, &event);
	};
	const auto drag = [&send](QPointF from, QPointF to, Qt::KeyboardModifiers mods = Qt::NoModifier) {
		send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, mods);
		send(QEvent::MouseMove, (from + to) * 0.5, Qt::NoButton, Qt::LeftButton, mods);
		send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, mods);
		send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, mods);
	};
	// A place without roads nearby for every point the test clicks (an L of two segments, a branch,
	// a junction, a bridge), about 25 pixels clear of any road; the map may have a road network.
	const float clearance = static_cast<float>(std::abs(view->widgetToWorld(QPointF(25, 0)).x() - view->widgetToWorld(QPointF(0, 0)).x()));
	const QPointF offsets[] = {QPointF(0, 0), QPointF(80, 0), QPointF(80, 60), QPointF(40, 0), QPointF(40, -60), QPointF(60, 0),
		QPointF(60, 70), QPointF(0, 130), QPointF(90, 130), QPointF(45, 130)};
	QPointF a;
	bool found = false;
	for (int n = 0; n < 300 && !found; ++n)
	{
		a = QPointF(view->width() * (0.08 + 0.04 * (n % 20)), view->height() * (0.12 + 0.05 * (n / 20)));
		found = true;
		for (const QPointF &offset : offsets)
		{
			const QPointF p = a + offset;
			const QPointF w = view->widgetToWorld(p);
			float ox = 0.0f, oy = 0.0f;
			found = found && p.x() > 0 && p.y() > 0 && p.x() < view->width() && p.y() < view->height()
				&& MapEditorCore::roadSegmentAt(map, static_cast<float>(w.x()), static_cast<float>(w.y()), clearance, ox, oy,
					MapEditorCore::SegmentKind::Both) < 0;
		}
	}
	if (!found)
	{
		step(false, QStringLiteral("no free place for roads"));
		return false;
	}
	const QPointF b = a + QPointF(80, 0), c = a + QPointF(80, 60);
	drag(a, b);
	const std::vector<std::int32_t> one = MapEditorCore::roadSegments(map);
	step(one.size() == segmentsBefore + 1 && history.undoName() == "Add road" && m_road == one.back(), QStringLiteral("dragging draws a segment and selects it"));
	drag(b + QPointF(3, 2), c);
	const std::vector<std::int32_t> two = MapEditorCore::roadSegments(map);
	const MapModel::MapObject *firstEnd = two.size() >= 2 ? &(*map.objects)[static_cast<std::size_t>(one.back()) + 1] : nullptr;
	const MapModel::MapObject *secondStart = two.size() >= 2 ? &(*map.objects)[static_cast<std::size_t>(two.back())] : nullptr;
	step(two.size() == segmentsBefore + 2 && firstEnd && secondStart && firstEnd->x == secondStart->x && firstEnd->y == secondStart->y,
		QStringLiteral("the next segment snaps to the end of the first"));

	// Click selects, the panel changes the corners.
	send(QEvent::MouseButtonPress, (a + b) * 0.5, Qt::LeftButton, Qt::LeftButton);
	send(QEvent::MouseButtonRelease, (a + b) * 0.5, Qt::LeftButton, Qt::NoButton);
	step(m_road == one.back(), QStringLiteral("a click selects a segment"));
	QApplication::processEvents();
	QComboBox *corner = m_selectionBox ? m_selectionBox->findChild<QComboBox *>(QStringLiteral("RoadCorner")) : nullptr;
	if (corner)
	{
		corner->setCurrentIndex(1);
		emit corner->activated(1);
	}
	step(m_road >= 0 && ((*map.objects)[static_cast<std::size_t>(m_road)].flags & MapEditorCore::ROAD_CORNER_ANGLED) && history.undoName() == "Change road",
		QStringLiteral("the panel makes its corners angled"));

	// Shift+drag moves the joint of both segments.
	const MapModel::MapObject joint = *firstEnd;
	drag(b, b + QPointF(0, -20), Qt::ShiftModifier);
	const std::vector<std::int32_t> atJoint = MapEditorCore::roadPointsAt(map, joint.x, joint.y);
	step(atJoint.empty() && history.undoName() == "Move road point", QStringLiteral("Shift+drag moves the joint of both segments"));
	undo();

	// A branch from the middle of the first segment cuts it.
	drag((a + b) * 0.5, (a + b) * 0.5 + QPointF(0, -60));
	const std::size_t afterBranch = MapEditorCore::roadSegments(map).size();
	step(afterBranch == segmentsBefore + 4 && history.undoName() == "Add road", QStringLiteral("a branch cuts the segment it starts on (%1 segments)").arg(afterBranch - segmentsBefore));
	const QString shot = QDir(shotDir).filePath(QStringLiteral("roads_2d.png"));
	step(view->grab().save(shot), QStringLiteral("picture %1").arg(shot));
	deleteSelection();
	step(MapEditorCore::roadSegments(map).size() == afterBranch - 1 && history.undoName() == "Delete road" && m_state.map->selection.empty(),
		QStringLiteral("Del deletes the selected segment"));

	// A road ending on the middle of another makes a T junction; the new one joins it.
	const QPointF onRoad = a + QPointF(60, 0);
	const std::size_t beforeJunction = MapEditorCore::roadSegments(map).size();
	drag(a + QPointF(60, 70), onRoad);
	const QPointF junction = view->widgetToWorld(onRoad);
	const MapModel::MapObject &junctionEnd = (*map.objects).back();
	step(MapEditorCore::roadSegments(map).size() == beforeJunction + 2 && MapEditorCore::roadPointsAt(map, junctionEnd.x, junctionEnd.y).size() == 3
			&& std::abs(junctionEnd.y - static_cast<float>(junction.y())) < 15.0f,
		QStringLiteral("a road ending on another makes a T junction"));
	QApplication::processEvents();
	QCheckBox *join = m_selectionBox ? m_selectionBox->findChild<QCheckBox *>(QStringLiteral("RoadJoin")) : nullptr;
	if (join)
		join->click();
	step(m_road >= 0 && ((*map.objects)[static_cast<std::size_t>(m_road)].flags & MapEditorCore::ROAD_JOIN)
			&& ((*map.objects)[static_cast<std::size_t>(m_road) + 1].flags & MapEditorCore::ROAD_JOIN) && history.undoName() == "Change road",
		QStringLiteral("the panel sets the join to another road type"));

	// Bridges: their own catalog entries, no snapping; a click selects, Del deletes.
	QString bridgeTool;
	for (QTreeWidgetItemIterator it(m_catalogTree); m_catalogTree && *it; ++it)
	{
		if ((*it)->data(0, Qt::UserRole).toString().startsWith(QLatin1String(BRIDGE_TOOL_PREFIX)))
		{
			m_catalogTree->setCurrentItem(*it);
			bridgeTool = (*it)->data(0, Qt::UserRole).toString();
			break;
		}
	}
	step(!bridgeTool.isEmpty() && this->bridgeTool(), QStringLiteral("the catalog has bridge types (%1)").arg(bridgeTool));
	if (this->bridgeTool())
	{
		const QPointF from = a + QPointF(0, 130), to = a + QPointF(90, 130);
		drag(from, to);
		const std::vector<std::int32_t> bridges = MapEditorCore::roadSegments(map, MapEditorCore::SegmentKind::Bridges);
		step(!bridges.empty() && history.undoName() == "Add bridge" && m_road == bridges.back()
				&& ((*map.objects)[static_cast<std::size_t>(bridges.back())].flags & MapEditorCore::BRIDGE_POINT1),
			QStringLiteral("dragging draws a bridge"));
		const QString bridgeShot = QDir(shotDir).filePath(QStringLiteral("roads_bridge_2d.png"));
		step(view->grab().save(bridgeShot), QStringLiteral("picture %1").arg(bridgeShot));
		escape();
		send(QEvent::MouseButtonPress, (from + to) * 0.5, Qt::LeftButton, Qt::LeftButton);
		send(QEvent::MouseButtonRelease, (from + to) * 0.5, Qt::LeftButton, Qt::NoButton);
		step(!bridges.empty() && m_road == bridges.back(), QStringLiteral("a click selects the bridge"));
		deleteSelection();
		step(MapEditorCore::roadSegments(map, MapEditorCore::SegmentKind::Bridges).size() == bridges.size() - 1, QStringLiteral("Del deletes the bridge"));
	}
	for (QTreeWidgetItemIterator it(m_catalogTree); m_catalogTree && *it; ++it)
	{
		if ((*it)->data(0, Qt::UserRole).toString() == tool)
			m_catalogTree->setCurrentItem(*it);
	}

	// 3D: a drag draws a segment there too.
	m_show3D = true;
	rebuild();
	QApplication::processEvents();
	if (m_viewport->hasDevice())
	{
		const std::size_t before3D = MapEditorCore::roadSegments(map).size();
		const qsizetype loadsBefore = EngineClient::instance().report().count(QStringLiteral("roads and bridges loaded"));
		const QPointF from(m_viewport->width() * 0.5 - 60, m_viewport->height() * 0.5 + 80);
		const QPointF to = from + QPointF(120, 0);
		const auto mouse = [this](QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
			QMouseEvent event(type, pos, m_viewport->mapToGlobal(pos), button, buttons, Qt::ControlModifier);
			QApplication::sendEvent(m_viewport, &event);
		};
		mouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
		mouse(QEvent::MouseMove, (from + to) * 0.5, Qt::NoButton, Qt::LeftButton);
		mouse(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
		mouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
		step(MapEditorCore::roadSegments(map).size() == before3D + 1, QStringLiteral("3D: a drag draws a segment"));
		EngineClient::instance().render();
		step(EngineClient::instance().report().count(QStringLiteral("roads and bridges loaded")) > loadsBefore, QStringLiteral("3D: the terrain rebuilt the roads"));
	}
	else
		step(false, QStringLiteral("3D: no view"));

	escape();
	escape();
	while (history.isModified() && history.canUndo())
		undo();
	step(MapEditorCore::roadSegments(map).size() == segmentsBefore && !history.isModified(), QStringLiteral("undo all, map unmodified"));
	m_workMode = WorkMode::View;
	rebuild();
	QApplication::processEvents();
	return step.ok();
}

} // namespace MapEditorApp
