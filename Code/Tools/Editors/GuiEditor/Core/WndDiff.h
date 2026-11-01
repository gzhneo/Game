/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WndDiff.h -- what differs between two window layouts. Windows are matched by their name without the file in
// front (so a renamed file still compares), the n-th window of a name with the n-th of the other layout. Field
// values compare without regard to whitespace, so text that is only laid out differently is no change. Without
// the engine and Qt.

#pragma once

#include "Model/WndLayout.h"

#include <string>
#include <vector>

namespace WndModel
{

struct FieldDiff
{
	std::string key;		// the field; "parent" for a window that moved to another parent
	std::string before;		// empty if the field is new
	std::string after;		// empty if the field is gone
};

struct WindowDiff
{
	enum class Kind
	{
		Added,
		Removed,
		Changed,
	};

	Kind kind = Kind::Changed;
	std::string name;				// without the file; empty for the layout block
	std::vector<FieldDiff> fields;	// Changed only
};

// From a to b: first the layout block and the windows of b in file order (added or changed), then the
// windows only a has, in its order.
[[nodiscard]] std::vector<WindowDiff> diffLayouts(const Layout &a, const Layout &b);

} // namespace WndModel
