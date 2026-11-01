/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapTool.cpp -- command line access to the map model: inspect maps and check that reading and
// writing reproduces them byte for byte. Needs no game data and no graphics.

#include "Model/MapDump.h"
#include "Model/MapFile.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace MapModel;

namespace
{

int usage()
{
	std::puts(
		"MapTool -- map files of Generals and Zero Hour\n"
		"\n"
		"  MapTool info <file.map>            summary\n"
		"  MapTool dump <file.map>            everything, as text\n"
		"  MapTool check <file.map|folder>... read and write back and compare: OK, DATA (packed\n"
		"                                     differently), UPGR (old chunk versions upgraded, content\n"
		"                                     unchanged), DIFF, FAIL; fails on DIFF and FAIL\n"
		"  MapTool unpack <file.map> <out>    write the uncompressed chunk data\n"
		"  MapTool resave <file.map> <out>    read and save again\n");
	return 2;
}

std::size_t firstDifference(const std::vector<std::uint8_t> &a, const std::vector<std::uint8_t> &b)
{
	const std::size_t n = std::min(a.size(), b.size());
	for (std::size_t i = 0; i < n; ++i)
		if (a[i] != b[i])
			return i;
	return n;
}

struct TopChunks
{
	NameTable names;
	std::vector<ChunkView> chunks;
};

TopChunks topChunks(const std::vector<std::uint8_t> &bytes)
{
	TopChunks result;
	ByteReader r(bytes);
	std::string error;
	if (result.names.read(r))
		splitChunks(r.rest(), result.names, result.chunks, error);
	return result;
}

// Top-level chunks that differ between the file and what the model writes.
std::vector<std::string> differingChunks(const std::vector<std::uint8_t> &original, const std::vector<std::uint8_t> &written)
{
	const TopChunks a = topChunks(original);
	const TopChunks b = topChunks(written);
	std::vector<std::string> result;
	const std::size_t n = std::max(a.chunks.size(), b.chunks.size());
	for (std::size_t i = 0; i < n; ++i)
	{
		const ChunkView *x = i < a.chunks.size() ? &a.chunks[i] : nullptr;
		const ChunkView *y = i < b.chunks.size() ? &b.chunks[i] : nullptr;
		if (x && y && x->name == y->name && x->version == y->version && std::equal(x->data.begin(), x->data.end(), y->data.begin(), y->data.end()))
			continue;
		std::string line = x ? x->name : y->name;
		line += ": ";
		line += x ? "v" + std::to_string(x->version) + ", " + std::to_string(x->data.size()) + " bytes" : "missing";
		line += " -> ";
		line += y ? "v" + std::to_string(y->version) + ", " + std::to_string(y->data.size()) + " bytes" : "missing";
		if (x && y)
		{
			const std::size_t m = std::min(x->data.size(), y->data.size());
			std::size_t k = 0;
			while (k < m && x->data[k] == y->data[k])
				++k;
			line += ", first difference at chunk offset " + std::to_string(k);
		}
		result.push_back(line);
	}
	if (a.names.entries().size() != b.names.entries().size())
		result.push_back("name table: " + std::to_string(a.names.entries().size()) + " -> " + std::to_string(b.names.entries().size()) + " names");
	return result;
}

void printWarnings(const ReadResult &r)
{
	for (const std::string &w : r.warnings)
		std::printf("  warning: %s\n", w.c_str());
}

// Result of one map: OK (same file), DATA (same chunk data, packed differently), UPGR (older chunk
// versions written in the newest one, content read back unchanged), DIFF, FAIL. Returns false for
// DIFF and FAIL.
bool checkOne(const fs::path &path)
{
	LoadedMap loaded = loadMap(path);
	if (!loaded.result.ok())
	{
		std::printf("FAIL  %s: %s\n", path.string().c_str(), loaded.result.error.c_str());
		return false;
	}
	WriteResult written = writeMap(loaded.result.map);
	if (!written.ok())
	{
		std::printf("FAIL  %s: write: %s\n", path.string().c_str(), written.error.c_str());
		printWarnings(loaded.result);
		return false;
	}
	if (written.bytes == loaded.chunkBytes)
	{
		std::vector<std::uint8_t> packed;
		std::string error;
		// Pack with the compression the file has, to compare the encoder too.
		if (!pack(written.bytes, loaded.compression, packed, error))
		{
			std::printf("FAIL  %s: pack: %s\n", path.string().c_str(), error.c_str());
			return false;
		}
		const bool sameFile = packed == loaded.fileBytes;
		std::printf("%s  %s%s\n", sameFile ? "OK  " : "DATA", path.string().c_str(),
			sameFile ? "" : " (chunk data identical, packed file differs)");
		printWarnings(loaded.result);
		return true;
	}

	const ReadResult again = readMap(written.bytes);
	const bool sameMap = again.ok() && sameContent(loaded.result.map, again.map);
	const std::vector<std::string> differing = differingChunks(loaded.chunkBytes, written.bytes);
	bool onlyUpgraded = sameMap;
	for (const std::string &d : differing)
	{
		const std::string name = d.substr(0, d.find(':'));
		const auto &up = loaded.result.upgradedChunks;
		if (std::find(up.begin(), up.end(), name) == up.end())
			onlyUpgraded = false;
	}
	if (onlyUpgraded)
		std::printf("UPGR  %s (older chunk versions, written in the newest; content read back unchanged)\n", path.string().c_str());
	else
		std::printf("DIFF  %s: chunk data differs at offset %zu (%zu vs %zu bytes)%s\n", path.string().c_str(),
			firstDifference(written.bytes, loaded.chunkBytes), written.bytes.size(), loaded.chunkBytes.size(),
			!again.ok() ? ", written map not readable" : (sameMap ? "" : ", content differs after reading back"));
	for (const std::string &d : differing)
		std::printf("  chunk: %s\n", d.c_str());
	for (const std::string &u : loaded.result.upgrades)
		std::printf("  upgrade: %s\n", u.c_str());
	printWarnings(loaded.result);
	return onlyUpgraded;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 3)
		return usage();
	const std::string command = argv[1];

	if (command == "info" || command == "dump")
	{
		LoadedMap loaded = loadMap(argv[2]);
		if (!loaded.result.ok())
		{
			std::fprintf(stderr, "%s\n", loaded.result.error.c_str());
			return 1;
		}
		std::printf("file: %s (%zu bytes, compression %d)\n", argv[2], loaded.fileBytes.size(), loaded.compression);
		std::fputs(dumpMap(loaded.result.map, command == "dump" ? DumpDetail::Full : DumpDetail::Summary).c_str(), stdout);
		printWarnings(loaded.result);
		return 0;
	}
	if (command == "check")
	{
		int total = 0;
		int failed = 0;
		for (int i = 2; i < argc; ++i)
		{
			std::vector<fs::path> files;
			std::error_code ec;
			if (fs::is_directory(argv[i], ec))
			{
				for (const auto &entry : fs::recursive_directory_iterator(argv[i], ec))
					if (entry.is_regular_file() && entry.path().extension() == ".map")
						files.push_back(entry.path());
				std::sort(files.begin(), files.end());
			}
			else
			{
				files.emplace_back(argv[i]);
			}
			for (const fs::path &f : files)
			{
				++total;
				if (!checkOne(f))
					++failed;
			}
		}
		std::printf("\n%d maps, %d failed\n", total, failed);
		return failed == 0 ? 0 : 1;
	}
	if ((command == "unpack" || command == "resave") && argc == 4)
	{
		LoadedMap loaded = loadMap(argv[2]);
		if (!loaded.result.ok())
		{
			std::fprintf(stderr, "%s\n", loaded.result.error.c_str());
			return 1;
		}
		std::string error;
		const bool ok = command == "unpack" ? writeFile(argv[3], loaded.chunkBytes, error) : saveMap(argv[3], loaded.result.map, error);
		if (!ok)
		{
			std::fprintf(stderr, "%s\n", error.c_str());
			return 1;
		}
		return 0;
	}
	return usage();
}
