/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ZipArchive.cpp -- directory and entries of a .zip archive, see ZipArchive.h.

#include "Engine/ZipArchive.h"

#include <zlib.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <new>

namespace Engine
{

namespace
{

const std::size_t END_RECORD_SIZE = 22;
const std::size_t MAX_COMMENT = 65535;
const std::size_t CENTRAL_HEADER_SIZE = 46;
const std::size_t LOCAL_HEADER_SIZE = 30;
const std::size_t ZIP64_LOCATOR_SIZE = 20;
const std::uint32_t END_RECORD_SIGNATURE = 0x06054b50;
const std::uint32_t ZIP64_LOCATOR_SIGNATURE = 0x07064b50;
const std::uint32_t CENTRAL_HEADER_SIGNATURE = 0x02014b50;
const std::uint32_t LOCAL_HEADER_SIGNATURE = 0x04034b50;
const std::uint16_t FLAG_ENCRYPTED = 0x0001;
const std::uint16_t FLAG_STRONG_ENCRYPTION = 0x0040;
const std::size_t INFLATE_CHUNK = 65536;

std::uint16_t le16(const unsigned char *p)
{
	return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t le32(const unsigned char *p)
{
	return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

/// Reads exactly size bytes; a short read means the archive ends early.
bool readExact(const ZipArchiveReadAt &readAt, std::uint64_t offset, void *dest, std::size_t size)
{
	unsigned char *out = static_cast<unsigned char *>(dest);
	while (size > 0)
	{
		const std::size_t got = readAt(offset, out, size);
		if (got == 0 || got > size)
			return false;
		offset += got;
		out += got;
		size -= got;
	}
	return true;
}

/// Names must stay below the mount point: no "..", no absolute path, no drive, no control bytes.
ZipArchiveError checkPath(const std::string &path)
{
	if (path.empty() || path[0] == '/' || path[0] == '\\')
		return ZipArchiveError::BadPath;
	bool anyPart = false;
	std::size_t pos = 0;
	while (pos <= path.size())
	{
		std::size_t end = path.find_first_of("/\\", pos);
		if (end == std::string::npos)
			end = path.size();
		const std::string part = path.substr(pos, end - pos);
		pos = end + 1;
		if (part == "..")
			return ZipArchiveError::BadPath;
		if (!part.empty() && part != ".")
			anyPart = true;
	}
	for (char c : path)
	{
		if (static_cast<unsigned char>(c) < 0x20 || c == ':')
			return ZipArchiveError::BadPath;
	}
	return anyPart ? ZipArchiveError::None : ZipArchiveError::BadPath;
}

std::uint32_t crcOf(const std::vector<unsigned char> &data)
{
	uLong crc = crc32(0L, Z_NULL, 0);
	std::size_t pos = 0;
	while (pos < data.size())
	{
		const std::size_t n = std::min<std::size_t>(data.size() - pos, UINT_MAX);
		crc = crc32(crc, data.data() + pos, static_cast<uInt>(n));
		pos += n;
	}
	return static_cast<std::uint32_t>(crc);
}

ZipArchiveError inflateEntry(const ZipArchiveReadAt &readAt, std::uint64_t dataOffset, const ZipArchiveEntry &entry,
	std::vector<unsigned char> &data)
{
	z_stream stream;
	std::memset(&stream, 0, sizeof(stream));
	if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
		return ZipArchiveError::CorruptData;

	std::vector<unsigned char> input(INFLATE_CHUNK);
	std::uint64_t inputLeft = entry.compressedSize;
	std::uint64_t inputPos = dataOffset;
	ZipArchiveError result = ZipArchiveError::None;
	for (;;)
	{
		if (stream.avail_in == 0 && inputLeft > 0)
		{
			const std::size_t chunk = static_cast<std::size_t>(std::min<std::uint64_t>(inputLeft, INFLATE_CHUNK));
			if (!readExact(readAt, inputPos, input.data(), chunk))
			{
				result = ZipArchiveError::ReadFailed;
				break;
			}
			stream.next_in = input.data();
			stream.avail_in = static_cast<uInt>(chunk);
			inputLeft -= chunk;
			inputPos += chunk;
		}

		// Output goes only up to the announced size; one spare byte shows whether the stream wants more.
		const std::uint64_t produced = stream.total_out;
		unsigned char spare = 0;
		const bool full = produced >= entry.size;
		if (full)
		{
			stream.next_out = &spare;
			stream.avail_out = 1;
		}
		else
		{
			stream.next_out = data.data() + produced;
			stream.avail_out = static_cast<uInt>(std::min<std::uint64_t>(entry.size - produced, UINT_MAX));
		}

		const int status = inflate(&stream, Z_NO_FLUSH);
		if (full && stream.avail_out == 0)
		{
			result = ZipArchiveError::SizeMismatch;
			break;
		}
		if (status == Z_STREAM_END)
		{
			if (stream.total_out != entry.size)
				result = ZipArchiveError::SizeMismatch;
			break;
		}
		if (status != Z_OK)
		{
			// Z_BUF_ERROR: no input left before the end of the stream.
			result = ZipArchiveError::CorruptData;
			break;
		}
	}
	inflateEnd(&stream);
	return result;
}

}	// namespace

ZipArchiveError readZipArchiveDirectory(const ZipArchiveReadAt &readAt, std::uint64_t archiveSize,
	std::vector<ZipArchiveEntry> &entries, std::vector<ZipArchiveSkipped> *skipped)
{
	entries.clear();
	if (skipped != nullptr)
		skipped->clear();
	if (archiveSize < END_RECORD_SIZE)
		return ZipArchiveError::NotZip;

	// The end record sits at the end, behind a comment of up to 64 KiB.
	const std::size_t tailSize = static_cast<std::size_t>(std::min<std::uint64_t>(archiveSize, END_RECORD_SIZE + MAX_COMMENT));
	const std::uint64_t tailStart = archiveSize - tailSize;
	std::vector<unsigned char> tail(tailSize);
	if (!readExact(readAt, tailStart, tail.data(), tailSize))
		return ZipArchiveError::NotZip;

	std::size_t found = tailSize;
	for (std::size_t i = tailSize - END_RECORD_SIZE + 1; i-- > 0;)
	{
		if (le32(&tail[i]) == END_RECORD_SIGNATURE && i + END_RECORD_SIZE + le16(&tail[i + 20]) <= tailSize)
		{
			found = i;
			break;
		}
	}
	if (found == tailSize)
		return ZipArchiveError::NotZip;

	const unsigned char *end = &tail[found];
	const std::uint64_t endPos = tailStart + found;
	const std::uint16_t disk = le16(end + 4);
	const std::uint16_t directoryDisk = le16(end + 6);
	const std::uint16_t diskEntries = le16(end + 8);
	const std::uint16_t count = le16(end + 10);
	const std::uint32_t directorySize = le32(end + 12);
	const std::uint32_t directoryOffset = le32(end + 16);

	if (count == 0xFFFF || directorySize == 0xFFFFFFFF || directoryOffset == 0xFFFFFFFF)
		return ZipArchiveError::Zip64;
	if (endPos >= ZIP64_LOCATOR_SIZE)
	{
		unsigned char locator[4];
		if (readExact(readAt, endPos - ZIP64_LOCATOR_SIZE, locator, sizeof(locator)) && le32(locator) == ZIP64_LOCATOR_SIGNATURE)
			return ZipArchiveError::Zip64;
	}
	if (disk != 0 || directoryDisk != 0 || diskEntries != count)
		return ZipArchiveError::MultiDisk;
	if (std::uint64_t(directoryOffset) + directorySize > endPos)
		return ZipArchiveError::BadDirectory;
	if (directorySize > ZIP_ARCHIVE_MAX_DIRECTORY)
		return ZipArchiveError::DirectoryTooLarge;
	if (count > directorySize / CENTRAL_HEADER_SIZE)
		return ZipArchiveError::BadEntryCount;

	std::vector<unsigned char> directory(directorySize);
	if (!readExact(readAt, directoryOffset, directory.data(), directory.size()))
		return ZipArchiveError::TruncatedDirectory;

	auto skip = [skipped](std::string path, ZipArchiveError reason)
	{
		if (skipped != nullptr)
			skipped->push_back({std::move(path), reason});
	};

	entries.reserve(count);
	std::size_t pos = 0;
	for (std::uint32_t i = 0; i < count; ++i)
	{
		if (directory.size() - pos < CENTRAL_HEADER_SIZE)
			return ZipArchiveError::TruncatedDirectory;
		const unsigned char *h = &directory[pos];
		if (le32(h) != CENTRAL_HEADER_SIGNATURE)
			return ZipArchiveError::BadDirectory;
		const std::uint16_t flags = le16(h + 8);
		const std::uint16_t method = le16(h + 10);
		const std::uint32_t crc = le32(h + 16);
		const std::uint32_t compressedSize = le32(h + 20);
		const std::uint32_t size = le32(h + 24);
		const std::size_t nameLength = le16(h + 28);
		const std::size_t extraLength = le16(h + 30);
		const std::size_t commentLength = le16(h + 32);
		const std::uint16_t startDisk = le16(h + 34);
		const std::uint32_t headerOffset = le32(h + 42);
		const std::size_t recordSize = CENTRAL_HEADER_SIZE + nameLength + extraLength + commentLength;
		if (directory.size() - pos < recordSize)
			return ZipArchiveError::TruncatedDirectory;
		std::string path(reinterpret_cast<const char *>(h + CENTRAL_HEADER_SIZE), nameLength);
		pos += recordSize;

		if (!path.empty() && (path.back() == '/' || path.back() == '\\'))
			continue;	// folder entry
		if (nameLength > ZIP_ARCHIVE_MAX_NAME)
		{
			skip(path.substr(0, ZIP_ARCHIVE_MAX_NAME), ZipArchiveError::NameTooLong);
			continue;
		}
		ZipArchiveError problem = checkPath(path);
		if (problem == ZipArchiveError::None && (flags & (FLAG_ENCRYPTED | FLAG_STRONG_ENCRYPTION)) != 0)
			problem = ZipArchiveError::Encrypted;
		if (problem == ZipArchiveError::None && method != ZIP_METHOD_STORED && method != ZIP_METHOD_DEFLATE)
			problem = ZipArchiveError::UnsupportedMethod;
		if (problem == ZipArchiveError::None
			&& (compressedSize == 0xFFFFFFFF || size == 0xFFFFFFFF || headerOffset == 0xFFFFFFFF))
			problem = ZipArchiveError::Zip64;
		if (problem == ZipArchiveError::None && startDisk != 0)
			problem = ZipArchiveError::MultiDisk;
		if (problem == ZipArchiveError::None && size > ZIP_ARCHIVE_MAX_ENTRY_SIZE)
			problem = ZipArchiveError::EntryTooLarge;
		if (problem == ZipArchiveError::None && method == ZIP_METHOD_STORED && compressedSize != size)
			problem = ZipArchiveError::SizeMismatch;
		if (problem == ZipArchiveError::None && method == ZIP_METHOD_DEFLATE
			&& std::uint64_t(size) > std::uint64_t(compressedSize) * ZIP_ARCHIVE_MAX_RATIO + 1024)
			problem = ZipArchiveError::ImplausibleRatio;
		if (problem == ZipArchiveError::None
			&& std::uint64_t(headerOffset) + LOCAL_HEADER_SIZE + compressedSize > directoryOffset)
			problem = ZipArchiveError::EntryOutsideArchive;
		if (problem != ZipArchiveError::None)
		{
			skip(std::move(path), problem);
			continue;
		}

		ZipArchiveEntry entry;
		entry.path = std::move(path);
		entry.headerOffset = headerOffset;
		entry.compressedSize = compressedSize;
		entry.size = size;
		entry.crc = crc;
		entry.method = method;
		entries.push_back(std::move(entry));
	}
	return ZipArchiveError::None;
}

ZipArchiveError locateZipEntryData(const ZipArchiveReadAt &readAt, std::uint64_t archiveSize,
	const ZipArchiveEntry &entry, std::uint64_t &dataOffset)
{
	dataOffset = 0;
	if (entry.headerOffset > archiveSize || archiveSize - entry.headerOffset < LOCAL_HEADER_SIZE)
		return ZipArchiveError::EntryOutsideArchive;
	unsigned char header[LOCAL_HEADER_SIZE];
	if (!readExact(readAt, entry.headerOffset, header, sizeof(header)))
		return ZipArchiveError::ReadFailed;
	if (le32(header) != LOCAL_HEADER_SIGNATURE || le16(header + 8) != entry.method)
		return ZipArchiveError::BadLocalHeader;
	if ((le16(header + 6) & (FLAG_ENCRYPTED | FLAG_STRONG_ENCRYPTION)) != 0)
		return ZipArchiveError::Encrypted;
	const std::uint64_t start = entry.headerOffset + LOCAL_HEADER_SIZE + le16(header + 26) + le16(header + 28);
	if (start > archiveSize || archiveSize - start < entry.compressedSize)
		return ZipArchiveError::EntryOutsideArchive;
	dataOffset = start;
	return ZipArchiveError::None;
}

ZipArchiveError extractZipEntry(const ZipArchiveReadAt &readAt, std::uint64_t archiveSize,
	const ZipArchiveEntry &entry, std::vector<unsigned char> &data)
{
	data.clear();
	if (entry.size > ZIP_ARCHIVE_MAX_ENTRY_SIZE)
		return ZipArchiveError::EntryTooLarge;
	if (entry.method == ZIP_METHOD_STORED && entry.compressedSize != entry.size)
		return ZipArchiveError::SizeMismatch;
	if (entry.method != ZIP_METHOD_STORED && entry.method != ZIP_METHOD_DEFLATE)
		return ZipArchiveError::UnsupportedMethod;

	std::uint64_t dataOffset = 0;
	ZipArchiveError result = locateZipEntryData(readAt, archiveSize, entry, dataOffset);
	if (result != ZipArchiveError::None)
		return result;

	try
	{
		data.resize(static_cast<std::size_t>(entry.size));
	}
	catch (const std::bad_alloc &)
	{
		return ZipArchiveError::EntryTooLarge;
	}

	if (entry.method == ZIP_METHOD_STORED)
		result = readExact(readAt, dataOffset, data.data(), data.size()) ? ZipArchiveError::None : ZipArchiveError::ReadFailed;
	else
		result = inflateEntry(readAt, dataOffset, entry, data);
	if (result == ZipArchiveError::None && crcOf(data) != entry.crc)
		result = ZipArchiveError::CrcMismatch;
	if (result != ZipArchiveError::None)
		data.clear();
	return result;
}

const char *zipArchiveErrorText(ZipArchiveError error)
{
	switch (error)
	{
		case ZipArchiveError::None: return "no error";
		case ZipArchiveError::NotZip: return "not a ZIP archive (no end record)";
		case ZipArchiveError::MultiDisk: return "archive split over several files";
		case ZipArchiveError::Zip64: return "Zip64 not supported";
		case ZipArchiveError::BadDirectory: return "central directory damaged or outside the archive";
		case ZipArchiveError::DirectoryTooLarge: return "central directory too large";
		case ZipArchiveError::BadEntryCount: return "entry count larger than the directory";
		case ZipArchiveError::TruncatedDirectory: return "central directory cut off";
		case ZipArchiveError::Encrypted: return "encrypted entry";
		case ZipArchiveError::UnsupportedMethod: return "compression method not supported";
		case ZipArchiveError::BadPath: return "path leaves the archive or is empty";
		case ZipArchiveError::NameTooLong: return "entry name too long";
		case ZipArchiveError::EntryTooLarge: return "entry too large";
		case ZipArchiveError::ImplausibleRatio: return "implausible compression ratio";
		case ZipArchiveError::EntryOutsideArchive: return "entry beyond the end of the archive";
		case ZipArchiveError::BadLocalHeader: return "local header damaged";
		case ZipArchiveError::CorruptData: return "compressed data damaged";
		case ZipArchiveError::SizeMismatch: return "size differs from the directory";
		case ZipArchiveError::CrcMismatch: return "CRC differs from the directory";
		case ZipArchiveError::ReadFailed: return "read failed";
	}
	return "unknown error";
}

}	// namespace Engine
