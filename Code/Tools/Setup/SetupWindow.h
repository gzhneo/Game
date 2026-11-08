// Copyright (C) 2026 GZH Neo
// Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
// (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.

// The setup window: shows the installations found, lets the player pick other folders, checks the free space and
// copies the game data with progress and cancel. German or English after the Windows display language.

#pragma once

#include <filesystem>

namespace gzhsetup
{

// Returns the exit code: 0 set up, 1 error or closed before, 2 cancelled.
int runSetupWindow(const std::filesystem::path& target);

} // namespace gzhsetup
