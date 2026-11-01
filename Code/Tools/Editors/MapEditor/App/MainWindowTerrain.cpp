/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowTerrain.cpp -- the terrain mode and its height brush (raise, lower, set to a height,
// smooth) with size, feather and strength, after the old WorldBuilder's BrushTool. A stroke paints
// while the left button is held, in the 3D view and in the overview; it is one step in the history.
// Alt+click takes the height under the cursor. Textures, passability and shapes have files of their
// own (MainWindowTextures.cpp, MainWindowPathing.cpp, MainWindowShapes.cpp).

#include "App/Common.h"
#include "App/MainWindow.h"

#include "App/EngineClient.h"
#include "App/Icons.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/ToolEvent.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/AreaCommands.h"
#include "Core/CliffTexture.h"
#include "Core/Slope.h"
#include "Core/TextureBlend.h"
#include "Core/TextureCommands.h"
#include "Model/MapIO.h"

#include <QButtonGroup>
#include <QGridLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace MapEditorApp
{

namespace
{

const char *strokeName(MapEditorCore::BrushMode mode)
{
	switch (mode)
	{
	case MapEditorCore::BrushMode::Raise: return "Raise terrain";
	case MapEditorCore::BrushMode::Lower: return "Lower terrain";
	case MapEditorCore::BrushMode::Set: return "Set terrain height";
	case MapEditorCore::BrushMode::Smooth: return "Smooth terrain";
	}
	return "Change terrain";
}

} // namespace

bool MainWindow::terrainTool() const
{
	return m_workMode == WorkMode::Terrain && m_state.map && m_state.map->map().heightMap;
}

QWidget *MainWindow::buildTerrainPanel(const Tokens &t)
{
	auto *panel = new QWidget;
	panel->setObjectName(QStringLiteral("Panel"));
	panel->setAttribute(Qt::WA_StyledBackground);
	panel->setFixedWidth(300);
	auto *col = new QVBoxLayout(panel);
	col->setContentsMargins(18, 18, 18, 18);
	col->setSpacing(8);
	// Height brush or texture brush; the panel shows the settings of the one chosen.
	auto *tools = segmented({T("Height"), T("Texture"), T("Shapes"), T("Pathing")}, static_cast<int>(m_terrainTool), [this](int i) {
		const TerrainTool tool = static_cast<TerrainTool>(i);
		if (tool == m_terrainTool)
			return;
		m_terrainTool = tool;
		QTimer::singleShot(0, this, [this] { rebuild(); });
	}, panel, {true, true, true, true});
	tools->setObjectName(QStringLiteral("TerrainTools"));
	col->addWidget(tools);
	if (m_terrainTool != TerrainTool::Height)
	{
		if (m_terrainTool == TerrainTool::Texture)
			addTexturePanel(col, t);
		else if (m_terrainTool == TerrainTool::Shape)
			addShapePanel(col);
		else
			addPathingPanel(col);
		if (m_viewport)
			m_viewport->setToolMode(true);
		return panel;
	}
	col->addWidget(label(T("HEIGHT BRUSH"), "Section"));

	// The four modes as a row of buttons, one checked.
	auto *modes = new QGridLayout;
	modes->setSpacing(6);
	auto *group = new QButtonGroup(panel);
	const std::pair<MapEditorCore::BrushMode, QString> entries[] = {{MapEditorCore::BrushMode::Raise, T("Raise")},
		{MapEditorCore::BrushMode::Lower, T("Lower")}, {MapEditorCore::BrushMode::Set, T("Set height")}, {MapEditorCore::BrushMode::Smooth, T("Smooth")}};
	int n = 0;
	for (const auto &[mode, text] : entries)
	{
		auto *button = new QPushButton(text);
		button->setCheckable(true);
		button->setChecked(m_brush.mode == mode);
		button->setObjectName(QStringLiteral("BrushMode%1").arg(n));
		group->addButton(button, n);
		modes->addWidget(button, n / 2, n % 2);
		++n;
	}
	connect(group, &QButtonGroup::idClicked, this, [this](int id) {
		m_brush.mode = static_cast<MapEditorCore::BrushMode>(id);
		updateSelectionStatus();
	});
	col->addLayout(modes);

	auto *grid = new QGridLayout;
	grid->setHorizontalSpacing(10);
	grid->setVerticalSpacing(6);
	int row = 0;
	const auto spin = [&](const QString &name, const char *objectName, int minimum, int maximum, int value, const QString &suffix) {
		auto *box = new QSpinBox;
		box->setObjectName(QString::fromLatin1(objectName));
		box->setRange(minimum, maximum);
		box->setValue(value);
		box->setSuffix(suffix);
		grid->addWidget(label(name, "Meta"), row, 0);
		grid->addWidget(box, row++, 1);
		return box;
	};
	auto *size = spin(T("Size"), "BrushSize", 1, 60, static_cast<int>(std::lround(m_brush.radius)), QLatin1Char(' ') + T("cells"));
	connect(size, &QSpinBox::valueChanged, this, [this](int v) {
		m_brush.radius = static_cast<float>(v);
		applyOverlay();
		if (m_overview)
			m_overview->update();
	});
	auto *feather = spin(T("Feather"), "BrushFeather", 0, 100, static_cast<int>(std::lround(m_brush.feather * 100.0f)), QStringLiteral(" %"));
	connect(feather, &QSpinBox::valueChanged, this, [this](int v) { m_brush.feather = static_cast<float>(v) / 100.0f; });
	auto *strength = spin(T("Strength"), "BrushStrength", 1, 32, static_cast<int>(std::lround(m_brush.strength)), QString());
	connect(strength, &QSpinBox::valueChanged, this, [this](int v) { m_brush.strength = static_cast<float>(v); });
	auto *height = spin(T("Height"), "BrushHeight", 0, 255, m_brush.height, QString());
	connect(height, &QSpinBox::valueChanged, this, [this](int v) { m_brush.height = v; });
	m_brushHeightField = height;
	col->addLayout(grid);
	auto *hint = label(T("Drag to paint. \"Set height\" pulls the ground to the height above; Alt+click takes the height under the cursor. One stroke is one step to undo."), "Hint");
	hint->setWordWrap(true);
	col->addWidget(hint);
	auto *info = label(QString(), "Hint");
	m_brushInfo = info;
	col->addWidget(info);
	col->addStretch(1);
	if (m_viewport)
		m_viewport->setToolMode(true);
	return panel;
}

void MainWindow::brushMoved(bool valid, float x, float y)
{
	if (!terrainTool())
		return;
	m_brushValid = valid;
	m_brushX = x;
	m_brushY = y;
	if (m_brushInfo && valid)
	{
		const MapModel::HeightMap &hm = *m_state.map->map().heightMap;
		float cx = 0.0f, cy = 0.0f;
		MapEditorCore::worldToCell(hm, x, y, cx, cy);
		const int ix = std::clamp(static_cast<int>(std::lround(cx)), 0, hm.width - 1);
		const int iy = std::clamp(static_cast<int>(std::lround(cy)), 0, hm.height - 1);
		m_brushInfo->setText(T("Height under the cursor: %1").arg(hm.heights[static_cast<std::size_t>(iy) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(ix)]));
	}
	const bool brush = m_terrainTool == TerrainTool::Height || m_terrainTool == TerrainTool::Pathing
		|| (m_terrainTool == TerrainTool::Texture && m_textureMode == TextureMode::Paint);
	if (m_overview)
	{
		m_overview->setBrushCircle(valid && brush && m_state.display.brushOutline, x, y, m_brush.radius * CELL);
		m_overview->setToolLines(toolLines());
	}
	applyOverlay();
}

std::vector<float> MainWindow::brushCircle() const
{
	std::vector<float> points;
	const bool brush = m_terrainTool == TerrainTool::Height || m_terrainTool == TerrainTool::Pathing
		|| (m_terrainTool == TerrainTool::Texture && m_textureMode == TextureMode::Paint);
	if (!terrainTool() || !m_brushValid || !brush || !m_state.display.brushOutline)
		return points;
	for (int i = 0; i < 32; ++i)
	{
		const float a = static_cast<float>(i) * 6.2831853f / 32.0f;
		points.push_back(m_brushX + std::cos(a) * m_brush.radius * CELL);
		points.push_back(m_brushY + std::sin(a) * m_brush.radius * CELL);
	}
	return points;
}

void MainWindow::terrainToolEvent(const ToolEvent &e)
{
	if (!terrainTool())
		return;
	if (m_terrainTool == TerrainTool::Texture)
	{
		textureToolEvent(e);
		return;
	}
	if (m_terrainTool == TerrainTool::Shape)
	{
		shapeToolEvent(e);
		return;
	}
	if (m_terrainTool == TerrainTool::Pathing)
	{
		pathingToolEvent(e);
		return;
	}
	MapModel::Map &map = m_state.map->map();
	MapModel::HeightMap &hm = *map.heightMap;
	float cx = 0.0f, cy = 0.0f;
	MapEditorCore::worldToCell(hm, e.x, e.y, cx, cy);
	// Paints at the cell position and shows the change in the views at once.
	const auto paint = [&](float px, float py) {
		const MapEditorCore::CellRect changed = MapEditorCore::applyBrush(hm, px, py, m_brush);
		if (changed.empty())
			return;
		m_stroke.rect.unite(changed);
		EngineClient::instance().setHeights(changed.x0, changed.y0, changed.x1, changed.y1, hm.heights, hm.width);
		if (m_overview)
			m_overview->refreshImage();
		m_viewport->update();
	};
	switch (e.kind)
	{
	case ToolEvent::Kind::Press:
	{
		if (e.alt)
		{
			const int ix = std::clamp(static_cast<int>(std::lround(cx)), 0, hm.width - 1);
			const int iy = std::clamp(static_cast<int>(std::lround(cy)), 0, hm.height - 1);
			m_brush.height = hm.heights[static_cast<std::size_t>(iy) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(ix)];
			if (m_brushHeightField)
				m_brushHeightField->setValue(m_brush.height);
			toast(T("Height %1 taken").arg(m_brush.height));
			return;
		}
		m_stroke = Stroke{};
		m_stroke.active = true;
		m_stroke.before = hm.heights;
		m_stroke.lastX = cx;
		m_stroke.lastY = cy;
		paint(cx, cy);
		return;
	}
	case ToolEvent::Kind::Move:
	{
		brushMoved(true, e.x, e.y);
		if (!m_stroke.active)
			return;
		// Dabs a quarter of the radius apart, so a fast drag leaves no gaps.
		const float spacing = std::max(0.5f, m_brush.radius * 0.25f);
		const float dx = cx - m_stroke.lastX;
		const float dy = cy - m_stroke.lastY;
		const float distance = std::hypot(dx, dy);
		if (distance < spacing)
			return;
		const int steps = static_cast<int>(distance / spacing);
		for (int i = 1; i <= steps; ++i)
			paint(m_stroke.lastX + dx * static_cast<float>(i) / static_cast<float>(steps), m_stroke.lastY + dy * static_cast<float>(i) / static_cast<float>(steps));
		m_stroke.lastX = cx;
		m_stroke.lastY = cy;
		return;
	}
	case ToolEvent::Kind::Release:
	{
		if (!m_stroke.active)
			return;
		m_stroke.active = false;
		auto command = MapEditorCore::heightStroke(map, m_stroke.before, m_stroke.rect, strokeName(m_brush.mode));
		m_stroke.before.clear();
		if (!command)
			return;
		const MapEditorCore::Changes changes = command->changes();
		m_state.map->history.record(std::move(command));
		applyChanges(changes);
		return;
	}
	case ToolEvent::Kind::DoubleClick:
		return;
	}
}

void MainWindow::terrainChanged(const MapEditorCore::Changes &changes)
{
	if (!m_state.map || !m_state.map->map().heightMap)
		return;
	const MapModel::HeightMap &hm = *m_state.map->map().heightMap;
	EngineClient &engine = EngineClient::instance();
	const bool engineMap = m_viewport->hasDevice() && m_viewMapSerial == m_mapSerial;
	// A cell's cliff flag depends on its four corners: one cell more to the left and below.
	if (changes.passability && engineMap && m_state.map->map().blendTiles)
		engine.setCliffFlags(changes.terrainRect[0] - 1, changes.terrainRect[1] - 1, changes.terrainRect[2], changes.terrainRect[3],
			m_state.map->map().blendTiles->cliffState, hm.width);
	// The tint of slopes and cliff flags follows heights and flags.
	if ((changes.passability || changes.terrain) && (m_state.display.slopes || (m_page == Page::Map && terrainTool() && m_terrainTool == TerrainTool::Pathing)))
		updateImpassableView();
	if (!changes.terrain && !changes.textures)
	{
		if (m_overview)
			m_overview->refreshImage();
		m_viewport->update();
		updateTitle();
		return;
	}
	if (changes.textures)
	{
		// New or removed texture classes change the terrain atlas: the engine loads the map again.
		if (changes.textureClasses)
			reloadEngineMap();
		else if (m_viewport->hasDevice() && m_viewMapSerial == m_mapSerial && m_state.map->map().blendTiles)
		{
			const MapModel::BlendTiles &bt = *m_state.map->map().blendTiles;
			engine.setBlendTable(bt.blendedTiles);
			engine.setTiles(changes.terrainRect[0], changes.terrainRect[1], changes.terrainRect[2], changes.terrainRect[3], bt.tiles, bt.blends,
				bt.extraBlends, bt.cliffInfoIndices, hm.width);
		}
		if (m_overview)
			m_overview->refreshImage();
		m_viewport->update();
		updateTitle();
		return;
	}
	if (m_viewport->hasDevice() && m_viewMapSerial == m_mapSerial)
	{
		engine.setHeights(changes.terrainRect[0], changes.terrainRect[1], changes.terrainRect[2], changes.terrainRect[3], hm.heights, hm.width);
		// Objects, trees and roads stand on the ground: they follow it.
		const MapModel::WriteResult bytes = engineMapBytes();
		if (!bytes.ok() || !engine.reloadObjects(bytes.bytes))
			m_viewMapSerial = -1;
	}
	if (m_overview)
		m_overview->refreshImage();
	applyOverlay();
	updateTitle();
}

} // namespace MapEditorApp
