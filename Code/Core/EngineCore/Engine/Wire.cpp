/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Wire.cpp -- see Wire.h.

#include "Engine/Wire.h"

#include <cstring>
#include <format>
#include <iterator>

namespace Engine
{

WireEndpoint WireEndpoint::ipv4(std::uint32_t hostOrderAddress, std::uint16_t port)
{
	WireEndpoint e;
	e.family = 4;
	e.address[0] = static_cast<std::uint8_t>(hostOrderAddress >> 24);
	e.address[1] = static_cast<std::uint8_t>(hostOrderAddress >> 16);
	e.address[2] = static_cast<std::uint8_t>(hostOrderAddress >> 8);
	e.address[3] = static_cast<std::uint8_t>(hostOrderAddress);
	e.port = port;
	return e;
}

std::uint32_t WireEndpoint::ipv4Address() const
{
	if (family != 4)
		return 0;
	return (static_cast<std::uint32_t>(address[0]) << 24) | (static_cast<std::uint32_t>(address[1]) << 16)
		| (static_cast<std::uint32_t>(address[2]) << 8) | static_cast<std::uint32_t>(address[3]);
}

bool WireEndpoint::operator==(const WireEndpoint &other) const
{
	if (family != other.family || port != other.port)
		return false;
	const std::size_t length = family == 6 ? 16 : (family == 4 ? 4 : 0);
	return std::memcmp(address, other.address, length) == 0;
}

std::string WireEndpoint::text() const
{
	if (family == 4)
		return std::format("{}.{}.{}.{}:{}", address[0], address[1], address[2], address[3], port);
	if (family == 6)
	{
		std::string text = "[";
		for (int i = 0; i < 16; i += 2)
			std::format_to(std::back_inserter(text), "{}{:x}", i ? ":" : "", (address[i] << 8) | address[i + 1]);
		std::format_to(std::back_inserter(text), "]:{}", port);
		return text;
	}
	return "-";
}

// --- writer -------------------------------------------------------------------------------------

bool WireWriter::reserve(std::size_t size)
{
	if (!m_ok || m_buffer == nullptr || size > m_capacity - m_size)
	{
		m_ok = false;
		return false;
	}
	return true;
}

void WireWriter::u8(std::uint8_t value)
{
	if (reserve(1))
		m_buffer[m_size++] = value;
}

void WireWriter::u16(std::uint16_t value)
{
	if (!reserve(2))
		return;
	m_buffer[m_size++] = static_cast<std::uint8_t>(value);
	m_buffer[m_size++] = static_cast<std::uint8_t>(value >> 8);
}

void WireWriter::u32(std::uint32_t value)
{
	if (!reserve(4))
		return;
	for (int i = 0; i < 4; ++i)
		m_buffer[m_size++] = static_cast<std::uint8_t>(value >> (8 * i));
}

void WireWriter::u64(std::uint64_t value)
{
	if (!reserve(8))
		return;
	for (int i = 0; i < 8; ++i)
		m_buffer[m_size++] = static_cast<std::uint8_t>(value >> (8 * i));
}

void WireWriter::bytes(const void *data, std::size_t size)
{
	if (size == 0 || !reserve(size))
		return;
	if (data == nullptr)
	{
		m_ok = false;
		return;
	}
	std::memcpy(m_buffer + m_size, data, size);
	m_size += size;
}

void WireWriter::zeros(std::size_t size)
{
	if (size == 0 || !reserve(size))
		return;
	std::memset(m_buffer + m_size, 0, size);
	m_size += size;
}

void WireWriter::text(const std::string &value, std::size_t maxBytes)
{
	if (value.size() > maxBytes || value.size() > 255)
	{
		m_ok = false;
		return;
	}
	u8(static_cast<std::uint8_t>(value.size()));
	bytes(value.data(), value.size());
}

void WireWriter::longText(const std::string &value, std::size_t maxBytes)
{
	if (value.size() > maxBytes || value.size() > 65535)
	{
		m_ok = false;
		return;
	}
	u16(static_cast<std::uint16_t>(value.size()));
	bytes(value.data(), value.size());
}

void WireWriter::endpoint(const WireEndpoint &value)
{
	if (value.family != 0 && value.family != 4 && value.family != 6)
	{
		m_ok = false;
		return;
	}
	u8(value.family);
	if (value.family == 4)
		bytes(value.address, 4);
	else if (value.family == 6)
		bytes(value.address, 16);
	if (value.family != 0)
		u16(value.port);
}

void WireWriter::patchU16(std::size_t position, std::uint16_t value)
{
	if (!m_ok || position + 2 > m_size)
	{
		m_ok = false;
		return;
	}
	m_buffer[position] = static_cast<std::uint8_t>(value);
	m_buffer[position + 1] = static_cast<std::uint8_t>(value >> 8);
}

// --- reader -------------------------------------------------------------------------------------

bool WireReader::take(std::size_t size)
{
	if (!m_ok || m_data == nullptr || size > m_size - m_position)
	{
		m_ok = false;
		return false;
	}
	return true;
}

std::uint8_t WireReader::u8()
{
	if (!take(1))
		return 0;
	return m_data[m_position++];
}

std::uint16_t WireReader::u16()
{
	if (!take(2))
		return 0;
	const std::uint16_t value = static_cast<std::uint16_t>(m_data[m_position] | (m_data[m_position + 1] << 8));
	m_position += 2;
	return value;
}

std::uint32_t WireReader::u32()
{
	if (!take(4))
		return 0;
	std::uint32_t value = 0;
	for (int i = 0; i < 4; ++i)
		value |= static_cast<std::uint32_t>(m_data[m_position + static_cast<std::size_t>(i)]) << (8 * i);
	m_position += 4;
	return value;
}

std::uint64_t WireReader::u64()
{
	if (!take(8))
		return 0;
	std::uint64_t value = 0;
	for (int i = 0; i < 8; ++i)
		value |= static_cast<std::uint64_t>(m_data[m_position + static_cast<std::size_t>(i)]) << (8 * i);
	m_position += 8;
	return value;
}

void WireReader::bytes(void *out, std::size_t size)
{
	if (size == 0)
		return;
	if (!take(size) || out == nullptr)
	{
		m_ok = false;
		if (out != nullptr)
			std::memset(out, 0, size);
		return;
	}
	std::memcpy(out, m_data + m_position, size);
	m_position += size;
}

void WireReader::skip(std::size_t size)
{
	if (take(size))
		m_position += size;
}

void WireReader::zeros(std::size_t size)
{
	if (!take(size))
		return;
	for (std::size_t i = 0; i < size; ++i)
	{
		if (m_data[m_position + i] != 0)
		{
			m_ok = false;
			return;
		}
	}
	m_position += size;
}

std::string WireReader::textOf(std::size_t length, std::size_t maxBytes)
{
	if (!m_ok || length > maxBytes || !take(length))
	{
		m_ok = false;
		return std::string();
	}
	std::string value(reinterpret_cast<const char *>(m_data + m_position), length);
	m_position += length;
	for (char c : value)
	{
		const unsigned char u = static_cast<unsigned char>(c);
		if (u < 0x20 || u == 0x7F)
		{
			m_ok = false;
			return std::string();
		}
	}
	return value;
}

std::string WireReader::text(std::size_t maxBytes)
{
	const std::size_t length = u8();
	return textOf(length, maxBytes);
}

std::string WireReader::longText(std::size_t maxBytes)
{
	const std::size_t length = u16();
	return textOf(length, maxBytes);
}

WireEndpoint WireReader::endpoint()
{
	WireEndpoint e;
	e.family = u8();
	if (e.family == 4)
		bytes(e.address, 4);
	else if (e.family == 6)
		bytes(e.address, 16);
	else if (e.family != 0)
		m_ok = false;
	if (e.family == 4 || e.family == 6)
		e.port = u16();
	if (!m_ok)
		return WireEndpoint();
	return e;
}

bool WireReader::flag()
{
	const std::uint8_t value = u8();
	if (value > 1)
		m_ok = false;
	return value == 1;
}

} // namespace Engine
