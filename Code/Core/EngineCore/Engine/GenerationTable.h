/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// GenerationTable.h -- generation counter per table slot.
//
// The bookkeeping behind `ObjectHandle`: each slot in the object table
// (`GameLogic::m_objVector`) has a counter that increases on every occupy and every free.
// A handle stores slot and counter; resolving requires both to match. A handle to a dead
// object therefore resolves to nothing instead of to its successor in the same slot.
//
// No engine dependencies so the mechanism can be tested in EngineTests without memory pool,
// singletons or Windows.

#pragma once

#ifndef ENGINE_GENERATIONTABLE_H
#define ENGINE_GENERATIONTABLE_H

#include <cstddef>
#include <vector>

namespace Engine
{

using Generation = unsigned int;

class GenerationTable
{
public:
	/// Clears all slots and makes room for `capacity` entries (generation 0 = never occupied).
	void reset(std::size_t capacity)
	{
		m_generations.clear();
		m_generations.resize(capacity, 0);
	}

	/// Grows the table to at least this size; new slots start at 0.
	void ensureCapacity(std::size_t capacity)
	{
		if (m_generations.size() < capacity)
			m_generations.resize(capacity, 0);
	}

	std::size_t capacity() const { return m_generations.size(); }

	/// Slot is occupied: increments the generation and returns the new one.
	Generation onSlotOccupied(std::size_t index)
	{
		ensureCapacity(index + 1);
		return ++m_generations[index];
	}

	/// Slot is freed: increments the generation (handles to the old content become invalid).
	void onSlotFreed(std::size_t index)
	{
		if (index < m_generations.size())
			++m_generations[index];
	}

	/// Current generation of a slot; 0 for never occupied or unknown slots.
	Generation generation(std::size_t index) const
	{
		return (index < m_generations.size()) ? m_generations[index] : 0;
	}

	/// Does a stored generation still match the slot?
	bool matches(std::size_t index, Generation generation) const
	{
		return generation != 0 && this->generation(index) == generation;
	}

private:
	std::vector<Generation> m_generations;
};

} // namespace Engine

#endif // ENGINE_GENERATIONTABLE_H
