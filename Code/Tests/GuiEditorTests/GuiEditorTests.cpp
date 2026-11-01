/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// GuiEditorTests.cpp -- tests of the layout model and the editing core of the GUI editor (Code/Tools/Editors/GuiEditor/Model,
// Code/Tools/Editors/GuiEditor/Core): reading and writing, edits, undo, layout schemes, comparing layouts.
//
//   GuiEditorTests [folder]   with a folder: every .wnd in it must be written back unchanged and pass the
//                             check without errors, survive moving and undo, copy and paste (the gate passes
//                             the test layouts of tools/wine/testdaten)

#include "Core/Document.h"
#include "Core/WndArrange.h"
#include "Core/WndDiff.h"
#include "Core/WndEdit.h"
#include "Core/WndScheme.h"
#include "Model/WndCheck.h"
#include "Model/WndLayout.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace WndModel;

static int s_failures = 0;
static int s_checks = 0;

static void check(bool condition, const char *what, int line)
{
	++s_checks;
	if (!condition)
	{
		++s_failures;
		std::printf("  FAILED (line %d): %s\n", line, what);
	}
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// A layout in the style of the old GUIEdit (Save.cpp), with Windows line ends: a user window with a push
// button and a list box as children, children written in reverse order.
static std::string sampleLayout()
{
	std::string s =
		"FILE_VERSION = 2;\n"
		"STARTLAYOUTBLOCK\n"
		"  LAYOUTINIT = SampleInit;\n"
		"  LAYOUTUPDATE = [None];\n"
		"  LAYOUTSHUTDOWN = SampleShutdown;\n"
		"ENDLAYOUTBLOCK\n"
		"WINDOW\n"
		"  WINDOWTYPE = USER;\n"
		"  SCREENRECT = UPPERLEFT: 0 0,\n"
		"               BOTTOMRIGHT: 800 600,\n"
		"               CREATIONRESOLUTION: 800 600;\n"
		"  NAME = \"Sample.wnd:Parent\";\n"
		"  STATUS = ENABLED+IMAGE;\n"
		"  STYLE = USER;\n"
		"  SYSTEMCALLBACK = \"SampleSystem\";\n"
		"  INPUTCALLBACK = \"[None]\";\n"
		"  TOOLTIPCALLBACK = \"[None]\";\n"
		"  DRAWCALLBACK = \"[None]\";\n"
		"  FONT = NAME: \"Arial\", SIZE: 12, BOLD: 1;\n"
		"  HEADERTEMPLATE = \"[NONE]\";\n"
		"  TOOLTIPDELAY = -1;\n"
		"  TEXTCOLOR = ENABLED:  255 255 255 255, ENABLEDBORDER:  0 0 0 255,\n"
		"            DISABLED: 128 128 128 255, DISABLEDBORDER: 0 0 0 255,\n"
		"            HILITE:   255 255 0 255, HILITEBORDER:   0 0 0 255;\n"
		"  ENABLEDDRAWDATA = IMAGE: MainMenuBackdrop, COLOR: 1 2 3 4, BORDERCOLOR: 5 6 7 8,\n";
	for (int i = 0; i < 7; ++i)
		s += "                      IMAGE: NoImage, COLOR: 0 0 0 0, BORDERCOLOR: 0 0 0 0,\n";
	s += "                      IMAGE: NoImage, COLOR: 9 9 9 9, BORDERCOLOR: 0 0 0 0;\n"
		"  CHILD\n"
		"  WINDOW\n"
		"    WINDOWTYPE = SCROLLLISTBOX;\n"
		"    SCREENRECT = UPPERLEFT: 10 300,\n"
		"                 BOTTOMRIGHT: 200 500,\n"
		"                 CREATIONRESOLUTION: 800 600;\n"
		"    NAME = \"Sample.wnd:ListGames\";\n"
		"    STATUS = ENABLED;\n"
		"    STYLE = SCROLLLISTBOX;\n"
		"    LISTBOXDATA = LENGTH: 100,\n"
		"                  AUTOSCROLL: 0,\n"
		"                  FORCESELECT: 0;\n"
		"  END\n"
		"  CHILD\n"
		"  WINDOW\n"
		"    WINDOWTYPE = PUSHBUTTON;\n"
		"    SCREENRECT = UPPERLEFT: 10 10,\n"
		"                 BOTTOMRIGHT: 110 40,\n"
		"                 CREATIONRESOLUTION: 800 600;\n"
		"    NAME = \"Sample.wnd:ButtonOk\";\n"
		"    STATUS = ENABLED+HIDDEN;\n"
		"    STYLE = PUSHBUTTON+MOUSETRACK;\n"
		"    TEXT = \"GUI:Ok\";\n"
		"  END\n"
		"  ENDALLCHILDREN\n"
		"END\n";
	std::string crlf;
	for (const char c : s)
	{
		if (c == '\n')
			crlf += '\r';
		crlf += c;
	}
	return crlf;
}

static void testReadWrite()
{
	const std::string text = sampleLayout();
	const Layout layout = read(text);
	CHECK(write(layout) == text);
	CHECK(layout.version() == 2);
	CHECK(layout.layoutCallback("LAYOUTINIT") == "SampleInit" && layout.layoutCallback("LAYOUTUPDATE") == "[None]");
	CHECK(layout.windows().size() == 1);
	if (layout.windows().size() != 1)
		return;
	const Window &parent = *layout.windows().front();
	CHECK(parent.type() == "USER" && parent.name() == "Parent" && parent.fullName() == "Sample.wnd:Parent");
	const std::optional<Rect> r = parent.rect();
	CHECK(r && r->left == 0 && r->top == 0 && r->right == 800 && r->bottom == 600 && r->creationWidth == 800);
	CHECK(parent.status() == (std::vector<std::string>{"ENABLED", "IMAGE"}) && parent.hasStatus("IMAGE") && !parent.hasStatus("HIDDEN"));
	CHECK(parent.callback("SYSTEMCALLBACK") == "SampleSystem");
	const std::optional<Font> f = parent.font();
	CHECK(f && f->name == "Arial" && f->size == 12 && f->bold == 1);
	const std::vector<DrawEntry> draw = parent.drawData("ENABLEDDRAWDATA");
	CHECK(draw.size() == 9 && draw[0].image == "MainMenuBackdrop" && draw[0].color == (Color{1, 2, 3, 4})
		&& draw[0].border == (Color{5, 6, 7, 8}) && draw[8].color == (Color{9, 9, 9, 9}));
	CHECK(!parent.isGadget());
	const std::vector<const Window *> kids = parent.children();
	CHECK(kids.size() == 2 && kids[0]->name() == "ListGames" && kids[1]->name() == "ButtonOk");
	CHECK(kids.size() == 2 && kids[1]->text() == "GUI:Ok" && kids[1]->isGadget() && kids[1]->hasStatus("HIDDEN"));
	int count = 0;
	int deepest = 0;
	forEachWindow(layout, [&](const Window &, int depth) { ++count; deepest = std::max(deepest, depth); });
	CHECK(count == 3 && deepest == 1);
	CHECK(check(layout).empty());
}

static void testEdits()
{
	const std::string text = sampleLayout();
	Layout layout = read(text);
	Window &parent = *layout.windows().front();
	Window &button = *parent.children()[1];

	// A changed rectangle is written in the GUIEdit style; the rest of the file stays as it was.
	Rect r = *button.rect();
	r.left = 20;
	r.right = 140;
	button.setRect(r);
	const std::string written = write(layout);
	CHECK(written.find("    SCREENRECT = UPPERLEFT: 20 10,\r\n                 BOTTOMRIGHT: 140 40,\r\n                 CREATIONRESOLUTION: 800 600;") != std::string::npos);
	const Layout again = read(written);
	CHECK(write(again) == written);
	CHECK(again.windows().front()->children()[1]->rect()->left == 20);
	CHECK(written.size() == text.size());	// same lengths of numbers, same indent: only the digits change

	// Status, name and a draw data block.
	button.setStatus({"ENABLED"});
	CHECK(button.value("STATUS") == "ENABLED");
	button.setStatus({});
	CHECK(button.value("STATUS") == "NONE" && button.status().empty());
	button.setName("Sample.wnd", "ButtonCancel");
	CHECK(button.name() == "ButtonCancel" && button.fullName() == "Sample.wnd:ButtonCancel");
	std::vector<DrawEntry> draw(9);
	draw[3].image = "Button";
	draw[3].color = {10, 20, 30, 40};
	button.setDrawData("HILITEDRAWDATA", draw);
	CHECK(button.drawData("HILITEDRAWDATA") == draw);

	// Data fields as pairs: values with commas and repeated keys stay as they are.
	const auto list = parent.children()[0]->dataFields("LISTBOXDATA");
	CHECK(list.size() == 3 && list[0] == (std::pair<std::string, std::string>{"LENGTH", "100"}) && list[2].first == "FORCESELECT");
	const auto colors = parent.dataFields("TEXTCOLOR");
	CHECK(colors.size() == 6 && colors[0].first == "ENABLED" && colors[0].second == "255 255 255 255" && colors[5].first == "HILITEBORDER");
	parent.setValue("TABCONTROLDATA", "TABCOUNT: 2, PANEDISABLED: 8,0,1,0,0,0,0,0,0, COLUMNSWIDTH%: 10, COLUMNSWIDTH%: 90");
	auto tab = parent.dataFields("TABCONTROLDATA");
	CHECK(tab.size() == 4 && tab[1].second == "8,0,1,0,0,0,0,0,0" && tab[2].first == "COLUMNSWIDTH%" && tab[3].second == "90");
	tab[0].second = "3";
	parent.setDataFields("TABCONTROLDATA", tab);
	CHECK(parent.value("TABCONTROLDATA") == "TABCOUNT: 3, PANEDISABLED: 8,0,1,0,0,0,0,0,0, COLUMNSWIDTH%: 10, COLUMNSWIDTH%: 90");
	CHECK(parent.removeField("TABCONTROLDATA") && parent.dataFields("TABCONTROLDATA").empty());

	// A missing field goes after the last field, before the children, with the window's indent.
	parent.setValue("TOOLTIPTEXT", "\"GUI:Tip\"");
	const std::string withTip = write(layout);
	const std::size_t tip = withTip.find("\r\n  TOOLTIPTEXT = \"GUI:Tip\";");
	const std::size_t child = withTip.find("  CHILD");
	CHECK(tip != std::string::npos && child != std::string::npos && tip < child);
	CHECK(parent.removeField("TOOLTIPTEXT") && !parent.field("TOOLTIPTEXT") && !parent.removeField("TOOLTIPTEXT"));
}

static void testOddInput()
{
	// Whatever the text, reading and writing gives it back, and the check does not stop.
	const char *odd[] = {"", "   \n\t", "WINDOW", "WINDOW\n  WINDOWTYPE = USER", "END END END", "FILE_VERSION = 2",
		"WINDOW\n  CHILD\n  WINDOW\n  WINDOW\n", "KEY;; ;", "WINDOW WINDOWTYPE=USER; SCREENRECT = ; END",
		"\r\n\r\nWINDOW\r\nEND\r\n\r\n", "ENDALLCHILDREN CHILD WINDOW END STARTLAYOUTBLOCK"};
	for (const char *text : odd)
	{
		const Layout layout = read(text);
		CHECK(write(layout) == text);
		(void)check(layout);
	}
	// A file that ends inside a window: the window is open, and the check says so.
	const Layout cut = read("FILE_VERSION = 2;\nWINDOW\n  WINDOWTYPE = USER;\n");
	CHECK(cut.windows().size() == 1 && !cut.windows().front()->ended);
	bool reported = false;
	for (const Problem &p : check(cut))
		reported = reported || p.text.find("END missing") != std::string::npos;
	CHECK(reported);

	// Random changes to a real layout: still read and written back byte for byte.
	std::mt19937 random(1234);
	const std::string base = sampleLayout();
	const std::string alphabet = "WINDOW END CHILD;=\r\n \"+:,ENDALLCHILDREN";
	for (int i = 0; i < 3000; ++i)
	{
		std::string text = base;
		const int changes = 1 + static_cast<int>(random() % 8);
		for (int c = 0; c < changes && !text.empty(); ++c)
		{
			const std::size_t at = random() % text.size();
			switch (random() % 3)
			{
			case 0:
				text[at] = alphabet[random() % alphabet.size()];
				break;
			case 1:
				text.erase(at, 1 + random() % 20);
				break;
			default:
				text.insert(at, 1, alphabet[random() % alphabet.size()]);
				break;
			}
		}
		const Layout layout = read(text);
		if (write(layout) != text)
		{
			CHECK(write(layout) == text);
			break;
		}
		(void)check(layout);
	}
}

static void testCheck()
{
	std::string text = sampleLayout();
	// The list box's data in a static text, a field the reader does not know, a name used twice, a missing type.
	const auto replace = [&text](const std::string &from, const std::string &to) {
		const std::size_t at = text.find(from);
		if (at != std::string::npos)
			text.replace(at, from.size(), to);
	};
	replace("WINDOWTYPE = SCROLLLISTBOX;", "WINDOWTYPE = STATICTEXT;");
	replace("STYLE = PUSHBUTTON+MOUSETRACK;", "STYLE = PUSHBUTTON+MOUSETRACK;\r\n    SPARKLE = 3;");
	replace("Sample.wnd:ButtonOk", "Sample.wnd:ListGames");
	const std::vector<Problem> problems = check(read(text));
	const auto has = [&problems](const char *what, Problem::Severity severity) {
		for (const Problem &p : problems)
			if (p.text.find(what) != std::string::npos && p.severity == severity)
				return true;
		return false;
	};
	CHECK(has("LISTBOXDATA does not belong to a window of type STATICTEXT", Problem::Severity::Warning));
	CHECK(has("unknown field SPARKLE", Problem::Severity::Warning));
	CHECK(has("name used more than once", Problem::Severity::Error));

	// Too deep nesting and a too long name.
	std::string deep = "FILE_VERSION = 2;\n";
	for (int i = 0; i < 12; ++i)
		deep += "WINDOW WINDOWTYPE = USER; SCREENRECT = UPPERLEFT: 0 0, BOTTOMRIGHT: 1 1, CREATIONRESOLUTION: 800 600; NAME = \"D.wnd:W"
			+ std::to_string(i) + "\"; CHILD ";
	for (int i = 0; i < 12; ++i)
		deep += "ENDALLCHILDREN END ";
	bool tooDeep = false;
	for (const Problem &p : check(read(deep)))
		tooDeep = tooDeep || p.text.find("nested deeper") != std::string::npos;
	CHECK(tooDeep);
	const std::string longName = "FILE_VERSION = 2;\nWINDOW WINDOWTYPE = USER; SCREENRECT = UPPERLEFT: 0 0, BOTTOMRIGHT: 1 1, "
		"CREATIONRESOLUTION: 800 600; NAME = \"Sample.wnd:" + std::string(60, 'x') + "\"; END";
	bool tooLong = false;
	for (const Problem &p : check(read(longName)))
		tooLong = tooLong || p.text.find("longer than") != std::string::npos;
	CHECK(tooLong);
}

static bool fieldsBeforeChildren(const Window &w)
{
	bool child = false;
	for (const Entry &e : w.entries)
	{
		if (e.kind == Entry::Kind::Field && child)
			return false;
		if (e.kind != Entry::Kind::Field)
			child = true;
		if (e.kind == Entry::Kind::Window && !fieldsBeforeChildren(e.window.front()))
			return false;
	}
	return true;
}

static bool namesUnique(const Layout &layout)
{
	const std::vector<std::string> names = fullNames(layout);
	return std::set<std::string>(names.begin(), names.end()).size() == names.size();
}

static int errorsOf(const Layout &layout)
{
	int n = 0;
	for (const Problem &p : check(layout))
		n += p.severity == Problem::Severity::Error ? 1 : 0;
	return n;
}

static void testWndEdit()
{
	const std::string text = sampleLayout();
	Layout layout = read(text);

	// Finding windows.
	CHECK(findWindow(layout, "Sample.wnd:Parent") == (WindowPath{0}));
	CHECK(findWindow(layout, "Sample.wnd:ButtonOk") == (WindowPath{0, 1}));
	CHECK(!findWindow(layout, "Sample.wnd:Nothing"));
	CHECK(windowAt(layout, {0, 0}) && windowAt(layout, {0, 0})->name() == "ListGames" && !windowAt(layout, {0, 2}) && !windowAt(layout, {}));
	CHECK(contains({0}, {0, 1}) && contains({0, 1}, {0, 1}) && !contains({0, 1}, {0}) && !contains({1}, {0, 1}));
	CHECK(layoutFile(layout) == "Sample.wnd");

	// Names.
	CHECK(uniqueName(layout, "Sample.wnd", "ButtonOk") == "ButtonOk1");
	CHECK(uniqueName(layout, "Sample.wnd", "ButtonNew") == "ButtonNew");
	CHECK(uniqueName(layout, "Other.wnd", "ButtonOk") == "ButtonOk");

	// A new child at the end and away again: the file is as it was (GUIEdit style, CHILD in front of each).
	Window made = makeWindow("PUSHBUTTON", "Sample.wnd", "ButtonNew", defaultRect("PUSHBUTTON", 300, 200));
	CHECK(made.type() == "PUSHBUTTON" && made.fullName() == "Sample.wnd:ButtonNew" && made.rect()->right == 420);
	CHECK(made.value("STYLE") == "PUSHBUTTON+MOUSETRACK" && makeWindow("USER", "S.wnd", "U", defaultRect("USER", 0, 0)).value("STYLE") == "USER");
	const WindowPath at = insertWindow(layout, {0}, 99, made);
	CHECK(at == (WindowPath{0, 2}) && windowAt(layout, at) && windowAt(layout, at)->name() == "ButtonNew");
	const std::string inserted = write(layout);
	CHECK(inserted.find("\r\n  CHILD\r\n  WINDOW\r\n    WINDOWTYPE = PUSHBUTTON;") != std::string::npos);
	CHECK(inserted.find("\n    WINDOWTYPE") != std::string::npos && inserted.find("WINDOWTYPE = PUSHBUTTON;\n") == std::string::npos);
	CHECK(write(read(inserted)) == inserted && errorsOf(read(inserted)) == 0 && fieldsBeforeChildren(*layout.windows().front()));
	CHECK(removeWindow(layout, at) && write(layout) == text);

	// The first child, and a window that had no children.
	insertWindow(layout, {0}, 0, made);
	CHECK(windowAt(layout, {0, 0})->name() == "ButtonNew" && windowAt(layout, {0, 1})->name() == "ListGames");
	const WindowPath grand = insertWindow(layout, {0, 1}, 0, makeWindow("STATICTEXT", "Sample.wnd", "Label", defaultRect("STATICTEXT", 20, 310)));
	CHECK(grand == (WindowPath{0, 1, 0}) && windowAt(layout, grand) && windowAt(layout, grand)->name() == "Label");
	CHECK(write(layout).find("ENDALLCHILDREN") != write(layout).rfind("ENDALLCHILDREN"));
	CHECK(fieldsBeforeChildren(*layout.windows().front()) && errorsOf(layout) == 0);
	CHECK(removeWindow(layout, grand) && windowAt(layout, {0, 1})->children().empty());
	CHECK(write(layout).find("ENDALLCHILDREN") == write(layout).rfind("ENDALLCHILDREN"));
	CHECK(removeWindow(layout, {0, 0}) && write(layout) == text);

	// Moving in the tree: out to the top level and back, never into itself.
	std::optional<WindowPath> moved = moveWindow(layout, {0, 1}, {}, 99);
	CHECK(moved == (WindowPath{1}) && layout.windows().size() == 2 && layout.windows()[1]->name() == "ButtonOk");
	CHECK(write(read(write(layout))) == write(layout) && errorsOf(layout) == 0);
	moved = moveWindow(layout, {1}, {0}, 1);
	CHECK(moved == (WindowPath{0, 1}) && layout.windows().size() == 1 && windowAt(layout, {0, 1})->name() == "ButtonOk");
	CHECK(write(layout) == text);
	CHECK(!moveWindow(layout, {0}, {0, 0}, 0) && !moveWindow(layout, {0, 5}, {}, 0) && write(layout) == text);
	// Order among siblings: the list goes behind the button (drawn above it).
	moved = moveWindow(layout, {0, 0}, {0}, 1);
	CHECK(moved == (WindowPath{0, 1}) && windowAt(layout, {0, 0})->name() == "ButtonOk" && windowAt(layout, {0, 1})->name() == "ListGames");
	CHECK(fieldsBeforeChildren(*layout.windows().front()));
	layout = read(text);

	// Moving on the screen takes the children along.
	offsetWindow(*layout.windows().front(), 10, 5);
	CHECK(layout.windows().front()->rect()->left == 10 && windowAt(layout, {0, 1})->rect()->left == 20 && windowAt(layout, {0, 1})->rect()->top == 15);
	offsetWindow(*layout.windows().front(), -10, -5);
	CHECK(write(layout) == text);

	// Copy and paste: the same windows come back, with names nobody has.
	const std::string clip = windowsText({layout.windows().front()});
	std::vector<Window> pasted = readWindows(clip);
	CHECK(pasted.size() == 1 && pasted[0].children().size() == 2 && clip.find("\r\n") != std::string::npos);
	CHECK(readWindows("no windows here; FIELD = 1;").empty());
	if (!pasted.empty())
	{
		makeNamesUnique(layout, "Sample.wnd", pasted[0]);
		CHECK(pasted[0].name() == "Parent1" && pasted[0].children()[1]->name() == "ButtonOk1");
		insertWindow(layout, {}, 99, pasted[0]);
		CHECK(layout.windows().size() == 2 && namesUnique(layout) && errorsOf(layout) == 0 && write(read(write(layout))) == write(layout));
	}

	// The layout block.
	setLayoutValue(layout, "LAYOUTUPDATE", "SampleUpdate");
	CHECK(layout.layoutCallback("LAYOUTUPDATE") == "SampleUpdate" && layout.layoutCallback("LAYOUTINIT") == "SampleInit");

	// Another file name in front of every NAME.
	setLayoutFile(layout, "Copy.wnd");
	CHECK(layoutFile(layout) == "Copy.wnd" && findWindow(layout, "Copy.wnd:ButtonOk1"));

	// Every type of window the editor makes reads without errors, alone and as children.
	Layout fresh = newLayout();
	CHECK(fresh.version() == 2 && fresh.windows().empty() && write(fresh).find("\r\n") != std::string::npos);
	insertWindow(fresh, {}, 0, makeWindow("USER", "New.wnd", "Parent", defaultRect("USER", 0, 0)));
	int made_ = 0;
	for (const std::string &type : windowTypes())
	{
		const std::string name = uniqueName(fresh, "New.wnd", type);
		insertWindow(fresh, {0}, 99, makeWindow(type, "New.wnd", name, defaultRect(type, 10, 10)));
		++made_;
	}
	const Layout back = read(write(fresh));
	CHECK(write(back) == write(fresh) && errorsOf(back) == 0 && namesUnique(back));
	CHECK(back.windows().size() == 1 && static_cast<int>(back.windows().front()->children().size()) == made_);
	for (const Problem &p : check(back))
		if (p.severity == Problem::Severity::Error)
			std::printf("  new windows: %s: %s\n", p.window.c_str(), p.text.c_str());
	// A tab control comes with its eight panes below the tabs, the first one shown.
	const Window tabs = makeWindow("TABCONTROL", "New.wnd", "Tabs", defaultRect("TABCONTROL", 100, 100));
	CHECK(tabs.children().size() == 8 && tabs.children()[0]->type() == "TABPANE" && tabs.children()[7]->name() == "TabsPane7");
	CHECK(!tabs.children()[0]->hasStatus("HIDDEN") && tabs.children()[1]->hasStatus("HIDDEN"));
	CHECK(tabs.children()[0]->rect() && tabs.children()[0]->rect()->left == 104 && tabs.children()[0]->rect()->top == 124
		&& tabs.children()[0]->rect()->right == 396 && tabs.children()[0]->rect()->bottom == 296);
	CHECK(tabs.dataFields("TABCONTROLDATA").size() == 7 && tabs.dataFields("TABCONTROLDATA")[1].second == "3");
	const std::optional<WindowPath> button = findWindow(back, "New.wnd:PUSHBUTTON");
	CHECK(canHaveChildren(*back.windows().front()) && button && !canHaveChildren(*windowAt(back, *button)));
}

static void testDocument()
{
	const std::string text = sampleLayout();
	Document doc(text);
	CHECK(!doc.modified() && !doc.canUndo() && !doc.canRedo());
	CHECK(!doc.edit("nothing", [](Layout &) {}) && !doc.canUndo());
	CHECK(doc.edit("Move", [](Layout &l) { offsetWindow(*l.windows().front(), 4, 2); }));
	CHECK(doc.modified() && doc.canUndo() && doc.undoText() == "Move");
	const std::string moved = doc.text();
	CHECK(doc.undo() && doc.text() == text && !doc.modified() && doc.canRedo() && doc.redoText() == "Move");
	CHECK(doc.redo() && doc.text() == moved && !doc.canRedo());
	// A drag: many live changes, one step; each live change starts again from where the drag began.
	doc.begin();
	doc.live([](Layout &l) { offsetWindow(*l.windows().front(), 1, 0); });
	doc.live([](Layout &l) { offsetWindow(*l.windows().front(), 3, 0); });
	CHECK(doc.inGesture() && doc.layout().windows().front()->rect()->left == 7);
	CHECK(doc.finish("Drag") && doc.undoText() == "Drag" && !doc.inGesture());
	CHECK(doc.undo() && doc.text() == moved);
	doc.begin();
	doc.live([](Layout &l) { offsetWindow(*l.windows().front(), 9, 9); });
	doc.cancel();
	CHECK(doc.text() == moved && doc.undoText() == "Move");
	doc.markSaved();
	CHECK(!doc.modified());
	// A new step after undo drops what could be redone.
	CHECK(doc.undo() && doc.edit("Rename", [](Layout &l) { l.windows().front()->setName("Sample.wnd", "Main"); }) && !doc.canRedo());
	Document empty;
	CHECK(empty.layout().windows().empty() && !empty.modified());
}

static std::size_t stateOf(std::string_view name)
{
	for (std::size_t i = 0; i < schemeStateCount(); ++i)
		if (schemeStateName(i) == name)
			return i;
	return schemeStateCount();
}

static void testScheme()
{
	// A scheme file as GUIEdit writes it reads back to the same text; NONE is NoImage.
	Scheme scheme = emptyScheme();
	CHECK(schemeStateCount() == 253 && schemeStateName(1) == "BUTTON_ENABLED" && schemeStateName(252) == "GENERIC_HILITE");
	const std::size_t button = stateOf("BUTTON_ENABLED");
	const std::size_t hilite = stateOf("BUTTON_HILITE_PUSHED");
	const std::size_t thumb = stateOf("VSLIDER_THUMB_HILITE");
	const std::size_t generic = stateOf("GENERIC_DISABLED");
	CHECK(button < schemeStateCount() && hilite < schemeStateCount() && thumb < schemeStateCount() && generic < schemeStateCount());
	scheme.states[button] = {"ButtonUp", {10, 20, 30, 255}, {1, 2, 3, 4}};
	scheme.states[hilite] = {"ButtonHot", {40, 50, 60, 255}, {5, 6, 7, 8}};
	scheme.states[thumb] = {"Thumb", {70, 80, 90, 255}, {9, 9, 9, 9}};
	scheme.states[generic] = {"NoImage", {11, 12, 13, 14}, {15, 16, 17, 18}};
	scheme.enabledText = {200, 201, 202, 255};
	scheme.font = Font{"Times New Roman", 14, 1};
	const std::string text = writeScheme(scheme);
	CHECK(text.rfind("Window Layout Scheme: Version '1'\nEnabled Text: (200,201,202,255)\n", 0) == 0);
	CHECK(text.find("Default Font: \"Times New Roman\" Size: 14 Bold: 1\nNumber of states: 253\n0: Image: NONE ") != std::string::npos);
	CHECK(text.find("\n1: Image: ButtonUp Color: (10,20,30,255) Border: (1,2,3,4)\n") != std::string::npos);
	const std::optional<Scheme> back = readScheme(text);
	CHECK(back && writeScheme(*back) == text && back->states[button] == scheme.states[button] && back->font == scheme.font);
	std::string crlf;
	for (const char c : text)
		crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
	CHECK(readScheme(crlf) && writeScheme(*readScheme(crlf)) == text);
	CHECK(!readScheme("Window Layout Scheme: Version '2'\n") && !readScheme("FILE_VERSION = 2;") && !readScheme(""));
	std::string noFont = text;
	noFont.replace(noFont.find("\"Times New Roman\" Size: 14 Bold: 1"), 34, "\"None\" Size: 0 Bold: 0");
	CHECK(readScheme(noFont) && !readScheme(noFont)->font);

	// Applying: the button takes its states, the user window the generic ones, the slider's thumb its part;
	// a second time changes nothing.
	Layout layout = newLayout();
	insertWindow(layout, {}, 0, makeWindow("USER", "S.wnd", "Parent", defaultRect("USER", 0, 0)));
	insertWindow(layout, {0}, 99, makeWindow("PUSHBUTTON", "S.wnd", "Button", defaultRect("PUSHBUTTON", 10, 10)));
	insertWindow(layout, {0}, 99, makeWindow("VERTSLIDER", "S.wnd", "Slider", defaultRect("VERTSLIDER", 10, 60)));
	insertWindow(layout, {0}, 99, makeWindow("TABCONTROL", "S.wnd", "Tabs", defaultRect("TABCONTROL", 100, 100)));
	Window &root = *layout.windows().front();
	CHECK(applyScheme(scheme, root) == 4);
	CHECK(applyScheme(scheme, root) == 0);
	const Window &b = *windowAt(layout, {0, 0});
	CHECK(b.drawData("ENABLEDDRAWDATA").size() == 9 && b.drawData("ENABLEDDRAWDATA")[0] == scheme.states[button]);
	CHECK(b.drawData("HILITEDRAWDATA")[1] == scheme.states[hilite]);
	CHECK(windowAt(layout, {0, 1})->drawData("SLIDERTHUMBHILITEDRAWDATA")[0] == scheme.states[thumb]);
	CHECK(root.drawData("DISABLEDDRAWDATA")[0] == scheme.states[generic]);
	// The panes of the tab control belong to it and stay as they were.
	const Window &pane = *windowAt(layout, {0, 2, 0});
	CHECK(pane.type() == "TABPANE" && !(pane.drawData("DISABLEDDRAWDATA")[0] == scheme.states[generic]));
	CHECK(errorsOf(read(write(layout))) == 0 && write(read(write(layout))) == write(layout));

	// Parts the window does not have are not added; missing base draw data is.
	const Layout bareLayout = read("WINDOW\r\n  WINDOWTYPE = PUSHBUTTON;\r\n  NAME = \"S.wnd:Bare\";\r\nEND\r\n");
	CHECK(bareLayout.windows().size() == 1);
	Window bare = bareLayout.windows().empty() ? Window{} : *bareLayout.windows().front();
	CHECK(applyScheme(scheme, bare) == 1 && bare.drawData("ENABLEDDRAWDATA").size() == 9 && !bare.field("LISTBOXENABLEDSLIDERDRAWDATA"));
	CHECK(bare.drawData("ENABLEDDRAWDATA")[0] == scheme.states[button] && bare.drawData("ENABLEDDRAWDATA")[8].image == "NoImage");

	// Text colours and font for a new window.
	Window fresh = makeWindow("STATICTEXT", "S.wnd", "Label", defaultRect("STATICTEXT", 0, 0));
	applySchemeText(scheme, fresh);
	CHECK(fresh.dataFields("TEXTCOLOR").size() == 6 && fresh.dataFields("TEXTCOLOR")[0].second == "200 201 202 255");
	CHECK(fresh.font() == scheme.font);

	// The scheme of a layout gives back what was applied.
	const Scheme taken = schemeOf(layout, emptyScheme());
	CHECK(taken.states[button] == scheme.states[button] && taken.states[hilite] == scheme.states[hilite]);
	CHECK(taken.states[thumb] == scheme.states[thumb] && taken.states[generic] == scheme.states[generic]);
	CHECK(taken.font && taken.font->name == "Arial");
}

static void testDiff()
{
	const std::string text = sampleLayout();
	const Layout a = read(text);
	CHECK(diffLayouts(a, a).empty());
	// Only laid out differently: no change.
	std::string spaced = text;
	spaced.replace(spaced.find("WINDOWTYPE = USER;"), 18, "WINDOWTYPE =    USER ;");
	CHECK(diffLayouts(a, read(spaced)).empty());

	Layout b = read(text);
	offsetWindow(*windowAt(b, {0, 1}), 5, 0);
	removeWindow(b, {0, 0});
	insertWindow(b, {}, 99, makeWindow("STATICTEXT", "Sample.wnd", "Label", defaultRect("STATICTEXT", 0, 0)));
	setLayoutValue(b, "LAYOUTINIT", "OtherInit");
	const std::vector<WindowDiff> d = diffLayouts(a, b);
	CHECK(d.size() == 4);
	if (d.size() == 4)
	{
		CHECK(d[0].kind == WindowDiff::Kind::Changed && d[0].name.empty() && d[0].fields.size() == 1 && d[0].fields[0].key == "LAYOUTINIT"
			&& d[0].fields[0].after == "OtherInit");
		CHECK(d[1].kind == WindowDiff::Kind::Changed && d[1].name == "ButtonOk" && d[1].fields.size() == 1 && d[1].fields[0].key == "SCREENRECT");
		CHECK(d[2].kind == WindowDiff::Kind::Added && d[2].name == "Label");
		CHECK(d[3].kind == WindowDiff::Kind::Removed && d[3].name == "ListGames");
	}
	// Moved to another parent; another file name in front of the names is no change.
	Layout c = read(text);
	moveWindow(c, {0, 1}, {}, 99);
	setLayoutFile(c, "Copy.wnd");
	const std::vector<WindowDiff> m = diffLayouts(a, c);
	CHECK(m.size() == 1 && m[0].name == "ButtonOk" && m[0].fields.size() == 1 && m[0].fields[0].key == "parent"
		&& m[0].fields[0].before == "Parent" && m[0].fields[0].after.empty());
}

static Rect rectAt(const Layout &l, const WindowPath &p)
{
	const Window *w = windowAt(l, p);
	return w && w->rect() ? *w->rect() : Rect{};
}

static void testArrange()
{
	// Three buttons in a user window: A at (10,10) 100x30, B at (200,60) 50x20, C at (400,100) 80x40.
	Layout layout = newLayout();
	insertWindow(layout, {}, 0, makeWindow("USER", "A.wnd", "Parent", defaultRect("USER", 0, 0)));
	const auto add = [&](const char *name, int l, int t, int w, int h) {
		Rect r = defaultRect("PUSHBUTTON", l, t);
		r.right = l + w;
		r.bottom = t + h;
		insertWindow(layout, {0}, 99, makeWindow("PUSHBUTTON", "A.wnd", name, r));
	};
	add("A", 10, 10, 100, 30);
	add("B", 200, 60, 50, 20);
	add("C", 400, 100, 80, 40);
	const std::vector<WindowPath> abc = {{0, 0}, {0, 1}, {0, 2}};
	const Layout before = layout;

	// Aligned to the last one (C).
	CHECK(alignWindows(layout, abc, Align::Left) == 2 && rectAt(layout, {0, 0}).left == 400 && rectAt(layout, {0, 1}).left == 400);
	CHECK(rectAt(layout, {0, 0}).right == 500 && rectAt(layout, {0, 0}).top == 10 && rectAt(layout, {0, 2}).left == 400);
	CHECK(alignWindows(layout, abc, Align::Left) == 0);
	layout = before;
	CHECK(alignWindows(layout, abc, Align::Bottom) == 2 && rectAt(layout, {0, 0}).bottom == 140 && rectAt(layout, {0, 1}).bottom == 140);
	layout = before;
	CHECK(alignWindows(layout, abc, Align::CenterX) == 2 && rectAt(layout, {0, 1}).left == 415 && rectAt(layout, {0, 0}).left == 390);
	layout = before;
	CHECK(alignWindows(layout, abc, Align::Right) == 2 && rectAt(layout, {0, 1}).right == 480);

	// Spread: A and C stay, B sits so both gaps are equal.
	layout = before;
	CHECK(distributeWindows(layout, abc, true) == 1);
	const Rect a = rectAt(layout, {0, 0});
	const Rect b = rectAt(layout, {0, 1});
	const Rect c = rectAt(layout, {0, 2});
	CHECK(a.left == 10 && c.left == 400 && b.left - a.right == c.left - b.right && b.top == 60);
	CHECK(distributeWindows(layout, {{0, 0}, {0, 1}}, true) == 0);
	layout = before;
	// Vertically they already stand evenly (gaps 20 and 20): nothing moves.
	CHECK(distributeWindows(layout, abc, false) == 0 && rectAt(layout, {0, 1}).top - 40 == 100 - rectAt(layout, {0, 1}).bottom);

	// Same size as the last one; the top left corner stays.
	layout = before;
	CHECK(sameSize(layout, abc, true, true) == 2 && rectAt(layout, {0, 0}).right == 90 && rectAt(layout, {0, 0}).bottom == 50
		&& rectAt(layout, {0, 1}).left == 200);
	layout = before;
	CHECK(sameSize(layout, abc, true, false) == 2 && rectAt(layout, {0, 1}).bottom == 80);
	CHECK(errorsOf(read(write(layout))) == 0);

	// Guides: the moved rectangle snaps to the nearest edge or centre within the tolerance.
	const std::vector<Rect> others = {Rect{100, 100, 200, 150}};
	GuideSnap g = snapToGuides(Rect{203, 300, 253, 320}, others, 4);
	CHECK(g.dx == -3 && g.lineX && *g.lineX == 200 && g.dy == 0 && !g.lineY);
	g = snapToGuides(Rect{500, 122, 520, 128}, others, 4);
	CHECK(g.dy == 0 && g.lineY && *g.lineY == 125 && !g.lineX);
	g = snapToGuides(Rect{500, 400, 520, 420}, others, 4);
	CHECK(!g.lineX && !g.lineY && g.dx == 0 && g.dy == 0);
}

static void testHistory()
{
	Document doc(sampleLayout());
	for (const char *step : {"One", "Two", "Three"})
		doc.edit(step, [](Layout &l) { offsetWindow(*l.windows().front(), 1, 0); });
	CHECK(doc.undoSteps() == (std::vector<std::string>{"One", "Two", "Three"}) && doc.redoSteps().empty());
	doc.undo();
	doc.undo();
	CHECK(doc.undoSteps() == (std::vector<std::string>{"One"}) && doc.redoSteps() == (std::vector<std::string>{"Two", "Three"}));
}

static void testFolder(const char *folder)
{
	int files = 0;
	int windows = 0;
	std::error_code ec;
	for (const auto &e : std::filesystem::recursive_directory_iterator(folder, ec))
	{
		if (!e.is_regular_file(ec) || e.path().extension() != ".wnd")
			continue;
		std::ifstream in(e.path(), std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		const Layout layout = read(text);
		++files;
		CHECK(write(layout) == text);
		// Moving every top-level window and undoing gives the file back; copying gives the same windows.
		Document doc(text);
		doc.edit("Move", [](Layout &l) {
			for (Window *w : l.windows())
				offsetWindow(*w, 3, 4);
		});
		CHECK(doc.undo() && doc.text() == text);
		int copied = 0;
		for (const Window &w : readWindows(windowsText(layout.windows())))
			forEachWindow(w, [&copied](const Window &, int) { ++copied; });
		int all = 0;
		forEachWindow(layout, [&all](const Window &, int) { ++all; });
		CHECK(copied == all);
		// The layout's own scheme, applied and written to a scheme file and back, keeps a layout the game reads;
		// comparing with itself finds nothing, comparing with the schemed layout only changed windows.
		const Scheme own = schemeOf(layout, emptyScheme());
		const std::optional<Scheme> again = readScheme(writeScheme(own));
		CHECK(again && writeScheme(*again) == writeScheme(own));
		Layout schemed = read(text);
		for (Window *w : schemed.windows())
			applyScheme(own, *w);
		CHECK(write(read(write(schemed))) == write(schemed) && errorsOf(schemed) == errorsOf(layout));
		CHECK(diffLayouts(layout, layout).empty());
		for (const WindowDiff &d : diffLayouts(layout, schemed))
			CHECK(d.kind == WindowDiff::Kind::Changed && !d.fields.empty());
		forEachWindow(layout, [&windows](const Window &, int) { ++windows; });
		for (const Problem &p : check(layout))
			if (p.severity == Problem::Severity::Error)
			{
				std::printf("  %s: %s: %s\n", e.path().filename().string().c_str(), p.window.c_str(), p.text.c_str());
				CHECK(p.severity != Problem::Severity::Error);
			}
	}
	std::printf("  %d layouts, %d windows from %s\n", files, windows, folder);
	CHECK(files > 0);
}

int main(int argc, char **argv)
{
	std::printf("GuiEditorTests\n\n");
	testReadWrite();
	testEdits();
	testOddInput();
	testCheck();
	testWndEdit();
	testDocument();
	testScheme();
	testDiff();
	testArrange();
	testHistory();
	if (argc > 1)
		testFolder(argv[1]);
	std::printf("\n%d checks, %d failures\n", s_checks, s_failures);
	return s_failures == 0 ? 0 : 1;
}
