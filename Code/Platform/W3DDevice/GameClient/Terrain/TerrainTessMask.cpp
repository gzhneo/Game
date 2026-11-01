/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// TerrainTessMask.cpp -- displacement mask of the terrain tessellation, see header.

#include "W3DDevice/GameClient/TerrainTessMask.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/MapObject.h"
#include "GameClient/TerrainRoads.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DRoadBuffer.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "WW3D2/Common/renderdevice.h"

namespace
{
	const Real CLEAR_MARGIN = 3.0f;		///< world units beyond the object or road that stay flat
	const Real FADE_WIDTH = 8.0f;		///< then the displacement fades in over this distance
	const Real SHORE_CELLS = 3.0f;		///< grid points within this many cells of water stay flat
	const Real BRIDGE_HALF_WIDTH = 30.0f;	///< bridge ends from the map (the bridge object adds its own box)
}

TerrainTessMask::~TerrainTessMask()
{
	release();
}

void TerrainTessMask::release()
{
	delete[] m_static;
	delete[] m_mask;
	m_static = nullptr;
	m_mask = nullptr;
	m_target = nullptr;
	m_map = nullptr;
	m_width = m_height = 0;
}

void TerrainTessMask::lower(Int gx, Int gy, Real distance)
{
	if (gx < 0 || gy < 0 || gx >= m_width || gy >= m_height)
		return;
	Real f = (distance - CLEAR_MARGIN) / FADE_WIDTH;
	if (f < 0.0f) f = 0.0f;
	if (f > 1.0f) f = 1.0f;
	const UnsignedByte v = static_cast<UnsignedByte>(f * 255.0f + 0.5f);
	UnsignedByte &m = m_target[gy * m_width + gx];
	if (v < m)
		m = v;
}

void TerrainTessMask::clearAround(Real x, Real y, Real radius)
{
	const Real reach = radius + CLEAR_MARGIN + FADE_WIDTH;
	const Int x0 = static_cast<Int>(floorf((x - reach) / MAP_XY_FACTOR)) + m_border;
	const Int x1 = static_cast<Int>(ceilf((x + reach) / MAP_XY_FACTOR)) + m_border;
	const Int y0 = static_cast<Int>(floorf((y - reach) / MAP_XY_FACTOR)) + m_border;
	const Int y1 = static_cast<Int>(ceilf((y + reach) / MAP_XY_FACTOR)) + m_border;
	for (Int gy = y0; gy <= y1; ++gy) {
		for (Int gx = x0; gx <= x1; ++gx) {
			const Real dx = (gx - m_border) * MAP_XY_FACTOR - x;
			const Real dy = (gy - m_border) * MAP_XY_FACTOR - y;
			Real d = sqrtf(dx * dx + dy * dy) - radius;
			lower(gx, gy, d < 0.0f ? 0.0f : d);
		}
	}
}

void TerrainTessMask::clearSegment(Real x0, Real y0, Real x1, Real y1, Real halfWidth)
{
	const Real reach = halfWidth + CLEAR_MARGIN + FADE_WIDTH;
	const Int gx0 = static_cast<Int>(floorf(((x0 < x1 ? x0 : x1) - reach) / MAP_XY_FACTOR)) + m_border;
	const Int gx1 = static_cast<Int>(ceilf(((x0 > x1 ? x0 : x1) + reach) / MAP_XY_FACTOR)) + m_border;
	const Int gy0 = static_cast<Int>(floorf(((y0 < y1 ? y0 : y1) - reach) / MAP_XY_FACTOR)) + m_border;
	const Int gy1 = static_cast<Int>(ceilf(((y0 > y1 ? y0 : y1) + reach) / MAP_XY_FACTOR)) + m_border;
	const Real ex = x1 - x0, ey = y1 - y0;
	const Real len2 = ex * ex + ey * ey;
	for (Int gy = gy0; gy <= gy1; ++gy) {
		for (Int gx = gx0; gx <= gx1; ++gx) {
			const Real px = (gx - m_border) * MAP_XY_FACTOR - x0;
			const Real py = (gy - m_border) * MAP_XY_FACTOR - y0;
			Real t = (len2 > 0.0f) ? (px * ex + py * ey) / len2 : 0.0f;
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			const Real dx = px - t * ex, dy = py - t * ey;
			Real d = sqrtf(dx * dx + dy * dy) - halfWidth;
			lower(gx, gy, d < 0.0f ? 0.0f : d);
		}
	}
}

void TerrainTessMask::clearBox(Real x, Real y, Real angle, Real halfLength, Real halfWidth)
{
	const Real reach = sqrtf(halfLength * halfLength + halfWidth * halfWidth) + CLEAR_MARGIN + FADE_WIDTH;
	const Real c = cosf(angle), s = sinf(angle);
	const Int gx0 = static_cast<Int>(floorf((x - reach) / MAP_XY_FACTOR)) + m_border;
	const Int gx1 = static_cast<Int>(ceilf((x + reach) / MAP_XY_FACTOR)) + m_border;
	const Int gy0 = static_cast<Int>(floorf((y - reach) / MAP_XY_FACTOR)) + m_border;
	const Int gy1 = static_cast<Int>(ceilf((y + reach) / MAP_XY_FACTOR)) + m_border;
	for (Int gy = gy0; gy <= gy1; ++gy) {
		for (Int gx = gx0; gx <= gx1; ++gx) {
			const Real px = (gx - m_border) * MAP_XY_FACTOR - x;
			const Real py = (gy - m_border) * MAP_XY_FACTOR - y;
			Real u = fabsf(px * c + py * s) - halfLength;
			Real v = fabsf(-px * s + py * c) - halfWidth;
			if (u < 0.0f) u = 0.0f;
			if (v < 0.0f) v = 0.0f;
			lower(gx, gy, sqrtf(u * u + v * v));
		}
	}
}

void TerrainTessMask::buildStatic(WorldHeightMap *map)
{
	release();
	m_map = map;
	m_width = map->getXExtent();
	m_height = map->getYExtent();
	m_border = map->getBorderSizeInline();
	if (m_width <= 0 || m_height <= 0)
		return;
	const Int n = m_width * m_height;
	m_static = new UnsignedByte[n];
	m_mask = new UnsignedByte[n];
	memset(m_static, 255, n);
	m_target = m_static;

	// Water and shore: grid points under water and within SHORE_CELLS of one.
	Int wet = 0;
	if (TheTerrainLogic) {
		UnsignedByte *underwater = new UnsignedByte[n];
		for (Int gy = 0; gy < m_height; ++gy) {
			for (Int gx = 0; gx < m_width; ++gx) {
				const Real x = (gx - m_border) * MAP_XY_FACTOR;
				const Real y = (gy - m_border) * MAP_XY_FACTOR;
				underwater[gy * m_width + gx] = TheTerrainLogic->isUnderwater(x, y) ? 1 : 0;
			}
		}
		const Int reach = static_cast<Int>(SHORE_CELLS) + 1;
		for (Int gy = 0; gy < m_height; ++gy) {
			for (Int gx = 0; gx < m_width; ++gx) {
				if (!underwater[gy * m_width + gx])
					continue;
				++wet;
				for (Int dy = -reach; dy <= reach; ++dy) {
					for (Int dx = -reach; dx <= reach; ++dx) {
						const Int x = gx + dx, y = gy + dy;
						if (x < 0 || y < 0 || x >= m_width || y >= m_height)
							continue;
						Real f = (sqrtf(static_cast<Real>(dx * dx + dy * dy)) - (SHORE_CELLS - 1.0f)) / 2.0f;
						if (f < 0.0f) f = 0.0f;
						if (f > 1.0f) f = 1.0f;
						const UnsignedByte v = static_cast<UnsignedByte>(f * 255.0f + 0.5f);
						if (v < m_static[y * m_width + x])
							m_static[y * m_width + x] = v;
					}
				}
			}
		}
		delete[] underwater;
	}

	// Roads and bridge ends from the map objects (the same pairs W3DRoadBuffer and W3DBridgeBuffer read).
	Int roads = 0;
	for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
		MapObject *next = obj->getNext();
		if (!next)
			break;
		if (obj->getFlag(FLAG_ROAD_POINT1) && next->getFlag(FLAG_ROAD_POINT2)) {
			Real halfWidth = DEFAULT_ROAD_SCALE * 0.5f;
			TerrainRoadType *road = TheTerrainRoads ? TheTerrainRoads->findRoad(obj->getName()) : nullptr;
			if (road)
				halfWidth = road->getRoadWidth() * road->getRoadWidthInTexture() * 0.5f;
			clearSegment(obj->getLocation()->x, obj->getLocation()->y, next->getLocation()->x, next->getLocation()->y, halfWidth);
			++roads;
		}
		else if (obj->getFlag(FLAG_BRIDGE_POINT1) && next->getFlag(FLAG_BRIDGE_POINT2)) {
			clearSegment(obj->getLocation()->x, obj->getLocation()->y, next->getLocation()->x, next->getLocation()->y, BRIDGE_HALF_WIDTH);
			++roads;
		}
	}
	char line[128];
	snprintf(line, sizeof(line), "TERRAINTESS maske %d x %d wasserpunkte %d strassen %d", m_width, m_height, wet, roads);
	DEBUG_LOG(("%s\n", line));
	GameEngine_RunnerLog(line);
}

UnsignedInt TerrainTessMask::structureSignature(const Real *scorchDiscs, Int scorchCount) const
{
	UnsignedInt sig = 2166136261u;
	sig = (sig ^ static_cast<UnsignedInt>(scorchCount)) * 16777619u;
	for (Int i = 0; i < scorchCount * 3; ++i)
		sig = (sig ^ static_cast<UnsignedInt>(static_cast<Int>(scorchDiscs[i] * 4.0f))) * 16777619u;
	if (!TheGameLogic)
		return sig;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject()) {
		if (!obj->isKindOf(KINDOF_STRUCTURE) && !obj->isKindOf(KINDOF_BRIDGE) && !obj->isKindOf(KINDOF_BRIDGE_TOWER))
			continue;
		const Coord3D *pos = obj->getPosition();
		const UnsignedInt v[3] = { static_cast<UnsignedInt>(obj->getID()), static_cast<UnsignedInt>(static_cast<Int>(pos->x * 4.0f)),
			static_cast<UnsignedInt>(static_cast<Int>(pos->y * 4.0f)) };
		for (Int i = 0; i < 3; ++i)
			sig = (sig ^ v[i]) * 16777619u;
	}
	return sig;
}

void TerrainTessMask::addStructures(const Real *scorchDiscs, Int scorchCount)
{
	m_target = m_mask;
	for (Int i = 0; i < scorchCount; ++i)
		clearAround(scorchDiscs[i * 3], scorchDiscs[i * 3 + 1], scorchDiscs[i * 3 + 2]);
	if (!TheGameLogic)
		return;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject()) {
		if (!obj->isKindOf(KINDOF_STRUCTURE) && !obj->isKindOf(KINDOF_BRIDGE) && !obj->isKindOf(KINDOF_BRIDGE_TOWER))
			continue;
		const GeometryInfo &geom = obj->getGeometryInfo();
		const Coord3D *pos = obj->getPosition();
		if (geom.getGeomType() == GEOMETRY_BOX)
			clearBox(pos->x, pos->y, obj->getOrientation(), geom.getMajorRadius(), geom.getMinorRadius());
		else
			clearAround(pos->x, pos->y, geom.getMajorRadius());
	}
}

void TerrainTessMask::update(WorldHeightMap *map, const Real *scorchDiscs, Int scorchCount)
{
	if (!map)
		return;
	const Bool newMap = map != m_map || map->getXExtent() != m_width || map->getYExtent() != m_height;
	if (newMap) {
		buildStatic(map);
		m_countdown = 0;
	}
	if (!m_mask)
		return;
	if (!newMap && m_countdown > 0) {
		--m_countdown;
		return;
	}
	m_countdown = CHECK_INTERVAL;
	const UnsignedInt sig = structureSignature(scorchDiscs, scorchCount);
	if (!newMap && sig == m_signature)
		return;
	m_signature = sig;
	memcpy(m_mask, m_static, static_cast<size_t>(m_width) * static_cast<size_t>(m_height));
	addStructures(scorchDiscs, scorchCount);
	RenderDevice::Set_Terrain_Tess_Mask(m_mask, m_width, m_height);
}
