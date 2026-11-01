/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FuzzTarga.cpp -- fuzz target for the TGA reader (WWLib/TARGA.CPP).
//
// Textures, fonts and the preview image of every map (which comes with a map transfer from other
// players) are .tga files. Each input is loaded twice, as the texture loader does (fixed palette of
// 256 entries, image allocated by the reader) and with the reader allocating both. Files come from an
// in-memory file factory. Run as libFuzzer target or as a plain program with input files as arguments.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "WWFILE.H"
#include "ffactory.h"
#include "TARGA.H"

namespace {

const unsigned char *g_data = nullptr;
size_t g_size = 0;

class MemoryFile : public FileClass
{
public:
	char const *File_Name(void) const override { return "fuzz.tga"; }
	char const *Set_Name(char const *) override { return "fuzz.tga"; }
	int Create(void) override { return 0; }
	int Delete(void) override { return 0; }
	bool Is_Available(int) override { return true; }
	bool Is_Open(void) const override { return m_open; }
	int Open(char const *, int rights) override { return Open(rights); }
	int Open(int) override { m_open = true; m_pos = 0; return 1; }
	int Read(void *buffer, int size) override
	{
		if (size <= 0 || m_pos >= g_size)
			return 0;
		size_t n = static_cast<size_t>(size);
		if (n > g_size - m_pos)
			n = g_size - m_pos;
		std::memcpy(buffer, g_data + m_pos, n);
		m_pos += n;
		return static_cast<int>(n);
	}
	int Seek(int pos, int dir) override
	{
		long long base = dir == SEEK_SET ? 0 : dir == SEEK_END ? static_cast<long long>(g_size) : static_cast<long long>(m_pos);
		long long target = base + pos;
		if (target < 0 || target > static_cast<long long>(g_size))
			return -1;
		m_pos = static_cast<size_t>(target);
		return static_cast<int>(m_pos);
	}
	int Size(void) override { return static_cast<int>(g_size); }
	int Write(void const *, int) override { return 0; }
	void Close(void) override { m_open = false; }

private:
	bool m_open = false;
	size_t m_pos = 0;
};

class MemoryFileFactory : public FileFactoryClass
{
public:
	FileClass *Get_File(char const *) override { return new MemoryFile; }
	void Return_File(FileClass *file) override { delete file; }
};

MemoryFileFactory s_factory;

}	// namespace

FileFactoryClass *_TheFileFactory = &s_factory;
RawFileFactoryClass *_TheWritingFileFactory = nullptr;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size > 4 * 1024 * 1024)
		return 0;
	g_data = data;
	g_size = size;

	{
		// like TextureLoader: header first, then a fixed palette and the image from the reader
		Targa targa;
		if (targa.Open("fuzz.tga", TGA_READMODE) == 0)
		{
			char *palette = static_cast<char *>(std::malloc(256 * 4));
			targa.SetPalette(palette);
			targa.Load("fuzz.tga", TGAF_IMAGE, false);
			targa.SetPalette(nullptr);
			std::free(palette);
		}
	}
	{
		Targa targa;
		targa.Load("fuzz.tga", TGAF_IMAGE | TGAF_PAL, true);
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
