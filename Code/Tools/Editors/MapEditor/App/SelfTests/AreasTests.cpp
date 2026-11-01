/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// AreasTests.cpp -- the self test of MainWindowAreas.cpp (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/ToolEvent.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/AreaCommands.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <cmath>
#include <string>

namespace MapEditorApp
{

bool MainWindow::selfTestAreas(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "areas");
	MapEditorCore::History &history = m_state.map->history;
	const MapModel::Map &map = m_state.map->map();
	const std::size_t areasBefore = map.polygons ? map.polygons->size() : 0;
	const auto chooseTool = [this](const char *tool) {
		if (!m_catalogTree)
			return false;
		for (QTreeWidgetItemIterator it(m_catalogTree); *it; ++it)
		{
			if ((*it)->data(0, Qt::UserRole).toString() == QLatin1String(tool))
			{
				m_catalogTree->setCurrentItem(*it);
				return m_placeTemplate == QLatin1String(tool);
			}
		}
		return false;
	};

	m_workMode = WorkMode::Objects;
	m_show3D = false;
	rebuild();
	QApplication::processEvents();
	selectNone();
	step(chooseTool(AREA_TRIGGER_TOOL), QStringLiteral("the catalog has the area tool"));
	MapOverview *view = m_overview;
	if (!view || !areaTool())
		return false;
	view->fit();
	const auto send = [view](QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods = Qt::NoModifier) {
		QMouseEvent event(type, pos, view->mapToGlobal(pos), button, buttons, mods);
		QApplication::sendEvent(view, &event);
	};
	const auto click = [&send](QPointF pos, Qt::KeyboardModifiers mods = Qt::NoModifier) {
		send(QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton, mods);
		send(QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton, mods);
	};
	const auto drag = [&send](QPointF from, QPointF to) {
		send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
		send(QEvent::MouseMove, (from + to) * 0.5, Qt::NoButton, Qt::LeftButton);
		send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
		send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
	};

	// A square of 60 pixels near the middle; Ctrl+click starts it even inside an existing area.
	const QPointF c(view->width() * 0.5, view->height() * 0.5);
	const QPointF a = c + QPointF(-30, -30), b = c + QPointF(30, -30), d = c + QPointF(30, 30), e = c + QPointF(-30, 30);
	click(a, Qt::ControlModifier);
	click(b);
	click(d);
	click(e);
	step(m_areaDraft && m_areaDraft->points.size() == 4 && (map.polygons ? map.polygons->size() : 0) == areasBefore,
		QStringLiteral("four clicks draw an area that is not in the map yet"));
	QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
	QApplication::sendEvent(view, &enter);
	const int added = static_cast<int>(areasBefore);
	const bool closed = !m_areaDraft && map.polygons && map.polygons->size() == areasBefore + 1;
	step(closed && m_area == added && history.undoName() == "Add area"
			&& (*map.polygons)[static_cast<std::size_t>(added)].name.rfind("Area ", 0) == 0,
		QStringLiteral("Enter closes it (%1)").arg(closed ? utf8((*map.polygons)[static_cast<std::size_t>(added)].name) : QString()));
	if (!closed)
		return false;
	const auto area = [&map, added]() -> const MapModel::PolygonArea & { return (*map.polygons)[static_cast<std::size_t>(added)]; };
	const std::int32_t cornerX = area().points[2][0];
	drag(d, d + QPointF(20, 0));
	step(area().points[2][0] > cornerX + 10 && history.undoName() == "Move point", QStringLiteral("dragging a point moves it"));
	click((a + b) * 0.5);
	step(area().points.size() == 5 && m_areaPoint == 1 && history.undoName() == "Insert point", QStringLiteral("a click on an edge inserts a point"));
	deleteSelection();
	step(area().points.size() == 4 && history.undoName() == "Remove point", QStringLiteral("Del removes the selected point"));
	const std::int32_t leftX = area().points[0][0];
	drag(c, c + QPointF(-25, 0));
	step(area().points[0][0] < leftX - 10 && history.undoName() == "Move area", QStringLiteral("dragging inside moves the area"));

	// Side panel: name and water.
	QLineEdit *name = m_selectionBox ? m_selectionBox->findChild<QLineEdit *>(QStringLiteral("AreaName")) : nullptr;
	if (name)
	{
		name->setText(QStringLiteral("SelfTestArea"));
		emit name->editingFinished();
	}
	step(area().name == "SelfTestArea" && history.undoName() == "Rename area", QStringLiteral("the panel renames it"));
	QApplication::processEvents();
	QCheckBox *water = m_selectionBox ? m_selectionBox->findChild<QCheckBox *>(QStringLiteral("AreaWater")) : nullptr;
	if (water)
		water->click();
	step(area().isWater && area().points[0][2] == area().points[3][2], QStringLiteral("the panel makes it water (level %1)").arg(area().points[0][2]));
	const QString shot = QDir(shotDir).filePath(QStringLiteral("areas_2d.png"));
	step(view->grab().save(shot), QStringLiteral("picture %1").arg(shot));
	m_areaPoint = -1;
	deleteSelection();
	step(map.polygons->size() == areasBefore && m_area < 0 && history.undoName() == "Delete area", QStringLiteral("Del deletes the area"));

	// Water tool: a triangle closed by a click on its first point.
	step(chooseTool(AREA_WATER_TOOL), QStringLiteral("the catalog has the water tool"));
	click(a, Qt::ControlModifier);
	click(b);
	click(d);
	click(a);
	const bool lake = map.polygons && map.polygons->size() == areasBefore + 1 && (*map.polygons).back().isWater
		&& (*map.polygons).back().points.size() == 3 && (*map.polygons).back().name.rfind("Water Area ", 0) == 0;
	step(lake, QStringLiteral("a click on the first point closes a water area"));

	// 3D: a click starts an area there; Esc drops it.
	m_show3D = true;
	rebuild();
	QApplication::processEvents();
	if (m_viewport->hasDevice())
	{
		const QPointF at(m_viewport->width() * 0.5, m_viewport->height() * 0.5);
		QMouseEvent press(QEvent::MouseButtonPress, at, m_viewport->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
		QApplication::sendEvent(m_viewport, &press);
		QMouseEvent release(QEvent::MouseButtonRelease, at, m_viewport->mapToGlobal(at), Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
		QApplication::sendEvent(m_viewport, &release);
		step(m_areaDraft && m_areaDraft->points.size() == 1, QStringLiteral("3D: a click starts an area"));
		QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
		QApplication::sendEvent(m_viewport, &esc);
		step(!m_areaDraft, QStringLiteral("3D: Esc drops it"));
	}
	else
		step(false, QStringLiteral("3D: no view"));

	escape();
	escape();
	while (history.isModified() && history.canUndo())
		undo();
	step((map.polygons ? map.polygons->size() : 0) == areasBefore && !history.isModified(), QStringLiteral("undo all, map unmodified"));
	m_workMode = WorkMode::View;
	rebuild();
	QApplication::processEvents();
	return step.ok();
}

} // namespace MapEditorApp
