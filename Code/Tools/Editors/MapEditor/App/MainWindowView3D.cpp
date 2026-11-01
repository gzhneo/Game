/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowView3D.cpp -- the 3D view: the engine's map, the overlay of the editor and pictures of the view.

#include "App/MainWindow.h"
#include "App/Windows/ParticlesWindow.h"
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
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
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

// 3D view

QString MainWindow::engineLog() const
{
	if (!m_engineLog.isEmpty())
		return m_engineLog;
	const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
	QDir().mkpath(dir);
	return QDir(dir).filePath(QStringLiteral("engine.log"));
}

bool MainWindow::prepare3D(QString *error, bool *chooseData, bool *restart)
{
	if (!ensureEngine(error, chooseData, restart))
		return false;
	EngineClient &engine = EngineClient::instance();
	if (!m_viewport->ensureDevice(error))
		return false;
	// The engine keeps the options for the map it loads next.
	engine.setDisplay(engineDisplay());
	if (m_viewMapSerial != m_mapSerial)
	{
		const MapModel::WriteResult bytes = engineMapBytes();
		if (!bytes.ok())
		{
			*error = T("The map cannot be prepared for the 3D view: %1").arg(utf8(bytes.error));
			return false;
		}
		MeMapInfo info{};
		QApplication::setOverrideCursor(Qt::WaitCursor);
		const bool ok = engine.loadMap(bytes.bytes, &info, error);
		QApplication::restoreOverrideCursor();
		if (!ok)
			return false;
		m_viewport->setMapInfo(info);
		m_viewMapSerial = m_mapSerial;
		pushMapWaterLook();
		updateImpassableView();
		applyOverlay();
	}
	return true;
}

bool MainWindow::ensureEngine(QString *error, bool *chooseData, bool *restart)
{
	EngineClient &engine = EngineClient::instance();
	const Game game = m_state.game;
	if (engine.isStarted() && engine.startedGame() != game)
	{
		*error = T("The 3D view is running for %1. Switching the game needs a restart of the editor.").arg(gameName(engine.startedGame()));
		*restart = true;
		return false;
	}
	if (!engine.isStarted())
	{
		const QString dataFolder = m_state.dataFolder(game);
		if (dataFolder.isEmpty())
		{
			*error = T("The 3D view needs the game data of %1: the folder with its .big archives, or the asset copy of the build.").arg(gameName(game));
			*chooseData = true;
			return false;
		}
		QApplication::setOverrideCursor(Qt::WaitCursor);
		const bool ok = engine.start(game, dataFolder, engineLog(), error);
		QApplication::restoreOverrideCursor();
		if (!ok)
		{
			*chooseData = engine.isLoaded();
			*restart = engine.isLoaded();
			return false;
		}
	}
	return true;
}

MapModel::WriteResult MainWindow::engineMapBytes() const
{
	// The engine gets a copy in which every object carries its index in the model, so picks and
	// moves find the model object whatever the engine skips while reading.
	MapModel::Map copy = m_state.map->loaded.result.map;
	if (copy.objects)
	{
		for (std::size_t i = 0; i < copy.objects->size(); ++i)
			(*copy.objects)[i].properties.set("editorIndex", static_cast<std::int32_t>(i));
		// Objects of hidden layers stay out of the view; roads belong to the terrain and stay.
		if (!m_hiddenLayers.empty())
		{
			std::erase_if(*copy.objects, [this](const MapModel::MapObject &o) {
				return (o.flags & 0x36) == 0 && m_hiddenLayers.count(o.properties.getAscii("objectLayer").value_or("")) != 0;
			});
		}
	}
	appendBuildListObjects(copy);
	return MapModel::writeMap(copy);
}

void MainWindow::applyOverlay()
{
	if (!m_state.map)
		return;
	if (m_overview)
		m_overview->update();
	if (!m_viewport->hasDevice())
		return;
	std::vector<SelectionMark> marks;
	for (std::int32_t object : m_state.map->selection.objects)
	{
		const auto radius = m_selectionRadii.find(object);
		marks.push_back(SelectionMark{object, radius != m_selectionRadii.end() ? radius->second : 8.0f});
	}
	AreaMarks areaMarks;
	if (areaTool())
	{
		areaMarks.area = m_area;
		areaMarks.point = m_areaPoint;
		areaMarks.draft = m_areaDraft ? &*m_areaDraft : nullptr;
	}
	areaMarks.lines = toolLines();
	areaMarks.circle = brushCircle();
	m_overlay.sounds = m_state.display.sounds;
	OverlayShapes overlay = buildOverlay(m_state.map->map(), m_overlay, marks, areaMarks,
		[this](const MapModel::MapObject &o, float &minRange, float &maxRange) { return objectSoundRange(o, minRange, maxRange); },
		[this](const std::string &name) { return templateInfo(name); });
	if (m_particlesWindow)
		m_particlesWindow->appendParticleOutline(overlay.points, overlay.shapes);
	EngineClient::instance().setOverlay(overlay.points, overlay.shapes);
	m_viewport->update();
}

bool MainWindow::confirmUnsavedChanges()
{
	if (!m_state.map || !m_state.map->history.isModified())
		return true;
	QMessageBox box(this);
	box.setIcon(QMessageBox::Question);
	box.setWindowTitle(QStringLiteral("MapEditor"));
	box.setText(T("The map has unsaved changes."));
	box.setInformativeText(T("Save them before continuing?"));
	QPushButton *saveButton = box.addButton(T("Save"), QMessageBox::AcceptRole);
	QPushButton *discard = box.addButton(T("Discard"), QMessageBox::DestructiveRole);
	box.addButton(T("Cancel"), QMessageBox::RejectRole);
	box.setDefaultButton(saveButton);
	box.exec();
	if (box.clickedButton() == discard)
		return true;
	if (box.clickedButton() != saveButton)
		return false;
	save(false);
	return !m_state.map->history.isModified();
}

QWidget *MainWindow::buildViewArea(const Tokens &t)
{
	QString error;
	bool chooseData = false;
	bool restart = false;
	if (prepare3D(&error, &chooseData, &restart))
	{
		m_last3DError.clear();
		auto *slot = new QWidget;
		slot->setAttribute(Qt::WA_StyledBackground);
		slot->setStyleSheet(QStringLiteral("background: #1f2126;"));
		m_viewport->trackSlot(slot);
		return slot;
	}
	m_last3DError = error;
	auto *panel = new QWidget;
	auto *outer = new QVBoxLayout(panel);
	outer->addStretch(1);
	auto *box = new QWidget;
	box->setMaximumWidth(560);
	auto *v = new QVBoxLayout(box);
	v->setSpacing(10);
	v->addWidget(label(T("3D view not available"), "Heading"));
	auto *text = label(error, "Secondary");
	text->setWordWrap(true);
	text->setTextInteractionFlags(Qt::TextSelectableByMouse);
	v->addWidget(text);
	auto *buttons = new QHBoxLayout;
	if (chooseData)
	{
		auto *choose = new QPushButton(T("Choose game data folder\u2026"));
		choose->setObjectName(QStringLiteral("Primary"));
		choose->setCursor(Qt::PointingHandCursor);
		const Game game = m_state.game;
		connect(choose, &QPushButton::clicked, this, [this, game] { chooseDataFolder(game); });
		buttons->addWidget(choose);
	}
	if (restart)
	{
		auto *again = new QPushButton(T("Restart editor"));
		again->setCursor(Qt::PointingHandCursor);
		connect(again, &QPushButton::clicked, this, [this] { restartEditor(); });
		buttons->addWidget(again);
	}
	auto *back = new QPushButton(T("Back to the 2D overview"));
	back->setObjectName(QStringLiteral("Ghost"));
	back->setCursor(Qt::PointingHandCursor);
	connect(back, &QPushButton::clicked, this, [this] {
		m_show3D = false;
		QTimer::singleShot(0, this, [this] { rebuild(); });
	});
	buttons->addWidget(back);
	buttons->addStretch(1);
	v->addLayout(buttons);
	auto *centreRow = new QHBoxLayout;
	centreRow->addStretch(1);
	centreRow->addWidget(box);
	centreRow->addStretch(1);
	outer->addLayout(centreRow);
	outer->addStretch(2);
	(void)t;
	return panel;
}

void MainWindow::chooseDataFolder(Game game)
{
	const QString start = m_state.dataFolder(game).isEmpty() ? QDir::homePath() : m_state.dataFolder(game);
	const QString folder = QFileDialog::getExistingDirectory(this, T("Game data of %1").arg(gameName(game)), start);
	if (folder.isEmpty())
		return;
	if (!AppState::looksLikeGameData(folder))
	{
		toast(T("This folder holds neither .big archives nor a Data\\INI folder."));
		return;
	}
	m_state.setDataFolder(game, QDir::toNativeSeparators(folder));
	m_state.save();
	if (EngineClient::instance().isStarted())
		toast(T("The new folder is used after a restart of the editor."));
	QTimer::singleShot(0, this, [this] { rebuild(); });
}

void MainWindow::restartEditor()
{
	m_state.save();
	QStringList args;
	if (m_state.map && !m_state.map->path.isEmpty())
		args << m_state.map->path;
	if (QProcess::startDetached(QCoreApplication::applicationFilePath(), args))
		close();
	else
		toast(T("The editor could not be restarted."));
}

QRect MainWindow::viewportRect() const
{
	return m_viewport ? m_viewport->geometry() : QRect();
}

bool MainWindow::capture3D(const QString &bmpPath, QString *error, bool wholeMap)
{
	m_show3D = true;
	m_page = Page::Map;
	rebuild();
	for (int i = 0; i < 5; ++i)
		QApplication::processEvents();
	if (!m_viewport->hasDevice() || !m_last3DError.isEmpty())
	{
		if (error)
			*error = m_last3DError.isEmpty() ? T("3D view not available") : m_last3DError;
		return false;
	}
	if (wholeMap)
		m_viewport->showWholeMap();
	else
		m_viewport->resetCamera();
	return m_viewport->captureFrame(bmpPath);
}

} // namespace MapEditorApp
