/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// TerrainTileImage.cpp -- TGA and DDS terrain tile files to one BGRA picture, see header.
//
// DirectXTex reads the DDS and decompresses BC1/BC4/BC7; it needs <windows.h> first.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <DirectXTex.h>

#include "W3DDevice/GameClient/TerrainTileImage.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>

namespace TerrainTileImage
{
namespace
{
	// Larger pictures are rejected before any memory is reserved; the largest tile file holds 10 x 10 tiles
	// of 256 pixels.
	constexpr int MAX_IMAGE_EXTENT = 4096;

	int readShort(const unsigned char *p)
	{
		return static_cast<std::int16_t>(static_cast<std::uint16_t>(p[0] | (p[1] << 8)));
	}

	bool decodeTga(const unsigned char *data, std::size_t size, Image &image, const char **error)
	{
		constexpr std::size_t HEADER_BYTES = 18;
		if (size < HEADER_BYTES) {
			*error = "TGA header incomplete";
			return false;
		}
		const std::size_t idLength = data[0];
		const int imageType = data[2];
		const int width = readShort(data + 12);
		const int height = readShort(data + 14);
		const int bytesPerPixel = (data[16] + 7) / 8;
		if (data[1] != 0 || (imageType != 2 && imageType != 10)) {
			*error = "TGA is not true colour (type 2 or 10)";
			return false;
		}
		if (bytesPerPixel < 3 || bytesPerPixel > 4) {
			*error = "TGA is not 24 or 32 bit";
			return false;
		}
		if (width <= 0 || height <= 0 || width > MAX_IMAGE_EXTENT || height > MAX_IMAGE_EXTENT) {
			*error = "TGA size out of range";
			return false;
		}

		// Texture quality tiers write "ZHTS<s>" into the ID field for tiles of 64*s pixels.
		int scale = 1;
		std::size_t pos = HEADER_BYTES;
		if (idLength > 0) {
			if (size - pos >= idLength) {
				if (idLength >= 5 && std::memcmp(data + pos, "ZHTS", 4) == 0) {
					if (data[pos + 4] == '2') scale = 2;
					if (data[pos + 4] == '4') scale = 4;
				}
				pos += idLength;
			} else {
				pos = size;
			}
		}

		image.info.format = Format::TGA;
		image.info.width = width;
		image.info.height = height;
		image.info.tileExtent = BASE_TILE_EXTENT * scale;
		image.bgra.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);

		// A truncated file keeps the bytes of the last pixel, like the stream reader this replaces.
		unsigned char pixel[4] = { 0, 0, 0, 0 };
		auto readPixel = [&]() {
			const std::size_t n = size - pos < static_cast<std::size_t>(bytesPerPixel) ? size - pos : static_cast<std::size_t>(bytesPerPixel);
			std::memcpy(pixel, data + pos, n);
			pos += n;
		};
		const bool compressed = (imageType & 0x08) != 0;
		int repeatCount = 0;
		bool running = false;
		unsigned char *out = image.bgra.data();
		for (int row = 0; row < height; ++row) {
			for (int column = 0; column < width; ++column) {
				if (compressed && repeatCount == 0) {
					const unsigned char flag = pos < size ? data[pos++] : 0;
					repeatCount = (flag & 0x7f) + 1;
					running = (flag & 0x80) != 0;
					if (running) {
						readPixel();
					}
				}
				if (compressed) {
					--repeatCount;
				}
				if (!running) {
					readPixel();
				}
				out[0] = pixel[0];
				out[1] = pixel[1];
				out[2] = pixel[2];
				out[3] = bytesPerPixel == 4 ? pixel[3] : 255;
				out += 4;
			}
		}
		return true;
	}

	// Halves a decoded picture level times with a 2 x 2 box filter (texture package: a TGA scaled for a
	// higher tier, loaded at a lower one). Stops at tiles of 64 pixels or a picture of one pixel.
	void halveTga(Image &image, int level)
	{
		int done = 0;
		while (done < level && image.info.tileExtent / 2 >= BASE_TILE_EXTENT && image.info.width >= 2 && image.info.height >= 2) {
			const int width = image.info.width / 2;
			const int height = image.info.height / 2;
			const std::size_t stride = static_cast<std::size_t>(image.info.width) * 4;
			std::vector<unsigned char> half(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
			for (int y = 0; y < height; ++y) {
				const unsigned char *top = image.bgra.data() + static_cast<std::size_t>(2 * y) * stride;
				const unsigned char *bottom = top + stride;
				unsigned char *out = half.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
				for (int x = 0; x < width; ++x) {
					for (int c = 0; c < 4; ++c) {
						const int sum = top[8 * x + c] + top[8 * x + 4 + c] + bottom[8 * x + c] + bottom[8 * x + 4 + c];
						out[4 * x + c] = static_cast<unsigned char>((sum + 2) / 4);
					}
				}
			}
			image.bgra.swap(half);
			image.info.width = width;
			image.info.height = height;
			image.info.tileExtent /= 2;
			++done;
		}
		image.info.mipLevel = done;
	}

	bool isSupportedDdsFormat(DXGI_FORMAT format)
	{
		switch (format) {
			case DXGI_FORMAT_BC1_UNORM:
			case DXGI_FORMAT_BC1_UNORM_SRGB:
			case DXGI_FORMAT_BC4_UNORM:
			case DXGI_FORMAT_BC7_UNORM:
			case DXGI_FORMAT_BC7_UNORM_SRGB:
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			case DXGI_FORMAT_B8G8R8X8_UNORM:
			case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
				return true;
			default:
				return false;
		}
	}

	bool checkDdsMetadata(const DirectX::TexMetadata &meta, const char **error)
	{
		if (meta.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || meta.arraySize != 1 || meta.depth != 1 || meta.IsCubemap()) {
			*error = "DDS is not a single 2D image";
			return false;
		}
		if (!isSupportedDdsFormat(meta.format)) {
			*error = "DDS format not BC1, BC4, BC7 or 8-bit RGBA";
			return false;
		}
		if (meta.width == 0 || meta.height == 0 || meta.width > MAX_IMAGE_EXTENT || meta.height > MAX_IMAGE_EXTENT) {
			*error = "DDS size out of range";
			return false;
		}
		return true;
	}

	int clampMipLevel(const DirectX::TexMetadata &meta, int mipLevel)
	{
		const int last = meta.mipLevels > 0 ? static_cast<int>(meta.mipLevels) - 1 : 0;
		return mipLevel < 0 ? 0 : (mipLevel > last ? last : mipLevel);
	}

	bool decodeDds(const unsigned char *data, std::size_t size, int tilesPerRow, Image &image, const char **error, int mipLevel)
	{
		DirectX::TexMetadata meta;
		DirectX::ScratchImage loaded;
		if (FAILED(DirectX::LoadFromDDSMemory(data, size, DirectX::DDS_FLAGS_NONE, &meta, loaded))) {
			*error = "DDS unreadable";
			return false;
		}
		if (!checkDdsMetadata(meta, error)) {
			return false;
		}
		const int level = clampMipLevel(meta, mipLevel);
		const DirectX::Image *source = loaded.GetImage(static_cast<std::size_t>(level), 0, 0);
		DirectX::ScratchImage decompressed;
		if (DirectX::IsCompressed(meta.format)) {
			// Same colour space as the source, so the stored values come out unconverted.
			const DXGI_FORMAT target = DirectX::IsSRGB(meta.format) ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM;
			if (source == nullptr || FAILED(DirectX::Decompress(*source, target, decompressed))) {
				*error = "DDS decompression failed";
				return false;
			}
			source = decompressed.GetImage(0, 0, 0);
		}
		if (source == nullptr || source->pixels == nullptr) {
			*error = "DDS without pixels";
			return false;
		}

		const int width = static_cast<int>(source->width);
		const int height = static_cast<int>(source->height);
		const bool rgbaOrder = source->format == DXGI_FORMAT_R8G8B8A8_UNORM || source->format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		const bool opaque = source->format == DXGI_FORMAT_B8G8R8X8_UNORM || source->format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;

		image.info.format = Format::DDS;
		image.info.width = width;
		image.info.height = height;
		image.info.tileExtent = ddsTileExtent(width, tilesPerRow);
		image.info.mipLevels = static_cast<int>(meta.mipLevels);
		image.info.mipLevel = level;
		image.bgra.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
		for (int y = 0; y < height; ++y) {
			// DDS rows run top-down, tile rows bottom-up.
			const unsigned char *in = source->pixels + static_cast<std::size_t>(y) * source->rowPitch;
			unsigned char *out = image.bgra.data() + static_cast<std::size_t>(height - 1 - y) * static_cast<std::size_t>(width) * 4;
			for (int x = 0; x < width; ++x, in += 4, out += 4) {
				out[0] = rgbaOrder ? in[2] : in[0];
				out[1] = in[1];
				out[2] = rgbaOrder ? in[0] : in[2];
				out[3] = opaque ? 255 : in[3];
			}
		}
		return true;
	}
}

bool isDds(const void *data, std::size_t size)
{
	return data != nullptr && size >= 4 && std::memcmp(data, "DDS ", 4) == 0;
}

int ddsTileExtent(int width, int tilesPerRow)
{
	if (tilesPerRow > 0 && width % tilesPerRow == 0) {
		const int extent = width / tilesPerRow;
		if (extent == BASE_TILE_EXTENT || extent == 2 * BASE_TILE_EXTENT || extent == 4 * BASE_TILE_EXTENT) {
			return extent;
		}
	}
	return BASE_TILE_EXTENT;
}

bool readDdsInfo(const void *data, std::size_t size, int tilesPerRow, Info &info, const char **error, int mipLevel)
{
	const char *ignored = nullptr;
	if (error == nullptr) {
		error = &ignored;
	}
	DirectX::TexMetadata meta;
	if (!isDds(data, size) || FAILED(DirectX::GetMetadataFromDDSMemory(static_cast<const std::uint8_t *>(data), size, DirectX::DDS_FLAGS_NONE, meta))) {
		*error = "DDS header unreadable";
		return false;
	}
	if (!checkDdsMetadata(meta, error)) {
		return false;
	}
	const int level = clampMipLevel(meta, mipLevel);
	info.format = Format::DDS;
	info.width = (std::max)(1, static_cast<int>(meta.width) >> level);
	info.height = (std::max)(1, static_cast<int>(meta.height) >> level);
	info.tileExtent = ddsTileExtent(info.width, tilesPerRow);
	info.mipLevels = static_cast<int>(meta.mipLevels);
	info.mipLevel = level;
	return true;
}

bool decode(const void *data, std::size_t size, int tilesPerRow, Image &image, const char **error, int mipLevel)
{
	const char *ignored = nullptr;
	if (error == nullptr) {
		error = &ignored;
	}
	image = Image();
	if (data == nullptr) {
		*error = "no data";
		return false;
	}
	try {
		const unsigned char *bytes = static_cast<const unsigned char *>(data);
		const bool ok = isDds(data, size) ? decodeDds(bytes, size, tilesPerRow, image, error, mipLevel) : decodeTga(bytes, size, image, error);
		if (ok && image.info.format == Format::TGA && mipLevel > 0) {
			halveTga(image, mipLevel);
		}
		if (!ok) {
			image.bgra.clear();
		}
		return ok;
	} catch (const std::bad_alloc &) {
		*error = "out of memory";
		image = Image();
		return false;
	}
}

}
