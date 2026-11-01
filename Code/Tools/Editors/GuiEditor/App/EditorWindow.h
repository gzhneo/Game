/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EditorWindow.h -- the main window of the GUI editor: the layout as the game draws it, the window
// tree, the properties of the selected window, the layouts of the game data, problems and the file text.
//
// The document (Core/Document) holds the layout; after every change the engine reads the same text with
// the game's reader and draws it. Windows are matched between both by NAME.
//
// EditorWindow.cpp: files, menus, panels; EditorEditing.cpp: selection, mouse, keys, changes to the
// windows; EditorProperties.cpp: the properties panel; EditorCatalog.cpp: what the game data offer;
// EditorSchemes.cpp: layout schemes and comparing two layouts; EditorTest.cpp: the test mode; EditorArrange.cpp:
// arranging, the undo history, autosave, windows hidden or locked in the editor.

#pragma once

#include "Core/Document.h"
#include "Core/WndArrange.h"
#include "Core/WndDiff.h"
#include "Core/WndEdit.h"
#include "Core/WndScheme.h"
#include "EditorCommon/Language.h"
#include "EditorCommon/Theme.h"
#include "Model/WndCheck.h"
#include "Model/WndLayout.h"

#include <QElapsedTimer>
#include <QColor>
#include <QHash>
#include <QIcon>
#include <QMainWindow>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <functional>
#include <optional>
#include <vector>

class QAction;
class QMenu;
class QApplication;
class QDockWidget;
class QLabel;
class QScrollArea;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace GuiEditorApp
{

class LayoutView;
class WindowTree;

using EditorCommon::Game;
using EditorCommon::Language;
using EditorCommon::ThemeKind;

struct Resolution
{
	int width = 800;
	int height = 600;
};

// The resolutions the view offers; the first is the one the layouts are made for.
const std::vector<Resolution> &resolutions();

// Where a layout comes from: a file anywhere, or the game data (loose or in an archive).
struct LayoutSource
{
	enum class Kind
	{
		None,
		File,
		Game,
	};
	Kind kind = Kind::None;
	QString path;		// File: absolute path; Game: below the folder Window, with '/'

	[[nodiscard]] QString key() const;	// "file:<path>" or "game:<path>", for the recent list
	static LayoutSource fromKey(const QString &key);
	bool operator==(const LayoutSource &) const = default;
};

class EditorWindow : public QMainWindow
{
public:
	EditorWindow(QApplication &app, Game game, bool interactive);

	[[nodiscard]] LayoutView *view() const { return m_view; }
	bool startEngine(const QString &dataFolder, const QString &logFile, QString *error);

	// Files. Opening asks what to do with unsaved changes (interactive editor only).
	// Opens a layout: an existing file, otherwise a path below the folder Window of the game data.
	// Returns the number of windows the game's reader made, -1 if it cannot be read.
	int openLayout(const QString &path);
	int open(const LayoutSource &source);
	int reload();
	void newLayout();
	bool save();
	bool saveAs(const QString &path);

	[[nodiscard]] const WndModel::Layout &layout() const { return m_doc.layout(); }
	[[nodiscard]] const WndModel::Document &document() const { return m_doc; }
	[[nodiscard]] const std::vector<WndModel::Problem> &problems() const { return m_problems; }
	[[nodiscard]] const LayoutSource &source() const { return m_source; }
	[[nodiscard]] QString fileName() const { return m_fileName; }
	[[nodiscard]] QStringList gameLayouts() const { return m_gameLayouts; }
	// Windows the game's reader made of the current text, -1 if it could not read it.
	[[nodiscard]] int engineCount() const { return m_engineCount; }

	// Grid in layout pixels: drawn over the layout, windows snap to it when dragged; arrow keys then move by
	// one step (Ctrl: by one pixel).
	void setGrid(bool shown, bool snap, int step);
	void setBackground(const QColor &color);
	// A picture under the layout (a scene of the game), stretched to the view; an empty path removes it.
	bool setBackgroundImage(const QString &path);
	[[nodiscard]] QString backgroundImage() const { return m_backgroundImage; }
	// Frames around the windows the game hides or draws see-through, so they can be found.
	void setOutlines(bool shown);
	// The menu of the right mouse button in the drawing and the tree.
	[[nodiscard]] QMenu *contextMenu();

	void setResolution(int index);
	[[nodiscard]] Resolution resolution() const;
	void setShowHidden(bool show);
	void setLanguage(Language language);
	void setTheme(ThemeKind theme);

	// Selection by NAME as written (with the file in front); the last one is the one the properties show.
	bool selectWindow(const QString &fullName);
	void setSelection(const QStringList &fullNames);
	[[nodiscard]] QString selectedWindow() const;
	[[nodiscard]] QStringList selection() const { return m_selection; }
	// The window under a point of the drawing, selected as a click would.
	QString selectAt(int x, int y);
	[[nodiscard]] int treeCount() const;
	[[nodiscard]] int propertyCount() const;

	// Changes (each one step for undo).
	void undo();
	void redo();
	// Inserts a window of the type (WINDOWTYPE) into the selected window if it holds children, otherwise
	// next to it; returns its NAME.
	QString insertWindow(const QString &type);
	void deleteSelection();
	void copySelection();
	void cutSelection();
	// Pastes windows from the clipboard; returns their names.
	QStringList paste();
	QStringList duplicateSelection();
	// Moves the selected windows on the screen, in layout pixels.
	void nudgeSelection(int dx, int dy);
	// Earlier in the file is drawn below: by one place, or to the end (front) or the start (back).
	void reorderSelection(int steps);
	void reorderSelectionToEnd(bool front);
	// Puts the windows into another parent (empty: top level) at the place index.
	bool moveInto(const QStringList &fullNames, const QString &parent, int index);
	// Sets one value of the selected window through the properties panel, as the user would; true if the
	// panel had that property.
	bool editProperty(const QString &key, const QString &value);
	// The same as dragging with the mouse in the view, from one point to another (pixels of the drawing).
	void drag(QPoint from, QPoint to, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

	// Layout schemes (.ls of the old GUIEdit): images and colours per window type and state. A loaded scheme
	// also gives new windows its text colours and font.
	bool loadScheme(const QString &path);
	void clearScheme();
	[[nodiscard]] const std::optional<WndModel::Scheme> &scheme() const { return m_scheme; }
	// Writes the scheme the layout uses (states the layout has no window for from the loaded scheme).
	bool saveLayoutScheme(const QString &path);
	// Applies the loaded scheme to the whole layout or the selected windows (one step); returns the number of
	// windows that changed.
	int applyScheme(bool selectionOnly);

	// Compares the layout with another (a file, or a path below Window in the game data; empty: the saved
	// state of the open layout) and shows the differences; they follow every change. Returns their number,
	// -1 if the other layout cannot be read.
	int compareWith(const QString &path);
	void stopCompare();
	[[nodiscard]] const std::vector<WndModel::WindowDiff> &differences() const { return m_differences; }

	// Test mode: mouse and keys work the windows as in the game (the layout stays as it is); leaving it reads
	// the layout again.
	void setTestMode(bool on);
	[[nodiscard]] bool testMode() const { return m_testMode; }
	// Input as the view passes it in the test mode (kind and value as meWindowsInput); true if a window used it.
	bool testInput(int kind, int x, int y, int value);
	// What the gadgets reported, one line each, as the test panel shows it.
	[[nodiscard]] const QStringList &testLog() const { return m_testLog; }

	// Several selected windows: aligned to, or given the size of, the last selected one; spread evenly
	// (three or more). Each is one step; false if nothing changed.
	bool alignSelection(WndModel::Align align);
	bool distributeSelection(bool horizontal);
	bool sameSizeSelection(bool width, bool height);
	// Undo or redo until this many steps are applied (the history panel).
	void goToStep(int applied);
	// Writes the unsaved layout to the autosave folder (also every minute); false if there was nothing to write.
	bool autosaveNow();
	[[nodiscard]] QString autosavePath() const;
	// Windows hidden in the editor only (the file and the game keep them), and windows a click does not select.
	void setHiddenInEditor(const QString &fullName, bool hidden);
	void setLocked(const QString &fullName, bool locked);
	[[nodiscard]] bool isHiddenInEditor(const QString &fullName) const { return m_editorHidden.contains(fullName); }
	[[nodiscard]] bool isLocked(const QString &fullName) const { return m_locked.contains(fullName); }
	// The guide lines a drag snapped to last, in layout pixels (for the self test).
	[[nodiscard]] std::optional<int> guideX() const { return m_guideX; }
	[[nodiscard]] std::optional<int> guideY() const { return m_guideY; }

protected:
	void closeEvent(QCloseEvent *event) override;

private:
	enum class Gesture
	{
		None,
		Pending,	// pressed on a window, not moved far enough yet
		Move,
		Resize,
		Band,
	};

	void rebuild();
	void buildMenus();
	QWidget *buildLayoutsPage();
	QWidget *buildTreePage();
	QWidget *buildPropertiesPage();
	QWidget *buildProblemsPage();
	QWidget *buildTextPage();
	QWidget *buildComparePage();
	QWidget *buildTestPage();
	QWidget *buildHistoryPage();
	void fillHistory();
	void buildToolBar();
	[[nodiscard]] std::vector<WndModel::WindowPath> arrangePaths() const;
	void removeAutosave();
	void offerAutosaves();
	// The text the engine draws: the document without the windows hidden in the editor.
	[[nodiscard]] std::string engineText() const;
	void fillTestLog();
	[[nodiscard]] QString eventText(const QString &event) const;
	void fillCompare();
	// The text of a layout: a file, or a path below Window in the game data; null if it cannot be read.
	[[nodiscard]] QByteArray readLayoutText(const LayoutSource &source) const;
	[[nodiscard]] LayoutSource sourceOf(const QString &path) const;
	QDockWidget *dock(const char *objectName, Qt::DockWidgetArea area);
	void setDockPage(QDockWidget *dock, const QString &title, QWidget *page);

	void startDocument(WndModel::Document doc, const LayoutSource &source, const QString &fileName);
	bool maybeSave();
	// After a change of the document: problems, engine, panels.
	void changed();
	// The engine reads the text of the document again.
	void reloadEngine();
	void fillTree();
	void fillProperties();
	void fillProblems();
	void fillText();
	void showSelection();
	void updateOverlay();
	void updateActions();
	void applyResolution();
	void updateTitle();
	void updateStatus();
	void addRecent(const LayoutSource &source);
	void saveSettings() const;

	// Editing helpers.
	bool edit(const QString &description, const std::function<void(WndModel::Layout &)> &change);
	[[nodiscard]] std::vector<WndModel::WindowPath> selectedPaths() const;	// outermost selected windows, in file order
	[[nodiscard]] WndModel::WindowPath insertParent(std::size_t *index) const;
	QStringList pasteWindows(std::vector<WndModel::Window> windows, bool offset);
	void mouse(int event, int x, int y, Qt::KeyboardModifiers modifiers);
	bool key(int key, Qt::KeyboardModifiers modifiers);
	[[nodiscard]] int handleAt(QPoint p) const;
	[[nodiscard]] QRect handleRect(const QRect &window, int handle) const;
	[[nodiscard]] double layoutScaleX() const;
	[[nodiscard]] double layoutScaleY() const;
	[[nodiscard]] int snapped(int value) const;
	void applyProperty(QTreeWidgetItem *item);
	void pickColor(QTreeWidgetItem *item);
	// Opens the picker that fits the row (image, header template, callback, font, text); false if none does.
	bool pickValue(QTreeWidgetItem *item);
	void loadCatalog();
	// Images, texts, callbacks and header templates the game does not have.
	[[nodiscard]] std::vector<WndModel::Problem> catalogProblems() const;
	[[nodiscard]] QIcon imageIcon(const QString &name, int size);
	[[nodiscard]] QString gameText(const QString &key, bool *exists);

	QApplication &m_app;
	Game m_game;
	ThemeKind m_theme = ThemeKind::Dark;
	bool m_interactive = true;
	bool m_engine = false;

	LayoutView *m_view = nullptr;
	QScrollArea *m_scroll = nullptr;
	QTimer *m_frames = nullptr;
	QDockWidget *m_layoutsDock = nullptr;
	QDockWidget *m_treeDock = nullptr;
	QDockWidget *m_propertiesDock = nullptr;
	QDockWidget *m_problemsDock = nullptr;
	QDockWidget *m_textDock = nullptr;
	QDockWidget *m_compareDock = nullptr;
	QDockWidget *m_testDock = nullptr;
	QDockWidget *m_historyDock = nullptr;
	QTimer *m_autosave = nullptr;
	WindowTree *m_tree = nullptr;
	QTreeWidget *m_properties = nullptr;
	QLabel *m_statusLeft = nullptr;
	QLabel *m_statusRight = nullptr;
	QHash<QString, QAction *> m_actions;	// by object name, for enabling

	// What the game data offer, read once the engine runs.
	struct Catalog
	{
		bool loaded = false;
		QStringList images;
		QHash<QString, QString> imageSizes;
		QStringList headerTemplates;
		QHash<QString, QStringList> callbacks;	// by kind: system, input, tooltip, draw, init, update, shutdown
		QStringList textKeys;
		QSet<QString> textSet;
		QSet<QString> imageSet;
	};
	Catalog m_catalog;
	QHash<QString, QIcon> m_imageIcons;
	QHash<QString, QString> m_texts;	// key -> text of the string table (missing keys hold a null string)

	WndModel::Document m_doc;
	LayoutSource m_source;
	QString m_fileName = QStringLiteral("NewLayout.wnd");	// the file in front of the names
	std::vector<WndModel::Problem> m_problems;
	QTemporaryDir m_temp;
	int m_engineCount = -1;
	QString m_error;
	QHash<QString, QTreeWidgetItem *> m_items;	// NAME as written -> tree item
	QStringList m_selection;
	QStringList m_gameLayouts;
	QStringList m_recent;
	QString m_saveFolder;
	int m_resolution = 0;
	bool m_showHidden = false;
	QString m_treeFilter;
	QString m_layoutFilter;
	bool m_filling = false;		// panels are being filled: their signals are no edits
	int m_gridStep = 10;
	bool m_gridShown = false;
	bool m_snap = false;
	bool m_outlines = true;
	QColor m_background = QColor(0x20, 0x24, 0x28);

	std::optional<WndModel::Scheme> m_scheme;
	QString m_schemePath;
	std::optional<WndModel::Layout> m_compare;		// the other layout, while comparing
	QString m_compareName;
	std::vector<WndModel::WindowDiff> m_differences;
	bool m_testMode = false;
	QSet<QString> m_editorHidden;
	QString m_backgroundImage;
	QSet<QString> m_locked;
	std::optional<int> m_guideX;
	std::optional<int> m_guideY;
	QStringList m_testLog;

	Gesture m_gesture = Gesture::None;
	QPoint m_pressAt;
	int m_handle = -1;
	QRect m_band;
	QElapsedTimer m_lastLive;
};

} // namespace GuiEditorApp
