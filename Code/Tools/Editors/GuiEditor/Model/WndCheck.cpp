/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WndCheck.cpp -- checking a layout against the rules of the game's reader.

#include "Model/WndCheck.h"

#include "Engine/WndFields.h"

#include <algorithm>
#include <map>

namespace WndModel
{

namespace
{

const std::vector<std::string> KNOWN_FIELDS = {"WINDOWTYPE", "SCREENRECT", "NAME", "STATUS", "STYLE", "SYSTEMCALLBACK",
	"INPUTCALLBACK", "TOOLTIPCALLBACK", "DRAWCALLBACK", "FONT", "HEADERTEMPLATE", "LISTBOXDATA", "COMBOBOXDATA",
	"SLIDERDATA", "RADIOBUTTONDATA", "TOOLTIPTEXT", "TOOLTIPDELAY", "TEXT", "TEXTCOLOR", "STATICTEXTDATA",
	"TEXTENTRYDATA", "TABCONTROLDATA", "ENABLEDDRAWDATA", "DISABLEDDRAWDATA", "HILITEDRAWDATA",
	"LISTBOXENABLEDUPBUTTONDRAWDATA", "LISTBOXENABLEDDOWNBUTTONDRAWDATA", "LISTBOXENABLEDSLIDERDRAWDATA",
	"LISTBOXDISABLEDUPBUTTONDRAWDATA", "LISTBOXDISABLEDDOWNBUTTONDRAWDATA", "LISTBOXDISABLEDSLIDERDRAWDATA",
	"LISTBOXHILITEUPBUTTONDRAWDATA", "LISTBOXHILITEDOWNBUTTONDRAWDATA", "LISTBOXHILITESLIDERDRAWDATA",
	"SLIDERTHUMBENABLEDDRAWDATA", "SLIDERTHUMBDISABLEDDRAWDATA", "SLIDERTHUMBHILITEDRAWDATA",
	"COMBOBOXDROPDOWNBUTTONENABLEDDRAWDATA", "COMBOBOXDROPDOWNBUTTONDISABLEDDRAWDATA", "COMBOBOXDROPDOWNBUTTONHILITEDRAWDATA",
	"COMBOBOXEDITBOXENABLEDDRAWDATA", "COMBOBOXEDITBOXDISABLEDDRAWDATA", "COMBOBOXEDITBOXHILITEDRAWDATA",
	"COMBOBOXLISTBOXENABLEDDRAWDATA", "COMBOBOXLISTBOXDISABLEDDRAWDATA", "COMBOBOXLISTBOXHILITEDRAWDATA", "IMAGEOFFSET",
	"TOOLTIP", "DATA",
	// Default colours, also read between child windows.
	"ENABLEDCOLOR", "DISABLEDCOLOR", "HILITECOLOR", "SELECTEDCOLOR", "TEXTCOLOR", "BACKGROUNDCOLOR"};

const std::vector<std::string> FILE_FIELDS = {"FILE_VERSION", "LAYOUTINIT", "LAYOUTUPDATE", "LAYOUTSHUTDOWN", "ENABLEDCOLOR",
	"DISABLEDCOLOR", "HILITECOLOR", "SELECTEDCOLOR", "TEXTCOLOR", "BACKGROUNDCOLOR", "FONT"};

bool contains(const std::vector<std::string> &list, std::string_view word)
{
	return std::find(list.begin(), list.end(), word) != list.end();
}

struct Checker
{
	std::vector<Problem> problems;
	std::map<std::string, int> names;

	void add(Problem::Severity severity, const std::string &window, std::string text)
	{
		problems.push_back({severity, window, std::move(text)});
	}

	void window(const Window &w, int depth)
	{
		const std::string name = w.name();
		const std::string label = name.empty() ? std::string("(unnamed)") : name;
		std::vector<const Entry *> fields;
		for (const Entry &e : w.entries)
			if (e.kind == Entry::Kind::Field)
				fields.push_back(&e);
		if (fields.size() < 2 || fields[0]->key != "WINDOWTYPE" || fields[1]->key != "SCREENRECT")
			add(Problem::Severity::Error, label, "WINDOWTYPE and SCREENRECT must be the first two fields; the reader takes the first two as them");
		const std::string type = w.type();
		if (!type.empty() && !contains(windowTypes(), type))
			add(Problem::Severity::Error, label, "unknown window type " + type);
		if (w.field("SCREENRECT") && !w.rect())
			add(Problem::Severity::Error, label, "SCREENRECT without its corners");
		if (!w.ended)
			add(Problem::Severity::Error, label, "the file ends inside the window (END missing)");
		if (depth >= MAX_NESTING)
			add(Problem::Severity::Error, label, "nested deeper than " + std::to_string(MAX_NESTING) + " windows; the reader stops there");
		for (const Entry *e : fields)
		{
			if (!knownWindowField(e->key))
				add(Problem::Severity::Warning, label, "unknown field " + e->key + " (the reader skips it)");
			else if (!type.empty() && !Engine::wndFieldFitsType(e->key.c_str(), type.c_str()))
				add(Problem::Severity::Warning, label, e->key + " does not belong to a window of type " + type + " (the reader ignores it)");
			if (e->text.empty() || e->text.back() != ';')
				add(Problem::Severity::Error, label, e->key + " has no ';' (the reader takes the rest of the file)");
		}
		for (const std::string &word : w.status())
			if (!contains(statusWords(), word))
				add(Problem::Severity::Warning, label, "unknown status " + word);
		if (name.empty())
			add(Problem::Severity::Warning, label, "window without a name");
		else
		{
			if (static_cast<int>(w.fullName().size()) >= MAX_NAME_LENGTH)
				add(Problem::Severity::Error, label, "name with file longer than " + std::to_string(MAX_NAME_LENGTH - 1) + " characters");
			if (++names[name] == 2)
				add(Problem::Severity::Error, label, "name used more than once");
		}
	}
};

} // namespace

const std::vector<std::string> &windowTypes()
{
	static const std::vector<std::string> types = {"USER", "PUSHBUTTON", "RADIOBUTTON", "TABCONTROL", "TABPANE", "CHECKBOX",
		"VERTSLIDER", "HORZSLIDER", "SCROLLLISTBOX", "COMBOBOX", "ENTRYFIELD", "STATICTEXT", "PROGRESSBAR"};
	return types;
}

bool knownWindowField(std::string_view key)
{
	return contains(KNOWN_FIELDS, key);
}

std::vector<Problem> check(const Layout &layout)
{
	Checker c;
	if (layout.version() == 0)
		c.add(Problem::Severity::Warning, {}, "no FILE_VERSION; the reader expects it first");
	for (const Entry &e : layout.entries)
		if (e.kind == Entry::Kind::Field && !contains(FILE_FIELDS, e.key))
			c.add(Problem::Severity::Warning, {}, "unknown field " + e.key + " outside a window (the reader skips it)");
	forEachWindow(layout, [&c](const Window &w, int depth) { c.window(w, depth); });
	return c.problems;
}

} // namespace WndModel
