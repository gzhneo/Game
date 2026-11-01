/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// TexturePackage.cpp -- see TexturePackage.h.

#include "Engine/TexturePackage.h"
#include "Engine/TextureTier.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace Engine
{

const char *const TEXTURE_PACKAGE_MANIFEST = "texturpaket.txt";

unsigned textureTierScale(int tier)
{
	switch (tier)
	{
		case TEXTURE_TIER_HD:	return 2;
		case TEXTURE_TIER_UHD:	return 4;
		default:				return 1;
	}
}

unsigned textureMipOffset(unsigned fileWidth, unsigned fileHeight, unsigned logicalWidth, unsigned logicalHeight,
	unsigned scale, unsigned mipCount)
{
	if (logicalWidth == 0 || logicalHeight == 0 || scale == 0 || mipCount < 2)
		return 0;
	const unsigned long long targetWidth = static_cast<unsigned long long>(logicalWidth) * scale;
	const unsigned long long targetHeight = static_cast<unsigned long long>(logicalHeight) * scale;
	unsigned offset = 0;
	// both edges of the next level must still reach the target: no edge ends up below it
	while (offset + 1 < mipCount && offset < 31
		&& (fileWidth >> (offset + 1)) >= targetWidth && (fileHeight >> (offset + 1)) >= targetHeight)
	{
		++offset;
	}
	return offset;
}

std::string texturePackageKey(const char *name)
{
	std::string key;
	if (name == nullptr)
		return key;
	const char *start = name;
	for (const char *p = name; *p != 0; ++p)
	{
		if (*p == '\\' || *p == '/' || *p == ':')
			start = p + 1;
	}
	const char *end = std::strrchr(start, '.');
	if (end == nullptr)
		end = start + std::strlen(start);
	key.reserve(static_cast<std::size_t>(end - start));
	for (const char *p = start; p < end; ++p)
		key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
	return key;
}

TexturePackage &TexturePackage::instance()
{
	static TexturePackage package;
	return package;
}

void TexturePackage::clear()
{
	m_entries.reset();
	m_sources.reset();
	m_manifests = 0;
	m_highestTier = -1;
	m_tier = 0;
	m_badLines = 0;
}

namespace
{
	/// Next whitespace-separated token in [pos, end); empty at the end of the line.
	std::string token(const char *&pos, const char *end)
	{
		while (pos < end && (*pos == ' ' || *pos == '\t'))
			++pos;
		const char *start = pos;
		while (pos < end && *pos != ' ' && *pos != '\t')
			++pos;
		return std::string(start, pos);
	}

	/// Last whitespace-separated token in [begin, end); end moves to the character before it.
	std::string lastToken(const char *begin, const char *&end)
	{
		while (end > begin && (end[-1] == ' ' || end[-1] == '\t'))
			--end;
		const char *stop = end;
		while (end > begin && end[-1] != ' ' && end[-1] != '\t')
			--end;
		return std::string(end, stop);
	}

	bool isTextureName(const std::string &path)
	{
		if (path.size() < 5)
			return false;
		std::string ext = path.substr(path.size() - 4);
		for (char &c : ext)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return ext == ".dds" || ext == ".tga";
	}

	bool parseUnsigned(const std::string &text, unsigned &value)
	{
		if (text.empty() || text.size() > 6)
			return false;
		unsigned v = 0;
		for (char c : text)
		{
			if (c < '0' || c > '9')
				return false;
			v = v * 10 + static_cast<unsigned>(c - '0');
		}
		value = v;
		return v > 0 && v <= 65536;
	}
}

namespace
{
	std::string lowerSource(const char *source)
	{
		std::string lower = source != nullptr ? source : "";
		for (char &c : lower)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return lower;
	}
}

int TexturePackage::addManifest(const char *text, std::size_t length, const char *source)
{
	if (text == nullptr)
		return -1;
	std::vector<std::string> keys;
	const char *pos = text;
	const char *const end = text + length;
	bool header = false;
	int added = 0;
	while (pos < end)
	{
		const char *lineEnd = pos;
		while (lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r')
			++lineEnd;
		const char *p = pos;
		const std::string first = token(p, lineEnd);
		if (!header)
		{
			// the first non-empty line is the header
			if (!first.empty())
			{
				const std::string version = token(p, lineEnd);
				const std::string tierName = token(p, lineEnd);
				const int tier = parseTextureTier(tierName.c_str());
				if (first != "TEXTURPAKET" || version != "1" || tier < TEXTURE_TIER_SD)
					return -1;
				header = true;
				if (m_highestTier < 0 || tier < m_highestTier)
					m_highestTier = tier;
			}
		}
		else if (!first.empty() && first[0] != ';')
		{
			// Some EA texture names contain spaces ("gas can.dds"): the size is the last two tokens,
			// everything before them is the path, which has to name a texture.
			const char *pathBegin = pos;
			while (pathBegin < lineEnd && (*pathBegin == ' ' || *pathBegin == '\t'))
				++pathBegin;
			const char *pathEnd = lineEnd;
			const std::string h = lastToken(pathBegin, pathEnd);
			const std::string w = lastToken(pathBegin, pathEnd);
			while (pathEnd > pathBegin && (pathEnd[-1] == ' ' || pathEnd[-1] == '\t'))
				--pathEnd;
			const std::string path(pathBegin, pathEnd);
			unsigned width = 0, height = 0;
			if (!parseUnsigned(w, width) || !parseUnsigned(h, height) || !isTextureName(path))
				++m_badLines;
			else
			{
				const std::string key = texturePackageKey(path.c_str());
				if (key.empty())
					++m_badLines;
				else
				{
					if (!m_entries)
						m_entries = std::make_unique<std::unordered_map<std::string, Size>>();
					if (m_entries->emplace(key, Size{ width, height, path }).second)
						++added;
					keys.push_back(key);
				}
			}
		}
		pos = lineEnd;
		while (pos < end && (*pos == '\n' || *pos == '\r'))
			++pos;
	}
	if (!header)
		return -1;
	++m_manifests;
	if (!m_sources)
		m_sources = std::make_unique<std::unordered_map<std::string, std::unordered_set<std::string>>>();
	std::unordered_set<std::string> &named = (*m_sources)[lowerSource(source)];
	named.insert(keys.begin(), keys.end());
	return added;
}

int TexturePackage::namedBy(const char *source) const
{
	if (!m_sources)
		return -1;
	const auto it = m_sources->find(lowerSource(source));
	return it != m_sources->end() ? static_cast<int>(it->second.size()) : -1;
}

bool TexturePackage::provides(const char *source, const char *name) const
{
	if (!m_sources)
		return false;
	const auto it = m_sources->find(lowerSource(source));
	return it != m_sources->end() && it->second.count(texturePackageKey(name)) > 0;
}

bool TexturePackage::logicalSize(const char *name, unsigned &width, unsigned &height) const
{
	if (!m_entries || m_entries->empty())
		return false;
	const auto it = m_entries->find(texturePackageKey(name));
	if (it == m_entries->end())
		return false;
	width = it->second.width;
	height = it->second.height;
	return true;
}

unsigned TexturePackage::mipOffset(const char *name, unsigned fileWidth, unsigned fileHeight, unsigned mipCount) const
{
	unsigned width = 0, height = 0;
	if (!logicalSize(name, width, height))
		return 0;
	return textureMipOffset(fileWidth, fileHeight, width, height, textureTierScale(m_tier), mipCount);
}

float TexturePackage::scaleFactor(const char *name, unsigned loadedWidth) const
{
	unsigned width = 0, height = 0;
	if (loadedWidth == 0 || !logicalSize(name, width, height))
		return 1.0f;
	return static_cast<float>(loadedWidth) / static_cast<float>(width);
}

std::vector<std::string> TexturePackage::paths() const
{
	std::vector<std::string> result;
	if (!m_entries)
		return result;
	result.reserve(m_entries->size());
	for (const auto &entry : *m_entries)
		result.push_back(entry.second.path);
	return result;
}

bool TexturePackage::forget(const char *name)
{
	return m_entries && m_entries->erase(texturePackageKey(name)) > 0;
}

} // namespace Engine
