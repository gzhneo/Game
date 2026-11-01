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

#include "GameLogic/AIPathfind.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/CRCDebug.h"
#include "Common/GlobalData.h"
#include "Common/LatchRestore.h"	 
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"							 
#include "GameClient/Line2D.h"
#include "GameLogic/AI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/ScriptEngine.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"
#include "Common/PerfMetrics.h"
#include "Common/PerfTimer.h"
#include "Engine/Clock.h"
#include <algorithm>	// std::sort in PathfindOpenList::releaseAll

#include "AIPathfindInternal.h"

enum { PATHFIND_CELLS_PER_FRAME=5000}; // Number of cells we will search pathfinding per frame.

// Path queue and searches: A*, ground and hierarchical paths, closest paths, path building

/**
 * Queues an object to do a pathfind.
 * It will call the object's ai update->doPathfind() during processPathfindQueue().
 */
Bool Pathfinder::queueForPath(ObjectID id)
{
#if defined(_DEBUG)
	{
		Object *tmpObj = TheGameLogic->findObjectByID(id);
		if (tmpObj) {
			AIUpdateInterface *tmpAI = tmpObj->getAIUpdateInterface();
			if (tmpAI) {
				const Coord3D* pos = tmpAI->friend_getRequestedDestination();
				DEBUG_ASSERTLOG(pos->x != 0.0 && pos->y != 0.0, ("Queueing pathfind to (0, 0), usually a bug. (Unit Name: '%s', Type: '%s' \n", tmpObj->getName().str(), tmpObj->getTemplate()->getName().str()));
			}
		}
	}
#endif
	
	/* Check & see if we are already queued. */
	Int slot = m_queuePRHead;
	while (slot != m_queuePRTail) {
		if (m_queuedPathfindRequests[slot] == id) {
			return true;
		}
		slot++;
		if (slot >= PATHFIND_QUEUE_LEN) {
			slot = 0;
		}
	}

	// Tail is the first available slot.
	Int nextSlot = m_queuePRTail+1;
	if (nextSlot >= PATHFIND_QUEUE_LEN) {
		nextSlot = 0;
	}
	if (nextSlot==m_queuePRHead) {
		DEBUG_CRASH(("Ran out of pathfind queue slots."));
		return false;
	}
	m_queuedPathfindRequests[m_queuePRTail] = id;
	m_queuePRTail = nextSlot;
	return true;
}

#if defined _DEBUG
void Pathfinder::doDebugIcons(void) {
	const Int FRAMES_TO_SHOW_OBSTACLES = 100;
	extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
	// render AI debug information
	if (TheGlobalData->m_debugAI!=AI_DEBUG_CELLS && TheGlobalData->m_debugAI!=AI_DEBUG_TERRAIN) {
		return;
	}

		RGBColor color;
		color.red = color.green = color.blue = 0;	
		addIcon(nullptr, 0, 0, color);	 // clear.
		Coord3D topLeftCorner;
		Bool showCells = TheGlobalData->m_debugAI==AI_DEBUG_CELLS;
		Int i;
		for (i=0; i<=LAYER_LAST; i++) {
			m_layers[i].doDebugIcons();
		}
		if (!showCells)	{
			frameToShowObstacles = TheGameLogic->getFrame()+FRAMES_TO_SHOW_OBSTACLES;
			//return;
		}	
		// show the pathfind grid
		for( int j=0; j<getExtent()->y; j++ )
		{
			topLeftCorner.y = static_cast<Real>(j) * PATHFIND_CELL_SIZE_F;

			for( int i3=0; i3<getExtent()->x; i3++ )
			{
				topLeftCorner.x = static_cast<Real>(i3) * PATHFIND_CELL_SIZE_F;
				
				color.red = color.green = color.blue = 0;	
				Bool empty = true;
				
				const PathfindCell *cell = TheAI->pathfinder()->getCell( LAYER_GROUND, i3, j );
				if (cell)
				{
					switch (cell->getType())
					{
						case PathfindCell::CELL_CLIFF:
							color.red = 1;
							empty = false;
							break;
						case PathfindCell::CELL_BRIDGE_IMPASSABLE:
							color.blue = color.red = 1;
							empty = false;
							break;
						case PathfindCell::CELL_IMPASSABLE:
							color.green = 1;
							empty = false;
							break;

						case PathfindCell::CELL_WATER:
							color.blue = 1;
							empty = false;
							break;

						case PathfindCell::CELL_RUBBLE:
							color.red = 1;
							color.green = 0.5;
							empty = false;
							break;

						case PathfindCell::CELL_OBSTACLE:
							color.red = color.green = 1;
							empty = false;
							break;
						default:
							if (cell->getPinched()) {
								color.blue = color.green = 0.7f;
								empty = false;
							}
							break;
					}
				}
				if (showCells) {
					empty = true;
					color.red = color.green = color.blue = 0;	
					if (empty && cell) {
						if (cell->getFlags()!=PathfindCell::NO_UNITS) {
							empty = false;
							if (cell->getFlags() == PathfindCell::UNIT_GOAL) {
								color.red = 1;
							}	else if (cell->getFlags() == PathfindCell::UNIT_PRESENT_FIXED) {
								color.green = color.blue = color.red = 1;
							}	else if (cell->getFlags() == PathfindCell::UNIT_PRESENT_MOVING) {
								color.green = 1;
							}	else {
								color.green = color.red = 1;
							}
						}
						if (cell->isAircraftGoal()) {
							empty = false;
							color.red = 0;
							color.green = color.blue = 1;
						}
					}
				}
				if (!empty) {
					Coord3D loc;
					loc.x = topLeftCorner.x + PATHFIND_CELL_SIZE_F/2.0f;
					loc.y = topLeftCorner.y + PATHFIND_CELL_SIZE_F/2.0f;
					loc.z = TheTerrainLogic->getGroundHeight(loc.x , loc.y);
					addIcon(&loc, PATHFIND_CELL_SIZE_F*0.8f, FRAMES_TO_SHOW_OBSTACLES-1, color);
				}
			}

	}
}
#endif

//-------------------------------------------------------------------------------------------------
/**
 * Create an aircraft path.  Just jogs around tall buildings marked with KINDOF_AIRCRAFT_PATH_AROUND.
 */
Path *Pathfinder::getAircraftPath( const Object *obj, const Coord3D *to )
{
	// for now, quick path objects don't pathfind, generally airborne units
	// build a trivial one-node path containing destination, then avoid buildings.
	

	Path *thePath = newInstance(Path);
	const AIUpdateInterface *ai = obj->getAI();
	ObjectID avoidObject = INVALID_ID;
	if (ai) {
		avoidObject = ai->getBuildingToNotPathAround();
	}

	// If it is an aircraft that circles (like raptors & migs) we need to adjust the destination
	// to one that doesn't clip buildings.
	Bool checkClips = false;
	if (ai && ai->getCurLocomotor()) {
		if (ai->getCurLocomotor()->getAppearance() == LOCO_WINGS) {
			checkClips = true;
		}
	}

	Real radius = 100;
	Coord3D adjDest = *to;
	if (checkClips) {
		circleClipsTallBuilding(obj->getPosition(), to, radius, avoidObject, &adjDest);
	}
	thePath->prependNode(&adjDest, LAYER_GROUND);
	Coord3D pos = *obj->getPosition();
	pos.z = to->z;
	thePath->prependNode( &pos, LAYER_GROUND );
	Int limit = 20;
	PathNode *curNode = thePath->getFirstNode();
	while (curNode && curNode->getNext()) {
		Coord3D newPos1, newPos2, newPos3;
		if (segmentIntersectsTallBuilding(curNode, curNode->getNext(), avoidObject, &newPos1, &newPos2, &newPos3)) {
			PathNode *newNode3 = newInstance(PathNode);
			newNode3->setPosition( &newPos3 );
			newNode3->setLayer(LAYER_GROUND);	
			curNode->append(newNode3);
			PathNode *newNode2 = newInstance(PathNode);
			newNode2->setPosition( &newPos2 );
			newNode2->setLayer(LAYER_GROUND);	
			curNode->append(newNode2);
			PathNode *newNode1 = newInstance(PathNode);
			newNode1->setPosition( &newPos1 );
			newNode1->setLayer(LAYER_GROUND);	
			curNode->append(newNode1);
			curNode = newNode2;
		}
		curNode = curNode->getNext();
		limit--;
		if (limit<0) break;
	}

	curNode = thePath->getFirstNode();
	while (curNode && curNode->getNext()) {
		curNode->setNextOptimized(curNode->getNext());
		curNode = curNode->getNext();
	}
	thePath->markOptimized();
	if (TheGlobalData->m_debugAI==AI_DEBUG_PATHS) {
		TheAI->pathfinder()->setDebugPath(thePath);
	}

	return thePath;
}

//-------------------------------------------------------------------------------------------------
void Pathfinder::formatStatistics( char *text, size_t size ) const
{
	text[0] = 0;
	if (m_statTicks == 0)
		return;
	const double ticks = static_cast<double>(m_statTicks);
	snprintf(text, size, "PFADSUCHE %.4f ms je Tick mit Anfragen (%u Ticks, hoechstens %.3f ms) anfragen %u zellen %.0f "
		"warteschlange %.1f hoechstens %u rest %.2f anfragen_je_tick %u %u %u %u einzeln_hoechstens %.3f ms",
		m_statQueueMs / ticks, m_statTicks, m_statMaxMs, m_statRequests, m_statCells,
		m_statQueueLength / ticks, m_statQueueMax, m_statLeftOver / ticks,
		m_statPerTick[0], m_statPerTick[1], m_statPerTick[2], m_statPerTick[3], m_statRequestMaxMs);
}

void Pathfinder::noteFailedPath( const Object *obj, const PathfindCell *goalCell )
{
	++m_statPathFailed;
	if (s_statInfoExhausted != m_statExhaustedAtSearchStart)
	{
		++m_statPathFailedExhausted;
		m_statFreeSumExhausted += m_statFreeAtSearchStart;
	}
	if (obj == nullptr || goalCell == nullptr)
		return;
	const UnsignedInt frame = TheGameLogic->getFrame();
	FailedPath &last = m_statLastFailed[obj->getID() & 1023];
	if (last.id == obj->getID() && last.x == goalCell->getXIndex() && last.y == goalCell->getYIndex()
		&& frame - last.frame <= 300)
		++m_statPathFailedRepeat;
	last.id = obj->getID();
	last.x = goalCell->getXIndex();
	last.y = goalCell->getYIndex();
	last.frame = frame;
}

void Pathfinder::nextLineMemoGeneration( void )
{
	// 31 bits; 0 means "no memo"
	m_lineMemoGeneration = (m_lineMemoGeneration + 1) & 0x7FFFFFFFu;
	if (m_lineMemoGeneration == 0)
		m_lineMemoGeneration = 1;
}

void Pathfinder::formatSearchStatistics( char *text, size_t size ) const
{
	static const char *const names[SEARCH_KINDS] = { "pfad", "naechster", "angriff", "sicher", "flicken", "kosten",
		"boden", "hierarchisch", "ausweichen" };
	size_t used = static_cast<size_t>(snprintf(text, size, "PFADSUCHE_ARTEN"));
	for (Int i = 0; i < SEARCH_KINDS && used < size; ++i)
	{
		const SearchStats &s = m_statSearch[i];
		if (s.searches == 0)
			continue;
		used += static_cast<size_t>(snprintf(text + used, size - used, " %s %u zellen %.0f ms %.1f hoechstens %.2f",
			names[i], s.searches, s.cells, s.ms, s.maxMs));
	}
	if (used < size)
		snprintf(text + used, size - used, " sichtlinie %.0f schritte %.0f abbrueche %.0f vorrat_leer %u"
			" pfad_fehlschlaege %u davon_vorrat_leer %u wiederholt %u frei_beim_start mittel %.0f kleinste %u"
			" frei_bei_vorrat_leer %.0f",
			m_statLineCalls, m_statLineSteps, m_statLineAborts, s_statInfoExhausted,
			m_statPathFailed, m_statPathFailedExhausted, m_statPathFailedRepeat,
			m_statFreeSamples ? m_statFreeSum / m_statFreeSamples : 0.0, m_statFreeSamples ? m_statFreeMin : 0u,
			m_statPathFailedExhausted ? m_statFreeSumExhausted / m_statPathFailedExhausted : 0.0);
}

/** 
 * Process some path requests in the pathfind queue.
 */
//DECLARE_PERF_TIMER(processPathfindQueue)
void Pathfinder::processPathfindQueue(void)
{
	//USE_PERF_TIMER(processPathfindQueue)
	if (!m_isMapReady) {
		return;
	}
#ifdef DEBUG_QPF
#if defined _DEBUG
	Int startTimeMS = Engine::Clock::milliseconds();
	__int64 startTime64;
	double timeToUpdate=0.0f;
	__int64 endTime64,freq64;
	QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER *>(&freq64));
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&startTime64));
#endif
#endif



	if (  
#ifdef forceRefreshCalling
#pragma message("AHHHH!, forced calls to pathzonerefresh still in code...  notify M Lorenzen")
    s_stopForceCalling==FALSE || 
#endif
    m_zoneManager.needToCalculateZones()) 
  {
		m_zoneManager.calculateZones(m_map, m_layers, m_extent);
		return;
	}

	// Get the current logical extent.
	Region3D terrainExtent;
	TheTerrainLogic->getExtent( &terrainExtent );
	IRegion2D bounds;
	bounds.lo.x = REAL_TO_INT_FLOOR(terrainExtent.lo.x / PATHFIND_CELL_SIZE_F);
	bounds.hi.x = REAL_TO_INT_FLOOR(terrainExtent.hi.x / PATHFIND_CELL_SIZE_F);
	bounds.lo.y = REAL_TO_INT_FLOOR(terrainExtent.lo.y / PATHFIND_CELL_SIZE_F);
	bounds.hi.y = REAL_TO_INT_FLOOR(terrainExtent.hi.y / PATHFIND_CELL_SIZE_F);
	bounds.hi.x--;
	bounds.hi.y--;
	m_logicalExtent = bounds;

	m_cumulativeCellsAllocated = 0;	// Number of pathfind cells examined.
#ifdef DEBUG_QPF
	Int pathsFound = 0;
#endif
	const Engine::ScopedTimer queueTimer;
	const Bool anyRequest = m_queuePRTail != m_queuePRHead;
	const UnsignedInt queueLength = static_cast<UnsignedInt>((m_queuePRTail - m_queuePRHead + PATHFIND_QUEUE_LEN) % PATHFIND_QUEUE_LEN);
	UnsignedInt requestsThisTick = 0;
	while (m_cumulativeCellsAllocated < PATHFIND_CELLS_PER_FRAME && 
		m_queuePRTail!=m_queuePRHead) {
		++m_statRequests;
		++requestsThisTick;
		const Engine::ScopedTimer requestTimer;
		Object *obj = TheGameLogic->findObjectByID(m_queuedPathfindRequests[m_queuePRHead]);
		m_queuedPathfindRequests[m_queuePRHead] = INVALID_ID;
		if (obj) {
			AIUpdateInterface *ai = obj->getAIUpdateInterface();
			if (ai) {
				ai->doPathfind(this);
#ifdef DEBUG_QPF
				pathsFound++;
#endif
			}
		}
		m_queuePRHead = m_queuePRHead+1;
		if (m_queuePRHead >= PATHFIND_QUEUE_LEN) {
			m_queuePRHead = 0;
		}
		const double requestMs = requestTimer.elapsedMilliseconds();
		if (requestMs > m_statRequestMaxMs) m_statRequestMaxMs = requestMs;
	}
	if (anyRequest) {
		const double ms = queueTimer.elapsedMilliseconds();
		m_statQueueMs += ms;
		if (ms > m_statMaxMs) m_statMaxMs = ms;
		m_statCells += m_cumulativeCellsAllocated;
		m_statQueueLength += queueLength;
		if (queueLength > m_statQueueMax) m_statQueueMax = queueLength;
		m_statLeftOver += static_cast<UnsignedInt>((m_queuePRTail - m_queuePRHead + PATHFIND_QUEUE_LEN) % PATHFIND_QUEUE_LEN);
		++m_statPerTick[requestsThisTick <= 1 ? 0 : requestsThisTick <= 3 ? 1 : requestsThisTick <= 7 ? 2 : 3];
		++m_statTicks;
	}
	if (pathsFound>0) {
#ifdef DEBUG_QPF
#if defined _DEBUG
		QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&endTime64));
		timeToUpdate = (static_cast<double>(endTime64-startTime64) / static_cast<double>(freq64));
		if (timeToUpdate>0.01f) 
		{
			DEBUG_LOG(("%d Pathfind queue: %d paths, %d cells", TheGameLogic->getFrame(), pathsFound, m_cumulativeCellsAllocated));
			DEBUG_LOG(("Time %f (%f)", timeToUpdate, (Engine::Clock::milliseconds()-startTimeMS)/1000.0f));
			DEBUG_LOG(("\n"));
		}
#endif
#endif
	}
#if defined _DEBUG
	doDebugIcons();
#endif

}

void Pathfinder::checkChangeLayers(PathfindCell *parentCell)
{
		ICoord2D newCellCoord;
		PathfindCell *newCell;
		if (parentCell->getConnectLayer() != LAYER_INVALID) {
			newCellCoord.x = parentCell->getXIndex();
			newCellCoord.y = parentCell->getYIndex();

			if (parentCell->getConnectLayer() == LAYER_GROUND) {
				newCell = getCell(LAYER_GROUND, newCellCoord.x, newCellCoord.y );
			}	else {
				newCell = getCell(parentCell->getConnectLayer(), newCellCoord.x, newCellCoord.y);
			}
			DEBUG_ASSERTCRASH(newCell, ("Couldn't find cell."));
			if (newCell) {
				Bool onList = false;
				if (newCell->hasInfo()) {
					if (newCell->getOpen() || newCell->getClosed())
					{
						// already on one of the lists 
						onList = true;
					}
				}
				if (!onList) {
					if (!newCell->allocateInfo(newCellCoord)) {
						// Out of cells for pathing...
						return;
					}
					// compute cost of path thus far
					// keep track of path we're building - point back to cell we moved here from
					newCell->setParentCell(parentCell) ;
					// store cost of this path
					newCell->setCostSoFar(parentCell->getCostSoFar()); // same as parent cost
					newCell->setTotalCost(parentCell->getTotalCost()) ;
					// insert newCell in open list such that open list is sorted, smallest total path cost first
					m_openList.insert(newCell);

				}
			}
		}
}

struct ExamineCellsStruct
{
	Pathfinder					*thePathfinder;
	const LocomotorSet	*theLoco;
	Bool								centerInCell;
	Bool								isHuman;
	Int									radius;
	const Object				*obj;
	PathfindCell				*goalCell;
};

// The Bresenham walk of Pathfinder::iterateCellsAlongLine with the step as a template argument, so the work per cell
// is inlined: the line toward the goal in examineNeighboringCells runs billions of steps on large maps. Same cells,
// same order, same stop.
template <class Step>
static Int walkCellsAlongLine(Pathfinder &pathfinder, const ICoord2D &start, const ICoord2D &end, PathfindLayerEnum layer,
	Step step)
{
	const Int delta_x = abs(end.x - start.x);
	const Int delta_y = abs(end.y - start.y);
	Int x = start.x;
	Int y = start.y;
	Int xinc1 = (end.x >= start.x) ? 1 : -1;
	Int xinc2 = xinc1;
	Int yinc1 = (end.y >= start.y) ? 1 : -1;
	Int yinc2 = yinc1;
	Int den, num, numadd, numpixels;
	if (delta_x >= delta_y)
	{
		xinc1 = 0;
		yinc2 = 0;
		den = delta_x;
		num = delta_x / 2;
		numadd = delta_y;
		numpixels = delta_x;
	}
	else
	{
		xinc2 = 0;
		yinc1 = 0;
		den = delta_y;
		num = delta_y / 2;
		numadd = delta_x;
		numpixels = delta_y;
	}

	PathfindCell* from = nullptr;
	for (Int curpixel = 0; curpixel <= numpixels; curpixel++)
	{
		PathfindCell* to = pathfinder.getCell( layer, x, y );
		if (to == nullptr) return 0;
		const Int ret = step(from, to, x, y);
		if (ret != 0)
			return ret;

		num += numadd;
		if (num >= den)
		{
			num -= den;
			x += xinc1;
			y += yinc1;
			from = to;
			to = pathfinder.getCell( layer, x, y );
			if (to == nullptr) return 0;
			const Int ret2 = step(from, to, x, y);
			if (ret2 != 0)
				return ret2;
		}
		x += xinc2;
		y += yinc2;

		from = to;
	}
	return 0;
}

/*static*/ Int Pathfinder::examineCellsCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	ExamineCellsStruct* d = static_cast<ExamineCellsStruct*>(userData);
	const Int result = examineCellsStep(from, to, to_x, to_y, d);
	++d->thePathfinder->m_statLineSteps;
	if (result != 0)
		++d->thePathfinder->m_statLineAborts;
	return result;
}

// The checks of a line-of-sight step that end the line; none of them changes anything, and within one search they
// give the same answer for the same cell (validMovementPosition does not use the origin cell).
/*static*/ Bool Pathfinder::lineStepBlocked(PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, ExamineCellsStruct* d)
{
	Bool isCrusher = d->obj ? d->obj->getCrusherLevel() > 0 : false;
	if (!d->thePathfinder->validMovementPosition( isCrusher, d->theLoco->getValidSurfaces(), to, from )) {
		return true;
	}
	if ( (to->getLayer() == LAYER_GROUND) && !d->thePathfinder->m_zoneManager.isPassable(to_x, to_y) ) {
		return true;
	}
	if (to->getPinched()) {
		return true;
	}
	if (d->isHuman) {
		// check if new cell is in logical map.	(computer can move off logical map)
		if (to_x < d->thePathfinder->m_logicalExtent.lo.x) return true;
		if (to_y < d->thePathfinder->m_logicalExtent.lo.y) return true;
		if (to_x > d->thePathfinder->m_logicalExtent.hi.x) return true;
		if (to_y > d->thePathfinder->m_logicalExtent.hi.y) return true;
	}
	TCheckMovementInfo info;
	info.cell.x = to_x;
	info.cell.y = to_y;
	info.layer = from->getLayer();
	info.centerInCell = d->centerInCell;
	info.radius = d->radius;
	info.considerTransient = false;
	info.acceptableSurfaces = d->theLoco->getValidSurfaces();
	if (!d->thePathfinder->checkForMovement(d->obj, info) || info.enemyFixed) {
		return true;
	}
	if (to->getType() == PathfindCell::CELL_CLIFF ) {
		return true;
	}
	if (info.allyFixedCount) {
		return true;
	}
	return false;
}

/*static*/ Int Pathfinder::examineCellsStep(PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, ExamineCellsStruct* d)
{
	if (d->thePathfinder->m_isTunneling) return 1; // abort.
	if (from && to) {
			// The same cells are walked again from many parents; the memo needs the layer of the origin to be the
			// cell's own, since checkForMovement looks around on the origin's layer.
			const UnsignedInt generation = d->thePathfinder->m_lineMemoGeneration;
			const Bool sameLayer = from->getLayer() == to->getLayer();
			Bool blocked = false;
			if (!(sameLayer && to->getLineMemo(generation, blocked))) {
				blocked = lineStepBlocked(from, to, to_x, to_y, d);
				if (sameLayer)
					to->setLineMemo(generation, blocked);
			}
			if (blocked) {
				return 1; // abort.
			}
			Bool onList = false;
			if (to->hasInfo()) {
				if (to->getOpen() || to->getClosed())
				{
					// already on one of the lists 
					onList = true;
				}
			}
			ICoord2D newCellCoord;
			newCellCoord.x = to_x;
			newCellCoord.y = to_y;

			UnsignedInt newCostSoFar = static_cast<UnsignedInt>(from->getCostSoFar( ) + 0.5f*COST_ORTHOGONAL);

			if (!to->allocateInfo(newCellCoord)) {
				// Out of cells for pathing...
 				return 1;
			}								
			if (sameLayer)
				to->setLineMemo(generation, false);
			to->setBlockedByAlly(false);
			// check if this neighbor cell is already on the open (waiting to be tried) 
			// or closed (already tried) lists
			if (onList)
			{
				// already on one of the lists - if existing costSoFar is less, 
				// the new cell is on a longer path, so skip it
				if (to->getCostSoFar() <= newCostSoFar)
					return 0; // keep going.
			}
			const Int costRemaining = to->costToGoal( d->goalCell );
			to->setCostSoFar(newCostSoFar);
			// keep track of path we're building - point back to cell we moved here from
			to->setParentCell(from) ;
			to->setTotalCost(to->getCostSoFar() + costRemaining) ;

			//DEBUG_LOG(("Cell (%d,%d), Parent cost %d, newCostSoFar %d, cost rem %d, tot %d\n", 
			//	to->getXIndex(), to->getYIndex(), 
			//	to->costSoFar(from), newCostSoFar, costRemaining, to->getCostSoFar() + costRemaining));

			// if to was on closed list, remove it from the list
			if (to->getClosed())
				d->thePathfinder->m_closedList = to->removeFromClosedList( d->thePathfinder->m_closedList );

			// if the to was already on the open list, remove it so it can be re-inserted in order
			if (to->getOpen())
				d->thePathfinder->m_openList.remove(to);

			// insert to in open list such that open list is sorted, smallest total path cost first
			d->thePathfinder->m_openList.insert(to);
	}

	return 0;	// keep going
}

Int Pathfinder::examineNeighboringCells(PathfindCell *parentCell, PathfindCell *goalCell, const LocomotorSet& locomotorSet, 
																				 Bool isHuman, Bool centerInCell, Int radius, const ICoord2D &startCellNdx,
																				 const Object *obj, Int attackDistance)
{
		Bool canPathThroughUnits = false;
		if (obj && obj->getAIUpdateInterface()) {
			canPathThroughUnits = obj->getAIUpdateInterface()->canPathThroughUnits();
		}
		Bool isCrusher = obj ? obj->getCrusherLevel() > 0 : false;
		if (attackDistance==NO_ATTACK && !m_isTunneling && !locomotorSet.isDownhillOnly() && goalCell) {
			LineMemoContext context;
			context.obj = obj;
			context.objID = obj ? obj->getID() : INVALID_ID;
			context.radius = radius;
			context.centerInCell = centerInCell;
			context.isHuman = isHuman;
			context.isCrusher = isCrusher;
			context.surfaces = locomotorSet.getValidSurfaces();
			context.ignoreObstacleID = m_ignoreObstacleID;
			context.logicalExtent = m_logicalExtent;
			if (!context.sameAs(m_lineMemoContext)) {
				m_lineMemoContext = context;
				nextLineMemoGeneration();
			}
			ExamineCellsStruct info;
			info.thePathfinder = this;
			info.theLoco = &locomotorSet;
			info.centerInCell = centerInCell;
			info.radius = radius;
			info.obj = obj;
			info.isHuman = isHuman;
			info.goalCell = goalCell;
			ICoord2D start, end;
			start.x = parentCell->getXIndex();
			start.y = parentCell->getYIndex();
			end.x = goalCell->getXIndex();
			end.y = goalCell->getYIndex();
			++m_statLineCalls;
			walkCellsAlongLine(*this, start, end, parentCell->getLayer(),
				[&info](PathfindCell* from, PathfindCell* to, Int x, Int y) { return examineCellsCallback(nullptr, from, to, x, y, &info); });
		}

		Int cellCount = 0;
		// expand search to neighboring orthogonal cells
		static ICoord2D delta[] = 
		{ 
			{ 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 }, 
			{ 1, 1 }, { -1, 1 }, { -1, -1 }, { 1, -1 } 
		};
		const Int numNeighbors = 8;
		const Int firstDiagonal = 4;
		ICoord2D newCellCoord;
		PathfindCell *newCell;
		const Int adjacent[5] = {0, 1, 2, 3, 0};
		Bool neighborFlags[8] = {false, false, false, false, false, false, false};

		UnsignedInt newCostSoFar;



		for( int i=0; i<numNeighbors; i++ )
		{
			neighborFlags[i] = false;
			// determine neighbor cell to try
			newCellCoord.x = parentCell->getXIndex() + delta[i].x;
			newCellCoord.y = parentCell->getYIndex() + delta[i].y;

			// get the neighboring cell
			newCell = getCell(parentCell->getLayer(), newCellCoord.x, newCellCoord.y );

			// check if cell is on the map
			if (newCell == nullptr)
				continue;

			Bool notZonePassable = false;
			if ((newCell->getLayer()==LAYER_GROUND) && !m_zoneManager.isPassable(newCellCoord.x, newCellCoord.y)) {
				notZonePassable = true;
			}
			if (isHuman) {
				// check if new cell is in logical map.	(computer can move off logical map)
				if (newCellCoord.x < m_logicalExtent.lo.x) continue;
				if (newCellCoord.y < m_logicalExtent.lo.y) continue; 
				if (newCellCoord.x > m_logicalExtent.hi.x) continue; 
				if (newCellCoord.y > m_logicalExtent.hi.y) continue; 
			}

			// check if this neighbor cell is already on the open (waiting to be tried) 
			// or closed (already tried) lists
			Bool onList = false;
			if (newCell->hasInfo()) {
				if (newCell->getOpen() || newCell->getClosed())
				{
					// already on one of the lists 
					onList = true;
				}
			}
			if (onList) {
				// we have already examined this one, so continue.
				continue;
			}
			if (i>=firstDiagonal) {
				// make sure one of the adjacent sides is open.
				if (!neighborFlags[adjacent[i-4]] && !neighborFlags[adjacent[i-3]]) {
					continue;
				}
			}

			// do the gravity check here
			if ( locomotorSet.isDownhillOnly() )
			{
				Coord3D fromPos;
				fromPos.x = parentCell->getXIndex() * PATHFIND_CELL_SIZE_F ;
				fromPos.y = parentCell->getYIndex() * PATHFIND_CELL_SIZE_F ;
				fromPos.z = TheTerrainLogic->getGroundHeight(fromPos.x , fromPos.y);

				Coord3D toPos;
				toPos.x = newCellCoord.x * PATHFIND_CELL_SIZE_F ;
				toPos.y = newCellCoord.y * PATHFIND_CELL_SIZE_F ;
				toPos.z = TheTerrainLogic->getGroundHeight(toPos.x , toPos.y);

				if ( fromPos.z < toPos.z )
					continue;
			}



			Bool movementValid = true;
			Bool dozerHack = false;
			if (validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), newCell, parentCell )) {
			}	else {
				movementValid = false;
				if (obj && obj->isKindOf(KINDOF_DOZER)) {
					if (newCell->getType()==PathfindCell::CELL_OBSTACLE) {
						Object *obstacle = TheGameLogic->findObjectByID(newCell->getObstacleID());
						if (obstacle && !(obj->getRelationship(obstacle)==ENEMIES)) {
							movementValid = true;
							dozerHack = true;
						}
					}
				}
				if (!movementValid && !m_isTunneling) {
					continue;
				}
			}	
			if (!dozerHack) 
				neighborFlags[i] = true;

			TCheckMovementInfo info;
			info.cell = newCellCoord;
			info.layer = parentCell->getLayer();
			info.centerInCell = centerInCell;
			info.radius = radius;
			info.considerTransient = false;
			info.acceptableSurfaces = locomotorSet.getValidSurfaces();
			Int dx = newCellCoord.x-startCellNdx.x;
			Int dy = newCellCoord.y-startCellNdx.y;
			if (dx<0) dx = -dx;
			if (dy<0) dy = -dy;
			if (dx>1+radius) info.considerTransient = false;
			if (dy>1+radius) info.considerTransient = false;
			if (!checkForMovement(obj, info) || info.enemyFixed) {
				if (!m_isTunneling) {
					continue;
				}
				movementValid = false;
			}	
			if (movementValid && !newCell->getPinched()) {
				//Note to self - only turn off tunneling after check for movement.jba. 
				m_isTunneling = false;
			}
			if (!newCell->hasInfo()) {
				if (!newCell->allocateInfo(newCellCoord)) {
					// Out of cells for pathing...
 					return cellCount;
				}								
				cellCount++;
			}

			newCostSoFar = newCell->costSoFar( parentCell );
			if (info.allyMoving && dx<10 && dy<10) {
				newCostSoFar += 3*COST_DIAGONAL;
			}
			if (newCell->getType() == PathfindCell::CELL_CLIFF && !newCell->getPinched() ) {
				Coord3D fromPos;
				fromPos.x = parentCell->getXIndex() * PATHFIND_CELL_SIZE_F ;
				fromPos.y = parentCell->getYIndex() * PATHFIND_CELL_SIZE_F ;
				fromPos.z = TheTerrainLogic->getGroundHeight(fromPos.x , fromPos.y);

				Coord3D toPos;
				toPos.x = newCellCoord.x * PATHFIND_CELL_SIZE_F ;
				toPos.y = newCellCoord.y * PATHFIND_CELL_SIZE_F ;
				toPos.z = TheTerrainLogic->getGroundHeight(toPos.x , toPos.y);

				if ( fabs(fromPos.z - toPos.z)<PATHFIND_CELL_SIZE_F) {
					newCostSoFar += 7*COST_DIAGONAL;
				}
			} else if (newCell->getPinched()) {
				newCostSoFar += COST_ORTHOGONAL;
			}
			newCell->setBlockedByAlly(false);
			if (info.allyFixedCount>0) {
				Int costFactor = 3*COST_DIAGONAL;
				if (attackDistance != NO_ATTACK) {
					costFactor = 3*COST_DIAGONAL;
				}
				if (canPathThroughUnits) {
					newCostSoFar += costFactor;
				}	else {
					newCell->setBlockedByAlly(true);
					newCostSoFar += costFactor;
				}
			} 
			Int costRemaining = 0;
			if (goalCell) {
				if (attackDistance == NO_ATTACK)  {
					costRemaining = newCell->costToGoal( goalCell );
				}	else {
					dx = newCellCoord.x - goalCell->getXIndex();
					dy = newCellCoord.y - goalCell->getYIndex();
					costRemaining = static_cast<Int>(COST_ORTHOGONAL*sqrt(dx*dx + dy*dy));
					costRemaining -= attackDistance/2;
					if (costRemaining<0) costRemaining=0;
					if (info.allyGoal) {
						if (obj && obj->isKindOf(KINDOF_VEHICLE)) {
							newCostSoFar += 3*COST_ORTHOGONAL; 
						}	else {
							// Infantry can pass through infantry.
							newCostSoFar += COST_ORTHOGONAL;
						}
					}
				}
			}
			if (notZonePassable) {
				newCostSoFar += 100*COST_ORTHOGONAL;
			}
			if (newCell->getType()==PathfindCell::CELL_OBSTACLE) {
				newCostSoFar += 100*COST_ORTHOGONAL;
			}
			// check if this neighbor cell is already on the open (waiting to be tried) 
			// or closed (already tried) lists
			if (onList)
			{
				// already on one of the lists - if existing costSoFar is less, 
				// the new cell is on a longer path, so skip it
				if (newCell->getCostSoFar() <= newCostSoFar)
					continue;
			}
			if (m_isTunneling) {
				if (!validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), newCell, parentCell )) {
					newCostSoFar += 10*COST_ORTHOGONAL;
				}
			}
			newCell->setCostSoFar(newCostSoFar);
			// keep track of path we're building - point back to cell we moved here from
			newCell->setParentCell(parentCell) ;
			if (m_isTunneling) {
				costRemaining = 0; // find the closest valid cell.
			}
			newCell->setTotalCost(newCell->getCostSoFar() + costRemaining) ;

			//DEBUG_LOG(("Cell (%d,%d), Parent cost %d, newCostSoFar %d, cost rem %d, tot %d\n", 
			//	newCell->getXIndex(), newCell->getYIndex(), 
			//	newCell->costSoFar(parentCell), newCostSoFar, costRemaining, newCell->getCostSoFar() + costRemaining));

			// if newCell was on closed list, remove it from the list
			if (newCell->getClosed())
				m_closedList = newCell->removeFromClosedList( m_closedList );

			// if the newCell was already on the open list, remove it so it can be re-inserted in order
			if (newCell->getOpen())
				m_openList.remove(newCell);

			// insert newCell in open list such that open list is sorted, smallest total path cost first
			m_openList.insert(newCell);
		}
	return cellCount;
}

/**
 * Find a short, valid path between given locations.
 * Uses A* algorithm.
 */
Path *Pathfinder::findPath( Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from, 
													 const Coord3D *rawTo)
{
	const SearchStatScope statScope( *this, SEARCH_PATH );
	if (!clientSafeQuickDoesPathExist(locomotorSet, from, rawTo)) {
		return nullptr;
	}
	Bool isHuman = true;
	if (obj && obj->getControllingPlayer() && (obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER)) {
		isHuman = false; // computer gets to cheat.
	}

	m_zoneManager.clearPassableFlags();
	Path *hPat = findHierarchicalPath(isHuman, locomotorSet, from, rawTo, false);
	if (hPat) {
		deleteInstanceOf(hPat);
	}	else {
		m_zoneManager.setAllPassable();
	}

	Path *pat = internalFindPath(obj, locomotorSet, from, rawTo);
	if (pat!=nullptr) {
		return pat;
	}

/* hierarchical build path code.
	if (pat) {

		Path *path = newInstance(Path);
		PathNode *prior = NULL;
		for (PathNode *node = pat->getLastNode(); node; node=node->getPrevious()) {
			Bool unobstructed = true;
			if (prior!=NULL) {
				unobstructed = isLinePassable( obj, locomotorSet.getValidSurfaces(), 
				prior->getLayer(), *prior->getPosition(), *node->getPosition(), false, true);
			}
			if (unobstructed) {
				path->prependNode( node->getPosition(), node->getLayer() );
				path->getFirstNode()->setCanOptimize(node->getCanOptimize());	
				path->getFirstNode()->setNextOptimized(path->getFirstNode()->getNext());
			} else {
				Path *linkPath = findClosestPath(obj, locomotorSet, node->getPosition(), 
					prior->getPosition(), false, 0, true);
				if (linkPath==NULL) {
					DEBUG_LOG(("Couldn't find path - unexpected. jba.\n"));
					continue;
				}
				PathNode *linkNode = linkPath->getLastNode();
				if (linkNode==NULL) {
					DEBUG_LOG(("Empty path - unexpected. jba.\n"));
					continue;
				}
				for (linkNode=linkNode->getPrevious(); linkNode; linkNode=linkNode->getPrevious()) {
					path->prependNode( linkNode->getPosition(), linkNode->getLayer() );
					path->getFirstNode()->setCanOptimize(linkNode->getCanOptimize());
					path->getFirstNode()->setNextOptimized(path->getFirstNode()->getNext());
				}
				deleteInstanceOf(linkPath);
			}
			prior = node;
		}
		deleteInstanceOf(pat);
		path->optimize(obj, locomotorSet.getValidSurfaces(), false);
		if (TheGlobalData->m_debugAI) {
			setDebugPath(path);
		}
		return path;
	}
*/
	return nullptr; 
}

/**
 * Find a short, valid path between given locations.
 * Uses A* algorithm.
 */
Path *Pathfinder::internalFindPath( Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from, 
													 const Coord3D *rawTo)
{
	m_statExhaustedAtSearchStart = s_statInfoExhausted;
	m_statFreeAtSearchStart = s_statInfoFree;
	if (s_statInfoFree < m_statFreeMin)
		m_statFreeMin = s_statInfoFree;
	m_statFreeSum += s_statInfoFree;
	++m_statFreeSamples;
	//CRCDEBUG_LOG(("Pathfinder::findPath()\n"));
#ifdef INTENSE_DEBUG
	DEBUG_LOG(("internal find path...\n"));
#endif

#if defined _DEBUG
	Int startTimeMS = Engine::Clock::milliseconds();
#endif
	Bool centerInCell = true;
	Int radius = 0;
	if (obj) {
		getRadiusAndCenter(obj, radius, centerInCell);
	}
	
	Bool isHuman = true;
	if (obj && obj->getControllingPlayer() && (obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER)) {
		isHuman = false; // computer gets to cheat.
	}

	if (rawTo->x == 0.0f && rawTo->y == 0.0f) {
		DEBUG_LOG(("Attempting pathfind to 0,0, generally a bug.\n"));
		return nullptr;
	}
	DEBUG_ASSERTCRASH(m_openList.isEmpty() && m_closedList == nullptr, ("Dangling lists."));
	if (m_isMapReady == false) {
		return nullptr;
	}

	Coord3D adjustTo = *rawTo;
	Coord3D *to = &adjustTo;
	Coord3D clipFrom = *from;
	clip(&clipFrom, &adjustTo);

	if (!centerInCell) {
		adjustTo.x += PATHFIND_CELL_SIZE_F/2;
		adjustTo.y += PATHFIND_CELL_SIZE_F/2;
	}

	m_isTunneling = false;

	PathfindLayerEnum destinationLayer = TheTerrainLogic->getLayerForDestination(to);
	// determine goal cell
	PathfindCell *goalCell = getCell( destinationLayer, to );
	if (goalCell == nullptr) {
		return nullptr;
	}
	
	ICoord2D cell;
	worldToCell( to, &cell );

	if (!checkDestination(obj, cell.x, cell.y, destinationLayer, radius, centerInCell)) {
		return nullptr;
	}
	// determine start cell
	ICoord2D startCellNdx;
	worldToCell(&clipFrom, &startCellNdx);
	PathfindLayerEnum layer = LAYER_GROUND;
	if (obj) {
		layer = obj->getLayer();
	}
	PathfindCell *parentCell = getClippedCell( layer,&clipFrom );
	if (parentCell == nullptr) {
		return nullptr;
	}

	ICoord2D pos2d;
	worldToCell(to, &pos2d);
	if (!goalCell->allocateInfo(pos2d)) {
		return nullptr;
	}
	if (parentCell!=goalCell) {
		worldToCell(&clipFrom, &pos2d);
		if (!parentCell->allocateInfo(pos2d)) {
			goalCell->releaseInfo();
			return nullptr;
		}
	}
	//
	// Determine if this pathfind is "tunneling" or not.
	// A tunneling pathfind starts from within an obstacle, and is allowed
	// to ignore obstacle cells until it reaches a cell that is no longer
	// classified as an obstacle.  At that point, the pathfind behaves normally.
	//
	if (parentCell->getType() == PathfindCell::CELL_OBSTACLE)	{
		m_isTunneling = true;
	}
	else {
		m_isTunneling = false;
	}

	Int zone1, zone2;
	Bool isCrusher = obj ? obj->getCrusherLevel() > 0 : false;
	zone1 = m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), isCrusher, parentCell->getZone()); 
	zone2 =  m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), isCrusher, goalCell->getZone());

	if (layer==LAYER_WALL && zone1 == 0) {
		return nullptr;
	}

	if (destinationLayer==LAYER_WALL && zone2 == 0) {
		return nullptr;
	}

	if (goalCell->isObstaclePresent(m_ignoreObstacleID) || m_isTunneling) {
		// Use terrain zones instead of building zones, since we are moving into or out of a building.
		zone2 = m_zoneManager.getEffectiveTerrainZone(static_cast<zoneStorageType>(zone2));
		zone2 =  m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), isCrusher, static_cast<zoneStorageType>(zone2));
		zone1 = m_zoneManager.getEffectiveTerrainZone(static_cast<zoneStorageType>(zone1));
		zone1 = m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), isCrusher, static_cast<zoneStorageType>(zone1)); 
	}																																 

	//DEBUG_LOG(("Zones %d to %d\n", zone1, zone2));

	if ( zone1 != zone2) {
		//DEBUG_LOG(("Intense Debug Info - Pathfind Zone screen failed-cannot reach desired location.\n"));
		goalCell->releaseInfo();
		parentCell->releaseInfo();
		return nullptr;
	}

	// sanity check - if destination is invalid, can't path there
	if (validMovementPosition( isCrusher, destinationLayer, locomotorSet, to ) == false)	{
		m_isTunneling = false;
		goalCell->releaseInfo();
		parentCell->releaseInfo();
		return nullptr;
	}

	// sanity check - if source is invalid, we have to cheat
	if (validMovementPosition( isCrusher, layer, locomotorSet, from ) == false)	{
		// somehow we got to an impassable location.
		m_isTunneling = true;
	}

	parentCell->startPathfind(goalCell);

	// initialize "open" list to contain start cell
	m_openList.initWithStartCell(parentCell);

	// "closed" list is initially empty
	m_closedList = nullptr;

	Int cellCount = 0;

	//
	// Continue search until "open" list is empty, or
	// until goal is found.
	//
	while( !m_openList.isEmpty() )
	{
		// take head cell off of open list - it has lowest estimated total path cost
		parentCell = m_openList.getHead();
		m_openList.remove(parentCell);

		if (parentCell == goalCell)
		{
			// success - found a path to the goal
			Bool show = TheGlobalData->m_debugAI==AI_DEBUG_PATHS;
#ifdef INTENSE_DEBUG
			DEBUG_LOG(("internal find path SUCCESS...\n"));
			Int count = 0;
			if (cellCount>1000 && obj) {
				show = true;
				DEBUG_LOG(("cells %d obj %s %x from (%f,%f) to(%f, %f)\n", count, obj->getTemplate()->getName().str(), obj, from->x, from->y, to->x, to->y));
#ifdef STATE_MACHINE_DEBUG
				if( obj->getAIUpdateInterface() )
				{
					DEBUG_LOG(("State %s\n",  obj->getAIUpdateInterface()->getCurrentStateName().str()));
				}
#endif
				TheScriptEngine->AppendDebugMessage("Big path", false);
			}
#endif
			if (show)
				debugShowSearch(true);

			m_isTunneling = false;
			// construct and return path
			Path *path =  buildActualPath( obj, locomotorSet.getValidSurfaces(), from, goalCell, centerInCell, false );
			parentCell->releaseInfo();
			cleanOpenAndClosedLists();
			return path;
		}	

		// put parent cell onto closed list - its evaluation is finished
		m_closedList = parentCell->putOnClosedList( m_closedList );

		// Check to see if we can change layers in this cell.
		checkChangeLayers(parentCell);

		cellCount += examineNeighboringCells(parentCell, goalCell, locomotorSet, isHuman, centerInCell, radius, startCellNdx, obj, NO_ATTACK);

	}

	// failure - goal cannot be reached
#if defined _DEBUG
#ifdef INTENSE_DEBUG
	DEBUG_LOG(("internal find path FAILURE...\n"));
#endif
	if (TheGlobalData->m_debugAI == AI_DEBUG_PATHS)
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
 		RGBColor color;
		color.blue = 0;
		color.red = color.green = 1;
		addIcon(nullptr, 0, 0, color);
		debugShowSearch(false);
		Coord3D pos;
		pos = *from;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y ) + 0.5f;
		addIcon(&pos, 3*PATHFIND_CELL_SIZE_F, 600, color);
		pos = *to;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y ) + 0.5f;
		addIcon(&pos, 3*PATHFIND_CELL_SIZE_F, 600, color);	
		Real dx, dy;
		dx = from->x - to->x;
		dy = from->y - to->y;

		Int count = static_cast<Int>(sqrt(dx*dx+dy*dy)/(PATHFIND_CELL_SIZE_F/2));
		if (count<2) count = 2;
		Int i;
		color.green = 0;
		for (i=1; i<count; i++) {
			pos.x = from->x + (to->x-from->x)*i/count;
			pos.y = from->y + (to->y-from->y)*i/count;
			pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y ) + 0.5f;
			addIcon(&pos, PATHFIND_CELL_SIZE_F/2, 60, color);

		}
	}	

	if (obj) {
		Bool valid;
		valid = validMovementPosition( isCrusher, obj->getLayer(), locomotorSet, to ) ;

		DEBUG_LOG(("%d ", TheGameLogic->getFrame()));
		DEBUG_LOG(("Pathfind failed from (%f,%f) to (%f,%f), OV %d\n", from->x, from->y, to->x, to->y, valid));
		DEBUG_LOG(("Unit '%s', time %f, cells %d\n", obj->getTemplate()->getName().str(), (Engine::Clock::milliseconds()-startTimeMS)/1000.0f,cellCount));
#ifdef DUMP_PERF_STATS
		TheGameLogic->incrementOverallFailedPathfinds();
#endif
#ifdef STATE_MACHINE_DEBUG
		if( obj->getAIUpdateInterface() )
		{
			DEBUG_LOG(("state %s\n", obj->getAIUpdateInterface()->getCurrentStateName().str()));
		}
#endif
	}
#endif
	m_isTunneling = false;
	noteFailedPath(obj, goalCell);
	goalCell->releaseInfo();
	cleanOpenAndClosedLists();
	return nullptr;
}

/**
 * Checks to see if there is enough path width at this cell for ground
 * movement.  Returns the width available.
 */
Int Pathfinder::clearCellForDiameter(Bool crusher, Int cellX, Int cellY, PathfindLayerEnum layer, Int pathDiameter)	  
{
	Int radius = pathDiameter/2;
	Int numCellsAbove = radius;
	if (radius==0) numCellsAbove++;
	Int i, j;
	Bool clear = true;
	Bool cutCorners = false;
	if (radius>1) {
		cutCorners = true;
		// We remove the outside corner cells from the check.
	}
	for (i=cellX-radius; i<cellX+numCellsAbove; i++) {
		Bool xMinOrMax = (i==cellX-radius) || (i==cellX+numCellsAbove-1);
		for (j=cellY-radius; j<cellY+numCellsAbove; j++) {
			Bool yMinOrMax = (j==cellY-radius) || (j==cellY+numCellsAbove-1);
			if (xMinOrMax && yMinOrMax && cutCorners) {
				continue; // this is an outside corner cell, and we are cutting corners. jba. :)
			}
			PathfindCell	*cell = getCell(layer, i, j);
			if (cell) {
				if (cell->getType() != PathfindCell::CELL_CLEAR) {
					if (cell->getType() == PathfindCell::CELL_OBSTACLE) {
						if (cell->isObstacleFence()) {
							if (!crusher) {
								clear = false;
							}
						} else {
							clear = false;
						}
					} else {
						clear = false;
					}
				}
				if (cell->getFlags() == PathfindCell::UNIT_PRESENT_FIXED && pathDiameter>=2) {
					Object *obj = TheGameLogic->findObjectByID(cell->getPosUnit());
					if (obj) {
						if (crusher) {
							if (obj->getCrushableLevel()>1) {
								clear = false;
							}
						} else {
							if (obj->getCrushableLevel()>0) {
								clear = false;
							}
						}
					}
				}
			} else {
				return false; // off the map.
			}
			if (!clear) break;
		}
	}
	if (clear) {
		if (radius==0) return 1;
		return 2*radius;
	}
	if (pathDiameter < 2) return 0;
	return clearCellForDiameter(crusher, cellX, cellY, layer, pathDiameter-2);
}

/**
 * Work backwards from goal cell to construct final path.
 */
Path *Pathfinder::buildGroundPath(Bool isCrusher, const Coord3D *fromPos, PathfindCell *goalCell, Bool center, Int pathDiameter )
{
	DEBUG_ASSERTCRASH( goalCell, ("Pathfinder::buildActualPath: goalCell == NULL") );

	Path *path = newInstance(Path);

	prependCells(path, fromPos, goalCell, center);

	// cleanup the path by checking line of sight
	path->optimizeGroundPath( isCrusher, pathDiameter );


#if defined _DEBUG
	if (TheGlobalData->m_debugAI==AI_DEBUG_GROUND_PATHS)
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
 		RGBColor color;
		color.blue = 0;
		color.red = color.green = 1;
		Coord3D pos;
		PathNode *node;
		for( node = path->getFirstNode(); node; node = node->getNext() )
		{

			// create objects to show path - they decay

			pos = *node->getPosition();
			color.red = color.green = 1;
			if (node->getLayer() != LAYER_GROUND) {
				color.red = 0;
			}
			addIcon(&pos, PATHFIND_CELL_SIZE_F*.25f, 200, color);
		}

		// show optimized path
		for( node = path->getFirstNode(); node; node = node->getNextOptimized() )
		{
			pos = *node->getPosition();
			addIcon(&pos, PATHFIND_CELL_SIZE_F*.8f, 200, color);
		}
		setDebugPath(path);
	}
#endif
	return path;
}			 

/**
 * Work backwards from goal cell to construct final path.
 */
Path *Pathfinder::buildHierachicalPath( const Coord3D *fromPos, PathfindCell *goalCell )
{
	DEBUG_ASSERTCRASH( goalCell, ("Pathfinder::buildHierachicalPath: goalCell == NULL") );

	Path *path = newInstance(Path);

	prependCells(path, fromPos, goalCell, true);

	// Expand the hierarchical path around the starting point. jba [8/24/2003]
	// This allows the unit to get around friendly units that may be near it.
	Coord3D pos = *path->getFirstNode()->getPosition();
	Coord3D minPos = pos;
	minPos.x -= static_cast<Real>(PathfindZoneManager::ZONE_BLOCK_SIZE)*PATHFIND_CELL_SIZE_F;
	minPos.y -= static_cast<Real>(PathfindZoneManager::ZONE_BLOCK_SIZE)*PATHFIND_CELL_SIZE_F;
	Coord3D maxPos = pos;
	maxPos.x += static_cast<Real>(PathfindZoneManager::ZONE_BLOCK_SIZE)*PATHFIND_CELL_SIZE_F;
	maxPos.y += static_cast<Real>(PathfindZoneManager::ZONE_BLOCK_SIZE)*PATHFIND_CELL_SIZE_F;
	ICoord2D cellNdxMin, cellNdxMax;
	worldToCell(&minPos, &cellNdxMin);
	worldToCell(&maxPos, &cellNdxMax);
	Int i, j;
	for (i=cellNdxMin.x; i<=cellNdxMax.x; i++) {
		for (j=cellNdxMin.y; j<=cellNdxMax.y; j++) {
			m_zoneManager.setPassable(i, j, true);
		}
	}

#if defined _DEBUG
	if (TheGlobalData->m_debugAI==AI_DEBUG_PATHS)
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
 		RGBColor color;
		color.blue = 0;
		color.red = color.green = 1;
		Coord3D pos2;
		Int i2;
		for (i2=0; i2<3; i2++)
		for( PathNode *node = path->getFirstNode(); node; node = node->getNext() )
		{

			// create objects to show path - they decay

			pos2 = *node->getPosition();
			color.red = 1;
			color.green = 0.4f;
			if (node->getLayer() != LAYER_GROUND) {
				color.red = 0;
			}
			addIcon(&pos2, PATHFIND_CELL_SIZE_F, 200, color);
		}
		setDebugPath(path);
	}
#endif
	return path;
}			 

struct MADStruct
{
	Pathfinder					*thePathfinder;
	Object							*obj;
	ObjectID						ignoreID;
};

/*static*/ Int Pathfinder::moveAlliesDestinationCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	MADStruct* d = static_cast<MADStruct*>(userData);
	if (to) {
		if (to->getPosUnit()==INVALID_ID) {
			return 0;
		}
		if (to->getPosUnit()==d->obj->getID()) { 
			return 0;	// It's us.
		}
		if (to->getPosUnit()==d->ignoreID) { 
			return 0;	 // It's the one we are ignoring.
		}
		Object *otherObj = TheGameLogic->findObjectByID(to->getPosUnit());
		if (otherObj==nullptr) return 0;
		if (d->obj->getRelationship(otherObj)!=ALLIES) {
			return 0;  // Only move allies.
		}
		if( otherObj && otherObj->getAI() && !otherObj->getAI()->isMoving() ) 
		{
			//Kris: Patch 1.01 November 3, 2003
			//Black Lotus exploit fix -- moving while hacking.
			if( otherObj->testStatus( OBJECT_STATUS_IS_USING_ABILITY ) || otherObj->getAI()->isBusy() )
			{
				return 0; // Packing or unpacking objects for example 
			}
			//DEBUG_LOG(("Moving ally\n"));
			otherObj->getAI()->aiMoveAwayFromUnit(d->obj, CMD_FROM_AI);
		}
	}

	return 0;	// keep going
}

void Pathfinder::moveAlliesAwayFromDestination(Object *obj,const Coord3D& destination)
{
	MADStruct info;
	info.obj = obj;
	info.ignoreID = obj->getAI()->getIgnoredObstacleID();
	info.thePathfinder = this;
	PathfindLayerEnum layer = obj->getLayer();
	if (layer==LAYER_GROUND) {
		layer = TheTerrainLogic->getLayerForDestination(&destination);
	}
	iterateCellsAlongLine(*obj->getPosition(), destination, layer, moveAlliesDestinationCallback, &info);

}

struct GroundCellsStruct
{
	Pathfinder					*thePathfinder;
	Bool								centerInCell;
	Int									pathDiameter;
	PathfindCell				*goalCell;
	Bool								crusher;
};

/*static*/ Int Pathfinder::groundCellsCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	GroundCellsStruct* d = static_cast<GroundCellsStruct*>(userData);
	if (from && to) {
			if (to->hasInfo()) {
				if (to->getOpen() || to->getClosed())
				{
					// already on one of the lists 
					return 1; // abort.
				}
			}
			// See how wide the cell is.
			Int clearDiameter = d->thePathfinder->clearCellForDiameter(d->crusher, to_x, to_y, to->getLayer(), d->pathDiameter); 
			if (clearDiameter != d->pathDiameter) {
				return 1;
			}
			ICoord2D newCellCoord;
			newCellCoord.x = to_x;
			newCellCoord.y = to_y;
			if (!to->allocateInfo(newCellCoord)) {
				// Out of cells for pathing...
 				return 1;
			}								

			UnsignedInt newCostSoFar = static_cast<UnsignedInt>(from->getCostSoFar( ) + 0.5f*COST_ORTHOGONAL);
			to->setBlockedByAlly(false);

			Int costRemaining = 0;
			costRemaining = to->costToGoal( d->goalCell );
			to->setCostSoFar(newCostSoFar);
			// keep track of path we're building - point back to cell we moved here from
			to->setParentCell(from) ;
			to->setTotalCost(to->getCostSoFar() + costRemaining) ;

			// insert to in open list such that open list is sorted, smallest total path cost first
			d->thePathfinder->m_openList.insert(to);
	}

	return 0;	// keep going
}

/**
 * Find a short, valid path between given locations.
 * Uses A* algorithm.
 */
Path *Pathfinder::findGroundPath( const Coord3D *from, 
													 const Coord3D *rawTo, Int pathDiameter, Bool crusher)
{
	const SearchStatScope statScope( *this, SEARCH_GROUND );
	//CRCDEBUG_LOG(("Pathfinder::findGroundPath()\n"));
#if defined _DEBUG
	Int startTimeMS = Engine::Clock::milliseconds();
#endif
#ifdef INTENSE_DEBUG
	DEBUG_LOG(("Find ground path..."));
#endif	
	Bool centerInCell = false;
	
	m_zoneManager.clearPassableFlags();
	Bool isHuman = true;

	Path *hPat = internal_findHierarchicalPath(isHuman, LOCOMOTORSURFACE_GROUND, from, rawTo, false, false);
	if (hPat) {
		deleteInstanceOf(hPat);
	}	else {
		m_zoneManager.setAllPassable();
	}

	if (rawTo->x == 0.0f && rawTo->y == 0.0f) {
		DEBUG_LOG(("Attempting pathfind to 0,0, generally a bug.\n"));
		return nullptr;
	}
	DEBUG_ASSERTCRASH(m_openList.isEmpty() && m_closedList == nullptr, ("Dangling lists."));
	if (m_isMapReady == false) {
		return nullptr;
	}

	Coord3D adjustTo = *rawTo;
	Coord3D *to = &adjustTo;
	Coord3D clipFrom = *from;
	clip(&clipFrom, &adjustTo);

	m_isTunneling = false;

	PathfindLayerEnum destinationLayer = TheTerrainLogic->getLayerForDestination(to);
	
	ICoord2D cell;
	worldToCell( to, &cell );

	if (pathDiameter!=clearCellForDiameter(crusher, cell.x, cell.y, destinationLayer, pathDiameter)) {
		Int offset=1;
		ICoord2D newCell;
		const Int MAX_OFFSET = 8;
		while (offset<MAX_OFFSET) {
			newCell = cell;
			cell.x += offset;
			if (clearCellForDiameter(crusher, cell.x, cell.y, destinationLayer, pathDiameter)==pathDiameter) break;
			cell.y += offset;
			if (clearCellForDiameter(crusher, cell.x, cell.y, destinationLayer, pathDiameter)==pathDiameter) break;
			cell.x -= offset;
			if (clearCellForDiameter(crusher, cell.x, cell.y, destinationLayer, pathDiameter)==pathDiameter) break;
			cell.x -= offset;
			if (clearCellForDiameter(crusher, cell.x, cell.y, destinationLayer, pathDiameter)==pathDiameter) break;
			cell.y -= offset;
			if (clearCellForDiameter(crusher, cell.x, cell.y, destinationLayer, pathDiameter)==pathDiameter) break;
			cell.y -= offset;
			if (clearCellForDiameter(crusher, cell.x, cell.y, destinationLayer, pathDiameter)==pathDiameter) break;
			cell.x += offset;
			if (clearCellForDiameter(crusher, cell.x, cell.y, destinationLayer, pathDiameter)==pathDiameter) break;
			cell.x += offset;
			if (clearCellForDiameter(crusher, cell.x, cell.y, destinationLayer, pathDiameter)==pathDiameter) break;
			offset++;
			cell = newCell;
		}
		if (offset >= MAX_OFFSET) {
			return nullptr;
		}
	}

	// determine goal cell
	PathfindCell *goalCell = getCell( destinationLayer, cell.x, cell.y );
	if (goalCell == nullptr) {
		return nullptr;
	}
	if (!goalCell->allocateInfo(cell)) {
		return nullptr;
	}

	// determine start cell
	ICoord2D startCellNdx;
	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(from);
	PathfindCell *parentCell = getClippedCell( layer,&clipFrom );
	if (parentCell == nullptr) {
		return nullptr;
	}
	if (parentCell!=goalCell) {
		worldToCell(&clipFrom, &startCellNdx);
		if (!parentCell->allocateInfo(startCellNdx)) {
			goalCell->releaseInfo();
			return nullptr;
		}
	}


	Int zone1, zone2;
	// m_isCrusher = false;
	zone1 = m_zoneManager.getEffectiveZone(LOCOMOTORSURFACE_GROUND, false, parentCell->getZone()); 
	zone2 =  m_zoneManager.getEffectiveZone(LOCOMOTORSURFACE_GROUND, false, goalCell->getZone());

	//DEBUG_LOG(("Zones %d to %d\n", zone1, zone2));

	if ( zone1 != zone2) {
		goalCell->releaseInfo();
		parentCell->releaseInfo();
		return nullptr;
	}
	parentCell->startPathfind(goalCell);

	// initialize "open" list to contain start cell
	m_openList.initWithStartCell(parentCell);

	// "closed" list is initially empty
	m_closedList = nullptr;

	//
	// Continue search until "open" list is empty, or
	// until goal is found.
	//
	Int cellCount = 0;
	while( !m_openList.isEmpty() )
	{
		// take head cell off of open list - it has lowest estimated total path cost
		parentCell = m_openList.getHead();
		m_openList.remove(parentCell);

		if (parentCell == goalCell)
		{
			// success - found a path to the goal	 
#ifdef INTENSE_DEBUG
	DEBUG_LOG((" time %d msec %d cells", (Engine::Clock::milliseconds()-startTimeMS), cellCount));
	DEBUG_LOG((" SUCCESS\n"));
#endif	
#if defined _DEBUG
			Bool show = TheGlobalData->m_debugAI==AI_DEBUG_GROUND_PATHS;
			if (show)
				debugShowSearch(true);
#endif
			m_isTunneling = false;
			// construct and return path
			Path *path =  buildGroundPath(crusher, from, goalCell, centerInCell, pathDiameter );
			parentCell->releaseInfo();
			cleanOpenAndClosedLists();
			return path;
		}	

		// put parent cell onto closed list - its evaluation is finished
		m_closedList = parentCell->putOnClosedList( m_closedList );

		// Check to see if we can change layers in this cell.
		checkChangeLayers(parentCell);

		GroundCellsStruct info;
		info.thePathfinder = this;
		info.centerInCell = centerInCell;
		info.pathDiameter = pathDiameter;
		info.goalCell = goalCell;
		info.crusher = crusher;
		ICoord2D start, end;
		start.x = parentCell->getXIndex();
		start.y = parentCell->getYIndex();
		end.x = goalCell->getXIndex();
		end.y = goalCell->getYIndex();
		iterateCellsAlongLine(start, end, parentCell->getLayer(), groundCellsCallback, &info);

		// expand search to neighboring orthogonal cells
		static ICoord2D delta[] = 
		{ 
			{ 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 }, 
			{ 1, 1 }, { -1, 1 }, { -1, -1 }, { 1, -1 } 
		};
		const Int numNeighbors = 8;
		const Int firstDiagonal = 4;
		ICoord2D newCellCoord;
		PathfindCell *newCell;
		const Int adjacent[5] = {0, 1, 2, 3, 0};
		Bool neighborFlags[8] = {false, false, false, false, false, false, false};

		UnsignedInt newCostSoFar;

		for( int i=0; i<numNeighbors; i++ )
		{
			neighborFlags[i] = false;
			// determine neighbor cell to try
			newCellCoord.x = parentCell->getXIndex() + delta[i].x;
			newCellCoord.y = parentCell->getYIndex() + delta[i].y;

			// get the neighboring cell
			newCell = getCell(parentCell->getLayer(), newCellCoord.x, newCellCoord.y );

			// check if cell is on the map
			if (newCell == nullptr)
				continue;

			if ((newCell->getLayer()==LAYER_GROUND) && !m_zoneManager.isPassable(newCellCoord.x, newCellCoord.y)) {
				// check if we are within 3.
				Bool passable = false;
				if (m_zoneManager.clipIsPassable(newCellCoord.x+3, newCellCoord.y+3)) passable = true;
				if (m_zoneManager.clipIsPassable(newCellCoord.x-3, newCellCoord.y+3)) passable = true;
				if (m_zoneManager.clipIsPassable(newCellCoord.x+3, newCellCoord.y-3)) passable = true;
				if (m_zoneManager.clipIsPassable(newCellCoord.x-3, newCellCoord.y-3)) passable = true;
				if (!passable) continue;
			}

			// check if this neighbor cell is already on the open (waiting to be tried) 
			// or closed (already tried) lists
			Bool onList = false;
			if (newCell->hasInfo()) {
				if (newCell->getOpen() || newCell->getClosed())
				{
					// already on one of the lists 
					onList = true;
				}
			}
			Int clearDiameter = 0;
			if (newCell!=goalCell) {

				if (i>=firstDiagonal) {
					// make sure one of the adjacent sides is open.
					if (!neighborFlags[adjacent[i-4]] && !neighborFlags[adjacent[i-3]]) {
						continue;
					}
				}

				// See how wide the cell is.
				clearDiameter = clearCellForDiameter(crusher, newCellCoord.x, newCellCoord.y, newCell->getLayer(), pathDiameter); 
				if (newCell->getType() != PathfindCell::CELL_CLEAR) {
					continue;
				}
				if (newCell->getPinched()) {
					continue;
				}
				neighborFlags[i] = true;

				if (!newCell->allocateInfo(newCellCoord)) {
					// Out of cells for pathing...
 					continue;
				}								
				cellCount++;

				newCostSoFar = newCell->costSoFar( parentCell );
				if (clearDiameter<pathDiameter) {
					int delta2 = pathDiameter-clearDiameter;
					newCostSoFar = static_cast<UnsignedInt>(newCostSoFar + (0.6f*(delta2*COST_ORTHOGONAL)));
				}
				newCell->setBlockedByAlly(false);
			}
			else
			{
				// newCostSoFar is not set for the goal cell, but it is read right below
				newCostSoFar = newCell->costSoFar( parentCell );
			}
			Int costRemaining = 0;
			costRemaining = newCell->costToGoal( goalCell );

			// check if this neighbor cell is already on the open (waiting to be tried) 
			// or closed (already tried) lists
			if (onList)
			{
				// already on one of the lists - if existing costSoFar is less, 
				// the new cell is on a longer path, so skip it
				if (newCell->getCostSoFar() <= newCostSoFar)
					continue;
			}
			//DEBUG_LOG(("CELL(%d,%d)L%d CD%d CSF %d, CR%d // ",newCell->getXIndex(), newCell->getYIndex(), 
			//	newCell->getLayer(), clearDiameter, newCostSoFar, costRemaining));
			//if ((cellCount&7)==0) DEBUG_LOG(("\n"));
			newCell->setCostSoFar(newCostSoFar);
			// keep track of path we're building - point back to cell we moved here from
			newCell->setParentCell(parentCell) ;
			newCell->setTotalCost(newCell->getCostSoFar() + costRemaining) ;

			//DEBUG_LOG(("Cell (%d,%d), Parent cost %d, newCostSoFar %d, cost rem %d, tot %d\n", 
			//	newCell->getXIndex(), newCell->getYIndex(), 
			//	newCell->costSoFar(parentCell), newCostSoFar, costRemaining, newCell->getCostSoFar() + costRemaining));

			// if newCell was on closed list, remove it from the list
			if (newCell->getClosed())
				m_closedList = newCell->removeFromClosedList( m_closedList );

			// if the newCell was already on the open list, remove it so it can be re-inserted in order
			if (newCell->getOpen())
				m_openList.remove(newCell);

			// insert newCell in open list such that open list is sorted, smallest total path cost first
			m_openList.insert(newCell);
		}


	}
	// failure - goal cannot be reached
#ifdef INTENSE_DEBUG
	DEBUG_LOG((" FAILURE\n"));
#endif	
#if defined _DEBUG
	if (TheGlobalData->m_debugAI)
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
 		RGBColor color;
		color.blue = 0;
		color.red = color.green = 1;
		addIcon(nullptr, 0, 0, color);
		debugShowSearch(false);
		Coord3D pos;
		pos = *from;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y ) + 0.5f;
		addIcon(&pos, 3*PATHFIND_CELL_SIZE_F, 600, color);
		pos = *to;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y ) + 0.5f;
		addIcon(&pos, 3*PATHFIND_CELL_SIZE_F, 600, color);	
		Real dx, dy;
		dx = from->x - to->x;
		dy = from->y - to->y;

		Int count = static_cast<Int>(sqrt(dx*dx+dy*dy)/(PATHFIND_CELL_SIZE_F/2));
		if (count<2) count = 2;
		Int i;
		color.green = 0;
		for (i=1; i<count; i++) {
			pos.x = from->x + (to->x-from->x)*i/count;
			pos.y = from->y + (to->y-from->y)*i/count;
			pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y ) + 0.5f;
			addIcon(&pos, PATHFIND_CELL_SIZE_F/2, 60, color);

		}
	}	

	DEBUG_LOG(("%d ", TheGameLogic->getFrame()));
	DEBUG_LOG(("FindGroundPath failed from (%f,%f) to (%f,%f)\n", from->x, from->y, to->x, to->y));
	DEBUG_LOG(("time %f\n", (Engine::Clock::milliseconds()-startTimeMS)/1000.0f));
#endif
#ifdef DUMP_PERF_STATS
	TheGameLogic->incrementOverallFailedPathfinds();
#endif
	m_isTunneling = false;
	goalCell->releaseInfo();
	cleanOpenAndClosedLists();
	return nullptr;
}

/**
 * Find a short, valid path between given locations.
 * Uses A* algorithm.
 */
void Pathfinder::processHierarchicalCell( const ICoord2D &scanCell, const ICoord2D &delta, PathfindCell *parentCell, 
																				 PathfindCell *goalCell, zoneStorageType parentZone, 
																				 zoneStorageType *examinedZones, Int &numExZones,
																				 Bool crusher, Int &cellCount)
{
	if (scanCell.x<m_extent.lo.x || scanCell.x>m_extent.hi.x ||
		scanCell.y<m_extent.lo.y || scanCell.y>m_extent.hi.y) {
		return;
	}
	if (parentZone == PathfindZoneManager::UNINITIALIZED_ZONE) {
		return;
	}
	if (parentZone == m_zoneManager.getBlockZone(LOCOMOTORSURFACE_GROUND,
		crusher, scanCell.x, scanCell.y, m_map)) { 
		PathfindCell *newCell = getCell(LAYER_GROUND, scanCell.x, scanCell.y);
		if( !newCell->hasInfo() )
		{
 			return;
		}

		if( newCell->getOpen() || newCell->getClosed() ) 
			return; // already looked at this one.

		ICoord2D adjacentCell = scanCell;
		//DEBUG_ASSERTCRASH(parentZone==newCell->getZone(), ("Different zones?"));
		if (parentZone!=newCell->getZone()) return;
		adjacentCell.x += delta.x;
		adjacentCell.y += delta.y;
		if (adjacentCell.x<m_extent.lo.x || adjacentCell.x>m_extent.hi.x ||
			adjacentCell.y<m_extent.lo.y || adjacentCell.y>m_extent.hi.y) {
			return;
		}
		PathfindCell *adjNewCell = getCell(LAYER_GROUND, adjacentCell.x, adjacentCell.y); 
		if (adjNewCell->hasInfo() && (adjNewCell->getOpen() || adjNewCell->getClosed())) return; // already looked at this one.
		zoneStorageType parentGlobalZone = m_zoneManager.getEffectiveZone(LOCOMOTORSURFACE_GROUND, crusher, parentZone);

		/// @todo - somehow out of bounds or bogus newZone.
		zoneStorageType newZone = m_zoneManager.getBlockZone(LOCOMOTORSURFACE_GROUND,
							crusher, adjacentCell.x, adjacentCell.y, m_map);
		zoneStorageType newGlobalZone = m_zoneManager.getEffectiveZone(LOCOMOTORSURFACE_GROUND, crusher, newZone);
		if (newGlobalZone != parentGlobalZone) {
			return; // can't step over. jba.
		}
		Int j;
		Bool found=false;
		for (j=0; j<numExZones; j++) {
			if (examinedZones[j] == newZone) {
				found = true;
				break;
			}
		}
		if (found) {
			return;
		}
		
		newCell->allocateInfo(scanCell);
		if (!newCell->getClosed() && !newCell->getOpen()) {
			m_closedList = newCell->putOnClosedList(m_closedList);
		}

		adjNewCell->allocateInfo(adjacentCell);
		if( adjNewCell->hasInfo() )
		{

			cellCount++;
			Int curCost = adjNewCell->costToHierGoal(parentCell);
			Int remCost = adjNewCell->costToHierGoal(goalCell);
			if (adjNewCell->getPinched() || newCell->getPinched()) {
				curCost += 2*COST_ORTHOGONAL;
			}	else {
				examinedZones[numExZones] = newZone;
				numExZones++;
			}

			adjNewCell->setCostSoFar(parentCell->getCostSoFar() + curCost);
			adjNewCell->setTotalCost(adjNewCell->getCostSoFar()+remCost);
			adjNewCell->setParentCellHierarchical(parentCell);
			// insert newCell in open list such that open list is sorted, smallest total path cost first
			m_openList.insert(adjNewCell);
		}

	}
}

/**
 * Find a short, valid path between given locations.
 * Uses A* algorithm.
 */
Path *Pathfinder::findHierarchicalPath( Bool isHuman, const LocomotorSet& locomotorSet, const Coord3D *from, 
													 const Coord3D *to, Bool crusher)
{
	return internal_findHierarchicalPath(isHuman, locomotorSet.getValidSurfaces(), from, to, crusher, FALSE);
}

/**
 * Find a short, valid path between given locations.
 * Uses A* algorithm.
 */
Path *Pathfinder::findClosestHierarchicalPath( Bool isHuman, const LocomotorSet& locomotorSet, const Coord3D *from, 
													 const Coord3D *to, Bool crusher)
{
	return internal_findHierarchicalPath(isHuman, locomotorSet.getValidSurfaces(), from, to, crusher, TRUE);
}

/**
 * Find a short, valid path between given locations.
 * Uses A* algorithm.
 */
Path *Pathfinder::internal_findHierarchicalPath( Bool isHuman, const LocomotorSurfaceTypeMask locomotorSurface, const Coord3D *from, 
													 const Coord3D *rawTo, Bool crusher, Bool closestOK)
{
	const SearchStatScope statScope( *this, SEARCH_HIERARCHICAL );
	//CRCDEBUG_LOG(("Pathfinder::findGroundPath()\n"));
#if defined _DEBUG
	Int startTimeMS = Engine::Clock::milliseconds();
#endif
	
	if (rawTo->x == 0.0f && rawTo->y == 0.0f) {
		DEBUG_LOG(("Attempting pathfind to 0,0, generally a bug.\n"));
		return nullptr;
	}
	DEBUG_ASSERTCRASH(m_openList.isEmpty() && m_closedList == nullptr, ("Dangling lists."));
	if (m_isMapReady == false) {
		return nullptr;
	}

	Coord3D adjustTo = *rawTo;
	Coord3D *to = &adjustTo;
	Coord3D clipFrom = *from;
	clip(&clipFrom, &adjustTo);

	m_isTunneling = false;

	PathfindLayerEnum destinationLayer = TheTerrainLogic->getLayerForDestination(to);
	
	ICoord2D cell;
	worldToCell( to, &cell );

	// determine goal cell
	PathfindCell *goalCell = getCell( destinationLayer, cell.x, cell.y );
	if (goalCell == nullptr) {
		return nullptr;
	}
	if (!goalCell->allocateInfo(cell)) {
		return nullptr;
	}

	// determine start cell
	ICoord2D startCellNdx;
	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(from);
	PathfindCell *parentCell = getClippedCell( layer,&clipFrom );
	if (parentCell == nullptr) {
		return nullptr;
	}
	if (parentCell!=goalCell) {
		worldToCell(&clipFrom, &startCellNdx);
		if (!parentCell->allocateInfo(startCellNdx)) {
			goalCell->releaseInfo();
			return nullptr;
		}
	}

	Int zone1, zone2;
	// m_isCrusher = false;
	zone1 = m_zoneManager.getEffectiveZone(locomotorSurface, false, parentCell->getZone()); 
	zone2 =  m_zoneManager.getEffectiveZone(locomotorSurface, false, goalCell->getZone());

	if ( zone1 != zone2) {
		goalCell->releaseInfo();
		parentCell->releaseInfo();
		return nullptr;
	}

	parentCell->startPathfind(goalCell);

	// "closed" list is initially empty
	m_closedList = nullptr;

	Int cellCount = 0;

	zoneStorageType goalBlockZone;
	ICoord2D goalBlockNdx;
	if (goalCell->getLayer()==LAYER_GROUND) {
		goalBlockZone = m_zoneManager.getBlockZone(locomotorSurface,
			crusher, goalCell->getXIndex(), goalCell->getYIndex(), m_map);
		
		goalBlockNdx.x = goalCell->getXIndex()/PathfindZoneManager::ZONE_BLOCK_SIZE;
		goalBlockNdx.y = goalCell->getYIndex()/PathfindZoneManager::ZONE_BLOCK_SIZE;
	}	else {
		goalBlockZone = goalCell->getZone();
		goalBlockNdx.x = -1;
		goalBlockNdx.y = -1;
	}

	if (parentCell->getLayer()==LAYER_GROUND) {
		// initialize "open" list to contain start cell
		m_openList.initWithStartCell(parentCell);
	}	else {
		m_openList.initWithStartCell(parentCell);
		PathfindLayerEnum layer2 = parentCell->getLayer();
		// We're starting on a bridge, so link to land at the bridge end points.
		ICoord2D ndx;
		ICoord2D toNdx;
		m_layers[layer2].getStartCellIndex(&ndx);
		m_layers[layer2].getEndCellIndex(&toNdx);
 		PathfindCell *cell7 = getCell(LAYER_GROUND, toNdx.x, toNdx.y);
		PathfindCell *startCell = getCell(LAYER_GROUND, ndx.x, ndx.y);
		if (cell7 && startCell) {
			// Close parent cell;
			m_openList.remove(parentCell);
			m_closedList = parentCell->putOnClosedList(m_closedList);
			startCell->allocateInfo(ndx);
			startCell->setParentCellHierarchical(parentCell);
			cellCount++;
			Int curCost = startCell->costToHierGoal(parentCell);
			Int remCost = startCell->costToHierGoal(goalCell);
			startCell->setCostSoFar(curCost);
			startCell->setTotalCost(remCost);
			startCell->setParentCellHierarchical(parentCell);
			// insert newCell in open list such that open list is sorted, smallest total path cost first
			m_openList.insert(startCell);

			cellCount++;
			cell7->allocateInfo(toNdx);
			curCost = cell7->costToHierGoal(parentCell);
			remCost = cell7->costToHierGoal(goalCell);
			cell7->setCostSoFar(curCost);
			cell7->setTotalCost(remCost);
			cell7->setParentCellHierarchical(parentCell);
			// insert newCell in open list such that open list is sorted, smallest total path cost first
			m_openList.insert(cell7);
		}
	}

	PathfindCell *closestCell = nullptr;
	Real closestDistSqr = sqr(HUGE_DIST);

	//
	// Continue search until "open" list is empty, or
	// until goal is found.
	//
	while( !m_openList.isEmpty() )
	{
		// take head cell off of open list - it has lowest estimated total path cost
		parentCell = m_openList.getHead();
		m_openList.remove(parentCell);

		zoneStorageType parentZone;
		if (parentCell->getLayer()==LAYER_GROUND) {
			parentZone = m_zoneManager.getBlockZone(locomotorSurface,
				crusher, parentCell->getXIndex(), parentCell->getYIndex(), m_map);
		}	else {
			parentZone = parentCell->getZone();
		}

		Bool reachedGoal = false;
		
		Int blockX = parentCell->getXIndex()/PathfindZoneManager::ZONE_BLOCK_SIZE;
		Int blockY = parentCell->getYIndex()/PathfindZoneManager::ZONE_BLOCK_SIZE;
		if (parentZone == goalBlockZone) {
			if (goalBlockNdx.x == -1 || (blockX==goalBlockNdx.x && blockY == goalBlockNdx.y)) {
				reachedGoal = true;
			} else {
				DEBUG_LOG(("Hmm, got match before correct cell."));
			}
		}

		ICoord2D zoneBlockExtent;
		m_zoneManager.getExtent(zoneBlockExtent);
		
		if (!reachedGoal && m_zoneManager.interactsWithBridge(parentCell->getXIndex(), parentCell->getYIndex())) {
			Int i;
			for (i=0; i<=LAYER_LAST; i++) {
				if (m_layers[i].isUnused() || m_layers[i].isDestroyed()) {
					continue;
				}
				ICoord2D ndx;
				ICoord2D toNdx;
				m_layers[i].getStartCellIndex(&ndx);
				m_layers[i].getEndCellIndex(&toNdx);
				if (ndx.x/PathfindZoneManager::ZONE_BLOCK_SIZE != blockX ||
						ndx.y/PathfindZoneManager::ZONE_BLOCK_SIZE != blockY) {
					m_layers[i].getStartCellIndex(&toNdx);
					m_layers[i].getEndCellIndex(&ndx);
				}
				if (ndx.x<0 || ndx.y<0) continue;
				if (toNdx.x<0 || toNdx.y<0) continue;
				if (ndx.x/PathfindZoneManager::ZONE_BLOCK_SIZE == blockX &&
						ndx.y/PathfindZoneManager::ZONE_BLOCK_SIZE == blockY) {
					// Bridge connects to this block.
					Int bridgeZone = m_zoneManager.getBlockZone(locomotorSurface, crusher, ndx.x, ndx.y, m_map);
					if (bridgeZone != parentZone) {
						continue;
					}
					// We have a winner.
					if (m_layers[i].getZone() == goalBlockZone) {
						reachedGoal = true;
						break;
					}
 					PathfindCell *cell6 = getCell(LAYER_GROUND, toNdx.x, toNdx.y);
					if (cell6==nullptr) continue;
					if (cell6->hasInfo() && (cell6->getClosed() || cell6->getOpen())) {
						continue;
					}
					PathfindCell *startCell = getCell(LAYER_GROUND, ndx.x, ndx.y); 
					if (startCell==nullptr) continue;
					if (startCell != parentCell) {
						startCell->allocateInfo(ndx);
						startCell->setParentCellHierarchical(parentCell);
						if (!startCell->getClosed() && !startCell->getOpen()) {
							m_closedList = startCell->putOnClosedList(m_closedList);
						}
					}
					cell6->allocateInfo(toNdx);
					cell6->setParentCellHierarchical(startCell);

					cellCount++;
					Int curCost = cell6->costToHierGoal(startCell);
					Int remCost = cell6->costToHierGoal(goalCell);

					cell6->setCostSoFar(startCell->getCostSoFar() + curCost);
					cell6->setTotalCost(cell6->getCostSoFar()+remCost);
					cell6->setParentCellHierarchical(startCell);
					// insert newCell in open list such that open list is sorted, smallest total path cost first
					m_openList.insert(cell6);

				}
			}
		}

		if (reachedGoal)
		{
			if (parentCell != goalCell) {
				goalCell->setParentCellHierarchical(parentCell);
			}
			// success - found a path to the goal	 

			m_isTunneling = false;
			// construct and return path
			Path *path =  buildHierachicalPath( from, goalCell );
#if defined _DEBUG
			Bool show = TheGlobalData->m_debugAI==AI_DEBUG_PATHS;
			show |= (TheGlobalData->m_debugAI==AI_DEBUG_GROUND_PATHS);
			if (show)	{
				debugShowSearch(true);
			}
#endif
			if (goalCell->hasInfo() && !goalCell->getClosed() && !goalCell->getOpen()) {
				goalCell->releaseInfo();
			}
			parentCell->releaseInfo();
			cleanOpenAndClosedLists();
			return path;
		}	

#if defined _DEBUG
#endif
		Real dx = static_cast<Real>(IABS(goalCell->getXIndex()-parentCell->getXIndex()));
		Real dy = static_cast<Real>(IABS(goalCell->getYIndex()-parentCell->getYIndex()));
		Real distSqr = dx*dx+dy*dy;
		if (distSqr < closestDistSqr) {
			closestCell = parentCell;
			closestDistSqr = distSqr;
		}

		// put parent cell onto closed list - its evaluation is finished
		m_closedList = parentCell->putOnClosedList( m_closedList );

		Int i;
		zoneStorageType examinedZones[PathfindZoneManager::ZONE_BLOCK_SIZE];
		Int numExZones = 0;
		// Left side.
		if (blockX>0) {
			for (i=1; i<=PathfindZoneManager::ZONE_BLOCK_SIZE; i++) {
			ICoord2D scanCell;
				scanCell.x = blockX*PathfindZoneManager::ZONE_BLOCK_SIZE;
				scanCell.y = (blockY*PathfindZoneManager::ZONE_BLOCK_SIZE);
				scanCell.y += PathfindZoneManager::ZONE_BLOCK_SIZE/2;
				Int offset = i>>1;
				if (i&1) offset = -offset;
				scanCell.y += offset;	
				ICoord2D delta;
				delta.x = -1; // left side moves -1.
				delta.y = 0;
				PathfindCell *cell5 = getCell(LAYER_GROUND, scanCell.x, scanCell.y);
				if (cell5==nullptr) continue;
				if (cell5->hasInfo() && (cell5->getClosed() || cell5->getOpen())) {
					if (parentZone == m_zoneManager.getBlockZone(locomotorSurface,
						crusher, scanCell.x, scanCell.y, m_map)) { 
						break;
					}
				}
				if (isHuman) {
					if (scanCell.x < m_logicalExtent.lo.x || scanCell.x > m_logicalExtent.hi.x ||
							scanCell.y < m_logicalExtent.lo.y || scanCell.y > m_logicalExtent.hi.y) {
						continue;
					}
				}
				processHierarchicalCell(scanCell, delta, parentCell, 
					goalCell, parentZone, examinedZones, numExZones, crusher, cellCount);
			}
		}
		// Right side.
		if (blockX<zoneBlockExtent.x-1) {
			numExZones = 0;
			for (i=1; i<=PathfindZoneManager::ZONE_BLOCK_SIZE; i++) {
			ICoord2D scanCell;
				scanCell.x = blockX*PathfindZoneManager::ZONE_BLOCK_SIZE;
				scanCell.x += PathfindZoneManager::ZONE_BLOCK_SIZE-1;
				scanCell.y = (blockY*PathfindZoneManager::ZONE_BLOCK_SIZE);
				scanCell.y += PathfindZoneManager::ZONE_BLOCK_SIZE/2;
				Int offset = i>>1;
				if (i&1) offset = -offset;
				scanCell.y += offset;	
				ICoord2D delta;
				delta.x = 1; // right side moves +1.
				delta.y = 0;
				PathfindCell *cell4 = getCell(LAYER_GROUND, scanCell.x, scanCell.y);
				if (cell4==nullptr) continue;
				if (cell4->hasInfo() && (cell4->getClosed() || cell4->getOpen())) {
					if (parentZone == m_zoneManager.getBlockZone(locomotorSurface,
						crusher, scanCell.x, scanCell.y, m_map)) { 
						break;
					}
				}
				if (isHuman) {
					if (scanCell.x < m_logicalExtent.lo.x || scanCell.x > m_logicalExtent.hi.x ||
							scanCell.y < m_logicalExtent.lo.y || scanCell.y > m_logicalExtent.hi.y) {
						continue;
					}
				}
				processHierarchicalCell(scanCell, delta, parentCell, 
					goalCell, parentZone, examinedZones, numExZones, crusher, cellCount);
			}
		}
		// Top side.
		if (blockY>0) {
			numExZones = 0;
			for (i=1; i<=PathfindZoneManager::ZONE_BLOCK_SIZE; i++) {
			ICoord2D scanCell;
				scanCell.y = blockY*PathfindZoneManager::ZONE_BLOCK_SIZE;
				scanCell.x = (blockX*PathfindZoneManager::ZONE_BLOCK_SIZE);
				scanCell.x += PathfindZoneManager::ZONE_BLOCK_SIZE/2;
				Int offset = i>>1;
				if (i&1) offset = -offset;
				scanCell.x += offset;	
				ICoord2D delta;
				delta.x = 0; 
				delta.y = -1;	// Top side moves -1.
				PathfindCell *cell3 = getCell(LAYER_GROUND, scanCell.x, scanCell.y);
				if (cell3==nullptr) continue;
				if (cell3->hasInfo() && (cell3->getClosed() || cell3->getOpen())) {
					if (parentZone == m_zoneManager.getBlockZone(locomotorSurface,
						crusher, scanCell.x, scanCell.y, m_map)) { 
						break;
					}
				}
				if (isHuman) {
					if (scanCell.x < m_logicalExtent.lo.x || scanCell.x > m_logicalExtent.hi.x ||
							scanCell.y < m_logicalExtent.lo.y || scanCell.y > m_logicalExtent.hi.y) {
						continue;
					}
				}
				processHierarchicalCell(scanCell, delta, parentCell, 
					goalCell, parentZone, examinedZones, numExZones, crusher, cellCount);
			}
		}
		// Bottom side.
		if (blockY<zoneBlockExtent.y-1) {
			numExZones = 0;
			for (i=1; i<=PathfindZoneManager::ZONE_BLOCK_SIZE; i++) {
			ICoord2D scanCell;
				scanCell.y = blockY*PathfindZoneManager::ZONE_BLOCK_SIZE;
				scanCell.y += PathfindZoneManager::ZONE_BLOCK_SIZE-1;
				scanCell.x = (blockX*PathfindZoneManager::ZONE_BLOCK_SIZE);
				scanCell.x += PathfindZoneManager::ZONE_BLOCK_SIZE/2;
				Int offset = i>>1;
				if (i&1) offset = -offset;
				scanCell.x += offset;	
				ICoord2D delta;
				delta.x = 0; 
				delta.y = 1; // Top side moves +1.
				PathfindCell *cell2 = getCell(LAYER_GROUND, scanCell.x, scanCell.y);
				if (cell2==nullptr) continue;
				if (cell2->hasInfo() && (cell2->getClosed() || cell2->getOpen())) {
					if (parentZone == m_zoneManager.getBlockZone(locomotorSurface,
						crusher, scanCell.x, scanCell.y, m_map)) { 
						break;
					}
				}
				if (isHuman) {
					if (scanCell.x < m_logicalExtent.lo.x || scanCell.x > m_logicalExtent.hi.x ||
							scanCell.y < m_logicalExtent.lo.y || scanCell.y > m_logicalExtent.hi.y) {
						continue;
					}
				}
				processHierarchicalCell(scanCell, delta, parentCell, 
					goalCell, parentZone, examinedZones, numExZones, crusher, cellCount);
			}
		}
	}

	if (closestOK && closestCell) {
		m_isTunneling = false;
		// construct and return path
		Path *path =  buildHierachicalPath( from, closestCell );
#if defined _DEBUG
#endif
		if (goalCell->hasInfo() && !goalCell->getClosed() && !goalCell->getOpen()) {
			goalCell->releaseInfo();
		}
		cleanOpenAndClosedLists();
		return path;
	}

	// failure - goal cannot be reached
#if defined _DEBUG
	if (TheGlobalData->m_debugAI)
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
 		RGBColor color;
		color.blue = 0;
		color.red = color.green = 1;
		addIcon(nullptr, 0, 0, color);
		debugShowSearch(false);
		Coord3D pos;
		pos = *from;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y ) + 0.5f;
		addIcon(&pos, 3*PATHFIND_CELL_SIZE_F, 600, color);
		pos = *to;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y ) + 0.5f;
		addIcon(&pos, 3*PATHFIND_CELL_SIZE_F, 600, color);	
		Real dx, dy;
		dx = from->x - to->x;
		dy = from->y - to->y;

		Int count = static_cast<Int>(sqrt(dx*dx+dy*dy)/(PATHFIND_CELL_SIZE_F/2));
		if (count<2) count = 2;
		Int i;
		color.green = 0;
		for (i=1; i<count; i++) {
			pos.x = from->x + (to->x-from->x)*i/count;
			pos.y = from->y + (to->y-from->y)*i/count;
			pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y ) + 0.5f;
			addIcon(&pos, PATHFIND_CELL_SIZE_F/2, 60, color);

		}
	}	

	DEBUG_LOG(("%d ", TheGameLogic->getFrame()));
	DEBUG_LOG(("FindHierarchicalPath failed from (%f,%f) to (%f,%f)\n", from->x, from->y, to->x, to->y));
	DEBUG_LOG(("time %f\n", (Engine::Clock::milliseconds()-startTimeMS)/1000.0f));
#endif
#ifdef DUMP_PERF_STATS
	TheGameLogic->incrementOverallFailedPathfinds();
#endif
	m_isTunneling = false;
	goalCell->releaseInfo();
	cleanOpenAndClosedLists();
	return nullptr;
}

/**
 * Does any broken bridge join from and to?  
 * True means that if bridge BridgeID is repaired, there is a land path from to to..
 */
Bool Pathfinder::findBrokenBridge(const LocomotorSet& locoSet, 
																	const Coord3D *from, const Coord3D *to, ObjectID *bridgeID) 
{
	// See if terrain or building is blocking the destination.
	PathfindLayerEnum destinationLayer = TheTerrainLogic->getLayerForDestination(to);
	PathfindLayerEnum fromLayer = TheTerrainLogic->getLayerForDestination(from);

	Int zone1, zone2;
	*bridgeID = INVALID_ID;

	PathfindCell *parentCell = getClippedCell(fromLayer, from);
	PathfindCell *goalCell = getClippedCell(destinationLayer, to);
	zone1 = m_zoneManager.getEffectiveZone(locoSet.getValidSurfaces(), false, parentCell->getZone()); 
	zone2 =  m_zoneManager.getEffectiveZone(locoSet.getValidSurfaces(), false, goalCell->getZone());
	zone1 = m_zoneManager.getEffectiveTerrainZone(static_cast<zoneStorageType>(zone1));
	zone2 = m_zoneManager.getEffectiveTerrainZone(static_cast<zoneStorageType>(zone2));
	zone1 = m_zoneManager.getEffectiveZone(locoSet.getValidSurfaces(), false, static_cast<zoneStorageType>(zone1)); 
	zone2 =  m_zoneManager.getEffectiveZone(locoSet.getValidSurfaces(), false, static_cast<zoneStorageType>(zone2));

	// If the terrain is connected using this locomotor set, we can path somehow.
	if (zone1 == zone2) {
		// There is not terrain blocking the from & to.
		return false;
	}

	// Check broken bridges.
	Int i;
	for (i=0; i<=LAYER_LAST; i++) {
		if (m_layers[i].isDestroyed()) {
			if (m_layers[i].connectsZones(&m_zoneManager, locoSet, zone1, zone2)) {
				*bridgeID = m_layers[i].getBridgeID();
				return true;
			}
		}
	}
	return false;
}

/**
 * Does any path exist from 'from' to 'to' given the locomotor set
 * This is the quick check, only looks at whether the terrain is possible or
 * impossible to path over.  Doesn't take other units into account.
 * False means it is impossible to path.
 * True means it is possible given the terrain, but there may be units in the way.
 */
Bool Pathfinder::clientSafeQuickDoesPathExist( const LocomotorSet& locomotorSet, 
																const Coord3D *from, 
																const Coord3D *to ) 
{
	// See if terrain or building is blocking the destination.
	PathfindLayerEnum destinationLayer = TheTerrainLogic->getLayerForDestination(to);
	if (!validMovementPosition(false, destinationLayer, locomotorSet, to)) {
		return false;
	}
	PathfindLayerEnum fromLayer = TheTerrainLogic->getLayerForDestination(from);
	Int zone1, zone2;

	PathfindCell *parentCell = getClippedCell(fromLayer, from);
	PathfindCell *goalCell = getClippedCell(destinationLayer, to);
	if (goalCell->getType()==PathfindCell::CELL_CLIFF) {
		return false; // No goals on cliffs.
	}
	Bool doingTerrainZone = false;
	zone1 = m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), false, parentCell->getZone()); 

	if (parentCell->getType() == PathfindCell::CELL_OBSTACLE) {
		doingTerrainZone = true;
		if (zone1 == PathfindZoneManager::UNINITIALIZED_ZONE) {
			// We are in a building that just got placed, and zones haven't been updated yet. [8/8/2003]
			// It is better to return a false positive than a false negative. jba.
			return true;
		}
	}
	zone2 =  m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), false, goalCell->getZone());
	if (goalCell->getType() == PathfindCell::CELL_OBSTACLE) {
		doingTerrainZone = true;
	}
	if (doingTerrainZone) {
		zone1 = parentCell->getZone();
		zone1 = m_zoneManager.getEffectiveTerrainZone(static_cast<zoneStorageType>(zone1));
		zone1 = m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), false, static_cast<zoneStorageType>(zone1)); 
		zone1 = m_zoneManager.getEffectiveTerrainZone(static_cast<zoneStorageType>(zone1));
		zone2 = goalCell->getZone();
		zone2 = m_zoneManager.getEffectiveTerrainZone(static_cast<zoneStorageType>(zone2));
		zone2 = m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), false, static_cast<zoneStorageType>(zone2)); 
		zone2 = m_zoneManager.getEffectiveTerrainZone(static_cast<zoneStorageType>(zone2));
	}
	// If the terrain is connected using this locomotor set, we can path somehow.
	if (zone1 == zone2) {
		// There is not terrain blocking the from & to.
		return true;
	}
	return FALSE;  // no path exists

}

/**
 * Does any path exist from 'from' to 'to' given the locomotor set
 * This is the quick check, only looks at whether the terrain is possible or
 * impossible to path over.  Doesn't take other units into account.
 * False means it is impossible to path.
 * True means it is possible given the terrain, but there may be units in the way.
 */
Bool Pathfinder::clientSafeQuickDoesPathExistForUI( const LocomotorSet& locomotorSet, 
																const Coord3D *from, 
																const Coord3D *to ) 
{
	// See if terrain or building is blocking the destination.
	PathfindLayerEnum destinationLayer = TheTerrainLogic->getLayerForDestination(to);
	PathfindLayerEnum fromLayer = TheTerrainLogic->getLayerForDestination(from);
	Int zone1, zone2;

	PathfindCell *parentCell = getClippedCell(fromLayer, from);
	PathfindCell *goalCell = getClippedCell(destinationLayer, to);
	if (goalCell->getType()==PathfindCell::CELL_CLIFF) {
		return false; // No goals on cliffs.
	}

	zone1 = m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), false, parentCell->getZone()); 
	zone2 =  m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), false, goalCell->getZone());

	if (zone1 == PathfindZoneManager::UNINITIALIZED_ZONE ||
			zone2 == PathfindZoneManager::UNINITIALIZED_ZONE) {
		// We are in a building that just got placed, and zones haven't been updated yet. [8/8/2003]
		// It is better to return a false positive than a false negative. jba.
		return true;
	}
	/* Do the effective terrain zone.  This feedback is for the ui, so we won't take structures into account, 
		beacuse if they are visible it will be obvious, and if they are stealthed they should be invisible to the 
		pathing as well. jba. */
	zone1 = parentCell->getZone();
	zone1 = m_zoneManager.getEffectiveTerrainZone(static_cast<zoneStorageType>(zone1));
	zone1 = m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), false, static_cast<zoneStorageType>(zone1)); 
	zone1 = m_zoneManager.getEffectiveTerrainZone(static_cast<zoneStorageType>(zone1));
	zone2 = goalCell->getZone();
	zone2 = m_zoneManager.getEffectiveTerrainZone(static_cast<zoneStorageType>(zone2));
	zone2 = m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), false, static_cast<zoneStorageType>(zone2)); 
	zone2 = m_zoneManager.getEffectiveTerrainZone(static_cast<zoneStorageType>(zone2));

	if (zone1 == PathfindZoneManager::UNINITIALIZED_ZONE) {
		// We are in a building that just got placed, and zones haven't been updated yet. [8/8/2003]
		// It is better to return a false positive than a false negative. jba.
		return true;
	}
	// If the terrain is connected using this locomotor set, we can path somehow.
	if (zone1 == zone2) {
		// There is not terrain blocking the from & to.
		return true;
	}
	return FALSE;  // no path exists

}

/**
 * Does any path exist from 'from' to 'to' given the locomotor set
 * This is the careful check, looks at whether the terrain, buindings and units are possible or
 * impossible to path over.  Takes other units into account.
 * False means it is impossible to path.
 * True means it is possible to path.
 */
Bool Pathfinder::slowDoesPathExist( Object *obj, 
																const Coord3D *from, 
																const Coord3D *to,
																ObjectID ignoreObject)
{
	AIUpdateInterface *ai = obj->getAI();
	if (ai==nullptr) {
		return false;
	}
	const LocomotorSet &locoSet = ai->getLocomotorSet();
	m_ignoreObstacleID = ignoreObject;
	Path *path = findPath(obj, locoSet, from, to);
	m_ignoreObstacleID = INVALID_ID;
	Bool found = (path!=nullptr);
	if (path) {
		deleteInstanceOf(path);
		path = nullptr;
	}
	return found;
}

void Pathfinder::clip( Coord3D *from, Coord3D *to )
{
	ICoord2D fromCell, toCell;
	ICoord2D clipFromCell, clipToCell;
	fromCell.x = REAL_TO_INT_FLOOR(from->x/PATHFIND_CELL_SIZE);
	fromCell.y = REAL_TO_INT_FLOOR(from->y/PATHFIND_CELL_SIZE);
	toCell.x = REAL_TO_INT_FLOOR(to->x/PATHFIND_CELL_SIZE);
	toCell.y = REAL_TO_INT_FLOOR(to->y/PATHFIND_CELL_SIZE);
	if (ClipLine2D(&fromCell, &toCell, &clipFromCell, &clipToCell,&m_extent)) {
		if (fromCell.x!=clipFromCell.x || fromCell.y != clipFromCell.y) {
			from->x = clipFromCell.x*PATHFIND_CELL_SIZE_F + 0.05f;
			from->y = clipFromCell.y*PATHFIND_CELL_SIZE_F + 0.05f;
		}
		if (toCell.x!=clipToCell.x || toCell.y != clipToCell.y) {
			to->x = clipToCell.x*PATHFIND_CELL_SIZE_F + 0.05f;
			to->y = clipToCell.y*PATHFIND_CELL_SIZE_F + 0.05f;
		}
	}

}

Bool Pathfinder::pathDestination( 	Object *obj, const LocomotorSet& locomotorSet, Coord3D *dest, 
		PathfindLayerEnum layer, const Coord3D *groupDest)
{
	//CRCDEBUG_LOG(("Pathfinder::pathDestination()\n"));
	if (m_isMapReady == false) return NULL;
	
	if (!obj) return false;

	Int cellCount = 0;
#define MAX_CELL_COUNT 500

	Coord3D adjustTo = *groupDest;
	Coord3D *to = &adjustTo;
	DEBUG_ASSERTCRASH(m_openList.isEmpty() && m_closedList == nullptr, ("Dangling lists."));
	// create unique "mark" values for open and closed cells for this pathfind invocation

	Bool isCrusher = obj ? obj->getCrusherLevel() > 0 : false;

	PathfindLayerEnum desiredLayer = TheTerrainLogic->getLayerForDestination(dest);
	// determine desired
	PathfindCell *desiredCell = getClippedCell( desiredLayer,  dest );
	if (desiredCell == nullptr)
		return FALSE;

	PathfindLayerEnum goalLayer = TheTerrainLogic->getLayerForDestination(to);
	// determine goal cell
	PathfindCell *goalCell = getClippedCell( goalLayer,  to );
	if (goalCell == nullptr)
		return FALSE;


	Bool isHuman = true;
	if (obj && obj->getControllingPlayer() && (obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER)) {
		isHuman = false; // computer gets to cheat.
	}
	Bool center;
	Int radius;
	getRadiusAndCenter(obj, radius, center);

	// determine start cell
	ICoord2D startCellNdx; 
	worldToCell(dest, &startCellNdx);
	PathfindCell *parentCell = getCell( layer, startCellNdx.x, startCellNdx.y );
	if (parentCell == nullptr)
		return FALSE;
	ICoord2D pos2d;
	worldToCell(to, &pos2d);
	if (!goalCell->allocateInfo(pos2d)) {
		return FALSE;
	}

	if (parentCell!=goalCell) {
		if (!parentCell->allocateInfo(startCellNdx)) {
			desiredCell->releaseInfo();
			goalCell->releaseInfo();
			return FALSE;
		}
	}

	PathfindCell *closestCell = nullptr;
	Real closestDistanceSqr = FLT_MAX;
	Coord3D closestPos = {};

	if (validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), parentCell ) == false) {
		parentCell->releaseInfo();
		goalCell->releaseInfo();
		return FALSE;
	}

	parentCell->startPathfind(goalCell);

	// initialize "open" list to contain start cell
	m_openList.initWithStartCell(parentCell);

	// "closed" list is initially empty
	m_closedList = nullptr;

	//
	// Continue search until "open" list is empty, or
	// until goal is found.
	//
	while( !m_openList.isEmpty() )
	{
		// take head cell off of open list - it has lowest estimated total path cost
		parentCell = m_openList.getHead();
		m_openList.remove(parentCell);

		Coord3D pos;
		// put parent cell onto closed list - its evaluation is finished
		m_closedList = parentCell->putOnClosedList( m_closedList );
		if (checkForAdjust(obj, locomotorSet, isHuman, parentCell->getXIndex(), parentCell->getYIndex(), parentCell->getLayer(), 
			radius, center, &pos, groupDest)) { 
			Int dx = IABS(goalCell->getXIndex()-parentCell->getXIndex());
			Int dy = IABS(goalCell->getYIndex()-parentCell->getYIndex());
			Real distSqr = static_cast<Real>(dx*dx+dy*dy);
			//Real cost = (parentCell->getCostSoFar()*(parentCell->getCostSoFar()*COST_TO_DISTANCE_FACTOR))*0.5f;
			//distSqr += sqr(cost);
			if (distSqr < closestDistanceSqr) {
				closestCell = parentCell;
				closestDistanceSqr = distSqr;
				closestPos = pos;
			} else {
				continue;
			}
		} else {
			continue;
		}

		if (cellCount > MAX_CELL_COUNT) {
			continue;
		}
		// Check to see if we can change layers in this cell.
		checkChangeLayers(parentCell);

		// expand search to neighboring orthogonal cells
		static ICoord2D delta[] = 
		{ 
			{ 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 }, 
			{ 1, 1 }, { -1, 1 }, { -1, -1 }, { 1, -1 } 
		};
		const Int numNeighbors = 8;
		const Int firstDiagonal = 4;
		ICoord2D newCellCoord;
		PathfindCell *newCell;
		const Int adjacent[5] = {0, 1, 2, 3, 0};
		Bool neighborFlags[8] = {false, false, false, false, false, false, false};

		UnsignedInt newCostSoFar;

		for( int i=0; i<numNeighbors; i++ )
		{
			neighborFlags[i] = false;
			// determine neighbor cell to try
			newCellCoord.x = parentCell->getXIndex() + delta[i].x;
			newCellCoord.y = parentCell->getYIndex() + delta[i].y;

			// get the neighboring cell
			newCell = getCell(parentCell->getLayer(), newCellCoord.x, newCellCoord.y );

			// check if cell is on the map
			if (newCell == nullptr)
				continue;

			// check if this neighbor cell is already on the open (waiting to be tried) 
			// or closed (already tried) lists
			Bool onList = false;
			if (newCell->hasInfo()) {
				if (newCell->getOpen() || newCell->getClosed())
				{
					// already on one of the lists 
					onList = true;
				}
			}
			if (i>=firstDiagonal) {
				// make sure one of the adjacent sides is open.
				if (!neighborFlags[adjacent[i-4]] && !neighborFlags[adjacent[i-3]]) {
					continue;
				}
			}

			if (!validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), newCell, parentCell )) {
				continue;
			}	

			neighborFlags[i] = true;

			if (!newCell->allocateInfo(newCellCoord)) {
				// Out of cells for pathing...
 				return cellCount;
			}								
			cellCount++;

			newCostSoFar = newCell->costSoFar( parentCell );
			newCell->setBlockedByAlly(false);

			// check if this neighbor cell is already on the open (waiting to be tried) 
			// or closed (already tried) lists
			if (onList)
			{
				// already on one of the lists - if existing costSoFar is less, 
				// the new cell is on a longer path, so skip it
				if (newCell->getCostSoFar() <= newCostSoFar)
					continue;
			}

			// keep track of path we're building - point back to cell we moved here from
			newCell->setParentCell(parentCell) ;

			// store cost of this path
			newCell->setCostSoFar(newCostSoFar);
			
			Int costRemaining = 0;
			if (goalCell) {
				costRemaining = newCell->costToGoal( goalCell );
			}

			newCell->setTotalCost(newCell->getCostSoFar() + costRemaining) ;

			//DEBUG_LOG(("Cell (%d,%d), Parent cost %d, newCostSoFar %d, cost rem %d, tot %d\n", 
			//	newCell->getXIndex(), newCell->getYIndex(), 
			//	newCell->costSoFar(parentCell), newCostSoFar, costRemaining, newCell->getCostSoFar() + costRemaining));

			// if newCell was on closed list, remove it from the list
			if (newCell->getClosed())
				m_closedList = newCell->removeFromClosedList( m_closedList );

			// if the newCell was already on the open list, remove it so it can be re-inserted in order
			if (newCell->getOpen())
				m_openList.remove(newCell);

			// insert newCell in open list such that open list is sorted, smallest total path cost first
			m_openList.insert(newCell);
		}
	}

#if defined _DEBUG
	if (closestCell) {
		debugShowSearch(true);
		*dest = closestPos;
	} else {
		debugShowSearch(true);
	}
#endif
	m_isTunneling = false;
	cleanOpenAndClosedLists();
	goalCell->releaseInfo();
	return false;
}

struct TightenPathStruct
{
	Object *obj;
	const LocomotorSet *locomotorSet;
	PathfindLayerEnum layer;
	Int		radius;
	Bool	center;
	Bool	foundDest;
	Coord3D destPos;
};

/*static*/ Int Pathfinder::tightenPathCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	TightenPathStruct* d = static_cast<TightenPathStruct*>(userData);
	if (from == nullptr || to==nullptr) return 0;
	if (d->layer != to->getLayer()) {
		return 0; // abort.
	}
	Coord3D pos;
	if (!TheAI->pathfinder()->checkForAdjust(d->obj, *d->locomotorSet, true, to_x, to_y, to->getLayer(), d->radius, d->center, &pos, nullptr)) 
	{
		return 0;	// bail early
	}
	d->foundDest = true; 
	d->destPos = pos;

	return 0;	// keep going
}

/* Returns the cost, which is in the same units as coord3d distance. */
void Pathfinder::tightenPath(Object *obj, const LocomotorSet& locomotorSet, Coord3D *from, 
		const Coord3D *to)
{
	TightenPathStruct info;

	getRadiusAndCenter(obj, info.radius, info.center);
	info.layer = TheTerrainLogic->getLayerForDestination(from);
	info.obj = obj;
	info.locomotorSet = &locomotorSet;
	info.foundDest = false;
	iterateCellsAlongLine(*from, *to, info.layer, tightenPathCallback, &info);
	if (info.foundDest) {
		*from = info.destPos;
	}
}

/* Returns the cost, which is in the same units as coord3d distance. */
Int Pathfinder::checkPathCost(Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from, 
		const Coord3D *rawTo)
{
	const SearchStatScope statScope( *this, SEARCH_COST );
	//CRCDEBUG_LOG(("Pathfinder::checkPathCost()\n"));
	if (m_isMapReady == false) return NULL;
	enum {MAX_COST = 0x7fff0000};	
	if (!obj) return MAX_COST;

	Int cellCount = 0;
#define MAX_CELL_COUNT 500

	Coord3D adjustTo = *rawTo;
	Coord3D *to = &adjustTo;
	DEBUG_ASSERTCRASH(m_openList.isEmpty() && m_closedList == nullptr, ("Dangling lists."));
	// create unique "mark" values for open and closed cells for this pathfind invocation

	Bool isCrusher = obj ? obj->getCrusherLevel() > 0 : false;

	PathfindLayerEnum goalLayer = TheTerrainLogic->getLayerForDestination(to);
	// determine goal cell
	PathfindCell *goalCell = getClippedCell( goalLayer,  to );
	if (goalCell == nullptr)
		return MAX_COST;


	Bool center;
	Int radius;
	getRadiusAndCenter(obj, radius, center);

	// determine start cell
	ICoord2D startCellNdx; 
	worldToCell(from, &startCellNdx);
	PathfindLayerEnum fromLayer = TheTerrainLogic->getLayerForDestination(from);
	PathfindCell *parentCell = getCell( fromLayer, from );
	if (parentCell == nullptr)
		return MAX_COST;
	ICoord2D pos2d;
	worldToCell(to, &pos2d);
	if (!goalCell->allocateInfo(pos2d)) {
		return MAX_COST;
	}

	if (parentCell!=goalCell) {
		if (!parentCell->allocateInfo(startCellNdx)) {
			goalCell->releaseInfo();
			return MAX_COST;
		}
	}

	if (validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), parentCell ) == false) {
		parentCell->releaseInfo();
		goalCell->releaseInfo();
		return MAX_COST;
	}

	parentCell->startPathfind(goalCell);

	// initialize "open" list to contain start cell
	m_openList.initWithStartCell(parentCell);

	// "closed" list is initially empty
	m_closedList = nullptr;

	//
	// Continue search until "open" list is empty, or
	// until goal is found.
	//
	while( !m_openList.isEmpty() )
	{
		// take head cell off of open list - it has lowest estimated total path cost
		parentCell = m_openList.getHead();
		m_openList.remove(parentCell);

		// put parent cell onto closed list - its evaluation is finished
		m_closedList = parentCell->putOnClosedList( m_closedList );

		if (parentCell==goalCell) {	 
			Int cost = parentCell->getTotalCost();
			m_isTunneling = false;
			cleanOpenAndClosedLists();
			return cost;
		}

		if (cellCount > MAX_CELL_COUNT) {
			continue;
		}
		// Check to see if we can change layers in this cell.
		checkChangeLayers(parentCell);

		// expand search to neighboring orthogonal cells
		static ICoord2D delta[] = 
		{ 
			{ 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 }, 
			{ 1, 1 }, { -1, 1 }, { -1, -1 }, { 1, -1 } 
		};
		const Int numNeighbors = 8;
		const Int firstDiagonal = 4;
		ICoord2D newCellCoord;
		PathfindCell *newCell;
		const Int adjacent[5] = {0, 1, 2, 3, 0};
		Bool neighborFlags[8] = {false, false, false, false, false, false, false};

		UnsignedInt newCostSoFar;

		for( int i=0; i<numNeighbors; i++ )
		{
			neighborFlags[i] = false;
			// determine neighbor cell to try
			newCellCoord.x = parentCell->getXIndex() + delta[i].x;
			newCellCoord.y = parentCell->getYIndex() + delta[i].y;

			// get the neighboring cell
			newCell = getCell(parentCell->getLayer(), newCellCoord.x, newCellCoord.y );

			// check if cell is on the map
			if (newCell == nullptr)
				continue;

			// check if this neighbor cell is already on the open (waiting to be tried) 
			// or closed (already tried) lists
			Bool onList = false;
			if (newCell->hasInfo()) {
				if (newCell->getOpen() || newCell->getClosed())
				{
					// already on one of the lists 
					onList = true;
				}
			}
			if (i>=firstDiagonal) {
				// make sure one of the adjacent sides is open.
				if (!neighborFlags[adjacent[i-4]] && !neighborFlags[adjacent[i-3]]) {
					continue;
				}
			}

			if (!validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), newCell, parentCell )) {
				continue;
			}	

			neighborFlags[i] = true;

			if (!newCell->allocateInfo(newCellCoord)) {
				// Out of cells for pathing...
 				return cellCount;
			}								
			cellCount++;

			newCostSoFar = newCell->costSoFar( parentCell );
			newCell->setBlockedByAlly(false);

			// check if this neighbor cell is already on the open (waiting to be tried) 
			// or closed (already tried) lists
			if (onList)
			{
				// already on one of the lists - if existing costSoFar is less, 
				// the new cell is on a longer path, so skip it
				if (newCell->getCostSoFar() <= newCostSoFar)
					continue;
			}

			// keep track of path we're building - point back to cell we moved here from
			newCell->setParentCell(parentCell) ;

			// store cost of this path
			newCell->setCostSoFar(newCostSoFar);
			
			Int costRemaining = 0;
			if (goalCell) {
				costRemaining = newCell->costToGoal( goalCell );
			}

			newCell->setTotalCost(newCell->getCostSoFar() + costRemaining) ;

			//DEBUG_LOG(("Cell (%d,%d), Parent cost %d, newCostSoFar %d, cost rem %d, tot %d\n", 
			//	newCell->getXIndex(), newCell->getYIndex(), 
			//	newCell->costSoFar(parentCell), newCostSoFar, costRemaining, newCell->getCostSoFar() + costRemaining));

			// if newCell was on closed list, remove it from the list
			if (newCell->getClosed())
				m_closedList = newCell->removeFromClosedList( m_closedList );

			// if the newCell was already on the open list, remove it so it can be re-inserted in order
			if (newCell->getOpen())
				m_openList.remove(newCell);

			// insert newCell in open list such that open list is sorted, smallest total path cost first
			m_openList.insert(newCell);
		}
	}

	m_isTunneling = false;
	if (goalCell->hasInfo() && !goalCell->getClosed() && !goalCell->getOpen()) {
		goalCell->releaseInfo();
	}
	cleanOpenAndClosedLists();
	return MAX_COST;
}

/**
 * Find a short, valid path between the FROM location and a location NEAR the to location.
 * Uses A* algorithm.
 */
Path *Pathfinder::findClosestPath( Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from, 
																	Coord3D *rawTo, Bool blocked, Real pathCostMultiplier, Bool moveAllies)
{
	const SearchStatScope statScope( *this, SEARCH_CLOSEST );
	//CRCDEBUG_LOG(("Pathfinder::findClosestPath()\n"));
#if defined _DEBUG
	Int startTimeMS = Engine::Clock::milliseconds();
#endif
	Bool isHuman = true;
	if (obj && obj->getControllingPlayer() && (obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER)) {
		isHuman = false; // computer gets to cheat.
	}

	if (locomotorSet.getValidSurfaces() == 0) {
		DEBUG_CRASH(("Attempting to path immobile unit."));
		return nullptr;
	}

	if (m_isMapReady == false) return nullptr;

	m_isTunneling = false;

	if (!obj) return nullptr;

	Bool canPathThroughUnits = false;
	if (obj && obj->getAIUpdateInterface()) {
		canPathThroughUnits = obj->getAIUpdateInterface()->canPathThroughUnits();
	}
	Bool centerInCell;
	Int radius;
	getRadiusAndCenter(obj, radius, centerInCell);

	Coord3D adjustTo = *rawTo;
	Coord3D *to = &adjustTo;
	if (!centerInCell) {
		adjustTo.x += PATHFIND_CELL_SIZE_F/2;
		adjustTo.y += PATHFIND_CELL_SIZE_F/2;
	}
	DEBUG_ASSERTCRASH(m_openList.isEmpty() && m_closedList == nullptr, ("Dangling lists."));
	// create unique "mark" values for open and closed cells for this pathfind invocation

	Bool isCrusher = obj ? obj->getCrusherLevel() > 0 : false;


	Coord3D clipFrom = *from;
	clip(&clipFrom, &adjustTo);

	PathfindLayerEnum destinationLayer = TheTerrainLogic->getLayerForDestination(to);
	// determine goal cell
	PathfindCell *goalCell = getClippedCell( destinationLayer,  to );
	if (goalCell == nullptr)
		return nullptr;

	if (goalCell->getZone()==0 && destinationLayer==LAYER_WALL) {
		return nullptr;
	}

	Bool goalOnObstacle = false;
	if (m_ignoreObstacleID != INVALID_ID) {
		// Check for object on structure.
		// srj sez: check for obstacle on AIRFIELD... only want to do this for things
		// that are "parked" on the airfield, but not for things hovering over an obstacle
		// (eg, a chinook over a supply dock).
		Object *goalObj = TheGameLogic->findObjectByID(m_ignoreObstacleID);
		if (goalObj) {
			PathfindCell *ignoreCell = getClippedCell(goalObj->getLayer(), goalObj->getPosition());
			if ( (goalCell->getObstacleID()==ignoreCell->getObstacleID()) && (goalCell->getObstacleID() != INVALID_ID) ) {
				Object* newObstacle = TheGameLogic->findObjectByID(goalCell->getObstacleID());
				if (newObstacle != nullptr && newObstacle->isKindOf(KINDOF_FS_AIRFIELD))
				{
					m_ignoreObstacleID = goalCell->getObstacleID();
					goalOnObstacle = true;
				}	
				else 
				{
					if (m_ignoreObstacleID == goalCell->getObstacleID()) {
						goalOnObstacle = true;
					}
				}
			}
		}
	}

	// determine start cell
	ICoord2D startCellNdx;
	worldToCell(from, &startCellNdx);
 	PathfindCell *parentCell = getClippedCell( obj->getLayer(), &clipFrom );
	if (parentCell == nullptr)
		return nullptr;

	if (validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), parentCell ) == false) {
		m_isTunneling = true; // We can't move from our current location.  So relax the constraints.
	}
	TCheckMovementInfo info;
	info.cell = startCellNdx;
	info.layer = obj->getLayer();
	info.centerInCell = centerInCell;
	info.radius = radius;
	info.considerTransient = blocked;
	info.acceptableSurfaces = locomotorSet.getValidSurfaces();
	if (!checkForMovement(obj, info) || info.enemyFixed) {
		m_isTunneling = true; // We can't move from our current location.  So relax the constraints.
	}

	Bool gotHierarchicalPath = false;
	if (m_isTunneling) {
		m_zoneManager.setAllPassable(); // can't optimize.
	}	else {
		m_zoneManager.clearPassableFlags();
		Path *hPat = findClosestHierarchicalPath(isHuman, locomotorSet, from, rawTo, false);
		if (hPat) {
			deleteInstanceOf(hPat);
			gotHierarchicalPath = true;
		}	else {
			m_zoneManager.setAllPassable();
		}
	}
	const Bool startedStuck = m_isTunneling;

	ICoord2D pos2d;
	worldToCell(to, &pos2d);
	if (!goalCell->allocateInfo(pos2d)) {
		return nullptr;
	}
	if (parentCell!=goalCell) {
		worldToCell(&clipFrom, &pos2d);
		if (!parentCell->allocateInfo(pos2d)) {
			return nullptr;
		}
	}
	parentCell->startPathfind(goalCell);

	PathfindCell *closesetCell = nullptr;
	Real closestDistanceSqr = FLT_MAX;
	Real closestDistScreenSqr = FLT_MAX;

	// initialize "open" list to contain start cell
	m_openList.initWithStartCell(parentCell);

	// "closed" list is initially empty
	m_closedList = nullptr;
	Int count = 0;
	//
	// Continue search until "open" list is empty, or
	// until goal is found.
	//
	Bool foundGoal = false;
	while( !m_openList.isEmpty() )
	{
		Real dx;
		Real dy;
		Real distSqr;
		// take head cell off of open list - it has lowest estimated total path cost
		parentCell = m_openList.getHead();
		m_openList.remove(parentCell);

		if (parentCell == goalCell)
		{
			// success - found a path to the goal
			if (!goalOnObstacle) {
				// See if the goal is a valid destination.  If not, accept closest cell.
				if (closesetCell!=nullptr && !canPathThroughUnits && !checkDestination(obj, parentCell->getXIndex(), parentCell->getYIndex(), parentCell->getLayer(), radius, centerInCell)) {
					foundGoal = true;
					// Continue processing the open list to find a possibly closer cell. jba. [8/25/2003]
					continue;
				}
			}

			Bool show = TheGlobalData->m_debugAI;
#ifdef INTENSE_DEBUG
			Int count = 0;
			PathfindCell *cur;
			for (cur = m_closedList; cur; cur=cur->getNextOpen()) {
				count++;
			}
			if (count>1000) {
				show = true;
				DEBUG_LOG(("FCP - cells %d obj %s %x\n", count, obj->getTemplate()->getName().str(), obj));
#ifdef STATE_MACHINE_DEBUG
				if( obj->getAIUpdateInterface() )
				{
					DEBUG_LOG(("State %s\n",  obj->getAIUpdateInterface()->getCurrentStateName().str()));
				}
#endif
				TheScriptEngine->AppendDebugMessage("Big path FCP", false);
			}
#endif
			if (show)
				debugShowSearch(true);
			m_isTunneling = false;
			// construct and return path
			Path *path = buildActualPath( obj, locomotorSet.getValidSurfaces(), from, goalCell, centerInCell, blocked);
			parentCell->releaseInfo();
			goalCell->releaseInfo();
			cleanOpenAndClosedLists();
			return path;
		}	
		// put parent cell onto closed list - its evaluation is finished
		m_closedList = parentCell->putOnClosedList( m_closedList );
		if (!m_isTunneling && checkDestination(obj, parentCell->getXIndex(), parentCell->getYIndex(), parentCell->getLayer(), radius, centerInCell)) {
			if (!startedStuck || validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), parentCell )) {
				dx = static_cast<Real>(IABS(goalCell->getXIndex()-parentCell->getXIndex()));
				dy = static_cast<Real>(IABS(goalCell->getYIndex()-parentCell->getYIndex()));
				distSqr = dx*dx+dy*dy;		
				if (distSqr<closestDistScreenSqr) {
					closestDistScreenSqr = distSqr;
				}
				distSqr += (parentCell->getCostSoFar()*(parentCell->getCostSoFar()*COST_TO_DISTANCE_FACTOR_SQR))*pathCostMultiplier;
				if (distSqr < closestDistanceSqr) {
					closesetCell = parentCell;
					closestDistanceSqr = distSqr;
				}
			}
		}

		dx = static_cast<Real>(IABS(goalCell->getXIndex()-parentCell->getXIndex()));
		dy = static_cast<Real>(IABS(goalCell->getYIndex()-parentCell->getYIndex()));
		distSqr = dx*dx+dy*dy;
		// If we are 2x farther than the closest location already found, don't continue.
		if (distSqr > closestDistScreenSqr*4) {
			Bool skip = false;
			if (!gotHierarchicalPath) {
				skip = true;
			}
			if (count>2000) {
				skip = true;
			}
			if (closestDistScreenSqr < 10*10*PATHFIND_CELL_SIZE_F) {
				skip = true;
			}
			if (skip) {
				continue;
			}
		}
		// If we haven't already found the goal cell, continue examining. [8/25/2003]
		if (!foundGoal) {
			// Check to see if we can change layers in this cell.
			checkChangeLayers(parentCell);
			count += examineNeighboringCells(parentCell, goalCell, locomotorSet, isHuman, centerInCell, radius, startCellNdx, obj, NO_ATTACK);
		}
	}

	if (closesetCell) {
		// success - found a path to near the goal

		Bool show = TheGlobalData->m_debugAI;

#ifdef INTENSE_DEBUG
		if (count>5000) {
			show = true;
			DEBUG_LOG(("FCP CC cells %d obj %s %x\n", count, obj->getTemplate()->getName().str(), obj));
#ifdef STATE_MACHINE_DEBUG
			if( obj->getAIUpdateInterface() )
			{
				DEBUG_LOG(("State %s\n",  obj->getAIUpdateInterface()->getCurrentStateName().str()));
			}
#endif

			DEBUG_LOG(("%d ", TheGameLogic->getFrame()));
			DEBUG_LOG(("Pathfind(findClosestPath) chugged from (%f,%f) to (%f,%f), --", from->x, from->y, to->x, to->y));
			DEBUG_LOG(("Unit '%s', time %f\n", obj->getTemplate()->getName().str(), (Engine::Clock::milliseconds()-startTimeMS)/1000.0f));
#ifdef INTENSE_DEBUG
			TheScriptEngine->AppendDebugMessage("Big path FCP CC", false);
#endif
		}
#endif
		if (show)
			debugShowSearch(true);

		m_isTunneling = false;
		rawTo->x = closesetCell->getXIndex()*PATHFIND_CELL_SIZE_F + PATHFIND_CELL_SIZE_F/2.0f;
		rawTo->y = closesetCell->getYIndex()*PATHFIND_CELL_SIZE_F + PATHFIND_CELL_SIZE_F/2.0f;
		// construct and return path
		Path *path = buildActualPath( obj, locomotorSet.getValidSurfaces(), from, closesetCell, centerInCell, blocked );
		goalCell->releaseInfo();
		cleanOpenAndClosedLists();
		return path;
	}

	// failure - goal cannot be reached
#if defined _DEBUG
	Bool valid;
	valid = validMovementPosition( isCrusher, obj->getLayer(), locomotorSet, to ) ;

	DEBUG_LOG(("%d ", TheGameLogic->getFrame()));
	DEBUG_LOG(("Pathfind(findClosestPath) failed from (%f,%f) to (%f,%f), original valid %d --", from->x, from->y, to->x, to->y, valid));
	DEBUG_LOG(("Unit '%s', time %f\n", obj->getTemplate()->getName().str(), (Engine::Clock::milliseconds()-startTimeMS)/1000.0f));
	if (TheGlobalData->m_debugAI) 
		debugShowSearch(false);
#endif
#ifdef DUMP_PERF_STATS
	TheGameLogic->incrementOverallFailedPathfinds();
#endif
	m_isTunneling = false;
	goalCell->releaseInfo();
	cleanOpenAndClosedLists();
	return nullptr;
}

void Pathfinder::adjustCoordToCell(Int cellX, Int cellY, Bool centerInCell, Coord3D &pos, PathfindLayerEnum layer)
{
	if (centerInCell) {
		pos.x = (static_cast<Real>(cellX) + 0.5f) * PATHFIND_CELL_SIZE_F;
		pos.y = (static_cast<Real>(cellY) + 0.5f) * PATHFIND_CELL_SIZE_F;
	} else {
		pos.x = static_cast<Real>((static_cast<Real>(cellX)+0.05) * PATHFIND_CELL_SIZE_F);
		pos.y = static_cast<Real>((static_cast<Real>(cellY)+0.05) * PATHFIND_CELL_SIZE_F);
	}
	pos.z = TheTerrainLogic->getLayerHeight( pos.x, pos.y, layer );
}

/**
 * Work backwards from goal cell to construct final path.
 */
Path *Pathfinder::buildActualPath( const Object *obj, LocomotorSurfaceTypeMask acceptableSurfaces, const Coord3D *fromPos, 
																	PathfindCell *goalCell, Bool center, Bool blocked )
{
	DEBUG_ASSERTCRASH( goalCell, ("Pathfinder::buildActualPath: goalCell == NULL") );

	Path *path = newInstance(Path);

	if (goalCell->getPinched() && goalCell->getParentCell() && !goalCell->getParentCell()->getPinched()) {
		goalCell = goalCell->getParentCell();
	}

	prependCells(path, fromPos, goalCell, center);

	// cleanup the path by checking line of sight
	path->optimize(obj, acceptableSurfaces, blocked);

#if defined _DEBUG
	if (TheGlobalData->m_debugAI==AI_DEBUG_PATHS) 
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
 		RGBColor color;
		color.blue = 0;
		color.red = color.green = 1;
		Coord3D pos;
		PathNode *node;
		for( node = path->getFirstNode(); node; node = node->getNext() )
		{

			// create objects to show path - they decay

			pos = *node->getPosition();
			color.red = color.green = 1;
			if (node->getLayer() != LAYER_GROUND) {
				color.red = 0;
			}
			addIcon(&pos, PATHFIND_CELL_SIZE_F*.25f, 200, color);
		}

		// show optimized path
		for( node = path->getFirstNode(); node; node = node->getNextOptimized() )
		{
			pos = *node->getPosition();
			addIcon(&pos, PATHFIND_CELL_SIZE_F*.8f, 200, color);
		}
		setDebugPath(path);
	}
#endif
	return path;
}			 

/**
 * Work backwards from goal cell to construct final path.
 */
void Pathfinder::prependCells( Path *path, const Coord3D *fromPos, 
																	PathfindCell *goalCell, Bool center )
{
	// traverse path cells in REVERSE order, creating path in desired order
	// skip the LAST node, as that will be in the same cell as the unit itself - so use the unit's position
	Coord3D pos;
	PathfindCell *cell, *prevCell = nullptr;
	Bool goalCellNull = (goalCell->getParentCell()==nullptr);
	for( cell = goalCell; cell->getParentCell(); cell = cell->getParentCell() )
	{
		m_zoneManager.setPassable(cell->getXIndex(), cell->getYIndex(), true);
		adjustCoordToCell(cell->getXIndex(), cell->getYIndex(), center, pos, cell->getLayer());
		if (prevCell && cell->getXIndex()==prevCell->getXIndex() && cell->getYIndex()==prevCell->getYIndex()) {
			// transitioning layers.
			PathfindLayerEnum layer = cell->getLayer();
			if (layer==LAYER_GROUND) {
				layer = prevCell->getLayer();
			}
			DEBUG_ASSERTCRASH(layer!=LAYER_GROUND, ("Should have at 1 non-ground layer. jba"));
			path->getFirstNode()->setLayer(layer);
			continue;
		}
		
		Bool canOptimize = true;
		if (cell->getType() == PathfindCell::CELL_CLIFF) {
			if (prevCell && prevCell->getType() != PathfindCell::CELL_CLIFF) {
				if (path->getFirstNode()) {
					path->getFirstNode()->setCanOptimize(false);
				}
			}
		}	else {
			if (prevCell && prevCell->getType() == PathfindCell::CELL_CLIFF) {
				canOptimize = false;
			}
		}

		path->prependNode( &pos, cell->getLayer() );
		path->getFirstNode()->setCanOptimize(canOptimize);
		if (cell->isBlockedByAlly()) {
			path->setBlockedByAlly(true);
		}
		if (prevCell) {
			prevCell->clearParentCell();
		}
		prevCell = cell;
	}
	m_zoneManager.setPassable(cell->getXIndex(), cell->getYIndex(), true);
	if (goalCellNull) {
		// Very short path.
		adjustCoordToCell(cell->getXIndex(), cell->getYIndex(), center, pos, cell->getLayer());
		path->prependNode( &pos, cell->getLayer() );
	}
	// put actual start position as first node on the path, so it begins right at the unit's feet
	if (fromPos->x != path->getFirstNode()->getPosition()->x || fromPos->y != path->getFirstNode()->getPosition()->y) {
		path->prependNode( fromPos, cell->getLayer() );
	}

}			 

void Pathfinder::setDebugPath(Path *newDebugpath) 
{
	if (TheGlobalData->m_debugAI)
	{
		// copy the path for debugging
		if (debugPath)
			deleteInstanceOf(debugPath);

		debugPath = newInstance(Path);
					
		for( PathNode *copyNode = newDebugpath->getFirstNode(); copyNode; copyNode = copyNode->getNextOptimized() )
			debugPath->appendNode( copyNode->getPosition(), copyNode->getLayer() );
	}

}
