/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// VfsDump -- resolves every archived path of a game folder with the new file system core
// (Engine::Vfs) and prints it in the format of the game switch -dateiIndexAusgeben:
//
//     <path, lower case, '/'>\t<archive the path comes from>
//
// Both lists must be equal for the same folder, tier and mods; that is the check that the mount
// plan reproduces the old precedence. The folder scan copies the old one
// (Win32LocalFileSystem::getFileListInDirectory): archive names keep their case and use '\\',
// subfolders with a dot in their name are not entered (the old search pattern "*." skips them).
// A mod folder contributes its .big and .zip archives and, above them, its loose files; a loose
// file is listed with its own path as the source, as the game does. From HD on the texture package
// (Texturstufen\Paket) of the game and of each mod folder goes on top; a package archive without its
// manifest is left out, and without a package of the game only SD exists, as in the game.
//
//   VfsDump <game folder> [--zh] [--tier sd|hd|uhd] [--basegame DIR[=NAME]] [--mod PATH[=NAME]] ... [--exact]
//
// --exact keeps the mods in the order given, as the game does for the active list of Settings.ini (without
// it two mods, a folder and an archive, put the folder on top like -mod).
//
// NAME is the path as the game sees it (the game prints archive names with it), for example
// "Z:\\tmp\\mod\\" when the game runs under Wine. Without NAME the path itself is used.

#include "Engine/Vfs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Engine::Vfs;

namespace
{

bool iequalsTail(const std::string &text, const char *tail)
{
	const std::size_t n = std::strlen(tail);
	if (text.size() < n)
		return false;
	for (std::size_t i = 0; i < n; ++i)
	{
		char c = text[text.size() - n + i];
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
		if (c != tail[i])
			return false;
	}
	return true;
}

bool istartsWith(const std::string &text, const std::string &head)
{
	return text.size() >= head.size() && compareLegacyOrder(text.substr(0, head.size()), head) == 0;
}

bool isArchiveName(const std::string &name, bool withZip)
{
	return iequalsTail(name, ".big") || (withZip && iequalsTail(name, ".zip"));
}

/// Archives below dir as the old recursive search found them; names are prefix + relative path.
/// Mod folders also hold .zip archives.
void scanArchives(const fs::path &dir, const std::string &prefix, std::vector<std::string> &names,
	std::map<std::string, fs::path> &disk, bool withZip = false, bool skipTierFolder = false)
{
	std::error_code error;
	std::vector<fs::directory_entry> entries;
	for (const fs::directory_entry &entry : fs::directory_iterator(dir, error))
		entries.push_back(entry);
	for (const fs::directory_entry &entry : entries)
	{
		const std::string name = entry.path().filename().string();
		if (entry.is_regular_file(error) && isArchiveName(name, withZip))
		{
			names.push_back(prefix + name);
			disk[prefix + name] = entry.path();
		}
	}
	for (const fs::directory_entry &entry : entries)
	{
		const std::string name = entry.path().filename().string();
		if (entry.is_directory(error) && name.find('.') == std::string::npos
			&& !(skipTierFolder && compareLegacyOrder(name, "Texturstufen") == 0))
			scanArchives(entry.path(), prefix + name + "\\", names, disk, withZip);
	}
}

/// Loose files of a mod folder as ArchiveFileSystem::loadModFolder finds them: the same walk, but
/// without archives and without the Texturstufen folder.
void scanLooseFiles(const fs::path &dir, const std::string &relative, std::vector<FolderSource::File> &files,
	std::map<std::string, fs::path> &disk)
{
	std::error_code error;
	std::vector<fs::directory_entry> entries;
	for (const fs::directory_entry &entry : fs::directory_iterator(dir, error))
		entries.push_back(entry);
	for (const fs::directory_entry &entry : entries)
	{
		const std::string name = entry.path().filename().string();
		if (!entry.is_regular_file(error) || isArchiveName(name, true))
			continue;
		const std::string path = relative + name;
		if (istartsWith(path, "Texturstufen\\"))
			continue;
		files.push_back({path, entry.file_size(error)});
		disk[path] = entry.path();
	}
	for (const fs::directory_entry &entry : entries)
	{
		const std::string name = entry.path().filename().string();
		if (entry.is_directory(error) && name.find('.') == std::string::npos)
			scanLooseFiles(entry.path(), relative + name + "\\", files, disk);
	}
}

/// A package archive carries its manifest at the root.
bool hasManifest(const Source &source)
{
	for (const Entry &entry : source.entries())
		if (entry.key == "texturpaket.txt")
			return true;
	return false;
}

/// Positional reads from a file; one stream per source, so reads take turns.
ReadAt readAtFile(std::ifstream *stream)
{
	return [stream](std::uint64_t offset, void *dest, std::size_t count) -> std::size_t
	{
		stream->clear();
		stream->seekg(static_cast<std::streamoff>(offset));
		stream->read(static_cast<char *>(dest), static_cast<std::streamsize>(count));
		return static_cast<std::size_t>(stream->gcount());
	};
}

/// "DISK=NAME" or "DISK".
void splitMapping(const std::string &argument, std::string &diskPath, std::string &gameName)
{
	const std::size_t equals = argument.find('=');
	diskPath = argument.substr(0, equals);
	gameName = equals == std::string::npos ? diskPath : argument.substr(equals + 1);
}

std::string withBackslash(std::string name)
{
	if (!name.empty() && name.back() != '\\' && name.back() != '/')
		name += '\\';
	return name;
}

int usage()
{
	std::fprintf(stderr, "usage: VfsDump <game folder> [--zh] [--tier sd|hd|uhd] [--basegame DIR[=NAME]] [--mod PATH[=NAME]] ... [--exact]\n");
	return 2;
}

}	// namespace

int main(int argc, char **argv)
{
	if (argc < 2)
		return usage();

	const fs::path game = argv[1];
	bool zeroHour = false;
	MountInputs inputs;
	std::map<std::string, fs::path> disk;	// archive name as the game sees it -> file on disk
	std::map<std::string, std::unique_ptr<FolderSource>> folders;	// mod folder as the game sees it -> loose files

	for (int i = 2; i < argc; ++i)
	{
		const std::string arg = argv[i];
		if (arg == "--exact")
			inputs.exactOrder = true;
		else if (arg == "--zh")
			zeroHour = true;
		else if (arg == "--tier" && i + 1 < argc)
		{
			const std::string tier = argv[++i];
			inputs.tier = tier == "uhd" ? 2 : (tier == "hd" ? 1 : 0);
		}
		else if (arg == "--basegame" && i + 1 < argc)
		{
			std::string diskPath, gameName;
			splitMapping(argv[++i], diskPath, gameName);
			scanArchives(diskPath, withBackslash(gameName), inputs.baseGameArchives, disk);
		}
		else if (arg == "--mod" && i + 1 < argc)
		{
			std::string diskPath, gameName;
			splitMapping(argv[++i], diskPath, gameName);
			ModInput mod;
			if (fs::is_directory(diskPath))
			{
				// The archives of the folder; those in its Texturstufen\Paket are the mod's texture package.
				mod.folder = withBackslash(gameName);
				scanArchives(diskPath, mod.folder, mod.folderArchives, disk, true, true);
				const fs::path packageDir = fs::path(diskPath) / "Texturstufen" / "Paket";
				if (fs::is_directory(packageDir))
					scanArchives(packageDir, mod.folder + "Texturstufen\\Paket\\", mod.packageArchives, disk, true);

				std::vector<FolderSource::File> files;
				auto looseDisk = std::make_shared<std::map<std::string, fs::path>>();
				scanLooseFiles(diskPath, "", files, *looseDisk);
				mod.hasLooseFiles = !files.empty();
				folders[mod.folder] = std::make_unique<FolderSource>(mod.folder, files,
					[looseDisk](const std::string &relative, std::uint64_t offset, void *dest, std::size_t count) -> std::size_t
					{
						std::ifstream file(looseDisk->at(relative), std::ios::binary);
						file.seekg(static_cast<std::streamoff>(offset));
						file.read(static_cast<char *>(dest), static_cast<std::streamsize>(count));
						return static_cast<std::size_t>(file.gcount());
					});
			}
			else
			{
				mod.archive = gameName;
				disk[gameName] = diskPath;
			}
			inputs.mods.push_back(mod);
		}
		else
			return usage();
	}

	// Base archives: recursive scan without the Texturstufen folder; Zero Hour deletes Data\INI\INIZH.big
	// before the scan (GameEngine::init).
	std::vector<std::string> base;
	scanArchives(game, "", base, disk);
	for (const std::string &name : base)
	{
		if (istartsWith(name, "Texturstufen\\"))
			continue;
		if (zeroHour && compareLegacyOrder(name, "Data\\INI\\INIZH.big") == 0)
			continue;
		inputs.baseArchives.push_back(name);
	}
	const fs::path packageDir = game / "Texturstufen" / "Paket";
	if (fs::is_directory(packageDir))
		scanArchives(packageDir, "Texturstufen\\Paket\\", inputs.packageArchives, disk);

	// Open the archives of the plan; an archive that does not read is not used, as before.
	std::vector<std::unique_ptr<Source>> sources;
	std::vector<std::unique_ptr<std::ifstream>> files;
	std::vector<Layer> layers;
	int gamePackages = static_cast<int>(inputs.packageArchives.size());
	for (const Mount &mount : buildMountPlan(inputs))
	{
		if (mount.folder)
		{
			const auto folder = folders.find(mount.path);
			if (folder != folders.end())
				layers.push_back({folder->second.get(), false});
			continue;
		}
		const auto found = disk.find(mount.path);
		if (found == disk.end())
			continue;
		auto file = std::make_unique<std::ifstream>(found->second, std::ios::binary);
		std::error_code error;
		const std::uint64_t size = fs::file_size(found->second, error);
		if (!*file || error)
		{
			std::fprintf(stderr, "cannot open %s\n", found->second.string().c_str());
			continue;
		}
		std::ifstream *stream = file.get();
		files.push_back(std::move(file));
		std::unique_ptr<Source> source;
		if (iequalsTail(mount.path, ".zip"))
		{
			Engine::ZipArchiveError zipError = Engine::ZipArchiveError::None;
			std::unique_ptr<ZipSource> zip = ZipSource::open(mount.path, readAtFile(stream), size, &zipError);
			if (zip == nullptr)
			{
				std::fprintf(stderr, "%s not used: %s\n", mount.path.c_str(), Engine::zipArchiveErrorText(zipError));
				continue;
			}
			for (const Engine::ZipArchiveSkipped &skipped : zip->rejected())
				std::fprintf(stderr, "%s: entry %s left out: %s\n", mount.path.c_str(), skipped.path.c_str(),
					Engine::zipArchiveErrorText(skipped.reason));
			source = std::move(zip);
		}
		else
		{
			Engine::BigArchiveError bigError = Engine::BigArchiveError::None;
			source = BigSource::open(mount.path, readAtFile(stream), size, &bigError);
			if (source == nullptr)
			{
				std::fprintf(stderr, "%s not used: %s\n", mount.path.c_str(), Engine::bigArchiveErrorText(bigError));
				continue;
			}
		}
		if (mount.texturesOnly && !hasManifest(*source))
		{
			std::fprintf(stderr, "texture package archive %s left out (no manifest)\n", mount.path.c_str());
			if (mount.rank == Rank::GamePackage)
				--gamePackages;
			continue;
		}
		layers.push_back({source.get(), mount.texturesOnly});
		sources.push_back(std::move(source));
	}
	if (inputs.tier > 0 && gamePackages <= 0)
	{
		// Without a package of the game the game only offers SD: no package is loaded at all.
		std::fprintf(stderr, "no texture package: tier SD\n");
		std::vector<Layer> kept;
		for (std::size_t i = 0; i < layers.size(); ++i)
			if (!layers[i].texturesOnly)
				kept.push_back(layers[i]);
		layers.swap(kept);
	}

	Index index;
	std::vector<std::string> rejected;
	index.build(layers, &rejected);
	for (const std::string &name : rejected)
		std::fprintf(stderr, "texture package archive %s rejected (not only textures)\n", name.c_str());

	std::vector<std::string> keys = index.list("", "*", true);
	std::sort(keys.begin(), keys.end());	// byte order, as the game's std::map writes it
	for (const std::string &key : keys)
	{
		const Hit *hit = index.find(key);
		const bool loose = folders.find(hit->source->name()) != folders.end()
			&& folders.find(hit->source->name())->second.get() == hit->source;
		std::printf("%s\t%s%s\n", key.c_str(), hit->source->name().c_str(), loose ? hit->entry->stored.c_str() : "");
	}
	return 0;
}
