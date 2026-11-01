/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EngineInternal.h -- state shared by the files of MapEditorEngine.dll.

#pragma once

#include "Engine/EngineApi.h"

#include <cstdint>
#include <string>
#include <vector>

class AsciiString;
class CameraClass;
class HeightMapRenderObjClass;
class MapObject;
class RenderObjClass;
class ThingTemplate;
class Vector3;
class RTS3DScene;
class W3DAssetManager;

namespace MapEditorEngine
{

struct ObjectCounts
{
	int models = 0;
	int trees = 0;
	int scorches = 0;
	int shadows = 0;
	int withoutModel = 0;	// objects whose template has no model (sounds, markers)
	int missingModel = 0;	// model named, but not found in the data
	int unknown = 0;		// template not in the object catalog
	double milliseconds = 0.0;
};

// One map object of the view: what draws it, and its index in the editor's model.
struct MapEntry
{
	MapObject *object = nullptr;
	int editorIndex = -1;
	RenderObjClass *model = nullptr;	// holds the reference from Create_Render_Obj
	bool tree = false;					// drawn by the terrain's tree buffer
	bool scorch = false;				// drawn by the terrain
	bool vehicle = false;				// tilts with the slope
	float size = 0.0f;					// pick radius of a tree
};

// Adds a model per map object to the scene with its shadow, trees and scorch marks to the terrain,
// and an entry per object (road and bridge points excepted). Without models, objects get only their
// entry (picked by a sphere); scorch marks stay. garrisoned: models in their garrisoned state.
ObjectCounts buildMapObjects(RTS3DScene *scene, W3DAssetManager *assets, HeightMapRenderObjClass *terrain,
	std::vector<MapEntry> &entries, bool models, bool garrisoned);
void releaseMapObjects(RTS3DScene *scene, HeightMapRenderObjClass *terrain, std::vector<MapEntry> &entries);
// Nearest object hit by the ray (unit direction) before maxDistance; models by their bounding box,
// the rest by a sphere. perPixel: world units per pixel at distance 1.
bool pickMapObject(const std::vector<MapEntry> &entries, HeightMapRenderObjClass *terrain, const Vector3 &from,
	const Vector3 &dir, float maxDistance, float perPixel, MePickResult *result);
// Places the object (z above the ground) and updates what draws it.
bool moveMapObject(std::vector<MapEntry> &entries, HeightMapRenderObjClass *terrain, int editorIndex, float x, float y, float z, float angle);
bool mapObjectInfo(const std::vector<MapEntry> &entries, HeightMapRenderObjClass *terrain, int editorIndex, MePickResult *result);
// Editor indices of the objects whose position the camera projects into the pixel rectangle.
int objectsInRect(const std::vector<MapEntry> &entries, HeightMapRenderObjClass *terrain, CameraClass *camera,
	int width, int height, int x0, int y0, int x1, int y1, int32_t *objects, int maxObjects);

// Overlay of the editor (EngineOverlay.cpp), drawn after the scene.
void clearOverlay();
// The terrain's heights changed: the tint lies on the ground anew.
void terrainHeightsChanged();
// perPixelAtUnitDistance: world units per pixel at distance 1 from the camera.
void renderOverlay(CameraClass *camera, HeightMapRenderObjClass *terrain, float perPixelAtUnitDistance);
// Selection rectangle in pixels of the view (drawn by renderOverlay); empty hides it.
void setMarquee(int x0, int y0, int x1, int y1);
void renderMarquee(int width, int height);
// Black bars above and below a 16:9 picture, as the game shows cinematics.
void renderLetterbox(int width, int height);

// Colour of the side that owns the team in the map the engine read; white if none.
int teamColor(const char *teamName);
// Model shown for a template without a map object: undamaged, the map's weather and time aside; a
// tree's model. Empty if the template has none (sounds, markers).
AsciiString defaultModelName(const ThingTemplate *tt);
// The 3D device is ready (meViewAttach succeeded).
bool viewReady();
// Size of the attached view in pixels; 0 without a view.
void viewSize(int &width, int &height);
// Ends the window system of the GUI editor (EngineWindows.cpp); before the view goes.
void stopWindows();

// A text of the game (UTF-16) as UTF-8.
std::string utf8(const wchar_t *text);
// Part of a texture (u and v from 0 to 1) fitted into size x size RGBA, the rest transparent; false if the
// texture cannot be read.
bool textureCrop(const std::string &file, float u0, float v0, float u1, float v1, int size, uint8_t *rgba);

// Appends a line to the start report and to the log file of the run.
void report(const std::string &line);
bool engineStarted();
void setError(char *error, int32_t size, const std::string &text);

// Sets the lighting of GameData.ini again: a map without a lighting chunk keeps what is set.
void restoreDefaultLighting();

// Shuts the view down before the subsystems go (called by meEngineStop).
void shutdownView();
// Drops the object catalog; the next engine start may read other game data.
void clearCatalog();
// Drops the list of terrain textures, like the catalog.
void clearTerrains();
// Drops the list of mold files, like the catalog.
void clearMolds();
// Drops the value lists of script parameters, like the catalog.
void clearScriptValues();

// Height of the view's terrain at (x, y); false without a loaded map.
bool terrainHeightAt(float x, float y, float *z);
// Advances the particle preview at the game's logic rate by the time since the last call; does
// nothing without a preview (EngineParticles.cpp).
void stepParticles();
// Stops the preview and destroys every particle system.
void stopParticles();
// While alive, the particle renderer finds its textures in the view's asset manager. Only set when
// particle systems exist, so a view without them renders as before.
class ParticleRenderScope
{
public:
	explicit ParticleRenderScope(W3DAssetManager *assets);
	~ParticleRenderScope();
	ParticleRenderScope(const ParticleRenderScope &) = delete;
	ParticleRenderScope &operator=(const ParticleRenderScope &) = delete;

private:
	W3DAssetManager *m_previous = nullptr;
	bool m_active = false;
};

} // namespace MapEditorEngine
