/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ByteIO.h -- bounds-checked little-endian reading and writing of byte buffers.
//
// The reader never throws and never reads past its span: after the first failure it returns zeros
// and keeps the first error message, so parsers can read a whole record and check ok() once.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace MapModel
{

class ByteReader
{
public:
	explicit ByteReader(std::span<const std::uint8_t> data) : m_data(data) {}

	bool ok() const { return m_error.empty(); }
	const std::string &error() const { return m_error; }
	void fail(const std::string &message);

	std::size_t position() const { return m_pos; }
	std::size_t remaining() const { return m_data.size() - m_pos; }
	bool atEnd() const { return m_pos >= m_data.size(); }

	std::uint8_t u8();
	std::uint16_t u16();
	std::uint32_t u32();
	std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
	float f32();
	bool boolean() { return u8() != 0; }

	// Returns an empty span (and fails) if fewer than count bytes are left.
	std::span<const std::uint8_t> bytes(std::size_t count);
	std::span<const std::uint8_t> rest();

	// Element count read from the file, checked against the bytes left: each element needs at
	// least minBytesEach bytes. Protects allocations against corrupt or hostile counts.
	std::size_t count(std::int32_t value, std::size_t minBytesEach, const char *what);

private:
	bool need(std::size_t count);

	std::span<const std::uint8_t> m_data;
	std::size_t m_pos = 0;
	std::string m_error;
};

class ByteWriter
{
public:
	void u8(std::uint8_t v) { m_data.push_back(v); }
	void u16(std::uint16_t v);
	void u32(std::uint32_t v);
	void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
	void f32(float v);
	void boolean(bool v) { u8(v ? 1 : 0); }
	void bytes(std::span<const std::uint8_t> data) { m_data.insert(m_data.end(), data.begin(), data.end()); }

	void patchU32(std::size_t pos, std::uint32_t v);

	std::size_t size() const { return m_data.size(); }
	const std::vector<std::uint8_t> &data() const { return m_data; }
	std::vector<std::uint8_t> take() { return std::move(m_data); }

private:
	std::vector<std::uint8_t> m_data;
};

} // namespace MapModel
