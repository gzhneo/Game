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

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include <stdlib.h>
#include <windows.h>
#include <io.h>
#include <time.h>
#include "Common/ThingFactory.h"
#include "formatinto.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "Engine/Clock.h"
#include "Common/GameEngine.h"
#include "Common/GameEdition.h"	// Generals-specific shader and light settings
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/FileSystem.h"
#include "Common/LocalFileSystem.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "Common/GameLOD.h"
#include "Common/DrawModule.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameClient/CameraPath.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameText.h"
#include "GameClient/Line2D.h"
#include "GameClient/Mouse.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Water.h"
#include "GameNetwork/NetworkInterface.h"
#include "Common/ModelState.h"
#include "Lib/BaseType.h"
#include "W3DDevice/Common/W3DConvert.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/camerashakesystem.h"
#include "W3DDevice/GameClient/W3DGameClient.h"
#include "W3DDevice/GameClient/W3DFileSystem.h"
#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DTerrainTracks.h"
#include "W3DDevice/GameClient/W3DWater.h"
#include "W3DDevice/GameClient/W3DVideobuffer.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DDebugDisplay.h"
#include "W3DDevice/GameClient/W3DProjectedShadow.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "WWMath/wwmath.h"
#include "WW3D2/System/ww3d.h"
#include "WW3D2/Mesh/predlod.h"
#include "WW3D2/Effects/part_emt.h"
#include "WW3D2/Effects/part_ldr.h"
#include "WW3D2/Common/rendercaps.h"
#include "WW3D2/Mesh/meshrenderer.h"
#include "WW3D2/Effects/pointgr.h"
#include "WW3D2/Material/ww3dformat.h"
#include "WW3D2/Animation/agg_def.h"
#include "WW3D2/Render2D/render2dsentence.h"
#include "WW3D2/Buffers/sortingrenderer.h"
#include "WW3D2/Material/textureloader.h"
#include "WW3D2/Mesh/mesh.h"
#include "WW3D2/Mesh/hlod.h"
#include "WW3D2/Material/meshmatdesc.h"
#include "WW3D2/Mesh/meshmdl.h"
#include "WW3D2/Scene/rddesc.h"
#include "TARGA.H"
#include "Lib/BaseType.h"
#include "GameLogic/ScriptEngine.h"		// For TheScriptEngine - jkmcd
#include "GameLogic/GameLogic.h"
#include "GameLogic/PartitionManager.h"	// shroud status of light positions (dynamic lights)
#include "Win32Device/Common/Win32AppWindow.h"
#include "GameClient/GameClient.h"	// isRenderOnlyFrame()
#include "Engine/ThreadRole.h"
#include "Lib/SafeString.h"
#include <cstdarg>
#include "formatinto.h"

// W3DDisplay: drawing in 2D -- lines, rectangles, clocks, images, video, clip region

// W3DDisplay::drawLine =======================================================
/** draw a line on the display in pixel coordinates with the specified color */
//=============================================================================
void W3DDisplay::drawLine( Int startX, Int startY, 
													 Int endX, Int endY, 
													 Real lineWidth,
													 UnsignedInt lineColor )
{
	
	/// @todo we need to consider the efficiency of the 2D renderer
	m_2DRender->Reset();
	m_2DRender->Enable_Texturing( FALSE );
	m_2DRender->Add_Line( Vector2( static_cast<float>(startX), static_cast<float>(startY) ), Vector2( static_cast<float>(endX), static_cast<float>(endY) ), 
												lineWidth, lineColor );
	m_2DRender->Render();

}  // end drawLine

// W3DDisplay::drawLine =======================================================
/** draw a line on the display in pixel coordinates with the specified color */
//=============================================================================
void W3DDisplay::drawLine( Int startX, Int startY, 
													 Int endX, Int endY, 
													 Real lineWidth,
													 UnsignedInt lineColor1,UnsignedInt lineColor2 )
{
	
	/// @todo we need to consider the efficiency of the 2D renderer
	m_2DRender->Reset();
	m_2DRender->Enable_Texturing( FALSE );
	m_2DRender->Add_Line( Vector2( static_cast<float>(startX), static_cast<float>(startY) ), Vector2( static_cast<float>(endX), static_cast<float>(endY) ), 
												lineWidth, lineColor1, lineColor2 );
	m_2DRender->Render();

}  // end drawLine

// W3DDisplay::drawOpenRect ===================================================
//=============================================================================
void W3DDisplay::drawOpenRect( Int startX, Int startY, Int width, Int height,
															 Real lineWidth, UnsignedInt lineColor )
{
	
	if (m_isClippedEnabled)
	{
		ICoord2D start, end, returnStart, returnEnd;
		start.x = startX;
		start.y = startY;

		end.x = start.x;
		end.y = start.y + height;
		if(ClipLine2D(&start, &end, &returnStart, &returnEnd, &m_clipRegion ))
			drawLine( returnStart.x, returnStart.y, returnEnd.x, returnEnd.y, lineWidth, lineColor);
			
		end.x = start.x + width;
		end.y = start.y;
		if(ClipLine2D(&start, &end, &returnStart, &returnEnd, &m_clipRegion ))
			drawLine( returnStart.x, returnStart.y, returnEnd.x, returnEnd.y, lineWidth, lineColor);

		start.x = startX + width;
		start.y = startY;
		end.x = start.x;
		end.y = start.y + height;
		if(ClipLine2D(&start, &end, &returnStart, &returnEnd, &m_clipRegion ))
			drawLine( returnStart.x, returnStart.y, returnEnd.x, returnEnd.y, lineWidth, lineColor);

		start.x = startX;
		start.y = startY + height;
		end.x = start.x + width;
		end.y = start.y;
		if(ClipLine2D(&start, &end, &returnStart, &returnEnd, &m_clipRegion ))
			drawLine( returnStart.x, returnStart.y, returnEnd.x, returnEnd.y, lineWidth, lineColor);
	}
	else
	{
		/// @todo we need to consider the efficiency of the 2D renderer
		m_2DRender->Reset();		
		m_2DRender->Enable_Texturing( FALSE );
		
		m_2DRender->Add_Outline( RectClass( static_cast<float>(startX), static_cast<float>(startY), 
																				static_cast<float>(startX + width), static_cast<float>(startY + height) ), 
														 lineWidth, lineColor );

		// render it now!
		m_2DRender->Render();
	}

}  // end drawOpenRect

// W3DDisplay::drawFillRect ===================================================
//=============================================================================
void W3DDisplay::drawFillRect( Int startX, Int startY, Int width, Int height,
															 UnsignedInt color )
{

	/// @todo we need to consider the efficiency of the 2D renderer
	m_2DRender->Reset();		
	m_2DRender->Enable_Texturing( FALSE );
	m_2DRender->Add_Rect( RectClass( static_cast<float>(startX), static_cast<float>(startY), 
																	 static_cast<float>(startX + width), static_cast<float>(startY + height) ), 
												0, 0, color );

	// render it now!
	m_2DRender->Render();

}  // end drawFillRect

void W3DDisplay::drawRectClock(Int startX, Int startY, Int width, Int height, Int percent, UnsignedInt color)
{
	// sanity
	if(percent < 1 || percent > 100)
		return;

	m_2DRender->Reset();		
	m_2DRender->Enable_Texturing( FALSE );

// The rectanges are numberd as follows
//(x,y)	|---------|
//			| 4  | 1  |
//			|----+----|
//			| 3  | 2  |
//			|---------| (x + width, y + width)
//	
	// we're done, lets just draw one rectangle for it all.
	if(percent == 100)
	{
		m_2DRender->Add_Rect(RectClass( static_cast<float>(startX), static_cast<float>(startY), 
																		static_cast<float>(startX + width), static_cast<float>(startY + height)), static_cast<float>(0),0, color);
	}
	else if( percent> 75)
	{
		//rectangle #1 & 2
		m_2DRender->Add_Rect(RectClass( static_cast<float>(startX + width/2), static_cast<float>(startY), 
																		static_cast<float>(startX + width), static_cast<float>(startY + height)), static_cast<float>(0),0, color);
		// rectangle #3
		m_2DRender->Add_Rect(RectClass( static_cast<float>(startX), static_cast<float>(startY + height/2), 
																		static_cast<float>(startX + width/2), static_cast<float>(startY + height)), static_cast<float>(0),0, color);
		// draw the part of rectangle 4
		Real remain = static_cast<Real>(percent - 75);
		if(remain > 12)
		{
			//draw the full triangle
			m_2DRender->Add_Tri(Vector2(static_cast<float>(startX), static_cast<float>(startY)), 
													Vector2(static_cast<float>(startX), static_cast<float>(startY + height/2)),
													Vector2(static_cast<float>(startX + width/2), static_cast<float>(startY + height/2)),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
			
			// draw the part of triangle
			Real percentDraw = static_cast<Real>(remain - 12)/ 13;
			m_2DRender->Add_Tri(Vector2(static_cast<float>(startX), static_cast<float>(startY)), 
													Vector2(static_cast<float>(startX + width/2), static_cast<float>(startY + height/2)),
													Vector2(static_cast<Real>(startX) + (static_cast<Real>(width/2) * percentDraw), static_cast<float>(startY)),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
		}
		else
		{
			// draw the part of triangle
			Real percentDraw = static_cast<Real>(remain)/ 12;
			m_2DRender->Add_Tri(Vector2(static_cast<float>(startX), static_cast<Real>(startY + height/2) - (static_cast<Real>(height/2) * percentDraw)), 
													Vector2(static_cast<float>(startX), static_cast<float>(startY + height/2)),
													Vector2(static_cast<float>(startX + width/2), static_cast<float>(startY + height/2)),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
		}

	}
	else if( percent > 50)
	{
		//rectangle #1 & 2
		m_2DRender->Add_Rect(RectClass( static_cast<float>(startX + width/2), static_cast<float>(startY), 
																		static_cast<float>(startX + width), static_cast<float>(startY + height)), static_cast<float>(0),0, color);
		// draw the part of rectangle 3
		Real remain = static_cast<Real>(percent - 50);
		if(remain > 12)
		{
			//draw the full triangle
			m_2DRender->Add_Tri(Vector2(static_cast<float>(startX + width/2), static_cast<float>(startY + height/2)), 
													Vector2(static_cast<float>(startX), static_cast<float>(startY + height)),
													Vector2(static_cast<float>(startX + width/2), static_cast<float>(startY + height)),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
			
			// draw the part of triangle
			Real percentDraw = static_cast<Real>(remain - 12)/ 13;
			m_2DRender->Add_Tri(Vector2(static_cast<float>(startX), static_cast<Real>(startY + height) - (static_cast<Real>(height/2) * percentDraw)), 
													Vector2(static_cast<float>(startX), static_cast<float>(startY + height)),
													Vector2(static_cast<float>(startX + width/2), static_cast<float>(startY + height/2)),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
		}
		else
		{
			// draw the part of triangle
			Real percentDraw = static_cast<Real>(remain)/ 12;
			m_2DRender->Add_Tri(Vector2(static_cast<float>(startX + width/2), static_cast<float>(startY + height)),  
													Vector2(static_cast<float>(startX + width/2), static_cast<float>(startY + height/2)),
													Vector2(static_cast<Real>(startX + width/2) - ( static_cast<Real>(width/2) * percentDraw), static_cast<float>(startY + height)),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
		}
	}
	else if(percent > 25)
	{
		// rectangel #1
		m_2DRender->Add_Rect(RectClass( static_cast<float>(startX + width/2), static_cast<float>(startY), 
																		static_cast<float>(startX + width), static_cast<float>(startY + height/2)), static_cast<float>(0),0, color);
		// draw the part of rectangle 2
		Real remain = static_cast<Real>(percent - 25);
		if(remain > 12)
		{
			//draw the full triangle
			m_2DRender->Add_Tri(Vector2(static_cast<float>(startX + width/2), static_cast<float>(startY + height/2)), 
													Vector2(static_cast<float>(startX + width), static_cast<float>(startY + height)),
													Vector2(static_cast<float>(startX + width), static_cast<float>(startY + height/2)),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
			
			// draw the part of triangle
			Real percentDraw = static_cast<Real>(remain - 12)/ 13;
			m_2DRender->Add_Tri(Vector2(static_cast<float>(startX + width/2), static_cast<float>(startY + height/2)), 
													Vector2(static_cast<Real>(startX + width) - (static_cast<Real>(width/2) * percentDraw), static_cast<float>(startY + height)),
													Vector2(static_cast<float>(startX + width), static_cast<float>(startY + height)),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
		}
		else
		{
			// draw the part of triangle
			Real percentDraw = static_cast<Real>(remain)/ 12;
			m_2DRender->Add_Tri(Vector2(static_cast<float>(startX + width), static_cast<float>(startY + height/2)),  
													Vector2(static_cast<float>(startX + width/2), static_cast<float>(startY + height/2)),
													Vector2(static_cast<float>(startX + width), static_cast<Real>(startY + height/2) + ( static_cast<Real>(height/2) * percentDraw)),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
		}
	}
	else
	{
				// draw the part of rectangle 1
		
		if(percent > 12)
		{
			//draw the full triangle
			m_2DRender->Add_Tri(Vector2(static_cast<float>(startX + width/2), static_cast<float>(startY)), 
													Vector2(static_cast<float>(startX + width/2), static_cast<float>(startY + height/2)),
													Vector2(static_cast<float>(startX + width), static_cast<float>(startY)),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
			
			// draw the part of triangle
			Real percentDraw = static_cast<Real>(percent - 12)/ 13;
			m_2DRender->Add_Tri(Vector2(static_cast<float>(startX + width), static_cast<float>(startY)),
													Vector2(static_cast<float>(startX + width/2), static_cast<float>(startY + height/2)), 
													Vector2(static_cast<float>(startX + width), static_cast<Real>(startY) + (static_cast<Real>(height/2) * percentDraw)),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
		}
		else
		{
			// draw the part of triangle
			Real percentDraw = static_cast<Real>(percent)/ 12;
			m_2DRender->Add_Tri(Vector2(static_cast<float>(startX + width/2), static_cast<float>(startY)),  
													Vector2(static_cast<float>(startX + width/2), static_cast<float>(startY + height/2)),
													Vector2(static_cast<Real>(startX + width/2) + (static_cast<Real>(width/2) * percentDraw), static_cast<float>(startY) ),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
		}
	}

	// render it now!
	m_2DRender->Render();

}

//--------------------------------------------------------------------------------------------------------------------
// W3DDisplay::drawRemainingRectClock
// Variation added by Kris -- October 2002
// This version will overlay a clock progress from the specified percentage to 100%. Essentially, this function will
// "reveal" an icon as it progresses towards completion.
//--------------------------------------------------------------------------------------------------------------------
void W3DDisplay::drawRemainingRectClock(Int startX, Int startY, Int width, Int height, Int percent, UnsignedInt color)
{
	// sanity
	if( percent < 0 || percent > 99 )
		return;

	m_2DRender->Reset();		
	m_2DRender->Enable_Texturing( FALSE );

// The rectanges are numbered as follows
//(x,y)	|---------|
//			| 4  | 1  |
//			|----+----|
//			| 3  | 2  |
//			|---------| (x + width, y + width)
//	

	Int midX = startX + width/2;
	Int midY = startY + height/2;
	Int endX = startX + width;
	Int endY = startY + height;
	Int halfWidth = width/2;
	Int halfHeight = height/2;

	if( percent == 0 )
	{
		// We just started, so draw the entire remaining rectangle.
		// #1, #2, #3, and #4
		m_2DRender->Add_Rect( RectClass( static_cast<float>(startX), static_cast<float>(startY), static_cast<float>(endX), static_cast<float>(endY) ), static_cast<float>(0), 0, color );
	}
	else if( percent < 25 )
	{
		//1-25%
		//-----

		//Rectangle #3 & 4
		m_2DRender->Add_Rect( RectClass( static_cast<float>(startX), static_cast<float>(startY), static_cast<float>(midX), static_cast<float>(endY) ), static_cast<float>(0), 0, color );
		
		//Rectangle #2
		m_2DRender->Add_Rect( RectClass( static_cast<float>(midX), static_cast<float>(midY), static_cast<float>(endX), static_cast<float>(endY) ), static_cast<float>(0), 0, color );

		//Handle rectangle #1 than needs partial rendering.
		if( percent < 13 )
		{
			//1-12%
  		//-----

			//Draw the 2nd half of rectangle #1
			m_2DRender->Add_Tri( Vector2( static_cast<float>(midX), static_cast<float>(midY) ), Vector2( static_cast<float>(endX), static_cast<float>(midY) ), Vector2( static_cast<float>(endX), static_cast<float>(startY) ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );

			//Draw the last part of the 1st portion of rectangle #1
			Real percentDraw = static_cast<Real>( 13 - percent ) / 13;
			m_2DRender->Add_Tri( Vector2( static_cast<float>(midX), static_cast<float>(midY) ), Vector2( static_cast<float>(endX), static_cast<float>(startY) ), Vector2( static_cast<Real>(endX) - static_cast<Real>(halfWidth) * percentDraw, static_cast<float>(startY) ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );
		}
		else
		{
			//13-24%
			//------

			//Draw the last part of the 2nd half of rectangle #1
			Real percentDraw = static_cast<Real>( percent - 13 ) / 12;
			m_2DRender->Add_Tri( Vector2( static_cast<float>(midX), static_cast<float>(midY) ), Vector2( static_cast<float>(endX), static_cast<float>(midY) ), Vector2( static_cast<float>(endX), static_cast<Real>(startY) + static_cast<Real>(halfHeight) * percentDraw ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );
		}
	}
	else if( percent < 50 )
	{
		//25-49%
		//------

		//rectangle #3 & 4
		m_2DRender->Add_Rect( RectClass( static_cast<float>(startX), static_cast<float>(startY), static_cast<float>(midX), static_cast<float>(endY) ), static_cast<float>(0), 0, color );

		//Handle rectangle #2 that needs partial rendering.
		if( percent < 38 )
		{
			//25-37%
  		//-----

			//Draw the 2nd half of rectangle #2
			m_2DRender->Add_Tri( Vector2( static_cast<float>(midX), static_cast<float>(midY) ), Vector2( static_cast<float>(midX), static_cast<float>(endY) ), Vector2( static_cast<float>(endX), static_cast<float>(endY) ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );

			//Draw the last part of the 1st portion of rectangle #2
			Real percentDraw = static_cast<Real>( percent - 25 ) / 13;
			m_2DRender->Add_Tri( Vector2( static_cast<float>(midX), static_cast<float>(midY) ), Vector2( static_cast<float>(endX), static_cast<float>(endY) ), Vector2( static_cast<float>(endX), static_cast<Real>(midY) + static_cast<Real>(halfHeight) * percentDraw ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );
		}
		else
		{
			//38-49%
			//------

			//Draw the last part of the 2nd half of rectangle #1
			Real percentDraw = static_cast<Real>( percent - 38 ) / 12;
			m_2DRender->Add_Tri( Vector2( static_cast<float>(midX), static_cast<float>(midY) ), Vector2( static_cast<float>(midX), static_cast<float>(endY) ), Vector2( static_cast<Real>(endX) - static_cast<Real>(halfWidth) * percentDraw, static_cast<float>(endY) ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );
		}
	}
	else if( percent < 75 )
	{
		//50-74%
		//------

		//Rectangle #4
		m_2DRender->Add_Rect( RectClass( static_cast<float>(startX), static_cast<float>(startY), static_cast<float>(midX), static_cast<float>(midY) ), static_cast<float>(0), 0, color );

		//Handle rectangle #3 that needs partial rendering.
		if( percent < 63 )
		{
			//50-62%
  		//-----

			//Draw the 2nd half of rectangle #3
			m_2DRender->Add_Tri( Vector2( static_cast<float>(midX), static_cast<float>(midY) ), Vector2( static_cast<float>(startX), static_cast<float>(midY) ), Vector2( static_cast<float>(startX), static_cast<float>(endY) ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );

			//Draw the last part of the 1st portion of rectangle #3
			Real percentDraw = static_cast<Real>( percent - 50 ) / 13;
			m_2DRender->Add_Tri( Vector2( static_cast<float>(midX), static_cast<float>(midY) ), Vector2( static_cast<float>(startX), static_cast<float>(endY) ), Vector2( static_cast<Real>(midX) - static_cast<Real>(halfWidth) * percentDraw, static_cast<float>(endY) ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );
		}
		else
		{
			//62-74%
			//------

			//Draw the last part of the 2nd half of rectangle #3
			Real percentDraw = static_cast<Real>( percent - 62 ) / 12;
			m_2DRender->Add_Tri( Vector2( static_cast<float>(midX), static_cast<float>(midY) ), Vector2( static_cast<float>(startX), static_cast<float>(midY) ), Vector2( static_cast<float>(startX), static_cast<Real>(endY) - static_cast<Real>(halfHeight) * percentDraw ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );
		}
	}
	else
	{
		//75-99%
		//------
		
		//Handle rectangle #4 that needs partial rendering.
		if( percent < 87 )
		{
			//75-87%
  		//-----

			//Draw the 2nd half of rectangle #4
			m_2DRender->Add_Tri( Vector2( static_cast<float>(midX), static_cast<float>(midY) ), Vector2( static_cast<float>(midX), static_cast<float>(startY) ), Vector2( static_cast<float>(startX), static_cast<float>(startY) ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );

			//Draw the last part of the 1st portion of rectangle #4
			Real percentDraw = static_cast<Real>( percent - 75 ) / 13;
			m_2DRender->Add_Tri( Vector2( static_cast<float>(midX), static_cast<float>(midY) ), Vector2( static_cast<float>(startX), static_cast<float>(startY) ), Vector2( static_cast<float>(startX), static_cast<Real>(midY) - static_cast<Real>(halfHeight) * percentDraw ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );
		}
		else
		{
			//88-99%
			//------

			//Draw the last part of the 2nd half of rectangle #4
			Real percentDraw = static_cast<Real>( percent - 88 ) / 12;
			m_2DRender->Add_Tri( Vector2( static_cast<float>(midX), static_cast<float>(midY) ), Vector2( static_cast<float>(midX), static_cast<float>(startY) ), Vector2( static_cast<Real>(startX) + static_cast<Real>(halfWidth) * percentDraw, static_cast<float>(startY) ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );
		}
	}

	// render it now!
	m_2DRender->Render();
}

// W3DDisplay::drawImage ======================================================
/** Draws an images at the screen coordinates and keeps it within the end
	* screen coords specified */
//=============================================================================
void W3DDisplay::drawImage( const Image *image, Int startX, Int startY, 
														Int endX, Int endY, Color color, DrawImageMode mode)
{

	// sanity
	if( image == nullptr )
		return;

	// !!
	// Remember to update the GUIEditDisplay::drawImage when you make
	// changes to this, it technically uses W3D code to render itself,
	// but it not derived on the W3DDisplay
	// !!

	const Region2D *uv = image->getUV();

	m_2DRender->Reset();
	m_2DRender->Enable_Texturing( TRUE );

	Bool doAlphaReset=FALSE;

	///@todo: Why are we alpha blending all images?  Reduces our fillrate. -MW
	switch (mode)
	{
		case DrawImageMode::DRAW_IMAGE_ALPHA:	//nothing to do since alpha is the default state
			break;
		case DrawImageMode::DRAW_IMAGE_GRAYSCALE:
			m_2DRender->Enable_Grayscale(true);
			break;
		case DrawImageMode::DRAW_IMAGE_ADDITIVE:
			m_2DRender->Enable_Additive(true);
			doAlphaReset = TRUE;
			break;
		case DrawImageMode::DRAW_IMAGE_SOLID:
			m_2DRender->Enable_Additive(false);
			m_2DRender->Enable_Alpha(false);
			doAlphaReset = TRUE;
			break;
		default:
			break;
	}

	// if we have raw texture data we will use it, otherwise we are referencing filenames
	if( BitTest( image->getStatus(), IMAGE_STATUS_RAW_TEXTURE ) )
		m_2DRender->Set_Texture( (TextureClass *)(image->getRawTextureData()) );
	else
		m_2DRender->Set_Texture( image->getFilename().str() );

	RectClass screen_rect(static_cast<float>(startX),static_cast<float>(startY),static_cast<float>(endX),static_cast<float>(endY));
	RectClass uv_rect(uv->lo.x,uv->lo.y,uv->hi.x,uv->hi.y);

	if (m_isClippedEnabled)
	{	//need to clip this quad to clip rectangle

		//
		//	Check for completely clipped
		//
		if (	endX <= m_clipRegion.lo.x ||
				endY <= m_clipRegion.lo.y)
		{
			return;	//nothing to render
		} else {
			RectClass clipped_rect;
			RectClass clipped_uv_rect;

			if( BitTest( image->getStatus(), IMAGE_STATUS_ROTATED_90_CLOCKWISE ) )
			{

	
				//
				//	Clip the polygons to the specified area
				//
				
				clipped_rect.Left		= __max (screen_rect.Left, m_clipRegion.lo.x);
				clipped_rect.Right	= __min (screen_rect.Right, m_clipRegion.hi.x);
				clipped_rect.Top		= __max (screen_rect.Top, m_clipRegion.lo.y);
				clipped_rect.Bottom	= __min (screen_rect.Bottom, m_clipRegion.hi.y);

				//
				//	Clip the texture to the specified area
				//
				
				float percent				= ((clipped_rect.Left - screen_rect.Left) / screen_rect.Width ());
				clipped_uv_rect.Top		= uv_rect.Top + (uv_rect.Height () * percent);

				percent						= ((clipped_rect.Right - screen_rect.Left) / screen_rect.Width ());
				clipped_uv_rect.Bottom	= uv_rect.Top + (uv_rect.Height () * percent);

				percent						= ((clipped_rect.Top - screen_rect.Top) / screen_rect.Height ());
				clipped_uv_rect.Right	= uv_rect.Right - (uv_rect.Width () * percent);

				percent						= ((clipped_rect.Bottom - screen_rect.Top) / screen_rect.Height ());
				clipped_uv_rect.Left		= uv_rect.Right - (uv_rect.Width () * percent);
			}
			else

			{
			
				//
				//	Clip the polygons to the specified area
				//
				
				clipped_rect.Left		= __max (screen_rect.Left, m_clipRegion.lo.x);
				clipped_rect.Right	= __min (screen_rect.Right, m_clipRegion.hi.x);
				clipped_rect.Top		= __max (screen_rect.Top, m_clipRegion.lo.y);
				clipped_rect.Bottom	= __min (screen_rect.Bottom, m_clipRegion.hi.y);

				//
				//	Clip the texture to the specified area
				//
				
				float percent				= ((clipped_rect.Left - screen_rect.Left) / screen_rect.Width ());
				clipped_uv_rect.Left		= uv_rect.Left + (uv_rect.Width () * percent);

				percent						= ((clipped_rect.Right - screen_rect.Left) / screen_rect.Width ());
				clipped_uv_rect.Right	= uv_rect.Left + (uv_rect.Width () * percent);

				percent						= ((clipped_rect.Top - screen_rect.Top) / screen_rect.Height ());
				clipped_uv_rect.Top		= uv_rect.Top + (uv_rect.Height () * percent);

				percent						= ((clipped_rect.Bottom - screen_rect.Top) / screen_rect.Height ());
				clipped_uv_rect.Bottom	= uv_rect.Top + (uv_rect.Height () * percent);
			}

			//
			//	Use the clipped rectangles to render
			//
			screen_rect = clipped_rect;
			uv_rect		= clipped_uv_rect;
		}
	}

	// if rotated 90 degrees clockwise we have to adjust the uv coords
	if( BitTest( image->getStatus(), IMAGE_STATUS_ROTATED_90_CLOCKWISE ) )
	{

		m_2DRender->Add_Tri( Vector2( screen_rect.Left, screen_rect.Top ), 
												 Vector2( screen_rect.Left, screen_rect.Bottom ),
												 Vector2( screen_rect.Right, screen_rect.Top ),
												 Vector2( uv_rect.Right, uv_rect.Top),
												 Vector2( uv_rect.Left, uv_rect.Top),
												 Vector2( uv_rect.Right, uv_rect.Bottom ),
												 color );

		m_2DRender->Add_Tri( Vector2( screen_rect.Right, screen_rect.Bottom ),
												 Vector2( screen_rect.Right, screen_rect.Top ),
												 Vector2( screen_rect.Left, screen_rect.Bottom ),
												 Vector2( uv_rect.Left, uv_rect.Bottom ),
												 Vector2( uv_rect.Right, uv_rect.Bottom ),
												 Vector2( uv_rect.Left, uv_rect.Top ),
												 color );

	}  // end if
	else
	{

		// just draw as normal
		m_2DRender->Add_Quad( screen_rect, uv_rect, color );

	}  // end else

	m_2DRender->Render();

	//reset to default states for next time this method is called.
	m_2DRender->Enable_Grayscale(false);	//never leave it in this mode
	if (doAlphaReset)
		m_2DRender->Enable_Alpha(true);

}  // end drawImage

VideoBuffer*	W3DDisplay::createVideoBuffer( void )
{
	VideoBuffer::Type format = VideoBuffer::TYPE_UNKNOWN;

	/// @todo query video player for supported formats - we assume bink formats here

	// first try to use the native format

	WW3DFormat displayFormat = RenderDevice::getBackBufferFormat();

	if ( RenderCaps::Support_Texture_Format( displayFormat ))
	{
		format = W3DVideoBuffer::W3DFormatToType( displayFormat );
	}

	if ( format == VideoBuffer::TYPE_UNKNOWN )
	{
		if ( RenderCaps::Support_Texture_Format( WW3D_FORMAT_X8R8G8B8 ))
		{
			format = VideoBuffer::TYPE_X8R8G8B8;
		}
		else if ( RenderCaps::Support_Texture_Format( WW3D_FORMAT_R8G8B8 ))
		{
			format = VideoBuffer::TYPE_R8G8B8;
		}
		else if ( RenderCaps::Support_Texture_Format( WW3D_FORMAT_R5G6B5 ))
		{
			format = VideoBuffer::TYPE_R5G6B5;
		}
		else if ( RenderCaps::Support_Texture_Format( WW3D_FORMAT_X1R5G5B5 ))
		{
			format = VideoBuffer::TYPE_X1R5G5B5;
		}
		else
		{
			// card does not support any of the formats we need
			return nullptr;
		}
	}
	// on low mem machines, render every video in 16bit except for the EA Logo movie
	if(!TheGlobalData->m_playIntro )
		format = VideoBuffer::TYPE_R5G6B5;

	W3DVideoBuffer *buffer = NEW W3DVideoBuffer( format );

	return buffer;
}

void W3DDisplay::drawVideoBuffer( VideoBuffer *buffer, Int startX, Int startY, Int endX, Int endY )
{
	W3DVideoBuffer *vbuffer = static_cast<W3DVideoBuffer*>(buffer);

	// The video texture is padded to a power of two. Clamp and move the texture coordinates half a
	// texel inwards, so bilinear filtering does not blend padding texels or the opposite edge into
	// the image border.
	TextureClass *tex = vbuffer->texture();
	if ( tex )
	{
		tex->Get_Filter().Set_U_Addr_Mode( TextureFilterClass::TEXTURE_ADDRESS_CLAMP );
		tex->Get_Filter().Set_V_Addr_Mode( TextureFilterClass::TEXTURE_ADDRESS_CLAMP );
	}
	RectClass uv = vbuffer->Rect( 0, 0, 1, 1 );
	const Real texW = static_cast<Real>(vbuffer->textureWidth());
	const Real texH = static_cast<Real>(vbuffer->textureHeight());
	if ( texW > 0.0f && texH > 0.0f )
	{
		uv.Left   += 0.5f / texW;
		uv.Right  -= 0.5f / texW;
		uv.Top    += 0.5f / texH;
		uv.Bottom -= 0.5f / texH;
	}
	m_2DRender->Reset();
	m_2DRender->Enable_Texturing( TRUE );
	m_2DRender->Set_Texture( tex );
	m_2DRender->Add_Quad( RectClass( static_cast<float>(startX), static_cast<float>(startY), static_cast<float>(endX), static_cast<float>(endY) ), uv );
	m_2DRender->Render();

}

// W3DDisplay::setClipRegion ============================================
/** Set the clipping region for images.
  @todo: Make this work for all primitives, not just drawImage. */
//=============================================================================
void W3DDisplay::setClipRegion( IRegion2D *region )
{
		// assign new region
		m_clipRegion = *region;
		m_isClippedEnabled = TRUE;

}  // end setClipRegion
