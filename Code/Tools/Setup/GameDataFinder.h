// Copyright (C) 2026 GZH Neo
// Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
// (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.

// Finds installations of Generals and Zero Hour (Steam, EA app, Origin, retail discs) by their content.
// Portable: the Windows-specific search for candidate folders lives in SystemProbeWin32.cpp.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace gzhsetup
{

enum class Game
{
	Generals,
	ZeroHour,
};

const char* gameName(Game game);

// A folder to look into, with where it came from ("Steam", "EA app", "registry", "manual", ...).
struct Candidate
{
	std::filesystem::path path;
	std::string origin;
};

// A folder that holds the archives of one game.
struct GameFolder
{
	Game game = Game::Generals;
	std::filesystem::path path;
	std::string origin;
	// Language as the game names it (english, german, german2, ...), from the audio archive's name.
	std::string language;
	// Known release, e.g. "1.08 retail" or "1.04"; empty if the archive sizes match no known release.
	std::string edition;
	// The Generals archives that Zero Hour installations carry in their ZH_Generals subfolder.
	bool baseFolderOfZeroHour = false;
	// .big files that are not EA archives of this game (mods); the copy leaves them out.
	std::vector<std::string> foreignArchives;
};

struct Detection
{
	std::vector<GameFolder> found;
	std::optional<GameFolder> generals;
	std::optional<GameFolder> zeroHour;
};

// EA's archive names of a game in lower case; the <language> archives come from the given language.
std::set<std::string> eaArchiveNames(Game game, const std::string& language);

// Looks at one folder only. Returns nothing if it holds neither game.
std::optional<GameFolder> classifyFolder(const std::filesystem::path& folder, const std::string& origin);

// Looks at every candidate and its subfolders down to two levels (EA app: both games below one folder,
// Steam and EA app: Generals in ZH_Generals). Each folder is reported once.
std::vector<GameFolder> findGameFolders(const std::vector<Candidate>& candidates);

// Picks one folder per game. Zero Hour: a known release before an unknown one, then the order of discovery.
// Generals: a known release, then one from the same installation as Zero Hour (same origin), then a folder
// of its own before ZH_Generals (it has the language videos and the small files).
Detection chooseGameFolders(std::vector<GameFolder> found);

// Library folders from Steam's steamapps/libraryfolders.vdf (the "path" values).
std::vector<std::filesystem::path> parseSteamLibraryFolders(const std::string& vdfText);

// "installdir" from a Steam appmanifest_<id>.acf; empty if missing.
std::string parseSteamInstallDir(const std::string& acfText);

// Steam's app id of "Command & Conquer Generals Zero Hour" (contains both games).
inline constexpr const char* STEAM_APP_ID = "2732960";

// Candidate folders on this machine: Steam libraries, EA app / Origin / retail registry entries, default
// install folders. Windows only (SystemProbeWin32.cpp).
std::vector<Candidate> collectSystemCandidates();

// Built-in tests for --selftest; returns the number of failures.
int runSelfTest();

} // namespace gzhsetup
