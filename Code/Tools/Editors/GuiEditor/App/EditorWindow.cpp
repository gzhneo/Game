/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EditorWindow.cpp -- the main window of the GUI editor: files, menus, panels, the engine's drawing.

#include "App/EditorWindow.h"

#include "App/EditorShared.h"
#include "App/GuiEngine.h"
#include "App/LayoutView.h"
#include "App/WindowTree.h"
#include "EditorCommon/Widgets.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <string>

namespace GuiEditorApp
{

using namespace EditorCommon;

namespace
{

constexpr int MAX_RECENT = 10;

// The window types the Insert menu offers, with their names in the interface.
struct TypeName
{
	const char *type;
	QString shown;
};

std::vector<TypeName> insertTypes()
{
	return {{"USER", T("User window")}, {"PUSHBUTTON", T("Push button")}, {"CHECKBOX", T("Check box")},
		{"RADIOBUTTON", T("Radio button")}, {"SCROLLLISTBOX", T("List box")}, {"COMBOBOX", T("Combo box")},
		{"HORZSLIDER", T("Horizontal slider")}, {"VERTSLIDER", T("Vertical slider")}, {"PROGRESSBAR", T("Progress bar")},
		{"ENTRYFIELD", T("Text entry")}, {"STATICTEXT", T("Static text")}, {"TABCONTROL", T("Tab control")}};
}

} // namespace

const std::vector<Resolution> &resolutions()
{
	static const std::vector<Resolution> list = {
		{800, 600}, {1024, 768}, {1280, 720}, {1280, 1024}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1440},
	};
	return list;
}

QString LayoutSource::key() const
{
	switch (kind)
	{
	case Kind::File:
		return QStringLiteral("file:") + path;
	case Kind::Game:
		return QStringLiteral("game:") + path;
	case Kind::None:
		break;
	}
	return {};
}

LayoutSource LayoutSource::fromKey(const QString &key)
{
	LayoutSource source;
	if (key.startsWith(QLatin1String("file:")))
		source = {Kind::File, key.mid(5)};
	else if (key.startsWith(QLatin1String("game:")))
		source = {Kind::Game, key.mid(5)};
	return source;
}

EditorWindow::EditorWindow(QApplication &app, Game game, bool interactive) : m_app(app), m_game(game), m_interactive(interactive)
{
	setObjectName(QStringLiteral("GuiEditorWindow"));
	if (m_interactive)
	{
		const QSettings s;
		m_resolution = std::clamp(s.value(QStringLiteral("resolution"), 0).toInt(), 0, static_cast<int>(resolutions().size()) - 1);
		m_showHidden = s.value(QStringLiteral("showHidden"), false).toBool();
		m_theme = s.value(QStringLiteral("theme"), 0).toInt() == 1 ? ThemeKind::Light : ThemeKind::Dark;
		m_recent = s.value(QStringLiteral("recent")).toStringList();
		m_saveFolder = s.value(QStringLiteral("saveFolder")).toString();
		m_gridStep = std::clamp(s.value(QStringLiteral("gridStep"), 10).toInt(), 2, 100);
		m_gridShown = s.value(QStringLiteral("gridShown"), false).toBool();
		m_snap = s.value(QStringLiteral("snap"), false).toBool();
		m_schemePath = s.value(QStringLiteral("scheme")).toString();
		m_backgroundImage = s.value(QStringLiteral("backgroundImage")).toString();
		m_outlines = s.value(QStringLiteral("outlines"), true).toBool();
		if (const QColor bg(s.value(QStringLiteral("background")).toString()); bg.isValid())
			m_background = bg;
		if (s.contains(QStringLiteral("language")))
			EditorCommon::setLanguage(s.value(QStringLiteral("language")).toInt() == 1 ? Language::German : Language::English);
	}
	applyTheme(m_app, m_theme, m_game);

	// The view is a native window the engine draws into; it lives as long as the editor.
	m_view = new LayoutView;
	m_view->setObjectName(QStringLiteral("LayoutView"));
	m_view->setBackground(argbOf(m_background) & 0xFFFFFFu);
	m_view->onContextMenu = [this](int x, int y, QPoint global) {
		const QString name = GuiEngine::instance().windowAt(x, y);
		if (!name.isEmpty() && !m_selection.contains(name))
			setSelection({name});
		contextMenu()->popup(global);
	};
	m_view->onMouse = [this](LayoutView::MouseEvent e, int x, int y, Qt::KeyboardModifiers modifiers) {
		mouse(static_cast<int>(e), x, y, modifiers);
	};
	m_view->onKey = [this](int k, Qt::KeyboardModifiers modifiers) { return key(k, modifiers); };
	m_scroll = new QScrollArea;
	m_scroll->setObjectName(QStringLiteral("LayoutScroll"));
	m_scroll->setAlignment(Qt::AlignCenter);
	m_scroll->setWidget(m_view);
	setCentralWidget(m_scroll);
	applyResolution();

	m_layoutsDock = dock("LayoutsDock", Qt::LeftDockWidgetArea);
	m_treeDock = dock("TreeDock", Qt::LeftDockWidgetArea);
	m_propertiesDock = dock("PropertiesDock", Qt::RightDockWidgetArea);
	m_problemsDock = dock("ProblemsDock", Qt::BottomDockWidgetArea);
	m_textDock = dock("TextDock", Qt::BottomDockWidgetArea);
	tabifyDockWidget(m_layoutsDock, m_treeDock);
	m_compareDock = dock("CompareDock", Qt::BottomDockWidgetArea);
	tabifyDockWidget(m_problemsDock, m_textDock);
	m_testDock = dock("TestDock", Qt::BottomDockWidgetArea);
	m_historyDock = dock("HistoryDock", Qt::RightDockWidgetArea);
	tabifyDockWidget(m_propertiesDock, m_historyDock);
	m_propertiesDock->raise();
	tabifyDockWidget(m_problemsDock, m_compareDock);
	tabifyDockWidget(m_problemsDock, m_testDock);
	m_compareDock->hide();
	m_testDock->hide();
	m_treeDock->raise();
	m_problemsDock->raise();

	m_frames = new QTimer(this);
	m_frames->setInterval(33);
	connect(m_frames, &QTimer::timeout, m_view, [this] { m_view->renderFrame(); });

	m_statusLeft = label(QString());
	m_statusRight = label(QString());
	statusBar()->setSizeGripEnabled(false);
	statusBar()->addWidget(m_statusLeft, 1);
	statusBar()->addPermanentWidget(m_statusRight);

	resize(1400, 900);
	resizeDocks({m_treeDock, m_propertiesDock}, {340, 380}, Qt::Horizontal);
	resizeDocks({m_problemsDock}, {150}, Qt::Vertical);
	if (m_interactive)
	{
		const QSettings s;
		restoreGeometry(s.value(QStringLiteral("geometry")).toByteArray());
		restoreState(s.value(QStringLiteral("state")).toByteArray());
	}
	rebuild();
	if (!m_schemePath.isEmpty() && !loadScheme(m_schemePath))
		m_schemePath.clear();
	// An unsaved layout goes to the autosave folder every minute; the copy goes when it is saved or closed.
	m_autosave = new QTimer(this);
	m_autosave->setInterval(60 * 1000);
	connect(m_autosave, &QTimer::timeout, this, [this] { autosaveNow(); });
	if (m_interactive)
	{
		m_autosave->start();
		QTimer::singleShot(0, this, [this] { offerAutosaves(); });
	}
}

QDockWidget *EditorWindow::dock(const char *objectName, Qt::DockWidgetArea area)
{
	auto *d = new QDockWidget(this);
	d->setObjectName(QString::fromLatin1(objectName));
	d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
	addDockWidget(area, d);
	return d;
}

void EditorWindow::setDockPage(QDockWidget *d, const QString &title, QWidget *page)
{
	d->setWindowTitle(title);
	QWidget *old = d->widget();
	d->setWidget(page);
	if (old)
		old->deleteLater();
}

bool EditorWindow::startEngine(const QString &dataFolder, const QString &logFile, QString *error)
{
	GuiEngine &engine = GuiEngine::instance();
	if (!engine.start(m_game, dataFolder, logFile, error))
		return false;
	if (!m_view->ensureDevice(error))
		return false;
	if (!engine.startWindows(error))
		return false;
	engine.setShowHidden(m_showHidden);
	m_engine = true;
	if (!m_backgroundImage.isEmpty() && !setBackgroundImage(m_backgroundImage))
		m_backgroundImage.clear();
	m_gameLayouts = engine.layouts();
	loadCatalog();
	m_frames->start();
	rebuild();
	changed();
	return true;
}

// ---------------------------------------------------------------------------------------------------------
// Files

int EditorWindow::openLayout(const QString &path)
{
	return open(sourceOf(path));
}

LayoutSource EditorWindow::sourceOf(const QString &path) const
{
	const QFileInfo file(path);
	if (QDir::isAbsolutePath(path) || file.exists())
		return {LayoutSource::Kind::File, file.absoluteFilePath()};
	QString relative = QDir::fromNativeSeparators(path);
	if (relative.startsWith(QLatin1String("Window/"), Qt::CaseInsensitive))
		relative = relative.mid(7);
	return {LayoutSource::Kind::Game, relative};
}

QByteArray EditorWindow::readLayoutText(const LayoutSource &source) const
{
	if (source.kind == LayoutSource::Kind::File)
	{
		QFile file(source.path);
		if (file.open(QIODevice::ReadOnly))
			return file.readAll();
	}
	else if (source.kind == LayoutSource::Kind::Game)
		return GuiEngine::instance().readFile(QStringLiteral("Window\\") + QDir::toNativeSeparators(source.path));
	return {};
}

int EditorWindow::open(const LayoutSource &source)
{
	if (!maybeSave())
		return -1;
	m_error.clear();
	const QByteArray text = readLayoutText(source);
	if (text.isNull())
	{
		m_error = T("%1 cannot be read.").arg(source.path);
		updateStatus();
		return -1;
	}
	WndModel::Document doc(std::string(text.constData(), static_cast<std::size_t>(text.size())));
	QString fileName = latin1(WndModel::layoutFile(doc.layout()));
	if (fileName.isEmpty())
		fileName = QFileInfo(source.path).fileName();
	startDocument(std::move(doc), source, fileName);
	addRecent(source);
	return m_engineCount;
}

int EditorWindow::reload()
{
	if (m_source.kind == LayoutSource::Kind::None)
		return -1;
	const QStringList selected = m_selection;
	const int count = open(m_source);
	setSelection(selected);
	return count;
}

void EditorWindow::newLayout()
{
	if (!maybeSave())
		return;
	m_error.clear();
	startDocument(WndModel::Document(), LayoutSource{}, QStringLiteral("NewLayout.wnd"));
}

void EditorWindow::startDocument(WndModel::Document doc, const LayoutSource &source, const QString &fileName)
{
	m_doc = std::move(doc);
	m_source = source;
	m_fileName = fileName;
	setTestMode(false);
	if (m_doc.modified())
		removeAutosave();
	m_editorHidden.clear();
	m_locked.clear();
	m_selection.clear();
	m_gesture = Gesture::None;
	m_compare.reset();
	m_differences.clear();
	rebuild();
	changed();
}

bool EditorWindow::maybeSave()
{
	if (!m_interactive || !m_doc.modified())
		return true;
	const QMessageBox::StandardButton answer = QMessageBox::question(this, T("GUI Editor"),
		T("%1 has changes that are not saved. Save them?").arg(m_fileName),
		QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
	if (answer == QMessageBox::Save)
		return save();
	return answer == QMessageBox::Discard;
}

bool EditorWindow::save()
{
	if (m_source.kind == LayoutSource::Kind::File)
		return saveAs(m_source.path);
	if (!m_interactive)
		return false;
	QString folder = m_saveFolder;
	if (folder.isEmpty())
		folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	const QString path = QFileDialog::getSaveFileName(this, T("Save layout"), QDir(folder).filePath(m_fileName),
		T("Window layouts (*.wnd)"));
	return !path.isEmpty() && saveAs(path);
}

bool EditorWindow::saveAs(const QString &path)
{
	int errors = 0;
	for (const WndModel::Problem &p : WndModel::check(m_doc.layout()))
		errors += p.severity == WndModel::Problem::Severity::Error ? 1 : 0;
	if (errors > 0 && m_interactive
		&& QMessageBox::warning(this, T("GUI Editor"),
			   T("The game would read %1 window(s) differently from what the file says (see Problems). Save anyway?").arg(errors),
			   QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Save)
		return false;
	// The names carry the file they are in, as GUIEdit writes them.
	const QString base = QFileInfo(path).fileName();
	if (!m_doc.layout().windows().empty() && latin1(WndModel::layoutFile(m_doc.layout())) != base)
	{
		const QString old = m_fileName;
		edit(T("Names for %1").arg(base), [&](WndModel::Layout &l) { WndModel::setLayoutFile(l, toLatin1(base)); });
		for (QString &name : m_selection)
			if (name.startsWith(old + QLatin1Char(':')))
				name = base + name.mid(old.size());
	}
	QSaveFile file(path);
	const std::string text = m_doc.text();
	if (!file.open(QIODevice::WriteOnly) || file.write(text.data(), static_cast<qint64>(text.size())) != static_cast<qint64>(text.size())
		|| !file.commit())
	{
		m_error = T("%1 cannot be written.").arg(QDir::toNativeSeparators(path));
		if (m_interactive)
			QMessageBox::warning(this, T("GUI Editor"), m_error);
		updateStatus();
		return false;
	}
	m_doc.markSaved();
	m_source = {LayoutSource::Kind::File, QFileInfo(path).absoluteFilePath()};
	m_fileName = base;
	m_saveFolder = QFileInfo(path).absolutePath();
	m_error.clear();
	addRecent(m_source);
	statusBar()->showMessage(T("Saved: %1").arg(QDir::toNativeSeparators(path)), 4000);
	changed();
	return true;
}

// ---------------------------------------------------------------------------------------------------------
// The engine's drawing and the panels

void EditorWindow::changed()
{
	m_problems = WndModel::check(m_doc.layout());
	for (WndModel::Problem &p : catalogProblems())
		m_problems.push_back(std::move(p));
	reloadEngine();
	fillTree();
	// Names that are gone (deleted, renamed elsewhere) leave the selection.
	m_selection.erase(std::remove_if(m_selection.begin(), m_selection.end(), [this](const QString &n) { return !m_items.contains(n); }),
		m_selection.end());
	showSelection();
	fillProblems();
	fillText();
	fillCompare();
	fillHistory();
	updateActions();
	updateTitle();
	updateStatus();
}

void EditorWindow::reloadEngine()
{
	if (!m_engine)
		return;
	GuiEngine &engine = GuiEngine::instance();
	if (m_doc.layout().windows().empty())
	{
		engine.clear();
		m_engineCount = 0;
		return;
	}
	// The engine reads files: the text of the document under the layout's own file name.
	const QString path = m_temp.filePath(m_fileName.isEmpty() ? QStringLiteral("Layout.wnd") : m_fileName);
	QFile file(path);
	const std::string text = engineText();
	if (!file.open(QIODevice::WriteOnly) || file.write(text.data(), static_cast<qint64>(text.size())) != static_cast<qint64>(text.size()))
	{
		m_error = T("%1 cannot be written.").arg(QDir::toNativeSeparators(path));
		m_engineCount = -1;
		return;
	}
	file.close();
	QString error;
	m_engineCount = engine.loadLayout(path, &error);
	if (m_engineCount < 0)
		m_error = error;
}

void EditorWindow::setResolution(int index)
{
	m_resolution = std::clamp(index, 0, static_cast<int>(resolutions().size()) - 1);
	applyResolution();
	reloadEngine();
	showSelection();
	updateStatus();
	saveSettings();
	buildMenus();
}

Resolution EditorWindow::resolution() const
{
	return resolutions()[static_cast<std::size_t>(m_resolution)];
}

void EditorWindow::applyResolution()
{
	// The game places the windows for the size of the display: the view is exactly as many pixels.
	const Resolution r = resolution();
	const qreal ratio = m_view->devicePixelRatioF();
	m_view->setFixedSize(std::max(1, static_cast<int>(r.width / ratio)), std::max(1, static_cast<int>(r.height / ratio)));
}

void EditorWindow::setShowHidden(bool show)
{
	m_showHidden = show;
	GuiEngine::instance().setShowHidden(show);
	reloadEngine();
	showSelection();
	updateStatus();
	saveSettings();
}

void EditorWindow::setLanguage(Language language)
{
	EditorCommon::setLanguage(language);
	rebuild();
	saveSettings();
}

void EditorWindow::setTheme(ThemeKind theme)
{
	m_theme = theme;
	applyTheme(m_app, m_theme, m_game);
	rebuild();
	saveSettings();
}

void EditorWindow::rebuild()
{
	buildMenus();
	setDockPage(m_layoutsDock, T("Game layouts"), buildLayoutsPage());
	setDockPage(m_treeDock, T("Windows"), buildTreePage());
	setDockPage(m_propertiesDock, T("Properties"), buildPropertiesPage());
	setDockPage(m_problemsDock, T("Problems (%1)").arg(m_problems.size()), buildProblemsPage());
	setDockPage(m_textDock, T("File text"), buildTextPage());
	fillCompare();
	setDockPage(m_testDock, T("Test"), buildTestPage());
	fillTestLog();
	setDockPage(m_historyDock, T("History"), buildHistoryPage());
	fillHistory();
	buildToolBar();
	fillTree();
	showSelection();
	fillProblems();
	fillText();
	updateActions();
	updateTitle();
	updateStatus();
}

void EditorWindow::buildMenus()
{
	QMenuBar *bar = menuBar();
	bar->clear();
	m_actions.clear();
	const auto action = [this](QMenu *menu, const char *name, const QString &text, const QKeySequence &keys, auto &&slot) {
		QAction *a = menu->addAction(text);
		a->setObjectName(QString::fromLatin1(name));
		if (!keys.isEmpty())
			a->setShortcut(keys);
		connect(a, &QAction::triggered, this, slot);
		m_actions.insert(a->objectName(), a);
		return a;
	};

	QMenu *file = bar->addMenu(T("&File"));
	action(file, "New", T("&New layout"), QKeySequence::New, [this] { newLayout(); });
	action(file, "Open", T("&Open layout..."), QKeySequence::Open, [this] {
		const QString start = m_source.kind == LayoutSource::Kind::File ? QFileInfo(m_source.path).absolutePath() : m_saveFolder;
		const QString path = QFileDialog::getOpenFileName(this, T("Open layout"), start, T("Window layouts (*.wnd)"));
		if (!path.isEmpty())
			openLayout(path);
	});
	action(file, "OpenGame", T("Open from &game data"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), [this] {
		m_layoutsDock->show();
		m_layoutsDock->raise();
		if (auto *filter = child<QLineEdit>(this, "LayoutFilter"))
			filter->setFocus();
	});
	QMenu *recent = file->addMenu(T("&Recent"));
	recent->setEnabled(!m_recent.isEmpty());
	for (const QString &k : m_recent)
	{
		const LayoutSource source = LayoutSource::fromKey(k);
		const QString shown = source.kind == LayoutSource::Kind::Game ? T("%1 (game data)").arg(source.path) : QDir::toNativeSeparators(source.path);
		connect(recent->addAction(shown), &QAction::triggered, this, [this, source] { open(source); });
	}
	file->addSeparator();
	action(file, "Save", T("&Save"), QKeySequence::Save, [this] { save(); });
	action(file, "SaveAs", T("Save &as..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), [this] {
		QString folder = m_source.kind == LayoutSource::Kind::File ? QFileInfo(m_source.path).absolutePath() : m_saveFolder;
		if (folder.isEmpty())
			folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
		const QString path = QFileDialog::getSaveFileName(this, T("Save layout"), QDir(folder).filePath(m_fileName),
			T("Window layouts (*.wnd)"));
		if (!path.isEmpty())
			saveAs(path);
	});
	action(file, "Reload", T("Re&load"), QKeySequence::Refresh, [this] { reload(); });
	file->addSeparator();
	action(file, "CompareSaved", T("Compare with the &saved version"), QKeySequence(), [this] { compareWith(QString()); });
	action(file, "CompareWith", T("&Compare with..."), QKeySequence(), [this] {
		const QString start = m_source.kind == LayoutSource::Kind::File ? QFileInfo(m_source.path).absolutePath() : m_saveFolder;
		const QString path = QFileDialog::getOpenFileName(this, T("Compare with layout"), start, T("Window layouts (*.wnd)"));
		if (!path.isEmpty())
			compareWith(path);
	});
	file->addSeparator();
	action(file, "Quit", T("&Quit"), QKeySequence::Quit, [this] { close(); });

	QMenu *editMenu = bar->addMenu(T("&Edit"));
	action(editMenu, "Undo", T("&Undo"), QKeySequence::Undo, [this] { undo(); });
	action(editMenu, "Redo", T("&Redo"), QKeySequence::Redo, [this] { redo(); });
	editMenu->addSeparator();
	action(editMenu, "Cut", T("Cu&t"), QKeySequence::Cut, [this] { cutSelection(); });
	action(editMenu, "Copy", T("&Copy"), QKeySequence::Copy, [this] { copySelection(); });
	action(editMenu, "Paste", T("&Paste"), QKeySequence::Paste, [this] { paste(); });
	action(editMenu, "Duplicate", T("&Duplicate"), QKeySequence(Qt::CTRL | Qt::Key_D), [this] { duplicateSelection(); });
	action(editMenu, "Delete", T("De&lete"), QKeySequence::Delete, [this] { deleteSelection(); });
	editMenu->addSeparator();
	action(editMenu, "Forward", T("Bring &forward"), QKeySequence(Qt::CTRL | Qt::Key_PageUp), [this] { reorderSelection(1); });
	action(editMenu, "Backward", T("Send &backward"), QKeySequence(Qt::CTRL | Qt::Key_PageDown), [this] { reorderSelection(-1); });
	action(editMenu, "Front", T("Bring to fr&ont"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_PageUp), [this] { reorderSelectionToEnd(true); });
	action(editMenu, "Back", T("Send to bac&k"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_PageDown), [this] { reorderSelectionToEnd(false); });
	editMenu->addSeparator();
	QMenu *arrange = editMenu->addMenu(T("&Arrange"));
	arrange->setObjectName(QStringLiteral("ArrangeMenu"));
	const std::pair<const char *, std::pair<QString, WndModel::Align>> aligns[] = {
		{"AlignLeft", {T("Align &left edges"), WndModel::Align::Left}},
		{"AlignCenterX", {T("Align &centres (vertical line)"), WndModel::Align::CenterX}},
		{"AlignRight", {T("Align &right edges"), WndModel::Align::Right}},
		{"AlignTop", {T("Align &top edges"), WndModel::Align::Top}},
		{"AlignCenterY", {T("Align c&entres (horizontal line)"), WndModel::Align::CenterY}},
		{"AlignBottom", {T("Align &bottom edges"), WndModel::Align::Bottom}}};
	for (const auto &[name, item] : aligns)
	{
		const WndModel::Align how = item.second;
		action(arrange, name, item.first, QKeySequence(), [this, how] { alignSelection(how); });
	}
	arrange->addSeparator();
	action(arrange, "DistributeX", T("Spread &horizontally"), QKeySequence(), [this] { distributeSelection(true); });
	action(arrange, "DistributeY", T("Spread &vertically"), QKeySequence(), [this] { distributeSelection(false); });
	arrange->addSeparator();
	action(arrange, "SameWidth", T("Same &width"), QKeySequence(), [this] { sameSizeSelection(true, false); });
	action(arrange, "SameHeight", T("Same hei&ght"), QKeySequence(), [this] { sameSizeSelection(false, true); });
	action(arrange, "SameSize", T("Same &size"), QKeySequence(), [this] { sameSizeSelection(true, true); });

	QMenu *insert = bar->addMenu(T("&Insert"));
	for (const TypeName &t : insertTypes())
	{
		const QString type = QString::fromLatin1(t.type);
		QAction *a = insert->addAction(t.shown);
		a->setObjectName(QStringLiteral("Insert") + type);
		connect(a, &QAction::triggered, this, [this, type] { insertWindow(type); });
		m_actions.insert(a->objectName(), a);
	}

	QMenu *schemeMenu = bar->addMenu(T("S&cheme"));
	QAction *current = schemeMenu->addAction(m_scheme ? T("Scheme: %1").arg(QFileInfo(m_schemePath).fileName()) : T("No scheme loaded"));
	current->setEnabled(false);
	schemeMenu->addSeparator();
	action(schemeMenu, "SchemeLoad", T("&Load scheme..."), QKeySequence(), [this] {
		const QString path = QFileDialog::getOpenFileName(this, T("Load scheme"), QFileInfo(m_schemePath).absolutePath(),
			T("Layout schemes (*.ls)"));
		if (!path.isEmpty() && !loadScheme(path))
			QMessageBox::warning(this, T("GUI Editor"), T("%1 is no layout scheme.").arg(QDir::toNativeSeparators(path)));
	});
	action(schemeMenu, "SchemeSave", T("&Save the layout's scheme as..."), QKeySequence(), [this] {
		QString folder = m_schemePath.isEmpty() ? m_saveFolder : QFileInfo(m_schemePath).absolutePath();
		const QString path = QFileDialog::getSaveFileName(this, T("Save scheme"), QDir(folder).filePath(QFileInfo(m_fileName).completeBaseName() + QStringLiteral(".ls")),
			T("Layout schemes (*.ls)"));
		if (!path.isEmpty() && !saveLayoutScheme(path))
			QMessageBox::warning(this, T("GUI Editor"), T("%1 cannot be written.").arg(QDir::toNativeSeparators(path)));
	});
	action(schemeMenu, "SchemeClear", T("&Forget the scheme"), QKeySequence(), [this] { clearScheme(); });
	schemeMenu->addSeparator();
	action(schemeMenu, "SchemeApplyLayout", T("Apply to the &layout"), QKeySequence(), [this] { applyScheme(false); });
	action(schemeMenu, "SchemeApplySelection", T("Apply to the s&elected windows"), QKeySequence(), [this] { applyScheme(true); });

	QMenu *viewMenu = bar->addMenu(T("&View"));
	QMenu *resolutionMenu = viewMenu->addMenu(T("&Resolution"));
	auto *group = new QActionGroup(resolutionMenu);
	for (std::size_t i = 0; i < resolutions().size(); ++i)
	{
		const Resolution r = resolutions()[i];
		QString text = QStringLiteral("%1 \u00d7 %2").arg(r.width).arg(r.height);
		if (i == 0)
			text += QStringLiteral("  \u2013  ") + T("the layouts' own");
		QAction *a = resolutionMenu->addAction(text);
		a->setCheckable(true);
		a->setChecked(static_cast<int>(i) == m_resolution);
		group->addAction(a);
		connect(a, &QAction::triggered, this, [this, i] { setResolution(static_cast<int>(i)); });
	}
	QAction *hidden = viewMenu->addAction(T("Show &hidden windows"));
	hidden->setCheckable(true);
	hidden->setChecked(m_showHidden);
	hidden->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
	hidden->setToolTip(T("Shows the windows the game shows only later (transitions, game events); the file stays as it is."));
	connect(hidden, &QAction::toggled, this, [this](bool on) { setShowHidden(on); });
	QAction *outlines = viewMenu->addAction(T("&Outline hidden and see-through windows"));
	outlines->setCheckable(true);
	outlines->setChecked(m_outlines);
	connect(outlines, &QAction::toggled, this, [this](bool on) { setOutlines(on); });
	QAction *background = viewMenu->addAction(T("&Background colour..."));
	connect(background, &QAction::triggered, this, [this] {
		const QColor c = QColorDialog::getColor(m_background, this, T("Background colour"));
		if (c.isValid())
			setBackground(c);
	});
	QAction *test = viewMenu->addAction(T("&Test mode"));
	test->setObjectName(QStringLiteral("TestMode"));
	test->setCheckable(true);
	test->setChecked(m_testMode);
	test->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
	test->setToolTip(T("Mouse and keys work the windows as in the game; the layout stays as it is."));
	connect(test, &QAction::toggled, this, [this](bool on) { setTestMode(on); });
	m_actions.insert(test->objectName(), test);
	QAction *picture = viewMenu->addAction(T("Background &picture..."));
	picture->setToolTip(T("A scene of the game under the layout, to see it as in the game."));
	connect(picture, &QAction::triggered, this, [this] {
		const QString path = QFileDialog::getOpenFileName(this, T("Background picture"), QFileInfo(m_backgroundImage).absolutePath(),
			T("Pictures (*.png *.jpg *.jpeg *.bmp *.tga)"));
		if (!path.isEmpty() && !setBackgroundImage(path))
			QMessageBox::warning(this, T("GUI Editor"), T("%1 cannot be read.").arg(QDir::toNativeSeparators(path)));
	});
	QAction *noPicture = viewMenu->addAction(T("No background picture"));
	noPicture->setEnabled(!m_backgroundImage.isEmpty());
	connect(noPicture, &QAction::triggered, this, [this] {
		setBackgroundImage(QString());
		buildMenus();
	});
	QMenu *gridMenu = viewMenu->addMenu(T("&Grid"));
	QAction *gridShown = gridMenu->addAction(T("&Show grid"));
	gridShown->setCheckable(true);
	gridShown->setChecked(m_gridShown);
	gridShown->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
	connect(gridShown, &QAction::toggled, this, [this](bool on) { setGrid(on, m_snap, m_gridStep); });
	QAction *snap = gridMenu->addAction(T("S&nap to grid"));
	snap->setCheckable(true);
	snap->setChecked(m_snap);
	snap->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
	connect(snap, &QAction::toggled, this, [this](bool on) { setGrid(m_gridShown, on, m_gridStep); });
	gridMenu->addSeparator();
	auto *steps = new QActionGroup(gridMenu);
	for (const int step : {2, 4, 5, 8, 10, 16, 20, 25, 50})
	{
		QAction *a = gridMenu->addAction(T("%1 pixels").arg(step));
		a->setCheckable(true);
		a->setChecked(step == m_gridStep);
		steps->addAction(a);
		connect(a, &QAction::triggered, this, [this, step] { setGrid(m_gridShown, m_snap, step); });
	}
	viewMenu->addSeparator();
	for (QDockWidget *d : {m_layoutsDock, m_treeDock, m_propertiesDock, m_historyDock, m_problemsDock, m_textDock, m_compareDock, m_testDock})
		viewMenu->addAction(d->toggleViewAction());

	QMenu *settingsMenu = bar->addMenu(T("&Settings"));
	QMenu *themeMenu = settingsMenu->addMenu(T("&Theme"));
	auto *themes = new QActionGroup(themeMenu);
	const std::pair<ThemeKind, QString> themeItems[] = {{ThemeKind::Dark, T("Dark")}, {ThemeKind::Light, T("Light")}};
	for (const auto &[kind, text] : themeItems)
	{
		QAction *a = themeMenu->addAction(text);
		a->setCheckable(true);
		a->setChecked(kind == m_theme);
		themes->addAction(a);
		const ThemeKind chosen = kind;
		connect(a, &QAction::triggered, this, [this, chosen] { setTheme(chosen); });
	}
	QMenu *languageMenu = settingsMenu->addMenu(T("&Language"));
	auto *languages = new QActionGroup(languageMenu);
	const std::pair<Language, QString> languageItems[] = {{Language::English, QStringLiteral("English")}, {Language::German, QStringLiteral("Deutsch")}};
	for (const auto &[kind, text] : languageItems)
	{
		QAction *a = languageMenu->addAction(text);
		a->setCheckable(true);
		a->setChecked(kind == currentLanguage());
		languages->addAction(a);
		const Language chosen = kind;
		connect(a, &QAction::triggered, this, [this, chosen] { QTimer::singleShot(0, this, [this, chosen] { setLanguage(chosen); }); });
	}
	updateActions();
}

void EditorWindow::updateActions()
{
	const auto enable = [this](const char *name, bool on, const QString &text = QString()) {
		if (QAction *a = m_actions.value(QString::fromLatin1(name)))
		{
			a->setEnabled(on);
			if (!text.isEmpty())
				a->setText(text);
		}
	};
	const bool selected = !m_selection.isEmpty();
	enable("Undo", m_doc.canUndo(), m_doc.canUndo() ? T("&Undo: %1").arg(latin1(m_doc.undoText())) : T("&Undo"));
	enable("Redo", m_doc.canRedo(), m_doc.canRedo() ? T("&Redo: %1").arg(latin1(m_doc.redoText())) : T("&Redo"));
	for (const char *name : {"Cut", "Copy", "Duplicate", "Delete", "Forward", "Backward", "Front", "Back"})
		enable(name, selected);
	const std::size_t arranged = arrangePaths().size();
	for (const char *name : {"AlignLeft", "AlignCenterX", "AlignRight", "AlignTop", "AlignCenterY", "AlignBottom", "SameWidth", "SameHeight", "SameSize"})
		enable(name, arranged >= 2);
	enable("DistributeX", arranged >= 3);
	enable("DistributeY", arranged >= 3);
	enable("Reload", m_source.kind != LayoutSource::Kind::None);
	enable("Save", m_doc.modified() || m_source.kind != LayoutSource::Kind::File);
	enable("CompareSaved", m_source.kind != LayoutSource::Kind::None);
	enable("SchemeClear", m_scheme.has_value());
	enable("SchemeApplyLayout", m_scheme && !m_doc.layout().windows().empty());
	enable("SchemeApplySelection", m_scheme && selected);
	enable("SchemeSave", !m_doc.layout().windows().empty());
}

QWidget *EditorWindow::buildLayoutsPage()
{
	auto *page = new QWidget;
	auto *box = new QVBoxLayout(page);
	box->setContentsMargins(6, 6, 6, 6);
	auto *filter = new QLineEdit;
	filter->setObjectName(QStringLiteral("LayoutFilter"));
	filter->setPlaceholderText(T("Search layouts"));
	filter->setClearButtonEnabled(true);
	filter->setText(m_layoutFilter);
	auto *list = new QListWidget;
	list->setObjectName(QStringLiteral("LayoutList"));
	for (const QString &path : m_gameLayouts)
	{
		auto *item = new QListWidgetItem(path, list);
		if (m_source.kind == LayoutSource::Kind::Game && m_source.path.compare(path, Qt::CaseInsensitive) == 0)
		{
			QFont font = item->font();
			font.setBold(true);
			item->setFont(font);
			list->setCurrentItem(item);
		}
		item->setHidden(!m_layoutFilter.isEmpty() && !path.contains(m_layoutFilter, Qt::CaseInsensitive));
	}
	if (m_gameLayouts.isEmpty())
		new QListWidgetItem(m_engine ? T("The game data have no layouts.") : T("The engine is not started."), list);
	connect(filter, &QLineEdit::textChanged, list, [this, list](const QString &text) {
		m_layoutFilter = text;
		for (int i = 0; i < list->count(); ++i)
			list->item(i)->setHidden(!text.isEmpty() && !list->item(i)->text().contains(text, Qt::CaseInsensitive));
	});
	const auto openItem = [this](QListWidgetItem *item) {
		if (item && m_gameLayouts.contains(item->text()))
		{
			const QString path = item->text();
			QTimer::singleShot(0, this, [this, path] { open({LayoutSource::Kind::Game, path}); });
		}
	};
	connect(list, &QListWidget::itemActivated, this, openItem);
	connect(filter, &QLineEdit::returnPressed, list, [list, openItem] {
		for (int i = 0; i < list->count(); ++i)
			if (!list->item(i)->isHidden())
			{
				openItem(list->item(i));
				return;
			}
	});
	box->addWidget(filter);
	box->addWidget(list, 1);
	box->addWidget(label(T("%1 layouts; double-click opens one, saving makes a copy.").arg(m_gameLayouts.size()), "Hint"));
	return page;
}

QWidget *EditorWindow::buildTreePage()
{
	auto *page = new QWidget;
	auto *box = new QVBoxLayout(page);
	box->setContentsMargins(6, 6, 6, 6);
	auto *filter = new QLineEdit;
	filter->setObjectName(QStringLiteral("TreeFilter"));
	filter->setPlaceholderText(T("Search windows"));
	filter->setClearButtonEnabled(true);
	filter->setText(m_treeFilter);
	m_tree = new WindowTree;
	m_tree->setObjectName(QStringLiteral("WindowTree"));
	m_tree->setColumnCount(2);
	m_tree->setHeaderLabels({T("Name"), T("Type")});
	m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	m_tree->header()->setStretchLastSection(false);
	m_tree->setToolTip(T("Later in the list is drawn above. Drag windows to put them into another window or in another order."));
	m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint &at) {
		if (QTreeWidgetItem *item = m_tree->itemAt(at); item && !item->isSelected())
			setSelection({item->data(0, Qt::UserRole).toString()});
		contextMenu()->popup(m_tree->viewport()->mapToGlobal(at));
	});
	connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] {
		if (m_filling)
			return;
		QStringList names;
		for (QTreeWidgetItem *item : m_tree->selectedItems())
			names << item->data(0, Qt::UserRole).toString();
		// The current item is the one the properties show: it goes last.
		if (QTreeWidgetItem *current = m_tree->currentItem(); current && current->isSelected())
		{
			const QString name = current->data(0, Qt::UserRole).toString();
			names.removeAll(name);
			names << name;
		}
		if (names == m_selection)
			return;
		m_selection = names;
		showSelection();
	});
	m_tree->onDrop = [this](const QList<QTreeWidgetItem *> &items, QTreeWidgetItem *target, WindowTree::Drop where) {
		QStringList names;
		for (QTreeWidgetItem *item : items)
			names << item->data(0, Qt::UserRole).toString();
		QString parent;
		int index = 1 << 20;
		if (target)
		{
			const QString name = target->data(0, Qt::UserRole).toString();
			const std::optional<WndModel::WindowPath> path = WndModel::findWindow(m_doc.layout(), toLatin1(name));
			const WndModel::Window *w = path ? WndModel::windowAt(m_doc.layout(), *path) : nullptr;
			if (where == WindowTree::Drop::Into && w && WndModel::canHaveChildren(*w))
				parent = name;
			else if (path)
			{
				if (QTreeWidgetItem *up = target->parent())
					parent = up->data(0, Qt::UserRole).toString();
				index = static_cast<int>(path->back()) + (where == WindowTree::Drop::Before ? 0 : 1);
			}
		}
		QTimer::singleShot(0, this, [this, names, parent, index] { moveInto(names, parent, index); });
	};
	connect(filter, &QLineEdit::textChanged, this, [this](const QString &text) {
		m_treeFilter = text;
		fillTree();
	});
	box->addWidget(filter);
	box->addWidget(m_tree, 1);
	return page;
}

void EditorWindow::fillTree()
{
	if (!m_tree)
		return;
	m_filling = true;
	const QSignalBlocker block(m_tree);
	m_tree->clear();
	m_items.clear();
	const Tokens t = tokens(m_theme, m_game);
	std::vector<QTreeWidgetItem *> parents;
	WndModel::forEachWindow(m_doc.layout(), [&](const WndModel::Window &window, int depth) {
		parents.resize(static_cast<std::size_t>(depth));
		auto *item = depth == 0 ? new QTreeWidgetItem(m_tree) : new QTreeWidgetItem(parents.back());
		const QString full = latin1(window.fullName());
		const QString name = latin1(window.name());
		item->setText(0, name.isEmpty() ? T("(no name)") : name);
		item->setText(1, latin1(window.type()));
		item->setData(0, Qt::UserRole, full);
		Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
		if (WndModel::canHaveChildren(window))
			flags |= Qt::ItemIsDropEnabled;
		item->setFlags(flags);
		if (window.hasStatus("HIDDEN"))
		{
			item->setForeground(0, t.tx3);
			item->setForeground(1, t.tx3);
			item->setToolTip(0, T("Hidden: the game shows it later"));
		}
		if (m_editorHidden.contains(full))
		{
			item->setText(0, item->text(0) + QStringLiteral("  ") + T("(hidden in the editor)"));
			item->setForeground(0, t.tx3);
		}
		if (m_locked.contains(full))
			item->setText(0, item->text(0) + QStringLiteral("  ") + T("(locked)"));
		if (!full.isEmpty() && !m_items.contains(full))
			m_items.insert(full, item);
		parents.push_back(item);
	});
	m_tree->expandAll();
	if (!m_treeFilter.isEmpty())
	{
		// Matches stay with their parents; everything else hides.
		const std::function<bool(QTreeWidgetItem *)> filter = [&](QTreeWidgetItem *item) {
			bool visible = item->text(0).contains(m_treeFilter, Qt::CaseInsensitive)
				|| item->text(1).contains(m_treeFilter, Qt::CaseInsensitive);
			for (int i = 0; i < item->childCount(); ++i)
				visible = filter(item->child(i)) || visible;
			item->setHidden(!visible);
			return visible;
		};
		for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
			filter(m_tree->topLevelItem(i));
	}
	for (const QString &name : m_selection)
		if (QTreeWidgetItem *item = m_items.value(name))
			item->setSelected(true);
	if (!m_selection.isEmpty())
		if (QTreeWidgetItem *item = m_items.value(m_selection.back()))
			m_tree->setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
	m_filling = false;
}

void EditorWindow::fillProblems()
{
	setDockPage(m_problemsDock, T("Problems (%1)").arg(m_problems.size()), buildProblemsPage());
}

QWidget *EditorWindow::buildProblemsPage()
{
	auto *list = new QListWidget;
	list->setObjectName(QStringLiteral("ProblemList"));
	const Tokens t = tokens(m_theme, m_game);
	for (const WndModel::Problem &p : m_problems)
	{
		const bool error = p.severity == WndModel::Problem::Severity::Error;
		QString text = error ? T("Error") : T("Warning");
		if (!p.window.empty())
			text += QStringLiteral(" \u2013 ") + latin1(p.window);
		text += QStringLiteral(": ") + latin1(p.text);
		auto *item = new QListWidgetItem(text, list);
		item->setData(Qt::UserRole, latin1(p.window));
		if (error)
			item->setForeground(t.error);
	}
	if (m_problems.empty())
		new QListWidgetItem(m_doc.layout().windows().empty() ? QString() : T("No problems: the game reads the layout as the file says."), list);
	connect(list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
		const QString window = item->data(Qt::UserRole).toString();
		if (window.isEmpty())
			return;
		// The check names windows without the file in front.
		for (auto it = m_items.cbegin(); it != m_items.cend(); ++it)
			if (it.key() == window || it.key().endsWith(QLatin1Char(':') + window))
			{
				selectWindow(it.key());
				return;
			}
	});
	return list;
}

void EditorWindow::fillText()
{
	if (auto *text = child<QPlainTextEdit>(this, "FileText"))
	{
		const std::string s = m_doc.text();
		text->setPlainText(latin1(s));
	}
}

QWidget *EditorWindow::buildTextPage()
{
	auto *text = new QPlainTextEdit;
	text->setObjectName(QStringLiteral("FileText"));
	text->setReadOnly(true);
	text->setLineWrapMode(QPlainTextEdit::NoWrap);
	text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	return text;
}

void EditorWindow::showSelection()
{
	fillProperties();
	updateOverlay();
	const QString primary = selectedWindow();
	if (auto *text = child<QPlainTextEdit>(this, "FileText"); text && !primary.isEmpty())
	{
		QTextCursor found = text->document()->find(QStringLiteral("\"%1\"").arg(primary));
		if (!found.isNull())
		{
			found.movePosition(QTextCursor::StartOfLine);
			text->setTextCursor(found);
			text->centerCursor();
		}
	}
	if (m_engine && !primary.isEmpty() && m_gesture == Gesture::None)
	{
		const QRect r = GuiEngine::instance().windowRect(primary);
		if (!r.isEmpty())
		{
			const qreal ratio = m_view->devicePixelRatioF();
			m_scroll->ensureVisible(static_cast<int>(r.center().x() / ratio), static_cast<int>(r.center().y() / ratio),
				static_cast<int>(r.width() / ratio / 2) + 20, static_cast<int>(r.height() / ratio / 2) + 20);
		}
	}
	updateActions();
	updateStatus();
}

int EditorWindow::treeCount() const
{
	// Every row: windows without a name have one too, but no entry in m_items.
	int n = 0;
	if (m_tree)
		for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
			++n;
	return n;
}

void EditorWindow::updateTitle()
{
	QString title = T("GUI Editor") + QStringLiteral(" \u2013 ") + gameName(m_game);
	title = m_fileName + (m_doc.modified() ? QStringLiteral(" *") : QString()) + QStringLiteral(" \u2013 ") + title;
	setWindowTitle(title);
}

void EditorWindow::updateStatus()
{
	if (!m_statusLeft)
		return;
	QString left;
	if (!m_error.isEmpty())
		left = m_error;
	else if (m_source.kind == LayoutSource::Kind::Game)
		left = T("%1 (game data, saving makes a copy)").arg(m_source.path);
	else if (m_source.kind == LayoutSource::Kind::File)
		left = QDir::toNativeSeparators(m_source.path);
	else
		left = T("New layout, not saved yet");
	if (!m_selection.isEmpty())
		left += QStringLiteral("  \u00b7  ") + (m_selection.size() == 1 ? m_selection.back() : T("%1 windows selected").arg(m_selection.size()));
	m_statusLeft->setText(left);
	const Resolution r = resolution();
	QString right = QStringLiteral("%1 \u00d7 %2").arg(r.width).arg(r.height);
	if (m_engineCount >= 0)
		right = T("%1 windows").arg(m_engineCount) + QStringLiteral("  \u00b7  ") + right;
	if (m_showHidden)
		right = T("hidden windows shown") + QStringLiteral("  \u00b7  ") + right;
	if (m_scheme)
		right = T("scheme %1").arg(QFileInfo(m_schemePath).fileName()) + QStringLiteral("  \u00b7  ") + right;
	if (m_testMode)
		right = T("test mode") + QStringLiteral("  \u00b7  ") + right;
	m_statusRight->setText(right);
}

void EditorWindow::addRecent(const LayoutSource &source)
{
	if (source.kind == LayoutSource::Kind::None)
		return;
	m_recent.removeAll(source.key());
	m_recent.prepend(source.key());
	while (m_recent.size() > MAX_RECENT)
		m_recent.removeLast();
	saveSettings();
}

void EditorWindow::saveSettings() const
{
	if (!m_interactive)
		return;
	QSettings s;
	s.setValue(QStringLiteral("resolution"), m_resolution);
	s.setValue(QStringLiteral("showHidden"), m_showHidden);
	s.setValue(QStringLiteral("theme"), m_theme == ThemeKind::Light ? 1 : 0);
	s.setValue(QStringLiteral("language"), currentLanguage() == Language::German ? 1 : 0);
	s.setValue(QStringLiteral("recent"), m_recent);
	s.setValue(QStringLiteral("saveFolder"), m_saveFolder);
	s.setValue(QStringLiteral("gridStep"), m_gridStep);
	s.setValue(QStringLiteral("gridShown"), m_gridShown);
	s.setValue(QStringLiteral("snap"), m_snap);
	s.setValue(QStringLiteral("outlines"), m_outlines);
	s.setValue(QStringLiteral("background"), m_background.name());
	s.setValue(QStringLiteral("scheme"), m_schemePath);
	s.setValue(QStringLiteral("backgroundImage"), m_backgroundImage);
}

void EditorWindow::closeEvent(QCloseEvent *event)
{
	if (!maybeSave())
	{
		event->ignore();
		return;
	}
	if (m_interactive)
	{
		QSettings s;
		s.setValue(QStringLiteral("geometry"), saveGeometry());
		s.setValue(QStringLiteral("state"), saveState());
		s.setValue(QStringLiteral("last"), m_source.key());
	}
	saveSettings();
	m_frames->stop();
	QMainWindow::closeEvent(event);
}

void EditorWindow::buildToolBar()
{
	// The actions of the menus; the toolbar is rebuilt with them (language, theme).
	QToolBar *bar = findChild<QToolBar *>(QStringLiteral("MainToolBar"));
	if (!bar)
	{
		bar = addToolBar(T("Tools"));
		bar->setObjectName(QStringLiteral("MainToolBar"));
		bar->setMovable(false);
	}
	bar->clear();
	bar->setWindowTitle(T("Tools"));
	const auto add = [&](const char *name) {
		if (QAction *a = m_actions.value(QString::fromLatin1(name)))
			bar->addAction(a);
	};
	for (const char *name : {"New", "Open", "Save"})
		add(name);
	bar->addSeparator();
	for (const char *name : {"Undo", "Redo"})
		add(name);
	bar->addSeparator();
	auto *insert = new QToolButton(bar);
	insert->setText(T("Insert"));
	insert->setPopupMode(QToolButton::InstantPopup);
	auto *menu = new QMenu(insert);
	for (auto it = m_actions.cbegin(); it != m_actions.cend(); ++it)
		if (it.key().startsWith(QLatin1String("Insert")))
			menu->addAction(it.value());
	insert->setMenu(menu);
	bar->addWidget(insert);
	auto *arrange = new QToolButton(bar);
	arrange->setText(T("Arrange"));
	arrange->setPopupMode(QToolButton::InstantPopup);
	if (auto *arrangeMenu = menuBar()->findChild<QMenu *>(QStringLiteral("ArrangeMenu")))
		arrange->setMenu(arrangeMenu);
	bar->addWidget(arrange);
	bar->addSeparator();
	add("TestMode");
}

} // namespace GuiEditorApp
