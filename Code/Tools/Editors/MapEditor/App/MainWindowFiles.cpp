/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowFiles.cpp -- opening and saving maps, the recent list and the preview picture written with every save.

#include "App/MainWindow.h"
#include "App/Common.h"
#include "App/EngineClient.h"
#include "App/Icons.h"
#include "App/Language.h"
#include "App/MapImage.h"
#include "App/MapOverview.h"
#include "App/MiniMap.h"
#include "App/ToolEvent.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/SidesCommands.h"
#include "Model/MapDump.h"
#include "Model/MapIO.h"
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

namespace MapEditorApp
{

// Files

void MainWindow::openDialog()
{
	QString start = m_state.userMapFolder(m_state.game);
	if (!QDir(start).exists())
		start = QDir::homePath();
	const QString path = QFileDialog::getOpenFileName(this, T("Open map"), start, T("Maps (*.map);;All files (*)"));
	if (!path.isEmpty())
		openMapFile(path);
}

bool MainWindow::openMapFile(const QString &path, QString *error)
{
	if (!confirmUnsavedChanges())
		return false;
	auto open = std::make_unique<OpenMap>();
	open->path = path;
	open->loaded = MapModel::loadMap(toFsPath(path));
	if (!open->loaded.result.ok())
	{
		const QString message = T("The map cannot be read: %1").arg(utf8(open->loaded.result.error));
		if (error)
			*error = message;
		toast(message);
		return false;
	}
	// A map from a game's user folder selects that game.
	for (Game g : {Game::Generals, Game::ZeroHour})
		if (QDir::cleanPath(path).startsWith(QDir::cleanPath(m_state.userMapFolder(g)), Qt::CaseInsensitive))
			m_state.game = g;
	m_state.map = std::move(open);
	++m_mapSerial;
	mapReplaced();
	m_state.addRecent(path);
	m_state.save();
	m_page = Page::Map;
	applySettings();
	return true;
}

void MainWindow::showMapModel(MapModel::Map map, const QString &name)
{
	if (!confirmUnsavedChanges())
		return;
	auto open = std::make_unique<OpenMap>();
	open->loaded.result.map = std::move(map);
	open->loaded.compression = 1;
	(void)name;
	m_state.map = std::move(open);
	++m_mapSerial;
	mapReplaced();
	m_page = Page::Map;
	rebuild();
}

void MainWindow::save(bool saveAs)
{
	if (!m_state.map)
		return;
	// What the view shows is what is saved: a mold being placed goes in as a step.
	finishMold(true);
	QString path = m_state.map->path;
	if (saveAs || path.isEmpty())
	{
		path = QFileDialog::getSaveFileName(this, T("Save map as"), path.isEmpty() ? m_state.userMapFolder(m_state.game) : path, T("Maps (*.map)"));
		if (path.isEmpty())
			return;
	}
	std::string error;
	if (!MapModel::saveMap(toFsPath(path), m_state.map->loaded.result.map, error))
	{
		toast(T("Saving failed: %1").arg(utf8(error)));
		return;
	}
	m_state.map->path = path;
	m_state.map->history.markSaved();
	refreshHistoryWindow();
	markAutosaveClean();
	// The old editor wrote the preview picture with every save; the game shows it in its map lists.
	if (!writePreview(path))
		toast(T("The map is saved, but its preview picture could not be written."));
	m_state.addRecent(path);
	m_state.save();
	toast(T("Map saved"));
	updateTitle();
}

void MainWindow::closeMap()
{
	if (!confirmUnsavedChanges())
		return;
	m_state.map.reset();
	markAutosaveClean();
	++m_mapSerial;
	mapReplaced();
	EngineClient::instance().unloadMap();
	m_viewMapSerial = -1;
	showPage(Page::Start);
}

void MainWindow::toast(const QString &text)
{
	const Tokens t = tokens(m_state.theme, m_state.game);
	auto *box = new QLabel(text, this);
	box->setWordWrap(true);
	box->setMaximumWidth(560);
	box->setStyleSheet(QStringLiteral("background: %1; color: %2; border: 1px solid %3; border-radius: 10px; padding: 10px 16px;")
		.arg(t.s1.name(), t.tx.name(), t.bd.name()));
	box->adjustSize();
	box->move((width() - box->width()) / 2, height() - box->height() - 48);
	box->show();
	box->raise();
	QTimer::singleShot(4000, box, &QObject::deleteLater);
	if (m_statusLeft)
		m_statusLeft->setText(text);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	if (!confirmUnsavedChanges())
	{
		event->ignore();
		return;
	}
	m_state.save();
	markAutosaveClean();
	// The device belongs to the view's window: shut the engine down while it still exists.
	EngineClient::instance().stop();
	event->accept();
}

} // namespace MapEditorApp
