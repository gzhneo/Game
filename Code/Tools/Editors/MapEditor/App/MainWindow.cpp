/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindow.cpp -- the editor window.

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

#include <QApplication>
#include <QFileInfo>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
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

MainWindow::MainWindow(AppState &state, QApplication &app) : m_state(state), m_app(app)
{
	setMinimumSize(1100, 700);
	resize(1440, 900);
	auto *palette = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")), this);
	connect(palette, &QShortcut::activated, this, [this] { showCommands(); });
	// F1 in every window of the editor: the help of the window or tool in use.
	auto *help = new QShortcut(QKeySequence(Qt::Key_F1), this);
	help->setContext(Qt::ApplicationShortcut);
	connect(help, &QShortcut::activated, this, [this] { showHelp(helpTopic()); });
	m_viewport = new Viewport3D(this);
	m_viewport->onHover = [this](bool valid, float x, float y, float z) {
		// Remembered for pasting at the cursor.
		m_hoverValid = valid;
		m_hoverX = x;
		m_hoverY = y;
		brushMoved(valid, x, y);
		if (!m_statusLeft)
			return;
		m_statusLeft->setText(valid ? T("World %1, %2   Height %3").arg(static_cast<double>(x), 0, 'f', 0).arg(static_cast<double>(y), 0, 'f', 0).arg(static_cast<double>(z), 0, 'f', 1) : QString());
	};
	m_viewport->onClickObject = [this](int object, bool toggle, bool released) { clickObject(object, toggle, released); };
	m_viewport->onSelectRect = [this](const std::vector<int> &objects, bool add) { selectRect(objects, add); };
	m_viewport->onDragSelection = [this](float dx, float dy) { dragSelection(dx, dy); };
	m_viewport->onDragSelectionEnd = [this](bool cancel) { endDragSelection(cancel); };
	m_viewport->onRotateSelection = [this](float startX, float startY, float x, float y, bool snap) {
		rotateSelection(startX, startY, x, y, snap);
	};
	m_viewport->onRotateSelectionEnd = [this](bool cancel) { endRotateSelection(cancel); };
	m_viewport->onEscape = [this] { escape(); };
	m_viewport->onPlace = [this](float x, float y, float angle, bool aimed, int object, bool ctrl) { placeObject(x, y, angle, aimed, object, ctrl); };
	m_viewport->onTool = [this](const ToolEvent &e) { toolEvent(e); };
	m_viewport->onToolKey = [this](int key) { toolKey(key); };
	applySettings();
	startAutosaveTimer();
}

void MainWindow::applySettings()
{
	setLanguage(m_state.language);
	applyTheme(m_app, m_state.theme, m_state.game);
#ifdef _WIN32
	// Dark title bar to match the theme (Windows 10 2004 and later).
	const BOOL dark = m_state.theme == ThemeKind::Dark ? TRUE : FALSE;
	DwmSetWindowAttribute(reinterpret_cast<HWND>(winId()), 20, &dark, sizeof(dark));
#endif
	rebuild();
}

void MainWindow::rebuild()
{
	const Tokens t = tokens(m_state.theme, m_state.game);
	if (m_mold.active && !(terrainTool() && m_terrainTool == TerrainTool::Shape && m_shapeTool == ShapeTool::Mold))
		finishMold(false);
	m_shapeDrag = ShapeDrag{};
	if (m_viewport)
		m_viewport->trackSlot(nullptr);
	setMenuWidget(buildTopBar());

	m_pages = new QStackedWidget;
	m_pages->setObjectName(QStringLiteral("Pages"));
	m_pages->addWidget(buildStartPage());
	m_pages->addWidget(m_state.map ? buildMapPage() : new QWidget);
	m_pages->addWidget(buildSettingsPage());
	setCentralWidget(m_pages);

	auto *bar = new QStatusBar;
	bar->setSizeGripEnabled(false);
	m_statusLeft = label(QString(), nullptr);
	m_statusRight = label(QString(), nullptr);
	bar->addWidget(m_statusLeft, 1);
	m_problemsButton = new QPushButton(T("%1 problems").arg(m_problems.size()));
	m_problemsButton->setObjectName(QStringLiteral("ProblemsButton"));
	m_problemsButton->setFlat(true);
	m_problemsButton->setVisible(m_state.map && !m_problems.empty());
	connect(m_problemsButton, &QPushButton::clicked, this, [this] { showProblems(); });
	bar->addPermanentWidget(m_problemsButton);
	bar->addPermanentWidget(m_statusRight);
	setStatusBar(bar);
	updateSelectionStatus();

	if (m_page == Page::Map && !m_state.map)
		m_page = Page::Start;
	m_pages->setCurrentIndex(static_cast<int>(m_page));
	updateTitle();
	updateImpassableView();
	(void)t;
}

void MainWindow::showPage(Page page)
{
	if (page == Page::Map && !m_state.map)
		page = Page::Start;
	m_page = page;
	if (page == Page::Start)
	{
		// Recent maps and previews may have changed.
		QTimer::singleShot(0, this, [this] { rebuild(); });
		return;
	}
	if (m_pages)
		m_pages->setCurrentIndex(static_cast<int>(page));
	updateTitle();
}

void MainWindow::updateTitle()
{
	QString title = QStringLiteral("MapEditor");
	if (m_state.map && m_page == Page::Map)
	{
		const QString name = m_state.map->path.isEmpty() ? T("Untitled") : QFileInfo(m_state.map->path).completeBaseName();
		title = name + (m_state.map->history.isModified() ? QStringLiteral("*") : QString()) + QStringLiteral(" \u2013 ") + title;
	}
	setWindowTitle(title + QStringLiteral(" \u2013 ") + gameName(m_state.game));
}

} // namespace MapEditorApp
