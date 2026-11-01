/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FuzzMapReader.cpp -- fuzz target for the map model of the new map editor
// (Code/Tools/Editors/MapEditor/Model): chunk container, dictionaries and all map chunks.
//
// Besides crashes and undefined behaviour it checks one property: whatever the reader accepts, the
// writer writes, and writing that again gives the same bytes. The first write may differ from the
// input (old chunk versions are written in the newest one), after that the data must be stable.
//
// Like the other targets it runs under libFuzzer (FUZZ_LIBFUZZER_MAIN) or as a plain program that
// checks the files given on the command line.

#include "Model/MapIO.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	const MapModel::ReadResult first = MapModel::readMap({data, size});
	if (!first.ok())
		return 0;
	const MapModel::WriteResult written = MapModel::writeMap(first.map);
	if (!written.ok())
		return 0;
	const MapModel::ReadResult second = MapModel::readMap(written.bytes);
	if (!second.ok())
	{
		std::fprintf(stderr, "written map not readable: %s\n", second.error.c_str());
		std::abort();
	}
	const MapModel::WriteResult again = MapModel::writeMap(second.map);
	if (!again.ok() || again.bytes != written.bytes)
	{
		std::fprintf(stderr, "writing is not stable\n");
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
		if (!f)
		{
			std::printf("cannot read %s\n", argv[i]);
			return 2;
		}
		std::vector<uint8_t> buf;
		uint8_t tmp[65536];
		size_t n;
		while ((n = std::fread(tmp, 1, sizeof(tmp), f)) > 0)
			buf.insert(buf.end(), tmp, tmp + n);
		std::fclose(f);
		LLVMFuzzerTestOneInput(buf.data(), buf.size());
	}
	return 0;
}
#endif
