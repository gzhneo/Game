/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Model/Zip.h"

#include "Engine/ZipArchive.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>

namespace Babylon
{

namespace
{

void put16(std::vector<std::uint8_t> &out, std::uint32_t v)
{
	out.push_back(static_cast<std::uint8_t>(v & 0xFF));
	out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void put32(std::vector<std::uint8_t> &out, std::uint32_t v)
{
	put16(out, v & 0xFFFF);
	put16(out, v >> 16);
}

// 1 January 2010, 00:00: a fixed time keeps the files of the same content identical.
const std::uint32_t DOS_TIME = 0;
const std::uint32_t DOS_DATE = ((2010 - 1980) << 9) | (1 << 5) | 1;

std::string deflateRaw(std::string_view data)
{
	z_stream s;
	std::memset(&s, 0, sizeof(s));
	deflateInit2(&s, 6, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
	std::string out(deflateBound(&s, static_cast<uLong>(data.size())), '\0');
	s.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
	s.avail_in = static_cast<uInt>(data.size());
	s.next_out = reinterpret_cast<Bytef *>(out.data());
	s.avail_out = static_cast<uInt>(out.size());
	deflate(&s, Z_FINISH);
	out.resize(s.total_out);
	deflateEnd(&s);
	return out;
}

} // namespace

void ZipWriter::add(const std::string &name, std::string_view data, bool stored)
{
	Entry e;
	e.name = name;
	e.crc = static_cast<std::uint32_t>(crc32(0, reinterpret_cast<const Bytef *>(data.data()), static_cast<uInt>(data.size())));
	e.size = static_cast<std::uint32_t>(data.size());
	e.offset = static_cast<std::uint32_t>(m_out.size());
	std::string packed;
	if (stored)
		e.method = 0;
	else
	{
		packed = deflateRaw(data);
		e.method = 8;
	}
	const std::string_view body = stored ? data : std::string_view(packed);
	e.packed = static_cast<std::uint32_t>(body.size());

	put32(m_out, 0x04034B50);
	put16(m_out, 20);
	put16(m_out, 0x0800);	// names are UTF-8
	put16(m_out, e.method);
	put16(m_out, DOS_TIME);
	put16(m_out, DOS_DATE);
	put32(m_out, e.crc);
	put32(m_out, e.packed);
	put32(m_out, e.size);
	put16(m_out, static_cast<std::uint32_t>(name.size()));
	put16(m_out, 0);
	m_out.insert(m_out.end(), name.begin(), name.end());
	m_out.insert(m_out.end(), body.begin(), body.end());
	m_entries.push_back(std::move(e));
}

std::vector<std::uint8_t> ZipWriter::finish()
{
	const auto dirStart = static_cast<std::uint32_t>(m_out.size());
	for (const Entry &e : m_entries)
	{
		put32(m_out, 0x02014B50);
		put16(m_out, 20);
		put16(m_out, 20);
		put16(m_out, 0x0800);
		put16(m_out, e.method);
		put16(m_out, DOS_TIME);
		put16(m_out, DOS_DATE);
		put32(m_out, e.crc);
		put32(m_out, e.packed);
		put32(m_out, e.size);
		put16(m_out, static_cast<std::uint32_t>(e.name.size()));
		put16(m_out, 0);
		put16(m_out, 0);
		put16(m_out, 0);
		put16(m_out, 0);
		put32(m_out, 0);
		put32(m_out, e.offset);
		m_out.insert(m_out.end(), e.name.begin(), e.name.end());
	}
	const auto dirSize = static_cast<std::uint32_t>(m_out.size()) - dirStart;
	put32(m_out, 0x06054B50);
	put16(m_out, 0);
	put16(m_out, 0);
	put16(m_out, static_cast<std::uint32_t>(m_entries.size()));
	put16(m_out, static_cast<std::uint32_t>(m_entries.size()));
	put32(m_out, dirSize);
	put32(m_out, dirStart);
	put16(m_out, 0);
	std::vector<std::uint8_t> out;
	out.swap(m_out);
	m_entries.clear();
	return out;
}

bool readZip(std::span<const std::uint8_t> data, std::map<std::string, std::string> &files, std::string &error)
{
	files.clear();
	const Engine::ZipArchiveReadAt readAt = [data](std::uint64_t offset, void *dest, std::size_t size) -> std::size_t {
		if (offset >= data.size())
			return 0;
		const std::size_t n = std::min<std::size_t>(size, data.size() - static_cast<std::size_t>(offset));
		std::memcpy(dest, data.data() + offset, n);
		return n;
	};
	std::vector<Engine::ZipArchiveEntry> entries;
	if (Engine::readZipArchiveDirectory(readAt, data.size(), entries) != Engine::ZipArchiveError::None)
	{
		error = "not a readable ZIP archive";
		return false;
	}
	for (const Engine::ZipArchiveEntry &entry : entries)
	{
		std::vector<unsigned char> content;
		if (Engine::extractZipEntry(readAt, data.size(), entry, content) != Engine::ZipArchiveError::None)
		{
			error = "broken entry " + entry.path;
			return false;
		}
		std::string path = entry.path;
		for (char &c : path)
			if (c == '\\')
				c = '/';
		files[path] = std::string(content.begin(), content.end());
	}
	return true;
}

} // namespace Babylon
