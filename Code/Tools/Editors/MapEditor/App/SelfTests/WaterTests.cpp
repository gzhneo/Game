/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WaterTests.cpp -- the self test of MainWindowWater.cpp (MapEditor --selftest).

#include "App/Common.h"
#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/WaterLook.h"
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QImage>
#include <cstdlib>
#include <optional>

namespace MapEditorApp
{

bool MainWindow::selfTestWater(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "water");
	EngineClient &engine = EngineClient::instance();
	if (!m_state.map || !m_viewport || !m_viewport->hasDevice() || !engine.waterLook(true))
		return true;
	const QString savedPath = m_state.map->path;
	const DisplayOptions savedDisplay = m_state.display;
	// A map in a folder of its own, so its map.ini is the test's.
	const QString folder = QDir(shotDir).filePath(QStringLiteral("WaterTest"));
	QDir(folder).removeRecursively();
	QDir().mkpath(folder);
	m_state.map->path = QDir(folder).filePath(QStringLiteral("WaterTest.map"));
	QFile other(QDir(folder).filePath(QStringLiteral("map.ini")));
	if (other.open(QIODevice::WriteOnly))
		other.write("Weapon Keep\r\n  Damage = 1\r\nEnd\r\n");
	other.close();
	pushMapWaterLook();
	const MapEditorCore::WaterLook base = *engine.waterLook(true);
	step(m_waterLook && *m_waterLook == base, QStringLiteral("a map.ini without water keeps the game data (visibility depth %1)").arg(base.turbidityDepth, 0, 'f', 1));

	m_state.display.graphicsLevel = QStringLiteral("High");
	m_state.display.effectsOff.clear();
	applyDisplay();
	// The test compares pictures of the water, so the view has to show some: look at the first water area of the map.
	const MeCamera savedCamera = engine.camera();
	if (m_state.map->map().polygons)
	{
		for (const MapModel::PolygonArea &area : *m_state.map->map().polygons)
		{
			if (!area.isWater || area.points.empty())
				continue;
			double x = 0, y = 0;
			for (const auto &point : area.points)
			{
				x += point[0];
				y += point[1];
			}
			MeCamera c = savedCamera;
			c.targetX = static_cast<float>(x / static_cast<double>(area.points.size()));
			c.targetY = static_cast<float>(y / static_cast<double>(area.points.size()));
			c.targetZ = static_cast<float>(area.points[0][2]);
			c.pitch = 0.9f;
			engine.setCamera(c);
			break;
		}
	}
	engine.freezeTime(true);
	const auto shoot = [&](const QString &name) {
		for (int i = 0; i < 3; ++i)
			engine.render();
		const QString file = QDir(shotDir).filePath(name);
		m_viewport->captureFrame(file);
		return QImage(file).convertToFormat(QImage::Format_RGB32);
	};
	const QImage murky = shoot(QStringLiteral("water_game_data.bmp"));
	// The water tab: a clear, dark-blue water, live in the view. Visibility depth and turbidity act on
	// standing water, the colour on rivers too.
	showMapSettings();
	auto *depth = m_settingsWindow ? child<QDoubleSpinBox>(m_settingsWindow, "WaterTurbidityDepth") : nullptr;
	if (depth)
		depth->setValue(200.0);
	if (m_waterLook)
	{
		m_waterLook->turbidityColor = {20, 40, 140};
		m_waterLook->standingColor = {90, 130, 255};
	}
	engine.setWaterLook(*m_waterLook);
	const QImage clear = shoot(QStringLiteral("water_clear.bmp"));
	qint64 differing = 0;
	for (int y = 0; y < clear.height() && clear.size() == murky.size(); ++y)
	{
		const QRgb *a = reinterpret_cast<const QRgb *>(murky.constScanLine(y));
		const QRgb *b = reinterpret_cast<const QRgb *>(clear.constScanLine(y));
		for (int x = 0; x < clear.width(); ++x)
			differing += (std::abs(qRed(a[x]) - qRed(b[x])) + std::abs(qGreen(a[x]) - qGreen(b[x])) + std::abs(qBlue(a[x]) - qBlue(b[x]))) > 30 ? 1 : 0;
	}
	const std::optional<MapEditorCore::WaterLook> current = engine.waterLook(false);
	step(depth && current && current->turbidityDepth == 200.0f && differing > 500,
		QStringLiteral("the water tab changes the water in the view at once (%1 pixels), picture water_clear.bmp").arg(differing));

	QString error;
	const bool written = writeMapWaterLook(&error);
	const std::string text = readText(QDir(folder).filePath(QStringLiteral("map.ini")));
	step(written && text.find("Weapon Keep") != std::string::npos && text.find("TurbidityDepth = 200.00") != std::string::npos
			&& text.find("RefractionStrength") == std::string::npos,
		QStringLiteral("the map.ini gets only the values that differ, its other blocks stay %1").arg(error));
	// Read back from the file, as when the map is opened again.
	engine.setWaterLook(base);
	pushMapWaterLook();
	step(m_waterLook && m_waterLook->turbidityDepth == 200.0f && m_waterLook->turbidityColor == (std::array<int, 3>{20, 40, 140}),
		QStringLiteral("the view takes the water of the map's map.ini"));

	engine.freezeTime(false);
	if (m_settingsWindow)
		m_settingsWindow->close();
	m_state.map->path = savedPath;
	pushMapWaterLook();
	engine.setCamera(savedCamera);
	m_state.display = savedDisplay;
	applyDisplay();
	QDir(folder).removeRecursively();
	return step.ok();
}

} // namespace MapEditorApp
