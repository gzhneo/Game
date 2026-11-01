/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// TextureCommands.h -- painting terrain textures on the map, as the old WorldBuilder's TileTool does:
// each cell gets the tile its texture shows at that place (a texture repeats every width tiles, a
// tile covers 2 x 2 cells), with no blend. A texture the map does not have yet is added if the
// map's terrain atlas still has room (the same packing as WorldHeightMap::updateTileTexturePositions).

#pragma once

#include "Core/TerrainCommands.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace MapEditorCore
{

// Texture classes of the given widths (tiles across) fit into the terrain atlas.
[[nodiscard]] bool texturesFit(const std::vector<std::int32_t> &widths);

// Index of the map's texture class with the name, -1 if it has none.
[[nodiscard]] int textureClassIndex(const MapModel::BlendTiles &tiles, const std::string &name);

// Adds a texture class (numTiles tiles, width across) after the map's tiles; returns its index, or
// -1 if the atlas or the tile table (1024 tiles) has no room.
int addTextureClass(MapModel::BlendTiles &tiles, const std::string &name, std::int32_t numTiles, std::int32_t width);

// The tile index a cell shows for the class (tile * 4 + quarter of the tile).
[[nodiscard]] std::int16_t tileForClass(const MapModel::TextureClass &textureClass, int x, int y);

// Paints the class onto the cells within radius of the cell position; returns the changed cells.
CellRect paintTexture(MapModel::BlendTiles &tiles, int width, int height, float cellX, float cellY, float radius, int classIndex);

// The cell indices (tiles, blends, extra blends, cliffs) of a rectangle before and after, with the
// texture classes and the blend table (a step may add to either).
class EditTiles : public Command
{
public:
	struct Cells
	{
		std::vector<std::int16_t> tiles, blends, extraBlends, cliffs;
	};
	struct Classes
	{
		std::int32_t numBitmapTiles = 0;
		std::vector<MapModel::TextureClass> textureClasses;
		std::vector<MapModel::BlendedTile> blendedTiles;
		std::vector<MapModel::CliffInfo> cliffInfos;
	};

	EditTiles(std::string name, CellRect rect, Cells before, Cells after, Classes classesBefore, Classes classesAfter)
		: m_name(std::move(name)), m_rect(rect), m_before(std::move(before)), m_after(std::move(after)),
		  m_classesBefore(std::move(classesBefore)), m_classesAfter(std::move(classesAfter)) {}

	virtual void apply(MapModel::Map &map) override { copy(map, m_after, m_classesAfter); }
	virtual void revert(MapModel::Map &map) override { copy(map, m_before, m_classesBefore); }
	virtual std::string name() const override { return m_name; }
	virtual Changes changes() const override;

private:
	void copy(MapModel::Map &map, const Cells &cells, const Classes &classes) const;

	std::string m_name;
	CellRect m_rect;
	Cells m_before, m_after;
	Classes m_classesBefore, m_classesAfter;
};

// The step of a texture stroke: the map now against a copy of its tiles from the stroke's start,
// within the rectangle it touched. Null if nothing changed.
std::unique_ptr<EditTiles> tileStroke(const MapModel::Map &map, const MapModel::BlendTiles &before, CellRect rect, std::string name);

} // namespace MapEditorCore
