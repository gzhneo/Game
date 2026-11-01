/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MemoryStream.h -- the engine's chunk reader over a buffer of the caller, so a map from the editor
// model reaches WorldHeightMap without a temporary file.

#pragma once

#include "Common/AsciiString.h"
#include "Common/MapReaderWriterInfo.h"

#include <cstring>

namespace MapEditorEngine
{

class MemoryChunkStream : public ChunkInputStream
{
public:
	MemoryChunkStream(const unsigned char *data, Int size) : m_data(data), m_size(size) {}

	virtual Int read(void *pData, Int numBytes) override
	{
		if (numBytes <= 0 || m_pos >= m_size)
			return 0;
		const Int n = numBytes < m_size - m_pos ? numBytes : m_size - m_pos;
		std::memcpy(pData, m_data + m_pos, static_cast<size_t>(n));
		m_pos += n;
		return n;
	}
	virtual UnsignedInt tell(void) override { return static_cast<UnsignedInt>(m_pos); }
	virtual Bool absoluteSeek(UnsignedInt pos) override
	{
		if (pos > static_cast<UnsignedInt>(m_size))
			return false;
		m_pos = static_cast<Int>(pos);
		return true;
	}
	virtual Bool eof(void) override { return m_pos >= m_size; }
	virtual Int bytesLeft(void) override { return m_size - m_pos; }

private:
	const unsigned char *m_data;
	Int m_size;
	Int m_pos = 0;
};

} // namespace MapEditorEngine
