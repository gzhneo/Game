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

// PartitionManagerInternal.h -- what the Partition*.cpp files share and nobody else needs.

#pragma once

struct CollideInfo
{
	Coord3D position;
	GeometryInfo geom;
	Real angle;

	CollideInfo(const Coord3D* p, const GeometryInfo& g, Real a) : position(*p), geom(g), angle(a) { }
};

/*
	Notes:
	-- collideLoc and collideNormal will only be filled in if result is true;
			they are undefined if result is false.
	-- either collideLoc or collideNormal may be null if you don't need 'em.
	-- collideLoc is not guaranteed to lie precisely on either 'a' or 'b'! 
			(this matters iff the objects are overlapping and thus have multiple/ambiguous 
			collision points; we pick an 'in-between' spot to simplify matters, such that
			the same spot will be used for the collision in both directions)
	-- collideNormal is the normal the collision surface at 'a' at collideLoc.
			(thus the normal for b is exactly opposite in direction)
	-- collideNormal is guaranteed to be a unit vector
*/
using CollideTestProc = Bool (*)(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);

// if the dist is greater than maxDist, return false, and the output stuff is undefined.
using DistCalcProc = Bool (*)
(
	const Coord3D *posA, 
	const Object *objA, 
	const Coord3D *posB, 
	const Object *objB, 
	Real& abDistSqr, 
	Coord3D& abVec,
	Real maxDistSqr
); 

extern DistCalcProc theDistCalcProcs[];
extern CollideTestProc theCollideTestProcs[];

//-----------------------------------------------------------------------------
inline void vecDiff_2D(const Coord3D *posA, const Coord3D *posB, Coord3D *resultVec)
{
	DEBUG_ASSERTCRASH(posA && posB && resultVec, ("null parm"));
	resultVec->x = posA->x - posB->x;
	resultVec->y = posA->y - posB->y;
	resultVec->z = 0.0f;
}

//-----------------------------------------------------------------------------
inline void vecDiff_3D(const Coord3D *posA, const Coord3D *posB, Coord3D *resultVec)
{
	DEBUG_ASSERTCRASH(posA && posB && resultVec, ("null parm"));
	resultVec->x = posA->x - posB->x;
	resultVec->y = posA->y - posB->y;
	resultVec->z = posA->z - posB->z;
}

//-----------------------------------------------------------------------------
inline Real calcSqrDist_2D(const Coord3D *dist)
{
	return sqr(dist->x) + sqr(dist->y);
}

//-----------------------------------------------------------------------------
inline Real calcSqrDist_3D(const Coord3D *dist)
{
	return sqr(dist->x) + sqr(dist->y) + sqr(dist->z);
}

//-----------------------------------------------------------------------------
inline Int absInt(Int a)
{
	if (a < 0) return -a; else return a;
}

//-----------------------------------------------------------------------------
inline Int minInt(Int a, Int b)
{
	if (a < b) return a; else return b;
}

//-----------------------------------------------------------------------------
inline Int maxInt(Int a, Int b)
{
	if (a > b) return a; else return b;
}

//-----------------------------------------------------------------------------
inline Real minReal(Real a, Real b)
{
	if (a < b) return a; else return b;
}

//-----------------------------------------------------------------------------
inline Real maxReal(Real a, Real b)
{
	if (a > b) return a; else return b;
}


class PartitionContactListNode : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(PartitionContactListNode, "PartitionContactListNode" )

public:
	PartitionContactListNode*			m_nextHash;	///< next node with same hash value 
	PartitionContactListNode*			m_next;			///< next node
	PartitionData*								m_obj;			///< one object that is possibly colliding
	PartitionData*								m_other;		///< the other object (or null for collisions with the terrain)
	Int														m_hashValue;///< index into hash table 
};

inline PartitionContactListNode::~PartitionContactListNode() { }

//-----------------------------------------------------------------------------

class PartitionContactList
{
private:

	/*
		socketcount should be prime (and "not too close to a power of 2) for best results.
		
		if this one isn't large enough, try this website:
		http://www.utm.edu/research/primes/lists/small/1000.txt

		So how is this chosen? Eh, pretty much based on experimentation.
	*/
	enum { PartitionContactList_SOCKET_COUNT = 5381 };


	PartitionContactListNode* m_contactHash[PartitionContactList_SOCKET_COUNT];
	PartitionContactListNode* m_contactList;

public:

	PartitionContactList()
	{
		memset(m_contactHash, 0, sizeof(m_contactHash));
		m_contactList = nullptr;
	}

	~PartitionContactList()
	{
		resetContactList();
	}

	/**
		add a pair of objects to the contact list. If the pair is already
		present (in either this order or reverse order), nothing is done.
		Note that it is OK for other==null (this indicates a collisions with
		the ground) but it is not OK for obj==null.
	*/
	void addToContactList(PartitionData *obj, PartitionData *other);

	/**
		process all pairs in the contact list: first, determine if they
		truly collide, based on their geometry. if so, call the collide
		actions for each object in the pair.
	*/
	void processContactList();

	/**
		discard the contents of the contact list.
	*/
	void resetContactList();

	/**
		remove any contacts that refer to the given data.
	*/
	void removeSpecificPartitionData(PartitionData* data);

};

extern PartitionContactList* TheContactList;

#ifdef DUMP_PERF_STATS
extern long s_countInClosestObjects;
extern long s_countInClosestObjectsThisFrame;
extern Int64 s_timeInClosestObjects;
extern Int64 s_timeInClosestObjectsThisFrame;
extern UnsignedInt s_gcoPerfFrame;
#endif
