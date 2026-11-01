/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EngineClient.h -- the editor's side of MapEditorEngine.dll. Loads the DLL on first use, so the
// editor starts fast and works without it (2D overview only).

#pragma once

#include "App/Theme.h"
#include "Core/WaterLook.h"
#include "Engine/EngineApi.h"
#include "Model/Map.h"

#include <QColor>
#include <QImage>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace MapEditorApp
{

class EngineClient
{
public:
	static EngineClient &instance();

	// Loads the DLL next to the editor. False with a reason if it is missing or too old.
	bool load(QString *error);
	bool isLoaded() const { return m_loaded; }

	bool start(Game game, const QString &dataFolder, const QString &logFile, QString *error);
	bool isStarted() const { return m_started; }
	Game startedGame() const { return m_game; }
	QString report() const;
	void stop();

	bool attach(void *hwnd, int width, int height, QString *error);
	bool isAttached() const { return m_attached; }
	void detach();
	void resize(int width, int height);
	bool loadMap(const std::vector<std::uint8_t> &chunks, MeMapInfo *info, QString *error);
	void unloadMap();
	MeCamera camera() const;
	void setCamera(const MeCamera &camera);
	void resetCamera();
	bool pick(int x, int y, float world[3]);
	bool pickObject(int x, int y, MePickResult *result);
	void moveObject(int object, float x, float y, float z, float angle);
	bool objectInfo(int object, MePickResult *result);
	std::vector<int> objectsInRect(int x0, int y0, int x1, int y1);
	void setMarquee(int x0, int y0, int x1, int y1);
	bool reloadObjects(const std::vector<std::uint8_t> &chunks);
	void render();
	void capture(const QString &bmpPath);
	// points: x, y, z per point (see MeOverlayShape).
	// Height map cells x0..x1, y0..y1 from the model's heights (meViewSetHeights).
	void setHeights(int x0, int y0, int x1, int y1, const std::vector<std::uint8_t> &heights, int width);
	void setOverlay(const std::vector<float> &points, const std::vector<MeOverlayShape> &shapes);
	// Every object template of the started game; read once and kept until the engine stops.
	const std::vector<MeTemplateInfo> &catalog();
	// Lighting and water height of the game data, for a new map.
	bool mapDefaults(MeMapDefaults &defaults);
	// Factions and player colours of the started game; read once, like the catalog.
	const std::vector<MePlayerTemplateInfo> &factions();
	const std::vector<MeColorInfo> &colors();
	// Script conditions or actions of the started game, indexed by the number the map stores; read once.
	const std::vector<MeScriptTemplate> &scriptTemplates(bool actions);
	// Names the game data has for a script parameter type; read once per type.
	const std::vector<MeNamedValue> &scriptValues(int parameterType);
	// Road and bridge types of the started game; read once, like the catalog.
	const std::vector<MeRoadInfo> &roads();
	// Sound effects of the started game, sorted by name; read once.
	const std::vector<MeSoundInfo> &sounds();
	// Terrain textures of the started game, sorted by name; read once. A picture of one tile.
	const std::vector<MeTerrainInfo> &terrains();
	QImage terrainPreview(const QString &name, int size);
	// Tile indices of height map cells x0..x1, y0..y1 from the model's arrays (meViewSetTiles).
	void setTiles(int x0, int y0, int x1, int y1, const std::vector<std::int16_t> &tiles, const std::vector<std::int16_t> &blends,
		const std::vector<std::int16_t> &extraBlends, const std::vector<std::int16_t> &cliffs, int width);
	// The model's blend table into the view (meViewSetBlendTable), before setTiles uses new entries.
	void setBlendTable(const std::vector<MapModel::BlendedTile> &table);
	// Cliff flags of cells x0..x1, y0..y1 into the view.
	void setCliffFlags(int x0, int y0, int x1, int y1, const std::vector<std::uint8_t> &flags, int width);
	// One 0xAARRGGBB per height map cell over the terrain (slopes, passability); empty clears it.
	void setTerrainTint(const std::vector<std::uint32_t> &colors, int width, int height, int border);
	// Animations of the view stand still while frozen (image comparisons, posters).
	void freezeTime(bool freeze);
	// Draw only a part of the view, in fractions of it (posters); 0, 0, 1, 1 for all of it.
	void setTile(float left, float top, float right, float bottom);
	// How the water looks: the game data's values (before any change), or the current ones.
	std::optional<MapEditorCore::WaterLook> waterLook(bool gameData);
	bool setWaterLook(const MapEditorCore::WaterLook &look);
	// Mesh mold files of the started game, sorted; read once. The top surface of one as a height field
	// (x0, y0, step, cols, rows, z; NaN off the mold); false without the file or a 3D device.
	const QStringList &molds();
	// What the view draws; true if the objects must be reloaded (models switched on or off). The
	// engine keeps the options for maps loaded later.
	bool setDisplay(const MeDisplayOptions &options);
	// The map's lighting of its time of day into the view, without loading the map again.
	bool setLighting(const MapModel::GlobalLighting &lighting);
	// Noise textures of the game data for the macro texture, sorted; read once.
	const QStringList &macroTextures();
	// Drops the loaded textures so they are read again from the files.
	void reloadTextures();
	// Particle system templates of the started game, sorted; read once (again after an apply).
	const QStringList &particleNames();
	// A template as its INI block; empty if unknown.
	QString particleIni(const QString &name);
	// Loads ParticleSystem blocks over the templates of their names; error has line and reason.
	bool applyParticleIni(const QString &text, QString *error);
	bool saveAllParticles(const QString &path);
	// Shows the system in the 3D view (z below -1e7: on the ground); the view must render
	// continuously while it runs.
	bool previewParticle(const QString &name, float x, float y, float z);
	void stopParticles();
	// 0 as the game data have it, 1 changed, 2 made in the editor.
	int particleChange(const QString &name);
	// Back to the game data; an empty name puts back every system. Returns how many were put back.
	int resetParticles(const QString &name);
	// The blocks of every changed or new system; empty if there are none.
	QString changedParticlesIni();
	// Most particles at once in the preview; 0 takes the game's setting.
	void setParticleLimit(int count);
	// The system as the game data have it; empty for one made in the editor.
	QString particleOriginalIni(const QString &name);
	// Textures of the game data by the name particle systems use; read once.
	const QStringList &textureNames();
	// The texture fitted into size x size pixels; a null image if it cannot be read.
	QImage textureThumbnail(const QString &name, int size);
	bool moldSample(const QString &name, float &x0, float &y0, float &step, int &cols, int &rows, std::vector<float> &z);
	// Picture of the template's model (mePreviewTemplate); null without a model or 3D device.
	QImage preview(const QString &templateName, int size, float yaw, const QString &team, const QColor &background);

private:
	EngineClient() = default;

	bool m_loaded = false;
	bool m_started = false;
	bool m_attached = false;
	Game m_game = Game::ZeroHour;

	MeEngineStartFn m_start = nullptr;
	MeEngineStopFn m_stop = nullptr;
	MeEngineReportFn m_report = nullptr;
	MeViewAttachFn m_attach = nullptr;
	MeViewDetachFn m_detach = nullptr;
	MeViewResizeFn m_resize = nullptr;
	MeViewLoadMapFn m_loadMap = nullptr;
	MeViewUnloadMapFn m_unloadMap = nullptr;
	MeViewGetCameraFn m_getCamera = nullptr;
	MeViewSetCameraFn m_setCamera = nullptr;
	MeViewResetCameraFn m_resetCamera = nullptr;
	MeViewPickFn m_pick = nullptr;
	MeViewPickObjectFn m_pickObject = nullptr;
	MeViewMoveObjectFn m_moveObject = nullptr;
	MeViewObjectInfoFn m_objectInfo = nullptr;
	MeViewObjectsInRectFn m_objectsInRect = nullptr;
	MeViewSetMarqueeFn m_setMarquee = nullptr;
	MeViewReloadObjectsFn m_reloadObjects = nullptr;
	MeViewRenderFn m_render = nullptr;
	MeViewCaptureFn m_capture = nullptr;
	MeViewSetOverlayFn m_setOverlay = nullptr;
	MeViewSetHeightsFn m_setHeights = nullptr;
	MeCatalogCountFn m_catalogCount = nullptr;
	MeCatalogEntryFn m_catalogEntry = nullptr;
	MePreviewTemplateFn m_preview = nullptr;
	MeRoadCountFn m_roadCount = nullptr;
	MeRoadEntryFn m_roadEntry = nullptr;
	MeMapDefaultsFn m_mapDefaults = nullptr;
	MePlayerTemplateCountFn m_factionCount = nullptr;
	MePlayerTemplateEntryFn m_factionEntry = nullptr;
	MeColorCountFn m_colorCount = nullptr;
	MeColorEntryFn m_colorEntry = nullptr;
	MeScriptTemplateCountFn m_scriptTemplateCount = nullptr;
	MeScriptTemplateFn m_scriptTemplate = nullptr;
	MeScriptValueCountFn m_scriptValueCount = nullptr;
	MeScriptValueFn m_scriptValue = nullptr;
	std::vector<MeRoadInfo> m_roads;
	std::vector<MePlayerTemplateInfo> m_factions;
	std::vector<MeColorInfo> m_colors;
	std::vector<MeScriptTemplate> m_conditionTemplates;
	std::vector<MeScriptTemplate> m_actionTemplates;
	std::map<int, std::vector<MeNamedValue>> m_scriptValues;
	MeSoundCountFn m_soundCount = nullptr;
	MeSoundEntryFn m_soundEntry = nullptr;
	std::vector<MeSoundInfo> m_sounds;
	MeTerrainCountFn m_terrainCount = nullptr;
	MeTerrainEntryFn m_terrainEntry = nullptr;
	MeTerrainPreviewFn m_terrainPreview = nullptr;
	MeViewSetTilesFn m_setTiles = nullptr;
	std::vector<MeTerrainInfo> m_terrains;
	MeViewSetBlendTableFn m_setBlendTable = nullptr;
	MeViewSetCliffFlagsFn m_setCliffFlags = nullptr;
	MeViewSetTerrainTintFn m_setTerrainTint = nullptr;
	MeViewSetDisplayFn m_setDisplay = nullptr;
	MeMacroTextureCountFn m_macroTextureCount = nullptr;
	MeMacroTextureNameFn m_macroTextureName = nullptr;
	MeViewReloadTexturesFn m_reloadTextures = nullptr;
	MeParticleCountFn m_particleCount = nullptr;
	MeParticleNameFn m_particleName = nullptr;
	MeParticleIniFn m_particleIni = nullptr;
	MeParticleApplyFn m_particleApply = nullptr;
	MeParticleSaveAllFn m_particleSaveAll = nullptr;
	MeParticlePreviewFn m_particlePreview = nullptr;
	MeParticleStopAllFn m_particleStopAll = nullptr;
	MeParticleChangedFn m_particleChanged = nullptr;
	MeParticleResetFn m_particleReset = nullptr;
	MeParticleChangedIniFn m_particleChangedIni = nullptr;
	MeParticleSetLimitFn m_particleSetLimit = nullptr;
	MeViewSetLightingFn m_setLighting = nullptr;
	MeViewFreezeTimeFn m_freezeTime = nullptr;
	MeViewSetTileFn m_setTile = nullptr;
	MeViewGetWaterLookFn m_getWaterLook = nullptr;
	MeViewSetWaterLookFn m_setWaterLook = nullptr;
	MeParticleOriginalIniFn m_particleOriginalIni = nullptr;
	MeTextureCountFn m_textureCount = nullptr;
	MeTextureNameFn m_textureName = nullptr;
	MeTextureThumbnailFn m_textureThumbnail = nullptr;
	QStringList m_textures;
	bool m_texturesRead = false;
	std::map<QString, QImage> m_thumbnails;		// name@size
	MeMoldCountFn m_moldCount = nullptr;
	MeMoldNameFn m_moldName = nullptr;
	MeMoldSampleFn m_moldSample = nullptr;
	QStringList m_molds;
	bool m_moldsRead = false;
	QStringList m_macroTextures;
	bool m_macroTexturesRead = false;
	QStringList m_particles;
	bool m_particlesRead = false;
	std::vector<MeTemplateInfo> m_catalog;
};

} // namespace MapEditorApp
