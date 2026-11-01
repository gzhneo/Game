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

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "stdio.h"
#include "formatinto.h"
#include "W3DDevice/GameClient/W3DWater.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "W3DDevice/GameClient/W3DWaterTracks.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "texture.h"
#include "assetmgr.h"
#include "rinfo.h"
#include "camera.h"
#include "scene.h"
#include "WW3D2/Common/renderdevice.h"
#include "WW3D2/Common/renderbackend.h"	// viewport for the scene copy
#include "light.h"
#include "wwd3dmath.h"	// matrix math helpers
#include "simplevec.h"
#include "mesh.h"
#include "matinfo.h"

#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/Xfer.h"
#include "Common/GameLOD.h"
#include "Common/GameEngine.h"	// GameEngine_RenderTimeMs
#include "Common/FileSystem.h"		// water textures from ProjectData
#include "Common/file.h"
#include "Common/ArchiveFileSystem.h"	// sky tiling for HD texture archives
#include <vector>

#include "GameClient/Water.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptEngine.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DPoly.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DCustomScene.h"
#include "formatinto.h"
#include "W3DWaterInternal.h"

// Sky plane, sky body and the mirror reflection

#define SKYPLANE_SIZE	(384.0f*MAP_XY_FACTOR)
#define SKYPLANE_HEIGHT	(30.0f)

/* in the bay
#define SKYBODY_X	120.0f			//location of skybody
#define SKYBODY_Y	75.0f			//location of skybody
*/

#define SKYBODY_HEIGHT	SKYPLANE_HEIGHT	//altitude of sky body (z-buffer disabled, so can equal sky height).

constexpr auto SKYBODY_SIZE = 45.0f;		//extent or radius of sky body

constexpr auto SKYBODY_X = 150.0f;	//location of skybody

constexpr auto SKYBODY_Y = 550.0f;	//location of skybody

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::replaceSkyboxTexture(const AsciiString& oldTexName, const AsciiString& newTextName)
{
	W3DAssetManager* assetManager = (static_cast<W3DAssetManager*>(W3DAssetManager::Get_Instance()));

	assetManager->replacePrototypeTexture(m_skyBox, oldTexName.str(), newTextName.str());

	//Enable clamping on all textures used by the skybox (to reduce corner seams).
	if (m_skyBox && m_skyBox->Class_ID() == RenderObjClass::CLASSID_MESH)
	{
		MeshClass *mesh=static_cast<MeshClass*>(m_skyBox);
		MaterialInfoClass	*material = mesh->Get_Material_Info();

		for (Int i=0; i<material->Texture_Count(); i++)
		{
			if (material->Peek_Texture(i))
			{		
				material->Peek_Texture(i)->Get_Filter().Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
				material->Peek_Texture(i)->Get_Filter().Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
			}
		}

		// Get_Material_Info returns a counted reference (Add_Ref); release it as in init().
		REF_PTR_RELEASE(material);
	}

}

//-------------------------------------------------------------------------------------------------
/** Renders the reflected scene into an offscreen texture. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::renderMirror(CameraClass *cam)
{
#ifdef EXTENDED_STATS
	if (RenderDevice::stats.m_disableWater) {
		return;
	}
#endif
	Matrix3D	OldCameraMatrix=cam->Get_Transform();
	Matrix4x4	FullMatrix4(cam->Get_Transform());	//copy 3x4 matrix into a 4x4
	Vector3		WaterNormal(0,0,1);	//normal of plane used for reflection
	Vector4		WaterPlane(WaterNormal.X,WaterNormal.Y,WaterNormal.Z,m_level);
	Vector3		rRight,rUp,rN,rPos;	//orientation and translation vectors of camera

	Matrix4x4	FullMatrix(FullMatrix4.Transpose());	//swap rows/columns

	//reflect camera right vector
	Real axis_distance=Vector3::Dot_Product(reinterpret_cast<Vector3&>(FullMatrix[0]),WaterNormal);
	rRight = reinterpret_cast<Vector3&>(FullMatrix[0]) - (2.0f*axis_distance*WaterNormal);

	//reflect camera up vector
	axis_distance=Vector3::Dot_Product(reinterpret_cast<Vector3&>(FullMatrix[1]),WaterNormal);
	rUp = reinterpret_cast<Vector3&>(FullMatrix[1]) - (2.0f*axis_distance*WaterNormal);

	//reflect camera n vector
	axis_distance=Vector3::Dot_Product(reinterpret_cast<Vector3&>(FullMatrix[2]),WaterNormal);
	rN = reinterpret_cast<Vector3&>(FullMatrix[2]) - (2.0f*axis_distance*WaterNormal);

	//reflect camera position
	axis_distance=Vector3::Dot_Product(reinterpret_cast<Vector3&>(FullMatrix[3]),WaterNormal);	//distance cam to origin
	axis_distance -= WaterPlane.W;	// subtract mirror plane distance to get distance camera to plane
	rPos = reinterpret_cast<Vector3&>(FullMatrix[3]) - (2.0f*axis_distance*WaterNormal);

	//generate a new camera matrix from reflected vectors
	Matrix3D reflectedTransform(rRight,rUp,rN,rPos);


	RenderDevice::Begin_Target_Pass(RENDER_GPU_PASS_MIRROR, static_cast<TextureClass*>(m_pReflectionTexture));

	// Clear the backbuffer
	WW3D::Begin_Render(false,true,Vector3(0.0f,0.0f,0.0f));	//clearing only z-buffer since background always filled with clouds

	cam->Set_Transform( reflectedTransform );

	//Force reflected image to be drawn into full texture size - not a viewport inside texture.
	Vector2 vMin,vMax,vOldMax,vOldMin;
 	cam->Get_Viewport(vOldMin,vOldMax);
 	vMax.X=vMax.Y=1.0f;
	vMin.X=vMin.Y=0.0f;
 	cam->Set_Viewport(vMin,vMax);

	cam->Apply();	//force an update of all the camera dependent parameters like frustum clip planes

	//flip the winding order of polygons to draw the reflected back sides.
	ShaderClass::Invert_Backface_Culling(true);

	// Render the scene
	renderSky();
	if (m_tod == TIME_OF_DAY_NIGHT)
		renderSkyBody(&reflectedTransform);

	WW3D::Render(m_parentScene,cam);

	cam->Set_Transform(OldCameraMatrix);	//restore original non-reflected matrix
 	cam->Set_Viewport(vOldMin,vOldMax);

	cam->Apply();	//force an update of all the camera dependent parameters like frustum clip planes

	ShaderClass::Invert_Backface_Culling(false);

	WW3D::End_Render(false);

	// Change the rendertarget back to the main backbuffer
	RenderDevice::End_Target_Pass(RENDER_GPU_PASS_MIRROR);
}

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the sky plane.  Will apply current time-of-day settings including
	* some simple UV scrolling animation. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::renderSky(void)
{
	Int timeNow,timeDiff;
	Real fu,fv;

	Setting *setting=&m_settings[m_tod];

	timeNow=GameEngine_RenderTimeMs();	// render clock

	timeDiff=timeNow-m_LastUpdateTime;
	m_LastUpdateTime=timeNow;

	m_uOffset += timeDiff * setting->uScrollPerMs * setting->skyTexelsPerUnit;
	m_vOffset += timeDiff * setting->vScrollPerMs * setting->skyTexelsPerUnit;

	//clamp uv coordinate into 0,1 range
	m_uOffset = m_uOffset - static_cast<Real>(static_cast<Int>(m_uOffset));
	m_vOffset = m_vOffset - static_cast<Real>(static_cast<Int>(m_vOffset));

	fu= m_uOffset + (SKYPLANE_SIZE * 2) * setting->skyTexelsPerUnit;
	fv= m_vOffset + (SKYPLANE_SIZE * 2) * setting->skyTexelsPerUnit;


	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	RenderDevice::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);

	ShaderClass m_shader2=ShaderClass::_PresetOpaqueShader;
	m_shader2.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
	m_shader2.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);	//no need to check against z-buffer, sky always rendered first.
	m_shader2.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);	//sky is always behind everything so no need to update z-buffer

	RenderDevice::Set_Shader(m_shader2);

	RenderDevice::Set_Texture(0,setting->skyTexture);

	//draw an infinite sky plane
	DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DEVICE,dynamic_fvf_type,4);
	{
		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* verts=lock.Get_Formatted_Vertex_Array();
		if(verts)
		{
			verts[0].x=-SKYPLANE_SIZE;
			verts[0].y=SKYPLANE_SIZE;
			verts[0].z=SKYPLANE_HEIGHT;
			verts[0].u1=m_uOffset;
			verts[0].v1=fv;
			verts[0].diffuse=setting->vertex01Diffuse;
	
			verts[1].x=SKYPLANE_SIZE;
			verts[1].y=SKYPLANE_SIZE;
			verts[1].z=SKYPLANE_HEIGHT;
			verts[1].u1=fu;
			verts[1].v1=fv;
			verts[1].diffuse=setting->vertex11Diffuse;
	
			verts[2].x=SKYPLANE_SIZE;
			verts[2].y=-SKYPLANE_SIZE;
			verts[2].z=SKYPLANE_HEIGHT;
			verts[2].u1=fu;
			verts[2].v1=m_vOffset;
			verts[2].diffuse=setting->vertex10Diffuse;
	
			verts[3].x=-SKYPLANE_SIZE;
			verts[3].y=-SKYPLANE_SIZE;
			verts[3].z=SKYPLANE_HEIGHT;
			verts[3].u1=m_uOffset;
			verts[3].v1=m_vOffset;
			verts[3].diffuse=setting->vertex00Diffuse;
		}
	}

	RenderDevice::Set_Index_Buffer(m_indexBuffer,0);
	RenderDevice::Set_Vertex_Buffer(vb_access);

	Matrix3D tm(1);
	tm.Set_Translation(Vector3(0,0,0));
	RenderDevice::Set_Transform(WW3D_TS_WORLD,tm);

	RenderDevice::Draw_Triangles(	0,2, 0,	4);	//draw a quad, 2 triangles, 4 verts
}

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the sky body.  Used for moon and sun.  We rotate the image
	* so that it always faces the camera.  This removes perspective and helps hide that
	* it's a flat image. */
//-------------------------------------------------------------------------------------------------
///	@todo: Add code to render properly sorted sun sky body.
void WaterRenderObjClass::renderSkyBody(Matrix3D *mat)
{	
	Vector3 cPos;

	Vector3 pView,pRight,pUp,pPos(SKYBODY_X,SKYBODY_Y,SKYBODY_HEIGHT);

	mat->Get_Translation(&cPos);

	pView=cPos-pPos;	//billboard to camera
	pView.Normalize();	//particle view direction

	Vector3 WorldUp(0,0,-1);	///@todo: hacked so only works for reflections across xy plane

	Vector3 rotAxis;
	Vector3::Normalized_Cross_Product(WorldUp, pView, &rotAxis);	

	Real angle=Vector3::Dot_Product(WorldUp,pView);

	angle = acos(angle);


	Matrix3D tm(1);
	tm.Set(rotAxis,angle);
	tm.Adjust_Translation(Vector3(SKYBODY_X,SKYBODY_Y,SKYBODY_HEIGHT));


	RenderDevice::Set_Transform(WW3D_TS_WORLD,tm);


	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	RenderDevice::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);

	ShaderClass m_shader2=ShaderClass::_PresetAlphaShader;
	m_shader2.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
	m_shader2.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);	//no need to check against z-buffer, sky always rendered first.
	m_shader2.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);	//sky is always behind everything so no need to update z-buffer

	RenderDevice::Set_Shader(m_shader2);


//	RenderDevice::Set_Shader(ShaderClass::/*_PresetAdditiveShader*//*_PresetOpaqueShader*/_PresetAlphaShader);
//	RenderDevice::Set_Texture(0,setting->skyBodyTexture);

	RenderDevice::Set_Texture(0,m_alphaClippingTexture);

	//draw an infinite sky plane
	DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DEVICE,dynamic_fvf_type,4);
	{
		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* verts=lock.Get_Formatted_Vertex_Array();
		if(verts)
		{
			verts[0].x=-SKYBODY_SIZE;
			verts[0].y=SKYBODY_SIZE;
			verts[0].z=0;
			verts[0].u2=0;
			verts[0].v2=1;
			verts[0].diffuse=0xffffffff;
	
			verts[1].x=SKYBODY_SIZE;
			verts[1].y=SKYBODY_SIZE;
			verts[1].z=0;
			verts[1].u2=1;
			verts[1].v2=1;
			verts[1].diffuse=0xffffffff;
	
			verts[2].x=SKYBODY_SIZE;
			verts[2].y=-SKYBODY_SIZE;
			verts[2].z=0;
			verts[2].u2=1;
			verts[2].v2=0;
			verts[2].diffuse=0xffffffff;
	
			verts[3].x=-SKYBODY_SIZE;
			verts[3].y=-SKYBODY_SIZE;
			verts[3].z=0;
			verts[3].u2=0;
			verts[3].v2=0;
			verts[3].diffuse=0xffffffff;
		}
	}

	RenderDevice::Set_Index_Buffer(m_indexBuffer,0);
	RenderDevice::Set_Vertex_Buffer(vb_access);

	RenderDevice::Draw_Triangles(	0,2, 0,	4);	//draw a quad, 2 triangles, 4 verts
}
