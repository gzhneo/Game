/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowShapes.cpp -- ramps and mesh molds in the terrain mode.

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
#include <QGridLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

namespace MapEditorApp
{

namespace
{

// Name of a built-in mold in the editor's language.
QString moldTitle(const std::string &name)
{
	if (name == "Hill")
		return T("Hill");
	if (name == "Crater")
		return T("Crater");
	if (name == "Plateau")
		return T("Plateau");
	if (name == "Ridge")
		return T("Ridge");
	if (name == "Trench")
		return T("Trench");
	return QString::fromStdString(name);
}

} // namespace

void MainWindow::addShapePanel(QVBoxLayout *col)
{
	auto *tools = segmented({T("Ramp"), T("Mesh mold")}, static_cast<int>(m_shapeTool), [this](int i) {
		const ShapeTool tool = static_cast<ShapeTool>(i);
		if (tool == m_shapeTool)
			return;
		finishMold(false);
		m_shapeTool = tool;
		QTimer::singleShot(0, this, [this] { rebuild(); });
	}, nullptr, {true, true});
	tools->setObjectName(QStringLiteral("ShapeTools"));
	col->addWidget(tools);
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
	if (m_shapeTool == ShapeTool::Ramp)
	{
		col->addWidget(label(T("RAMP"), "Section"));
		auto *width = spin(T("Width"), "RampWidth", 1, 40, static_cast<int>(std::lround(m_rampWidth)), QLatin1Char(' ') + T("cells"));
		connect(width, &QSpinBox::valueChanged, this, [this](int v) {
			m_rampWidth = static_cast<float>(v);
			applyOverlay();
		});
		col->addLayout(grid);
		auto *hint = label(T("Drag from one end of the ramp to the other: the ground between them becomes an even slope from the height at the start to the height at the end."), "Hint");
		hint->setWordWrap(true);
		col->addWidget(hint);
	}
	else
	{
		col->addWidget(label(T("MESH MOLD"), "Section"));
		refreshMolds();
		auto *list = new QListWidget;
		list->setObjectName(QStringLiteral("MoldList"));
		for (std::size_t i = 0; i < m_molds.size(); ++i)
		{
			const QString name = QString::fromStdString(m_molds[i].name);
			auto *item = new QListWidgetItem(static_cast<int>(i) < m_builtInMolds ? moldTitle(m_molds[i].name) : name, list);
			item->setToolTip(static_cast<int>(i) < m_builtInMolds ? T("Built-in shape") : T("Mold file %1").arg(name));
		}
		list->setCurrentRow(std::clamp(m_moldIndex, 0, std::max(0, static_cast<int>(m_molds.size()) - 1)));
		connect(list, &QListWidget::currentRowChanged, this, [this](int i) {
			if (i < 0)
				return;
			m_moldIndex = i;
			placeMold();
		});
		col->addWidget(list, 1);
		const auto changed = [this] { placeMold(); };
		auto *scale = spin(T("Scale"), "MoldScale", 10, 1000, static_cast<int>(std::lround(m_moldPlace.scale * 100.0f)), QStringLiteral(" %"));
		connect(scale, &QSpinBox::valueChanged, this, [this, changed](int v) {
			m_moldPlace.scale = static_cast<float>(v) / 100.0f;
			changed();
		});
		auto *angle = spin(T("Angle"), "MoldAngle", -360, 360, static_cast<int>(std::lround(m_moldPlace.angle)), QStringLiteral(" \u00b0"));
		connect(angle, &QSpinBox::valueChanged, this, [this, changed](int v) {
			m_moldPlace.angle = static_cast<float>(v);
			changed();
		});
		auto *height = spin(T("Height"), "MoldHeight", 0, 255, static_cast<int>(std::lround(m_moldPlace.height / HEIGHT_SCALE)), QString());
		connect(height, &QSpinBox::valueChanged, this, [this, changed](int v) {
			m_moldPlace.height = static_cast<float>(v) * HEIGHT_SCALE;
			changed();
		});
		m_moldHeightField = height;
		col->addLayout(grid);
		auto *modes = segmented({T("Raise and lower"), T("Raise only"), T("Lower only")}, static_cast<int>(m_moldPlace.mode), [this](int i) {
			m_moldPlace.mode = static_cast<MapEditorCore::MoldMode>(i);
			placeMold();
		}, nullptr, {true, true, true});
		modes->setObjectName(QStringLiteral("MoldModes"));
		col->addWidget(modes);
		auto *buttons = new QHBoxLayout;
		auto *apply = new QPushButton(T("Apply"));
		apply->setObjectName(QStringLiteral("MoldApply"));
		connect(apply, &QPushButton::clicked, this, [this] { finishMold(true); });
		auto *discard = new QPushButton(T("Discard"));
		connect(discard, &QPushButton::clicked, this, [this] { finishMold(false); });
		buttons->addWidget(apply);
		buttons->addWidget(discard);
		col->addLayout(buttons);
		auto *hint = label(T("Click to put the mold on the ground, drag to move it. It shows at once; Enter applies it as one step, Esc discards it. Alt+click takes the height under the cursor."), "Hint");
		hint->setWordWrap(true);
		col->addWidget(hint);
	}
	auto *info = label(QString(), "Hint");
	m_brushInfo = info;
	col->addWidget(info);
	if (m_shapeTool == ShapeTool::Ramp)
		col->addStretch(1);
}

void MainWindow::refreshMolds()
{
	m_molds = MapEditorCore::builtInMolds();
	m_builtInMolds = static_cast<int>(m_molds.size());
	EngineClient &engine = EngineClient::instance();
	for (const QString &name : engine.molds())
	{
		auto found = m_moldFiles.find(name);
		if (found == m_moldFiles.end())
		{
			MapEditorCore::Mold mold;
			mold.name = name.toStdString();
			if (!engine.moldSample(name, mold.x0, mold.y0, mold.step, mold.cols, mold.rows, mold.z))
				continue;
			found = m_moldFiles.emplace(name, std::move(mold)).first;
		}
		m_molds.push_back(found->second);
	}
}

std::vector<float> MainWindow::terrainLines() const
{
	std::vector<float> lines;
	if (!terrainTool())
		return lines;
	if (m_terrainTool == TerrainTool::Texture && m_textureMode == TextureMode::Edge && m_shapeDrag.active)
		return {m_shapeDrag.x1, m_shapeDrag.y1, m_shapeDrag.x2, m_shapeDrag.y2};
	if (m_terrainTool != TerrainTool::Shape)
		return lines;
	// Outline of a rectangle: corners a, b, c, d.
	const auto box = [&lines](const float (&x)[4], const float (&y)[4]) {
		for (int i = 0; i < 4; ++i)
			lines.insert(lines.end(), {x[i], y[i], x[(i + 1) % 4], y[(i + 1) % 4]});
	};
	if (m_shapeTool == ShapeTool::Ramp && m_shapeDrag.active)
	{
		const float dx = m_shapeDrag.x2 - m_shapeDrag.x1;
		const float dy = m_shapeDrag.y2 - m_shapeDrag.y1;
		const float length = std::hypot(dx, dy);
		if (length < 1.0f)
			return lines;
		const float nx = -dy / length * m_rampWidth * CELL * 0.5f;
		const float ny = dx / length * m_rampWidth * CELL * 0.5f;
		box({m_shapeDrag.x1 + nx, m_shapeDrag.x2 + nx, m_shapeDrag.x2 - nx, m_shapeDrag.x1 - nx},
			{m_shapeDrag.y1 + ny, m_shapeDrag.y2 + ny, m_shapeDrag.y2 - ny, m_shapeDrag.y1 - ny});
		lines.insert(lines.end(), {m_shapeDrag.x1, m_shapeDrag.y1, m_shapeDrag.x2, m_shapeDrag.y2});
	}
	if (m_shapeTool == ShapeTool::Mold && m_mold.active && m_moldIndex < static_cast<int>(m_molds.size()))
	{
		const MapEditorCore::Mold &mold = m_molds[static_cast<std::size_t>(m_moldIndex)];
		const float s = m_moldPlace.scale;
		const float a = m_moldPlace.angle * 3.14159265f / 180.0f;
		const float x0 = mold.x0 * s, y0 = mold.y0 * s;
		const float x1 = (mold.x0 + static_cast<float>(mold.cols - 1) * mold.step) * s;
		const float y1 = (mold.y0 + static_cast<float>(mold.rows - 1) * mold.step) * s;
		const float cx[4] = {x0, x1, x1, x0};
		const float cy[4] = {y0, y0, y1, y1};
		float wx[4], wy[4];
		for (int i = 0; i < 4; ++i)
		{
			wx[i] = m_moldPlace.x + cx[i] * std::cos(a) - cy[i] * std::sin(a);
			wy[i] = m_moldPlace.y + cx[i] * std::sin(a) + cy[i] * std::cos(a);
		}
		box(wx, wy);
	}
	return lines;
}

void MainWindow::placeMold()
{
	if (!m_mold.active || !m_state.map || !m_state.map->map().heightMap || m_moldIndex < 0 || m_moldIndex >= static_cast<int>(m_molds.size()))
		return;
	MapModel::HeightMap &hm = *m_state.map->map().heightMap;
	if (m_mold.mapSerial != m_mapSerial || m_mold.before.size() != hm.heights.size())
	{
		m_mold = MoldState{};
		return;
	}
	// Back to the heights from before where the mold was, then the mold where it is now.
	const MapEditorCore::CellRect old = m_mold.rect;
	for (int y = old.y0; y <= old.y1 && !old.empty(); ++y)
	{
		const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(hm.width);
		std::copy(m_mold.before.begin() + static_cast<std::ptrdiff_t>(row + static_cast<std::size_t>(old.x0)),
			m_mold.before.begin() + static_cast<std::ptrdiff_t>(row + static_cast<std::size_t>(old.x1) + 1),
			hm.heights.begin() + static_cast<std::ptrdiff_t>(row + static_cast<std::size_t>(old.x0)));
	}
	m_mold.rect = MapEditorCore::applyMold(hm, m_mold.before, m_molds[static_cast<std::size_t>(m_moldIndex)], m_moldPlace);
	MapEditorCore::CellRect update = old;
	update.unite(m_mold.rect);
	m_mold.touched.unite(m_mold.rect);
	if (!update.empty() && m_viewport->hasDevice() && m_viewMapSerial == m_mapSerial)
		EngineClient::instance().setHeights(update.x0, update.y0, update.x1, update.y1, hm.heights, hm.width);
	if (m_overview)
	{
		m_overview->refreshImage();
		m_overview->setToolLines(toolLines());
	}
	applyOverlay();
}

void MainWindow::finishMold(bool apply)
{
	if (!m_mold.active)
		return;
	MoldState mold = std::move(m_mold);
	m_mold = MoldState{};
	if (!m_state.map || !m_state.map->map().heightMap || mold.mapSerial != m_mapSerial)
		return;
	MapModel::Map &map = m_state.map->map();
	MapModel::HeightMap &hm = *map.heightMap;
	if (apply)
	{
		auto command = MapEditorCore::heightStroke(map, mold.before, mold.touched, "Mesh mold");
		if (command)
		{
			const MapEditorCore::Changes changes = command->changes();
			m_state.map->history.record(std::move(command));
			applyChanges(changes);
		}
	}
	else if (!mold.touched.empty() && mold.before.size() == hm.heights.size())
	{
		hm.heights = mold.before;
		if (m_viewport->hasDevice() && m_viewMapSerial == m_mapSerial)
			EngineClient::instance().setHeights(mold.touched.x0, mold.touched.y0, mold.touched.x1, mold.touched.y1, hm.heights, hm.width);
		if (m_overview)
			m_overview->refreshImage();
	}
	if (m_overview)
		m_overview->setToolLines(toolLines());
	applyOverlay();
}

void MainWindow::toolKey(int key)
{
	if (terrainTool() && m_terrainTool == TerrainTool::Shape && m_shapeTool == ShapeTool::Mold)
	{
		finishMold(key != Qt::Key_Backspace);
		return;
	}
	areaToolKey(key);
}

void MainWindow::shapeToolEvent(const ToolEvent &e)
{
	MapModel::Map &map = m_state.map->map();
	MapModel::HeightMap &hm = *map.heightMap;
	if (m_shapeTool == ShapeTool::Ramp)
	{
		switch (e.kind)
		{
		case ToolEvent::Kind::Press:
			m_shapeDrag = ShapeDrag{true, e.x, e.y, e.x, e.y};
			break;
		case ToolEvent::Kind::Move:
			if (m_shapeDrag.active)
			{
				m_shapeDrag.x2 = e.x;
				m_shapeDrag.y2 = e.y;
			}
			brushMoved(true, e.x, e.y);
			return;
		case ToolEvent::Kind::Release:
		{
			if (!m_shapeDrag.active)
				return;
			m_shapeDrag.active = false;
			const float z1 = MapEditorCore::groundHeight(map, m_shapeDrag.x1, m_shapeDrag.y1);
			const float z2 = MapEditorCore::groundHeight(map, e.x, e.y);
			const std::vector<std::uint8_t> before = hm.heights;
			const MapEditorCore::CellRect changed = MapEditorCore::applyRamp(hm, m_shapeDrag.x1, m_shapeDrag.y1, z1, e.x, e.y, z2, m_rampWidth * CELL);
			auto command = changed.empty() ? nullptr : MapEditorCore::heightStroke(map, before, changed, "Ramp");
			if (command)
			{
				const MapEditorCore::Changes changes = command->changes();
				m_state.map->history.record(std::move(command));
				applyChanges(changes);
			}
			else
				toast(T("Drag from one end of the ramp to the other."));
			break;
		}
		case ToolEvent::Kind::DoubleClick:
			return;
		}
		if (m_overview)
			m_overview->setToolLines(toolLines());
		applyOverlay();
		return;
	}

	// Mesh mold.
	switch (e.kind)
	{
	case ToolEvent::Kind::Press:
	{
		if (e.alt)
		{
			m_moldPlace.height = MapEditorCore::groundHeight(map, e.x, e.y);
			if (m_moldHeightField)
				m_moldHeightField->setValue(static_cast<int>(std::lround(m_moldPlace.height / HEIGHT_SCALE)));
			toast(T("Height %1 taken").arg(std::lround(m_moldPlace.height / HEIGHT_SCALE)));
			return;
		}
		if (m_molds.empty() || m_moldIndex >= static_cast<int>(m_molds.size()))
			return;
		const float reach = m_molds[static_cast<std::size_t>(m_moldIndex)].radius() * m_moldPlace.scale;
		if (!m_mold.active)
		{
			m_mold.active = true;
			m_mold.mapSerial = m_mapSerial;
			m_mold.before = hm.heights;
			m_moldPlace.height = MapEditorCore::groundHeight(map, e.x, e.y);
			if (m_moldHeightField)
			{
				const QSignalBlocker block(m_moldHeightField);
				m_moldHeightField->setValue(static_cast<int>(std::lround(m_moldPlace.height / HEIGHT_SCALE)));
			}
		}
		// A press on the mold grabs it; one beside it puts it there.
		if (std::hypot(e.x - m_moldPlace.x, e.y - m_moldPlace.y) > reach || m_mold.touched.empty())
		{
			m_moldPlace.x = e.x;
			m_moldPlace.y = e.y;
		}
		m_mold.offsetX = m_moldPlace.x - e.x;
		m_mold.offsetY = m_moldPlace.y - e.y;
		m_mold.dragging = true;
		placeMold();
		return;
	}
	case ToolEvent::Kind::Move:
		brushMoved(true, e.x, e.y);
		if (!m_mold.active || !m_mold.dragging)
			return;
		m_moldPlace.x = e.x + m_mold.offsetX;
		m_moldPlace.y = e.y + m_mold.offsetY;
		placeMold();
		return;
	case ToolEvent::Kind::Release:
		m_mold.dragging = false;
		return;
	case ToolEvent::Kind::DoubleClick:
		return;
	}
}

} // namespace MapEditorApp
