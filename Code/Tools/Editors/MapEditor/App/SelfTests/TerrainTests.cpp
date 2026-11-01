/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// TerrainTests.cpp -- the self test of MainWindowTerrain.cpp (MapEditor --selftest).

#include "App/Common.h"
#include "App/MainWindow.h"
#include "App/SelfTest.h"
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
#include <QApplication>
#include <QListWidget>
#include <QDir>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <cmath>

namespace MapEditorApp
{

bool MainWindow::selfTestTerrain(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "terrain");
	MapEditorCore::History &history = m_state.map->history;
	const MapModel::HeightMap &hm = *m_state.map->map().heightMap;
	const std::vector<std::uint8_t> original = hm.heights;
	const MapEditorCore::Brush brushBefore = m_brush;

	m_workMode = WorkMode::Terrain;
	m_show3D = false;
	rebuild();
	QApplication::processEvents();
	MapOverview *view = m_overview;
	step(view && terrainTool() && m_brushHeightField, QStringLiteral("the terrain mode has the height brush"));
	if (!view || !terrainTool())
		return false;
	view->fit();
	const auto send = [view](QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods = Qt::NoModifier) {
		QMouseEvent event(type, pos, view->mapToGlobal(pos), button, buttons, mods);
		QApplication::sendEvent(view, &event);
	};
	const auto heightAt = [&hm, view](QPointF pos) {
		const QPointF w = view->widgetToWorld(pos);
		float cx = 0.0f, cy = 0.0f;
		MapEditorCore::worldToCell(hm, static_cast<float>(w.x()), static_cast<float>(w.y()), cx, cy);
		return static_cast<int>(hm.heights[static_cast<std::size_t>(std::lround(cy)) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(std::lround(cx))]);
	};
	const QPointF c(view->width() * 0.5, view->height() * 0.5);
	m_brush.mode = MapEditorCore::BrushMode::Raise;
	m_brush.radius = 4.0f;
	m_brush.strength = 8.0f;
	m_brush.feather = 0.5f;
	const int start = heightAt(c);
	send(QEvent::MouseButtonPress, c, Qt::LeftButton, Qt::LeftButton);
	send(QEvent::MouseButtonRelease, c, Qt::LeftButton, Qt::NoButton);
	step(heightAt(c) == std::min(255, start + 8) && history.undoName() == "Raise terrain",
		QStringLiteral("a click raises the ground (%1 -> %2)").arg(start).arg(heightAt(c)));
	undo();
	step(hm.heights == original && !history.isModified(), QStringLiteral("undo lowers it again"));

	// A stroke along a line is one step; every point on it ends higher than before.
	std::vector<int> lineBefore;
	for (int i = -5; i <= 5; ++i)
		lineBefore.push_back(heightAt(c + QPointF(i * 10.0, 0)));
	send(QEvent::MouseButtonPress, c - QPointF(60, 0), Qt::LeftButton, Qt::LeftButton);
	for (int i = 1; i <= 12; ++i)
		send(QEvent::MouseMove, c - QPointF(60, 0) + QPointF(i * 10.0, 0), Qt::NoButton, Qt::LeftButton);
	send(QEvent::MouseButtonRelease, c + QPointF(60, 0), Qt::LeftButton, Qt::NoButton);
	int raised = 0;
	for (int i = -5; i <= 5; ++i)
		raised += heightAt(c + QPointF(i * 10.0, 0)) > lineBefore[static_cast<std::size_t>(i + 5)] || lineBefore[static_cast<std::size_t>(i + 5)] == 255 ? 1 : 0;
	step(raised >= 9 && history.undoName() == "Raise terrain", QStringLiteral("a stroke raises a ridge (%1 of 11 points higher)").arg(raised));
	const QString shot = QDir(shotDir).filePath(QStringLiteral("terrain_2d.png"));
	step(view->grab().save(shot), QStringLiteral("picture %1").arg(shot));
	undo();

	// Set height, and Alt+click takes a height.
	m_brush.mode = MapEditorCore::BrushMode::Set;
	m_brush.height = 100;
	send(QEvent::MouseButtonPress, c, Qt::LeftButton, Qt::LeftButton);
	send(QEvent::MouseButtonRelease, c, Qt::LeftButton, Qt::NoButton);
	step(heightAt(c) == 100 && history.undoName() == "Set terrain height", QStringLiteral("set height pulls the ground to 100"));
	send(QEvent::MouseButtonPress, c + QPointF(0, 120), Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
	send(QEvent::MouseButtonRelease, c + QPointF(0, 120), Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
	step(m_brush.height == heightAt(c + QPointF(0, 120)) && m_brushHeightField && m_brushHeightField->value() == m_brush.height,
		QStringLiteral("Alt+click takes the height %1").arg(m_brush.height));
	undo();

	// 3D: a click raises the ground the view shows.
	m_show3D = true;
	rebuild();
	QApplication::processEvents();
	if (m_viewport->hasDevice())
	{
		EngineClient &engine = EngineClient::instance();
		const qreal ratio = m_viewport->devicePixelRatioF();
		const QPointF at(m_viewport->width() * 0.5, m_viewport->height() * 0.5);
		float before[3] = {}, after[3] = {};
		const bool picked = engine.pick(static_cast<int>(at.x() * ratio), static_cast<int>(at.y() * ratio), before);
		m_brush.mode = MapEditorCore::BrushMode::Raise;
		m_brush.strength = 16.0f;
		m_brush.radius = 8.0f;
		QMouseEvent press(QEvent::MouseButtonPress, at, m_viewport->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(m_viewport, &press);
		QMouseEvent release(QEvent::MouseButtonRelease, at, m_viewport->mapToGlobal(at), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(m_viewport, &release);
		// The picked point lies on the raised ground now (the camera looks down, so it is nearer).
		const bool pickedAfter = engine.pick(static_cast<int>(at.x() * ratio), static_cast<int>(at.y() * ratio), after);
		step(picked && pickedAfter && after[2] > before[2] + 2.0f * HEIGHT_SCALE && history.undoName() == "Raise terrain",
			QStringLiteral("3D: a click raises the ground (%1 -> %2)").arg(static_cast<double>(before[2]), 0, 'f', 1).arg(static_cast<double>(after[2]), 0, 'f', 1));
	}
	else
		step(false, QStringLiteral("3D: no view"));

	// Texture brush: paint a texture of the map, then one the map lacks (added to its classes).
	m_terrainTool = TerrainTool::Texture;
	m_show3D = false;
	rebuild();
	QApplication::processEvents();
	MapModel::BlendTiles &bt = *m_state.map->map().blendTiles;
	const MapModel::BlendTiles tilesOriginal = bt;
	auto *list = m_overview ? findChild<QListWidget *>(QStringLiteral("TextureList")) : nullptr;
	view = m_overview;
	step(list && list->count() > 0 && view, QStringLiteral("the texture brush lists %1 textures").arg(list ? list->count() : 0));
	if (list && view && !bt.textureClasses.empty())
	{
		view->fit();
		const QPointF at(view->width() * 0.5, view->height() * 0.5);
		const QPointF w = view->widgetToWorld(at);
		float cx = 0.0f, cy = 0.0f;
		MapEditorCore::worldToCell(hm, static_cast<float>(w.x()), static_cast<float>(w.y()), cx, cy);
		const std::size_t cell = static_cast<std::size_t>(std::floor(cy)) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(std::floor(cx));
		const auto choose = [list](const QString &name) {
			for (int i = 0; i < list->count(); ++i)
			{
				if (list->item(i)->data(Qt::UserRole).toString() == name)
				{
					list->setCurrentRow(i);
					return true;
				}
			}
			return false;
		};
		// The overview was built anew for the texture panel.
		MapOverview *textureView = view;
		const auto click = [textureView, at] {
			QMouseEvent press(QEvent::MouseButtonPress, at, textureView->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent(textureView, &press);
			QMouseEvent release(QEvent::MouseButtonRelease, at, textureView->mapToGlobal(at), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
			QApplication::sendEvent(textureView, &release);
		};
		// A texture of the map other than the one under the cursor.
		const auto classAt = [&bt](std::int16_t tile) {
			for (std::size_t i = 0; i < bt.textureClasses.size(); ++i)
				if ((tile >> 2) >= bt.textureClasses[i].firstTile && (tile >> 2) < bt.textureClasses[i].firstTile + bt.textureClasses[i].numTiles)
					return static_cast<int>(i);
			return -1;
		};
		const int under = classAt(bt.tiles[cell]);
		// A copy: undo replaces the map's texture list, a reference into it would dangle.
		const MapModel::TextureClass other = bt.textureClasses[under == 0 && bt.textureClasses.size() > 1 ? 1 : 0];
		const bool chosen = choose(QString::fromStdString(other.name));
		m_brush.radius = 3.0f;
		click();
		step(chosen && classAt(bt.tiles[cell]) == MapEditorCore::textureClassIndex(bt, other.name) && bt.blends[cell] == 0
				&& history.undoName() == "Paint texture",
			QStringLiteral("a click paints %1").arg(QString::fromStdString(other.name)));
		undo();
		step(bt == tilesOriginal, QStringLiteral("undo takes it back"));

		// A texture the map does not have yet.
		QString missing;
		for (int i = 0; i < list->count() && missing.isEmpty(); ++i)
		{
			const QString name = list->item(i)->data(Qt::UserRole).toString();
			if (!name.isEmpty() && MapEditorCore::textureClassIndex(bt, name.toStdString()) < 0)
				missing = name;
		}
		if (!missing.isEmpty())
		{
			EngineClient &engine = EngineClient::instance();
			const qsizetype loads = engine.report().count(QStringLiteral("map: terrain built"));
			choose(missing);
			click();
			const int added = MapEditorCore::textureClassIndex(bt, missing.toStdString());
			step(added == static_cast<int>(tilesOriginal.textureClasses.size()) && classAt(bt.tiles[cell]) == added
					&& (!m_viewport->hasDevice() || engine.report().count(QStringLiteral("map: terrain built")) > loads),
				QStringLiteral("painting %1 adds it to the map's textures").arg(missing));
			const QString textureShot = QDir(shotDir).filePath(QStringLiteral("textures_2d.png"));
			step(view->grab().save(textureShot), QStringLiteral("picture %1").arg(textureShot));
			undo();
			step(bt == tilesOriginal, QStringLiteral("undo removes the texture again"));
		}
		else
			step(true, QStringLiteral("every texture is in the map already, nothing to add"));

		// Soft edges: the painted texture blends into the cells around it.
		const int otherIndex = MapEditorCore::textureClassIndex(bt, other.name);
		const auto towardOther = [&bt, &hm, cell, otherIndex] {
			int count = 0;
			const int x0 = static_cast<int>(cell % static_cast<std::size_t>(hm.width));
			const int y0 = static_cast<int>(cell / static_cast<std::size_t>(hm.width));
			for (int y = std::max(0, y0 - 8); y <= std::min(hm.height - 1, y0 + 8); ++y)
				for (int x = std::max(0, x0 - 8); x <= std::min(hm.width - 1, x0 + 8); ++x)
				{
					const std::int16_t k = bt.blends[static_cast<std::size_t>(y) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(x)];
					if (k > 0 && static_cast<std::size_t>(k) <= bt.blendedTiles.size()
						&& MapEditorCore::classOfTile(bt, bt.blendedTiles[static_cast<std::size_t>(k) - 1].blendIndex) == otherIndex)
						++count;
				}
			return count;
		};
		const int towardBefore = towardOther();
		choose(QString::fromStdString(other.name));
		m_softEdges = true;
		click();
		const int soft = towardOther();
		step(soft > towardBefore && history.undoName() == "Paint texture",
			QStringLiteral("soft edges: %1 cells around blend into %2").arg(soft - towardBefore).arg(QString::fromStdString(other.name)));
		const QString softShot = QDir(shotDir).filePath(QStringLiteral("textures_soft_2d.png"));
		step(textureView->grab().save(softShot), QStringLiteral("picture %1").arg(softShot));
		undo();
		step(bt == tilesOriginal, QStringLiteral("undo takes the soft edges back"));

		// Hard edges, then the auto edge tool on the painted spot.
		m_softEdges = false;
		click();
		const int hard = towardOther();
		m_textureMode = TextureMode::AutoEdge;
		click();
		const int autoEdged = towardOther();
		step(autoEdged > hard && history.undoName() == "Auto edge",
			QStringLiteral("auto edge blends the painted spot (%1 -> %2 cells)").arg(hard).arg(autoEdged));
		undo();

		// Blend edge: a drag from the painted spot onto a cell of another texture beside it.
		m_textureMode = TextureMode::Edge;
		const int cx0 = static_cast<int>(cell % static_cast<std::size_t>(hm.width));
		const int cy0 = static_cast<int>(cell / static_cast<std::size_t>(hm.width));
		int tx = cx0 + 1;
		while (tx < hm.width - 1 && MapEditorCore::cellClass(bt, hm.width, hm.height, tx, cy0, true) == otherIndex)
			++tx;
		const std::size_t target = static_cast<std::size_t>(cy0) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(tx);
		const auto cellCentre = [&hm, textureView](int x, int y) {
			return textureView->worldToWidget((static_cast<float>(x) - static_cast<float>(hm.borderSize) + 0.5f) * CELL,
				(static_cast<float>(y) - static_cast<float>(hm.borderSize) + 0.5f) * CELL);
		};
		const QPointF from = cellCentre(tx - 1, cy0);
		const QPointF to = cellCentre(tx, cy0);
		const auto sendTo = [textureView](QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
			QMouseEvent event(type, pos, textureView->mapToGlobal(pos), button, buttons, Qt::NoModifier);
			QApplication::sendEvent(textureView, &event);
		};
		sendTo(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
		sendTo(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
		sendTo(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
		const std::int16_t edge = bt.blends[target];
		step(history.undoName() == "Blend edge" && edge > 0 && static_cast<std::size_t>(edge) <= bt.blendedTiles.size()
				&& MapEditorCore::classOfTile(bt, bt.blendedTiles[static_cast<std::size_t>(edge) - 1].blendIndex) == otherIndex,
			QStringLiteral("a drag blends the edge of cell %1, %2").arg(tx).arg(cy0));
		undo();
		undo();
		step(bt == tilesOriginal, QStringLiteral("undo takes the edges and the paint back"));

		// Fill, pipette and removing unused textures, with tool events at the world position.
		const auto textureEvent = [this, w](bool alt, bool shift) {
			ToolEvent e;
			e.kind = ToolEvent::Kind::Press;
			e.x = static_cast<float>(w.x());
			e.y = static_cast<float>(w.y());
			e.alt = alt;
			e.shift = shift;
			terrainToolEvent(e);
		};
		m_textureMode = TextureMode::Fill;
		m_paintTexture = QString::fromStdString(other.name);
		textureEvent(false, false);
		step(classAt(bt.tiles[cell]) == otherIndex && history.undoName() == "Fill texture",
			QStringLiteral("fill paints the region with %1").arg(m_paintTexture));
		undo();
		textureEvent(false, true);
		step(classAt(bt.tiles[cell]) == otherIndex && history.undoName() == "Replace texture", QStringLiteral("Shift+fill replaces the texture everywhere"));
		undo();
		step(bt == tilesOriginal, QStringLiteral("undo takes the fills back"));
		m_textureMode = TextureMode::Paint;
		textureEvent(true, false);
		step(under >= 0 && m_paintTexture == QString::fromStdString(bt.textureClasses[static_cast<std::size_t>(under)].name),
			QStringLiteral("Alt+click takes the texture %1").arg(m_paintTexture));
		if (auto *optimize = findChild<QPushButton *>(QStringLiteral("OptimizeTextures")))
		{
			optimize->click();
			if (history.undoName() == "Remove unused textures")
			{
				step(bt.textureClasses.size() <= tilesOriginal.textureClasses.size(),
					QStringLiteral("unused textures removed (%1 -> %2 textures)").arg(tilesOriginal.textureClasses.size()).arg(bt.textureClasses.size()));
				undo();
				step(bt == tilesOriginal, QStringLiteral("undo brings the removed textures back"));
			}
			else
				step(bt == tilesOriginal, QStringLiteral("every texture of the map is in use, nothing removed"));
		}
		else
			step(false, QStringLiteral("the texture panel has no button to remove unused textures"));
		m_softEdges = true;
	}

	// Ramp and mesh mold, driven with tool events at world positions.
	const std::vector<std::uint8_t> shapeStart = hm.heights;
	m_terrainTool = TerrainTool::Shape;
	m_shapeTool = ShapeTool::Ramp;
	rebuild();
	QApplication::processEvents();
	view = m_overview;
	step(view && findChild<QSpinBox *>(QStringLiteral("RampWidth")), QStringLiteral("the shapes panel has the ramp"));
	if (view)
	{
		view->fit();
		const QPointF mid = view->widgetToWorld(QPointF(view->width() * 0.5, view->height() * 0.5));
		const QPointF wa = mid - QPointF(150.0, 0.0);
		const QPointF wb = mid + QPointF(150.0, 0.0);
		const auto heightAtWorld = [&hm](QPointF w) {
			float cx = 0.0f, cy = 0.0f;
			MapEditorCore::worldToCell(hm, static_cast<float>(w.x()), static_cast<float>(w.y()), cx, cy);
			return static_cast<int>(hm.heights[static_cast<std::size_t>(std::lround(cy)) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(std::lround(cx))]);
		};
		const auto tool = [this](ToolEvent::Kind kind, QPointF w, bool alt = false) {
			ToolEvent e;
			e.kind = kind;
			e.x = static_cast<float>(w.x());
			e.y = static_cast<float>(w.y());
			e.alt = alt;
			terrainToolEvent(e);
		};
		// A plateau at one end, so the ramp has something to climb.
		m_terrainTool = TerrainTool::Height;
		m_brush.mode = MapEditorCore::BrushMode::Set;
		m_brush.height = std::min(255, heightAtWorld(wa) + 80);
		m_brush.radius = 3.0f;
		m_brush.feather = 0.0f;
		tool(ToolEvent::Kind::Press, wb);
		tool(ToolEvent::Kind::Release, wb);
		m_terrainTool = TerrainTool::Shape;
		m_rampWidth = 3.0f;
		tool(ToolEvent::Kind::Press, wa);
		tool(ToolEvent::Kind::Move, mid);
		tool(ToolEvent::Kind::Release, wb);
		const int ha = heightAtWorld(wa), hmid = heightAtWorld(mid), hb = heightAtWorld(wb);
		step(history.undoName() == "Ramp" && hb > ha + 40 && hmid > ha + 20 && hmid < hb - 20,
			QStringLiteral("a ramp climbs evenly (%1 -> %2 -> %3)").arg(ha).arg(hmid).arg(hb));
		undo();
		undo();
		step(hm.heights == shapeStart, QStringLiteral("undo takes ramp and plateau back"));

		// Mesh mold: a click shows the hill at once, Enter applies it, Esc discards it.
		m_shapeTool = ShapeTool::Mold;
		rebuild();
		QApplication::processEvents();
		step(!m_molds.empty() && findChild<QListWidget *>(QStringLiteral("MoldList")),
			QStringLiteral("the mold list has %1 built-in shapes and %2 mold files").arg(m_builtInMolds).arg(static_cast<int>(m_molds.size()) - m_builtInMolds));
		m_moldIndex = 0;
		m_moldPlace = MapEditorCore::MoldPlacement{};
		const std::string undoBefore = history.undoName();
		const int ground = heightAtWorld(mid);
		tool(ToolEvent::Kind::Press, mid);
		tool(ToolEvent::Kind::Release, mid);
		const int hill = heightAtWorld(mid);
		step(m_mold.active && hill >= std::min(255, ground + 40) && history.undoName() == undoBefore,
			QStringLiteral("a click shows the hill mold (%1 -> %2), not yet a step").arg(ground).arg(hill));
		if (m_overview)
		{
			const QString moldShot = QDir(shotDir).filePath(QStringLiteral("mold_2d.png"));
			step(m_overview->grab().save(moldShot), QStringLiteral("picture %1").arg(moldShot));
		}
		tool(ToolEvent::Kind::Press, mid);
		tool(ToolEvent::Kind::Move, mid + QPointF(40.0, 0.0));
		tool(ToolEvent::Kind::Release, mid + QPointF(40.0, 0.0));
		const int moved = heightAtWorld(mid + QPointF(40.0, 0.0));
		const int left = heightAtWorld(mid);
		step(moved >= hill - 2 && left < hill - 10, QStringLiteral("dragging moves the mold (peak %1, old place %2)").arg(moved).arg(left));
		toolKey(Qt::Key_Return);
		step(!m_mold.active && history.undoName() == "Mesh mold", QStringLiteral("Enter applies the mold as one step"));
		undo();
		step(hm.heights == shapeStart, QStringLiteral("undo takes the mold back"));
		tool(ToolEvent::Kind::Press, mid);
		tool(ToolEvent::Kind::Release, mid);
		escape();
		step(!m_mold.active && hm.heights == shapeStart && history.undoName() == undoBefore, QStringLiteral("Esc discards the mold"));
		// A mold of the game's mold files, sampled from its mesh by the engine.
		if (static_cast<int>(m_molds.size()) > m_builtInMolds)
		{
			m_moldIndex = m_builtInMolds;
			m_moldPlace = MapEditorCore::MoldPlacement{};
			const MapEditorCore::Mold &file = m_molds[static_cast<std::size_t>(m_moldIndex)];
			float top = -1e9f;
			for (float z : file.z)
				top = std::isnan(z) ? top : std::max(top, z);
			tool(ToolEvent::Kind::Press, mid);
			tool(ToolEvent::Kind::Release, mid);
			const int molded = heightAtWorld(mid);
			step(m_mold.active && top > 0.0f && molded > ground + static_cast<int>(top / HEIGHT_SCALE) / 2,
				QStringLiteral("the mold file %1 (%2 x %3 samples, top %4) raises the ground (%5 -> %6)").arg(QString::fromStdString(file.name))
					.arg(file.cols).arg(file.rows).arg(static_cast<double>(top), 0, 'f', 1).arg(ground).arg(molded));
			escape();
		}
	}

	// Passability: the brush marks cells impassable, shaded red; undo clears them again.
	m_terrainTool = TerrainTool::Pathing;
	rebuild();
	QApplication::processEvents();
	view = m_overview;
	step(view && findChild<QPushButton *>(QStringLiteral("CliffsFromHeights")), QStringLiteral("the passability panel is there"));
	if (view && !m_state.map->map().blendTiles->cliffState.empty())
	{
		view->fit();
		std::vector<std::uint8_t> &flags = m_state.map->map().blendTiles->cliffState;
		const std::vector<std::uint8_t> flagsStart = flags;
		const QPointF mid = view->widgetToWorld(QPointF(view->width() * 0.5, view->height() * 0.5));
		float cx = 0.0f, cy = 0.0f;
		MapEditorCore::worldToCell(hm, static_cast<float>(mid.x()), static_cast<float>(mid.y()), cx, cy);
		const int ix = static_cast<int>(std::floor(cx));
		const int iy = static_cast<int>(std::floor(cy));
		const auto pathing = [this, mid](ToolEvent::Kind kind) {
			ToolEvent e;
			e.kind = kind;
			e.x = static_cast<float>(mid.x());
			e.y = static_cast<float>(mid.y());
			terrainToolEvent(e);
		};
		m_markImpassable = true;
		m_brush.radius = 3.0f;
		pathing(ToolEvent::Kind::Press);
		pathing(ToolEvent::Kind::Release);
		step(MapModel::cliffFlag(flags, hm.width, ix, iy) && history.undoName() == "Mark impassable"
				&& (MapModel::cliffFlag(flagsStart, hm.width, ix, iy) || flags != flagsStart),
			QStringLiteral("the brush marks cell %1, %2 impassable").arg(ix).arg(iy));
		const QString pathShot = QDir(shotDir).filePath(QStringLiteral("pathing_2d.png"));
		step(view->grab().save(pathShot), QStringLiteral("picture %1").arg(pathShot));
		if (history.undoName() == "Mark impassable")
			undo();
		step(flags == flagsStart, QStringLiteral("undo clears them again"));
		m_markImpassable = false;
		pathing(ToolEvent::Kind::Press);
		pathing(ToolEvent::Kind::Release);
		step(!MapModel::cliffFlag(flags, hm.width, ix, iy), QStringLiteral("the brush marks cells passable"));
		if (flags != flagsStart)
			undo();
		m_markImpassable = true;
		if (auto *reset = findChild<QPushButton *>(QStringLiteral("CliffsFromHeights")))
		{
			reset->click();
			std::vector<std::uint8_t> computed = flags;
			MapModel::updateCliffFlags(hm, computed, 0, 0, hm.width - 1, hm.height - 1);
			step(flags == computed, QStringLiteral("\"set from the heights\" computes the flags as the game does (%1)")
				.arg(history.undoName() == "Passability from heights" ? QStringLiteral("changed") : QStringLiteral("unchanged")));
			if (history.undoName() == "Passability from heights")
				undo();
			step(flags == flagsStart, QStringLiteral("undo restores the map's own flags"));
		}
	}
	m_terrainTool = TerrainTool::Height;

	while (history.isModified() && history.canUndo())
		undo();
	step(hm.heights == original && !history.isModified(), QStringLiteral("undo all, heights as before"));
	m_brush = brushBefore;
	m_workMode = WorkMode::View;
	rebuild();
	QApplication::processEvents();
	return step.ok();
}

} // namespace MapEditorApp
