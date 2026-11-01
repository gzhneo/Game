/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EditorArrange.cpp -- the main window of the GUI editor: arranging several windows, the undo history, the
// autosave copy, and windows hidden or locked in the editor only (the file keeps them as they are).

#include "App/EditorWindow.h"

#include "App/EditorShared.h"
#include "EditorCommon/Language.h"

#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStatusBar>

#include <algorithm>

namespace GuiEditorApp
{

using namespace EditorCommon;

// ---------------------------------------------------------------------------------------------------------
// Arranging

std::vector<WndModel::WindowPath> EditorWindow::arrangePaths() const
{
	// In the order of the selection, so the last one is the one the others follow; a window inside another
	// selected one moves with it and does not count on its own.
	std::vector<WndModel::WindowPath> all;
	for (const QString &name : m_selection)
		if (const std::optional<WndModel::WindowPath> p = WndModel::findWindow(m_doc.layout(), toLatin1(name)))
			all.push_back(*p);
	std::vector<WndModel::WindowPath> out;
	for (const WndModel::WindowPath &p : all)
		if (std::none_of(all.begin(), all.end(), [&p](const WndModel::WindowPath &q) { return q != p && WndModel::contains(q, p); }))
			out.push_back(p);
	return out;
}

bool EditorWindow::alignSelection(WndModel::Align align)
{
	const std::vector<WndModel::WindowPath> paths = arrangePaths();
	return paths.size() >= 2 && edit(T("Align"), [&](WndModel::Layout &l) { WndModel::alignWindows(l, paths, align); });
}

bool EditorWindow::distributeSelection(bool horizontal)
{
	const std::vector<WndModel::WindowPath> paths = arrangePaths();
	return paths.size() >= 3 && edit(T("Distribute"), [&](WndModel::Layout &l) { WndModel::distributeWindows(l, paths, horizontal); });
}

bool EditorWindow::sameSizeSelection(bool width, bool height)
{
	const std::vector<WndModel::WindowPath> paths = arrangePaths();
	return paths.size() >= 2 && edit(T("Same size"), [&](WndModel::Layout &l) { WndModel::sameSize(l, paths, width, height); });
}

// ---------------------------------------------------------------------------------------------------------
// History

QWidget *EditorWindow::buildHistoryPage()
{
	auto *list = new QListWidget;
	list->setObjectName(QStringLiteral("HistoryList"));
	connect(list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) { goToStep(item->data(Qt::UserRole).toInt()); });
	return list;
}

void EditorWindow::fillHistory()
{
	auto *list = m_historyDock ? qobject_cast<QListWidget *>(m_historyDock->widget()) : nullptr;
	if (!list)
		return;
	const Tokens t = tokens(m_theme, m_game);
	const std::vector<std::string> done = m_doc.undoSteps();
	const std::vector<std::string> undone = m_doc.redoSteps();
	list->clear();
	auto *start = new QListWidgetItem(T("(as opened)"), list);
	start->setData(Qt::UserRole, 0);
	for (std::size_t i = 0; i < done.size(); ++i)
	{
		auto *item = new QListWidgetItem(latin1(done[i]), list);
		item->setData(Qt::UserRole, static_cast<int>(i + 1));
	}
	for (std::size_t i = 0; i < undone.size(); ++i)
	{
		auto *item = new QListWidgetItem(latin1(undone[i]), list);
		item->setData(Qt::UserRole, static_cast<int>(done.size() + i + 1));
		item->setForeground(t.tx3);
	}
	list->setCurrentRow(static_cast<int>(done.size()));
}

void EditorWindow::goToStep(int applied)
{
	bool moved = false;
	while (static_cast<int>(m_doc.undoSteps().size()) > applied && m_doc.undo())
		moved = true;
	while (static_cast<int>(m_doc.undoSteps().size()) < applied && m_doc.redo())
		moved = true;
	if (moved)
		changed();
}

// ---------------------------------------------------------------------------------------------------------
// Autosave

QString EditorWindow::autosavePath() const
{
	const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/Autosave");
	return QDir(folder).filePath(m_fileName.isEmpty() ? QStringLiteral("NewLayout.wnd") : m_fileName);
}

bool EditorWindow::autosaveNow()
{
	if (!m_doc.modified())
		return false;
	const QString path = autosavePath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	const std::string text = m_doc.text();
	QSaveFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(text.data(), static_cast<qint64>(text.size())) == static_cast<qint64>(text.size())
		&& file.commit();
}

void EditorWindow::removeAutosave()
{
	QFile::remove(autosavePath());
}

void EditorWindow::offerAutosaves()
{
	const QDir folder(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/Autosave"));
	for (const QFileInfo &file : folder.entryInfoList({QStringLiteral("*.wnd")}, QDir::Files, QDir::Time))
	{
		const QMessageBox::StandardButton answer = QMessageBox::question(this, T("GUI Editor"),
			T("%1 was not saved when the editor ended (%2). Open the copy?")
				.arg(file.fileName(), QLocale().toString(file.lastModified(), QLocale::ShortFormat)),
			QMessageBox::Open | QMessageBox::Discard | QMessageBox::Ignore);
		if (answer == QMessageBox::Open)
		{
			QFile in(file.absoluteFilePath());
			if (!in.open(QIODevice::ReadOnly))
				continue;
			const QByteArray text = in.readAll();
			in.close();
			startDocument(WndModel::Document(), LayoutSource{}, file.fileName());
			edit(T("Autosave copy"), [&](WndModel::Layout &l) { l = WndModel::read(std::string(text.constData(), static_cast<std::size_t>(text.size()))); });
			QFile::remove(file.absoluteFilePath());
			return;
		}
		if (answer == QMessageBox::Discard)
			QFile::remove(file.absoluteFilePath());
	}
}

// ---------------------------------------------------------------------------------------------------------
// Hidden and locked in the editor

void EditorWindow::setHiddenInEditor(const QString &fullName, bool hidden)
{
	if (hidden)
		m_editorHidden.insert(fullName);
	else
		m_editorHidden.remove(fullName);
	if (hidden)
		m_selection.removeAll(fullName);
	changed();
}

void EditorWindow::setLocked(const QString &fullName, bool locked)
{
	if (locked)
		m_locked.insert(fullName);
	else
		m_locked.remove(fullName);
	fillTree();
	updateStatus();
}

std::string EditorWindow::engineText() const
{
	if (m_editorHidden.isEmpty())
		return m_doc.text();
	WndModel::Layout copy = m_doc.layout();
	for (const QString &name : m_editorHidden)
		if (const std::optional<WndModel::WindowPath> p = WndModel::findWindow(copy, toLatin1(name)))
			WndModel::removeWindow(copy, *p);
	return WndModel::write(copy);
}

} // namespace GuiEditorApp
