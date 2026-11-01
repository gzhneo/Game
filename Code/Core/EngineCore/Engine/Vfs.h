/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Vfs.h -- core of the virtual file system: normalized paths, sources (.big and .zip archives,
// folders), the mount plan (which source outranks which) and the index built from it. Needs no engine
// header, so EngineTests test it directly.
//
// The mount plan states the precedence of the archive system (base archives, texture package, mods)
// as an ordered list, highest priority first. The index walks that list once: the
// first source that holds a path wins. After build() the index is not changed any more, so any
// number of threads may look up and read at the same time.

#pragma once

#ifndef ENGINE_VFS_H
#define ENGINE_VFS_H

#include "Engine/BigArchive.h"
#include "Engine/ZipArchive.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Engine
{
namespace Vfs
{

// ---------------------------------------------------------------------------------------------
// Paths

/// Normalized key of a path: ASCII letters in lower case (the engine lowers only ASCII, like
/// AsciiString::toLower), '/' as separator, empty and "." components dropped. Fails for "..",
/// a drive letter or an empty result.
bool normalizePath(std::string_view path, std::string &key);

/// Order of the engine's file name lists (FilenameList): case-insensitive for ASCII, bytes
/// unsigned, and '/' compared as '\\' because the lists always held backslashes. The order of the
/// INI files loaded from a directory depends on it: '\\' sorts after the digits, '/' before them.
int compareLegacyOrder(std::string_view a, std::string_view b);

struct LegacyOrderLess
{
	bool operator()(std::string_view a, std::string_view b) const { return compareLegacyOrder(a, b) < 0; }
};

/// '*' matches any run of characters, '?' one character; case-insensitive for ASCII.
bool wildcardMatch(std::string_view name, std::string_view pattern);

/// A texture package archive may only hold .dds and .tga files in the original texture folders
/// (art/textures, art/terrain, data/<language>/art/textures) and its manifest at the root
/// (texturpaket.txt). key must be normalized.
bool isTexturePackagePath(std::string_view key);

// ---------------------------------------------------------------------------------------------
// Sources

struct Entry
{
	std::string key;			///< normalized path
	std::string stored;			///< path as stored in the source (folder: relative path on disk)
	std::uint64_t offset = 0;	///< start in the archive (0 for a folder)
	std::uint64_t size = 0;
};

/// Reads up to size bytes at offset and returns how many were read. Must be callable from
/// several threads at once (positional reads, no shared file pointer).
using ReadAt = std::function<std::size_t(std::uint64_t offset, void *dest, std::size_t size)>;

class Source
{
public:
	virtual ~Source() = default;

	/// Name as the old archive system knew the archive (getArchiveFilenameForFile).
	const std::string &name() const { return m_name; }
	const std::vector<Entry> &entries() const { return m_entries; }
	/// Paths the source holds that do not normalize (".." or a drive); they are not served.
	std::size_t skippedEntries() const { return m_skipped; }

	/// Looks up a path (normalized first). nullptr if the source does not hold it.
	const Entry *find(std::string_view path) const;

	/// Reads from the entry, offset counted from its start. Thread-safe.
	virtual std::size_t read(const Entry &entry, std::uint64_t offset, void *dest, std::size_t size) const = 0;

protected:
	explicit Source(std::string name) : m_name(std::move(name)) {}

	/// Adds an entry; a later entry with the same key replaces the earlier one, as in the old
	/// per-archive directory tree. Returns its index in m_entries, or SIZE_MAX if the path does
	/// not normalize.
	std::size_t addEntry(const std::string &stored, std::uint64_t offset, std::uint64_t size);

	std::string m_name;
	std::vector<Entry> m_entries;
	std::unordered_map<std::string, std::size_t> m_positions;	///< key -> index in m_entries
	std::size_t m_skipped = 0;
};

class BigSource final : public Source
{
public:
	/// Reads the directory. Returns nullptr on a damaged archive and sets *error.
	static std::unique_ptr<BigSource> open(std::string name, ReadAt readAt, std::uint64_t archiveSize,
		BigArchiveError *error = nullptr);

	std::size_t read(const Entry &entry, std::uint64_t offset, void *dest, std::size_t size) const override;

private:
	BigSource(std::string name, ReadAt readAt) : Source(std::move(name)), m_readAt(std::move(readAt)) {}

	ReadAt m_readAt;
};

/// A .zip archive. Stored entries are read in place; a deflated entry is unpacked whole on every
/// read, so callers read it in one piece (readEntry).
class ZipSource final : public Source
{
public:
	/// Reads the directory. Returns nullptr on a damaged archive and sets *error. Entries the
	/// reader cannot serve are left out and listed in rejected().
	static std::unique_ptr<ZipSource> open(std::string name, ReadAt readAt, std::uint64_t archiveSize,
		ZipArchiveError *error = nullptr);

	std::size_t read(const Entry &entry, std::uint64_t offset, void *dest, std::size_t size) const override;

	/// The whole entry, unpacked, with size and CRC checked.
	ZipArchiveError readEntry(const Entry &entry, std::vector<unsigned char> &data) const;

	const std::vector<ZipArchiveSkipped> &rejected() const { return m_rejected; }

private:
	ZipSource(std::string name, ReadAt readAt, std::uint64_t archiveSize)
		: Source(std::move(name)), m_readAt(std::move(readAt)), m_archiveSize(archiveSize) {}

	const ZipArchiveEntry *zipEntry(const Entry &entry) const;

	ReadAt m_readAt;
	std::uint64_t m_archiveSize = 0;
	std::vector<ZipArchiveEntry> m_zip;		///< parallel to m_entries
	std::vector<ZipArchiveSkipped> m_rejected;
};

/// Loose files of a folder. The caller gathers the list (a directory walk, or a list in tests).
class FolderSource final : public Source
{
public:
	struct File
	{
		std::string relativePath;
		std::uint64_t size = 0;
	};
	using ReadFile = std::function<std::size_t(const std::string &relativePath, std::uint64_t offset,
		void *dest, std::size_t size)>;

	FolderSource(std::string name, const std::vector<File> &files, ReadFile readFile);

	std::size_t read(const Entry &entry, std::uint64_t offset, void *dest, std::size_t size) const override;

private:
	ReadFile m_readFile;
};

// ---------------------------------------------------------------------------------------------
// Mount plan

enum class Rank
{
	ModPackage,		///< texture package of a mod (<mod>/Texturstufen/Paket)
	ModLoose,		///< loose files in a mod folder
	ModArchives,	///< archives in a mod folder
	ModArchive,		///< a mod given as a single archive
	GamePackage,	///< texture package of the game (Texturstufen/Paket)
	BaseArchives,	///< archives of the game folder
	BaseGame		///< Zero Hour: Generals archives from -baseGame
};

const char *rankName(Rank rank);

struct ModInput
{
	std::string folder;							///< mod folder; empty for a single archive
	std::string archive;						///< single archive; empty for a folder
	std::vector<std::string> folderArchives;	///< archives found in the folder, without its Texturstufen folder
	std::vector<std::string> packageArchives;	///< archives of the mod's texture package
	bool hasLooseFiles = false;
};

struct MountInputs
{
	std::vector<std::string> baseArchives;		///< recursive scan of the game folder, without the Texturstufen folder
	std::vector<std::string> baseGameArchives;	///< Zero Hour: archives of -baseGame
	std::vector<std::string> packageArchives;	///< archives of the game's texture package
	int tier = 0;								///< chosen texture tier (0 SD, 1 HD, 2 UHD); SD leaves the packages closed
	std::vector<ModInput> mods;					///< in command line order; the last one ranks highest
	bool exactOrder = false;					///< mods from the active list: no folder-above-archive rule
};

struct Mount
{
	std::string path;			///< archive or folder, as given in the inputs
	bool folder = false;
	Rank rank = Rank::BaseArchives;
	bool texturesOnly = false;	///< package archive: only textures and the manifest allowed
	int mod = -1;				///< index into MountInputs::mods
};

/// Indices into mods, highest priority first: the last one on the command line ranks highest. The
/// old engine kept one folder and one archive and always put the folder above the archive; two mods
/// of that shape keep that order, unless exactOrder (a list the player ordered in the menu).
std::vector<int> modPriority(const std::vector<ModInput> &mods, bool exactOrder = false);

/// Mounts in priority order, highest first. Reproduces the load order: base archives in name order
/// with the first one winning, then -baseGame below them; above them the texture package (HD and UHD
/// only) and the mods, each loaded with overwrite, so there the later archive wins.
std::vector<Mount> buildMountPlan(const MountInputs &inputs);

// ---------------------------------------------------------------------------------------------
// Index

struct Layer
{
	const Source *source = nullptr;
	bool texturesOnly = false;
	std::string name;				///< for the caller: the archive or folder as mounted
	const void *owner = nullptr;	///< for the caller: what holds the source
	int flags = 0;					///< for the caller
};

struct Hit
{
	const Source *source = nullptr;
	const Entry *entry = nullptr;
	std::size_t layer = 0;		///< index into the layers given to build()
};

class Index
{
public:
	/// Layers in priority order, highest first. A texturesOnly layer holding anything else is not
	/// used at all; its name is added to *rejected.
	void build(const std::vector<Layer> &layers, std::vector<std::string> *rejected = nullptr);

	/// Looks up a path (normalized first). nullptr if no layer holds it.
	const Hit *find(std::string_view path) const;

	std::size_t size() const { return m_files.size(); }

	/// Keys below directory ("" for all) whose file name matches pattern, in the legacy order.
	/// Without recursive only files directly in the directory.
	std::vector<std::string> list(std::string_view directory, std::string_view pattern, bool recursive) const;

	/// Calls f(key, hit) for every path in the index, in no particular order.
	template <typename F> void forEach(F &&f) const
	{
		for (const auto &file : m_files)
			f(file.first, file.second);
	}

private:
	std::unordered_map<std::string, Hit> m_files;
	std::vector<std::string> m_keys;	///< all keys, sorted by plain byte order for prefix search
};

// ---------------------------------------------------------------------------------------------
// Live index: layers that may change while other threads look up files

/// The layers and the index built from them. Never changed after it is built; a hit points into
/// sources the caller keeps alive for as long as any snapshot may exist.
struct Snapshot
{
	std::vector<Layer> layers;	///< highest first; Hit::layer counts in this list
	Index index;
	std::vector<std::string> rejected;
};

/// Mounting builds a new snapshot instead of changing the one in use, so a lookup holds on to
/// the snapshot it got and never sees an index half rebuilt. The next current() after a change
/// builds the new snapshot once.
class LiveIndex
{
public:
	void addTop(Layer layer);
	void addBottom(Layer layer);
	/// Removes the layers with this owner; returns how many.
	std::size_t removeOwner(const void *owner);
	std::size_t layerCount() const;

	/// The current snapshot (never null). Thread-safe.
	std::shared_ptr<const Snapshot> current() const;

private:
	mutable std::mutex m_lock;
	std::vector<Layer> m_layers;
	mutable std::shared_ptr<const Snapshot> m_snapshot;
	mutable bool m_stale = true;
};

// ---------------------------------------------------------------------------------------------
// Read-ahead plan

struct PrefetchRange
{
	std::uint64_t offset = 0;
	std::uint64_t size = 0;
};

struct PrefetchItem
{
	std::size_t layer = 0;						///< index into Snapshot::layers
	std::vector<const Entry *> files;			///< the entries the index serves from this layer
	std::vector<PrefetchRange> ranges;			///< .big: their byte ranges, sorted and merged; empty: read the whole source
	std::uint64_t bytes = 0;					///< what reading the item costs
};

/// What to read ahead so a first map load finds its files in the cache: every layer that serves at
/// least one file, in priority order, without sound (archive named audio*, music*, speech* or
/// serving only .wav and .mp3). From a .big only the served entries are read, gaps up to
/// mergeGap bytes are read through; a .zip is read whole, a folder file by file.
std::vector<PrefetchItem> prefetchPlan(const Snapshot &snapshot, std::uint64_t mergeGap = 256 * 1024);

}	// namespace Vfs
}	// namespace Engine

#endif	// ENGINE_VFS_H
