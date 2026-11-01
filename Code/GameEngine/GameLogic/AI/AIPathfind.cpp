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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// AIPathfind.cpp
// AI pathfinding system
// Author: Michael S. Booth, October 2001
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameLogic/AIPathfind.h"

#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/CRCDebug.h"
#include "Common/GameState.h"
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



#ifdef INTENSE_DEBUG
#include "GameLogic/ScriptEngine.h"
#endif

#include "Common/Xfer.h"
#include "Common/XferCRC.h"

//------------------------------------------------------------------------------ Performance Timers 
#include "Common/PerfMetrics.h"
#include "Common/PerfTimer.h"
#include "Engine/Clock.h"

#include <algorithm>	// std::sort in PathfindOpenList::releaseAll

#include "AIPathfindInternal.h"

//-------------------------------------------------------------------------------------------------


//-----------------------------------------------------------------------------------
Int frameToShowObstacles;



//-----------------------------------------------------------------------------------
PathNode::PathNode() :
	m_nextOpti(0),
	m_next(0),
	m_prev(0),
	m_nextOptiDist2D(0),
	m_canOptimize(false),
	m_id(-1)
{
	m_nextOptiDirNorm2D.x = 0;
	m_nextOptiDirNorm2D.y = 0;
	m_pos.zero();
	m_layer = LAYER_INVALID;
}

//-----------------------------------------------------------------------------------
PathNode::~PathNode()
{
}
								 
//-----------------------------------------------------------------------------------
void PathNode::setNextOptimized(PathNode *node)
{
	m_nextOpti = node;
	if (node)
	{
		m_nextOptiDirNorm2D.x = node->getPosition()->x - getPosition()->x;
		m_nextOptiDirNorm2D.y = node->getPosition()->y - getPosition()->y;
		m_nextOptiDist2D = m_nextOptiDirNorm2D.length();
		if (m_nextOptiDist2D == 0.0f) 
		{
			//DEBUG_LOG(("Warning - Path Seg length == 0, adjusting. john a.\n"));
			m_nextOptiDist2D = 0.01f;
		}
		m_nextOptiDirNorm2D.x /= m_nextOptiDist2D;
		m_nextOptiDirNorm2D.y /= m_nextOptiDist2D;
	}
	else
	{
		m_nextOptiDist2D = 0;
	}
}

//-----------------------------------------------------------------------------------
/// given a list, prepend this node, return new list
PathNode *PathNode::prependToList( PathNode *list ) 
{ 
	m_next = list; 
	if (list)
		list->m_prev = this;
	m_prev = nullptr; 
	return this; 
}

//-----------------------------------------------------------------------------------
/// given a list, append this node, return new list.  slow implementation.
/// @todo optimize this
PathNode *PathNode::appendToList( PathNode *list ) 
{ 
	if (list == nullptr)
	{
		m_next = nullptr;
		m_prev = nullptr;
		return this;
	}

	PathNode *tail;
	for( tail = list; tail->m_next; tail = tail->m_next )
		;

	tail->m_next = this;
	m_prev = tail;
	m_next = nullptr;

	return list; 
}

//-----------------------------------------------------------------------------------
/// given a node, append new node to this.
void PathNode::append( PathNode *newNode ) 
{ 
	newNode->m_next = this->m_next;	
	newNode->m_prev = this;
	if (newNode->m_next) {
		newNode->m_next->m_prev = newNode;
	}
	this->m_next = newNode;

}

//-----------------------------------------------------------------------------------
/**
 * Compute direction vector to next node
 */
const Coord3D *PathNode::computeDirectionVector( void )
{
	static Coord3D dir;

	if (m_next == nullptr)
	{
		if (m_prev == nullptr)
		{
			// only one node on whole path - no direction
			dir.x = 0.0f;
			dir.y = 0.0f;
			dir.z = 0.0f;
		}
		else
		{
			// tail node - continue prior direction
			return m_prev->computeDirectionVector();
		}
	}
	else
	{
		dir.x = m_next->m_pos.x - m_pos.x;
		dir.y = m_next->m_pos.y - m_pos.y;
		dir.z = m_next->m_pos.z - m_pos.z;
	}

	return &dir;
}


//-----------------------------------------------------------------------------------
Path::Path():
m_path(nullptr),
m_pathTail(nullptr),
m_isOptimized(FALSE), 
m_blockedByAlly(FALSE),
m_cpopRecentStart(nullptr),
m_cpopCountdown(MAX_CPOP),
m_cpopValid(FALSE)
{
	m_cpopIn.zero();
	m_cpopOut.distAlongPath=0;
	m_cpopOut.layer = LAYER_GROUND;
	m_cpopOut.posOnPath.zero();
}

Path::~Path( void )
{
	PathNode *node, *nextNode;

	// delete all of the path nodes	
	for( node = m_path; node; node = nextNode )
	{
		nextNode = node->getNext();
		deleteInstanceOf(node);
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void Path::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void Path::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	PathNode *node = m_path;
	Int count = 0;
	while (node) {
		count++;
		node = node->getNext();
	}
	xfer->xferInt(&count);

	if (xfer->getXferMode() == XFER_SAVE)	{	
		node = m_pathTail;  // Write them out backwards.
		while (node) {
			node->m_id = count;
			xfer->xferInt(&count);
			Coord3D pos = *node->getPosition();
			xfer->xferCoord3D(&pos);
			PathfindLayerEnum layer = node->getLayer();
			xfer->xferUser(&layer, sizeof(layer));
			Bool canOpt = node->getCanOptimize();
			xfer->xferBool(&canOpt);
			Int id = -1;
			if (node->getNextOptimized()) {
				id = node->getNextOptimized()->m_id;
			}
			xfer->xferInt(&id);
			count--; 
			node = node->getPrevious();
		}
		DEBUG_ASSERTCRASH(count==0, ("Wrong data count"));
	} else {
		m_cpopValid = FALSE;
		while (count) {
			Int nodeId;
			xfer->xferInt(&nodeId);
			DEBUG_ASSERTCRASH(nodeId==count, ("Bad data"));
			Coord3D pos;
			xfer->xferCoord3D(&pos);
			PathfindLayerEnum layer;
			xfer->xferUser(&layer, sizeof(layer));
			Bool canOpt;
			xfer->xferBool(&canOpt);
			Int optID = -1;
			xfer->xferInt(&optID);
			PathNode *node3 = newInstance(PathNode);
			node3->m_id = nodeId;
			node3->setPosition(&pos);
			node3->setLayer(layer);
			node3->setCanOptimize(canOpt);
			PathNode *optNode = nullptr;
			if (optID > 0) {
				optNode = m_path;
				while (optNode && optNode->m_id != optID) {
					optNode = optNode->getNext();
				}
				DEBUG_ASSERTCRASH (optNode && optNode->m_id == optID, ("Could not find optimized link."));
			}
			m_path = node3->prependToList(m_path);
			if (m_pathTail == nullptr)
				m_pathTail = node3;
			if (optNode) {
				node3->setNextOptimized(optNode);
			}
			count--;
		}
	}

	xfer->xferBool(&m_isOptimized);
	Int obsolete1 = 0;
	xfer->xferInt(&obsolete1);	
	UnsignedInt obsolete2;
	xfer->xferUnsignedInt(&obsolete2);
	xfer->xferBool(&m_blockedByAlly);


#if defined _DEBUG
	if (TheGlobalData->m_debugAI == AI_DEBUG_PATHS)
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
 		RGBColor color;
		color.blue = 0;
		color.red = color.green = 1;
		Coord3D pos;
		addIcon(nullptr, 0, 0, color); // erase feedback.
		for( PathNode *node2 = getFirstNode(); node2; node2 = node2->getNext() )
		{

			// create objects to show path - they decay

			pos = *node2->getPosition();
			addIcon(&pos, PATHFIND_CELL_SIZE_F*.25f, 200, color);
		}

		// show optimized path
		for( node = getFirstNode(); node; node = node->getNextOptimized() )
		{
			pos = *node->getPosition();
			addIcon(&pos, PATHFIND_CELL_SIZE_F*.8f, 200, color);
		}
		TheAI->pathfinder()->setDebugPath(this);
	}
#endif
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void Path::loadPostProcess( void )
{
}  // end loadPostProcess

/**
 * Create a new node at the head of the path
 */
void Path::prependNode( const Coord3D *pos, PathfindLayerEnum layer )
{
	PathNode *node = newInstance(PathNode);

	node->setPosition( pos );
	node->setLayer(layer);

	m_path = node->prependToList( m_path );

	if (m_pathTail == nullptr)
		m_pathTail = node;

	m_isOptimized = false;

#ifdef CPOP_STARTS_FROM_PREV_SEG
	m_cpopRecentStart = nullptr;
#endif
}

/**
 * Create a new node at the tail of the path
 */
void Path::appendNode( const Coord3D *pos, PathfindLayerEnum layer )
{
	if (m_isOptimized && m_pathTail) 
	{
		/* Check for duplicates. */
		if (pos->x == m_pathTail->getPosition()->x && pos->y == m_pathTail->getPosition()->y) {
			DEBUG_LOG(("Warning - Path Seg length == 0, ignoring. john a.\n"));
			return;
		}
	}
	PathNode *node = newInstance(PathNode);

	node->setPosition( pos );
	node->setLayer(layer);

	m_path = node->appendToList( m_path );

	if (m_isOptimized && m_pathTail) 
	{
		m_pathTail->setNextOptimized(node);
	}

	m_pathTail = node;

#ifdef CPOP_STARTS_FROM_PREV_SEG
	m_cpopRecentStart = nullptr;
#endif
}
/**
 * Create a new node at the tail of the path
 */
void Path::updateLastNode( const Coord3D *pos )
{
	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(pos);
	if (m_pathTail) {
		m_pathTail->setPosition(pos);
		m_pathTail->setLayer(layer);
	}
	if (m_isOptimized && m_pathTail) 
	{
		PathNode *node = m_path;
		while(node && node->getNextOptimized() != m_pathTail) {
			node = node->getNextOptimized();
		}
		if (node && node->getNextOptimized() == m_pathTail) {
			node->setNextOptimized(m_pathTail);
		}
	}
}

/**
 * Optimize the path by checking line of sight
 */
void Path::optimize( const Object *obj, LocomotorSurfaceTypeMask acceptableSurfaces, Bool blocked )
{
	PathNode *node, *anchor;

	// start with first node in the path
	anchor = getFirstNode();

	Bool firstNode = true;
	PathfindLayerEnum firstLayer = anchor->getLayer();

	// backwards.

	//
	// For each node in the path, check LOS from last node in path, working forward.
	// When a clear LOS is found, keep the resulting straight line segment.
	//
	while( anchor != getLastNode() )
	{
		// find the farthest node in the path that has a clear line-of-sight to this anchor
		Bool optimizedSegment = false;
		PathfindLayerEnum layer = anchor->getLayer();
		PathfindLayerEnum curLayer = anchor->getLayer();
		Int count = 0;
		const Int ALLOWED_STEPS = 3; // we can optimize 3 steps to or from a bridge.  Otherwise, we need to insert a point. jba.
		for (node = anchor->getNext(); node->getNext(); node=node->getNext()) {
			count++;
			if (curLayer==LAYER_GROUND) {
				if (node->getLayer() != curLayer) {
					layer = node->getLayer();
					curLayer = layer;
					if (count > ALLOWED_STEPS) break;
				}
			}	else {
				if (node->getNext()->getLayer() != curLayer) {
					if (count > ALLOWED_STEPS) break;
				}
			}
			curLayer = node->getLayer();
			if (node->getCanOptimize()==false) {
				break;
			}
		}
		if (firstNode) {
			layer = firstLayer;
			firstNode = false;
		}
		//PathfindLayerEnum curLayer = LAYER_GROUND;
		for( ; node != anchor; node = node->getPrevious() )		
		{
			Bool isPassable = false;
			//CRCDEBUG_LOG(("Path::optimize() calling isLinePassable()\n"));
			if (TheAI->pathfinder()->isLinePassable( obj, acceptableSurfaces, layer, *anchor->getPosition(), 
				*node->getPosition(), blocked, false))
			{
				isPassable = true;
			} 
			PathfindCell* cell = TheAI->pathfinder()->getCell( layer, node->getPosition());
			if (cell && cell->getType()==PathfindCell::CELL_CLIFF && !cell->getPinched()) {
				isPassable = true;
			}
			// Horizontal, diagonal, and vertical steps are passable.
			if (!isPassable) {
				Int dx = static_cast<Int>(node->getPosition()->x - anchor->getPosition()->x);
				Int dy = static_cast<Int>(node->getPosition()->y - anchor->getPosition()->y);
				Bool mightBePassable = false;
				if (IABS(dx)==PATHFIND_CELL_SIZE && IABS(dy)==PATHFIND_CELL_SIZE) {
					isPassable = true;
				}
				PathNode *tmpNode;
				if (dx==0) {
					mightBePassable = true;
					for (tmpNode = node->getPrevious(); tmpNode && tmpNode != anchor; tmpNode = tmpNode->getPrevious()) {
						dx = static_cast<Int>(tmpNode->getNext()->getPosition()->x - tmpNode->getPosition()->x);
						if (dx!=0) mightBePassable = false;
					}
				}
				if (dy==0) {
					mightBePassable = true;
					for (tmpNode = node->getPrevious(); tmpNode && tmpNode != anchor; tmpNode = tmpNode->getPrevious()) {
						dy = static_cast<Int>(tmpNode->getNext()->getPosition()->y - tmpNode->getPosition()->y);
						if (dy!=0) mightBePassable = false;
					}
				}
				if (dx == dy) {
					mightBePassable = true;
					for (tmpNode = node->getPrevious(); tmpNode &&   tmpNode != anchor; tmpNode = tmpNode->getPrevious()) {
						dx = static_cast<Int>(tmpNode->getNext()->getPosition()->x - tmpNode->getPosition()->x);
						dy = static_cast<Int>(tmpNode->getNext()->getPosition()->y - tmpNode->getPosition()->y);
						if (dy!=dx) mightBePassable = false;
					}
				}
				if (dx == -dy) {
					mightBePassable = true;
					for (tmpNode = node->getPrevious(); tmpNode &&   tmpNode != anchor; tmpNode = tmpNode->getPrevious()) {
						dx = static_cast<Int>(tmpNode->getNext()->getPosition()->x - tmpNode->getPosition()->x);
						dy = static_cast<Int>(tmpNode->getNext()->getPosition()->y - tmpNode->getPosition()->y);
						if (dy!=-dx) mightBePassable = false;
					}
				}
				if (mightBePassable) {
					isPassable = true;
				}
			}
			if (isPassable) 
			{
				// anchor can directly see this node, make it next in the optimized path
				anchor->setNextOptimized( node );
				anchor = node;
				optimizedSegment = true;
				break;
			}
		}
			
		if (optimizedSegment == false)
		{
			// for some reason, there is no clear LOS between the anchor node and the very next node
			anchor->setNextOptimized( anchor->getNext() );
			anchor = anchor->getNext();
		}
	}

	// the path has been optimized
	m_isOptimized = true;
}

/**
 * Optimize the path by checking line of sight
 */
void Path::optimizeGroundPath( Bool crusher, Int pathDiameter )
{
	PathNode *node, *anchor;

	// start with first node in the path
	anchor = getFirstNode();

	//
	// For each node in the path, check LOS from last node in path, working forward.
	// When a clear LOS is found, keep the resulting straight line segment.
	//
	while( anchor != getLastNode() )
	{
		// find the farthest node in the path that has a clear line-of-sight to this anchor
		Bool optimizedSegment = false;
		PathfindLayerEnum layer = anchor->getLayer();
		PathfindLayerEnum curLayer = anchor->getLayer();
		Int count = 0;
		const Int ALLOWED_STEPS = 3; // we can optimize 3 steps to or from a bridge.  Otherwise, we need to insert a point. jba.
		for (node = anchor->getNext(); node->getNext(); node=node->getNext()) {
			count++;
			if (curLayer==LAYER_GROUND) {
				if (node->getLayer() != curLayer) {
					layer = node->getLayer();
					curLayer = layer;
					if (count > ALLOWED_STEPS) break;
				}
			}	else {
				if (node->getNext()->getLayer() != curLayer) {
					if (count > ALLOWED_STEPS) break;
				}
			}
			curLayer = node->getLayer();
		}

		// find the farthest node in the path that has a clear line-of-sight to this anchor
		for( ; node != anchor; node = node->getPrevious() )		
		{
			Bool isPassable = false;
			//CRCDEBUG_LOG(("Path::optimize() calling isLinePassable()\n"));
			if (TheAI->pathfinder()->isGroundPathPassable( crusher, *anchor->getPosition(), layer,
				*node->getPosition(), pathDiameter))
			{
				isPassable = true;
			} 
			// Horizontal, diagonal, and vertical steps are passable.
			if (!isPassable) {
				Int dx = static_cast<Int>(node->getPosition()->x - anchor->getPosition()->x);
				Int dy = static_cast<Int>(node->getPosition()->y - anchor->getPosition()->y);
				Bool mightBePassable = false;
				PathNode *tmpNode;
				if (dx==0) {
					mightBePassable = true;
					for (tmpNode = node->getPrevious(); tmpNode && tmpNode != anchor; tmpNode = tmpNode->getPrevious()) {
						dx = static_cast<Int>(tmpNode->getNext()->getPosition()->x - tmpNode->getPosition()->x);
						if (dx!=0) mightBePassable = false;
					}
				}
				if (dy==0) {
					mightBePassable = true;
					for (tmpNode = node->getPrevious(); tmpNode && tmpNode != anchor; tmpNode = tmpNode->getPrevious()) {
						dy = static_cast<Int>(tmpNode->getNext()->getPosition()->y - tmpNode->getPosition()->y);
						if (dy!=0) mightBePassable = false;
					}
				}
				if (dx == dy) {
					mightBePassable = true;
					for (tmpNode = node->getPrevious(); tmpNode &&   tmpNode != anchor; tmpNode = tmpNode->getPrevious()) {
						dx = static_cast<Int>(tmpNode->getNext()->getPosition()->x - tmpNode->getPosition()->x);
						dy = static_cast<Int>(tmpNode->getNext()->getPosition()->y - tmpNode->getPosition()->y);
						if (dy!=dx) mightBePassable = false;
					}
				}
				if (dx == -dy) {
					mightBePassable = true;
					for (tmpNode = node->getPrevious(); tmpNode &&   tmpNode != anchor; tmpNode = tmpNode->getPrevious()) {
						dx = static_cast<Int>(tmpNode->getNext()->getPosition()->x - tmpNode->getPosition()->x);
						dy = static_cast<Int>(tmpNode->getNext()->getPosition()->y - tmpNode->getPosition()->y);
						if (dy!=-dx) mightBePassable = false;
					}
				}
				if (mightBePassable) {
					isPassable = true;
				}
			}
			if (isPassable) 
			{
				// anchor can directly see this node, make it next in the optimized path
				anchor->setNextOptimized( node );
				anchor = node;
				optimizedSegment = true;
				break;
			}
		}
			
		if (optimizedSegment == false)
		{
			// for some reason, there is no clear LOS between the anchor node and the very next node
			anchor->setNextOptimized( anchor->getNext() );
			anchor = anchor->getNext();
		}
	}

	// Remove jig/jogs :) jba.
	for (anchor=getFirstNode(); anchor!=nullptr; anchor=anchor->getNextOptimized()) {
		node = anchor->getNextOptimized();
		if (node && node->getNextOptimized()) {
			Real dx = node->getPosition()->x - anchor->getPosition()->x;
			Real dy = node->getPosition()->y - anchor->getPosition()->y;
			// If the x & y offsets are less than 2 pathfind cells, kill it.
			if (dx*dx+dy*dy < sqr(PATHFIND_CELL_SIZE_F)*3.9f) {
				anchor->setNextOptimized(node->getNextOptimized());
			}
		}
	}

	// the path has been optimized
	m_isOptimized = true;
}

inline Bool isReallyClose(const Coord3D& a, const Coord3D& b)
{
	const Real CLOSE_ENOUGH = 0.1f;
	return 
		fabs(a.x-b.x) <= CLOSE_ENOUGH &&
		fabs(a.y-b.y) <= CLOSE_ENOUGH &&
		fabs(a.z-b.z) <= CLOSE_ENOUGH;
}

/**
 * Given a location, return the closest position on the path.
 * If 'allowBacktrack' is true, the entire path is considered.
 * If it is false, the point computed cannot be prior to previously returned non-backtracking points on this path.
 * Because the path "knows" the direction of travel, it will "lead" the given position a bit
 * to ensure the path is followed in the inteded direction.
 *
 * Note: The path cleanup does not take into account rolling terrain, so we can end up with
 * these situations:
 *
 *           B
 *        ######
 *      ##########
 *  A-##----------##---C
 * #######################
 *
 *
 * When an agent gets to B, he seems far off of the path, but it really not.
 * There are similar problems with valleys.
 *
 * Since agents track the closest path, if a high hill gets close to the underside of
 * a bridge, an agent may 'jump' to the higher path.  This must be avoided in maps.
 *
 * return along-path distance to the end will be returned as function result
 */
void Path::computePointOnPath(
	const Object* obj,
	const LocomotorSet& locomotorSet,
	const Coord3D& pos,
	ClosestPointOnPathInfo& out
)
{
	CRCDEBUG_LOG(("Path::computePointOnPath() fzor %s\n", DescribeObject(obj).str()));

	out.layer = LAYER_GROUND;
	out.posOnPath.zero();
	out.distAlongPath = 0;

	if (m_path == nullptr)
	{
		m_cpopValid = false;
		return;
	}
	out.layer = m_path->getLayer();

	if (m_cpopValid && m_cpopCountdown>0 && isReallyClose(pos, m_cpopIn))
	{
		out = m_cpopOut;
		m_cpopCountdown--;
		CRCDEBUG_LOG(("Path::computePointOnPath() end because we're really close\n"));
		return;
	}
	m_cpopCountdown = MAX_CPOP;

	// default pathPos to end of the path
	out.posOnPath = *getLastNode()->getPosition();

	const PathNode* closeNode = nullptr;
	Coord2D toPos;
	Real closeDistSqr = 99999999.9f;
	Real totalPathLength = 0.0f;
	Real lengthAlongPathToPos = 0.0f;

	//
	// Find the closest segment of the path
	//
#ifdef CPOP_STARTS_FROM_PREV_SEG
	const PathNode* prevNode = m_cpopRecentStart;
	if (prevNode == nullptr)
		prevNode = m_path;
#else
	const PathNode* prevNode = m_path;
#endif
	Coord2D segmentDirNorm;
	Real segmentLength;

	// note that the seg dir and len returned by this is the dist & vec from 'prevNode' to 'node'
	for ( const PathNode* node = prevNode->getNextOptimized(&segmentDirNorm, &segmentLength); 
				node != nullptr; 
				node = node->getNextOptimized(&segmentDirNorm, &segmentLength) )
	{
		const Coord3D* prevNodePos = prevNode->getPosition();
		const Coord3D* nodePos = node->getPosition();

		// compute vector from start of segment to pos
		toPos.x = pos.x - prevNodePos->x;
		toPos.y = pos.y - prevNodePos->y;

		// compute distance projection of 'toPos' onto segment
		Real alongPathDist = segmentDirNorm.x * toPos.x + segmentDirNorm.y * toPos.y;
				
		Coord3D pointOnPath;
		if (alongPathDist < 0.0f)
		{
			// projected point is before start of segment, use starting point
			alongPathDist = 0.0f;
			pointOnPath = *prevNodePos;
		}
		else if (alongPathDist > segmentLength)
		{
			// projected point is beyond end of segment, use end point
			if (node->getNextOptimized() == nullptr)
			{
				alongPathDist = segmentLength;
				pointOnPath = *nodePos;
			}
			else
			{
				// beyond the end of this segment, skip this segment
				// if bend is sharp, start of next segment will grab this point
				// if bend is gradual, the point will project into the next segment
				totalPathLength += segmentLength;
				prevNode = node;
				continue;
			}
		}
		else
		{
			// projected point is on this segment, compute it
			pointOnPath.x = prevNodePos->x + alongPathDist * segmentDirNorm.x;
			pointOnPath.y = prevNodePos->y + alongPathDist * segmentDirNorm.y;
			pointOnPath.z = 0;
		}

		// compute distance to point on path, and track the closest we've found so far
		Coord2D offset;
		offset.x = pos.x - pointOnPath.x;
		offset.y = pos.y - pointOnPath.y;

		Real offsetDistSqr = offset.x*offset.x + offset.y*offset.y;
		if (offsetDistSqr < closeDistSqr)
		{
			closeDistSqr = offsetDistSqr;
			closeNode = prevNode;
			out.posOnPath = pointOnPath;

			lengthAlongPathToPos = totalPathLength + alongPathDist;
		}

		// add this segment's length to find total path length
		/// @todo Precompute this and store in path
		totalPathLength += segmentLength;
		prevNode = node;
		DUMPCOORD3D(&pointOnPath);
	}

#ifdef CPOP_STARTS_FROM_PREV_SEG
	m_cpopRecentStart = closeNode;
#endif

	//
	// Compute the goal movement position for this agent
	//
	if (closeNode && closeNode->getNextOptimized())
	{
		// note that the seg dir and len returned by this is the dist & vec from 'closeNode' to 'closeNext'
		const PathNode* closeNext = closeNode->getNextOptimized(&segmentDirNorm, &segmentLength);
		const Coord3D* nextNodePos = closeNext->getPosition();
		const Coord3D* closeNodePos = closeNode->getPosition();
		
		const PathNode* closePrev = closeNode->getPrevious();
		if (closePrev && closePrev->getLayer() > LAYER_GROUND) 
		{
			out.layer = closeNode->getLayer();
		}
		if (closeNode->getLayer() > LAYER_GROUND) 
		{
			out.layer = closeNode->getLayer();
		}

		if (closeNext->getLayer() > LAYER_GROUND) 
		{
			out.layer = closeNext->getLayer();
		}

		// compute vector from start of segment to pos
		toPos.x = pos.x - closeNodePos->x;
		toPos.y = pos.y - closeNodePos->y;

		// compute distance projection of 'toPos' onto segment
		Real alongPathDist = segmentDirNorm.x * toPos.x + segmentDirNorm.y * toPos.y;

		// we know this is the closest segment, so don't allow farther back than the start node
		if (alongPathDist < 0.0f)
			alongPathDist = 0.0f;

		// compute distance of point from this path segment
		Real toDistSqr = sqr(toPos.x) + sqr(toPos.y);
		Real offsetDistSq = toDistSqr - sqr(alongPathDist);
		Real offsetDist = static_cast<Real>((offsetDistSq <= 0.0) ? 0.0 : sqrt(offsetDistSq));

		// If we are basically on the path, return the next path node as the movement goal.
		// However, the farther off the path we get, the movement goal becomes closer to our
		// projected position on the path.  If we are very far off the path, we will move 
		// directly towards the nearest point on the path, and not the next path node.
		const Real maxPathError = 3.0f * PATHFIND_CELL_SIZE_F;
		const Real maxPathErrorInv = static_cast<Real>(1.0 / maxPathError);
		Real k = offsetDist * maxPathErrorInv;
		if (k > 1.0f)
			k = 1.0f;

		Bool gotPos = false;
		CRCDEBUG_LOG(("Path::computePointOnPath() calling isLinePassable() 1\n"));
		if (TheAI->pathfinder()->isLinePassable( obj, locomotorSet.getValidSurfaces(), out.layer, pos, *nextNodePos, 
			false, true )) 
		{
			out.posOnPath = *nextNodePos;
			gotPos = true;

			Bool tryAhead = alongPathDist > segmentLength * 0.5;
			if (closeNext->getCanOptimize() == false) 
			{
				tryAhead = false; // don't go past no-opt nodes.
			}
			if (closeNode->getLayer() != closeNext->getLayer()) 
			{
				tryAhead = false; // don't go past layers.
			}
			if (obj->getLayer()!=LAYER_GROUND) {
				tryAhead = false;
			}
			Bool veryClose = false;
			if (segmentLength-alongPathDist<1.0f) {
				tryAhead = true;
				veryClose = true;
			}
			if (tryAhead) 
			{
				// try next segment middle.	
				const PathNode *next = closeNext->getNextOptimized();
				if (next) 
				{
					Coord3D tryPos;
					tryPos.x = static_cast<Real>((nextNodePos->x + next->getPosition()->x) * 0.5);
					tryPos.y = static_cast<Real>((nextNodePos->y + next->getPosition()->y) * 0.5);
					tryPos.z = nextNodePos->z;
					CRCDEBUG_LOG(("Path::computePointOnPath() calling isLinePassable() 2\n"));
					if (veryClose || TheAI->pathfinder()->isLinePassable( obj, locomotorSet.getValidSurfaces(), closeNext->getLayer(), pos, tryPos, false, true )) 
					{
						gotPos = true;
						out.posOnPath = tryPos;
					}
				}
			}
		} 
		else if (k > 0.5f) 
		{
			Real tryDist = static_cast<Real>(alongPathDist + (0.5) * (segmentLength - alongPathDist));

			// projected point is on this segment, compute it
			out.posOnPath.x = closeNodePos->x + tryDist * segmentDirNorm.x;
			out.posOnPath.y = closeNodePos->y + tryDist * segmentDirNorm.y;
			out.posOnPath.z = closeNodePos->z;

			CRCDEBUG_LOG(("Path::computePointOnPath() calling isLinePassable() 3\n"));
			if (TheAI->pathfinder()->isLinePassable( obj, locomotorSet.getValidSurfaces(), out.layer, pos, out.posOnPath, false, true )) 
			{
				k = 0.5f;
				gotPos = true;
			}
		}	

		// if we are on the path (k == 0), then alongPathDist == segmentLength
		// if we are way off the path (k == 1), then alongPathDist is unchanged, and it projection of actual pos
		alongPathDist += (1.0f - k) * (segmentLength - alongPathDist);

		if (!gotPos) 
		{
			if (alongPathDist > segmentLength)
			{
				alongPathDist = segmentLength;
				out.posOnPath = *nextNodePos;
			}
			else
			{
				// projected point is on this segment, compute it
				out.posOnPath.x = closeNodePos->x + alongPathDist * segmentDirNorm.x;
				out.posOnPath.y = closeNodePos->y + alongPathDist * segmentDirNorm.y;
				out.posOnPath.z = closeNodePos->z;
				Real dx = fabs(pos.x - out.posOnPath.x);
				Real dy = fabs(pos.y - out.posOnPath.y);
				if (dx<1 && dy<1 && closeNode->getNextOptimized() && closeNode->getNextOptimized()->getNextOptimized()) {
					out.posOnPath = *closeNode->getNextOptimized()->getNextOptimized()->getPosition();
				}
			}
		}
	}

	TheAI->pathfinder()->setDebugPathPosition( &out.posOnPath );

	out.distAlongPath = totalPathLength - lengthAlongPathToPos;

	Coord3D delta;
	delta.x = out.posOnPath.x - pos.x;
	delta.y = out.posOnPath.y - pos.y;
	delta.z = 0;
	Real lenDelta = delta.length();
	if (lenDelta > out.distAlongPath && out.distAlongPath > PATHFIND_CLOSE_ENOUGH) 
	{
		out.distAlongPath = lenDelta;
	}

	m_cpopIn = pos;
	m_cpopOut = out;
	m_cpopValid = true;
	CRCDEBUG_LOG(("Path::computePointOnPath() end\n"));

}


/**
	Given a position, computes the distance to the goal.  Returns 0 if we are past the goal.
	Returns the goal position in goalPos.  This is intended for use with flying paths, that go
	directly to the goal and don't consider obstacles.  jba.
 */
Real Path::computeFlightDistToGoal( const Coord3D *pos, Coord3D& goalPos )
{
	if (m_path == nullptr)
	{
		goalPos.x = 0.0f;
		goalPos.y = 0.0f;
		goalPos.z = 0.0f;
		return 0.0f;
	}
	const PathNode *curNode = getFirstNode();
	if (m_cpopRecentStart) {
		curNode = m_cpopRecentStart;
	} else {
		m_cpopRecentStart = curNode;
	}
	const PathNode *nextNode = curNode->getNextOptimized();
	goalPos = *curNode->getPosition();
	Real distance = 0;
	Bool useNext = true;
	while (nextNode) {

		if (useNext) {
			goalPos = *nextNode->getPosition();
		}

		Coord3D startPos = *curNode->getPosition();
		Coord3D endPos = *nextNode->getPosition();

		Coord2D posToGoalVector;
		// posToGoalVector is pos to goalPos vector.
		posToGoalVector.x = endPos.x - pos->x;
		posToGoalVector.y = endPos.y - pos->y;

		// pathVector is the startPos to goal pos vector.
		Coord2D pathVector;
		pathVector.x = endPos.x - startPos.x;
		pathVector.y = endPos.y - startPos.y;

		// Normalize pathVector
		pathVector.normalize();

		// Dot product is the posToGoal vector projected onto the path vector.
		Real dotProduct = posToGoalVector.x*pathVector.x	+ posToGoalVector.y*pathVector.y;
		if (dotProduct>=0) {
			distance += dotProduct;
			useNext = false;
		}	else if (useNext) {
			m_cpopRecentStart = nextNode;
		}
		curNode = nextNode;
		nextNode = curNode->getNextOptimized();
	}
	return distance;

}
//-----------------------------------------------------------------------------------

enum {CELL_INFOS_TO_ALLOCATE = 30000};
PathfindCellInfo *PathfindCellInfo::s_infoArray = nullptr;
PathfindCellInfo *PathfindCellInfo::s_firstFree = nullptr;						
/**
 * Allocates a pool of pathfind cell infos.
 */
void PathfindCellInfo::allocateCellInfos(void) 
{
	releaseCellInfos();
	s_infoArray = MSGNEW("PathfindCellInfo") PathfindCellInfo[CELL_INFOS_TO_ALLOCATE];	// pool[]ify
	s_infoArray[CELL_INFOS_TO_ALLOCATE-1].m_pathParent = nullptr;
	s_infoArray[CELL_INFOS_TO_ALLOCATE-1].m_isFree = true;
	s_firstFree = s_infoArray;
	s_statInfoFree = CELL_INFOS_TO_ALLOCATE;
	for (Int i=0; i<CELL_INFOS_TO_ALLOCATE-1; i++) {
		s_infoArray[i].m_pathParent = &s_infoArray[i+1];
		s_infoArray[i].m_isFree = true; 
	}
}

/**
 * Releases a pool of pathfind cell infos.
 */
void PathfindCellInfo::releaseCellInfos(void) 
{
	if (s_infoArray==nullptr) {
		return; // haven't allocated any yet.
	}
	Int count=0;
	while (s_firstFree) {
		count++;
		DEBUG_ASSERTCRASH(s_firstFree->m_isFree, ("Should be freed."));
		s_firstFree = s_firstFree->m_pathParent;
	}
	DEBUG_ASSERTCRASH(count==CELL_INFOS_TO_ALLOCATE, ("Error - Allocated cellinfos."));
	delete[] s_infoArray;
	s_infoArray = nullptr;
	s_firstFree = nullptr;
	s_statInfoFree = 0;
}

/**
 * Gets a pathfindcellinfo.
 */
UnsignedInt s_statInfoExhausted = 0;
UnsignedInt s_statInfoFree = 0;

PathfindCellInfo *PathfindCellInfo::getACellInfo(PathfindCell *cell,const ICoord2D &pos) 
{
	PathfindCellInfo *info = s_firstFree;
	if (!info)
		++s_statInfoExhausted;
	if (s_firstFree) {
		DEBUG_ASSERTCRASH(s_firstFree->m_isFree, ("Should be freed."));
		s_firstFree = s_firstFree->m_pathParent;
		--s_statInfoFree;
		info->m_isFree = false;  // Just allocated it.
		info->m_cell = cell;
		info->m_pos = pos;

		info->m_nextOpen = nullptr;
		info->m_prevOpen = nullptr;
		info->m_pathParent = nullptr;
		info->m_costSoFar = 0;		
		info->m_totalCost = 0;
		info->m_open = 0;
		info->m_closed = 0;
		info->m_heapIndex = 0;
		info->m_lineMemo = 0;
		info->m_obstacleID = INVALID_ID;
		info->m_goalUnitID = INVALID_ID;
		info->m_posUnitID = INVALID_ID;
		info->m_goalAircraftID = INVALID_ID;
		info->m_obstacleIsFence = false;
		info->m_obstacleIsTransparent = false;
		info->m_blockedByAlly = false;
	}
	return info;
}

/**
 * Returns a pathfindcellinfo.
 */
void PathfindCellInfo::releaseACellInfo(PathfindCellInfo *theInfo) 
{
	DEBUG_ASSERTCRASH(!theInfo->m_isFree, ("Shouldn't be free."));
	//@ todo -fix this assert on usa04.  jba.
	//DEBUG_ASSERTCRASH(theInfo->m_obstacleID==0, ("Shouldn't be obstacle."));
	theInfo->m_pathParent = s_firstFree;
	s_firstFree = theInfo;
	s_firstFree->m_isFree = true;
	++s_statInfoFree;
}

//-----------------------------------------------------------------------------------

/**
 * Constructor
 */
PathfindCell::PathfindCell( void ) :m_info(nullptr)
{ 
	reset();
}

/**
 * Destructor
 */
PathfindCell::~PathfindCell( void ) 
{ 	
	if (m_info) PathfindCellInfo::releaseACellInfo(m_info);
	m_info = nullptr;
	static Bool warn = true;
	if (warn) {
		warn = false;
		DEBUG_LOG( ("PathfindCell::~PathfindCell m_info Allocated."));
	}
}

/**
 * Reset the cell to default values
 */
void PathfindCell::reset( ) 
{ 
	m_type = PathfindCell::CELL_CLEAR; 
	m_flags = PathfindCell::NO_UNITS;
	m_zone = 0;
	m_aircraftGoal = false;
	m_pinched = false;
	if (m_info) {
		m_info->m_obstacleID = INVALID_ID;
		PathfindCellInfo::releaseACellInfo(m_info);
		m_info = nullptr;
	}
	m_connectsToLayer = LAYER_INVALID;
	m_layer = LAYER_GROUND;
	
}

/**
 * Reset the pathfinding values in the cell.
 */
Bool PathfindCell::startPathfind( PathfindCell *goalCell  ) 
{ 
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	m_info->m_nextOpen = nullptr;
	m_info->m_prevOpen = nullptr;
	m_info->m_pathParent = nullptr;
	m_info->m_costSoFar = 0;		// start node, no cost to get here
	m_info->m_totalCost = 0;
	if (goalCell) {
		m_info->m_totalCost = static_cast<UnsignedShort>(costToGoal( goalCell ));
	}
	m_info->m_open = TRUE;
	m_info->m_closed = FALSE;
	return true;
}
/**
 * Set the parent pointer.
 */
void PathfindCell::setParentCell( PathfindCell* parent  ) 
{ 
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	m_info->m_pathParent = parent->m_info;
	Int dx = m_info->m_pos.x - parent->m_info->m_pos.x;
	Int dy = m_info->m_pos.y - parent->m_info->m_pos.y;
	if (dx<-1 || dx>1 || dy<-1 || dy>1) {
		DEBUG_CRASH(("Invalid parent index."));
	}
}

/**
 * Set the parent pointer.
 */
void PathfindCell::setParentCellHierarchical( PathfindCell* parent  ) 
{ 
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	m_info->m_pathParent = parent->m_info;
}

/**
 * Reset the parent cell.
 */
void PathfindCell::clearParentCell( void  ) 
{ 
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	m_info->m_pathParent = nullptr;
}


/**
 * Allocates an info record for a cell.
 */
Bool PathfindCell::allocateInfo( const ICoord2D &pos ) 
{ 
	if (!m_info) {
		m_info = PathfindCellInfo::getACellInfo(this, pos);
		return (m_info != nullptr);
	} 
	return true;
}

/**
 * Releases an info record for a cell.
 */
void PathfindCell::releaseInfo( void ) 
{ 
	if (m_type==PathfindCell::CELL_OBSTACLE) {
		return;
	}
	if (m_flags!=NO_UNITS) {
		return;
	}
	if (m_aircraftGoal) {
		return;
	}

	if (m_info) {
		DEBUG_ASSERTCRASH(m_info->m_prevOpen==nullptr && m_info->m_nextOpen==nullptr, ("Shouldn't be linked."));
		DEBUG_ASSERTCRASH(m_info->m_open==NULL && m_info->m_closed==NULL, ("Shouldn't be linked."));
		DEBUG_ASSERTCRASH(m_info->m_goalUnitID==INVALID_ID && m_info->m_posUnitID==INVALID_ID, ("Shouldn't be occupied."));
		DEBUG_ASSERTCRASH(m_info->m_goalAircraftID==INVALID_ID , ("Shouldn't be occupied by aircraft."));
		if (m_info->m_prevOpen || m_info->m_nextOpen || m_info->m_open || m_info->m_closed) {
			// Bad release.  Skip for now, better leak than crash.  jba.
			return;
		}
		PathfindCellInfo::releaseACellInfo(m_info);
		m_info = nullptr;
	}
}

/**
 * Sets the goal unit into the info record for a cell.
 */
void PathfindCell::setGoalUnit(ObjectID unitID, const ICoord2D &pos ) 
{ 
	if (unitID==INVALID_ID) {
		// removing goal.
		if (m_info) {
			m_info->m_goalUnitID = INVALID_ID;
			if (m_info->m_posUnitID == INVALID_ID) {
				// No units here.
				DEBUG_ASSERTCRASH(m_flags==UNIT_GOAL, ("Bad flags."));
				m_flags = NO_UNITS;
				releaseInfo();
			} else{
				m_flags = UNIT_PRESENT_MOVING;
			}
		}	else {
			DEBUG_ASSERTCRASH(m_flags == NO_UNITS, ("Bad flags."));
		}
	} else {
		// adding goal.
		if (!m_info) {
			DEBUG_ASSERTCRASH(m_flags == NO_UNITS, ("Bad flags."));
			allocateInfo(pos);
		}
		if (!m_info) {
			DEBUG_CRASH(("Ran out of pathfind cells - fatal error!!!!! jba. "));
			return;
		}
		m_info->m_goalUnitID = unitID;
		if (unitID==m_info->m_posUnitID) {
			m_flags = UNIT_PRESENT_FIXED;
		} else if (m_info->m_posUnitID==INVALID_ID) {
			m_flags = UNIT_GOAL;
		}	else {
			m_flags = UNIT_GOAL_OTHER_MOVING;
		}
	}
}


//-----------------------------------------------------------------------------
void PathfindCell::friend_setUnitMarks(ObjectID goalUnit, ObjectID posUnit, ObjectID goalAircraft, Int flags, const ICoord2D &pos)
{
	const Bool marked = goalUnit != INVALID_ID || posUnit != INVALID_ID || goalAircraft != INVALID_ID || flags != NO_UNITS;
	if (marked && !allocateInfo(pos)) {
		DEBUG_CRASH(("Ran out of pathfind cells while loading."));
		return;
	}
	if (m_info) {
		m_info->m_goalUnitID = goalUnit;
		m_info->m_posUnitID = posUnit;
		m_info->m_goalAircraftID = goalAircraft;
	}
	m_flags = flags;
	m_aircraftGoal = goalAircraft != INVALID_ID;
	if (!marked)
		releaseInfo();
}

/**
 * Sets the goal aircraft into the info record for a cell.
 */
void PathfindCell::setGoalAircraft(ObjectID unitID, const ICoord2D &pos ) 
{ 
	if (unitID==INVALID_ID) {
		// removing goal.
		if (m_info) {
			m_info->m_goalAircraftID = INVALID_ID;
			m_aircraftGoal = false;
			releaseInfo();
		}	else {
			DEBUG_ASSERTCRASH(m_aircraftGoal==false, ("Bad flags."));
		}
	} else {
		// adding goal.
		if (!m_info) {
			DEBUG_ASSERTCRASH(m_aircraftGoal==false, ("Bad flags."));
			allocateInfo(pos);
		}
		if (!m_info) {
			DEBUG_CRASH(("Ran out of pathfind cells - fatal error!!!!! jba. "));
			return;
		}
		m_info->m_goalAircraftID = unitID;
		m_aircraftGoal = true;
	}
}


/**
 * Sets the position unit into the info record for a cell.
 */
void PathfindCell::setPosUnit(ObjectID unitID, const ICoord2D &pos ) 
{ 
	if (unitID==INVALID_ID) {
		// removing position.
		if (m_info) {
			m_info->m_posUnitID = INVALID_ID;
			if (m_info->m_goalUnitID == INVALID_ID) {
				// No units here.
				DEBUG_ASSERTCRASH(m_flags==UNIT_PRESENT_MOVING, ("Bad flags."));
				m_flags = NO_UNITS;
				releaseInfo();
			}	else {
				m_flags = UNIT_GOAL;
			}
		}	else {
			DEBUG_ASSERTCRASH(m_flags == NO_UNITS, ("Bad flags."));
		}
	} else {
		// adding goal.
		if (!m_info) {
			DEBUG_ASSERTCRASH(m_flags == NO_UNITS, ("Bad flags."));
			allocateInfo(pos);
		}
		if (!m_info) {
			DEBUG_CRASH(("Ran out of pathfind cells - fatal error!!!!! jba. "));
			return;
		}
		if (m_info->m_goalUnitID!=INVALID_ID && (m_info->m_goalUnitID==m_info->m_posUnitID)) {
			// A unit is already occupying this cell.
			return;
		}
		m_info->m_posUnitID = unitID;
		if (unitID==m_info->m_goalUnitID) {
			m_flags = UNIT_PRESENT_FIXED;
		} else if (m_info->m_goalUnitID==INVALID_ID) {
			m_flags = UNIT_PRESENT_MOVING;
		}	else {
			m_flags = UNIT_GOAL_OTHER_MOVING;
		}
	}
}


/**
 * Flag this cell as an obstacle, from the given one.
 * Return true if cell was flagged.
 */
Bool PathfindCell::setTypeAsObstacle( Object *obstacle, Bool isFence, const ICoord2D &pos )
{
	if (m_type!=PathfindCell::CELL_CLEAR && m_type != PathfindCell::CELL_IMPASSABLE) {
		return false;
	}

	Bool isRubble = false;
	if (obstacle->getBodyModule() && obstacle->getBodyModule()->getDamageState() == BODY_RUBBLE) 
	{
		isRubble = true;
	}

	if (isRubble) {
		m_type = PathfindCell::CELL_RUBBLE;
		if (m_info) {
			m_info->m_obstacleID = INVALID_ID;
			releaseInfo();
		}
		return true;
	}

	m_type = PathfindCell::CELL_OBSTACLE ;
	if (!m_info) {
		m_info = PathfindCellInfo::getACellInfo(this, pos);
		if (!m_info) {
			DEBUG_CRASH(("Not enough PathFindCellInfos in pool."));
			return false;
		}
	}
	m_info->m_obstacleID = obstacle->getID();
	m_info->m_obstacleIsFence = isFence;
	m_info->m_obstacleIsTransparent = obstacle->isKindOf(KINDOF_CAN_SEE_THROUGH_STRUCTURE);
	return true;
}

/**
 * Flag this cell as given type.
 */
void PathfindCell::setType( CellType type )
{
	if (m_info && (m_info->m_obstacleID != INVALID_ID)) {
		DEBUG_ASSERTCRASH(type==PathfindCell::CELL_OBSTACLE, ("Wrong type."));
		m_type = PathfindCell::CELL_OBSTACLE;
		return;
	}
	m_type = type;
}

/**
 * Unflag this cell as an obstacle, from the given one.
 * Return true if this cell was previously flagged as an obstacle by this object. 
 */
Bool PathfindCell::removeObstacle( Object *obstacle )
{
	if (m_type == PathfindCell::CELL_RUBBLE) {
		m_type = PathfindCell::CELL_CLEAR;
	}
	if (!m_info) return false;
	if (m_info->m_obstacleID != obstacle->getID()) return false;
	m_type = PathfindCell::CELL_CLEAR;
	m_info->m_obstacleID = INVALID_ID;
	releaseInfo();
	return true;
}

//-----------------------------------------------------------------------------------
// Open list as an indexed binary heap.
//
// Cells come out in the order of a list sorted by m_totalCost in which a new cell goes behind
// all cells of equal cost (FIFO on ties). Path results, and with them the game state, depend on
// this order. A cell's cost never changes while it is in the list; re-rating a cell removes and
// re-inserts it.
//
// The heap key encodes exactly this order: cost (UnsignedShort, 16 bit) at the top, a running
// insertion number (32 bit) below it, and the cell info index (16 bit) at the bottom, used only to
// find the cell. The number is unique per list, so the index never decides. The cost is copied
// into the key on insertion, so callers may change m_totalCost before removing a cell.
//
// releaseAll frees in key order because releaseInfo() pushes onto the free list of cell infos,
// whose order must stay stable. debugShowSearch walks the heap order, which is fine for display.
//-----------------------------------------------------------------------------------

enum
{
	OPEN_KEY_INDEX_BITS = 16,
	OPEN_KEY_SEQ_SHIFT = 16,
	OPEN_KEY_COST_SHIFT = 48
};
static_assert(CELL_INFOS_TO_ALLOCATE <= (1 << OPEN_KEY_INDEX_BITS), "cell info index does not fit the heap key");
static_assert(CELL_INFOS_TO_ALLOCATE <= (1 << 16), "m_heapIndex hat 16 Bit");

PathfindOpenList::PathfindOpenList( void ) : m_heap(nullptr), m_count(0), m_nextSeq(0)
{
	// The open list cannot hold more entries than there are cell infos.
	m_heap = MSGNEW("PathfindOpenList") UnsignedInt64[CELL_INFOS_TO_ALLOCATE];
}

PathfindOpenList::~PathfindOpenList( void )
{
	delete [] m_heap;
	m_heap = nullptr;
}

inline PathfindCellInfo *PathfindOpenList::infoOfKey( UnsignedInt64 key )
{
	return &PathfindCellInfo::s_infoArray[static_cast<Int>(key & ((1 << OPEN_KEY_INDEX_BITS) - 1))];
}

PathfindCell *PathfindOpenList::getHead( void ) const
{
	return m_count ? infoOfKey(m_heap[0])->m_cell : nullptr;
}

PathfindCell *PathfindOpenList::getCellAt( Int ndx ) const
{
	return infoOfKey(m_heap[ndx])->m_cell;
}

void PathfindOpenList::siftUp( Int ndx )
{
	UnsignedInt64 key = m_heap[ndx];
	while (ndx > 0)
	{
		Int parent = (ndx - 1) >> 1;
		UnsignedInt64 parentKey = m_heap[parent];
		if (parentKey < key)
			break;
		m_heap[ndx] = parentKey;
		infoOfKey(parentKey)->m_heapIndex = ndx;
		ndx = parent;
	}
	m_heap[ndx] = key;
	infoOfKey(key)->m_heapIndex = ndx;
}

void PathfindOpenList::siftDown( Int ndx )
{
	UnsignedInt64 key = m_heap[ndx];
	for (;;)
	{
		Int child = 2*ndx + 1;
		if (child >= m_count)
			break;
		if (child + 1 < m_count && m_heap[child + 1] < m_heap[child])
			child++;
		UnsignedInt64 childKey = m_heap[child];
		if (key < childKey)
			break;
		m_heap[ndx] = childKey;
		infoOfKey(childKey)->m_heapIndex = ndx;
		ndx = child;
	}
	m_heap[ndx] = key;
	infoOfKey(key)->m_heapIndex = ndx;
}

void PathfindOpenList::push( PathfindCellInfo *info )
{
	if (m_count >= CELL_INFOS_TO_ALLOCATE)
	{
		// Only possible if a cell was inserted twice.
		DEBUG_CRASH(("Pathfind open list overflow."));
		return;
	}
	UnsignedInt64 key = (static_cast<UnsignedInt64>(info->m_totalCost) << OPEN_KEY_COST_SHIFT)
		| (static_cast<UnsignedInt64>(m_nextSeq) << OPEN_KEY_SEQ_SHIFT)
		| static_cast<UnsignedInt64>(info - PathfindCellInfo::s_infoArray);
	m_nextSeq++;
	m_heap[m_count] = key;
	m_count++;
	siftUp(m_count - 1);
}

/// start the list with startCell, right after startCell->startPathfind()
void PathfindOpenList::initWithStartCell( PathfindCell *startCell )
{
	DEBUG_ASSERTCRASH(startCell->m_info, ("Has to have info."));
	DEBUG_ASSERTCRASH(startCell->m_info->m_open==TRUE && startCell->m_info->m_closed==FALSE, ("Serious error - Invalid flags. jba"));
	// A stale list is dropped, not released.
	m_count = 0;
	m_nextSeq = 0;
	push(startCell->m_info);
}

/// put cell on "open" list in ascending cost order
void PathfindOpenList::insert( PathfindCell *cell )
{
	PathfindCellInfo *info = cell->m_info;
	DEBUG_ASSERTCRASH(info, ("Has to have info."));
	DEBUG_ASSERTCRASH(info->m_closed==FALSE && info->m_open==FALSE, ("Serious error - Invalid flags. jba"));
	push(info);
	// The open list does not use the links; they belong to the closed list only.
	info->m_prevOpen = nullptr;
	info->m_nextOpen = nullptr;
	// mark newCell as being on open list
	info->m_open = true;
	info->m_closed = false;
}

/// remove cell from "open" list
void PathfindOpenList::remove( PathfindCell *cell )
{
	PathfindCellInfo *info = cell->m_info;
	DEBUG_ASSERTCRASH(info, ("Has to have info."));
	DEBUG_ASSERTCRASH(info->m_closed==FALSE && info->m_open==TRUE, ("Serious error - Invalid flags. jba"));
	Int ndx = info->m_heapIndex;
	if (ndx < m_count && infoOfKey(m_heap[ndx]) == info)
	{
		m_count--;
		if (ndx < m_count)
		{
			UnsignedInt64 last = m_heap[m_count];
			m_heap[ndx] = last;
			if (ndx > 0 && last < m_heap[(ndx - 1) >> 1])
				siftUp(ndx);
			else
				siftDown(ndx);
		}
	}
	else
	{
		// The cell has m_open set but is not in the list: the start cell of a search that stopped after
		// startPathfind() before the list was set up (findAttackPath). EA's linked list lost all entries
		// in this case; clearing keeps that behaviour. The dropped cells keep m_open.
		m_count = 0;
	}
	info->m_open = false;
	info->m_nextOpen = nullptr;
	info->m_prevOpen = nullptr;
}

/// remove all cells from "open" list
Int PathfindOpenList::releaseAll( void )
{
	// Free in ascending key order: releaseInfo() pushes each cell info onto the front of the free
	// list, and that order affects later searches, so it must stay stable.
	std::sort(m_heap, m_heap + m_count);
	Int count = m_count;
	for (Int i = 0; i < count; i++)
	{
		PathfindCellInfo *curInfo = infoOfKey(m_heap[i]);
		PathfindCell *cur = curInfo->m_cell;
		DEBUG_ASSERTCRASH(cur->m_info == curInfo, ("Bad backpointer in PathfindCellInfo"));
		DEBUG_ASSERTCRASH(curInfo->m_closed==FALSE && curInfo->m_open==TRUE, ("Serious error - Invalid flags. jba"));
		curInfo->m_nextOpen = nullptr;
		curInfo->m_prevOpen = nullptr;
		curInfo->m_open = FALSE;
		cur->releaseInfo();
	}
	m_count = 0;
	m_nextSeq = 0;
	return count;
}

/// remove all cells from "closed" list
Int PathfindCell::releaseClosedList( PathfindCell *list )
{
	Int count = 0;
	while (list) {
		count++;
		DEBUG_ASSERTCRASH(list->m_info, ("Has to have info."));
		DEBUG_ASSERTCRASH(list->m_info->m_closed==TRUE && list->m_info->m_open==FALSE, ("Serious error - Invalid flags. jba"));
		PathfindCell *cur = list;
		PathfindCellInfo *curInfo = list->m_info;
		if (curInfo->m_nextOpen) {
			list = curInfo->m_nextOpen->m_cell;
		}	else {
			list = nullptr;
		}
		DEBUG_ASSERTCRASH(cur == curInfo->m_cell, ("Bad backpointer in PathfindCellInfo"));
		curInfo->m_nextOpen = nullptr;
		curInfo->m_prevOpen = nullptr;
		curInfo->m_closed = FALSE;
		cur->releaseInfo();
	}
	return count;
}

/// put self on "closed" list, return new list
PathfindCell *PathfindCell::putOnClosedList( PathfindCell *list )
{
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	DEBUG_ASSERTCRASH(m_info->m_closed==FALSE && m_info->m_open==FALSE, ("Serious error - Invalid flags. jba"));
	// only put on list if not already on it
	if (m_info->m_closed == FALSE)
	{
		m_info->m_closed = FALSE;
		m_info->m_closed = TRUE;

		m_info->m_prevOpen = nullptr;
		m_info->m_nextOpen = list?list->m_info:nullptr;
		if (list)
			list->m_info->m_prevOpen = this->m_info;
		
		list = this;
	}

	return list;
}

/// remove self from "closed" list
PathfindCell *PathfindCell::removeFromClosedList( PathfindCell *list )
{
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	DEBUG_ASSERTCRASH(m_info->m_closed==TRUE && m_info->m_open==FALSE, ("Serious error - Invalid flags. jba"));
	if (m_info->m_nextOpen)
		m_info->m_nextOpen->m_prevOpen = m_info->m_prevOpen;
	
	if (m_info->m_prevOpen)
		m_info->m_prevOpen->m_nextOpen = m_info->m_nextOpen;
	else
		list = getNextOpen();

	m_info->m_closed = false;
	m_info->m_nextOpen = nullptr;
	m_info->m_prevOpen = nullptr;

	return list;
}

UnsignedInt PathfindCell::costToGoal( PathfindCell *goal )
{
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	Int dx = m_info->m_pos.x - goal->getXIndex();
	Int dy = m_info->m_pos.y - goal->getYIndex();
#define NO_REAL_DIST
#ifdef REAL_DIST
	Int cost = COST_ORTHOGONAL*sqrt(dx*dx + dy*dy);
#else
	if (dx<0) dx = -dx;
	if (dy<0) dy = -dy;
	Int cost;
	if (dx>dy) {
		cost= COST_ORTHOGONAL*dx + (COST_ORTHOGONAL*dy)/2;
	}	else {
		cost= COST_ORTHOGONAL*dy + (COST_ORTHOGONAL*dx)/2;
	}

#endif


	return cost;
}

UnsignedInt PathfindCell::costToHierGoal( PathfindCell *goal )
{
	if( !m_info )
	{
		DEBUG_CRASH( ("Has to have info.") );
		return 100000; //...patch hack 1.01
	}
	Int dx = m_info->m_pos.x - goal->getXIndex();
	Int dy = m_info->m_pos.y - goal->getYIndex();
	Int cost = REAL_TO_INT_FLOOR(static_cast<Real>(COST_ORTHOGONAL*sqrt(dx*dx + dy*dy) + 0.5f));
	return cost;
}

UnsignedInt PathfindCell::costSoFar( PathfindCell *parent )
{
	DEBUG_ASSERTCRASH(m_info, ("Has to have info."));
	// very first node in path - no turns, no cost
	if (parent == nullptr)
		return 0;

	// add in number of turns in path so far
	ICoord2D prevDir;
	Int cost;

	prevDir.x = parent->getXIndex() - m_info->m_pos.x;
	prevDir.y = parent->getYIndex() - m_info->m_pos.y;

	// diagonal moves cost a bit more than orthogonal ones
	if (prevDir.x == 0 || prevDir.y == 0)
		cost = parent->getCostSoFar() + COST_ORTHOGONAL;
	else
		cost = parent->getCostSoFar() + COST_DIAGONAL;
	if (getPinched()) {
		cost += 1*COST_DIAGONAL;
	}

#if 1
	// Increase cost of turns.
	Int numTurns = 0;
	PathfindCell *prevCell = parent->getParentCell();
	if (prevCell) {
		ICoord2D dir;
		dir.x = prevCell->getXIndex() - parent->getXIndex();
		dir.y = prevCell->getYIndex() - parent->getYIndex();
		
		// count number of direction changes
		if (dir.x != prevDir.x || dir.y != prevDir.y)
		{
			Int dot = dir.x * prevDir.x + dir.y * prevDir.y;
			if (dot > 0)
				numTurns=4;				// 45 degree turn
			else if (dot == 0)
				numTurns = 8;		// 90 degree turn
			else
				numTurns = 16;		// 135 degree turn
		}
	}

	return cost + numTurns;
#else
	return cost;
#endif

}






/**
 * Calculate zones.  A zone is an area of the same terrain - clear, water or cliff.
 * The utility of zones is that if current location and destiontion are in the same zone, 
 * you can successfully pathfind.
 * If you are a multiple terrain vehicle, like amphibious transport, the lookup is a little more
 * complicated.
 */



#ifdef forceRefreshCalling
Bool s_stopForceCalling = FALSE;
#endif













//
// Clear the passable flags.
//
void PathfindZoneManager::clearPassableFlags( ) 
{	Int blockX;
	Int blockY;
	for (blockX = 0; blockX<m_zoneBlockExtent.x; blockX++) {
		for (blockY = 0; blockY<m_zoneBlockExtent.y; blockY++) {
			m_zoneBlocks[blockX][blockY].setPassable(false);
		}
	}
}

//
// Set the passable flags.
//
void PathfindZoneManager::setAllPassable( ) 
{	Int blockX;
	Int blockY;
	for (blockX = 0; blockX<m_zoneBlockExtent.x; blockX++) {
		for (blockY = 0; blockY<m_zoneBlockExtent.y; blockY++) {
			m_zoneBlocks[blockX][blockY].setPassable(true);
		}
	}
}










//----------------------- Pathfinder ---------------------------------------

Pathfinder::Pathfinder( void ) :m_map(nullptr)
{
	m_statQueueMs = 0.0;
	m_statMaxMs = 0.0;
	m_statCells = 0.0;
	m_statQueueLength = 0.0;
	m_statRequestMaxMs = 0.0;
	m_statQueueMax = 0;
	m_statLeftOver = 0;
	for (Int i = 0; i < 4; ++i) m_statPerTick[i] = 0;
	m_statTicks = 0;
	m_statRequests = 0;
	debugPath = nullptr;
	PathfindCellInfo::allocateCellInfos();
	reset();
}

Pathfinder::~Pathfinder( void )
{
	PathfindCellInfo::releaseCellInfos();
}

void Pathfinder::reset( void )
{
	frameToShowObstacles = 0;
	DEBUG_LOG(("Pathfind cell is %d bytes, PathfindCellInfo is %d bytes\n", static_cast<Int>(sizeof(PathfindCell)), static_cast<Int>(sizeof(PathfindCellInfo))));

	if (m_blockOfMapCells) {
		delete []m_blockOfMapCells;
		m_blockOfMapCells = nullptr;
	}
	if (m_map) {	 
		delete [] m_map;
		m_map = nullptr;
	}

	Int i;
	for (i=0; i<=LAYER_LAST; i++) {
		m_layers[i].reset();
	}

	// reset the pathfind grid
	m_extent.lo.x=m_extent.lo.y=m_extent.hi.x=m_extent.hi.y=0;
	m_logicalExtent.lo.x=m_logicalExtent.lo.y=m_logicalExtent.hi.x=m_logicalExtent.hi.y=0;
	m_openList.clearWithoutRelease();
	m_closedList = nullptr;

	m_ignoreObstacleID = INVALID_ID;
	m_isTunneling = false;

	m_moveAlliesDepth = 0;

	// pathfind grid cells have not been classified yet
	m_isMapReady = false;
	m_cumulativeCellsAllocated = 0;

	debugPathPos.x = 0.0f;
	debugPathPos.y = 0.0f;
	debugPathPos.z = 0.0f;

	if (debugPath)
		deleteInstanceOf(debugPath);

	debugPath = nullptr;
	m_frameToShowObstacles = 0;

	for (m_queuePRHead=0; m_queuePRHead<PATHFIND_QUEUE_LEN; m_queuePRHead++) {
		m_queuedPathfindRequests[m_queuePRHead] = INVALID_ID;
	}
	m_queuePRHead = 0;
	m_queuePRTail = 0;

	m_numWallPieces = 0;
	for (i=0; i<MAX_WALL_PIECES; ++i)
	{
		m_wallPieces[i] = INVALID_ID;
	}

	if (TheAI && TheAI->getAiData()) {
		m_wallHeight = TheAI->getAiData()->m_wallHeight;
	}
	else
	{
		m_wallHeight = 0.0f;
	}
	m_zoneManager.reset();
}

/** 
 * Adds a piece of a wall. 
 */
void Pathfinder::addWallPiece(Object *wallPiece)
{
	if (m_numWallPieces<MAX_WALL_PIECES-1) {
		m_wallPieces[m_numWallPieces] = wallPiece->getID();
		m_numWallPieces++;
	}
}

/**
 * Removes a piece of a wall
 */
void Pathfinder::removeWallPiece(Object *wallPiece)
{

	// sanity
  if( wallPiece == nullptr )
		return;

	// find entry
	for( Int i = 0; i < m_numWallPieces; ++i )
	{

		// match by id
		if( m_wallPieces[ i ] == wallPiece->getID() )
		{

			// put the last id in the wall piece array here
			m_wallPieces[ i ] = m_wallPieces[ m_numWallPieces - 1 ];

			// we now have one less entry
			m_numWallPieces--;

			// all done
			return;

		}  // end if

	}  // end for, i

}  // end removeWallPiece

/** 
 * Checks if a point is on the wall. 
 */
Bool Pathfinder::isPointOnWall(const Coord3D *pos)
{
	if (m_numWallPieces==0) return false;
	if (m_layers[LAYER_WALL].isUnused()) return false;
	PathfindLayerEnum layer = static_cast<PathfindLayerEnum>(LAYER_WALL);
	PathfindCell *cell = getCell(layer, pos);
	// make sure the layer matches, since getCell can return ground layer cells if the pos is 'off' the bridge/wall
	if (cell && cell->getLayer() == layer) {
		if (cell->getType() == PathfindCell::CELL_CLEAR) {
			return true;
		}
	}
	return false;
}

/** 
 * Adds a bridge & returns the layer. 
 */
PathfindLayerEnum Pathfinder::addBridge(Bridge *theBridge)
{
	Int layer = LAYER_GROUND+1;
	while (layer<=LAYER_WALL) {
		if (m_layers[layer].isUnused()) {
			if (m_layers[layer].init(theBridge, static_cast<PathfindLayerEnum>(layer)) ) {
				return static_cast<PathfindLayerEnum>(layer);
			}
			DEBUG_LOG(("WARNING: Bridge failed to init in pathfinder\n"));
			return LAYER_GROUND; // failed to init, usually cause off of the map.  jba.
		}
		layer++;
	}
	DEBUG_CRASH(("Ran out of bridge layers."));
	return LAYER_GROUND;
}

/** 
 * Updates an object's layer, making sure the object is actually on the bridge first. 
 */
void Pathfinder::updateLayer(Object *obj, PathfindLayerEnum layer)
{
	if (layer != LAYER_GROUND) {
		if (!TheTerrainLogic->objectInteractsWithBridgeLayer(obj, layer)) {
			layer = LAYER_GROUND;
		}
	}
	//DEBUG_LOG(("Object layer is %d\n", layer));
	obj->setLayer(layer);
}

/** 
 * Classify the cells under the given object
 * If 'insert' is true, object is being added 
 * If 'insert' is false, object is being removed 
 */
void Pathfinder::classifyFence( Object *obj, Bool insert )
{	
	const Coord3D *pos = obj->getPosition();
  Real angle = obj->getOrientation();
 
 	Real halfsizeX = obj->getTemplate()->getFenceWidth()/2;
 	Real halfsizeY = PATHFIND_CELL_SIZE_F/10.0f;
 	Real fenceOffset = obj->getTemplate()->getFenceXOffset();

 	Real c = static_cast<Real>(Cos(angle));
 	Real s = static_cast<Real>(Sin(angle));
 		
 	const Real STEP_SIZE = PATHFIND_CELL_SIZE_F * 0.5f;	// in theory, should be PATHFIND_CELL_SIZE_F exactly, but needs to be smaller to avoid aliasing problems
 	Real ydx = s * STEP_SIZE;
 	Real ydy = -c * STEP_SIZE;
 	Real xdx = c * STEP_SIZE;
 	Real xdy = s * STEP_SIZE;

 	Int numStepsX = REAL_TO_INT_CEIL(2.0f * halfsizeX / STEP_SIZE);
 	Int numStepsY = REAL_TO_INT_CEIL(2.0f * halfsizeY / STEP_SIZE);

 	Real tl_x = pos->x - fenceOffset*c - halfsizeY*s;
 	Real tl_y = pos->y + halfsizeY*c - fenceOffset*s;

	IRegion2D cellBounds;
	cellBounds.lo.x = REAL_TO_INT_FLOOR((pos->x + 0.5f)/PATHFIND_CELL_SIZE_F);
	cellBounds.lo.y = REAL_TO_INT_FLOOR((pos->y + 0.5f)/PATHFIND_CELL_SIZE_F);
	cellBounds.hi = cellBounds.lo;	// hi is read below; set as in internal_classifyObjectFootprint
	Bool didAnything = false;

 	for (Int iy = 0; iy < numStepsY; ++iy, tl_x += ydx, tl_y += ydy)
 	{
 		Real x = tl_x;
 		Real y = tl_y;
 		for (Int ix = 0; ix < numStepsX; ++ix, x += xdx, y += xdy)
 		{
 			Int cx = REAL_TO_INT_FLOOR((x + 0.5f)/PATHFIND_CELL_SIZE_F);
 			Int cy = REAL_TO_INT_FLOOR((y + 0.5f)/PATHFIND_CELL_SIZE_F);
 			if (cx >= 0 && cy >= 0 && cx < m_extent.hi.x && cy < m_extent.hi.y)
 			{
 				if (insert) {
 					ICoord2D pos5;
 					pos5.x = cx;
 					pos5.y = cy;
					if (m_map[cx][cy].setTypeAsObstacle( obj, true, pos5 )) {
						didAnything = true;
 						m_map[cx][cy].setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
					}
 				}
				else {
					if (m_map[cx][cy].removeObstacle(obj)) {
						didAnything = true;
 						m_map[cx][cy].setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
					}
				}
				if (cellBounds.lo.x>cx) cellBounds.lo.x = cx;
 				if (cellBounds.lo.y>cy) cellBounds.lo.y = cy;
 				if (cellBounds.hi.x<cx) cellBounds.hi.x = cx;
 				if (cellBounds.hi.y<cy) cellBounds.hi.y = cy;
 			} 			
 		}
 	}
	if (didAnything) {
		m_zoneManager.markZonesDirty( insert );
		m_zoneManager.updateZonesForModify(m_map, m_layers, cellBounds, m_extent);
	}

}

/** 
 * Classify the cells under the given object
 * If 'insert' is true, object is being added 
 * If 'insert' is false, object is being removed 
 */
void Pathfinder::classifyObjectFootprint( Object *obj, Bool insert )
{
	if (obj->isKindOf(KINDOF_MINE)) {
		return;  // don't pathfind around mines.
	}

	if (obj->isKindOf(KINDOF_PROJECTILE)) {
		return;  // don't care about projectiles.
	}

	if (obj->isKindOf(KINDOF_BRIDGE_TOWER)) {
		return;  // It is important to not abuse bridge towers.
	}

	if (obj->getTemplate()->getFenceWidth() > 0.0f) 
	{
		if (!obj->isKindOf(KINDOF_DEFENSIVE_WALL))
		{
			classifyFence(obj, insert);
			return;
		}
	}

	if (!insert) {
		// Just in case, remove the object.  Remove checks that the object has been added before
		// removing, so it's safer to just remove it, as by the time some units "die", they've become
		// lifeless immobile husks of debris, but we still need to remove them.  jba.

    if ( obj->isKindOf( KINDOF_BLAST_CRATER ) ) // since these footprints are permanent, never remove them
      return;


		removeUnitFromPathfindMap(obj);
		if (obj->isKindOf(KINDOF_WALK_ON_TOP_OF_WALL)) { 
			if (!m_layers[LAYER_WALL].isUnused()) {
				Int i;
				ObjectID curID = obj->getID();
				for (i=0; i<m_numWallPieces; i++) {
					if (curID == m_wallPieces[i]) {
						m_wallPieces[i]=INVALID_ID;
					}
				}
				// Kill anybody on the wall.
				Object *obj2;
				for (obj2 = TheGameLogic->getFirstObject(); obj2; obj2=obj2->getNextObject()) {
					if (obj2->getLayer() == LAYER_WALL) {
						if (m_layers[LAYER_WALL].isPointOnWall(&curID, 1, obj2->getPosition())) 
						{
							// The object fell off the wall.
							// Destroy it.
							DamageInfo extraDamageInfo;
							extraDamageInfo.in.m_damageType = DAMAGE_FALLING;
							extraDamageInfo.in.m_deathType = DEATH_SPLATTED;
							extraDamageInfo.in.m_sourceID = obj2->getID();
							extraDamageInfo.in.m_amount = HUGE_DAMAGE_AMOUNT;
							obj2->attemptDamage(&extraDamageInfo);
						}
					}
				}
				// recalc the wall.
				m_layers[LAYER_WALL].classifyWallCells(m_wallPieces, m_numWallPieces);
			}
		}
	}
	if (!obj->isKindOf(KINDOF_STRUCTURE)) {
		return;  // Only path around structures.
	}
	if (obj->isMobile()) {
		return; // mobile aren't obstacles.
	}
	/// For now, all small objects will not be obstacles
	if (obj->getGeometryInfo().getIsSmall()) {
		return;
	}

	if (obj->getHeightAboveTerrain() > PATHFIND_CELL_SIZE_F && ( ! obj->isKindOf( KINDOF_BLAST_CRATER ) ) ) 
  {
		return; // Don't add bounds that are up in the air.... unless a blast crater wants to do just that
	}
	internal_classifyObjectFootprint(obj, insert);
}

void Pathfinder::internal_classifyObjectFootprint( Object *obj, Bool insert )
{
	IRegion2D cellBounds;
	const Coord3D *pos = obj->getPosition();
	cellBounds.lo.x = REAL_TO_INT_FLOOR((pos->x + 0.5f)/PATHFIND_CELL_SIZE_F);
	cellBounds.lo.y = REAL_TO_INT_FLOOR((pos->y + 0.5f)/PATHFIND_CELL_SIZE_F);
	cellBounds.hi = cellBounds.lo;

	switch(obj->getGeometryInfo().getGeomType())
	{
		case GEOMETRY_BOX:
		{
			m_zoneManager.markZonesDirty( insert );
      
			Real angle = obj->getOrientation();

			Real halfsizeX = obj->getGeometryInfo().getMajorRadius();
			Real halfsizeY = obj->getGeometryInfo().getMinorRadius();

			Real c = static_cast<Real>(Cos(angle));
			Real s = static_cast<Real>(Sin(angle));
				
			const Real STEP_SIZE = PATHFIND_CELL_SIZE_F * 0.5f;	// in theory, should be PATHFIND_CELL_SIZE_F exactly, but needs to be smaller to avoid aliasing problems
			Real ydx = s * STEP_SIZE;
			Real ydy = -c * STEP_SIZE;
			Real xdx = c * STEP_SIZE;
			Real xdy = s * STEP_SIZE;

			Int numStepsX = REAL_TO_INT_CEIL(2.0f * halfsizeX / STEP_SIZE);
			Int numStepsY = REAL_TO_INT_CEIL(2.0f * halfsizeY / STEP_SIZE);

			Real tl_x = pos->x - halfsizeX*c - halfsizeY*s;
			Real tl_y = pos->y + halfsizeY*c - halfsizeX*s;

			for (Int iy = 0; iy < numStepsY; ++iy, tl_x += ydx, tl_y += ydy)
			{
				Real x = tl_x;
				Real y = tl_y;
				for (Int ix = 0; ix < numStepsX; ++ix, x += xdx, y += xdy)
				{
					Int cx = REAL_TO_INT_FLOOR((x + 0.5f)/PATHFIND_CELL_SIZE_F);
					Int cy = REAL_TO_INT_FLOOR((y + 0.5f)/PATHFIND_CELL_SIZE_F);

					if (cx >= 0 && cy >= 0 && cx < m_extent.hi.x && cy < m_extent.hi.y)
					{
						if (insert) {
							ICoord2D pos4;
							pos4.x = cx;
							pos4.y = cy;
							if (m_map[cx][cy].setTypeAsObstacle( obj, false, pos4 )) {
 								m_map[cx][cy].setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
							}
						}
						else {
							if (m_map[cx][cy].removeObstacle(obj)) {
 								m_map[cx][cy].setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
							}
						}
 						if (cellBounds.lo.x>cx) cellBounds.lo.x = cx;
 						if (cellBounds.lo.y>cy) cellBounds.lo.y = cy;
 						if (cellBounds.hi.x<cx) cellBounds.hi.x = cx;
 						if (cellBounds.hi.y<cy) cellBounds.hi.y = cy;
					}
				}
			}
		}
		break;

		case GEOMETRY_SPHERE:	// not quite right, but close enough
		case GEOMETRY_CYLINDER:
		{
			m_zoneManager.markZonesDirty( insert );
			// fill in all cells that overlap as obstacle cells
			/// @todo This is a very inefficient circle-rasterizer
			ICoord2D topLeft, bottomRight;
			Coord2D center, delta;
			Real radius = obj->getGeometryInfo().getMajorRadius();
			Real r2, size;

			topLeft.x = REAL_TO_INT_FLOOR(0.5f + (pos->x - radius)/PATHFIND_CELL_SIZE_F)-1;
			topLeft.y = REAL_TO_INT_FLOOR(0.5f + (pos->y - radius)/PATHFIND_CELL_SIZE_F)-1;
			size = (radius/PATHFIND_CELL_SIZE_F);
			center.x = (pos->x/PATHFIND_CELL_SIZE_F);
			center.y = (pos->y/PATHFIND_CELL_SIZE_F);

			size += 0.4f;
			r2 = size*size;

			bottomRight.x = static_cast<Int>(topLeft.x + 2*size + 2);
			bottomRight.y = static_cast<Int>(topLeft.y + 2*size + 2);

			for( int j = topLeft.y; j < bottomRight.y; j++ )
			{
				for( int i = topLeft.x; i < bottomRight.x; i++ )
				{
					delta.x = i+0.5f - center.x;
					delta.y = j+0.5f - center.y;

					if (delta.x*delta.x + delta.y*delta.y <= r2)
					{
						if (i >= 0 && j >= 0 && i < m_extent.hi.x && j < m_extent.hi.y)
						{
							if (insert) {
								ICoord2D pos3;
								pos3.x = i;
								pos3.y = j;
								if (m_map[i][j].setTypeAsObstacle( obj, false, pos3 )) {
 									m_map[i][j].setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
								}
							}
							else {
								if (m_map[i][j].removeObstacle(obj)) {
 									m_map[i][j].setZone(PathfindZoneManager::UNINITIALIZED_ZONE);
								}
							}
 							if (cellBounds.lo.x>i) cellBounds.lo.x = i;
 							if (cellBounds.lo.y>j) cellBounds.lo.y = j;
 							if (cellBounds.hi.x<i) cellBounds.hi.x = i;
 							if (cellBounds.hi.y<j) cellBounds.hi.y = j;
						}
					}
				} // for i
			} // for j
		} // cylinder
		break;
	} // switch
	m_zoneManager.updateZonesForModify(m_map, m_layers, cellBounds, m_extent);
	
	Int i, j;
	cellBounds.lo.x -= 2;
	cellBounds.lo.y -= 2;
	cellBounds.hi.x += 2;
	cellBounds.hi.y += 2;
	if (cellBounds.lo.x < m_extent.lo.x) {
		cellBounds.lo.x = m_extent.lo.x;
	}
	if (cellBounds.lo.y < m_extent.lo.y) {
		cellBounds.lo.y = m_extent.lo.y;
	}
	if (cellBounds.lo.y < m_extent.lo.y) {
		cellBounds.lo.y = m_extent.lo.y;
	}
	if (cellBounds.hi.x > m_extent.hi.x) {
		cellBounds.hi.x = m_extent.hi.x;
	}
	if (cellBounds.hi.y > m_extent.hi.y) {
		cellBounds.hi.y = m_extent.hi.y;
	}



	// Expand building bounds 1 cell.
#define no_EXPAND_ONE_CELL
#ifdef EXPAND_ONE_CELL
	for( j=cellBounds.lo.y; j<=cellBounds.hi.y; j++ )
	{
		for( i=cellBounds.lo.x; i<=cellBounds.hi.x; i++ )
		{
			if (!insert) {
				if (m_map[i][j].getType() == PathfindCell::CELL_IMPASSABLE) {
					m_map[i][j].setType(PathfindCell::CELL_CLEAR);
				}
				m_map[i][j].setPinched(false);
			}
			if (!insert) {
				if (m_map[i][j].isObstaclePresent(obj->getID())) {
					m_map[i][j].removeObstacle( obj );
				}
				continue;
			}
			if (m_map[i][j].getType() == PathfindCell::CELL_CLEAR) {
				Bool obstacleAdjacent = false;
				Int k, l;
				for (k=i-1; k<i+2; k++) {
					if (k<m_extent.lo.x || k> m_extent.hi.x) continue;
					for (l=j-1; l<j+2; l++) {
						if (l<m_extent.lo.y || l> m_extent.hi.y) continue;
						if ((k==i) && (l==j)) continue;
						if ((k!=i) && (l!=j)) continue;
						if (m_map[k][l].getType()!=PathfindCell::CELL_CLEAR)) {	
							objectAdjacent = true;
							break;
						}

					}
				}
				if (obstacleAdjacent) {
					m_map[i][j].setPinched(true);
				}
				// If the total open cells are < 2
			}
		}
	}	 

	if (insert) {
		for( j=cellBounds.lo.y; j<=cellBounds.hi.y; j++ )
		{
			for( i=cellBounds.lo.x; i<=cellBounds.hi.x; i++ )
			{
				if (m_map[i][j].getPinched() && m_map[i][j].getType() == PathfindCell::CELL_CLEAR) {
					ICoord2D pos;
					pos.x = i;
					pos.y = j;
					m_map[i][j].setTypeAsObstacle( obj, false, pos );
					//m_map[i][j].setType(PathfindCell::CELL_CLIFF);
					m_map[i][j].setPinched(false);
				}
			}
		}
	}
#endif



	if (!insert) {
		for( j=cellBounds.lo.y; j<=cellBounds.hi.y; j++ )
		{
			for( i=cellBounds.lo.x; i<=cellBounds.hi.x; i++ )
			{
				if (m_map[i][j].getType()==PathfindCell::CELL_IMPASSABLE) {
					m_map[i][j].setType(PathfindCell::CELL_CLEAR);
				}
			}
		}
	}
	// Check for pinched cells, and close them off.

	for( j=cellBounds.lo.y; j<=cellBounds.hi.y; j++ )
	{
		for( i=cellBounds.lo.x; i<=cellBounds.hi.x; i++ )
		{
			m_map[i][j].setPinched(false);
			if (m_map[i][j].getType() == PathfindCell::CELL_CLEAR) {
				Int totalCount = 0;
				Int orthogonalCount = 0;
				Int k, l;
				for (k=i-1; k<i+2; k++) {
					if (k<m_extent.lo.x || k> m_extent.hi.x) continue;
					for (l=j-1; l<j+2; l++) {
						if (l<m_extent.lo.y || l> m_extent.hi.y) continue;
						if ((k==i) && (j==l)) continue;
						if (m_map[k][l].getType() == PathfindCell::CELL_CLEAR) {
							totalCount++;
							if ((k==i) || (l==j)) {
								orthogonalCount++;
							}
						}

					}
				}
				// If the total open cells are < 2 or total cells < 4, we are pinched.
				if (orthogonalCount<2 || totalCount<4) {
					m_map[i][j].setPinched(true);
				}
			}
		}
	}

	for( j=cellBounds.lo.y; j<=cellBounds.hi.y; j++ )
	{
		for( i=cellBounds.lo.x; i<=cellBounds.hi.x; i++ )
		{
			if (m_map[i][j].getPinched() && (m_map[i][j].getType() == PathfindCell::CELL_CLEAR)) {
				m_map[i][j].setType(PathfindCell::CELL_IMPASSABLE);
				m_map[i][j].setPinched(false);
			}
		}
	}

	// Expand building bounds 1 cell.
#define MARK_BORDER_PINCHED
#ifdef MARK_BORDER_PINCHED
	for( j=cellBounds.lo.y; j<=cellBounds.hi.y; j++ )
	{
		for( i=cellBounds.lo.x; i<=cellBounds.hi.x; i++ )
		{
			if (m_map[i][j].getType() == PathfindCell::CELL_CLEAR) {
				Bool objectAdjacent = false;
				Int k, l;
				for (k=i-1; k<i+2; k++) {
					if (k<m_extent.lo.x || k> m_extent.hi.x) continue;
					for (l=j-1; l<j+2; l++) {
						if (l<m_extent.lo.y || l> m_extent.hi.y) continue;
						if ((k==i) && (l==j)) continue;
						if ((k!=i) && (l!=j)) continue;
						if (m_map[k][l].getType() == PathfindCell::CELL_OBSTACLE) {	
							objectAdjacent = true;
							break;
						}

					}
				}
				if (objectAdjacent) {
					m_map[i][j].setPinched(true);
				}
			}
		}
	}	
#endif



}

/**
 * Classify the given map cell as WATER, CLIFF, etc.
 * Note that this does NOT classify cells as OBSTACLES. 
 * OBSTACLE cells are classified only via objects.
 * @todo optimize this - lots of redundant computation
 */
void Pathfinder::classifyMapCell( Int i, Int j , PathfindCell *cell)
{
	Coord3D topLeftCorner, bottomRightCorner;


	Bool hasObstacle =  (cell->getType() == PathfindCell::CELL_OBSTACLE) ;

	topLeftCorner.y = static_cast<Real>(j) * PATHFIND_CELL_SIZE_F;
	bottomRightCorner.y = topLeftCorner.y + PATHFIND_CELL_SIZE_F;

	topLeftCorner.x = static_cast<Real>(i) * PATHFIND_CELL_SIZE_F;
	bottomRightCorner.x = topLeftCorner.x + PATHFIND_CELL_SIZE_F;

	cell->setPinched(false);

	PathfindCell::CellType type = PathfindCell::CELL_CLEAR;
	if (TheTerrainLogic->isCliffCell(topLeftCorner.x, topLeftCorner.y))
	{
		type = PathfindCell::CELL_CLIFF;
	}

	//
	// If any corners are underwater, this is a water cell
	//
	if (TheTerrainLogic->isUnderwater( topLeftCorner.x, topLeftCorner.y ) ) type = PathfindCell::CELL_WATER;
	if (TheTerrainLogic->isUnderwater( topLeftCorner.x, bottomRightCorner.y) ) type = PathfindCell::CELL_WATER;
	if (TheTerrainLogic->isUnderwater( bottomRightCorner.x, bottomRightCorner.y ) ) type = PathfindCell::CELL_WATER;
	if (TheTerrainLogic->isUnderwater( bottomRightCorner.x, topLeftCorner.y ) ) type = PathfindCell::CELL_WATER;

	if (hasObstacle) {
		type =  PathfindCell::CELL_OBSTACLE;
	}
	cell->setType( type );
	cell->releaseInfo();
}

/**
 * Set up for a new map.
 */
void Pathfinder::newMap( void )
{
	m_wallHeight = TheAI->getAiData()->m_wallHeight; // may be updated by map.ini.
	Region3D terrainExtent;
	TheTerrainLogic->getMaximumPathfindExtent( &terrainExtent );
	IRegion2D bounds;
	bounds.lo.x = REAL_TO_INT_FLOOR(terrainExtent.lo.x / PATHFIND_CELL_SIZE_F);
	bounds.hi.x = REAL_TO_INT_FLOOR(terrainExtent.hi.x / PATHFIND_CELL_SIZE_F);
	bounds.lo.y = REAL_TO_INT_FLOOR(terrainExtent.lo.y / PATHFIND_CELL_SIZE_F);
	bounds.hi.y = REAL_TO_INT_FLOOR(terrainExtent.hi.y / PATHFIND_CELL_SIZE_F);
	bounds.hi.x--;
	bounds.hi.y--;
	Bool dataAllocated = false;
	if (m_extent.hi.x==bounds.hi.x && m_extent.hi.y==bounds.hi.y) {
		if (m_blockOfMapCells != nullptr && m_map!=nullptr) {
			dataAllocated = true;
		}
	}
	// For map load from file, we have to call newMap twice to do sequencing issues.
	// so the second time through, dataAllocated==TRUE, so we skip the allocate.
	if (!dataAllocated) {
		m_extent = bounds;
		DEBUG_ASSERTCRASH(m_map == nullptr, ("Can't reallocate pathfind cells."));
 		m_zoneManager.allocateBlocks(m_extent);
		// Allocate cells.
		m_blockOfMapCells = MSGNEW("PathfindMapCells") PathfindCell[(bounds.hi.x+1)*(bounds.hi.y+1)];
		m_map = MSGNEW("PathfindMapCells") PathfindCellP[bounds.hi.x+1];
		Int i;
		for (i=0; i<=bounds.hi.x; i++) {
			m_map[i] = &m_blockOfMapCells[i*(bounds.hi.y+1)];
		}
		for (i=0; i<LAYER_LAST; i++) {
			if (!m_layers[i].isUnused()) {
				m_layers[i].allocateCells(&m_extent);
			}
		}
		if (m_numWallPieces>0) {
			m_layers[LAYER_WALL].init(nullptr, LAYER_WALL);
			m_layers[LAYER_WALL].allocateCellsForWallLayer(&m_extent, m_wallPieces, m_numWallPieces);
		}
	}
	classifyMap();
	// Add existing objects.
	Object *obj;
	for( obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		classifyObjectFootprint(obj, true);
	}

	m_isMapReady = true;
}

/**
 * Classify all cells in grid as obstacles, etc.
 */
void Pathfinder::classifyMap(void)
{

	Int i, j;
	// for now, sample cell corners and classify cell accordingly
	for( j=m_extent.lo.y; j<=m_extent.hi.y; j++ )
	{
		for( i=m_extent.lo.x; i<=m_extent.hi.x; i++ )
		{
			classifyMapCell( i, j, &m_map[i][j]);
		}
	}
#if 1
	// Expand all cliff cells one step (mark pinched)
	for( j=m_extent.lo.y; j<=m_extent.hi.y; j++ )
	{
		for( i=m_extent.lo.x; i<=m_extent.hi.x; i++ )
		{
			if (m_map[i][j].getType() & PathfindCell::CELL_CLIFF) {
				Int k, l;
				for (k=i-1; k<i+2; k++) {
					if (k<m_extent.lo.x || k> m_extent.hi.x) continue;
					for (l=j-1; l<j+2; l++) {
						if (l<m_extent.lo.y || l> m_extent.hi.y) continue;
						if (m_map[k][l].getType() == PathfindCell::CELL_CLEAR) {
							m_map[k][l].setPinched(true);	
						}

					}
				}
			}
		}
	}
	// Convert pinched to cliff.
	for( j=m_extent.lo.y; j<=m_extent.hi.y; j++ )
	{
		for( i=m_extent.lo.x; i<=m_extent.hi.x; i++ )
		{
			if (m_map[i][j].getPinched()) {
				if (m_map[i][j].getType()==PathfindCell::CELL_CLEAR) {
					m_map[i][j].setType(PathfindCell::CELL_CLIFF);
				}
			}
		}
	}
	// Add a border of pinched cells to cliffs.
	for( j=m_extent.lo.y; j<=m_extent.hi.y; j++ )
	{
		for( i=m_extent.lo.x; i<=m_extent.hi.x; i++ )
		{
			if (m_map[i][j].getType() & PathfindCell::CELL_CLIFF) {
				Int k, l;
				for (k=i-1; k<i+2; k++) {
					if (k<m_extent.lo.x || k> m_extent.hi.x) continue;
					for (l=j-1; l<j+2; l++) {
						if (l<m_extent.lo.y || l> m_extent.hi.y) continue;
						if (m_map[k][l].getType() == PathfindCell::CELL_CLEAR) {
							m_map[k][l].setPinched(true);	
						}

					}
				}
			}
		}
	}
#endif
	for (i=0; i<LAYER_LAST; i++) {
		if (!m_layers[i].isUnused()) {
			m_layers[i].classifyCells();
		}
	}
	if (!m_layers[LAYER_WALL].isUnused()) {
		m_layers[LAYER_WALL].classifyWallCells(m_wallPieces, m_numWallPieces);
	}
	m_zoneManager.calculateZones(m_map, m_layers, m_extent);
}


/**
 * Force pathfind map recomputation.
 */
void Pathfinder::forceMapRecalculation( void )
{
	classifyMap( );
}

/**
 * Show all cells touched in the last search
 */
void Pathfinder::debugShowSearch(  Bool pathFound  )
{
	if (!TheGlobalData->m_debugAI) {
		return;
	}
#if defined _DEBUG
	extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);

	// show all explored cells for debugging
	PathfindCell *s;

		
	RGBColor color;
	color.red = color.blue = color.green = 1;
	if (!pathFound) {
		addIcon(nullptr, 0, 0, color);	 // erase.
	}

	for( Int openNdx = 0; openNdx < m_openList.getCount(); openNdx++ )	// heap order is fine here
	{
		s = m_openList.getCellAt(openNdx);
		// create objects to show path - they decay
		RGBColor color3;
		color3.red = color3.green = 0;
		color3.blue = 1;

		Coord3D pos;
		pos.x = (static_cast<Real>(s->getXIndex()) + 0.5f) * PATHFIND_CELL_SIZE_F;
		pos.y = (static_cast<Real>(s->getYIndex()) + 0.5f) * PATHFIND_CELL_SIZE_F;
		pos.z = TheTerrainLogic->getLayerHeight( pos.x, pos.y, s->getLayer() ) + 0.5f;
		addIcon(&pos, PATHFIND_CELL_SIZE_F*.6f, 200, color3);
	}

	for( s = m_closedList; s; s=s->getNextOpen() )
	{
		// create objects to show path - they decay
		// create objects to show path - they decay
		RGBColor color2;
		color2.red = color2.blue = 1;
		color2.green = 0;
		if (!pathFound)	color2.blue = 0;

		Int length=200;
		if (!pathFound) 
			length *= 2;

		Coord3D pos;
		pos.x = (static_cast<Real>(s->getXIndex()) + 0.5f) * PATHFIND_CELL_SIZE_F;
		pos.y = (static_cast<Real>(s->getYIndex()) + 0.5f) * PATHFIND_CELL_SIZE_F;
		pos.z = TheTerrainLogic->getLayerHeight( pos.x, pos.y, s->getLayer()) + 0.5f;
		addIcon(&pos, PATHFIND_CELL_SIZE_F*.6f, length, color2);
	}
#endif
}

//
// Releases the cells on the open & closed lists.
//
void Pathfinder::cleanOpenAndClosedLists(void) {
	Int count = 0;
	if (!m_openList.isEmpty()) {
		count += m_openList.releaseAll();
	}		 
	if (m_closedList) {
		count += PathfindCell::releaseClosedList(m_closedList);
		m_closedList = nullptr;
	}		 
	m_cumulativeCellsAllocated += count;
	m_statCellsReleased += count;
	nextLineMemoGeneration();
//#ifdef _DEBUG
//#endif

}


 































//-----------------------------------------------------------------------------





//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------





//-----------------------------------------------------------------------------
void Pathfinder::xferCellsForDiagnosis( Xfer *xfer )
{
	// no map yet: the deep CRC runs at the reset of the subsystems, before any map is loaded
	if( m_map == nullptr )
		return;
	for( Int x = m_extent.lo.x; x <= m_extent.hi.x; ++x )
	{
		for( Int y = m_extent.lo.y; y <= m_extent.hi.y; ++y )
		{
			PathfindCell *cell = getCell( LAYER_GROUND, x, y );
			if( cell == nullptr )
				continue;
			Int values[ 9 ] = { x, y, static_cast<Int>( cell->getGoalUnit() ), static_cast<Int>( cell->getPosUnit() ),
				static_cast<Int>( cell->getGoalAircraft() ), static_cast<Int>( cell->getObstacleID() ),
				static_cast<Int>( cell->getType() ), static_cast<Int>( cell->getFlags() ), static_cast<Int>( cell->getZone() ) };
			for( Int &v : values )
				xfer->xferInt( &v );
		}
	}
}

void Pathfinder::crc( Xfer *xfer )
{
	CRCDEBUG_LOG(("Pathfinder::crc() on frame %d\n", TheGameLogic->getFrame()));
	CRCDEBUG_LOG(("beginning CRC: %8.8X\n", ((XferCRC *)xfer)->getCRC()));

	xfer->xferUser( &m_extent, sizeof(IRegion2D) );
	CRCDEBUG_LOG(("m_extent: %8.8X\n", ((XferCRC *)xfer)->getCRC()));

	xfer->xferBool( &m_isMapReady );
	CRCDEBUG_LOG(("m_isMapReady: %8.8X\n", ((XferCRC *)xfer)->getCRC()));
	xfer->xferBool( &m_isTunneling );
	CRCDEBUG_LOG(("m_isTunneling: %8.8X\n", ((XferCRC *)xfer)->getCRC()));

	Int obsolete1 = 0;
	xfer->xferInt( &obsolete1 );

	xfer->xferUser(&m_ignoreObstacleID, sizeof(ObjectID));
	CRCDEBUG_LOG(("m_ignoreObstacleID: %8.8X\n", ((XferCRC *)xfer)->getCRC()));

	xfer->xferUser(m_queuedPathfindRequests, sizeof(ObjectID)*PATHFIND_QUEUE_LEN);
	CRCDEBUG_LOG(("m_queuedPathfindRequests: %8.8X\n", ((XferCRC *)xfer)->getCRC()));
	xfer->xferInt(&m_queuePRHead);
	CRCDEBUG_LOG(("m_queuePRHead: %8.8X\n", ((XferCRC *)xfer)->getCRC()));
	xfer->xferInt(&m_queuePRTail);
	CRCDEBUG_LOG(("m_queuePRTail: %8.8X\n", ((XferCRC *)xfer)->getCRC()));

	xfer->xferInt(&m_numWallPieces);
	CRCDEBUG_LOG(("m_numWallPieces: %8.8X\n", ((XferCRC *)xfer)->getCRC()));
	for (Int i=0; i<MAX_WALL_PIECES; ++i)
	{
		xfer->xferObjectID(&m_wallPieces[i]);
	}
	CRCDEBUG_LOG(("m_wallPieces: %8.8X\n", ((XferCRC *)xfer)->getCRC()));

	xfer->xferReal(&m_wallHeight);
	CRCDEBUG_LOG(("m_wallHeight: %8.8X\n", ((XferCRC *)xfer)->getCRC()));
	xfer->xferInt(&m_cumulativeCellsAllocated);
	CRCDEBUG_LOG(("m_cumulativeCellsAllocated: %8.8X\n", ((XferCRC *)xfer)->getCRC()));

}  // end crc

//-----------------------------------------------------------------------------
void Pathfinder::xfer( Xfer *xfer )
{

	// version
	// 2: the path request queue and the per-frame cell budget, so that the paths of a loaded game are
	//    computed in the same frames and order as without saving
	// 3: the zones of all cells and the zone tables (see PathfindZoneManager::xferZones)
	// 4: the unit markings of all cells
	XferVersion currentVersion = 4;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	if( version >= 2 )
	{
		xfer->xferBool( &m_isTunneling );
		xfer->xferObjectID( &m_ignoreObstacleID );
		for( Int i = 0; i < PATHFIND_QUEUE_LEN; ++i )
			xfer->xferObjectID( &m_queuedPathfindRequests[ i ] );
		xfer->xferInt( &m_queuePRHead );
		xfer->xferInt( &m_queuePRTail );
		xfer->xferInt( &m_cumulativeCellsAllocated );
	}

	if( version >= 3 )
	{
		IRegion2D extent = m_extent;
		xfer->xferUser( &extent, sizeof( IRegion2D ) );
		if( memcmp( &extent, &m_extent, sizeof( IRegion2D ) ) != 0 )
		{
			DEBUG_CRASH(( "Pathfinder::xfer - map extent mismatch" ));
			throw SaveCode::SC_INVALID_DATA;
		}
		for( Int x = m_extent.lo.x; m_map != nullptr && x <= m_extent.hi.x; ++x )
		{
			for( Int y = m_extent.lo.y; y <= m_extent.hi.y; ++y )
			{
				zoneStorageType zone = m_map[ x ][ y ].getZone();
				xfer->xferUnsignedShort( &zone );
				m_map[ x ][ y ].setZone( zone );
			}
		}
		for( Int i = LAYER_GROUND + 1; i <= LAYER_LAST; ++i )
		{
			Int zone = m_layers[ i ].getZone();
			xfer->xferInt( &zone );
			if( xfer->getXferMode() == XFER_LOAD && zone != m_layers[ i ].getZone() )
			{
				m_layers[ i ].setZone( zone );
				m_layers[ i ].applyZone();
			}
		}
		m_zoneManager.xferZones( xfer );
	}

	if( version >= 4 )
		xferUnitMarks( xfer );

}  // end xfer

//-----------------------------------------------------------------------------
PathfindCell *Pathfinder::cellForSave( Int layer, Int i, Int j )
{
	if( layer == LAYER_GROUND )
	{
		if( i < m_extent.lo.x || i > m_extent.hi.x || j < m_extent.lo.y || j > m_extent.hi.y )
			return nullptr;
		return &m_map[ i ][ j ];
	}
	if( layer <= LAYER_GROUND || layer > LAYER_LAST )
		return nullptr;
	return m_layers[ layer ].friend_getCellAtIndex( i, j );
}

//-----------------------------------------------------------------------------
void Pathfinder::xferUnitMarks( Xfer *xfer )
{
	if( xfer->getXferMode() != XFER_LOAD )
	{
		std::vector<SavedUnitMark> marks;
		if( m_map == nullptr )
		{
			Int none = 0;
			xfer->xferInt( &none );
			return;
		}
		auto note = [&marks]( Int layer, Int i, Int j, PathfindCell *cell )
		{
			if( cell == nullptr )
				return;
			SavedUnitMark m;
			m.layer = layer;
			m.i = i;
			m.j = j;
			m.goalUnit = cell->getGoalUnit();
			m.posUnit = cell->getPosUnit();
			m.goalAircraft = cell->getGoalAircraft();
			m.flags = static_cast<Int>( cell->getFlags() );
			if( m.goalUnit != INVALID_ID || m.posUnit != INVALID_ID || m.goalAircraft != INVALID_ID || m.flags != 0 )
				marks.push_back( m );
		};
		for( Int i = m_extent.lo.x; i <= m_extent.hi.x; ++i )
			for( Int j = m_extent.lo.y; j <= m_extent.hi.y; ++j )
				note( LAYER_GROUND, i, j, &m_map[ i ][ j ] );
		for( Int layer = LAYER_GROUND + 1; layer <= LAYER_LAST; ++layer )
			for( Int i = 0; i < m_layers[ layer ].friend_getWidth(); ++i )
				for( Int j = 0; j < m_layers[ layer ].friend_getHeight(); ++j )
					note( layer, i, j, m_layers[ layer ].friend_getCellAtIndex( i, j ) );

		Int count = static_cast<Int>( marks.size() );
		xfer->xferInt( &count );
		for( SavedUnitMark &m : marks )
		{
			xfer->xferInt( &m.layer );
			xfer->xferInt( &m.i );
			xfer->xferInt( &m.j );
			xfer->xferObjectID( &m.goalUnit );
			xfer->xferObjectID( &m.posUnit );
			xfer->xferObjectID( &m.goalAircraft );
			xfer->xferInt( &m.flags );
		}
		return;
	}

	m_loadedUnitMarks.clear();
	Int count = 0;
	xfer->xferInt( &count );
	for( Int k = 0; k < count; ++k )
	{
		SavedUnitMark m;
		xfer->xferInt( &m.layer );
		xfer->xferInt( &m.i );
		xfer->xferInt( &m.j );
		xfer->xferObjectID( &m.goalUnit );
		xfer->xferObjectID( &m.posUnit );
		xfer->xferObjectID( &m.goalAircraft );
		xfer->xferInt( &m.flags );
		m_loadedUnitMarks.push_back( m );
	}
	m_loadedUnitMarksPending = TRUE;
}

//-----------------------------------------------------------------------------
void Pathfinder::friend_applyLoadedUnitMarks( void )
{
	if( !m_loadedUnitMarksPending )
		return;
	m_loadedUnitMarksPending = FALSE;
	if( m_map == nullptr )
		return;

	// the units marked their cells again while loading; replace that by the saved markings
	ICoord2D pos;
	for( Int i = m_extent.lo.x; i <= m_extent.hi.x; ++i )
	{
		for( Int j = m_extent.lo.y; j <= m_extent.hi.y; ++j )
		{
			pos.x = i;
			pos.y = j;
			m_map[ i ][ j ].friend_setUnitMarks( INVALID_ID, INVALID_ID, INVALID_ID, PathfindCell::NO_UNITS, pos );
		}
	}
	for( Int layer = LAYER_GROUND + 1; layer <= LAYER_LAST; ++layer )
	{
		for( Int i = 0; i < m_layers[ layer ].friend_getWidth(); ++i )
		{
			for( Int j = 0; j < m_layers[ layer ].friend_getHeight(); ++j )
			{
				PathfindCell *cell = m_layers[ layer ].friend_getCellAtIndex( i, j );
				pos.x = m_layers[ layer ].friend_getOrigin().x + i;
				pos.y = m_layers[ layer ].friend_getOrigin().y + j;
				if( cell )
					cell->friend_setUnitMarks( INVALID_ID, INVALID_ID, INVALID_ID, PathfindCell::NO_UNITS, pos );
			}
		}
	}

	for( const SavedUnitMark &m : m_loadedUnitMarks )
	{
		PathfindCell *cell = cellForSave( m.layer, m.i, m.j );
		if( cell == nullptr )
			continue;
		pos.x = m.i;
		pos.y = m.j;
		if( m.layer != LAYER_GROUND )
		{
			pos.x += m_layers[ m.layer ].friend_getOrigin().x;
			pos.y += m_layers[ m.layer ].friend_getOrigin().y;
		}
		cell->friend_setUnitMarks( m.goalUnit, m.posUnit, m.goalAircraft, m.flags, pos );
	}
	m_loadedUnitMarks.clear();
}

//-----------------------------------------------------------------------------
void Pathfinder::loadPostProcess( void )
{

}  // end loadPostProcess
