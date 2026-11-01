/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowViewer.cpp -- the map page: the 2D overview and the 3D view side by side, the side panels and
// the status line.

#include "App/MainWindow.h"
#include "App/Windows/SidesWindow.h"
#include "App/EngineClient.h"
#include "App/Icons.h"
#include "App/Language.h"
#include "App/MapImage.h"
#include "App/MapOverview.h"
#include "App/MiniMap.h"
#include "App/ToolEvent.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/SidesCommands.h"
#include "Model/MapDump.h"
#include "Model/MapIO.h"
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

namespace MapEditorApp
{

namespace
{

QString mapName(const MapModel::Map &map, const QString &fallback)
{
	if (map.worldInfo)
	{
		if (const auto name = map.worldInfo->getAscii("mapName"))
		{
			QString n = QString::fromStdString(*name);
			// Official maps name a string table entry ("MAP:AlpineAssault"); show the key readable.
			if (n.startsWith(QStringLiteral("MAP:")))
				n = n.mid(4);
			if (!n.isEmpty())
				return n;
		}
	}
	return fallback;
}

} // namespace

// Map viewer

QWidget *MainWindow::buildMapPage()
{
	const Tokens t = tokens(m_state.theme, m_state.game);
	const MapModel::Map &map = m_state.map->loaded.result.map;
	auto *page = new QWidget;
	page->setObjectName(QStringLiteral("Page"));
	auto *row = new QHBoxLayout(page);
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(0);

	// Left: facts about the map.
	auto *left = new QWidget;
	left->setObjectName(QStringLiteral("Panel"));
	left->setAttribute(Qt::WA_StyledBackground);
	left->setFixedWidth(300);
	auto *lc = new QVBoxLayout(left);
	lc->setContentsMargins(18, 18, 18, 18);
	lc->setSpacing(8);
	const QString fallback = m_state.map->path.isEmpty() ? T("Unnamed map") : QFileInfo(m_state.map->path).completeBaseName();
	auto *name = label(mapName(map, fallback), "Heading");
	name->setWordWrap(true);
	lc->addWidget(name);
	auto *file = label(m_state.map->path.isEmpty() ? T("Not saved yet") : QDir::toNativeSeparators(m_state.map->path), "Meta");
	file->setWordWrap(true);
	lc->addWidget(file);
	lc->addSpacing(8);
	lc->addWidget(label(T("MAP"), "Section"));
	auto *facts = new QGridLayout;
	facts->setHorizontalSpacing(12);
	facts->setVerticalSpacing(6);
	int factRow = 0;
	auto fact = [&](const QString &key, const QString &value) {
		facts->addWidget(label(key, "Hint"), factRow, 0);
		auto *v = label(value, nullptr);
		v->setWordWrap(true);
		facts->addWidget(v, factRow, 1);
		++factRow;
	};
	if (map.heightMap)
	{
		const auto &hm = *map.heightMap;
		fact(T("Size"), T("%1 \u00d7 %2 cells").arg(hm.width).arg(hm.height));
		if (!hm.boundaries.empty())
			fact(T("Playable"), QStringLiteral("%1 \u00d7 %2").arg(hm.boundaries[0].x).arg(hm.boundaries[0].y));
		fact(T("Border"), QString::number(hm.borderSize));
	}
	std::size_t waypoints = 0;
	std::size_t starts = 0;
	if (map.objects)
	{
		for (const MapModel::MapObject &o : *map.objects)
		{
			if (!o.properties.getInt("waypointID"))
				continue;
			++waypoints;
			const std::string wn = o.properties.getAscii("waypointName").value_or("");
			if (wn.rfind("Player_", 0) == 0 && wn.find("_Start") != std::string::npos)
				++starts;
		}
		fact(T("Objects"), QString::number(map.objects->size() - waypoints));
	}
	fact(T("Waypoints"), QString::number(waypoints));
	fact(T("Start positions"), QString::number(starts));
	if (map.polygons)
	{
		const auto water = std::count_if(map.polygons->begin(), map.polygons->end(), [](const MapModel::PolygonArea &a) { return a.isWater; });
		fact(T("Areas"), T("%1 (%2 water)").arg(map.polygons->size()).arg(water));
	}
	fact(T("Compression"), m_state.map->loaded.compression == 0 ? T("none") : QStringLiteral("RefPack"));
	lc->addLayout(facts);
	// Beside the 3D view: the whole map with the camera; a click moves the camera there.
	if (m_show3D && map.heightMap)
	{
		lc->addSpacing(8);
		auto *mini = new MiniMap(map);
		mini->setObjectName(QStringLiteral("MiniMap"));
		mini->setFixedHeight(220);
		mini->onJump = [this](float x, float y) { centerOn(x, y); };
		auto *follow = new QTimer(mini);
		connect(follow, &QTimer::timeout, mini, [this, mini] {
			if (!m_viewport || !m_viewport->hasDevice())
				return;
			const MeCamera c = EngineClient::instance().camera();
			// The camera looks along (sin yaw, cos yaw), see the view's camera placement.
			mini->setCamera(c.targetX, c.targetY, std::atan2(std::cos(c.yaw), std::sin(c.yaw)));
		});
		follow->start(150);
		lc->addWidget(mini);
	}

	if (map.blendTiles && !map.blendTiles->textureClasses.empty())
	{
		lc->addSpacing(8);
		lc->addWidget(label(T("TEXTURES"), "Section"));
		auto *list = new QListWidget;
		for (const MapModel::TextureClass &tc : map.blendTiles->textureClasses)
			list->addItem(utf8(tc.name));
		lc->addWidget(list, 1);
	}
	else
	{
		lc->addStretch(1);
	}
	row->addWidget(left);
	// In the objects mode the catalog takes the place of the map facts (MainWindowCatalog.cpp).
	if (m_workMode == WorkMode::Objects)
	{
		left->hide();
		row->insertWidget(0, buildCatalogPanel(t));
	}
	else if (terrainTool())
	{
		left->hide();
		row->insertWidget(0, buildTerrainPanel(t));
	}

	// Centre: overview, with a note for maps in older versions.
	auto *centre = new QWidget;
	auto *cc = new QVBoxLayout(centre);
	cc->setContentsMargins(0, 0, 0, 0);
	cc->setSpacing(0);
	const MapModel::ReadResult &rr = m_state.map->loaded.result;
	if (!rr.upgrades.empty() || !rr.warnings.empty())
	{
		auto *note = new QLabel;
		QString text = !rr.upgrades.empty()
			? T("This map uses older format versions. Saving writes the newest ones, like the old WorldBuilder; the content stays the same.")
			: QString();
		for (const std::string &w : rr.warnings)
			text += (text.isEmpty() ? QString() : QStringLiteral("\n")) + T("Note:") + QStringLiteral(" ") + utf8(w);
		note->setText(text);
		note->setWordWrap(true);
		note->setStyleSheet(QStringLiteral("background: %1; color: %2; padding: 8px 14px; border-bottom: 1px solid %3;")
			.arg(t.s1.name(), t.tx2.name(), t.bd.name()));
		cc->addWidget(note);
	}
	// View switch: the 2D overview needs nothing, the 3D view the engine and the game data.
	auto *switchBar = new QWidget;
	switchBar->setAttribute(Qt::WA_StyledBackground);
	switchBar->setStyleSheet(QStringLiteral("background: %1; border-bottom: 1px solid %2;").arg(t.s1.name(), t.bd.name()));
	auto *sb = new QHBoxLayout(switchBar);
	sb->setContentsMargins(14, 6, 14, 6);
	sb->addWidget(segmented({T("2D overview"), T("3D view")}, m_show3D ? 1 : 0, [this](int i) {
		m_show3D = i == 1;
		QTimer::singleShot(0, this, [this] { rebuild(); });
	}, switchBar));
	sb->addStretch(1);
	if (m_show3D)
	{
		auto *startCam = new QPushButton(T("Start camera"));
		startCam->setObjectName(QStringLiteral("Ghost"));
		startCam->setCursor(Qt::PointingHandCursor);
		startCam->setToolTip(QStringLiteral("Home"));
		connect(startCam, &QPushButton::clicked, this, [this] { m_viewport->resetCamera(); });
		auto *whole = new QPushButton(T("Whole map"));
		whole->setObjectName(QStringLiteral("Ghost"));
		whole->setCursor(Qt::PointingHandCursor);
		connect(whole, &QPushButton::clicked, this, [this] { m_viewport->showWholeMap(); });
		// What the 3D view shows on top of the scene.
		const auto overlayChip = [this, sb](const QString &text, bool OverlayOptions::*flag) {
			auto *chip = new QPushButton(text);
			chip->setObjectName(QStringLiteral("Chip"));
			chip->setCheckable(true);
			chip->setChecked(m_overlay.*flag);
			chip->setCursor(Qt::PointingHandCursor);
			connect(chip, &QPushButton::toggled, this, [this, flag](bool on) {
				m_overlay.*flag = on;
				applyOverlay();
			});
			sb->addWidget(chip);
		};
		overlayChip(T("Borders"), &OverlayOptions::boundaries);
		overlayChip(T("Waypoints"), &OverlayOptions::waypoints);
		overlayChip(T("Areas"), &OverlayOptions::areas);
		sb->addSpacing(12);
		sb->addWidget(startCam);
		sb->addWidget(whole);
	}
	cc->addWidget(switchBar);
	QLabel *hint = nullptr;
	if (m_show3D)
	{
		cc->addWidget(buildViewArea(t), 1);
		hint = label(T("Left button selects \u00b7 middle button moves \u00b7 right button turns \u00b7 wheel zooms \u00b7 double-click or Home: start camera"), "Hint");
	}
	else
	{
		auto *overview = new MapOverview(map, m_state.map->selection, t);
		overview->onHover = [this](int cx, int cy, int h, float wx, float wy) {
			// Remembered for pasting at the cursor.
			m_hoverValid = cx >= 0;
			m_hoverX = wx;
			m_hoverY = wy;
			brushMoved(cx >= 0, wx, wy);
			if (!m_statusLeft)
				return;
			if (cx < 0)
				m_statusLeft->setText(QString());
			else
				m_statusLeft->setText(T("Cell %1, %2   World %3, %4   Height %5").arg(cx).arg(cy).arg(static_cast<double>(wx), 0, 'f', 0).arg(static_cast<double>(wy), 0, 'f', 0).arg(h));
		};
		overview->onClickObject = [this](int object, bool toggle, bool released) { clickObject(object, toggle, released); };
		overview->onSelectRect = [this](const std::vector<int> &objects, bool add) { selectRect(objects, add); };
		overview->onDragSelection = [this](float dx, float dy) { dragSelection(dx, dy); };
		overview->onDragSelectionEnd = [this](bool cancel) { endDragSelection(cancel); };
		overview->onRotateSelection = [this](float startX, float startY, float x, float y, bool snap) {
			rotateSelection(startX, startY, x, y, snap);
		};
		overview->onRotateSelectionEnd = [this](bool cancel) { endRotateSelection(cancel); };
		overview->onEscape = [this] { escape(); };
		overview->onPlace = [this](float x, float y, float angle, bool aimed, int object, bool ctrl) { placeObject(x, y, angle, aimed, object, ctrl); };
		overview->onTool = [this](const ToolEvent &e) { toolEvent(e); };
		overview->onToolKey = [this](int key) { toolKey(key); };
		overview->setToolMode(areaTool() || roadTool() || terrainTool() || placementTool());
		overview->setPlacing(!m_placeTemplate.isEmpty() && !areaTool() && !roadTool());
		overview->setToolLines(toolLines());
		overview->setHiddenLayers(m_hiddenLayers);
		overview->setAreaMarks(m_area, m_areaPoint, m_areaDraft ? &*m_areaDraft : nullptr);
		m_overview = overview;
		overview->setContours(m_state.display.contours ? m_state.display.contourStep : 0, m_state.display.contourOffset);
		overview->setShowGrid(m_state.display.grid);
		overview->setShowLabels(m_state.display.labels);
		overview->setShowObjects(m_state.display.objectIcons);
		cc->addWidget(overview, 1);
		QTimer::singleShot(0, overview, [overview] { overview->fit(); });
		hint = label(T("Left button selects \u00b7 middle or right button moves \u00b7 wheel zooms \u00b7 double-click fits"), "Hint");
	}
	hint->setContentsMargins(14, 6, 14, 6);
	hint->setStyleSheet(QStringLiteral("background: %1;").arg(t.s1.name()));
	cc->addWidget(hint);
	row->addWidget(centre, 1);

	// Right: the selection, then players, their teams, build lists and scripts.
	auto *right = new QWidget;
	right->setObjectName(QStringLiteral("Panel"));
	right->setAttribute(Qt::WA_StyledBackground);
	right->setFixedWidth(300);
	auto *rc = new QVBoxLayout(right);
	rc->setContentsMargins(14, 18, 14, 18);
	rc->setSpacing(8);
	// The selection on top, scrolling when its fields are more than the window shows; filled by
	// fillSelectionPanel (MainWindowEdit.cpp), which also hides the scroll area without a selection.
	auto *selectionScroll = new QScrollArea;
	selectionScroll->setWidgetResizable(true);
	selectionScroll->setFrameShape(QFrame::NoFrame);
	selectionScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_selectionBox = new QWidget;
	m_selectionBox->setObjectName(QStringLiteral("Panel"));
	selectionScroll->setWidget(m_selectionBox);
	rc->addWidget(selectionScroll, 3);
	fillSelectionPanel();
	rc->addWidget(label(T("PLAYERS AND TEAMS"), "Section"));
	auto *tree = new QTreeWidget;
	tree->setObjectName(QStringLiteral("SidesTree"));
	tree->setHeaderHidden(true);
	tree->setIndentation(14);
	tree->setToolTip(T("Double-click opens players and teams"));
	m_sidesTree = tree;
	fillSidesTree();
	// Double-click: a player opens the player page, a team the team page.
	connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
		// "Scripts: n" opens the scripts of that player.
		if (item && item->data(0, Qt::UserRole + 2).isValid())
		{
			const MapEditorCore::ScriptPath player{item->data(0, Qt::UserRole + 2).toInt(), -1, -1};
			showScripts(&player);
			return;
		}
		while (item && !item->data(0, Qt::UserRole + 1).isValid())
			item = item->parent();
		if (!item)
			return;
		const int index = item->data(0, Qt::UserRole + 1).toInt();
		SidesWindow *sides = sidesWindow();
		if (index >= 0)
		{
			sides->setPlayer(index);
			sides->setTeam(-1);
			showSides(0);
		}
		else
		{
			const int team = -2 - index;
			sides->setTeam(team);
			const MapModel::Map &m = m_state.map->map();
			if (m.sides && team < static_cast<int>(m.sides->teams.size()))
				sides->setPlayer(std::max(0, MapEditorCore::findPlayer(*m.sides, MapEditorCore::teamOwner(m.sides->teams[static_cast<std::size_t>(team)]))));
			showSides(1);
		}
	});
	rc->addWidget(tree, 1);
	row->addWidget(right);

	return page;
}

} // namespace MapEditorApp
