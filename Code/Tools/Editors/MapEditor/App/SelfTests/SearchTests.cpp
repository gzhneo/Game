/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SearchTests.cpp -- the self test of the search window (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "App/Windows/SearchWindow.h"
#include "Core/References.h"
#include "Core/ScriptCommands.h"


#include <algorithm>

namespace MapEditorApp
{

bool MainWindow::selfTestSearch(QStringList &report)
{
	TestSteps step(report, "search");
	if (!m_state.map || !m_state.map->map().objects || m_state.map->map().objects->empty() || !m_state.map->map().sides
		|| m_state.map->map().sides->sides.empty())
		return true;
	const MapModel::Map &map = m_state.map->map();
	// "Where is this used?" on the first object finds at least that object.
	m_state.map->selection.set(0);
	selectionChanged();
	whereUsed();
	const QString name = selectionName();
	const std::vector<MapEditorCore::Reference> &hits = m_searchWindow->hits();
	const bool found = std::any_of(hits.begin(), hits.end(),
		[](const MapEditorCore::Reference &r) { return r.kind == MapEditorCore::Reference::Kind::Object && r.index == 0; });
	step(found && m_searchWindow->view()->model()->rowCount() == static_cast<int>(hits.size()),
		QStringLiteral("\"where is this used?\" for %1 lists %2 places, the object among them").arg(name).arg(hits.size()));

	// A script with a marked comment: found, replaced in one undo step, undone.
	const std::size_t undoBefore = m_state.map->history.undoCount();
	MapModel::Script script = MapEditorCore::newScript(m_scriptCatalog.templates(), "SearchTest");
	script.comment = "sends Team MapEditorSearchProbe";
	editScripts("Add script", [&script](MapModel::Sides &s) {
		MapEditorCore::insertScript(s, MapEditorCore::ScriptPath{0, -1, -1}, script);
		return true;
	});
	showSearch(QStringLiteral("MapEditorSearchProbe"), false);
	const std::size_t probeHits = m_searchWindow->hits().size();
	const int replaced = replaceInScripts(QStringLiteral("MapEditorSearchProbe"), QStringLiteral("MapEditorSearchDone"), false, true);
	MapEditorCore::SearchOptions exact;
	exact.matchCase = true;
	const bool gone = MapEditorCore::findReferences(map, "MapEditorSearchProbe", exact).empty();
	const bool there = !MapEditorCore::findReferences(map, "MapEditorSearchDone", exact).empty();
	step(probeHits == 1 && replaced == 1 && gone && there && m_state.map->history.undoCount() == undoBefore + 2,
		QStringLiteral("a text in a script comment is found and replaced in one undo step (%1 found, %2 replaced)").arg(probeHits).arg(replaced));
	undo();
	undo();
	step(m_state.map->history.undoCount() == undoBefore && MapEditorCore::findReferences(map, "MapEditorSearch", exact).empty(),
		QStringLiteral("undo takes the replacement and the script back"));
	// The open window follows the map: after the undo the probe is no longer listed.
	step(m_searchWindow->hits().empty(), QStringLiteral("the hits follow the map (%1 left after the undo)").arg(m_searchWindow->hits().size()));
	m_searchWindow->close();
	return step.ok();
}

} // namespace MapEditorApp
