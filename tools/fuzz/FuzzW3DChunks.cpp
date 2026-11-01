/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FuzzW3DChunks.cpp -- fuzz target for the W3D chunk reader (WWLib ChunkLoadClass).
//
// Every W3D model, animation and hierarchy is read through ChunkLoadClass: the loaders open chunks,
// descend into nested ones, open micro chunks and read the payload. The walker below does the same
// for any input, so lengths and nesting taken from a damaged file are exercised the way the loaders
// would. Run as libFuzzer target or as a plain program with input files as arguments.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "chunkio.h"

namespace
{

// Read-only FileClass over a memory buffer.
class MemoryFile : public FileClass
{
public:
	MemoryFile(const uint8_t *data, size_t size) : m_data(data), m_size(static_cast<int>(size)), m_pos(0) {}
	char const *File_Name(void) const override { return "fuzz"; }
	char const *Set_Name(char const *) override { return "fuzz"; }
	int Create(void) override { return 0; }
	int Delete(void) override { return 0; }
	bool Is_Available(int) override { return true; }
	bool Is_Open(void) const override { return true; }
	int Open(char const *, int) override { return 1; }
	int Open(int) override { return 1; }
	int Read(void *buffer, int size) override
	{
		if (size <= 0)
			return 0;
		const int n = (size < m_size - m_pos) ? size : m_size - m_pos;
		if (n <= 0)
			return 0;
		std::memcpy(buffer, m_data + m_pos, static_cast<size_t>(n));
		m_pos += n;
		return n;
	}
	int Seek(int pos, int dir) override
	{
		long long target = (dir == SEEK_SET) ? pos : (dir == SEEK_END) ? m_size + static_cast<long long>(pos) : m_pos + static_cast<long long>(pos);
		if (target < 0) target = 0;
		if (target > m_size) target = m_size;
		m_pos = static_cast<int>(target);
		return m_pos;
	}
	int Size(void) override { return m_size; }
	int Write(void const *, int) override { return 0; }
	void Close(void) override {}

private:
	const uint8_t *m_data;
	int m_size;
	int m_pos;
};

void walk(ChunkLoadClass &cload, int depth)
{
	// The loaders nest a handful of levels; a crafted file must not drive recursion without bound.
	if (depth > 64)
		return;
	int chunks = 0;
	while (chunks++ < 4096 && cload.Open_Chunk())
	{
		if (cload.Contains_Chunks())
		{
			walk(cload, depth + 1);
		}
		else if (cload.Cur_Chunk_ID() & 1)
		{
			// Some payloads are micro chunk lists (e.g. texture info, shader names).
			int micro = 0;
			while (micro++ < 4096 && cload.Open_Micro_Chunk())
			{
				char buffer[256];
				cload.Read(buffer, sizeof(buffer));
				cload.Close_Micro_Chunk();
			}
		}
		else
		{
			uint8_t buffer[512];
			uint32_t left = cload.Cur_Chunk_Length();
			while (left > 0)
			{
				const uint32_t got = cload.Read(buffer, left < sizeof(buffer) ? left : sizeof(buffer));
				if (got == 0)
					break;
				left -= got;
			}
		}
		cload.Close_Chunk();
	}
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	MemoryFile file(data, size);
	ChunkLoadClass cload(&file);
	walk(cload, 0);
	return 0;
}

#ifndef FUZZ_LIBFUZZER_MAIN
int main(int argc, char **argv)
{
	for (int i = 1; i < argc; ++i)
	{
		FILE *f = std::fopen(argv[i], "rb");
		if (!f) { std::printf("nicht lesbar: %s\n", argv[i]); return 2; }
		std::vector<uint8_t> buf;
		uint8_t tmp[65536];
		size_t n;
		while ((n = std::fread(tmp, 1, sizeof(tmp), f)) > 0) buf.insert(buf.end(), tmp, tmp + n);
		std::fclose(f);
		LLVMFuzzerTestOneInput(buf.data(), buf.size());
	}
	return 0;
}
#endif
