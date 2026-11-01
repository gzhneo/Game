/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowRoads.cpp -- the road tool (catalog: a road type from Roads.ini), after the old
// WorldBuilder's RoadTool: dragging draws a segment, whose ends snap to the ends of other segments;
// starting on the middle of a segment cuts it there so the new road branches off. A click selects
// a segment (type and corners in the side panel, Del deletes it), Shift+drag on a road end moves
// every end that meets there. And the self test of all that.

#include "App/MainWindow.h"

#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/ToolEvent.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/RoadCommands.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace MapEditorApp
{

namespace
{

bool validSegment(const MapModel::Map &map, int first)
{
	const std::vector<std::int32_t> segments = MapEditorCore::roadSegments(map, MapEditorCore::SegmentKind::Both);
	return std::find(segments.begin(), segments.end(), first) != segments.end();
}

int cornerIndex(std::int32_t flags)
{
	if (flags & MapEditorCore::ROAD_CORNER_ANGLED)
		return 1;
	return (flags & MapEditorCore::ROAD_CORNER_TIGHT) ? 2 : 0;
}

std::int32_t cornerFlags(int index)
{
	return index == 1 ? MapEditorCore::ROAD_CORNER_ANGLED : (index == 2 ? MapEditorCore::ROAD_CORNER_TIGHT : 0);
}

} // namespace

bool MainWindow::roadTool() const
{
	return m_placeTemplate.startsWith(QLatin1String(ROAD_TOOL_PREFIX)) || bridgeTool();
}

bool MainWindow::bridgeTool() const
{
	return m_placeTemplate.startsWith(QLatin1String(BRIDGE_TOOL_PREFIX));
}

std::string MainWindow::roadToolType() const
{
	const char *prefix = bridgeTool() ? BRIDGE_TOOL_PREFIX : ROAD_TOOL_PREFIX;
	return m_placeTemplate.mid(static_cast<qsizetype>(std::strlen(prefix))).toUtf8().toStdString();
}

std::vector<float> MainWindow::toolLines() const
{
	// The symmetry dialog shows its mirror lines over any tool.
	if (!m_symmetryLines.empty())
		return m_symmetryLines;
	if (terrainTool())
		return terrainLines();
	if (placementTool())
		return placementLines();
	std::vector<float> lines;
	if (!m_state.map || !roadTool())
		return lines;
	const MapModel::Map &map = m_state.map->map();
	if (m_roadDrag.active && !m_roadDrag.move)
		lines = {m_roadDrag.x1, m_roadDrag.y1, m_roadDrag.x2, m_roadDrag.y2};
	if (validSegment(map, m_road))
	{
		const MapModel::MapObject &a = (*map.objects)[static_cast<std::size_t>(m_road)];
		const MapModel::MapObject &b = (*map.objects)[static_cast<std::size_t>(m_road) + 1];
		lines.insert(lines.end(), {a.x, a.y, b.x, b.y});
	}
	return lines;
}

void MainWindow::roadsChanged()
{
	if (m_state.map && !validSegment(m_state.map->map(), m_road))
		m_road = -1;
	if (m_overview)
		m_overview->setToolLines(toolLines());
	applyOverlay();
}

void MainWindow::roadToolEvent(const ToolEvent &e)
{
	if (!m_state.map || !roadTool())
		return;
	MapModel::Map &map = m_state.map->map();
	// Ends snap within the tool's reach, at least a cell (the old tool's snap distance).
	const float reach = std::max(e.tolerance, 10.0f);
	switch (e.kind)
	{
	case ToolEvent::Kind::Press:
	{
		m_roadDrag = RoadDrag{};
		float ex = 0.0f, ey = 0.0f;
		const bool onEnd = !bridgeTool() && MapEditorCore::roadEndAt(map, e.x, e.y, reach, ex, ey) >= 0;
		if (e.shift && onEnd)
		{
			// Every end that meets there moves; the release records one step.
			m_roadDrag.active = true;
			m_roadDrag.move = true;
			m_roadDrag.x1 = ex;
			m_roadDrag.y1 = ey;
			for (std::int32_t i : MapEditorCore::roadPointsAt(map, ex, ey))
			{
				m_roadDrag.points.push_back(i);
				m_roadDrag.before.push_back((*map.objects)[static_cast<std::size_t>(i)]);
			}
			return;
		}
		m_roadDrag.active = true;
		m_roadDrag.x1 = std::round(e.x);
		m_roadDrag.y1 = std::round(e.y);
		float ox = 0.0f, oy = 0.0f;
		if (bridgeTool())
		{
		}
		else if (onEnd)
		{
			m_roadDrag.x1 = ex;
			m_roadDrag.y1 = ey;
		}
		else if (!e.ctrl)
		{
			const std::int32_t segment = MapEditorCore::roadSegmentAt(map, e.x, e.y, reach, ox, oy);
			if (segment >= 0)
			{
				m_roadDrag.split = segment;
				m_roadDrag.x1 = std::round(ox);
				m_roadDrag.y1 = std::round(oy);
			}
		}
		m_roadDrag.pressX = e.x;
		m_roadDrag.pressY = e.y;
		m_roadDrag.x2 = m_roadDrag.x1;
		m_roadDrag.y2 = m_roadDrag.y1;
		roadsChanged();
		return;
	}
	case ToolEvent::Kind::Move:
	{
		if (!m_roadDrag.active)
			return;
		if (m_roadDrag.move)
		{
			for (std::int32_t i : m_roadDrag.points)
			{
				MapModel::MapObject &o = (*map.objects)[static_cast<std::size_t>(i)];
				o.x = std::round(e.x);
				o.y = std::round(e.y);
			}
			if (m_overview)
				m_overview->update();
			roadsChanged();
			return;
		}
		// The end snaps to a road end, else (not with Ctrl) onto a road, which then gets a junction.
		float ex = 0.0f, ey = 0.0f;
		m_roadDrag.splitEnd = -1;
		m_roadDrag.x2 = std::round(e.x);
		m_roadDrag.y2 = std::round(e.y);
		if (!bridgeTool() && MapEditorCore::roadEndAt(map, e.x, e.y, reach, ex, ey) >= 0)
		{
			m_roadDrag.x2 = ex;
			m_roadDrag.y2 = ey;
		}
		else if (!bridgeTool() && !e.ctrl)
		{
			const std::int32_t segment = MapEditorCore::roadSegmentAt(map, e.x, e.y, reach, ex, ey);
			if (segment >= 0 && segment != m_roadDrag.split)
			{
				m_roadDrag.splitEnd = segment;
				m_roadDrag.x2 = std::round(ex);
				m_roadDrag.y2 = std::round(ey);
			}
		}
		roadsChanged();
		return;
	}
	case ToolEvent::Kind::Release:
	{
		if (!m_roadDrag.active)
			return;
		RoadDrag drag = m_roadDrag;
		m_roadDrag = RoadDrag{};
		if (drag.move)
		{
			std::vector<MapEditorCore::EditObjects::Edit> edits;
			for (std::size_t n = 0; n < drag.points.size(); ++n)
			{
				const MapModel::MapObject &now = (*map.objects)[static_cast<std::size_t>(drag.points[n])];
				if (now != drag.before[n])
					edits.push_back({drag.points[n], drag.before[n], now});
			}
			if (!edits.empty())
			{
				auto command = std::make_unique<MapEditorCore::EditObjects>("Move road point", std::move(edits), true);
				const MapEditorCore::Changes changes = command->changes();
				m_state.map->history.record(std::move(command));
				applyChanges(changes);
			}
			roadsChanged();
			return;
		}
		if (std::hypot(drag.x2 - drag.x1, drag.y2 - drag.y1) < reach)
		{
			// A click: the segment under it becomes the selected one.
			float ox = 0.0f, oy = 0.0f;
			m_road = MapEditorCore::roadSegmentAt(map, drag.pressX, drag.pressY, reach, ox, oy, MapEditorCore::SegmentKind::Both);
			roadsChanged();
			fillSelectionPanel();
			return;
		}
		if (bridgeTool())
			runCommand(MapEditorCore::addBridge(map, roadToolType(), drag.x1, drag.y1, drag.x2, drag.y2));
		else
			runCommand(MapEditorCore::addRoad(map, roadToolType(), drag.x1, drag.y1, drag.x2, drag.y2,
				m_roadCorner | (m_roadJoin ? MapEditorCore::ROAD_JOIN : 0), drag.split, drag.splitEnd));
		m_road = map.objects ? static_cast<int>(map.objects->size()) - 2 : -1;
		roadsChanged();
		fillSelectionPanel();
		return;
	}
	case ToolEvent::Kind::DoubleClick:
		return;
	}
}

bool MainWindow::deleteRoadSelection()
{
	if (!m_state.map || !roadTool() || !validSegment(m_state.map->map(), m_road))
		return false;
	auto command = MapEditorCore::deleteRoad(m_state.map->map(), m_road);
	m_road = -1;
	runCommand(std::move(command));
	roadsChanged();
	fillSelectionPanel();
	return true;
}

// ------------------------------------------------------------------------------------------------
// Side panel: the selected segment, else the corners of new roads

void MainWindow::addRoadEditors(QVBoxLayout *box)
{
	const MapModel::Map &map = m_state.map->map();
	const bool selected = validSegment(map, m_road);
	const QStringList corners = {T("Curved"), T("Angled"), T("Tight curve")};
	if (!selected && bridgeTool())
	{
		box->addWidget(label(T("NEW BRIDGES"), "Section"));
		box->addWidget(label(T("Drag from one bank to the other; the terrain places the bridge model between the ends. A click selects a bridge."), "Hint"));
		return;
	}
	if (!selected)
	{
		box->addWidget(label(T("NEW ROADS"), "Section"));
		auto *row = new QHBoxLayout;
		row->addWidget(label(T("Corners"), "Meta"));
		auto *corner = new QComboBox;
		corner->setObjectName(QStringLiteral("RoadCornerNew"));
		corner->addItems(corners);
		corner->setCurrentIndex(cornerIndex(m_roadCorner));
		connect(corner, &QComboBox::currentIndexChanged, this, [this](int i) { m_roadCorner = cornerFlags(i); });
		row->addWidget(corner, 1);
		box->addLayout(row);
		auto *join = new QCheckBox(T("Join to a road of another type"));
		join->setChecked(m_roadJoin);
		connect(join, &QCheckBox::toggled, this, [this](bool on) { m_roadJoin = on; });
		box->addWidget(join);
		box->addWidget(label(T("Drag to draw a segment; its ends snap to other roads. A click selects a segment, Shift+drag moves a road end."), "Hint"));
		return;
	}
	const MapModel::MapObject &a = (*map.objects)[static_cast<std::size_t>(m_road)];
	const MapModel::MapObject &b = (*map.objects)[static_cast<std::size_t>(m_road) + 1];
	const int first = m_road;
	const bool bridge = MapEditorCore::isBridgeSegment(map, first);
	box->addWidget(label(bridge ? T("BRIDGE") : T("ROAD"), "Section"));
	auto *type = new QComboBox;
	type->setObjectName(QStringLiteral("RoadType"));
	for (const MeRoadInfo &road : EngineClient::instance().roads())
	{
		if ((road.bridge != 0) == bridge)
			type->addItem(QString::fromUtf8(road.name));
	}
	if (type->findText(utf8(a.templateName)) < 0)
		type->addItem(utf8(a.templateName));	// a type the data does not list (kept as it is)
	type->setCurrentText(utf8(a.templateName));
	auto *corner = new QComboBox;
	corner->setObjectName(QStringLiteral("RoadCorner"));
	corner->addItems(corners);
	corner->setCurrentIndex(cornerIndex(a.flags));
	auto *join = new QCheckBox(T("Join to a road of another type"));
	join->setObjectName(QStringLiteral("RoadJoin"));
	join->setChecked((a.flags & MapEditorCore::ROAD_JOIN) != 0);
	const auto apply = [this, first, type, corner, join] {
		const std::int32_t style = cornerFlags(corner->currentIndex()) | (join->isChecked() ? MapEditorCore::ROAD_JOIN : 0);
		runCommand(MapEditorCore::changeRoad(m_state.map->map(), first, type->currentText().toUtf8().toStdString(), style));
	};
	connect(type, &QComboBox::activated, this, apply);
	connect(corner, &QComboBox::activated, this, apply);
	connect(join, &QCheckBox::clicked, this, apply);
	auto *typeRow = new QHBoxLayout;
	typeRow->addWidget(label(T("Type"), "Meta"));
	typeRow->addWidget(type, 1);
	box->addLayout(typeRow);
	if (!bridge)
	{
		auto *cornerRow = new QHBoxLayout;
		cornerRow->addWidget(label(T("Corners"), "Meta"));
		cornerRow->addWidget(corner, 1);
		box->addLayout(cornerRow);
		box->addWidget(join);
	}
	else
	{
		corner->deleteLater();
		join->deleteLater();
	}
	// Ends where three or four segments meet: the terrain draws a junction there.
	int junctions = 0;
	for (const MapModel::MapObject *end : {&a, &b})
		junctions += MapEditorCore::roadPointsAt(map, end->x, end->y).size() >= 3 ? 1 : 0;
	box->addWidget(label(T("Length %1").arg(std::hypot(b.x - a.x, b.y - a.y), 0, 'f', 0)
		+ (junctions > 0 ? QStringLiteral(", ") + T("%1 junctions").arg(junctions) : QString()), "Hint"));
	auto *remove = new QPushButton(bridge ? T("Delete bridge") : T("Delete road"));
	remove->setObjectName(QStringLiteral("RoadDelete"));
	connect(remove, &QPushButton::clicked, this, [this] { deleteRoadSelection(); });
	box->addWidget(remove, 0, Qt::AlignRight);
}

} // namespace MapEditorApp
