/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#pragma once

#include <cstddef>

// Memory a buffer lock hands out when the device could not lock the buffer (device lost, out of memory):
// the callers write into it as usual and the geometry of that draw is lost. Owned by the lock object, so
// nested locks never share it.
class LockScratch
{
	unsigned char* data = nullptr;

public:
	LockScratch() = default;
	LockScratch(const LockScratch&) = delete;
	LockScratch& operator=(const LockScratch&) = delete;
	~LockScratch() { delete[] data; }

	void* Get(size_t bytes)
	{
		delete[] data;
		data = new unsigned char[bytes ? bytes : 1]();
		return data;
	}
};
