/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapSizeTests.cpp -- the self test of MainWindowMapSize.cpp (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/MapSize.h"
#include "Model/MapIO.h"
#include <QApplication>
#include <algorithm>
#include <cmath>

namespace MapEditorApp
{

bool MainWindow::selfTestMapSize(QStringList &report)
{
	TestSteps step(report, "size");
	if (!m_state.map || !m_state.map->map().heightMap)
		return true;
	MapModel::Map &map = m_state.map->map();
	const MapEditorCore::History &history = m_state.map->history;
	const MapModel::HeightMap before = *map.heightMap;
	const std::size_t objects = map.objects ? map.objects->size() : 0;
	const float firstX = objects > 0 ? (*map.objects)[0].x : 0.0f;

	// Wider by 20 cells, kept at the right: everything moves 200 units east.
	MapEditorCore::ResizeOptions options;
	options.width = before.width - 2 * before.borderSize + 20;
	options.height = before.height - 2 * before.borderSize;
	options.border = before.borderSize;
	options.x = MapEditorCore::Anchor::End;
	const bool resized = resizeMapTo(options);
	step(resized && map.heightMap->width == before.width + 20 && history.undoName() == "Resize map",
		QStringLiteral("the map is 20 cells wider (%1 x %2)").arg(map.heightMap->width).arg(map.heightMap->height));
	step(objects == 0 || (map.objects->size() == objects && std::abs((*map.objects)[0].x - firstX - 200.0f) < 0.01f),
		QStringLiteral("objects move along with the terrain"));
	QApplication::processEvents();
	if (m_viewport && m_viewport->hasDevice())
		step(m_viewMapSerial == m_mapSerial, QStringLiteral("the view loads the resized map"));
	undo();
	QApplication::processEvents();
	step(*map.heightMap == before && !history.isModified() && (objects == 0 || (*map.objects)[0].x == firstX),
		QStringLiteral("undo gives the old size back"));

	// A new map as the dialog makes it; the engine must read it. The open map stays.
	QString error;
	std::optional<MapEditorCore::NewMapOptions> newOptions = newMapOptions(QString(), &error);
	if (!newOptions)
	{
		step(false, QStringLiteral("no values for a new map: %1").arg(error));
		return step.ok();
	}
	newOptions->width = 64;
	newOptions->height = 48;
	newOptions->border = 10;
	newOptions->players = 4;
	newOptions->layout = MapEditorCore::StartLayout::Circle;
	const std::optional<MapModel::Map> created = MapEditorCore::newMap(*newOptions);
	step(created && created->heightMap->width == 84 && created->lighting.has_value(),
		QStringLiteral("a new map of 64 x 48 cells with %1 and the lighting of the game data").arg(QString::fromStdString(newOptions->texture)));
	step(created && created->objects && created->objects->size() == 4
			&& created->objects->back().properties.getAscii("waypointName") == std::string("Player_4_Start"),
		QStringLiteral("the new map has four start waypoints in a circle"));
	if (created && m_viewport && m_viewport->hasDevice())
	{
		const MapModel::WriteResult bytes = MapModel::writeMap(*created);
		MeMapInfo info{};
		const bool loaded = bytes.ok() && EngineClient::instance().loadMap(bytes.bytes, &info, &error);
		step(loaded && info.cellsX == 84 && info.cellsY == 68, QStringLiteral("the engine reads the new map %1").arg(error));
		reloadEngineMap();
		step(m_viewMapSerial == m_mapSerial, QStringLiteral("the open map is shown again"));
	}
	return step.ok();
}

} // namespace MapEditorApp
