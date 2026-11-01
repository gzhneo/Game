/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowCommandNames.cpp -- the names of the steps in the history and in the menus, in the editor's
// language (the core names its commands in English).

#include "App/MainWindow.h"
#include "App/EngineClient.h"
#include "App/Icons.h"
#include "App/Language.h"
#include "App/MapImage.h"
#include "App/MapOverview.h"
#include "App/MiniMap.h"
#include "App/ToolEvent.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/SidesCommands.h"
#include "Model/MapDump.h"
#include "Model/MapIO.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

namespace MapEditorApp
{

namespace
{

// Menu text of a command of the core (the core names its commands in English).
QString commandText(const std::string &name)
{
	if (name == "Move object")
		return T("Move object");
	if (name == "Replace in scripts")
		return T("Replace in scripts");
	if (name == "Symmetry")
		return T("Symmetry");
	if (name == "Move objects")
		return T("Move objects");
	if (name == "Rotate object")
		return T("Rotate object");
	if (name == "Rotate objects")
		return T("Rotate objects");
	if (name == "Change height")
		return T("Change height");
	if (name == "Delete object")
		return T("Delete object");
	if (name == "Delete objects")
		return T("Delete objects");
	if (name == "Cut")
		return T("Cut");
	if (name == "Paste")
		return T("Paste");
	if (name == "Duplicate")
		return T("Duplicate");
	if (name == "Place object")
		return T("Place object");
	if (name == "Add waypoint")
		return T("Add waypoint");
	if (name == "Link waypoints")
		return T("Link waypoints");
	if (name == "Unlink waypoints")
		return T("Unlink waypoints");
	if (name == "Change path")
		return T("Change path");
	if (name == "Rename object")
		return T("Rename object");
	if (name == "Rename waypoint")
		return T("Rename waypoint");
	if (name == "Change team")
		return T("Change team");
	if (name == "Change health")
		return T("Change health");
	if (name == "Change veterancy")
		return T("Change veterancy");
	if (name == "Change behaviour")
		return T("Change behaviour");
	if (name == "Change model")
		return T("Change model");
	if (name == "Change property")
		return T("Change property");
	if (name == "Paint texture")
		return T("Paint texture");
	if (name == "Blend edge")
		return T("Blend edge");
	if (name == "Auto edge")
		return T("Auto edge");
	if (name == "Ramp")
		return T("Ramp");
	if (name == "Mesh mold")
		return T("Mesh mold");
	if (name == "Map name")
		return T("Map name");
	if (name == "Time of day")
		return T("Time of day");
	if (name == "Weather")
		return T("Weather");
	if (name == "Compression")
		return T("Compression");
	if (name == "Change light")
		return T("Change light");
	if (name == "Move the sun")
		return T("Move the sun");
	if (name == "Shadow colour")
		return T("Shadow colour");
	if (name == "Change playable area")
		return T("Change playable area");
	if (name == "Delete playable area")
		return T("Delete playable area");
	if (name == "Move to layer")
		return T("Move to layer");
	if (name == "Rename layer")
		return T("Rename layer");
	if (name == "Delete layer")
		return T("Delete layer");
	if (name == "Group objects")
		return T("Group objects");
	if (name == "Ungroup objects")
		return T("Ungroup objects");
	if (name == "Plant grove")
		return T("Plant grove");
	if (name == "Plant trees")
		return T("Plant trees");
	if (name == "Place fence")
		return T("Place fence");
	if (name == "Place scorch mark")
		return T("Place scorch mark");
	if (name == "Fill texture")
		return T("Fill texture");
	if (name == "Replace texture")
		return T("Replace texture");
	if (name == "Remove unused textures")
		return T("Remove unused textures");
	if (name == "Mark impassable")
		return T("Mark impassable");
	if (name == "Mark passable")
		return T("Mark passable");
	if (name == "Passability from heights")
		return T("Passability from heights");
	if (name == "Raise terrain")
		return T("Raise terrain");
	if (name == "Lower terrain")
		return T("Lower terrain");
	if (name == "Set terrain height")
		return T("Set terrain height");
	if (name == "Smooth terrain")
		return T("Smooth terrain");
	if (name == "Change sound")
		return T("Change sound");
	if (name == "Add bridge")
		return T("Add bridge");
	if (name == "Add road")
		return T("Add road");
	if (name == "Delete road")
		return T("Delete road");
	if (name == "Change road")
		return T("Change road");
	if (name == "Move road point")
		return T("Move road point");
	if (name == "Add area")
		return T("Add area");
	if (name == "Move point")
		return T("Move point");
	if (name == "Insert point")
		return T("Insert point");
	if (name == "Remove point")
		return T("Remove point");
	if (name == "Move area")
		return T("Move area");
	if (name == "Rename area")
		return T("Rename area");
	if (name == "Change area")
		return T("Change area");
	if (name == "Change water height")
		return T("Change water height");
	if (name == "Delete area")
		return T("Delete area");
	if (name == "Add property")
		return T("Add property");
	if (name == "Remove property")
		return T("Remove property");
	if (name == "Resize map")
		return T("Resize map");
	if (name == "Import scripts")
		return T("Import scripts");
	if (name == "Import heights")
		return T("Import heights");
	if (name == "Place prefab")
		return T("Place prefab");
	if (name == "Texture cliffs")
		return T("Texture cliffs");
	if (name == "Remove cliff alignment")
		return T("Remove cliff alignment");
	if (name == "Merge layers")
		return T("Merge layers");
	if (name == "Fix problems")
		return T("Fix problems");
	if (name == "Replace object type")
		return T("Replace object type");
	if (name == "Remove object type")
		return T("Remove object type");
	if (name == "Fix teams")
		return T("Fix teams");
	if (name == "Set faction")
		return T("Set faction");
	if (name == "Add to build list")
		return T("Add to build list");
	if (name == "Move build list entry")
		return T("Move build list entry");
	if (name == "Turn build list entry")
		return T("Turn build list entry");
	if (name == "Delete build list entry")
		return T("Delete build list entry");
	if (name == "Change build list entry")
		return T("Change build list entry");
	if (name == "Patch GC")
		return T("Patch GC");
	if (name == "Add script")
		return T("Add script");
	if (name == "Add script group")
		return T("Add script group");
	if (name == "Copy script")
		return T("Copy script");
	if (name == "Delete script")
		return T("Delete script");
	if (name == "Switch script")
		return T("Switch script");
	if (name == "Move script")
		return T("Move script");
	if (name == "Rename script group")
		return T("Rename script group");
	if (name == "Change script group")
		return T("Change script group");
	if (name == "Rename script")
		return T("Rename script");
	if (name == "Change comment")
		return T("Change comment");
	if (name == "Change script")
		return T("Change script");
	if (name == "Change condition")
		return T("Change condition");
	if (name == "Add condition")
		return T("Add condition");
	if (name == "Delete condition")
		return T("Delete condition");
	if (name == "Move condition")
		return T("Move condition");
	if (name == "Change action")
		return T("Change action");
	if (name == "Add action")
		return T("Add action");
	if (name == "Copy action")
		return T("Copy action");
	if (name == "Delete action")
		return T("Delete action");
	if (name == "Move action")
		return T("Move action");
	if (name == "Add player")
		return T("Add player");
	if (name == "Remove player")
		return T("Remove player");
	if (name == "Rename player")
		return T("Rename player");
	if (name == "Change display name")
		return T("Change display name");
	if (name == "Change faction")
		return T("Change faction");
	if (name == "Change controller")
		return T("Change controller");
	if (name == "Change colour")
		return T("Change colour");
	if (name == "Change relation")
		return T("Change relation");
	if (name == "Add skirmish players")
		return T("Add skirmish players");
	if (name == "Add team")
		return T("Add team");
	if (name == "Copy team")
		return T("Copy team");
	if (name == "Delete team")
		return T("Delete team");
	if (name == "Move team")
		return T("Move team");
	if (name == "Rename team")
		return T("Rename team");
	if (name == "Change team owner")
		return T("Change team owner");
	if (name == "Change team")
		return T("Change team");
	return utf8(name);
}

} // namespace

QString MainWindow::stepName(const std::string &name)
{
	return commandText(name);
}

} // namespace MapEditorApp
