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

// EngineObjects.cpp -- the map objects of the 3D view: a model per object from the object catalog
// (model state from damage, weather and time of day, player colour of the owning side), trees in the
// terrain's tree buffer, scorch marks on the terrain, and the shadow of the template (volume or
// decal; trees bring their own). Follows the old WorldBuilder (WbView3d::invalObjectInView,
// updateTrees, updateScorches). Waypoints, roads, bridges, lights and sound objects draw no model here.
// Every object except road and bridge points gets an entry, so the editor can pick and move it.

#include "Engine/EngineInternal.h"

#include "Common/GlobalData.h"
#include "Common/MapObject.h"
#include "Common/NameKeyGenerator.h"
#include "Common/ModelState.h"
#include "Common/PlayerTemplate.h"
#include "Common/ThingTemplate.h"
#include "Common/WellKnownKeys.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/TerrainLogic.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "W3DDevice/GameClient/Module/W3DTreeDraw.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "aabox.h"
#include "camera.h"
#include "rendobj.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{

// Tree IDs only have to be unique per object; the pointer folded to 32 bits, as the WorldBuilder does.
DrawableID treeId(const MapObject *o)
{
	const UnsignedInt64 p = static_cast<UnsignedInt64>(reinterpret_cast<uintptr_t>(o));
	return static_cast<DrawableID>(static_cast<Int>(static_cast<UnsignedInt>(p ^ (p >> 32))));
}

// Colour of the side that owns the object's team, else white.
Int playerColorOf(MapObject *o)
{
	Bool exists = false;
	const AsciiString teamName = o->getProperties()->getAsciiString(TheKey_originalOwner, &exists);
	return exists ? MapEditorEngine::teamColor(teamName.str()) : 0xFFFFFF;
}

ModelConditionFlags modelState(MapObject *o, bool garrisoned)
{
	ModelConditionFlags state;
	state.clear();
	if (garrisoned)
		state.set(MODELCONDITION_GARRISONED);
	Bool exists = false;
	const Int health = o->getProperties()->getInt(TheKey_objectInitialHealth, &exists);
	const Real ratio = exists ? health / 100.0f : 1.0f;
	if (ratio <= 0.0f)
		state.set(MODELCONDITION_RUBBLE);
	else if (ratio <= TheGlobalData->m_unitReallyDamagedThresh)
		state.set(MODELCONDITION_REALLY_DAMAGED);
	else if (ratio <= TheGlobalData->m_unitDamagedThresh)
		state.set(MODELCONDITION_DAMAGED);

	// Weather and time: 0 follows the map, 1 forces normal, 2 forces snow or night.
	const Int weather = o->getProperties()->getInt(TheKey_objectWeather, &exists);
	if (weather == 2 || (weather == 0 && TheGlobalData->m_weather == WEATHER_SNOWY))
		state.set(MODELCONDITION_SNOW);
	const Int time = o->getProperties()->getInt(TheKey_objectTime, &exists);
	if (time == 2 || (time == 0 && TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT))
		state.set(MODELCONDITION_NIGHT);
	return state;
}

AsciiString modelNameOf(const ThingTemplate *tt, const ModelConditionFlags &state)
{
	const ModuleInfo &mi = tt->getDrawModuleInfo();
	if (mi.getCount() == 0)
		return AsciiString::TheEmptyString;
	const ModuleData *mdd = mi.getNthData(0);
	const W3DModelDrawModuleData *md = mdd ? mdd->getAsW3DModelDrawModuleData() : nullptr;
	return md ? md->getBestModelNameForWB(state) : AsciiString::TheEmptyString;
}

// The shadow the game gives the object (W3DModelDraw); the image is fixed, as nothing moves.
void addShadow(RenderObjClass *model, const ThingTemplate *tt, MapEditorEngine::ObjectCounts &counts)
{
	if (!TheW3DShadowManager || tt->getShadowType() == SHADOW_NONE)
		return;
	Shadow::ShadowTypeInfo info;
	std::memset(&info, 0, sizeof(info));
	info.allowUpdates = FALSE;
	info.allowWorldAlign = TRUE;
	std::snprintf(info.m_ShadowName, sizeof(info.m_ShadowName), "%s", tt->getShadowTextureName().str());
	info.m_type = tt->getShadowType();
	info.m_sizeX = tt->getShadowSizeX();
	info.m_sizeY = tt->getShadowSizeY();
	info.m_offsetX = tt->getShadowOffsetX();
	info.m_offsetY = tt->getShadowOffsetY();
	if (TheW3DShadowManager->addShadow(model, &info))
		++counts.shadows;
}

} // namespace

namespace MapEditorEngine
{

int teamColor(const char *teamName)
{
	TeamsInfo *team = teamName && TheSidesList ? TheSidesList->findTeamInfo(AsciiString(teamName)) : nullptr;
	if (!team)
		return 0xFFFFFF;
	SidesInfo *side = TheSidesList->findSideInfo(team->getDict()->getAsciiString(TheKey_teamOwner));
	if (!side)
		return 0xFFFFFF;
	Bool hasColor = false;
	const Int color = side->getDict()->getInt(TheKey_playerColor, &hasColor);
	if (hasColor)
		return color;
	const PlayerTemplate *pt = ThePlayerTemplateStore
		? ThePlayerTemplateStore->findPlayerTemplate(NAMEKEY(side->getDict()->getAsciiString(TheKey_playerFaction)))
		: nullptr;
	return pt ? pt->getPreferredColor()->getAsInt() : 0xFFFFFF;
}

AsciiString defaultModelName(const ThingTemplate *tt)
{
	if (tt->isKindOf(KINDOF_OPTIMIZED_TREE))
	{
		const ModuleInfo &mi = tt->getDrawModuleInfo();
		const ModuleData *mdd = mi.getCount() > 0 ? mi.getNthData(0) : nullptr;
		if (const W3DTreeDrawModuleData *md = mdd ? mdd->getAsW3DTreeDrawModuleData() : nullptr)
			return md->m_modelName;
	}
	ModelConditionFlags state;
	state.clear();
	return modelNameOf(tt, state);
}

namespace
{

// Index of the object in the editor's model; the editor writes it into the copy the engine reads,
// because the engine skips objects it cannot place and the order alone would not match.
Int editorIndexOf(MapObject *o)
{
	static const NameKeyType key = TheNameKeyGenerator->nameToKey("editorIndex");
	Bool exists = false;
	const Int index = o->getProperties()->getInt(key, &exists);
	return exists ? index : -1;
}

Coord3D groundLocation(const MapObject *o, HeightMapRenderObjClass *terrain)
{
	Coord3D loc = *o->getLocation();
	loc.z += terrain->getHeightMapHeight(loc.x, loc.y, nullptr);
	return loc;
}

void placeModel(MapEntry &e, HeightMapRenderObjClass *terrain)
{
	const Coord3D loc = groundLocation(e.object, terrain);
	Matrix3D transform(true);
	if (e.vehicle)
	{
		// Vehicles tilt with the slope, as in the game; the position stays where the map puts it.
		Coord3D normal;
		terrain->getHeightMapHeight(loc.x, loc.y, &normal);
		makeAlignToNormalMatrix(e.object->getAngle(), loc, normal, transform);
	}
	else
	{
		transform.Translate(loc.x, loc.y, loc.z);
		transform.Rotate_Z(e.object->getAngle());
	}
	e.model->Set_Transform(transform);
}

void addScorchOf(MapObject *o, HeightMapRenderObjClass *terrain)
{
	const Coord3D *loc = o->getLocation();
	const Real radius = o->getProperties()->getReal(TheKey_objectRadius);
	const Scorches type = static_cast<Scorches>(o->getProperties()->getInt(TheKey_scorchType));
	terrain->addScorch(Vector3(loc->x, loc->y, loc->z), radius, type);
}

// Distance along the ray (unit direction) to the sphere, or a negative value.
float raySphere(const Vector3 &from, const Vector3 &dir, const Vector3 &center, float radius)
{
	const Vector3 d = from - center;
	const float b = Vector3::Dot_Product(d, dir);
	const float c = Vector3::Dot_Product(d, d) - radius * radius;
	const float disc = b * b - c;
	if (disc < 0.0f)
		return -1.0f;
	const float t = -b - std::sqrt(disc);
	return t >= 0.0f ? t : (c <= 0.0f ? 0.0f : -1.0f);
}

// Distance along the ray to the box (slab test), or a negative value.
float rayBox(const Vector3 &from, const Vector3 &dir, const AABoxClass &box)
{
	float tMin = 0.0f;
	float tMax = 1.0e30f;
	for (int axis = 0; axis < 3; ++axis)
	{
		const float o = from[axis];
		const float d = dir[axis];
		const float lo = box.Center[axis] - box.Extent[axis];
		const float hi = box.Center[axis] + box.Extent[axis];
		if (std::fabs(d) < 1.0e-6f)
		{
			if (o < lo || o > hi)
				return -1.0f;
			continue;
		}
		float t0 = (lo - o) / d;
		float t1 = (hi - o) / d;
		if (t0 > t1)
			std::swap(t0, t1);
		tMin = std::max(tMin, t0);
		tMax = std::min(tMax, t1);
		if (tMin > tMax)
			return -1.0f;
	}
	return tMin;
}

} // namespace

ObjectCounts buildMapObjects(RTS3DScene *scene, W3DAssetManager *assets, HeightMapRenderObjClass *terrain,
	std::vector<MapEntry> &entries, bool models, bool garrisoned)
{
	ObjectCounts counts;
	const auto start = std::chrono::steady_clock::now();
	terrain->removeAllTrees();
	terrain->removeAllProps();
	terrain->clearAllScorches();
	for (MapObject *o = MapObject::getFirstMapObject(); o; o = o->getNext())
	{
		if (o->getFlags() & (FLAG_ROAD_FLAGS | FLAG_BRIDGE_FLAGS))
			continue;	// the terrain draws roads and bridges
		MapEntry e;
		e.object = o;
		e.editorIndex = editorIndexOf(o);
		if (o->isWaypoint() || o->isLight())
		{
			entries.push_back(e);
			continue;
		}
		if (o->isScorch())
		{
			addScorchOf(o, terrain);
			e.scorch = true;
			entries.push_back(e);
			++counts.scorches;
			continue;
		}
		const ThingTemplate *tt = o->getThingTemplate();
		if (!tt)
		{
			entries.push_back(e);
			++counts.unknown;
			continue;
		}
		if (!models || (o->getFlags() & FLAG_DONT_RENDER))
		{
			entries.push_back(e);
			continue;
		}

		if (tt->isKindOf(KINDOF_OPTIMIZED_TREE))
		{
			const ModuleInfo &mi = tt->getDrawModuleInfo();
			const ModuleData *mdd = mi.getCount() > 0 ? mi.getNthData(0) : nullptr;
			const W3DTreeDrawModuleData *md = mdd ? mdd->getAsW3DTreeDrawModuleData() : nullptr;
			if (md)
			{
				terrain->addTree(treeId(o), groundLocation(o, terrain), tt->getAssetScale(), o->getAngle(), 0.0f, md);
				e.tree = true;
				e.size = 10.0f * tt->getAssetScale();
				entries.push_back(e);
				++counts.trees;
				continue;
			}
		}

		const AsciiString modelName = modelNameOf(tt, modelState(o, garrisoned));
		if (modelName.isEmpty())
		{
			entries.push_back(e);
			++counts.withoutModel;	// sounds, markers and other objects without a model
			continue;
		}
		e.model = assets->Create_Render_Obj(modelName.str(), tt->getAssetScale(), playerColorOf(o));
		if (!e.model)
		{
			entries.push_back(e);
			++counts.missingModel;
			continue;
		}
		e.vehicle = tt->isKindOf(KINDOF_VEHICLE);
		placeModel(e, terrain);
		scene->Add_Render_Object(e.model);
		addShadow(e.model, tt, counts);
		entries.push_back(e);	// keeps the reference from Create_Render_Obj
		++counts.models;
	}
	counts.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	return counts;
}

void releaseMapObjects(RTS3DScene *scene, HeightMapRenderObjClass *terrain, std::vector<MapEntry> &entries)
{
	// Shadows refer to their models, so they go first.
	if (TheW3DShadowManager)
		TheW3DShadowManager->removeAllShadows();
	for (MapEntry &e : entries)
	{
		if (!e.model)
			continue;
		if (scene)
			scene->Remove_Render_Object(e.model);
		REF_PTR_RELEASE(e.model);
	}
	entries.clear();
	if (terrain)
	{
		terrain->removeAllTrees();
		terrain->removeAllProps();
		terrain->clearAllScorches();
	}
}

namespace
{

// Radius of objects drawn by nothing: markers are handles of a few world units.
constexpr float MARKER_RADIUS = 8.0f;

void describe(const MapEntry &e, HeightMapRenderObjClass *terrain, float radius, MePickResult *result)
{
	const Coord3D loc = groundLocation(e.object, terrain);
	result->object = e.editorIndex;
	result->x = loc.x;
	result->y = loc.y;
	result->z = loc.z;
	result->radius = radius;
	result->kind = e.model ? ME_OBJECT_MODEL : (e.tree ? ME_OBJECT_TREE : ME_OBJECT_MARKER);
}

float radiusOf(const MapEntry &e)
{
	if (e.model)
	{
		const AABoxClass &box = e.model->Get_Bounding_Box();
		return std::max(box.Extent.X, box.Extent.Y);
	}
	return e.tree ? e.size : MARKER_RADIUS;
}

} // namespace

bool pickMapObject(const std::vector<MapEntry> &entries, HeightMapRenderObjClass *terrain, const Vector3 &from,
	const Vector3 &dir, float maxDistance, float perPixel, MePickResult *result)
{
	const MapEntry *best = nullptr;
	float bestT = maxDistance;
	float bestRadius = 0.0f;
	for (const MapEntry &e : entries)
	{
		if (e.editorIndex < 0)
			continue;
		float t = -1.0f;
		float radius = 0.0f;
		if (e.model)
		{
			const AABoxClass &box = e.model->Get_Bounding_Box();
			t = rayBox(from, dir, box);
			radius = radiusOf(e);
		}
		else
		{
			const Coord3D loc = groundLocation(e.object, terrain);
			const Vector3 ground(loc.x, loc.y, loc.z);
			// Trees by their size; everything else (waypoints, sounds, lights, scorch marks) as a
			// handle of about ten pixels, like the markers of the overlay.
			radius = e.tree ? e.size : std::max(3.0f, 10.0f * perPixel * (ground - from).Length());
			t = raySphere(from, dir, ground + Vector3(0.0f, 0.0f, e.tree ? radius : 0.0f), radius);
		}
		if (t >= 0.0f && t < bestT)
		{
			bestT = t;
			best = &e;
			bestRadius = radius;
		}
	}
	if (!best)
		return false;
	if (result)
		describe(*best, terrain, bestRadius, result);
	return true;
}

bool mapObjectInfo(const std::vector<MapEntry> &entries, HeightMapRenderObjClass *terrain, int editorIndex, MePickResult *result)
{
	for (const MapEntry &e : entries)
	{
		if (e.editorIndex != editorIndex)
			continue;
		if (result)
			describe(e, terrain, radiusOf(e), result);
		return true;
	}
	return false;
}

int objectsInRect(const std::vector<MapEntry> &entries, HeightMapRenderObjClass *terrain, CameraClass *camera,
	int width, int height, int x0, int y0, int x1, int y1, int32_t *objects, int maxObjects)
{
	const float left = static_cast<float>(std::min(x0, x1));
	const float right = static_cast<float>(std::max(x0, x1));
	const float top = static_cast<float>(std::min(y0, y1));
	const float bottom = static_cast<float>(std::max(y0, y1));
	int count = 0;
	for (const MapEntry &e : entries)
	{
		if (e.editorIndex < 0)
			continue;
		const Coord3D loc = groundLocation(e.object, terrain);
		Vector3 ndc;
		if (camera->Project(ndc, Vector3(loc.x, loc.y, loc.z)) != CameraClass::INSIDE_FRUSTUM)
			continue;
		const float sx = (ndc.X + 1.0f) * 0.5f * static_cast<float>(width);
		const float sy = (1.0f - ndc.Y) * 0.5f * static_cast<float>(height);
		if (sx < left || sx > right || sy < top || sy > bottom)
			continue;
		if (objects && count < maxObjects)
			objects[count] = e.editorIndex;
		++count;
	}
	return count;
}

bool moveMapObject(std::vector<MapEntry> &entries, HeightMapRenderObjClass *terrain, int editorIndex, float x, float y, float z, float angle)
{
	for (MapEntry &e : entries)
	{
		if (e.editorIndex != editorIndex)
			continue;
		Coord3D loc = *e.object->getLocation();
		loc.x = x;
		loc.y = y;
		loc.z = z;
		e.object->setLocation(&loc);
		e.object->setAngle(angle);
		if (e.model)
			placeModel(e, terrain);
		else if (e.tree)
			terrain->updateTreePosition(treeId(e.object), groundLocation(e.object, terrain), angle);
		else if (e.scorch)
		{
			terrain->clearAllScorches();
			for (const MapEntry &s : entries)
			{
				if (s.scorch)
					addScorchOf(s.object, terrain);
			}
		}
		return true;
	}
	return false;
}

} // namespace MapEditorEngine
