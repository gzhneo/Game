/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowProblems.cpp -- the problem list: the map check with a fix for each problem.
//
// The old WorldBuilder asked on loading for a replacement of every missing object template and
// fixed teams from a menu entry with a message box per case. Here the check runs after loading and
// after each change (a button in the status bar counts the problems); the list shows them all, a
// double-click shows the objects, team, player or script, and each fix is one undo step. "Apply
// suggestions" does what the old editor did by itself: beta names to their final names, duplicate
// teams away; plus duplicate objects and links to missing waypoints.

#include "App/MainWindow.h"
#include "App/Windows/SidesWindow.h"
#include "App/Windows/TextListModel.h"
#include "App/Windows/FairnessWindow.h"
#include "App/Windows/SearchWindow.h"
#include "App/MainWindowParts.h"

#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/Widgets.h"
#include "Core/ScriptExchange.h"
#include "Core/RoadCommands.h"
#include "Core/SelectionTools.h"
#include "Core/SidesCommands.h"
#include "Core/WaypointCommands.h"

#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QListView>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

namespace MapEditorApp
{

namespace
{

constexpr int CHECK_DELAY_MS = 400;

std::string toStd(const QString &s)
{
	return s.trimmed().toUtf8().toStdString();
}

QString playerLabel(const std::string &name)
{
	return name.empty() ? T("(neutral)") : utf8(name);
}

} // namespace

std::vector<MapEditorCore::Problem> MainWindow::mapProblems()
{
	if (!m_state.map)
		return {};
	EngineClient &engine = EngineClient::instance();
	MapEditorCore::MapCheckInput input;
	std::set<std::string> templates;
	std::set<std::string> factions;
	MapEditorCore::ScriptCheck check;
	if (engine.isStarted() && engine.startedGame() == m_state.game)
	{
		for (const MeTemplateInfo &info : engine.catalog())
			templates.insert(info.name);
		for (const MePlayerTemplateInfo &info : engine.factions())
			factions.insert(info.name);
		// A catalog that did not load says nothing about the map.
		if (!templates.empty())
			input.templateKnown = [&templates](const std::string &name) { return templates.count(name) != 0; };
		if (!factions.empty())
			input.factionKnown = [&factions](const std::string &name) { return factions.count(name) != 0; };
		check = m_scriptCatalog.check(&m_state.map->map());
		if (!m_scriptCatalog.templates().conditions.empty())
		{
			input.scriptTemplates = &m_scriptCatalog.templates();
			input.scriptCheck = &check;
		}
	}
	return MapEditorCore::checkMap(m_state.map->map(), input);
}

QString MainWindow::problemText(const MapEditorCore::Problem &p) const
{
	using MapEditorCore::ProblemKind;
	const QString subject = utf8(p.subject);
	switch (p.kind)
	{
	case ProblemKind::UnknownTemplate:
	{
		QStringList uses;
		if (!p.objects.empty())
			uses << T("%1 objects").arg(p.objects.size());
		if (p.buildEntries > 0)
			uses << T("%1 build list entries").arg(p.buildEntries);
		if (p.teamUnits > 0)
			uses << T("%1 team units").arg(p.teamUnits);
		return T("The game has no object %1 (%2)").arg(subject, uses.join(QStringLiteral(", ")));
	}
	case ProblemKind::ObjectTeam:
		return p.subject.empty() ? T("%1 objects have no team").arg(p.objects.size())
			: T("%1 objects are on team %2, which is missing or has no valid owner").arg(p.objects.size()).arg(subject);
	case ProblemKind::TeamOwner:
		return T("Team %1 belongs to no player").arg(subject);
	case ProblemKind::DuplicateTeam:
		return T("Team %1 exists twice").arg(subject);
	case ProblemKind::UnknownFaction:
		return T("The game has no faction %1").arg(subject);
	case ProblemKind::DuplicateObject:
		return T("%1 objects %2 lie on another of the same kind").arg(p.objects.size()).arg(subject);
	case ProblemKind::OffTerrain:
		return T("%1 objects lie outside the terrain").arg(p.objects.size());
	case ProblemKind::WaypointLink:
		return T("Waypoint links lead to waypoint %1, which does not exist").arg(subject);
	case ProblemKind::Script:
		return T("Script: %1").arg(subject);
	}
	return subject;
}

bool MainWindow::fixMap(const char *name, const std::function<int(MapModel::Map &)> &fix)
{
	if (!m_state.map)
		return false;
	MapModel::Map &map = m_state.map->map();
	const MapEditorCore::EditMapParts::Parts before = MapEditorCore::mapParts(map);
	fix(map);
	auto step = MapEditorCore::mapPartsStep(map, before, name);
	if (!step)
		return false;
	const MapEditorCore::Changes changes = step->changes();
	m_state.map->history.record(std::move(step));
	applyChanges(changes);
	return true;
}

int MainWindow::applySuggestions()
{
	using MapEditorCore::ProblemKind;
	const std::vector<MapEditorCore::Problem> problems = mapProblems();
	int fixed = 0;
	fixMap("Fix problems", [&problems, &fixed](MapModel::Map &map) {
		std::vector<std::int32_t> duplicates;
		std::vector<int> teams;
		for (const MapEditorCore::Problem &p : problems)
		{
			if (p.kind == ProblemKind::UnknownTemplate && p.suggestion)
				fixed += MapEditorCore::replaceTemplate(map, p.subject, *p.suggestion) > 0 ? 1 : 0;
			else if (p.kind == ProblemKind::UnknownFaction && p.suggestion && map.sides)
				fixed += MapEditorCore::setFaction(*map.sides, p.side, *p.suggestion);
			else if (p.kind == ProblemKind::DuplicateObject)
			{
				duplicates.insert(duplicates.end(), p.objects.begin(), p.objects.end());
				++fixed;
			}
			else if (p.kind == ProblemKind::DuplicateTeam)
				teams.push_back(p.team);
			else if (p.kind == ProblemKind::WaypointLink)
				fixed += MapEditorCore::removeWaypointLinks(map, p.waypoint) > 0 ? 1 : 0;
		}
		// Teams from the back, so the indices in front stay; objects last, with their links.
		std::sort(teams.rbegin(), teams.rend());
		for (const int team : teams)
			if (map.sides)
				fixed += MapEditorCore::removeTeamAt(*map.sides, team);
		MapEditorCore::removeObjects(map, duplicates);
		return fixed;
	});
	return fixed;
}

void MainWindow::showProblem(const MapEditorCore::Problem &p)
{
	using MapEditorCore::ProblemKind;
	switch (p.kind)
	{
	case ProblemKind::TeamOwner:
	case ProblemKind::DuplicateTeam:
		sidesWindow()->setTeam(p.team);
		showSides(1);
		return;
	case ProblemKind::UnknownFaction:
		sidesWindow()->setPlayer(p.side);
		showSides(0);
		return;
	case ProblemKind::Script:
		showScripts(&p.script);
		return;
	default:
		break;
	}
	if (p.objects.empty())
		return;
	m_state.map->selection.objects = p.objects;
	selectionChanged();
	centerOnSelection();
}

void MainWindow::scheduleProblemCheck()
{
	if (!m_problemsTimer)
	{
		m_problemsTimer = new QTimer(this);
		m_problemsTimer->setSingleShot(true);
		connect(m_problemsTimer, &QTimer::timeout, this, [this] { refreshProblems(); });
	}
	m_problemsTimer->start(CHECK_DELAY_MS);
}

void MainWindow::mapReplaced()
{
	m_problems.clear();
	refreshHistoryWindow();
	if (m_searchWindow)
		m_searchWindow->setMap(m_state.map ? &m_state.map->map() : nullptr);
	if (m_fairnessWindow)
		m_fairnessWindow->close();
	m_territoryTint.clear();
	if (m_overview)
		m_overview->setTint({});
	if (m_state.map)
		scheduleProblemCheck();
	else if (m_problemsWindow)
		m_problemsWindow->close();
}

void MainWindow::refreshProblems()
{
	m_problems = mapProblems();
	if (m_problemsButton)
	{
		m_problemsButton->setText(T("%1 problems").arg(m_problems.size()));
		m_problemsButton->setVisible(!m_problems.empty());
	}
	if (!m_problemsWindow)
		return;
	auto *view = child<QListView>(m_problemsWindow, "ProblemList");
	auto *head = child<QLabel>(m_problemsWindow, "ProblemCount");
	if (!view || !head || !m_problemsModel)
		return;
	const int current = std::max(0, view->currentIndex().row());
	QStringList texts;
	for (const MapEditorCore::Problem &p : m_problems)
		texts << problemText(p);
	m_problemsModel->setTexts(texts);
	head->setText(m_problems.empty() ? T("No problems found.") : T("%1 problems; a double-click shows where.").arg(m_problems.size()));
	const EngineClient &engine = EngineClient::instance();
	if (!engine.isStarted())
		head->setText(head->text() + QLatin1Char(' ') + T("Without the game data, object types, factions and scripts are not checked."));
	if (!m_problems.empty())
		view->setCurrentIndex(m_problemsModel->index(std::clamp(current, 0, static_cast<int>(m_problems.size()) - 1)));
	else
		fillProblemFix(-1);
}

void MainWindow::fillProblemFix(int index)
{
	using MapEditorCore::ProblemKind;
	if (!m_problemsWindow)
		return;
	auto *box = m_problemsWindow->findChild<QWidget *>(QStringLiteral("ProblemFix"));
	if (!box)
		return;
	// The fields of the previous problem go at once, so findChild sees only the new ones.
	for (QWidget *old : box->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
	{
		old->setParent(nullptr);
		old->deleteLater();
	}
	delete box->layout();
	auto *row = new QHBoxLayout(box);
	row->setContentsMargins(0, 0, 0, 0);
	if (index < 0 || index >= static_cast<int>(m_problems.size()) || !m_state.map)
	{
		row->addStretch(1);
		return;
	}
	const MapEditorCore::Problem p = m_problems[static_cast<std::size_t>(index)];
	const auto button = [this, box, row](const QString &text, const char *name, const std::function<void()> &action) {
		auto *b = new QPushButton(text, box);
		b->setObjectName(QString::fromLatin1(name));
		connect(b, &QPushButton::clicked, this, [this, action] {
			action();
			refreshProblems();
		});
		row->addWidget(b);
	};
	const auto players = [this, box]() {
		auto *combo = new QComboBox(box);
		combo->setObjectName(QStringLiteral("ProblemPlayer"));
		if (m_state.map->map().sides)
			for (const MapModel::Side &side : m_state.map->map().sides->sides)
				combo->addItem(playerLabel(MapEditorCore::playerName(side)), utf8(MapEditorCore::playerName(side)));
		return combo;
	};
	switch (p.kind)
	{
	case ProblemKind::UnknownTemplate:
	{
		auto *choice = new QComboBox(box);
		choice->setObjectName(QStringLiteral("ProblemTemplate"));
		for (const MeTemplateInfo &info : EngineClient::instance().catalog())
			choice->addItem(QString::fromUtf8(info.name));
		choice->setEditable(true);
		choice->setInsertPolicy(QComboBox::NoInsert);
		choice->completer()->setFilterMode(Qt::MatchContains);
		choice->completer()->setCompletionMode(QCompleter::PopupCompletion);
		choice->setCurrentText(p.suggestion ? utf8(*p.suggestion) : QString());
		row->addWidget(label(T("Replace with"), nullptr, box));
		row->addWidget(choice, 1);
		button(T("Replace"), "ProblemReplace", [this, p, choice] {
			const std::string to = toStd(choice->currentText());
			if (!to.empty())
				fixMap("Replace object type", [&p, &to](MapModel::Map &map) { return MapEditorCore::replaceTemplate(map, p.subject, to); });
		});
		button(T("Remove all"), "ProblemRemove", [this, p] {
			fixMap("Remove object type", [&p](MapModel::Map &map) { return MapEditorCore::removeTemplate(map, p.subject); });
		});
		break;
	}
	case ProblemKind::ObjectTeam:
	{
		QComboBox *combo = players();
		row->addWidget(label(T("Put on the team of"), nullptr, box));
		row->addWidget(combo, 1);
		button(T("Move"), "ProblemMove", [this, p, combo] {
			const std::string player = toStd(combo->currentData().toString());
			fixMap("Fix teams", [&p, &player](MapModel::Map &map) { return MapEditorCore::setObjectsTeam(map, p.objects, player); });
		});
		break;
	}
	case ProblemKind::TeamOwner:
	{
		QComboBox *combo = players();
		row->addWidget(label(T("Owner"), nullptr, box));
		row->addWidget(combo, 1);
		button(T("Set owner"), "ProblemOwner", [this, p, combo] {
			const std::string player = toStd(combo->currentData().toString());
			fixMap("Fix teams", [&p, &player](MapModel::Map &map) { return map.sides ? MapEditorCore::setTeamOwner(*map.sides, p.team, player) : 0; });
		});
		button(T("Remove team"), "ProblemRemoveTeam", [this, p] {
			fixMap("Fix teams", [&p](MapModel::Map &map) { return map.sides ? MapEditorCore::removeTeamAt(*map.sides, p.team) : 0; });
		});
		break;
	}
	case ProblemKind::DuplicateTeam:
		row->addStretch(1);
		button(T("Remove team"), "ProblemRemoveTeam", [this, p] {
			fixMap("Fix teams", [&p](MapModel::Map &map) { return map.sides ? MapEditorCore::removeTeamAt(*map.sides, p.team) : 0; });
		});
		break;
	case ProblemKind::UnknownFaction:
	{
		auto *combo = new QComboBox(box);
		combo->setObjectName(QStringLiteral("ProblemFaction"));
		for (const MePlayerTemplateInfo &info : EngineClient::instance().factions())
			combo->addItem(QString::fromUtf8(info.name));
		if (p.suggestion)
			combo->setCurrentText(utf8(*p.suggestion));
		row->addWidget(label(T("Faction"), nullptr, box));
		row->addWidget(combo, 1);
		button(T("Set faction"), "ProblemSetFaction", [this, p, combo] {
			const std::string faction = toStd(combo->currentText());
			fixMap("Set faction", [&p, &faction](MapModel::Map &map) { return map.sides ? MapEditorCore::setFaction(*map.sides, p.side, faction) : 0; });
		});
		break;
	}
	case ProblemKind::DuplicateObject:
	case ProblemKind::OffTerrain:
		row->addStretch(1);
		button(T("Select"), "ProblemSelect", [this, p] { showProblem(p); });
		button(T("Delete objects"), "ProblemDelete", [this, p] {
			fixMap("Delete objects", [&p](MapModel::Map &map) { return MapEditorCore::removeObjects(map, p.objects); });
		});
		break;
	case ProblemKind::WaypointLink:
		row->addStretch(1);
		button(T("Remove links"), "ProblemUnlink", [this, p] {
			fixMap("Unlink waypoints", [&p](MapModel::Map &map) { return MapEditorCore::removeWaypointLinks(map, p.waypoint); });
		});
		break;
	case ProblemKind::Script:
		row->addStretch(1);
		button(T("Show script"), "ProblemShowScript", [this, p] { showProblem(p); });
		break;
	}
}

void MainWindow::showProblems()
{
	if (!m_state.map)
		return;
	if (!m_problemsWindow)
	{
		auto *window = new QDialog(this);
		window->setObjectName(QStringLiteral("ProblemsWindow"));
		window->setWindowTitle(T("Check map"));
		window->resize(820, 460);
		auto *col = new QVBoxLayout(window);
		auto *head = label(QString(), "Hint", window);
		head->setObjectName(QStringLiteral("ProblemCount"));
		head->setWordWrap(true);
		col->addWidget(head);
		// Row n of the list is m_problems[n].
		m_problemsModel = new TextListModel(window);
		auto *view = new QListView(window);
		view->setObjectName(QStringLiteral("ProblemList"));
		view->setModel(m_problemsModel);
		view->setEditTriggers(QAbstractItemView::NoEditTriggers);
		col->addWidget(view, 1);
		connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &index) {
			fillProblemFix(index.isValid() ? index.row() : -1);
		});
		connect(view, &QListView::doubleClicked, this, [this](const QModelIndex &index) {
			if (index.row() >= 0 && index.row() < static_cast<int>(m_problems.size()))
				showProblem(m_problems[static_cast<std::size_t>(index.row())]);
		});
		auto *fix = new QWidget(window);
		fix->setObjectName(QStringLiteral("ProblemFix"));
		col->addWidget(fix);
		auto *hint = label(T("\"Apply suggestions\" renames old object types and factions to the ones the game has and removes duplicate teams, duplicate objects and links to missing waypoints, as one step."), "Hint", window);
		hint->setWordWrap(true);
		col->addWidget(hint);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, window);
		QPushButton *suggest = buttons->addButton(T("Apply suggestions"), QDialogButtonBox::ActionRole);
		suggest->setObjectName(QStringLiteral("ProblemSuggestions"));
		connect(suggest, &QPushButton::clicked, this, [this] {
			const int fixed = applySuggestions();
			refreshProblems();
			toast(fixed > 0 ? T("%1 problems fixed").arg(fixed) : T("Nothing to fix by suggestion."));
		});
		QPushButton *again = buttons->addButton(T("Check again"), QDialogButtonBox::ActionRole);
		connect(again, &QPushButton::clicked, this, [this] { refreshProblems(); });
		connect(buttons, &QDialogButtonBox::rejected, window, &QDialog::reject);
		col->addWidget(buttons);
		m_problemsWindow = window;
	}
	refreshProblems();
	m_problemsWindow->show();
	m_problemsWindow->raise();
}

} // namespace MapEditorApp
