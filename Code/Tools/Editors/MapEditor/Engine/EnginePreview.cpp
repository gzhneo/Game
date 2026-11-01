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

// EnginePreview.cpp -- picture of a template's model for the editor's catalog: the model alone in a
// render target of its own, framed by its bounding box and lit by the map's object lights, then
// read back into memory. Follows the old WorldBuilder (ObjectPreview.cpp).

#include "Engine/EngineApi.h"
#include "Engine/EngineInternal.h"

#include "Common/GlobalData.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "aabox.h"
#include "camera.h"
#include "light.h"
#include "lightenvironment.h"
#include "WW3D2/Common/renderdevice.h"
#include "rendobj.h"
#include "rinfo.h"
#include "texture.h"
#include "ww3d.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{

// Copies the render target into rgba (top row first, opaque). False if it cannot be read.
bool readTarget(TextureClass *target, int size, WW3DFormat format, uint8_t *rgba)
{
	WW3DSurfaceHandle gpu = target->Get_D3D_Surface_Level();
	if (!gpu)
		return false;
	WW3DSurfaceHandle cpu = RenderDevice::_Create_Device_Surface(static_cast<unsigned>(size), static_cast<unsigned>(size), format);
	bool ok = false;
	if (cpu)
	{
		RenderDevice::_Copy_Rects(gpu, nullptr, 0, cpu, nullptr);
		WW3DLockedRect rect;
		if (SUCCEEDED(RenderDevice::Lock_Surface(cpu, &rect, nullptr, WW3D_LOCK_READONLY)))
		{
			// A8R8G8B8 and X8R8G8B8 lie in memory as B, G, R, A.
			for (int y = 0; y < size; ++y)
			{
				const uint8_t *src = static_cast<const uint8_t *>(rect.pBits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(rect.Pitch);
				uint8_t *dst = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(size) * 4;
				for (int x = 0; x < size; ++x, src += 4, dst += 4)
				{
					dst[0] = src[2];
					dst[1] = src[1];
					dst[2] = src[0];
					dst[3] = 255;
				}
			}
			RenderDevice::Unlock_Surface(cpu);
			ok = true;
		}
		RenderDevice::Release_Resource(cpu);
	}
	RenderDevice::Release_Resource(gpu);
	return ok;
}

// The object lights of the map's time of day, as the 3D view uses them.
void addMapLights(LightEnvironmentClass &lights, const Vector3 &center)
{
	const Int time = TheGlobalData->m_timeOfDay;
	const GlobalData::TerrainLighting *ol = &TheGlobalData->m_terrainObjectsLighting[time][0];
	lights.Reset(center, Vector3(ol->ambient.red, ol->ambient.green, ol->ambient.blue));
	for (Int i = 0; i < MAX_GLOBAL_LIGHTS; ++i)
	{
		ol = &TheGlobalData->m_terrainObjectsLighting[time][i];
		LightClass *light = NEW_REF(LightClass, (LightClass::DIRECTIONAL));
		light->Set_Ambient(Vector3(0.0f, 0.0f, 0.0f));
		light->Set_Diffuse(Vector3(ol->diffuse.red, ol->diffuse.green, ol->diffuse.blue));
		light->Set_Specular(Vector3(0.0f, 0.0f, 0.0f));
		Matrix3D mtx;
		mtx.Set(Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(ol->lightPos.x, ol->lightPos.y, ol->lightPos.z), Vector3(0, 0, 0));
		light->Set_Transform(mtx);
		lights.Add_Light(*light);
		REF_PTR_RELEASE(light);
	}
}

} // namespace

MEAPI int32_t mePreviewTemplate(const char *name, int32_t size, float yaw, const char *team, uint32_t background, uint8_t *rgba)
{
	if (!name || !rgba || size < 16 || size > 512 || (size & (size - 1)) != 0 || !MapEditorEngine::viewReady() || !TheThingFactory)
		return 0;
	const ThingTemplate *tt = TheThingFactory->findTemplate(AsciiString(name), FALSE);
	if (!tt)
		return 0;
	const AsciiString modelName = MapEditorEngine::defaultModelName(tt);
	W3DAssetManager *assets = static_cast<W3DAssetManager *>(WW3DAssetManager::Get_Instance());
	if (modelName.isEmpty() || !assets)
		return 0;
	RenderObjClass *model = assets->Create_Render_Obj(modelName.str(), tt->getAssetScale(), team ? MapEditorEngine::teamColor(team) : 0xFFFFFF);
	if (!model)
		return 0;

	WW3DFormat format = WW3D_FORMAT_A8R8G8B8;
	TextureClass *target = RenderDevice::Create_Render_Target(size, size, format);
	if (!target)
	{
		format = WW3D_FORMAT_X8R8G8B8;
		target = RenderDevice::Create_Render_Target(size, size, format);
	}
	bool ok = false;
	if (target && target->Get_Width() == size)
	{
		model->Set_Transform(Matrix3D(true));
		// Looking slightly down from the given side; the camera backs off until every corner of the
		// bounding box lies in the picture, so slim models (trees, masts) fill it too.
		const AABoxClass box = model->Get_Bounding_Box();
		const Vector3 center = box.Center;
		const float fov = 0.6f;
		const float pitch = 0.42f;
		const float tanHalf = std::tan(fov * 0.5f);
		const Vector3 dir(std::cos(yaw) * std::cos(pitch), std::sin(yaw) * std::cos(pitch), std::sin(pitch));
		const Vector3 right(-std::sin(yaw), std::cos(yaw), 0.0f);
		const Vector3 up(-std::cos(yaw) * std::sin(pitch), -std::sin(yaw) * std::sin(pitch), std::cos(pitch));
		float distance = 1.0f;
		for (int corner = 0; corner < 8; ++corner)
		{
			const Vector3 rel((corner & 1) ? box.Extent.X : -box.Extent.X, (corner & 2) ? box.Extent.Y : -box.Extent.Y,
				(corner & 4) ? box.Extent.Z : -box.Extent.Z);
			const float side = std::max(std::abs(Vector3::Dot_Product(rel, right)), std::abs(Vector3::Dot_Product(rel, up)));
			distance = std::max(distance, side / tanHalf + Vector3::Dot_Product(rel, dir));
		}
		distance *= 1.06f;
		const float reach = std::max(box.Extent.Length(), 1.0f);
		CameraClass *camera = NEW_REF(CameraClass, ());
		Matrix3D camTm;
		camTm.Look_At(center + dir * distance, center, 0.0f);
		camera->Set_Transform(camTm);
		camera->Set_View_Plane(fov, fov);
		camera->Set_Clip_Planes(std::max(distance - reach * 1.5f, distance * 0.02f), distance + reach * 1.5f);

		RenderDevice::Set_Render_Target_With_Z(target);
		const Vector3 color(static_cast<float>((background >> 16) & 0xFFu) / 255.0f, static_cast<float>((background >> 8) & 0xFFu) / 255.0f,
			static_cast<float>(background & 0xFFu) / 255.0f);
		if (WW3D::Begin_Render(true, true, color) == WW3D_ERROR_OK)
		{
			RenderInfoClass rinfo(*camera);
			LightEnvironmentClass lights;
			addMapLights(lights, center);
			lights.Pre_Render_Update(camTm);
			rinfo.light_environment = &lights;
			WW3D::Render(*model, rinfo);
			WW3D::End_Render(false);
			ok = true;
		}
		RenderDevice::Set_Render_Target(static_cast<WW3DSurfaceHandle>(nullptr));
		ok = ok && readTarget(target, size, format, rgba);
		REF_PTR_RELEASE(camera);
	}
	REF_PTR_RELEASE(target);
	REF_PTR_RELEASE(model);
	return ok ? 1 : 0;
}
