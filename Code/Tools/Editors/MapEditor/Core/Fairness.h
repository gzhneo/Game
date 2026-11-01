/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Fairness.h -- how fair a multiplayer map is: ground path distances from every start position
// (waypoints Player_1_Start ... Player_8_Start) to the supplies, the tech buildings and the other
// starts, over the cells units can cross (no cliff flag, not under water, inside the border), and the
// territory each start reaches first.

#pragma once

#include "Model/Map.h"

#include <cstdint>
#include <string>
#include <vector>

namespace MapEditorCore
{

enum class ResourceKind
{
	None,
	Supply,		// supply docks, piles and warehouses
	Tech		// tech buildings (oil derricks, hospitals, artillery ...)
};
[[nodiscard]] ResourceKind resourceKind(const std::string &templateName);

// Start number (1..8) of a waypoint named Player_<n>_Start, else 0.
[[nodiscard]] int startNumber(const std::string &waypointName);

struct StartFairness
{
	int number = 0;					// Player_<number>_Start
	float x = 0.0f, y = 0.0f;
	float nearestSupply = -1.0f;	// path length in world units, -1 if none can be reached
	float nearestEnemy = -1.0f;
	int suppliesFirst = 0;			// supplies this start reaches before every other
	int techFirst = 0;
	int unreachable = 0;			// supplies and tech buildings it cannot reach
};

struct ResourceFairness
{
	std::string templateName;
	ResourceKind kind = ResourceKind::None;
	float x = 0.0f, y = 0.0f;
	std::vector<float> distance;	// per start, -1 unreachable
	int first = -1;					// index of the start that reaches it first, -1 none
	bool contested = false;			// the second start is within 10 % of the first
};

struct FairnessReport
{
	std::vector<StartFairness> starts;		// by number
	std::vector<ResourceFairness> resources;
	// Per height map cell: index of the start that reaches it first, -1 blocked or unreachable,
	// -2 contested (two starts within 10 %).
	std::vector<int> territory;
	int width = 0, height = 0;
	// (largest - smallest) / mean of the starts' nearest supply; 0 when even or fewer than two starts.
	float supplySpread = 0.0f;
};

// Cells units can cross: no cliff flag, not in a water area, inside the map's border.
[[nodiscard]] std::vector<std::uint8_t> passableCells(const MapModel::Map &map);
// Ground path length in cells (diagonal steps sqrt 2, no corner cutting) from one cell to every
// cell; -1 where it cannot be reached.
[[nodiscard]] std::vector<float> pathDistances(const std::vector<std::uint8_t> &passable, int width, int height, int fromX, int fromY);

[[nodiscard]] FairnessReport fairnessReport(const MapModel::Map &map);

} // namespace MapEditorCore
