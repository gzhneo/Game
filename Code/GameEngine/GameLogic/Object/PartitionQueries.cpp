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

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/LogicToClient.h"
#include "Common/ActionManager.h"
#include "Common/DiscreteCircle.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Radar.h"
#include "Common/ThingFactory.h"	// for bullet type hack
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/Squad.h"
#include "GameLogic/GhostObject.h"
#include "GameClient/Line2D.h"
#include "GameClient/ControlBar.h"
#include "Common/PlayerList.h"
#include "Common/MapObject.h"

#include "PartitionManagerInternal.h"

// Object queries: closest objects, distances, ranges, free positions, terrain lines of sight

const Real HUGE_DIST_SQR = (HUGE_DIST*HUGE_DIST);

#define NO_FILTER_PROFILING

#ifdef FILTER_PROFILING
Bool DoFilterProfiling = false;
#endif

//DECLARE_PERF_TIMER(filtersAllow)
inline Bool filtersAllow(PartitionFilter **filters, Object *objOther)
{
	//USE_PERF_TIMER(filtersAllow)
#ifdef FILTER_PROFILING
	const Int MAXR = 32;
	static const char* names[MAXR];
	static Int rejections[MAXR];
	static Int usefulRejections[MAXR];
	static Int calls = 0;
	static Int maxEver = 0;

	Int idx;
	Bool allow = true;

	if (DoFilterProfiling)
	{
		if (calls == 0)
		{
			for (idx = 0; idx < MAXR; ++idx)
			{
				names[idx] = nullptr;
				rejections[idx] = 0;
				usefulRejections[idx] = 0;
			}
			maxEver = 0;
		}
		++calls;
	}

	for (PartitionFilter **fp = filters; fp && *fp; fp++)
	{
		if (!(*fp)->allow(objOther))
		{
			if (DoFilterProfiling)
			{
				const char** namesarr = names;
				for (idx = 0; idx < maxEver; ++idx)
					if (namesarr[idx] == (*fp)->debugGetName())
						break;
				if (idx == maxEver)
				{
					namesarr[maxEver++] = (*fp)->debugGetName();
					DEBUG_ASSERTCRASH(maxEver<MAXR,("hmm, enlarge this array"));
				}
				++rejections[idx];
				if (allow)
					++usefulRejections[idx];
				allow = false;
				if (maxEver < idx)
					maxEver = idx;
			}
			else
			{
				return false;
			}
		}
	}

	if (DoFilterProfiling && calls > 0 && calls % 1000==0)
	{
		DEBUG_LOG(("\n\n"));
		for (idx = 0; idx < maxEver; idx++)
		{
			DEBUG_LOG(("rejections[%s] = %d (useful = %d)\n",names[idx],rejections[idx],usefulRejections[idx]));
		}
	}

	return allow;
#else
	for (PartitionFilter **fp = filters; fp && *fp; fp++)
	{
		if (!(*fp)->allow(objOther))
		{
			return false;
		}
	}
	// assume true if no filters rejected it	
	return true;
#endif
}

#ifdef FASTER_GCO
//-----------------------------------------------------------------------------
Int PartitionManager::calcMinRadius(const ICoord2D& cur)
{
	/*
		ok, so if obj "A" is in cell (0,0), and obj "B"
		is in cell (cur.x, cur.y), calc the smallest distance they could be
		from each other, in integral multiples of cellSize. Note that
		each object can be anywhere within a given cell (not just in the center!)
		so even objects in the same cell can still be cellSize*1.414 apart,
		and objects in adjacent cells can be nearly-zero distance apart!
	*/
	Real halfCell = static_cast<Real>(m_cellSize * 0.5);
	Coord3D centerPos[4] = 
	{ 
		{ -halfCell, -halfCell }, 
		{ halfCell, -halfCell }, 
		{ -halfCell, halfCell }, 
		{ halfCell, halfCell } 
	};

	Real x = cur.x * m_cellSize;
	Real y = cur.y * m_cellSize;
	Coord3D otherPos[4] = 
	{ 
		{ x - halfCell, y - halfCell }, 
		{ x + halfCell, y - halfCell }, 
		{ x - halfCell, y + halfCell }, 
		{ x + halfCell, y + halfCell } 
	};

	/*
		this is ugly and there's probably a faster way, but we only do this once per PM reset,
		so it really shouldn't matter... (I hope)
	*/

	double minDistSqr = 1e12;				// double, not real
	for (int i = 0; i < 4; ++i)
	{
		for (int j = 0; j < 4; ++j)
		{
			// double, not real
			double dx = centerPos[i].x - otherPos[j].x;
			double dy = centerPos[i].y - otherPos[j].y;
			double curDistSqr = dx*dx + dy*dy;
			if (minDistSqr > curDistSqr)
				minDistSqr = curDistSqr;
		}
	}

	// double, not real
	double dist = sqrtf(static_cast<float>(minDistSqr));
	Int minRadius = REAL_TO_INT_CEIL( static_cast<Real>(dist / m_cellSize) );

	return minRadius;
}
#endif

#ifdef FASTER_GCO
//-----------------------------------------------------------------------------
void PartitionManager::calcRadiusVec()
{
	Real cellSize = getCellSize();
	Int cx = getCellCountX();
	Int cy = getCellCountY();

	// double, not real
	double dx = static_cast<double>(cx) * static_cast<double>(cellSize);
	double dy = static_cast<double>(cy) * static_cast<double>(cellSize);
	double maxPossibleDist = sqrt(dx*dx + dy*dy);
	
	m_maxGcoRadius = REAL_TO_INT_CEIL(static_cast<Real>(maxPossibleDist / cellSize));
	
	m_radiusVec.clear();
	m_radiusVec.resize(m_maxGcoRadius+1, OffsetVec());

	ICoord2D cur;
	for (cur.y = -cy+1; cur.y < cy; ++cur.y)
	{
		for (cur.x = -cx+1; cur.x < cx; ++cur.x)
		{
			/*
				m_radiusVec[curRadius] contains a list of the cells (foo) that could
				contain objects that are <= (curRadius * cellSize) distance away from cell (0,0).
			*/
			Int curRadius = calcMinRadius(cur);
			DEBUG_ASSERTCRASH(curRadius <= m_maxGcoRadius, ("expected max of %d but got %d\n",m_maxGcoRadius,curRadius));
			if (curRadius <= m_maxGcoRadius)
				m_radiusVec[curRadius].push_back(cur);
		}
	}

#if defined(_DEBUG)
	Int total = 0;
	for (Int i = 0; i <= m_maxGcoRadius; ++i)
	{
		total += static_cast<Int>(m_radiusVec[i].size());
		//DEBUG_LOG(("radius %d has %d entries\n",i,m_radiusVec[i].size()));
	}
	DEBUG_ASSERTCRASH(total == (cx*2-1)*(cy*2-1),("expected %d, got %d\n",(cx*2-1)*(cy*2-1),total));
#endif

}
#endif

//-----------------------------------------------------------------------------
//DECLARE_PERF_TIMER(getClosestObjects)
Object *PartitionManager::getClosestObjects(
	const Object *obj, 
	const Coord3D *pos, 
	Real maxDist, 
	DistanceCalculationType dc, 
	PartitionFilter **filters, 
	SimpleObjectIterator *iterArg,	// if nonnull, append ALL satisfactory objects to the iterator (not just the single closest)
	Real *closestDistArg,
	Coord3D *closestVecArg
)
{
	//USE_PERF_TIMER(getClosestObjects)

#ifdef DUMP_PERF_STATS
	if (TheGameLogic->getFrame() != s_gcoPerfFrame)
	{
		s_gcoPerfFrame = TheGameLogic->getFrame();
		s_countInClosestObjectsThisFrame = 0;
		s_timeInClosestObjectsThisFrame = 0;
	}
	++s_countInClosestObjects;
	++s_countInClosestObjectsThisFrame;

	Int64 startTime64;
	GetPrecisionTimer(&startTime64);
#endif
	
#ifdef _DEBUG
	static Int theEntrancyCount = 0;
	DEBUG_ASSERTCRASH(theEntrancyCount == 0, ("sorry, this routine is not reentrant"));
	++theEntrancyCount;
#endif

	DEBUG_ASSERTCRASH((obj==nullptr) != (pos == nullptr), ("either obj or pos must be null"));

	DistCalcProc distProc = theDistCalcProcs[static_cast<Int>(dc)];

	const Coord3D *objPos;
	const Object *objToUse;
	if (pos) 
	{
		objPos = pos;
		objToUse = nullptr;
	}
	else
	{
		objPos = obj->getPosition();
		objToUse = obj;
	}
	Int cellCenterX, cellCenterY;
	worldToCell(objPos->x, objPos->y, &cellCenterX, &cellCenterY);

	Object* closestObj = nullptr;
	Real closestDistSqr = maxDist * maxDist;	// if it's not closer than this, we shouldn't consider it anyway...
	// stays zero if nothing is found
	Coord3D closestVec = { 0, 0, 0 };

#ifdef FASTER_GCO

	Int maxRadius = m_maxGcoRadius;
	if (maxDist < HUGE_DIST)
	{
		// don't go outwards any farther than necessary.
		maxRadius = minInt(m_maxGcoRadius, worldToCellDist(maxDist));
	}
#if defined(INTENSE_DEBUG)
	/*
		Note, if you ever enable this code, be forewarned that it can give
		you "false positives" for objects that are located just off the map... (srj)
	*/
	Int maxRadiusLimit = maxRadius + 3;
	if (maxRadiusLimit > m_maxGcoRadius) maxRadiusLimit = m_maxGcoRadius;
#else
	Int maxRadiusLimit = maxRadius;
#endif

	Bool foundAny = false;

	static Int theIterFlag = 1;	// nonzero, thanks
	++theIterFlag;

	/*
		m_radiusVec[curRadius] contains a list of the cells (foo) that could
		contain objects that are <= (curRadius * cellSize) distance away from cell (0,0).
	*/
  for (Int curRadius = 0; curRadius <= maxRadiusLimit; ++curRadius)
  {
    const OffsetVec& offsets = m_radiusVec[curRadius];
		if (offsets.empty())
			continue;
    for (OffsetVec::const_iterator it = offsets.begin(); it != offsets.end(); ++it)
		{
			PartitionCell* thisCell = getCellAt(cellCenterX + it->x, cellCenterY + it->y);
			if (thisCell == nullptr)
				continue;

			for (CellAndObjectIntersection *thisCoi = thisCell->getFirstCoiInCell(); thisCoi; thisCoi = thisCoi->getNextCoi())
			{
				PartitionData *thisMod = thisCoi->getModule();
				Object *thisObj = thisMod->getObject();

				// never compare against ourself.
				if (thisObj == obj || thisObj == nullptr) 
					continue;

				// since an object can exist in multiple COIs, we use this to avoid processing
				// the same one more than once.
				if (thisMod->friend_getDoneFlag() == theIterFlag)
					continue;
				thisMod->friend_setDoneFlag(theIterFlag);
			
				Real thisDistSqr;
				Coord3D distVec;
				if (!(*distProc)(objPos, objToUse, thisObj->getPosition(), thisObj, thisDistSqr, distVec, closestDistSqr))
					continue;

				if (!filtersAllow(filters, thisObj))
					continue;

				// ok, this is within the range, and the filters allow it.
				// add it to the iter, if we have one....
				if (iterArg)
				{
					iterArg->insert(thisObj, thisDistSqr);
				}
				else
				{
					// hey, this is the new closest object! cool.
					// (note that we can't break out now 'cuz we have to finish examining the
					// rest of curRadius)
					closestObj = thisObj;
					closestDistSqr = thisDistSqr;
					closestVec = distVec;

					if (!foundAny)
					{
						// if not adding to iterArg, we want to stop once we have the closest object. 
						maxRadiusLimit = curRadius;
					}
					foundAny = true;
				}

			} // next coi
		}	// next cell in this radius
  } // next radius

#else // not FASTER_GCO

	CellOutwardIterator iter(this, cellCenterX, cellCenterY);
	if (maxDist < HUGE_DIST)
	{
		// don't go outwards any farther than necessary.
		Int max = worldToCellDist(maxDist) + 1;
		// default value for "max" is largest possible, based on map size, so we should
		// never make it any larger than that
		if (max < iter.getMaxRadius())
			iter.setMaxRadius(max);
	}

	Bool foundAny = false;

	static Int theIterFlag = 1;	// nonzero, thanks
	++theIterFlag;

	PartitionCell *thisCell;
	while ((thisCell = iter.nextNonEmpty()) != nullptr)
	{
		CellAndObjectIntersection *nextCoi;
		for (CellAndObjectIntersection *thisCoi = thisCell->getFirstCoiInCell(); thisCoi; thisCoi = nextCoi)
		{
			nextCoi = thisCoi->getNextCoi();
		
			PartitionData *thisMod = thisCoi->getModule();

			Object *thisObj = thisMod->getObject();

			// never compare against ourself.
			if (thisObj == obj) 
				continue;

			if (thisMod->friend_getDoneFlag() == theIterFlag)
				continue;

			thisMod->friend_setDoneFlag(theIterFlag);
		
			// hmm, ok, calc the distance.
			Real thisDistSqr;
			Coord3D distVec;
			if (!(*distProc)(objPos, objToUse, thisObj->getPosition(), thisObj, &thisDistSqr, &distVec, closestDistSqr))
				continue;

			// check the filters now
			if (!filtersAllow(filters, thisObj))
				continue;

			// ok, guess this is a winner!
			if (iterArg)
			{
				iterArg->insert(thisObj, thisDistSqr);
			}
			else
			{
				closestObj = thisObj;
				closestDistSqr = thisDistSqr;
				closestVec = distVec;

				if (!foundAny)
				{
					// if not adding to iterArg, we want to stop once we have the closest object. 
					// since all objects in this radius (and the next radius, due to slop) might
					// be slightly closer, we still have to check all of them. so set the termination
					// radius to be our-current-radius-plus-1. (if we ARE adding to the iterArg, we skip
					// this, cuz we want to go all the way out to the original max we specified as an arg.)
					iter.setMaxRadius(iter.getCurCellRadius() + 2);
				}
				foundAny = true;
			}
		}
	}
	
#endif  // not FASTER_GCO

	if (closestVecArg)
	{
		*closestVecArg = closestVec;
	}
	if (closestDistArg)
	{
		*closestDistArg = static_cast<Real>(sqrtf(closestDistSqr));
	}

#ifdef _DEBUG
	--theEntrancyCount;
#endif
#ifdef DUMP_PERF_STATS
	Int64 endTime64;
	GetPrecisionTimer(&endTime64);
	Int64 delta = (endTime64 - startTime64);
	s_timeInClosestObjects += delta;
	s_timeInClosestObjectsThisFrame += delta;
#endif

	return closestObj;	// might be null...
}

//-----------------------------------------------------------------------------
Object *PartitionManager::getClosestObject(
	const Object *obj, 
	Real maxDist, 
	DistanceCalculationType dc, 
	PartitionFilter **filters, 
	Real *closestDist,
	Coord3D *closestDistVec
)
{
	return getClosestObjects(obj, nullptr, maxDist, dc, filters, nullptr, closestDist, closestDistVec);
}

//-----------------------------------------------------------------------------
Object *PartitionManager::getClosestObject(
	const Coord3D *pos, 
	Real maxDist, 
	DistanceCalculationType dc, 
	PartitionFilter **filters, 
	Real *closestDist,
	Coord3D *closestDistVec
)
{
	return getClosestObjects(nullptr, pos, maxDist, dc, filters, nullptr, closestDist, closestDistVec);
}

//-----------------------------------------------------------------------------
void PartitionManager::getVectorTo(const Object *obj, const Object *otherObj, DistanceCalculationType dc, Coord3D& vec)
{
	DistCalcProc distProc = theDistCalcProcs[static_cast<Int>(dc)];
	Real distSqr;
	(*distProc)(obj->getPosition(), obj, otherObj->getPosition(), otherObj, distSqr, vec, HUGE_DIST_SQR);
}

//-----------------------------------------------------------------------------
void PartitionManager::getVectorTo(const Object *obj, const Coord3D *pos, DistanceCalculationType dc, Coord3D& vec)
{
	DistCalcProc distProc = theDistCalcProcs[static_cast<Int>(dc)];
	Real distSqr;
	(*distProc)(obj->getPosition(), obj, pos, nullptr, distSqr, vec, HUGE_DIST_SQR);
}

//-----------------------------------------------------------------------------
Real PartitionManager::getDistanceSquared(const Object *obj, const Object *otherObj, DistanceCalculationType dc, Coord3D *vec)
{
	DistCalcProc distProc = theDistCalcProcs[static_cast<Int>(dc)];
	Real thisDistSqr;
	Coord3D thisVec;
	(*distProc)(obj->getPosition(), obj, otherObj->getPosition(), otherObj, thisDistSqr, thisVec, HUGE_DIST_SQR);
	if (vec)
		*vec = thisVec;
	return thisDistSqr;
}

//-----------------------------------------------------------------------------
Real PartitionManager::getDistanceSquared(const Object *obj, const Coord3D *pos, DistanceCalculationType dc, Coord3D *vec)
{
	DistCalcProc distProc = theDistCalcProcs[static_cast<Int>(dc)];
	Real thisDistSqr;
	Coord3D thisVec;
	(*distProc)(obj->getPosition(), obj, pos, nullptr, thisDistSqr, thisVec, HUGE_DIST_SQR);
	if (vec)
		*vec = thisVec;
	return thisDistSqr;
}

//-----------------------------------------------------------------------------
// Gets the distance if obj were at goalPos.  Used to calculate attack position paths.
Real PartitionManager::getGoalDistanceSquared(const Object *obj, const Coord3D *goalPos, const Object *otherObj, DistanceCalculationType dc, Coord3D *vec)
{
	DistCalcProc distProc = theDistCalcProcs[static_cast<Int>(dc)];
	Real thisDistSqr;
	Coord3D thisVec;
	(*distProc)(goalPos, obj, otherObj->getPosition(), otherObj, thisDistSqr, thisVec, HUGE_DIST_SQR);
	if (vec)
		*vec = thisVec;
	return thisDistSqr;
}

//-----------------------------------------------------------------------------
// Gets the distance if obj were at goalPos.  Used to calculate attack position paths.
Real PartitionManager::getGoalDistanceSquared(const Object *obj, const Coord3D *goalPos, const Coord3D *otherPos, DistanceCalculationType dc, Coord3D *vec)
{
	DistCalcProc distProc = theDistCalcProcs[static_cast<Int>(dc)];
	Real thisDistSqr;
	Coord3D thisVec;
	(*distProc)(goalPos, obj, otherPos, nullptr, thisDistSqr, thisVec, HUGE_DIST_SQR);
	if (vec)
		*vec = thisVec;
	return thisDistSqr;
}

//-----------------------------------------------------------------------------
Real PartitionManager::getRelativeAngle2D( const Object *obj, const Object *otherObj )
{
	return getRelativeAngle2D(obj, otherObj->getPosition());
}

//-----------------------------------------------------------------------------
Real PartitionManager::getRelativeAngle2D( const Object *obj, const Coord3D *pos )
{
	Coord3D v;
	
	// compute vector to given position
	Coord3D objPos = *obj->getPosition();
	v.x = pos->x - objPos.x;
	v.y = pos->y - objPos.y;
	v.z = 0.0f;

	Real dist = static_cast<Real>(sqrtf(sqr(v.x) + sqr(v.y)));

	// normalize
	if (dist == 0.0f)
		return 0.0f;


	const Coord3D *dir = obj->getUnitDirectionVector2D();

	Real distInv = 1.0f / dist;
	v.x *= distInv;
	v.y *= distInv;
	v.z *= distInv;

	// dot of two unit vectors is cos of angle
	Real c = dir->x*v.x + dir->y*v.y; // + dir->z*v.z;

	// bound it in case of numerical error
	if (c < -1.0)
		c = -1.0;
	else if (c > 1.0)
		c = 1.0;

	Real value = static_cast<Real>(ACos( c ));

	// Determine sign by checking Z component of dir cross v
	// Note this is assumes 2D, and is identical to dotting the perpendicular of v with dir
	Real perpZ = dir->x * v.y - dir->y * v.x;
	if (perpZ < 0.0f)
		value = -value;

	// note: to make this 3D, 'dir' and 'v' can be normalized and dotted just as they are
	// to test sign, compute N = dir X v, then P = N x dir, then S = P . v, where sign of
	// S is sign of angle - MSB

	return value;
}

//-----------------------------------------------------------------------------
SimpleObjectIterator *PartitionManager::iterateObjectsInRange(
	const Object *obj, 
	Real maxDist, 
	DistanceCalculationType dc, 
	PartitionFilter **filters, 
	IterOrderType order
)
{
	MemoryPoolObjectHolder iterHolder;
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	iterHolder.hold(iter);

	getClosestObjects(obj, nullptr, maxDist, dc, filters, iter, nullptr, nullptr);

	iter->sort(order);
	iterHolder.release();
	return iter;
}

//-----------------------------------------------------------------------------
SimpleObjectIterator *PartitionManager::iterateObjectsInRange(
	const Coord3D *pos, 
	Real maxDist, 
	DistanceCalculationType dc, 
	PartitionFilter **filters, 
	IterOrderType order
)
{
	MemoryPoolObjectHolder iterHolder;
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	iterHolder.hold(iter);

	getClosestObjects(nullptr, pos, maxDist, dc, filters, iter, nullptr, nullptr);

	iter->sort(order);
	iterHolder.release();
	return iter;
}

//-----------------------------------------------------------------------------
SimpleObjectIterator* PartitionManager::iteratePotentialCollisions(
	const Coord3D* pos, 
	const GeometryInfo& geom,
	Real angle,
	Bool use2D
)
{	
	Real maxDist = geom.getBoundingSphereRadius();
	maxDist *= 1.1f;	// just a little slop

	MemoryPoolObjectHolder iterHolder;
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	iterHolder.hold(iter);

	PartitionFilterWouldCollide filter(*pos, geom, angle, true);
	PartitionFilter *filters[] = { &filter, nullptr };

	getClosestObjects(nullptr, pos, maxDist, use2D ? DistanceCalculationType::FROM_BOUNDINGSPHERE_2D : DistanceCalculationType::FROM_BOUNDINGSPHERE_3D, filters, iter, nullptr, nullptr);

	iterHolder.release();
	return iter;
}

//-----------------------------------------------------------------------------
Bool PartitionManager::isColliding( const Object *a, const Object *b ) const
{
	//Make sure we have objects
  if( !a || !b )
	{
    return false;
	}

	//Get both object's partition data
  const PartitionData* ad = a->friend_getConstPartitionData();
  const PartitionData* bd = b->friend_getConstPartitionData();

	//Make sure we got it
  if( !ad || !bd )
	{
    return false;
	}

	//See if the partition data collides.
	return ad->friend_collidesWith( bd, nullptr );
}

//-----------------------------------------------------------------------------
SimpleObjectIterator *PartitionManager::iterateAllObjects(PartitionFilter **filters)
{
	MemoryPoolObjectHolder iterHolder;
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	iterHolder.hold(iter);

	// no distance constraints; we're gonna have to process 'em all anyway,
	// so go thru in the fastest way we know how.
	PartitionData *mod, *nextMod;
	for( mod = m_moduleList; mod; mod = nextMod )
	{
		nextMod = mod->getNext();
		Object *obj = mod->getObject();
		if (obj && filtersAllow(filters, obj))
		{
			iter->insert( obj );
		}
	}

	iterHolder.release();
	return iter;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool PartitionManager::tryPosition( const Coord3D *center,
																		Real dist,
																		Real angle,
																		const FindPositionOptions *options,
																		Coord3D *result )
{

	// compute the spot on the terrain we've picked
	Coord3D pos;
	pos.x = dist * Cos( angle ) + center->x;
	pos.y = dist * Sin( angle ) + center->y;

	PathfindLayerEnum layer = LAYER_GROUND;
	if ((options->flags & FPF_USE_HIGHEST_LAYER) != 0)
	{
		pos.z = 99999.0f;
		layer = TheTerrainLogic->getHighestLayerForDestination(&pos);
		pos.z = TheTerrainLogic->getLayerHeight(pos.x, pos.y, layer);
		// ensure we are slightly above the bridge, to account for fudge & sloppy art
		if (layer != LAYER_GROUND)
			pos.z += 1.0f;
	}
	else
	{
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
	}

	if (fabs(pos.z - center->z) > options->maxZDelta)
		return FALSE;

	//
	// we don't usually find positions on cliffs.
	// someday, add bit options for this, like for water.
	//
	if (TheTerrainLogic->isCliffCell(pos.x, pos.y) && layer == LAYER_GROUND)
		return FALSE;

	//
	// srj sez:
	// we don't usually find positions on impassable areas.
	// someday, add bit options for this, like for water.
	//
	{
		Int cellX = REAL_TO_INT_FLOOR(pos.x / PATHFIND_CELL_SIZE);
		Int cellY = REAL_TO_INT_FLOOR(pos.y / PATHFIND_CELL_SIZE);
		PathfindCell* cell = TheAI->pathfinder()->getCell(layer, cellX, cellY);
		if (!cell || cell->getType() == PathfindCell::CELL_IMPASSABLE)
		{
			return FALSE;
		}
		if( BitTest(options->flags, FPF_CLEAR_CELLS_ONLY) && cell->getType() != PathfindCell::CELL_CLEAR )
			return FALSE;

	}

	//
	// we don't usually find positions in the water unless we explicitly say that's OK,
	// or if the option is set we can only pick position underwater
	//
	if( BitTest( options->flags, FPF_IGNORE_WATER ) == FALSE )
	{
		Bool isUnderwater = TheTerrainLogic->isUnderwater( pos.x, pos.y );

		//
		// if we want water spots only and this is underwater it's no good, otherwise we want
		// the default behavior where underwater spots are invalid
		//
		if( BitTest( options->flags, FPF_WATER_ONLY ) && (isUnderwater == FALSE || layer != LAYER_GROUND) )
			return FALSE;
		else if( isUnderwater == TRUE && layer == LAYER_GROUND )
			return FALSE;

	}  // end if

	// object checks
	if( BitTest( options->flags, FPF_IGNORE_ALL_OBJECTS ) == FALSE )
	{

		//
		// iterate the potential collisions at this location using a 
		// very small sphere geometry around the point
		//
		GeometryInfo geometry( GEOMETRY_SPHERE, TRUE, 5.0f, 5.0f, 5.0f );
		ObjectIterator *iter = ThePartitionManager->iteratePotentialCollisions( &pos, geometry, angle, true );
		MemoryPoolObjectHolder hold( iter );
//	Bool overlap = FALSE;

		for( Object *them = iter->first(); them; them = iter->next() )
		{

			// if this is the ignore object, ignore it
			if( them == options->ignoreObject )
				continue;  // continue in the object iterator for loop

			// relationship checks
			if( options->relationshipObject )
			{

				// if this is an ally/neutral unit and we ignore those, do so
				if( BitTest( options->flags, FPF_IGNORE_ALLY_OR_NEUTRAL_UNITS ) == TRUE &&
						options->relationshipObject->getRelationship( them ) != ENEMIES &&
						(them->isKindOf( KINDOF_INFANTRY ) || them->isKindOf( KINDOF_VEHICLE )) )
					continue;

				// if this is an ally/neutral structure and we ignore those, do so
				if( BitTest( options->flags, FPF_IGNORE_ALLY_OR_NEUTRAL_STRUCTURES ) == TRUE &&
						options->relationshipObject->getRelationship( them ) != ENEMIES &&
						them->isKindOf( KINDOF_STRUCTURE ) )
					continue;

				// if this is an enemy unit and we ignore those, do so
				if( BitTest( options->flags, FPF_IGNORE_ENEMY_UNITS ) == TRUE &&
						options->relationshipObject->getRelationship( them ) == ENEMIES &&
						(them->isKindOf( KINDOF_INFANTRY ) || them->isKindOf( KINDOF_VEHICLE )) )
					continue;

				// if this is an enemy structure and we ignore those, do so
				if( BitTest( options->flags, FPF_IGNORE_ENEMY_STRUCTURES ) == TRUE &&
						options->relationshipObject->getRelationship( them ) == ENEMIES &&
						them->isKindOf( KINDOF_STRUCTURE ) )
					continue;

			}  // end if, relationship checks

			//
			// if we have a source that must path to the destination we will ignore that too
			// NOTE: If needed, we can reorganize the meaning of this method to be
			// more explicit on when to pay attention to any 'source' objects and when not too
			// but for now we're keeping the API very simple for the kinds of queries
			// that we're likely to do for an RTS (CBD)
			//
			if( them == options->sourceToPathToDest )
				continue;  // continue in the object iterator for loop

			// oops, we have overlapped something
			return FALSE;

		}  // end for, them

	}  // end if

	//
	// finally ... if sourceToPathDest is valid ... the source object must be able to
	// find a path the the position we have selected.  We want to do this last because
	// a pathfind can sometimes be an relatively expensive operation
	//
	if( options->sourceToPathToDest )
	{
		const AIUpdateInterface *ai = options->sourceToPathToDest->getAIUpdateInterface();

		// check for path existence
		if( ai && TheAI->pathfinder()->clientSafeQuickDoesPathExist( ai->getLocomotorSet(),
																									options->sourceToPathToDest->getPosition(),
																									&pos ) == FALSE )
				return FALSE;

	}  // end if

	// save result and return TRUE for position found
	*result = pos;
	return TRUE;

}  // end tryPosition

//
// the following determines how fast we expand our concentric ring search for the
// find position methods
//
static Real ringSpacing = 5.0f;

//-------------------------------------------------------------------------------------------------
/** This method will attempt to find a legal postion from the center position specified,
	* at least minRadis away from it, but no more than maxRadius away.
	*
	* Return TRUE if position is found and that position is returned in 'result'
	* return FALSE no legal position exists or invalid params 
	*/
//-------------------------------------------------------------------------------------------------
Bool PartitionManager::findPositionAround( const Coord3D *center, 
																					 const FindPositionOptions *options, 
																					 Coord3D *result )
{

	// sanity
	if( center == nullptr || result == nullptr || options == nullptr )
		return FALSE;

	Region3D extent;
	TheTerrainLogic->getMaximumPathfindExtent(&extent);
	// If the goal is off the map, it is a scripted setup, so just
	// use the center.
	if (!extent.isInRegionNoZ(center)) {
		*result = *center;
		return true;
	}
	// sanity, FPF_IGNORE_WATER and FPF_WATER_ONLY are mutually exclusive
	DEBUG_ASSERTCRASH( !(BitTest( options->flags, FPF_IGNORE_WATER ) == TRUE &&
										   BitTest( options->flags, FPF_WATER_ONLY ) == TRUE),
										 ("PartitionManager::findPositionAround - The options FPF_WATER_ONLY and FPF_IGNORE_WATER are mutually exclusive.  You cannot use them together\n") );

	// pick a random angle from the center location to start at
	Real startAngle;
	if( options->startAngle == RANDOM_START_ANGLE )
		startAngle = GameLogicRandomValueReal( 0.0f, TWO_PI );
	else
		startAngle = options->startAngle;

	// start the search at the most inner ring (minRadius) and expand to outer most ring (maxRadius)
	for( Real dist = options->minRadius; dist <= options->maxRadius; dist += ringSpacing )
	{

		//
		// given the spacing that we've been using for the angles on the 'idea' inner circle,
		// we will need more points on larger circles to cover more ground
		//
		Real angleSpacing;
		if( dist == options->minRadius )
			angleSpacing = TWO_PI;
		else
			angleSpacing = (ringSpacing / (dist + 1.0f)) * (TWO_PI / 6.0f);  // larger float = more samples

		//
		// on this "ring", try all the angles available to us in a circle ... we'll start at
		// the 'startAngle', then try 'angleSpacing' to the left, then 'angleSpacing' to the
		// right, then '2 angleSpacings' to the left etc., ping ponging around the start angle
		// until we've done them all the way around the circle
		//

		// how many samples will we test
		Int samples = REAL_TO_INT_CEIL( (TWO_PI / angleSpacing) / 2.0f);
		for( Int i = 0; i < samples; ++i )
		{

			// try one "side"
			if( tryPosition( center, dist, startAngle + angleSpacing * i, options, result ) == TRUE )
				return TRUE;

			//
			// try the other "side" ... note for the first point at minRadius this position is
			// the same so we won't test it again in that case
			//
			if( i != 0 )
				if( tryPosition( center, dist, startAngle - angleSpacing * i, options, result ) == TRUE )
					return TRUE;	

		}  // end if

	}  // end for, dist

	// no position was able to be found
	return FALSE;

}  // end findPositionAround

//-----------------------------------------------------------------------------
// Uses Bresenham line algorithm from www.gamedev.net.
Int PartitionManager::iterateCellsAlongLine(const Coord3D& pos, const Coord3D& posOther, CellAlongLineProc proc, void* userData)
{
	ICoord2D start, end, delta;
	Int x, y;
	Int xinc1, xinc2;
	Int yinc1, yinc2;
	Int den, num, numadd;
	Int numpixels;

	worldToCell(pos.x, pos.y, &start.x, &start.y);
	worldToCell(posOther.x, posOther.y, &end.x, &end.y);

	delta.x = abs(end.x - start.x);			// The difference between the x's
	delta.y = abs(end.y - start.y);			// The difference between the y's
	x = start.x;												// Start x off at the first pixel
	y = start.y;												// Start y off at the first pixel

	if (end.x >= start.x)								// The x-values are increasing
	{
		xinc1 = 1;
		xinc2 = 1;
	}
	else																// The x-values are decreasing
	{
		xinc1 = -1;
		xinc2 = -1;
	}

	if (end.y >= start.y)               // The y-values are increasing
	{
		yinc1 = 1;
		yinc2 = 1;
	}
	else																// The y-values are decreasing
	{
		yinc1 = -1;
		yinc2 = -1;
	}
	Bool checkY = true;
	if (delta.x >= delta.y)							// There is at least one x-value for every y-value
	{
		xinc1 = 0;												// Don't change the x when numerator >= denominator
		yinc2 = 0;												// Don't change the y for every iteration
		den = delta.x;
		num = delta.x / 2;
		numadd = delta.y;
		numpixels = delta.x;							// There are more x-values than y-values
	}
	else																// There is at least one y-value for every x-value
	{
		checkY = false;
		xinc2 = 0;												// Don't change the x for every iteration
		yinc1 = 0;												// Don't change the y when numerator >= denominator
		den = delta.y;
		num = delta.y / 2;
		numadd = delta.x;
		numpixels = delta.y;							// There are more y-values than x-values
	}

	for (Int curpixel = 0; curpixel <= numpixels; curpixel++)
	{
		PartitionCell* cell = ThePartitionManager->getCellAt(x, y);	// might be null if off the edge
		DEBUG_ASSERTCRASH(cell != nullptr, ("off the map"));
		if (cell)
		{
			Int ret = (*proc)(cell, userData);
			if (ret != 0)
				return ret;	// bail early
		}

		num += numadd;										// Increase the numerator by the top of the fraction
		if (num >= den)										// Check if numerator >= denominator
		{
			num -= den;											// Calculate the new numerator value
			x += xinc1;											// Change the x as appropriate
			y += yinc1;											// Change the y as appropriate
		}
		x += xinc2;												// Change the x as appropriate
		y += yinc2;												// Change the y as appropriate
	}
	
	return 0;
}

//-----------------------------------------------------------------------------
Int PartitionManager::iterateCellsBreadthFirst(const Coord3D *pos, CellBreadthFirstProc proc, void *userData)
{
	// starting at pos, iterate the cells in the following manner:
	// left, up, right, down
	// using Breadth-first search.
	// Call proc on each cell, and if it returns non-zero, then return the cell index
	// -1 means error, but we should add a define later for this.

	Int cellX, cellY;
	ThePartitionManager->worldToCell(pos->x, pos->y, &cellX, &cellY);
	
	// Note, bool. not Bool, cause bool will cause this to be a bitfield.
	std::vector<bool> bitField;
	Int cellCount = m_cellCountX * m_cellCountY;
	bitField.resize(cellCount);
	// 0 means not done, 1 means done.

	for (Int i = 0; i < cellCount; ++i) {
		bitField[i] = false;
	}

	std::queue<PartitionCell *> cellQ;

	// mark the starting cell done
	bitField[cellY * m_cellCountX + cellX] = true;
	cellQ.push(&m_cells[cellY * m_cellCountX + cellX]);

	Int curX = cellX;
	Int curY = cellY;
	while (!cellQ.empty()) {
		// first, add the neighbors
		// left
		if (curX - 1 >= 0) {
			if (!bitField[curY * m_cellCountX + curX - 1]) {
				bitField[curY * m_cellCountX + curX - 1] = true;
				cellQ.push(&m_cells[curY * m_cellCountX + curX - 1]);
			}
		}

		// top
		if (curY - 1 >= 0) {
			if (!bitField[(curY - 1) * m_cellCountX + curX]) {
				bitField[(curY - 1) * m_cellCountX + curX] = true;
				cellQ.push(&m_cells[(curY - 1) * m_cellCountX + curX]);
			}
		}

		// right
		if (curX + 1 < m_cellCountX) {
			if (!bitField[curY * m_cellCountX + curX + 1]) {
				bitField[curY * m_cellCountX + curX + 1] = true;
				cellQ.push(&m_cells[curY * m_cellCountX + curX + 1]);
			}
		}

		// bottom
		if (curY + 1 > 0) {
			if (!bitField[(curY + 1) * m_cellCountX + curX]) {
				bitField[(curY + 1) * m_cellCountX + curX] = true;
				cellQ.push(&m_cells[(curY + 1) * m_cellCountX + curX]);
			}
		}

		// now process the current top.
		PartitionCell *curCell = cellQ.front();
		cellQ.pop();
		curX = curCell->getCellX();
		curY = curCell->getCellY();

		if ((proc)(curCell, userData) != 0)
			return (curCell->getCellY() * m_cellCountX + curCell->getCellX());
	}

	return -1;
}

//-----------------------------------------------------------------------------
#ifdef NO_BAD_AND_INACCURATE
static Real calcDist2D(Real x1, Real y1, Real x2, Real y2)
{
	return sqrtf(sqr(x1-x2) + sqr(y1-y2));
}
#endif

//-----------------------------------------------------------------------------
Bool PartitionManager::isClearLineOfSightTerrain(const Object* obj, const Coord3D& objPos, const Object* other, const Coord3D& otherPos)
{
	Coord3D pos, posOther;
	
	if (obj)
	{
		pos = *obj->getPosition();
		// note that we want to measure from the top of the collision
		// shape, not the bottom! (most objects have eyes a lot closer
		// to their head than their feet. if we have really odd critters
		// with eye-feet, we'll need to change this assumption.)
		pos.z += obj->getGeometryInfo().getMaxHeightAbovePosition();
	}
	else
	{
		pos = objPos;
	}

	if (other)
	{
		posOther = *other->getPosition();
		// note that we want to measure from the top of the collision
		// shape, not the bottom! (most objects have eyes a lot closer
		// to their head than their feet. if we have really odd critters
		// with eye-feet, we'll need to change this assumption.)
		posOther.z += other->getGeometryInfo().getMaxHeightAbovePosition();
	}
	else
	{
		posOther = otherPos;
	}

#ifdef NO_BAD_AND_INACCURATE
/*
	This looks tempting, and in theory would be better than the below code; however, it
	is not (yet) accurate enough for our targeting needs. what we really need to do is add a Bresenham
	line iterator to the TerrainLogic itself, which would probably suffice. Until then, this code is
	left in as an example of what NOT to do. (srj)
*/
	Real maxZ;
	Coord2D maxZPos;
	Bool valid = estimateTerrainExtremesAlongLine(pos, posOther, nullptr, &maxZ, nullptr, &maxZPos);
	DEBUG_ASSERTCRASH(valid, ("this should never happen unless both positions are off-map"));
	if (!valid)
		return true;

	Real terrainAtHighPoint = maxZ;

	Real totalDist2D = calcDist2D(pos.x, pos.y, posOther.x, posOther.y);
	Real terrainDist2D = calcDist2D(pos.x, pos.y, maxZPos.x, maxZPos.y);
	
	const Real TINY_DIST = 0.01f;
	Real percent = (totalDist2D > TINY_DIST) ? (terrainDist2D / totalDist2D) : 0.0f;
	if (percent < 0.0f) percent = 0.0f;
	if (percent > 1.0f) percent = 1.0f;

	Real lineOfSightAtHighPoint = pos.z + (posOther.z - pos.z) * percent;

	// if terrainAtHighPoint > lineOfSightAtHighPoint, we can't see.
	// add a little fudge to account for slop.
	const Real LOS_FUDGE = 0.5f;
	if (terrainAtHighPoint > lineOfSightAtHighPoint + LOS_FUDGE)
	{
		//DEBUG_LOG(("isClearLineOfSightTerrain fails\n"));
		return false;
	}

	return true;

#else
	return TheTerrainLogic->isClearLineOfSight(pos, posOther);
#endif
}

//-----------------------------------------------------------------------------
Real PartitionManager::getGroundOrStructureHeight(Real posx, Real posy)
{
	// get the terrain height
	Real terrainHeightHere = TheTerrainLogic->getGroundHeight( posx, posy );

	// scan all objects in the radius of our extent and find the tallest height among them
	PartitionFilterAcceptByKindOf filter1( MAKE_KINDOF_MASK( KINDOF_STRUCTURE ), KINDOFMASK_NONE );
	PartitionFilter *filters[] = { &filter1, nullptr };
  Coord3D pos;
  pos.x = posx;
  pos.y = posy;
  pos.z = terrainHeightHere;
	const Real RANGE = 1.0f;
	ObjectIterator *iter = iterateObjectsInRange( &pos, RANGE, DistanceCalculationType::FROM_BOUNDINGSPHERE_2D, filters );
	MemoryPoolObjectHolder hold( iter );

	Real tallestHeight = 0.0f;
	Real thisHeight;
	for( Object* obj = iter->first(); obj; obj = iter->next() )
	{
		// store the height of the tallest object under us
		thisHeight = obj->getGeometryInfo().getMaxHeightAbovePosition();
		if( thisHeight > tallestHeight )
			tallestHeight = thisHeight;

	}
		
  return terrainHeightHere + tallestHeight;
}
