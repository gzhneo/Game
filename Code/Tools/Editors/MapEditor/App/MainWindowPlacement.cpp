/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowPlacement.cpp -- groves, fences and scorch marks, after the old WorldBuilder's GroveTool,
// FenceTool and ScorchTool. A click grows a grove, a drag scatters trees in the rectangle; a drag
// sets a fence along the line; a click sets a scorch mark or picks the one there. Each is one step.
// The tool event dispatch of the views and the self test of these tools live here too.

#include "App/Common.h"
#include "App/MainWindow.h"
#include "App/MainWindowParts.h"

#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/ToolEvent.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/ObjectCommands.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>
#include <cstring>

namespace MapEditorApp
{

namespace
{

constexpr int GROVE_TYPES = 5;

} // namespace

// Tree templates of the game (editor sorting "shrubbery", as the old grove dialog lists them).
QStringList treeTemplates()
{
	QStringList names;
	for (const MeTemplateInfo &info : EngineClient::instance().catalog())
	{
		if (info.sorting == 4)
			names << QString::fromUtf8(info.name);
	}
	names.sort(Qt::CaseInsensitive);
	return names;
}

bool MainWindow::groveTool() const
{
	return m_placeTemplate == QLatin1String(GROVE_TOOL);
}

bool MainWindow::fenceTool() const
{
	return m_placeTemplate.startsWith(QLatin1String(FENCE_TOOL_PREFIX));
}

bool MainWindow::scorchTool() const
{
	return m_placeTemplate == QLatin1String(SCORCH_TOOL);
}

bool MainWindow::placementTool() const
{
	return m_state.map && (groveTool() || fenceTool() || scorchTool() || borderTool() || rulerTool() || buildListTool());
}

void MainWindow::toolEvent(const ToolEvent &e)
{
	if (terrainTool())
		terrainToolEvent(e);
	else if (roadTool())
		roadToolEvent(e);
	else if (placementTool())
		placementToolEvent(e);
	else
		areaToolEvent(e);
}

void MainWindow::insertObjects(std::vector<MapModel::MapObject> objects, const char *name)
{
	if (objects.empty() || !m_state.map)
		return;
	MapEditorCore::Clipboard clipboard;
	// New objects go into the active layer.
	for (MapModel::MapObject &o : objects)
	{
		if (!m_activeLayer.empty())
			o.properties.set("objectLayer", MapModel::PropertyValue(m_activeLayer));
	}
	clipboard.objects = std::move(objects);
	// A paste gives every object a unique ID of the map.
	auto command = MapEditorCore::pasteObjects(m_state.map->map(), clipboard, 0.0f, 0.0f, name);
	MapEditorCore::InsertObjects *inserted = command.get();
	m_state.map->history.execute(std::move(command), m_state.map->map());
	applyChanges(inserted->changes());
}

std::vector<float> MainWindow::placementLines() const
{
	if (borderTool())
		return boundaryLines();
	if (rulerTool())
		return rulerLines();
	if (buildListTool())
		return buildListLines();
	std::vector<float> lines;
	if (!m_shapeDrag.active)
		return lines;
	const ShapeDrag &d = m_shapeDrag;
	if (groveTool())
	{
		// The rectangle the trees are scattered in.
		lines = {d.x1, d.y1, d.x2, d.y1, d.x2, d.y1, d.x2, d.y2, d.x2, d.y2, d.x1, d.y2, d.x1, d.y2, d.x1, d.y1};
	}
	else if (fenceTool())
	{
		lines = {d.x1, d.y1, d.x2, d.y2};
		// A short cross line at every piece.
		const std::string name = m_placeTemplate.mid(static_cast<qsizetype>(std::strlen(FENCE_TOOL_PREFIX))).toStdString();
		for (const MapModel::MapObject &piece : MapEditorCore::fencePieces(name, "team", d.x1, d.y1, d.x2, d.y2, m_fenceSpacing, m_fenceOffset, d.fenceCount))
		{
			const float nx = -std::sin(piece.angle) * 4.0f;
			const float ny = std::cos(piece.angle) * 4.0f;
			lines.insert(lines.end(), {piece.x - nx, piece.y - ny, piece.x + nx, piece.y + ny});
		}
	}
	return lines;
}

void MainWindow::placementToolEvent(const ToolEvent &e)
{
	if (borderTool())
	{
		boundaryToolEvent(e);
		return;
	}
	if (rulerTool())
	{
		rulerToolEvent(e);
		return;
	}
	if (buildListTool())
	{
		buildListToolEvent(e);
		return;
	}
	MapModel::Map &map = m_state.map->map();
	const auto refresh = [this] {
		if (m_overview)
			m_overview->setToolLines(toolLines());
		applyOverlay();
	};
	if (scorchTool())
	{
		if (e.kind != ToolEvent::Kind::Press)
			return;
		const int existing = MapEditorCore::scorchAt(map, e.x, e.y);
		if (existing >= 0)
		{
			m_state.map->selection.set(existing);
			selectionChanged();
			return;
		}
		insertObjects({MapEditorCore::scorchMark(e.x, e.y, m_scorchType, m_scorchRadius)}, "Place scorch mark");
		return;
	}
	switch (e.kind)
	{
	case ToolEvent::Kind::Press:
		m_shapeDrag = ShapeDrag{true, e.x, e.y, e.x, e.y};
		refresh();
		return;
	case ToolEvent::Kind::Move:
		if (!m_shapeDrag.active)
			return;
		// Shift holds the number of fence pieces reached so far and stretches their spacing (old FenceTool).
		if (fenceTool() && e.shift && m_shapeDrag.fenceCount == 0)
			m_shapeDrag.fenceCount = static_cast<int>(MapEditorCore::fencePieces("x", "", m_shapeDrag.x1, m_shapeDrag.y1, m_shapeDrag.x2,
				m_shapeDrag.y2, m_fenceSpacing, 0.0f).size());
		else if (!e.shift)
			m_shapeDrag.fenceCount = 0;
		m_shapeDrag.x2 = e.x;
		m_shapeDrag.y2 = e.y;
		refresh();
		return;
	case ToolEvent::Kind::Release:
	{
		if (!m_shapeDrag.active)
			return;
		const ShapeDrag d = m_shapeDrag;
		m_shapeDrag = ShapeDrag{};
		refresh();
		const bool dragged = std::hypot(e.x - d.x1, e.y - d.y1) > std::max(e.tolerance, CELL);
		if (groveTool())
		{
			if (m_grove.types.empty())
			{
				toast(T("Choose at least one tree for the grove."));
				return;
			}
			std::vector<MapModel::MapObject> trees = dragged ? MapEditorCore::scatterGrove(map, d.x1, d.y1, e.x, e.y, m_grove, m_placeRandom)
				: MapEditorCore::growGrove(map, d.x1, d.y1, m_grove, m_placeRandom);
			if (trees.empty())
				toast(T("No place for trees here (water or slopes)."));
			insertObjects(std::move(trees), dragged ? "Plant trees" : "Plant grove");
			return;
		}
		if (fenceTool())
		{
			const std::string name = m_placeTemplate.mid(static_cast<qsizetype>(std::strlen(FENCE_TOOL_PREFIX))).toUtf8().toStdString();
			insertObjects(MapEditorCore::fencePieces(name, m_placeTeam.toUtf8().toStdString(), d.x1, d.y1, e.x, e.y, m_fenceSpacing, m_fenceOffset,
				e.shift ? d.fenceCount : 0), "Place fence");
		}
		return;
	}
	case ToolEvent::Kind::DoubleClick:
		return;
	}
}

void MainWindow::addPlacementEditors(QVBoxLayout *box)
{
	if (borderTool())
	{
		addBoundaryEditors(box);
		return;
	}
	if (rulerTool())
	{
		addRulerEditors(box);
		return;
	}
	if (buildListTool())
	{
		addBuildListEditors(box);
		return;
	}
	if (scorchTool())
	{
		box->addWidget(label(T("NEW SCORCH MARKS"), "Section"));
		auto *grid = new QGridLayout;
		grid->addWidget(label(T("Kind"), "Meta"), 0, 0);
		auto *kind = new QComboBox;
		kind->setObjectName(QStringLiteral("ScorchType"));
		for (int i = 0; i < MapEditorCore::SCORCH_TYPES - 1; ++i)
			kind->addItem(T("Scorch %1").arg(i + 1));
		kind->addItem(T("Shadow"));
		kind->setCurrentIndex(m_scorchType);
		connect(kind, &QComboBox::currentIndexChanged, this, [this](int i) { m_scorchType = std::max(i, 0); });
		grid->addWidget(kind, 0, 1);
		grid->addWidget(label(T("Radius"), "Meta"), 1, 0);
		auto *radius = new QSpinBox;
		radius->setObjectName(QStringLiteral("ScorchRadius"));
		radius->setRange(2, 400);
		radius->setValue(static_cast<int>(std::lround(m_scorchRadius)));
		connect(radius, &QSpinBox::valueChanged, this, [this](int v) { m_scorchRadius = static_cast<float>(v); });
		grid->addWidget(radius, 1, 1);
		box->addLayout(grid);
		auto *hint = label(T("A click sets a scorch mark; a click on one picks it, so its kind and radius can be changed on the right."), "Hint");
		hint->setWordWrap(true);
		box->addWidget(hint);
		return;
	}
	if (fenceTool())
	{
		box->addWidget(label(T("NEW FENCE"), "Section"));
		box->addWidget(label(m_placeTemplate.mid(static_cast<qsizetype>(std::strlen(FENCE_TOOL_PREFIX))), "Secondary"));
		auto *grid = new QGridLayout;
		const auto number = [&](int row, const QString &text, const char *name, double value, double minimum) {
			grid->addWidget(label(text, "Meta"), row, 0);
			auto *field = new QDoubleSpinBox;
			field->setObjectName(QString::fromLatin1(name));
			field->setRange(minimum, 1000.0);
			field->setDecimals(1);
			field->setValue(value);
			grid->addWidget(field, row, 1);
			return field;
		};
		auto *spacing = number(0, T("Spacing"), "FenceSpacing", static_cast<double>(m_fenceSpacing), 1.0);
		connect(spacing, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_fenceSpacing = static_cast<float>(v); });
		auto *offset = number(1, T("Offset"), "FenceOffset", static_cast<double>(m_fenceOffset), -1000.0);
		connect(offset, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_fenceOffset = static_cast<float>(v); });
		box->addLayout(grid);
		auto *hint = label(T("Drag from one end of the fence to the other: a piece every spacing, facing along the line. The spacing starts at the template's fence width. Hold Shift to keep the number of pieces and stretch their spacing."), "Hint");
		hint->setWordWrap(true);
		box->addWidget(hint);
		return;
	}
	// Grove: up to five kinds of tree with how often each is chosen.
	box->addWidget(label(T("GROVE"), "Section"));
	const QStringList trees = treeTemplates();
	if (m_grove.types.empty() && !trees.isEmpty())
		m_grove.types.push_back({trees.front().toStdString(), 100});
	auto *grid = new QGridLayout;
	grid->setHorizontalSpacing(6);
	for (int row = 0; row < GROVE_TYPES; ++row)
	{
		auto *type = new QComboBox;
		type->setObjectName(QStringLiteral("GroveType%1").arg(row));
		type->addItem(T("(none)"), QString());
		for (const QString &name : trees)
			type->addItem(name, name);
		auto *weight = new QSpinBox;
		weight->setObjectName(QStringLiteral("GroveWeight%1").arg(row));
		weight->setRange(0, 100);
		weight->setSuffix(QStringLiteral(" %"));
		if (row < static_cast<int>(m_grove.types.size()))
		{
			type->setCurrentIndex(std::max(0, type->findData(QString::fromStdString(m_grove.types[static_cast<std::size_t>(row)].first))));
			weight->setValue(m_grove.types[static_cast<std::size_t>(row)].second);
		}
		// The rows are read back into the options on every change; empty rows drop out.
		const auto update = [this, grid] {
			std::vector<std::pair<std::string, int>> types;
			for (int r = 0; r < GROVE_TYPES; ++r)
			{
				auto *t = qobject_cast<QComboBox *>(grid->itemAtPosition(r, 0)->widget());
				auto *w = qobject_cast<QSpinBox *>(grid->itemAtPosition(r, 1)->widget());
				const QString name = t ? t->currentData().toString() : QString();
				if (!name.isEmpty() && w && w->value() > 0)
					types.push_back({name.toStdString(), w->value()});
			}
			m_grove.types = std::move(types);
		};
		connect(type, &QComboBox::currentIndexChanged, this, update);
		connect(weight, &QSpinBox::valueChanged, this, update);
		grid->addWidget(type, row, 0);
		grid->addWidget(weight, row, 1);
	}
	grid->setColumnStretch(0, 1);
	box->addLayout(grid);
	auto *countRow = new QHBoxLayout;
	countRow->addWidget(label(T("Trees in a rectangle"), "Meta"));
	auto *count = new QSpinBox;
	count->setObjectName(QStringLiteral("GroveCount"));
	count->setRange(1, 500);
	count->setValue(m_grove.boxCount);
	connect(count, &QSpinBox::valueChanged, this, [this](int v) { m_grove.boxCount = v; });
	countRow->addWidget(count);
	box->addLayout(countRow);
	auto *water = new QCheckBox(T("Also in water"));
	water->setChecked(m_grove.allowInWater);
	connect(water, &QCheckBox::toggled, this, [this](bool on) { m_grove.allowInWater = on; });
	box->addWidget(water);
	auto *slopes = new QCheckBox(T("Also on steep slopes"));
	slopes->setChecked(m_grove.allowOnSlopes);
	connect(slopes, &QCheckBox::toggled, this, [this](bool on) { m_grove.allowOnSlopes = on; });
	box->addWidget(slopes);
	auto *hint = label(T("A click grows a grove outwards from that point, avoiding water and slopes; a drag scatters the number of trees above in the rectangle."), "Hint");
	hint->setWordWrap(true);
	box->addWidget(hint);
	if (trees.isEmpty())
		box->addWidget(label(T("The game data has no trees (editor sorting shrubbery)."), "Hint"));
}

} // namespace MapEditorApp
