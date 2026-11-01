/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowSettings.cpp -- the map settings window (after the old WorldBuilder's MapSettings,
// GlobalLightOptions and ShadowOptions): name, time of day, weather, compression; the three lights of
// the time of day for terrain and objects; the shadow colour. Every change is a step in the history;
// the 3D view loads the map again shortly after, keeping the camera. And the playable areas tool
// (BorderTool): drag from anywhere for a new area, drag a corner or edge to resize.

#include "App/Common.h"
#include "App/MainWindow.h"

#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/SunDial.h"
#include "App/MapOverview.h"
#include "App/ToolEvent.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/MapSettingsCommands.h"

#include <QGridLayout>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>

namespace MapEditorApp
{

bool MainWindow::borderTool() const
{
	return m_placeTemplate == QLatin1String(BORDER_TOOL);
}

void MainWindow::editSettings(const char *name, const std::function<void(MapModel::Map &)> &change)
{
	if (!m_state.map)
		return;
	MapModel::Map &map = m_state.map->map();
	const MapEditorCore::MapSettings before = MapEditorCore::mapSettings(map);
	change(map);
	auto step = MapEditorCore::settingsStep(map, before, name);
	if (!step)
		return;
	const MapEditorCore::Changes changes = step->changes();
	m_state.map->history.record(std::move(step));
	m_settingsEditing = true;
	applyChanges(changes);
	m_settingsEditing = false;
}

void MainWindow::editLighting(const char *name, const std::function<void(MapModel::Map &)> &change)
{
	m_lightingOnly = true;
	editSettings(name, change);
	m_lightingOnly = false;
}

void MainWindow::previewLighting(const MapModel::GlobalLighting &lighting)
{
	if (m_viewport && m_viewport->hasDevice() && EngineClient::instance().setLighting(lighting))
		m_viewport->update();
}

void MainWindow::pushLighting()
{
	if (m_state.map && m_state.map->map().lighting && m_viewport && m_viewport->hasDevice() && EngineClient::instance().setLighting(*m_state.map->map().lighting))
		m_viewport->update();
}

void MainWindow::settingsChanged(const MapEditorCore::Changes &changes)
{
	// Lighting goes to the view at once; the sun shadows follow in the next frame.
	if (changes.settings)
		pushLighting();
	if (changes.settings && !m_lightingOnly)
	{
		// Lighting, time of day and weather are read with the map: load it again once the edits pause.
		if (!m_reloadTimer)
		{
			m_reloadTimer = new QTimer(this);
			m_reloadTimer->setSingleShot(true);
			m_reloadTimer->setInterval(300);
			connect(m_reloadTimer, &QTimer::timeout, this, [this] {
				reloadEngineMap();
				if (m_viewport)
					m_viewport->update();
			});
		}
		m_reloadTimer->start();
	}
	if (changes.boundaries && m_overview)
		m_overview->setToolLines(toolLines());
	if (!m_settingsEditing && m_settingsWindow)
		m_settingsWindow->refresh();
	applyOverlay();
	updateTitle();
	if (changes.boundaries)
		fillSelectionPanel();
}

void MainWindow::showMapSettings()
{
	if (!m_state.map)
		return;
	if (!m_settingsWindow)
		m_settingsWindow = new MapSettingsWindow(*this, *this, this);
	m_settingsWindow->refresh();
	m_settingsWindow->show();
	m_settingsWindow->raise();
}

// ------------------------------------------------------------------------------------------------
// Playable areas

std::vector<float> MainWindow::boundaryLines() const
{
	std::vector<float> lines;
	if (!m_state.map || !m_state.map->map().heightMap)
		return lines;
	const std::vector<MapModel::Point2> &areas = m_state.map->map().heightMap->boundaries;
	for (std::size_t i = 0; i < areas.size(); ++i)
	{
		const float x = static_cast<float>(areas[i].x) * CELL;
		const float y = static_cast<float>(areas[i].y) * CELL;
		lines.insert(lines.end(), {0.0f, y, x, y, x, y, x, 0.0f});
		// The chosen area gets a handle at its corner.
		if (static_cast<int>(i) == m_boundary)
		{
			const float h = 8.0f;
			lines.insert(lines.end(), {x - h, y - h, x + h, y - h, x + h, y - h, x + h, y + h, x + h, y + h, x - h, y + h, x - h, y + h, x - h, y - h});
		}
	}
	return lines;
}

void MainWindow::boundaryToolEvent(const ToolEvent &e)
{
	MapModel::Map &map = m_state.map->map();
	if (!map.heightMap)
		return;
	std::vector<MapModel::Point2> &areas = map.heightMap->boundaries;
	const auto cell = [](float v) { return std::max(1, static_cast<int>(std::lround(v / CELL))); };
	const auto refresh = [this] {
		if (m_overview)
			m_overview->setToolLines(toolLines());
		applyOverlay();
	};
	switch (e.kind)
	{
	case ToolEvent::Kind::Press:
	{
		m_boundaryDrag = BoundaryDrag{};
		m_boundaryDrag.before = MapEditorCore::mapSettings(map);
		const MapEditorCore::BoundaryHit hit = MapEditorCore::boundaryAt(areas, e.x, e.y, std::max(e.tolerance, 6.0f));
		if (hit.index < 0)
		{
			// A new area, its corner following the drag.
			areas.push_back({cell(e.x), cell(e.y)});
			m_boundaryDrag.index = static_cast<int>(areas.size()) - 1;
			m_boundaryDrag.part = MapEditorCore::BoundaryHit::Part::Corner;
		}
		else
		{
			m_boundaryDrag.index = hit.index;
			m_boundaryDrag.part = hit.part;
		}
		m_boundaryDrag.active = true;
		m_boundary = m_boundaryDrag.index;
		refresh();
		return;
	}
	case ToolEvent::Kind::Move:
	{
		if (!m_boundaryDrag.active || m_boundaryDrag.index < 0 || static_cast<std::size_t>(m_boundaryDrag.index) >= areas.size())
			return;
		MapModel::Point2 &area = areas[static_cast<std::size_t>(m_boundaryDrag.index)];
		if (m_boundaryDrag.part != MapEditorCore::BoundaryHit::Part::Top)
			area.x = cell(e.x);
		if (m_boundaryDrag.part != MapEditorCore::BoundaryHit::Part::Right)
			area.y = cell(e.y);
		refresh();
		return;
	}
	case ToolEvent::Kind::Release:
	{
		if (!m_boundaryDrag.active)
			return;
		m_boundaryDrag.active = false;
		auto step = MapEditorCore::settingsStep(map, m_boundaryDrag.before, "Change playable area");
		if (step)
		{
			const MapEditorCore::Changes changes = step->changes();
			m_state.map->history.record(std::move(step));
			applyChanges(changes);
		}
		fillSelectionPanel();
		return;
	}
	case ToolEvent::Kind::DoubleClick:
		return;
	}
}

bool MainWindow::deleteBoundarySelection()
{
	if (!borderTool() || !m_state.map || !m_state.map->map().heightMap || m_boundary < 0
		|| static_cast<std::size_t>(m_boundary) >= m_state.map->map().heightMap->boundaries.size())
		return false;
	const int index = m_boundary;
	m_boundary = -1;
	editSettings("Delete playable area", [index](MapModel::Map &m) {
		m.heightMap->boundaries.erase(m.heightMap->boundaries.begin() + index);
	});
	return true;
}

void MainWindow::addBoundaryEditors(QVBoxLayout *box)
{
	box->addWidget(label(T("PLAYABLE AREAS"), "Section"));
	const std::vector<MapModel::Point2> &areas = m_state.map->map().heightMap ? m_state.map->map().heightMap->boundaries : std::vector<MapModel::Point2>{};
	auto *list = new QListWidget;
	list->setObjectName(QStringLiteral("BoundaryList"));
	list->setMaximumHeight(120);
	for (std::size_t i = 0; i < areas.size(); ++i)
		list->addItem(T("Area %1: %2 x %3 cells").arg(i + 1).arg(areas[i].x).arg(areas[i].y));
	if (m_boundary >= 0 && m_boundary < list->count())
		list->setCurrentRow(m_boundary);
	connect(list, &QListWidget::currentRowChanged, this, [this](int row) {
		m_boundary = row;
		QTimer::singleShot(0, this, [this] {
			fillSelectionPanel();
			applyOverlay();
			if (m_overview)
				m_overview->setToolLines(toolLines());
		});
	});
	box->addWidget(list);
	if (m_boundary >= 0 && static_cast<std::size_t>(m_boundary) < areas.size())
	{
		auto *grid = new QGridLayout;
		const int index = m_boundary;
		const auto size = [&](int row, const QString &text, int value, bool width) {
			grid->addWidget(label(text, "Meta"), row, 0);
			auto *spin = new QSpinBox;
			spin->setObjectName(width ? QStringLiteral("BoundaryWidth") : QStringLiteral("BoundaryHeight"));
			spin->setRange(1, 10000);
			spin->setValue(value);
			spin->setSuffix(QLatin1Char(' ') + T("cells"));
			connect(spin, &QSpinBox::editingFinished, this, [this, spin, index, width] {
				const int v = spin->value();
				editSettings("Change playable area", [index, width, v](MapModel::Map &m) {
					if (m.heightMap && static_cast<std::size_t>(index) < m.heightMap->boundaries.size())
						(width ? m.heightMap->boundaries[static_cast<std::size_t>(index)].x : m.heightMap->boundaries[static_cast<std::size_t>(index)].y) = v;
				});
			});
			grid->addWidget(spin, row, 1);
		};
		size(0, T("Width"), areas[static_cast<std::size_t>(index)].x, true);
		size(1, T("Height"), areas[static_cast<std::size_t>(index)].y, false);
		box->addLayout(grid);
		auto *remove = new QPushButton(T("Delete area"));
		connect(remove, &QPushButton::clicked, this, [this] { deleteBoundarySelection(); });
		box->addWidget(remove);
	}
	auto *hint = label(T("Each area runs from the map's lower left corner. Drag from anywhere for a new one; drag a corner or an edge to resize, Del deletes the chosen one. Scripts switch between the areas."), "Hint");
	hint->setWordWrap(true);
	box->addWidget(hint);
}

} // namespace MapEditorApp
