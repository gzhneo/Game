/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapCheck.cpp -- the map check behind the problem list.

#include "Core/MapCheck.h"

#include "Core/PlacementCommands.h"
#include "Core/RoadCommands.h"
#include "Core/SelectionTools.h"
#include "Core/SidesCommands.h"
#include "Core/WaypointCommands.h"

#include <algorithm>
#include <map>
#include <set>

namespace MapEditorCore
{

namespace
{

constexpr float CELL = 10.0f;	// world units per height sample
constexpr int TEAM_UNIT_SLOTS = 7;

std::string unitKey(int slot)
{
	return "teamUnitType" + std::to_string(slot);
}

// Objects that stand for something other than a thing of the game: waypoints, road and bridge
// points, scorch marks and the editor's own markers ("*Waypoints/...", "*Lights/...").
bool isThing(const MapModel::MapObject &o)
{
	return !isWaypoint(o) && !isRoadOrBridgePoint(o) && !isScorch(o) && !o.templateName.empty() && o.templateName[0] != '*';
}

std::string defaultTeam(const std::string &player)
{
	return "team" + player;
}

bool startsWith(const std::string &s, const std::string &prefix)
{
	return s.compare(0, prefix.size(), prefix) == 0;
}

void checkTemplates(const MapModel::Map &map, const MapCheckInput &input, std::vector<Problem> &out)
{
	if (!input.templateKnown)
		return;
	std::map<std::string, Problem> missing;
	const auto use = [&missing, &input](const std::string &name) -> Problem * {
		if (name.empty() || input.templateKnown(name))
			return nullptr;
		Problem &p = missing[name];
		p.kind = ProblemKind::UnknownTemplate;
		p.subject = name;
		return &p;
	};
	if (map.objects)
	{
		for (std::size_t i = 0; i < map.objects->size(); ++i)
		{
			const MapModel::MapObject &o = (*map.objects)[i];
			if (!isThing(o))
				continue;
			if (Problem *p = use(o.templateName))
				p->objects.push_back(static_cast<std::int32_t>(i));
		}
	}
	if (map.sides)
	{
		for (const MapModel::Side &side : map.sides->sides)
			for (const MapModel::BuildListEntry &e : side.buildList)
				if (Problem *p = use(e.templateName))
					++p->buildEntries;
		for (const MapModel::PropertyMap &team : map.sides->teams)
			for (int slot = 1; slot <= TEAM_UNIT_SLOTS; ++slot)
				if (Problem *p = use(team.getAscii(unitKey(slot)).value_or("")))
					++p->teamUnits;
	}
	for (auto &[name, p] : missing)
	{
		p.suggestion = replacementName(name, input.templateKnown);
		out.push_back(std::move(p));
	}
}

void checkSides(const MapModel::Map &map, const MapCheckInput &input, std::vector<Problem> &out)
{
	if (!map.sides)
		return;
	const MapModel::Sides &sides = *map.sides;
	if (input.factionKnown)
	{
		for (std::size_t i = 0; i < sides.sides.size(); ++i)
		{
			const std::string faction = sides.sides[i].properties.getAscii("playerFaction").value_or("");
			// The neutral player has none.
			if (faction.empty() || input.factionKnown(faction))
				continue;
			Problem p;
			p.kind = ProblemKind::UnknownFaction;
			p.subject = faction;
			p.side = static_cast<int>(i);
			p.suggestion = replacementName(faction, input.factionKnown);
			out.push_back(std::move(p));
		}
	}
	std::set<std::string> seen;
	for (std::size_t i = 0; i < sides.teams.size(); ++i)
	{
		const std::string name = teamName(sides.teams[i]);
		Problem p;
		p.subject = name;
		p.team = static_cast<int>(i);
		if (!seen.insert(name).second)
		{
			p.kind = ProblemKind::DuplicateTeam;
			out.push_back(std::move(p));
		}
		else if (findPlayer(sides, teamOwner(sides.teams[i])) < 0)
		{
			p.kind = ProblemKind::TeamOwner;
			out.push_back(std::move(p));
		}
	}
}

void checkObjectTeams(const MapModel::Map &map, const MapCheckInput &input, std::vector<Problem> &out)
{
	if (!map.objects)
		return;
	std::map<std::string, Problem> invalid;
	for (std::size_t i = 0; i < map.objects->size(); ++i)
	{
		const MapModel::MapObject &o = (*map.objects)[i];
		// Objects without a template need no team (the old check skipped them too).
		if (!isThing(o) || (input.templateKnown && !input.templateKnown(o.templateName)))
			continue;
		const std::string team = o.properties.getAscii("originalOwner").value_or("");
		const int index = map.sides ? findTeam(*map.sides, team) : -1;
		if (index >= 0 && findPlayer(*map.sides, teamOwner(map.sides->teams[static_cast<std::size_t>(index)])) >= 0)
			continue;
		Problem &p = invalid[team];
		p.kind = ProblemKind::ObjectTeam;
		p.subject = team;
		p.objects.push_back(static_cast<std::int32_t>(i));
	}
	for (auto &[team, p] : invalid)
		out.push_back(std::move(p));
}

void checkObjects(const MapModel::Map &map, std::vector<Problem> &out)
{
	if (!map.objects)
		return;
	const std::vector<MapModel::MapObject> &objects = *map.objects;
	std::map<std::string, Problem> duplicates;
	for (const std::int32_t i : findDuplicates(map))
	{
		Problem &p = duplicates[objects[static_cast<std::size_t>(i)].templateName];
		p.kind = ProblemKind::DuplicateObject;
		p.subject = objects[static_cast<std::size_t>(i)].templateName;
		p.objects.push_back(i);
	}
	for (auto &[name, p] : duplicates)
		out.push_back(std::move(p));

	if (map.heightMap && map.heightMap->width > 0)
	{
		const MapModel::HeightMap &hm = *map.heightMap;
		const float x0 = -static_cast<float>(hm.borderSize) * CELL;
		const float y0 = x0;
		const float x1 = static_cast<float>(hm.width - 1 - hm.borderSize) * CELL;
		const float y1 = static_cast<float>(hm.height - 1 - hm.borderSize) * CELL;
		Problem off;
		off.kind = ProblemKind::OffTerrain;
		for (std::size_t i = 0; i < objects.size(); ++i)
			if (objects[i].x < x0 || objects[i].y < y0 || objects[i].x > x1 || objects[i].y > y1)
				off.objects.push_back(static_cast<std::int32_t>(i));
		if (!off.objects.empty())
			out.push_back(std::move(off));
	}

	if (map.waypointLinks)
	{
		std::set<std::int32_t> ids;
		for (const MapModel::MapObject &o : objects)
			if (const std::optional<std::int32_t> id = o.properties.getInt("waypointID"))
				ids.insert(*id);
		std::set<std::int32_t> missing;
		for (const MapModel::WaypointLink &link : *map.waypointLinks)
		{
			if (!ids.count(link.from))
				missing.insert(link.from);
			if (!ids.count(link.to))
				missing.insert(link.to);
		}
		for (const std::int32_t id : missing)
		{
			Problem p;
			p.kind = ProblemKind::WaypointLink;
			p.subject = std::to_string(id);
			p.waypoint = id;
			out.push_back(std::move(p));
		}
	}
}

} // namespace

std::vector<Problem> checkMap(const MapModel::Map &map, const MapCheckInput &input)
{
	std::vector<Problem> out;
	checkTemplates(map, input, out);
	checkSides(map, input, out);
	checkObjectTeams(map, input, out);
	checkObjects(map, out);
	if (map.sides && input.scriptTemplates && input.scriptCheck)
	{
		for (ScriptProblem &s : scriptProblems(*map.sides, *input.scriptTemplates, *input.scriptCheck))
		{
			Problem p;
			p.kind = ProblemKind::Script;
			p.subject = std::move(s.text);
			p.script = s.path;
			out.push_back(std::move(p));
		}
	}
	return out;
}

bool problemFixable(ProblemKind kind)
{
	return kind != ProblemKind::Script;
}

std::optional<std::string> replacementName(const std::string &name, const std::function<bool(const std::string &)> &known)
{
	if (!known)
		return std::nullopt;
	std::string swap;
	if (startsWith(name, "FactionFundamentalist"))
		swap = "FactionGLA" + name.substr(21);
	else if (startsWith(name, "Fundamentalist"))
		swap = "GLA" + name.substr(14);
	else if (startsWith(name, "GC_"))
		swap = name.substr(3);
	if (swap.empty() || !known(swap))
		return std::nullopt;
	return swap;
}

int replaceTemplate(MapModel::Map &map, const std::string &from, const std::string &to)
{
	if (from.empty() || to.empty() || from == to)
		return 0;
	int changed = 0;
	if (map.objects)
	{
		for (MapModel::MapObject &o : *map.objects)
		{
			if (isThing(o) && o.templateName == from)
			{
				o.templateName = to;
				++changed;
			}
		}
	}
	if (map.sides)
	{
		for (MapModel::Side &side : map.sides->sides)
		{
			for (MapModel::BuildListEntry &e : side.buildList)
			{
				if (e.templateName == from)
				{
					e.templateName = to;
					++changed;
				}
			}
		}
		for (MapModel::PropertyMap &team : map.sides->teams)
		{
			for (int slot = 1; slot <= TEAM_UNIT_SLOTS; ++slot)
			{
				if (team.getAscii(unitKey(slot)) == from)
				{
					team.set(unitKey(slot), MapModel::PropertyValue(to));
					++changed;
				}
			}
		}
	}
	return changed;
}

int removeTemplate(MapModel::Map &map, const std::string &templateName)
{
	if (templateName.empty())
		return 0;
	int changed = 0;
	if (map.objects)
	{
		std::vector<std::int32_t> gone;
		for (std::size_t i = 0; i < map.objects->size(); ++i)
			if (isThing((*map.objects)[i]) && (*map.objects)[i].templateName == templateName)
				gone.push_back(static_cast<std::int32_t>(i));
		changed += removeObjects(map, gone);
	}
	if (map.sides)
	{
		for (MapModel::Side &side : map.sides->sides)
		{
			const auto end = std::remove_if(side.buildList.begin(), side.buildList.end(),
				[&templateName](const MapModel::BuildListEntry &e) { return e.templateName == templateName; });
			changed += static_cast<int>(side.buildList.end() - end);
			side.buildList.erase(end, side.buildList.end());
		}
		for (MapModel::PropertyMap &team : map.sides->teams)
		{
			for (int slot = 1; slot <= TEAM_UNIT_SLOTS; ++slot)
			{
				if (team.getAscii(unitKey(slot)) == templateName)
				{
					// An empty slot as the teams dialog leaves it.
					team.set(unitKey(slot), MapModel::PropertyValue(std::string()));
					team.set("teamUnitMinCount" + std::to_string(slot), MapModel::PropertyValue(std::int32_t(0)));
					team.set("teamUnitMaxCount" + std::to_string(slot), MapModel::PropertyValue(std::int32_t(0)));
					++changed;
				}
			}
		}
	}
	return changed;
}

int setObjectsTeam(MapModel::Map &map, const std::vector<std::int32_t> &objects, const std::string &player)
{
	if (!map.objects || !map.sides)
		return 0;
	std::string team = defaultTeam(player);
	// As the old fix: the neutral team if the player has no default team.
	if (findTeam(*map.sides, team) < 0)
		team = defaultTeam("");
	int changed = 0;
	for (const std::int32_t i : objects)
	{
		if (i < 0 || i >= static_cast<std::int32_t>(map.objects->size()))
			continue;
		MapModel::MapObject &o = (*map.objects)[static_cast<std::size_t>(i)];
		if (o.properties.getAscii("originalOwner") != team)
		{
			o.properties.set("originalOwner", MapModel::PropertyValue(team));
			++changed;
		}
	}
	return changed;
}

int setTeamOwner(MapModel::Sides &sides, int team, const std::string &player)
{
	if (team < 0 || team >= static_cast<int>(sides.teams.size()) || findPlayer(sides, player) < 0)
		return 0;
	MapModel::PropertyMap &t = sides.teams[static_cast<std::size_t>(team)];
	if (t.getAscii("teamOwner") == player)
		return 0;
	t.set("teamOwner", MapModel::PropertyValue(player));
	return 1;
}

int removeTeamAt(MapModel::Sides &sides, int team)
{
	if (team < 0 || team >= static_cast<int>(sides.teams.size()))
		return 0;
	const std::string name = teamName(sides.teams[static_cast<std::size_t>(team)]);
	// A second team of a name goes even if the name is a default team's; the first stays.
	const bool duplicate = std::any_of(sides.teams.begin(), sides.teams.begin() + team,
		[&name](const MapModel::PropertyMap &t) { return teamName(t) == name; });
	if (!duplicate && isDefaultTeam(sides, sides.teams[static_cast<std::size_t>(team)]))
		return 0;
	sides.teams.erase(sides.teams.begin() + team);
	return 1;
}

int setFaction(MapModel::Sides &sides, int side, const std::string &faction)
{
	if (side < 0 || side >= static_cast<int>(sides.sides.size()) || faction.empty())
		return 0;
	MapModel::PropertyMap &p = sides.sides[static_cast<std::size_t>(side)].properties;
	if (p.getAscii("playerFaction") == faction)
		return 0;
	p.set("playerFaction", MapModel::PropertyValue(faction));
	return 1;
}

int removeObjects(MapModel::Map &map, std::vector<std::int32_t> objects)
{
	if (!map.objects)
		return 0;
	std::sort(objects.begin(), objects.end());
	objects.erase(std::unique(objects.begin(), objects.end()), objects.end());
	int changed = 0;
	for (auto it = objects.rbegin(); it != objects.rend(); ++it)
	{
		if (*it < 0 || *it >= static_cast<std::int32_t>(map.objects->size()))
			continue;
		// A waypoint takes its links along, as deleting it in the view does.
		if (const std::optional<std::int32_t> id = (*map.objects)[static_cast<std::size_t>(*it)].properties.getInt("waypointID"))
			removeWaypointLinks(map, *id);
		map.objects->erase(map.objects->begin() + *it);
		++changed;
	}
	return changed;
}

int removeWaypointLinks(MapModel::Map &map, std::int32_t waypoint)
{
	if (!map.waypointLinks)
		return 0;
	std::vector<MapModel::WaypointLink> &links = *map.waypointLinks;
	const auto end = std::remove_if(links.begin(), links.end(),
		[waypoint](const MapModel::WaypointLink &l) { return l.from == waypoint || l.to == waypoint; });
	const int changed = static_cast<int>(links.end() - end);
	links.erase(end, links.end());
	return changed;
}

} // namespace MapEditorCore
