/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WndEdit.h -- changes to a window layout: find, create, remove, move in the tree, move on screen, names,
// copy and paste. Without the engine and Qt.
//
// The game's reader decides the shape: every field of a window comes before its children, the children
// stand between CHILD and ENDALLCHILDREN, and a later window is drawn above an earlier one. SCREENRECT is
// absolute, so moving a window moves its children by the same amount. New text is written as the old
// GUIEdit writes it (Save.cpp): CHILD in front of every child, two spaces of indent per level.

#pragma once

#include "Model/WndLayout.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace WndModel
{

// A window by its position: the index among the top-level windows, then among the children of each level.
using WindowPath = std::vector<std::size_t>;

[[nodiscard]] Window *windowAt(Layout &layout, const WindowPath &path);
[[nodiscard]] const Window *windowAt(const Layout &layout, const WindowPath &path);
// The first window with this NAME (as written, with the file in front).
[[nodiscard]] std::optional<WindowPath> findWindow(const Layout &layout, std::string_view fullName);
// True if b is a or lies inside a.
[[nodiscard]] bool contains(const WindowPath &a, const WindowPath &b);

// The file in front of the names ("MainMenu.wnd"), taken from the first window; empty if there is none.
[[nodiscard]] std::string layoutFile(const Layout &layout);
// Writes the file in front of every NAME.
void setLayoutFile(Layout &layout, std::string_view file);
// NAME as written of every window.
[[nodiscard]] std::vector<std::string> fullNames(const Layout &layout);
// The name itself if no window has it, otherwise the name with the lowest free number behind it.
[[nodiscard]] std::string uniqueName(const Layout &layout, std::string_view file, std::string_view name);

// A new window of the type (WINDOWTYPE) as GUIEdit writes it, with visible default colours and no TEXT (the
// game reads TEXT as a key of its string table).
[[nodiscard]] Window makeWindow(std::string_view type, std::string_view file, std::string_view name, const Rect &rect);
// The default size of a new window of the type, in layout pixels.
[[nodiscard]] Rect defaultRect(std::string_view type, int left, int top);
// True for the types that hold child windows in the file (user windows and tab controls).
[[nodiscard]] bool canHaveChildren(const Window &window);

// Inserts the window as child number index of the parent (top level for an empty parent path); index past
// the end appends. Returns the path of the inserted window.
WindowPath insertWindow(Layout &layout, const WindowPath &parent, std::size_t index, Window window);
// Removes the window with its children and returns it.
std::optional<Window> removeWindow(Layout &layout, const WindowPath &path);
// Moves the window into another parent or to another place among its siblings (index counted after the
// removal); a window cannot go into itself. Returns the new path.
std::optional<WindowPath> moveWindow(Layout &layout, const WindowPath &from, const WindowPath &parent, std::size_t index);

// Moves the window and all its children on the screen.
void offsetWindow(Window &window, int dx, int dy);

// The windows as text, for the clipboard; readWindows reads it back (other text gives no windows).
[[nodiscard]] std::string windowsText(const std::vector<const Window *> &windows);
[[nodiscard]] std::vector<Window> readWindows(std::string_view text);
// Gives the window and its children names no window of the layout has, with the file in front.
void makeNamesUnique(const Layout &layout, std::string_view file, Window &window);

// Sets LAYOUTINIT, LAYOUTUPDATE or LAYOUTSHUTDOWN (inside the layout block; added there if missing).
void setLayoutValue(Layout &layout, std::string_view key, std::string_view value);

// An empty layout: version, layout block, no windows.
[[nodiscard]] Layout newLayout();

} // namespace WndModel
