/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowWater.cpp -- how a map's water looks. The game reads the WaterTransparency values from
// Water.ini and a map overrides them in the block of the same name in its map.ini; the realistic
// water of the detail levels Medium and up uses turbidity, refraction and reflection. The "Water" tab
// of the map settings changes them with a live preview in the 3D view and writes the values that
// differ from the game data into the map's map.ini.

#include "App/Common.h"
#include "App/MainWindow.h"

#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/WaterLook.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdlib>
#include <optional>

namespace MapEditorApp
{

namespace
{

QString mapIniPath(const QString &mapPath)
{
	return mapPath.isEmpty() ? QString() : QFileInfo(mapPath).dir().filePath(QStringLiteral("map.ini"));
}

} // namespace

std::optional<MapEditorCore::WaterLook> MainWindow::mapWaterLook()
{
	const std::optional<MapEditorCore::WaterLook> base = EngineClient::instance().waterLook(true);
	if (!base || !m_state.map)
		return base;
	return MapEditorCore::readWaterLook(readText(mapIniPath(m_state.map->path)), *base);
}

void MainWindow::pushMapWaterLook()
{
	m_waterLook = mapWaterLook();
	if (m_waterLook)
		EngineClient::instance().setWaterLook(*m_waterLook);
}

bool MainWindow::writeMapWaterLook(QString *error)
{
	const std::optional<MapEditorCore::WaterLook> base = EngineClient::instance().waterLook(true);
	const QString path = m_state.map ? mapIniPath(m_state.map->path) : QString();
	if (path.isEmpty() || !base || !m_waterLook)
	{
		*error = T("Save the map first: the water values go into the map.ini beside it.");
		return false;
	}
	const std::string text = MapEditorCore::writeWaterLook(readText(path), *m_waterLook, *base);
	// A map.ini with nothing left is removed, as the map.ini editor does it.
	if (text.find_first_not_of(" \t\r\n") == std::string::npos)
	{
		QFile::remove(path);
		return true;
	}
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(text.data(), static_cast<qint64>(text.size())) != static_cast<qint64>(text.size()))
	{
		*error = T("The file cannot be written: %1").arg(path);
		return false;
	}
	return true;
}

std::optional<MapEditorCore::WaterLook> &MainWindow::waterLook()
{
	if (!m_waterLook)
		m_waterLook = mapWaterLook();
	return m_waterLook;
}

void MainWindow::showWaterLook()
{
	if (m_waterLook && EngineClient::instance().setWaterLook(*m_waterLook) && m_viewport)
		m_viewport->update();
}

void MainWindow::resetWaterLook()
{
	m_waterLook = EngineClient::instance().waterLook(true);
	if (m_waterLook)
		EngineClient::instance().setWaterLook(*m_waterLook);
	if (m_viewport)
		m_viewport->update();
}

} // namespace MapEditorApp
