/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "App/ScriptCatalog.h"

#include "App/EngineClient.h"
#include "App/Widgets.h"
#include "Core/RoadCommands.h"
#include "Core/SidesCommands.h"
#include "Core/WaypointCommands.h"

#include <set>

namespace MapEditorApp
{

namespace Param = MapEditorCore::Param;

const MapEditorCore::ScriptTemplates &ScriptCatalog::templates()
{
	if (m_templates.conditions.empty() && EngineClient::instance().isStarted())
	{
		for (bool action : {false, true})
		{
			std::vector<MapEditorCore::ScriptTemplate> &list = action ? m_templates.actions : m_templates.conditions;
			for (const MeScriptTemplate &m : EngineClient::instance().scriptTemplates(action))
			{
				MapEditorCore::ScriptTemplate t;
				t.internalName = m.internalName;
				t.uiName = m.uiName;
				t.uiName2 = m.uiName2;
				t.help = m.help;
				for (int32_t i = 0; i < m.numParameters && i < ME_SCRIPT_MAX_PARAMETERS; ++i)
					t.parameterTypes.push_back(m.parameterTypes[i]);
				for (int32_t i = 0; i < m.numUiStrings && i < ME_SCRIPT_MAX_PARAMETERS; ++i)
					t.uiStrings.push_back(m.uiStrings[i]);
				list.push_back(std::move(t));
			}
		}
	}
	return m_templates;
}

MapEditorCore::ValueNames ScriptCatalog::valueNames()
{
	return [](std::int32_t type, std::int32_t value) -> std::string {
		for (const MeNamedValue &v : EngineClient::instance().scriptValues(type))
			if (v.value == value)
				return v.name;
		return std::string();
	};
}

QString ScriptCatalog::callText(const MapModel::ScriptCall &call, bool action)
{
	const MapEditorCore::ScriptTemplate *t = MapEditorCore::callTemplate(templates(), action, call);
	return utf8(MapEditorCore::callText(call, t, valueNames()));
}

QStringList ScriptCatalog::parameterChoices(const MapModel::Map *mapOrNull, std::int32_t type)
{
	QStringList out;
	if (!mapOrNull)
		return out;
	const MapModel::Map &map = *mapOrNull;
	const MapModel::Sides empty;
	const MapModel::Sides &sides = map.sides ? *map.sides : empty;
	const MapEditorCore::ScriptTemplates &templates = this->templates();
	const auto addAll = [&out](const std::vector<std::string> &names) {
		for (const std::string &n : names)
			out << utf8(n);
	};
	const auto engineList = [&out, type] {
		for (const MeNamedValue &v : EngineClient::instance().scriptValues(type))
			out << utf8(v.name);
	};
	switch (type)
	{
		case Param::TEAM:
			out << QString::fromLatin1(MapEditorCore::THIS_TEAM);
			for (const MapModel::PropertyMap &team : sides.teams)
				if (MapEditorCore::teamName(team) != "team")
					out << utf8(MapEditorCore::teamName(team));
			break;
		case Param::SIDE:
			out << QString::fromLatin1(MapEditorCore::LOCAL_PLAYER) << QString::fromLatin1(MapEditorCore::THIS_PLAYER)
				<< QString::fromLatin1(MapEditorCore::THIS_PLAYER_ENEMY);
			for (const MapModel::Side &s : sides.sides)
				if (!MapEditorCore::playerName(s).empty())
					out << utf8(MapEditorCore::playerName(s));
			break;
		case Param::UNIT:
		{
			out << QString::fromLatin1(MapEditorCore::THIS_OBJECT);
			std::set<std::string> names;
			if (map.objects)
				for (const MapModel::MapObject &o : *map.objects)
					if ((o.flags & (MapEditorCore::ROAD_POINT1 | MapEditorCore::ROAD_POINT2)) == 0)
						if (const auto n = o.properties.getAscii("objectName"); n && !n->empty())
							names.insert(*n);
			for (const MapModel::Side &s : sides.sides)
				for (const MapModel::BuildListEntry &b : s.buildList)
					if (!b.buildingName.empty())
						names.insert(b.buildingName);
			for (const std::string &n : MapEditorCore::usedNames(sides, templates, Param::UNIT, {"CREATE_NAMED_ON_TEAM_AT_WAYPOINT", "UNIT_SPAWN_NAMED_LOCATION_ORIENTATION"}))
				names.insert(n);
			addAll({names.begin(), names.end()});
			break;
		}
		case Param::BRIDGE:
			if (map.objects)
				for (std::size_t i = 0; i + 1 < map.objects->size(); ++i)
				{
					const MapModel::MapObject &o = (*map.objects)[i];
					if ((o.flags & MapEditorCore::BRIDGE_POINT1) && ((*map.objects)[i + 1].flags & MapEditorCore::BRIDGE_POINT2))
						if (const auto n = o.properties.getAscii("objectName"); n && !n->empty())
							out << utf8(*n);
				}
			break;
		case Param::WAYPOINT:
		case Param::WAYPOINT_PATH:
		{
			std::set<std::string> names;
			if (map.objects)
				for (const MapModel::MapObject &o : *map.objects)
				{
					if (!MapEditorCore::isWaypoint(o))
						continue;
					if (type == Param::WAYPOINT)
					{
						if (const auto n = o.properties.getAscii("waypointName"); n && !n->empty())
							names.insert(*n);
					}
					else
						for (const char *key : {"waypointPathLabel1", "waypointPathLabel2", "waypointPathLabel3"})
							if (const auto n = o.properties.getAscii(key); n && !n->empty())
								names.insert(*n);
				}
			addAll({names.begin(), names.end()});
			break;
		}
		case Param::TRIGGER_AREA:
			if (map.polygons)
				for (const MapModel::PolygonArea &a : *map.polygons)
					out << utf8(a.name);
			out << QStringLiteral("[Skirmish]MyInnerPerimeter") << QStringLiteral("[Skirmish]MyOuterPerimeter")
				<< QStringLiteral("[Skirmish]EnemyOuterPerimeter") << QStringLiteral("[Skirmish]EnemyInnerPerimeter") << QStringLiteral("Water Grid");
			break;
		case Param::SCRIPT:
		case Param::SCRIPT_SUBROUTINE:
			addAll(MapEditorCore::scriptNames(sides, type == Param::SCRIPT_SUBROUTINE));
			break;
		case Param::COUNTER:
		case Param::FONT_NAME:
			addAll(MapEditorCore::usedNames(sides, templates, type));
			break;
		case Param::FLAG:
			addAll(MapEditorCore::usedNames(sides, templates, type));
			engineList();
			break;
		case Param::ATTACK_PRIORITY_SET:
			addAll(MapEditorCore::usedNames(sides, templates, type, {"SET_ATTACK_PRIORITY_THING", "SET_ATTACK_PRIORITY_KIND_OF", "SET_DEFAULT_ATTACK_PRIORITY"}));
			break;
		case Param::OBJECT_TYPE_LIST:
			addAll(MapEditorCore::usedNames(sides, templates, type, {"OBJECTLIST_ADDOBJECTTYPE"}));
			break;
		case Param::REVEALNAME:
			addAll(MapEditorCore::usedNames(sides, templates, type, {"MAP_REVEAL_PERMANENTLY_AT_WAYPOINT"}));
			break;
		case Param::OBJECT_TYPE:
		{
			addAll(MapEditorCore::usedNames(sides, templates, Param::OBJECT_TYPE_LIST, {"OBJECTLIST_ADDOBJECTTYPE"}));
			QStringList objects;
			for (const MeTemplateInfo &info : EngineClient::instance().catalog())
				objects << QString::fromLatin1(info.name);
			objects.sort(Qt::CaseInsensitive);
			out << objects;
			break;
		}
		default:
			engineList();
			break;
	}
	out.removeDuplicates();
	return out;
}

MapEditorCore::ScriptCheck ScriptCatalog::check(const MapModel::Map *mapOrNull)
{
	if (!mapOrNull)
		return {};
	EngineClient &engine = EngineClient::instance();
	const bool started = engine.isStarted();
	return MapEditorCore::scriptCheck(*mapOrNull, templates(), [&engine, started](std::int32_t type) {
		std::vector<std::string> names;
		if (!started)
			return names;
		if (type == Param::OBJECT_TYPE)
		{
			for (const MeTemplateInfo &info : engine.catalog())
				names.push_back(info.name);
			return names;
		}
		for (const MeNamedValue &v : engine.scriptValues(type))
			names.push_back(v.name);
		return names;
	});
}

} // namespace MapEditorApp
