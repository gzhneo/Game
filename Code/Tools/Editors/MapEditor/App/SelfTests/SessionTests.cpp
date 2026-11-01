/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SessionTests.cpp -- the self test of MainWindowSession.cpp (MapEditor --selftest).

#include "App/Common.h"
#include "App/MainWindow.h"
#include "App/MainWindowParts.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "Core/GameLaunch.h"
#include "Model/MapFile.h"
#include <QDir>
#include <QFile>
#include <filesystem>

namespace MapEditorApp
{

bool MainWindow::selfTestSession(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "session");
	if (!m_state.map)
		return true;
	MapModel::Map &map = m_state.map->map();
	MapEditorCore::History &history = m_state.map->history;
	m_autosaveFolder = QDir(shotDir).filePath(QStringLiteral("autosave"));
	QDir(m_autosaveFolder).removeRecursively();

	// Nothing to save while the map is as loaded.
	step(!autosaveNow(), QStringLiteral("no autosave without changes"));
	// A change: the world info name.
	editSettings("Map name", [](MapModel::Map &m) {
		if (!m.worldInfo)
			m.worldInfo = MapModel::PropertyMap{};
		m.worldInfo->set("mapName", MapModel::PropertyValue(std::string("AutosaveTest")));
	});
	const bool first = autosaveNow();
	const bool again = autosaveNow();
	editSettings("Map name", [](MapModel::Map &m) { m.worldInfo->set("mapName", MapModel::PropertyValue(std::string("AutosaveTest2"))); });
	const bool second = autosaveNow();
	const MapModel::LoadedMap saved = MapModel::loadMap(toFsPath(autosaveFile(m_autosaveFolder, 1)));
	step(first && !again && second && QFile::exists(autosaveFile(m_autosaveFolder, 2)) && saved.result.ok()
		&& MapModel::sameContent(saved.result.map, map), QStringLiteral("autosave after a change, the older one kept, same content"));
	AutosaveInfo info = autosaveInfo();
	step(info.pending && info.file == autosaveFile(m_autosaveFolder, 1), QStringLiteral("the note says there is unsaved work"));
	markAutosaveClean();
	info = autosaveInfo();
	step(!info.pending, QStringLiteral("saving or closing clears the note"));

	// Test in game: the command line, without starting anything.
	QString program;
	QString mapFile;
	QString error;
	const QString savedPath = m_state.map->path;
	m_state.map->path = QDir(shotDir).filePath(QStringLiteral("Folder/Folder.map"));
	const QStringList args = gameLaunch(&program, &mapFile, &error);
	const bool expected = program.isEmpty() ? args.isEmpty() && !error.isEmpty()
		: args.size() == 3 && args[0] == QStringLiteral("-win") && args[1] == QStringLiteral("-file") && args[2].endsWith(QStringLiteral("Folder.map"))
			&& !args[2].contains(QStringLiteral("Folder\\Folder"));
	step(expected, program.isEmpty() ? QStringLiteral("no game program: %1").arg(error) : QStringLiteral("%1 %2").arg(program, args.join(QLatin1Char(' '))));
	m_state.map->path = savedPath;

	while (history.isModified() && history.canUndo())
		undo();
	QDir(m_autosaveFolder).removeRecursively();
	m_autosaveFolder.clear();
	step(!history.isModified(), QStringLiteral("undo all"));
	return step.ok();
}

} // namespace MapEditorApp
