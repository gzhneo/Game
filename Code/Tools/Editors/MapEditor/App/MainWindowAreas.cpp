/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowAreas.cpp -- the area tools of the catalog (trigger areas and water areas), after the
// old WorldBuilder's PolygonTool and WaterTool: clicks set the points of a new area, a click on its
// first point, a double-click or Enter closes it (one step in the history); dragging a point moves
// it, a click on an edge of the selected area inserts one, dragging inside moves the whole area.
// Del removes the selected point (an area keeps three), otherwise the area. The side panel edits
// name, water, river and water height. And the self test of all that.

#include "App/MainWindow.h"

#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/ToolEvent.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/AreaCommands.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>
#include <string>

namespace MapEditorApp
{

namespace
{

// New water stands this far above the ground at its first point (world units).
constexpr float NEW_WATER_DEPTH = 10.0f;

std::int32_t rounded(float v)
{
	return static_cast<std::int32_t>(std::lround(v));
}

} // namespace

bool MainWindow::areaTool() const
{
	return m_placeTemplate == QLatin1String(AREA_TRIGGER_TOOL) || m_placeTemplate == QLatin1String(AREA_WATER_TOOL);
}

void MainWindow::areasChanged()
{
	const MapModel::Map *map = m_state.map ? &m_state.map->map() : nullptr;
	if (!map || !map->polygons || m_area < 0 || static_cast<std::size_t>(m_area) >= map->polygons->size())
	{
		m_area = -1;
		m_areaPoint = -1;
	}
	else if (m_areaPoint >= static_cast<int>((*map->polygons)[static_cast<std::size_t>(m_area)].points.size()))
		m_areaPoint = -1;
	if (m_overview)
		m_overview->setAreaMarks(m_area, m_areaPoint, m_areaDraft ? &*m_areaDraft : nullptr);
	applyOverlay();
}

void MainWindow::areaToolEvent(const ToolEvent &e)
{
	if (!m_state.map || !areaTool())
		return;
	MapModel::Map &map = m_state.map->map();
	const bool water = m_placeTemplate == QLatin1String(AREA_WATER_TOOL);
	switch (e.kind)
	{
	case ToolEvent::Kind::Press:
	{
		if (m_areaDraft)
		{
			MapModel::PolygonArea &draft = *m_areaDraft;
			if (draft.points.size() >= 3 && MapEditorCore::areaPointAt(draft, e.x, e.y, e.tolerance) == 0)
			{
				finishAreaDraft();
				return;
			}
			const std::int32_t z = draft.isWater ? draft.points[0][2] : rounded(MapEditorCore::groundHeight(map, e.x, e.y));
			draft.points.push_back({rounded(e.x), rounded(e.y), z});
			areasChanged();
			return;
		}
		// What is under the cursor: a point (the selected area first), an edge of the selected area,
		// the inside of an area. Ctrl starts a new area anyway.
		int hitArea = -1, hitPoint = -1, insertAt = -1;
		bool inside = false;
		const std::size_t count = map.polygons ? map.polygons->size() : 0;
		std::vector<int> order;
		if (m_area >= 0)
			order.push_back(m_area);
		for (std::size_t i = 0; i < count; ++i)
		{
			if (static_cast<int>(i) != m_area)
				order.push_back(static_cast<int>(i));
		}
		for (int i = 0; !e.ctrl && hitArea < 0 && i < static_cast<int>(order.size()); ++i)
		{
			const int p = MapEditorCore::areaPointAt((*map.polygons)[static_cast<std::size_t>(order[static_cast<std::size_t>(i)])], e.x, e.y, e.tolerance);
			if (p >= 0)
			{
				hitArea = order[static_cast<std::size_t>(i)];
				hitPoint = p;
			}
		}
		if (!e.ctrl && hitArea < 0 && m_area >= 0)
		{
			insertAt = MapEditorCore::areaEdgeAt((*map.polygons)[static_cast<std::size_t>(m_area)], e.x, e.y, e.tolerance);
			if (insertAt >= 0)
				hitArea = m_area;
		}
		for (int i = 0; !e.ctrl && hitArea < 0 && i < static_cast<int>(order.size()); ++i)
		{
			if (MapEditorCore::areaContains((*map.polygons)[static_cast<std::size_t>(order[static_cast<std::size_t>(i)])], e.x, e.y))
			{
				hitArea = order[static_cast<std::size_t>(i)];
				inside = true;
			}
		}
		if (hitArea >= 0)
		{
			m_areaDrag = AreaDrag{};
			m_areaDrag.active = true;
			m_areaDrag.whole = inside;
			m_areaDrag.startX = e.x;
			m_areaDrag.startY = e.y;
			m_areaDrag.before = map.polygons;
			m_area = hitArea;
			m_areaPoint = hitPoint;
			if (insertAt >= 0)
			{
				MapModel::PolygonArea &area = (*map.polygons)[static_cast<std::size_t>(m_area)];
				const std::int32_t z = area.isWater ? area.points[0][2] : rounded(MapEditorCore::groundHeight(map, e.x, e.y));
				area.points.insert(area.points.begin() + insertAt, {rounded(e.x), rounded(e.y), z});
				m_areaDrag.inserted = true;
				m_areaPoint = insertAt;
			}
			areasChanged();
			fillSelectionPanel();
			updateSelectionStatus();
			return;
		}
		// A new area; it goes into the map when it is closed.
		m_area = -1;
		m_areaPoint = -1;
		MapModel::PolygonArea draft;
		draft.isWater = water;
		const float ground = MapEditorCore::groundHeight(map, e.x, e.y);
		draft.points.push_back({rounded(e.x), rounded(e.y), rounded(water ? ground + NEW_WATER_DEPTH : ground)});
		m_areaDraft = draft;
		areasChanged();
		fillSelectionPanel();
		updateSelectionStatus();
		return;
	}
	case ToolEvent::Kind::Move:
	{
		if (!m_areaDrag.active || !map.polygons || !m_areaDrag.before || m_area < 0)
			return;
		MapModel::PolygonArea &area = (*map.polygons)[static_cast<std::size_t>(m_area)];
		const MapModel::PolygonArea &original = (*m_areaDrag.before)[static_cast<std::size_t>(m_area)];
		if (m_areaDrag.whole)
		{
			const std::int32_t dx = rounded(e.x - m_areaDrag.startX);
			const std::int32_t dy = rounded(e.y - m_areaDrag.startY);
			for (std::size_t i = 0; i < area.points.size() && i < original.points.size(); ++i)
			{
				const auto &p = original.points[i];
				const std::int32_t z = area.isWater ? p[2] : rounded(MapEditorCore::groundHeight(map, static_cast<float>(p[0] + dx), static_cast<float>(p[1] + dy)));
				area.points[i] = {p[0] + dx, p[1] + dy, z};
			}
		}
		else if (m_areaPoint >= 0)
		{
			const std::int32_t z = area.isWater ? area.points[0][2] : rounded(MapEditorCore::groundHeight(map, e.x, e.y));
			area.points[static_cast<std::size_t>(m_areaPoint)] = {rounded(e.x), rounded(e.y), z};
		}
		areasChanged();
		return;
	}
	case ToolEvent::Kind::Release:
	{
		if (!m_areaDrag.active)
			return;
		m_areaDrag.active = false;
		if (map.polygons == m_areaDrag.before)
			return;
		const std::string name = m_areaDrag.inserted ? "Insert point" : (m_areaDrag.whole ? "Move area" : "Move point");
		auto command = std::make_unique<MapEditorCore::EditAreas>(name, m_areaDrag.before, map.polygons, m_area);
		const MapEditorCore::Changes changes = command->changes();
		m_state.map->history.record(std::move(command));
		applyChanges(changes);
		return;
	}
	case ToolEvent::Kind::DoubleClick:
		if (m_areaDraft)
			finishAreaDraft();
		return;
	}
}

void MainWindow::areaToolKey(int key)
{
	if (!m_areaDraft)
		return;
	if (key == Qt::Key_Backspace)
	{
		m_areaDraft->points.pop_back();
		if (m_areaDraft->points.empty())
			m_areaDraft.reset();
		areasChanged();
		return;
	}
	finishAreaDraft();
}

void MainWindow::finishAreaDraft()
{
	if (!m_areaDraft || !m_state.map)
		return;
	if (m_areaDraft->points.size() < 3)
	{
		toast(T("An area needs at least three points."));
		return;
	}
	auto command = MapEditorCore::addArea(m_state.map->map(), *m_areaDraft);
	m_areaDraft.reset();
	m_area = command->area();
	m_areaPoint = -1;
	runCommand(std::move(command));
	updateSelectionStatus();
}

bool MainWindow::deleteAreaSelection()
{
	if (!m_state.map || !areaTool())
		return false;
	if (m_areaDraft)
	{
		m_areaDraft.reset();
		areasChanged();
		fillSelectionPanel();
		return true;
	}
	const MapModel::Map &map = m_state.map->map();
	if (!map.polygons || m_area < 0 || static_cast<std::size_t>(m_area) >= map.polygons->size())
		return false;
	const MapModel::PolygonArea &area = (*map.polygons)[static_cast<std::size_t>(m_area)];
	if (m_areaPoint >= 0)
	{
		if (area.points.size() <= 3)
		{
			toast(T("An area keeps at least three points; Del without a selected point deletes the area."));
			m_areaPoint = -1;
			areasChanged();
			return true;
		}
		MapModel::PolygonArea after = area;
		after.points.erase(after.points.begin() + m_areaPoint);
		m_areaPoint = -1;
		runCommand(MapEditorCore::changeArea(map, m_area, after, "Remove point"));
		return true;
	}
	auto command = MapEditorCore::deleteArea(map, m_area);
	m_area = -1;
	runCommand(std::move(command));
	return true;
}

// ------------------------------------------------------------------------------------------------
// Side panel of the selected area

void MainWindow::addAreaEditors(QVBoxLayout *box)
{
	const MapModel::Map &map = m_state.map->map();
	const MapModel::PolygonArea &area = (*map.polygons)[static_cast<std::size_t>(m_area)];
	const int index = m_area;
	box->addWidget(label(area.isWater ? T("WATER AREA") : T("AREA"), "Section"));
	auto *name = new QLineEdit(utf8(area.name));
	name->setObjectName(QStringLiteral("AreaName"));
	connect(name, &QLineEdit::editingFinished, this, [this, name, index] {
		const std::string text = name->text().trimmed().toUtf8().toStdString();
		const MapModel::Map &m = m_state.map->map();
		if (text.empty() || !m.polygons || static_cast<std::size_t>(index) >= m.polygons->size())
			return;
		MapModel::PolygonArea after = (*m.polygons)[static_cast<std::size_t>(index)];
		after.name = text;
		runCommand(MapEditorCore::changeArea(m, index, after, "Rename area"));
	});
	box->addWidget(name);

	// Water: the points carry the water level as their height.
	auto *water = new QCheckBox(T("Water area"));
	water->setObjectName(QStringLiteral("AreaWater"));
	water->setChecked(area.isWater);
	connect(water, &QCheckBox::clicked, this, [this, index](bool on) {
		const MapModel::Map &m = m_state.map->map();
		MapModel::PolygonArea after = (*m.polygons)[static_cast<std::size_t>(index)];
		after.isWater = on;
		if (!on)
			after.isRiver = false;
		else if (!after.points.empty())
		{
			const std::int32_t level = rounded(MapEditorCore::groundHeight(m, static_cast<float>(after.points[0][0]), static_cast<float>(after.points[0][1])) + NEW_WATER_DEPTH);
			for (auto &p : after.points)
				p[2] = level;
		}
		runCommand(MapEditorCore::changeArea(m, index, after, "Change area"));
	});
	box->addWidget(water);
	if (area.isWater)
	{
		auto *river = new QCheckBox(T("River (flows from the start point)"));
		river->setChecked(area.isRiver);
		connect(river, &QCheckBox::clicked, this, [this, index](bool on) {
			const MapModel::Map &m = m_state.map->map();
			MapModel::PolygonArea after = (*m.polygons)[static_cast<std::size_t>(index)];
			after.isRiver = on;
			runCommand(MapEditorCore::changeArea(m, index, after, "Change area"));
		});
		box->addWidget(river);
		auto *row = new QHBoxLayout;
		row->addWidget(label(T("Water height"), "Meta"));
		auto *height = new QSpinBox;
		height->setObjectName(QStringLiteral("AreaWaterHeight"));
		height->setRange(-1000, 5000);
		height->setValue(area.points.empty() ? 0 : area.points[0][2]);
		connect(height, &QSpinBox::editingFinished, this, [this, height, index] {
			const MapModel::Map &m = m_state.map->map();
			MapModel::PolygonArea after = (*m.polygons)[static_cast<std::size_t>(index)];
			for (auto &p : after.points)
				p[2] = height->value();
			runCommand(MapEditorCore::changeArea(m, index, after, "Change water height"));
		});
		row->addWidget(height, 1);
		box->addLayout(row);
	}
	box->addWidget(label(T("%1 points, ID %2").arg(area.points.size()).arg(area.id), "Hint"));
	auto *buttons = new QHBoxLayout;
	auto *removePoint = new QPushButton(T("Remove point"));
	removePoint->setEnabled(m_areaPoint >= 0 && area.points.size() > 3);
	connect(removePoint, &QPushButton::clicked, this, [this] { deleteAreaSelection(); });
	auto *remove = new QPushButton(T("Delete area"));
	remove->setObjectName(QStringLiteral("AreaDelete"));
	connect(remove, &QPushButton::clicked, this, [this] {
		m_areaPoint = -1;
		deleteAreaSelection();
	});
	buttons->addWidget(removePoint);
	buttons->addStretch(1);
	buttons->addWidget(remove);
	box->addLayout(buttons);
}

} // namespace MapEditorApp
