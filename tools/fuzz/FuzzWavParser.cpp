/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FuzzWavParser.cpp -- fuzz target for ParseWavAndDecode (XAudio2Device/WavParser.cpp).
//
// Every sound effect and speech line comes from a .wav in the archives or a mod: RIFF chunks, format
// header, PCM or IMA ADPCM data. The parser takes chunk lengths and block sizes from the file. Run as
// libFuzzer target or as a plain program with input files as arguments.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "XAudio2Device/WavParser.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size > 16 * 1024 * 1024)
		return 0;
	WaveFormatInfo info = {};
	void *decoded = nullptr;
	if (ParseWavAndDecode(data, static_cast<UnsignedInt>(size), info, decoded))
	{
		// Touch the reported range: a length that does not match the buffer shows up here under ASan.
		const unsigned char *bytes = static_cast<const unsigned char *>(info.data);
		volatile unsigned sum = 0;
		for (UnsignedInt i = 0; i < info.dataLen; ++i)
			sum += bytes[i];
	}
	delete[] static_cast<char *>(decoded);
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
