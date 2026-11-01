/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EngineApi.h -- the C interface of MapEditorEngine.dll: engine start with the game data, and one
// 3D view (Direct3D 11 through WW3D) in a window of the caller.
//
// The engine replaces the global operator new with its own memory manager, so it lives in its own
// DLL: nothing allocated on one side is freed on the other. Every function takes and returns plain
// values; texts go into buffers of the caller. The editor loads the DLL at runtime (EngineClient.cpp)
// and keeps working without it.
//
// All functions must be called from the same thread (the GUI thread). The engine can be started once
// per process and for one game; a change of game needs a new process.

#pragma once

#include <stdint.h>

// Outside Windows there is no engine DLL; the editor still builds there (without 3D view) for its self test.
#if !defined(_WIN32)
	#define MEAPI extern "C"
#elif defined(MAPEDITOR_ENGINE_BUILD)
	#define MEAPI extern "C" __declspec(dllexport)
#else
	#define MEAPI extern "C" __declspec(dllimport)
#endif

// Raised when the functions or structures below change incompatibly; the editor refuses a DLL with
// another version.
#define ME_API_VERSION 38

enum MeGame
{
	ME_GAME_GENERALS = 0,
	ME_GAME_ZERO_HOUR = 1
};

struct MeEngineSettings
{
	int32_t game;						// MeGame
	const wchar_t *dataFolder;			// *.big and Data\ of the game (Zero Hour: ZH_Generals\ inside)
	const wchar_t *shaderFolder;		// folder with sm4\ (the Shaders folder next to the editor)
	const wchar_t *logFile;				// engine log of this run, or null
	int32_t windowsOnly;				// 1: only what window layouts need (GUI editor): no terrain, scripts or object catalog
};

// Orbit camera around a point on the ground. Angles in radians: yaw 0 looks north (+y), pitch is the
// angle below the horizon.
struct MeCamera
{
	float targetX, targetY, targetZ;
	float yaw;
	float pitch;
	float distance;
};

struct MeMapInfo
{
	int32_t cellsX, cellsY;				// height samples including the border
	int32_t border;
	float worldWidth, worldHeight;		// playable area in world units
	int32_t models;						// objects drawn (models and trees)
	int32_t missingModels;				// objects whose template or model is not in the data
};

// What the 3D view draws (the old WorldBuilder's view menu); 1 on, 0 off.
struct MeDisplayOptions
{
	int32_t models;						// object models and trees
	int32_t shadows;
	int32_t clouds;						// moving cloud shadows on the terrain
	int32_t macroTexture;				// large noise texture over the terrain against tiling
	char macroTextureName[64];			// empty: the game's default
	int32_t softWater;					// soft shore lines
	int32_t wireframe;					// triangle edges over the scene
	int32_t extraBlends;				// cells with a third texture drawn black
	int32_t letterbox;					// black bars for the game's 16:9 cinematic view
	int32_t garrisoned;					// buildings in their garrisoned model state
	// Direct3D 11 look of one of the game's detail levels (GameLODOverride.ini): the effect list as
	// -d3d11Effects takes it (empty: none, the classic look), water quality (0 classic, 1 and 2 the
	// realistic water) and anisotropic filtering of the terrain atlas.
	char effects[256];
	int32_t waterQuality;
	int32_t terrainAnisotropy;
};

// Overlay of the 3D view: shapes over a shared point list (x, y, z in world units per point). The
// z value is a minimum height (water areas lie on their water level); ME_OVERLAY_ON_TERRAIN puts the
// point on the ground. Colours are 0xAARRGGBB, sizes in pixels (line width, marker radius).
enum MeOverlayKind
{
	ME_OVERLAY_PATH = 0,		// open line through the points
	ME_OVERLAY_LOOP = 1,		// closed line
	ME_OVERLAY_MARKER = 2		// diamond at the first point
};

#define ME_OVERLAY_ON_TERRAIN (-1.0e8f)

struct MeOverlayShape
{
	int32_t kind;						// MeOverlayKind
	uint32_t color;
	int32_t firstPoint;
	int32_t pointCount;
	float size;
};

enum MeObjectKind
{
	ME_OBJECT_MODEL = 0,		// drawn by a model
	ME_OBJECT_TREE = 1,			// drawn by the terrain's tree buffer
	ME_OBJECT_MARKER = 2		// nothing drawn: waypoints, sounds, lights, scorch marks, missing models
};

// A map object: its index in the editor's object list (the engine reads it from the property
// "editorIndex" the editor writes into the map it passes), its position on the ground (terrain
// height plus the object's height), its radius in the ground plane and what draws it.
struct MePickResult
{
	int32_t object;
	float x, y, z;
	float radius;
	int32_t kind;						// MeObjectKind
};

// An object template of the game data (object catalog). Texts are UTF-8.
struct MeTemplateInfo
{
	char name[96];						// template name, as the map stores it
	char side[48];						// default owning side ("America", "Civilian", ...), may be empty
	char displayName[128];				// the game's name for it, may be empty
	int32_t sorting;					// editor sorting: 0 none, 1 structure, 2 infantry, 3 vehicle,
										// 4 shrubbery, 5 man-made, 6 natural, 7 debris, 8 system,
										// 9 audio, 10 test, 11 for review, 12 road, 13 waypoint
	float placementAngle;				// facing in radians for an object placed with a click
	char ambientSound[96];				// sound the template plays by default, empty if none
	float fenceWidth;					// distance between the pieces of a fence, 0 if it is none
	float fenceOffset;					// offset of a fence piece along the fence
	int32_t energy;						// power it produces (positive) or uses (negative)
	int32_t geometry;					// 0 sphere, 1 cylinder, 2 box
	float majorRadius;					// radius, or half length along the facing for a box
	float minorRadius;					// half width of a box
	float visionRange;					// 0 if it sees nothing
	float weaponRange[3];				// longest range per weapon slot (primary, secondary, tertiary), 0 if none
};

// A sound effect (SoundEffects.ini) with the values an object's customised sound starts from.
struct MeSoundInfo
{
	char name[96];
	float volume;						// 0 to 1
	float minVolume;
	float minRange;
	float maxRange;
	int32_t priority;					// 0 lowest, 1 low, 2 normal, 3 high, 4 critical
	int32_t looping;					// 1 if the sound loops
	int32_t loopCount;					// 0: forever (when looping)
};

typedef int32_t (*MeApiVersionFn)(void);
typedef int32_t (*MeEngineStartFn)(const MeEngineSettings *settings, char *error, int32_t errorSize);
typedef void (*MeEngineStopFn)(void);
typedef int32_t (*MeEngineReportFn)(char *text, int32_t size);
typedef int32_t (*MeViewAttachFn)(void *hwnd, int32_t width, int32_t height, char *error, int32_t errorSize);
typedef void (*MeViewDetachFn)(void);
typedef void (*MeViewResizeFn)(int32_t width, int32_t height);
typedef int32_t (*MeViewLoadMapFn)(const uint8_t *chunks, int32_t size, MeMapInfo *info, char *error, int32_t errorSize);
typedef void (*MeViewUnloadMapFn)(void);
typedef void (*MeViewGetCameraFn)(MeCamera *camera);
typedef void (*MeViewSetCameraFn)(const MeCamera *camera);
typedef void (*MeViewResetCameraFn)(void);
typedef int32_t (*MeViewPickFn)(int32_t x, int32_t y, float *world3);
typedef void (*MeViewRenderFn)(void);
typedef void (*MeViewCaptureFn)(const wchar_t *bmpPath);
typedef int32_t (*MeViewPickObjectFn)(int32_t x, int32_t y, MePickResult *result);
typedef void (*MeViewMoveObjectFn)(int32_t object, float x, float y, float z, float angle);
typedef int32_t (*MeViewObjectInfoFn)(int32_t object, MePickResult *result);
typedef int32_t (*MeViewObjectsInRectFn)(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t *objects, int32_t maxObjects);
typedef void (*MeViewSetMarqueeFn)(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
typedef int32_t (*MeViewReloadObjectsFn)(const uint8_t *chunks, int32_t size);
typedef int32_t (*MeCatalogCountFn)(void);
typedef int32_t (*MeCatalogEntryFn)(int32_t index, MeTemplateInfo *info);
// A terrain texture from Terrain.ini: its tiles (64-pixel squares, each covering 2 x 2 cells) as the
// old WorldBuilder counts them.
struct MeTerrainInfo
{
	char name[64];
	int32_t numTiles;					// width * width
	int32_t width;						// tiles across
	int32_t blendEdge;					// 1: an edge texture for blending, not for painting
};

// An entry of the map's blend table (BlendTileData): the tile blended over a cell and how.
struct MeBlendTile
{
	int32_t blendIndex;					// tile index (tile * 4 + quarter)
	uint8_t horizontal;
	uint8_t vertical;
	uint8_t rightDiagonal;
	uint8_t leftDiagonal;
	uint8_t inverted;					// bit 0: inverted, bit 1: forced flip
	uint8_t longDiagonal;
	uint8_t unused[2];
	int32_t customBlendEdgeClass;		// -1: alpha blend
};

// The grid of a mold's height field: cols x rows samples, step apart, the first at (x0, y0).
struct MeMoldGrid
{
	float x0;
	float y0;
	float step;
	int32_t cols;
	int32_t rows;
};

// Lighting and water height of GameData.ini, as a new map starts with them.
struct MeLight
{
	float ambient[3];
	float diffuse[3];
	float position[3];
};
// How the water looks (WaterTransparency of Water.ini, which a map overrides in its map.ini); colours
// 0..1. The realistic water of the detail levels Medium and up uses turbidity, refraction and
// reflection; turbidityDepth 0 keeps the classic water on the map.
struct MeWaterLook
{
	float transparentDepth;
	float minOpacity;
	float standingColor[3];
	int32_t additive;
	float turbidityDepth;
	float turbidityColor[3];
	float refraction;
	float reflection;
};
// Lighting of one time of day (1 morning to 4 night) as the map stores it: sun and two accent lights
// for the terrain and for objects, colours 0..1, position as the direction the light shines in;
// shadowColor 0xAARRGGBB, 0 keeps the current one.
struct MeLighting
{
	int32_t timeOfDay;
	MeLight terrain[3];
	MeLight objects[3];
	uint32_t shadowColor;
};
struct MeMapDefaults
{
	int32_t timeOfDay;					// 1 morning .. 4 night
	MeLight terrain[4][3];				// per time of day, three lights
	MeLight objects[4][3];
	float waterHeight;
};

// A faction from PlayerTemplate.ini.
struct MePlayerTemplateInfo
{
	char name[64];						// "FactionAmerica", as the map stores it
	char side[48];
	char displayName[96];				// the game's name for it (UTF-8), may be empty
	uint32_t color;						// preferred colour, 0xRRGGBB
	int32_t playable;					// 1: a side players can pick
};

// A player colour from Multiplayer.ini.
struct MeColorInfo
{
	char name[64];						// the game's name for it (UTF-8)
	uint32_t color;						// 0xAARRGGBB, as the map stores playerColor
};

// A condition or action of the script engine; its index is the number the map stores.
#define ME_SCRIPT_MAX_PARAMETERS 12
struct MeScriptTemplate
{
	char internalName[64];				// the name the map stores beside the index
	char uiName[160];					// menu path, parts separated by '/'
	char uiName2[160];					// a second path, may be empty
	char help[256];
	int32_t numParameters;
	int32_t parameterTypes[ME_SCRIPT_MAX_PARAMETERS];	// Parameter::ParameterType
	int32_t numUiStrings;				// text pieces around the parameters
	char uiStrings[ME_SCRIPT_MAX_PARAMETERS][192];
};

// A value the game data offers for a script parameter: its name and, for types stored as numbers,
// the number.
struct MeNamedValue
{
	char name[96];
	int32_t value;
};

// A road or bridge type from Roads.ini.
struct MeRoadInfo
{
	char name[96];
	int32_t bridge;						// 1: a bridge type
};

typedef void (*MeViewSetHeightsFn)(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t *heights, int32_t width);
typedef int32_t (*MeTerrainCountFn)(void);
typedef int32_t (*MeTerrainEntryFn)(int32_t index, MeTerrainInfo *info);
typedef int32_t (*MeTerrainPreviewFn)(const char *name, int32_t size, uint8_t *rgba);
typedef void (*MeViewSetTilesFn)(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const int16_t *tiles, const int16_t *blends,
	const int16_t *extraBlends, const int16_t *cliffInfos, int32_t width);
typedef void (*MeViewSetBlendTableFn)(const MeBlendTile *entries, int32_t count);
typedef void (*MeViewSetCliffFlagsFn)(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t *flags, int32_t width);
typedef void (*MeViewSetTerrainTintFn)(const uint32_t *colors, int32_t width, int32_t height, int32_t border);
typedef int32_t (*MeViewSetDisplayFn)(const MeDisplayOptions *options);
typedef int32_t (*MeMacroTextureCountFn)(void);
typedef int32_t (*MeMacroTextureNameFn)(int32_t index, char *name, int32_t size);
typedef void (*MeViewReloadTexturesFn)(void);
typedef int32_t (*MeViewSetLightingFn)(const MeLighting *lighting);
typedef void (*MeViewFreezeTimeFn)(int32_t freeze);
typedef void (*MeViewSetTileFn)(float left, float top, float right, float bottom);
typedef int32_t (*MeViewGetWaterLookFn)(MeWaterLook *look, int32_t gameData);
typedef int32_t (*MeViewSetWaterLookFn)(const MeWaterLook *look);
typedef int32_t (*MeMoldCountFn)(void);
typedef int32_t (*MeMoldNameFn)(int32_t index, char *name, int32_t size);
typedef int32_t (*MeMoldSampleFn)(const char *name, MeMoldGrid *grid, float *z, int32_t capacity);
typedef int32_t (*MeParticleCountFn)(void);
typedef int32_t (*MeParticleNameFn)(int32_t index, char *name, int32_t size);
typedef int32_t (*MeParticleIniFn)(const char *name, char *text, int32_t size);
typedef int32_t (*MeParticleApplyFn)(const char *iniText, char *error, int32_t errorSize);
typedef int32_t (*MeParticleSaveAllFn)(const wchar_t *path);
typedef int32_t (*MeParticlePreviewFn)(const char *name, float x, float y, float z);
typedef void (*MeParticleStopAllFn)(void);
typedef int32_t (*MeParticleChangedFn)(const char *name);
typedef int32_t (*MeParticleResetFn)(const char *name);
typedef int32_t (*MeParticleChangedIniFn)(char *text, int32_t size);
typedef void (*MeParticleSetLimitFn)(int32_t count);
typedef int32_t (*MeParticleOriginalIniFn)(const char *name, char *text, int32_t size);
typedef int32_t (*MeTextureCountFn)(void);
typedef int32_t (*MeTextureNameFn)(int32_t index, char *name, int32_t size);
typedef int32_t (*MeTextureThumbnailFn)(const char *name, int32_t size, uint8_t *rgba);
typedef int32_t (*MeSoundCountFn)(void);
typedef int32_t (*MeSoundEntryFn)(int32_t index, MeSoundInfo *info);
typedef int32_t (*MeRoadCountFn)(void);
typedef int32_t (*MeRoadEntryFn)(int32_t index, MeRoadInfo *info);
typedef int32_t (*MePreviewTemplateFn)(const char *name, int32_t size, float yaw, const char *team, uint32_t background, uint8_t *rgba);
typedef int32_t (*MeMapDefaultsFn)(MeMapDefaults *defaults);
typedef int32_t (*MePlayerTemplateCountFn)(void);
typedef int32_t (*MePlayerTemplateEntryFn)(int32_t index, MePlayerTemplateInfo *info);
typedef int32_t (*MeColorCountFn)(void);
typedef int32_t (*MeColorEntryFn)(int32_t index, MeColorInfo *info);
typedef int32_t (*MeScriptTemplateCountFn)(int32_t actions);
typedef int32_t (*MeScriptTemplateFn)(int32_t actions, int32_t index, MeScriptTemplate *info);
typedef int32_t (*MeScriptValueCountFn)(int32_t parameterType);
typedef int32_t (*MeScriptValueFn)(int32_t parameterType, int32_t index, MeNamedValue *value);
typedef void (*MeViewSetOverlayFn)(const float *points, int32_t pointCount, const MeOverlayShape *shapes, int32_t shapeCount);

// Version of this interface (ME_API_VERSION).
MEAPI int32_t meApiVersion(void);

// Starts the engine for one game: file system on dataFolder, game data (INI), terrain types, shaders.
// Returns 1 on success; on failure 0 and a message in error. Repeated calls after a success return 1.
MEAPI int32_t meEngineStart(const MeEngineSettings *settings, char *error, int32_t errorSize);
// Shuts the engine down (view, device, subsystems). Safe to call when nothing was started.
MEAPI void meEngineStop(void);
// Start report: steps with their duration and the renderer, one per line. Returns the length.
MEAPI int32_t meEngineReport(char *text, int32_t size);
// The object catalog of the started engine: number of templates, and one of them. Entries whose
// name does not fit into MeTemplateInfo::name are left out.
MEAPI int32_t meCatalogCount(void);
MEAPI int32_t meCatalogEntry(int32_t index, MeTemplateInfo *info);
// Picture of a template's model for the catalog: size x size pixels (a power of two, 16 to 512),
// RGBA with the top row first, lit like the objects of the map. yaw: direction the camera looks
// from, radians around the model (0: from the front). team: team whose colour the model wears (null:
// none). background: 0xRRGGBB. Needs the 3D device; returns 0 if the template has no model.
// Road and bridge types of the started engine (Roads.ini): number, and one of them.
// Terrain textures of the started engine, sorted by name; a picture of one tile of a texture (size x
// size pixels, RGBA, top row first).
MEAPI int32_t meTerrainCount(void);
MEAPI int32_t meTerrainEntry(int32_t index, MeTerrainInfo *info);
MEAPI int32_t meTerrainPreview(const char *name, int32_t size, uint8_t *rgba);
// Mesh molds (Data\Editor\Molds\*.w3d of the started engine), sorted by name. meMoldSample fills the
// grid of the mold's top surface and returns its number of samples; with room for them (capacity) it
// writes the heights too, NaN where the mold has none. Needs the 3D device.
MEAPI int32_t meMoldCount(void);
MEAPI int32_t meMoldName(int32_t index, char *name, int32_t size);
MEAPI int32_t meMoldSample(const char *name, MeMoldGrid *grid, float *z, int32_t capacity);
// Sound effects of the started engine, sorted by name.
MEAPI int32_t meSoundCount(void);
MEAPI int32_t meSoundEntry(int32_t index, MeSoundInfo *info);
// Lighting and water height of the game data, unchanged by the maps loaded since. 0 before the start.
MEAPI int32_t meMapDefaults(MeMapDefaults *defaults);
// Factions and player colours of the game data, in the game's order.
MEAPI int32_t mePlayerTemplateCount(void);
MEAPI int32_t mePlayerTemplateEntry(int32_t index, MePlayerTemplateInfo *info);
MEAPI int32_t meColorCount(void);
MEAPI int32_t meColorEntry(int32_t index, MeColorInfo *info);
// Script conditions (actions 0) and actions (actions 1), in the order of the game's numbers.
MEAPI int32_t meScriptTemplateCount(int32_t actions);
MEAPI int32_t meScriptTemplate(int32_t actions, int32_t index, MeScriptTemplate *info);
// Names the game data has for a parameter type (sounds, sciences, kinds of, ...); 0 for types whose
// values come from the map or are typed in.
MEAPI int32_t meScriptValueCount(int32_t parameterType);
MEAPI int32_t meScriptValue(int32_t parameterType, int32_t index, MeNamedValue *value);
MEAPI int32_t meRoadCount(void);
MEAPI int32_t meRoadEntry(int32_t index, MeRoadInfo *info);
MEAPI int32_t mePreviewTemplate(const char *name, int32_t size, float yaw, const char *team, uint32_t background, uint8_t *rgba);

// Creates the Direct3D 11 device on the window (a native child window of the editor).
MEAPI int32_t meViewAttach(void *hwnd, int32_t width, int32_t height, char *error, int32_t errorSize);
MEAPI void meViewDetach(void);
MEAPI void meViewResize(int32_t width, int32_t height);

// Builds terrain and lighting from an uncompressed map (the chunk bytes of a .map file). The
// camera is reset to the start camera of the map.
MEAPI int32_t meViewLoadMap(const uint8_t *chunks, int32_t size, MeMapInfo *info, char *error, int32_t errorSize);
MEAPI void meViewUnloadMap(void);

MEAPI void meViewGetCamera(MeCamera *camera);
MEAPI void meViewSetCamera(const MeCamera *camera);
MEAPI void meViewResetCamera(void);

// Point on the terrain under the window pixel (x, y). Returns 1 on a hit.
MEAPI int32_t meViewPick(int32_t x, int32_t y, float *world3);

// Draws one frame and presents it.
MEAPI void meViewRender(void);
// Saves the next rendered frame as a 24-bit BMP (self test).
MEAPI void meViewCapture(const wchar_t *bmpPath);

// Object under the window pixel (x, y), hidden ones behind hills excepted. Returns 1 on a hit.
MEAPI int32_t meViewPickObject(int32_t x, int32_t y, MePickResult *result);
// Puts the object (editor index) at (x, y) with z above the ground and the angle in radians.
MEAPI void meViewMoveObject(int32_t object, float x, float y, float z, float angle);
// Position, radius and kind of the object (editor index). Returns 1 if the view has it.
MEAPI int32_t meViewObjectInfo(int32_t object, MePickResult *result);
// Objects whose position lies in the window rectangle (pixels, corners in any order). Writes up to
// maxObjects editor indices and returns how many objects lie in the rectangle.
MEAPI int32_t meViewObjectsInRect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t *objects, int32_t maxObjects);
// Selection rectangle drawn over the view (pixels); an empty rectangle hides it.
MEAPI void meViewSetMarquee(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
// Rebuilds the map objects (models, trees, shadows, scorch marks, waypoints, areas) from the chunk
// bytes of the map after objects were removed or inserted; terrain, roads and water stay. Returns 1
// on success.
MEAPI int32_t meViewReloadObjects(const uint8_t *chunks, int32_t size);

// Replaces the overlay (the data is copied); no shapes clear it. Loading or unloading a map clears it.
// Copies the cells x0..x1, y0..y1 (height map cells, border included) from heights (the whole height
// map, width cells per row, as the map stores it) into the view and rebuilds the terrain there.
// Objects keep their places until the next meViewReloadObjects.
MEAPI void meViewSetHeights(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t *heights, int32_t width);
// Copies the tile, blend, extra blend and cliff indices of the cells x0..x1, y0..y1 (whole arrays of
// the map, width cells per row) into the view and rebuilds the terrain there. The tiles must belong to
// texture classes the view loaded with the map; new classes need meViewLoadMap.
MEAPI void meViewSetTiles(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const int16_t *tiles, const int16_t *blends,
	const int16_t *extraBlends, const int16_t *cliffInfos, int32_t width);
// Replaces the view's blend table with entries 1..count (entry 0, no blend, is implicit). Blends the
// editor adds go in here before meViewSetTiles uses them.
MEAPI void meViewSetBlendTable(const MeBlendTile *entries, int32_t count);
// Copies the cliff flags of cells x0..x1, y0..y1 from flags (the map's cliff state: one bit per cell,
// rows of (width + 7) / 8 bytes) into the view; they decide where units cannot go.
MEAPI void meViewSetCliffFlags(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t *flags, int32_t width);
// Tints the terrain cell by cell over the scene (slopes, passability): colors holds one 0xAARRGGBB
// per height map cell, width x height with the map's border, alpha 0 for none; null clears it.
MEAPI void meViewSetTerrainTint(const uint32_t *colors, int32_t width, int32_t height, int32_t border);
// Stops the clock of the view's animations (water, models) while freeze is 1, as the game's fixed
// time step does for image comparisons; 0 lets it run again.
MEAPI void meViewFreezeTime(int32_t freeze);
// Draws only a part of the camera's picture, in fractions of it (0, 0 top left; 1, 1 bottom right; a
// part may reach beyond), so a picture larger than the view can be put together from views
// (posters); 0, 0, 1, 1 draws all of it.
MEAPI void meViewSetTile(float left, float top, float right, float bottom);
// The water look (MeWaterLook). gameData 1: the values of the game data, before any
// meViewSetWaterLook; 0: the current ones.
MEAPI int32_t meViewGetWaterLook(MeWaterLook *look, int32_t gameData);
// Sets the values for the view (a map's map.ini, or a live edit); the water shows them in the next frame.
MEAPI int32_t meViewSetWaterLook(const MeWaterLook *look);
// Sets what the view draws; kept for maps loaded later. Returns 1 if the objects must be built again
// (meViewReloadObjects) because models or the garrisoned state were switched.
MEAPI int32_t meViewSetDisplay(const MeDisplayOptions *options);
// Lighting of one time of day into the view (MeLighting). Relights terrain and objects and moves the
// sun shadows without loading the map again. Returns 1 if a view shows a map.
MEAPI int32_t meViewSetLighting(const MeLighting *lighting);
// Noise textures of the game data for the macro texture (Art/Textures), sorted.
MEAPI int32_t meMacroTextureCount(void);
MEAPI int32_t meMacroTextureName(int32_t index, char *name, int32_t size);
// Drops every loaded texture, so the files are read again; the map is to be loaded again after it.
MEAPI void meViewReloadTextures(void);
MEAPI void meViewSetOverlay(const float *points, int32_t pointCount, const MeOverlayShape *shapes, int32_t shapeCount);

// Particle system templates of the started engine (ParticleSystem.ini and what meParticleApply
// added), sorted by name.
MEAPI int32_t meParticleCount(void);
MEAPI int32_t meParticleName(int32_t index, char *name, int32_t size);
// The template as its INI block ("ParticleSystem <Name>" ... "End", lines end in \n), written by
// the game's own writer. Returns the full length like snprintf; text is NUL-terminated and cut if
// size is too small. 0 if the template is unknown.
MEAPI int32_t meParticleIni(const char *name, char *text, int32_t size);
// Reads one or more ParticleSystem blocks and loads them over the templates of those names (new
// names create templates). On an error returns 0 with the line (counted in iniText) and the reason
// in error; templates that existed before keep their values, a new name is dropped again. A running
// preview starts again with the new values. Returns 1 on success.
MEAPI int32_t meParticleApply(const char *iniText, char *error, int32_t errorSize);
// How a template differs from the game data: 0 not at all (or unknown), 1 changed values, 2 made
// by meParticleApply.
MEAPI int32_t meParticleChanged(const char *name);
// Puts the template back as the game data have it (a new one is dropped); an empty or null name does
// this for every template. Returns the number of templates put back.
MEAPI int32_t meParticleReset(const char *name);
// The INI blocks of every changed or new template, sorted by name; length and cutting as
// meParticleIni. 0 if nothing is changed.
MEAPI int32_t meParticleChangedIni(char *text, int32_t size);
// Most particles alive at once in the preview; 0 takes the game's detail setting (or 5000).
MEAPI void meParticleSetLimit(int32_t count);
// The template as the game data have it, before any meParticleApply (length and cutting as
// meParticleIni); for an unchanged template the same as meParticleIni, 0 for one made by the editor.
MEAPI int32_t meParticleOriginalIni(const char *name, char *text, int32_t size);

// Textures of the game data (Art\Textures, loose files and archives), sorted, by the name models and
// particle systems use (file name with .tga, also for a .dds).
MEAPI int32_t meTextureCount(void);
MEAPI int32_t meTextureName(int32_t index, char *name, int32_t size);
// The texture fitted into size x size RGBA pixels (rows top down, transparent around it). Returns 1 on
// success, 0 if the texture cannot be read.
MEAPI int32_t meTextureThumbnail(const char *name, int32_t size, uint8_t *rgba);
// Writes every template, sorted by name, to the file (CRLF lines). Returns 1 on success.
MEAPI int32_t meParticleSaveAll(const wchar_t *path);
// Shows the system in the 3D view at the world position; z below -1e7 puts it on the terrain. Stops
// the previous preview; a system that has died out starts again after a short pause. Needs the 3D
// device and a loaded map. The systems advance at the game's logic rate in meViewRender, so the
// caller renders continuously while a preview runs. Returns 1 on success.
MEAPI int32_t meParticlePreview(const char *name, float x, float y, float z);
// Stops the preview and destroys every particle system.
MEAPI void meParticleStopAll(void);

// Window layouts (.wnd) with the game's window system, drawn into the attached view (GUI editor).
// Start after meViewAttach; the display takes the size of the view.
MEAPI int32_t meWindowsStart(char *error, int32_t errorSize);
// Reads a layout with the game's reader (a path without a backslash is looked up in the folder
// Window); the windows of the layout before are gone. Returns the number of windows, -1 if it cannot be read.
MEAPI int32_t meWindowsLoad(const char *path, char *error, int32_t errorSize);
MEAPI void meWindowsClear(void);
// Draws the windows on a background of 0xRRGGBB.
MEAPI void meWindowsRender(uint32_t background);
// Every layout the game can read, loose or in an archive: paths below the folder Window with '/',
// sorted, one per line. Returns the length of the text (the buffer may be null to ask for it).
MEAPI int32_t meWindowsLayouts(char *text, int32_t size);
// Reads a file through the game's file system (archives included) into the buffer, at most size
// bytes. Returns the size of the file, -1 if there is none.
MEAPI int32_t meFileRead(const char *path, char *buffer, int32_t size);
// Shows the windows a layout starts hidden (the game shows them later); applies from the next load.
MEAPI void meWindowsShowHidden(int32_t show);
// NAME of the deepest visible window at the point of the view, as written in the layout; empty if none.
// Returns the length of the name.
MEAPI int32_t meWindowsHit(int32_t x, int32_t y, char *name, int32_t size);
// Frames the windows with these names (NAME as written, one per line) in the colour 0xAARRGGBB; null clears.
MEAPI void meWindowsHighlight(const char *names, uint32_t argb);
// Screen rectangle (left, top, right, bottom) of the first window with this NAME; 0 if there is none.
MEAPI int32_t meWindowsRect(const char *name, int32_t *rect);
// Rectangles drawn over the windows (handles, a selection band): count items of six values each, left, top,
// right, bottom in view pixels, colour 0xAARRGGBB, filled (1) or a frame (0). Replaces the ones before.
MEAPI void meWindowsOverlay(const int32_t *items, int32_t count);
// Catalogs for the GUI editor. Each returns the length of its text (the buffer may be null to ask for it).
// Mapped images, one per line: name, width and height separated by tabs, sorted by name.
MEAPI int32_t meWindowsImages(char *text, int32_t size);
// The picture of a mapped image fitted into size x size RGBA (rest transparent); 1 on success.
MEAPI int32_t meWindowsImagePreview(const char *name, int32_t size, uint8_t *rgba);
// Header templates, one name per line.
MEAPI int32_t meWindowsHeaderTemplates(char *text, int32_t size);
// The game's callbacks of a kind ("system", "input", "tooltip", "draw", "init", "update", "shutdown"), one per line.
MEAPI int32_t meWindowsCallbacks(const char *kind, char *text, int32_t size);
// Every key of the game's string table, one per line, sorted.
MEAPI int32_t meWindowsTextKeys(char *text, int32_t size);
// The text of a key in UTF-8; -1 if the string table has no such key.
MEAPI int32_t meWindowsText(const char *key, char *text, int32_t size);

// Test mode: input to the windows as the game passes it on (WindowXlat.cpp). Position in view pixels; a move
// with the left button down is a drag. ME_INPUT_WHEEL: data > 0 up. ME_INPUT_CHAR: data is a UTF-16 unit of
// typed text (13 for Enter) for the window with the keyboard focus. ME_INPUT_KEY: data is a MeWindowsKey.
enum MeWindowsInput
{
	ME_INPUT_MOVE = 0,
	ME_INPUT_LEFT_DOWN,
	ME_INPUT_LEFT_UP,
	ME_INPUT_RIGHT_DOWN,
	ME_INPUT_RIGHT_UP,
	ME_INPUT_WHEEL,
	ME_INPUT_CHAR,
	ME_INPUT_KEY
};
enum MeWindowsKey
{
	ME_KEY_BACKSPACE = 1,
	ME_KEY_DELETE,
	ME_KEY_LEFT,
	ME_KEY_RIGHT,
	ME_KEY_UP,
	ME_KEY_DOWN,
	ME_KEY_HOME,
	ME_KEY_END,
	ME_KEY_TAB
};
// Returns 1 if a window used the input.
MEAPI int32_t meWindowsInput(int32_t kind, int32_t x, int32_t y, int32_t data);
// What the gadgets told their windows since the last call (a button pressed, a slider moved, a row chosen,
// text typed), one per line: NAME as written, the message (GBM_SELECTED, ...) and its value, separated by
// tabs. Loading a layout clears it. Returns the length of the text.
MEAPI int32_t meWindowsEvents(char *text, int32_t size);
// A picture drawn under the windows, stretched to the view (a scene of the game, to see the layout on it):
// width x height RGBA, rows from the top; null clears it. Returns 1 on success.
MEAPI int32_t meWindowsBackgroundImage(const uint8_t *rgba, int32_t width, int32_t height);
