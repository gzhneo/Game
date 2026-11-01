/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// TexturePackage.h -- one texture package instead of separate tier archives.
//
// The package holds every texture once, in its highest resolution and with a full mip chain. Its
// manifest names the logical (original) size of each texture. The chosen tier only decides how many
// top mip levels the loader skips: SD loads a texture at its original size, HD at twice and UHD at
// four times its edge length, never larger than the file.
//
// Manifest (text, one archive may carry one as TEXTURE_PACKAGE_MANIFEST at its root):
//   TEXTURPAKET 1 <highest tier SD|HD|UHD>
//   ; comment
//   <archive path of the texture> <original width> <original height>
// Names are looked up without folder and extension, case-insensitive: the models name "abc.tga",
// the package holds "Art\Textures\abc.dds".
//
// Manifests come from the game's package, from a mod's package and from free content folders; each
// is added with its source (archive or folder name), so a texture keeps its entry only while the file
// the game loads comes from a source whose manifest names it.
//
// Filled once at startup (ArchiveFileSystem::loadTextureTiers, loadFreeContent, loadMods), read from
// then on, also from the texture loader; there is no locking.

#pragma once

#ifndef ENGINE_TEXTUREPACKAGE_H
#define ENGINE_TEXTUREPACKAGE_H

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Engine
{

/// File name of the manifest at the root of a package archive.
extern const char *const TEXTURE_PACKAGE_MANIFEST;

/// Edge length factor of a tier relative to the original: SD 1, HD 2, UHD 4.
unsigned textureTierScale(int tier);

/// Mip levels to skip so that a file of fileWidth x fileHeight with mipCount levels is loaded at
/// logical size x scale: the largest level whose edges are both still at least the target. Never
/// skips the last level; 0 if the file is not larger than the target or the size is unknown.
unsigned textureMipOffset(unsigned fileWidth, unsigned fileHeight, unsigned logicalWidth, unsigned logicalHeight,
	unsigned scale, unsigned mipCount);

/// Lookup key of a texture name: without folder and extension, lower case ("Art\Textures\ABC.dds" -> "abc").
std::string texturePackageKey(const char *name);

class TexturePackage
{
public:
	static TexturePackage &instance();

	/// Adds the entries of a manifest read from source (archive or folder name, case-insensitive). An
	/// entry already known keeps its first size (the original size of a texture is the same in every
	/// source). Returns the number of new entries, -1 if the text is not a manifest; malformed lines are
	/// skipped and counted in badLines().
	int addManifest(const char *text, std::size_t length, const char *source = "");

	/// Whether the manifest of source names the texture.
	bool provides(const char *source, const char *name) const;
	/// Textures the manifest of source names; -1 if source added no manifest.
	int namedBy(const char *source) const;

	/// Frees the table itself, not only its entries: the instance outlives the engine's memory
	/// manager, which would report the bucket array of an empty map as a leak.
	void clear();

	/// At least one manifest was read.
	bool active() const { return m_manifests > 0; }
	std::size_t size() const { return m_entries ? m_entries->size() : 0; }
	std::size_t badLines() const { return m_badLines; }

	/// Highest tier the package holds (the lowest any of its manifests names).
	int highestTier() const { return m_highestTier; }

	/// Tier the textures are loaded at; set once the tier is chosen.
	void setTier(int tier) { m_tier = tier; }
	int tier() const { return m_tier; }

	/// Original size of a texture; false if the package does not know it.
	bool logicalSize(const char *name, unsigned &width, unsigned &height) const;

	/// Mip levels the loader skips for this texture at the current tier (textureMipOffset).
	unsigned mipOffset(const char *name, unsigned fileWidth, unsigned fileHeight, unsigned mipCount) const;

	/// Loaded width over original width (2 for an original of 256 loaded at 512); 1 if unknown.
	float scaleFactor(const char *name, unsigned loadedWidth) const;

	/// Path of every entry in its archive, as the manifest names it.
	std::vector<std::string> paths() const;

	/// Drops the entry of a texture whose loaded file does not come from the package (free content or a
	/// mod replaces it); that file is then loaded and scaled as it is. False if there was no entry.
	bool forget(const char *name);

private:
	struct Size
	{
		unsigned width;
		unsigned height;
		std::string path;
	};

	std::unique_ptr<std::unordered_map<std::string, Size>> m_entries;
	/// Lower-case source -> keys its manifest names.
	std::unique_ptr<std::unordered_map<std::string, std::unordered_set<std::string>>> m_sources;
	int m_manifests = 0;
	int m_highestTier = -1;
	int m_tier = 0;
	std::size_t m_badLines = 0;
};

} // namespace Engine

#endif // ENGINE_TEXTUREPACKAGE_H
