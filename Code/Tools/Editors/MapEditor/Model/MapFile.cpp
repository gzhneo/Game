/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapFile.cpp -- .map files on disk: compression and the chunk data.

#include "Model/MapFile.h"

#include "Model/CompressionCodec.h"

#include <fstream>
#include <limits>
#include <system_error>

namespace MapModel
{

Unpacked unpack(std::span<const std::uint8_t> file)
{
	Unpacked result;
	if (file.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
	{
		result.error = "file too large";
		return result;
	}
	if (!Codec::isCompressed(file.data(), file.size()))
	{
		result.data.assign(file.begin(), file.end());
		return result;
	}
	result.compression = Codec::typeOf(file.data(), file.size());
	const std::int32_t size = Codec::uncompressedSize(file.data(), file.size());
	// For a corrupt header the manager returns the packed size; decoding then fails below.
	if (size <= 0)
	{
		result.error = "bad compressed size in header";
		return result;
	}
	result.data.resize(static_cast<std::size_t>(size));
	const std::int32_t got = Codec::decompress(file.data(), file.size(), result.data.data(), size);
	if (got != size)
	{
		// The game then uses the data as it is (CachedFileInputStream::open); for an editor a
		// clear error is better than a half-read map.
		result.error = "decompression failed";
		result.data.clear();
	}
	return result;
}

bool pack(std::span<const std::uint8_t> data, std::int32_t compression, std::vector<std::uint8_t> &out, std::string &error)
{
	if (compression == static_cast<std::int32_t>(Compression::None))
	{
		out.assign(data.begin(), data.end());
		return true;
	}
	if (data.size() > static_cast<std::size_t>(Codec::maxUncompressedSize()))
	{
		error = "map data too large to compress";
		return false;
	}
	const std::int32_t srcLen = static_cast<std::int32_t>(data.size());
	const std::int32_t maxLen = Codec::maxCompressedSize(srcLen, compression);
	if (maxLen <= 0)
	{
		out.assign(data.begin(), data.end());
		return true;
	}
	std::vector<std::uint8_t> buffer(static_cast<std::size_t>(maxLen));
	const std::int32_t len = Codec::compress(compression, data.data(), srcLen, buffer.data(), maxLen);
	if (len <= 0)
	{
		// Same fallback as the old WorldBuilder: store uncompressed.
		out.assign(data.begin(), data.end());
		return true;
	}
	buffer.resize(static_cast<std::size_t>(len));
	out = std::move(buffer);
	return true;
}

std::int32_t compressionForSave(const Map &map)
{
	if (map.worldInfo)
	{
		if (const auto value = map.worldInfo->getInt("compression"))
			if (*value >= Codec::minSaveType() && *value <= Codec::maxSaveType())
				return *value;
	}
	return Codec::preferredType();
}

bool readFile(const std::filesystem::path &path, std::vector<std::uint8_t> &out, std::string &error)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
	{
		error = "cannot open " + path.string();
		return false;
	}
	in.seekg(0, std::ios::end);
	const std::streamoff size = in.tellg();
	in.seekg(0, std::ios::beg);
	if (size < 0)
	{
		error = "cannot read " + path.string();
		return false;
	}
	out.resize(static_cast<std::size_t>(size));
	if (size > 0 && !in.read(reinterpret_cast<char *>(out.data()), size))
	{
		error = "cannot read " + path.string();
		return false;
	}
	return true;
}

bool writeFile(const std::filesystem::path &path, std::span<const std::uint8_t> data, std::string &error)
{
	std::filesystem::path temp = path;
	temp += ".tmp";
	{
		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		if (!out || !out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size())))
		{
			error = "cannot write " + temp.string();
			return false;
		}
	}
	std::error_code ec;
	std::filesystem::rename(temp, path, ec);
	if (ec)
	{
		error = "cannot replace " + path.string() + ": " + ec.message();
		std::filesystem::remove(temp, ec);
		return false;
	}
	return true;
}

LoadedMap loadMap(const std::filesystem::path &path)
{
	LoadedMap loaded;
	if (!readFile(path, loaded.fileBytes, loaded.result.error))
		return loaded;
	Unpacked unpacked = unpack(loaded.fileBytes);
	if (!unpacked.ok())
	{
		loaded.result.error = unpacked.error;
		return loaded;
	}
	loaded.compression = unpacked.compression;
	loaded.chunkBytes = std::move(unpacked.data);
	loaded.result = readMap(loaded.chunkBytes);
	return loaded;
}

bool saveMap(const std::filesystem::path &path, const Map &map, std::string &error)
{
	WriteResult written = writeMap(map);
	if (!written.ok())
	{
		error = written.error;
		return false;
	}
	std::vector<std::uint8_t> packed;
	if (!pack(written.bytes, compressionForSave(map), packed, error))
		return false;
	return writeFile(path, packed, error);
}

} // namespace MapModel
