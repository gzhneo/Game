/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// PlacementCommands.h -- objects the editor places in numbers, after the old WorldBuilder's tools:
// a grove of trees grown outwards from a point or scattered in a rectangle (GroveTool), a fence of
// equally spaced pieces along a line (FenceTool), and scorch marks (ScorchTool). The functions only
// build the objects; placing them is a paste (pasteObjects), one step in the history.

#pragma once

#include "Model/Map.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace MapEditorCore
{

// Up to five tree templates and how often each is chosen (weights, any scale).
struct GroveOptions
{
	std::vector<std::pair<std::string, int>> types;
	int boxCount = 20;				// trees scattered in a rectangle
	bool allowInWater = false;
	bool allowOnSlopes = false;
	std::string owner = "team";		// team of the new objects (neutral)
};

// True if the ground at the point lies below the surface of a water area.
[[nodiscard]] bool underWater(const MapModel::Map &map, float x, float y);

// True if the ground at the point is too steep for a tree (its normal leans more than the old
// tool allowed).
[[nodiscard]] bool tooSteepForTree(const MapModel::Map &map, float x, float y);

// A grove grown out from the point: a tree there, then two children a few cells away, three levels
// deep; children avoid slopes, water, big height changes and the way back. Positions and trees come
// from the generator. Empty if the options name no tree.
std::vector<MapModel::MapObject> growGrove(const MapModel::Map &map, float x, float y, const GroveOptions &options, std::mt19937 &random);

// boxCount trees at random places in the rectangle, without water and slopes unless allowed.
std::vector<MapModel::MapObject> scatterGrove(const MapModel::Map &map, float x1, float y1, float x2, float y2,
	const GroveOptions &options, std::mt19937 &random);

// Fence pieces from (x1, y1) towards (x2, y2): one every spacing, at least one, at most 200, all
// facing along the line and moved by offset along it. With count > 0 exactly that many pieces
// share the whole length (stretched).
std::vector<MapModel::MapObject> fencePieces(const std::string &templateName, const std::string &owner, float x1, float y1,
	float x2, float y2, float spacing, float offset, int count = 0);

// A scorch mark: the "Scorch" template with its type (0 to 4) and radius.
constexpr int SCORCH_TYPES = 5;
MapModel::MapObject scorchMark(float x, float y, int type, float radius);
[[nodiscard]] bool isScorch(const MapModel::MapObject &object);

// The scorch mark nearest to the point within 1.5 cells (half a cell counts first), -1 if none.
[[nodiscard]] int scorchAt(const MapModel::Map &map, float x, float y);

} // namespace MapEditorCore
