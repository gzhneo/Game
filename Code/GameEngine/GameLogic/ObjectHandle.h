/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ObjectHandle: object references that survive the object's death.
//
// A raw Object* kept beyond a tick may dangle: if the object dies between two ticks
// (processDestroyList), the pointer points to freed memory. An ObjectID alone is not enough
// once IDs are reused: an old ID would hit a different, newer object.
//
// ObjectHandle is the ID plus a generation counter. TheGameLogic->resolveObjectHandle() uses the
// ID to find the slot and the generation to check that the same object still lives there;
// for a dead object it returns NULL.
//
// Rules:
//   * References that live beyond a tick are handles (or ObjectID where the code checks anyway).
//   * Pointers within a single call stay pointers; handles would only cost time there.
//   * Back references to the owner (m_owner, m_parent) stay pointers: they live exactly as
//     long as the module inside the object.
//   * List links (Object::m_next/m_prev) stay pointers.

#pragma once

#ifndef __OBJECTHANDLE_H_
#define __OBJECTHANDLE_H_

#include "Lib/BaseType.h"
#include "Common/GameType.h"		// ObjectID, INVALID_ID

class Object;

/// Generation of a slot in the object table. 0 means "never used".
using ObjectGeneration = UnsignedInt;

class ObjectHandle
{
public:
	ObjectHandle() : m_id(INVALID_ID), m_generation(0) { }
	ObjectHandle(ObjectID id, ObjectGeneration generation) : m_id(id), m_generation(generation) { }

	ObjectID getID() const { return m_id; }
	ObjectGeneration getGeneration() const { return m_generation; }

	/// TRUE if an object was ever referenced (says nothing about whether it still lives;
	/// only TheGameLogic->resolveObjectHandle() answers that).
	Bool isSet() const { return m_id != INVALID_ID; }

	void clear() { m_id = INVALID_ID; m_generation = 0; }

	Bool operator==(const ObjectHandle& other) const
	{
		return m_id == other.m_id && m_generation == other.m_generation;
	}
	Bool operator!=(const ObjectHandle& other) const { return !(*this == other); }

private:
	ObjectID			m_id;
	ObjectGeneration	m_generation;
};

#endif // __OBJECTHANDLE_H_
