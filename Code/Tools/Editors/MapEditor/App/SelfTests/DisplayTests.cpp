/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// DisplayTests.cpp -- the self test of MainWindowDisplay.cpp (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/GraphicsLevels.h"
#include "Core/Slope.h"
#include <QApplication>
#include <QDir>
#include <QImage>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

namespace MapEditorApp
{

bool MainWindow::selfTestSlopes(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "slopes");
	if (!m_state.map || !m_state.map->map().heightMap || !m_overview)
		return true;
	const DisplayOptions saved = m_state.display;
	// A limit that river banks and hillsides in the 3D view exceed, lower on flatter maps.
	const std::vector<float> slopes = MapEditorCore::slopeMap(*m_state.map->map().heightMap);
	const float steepest = slopes.empty() ? 0.0f : *std::max_element(slopes.begin(), slopes.end());
	const int limit = std::clamp(static_cast<int>(std::min(15.0f, steepest / 2.0f)), 1, 89);
	const MapModel::HeightMap &hm = *m_state.map->map().heightMap;
	int expected = 0;
	for (int y = 0; y + 1 < hm.height; ++y)
		for (int x = 0; x + 1 < hm.width; ++x)
			expected += slopes[static_cast<std::size_t>(y) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(x)] >= static_cast<float>(limit) ? 1 : 0;
	// The camera looks at the densest patch of steep cells, so the red shows in the picture.
	int bestX = 0, bestY = 0, bestCount = -1;
	for (int y = 0; y + 1 < hm.height; y += 2)
	{
		for (int x = 0; x + 1 < hm.width; x += 2)
		{
			int count = 0;
			for (int dy = -4; dy <= 4; ++dy)
				for (int dx = -4; dx <= 4; ++dx)
				{
					const int cx = x + dx, cy = y + dy;
					if (cx >= 0 && cy >= 0 && cx + 1 < hm.width && cy + 1 < hm.height)
						count += slopes[static_cast<std::size_t>(cy) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(cx)] >= static_cast<float>(limit) ? 1 : 0;
				}
			if (count > bestCount)
			{
				bestCount = count;
				bestX = x;
				bestY = y;
			}
		}
	}
	EngineClient &engine = EngineClient::instance();
	const bool view = m_viewport && m_viewport->hasDevice();
	const MeCamera camera = view ? engine.camera() : MeCamera{};
	if (view)
		centerOn((static_cast<float>(bestX - hm.borderSize) + 0.5f) * 10.0f, (static_cast<float>(bestY - hm.borderSize) + 0.5f) * 10.0f);
	const auto shoot = [&](const QString &name) {
		for (int i = 0; i < 2; ++i)
			engine.render();
		const QString file = QDir(shotDir).filePath(name);
		m_viewport->captureFrame(file);
		return QImage(file).convertToFormat(QImage::Format_RGB32);
	};
	engine.freezeTime(true);
	const QImage off = view ? shoot(QStringLiteral("slopes_off.bmp")) : QImage();
	m_state.display.slopes = true;
	m_state.display.slopeLimit = limit;
	applyDisplay();
	step(expected > 0 && m_overview->steepCellCount() == expected,
		QStringLiteral("the 2D slope map marks the %1 cells steeper than %2 degrees (steepest %3)").arg(m_overview->steepCellCount()).arg(limit).arg(steepest, 0, 'f', 1));
	if (view)
	{
		const QImage on = shoot(QStringLiteral("slopes_on.bmp"));
		qint64 redder = 0;
		for (int y = 0; y < on.height() && on.size() == off.size(); ++y)
		{
			const QRgb *a = reinterpret_cast<const QRgb *>(off.constScanLine(y));
			const QRgb *b = reinterpret_cast<const QRgb *>(on.constScanLine(y));
			for (int x = 0; x < on.width(); ++x)
				redder += (qRed(b[x]) - qGreen(b[x])) - (qRed(a[x]) - qGreen(a[x])) > 40 ? 1 : 0;
		}
		step(redder > 0, QStringLiteral("the 3D view marks steep cells red (%1 pixels, view %2), picture slopes_on.bmp")
			.arg(redder).arg(m_viewMapSerial == m_mapSerial ? QStringLiteral("current") : QStringLiteral("outdated")));
	}
	m_overview->grab().save(QDir(shotDir).filePath(QStringLiteral("slopes_2d.png")));
	engine.freezeTime(false);
	if (view)
		engine.setCamera(camera);
	m_state.display = saved;
	applyDisplay();
	return step.ok();
}

bool MainWindow::selfTestDisplay(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "display");
	if (!m_state.map || !m_state.map->map().heightMap)
		return true;
	const DisplayOptions saved = m_state.display;
	const bool was3D = m_show3D;

	// Contour lines in the 2D overview; a smaller step gives more lines.
	m_show3D = false;
	rebuild();
	QApplication::processEvents();
	m_state.display.contours = true;
	m_state.display.contourStep = 16;
	m_state.display.contourOffset = 0;
	applyDisplay();
	const int coarse = m_overview ? m_overview->contourLineCount() : 0;
	m_state.display.contourStep = 4;
	applyDisplay();
	const int fine = m_overview ? m_overview->contourLineCount() : 0;
	step(coarse > 0 && fine > coarse, QStringLiteral("contour lines: %1 segments every 16 steps, %2 every 4").arg(coarse).arg(fine));
	if (m_overview)
	{
		const QString shot = QDir(shotDir).filePath(QStringLiteral("contours_2d.png"));
		step(m_overview->grab().save(shot), QStringLiteral("picture %1").arg(shot));
	}
	m_state.display.contours = false;
	applyDisplay();
	step(!m_overview || m_overview->contourLineCount() == 0, QStringLiteral("contour lines off"));

	// Ambient sounds: the range of the sound, or the object's own.
	EngineClient &engine = EngineClient::instance();
	if (engine.isStarted() && !engine.sounds().empty())
	{
		const MeSoundInfo &sound = engine.sounds().front();
		MapModel::MapObject o;
		o.templateName = "SelfTestSound";
		o.properties.set("objectSoundAmbient", MapModel::PropertyValue(std::string(sound.name)));
		float minRange = -1.0f;
		float maxRange = -1.0f;
		const bool byDefault = objectSoundRange(o, minRange, maxRange) && minRange == sound.minRange && maxRange == sound.maxRange;
		o.properties.set("objectSoundAmbientCustomized", MapModel::PropertyValue(true));
		o.properties.set("objectSoundAmbientMinRange", MapModel::PropertyValue(50.0f));
		o.properties.set("objectSoundAmbientMaxRange", MapModel::PropertyValue(200.0f));
		const bool custom = objectSoundRange(o, minRange, maxRange) && minRange == 50.0f && maxRange == 200.0f;
		MapModel::Map one;
		one.objects = std::vector<MapModel::MapObject>{o};
		OverlayOptions options;
		options.sounds = true;
		const OverlayShapes shapes = buildOverlay(one, options, {}, {},
			[this](const MapModel::MapObject &object, float &lo, float &hi) { return objectSoundRange(object, lo, hi); });
		MapModel::MapObject silent;
		silent.templateName = "SelfTestSound";
		silent.properties.set("objectSoundAmbient", MapModel::PropertyValue(std::string()));
		step(byDefault && custom && shapes.shapes.size() == 3 && !objectSoundRange(silent, minRange, maxRange),
			QStringLiteral("sound ranges of %1 (%2 to %3), customised 50 to 200, marker and two circles").arg(QString::fromUtf8(sound.name))
				.arg(static_cast<double>(sound.minRange)).arg(static_cast<double>(sound.maxRange)));
	}
	else
		report << QStringLiteral("info display: no sounds in the game data, sound ranges not tested");

	// The 3D view: models off and on again rebuild its objects; wireframe and the rest are drawn.
	m_show3D = true;
	rebuild();
	QApplication::processEvents();
	if (m_viewport && m_viewport->hasDevice() && m_viewMapSerial == m_mapSerial)
	{
		m_state.display.models = false;
		applyDisplay();
		const bool offKept = m_viewMapSerial == m_mapSerial;
		m_state.display.models = true;
		m_state.display.wireframe = true;
		m_state.display.extraBlends = true;
		applyDisplay();
		const QString shot = QDir(shotDir).filePath(QStringLiteral("display_wireframe.bmp"));
		m_viewport->captureFrame(shot);
		m_state.display.wireframe = false;
		m_state.display.extraBlends = false;
		m_state.display.sounds = true;
		applyDisplay();
		const QString sounds = QDir(shotDir).filePath(QStringLiteral("display_sounds.bmp"));
		m_viewport->captureFrame(sounds);
		step(offKept && m_viewMapSerial == m_mapSerial, QStringLiteral("models off and on, wireframe and three-way blends (%1), sounds (%2)").arg(shot, sounds));
		const QStringList textures = engine.macroTextures();
		report << QStringLiteral("info display: macro textures: %1").arg(textures.isEmpty() ? QStringLiteral("(none)") : textures.join(QStringLiteral(", ")));
	}
	else
		report << QStringLiteral("info display: no 3D view, engine options not tested");

	// Footprint, sight and weapon ranges from the template; an unknown type is marked.
	{
		MeTemplateInfo box{};
		box.geometry = 2;
		box.majorRadius = 20.0f;
		box.minorRadius = 10.0f;
		box.visionRange = 150.0f;
		box.weaponRange[0] = 100.0f;
		box.weaponRange[2] = 60.0f;
		MapModel::Map one;
		MapModel::MapObject o;
		o.templateName = "SelfTestBox";
		MapModel::MapObject unknown;
		unknown.templateName = "SelfTestUnknown";
		one.objects = std::vector<MapModel::MapObject>{o, unknown};
		OverlayOptions options;
		options.boundingBoxes = true;
		options.sightRanges = true;
		options.weaponRanges = true;
		options.testArt = true;
		const OverlayShapes shapes = buildOverlay(one, options, {}, {}, {},
			[&box](const std::string &name) { return name == "SelfTestBox" ? &box : nullptr; });
		// Box, sight, two weapon circles; circle and marker for the unknown type.
		const bool boxFirst = !shapes.shapes.empty() && shapes.shapes.front().kind == ME_OVERLAY_LOOP && shapes.shapes.front().pointCount == 4;
		step(shapes.shapes.size() == 6 && boxFirst, QStringLiteral("bounding box, sight and weapon ranges, unknown type marked (%1 shapes)").arg(shapes.shapes.size()));
	}
	// Snap to grid: whole cells while it is on.
	m_state.display.snapToGrid = true;
	const float snapped = snapCoord(13.0f);
	m_state.display.snapToGrid = false;
	step(snapped == 10.0f && snapCoord(13.0f) == 13.0f, QStringLiteral("snap to grid: 13 becomes 10, off stays 13"));
	// Labels and without object dots in the 2D overview; letterbox and garrisoned in 3D.
	m_show3D = false;
	rebuild();
	QApplication::processEvents();
	m_state.display.labels = true;
	m_state.display.objectIcons = false;
	applyDisplay();
	if (m_overview)
	{
		const QString shot = QDir(shotDir).filePath(QStringLiteral("labels_2d.png"));
		step(m_overview->grab().save(shot), QStringLiteral("labels: picture %1").arg(shot));
	}
	m_state.display.labels = false;
	m_state.display.objectIcons = true;
	m_show3D = true;
	rebuild();
	QApplication::processEvents();
	if (m_viewport && m_viewport->hasDevice() && m_viewMapSerial == m_mapSerial)
	{
		m_state.display.letterbox = true;
		m_state.display.garrisoned = true;
		m_state.display.boundingBoxes = true;
		applyDisplay();
		const QString shot = QDir(shotDir).filePath(QStringLiteral("display_letterbox.bmp"));
		m_viewport->captureFrame(shot);
		step(m_viewMapSerial == m_mapSerial, QStringLiteral("letterbox, garrisoned, bounding boxes: picture %1").arg(shot));
		m_state.display.letterbox = false;
		m_state.display.garrisoned = false;
		m_state.display.boundingBoxes = false;
		applyDisplay();
		reloadTextures();
		QApplication::processEvents();
		step(m_viewMapSerial == m_mapSerial, QStringLiteral("reload textures loads the map again"));
	}

	m_state.display = saved;
	applyDisplay();
	m_show3D = was3D;
	rebuild();
	QApplication::processEvents();
	return step.ok();
}

bool MainWindow::selfTestGraphics(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "graphics");
	QStringList names;
	for (const MapEditorCore::GraphicsLevel &l : graphicsLevels())
		names << QString::fromStdString(l.name);
	step(names == QStringList({QStringLiteral("Low"), QStringLiteral("Medium"), QStringLiteral("High"), QStringLiteral("VeryHigh")}),
		QStringLiteral("the game's detail levels from GameLODOverride.ini: %1").arg(names.join(QStringLiteral(", "))));
	if (!m_viewport || !m_viewport->hasDevice() || !m_state.map)
		return step.ok();
	EngineClient &engine = EngineClient::instance();
	const DisplayOptions saved = m_state.display;
	const auto shoot = [&](const QString &level, const QStringList &off, const QString &name) {
		m_state.display.graphicsLevel = level;
		m_state.display.effectsOff = off;
		applyDisplay();
		for (int i = 0; i < 3; ++i)
			engine.render();
		const QString file = QDir(shotDir).filePath(name);
		m_viewport->captureFrame(file);
		return QImage(file).convertToFormat(QImage::Format_RGB32);
	};
	// Share of pixels that differ clearly between two pictures of the same view.
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
	const auto blackShare = [](const QImage &image) {
		qint64 black = 0;
		for (int y = 0; y < image.height(); ++y)
		{
			const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
			for (int x = 0; x < image.width(); ++x)
				black += (qRed(row[x]) < 12 && qGreen(row[x]) < 12 && qBlue(row[x]) < 12) ? 1 : 0;
		}
		return image.isNull() ? 1.0 : static_cast<double>(black) / (static_cast<double>(image.width()) * image.height());
	};
	// The animations stand still, so two pictures of the same state are equal.
	engine.freezeTime(true);
	const QImage classic = shoot(QString(), {}, QStringLiteral("graphics_classic.bmp"));
	const QImage high = shoot(QStringLiteral("High"), {}, QStringLiteral("graphics_high.bmp"));
	const QImage highAgain = shoot(QStringLiteral("High"), {}, QStringLiteral("graphics_high_again.bmp"));
	const double noise = differing(high, highAgain);
	step(noise >= 0.0 && noise < 0.002, QStringLiteral("with the clock stopped two pictures of the same state are equal (%1 % differ)").arg(noise * 100.0, 0, 'f', 2));
	const QImage noShadows = shoot(QStringLiteral("High"), {QStringLiteral("shadowmap")}, QStringLiteral("graphics_high_noshadowmap.bmp"));
	const double changed = differing(classic, high);
	step(changed > 0.02 && blackShare(high) < 0.4,
		QStringLiteral("the level High changes the picture against the classic look (%1 % of the pixels), picture graphics_high.bmp").arg(changed * 100.0, 0, 'f', 1));
	const double shadows = differing(high, noShadows);
	step(shadows > noise + 0.005, QStringLiteral("switching off the sun shadows changes the picture (%1 % of the pixels)").arg(shadows * 100.0, 0, 'f', 2));
	engine.freezeTime(false);
	m_state.display = saved;
	applyDisplay();
	return step.ok();
}

} // namespace MapEditorApp
