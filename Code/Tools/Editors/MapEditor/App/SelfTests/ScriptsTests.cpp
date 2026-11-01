/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ScriptsTests.cpp -- the self test of MainWindowScripts.cpp (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/Windows/ScriptsWindow.h"
#include "App/MainWindowParts.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/RoadCommands.h"
#include "Core/ScriptCommands.h"
#include "Core/ScriptExchange.h"
#include "Core/SidesCommands.h"
#include "Core/WaypointCommands.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <algorithm>
#include <climits>
#include <functional>
#include <map>
#include <memory>
#include <set>

namespace MapEditorApp
{

bool MainWindow::selfTestScripts(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "scripts");
	if (!m_state.map || !m_state.map->map().sides)
		return true;
	MapModel::Map &map = m_state.map->map();
	MapEditorCore::History &history = m_state.map->history;
	const MapEditorCore::ScriptTemplates &templates = m_scriptCatalog.templates();
	if (templates.conditions.empty())
	{
		step(false, QStringLiteral("no script templates from the engine"));
		return step.ok();
	}
	step(templates.find(false, "CONDITION_TRUE") >= 0 && templates.find(true, "NO_OP") >= 0,
		QStringLiteral("%1 conditions and %2 actions from the engine").arg(templates.conditions.size()).arg(templates.actions.size()));

	// Every call of the map is known to the engine, and its sentence has no gaps.
	int calls = 0;
	int unknown = 0;
	int gaps = 0;
	MapEditorCore::forEachScript(*map.sides, [&](const MapEditorCore::ScriptPath &, const MapModel::Script &s) {
		const auto visit = [&](const MapModel::ScriptCall &c, bool action) {
			++calls;
			const MapEditorCore::ScriptTemplate *t = MapEditorCore::callTemplate(templates, action, c);
			if (!t)
				++unknown;
			else if (MapEditorCore::callText(c, t, m_scriptCatalog.valueNames()).find("???") != std::string::npos)
				++gaps;
		};
		for (const MapModel::OrCondition &o : s.conditions)
			for (const MapModel::ScriptCall &c : o.conditions)
				visit(c, false);
		for (const MapModel::ScriptCall &a : s.actions)
			visit(a, true);
		for (const MapModel::ScriptCall &a : s.falseActions)
			visit(a, true);
	});
	step(unknown == 0, QStringLiteral("%1 conditions and actions of the map, %2 unknown to the engine").arg(calls).arg(unknown));
	report << QStringLiteral("info scripts: %1 sentences with an empty or unnamed value").arg(gaps);

	const MapEditorCore::ScriptPath first{0, -1, -1};
	showScripts(&first);
	ScriptsWindow *window = scriptsWindow();
	QApplication::processEvents();
	auto *tree = window->findChild<QTreeWidget *>(QStringLiteral("ScriptsTree"));
	step(tree && tree->topLevelItemCount() == static_cast<int>(map.sides->sides.size()), QStringLiteral("the tree lists the players"));

	// A new script: always true, doing nothing.
	std::size_t before = 0;
	MapEditorCore::forEachScript(*map.sides, [&before](const MapEditorCore::ScriptPath &, const MapModel::Script &) { ++before; });
	if (auto *add = window->findChild<QPushButton *>(QStringLiteral("ScriptNew")))
		add->click();
	QApplication::processEvents();
	const MapModel::Script *added = MapEditorCore::scriptAt(*map.sides, window->path());
	step(added && added->actions.size() == 1 && history.undoName() == "Add script", QStringLiteral("a new script %1").arg(added ? utf8(added->name) : QString()));
	auto *name = window->findChild<QLineEdit *>(QStringLiteral("ScriptName"));
	if (name)
	{
		name->setText(QStringLiteral("SelfTestScript"));
		emit name->editingFinished();
	}
	added = MapEditorCore::scriptAt(*map.sides, window->path());
	step(added && added->name == "SelfTestScript", QStringLiteral("the name is set"));

	// An action through the dialog: template from the menu tree, values in the fields.
	auto *newAction = window->findChild<QPushButton *>(QStringLiteral("ScriptActionsNew"));
	if (newAction)
		newAction->click();
	QApplication::processEvents();
	auto *dialog = window->findChild<QDialog *>(QStringLiteral("ScriptCallDialog"));
	const int setFlag = templates.find(true, "SET_FLAG");
	if (dialog && setFlag >= 0)
	{
		auto *templatesTree = dialog->findChild<QTreeWidget *>(QStringLiteral("ScriptCallTemplates"));
		QTreeWidgetItem *leaf = nullptr;
		for (QTreeWidgetItemIterator it(templatesTree); *it && !leaf; ++it)
			if ((*it)->data(0, ROLE_TEMPLATE).toInt() == setFlag)
				leaf = *it;
		if (leaf)
			templatesTree->setCurrentItem(leaf);
		QApplication::processEvents();
		if (auto *flag = dialog->findChild<QComboBox *>(QStringLiteral("ScriptParam0")))
			flag->setCurrentText(QStringLiteral("SelfTestFlag"));
		if (auto *value = dialog->findChild<QComboBox *>(QStringLiteral("ScriptParam1")))
		{
			value->setCurrentIndex(1);
			emit value->activated(1);
		}
		auto *sentence = dialog->findChild<QLabel *>(QStringLiteral("ScriptCallSentence"));
		step(sentence && sentence->text().contains(QStringLiteral("SelfTestFlag")) && sentence->text().contains(QStringLiteral("TRUE")),
			QStringLiteral("the dialog shows: %1").arg(sentence ? sentence->text() : QString()));
		dialog->resize(1000, 620);
		const QString callShot = QDir(shotDir).filePath(QStringLiteral("script_call.png"));
		step(dialog->grab().save(callShot), QStringLiteral("picture %1").arg(callShot));
		dialog->accept();
		QApplication::processEvents();
	}
	else
		step(false, QStringLiteral("the action dialog opens"));
	added = MapEditorCore::scriptAt(*map.sides, window->path());
	step(added && added->actions.size() == 2 && added->actions[1].internalName == "SET_FLAG" && added->actions[1].parameters.size() == 2
		&& added->actions[1].parameters[0].text == "SelfTestFlag" && added->actions[1].parameters[1].intValue == 1 && history.undoName() == "Add action",
		QStringLiteral("the action is in the script"));

	// Flags list what the scripts use.
	step(m_scriptCatalog.parameterChoices(&map, MapEditorCore::Param::FLAG).contains(QStringLiteral("SelfTestFlag")), QStringLiteral("the new flag is offered for other scripts"));

	// Checking: a condition on a flag nothing sets is a problem until an action sets it.
	const MapEditorCore::ScriptPath self = window->path();
	const int flagCondition = templates.find(false, "FLAG");
	if (flagCondition >= 0)
	{
		editScripts("Add condition", [&templates, self, flagCondition](MapModel::Sides &s) {
			MapModel::ScriptCall c = MapEditorCore::makeCall(templates, false, flagCondition);
			c.parameters[0].text = "SelfTestNeverSet";
			MapEditorCore::scriptAt(s, self)->conditions[0].conditions.push_back(c);
			return true;
		});
		const std::vector<MapEditorCore::ScriptProblem> problems = MapEditorCore::scriptProblems(*map.sides, templates, m_scriptCatalog.check(&map));
		const bool found = std::any_of(problems.begin(), problems.end(), [](const MapEditorCore::ScriptProblem &p) {
			return p.text.find("SelfTestNeverSet' is never initialized") != std::string::npos;
		});
		auto *tree2 = window->findChild<QTreeWidget *>(QStringLiteral("ScriptsTree"));
		bool marked = false;
		for (QTreeWidgetItemIterator it(tree2); *it; ++it)
			marked = marked || (*it)->text(0).startsWith(QStringLiteral("[!] SelfTestScript"));
		step(found && marked, QStringLiteral("the check finds the flag nothing sets, the tree marks the script"));
		if (auto *checkButton = window->findChild<QPushButton *>(QStringLiteral("ScriptCheck")))
			checkButton->click();
		QApplication::processEvents();
		auto *problemDialog = window->findChild<QDialog *>(QStringLiteral("ScriptProblems"));
		auto *problemList = problemDialog ? problemDialog->findChild<QListWidget *>(QStringLiteral("ScriptProblemList")) : nullptr;
		step(problemList && problemList->count() == static_cast<int>(problems.size()), QStringLiteral("the check window lists %1 problems").arg(problems.size()));
		if (problemDialog)
			problemDialog->close();
	}

	// "Patch GC": an object type with the GC_ prefix the game does not have.
	int objectAction = -1;
	for (std::size_t i = 0; i < templates.actions.size() && objectAction < 0; ++i)
		if (templates.actions[i].valid() && templates.actions[i].parameterTypes.size() == 1 && templates.actions[i].parameterTypes[0] == MapEditorCore::Param::OBJECT_TYPE)
			objectAction = static_cast<int>(i);
	const std::vector<MeTemplateInfo> &catalog = EngineClient::instance().catalog();
	if (objectAction >= 0 && !catalog.empty())
	{
		const std::string real = catalog.front().name;
		editScripts("Add action", [&templates, self, objectAction, real](MapModel::Sides &s) {
			MapModel::ScriptCall a = MapEditorCore::makeCall(templates, true, objectAction);
			a.parameters[0].text = "GC_" + real;
			MapEditorCore::scriptAt(s, self)->actions.push_back(a);
			return true;
		});
		if (auto *patch = window->findChild<QPushButton *>(QStringLiteral("ScriptPatchGc")))
			patch->click();
		QApplication::processEvents();
		const MapModel::Script *patched = MapEditorCore::scriptAt(*map.sides, self);
		step(patched && patched->actions.back().parameters[0].text == real && history.undoName() == "Patch GC",
			QStringLiteral("Patch GC turns GC_%1 into %1").arg(utf8(real)));
	}

	// Export all, delete the script, import: it is back, and nothing else is doubled.
	const QString scb = QDir(shotDir).filePath(QStringLiteral("selftest_scripts.scb"));
	MapEditorCore::ScriptExport all;
	QString exportError;
	const bool exported = window->exportFile(scb, all, &exportError);
	const std::size_t objectsBefore = map.objects ? map.objects->size() : 0;
	editScripts("Delete script", [self](MapModel::Sides &s) { return MapEditorCore::removeScriptOrGroup(s, self); });
	QString summary;
	QStringList messages;
	const bool imported = exported && window->importFile(scb, &summary, &messages);
	std::size_t named = 0;
	MapEditorCore::forEachScript(*map.sides, [&named](const MapEditorCore::ScriptPath &, const MapModel::Script &s) { named += s.name == "SelfTestScript" ? 1 : 0; });
	step(imported && named == 1 && (map.objects ? map.objects->size() : 0) == objectsBefore && history.undoName() == "Import scripts",
		QStringLiteral("export and import: %1 %2").arg(summary, exportError));
	QFile::remove(scb);
	window->setPath(self);
	window->refresh();

	// Copy, then drag the copy into the neutral player.
	if (auto *copy = window->findChild<QPushButton *>(QStringLiteral("ScriptCopy")))
		copy->click();
	QApplication::processEvents();
	const MapEditorCore::ScriptPath copied = window->path();
	editScripts("Move script", [copied](MapModel::Sides &s) {
		return MapEditorCore::moveScriptOrGroup(s, copied, {0, -1, -1}).side >= 0;
	});
	step(!map.sides->sides[0].scripts.scripts.empty() && map.sides->sides[0].scripts.scripts[0].name == "SelfTestScript C",
		QStringLiteral("the copy moves to the first player"));

	window->resize(1180, 760);
	QApplication::processEvents();
	const QString shot = QDir(shotDir).filePath(QStringLiteral("scripts.png"));
	step(window->grab().save(shot), QStringLiteral("picture %1").arg(shot));

	while (history.isModified() && history.canUndo())
		undo();
	QApplication::processEvents();
	std::size_t after = 0;
	MapEditorCore::forEachScript(*map.sides, [&after](const MapEditorCore::ScriptPath &, const MapModel::Script &) { ++after; });
	step(after == before && !history.isModified(), QStringLiteral("undo all, %1 scripts as before").arg(after));
	window->hide();
	return step.ok();
}

} // namespace MapEditorApp
