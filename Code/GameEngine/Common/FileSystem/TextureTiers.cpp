/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// TextureTiers.cpp -- texture tiers SD, HD, UHD in the archive system.
//
// HD and UHD come from the texture package: the archives under Texturstufen\Paket next to the exe
// (junction into the asset folder). Each holds every texture once in its highest resolution with a
// full mip chain, plus a manifest with the original size of each texture (Engine::TexturePackage).
// The tier only decides how many top mip levels the texture loader skips (W3DDisplay::init sets the
// hook). SD shows the originals and leaves the package closed; without a package only SD exists.
// Win32BIGFileSystem::init skips the Texturstufen folder in its recursive scan. Called at the end of
// parseCommandLine, before the free content and the mods, which stay on top. The tier is fixed from
// then on; switching needs a restart (like AntiAliasing).
//
// A package may contain only textures (.dds/.tga under Art\Textures, Art\Terrain or
// Data\<language>\Art\Textures) and its manifest. INI files would enter m_iniCRC and models (bones)
// the game logic, so an archive with other content is rejected as a whole. The same holds for a
// mod's package (loadMods). A free content folder may in addition carry models under Art\W3D (trees
// and bushes without weapon bones), which only change what is drawn.

#include "PreRTS.h"
#include "formatinto.h"
#include "Common/ArchiveFile.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/file.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/LocalFileSystem.h"
#include "Common/UserSettings.h"
#include "Common/CommandLine.h"
#include "Engine/TextureTier.h"
#include "Engine/TexturePackage.h"
#include "Common/VfsArchiveFile.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
	const char *const TIER_ROOT = "Texturstufen\\";
	const char *const PACKAGE_DIR = "Texturstufen\\Paket\\";
	const char *const FREE_CONTENT_ROOT = "ProjectData\\Inhalt\\";

	/// Working set of a map per tier (video memory). Measured with -logTextureStats: after two minutes
	/// of play at most 42 MB in SD and 141 MB in HD (factor 3.2 to 3.4), both games, three maps, two
	/// and four players. Set with headroom for longer games: SD 64 MB, HD four times, UHD sixteen times.
	void setDemand(::Engine::TextureTierInput &input)
	{
		const unsigned __int64 MB = 1024ull * 1024ull;
		const unsigned __int64 sd = 64ull * MB;
		input.demandBytes[::Engine::TEXTURE_TIER_SD] = sd;
		input.demandBytes[::Engine::TEXTURE_TIER_HD] = sd * 4;
		input.demandBytes[::Engine::TEXTURE_TIER_UHD] = sd * 16;
	}

	/// Only textures in the original paths (lower case, as in the archive tree) and the manifest at the root.
	Bool isAllowedPackageEntry(const AsciiString &name)
	{
		AsciiString lower = name;
		lower.toLower();
		if (lower == ::Engine::TEXTURE_PACKAGE_MANIFEST)
			return TRUE;
		if (!lower.endsWith(".dds") && !lower.endsWith(".tga"))
			return FALSE;
		if (lower.startsWith("art\\textures\\") || lower.startsWith("art/textures/")
			|| lower.startsWith("art\\terrain\\") || lower.startsWith("art/terrain/"))
			return TRUE;
		if (lower.startsWith("data\\") || lower.startsWith("data/"))
			return (strstr(lower.str(), "\\art\\textures\\") != nullptr || strstr(lower.str(), "/art/textures/") != nullptr);
		return FALSE;
	}

	/// Models of a free content folder: .w3d in the original path.
	Bool isAllowedFreeModelEntry(const AsciiString &name)
	{
		AsciiString lower = name;
		lower.toLower();
		return lower.endsWith(".w3d") && (lower.startsWith("art\\w3d\\") || lower.startsWith("art/w3d/"));
	}
}

// Returns TRUE if the path is inside the Texturstufen folder (for Win32BIGFileSystem::loadBigFilesFromDirectory).
Bool ArchiveFileSystem::isTextureTierPath(const AsciiString &path)
{
	return _strnicmp(path.str(), TIER_ROOT, strlen(TIER_ROOT)) == 0;
}

ArchiveFile *ArchiveFileSystem::openTexturePackageArchive(const AsciiString &archiveName)
{
	std::unique_ptr<ArchiveFile> archiveFile(openArchiveFile(archiveName.str()));
	if (archiveFile == nullptr)
		return nullptr;

	FilenameList contents;
	archiveFile->getFileListInDirectory(AsciiString(""), AsciiString(""), AsciiString("*"), contents, TRUE);
	const char *reason = nullptr;
	AsciiString offending;
	for (FilenameListIter it = contents.begin(); it != contents.end() && reason == nullptr; ++it)
	{
		if (!isAllowedPackageEntry(*it))
		{
			reason = "not a texture";
			offending = *it;
		}
	}

	if (reason == nullptr)
	{
		File *manifest = archiveFile->openFile(::Engine::TEXTURE_PACKAGE_MANIFEST);
		if (manifest == nullptr)
			reason = "no manifest";
		else
		{
			const Int size = manifest->size();
			std::vector<char> text(size > 0 ? static_cast<size_t>(size) : 0);
			const Bool complete = size > 0 && manifest->read(text.data(), size) == size;
			manifest->close();
			if (!complete || ::Engine::TexturePackage::instance().addManifest(text.data(), text.size(), archiveName.str()) < 0)
				reason = "manifest not readable";
		}
	}

	if (reason != nullptr)
	{
		DEBUG_LOG(("texture package: %s rejected, %s %s\n", archiveName.str(), reason, offending.str()));
		char line[MAX_PATH * 2 + 64];
		WWFormat::Into(line, "TEXTURPAKET abgewiesen {} wegen {} {}", archiveName.str(), reason, offending.str());
		line[sizeof(line) - 1] = 0;
		GameEngine_RunnerLog(line);
		return nullptr;
	}
	return archiveFile.release();
}

// The detail level is not loaded yet when the tier is chosen: -staticLOD, otherwise Settings.ini "StaticGameLOD".
// Unknown (the game measures it on the first start) counts as not Low.
static Bool staticLevelIsLowAtStartup()
{
	const char *level = getForcedStaticLOD();
	AsciiString stored;
	if (level == nullptr)
	{
		if (UserSettings::isSetInFile("Detail.StaticGameLOD"))
		{
			stored = UserSettings::getText("Detail.StaticGameLOD", AsciiString::TheEmptyString);
			level = stored.str();
		}
		else	// first start: the level the detection will choose (GameLODManager::findStaticLODLevel)
			return ::Engine::recommendDetailLevel(::Engine::VideoMemoryInfo::get()).level == 0;
	}
	return level != nullptr && stricmp(level, "Low") == 0;
}

// Share of the window height the 3D scene is drawn in (internal render resolution); the tier follows the height
// actually rendered: -renderScale, Settings.ini "RenderScale", otherwise the share of the level Low (same value as
// GameLODOverride.ini).
static const Real LOW_LEVEL_RENDER_SCALE = 0.75f;

static Real renderScaleAtStartup()
{
	const Real user = UserSettings::getRenderScale();
	if (user > 0.0f)
		return user;
	return staticLevelIsLowAtStartup() ? LOW_LEVEL_RENDER_SCALE : 1.0f;
}

void ArchiveFileSystem::loadTextureTiers()
{
	// Requested tier: -textureQuality overrides Options.ini "TextureQuality" (default Auto).
	::Engine::TextureTierInput input;
	const char *source = "-textureQuality";
	input.requested = TheGlobalData->m_textureQuality;
	if (input.requested == ::Engine::TEXTURE_TIER_UNSET)
	{
		input.requested = UserSettings::getTextureQuality();
		source = "Settings.ini";
	}

	// Texture package: its manifests name the highest tier it holds. Archives that fail the check are
	// left out; without a valid archive only SD is available.
	FilenameList packageNames;
	TheLocalFileSystem->getFileListInDirectory(AsciiString(PACKAGE_DIR), AsciiString(""), AsciiString("*.big"), packageNames, TRUE);
	::Engine::TexturePackage &package = ::Engine::TexturePackage::instance();
	package.clear();
	std::vector<std::pair<AsciiString, ArchiveFile *>> packageArchives;
	Int packageRejected = 0;
	for (FilenameListIter it = packageNames.begin(); it != packageNames.end(); ++it)
	{
		ArchiveFile *archive = openTexturePackageArchive(*it);
		if (archive != nullptr)
			packageArchives.emplace_back(*it, archive);
		else
			++packageRejected;
	}
	const Bool usePackage = !packageArchives.empty() && package.active();
	for (Int tier = ::Engine::TEXTURE_TIER_HD; tier < ::Engine::TEXTURE_TIER_COUNT; ++tier)
		input.available[tier] = usePackage && tier <= package.highestTier();

	// Rendered height (desktop height with -borderless, like W3DDisplay::init, times the render scale) and video memory
	const Int windowHeight = TheGlobalData->m_borderless ? ::GetSystemMetrics(SM_CYSCREEN) : TheGlobalData->m_yResolution;
	input.outputHeight = static_cast<Int>(static_cast<Real>(windowHeight) * renderScaleAtStartup() + 0.5f);
	const ::Engine::VideoMemoryInfo &memory = ::Engine::VideoMemoryInfo::get();
	input.budgetBytes = memory.budgetBytes();
	input.unifiedMemory = memory.umaKnown && memory.unifiedMemory;
	input.lowDetail = staticLevelIsLowAtStartup();
	setDemand(input);

	const ::Engine::TextureTierChoice choice = ::Engine::chooseTextureTier(input);
	TheWritableGlobalData->m_textureTier = choice.tier;

	// SD shows the originals: the package holds upscaled textures, and their mips at original size look
	// softer than the originals.
	if (usePackage && choice.tier > ::Engine::TEXTURE_TIER_SD)
	{
		for (auto &entry : packageArchives)
		{
			loadIntoDirectoryTree(entry.second, entry.first, TRUE);
			addArchiveFile(entry.first, entry.second);
		}
		package.setTier(choice.tier);

		char line[512];
		WWFormat::Into(line, "TEXTURPAKET {} wunsch {} ({}) grund {} hoehe {:d} budget {:d} MB uma {} hoechste {} archive {:d} texturen {:d} fehlerzeilen {:d} abgewiesen {:d}", ::Engine::textureTierName(choice.tier), ::Engine::textureTierName(input.requested), source, choice.reason, input.outputHeight, static_cast<unsigned>(input.budgetBytes / (1024ull * 1024ull)), memory.umaKnown ? (input.unifiedMemory ? "ja" : "nein") : "unbekannt", ::Engine::textureTierName(package.highestTier()), static_cast<Int>(packageArchives.size()), static_cast<Int>(package.size()), static_cast<Int>(package.badLines()), packageRejected);
		line[sizeof(line) - 1] = 0;
		GameEngine_RunnerLog(line);
		DEBUG_LOG(("HD_ASSETS: %s\n", line));
		return;
	}
	for (auto &entry : packageArchives)
		delete entry.second;
	// The manifests of the free content (loaded next) still apply in SD.
	package.clear();
	package.setTier(choice.tier);

	char line[512];
	WWFormat::Into(line, "TEXTURESTUFE {} wunsch {} ({}) grund {} hoehe {:d} budget {:d} MB uma {} vorhanden SD{}{} paket {:d} abgewiesen {:d}", ::Engine::textureTierName(choice.tier), ::Engine::textureTierName(input.requested), source, choice.reason, input.outputHeight, static_cast<unsigned>(input.budgetBytes / (1024ull * 1024ull)), memory.umaKnown ? (input.unifiedMemory ? "ja" : "nein") : "unbekannt", input.available[::Engine::TEXTURE_TIER_HD] ? ",HD" : "", input.available[::Engine::TEXTURE_TIER_UHD] ? ",UHD" : "", static_cast<Int>(packageArchives.size()), packageRejected);
	line[sizeof(line) - 1] = 0;
	GameEngine_RunnerLog(line);
	DEBUG_LOG(("HD_ASSETS: %s\n", line));
}

// A mod's texture package (<mod>\Texturstufen\Paket\*.big, *.zip), built like the game's, loaded above the
// mod's own files from HD on. In SD the mod's loose files and archives show its originals.
void ArchiveFileSystem::loadModPackage(const AsciiString &folder)
{
	if (TheGlobalData->m_textureTier <= ::Engine::TEXTURE_TIER_SD)
		return;
	AsciiString dir;
	dir.format("%s%s", folder.str(), PACKAGE_DIR);
	FilenameList names;
	TheLocalFileSystem->getFileListInDirectory(dir, AsciiString(""), AsciiString("*.big"), names, TRUE);
	TheLocalFileSystem->getFileListInDirectory(dir, AsciiString(""), AsciiString("*.zip"), names, TRUE);
	if (names.empty())
		return;
	Int archives = 0, rejected = 0;
	for (FilenameListIter it = names.begin(); it != names.end(); ++it)
	{
		ArchiveFile *archive = openTexturePackageArchive(*it);
		if (archive == nullptr)
		{
			++rejected;
			continue;
		}
		loadIntoDirectoryTree(archive, *it, TRUE);
		addArchiveFile(*it, archive);
		++archives;
	}
	char line[MAX_PATH + 96];
	WWFormat::Into(line, "MOD paket {} {} archive {:d} abgewiesen {:d}", ::Engine::textureTierName(TheGlobalData->m_textureTier), folder.str(), archives, rejected);
	line[sizeof(line) - 1] = 0;
	GameEngine_RunnerLog(line);
	DEBUG_LOG(("%s\n", line));
	if (rejected > 0)
		addModProblem(folder, ModProblemKind::PackageRejected, AsciiString::TheEmptyString, rejected);
}

void ArchiveFileSystem::checkTexturePackageSources()
{
	::Engine::TexturePackage &package = ::Engine::TexturePackage::instance();
	if (!package.active())
		return;
	Int replaced = 0;
	for (const std::string &path : package.paths())
	{
		// The loader takes the DDS of a texture name if one exists anywhere, else the TGA.
		const size_t dot = path.find_last_of('.');
		const std::string stem = dot == std::string::npos ? path : path.substr(0, dot);
		AsciiString archive = getArchiveFilenameForFile(AsciiString((stem + ".dds").c_str()));
		if (archive.isEmpty())
			archive = getArchiveFilenameForFile(AsciiString(path.c_str()));
		if (!archive.isEmpty() && !package.provides(archive.str(), path.c_str()) && package.forget(path.c_str()))
		{
			DEBUG_LOG(("HD_ASSETS: %s comes from %s, whose manifest does not name it\n", path.c_str(), archive.str()));
			++replaced;
		}
	}
	char line[96];
	WWFormat::Into(line, "TEXTURPAKET ersetzt {:d} texturen {:d}", replaced, static_cast<Int>(package.size()));
	line[sizeof(line) - 1] = 0;
	GameEngine_RunnerLog(line);
}

// Free content: every folder under ProjectData\Inhalt is a package of loose textures and models (free terrain,
// free roads, free trees). It replaces textures of the game data and of the texture package, so it is loaded
// after the package; mods stay above it. It may contain only textures, models under Art\W3D and its manifest;
// a folder with other files is rejected as a whole. With a manifest (texturpaket.txt) its files
// are its highest version with a full mip chain, and the tier sets their mip offset as for the game's
// package, in SD too; without one they load at their own size.
Int ArchiveFileSystem::loadFreeContentFolder(const AsciiString &folder, Int &manifestEntries)
{
	manifestEntries = -1;
	FilenameList names;
	TheLocalFileSystem->getFileListInDirectory(folder, AsciiString(""), AsciiString("*"), names, TRUE);
	std::vector<AsciiString> relativePaths;
	Bool hasManifest = FALSE;
	for (FilenameListIter it = names.begin(); it != names.end(); ++it)
	{
		if (it->getLength() <= folder.getLength())
			continue;
		AsciiString relative = it->str() + folder.getLength();
		if (isTextureTierPath(relative))
			continue;		// tier folders of an older copy; the package carries its highest version itself
		AsciiString lower = relative;
		lower.toLower();
		if (lower == ::Engine::TEXTURE_PACKAGE_MANIFEST)
		{
			hasManifest = TRUE;
			continue;
		}
		if (lower.endsWith(".md") || lower.endsWith(".txt"))
			continue;		// package description
		if (!isAllowedPackageEntry(relative) && !isAllowedFreeModelEntry(relative))
		{
			DEBUG_LOG(("free content: %s rejected, contains %s (only textures and models allowed)\n", folder.str(), relative.str()));
			char line[MAX_PATH * 2 + 64];
			WWFormat::Into(line, "FREIER INHALT abgewiesen {} wegen {}", folder.str(), relative.str());
			line[sizeof(line) - 1] = 0;
			GameEngine_RunnerLog(line);
			return -1;
		}
		relativePaths.push_back(relative);
	}
	if (hasManifest)
	{
		AsciiString manifestPath;
		manifestPath.format("%s%s", folder.str(), ::Engine::TEXTURE_PACKAGE_MANIFEST);
		File *manifest = TheLocalFileSystem->openFile(manifestPath.str(), File::READ | File::BINARY);
		if (manifest != nullptr)
		{
			const Int size = manifest->size();
			std::vector<char> text(size > 0 ? static_cast<size_t>(size) : 0);
			const Bool complete = size > 0 && manifest->read(text.data(), size) == size;
			manifest->close();
			::Engine::TexturePackage &package = ::Engine::TexturePackage::instance();
			if (complete && package.addManifest(text.data(), text.size(), folder.str()) >= 0)
				manifestEntries = package.namedBy(folder.str());
		}
		if (manifestEntries < 0)
		{
			char line[MAX_PATH + 64];
			WWFormat::Into(line, "FREIER INHALT manifest unlesbar {}", folder.str());
			line[sizeof(line) - 1] = 0;
			GameEngine_RunnerLog(line);
		}
	}
	if (relativePaths.empty())
		return 0;
	VfsArchiveFile *archiveFile = VfsArchiveFile::openFolder(folder, relativePaths);
	loadIntoDirectoryTree(archiveFile, folder, TRUE);
	addArchiveFile(folder, archiveFile);
	return static_cast<Int>(relativePaths.size());
}

void ArchiveFileSystem::loadFreeContent()
{
	if (TheGlobalData->m_freeContent < 0)
	{
		Int choice = 1;
		if (TheGlobalData->m_isWorldBuilder)
			choice = 0;		// the map editor shows the game data unless the switch is given
		else
		{
			if (UserSettings::getText("Detail.FreeContent", AsciiString::TheEmptyString).compareNoCase("ea") == 0)
				choice = 0;
		}
		TheWritableGlobalData->m_freeContent = choice;
	}
	if (TheGlobalData->m_freeContent != 1)
	{
		GameEngine_RunnerLog("FREIER INHALT aus");
		return;
	}

	FilenameList entries;
	TheLocalFileSystem->getFileListInDirectory(AsciiString(FREE_CONTENT_ROOT), AsciiString(""), AsciiString("*"), entries, TRUE);
	std::vector<AsciiString> packages;
	const size_t rootLength = strlen(FREE_CONTENT_ROOT);
	for (FilenameListIter it = entries.begin(); it != entries.end(); ++it)
	{
		if (it->getLength() <= static_cast<Int>(rootLength))
			continue;
		const char *rest = it->str() + rootLength;
		const char *slash = strpbrk(rest, "\\/");
		if (slash == nullptr)
			continue;		// a file directly under Inhalt is no package
		const std::string prefix(it->str(), static_cast<size_t>(slash - it->str()) + 1);
		AsciiString package(prefix.c_str());
		if (std::find(packages.begin(), packages.end(), package) == packages.end())
			packages.push_back(package);
	}
	std::sort(packages.begin(), packages.end(), [](const AsciiString &a, const AsciiString &b) { return stricmp(a.str(), b.str()) < 0; });

	for (const AsciiString &package : packages)
	{
		Int manifestEntries = -1;
		const Int files = loadFreeContentFolder(package, manifestEntries);
		char line[MAX_PATH + 96];
		WWFormat::Into(line, "FREIER INHALT {} dateien {:d} stufe {} manifest {:d}", package.str(), files,
			::Engine::textureTierName(TheGlobalData->m_textureTier), manifestEntries);
		line[sizeof(line) - 1] = 0;
		GameEngine_RunnerLog(line);
		DEBUG_LOG(("%s\n", line));
	}
	if (packages.empty())
		GameEngine_RunnerLog("FREIER INHALT keine Pakete");
}
