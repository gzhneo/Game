/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ScriptCommands.cpp -- the players' scripts.

#include "Core/ScriptCommands.h"

#include "Core/RoadCommands.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>

namespace MapEditorCore
{

namespace
{

constexpr double PI = 3.14159265358979323846;

std::string format(const char *pattern, const std::string &text)
{
	char buffer[512];
	std::snprintf(buffer, sizeof(buffer), pattern, text.c_str());
	return buffer;
}

std::string formatReal(const char *pattern, double value)
{
	char buffer[64];
	std::snprintf(buffer, sizeof(buffer), pattern, value);
	return buffer;
}

std::vector<MapModel::Script> *listOf(MapModel::Sides &sides, int side, int group)
{
	if (side < 0 || side >= static_cast<int>(sides.sides.size()))
		return nullptr;
	MapModel::ScriptList &list = sides.sides[static_cast<std::size_t>(side)].scripts;
	if (group < 0)
		return &list.scripts;
	if (group >= static_cast<int>(list.groups.size()))
		return nullptr;
	return &list.groups[static_cast<std::size_t>(group)].scripts;
}

void forEachCall(const MapModel::Script &s, const std::function<void(const MapModel::ScriptCall &, bool action)> &visit)
{
	for (const MapModel::OrCondition &o : s.conditions)
		for (const MapModel::ScriptCall &c : o.conditions)
			visit(c, false);
	for (const MapModel::ScriptCall &a : s.actions)
		visit(a, true);
	for (const MapModel::ScriptCall &a : s.falseActions)
		visit(a, true);
}

} // namespace

ValueKind valueKind(std::int32_t type)
{
	switch (type)
	{
		case Param::INT:
		case Param::COMPARISON:
		case Param::BOOLEAN:
		case Param::RELATION:
		case Param::AI_MOOD:
		case Param::KIND_OF_PARAM:
		case Param::RADAR_EVENT_TYPE:
		case Param::BOUNDARY:
		case Param::BUILDABLE:
		case Param::SURFACES_ALLOWED:
		case Param::SHAKE_INTENSITY:
		case Param::COLOR:
		case Param::LEFT_OR_RIGHT:
			return ValueKind::Int;
		case Param::REAL:
		case Param::ANGLE:
		case Param::PERCENT:
			return ValueKind::Real;
		case Param::COORD3D:
			return ValueKind::Coord;
		default:
			return ValueKind::Text;
	}
}

int ScriptTemplates::find(bool action, const std::string &internalName) const
{
	const std::vector<ScriptTemplate> &list = action ? actions : conditions;
	for (std::size_t i = 0; i < list.size(); ++i)
		if (list[i].internalName == internalName)
			return static_cast<int>(i);
	return -1;
}

const ScriptTemplate *callTemplate(const ScriptTemplates &templates, bool action, const MapModel::ScriptCall &call)
{
	const std::vector<ScriptTemplate> &list = action ? templates.actions : templates.conditions;
	if (!call.internalName.empty())
	{
		const int i = templates.find(action, call.internalName);
		return i >= 0 ? &list[static_cast<std::size_t>(i)] : nullptr;
	}
	if (call.type >= 0 && call.type < static_cast<std::int32_t>(list.size()) && list[static_cast<std::size_t>(call.type)].valid())
		return &list[static_cast<std::size_t>(call.type)];
	return nullptr;
}

MapModel::ScriptCall makeCall(const ScriptTemplates &templates, bool action, int index)
{
	MapModel::ScriptCall call;
	const std::vector<ScriptTemplate> &list = action ? templates.actions : templates.conditions;
	call.type = index;
	if (index < 0 || index >= static_cast<int>(list.size()))
		return call;
	const ScriptTemplate &t = list[static_cast<std::size_t>(index)];
	call.internalName = t.internalName;
	for (std::int32_t type : t.parameterTypes)
	{
		MapModel::ScriptParameter p;
		p.type = type;
		call.parameters.push_back(p);
	}
	return call;
}

void setCallTemplate(MapModel::ScriptCall &call, const ScriptTemplates &templates, bool action, int index)
{
	MapModel::ScriptCall changed = makeCall(templates, action, index);
	for (std::size_t i = 0; i < changed.parameters.size() && i < call.parameters.size(); ++i)
		if (call.parameters[i].type == changed.parameters[i].type)
			changed.parameters[i] = call.parameters[i];
	call = std::move(changed);
}

std::string parameterText(const MapModel::ScriptParameter &p, const ValueNames &names)
{
	const std::string text = p.text.empty() ? std::string("???") : p.text;
	const auto named = [&names, &p]() {
		const std::string n = names ? names(p.type, p.intValue) : std::string();
		return n.empty() ? std::string("???") : n;
	};
	switch (p.type)
	{
		case Param::SOUND: return format("Sound '%s'", text);
		case Param::SCRIPT: return format("Script '%s'", text);
		case Param::TEAM_STATE: return format("'%s'", text);
		case Param::SCRIPT_SUBROUTINE: return format("Subroutine '%s'", text);
		case Param::ATTACK_PRIORITY_SET: return format("Attack priority set '%s'", text);
		case Param::WAYPOINT: return format("Waypoint '%s'", text);
		case Param::WAYPOINT_PATH: return format("Waypoint Path '%s'", text);
		case Param::TRIGGER_AREA: return format(" area '%s'", text);
		case Param::COMMAND_BUTTON: return format("Command button: '%s'", text);
		case Param::FONT_NAME: return format("Font: '%s'", text);
		case Param::LOCALIZED_TEXT: return format("Localized String: '%s'", text);
		case Param::TEXT_STRING: return format("String: '%s'", text);
		case Param::TEAM: return format("Team '%s'", text);
		case Param::UNIT: return format("Unit '%s'", text);
		case Param::BRIDGE: return format("Bridge '%s'", text);
		case Param::ANGLE: return formatReal("%.2f degrees", p.realValue * 180.0 / PI);
		case Param::PERCENT: return formatReal("%.2f%%", p.realValue * 100.0);
		case Param::COORD3D:
		{
			char buffer[96];
			std::snprintf(buffer, sizeof(buffer), "(%.2f,%.2f,%.2f)", p.coord[0], p.coord[1], p.coord[2]);
			return buffer;
		}
		case Param::OBJECT_TYPE: return format("'%s'", text);
		case Param::KIND_OF_PARAM: return format("Kind is '%s'", named());
		case Param::SIDE: return format("Player '%s'", text);
		case Param::COUNTER: return format("'%s'", text);
		case Param::INT: return " " + std::to_string(p.intValue) + " ";
		case Param::BOOLEAN: return p.intValue ? "TRUE" : "FALSE";
		case Param::REAL: return formatReal("%.2f", p.realValue);
		case Param::FLAG: return format("Flag named '%s'", text);
		case Param::COMPARISON:
		case Param::RELATION:
		case Param::AI_MOOD:
		case Param::RADAR_EVENT_TYPE:
			return named();
		case Param::LEFT_OR_RIGHT:
		{
			const std::string n = names ? names(p.type, p.intValue) : std::string();
			return n.empty() ? std::string("unspecified") : n;
		}
		case Param::DIALOG: return format("'%s'", text);
		case Param::SKIRMISH_WAYPOINT_PATH: return format("'%s'", text);
		case Param::COLOR:
		{
			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), " R:%d G:%d B:%d ", (p.intValue >> 16) & 0xFF, (p.intValue >> 8) & 0xFF, p.intValue & 0xFF);
			return buffer;
		}
		case Param::MUSIC: return format("'%s'", text);
		case Param::MOVIE: return format("'%s'", text);
		case Param::SPECIAL_POWER: return format("Special power '%s'", text);
		case Param::SCIENCE: return format("Science '%s'", text);
		case Param::SCIENCE_AVAILABILITY: return format("Science availability '%s'", text);
		case Param::UPGRADE: return format("Upgrade '%s'", text);
		case Param::COMMANDBUTTON_ABILITY:
		case Param::COMMANDBUTTON_ALL_ABILITIES:
			return format("Ability '%s'", text);
		case Param::EMOTICON: return format("Emoticon '%s'", text);
		case Param::BOUNDARY: return format("Boundary %s", named());
		case Param::BUILDABLE: return format("Buildable (%s)", named());
		case Param::SURFACES_ALLOWED: return format("Surfaces Allowed: %s", named());
		case Param::SHAKE_INTENSITY: return format("Shake Intensity: %s", named());
		case Param::OBJECT_STATUS: return format("Object Status is '%s'", text);
		case Param::FACTION_NAME: return format("Faction Name: %s", text);
		case Param::OBJECT_TYPE_LIST: return format("'%s'", text);
		case Param::REVEALNAME: return format("Reveal Name: %s", text);
		case Param::OBJECT_PANEL_FLAG: return format("Object Flag: %s", text);
		default: return format("'%s'", text);
	}
}

std::string callText(const MapModel::ScriptCall &call, const ScriptTemplate *t, const ValueNames &names)
{
	std::string out;
	if (!t)
	{
		out = "[" + (call.internalName.empty() ? "#" + std::to_string(call.type) : call.internalName) + "]";
		for (const MapModel::ScriptParameter &p : call.parameters)
			out += " " + parameterText(p, names);
		return out;
	}
	const std::size_t n = std::max(t->uiStrings.size(), call.parameters.size());
	for (std::size_t i = 0; i < n; ++i)
	{
		if (i < t->uiStrings.size())
			out += t->uiStrings[i];
		if (i < call.parameters.size())
			out += parameterText(call.parameters[i], names);
	}
	return out;
}

MapModel::Script *scriptAt(MapModel::Sides &sides, const ScriptPath &path)
{
	std::vector<MapModel::Script> *list = listOf(sides, path.side, path.group);
	if (!list || path.script < 0 || path.script >= static_cast<int>(list->size()))
		return nullptr;
	return &(*list)[static_cast<std::size_t>(path.script)];
}

const MapModel::Script *scriptAt(const MapModel::Sides &sides, const ScriptPath &path)
{
	return scriptAt(const_cast<MapModel::Sides &>(sides), path);
}

MapModel::ScriptGroup *groupAt(MapModel::Sides &sides, const ScriptPath &path)
{
	if (path.side < 0 || path.side >= static_cast<int>(sides.sides.size()) || path.group < 0)
		return nullptr;
	MapModel::ScriptList &list = sides.sides[static_cast<std::size_t>(path.side)].scripts;
	if (path.group >= static_cast<int>(list.groups.size()))
		return nullptr;
	return &list.groups[static_cast<std::size_t>(path.group)];
}

const MapModel::ScriptGroup *groupAt(const MapModel::Sides &sides, const ScriptPath &path)
{
	return groupAt(const_cast<MapModel::Sides &>(sides), path);
}

std::string freeScriptName(const MapModel::Sides &sides, const std::string &base)
{
	std::set<std::string> used;
	for (const MapModel::Side &side : sides.sides)
	{
		for (const MapModel::Script &s : side.scripts.scripts)
			used.insert(s.name);
		for (const MapModel::ScriptGroup &g : side.scripts.groups)
		{
			used.insert(g.name);
			for (const MapModel::Script &s : g.scripts)
				used.insert(s.name);
		}
	}
	for (int n = 1;; ++n)
	{
		const std::string name = base + " " + std::to_string(n);
		if (!used.count(name))
			return name;
	}
}

MapModel::Script newScript(const ScriptTemplates &templates, const std::string &name)
{
	MapModel::Script s;
	s.name = name;
	MapModel::OrCondition always;
	always.conditions.push_back(makeCall(templates, false, std::max(0, templates.find(false, "CONDITION_TRUE"))));
	s.conditions.push_back(always);
	s.actions.push_back(makeCall(templates, true, std::max(0, templates.find(true, "NO_OP"))));
	return s;
}

ScriptPath insertScript(MapModel::Sides &sides, const ScriptPath &at, MapModel::Script script)
{
	std::vector<MapModel::Script> *list = listOf(sides, at.side, at.group);
	if (!list)
		return {};
	const int index = std::clamp(at.script < 0 ? 0 : at.script + 1, 0, static_cast<int>(list->size()));
	list->insert(list->begin() + index, std::move(script));
	return {at.side, at.group, index};
}

ScriptPath insertGroup(MapModel::Sides &sides, const ScriptPath &at, MapModel::ScriptGroup group)
{
	if (at.side < 0 || at.side >= static_cast<int>(sides.sides.size()))
		return {};
	std::vector<MapModel::ScriptGroup> &groups = sides.sides[static_cast<std::size_t>(at.side)].scripts.groups;
	const int index = std::clamp(at.group < 0 ? 0 : at.group + 1, 0, static_cast<int>(groups.size()));
	groups.insert(groups.begin() + index, std::move(group));
	return {at.side, index, -1};
}

ScriptPath copyScript(MapModel::Sides &sides, const ScriptPath &path)
{
	const MapModel::Script *s = scriptAt(sides, path);
	if (!s)
		return {};
	MapModel::Script copy = *s;
	copy.name += " C";
	return insertScript(sides, path, std::move(copy));
}

bool removeScriptOrGroup(MapModel::Sides &sides, const ScriptPath &path)
{
	if (path.script >= 0)
	{
		std::vector<MapModel::Script> *list = listOf(sides, path.side, path.group);
		if (!list || path.script >= static_cast<int>(list->size()))
			return false;
		list->erase(list->begin() + path.script);
		return true;
	}
	if (!groupAt(sides, path))
		return false;
	std::vector<MapModel::ScriptGroup> &groups = sides.sides[static_cast<std::size_t>(path.side)].scripts.groups;
	groups.erase(groups.begin() + path.group);
	return true;
}

ScriptPath moveScriptOrGroup(MapModel::Sides &sides, const ScriptPath &from, const ScriptPath &targetIn)
{
	ScriptPath target = targetIn;
	if (from == target || target.side < 0 || target.side >= static_cast<int>(sides.sides.size()))
		return {};
	if (from.script >= 0)
	{
		const MapModel::Script *s = scriptAt(sides, from);
		if (!s)
			return {};
		// A target group or player must stay valid once the script is out.
		if (target.script < 0 && target.group >= 0 && !groupAt(sides, target))
			return {};
		MapModel::Script moving = *s;
		removeScriptOrGroup(sides, from);
		if (from.side == target.side && from.group == target.group && target.script > from.script)
			--target.script;
		std::vector<MapModel::Script> *list = listOf(sides, target.side, target.group);
		if (!list)
			return {};
		const int index = std::clamp(target.script < 0 ? 0 : target.script, 0, static_cast<int>(list->size()));
		list->insert(list->begin() + index, std::move(moving));
		return {target.side, target.group, index};
	}
	const MapModel::ScriptGroup *g = groupAt(sides, from);
	if (!g || (from.side == target.side && from.group == target.group))
		return {};
	MapModel::ScriptGroup moving = *g;
	removeScriptOrGroup(sides, from);
	if (from.side == target.side && target.group > from.group)
		--target.group;
	std::vector<MapModel::ScriptGroup> &groups = sides.sides[static_cast<std::size_t>(target.side)].scripts.groups;
	int index = 0;
	if (target.group < 0)
		index = target.script >= 0 ? static_cast<int>(groups.size()) : 0;
	else
		index = target.script >= 0 ? target.group + 1 : target.group;
	index = std::clamp(index, 0, static_cast<int>(groups.size()));
	groups.insert(groups.begin() + index, std::move(moving));
	return {target.side, index, -1};
}

void forEachScript(const MapModel::Sides &sides, const std::function<void(const ScriptPath &, const MapModel::Script &)> &visit)
{
	for (std::size_t s = 0; s < sides.sides.size(); ++s)
	{
		const MapModel::ScriptList &list = sides.sides[s].scripts;
		for (std::size_t i = 0; i < list.scripts.size(); ++i)
			visit({static_cast<int>(s), -1, static_cast<int>(i)}, list.scripts[i]);
		for (std::size_t g = 0; g < list.groups.size(); ++g)
			for (std::size_t i = 0; i < list.groups[g].scripts.size(); ++i)
				visit({static_cast<int>(s), static_cast<int>(g), static_cast<int>(i)}, list.groups[g].scripts[i]);
	}
}

std::vector<std::string> usedNames(const MapModel::Sides &sides, const ScriptTemplates &templates, std::int32_t type,
	const std::vector<std::string> &onlyInActions)
{
	std::set<std::string> names;
	forEachScript(sides, [&](const ScriptPath &, const MapModel::Script &s) {
		forEachCall(s, [&](const MapModel::ScriptCall &call, bool action) {
			if (!onlyInActions.empty())
			{
				const ScriptTemplate *t = action ? callTemplate(templates, true, call) : nullptr;
				const std::string name = t ? t->internalName : call.internalName;
				if (!action || std::find(onlyInActions.begin(), onlyInActions.end(), name) == onlyInActions.end())
					return;
			}
			for (const MapModel::ScriptParameter &p : call.parameters)
				if (p.type == type && !p.text.empty())
					names.insert(p.text);
		});
	});
	return {names.begin(), names.end()};
}

std::vector<std::string> scriptNames(const MapModel::Sides &sides, bool subroutinesOnly)
{
	std::vector<std::string> names;
	for (const MapModel::Side &side : sides.sides)
	{
		for (const MapModel::Script &s : side.scripts.scripts)
			if (!subroutinesOnly || s.isSubroutine)
				names.push_back(s.name);
		for (const MapModel::ScriptGroup &g : side.scripts.groups)
		{
			if (!subroutinesOnly || g.isSubroutine)
				names.push_back(g.name);
			for (const MapModel::Script &s : g.scripts)
				if (!subroutinesOnly || s.isSubroutine)
					names.push_back(s.name);
		}
	}
	return names;
}

ScriptCheck scriptCheck(const MapModel::Map &map, const ScriptTemplates &templates,
	const std::function<std::vector<std::string>(std::int32_t type)> &gameNames)
{
	ScriptCheck check;
	const MapModel::Sides empty;
	const MapModel::Sides &sides = map.sides ? *map.sides : empty;
	auto &known = check.known;
	for (const std::string &n : scriptNames(sides, false))
		known[Param::SCRIPT].insert(n);
	for (const std::string &n : scriptNames(sides, true))
		check.subroutines.insert(n);
	known[Param::TEAM] = {THIS_TEAM, ANY_TEAM};
	for (const MapModel::PropertyMap &team : sides.teams)
	{
		const std::string name = team.getAscii("teamName").value_or("");
		// The old list left out the neutral player's default team.
		if (name != "team")
			known[Param::TEAM].insert(name);
	}
	known[Param::SIDE] = {LOCAL_PLAYER, THIS_PLAYER, THIS_PLAYER_ENEMY};
	for (const MapModel::Side &side : sides.sides)
		if (const std::string n = side.properties.getAscii("playerName").value_or(""); !n.empty())
			known[Param::SIDE].insert(n);
	known[Param::UNIT] = {THIS_OBJECT, ANY_OBJECT};
	known[Param::WAYPOINT];
	known[Param::WAYPOINT_PATH];
	known[Param::BRIDGE];
	if (map.objects)
	{
		const std::vector<MapModel::MapObject> &objects = *map.objects;
		for (std::size_t i = 0; i < objects.size(); ++i)
		{
			const MapModel::MapObject &o = objects[i];
			const std::string name = o.properties.getAscii("objectName").value_or("");
			if (!name.empty() && (o.flags & (ROAD_POINT1 | ROAD_POINT2)) == 0)
				known[Param::UNIT].insert(name);
			if (!name.empty() && (o.flags & BRIDGE_POINT1) && i + 1 < objects.size() && (objects[i + 1].flags & BRIDGE_POINT2))
				known[Param::BRIDGE].insert(name);
			if (o.properties.getInt("waypointID"))
			{
				if (const auto n = o.properties.getAscii("waypointName"); n && !n->empty())
					known[Param::WAYPOINT].insert(*n);
				for (const char *key : {"waypointPathLabel1", "waypointPathLabel2", "waypointPathLabel3"})
					if (const auto n = o.properties.getAscii(key); n && !n->empty())
						known[Param::WAYPOINT_PATH].insert(*n);
			}
		}
	}
	for (const MapModel::Side &side : sides.sides)
		for (const MapModel::BuildListEntry &b : side.buildList)
			if (!b.buildingName.empty())
				known[Param::UNIT].insert(b.buildingName);
	for (const std::string &n : usedNames(sides, templates, Param::UNIT, {"CREATE_NAMED_ON_TEAM_AT_WAYPOINT", "UNIT_SPAWN_NAMED_LOCATION_ORIENTATION"}))
		known[Param::UNIT].insert(n);
	known[Param::TRIGGER_AREA] = {"[Skirmish]MyInnerPerimeter", "[Skirmish]MyOuterPerimeter", "[Skirmish]EnemyOuterPerimeter",
		"[Skirmish]EnemyInnerPerimeter", "Water Grid"};
	if (map.polygons)
		for (const MapModel::PolygonArea &a : *map.polygons)
			known[Param::TRIGGER_AREA].insert(a.name);
	for (const std::string &n : usedNames(sides, templates, Param::ATTACK_PRIORITY_SET, {"SET_ATTACK_PRIORITY_THING", "SET_ATTACK_PRIORITY_KIND_OF", "SET_DEFAULT_ATTACK_PRIORITY"}))
		known[Param::ATTACK_PRIORITY_SET].insert(n);
	known[Param::ATTACK_PRIORITY_SET];
	// Counters and flags: what an action sets (EditParameter::loadActionParameter).
	forEachScript(sides, [&check](const ScriptPath &, const MapModel::Script &s) {
		for (const auto *list : {&s.actions, &s.falseActions})
			for (const MapModel::ScriptCall &a : *list)
				for (const MapModel::ScriptParameter &p : a.parameters)
				{
					if (p.type == Param::COUNTER)
						check.counters.insert(p.text);
					else if (p.type == Param::FLAG)
						check.flags.insert(p.text);
				}
	});
	if (gameNames)
	{
		for (std::int32_t type : {Param::SOUND, Param::DIALOG, Param::MUSIC, Param::MOVIE, Param::SPECIAL_POWER, Param::SCIENCE,
				 Param::SCIENCE_AVAILABILITY, Param::UPGRADE, Param::COMMAND_BUTTON, Param::LOCALIZED_TEXT, Param::OBJECT_PANEL_FLAG,
				 Param::OBJECT_TYPE})
		{
			const std::vector<std::string> names = gameNames(type);
			if (names.empty())
				continue;
			std::set<std::string> &set = known[type];
			for (const std::string &n : names)
				set.insert(n);
		}
		for (const std::string &n : gameNames(Param::FLAG))
			check.flags.insert(n);
		if (known.count(Param::OBJECT_TYPE))
			for (const std::string &n : usedNames(sides, templates, Param::OBJECT_TYPE_LIST, {"OBJECTLIST_ADDOBJECTTYPE"}))
				known[Param::OBJECT_TYPE].insert(n);
	}
	check.boundaries = map.heightMap ? static_cast<int>(map.heightMap->boundaries.size()) : 0;
	return check;
}

std::string parameterWarning(const MapModel::ScriptParameter &p, bool action, const ScriptCheck &check)
{
	const std::string value = p.text.empty() ? std::string("???") : p.text;
	const auto missing = [&check, &p](std::int32_t type) {
		const auto it = check.known.find(type);
		if (it == check.known.end())
			return false;
		if (type == Param::SCIENCE_AVAILABILITY)
		{
			for (const std::string &n : it->second)
				if (n.size() == p.text.size() && std::equal(n.begin(), n.end(), p.text.begin(), [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }))
					return false;
			return true;
		}
		return it->second.count(p.text) == 0;
	};
	const auto text = [&value](const char *pattern) {
		std::string out = pattern;
		const std::size_t at = out.find("%s");
		if (at != std::string::npos)
			out.replace(at, 2, value);
		return out;
	};
	switch (p.type)
	{
		case Param::SCRIPT:
			return missing(Param::SCRIPT) ? text("Script '%s' does not exist.") : std::string();
		case Param::SCRIPT_SUBROUTINE:
			if (check.subroutines.count(p.text))
				return {};
			return missing(Param::SCRIPT) ? text("Script '%s' does not exist.") : text("Script '%s' is not a subroutine.");
		case Param::ATTACK_PRIORITY_SET: return missing(p.type) ? text("Attack priority set '%s' does not exist.") : std::string();
		case Param::WAYPOINT: return missing(p.type) ? text("Waypoint '%s' does not exist.") : std::string();
		case Param::WAYPOINT_PATH: return missing(p.type) ? text("Waypoint path '%s' does not exist.") : std::string();
		case Param::TRIGGER_AREA: return missing(p.type) ? text("Area '%s' does not exist.") : std::string();
		case Param::COMMAND_BUTTON: return missing(p.type) ? text("Command button '%s' does not exist.") : std::string();
		case Param::LOCALIZED_TEXT: return missing(p.type) ? text("Localized string '%s' does not exist.") : std::string();
		case Param::SOUND: return missing(p.type) ? text("Sound '%s' does not exist.") : std::string();
		case Param::TEAM: return missing(p.type) ? text("Team '%s' does not exist.") : std::string();
		case Param::BRIDGE: return missing(p.type) ? text("Bridge '%s' does not exist.") : std::string();
		case Param::UNIT: return missing(p.type) ? text("Unit '%s' does not exist.") : std::string();
		case Param::OBJECT_TYPE: return missing(p.type) ? text("Object type '%s' does not exist.") : std::string();
		case Param::SIDE: return missing(p.type) ? text("Player '%s' does not exist.") : std::string();
		case Param::OBJECT_PANEL_FLAG: return missing(p.type) ? text("Object flag '%s' is unrecognized.") : std::string();
		case Param::COUNTER: return !action && !check.counters.count(p.text) ? text("Counter/Timer '%s' does not exist.") : std::string();
		case Param::FLAG: return !action && !check.flags.count(p.text) ? text("Flag '%s' is never initialized.") : std::string();
		case Param::DIALOG: return missing(p.type) ? text("Dialog '%s' does not exist.") : std::string();
		case Param::MUSIC: return missing(p.type) ? text("Track '%s' does not exist.") : std::string();
		case Param::MOVIE: return missing(p.type) ? text("Movie '%s' does not exist.") : std::string();
		case Param::SPECIAL_POWER: return missing(p.type) ? text("Special Power '%s' does not exist.") : std::string();
		case Param::SCIENCE: return missing(p.type) ? text("Science '%s' does not exist.") : std::string();
		case Param::SCIENCE_AVAILABILITY: return missing(p.type) ? text("Science availability '%s' does not exist.") : std::string();
		case Param::UPGRADE: return missing(p.type) ? text("Upgrade '%s' does not exist.") : std::string();
		case Param::BOUNDARY:
			return p.intValue >= check.boundaries ? "Border " + std::to_string(p.intValue + 1) + " does not exist." : std::string();
		default:
			return {};
	}
}

std::vector<std::string> callWarnings(const MapModel::ScriptCall &call, bool action, const ScriptTemplates &templates, const ScriptCheck &check)
{
	std::vector<std::string> out;
	if (!templates.conditions.empty() && !callTemplate(templates, action, call))
		out.push_back("The game does not know " + (call.internalName.empty() ? "#" + std::to_string(call.type) : call.internalName) + ".");
	for (const MapModel::ScriptParameter &p : call.parameters)
		if (std::string w = parameterWarning(p, action, check); !w.empty())
			out.push_back(std::move(w));
	return out;
}

std::vector<ScriptProblem> scriptProblems(const MapModel::Sides &sides, const ScriptTemplates &templates, const ScriptCheck &check)
{
	std::vector<ScriptProblem> out;
	forEachScript(sides, [&](const ScriptPath &path, const MapModel::Script &s) {
		forEachCall(s, [&](const MapModel::ScriptCall &c, bool action) {
			for (std::string &w : callWarnings(c, action, templates, check))
				out.push_back({path, s.name + ": " + w});
		});
	});
	return out;
}

bool scriptHasProblems(const MapModel::Script &s, const ScriptTemplates &templates, const ScriptCheck &check)
{
	bool found = false;
	forEachCall(s, [&](const MapModel::ScriptCall &c, bool action) {
		found = found || !callWarnings(c, action, templates, check).empty();
	});
	return found;
}

int patchGcObjectTypes(MapModel::Sides &sides, const ScriptCheck &check)
{
	const auto it = check.known.find(Param::OBJECT_TYPE);
	if (it == check.known.end())
		return 0;
	int changed = 0;
	const auto patch = [&](std::vector<MapModel::ScriptCall> &calls) {
		for (MapModel::ScriptCall &c : calls)
			for (MapModel::ScriptParameter &p : c.parameters)
				if (p.type == Param::OBJECT_TYPE && p.text.rfind("GC_", 0) == 0 && !it->second.count(p.text) && it->second.count(p.text.substr(3)))
				{
					p.text = p.text.substr(3);
					++changed;
				}
	};
	const auto script = [&](MapModel::Script &s) {
		for (MapModel::OrCondition &o : s.conditions)
			patch(o.conditions);
		patch(s.actions);
		patch(s.falseActions);
	};
	for (MapModel::Side &side : sides.sides)
	{
		for (MapModel::Script &s : side.scripts.scripts)
			script(s);
		for (MapModel::ScriptGroup &g : side.scripts.groups)
			for (MapModel::Script &s : g.scripts)
				script(s);
	}
	return changed;
}

void EditScripts::apply(MapModel::Map &map)
{
	for (const SideLists &l : m_lists)
		if (map.sides && l.side < static_cast<int>(map.sides->sides.size()))
			map.sides->sides[static_cast<std::size_t>(l.side)].scripts = l.after;
}

void EditScripts::revert(MapModel::Map &map)
{
	for (const SideLists &l : m_lists)
		if (map.sides && l.side < static_cast<int>(map.sides->sides.size()))
			map.sides->sides[static_cast<std::size_t>(l.side)].scripts = l.before;
}

Changes EditScripts::changes() const
{
	Changes c;
	c.scripts = true;
	return c;
}

bool EditScripts::mergeWith(const Command &next)
{
	const auto *other = dynamic_cast<const EditScripts *>(&next);
	if (!m_mergeable || !other || !other->m_mergeable || other->m_name != m_name || other->m_lists.size() != m_lists.size())
		return false;
	for (std::size_t i = 0; i < m_lists.size(); ++i)
		if (m_lists[i].side != other->m_lists[i].side)
			return false;
	for (std::size_t i = 0; i < m_lists.size(); ++i)
		m_lists[i].after = other->m_lists[i].after;
	return true;
}

std::vector<MapModel::ScriptList> scriptLists(const MapModel::Map &map)
{
	std::vector<MapModel::ScriptList> lists;
	if (map.sides)
		for (const MapModel::Side &side : map.sides->sides)
			lists.push_back(side.scripts);
	return lists;
}

std::unique_ptr<EditScripts> scriptsStep(const MapModel::Map &map, std::vector<MapModel::ScriptList> before, std::string name, bool mergeable)
{
	if (!map.sides || before.size() != map.sides->sides.size())
		return nullptr;
	std::vector<EditScripts::SideLists> changed;
	for (std::size_t i = 0; i < before.size(); ++i)
	{
		const MapModel::ScriptList &now = map.sides->sides[i].scripts;
		if (now == before[i])
			continue;
		changed.push_back({static_cast<int>(i), std::move(before[i]), now});
	}
	if (changed.empty())
		return nullptr;
	return std::make_unique<EditScripts>(std::move(name), std::move(changed), mergeable);
}

} // namespace MapEditorCore
