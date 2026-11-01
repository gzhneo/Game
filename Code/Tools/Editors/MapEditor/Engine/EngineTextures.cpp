/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EngineTextures.cpp -- the textures of the game data (Art\Textures) by name, and scaled pictures of
// them for the editor's pickers. The files are read through the game's file system, so textures in
// .big archives count as well; DirectXTex decodes TGA and DDS.

#include "Engine/EngineApi.h"
#include "Engine/EngineInternal.h"

#include "Common/FileSystem.h"
#include "Common/file.h"

#include <DirectXTex.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{

constexpr const char *TEXTURE_FOLDER = "Art\\Textures\\";

std::vector<std::string> s_textures;
bool s_texturesRead = false;

std::string lower(std::string s)
{
	for (char &c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

// The name a particle system or a model uses: the file name with .tga, which the renderer also
// resolves to a .dds of the same name.
std::string textureName(const std::string &path)
{
	std::string name = path;
	const size_t slash = name.find_last_of("\\/");
	if (slash != std::string::npos)
		name.erase(0, slash + 1);
	const size_t dot = name.find_last_of('.');
	if (dot != std::string::npos)
		name.erase(dot);
	return name + ".tga";
}

const std::vector<std::string> &textures()
{
	if (!s_texturesRead && TheFileSystem)
	{
		s_texturesRead = true;
		std::vector<std::string> found;
		for (const char *pattern : {"*.tga", "*.dds"})
		{
			FilenameList files;
			TheFileSystem->getFileListInDirectory(AsciiString(TEXTURE_FOLDER), AsciiString(pattern), files, FALSE);
			for (const AsciiString &f : files)
				found.push_back(textureName(f.str()));
		}
		std::sort(found.begin(), found.end(), [](const std::string &a, const std::string &b) {
			const std::string la = lower(a);
			const std::string lb = lower(b);
			return la != lb ? la < lb : a < b;
		});
		found.erase(std::unique(found.begin(), found.end(), [](const std::string &a, const std::string &b) { return lower(a) == lower(b); }), found.end());
		s_textures = std::move(found);
	}
	return s_textures;
}

bool readFile(const std::string &path, std::vector<uint8_t> &bytes)
{
	if (!TheFileSystem || !TheFileSystem->doesFileExist(path.c_str()))
		return false;
	File *file = TheFileSystem->openFile(path.c_str(), File::READ | File::BINARY);
	if (!file)
		return false;
	const Int size = file->size();
	bytes.resize(size > 0 ? static_cast<size_t>(size) : 0);
	const bool ok = size > 0 && file->read(bytes.data(), size) == size;
	file->close();
	return ok;
}

// The texture as 8-bit RGBA, from the .dds of that name first (as the renderer prefers it), else the .tga.
bool loadTexture(const std::string &name, DirectX::ScratchImage &rgba)
{
	std::string base = name;
	const size_t dot = base.find_last_of('.');
	if (dot != std::string::npos)
		base.erase(dot);
	std::vector<uint8_t> bytes;
	DirectX::ScratchImage loaded;
	HRESULT hr = E_FAIL;
	if (readFile(TEXTURE_FOLDER + base + ".dds", bytes))
		hr = DirectX::LoadFromDDSMemory(bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, nullptr, loaded);
	if (FAILED(hr) && readFile(TEXTURE_FOLDER + base + ".tga", bytes))
		hr = DirectX::LoadFromTGAMemory(bytes.data(), bytes.size(), DirectX::TGA_FLAGS_NONE, nullptr, loaded);
	if (FAILED(hr) || !loaded.GetImage(0, 0, 0))
		return false;
	const DirectX::Image &image = *loaded.GetImage(0, 0, 0);
	if (DirectX::IsCompressed(image.format))
		hr = DirectX::Decompress(image, DXGI_FORMAT_R8G8B8A8_UNORM, rgba);
	else if (image.format != DXGI_FORMAT_R8G8B8A8_UNORM)
		hr = DirectX::Convert(image, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, rgba);
	else
		hr = rgba.InitializeFromImage(image);
	return SUCCEEDED(hr) && rgba.GetImage(0, 0, 0);
}

} // namespace

MEAPI int32_t meTextureCount(void)
{
	if (!MapEditorEngine::engineStarted())
		return 0;
	return static_cast<int32_t>(textures().size());
}

MEAPI int32_t meTextureName(int32_t index, char *name, int32_t size)
{
	const std::vector<std::string> &list = textures();
	if (index < 0 || index >= static_cast<int32_t>(list.size()) || !name || size <= 0)
		return 0;
	std::snprintf(name, static_cast<size_t>(size), "%s", list[static_cast<size_t>(index)].c_str());
	return 1;
}

MEAPI int32_t meTextureThumbnail(const char *name, int32_t size, uint8_t *rgba)
{
	if (!MapEditorEngine::engineStarted() || !name || !name[0] || size <= 0 || size > 1024 || !rgba)
		return 0;
	DirectX::ScratchImage full;
	if (!loadTexture(name, full))
		return 0;
	const DirectX::Image &image = *full.GetImage(0, 0, 0);
	// Fitted into the square, the rest transparent. (std::max) because windows.h defines max.
	const double scale = static_cast<double>(size) / static_cast<double>((std::max)(image.width, image.height));
	const size_t w = (std::max<size_t>)(1, static_cast<size_t>(image.width * scale + 0.5));
	const size_t h = (std::max<size_t>)(1, static_cast<size_t>(image.height * scale + 0.5));
	DirectX::ScratchImage scaled;
	if (FAILED(DirectX::Resize(image, w, h, DirectX::TEX_FILTER_BOX, scaled)) || !scaled.GetImage(0, 0, 0))
		return 0;
	const DirectX::Image &fitted = *scaled.GetImage(0, 0, 0);
	std::memset(rgba, 0, static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
	const size_t x0 = (static_cast<size_t>(size) - fitted.width) / 2;
	const size_t y0 = (static_cast<size_t>(size) - fitted.height) / 2;
	for (size_t y = 0; y < fitted.height; ++y)
		std::memcpy(rgba + ((y0 + y) * static_cast<size_t>(size) + x0) * 4, fitted.pixels + y * fitted.rowPitch, fitted.width * 4);
	return 1;
}

bool MapEditorEngine::textureCrop(const std::string &file, float u0, float v0, float u1, float v1, int size, uint8_t *rgba)
{
	if (size <= 0 || size > 1024 || !rgba)
		return false;
	// Interface pictures share a few textures: the last ones read stay decoded.
	static std::vector<std::pair<std::string, std::shared_ptr<DirectX::ScratchImage>>> cache;
	const std::string key = lower(file);
	std::shared_ptr<DirectX::ScratchImage> full;
	for (const auto &entry : cache)
		if (entry.first == key)
			full = entry.second;
	if (!full)
	{
		full = std::make_shared<DirectX::ScratchImage>();
		if (!loadTexture(file, *full))
			return false;
		cache.emplace_back(key, full);
		if (cache.size() > 8)
			cache.erase(cache.begin());
	}
	const DirectX::Image &image = *full->GetImage(0, 0, 0);
	const size_t x0 = static_cast<size_t>(std::clamp(u0, 0.0f, 1.0f) * static_cast<float>(image.width));
	const size_t y0 = static_cast<size_t>(std::clamp(v0, 0.0f, 1.0f) * static_cast<float>(image.height));
	const size_t x1 = static_cast<size_t>(std::clamp(u1, 0.0f, 1.0f) * static_cast<float>(image.width));
	const size_t y1 = static_cast<size_t>(std::clamp(v1, 0.0f, 1.0f) * static_cast<float>(image.height));
	if (x1 <= x0 || y1 <= y0)
		return false;
	DirectX::ScratchImage part;
	if (FAILED(part.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, x1 - x0, y1 - y0, 1, 1)))
		return false;
	const DirectX::Rect rect(x0, y0, x1 - x0, y1 - y0);
	if (FAILED(DirectX::CopyRectangle(image, rect, *part.GetImage(0, 0, 0), DirectX::TEX_FILTER_DEFAULT, 0, 0)))
		return false;
	const DirectX::Image &cut = *part.GetImage(0, 0, 0);
	const double scale = static_cast<double>(size) / static_cast<double>((std::max)(cut.width, cut.height));
	const size_t w = (std::max<size_t>)(1, static_cast<size_t>(static_cast<double>(cut.width) * scale + 0.5));
	const size_t h = (std::max<size_t>)(1, static_cast<size_t>(static_cast<double>(cut.height) * scale + 0.5));
	DirectX::ScratchImage scaled;
	if (FAILED(DirectX::Resize(cut, w, h, DirectX::TEX_FILTER_BOX, scaled)) || !scaled.GetImage(0, 0, 0))
		return false;
	const DirectX::Image &fitted = *scaled.GetImage(0, 0, 0);
	std::memset(rgba, 0, static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
	const size_t ox = (static_cast<size_t>(size) - fitted.width) / 2;
	const size_t oy = (static_cast<size_t>(size) - fitted.height) / 2;
	for (size_t y = 0; y < fitted.height; ++y)
		std::memcpy(rgba + ((oy + y) * static_cast<size_t>(size) + ox) * 4, fitted.pixels + y * fitted.rowPitch, fitted.width * 4);
	return true;
}
