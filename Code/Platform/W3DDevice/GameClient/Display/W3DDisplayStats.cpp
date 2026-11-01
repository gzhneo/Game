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
#include "W3DDisplayInternal.h"

// W3DDisplay: statistics -- the debug and frame rate displays

// W3DDisplay::gatherDebugStats ===================================================
/** Compute and display debug stats on screen */
//=============================================================================
void W3DDisplay::gatherDebugStats( void )
{
	static UnsignedInt s_framesRenderedSinceLastUpdate = 0;
	static Int64 s_lastUpdateTime64 = 0;
	static double s_timeSinceLastUpdateInSecs = 0.0;
	static Int s_drawCallsSinceLastUpdate = 0;
	static Int s_sortedPolysSinceLastUpdate = 0;

	// allocate the display strings if needed
	if( m_displayStrings[0] == nullptr )
	{
		GameFont *font;
		if (TheGlobalLanguageData && TheGlobalLanguageData->m_nativeDebugDisplay.name.isNotEmpty())
		{
			font=TheFontLibrary->getFont(
				TheGlobalLanguageData->m_nativeDebugDisplay.name,
				TheGlobalLanguageData->m_nativeDebugDisplay.size,
				TheGlobalLanguageData->m_nativeDebugDisplay.bold);
		}
		else
			font = TheFontLibrary->getFont( AsciiString("FixedSys"), 8, FALSE );

		for (int i = 0; i < DisplayStringCount; i++)
		{
			if (m_displayStrings[i] == nullptr)
			{
				m_displayStrings[i] = TheDisplayStringManager->newDisplayString();
				DEBUG_ASSERTCRASH( m_displayStrings[i], ("Failed to create DisplayString") );
				m_displayStrings[i]->setFont( font );
			}
		}

	}  // end if

	if (m_benchmarkDisplayString == nullptr)
	{
		GameFont *thisFont = TheFontLibrary->getFont( AsciiString("FixedSys"), 8, FALSE );
		m_benchmarkDisplayString = TheDisplayStringManager->newDisplayString();
		DEBUG_ASSERTCRASH( m_benchmarkDisplayString, ("Failed to create DisplayString") );
		m_benchmarkDisplayString->setFont( thisFont );
	}

	++s_framesRenderedSinceLastUpdate;
  s_drawCallsSinceLastUpdate += Debug_Statistics::Get_Draw_Calls();
	s_sortedPolysSinceLastUpdate += Debug_Statistics::Get_Sorting_Polygons();

	Int64 freq64 = getPerformanceCounterFrequency();
	Int64 time64 = getPerformanceCounter();

	s_timeSinceLastUpdateInSecs = (static_cast<double>(time64 - s_lastUpdateTime64) / static_cast<double>(freq64));

#ifdef EXTENDED_STATS
		static FILE *pListFile = nullptr;
		static Int64 lastFrameTime=0;
		static samples = 0;
		if (pListFile == nullptr) {
			pListFile = fopen("FrameRateLog.txt", "w");
		}
		samples++;
		if (pListFile && lastFrameTime && samples<100) {
			float timeSinceLastFrame = (float)((double)(time64-lastFrameTime) / (double)(freq64));
			fprintf(pListFile, "%d ", (int)(1/timeSinceLastFrame));
		}
		lastFrameTime = time64;
#endif

	// we update stats on a delay
	const Real UPDATE_RATE_SECS = 2.0;
	if( s_timeSinceLastUpdateInSecs >= UPDATE_RATE_SECS || TheGlobalData->m_constantDebugUpdate )
	{	
		UnicodeString unibuffer, unibuffer2;
		UnicodeString fpsString;
			
		// setup texture stats
		Debug_Statistics::Record_Texture_Mode(Debug_Statistics::RECORD_TEXTURE_SIMPLE/*RECORD_TEXTURE_NONE*/);

		// frames per second	
		double fps = static_cast<Real>(s_framesRenderedSinceLastUpdate) / s_timeSinceLastUpdateInSecs;
		double drawsPerFrame = Debug_Statistics::Get_Draw_Calls(); //(Real)s_drawCallsSinceLastUpdate / (Real)s_framesRenderedSinceLastUpdate;
		double sortPolysPerFrame = Debug_Statistics::Get_Sorting_Polygons();  //(Real)s_sortedPolysSinceLastUpdate / (Real)s_framesRenderedSinceLastUpdate;
		double skinDrawsPerFrame = Debug_Statistics::Get_Device_Skin_Renders();

		if (fps<0.1) fps = 0.1;

		double ms = 1000.0f/fps;


#if defined(_DEBUG)
		double cumuTime = (static_cast<double>(time64 - m_timerAtCumuFPSStart) / static_cast<double>(freq64));
		if (cumuTime < 0.0) cumuTime = 0.0;
		Int numFrames = static_cast<Int>(TheGameLogic->getFrame()) - static_cast<Int>(START_CUMU_FRAME);
		double cumuFPS = (numFrames > 0 && cumuTime > 0.0) ? (numFrames / cumuTime) : 0.0;
		double skinPolysPerFrame = Debug_Statistics::Get_Device_Skin_Polygons();

		Int LOD = TheGlobalData->m_terrainLOD;
		//unibuffer.format( L"FPS: %.2f, %.2fms mapLOD=%d [cumu FPS=%.2f] draws: %.2f sort: %.2f", fps, ms, LOD, cumuFPS, drawsPerFrame,sortPolysPerFrame);
		if (TheGlobalData->m_useFpsLimit) 
				unibuffer.format( L"%.2f/%d FPS, ", fps, TheGameEngine->getFramesPerSecondLimit());
		else
				unibuffer.format( L"%.2f FPS, ", fps);

		unibuffer2.format( L"%.2fms [cumuFPS=%.2f] draws: %d skins: %d sortP: %d skinP: %d LOD %d", ms, cumuFPS, static_cast<Int>(drawsPerFrame),static_cast<Int>(skinDrawsPerFrame),static_cast<Int>(sortPolysPerFrame), static_cast<Int>(skinPolysPerFrame), LOD);
		unibuffer.concat(unibuffer2);
#else
		//Int LOD = TheGlobalData->m_terrainLOD;
		//unibuffer.format( L"FPS: %.2f, %.2fms mapLOD=%d draws: %.2f sort %.2f", fps, ms, LOD, drawsPerFrame,sortPolysPerFrame);
		unibuffer.format( L"FPS: %.2f, %.2fms draws: %.2f skins: %.2f sort %.2f", fps, ms, drawsPerFrame,skinDrawsPerFrame,sortPolysPerFrame);
		if (TheGlobalData->m_useFpsLimit) 
		{
			unibuffer2.format(L", FPSLock %d",TheGlobalData->m_framesPerSecondLimit);
			unibuffer.concat(unibuffer2);
		}
#endif

		fpsString.format( L"FPS: %.2f", fps);
		m_benchmarkDisplayString->setText( fpsString );

		Int polyPerFrame = Debug_Statistics::Get_Device_Polygons();

#ifdef EXTENDED_STATS
		static float gameOverheadMS = 0.0f;
		static float consoleMS = 0.0f;
		static float threeDOverheadMS = 0.0f;
		static float terrainMS = 0.0f;
		static float objectMS = 0.0f;
		static float overlapMS = 0.0f;
		static int  extendedStats = 0;
		const int SHOW_STATS_TIME=12; // show extended stats for 5 cycles == 10 seconds.
		static enum {disabled, sync, gameOverhead, console, threeDOverhead, terrain, objects, overlap, normal} statMode = disabled;

		if (statMode == sync) {
			extendedStats = SHOW_STATS_TIME;
			statMode = gameOverhead;
		} else if (statMode == gameOverhead) {
			gameOverheadMS = ms;
			statMode = console;
			RenderDevice::stats.m_disableTerrain = true;
			RenderDevice::stats.m_disableOverhead = true;
			RenderDevice::stats.m_disableWater = true;
			RenderDevice::stats.m_disableObjects = true;
			RenderDevice::stats.m_disableConsole = false;
			RenderDevice::stats.m_debugLinesToShow = 1;
		} else if (statMode == console) {
			consoleMS = ms;
			statMode = threeDOverhead;
			RenderDevice::stats.m_disableTerrain = true;
			RenderDevice::stats.m_disableOverhead = true;
			RenderDevice::stats.m_disableWater = true;
			RenderDevice::stats.m_disableObjects = true;
			RenderDevice::stats.m_disableConsole = true;
			RenderDevice::stats.m_debugLinesToShow = 1;
		} else if (statMode == threeDOverhead) {				 
			threeDOverheadMS = ms;
			statMode = terrain;
			RenderDevice::stats.m_disableTerrain = false;
			RenderDevice::stats.m_disableOverhead = true;
			RenderDevice::stats.m_disableWater = true;
			RenderDevice::stats.m_disableObjects = true;
			RenderDevice::stats.m_disableConsole = true;
			RenderDevice::stats.m_debugLinesToShow = 1;
		} else if (statMode == terrain) {
			terrainMS = ms;
			statMode = objects;
			RenderDevice::stats.m_disableOverhead = true;
			RenderDevice::stats.m_disableTerrain = true;
			RenderDevice::stats.m_disableWater = true;
			RenderDevice::stats.m_disableObjects = false;
			RenderDevice::stats.m_disableConsole = true;
			RenderDevice::stats.m_debugLinesToShow = 1;
		} else if (statMode == objects) {
			objectMS = ms;
			statMode = overlap;
			RenderDevice::stats.m_disableOverhead = false;
			RenderDevice::stats.m_disableTerrain = false;
			RenderDevice::stats.m_disableWater = false;
			RenderDevice::stats.m_disableObjects = false;
			RenderDevice::stats.m_disableConsole = true;
			RenderDevice::stats.m_sleepTime = (int)(terrainMS);
			RenderDevice::stats.m_debugLinesToShow = 1;
		} else if (statMode == overlap) {
			overlapMS = ms;
			statMode = normal;
			RenderDevice::stats.m_disableOverhead = false;
			RenderDevice::stats.m_disableTerrain = false;
			RenderDevice::stats.m_disableWater = false;
			RenderDevice::stats.m_disableObjects = false;
			RenderDevice::stats.m_disableConsole = true;
			RenderDevice::stats.m_sleepTime = 0;
			RenderDevice::stats.m_debugLinesToShow = 1;
		} else if (statMode == normal) {
			overlapMS = (ms + ((int)terrainMS) - overlapMS );
			statMode = disabled;
			extendedStats = SHOW_STATS_TIME;

			// Done collecting stats. Re-enable stuff
			RenderDevice::stats.m_disableConsole = false;
			RenderDevice::stats.m_debugLinesToShow = -1;
		} else if (!RenderDevice::stats.m_showingStats) {
			// start collecting extended info. 
			RenderDevice::stats.m_showingStats = true;
			RenderDevice::stats.m_disableOverhead = false;
			RenderDevice::stats.m_disableTerrain = true;
			RenderDevice::stats.m_disableWater = true;
			RenderDevice::stats.m_disableObjects = true;
			RenderDevice::stats.m_disableConsole = true;
			RenderDevice::stats.m_debugLinesToShow = 1;
			statMode = sync;
			gameOverheadMS = 0.0f;
			threeDOverheadMS = 0.0f;
			terrainMS = 0.0f;
			objectMS = 0.0f;
		}
		if (statMode != disabled) {
			unibuffer.format(L"FPS: %.2f, %.2fms - Collecting extended stats.", fps, ms);
		} else if (extendedStats>0) {
			extendedStats--;
			unibuffer.format( L"FPS: %.2f, %.2fms - OH %.2fms, Console %.2fms, 3D OH %.2fms, Terrain %.2fms, Obs %.2fms, CPU %.2fms", 
				fps, ms, gameOverheadMS, consoleMS, threeDOverheadMS, terrainMS, objectMS, overlapMS);
			if (extendedStats==SHOW_STATS_TIME-2) {
				char bufferA[ 256 ];
				WWFormat::Into(bufferA, "FPS: {:.2f}, {:.2f}ms - OH {:.2f}ms, Console {:.2f}ms, 3D OH {:.2f}ms, Terrain {:.2f}ms, Obs {:.2f}ms, CPU {:.2f}ms\n", fps, ms, gameOverheadMS, consoleMS, threeDOverheadMS, terrainMS, objectMS, overlapMS);
				::OutputDebugString(bufferA);
				if (pListFile) {
					fprintf(pListFile, "\n%s", bufferA);
				}				
				WWFormat::Into(bufferA, "Polygons: per frame {:d}, per second {:d}\n", polyPerFrame, (Int)(polyPerFrame*fps));
				::OutputDebugString(bufferA);
				if (pListFile) {
					fprintf(pListFile, "%s", bufferA);
					fflush(pListFile);
				}				
			}
		} 
 		if (pListFile) {
			fprintf(pListFile, "\nFPS: %.2f, %.2fms\n", fps, ms);
			fflush(pListFile);
		}				
		if (pListFile) {
			samples = 0;
			if (statMode != disabled) {
				fprintf(pListFile, "Stat%d-", statMode);
			} 
		}				

#endif
#ifdef _DEBUG
		unibuffer.concat(L", DEBUG app");
#endif

		m_displayStrings[FPS]->setText( unibuffer );

		// Actual GameLogic frame number
		unibuffer.format(L"Frame: %d", TheGameLogic->getFrame());
		m_displayStrings[Frame]->setText( unibuffer );

		// polygons this frame	
		unibuffer.format( L"Polygons: per frame %d, per second %d", polyPerFrame,
				static_cast<Int>(polyPerFrame*fps));
		m_displayStrings[Polygons]->setText( unibuffer );

		// vertices this frame
		unibuffer.format( L"Vertices: %d", Debug_Statistics::Get_Device_Vertices() );
		m_displayStrings[Vertices]->setText( unibuffer );		

		//
		// I'm adjusting the texture memory usage counter by subtracting 
		// out the terrain alpha texture (since it's really == terrain texture).
		//
		unibuffer.format( L"Video RAM: %d", Debug_Statistics::Get_Record_Texture_Size() - 1376256 );
		m_displayStrings[VideoRam]->setText( unibuffer );

		s_lastUpdateTime64 = time64;
		s_timeSinceLastUpdateInSecs = 0.0f;
		s_framesRenderedSinceLastUpdate = 0;
		s_drawCallsSinceLastUpdate = 0;
		s_sortedPolysSinceLastUpdate = 0;

		// terrain stats
		unibuffer.format( L"3-Way Blends: %d/%d, Shoreline Blends: %d/%d", TheTerrainRenderObject->getNumExtraBlendTiles(TRUE),
			TheTerrainRenderObject->getNumExtraBlendTiles(FALSE),
			TheTerrainRenderObject->getNumShoreLineTiles(TRUE),
			TheTerrainRenderObject->getNumShoreLineTiles(FALSE));
		m_displayStrings[TerrainStats]->setText( unibuffer );

		// misc debug info
		Coord3D camPos;
		TheTacticalView->getPosition(&camPos);
		Real zoom = TheTacticalView->getZoom();
		Real pitch = TheTacticalView->getPitch();
		Real FXPitch = TheTacticalView->getFXPitch();
		Real angle = TheTacticalView->getAngle();
		Real FOV = TheTacticalView->getFieldOfView();
		//Real desiredHeight = TheTacticalView->getHeightAboveGround();
		Real terrainHeight = TheTacticalView->getTerrainHeightUnderCamera();
		Real actualHeightAboveGround = TheTacticalView->getCurrentHeightAboveGround();

		unibuffer.format( L"Camera zoom: %g, pitch: %g/%g, yaw: %g, pos: %g, %g, %g, FOV: %g\n       Height above ground: %g Terrain height: %g",
												zoom,
												pitch,
												FXPitch,
												angle,
												camPos.x, camPos.y, camPos.z,
												FOV,
												/*
												zoom,
												pitch * 180.0f / PI,
												FXPitch * 180.0f / PI,
												angle * 180.0f / PI,
												camPos.x, camPos.y, camPos.z,
												FOV * 180.0f / PI,
												*/
												actualHeightAboveGround, terrainHeight );
		m_displayStrings[DebugInfo]->setText( unibuffer );

		// display the keyboard modifier and mouse states.
		unibuffer.format( L"States: " );
		if( TheKeyboard->isShift() )
		{
			unibuffer.concat( L"Shift(" );
			if( TheKeyboard->getModifierFlags() & KEY_STATE_LSHIFT )
			{
				unibuffer.concat( L"L" );
			}
			if( TheKeyboard->getModifierFlags() & KEY_STATE_RSHIFT )
			{
				unibuffer.concat( L"R" );
			}
			unibuffer.concat( L") " );
		}
		if( TheKeyboard->isCtrl() )
		{
			unibuffer.concat( L"Ctrl(" );
			if( TheKeyboard->getModifierFlags() & KEY_STATE_LCONTROL )
			{
				unibuffer.concat( L"L" );
			}
			if( TheKeyboard->getModifierFlags() & KEY_STATE_RCONTROL )
			{
				unibuffer.concat( L"R" );
			}
			unibuffer.concat( L") " );
		}
		if( TheKeyboard->isAlt() )
		{
			unibuffer.concat( L"Alt(" );
			if( TheKeyboard->getModifierFlags() & KEY_STATE_LALT )
			{
				unibuffer.concat( L"L" );
			}
			if( TheKeyboard->getModifierFlags() & KEY_STATE_RALT )
			{
				unibuffer.concat( L"R" );
			}
			unibuffer.concat( L") " );
		}

		const MouseIO *mouseStatus = TheMouse->getMouseStatus();

		if( mouseStatus->leftState )
		{
			unibuffer.concat( L"LMB " );
		}
		if( mouseStatus->middleState )
		{
			unibuffer.concat( L"MMB " );
		}
		if( mouseStatus->rightState )
		{
			unibuffer.concat( L"RMB " );
		}

		Object *object = nullptr;
#if defined(_DEBUG)	//debug hack to view object under mouse stats
		Drawable *draw = 	TheTacticalView->pickDrawable(&TheMousePos, FALSE, static_cast<PickType>(0xffffffff) );
#else
		Drawable *draw = TheGameClient->findDrawableByID( TheInGameUI->getMousedOverDrawableID() );
#endif
		if( draw  )
			object = draw->getObject();
		if( object )
		{
			unibuffer2.format( L"Moused over object: %S (%d) ", object->getTemplate()->getName().str(), object->getID() );
			unibuffer.concat( unibuffer2 );
		}
		else
		{
			unibuffer.concat( L"Moused over object: TERRAIN " );
		}
		
		m_displayStrings[ KEY_MOUSE_STATES ]->setText( unibuffer );

		//display the x and y mouse coordinates
		const MouseIO *mouseIO = TheMouse->getMouseStatus();
		Coord3D worldPos;
		TheTacticalView->screenToTerrain(&mouseIO->pos, &worldPos);
		unibuffer.format( L"Mouse position: screen: (%d, %d), world: (%g, %g, %g)", mouseIO->pos.x, mouseIO->pos.y,
			worldPos.x, worldPos.y, worldPos.z);
		m_displayStrings[MousePosition]->setText( unibuffer );
		
		//display the number of particles in the world and being displayed on screen
		Int totalParticles = TheParticleSystemManager->getParticleCount();
		Int onScreenParticleCount = TheParticleSystemManager->getOnScreenParticleCount();
		unibuffer.format( L"Particles: %d in world, %d being displayed", totalParticles, onScreenParticleCount );
		m_displayStrings[Particles]->setText( unibuffer );

		//display the number of objects in the world
		UnsignedInt objCount = TheGameLogic->getObjectCount();
		UnsignedInt objScreenCount = TheGameClient->getRenderedObjectCount();

		unibuffer.format(L"Objects: %d in world, %d being displayed", objCount, objScreenCount );
		m_displayStrings[Objects]->setText( unibuffer );

		// Network incoming bandwidth stats
		if (TheNetwork != nullptr) {
			unibuffer.format(L"IN: %.2f bytes/sec, %.2f packets/sec",
				TheNetwork->getIncomingBytesPerSecond(), TheNetwork->getIncomingPacketsPerSecond());
			m_displayStrings[NetIncoming]->setText( unibuffer );

			// Network outgoing bandwidth stats
			unibuffer.format(L"OUT: %.2f bytes/sec, %.2f packets/sec",
				TheNetwork->getOutgoingBytesPerSecond(), TheNetwork->getOutgoingPacketsPerSecond());
			m_displayStrings[NetOutgoing]->setText( unibuffer );

			// Network performance stats
			unibuffer.format(L"Run Ahead: %d, Net FPS: %d, Packet arrival cushion: %d",
				TheNetwork->getRunAhead(), TheNetwork->getFrameRate(), TheNetwork->getPacketArrivalCushion());
			m_displayStrings[NetStats]->setText( unibuffer );

			// Client frame rate averages for all players in the game.  This only works right for the packet router.
			unibuffer.clear();
			Int numPlayers = TheNetwork->getNumPlayers();
			for (Int i = 0; i < numPlayers; ++i) {
				UnicodeString tempstr;
				tempstr.format(L"%s: %d ", TheNetwork->getPlayerName(i).str(), TheNetwork->getSlotAverageFPS(i));
				unibuffer.concat(tempstr);
			}
			m_displayStrings[NetFPSAverages]->setText( unibuffer );
		} else {
//			unibuffer.format(L"IN: 0.0 bytes/sec, 0.0 packets/sec");
//			m_displayStrings[NetIncoming]->setText( unibuffer );

			// Network outgoing bandwidth stats
//			unibuffer.format(L"OUT: 0.0 bytes/sec, 0.0 packets/sec");
//			m_displayStrings[NetOutgoing]->setText( unibuffer );
      unibuffer.format(L"");
//			unibuffer.format(L"Network not present");
			m_displayStrings[NetOutgoing]->setText(unibuffer);
			m_displayStrings[NetIncoming]->setText(unibuffer);
			m_displayStrings[NetStats]->setText(unibuffer);
			m_displayStrings[NetFPSAverages]->setText( unibuffer );
		}

		// selected object info stats
		unibuffer.format( L"Select Info: '%d' drawables selected", TheInGameUI->getSelectCount() );
		


		//Sorry, guys. I need a special kluge here to get constantdebug results for angry mob.
		//Do no be cross with me.
		//if there is not exactly one drawable selected it will report on the moused-over drawable
		if (TheInGameUI->getSelectCount() == 1)
			draw = TheInGameUI->getFirstSelectedDrawable();


		if( draw )
		{
			Object *obj = draw->getObject();
			AsciiString objectName;

			objectName.set( "No-Name" );
			if( obj && obj->getName().isEmpty() == FALSE )
				objectName = obj->getName();

			unibuffer.format( L"Select Info: '%S'(%S) at (%.3f,%.3f,%.3f)",
												draw->getTemplate()->getName().str(),
												objectName.str(),
												draw->getPosition()->x,
												draw->getPosition()->y,
												draw->getPosition()->z
											);

			const PhysicsBehavior *physics = obj->getPhysics();
			PhysicsTurningType turnType = physics ? physics->getTurning() : TURN_NONE;

			const DrawableLocoInfo *locoInfo = draw->getLocoInfo();
			if( locoInfo )
			{
				unibuffer2.format( L"\nPhysics Info -- Turn: %d, Pitch(accel): %.3f(%.3f), Roll(accel): %.3f(%.3f)",
													 turnType,
													 locoInfo->m_accelerationPitch, locoInfo->m_accelerationPitchRate,
													 locoInfo->m_accelerationRoll, locoInfo->m_accelerationRollRate );
				unibuffer.concat( unibuffer2 );
			}



		
			

			// (gth) compute some stats about the rendering cost of this drawable
#if defined(_DEBUG)	
			RenderCost rcost;
			for (DrawModule** dm = draw->getDrawModules(); *dm; ++dm)
			{
				(*dm)->getRenderCost(rcost);
			}
			if (rcost.getDrawCallCount() > 0) 
			{
				unibuffer2.format( L"\ndraw calls: %d(+%d) sort meshes: %d skins: %d  bones: %d",rcost.getDrawCallCount(),rcost.getShadowDrawCount(),rcost.getSortedMeshCount(),rcost.getSkinMeshCount(),rcost.getBoneCount());
				unibuffer.concat( unibuffer2 );
			}
#endif

			unibuffer.concat( L"\nModelStates: " );
			ModelConditionFlags mcFlags = draw->getModelConditionFlags();
			const int numEntriesPerLine = 4;
			int lineCount = 0;

			for( int i = 0; i < MODELCONDITION_COUNT; i++ )
			{
				if( mcFlags.test( i ) )
				{
					unibuffer2.format( L"%S ", ModelConditionFlags::getBitNames()[ i ] );
					unibuffer.concat( unibuffer2 );
					lineCount++;
					if( lineCount == numEntriesPerLine )
					{
						lineCount = 0;
						unibuffer.concat( L"\n" );
					}
				}
			}

			//Render ALL modelcondition statii

		}  // end if
		m_displayStrings[ SelectedInfo ]->setText( unibuffer );

	}

}

// W3DDisplay::drawDebugStats =================================================
/** Draw debug statistics */
//=============================================================================
void W3DDisplay::drawDebugStats( void )
{
	Int	x = 3;
	Int	y = 3;
	Color textColor = GameMakeColor( 255, 255, 255, 255 );
	Color dropColor = GameMakeColor( 0, 0, 0, 255 );

	int linesOfStrings = DisplayStringCount;
#ifdef EXTENDED_STATS
	if (RenderDevice::stats.m_debugLinesToShow > -1) 
	{
		linesOfStrings = RenderDevice::stats.m_debugLinesToShow;
	}

#endif


	Int w, h;
	for (int i = 0; i < linesOfStrings; i++)
	{
		m_displayStrings[i]->draw( x, y, textColor, dropColor );
		m_displayStrings[i]->getSize(&w, &h);
		y += h;
	}

}  // end drawDebugStats

// W3DDisplay::drawFPSStats =================================================
/** Draw the FPS on the screen */
//=============================================================================
void W3DDisplay::drawFPSStats( void )
{
	Int	x = 3;
	Int	y = 20;
	Color textColor = GameMakeColor( 255, 255, 255, 255 );
	Color dropColor = GameMakeColor( 0, 0, 0, 255 );

	int linesOfStrings = 1;

	for (int i = 0; i < linesOfStrings; i++)
	{
		m_benchmarkDisplayString->draw( x, y, textColor, dropColor );
	}
}

//=============================================================================
void StatDebugDisplay( DebugDisplayInterface *, void *, FILE *fp )
{
	DEBUG_CRASH(("This should never be called directly, but is just a placeholder for drawDebugStats()"));
}

// W3DDisplay::drawCurrentDebugDisplay =================================================
/** Draw current debug display */
//=============================================================================
void W3DDisplay::drawCurrentDebugDisplay( void )
{
	if (m_debugDisplayCallback == StatDebugDisplay)
	{
		drawDebugStats();
	}
	else
	{
		if ( m_debugDisplay && m_debugDisplayCallback )
		{
			m_debugDisplay->reset();
			m_debugDisplayCallback( m_debugDisplay, m_debugDisplayUserData, nullptr );
		}
	}
}  // end drawCurrentDebugDisplay
