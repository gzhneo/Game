/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ScriptCommands.h -- the players' scripts as the old WorldBuilder's script dialog edits them
// (ScriptDialog, ScriptProperties, ScriptConditions, ScriptActionsTrue/False, EditCondition,
// EditAction, EditParameter): the tree of scripts and groups per player, a script's properties, its
// conditions (any of several or-groups, each a list of conditions that must all hold) and actions,
// and the sentence the game shows for each of them (Parameter::getUiText).
//
// The script templates come from the engine; the core only gets them as plain values.

#pragma once

#include "Core/History.h"

#include <functional>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

namespace MapEditorCore
{

// Parameter::ParameterType.
namespace Param
{
enum : std::int32_t
{
	INT = 0, REAL, SCRIPT, TEAM, COUNTER, FLAG, COMPARISON, WAYPOINT, BOOLEAN, TRIGGER_AREA, TEXT_STRING, SIDE, SOUND,
	SCRIPT_SUBROUTINE, UNIT, OBJECT_TYPE, COORD3D, ANGLE, TEAM_STATE, RELATION, AI_MOOD, DIALOG, MUSIC, MOVIE,
	WAYPOINT_PATH, LOCALIZED_TEXT, BRIDGE, KIND_OF_PARAM, ATTACK_PRIORITY_SET, RADAR_EVENT_TYPE, SPECIAL_POWER, SCIENCE,
	UPGRADE, COMMANDBUTTON_ABILITY, BOUNDARY, BUILDABLE, SURFACES_ALLOWED, SHAKE_INTENSITY, COMMAND_BUTTON, FONT_NAME,
	OBJECT_STATUS, COMMANDBUTTON_ALL_ABILITIES, SKIRMISH_WAYPOINT_PATH, COLOR, EMOTICON, OBJECT_PANEL_FLAG, FACTION_NAME,
	OBJECT_TYPE_LIST, REVEALNAME, SCIENCE_AVAILABILITY, LEFT_OR_RIGHT, PERCENT, COUNT
};
}

// Special names the script engine understands (GameLogic/Scripts.h).
constexpr const char *THIS_TEAM = "<This Team>";
constexpr const char *ANY_TEAM = "<Any Team>";
constexpr const char *ANY_OBJECT = "<Any Object>";
constexpr const char *THIS_OBJECT = "<This Object>";
constexpr const char *THIS_PLAYER = "<This Player>";
constexpr const char *LOCAL_PLAYER = "<Local Player>";
constexpr const char *THIS_PLAYER_ENEMY = "<This Player's Enemy>";

// Which field of a parameter holds its value.
enum class ValueKind
{
	Int,
	Real,
	Text,
	Coord
};
[[nodiscard]] ValueKind valueKind(std::int32_t type);

struct ScriptTemplate
{
	std::string internalName;
	std::string uiName;						// menu path, parts separated by '/'
	std::string uiName2;
	std::string help;
	std::vector<std::int32_t> parameterTypes;
	std::vector<std::string> uiStrings;		// text around the parameters

	bool valid() const { return !internalName.empty() && !uiName.empty(); }
};

// Indexed by the number a map stores.
struct ScriptTemplates
{
	std::vector<ScriptTemplate> conditions;
	std::vector<ScriptTemplate> actions;

	int find(bool action, const std::string &internalName) const;
};

// The template of a call: by its internal name, else (old maps) by its number. Null if unknown.
const ScriptTemplate *callTemplate(const ScriptTemplates &templates, bool action, const MapModel::ScriptCall &call);
// A call of the template with its parameters zero or empty (Condition::setConditionType).
MapModel::ScriptCall makeCall(const ScriptTemplates &templates, bool action, int index);
// Changes the call to another template; parameters of the same type at the same place keep their values.
void setCallTemplate(MapModel::ScriptCall &call, const ScriptTemplates &templates, bool action, int index);

// Names of number-valued parameters (comparison, kind of, relation, ...): empty if unknown.
using ValueNames = std::function<std::string(std::int32_t type, std::int32_t value)>;
// Parameter::getUiText.
[[nodiscard]] std::string parameterText(const MapModel::ScriptParameter &parameter, const ValueNames &names);
// The text pieces of the template with the parameters between them (Condition::getUiText).
[[nodiscard]] std::string callText(const MapModel::ScriptCall &call, const ScriptTemplate *t, const ValueNames &names);

// A place in the tree: player, group (-1: the player's own list) and script (-1: the group itself;
// both -1: the player).
struct ScriptPath
{
	int side = -1;
	int group = -1;
	int script = -1;

	bool operator==(const ScriptPath &) const = default;
};

MapModel::Script *scriptAt(MapModel::Sides &sides, const ScriptPath &path);
const MapModel::Script *scriptAt(const MapModel::Sides &sides, const ScriptPath &path);
MapModel::ScriptGroup *groupAt(MapModel::Sides &sides, const ScriptPath &path);
const MapModel::ScriptGroup *groupAt(const MapModel::Sides &sides, const ScriptPath &path);

// The first free "<base> 1", "<base> 2", ... among scripts and groups.
[[nodiscard]] std::string freeScriptName(const MapModel::Sides &sides, const std::string &base);
// A new script: always true, doing nothing (ScriptDialog::OnNewScript).
MapModel::Script newScript(const ScriptTemplates &templates, const std::string &name);
// Inserts after the selected script (or first in the selected player or group); returns where.
ScriptPath insertScript(MapModel::Sides &sides, const ScriptPath &at, MapModel::Script script);
// Inserts a group after the selected group (or first); returns where.
ScriptPath insertGroup(MapModel::Sides &sides, const ScriptPath &at, MapModel::ScriptGroup group);
// A copy named "<name> C" after the script (ScriptDialog::OnCopyScript).
ScriptPath copyScript(MapModel::Sides &sides, const ScriptPath &path);
bool removeScriptOrGroup(MapModel::Sides &sides, const ScriptPath &path);
// Moves a script or group onto the target (ScriptDialog::doDropOn): a script goes before a target
// script or first into a target group or player; a group goes after the target's group, or last for
// a target in the player's own list. Returns the new place, side -1 if nothing moved.
ScriptPath moveScriptOrGroup(MapModel::Sides &sides, const ScriptPath &from, const ScriptPath &target);

// Names the map's scripts use for a parameter type (counters, flags, attack priority sets, ...);
// only in the named actions if given. Sorted, without duplicates or empty names.
std::vector<std::string> usedNames(const MapModel::Sides &sides, const ScriptTemplates &templates, std::int32_t type,
	const std::vector<std::string> &onlyInActions = {});
// Names of scripts (and groups when subroutinesOnly, which the game can run as subroutines too).
[[nodiscard]] std::vector<std::string> scriptNames(const MapModel::Sides &sides, bool subroutinesOnly);
// Every script of every player, with its place.
void forEachScript(const MapModel::Sides &sides, const std::function<void(const ScriptPath &, const MapModel::Script &)> &visit);

// Checking parameters (EditParameter::getWarningText): the names that exist, by parameter type.
// Types without a list are not checked; game data lists that are empty (no engine) are left out.
struct ScriptCheck
{
	std::map<std::int32_t, std::set<std::string>> known;
	std::set<std::string> subroutines;
	std::set<std::string> counters;			// set by an action
	std::set<std::string> flags;			// set by an action, or by the shell
	int boundaries = 0;
};
// gameNames: the game data's names for a type (sounds, sciences, object types, ...).
ScriptCheck scriptCheck(const MapModel::Map &map, const ScriptTemplates &templates,
	const std::function<std::vector<std::string>(std::int32_t type)> &gameNames);
// The warning for a parameter, in the old dialog's words; empty if the value exists.
[[nodiscard]] std::string parameterWarning(const MapModel::ScriptParameter &p, bool action, const ScriptCheck &check);
[[nodiscard]] std::vector<std::string> callWarnings(const MapModel::ScriptCall &call, bool action, const ScriptTemplates &templates, const ScriptCheck &check);

struct ScriptProblem
{
	ScriptPath path;
	std::string text;
};
[[nodiscard]] std::vector<ScriptProblem> scriptProblems(const MapModel::Sides &sides, const ScriptTemplates &templates, const ScriptCheck &check);
[[nodiscard]] bool scriptHasProblems(const MapModel::Script &s, const ScriptTemplates &templates, const ScriptCheck &check);
// "Patch GC" (ScriptDialog::checkParametersForGC): object types "GC_<name>" that do not exist become
// "<name>" where that exists. Returns how many changed.
int patchGcObjectTypes(MapModel::Sides &sides, const ScriptCheck &check);

// The script lists of the players that changed, before and after, as one step. Steps of the same name
// that follow each other fold together if marked mergeable (typing in a field).
class EditScripts : public Command
{
public:
	struct SideLists
	{
		int side = 0;
		MapModel::ScriptList before;
		MapModel::ScriptList after;
	};

	EditScripts(std::string name, std::vector<SideLists> lists, bool mergeable)
		: m_name(std::move(name)), m_lists(std::move(lists)), m_mergeable(mergeable) {}

	virtual void apply(MapModel::Map &map) override;
	virtual void revert(MapModel::Map &map) override;
	virtual std::string name() const override { return m_name; }
	virtual Changes changes() const override;
	virtual bool mergeWith(const Command &next) override;

private:
	std::string m_name;
	std::vector<SideLists> m_lists;
	bool m_mergeable;
};

[[nodiscard]] std::vector<MapModel::ScriptList> scriptLists(const MapModel::Map &map);
// The step from the lists before to the map now; null if nothing changed or the players changed.
std::unique_ptr<EditScripts> scriptsStep(const MapModel::Map &map, std::vector<MapModel::ScriptList> before, std::string name, bool mergeable = false);

} // namespace MapEditorCore
