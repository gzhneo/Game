/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Common.h -- values and conversions the editor's windows share: map units, angles, the neutral
// team, file names and texts between Qt and the core.

#pragma once

#include <QString>

#include <filesystem>
#include <string>

namespace MapEditorApp
{

inline constexpr float CELL = 10.0f;					// world units per height sample
inline constexpr float HEIGHT_SCALE = CELL / 16.0f;		// world units per height step
inline constexpr float PI = 3.14159265358979323846f;
inline constexpr double RAD_TO_DEG = 57.29577951308232;
// Owner the game gives objects of no player.
inline constexpr const char *NEUTRAL_TEAM = "team";

// Names in the map (templates, teams, scripts) are Latin-1 in the game.
[[nodiscard]] std::string ascii(const QString &s);
[[nodiscard]] QString latin(const std::string &s);
// A path the standard library opens with every character (Windows: wide).
[[nodiscard]] std::filesystem::path toFsPath(const QString &path);
// A whole text file; empty if it cannot be read.
[[nodiscard]] std::string readText(const QString &path);

} // namespace MapEditorApp
