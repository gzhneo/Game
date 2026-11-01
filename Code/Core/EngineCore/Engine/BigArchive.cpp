/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// BigArchive.cpp -- directory of a .big archive, see BigArchive.h.

#include "Engine/BigArchive.h"

#include <cstring>

namespace Engine
{

namespace
{

const std::size_t HEADER_SIZE = 16;
const std::size_t MIN_ENTRY_SIZE = 9;	// offset, size, empty name

// Reads the directory in blocks instead of one call per name character.
class BlockReader
{
public:
	explicit BlockReader(const BigArchiveRead &read) : m_read(read) {}

	bool get(void *dest, std::size_t size)
	{
		unsigned char *out = static_cast<unsigned char *>(dest);
		while (size > 0)
		{
			if (!fill())
				return false;
			const std::size_t n = size < m_end - m_pos ? size : m_end - m_pos;
			std::memcpy(out, m_buffer + m_pos, n);
			m_pos += n;
			out += n;
			size -= n;
		}
		return true;
	}

	bool getByte(unsigned char &value) { return get(&value, 1); }

private:
	bool fill()
	{
		if (m_pos < m_end)
			return true;
		m_pos = 0;
		m_end = m_read(m_buffer, sizeof(m_buffer));
		if (m_end > sizeof(m_buffer))
			m_end = 0;
		return m_end > 0;
	}

	const BigArchiveRead &m_read;
	unsigned char m_buffer[16384];
	std::size_t m_pos = 0;
	std::size_t m_end = 0;
};

std::uint32_t bigEndian(const unsigned char *p)
{
	return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}

}	// namespace

BigArchiveError readBigArchiveDirectory(const BigArchiveRead &read, std::uint64_t archiveSize,
	std::vector<BigArchiveEntry> &entries)
{
	entries.clear();
	BlockReader reader(read);

	unsigned char header[HEADER_SIZE];
	if (!reader.get(header, sizeof(header)))
		return BigArchiveError::TruncatedHeader;
	if (std::memcmp(header, "BIGF", 4) != 0)
		return BigArchiveError::NotBig;

	const std::uint32_t count = bigEndian(header + 8);
	if (archiveSize < HEADER_SIZE || count > (archiveSize - HEADER_SIZE) / MIN_ENTRY_SIZE)
		return BigArchiveError::BadEntryCount;

	entries.reserve(count);
	for (std::uint32_t i = 0; i < count; ++i)
	{
		unsigned char position[8];
		if (!reader.get(position, sizeof(position)))
			return BigArchiveError::TruncatedEntry;
		BigArchiveEntry entry;
		entry.offset = bigEndian(position);
		entry.size = bigEndian(position + 4);

		for (;;)
		{
			unsigned char c;
			if (!reader.getByte(c))
				return BigArchiveError::TruncatedEntry;
			if (c == 0)
				break;
			if (entry.path.size() >= BIG_ARCHIVE_MAX_NAME)
				return BigArchiveError::NameTooLong;
			entry.path.push_back(static_cast<char>(c));
		}

		if (std::uint64_t(entry.offset) + entry.size > archiveSize)
			return BigArchiveError::EntryOutsideArchive;
		entries.push_back(std::move(entry));
	}
	return BigArchiveError::None;
}

const char *bigArchiveErrorText(BigArchiveError error)
{
	switch (error)
	{
		case BigArchiveError::None: return "no error";
		case BigArchiveError::NotBig: return "not a BIG archive";
		case BigArchiveError::TruncatedHeader: return "header cut off";
		case BigArchiveError::BadEntryCount: return "entry count larger than the archive";
		case BigArchiveError::TruncatedEntry: return "directory cut off";
		case BigArchiveError::NameTooLong: return "entry name too long";
		case BigArchiveError::EntryOutsideArchive: return "entry beyond the end of the archive";
	}
	return "unknown error";
}

}	// namespace Engine
