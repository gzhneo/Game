/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// CameraTests.cpp -- the self test of MainWindowCamera.cpp (MapEditor --selftest).

#include "App/Common.h"
#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/ToolEvent.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/MapPreview.h"
#include "Core/WaypointCommands.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QSpinBox>
#include <cmath>
#include <functional>

namespace MapEditorApp
{

bool MainWindow::selfTestCamera(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "camera");
	if (!m_state.map || !m_state.map->map().heightMap)
		return true;
	MapModel::Map &map = m_state.map->map();

	// Preview picture: the pixels, and the .tga as the game reads it.
	const std::vector<std::uint32_t> pixels = previewPixels();
	QImage picture(MapEditorCore::PREVIEW_SIZE, MapEditorCore::PREVIEW_SIZE, QImage::Format_ARGB32);
	for (int y = 0; y < MapEditorCore::PREVIEW_SIZE; ++y)
		for (int x = 0; x < MapEditorCore::PREVIEW_SIZE; ++x)
			picture.setPixel(x, MapEditorCore::PREVIEW_SIZE - 1 - y, pixels[static_cast<std::size_t>(y * MapEditorCore::PREVIEW_SIZE + x)]);
	const QString previewShot = QDir(shotDir).filePath(QStringLiteral("map_preview.png"));
	step(picture.scaled(256, 256).save(previewShot), QStringLiteral("preview picture %1").arg(previewShot));
	const QString tgaPath = QDir(shotDir).filePath(QStringLiteral("preview_test.map"));
	const bool written = writePreview(tgaPath);
	QFile tga(QDir(shotDir).filePath(QStringLiteral("preview_test.tga")));
	step(written && tga.size() == 18 + 128 * 128 * 4, QStringLiteral("the preview is written as a 128 x 128 TGA next to the map"));
	tga.remove();

	// Ruler: a length and a circle.
	m_show3D = false;
	m_workMode = WorkMode::Objects;
	rebuild();
	QApplication::processEvents();
	setPlacingTemplate(QString::fromLatin1(RULER_TOOL), 0.0f);
	const auto sendTool = [this](ToolEvent::Kind kind, float x, float y) {
		ToolEvent e;
		e.kind = kind;
		e.x = x;
		e.y = y;
		toolEvent(e);
	};
	const std::size_t objects = map.objects ? map.objects->size() : 0;
	m_ruler.circle = false;
	sendTool(ToolEvent::Kind::Press, 100.0f, 100.0f);
	sendTool(ToolEvent::Kind::Move, 400.0f, 500.0f);
	sendTool(ToolEvent::Kind::Release, 400.0f, 500.0f);
	step(rulerText().contains(QStringLiteral("500.0")) && rulerLines().size() == 4 && (map.objects ? map.objects->size() : 0) == objects,
		QStringLiteral("the ruler measures %1").arg(rulerText()));
	m_ruler.circle = true;
	step(rulerLines().size() == 48 * 4 + 4 && rulerText().contains(QStringLiteral("1000.0")), QStringLiteral("as a circle: %1").arg(rulerText()));
	setPlacingTemplate(QString(), 0.0f);

	// Camera: turn, pitch, centre on a selection, a waypoint where it looks.
	m_show3D = true;
	rebuild();
	QApplication::processEvents();
	if (m_viewport && m_viewport->hasDevice())
	{
		EngineClient &engine = EngineClient::instance();
		const MeCamera start = engine.camera();
		setCameraYaw(90.0f);
		step(std::abs(engine.camera().yaw - PI / 2.0f) < 0.01f, QStringLiteral("the camera looks east"));
		showCameraWindow();
		QApplication::processEvents();
		auto *pitch = m_cameraWindow ? m_cameraWindow->findChild<QSpinBox *>(QStringLiteral("CameraPitch")) : nullptr;
		if (pitch)
			pitch->setValue(60);
		step(pitch && std::abs(engine.camera().pitch - 60.0f * PI / 180.0f) < 0.01f, QStringLiteral("the pitch field tilts the camera"));
		int object = -1;
		for (std::size_t i = 0; map.objects && i < map.objects->size() && object < 0; ++i)
			if (((*map.objects)[i].flags & 0x36) == 0)
				object = static_cast<int>(i);
		if (object >= 0)
		{
			m_state.map->selection.set(object);
			centerOnSelection();
			const MapModel::MapObject &o = (*map.objects)[static_cast<std::size_t>(object)];
			step(std::abs(engine.camera().targetX - o.x) < 1.0f && std::abs(engine.camera().targetY - o.y) < 1.0f,
				QStringLiteral("the camera centres on the selected object"));
			const std::size_t before = map.objects->size();
			waypointAtCamera();
			step(map.objects->size() == before + 1 && MapEditorCore::isWaypoint(map.objects->back())
					&& std::abs(map.objects->back().x - o.x) < 1.0f,
				QStringLiteral("a waypoint is set where the camera looks"));
			undo();
		}
		engine.setCamera(start);
		if (m_cameraWindow)
			m_cameraWindow->close();
	}
	else
		step(false, QStringLiteral("3D: no view"));
	m_state.map->selection.clear();
	m_workMode = WorkMode::View;
	rebuild();
	QApplication::processEvents();
	return step.ok();
}

} // namespace MapEditorApp
