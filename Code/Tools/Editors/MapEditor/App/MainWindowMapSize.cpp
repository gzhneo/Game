/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowMapSize.cpp -- a new map and a new size for the open one.

#include "App/MainWindow.h"

#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/MapSize.h"
#include "Model/MapIO.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace MapEditorApp
{

namespace
{

// Values of the last new map in this session, as the old editor kept them.
struct NewMapChoice
{
	int width = 100;
	int height = 100;
	int border = 30;
	int initialHeight = 16;
	QString texture;
	bool water = true;
	int players = 0;
	MapEditorCore::StartLayout layout = MapEditorCore::StartLayout::Corners;
};
NewMapChoice s_newMap;

MapModel::Light light(const MeLight &l)
{
	MapModel::Light out;
	for (int i = 0; i < 3; ++i)
	{
		out.ambient[static_cast<std::size_t>(i)] = l.ambient[i];
		out.diffuse[static_cast<std::size_t>(i)] = l.diffuse[i];
		out.position[static_cast<std::size_t>(i)] = l.position[i];
	}
	return out;
}

QSpinBox *spinBox(int lo, int hi, int value, const char *name)
{
	auto *spin = new QSpinBox;
	spin->setRange(lo, hi);
	spin->setValue(value);
	spin->setObjectName(QString::fromLatin1(name));
	return spin;
}

} // namespace

std::optional<MapEditorCore::NewMapOptions> MainWindow::newMapOptions(const QString &texture, QString *error)
{
	QString problem;
	bool chooseData = false;
	bool restart = false;
	if (!ensureEngine(&problem, &chooseData, &restart))
	{
		if (error)
			*error = problem;
		return std::nullopt;
	}
	EngineClient &engine = EngineClient::instance();
	MapEditorCore::NewMapOptions options;
	const MeTerrainInfo *chosen = nullptr;
	for (const MeTerrainInfo &info : engine.terrains())
	{
		if (info.blendEdge)
			continue;
		const QString name = QString::fromLatin1(info.name);
		if (name == texture || (!chosen && texture.isEmpty() && name.contains(QStringLiteral("Grass"), Qt::CaseInsensitive)))
			chosen = &info;
	}
	if (!chosen)
	{
		for (const MeTerrainInfo &info : engine.terrains())
			if (!info.blendEdge && !chosen)
				chosen = &info;
	}
	if (!chosen)
	{
		if (error)
			*error = T("The game data has no terrain textures.");
		return std::nullopt;
	}
	options.texture = chosen->name;
	options.textureTiles = chosen->numTiles;
	options.textureWidth = chosen->width;
	MeMapDefaults defaults{};
	if (engine.mapDefaults(defaults))
	{
		MapModel::GlobalLighting lighting;
		lighting.timeOfDay = std::clamp(defaults.timeOfDay, 1, 4);
		for (std::size_t t = 0; t < 4; ++t)
			for (std::size_t i = 0; i < 3; ++i)
			{
				lighting.times[t].terrain[i] = light(defaults.terrain[t][i]);
				lighting.times[t].objects[i] = light(defaults.objects[t][i]);
			}
		options.lighting = lighting;
		options.waterHeight = defaults.waterHeight;
	}
	return options;
}

void MainWindow::newMapDialog()
{
	if (!confirmUnsavedChanges())
		return;
	QString error;
	if (!newMapOptions(s_newMap.texture, &error))
	{
		toast(T("A new map needs the game data: %1").arg(error));
		return;
	}
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("NewMapWindow"));
	dialog.setWindowTitle(T("New map"));
	auto *col = new QVBoxLayout(&dialog);
	auto *form = new QFormLayout;
	col->addLayout(form);
	QSpinBox *width = spinBox(MapEditorCore::MIN_MAP_CELLS, MapEditorCore::MAX_MAP_CELLS, s_newMap.width, "NewMapWidth");
	QSpinBox *height = spinBox(MapEditorCore::MIN_MAP_CELLS, MapEditorCore::MAX_MAP_CELLS, s_newMap.height, "NewMapHeight");
	QSpinBox *border = spinBox(0, MapEditorCore::MAX_MAP_BORDER, s_newMap.border, "NewMapBorder");
	QSpinBox *ground = spinBox(0, 255, s_newMap.initialHeight, "NewMapGround");
	form->addRow(T("Width (cells)"), width);
	form->addRow(T("Height (cells)"), height);
	form->addRow(T("Border (cells)"), border);
	form->addRow(T("Ground height"), ground);
	auto *texture = new QComboBox;
	texture->setObjectName(QStringLiteral("NewMapTexture"));
	const std::optional<MapEditorCore::NewMapOptions> preset = newMapOptions(s_newMap.texture, nullptr);
	for (const MeTerrainInfo &info : EngineClient::instance().terrains())
		if (!info.blendEdge)
			texture->addItem(QString::fromLatin1(info.name));
	if (preset)
		texture->setCurrentText(QString::fromStdString(preset->texture));
	form->addRow(T("Texture"), texture);
	auto *water = new QCheckBox(T("Water area over the whole map"));
	water->setChecked(s_newMap.water);
	form->addRow(QString(), water);
	auto *players = new QComboBox;
	players->setObjectName(QStringLiteral("NewMapPlayers"));
	players->addItem(T("None"), 0);
	for (int n = 2; n <= MapEditorCore::MAX_START_POSITIONS; ++n)
		players->addItem(QString::number(n), n);
	players->setCurrentIndex(std::max(0, players->findData(s_newMap.players)));
	form->addRow(T("Start positions"), players);
	auto *layout = new QComboBox;
	layout->setObjectName(QStringLiteral("NewMapLayout"));
	layout->addItem(T("Corners, then edges"), static_cast<int>(MapEditorCore::StartLayout::Corners));
	layout->addItem(T("Circle, opposite pairs"), static_cast<int>(MapEditorCore::StartLayout::Circle));
	layout->addItem(T("Two sides (odd against even)"), static_cast<int>(MapEditorCore::StartLayout::TwoSides));
	layout->setCurrentIndex(std::max(0, layout->findData(static_cast<int>(s_newMap.layout))));
	layout->setEnabled(s_newMap.players > 0);
	connect(players, &QComboBox::currentIndexChanged, &dialog, [players, layout] { layout->setEnabled(players->currentData().toInt() > 0); });
	form->addRow(T("Arrangement"), layout);
	auto *size = label(QString(), "Hint");
	const auto updateSize = [size, width, height] {
		size->setText(T("Playable area %1 x %2 world units").arg(width->value() * 10).arg(height->value() * 10));
	};
	connect(width, &QSpinBox::valueChanged, &dialog, updateSize);
	connect(height, &QSpinBox::valueChanged, &dialog, updateSize);
	updateSize();
	col->addWidget(size);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	col->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return;

	s_newMap = {width->value(), height->value(), border->value(), ground->value(), texture->currentText(), water->isChecked(),
		players->currentData().toInt(), static_cast<MapEditorCore::StartLayout>(layout->currentData().toInt())};
	std::optional<MapEditorCore::NewMapOptions> options = newMapOptions(s_newMap.texture, &error);
	if (!options)
	{
		toast(T("A new map needs the game data: %1").arg(error));
		return;
	}
	options->width = s_newMap.width;
	options->height = s_newMap.height;
	options->border = s_newMap.border;
	options->initialHeight = s_newMap.initialHeight;
	options->players = s_newMap.players;
	options->layout = s_newMap.layout;
	if (!s_newMap.water)
		options->waterHeight.reset();
	std::optional<MapModel::Map> map = MapEditorCore::newMap(*options);
	if (!map)
	{
		toast(T("The map cannot be made with these values."));
		return;
	}
	openNewMap(std::move(*map));
}

void MainWindow::openNewMap(MapModel::Map map)
{
	auto open = std::make_unique<OpenMap>();
	open->loaded.result.map = std::move(map);
	open->loaded.compression = 1;
	m_state.map = std::move(open);
	++m_mapSerial;
	mapReplaced();
	m_page = Page::Map;
	rebuild();
}

void MainWindow::resizeMapDialog()
{
	if (!m_state.map || !m_state.map->map().heightMap)
		return;
	finishMold(true);
	const MapModel::HeightMap &hm = *m_state.map->map().heightMap;
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("ResizeMapWindow"));
	dialog.setWindowTitle(T("Resize map"));
	auto *col = new QVBoxLayout(&dialog);
	auto *form = new QFormLayout;
	col->addLayout(form);
	QSpinBox *width = spinBox(MapEditorCore::MIN_MAP_CELLS, MapEditorCore::MAX_MAP_CELLS, hm.width - 2 * hm.borderSize, "ResizeWidth");
	QSpinBox *height = spinBox(MapEditorCore::MIN_MAP_CELLS, MapEditorCore::MAX_MAP_CELLS, hm.height - 2 * hm.borderSize, "ResizeHeight");
	QSpinBox *border = spinBox(0, MapEditorCore::MAX_MAP_BORDER, hm.borderSize, "ResizeBorder");
	form->addRow(T("Width (cells)"), width);
	form->addRow(T("Height (cells)"), height);
	form->addRow(T("Border (cells)"), border);

	// Anchor: which part of the map stays in place, as a 3 x 3 grid (top row is north).
	auto *grid = new QGridLayout;
	grid->setSpacing(2);
	auto *anchors = new QButtonGroup(&dialog);
	for (int row = 0; row < 3; ++row)
		for (int column = 0; column < 3; ++column)
		{
			auto *b = new QToolButton;
			b->setCheckable(true);
			b->setFixedSize(28, 28);
			b->setText(row == 1 && column == 1 ? QStringLiteral("\u25CF") : QStringLiteral("\u25CB"));
			grid->addWidget(b, row, column);
			anchors->addButton(b, row * 3 + column);
		}
	anchors->button(4)->setChecked(true);
	auto *anchorBox = new QWidget;
	anchorBox->setLayout(grid);
	form->addRow(T("Keep in place"), anchorBox);
	auto *info = label(QString(), "Hint");
	info->setWordWrap(true);
	col->addWidget(info);

	const auto options = [width, height, border, anchors] {
		MapEditorCore::ResizeOptions o;
		o.width = width->value();
		o.height = height->value();
		o.border = border->value();
		const int id = std::max(0, anchors->checkedId());
		const int column = id % 3;
		const int row = id / 3;
		o.x = column == 0 ? MapEditorCore::Anchor::Start : (column == 2 ? MapEditorCore::Anchor::End : MapEditorCore::Anchor::Centre);
		o.y = row == 0 ? MapEditorCore::Anchor::End : (row == 2 ? MapEditorCore::Anchor::Start : MapEditorCore::Anchor::Centre);
		return o;
	};
	const auto update = [this, info, options] {
		const int outside = MapEditorCore::objectsOutsideAfterResize(m_state.map->map(), options());
		info->setText(outside > 0 ? T("%1 objects will lie outside the playable area; they stay in the map.").arg(outside)
			: T("Everything placed moves along with the terrain."));
	};
	connect(width, &QSpinBox::valueChanged, &dialog, update);
	connect(height, &QSpinBox::valueChanged, &dialog, update);
	connect(border, &QSpinBox::valueChanged, &dialog, update);
	connect(anchors, &QButtonGroup::idClicked, &dialog, update);
	update();
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	col->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return;
	resizeMapTo(options());
}

bool MainWindow::resizeMapTo(const MapEditorCore::ResizeOptions &options)
{
	auto command = MapEditorCore::resizeMap(m_state.map->map(), options);
	if (!command)
	{
		toast(T("The size stays as it is."));
		return false;
	}
	const MapEditorCore::Changes changes = command->changes();
	m_state.map->history.execute(std::move(command), m_state.map->map());
	applyChanges(changes);
	return true;
}

void MainWindow::mapResized()
{
	// Every cell index and position changed: the views start again from the model.
	m_state.map->selection.clear();
	refreshLayers();
	++m_mapSerial;
	rebuild();
	if (m_settingsWindow)
		m_settingsWindow->refresh();
}

} // namespace MapEditorApp
