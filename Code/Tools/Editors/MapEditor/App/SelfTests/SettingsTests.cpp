/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SettingsTests.cpp -- the self test of MainWindowSettings.cpp (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/SunDial.h"
#include "App/MapOverview.h"
#include "App/ToolEvent.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/MapSettingsCommands.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QImage>
#include <QLineEdit>
#include <QSpinBox>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>

namespace MapEditorApp
{

bool MainWindow::selfTestMapSettings(QStringList &report)
{
	TestSteps step(report, "settings");
	if (!m_state.map || !m_state.map->map().heightMap)
		return true;
	MapModel::Map &map = m_state.map->map();
	MapEditorCore::History &history = m_state.map->history;
	const MapEditorCore::MapSettings original = MapEditorCore::mapSettings(map);

	showMapSettings();
	QApplication::processEvents();
	auto *name = m_settingsWindow ? m_settingsWindow->findChild<QLineEdit *>(QStringLiteral("SettingsName")) : nullptr;
	step(name != nullptr, QStringLiteral("the map settings window is open"));
	if (!name)
		return step.ok();
	name->setText(QStringLiteral("Selftest Name"));
	emit name->editingFinished();
	step(map.worldInfo && map.worldInfo->getAscii("mapName") == "Selftest Name" && history.undoName() == "Map name",
		QStringLiteral("the name is set"));
	auto *weather = m_settingsWindow->findChild<QComboBox *>(QStringLiteral("SettingsWeather"));
	if (weather)
		weather->setCurrentIndex(weather->currentIndex() == 0 ? 1 : 0);
	step(weather && map.worldInfo->getInt("weather") == weather->currentIndex(), QStringLiteral("the weather is set"));

	if (map.lighting)
	{
		EngineClient &engine = EngineClient::instance();
		const qsizetype loads = engine.report().count(QStringLiteral("map: terrain built"));
		const int time = map.lighting->timeOfDay;
		auto *timeBox = m_settingsWindow->findChild<QComboBox *>(QStringLiteral("SettingsTime"));
		if (timeBox)
			timeBox->setCurrentIndex(time == 4 ? 0 : 3);
		QApplication::processEvents();
		step(map.lighting->timeOfDay != time && history.undoName() == "Time of day", QStringLiteral("the time of day is set"));
		// The sun, for terrain and objects: one step for all turns of the field.
		auto *azimuth = m_settingsWindow->findChild<QSpinBox *>(QStringLiteral("LightAzimuth0"));
		const std::size_t t = static_cast<std::size_t>(map.lighting->timeOfDay - 1);
		const std::array<float, 3> sunBefore = map.lighting->times[t].terrain[0].position;
		if (azimuth)
		{
			azimuth->setValue(azimuth->value() > 100 ? azimuth->value() - 60 : azimuth->value() + 60);
			azimuth->setValue(azimuth->value() + 5);
		}
		step(azimuth && map.lighting->times[t].terrain[0].position != sunBefore
				&& map.lighting->times[t].terrain[0].position == map.lighting->times[t].objects[0].position && history.undoName() == "Change light",
			QStringLiteral("turning the sun changes terrain and objects"));
		// The view loads the map again after the pause.
		for (int i = 0; i < 40 && engine.report().count(QStringLiteral("map: terrain built")) == loads; ++i)
		{
			QApplication::processEvents(QEventLoop::AllEvents, 50);
			QThread::msleep(20);
		}
		step(!m_viewport->hasDevice() || engine.report().count(QStringLiteral("map: terrain built")) > loads,
			QStringLiteral("the view loads the map with the new light"));
		undo();
		step(map.lighting->times[t].terrain[0].position == sunBefore, QStringLiteral("one undo takes the turns back"));
	}

	// Playable areas: a drag makes one, a corner drag resizes it, Del removes it.
	m_workMode = WorkMode::Objects;
	rebuild();
	QApplication::processEvents();
	setPlacingTemplate(QString::fromLatin1(BORDER_TOOL), 0.0f);
	const std::size_t areasBefore = map.heightMap->boundaries.size();
	const auto sendTool = [this](ToolEvent::Kind kind, float x, float y) {
		ToolEvent e;
		e.kind = kind;
		e.x = x;
		e.y = y;
		e.tolerance = 8.0f;
		toolEvent(e);
	};
	sendTool(ToolEvent::Kind::Press, 50.0f, 50.0f);
	sendTool(ToolEvent::Kind::Move, 300.0f, 200.0f);
	sendTool(ToolEvent::Kind::Release, 300.0f, 200.0f);
	const bool added = map.heightMap->boundaries.size() == areasBefore + 1 && map.heightMap->boundaries.back().x == 30
		&& map.heightMap->boundaries.back().y == 20 && history.undoName() == "Change playable area";
	step(added, QStringLiteral("a drag makes a playable area of 30 x 20 cells"));
	sendTool(ToolEvent::Kind::Press, 300.0f, 200.0f);
	sendTool(ToolEvent::Kind::Move, 400.0f, 260.0f);
	sendTool(ToolEvent::Kind::Release, 400.0f, 260.0f);
	step(added && map.heightMap->boundaries.back().x == 40 && map.heightMap->boundaries.back().y == 26, QStringLiteral("dragging its corner resizes it"));
	sendTool(ToolEvent::Kind::Press, 200.0f, 262.0f);
	sendTool(ToolEvent::Kind::Move, 200.0f, 300.0f);
	sendTool(ToolEvent::Kind::Release, 200.0f, 300.0f);
	step(added && map.heightMap->boundaries.back().x == 40 && map.heightMap->boundaries.back().y == 30, QStringLiteral("dragging its top edge changes only the height"));
	deleteSelection();
	step(map.heightMap->boundaries.size() == areasBefore && history.undoName() == "Delete playable area", QStringLiteral("Del removes it"));
	while (history.isModified() && history.canUndo())
		undo();
	step(MapEditorCore::mapSettings(map) == original && !history.isModified(), QStringLiteral("undo all, settings as before"));
	setPlacingTemplate(QString(), 0.0f);
	if (m_settingsWindow)
		m_settingsWindow->close();
	m_workMode = WorkMode::View;
	rebuild();
	QApplication::processEvents();
	return step.ok();
}

bool MainWindow::selfTestLighting(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "lighting");
	if (!m_state.map || !m_state.map->map().lighting || !m_viewport || !m_viewport->hasDevice())
		return true;
	EngineClient &engine = EngineClient::instance();
	const DisplayOptions savedDisplay = m_state.display;
	m_state.display.graphicsLevel = QStringLiteral("High");
	m_state.display.effectsOff.clear();
	applyDisplay();
	const auto shoot = [&](const QString &name) {
		for (int i = 0; i < 3; ++i)
			engine.render();
		const QString file = QDir(shotDir).filePath(name);
		m_viewport->captureFrame(file);
		return QImage(file).convertToFormat(QImage::Format_RGB32);
	};
	const auto differing = [](const QImage &a, const QImage &b) {
		if (a.isNull() || a.size() != b.size())
			return -1.0;
		qint64 count = 0;
		for (int y = 0; y < a.height(); ++y)
		{
			const QRgb *ra = reinterpret_cast<const QRgb *>(a.constScanLine(y));
			const QRgb *rb = reinterpret_cast<const QRgb *>(b.constScanLine(y));
			for (int x = 0; x < a.width(); ++x)
				count += (std::abs(qRed(ra[x]) - qRed(rb[x])) + std::abs(qGreen(ra[x]) - qGreen(rb[x])) + std::abs(qBlue(ra[x]) - qBlue(rb[x]))) > 30 ? 1 : 0;
		}
		return static_cast<double>(count) / (static_cast<double>(a.width()) * a.height());
	};
	// The animations stand still, so undo must bring back the very same picture.
	engine.freezeTime(true);
	const QImage before = shoot(QStringLiteral("lighting_before.bmp"));

	// The sun dial of the settings window: dragging previews, letting go makes one step.
	showMapSettings();
	auto *dial = m_settingsWindow ? child<SunDial>(m_settingsWindow, "SunDial") : nullptr;
	step(dial != nullptr, QStringLiteral("the lighting tab has the sun dial"));
	const std::size_t steps = m_state.map->history.undoCount();
	if (dial)
	{
		const float azimuth = dial->azimuth + 120.0f > 180.0f ? dial->azimuth - 240.0f : dial->azimuth + 120.0f;
		const float elevation = 25.0f;
		dial->onMove(azimuth, elevation);
		const bool unchangedWhileDragging = m_state.map->history.undoCount() == steps;
		dial->onDone(azimuth, elevation);
		float a = 0.0f;
		float e = 0.0f;
		const MapModel::TimeOfDayLighting &t = m_state.map->map().lighting->times[static_cast<std::size_t>(m_state.map->map().lighting->timeOfDay - 1)];
		MapEditorCore::lightAngles(t.terrain[0].position, a, e);
		step(unchangedWhileDragging && m_state.map->history.undoCount() == steps + 1 && m_state.map->history.undoName() == "Move the sun"
				&& std::abs(a - azimuth) < 1.0f && std::abs(e - elevation) < 1.0f,
			QStringLiteral("dragging the sun previews, letting go is one undo step (azimuth %1, elevation %2)").arg(a, 0, 'f', 0).arg(e, 0, 'f', 0));
	}
	const bool reloadPending = m_reloadTimer && m_reloadTimer->isActive();
	const QImage moved = shoot(QStringLiteral("lighting_sun_moved.bmp"));
	const double changed = differing(before, moved);
	step(!reloadPending && changed > 0.01,
		QStringLiteral("the view shows the new sun at once, without loading the map (%1 % of the pixels changed)").arg(changed * 100.0, 0, 'f', 1));
	if (m_state.map->history.undoCount() > steps)
		undo();
	const QImage back = shoot(QStringLiteral("lighting_undone.bmp"));
	const double left = differing(before, back);
	step(left >= 0.0 && left < 0.002, QStringLiteral("undo brings the old light back (%1 % of the pixels differ)").arg(left * 100.0, 0, 'f', 2));
	engine.freezeTime(false);
	if (m_settingsWindow)
		m_settingsWindow->close();
	m_state.display = savedDisplay;
	applyDisplay();
	return step.ok();
}

} // namespace MapEditorApp
