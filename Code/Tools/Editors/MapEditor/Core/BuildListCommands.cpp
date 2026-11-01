/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// BuildListCommands.cpp -- a player's build list.

#include "Core/BuildListCommands.h"

#include "Core/SidesCommands.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace MapEditorCore
{

namespace
{

std::vector<MapModel::BuildListEntry> *listOf(MapModel::Sides &sides, int side)
{
	if (side < 0 || side >= static_cast<int>(sides.sides.size()))
		return nullptr;
	return &sides.sides[static_cast<std::size_t>(side)].buildList;
}

bool sameName(const std::string &a, const std::string &b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
	});
}

} // namespace

MapModel::BuildListEntry newBuildEntry(const std::string &templateName, float x, float y, float angle)
{
	MapModel::BuildListEntry e;
	e.templateName = templateName;
	e.x = x;
	e.y = y;
	e.angle = angle;
	return e;
}

int addBuildEntry(MapModel::Sides &sides, int side, MapModel::BuildListEntry entry)
{
	std::vector<MapModel::BuildListEntry> *list = listOf(sides, side);
	if (!list)
		return -1;
	list->push_back(std::move(entry));
	return static_cast<int>(list->size()) - 1;
}

bool removeBuildEntry(MapModel::Sides &sides, int side, int index)
{
	std::vector<MapModel::BuildListEntry> *list = listOf(sides, side);
	if (!list || index < 0 || index >= static_cast<int>(list->size()))
		return false;
	list->erase(list->begin() + index);
	return true;
}

bool moveBuildEntry(MapModel::Sides &sides, int side, int index, int delta)
{
	std::vector<MapModel::BuildListEntry> *list = listOf(sides, side);
	const int other = index + delta;
	if (!list || (delta != -1 && delta != 1) || index < 0 || other < 0 || index >= static_cast<int>(list->size()) || other >= static_cast<int>(list->size()))
		return false;
	std::swap((*list)[static_cast<std::size_t>(index)], (*list)[static_cast<std::size_t>(other)]);
	return true;
}

int buildEntryAt(const MapModel::Sides &sides, int side, float x, float y, float radius)
{
	if (side < 0 || side >= static_cast<int>(sides.sides.size()))
		return -1;
	const std::vector<MapModel::BuildListEntry> &list = sides.sides[static_cast<std::size_t>(side)].buildList;
	int best = -1;
	float bestDistance = radius;
	for (std::size_t i = 0; i < list.size(); ++i)
	{
		const float d = std::hypot(list[i].x - x, list[i].y - y);
		if (d <= bestDistance)
		{
			bestDistance = d;
			best = static_cast<int>(i);
		}
	}
	return best;
}

float BuildPower::used() const
{
	if (production > 0)
		return std::clamp(static_cast<float>(consumption) / static_cast<float>(production), 0.0f, 1.0f);
	return consumption > 0 ? 1.0f : 0.0f;
}

BuildPower buildListPower(const MapModel::Map &map, int side, int upTo, const std::function<int(const std::string &templateName)> &energyOf)
{
	BuildPower power;
	if (!map.sides || side < 0 || side >= static_cast<int>(map.sides->sides.size()) || !energyOf)
		return power;
	const MapModel::Sides &sides = *map.sides;
	const std::string player = playerName(sides.sides[static_cast<std::size_t>(side)]);
	const auto count = [&power](int energy) {
		if (energy > 0)
			power.production += energy;
		else
			power.consumption -= energy;
	};
	// Objects placed for the player: their team belongs to it.
	if (map.objects)
	{
		for (const MapModel::MapObject &o : *map.objects)
		{
			const int team = findTeam(sides, o.properties.getAscii("originalOwner").value_or(""));
			if (team >= 0 && sameName(teamOwner(sides.teams[static_cast<std::size_t>(team)]), player))
				count(energyOf(o.templateName));
		}
	}
	const std::vector<MapModel::BuildListEntry> &list = sides.sides[static_cast<std::size_t>(side)].buildList;
	for (int i = 0; i <= upTo && i < static_cast<int>(list.size()); ++i)
		count(energyOf(list[static_cast<std::size_t>(i)].templateName));
	return power;
}

std::string skirmishBuildListIni(const MapModel::Side &side, const std::string &factionSide)
{
	std::string out = ";Skirmish AI Build List\nSkirmishBuildList " + factionSide + "\n";
	char line[256];
	for (const MapModel::BuildListEntry &e : side.buildList)
	{
		out += "  Structure " + e.templateName + "\n";
		if (!e.buildingName.empty())
			out += "    Name = " + e.buildingName + "\n";
		std::snprintf(line, sizeof(line), "    Location = X:%.2f Y:%.2f\n", e.x, e.y);
		out += line;
		out += "    Rebuilds = " + std::to_string(e.numRebuilds) + "\n";
		std::snprintf(line, sizeof(line), "    Angle = %.2f\n", e.angle * 180.0 / 3.14159265358979323846);
		out += line;
		out += std::string("    InitiallyBuilt = ") + (e.initiallyBuilt ? "Yes" : "No") + "\n";
		out += "    AutomaticallyBuild = No\n";
		out += "  END ;Structure " + e.templateName + "\n";
	}
	out += "END ;SkirmishBuildList " + factionSide + "\n";
	return out;
}

} // namespace MapEditorCore
