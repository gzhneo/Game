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

#define SCROLL_UV

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

// 3D water mesh grid: waves, velocity, grid transform and resolution

//Defines for procedural water animation.
#define WATER_FREQ	(2.0*3.2831/4.0)	//2pi (full cycle) cover 4 units

void WaterRenderObjClass::enableWaterGrid(Bool state)
{
	m_doWaterGrid = state;

	m_drawingRiver = false;
	m_disableRiver = false;

	if (state && m_meshData == nullptr)
	{	//water type has changed, must allocate necessary assets for new water.
		//contains the current deformed water surface z(height) values.  With 1 vertex invisible border
		//around surface to speed up normal calculations.
		m_meshDataSize = (m_gridCellsX+1+2)*(m_gridCellsY+1+2);
		m_meshData=NEW WaterMeshData[ m_meshDataSize ];
		memset(m_meshData,0,sizeof(WaterMeshData)*(m_gridCellsX+1+2)*(m_gridCellsY+1+2));
		reset();

		//Release existing grid data
		SAFE_RELEASE(m_vertexBufferD3D);
		SAFE_RELEASE(m_indexBufferD3D);

		//Create new grid data
		if (FAILED(generateIndexBuffer(m_gridCellsX+1,m_gridCellsY+1)))
			return;
		if (FAILED(generateVertexBuffer(m_gridCellsX+1,m_gridCellsY+1,sizeof(MaterMeshVertexFormat),false)))
			return;
	}
}

constexpr auto WATER_AMP = 1.0f;

constexpr auto WATER_OFFSET = 0.1f;

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the water surface mesh geometry.
	*	This is a work-in-progress!  Do not use this code! */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::renderWaterMesh(void)
{

	if (!m_doWaterGrid)
		return;	//the water grid is disabled.

 	//According to Nvidia there's a D3D bug that happens if you don't start with a
 	//new dynamic VB each frame - so we force a DISCARD by overflowing the counter.
 	m_vertexBufferD3DOffset = 0xffff;

	Setting *setting=&m_settings[m_tod];

	WaterMeshData *pData;
	Int	mx=m_gridCellsX+1;
	Int my=m_gridCellsY+1;
	Int i,j;
	
	Real cellSizeX=m_gridCellSize;
	Real cellSizeY=m_gridCellSize;
//	Real	uScale2=5.0f*setting->waterRepeatCount/(128.0f)*cellSizeX/10.0f;
//	Real	vScale2=5.0f*setting->waterRepeatCount/(128.0f)*cellSizeY/10.0f;

	//Old waterRepeatCount settings in INI were based on 128x128 water grid of cellsize=10
	//Scale values to correct size.
	Real	uScale=setting->waterRepeatCount/(128.0f)*cellSizeX/10.0f*0.2f;
	Real	vScale=setting->waterRepeatCount/(128.0f)*cellSizeY/10.0f*0.2f;

	Vector3	nx(cellSizeX*2.0f,0,0);
	Vector3 ny(0,cellSizeY*2.0f,0);
	Vector3 C;

#ifdef DO_WATER_SIMULATION		//Debug code used to create a dummy water animation
	//
	// Mark: If you re-enable this water simulation, you might want to consider moving
	// this code to the update() method of the water render object (Colin)
	//

	static Real PhasePerFrameX=0.1f;
	static Real PhasePerFrameY=0.1f;

	//update the mesh heights for this frame (update buffer is 2 samples wider/taller due to border)
	for (j=0,pData=m_meshData; j<(my+2); j++)
	{	
		for (i=0; i<(mx+2); i++)
		{
			//*pData = WATER_AMP * sin(WATER_FREQ*(0.7f*i + 0.7f*j) - PhasePerFrame);

			pData->height=WATER_OFFSET+WATER_AMP*(sin((float)i*WATER_FREQ*0.4+PhasePerFrameX*0.5)+sin((float)i*WATER_FREQ*0.6+PhasePerFrameX*0.2)+sin((float)j*WATER_FREQ+PhasePerFrameX)+sin((float)j*WATER_FREQ*0.7+PhasePerFrameX*0.3));
//			*pData=WATER_OFFSET+WATER_AMP*(sin((float)i*WATER_FREQ*0.4+PhasePerFrameX*0.5)+sin((float)i*WATER_FREQ*0.6+PhasePerFrameX*0.2)+sin((float)j*WATER_FREQ+PhasePerFrameX)+sin((float)j*WATER_FREQ*0.7+PhasePerFrameX*0.3));
			pData++;
		}
	}

	PhasePerFrameX -= 0.08f;
	PhasePerFrameY -= 0.1f;
#endif

	MaterMeshVertexFormat *vb;
	if (m_vertexBufferD3DOffset < m_numVertices)
	{	//we have room in current VB, append new verts
		if(RenderDevice::Lock_Vertex_Buffer(m_vertexBufferD3D, m_vertexBufferD3DOffset*sizeof(MaterMeshVertexFormat),mx*my*sizeof(MaterMeshVertexFormat),reinterpret_cast<void**>(&vb),WW3D_LOCK_NOOVERWRITE) != WW3D_OK)
			return;
	}
	else
	{	//ran out of room in last VB, request a substitute VB.
		if(RenderDevice::Lock_Vertex_Buffer(m_vertexBufferD3D, 0,mx*my*sizeof(MaterMeshVertexFormat),reinterpret_cast<void**>(&vb),WW3D_LOCK_DISCARD) != WW3D_OK)
			return;
		m_vertexBufferD3DOffset=0;	//reset start of page to first vertex
	}
	Int diffuse;
	diffuse = setting->waterDiffuse&0x00ffffff;
	Int alpha = (setting->waterDiffuse & 0xff000000)>>24;
	// Reduce alpha for wave mesh
	alpha -= 0x20;
	diffuse |= alpha<<24;

	//I pulled some of these constants out of the loops for speed:
	Real uvCosScale=static_cast<Real>(0.02*cos(3*m_riverVOrigin));
	Real sinOffset=25*m_riverVOrigin;
	Real originScale=m_riverVOrigin/vScale; 
	Real bumpSizeDiv=cellSizeY/BUMP_SIZE;
	Real bumpSizeDiv2=0.3f*cellSizeY/BUMP_SIZE;

	//Data has a 1 vertex padding all around it so we don't need to special-case edges.  Improves performance
	for (j=0,pData=m_meshData+mx+2+1; j<my; j++,pData+=2)	//skip 2 horizontal border samples after each row
	{	
		Real y=static_cast<float>(j)*cellSizeY;
		Real v1Offset=m_riverVOrigin+static_cast<float>(j)*vScale + uvCosScale*WWMath::Fast_Sin(sinOffset+y*PI/(8*MAP_XY_FACTOR));
		Real v2Offset=(static_cast<float>(j)+originScale)*bumpSizeDiv + static_cast<float>(j)*bumpSizeDiv2;

		for (i=0; i<mx; i++)
		{
			//compute normal by looking at 4 vertex neightbors
#ifdef USE_MESH_NORMALS
			nx.Z=(pData+1)->height - (pData-1)->height;
			ny.Z=(pData+mx+2)->height - (pData-mx-2)->height;
//			nx.Z=*(pData+1)-*(pData-1);
//			ny.Z=*(pData+mx+2)-*(pData-mx-2);
			Vector3::Cross_Product(nx,ny,&C);
			C.Normalize();
			vb->nx = C.X;
			vb->ny = C.X;
			vb->nz = C.X;
#endif
			Real x = static_cast<float>(i)*cellSizeX;
			vb->x=	x;
			vb->y=	y;
			vb->z=  pData->height;//WATER_OFFSET+WATER_AMP*(sin((float)i*WATER_FREQ+PhasePerFrame)+cos((float)j*WATER_FREQ+PhasePerFrame));

			vb->diffuse = diffuse;
#ifdef SCROLL_UV
//			vb->diffuse=0x80ffffff;
			vb->u1=static_cast<float>(i)*uScale;
			vb->v1=v1Offset;

			//old slow version
			//vb->v1=m_riverVOrigin+(float)j*vScale + 0.02*cos(3*m_riverVOrigin)*sin(25*m_riverVOrigin+y*PI/(8*MAP_XY_FACTOR));

//			vb->u2=m_initialGridU2+(float)i*uScale2;
//			vb->v2=m_initialGridV2+(float)j*vScale2;
#else
			vb->u1=(float)i*uScale;
			vb->v1=(float)j*vScale;
#endif
			vb->u2=static_cast<float>(i)*cellSizeX/BUMP_SIZE; 
			vb->v2=v2Offset;
			//old slow code
			//vb->v2=(float)(j+m_riverVOrigin/vScale )*cellSizeY/BUMP_SIZE+ 0.3f*(float)j*cellSizeY/BUMP_SIZE;
			vb++;
			pData++;
		}
	}

	RenderDevice::Unlock_Vertex_Buffer(m_vertexBufferD3D);

	Matrix3D tm(Transform);

	RenderDevice::Set_Transform(WW3D_TS_WORLD,tm);	//position the water surface
	RenderDevice::Set_Material(m_meshVertexMaterialClass);

	ShaderClass::CullModeType oldCullMode=m_shaderClass.Get_Cull_Mode();

	ShaderClass::DepthMaskType oldDepthMask=m_shaderClass.Get_Depth_Mask();
	m_shaderClass.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);	//disable writing to z-buffer to prevent particle clipping.

	m_shaderClass.Set_Cull_Mode(ShaderClass::CULL_MODE_ENABLE);	//water should be visible from both sides

	RenderDevice::Set_Shader(m_shaderClass);
#if 1
	setupFlatWaterShader();
#else 
	//RenderDevice::Set_Shader(ShaderClass::_PresetOpaqueShader);
	RenderDevice::Set_Texture(0,setting->waterTexture);
	RenderDevice::Set_Texture(1,setting->waterTexture);

	RenderDevice::Set_Light(0,*m_meshLight);
	RenderDevice::Set_Light(1,nullptr);
	RenderDevice::Set_Light(2,nullptr);
	RenderDevice::Set_Light(3,nullptr);
/*
	RenderDevice::Set_Render_State(WW3D_RS_AMBIENT,0);	//turn off scene ambient
	RenderDevice::Set_Render_State(WW3D_RS_SPECULARENABLE,TRUE);
	RenderDevice::Set_Render_State(WW3D_RS_LOCALVIEWER,TRUE); 
*/

	RenderDevice::Apply_Render_State_Changes();	//force update of view and projection matrices
#endif


//	RenderDevice::Set_Render_State(WW3D_RS_ZFUNC,WW3D_CMP_ALWAYS);	//used to display grid under map.

	// BaseVertexIndex is passed to Draw_Indexed_Primitive; fixed-function input uses the vertex
	// declaration derived from the FVF.
	RenderDevice::Set_Indices(m_indexBufferD3D);
	RenderDevice::Set_Stream_Source(0,m_vertexBufferD3D,0,sizeof(MaterMeshVertexFormat));
	RenderDevice::Set_Vertex_Shader_Object(nullptr);
	RenderDevice::Set_Vertex_Declaration(RenderDevice::Get_Vertex_Declaration_For_FVF(WATER_MESH_FVF));


	if (TheTerrainRenderObject->getShroud() && !m_trapezoidWaterPixelShader)
	{	//we have a shroud to apply and can't do it inside the pixel shader.
		//so do it in stage1
		W3DShaderManager::setTexture(0,TheTerrainRenderObject->getShroud()->getShroudTexture());
		W3DShaderManager::setShader(W3DShaderManager::ST_SHROUD_TEXTURE, 1);

		//modulate with shroud texture
		RenderDevice::Set_Texture_Stage(1, TextureStageDesc()
			.Color(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_CURRENT)
			.Alpha_Op(WW3D_TOP_MODULATE)
		);

		//Shroud shader uses z-compare of EQUAL which wouldn't work on water because it doesn't
		//write to the zbuffer.  Change to LESSEQUAL.
		RenderDevice::Set_Render_State(WW3D_RS_ZFUNC, WW3D_CMP_LESSEQUAL);
		RenderDevice::Draw_Indexed_Primitive(WW3D_PT_TRIANGLESTRIP,m_vertexBufferD3DOffset,0,mx*my,0,m_numIndices-2);
		RenderDevice::Set_Render_State(WW3D_RS_ZFUNC, WW3D_CMP_EQUAL);
		W3DShaderManager::resetShader(W3DShaderManager::ST_SHROUD_TEXTURE);
	}
	else
		RenderDevice::Draw_Indexed_Primitive(WW3D_PT_TRIANGLESTRIP,m_vertexBufferD3DOffset,0,mx*my,0,m_numIndices-2);
	
	Debug_Statistics::Record_Device_Polys_And_Vertices(m_numIndices-2,mx*my,ShaderClass::_PresetOpaqueShader);

//	RenderDevice::Set_Render_State(WW3D_RS_FILLMODE,WW3D_FILL_SOLID);

	if (m_trapezoidWaterPixelShader) RenderDevice::Set_Pixel_Shader_Object(nullptr);

	m_vertexBufferD3DOffset += mx*my;	//advance past vertices already in buffer

	RenderDevice::Set_Texture(0,nullptr);
	RenderDevice::Set_Texture(1,nullptr);
	ShaderClass::Invalidate();
	m_shaderClass.Set_Cull_Mode(oldCullMode);	//water should be visible from both sides

	// restore shader to old mask
	m_shaderClass.Set_Depth_Mask(oldDepthMask);	 

	//W3DShaderManager::resetShader(W3DShaderManager::ST_SHROUD_TEXTURE);

}

inline void WaterRenderObjClass::setGridVertexHeight(Int x, Int y, Real value)
{
	DEBUG_ASSERTCRASH( x < (m_gridCellsX+1) && y < (m_gridCellsY+1), ("Invalid Water Mesh Coordinates\n") );

	if (m_meshData)
	{
		m_meshData[(y+1)*(m_gridCellsX+1+2)+x+1].height = value;
	}
}

void WaterRenderObjClass::setGridHeightClamps(Real minz, Real maxz)
{
	m_minGridHeight = minz;
	m_maxGridHeight = maxz;
}

void WaterRenderObjClass::addVelocity( Real worldX, Real worldY, 
																			 Real zVelocity, Real preferredHeight )
{

	if( m_doWaterGrid)
	{
		Real gx,gy;
		Real minX,maxX,minY,maxY;
		Int x,y;
		WaterMeshData *meshPoint;
		m_disableRiver = true;

		//check if center falls within grid bounds
		if (worldToGridSpace(worldX, worldY, gx, gy))
		{	
		
			//find extents of influence
			minX = floorf(gx - m_gridChangeMaxRange);
			if (minX < 0 )
				minX = 0;	//clamp extent to fall within box
			maxX = ceilf(gx + m_gridChangeMaxRange);
			if (maxX > m_gridCellsX)
				maxX = static_cast<Real>(m_gridCellsX);	//clamp extent to fall within box

			minY = floorf(gy - m_gridChangeMaxRange);
			if (minY < 0 )
				minY = 0;	//clamp extent to fall within box
			maxY = ceilf(gy + m_gridChangeMaxRange);
			if (maxY > m_gridCellsY)
				maxY = static_cast<Real>(m_gridCellsY);	//clamp extent to fall within box

			for (y=static_cast<Int>(minY); y<=maxY; y++)
			{
				for (x=static_cast<Int>(minX); x<=maxX; x++)
				{	

					// get the mesh point that we're concerned with
					meshPoint = &m_meshData[ (y + 1) * (m_gridCellsX + 1 + 2) + x + 1 ];

					// we now have a new preferred height
					meshPoint->preferredHeight = static_cast<UnsignedByte>(preferredHeight);

					//
					// set the velocity of this point based on the distance from the center of the
					// "core" point for this call
					//
					meshPoint->velocity = meshPoint->velocity + zVelocity;				

					// this point is now "in motion"
					BitSet( meshPoint->status, WaterRenderObjClass::IN_MOTION );

				}
			}

			//
			// the mesh data is now dirty, we need to pass through the velocity field
			// during an update phase to update the positions
			//
			m_meshInMotion = TRUE;

		}

	}  // end if, water type is 3

}

void WaterRenderObjClass::changeGridHeight(Real wx, Real wy, Real delta)
{
	Real gx,gy;
	Real *oldData;
	Real newData;
	Real distance;
	Real minX,maxX,minY,maxY;
	Int x,y;
	
	//check if center falls within grid bounds
	if (worldToGridSpace(wx, wy, gx, gy))
	{	//find extents of influence
		minX = floorf(gx - m_gridChangeMaxRange);
		if (minX < 0 )
			minX = 0;	//clamp extent to fall within box
		maxX = ceilf(gx + m_gridChangeMaxRange);
		if (maxX > m_gridCellsX)
			maxX = static_cast<Real>(m_gridCellsX);	//clamp extent to fall within box

		minY = floorf(gy - m_gridChangeMaxRange);
		if (minY < 0 )
			minY = 0;	//clamp extent to fall within box
		maxY = ceilf(gy + m_gridChangeMaxRange);
		if (maxY > m_gridCellsY)
			maxY = static_cast<Real>(m_gridCellsY);	//clamp extent to fall within box

		for (y=static_cast<Int>(minY); y<=maxY; y++)
		{
			for (x=static_cast<Int>(minX); x<=maxX; x++)
			{	oldData = &m_meshData[(y+1)*(m_gridCellsX+1+2)+x+1].height;
				distance = (gx - static_cast<Real>(x))*(gx - static_cast<Real>(x)) + (gy - static_cast<Real>(y))*(gy - static_cast<Real>(y));
				distance = sqrt(distance);
				newData = *oldData + 1.0f/(m_gridChangeAtt0+m_gridChangeAtt1*distance+distance*distance*m_gridChangeAtt2)*delta;
				//Clamp to min/max values
				if (newData < m_minGridHeight)
					newData = m_minGridHeight;
				if (newData > m_maxGridHeight)
					newData = m_maxGridHeight;
				*oldData = newData;
			}
		}
	}
}

void WaterRenderObjClass::setGridChangeAttenuationFactors(Real a, Real b, Real c, Real range)
{
	m_gridChangeAtt0 = a;
	m_gridChangeAtt1 = b;
	m_gridChangeAtt2 = c;
	m_gridChangeMaxRange = range/m_gridCellSize;	//convert range to grid space
}

void WaterRenderObjClass::setGridTransform(Real angle, Real x, Real y, Real z)
{
	m_gridDirectionX = Vector2(1.0f,0.0f);

	m_gridOrigin.X = x;
	m_gridOrigin.Y = y;

	Matrix3D xform(1);
	xform.Rotate_Z(angle);

	m_gridDirectionX.X = xform.Get_X_Vector().X;
	m_gridDirectionX.Y = xform.Get_X_Vector().Y;

	m_gridDirectionY.X = xform.Get_Y_Vector().X;
	m_gridDirectionY.Y = xform.Get_Y_Vector().Y;

	xform.Set_Translation(Vector3(x,y,z));
	Set_Transform(xform);
}

void WaterRenderObjClass::setGridTransform(const Matrix3D *transform )
{

	if( transform )
		Set_Transform( *transform );

}

void WaterRenderObjClass::getGridTransform(Matrix3D *transform )
{

	if( transform )
		*transform = Get_Transform();

}

void WaterRenderObjClass::setGridResolution(Real gridCellsX, Real gridCellsY, Real cellSize)
{
	m_gridCellSize=cellSize;

	if (m_gridCellsX != gridCellsX || m_gridCellsY != gridCellsY)
	{	//resolutoin has changed
		m_gridCellsX=static_cast<Int>(gridCellsX);
		m_gridCellsY=static_cast<Int>(gridCellsY);

		if (m_meshData)
		{	

			delete [] m_meshData;//free previously allocated grid and allocate new size
			m_meshData = nullptr;	 // must set to NULL so that we properly re-allocate
			m_meshDataSize = 0;

			Bool enable = m_doWaterGrid;
			enableWaterGrid(true);	// allocates buffers.
			m_doWaterGrid = enable;

		}
	}
}

void WaterRenderObjClass::getGridResolution( Real *gridCellsX, Real *gridCellsY, Real *cellSize )
{

	if( gridCellsX )
		*gridCellsX = static_cast<Real>(m_gridCellsX);
	if( gridCellsY )
		*gridCellsY = static_cast<Real>(m_gridCellsY);
	if( cellSize )
		*cellSize = m_gridCellSize;

}
