/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// LayersTests.cpp -- the self test of MainWindowLayers.cpp (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/Widgets.h"
#include "Core/LayerCommands.h"
#include <QApplication>
#include <QTreeWidget>
#include <algorithm>
#include <functional>

namespace MapEditorApp
{

bool MainWindow::selfTestLayers(QStringList &report)
{
	TestSteps step(report, "layers");
	if (!m_state.map || !m_state.map->map().objects)
		return true;
	MapModel::Map &map = m_state.map->map();
	MapEditorCore::Selection &sel = m_state.map->selection;
	MapEditorCore::History &history = m_state.map->history;
	const std::vector<MapModel::MapObject> objectsBefore = *map.objects;
	// Two ordinary objects of the map.
	std::vector<std::int32_t> two;
	for (std::size_t i = 0; i < map.objects->size() && two.size() < 2; ++i)
	{
		const MapModel::MapObject &o = (*map.objects)[i];
		if ((o.flags & 0x36) == 0 && !o.properties.getInt("waypointID"))
			two.push_back(static_cast<std::int32_t>(i));
	}
	step(two.size() == 2, QStringLiteral("the map has two objects to work with"));
	if (two.size() < 2)
		return step.ok();

	showLayers();
	QApplication::processEvents();
	auto *tree = m_layersWindow ? m_layersWindow->findChild<QTreeWidget *>(QStringLiteral("LayersTree")) : nullptr;
	const int layersBefore = tree ? tree->topLevelItemCount() : 0;
	step(tree && layersBefore >= 1, QStringLiteral("the layers window lists %1 layers").arg(layersBefore));

	// A new layer becomes active; the selection moves there.
	const std::string layer = newLayer();
	sel.objects = {two[0]};
	moveSelectionToLayer(layer);
	step(m_activeLayer == layer && MapEditorCore::objectLayer((*map.objects)[static_cast<std::size_t>(two[0])]) == layer
			&& history.undoName() == "Move to layer" && tree && tree->topLevelItemCount() == layersBefore + 1,
		QStringLiteral("a new layer is active and takes the selection"));

	// Hidden: the object can no longer be picked, and the engine leaves it out.
	setActiveLayer(std::string());
	setLayerHidden(layer, true);
	sel.clear();
	clickObject(two[0], false, false);
	step(sel.empty() && !pickable(two[0]), QStringLiteral("a hidden layer's object cannot be picked"));
	setLayerHidden(layer, false);
	clickObject(two[0], false, false);
	step(sel.objects == std::vector<std::int32_t>{two[0]}, QStringLiteral("shown again, it can"));

	// Rename, select, delete (back to the default layer).
	renameLayer(layer, "Walls");
	selectLayer("Walls");
	step(MapEditorCore::objectLayer((*map.objects)[static_cast<std::size_t>(two[0])]) == "Walls" && sel.objects == std::vector<std::int32_t>{two[0]},
		QStringLiteral("rename and select a layer"));
	deleteLayer("Walls");
	step(MapEditorCore::objectLayer((*map.objects)[static_cast<std::size_t>(two[0])]).empty() && history.undoName() == "Delete layer",
		QStringLiteral("deleting a layer moves its objects to the default one"));

	// Groups: Ctrl+G joins two objects, a click on one picks both, ungroup parts them.
	sel.objects = two;
	groupSelection();
	sel.clear();
	clickObject(two[1], false, false);
	step(history.undoName() == "Group objects" && sel.objects == two, QStringLiteral("a click on a group member picks the whole group"));
	selectRect({two[0]}, false);
	step(sel.objects == two, QStringLiteral("so does a rectangle"));
	ungroupSelection();
	sel.clear();
	clickObject(two[1], false, false);
	step(sel.objects == std::vector<std::int32_t>{two[1]}, QStringLiteral("after ungrouping only the object"));

	while (history.isModified() && history.canUndo())
		undo();
	step(*map.objects == objectsBefore && !history.isModified(), QStringLiteral("undo all, objects as before"));
	m_newLayers.clear();
	m_activeLayer.clear();
	if (m_layersWindow)
		m_layersWindow->close();
	selectNone();
	return step.ok();
}

} // namespace MapEditorApp
