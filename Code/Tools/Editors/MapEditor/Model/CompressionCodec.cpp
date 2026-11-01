/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// CompressionCodec.cpp -- the game's CompressionManager behind plain types.

#include "Model/CompressionCodec.h"

// Last include: see CompressionCodec.h.
#include "Compression.h"

namespace MapModel::Codec
{

namespace
{
// The manager takes non-const pointers but does not write through the source.
void *source(const std::uint8_t *data)
{
	return const_cast<std::uint8_t *>(data);
}
}

bool isCompressed(const std::uint8_t *data, std::size_t size)
{
	return CompressionManager::isDataCompressed(data, static_cast<Int>(size));
}

std::int32_t typeOf(const std::uint8_t *data, std::size_t size)
{
	return CompressionManager::getCompressionType(data, static_cast<Int>(size));
}

std::int32_t uncompressedSize(const std::uint8_t *data, std::size_t size)
{
	return CompressionManager::getUncompressedSize(data, static_cast<Int>(size));
}

std::int32_t decompress(const std::uint8_t *data, std::size_t size, std::uint8_t *out, std::int32_t outSize)
{
	return CompressionManager::decompressData(source(data), static_cast<Int>(size), out, outSize);
}

std::int32_t maxCompressedSize(std::int32_t size, std::int32_t type)
{
	return CompressionManager::getMaxCompressedSize(size, static_cast<CompressionType>(type));
}

std::int32_t compress(std::int32_t type, const std::uint8_t *data, std::int32_t size, std::uint8_t *out, std::int32_t outSize)
{
	return CompressionManager::compressData(static_cast<CompressionType>(type), source(data), size, out, outSize);
}

std::int32_t preferredType()
{
	return CompressionManager::getPreferredCompression();
}

std::int32_t minSaveType()
{
	return COMPRESSION_MIN;
}

std::int32_t maxSaveType()
{
	return COMPRESSION_MAX;
}

std::int32_t maxUncompressedSize()
{
	return CompressionManager::MAX_UNCOMPRESSED_SIZE;
}

} // namespace MapModel::Codec
