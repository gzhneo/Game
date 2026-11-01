/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ModCatalog.h -- the mods a player can choose: everything directly in the mods folder (<user data>\Mods\)
// that is a folder, a .big or a .zip, each with an optional mod.txt describing it, and the list of active
// mods as Settings.ini keeps it ("Name;Name", highest priority first). Needs no engine header, so
// EngineTests test it directly.

#pragma once

#ifndef ENGINE_MODCATALOG_H
#define ENGINE_MODCATALOG_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Engine
{

enum class ModKind
{
	Folder,
	Big,
	Zip
};

enum class ModGame
{
	Any,		///< mod.txt names no game
	Generals,
	ZeroHour
};

struct ModInfo
{
	std::string name;			///< file or folder name in the mods folder; what the active list holds
	std::string path;			///< narrow path; a folder ends with a separator
	ModKind kind = ModKind::Folder;
	bool hasManifest = false;	///< a mod.txt was found
	std::string title;			///< from mod.txt, else the name
	std::string version;
	std::string author;
	std::string description;
	ModGame game = ModGame::Any;
	std::string manifestError;	///< mod.txt found but (partly) unusable; the mod stays usable
};

/// Reads mod.txt: one "Key = Value" per line, "#" and ";" start a comment line. Keys (case-insensitive):
/// Name, Version, Author/Autor, Game/Spiel (Generals, ZeroHour or ZH), Description/Beschreibung. A UTF-8
/// byte order mark is skipped. Unknown keys and lines without "=" are reported in info.manifestError.
void parseModManifest(std::string_view text, ModInfo &info);

/// Every folder, .big and .zip directly in folder (narrow path), sorted by name without case. A folder
/// named Texturstufen is not a mod. mod.txt is read from the root of the folder or archive.
std::vector<ModInfo> scanModFolder(std::string_view narrowFolder);

/// The active list: names separated by ';', trimmed, empty entries and repeats (without case) dropped.
std::vector<std::string> splitModList(std::string_view list);
std::string joinModList(const std::vector<std::string> &names);

enum class ModState
{
	Ready,
	Missing,	///< not in the mods folder
	OtherGame	///< mod.txt names the other game
};

struct ActiveMod
{
	std::string name;
	const ModInfo *info = nullptr;	///< into the catalog; nullptr when missing
	ModState state = ModState::Missing;
};

/// The active names looked up in the catalog (case-insensitive), in the order of the list.
std::vector<ActiveMod> resolveModList(const std::vector<ModInfo> &catalog, const std::vector<std::string> &names, ModGame game);

const char *modStateName(ModState state);

/// The command line for starting the game again after the active list changed: the arguments of this
/// run (the program first) without -mod and its value, which would replace the new list; quoted the way
/// CommandLineToArgvW reads it back.
std::wstring restartCommandLine(const std::vector<std::wstring> &arguments);

/// A loaded mod as games, replays and save games name it.
struct ModTagEntry
{
	std::string name;
	std::string version;
};

/// The mod list of the game options ("MD=" key) and save games: entries highest priority first, separated
/// by ',', each "name" or "name@version"; every character outside letters, digits, '.', '-', '_', '(' and
/// ')' is written as %XX, so the text never holds the separators of the game options. Entries that
/// would make it longer than maxLength are left out; a last entry "+N" counts them.
std::string encodeModTag(const std::vector<ModTagEntry> &mods, std::size_t maxLength);

/// Reads encodeModTag back; the "+N" entry becomes *omitted (0 without one). Broken escapes are kept as
/// they are.
std::vector<ModTagEntry> decodeModTag(std::string_view tag, int *omitted = nullptr);

/// Same mods in the same order: names without case, versions exactly.
bool sameModTags(std::string_view a, std::string_view b);

/// For messages: "Name 1.2, Other" (", ... (+N)" for left out entries); empty without mods.
std::string describeModTag(std::string_view tag);

}	// namespace Engine

#endif	// ENGINE_MODCATALOG_H
