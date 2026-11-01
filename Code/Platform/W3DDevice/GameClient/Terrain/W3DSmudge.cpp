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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// W3DSmudge.cpp ////////////////////////////////////////////////////////////////////////////////
// Smudge System implementation
// Author: Mark Wilczynski, June 2003
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "Lib/BaseType.h"
#include "always.h"
#include "W3DDevice/GameClient/W3DSmudge.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "Common/GameMemory.h"
#include "GameClient/View.h"
#include "GameClient/Display.h"
#include "WW3D2/Material/texture.h"
#include "WW3D2/Buffers/indexbuffer.h"
#include "WW3D2/Common/renderdevice.h"
#include "WW3D2/Scene/rinfo.h"
#include "WW3D2/Scene/camera.h"
#include "WW3D2/Buffers/sortingrenderer.h"


// The heat haze renders into the screen filter texture (W3DShaderManager::startRenderToTexture) and would
// also sample it. D3D11 never binds a render target as input at the same time (the slot reads 0), so the
// target is first copied into a separate texture (like the water scene copy) and the copy is sampled.
static WW3DTextureHandle s_smudgeCopy = nullptr;
static unsigned s_smudgeCopyWidth = 0, s_smudgeCopyHeight = 0, s_smudgeCopyFormat = 0;

static void Release_Smudge_Copy(void)
{
	if (s_smudgeCopy)
		RenderDevice::Release_Resource(s_smudgeCopy);
	s_smudgeCopy = nullptr;
	s_smudgeCopyWidth = s_smudgeCopyHeight = s_smudgeCopyFormat = 0;
}

/// Sampling source: a copy of the current render target (nullptr if the copy fails).
static WW3DTextureHandle Smudge_Source(WW3DTextureHandle /*backTexture*/)
{
	WW3DSurfaceHandle target = nullptr;
	if (RenderDevice::Get_Render_Target(&target) != S_OK || !target)
		return nullptr;
	WW3DSurfaceDesc desc;
	::memset(&desc, 0, sizeof(desc));
	const HRESULT hr = RenderDevice::Get_Surface_Desc(target, &desc);
	RenderDevice::Release_Resource(target);
	if (hr != S_OK || desc.Width == 0 || desc.Height == 0)
		return nullptr;
	if (!s_smudgeCopy || desc.Width != s_smudgeCopyWidth || desc.Height != s_smudgeCopyHeight || desc.Format != s_smudgeCopyFormat)
	{
		Release_Smudge_Copy();
		if (RenderDevice::Create_Device_Render_Target_Texture(desc.Width, desc.Height, static_cast<WW3DDeviceFormat>(desc.Format), &s_smudgeCopy) != S_OK)
		{	s_smudgeCopy = nullptr;
			return nullptr;
		}
		s_smudgeCopyWidth = desc.Width;
		s_smudgeCopyHeight = desc.Height;
		s_smudgeCopyFormat = desc.Format;
	}
	return RenderDevice::Copy_Render_Target_To_Texture(s_smudgeCopy) ? s_smudgeCopy : nullptr;
}

W3DSmudgeManager::W3DSmudgeManager(void)
{
}

W3DSmudgeManager::~W3DSmudgeManager()
{
	ReleaseResources();
}

void W3DSmudgeManager::init(void)
{
	SmudgeManager::init();
	ReAcquireResources();
}

void W3DSmudgeManager::reset (void)
{
	SmudgeManager::reset();	//base
}

void W3DSmudgeManager::ReleaseResources(void)
{
	Release_Smudge_Copy();
#ifdef USE_COPY_RECTS
	REF_PTR_RELEASE(m_backgroundTexture);
#endif
	REF_PTR_RELEASE(m_indexBuffer);
}

//Make sure (SMUDGE_DRAW_SIZE * 12) < 65535 because that's the max index buffer size.
constexpr auto SMUDGE_DRAW_SIZE = 500;	//draw at most 50 smudges per call. Tweak value to improve CPU/GPU parallelism.

void W3DSmudgeManager::ReAcquireResources(void)
{
	ReleaseResources();

	SurfaceClass *surface=RenderDevice::_Get_Back_Buffer();
	SurfaceClass::SurfaceDescription surface_desc;

	surface->Get_Description(surface_desc);
	REF_PTR_RELEASE(surface);

	#ifdef USE_COPY_RECTS
	m_backgroundTexture = MSGNEW("TextureClass") TextureClass(TheTacticalView->getWidth(),TheTacticalView->getHeight(),surface_desc.Format,MIP_LEVELS_1,TextureClass::POOL_DEFAULT, true);
	#endif

	m_backBufferWidth = surface_desc.Width;
	m_backBufferHeight = surface_desc.Height;

	m_indexBuffer=NEW_REF(DeviceIndexBufferClass,(SMUDGE_DRAW_SIZE*4*3));	//allocate 4 triangles per smudge, each with 3 indices.

	// Fill up the IB with static vertex indices that will be used for all smudges.
	{
		DeviceIndexBufferClass::WriteLockClass lockIdxBuffer(m_indexBuffer);
		UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
		//quad of 4 triangles:
		//	0-----3
		//  |\   /|
		//  |  4  |
		//	|/   \|
		//  1-----2
		Int vbCount=0;
		for (Int i=0; i<SMUDGE_DRAW_SIZE; i++)
		{
			//Top
			ib[0]=static_cast<UnsignedShort>(vbCount);
			ib[1]=static_cast<UnsignedShort>(vbCount+4);
			ib[2]=static_cast<UnsignedShort>(vbCount+3);
			//Right
			ib[3]=static_cast<UnsignedShort>(vbCount+3);
			ib[4]=static_cast<UnsignedShort>(vbCount+4);
			ib[5]=static_cast<UnsignedShort>(vbCount+2);
			//Bottom
			ib[6]=static_cast<UnsignedShort>(vbCount+2);
			ib[7]=static_cast<UnsignedShort>(vbCount+4);
			ib[8]=static_cast<UnsignedShort>(vbCount+1);
			//Left
			ib[9]=static_cast<UnsignedShort>(vbCount+1);
			ib[10]=static_cast<UnsignedShort>(vbCount+4);
			ib[11]=static_cast<UnsignedShort>(vbCount+0);

			vbCount += 5;
			ib+=12;
		}
	}
}

/*Copies a portion of the current render target into a specified buffer*/
Int copyRect(unsigned char *buf, Int bufSize, int oX, int oY, int width, int height)
{
 	WW3DSurfaceHandle surface=nullptr;	///<previous render target
 	WW3DSurfaceHandle tempSurface=nullptr;
	Int result = 0;
	HRESULT hr = S_OK;

	// releases both surfaces on every way out of the function
	struct SurfaceRelease
	{
		WW3DSurfaceHandle &a;
		WW3DSurfaceHandle &b;
		~SurfaceRelease() { RenderDevice::Release_Resource(a); RenderDevice::Release_Resource(b); }
	} surfaceRelease{ surface, tempSurface };

	if (!RenderDevice::Has_Device())
		return result;

 	RenderDevice::Get_Render_Target(&surface);

	if (!surface)
		return result;

 	WW3DSurfaceDesc desc;

 	RenderDevice::Get_Surface_Desc(surface, &desc);

	RECT srcRect;
	srcRect.left=oX;
	srcRect.top=oY;
	srcRect.right=oX+width;
	srcRect.bottom=oY+height;

	POINT dstPoint;
	dstPoint.x=0;
	dstPoint.y=0;

 	hr=RenderDevice::Create_Device_Offscreen_Surface(width, height, desc.Format, &tempSurface);

	if (hr != S_OK)
		return result;

 	RenderDevice::_Copy_Rects(surface,&srcRect,1,tempSurface,&dstPoint);

 	WW3DLockedRect lrect;

 	hr=RenderDevice::Lock_Surface(tempSurface,&lrect,nullptr,WW3D_LOCK_READONLY);

	if (hr != S_OK)
		return result;

 	RenderDevice::Get_Surface_Desc(tempSurface, &desc);

	{
		// surface size in bytes from pitch and height
		LONG surfaceSize = lrect.Pitch * static_cast<LONG>(desc.Height);
		if (surfaceSize < bufSize)
			bufSize = surfaceSize;
	}
		
	memcpy(buf,lrect.pBits,bufSize);
	result = bufSize;

	return result;
}

constexpr auto UNIQUE_COLOR = 0x12345678;
#define BLOCK_SIZE	(8)

Bool W3DSmudgeManager::testHardwareSupport(void)
{
	if (m_hardwareSupportStatus == HardwareSmudgeSupport::SMUDGE_SUPPORT_UNKNOWN)
	{	//we have not done the test yet.

		WW3DTextureHandle backTexture=W3DShaderManager::getRenderTexture();
		if (!backTexture)
		{	//do trivial test first to see if render target exists.
			m_hardwareSupportStatus = HardwareSmudgeSupport::SMUDGE_SUPPORT_NO;
			return FALSE;
		}

		if (!W3DShaderManager::isRenderingToTexture())
			return FALSE;	//can't do the test unless we're rendering to texture.

		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		RenderDevice::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		
		ShaderClass shader=ShaderClass::_PresetOpaqueShader;
		shader.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);
		shader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);
		RenderDevice::Set_Shader(shader);
		RenderDevice::Set_Texture(0,nullptr);
		RenderDevice::Apply_Render_State_Changes();	//force update of view and projection matrices

		// Screen-space quad drawn with RenderDevice::Draw_Screen_Quad_UP.
		struct _TRANS_LIT_TEX_VERTEX {
			Vector3 p;	// pixel coordinates
			DWORD color;   // diffuse color
			float	u;
			float	v;
		} v[4];

		//bottom right
		v[0].p = Vector3( BLOCK_SIZE, BLOCK_SIZE, 0.0f );
		v[0].u = BLOCK_SIZE/static_cast<Real>(TheDisplay->getWidth());	v[0].v = BLOCK_SIZE/static_cast<Real>(TheDisplay->getHeight());;
		//top right
		v[1].p = Vector3( BLOCK_SIZE, 0, 0.0f );
		v[1].u = BLOCK_SIZE/static_cast<Real>(TheDisplay->getWidth());	v[1].v = 0;
		//bottom left
		v[2].p = Vector3(  0, BLOCK_SIZE, 0.0f );
		v[2].u = 0;	v[2].v = BLOCK_SIZE/static_cast<Real>(TheDisplay->getHeight());
		//top left
		v[3].p = Vector3(  0,  0, 0.0f );
		v[3].u = 0;	v[3].v = 0;

		v[0].color = UNIQUE_COLOR;
		v[1].color = UNIQUE_COLOR;
		v[2].color = UNIQUE_COLOR;
		v[3].color = UNIQUE_COLOR;

		//draw polygons like this is very inefficient but for only 2 triangles, it's
		//not worth bothering with index/vertex buffers.
		RenderDevice::Draw_Screen_Quad_UP(v, sizeof(_TRANS_LIT_TEX_VERTEX), WW3D_FVF_XYZ | WW3D_FVF_DIFFUSE | WW3D_FVF_TEX1);

		DWORD refData[BLOCK_SIZE*BLOCK_SIZE];
		memset(refData,0,sizeof(refData));
		Int bufSize=copyRect(reinterpret_cast<unsigned char *>(refData),sizeof(refData),0,0,BLOCK_SIZE,BLOCK_SIZE);	//copy area we just rendered using solid color
		if (!bufSize)
		{
			m_hardwareSupportStatus = HardwareSmudgeSupport::SMUDGE_SUPPORT_NO;
			return FALSE;
		}

		// sample a copy of the render target, not the target itself
		WW3DTextureHandle source = Smudge_Source(backTexture);
		if (!source)
		{
			m_hardwareSupportStatus = HardwareSmudgeSupport::SMUDGE_SUPPORT_NO;
			return FALSE;
		}
		RenderDevice::Set_Device_Texture(0,source);

		DWORD testData[BLOCK_SIZE*BLOCK_SIZE];
		memset(testData,0xff,sizeof(testData));

		v[0].color = 0xffffffff;
		v[1].color = 0xffffffff;
		v[2].color = 0xffffffff;
		v[3].color = 0xffffffff;

		RenderDevice::Draw_Screen_Quad_UP(v, sizeof(_TRANS_LIT_TEX_VERTEX), WW3D_FVF_XYZ | WW3D_FVF_DIFFUSE | WW3D_FVF_TEX1);
		bufSize=copyRect(reinterpret_cast<unsigned char *>(testData),sizeof(testData),0,0,BLOCK_SIZE,BLOCK_SIZE);

		if (!bufSize)
		{
			m_hardwareSupportStatus = HardwareSmudgeSupport::SMUDGE_SUPPORT_NO;
			return FALSE;
		}

		//compare the 2 buffers to see if they match.
		if (memcmp(testData,refData,bufSize) == 0)
		{
			m_hardwareSupportStatus = HardwareSmudgeSupport::SMUDGE_SUPPORT_YES;
			return TRUE;
		}
		m_hardwareSupportStatus = HardwareSmudgeSupport::SMUDGE_SUPPORT_NO;
	}

	return (HardwareSmudgeSupport::SMUDGE_SUPPORT_YES == m_hardwareSupportStatus);
}

void W3DSmudgeManager::render(RenderInfoClass &rinfo)
{
	//Verify that the card supports the effect.
	if (!testHardwareSupport())
		return;

	CameraClass &camera=rinfo.Camera;
	Vector3 vsVert;
	Vector4 ssVert;
	Real uvSpanX,uvSpanY;
	Vector3 vertex_offsets[4] = {
		Vector3(-0.5f, 0.5f, 0.0f),
		Vector3(-0.5f, -0.5f, 0.0f),
		Vector3(0.5f, -0.5f, 0.0f),
		Vector3(0.5f, 0.5f, 0.0f)
	};

#define THE_COLOR (0x00ffeedd)

	UnsignedInt vertexDiffuse[5]={THE_COLOR,THE_COLOR,THE_COLOR,THE_COLOR,THE_COLOR};

	Matrix4x4 proj;
	Matrix3D view;

	camera.Get_View_Matrix(&view);
	camera.Get_Projection_Matrix(&proj);

	SurfaceClass::SurfaceDescription surface_desc;
#ifdef USE_COPY_RECTS
	SurfaceClass *background=m_backgroundTexture->Get_Surface_Level();
	background->Get_Description(surface_desc);
#else
	WW3DSurfaceDesc D3DDesc;

	WW3DTextureHandle backTexture=W3DShaderManager::getRenderTexture();
	if (!backTexture || !W3DShaderManager::isRenderingToTexture())
		return;	//this card doesn't support render targets.

	RenderDevice::Get_Texture_Level_Desc(backTexture, 0, &D3DDesc);

	surface_desc.Width = D3DDesc.Width;
	surface_desc.Height = D3DDesc.Height;
#endif

	Real texClampX = static_cast<Real>(TheTacticalView->getWidth())/static_cast<Real>(surface_desc.Width);
	Real texClampY = static_cast<Real>(TheTacticalView->getHeight())/static_cast<Real>(surface_desc.Height);

	Real texScaleX = texClampX*0.5f;
	Real texScaleY = texClampY*0.5f;

	//Do a first pass over the smudges to determine how many are visible
	//and to fill in their world-space positions and screen uv coordinates.
	//TODO: Optimize out this extra pass!
	//TODO: Find size of screen rectangle that actually needs copying.

	SmudgeSet *set=m_usedSmudgeSetList.Head();	//first set that didn't fit into render batch.
	Int count = 0;

	if (set)
	{	//there are possibly some smudges to render, so make sure background particles have finished drawing.
		SortingRendererClass::Flush();	//draw sorted translucent polys like particles.
	}

	while (set)
	{
		Smudge *smudge=set->getUsedSmudgeList().Head();

		while (smudge)
		{
			//Get view-space center
			Matrix3D::Transform_Vector(view,smudge->m_pos,&vsVert);

			//Get 5 view-space vertices
			Smudge::smudgeVertex *verts=smudge->m_verts;

			//Do center vertex outside 'for' loop since it's different.
			verts[4].pos = vsVert;

			for (Int i=0; i<4; i++)
			{
				verts[i].pos = vsVert + vertex_offsets[i] * smudge->m_size;
				//Ge uv coordinates for each vertex
				ssVert = proj * verts[i].pos;
				Real oow = 1.0f/ssVert.W;
				ssVert *= oow;	//returned in camera space which is -1,-1 (bottom-left) to 1,1 (top-right)
				//convert camera space to uv space: 0,0 (top-left), 1,1 (bottom-right)
				verts[i].uv.Set((ssVert.X+1.0f)*texScaleX,(1.0f-ssVert.Y)*texScaleY);

				Vector2 &thisUV=verts[i].uv;

				//Clamp coordinates so we're not referencing texels outside the view.
				if (thisUV.X > texClampX)
					smudge->m_offset.X = 0;
				else
				if (thisUV.X < 0)
					smudge->m_offset.X = 0;

				if (thisUV.Y > texClampY)
					smudge->m_offset.Y = 0;
				else
				if (thisUV.Y < 0)
					smudge->m_offset.Y = 0;

			}

			//Finish center vertex
			//Ge uv coordinates by interpolating corner uv coordinates and applying desired offset.
			uvSpanX=verts[3].uv.X - verts[0].uv.X;
			uvSpanY=verts[1].uv.Y - verts[0].uv.Y;
			verts[4].uv.X=verts[0].uv.X+uvSpanX*(0.5f+smudge->m_offset.X);
			verts[4].uv.Y=verts[0].uv.Y+uvSpanY*(0.5f+smudge->m_offset.X);

			count++;	//increment visible smudge count.
			smudge=smudge->Succ();
		}

		set=set->Succ();	//advance to next node.
	}

	if (!count)
	{
#ifdef USE_COPY_RECTS
		REF_PTR_RELEASE(background);
#endif
		return;	//nothing to render.
	}

#ifdef USE_COPY_RECTS
	SurfaceClass *backBuffer=RenderDevice::_Get_Back_Buffer();

	backBuffer->Get_Description(surface_desc);

	//Copy the area of backbuffer occupied by smudges into an alternate buffer.
	background->Copy(0,0,0,0,surface_desc.Width,surface_desc.Height,backBuffer);

	REF_PTR_RELEASE(background);
	REF_PTR_RELEASE(backBuffer);
#endif

	Matrix4x4 identity(true);
	RenderDevice::Set_Transform(WW3D_TS_WORLD,identity);	
	RenderDevice::Set_Transform(WW3D_TS_VIEW,identity);	

	RenderDevice::Set_Index_Buffer(m_indexBuffer,0);
	//RenderDevice::Set_Shader(ShaderClass::_PresetOpaqueSpriteShader);

	RenderDevice::Set_Shader(ShaderClass::_PresetAlphaShader);
#ifdef USE_COPY_RECTS
	RenderDevice::Set_Texture(0,m_backgroundTexture);
#else
	// sample a copy of the render target; use backTexture if the copy fails
	{
		WW3DTextureHandle source = Smudge_Source(backTexture);
		RenderDevice::Set_Device_Texture(0, source ? source : backTexture);
	}
	//Need these states in case texture is non-power-of-2
	RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_CLAMP);
	RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_CLAMP);
	RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSW, WW3D_TADDRESS_CLAMP);
	RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
	RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_MIPFILTER, WW3D_TEXF_NONE);
#endif
	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	RenderDevice::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);
	RenderDevice::Apply_Render_State_Changes();

	//Disable reading texture alpha since it's undefined.
	//RenderDevice::Set_Texture_Stage_State(0,WW3D_TSS_COLOROP,WW3D_TOP_SELECTARG1);			
	RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Alpha_Op(WW3D_TOP_SELECTARG2));

	Int smudgesRemaining=count;
	set=m_usedSmudgeSetList.Head();	//first smudge set that needs rendering.
	Smudge	*remainingSmudgeStart=set->getUsedSmudgeList().Head();	//first smudge that needs rendering.

	while (smudgesRemaining)	//keep drawing smudges until we run out.
	{
		//Now that we know how many smudges need rendering, allocate vertex buffer space and copy verts.
		count=smudgesRemaining;

		if (count > SMUDGE_DRAW_SIZE)
			count = SMUDGE_DRAW_SIZE;

		Int smudgesInRenderBatch=0;

		DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DEVICE,dynamic_fvf_type,static_cast<unsigned short>(count*5));	//allocate 5 verts per smudge.
		{
			DynamicVBAccessClass::WriteLockClass lock(&vb_access);
			VertexFormatXYZNDUV2* verts=lock.Get_Formatted_Vertex_Array();
			Bool batchFull = false;

			while (set)
			{
				Smudge *smudge=remainingSmudgeStart;

				while (smudge)
				{
					Smudge::smudgeVertex *smVerts = smudge->m_verts;

					//Check if we exceeded maximum number of smudges allowed per draw call.
					if (smudgesInRenderBatch >= count)
					{	remainingSmudgeStart = smudge;
						batchFull = true;
						break;
					}

					//Set center vertex opacity.
					vertexDiffuse[4] = (static_cast<Int>(smudge->m_opacity * 255.0f) << 24) | THE_COLOR;

					for (Int i=0; i<5; i++)
					{
						verts->x=smVerts->pos.X;
						verts->y=smVerts->pos.Y;
						verts->z=smVerts->pos.Z;
						verts->nx=0;	//keep AGP write-combining active
						verts->ny=0;
						verts->nz=0;
						verts->diffuse=vertexDiffuse[i];	//set to transparent
						verts->u1=smVerts->uv.X;
						verts->v1=smVerts->uv.Y;
						verts->u2=0;	//keep AGP write-combining active
						verts->v2=0;
						verts++;
						smVerts++;
					}

					smudgesInRenderBatch++;
					smudge=smudge->Succ();
				}
				if (batchFull)
					break;	// the next batch starts at remainingSmudgeStart in this set

				set=set->Succ();	//advance to next node.

				if (set)	//start next batch at beginning of set.
					remainingSmudgeStart = set->getUsedSmudgeList().Head();
			}
			RenderDevice::Set_Vertex_Buffer(vb_access);
		}

		RenderDevice::Draw_Triangles(	0,static_cast<unsigned short>(smudgesInRenderBatch*4), 0, static_cast<unsigned short>(smudgesInRenderBatch*5));	

//Debug Code which draws outline around smudge
/*		RenderDevice::Set_Render_State(WW3D_RS_FILLMODE,WW3D_FILL_WIREFRAME);
		RenderDevice::Set_Render_State(WW3D_RS_ALPHABLENDENABLE,FALSE);
		RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Color_Op(WW3D_TOP_SELECTARG2));
		RenderDevice::Draw_Triangles(	0,smudgesInRenderBatch*4, 0, smudgesInRenderBatch*5);	
		RenderDevice::Set_Render_State(WW3D_RS_FILLMODE,WW3D_FILL_SOLID);
		RenderDevice::Set_Render_State(WW3D_RS_ALPHABLENDENABLE,TRUE);
		RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Color_Op(WW3D_TOP_SELECTARG1));
*/
		smudgesRemaining -= smudgesInRenderBatch;
	}

	RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Color_Op(WW3D_TOP_MODULATE).Alpha_Op(WW3D_TOP_MODULATE));

}