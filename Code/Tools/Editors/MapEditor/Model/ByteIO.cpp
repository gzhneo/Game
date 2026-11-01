/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ByteIO.cpp -- bounds-checked little-endian reading and writing of byte buffers.

#include "Model/ByteIO.h"

#include <bit>
#include <cstring>

namespace MapModel
{

void ByteReader::fail(const std::string &message)
{
	if (m_error.empty())
		m_error = message;
	m_pos = m_data.size();
}

bool ByteReader::need(std::size_t count)
{
	if (!ok())
		return false;
	if (count > remaining())
	{
		fail("unexpected end of data at offset " + std::to_string(m_pos));
		return false;
	}
	return true;
}

std::uint8_t ByteReader::u8()
{
	if (!need(1))
		return 0;
	return m_data[m_pos++];
}

std::uint16_t ByteReader::u16()
{
	if (!need(2))
		return 0;
	const std::uint16_t v = static_cast<std::uint16_t>(m_data[m_pos] | (m_data[m_pos + 1] << 8));
	m_pos += 2;
	return v;
}

std::uint32_t ByteReader::u32()
{
	if (!need(4))
		return 0;
	const std::uint32_t v = static_cast<std::uint32_t>(m_data[m_pos])
		| (static_cast<std::uint32_t>(m_data[m_pos + 1]) << 8)
		| (static_cast<std::uint32_t>(m_data[m_pos + 2]) << 16)
		| (static_cast<std::uint32_t>(m_data[m_pos + 3]) << 24);
	m_pos += 4;
	return v;
}

float ByteReader::f32()
{
	return std::bit_cast<float>(u32());
}

std::span<const std::uint8_t> ByteReader::bytes(std::size_t count)
{
	if (!need(count))
		return {};
	const auto result = m_data.subspan(m_pos, count);
	m_pos += count;
	return result;
}

std::span<const std::uint8_t> ByteReader::rest()
{
	return bytes(remaining());
}

std::size_t ByteReader::count(std::int32_t value, std::size_t minBytesEach, const char *what)
{
	if (!ok())
		return 0;
	if (value < 0)
	{
		fail(std::string("negative count for ") + what);
		return 0;
	}
	const std::size_t n = static_cast<std::size_t>(value);
	if (minBytesEach > 0 && n > remaining() / minBytesEach)
	{
		fail(std::string("count for ") + what + " exceeds the data left (" + std::to_string(n) + ")");
		return 0;
	}
	return n;
}

void ByteWriter::u16(std::uint16_t v)
{
	m_data.push_back(static_cast<std::uint8_t>(v));
	m_data.push_back(static_cast<std::uint8_t>(v >> 8));
}

void ByteWriter::u32(std::uint32_t v)
{
	m_data.push_back(static_cast<std::uint8_t>(v));
	m_data.push_back(static_cast<std::uint8_t>(v >> 8));
	m_data.push_back(static_cast<std::uint8_t>(v >> 16));
	m_data.push_back(static_cast<std::uint8_t>(v >> 24));
}

void ByteWriter::f32(float v)
{
	u32(std::bit_cast<std::uint32_t>(v));
}

void ByteWriter::patchU32(std::size_t pos, std::uint32_t v)
{
	m_data[pos] = static_cast<std::uint8_t>(v);
	m_data[pos + 1] = static_cast<std::uint8_t>(v >> 8);
	m_data[pos + 2] = static_cast<std::uint8_t>(v >> 16);
	m_data[pos + 3] = static_cast<std::uint8_t>(v >> 24);
}

} // namespace MapModel
