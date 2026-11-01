/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Replaces a global singleton pointer (`TheDisplay`, `TheGameLogic`, ...) for the lifetime of the object and
// restores the previous value afterwards, also when a test fails with an exception. This is how code that
// reads a singleton gets a test double or a scratch instance without a second copy of the global.
//
//     Engine::ScopedGlobal<GameText> guard(TheGameText, &fakeText);
//
// The pointer is only swapped, never owned or deleted; the replacement has to outlive the guard.

#pragma once

namespace Engine
{

template <class T>
class ScopedGlobal
{
public:
	ScopedGlobal(T *& global, T * replacement) : m_global(global), m_previous(global) { m_global = replacement; }
	~ScopedGlobal() { m_global = m_previous; }

	ScopedGlobal(const ScopedGlobal &) = delete;
	ScopedGlobal & operator=(const ScopedGlobal &) = delete;

	T * previous() const { return m_previous; }

private:
	T *& m_global;
	T * m_previous;
};

}
