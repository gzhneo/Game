/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ProblemsTests.cpp -- the self test of MainWindowProblems.cpp (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/MainWindowParts.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/Widgets.h"
#include "Core/ScriptExchange.h"
#include "Core/RoadCommands.h"
#include "Core/SelectionTools.h"
#include "Core/SidesCommands.h"
#include "Core/WaypointCommands.h"
#include <QElapsedTimer>
#include <QPushButton>
#include <QListView>
#include <algorithm>
#include <set>

namespace MapEditorApp
{

bool MainWindow::selfTestProblems(QStringList &report)
{
	using MapEditorCore::ProblemKind;
	TestSteps step(report, "problems");
	if (!m_state.map || !m_state.map->map().objects || !m_state.map->map().sides)
		return true;
	MapModel::Map &map = m_state.map->map();
	MapEditorCore::History &history = m_state.map->history;
	QElapsedTimer clock;
	clock.start();
	const std::vector<MapEditorCore::Problem> before = mapProblems();
	report << QStringLiteral("info problems: one check takes %1 ms").arg(clock.elapsed());
	QStringList kinds;
	for (const MapEditorCore::Problem &p : before)
		kinds << problemText(p);
	report << QStringLiteral("info problems: the map has %1: %2").arg(before.size()).arg(kinds.mid(0, 8).join(QStringLiteral("; ")));

	// Two objects of a type the game has, not lying on others, first put on the neutral team.
	const std::vector<std::int32_t> duplicates = MapEditorCore::findDuplicates(map);
	std::vector<int> candidates;
	for (std::size_t i = 0; i < map.objects->size() && candidates.size() < 2; ++i)
	{
		const MapModel::MapObject &o = (*map.objects)[i];
		const bool thing = !o.templateName.empty() && o.templateName[0] != '*' && !MapEditorCore::isWaypoint(o) && !MapEditorCore::isRoadOrBridgePoint(o);
		const bool unknown = std::any_of(before.begin(), before.end(), [&o](const MapEditorCore::Problem &p) {
			return p.kind == ProblemKind::UnknownTemplate && p.subject == o.templateName;
		});
		if (thing && !unknown && std::find(duplicates.begin(), duplicates.end(), static_cast<std::int32_t>(i)) == duplicates.end())
			candidates.push_back(static_cast<int>(i));
	}
	if (candidates.size() < 2)
	{
		report << QStringLiteral("info problems: no two objects to test with");
		return step.ok();
	}
	const int index = candidates[0];
	const int other = candidates[1];
	const std::string type = (*map.objects)[static_cast<std::size_t>(index)].templateName;
	const std::string otherTeam = (*map.objects)[static_cast<std::size_t>(other)].properties.getAscii("originalOwner").value_or("");
	fixMap("Fix teams", [index, other](MapModel::Map &m) {
		// A map without players gets the neutral one with its team.
		MapEditorCore::validateSides(*m.sides);
		for (const int i : {index, other})
			(*m.objects)[static_cast<std::size_t>(i)].properties.set("originalOwner", MapModel::PropertyValue(std::string("team")));
		return 2;
	});
	const bool engine = EngineClient::instance().isStarted() && !EngineClient::instance().catalog().empty();
	// A beta name ("GC_<type>") and a missing team.
	fixMap("Fix teams", [index, &type](MapModel::Map &m) {
		(*m.objects)[static_cast<std::size_t>(index)].templateName = "GC_" + type;
		return 1;
	});
	fixMap("Fix teams", [other](MapModel::Map &m) {
		(*m.objects)[static_cast<std::size_t>(other)].properties.set("originalOwner", MapModel::PropertyValue(std::string("teamSelfTestMissing")));
		return 1;
	});
	showProblems();
	const auto has = [this](ProblemKind kind, const std::string &subject) {
		return std::any_of(m_problems.begin(), m_problems.end(), [&](const MapEditorCore::Problem &p) { return p.kind == kind && p.subject == subject; });
	};
	auto *view = child<QListView>(m_problemsWindow, "ProblemList");
	step(view && view->model()->rowCount() == static_cast<int>(m_problems.size()) && has(ProblemKind::ObjectTeam, "teamSelfTestMissing")
			&& (!engine || has(ProblemKind::UnknownTemplate, "GC_" + type)),
		QStringLiteral("the list shows a missing team%1").arg(engine ? QStringLiteral(" and a missing object type") : QString()));
	step(m_problemsButton && m_problemsButton->isVisible(), QStringLiteral("the status bar counts the problems"));

	// The missing team: the row's fix puts the object on the neutral team.
	for (int i = 0; view && i < static_cast<int>(m_problems.size()); ++i)
		if (m_problems[static_cast<std::size_t>(i)].subject == "teamSelfTestMissing")
			view->setCurrentIndex(view->model()->index(i, 0));
	auto *move = m_problemsWindow ? m_problemsWindow->findChild<QPushButton *>(QStringLiteral("ProblemMove")) : nullptr;
	if (move)
		move->click();
	step(move && (*map.objects)[static_cast<std::size_t>(other)].properties.getAscii("originalOwner") == std::string("team")
		&& !has(ProblemKind::ObjectTeam, "teamSelfTestMissing") && history.undoName() == "Fix teams", QStringLiteral("fix: the object goes to the neutral team"));
	if (engine)
	{
		const int fixed = applySuggestions();
		refreshProblems();
		step(fixed >= 1 && (*map.objects)[static_cast<std::size_t>(index)].templateName == type && !has(ProblemKind::UnknownTemplate, "GC_" + type),
			QStringLiteral("apply suggestions: GC_%1 becomes %1").arg(utf8(type)));
	}
	while (history.isModified() && history.canUndo())
		undo();
	refreshProblems();
	step(!history.isModified() && m_problems.size() == before.size()
			&& (*map.objects)[static_cast<std::size_t>(other)].properties.getAscii("originalOwner").value_or("") == otherTeam,
		QStringLiteral("undo all, the same problems as before"));
	m_problemsWindow->close();
	return step.ok();
}

} // namespace MapEditorApp
