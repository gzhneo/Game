/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapSize.h -- a new map and a new size for a map, as the old WorldBuilder makes them (File > New:
// CWorldBuilderDoc::OnNewDocument, File > Resize: WorldHeightMapEdit::resize).
//
// A new map is flat at one height, covered with one texture, has the neutral player with its team,
// one playable area over the whole map and, if asked, a water area over everything. Resizing keeps
// the terrain at the anchor, continues the edge rows into new cells and moves everything placed in
// world units (objects, build lists, areas, script positions) along with the terrain.

#pragma once

#include "Core/History.h"

#include <memory>
#include <array>
#include <optional>
#include <vector>
#include <string>

namespace MapEditorCore
{

// Playable cells per side and border cells allowed for a new size.
constexpr int MIN_MAP_CELLS = 16;
constexpr int MAX_MAP_CELLS = 1000;
constexpr int MAX_MAP_BORDER = 200;

// How the start waypoints of a new map are arranged.
enum class StartLayout
{
	Corners,	// corners first (1 and 2 diagonal), then the middles of the edges
	Circle,		// evenly on an ellipse, 1 at the lower left, each opposite another for an even count
	TwoSides,	// odd numbers on the left, even ones on the right: teams 1, 3, ... against 2, 4, ...
};

constexpr int MAX_START_POSITIONS = 8;

// Positions (world units, playable area from 0) of the start waypoints Player_1_Start ... for
// that many players on a playable area of width x height cells; empty below 1 or above 8.
[[nodiscard]] std::vector<std::array<float, 2>> startPositions(int width, int height, int players, StartLayout layout);

struct NewMapOptions
{
	int width = 100;					// playable cells
	int height = 100;
	int border = 30;
	int initialHeight = 16;				// height steps
	std::string texture;				// texture class that covers the map
	std::int32_t textureTiles = 0;		// its tiles and tiles across (Terrain.ini)
	std::int32_t textureWidth = 0;
	std::optional<MapModel::GlobalLighting> lighting;
	std::optional<float> waterHeight;	// a water area over the whole map at this height
	int players = 0;					// start waypoints Player_1_Start ... (0 for none)
	StartLayout layout = StartLayout::Corners;
};

// The new map; nullopt if the size is out of range or the texture does not fit the terrain atlas.
[[nodiscard]] std::optional<MapModel::Map> newMap(const NewMapOptions &options);

// Which side of the map stays where it is: Start is left (x) or bottom (y), End right or top.
enum class Anchor
{
	Start,
	Centre,
	End
};

struct ResizeOptions
{
	int width = 0;						// playable cells
	int height = 0;
	int border = 0;
	Anchor x = Anchor::Centre;
	Anchor y = Anchor::Centre;
};

// How far world positions move with a resize (world units).
struct ResizeOffset
{
	float x = 0.0f;
	float y = 0.0f;
};
[[nodiscard]] ResizeOffset resizeOffset(const MapModel::HeightMap &hm, const ResizeOptions &options);

// Objects that lie outside the playable area after the resize (they stay in the map).
[[nodiscard]] int objectsOutsideAfterResize(const MapModel::Map &map, const ResizeOptions &options);

// Terrain, objects, waypoint links, build lists, areas and scripts before and after a step on the whole
// map (resize, symmetry).
class ResizeMap : public Command
{
public:
	struct Content
	{
		std::optional<MapModel::HeightMap> heightMap;
		std::optional<MapModel::BlendTiles> blendTiles;
		std::optional<std::vector<MapModel::MapObject>> objects;
		std::optional<std::vector<MapModel::PolygonArea>> polygons;
		std::optional<MapModel::Sides> sides;
		std::optional<std::vector<MapModel::WaypointLink>> waypointLinks;
	};

	ResizeMap(Content before, Content after, std::string name = "Resize map")
		: m_before(std::move(before)), m_after(std::move(after)), m_name(std::move(name)) {}

	virtual void apply(MapModel::Map &map) override { set(map, m_after); }
	virtual void revert(MapModel::Map &map) override { set(map, m_before); }
	virtual std::string name() const override { return m_name; }
	virtual Changes changes() const override;

private:
	static void set(MapModel::Map &map, const Content &content);

	Content m_before;
	Content m_after;
	std::string m_name;
};

// The terrain, objects, links, areas and sides of the map as one Content.
ResizeMap::Content mapContent(const MapModel::Map &map);

// The step that resizes the map; null if the map has no terrain, the size is out of range or the
// same as now.
std::unique_ptr<ResizeMap> resizeMap(const MapModel::Map &map, const ResizeOptions &options);

} // namespace MapEditorCore
