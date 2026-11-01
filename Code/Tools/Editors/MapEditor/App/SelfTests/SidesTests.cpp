/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SidesTests.cpp -- the self test of MainWindowSides.cpp (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/Windows/SidesWindow.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/Widgets.h"
#include "Core/SidesCommands.h"
#include "Core/WaypointCommands.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <algorithm>
#include <functional>
#include <set>

namespace MapEditorApp
{

bool MainWindow::selfTestSides(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "sides");
	if (!m_state.map || !m_state.map->map().sides)
		return true;
	MapModel::Map &map = m_state.map->map();
	MapEditorCore::History &history = m_state.map->history;
	const std::size_t players = map.sides->sides.size();
	const std::size_t teams = map.sides->teams.size();
	sidesWindow()->setPlayer(0);
	sidesWindow()->setTeam(-1);
	showSides(0);
	QApplication::processEvents();
	auto *list = m_sidesWindow ? m_sidesWindow->findChild<QListWidget *>(QStringLiteral("SidesPlayers")) : nullptr;
	step(list && list->count() == static_cast<int>(players), QStringLiteral("the window lists %1 players").arg(players));

	// A new player with its default team.
	int added = -1;
	editSides("Add player", [&added](MapModel::Map &m) {
		added = MapEditorCore::addPlayer(*m.sides, "SelfTest", u"Self test", "FactionAmerica", true);
		return added >= 0;
	});
	list = m_sidesWindow->findChild<QListWidget *>(QStringLiteral("SidesPlayers"));
	step(added >= 0 && list && list->count() == static_cast<int>(players) + 1 && MapEditorCore::findTeam(*map.sides, "teamSelfTest") >= 0,
		QStringLiteral("a new player with its default team"));

	// Relation through the table of the window.
	sidesWindow()->setPlayer(added);
	m_sidesWindow->refresh();
	int other = -1;
	for (std::size_t i = 0; i < map.sides->sides.size() && other < 0; ++i)
		if (static_cast<int>(i) != added && !MapEditorCore::playerName(map.sides->sides[i]).empty())
			other = static_cast<int>(i);
	if (auto *relation = other >= 0 ? m_sidesWindow->findChild<QComboBox *>(QStringLiteral("SidesRelation%1").arg(other)) : nullptr)
	{
		relation->setCurrentIndex(2);
		emit relation->activated(2);
		step(MapEditorCore::relation(*map.sides, added, other) == MapEditorCore::Relation::Enemy
			&& MapEditorCore::relation(*map.sides, other, added) == MapEditorCore::Relation::Neutral,
			QStringLiteral("the new player regards %1 as enemy").arg(utf8(MapEditorCore::playerName(map.sides->sides[static_cast<std::size_t>(other)]))));
	}
	else
		step(other < 0, QStringLiteral("a relation to change"));

	// Display name: typing is one step.
	auto *display = m_sidesWindow->findChild<QLineEdit *>(QStringLiteral("SidesDisplayName"));
	const std::string undoBefore = history.undoName();
	if (display)
	{
		for (const QString &text : {QStringLiteral("S"), QStringLiteral("Se"), QStringLiteral("Sel")})
		{
			display->setText(text);
			emit display->textEdited(text);
		}
	}
	const auto shownName = [&map, added] {
		const auto *v = map.sides->sides[static_cast<std::size_t>(added)].properties.find("playerDisplayName");
		return v && std::holds_alternative<std::u16string>(*v) ? QString::fromStdU16String(std::get<std::u16string>(*v)) : QString();
	};
	const bool typed = display && shownName() == QStringLiteral("Sel") && history.undoName() == "Change display name";
	undo();
	step(typed && shownName() == QStringLiteral("Self test") && history.undoName() == undoBefore, QStringLiteral("typing a display name is one step"));
	redo();

	// A team: new, a value, the list follows.
	sidesWindow()->setTab(1);
	m_sidesWindow->refresh();
	const std::size_t teamsBeforeNew = map.sides->teams.size();
	auto *newTeam = m_sidesWindow->findChild<QPushButton *>(QStringLiteral("SidesTeamNew"));
	if (newTeam)
		newTeam->click();
	QApplication::processEvents();
	const int team = m_sidesWindow->team();
	step(team >= 0 && map.sides->teams.size() == teamsBeforeNew + 1 && MapEditorCore::teamOwner(map.sides->teams[static_cast<std::size_t>(team)]) == "SelfTest",
		QStringLiteral("a new team of the player"));
	auto *priority = m_sidesWindow->findChild<QSpinBox *>(QStringLiteral("Team_teamProductionPriority"));
	if (priority)
	{
		priority->setValue(5);
		priority->setValue(7);
	}
	step(team >= 0 && map.sides->teams[static_cast<std::size_t>(team)].getInt("teamProductionPriority") == 7 && history.undoName() == "Change team",
		QStringLiteral("the production priority is set"));
	auto *attitude = m_sidesWindow->findChild<QComboBox *>(QStringLiteral("Team_teamAggressiveness"));
	if (attitude)
	{
		attitude->setCurrentIndex(4);
		emit attitude->activated(4);
	}
	step(team >= 0 && map.sides->teams[static_cast<std::size_t>(team)].getInt("teamAggressiveness") == 2, QStringLiteral("the attitude is aggressive"));

	// Pictures of both pages for a look at the layout.
	m_sidesWindow->resize(1000, 760);
	sidesWindow()->setTab(1);
	m_sidesWindow->refresh();
	QApplication::processEvents();
	const QString teamShot = QDir(shotDir).filePath(QStringLiteral("sides_teams.png"));
	step(m_sidesWindow->grab().save(teamShot), QStringLiteral("picture %1").arg(teamShot));
	sidesWindow()->setTab(0);
	m_sidesWindow->refresh();
	QApplication::processEvents();
	const QString playerShot = QDir(shotDir).filePath(QStringLiteral("sides_players.png"));
	step(m_sidesWindow->grab().save(playerShot), QStringLiteral("picture %1").arg(playerShot));

	// Rename the player: its default team follows.
	const int self = MapEditorCore::findPlayer(*map.sides, "SelfTest");
	editSides("Rename player", [self](MapModel::Map &m) { return MapEditorCore::renamePlayer(m, self, "SelfTest2"); });
	step(MapEditorCore::findTeam(*map.sides, "teamSelfTest2") >= 0 && team >= 0
		&& MapEditorCore::teamOwner(map.sides->teams[static_cast<std::size_t>(team)]) == "SelfTest2", QStringLiteral("renaming the player renames its teams' owner"));

	// Members of the neutral default team.
	const int neutralTeam = MapEditorCore::findTeam(*map.sides, "team");
	selectTeamMembers(neutralTeam);
	step(neutralTeam < 0 || m_state.map->selection.objects.size() == MapEditorCore::teamObjects(map, "team").size(),
		QStringLiteral("%1 members of the neutral team selected").arg(m_state.map->selection.objects.size()));
	m_state.map->selection.clear();
	selectionChanged();

	while (history.isModified() && history.canUndo())
		undo();
	QApplication::processEvents();
	step(map.sides->sides.size() == players && map.sides->teams.size() == teams && !history.isModified(), QStringLiteral("undo all, players and teams as before"));
	if (m_sidesWindow)
		m_sidesWindow->hide();
	return step.ok();
}

} // namespace MapEditorApp
