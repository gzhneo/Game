/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ToolsTests.cpp -- the self test of MainWindowTools.cpp (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/Widgets.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapImage.h"
#include "App/MapOverview.h"
#include "App/MiniMap.h"
#include "App/Overlay3D.h"
#include "App/Viewport3D.h"
#include "Core/CliffTexture.h"
#include "Model/MapDump.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <cmath>

namespace MapEditorApp
{

bool MainWindow::selfTestTools(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "tools");
	if (!m_state.map)
		return true;

	// Command palette: the menus and "Go to" for a named waypoint.
	const std::vector<PaletteEntry> entries = paletteEntries();
	const auto find = [&entries](const QString &part) {
		for (const PaletteEntry &e : entries)
			if (e.text.contains(part, Qt::CaseInsensitive))
				return &e;
		return static_cast<const PaletteEntry *>(nullptr);
	};
	const PaletteEntry *snap = find(T("Snap to grid"));
	const PaletteEntry *goTo = nullptr;
	int waypoint = -1;
	const auto &objects = *m_state.map->map().objects;
	for (std::size_t i = 0; i < objects.size() && !goTo; ++i)
	{
		const std::optional<std::string> name = objects[i].properties.getAscii("waypointName");
		if (name && !name->empty())
		{
			goTo = find(T("Go to waypoint %1").arg(QString::fromUtf8(name->c_str())));
			waypoint = static_cast<int>(i);
		}
	}
	if (goTo)
		goTo->run();
	step(snap && (!goTo || m_state.map->selection.objects == std::vector<std::int32_t>{waypoint}),
		QStringLiteral("command palette: %1 entries, menu commands and go to a waypoint").arg(entries.size()));
	m_state.map->selection.clear();
	selectionChanged();

	// A selected light shows its two radii.
	{
		MapModel::Map one;
		MapModel::MapObject light;
		light.properties.set("lightHeightAboveTerrain", MapModel::PropertyValue(10.0f));
		light.properties.set("lightInnerRadius", MapModel::PropertyValue(50.0f));
		light.properties.set("lightOuterRadius", MapModel::PropertyValue(120.0f));
		one.objects = std::vector<MapModel::MapObject>{light};
		const OverlayShapes shapes = buildOverlay(one, OverlayOptions{}, {SelectionMark{0, 8.0f}});
		step(shapes.shapes.size() == 4, QStringLiteral("a selected light shows ring, facing and its two radii (%1 shapes)").arg(shapes.shapes.size()));
	}

	// Cliffs: align all, one undo step, undo gives the map back.
	if (m_state.map->map().blendTiles && m_state.map->map().heightMap)
	{
		MapModel::BlendTiles &bt = *m_state.map->map().blendTiles;
		const MapModel::BlendTiles before = bt;
		const int regions = MapEditorCore::textureAllCliffs(bt, *m_state.map->map().heightMap);
		if (regions > 0 && bt != before)
		{
			const std::size_t mapped = bt.cliffInfos.size() - before.cliffInfos.size();
			recordCliffs(before, "Texture cliffs");
			const bool recorded = m_state.map->history.undoName() == "Texture cliffs";
			undo();
			step(recorded && *m_state.map->map().blendTiles == before,
				QStringLiteral("align all cliffs: %1 regions, %2 cell mappings, undo restores the map").arg(regions).arg(mapped));
		}
		else
			report << QStringLiteral("info tools: the map has no cliff to align");
		QApplication::processEvents();
	}

	// The minimap beside the 3D view: a position on it is the same place in the world.
	if (auto *mini = child<MiniMap>(this, "MiniMap"))
	{
		const float tx = 100.0f, ty = 200.0f;
		mini->setCamera(tx, ty, 0.0f);
		float x = 0.0f, y = 0.0f;
		const QRect area = mini->rect();
		const bool inside = mini->widgetToWorld(QPointF(area.center()), x, y);
		float x2 = 0.0f, y2 = 0.0f;
		mini->widgetToWorld(QPointF(area.center()) + QPointF(10.0, 0.0), x2, y2);
		step(inside && x2 > x && std::abs(y2 - y) < 0.01f, QStringLiteral("minimap beside the 3D view: its centre is %1, %2").arg(static_cast<double>(x), 0, 'f', 0).arg(static_cast<double>(y), 0, 'f', 0));
	}
	else if (m_show3D)
		step(false, QStringLiteral("minimap beside the 3D view"));

	// Text and picture of the map.
	QString error;
	const QString text = QDir(shotDir).filePath(QStringLiteral("map_dump.txt"));
	const QString picture = QDir(shotDir).filePath(QStringLiteral("map_export.png"));
	const bool textOk = exportMapText(text, &error) && QFileInfo(text).size() > 100;
	const bool pictureOk = exportMapPicture(picture, 4, &error) && QImage(picture).width() == m_state.map->map().heightMap->width * 4;
	step(textOk && pictureOk, QStringLiteral("export as text (%1 bytes) and as a picture, 4 pixels per cell").arg(QFileInfo(text).size()));

	// Bookmarks: saved with the camera, and going back to one restores it.
	if (m_viewport && m_viewport->hasDevice())
	{
		EngineClient &engine = EngineClient::instance();
		const std::vector<CameraBookmark> saved = m_state.bookmarks(m_state.map->path);
		const MeCamera start = engine.camera();
		m_state.setBookmarks(m_state.map->path, {CameraBookmark{QStringLiteral("SelfTest"), start.targetX, start.targetY, start.targetZ,
			start.yaw, start.pitch, start.distance}});
		MeCamera moved = start;
		moved.targetX += 200.0f;
		engine.setCamera(moved);
		goToBookmark(0);
		const MeCamera back = engine.camera();
		step(std::abs(back.targetX - start.targetX) < 1.0f && m_state.bookmarks(m_state.map->path).size() == 1,
			QStringLiteral("camera bookmark saved and restored"));
		m_state.setBookmarks(m_state.map->path, saved);
	}
	return step.ok();
}

} // namespace MapEditorApp
