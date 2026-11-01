/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowTextures.cpp -- the texture brush of the terrain mode: paint (with soft edges), fill, blend an
// edge, auto edge a region, map cliff textures, the pipette and cleaning up the texture classes.

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
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

namespace MapEditorApp
{

void MainWindow::addTexturePanel(QVBoxLayout *col, const Tokens &t)
{
	col->addWidget(label(T("TEXTURE BRUSH"), "Section"));
	auto *modes = segmented({T("Paint"), T("Fill"), T("Edge"), T("Auto edge"), T("Cliffs")}, static_cast<int>(m_textureMode), [this](int i) {
		const TextureMode mode = static_cast<TextureMode>(i);
		if (mode == m_textureMode)
			return;
		m_textureMode = mode;
		QTimer::singleShot(0, this, [this] { rebuild(); });
	}, nullptr, {true, true, true, true});
	modes->setObjectName(QStringLiteral("TextureModes"));
	col->addWidget(modes);
	auto *search = new QLineEdit;
	search->setObjectName(QStringLiteral("TextureSearch"));
	search->setPlaceholderText(T("Search textures"));
	search->setClearButtonEnabled(true);
	search->addAction(icon(QStringLiteral("search"), t.tx3), QLineEdit::LeadingPosition);
	search->setText(m_textureFilter);
	col->addWidget(search);
	// Every paintable texture of the game; the map's own come first, marked with a dot.
	auto *list = new QListWidget;
	list->setObjectName(QStringLiteral("TextureList"));
	list->setIconSize(QSize(40, 40));
	list->setUniformItemSizes(true);
	const MapModel::BlendTiles *tiles = m_state.map && m_state.map->map().blendTiles ? &*m_state.map->map().blendTiles : nullptr;
	std::vector<std::pair<bool, const MeTerrainInfo *>> entries;
	for (const MeTerrainInfo &info : EngineClient::instance().terrains())
	{
		if (!info.blendEdge)
			entries.push_back({tiles && MapEditorCore::textureClassIndex(*tiles, info.name) >= 0, &info});
	}
	std::stable_sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.first && !b.first; });
	for (const auto &[inMap, info] : entries)
	{
		const QString name = QString::fromUtf8(info->name);
		auto *item = new QListWidgetItem(inMap ? name + QStringLiteral("  \u2022") : name, list);
		item->setData(Qt::UserRole, name);
		const QImage swatch = EngineClient::instance().terrainPreview(name, 40);
		if (!swatch.isNull())
			item->setIcon(QIcon(QPixmap::fromImage(swatch)));
		item->setToolTip(inMap ? T("%1 (in the map)").arg(name) : T("%1 (added to the map when painted)").arg(name));
		if (name == m_paintTexture)
			list->setCurrentItem(item);
	}
	if (entries.empty())
		list->addItem(T("No textures: the texture brush needs the game data."));
	const auto filter = [list](const QString &text) {
		for (int i = 0; i < list->count(); ++i)
			list->item(i)->setHidden(!text.isEmpty() && !list->item(i)->text().contains(text, Qt::CaseInsensitive));
	};
	filter(m_textureFilter);
	connect(search, &QLineEdit::textChanged, this, [this, filter](const QString &text) {
		m_textureFilter = text;
		filter(text);
	});
	connect(list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item, QListWidgetItem *) {
		m_paintTexture = item ? item->data(Qt::UserRole).toString() : QString();
		updateSelectionStatus();
	});
	col->addWidget(list, 1);
	// How full the terrain atlas and the blend tables are (the old "Texture sizing -> Info").
	if (m_state.map && m_state.map->map().blendTiles)
	{
		const MapModel::BlendTiles &bt = *m_state.map->map().blendTiles;
		// Tiles of 64 pixels with an 8 pixel border in quarters of a 2048 atlas, as the engine packs them.
		constexpr int ATLAS_TILES = 4 * (2048 / (2 * 64 + 8)) * (2048 / (2 * 64 + 8));
		constexpr int BLEND_ENTRIES = 16192;
		int usedTiles = 0;
		for (const MapModel::TextureClass &c : bt.textureClasses)
			usedTiles += c.numTiles;
		auto *stats = label(T("Atlas %1 of %2 tiles \u00b7 blends %3 of %4 \u00b7 %5 cliff mappings")
			.arg(usedTiles).arg(ATLAS_TILES).arg(bt.blendedTiles.size() + 1).arg(BLEND_ENTRIES).arg(bt.cliffInfos.size()), "Hint");
		stats->setObjectName(QStringLiteral("TextureStats"));
		stats->setWordWrap(true);
		col->addWidget(stats);
	}

	// Drops the textures no cell shows any more, so the atlas has room again.
	auto *optimize = new QPushButton(T("Remove unused textures"));
	optimize->setObjectName(QStringLiteral("OptimizeTextures"));
	connect(optimize, &QPushButton::clicked, this, [this] {
		if (!m_state.map || !m_state.map->map().blendTiles || !m_state.map->map().heightMap)
			return;
		MapModel::BlendTiles &bt = *m_state.map->map().blendTiles;
		const MapModel::HeightMap &hm = *m_state.map->map().heightMap;
		const MapModel::BlendTiles before = bt;
		if (!MapEditorCore::optimizeTextures(bt, hm.width, hm.height))
		{
			toast(T("Every texture of the map is in use."));
			return;
		}
		const int removed = static_cast<int>(before.textureClasses.size() - bt.textureClasses.size());
		recordTiles(before, MapEditorCore::CellRect{0, 0, hm.width - 1, hm.height - 1}, "Remove unused textures");
		toast(T("%1 unused textures removed, blend table %2 -> %3 entries").arg(removed).arg(before.blendedTiles.size()).arg(bt.blendedTiles.size()));
		QTimer::singleShot(0, this, [this] { rebuild(); });
	});
	if (m_textureMode == TextureMode::Fill)
	{
		auto *hint = label(T("Click a texture region: it takes the chosen texture, up to where other textures begin. With Shift every cell of that texture on the map. Alt+click takes the texture under the cursor."), "Hint");
		hint->setWordWrap(true);
		col->addWidget(hint);
		col->addWidget(optimize);
		return;
	}
	if (m_textureMode == TextureMode::Cliffs)
	{
		auto *hint = label(T("Click a steep slope: the texture there is laid along the slope instead of being stretched down it, over the whole connected cliff of that texture."), "Hint");
		hint->setWordWrap(true);
		col->addWidget(hint);
		auto *all = new QPushButton(T("Align all cliffs"));
		all->setObjectName(QStringLiteral("TextureAllCliffs"));
		connect(all, &QPushButton::clicked, this, [this] {
			MapModel::BlendTiles &bt = *m_state.map->map().blendTiles;
			const MapModel::BlendTiles before = bt;
			const int regions = MapEditorCore::textureAllCliffs(bt, *m_state.map->map().heightMap);
			if (regions == 0 || bt == before)
			{
				bt = before;
				toast(T("The map has no cliff left to align."));
				return;
			}
			recordCliffs(before, "Texture cliffs");
			toast(T("%1 cliffs aligned").arg(regions));
		});
		auto *remove = new QPushButton(T("Remove cliff alignment"));
		remove->setObjectName(QStringLiteral("RemoveCliffMapping"));
		connect(remove, &QPushButton::clicked, this, [this] {
			MapModel::BlendTiles &bt = *m_state.map->map().blendTiles;
			const MapModel::BlendTiles before = bt;
			if (MapEditorCore::removeCliffMapping(bt) == 0)
			{
				toast(T("The map has no aligned cliffs."));
				return;
			}
			recordCliffs(before, "Remove cliff alignment");
		});
		col->addWidget(all);
		col->addWidget(remove);
		return;
	}
	if (m_textureMode != TextureMode::Paint)
	{
		auto *hint = label(m_textureMode == TextureMode::Edge
			? T("Drag from a cell onto its neighbour: the neighbour gets a soft edge of the texture you started on. With Shift the texture chosen above blends in instead.")
			: T("Click a texture region: its edges blend softly into the textures around it, all around the region."), "Hint");
		hint->setWordWrap(true);
		col->addWidget(hint);
		col->addWidget(optimize);
		return;
	}
	auto *soft = new QCheckBox(T("Soft edges"));
	soft->setObjectName(QStringLiteral("SoftEdges"));
	soft->setChecked(m_softEdges);
	connect(soft, &QCheckBox::toggled, this, [this](bool on) { m_softEdges = on; });
	col->addWidget(soft);
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
		if (m_overview)
			m_overview->update();
	});
	row->addWidget(size, 1);
	col->addLayout(row);
	auto *hint = label(T("Drag to paint the chosen texture; with soft edges it blends into the textures around it, else the edges stay hard as with the old tile tool. A texture the map lacks is added if the terrain atlas has room. Alt+click takes the texture under the cursor."), "Hint");
	hint->setWordWrap(true);
	col->addWidget(hint);
	col->addWidget(optimize);
}

void MainWindow::reloadEngineMap()
{
	if (!m_viewport->hasDevice() || m_viewMapSerial != m_mapSerial)
		return;
	EngineClient &engine = EngineClient::instance();
	const MeCamera camera = engine.camera();
	const MapModel::WriteResult bytes = engineMapBytes();
	MeMapInfo info{};
	QString error;
	if (!bytes.ok() || !engine.loadMap(bytes.bytes, &info, &error))
	{
		m_viewMapSerial = -1;
		return;
	}
	m_viewport->setMapInfo(info);
	engine.setCamera(camera);
	updateImpassableView();
	applyOverlay();
}

void MainWindow::recordCliffs(const MapModel::BlendTiles &before, const char *name)
{
	const MapModel::HeightMap &hm = *m_state.map->map().heightMap;
	// A changed cliff table makes the view load the map again (EditTiles::changes).
	recordTiles(before, MapEditorCore::CellRect{0, 0, hm.width - 1, hm.height - 1}, name);
}

void MainWindow::recordTiles(const MapModel::BlendTiles &before, MapEditorCore::CellRect rect, const char *name)
{
	auto command = MapEditorCore::tileStroke(m_state.map->map(), before, rect, name);
	if (!command)
		return;
	const MapEditorCore::Changes changes = command->changes();
	m_state.map->history.record(std::move(command));
	applyChanges(changes);
}

void MainWindow::textureToolEvent(const ToolEvent &e)
{
	MapModel::Map &map = m_state.map->map();
	if (!map.blendTiles || !map.heightMap)
		return;
	MapModel::BlendTiles &bt = *map.blendTiles;
	const MapModel::HeightMap &hm = *map.heightMap;
	float cx = 0.0f, cy = 0.0f;
	MapEditorCore::worldToCell(hm, e.x, e.y, cx, cy);
	const int ix = std::clamp(static_cast<int>(std::floor(cx)), 0, hm.width - 1);
	const int iy = std::clamp(static_cast<int>(std::floor(cy)), 0, hm.height - 1);
	if (e.kind == ToolEvent::Kind::Press && e.alt && (m_textureMode == TextureMode::Paint || m_textureMode == TextureMode::Fill))
	{
		pickTexture(ix, iy);
		return;
	}
	// The chosen texture's class in the map, added if the map lacks it (-1: no room, told).
	const auto classFor = [&]() {
		if (m_paintTexture.isEmpty())
		{
			toast(T("Choose a texture first."));
			return -1;
		}
		const std::string name = m_paintTexture.toUtf8().toStdString();
		int classIndex = MapEditorCore::textureClassIndex(bt, name);
		if (classIndex >= 0)
			return classIndex;
		const MeTerrainInfo *info = nullptr;
		for (const MeTerrainInfo &terrain : EngineClient::instance().terrains())
		{
			if (name == terrain.name)
				info = &terrain;
		}
		classIndex = info ? MapEditorCore::addTextureClass(bt, name, info->numTiles, info->width) : -1;
		if (classIndex < 0)
			toast(T("The terrain atlas has no room for %1.").arg(m_paintTexture));
		return classIndex;
	};

	if (m_textureMode == TextureMode::Fill)
	{
		if (e.kind == ToolEvent::Kind::Move)
			brushMoved(true, e.x, e.y);
		if (e.kind != ToolEvent::Kind::Press)
			return;
		const MapModel::BlendTiles before = bt;
		const int classIndex = classFor();
		if (classIndex < 0)
			return;
		const MapEditorCore::CellRect changed = MapEditorCore::fillTexture(bt, hm.width, hm.height, ix, iy, classIndex, e.shift);
		if (changed.empty())
		{
			bt = before;
			toast(T("This place shows %1 already.").arg(m_paintTexture));
			return;
		}
		recordTiles(before, changed, e.shift ? "Replace texture" : "Fill texture");
		return;
	}

	if (m_textureMode == TextureMode::Cliffs)
	{
		if (e.kind == ToolEvent::Kind::Move)
			brushMoved(true, e.x, e.y);
		if (e.kind != ToolEvent::Kind::Press)
			return;
		const MapModel::BlendTiles before = bt;
		if (!MapEditorCore::textureCliffRegion(bt, hm, ix, iy) || bt == before)
		{
			bt = before;
			toast(T("No cliff to align here: click a steep slope."));
			return;
		}
		recordCliffs(before, "Texture cliffs");
		return;
	}
	if (m_textureMode == TextureMode::AutoEdge)
	{
		if (e.kind == ToolEvent::Kind::Move)
			brushMoved(true, e.x, e.y);
		if (e.kind != ToolEvent::Kind::Press)
			return;
		const MapModel::BlendTiles before = bt;
		const MapEditorCore::CellRect changed = MapEditorCore::autoBlend(bt, hm.width, hm.height, {{ix, iy}});
		if (changed.empty())
		{
			toast(T("Nothing to blend here: click a texture that meets other textures, not a blended cell."));
			return;
		}
		recordTiles(before, changed, "Auto edge");
		return;
	}
	if (m_textureMode == TextureMode::Edge)
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
			float fx = 0.0f, fy = 0.0f;
			MapEditorCore::worldToCell(hm, m_shapeDrag.x1, m_shapeDrag.y1, fx, fy);
			const int sx = std::clamp(static_cast<int>(std::floor(fx)), 0, hm.width - 1);
			const int sy = std::clamp(static_cast<int>(std::floor(fy)), 0, hm.height - 1);
			// With Shift the chosen texture blends in, as the old tool does with the right button.
			const int classIndex = e.shift && !m_paintTexture.isEmpty() ? MapEditorCore::textureClassIndex(bt, m_paintTexture.toUtf8().toStdString()) : -1;
			if (e.shift && classIndex < 0)
				toast(T("The chosen texture is not in the map yet: paint it once first."));
			const MapModel::BlendTiles before = bt;
			const MapEditorCore::CellRect changed = (sx == ix && sy == iy) || (e.shift && classIndex < 0)
				? MapEditorCore::CellRect{} : MapEditorCore::blendEdge(bt, hm.width, hm.height, ix, iy, sx, sy, classIndex);
			if (changed.empty() && !(e.shift && classIndex < 0))
				toast(T("Drag from one cell onto a neighbouring cell of another texture."));
			recordTiles(before, changed, "Blend edge");
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

	const auto paint = [&](float px, float py, int classIndex) {
		const std::size_t blendCount = bt.blendedTiles.size();
		MapEditorCore::CellRect changed = MapEditorCore::paintTexture(bt, hm.width, hm.height, px, py, m_brush.radius, classIndex);
		if (changed.empty())
			return;
		if (m_softEdges)
			changed.unite(MapEditorCore::softenEdges(bt, hm.width, hm.height, classIndex, changed));
		m_stroke.rect.unite(changed);
		EngineClient &engine = EngineClient::instance();
		if (bt.blendedTiles.size() != blendCount)
			engine.setBlendTable(bt.blendedTiles);
		engine.setTiles(changed.x0, changed.y0, changed.x1, changed.y1, bt.tiles, bt.blends, bt.extraBlends, bt.cliffInfoIndices, hm.width);
		if (m_overview)
			m_overview->refreshImage();
		m_viewport->update();
	};
	switch (e.kind)
	{
	case ToolEvent::Kind::Press:
	{
		m_stroke = Stroke{};
		m_tilesBefore = bt;
		const std::size_t classCount = bt.textureClasses.size();
		const int classIndex = classFor();
		if (classIndex < 0)
			return;
		// The engine needs a new texture in its atlas before cells can show it.
		if (bt.textureClasses.size() != classCount)
			reloadEngineMap();
		m_stroke.active = true;
		m_stroke.lastX = cx;
		m_stroke.lastY = cy;
		m_strokeClass = classIndex;
		paint(cx, cy, classIndex);
		return;
	}
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
			paint(m_stroke.lastX + dx * static_cast<float>(i) / static_cast<float>(steps), m_stroke.lastY + dy * static_cast<float>(i) / static_cast<float>(steps), m_strokeClass);
		m_stroke.lastX = cx;
		m_stroke.lastY = cy;
		return;
	}
	case ToolEvent::Kind::Release:
	{
		if (!m_stroke.active)
			return;
		m_stroke.active = false;
		// Painted cells at the foot of a mapped cliff of their texture continue its mapping.
		const MapEditorCore::CellRect &r = m_stroke.rect;
		const bool fitted = MapEditorCore::fitPaintedCells(bt, hm, r.x0, r.y0, r.x1, r.y1) > 0;
		auto command = MapEditorCore::tileStroke(map, m_tilesBefore, m_stroke.rect, "Paint texture");
		m_tilesBefore = MapModel::BlendTiles{};
		if (!command)
			return;
		const MapEditorCore::Changes changes = command->changes();
		m_state.map->history.record(std::move(command));
		if (fitted)
		{
			applyChanges(changes);
			return;
		}
		// The engine already shows the stroke (and loaded a new class at its start).
		if (m_overview)
			m_overview->refreshImage();
		updateTitle();
		return;
	}
	case ToolEvent::Kind::DoubleClick:
		return;
	}
}

void MainWindow::pickTexture(int x, int y)
{
	const MapModel::Map &map = m_state.map->map();
	if (!map.blendTiles || !map.heightMap)
		return;
	const int classIndex = MapEditorCore::cellClass(*map.blendTiles, map.heightMap->width, map.heightMap->height, x, y, true);
	if (classIndex < 0)
		return;
	m_paintTexture = QString::fromStdString(map.blendTiles->textureClasses[static_cast<std::size_t>(classIndex)].name);
	toast(T("Texture %1 taken").arg(m_paintTexture));
	if (auto *list = findChild<QListWidget *>(QStringLiteral("TextureList")))
	{
		for (int i = 0; i < list->count(); ++i)
		{
			if (list->item(i)->data(Qt::UserRole).toString() == m_paintTexture)
			{
				list->setCurrentRow(i);
				list->scrollToItem(list->item(i));
			}
		}
	}
	updateSelectionStatus();
}

// ------------------------------------------------------------------------------------------------

} // namespace MapEditorApp
