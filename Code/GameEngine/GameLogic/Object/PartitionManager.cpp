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

// FILE: PartitionManager.cpp ////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: PartitionManager.cpp
//
// Created:   Steven Johnson, September 2001
//
// Desc:      Partition management, this system will allow us to partition the
//					  objects in space, iterate objects in specified volumes, 
//					  regions, by types and other properties.
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//         Includes                                                      
//-----------------------------------------------------------------------------
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

#ifdef _DEBUG
//#include "GameClient/InGameUI.h"	// for debugHints
#include "Common/PlayerList.h"
#endif

#ifdef PM_CACHE_TERRAIN_HEIGHT
#include "Common/MapObject.h"
#endif

#include "PartitionManagerInternal.h"

#ifdef DUMP_PERF_STATS
	long s_countInClosestObjects = 0;
	long s_countInClosestObjectsThisFrame = 0;
	Int64 s_timeInClosestObjects = 0;
	Int64 s_timeInClosestObjectsThisFrame = 0;
	UnsignedInt s_gcoPerfFrame = 0xffffffff;
#endif 

extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);

//------------------------------------------------------------------------------ Performance Timers 
//#include "Common/PerfMetrics.h"
//#include "Common/PerfTimer.h"

//-------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//         Defines                                                         
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
PartitionContactList* TheContactList = nullptr;

//-----------------------------------------------------------------------------
//         Local Functions                                                      
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

static void projectCoord3D(Coord3D *coord, const Coord3D *unitDir, Real dist);
static void flipCoord3D(Coord3D *coord);

static void rectToFourPoints(
	const CollideInfo *a,		// z is ignored
	Coord2D pts[]
);
static void testRotatedPointsAgainstRect(
	const Coord2D *pts,				// an array of 4
	const CollideInfo *a,
	Coord2D *avg,
	Int *avgTot
);

static Bool xy_collideTest_Rect_Rect(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool xy_collideTest_Rect_Circle(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool xy_collideTest_Circle_Rect(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);

static Bool collideTest_Sphere_Sphere(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool collideTest_Sphere_Cylinder(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool collideTest_Sphere_Box(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool collideTest_Cylinder_Sphere(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool collideTest_Cylinder_Cylinder(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool collideTest_Cylinder_Box(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool collideTest_Box_Sphere(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool collideTest_Box_Cylinder(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool collideTest_Box_Box(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);

static Bool distCalcProc_CenterAndCenter_2D(const Coord3D *posA, const Object *objA, const Coord3D *posB, const Object *objB, Real& abDistSqr, Coord3D& abVec, Real maxDistSqr); 
static Bool distCalcProc_BoundaryAndBoundary_2D(const Coord3D *posA, const Object *objA, const Coord3D *posB, const Object *objB, Real& abDistSqr, Coord3D& abVec, Real maxDistSqr); 
static Bool distCalcProc_CenterAndCenter_3D(const Coord3D *posA, const Object *objA, const Coord3D *posB, const Object *objB, Real& abDistSqr, Coord3D& abVec, Real maxDistSqr); 
static Bool distCalcProc_BoundaryAndBoundary_3D(const Coord3D *posA, const Object *objA, const Coord3D *posB, const Object *objB, Real& abDistSqr, Coord3D& abVec, Real maxDistSqr); 

//-----------------------------------------------------------------------------
inline void projectCoord3D(Coord3D *coord, const Coord3D *unitDir, Real dist)
{
	coord->x += unitDir->x * dist;
	coord->y += unitDir->y * dist;
	coord->z += unitDir->z * dist;
}

//-----------------------------------------------------------------------------
inline void flipCoord3D(Coord3D *coord)
{
	coord->x = -coord->x;
	coord->y = -coord->y;
	coord->z = -coord->z;
}

//-----------------------------------------------------------------------------
static void testRotatedPointsAgainstRect(
	const Coord2D *pts,				// an array of 4
	const CollideInfo *a,
	Coord2D *avg,
	Int *avgTot
)
{
	//DEBUG_ASSERTCRASH(a->geom.getGeomType() == GEOMETRY_BOX, ("only boxes are ok here"));
	Real major = a->geom.getMajorRadius();
	Real minor = (a->geom.getGeomType() == GEOMETRY_SPHERE) ? a->geom.getMajorRadius() : a->geom.getMinorRadius();

	Real c = static_cast<Real>(Cos(-a->angle));
	Real s = static_cast<Real>(Sin(-a->angle));

	for (Int i = 0; i < 4; ++i, ++pts)
	{
		// convert to a delta relative to rect ctr
		Real ptx = pts->x - a->position.x;
		Real pty = pts->y - a->position.y;

		// inverse-rotate it to the right coord system
		Real ptx_new = static_cast<Real>(fabs(ptx*c - pty*s));
		Real pty_new = static_cast<Real>(fabs(ptx*s + pty*c));

		#ifdef INTENSE_DEBUG
		Real mag_a = sqr(ptx)+sqr(pty);
		Real mag_b = sqr(ptx_new)+sqr(pty_new);
		DEBUG_ASSERTCRASH(fabs(mag_a - mag_b) <= 1.0, ("hmm, unlikely")); 
		#endif

		if (ptx_new <= major && pty_new <= minor)
		{
			avg->x += pts->x;
			avg->y += pts->y;
			*avgTot += 1;
		}
	}
}

//-----------------------------------------------------------------------------
static void rectToFourPoints(
	const CollideInfo *a,		// z is ignored
	Coord2D pts[]
)
{
	Real c = static_cast<Real>(Cos(a->angle));
	Real s = static_cast<Real>(Sin(a->angle));

	Real exc = a->geom.getMajorRadius()*c;
	Real eyc = a->geom.getMinorRadius()*c;
	Real exs = a->geom.getMajorRadius()*s;
	Real eys = a->geom.getMinorRadius()*s;

	// tl
	pts[0].x = a->position.x - exc - eys;
	pts[0].y = a->position.y + eyc - exs;

	// tr
	pts[1].x = a->position.x + exc - eys;
	pts[1].y = a->position.y + eyc + exs;

	// bl
	pts[2].x = a->position.x - exc + eys;
	pts[2].y = a->position.y - eyc - exs;

	// br
	pts[3].x = a->position.x + exc + eys;
	pts[3].y = a->position.y - eyc + exs;
}

//-----------------------------------------------------------------------------
/**
	2d utility routine for rect/cyl & rect/sphere collisions testing.
*/
static Bool xy_collideTest_Circle_Rect(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	Bool result = xy_collideTest_Rect_Circle(b, a, cinfo);
	if (cinfo)
		flipCoord3D(&cinfo->normal);
	return result;
}

//-----------------------------------------------------------------------------
/**
	2d utility routine for rect/cyl & rect/sphere collisions testing.
*/
static Bool xy_collideTest_Rect_Circle(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
#if 1
	/// @todo srj -- this is better than the other one, since it actually handles rotated rects,
	// but still not as accurate as is could be. (srj)
	CollideInfo btmp = *b;
	btmp.geom.setMinorRadius(btmp.geom.getMajorRadius());
	return xy_collideTest_Rect_Rect(a, &btmp, cinfo);
#else
	// note, this actually tests the intersection of the the rect with the circle's
	// bounding box. in practice, this is usually good enough, since most of
	// our sphere/cyl shapes are small relative to boxes. (in fact, the WWMath
	// library takes a similar shortcut when colliding spheres with boxes in 3d.
	// so I figured it was probably good enough for us too.)

	Real circ_l = b->position.x - b->geom.getMajorRadius();
	Real circ_r = b->position.x + b->geom.getMajorRadius();
	Real circ_t = b->position.y - b->geom.getMajorRadius();
	Real circ_b = b->position.y + b->geom.getMajorRadius();
	Real rect_l = a->position.x - a->geom.getMajorRadius();
	Real rect_r = a->position.x + a->geom.getMajorRadius();
	Real rect_t = a->position.y - a->geom.getMinorRadius();
	Real rect_b = a->position.y + a->geom.getMinorRadius();

	if (circ_r >= rect_l &&	circ_l <= rect_r &&
		circ_b >= rect_t &&	circ_t <= rect_b)
	{
		if (cinfo)
		{
			vecDiff_2D(&b->position, &a->position, &cinfo->normal);
			cinfo->normal.normalize();
			cinfo->loc.x = (maxReal(circ_l, rect_l) + minReal(circ_r, rect_r)) * 0.5f;
			cinfo->loc.y = (maxReal(circ_t, rect_t) + minReal(circ_b, rect_b)) * 0.5f;
			cinfo->loc.z = (a->position.z + b->position.z) * 0.5f;
		}
		return true;
	}
	return false;
#endif
}

//-----------------------------------------------------------------------------
/**
	2d utility routine for cyl & sphere collisions testing.
*/
static Bool xy_collideTest_Circle_Circle(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	Coord3D diff;
	vecDiff_2D(&b->position, &a->position, &diff);
	Real distSqr = calcSqrDist_2D(&diff);
	Real touchingDistSqr = sqr(a->geom.getMajorRadius() + b->geom.getMajorRadius());
	if (distSqr <= touchingDistSqr)
	{
		// calc the approximate location and normal.
		if (cinfo)
		{
			cinfo->normal = diff;
			cinfo->normal.normalize();
			cinfo->loc = a->position;
			projectCoord3D(&cinfo->loc, &cinfo->normal, a->geom.getMajorRadius());
		}
		
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
/**
	2d utility routine for rect collisions testing.
*/
static Bool xy_collideTest_Rect_Rect(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	Coord2D pts[4];
	Coord2D avg; avg.x = avg.y = 0.0f;
	Int avgTot = 0;

	rectToFourPoints(a, pts);
	testRotatedPointsAgainstRect(pts, b, &avg, &avgTot);

	rectToFourPoints(b, pts);
	testRotatedPointsAgainstRect(pts, a, &avg, &avgTot);
	
	if (avgTot > 0)
	{
		if (cinfo)
		{
			// this is a little hokey, but generally adequate...
			cinfo->loc.x = avg.x / avgTot;
			cinfo->loc.y = avg.y / avgTot;
			cinfo->loc.z = (a->position.z + b->position.z) * 0.5f;

			// yow, what exactly does it mean to find the normal to two *intersecting*
			// rects? with "true" physics we could project ahead and determine 
			// what the collision point would be, which would of course always
			// give a perfect, symmetrical normal. however, we almost always determine
			// collisions "after the fact" and thus the rects already intersect, meaning
			// that it's tricky to find the normal. we could do it the "right" way, which 
			// would be to back up the objects to find the perfect point (see above), but
			// we don't have velocity information at this point, so we can't really
			// do that. so, naturally, we provide a (poor) guess by just doing the center-to-center
			// normal. this is rarely the correct surface normal for a box-box collision,
			// but until I either (a) rewrite everything to have predictive collisions, 
			// or (b) come up with a better definition of a useful normal in this case,
			// I'm not sure we can do a whole lot better... (srj)
			vecDiff_2D(&b->position, &a->position, &cinfo->normal);
			cinfo->normal.normalize();
		}
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
inline Bool z_collideTest_Sphere_Nonsphere(CollideTestProc xyproc, const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	// case 1: center of sphere is within the nonsphere z-space.
	if (a->position.z >= b->position.z && a->position.z <= b->position.z + b->geom.getMaxHeightAbovePosition())
	{
		if (xyproc(a, b, cinfo))
		{
			// a side-to-side collision; just need to adjust the z-coord of collideLoc
			if (cinfo)
			{
				cinfo->loc.z = a->position.z;
			}
			return true;
		}
		return false;
	}

	// case 2: center of sphere is above or below nonsphere, but within
	// major-radius of top or bottom.

	// 2a: sphere is below the nonsphere
	Real b_bot = b->position.z;
	if (a->position.z < b_bot && a->position.z + a->geom.getMajorRadius() >= b_bot)
	{
		// find the radius of the slice of the sphere that is at b_bot
		CollideInfo amod = *a;
		amod.position.z = b_bot;
		amod.geom.setMajorRadius(static_cast<Real>(sqrtf(sqr(a->geom.getMajorRadius()) - sqr(b_bot - a->position.z))));
		if (xyproc(&amod, b, cinfo))
		{
			// if you want to have 'end' collisions, you should add something like:
			//		if (circle a is contained by circle b) normal points straight up;
			// but since that's currently never possible (and even if it was, wouldn't matter)
			// we don't bother with the calculation.
			return true;
		}
		return false;
	}

	// 2b: sphere is above the nonsphere
	Real b_top = b->position.z + b->geom.getMaxHeightAbovePosition();
	if (a->position.z > b_top && a->position.z - a->geom.getMajorRadius() <= b_top)
	{
		CollideInfo amod = *a;
		amod.position.z = b_top;
		amod.geom.setMajorRadius(static_cast<Real>(sqrtf(sqr(a->geom.getMajorRadius()) - sqr(a->position.z - b_top))));
		if (xyproc(&amod, b, cinfo))
		{
			// if you want to have 'end' collisions, you should add something like:
			//		if (circle a is contained by circle b) normal points straight down;
			// but since that's currently never possible (and even if it was, wouldn't matter)
			// we don't bother with the calculation.
			return true;
		}
		return false;
	}

	return false;
}

//-----------------------------------------------------------------------------
inline Bool z_collideTest_Nonsphere_Nonsphere(CollideTestProc xyproc, const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	// note: we already know that there is a z-intersection (our caller filters that out)
	// so we need not recheck that here.

	// Preflight for close objects.
	Real dSqr = sqr(a->position.x-b->position.x) + sqr(a->position.y-b->position.y);
	Real minRadius = a->geom.getMajorRadius();
	Real r = a->geom.getMinorRadius();
	if (minRadius>r) minRadius = r;
	r = b->geom.getMajorRadius();
	if (minRadius>r) minRadius = r;
	r = b->geom.getMinorRadius();
	if (minRadius>r) minRadius = r;
	
	Bool closeEnough = sqr(minRadius) > dSqr;

	if (closeEnough || xyproc(a, b, cinfo))
	{
		// since we allow objects to overlap at times, how do we distinguish
		// between a "top-to-bottom" vs "side-to-side" collision?
		// currently, we don't: cylindrical shapes currently never collide
		// top-to-bottom, so we just declare everything to be side-to-side.
		// if this changes, need to smarten this.

		// case 1: side-to-side collision
		// just need to adjust the z-coord of collideLoc
		if (cinfo)
		{
			if (b->position.z > a->position.z)
				cinfo->loc.z = (b->position.z + a->position.z + a->geom.getMaxHeightAbovePosition()) * 0.5f;
			else
				cinfo->loc.z = (a->position.z + b->position.z + b->geom.getMaxHeightAbovePosition()) * 0.5f;
		}
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
static Bool collideTest_Sphere_Sphere(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	Coord3D diff;
	vecDiff_3D(&b->position, &a->position, &diff);
	Real distSqr = calcSqrDist_3D(&diff);
	Real touchingDistSqr = sqr(a->geom.getMajorRadius() + b->geom.getMajorRadius());
	if (distSqr <= touchingDistSqr)
	{
		if (cinfo)
		{
			cinfo->normal = diff;
			cinfo->normal.normalize();
			cinfo->loc = a->position;
			projectCoord3D(&cinfo->loc, &cinfo->normal, a->geom.getMajorRadius());
		}

		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
static Bool collideTest_Sphere_Cylinder(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	return z_collideTest_Sphere_Nonsphere(xy_collideTest_Circle_Circle, a, b, cinfo);
}

//-----------------------------------------------------------------------------
static Bool collideTest_Sphere_Box(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	return z_collideTest_Sphere_Nonsphere(xy_collideTest_Circle_Rect, a, b, cinfo);
}

//-----------------------------------------------------------------------------
static Bool collideTest_Cylinder_Sphere(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	Bool result = z_collideTest_Sphere_Nonsphere(xy_collideTest_Circle_Circle, b, a, cinfo);
	if (cinfo)
		flipCoord3D(&cinfo->normal);
	return result;
}

//-----------------------------------------------------------------------------
static Bool collideTest_Cylinder_Cylinder(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	return z_collideTest_Nonsphere_Nonsphere(xy_collideTest_Circle_Circle, a, b, cinfo);
}

//-----------------------------------------------------------------------------
static Bool collideTest_Cylinder_Box(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	return z_collideTest_Nonsphere_Nonsphere(xy_collideTest_Circle_Rect, a, b, cinfo);
}

//-----------------------------------------------------------------------------
static Bool collideTest_Box_Sphere(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	Bool result = z_collideTest_Sphere_Nonsphere(xy_collideTest_Circle_Rect, b, a, cinfo);
	if (cinfo)
		flipCoord3D(&cinfo->normal);
	return result;
}

//-----------------------------------------------------------------------------
static Bool collideTest_Box_Cylinder(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	return z_collideTest_Nonsphere_Nonsphere(xy_collideTest_Rect_Circle, a, b, cinfo);
}

//-----------------------------------------------------------------------------
static Bool collideTest_Box_Box(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	return z_collideTest_Nonsphere_Nonsphere(xy_collideTest_Rect_Rect, a, b, cinfo);
}

//-----------------------------------------------------------------------------
static Bool distCalcProc_CenterAndCenter_2D(
	const Coord3D *posA, 
	const Object *objA, 
	const Coord3D *posB, 
	const Object *objB, 
	Real& abDistSqr, 
	Coord3D& abVec,
	Real maxDistSqr
)
{
	// note that object positions are defined as the bottom center of the geometry,
	// thus we must add the radius to the z coord to get the proper center of the bounding sphere.
	Coord3D diff;
	diff.x = posB->x - posA->x;
	diff.y = posB->y - posA->y;
	diff.z = 0.0f;
	
	//if (abDistSqr)
	{
		abDistSqr = sqr(diff.x) + sqr(diff.y);
	}

	//if (abVec)
	{
		abVec = diff;
	}

	return abDistSqr < maxDistSqr;
}

//-----------------------------------------------------------------------------
static Bool distCalcProc_BoundaryAndBoundary_2D(
	const Coord3D *posA, 
	const Object *objA, 
	const Coord3D *posB, 
	const Object *objB, 
	Real& abDistSqr, 
	Coord3D& abVec,
	Real maxDistSqr
)
{
	Coord3D diff;
	diff.x = posB->x - posA->x;
	diff.y = posB->y - posA->y;
	diff.z = 0.0f;
	
	Real actualDistSqr = sqr(diff.x) + sqr(diff.y);

	Real shrinkFactor = 1.0f;
	Real shrunkenDistSqr = actualDistSqr;
	Real totalRad = (objA ? objA->getGeometryInfo().getBoundingCircleRadius() : 0.0f) + 
							(objB ? objB->getGeometryInfo().getBoundingCircleRadius() : 0.0f);

	if (totalRad > 0.0f)
	{
		Real actualDist = sqrtf(actualDistSqr);
		Real shrunkenDist = actualDist - totalRad;
		if (shrunkenDist <= 0.0f) 
		{
			shrinkFactor = 0.0f;
			shrunkenDistSqr = 0.0f;	// sorry, distances can't be negative
		}
		else 
		{
			shrinkFactor = shrunkenDist / actualDist;
			shrunkenDistSqr = sqr(shrunkenDist);
		}
	}

	//if (abDistSqr)
	{
		abDistSqr = shrunkenDistSqr;
	}

	//if (abVec)
	{
		DEBUG_ASSERTCRASH(shrinkFactor >= 0.0f && shrinkFactor <= 1.0f, ("Hmm, this should not be possible."));
		diff.x *= shrinkFactor;
		diff.y *= shrinkFactor;
		abVec = diff;
	}
	return abDistSqr < maxDistSqr;
}

//-----------------------------------------------------------------------------
static Bool distCalcProc_CenterAndCenter_3D(
	const Coord3D *posA, 
	const Object *objA, 
	const Coord3D *posB, 
	const Object *objB, 
	Real& abDistSqr, 
	Coord3D& abVec,
	Real maxDistSqr
)
{
	// note that object positions are defined as the bottom center of the geometry,
	// thus we must add the radius to the z coord to get the proper center of the bounding sphere.
	Coord3D diff;
	diff.x = posB->x - posA->x;
	diff.y = posB->y - posA->y;
	diff.z = posB->z - posA->z;
	
	//if (abDistSqr)
	{
		abDistSqr = sqr(diff.x) + sqr(diff.y) + sqr(diff.z);
	}

	//if (abVec)
	{
		abVec = diff;
	}
	return abDistSqr < maxDistSqr;
}

//-----------------------------------------------------------------------------
static Bool distCalcProc_BoundaryAndBoundary_3D(
	const Coord3D *posA, 
	const Object *objA, 
	const Coord3D *posB, 
	const Object *objB, 
	Real& abDistSqr, 
	Coord3D& abVec,
	Real maxDistSqr
)
{
	const GeometryInfo* geomA = objA ? &objA->getGeometryInfo() : nullptr;
	const GeometryInfo* geomB = objB ? &objB->getGeometryInfo() : nullptr;

	// note that object positions are defined as the bottom center of the geometry,
	// thus we must add the radius to the z coord to get the proper center of the bounding sphere.
	Coord3D diff;
	diff.x = posB->x - posA->x;
	diff.y = posB->y - posA->y;
	diff.z = ((posB->z + (geomB ? geomB->getZDeltaToCenterPosition() : 0.0f)) 
						- (posA->z + (geomA ? geomA->getZDeltaToCenterPosition() : 0.0f)));
	
	Real actualDistSqr = sqr(diff.x) + sqr(diff.y) + sqr(diff.z);

	Real shrinkFactor = 1.0f;
	Real shrunkenDistSqr = actualDistSqr;
	Real totalRad = (geomA?geomA->getBoundingSphereRadius():0) + (geomB?geomB->getBoundingSphereRadius():0);
	if (totalRad > 0.0f)
	{
		Real actualDist = sqrtf(actualDistSqr);
		Real shrunkenDist = actualDist - totalRad;
		if (shrunkenDist <= 0.0f) 
		{
			shrinkFactor = 0.0f;
			shrunkenDistSqr = 0.0f;	// sorry, distances can't be negative
		}
		else 
		{
			shrinkFactor = shrunkenDist / actualDist;
			shrunkenDistSqr = sqr(shrunkenDist);
		}
	}

	//if (abDistSqr)
	{
		abDistSqr = shrunkenDistSqr;
	}

	//if (abVec)
	{
		DEBUG_ASSERTCRASH(shrinkFactor >= 0.0f && shrinkFactor <= 1.0f, ("Hmm, this should not be possible."));
		diff.x *= shrinkFactor;
		diff.y *= shrinkFactor;
		diff.z *= shrinkFactor;
		abVec = diff;
	}
	return abDistSqr < maxDistSqr;
}

//-----------------------------------------------------------------------------
//         Private Types                                                     
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//         Private Data                                                     
//-----------------------------------------------------------------------------

DistCalcProc theDistCalcProcs[] = 
{ 
	distCalcProc_CenterAndCenter_2D, 
	distCalcProc_CenterAndCenter_3D,
	distCalcProc_BoundaryAndBoundary_2D, 
	distCalcProc_BoundaryAndBoundary_3D, 
};

// NOTE: This *DEPENDS* on the order of the geometry enum defines
CollideTestProc theCollideTestProcs[] = 
{
	collideTest_Sphere_Sphere,
	collideTest_Sphere_Cylinder,
	collideTest_Sphere_Box,
	collideTest_Cylinder_Sphere,
	collideTest_Cylinder_Cylinder,
	collideTest_Cylinder_Box,
	collideTest_Box_Sphere,
	collideTest_Box_Cylinder,
	collideTest_Box_Box
};

//-----------------------------------------------------------------------------
//         Public Data                                                      
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//Bool PartitionCell::isShroudedForPlayer( Int playerIndex ) const
//{
	// There isn't an absolute answer.  This cell is only shrouded in regards to a person
//	return (m_shroudLevel[playerIndex].m_currentShroud == 1);
//}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
/* See if thisObj collides with geom at pos & angle. */
Bool PartitionManager::geomCollidesWithGeom(const Coord3D* pos1, 
		const GeometryInfo& geom1,
		Real angle1,
		const Coord3D* pos2, 
		const GeometryInfo& geom2,
		Real angle2) const
{
	CollideInfo thisInfo(pos1, geom1, angle1);
	CollideInfo thatInfo(pos2, geom2, angle2);

	// invariant for all geometries: first do z collision check.
	if (thisInfo.position.z + thisInfo.geom.getMaxHeightAbovePosition() >= thatInfo.position.z && 
			thisInfo.position.z <= thatInfo.position.z + thatInfo.geom.getMaxHeightAbovePosition())
	{
		GeometryType thisGeom = geom1.getGeomType();
		GeometryType thatGeom = geom2.getGeomType();

		//
		// NOTE: This assumes geometry enumerations that start at GEOMETRY_FIRST AND depends on the
		// order in which they appear in the enum list
		//
		CollideTestProc collideProc = theCollideTestProcs[ (thisGeom - GEOMETRY_FIRST) * GEOMETRY_NUM_TYPES + (thatGeom - GEOMETRY_FIRST) ];
		CollideLocAndNormal cloc;
		return (*collideProc)(&thisInfo, &thatInfo, &cloc);
	}
	else
	{
		// no z-intersection -> no collision.
		return false;
	}
}

//-----------------------------------------------------------------------------
inline UnsignedInt hash2ints(Int a, Int b)
{
	// do it this way so that [a,b] always hashes to the same value as [b,a].
	// this is unsophisticated but reasonable, since all ObjectIDs will
	// quite likely be well below 65536...
	if (a < b)
	{
		return (a<<16)+b;
	}
	else
	{
		return (b<<16)+a;
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
void PartitionContactList::addToContactList( PartitionData *obj, PartitionData *other )
{
	if (obj == other || obj == nullptr || other == nullptr)
		return;
	
	Object* obj_obj = obj->getObject();
	Object* other_obj = other->getObject();
	if (obj_obj == nullptr || other_obj == nullptr)
		return;

	// compute hash index based on object's ids.
	UnsignedInt hashValue = hash2ints(obj_obj->getID(), other_obj->getID());
	hashValue %= PartitionContactList_SOCKET_COUNT;

	// make sure given hit has not already been recorded 
	for (PartitionContactListNode* cd = m_contactHash[ hashValue ]; cd; cd = cd->m_nextHash )
	{
		if ((cd->m_obj == obj && cd->m_other == other) ||
				(cd->m_obj == other && cd->m_other == obj)) 
		{
			// already noted 
			return;
		}
	}

	// new hit 
	PartitionContactListNode *ncd = newInstance(PartitionContactListNode);
	ncd->m_obj = obj;
	ncd->m_other = other;
	ncd->m_hashValue = hashValue;

	// add to hash table 
	ncd->m_nextHash = m_contactHash[ hashValue ];
	m_contactHash[ hashValue ] = ncd;

	// add to list of contacts for this frame 
	ncd->m_next = m_contactList;
	m_contactList = ncd;




}

//-----------------------------------------------------------------------------
void PartitionContactList::removeSpecificPartitionData(PartitionData* data)
{
	for (PartitionContactListNode* cd = m_contactList; cd; cd = cd->m_next)
	{
		if (cd->m_obj == data || cd->m_other == data)
		{
			cd->m_obj = nullptr;
			cd->m_other = nullptr;
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionContactList::resetContactList()
{
	// remove items from hash table 
	PartitionContactListNode* cdnext;
	for (PartitionContactListNode* cd = m_contactList; cd; cd = cdnext)
	{
		cdnext = cd->m_next;
		deleteInstanceOf(cd);
	}

	memset(m_contactHash, 0, sizeof(m_contactHash));
	m_contactList = nullptr;
}

//-----------------------------------------------------------------------------
void PartitionContactList::processContactList()
{
	for (PartitionContactListNode* cd = m_contactList; cd; cd = cd->m_next) 
	{
		if (cd->m_obj == nullptr || cd->m_other == nullptr)
			continue;

		// we know that their partitions overlap; determine if they REALLY collide 
		// before proceeding...
		CollideLocAndNormal cinfo;
		if (!cd->m_obj->friend_collidesWith(cd->m_other, &cinfo))
			continue;

		Object* obj = cd->m_obj->getObject();
		Object* other = cd->m_other->getObject();
		
		if( obj->getStatusBits().test( OBJECT_STATUS_NO_COLLISIONS ) ||
				other->getStatusBits().test( OBJECT_STATUS_NO_COLLISIONS ) )
			continue;

		DEBUG_ASSERTCRASH(!(obj->isKindOf(KINDOF_IMMOBILE) && other->isKindOf(KINDOF_IMMOBILE)), 
			("we should never have collisions between two immobile things reported"));

		// the onCollide() calls can remove the object(s) from the partition mgr,
		// thus destroying the partitiondata for 'em. go ahead and null these out here
		// so we won't be tempted to use 'em (since they might be bogus).
		cd->m_obj = nullptr;
		cd->m_other = nullptr;

		obj->onCollide(other, &cinfo.loc, &cinfo.normal);
		flipCoord3D(&cinfo.normal);

 		//Before checking the "other" case, make sure that the previous collision didn't
 		//absorb him. This becomes a conflict for pilots giving veterancy to transports
 		//and pilots entering transports. Both would occur if these isDestroyed checks 
 		//were missing.
 		if( !obj->isDestroyed() && !other->isDestroyed() )
 		{
 			other->onCollide(obj, &cinfo.loc, &cinfo.normal);
 		}

		//
		// NOTE: it is VERY IMPORTANT (for performance reasons) to not re-dirty immobile things.
		//
		// NOTE also that we re-get partitiondata from the object, since it might have been
		// removed from the partition system by the onCollide call...
		//
		if (!obj->isDestroyed() && obj->friend_getPartitionData() != nullptr && !obj->isKindOf(KINDOF_IMMOBILE))
		{
//DEBUG_LOG(("%d: re-dirtying collision of %s %08lx with %s %08lx\n",TheGameLogic->getFrame(),obj->getTemplate()->getName().str(),obj,other->getTemplate()->getName().str(),other));
			obj->friend_getPartitionData()->makeDirty(false);
		}
		if (!other->isDestroyed() && other->friend_getPartitionData() != nullptr && !other->isKindOf(KINDOF_IMMOBILE))
		{
//DEBUG_LOG(("%d: re-dirtying collision of %s %08lx with %s %08lx [other]\n",TheGameLogic->getFrame(),other->getTemplate()->getName().str(),other,obj->getTemplate()->getName().str(),obj));
			other->friend_getPartitionData()->makeDirty(false);
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
PartitionManager::PartitionManager()
{
	m_moduleList = nullptr;
	m_cellSize = m_cellSizeInv = 0.0f;
	m_cellCountX = 0;
	m_cellCountY = 0;
	m_totalCellCount = 0;
	m_cells = nullptr;
	m_worldExtents.lo.zero();
	m_worldExtents.hi.zero();
	m_dirtyModules = nullptr;
	m_updatedSinceLastReset = false;
	m_loadedOrderPending = FALSE;
#ifdef FASTER_GCO
	m_maxGcoRadius = 0;
#endif
} 

//-----------------------------------------------------------------------------
PartitionManager::~PartitionManager()
{

	shutdown();

}  // end ~PartitionManager

//-----------------------------------------------------------------------------
#ifdef PM_CACHE_TERRAIN_HEIGHT
static void calcHeights(const Region3D& world, Real cellSize, Int x, Int y, Real& loZ, Real& hiZ)
{
	DEBUG_ASSERTCRASH(TheTerrainLogic, ("no TheTerrainLogic"));
	Real xbase = world.lo.x + (x * cellSize);
	Real ybase = world.lo.y + (y * cellSize);
	const Real ROUGH_STEP_SIZE = MAP_XY_FACTOR;	// no point in stepping smaller than grid scale
	Real numSteps = ceilf(cellSize / ROUGH_STEP_SIZE);
	Real step = cellSize / numSteps;
	loZ = HUGE_DIST;		// huge positive
	hiZ = -HUGE_DIST;		// huge negative
	for (Real yy = 0; yy <= cellSize; yy += step) 
	{
		for (Real xx = 0; xx <= cellSize; xx += step) 
		{
			Real h = TheTerrainLogic->getGroundHeight( xbase + xx, ybase + yy );
			if (h < loZ) loZ = h;
			if (h > hiZ) hiZ = h;
		}
	}
}
#endif

//-----------------------------------------------------------------------------
void PartitionManager::init()
{
	m_cellSize = TheGlobalData->m_partitionCellSize;
	if (m_cellSize < 1.0)
		m_cellSize = 1.0;

	m_cellSizeInv = static_cast<Real>(1.0 / m_cellSize);

	DEBUG_ASSERTCRASH(m_cells == nullptr, ("double init"));

	if (TheTerrainLogic)
	{
		TheTerrainLogic->getExtent(&m_worldExtents);
		//DEBUG_ASSERTLOG(m_worldExtents.width() > 0 && m_worldExtents.height() > 0, ("TheTerrainLogic must be loaded before ThePartitionManager"));
		// you'd think it wouldn't be legal for terrainlogic to have zero area, but apparently
		// that's what it resets itself to. let's just make the world a simple place and pretend
		// it's nonzero in area, so that we can proceed without crashing.
		if (m_worldExtents.width() < 1.0f)
			m_worldExtents.hi.x = m_worldExtents.lo.x + 1.0f;
		if (m_worldExtents.height() < 1.0f)
			m_worldExtents.hi.y = m_worldExtents.lo.y + 1.0f;
		m_cellCountX = REAL_TO_INT_CEIL(m_worldExtents.width() * m_cellSizeInv);
		m_cellCountY = REAL_TO_INT_CEIL(m_worldExtents.height() * m_cellSizeInv);
		m_totalCellCount = m_cellCountX * m_cellCountY;
		m_cells = MSGNEW("PartitionManager_Cells") PartitionCell[m_totalCellCount];
		for (Int x = 0; x < m_cellCountX; x++)
		{
			for (Int y = 0; y < m_cellCountY; y++)
			{
#ifdef PM_CACHE_TERRAIN_HEIGHT
				Real loZ, hiZ;
				calcHeights(m_worldExtents, m_cellSize, x, y, loZ, hiZ);
				getCellAt(x, y)->init(x, y, loZ, hiZ);
#else
				getCellAt(x, y)->init(x, y);
#endif
			}
		}

#ifdef FASTER_GCO
		calcRadiusVec();
#endif

	}
	else
	{
		m_cellSize = m_cellSizeInv = 0.0f;
		m_cellCountX = 0;
		m_cellCountY = 0;
		m_totalCellCount = 0;
		m_cells = nullptr;
		m_worldExtents.lo.zero();
		m_worldExtents.hi.zero();
	}
}

//-----------------------------------------------------------------------------
#ifdef DUMP_PERF_STATS
void PartitionManager::getPMStats(double& gcoTimeThisFrameTotal, double& gcoTimeThisFrameAvg)
{
	Int64 freq64;
	GetPrecisionTimerTicksPerSec(&freq64);

	double gcoTimeInMSecs = static_cast<double>(s_timeInClosestObjectsThisFrame) * 1000.0f / static_cast<double>(freq64);

	gcoTimeThisFrameTotal = gcoTimeInMSecs;
	gcoTimeThisFrameAvg = gcoTimeInMSecs / static_cast<double>(s_countInClosestObjectsThisFrame);
}
#endif

//-----------------------------------------------------------------------------
void PartitionManager::reset()
{
#ifdef DUMP_PERF_STATS
	s_countInClosestObjects = 0;
	s_timeInClosestObjects = 0;
	s_countInClosestObjectsThisFrame = 0;
	s_timeInClosestObjectsThisFrame = 0;
	s_gcoPerfFrame = 0xffffffff;
#endif

	resetPendingUndoShroudRevealQueue();
	m_loadedModuleOrder.clear();
	m_loadedDirtyOrder.clear();
	m_loadedCellOrder.clear();
	m_loadedOrderPending = FALSE;

	shutdown();
	//init();
}

//-----------------------------------------------------------------------------
void PartitionManager::shutdown()
{
	m_updatedSinceLastReset = false;
	ThePartitionManager->removeAllDirtyModules();

#ifdef _DEBUG
	// the above *should* remove all the touched cells (via unRegisterObject), but let's check:
	DEBUG_ASSERTCRASH( m_moduleList == nullptr, ("hmm, modules left over"));
	PartitionData *mod, *nextMod;
	for( mod = m_moduleList; mod; mod = nextMod )
	{
		nextMod = mod->getNext();
		DEBUG_ASSERTCRASH(mod->friend_getCoiInUseCount() == 0, ("hmm, coi count mismatch"));
		mod->friend_removeAllTouchedCells();
	}
#endif

#ifdef FASTER_GCO
	m_radiusVec.clear();
#endif

	resetPendingUndoShroudRevealQueue();
	
	delete [] m_cells;
	m_cells = nullptr;

	m_cellSize = m_cellSizeInv = 0.0f;
	m_cellCountX = 0;
	m_cellCountY = 0;
	m_totalCellCount = 0;
	m_worldExtents.lo.zero();
	m_worldExtents.hi.zero();
}

//-----------------------------------------------------------------------------
//DECLARE_PERF_TIMER(PartitionManager_update)
void PartitionManager::update()
{
	//USE_PERF_TIMER(PartitionManager_update)
	{
#ifdef INTENSE_DEBUG
		Int cc = 0;
#endif
		if (!m_updatedSinceLastReset) 
		{
			m_updatedSinceLastReset = true;
		}

		PartitionContactList ctList;
		TheContactList = &ctList;
		while (m_dirtyModules)
		{
#ifdef INTENSE_DEBUG
			++cc;
#endif

			// save it.
			PartitionData *dirty = m_dirtyModules;
			DEBUG_ASSERTCRASH(dirty->getObject() != nullptr || dirty->getGhostObject() != nullptr, 
												("must be attached to an Object here %p",dirty));

			// get this BEFORE removing from dirty list, since that clears the
			// flag in question.
			Bool updateEm = dirty->isInNeedOfUpdatingCells();
			Bool collideEm = dirty->isInNeedOfCollisionCheck() && dirty->getObject();	//only update collisions if we have object
			
			// detach it from the dirty list.
			removeFromDirtyModules(dirty);

			if (updateEm)
			{
				dirty->friend_updateCellsTouched();
			}

			if (collideEm && !dirty->getObject()->isKindOf(KINDOF_IMMOBILE))
			{
				dirty->addPossibleCollisions(&ctList);
			}
		}
		
		ctList.processContactList();
#ifdef INTENSE_DEBUG
		DEBUG_ASSERTLOG(cc==0,("updated partition info for %d objects\n",cc));
#endif
		TheContactList = nullptr;

		processPendingUndoShroudRevealQueue();
	}

#if defined(_DEBUG)
	if (TheGlobalData->m_debugThreatMap) 
	{
		if (TheGameLogic->getFrame() % TheGlobalData->m_debugThreatMapTileDuration)
		{
			Int cellCount = m_cellCountX * m_cellCountY;
			for (int i = 0; i < cellCount; ++i) 
			{
				UnsignedInt threat = m_cells[i].getThreatValue(ThePlayerList->getLocalPlayer()->getPlayerIndex());
				if (threat > 0) 
				{
					Real threatMul = INT_TO_REAL(threat) / TheGlobalData->m_maxDebugThreat;
					if (threatMul > 1.0f)
						threatMul = 1.0f;

					Real size = TheGlobalData->m_partitionCellSize;
					Coord3D pos = { m_cells[i].getCellX() * size, 
													m_cells[i].getCellY() * size, 
													0 };
					pos.z = TheTerrainLogic->getGroundHeight(pos.x, pos.y);
					RGBColor color;
					color.red = threatMul;
					color.blue = 0.0f;
					color.green = 0.0f;

					addIcon(&pos, size, TheGlobalData->m_debugThreatMapTileDuration, color);
				}
			}
		}
	}

	if (TheGlobalData->m_debugCashValueMap)
	{
		if (TheGameLogic->getFrame() % TheGlobalData->m_debugCashValueMapTileDuration)
		{
			Int cellCount = m_cellCountX * m_cellCountY;
			for (int i = 0; i < cellCount; ++i) 
			{
				UnsignedInt value = m_cells[i].getCashValue(ThePlayerList->getLocalPlayer()->getPlayerIndex());
				if (value > 0) 
				{
					Real valueMul = INT_TO_REAL(value) / TheGlobalData->m_maxDebugValue;
					if (valueMul > 1.0f)
						valueMul = 1.0f;

					Real size = TheGlobalData->m_partitionCellSize;
					Coord3D pos = { m_cells[i].getCellX() * size, 
													m_cells[i].getCellY() * size, 
													0 };
					pos.z = TheTerrainLogic->getGroundHeight(pos.x, pos.y);
					RGBColor color;
					color.red = 0.0f;
					color.blue = 0.0f;
					color.green = valueMul;

					addIcon(&pos, size, TheGlobalData->m_debugCashValueMapTileDuration, color);
				}
			}			
		}
	}
#endif // defined(_DEBUG)
}  // end update

//------------------------------------------------------------------------------
void PartitionManager::registerObject( Object* object )
{
	// sanity
	if( object == nullptr )
		return;

	// if object is already part of this system get out of here
	if( object->friend_getPartitionData() != nullptr )
	{
		DEBUG_LOG(( "Object '%s' already registered with partition manager\n",
								object->getTemplate()->getName().str() ));
		return;
	}  // end if

	// allocate a new module of partition data
	PartitionData *mod = newInstance( PartitionData );

	// link the module to the list in the partition manager
	mod->setPrev( nullptr );
	mod->setNext( m_moduleList );
	if( m_moduleList )
		m_moduleList->setPrev( mod );
	m_moduleList = mod;

	// add module to object
	mod->attachToObject( object );

}

//------------------------------------------------------------------------------
void PartitionManager::unRegisterObject( Object* object )
{
	// sanity
	if( object == nullptr )
		return;

	// get the partition module
	PartitionData *mod = object->friend_getPartitionData();
	if( mod == nullptr )
		return;

	GhostObject *ghost;

	// need to figure out if any players have a fogged memory of this object.
	// if so, we can't remove it from the shroud system just yet.
	if ((ghost=mod->getGhostObject()) != nullptr && mod->wasSeenByAnyPlayers() < MAX_PLAYER_COUNT)
	{
		if (TheContactList)
			TheContactList->removeSpecificPartitionData(mod);
		object->friend_setPartitionData(nullptr);
		mod->friend_setObject(nullptr);
		//Tell the ghost object that its parent is dead.
		ghost->updateParentObject(nullptr, mod);
		return;
	}

	// detach the module from the object
	mod->detachFromObject();

	// remove module from master list
	PartitionData *next = mod->getNext();
	PartitionData *prev = mod->getPrev();
	if( next )
		next->setPrev( prev );
	if( prev )
		prev->setNext( next );
	else
		m_moduleList = next;

	// delete module
	deleteInstanceOf(mod);

}

//------------------------------------------------------------------------------
void PartitionManager::registerGhostObject( GhostObject* object)
{
	// sanity
	if( object == nullptr )
		return;

	// if object is already part of this system get out of here
	if( object->friend_getPartitionData() != nullptr )
	{
		DEBUG_LOG(( "GhostObject already registered with partition manager\n"));
		return;
	}  // end if

	// allocate a new module of partition data
	PartitionData *mod = newInstance( PartitionData );

	// link the module to the list in the partition manager
	mod->setPrev( nullptr );
	mod->setNext( m_moduleList );
	if( m_moduleList )
		m_moduleList->setPrev( mod );
	m_moduleList = mod;

	// add module to object
	mod->attachToGhostObject( object);
}

//------------------------------------------------------------------------------
void PartitionManager::unRegisterGhostObject( GhostObject* object )
{
	// sanity
	if( object == nullptr )
		return;

	// get the partition module
	PartitionData *mod = object->friend_getPartitionData();
	if( mod == nullptr )
		return;

	// detach the module from the object
	mod->detachFromGhostObject();

	// remove module from master list
	PartitionData *next = mod->getNext();
	PartitionData *prev = mod->getPrev();
	if( next )
		next->setPrev( prev );
	if( prev )
		prev->setNext( next );
	else
		m_moduleList = next;

	// delete module
	deleteInstanceOf(mod);
}





//-----------------------------------------------------------------------------
void PartitionManager::getCellCenterPos(Int x, Int y, Real& xx, Real& yy)
{
	DEBUG_ASSERTCRASH(x >= 0 && y >= 0, ("hmm, invalid cell"));
	Real half = m_cellSize*0.5f;
	xx = m_worldExtents.lo.x + (x * m_cellSize) + half;
	yy = m_worldExtents.lo.y + (y * m_cellSize) + half;
}

//-----------------------------------------------------------------------------
#ifdef PM_CACHE_TERRAIN_HEIGHT

	struct TerrainExtremeData
	{
		Real* minZ;
		Real* maxZ;
		Coord2D* minZPos;
		Coord2D* maxZPos;
		Bool isValid;
	};

static Int checkTerrainExtreme(PartitionCell* cell, void* userData)
{
	TerrainExtremeData* data = static_cast<TerrainExtremeData*>(userData);
	
	data->isValid = true;

	Real tmp;

	if (data->minZ)
	{
		tmp = cell->getLoTerrain();
		if (tmp < *data->minZ ) 
		{
			*data->minZ = tmp;
			if (data->minZPos)
			{
				cell->getCellCenterPos(data->minZPos->x, data->minZPos->y);
			}
		}
	}

	if (data->maxZ)
	{
		tmp = cell->getHiTerrain();
		if (tmp > *data->maxZ ) 
		{
			*data->maxZ = tmp;
			if (data->maxZPos)
			{
				cell->getCellCenterPos(data->maxZPos->x, data->maxZPos->y);
			}
		}
	}

	return 0;	// zero to continue
}
#endif

//-----------------------------------------------------------------------------
#ifdef PM_CACHE_TERRAIN_HEIGHT
Bool PartitionManager::estimateTerrainExtremesAlongLine(const Coord3D& pos, const Coord3D& posOther, Real* minZ, Real* maxZ, Coord2D* minZPos, Coord2D* maxZPos)
{
	TerrainExtremeData data;
	data.minZ = minZ;
	data.maxZ = maxZ;
	data.minZPos = minZPos;
	data.maxZPos = maxZPos;
	data.isValid = false;
	if (minZ) *minZ = HUGE_DIST;
	if (maxZ) *maxZ = -HUGE_DIST;
	iterateCellsAlongLine(pos, posOther, checkTerrainExtreme, &data);
	return data.isValid;
}
#endif

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void PartitionManager::crc( Xfer *xfer )
{

	for (Int i=0; i<m_totalCellCount; ++i)
	{
		m_cells[i].crc(xfer);
	}

}  // end crc

// ------------------------------------------------------------------------------------------------
static void xferModuleID( Xfer *xfer, PartitionData *mod )
{
	ObjectID id = mod->getObject() ? mod->getObject()->getID() : INVALID_ID;
	xfer->xferObjectID( &id );
}

// ------------------------------------------------------------------------------------------------
void PartitionManager::xferOrderForDiagnosis( Xfer *xfer )
{
	for( PartitionData *mod = m_moduleList; mod; mod = mod->getNext() )
		xferModuleID( xfer, mod );

	Int separator = -1;
	xfer->xferInt( &separator );
	for( PartitionData *mod = m_dirtyModules; mod; mod = mod->getNextDirty() )
		xferModuleID( xfer, mod );

	for( Int i = 0; i < m_totalCellCount; ++i )
	{
		CellAndObjectIntersection *coi = m_cells[i].getFirstCoiInCell();
		if( coi == nullptr )
			continue;
		Int cell = i;
		xfer->xferInt( &separator );
		xfer->xferInt( &cell );
		for( ; coi; coi = coi->getNextCoi() )
			xferModuleID( xfer, coi->getModule() );
	}
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method
	* Version Info:
	* 1: Initial version
	* 2: m_pendingUndoShroudReveals stores Unlooks waiting to happen.
	* 3: order of the module list, the dirty list and the cell lists
	*/
// ------------------------------------------------------------------------------------------------
void PartitionManager::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// cell size information
	Real cellSize = m_cellSize;
	xfer->xferReal( &cellSize );

	//
	// cell size should match what we have allocated in the partition manager, if it doesn't
	// we need to increment the version here and edit this code to be able to load
	// the old cell size if changed after we release
	//
	if( cellSize != m_cellSize )
	{

		DEBUG_CRASH(( "Partition cell size has changed, this save game file is invalid\n" ));
		throw SaveCode::SC_INVALID_DATA;

	}  // end if

	// cell count
	Int totalCellCount = m_totalCellCount;
	xfer->xferInt( &totalCellCount );
	
	// total cell count better match what we have allocated for cells in the partition manager
	if( totalCellCount != m_totalCellCount )
	{

		DEBUG_CRASH(( "Partition total cell count mismatch %d, should be %d\n",
									totalCellCount, m_totalCellCount ));
		throw SaveCode::SC_INVALID_DATA;

	}  // end if

	// xfer each cell information
	PartitionCell *cell;
	for( Int i = 0; i < totalCellCount; ++i )
	{

		// get this partition cell
		cell = &m_cells[ i ];

		// xfer the data for this cell
		xfer->xferSnapshot( cell );

	}  // end for i

	// when loading tell the partition manager to rethink and refresh all shroud information
	if( xfer->getXferMode() == XFER_LOAD )
	{

		// tell partition manager to re-evaluate shroud things when next asked
		m_updatedSinceLastReset = FALSE;

		// refresh the shroud for the local player which will update the radar and everything
		refreshShroudForLocalPlayer();

	}  // end if

	if( version >= 2 )
	{
		Int queueSize = static_cast<Int>(m_pendingUndoShroudReveals.size());
		xfer->xferInt(&queueSize);
		// This xfer thing is still cool.  On save, I just wrote the number of elements.  On load, the first line 
		// above was a decoy, and the second found out the size.

		if(xfer->getXferMode() == XFER_LOAD)
		{
			// have to remove this assert, because during load there is a setTeam call for each guy on a sub-team, and that results 
			// in a queued unlook, so we actually have stuff in here at the start.  I am fairly certain that setTeam should wait
			// until loadPostProcess, but I ain't gonna change it now.
//			DEBUG_ASSERTCRASH(m_pendingUndoShroudReveals.size() == 0, ("At load, we appear to not be in a reset state.") );

			// I have to split this up though, since on Load I need to make new instances.
			for( Int infoIndex = 0; infoIndex < queueSize; infoIndex++ )
			{
				SightingInfo *newInfo = newInstance(SightingInfo);
				xfer->xferSnapshot(newInfo);
				m_pendingUndoShroudReveals.push(newInfo);
			}
		}
		else
		{
			// And on Save, I need to loop through, but not destroy anything, so I pop-push n times, cycling through the queue once
			// (No random access on queues, and speed not a concern in saveload)
			for( Int infoIndex = 0; infoIndex < queueSize; infoIndex++ )
			{
				SightingInfo *saveInfo = m_pendingUndoShroudReveals.front();
				xfer->xferSnapshot(saveInfo);
				m_pendingUndoShroudReveals.pop();
				m_pendingUndoShroudReveals.push(saveInfo);
			}
		}

	}
	else
	{
		// Version 1 save games will just not have any SightingInfos in the queue to be undone.
	}

	if( version >= 3 )
		xferOrder( xfer );

}  // end xfer

// ------------------------------------------------------------------------------------------------
void PartitionManager::xferOrder( Xfer *xfer )
{
	if( xfer->getXferMode() != XFER_LOAD )
	{
		Int count = 0;
		for( PartitionData *mod = m_moduleList; mod; mod = mod->getNext() )
			if( mod->getObject() )
				++count;
		xfer->xferInt( &count );
		for( PartitionData *mod = m_moduleList; mod; mod = mod->getNext() )
			if( mod->getObject() )
				xferModuleID( xfer, mod );

		count = 0;
		for( PartitionData *mod = m_dirtyModules; mod; mod = mod->getNextDirty() )
			if( mod->getObject() )
				++count;
		xfer->xferInt( &count );
		for( PartitionData *mod = m_dirtyModules; mod; mod = mod->getNextDirty() )
		{
			if( mod->getObject() == nullptr )
				continue;
			xferModuleID( xfer, mod );
			Int status = mod->friend_getDirtyStatus();
			xfer->xferInt( &status );
		}

		count = 0;
		for( Int i = 0; i < m_totalCellCount; ++i )
			if( m_cells[ i ].getFirstCoiInCell() )
				++count;
		xfer->xferInt( &count );
		for( Int i = 0; i < m_totalCellCount; ++i )
		{
			CellAndObjectIntersection *first = m_cells[ i ].getFirstCoiInCell();
			if( first == nullptr )
				continue;
			Int cell = i;
			xfer->xferInt( &cell );
			Int objects = 0;
			for( CellAndObjectIntersection *coi = first; coi; coi = coi->getNextCoi() )
				if( coi->getModule()->getObject() )
					++objects;
			xfer->xferInt( &objects );
			for( CellAndObjectIntersection *coi = first; coi; coi = coi->getNextCoi() )
				if( coi->getModule()->getObject() )
					xferModuleID( xfer, coi->getModule() );
		}
		return;
	}

	m_loadedModuleOrder.clear();
	m_loadedDirtyOrder.clear();
	m_loadedCellOrder.clear();

	Int count = 0;
	xfer->xferInt( &count );
	for( Int i = 0; i < count; ++i )
	{
		ObjectID id = INVALID_ID;
		xfer->xferObjectID( &id );
		m_loadedModuleOrder.push_back( id );
	}

	xfer->xferInt( &count );
	for( Int i = 0; i < count; ++i )
	{
		ObjectID id = INVALID_ID;
		Int status = 0;
		xfer->xferObjectID( &id );
		xfer->xferInt( &status );
		m_loadedDirtyOrder.emplace_back( id, status );
	}

	xfer->xferInt( &count );
	for( Int i = 0; i < count; ++i )
	{
		Int cell = 0;
		Int objects = 0;
		xfer->xferInt( &cell );
		xfer->xferInt( &objects );
		std::vector<ObjectID> ids;
		for( Int j = 0; j < objects; ++j )
		{
			ObjectID id = INVALID_ID;
			xfer->xferObjectID( &id );
			ids.push_back( id );
		}
		m_loadedCellOrder.emplace_back( cell, std::move( ids ) );
	}

	m_loadedOrderPending = TRUE;
}

// ------------------------------------------------------------------------------------------------
static PartitionData *findLoadedModule( ObjectID id )
{
	Object *obj = TheGameLogic->findObjectByID( id );
	return obj ? obj->friend_getPartitionData() : nullptr;
}

// ------------------------------------------------------------------------------------------------
void PartitionManager::friend_updateAfterLoad( void )
{
	if( !m_loadedOrderPending )
	{
		update();
		return;
	}
	m_loadedOrderPending = FALSE;

	m_updatedSinceLastReset = true;
	while( m_dirtyModules )
	{
		PartitionData *dirty = m_dirtyModules;
		const Bool updateEm = dirty->isInNeedOfUpdatingCells();
		removeFromDirtyModules( dirty );
		if( updateEm )
			dirty->friend_updateCellsTouched();
	}

	// Each list is rebuilt by moving the saved entries to its head in reverse order; entries the save
	// game does not know (ghost objects) end up behind them.
	for( auto it = m_loadedModuleOrder.rbegin(); it != m_loadedModuleOrder.rend(); ++it )
	{
		PartitionData *mod = findLoadedModule( *it );
		if( mod == nullptr || mod == m_moduleList )
			continue;
		if( mod->getPrev() )
			mod->getPrev()->setNext( mod->getNext() );
		if( mod->getNext() )
			mod->getNext()->setPrev( mod->getPrev() );
		mod->setPrev( nullptr );
		mod->setNext( m_moduleList );
		if( m_moduleList )
			m_moduleList->setPrev( mod );
		m_moduleList = mod;
	}

	for( const auto &entry : m_loadedCellOrder )
	{
		if( entry.first < 0 || entry.first >= m_totalCellCount )
			continue;
		PartitionCell &cell = m_cells[ entry.first ];
		for( auto it = entry.second.rbegin(); it != entry.second.rend(); ++it )
		{
			PartitionData *mod = findLoadedModule( *it );
			if( mod == nullptr )
				continue;
			for( CellAndObjectIntersection *coi = cell.getFirstCoiInCell(); coi; coi = coi->getNextCoi() )
			{
				if( coi->getModule() == mod )
				{
					cell.friend_removeFromCellList( coi );
					cell.friend_addToCellList( coi );
					break;
				}
			}
		}
	}

	for( auto it = m_loadedDirtyOrder.rbegin(); it != m_loadedDirtyOrder.rend(); ++it )
	{
		PartitionData *mod = findLoadedModule( it->first );
		if( mod == nullptr || it->second == 0 )
			continue;
		if( isInListDirtyModules( mod ) )
			removeFromDirtyModules( mod );
		mod->friend_setDirtyStatus( it->second );
		prependToDirtyModules( mod );
	}

	m_loadedModuleOrder.clear();
	m_loadedDirtyOrder.clear();
	m_loadedCellOrder.clear();
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void PartitionManager::loadPostProcess( void )
{

}  // end loadPostProcess


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------



//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
SightingInfo::SightingInfo() 
{
	reset();
}

// ------------------------------------------------------------------------------------------------
/** Resetting sighting info */
// ------------------------------------------------------------------------------------------------
void SightingInfo::reset()
{
	m_where.zero();
	m_howFar = 0.0f;
	m_forWhom = 0;
	m_data = 0;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool SightingInfo::isInvalid() const
{
	return m_howFar == 0.0f;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void SightingInfo::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void SightingInfo::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// where
	xfer->xferCoord3D( &m_where );

	// how far
	xfer->xferReal( &m_howFar );

	// for whom
	xfer->xferUser( &m_forWhom, sizeof( PlayerMaskType ) );

	// how much
	xfer->xferUnsignedInt( &m_data );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void SightingInfo::loadPostProcess()
{

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
SightingInfo::~SightingInfo()
{

}  // end loadPostProcess

