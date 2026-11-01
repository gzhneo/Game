/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ZipArchive.h -- reads the central directory of a .zip archive and unpacks its entries (stored or
// deflate, through zlib). Needs no engine header, so EngineTests and the fuzzer test it directly;
// Engine::Vfs::ZipSource uses it.
//
// Mods come from the net, so every value of the archive is checked: the end record, the directory
// against the archive, each entry against the directory, sizes and the compression ratio against
// fixed limits (zip bombs), names against "..", absolute paths and drive letters. A broken
// directory rejects the whole archive; an entry the reader cannot serve (encrypted, other
// compression, bad name, too large) is left out and reported. Unpacking stops at the size the
// directory announces and checks the CRC.
//
// Not supported: Zip64 (archives and entries beyond 4 GiB or 65535 entries), archives split over
// several files, archives with data in front (self-extracting), encryption.

#pragma once

#ifndef ENGINE_ZIPARCHIVE_H
#define ENGINE_ZIPARCHIVE_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Engine
{

struct ZipArchiveEntry
{
	std::string path;					///< as stored, '/' or '\\' as separator
	std::uint64_t headerOffset = 0;		///< local file header
	std::uint64_t compressedSize = 0;
	std::uint64_t size = 0;				///< unpacked
	std::uint32_t crc = 0;
	std::uint16_t method = 0;			///< ZIP_METHOD_STORED or ZIP_METHOD_DEFLATE
};

enum class ZipArchiveError
{
	None,
	// whole archive
	NotZip,					///< no end record
	MultiDisk,				///< split over several files
	Zip64,
	BadDirectory,			///< directory outside the archive or not where the end record says
	DirectoryTooLarge,
	BadEntryCount,			///< more entries than the directory can hold
	TruncatedDirectory,
	// one entry
	Encrypted,
	UnsupportedMethod,
	BadPath,				///< "..", absolute, drive letter, control character or empty
	NameTooLong,
	EntryTooLarge,			///< unpacked size above ZIP_ARCHIVE_MAX_ENTRY_SIZE
	ImplausibleRatio,		///< announced size larger than deflate can produce from the packed size
	EntryOutsideArchive,
	BadLocalHeader,
	CorruptData,			///< deflate stream broken or ends early
	SizeMismatch,			///< unpacks to another size than announced
	CrcMismatch,
	ReadFailed
};

const std::uint16_t ZIP_METHOD_STORED = 0;
const std::uint16_t ZIP_METHOD_DEFLATE = 8;

/// Longest entry path, as for .big archives (the engine keeps names in _MAX_PATH buffers).
const std::size_t ZIP_ARCHIVE_MAX_NAME = 259;
/// Largest unpacked entry; the engine reads files with 32-bit sizes and keeps them in memory.
const std::uint64_t ZIP_ARCHIVE_MAX_ENTRY_SIZE = 1024ull * 1024 * 1024;
/// Largest central directory read into memory.
const std::uint64_t ZIP_ARCHIVE_MAX_DIRECTORY = 64ull * 1024 * 1024;
/// Deflate cannot expand by more than about 1032:1; a larger announced ratio is a lie.
const std::uint64_t ZIP_ARCHIVE_MAX_RATIO = 1100;

/// Reads up to size bytes at offset and returns how many were read (fewer at the end).
using ZipArchiveReadAt = std::function<std::size_t(std::uint64_t offset, void *dest, std::size_t size)>;

struct ZipArchiveSkipped
{
	std::string path;
	ZipArchiveError reason = ZipArchiveError::None;
};

/// Reads the central directory. archiveSize is the real size of the file. Folder entries are left
/// out silently, entries that cannot be served go to *skipped. On an error the archive must not be
/// used.
ZipArchiveError readZipArchiveDirectory(const ZipArchiveReadAt &readAt, std::uint64_t archiveSize,
	std::vector<ZipArchiveEntry> &entries, std::vector<ZipArchiveSkipped> *skipped = nullptr);

/// Start of the entry's data behind its local header, checked against the archive.
ZipArchiveError locateZipEntryData(const ZipArchiveReadAt &readAt, std::uint64_t archiveSize,
	const ZipArchiveEntry &entry, std::uint64_t &dataOffset);

/// Unpacks the whole entry into data and checks size and CRC. data is empty on an error.
ZipArchiveError extractZipEntry(const ZipArchiveReadAt &readAt, std::uint64_t archiveSize,
	const ZipArchiveEntry &entry, std::vector<unsigned char> &data);

const char *zipArchiveErrorText(ZipArchiveError error);

}	// namespace Engine

#endif	// ENGINE_ZIPARCHIVE_H
