/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Vfs.cpp -- virtual file system core, see Vfs.h.

#include "Engine/Vfs.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>

namespace Engine
{
namespace Vfs
{

namespace
{

unsigned char lowerAscii(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c - 'A' + 'a') : c;
}

unsigned char legacyOrderByte(char c)
{
	return c == '/' ? static_cast<unsigned char>('\\') : lowerAscii(static_cast<unsigned char>(c));
}

bool endsWith(std::string_view text, std::string_view tail)
{
	return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
}

bool startsWith(std::string_view text, std::string_view head)
{
	return text.size() >= head.size() && text.compare(0, head.size(), head) == 0;
}

/// True if the path names the root: only separators and "." components.
bool isRootPath(std::string_view path)
{
	std::size_t pos = 0;
	while (pos <= path.size())
	{
		std::size_t end = path.find_first_of("/\\", pos);
		if (end == std::string_view::npos)
			end = path.size();
		const std::string_view part = path.substr(pos, end - pos);
		if (!part.empty() && part != ".")
			return false;
		pos = end + 1;
	}
	return true;
}

/// Name order as the old sets had it; equal names (ignoring case) are kept once.
std::vector<std::string> legacySorted(std::vector<std::string> names, bool highestFirst)
{
	std::stable_sort(names.begin(), names.end(), LegacyOrderLess());
	names.erase(std::unique(names.begin(), names.end(),
		[](const std::string &a, const std::string &b) { return compareLegacyOrder(a, b) == 0; }), names.end());
	if (highestFirst)
		std::reverse(names.begin(), names.end());
	return names;
}

}	// namespace

// ---------------------------------------------------------------------------------------------
// Paths

bool normalizePath(std::string_view path, std::string &key)
{
	key.clear();
	std::size_t pos = 0;
	while (pos <= path.size())
	{
		std::size_t end = path.find_first_of("/\\", pos);
		if (end == std::string_view::npos)
			end = path.size();
		const std::string_view part = path.substr(pos, end - pos);
		pos = end + 1;

		if (part.empty() || part == ".")
			continue;
		if (part == ".." || part.find(':') != std::string_view::npos)
		{
			key.clear();
			return false;
		}
		if (!key.empty())
			key += '/';
		for (char c : part)
			key += static_cast<char>(lowerAscii(static_cast<unsigned char>(c)));
	}
	return !key.empty();
}

int compareLegacyOrder(std::string_view a, std::string_view b)
{
	const std::size_t n = std::min(a.size(), b.size());
	for (std::size_t i = 0; i < n; ++i)
	{
		const unsigned char ca = legacyOrderByte(a[i]);
		const unsigned char cb = legacyOrderByte(b[i]);
		if (ca != cb)
			return ca < cb ? -1 : 1;
	}
	if (a.size() == b.size())
		return 0;
	return a.size() < b.size() ? -1 : 1;
}

bool wildcardMatch(std::string_view name, std::string_view pattern)
{
	// Iterative with one backtrack point for the last '*'.
	std::size_t n = 0, p = 0;
	std::size_t starP = std::string_view::npos, starN = 0;
	while (n < name.size())
	{
		if (p < pattern.size() && (pattern[p] == '?'
			|| lowerAscii(static_cast<unsigned char>(pattern[p])) == lowerAscii(static_cast<unsigned char>(name[n]))))
		{
			++n;
			++p;
		}
		else if (p < pattern.size() && pattern[p] == '*')
		{
			starP = p++;
			starN = n;
		}
		else if (starP != std::string_view::npos)
		{
			p = starP + 1;
			n = ++starN;
		}
		else
			return false;
	}
	while (p < pattern.size() && pattern[p] == '*')
		++p;
	return p == pattern.size();
}

bool isTexturePackagePath(std::string_view key)
{
	if (key == "texturpaket.txt")
		return true;
	if (!endsWith(key, ".dds") && !endsWith(key, ".tga"))
		return false;
	if (startsWith(key, "art/textures/") || startsWith(key, "art/terrain/"))
		return true;
	return startsWith(key, "data/") && key.find("/art/textures/") != std::string_view::npos;
}

// ---------------------------------------------------------------------------------------------
// Sources

std::size_t Source::addEntry(const std::string &stored, std::uint64_t offset, std::uint64_t size)
{
	Entry entry;
	if (!normalizePath(stored, entry.key))
	{
		++m_skipped;
		return SIZE_MAX;
	}
	entry.stored = stored;
	entry.offset = offset;
	entry.size = size;

	const auto found = m_positions.find(entry.key);
	if (found != m_positions.end())
	{
		m_entries[found->second] = std::move(entry);
		return found->second;
	}
	const std::size_t position = m_entries.size();
	m_positions.emplace(entry.key, position);
	m_entries.push_back(std::move(entry));
	return position;
}

const Entry *Source::find(std::string_view path) const
{
	std::string key;
	if (!normalizePath(path, key))
		return nullptr;
	const auto found = m_positions.find(key);
	return found == m_positions.end() ? nullptr : &m_entries[found->second];
}

std::unique_ptr<BigSource> BigSource::open(std::string name, ReadAt readAt, std::uint64_t archiveSize,
	BigArchiveError *error)
{
	std::uint64_t position = 0;
	std::vector<BigArchiveEntry> directory;
	const BigArchiveError result = readBigArchiveDirectory(
		[&readAt, &position](void *dest, std::size_t size) -> std::size_t
		{
			const std::size_t got = readAt(position, dest, size);
			position += got;
			return got;
		},
		archiveSize, directory);
	if (error != nullptr)
		*error = result;
	if (result != BigArchiveError::None)
		return nullptr;

	std::unique_ptr<BigSource> source(new BigSource(std::move(name), std::move(readAt)));
	for (const BigArchiveEntry &entry : directory)
		source->addEntry(entry.path, entry.offset, entry.size);
	return source;
}

std::size_t BigSource::read(const Entry &entry, std::uint64_t offset, void *dest, std::size_t size) const
{
	if (offset >= entry.size)
		return 0;
	const std::uint64_t left = entry.size - offset;
	const std::size_t n = left < size ? static_cast<std::size_t>(left) : size;
	return m_readAt(entry.offset + offset, dest, n);
}

std::unique_ptr<ZipSource> ZipSource::open(std::string name, ReadAt readAt, std::uint64_t archiveSize,
	ZipArchiveError *error)
{
	std::vector<ZipArchiveEntry> directory;
	std::vector<ZipArchiveSkipped> rejected;
	const ZipArchiveError result = readZipArchiveDirectory(readAt, archiveSize, directory, &rejected);
	if (error != nullptr)
		*error = result;
	if (result != ZipArchiveError::None)
		return nullptr;

	std::unique_ptr<ZipSource> source(new ZipSource(std::move(name), std::move(readAt), archiveSize));
	source->m_rejected = std::move(rejected);
	for (ZipArchiveEntry &zip : directory)
	{
		const std::size_t position = source->addEntry(zip.path, zip.headerOffset, zip.size);
		if (position == SIZE_MAX)
			continue;
		if (position == source->m_zip.size())
			source->m_zip.push_back(std::move(zip));
		else
			source->m_zip[position] = std::move(zip);
	}
	return source;
}

const ZipArchiveEntry *ZipSource::zipEntry(const Entry &entry) const
{
	const auto found = m_positions.find(entry.key);
	return found == m_positions.end() ? nullptr : &m_zip[found->second];
}

ZipArchiveError ZipSource::readEntry(const Entry &entry, std::vector<unsigned char> &data) const
{
	const ZipArchiveEntry *zip = zipEntry(entry);
	if (zip == nullptr)
	{
		data.clear();
		return ZipArchiveError::ReadFailed;
	}
	return extractZipEntry(m_readAt, m_archiveSize, *zip, data);
}

std::size_t ZipSource::read(const Entry &entry, std::uint64_t offset, void *dest, std::size_t size) const
{
	const ZipArchiveEntry *zip = zipEntry(entry);
	if (zip == nullptr || offset >= zip->size)
		return 0;
	const std::uint64_t left = zip->size - offset;
	const std::size_t n = left < size ? static_cast<std::size_t>(left) : size;

	if (zip->method == ZIP_METHOD_STORED && !(offset == 0 && n == zip->size))
	{
		std::uint64_t dataOffset = 0;
		if (locateZipEntryData(m_readAt, m_archiveSize, *zip, dataOffset) != ZipArchiveError::None)
			return 0;
		return m_readAt(dataOffset + offset, dest, n);
	}

	// A whole entry, or part of a deflated one: unpack and check it.
	std::vector<unsigned char> data;
	if (extractZipEntry(m_readAt, m_archiveSize, *zip, data) != ZipArchiveError::None)
		return 0;
	std::memcpy(dest, data.data() + offset, n);
	return n;
}

FolderSource::FolderSource(std::string name, const std::vector<File> &files, ReadFile readFile)
	: Source(std::move(name)), m_readFile(std::move(readFile))
{
	for (const File &file : files)
		addEntry(file.relativePath, 0, file.size);
}

std::size_t FolderSource::read(const Entry &entry, std::uint64_t offset, void *dest, std::size_t size) const
{
	if (offset >= entry.size)
		return 0;
	const std::uint64_t left = entry.size - offset;
	const std::size_t n = left < size ? static_cast<std::size_t>(left) : size;
	return m_readFile(entry.stored, offset, dest, n);
}

// ---------------------------------------------------------------------------------------------
// Mount plan

const char *rankName(Rank rank)
{
	switch (rank)
	{
		case Rank::ModPackage:		return "mod texture package";
		case Rank::ModLoose:		return "mod folder";
		case Rank::ModArchives:		return "mod archives";
		case Rank::ModArchive:		return "mod archive";
		case Rank::GamePackage:		return "texture package";
		case Rank::BaseArchives:	return "base archives";
		case Rank::BaseGame:		return "base game";
	}
	return "?";
}

std::vector<int> modPriority(const std::vector<ModInput> &mods, bool exactOrder)
{
	std::vector<int> order;
	for (int i = static_cast<int>(mods.size()) - 1; i >= 0; --i)
		order.push_back(i);
	if (mods.size() == 2 && !exactOrder)
	{
		const ModInput &a = mods[0];
		const ModInput &b = mods[1];
		if (!a.folder.empty() && a.archive.empty() && b.folder.empty() && !b.archive.empty())
			order = { 0, 1 };
	}
	return order;
}

std::vector<Mount> buildMountPlan(const MountInputs &inputs)
{
	std::vector<Mount> plan;
	const bool packages = std::clamp(inputs.tier, 0, 2) > 0;

	auto addArchives = [&plan](const std::vector<std::string> &names, bool highestFirst, Rank rank,
		bool texturesOnly, int mod)
	{
		for (const std::string &name : legacySorted(names, highestFirst))
		{
			Mount mount;
			mount.path = name;
			mount.rank = rank;
			mount.texturesOnly = texturesOnly;
			mount.mod = mod;
			plan.push_back(std::move(mount));
		}
	};

	for (int mod : modPriority(inputs.mods, inputs.exactOrder))
	{
		const ModInput &input = inputs.mods[static_cast<std::size_t>(mod)];
		if (packages)
			addArchives(input.packageArchives, true, Rank::ModPackage, true, mod);
		if (!input.folder.empty() && input.hasLooseFiles)
		{
			Mount mount;
			mount.path = input.folder;
			mount.folder = true;
			mount.rank = Rank::ModLoose;
			mount.mod = mod;
			plan.push_back(std::move(mount));
		}
		addArchives(input.folderArchives, true, Rank::ModArchives, false, mod);
		if (!input.archive.empty())
			addArchives({ input.archive }, true, Rank::ModArchive, false, mod);
	}

	// The package archives are loaded with overwrite: the later name above. SD shows the originals.
	if (packages)
		addArchives(inputs.packageArchives, true, Rank::GamePackage, true, -1);

	// Base archives are loaded without overwrite: the first name wins; -baseGame comes after them.
	addArchives(inputs.baseArchives, false, Rank::BaseArchives, false, -1);
	addArchives(inputs.baseGameArchives, false, Rank::BaseGame, false, -1);
	return plan;
}

// ---------------------------------------------------------------------------------------------
// Index

void Index::build(const std::vector<Layer> &layers, std::vector<std::string> *rejected)
{
	m_files.clear();
	m_keys.clear();
	for (std::size_t i = 0; i < layers.size(); ++i)
	{
		const Layer &layer = layers[i];
		if (layer.source == nullptr)
			continue;
		const std::vector<Entry> &entries = layer.source->entries();
		if (layer.texturesOnly)
		{
			const bool clean = std::all_of(entries.begin(), entries.end(),
				[](const Entry &entry) { return isTexturePackagePath(entry.key); });
			if (!clean)
			{
				if (rejected != nullptr)
					rejected->push_back(layer.source->name());
				continue;
			}
		}
		for (const Entry &entry : entries)
		{
			Hit hit;
			hit.source = layer.source;
			hit.entry = &entry;
			hit.layer = i;
			m_files.emplace(entry.key, hit);	// keeps the first, i.e. higher, layer
		}
	}
	m_keys.reserve(m_files.size());
	for (const auto &file : m_files)
		m_keys.push_back(file.first);
	std::sort(m_keys.begin(), m_keys.end());
}

const Hit *Index::find(std::string_view path) const
{
	std::string key;
	if (!normalizePath(path, key))
		return nullptr;
	const auto found = m_files.find(key);
	return found == m_files.end() ? nullptr : &found->second;
}

std::vector<std::string> Index::list(std::string_view directory, std::string_view pattern, bool recursive) const
{
	std::string prefix;
	if (normalizePath(directory, prefix))
		prefix += '/';
	else if (!isRootPath(directory))
		return {};	// ".." or a drive: nothing in the index lies there

	std::vector<std::string> result;
	for (auto it = std::lower_bound(m_keys.begin(), m_keys.end(), prefix);
		it != m_keys.end() && startsWith(*it, prefix); ++it)
	{
		const std::string_view rest = std::string_view(*it).substr(prefix.size());
		const std::size_t slash = rest.rfind('/');
		if (!recursive && slash != std::string_view::npos)
			continue;
		const std::string_view fileName = slash == std::string_view::npos ? rest : rest.substr(slash + 1);
		if (wildcardMatch(fileName, pattern))
			result.push_back(*it);
	}
	std::sort(result.begin(), result.end(), LegacyOrderLess());
	return result;
}

// ---------------------------------------------------------------------------------------------
// Live index

void LiveIndex::addTop(Layer layer)
{
	std::lock_guard<std::mutex> lock(m_lock);
	m_layers.insert(m_layers.begin(), std::move(layer));
	m_stale = true;
}

void LiveIndex::addBottom(Layer layer)
{
	std::lock_guard<std::mutex> lock(m_lock);
	m_layers.push_back(std::move(layer));
	m_stale = true;
}

std::size_t LiveIndex::removeOwner(const void *owner)
{
	std::lock_guard<std::mutex> lock(m_lock);
	const std::size_t before = m_layers.size();
	m_layers.erase(std::remove_if(m_layers.begin(), m_layers.end(),
		[owner](const Layer &layer) { return layer.owner == owner; }), m_layers.end());
	const std::size_t removed = before - m_layers.size();
	if (removed != 0)
		m_stale = true;
	return removed;
}

std::size_t LiveIndex::layerCount() const
{
	std::lock_guard<std::mutex> lock(m_lock);
	return m_layers.size();
}

std::shared_ptr<const Snapshot> LiveIndex::current() const
{
	std::lock_guard<std::mutex> lock(m_lock);
	if (m_stale || m_snapshot == nullptr)
	{
		auto snapshot = std::make_shared<Snapshot>();
		snapshot->layers = m_layers;
		snapshot->index.build(snapshot->layers, &snapshot->rejected);
		m_snapshot = std::move(snapshot);
		m_stale = false;
	}
	return m_snapshot;
}

// ---------------------------------------------------------------------------------------------
// Read-ahead plan

namespace
{

bool isSoundArchiveName(std::string_view name)
{
	const std::size_t slash = name.find_last_of("\\/");
	std::string file(name.substr(slash == std::string_view::npos ? 0 : slash + 1));
	for (char &c : file)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return startsWith(file, "audio") || startsWith(file, "music") || startsWith(file, "speech");
}

bool isSoundKey(std::string_view key)
{
	return endsWith(key, ".wav") || endsWith(key, ".mp3");
}

}	// namespace

std::vector<PrefetchItem> prefetchPlan(const Snapshot &snapshot, std::uint64_t mergeGap)
{
	std::vector<PrefetchItem> plan(snapshot.layers.size());
	std::vector<bool> onlySound(snapshot.layers.size(), true);
	snapshot.index.forEach([&](const std::string &key, const Hit &hit)
	{
		plan[hit.layer].files.push_back(hit.entry);
		if (!isSoundKey(key))
			onlySound[hit.layer] = false;
	});

	std::vector<PrefetchItem> result;
	for (std::size_t i = 0; i < plan.size(); ++i)
	{
		PrefetchItem &item = plan[i];
		const Layer &layer = snapshot.layers[i];
		if (item.files.empty() || onlySound[i] || isSoundArchiveName(layer.name.empty() ? layer.source->name() : layer.name))
			continue;
		item.layer = i;
		if (dynamic_cast<const BigSource *>(layer.source) != nullptr)
		{
			std::sort(item.files.begin(), item.files.end(),
				[](const Entry *a, const Entry *b) { return a->offset != b->offset ? a->offset < b->offset : a->key < b->key; });
			for (const Entry *entry : item.files)
			{
				if (entry->size == 0)
					continue;
				if (!item.ranges.empty())
				{
					PrefetchRange &last = item.ranges.back();
					const std::uint64_t end = last.offset + last.size;
					if (entry->offset <= end + mergeGap)
					{
						const std::uint64_t entryEnd = entry->offset + entry->size;
						if (entryEnd > end)
							last.size = entryEnd - last.offset;
						continue;
					}
				}
				item.ranges.push_back({entry->offset, entry->size});
			}
			for (const PrefetchRange &range : item.ranges)
				item.bytes += range.size;
		}
		else
		{
			std::sort(item.files.begin(), item.files.end(), [](const Entry *a, const Entry *b) { return a->key < b->key; });
			for (const Entry *entry : item.files)
				item.bytes += entry->size;
		}
		result.push_back(std::move(item));
	}
	return result;
}

}	// namespace Vfs
}	// namespace Engine
