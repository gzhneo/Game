/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// PlacementTests.cpp -- the self test of MainWindowPlacement.cpp (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/MainWindowParts.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/ToolEvent.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/ObjectCommands.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <cmath>
#include <cstring>

namespace MapEditorApp
{

bool MainWindow::selfTestPlacement(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "placement");
	MapModel::Map &map = m_state.map->map();
	MapEditorCore::History &history = m_state.map->history;
	const std::size_t objectsBefore = map.objects ? map.objects->size() : 0;
	m_show3D = false;
	m_workMode = WorkMode::Objects;
	rebuild();
	QApplication::processEvents();
	MapOverview *view = m_overview;
	if (!view || !map.heightMap)
	{
		step(false, QStringLiteral("no overview"));
		return false;
	}
	view->fit();
	const QPointF mid = view->widgetToWorld(QPointF(view->width() * 0.5, view->height() * 0.5));
	const auto sendTool = [this](ToolEvent::Kind kind, QPointF w, bool shift = false) {
		ToolEvent e;
		e.kind = kind;
		e.x = static_cast<float>(w.x());
		e.y = static_cast<float>(w.y());
		e.tolerance = 8.0f;
		e.shift = shift;
		toolEvent(e);
	};
	const auto count = [&map] { return map.objects ? map.objects->size() : 0; };

	// Grove: a click grows one, a drag scatters trees in the rectangle.
	setPlacingTemplate(QString::fromLatin1(GROVE_TOOL), 0.0f);
	QApplication::processEvents();
	const QStringList trees = treeTemplates();
	step(!trees.isEmpty() && findChild<QComboBox *>(QStringLiteral("GroveType0")),
		QStringLiteral("the grove panel lists %1 trees").arg(trees.size()));
	m_grove.types.clear();
	for (int i = 0; i < std::min<int>(2, static_cast<int>(trees.size())); ++i)
		m_grove.types.push_back({trees[i].toStdString(), 50});
	m_placeRandom.seed(11);
	std::size_t before = count();
	sendTool(ToolEvent::Kind::Press, mid);
	sendTool(ToolEvent::Kind::Release, mid);
	bool allTrees = true;
	for (std::size_t i = before; i < count(); ++i)
		allTrees = allTrees && trees.contains(QString::fromStdString((*map.objects)[i].templateName));
	step(count() > before && allTrees && history.undoName() == "Plant grove",
		QStringLiteral("a click grows a grove of %1 trees").arg(count() - before));
	const QString shot = QDir(shotDir).filePath(QStringLiteral("grove_2d.png"));
	step(view->grab().save(shot), QStringLiteral("picture %1").arg(shot));
	undo();
	step(count() == before, QStringLiteral("undo takes the grove back"));
	m_grove.boxCount = 30;
	sendTool(ToolEvent::Kind::Press, mid - QPointF(60.0, 60.0));
	sendTool(ToolEvent::Kind::Move, mid);
	sendTool(ToolEvent::Kind::Release, mid + QPointF(60.0, 60.0));
	bool inside = true;
	for (std::size_t i = before; i < count(); ++i)
	{
		const MapModel::MapObject &o = (*map.objects)[i];
		inside = inside && std::abs(o.x - static_cast<float>(mid.x())) <= 60.5f && std::abs(o.y - static_cast<float>(mid.y())) <= 60.5f;
	}
	step(count() > before && count() <= before + 30 && inside && history.undoName() == "Plant trees",
		QStringLiteral("a drag scatters %1 trees in the rectangle").arg(count() - before));
	undo();

	// Fence along a line of 100 units.
	QString fence;
	float width = 0.0f;
	for (const MeTemplateInfo &info : EngineClient::instance().catalog())
	{
		if (info.fenceWidth > 0.0f && fence.isEmpty())
		{
			fence = QString::fromUtf8(info.name);
			width = info.fenceWidth;
		}
	}
	if (!fence.isEmpty())
	{
		setPlacingTemplate(QString::fromLatin1(FENCE_TOOL_PREFIX) + fence, 0.0f);
		QApplication::processEvents();
		before = count();
		sendTool(ToolEvent::Kind::Press, mid);
		sendTool(ToolEvent::Kind::Move, mid + QPointF(50.0, 0.0));
		step(placementLines().size() > 4, QStringLiteral("the fence shows its pieces while dragging"));
		sendTool(ToolEvent::Kind::Release, mid + QPointF(100.0, 0.0));
		const std::size_t expected = static_cast<std::size_t>(std::max(1.0f, std::floor(100.0f / width)));
		step(m_fenceSpacing == width && count() == before + expected && history.undoName() == "Place fence"
				&& (*map.objects)[before].templateName == fence.toStdString(),
			QStringLiteral("a drag sets %1 pieces of %2 (spacing %3)").arg(count() - before).arg(fence).arg(static_cast<double>(width)));
		undo();
		// Shift keeps the count reached and stretches the spacing over the longer line.
		before = count();
		sendTool(ToolEvent::Kind::Press, mid);
		sendTool(ToolEvent::Kind::Move, mid + QPointF(100.0, 0.0));
		sendTool(ToolEvent::Kind::Move, mid + QPointF(100.0, 0.0), true);
		sendTool(ToolEvent::Kind::Release, mid + QPointF(200.0, 0.0), true);
		const bool stretched = count() == before + expected && (expected < 2
			|| std::fabs((*map.objects)[before + 1].x - (*map.objects)[before].x - 200.0f / static_cast<float>(expected)) < 0.01f);
		step(stretched, QStringLiteral("with Shift the %1 pieces stretch over twice the length").arg(count() - before));
		undo();
	}
	else
		step(false, QStringLiteral("the game data has no fence template"));

	// Scorch marks: a click sets one, a click on it picks it.
	setPlacingTemplate(QString::fromLatin1(SCORCH_TOOL), 0.0f);
	QApplication::processEvents();
	m_scorchType = 2;
	m_scorchRadius = 30.0f;
	before = count();
	sendTool(ToolEvent::Kind::Press, mid);
	sendTool(ToolEvent::Kind::Release, mid);
	const bool placed = count() == before + 1 && MapEditorCore::isScorch(map.objects->back())
		&& map.objects->back().properties.getInt("scorchType") == 2 && map.objects->back().properties.getReal("objectRadius") == 30.0f;
	step(placed && history.undoName() == "Place scorch mark", QStringLiteral("a click sets a scorch mark"));
	m_state.map->selection.clear();
	selectionChanged();
	sendTool(ToolEvent::Kind::Press, mid + QPointF(3.0, 0.0));
	step(count() == before + 1 && m_state.map->selection.objects.size() == 1
			&& m_state.map->selection.objects[0] == static_cast<std::int32_t>(before),
		QStringLiteral("a click on it picks it"));
	undo();
	step(count() == objectsBefore, QStringLiteral("undo all, objects as before"));
	setPlacingTemplate(QString(), 0.0f);
	m_workMode = WorkMode::View;
	rebuild();
	QApplication::processEvents();
	return step.ok();
}

} // namespace MapEditorApp
