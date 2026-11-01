/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// TerrainTessMask.h -------------------------------------------------------------------------------
//
// Displacement mask of the terrain tessellation, one byte per grid point of the map (255 = full displacement):
// zero under water and along the shore, on roads and bridges, under buildings and under scorch marks, with a soft
// edge. Decals, roads and units lie on the undisplaced terrain there. Reads the terrain logic (water), the map objects (roads, bridges)
// and the game objects (structures, bridges) and changes nothing in them. The structures are compared every
// CHECK_INTERVAL calls, the mask is passed to the render device only when it changed.
// ------------------------------------------------------------------------------------------------

#pragma once

#include "Lib/BaseType.h"

class WorldHeightMap;

class TerrainTessMask
{
public:
	TerrainTessMask() = default;
	~TerrainTessMask();
	TerrainTessMask(const TerrainTessMask&) = delete;
	TerrainTessMask& operator=(const TerrainTessMask&) = delete;

	/// Builds the mask for map (once per map) and updates it when structures appear or vanish.
	/// scorchDiscs holds scorchCount triples (x, y, radius) of the scorch marks, which stay flat like roads.
	void update(WorldHeightMap *map, const Real *scorchDiscs, Int scorchCount);

private:
	enum { CHECK_INTERVAL = 30 };

	void release();
	void buildStatic(WorldHeightMap *map);	///< water, shore, roads and bridges of the map
	void addStructures(const Real *scorchDiscs, Int scorchCount);	///< scorch marks, structures and bridges into m_mask
	void clearAround(Real x, Real y, Real radius);	///< soft disc
	void clearSegment(Real x0, Real y0, Real x1, Real y1, Real halfWidth);
	void clearBox(Real x, Real y, Real angle, Real halfLength, Real halfWidth);
	UnsignedInt structureSignature(const Real *scorchDiscs, Int scorchCount) const;
	void lower(Int gx, Int gy, Real distance);	///< mask value from the distance (world) to the cleared area

	WorldHeightMap *m_map = nullptr;	///< only compared
	Int m_width = 0;
	Int m_height = 0;
	Int m_border = 0;
	UnsignedByte *m_static = nullptr;	///< water and roads
	UnsignedByte *m_mask = nullptr;		///< plus structures
	UnsignedByte *m_target = nullptr;	///< the array clear* writes into
	UnsignedInt m_signature = 0;
	Int m_countdown = 0;
};
