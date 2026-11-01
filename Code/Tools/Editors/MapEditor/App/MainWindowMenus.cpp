/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowMenus.cpp -- the top bar: the menus with their commands and shortcuts, the quick toolbar and
// the problem button.

#include "App/MainWindow.h"
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
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
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

QPixmap dot(const QColor &c, int size)
{
	QPixmap pm(size, size);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(c);
	p.drawEllipse(0, 0, size, size);
	return pm;
}

} // namespace

// Top bar

QWidget *MainWindow::buildTopBar()
{
	const Tokens t = tokens(m_state.theme, m_state.game);
	auto *bar = new QWidget;
	bar->setObjectName(QStringLiteral("TopBar"));
	bar->setAttribute(Qt::WA_StyledBackground);
	bar->setFixedHeight(44);
	auto *row = new QHBoxLayout(bar);
	row->setContentsMargins(12, 0, 12, 0);
	row->setSpacing(10);

	auto *home = new QPushButton;
	home->setObjectName(QStringLiteral("Ghost"));
	home->setCursor(Qt::PointingHandCursor);
	home->setToolTip(T("Start page"));
	{
		QPixmap logo(22, 22);
		logo.fill(Qt::transparent);
		QPainter p(&logo);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(Qt::NoPen);
		p.setBrush(t.acc);
		p.drawRoundedRect(QRectF(1, 1, 20, 20), 5, 5);
		p.setBrush(Qt::white);
		const QPointF mountain[] = {{5, 16}, {9.5, 9}, {12.5, 13.5}, {14.5, 10.5}, {19, 16}};
		p.drawPolygon(mountain, 5);
		p.end();
		home->setIcon(QIcon(logo));
		home->setIconSize(QSize(22, 22));
	}
	connect(home, &QPushButton::clicked, this, [this] { showPage(Page::Start); });
	row->addWidget(home);

	auto *menus = new QMenuBar;
	menus->setNativeMenuBar(false);
	buildMenus(menus);
	row->addWidget(menus, 0, Qt::AlignVCenter);
	row->addStretch(1);

	auto *search = new QPushButton(T("Search commands"));
	search->setObjectName(QStringLiteral("Ghost"));
	search->setIcon(icon(QStringLiteral("search"), t.tx3));
	search->setCursor(Qt::PointingHandCursor);
	search->setMinimumWidth(320);
	search->setStyleSheet(QStringLiteral("QPushButton { background: %1; border: 1px solid %2; color: %3; padding: 4px 10px; }")
		.arg(t.s2.name(), t.bd.name(), t.tx3.name()));
	search->setToolTip(QStringLiteral("Ctrl+Shift+P"));
	connect(search, &QPushButton::clicked, this, [this] { showCommands(); });
	row->addWidget(search);
	row->addStretch(1);

	// View and Objects work; terrain and scripts follow.
	const int workIndex = m_workMode == WorkMode::Objects ? 2 : (m_workMode == WorkMode::Terrain ? 1 : 0);
	auto *work = segmented({T("View"), T("Terrain"), T("Objects"), T("Scripts")}, workIndex, [this](int i) {
		const WorkMode mode = i == 2 ? WorkMode::Objects : (i == 1 ? WorkMode::Terrain : WorkMode::View);
		if (mode == m_workMode)
			return;
		m_workMode = mode;
		if (mode != WorkMode::Objects)
			setPlacingTemplate(QString(), 0.0f);
		QTimer::singleShot(0, this, [this] { rebuild(); });
	}, bar, {true, m_state.map != nullptr && m_state.map->map().heightMap.has_value(), m_state.map != nullptr, false});
	work->setFixedHeight(30);
	row->addWidget(work, 0, Qt::AlignVCenter);

	auto *badge = new QLabel;
	badge->setPixmap(dot(t.accFg, 8));
	row->addWidget(badge);
	row->addWidget(label(gameName(m_state.game), "Badge"));

	QIcon play = icon(QStringLiteral("play"), QColor(Qt::white));
	play.addPixmap(iconPixmap(QStringLiteral("play"), t.tx3, 16), QIcon::Disabled);
	auto *test = new QPushButton(play, T("Test in game"));
	test->setObjectName(QStringLiteral("Primary"));
	test->setEnabled(m_state.map != nullptr);
	test->setToolTip(T("Saves the map and starts the game with it in a window (Ctrl+J)"));
	connect(test, &QPushButton::clicked, this, [this] { testInGame(); });
	row->addWidget(test);

	auto *settings = new QPushButton(icon(QStringLiteral("settings"), t.tx2), QString());
	settings->setObjectName(QStringLiteral("Ghost"));
	settings->setToolTip(T("Settings"));
	settings->setCursor(Qt::PointingHandCursor);
	connect(settings, &QPushButton::clicked, this, [this] { showPage(Page::Settings); });
	row->addWidget(settings);
	return bar;
}

void MainWindow::buildMenus(QMenuBar *bar)
{
	QMenu *file = bar->addMenu(T("File"));
	file->addAction(T("New map..."), QKeySequence::New, this, [this] { newMapDialog(); });
	file->addAction(T("Open..."), QKeySequence::Open, this, [this] { openDialog(); });
	QMenu *recent = file->addMenu(T("Recent maps"));
	for (std::size_t i = 0; i < m_state.recent.size() && i < 10; ++i)
	{
		const QString path = m_state.recent[i].path;
		recent->addAction(QFileInfo(path).completeBaseName(), this, [this, path] { openMapFile(path); });
	}
	recent->setEnabled(!recent->isEmpty());
	file->addSeparator();
	QAction *saveAct = file->addAction(T("Save"), QKeySequence::Save, this, [this] { save(false); });
	QAction *saveAs = file->addAction(T("Save as..."), QKeySequence::SaveAs, this, [this] { save(true); });
	QAction *settings = file->addAction(T("Map settings..."), this, [this] { showMapSettings(); });
	QAction *resize = file->addAction(T("Resize map..."), this, [this] { resizeMapDialog(); });
	QAction *sidesAct = file->addAction(T("Players and teams..."), this, [this] { showSides(); });
	QAction *scriptsAct = file->addAction(T("Scripts..."), this, [this] { showScripts(); });
	QAction *buildListAct = file->addAction(T("Build list"), this, [this] {
		if (m_workMode != WorkMode::Objects)
		{
			m_workMode = WorkMode::Objects;
			rebuild();
		}
		setPlacingTemplate(QString::fromLatin1(BUILD_LIST_TOOL), 0.0f);
	});
	QAction *close = file->addAction(T("Close map"), QKeySequence::Close, this, [this] { closeMap(); });
	// A new map has no file yet: Save asks for one.
	saveAct->setEnabled(m_state.map != nullptr);
	saveAs->setEnabled(m_state.map != nullptr);
	close->setEnabled(m_state.map != nullptr);
	settings->setEnabled(m_state.map != nullptr);
	resize->setEnabled(m_state.map != nullptr && m_state.map->map().heightMap.has_value());
	sidesAct->setEnabled(m_state.map != nullptr);
	scriptsAct->setEnabled(m_state.map != nullptr);
	buildListAct->setEnabled(m_state.map != nullptr);
	QAction *testAct = file->addAction(T("Test in game"), this, [this] { testInGame(); });
	testAct->setShortcuts({QKeySequence(QStringLiteral("F5")), QKeySequence(QStringLiteral("Ctrl+J"))});
	testAct->setEnabled(m_state.map != nullptr);
	file->addAction(T("Particle systems..."), this, [this] { showParticles(); });
	QAction *importAct = file->addAction(T("Import height map..."), this, [this] { importHeightmapDialog(); });
	importAct->setEnabled(m_state.map != nullptr && m_state.map->map().heightMap.has_value());
	QAction *compareAct = file->addAction(T("Compare with map..."), this, [this] { compareDialog(); });
	compareAct->setEnabled(m_state.map != nullptr);
	QAction *iniAct = file->addAction(T("Map INI files..."), this, [this] { showMapIni(); });
	iniAct->setEnabled(m_state.map != nullptr && !m_state.map->path.isEmpty());
	QMenu *exportMenu = file->addMenu(T("Export"));
	exportMenu->setEnabled(m_state.map != nullptr);
	exportMenu->addAction(T("Map as text..."), this, [this] {
		const QString path = QFileDialog::getSaveFileName(this, T("Export map as text"), QString(), T("Text (*.txt)"));
		QString error;
		if (!path.isEmpty() && !exportMapText(path, &error))
			toast(error);
	});
	exportMenu->addAction(T("Height map as a picture..."), this, [this] {
		const QString path = QFileDialog::getSaveFileName(this, T("Export height map"), QString(), T("Pictures (*.png)"));
		QString error;
		if (!path.isEmpty() && !exportHeightmap(path, &error))
			toast(error);
	});
	exportMenu->addAction(T("Picture of the map..."), this, [this] {
		const QString path = QFileDialog::getSaveFileName(this, T("Export picture of the map"), QString(), T("Pictures (*.png)"));
		QString error;
		if (!path.isEmpty() && !exportMapPicture(path, 4, &error))
			toast(error);
	});
	exportMenu->addAction(T("Poster of the 3D view..."), this, [this] { showPosterDialog(); });
	QAction *recoverAct = file->addAction(T("Recover autosave"), this, [this] { recoverAutosave(); });
	recoverAct->setEnabled(!autosaveInfo().file.isEmpty());
	file->addSeparator();
	file->addAction(T("Exit"), QKeySequence::Quit, this, [this] { this->close(); });

	// Undo and redo keep their shortcuts enabled; the texts follow the history when the menu opens.
	QMenu *edit = bar->addMenu(T("Edit"));
	// All key bindings of the platform (on Windows also Alt+Backspace, Ctrl+Ins, Shift+Ins, Shift+Del).
	QAction *undoAct = edit->addAction(T("Undo"), this, [this] { undo(); });
	undoAct->setShortcuts(QKeySequence::Undo);
	QAction *redoAct = edit->addAction(T("Redo"), this, [this] { redo(); });
	redoAct->setShortcuts(QKeySequence::Redo);
	connect(edit, &QMenu::aboutToShow, this, [this, undoAct, redoAct] {
		const MapEditorCore::History *h = m_state.map ? &m_state.map->history : nullptr;
		undoAct->setText(h && h->canUndo() ? T("Undo: %1").arg(stepName(h->undoName())) : T("Undo"));
		redoAct->setText(h && h->canRedo() ? T("Redo: %1").arg(stepName(h->redoName())) : T("Redo"));
		undoAct->setEnabled(h && h->canUndo());
		redoAct->setEnabled(h && h->canRedo());
	});
	connect(edit, &QMenu::aboutToHide, this, [undoAct, redoAct] {
		undoAct->setEnabled(true);
		redoAct->setEnabled(true);
	});
	edit->addAction(T("History..."), QKeySequence(QStringLiteral("Ctrl+H")), this, [this] { showHistory(); });
	edit->addAction(T("Find in the map..."), QKeySequence(QStringLiteral("Ctrl+Shift+F")), this, [this] { showSearch(); });
	edit->addAction(T("Where is this used?"), QKeySequence(QStringLiteral("Shift+F12")), this, [this] { whereUsed(); });
	edit->addSeparator();
	edit->addAction(T("Cut"), this, [this] { cutSelection(); })->setShortcuts(QKeySequence::Cut);
	edit->addAction(T("Copy"), this, [this] { copySelection(); })->setShortcuts(QKeySequence::Copy);
	edit->addAction(T("Paste"), this, [this] { paste(); })->setShortcuts(QKeySequence::Paste);
	edit->addAction(T("Duplicate"), QKeySequence(QStringLiteral("Ctrl+D")), this, [this] { duplicateSelection(); });
	edit->addAction(T("Link waypoints"), QKeySequence(QStringLiteral("Ctrl+L")), this, [this] { linkSelectedWaypoints(); });
	edit->addAction(T("Unlink waypoints"), QKeySequence(QStringLiteral("Ctrl+Shift+L")), this, [this] { unlinkSelectedWaypoints(); });
	edit->addAction(T("Delete"), QKeySequence::Delete, this, [this] { deleteSelection(); });
	edit->addSeparator();
	edit->addAction(T("Select all"), QKeySequence::SelectAll, this, [this] { selectAll(); });
	edit->addAction(T("Select none"), QKeySequence(QStringLiteral("Ctrl+Shift+A")), this, [this] { selectNone(); });
	edit->addAction(T("Group"), QKeySequence(QStringLiteral("Ctrl+G")), this, [this] { groupSelection(); });
	edit->addAction(T("Ungroup"), QKeySequence(QStringLiteral("Ctrl+Shift+G")), this, [this] { ungroupSelection(); });
	edit->addAction(T("Select similar"), QKeySequence(QStringLiteral("Ctrl+M")), this, [this] { selectSimilar(); });
	edit->addAction(T("Select duplicates"), this, [this] { selectDuplicates(); });
	edit->addAction(T("Select objects with invalid teams"), this, [this] { selectInvalidTeams(); });
	edit->addAction(T("Save selection as prefab..."), this, [this] { saveSelectionAsPrefab(); });
	edit->addAction(T("Delete prefab..."), this, [this] { deletePrefabDialog(); });
	QAction *checkAct = edit->addAction(T("Check map..."), QKeySequence(QStringLiteral("F7")), this, [this] { showProblems(); });
	edit->addAction(T("Multiplayer fairness..."), QKeySequence(QStringLiteral("Shift+F7")), this, [this] { showFairness(); });
	edit->addAction(T("Make symmetric..."), this, [this] { showSymmetry(); });
	checkAct->setEnabled(m_state.map != nullptr);
	// Pick filter by editor sorting, with the old WorldBuilder's keys.
	QMenu *pick = edit->addMenu(T("Pick only"));
	auto *pickGroup = new QActionGroup(pick);
	const std::pair<int, QString> filters[] = {{0, T("Anything")}, {1, T("Buildings")}, {2, T("Infantry")}, {3, T("Vehicles")},
		{4, T("Shrubbery")}, {5, T("Props")}, {6, T("Natural")}, {7, T("Debris")}, {13, T("Waypoints and areas")}, {12, T("Roads")},
		{9, T("Sounds")}};
	int key = 0;
	for (const auto &[sorting, text] : filters)
	{
		QAction *action = pick->addAction(text);
		action->setCheckable(true);
		action->setChecked(m_pickFilter == sorting);
		if (key <= 9)
			action->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(key)));
		++key;
		pickGroup->addAction(action);
		const int filter = sorting;
		connect(action, &QAction::triggered, this, [this, filter] { setPickFilter(filter); });
	}

	QMenu *view = bar->addMenu(T("View"));
	view->addAction(T("Start page"), this, [this] { showPage(Page::Start); });
	QAction *mapView = view->addAction(T("Map"), this, [this] { showPage(Page::Map); });
	mapView->setEnabled(m_state.map != nullptr);
	view->addSeparator();
	view->addAction(T("Dark theme"), this, [this] {
		m_state.theme = ThemeKind::Dark;
		m_state.save();
		QTimer::singleShot(0, this, [this] { applySettings(); });
	});
	view->addAction(T("Light theme"), this, [this] {
		m_state.theme = ThemeKind::Light;
		m_state.save();
		QTimer::singleShot(0, this, [this] { applySettings(); });
	});
	view->addSeparator();
	view->addAction(T("Layers..."), this, [this] { showLayers(); });
	view->addAction(T("Camera..."), this, [this] { showCameraWindow(); });
	view->addAction(T("Centre on selection"), QKeySequence(QStringLiteral("Ctrl+E")), this, [this] { centerOnSelection(); });
	QMenu *look = view->addMenu(T("Look"));
	look->addAction(T("North"), this, [this] { setCameraYaw(0.0f); });
	look->addAction(T("East"), this, [this] { setCameraYaw(90.0f); });
	look->addAction(T("South"), this, [this] { setCameraYaw(180.0f); });
	look->addAction(T("West"), this, [this] { setCameraYaw(-90.0f); });
	// What the views show, as the old editor's view menu.
	QMenu *show = view->addMenu(T("Show"));
	const auto toggle = [this, show](const QString &text, bool DisplayOptions::*flag, const QKeySequence &shortcut = QKeySequence()) {
		QAction *action = show->addAction(text);
		action->setCheckable(true);
		action->setChecked(m_state.display.*flag);
		action->setShortcut(shortcut);
		connect(action, &QAction::toggled, this, [this, flag](bool on) { setDisplay(flag, on); });
	};
	toggle(T("Models"), &DisplayOptions::models);
	toggle(T("Shadows"), &DisplayOptions::shadows);
	toggle(T("Clouds"), &DisplayOptions::clouds);
	toggle(T("Macro texture"), &DisplayOptions::macroTexture);
	toggle(T("Soft water edges"), &DisplayOptions::softWater);
	toggle(T("Wireframe"), &DisplayOptions::wireframe);
	toggle(T("Three-way blends in black"), &DisplayOptions::extraBlends);
	toggle(T("Ambient sounds and ranges"), &DisplayOptions::sounds);
	toggle(T("Bounding boxes"), &DisplayOptions::boundingBoxes);
	toggle(T("Sight ranges"), &DisplayOptions::sightRanges);
	toggle(T("Weapon ranges"), &DisplayOptions::weaponRanges);
	toggle(T("Highlight objects the game lacks"), &DisplayOptions::testArt);
	toggle(T("Garrisoned buildings"), &DisplayOptions::garrisoned);
	toggle(T("Letterbox"), &DisplayOptions::letterbox);
	toggle(T("Brush outline"), &DisplayOptions::brushOutline);
	show->addSeparator();
	toggle(T("Object icons (2D)"), &DisplayOptions::objectIcons);
	toggle(T("Labels (2D)"), &DisplayOptions::labels);
	toggle(T("Contour lines (2D)"), &DisplayOptions::contours, QKeySequence(QStringLiteral("Ctrl+R")));
	toggle(T("Slopes (coloured in the 2D map and the 3D view)"), &DisplayOptions::slopes);
	toggle(T("Grid (2D)"), &DisplayOptions::grid);
	show->addSeparator();
	show->addAction(T("Choose macro texture..."), this, [this] { chooseMacroTexture(); });
	show->addAction(T("Contour line options..."), this, [this] { contourOptions(); });
	show->addAction(T("Slope limit..."), this, [this] { slopeOptions(); });
	show->addAction(T("Reload textures"), this, [this] { reloadTextures(); });
	// The game's Direct3D 11 look by detail level; filled again whenever it opens.
	QMenu *graphics = view->addMenu(T("Graphics"));
	fillGraphicsMenu(graphics);
	connect(graphics, &QMenu::aboutToShow, this, [this, graphics] { fillGraphicsMenu(graphics); });
	edit->addSeparator();
	QAction *snap = edit->addAction(T("Snap to grid"));
	snap->setCheckable(true);
	snap->setChecked(m_state.display.snapToGrid);
	connect(snap, &QAction::toggled, this, [this](bool on) { setDisplay(&DisplayOptions::snapToGrid, on); });
	view->addAction(T("Settings"), this, [this] { showPage(Page::Settings); });

	QMenu *help = bar->addMenu(T("Help"));
	// Ctrl+Shift+P works everywhere through its own shortcut.
	help->addAction(T("Command palette...") + QStringLiteral("\tCtrl+Shift+P"), this, [this] { showCommands(); });
	help->addAction(T("Help") + QStringLiteral("\tF1"), this, [this] { showHelp(helpTopic()); });
	help->addAction(T("About the MapEditor"), this, [this] { showAbout(); });
	applyShortcuts(bar);
}

} // namespace MapEditorApp
