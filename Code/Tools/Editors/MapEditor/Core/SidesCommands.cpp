/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SidesCommands.cpp -- players and teams.

#include "Core/SidesCommands.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace MapEditorCore
{

namespace
{

std::string ascii(const MapModel::PropertyMap &p, const char *key)
{
	return p.getAscii(key).value_or(std::string());
}

std::string joinNames(const std::vector<std::string> &names)
{
	std::string out;
	for (const std::string &n : names)
	{
		if (!out.empty())
			out += ' ';
		out += n;
	}
	return out;
}

// SidesList::validateAllyEnemyList: only other players that exist.
bool validateNameList(const MapModel::Sides &sides, const std::string &self, std::string &names)
{
	std::vector<std::string> kept;
	for (const std::string &n : nameList(names))
		if (n != self && findPlayer(sides, n) >= 0)
			kept.push_back(n);
	const std::string joined = joinNames(kept);
	const bool changed = joined != names;
	names = joined;
	return changed;
}

void addDefaultTeam(MapModel::Sides &sides, const std::string &player)
{
	MapModel::PropertyMap team;
	team.set("teamName", "team" + player);
	team.set("teamOwner", player);
	team.set("teamIsSingleton", true);
	sides.teams.push_back(team);
}

void renameObjectsTeam(MapModel::Map &map, const std::string &from, const std::string &to)
{
	if (!map.objects)
		return;
	for (MapModel::MapObject &o : *map.objects)
		if (o.properties.getAscii("originalOwner") == from)
			o.properties.set("originalOwner", to);
}

} // namespace

std::string playerName(const MapModel::Side &side)
{
	return ascii(side.properties, "playerName");
}

std::string teamName(const MapModel::PropertyMap &team)
{
	return ascii(team, "teamName");
}

std::string teamOwner(const MapModel::PropertyMap &team)
{
	return ascii(team, "teamOwner");
}

int findPlayer(const MapModel::Sides &sides, const std::string &name)
{
	for (std::size_t i = 0; i < sides.sides.size(); ++i)
		if (playerName(sides.sides[i]) == name)
			return static_cast<int>(i);
	return -1;
}

int findTeam(const MapModel::Sides &sides, const std::string &name)
{
	for (std::size_t i = 0; i < sides.teams.size(); ++i)
		if (teamName(sides.teams[i]) == name)
			return static_cast<int>(i);
	return -1;
}

bool isDefaultTeam(const MapModel::Sides &sides, const MapModel::PropertyMap &team)
{
	const std::string name = teamName(team);
	return name.rfind("team", 0) == 0 && findPlayer(sides, name.substr(4)) >= 0;
}

std::string validPlayerName(const std::string &name)
{
	std::string out = name;
	for (char &c : out)
		if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
			c = '_';
	return out;
}

std::vector<std::string> nameList(const std::string &names)
{
	std::vector<std::string> out;
	std::string token;
	for (char c : names)
	{
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
		{
			if (!token.empty())
				out.push_back(token);
			token.clear();
		}
		else
			token += c;
	}
	if (!token.empty())
		out.push_back(token);
	return out;
}

bool validateSides(MapModel::Sides &sides)
{
	bool modified = false;
	bool hasNeutral = false;
	for (const MapModel::Side &s : sides.sides)
		hasNeutral = hasNeutral || playerName(s).empty();
	if (!hasNeutral)
	{
		// SidesList::addPlayerByTemplate with no template.
		MapModel::Side neutral;
		neutral.properties.set("playerName", std::string());
		neutral.properties.set("playerIsHuman", false);
		neutral.properties.set("playerDisplayName", std::u16string(u"Neutral"));
		neutral.properties.set("playerFaction", std::string());
		neutral.properties.set("playerAllies", std::string());
		neutral.properties.set("playerEnemies", std::string());
		sides.sides.push_back(neutral);
		addDefaultTeam(sides, std::string());
		modified = true;
	}
	for (MapModel::Side &side : sides.sides)
	{
		const std::string player = playerName(side);
		const int t = findTeam(sides, "team" + player);
		if (t < 0)
		{
			addDefaultTeam(sides, player);
			modified = true;
		}
		else
		{
			MapModel::PropertyMap &team = sides.teams[static_cast<std::size_t>(t)];
			if (teamOwner(team) != player)
			{
				team.set("teamOwner", player);
				modified = true;
			}
			if (team.getBool("teamIsSingleton") != true)
			{
				team.set("teamIsSingleton", true);
				modified = true;
			}
		}
		for (const char *key : {"playerAllies", "playerEnemies"})
		{
			std::string names = ascii(side.properties, key);
			if (validateNameList(sides, player, names))
			{
				side.properties.set(key, names);
				modified = true;
			}
		}
	}
	// A team named like a player goes; the player wins.
	const auto before = sides.teams.size();
	std::erase_if(sides.teams, [&sides](const MapModel::PropertyMap &team) { return findPlayer(sides, teamName(team)) >= 0; });
	modified = modified || sides.teams.size() != before;
	for (MapModel::PropertyMap &team : sides.teams)
	{
		const std::string owner = teamOwner(team);
		if (findPlayer(sides, owner) < 0 || owner == teamName(team))
		{
			team.set("teamOwner", std::string());
			modified = true;
		}
	}
	return modified;
}

Relation relation(const MapModel::Sides &sides, int from, int to)
{
	if (from < 0 || to < 0 || from >= static_cast<int>(sides.sides.size()) || to >= static_cast<int>(sides.sides.size()))
		return Relation::Neutral;
	const MapModel::PropertyMap &p = sides.sides[static_cast<std::size_t>(from)].properties;
	const std::string other = playerName(sides.sides[static_cast<std::size_t>(to)]);
	const auto contains = [&other](const std::string &names) {
		const std::vector<std::string> list = nameList(names);
		return std::find(list.begin(), list.end(), other) != list.end();
	};
	if (contains(ascii(p, "playerAllies")))
		return Relation::Ally;
	if (contains(ascii(p, "playerEnemies")))
		return Relation::Enemy;
	return Relation::Neutral;
}

void setRelation(MapModel::Sides &sides, int from, int to, Relation r)
{
	if (from < 0 || to < 0 || from == to || from >= static_cast<int>(sides.sides.size()) || to >= static_cast<int>(sides.sides.size()))
		return;
	MapModel::PropertyMap &p = sides.sides[static_cast<std::size_t>(from)].properties;
	const std::string other = playerName(sides.sides[static_cast<std::size_t>(to)]);
	if (other.empty() || playerName(sides.sides[static_cast<std::size_t>(from)]).empty())
		return;
	for (const char *key : {"playerAllies", "playerEnemies"})
	{
		std::vector<std::string> list = nameList(ascii(p, key));
		std::erase(list, other);
		if ((r == Relation::Ally && std::string(key) == "playerAllies") || (r == Relation::Enemy && std::string(key) == "playerEnemies"))
			list.push_back(other);
		p.set(key, joinNames(list));
	}
}

std::string freePlayerName(const MapModel::Sides &sides)
{
	for (int n = 1;; ++n)
	{
		char name[32];
		std::snprintf(name, sizeof(name), "player%04d", n);
		if (findPlayer(sides, name) < 0 && findTeam(sides, name) < 0)
			return name;
	}
}

int addPlayer(MapModel::Sides &sides, const std::string &name, const std::u16string &displayName, const std::string &faction, bool human)
{
	const std::string valid = validPlayerName(name);
	if (valid.empty() || static_cast<int>(sides.sides.size()) >= MAX_PLAYERS - 1 || findPlayer(sides, valid) >= 0)
		return -1;
	MapModel::Side side;
	side.properties.set("playerName", valid);
	side.properties.set("playerIsHuman", human);
	side.properties.set("playerDisplayName", displayName);
	side.properties.set("playerFaction", faction);
	side.properties.set("playerEnemies", std::string());
	side.properties.set("playerAllies", std::string());
	sides.sides.push_back(side);
	validateSides(sides);
	return findPlayer(sides, valid);
}

bool removePlayer(MapModel::Sides &sides, int index)
{
	if (index < 0 || index >= static_cast<int>(sides.sides.size()) || sides.sides.size() <= 1)
		return false;
	const std::string name = playerName(sides.sides[static_cast<std::size_t>(index)]);
	if (name.empty())
		return false;
	sides.sides.erase(sides.sides.begin() + index);
	std::erase_if(sides.teams, [&name](const MapModel::PropertyMap &team) { return teamOwner(team) == name; });
	validateSides(sides);
	return true;
}

bool renamePlayer(MapModel::Map &map, int index, const std::string &name)
{
	if (!map.sides || index < 0 || index >= static_cast<int>(map.sides->sides.size()))
		return false;
	MapModel::Sides &sides = *map.sides;
	const std::string oldName = playerName(sides.sides[static_cast<std::size_t>(index)]);
	const std::string newName = validPlayerName(name);
	if (oldName.empty() || newName.empty() || newName == oldName || findPlayer(sides, newName) >= 0 || findTeam(sides, newName) >= 0)
		return false;
	sides.sides[static_cast<std::size_t>(index)].properties.set("playerName", newName);
	for (MapModel::PropertyMap &team : sides.teams)
		if (teamOwner(team) == oldName)
			team.set("teamOwner", newName);
	// Allies and enemies of the others name the player too.
	for (MapModel::Side &side : sides.sides)
		for (const char *key : {"playerAllies", "playerEnemies"})
		{
			std::vector<std::string> list = nameList(ascii(side.properties, key));
			if (std::find(list.begin(), list.end(), oldName) == list.end())
				continue;
			std::replace(list.begin(), list.end(), oldName, newName);
			side.properties.set(key, joinNames(list));
		}
	const int t = findTeam(sides, "team" + oldName);
	if (t >= 0)
	{
		sides.teams[static_cast<std::size_t>(t)].set("teamName", "team" + newName);
		renameObjectsTeam(map, "team" + oldName, "team" + newName);
	}
	validateSides(sides);
	return true;
}

int addSkirmishPlayers(MapModel::Sides &sides, bool zeroHour)
{
	static const char *const BASE[][2] = {{"FactionCivilian", "PlyrCivilian"}, {"FactionAmerica", "SkirmishAmerica"},
		{"FactionChina", "SkirmishChina"}, {"FactionGLA", "SkirmishGLA"}};
	static const char *const GENERALS[] = {"AmericaAirForceGeneral", "AmericaLaserGeneral", "AmericaSuperWeaponGeneral",
		"ChinaTankGeneral", "ChinaNukeGeneral", "ChinaInfantryGeneral", "GLADemolitionGeneral", "GLAToxinGeneral", "GLAStealthGeneral"};
	std::vector<std::pair<std::string, std::string>> players;
	for (const auto &p : BASE)
		players.emplace_back(p[0], p[1]);
	if (zeroHour)
		for (const char *g : GENERALS)
			players.emplace_back(std::string("Faction") + g, std::string("Skirmish") + g);
	int added = 0;
	for (const auto &[faction, name] : players)
	{
		if (findPlayer(sides, name) >= 0)
			continue;
		const std::u16string display(name.begin(), name.end());
		if (addPlayer(sides, name, display, faction, false) >= 0)
			++added;
	}
	return added;
}

int addTeam(MapModel::Sides &sides, const std::string &owner)
{
	std::string name;
	for (int n = 1;; ++n)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "team%04d", n);
		name = buffer;
		if (findTeam(sides, name) < 0 && findPlayer(sides, name.substr(4)) < 0)
			break;
	}
	MapModel::PropertyMap team;
	team.set("teamName", name);
	team.set("teamOwner", owner);
	team.set("teamIsSingleton", false);
	sides.teams.push_back(team);
	return static_cast<int>(sides.teams.size()) - 1;
}

int copyTeam(MapModel::Sides &sides, int index)
{
	if (index < 0 || index >= static_cast<int>(sides.teams.size()) || isDefaultTeam(sides, sides.teams[static_cast<std::size_t>(index)]))
		return -1;
	MapModel::PropertyMap copy = sides.teams[static_cast<std::size_t>(index)];
	const std::string base = teamName(copy);
	// The old dialog wrote "%s.%2d", which puts a space into the name; two digits keep it whole.
	for (int n = 1;; ++n)
	{
		char suffix[32];
		std::snprintf(suffix, sizeof(suffix), ".%02d", n);
		if (findTeam(sides, base + suffix) < 0)
		{
			copy.set("teamName", base + suffix);
			break;
		}
	}
	sides.teams.insert(sides.teams.begin() + index + 1, copy);
	return index + 1;
}

bool removeTeam(MapModel::Sides &sides, int index)
{
	if (index < 0 || index >= static_cast<int>(sides.teams.size()) || isDefaultTeam(sides, sides.teams[static_cast<std::size_t>(index)]))
		return false;
	sides.teams.erase(sides.teams.begin() + index);
	return true;
}

bool moveTeam(MapModel::Sides &sides, int index, int delta)
{
	const int other = index + delta;
	if ((delta != -1 && delta != 1) || index < 0 || other < 0 || index >= static_cast<int>(sides.teams.size())
		|| other >= static_cast<int>(sides.teams.size()))
		return false;
	if (isDefaultTeam(sides, sides.teams[static_cast<std::size_t>(index)]) || isDefaultTeam(sides, sides.teams[static_cast<std::size_t>(other)]))
		return false;
	std::swap(sides.teams[static_cast<std::size_t>(index)], sides.teams[static_cast<std::size_t>(other)]);
	return true;
}

bool renameTeam(MapModel::Map &map, int index, const std::string &name)
{
	if (!map.sides || index < 0 || index >= static_cast<int>(map.sides->teams.size()))
		return false;
	MapModel::Sides &sides = *map.sides;
	MapModel::PropertyMap &team = sides.teams[static_cast<std::size_t>(index)];
	const std::string oldName = teamName(team);
	if (name.empty() || name == oldName || isDefaultTeam(sides, team) || findTeam(sides, name) >= 0 || findPlayer(sides, name) >= 0
		|| name.find(' ') != std::string::npos)
		return false;
	team.set("teamName", name);
	// A name like "team<player>" would make it that player's default team.
	if (isDefaultTeam(sides, team))
	{
		team.set("teamName", oldName);
		return false;
	}
	renameObjectsTeam(map, oldName, name);
	return true;
}

std::vector<std::int32_t> teamObjects(const MapModel::Map &map, const std::string &team)
{
	std::vector<std::int32_t> out;
	if (!map.objects)
		return out;
	for (std::size_t i = 0; i < map.objects->size(); ++i)
		if ((*map.objects)[i].properties.getAscii("originalOwner") == team)
			out.push_back(static_cast<std::int32_t>(i));
	return out;
}

int playerObjectCount(const MapModel::Map &map, int index)
{
	if (!map.sides || index < 0 || index >= static_cast<int>(map.sides->sides.size()))
		return 0;
	const std::string player = playerName(map.sides->sides[static_cast<std::size_t>(index)]);
	int count = 0;
	for (const MapModel::PropertyMap &team : map.sides->teams)
		if (teamOwner(team) == player)
			count += static_cast<int>(teamObjects(map, teamName(team)).size());
	return count;
}

Changes EditSides::changes() const
{
	Changes c;
	c.sides = true;
	// Team colours of the objects come from their owners.
	c.rebuild = true;
	return c;
}

bool EditSides::mergeWith(const Command &next)
{
	const auto *other = dynamic_cast<const EditSides *>(&next);
	if (!m_mergeable || !other || !other->m_mergeable || other->m_name != m_name || m_before.objects || other->m_before.objects)
		return false;
	m_after = other->m_after;
	return true;
}

void EditSides::set(MapModel::Map &map, const State &state)
{
	map.sides = state.sides;
	if (state.objects)
		map.objects = state.objects;
}

EditSides::State sidesState(const MapModel::Map &map)
{
	EditSides::State s;
	s.sides = map.sides;
	s.objects = map.objects;
	return s;
}

std::unique_ptr<EditSides> sidesStep(const MapModel::Map &map, EditSides::State before, std::string name, bool mergeable)
{
	EditSides::State after = sidesState(map);
	if (before.objects == after.objects)
	{
		before.objects.reset();
		after.objects.reset();
	}
	if (before.sides == after.sides && !before.objects)
		return nullptr;
	return std::make_unique<EditSides>(std::move(name), std::move(before), std::move(after), mergeable);
}

} // namespace MapEditorCore
