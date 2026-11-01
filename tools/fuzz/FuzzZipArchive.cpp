/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FuzzZipArchive.cpp -- fuzz target for Engine::readZipArchiveDirectory and Engine::extractZipEntry
// (Engine/ZipArchive.cpp).
//
// Mods as .zip archives come from the net: end record, central directory, local headers and the
// deflate streams come from the file. Every accepted entry is unpacked; the result must have the
// announced size and stay below the size limit. Reads return pieces of varying size, as a file read
// may. Run as libFuzzer target or as a plain program with input files as arguments.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "Engine/ZipArchive.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size > 16 * 1024 * 1024)
		return 0;
	const size_t step = size > 0 ? 1 + data[0] % 4096 : 1;
	Engine::ZipArchiveReadAt readAt = [&](std::uint64_t offset, void *dest, size_t want) -> size_t
	{
		if (offset >= size)
			return 0;
		size_t n = want < step ? want : step;
		if (n > size - offset)
			n = size - static_cast<size_t>(offset);
		std::memcpy(dest, data + offset, n);
		return n;
	};
	std::vector<Engine::ZipArchiveEntry> entries;
	std::vector<Engine::ZipArchiveSkipped> skipped;
	if (Engine::readZipArchiveDirectory(readAt, size, entries, &skipped) != Engine::ZipArchiveError::None)
		return 0;
	for (const Engine::ZipArchiveEntry &e : entries)
	{
		if (e.path.size() > Engine::ZIP_ARCHIVE_MAX_NAME || e.size > Engine::ZIP_ARCHIVE_MAX_ENTRY_SIZE)
			std::abort();
		if (e.size > 64 * 1024 * 1024)
			continue;	// allowed, but too slow for a fuzz run
		std::vector<unsigned char> out;
		if (Engine::extractZipEntry(readAt, size, e, out) == Engine::ZipArchiveError::None && out.size() != e.size)
			std::abort();
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
