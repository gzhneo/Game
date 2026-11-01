/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EditorContext.h -- what the editor's own windows (scripts, particles, ...) may ask of the main window:
// the open map, its selection, changing it as steps of the history, and a short message. The windows
// depend on this interface, not on MainWindow.

#pragma once

#include "App/AppState.h"
#include "Core/History.h"

#include <QString>

#include <functional>
#include <memory>

namespace MapEditorApp
{

class EditorContext
{
public:
	virtual ~EditorContext() = default;

	// The open map; null if none.
	[[nodiscard]] virtual OpenMap *openMap() = 0;
	// Folder for file dialogs: the map's own, else the user's map folder.
	[[nodiscard]] virtual QString mapFolder() const = 0;
	// True if the 3D view shows the map (camera positions can be taken from it).
	[[nodiscard]] virtual bool hasView() const = 0;
	// The first selected object; null if nothing is selected.
	[[nodiscard]] virtual const MapModel::MapObject *firstSelectedObject() const = 0;

	// Changes the players' scripts as one step (same-named mergeable steps in a row fold together);
	// refresh false leaves the scripts window as it is (an edit in its own fields).
	virtual bool editScripts(const char *name, const std::function<bool(MapModel::Sides &)> &change, bool mergeable = false, bool refresh = true) = 0;
	// Changes the players and teams as one step (see editScripts for mergeable and refresh).
	virtual bool editSides(const char *name, const std::function<bool(MapModel::Map &)> &change, bool mergeable = false, bool refresh = true) = 0;
	// Selects the objects of the team (index into the map's teams) in the views.
	virtual void selectTeam(int teamIndex) = 0;
	// The game whose data the editor uses.
	[[nodiscard]] virtual Game game() const = 0;
	// Records a step whose change is already in the map and shows it.
	virtual void recordStep(std::unique_ptr<MapEditorCore::Command> step) = 0;
	virtual void toast(const QString &text) = 0;
};

} // namespace MapEditorApp
