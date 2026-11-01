/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// PoolPtr.h -- sole ownership of an object from the engine's memory pools.
//
// Pool objects are made with newInstance and given back with deleteInstance (their destructors are
// not public). PoolPtr is a std::unique_ptr that gives them back that way.

#pragma once

#include <memory>

struct PoolDelete
{
	template <class T>
	void operator()( T *object ) const { deleteInstanceOf(object); }
};

template <class T>
using PoolPtr = std::unique_ptr<T, PoolDelete>;
