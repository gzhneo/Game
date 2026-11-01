/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WndScheme.cpp -- the layout schemes of the old GUIEdit.

#include "Core/WndScheme.h"

#include "Core/WndSchemeTable.h"

#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <utility>

namespace WndModel
{

namespace
{

constexpr std::size_t DRAW_ENTRIES = 9;					// MAX_DRAW_DATA
constexpr Color UNDEFINED = {255, 255, 255, 0};			// WIN_COLOR_UNDEFINED: the game does not draw it
constexpr const char *BASE_FIELDS[] = {"ENABLEDDRAWDATA", "DISABLEDDRAWDATA", "HILITEDRAWDATA"};

std::string_view trim(std::string_view s)
{
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
		s.remove_prefix(1);
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
		s.remove_suffix(1);
	return s;
}

bool startsWith(std::string_view s, std::string_view prefix)
{
	return s.substr(0, prefix.size()) == prefix;
}

// The integers in the text, in order (a '-' in front counts).
std::vector<int> numbers(std::string_view s)
{
	std::vector<int> out;
	for (std::size_t i = 0; i < s.size(); ++i)
	{
		const bool minus = s[i] == '-' && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9';
		if (!minus && (s[i] < '0' || s[i] > '9'))
			continue;
		std::size_t end = i + 1;
		while (end < s.size() && s[end] >= '0' && s[end] <= '9')
			++end;
		out.push_back(std::atoi(std::string(s.substr(i, end - i)).c_str()));
		i = end - 1;
	}
	return out;
}

// "(r,g,b,a)" after the prefix; colours keep 0..255 as GUIEdit's bytes do.
std::optional<Color> colorAfter(std::string_view line, std::string_view prefix)
{
	if (!startsWith(line, prefix))
		return std::nullopt;
	const std::vector<int> n = numbers(line.substr(prefix.size()));
	if (n.size() < 4)
		return std::nullopt;
	return Color{n[0] & 255, n[1] & 255, n[2] & 255, n[3] & 255};
}

std::string colorText(const Color &c)
{
	char buffer[64];
	std::snprintf(buffer, sizeof buffer, "(%d,%d,%d,%d)", c.r, c.g, c.b, c.a);
	return buffer;
}

std::string wndColor(const Color &c)
{
	return std::to_string(c.r) + ' ' + std::to_string(c.g) + ' ' + std::to_string(c.b) + ' ' + std::to_string(c.a);
}

bool isBaseField(std::string_view field)
{
	for (const char *f : BASE_FIELDS)
		if (field == f)
			return true;
	return false;
}

// The type the table names for the window: its WINDOWTYPE, GENERIC for every window that is no gadget.
std::string schemeType(const Window &w)
{
	return w.isGadget() ? w.type() : std::string("GENERIC");
}

bool applyTo(const Scheme &scheme, Window &w)
{
	const std::string type = schemeType(w);
	bool changed = false;
	std::string field;
	std::vector<DrawEntry> entries;
	bool skip = false;
	const auto flush = [&] {
		if (!field.empty() && !skip && entries != w.drawData(field))
		{
			w.setDrawData(field, entries);
			changed = true;
		}
	};
	for (const SchemeSlot &slot : SCHEME_SLOTS)
	{
		if (type != slot.type || static_cast<std::size_t>(slot.state) >= scheme.states.size())
			continue;
		if (field != slot.field)
		{
			flush();
			field = slot.field;
			skip = !w.field(field) && !isBaseField(field);
			entries = w.drawData(field);
			if (entries.size() < DRAW_ENTRIES)
				entries.resize(DRAW_ENTRIES, DrawEntry{"NoImage", UNDEFINED, UNDEFINED});
		}
		if (skip || static_cast<std::size_t>(slot.index) >= entries.size())
			continue;
		const DrawEntry &from = scheme.states[static_cast<std::size_t>(slot.state)];
		DrawEntry &to = entries[static_cast<std::size_t>(slot.index)];
		if (slot.part == 'i')
			to.image = from.image;
		else if (slot.part == 'c')
			to.color = from.color;
		else
			to.border = from.border;
	}
	flush();
	return changed;
}

int applyTree(const Scheme &scheme, Window &w)
{
	int n = applyTo(scheme, w) ? 1 : 0;
	if (!w.isGadget())
		for (Window *child : w.children())
			n += applyTree(scheme, *child);
	return n;
}

// Takes the states the window has into the scheme, where taken does not yet mark them.
void takeFrom(const Window &w, Scheme &scheme, std::vector<char> &taken)
{
	const std::string type = schemeType(w);
	std::string field;
	std::vector<DrawEntry> entries;
	for (const SchemeSlot &slot : SCHEME_SLOTS)
	{
		const std::size_t state = static_cast<std::size_t>(slot.state);
		if (type != slot.type || state >= scheme.states.size() || taken[state] == 2)
			continue;
		if (field != slot.field)
		{
			field = slot.field;
			entries = w.drawData(field);
		}
		if (static_cast<std::size_t>(slot.index) >= entries.size())
			continue;
		const DrawEntry &from = entries[static_cast<std::size_t>(slot.index)];
		DrawEntry &to = scheme.states[state];
		if (slot.part == 'i')
			to.image = from.image;
		else if (slot.part == 'c')
			to.color = from.color;
		else
			to.border = from.border;
		taken[state] = 1;
	}
	// A state is done once one window gave it, so later windows of the type do not mix into it.
	for (char &t : taken)
		if (t == 1)
			t = 2;
}

void takeTree(const Window &w, Scheme &scheme, std::vector<char> &taken)
{
	takeFrom(w, scheme, taken);
	if (!w.isGadget())
		for (const Window *child : w.children())
			takeTree(*child, scheme, taken);
}

} // namespace

std::size_t schemeStateCount()
{
	return std::size(SCHEME_STATES);
}

std::string_view schemeStateName(std::size_t state)
{
	return state < schemeStateCount() ? SCHEME_STATES[state] : std::string_view();
}

Scheme emptyScheme()
{
	Scheme s;
	s.enabledText = s.disabledText = s.hiliteText = {255, 255, 255, 255};
	s.enabledTextBorder = s.disabledTextBorder = s.hiliteTextBorder = {0, 0, 0, 255};
	s.states.assign(schemeStateCount(), DrawEntry{"NoImage", UNDEFINED, UNDEFINED});
	return s;
}

std::optional<Scheme> readScheme(std::string_view text)
{
	Scheme s = emptyScheme();
	bool header = false;
	std::size_t start = 0;
	while (start < text.size())
	{
		std::size_t end = text.find('\n', start);
		if (end == std::string_view::npos)
			end = text.size();
		const std::string_view line = trim(text.substr(start, end - start));
		start = end + 1;
		if (line.empty())
			continue;
		if (!header)
		{
			const std::vector<int> v = numbers(line);
			if (!startsWith(line, "Window Layout Scheme: Version") || v.size() != 1 || v[0] != 1)
				return std::nullopt;
			header = true;
			continue;
		}
		// The longer prefix first: "Enabled Text Border:" also starts with "Enabled Text".
		static constexpr std::pair<const char *, Color Scheme::*> TEXT_COLORS[] = {
			{"Enabled Text Border:", &Scheme::enabledTextBorder}, {"Enabled Text:", &Scheme::enabledText},
			{"Disabled Text Border:", &Scheme::disabledTextBorder}, {"Disabled Text:", &Scheme::disabledText},
			{"Hilite Text Border:", &Scheme::hiliteTextBorder}, {"Hilite Text:", &Scheme::hiliteText}};
		bool textColor = false;
		for (const auto &[prefix, member] : TEXT_COLORS)
			if (const std::optional<Color> c = colorAfter(line, prefix); c && !textColor)
			{
				s.*member = *c;
				textColor = true;
			}
		if (textColor)
			continue;
		if (startsWith(line, "Default Font:"))
		{
			const std::size_t open = line.find('"');
			const std::size_t close = open == std::string_view::npos ? open : line.find('"', open + 1);
			if (close == std::string_view::npos)
				continue;
			Font f;
			f.name = std::string(line.substr(open + 1, close - open - 1));
			const std::vector<int> n = numbers(line.substr(close + 1));
			f.size = n.size() > 0 ? n[0] : 0;
			f.bold = n.size() > 1 ? n[1] : 0;
			if (f.name == "None")
				s.font.reset();
			else
				s.font = f;
		}
		else if (startsWith(line, "Number of states:"))
			continue;
		else
		{
			// "n: Image: NAME Color: (r,g,b,a) Border: (r,g,b,a)"
			const std::size_t image = line.find(": Image: ");
			const std::size_t color = line.find(" Color: ");
			const std::size_t border = line.find(" Border: ");
			if (image == std::string_view::npos || color == std::string_view::npos || border == std::string_view::npos)
				continue;
			const std::vector<int> state = numbers(line.substr(0, image));
			if (state.size() != 1 || state[0] < 0 || static_cast<std::size_t>(state[0]) >= s.states.size())
				continue;
			DrawEntry &d = s.states[static_cast<std::size_t>(state[0])];
			const std::string_view name = trim(line.substr(image + 9, color - image - 9));
			d.image = name == "NONE" ? std::string("NoImage") : std::string(name);
			if (const auto c = colorAfter(line.substr(color + 1), "Color:"))
				d.color = *c;
			if (const auto c = colorAfter(line.substr(border + 1), "Border:"))
				d.border = *c;
		}
	}
	if (!header)
		return std::nullopt;
	return s;
}

std::string writeScheme(const Scheme &scheme)
{
	std::string out = "Window Layout Scheme: Version '1'\n";
	out += "Enabled Text: " + colorText(scheme.enabledText) + "\n";
	out += "Enabled Text Border: " + colorText(scheme.enabledTextBorder) + "\n";
	out += "Disabled Text: " + colorText(scheme.disabledText) + "\n";
	out += "Disabled Text Border: " + colorText(scheme.disabledTextBorder) + "\n";
	out += "Hilite Text: " + colorText(scheme.hiliteText) + "\n";
	out += "Hilite Text Border: " + colorText(scheme.hiliteTextBorder) + "\n";
	if (scheme.font)
		out += "Default Font: \"" + scheme.font->name + "\" Size: " + std::to_string(scheme.font->size)
			+ " Bold: " + std::to_string(scheme.font->bold) + "\n";
	else
		out += "Default Font: \"None\" Size: 0 Bold: 0\n";
	out += "Number of states: " + std::to_string(scheme.states.size()) + "\n";
	for (std::size_t i = 0; i < scheme.states.size(); ++i)
	{
		const DrawEntry &d = scheme.states[i];
		const bool none = d.image.empty() || d.image == "NoImage";
		out += std::to_string(i) + ": Image: " + (none ? std::string("NONE") : d.image) + " Color: " + colorText(d.color)
			+ " Border: " + colorText(d.border) + "\n";
	}
	return out;
}

int applyScheme(const Scheme &scheme, Window &window)
{
	return applyTree(scheme, window);
}

void applySchemeText(const Scheme &scheme, Window &window)
{
	window.setDataFields("TEXTCOLOR", {{"ENABLED", wndColor(scheme.enabledText)},
		{"ENABLEDBORDER", wndColor(scheme.enabledTextBorder)}, {"DISABLED", wndColor(scheme.disabledText)},
		{"DISABLEDBORDER", wndColor(scheme.disabledTextBorder)}, {"HILITE", wndColor(scheme.hiliteText)},
		{"HILITEBORDER", wndColor(scheme.hiliteTextBorder)}});
	if (scheme.font)
		window.setDataFields("FONT", {{"NAME", '"' + scheme.font->name + '"'}, {"SIZE", std::to_string(scheme.font->size)},
			{"BOLD", std::to_string(scheme.font->bold)}});
}

Scheme schemeOf(const Layout &layout, Scheme base)
{
	if (base.states.size() < schemeStateCount())
		base.states.resize(schemeStateCount(), DrawEntry{"NoImage", UNDEFINED, UNDEFINED});
	std::vector<char> taken(base.states.size(), 0);
	bool text = false;
	bool font = false;
	for (const Window *w : layout.windows())
	{
		takeTree(*w, base, taken);
		forEachWindow(*w, [&](const Window &x, int) {
			if (!text && x.field("TEXTCOLOR"))
			{
				for (const auto &[key, value] : x.dataFields("TEXTCOLOR"))
				{
					const std::vector<int> n = numbers(value);
					if (n.size() < 4)
						continue;
					const Color c{n[0], n[1], n[2], n[3]};
					if (key == "ENABLED")
						base.enabledText = c;
					else if (key == "ENABLEDBORDER")
						base.enabledTextBorder = c;
					else if (key == "DISABLED")
						base.disabledText = c;
					else if (key == "DISABLEDBORDER")
						base.disabledTextBorder = c;
					else if (key == "HILITE")
						base.hiliteText = c;
					else if (key == "HILITEBORDER")
						base.hiliteTextBorder = c;
				}
				text = true;
			}
			if (!font)
				if (const std::optional<Font> f = x.font())
				{
					base.font = f;
					font = true;
				}
		});
	}
	return base;
}

} // namespace WndModel
