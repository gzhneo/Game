/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowHelp.cpp -- F1 opens the built-in help (Windows/HelpWindow) at the topic of the window or tool
// in use.

#include "App/MainWindow.h"

#include "App/Windows/HelpWindow.h"

#include <QApplication>

namespace MapEditorApp
{

QString MainWindow::helpTopic() const
{
	// A window of the editor in front has its own topic.
	if (const QWidget *window = QApplication::activeWindow(); window && window != this)
	{
		static const std::pair<const char *, const char *> WINDOWS[] = {{"ScriptsWindow", "scripts"}, {"SidesWindow", "sides"},
			{"ParticlesWindow", "particles"}, {"ProblemsWindow", "problems"}, {"HistoryWindow", "history"}, {"SearchWindow", "search"},
			{"FairnessWindow", "fairness"}, {"SymmetryDialog", "symmetry"}, {"PosterDialog", "poster"}, {"LayersWindow", "layers"},
			{"MapSettingsWindow", "settings"}, {"CameraWindow", "view"}, {"ShortcutsWindow", "shortcuts"}};
		for (const auto &[name, topic] : WINDOWS)
			if (window->objectName() == QLatin1String(name))
				return QString::fromLatin1(topic);
	}
	if (m_page != Page::Map)
		return m_page == Page::Settings ? QStringLiteral("shortcuts") : QStringLiteral("start");
	if (terrainTool())
	{
		switch (m_terrainTool)
		{
			case TerrainTool::Height: return QStringLiteral("terrain-height");
			case TerrainTool::Texture: return QStringLiteral("terrain-texture");
			case TerrainTool::Shape: return QStringLiteral("terrain-shape");
			case TerrainTool::Pathing: return QStringLiteral("terrain-pathing");
		}
	}
	if (m_workMode == WorkMode::Objects)
	{
		if (areaTool())
			return QStringLiteral("areas");
		if (roadTool())
			return QStringLiteral("roads");
		if (placementTool())
			return QStringLiteral("placement");
		if (m_placeTemplate == QLatin1String(BUILD_LIST_TOOL))
			return QStringLiteral("buildlist");
		if (m_placeTemplate == QLatin1String(BORDER_TOOL))
			return QStringLiteral("settings");
		if (m_placeTemplate.contains(QLatin1String("Waypoint")))
			return QStringLiteral("waypoints");
		return QStringLiteral("objects");
	}
	return QStringLiteral("view");
}

void MainWindow::showHelp(const QString &topic)
{
	if (!m_helpWindow)
		m_helpWindow = new HelpWindow(this);
	m_helpWindow->showTopic(m_state.language, topic);
}

} // namespace MapEditorApp
