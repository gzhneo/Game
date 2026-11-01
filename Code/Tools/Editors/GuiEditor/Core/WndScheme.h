/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WndScheme.h -- the layout schemes (.ls) of the old GUIEdit: an image, a colour and a border colour for every
// state of every window type, default text colours and a default font. Applying a scheme writes the states into
// the draw data of the windows (WndSchemeTable.h says where); the text colours and the font are for new windows,
// as in GUIEdit. Without the engine and Qt.

#pragma once

#include "Model/WndLayout.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace WndModel
{

struct Scheme
{
	Color enabledText, enabledTextBorder;
	Color disabledText, disabledTextBorder;
	Color hiliteText, hiliteTextBorder;
	std::optional<Font> font;			// none: "None" in the file
	std::vector<DrawEntry> states;		// one per SCHEME_STATES entry; "NoImage" is NONE in the file
};

[[nodiscard]] std::size_t schemeStateCount();
// The name of a state (BUTTON_ENABLED, ...), empty past the end.
[[nodiscard]] std::string_view schemeStateName(std::size_t state);

// A scheme with every state NoImage and the colours of a window the game leaves undrawn.
[[nodiscard]] Scheme emptyScheme();
// Reads a scheme file of version 1 (the only one GUIEdit writes); none for other text.
[[nodiscard]] std::optional<Scheme> readScheme(std::string_view text);
// Writes the scheme as GUIEdit does.
[[nodiscard]] std::string writeScheme(const Scheme &scheme);

// Writes the states into the draw data of the window and of the windows inside it, but not into the children
// of a gadget (they belong to the gadget). A missing ENABLEDDRAWDATA, DISABLEDDRAWDATA or HILITEDRAWDATA is
// added; draw data of a gadget's parts (scroll bar, thumb, ...) only where the window has it. Returns the
// number of windows that changed.
int applyScheme(const Scheme &scheme, Window &window);
// The scheme's text colours and font for a new window.
void applySchemeText(const Scheme &scheme, Window &window);

// The scheme a layout uses: every state from the first window of its type that has the draw data, the
// others, the text colours and the font from base unless a window has them.
[[nodiscard]] Scheme schemeOf(const Layout &layout, Scheme base);

} // namespace WndModel
