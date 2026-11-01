/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// AIPathfindInternal.h -- what the AIPathfind*.cpp files share and nobody else needs.

#pragma once

#define no_INTENSE_DEBUG

#define DEBUG_QPF

static inline Bool IS_IMPASSABLE(PathfindCell::CellType type) {
	// Return true if cell is impassable to ground units. jba. [8/18/2003]
	if (type==PathfindCell::CELL_IMPASSABLE) {
		return true;
	}
	if (type==PathfindCell::CELL_OBSTACLE) {
		return true;
	}
	if (type==PathfindCell::CELL_BRIDGE_IMPASSABLE) {
		return true;
	}
	return false;
}


struct TCheckMovementInfo
{
	// Input
	ICoord2D					cell;
	PathfindLayerEnum layer;
	Int								radius;	
	Bool							centerInCell;
	Bool							considerTransient;
	LocomotorSurfaceTypeMask acceptableSurfaces;
	// Output 
	Int								allyFixedCount;
	Bool							enemyFixed;
	Bool							allyMoving;
	Bool							allyGoal;
};

inline Int IABS(Int x) {	if (x>=0) return x; return -x;};

extern Int frameToShowObstacles;

const Int COST_ORTHOGONAL = 10;
const Int COST_DIAGONAL = 14;
const Real COST_TO_DISTANCE_FACTOR = 1.0f/10.0f;
const Real COST_TO_DISTANCE_FACTOR_SQR = COST_TO_DISTANCE_FACTOR*COST_TO_DISTANCE_FACTOR;

inline Bool typesMatch(const PathfindCell &targetCell, const PathfindCell &sourceCell) {
	PathfindCell::CellType targetType = targetCell.getType();
	PathfindCell::CellType srcType = sourceCell.getType();
	if (targetType == srcType) return true;
	
	return false;
}

inline Bool waterGround(const PathfindCell &targetCell, const PathfindCell &sourceCell) {
	PathfindCell::CellType targetType = targetCell.getType();
	PathfindCell::CellType srcType = sourceCell.getType();
	if ( (targetType==PathfindCell::CELL_CLEAR && 
		(srcType&PathfindCell::CELL_WATER ))) {
			return true;
	}
	if ( (srcType==PathfindCell::CELL_CLEAR && 
		(targetType&PathfindCell::CELL_WATER ))) {
			return true;
	}

	return false;
}

inline Bool groundRubble(const PathfindCell &targetCell, const PathfindCell &sourceCell) {
	PathfindCell::CellType targetType = targetCell.getType();
	PathfindCell::CellType srcType = sourceCell.getType();
	if ( (targetType==PathfindCell::CELL_CLEAR && 
		(srcType==PathfindCell::CELL_RUBBLE ))) {
			return true;
	}
	if ( (srcType==PathfindCell::CELL_CLEAR && 
		(targetType==PathfindCell::CELL_RUBBLE ))) {
			return true;
	}

	return false;
}

inline Bool terrain(const PathfindCell &targetCell, const PathfindCell &sourceCell) {
	Int targetType = targetCell.getType();
	Int srcType = sourceCell.getType();
	if (targetType == PathfindCell::CELL_OBSTACLE) targetType = PathfindCell::CELL_CLEAR;
	if (srcType == PathfindCell::CELL_OBSTACLE) srcType = PathfindCell::CELL_CLEAR;
	if (targetType==srcType) {
		return true;
	}
	return false;
}

inline Bool crusherGround(const PathfindCell &targetCell, const PathfindCell &sourceCell) {
	Int targetType = targetCell.getType();
	Int srcType = sourceCell.getType();
	if (targetType==PathfindCell::CELL_OBSTACLE) {
		if (targetCell.isObstacleFence()) {
			if (srcType == PathfindCell::CELL_CLEAR) {
				return true;
			}
		}
	}
	if (srcType==PathfindCell::CELL_OBSTACLE) {
		if (sourceCell.isObstacleFence()) {
			if (targetType == PathfindCell::CELL_CLEAR) {
				return true;
			}
		}
	}
	return false;
}

inline Bool groundCliff(const PathfindCell &targetCell, const PathfindCell &sourceCell) {
	PathfindCell::CellType targetType = targetCell.getType();
	PathfindCell::CellType srcType = sourceCell.getType();
	
	if ( (targetType==PathfindCell::CELL_CLIFF ) && 
			 (srcType==PathfindCell::CELL_CLEAR) ) {
			return true;
	}
	if ( (targetType==PathfindCell::CELL_CLEAR ) && 
			 (srcType==PathfindCell::CELL_CLIFF) ) {
			return true;
	}
	return false;
}

// Cell infos the pool could not hand out (a search stops there); not saved, not in the CRC.
extern UnsignedInt s_statInfoExhausted;
// Cell infos in the free list; not saved, not in the CRC.
extern UnsignedInt s_statInfoFree;

// Counts and times the outermost search; nested searches (a search that calls another) add to it.
class Pathfinder::SearchStatScope
{
public:
	SearchStatScope( Pathfinder &pathfinder, SearchKind kind ) : m_pathfinder(pathfinder), m_kind(kind),
		m_outermost(pathfinder.m_statSearchDepth++ == 0), m_cellsBefore(pathfinder.m_statCellsReleased) {}
	~SearchStatScope()
	{
		--m_pathfinder.m_statSearchDepth;
		if (!m_outermost)
			return;
		SearchStats &s = m_pathfinder.m_statSearch[m_kind];
		const double ms = m_timer.elapsedMilliseconds();
		++s.searches;
		s.cells += m_pathfinder.m_statCellsReleased - m_cellsBefore;
		s.ms += ms;
		if (ms > s.maxMs)
			s.maxMs = ms;
	}
private:
	Pathfinder &m_pathfinder;
	const SearchKind m_kind;
	const Bool m_outermost;
	const double m_cellsBefore;
	const Engine::ScopedTimer m_timer;
};

#define dont_forceRefreshCalling
#ifdef forceRefreshCalling
extern Bool s_stopForceCalling;
#endif
