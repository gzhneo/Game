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

// Save game and CRC

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void WaterRenderObjClass::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void WaterRenderObjClass::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// grid cells x
	Int cellsX = m_gridCellsX;
	xfer->xferInt( &cellsX );
	if( cellsX != m_gridCellsX )
	{

		DEBUG_CRASH(( "WaterRenderObjClass::xfer - cells X mismatch\n" ));
		throw SaveCode::SC_INVALID_DATA;

	}  // end if
	
	// grid cells Y
	Int cellsY = m_gridCellsY;
	xfer->xferInt( &cellsY );
	if( cellsY != m_gridCellsY )
	{
	
		DEBUG_CRASH(( "WaterRenderObjClass::xfer - cells Y mismatch\n" ));
		throw SaveCode::SC_INVALID_DATA;

	}  // end if

	// xfer each of the mesh data points
	for( Int i = 0; static_cast<UnsignedInt>(i) < m_meshDataSize; ++i )
	{

		// height
		xfer->xferReal( &m_meshData[ i ].height );

		// velocity
		xfer->xferReal( &m_meshData[ i ].velocity );

		// status
		xfer->xferUnsignedByte( &m_meshData[ i ].status );

		// preferred height
		xfer->xferUnsignedByte( &m_meshData[ i ].preferredHeight );

	}  // end for, i

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void WaterRenderObjClass::loadPostProcess( void )
{

}  // end loadPostProcess
