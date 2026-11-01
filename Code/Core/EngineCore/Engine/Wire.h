/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Wire.h -- writing and reading the fields of network messages.
//
// Integers are little endian, texts carry their length in front (one byte, or two for long texts),
// endpoints are family | address | port. The writer never writes past its buffer; the reader never
// reads past its input and refuses a text longer than the field allows. Both keep a sticky error:
// after the first failure every further call fails, so a caller checks ok() once at the end.
//
// No engine header: tested by EngineTests.

#pragma once

#ifndef ENGINE_WIRE_H
#define ENGINE_WIRE_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace Engine
{

/// An IPv4 or IPv6 address with port; family 0 is "none".
struct WireEndpoint
{
	std::uint8_t family = 0;			///< 0, 4 or 6
	std::uint8_t address[16] = {};		///< IPv4 in the first four bytes, network order
	std::uint16_t port = 0;

	static WireEndpoint ipv4(std::uint32_t hostOrderAddress, std::uint16_t port);
	std::uint32_t ipv4Address() const;	///< host order; 0 unless family 4
	bool isSet() const { return family == 4 || family == 6; }
	bool operator==(const WireEndpoint &other) const;
	bool operator!=(const WireEndpoint &other) const { return !(*this == other); }
	std::string text() const;			///< "1.2.3.4:8086" or "[2001:db8::1]:8086"
};

class WireWriter
{
public:
	WireWriter(std::uint8_t *buffer, std::size_t capacity) : m_buffer(buffer), m_capacity(capacity) {}

	void u8(std::uint8_t value);
	void u16(std::uint16_t value);
	void u32(std::uint32_t value);
	void u64(std::uint64_t value);
	void bytes(const void *data, std::size_t size);
	void zeros(std::size_t size);
	/// Length byte and text; a text longer than maxBytes (at most 255) fails the writer.
	void text(const std::string &value, std::size_t maxBytes);
	/// Two length bytes, for texts up to 65535.
	void longText(const std::string &value, std::size_t maxBytes);
	void endpoint(const WireEndpoint &value);

	bool ok() const { return m_ok; }
	std::size_t size() const { return m_size; }
	/// Writes a 16-bit value at an earlier position (a length known only at the end).
	void patchU16(std::size_t position, std::uint16_t value);

private:
	bool reserve(std::size_t size);

	std::uint8_t *m_buffer;
	std::size_t m_capacity;
	std::size_t m_size = 0;
	bool m_ok = true;
};

class WireReader
{
public:
	WireReader(const std::uint8_t *data, std::size_t size) : m_data(data), m_size(size) {}

	std::uint8_t u8();
	std::uint16_t u16();
	std::uint32_t u32();
	std::uint64_t u64();
	void bytes(void *out, std::size_t size);
	void skip(std::size_t size);
	/// Skips padding; fails unless every byte is zero (one form per message).
	void zeros(std::size_t size);
	/// Refuses texts longer than maxBytes and texts with control characters (below 0x20, 0x7F).
	std::string text(std::size_t maxBytes);
	std::string longText(std::size_t maxBytes);
	/// Family 0, 4 or 6; anything else fails.
	WireEndpoint endpoint();
	/// A boolean byte: 0 or 1, anything else fails.
	bool flag();

	bool ok() const { return m_ok; }
	std::size_t remaining() const { return m_ok ? m_size - m_position : 0; }
	/// True if everything was read without error and nothing is left.
	bool done() const { return m_ok && m_position == m_size; }
	void fail() { m_ok = false; }

private:
	bool take(std::size_t size);
	std::string textOf(std::size_t length, std::size_t maxBytes);

	const std::uint8_t *m_data;
	std::size_t m_size;
	std::size_t m_position = 0;
	bool m_ok = true;
};

} // namespace Engine

#endif // ENGINE_WIRE_H
