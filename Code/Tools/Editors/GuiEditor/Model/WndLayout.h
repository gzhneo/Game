/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WndLayout.h -- a window layout (.wnd) as the GUI editor holds it, without the engine.
//
// The layout is read with the rules of the game's reader (GameWindowManagerScript.cpp): words are separated
// by whitespace, a field runs from its keyword to the first ';', WINDOW ... END is a window, CHILD and
// ENDALLCHILDREN are single words. Every entry keeps the whitespace in front of it and its exact text, so a
// layout written unchanged is the same file byte for byte, and a field the editor does not know stays as
// it is. A changed field is written in the style of the old GUIEdit (Save.cpp).

#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace WndModel
{

struct Window;

struct Entry
{
	enum class Kind
	{
		Field,		// KEY = value;  (also unknown keywords: the reader skips them to the ';')
		Word,		// CHILD, ENDALLCHILDREN, STARTLAYOUTBLOCK, ENDLAYOUTBLOCK, END outside a window
		Window,		// WINDOW ... END
	};

	Kind kind = Kind::Field;
	std::string trivia;				// whitespace in front of the entry, as in the file
	std::string text;				// Field, Word: the exact text (a field with its ';')
	std::string key;				// Field, Word: the first word
	std::vector<Window> window;		// Window: exactly one

	// Field: the text between '=' and ';', without the whitespace around it.
	[[nodiscard]] std::string value() const;
};

struct Rect
{
	int left = 0, top = 0, right = 0, bottom = 0;		// UPPERLEFT and BOTTOMRIGHT
	int creationWidth = 800, creationHeight = 600;		// CREATIONRESOLUTION
};

struct Color
{
	int r = 0, g = 0, b = 0, a = 0;
	bool operator==(const Color &) const = default;
};

// One of the nine entries of a DRAWDATA field.
struct DrawEntry
{
	std::string image = "NoImage";
	Color color;
	Color border;
	bool operator==(const DrawEntry &) const = default;
};

struct Font
{
	std::string name;
	int size = 0;
	int bold = 0;
	bool operator==(const Font &) const = default;
};

struct Window
{
	std::string opening = "WINDOW";	// the opening word as in the file
	std::vector<Entry> entries;		// fields, words and child windows in file order
	std::string endTrivia;			// whitespace in front of END
	bool ended = true;				// false: the file ended inside the window (the reader stops there)

	// The field with this key, null if there is none.
	[[nodiscard]] const Entry *field(std::string_view key) const;
	[[nodiscard]] Entry *field(std::string_view key);
	// The value of the field, empty if there is none.
	[[nodiscard]] std::string value(std::string_view key) const;
	// Sets the field's value ("KEY = value;"); a missing field is added after the last field before
	// the children, with the indent of the window's fields.
	void setValue(std::string_view key, std::string_view value);
	bool removeField(std::string_view key);

	[[nodiscard]] std::vector<const Window *> children() const;
	[[nodiscard]] std::vector<Window *> children();

	// WINDOWTYPE: USER, PUSHBUTTON, CHECKBOX, RADIOBUTTON, ...
	[[nodiscard]] std::string type() const { return value("WINDOWTYPE"); }
	// NAME without the "file.wnd:" in front.
	[[nodiscard]] std::string name() const;
	// NAME as written, with the file in front.
	[[nodiscard]] std::string fullName() const;
	void setName(std::string_view file, std::string_view name);
	[[nodiscard]] std::optional<Rect> rect() const;
	void setRect(const Rect &rect);
	// STATUS and STYLE as their words (ENABLED, HIDDEN, ...); NULL gives none.
	[[nodiscard]] std::vector<std::string> status() const;
	void setStatus(const std::vector<std::string> &words);
	[[nodiscard]] bool hasStatus(std::string_view word) const;
	[[nodiscard]] std::string text() const;						// TEXT, without the quotes
	[[nodiscard]] std::string callback(std::string_view key) const;	// SYSTEMCALLBACK etc., without the quotes
	[[nodiscard]] std::optional<Font> font() const;
	// The nine entries of ENABLEDDRAWDATA, DISABLEDDRAWDATA, ... (fewer if the field has fewer).
	[[nodiscard]] std::vector<DrawEntry> drawData(std::string_view key) const;
	void setDrawData(std::string_view key, const std::vector<DrawEntry> &entries);
	[[nodiscard]] bool isGadget() const;
	// The pairs of a data field ("KEY: value, KEY: value": LISTBOXDATA, SLIDERDATA, TEXTCOLOR, ...), in file
	// order; a value can hold commas (PANEDISABLED: 8,0,0,...), a key can repeat (COLUMNSWIDTH%).
	[[nodiscard]] std::vector<std::pair<std::string, std::string>> dataFields(std::string_view key) const;
	void setDataFields(std::string_view key, const std::vector<std::pair<std::string, std::string>> &pairs);
};

struct Layout
{
	std::vector<Entry> entries;		// FILE_VERSION, the layout block, defaults, windows, in file order
	std::string tail;				// whitespace after the last entry

	[[nodiscard]] int version() const;			// FILE_VERSION, 0 if missing
	// LAYOUTINIT, LAYOUTUPDATE, LAYOUTSHUTDOWN ("[None]" if missing).
	[[nodiscard]] std::string layoutCallback(std::string_view key) const;
	[[nodiscard]] std::vector<const Window *> windows() const;
	[[nodiscard]] std::vector<Window *> windows();
};

// Reads a layout; never fails (whatever the game's reader would skip stays as an entry).
[[nodiscard]] Layout read(std::string_view text);
// Writes the layout: the input of read() again if nothing changed.
[[nodiscard]] std::string write(const Layout &layout);

// Calls f for every window, parents before children, with the depth (0 for top-level windows).
template <class F>
void forEachWindow(const Window &window, F &&f, int depth = 0)
{
	f(window, depth);
	for (const Window *child : window.children())
		forEachWindow(*child, f, depth + 1);
}
template <class F>
void forEachWindow(const Layout &layout, F &&f)
{
	for (const Window *window : layout.windows())
		forEachWindow(*window, f, 0);
}

// The status and style words of the game (WindowStatusNames, WindowStyleNames).
[[nodiscard]] const std::vector<std::string> &statusWords();
[[nodiscard]] const std::vector<std::string> &styleWords();

// Values in the format of the old GUIEdit (Save.cpp).
[[nodiscard]] std::string formatRect(const Rect &rect, int indent);
[[nodiscard]] std::string formatDrawData(std::string_view key, const std::vector<DrawEntry> &entries, int indent);

} // namespace WndModel
