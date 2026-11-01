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

// Rivers and standing water areas (polygon triggers) and their shaders

#define NOISE_REPEAT_FACTOR ((float)(1.0f/(16.0f)))

#define FEATHER_LAYER_COUNT (5.0f)

constexpr auto REFLECTION_FACTOR = 0.1f;

static Bool wireframeForDebug = 0;

void WaterRenderObjClass::setupJbaWaterShader(void) 
{
	if (!TheWaterTransparency->m_additiveBlend)
		RenderDevice::Set_Shader(ShaderClass::_PresetAlphaShader);
	else
		RenderDevice::Set_Shader(ShaderClass::_PresetAdditiveShader);

	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	RenderDevice::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);
	TextureClass *riverTexture = waterColorTexture(TRUE);
	riverTexture->Get_Filter().Set_Mag_Filter(TextureFilterClass::FILTER_TYPE_BEST);
	riverTexture->Get_Filter().Set_Min_Filter(TextureFilterClass::FILTER_TYPE_BEST);
	riverTexture->Get_Filter().Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_BEST);


//	Setting *setting=&m_settings[m_tod];	


	RenderDevice::Apply_Render_State_Changes();	//force update of view and projection matrices
	RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Alpha_Op(WW3D_TOP_ADD));
	if (!m_riverAlphaEdge->Is_Initialized())
		m_riverAlphaEdge->Init();
	RenderDevice::Set_Device_Texture(3,m_riverAlphaEdge->Peek_D3D_Texture());	
	RenderDevice::Set_Sampler_State(3, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
	RenderDevice::Set_Sampler_State(3, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);
	RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Tex_Coord_Index(0));
	RenderDevice::Set_Texture_Stage(1, TextureStageDesc().Tex_Coord_Index(0));
	RenderDevice::Set_Texture_Stage(3, TextureStageDesc().Tex_Coord_Index(1));
	
	Bool doSparkles = true;

	if (m_riverWaterPixelShader && doSparkles) {
		if (!m_waterSparklesTexture->Is_Initialized())
			m_waterSparklesTexture->Init();
		RenderDevice::Set_Device_Texture(1,m_waterSparklesTexture->Peek_D3D_Texture());	

		if (!m_waterNoiseTexture->Is_Initialized())
			m_waterNoiseTexture->Init();
		RenderDevice::Set_Device_Texture(2,m_waterNoiseTexture->Peek_D3D_Texture());	

		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);
 
		RenderDevice::Set_Texture_Stage(2, TextureStageDesc().Tex_Coord_Index(WW3D_TCI_CAMERASPACEPOSITION));
		// Two output coordinates are used.
		RenderDevice::Set_Texture_Stage(2, TextureStageDesc().Tex_Transform(WW3D_TTFF_COUNT2));
		RenderDevice::Set_Sampler_State(2, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
		RenderDevice::Set_Sampler_State(2, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);
		WWD3DMatrix inv;
		float det;

		Matrix4x4 curView;
		RenderDevice::_Get_Device_Transform(WW3D_TS_VIEW, curView);
		WWD3D::MatrixInverse(&inv, &det, reinterpret_cast<WWD3DMatrix*>(&curView));
		WWD3DMatrix scale;

		WWD3D::MatrixScaling(&scale, NOISE_REPEAT_FACTOR, NOISE_REPEAT_FACTOR,1);
		WWD3DMatrix destMatrix = inv * scale;
		WWD3D::MatrixTranslation(&scale, m_riverVOrigin, m_riverVOrigin,0);
		destMatrix = destMatrix*scale;
		RenderDevice::_Set_Device_Transform(WW3D_TS_TEXTURE2, *reinterpret_cast<Matrix4x4*>(&destMatrix));
		
	}
	RenderDevice::Set_Sampler_State(0,WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR );
	RenderDevice::Set_Sampler_State(0,WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR );
	RenderDevice::Set_Sampler_State(1,WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR );
	RenderDevice::Set_Sampler_State(1,WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR );
	RenderDevice::Set_Sampler_State(2,WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR );
	RenderDevice::Set_Sampler_State(2,WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR );
	RenderDevice::Set_Sampler_State(3,WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR );
	RenderDevice::Set_Sampler_State(3,WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR );
	if (m_riverWaterPixelShader){
		RenderDevice::Set_Pixel_Shader_Constant(0, Vector4(REFLECTION_FACTOR, REFLECTION_FACTOR, REFLECTION_FACTOR, 1.0f));
		RenderDevice::Set_Pixel_Shader_Object(m_riverWaterPixelShader);
	}
}

//-------------------------------------------------------------------------------------------------
//Draw a many sided river polygon.
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::drawRiverWater(PolygonTrigger *pTrig)
{
	RenderDevice::Invalidate_Cached_Render_States();	///@todo: Figure out why rivers don't draw without reset of all states.

	Int rectangleCount = pTrig->getNumPoints()/2;
	rectangleCount--;

	Real bumpFactor = 5;
	static Bool doWobble = true;

	if (m_disableRiver) return;
	m_drawingRiver = true;

	//allocate 2 triangles per side with 3 indices per triangle
	DynamicIBAccessClass ib_access(BUFFER_TYPE_DYNAMIC_DEVICE,static_cast<unsigned short>((rectangleCount+1)*2*3));
	{
		DynamicIBAccessClass::WriteLockClass lockib(&ib_access);
 		UnsignedShort *curIb = lockib.Get_Index_Array();
		try {
		for (Int i=0; i<rectangleCount; i++)
		{
			//triangle 1
			curIb[0] = static_cast<UnsignedShort>(i*2);
			curIb[1] = static_cast<UnsignedShort>(i*2+1);
			curIb[2] = static_cast<UnsignedShort>(i*2+3);

			//triangle 2
			curIb[3] = static_cast<UnsignedShort>(i*2);
			curIb[4] = static_cast<UnsignedShort>(i*2+3);
			curIb[5] = static_cast<UnsignedShort>(i*2+2);

			curIb += 6;	//skip the 6 indices we just added.
		}
		IndexBufferExceptionFunc();
		} catch(...) {
			IndexBufferExceptionFunc();
		}
	}

	Real shadeR=TheWaterTransparency->m_standingWaterColor.red;
	Real shadeG=TheWaterTransparency->m_standingWaterColor.green;
	Real shadeB=TheWaterTransparency->m_standingWaterColor.blue;

	//If the water color is not overridden, use legacy lighting code.
	if ( shadeR==1.0f && shadeG==1.0f && shadeB==1.0f)
	{
		shadeR = TheGlobalData->m_terrainAmbient[0].red;
		shadeG = TheGlobalData->m_terrainAmbient[0].green;
		shadeB = TheGlobalData->m_terrainAmbient[0].blue;

		//Add in diffuse lighting from each terrain light
		for (Int lightIndex=0; lightIndex < TheGlobalData->m_numGlobalLights; lightIndex++)
		{
			if (-TheGlobalData->m_terrainLightPos[lightIndex].z > 0)
			{	shadeR += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].red;
				shadeG += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].green;
				shadeB += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].blue;
			}
		}

		//Get water material colors
		Real waterShadeR = (m_settings[m_tod].waterDiffuse & 0xff) / 255.0f;
		Real waterShadeG = ((m_settings[m_tod].waterDiffuse >> 8) & 0xff) / 255.0f;
		Real waterShadeB = ((m_settings[m_tod].waterDiffuse >> 16) & 0xff) / 255.0f;

		shadeR=shadeR*waterShadeR*255.0f;
		shadeG=shadeG*waterShadeG*255.0f;
		shadeB=shadeB*waterShadeB*255.0f;
	}
	else
	{
		shadeR=shadeR*255.0f;
		shadeG=shadeG*255.0f;
		shadeB=shadeB*255.0f;

		if (shadeR == 0 && shadeG == 0 && shadeB == 0)
		{	//special case where we disable lighting
			shadeR=255;
			shadeG=255;
			shadeB=255;
		}
	}

	Int diffuse=REAL_TO_INT(shadeB) | (REAL_TO_INT(shadeG) << 8) | (REAL_TO_INT(shadeR) << 16);

	//Keep diffuse from lighting calculations but substitute custom alpha
	diffuse |= m_settings[m_tod].waterDiffuse & 0xff000000;	//copy alpha/opacity from ini setting

	Int innerNdx = pTrig->getRiverStart();
	Int outerNdx = innerNdx+1;

	Real endLen=0;
	Real totalLen=0;
	Int i;
	for (i=0; i<pTrig->getNumPoints()-1; i++) {
		ICoord3D innerPt = *pTrig->getPoint(i);
		ICoord3D outerPt = *pTrig->getPoint(i+1);
		Real dx = static_cast<Real>(innerPt.x-outerPt.x);
		Real dy = static_cast<Real>(innerPt.y-outerPt.y);
		Real curLen = sqrt(dx*dx+dy*dy);
		totalLen += curLen;
		if ( i==innerNdx) {
			endLen = curLen;
		}
	}
	bumpFactor = endLen/BUMP_SIZE;

	Real lengthOfRiver = (totalLen/2)-endLen;
	Real repeatCount = lengthOfRiver / (endLen); 

	Real vScale=static_cast<Real>(repeatCount)/static_cast<Real>(rectangleCount);

#define HEIGHT_TO_USE (0.5f)
	if (innerNdx >= pTrig->getNumPoints()-1) return;
	//allocate 2 vertices per side
	DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DEVICE,dynamic_fvf_type,static_cast<unsigned short>((rectangleCount+1)*2));
	{
		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* vb=lock.Get_Formatted_Vertex_Array();

		Real constA=3*m_riverVOrigin;

		for (i=0; i<(pTrig->getNumPoints()/2); i++)
		{
			Real x,y;
			ICoord3D innerPt = *pTrig->getPoint(outerNdx);
			ICoord3D outerPt = *pTrig->getPoint(innerNdx);
			outerNdx++;
			innerNdx--;
			if (innerNdx<0) {
				innerNdx = pTrig->getNumPoints()-1;
			}
			if (outerNdx >= pTrig->getNumPoints()) {
				outerNdx = 0;
			}
			x=static_cast<Real>(innerPt.x);
			y=static_cast<Real>(innerPt.y);

			vb->x=x;
			vb->y=y;

			vb->z=static_cast<float>(innerPt.z);
			vb->diffuse= diffuse;
	
			Real wobbleConst=-m_riverVOrigin+vScale*static_cast<Real>(i) + WWMath::Fast_Sin(2*PI*(vScale*static_cast<Real>(i)) - constA)/22.0f;
 			//old slower version
			//vb->v1=-m_riverVOrigin+vScale*(Real)i + wobble(vScale*i, m_riverVOrigin, doWobble);
			vb->v1=wobbleConst;
			vb->u1=HEIGHT_TO_USE ;
			//old slower version
			//vb->v2 = -m_riverVOrigin+vScale*(Real)i + wobble(vScale*i, m_riverVOrigin, doWobble);
			vb->v2=wobbleConst;
			vb->u2 = 1.0f;
			vb->nx = 0;
			vb->ny = 0;
			vb->nz = 1.0f;
			vb++;

			x=static_cast<Real>(outerPt.x);
			y=static_cast<Real>(outerPt.y);

			vb->x=x;
			vb->y=y;
			vb->z=static_cast<float>(outerPt.z);
			vb->diffuse= diffuse;
 			//old slower version
			//vb->v1=-m_riverVOrigin+vScale*(Real)i + wobble(vScale*i, m_riverVOrigin, doWobble);
			vb->v1=wobbleConst;
			vb->u1=0;
			//old slower version
 			//vb->v2 = -m_riverVOrigin+vScale*(Real)i + wobble(vScale*i, m_riverVOrigin, doWobble);
			vb->v2 =wobbleConst;
			vb->u2 = 0;
			vb->nx = 0;
			vb->ny = 0;
			vb->nz = 1.0f;
			vb++;

		}
	}

	Matrix3D tm(1);

	RenderDevice::Set_Transform(WW3D_TS_WORLD,tm);	//position the water surface
	RenderDevice::Set_Index_Buffer(ib_access,0);
	RenderDevice::Set_Vertex_Buffer(vb_access);
	RenderDevice::Set_Texture(0,waterColorTexture(TRUE));	//set to blue

	setupJbaWaterShader();

	//In additive blending we need to use the alpha at the edges of river to darken
	//rgb instead.
	if (TheWaterTransparency->m_additiveBlend)
		RenderDevice::Set_Render_State(WW3D_RS_SRCBLEND, WW3D_BLEND_SRCALPHA );

	if (m_riverWaterPixelShader) RenderDevice::Set_Pixel_Shader_Object(m_riverWaterPixelShader);
	// Water version 2 rivers: flow, reflection, sun glint and foam; color, opacity and blending
	// stay as in the original.
	const Bool river2 = m_riverWaterPixelShader && setupRiverWater2();
	// The wrapper keeps a shadow of the render states; there is no read back from the device.
	// ShaderClass::Apply sets CULLMODE through the wrapper, so the shadow is valid. If it still
	// holds the placeholder from RenderDevice::Init, use the value of its state reset.
	DWORD cull = RenderDevice::Get_Render_State(WW3D_RS_CULLMODE);
	if (cull == 0x12345678) cull = WW3D_CULL_CW;
	RenderDevice::Set_Render_State(WW3D_RS_CULLMODE, WW3D_CULL_NONE);



	if (wireframeForDebug) {
		RenderDevice::Set_Render_State(WW3D_RS_FILLMODE,WW3D_FILL_WIREFRAME);
	}
	RenderDevice::Draw_Triangles(	0,static_cast<unsigned short>(rectangleCount*2), 0,	static_cast<unsigned short>((rectangleCount+1)*2));
	if (wireframeForDebug) {
		RenderDevice::Set_Render_State(WW3D_RS_FILLMODE,WW3D_FILL_SOLID);
	}
	if (river2)
		cleanupRealWaterShader();	// same stages as for standing water

	if (m_riverWaterPixelShader) RenderDevice::Set_Pixel_Shader_Object(nullptr);

	//restore blend mode to what W3D expects.
	if (TheWaterTransparency->m_additiveBlend)
		RenderDevice::Set_Render_State(WW3D_RS_SRCBLEND, WW3D_BLEND_ONE );

	//do second pass to apply the shroud on water plane
	if (TheTerrainRenderObject->getShroud())
	{
		W3DShaderManager::setTexture(0,TheTerrainRenderObject->getShroud()->getShroudTexture());
		W3DShaderManager::setShader(W3DShaderManager::ST_SHROUD_TEXTURE, 0);
		RenderDevice::Set_Render_State(WW3D_RS_CULLMODE, WW3D_CULL_NONE);
		//Shroud shader uses z-compare of EQUAL which wouldn't work on water because it doesn't
		//write to the zbuffer.  Change to LESSEQUAL.
		RenderDevice::Set_Render_State(WW3D_RS_ZFUNC, WW3D_CMP_LESSEQUAL);
		RenderDevice::Draw_Triangles(	0,static_cast<unsigned short>(rectangleCount*2), 0,	static_cast<unsigned short>((rectangleCount+1)*2));
		RenderDevice::Set_Render_State(WW3D_RS_ZFUNC, WW3D_CMP_EQUAL);
		W3DShaderManager::resetShader(W3DShaderManager::ST_SHROUD_TEXTURE);
	}
	RenderDevice::Set_Render_State(WW3D_RS_CULLMODE, cull);


}

void WaterRenderObjClass::setupFlatWaterShader(void) 
{

	TextureClass *waterTexture = waterColorTexture(FALSE);
	RenderDevice::Set_Texture(0,waterTexture);
	if (!TheWaterTransparency->m_additiveBlend)
		RenderDevice::Set_Shader(ShaderClass::_PresetAlphaShader);
	else
		RenderDevice::Set_Shader(ShaderClass::_PresetAdditiveShader);

	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	RenderDevice::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);
	waterTexture->Get_Filter().Set_Mag_Filter(TextureFilterClass::FILTER_TYPE_BEST);
	waterTexture->Get_Filter().Set_Min_Filter(TextureFilterClass::FILTER_TYPE_BEST);
	waterTexture->Get_Filter().Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_BEST);

	RenderDevice::Apply_Render_State_Changes();	//force update of view and projection matrices

	//Setup shroud to render in same pass as water
	if (m_trapezoidWaterPixelShader)
	{	if (TheTerrainRenderObject->getShroud())
		{
			W3DShaderManager::setTexture(0,TheTerrainRenderObject->getShroud()->getShroudTexture());
			//Use stage 3 to apply the shroud
			W3DShaderManager::setShader(W3DShaderManager::ST_SHROUD_TEXTURE, 3);
			//Shroud shader uses z-compare of EQUAL which wouldn't work on water because it doesn't
			//write to the zbuffer.  Change to LESSEQUAL.
			RenderDevice::Set_Render_State(WW3D_RS_ZFUNC, WW3D_CMP_LESSEQUAL);
		}
		else
		{	//Assume no shroud, so stage 3 will be "NULL" texture but using actual white because
			//pixel shader on GF4 generates random colors with SetTexture(3,NULL).
			if (!m_whiteTexture->Is_Initialized())
			{	m_whiteTexture->Init();
				SurfaceClass *surface=m_whiteTexture->Get_Surface_Level();
				surface->DrawPixel(0,0,0xffffffff);
				REF_PTR_RELEASE(surface);
			}
			RenderDevice::Set_Device_Texture(3,m_whiteTexture->Peek_D3D_Texture());	
		}
	}

	RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Alpha_Op(WW3D_TOP_ADD).Tex_Coord_Index(0));
	RenderDevice::Set_Texture_Stage(1, TextureStageDesc().Tex_Coord_Index(0));
	
	Bool doSparkles = true;

	if (m_trapezoidWaterPixelShader && doSparkles) {

		if (!m_waterSparklesTexture->Is_Initialized())
			m_waterSparklesTexture->Init();

		RenderDevice::Set_Device_Texture(1,m_waterSparklesTexture->Peek_D3D_Texture());	

		if (!m_waterNoiseTexture->Is_Initialized())
			m_waterNoiseTexture->Init();

		RenderDevice::Set_Device_Texture(2,m_waterNoiseTexture->Peek_D3D_Texture());	

		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
		RenderDevice::Set_Sampler_State(1, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);
 
		RenderDevice::Set_Texture_Stage(2, TextureStageDesc().Tex_Coord_Index(WW3D_TCI_CAMERASPACEPOSITION));
		// Two output coordinates are used.
		RenderDevice::Set_Texture_Stage(2, TextureStageDesc().Tex_Transform(WW3D_TTFF_COUNT2));
		RenderDevice::Set_Sampler_State(2, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
		RenderDevice::Set_Sampler_State(2, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);
		WWD3DMatrix inv;
		float det;

		Matrix4x4 curView;
		RenderDevice::_Get_Device_Transform(WW3D_TS_VIEW, curView);
		WWD3D::MatrixInverse(&inv, &det, reinterpret_cast<WWD3DMatrix*>(&curView));
		WWD3DMatrix scale;

		WWD3D::MatrixScaling(&scale, NOISE_REPEAT_FACTOR, NOISE_REPEAT_FACTOR,1);
		WWD3DMatrix destMatrix = inv * scale;
		WWD3D::MatrixTranslation(&scale, m_riverVOrigin, m_riverVOrigin,0);
		destMatrix = destMatrix*scale;
		RenderDevice::_Set_Device_Transform(WW3D_TS_TEXTURE2, *reinterpret_cast<Matrix4x4*>(&destMatrix));

	}
	RenderDevice::Set_Sampler_State(0,WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR );
	RenderDevice::Set_Sampler_State(0,WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR );
	RenderDevice::Set_Sampler_State(1,WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR );
	RenderDevice::Set_Sampler_State(1,WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR );
	RenderDevice::Set_Sampler_State(2,WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR );
	RenderDevice::Set_Sampler_State(2,WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR );
	if (m_trapezoidWaterPixelShader){
		RenderDevice::Set_Pixel_Shader_Constant(0, Vector4(REFLECTION_FACTOR, REFLECTION_FACTOR, REFLECTION_FACTOR, 1.0f));
		RenderDevice::Set_Pixel_Shader_Object(m_trapezoidWaterPixelShader);
	}
}

//-------------------------------------------------------------------------------------------------
//Draw a 4 sided flat water area.
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::drawTrapezoidWater(Vector3 points[4])
{
	Vector3 origin(points[0]);
	Vector3 uVec1(points[1]);
	Vector3 vVec1(points[3]);
	Vector3 uVec2(points[2]);
	Vector3 vVec2(points[2]);
	uVec2 -= vVec1;
	vVec2	-= uVec1;
	uVec1 -= origin;
	vVec1 -= origin;
	Int uCount = static_cast<Int>((uVec1.Length()+uVec2.Length()) / (8*MAP_XY_FACTOR));
	if (uCount<1) uCount = 1;
	Int vCount = static_cast<Int>((vVec1.Length()+vVec2.Length()) / (8*MAP_XY_FACTOR));
	if (vCount<1) vCount = 1;	

	if (uCount>50) uCount = 50;
	if (vCount>50) vCount = 50;

	static Bool doWobble = true;

	Int rectangleCount = uCount*vCount;

	uCount++;
	vCount++;

	Int i, j;
	//allocate 2 triangles per side with 3 indices per triangle
	DynamicIBAccessClass ib_access(BUFFER_TYPE_DYNAMIC_DEVICE,static_cast<unsigned short>((rectangleCount+1)*2*3));
	{
		DynamicIBAccessClass::WriteLockClass lockib(&ib_access);
 		UnsignedShort *curIb = lockib.Get_Index_Array();
		try {
		for (j=0; j<vCount-1; j++)
		{	for (i=0; i<uCount-1; i++)
			{
				//triangle 1
				curIb[0] = static_cast<UnsignedShort>((j)*uCount + i);
				curIb[1] = static_cast<UnsignedShort>((j+1)*uCount + i+1);
				curIb[2] = static_cast<UnsignedShort>((j+1)*uCount + i);

				//triangle 2
				curIb[3] = static_cast<UnsignedShort>((j)*uCount + i);
				curIb[4] = static_cast<UnsignedShort>((j)*uCount + i+1);
				curIb[5] = static_cast<UnsignedShort>((j+1)*uCount + i+1);

				curIb += 6;	//skip the 6 indices we just added.
			}
		}
		IndexBufferExceptionFunc();
		} catch(...) {
			IndexBufferExceptionFunc();
		}
	}

	Real	waterFactor=150;
	Real shadeR=TheWaterTransparency->m_standingWaterColor.red;
	Real shadeG=TheWaterTransparency->m_standingWaterColor.green;
	Real shadeB=TheWaterTransparency->m_standingWaterColor.blue;

	//If the water color is not overridden, use legacy lighting code.
	if ( shadeR==1.0f && shadeG==1.0f && shadeB==1.0f)
	{
		shadeR = TheGlobalData->m_terrainAmbient[0].red;
		shadeG = TheGlobalData->m_terrainAmbient[0].green;
		shadeB = TheGlobalData->m_terrainAmbient[0].blue;

		//Add in diffuse lighting from each terrain light
		for (Int lightIndex=0; lightIndex < TheGlobalData->m_numGlobalLights; lightIndex++)
		{
			if (-TheGlobalData->m_terrainLightPos[lightIndex].z > 0)
			{	shadeR += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].red;
				shadeG += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].green;
				shadeB += -TheGlobalData->m_terrainLightPos[lightIndex].z * TheGlobalData->m_terrainDiffuse[lightIndex].blue;
			}
		}

		//Get water material colors
		Real waterShadeR = (m_settings[m_tod].waterDiffuse & 0xff) / 255.0f;
		Real waterShadeG = ((m_settings[m_tod].waterDiffuse >> 8) & 0xff) / 255.0f;
		Real waterShadeB = ((m_settings[m_tod].waterDiffuse >> 16) & 0xff) / 255.0f;

		shadeR=shadeR*waterShadeR*255.0f;
		shadeG=shadeG*waterShadeG*255.0f;
		shadeB=shadeB*waterShadeB*255.0f;
	}
	else
	{
		shadeR=shadeR*255.0f;
		shadeG=shadeG*255.0f;
		shadeB=shadeB*255.0f;

		if (shadeR == 0 && shadeG == 0 && shadeB == 0)
		{	//special case where we disable lighting
			shadeR=255;
			shadeG=255;
			shadeB=255;
		}
	}

	Int diffuse=REAL_TO_INT(shadeB) | (REAL_TO_INT(shadeG) << 8) | (REAL_TO_INT(shadeR) << 16);

	//Keep diffuse from lighting calculations but substitute custom alpha
	diffuse |= m_settings[m_tod].waterDiffuse & 0xff000000;	//copy alpha/opacity from ini setting

	DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DEVICE,dynamic_fvf_type,static_cast<unsigned short>((rectangleCount+1)*2));

//#define WAVY_WATER
//#define FEATHER_LAYER_COUNT (3) //LORENZEN
//#define FEATHER_LAYER_THICKNESS (2.5f)
//#define FEATHER_WATER

//#ifdef WAVY_WATER // the NEW WATER a'la LORENZEN
	if ( TheGlobalData->m_featherWater )
	{

		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* vb=lock.Get_Formatted_Vertex_Array();

		Real phase = 0;
		Real mapCoeff = PI/(4*MAP_XY_FACTOR);
		Real wave = 0;
		Real amplitude = 0.5f;

		//The first (high order) byte is the Alpha value for this patch
		// It needs to be set proportional to the number of feather layers
		// this comes from TheGlobalData->m_featherWater, which is a count of layers


		Int Alpha = 0;
		if ( TheGlobalData->m_featherWater == 5) Alpha = 80;
		if ( TheGlobalData->m_featherWater == 4) Alpha = 110;
		if ( TheGlobalData->m_featherWater == 3) Alpha = 140;
		if ( TheGlobalData->m_featherWater == 2) Alpha = 200;
		if ( TheGlobalData->m_featherWater == 1) Alpha = 255;

		//Keep diffuse from lighting calculations but substitute custom alpha
		Int customDiffuse = (diffuse & 0x00ffffff) | (Alpha<< 24);//(0x80 << 16)|(0x90 << 8)|0xa0;

		for (j=0; j<vCount; j++)
		{
			Real dv = static_cast<Real>(j);
			dv /= (vCount-1);
			for (i=0; i<uCount; i++)
			{
				Real du = static_cast<Real>(i);
				du /= (uCount-1);
				Vector3 vertex = origin;
				vertex += uVec1*du;
				vertex += vVec1*dv;
				vertex += (dv)*(du)*(vVec2-vVec1);

				vb->x=vertex.X;
				vb->y=vertex.Y;

				// common to all the waving effects
				phase = 25 * m_riverVOrigin + vertex.X * mapCoeff;
				wave = (sin(phase) - 1.0f) * amplitude;

				vb->z = (vertex.Z + wave);
				vb->diffuse = customDiffuse;
				vb->u1 = static_cast<float>((vertex.X/waterFactor) + 0.02*cos(11*m_riverVOrigin)*wave);
				vb->v1 = static_cast<float>((vertex.Y/waterFactor) + 0.02*cos(5*m_riverVOrigin)*wave);
				vb->u2 = vertex.X/BUMP_SIZE;
				vb->v2 = vertex.Y/BUMP_SIZE + 0.3f*vertex.X/BUMP_SIZE;
				vb->nx = 0;
				vb->ny = 0;
				vb->nz = 1.0f;
				vb++;
			}
		}
	}
//#else // STILL THE OLD FLAT WATER
	else

	{
		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* vb=lock.Get_Formatted_Vertex_Array();

		//Pulling some constants out of the inner loops to improve performance -MW
		Real constA=static_cast<Real>(0.02*cos(11*m_riverVOrigin));
		Real constB=static_cast<Real>(0.02*cos(5*m_riverVOrigin));
		Real constC=25*m_riverVOrigin;
		Real ooWaterFactor = 1.0f/waterFactor;
		const Real constD=PI/(4*MAP_XY_FACTOR);
		Real constE=1.0f/static_cast<Real>(vCount-1);
		Real constF=1.0f/static_cast<Real>(uCount-1);

		for (j=0; j<vCount; j++)
		{
			Real dv = static_cast<Real>(j) * constE;

			for (i=0; i<uCount; i++)
			{
				Real du = static_cast<Real>(i) * constF;
				Vector3 vertex = origin;
				vertex += uVec1*du;
				vertex += vVec1*dv;
				vertex += (dv)*(du)*(vVec2-vVec1);

				vb->x=vertex.X;
				vb->y=vertex.Y;
				vb->z=vertex.Z;

				vb->diffuse= diffuse;
				//Old slower version
 				//vb->u1=(vertex.X/waterFactor) + 0.02*cos(11*m_riverVOrigin)*sin(25*m_riverVOrigin+vertex.X*PI/(4*MAP_XY_FACTOR));
 				//vb->v1=(vertex.Y/waterFactor) + 0.02*cos(5*m_riverVOrigin)*sin(25*m_riverVOrigin+vertex.Y*PI/(4*MAP_XY_FACTOR));
				vb->u1=vertex.X*ooWaterFactor + constA*WWMath::Fast_Sin(constC+vertex.X*constD);
				vb->v1=vertex.Y*ooWaterFactor + constB*WWMath::Fast_Sin(constC+vertex.Y*constD);
				vb->u2 = vertex.X/BUMP_SIZE;
				//Old slower version
 				//vb->v2 = vertex.Y/BUMP_SIZE + 0.3f*vertex.X/BUMP_SIZE;
				vb->v2 = (vertex.Y+0.3f*vertex.X)/BUMP_SIZE;
				vb->nx = 0;
				vb->ny = 0;
				vb->nz = 1.0f;
				vb++;
			}
		}
	}

//#endif // OLD VS NEW WATER



	Matrix3D tm(1);

	RenderDevice::Set_Transform(WW3D_TS_WORLD,tm);	//position the water surface
	RenderDevice::Set_Index_Buffer(ib_access,0);
	RenderDevice::Set_Vertex_Buffer(vb_access);

	setupFlatWaterShader();// lorenzen sez use the alpha shader

	// Realistic water blends itself (ground from the scene copy) and writes the final color;
	// destination alpha then carries the depth code.
	const Bool realWater = setupRealWaterShader();
	if (realWater)
	{	RenderDevice::Set_Render_State(WW3D_RS_SRCBLEND, WW3D_BLEND_ONE );
		RenderDevice::Set_Render_State(WW3D_RS_DESTBLEND, WW3D_BLEND_ZERO );
	}
	else
	//If video card supports it and it's enabled, feather the water edge using destination alpha
	if (RenderDevice::getBackBufferFormat() == WW3D_FORMAT_A8R8G8B8 && TheGlobalData->m_showSoftWaterEdge && TheWaterTransparency->m_transparentWaterDepth !=0)
	{		RenderDevice::Set_Render_State(WW3D_RS_SRCBLEND, WW3D_BLEND_DESTALPHA );
			if (!TheWaterTransparency->m_additiveBlend)
				RenderDevice::Set_Render_State(WW3D_RS_DESTBLEND, WW3D_BLEND_INVDESTALPHA );
	}


	// The wrapper keeps a shadow of the render states; there is no read back from the device.
	// ShaderClass::Apply sets CULLMODE through the wrapper, so the shadow is valid. If it still
	// holds the placeholder from RenderDevice::Init, use the value of its state reset.
	DWORD cull = RenderDevice::Get_Render_State(WW3D_RS_CULLMODE);
	if (cull == 0x12345678) cull = WW3D_CULL_CW;
	RenderDevice::Set_Render_State(WW3D_RS_CULLMODE, WW3D_CULL_NONE);



//#ifdef FEATHER_WATER // the NEW WATER a'la LORENZEN

//	int layer = 0;//LORENZEN
//	for (layer = 0; layer < FEATHER_LAYER_COUNT; ++layer)//LORENZEN
//#endif // FEATHER_WATER
	{
//#ifdef WAVY_WATER // the NEW WATER a'la LORENZEN

		//increment the depth of the water's surface for every vert in the buffer
//#ifdef  FEATHER_WATER
//		VertexFormatXYZNDUV2 *vertBuf = vertexBufferStart;
//		while (vertBuf < vertexBufferStart + vCount * uCount)
//		{
//			vertBuf->z *= FEATHER_LAYER_THICKNESS;
//			++vertBuf;
//		}
//#endif // FEATHER_WATER
//#endif //WAVY_WATER
		RenderDevice::Draw_Triangles(	0,static_cast<unsigned short>(rectangleCount*2), 0,	static_cast<unsigned short>((rectangleCount+1)*2));//lorenzen thinks this is where to itereate the soft shoreline effect
		// measurement helper CNC_WASSER_MESSUNG, no effect otherwise
		for (Int extra = realWater ? water2Measurement(TRUE) : 1; extra > 1; extra--)
			RenderDevice::Draw_Triangles(	0,static_cast<unsigned short>(rectangleCount*2), 0,	static_cast<unsigned short>((rectangleCount+1)*2));
	}

	if (realWater)
		cleanupRealWaterShader();




	if (false) {
		RenderDevice::Set_Render_State(WW3D_RS_FILLMODE,WW3D_FILL_WIREFRAME);
		RenderDevice::Set_Render_State(WW3D_RS_ALPHABLENDENABLE , false);
		RenderDevice::Draw_Triangles(	0,static_cast<unsigned short>(rectangleCount*2), 0,	static_cast<unsigned short>((rectangleCount+1)*2));
		RenderDevice::Set_Render_State(WW3D_RS_ALPHABLENDENABLE , true);
		RenderDevice::Set_Render_State(WW3D_RS_FILLMODE,WW3D_FILL_SOLID);
	}

	if (m_riverWaterPixelShader) RenderDevice::Set_Pixel_Shader_Object(nullptr);
	//Restore alpha blend to default values since we may have changed them to feather edges.
	if (!TheWaterTransparency->m_additiveBlend)
	{	RenderDevice::Set_Render_State(WW3D_RS_SRCBLEND, WW3D_BLEND_SRCALPHA );
		RenderDevice::Set_Render_State(WW3D_RS_DESTBLEND, WW3D_BLEND_INVSRCALPHA );
	}
	else
	{
		RenderDevice::Set_Render_State(WW3D_RS_SRCBLEND, WW3D_BLEND_ONE );
		RenderDevice::Set_Render_State(WW3D_RS_DESTBLEND, WW3D_BLEND_ONE );
	}

	if (TheTerrainRenderObject->getShroud())
	{
		if (m_trapezoidWaterPixelShader)
		{	//shroud was applied in stage3 of main pass so just need to restore state here.
			W3DShaderManager::resetShader(W3DShaderManager::ST_SHROUD_TEXTURE);
			RenderDevice::Set_Device_Texture(3,nullptr);	//free possible reference to shroud texture
			RenderDevice::Set_Render_State(WW3D_RS_ZFUNC, WW3D_CMP_EQUAL);
		}
		else
		{	//do second pass to apply the shroud on water plane for cards that can't do it in main pass.
			W3DShaderManager::setTexture(0,TheTerrainRenderObject->getShroud()->getShroudTexture());
			W3DShaderManager::setShader(W3DShaderManager::ST_SHROUD_TEXTURE, 0);
			RenderDevice::Set_Render_State(WW3D_RS_CULLMODE, WW3D_CULL_NONE);
			//Shroud shader uses z-compare of EQUAL which wouldn't work on water because it doesn't
			//write to the zbuffer.  Change to LESSEQUAL.
			RenderDevice::Set_Render_State(WW3D_RS_ZFUNC, WW3D_CMP_LESSEQUAL);
			RenderDevice::Draw_Triangles(	0,static_cast<unsigned short>(rectangleCount*2), 0,	static_cast<unsigned short>((rectangleCount+1)*2));
			RenderDevice::Set_Render_State(WW3D_RS_ZFUNC, WW3D_CMP_EQUAL);
			W3DShaderManager::resetShader(W3DShaderManager::ST_SHROUD_TEXTURE);
		}
	}
	RenderDevice::Set_Render_State(WW3D_RS_CULLMODE, cull);
}
