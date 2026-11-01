/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WndCheck.h -- what the game's layout reader would refuse, skip or misread in a layout, and what the
// old GUIEdit refused to save (names too long or used twice).

#pragma once

#include "Model/WndLayout.h"

#include <string>
#include <vector>

namespace WndModel
{

// The game's limits (GameWindowManagerScript.cpp, WinInstanceData.h).
constexpr int MAX_NESTING = 10;			// WIN_STACK_DEPTH: deeper children end the reading
constexpr int MAX_NAME_LENGTH = 64;		// MAX_WINDOW_NAME_LEN, "file.wnd:" included

struct Problem
{
	enum class Severity
	{
		Error,		// the reader stops, or the window comes out different from what the file says
		Warning,	// the reader skips something
	};
	Severity severity = Severity::Error;
	std::string window;		// name of the window (empty for the file)
	std::string text;
};

// The window types the reader creates (WINDOWTYPE), as the old GUIEdit writes them.
[[nodiscard]] const std::vector<std::string> &windowTypes();
// Fields the reader knows inside a window (gameWindowFieldList, plus DATA).
[[nodiscard]] bool knownWindowField(std::string_view key);

[[nodiscard]] std::vector<Problem> check(const Layout &layout);

} // namespace WndModel
