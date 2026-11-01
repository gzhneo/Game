/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EditorEditing.cpp -- the main window of the GUI editor: selection, mouse and keys in the drawing, and the
// changes to the windows (each one step for undo).

#include "App/EditorWindow.h"

#include "App/EditorShared.h"
#include "App/GuiEngine.h"
#include "App/LayoutView.h"
#include "App/WindowTree.h"
#include "EditorCommon/Language.h"
#include "EditorCommon/Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>

#include <algorithm>
#include <cmath>

namespace GuiEditorApp
{

using namespace EditorCommon;

namespace
{

constexpr int HANDLE = 7;			// size of a resize handle, pixels
constexpr int GUIDE_SNAP = 4;		// layout pixels a dragged window snaps to the edges and centres of others
constexpr int DRAG_START = 3;		// pixels the mouse moves before a press becomes a drag
constexpr int LIVE_INTERVAL = 40;	// milliseconds between two drawings while dragging
constexpr int MIN_SIZE = 4;			// smallest window, layout pixels
constexpr int PASTE_OFFSET = 10;	// pasted copies of windows that are still there move by this much

// Names of new windows by type.
std::string baseName(const std::string &type)
{
	if (type == "USER")
		return "Window";
	if (type == "PUSHBUTTON")
		return "Button";
	if (type == "CHECKBOX")
		return "CheckBox";
	if (type == "RADIOBUTTON")
		return "RadioButton";
	if (type == "SCROLLLISTBOX")
		return "ListBox";
	if (type == "COMBOBOX")
		return "ComboBox";
	if (type == "HORZSLIDER" || type == "VERTSLIDER")
		return "Slider";
	if (type == "PROGRESSBAR")
		return "ProgressBar";
	if (type == "ENTRYFIELD")
		return "TextEntry";
	if (type == "STATICTEXT")
		return "StaticText";
	if (type == "TABCONTROL")
		return "TabControl";
	return "Window";
}

} // namespace

bool EditorWindow::edit(const QString &description, const std::function<void(WndModel::Layout &)> &change)
{
	const bool done = m_doc.edit(toLatin1(description), change);
	if (done)
		changed();
	return done;
}

void EditorWindow::undo()
{
	if (m_doc.undo())
		changed();
}

void EditorWindow::redo()
{
	if (m_doc.redo())
		changed();
}

// ---------------------------------------------------------------------------------------------------------
// Selection

bool EditorWindow::selectWindow(const QString &fullName)
{
	if (!m_items.contains(fullName))
		return false;
	setSelection({fullName});
	return true;
}

void EditorWindow::setSelection(const QStringList &fullNames)
{
	QStringList names;
	for (const QString &n : fullNames)
		if (m_items.contains(n) && !names.contains(n))
			names << n;
	if (names == m_selection)
		return;
	m_selection = names;
	if (m_tree && !m_treeFilter.isEmpty())
		for (const QString &n : names)
			if (QTreeWidgetItem *item = m_items.value(n); item && item->isHidden())
			{
				m_treeFilter.clear();
				break;
			}
	fillTree();
	if (!names.isEmpty())
		if (QTreeWidgetItem *item = m_items.value(names.back()))
			m_tree->scrollToItem(item);
	showSelection();
}

QString EditorWindow::selectedWindow() const
{
	return m_selection.isEmpty() ? QString() : m_selection.back();
}

QString EditorWindow::selectAt(int x, int y)
{
	QString name = GuiEngine::instance().windowAt(x, y);
	if (m_locked.contains(name))
		name.clear();
	setSelection(name.isEmpty() ? QStringList() : QStringList{name});
	return name;
}

std::vector<WndModel::WindowPath> EditorWindow::selectedPaths() const
{
	std::vector<WndModel::WindowPath> paths;
	for (const QString &name : m_selection)
		if (std::optional<WndModel::WindowPath> p = WndModel::findWindow(m_doc.layout(), toLatin1(name)))
			paths.push_back(*p);
	std::sort(paths.begin(), paths.end());
	// A window inside a selected one goes with it.
	std::vector<WndModel::WindowPath> outer;
	for (const WndModel::WindowPath &p : paths)
		if (outer.empty() || !WndModel::contains(outer.back(), p))
			outer.push_back(p);
	return outer;
}

void EditorWindow::updateOverlay()
{
	if (!m_engine)
		return;
	GuiEngine &engine = GuiEngine::instance();
	if (m_testMode)
	{
		engine.highlight({}, 0);
		engine.overlay({});
		return;
	}
	const Tokens t = tokens(m_theme, m_game);
	engine.highlight(m_selection, argbOf(t.accFg));
	std::vector<GuiEngine::OverlayItem> items;
	if (m_gridShown && m_gridStep > 0)
	{
		const Resolution r = resolution();
		const double stepX = m_gridStep / layoutScaleX();
		const double stepY = m_gridStep / layoutScaleY();
		const std::uint32_t line = 0x30FFFFFFu;
		for (double x = stepX; x < r.width; x += stepX)
			items.push_back({QRect(static_cast<int>(std::lround(x)), 0, 1, r.height), line, true});
		for (double y = stepY; y < r.height; y += stepY)
			items.push_back({QRect(0, static_cast<int>(std::lround(y)), r.width, 1), line, true});
	}
	if (m_outlines)
		WndModel::forEachWindow(m_doc.layout(), [&](const WndModel::Window &w, int) {
			const bool hidden = w.hasStatus("HIDDEN") && !m_showHidden;
			if (!hidden && !w.hasStatus("SEE_THRU"))
				return;
			const QRect r = engine.windowRect(latin1(w.fullName()));
			if (!r.isEmpty())
				items.push_back({r, hidden ? 0xA0808080u : 0xA040C0C0u, false});
		});
	if (m_selection.size() == 1)
	{
		const QRect r = engine.windowRect(m_selection.back());
		if (!r.isEmpty())
			for (int h = 0; h < 8; ++h)
			{
				const QRect handle = handleRect(r, h);
				items.push_back({handle, argbOf(t.accFg), true});
				items.push_back({handle, 0xFF000000u, false});
			}
	}
	if (m_gesture == Gesture::Band && !m_band.isEmpty())
		items.push_back({m_band, argbOf(t.accFg), false});
	if (m_gesture == Gesture::Move)
	{
		const Resolution r = resolution();
		const std::uint32_t guide = 0xFFFF40C0u;
		if (m_guideX)
			items.push_back({QRect(static_cast<int>(std::lround(*m_guideX / layoutScaleX())), 0, 1, r.height), guide, true});
		if (m_guideY)
			items.push_back({QRect(0, static_cast<int>(std::lround(*m_guideY / layoutScaleY())), r.width, 1), guide, true});
	}
	engine.overlay(items);
}

QRect EditorWindow::handleRect(const QRect &w, int handle) const
{
	const int xs[3] = {w.left(), w.center().x(), w.right()};
	const int ys[3] = {w.top(), w.center().y(), w.bottom()};
	// Clockwise from the top left corner.
	static const int at[8][2] = {{0, 0}, {1, 0}, {2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {0, 1}};
	const QPoint c(xs[at[handle][0]], ys[at[handle][1]]);
	return QRect(c.x() - HANDLE / 2, c.y() - HANDLE / 2, HANDLE, HANDLE);
}

int EditorWindow::handleAt(QPoint p) const
{
	if (m_selection.size() != 1)
		return -1;
	const QRect r = GuiEngine::instance().windowRect(m_selection.back());
	if (r.isEmpty())
		return -1;
	for (int h = 0; h < 8; ++h)
		if (handleRect(r, h).adjusted(-2, -2, 2, 2).contains(p))
			return h;
	return -1;
}

double EditorWindow::layoutScaleX() const
{
	int width = 800;
	for (const WndModel::Window *w : m_doc.layout().windows())
		if (const std::optional<WndModel::Rect> r = w->rect())
		{
			width = r->creationWidth;
			break;
		}
	return static_cast<double>(width) / resolution().width;
}

double EditorWindow::layoutScaleY() const
{
	int height = 600;
	for (const WndModel::Window *w : m_doc.layout().windows())
		if (const std::optional<WndModel::Rect> r = w->rect())
		{
			height = r->creationHeight;
			break;
		}
	return static_cast<double>(height) / resolution().height;
}

// ---------------------------------------------------------------------------------------------------------
// Mouse and keys in the drawing

void EditorWindow::mouse(int event, int x, int y, Qt::KeyboardModifiers modifiers)
{
	if (!m_engine || m_testMode)
		return;
	const QPoint p(x, y);
	const auto kind = static_cast<LayoutView::MouseEvent>(event);
	if (kind == LayoutView::MouseEvent::Press)
	{
		m_pressAt = p;
		m_handle = handleAt(p);
		if (m_handle >= 0)
		{
			m_gesture = Gesture::Resize;
			m_doc.begin();
			return;
		}
		QString name = GuiEngine::instance().windowAt(x, y);
		if (m_locked.contains(name))
			name.clear();
		if (name.isEmpty())
		{
			if (!(modifiers & Qt::ControlModifier))
				setSelection({});
			m_gesture = Gesture::Band;
			m_band = QRect(p, p);
			return;
		}
		if (modifiers & Qt::ControlModifier)
		{
			QStringList names = m_selection;
			if (names.contains(name))
				names.removeAll(name);
			else
				names << name;
			setSelection(names);
			m_gesture = Gesture::None;
			return;
		}
		if (!m_selection.contains(name))
			setSelection({name});
		else if (m_selection.back() != name)
		{
			QStringList names = m_selection;
			names.removeAll(name);
			names << name;
			setSelection(names);
		}
		m_gesture = Gesture::Pending;
		return;
	}

	if (kind == LayoutView::MouseEvent::Move)
	{
		if (m_gesture == Gesture::Pending && (p - m_pressAt).manhattanLength() > DRAG_START)
		{
			m_gesture = Gesture::Move;
			m_doc.begin();
		}
		const int dx = static_cast<int>(std::lround((p.x() - m_pressAt.x()) * layoutScaleX()));
		const int dy = static_cast<int>(std::lround((p.y() - m_pressAt.y()) * layoutScaleY()));
		if (m_gesture == Gesture::Move)
		{
			const std::vector<WndModel::WindowPath> paths = selectedPaths();
			const std::string primary = toLatin1(m_selection.back());
			m_guideX.reset();
			m_guideY.reset();
			// Guides: edges and centres of the windows that do not move (Alt: none).
			std::vector<WndModel::Rect> others;
			if (!m_snap && !(modifiers & Qt::AltModifier))
				WndModel::forEachWindow(m_doc.layout(), [&](const WndModel::Window &w, int) {
					const std::optional<WndModel::WindowPath> at = WndModel::findWindow(m_doc.layout(), w.fullName());
					if (!at || w.hasStatus("HIDDEN") || m_editorHidden.contains(latin1(w.fullName())))
						return;
					if (std::any_of(paths.begin(), paths.end(), [&at](const WndModel::WindowPath &s) { return WndModel::contains(s, *at); }))
						return;
					if (const std::optional<WndModel::Rect> r = w.rect())
						others.push_back(*r);
				});
			m_doc.live([&](WndModel::Layout &l) {
				// With the grid on, the window the mouse holds lands on it; the others keep their distance.
				int sx = dx;
				int sy = dy;
				if (const std::optional<WndModel::WindowPath> held = WndModel::findWindow(l, primary))
					if (const std::optional<WndModel::Rect> r = WndModel::windowAt(l, *held)->rect())
					{
						if (m_snap)
						{
							sx = snapped(r->left + dx) - r->left;
							sy = snapped(r->top + dy) - r->top;
						}
						else if (!others.empty())
						{
							WndModel::Rect moved = *r;
							moved.left += dx;
							moved.right += dx;
							moved.top += dy;
							moved.bottom += dy;
							const WndModel::GuideSnap g = WndModel::snapToGuides(moved, others, GUIDE_SNAP);
							sx += g.dx;
							sy += g.dy;
							m_guideX = g.lineX;
							m_guideY = g.lineY;
						}
					}
				for (const WndModel::WindowPath &path : paths)
					if (WndModel::Window *w = WndModel::windowAt(l, path))
						WndModel::offsetWindow(*w, sx, sy);
			});
		}
		else if (m_gesture == Gesture::Resize)
		{
			const std::optional<WndModel::WindowPath> path = WndModel::findWindow(m_doc.layout(), toLatin1(m_selection.back()));
			const int handle = m_handle;
			m_doc.live([&](WndModel::Layout &l) {
				WndModel::Window *w = path ? WndModel::windowAt(l, *path) : nullptr;
				std::optional<WndModel::Rect> r = w ? w->rect() : std::nullopt;
				if (!r)
					return;
				if (handle == 0 || handle == 6 || handle == 7)
					r->left = std::min(snapped(r->left + dx), r->right - MIN_SIZE);
				if (handle == 2 || handle == 3 || handle == 4)
					r->right = std::max(snapped(r->right + dx), r->left + MIN_SIZE);
				if (handle == 0 || handle == 1 || handle == 2)
					r->top = std::min(snapped(r->top + dy), r->bottom - MIN_SIZE);
				if (handle == 4 || handle == 5 || handle == 6)
					r->bottom = std::max(snapped(r->bottom + dy), r->top + MIN_SIZE);
				w->setRect(*r);
			});
		}
		else if (m_gesture == Gesture::Band)
		{
			m_band = QRect(m_pressAt, p).normalized();
			updateOverlay();
			return;
		}
		else
			return;
		// While dragging the engine draws the moved windows, at most every few frames.
		if (!m_lastLive.isValid() || m_lastLive.elapsed() >= LIVE_INTERVAL)
		{
			reloadEngine();
			updateOverlay();
			m_lastLive.restart();
		}
		statusBar()->showMessage(m_gesture == Gesture::Move ? T("Move by %1, %2").arg(dx).arg(dy) : T("Resize by %1, %2").arg(dx).arg(dy));
		return;
	}

	// Release.
	const Gesture gesture = m_gesture;
	m_gesture = Gesture::None;
	if (gesture == Gesture::Move || gesture == Gesture::Resize)
	{
		m_doc.finish(toLatin1(gesture == Gesture::Move ? T("Move") : T("Resize")));
		m_guideX.reset();
		m_guideY.reset();
		statusBar()->clearMessage();
		changed();
	}
	else if (gesture == Gesture::Band)
	{
		QStringList names = (modifiers & Qt::ControlModifier) ? m_selection : QStringList();
		if (m_band.width() > DRAG_START || m_band.height() > DRAG_START)
			for (const std::string &n : WndModel::fullNames(m_doc.layout()))
			{
				const QString name = latin1(n);
				const QRect r = GuiEngine::instance().windowRect(name);
				if (!r.isEmpty() && m_band.contains(r) && !names.contains(name) && !m_locked.contains(name))
					names << name;
			}
		m_band = QRect();
		setSelection(names);
		updateOverlay();
	}
}

bool EditorWindow::key(int k, Qt::KeyboardModifiers modifiers)
{
	// With the grid on, one grid step (Ctrl: one pixel); without, one pixel (Shift: ten).
	int step = (modifiers & Qt::ShiftModifier) ? 10 : 1;
	if (m_snap && !(modifiers & Qt::ControlModifier))
		step = m_gridStep;
	switch (k)
	{
	case Qt::Key_Left:
		nudgeSelection(-step, 0);
		return true;
	case Qt::Key_Right:
		nudgeSelection(step, 0);
		return true;
	case Qt::Key_Up:
		nudgeSelection(0, -step);
		return true;
	case Qt::Key_Down:
		nudgeSelection(0, step);
		return true;
	case Qt::Key_Escape:
		if (m_gesture == Gesture::Move || m_gesture == Gesture::Resize)
		{
			m_doc.cancel();
			m_gesture = Gesture::None;
			changed();
		}
		else
			setSelection({});
		return true;
	default:
		return false;
	}
}

void EditorWindow::drag(QPoint from, QPoint to, Qt::KeyboardModifiers modifiers)
{
	const QPoint mid((from.x() + to.x()) / 2, (from.y() + to.y()) / 2);
	mouse(static_cast<int>(LayoutView::MouseEvent::Press), from.x(), from.y(), modifiers);
	mouse(static_cast<int>(LayoutView::MouseEvent::Move), mid.x(), mid.y(), modifiers);
	mouse(static_cast<int>(LayoutView::MouseEvent::Move), to.x(), to.y(), modifiers);
	mouse(static_cast<int>(LayoutView::MouseEvent::Release), to.x(), to.y(), modifiers);
}

int EditorWindow::snapped(int value) const
{
	if (!m_snap || m_gridStep <= 1)
		return value;
	return static_cast<int>(std::lround(static_cast<double>(value) / m_gridStep)) * m_gridStep;
}

void EditorWindow::setGrid(bool shown, bool snap, int step)
{
	m_gridShown = shown;
	m_snap = snap;
	m_gridStep = std::clamp(step, 2, 100);
	updateOverlay();
	saveSettings();
}

void EditorWindow::setBackground(const QColor &color)
{
	m_background = color;
	m_view->setBackground(argbOf(color) & 0xFFFFFFu);
	m_view->renderFrame();
	saveSettings();
}

bool EditorWindow::setBackgroundImage(const QString &path)
{
	QImage image;
	if (!path.isEmpty() && !image.load(path))
		return false;
	m_backgroundImage = path;
	const bool ok = !m_engine || GuiEngine::instance().setBackgroundImage(image);
	m_view->renderFrame();
	saveSettings();
	return ok;
}

void EditorWindow::setOutlines(bool shown)
{
	m_outlines = shown;
	updateOverlay();
	saveSettings();
}

QMenu *EditorWindow::contextMenu()
{
	auto *menu = new QMenu(this);
	menu->setObjectName(QStringLiteral("ContextMenu"));
	menu->setAttribute(Qt::WA_DeleteOnClose);
	const auto add = [&](const char *name) {
		if (QAction *a = m_actions.value(QString::fromLatin1(name)))
			menu->addAction(a);
	};
	for (const char *name : {"Cut", "Copy", "Paste", "Duplicate", "Delete"})
		add(name);
	menu->addSeparator();
	for (const char *name : {"Forward", "Backward", "Front", "Back"})
		add(name);
	menu->addSeparator();
	QMenu *insert = menu->addMenu(T("&Insert"));
	for (auto it = m_actions.cbegin(); it != m_actions.cend(); ++it)
		if (it.key().startsWith(QLatin1String("Insert")))
			insert->addAction(it.value());
	if (auto *arrange = menuBar()->findChild<QMenu *>(QStringLiteral("ArrangeMenu")); arrange && arrangePaths().size() >= 2)
		menu->addMenu(arrange);
	menu->addSeparator();
	if (!m_selection.isEmpty())
	{
		const QString primary = m_selection.back();
		QAction *hide = menu->addAction(T("&Hide in the editor"));
		hide->setCheckable(true);
		hide->setChecked(m_editorHidden.contains(primary));
		hide->setToolTip(T("The editor does not draw it; the file and the game keep it."));
		connect(hide, &QAction::toggled, this, [this, primary](bool on) { setHiddenInEditor(primary, on); });
		QAction *lock = menu->addAction(T("&Lock"));
		lock->setCheckable(true);
		lock->setChecked(m_locked.contains(primary));
		lock->setToolTip(T("A click in the drawing does not select it; the tree still does."));
		connect(lock, &QAction::toggled, this, [this, primary](bool on) { setLocked(primary, on); });
		if (!m_editorHidden.isEmpty())
		{
			QAction *showAll = menu->addAction(T("Show all hidden in the editor"));
			connect(showAll, &QAction::triggered, this, [this] {
				m_editorHidden.clear();
				changed();
			});
		}
	}
	menu->addSeparator();
	QAction *properties = menu->addAction(T("&Properties"));
	connect(properties, &QAction::triggered, this, [this] {
		m_propertiesDock->show();
		m_propertiesDock->raise();
	});
	return menu;
}

// ---------------------------------------------------------------------------------------------------------
// Changes to the windows

WndModel::WindowPath EditorWindow::insertParent(std::size_t *index) const
{
	*index = static_cast<std::size_t>(-1);
	const QString primary = selectedWindow();
	const std::optional<WndModel::WindowPath> path = primary.isEmpty() ? std::nullopt : WndModel::findWindow(m_doc.layout(), toLatin1(primary));
	if (!path)
		return {};
	const WndModel::Window *w = WndModel::windowAt(m_doc.layout(), *path);
	if (w && WndModel::canHaveChildren(*w))
		return *path;
	// Next to it, above it.
	*index = path->back() + 1;
	return WndModel::WindowPath(path->begin(), path->end() - 1);
}

QString EditorWindow::insertWindow(const QString &type)
{
	std::size_t index = 0;
	const WndModel::WindowPath parent = insertParent(&index);
	const std::string t = toLatin1(type);
	const std::string file = toLatin1(m_fileName);
	const std::string name = WndModel::uniqueName(m_doc.layout(), file, baseName(t) + "1");
	WndModel::Rect rect;
	if (const WndModel::Window *p = parent.empty() ? nullptr : WndModel::windowAt(m_doc.layout(), parent); p && p->rect())
		rect = WndModel::defaultRect(t, p->rect()->left + 20, p->rect()->top + 20);
	else if (t == "USER" && m_doc.layout().windows().empty())
	{
		// The first window of a new layout is the screen.
		rect = WndModel::defaultRect(t, 0, 0);
		rect.right = 800;
		rect.bottom = 600;
	}
	else
		rect = WndModel::defaultRect(t, 20, 20);
	const QString full = latin1(file + ":" + name);
	WndModel::Window made = WndModel::makeWindow(t, file, name, rect);
	if (m_scheme)
	{
		WndModel::applyScheme(*m_scheme, made);
		WndModel::applySchemeText(*m_scheme, made);
	}
	if (!edit(T("Insert %1").arg(type), [&](WndModel::Layout &l) { WndModel::insertWindow(l, parent, index, made); }))
		return {};
	setSelection({full});
	return full;
}

void EditorWindow::deleteSelection()
{
	std::vector<WndModel::WindowPath> paths = selectedPaths();
	if (paths.empty())
		return;
	// From the back, so the paths in front stay valid.
	std::reverse(paths.begin(), paths.end());
	edit(T("Delete"), [&](WndModel::Layout &l) {
		for (const WndModel::WindowPath &p : paths)
			WndModel::removeWindow(l, p);
	});
}

void EditorWindow::copySelection()
{
	std::vector<const WndModel::Window *> windows;
	for (const WndModel::WindowPath &p : selectedPaths())
		if (const WndModel::Window *w = WndModel::windowAt(m_doc.layout(), p))
			windows.push_back(w);
	if (windows.empty())
		return;
	QApplication::clipboard()->setText(latin1(WndModel::windowsText(windows)));
	statusBar()->showMessage(T("%1 window(s) copied").arg(windows.size()), 3000);
}

void EditorWindow::cutSelection()
{
	copySelection();
	deleteSelection();
}

QStringList EditorWindow::paste()
{
	std::vector<WndModel::Window> windows = WndModel::readWindows(toLatin1(QApplication::clipboard()->text()));
	if (windows.empty())
	{
		statusBar()->showMessage(T("The clipboard holds no windows."), 3000);
		return {};
	}
	return pasteWindows(std::move(windows), true);
}

QStringList EditorWindow::duplicateSelection()
{
	std::vector<WndModel::Window> windows;
	for (const WndModel::WindowPath &p : selectedPaths())
		if (const WndModel::Window *w = WndModel::windowAt(m_doc.layout(), p))
			windows.push_back(*w);
	return windows.empty() ? QStringList() : pasteWindows(std::move(windows), true);
}

QStringList EditorWindow::pasteWindows(std::vector<WndModel::Window> windows, bool offset)
{
	std::size_t index = 0;
	const WndModel::WindowPath parent = insertParent(&index);
	const std::string file = toLatin1(m_fileName);
	QStringList names;
	edit(T("Paste"), [&](WndModel::Layout &l) {
		for (WndModel::Window &w : windows)
		{
			// A copy of a window that is still there moves a little, so both can be seen.
			if (offset && WndModel::findWindow(l, file + ":" + w.name()))
				WndModel::offsetWindow(w, PASTE_OFFSET, PASTE_OFFSET);
			WndModel::makeNamesUnique(l, file, w);
			names << latin1(w.fullName());
			const WndModel::WindowPath at = WndModel::insertWindow(l, parent, index, std::move(w));
			if (!at.empty())
				index = at.back() + 1;
		}
	});
	setSelection(names);
	return names;
}

void EditorWindow::nudgeSelection(int dx, int dy)
{
	const std::vector<WndModel::WindowPath> paths = selectedPaths();
	if (paths.empty())
		return;
	edit(T("Move"), [&](WndModel::Layout &l) {
		for (const WndModel::WindowPath &p : paths)
			if (WndModel::Window *w = WndModel::windowAt(l, p))
				WndModel::offsetWindow(*w, dx, dy);
	});
}

void EditorWindow::reorderSelection(int steps)
{
	const QStringList names = m_selection;
	if (names.isEmpty() || steps == 0)
		return;
	edit(steps > 0 ? T("Bring forward") : T("Send backward"), [&](WndModel::Layout &l) {
		for (const QString &n : names)
		{
			const std::optional<WndModel::WindowPath> path = WndModel::findWindow(l, toLatin1(n));
			if (!path)
				continue;
			const WndModel::WindowPath parent(path->begin(), path->end() - 1);
			const WndModel::Window *owner = parent.empty() ? nullptr : WndModel::windowAt(l, parent);
			const std::size_t count = owner ? owner->children().size() : l.windows().size();
			const long target = std::clamp(static_cast<long>(path->back()) + steps, 0L, static_cast<long>(count) - 1);
			WndModel::moveWindow(l, *path, parent, static_cast<std::size_t>(target));
		}
	});
}

void EditorWindow::reorderSelectionToEnd(bool front)
{
	reorderSelection(front ? 1 << 20 : -(1 << 20));
}

bool EditorWindow::moveInto(const QStringList &fullNames, const QString &parentName, int index)
{
	const QStringList selection = m_selection;
	const bool done = edit(T("Move in the tree"), [&](WndModel::Layout &l) {
		std::size_t at = static_cast<std::size_t>(std::max(0, index));
		for (const QString &n : fullNames)
		{
			const std::optional<WndModel::WindowPath> from = WndModel::findWindow(l, toLatin1(n));
			WndModel::WindowPath parent;
			if (!parentName.isEmpty())
			{
				const std::optional<WndModel::WindowPath> p = WndModel::findWindow(l, toLatin1(parentName));
				if (!p)
					return;
				parent = *p;
			}
			if (!from || WndModel::contains(*from, parent))
				continue;
			// The place counts with the window still there; behind it, one place less once it is gone.
			std::size_t place = at;
			const WndModel::WindowPath fromParent(from->begin(), from->end() - 1);
			if (fromParent == parent && from->back() < place)
				--place;
			if (const std::optional<WndModel::WindowPath> moved = WndModel::moveWindow(l, *from, parent, place))
				at = moved->back() + 1;
		}
	});
	setSelection(selection);
	return done;
}

} // namespace GuiEditorApp
