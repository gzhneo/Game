/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// BigArchive.h -- reads the directory of a .big archive. Needs no engine header, so EngineTests
// and the fuzzer test it directly; Win32BIGFileSystem::openArchiveFile uses it.
//
// Format: "BIGF", archive size (little endian, not trusted), entry count (big endian), header size
// (big endian, not used), then per entry: offset and size (big endian) and the NUL-terminated path.
// Archives come from mods and other players, so every value is checked: the entry count against the
// archive size, each name against a length limit, each entry against the end of the archive.

#pragma once

#ifndef ENGINE_BIGARCHIVE_H
#define ENGINE_BIGARCHIVE_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Engine
{

struct BigArchiveEntry
{
	std::string path;			///< as stored, with the directory part
	std::uint32_t offset = 0;
	std::uint32_t size = 0;
};

enum class BigArchiveError
{
	None,
	NotBig,						///< no "BIGF" at the start
	TruncatedHeader,
	BadEntryCount,				///< more entries than the archive can hold
	TruncatedEntry,
	NameTooLong,
	EntryOutsideArchive			///< offset + size beyond the end of the archive
};

/// Longest path of an entry, as the archive system keeps names in _MAX_PATH buffers.
const std::size_t BIG_ARCHIVE_MAX_NAME = 259;

/// Reads up to size bytes into dest and returns how many were read (0 at the end).
using BigArchiveRead = std::function<std::size_t(void *dest, std::size_t size)>;

/// Reads the directory from the start of the archive. archiveSize is the real size of the file.
/// On an error, entries holds what was read so far and the archive must not be used.
BigArchiveError readBigArchiveDirectory(const BigArchiveRead &read, std::uint64_t archiveSize,
	std::vector<BigArchiveEntry> &entries);

const char *bigArchiveErrorText(BigArchiveError error);

}	// namespace Engine

#endif	// ENGINE_BIGARCHIVE_H
