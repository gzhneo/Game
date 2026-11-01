/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowParts.h -- what the parts of the main window (MainWindow*.cpp) share with each other and
// with their self tests: item data roles, property names and small helpers.

#pragma once

#include "Model/Map.h"

#include <QString>
#include <QStringList>
#include <Qt>

#include <functional>

class QAction;
class QMenuBar;
class QTreeWidget;
class QTreeWidgetItem;

namespace MapEditorApp
{

// Item data of the script template tree.
inline constexpr int ROLE_TEMPLATE = Qt::UserRole + 4;
// Property of a menu action with its id in the keyboard settings.
inline constexpr const char *ID_PROPERTY = "shortcutId";

// The item of the template in the catalog tree; null if not listed (MainWindowCatalog.cpp).
[[nodiscard]] QTreeWidgetItem *findTemplate(QTreeWidget *tree, const QString &name);
// Tree templates of the game, as the grove tool offers them (MainWindowPlacement.cpp).
[[nodiscard]] QStringList treeTemplates();
// The n-th autosave file in the folder (MainWindowSession.cpp).
[[nodiscard]] QString autosaveFile(const QString &folder, int n);
// Every action of the menu bar with the menu path it is shown under (MainWindowShortcuts.cpp).
void forEachAction(QMenuBar *bar, const std::function<void(QAction *, const QString &shownPath)> &visit);
// True if the object at index is a waypoint (MainWindowWaypoints.cpp).
[[nodiscard]] bool waypointAt(const MapModel::Map &map, int index);

} // namespace MapEditorApp
