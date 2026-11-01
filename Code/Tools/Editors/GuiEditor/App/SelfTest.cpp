/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SelfTest.cpp -- the self test of the GUI editor.

#include "App/SelfTest.h"

#include "App/EditorWindow.h"
#include "App/GuiEngine.h"
#include "App/LayoutView.h"
#include "App/Picker.h"
#include "Engine/EngineApi.h"
#include "Core/WndDiff.h"
#include "Core/WndEdit.h"
#include "Core/WndScheme.h"
#include "EditorCommon/Widgets.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QListWidget>
#include <QMenu>
#include <QPixmap>
#include <QMenuBar>
#include <QTextStream>
#include <QThread>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace GuiEditorApp
{

namespace
{

constexpr std::uint32_t BACKGROUND = 0x202428;

void wait(int milliseconds)
{
	QElapsedTimer t;
	t.start();
	while (t.elapsed() < milliseconds)
	{
		QApplication::processEvents();
		QThread::msleep(10);
	}
}

// Share of the pixels that differ from the background: an empty or failed drawing shows none.
double drawnShare(const QImage &frame)
{
	if (frame.isNull())
		return 0.0;
	const QImage image = frame.convertToFormat(QImage::Format_RGB32);
	const QRgb background = qRgb(static_cast<int>((BACKGROUND >> 16) & 0xFF), static_cast<int>((BACKGROUND >> 8) & 0xFF),
		static_cast<int>(BACKGROUND & 0xFF));
	qint64 differing = 0;
	for (int y = 0; y < image.height(); ++y)
	{
		const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
		for (int x = 0; x < image.width(); ++x)
			if (std::abs(qRed(row[x]) - qRed(background)) + std::abs(qGreen(row[x]) - qGreen(background))
				+ std::abs(qBlue(row[x]) - qBlue(background)) > 12)
				++differing;
	}
	return static_cast<double>(differing) / static_cast<double>(std::max<qint64>(1, qint64(image.width()) * image.height()));
}

// Draws the current layout into the BMP file and reads it back.
QImage capture(EditorWindow &window, const QString &bmp)
{
	QFile::remove(bmp);
	wait(200);
	GuiEngine::instance().capture(bmp);
	window.view()->renderFrame();
	wait(200);
	return QImage(bmp);
}

int modelCount(const WndModel::Layout &layout)
{
	int n = 0;
	WndModel::forEachWindow(layout, [&](const WndModel::Window &, int) { ++n; });
	return n;
}

QString percent(double share)
{
	return QString::number(share * 100.0, 'f', 1);
}

std::optional<WndModel::Rect> rectOf(const EditorWindow &window, const QString &name)
{
	const std::optional<WndModel::WindowPath> path = WndModel::findWindow(window.layout(), name.toStdString());
	const WndModel::Window *w = path ? WndModel::windowAt(window.layout(), *path) : nullptr;
	return w ? w->rect() : std::nullopt;
}

const WndModel::Window *windowOf(const EditorWindow &window, const QString &name)
{
	const std::optional<WndModel::WindowPath> path = WndModel::findWindow(window.layout(), name.toStdString());
	return path ? WndModel::windowAt(window.layout(), *path) : nullptr;
}

int errorsOf(const EditorWindow &window)
{
	int n = 0;
	for (const WndModel::Problem &p : window.problems())
		n += p.severity == WndModel::Problem::Severity::Error ? 1 : 0;
	return n;
}

// Editing: a new layout with every type of window, mouse, keys, properties, clipboard, tree, saving.
template <class Line>
void testEditing(EditorWindow &window, const QDir &folder, const Line &line)
{
	window.setResolution(0);
	window.newLayout();
	line(window.layout().windows().empty() && window.engineCount() == 0 && !window.document().modified(), QStringLiteral("a new layout is empty"));
	// The background colour of the drawing.
	window.setBackground(QColor(10, 80, 30));
	const QImage empty = capture(window, folder.filePath(QStringLiteral("Background.bmp")));
	const QRgb corner = empty.isNull() ? 0 : empty.pixel(empty.width() - 1, empty.height() - 1);
	line(!empty.isNull() && qRed(corner) == 10 && qGreen(corner) == 80 && qBlue(corner) == 30,
		QStringLiteral("background colour %1, %2, %3").arg(qRed(corner)).arg(qGreen(corner)).arg(qBlue(corner)));
	window.setBackground(QColor(0x20, 0x24, 0x28));
	const QString screen = window.insertWindow(QStringLiteral("USER"));
	const QStringList types = {QStringLiteral("PUSHBUTTON"), QStringLiteral("CHECKBOX"), QStringLiteral("RADIOBUTTON"),
		QStringLiteral("SCROLLLISTBOX"), QStringLiteral("COMBOBOX"), QStringLiteral("HORZSLIDER"), QStringLiteral("VERTSLIDER"),
		QStringLiteral("PROGRESSBAR"), QStringLiteral("ENTRYFIELD"), QStringLiteral("STATICTEXT"), QStringLiteral("TABCONTROL")};
	QStringList made;
	for (int i = 0; i < types.size(); ++i)
	{
		window.setSelection({screen});
		const QString name = window.insertWindow(types[i]);
		made << name;
		// A grid, so no window covers another.
		window.nudgeSelection((i % 4) * 190, (i / 4) * 180);
	}
	wait(200);
	const int count = modelCount(window.layout());
	// The screen, every type, and the eight panes of the tab control.
	const int all = static_cast<int>(types.size()) + 1 + 8;
	line(!made.contains(QString()) && count == all && window.engineCount() == count && window.treeCount() == count && errorsOf(window) == 0,
		QStringLiteral("every type of window inserted: %1 in the model, %2 read by the game's reader, %3 errors")
			.arg(count).arg(window.engineCount()).arg(errorsOf(window)));
	const double share = drawnShare(capture(window, folder.filePath(QStringLiteral("NewLayout.bmp"))));
	line(share > 0.05, QStringLiteral("the new windows are drawn: %1 % of the picture").arg(percent(share)));

	const QStringList panes = [&window, &made] {
		QStringList names;
		if (const WndModel::Window *tab = windowOf(window, made.back()))
			for (const WndModel::Window *pane : tab->children())
				names << QString::fromStdString(pane->fullName());
		return names;
	}();
	line(panes.size() == 8 && windowOf(window, panes.value(0)) && windowOf(window, panes.value(0))->type() == "TABPANE",
		QStringLiteral("the tab control came with %1 panes").arg(panes.size()));

	// Moving and resizing with the mouse, undo and redo.
	const QString button = made.value(0);
	window.setSelection({button});
	const std::optional<WndModel::Rect> r0 = rectOf(window, button);
	const QRect p0 = GuiEngine::instance().windowRect(button);
	window.drag(p0.center(), p0.center() + QPoint(40, 30));
	const std::optional<WndModel::Rect> r1 = rectOf(window, button);
	const QRect p1 = GuiEngine::instance().windowRect(button);
	line(r0 && r1 && r1->left == r0->left + 40 && r1->top == r0->top + 30 && p1.topLeft() == p0.topLeft() + QPoint(40, 30)
			&& window.document().undoText() == "Move",
		QStringLiteral("dragged with the mouse: %1, %2 to %3, %4").arg(r0 ? r0->left : -1).arg(r0 ? r0->top : -1).arg(r1 ? r1->left : -1).arg(r1 ? r1->top : -1));
	window.drag(p1.bottomRight(), p1.bottomRight() + QPoint(20, 10));
	const std::optional<WndModel::Rect> r2 = rectOf(window, button);
	line(r1 && r2 && r2->right - r2->left == r1->right - r1->left + 20 && r2->bottom - r2->top == r1->bottom - r1->top + 10
			&& r2->left == r1->left,
		QStringLiteral("resized with the corner handle: %1 x %2").arg(r2 ? r2->right - r2->left : -1).arg(r2 ? r2->bottom - r2->top : -1));
	window.undo();
	window.undo();
	const std::optional<WndModel::Rect> r3 = rectOf(window, button);
	window.redo();
	const std::optional<WndModel::Rect> r4 = rectOf(window, button);
	line(r3 && r0 && r3->left == r0->left && r3->right == r0->right && r4 && r1 && r4->left == r1->left,
		QStringLiteral("undo and redo of moving and resizing"));
	window.nudgeSelection(1, 0);
	line(rectOf(window, button) && r4 && rectOf(window, button)->left == r4->left + 1, QStringLiteral("arrow key moves by one pixel"));

	// With the grid the window lands on it.
	window.setGrid(true, true, 10);
	const std::optional<WndModel::Rect> g0 = rectOf(window, button);
	const QRect gp = GuiEngine::instance().windowRect(button);
	window.drag(gp.center(), gp.center() + QPoint(13, 7));
	const std::optional<WndModel::Rect> g1 = rectOf(window, button);
	line(g0 && g1 && g1->left % 10 == 0 && g1->top % 10 == 0 && std::abs(g1->left - (g0->left + 13)) <= 5 && std::abs(g1->top - (g0->top + 7)) <= 5,
		QStringLiteral("snapped to the grid: %1, %2 to %3, %4").arg(g0 ? g0->left : -1).arg(g0 ? g0->top : -1).arg(g1 ? g1->left : -1).arg(g1 ? g1->top : -1));
	capture(window, folder.filePath(QStringLiteral("Grid.bmp")));
	window.undo();
	window.setGrid(false, false, 10);
	QMenu *menu = window.contextMenu();
	const int menuEntries = static_cast<int>(menu->actions().size());
	menu->deleteLater();
	line(menuEntries >= 10, QStringLiteral("context menu with %1 entries").arg(menuEntries));

	// Properties.
	window.setSelection({button});
	window.editProperty(QStringLiteral("text"), QStringLiteral("GUI:Changed"));
	window.editProperty(QStringLiteral("status:HIDDEN"), QStringLiteral("1"));
	wait(100);
	const WndModel::Window *w = windowOf(window, button);
	line(w && w->text() == "GUI:Changed" && w->hasStatus("HIDDEN"), QStringLiteral("text and status set in the properties"));
	window.editProperty(QStringLiteral("status:HIDDEN"), QStringLiteral("0"));
	window.editProperty(QStringLiteral("draw:ENABLEDDRAWDATA:0:color"), QStringLiteral("10 20 30 255"));
	wait(100);
	w = windowOf(window, button);
	line(w && !w->hasStatus("HIDDEN") && !w->drawData("ENABLEDDRAWDATA").empty() && w->drawData("ENABLEDDRAWDATA")[0].color == (WndModel::Color{10, 20, 30, 255}),
		QStringLiteral("colour set in the properties"));
	window.editProperty(QStringLiteral("name"), QStringLiteral("ButtonRenamed"));
	wait(100);
	const QString renamed = window.fileName() + QStringLiteral(":ButtonRenamed");
	line(window.selectedWindow() == renamed && windowOf(window, renamed), QStringLiteral("renamed in the properties: %1").arg(window.selectedWindow()));
	window.editProperty(QStringLiteral("name"), made.value(1).mid(made.value(1).indexOf(QLatin1Char(':')) + 1));
	wait(100);
	line(window.selectedWindow() == renamed, QStringLiteral("a name another window has is refused"));

	// Clipboard.
	window.copySelection();
	const QStringList pasted = window.paste();
	const QStringList duplicated = window.duplicateSelection();
	line(pasted.size() == 1 && duplicated.size() == 1 && pasted != duplicated && modelCount(window.layout()) == all + 2
			&& window.engineCount() == all + 2 && errorsOf(window) == 0,
		QStringLiteral("copied, pasted and duplicated: %1, %2").arg(pasted.value(0), duplicated.value(0)));
	window.deleteSelection();
	window.setSelection(pasted);
	window.cutSelection();
	line(modelCount(window.layout()) == all && window.engineCount() == all, QStringLiteral("deleted and cut"));
	window.undo();
	line(modelCount(window.layout()) == all + 1 && windowOf(window, pasted.value(0)), QStringLiteral("undo brings the cut window back"));
	window.redo();

	// The tree: out to the top level, back in, order among the siblings.
	window.moveInto({renamed}, QString(), 99);
	const std::optional<WndModel::WindowPath> top = WndModel::findWindow(window.layout(), renamed.toStdString());
	line(top && top->size() == 1 && window.layout().windows().size() == 2 && window.engineCount() == all && errorsOf(window) == 0,
		QStringLiteral("moved to the top level"));
	window.moveInto({renamed}, screen, 0);
	window.setSelection({renamed});
	window.reorderSelectionToEnd(true);
	const std::optional<WndModel::WindowPath> front = WndModel::findWindow(window.layout(), renamed.toStdString());
	line(front && front->size() == 2 && static_cast<int>(front->back()) == static_cast<int>(types.size()) - 1 && window.engineCount() == all, QStringLiteral("back inside, brought to the front"));

	// Gadget data and text colours value by value; names the game does not know show up as problems.
	const QString slider = made.value(5);
	window.setSelection({slider});
	window.editProperty(QStringLiteral("data:SLIDERDATA:1"), QStringLiteral("50"));
	window.editProperty(QStringLiteral("datacolor:TEXTCOLOR:0"), QStringLiteral("1 2 3 255"));
	wait(100);
	const WndModel::Window *sw = windowOf(window, slider);
	line(sw && sw->value("SLIDERDATA") == "MINVALUE: 0, MAXVALUE: 50" && !sw->dataFields("TEXTCOLOR").empty()
			&& sw->dataFields("TEXTCOLOR")[0].second == "1 2 3 255",
		QStringLiteral("slider range and text colour set value by value: %1").arg(sw ? QString::fromStdString(sw->value("SLIDERDATA")) : QString()));
	const auto warnings = [&window](const char *what) {
		int n = 0;
		for (const WndModel::Problem &p : window.problems())
			n += p.severity == WndModel::Problem::Severity::Warning && p.text.find(what) != std::string::npos ? 1 : 0;
		return n;
	};
	const int textsBefore = warnings("string table");
	const int imagesBefore = warnings("game data");
	const int callbacksBefore = warnings("function of the game");
	window.editProperty(QStringLiteral("text"), QStringLiteral("GUI:NoSuchText"));
	window.editProperty(QStringLiteral("draw:ENABLEDDRAWDATA:0:image"), QStringLiteral("NoSuchImage"));
	window.editProperty(QStringLiteral("callback:SYSTEMCALLBACK"), QStringLiteral("NoSuchFunction"));
	wait(100);
	const int texts = warnings("string table") - textsBefore;
	const int images = warnings("game data") - imagesBefore;
	const int callbacks = warnings("function of the game") - callbacksBefore;
	line(texts == 1 && images == 1 && callbacks == 1 && errorsOf(window) == 0,
		QStringLiteral("unknown text, image and callback listed as problems: %1, %2, %3").arg(texts).arg(images).arg(callbacks));
	window.undo();
	window.undo();
	window.undo();

	// Saving: names carry the new file, the game's reader reads the file back.
	const QString saved = folder.filePath(QStringLiteral("Saved.wnd"));
	QFile::remove(saved);
	const bool ok = window.saveAs(saved);
	bool prefixed = true;
	for (const std::string &n : WndModel::fullNames(window.layout()))
		prefixed = prefixed && n.rfind("Saved.wnd:", 0) == 0;
	line(ok && QFileInfo::exists(saved) && prefixed && !window.document().modified() && window.fileName() == QStringLiteral("Saved.wnd"),
		QStringLiteral("saved as %1 with the file in every name").arg(QFileInfo(saved).fileName()));
	QFile file(saved);
	const QByteArray bytes = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
	const int reread = window.openLayout(saved);
	line(reread == all && modelCount(window.layout()) == all && window.document().text() == bytes.toStdString() && errorsOf(window) == 0,
		QStringLiteral("the saved file read again: %1 windows by the game's reader").arg(reread));

	// Comparing with the saved version follows every change.
	QString pushButton;
	for (const std::string &n : WndModel::fullNames(window.layout()))
		if (const WndModel::Window *candidate = windowOf(window, QString::fromStdString(n)); candidate && candidate->type() == "PUSHBUTTON" && pushButton.isEmpty())
			pushButton = QString::fromStdString(n);
	const int same = window.compareWith(QString());
	window.setSelection({pushButton});
	window.nudgeSelection(3, 0);
	const std::vector<WndModel::WindowDiff> moved = window.differences();
	window.undo();
	line(same == 0 && moved.size() == 1 && moved[0].kind == WndModel::WindowDiff::Kind::Changed && moved[0].fields.size() == 1
			&& moved[0].fields[0].key == "SCREENRECT" && window.differences().empty() && EditorCommon::child<QTreeWidget>(&window, "CompareTree"),
		QStringLiteral("compared with the saved version: %1, after a move %2, after undo %3").arg(same).arg(moved.size()).arg(window.differences().size()));
	window.stopCompare();

	// Schemes: the layout's own written and read back, a changed one applied to the layout and to new windows.
	const QString own = folder.filePath(QStringLiteral("Saved.ls"));
	const bool written = window.saveLayoutScheme(own);
	line(written && window.scheme() && window.scheme()->states.size() == WndModel::schemeStateCount() && window.loadScheme(own),
		QStringLiteral("the layout's scheme saved as %1 and loaded").arg(QFileInfo(own).fileName()));
	WndModel::Scheme other = *window.scheme();
	const WndModel::Color red{200, 30, 30, 255};
	for (std::size_t i = 0; i < other.states.size(); ++i)
		if (WndModel::schemeStateName(i) == "BUTTON_ENABLED")
			other.states[i].color = red;
	other.enabledText = WndModel::Color{1, 2, 3, 255};
	const QString otherPath = folder.filePath(QStringLiteral("Red.ls"));
	QFile otherFile(otherPath);
	const std::string otherText = WndModel::writeScheme(other);
	const bool otherWritten = otherFile.open(QIODevice::WriteOnly) && otherFile.write(otherText.data(), static_cast<qint64>(otherText.size())) > 0;
	otherFile.close();
	const bool loaded = otherWritten && window.loadScheme(otherPath);
	const int schemed = window.applyScheme(false);
	wait(100);
	const WndModel::Window *b = windowOf(window, pushButton);
	line(loaded && schemed >= 1 && b && !b->drawData("ENABLEDDRAWDATA").empty() && b->drawData("ENABLEDDRAWDATA")[0].color == red
			&& window.engineCount() == all && errorsOf(window) == 0,
		QStringLiteral("a scheme applied to the layout: %1 windows changed").arg(schemed));
	window.undo();
	window.setSelection({});
	const QString fresh = window.insertWindow(QStringLiteral("PUSHBUTTON"));
	const WndModel::Window *f = windowOf(window, fresh);
	line(f && !f->drawData("ENABLEDDRAWDATA").empty() && f->drawData("ENABLEDDRAWDATA")[0].color == red && !f->dataFields("TEXTCOLOR").empty()
			&& f->dataFields("TEXTCOLOR")[0].second == "1 2 3 255",
		QStringLiteral("a new window takes the scheme's colours"));
	window.undo();
	window.clearScheme();
}

} // namespace

// Test mode: a button pressed, a box ticked, text typed, as the game's windows report it; the layout stays.
template <class Line>
void testTestMode(EditorWindow &window, const Line &line)
{
	window.newLayout();
	const QString parent = window.insertWindow(QStringLiteral("USER"));
	const auto add = [&](const char *type, int dy) {
		window.setSelection({parent});
		const QString name = window.insertWindow(QString::fromLatin1(type));
		window.nudgeSelection(0, dy);
		return name;
	};
	const QString button = add("PUSHBUTTON", 0);
	const QString box = add("CHECKBOX", 60);
	const QString entry = add("ENTRYFIELD", 120);
	const std::string before = window.document().text();
	const int count = window.engineCount();
	wait(200);
	const auto click = [&window](const QString &name) {
		const QPoint c = GuiEngine::instance().windowRect(name).center();
		window.testInput(ME_INPUT_MOVE, c.x(), c.y(), 0);
		window.testInput(ME_INPUT_LEFT_DOWN, c.x(), c.y(), 0);
		window.testInput(ME_INPUT_LEFT_UP, c.x(), c.y(), 0);
	};
	window.setTestMode(true);
	click(button);
	click(box);
	click(entry);
	for (const char c : {'h', 'i'})
		window.testInput(ME_INPUT_CHAR, 0, 0, c);
	wait(100);
	const QStringList log = window.testLog();
	const auto has = [&log](const QString &name, const char *message, const QString &text) {
		const QString shortName = name.section(QLatin1Char(':'), -1);
		for (const QString &l : log)
			if (l.startsWith(shortName + QLatin1Char(' ')) && l.contains(QLatin1String(message)) && l.contains(text))
				return true;
		return false;
	};
	line(window.testMode() && has(button, "GBM_SELECTED", QString()) && has(box, "GBM_SELECTED", QStringLiteral("ticked"))
			&& has(entry, "GEM_UPDATE_TEXT", QStringLiteral("hi")),
		QStringLiteral("test mode: button, box and text field report as in the game (%1)").arg(log.join(QStringLiteral("; "))));
	window.setTestMode(false);
	line(!window.testMode() && window.document().text() == before && window.engineCount() == count,
		QStringLiteral("test mode left: the layout as it was, %1 windows").arg(window.engineCount()));
}

// Arranging several windows, guides while dragging, the history, autosave, hiding and locking in the editor,
// one value for several windows.
template <class Line>
void testArranging(EditorWindow &window, const QDir &folder, const Line &line)
{
	window.newLayout();
	const QString parent = window.insertWindow(QStringLiteral("USER"));
	const auto add = [&](int dx, int dy) {
		window.setSelection({parent});
		const QString name = window.insertWindow(QStringLiteral("PUSHBUTTON"));
		window.nudgeSelection(dx, dy);
		return name;
	};
	const QString a = add(0, 0);
	const QString b = add(150, 60);
	const QString c = add(300, 130);
	const std::string start = window.document().text();
	const int count = window.engineCount();

	window.setSelection({a, b, c});
	const bool aligned = window.alignSelection(WndModel::Align::Left);
	const auto left = [&window](const QString &n) { return rectOf(window, n) ? rectOf(window, n)->left : -1; };
	const auto top = [&window](const QString &n) { return rectOf(window, n) ? rectOf(window, n)->top : -1; };
	line(aligned && left(a) == left(c) && left(b) == left(c) && top(b) != top(c),
		QStringLiteral("aligned to the last selected window: left edges at %1").arg(left(c)));
	window.undo();
	const bool spread = window.distributeSelection(false);
	line(top(b) - top(a) == top(c) - top(b) || !spread, QStringLiteral("spread vertically: tops %1, %2, %3").arg(top(a)).arg(top(b)).arg(top(c)));
	if (spread)
		window.undo();

	// One value for every selected button.
	window.setSelection({a, b});
	window.editProperty(QStringLiteral("datacolor:TEXTCOLOR:0"), QStringLiteral("7 8 9 255"));
	wait(100);
	const WndModel::Window *wa = windowOf(window, a);
	const WndModel::Window *wb = windowOf(window, b);
	line(wa && wb && !wa->dataFields("TEXTCOLOR").empty() && !wb->dataFields("TEXTCOLOR").empty()
			&& wa->dataFields("TEXTCOLOR")[0].second == "7 8 9 255" && wb->dataFields("TEXTCOLOR")[0].second == "7 8 9 255",
		QStringLiteral("one text colour for two selected buttons"));
	window.undo();

	// Guides: drag b so its left edge ends 2 pixels right of a's; it snaps onto a's edge.
	window.setSelection({b});
	wait(100);
	const QRect rb = GuiEngine::instance().windowRect(b);
	const QPoint from = rb.center();
	const QPoint to = from + QPoint(left(a) + 2 - left(b), 0);
	window.drag(from, to);
	line(left(b) == left(a), QStringLiteral("dragged near another window, snapped to its edge: %1 and %2").arg(left(b)).arg(left(a)));

	// The history: back to the start and forward again.
	const std::string moved = window.document().text();
	const int steps = static_cast<int>(window.document().undoSteps().size());
	window.goToStep(0);
	const bool atStart = window.document().text() == WndModel::Document().text();
	window.goToStep(steps);
	line(steps >= 4 && atStart && window.document().text() == moved,
		QStringLiteral("history: %1 steps, back to the start and forward again").arg(steps));

	// Autosave of the unsaved layout.
	const bool saved = window.autosaveNow();
	QFile copy(window.autosavePath());
	const QByteArray copyText = copy.open(QIODevice::ReadOnly) ? copy.readAll() : QByteArray();
	copy.close();
	line(saved && copyText.toStdString() == window.document().text(), QStringLiteral("autosave copy written: %1").arg(QFileInfo(window.autosavePath()).fileName()));
	QFile::remove(window.autosavePath());

	// Hidden in the editor: the engine draws one window less, the file keeps it; locked: a click passes through.
	window.setHiddenInEditor(c, true);
	const int hiddenCount = window.engineCount();
	window.setHiddenInEditor(c, false);
	window.setLocked(a, true);
	wait(100);
	const QPoint inA = GuiEngine::instance().windowRect(a).center();
	const QString clicked = window.selectAt(inA.x(), inA.y());
	window.setLocked(a, false);
	line(hiddenCount == count - 1 && window.engineCount() == count && clicked != a,
		QStringLiteral("hidden in the editor: %1 of %2 windows drawn; locked window not selected by a click (%3)")
			.arg(hiddenCount).arg(count).arg(clicked.isEmpty() ? QStringLiteral("none") : clicked));
	(void)start;

	// A background picture under the layout: an empty layout shows it in the corner.
	QImage red(64, 64, QImage::Format_RGBA8888);
	red.fill(QColor(220, 30, 30));
	const QString picture = folder.filePath(QStringLiteral("Background.png"));
	red.save(picture);
	window.newLayout();
	const bool set = window.setBackgroundImage(picture);
	const QImage shown = capture(window, folder.filePath(QStringLiteral("background.bmp")));
	const QRgb corner = shown.isNull() ? 0 : shown.pixel(5, 5);
	window.setBackgroundImage(QString());
	line(set && qRed(corner) > 180 && qGreen(corner) < 80 && qBlue(corner) < 80,
		QStringLiteral("background picture drawn under the layout: %1, %2, %3").arg(qRed(corner)).arg(qGreen(corner)).arg(qBlue(corner)));
}

namespace
{

// The windows as the file writes them: one line WINDOW each, children included.
int windowLines(const QByteArray &text)
{
	int n = 0;
	for (const QByteArray &l : text.split('\n'))
		n += l.trimmed() == "WINDOW" ? 1 : 0;
	return n;
}

// Without the engine: layouts read into model and tree, properties, language and theme, editing, saving,
// comparing, schemes; the window itself is the picture. Returns the number of pictures.
template <class Line>
int testWithoutEngine(EditorWindow &window, const QDir &folder, const QStringList &layouts, const Line &line)
{
	int images = 0;
	line(!layouts.isEmpty(), QStringLiteral("%1 layouts given with --layout").arg(layouts.size()));
	QString largest;
	int most = 0;
	for (const QString &layout : layouts)
	{
		QFile file(layout);
		const QByteArray text = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
		window.openLayout(layout);
		const int model = modelCount(window.layout());
		line(model > 0 && model == windowLines(text) && window.treeCount() == model && errorsOf(window) == 0,
			QStringLiteral("%1: %2 windows in the file, %3 in the model, %4 in the tree, %5 errors")
				.arg(QFileInfo(layout).fileName()).arg(windowLines(text)).arg(model).arg(window.treeCount()).arg(errorsOf(window)));
		if (model > most)
		{
			most = model;
			largest = layout;
		}
	}
	if (!largest.isEmpty())
		window.openLayout(largest);

	QString button;
	WndModel::forEachWindow(window.layout(), [&](const WndModel::Window &w, int) {
		if (button.isEmpty() && w.type() == "PUSHBUTTON")
			button = QString::fromLatin1(w.fullName().c_str());
	});
	if (button.isEmpty() && !window.layout().windows().empty())
		button = QString::fromLatin1(window.layout().windows().front()->fullName().c_str());
	const bool selected = window.selectWindow(button);
	line(selected && window.selectedWindow() == button && window.propertyCount() >= 10,
		QStringLiteral("%1 selected: %2 properties").arg(button).arg(window.propertyCount()));
	// Menus and toolbar are rebuilt with the layout; their layout runs in the event loop.
	wait(200);
	int buttons = 0;
	QStringList shown;
	if (auto *bar = window.findChild<QToolBar *>(QStringLiteral("MainToolBar")))
		for (QToolButton *tb : bar->findChildren<QToolButton *>())
		{
			if (tb->isVisible() && tb->width() > 4 && (!tb->text().isEmpty() || !tb->icon().isNull()))
			{
				++buttons;
				shown << tb->text();
			}
		}
	line(buttons >= 7, QStringLiteral("toolbar shows %1 buttons: %2").arg(buttons).arg(shown.join(QStringLiteral(", "))));
	const QImage picture = window.grab().toImage();
	picture.save(folder.filePath(QStringLiteral("Window.png")));
	++images;
	line(!picture.isNull() && picture.width() >= 800, QStringLiteral("picture of the editor: %1 x %2").arg(picture.width()).arg(picture.height()));

	window.setLanguage(Language::German);
	const QString german = window.menuBar()->actions().isEmpty() ? QString() : window.menuBar()->actions().front()->text();
	window.setTheme(ThemeKind::Light);
	wait(200);
	window.grab().save(folder.filePath(QStringLiteral("WindowLight.png")));
	++images;
	window.setTheme(ThemeKind::Dark);
	window.setLanguage(Language::English);
	const QString english = window.menuBar()->actions().isEmpty() ? QString() : window.menuBar()->actions().front()->text();
	line(german == QStringLiteral("&Datei") && english == QStringLiteral("&File") && window.selectedWindow() == button
			&& window.treeCount() == modelCount(window.layout()),
		QStringLiteral("language and theme switched (%1, %2), layout and selection kept").arg(german, english));

	// A new layout with every type of window.
	window.newLayout();
	line(window.layout().windows().empty() && window.treeCount() == 0 && !window.document().modified(), QStringLiteral("a new layout is empty"));
	const QString screen = window.insertWindow(QStringLiteral("USER"));
	const QStringList types = {QStringLiteral("PUSHBUTTON"), QStringLiteral("CHECKBOX"), QStringLiteral("RADIOBUTTON"),
		QStringLiteral("SCROLLLISTBOX"), QStringLiteral("COMBOBOX"), QStringLiteral("HORZSLIDER"), QStringLiteral("VERTSLIDER"),
		QStringLiteral("PROGRESSBAR"), QStringLiteral("ENTRYFIELD"), QStringLiteral("STATICTEXT"), QStringLiteral("TABCONTROL")};
	QStringList made;
	for (int i = 0; i < types.size(); ++i)
	{
		window.setSelection({screen});
		made << window.insertWindow(types[i]);
		window.nudgeSelection((i % 4) * 190, (i / 4) * 180);
	}
	const int all = static_cast<int>(types.size()) + 1 + 8;
	line(!made.contains(QString()) && modelCount(window.layout()) == all && window.treeCount() == all && errorsOf(window) == 0,
		QStringLiteral("every type of window inserted: %1 in the model, %2 in the tree, %3 errors")
			.arg(modelCount(window.layout())).arg(window.treeCount()).arg(errorsOf(window)));

	// Arrow keys, undo and redo.
	const QString first = made.value(0);
	window.setSelection({first});
	const std::optional<WndModel::Rect> r0 = rectOf(window, first);
	window.nudgeSelection(5, 3);
	const std::optional<WndModel::Rect> r1 = rectOf(window, first);
	window.undo();
	const std::optional<WndModel::Rect> r2 = rectOf(window, first);
	window.redo();
	const std::optional<WndModel::Rect> r3 = rectOf(window, first);
	line(r0 && r1 && r2 && r3 && r1->left == r0->left + 5 && r1->top == r0->top + 3 && r2->left == r0->left && r3->left == r1->left,
		QStringLiteral("moved by keys, undone and redone"));

	// Properties.
	window.editProperty(QStringLiteral("text"), QStringLiteral("GUI:Changed"));
	window.editProperty(QStringLiteral("status:HIDDEN"), QStringLiteral("1"));
	window.editProperty(QStringLiteral("draw:ENABLEDDRAWDATA:0:color"), QStringLiteral("10 20 30 255"));
	wait(100);
	const WndModel::Window *w = windowOf(window, first);
	line(w && w->text() == "GUI:Changed" && w->hasStatus("HIDDEN") && !w->drawData("ENABLEDDRAWDATA").empty()
			&& w->drawData("ENABLEDDRAWDATA")[0].color == (WndModel::Color{10, 20, 30, 255}),
		QStringLiteral("text, status and colour set in the properties"));
	window.editProperty(QStringLiteral("name"), QStringLiteral("ButtonRenamed"));
	wait(100);
	const QString renamed = window.fileName() + QStringLiteral(":ButtonRenamed");
	window.editProperty(QStringLiteral("name"), made.value(1).mid(made.value(1).indexOf(QLatin1Char(':')) + 1));
	wait(100);
	line(window.selectedWindow() == renamed && windowOf(window, renamed), QStringLiteral("renamed in the properties, a taken name refused: %1").arg(window.selectedWindow()));

	// Clipboard and tree.
	window.copySelection();
	const QStringList pasted = window.paste();
	const QStringList duplicated = window.duplicateSelection();
	line(pasted.size() == 1 && duplicated.size() == 1 && pasted != duplicated && modelCount(window.layout()) == all + 2
			&& window.treeCount() == all + 2 && errorsOf(window) == 0,
		QStringLiteral("copied, pasted and duplicated: %1, %2").arg(pasted.value(0), duplicated.value(0)));
	window.deleteSelection();
	window.setSelection(pasted);
	window.cutSelection();
	const int afterCut = modelCount(window.layout());
	window.undo();
	line(afterCut == all && modelCount(window.layout()) == all + 1 && windowOf(window, pasted.value(0)), QStringLiteral("deleted, cut and brought back by undo"));
	window.redo();
	window.moveInto({renamed}, QString(), 99);
	const std::optional<WndModel::WindowPath> top = WndModel::findWindow(window.layout(), renamed.toStdString());
	window.moveInto({renamed}, screen, 0);
	const std::optional<WndModel::WindowPath> inside = WndModel::findWindow(window.layout(), renamed.toStdString());
	line(top && top->size() == 1 && inside && inside->size() == 2 && window.treeCount() == all && errorsOf(window) == 0,
		QStringLiteral("moved to the top level and back inside"));

	// Saving and reading back, comparing with the saved version.
	const QString saved = folder.filePath(QStringLiteral("Saved.wnd"));
	QFile::remove(saved);
	const bool ok = window.saveAs(saved);
	QFile file(saved);
	const QByteArray bytes = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
	window.openLayout(saved);
	line(ok && window.fileName() == QStringLiteral("Saved.wnd") && modelCount(window.layout()) == all && windowLines(bytes) == all
			&& window.document().text() == bytes.toStdString() && !window.document().modified(),
		QStringLiteral("saved as %1 and read again: %2 windows").arg(QFileInfo(saved).fileName()).arg(modelCount(window.layout())));
	QString pushButton;
	WndModel::forEachWindow(window.layout(), [&](const WndModel::Window &x, int) {
		if (pushButton.isEmpty() && x.type() == "PUSHBUTTON")
			pushButton = QString::fromLatin1(x.fullName().c_str());
	});
	const int same = window.compareWith(QString());
	window.setSelection({pushButton});
	window.nudgeSelection(3, 0);
	const std::size_t moved = window.differences().size();
	window.undo();
	line(same == 0 && moved == 1 && window.differences().empty(),
		QStringLiteral("compared with the saved version: %1, after a move %2, after undo %3").arg(same).arg(moved).arg(window.differences().size()));
	window.stopCompare();

	// The layout's scheme written and loaded again.
	const QString own = folder.filePath(QStringLiteral("Saved.ls"));
	line(window.saveLayoutScheme(own) && window.loadScheme(own) && window.scheme() && window.scheme()->states.size() == WndModel::schemeStateCount(),
		QStringLiteral("the layout's scheme saved as %1 and loaded").arg(QFileInfo(own).fileName()));
	window.clearScheme();
	wait(200);
	window.grab().save(folder.filePath(QStringLiteral("Edited.png")));
	++images;
	return images;
}

} // namespace

Options parseOptions(const QStringList &args)
{
	Options o;
	for (int i = 1; i < args.size(); ++i)
	{
		const QString &a = args[i];
		const bool hasValue = i + 1 < args.size();
		if (a == QLatin1String("--data") && hasValue)
			o.data = args[++i];
		else if (a == QLatin1String("--game") && hasValue)
			o.game = args[++i].compare(QLatin1String("generals"), Qt::CaseInsensitive) == 0 ? EditorCommon::Game::Generals
																							: EditorCommon::Game::ZeroHour;
		else if (a == QLatin1String("--selftest") && hasValue)
			o.selftest = args[++i];
		else if (a == QLatin1String("--no-engine"))
			o.engine = false;
		else if (a == QLatin1String("--layout") && hasValue)
			o.layouts << args[++i];
		else if (!a.startsWith(QLatin1Char('-')))	// -vulkan and other engine switches are not layouts
			o.layouts << a;
	}
	return o;
}

int runSelfTest(EditorWindow &window, Options options)
{
	// The engine makes the data folder the working directory: every path must be absolute before it starts.
	QDir folder(QDir(options.selftest).absolutePath());
	folder.mkpath(QStringLiteral("."));
	if (!options.data.isEmpty())
		options.data = QDir(options.data).absolutePath();
	QFile logFile(folder.filePath(QStringLiteral("selftest.log")));
	if (!logFile.open(QIODevice::WriteOnly | QIODevice::Text))
		qWarning("selftest.log cannot be written");
	QTextStream log(&logFile);
	int images = 0;
	int failed = 0;
	const auto line = [&](bool ok, const QString &text) {
		const QString out = (ok ? QStringLiteral("ok   ") : QStringLiteral("FAIL ")) + text;
		log << out << '\n';
		log.flush();
		std::printf("%s\n", out.toLocal8Bit().constData());
		std::fflush(stdout);
		if (!ok)
			++failed;
		return ok;
	};

	window.show();
	wait(300);
	if (!options.engine)
	{
		images = testWithoutEngine(window, folder, options.layouts, line);
		const QString end = QStringLiteral("%1 images, %2 failed").arg(images).arg(failed);
		log << end << '\n';
		std::printf("%s\n", end.toLocal8Bit().constData());
		return failed == 0 && images > 0 ? 0 : 1;
	}
	QString error;
	const bool started = window.startEngine(options.data, folder.filePath(QStringLiteral("engine.log")), &error);
	line(started, QStringLiteral("engine, view and window system started %1").arg(error));
	if (started)
	{
		// Without --layout: three layouts of the game. The options menu is mostly gadgets; the main menu is
		// not used because the game shows most of it through transitions. The score screen starts with all
		// windows hidden (the game shows them as the game ends), so it must draw nothing.
		const bool viewer = options.layouts.isEmpty();
		QStringList hidden;
		if (viewer)
		{
			options.layouts = {QStringLiteral("Menus/OptionsMenu.wnd"), QStringLiteral("ControlBar.wnd"), QStringLiteral("Menus/ScoreScreen.wnd")};
			hidden << QStringLiteral("Menus/ScoreScreen.wnd");
		}
		for (const QString &layout : options.layouts)
		{
			const int count = window.openLayout(layout);
			line(count > 0, QStringLiteral("%1 read: %2 windows").arg(layout).arg(count));
			if (count <= 0)
				continue;
			const int model = modelCount(window.layout());
			line(model == count && window.treeCount() == count,
				QStringLiteral("%1: the layout model and the tree have the windows of the game's reader (%2, %3, %4)")
					.arg(layout).arg(model).arg(window.treeCount()).arg(count));
			// Without the editor's outlines, so the picture shows only what the game draws.
			window.setOutlines(false);
			const double share = drawnShare(capture(window, folder.filePath(QFileInfo(layout).completeBaseName() + QStringLiteral(".bmp"))));
			window.setOutlines(true);
			++images;
			if (hidden.contains(layout))
			{
				line(share < 0.001, QStringLiteral("%1 with hidden windows draws nothing: %2 % differ from the background").arg(layout, percent(share)));
				const double outlined = drawnShare(capture(window, folder.filePath(QFileInfo(layout).completeBaseName() + QStringLiteral("Outlines.bmp"))));
				line(outlined > share, QStringLiteral("%1: the outlines of the hidden windows show: %2 %").arg(layout, percent(outlined)));
			}
			else
				line(share > 0.01, QStringLiteral("%1 drawn: %2 % of the picture differ from the background").arg(layout, percent(share)));
		}

		if (viewer)
		{
			// Layouts of the game data, loose and in archives.
			const QStringList layouts = window.gameLayouts();
			const auto has = [&](const char *name) {
				return std::any_of(layouts.begin(), layouts.end(), [&](const QString &l) { return l.compare(QLatin1String(name), Qt::CaseInsensitive) == 0; });
			};
			line(layouts.size() >= 10 && has("Menus/OptionsMenu.wnd") && has("ControlBar.wnd"),
				QStringLiteral("game layouts listed: %1").arg(layouts.size()));

			// Hidden windows: the main menu shows little until the game's transitions run.
			const int mainCount = window.open({LayoutSource::Kind::Game, QStringLiteral("Menus/MainMenu.wnd")});
			const double plain = drawnShare(capture(window, folder.filePath(QStringLiteral("MainMenu.bmp"))));
			window.setShowHidden(true);
			const double shown = drawnShare(capture(window, folder.filePath(QStringLiteral("MainMenuShown.bmp"))));
			window.setShowHidden(false);
			line(mainCount > 0 && shown > plain + 0.01,
				QStringLiteral("hidden windows shown on request: %1 % instead of %2 % drawn").arg(percent(shown), percent(plain)));

			// What the game data offer for the pickers.
			const GuiEngine &engine = GuiEngine::instance();
			const std::vector<GuiEngine::ImageInfo> imageList = engine.images();
			const QImage preview = engine.imagePreview(QStringLiteral("BtnM_N"), 32);
			const double previewShare = preview.isNull() ? 0.0 : [&preview] {
				int opaque = 0;
				for (int y = 0; y < preview.height(); ++y)
					for (int x = 0; x < preview.width(); ++x)
						opaque += qAlpha(preview.pixel(x, y)) > 0 ? 1 : 0;
				return static_cast<double>(opaque) / (preview.width() * preview.height());
			}();
			bool exists = false;
			const QString cancel = engine.text(QStringLiteral("GUI:Cancel"), &exists);
			line(imageList.size() >= 20 && previewShare > 0.1 && engine.callbacks("system").size() >= 10 && engine.callbacks("init").size() >= 5
					&& engine.textKeys().size() >= 100 && exists && !cancel.isEmpty(),
				QStringLiteral("catalogs: %1 images (preview %2 % covered), %3 system and %4 init callbacks, %5 texts (GUI:Cancel = %6), %7 header templates (the test data have none)")
					.arg(imageList.size()).arg(percent(previewShare)).arg(engine.callbacks("system").size()).arg(engine.callbacks("init").size())
					.arg(engine.textKeys().size()).arg(cancel).arg(engine.headerTemplates().size()));

			{
				// The image picker loads its pictures a few at a time.
				std::vector<Picker::Item> items;
				for (const GuiEngine::ImageInfo &i : imageList)
					items.push_back({i.name, QStringLiteral("%1 x %2").arg(i.width).arg(i.height)});
				Picker picker(&window, QStringLiteral("Image"), std::move(items), QString(),
					[&engine](const QString &name) { return QIcon(QPixmap::fromImage(engine.imagePreview(name, 48))); }, 48);
				picker.show();
				wait(1500);
				auto *list = picker.findChild<QListWidget *>(QStringLiteral("PickerList"));
				int pictures = 0;
				for (int i = 0; list && i < list->count(); ++i)
					pictures += list->item(i)->icon().isNull() ? 0 : 1;
				picker.grab().save(folder.filePath(QStringLiteral("Picker.png")));
				line(list && pictures == list->count() && pictures == static_cast<int>(imageList.size()),
					QStringLiteral("image picker shows %1 pictures").arg(pictures));
				picker.reject();
			}

			// Selecting by a click into the view, properties, problems.
			window.open({LayoutSource::Kind::Game, QStringLiteral("Menus/OptionsMenu.wnd")});
			int errors = 0;
			for (const WndModel::Problem &p : window.problems())
				errors += p.severity == WndModel::Problem::Severity::Error ? 1 : 0;
			line(errors == 0, QStringLiteral("Menus/OptionsMenu.wnd: %1 errors in the problem list").arg(errors));
			QString button;
			WndModel::forEachWindow(window.layout(), [&](const WndModel::Window &w, int) {
				if (button.isEmpty() && w.type() == "PUSHBUTTON" && !w.hasStatus("HIDDEN"))
					button = QString::fromLatin1(w.fullName().c_str());
			});
			const QRect at800 = GuiEngine::instance().windowRect(button);
			const QString hit = at800.isEmpty() ? QString() : window.selectAt(at800.center().x(), at800.center().y());
			line(!button.isEmpty() && hit == button && window.selectedWindow() == button,
				QStringLiteral("a click on %1 selects it (got %2)").arg(button, hit));
			window.grab().save(folder.filePath(QStringLiteral("Window.png")));
			line(window.propertyCount() >= 15, QStringLiteral("properties of the selected window: %1 rows").arg(window.propertyCount()));

			// Resolutions: the game places the windows for the size of the display.
			window.setResolution(1);
			const QRect at1024 = GuiEngine::instance().windowRect(button);
			const QImage big = capture(window, folder.filePath(QStringLiteral("OptionsMenu1024.bmp")));
			const double scale = at800.width() > 0 ? static_cast<double>(at1024.width()) / at800.width() : 0.0;
			line(std::abs(scale - 1024.0 / 800.0) < 0.03 && big.width() == 1024 && big.height() == 768 && window.selectedWindow() == button,
				QStringLiteral("1024 x 768: picture %1 x %2, windows scaled by %3, selection kept")
					.arg(big.width()).arg(big.height()).arg(scale, 0, 'f', 3));
			window.setResolution(0);

			// Language and theme rebuild the window without losing the layout or the selection.
			window.setLanguage(Language::German);
			const QString german = window.menuBar()->actions().isEmpty() ? QString() : window.menuBar()->actions().front()->text();
			window.setTheme(ThemeKind::Light);
			window.setTheme(ThemeKind::Dark);
			window.setLanguage(Language::English);
			const QString english = window.menuBar()->actions().isEmpty() ? QString() : window.menuBar()->actions().front()->text();
			line(german == QStringLiteral("&Datei") && english == QStringLiteral("&File") && window.selectedWindow() == button
					&& window.treeCount() == modelCount(window.layout()),
				QStringLiteral("language and theme switched (%1, %2), layout and selection kept").arg(german, english));

			// The same layout as a file of its own.
			const QString file = QDir(options.data).filePath(QStringLiteral("Window/Menus/OptionsMenu.wnd"));
			if (QFileInfo::exists(file))
			{
				const int fromFile = window.openLayout(file);
				line(fromFile == modelCount(window.layout()) && window.source().kind == LayoutSource::Kind::File,
					QStringLiteral("opened as a file: %1 windows").arg(fromFile));
			}
			testEditing(window, folder, line);
			testTestMode(window, line);
			testArranging(window, folder, line);
		}
	}
	QFile report(folder.filePath(QStringLiteral("engine-report.txt")));
	if (report.open(QIODevice::WriteOnly | QIODevice::Text))
		report.write(GuiEngine::instance().report().toLocal8Bit());
	GuiEngine::instance().stop();
	const QString end = QStringLiteral("%1 images, %2 failed").arg(images).arg(failed);
	log << end << '\n';
	std::printf("%s\n", end.toLocal8Bit().constData());
	return failed == 0 && images > 0 ? 0 : 1;
}

} // namespace GuiEditorApp
