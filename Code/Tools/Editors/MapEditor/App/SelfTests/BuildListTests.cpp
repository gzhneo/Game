/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// BuildListTests.cpp -- the self test of MainWindowBuildList.cpp (MapEditor --selftest).

#include "App/Common.h"
#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/ToolEvent.h"
#include "App/Widgets.h"
#include "Core/BuildListCommands.h"
#include "Core/SidesCommands.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QProgressBar>
#include <algorithm>
#include <cmath>

namespace MapEditorApp
{

bool MainWindow::selfTestBuildList(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "build list");
	if (!m_state.map || !m_state.map->map().sides || !m_state.map->map().heightMap)
		return true;
	MapModel::Map &map = m_state.map->map();
	MapEditorCore::History &history = m_state.map->history;
	// A player for the list, added if the map has none.
	int side = -1;
	for (std::size_t i = 0; i < map.sides->sides.size() && side < 0; ++i)
		if (!MapEditorCore::playerName(map.sides->sides[i]).empty())
			side = static_cast<int>(i);
	if (side < 0)
	{
		editSides("Add player", [&side](MapModel::Map &m) {
			side = MapEditorCore::addPlayer(*m.sides, "BuildTest", u"Build test", "FactionAmerica", false);
			return side >= 0;
		});
	}
	QString structure;
	for (const MeTemplateInfo &info : EngineClient::instance().catalog())
		if (structure.isEmpty() && info.sorting == 1)
			structure = QString::fromLatin1(info.name);
	if (structure.isEmpty() && !EngineClient::instance().catalog().empty())
		structure = QString::fromLatin1(EngineClient::instance().catalog().front().name);
	if (side < 0 || structure.isEmpty())
	{
		step(false, QStringLiteral("a player and a structure to work with"));
		return step.ok();
	}
	m_show3D = false;
	m_workMode = WorkMode::Objects;
	rebuild();
	QApplication::processEvents();
	setPlacingTemplate(QString::fromLatin1(BUILD_LIST_TOOL), 0.0f);
	m_buildSide = side;
	m_buildEntry = -1;
	m_buildTemplate = structure;
	fillSelectionPanel();
	QApplication::processEvents();
	const std::size_t before = map.sides->sides[static_cast<std::size_t>(side)].buildList.size();
	const MapModel::HeightMap &hm = *map.heightMap;
	const float cx = static_cast<float>(hm.width - 2 * hm.borderSize) * CELL / 2.0f;
	const float cy = static_cast<float>(hm.height - 2 * hm.borderSize) * CELL / 2.0f;
	const auto send = [this](ToolEvent::Kind kind, float x, float y) {
		ToolEvent e;
		e.kind = kind;
		e.x = x;
		e.y = y;
		e.tolerance = 4.0f;
		toolEvent(e);
	};
	// Add with a drag to the east: faces east.
	send(ToolEvent::Kind::Press, cx, cy);
	send(ToolEvent::Kind::Move, cx + 60.0f, cy);
	send(ToolEvent::Kind::Release, cx + 60.0f, cy);
	const std::vector<MapModel::BuildListEntry> &list = map.sides->sides[static_cast<std::size_t>(side)].buildList;
	step(list.size() == before + 1 && list.back().templateName == ascii(structure) && std::abs(list.back().angle) < 0.01f
		&& history.undoName() == "Add to build list", QStringLiteral("a click adds %1 to the list").arg(structure));
	// Move it by dragging.
	send(ToolEvent::Kind::Press, cx + 2.0f, cy + 2.0f);
	send(ToolEvent::Kind::Move, cx + 42.0f, cy + 32.0f);
	send(ToolEvent::Kind::Release, cx + 42.0f, cy + 32.0f);
	step(!list.empty() && std::abs(list.back().x - (cx + 40.0f)) < 0.01f && std::abs(list.back().y - (cy + 30.0f)) < 0.01f
		&& history.undoName() == "Move build list entry", QStringLiteral("dragging moves the entry"));
	// Turn it on its ring.
	const float ex = list.empty() ? cx : list.back().x;
	const float ey = list.empty() ? cy : list.back().y;
	send(ToolEvent::Kind::Press, ex, ey + 10.0f);
	send(ToolEvent::Kind::Move, ex, ey + 40.0f);
	send(ToolEvent::Kind::Release, ex, ey + 40.0f);
	step(!list.empty() && std::abs(list.back().angle - PI / 2.0f) < 0.01f && history.undoName() == "Turn build list entry",
		QStringLiteral("the ring turns it to the north"));
	// Values in the panel.
	QApplication::processEvents();
	if (auto *rebuilds = findChild<QComboBox *>(QStringLiteral("BuildListRebuilds")))
	{
		const int row = rebuilds->findData(MapEditorCore::UNLIMITED_REBUILDS);
		rebuilds->setCurrentIndex(row);
		emit rebuilds->activated(row);
	}
	QApplication::processEvents();
	if (auto *built = findChild<QCheckBox *>(QStringLiteral("BuildListBuilt")))
		built->setChecked(true);
	QApplication::processEvents();
	step(!list.empty() && list.back().numRebuilds == MapEditorCore::UNLIMITED_REBUILDS && list.back().initiallyBuilt,
		QStringLiteral("unlimited rebuilds, built at the start"));
	auto *power = findChild<QProgressBar *>(QStringLiteral("BuildListPower"));
	step(power != nullptr, QStringLiteral("power bar: %1").arg(power ? power->text() : QString()));
	// The engine's copy draws the entry.
	const MapModel::WriteResult bytes = engineMapBytes();
	const MapModel::ReadResult copy = bytes.ok() ? MapModel::readMap(bytes.bytes) : MapModel::ReadResult{};
	std::size_t entries = 0;
	for (const MapModel::Side &s : map.sides->sides)
		entries += s.buildList.size();
	step(copy.ok() && copy.map.objects && copy.map.objects->size() == (map.objects ? map.objects->size() : 0) + entries,
		QStringLiteral("the 3D view gets %1 build list entries").arg(entries));
	// Skirmish export.
	const QString ini = QDir(shotDir).filePath(QStringLiteral("selftest_buildlist.ini"));
	QString error;
	const bool exported = exportBuildListIni(ini, side, &error);
	QFile in(ini);
	const QString content = exported && in.open(QIODevice::ReadOnly) ? QString::fromLatin1(in.readAll()) : QString();
	in.close();
	step(content.contains(QStringLiteral("Structure ") + structure) && content.contains(QStringLiteral("Rebuilds = -1")),
		QStringLiteral("skirmish export %1").arg(error));
	QFile::remove(ini);
	const QString shot = QDir(shotDir).filePath(QStringLiteral("buildlist.png"));
	if (m_overview)
		step(m_overview->grab().save(shot), QStringLiteral("picture %1").arg(shot));

	setPlacingTemplate(QString(), 0.0f);
	while (history.isModified() && history.canUndo())
		undo();
	QApplication::processEvents();
	step(map.sides->sides.size() > static_cast<std::size_t>(side) ? map.sides->sides[static_cast<std::size_t>(side)].buildList.size() == before && !history.isModified() : !history.isModified(),
		QStringLiteral("undo all, build list as before"));
	m_show3D = true;
	rebuild();
	QApplication::processEvents();
	return step.ok();
}

} // namespace MapEditorApp
