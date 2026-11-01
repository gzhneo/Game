/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowPathing.cpp -- passability in the terrain mode: the brush for cliff flags, the flags from the
// heights, and the red cells and slopes over both views.

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
#include <QHBoxLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>

namespace MapEditorApp
{

void MainWindow::addPathingPanel(QVBoxLayout *col)
{
	col->addWidget(label(T("PASSABILITY"), "Section"));
	auto *modes = segmented({T("Impassable"), T("Passable")}, m_markImpassable ? 0 : 1, [this](int i) {
		m_markImpassable = i == 0;
		updateSelectionStatus();
	},
		nullptr, {true, true});
	modes->setObjectName(QStringLiteral("PathingModes"));
	col->addWidget(modes);
	auto *row = new QHBoxLayout;
	row->addWidget(label(T("Size"), "Meta"));
	auto *size = new QSpinBox;
	size->setObjectName(QStringLiteral("BrushSize"));
	size->setRange(1, 60);
	size->setSuffix(QLatin1Char(' ') + T("cells"));
	size->setValue(static_cast<int>(std::lround(m_brush.radius)));
	connect(size, &QSpinBox::valueChanged, this, [this](int v) {
		m_brush.radius = static_cast<float>(v);
		applyOverlay();
	});
	row->addWidget(size, 1);
	col->addLayout(row);
	// All flags from the heights again, as the game does for maps without them.
	auto *reset = new QPushButton(T("Set from the heights"));
	reset->setObjectName(QStringLiteral("CliffsFromHeights"));
	connect(reset, &QPushButton::clicked, this, [this] {
		MapModel::Map &map = m_state.map->map();
		if (!map.blendTiles || !map.heightMap)
			return;
		const MapModel::HeightMap &hm = *map.heightMap;
		const std::vector<std::uint8_t> before = map.blendTiles->cliffState;
		MapModel::updateCliffFlags(hm, map.blendTiles->cliffState, 0, 0, hm.width - 1, hm.height - 1);
		auto command = MapEditorCore::cliffStroke(map, before, MapEditorCore::CellRect{0, 0, hm.width - 1, hm.height - 1}, "Passability from heights");
		if (!command)
		{
			toast(T("The passability already matches the heights."));
			return;
		}
		const MapEditorCore::Changes changes = command->changes();
		m_state.map->history.record(std::move(command));
		applyChanges(changes);
	});
	col->addWidget(reset);
	auto *hint = label(T("Red cells are impassable: units cannot go there. Drag to mark cells; height changes set the cells they touch from the new slope, as the game does."), "Hint");
	hint->setWordWrap(true);
	col->addWidget(hint);
	col->addStretch(1);
}

void MainWindow::updateImpassableView()
{
	const bool show = m_page == Page::Map && terrainTool() && m_terrainTool == TerrainTool::Pathing;
	const float slopes = m_state.display.slopes ? static_cast<float>(m_state.display.slopeLimit) : 0.0f;
	if (m_overview)
	{
		m_overview->setShowImpassable(show);
		m_overview->setSlopes(slopes);
	}
	if (m_viewport && m_viewport->hasDevice() && m_viewMapSerial == m_mapSerial && m_state.map && m_state.map->map().heightMap)
	{
		const MapModel::HeightMap &hm = *m_state.map->map().heightMap;
		// Slopes and cliff flags lie over the territories of the fairness report.
		std::vector<std::uint32_t> tint = MapEditorCore::terrainTint(m_state.map->map(), slopes, show);
		if (m_territoryTint.size() == static_cast<std::size_t>(hm.width) * static_cast<std::size_t>(hm.height))
		{
			if (tint.empty())
				tint = m_territoryTint;
			else
				for (std::size_t i = 0; i < tint.size(); ++i)
					if ((tint[i] >> 24) == 0)
						tint[i] = m_territoryTint[i];
		}
		EngineClient::instance().setTerrainTint(tint, hm.width, hm.height, hm.borderSize);
		m_viewport->update();
	}
}

void MainWindow::pathingToolEvent(const ToolEvent &e)
{
	MapModel::Map &map = m_state.map->map();
	if (!map.blendTiles || !map.heightMap)
		return;
	std::vector<std::uint8_t> &flags = map.blendTiles->cliffState;
	const MapModel::HeightMap &hm = *map.heightMap;
	float cx = 0.0f, cy = 0.0f;
	MapEditorCore::worldToCell(hm, e.x, e.y, cx, cy);
	const auto paint = [&](float px, float py) {
		const MapEditorCore::CellRect changed = MapEditorCore::paintPassability(flags, hm.width, hm.height, px, py, m_brush.radius, m_markImpassable);
		if (changed.empty())
			return;
		m_stroke.rect.unite(changed);
		if (m_viewport->hasDevice() && m_viewMapSerial == m_mapSerial)
			EngineClient::instance().setCliffFlags(changed.x0, changed.y0, changed.x1, changed.y1, flags, hm.width);
		if (m_overview)
			m_overview->refreshImage();
		m_viewport->update();
	};
	switch (e.kind)
	{
	case ToolEvent::Kind::Press:
		if (flags.empty())
		{
			toast(T("The map has no passability data."));
			return;
		}
		m_stroke = Stroke{};
		m_stroke.active = true;
		m_stroke.lastX = cx;
		m_stroke.lastY = cy;
		m_cliffsBefore = flags;
		paint(cx, cy);
		return;
	case ToolEvent::Kind::Move:
	{
		brushMoved(true, e.x, e.y);
		if (!m_stroke.active)
			return;
		const float spacing = std::max(0.5f, m_brush.radius * 0.5f);
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
		auto command = MapEditorCore::cliffStroke(map, m_cliffsBefore, m_stroke.rect, m_markImpassable ? "Mark impassable" : "Mark passable");
		m_cliffsBefore.clear();
		if (command)
			m_state.map->history.record(std::move(command));
		updateTitle();
		return;
	}
	case ToolEvent::Kind::DoubleClick:
		return;
	}
}

// ------------------------------------------------------------------------------------------------

} // namespace MapEditorApp
