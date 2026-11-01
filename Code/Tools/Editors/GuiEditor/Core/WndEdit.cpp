/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WndEdit.cpp -- changes to a window layout.

#include "Core/WndEdit.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace WndModel
{

namespace
{

constexpr int INDENT = 2;		// spaces per level, as GUIEdit writes
constexpr int DRAW_ENTRIES = 9;	// MAX_DRAW_DATA
constexpr int TAB_PANES = 8;		// NUM_TAB_PANES
constexpr int TAB_WIDTH = 60;		// a new tab control's tabs, and the border around its panes
constexpr int TAB_HEIGHT = 20;
constexpr int PANE_BORDER = 4;

bool isWindow(const Entry &e)
{
	return e.kind == Entry::Kind::Window && !e.window.empty();
}

bool isWord(const Entry &e, std::string_view word)
{
	return e.kind == Entry::Kind::Word && e.key == word;
}

// Index in the entries of window number index, entries.size() if there are fewer.
std::size_t entryOf(const std::vector<Entry> &entries, std::size_t index)
{
	std::size_t n = 0;
	for (std::size_t i = 0; i < entries.size(); ++i)
		if (isWindow(entries[i]) && n++ == index)
			return i;
	return entries.size();
}

std::size_t windowCount(const std::vector<Entry> &entries)
{
	return static_cast<std::size_t>(std::count_if(entries.begin(), entries.end(), isWindow));
}

// The entries the children of the parent live in: the layout's for the top level.
std::vector<Entry> *containerOf(Layout &layout, const WindowPath &parent)
{
	if (parent.empty())
		return &layout.entries;
	Window *w = windowAt(layout, parent);
	return w ? &w->entries : nullptr;
}

std::string newlineOf(const Layout &layout)
{
	std::string found;
	const auto search = [&](const auto &self, const std::vector<Entry> &entries) -> bool {
		for (const Entry &e : entries)
		{
			if (e.trivia.find('\n') != std::string::npos)
			{
				found = e.trivia.find("\r\n") != std::string::npos ? "\r\n" : "\n";
				return true;
			}
			if (isWindow(e) && self(self, e.window.front().entries))
				return true;
		}
		return false;
	};
	search(search, layout.entries);
	return found.empty() ? std::string("\r\n") : found;
}

// The spaces after the last newline of a text, -1 if it has no newline.
int lastIndent(const std::string &s)
{
	const std::size_t nl = s.rfind('\n');
	if (nl == std::string::npos)
		return -1;
	int n = 0;
	for (std::size_t i = nl + 1; i < s.size() && s[i] == ' '; ++i)
		++n;
	return n;
}

// Changes the spaces after every newline by delta (at most down to none).
std::string shiftLines(const std::string &s, int delta)
{
	if (delta == 0 || s.find('\n') == std::string::npos)
		return s;
	std::string out;
	out.reserve(s.size() + 16);
	std::size_t i = 0;
	while (i < s.size())
	{
		out += s[i];
		if (s[i++] != '\n')
			continue;
		std::size_t spaces = 0;
		while (i + spaces < s.size() && s[i + spaces] == ' ')
			++spaces;
		const int now = std::max(0, static_cast<int>(spaces) + delta);
		out.append(static_cast<std::size_t>(now), ' ');
		i += spaces;
	}
	return out;
}

void shiftWindow(Window &w, int delta)
{
	for (Entry &e : w.entries)
	{
		e.trivia = shiftLines(e.trivia, delta);
		if (isWindow(e))
			shiftWindow(e.window.front(), delta);
		else
			e.text = shiftLines(e.text, delta);
	}
	w.endTrivia = shiftLines(w.endTrivia, delta);
}

std::string convertNewlines(const std::string &s, const std::string &newline)
{
	std::string out;
	out.reserve(s.size());
	for (std::size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n')
			continue;
		if (s[i] == '\n')
			out += newline;
		else
			out += s[i];
	}
	return out;
}

void setNewlines(Window &w, const std::string &newline)
{
	for (Entry &e : w.entries)
	{
		e.trivia = convertNewlines(e.trivia, newline);
		if (isWindow(e))
			setNewlines(e.window.front(), newline);
		else
			e.text = convertNewlines(e.text, newline);
	}
	w.endTrivia = convertNewlines(w.endTrivia, newline);
}

// The indent of the window's own WINDOW line: its END line has it.
int windowIndent(const Window &w)
{
	const int end = lastIndent(w.endTrivia);
	if (end >= 0)
		return end;
	for (const Entry &e : w.entries)
		if (e.kind == Entry::Kind::Field)
			return std::max(0, lastIndent(e.trivia) - INDENT);
	return 0;
}

// Fits the window for the place: its lines indented for the level, the newlines of the layout.
Entry placed(Window window, int indent, const std::string &newline)
{
	shiftWindow(window, indent - windowIndent(window));
	setNewlines(window, newline);
	Entry e;
	e.kind = Entry::Kind::Window;
	e.key = "WINDOW";
	e.trivia = newline + std::string(static_cast<std::size_t>(indent), ' ');
	e.window.push_back(std::move(window));
	return e;
}

Entry word(std::string_view w, const std::string &trivia)
{
	Entry e;
	e.kind = Entry::Kind::Word;
	e.key = std::string(w);
	e.text = std::string(w);
	e.trivia = trivia;
	return e;
}

// True if every child window has its own CHILD in front, as GUIEdit writes.
bool childBeforeEach(const std::vector<Entry> &entries)
{
	for (std::size_t i = 0; i < entries.size(); ++i)
		if (isWindow(entries[i]) && (i == 0 || !isWord(entries[i - 1], "CHILD")))
			return false;
	return true;
}

std::string colorText(int r, int g, int b, int a)
{
	return std::to_string(r) + ' ' + std::to_string(g) + ' ' + std::to_string(b) + ' ' + std::to_string(a);
}

std::string drawData(std::string_view key, const std::string &color, const std::string &border)
{
	const std::string pad(static_cast<std::size_t>(INDENT) + key.size() + 3, ' ');
	std::string out = "  " + std::string(key) + " = ";
	for (int i = 0; i < DRAW_ENTRIES; ++i)
	{
		if (i > 0)
			out += ",\n" + pad;
		out += "IMAGE: NoImage, COLOR: " + color + ", BORDERCOLOR: " + border;
	}
	return out + ";\n";
}

// ENABLED, DISABLED and HILITE draw data of a part, with the state word inside the key (%s).
std::string partDrawData(std::string_view pattern, const std::string &color, const std::string &border)
{
	std::string out;
	for (const char *state : {"ENABLED", "DISABLED", "HILITE"})
	{
		std::string key(pattern);
		key.replace(key.find("%s"), 2, state);
		out += drawData(key, color, border);
	}
	return out;
}

std::string numberless(std::string_view name)
{
	std::size_t end = name.size();
	while (end > 0 && std::isdigit(static_cast<unsigned char>(name[end - 1])))
		--end;
	return std::string(name.substr(0, end));
}

void collectNames(const Window &w, std::set<std::string> &names)
{
	names.insert(w.fullName());
	for (const Window *c : w.children())
		collectNames(*c, names);
}

void renameAll(Window &w, std::string_view file, std::set<std::string> &taken)
{
	std::string base = w.name();
	if (base.empty())
		base = "Window";
	std::string name = base;
	if (taken.count(std::string(file) + ":" + name))
	{
		const std::string stem = numberless(base);
		for (int n = 1;; ++n)
		{
			name = stem + std::to_string(n);
			if (!taken.count(std::string(file) + ":" + name))
				break;
		}
	}
	taken.insert(std::string(file) + ":" + name);
	if (w.fullName() != std::string(file) + ":" + name)
		w.setName(file, name);
	for (Window *c : w.children())
		renameAll(*c, file, taken);
}

} // namespace

Window *windowAt(Layout &layout, const WindowPath &path)
{
	return const_cast<Window *>(windowAt(static_cast<const Layout &>(layout), path));
}

const Window *windowAt(const Layout &layout, const WindowPath &path)
{
	if (path.empty())
		return nullptr;
	std::vector<const Window *> level = layout.windows();
	const Window *w = nullptr;
	for (const std::size_t index : path)
	{
		if (index >= level.size())
			return nullptr;
		w = level[index];
		level = w->children();
	}
	return w;
}

std::optional<WindowPath> findWindow(const Layout &layout, std::string_view fullName)
{
	WindowPath path;
	std::optional<WindowPath> found;
	const auto search = [&](const auto &self, const std::vector<const Window *> &level) -> void {
		for (std::size_t i = 0; i < level.size() && !found; ++i)
		{
			path.push_back(i);
			if (level[i]->fullName() == fullName)
				found = path;
			else
				self(self, level[i]->children());
			path.pop_back();
		}
	};
	search(search, layout.windows());
	return found;
}

bool contains(const WindowPath &a, const WindowPath &b)
{
	return b.size() >= a.size() && std::equal(a.begin(), a.end(), b.begin());
}

std::string layoutFile(const Layout &layout)
{
	for (const Window *w : layout.windows())
	{
		const std::string full = w->fullName();
		const std::size_t colon = full.find(':');
		if (colon != std::string::npos)
			return full.substr(0, colon);
	}
	return {};
}

void setLayoutFile(Layout &layout, std::string_view file)
{
	const auto rename = [&](const auto &self, Window &w) -> void {
		const std::string name = w.name();
		if (w.fullName() != std::string(file) + ":" + name)
			w.setName(file, name);
		for (Window *c : w.children())
			self(self, *c);
	};
	for (Window *w : layout.windows())
		rename(rename, *w);
}

std::vector<std::string> fullNames(const Layout &layout)
{
	std::vector<std::string> out;
	forEachWindow(layout, [&](const Window &w, int) { out.push_back(w.fullName()); });
	return out;
}

std::string uniqueName(const Layout &layout, std::string_view file, std::string_view name)
{
	const std::vector<std::string> all = fullNames(layout);
	const std::set<std::string> taken(all.begin(), all.end());
	const std::string prefix = std::string(file) + ":";
	if (!name.empty() && !taken.count(prefix + std::string(name)))
		return std::string(name);
	const std::string stem = name.empty() ? std::string("Window") : numberless(name);
	for (int n = 1;; ++n)
		if (!taken.count(prefix + stem + std::to_string(n)))
			return stem + std::to_string(n);
}

Rect defaultRect(std::string_view type, int left, int top)
{
	int w = 200, h = 150;
	if (type == "PUSHBUTTON")
		w = 120, h = 30;
	else if (type == "CHECKBOX" || type == "RADIOBUTTON" || type == "STATICTEXT")
		w = 120, h = 20;
	else if (type == "COMBOBOX" || type == "ENTRYFIELD")
		w = 160, h = 24;
	else if (type == "HORZSLIDER" || type == "PROGRESSBAR")
		w = 160, h = 20;
	else if (type == "VERTSLIDER")
		w = 20, h = 160;
	else if (type == "TABCONTROL")
		w = 300, h = 200;
	Rect r;
	r.left = left;
	r.top = top;
	r.right = left + w;
	r.bottom = top + h;
	return r;
}

bool canHaveChildren(const Window &window)
{
	const std::string type = window.type();
	return type == "USER" || type == "TABCONTROL";
}

Window makeWindow(std::string_view type, std::string_view file, std::string_view name, const Rect &rect)
{
	const std::string t(type);
	const bool text = t == "STATICTEXT";
	const std::string fill = text ? colorText(0, 0, 0, 0) : colorText(48, 52, 64, 255);
	const std::string edge = text ? colorText(0, 0, 0, 0) : colorText(150, 140, 96, 255);
	std::string s = "WINDOW\n";
	s += "  WINDOWTYPE = " + t + ";\n";
	s += "  SCREENRECT = " + formatRect(rect, INDENT) + ";\n";
	s += "  NAME = \"" + std::string(file) + ":" + std::string(name) + "\";\n";
	s += "  STATUS = ENABLED;\n";
	// Gadgets track the mouse as GUIEdit makes them: a check box or radio button takes a click only after
	// the mouse came in (GadgetCheckBox.cpp), a button lights up.
	s += "  STYLE = " + t + (t == "USER" ? std::string() : std::string("+MOUSETRACK")) + ";\n";
	s += "  SYSTEMCALLBACK = \"[None]\";\n  INPUTCALLBACK = \"[None]\";\n  TOOLTIPCALLBACK = \"[None]\";\n  DRAWCALLBACK = \"[None]\";\n";
	s += "  FONT = NAME: \"Arial\", SIZE: 10, BOLD: 0;\n";
	s += "  HEADERTEMPLATE = \"[None]\";\n";
	s += "  TOOLTIPDELAY = -1;\n";
	s += "  TEXTCOLOR = ENABLED:  255 255 255 255, ENABLEDBORDER:  0 0 0 255,\n"
		 "              DISABLED: 128 128 128 255, DISABLEDBORDER: 0 0 0 255,\n"
		 "              HILITE:   255 230 150 255, HILITEBORDER:   0 0 0 255;\n";
	s += drawData("ENABLEDDRAWDATA", fill, edge);
	s += drawData("DISABLEDDRAWDATA", text ? fill : colorText(32, 34, 42, 255), text ? edge : colorText(90, 90, 90, 255));
	s += drawData("HILITEDRAWDATA", text ? fill : colorText(72, 78, 96, 255), text ? edge : colorText(230, 190, 90, 255));
	const std::string part = colorText(70, 76, 70, 255);
	const std::string partEdge = colorText(150, 140, 96, 255);
	const std::string listParts = partDrawData("LISTBOX%sUPBUTTONDRAWDATA", part, partEdge)
		+ partDrawData("LISTBOX%sDOWNBUTTONDRAWDATA", part, partEdge)
		+ partDrawData("LISTBOX%sSLIDERDRAWDATA", colorText(30, 32, 30, 255), colorText(90, 90, 80, 255))
		+ partDrawData("SLIDERTHUMB%sDRAWDATA", colorText(150, 140, 96, 255), colorText(40, 40, 30, 255));
	if (t == "SCROLLLISTBOX")
		s += "  LISTBOXDATA = LENGTH: 10, AUTOSCROLL: 0, SCROLLIFATEND: 0, AUTOPURGE: 0, SCROLLBAR: 1, MULTISELECT: 0, COLUMNS: 1, FORCESELECT: 0;\n"
			+ listParts;
	else if (t == "COMBOBOX")
		s += "  COMBOBOXDATA = ISEDITABLE: 0, MAXCHARS: 32, MAXDISPLAY: 6, ASCIIONLY: 0, LETTERSANDNUMBERS: 0;\n"
			+ partDrawData("COMBOBOXDROPDOWNBUTTON%sDRAWDATA", part, partEdge)
			+ partDrawData("COMBOBOXEDITBOX%sDRAWDATA", colorText(16, 18, 16, 240), partEdge)
			+ partDrawData("COMBOBOXLISTBOX%sDRAWDATA", colorText(20, 22, 20, 245), partEdge) + listParts;
	else if (t == "HORZSLIDER" || t == "VERTSLIDER")
		s += "  SLIDERDATA = MINVALUE: 0, MAXVALUE: 100;\n"
			+ partDrawData("SLIDERTHUMB%sDRAWDATA", colorText(150, 140, 96, 255), colorText(40, 40, 30, 255));
	else if (t == "RADIOBUTTON")
		s += "  RADIOBUTTONDATA = GROUP: 0;\n";
	else if (t == "ENTRYFIELD")
		s += "  TEXTENTRYDATA = MAXLEN: 64, SECRETTEXT: 0, NUMERICALONLY: 0, ALPHANUMERICALONLY: 0, ASCIIONLY: 0;\n";
	else if (t == "STATICTEXT")
		s += "  STATICTEXTDATA = CENTERED: 0;\n";
	else if (t == "TABCONTROL")
		s += "  TABCONTROLDATA = TABORIENTATION: 1, TABEDGE: 3, TABWIDTH: " + std::to_string(TAB_WIDTH) + ", TABHEIGHT: "
			+ std::to_string(TAB_HEIGHT) + ", TABCOUNT: 2, PANEBORDER: " + std::to_string(PANE_BORDER)
			+ ", PANEDISABLED: 8,0,0,0,0,0,0,0,0;\n";
	s += "END";
	Layout l = read(s);
	std::vector<Window *> windows = l.windows();
	if (windows.empty())
		return Window{};
	if (t == "TABCONTROL")
	{
		// The game makes eight panes for a tab control and keeps the ones of the file instead (in file order);
		// a pane lies below the tabs, inside the border (GadgetTabControlComputeSubPaneSize, tabs on top).
		Rect pane;
		pane.left = rect.left + PANE_BORDER;
		pane.top = rect.top + PANE_BORDER + TAB_HEIGHT;
		pane.right = rect.right - PANE_BORDER;
		pane.bottom = rect.bottom - PANE_BORDER;
		pane.creationWidth = rect.creationWidth;
		pane.creationHeight = rect.creationHeight;
		for (int i = 0; i < TAB_PANES; ++i)
		{
			std::string p = "WINDOW\n  WINDOWTYPE = TABPANE;\n";
			p += "  SCREENRECT = " + formatRect(pane, INDENT) + ";\n";
			p += "  NAME = \"" + std::string(file) + ":" + std::string(name) + "Pane" + std::to_string(i) + "\";\n";
			p += i == 0 ? "  STATUS = ENABLED;\n" : "  STATUS = ENABLED+HIDDEN;\n";
			p += "  STYLE = TABPANE;\n";
			p += "  SYSTEMCALLBACK = \"PassSelectedButtonsToParentSystem\";\n  INPUTCALLBACK = \"[None]\";\n"
				 "  TOOLTIPCALLBACK = \"[None]\";\n  DRAWCALLBACK = \"[None]\";\n";
			p += "  FONT = NAME: \"Arial\", SIZE: 10, BOLD: 0;\n  HEADERTEMPLATE = \"[None]\";\n  TOOLTIPDELAY = -1;\n";
			const std::string clear = colorText(0, 0, 0, 0);
			p += drawData("ENABLEDDRAWDATA", clear, clear) + drawData("DISABLEDDRAWDATA", clear, clear) + drawData("HILITEDRAWDATA", clear, clear);
			p += "END";
			Layout pl = read(p);
			insertWindow(l, {0}, static_cast<std::size_t>(i), std::move(*pl.windows().front()));
		}
	}
	return std::move(*l.windows().front());
}

WindowPath insertWindow(Layout &layout, const WindowPath &parent, std::size_t index, Window window)
{
	std::vector<Entry> *entries = containerOf(layout, parent);
	if (!entries)
		return {};
	const std::string newline = newlineOf(layout);
	const std::size_t count = windowCount(*entries);
	index = std::min(index, count);
	WindowPath path = parent;
	path.push_back(index);
	if (parent.empty())
	{
		// Top level: before the window that has this place now, otherwise behind the last window.
		std::size_t at = entryOf(*entries, index);
		if (at == entries->size() && count > 0)
			at = entryOf(*entries, count - 1) + 1;
		entries->insert(entries->begin() + static_cast<std::ptrdiff_t>(at), placed(std::move(window), 0, newline));
		return path;
	}
	Window *owner = windowAt(layout, parent);
	const int indent = windowIndent(*owner) + INDENT;
	const std::string fieldTrivia = newline + std::string(static_cast<std::size_t>(indent), ' ');
	Entry entry = placed(std::move(window), indent, newline);
	if (count == 0)
	{
		entries->push_back(word("CHILD", fieldTrivia));
		entries->push_back(std::move(entry));
		entries->push_back(word("ENDALLCHILDREN", fieldTrivia));
		return path;
	}
	const bool own = childBeforeEach(*entries);
	std::size_t at = 0;
	if (index < count)
	{
		at = entryOf(*entries, index);
		// With one CHILD in front of all children a new first child goes behind it; with one per child in front.
		if (own && at > 0 && isWord((*entries)[at - 1], "CHILD"))
			--at;
	}
	else
	{
		at = entryOf(*entries, count - 1) + 1;
		while (at < entries->size() && !isWord((*entries)[at], "ENDALLCHILDREN"))
			++at;
		if (at == entries->size())
			at = entryOf(*entries, count - 1) + 1;
	}
	if (own)
	{
		entries->insert(entries->begin() + static_cast<std::ptrdiff_t>(at), std::move(entry));
		entries->insert(entries->begin() + static_cast<std::ptrdiff_t>(at), word("CHILD", fieldTrivia));
	}
	else
		entries->insert(entries->begin() + static_cast<std::ptrdiff_t>(at), std::move(entry));
	return path;
}

std::optional<Window> removeWindow(Layout &layout, const WindowPath &path)
{
	if (path.empty())
		return std::nullopt;
	const WindowPath parent(path.begin(), path.end() - 1);
	std::vector<Entry> *entries = containerOf(layout, parent);
	if (!entries)
		return std::nullopt;
	const std::size_t at = entryOf(*entries, path.back());
	if (at >= entries->size())
		return std::nullopt;
	Window removed = std::move((*entries)[at].window.front());
	entries->erase(entries->begin() + static_cast<std::ptrdiff_t>(at));
	if (!parent.empty())
	{
		// The CHILD of this window goes with it, unless it also stands for the windows behind it.
		if (at > 0 && isWord((*entries)[at - 1], "CHILD") && (at == entries->size() || !isWindow((*entries)[at])))
			entries->erase(entries->begin() + static_cast<std::ptrdiff_t>(at - 1));
		if (windowCount(*entries) == 0)
			entries->erase(std::remove_if(entries->begin(), entries->end(),
				[](const Entry &e) { return isWord(e, "CHILD") || isWord(e, "ENDALLCHILDREN"); }), entries->end());
	}
	return removed;
}

std::optional<WindowPath> moveWindow(Layout &layout, const WindowPath &from, const WindowPath &parent, std::size_t index)
{
	if (from.empty() || contains(from, parent) || !windowAt(layout, from) || (!parent.empty() && !windowAt(layout, parent)))
		return std::nullopt;
	WindowPath target = parent;
	// Removing the window shifts a later sibling on its level that lies on the way to the parent.
	const std::size_t level = from.size() - 1;
	if (target.size() > level && std::equal(from.begin(), from.begin() + static_cast<std::ptrdiff_t>(level), target.begin())
		&& target[level] > from[level])
		--target[level];
	std::optional<Window> window = removeWindow(layout, from);
	if (!window)
		return std::nullopt;
	return insertWindow(layout, target, index, std::move(*window));
}

void offsetWindow(Window &window, int dx, int dy)
{
	if (dx == 0 && dy == 0)
		return;
	if (std::optional<Rect> r = window.rect())
	{
		r->left += dx;
		r->right += dx;
		r->top += dy;
		r->bottom += dy;
		window.setRect(*r);
	}
	for (Window *c : window.children())
		offsetWindow(*c, dx, dy);
}

std::string windowsText(const std::vector<const Window *> &windows)
{
	Layout l;
	for (const Window *w : windows)
	{
		Entry e = placed(*w, 0, "\r\n");
		if (l.entries.empty())
			e.trivia.clear();
		l.entries.push_back(std::move(e));
	}
	l.tail = "\r\n";
	return write(l);
}

std::vector<Window> readWindows(std::string_view text)
{
	std::vector<Window> out;
	Layout l = read(text);
	for (Window *w : l.windows())
		if (!w->type().empty())
			out.push_back(std::move(*w));
	return out;
}

void makeNamesUnique(const Layout &layout, std::string_view file, Window &window)
{
	std::set<std::string> taken;
	for (const Window *w : layout.windows())
		collectNames(*w, taken);
	renameAll(window, file, taken);
}

void setLayoutValue(Layout &layout, std::string_view key, std::string_view value)
{
	const std::string text = std::string(key) + " = " + std::string(value) + ";";
	for (Entry &e : layout.entries)
		if (e.kind == Entry::Kind::Field && e.key == key)
		{
			e.text = text;
			return;
		}
	Entry field;
	field.kind = Entry::Kind::Field;
	field.key = std::string(key);
	field.text = text;
	field.trivia = newlineOf(layout) + std::string(static_cast<std::size_t>(INDENT), ' ');
	const auto end = std::find_if(layout.entries.begin(), layout.entries.end(), [](const Entry &e) { return isWord(e, "ENDLAYOUTBLOCK"); });
	layout.entries.insert(end, std::move(field));
}

Layout newLayout()
{
	return read("FILE_VERSION = 2;\r\nSTARTLAYOUTBLOCK\r\n  LAYOUTINIT = [None];\r\n  LAYOUTUPDATE = [None];\r\n"
				"  LAYOUTSHUTDOWN = [None];\r\nENDLAYOUTBLOCK\r\n");
}

} // namespace WndModel
