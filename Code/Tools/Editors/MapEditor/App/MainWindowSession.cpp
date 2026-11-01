/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowSession.cpp -- autosave with recovery, and "Test in game".
//
// Autosave (the old WorldBuilder's CMainFrame::OnTimer and CWorldBuilderDoc::autoSave): while the
// map has unsaved changes, it is written every few minutes into the editor's data folder, the last
// three kept. A note beside them says whether they hold work that was never saved; the start page
// then offers to recover it. Saving or closing the map clears the note.
//
// Test in game (CWorldBuilderDoc::OnJumpToGame): the map is saved and the game started with it, in
// a window. The game finds a map only in a folder of its name; a map elsewhere goes as a copy into
// the user map folder (MapEditorTest), with its map.ini and solo.ini. Particle systems changed in the
// editor go along in the copy's map.ini, which the game reads over its own templates; the map of the
// user stays untouched.

#include "App/Common.h"
#include "App/MainWindow.h"
#include "App/MainWindowParts.h"

#include "App/EngineClient.h"
#include "App/Language.h"
#include "Core/GameLaunch.h"
#include "Model/MapFile.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <filesystem>

namespace MapEditorApp
{

namespace
{

constexpr int AUTOSAVE_FILES = 3;
constexpr const char *TEST_MAP = "MapEditorTest";

} // namespace

QString autosaveFile(const QString &folder, int n)
{
	return QDir(folder).filePath(QStringLiteral("Autosave%1.map").arg(n));
}

QString MainWindow::autosaveFolder() const
{
	if (!m_autosaveFolder.isEmpty())
		return m_autosaveFolder;
	return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("Autosave"));
}

void MainWindow::startAutosaveTimer()
{
	if (!m_autosaveTimer)
	{
		m_autosaveTimer = new QTimer(this);
		connect(m_autosaveTimer, &QTimer::timeout, this, [this] { autosaveNow(); });
	}
	m_autosaveTimer->stop();
	if (m_state.autosave)
		m_autosaveTimer->start(m_state.autosaveSeconds * 1000);
}

bool MainWindow::autosaveNow()
{
	if (!m_state.map || !m_state.map->history.isModified() || m_editSerial == m_autosavedSerial)
		return false;
	const QString folder = autosaveFolder();
	QDir().mkpath(folder);
	// The oldest goes, the others move one place back.
	QFile::remove(autosaveFile(folder, AUTOSAVE_FILES));
	for (int n = AUTOSAVE_FILES - 1; n >= 1; --n)
		QFile::rename(autosaveFile(folder, n), autosaveFile(folder, n + 1));
	std::string error;
	if (!MapModel::saveMap(toFsPath(autosaveFile(folder, 1)), m_state.map->map(), error))
	{
		if (m_statusLeft)
			m_statusLeft->setText(T("Autosave failed: %1").arg(QString::fromStdString(error)));
		return false;
	}
	QSettings note(QDir(folder).filePath(QStringLiteral("Autosave.ini")), QSettings::IniFormat);
	note.setValue(QStringLiteral("pending"), true);
	note.setValue(QStringLiteral("map"), m_state.map->path);
	note.setValue(QStringLiteral("game"), m_state.game == Game::Generals ? QStringLiteral("generals") : QStringLiteral("zh"));
	note.setValue(QStringLiteral("time"), QDateTime::currentDateTime());
	note.sync();
	m_autosavedSerial = m_editSerial;
	if (m_statusLeft)
		m_statusLeft->setText(T("Autosaved at %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"))));
	return true;
}

void MainWindow::markAutosaveClean()
{
	const QString path = QDir(autosaveFolder()).filePath(QStringLiteral("Autosave.ini"));
	if (!QFile::exists(path))
		return;
	QSettings note(path, QSettings::IniFormat);
	note.setValue(QStringLiteral("pending"), false);
	note.sync();
}

MainWindow::AutosaveInfo MainWindow::autosaveInfo() const
{
	AutosaveInfo info;
	const QString folder = autosaveFolder();
	const QString path = QDir(folder).filePath(QStringLiteral("Autosave.ini"));
	if (!QFile::exists(path) || !QFile::exists(autosaveFile(folder, 1)))
		return info;
	QSettings note(path, QSettings::IniFormat);
	info.pending = note.value(QStringLiteral("pending"), false).toBool();
	info.file = autosaveFile(folder, 1);
	info.map = note.value(QStringLiteral("map")).toString();
	info.time = note.value(QStringLiteral("time")).toDateTime();
	info.game = note.value(QStringLiteral("game")).toString() == QStringLiteral("generals") ? Game::Generals : Game::ZeroHour;
	return info;
}

bool MainWindow::recoverAutosave()
{
	const AutosaveInfo info = autosaveInfo();
	if (info.file.isEmpty() || !confirmUnsavedChanges())
		return false;
	auto open = std::make_unique<OpenMap>();
	open->loaded = MapModel::loadMap(toFsPath(info.file));
	if (!open->loaded.result.ok())
	{
		toast(T("The autosave cannot be read: %1").arg(QString::fromStdString(open->loaded.result.error)));
		return false;
	}
	// Without a path Save asks where to put it; it counts as unsaved work.
	open->history.markUnsaved();
	m_state.game = info.game;
	m_state.map = std::move(open);
	++m_mapSerial;
	mapReplaced();
	m_page = Page::Map;
	applySettings();
	toast(info.map.isEmpty() ? T("Recovered the autosave of an unnamed map; save it under a name.")
		: T("Recovered the autosave of %1; save it under a name.").arg(QFileInfo(info.map).fileName()));
	return true;
}

QStringList MainWindow::gameLaunch(QString *program, QString *mapFile, QString *error)
{
	*program = m_state.gameProgram(m_state.game);
	if (program->isEmpty())
	{
		*error = T("No game program is set for %1; choose it in the settings.").arg(gameName(m_state.game));
		return {};
	}
	const QString path = QDir::toNativeSeparators(m_state.map->path);
	// Changed particle systems need the copy: they go into its map.ini.
	std::optional<std::string> file = EngineClient::instance().changedParticlesIni().isEmpty()
		? MapEditorCore::gameFileArgument(path.toStdString()) : std::nullopt;
	*mapFile = m_state.map->path;
	if (!file)
	{
		// A copy in a folder of its name in the user map folder, where the game looks for maps.
		*mapFile = writeTestCopy(QDir(m_state.userMapFolder(m_state.game)).filePath(QString::fromLatin1(TEST_MAP)), error);
		if (mapFile->isEmpty())
			return {};
		file = MapEditorCore::gameFileArgument(QDir::toNativeSeparators(*mapFile).toStdString());
	}
	QStringList args;
	for (const std::string &a : MapEditorCore::gameArguments(file.value_or(std::string())))
		args << QString::fromStdString(a);
	return args;
}

QString MainWindow::writeTestCopy(const QString &folder, QString *error)
{
	QDir().mkpath(folder);
	const QString mapFile = QDir(folder).filePath(QDir(folder).dirName() + QStringLiteral(".map"));
	std::string saveError;
	if (!MapModel::saveMap(toFsPath(mapFile), m_state.map->map(), saveError))
	{
		*error = T("The test copy cannot be written: %1").arg(QString::fromStdString(saveError));
		return {};
	}
	writePreview(mapFile);
	// The INI files of the map; a copy left from an earlier test must not stay.
	const QDir source = QFileInfo(m_state.map->path).dir();
	const QString particles = EngineClient::instance().changedParticlesIni();
	for (const QString &name : {QStringLiteral("map.ini"), QStringLiteral("solo.ini")})
	{
		QByteArray text;
		QFile own(source.filePath(name));
		if (!m_state.map->path.isEmpty() && own.open(QIODevice::ReadOnly))
			text = own.readAll();
		if (name == QLatin1String("map.ini") && !particles.isEmpty())
		{
			if (!text.isEmpty() && !text.endsWith('\n'))
				text += "\r\n";
			text += QStringLiteral("\r\n; Particle systems changed in the editor\r\n").toLatin1();
			text += QString(particles).replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\n'), QStringLiteral("\r\n")).toLatin1();
		}
		const QString target = QDir(folder).filePath(name);
		QFile::remove(target);
		if (text.isEmpty())
			continue;
		QFile out(target);
		if (!out.open(QIODevice::WriteOnly) || out.write(text) != text.size())
		{
			*error = T("The test copy cannot be written: %1").arg(target);
			return {};
		}
	}
	return mapFile;
}

void MainWindow::testInGame()
{
	if (!m_state.map)
		return;
	if (m_state.map->path.isEmpty() || m_state.map->history.isModified())
		save(false);
	if (m_state.map->path.isEmpty() || m_state.map->history.isModified())
		return;
	QString program;
	QString mapFile;
	QString error;
	const QStringList args = gameLaunch(&program, &mapFile, &error);
	if (args.isEmpty())
	{
		toast(error);
		if (m_state.gameProgram(m_state.game).isEmpty())
			showPage(Page::Settings);
		return;
	}
	// The game runs from its own folder, where its data lies.
	if (!QProcess::startDetached(program, args, QFileInfo(program).dir().path()))
	{
		toast(T("The game cannot be started: %1").arg(program));
		return;
	}
	if (EngineClient::instance().changedParticlesIni().isEmpty())
		toast(T("The game starts with %1").arg(QFileInfo(mapFile).fileName()));
	else
		toast(T("The game starts with %1 and the particle systems changed in the editor.").arg(QFileInfo(mapFile).fileName()));
}

} // namespace MapEditorApp
