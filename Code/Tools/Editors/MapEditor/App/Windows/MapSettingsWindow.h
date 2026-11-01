/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapSettingsWindow.h -- the map's settings: world (weather, compression), lighting with the sun dial,
// shadows and time of day, the water look, and the playable areas. Changes go to the map as steps of
// the history; the lighting and the water show in the 3D view at once.

#pragma once

#include "Core/WaterLook.h"
#include "Model/Map.h"

#include <QDialog>
#include <QString>

#include <functional>
#include <optional>

namespace MapEditorApp
{

class EditorContext;

// What the settings window asks of the main window beyond the EditorContext.
class MapSettingsContext
{
public:
	virtual ~MapSettingsContext() = default;

	// Changes the settings as one step (same-named steps in a row fold together).
	virtual void editSettings(const char *name, const std::function<void(MapModel::Map &)> &change) = 0;
	// A change of lighting, time of day or shadow colour: goes to the view at once, no reload.
	virtual void editLighting(const char *name, const std::function<void(MapModel::Map &)> &change) = 0;
	// Shows the lighting in the view without changing the map (while the sun is dragged).
	virtual void previewLighting(const MapModel::GlobalLighting &lighting) = 0;
	// The water look being edited (the game data with the map's map.ini over them); empty without
	// the game data.
	[[nodiscard]] virtual std::optional<MapEditorCore::WaterLook> &waterLook() = 0;
	virtual void showWaterLook() = 0;
	// Back to the game data's values.
	virtual void resetWaterLook() = 0;
	virtual bool writeMapWaterLook(QString *error) = 0;
};

class MapSettingsWindow : public QDialog
{
	Q_OBJECT

public:
	MapSettingsWindow(EditorContext &editor, MapSettingsContext &settings, QWidget *parent);

	// Builds the window anew from the map.
	void refresh();

private:
	QWidget *waterTab();

	EditorContext &m_editor;
	MapSettingsContext &m_settings;
	int m_lightTarget = 2;		// lights edited: 0 terrain, 1 objects, 2 both
};

} // namespace MapEditorApp
