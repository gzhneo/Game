/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Zip.h -- the ZIP container of .xlsx and .ods files: writing (deflate through zlib) and reading
// all entries into memory (Engine::ZipArchive, which checks every size and the CRC).

#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Babylon
{

class ZipWriter
{
public:
	// stored: uncompressed (the mimetype entry of OpenDocument must be).
	void add(const std::string &name, std::string_view data, bool stored = false);
	std::vector<std::uint8_t> finish();

private:
	struct Entry
	{
		std::string name;
		std::uint32_t crc;
		std::uint32_t size;
		std::uint32_t packed;
		std::uint16_t method;
		std::uint32_t offset;
	};
	std::vector<Entry> m_entries;
	std::vector<std::uint8_t> m_out;
};

// Entry path -> content. False with a message if the archive is broken.
bool readZip(std::span<const std::uint8_t> data, std::map<std::string, std::string> &files, std::string &error);

} // namespace Babylon
