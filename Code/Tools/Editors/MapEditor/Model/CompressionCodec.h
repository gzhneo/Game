/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// CompressionCodec.h -- the game's CompressionManager behind plain types.
//
// Compression.h pulls in Lib/BaseType.h, whose min/max macros break the standard headers; only
// CompressionCodec.cpp includes it, and nothing after it.

#pragma once

#include <cstddef>
#include <cstdint>

namespace MapModel::Codec
{

bool isCompressed(const std::uint8_t *data, std::size_t size);
std::int32_t typeOf(const std::uint8_t *data, std::size_t size);
std::int32_t uncompressedSize(const std::uint8_t *data, std::size_t size);
std::int32_t decompress(const std::uint8_t *data, std::size_t size, std::uint8_t *out, std::int32_t outSize);
std::int32_t maxCompressedSize(std::int32_t size, std::int32_t type);
std::int32_t compress(std::int32_t type, const std::uint8_t *data, std::int32_t size, std::uint8_t *out, std::int32_t outSize);
std::int32_t preferredType();
std::int32_t minSaveType();
std::int32_t maxSaveType();
std::int32_t maxUncompressedSize();

} // namespace MapModel::Codec
