/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FuzzBigArchive.cpp -- fuzz target for Engine::readBigArchiveDirectory (Engine/BigArchive.cpp).
//
// Every .big archive of the game, of a mod or of the texture tiers goes through this reader: entry
// count, offsets, sizes and names come from the file. The input is read in pieces of varying size,
// as a file read may return fewer bytes than asked for. Run as libFuzzer target or as a plain
// program with input files as arguments.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "Engine/BigArchive.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size > 16 * 1024 * 1024)
		return 0;
	size_t pos = 0;
	const size_t step = size > 0 ? 1 + data[0] % 64 : 1;
	Engine::BigArchiveRead read = [&](void *dest, size_t want) -> size_t
	{
		size_t n = want < step ? want : step;
		if (n > size - pos)
			n = size - pos;
		std::memcpy(dest, data + pos, n);
		pos += n;
		return n;
	};
	std::vector<Engine::BigArchiveEntry> entries;
	if (Engine::readBigArchiveDirectory(read, size, entries) == Engine::BigArchiveError::None)
	{
		// Every accepted entry must lie inside the input: touch its first and last byte under ASan.
		volatile unsigned sum = 0;
		for (const Engine::BigArchiveEntry &e : entries)
		{
			if (e.path.size() > Engine::BIG_ARCHIVE_MAX_NAME)
				std::abort();
			if (e.size > 0)
				sum += data[e.offset] + data[e.offset + e.size - 1];
		}
	}
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
