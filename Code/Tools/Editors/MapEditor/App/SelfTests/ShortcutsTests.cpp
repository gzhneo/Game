/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ShortcutsTests.cpp -- the self test of MainWindowShortcuts.cpp (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/MainWindowParts.h"
#include "App/SelfTest.h"
#include "App/Language.h"
#include "App/Widgets.h"
#include <QAction>
#include <QMenuBar>
#include <functional>

namespace MapEditorApp
{

bool MainWindow::selfTestShortcuts(QStringList &report)
{
	TestSteps step(report, "shortcuts");
	QMenuBar *bar = menuWidget() ? menuWidget()->findChild<QMenuBar *>() : nullptr;
	if (!bar)
		return true;
	const auto saved = m_state.shortcuts;
	const auto findAction = [this](const QString &id) -> QAction * {
		QMenuBar *menus = menuWidget() ? menuWidget()->findChild<QMenuBar *>() : nullptr;
		QAction *found = nullptr;
		if (menus)
			forEachAction(menus, [&](QAction *action, const QString &) {
				if (action->property(ID_PROPERTY).toString() == id)
					found = action;
			});
		return found;
	};
	const QString id = QStringLiteral("Edit/Check map...");
	QAction *check = findAction(id);
	step(check && check->shortcut() == QKeySequence(QStringLiteral("F7")), QStringLiteral("commands are known by their English menu path (%1)").arg(id));
	// A conflict is found; a free key is taken over after the menus are built again.
	step(!shortcutConflict(QKeySequence(QKeySequence::Save), QString()).isEmpty(), QStringLiteral("a taken shortcut is reported"));
	m_state.shortcuts[id] = QStringLiteral("Ctrl+Alt+K");
	rebuild();
	QAction *moved = findAction(id);
	step(moved && moved->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+K")), QStringLiteral("own shortcut applied to the menu"));
	m_state.shortcuts = saved;
	rebuild();
	return step.ok();
}

} // namespace MapEditorApp
