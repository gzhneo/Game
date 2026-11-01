/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Overlay3D.h -- what the 3D view shows on top of the scene, built from the map model: playable
// boundaries, waypoints with their links and the start positions, trigger and water areas. The
// engine lays the shapes on the terrain (meViewSetOverlay); colours follow the 2D overview.

#pragma once

#include "Engine/EngineApi.h"
#include "Model/Map.h"

#include <functional>
#include <vector>

namespace MapEditorApp
{

struct OverlayOptions
{
	bool boundaries = true;
	bool waypoints = true;
	bool areas = true;
	bool sounds = false;	// objects with an ambient sound: marker and range circles
	bool boundingBoxes = false;
	bool sightRanges = false;
	bool weaponRanges = false;
	bool testArt = false;	// objects whose type the game lacks
};

// The catalog entry of a template; null if the game has none.
using TemplateOf = std::function<const MeTemplateInfo *(const std::string &templateName)>;

// Minimum and maximum range of an object's ambient sound; false if it plays none.
using SoundRangeOf = std::function<bool(const MapModel::MapObject &object, float &minRange, float &maxRange)>;

// A selected object: index into Map::objects and its radius in the ground plane (from the engine).
struct SelectionMark
{
	int object = -1;
	float radius = 0.0f;
};

// The area the tool works on: its index with the selected point, and an area being drawn.
struct AreaMarks
{
	int area = -1;
	int point = -1;
	const MapModel::PolygonArea *draft = nullptr;
	std::vector<float> lines;			// tool lines x1, y1, x2, y2 (roads being drawn or selected)
	std::vector<float> circle;			// brush outline, x, y per point
};

struct OverlayShapes
{
	std::vector<float> points;					// x, y, z per point
	std::vector<MeOverlayShape> shapes;
};

// The selection (ring and facing) is always shown, whatever the options say.
OverlayShapes buildOverlay(const MapModel::Map &map, const OverlayOptions &options, const std::vector<SelectionMark> &selection = {},
	const AreaMarks &areas = {}, const SoundRangeOf &soundRange = {}, const TemplateOf &templateOf = {});

} // namespace MapEditorApp
