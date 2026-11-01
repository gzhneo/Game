/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowLayers.cpp -- layers and groups of map objects. The layers window lists the layers of the
// map (after the old WorldBuilder's LayersList): shown or hidden, the active one new objects go into,
// rename, delete (its objects go to the active or the default layer), move the selection there,
// select its objects. Groups: Ctrl+G joins the selection, a click on a member picks the whole group.

#include "App/MainWindow.h"

#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/Widgets.h"
#include "Core/LayerCommands.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace MapEditorApp
{

namespace
{

constexpr int ROLE_LAYER = Qt::UserRole + 1;

} // namespace

void MainWindow::showLayers()
{
	if (!m_layersWindow)
	{
		auto *window = new QDialog(this);
		window->setObjectName(QStringLiteral("LayersWindow"));
		window->setWindowTitle(T("Layers"));
		window->resize(420, 380);
		auto *col = new QVBoxLayout(window);
		auto *tree = new QTreeWidget(window);
		tree->setObjectName(QStringLiteral("LayersTree"));
		tree->setColumnCount(3);
		tree->setHeaderLabels({T("Layer"), T("Objects"), T("Areas")});
		tree->setRootIsDecorated(false);
		tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
		col->addWidget(tree, 1);
		// A checkbox shows or hides a layer, editing the name renames it.
		connect(tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
			if (column != 0)
				return;
			const std::string layer = item->data(0, ROLE_LAYER).toString().toStdString();
			const bool hidden = item->checkState(0) == Qt::Unchecked;
			if (hidden != (m_hiddenLayers.count(layer) != 0))
			{
				QTimer::singleShot(0, this, [this, layer, hidden] { setLayerHidden(layer, hidden); });
				return;
			}
			const std::string name = item->text(0).trimmed().toStdString();
			if (!layer.empty() && !name.empty() && name != layer && item->text(0) != T("%1 (active)").arg(QString::fromStdString(layer)))
				QTimer::singleShot(0, this, [this, layer, name] { renameLayer(layer, name); });
		});
		const auto current = [tree]() -> std::string {
			QTreeWidgetItem *item = tree->currentItem();
			return item ? item->data(0, ROLE_LAYER).toString().toStdString() : std::string();
		};
		auto *row = new QHBoxLayout;
		const auto button = [&](const QString &text, const char *name, const std::function<void()> &action) {
			auto *b = new QPushButton(text, window);
			b->setObjectName(QString::fromLatin1(name));
			connect(b, &QPushButton::clicked, this, action);
			row->addWidget(b);
		};
		button(T("New"), "LayerNew", [this] { newLayer(); });
		button(T("Active"), "LayerActive", [this, current] { setActiveLayer(current()); });
		button(T("Move selection here"), "LayerMove", [this, current] { moveSelectionToLayer(current()); });
		button(T("Select"), "LayerSelect", [this, current] { selectLayer(current()); });
		button(T("Merge into active"), "LayerMerge", [this, current] {
			const std::string layer = current();
			if (layer.empty() || layer == m_activeLayer)
				return;
			m_newLayers.erase(layer);
			m_hiddenLayers.erase(layer);
			runCommand(MapEditorCore::mergeLayer(m_state.map->map(), layer, m_activeLayer, "Merge layers"));
			if (m_overview)
				m_overview->setHiddenLayers(m_hiddenLayers);
			refreshLayers();
		});
		button(T("Delete"), "LayerDelete", [this, current] { deleteLayer(current()); });
		col->addLayout(row);
		auto *hint = label(T("Hidden layers are neither drawn nor picked; new objects go into the active layer. Double-click a name to rename the layer."), "Hint");
		hint->setWordWrap(true);
		col->addWidget(hint);
		m_layersWindow = window;
	}
	refreshLayers();
	m_layersWindow->show();
	m_layersWindow->raise();
}

void MainWindow::refreshLayers()
{
	if (!m_layersWindow)
		return;
	auto *tree = m_layersWindow->findChild<QTreeWidget *>(QStringLiteral("LayersTree"));
	if (!tree)
		return;
	const QString selected = tree->currentItem() ? tree->currentItem()->data(0, ROLE_LAYER).toString() : QString();
	const QSignalBlocker block(tree);
	tree->clear();
	if (!m_state.map)
		return;
	std::vector<MapEditorCore::LayerInfo> layers = MapEditorCore::mapLayers(m_state.map->map());
	for (const std::string &name : m_newLayers)
	{
		if (std::none_of(layers.begin(), layers.end(), [&name](const MapEditorCore::LayerInfo &l) { return l.name == name; }))
			layers.push_back({name, 0, 0});
	}
	for (const MapEditorCore::LayerInfo &layer : layers)
	{
		QString shown = layer.name.empty() ? T("Default layer") : QString::fromStdString(layer.name);
		if (layer.name == m_activeLayer && !layer.name.empty())
			shown = T("%1 (active)").arg(shown);
		auto *item = new QTreeWidgetItem(tree, {shown, QString::number(layer.objects), QString::number(layer.areas)});
		item->setData(0, ROLE_LAYER, QString::fromStdString(layer.name));
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable | (layer.name.empty() ? Qt::NoItemFlags : Qt::ItemIsEditable));
		item->setCheckState(0, m_hiddenLayers.count(layer.name) ? Qt::Unchecked : Qt::Checked);
		if (layer.name == m_activeLayer)
		{
			QFont font = item->font(0);
			font.setBold(true);
			item->setFont(0, font);
		}
		if (QString::fromStdString(layer.name) == selected)
			tree->setCurrentItem(item);
	}
}

void MainWindow::setLayerHidden(const std::string &layer, bool hidden)
{
	// The active layer stays shown, as in the old editor.
	if (hidden && layer == m_activeLayer && !layer.empty())
	{
		toast(T("The active layer cannot be hidden."));
		refreshLayers();
		return;
	}
	if (hidden)
		m_hiddenLayers.insert(layer);
	else
		m_hiddenLayers.erase(layer);
	if (m_state.map)
	{
		// Hidden objects leave the selection.
		std::vector<std::int32_t> &sel = m_state.map->selection.objects;
		std::erase_if(sel, [this](std::int32_t object) { return !pickable(object); });
	}
	if (m_overview)
		m_overview->setHiddenLayers(m_hiddenLayers);
	MapEditorCore::Changes changes;
	changes.rebuild = true;
	applyChanges(changes);
}

void MainWindow::setActiveLayer(const std::string &layer)
{
	m_activeLayer = layer == m_activeLayer ? std::string() : layer;
	if (!m_activeLayer.empty() && m_hiddenLayers.count(m_activeLayer))
		setLayerHidden(m_activeLayer, false);
	toast(m_activeLayer.empty() ? T("New objects go into the default layer") : T("New objects go into %1").arg(QString::fromStdString(m_activeLayer)));
	refreshLayers();
	updateSelectionStatus();
}

std::string MainWindow::newLayer()
{
	std::set<std::string> taken(m_newLayers);
	if (m_state.map)
	{
		for (const MapEditorCore::LayerInfo &layer : MapEditorCore::mapLayers(m_state.map->map()))
			taken.insert(layer.name);
	}
	std::string name;
	for (int n = 1; name.empty() || taken.count(name); ++n)
		name = "New Layer " + std::to_string(n);
	m_newLayers.insert(name);
	setActiveLayer(name);
	return name;
}

void MainWindow::renameLayer(const std::string &from, const std::string &to)
{
	if (!m_state.map || from.empty() || to.empty() || from == to)
		return;
	for (const MapEditorCore::LayerInfo &layer : MapEditorCore::mapLayers(m_state.map->map()))
	{
		if (layer.name == to)
		{
			toast(T("A layer %1 exists already.").arg(QString::fromStdString(to)));
			refreshLayers();
			return;
		}
	}
	if (m_newLayers.erase(from))
		m_newLayers.insert(to);
	if (m_hiddenLayers.erase(from))
		m_hiddenLayers.insert(to);
	if (m_activeLayer == from)
		m_activeLayer = to;
	runCommand(MapEditorCore::mergeLayer(m_state.map->map(), from, to, "Rename layer"));
	refreshLayers();
}

void MainWindow::deleteLayer(const std::string &layer)
{
	if (!m_state.map || layer.empty())
		return;
	// Its objects and areas go to the active layer, or to the default one if this is the active one.
	const std::string target = layer == m_activeLayer ? std::string() : m_activeLayer;
	if (layer == m_activeLayer)
		m_activeLayer.clear();
	m_newLayers.erase(layer);
	const bool wasHidden = m_hiddenLayers.erase(layer) != 0;
	runCommand(MapEditorCore::mergeLayer(m_state.map->map(), layer, target, "Delete layer"));
	if (wasHidden && m_overview)
		m_overview->setHiddenLayers(m_hiddenLayers);
	refreshLayers();
}

void MainWindow::moveSelectionToLayer(const std::string &layer)
{
	if (!m_state.map)
		return;
	// The selected area goes along when the area tool has one.
	std::vector<std::int32_t> areas;
	if (areaTool() && m_area >= 0)
		areas.push_back(m_area);
	auto command = MapEditorCore::moveToLayer(m_state.map->map(), m_state.map->selection.objects, areas, layer);
	if (!command)
	{
		toast(T("Nothing selected to move, or it is in that layer already."));
		return;
	}
	runCommand(std::move(command));
	refreshLayers();
}

void MainWindow::selectLayer(const std::string &layer)
{
	if (!m_state.map)
		return;
	std::vector<std::int32_t> objects;
	for (std::int32_t object : MapEditorCore::layerObjects(m_state.map->map(), layer))
	{
		if (((*m_state.map->map().objects)[static_cast<std::size_t>(object)].flags & 0x36) == 0 && pickable(object))
			objects.push_back(object);
	}
	m_state.map->selection.objects = objects;
	selectionChanged();
}

void MainWindow::groupSelection()
{
	if (!m_state.map)
		return;
	auto command = MapEditorCore::groupObjects(m_state.map->map(), m_state.map->selection.objects);
	if (!command)
	{
		toast(T("Select at least two objects to group them."));
		return;
	}
	runCommand(std::move(command));
	toast(T("%1 objects grouped: a click on one picks them all").arg(m_state.map->selection.objects.size()));
}

void MainWindow::ungroupSelection()
{
	if (!m_state.map)
		return;
	auto command = MapEditorCore::ungroupObjects(m_state.map->map(), m_state.map->selection.objects);
	if (!command)
	{
		toast(T("The selection has no group."));
		return;
	}
	runCommand(std::move(command));
}

} // namespace MapEditorApp
