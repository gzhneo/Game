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
#include "WW3D2/Common/renderpieces.h"
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

// Drawing the water: render entry, sea with bump mapping, water surfaces

// Converts a FLOAT to a DWORD for use in SetRenderState() calls
static inline DWORD F2DW( FLOAT f ) { return *(reinterpret_cast<DWORD*>(&f)); }

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the water.
	*	Algorithm:
	*	Draw reflected scene.
	*	Draw reflected sky layer(s) and bodies.
	*	Clear Zbuffer
	*	Fill Zbuffer by drawing water surface (allows proper sorting into regular scene).
	*	Draw non-reflected scene (done in regular app render loop).
	*
	*	This algorithm doesn't apply to translucent water, which is rendered into a
	*   texture and rendered at end of scene. */
//-------------------------------------------------------------------------------------------------
//DECLARE_PERF_TIMER(Water)
void WaterRenderObjClass::Render(RenderInfoClass & rinfo)
{
	//USE_PERF_TIMER(Water)
	if (TheTerrainRenderObject && !TheTerrainRenderObject->getMap())
		return;	//no map has been loaded yet.

	if ((static_cast<RTS3DScene *>(rinfo.Camera.Get_User_Data()))->getCustomPassMode() == CustomScenePassModes::SCENE_PASS_ALPHA_MASK ||
		(static_cast<SceneClass *>(rinfo.Camera.Get_User_Data()))->Get_Extra_Pass_Polygon_Mode() == SceneClass::EXTRA_PASS_CLEAR_LINE)
		return;	//water is not drawn in wireframe or custom scene passes

#ifdef EXTENDED_STATS
	if (RenderDevice::stats.m_disableWater) {
		return;
	}
#endif
	if (ShaderClass::Is_Backface_Culling_Inverted())
		return;	//the water object will not reflect in itself, so don't do anything if rendering a mirror.

	//this water type needs to rendered after the rest of scene, so buffer it up for later

	// If static sort lists are enabled and this mesh has a sort level, put it on the list instead
	// of rendering it.
	unsigned int sort_level = static_cast<unsigned int>(Get_Sort_Level());

	if (WW3D::Are_Static_Sort_Lists_Enabled() && sort_level != SORT_LEVEL_NONE) 
	{	
		WW3D::Add_To_Static_Sort_List(this, sort_level);
		return;
	}

	switch(m_waterType)
	{
		case WATER_TYPE_0_TRANSLUCENT:
		case WATER_TYPE_3_GRIDMESH:
			//Draw the water surface as a bunch of alpha blended tiles covering areas where water is visible
			renderWater();
			if (!m_drawingRiver || m_disableRiver) {
				renderWaterMesh();	//Draw water surface as 3D deforming mesh if it's enabled on this map.
			}
			break;

		case WATER_TYPE_2_PVSHADER:
			//Pixel/Vertex Shader based water which uses an off-screen rendered reflection texture
			drawSea(rinfo);	//draw water surface
			break;

		case WATER_TYPE_1_FB_REFLECTION:
			{
				//Normal frame buffer reflection water type. Non translucent.  Legacy code we're not using anymore.
				Matrix3D	OldCameraMatrix=rinfo.Camera.Get_Transform();
				Matrix4x4	FullMatrix4(rinfo.Camera.Get_Transform());	//copy 3x4 matrix into a 4x4
				Vector3		WaterNormal(0,0,1);	//normal of plane used for reflection
				Vector4		WaterPlane(WaterNormal.X,WaterNormal.Y,WaterNormal.Z,m_level);	//assume distance to origin 0
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

				//flip the winding order of polygons to draw the reflected back sides.
				ShaderClass::Invert_Backface_Culling(true);

			#ifdef CLIP_GEOMETRY_TO_PLANE
			  // Set a clip plane, so that only objects above the water are reflected
				WaterPlane.W *= -1.0f;	//flip sign of plane distance for D3D use.

			//	RenderDevice::Set_Clip_Plane( 0, &WaterPlane.X );
			//	RenderDevice::Set_Render_State(WW3D_RS_CLIPPLANEENABLE, WW3D_CLIPPLANE0 );	//turn on first clip plane

				// Alternate Clipping Method using alpha testing hack!
				/**************************************************************************************/

				WWD3DMatrix inv;
				WWD3DMatrix clipMatrix;
				Real det;
				Matrix4x4 curView;

				//get current view matrix
				RenderDevice::_Get_Device_Transform(WW3D_TS_VIEW, curView);

				//get inverse of view matrix(= view to world matrix)
				WWD3D::MatrixInverse(&inv, &det, (WWD3DMatrix*)&curView);

				//create clipping matrix by inserting our plane equation into the 1st column
				WWD3D::MatrixIdentity(&clipMatrix);
				clipMatrix(0,0)=WaterNormal.X;
				clipMatrix(1,0)=WaterNormal.Y;
				clipMatrix(2,0)=WaterNormal.Z;
				clipMatrix(3,0)=WaterPlane.W+0.5f;
				inv *=clipMatrix;

				// Change texture wrapping mode to 'clamp' for texture stage 1
				RenderDevice::Set_Sampler_State( 1, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_CLAMP);
				RenderDevice::Set_Sampler_State( 1, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_CLAMP);

				// Use CameraSpace vertices as input to matrix and use texture wrap mode from stage 1
				RenderDevice::Set_Texture_Stage(1, TextureStageDesc().Tex_Coord_Index(WW3D_TCI_CAMERASPACEPOSITION|1));
				// Two output coordinates are used.
				RenderDevice::Set_Texture_Stage(1, TextureStageDesc().Tex_Transform(WW3D_TTFF_COUNT2));

				// Set texture generation matrix for stage 1
				RenderDevice::_Set_Device_Transform(WW3D_TS_TEXTURE1, *((Matrix4x4*)&inv));

				// Disable bilinear filtering
				RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MINFILTER, WW3D_TEXF_POINT);
				RenderDevice::Set_Sampler_State(1, WW3D_SAMP_MAGFILTER, WW3D_TEXF_POINT);

				// Pass stage 0 texture data untouched(by modulating with white).
				// Color: modulating with white changes nothing; alpha: modulate with the clipping texture.
				RenderDevice::Set_Texture_Stage(1, TextureStageDesc()
					.Color(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_CURRENT)
					.Alpha(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_CURRENT)
				);

				RenderDevice::Set_Render_State(WW3D_RS_ALPHAREF,0x00);
				RenderDevice::Set_Render_State(WW3D_RS_ALPHAFUNC,WW3D_CMP_NOTEQUAL);	//pass pixels who's alpha is not zero
				RenderDevice::Set_Render_State(WW3D_RS_ALPHATESTENABLE, true);	//test pixels if transparent(clipped) before rendering.

				// Set clipping texture
				m_alphaClippingTexture->Set_U_Addr_Mode(TextureClass::TEXTURE_ADDRESS_CLAMP);
				m_alphaClippingTexture->Set_V_Addr_Mode(TextureClass::TEXTURE_ADDRESS_CLAMP);
				m_alphaClippingTexture->Set_Min_Filter(TextureClass::FILTER_TYPE_NONE);
				m_alphaClippingTexture->Set_Mag_Filter(TextureClass::FILTER_TYPE_NONE);
				m_alphaClippingTexture->Set_Mip_Mapping(TextureClass::FILTER_TYPE_NONE);

				RenderDevice::Set_Texture(0,m_alphaClippingTexture);

				//TODO: Will have to make sure that the shader system is not resetting my stage 1 setup
				//while rendering the scene

				/*************************************************************************************/
			#endif


			#ifdef CLIP_GEOMETRY_TO_PLANE
				//restore default culling mode
			//	RenderDevice::Set_Render_State(WW3D_RS_CLIPPLANEENABLE, 0 );	//turn off first clip plane

				//disable texture coordinate generation
				RenderDevice::Set_Texture_Stage(1, TextureStageDesc().Tex_Transform(WW3D_TTFF_DISABLE));
				RenderDevice::Set_Render_State(WW3D_RS_ALPHATESTENABLE, false);	//disable alpha testing
			#endif

				ShaderClass::Invert_Backface_Culling(false);	//return culling back to normal

				ShaderClass::Invalidate();	//reset shading system so it forces full state set.

				renderWater();
			}	//WATER_TYPE_1
			break;

		default:
			break;
	}//switch

	if (TheGlobalData && TheGlobalData->m_drawSkyBox)
	{	//center skybox around camera
		Vector3 pos=rinfo.Camera.Get_Position();
		pos.Z = TheGlobalData->m_skyBoxPositionZ;
		m_skyBox->Set_Position(pos);
		m_skyBox->Render(rinfo);
	}

	//Clean up after any pixel shaders.
	//Force render state apply so that the "NULL" texture gets applied to D3D, thus releasing shroud reference count.
	RenderDevice::Apply_Render_State_Changes();
	RenderDevice::Invalidate_Cached_Render_States();

	if (m_waterTrackSystem)
		m_waterTrackSystem->flush(rinfo);

//	renderWaterMesh();
//	renderWaterWave();
}

//-------------------------------------------------------------------------------------------------
/** Clips the water plane to the current camera frustum and returns a bounding
	* box enclosing the clipped plane.  Returns false if water plane is not visible. */
//-------------------------------------------------------------------------------------------------
Bool WaterRenderObjClass::getClippedWaterPlane(CameraClass *cam, AABoxClass *box)
{
	const FrustumClass & frustum = cam->Get_Frustum();

	ClipPolyClass	ClippedPoly0;
	ClipPolyClass	ClippedPoly1;

	///@todo: generate proper sized polygon
	ClippedPoly0.Reset();
	ClippedPoly0.Add_Vertex(Vector3(0,0,m_level));
	ClippedPoly0.Add_Vertex(Vector3(0,m_dy,m_level));
	ClippedPoly0.Add_Vertex(Vector3(m_dx,m_dy,m_level));
	ClippedPoly0.Add_Vertex(Vector3(m_dx,0,m_level));

	//clip against all 6 frustum planes
	ClippedPoly0.Clip(frustum.Planes[0],ClippedPoly1);
	ClippedPoly1.Clip(frustum.Planes[1],ClippedPoly0);
	ClippedPoly0.Clip(frustum.Planes[2],ClippedPoly1);
	ClippedPoly1.Clip(frustum.Planes[3],ClippedPoly0);
	ClippedPoly0.Clip(frustum.Planes[4],ClippedPoly1);
	ClippedPoly1.Clip(frustum.Planes[5],ClippedPoly0);

	Int final_vcount = ClippedPoly0.Verts.Count();

	//make sure the polygon is visible
	if (final_vcount >= 3)
	{	
		//find axis aligned bounding box around visible polygon
		if (box)
  			box->Init(&(ClippedPoly0.Verts[0]),final_vcount);
		return TRUE;
	}

	return FALSE;	//water plane is not visible
}

//-------------------------------------------------------------------------------------------------
/** Draws the water surface using a custom D3D vertex/pixel shader and a
	* reflection texture.  Only tested to work on GeForce3. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::drawSea(RenderInfoClass & rinfo)
{
	AABoxClass	seaBox;

	if (!getClippedWaterPlane(&rinfo.Camera,&seaBox))
		return;	//the sea is not visible

	WWD3DMatrix matProj, matView, matWW3D;

	//create a transform which will flip the y and z coordinates to fit our system
	memset(&matWW3D,0,sizeof(WW3DMatrixRaw));
	matWW3D._11=1.0f;
	matWW3D._32=1.0f;
	matWW3D._23=1.0f;
	matWW3D._44=1.0f;

	Matrix3D tm(Transform);

	RenderDevice::Set_Transform(WW3D_TS_WORLD,tm);	//position the water surface
	RenderDevice::Set_Texture(0,nullptr);	//we'll be setting our own textures, so reset W3D
	RenderDevice::Set_Texture(1,nullptr);	//we'll be setting our own textures, so reset W3D


	RenderDevice::Apply_Render_State_Changes();	//force update of view and projection matrices

	Vector3 camTran;

	rinfo.Camera.Get_Transform().Get_Translation(&camTran);

	RenderDevice::_Get_Device_Transform(WW3D_TS_VIEW, *reinterpret_cast<Matrix4x4*>(&matView));
	RenderDevice::_Get_Device_Transform(WW3D_TS_PROJECTION, *reinterpret_cast<Matrix4x4*>(&matProj));
	// The shader constant uses the projection including the backend's raster offset; matProj stays
	// raw because it is written back to the device below.
	WWD3DMatrix matProjShader;
	RenderDevice::_Get_Device_Shader_Projection(*reinterpret_cast<Matrix4x4*>(&matProjShader));

	//default setup from Kenny's demo
	RenderDevice::Set_Texture_Stage(0, TextureStageDesc()
		.Color(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_DIFFUSE)
		.Alpha_Op(WW3D_TOP_DISABLE)
		.Tex_Coord_Index(0)
	);

	RenderDevice::Set_Texture_Stage(1, TextureStageDesc()
		.Color(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_CURRENT)
		.Alpha_Op(WW3D_TOP_DISABLE)
		.Tex_Coord_Index(1)
	);

	RenderDevice::Set_Texture_Stage(2, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_PASSTHRU|2)
		.Tex_Transform(WW3D_TTFF_DISABLE)
	);

	RenderDevice::Set_Texture_Stage(3, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_PASSTHRU|3)
		.Tex_Transform(WW3D_TTFF_DISABLE)
	);

//	RenderDevice::Set_Sampler_State(0,WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR );
//	RenderDevice::Set_Sampler_State(0,WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR );
//	RenderDevice::Set_Sampler_State(0,WW3D_SAMP_MIPFILTER, WW3D_TEXF_POINT );

//	RenderDevice::Set_Sampler_State(1,WW3D_SAMP_MINFILTER, WW3D_TEXF_POINT );
//	RenderDevice::Set_Sampler_State(1,WW3D_SAMP_MAGFILTER, WW3D_TEXF_POINT );
//	RenderDevice::Set_Sampler_State(1,WW3D_SAMP_MIPFILTER, WW3D_TEXF_NONE );
	//end of default setup

	RenderDevice::Set_Sampler_State(0,WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
	RenderDevice::Set_Sampler_State(0,WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);
	RenderDevice::Set_Render_State(WW3D_RS_WRAP0, WW3D_WRAP_U | WW3D_WRAP_V);

	RenderDevice::Set_Sampler_State(1,WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_CLAMP);
	RenderDevice::Set_Sampler_State(1,WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_CLAMP);

	RenderDevice::Set_Device_Texture( 0, m_pBumpTexture[m_iBumpFrame]);
#ifdef MIPMAP_BUMP_TEXTURE
	RenderDevice::Set_Sampler_State(0,WW3D_SAMP_MIPFILTER, WW3D_TEXF_POINT );
	RenderDevice::Set_Sampler_State(0,WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR );
	RenderDevice::Set_Sampler_State(0,WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR );
#endif
	RenderDevice::Set_Texture_Stage_State(1,WW3D_TSS_BUMPENVMAT00, F2DW(m_fBumpScale) );
	RenderDevice::Set_Texture_Stage_State(1,WW3D_TSS_BUMPENVMAT01, F2DW(0.0f) );
	RenderDevice::Set_Texture_Stage_State(1,WW3D_TSS_BUMPENVMAT10, F2DW(0.0f) );
	RenderDevice::Set_Texture_Stage_State(1,WW3D_TSS_BUMPENVMAT11, F2DW(m_fBumpScale) );
	RenderDevice::Set_Texture_Stage_State(1,WW3D_TSS_BUMPENVLSCALE, F2DW(1.0f) );
	RenderDevice::Set_Texture_Stage_State(1,WW3D_TSS_BUMPENVLOFFSET, F2DW(0.0f) );
	// wave.ps.hlsl emulates texbem and needs the bump matrix (M00, M01, M10, M11).
	RenderDevice::Set_Pixel_Shader_Constant(RENDER_PIECE_REG(RenderSeaWavePS, bumpEnvMat), Vector4(m_fBumpScale, 0.0f, 0.0f, m_fBumpScale));

	RenderDevice::Disable_Texture_Stage(2);

	RenderDevice::Set_Render_State(WW3D_RS_ZWRITEENABLE , FALSE);

	WWD3DMatrix mat;
	memset(&mat,0,sizeof(WWD3DMatrix));

	mat._11 = 0.5f; mat._12 = -0.5f; mat._13 = 0.5f;   mat._14=0.5f;
	mat._21 = 0.5f; mat._22 = 0.5f; mat._23 = 0.0f;   mat._24=0.0f;
	mat._31 = 0.0f; mat._32 = 0.0f; mat._33 = 0.0f;   mat._34=1.0f;
	mat._41 = 0.0f; mat._42 = 0.0f; mat._43 = 0.0f;   mat._44=1.0f;

	RenderDevice::Set_Vertex_Shader_Constant(RENDER_PIECE_REG(RenderSeaWaveVS, texProj), reinterpret_cast<const float*>(&mat), 4);

	// Setup constants
	Vector4 zero4(0.0f, 0.0f, 0.0f, 0.0f), one4(1.0f, 1.0f, 1.0f, 1.0f);
	RenderDevice::Set_Vertex_Shader_Constant(RENDER_PIECE_REG(RenderSeaWaveVS, zero), reinterpret_cast<const float*>(&zero4), 1);
	RenderDevice::Set_Vertex_Shader_Constant(RENDER_PIECE_REG(RenderSeaWaveVS, one),  reinterpret_cast<const float*>(&one4), 1);

	// An active vertex shader needs a matching vertex declaration (see ReAcquireResources).
	RenderDevice::Set_Vertex_Declaration(m_waveVertexDeclaration);
	RenderDevice::Set_Vertex_Shader_Object(m_dwWaveVertexShader);
	RenderDevice::Set_Pixel_Shader_Object(m_dwWavePixelShader);

//	Make reflection brighter to compensate for darker coloring on sea floor
//	RenderDevice::Set_Render_State(WW3D_RS_SRCBLEND, WW3D_BLEND_ONE );
//	RenderDevice::Set_Render_State(WW3D_RS_DESTBLEND, WW3D_BLEND_SRCCOLOR );

	RenderDevice::Set_Render_State(WW3D_RS_SRCBLEND, WW3D_BLEND_SRCALPHA );
	RenderDevice::Set_Render_State(WW3D_RS_DESTBLEND, WW3D_BLEND_INVSRCALPHA );

	RenderDevice::Set_Render_State(WW3D_RS_ALPHABLENDENABLE , TRUE);
	RenderDevice::Set_Device_Texture( 1, m_pReflectionTexture->Peek_D3D_Texture());

//	RenderDevice::Set_Render_State(WW3D_RS_FILLMODE,WW3D_FILL_WIREFRAME);//LORENZEN

	Int patchX,patchY,startX,startY;

	WWD3DMatrix patchMatrix;
	memset(&patchMatrix,0,sizeof(WWD3DMatrix));
	patchMatrix._11=PATCH_SCALE;
	patchMatrix._22=1.0f;
	patchMatrix._33=PATCH_SCALE;
	patchMatrix._44=1.0f;

	RenderDevice::Set_Stream_Source(0,m_vertexBufferD3D,0,sizeof(WaterRenderObjClass::SEA_PATCH_VERTEX));
	RenderDevice::Set_Indices(m_indexBufferD3D);

	for (startY=patchY=static_cast<Int>((seaBox.Center.Y-seaBox.Extent.Y)/(PATCH_WIDTH*PATCH_SCALE)); (patchY*PATCH_WIDTH*PATCH_SCALE)<(seaBox.Center.Y+seaBox.Extent.Y); patchY++)
	{
		for (startX=patchX=static_cast<Int>((seaBox.Center.X-seaBox.Extent.X)/(PATCH_WIDTH*PATCH_SCALE)); (patchX*PATCH_WIDTH*PATCH_SCALE)<(seaBox.Center.X+seaBox.Extent.X); patchX++)
		{
			WWD3DMatrix matWorldViewProj, matTemp, matTempWorld;
			patchMatrix._41=static_cast<float>(patchX*PATCH_WIDTH*PATCH_SCALE );
			patchMatrix._43=static_cast<float>(patchY*PATCH_WIDTH*PATCH_SCALE );
			//convert the default D3D coordinate system into ours
			WWD3D::MatrixMultiply(&matTempWorld, &patchMatrix, &matWW3D);

			WWD3D::MatrixMultiply(&matTemp, &matTempWorld, &matView);
			WWD3D::MatrixMultiply(&matWorldViewProj, &matTemp, &matProjShader);
			//matrices must be transposed before loading into vertex shader registers
			WWD3D::MatrixTranspose(&matWorldViewProj, &matWorldViewProj);
			RenderDevice::Set_Vertex_Shader_Constant(RENDER_PIECE_REG(RenderSeaWaveVS, worldViewProj), reinterpret_cast<const float*>(&matWorldViewProj), 4);	//pass transform matrix into shader

			RenderDevice::Draw_Indexed_Primitive(WW3D_PT_TRIANGLESTRIP,0,0,m_numVertices,0,m_numIndices);
		}
	}
//	RenderDevice::Set_Render_State(WW3D_RS_FILLMODE,WW3D_FILL_SOLID);
	RenderDevice::Set_Render_State(WW3D_RS_ALPHABLENDENABLE , FALSE);
	RenderDevice::Set_Device_Texture( 0, nullptr);	//release reference to bump texture
	RenderDevice::Set_Device_Texture( 1, nullptr);	//release reference to reflection texture
	RenderDevice::Set_Device_Texture( 2, nullptr);	//release reference to reflection texture

	RenderDevice::Set_Texture_Stage(0, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_PASSTHRU|0)
		.Tex_Transform(WW3D_TTFF_DISABLE)
	);
	RenderDevice::Set_Texture_Stage(1, TextureStageDesc()
		.Tex_Coord_Index(WW3D_TCI_PASSTHRU|1)
		.Tex_Transform(WW3D_TTFF_DISABLE)
	);
	RenderDevice::Set_Render_State(WW3D_RS_ZWRITEENABLE , TRUE);

	RenderDevice::Set_Sampler_State(1,WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
	RenderDevice::Set_Sampler_State(1,WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);

	RenderDevice::Set_Render_State(WW3D_RS_WRAP0, 0);	//turn off texture wrapping

	RenderDevice::Disable_Texture_Stage(0);
	RenderDevice::Disable_Texture_Stage(1);
	RenderDevice::Disable_Texture_Stage(2);

	//Restore old transforms
	RenderDevice::_Set_Device_Transform(WW3D_TS_VIEW, *reinterpret_cast<Matrix4x4*>(&matView));
	RenderDevice::_Set_Device_Transform(WW3D_TS_PROJECTION, *reinterpret_cast<Matrix4x4*>(&matProj));

	RenderDevice::Set_Pixel_Shader_Object(nullptr);	//turn off pixel shader
	// Restore fixed-function input through the cached vertex declaration derived from the FVF.
	RenderDevice::Set_Vertex_Shader_Object(nullptr);	//turn off custom vertex shader
	RenderDevice::Set_Vertex_Declaration(RenderDevice::Get_Vertex_Declaration_For_FVF(RENDER_FVF_XYZDUV1));

	RenderDevice::Invalidate_Cached_Render_States();

	if (TheTerrainRenderObject->getShroud())
	{
		//do second pass to apply the shroud on water plane
		W3DShaderManager::setTexture(0,TheTerrainRenderObject->getShroud()->getShroudTexture());
		W3DShaderManager::setShader(W3DShaderManager::ST_SHROUD_TEXTURE, 0);
		RenderDevice::Set_Stream_Source(0,m_vertexBufferD3D,0,sizeof(WaterRenderObjClass::SEA_PATCH_VERTEX));
		RenderDevice::Set_Indices(m_indexBufferD3D);
		for (startY=patchY=static_cast<Int>((seaBox.Center.Y-seaBox.Extent.Y)/(PATCH_WIDTH*PATCH_SCALE)); (patchY*PATCH_WIDTH*PATCH_SCALE)<(seaBox.Center.Y+seaBox.Extent.Y); patchY++)
		{
			for (startX=patchX=static_cast<Int>((seaBox.Center.X-seaBox.Extent.X)/(PATCH_WIDTH*PATCH_SCALE)); (patchX*PATCH_WIDTH*PATCH_SCALE)<(seaBox.Center.X+seaBox.Extent.X); patchX++)
			{
				WWD3DMatrix matTemp;
				patchMatrix._41=static_cast<float>(patchX*PATCH_WIDTH*PATCH_SCALE);
				patchMatrix._43=static_cast<float>(patchY*PATCH_WIDTH*PATCH_SCALE);

				WWD3D::MatrixMultiply(&matTemp, &patchMatrix, &matWW3D);

				RenderDevice::_Set_Device_Transform(WW3D_TS_WORLD, *reinterpret_cast<Matrix4x4*>(&matTemp));

				RenderDevice::Draw_Indexed_Primitive(WW3D_PT_TRIANGLESTRIP,0,0,m_numVertices,0,m_numIndices);
			}
		}
		W3DShaderManager::resetShader(W3DShaderManager::ST_SHROUD_TEXTURE);
	}

}

constexpr auto FEATHER_THICKNESS = 4.0f;

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the water surface.*/
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::renderWater(void)
{
	// The scene copy is taken once per pass before the first water surface, so it holds the ground
	// and the depth code but no water yet.
	m_sceneCopyPending = TRUE;
	m_sceneCopyValid = FALSE;
	for (PolygonTrigger *pTrig=PolygonTrigger::getFirstPolygonTrigger(); pTrig; pTrig = pTrig->getNext()) {
		if (pTrig->isWaterArea()) {
			if (pTrig->getNumPoints()>2) {
				if (pTrig->isRiver()) {
					drawRiverWater(pTrig);
					continue;
				} 
				Int k;
				for (k=1; k<pTrig->getNumPoints()-1; k=k+2) {
					ICoord3D pt3 = *pTrig->getPoint(0);
					ICoord3D pt2 = *pTrig->getPoint(k);
					ICoord3D pt1 = *pTrig->getPoint(k+1);
					ICoord3D pt0 = *pTrig->getPoint(k+1);
					if (k+2<pTrig->getNumPoints()) {
						pt0 = *pTrig->getPoint(k+2);
					}
					Vector3 points[4];
					points[0].Set(static_cast<float>(pt0.x), static_cast<float>(pt0.y), static_cast<float>(pt0.z));
					points[1].Set(static_cast<float>(pt1.x), static_cast<float>(pt1.y), static_cast<float>(pt1.z));
					points[2].Set(static_cast<float>(pt2.x), static_cast<float>(pt2.y), static_cast<float>(pt2.z));
					points[3].Set(static_cast<float>(pt3.x), static_cast<float>(pt3.y), static_cast<float>(pt3.z));

					if ( TheGlobalData->m_featherWater )
					{
						for (int r = 0; r < TheGlobalData->m_featherWater; ++r)
						{
							drawTrapezoidWater(points);
							points[0].Z += (FEATHER_THICKNESS/TheGlobalData->m_featherWater);
						}
					}

					else
						drawTrapezoidWater(points);


				}
			}
		}
	}

}
