/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// GameLaunch.h -- starting the game with a map, as the old WorldBuilder's "Jump to Game" did.
//
// The game's -file option turns "<folder>\Name.map" into "<folder>\Name\Name.map"
// (CommandLine.cpp, ConvertShortMapPathToLongMapPath): a map must lie in a folder of its own name,
// and the option names it without that folder.

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace MapEditorCore
{

// The -file value for a map file; nullopt if the map does not lie in a folder of its name.
[[nodiscard]] std::optional<std::string> gameFileArgument(const std::string &mapPath);
// The game's command line for a map: always in a window.
[[nodiscard]] std::vector<std::string> gameArguments(const std::string &fileArgument);

} // namespace MapEditorCore
