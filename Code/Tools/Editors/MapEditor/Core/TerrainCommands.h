/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// TerrainCommands.h -- the height brush (raise, lower, set to a height, smooth) on the height map,
// after the old WorldBuilder's BrushTool, and the step a stroke leaves in the history: the changed
// rectangle before and after.

#pragma once

#include "Core/History.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace MapEditorCore
{

// Height map cells x0..x1, y0..y1 (border included); empty while x1 < x0.
struct CellRect
{
	int x0 = 0, y0 = 0, x1 = -1, y1 = -1;

	bool empty() const { return x1 < x0 || y1 < y0; }
	void unite(const CellRect &other);
};

enum class BrushMode
{
	Raise,
	Lower,
	Set,
	Smooth
};

struct Brush
{
	BrushMode mode = BrushMode::Raise;
	float radius = 5.0f;		// cells
	float feather = 0.5f;		// outer part of the radius that fades out, 0 to 1
	float strength = 2.0f;		// height steps per application (raise, lower), else how fast it pulls
	int height = 64;			// target of Set, in height steps
};

// The cell under a world position (fractional, border included).
void worldToCell(const MapModel::HeightMap &hm, float x, float y, float &cellX, float &cellY);

// Applies the brush once centred on the cell position; returns the cells it changed.
CellRect applyBrush(MapModel::HeightMap &hm, float cellX, float cellY, const Brush &brush);

// The heights of a rectangle before and after, as one step; with the map's cliff flags before and
// after if the step changed them.
class EditHeights : public Command
{
public:
	EditHeights(std::string name, CellRect rect, std::vector<std::uint8_t> before, std::vector<std::uint8_t> after)
		: m_name(std::move(name)), m_rect(rect), m_before(std::move(before)), m_after(std::move(after)) {}

	void setCliffs(std::vector<std::uint8_t> before, std::vector<std::uint8_t> after)
	{
		m_cliffsBefore = std::move(before);
		m_cliffsAfter = std::move(after);
	}

	virtual void apply(MapModel::Map &map) override { copy(map, m_after, m_cliffsAfter); }
	virtual void revert(MapModel::Map &map) override { copy(map, m_before, m_cliffsBefore); }
	virtual std::string name() const override { return m_name; }
	virtual Changes changes() const override;

private:
	void copy(MapModel::Map &map, const std::vector<std::uint8_t> &source, const std::vector<std::uint8_t> &cliffs) const;

	std::string m_name;
	CellRect m_rect;
	std::vector<std::uint8_t> m_before;
	std::vector<std::uint8_t> m_after;
	std::vector<std::uint8_t> m_cliffsBefore;
	std::vector<std::uint8_t> m_cliffsAfter;
};

// The step of a stroke: the map's heights now against a copy of all heights from its start, within
// the rectangle it touched. Sets the cliff flags there from the new heights, as the game computes
// them, and keeps the old ones for undo. Null if nothing changed.
std::unique_ptr<EditHeights> heightStroke(MapModel::Map &map, const std::vector<std::uint8_t> &before, CellRect rect,
	std::string name);

// Marks the cells within radius of the cell position as impassable (cliff) or passable; returns the
// changed cells.
CellRect paintPassability(std::vector<std::uint8_t> &flags, int width, int height, float cellX, float cellY, float radius, bool impassable);

// The map's cliff flags before and after, as one step (passability brush, flags from the heights).
class EditCliffs : public Command
{
public:
	EditCliffs(std::string name, CellRect rect, std::vector<std::uint8_t> before, std::vector<std::uint8_t> after)
		: m_name(std::move(name)), m_rect(rect), m_before(std::move(before)), m_after(std::move(after)) {}

	virtual void apply(MapModel::Map &map) override;
	virtual void revert(MapModel::Map &map) override;
	virtual std::string name() const override { return m_name; }
	virtual Changes changes() const override;

private:
	std::string m_name;
	CellRect m_rect;
	std::vector<std::uint8_t> m_before;
	std::vector<std::uint8_t> m_after;
};

// The step of a passability stroke or of resetting the flags; null if the flags did not change.
std::unique_ptr<EditCliffs> cliffStroke(const MapModel::Map &map, const std::vector<std::uint8_t> &before, CellRect rect, std::string name);

} // namespace MapEditorCore
