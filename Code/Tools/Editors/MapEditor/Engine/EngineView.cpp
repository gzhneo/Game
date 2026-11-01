/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// EngineView.cpp -- the 3D view of the map editor: WW3D on the caller's window, terrain and lighting
// of the map, orbit camera, picking. Device setup, scene and lighting follow the old WorldBuilder
// (wbview3d.cpp); the camera is new.

#include "Engine/EngineApi.h"
#include "Engine/EngineInternal.h"
#include "Engine/MemoryStream.h"

#include "Common/GlobalData.h"
#include "Common/MapObject.h"
#include "Common/WellKnownKeys.h"
#include "GameClient/View.h"
#include "GameClient/Water.h"
#include "GameLogic/PolygonTrigger.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/W3DWater.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "agg_def.h"
#include "texture.h"
#include "camera.h"
#include "coltest.h"
#include "light.h"
#include "part_ldr.h"
#include "predlod.h"
#include "WW3D2/Common/renderdevice.h"
#include "statistics.h"
#include "wwmath.h"
#include "ww3d.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

extern HWND ApplicationHWnd;

namespace
{

// The shadow manager and the terrain ask TheTacticalView for its size and field of view; the editor
// has no game view, so this one only answers.
class EditorView : public View
{
protected:
	Int m_width = 0, m_height = 0;
	Int m_originX = 0, m_originY = 0;

	virtual View *prependViewToList(View *) override { return nullptr; }
	virtual View *getNextView(void) override { return nullptr; }

public:
	virtual void init(void) override {}
	virtual UnsignedInt getID(void) override { return 1; }
	virtual Drawable *pickDrawable(const ICoord2D *, Bool, PickType) override { return nullptr; }
	virtual Int iterateDrawablesInRegion(IRegion2D *, Bool (*)(Drawable *, void *), void *) override { return 0; }
	virtual WorldToScreenReturn worldToScreenTriReturn(const Coord3D *, ICoord2D *) override { return WorldToScreenReturn::WTS_INVALID; }
	virtual void screenToWorld(const ICoord2D *, Coord3D *) override {}
	virtual void screenToTerrain(const ICoord2D *, Coord3D *) override {}
	virtual void screenToWorldAtZ(const ICoord2D *, Coord3D *, Real) override {}
	virtual void getScreenCornerWorldPointsAtZ(Coord3D *, Coord3D *, Coord3D *, Coord3D *, Real) override {}
	virtual void drawView(void) override {}
	virtual void updateView(void) override {}
	virtual void setZoomLimited(Bool) override {}
	virtual Bool isZoomLimited(void) override { return TRUE; }
	virtual void setWidth(Int width) override { m_width = width; }
	virtual Int getWidth(void) override { return m_width; }
	virtual void setHeight(Int height) override { m_height = height; }
	virtual Int getHeight(void) override { return m_height; }
	virtual void setOrigin(Int x, Int y) override { m_originX = x; m_originY = y; }
	virtual void getOrigin(Int *x, Int *y) override { *x = m_originX; *y = m_originY; }
	virtual void forceRedraw() override {}
	virtual void lookAt(const Coord3D *) override {}
	virtual void initHeightForMap(void) override {}
	virtual void scrollBy(Coord2D *) override {}
	virtual void moveCameraTo(const Coord3D *o, Int, Int, Bool, Real, Real) override { lookAt(o); }
	virtual void moveCameraAlongWaypointPath(Waypoint *, Int, Int, Bool, Real, Real) override {}
	virtual Bool isCameraMovementFinished(void) override { return true; }
	virtual void resetCamera(const Coord3D *, Int, Real, Real) override {}
	virtual void rotateCamera(Real, Int, Real, Real) override {}
	virtual void rotateCameraTowardObject(ObjectID, Int, Int, Real, Real) override {}
	virtual void cameraModFinalZoom(Real, Real, Real) override {}
	virtual void cameraModRollingAverage(Int) override {}
	virtual void cameraModFinalTimeMultiplier(Int) override {}
	virtual void cameraModFinalPitch(Real, Real, Real) override {}
	virtual void cameraModFreezeTime(void) override {}
	virtual void cameraModFreezeAngle(void) override {}
	virtual void cameraModLookToward(Coord3D *) override {}
	virtual void cameraModFinalLookToward(Coord3D *) override {}
	virtual void cameraModFinalMoveTo(Coord3D *) override {}
	virtual Bool isTimeFrozen(void) override { return false; }
	virtual Int getTimeMultiplier(void) override { return 1; }
	virtual void setTimeMultiplier(Int) override {}
	virtual void setDefaultView(Real, Real, Real) override {}
	virtual void zoomCamera(Real, Int, Real, Real) override {}
	virtual void pitchCamera(Real, Int, Real, Real) override {}
	virtual void setAngle(Real) override {}
	virtual Real getAngle(void) override { return 0; }
	virtual void setPitch(Real) override {}
	virtual Real getPitch(void) override { return 0; }
	virtual void setAngleAndPitchToDefault(void) override {}
	virtual void getPosition(Coord3D *) override {}
	virtual Real getHeightAboveGround() override { return 1; }
	virtual void setHeightAboveGround(Real) override {}
	virtual Real getZoom() override { return 1; }
	virtual void setZoom(Real) override {}
	virtual void zoomIn(void) override {}
	virtual void zoomOut(void) override {}
	virtual void setZoomToDefault(void) override {}
	virtual Real getMaxZoom(void) override { return 0.0f; }
	virtual void setOkToAdjustHeight(Bool) override {}
	virtual Real getTerrainHeightUnderCamera() override { return 0.0f; }
	virtual void setTerrainHeightUnderCamera(Real) override {}
	virtual Real getCurrentHeightAboveGround() override { return 0.0f; }
	virtual void setCurrentHeightAboveGround(Real) override {}
	virtual void getLocation(ViewLocation *) override {}
	virtual void setLocation(const ViewLocation *) override {}
	virtual ObjectID getCameraLock() const override { return INVALID_ID; }
	virtual void setCameraLock(ObjectID) override {}
	virtual void snapToCameraLock(void) override {}
	virtual void setSnapMode(CameraLockType, Real) override {}
	virtual Drawable *getCameraLockDrawable() const override { return nullptr; }
	virtual void setCameraLockDrawable(Drawable *) override {}
	virtual void setMouseLock(Bool) override {}
	virtual Bool isMouseLocked(void) override { return FALSE; }
	virtual void setFieldOfView(Real) override {}
	virtual Real getFieldOfView(void) override { return 0; }
	virtual Bool setViewFilterMode(enum FilterModes) override { return FALSE; }
	virtual void setViewFilterPos(const Coord3D *) override {}
	virtual void setFadeParameters(Int, Int) override {}
	virtual void set3DWireFrameMode(Bool) override {}
	virtual Bool setViewFilter(enum FilterTypes) override { return FALSE; }
	virtual enum FilterModes getViewFilterMode(void) override { return static_cast<enum FilterModes>(0); }
	virtual enum FilterTypes getViewFilterType(void) override { return static_cast<enum FilterTypes>(0); }
	virtual void shake(const Coord3D *, CameraShakeType) override {}
	virtual Real getFXPitch(void) const override { return 1.0f; }
	virtual void forceCameraConstraintRecalc(void) override {}
	virtual void rotateCameraTowardPosition(const Coord3D *, Int, Real, Real, Bool) override {}
	virtual const Coord3D &get3DCameraPosition() const override
	{
		static Coord3D dummy;
		return dummy;
	}
	virtual void setGuardBandBias(const Coord2D *) override {}
};

constexpr float HORIZONTAL_FOV = 50.0f * 3.14159265f / 180.0f;

struct ViewState
{
	bool deviceReady = false;
	int width = 0;
	int height = 0;
	EditorView tacticalView;
	W3DAssetManager *assets = nullptr;
	RTS3DScene *scene = nullptr;
	CameraClass *camera = nullptr;
	LightClass *globalLights[MAX_GLOBAL_LIGHTS] = {};
	RefRenderObjListClass lights;
	WorldHeightMap *map = nullptr;
	HeightMapRenderObjClass *terrain = nullptr;
	WaterRenderObjClass *water = nullptr;
	std::vector<MapEditorEngine::MapEntry> entries;
	MeCamera cam = {0, 0, 0, 0, 0.6f, 800.0f};
	MeCamera startCam = cam;
	bool roadsPending = false;
	bool shroudBorderPending = false;	// device reset or new map: the shroud texture starts black
};

ViewState *s_view = nullptr;

// What the view draws; kept across maps and devices.
MeDisplayOptions s_display = {1, 1, 1, 1, "", 1, 0, 0, 0, 0};
// Clock of WW3D while the animations stand still (meViewFreezeTime); 0 runs with the real time.
DWORD s_frozenClock = 0;
// The part of the picture the camera draws (meViewSetTile), in fractions of the whole view.
struct Tile
{
	float left = 0.0f;
	float top = 0.0f;
	float right = 1.0f;
	float bottom = 1.0f;

	bool whole() const { return left == 0.0f && top == 0.0f && right == 1.0f && bottom == 1.0f; }
};
Tile s_tile;
// The game data's value, restored when the three-way blends are no longer highlighted.
Int s_gameThreeWayBlends = -1;

// The graphics of a detail level, as GameLODManager::applyStaticLODLevel and W3DDisplay::draw set them
// in the game. Values a list leaves out keep the game's defaults.
void applyGraphics()
{
	struct Defaults
	{
		Real sharpen;
		Int shadowMapSize;
		Real shadowTerrain;
		Real terrainDetail;
		Real terrainRock;
	};
	static const Defaults defaults{TheGlobalData->m_renderEffectSharpen, TheGlobalData->m_renderEffectShadowMapSize,
		TheGlobalData->m_renderEffectShadowTerrain, TheGlobalData->m_renderEffectTerrainDetail, TheGlobalData->m_renderEffectTerrainRock};
	UnsignedInt flags = 0;
	Real sharpen = defaults.sharpen;
	Int msaa = -1;
	Int shadowMapSize = defaults.shadowMapSize;
	Real shadowTerrain = defaults.shadowTerrain;
	Real terrainDetail = defaults.terrainDetail;
	Real terrainRock = defaults.terrainRock;
	GlobalData::parseRenderEffects(s_display.effects, &flags, &sharpen, &msaa, 0, &shadowMapSize, &shadowTerrain, &terrainDetail, &terrainRock);
	TheWritableGlobalData->m_renderEffects = flags;
	TheWritableGlobalData->m_renderEffectSharpen = sharpen;
	TheWritableGlobalData->m_renderEffectShadowMapSize = shadowMapSize;
	TheWritableGlobalData->m_renderEffectShadowTerrain = shadowTerrain;
	TheWritableGlobalData->m_renderEffectTerrainDetail = terrainDetail;
	TheWritableGlobalData->m_renderEffectTerrainRock = terrainRock;
	TheWritableGlobalData->m_waterQuality = s_display.waterQuality;
	TheWritableGlobalData->m_terrainAnisotropy = s_display.terrainAnisotropy;
	TheWritableGlobalData->m_editorGameGraphics = TRUE;
	RenderEffectSettings effects;
	effects.flags = flags;
	effects.sharpen = sharpen;
	effects.shadow_map_size = static_cast<unsigned>(shadowMapSize);
	effects.shadow_terrain_strength = shadowTerrain;
	effects.terrain_detail = terrainDetail;
	effects.terrain_anisotropy = static_cast<unsigned>(s_display.terrainAnisotropy);
	effects.terrain_rock = terrainRock;
	RenderDevice::Set_Effect_Settings(effects);
}

void applyDisplayFlags()
{
	TheWritableGlobalData->m_useShadowVolumes = s_display.shadows != 0;
	TheWritableGlobalData->m_useShadowDecals = s_display.shadows != 0;
	TheWritableGlobalData->m_useCloudMap = s_display.clouds != 0;
	TheWritableGlobalData->m_useLightMap = s_display.macroTexture != 0;
	TheWritableGlobalData->m_showSoftWaterEdge = s_display.softWater != 0;
	if (s_gameThreeWayBlends < 0)
		s_gameThreeWayBlends = TheGlobalData->m_use3WayTerrainBlends;
	TheWritableGlobalData->m_use3WayTerrainBlends = s_display.extraBlends ? 2 : s_gameThreeWayBlends;
}

float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

void applyLights()
{
	ViewState &v = *s_view;
	TheWritableGlobalData->setTimeOfDay(TheGlobalData->m_timeOfDay);
	const GlobalData::TerrainLighting *ol = &TheGlobalData->m_terrainObjectsLighting[TheGlobalData->m_timeOfDay][0];
	v.scene->Set_Ambient_Light(Vector3(ol->ambient.red, ol->ambient.green, ol->ambient.blue));
	for (Int i = 0; i < MAX_GLOBAL_LIGHTS; i++)
	{
		ol = &TheGlobalData->m_terrainObjectsLighting[TheGlobalData->m_timeOfDay][i];
		v.globalLights[i]->Set_Ambient(Vector3(0.0f, 0.0f, 0.0f));
		v.globalLights[i]->Set_Diffuse(Vector3(ol->diffuse.red, ol->diffuse.green, ol->diffuse.blue));
		v.globalLights[i]->Set_Specular(Vector3(0, 0, 0));
		Matrix3D mtx;
		mtx.Set(Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(ol->lightPos.x, ol->lightPos.y, ol->lightPos.z), Vector3(0, 0, 0));
		v.globalLights[i]->Set_Transform(mtx);
		v.scene->setGlobalLight(v.globalLights[i], i);
	}
	if (TheW3DShadowManager)
		TheW3DShadowManager->setTimeOfDay(TheGlobalData->m_timeOfDay);
	if (v.terrain)
		v.terrain->setTimeOfDay(TheGlobalData->m_timeOfDay);
	if (v.water)
		v.water->setTimeOfDay(TheGlobalData->m_timeOfDay);
}

// The water object of the game (W3DTerrainVisual::init): standing water from the water areas of the
// map, rivers, the optional water grid and the sea of water type 2. It must exist before the terrain
// is built, which asks it for the water height to find the shore tiles.
void createWater()
{
	ViewState &v = *s_view;
	const GlobalData &gd = *TheGlobalData;
	// The water object takes the textures of every time of day from Water.ini without checking.
	bool complete = TheWaterTransparency != nullptr;
	for (Int t = TIME_OF_DAY_FIRST; t < TIME_OF_DAY_COUNT; t++)
		complete = complete && !WaterSettings[t].m_waterTextureFile.isEmpty() && !WaterSettings[t].m_skyTextureFile.isEmpty();
	if (!complete)
	{
		MapEditorEngine::report("view: no water (Water.ini lacks WaterSet or WaterTransparency)");
		return;
	}
	v.water = NEW_REF(WaterRenderObjClass, ());
	v.water->init(gd.m_waterPositionZ, gd.m_waterExtentX, gd.m_waterExtentY, v.scene,
		static_cast<WaterRenderObjClass::WaterType>(gd.m_waterType));
	v.water->Set_Position(Vector3(gd.m_waterPositionX, gd.m_waterPositionY, gd.m_waterPositionZ));
	v.water->toggleCloudLayer(gd.m_useCloudPlane);
	v.water->setGridHeightClamps(gd.m_vertexWaterHeightClampLow[0], gd.m_vertexWaterHeightClampHi[0]);
	v.water->setGridTransform(gd.m_vertexWaterAngle[0], gd.m_vertexWaterXPosition[0], gd.m_vertexWaterYPosition[0], gd.m_vertexWaterZPosition[0]);
	v.water->setGridResolution(gd.m_vertexWaterXGridCells[0], gd.m_vertexWaterYGridCells[0], gd.m_vertexWaterGridSize[0]);
	v.water->setGridChangeAttenuationFactors(gd.m_vertexWaterAttenuationA[0], gd.m_vertexWaterAttenuationB[0],
		gd.m_vertexWaterAttenuationC[0], gd.m_vertexWaterAttenuationRange[0]);
	::Engine::context().waterRenderObj = v.water;
}

void releaseWater()
{
	ViewState &v = *s_view;
	if (!v.water)
		return;
	if (v.water->Peek_Scene())
		v.water->Peek_Scene()->Remove_Render_Object(v.water);
	if (TheWaterRenderObj == v.water)
		::Engine::context().waterRenderObj = nullptr;
	REF_PTR_RELEASE(v.water);
}

void applyCamera()
{
	ViewState &v = *s_view;
	const MeCamera &c = v.cam;
	const float cosPitch = std::cos(c.pitch);
	const Vector3 target(c.targetX, c.targetY, c.targetZ);
	const Vector3 source(c.targetX - std::sin(c.yaw) * cosPitch * c.distance,
		c.targetY - std::cos(c.yaw) * cosPitch * c.distance,
		c.targetZ + std::sin(c.pitch) * c.distance);
	Matrix3D transform(1);
	transform.Look_At(source, target, 0);
	v.camera->Set_Transform(transform);
	const float nearZ = std::max(1.0f, c.distance / 200.0f);
	v.camera->Set_Clip_Planes(nearZ, c.distance * 3.0f + 4000.0f);
	const float aspect = v.height > 0 ? static_cast<float>(v.width) / static_cast<float>(v.height) : 1.0f;
	v.camera->Set_View_Plane(HORIZONTAL_FOV, 2.0f * std::atan(std::tan(HORIZONTAL_FOV * 0.5f) / aspect));
	// One tile of a poster: the part of the view plane that tile covers.
	if (!s_tile.whole())
	{
		Vector2 lo;
		Vector2 hi;
		v.camera->Get_View_Plane(lo, hi);
		const float w = hi.X - lo.X;
		const float h = hi.Y - lo.Y;
		v.camera->Set_View_Plane(Vector2(lo.X + w * s_tile.left, hi.Y - h * s_tile.bottom), Vector2(lo.X + w * s_tile.right, hi.Y - h * s_tile.top));
	}
}

// Start camera of the map: the waypoint InitialCameraPosition if there is one, else the map centre;
// pitch and height as the game's default camera.
void computeStartCamera()
{
	ViewState &v = *s_view;
	const Int border = v.map->getBorderSize();
	Coord3D pos;
	pos.x = MAP_XY_FACTOR * (v.map->getXExtent() - 2 * border) / 2.0f;
	pos.y = MAP_XY_FACTOR * (v.map->getYExtent() - 2 * border) / 2.0f;
	pos.z = 0;
	const AsciiString startName = TheNameKeyGenerator->keyToName(TheKey_InitialCameraPosition);
	for (MapObject *o = MapObject::getFirstMapObject(); o; o = o->getNext())
	{
		if (o->isWaypoint() && startName == o->getWaypointName())
			pos = *o->getLocation();
	}
	MeCamera c;
	c.targetX = pos.x;
	c.targetY = pos.y;
	c.targetZ = v.terrain ? v.terrain->getHeightMapHeight(pos.x, pos.y, nullptr) : 0.0f;
	c.yaw = TheGlobalData->m_cameraYaw * PI / 180.0f;
	c.pitch = clampf(TheGlobalData->m_cameraPitch * PI / 180.0f, 0.2f, 1.55f);
	const float height = TheGlobalData->m_maxCameraHeight > 0 ? TheGlobalData->m_maxCameraHeight : 300.0f;
	c.distance = height / std::sin(c.pitch);
	v.startCam = c;
	v.cam = c;
}

void releaseMap()
{
	ViewState &v = *s_view;
	MapEditorEngine::releaseMapObjects(v.scene, v.terrain, v.entries);
	MapEditorEngine::clearOverlay();
	if (v.water && v.water->Peek_Scene())
	{
		v.scene->Remove_Render_Object(v.water);
		v.water->reset();
	}
	if (v.terrain)
	{
		v.scene->Remove_Render_Object(v.terrain);
		REF_PTR_RELEASE(v.terrain);
	}
	REF_PTR_RELEASE(v.map);
	if (TheW3DShadowManager)
		TheW3DShadowManager->removeAllShadows();
	// The logical read fills the engine's global lists; they belong to the loaded map.
	WorldHeightMap::freeListOfMapObjects();
	PolygonTrigger::deleteTriggers();
}

} // namespace

namespace MapEditorEngine
{

bool viewReady()
{
	return s_view && s_view->deviceReady;
}

void viewSize(int &width, int &height)
{
	width = s_view ? s_view->width : 0;
	height = s_view ? s_view->height : 0;
}

bool terrainHeightAt(float x, float y, float *z)
{
	if (!s_view || !s_view->terrain)
		return false;
	if (z)
		*z = s_view->terrain->getHeightMapHeight(x, y, nullptr);
	return true;
}

void shutdownView()
{
	if (!s_view)
		return;
	ViewState &v = *s_view;
	stopWindows();
	stopParticles();
	releaseMap();
	releaseWater();
	v.lights.Reset_List();
	if (TheW3DShadowManager)
	{
		TheW3DShadowManager->removeAllShadows();
		delete TheW3DShadowManager;
		::Engine::context().w3DShadowManager = nullptr;
	}
	W3DShaderManager::shutdown();
	if (v.assets)
	{
		PredictiveLODOptimizerClass::Free();
		v.assets->Free_Assets();
		delete v.assets;
		v.assets = nullptr;
	}
	REF_PTR_RELEASE(v.scene);
	REF_PTR_RELEASE(v.camera);
	for (LightClass *&light : v.globalLights)
		REF_PTR_RELEASE(light);
	if (v.deviceReady)
	{
		Debug_Statistics::Shutdown_Statistics();
		WW3D::Shutdown();
		WWMath::Shutdown();
	}
	if (TheTacticalView == &v.tacticalView)
		::Engine::context().tacticalView = nullptr;
	delete s_view;
	s_view = nullptr;
}

} // namespace MapEditorEngine

namespace {

// The editors take -vulkan on their own command line like the game; the engine DLL reads it from the
// process, so the DLL interface stays as it is.
bool vulkanRequested()
{
	const char *line = ::GetCommandLineA();
	bool vulkan = false;
	while (line && *line)
	{
		while (*line == ' ' || *line == '\t')
			++line;
		const char *start = line;
		while (*line && *line != ' ' && *line != '\t')
			++line;
		const size_t length = static_cast<size_t>(line - start);
		if ((length == 7 && _strnicmp(start, "-vulkan", 7) == 0) || (length == 8 && _strnicmp(start, "--vulkan", 8) == 0))
			vulkan = true;
		else if ((length == 6 && _strnicmp(start, "-d3d11", 6) == 0) || (length == 7 && _strnicmp(start, "--d3d11", 7) == 0))
			vulkan = false;
	}
	return vulkan;
}

} // namespace

MEAPI int32_t meViewAttach(void *hwnd, int32_t width, int32_t height, char *error, int32_t errorSize)
{
	using MapEditorEngine::setError;
	if (!MapEditorEngine::engineStarted())
	{
		setError(error, errorSize, "engine not started");
		return 0;
	}
	if (s_view)
		return 1;
	s_view = new ViewState;
	ViewState &v = *s_view;
	v.width = std::max(1, static_cast<int>(width));
	v.height = std::max(1, static_cast<int>(height));
	::Engine::context().tacticalView = &v.tacticalView;
	v.tacticalView.setWidth(v.width);
	v.tacticalView.setHeight(v.height);
	v.tacticalView.setOrigin(0, 0);
	ApplicationHWnd = static_cast<HWND>(hwnd);

	WWMath::Init();
	WW3D::Set_Prelit_Mode(WW3D::PRELIT_MODE_VERTEX);
	v.assets = new W3DAssetManager;
	v.assets->Register_Prototype_Loader(&_ParticleEmitterLoader);
	v.assets->Register_Prototype_Loader(&_AggregateLoader);
	v.assets->Set_WW3D_Load_On_Demand(true);
	RenderDevice::Set_Backend_Request(vulkanRequested() ? "vulkan" : nullptr);
	if (WW3D::Init(hwnd) != WW3D_ERROR_OK)
	{
		setError(error, errorSize, "WW3D::Init failed");
		MapEditorEngine::shutdownView();
		return 0;
	}
	v.deviceReady = true;
	WW3D::Set_Prelit_Mode(WW3D::PRELIT_MODE_LIGHTMAP_MULTI_PASS);
	WW3D::Set_Collision_Box_Display_Mask(0x00);
	if (WW3D::Set_Render_Device(0, v.width, v.height, 32, true, true) != WW3D_ERROR_OK || !RenderDevice::Has_Device())
	{
		char text[200];
		std::snprintf(text, sizeof(text), "no %s device", RenderDevice::Get_Backend_Title());
		setError(error, errorSize, RenderDevice::Is_Backend("d3d11") ? "no Direct3D 11 device (feature level 11_1 needed)" : text);
		MapEditorEngine::shutdownView();
		return 0;
	}
	WW3D::Enable_Static_Sort_Lists(true);
	WW3D::Set_Screen_UV_Bias(TRUE);
	W3DShaderManager::init();

	v.scene = NEW_REF(RTS3DScene, ());
	v.scene->Set_Ambient_Light(Vector3(0.5f, 0.5f, 0.5f));
	v.camera = NEW_REF(CameraClass, ());
	for (LightClass *&light : v.globalLights)
		light = NEW_REF(LightClass, (LightClass::DIRECTIONAL));

	applyDisplayFlags();
	if (TheW3DShadowManager == nullptr)
	{
		::Engine::context().w3DShadowManager = new W3DShadowManager;
		TheW3DShadowManager->init();
	}
	createWater();
	applyLights();
	char text[600];
	if (vulkanRequested() && !RenderDevice::Is_Backend("vulkan"))
		std::snprintf(text, sizeof(text), "view: Vulkan not used (%s), %s device ready", RenderDevice::Get_Backend_Fallback_Reason(),
			RenderDevice::Get_Backend_Title());
	else
		std::snprintf(text, sizeof(text), "view: %s device ready", RenderDevice::Get_Backend_Title());
	MapEditorEngine::report(text);
	return 1;
}

MEAPI void meViewDetach(void)
{
	MapEditorEngine::shutdownView();
}

MEAPI void meViewResize(int32_t width, int32_t height)
{
	if (!s_view || width <= 0 || height <= 0)
		return;
	ViewState &v = *s_view;
	if (v.width == width && v.height == height)
		return;
	v.width = width;
	v.height = height;
	v.tacticalView.setWidth(width);
	v.tacticalView.setHeight(height);
	if (v.deviceReady)
		WW3D::Set_Device_Resolution(width, height, true);
	v.shroudBorderPending = true;
}

MEAPI int32_t meViewLoadMap(const uint8_t *chunks, int32_t size, MeMapInfo *info, char *error, int32_t errorSize)
{
	using MapEditorEngine::setError;
	if (!s_view || !s_view->deviceReady)
	{
		setError(error, errorSize, "no 3D view");
		return 0;
	}
	ViewState &v = *s_view;
	releaseMap();
	try
	{
		// Logical data first (map objects, waypoints, sides), then the graphical data.
		// A map the engine cannot read sets isLoadFailed instead of throwing.
		MapEditorEngine::MemoryChunkStream logical(chunks, size);
		WorldHeightMap *logicalMap = NEW WorldHeightMap(&logical, true);
		const bool logicalFailed = logicalMap->isLoadFailed();
		REF_PTR_RELEASE(logicalMap);
		if (logicalFailed)
			throw 0;
		MapEditorEngine::report("map: objects, waypoints and sides read");
		MapEditorEngine::restoreDefaultLighting();
		MapEditorEngine::MemoryChunkStream graphical(chunks, size);
		v.map = NEW WorldHeightMap(&graphical, false);
		if (v.map->isLoadFailed())
			throw 0;
		MapEditorEngine::report("map: heights, textures and lighting read");
	}
	catch (...)
	{
		setError(error, errorSize, "the engine could not read the map");
		REF_PTR_RELEASE(v.map);
		return 0;
	}

	v.terrain = NEW_REF(HeightMapRenderObjClass, ());
	v.scene->Add_Render_Object(v.terrain);
	applyLights();
	RefRenderObjListIterator lightIt(&v.lights);
	v.map->setDrawOrg(0, 0);
	v.map->setDrawWidth(v.map->getXExtent());
	v.map->setDrawHeight(v.map->getYExtent());
	v.terrain->initHeightData(v.map->getXExtent(), v.map->getYExtent(), v.map, &lightIt);
	MapEditorEngine::terrainHeightsChanged();
	v.terrain->updateViewImpassableAreas();
	if (s_display.macroTextureName[0])
		v.terrain->updateMacroTexture(AsciiString(s_display.macroTextureName));
	if (v.water)
	{
		v.scene->Add_Render_Object(v.water);
		v.water->enableWaterGrid(false);
		v.water->updateMapOverrides();
		v.water->load();
	}
	v.shroudBorderPending = true;
	MapEditorEngine::report("map: terrain built");
	v.roadsPending = true;
	computeStartCamera();
	const MapEditorEngine::ObjectCounts objects = MapEditorEngine::buildMapObjects(v.scene, v.assets, v.terrain, v.entries, s_display.models != 0, s_display.garrisoned != 0);

	if (info)
	{
		const Int border = v.map->getBorderSize();
		info->cellsX = v.map->getXExtent();
		info->cellsY = v.map->getYExtent();
		info->border = border;
		info->worldWidth = MAP_XY_FACTOR * (v.map->getXExtent() - 2 * border);
		info->worldHeight = MAP_XY_FACTOR * (v.map->getYExtent() - 2 * border);
	}
	char line[256];
	std::snprintf(line, sizeof(line), "map: %d x %d cells, border %d", v.map->getXExtent(), v.map->getYExtent(), v.map->getBorderSize());
	MapEditorEngine::report(line);
	std::snprintf(line, sizeof(line), "map: %d models, %d shadows, %d trees, %d scorch marks in %.0f ms; %d without model, %d models not found, %d unknown templates",
		objects.models, objects.shadows, objects.trees, objects.scorches, objects.milliseconds, objects.withoutModel, objects.missingModel, objects.unknown);
	MapEditorEngine::report(line);
	int lakes = 0;
	int rivers = 0;
	for (const PolygonTrigger *p = PolygonTrigger::getFirstPolygonTrigger(); p; p = p->getNext())
	{
		if (p->isWaterArea())
			++(p->isRiver() ? rivers : lakes);
	}
	std::snprintf(line, sizeof(line), "map: water type %d, %d standing water areas, %d rivers%s",
		static_cast<int>(TheGlobalData->m_waterType), lakes, rivers, v.water ? "" : " (not drawn: no water settings)");
	MapEditorEngine::report(line);
	if (info)
	{
		info->models = objects.models + objects.trees;
		info->missingModels = objects.missingModel + objects.unknown;
	}
	const GlobalData::TerrainLighting &sun = TheGlobalData->m_terrainLighting[TheGlobalData->m_timeOfDay][0];
	TextureClass *terrainTexture = v.map->getTerrainTexture();
	std::snprintf(line, sizeof(line), "map: time of day %d, sun ambient %.2f %.2f %.2f, diffuse %.2f %.2f %.2f, terrain texture %s",
		static_cast<int>(TheGlobalData->m_timeOfDay), sun.ambient.red, sun.ambient.green, sun.ambient.blue,
		sun.diffuse.red, sun.diffuse.green, sun.diffuse.blue, terrainTexture ? "ready" : "missing");
	MapEditorEngine::report(line);
	return 1;
}

MEAPI void meViewUnloadMap(void)
{
	if (s_view)
		releaseMap();
}

MEAPI void meViewGetCamera(MeCamera *camera)
{
	if (s_view && camera)
		*camera = s_view->cam;
}

MEAPI void meViewSetCamera(const MeCamera *camera)
{
	if (!s_view || !camera)
		return;
	MeCamera c = *camera;
	c.pitch = clampf(c.pitch, 0.05f, 1.5607f);
	c.distance = clampf(c.distance, 20.0f, 20000.0f);
	s_view->cam = c;
}

MEAPI void meViewResetCamera(void)
{
	if (s_view)
		s_view->cam = s_view->startCam;
}

namespace
{

// Ray through the window pixel (x, y): start at the camera, unit direction, useful length.
void screenRay(int32_t x, int32_t y, Vector3 &from, Vector3 &dir, float &length)
{
	ViewState &v = *s_view;
	applyCamera();
	const float logX = static_cast<float>(x) / static_cast<float>(v.width);
	const float logY = static_cast<float>(y) / static_cast<float>(v.height);
	const Matrix3D m = v.camera->Get_Transform();
	Vector2 vmin, vmax;
	v.camera->Get_View_Plane(vmin, vmax);
	const float xmod = (logX - 0.5f) * (vmax.X - vmin.X);
	const float ymod = (0.5f - logY) * (vmax.Y - vmin.Y);
	// The camera looks along its local -Z.
	dir = Vector3(-m[0][2] + xmod * m[0][0] + ymod * m[0][1],
		-m[1][2] + xmod * m[1][0] + ymod * m[1][1],
		-m[2][2] + xmod * m[2][0] + ymod * m[2][1]);
	dir.Normalize();
	from = v.camera->Get_Position();
	length = v.cam.distance * 4.0f + 8000.0f;
}

// Distance along the ray to the terrain, or a negative value.
float terrainHit(const Vector3 &from, const Vector3 &dir, float length, Vector3 *point)
{
	LineSegClass ray(from, from + dir * length);
	CastResultStruct result;
	RayCollisionTestClass test(ray, &result);
	if (!s_view->terrain->Cast_Ray(test))
		return -1.0f;
	if (point)
		*point = result.ContactPoint;
	return (result.ContactPoint - from).Length();
}

} // namespace

MEAPI int32_t meViewPick(int32_t x, int32_t y, float *world3)
{
	if (!s_view || !s_view->terrain || s_view->width <= 0 || s_view->height <= 0)
		return 0;
	Vector3 from, dir, point;
	float length = 0.0f;
	screenRay(x, y, from, dir, length);
	if (terrainHit(from, dir, length, &point) < 0.0f)
		return 0;
	if (world3)
	{
		world3[0] = point.X;
		world3[1] = point.Y;
		world3[2] = point.Z;
	}
	return 1;
}

MEAPI int32_t meViewPickObject(int32_t x, int32_t y, MePickResult *result)
{
	if (!s_view || !s_view->terrain || s_view->width <= 0 || s_view->height <= 0)
		return 0;
	ViewState &v = *s_view;
	Vector3 from, dir;
	float length = 0.0f;
	screenRay(x, y, from, dir, length);
	// Objects behind a hill stay hidden; a little slack keeps objects sunk into the ground pickable.
	const float ground = terrainHit(from, dir, length, nullptr);
	const float maxDistance = ground >= 0.0f ? ground + 20.0f : length;
	const float perPixel = 2.0f * std::tan(HORIZONTAL_FOV * 0.5f) / static_cast<float>(std::max(1, v.width));
	return MapEditorEngine::pickMapObject(v.entries, v.terrain, from, dir, maxDistance, perPixel, result) ? 1 : 0;
}

MEAPI void meViewMoveObject(int32_t object, float x, float y, float z, float angle)
{
	if (s_view && s_view->terrain)
		MapEditorEngine::moveMapObject(s_view->entries, s_view->terrain, object, x, y, z, angle);
}

MEAPI int32_t meViewObjectInfo(int32_t object, MePickResult *result)
{
	if (!s_view || !s_view->terrain)
		return 0;
	return MapEditorEngine::mapObjectInfo(s_view->entries, s_view->terrain, object, result) ? 1 : 0;
}

MEAPI int32_t meViewObjectsInRect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t *objects, int32_t maxObjects)
{
	if (!s_view || !s_view->terrain)
		return 0;
	ViewState &v = *s_view;
	applyCamera();
	return MapEditorEngine::objectsInRect(v.entries, v.terrain, v.camera, v.width, v.height, x0, y0, x1, y1, objects, maxObjects);
}

MEAPI void meViewSetMarquee(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	MapEditorEngine::setMarquee(x0, y0, x1, y1);
}

MEAPI int32_t meViewReloadObjects(const uint8_t *chunks, int32_t size)
{
	if (!s_view || !s_view->terrain)
		return 0;
	ViewState &v = *s_view;
	MapEditorEngine::releaseMapObjects(v.scene, v.terrain, v.entries);
	// The logical read fills the engine's global lists anew (objects, waypoints, areas, sides).
	WorldHeightMap::freeListOfMapObjects();
	PolygonTrigger::deleteTriggers();
	try
	{
		MapEditorEngine::MemoryChunkStream logical(chunks, size);
		WorldHeightMap *logicalMap = NEW WorldHeightMap(&logical, true);
		const bool failed = logicalMap->isLoadFailed();
		REF_PTR_RELEASE(logicalMap);
		if (failed)
			throw 0;
	}
	catch (...)
	{
		MapEditorEngine::report("map: objects could not be read again");
		return 0;
	}
	const MapEditorEngine::ObjectCounts objects = MapEditorEngine::buildMapObjects(v.scene, v.assets, v.terrain, v.entries, s_display.models != 0, s_display.garrisoned != 0);
	// Roads and bridges are built from the road points of the object list, on the next frame.
	v.roadsPending = true;
	char line[160];
	std::snprintf(line, sizeof(line), "map: objects rebuilt, %d models, %d trees in %.0f ms",
		objects.models, objects.trees, objects.milliseconds);
	MapEditorEngine::report(line);
	return 1;
}

MEAPI void meViewRender(void)
{
	if (!s_view || !s_view->deviceReady)
		return;
	ViewState &v = *s_view;
	applyCamera();
	if (v.terrain)
	{
		if (v.roadsPending)
		{
			v.terrain->loadRoadsAndBridges(nullptr, FALSE);
			v.roadsPending = false;
			MapEditorEngine::report("map: roads and bridges loaded");
		}
		RefRenderObjListIterator lightIt(&v.lights);
		v.terrain->updateCenter(v.camera, &lightIt);
		v.terrain->On_Frame_Update();
		// The scene always blends the shroud over the terrain, and the game uncovers it through its
		// logic; the editor shows the whole map, border included. A device reset (resize) creates
		// the shroud anew, so it is uncovered every frame (a few thousand cells) and copied into its
		// texture as W3DDisplay does.
		if (W3DShroud *shroud = v.terrain->getShroud())
		{
			if (v.shroudBorderPending)
			{
				shroud->setBorderShroudLevel(255);
				v.shroudBorderPending = false;
			}
			shroud->fillShroudData(255);
			shroud->render(v.camera);
		}
	}
	MapEditorEngine::stepParticles();
	WW3D::Sync(s_frozenClock ? s_frozenClock : GetTickCount());
	if (v.water && v.terrain)
	{
		v.water->update();
		if (TheGlobalData->m_waterType == WaterRenderObjClass::WATER_TYPE_2_PVSHADER)
			v.water->updateRenderTargetTextures(v.camera);
	}
	const float opacity = TheWaterTransparency ? TheWaterTransparency->m_minWaterOpacity : 1.0f;
	if (WW3D::Begin_Render(true, true, Vector3(0.12f, 0.13f, 0.15f), opacity) == WW3D_ERROR_OK)
	{
		if (v.terrain)
			v.terrain->doTextures(true);
		// Triangle edges as the scene's extra pass over the textured frame.
		v.scene->Set_Extra_Pass_Polygon_Mode(s_display.wireframe ? SceneClass::EXTRA_PASS_LINE : SceneClass::EXTRA_PASS_DISABLE);
		{
			// The scene draws the particles while flushing (RTS3DScene::Flush), after the translucent objects.
			MapEditorEngine::ParticleRenderScope particles(v.assets);
			WW3D::Render(v.scene, v.camera);
		}
		// Post-processing of the chosen detail level (antialiasing, sharpening, colour grading), as
		// W3DView::draw does it after the 3D scene; the editor's overlay comes after it, unfiltered.
		RenderDevice::Scene_3D_Done(0, 0, v.width, v.height);
		if (v.terrain)
		{
			// Markings keep their size in pixels on a part of the view (posters).
			const float perPixel = 2.0f * std::tan(HORIZONTAL_FOV * 0.5f) * (s_tile.right - s_tile.left) / static_cast<float>(std::max(1, v.width));
			MapEditorEngine::renderOverlay(v.camera, v.terrain, perPixel);
		}
		if (s_display.letterbox)
			MapEditorEngine::renderLetterbox(v.width, v.height);
		MapEditorEngine::renderMarquee(v.width, v.height);
		WW3D::End_Render();
	}
}

MEAPI void meViewCapture(const wchar_t *bmpPath)
{
	if (!s_view || !bmpPath)
		return;
	char ansi[MAX_PATH * 2] = {};
	WideCharToMultiByte(CP_ACP, 0, bmpPath, -1, ansi, sizeof(ansi), nullptr, nullptr);
	RenderDevice::Request_Back_Buffer_Capture(ansi);
}

MEAPI void meViewSetHeights(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t *heights, int32_t width)
{
	if (!s_view || !s_view->map || !s_view->terrain || !heights)
		return;
	ViewState &v = *s_view;
	const Int w = v.map->getXExtent();
	const Int h = v.map->getYExtent();
	if (width != w)
		return;
	x0 = std::max<int32_t>(0, x0);
	y0 = std::max<int32_t>(0, y0);
	x1 = std::min<int32_t>(w - 1, x1);
	y1 = std::min<int32_t>(h - 1, y1);
	if (x1 < x0 || y1 < y0)
		return;
	UnsignedByte *data = v.map->getDataPtr();
	for (Int y = y0; y <= y1; ++y)
		std::memcpy(data + y * w + x0, heights + y * w + x0, static_cast<std::size_t>(x1 - x0 + 1));
	// One cell more on each side: the normals and lighting of the neighbours change too.
	IRegion2D range;
	range.lo.x = x0 - 1;
	range.lo.y = y0 - 1;
	range.hi.x = x1 + 1;
	range.hi.y = y1 + 1;
	RefRenderObjListIterator lightIt(&v.lights);
	v.terrain->doPartialUpdate(range, v.map, &lightIt);
	MapEditorEngine::terrainHeightsChanged();
}

namespace
{

// The cell index arrays of the view's height map are protected; the editor writes them as the old
// WorldBuilder's WorldHeightMapEdit does.
class TileArrays : public WorldHeightMap
{
public:
	static constexpr Short *WorldHeightMap::*TILES = &TileArrays::m_tileNdxes;
	static constexpr Short *WorldHeightMap::*BLENDS = &TileArrays::m_blendTileNdxes;
	static constexpr Short *WorldHeightMap::*EXTRA = &TileArrays::m_extraBlendTileNdxes;
	static constexpr Short *WorldHeightMap::*CLIFFS = &TileArrays::m_cliffInfoNdxes;
	static constexpr TBlendTileInfo (WorldHeightMap::*BLENDED)[NUM_BLEND_TILES] = &TileArrays::m_blendedTiles;
	static constexpr Int WorldHeightMap::*NUM_BLENDED = &TileArrays::m_numBlendedTiles;
	static constexpr void (WorldHeightMap::*SET_CLIFF)(Int, Int, Bool) = &TileArrays::setCliffState;
};

} // namespace

MEAPI void meViewSetTiles(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const int16_t *tiles, const int16_t *blends,
	const int16_t *extraBlends, const int16_t *cliffInfos, int32_t width)
{
	if (!s_view || !s_view->map || !s_view->terrain || !tiles || !blends || !extraBlends || !cliffInfos)
		return;
	ViewState &v = *s_view;
	WorldHeightMap *map = v.map;
	const Int w = map->getXExtent();
	const Int h = map->getYExtent();
	if (width != w)
		return;
	x0 = std::max<int32_t>(0, x0);
	y0 = std::max<int32_t>(0, y0);
	x1 = std::min<int32_t>(w - 1, x1);
	y1 = std::min<int32_t>(h - 1, y1);
	if (x1 < x0 || y1 < y0)
		return;
	const std::pair<Short *WorldHeightMap::*, const int16_t *> arrays[] = {{TileArrays::TILES, tiles}, {TileArrays::BLENDS, blends},
		{TileArrays::EXTRA, extraBlends}, {TileArrays::CLIFFS, cliffInfos}};
	for (const auto &[member, source] : arrays)
	{
		Short *target = map->*member;
		if (!target)
			continue;
		for (Int y = y0; y <= y1; ++y)
			std::memcpy(target + y * w + x0, source + y * w + x0, static_cast<std::size_t>(x1 - x0 + 1) * sizeof(Short));
	}
	IRegion2D range;
	range.lo.x = x0 - 1;
	range.lo.y = y0 - 1;
	range.hi.x = x1 + 1;
	range.hi.y = y1 + 1;
	RefRenderObjListIterator lightIt(&v.lights);
	v.terrain->doPartialUpdate(range, v.map, &lightIt);
}

MEAPI void meViewSetBlendTable(const MeBlendTile *entries, int32_t count)
{
	if (!s_view || !s_view->map || count < 0 || count >= NUM_BLEND_TILES || (count > 0 && !entries))
		return;
	WorldHeightMap *map = s_view->map;
	TBlendTileInfo *table = map->*TileArrays::BLENDED;
	for (int32_t i = 0; i < count; ++i)
	{
		TBlendTileInfo &t = table[i + 1];
		t.blendNdx = entries[i].blendIndex;
		t.horiz = entries[i].horizontal;
		t.vert = entries[i].vertical;
		t.rightDiagonal = entries[i].rightDiagonal;
		t.leftDiagonal = entries[i].leftDiagonal;
		t.inverted = entries[i].inverted;
		t.longDiagonal = entries[i].longDiagonal;
		t.customBlendEdgeClass = entries[i].customBlendEdgeClass;
	}
	map->*TileArrays::NUM_BLENDED = count + 1;
}

MEAPI void meViewSetCliffFlags(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t *flags, int32_t width)
{
	if (!s_view || !s_view->map || !s_view->terrain || !flags)
		return;
	ViewState &v = *s_view;
	WorldHeightMap *map = v.map;
	if (width != map->getXExtent())
		return;
	x0 = std::max<int32_t>(0, x0);
	y0 = std::max<int32_t>(0, y0);
	x1 = std::min<int32_t>(map->getXExtent() - 1, x1);
	y1 = std::min<int32_t>(map->getYExtent() - 1, y1);
	const int32_t rowBytes = (width + 7) / 8;
	for (int32_t y = y0; y <= y1; ++y)
		for (int32_t x = x0; x <= x1; ++x)
			(map->*TileArrays::SET_CLIFF)(x, y, (flags[y * rowBytes + (x >> 3)] & (1 << (x & 7))) != 0);
}

namespace
{
// The water look as the game data have it, kept at the first change.
bool s_waterLookKept = false;
MeWaterLook s_gameWaterLook{};

WaterTransparencySetting *writableWaterTransparency()
{
	return TheWaterTransparency ? const_cast<WaterTransparencySetting *>(static_cast<const WaterTransparencySetting *>(TheWaterTransparency)) : nullptr;
}

MeWaterLook waterLookOf(const WaterTransparencySetting &wt)
{
	MeWaterLook look{};
	look.transparentDepth = wt.m_transparentWaterDepth;
	look.minOpacity = wt.m_minWaterOpacity;
	look.standingColor[0] = wt.m_standingWaterColor.red;
	look.standingColor[1] = wt.m_standingWaterColor.green;
	look.standingColor[2] = wt.m_standingWaterColor.blue;
	look.additive = wt.m_additiveBlend ? 1 : 0;
	look.turbidityDepth = wt.m_turbidityDepth;
	look.turbidityColor[0] = wt.m_turbidityColor.red;
	look.turbidityColor[1] = wt.m_turbidityColor.green;
	look.turbidityColor[2] = wt.m_turbidityColor.blue;
	look.refraction = wt.m_refractionStrength;
	look.reflection = wt.m_reflectionStrength;
	return look;
}
} // namespace

MEAPI int32_t meViewGetWaterLook(MeWaterLook *look, int32_t gameData)
{
	const WaterTransparencySetting *wt = writableWaterTransparency();
	if (!look || !wt)
		return 0;
	*look = (gameData && s_waterLookKept) ? s_gameWaterLook : waterLookOf(*wt);
	return 1;
}

MEAPI int32_t meViewSetWaterLook(const MeWaterLook *look)
{
	WaterTransparencySetting *wt = writableWaterTransparency();
	if (!look || !wt)
		return 0;
	if (!s_waterLookKept)
	{
		s_gameWaterLook = waterLookOf(*wt);
		s_waterLookKept = true;
	}
	wt->m_transparentWaterDepth = look->transparentDepth;
	wt->m_minWaterOpacity = look->minOpacity;
	wt->m_standingWaterColor.red = look->standingColor[0];
	wt->m_standingWaterColor.green = look->standingColor[1];
	wt->m_standingWaterColor.blue = look->standingColor[2];
	wt->m_additiveBlend = look->additive != 0;
	wt->m_turbidityDepth = look->turbidityDepth;
	wt->m_turbidityColor.red = look->turbidityColor[0];
	wt->m_turbidityColor.green = look->turbidityColor[1];
	wt->m_turbidityColor.blue = look->turbidityColor[2];
	wt->m_refractionStrength = look->refraction;
	wt->m_reflectionStrength = look->reflection;
	return 1;
}

MEAPI void meViewSetTile(float left, float top, float right, float bottom)
{
	const bool valid = right > left && bottom > top;
	s_tile = valid ? Tile{left, top, right, bottom} : Tile{};
}

MEAPI void meViewFreezeTime(int32_t freeze)
{
	s_frozenClock = freeze ? (s_frozenClock ? s_frozenClock : GetTickCount()) : 0;
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_fixedTimeStep = freeze != 0;
}

MEAPI int32_t meViewSetDisplay(const MeDisplayOptions *options)
{
	if (!options)
		return 0;
	const MeDisplayOptions before = s_display;
	s_display = *options;
	s_display.macroTextureName[sizeof(s_display.macroTextureName) - 1] = 0;
	s_display.effects[sizeof(s_display.effects) - 1] = 0;
	if (!TheWritableGlobalData)
		return 0;
	applyDisplayFlags();
	applyGraphics();
	if (!s_view || !s_view->map || !s_view->terrain)
		return 0;
	ViewState &v = *s_view;
	if (std::strcmp(before.macroTextureName, s_display.macroTextureName) != 0)
		v.terrain->updateMacroTexture(AsciiString(s_display.macroTextureName));
	const Int w = v.map->getXExtent();
	const Int h = v.map->getYExtent();
	// The shore lines exist only while soft water is on.
	if (s_display.softWater && !before.softWater)
		v.terrain->updateShorelineTiles(0, 0, w - 1, h - 1, v.map);
	if ((s_display.extraBlends != 0) != (before.extraBlends != 0))
	{
		IRegion2D range;
		range.lo.x = 0;
		range.lo.y = 0;
		range.hi.x = w;
		range.hi.y = h;
		RefRenderObjListIterator lightIt(&v.lights);
		v.terrain->doPartialUpdate(range, v.map, &lightIt);
	}
	return ((s_display.models != 0) != (before.models != 0) || (s_display.garrisoned != 0) != (before.garrisoned != 0)) ? 1 : 0;
}

MEAPI int32_t meViewSetLighting(const MeLighting *lighting)
{
	if (!lighting || !TheWritableGlobalData || lighting->timeOfDay < TIME_OF_DAY_FIRST || lighting->timeOfDay >= TIME_OF_DAY_COUNT)
		return 0;
	const Int tod = lighting->timeOfDay;
	const auto copy = [](GlobalData::TerrainLighting &to, const MeLight &from) {
		to.ambient.red = from.ambient[0];
		to.ambient.green = from.ambient[1];
		to.ambient.blue = from.ambient[2];
		to.diffuse.red = from.diffuse[0];
		to.diffuse.green = from.diffuse[1];
		to.diffuse.blue = from.diffuse[2];
		to.lightPos.x = from.position[0];
		to.lightPos.y = from.position[1];
		to.lightPos.z = from.position[2];
	};
	for (Int i = 0; i < 3 && i < MAX_GLOBAL_LIGHTS; i++)
	{
		copy(TheWritableGlobalData->m_terrainLighting[tod][i], lighting->terrain[i]);
		copy(TheWritableGlobalData->m_terrainObjectsLighting[tod][i], lighting->objects[i]);
	}
	TheWritableGlobalData->m_timeOfDay = static_cast<TimeOfDay>(tod);
	if (lighting->shadowColor != 0 && TheW3DShadowManager)
		TheW3DShadowManager->setShadowColor(lighting->shadowColor);
	if (!s_view || !s_view->map)
		return 0;
	// Scene lights, sun direction of the shadows, and the prelit terrain and water.
	applyLights();
	return 1;
}

MEAPI void meViewReloadTextures(void)
{
	WW3D::_Invalidate_Textures();
}
