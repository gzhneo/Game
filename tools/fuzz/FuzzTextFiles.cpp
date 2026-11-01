/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FuzzTextFiles.cpp -- fuzz target for Engine::readCsf and Engine::readStr (Engine/TextFiles.cpp).
//
// The game's texts come from generals.csf (or a mod's), map texts from map.str, which arrives with a
// map from another player. Every input goes through both readers; the limits of TextFiles.h must hold
// for every entry. Run as libFuzzer target or as a plain program with input files as arguments.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "Engine/TextFiles.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size > 4 * 1024 * 1024)
		return 0;
	int version, language;
	std::vector<Engine::CsfEntry> csf;
	if (Engine::readCsf(data, size, version, language, csf) == Engine::CsfError::None)
	{
		for (const Engine::CsfEntry &e : csf)
			if (e.label.size() > Engine::TEXT_FILE_MAX_LABEL || e.text.size() > Engine::TEXT_FILE_MAX_TEXT * 2 || e.speech.size() > Engine::TEXT_FILE_MAX_LABEL)
				std::abort();
	}
	std::vector<Engine::StrEntry> str;
	Engine::readStr(reinterpret_cast<const char *>(data), size, str);
	for (const Engine::StrEntry &e : str)
		if (e.label.size() > Engine::TEXT_FILE_MAX_TEXT || e.text.size() > Engine::TEXT_FILE_MAX_TEXT || e.speech.size() >= Engine::TEXT_FILE_MAX_LABEL)
			std::abort();
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
