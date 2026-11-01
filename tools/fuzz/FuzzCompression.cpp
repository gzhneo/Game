/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FuzzCompression.cpp -- E-8.4: Fuzzing-Ziel fuer die
// Dekompression (EA RefPack/BTree/Huffman und ZLib ueber CompressionManager).
//
// Warum gerade hier: Jede `.big`-Datei, jedes Replay und jeder Spielstand kann komprimierte
// Bloecke enthalten. Die Dekoder stammen von 1995 und pruefen ihre Eingabe praktisch nicht --
// sie vertrauen den Laengen im Kopf. Eine beschaedigte oder praeparierte Datei landet damit
// direkt in einer Schleife, die ueber ihren Puffer hinausliest oder -schreibt.
//
// Das Ziel laesst sich auf zwei Arten benutzen:
//
//   * **libFuzzer** (MSVC `/fsanitize=fuzzer`, CMake-Option ENABLE_FUZZERS) ueber
//     `LLVMFuzzerTestOneInput`.
//   * **Einzeln**, als normales Programm: `FuzzCompression datei1 datei2 ...` prueft die
//     angegebenen Dateien. So laesst sich ein Fund nachstellen, und so arbeitet auch
//     `tools/fuzz/dumbfuzz.py` (mutiert Saatdateien und ruft das Programm auf) -- damit braucht
//     man fuer einen ersten Durchlauf weder libFuzzer noch Windows.

// STL zuerst: BaseType.h (ueber Compression.h) definiert min/max als Makros und bricht sonst
// die Standardbibliothek.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "Compression.h"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

// Groesse, ab der eine Eingabe als unsinnig gilt: die Engine liest keine 256 MB am Stueck.
static const int MAX_UNCOMPRESSED_SIZE = 64 * 1024 * 1024;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (data == NULL || size < 8 || size > (size_t)MAX_UNCOMPRESSED_SIZE)
		return 0;

	// Genau der Weg, den die Engine geht: Kopf pruefen, Zielgroesse aus dem Kopf nehmen,
	// Puffer anlegen, dekomprimieren.
	if (!CompressionManager::isDataCompressed((const void *)data, (Int)size))
		return 0;

	const Int uncompressedSize = CompressionManager::getUncompressedSize((const void *)data, (Int)size);
	if (uncompressedSize <= 0 || uncompressedSize > MAX_UNCOMPRESSED_SIZE)
		return 0;

	std::vector<char> destination((size_t)uncompressedSize);
	CompressionManager::decompressData((void *)data, (Int)size,
		destination.empty() ? NULL : &destination[0], uncompressedSize);
	return 0;
}

#ifndef FUZZ_LIBFUZZER_MAIN
int main(int argc, char **argv)
{
	if (argc < 2)
	{
		std::printf("Aufruf: %s <datei> [...]\n", argv[0]);
		std::printf("Prueft jede Datei als komprimierten Block (E-8.4).\n");
		return 2;
	}

	for (int i = 1; i < argc; ++i)
	{
		std::FILE *file = std::fopen(argv[i], "rb");
		if (file == NULL)
		{
			std::printf("%s: nicht lesbar\n", argv[i]);
			continue;
		}
		std::fseek(file, 0, SEEK_END);
		const long length = std::ftell(file);
		std::fseek(file, 0, SEEK_SET);
		if (length > 0 && length < MAX_UNCOMPRESSED_SIZE)
		{
			std::vector<uint8_t> buffer((size_t)length);
			if (std::fread(&buffer[0], 1, (size_t)length, file) == (size_t)length)
				LLVMFuzzerTestOneInput(&buffer[0], buffer.size());
		}
		std::fclose(file);
	}
	return 0;
}
#endif
