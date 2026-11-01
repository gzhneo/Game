/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

//----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//----------------------------------------------------------------------------
//
// Project:   Generals
//
// Module:    Game Engine Common
//
// File name: ArchiveFileSystem.cpp
//
// Created:   11/26/01 TR
//
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Includes                                                      
//----------------------------------------------------------------------------

#include "PreRTS.h"
#include "Common/ArchiveFile.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/file.h"
#include "Common/LocalFileSystem.h"
#include "Common/PerfTimer.h"
#include "Common/VfsArchiveFile.h"
#include "Engine/ModCatalog.h"
#include "Engine/TexturePackage.h"
#include "Common/GameEdition.h"
#include "Common/GameEngine.h"
#include "Common/UserSettings.h"
#include "formatinto.h"
#include "Common/GlobalData.h"

#include <cstdio>
#include <map>
#include <string>
#include <string_view>

//----------------------------------------------------------------------------
//         Externals                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Defines                                                         
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Types                                                     
//----------------------------------------------------------------------------


//----------------------------------------------------------------------------
//         Private Data                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Public Data                                                      
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Prototypes                                               
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Functions                                               
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Public Functions                                                
//----------------------------------------------------------------------------


//------------------------------------------------------
// ArchivedFileInfo
//------------------------------------------------------
ArchiveFileSystem::ArchiveFileSystem() 
{
}

ArchiveFileSystem::~ArchiveFileSystem() 
{
	m_archiveFileMap.clear();
	::Engine::TexturePackage::instance().clear();
}

void ArchiveFileSystem::addArchiveFile(const AsciiString& archiveFilename, ArchiveFile *archiveFile)
{
	std::unique_ptr<ArchiveFile> &slot = m_archiveFileMap[archiveFilename];
	if (slot)
		m_replacedArchiveFiles.push_back(std::move(slot));
	slot.reset(archiveFile);
}

void ArchiveFileSystem::loadIntoDirectoryTree(const ArchiveFile *archiveFile, const AsciiString& archiveFilename, Bool overwrite)
{
	DEBUG_ASSERTCRASH(archiveFile->getVfsSource() != nullptr, ("archive %s has no source for the index", archiveFilename.str()));
	Engine::Vfs::Layer layer;
	layer.source = archiveFile->getVfsSource();
	layer.name = archiveFilename.str();
	layer.owner = archiveFile;
	layer.flags = m_loadingMods ? 1 : 0;
	if (overwrite)
		m_index.addTop(std::move(layer));
	else
		m_index.addBottom(std::move(layer));
}

static Bool isModFolderPath(const AsciiString& mod)
{
	return mod.endsWith("\\") || mod.endsWith("/");
}

// The active list of Settings.ini (Mods.Active, names in <user data>\Mods\, highest first) as paths in load
// order, lowest first. Not used when the command line names mods.
static std::vector<AsciiString> activeModsFromSettings(ArchiveFileSystem &fileSystem)
{
	std::vector<AsciiString> mods;
	if (UserSettings::isForced("Mods.Active"))
		return mods;
	const std::vector<std::string> names = ::Engine::splitModList(UserSettings::get("Mods.Active").str());
	if (names.empty())
		return mods;
	AsciiString folder;
	folder.format("%sMods\\", TheGlobalData->getPath_UserData().str());
	const std::vector<::Engine::ModInfo> catalog = ::Engine::scanModFolder(folder.str());
	const std::vector<::Engine::ActiveMod> active = ::Engine::resolveModList(catalog, names,
		isGameEditionZeroHour() ? ::Engine::ModGame::ZeroHour : ::Engine::ModGame::Generals);
	// log lines and problems in the order of the list, the paths lowest first
	char line[MAX_PATH * 2 + 64];
	for (const ::Engine::ActiveMod &mod : active)
	{
		WWFormat::Into(line, "MOD active {} {}", mod.name.c_str(), ::Engine::modStateName(mod.state));
		line[sizeof(line) - 1] = 0;
		GameEngine_RunnerLog(line);
		DEBUG_LOG(("%s\n", line));
		if (mod.state != ::Engine::ModState::Ready)
			fileSystem.addModProblem(AsciiString(mod.name.c_str()), mod.state == ::Engine::ModState::Missing
				? ArchiveFileSystem::ModProblemKind::Missing : ArchiveFileSystem::ModProblemKind::OtherGame, AsciiString::TheEmptyString);
		else if (!mod.info->manifestError.empty())
			fileSystem.addModProblem(AsciiString(mod.name.c_str()), ArchiveFileSystem::ModProblemKind::Manifest,
				AsciiString::TheEmptyString, 0, mod.info->manifestError.c_str());
	}
	for (std::vector<::Engine::ActiveMod>::const_reverse_iterator it = active.rbegin(); it != active.rend(); ++it)
		if (it->state == ::Engine::ModState::Ready)
			mods.push_back(AsciiString(it->info->path.c_str()));
	return mods;
}

// The name a player knows a mod by: the last part of its path.
static AsciiString modDisplayName(const AsciiString &modPath)
{
	std::string path = modPath.str();
	while (!path.empty() && (path.back() == '\\' || path.back() == '/'))
		path.pop_back();
	const std::size_t slash = path.find_last_of("\\/");
	return AsciiString(path.c_str() + (slash == std::string::npos ? 0 : slash + 1));
}

void ArchiveFileSystem::addModProblem(const AsciiString &modPath, ModProblemKind kind, const AsciiString &detail, Int count,
	const char *logReason)
{
	ModProblem problem;
	problem.mod = modDisplayName(modPath);
	problem.kind = kind;
	problem.detail = detail;
	problem.count = count;
	m_modProblems.push_back(problem);

	static const char *const KIND_NAMES[] = { "missing", "other-game", "unreadable", "partial", "package-rejected", "manifest" };
	char line[MAX_PATH * 2 + 128];
	WWFormat::Into(line, "MOD problem {} {} {:d} {}{}{}", problem.mod.str(), KIND_NAMES[static_cast<Int>(kind)], count, detail.str(),
		detail.isNotEmpty() && logReason != nullptr ? ": " : "", logReason != nullptr ? logReason : "");
	line[sizeof(line) - 1] = 0;
	GameEngine_RunnerLog(line);
	DEBUG_LOG(("%s\n", line));
}

// An archive of a mod that serves only part of its entries (a .zip leaves out what it cannot read).
void ArchiveFileSystem::noteSkippedEntries(const AsciiString &modPath, const ArchiveFile *archive, const AsciiString &archiveName)
{
	const VfsArchiveFile *vfsArchive = dynamic_cast<const VfsArchiveFile *>(archive);
	const Int skipped = vfsArchive != nullptr ? vfsArchive->getSkippedCount() : 0;
	if (skipped > 0)
		addModProblem(modPath, ModProblemKind::Partial, archiveName, skipped);
}

// The version mod.txt gives a mod at this path (empty without one).
static std::string modVersion(const AsciiString &modPath)
{
	std::string path = modPath.str();
	while (!path.empty() && (path.back() == '\\' || path.back() == '/'))
		path.pop_back();
	const std::size_t slash = path.find_last_of("\\/");
	const std::string folder = slash == std::string::npos ? std::string(".") : path.substr(0, slash + 1);
	const std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
	for (const ::Engine::ModInfo &info : ::Engine::scanModFolder(folder))
		if (_stricmp(info.name.c_str(), name.c_str()) == 0)
			return info.version;
	return std::string();
}

// Longest mod list in the game options; LAN announcements carry them in one packet.
static constexpr std::size_t MAX_MOD_TAG = 96;

void ArchiveFileSystem::loadMods() {
	// -mod on the command line keeps its old rules; a list ordered in the menu is taken exactly as it is
	const Bool exactOrder = TheGlobalData->m_mods.empty();
	const std::vector<AsciiString> mods = exactOrder ? activeModsFromSettings(*this) : TheGlobalData->m_mods;
	std::vector<Engine::Vfs::ModInput> inputs(mods.size());
	for (std::size_t i = 0; i < mods.size(); ++i)
	{
		if (isModFolderPath(mods[i]))
			inputs[i].folder = mods[i].str();
		else
			inputs[i].archive = mods[i].str();
	}

	// Loaded from the lowest to the highest; everything of a mod goes on top of what came before.
	m_loadingMods = TRUE;
	const std::vector<int> priority = Engine::Vfs::modPriority(inputs, exactOrder);
	std::vector<Bool> loaded(mods.size(), FALSE);
	for (std::vector<int>::const_reverse_iterator it = priority.rbegin(); it != priority.rend(); ++it)
	{
		const AsciiString &mod = mods[static_cast<std::size_t>(*it)];
		if (!isModFolderPath(mod))
		{
			ArchiveFile *archiveFile = openArchiveFile(mod.str());
			if (archiveFile != nullptr)
			{
				loadIntoDirectoryTree(archiveFile, mod, TRUE);
				addArchiveFile(mod, archiveFile);
				noteSkippedEntries(mod, archiveFile, mod);
				loaded[static_cast<std::size_t>(*it)] = TRUE;
			}
			else
			{
				DEBUG_LOG(("ArchiveFileSystem::loadMods - could not openArchiveFile(%s)\n", mod.str()));
				addModProblem(mod, ModProblemKind::Unreadable, AsciiString::TheEmptyString, 0, VfsArchiveFile::lastOpenError());
			}
			continue;
		}
		Bool found = loadModArchives(mod);
		found = loadModFolder(mod) || found;
		DEBUG_ASSERTLOG(found, ("mod folder %s holds neither archives nor files\n", mod.str()));
		loaded[static_cast<std::size_t>(*it)] = found;
		loadModPackage(mod);
	}
	m_loadingMods = FALSE;

	std::vector<::Engine::ModTagEntry> tagEntries;
	for (int index : priority)
	{
		if (!loaded[static_cast<std::size_t>(index)])
			continue;
		const AsciiString &mod = mods[static_cast<std::size_t>(index)];
		tagEntries.push_back(::Engine::ModTagEntry{ modDisplayName(mod).str(), modVersion(mod) });
	}
	m_modTag = ::Engine::encodeModTag(tagEntries, MAX_MOD_TAG).c_str();
	if (m_modTag.isNotEmpty())
	{
		char line[MAX_MOD_TAG + 32];
		WWFormat::Into(line, "MOD tag {}", m_modTag.str());
		line[sizeof(line) - 1] = 0;
		GameEngine_RunnerLog(line);
		DEBUG_LOG(("%s\n", line));
	}
}

Bool ArchiveFileSystem::loadModArchives(const AsciiString& folder)
{
	FilenameList names;
	TheLocalFileSystem->getFileListInDirectory(folder, AsciiString(""), AsciiString("*.big"), names, TRUE);
	TheLocalFileSystem->getFileListInDirectory(folder, AsciiString(""), AsciiString("*.zip"), names, TRUE);
	Bool loaded = FALSE;
	for (FilenameListIter it = names.begin(); it != names.end(); ++it)
	{
		if (it->getLength() <= folder.getLength() || isTextureTierPath(AsciiString(it->str() + folder.getLength())))
			continue;
		ArchiveFile *archiveFile = openArchiveFile(it->str());
		if (archiveFile == nullptr)
		{
			addModProblem(folder, ModProblemKind::Unreadable, AsciiString(it->str() + folder.getLength()), 0,
				VfsArchiveFile::lastOpenError());
			continue;
		}
		loadIntoDirectoryTree(archiveFile, *it, TRUE);
		addArchiveFile(*it, archiveFile);
		noteSkippedEntries(folder, archiveFile, AsciiString(it->str() + folder.getLength()));
		loaded = TRUE;
	}
	return loaded;
}

Bool ArchiveFileSystem::loadModFolder(const AsciiString& folder)
{
	FilenameList names;
	TheLocalFileSystem->getFileListInDirectory(folder, AsciiString(""), AsciiString("*"), names, TRUE);

	// Archives are mounted on their own; the texture tier folder only holds archives.
	std::vector<AsciiString> relativePaths;
	for (FilenameListIter it = names.begin(); it != names.end(); ++it)
	{
		if (it->getLength() <= folder.getLength())
			continue;
		AsciiString relative = it->str() + folder.getLength();
		AsciiString lower = relative;
		lower.toLower();
		// the description of the mod (mod.txt at its root) is not game data
		if (lower.endsWith(".big") || lower.endsWith(".zip") || isTextureTierPath(relative) || lower.compare("mod.txt") == 0)
			continue;
		relativePaths.push_back(relative);
	}
	if (relativePaths.empty())
		return FALSE;

	VfsArchiveFile *archiveFile = VfsArchiveFile::openFolder(folder, relativePaths);
	loadIntoDirectoryTree(archiveFile, folder, TRUE);
	addArchiveFile(folder, archiveFile);
	return TRUE;
}

// A lookup works on the snapshot it got; archives mounted meanwhile show up in the next lookup.
ArchiveFileSystem::Found ArchiveFileSystem::findLayer(const AsciiString& filename) const
{
	Found found;
	found.snapshot = m_index.current();
	const Engine::Vfs::Hit *hit = found.snapshot->index.find(filename.str());
	if (hit != nullptr)
		found.layer = &found.snapshot->layers[hit->layer];
	return found;
}

Bool ArchiveFileSystem::isModFile(const AsciiString& filename, AsciiString *diskPath) const
{
	if (diskPath != nullptr)
		diskPath->clear();
	const Found layer = findLayer(filename);
	if (!layer || !layer.mod())
		return FALSE;
	if (diskPath != nullptr)
	{
		// a loose file of a mod folder is named by its own path, an archive entry by the archive
		const AsciiString name = layer.name();
		const AsciiString source = layer.archive()->getSourceForFile(name, filename);
		if (source.compare(name) != 0)
			*diskPath = source;
	}
	return TRUE;
}

File *ArchiveFileSystem::openModFile(const Char *filename)
{
	const Found layer = findLayer(AsciiString(filename));
	return layer && layer.mod() ? layer.archive()->openFile(filename, File::READ | File::BINARY) : nullptr;
}

Bool ArchiveFileSystem::doesFileExist(const Char *filename) const
{
	return static_cast<bool>(findLayer(AsciiString(filename)));
}

File * ArchiveFileSystem::openFile(const Char *filename, Int access /* = 0 */) 
{
	const Found layer = findLayer(AsciiString(filename));
	return layer ? layer.archive()->openFile(filename, access) : nullptr;
}

Bool ArchiveFileSystem::getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const
{
	if (fileInfo == nullptr || filename.getLength() <= 0) {
		return FALSE;
	}
	const Found layer = findLayer(filename);
	return layer && layer.archive()->getFileInfo(filename, fileInfo);
}

AsciiString ArchiveFileSystem::getArchiveFilenameForFile(const AsciiString& filename) const
{
	const Found layer = findLayer(filename);
	return layer ? layer.name() : AsciiString::TheEmptyString;
}

// Always with subdirectories (the per-archive lists ignored the flag), names in lower case below the
// directory as given, and the pattern compared with case, so upper case in it never matches.
void ArchiveFileSystem::getFileListInDirectory(const AsciiString& /*currentDirectory*/, const AsciiString& originalDirectory, const AsciiString& searchName, FilenameList &filenameList, Bool /*searchSubdirectories*/) const
{
	const std::shared_ptr<const Engine::Vfs::Snapshot> snapshot = m_index.current();
	const char *pattern = searchName.str();
	for (const char *p = pattern; *p != 0; ++p)
	{
		if (*p >= 'A' && *p <= 'Z')
			return;
	}
	std::string prefix;
	const Bool atRoot = !Engine::Vfs::normalizePath(originalDirectory.str(), prefix);
	const std::size_t skip = atRoot ? 0 : prefix.size() + 1;
	AsciiString base = originalDirectory;
	if (base.getLength() > 0 && !base.endsWith("\\"))
		base.concat('\\');
	for (const std::string &key : snapshot->index.list(originalDirectory.str(), "*", true))
	{
		const std::size_t slash = key.rfind('/');
		const std::string_view fileName = std::string_view(key).substr(slash == std::string::npos ? 0 : slash + 1);
		if (!Engine::Vfs::wildcardMatch(fileName, pattern))
			continue;
		std::string relative = key.substr(skip);
		for (char &ch : relative)
		{
			if (ch == '/')
				ch = '\\';
		}
		AsciiString name = base;
		name.concat(relative.c_str());
		filenameList.insert(name);
	}
}

Bool ArchiveFileSystem::writeIndexDump(const char *path) const
{
	// Every path of a loaded archive with the archive it is read from (a loose file by its own path),
	// sorted by the map.
	const std::shared_ptr<const Engine::Vfs::Snapshot> snapshot = m_index.current();
	std::map<std::string, std::string> resolved;
	for (const std::string &key : snapshot->index.list("", "*", true))
	{
		const Engine::Vfs::Layer &layer = snapshot->layers[snapshot->index.find(key)->layer];
		const ArchiveFile *archive = static_cast<const ArchiveFile *>(layer.owner);
		resolved[key] = archive->getSourceForFile(AsciiString(layer.name.c_str()), AsciiString(key.c_str())).str();
	}

	FILE *fp = fopen(path, "w");
	if (fp == nullptr)
	{
		DEBUG_LOG(("ArchiveFileSystem::writeIndexDump - cannot write %s\n", path));
		return FALSE;
	}
	for (std::map<std::string, std::string>::const_iterator it = resolved.begin(); it != resolved.end(); ++it)
		fprintf(fp, "%s\t%s\n", it->first.c_str(), it->second.empty() ? "-" : it->second.c_str());
	fclose(fp);
	DEBUG_LOG(("ArchiveFileSystem::writeIndexDump - %d paths written to %s\n", static_cast<Int>(resolved.size()), path));
	return TRUE;
}

std::vector<ArchiveFileSystem::PrefetchJob> ArchiveFileSystem::getPrefetchJobs() const
{
	const std::shared_ptr<const Engine::Vfs::Snapshot> snapshot = m_index.current();
	std::vector<PrefetchJob> jobs;
	for (const Engine::Vfs::PrefetchItem &item : Engine::Vfs::prefetchPlan(*snapshot))
	{
		const Engine::Vfs::Layer &layer = snapshot->layers[item.layer];
		if (dynamic_cast<const Engine::Vfs::FolderSource *>(layer.source) != nullptr)
		{
			// a mod folder: its loose files one by one
			for (const Engine::Vfs::Entry *entry : item.files)
			{
				PrefetchJob job;
				job.path = static_cast<const ArchiveFile *>(layer.owner)->getSourceForFile(AsciiString(layer.name.c_str()),
					AsciiString(entry->key.c_str())).str();
				job.bytes = entry->size;
				jobs.push_back(std::move(job));
			}
			continue;
		}
		PrefetchJob job;
		job.path = layer.name;
		job.ranges = item.ranges;
		job.bytes = item.bytes;
		jobs.push_back(std::move(job));
	}
	return jobs;
}
