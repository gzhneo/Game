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

// FILE: W3DDisplay.cpp ///////////////////////////////////////////////////////
//
// W3D Implementation for the Game Display which is responsible for creating
// and maintaning the entire visual display
//
// Author: Colin Day, April 2001
//
///////////////////////////////////////////////////////////////////////////////

#if defined(_DEBUG)
static void drawFramerateBar(void);
#endif

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include <stdlib.h>
#include <windows.h>
#include <io.h>
#include <time.h>

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "Common/ThingFactory.h"
#include "formatinto.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "Engine/Clock.h"
#include "Common/GameEngine.h"
#include "Common/GameEdition.h"	// Generals-specific shader and light settings
#include "Common/GlobalData.h"
#include "Common/UserSettings.h"	// VSync, multisampling and render backend from Settings.ini
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
#include "GameLogic/TerrainLogic.h"
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
#include "Win32Device/Common/Win32Monitors.h"

extern HWND ApplicationHWnd;
#include "WWMath/wwmath.h"
#include "WW3D2/System/ww3d.h"
#include "WW3D2/Material/textureloader.h"
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

// DEFINE AND ENUMS ///////////////////////////////////////////////////////////
constexpr auto W3D_DISPLAY_DEFAULT_BIT_DEPTH = 32;

#define no_SAMPLE_DYNAMIC_LIGHT	1
#ifdef SAMPLE_DYNAMIC_LIGHT
static W3DDynamicLight * theDynamicLight = nullptr;
static Real theLightXOffset = 0.1f;
static Real theLightYOffset = 0.07f;
static Int theFlashCount = 0;
#endif

//*****************************************************************************************
//*****************************************************************************************
//**** Start Statistical Dump *************************************************************
//*****************************************************************************************

// Outside DUMP_PERF_STATS: ASSERT_MAIN_THREAD in W3DDisplay::draw needs this header in every build.
#include "Engine/ThreadRole.h"
#include "Lib/SafeString.h"
#include "Engine/SimThread.h"
#include "Engine/TexturePackage.h"
#include "W3DDisplayInternal.h"

#ifdef DUMP_PERF_STATS

#include <cstdarg>
#include "formatinto.h"

class StatDumpClass
{
public:
	StatDumpClass( const char *fname );
	~StatDumpClass();
	void dumpStats( Bool brief = FALSE, Bool flagSpikes = FALSE );

protected:
	FILE *m_fp;
};

//=============================================================================
//Open the file once at the beginning of the game -- everything appends to it.
//=============================================================================
StatDumpClass::StatDumpClass( const char *fname )
{
	char buffer[ _MAX_PATH ];
	GetModuleFileName( nullptr, buffer, sizeof( buffer ) );
	char *pEnd = buffer + strlen( buffer );
	while( pEnd != buffer )
	{
		if( *pEnd == '\\' )
		{
			*pEnd = 0;
			break;
		}
		pEnd--;
	}
	// TheStatDump is a global object, so this constructor runs before initMemoryManager().
	// AsciiString would allocate from the not yet initialized memory pool; use a stack buffer.
	char fullPath[ _MAX_PATH * 2 ];
	WWFormat::Into(fullPath, "{}\\{}", buffer, fname);
	fullPath[ sizeof(fullPath) - 1 ] = 0;
	m_fp = fopen( fullPath, "wt" );
}

//=============================================================================
//Close the file at the end of the application 
//=============================================================================
StatDumpClass::~StatDumpClass()
{
	if( m_fp )
	{
		fclose( m_fp );
	}
}

static const char *getCurrentTimeString(void)
{
	time_t aclock;
	time(&aclock);
	struct tm *newtime = localtime(&aclock);
	return asctime(newtime);
}

//=============================================================================
//Dump the stats
//=============================================================================


static Bool s_notFirstDump = FALSE;

void StatDumpClass::dumpStats( Bool brief, Bool flagSpikes )
{
	if( !m_fp )
	{
		return;
	}

  
  Bool beBrief = brief & s_notFirstDump;
  s_notFirstDump = TRUE;

	fprintf( m_fp, "----------------------------------------------------------------\n" );
	fprintf( m_fp, "Performance Statistical Dump -- Frame %d\n", TheGameLogic->getFrame() );
  if ( ! beBrief )
  {
	  //static char buf[1024];
	  fprintf( m_fp, "Time:\t%s", getCurrentTimeString() );
	  fprintf( m_fp, "Map:\t%s\n", TheGlobalData->m_mapName.str());
	  fprintf( m_fp, "Side:\t%s\n", ThePlayerList->getLocalPlayer()->getSide().str());
	  fprintf( m_fp, "----------------------------------------------------------------\n" );
  }

	//FPS
	Real fps = TheDisplay->getAverageFPS();
	fprintf( m_fp, "Average FPS: %.1f (%.5f msec)\n", fps, 1000.0f / fps );
	if ( flagSpikes && fps<20.0f )
		fprintf( m_fp, "                                                                      FPS OUT OF TOLERANCE\n" );


	//Rendering stats
	fprintf( m_fp, "Draws: %d \nSkins: %d \nSortedPolys: %d \nSkinPolys: %d\n",static_cast<Int>(Debug_Statistics::Get_Draw_Calls()),
		static_cast<Int>(Debug_Statistics::Get_Device_Skin_Renders()),
		static_cast<Int>(Debug_Statistics::Get_Sorting_Polygons()), static_cast<Int>(Debug_Statistics::Get_Device_Skin_Polygons()));

	Int onScreenParticleCount = TheParticleSystemManager->getOnScreenParticleCount();

  if ( flagSpikes )
  {
    if ( Debug_Statistics::Get_Draw_Calls()>2000 )
  	  fprintf( m_fp, "                                                                      DRAWS OUT OF TOLERANCE(2000)\n" );
    if ( Debug_Statistics::Get_Sorting_Polygons() > (onScreenParticleCount*2) + 300 )
  	  fprintf( m_fp, "                                                                      NON-PARTICLE-SORTS OUT OF TOLERANCE(300)\n" );
    if ( Debug_Statistics::Get_Device_Skin_Renders()>100 )
  	  fprintf( m_fp, "                                                                      SKINS OUT OF TOLERANCE(100)\n" );
  }


	//Object stats
	UnsignedInt objCount = TheGameLogic->getObjectCount();
	UnsignedInt objScreenCount = TheGameClient->getRenderedObjectCount();
	fprintf( m_fp, "Objects: %d in world (%d onscreen)\n", objCount, objScreenCount );
	if ( flagSpikes && objCount > 800 )
		fprintf( m_fp, "                                                                      OBJS OUT OF TOLERANCE(800)\n" );

	//AI stats
	UnsignedInt numAI, numMoving, numAttacking, numWaitingForPath, overallFailedPathfinds;
	TheGameLogic->getAIMetricsStatistics( &numAI, &numMoving, &numAttacking, &numWaitingForPath, &overallFailedPathfinds );
	fprintf( m_fp, "\n" );
	fprintf( m_fp, "AI Statistics:\n" );
	fprintf( m_fp, "  Total AI Objects: %d\n", numAI );
	fprintf( m_fp, "    -moving: %d\n", numMoving );
	fprintf( m_fp, "    -attacking: %d\n", numAttacking );
	fprintf( m_fp, "    -waiting for path: %d\n", numWaitingForPath );
	fprintf( m_fp, "  Total failed pathfinds: %d\n", overallFailedPathfinds );
	if ( flagSpikes && overallFailedPathfinds > 0 )
		fprintf( m_fp, "                                                                      FAILEDPATHFINDS OUT OF TOLERANCE(0)\n" );
	fprintf( m_fp, "\n" );

	// Script stats
	Real timeLastFrame, slowScript1, slowScript2;
	AsciiString slowScripts = TheScriptEngine->getStats(&timeLastFrame, &slowScript1, &slowScript2);
	fprintf( m_fp, "\n" );
	fprintf( m_fp, "Script Engine Statistics:\n" );
	fprintf( m_fp, "  Total time last frame: %.5f msec\n", timeLastFrame*1000 );
	fprintf( m_fp, "    -Slowest 2 scripts      %s\n", slowScripts.str() );
	fprintf( m_fp, "    -Slowest 2 script times %.5f msec, %.5f msec \n", slowScript1*1000, slowScript2*1000 );
	if ( flagSpikes && (slowScript1*1000 > 0.2f || slowScript2*1000 > 0.2f) )
		fprintf( m_fp, "                                                                      SLOW SCRIPT OUT OF TOLERANCE(0.2)\n" );
	fprintf( m_fp, "\n" );



	//PartitionMgr stats
	double gcoTimeThisFrameTotal, gcoTimeThisFrameAvg;
	ThePartitionManager->getPMStats(gcoTimeThisFrameTotal, gcoTimeThisFrameAvg);
	fprintf(m_fp, "Partition Manager Statistics:\n");
	fprintf(m_fp, "  Total time for object scans this frame is %.5f msec\n", gcoTimeThisFrameTotal);
	fprintf(m_fp, "  Avg time per object scan this frame is %.5f msec\n", gcoTimeThisFrameAvg);
	fprintf( m_fp, "\n" );

	// setup texture stats
	Debug_Statistics::Record_Texture_Mode(Debug_Statistics::RECORD_TEXTURE_SIMPLE/*RECORD_TEXTURE_NONE*/);

	fprintf( m_fp, "Video Statistics:\n" );
	//Particle system stats
	fprintf( m_fp, "  Particle Systems: %d\n", TheParticleSystemManager->getParticleSystemCount() );
	Int totalParticles = TheParticleSystemManager->getParticleCount();
	fprintf( m_fp, "  Particles: %d in world (%d onscreen)\n", totalParticles, onScreenParticleCount );

	if ( flagSpikes && totalParticles > TheGlobalData->m_maxParticleCount - 10 )
		fprintf( m_fp, "                                                                      PARTICLES OUT OF TOLERANCE(CAP-10)\n" );
	if ( flagSpikes && onScreenParticleCount > TheGlobalData->m_maxParticleCount - 10 )
		fprintf( m_fp, "                                                                      ON_SCREEN_PARTICLES OUT OF TOLERANCE(CAP-10)\n" );


	// polygons this frame	
	Int polyPerFrame = Debug_Statistics::Get_Device_Polygons();
	Int polyPerSecond = static_cast<Int>(polyPerFrame * fps);
	fprintf( m_fp, "  Polygons: %d per frame (%d per second)\n", polyPerFrame, polyPerSecond );

	// vertices this frame
	fprintf( m_fp, "  Vertices: %d\n", Debug_Statistics::Get_Device_Vertices() );

	//
	// I'm adjusting the texture memory usage counter by subtracting 
	// out the terrain alpha texture (since it's really == terrain texture).
	//
	fprintf( m_fp, "  Video RAM: %d\n", Debug_Statistics::Get_Record_Texture_Size() - 1376256 );

	// terrain stats
	fprintf( m_fp, "  3-Way Blends: %d/%d, \n Shoreline Blends: %d/%d\n", TheTerrainRenderObject->getNumExtraBlendTiles(TRUE),TheTerrainRenderObject->getNumExtraBlendTiles(FALSE), TheTerrainRenderObject->getNumShoreLineTiles(TRUE),TheTerrainRenderObject->getNumShoreLineTiles(FALSE));
	if ( flagSpikes && TheTerrainRenderObject->getNumExtraBlendTiles(TRUE) > 2000 )
		fprintf( m_fp, "                                                                      3-WAYS OUT OF TOLERANCE(2000)\n" );
	if ( flagSpikes && TheTerrainRenderObject->getNumShoreLineTiles(TRUE) > 2000 )
		fprintf( m_fp, "                                                                      SHORELINES OUT OF TOLERANCE(2000)\n" );

	fprintf( m_fp, "\n" );

#if defined(_DEBUG)
  if ( ! beBrief )
  {
    TheAudio->audioDebugDisplay( nullptr, nullptr, m_fp );
	  fprintf( m_fp, "\n" );
  }
#endif
	
#ifdef MEMORYPOOL_DEBUG
	//Report memory usage.
	TheMemoryPoolFactory->debugMemoryReport( REPORT_FACTORYINFO | REPORT_POOLINFO, 0, 0, m_fp );
#else
	fprintf( m_fp, "Memory Report -- unavailable \n(build doesn't have MEMORYPOOL_DEBUG defined)\n" );
#endif
	fprintf( m_fp, "\n" );

	fprintf( m_fp, "%s", TheSubsystemList->dumpTimesForAll().str());

  if ( ! beBrief )
  {
	  fprintf( m_fp, "----------------------------------------------------------------\n" );
	  fprintf( m_fp, "END -- Frame %d\n", TheGameLogic->getFrame() );
	  fprintf( m_fp, "----------------------------------------------------------------\n" );
  }
	fprintf( m_fp, "\n\n" );
	fflush(m_fp);
}

StatDumpClass TheStatDump("StatisticsDump.txt");

#endif //DUMP_PERF_STATS

//*****************************************************************************************
//**** End Statistical Dump ***************************************************************
//*****************************************************************************************
//*****************************************************************************************



///////////////////////////////////////////////////////////////////////////////
// DEFINITIONS ////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

//=============================================================================
RTS3DScene *W3DDisplay::m_3DScene = nullptr;
RTS2DScene *W3DDisplay::m_2DScene = nullptr;
RTS3DInterfaceScene *W3DDisplay::m_3DInterfaceScene = nullptr;
W3DAssetManager *W3DDisplay::m_assetManager = nullptr;

// W3DDisplay::W3DDisplay =====================================================
/** */
//=============================================================================
W3DDisplay::W3DDisplay()
{
	Int i;

	m_initialized = false;
	m_assetManager = nullptr;
	m_3DScene = nullptr;
	m_2DScene = nullptr;
	m_3DInterfaceScene = nullptr;
	m_averageFPS = static_cast<Real>(TheGlobalData->m_framesPerSecondLimit);
#if defined(_DEBUG)
	m_timerAtCumuFPSStart = 0;
#endif
	for (i=0; i<LightEnvironmentClass::MAX_LIGHTS; i++)
		m_myLight[i] = nullptr;
	m_2DRender = nullptr;
	m_isClippedEnabled = FALSE;
	m_clipRegion.lo.x = 0;
	m_clipRegion.lo.y = 0;
	m_clipRegion.hi.x = 0;
	m_clipRegion.hi.y = 0;

	for (i = 0; i < DisplayStringCount; i++)
		m_displayStrings[i] = nullptr;
	m_benchmarkDisplayString = nullptr;	// gatherDebugStats checks for nullptr

}  // end W3DDisplay

// W3DDisplay::~W3DDisplay ====================================================
/** */
//=============================================================================
W3DDisplay::~W3DDisplay()
{
	::Engine::SimThread::setRenderOwnerHook( nullptr );

	// Release the particle manager textures while the device is still alive.
	if (TheParticleSystemManager)
		static_cast<W3DParticleSystemManager*>(TheParticleSystemManager)->releaseDeviceResources();


	// get rid of the debug display
	delete m_debugDisplay;

	// delete the display strings
	for (int i = 0; i < DisplayStringCount; i++)
		TheDisplayStringManager->freeDisplayString(m_displayStrings[i]);
	// Free the performance display text (gatherDebugStats) too; a string still registered with the
	// DisplayStringManager trips its assertion on shutdown.
	if (m_benchmarkDisplayString)
	{
		TheDisplayStringManager->freeDisplayString(m_benchmarkDisplayString);
		m_benchmarkDisplayString = nullptr;
	}

	// delete 2D renderer
	if( m_2DRender )
	{

		m_2DRender->Reset();
		m_2DRender.reset();
		m_2DRender = nullptr;

	}  // end if

	//
	// delete all our views now since they are W3D views and we need to
	// free them BEFORE we shutdown W3D
	//
	Display::deleteViews();

	REF_PTR_RELEASE( m_3DScene );
	REF_PTR_RELEASE( m_2DScene );
	REF_PTR_RELEASE( m_3DInterfaceScene );
	for (Int j=0; j<LightEnvironmentClass::MAX_LIGHTS; j++)
		REF_PTR_RELEASE( m_myLight[j] );

	PredictiveLODOptimizerClass::Free();

	// Quitting during a camera shake would leave entries in the global CameraShakerSystem, including
	// list nodes that WWMath::Shutdown frees; clear them first.
	CameraShakerSystem.Remove_All();

	// shutdown
	Debug_Statistics::Shutdown_Statistics();
	W3DShaderManager::shutdown();
	m_assetManager->Free_Assets();
	delete m_assetManager;
	WW3D::Shutdown();
	WWMath::Shutdown();
	delete TheW3DFileSystem;
	::Engine::context().w3DFileSystem = nullptr;

}  // end ~W3DDisplay

constexpr auto MIN_DISPLAY_RESOLUTION_X = 800;
constexpr auto MIN_DISPLAY_RESOLUTOIN_Y = 600;


// Accept all modes from 800x600 with an aspect ratio between 5:4 and 32:9; the 2D layout is
// centered (Display::getUILayoutTransform) and the camera widens the field of view (Hor+).
static Bool isSupportedDisplayMode( const ResolutionDescClass &mode )
{
	if( mode.BitDepth < 24 || mode.Width < MIN_DISPLAY_RESOLUTION_X || mode.Height < MIN_DISPLAY_RESOLUTOIN_Y )
		return FALSE;

	const Real aspect = static_cast<Real>(mode.Width) / static_cast<Real>(mode.Height);
	return aspect >= 1.24f && aspect <= 3.6f;
}


/*Return number of screen modes supported by the current device*/
Bool W3DDisplay::offersRendererChoice( void )
{
	return RenderDevice::Get_Backend_Choices() > 1 ? TRUE : FALSE;
}

Int W3DDisplay::getDisplayModeCount(void)
{
	const RenderDeviceDescClass &devDesc=WW3D::Get_Render_Device_Desc(0);
	const DynamicVectorClass <ResolutionDescClass> &resolutions=devDesc.Enumerate_Resolutions();

	Int numResolutions=0;
/*	Bool needStencil=false;
	Bool needDestinationAlpha=false;
	Int minBitDepth=16;
	
	//Walk through all resolutions and determine which ones are compatible with other settings
	//chosen by user.  For example, 32-bit may be required for shadows, occlusion, soft water edge, etc.
	if (TheGlobalData->m_useShadowVolumes || (TheGlobalData->m_enableBehindBuildingMarkers && TheGameLogic->getShowBehindBuildingMarkers()))
		needStencil=true;

	if (TheGlobalData->m_showSoftWaterEdge)
	{	minBitDepth=32;
	}
*/
	for (int res = 0; res < resolutions.Count ();  res ++)
	{
		// Is this the resolution we are looking for?
		if (isSupportedDisplayMode(resolutions[res]))
		{	
			numResolutions++;
		}
	}

	return numResolutions;
}

void W3DDisplay::getDisplayModeDescription(Int modeIndex, Int *xres, Int *yres, Int *bitDepth)
{
	Int numResolutions=0;
	const RenderDeviceDescClass &devDesc=WW3D::Get_Render_Device_Desc(0);
	const DynamicVectorClass <ResolutionDescClass> &resolutions=devDesc.Enumerate_Resolutions();

	for (int res = 0; res < resolutions.Count ();  res ++)
	{
		// Is this the resolution we are looking for?
		if (isSupportedDisplayMode(resolutions[res]))
		{	
			if (numResolutions == modeIndex)
			{	//found the mode
				*xres=resolutions[res].Width;
				*yres=resolutions[res].Height;
				*bitDepth=resolutions[res].BitDepth;
				return;
			}
			numResolutions++;
		}
	}
}

void W3DDisplay::setGamma(Real gamma, Real bright, Real contrast, Bool calibrate)
{
	// The gamma curve is applied in a post-processing pass on the game image
	// (RenderCore::Fx_Apply_Gamma), with no side effect on the desktop.
	RenderDevice::Set_Gamma(gamma,bright,contrast,calibrate, false);
}


/** Set resolution of display */
//=============================================================================
Bool W3DDisplay::setDisplayMode( UnsignedInt xres, UnsignedInt yres, UnsignedInt bitdepth, Bool windowed )
{
	if (WW3D_ERROR_OK == WW3D::Set_Device_Resolution(xres,yres,bitdepth,windowed,true))
	{
		Render2DClass::Set_Screen_Resolution(RectClass(static_cast<float>(0), static_cast<float>(0), static_cast<float>(xres), static_cast<float>(yres)));
		Display::setDisplayMode(xres, yres, bitdepth, windowed);
		return TRUE;
	}

	//set back to the original mode.
	WW3D::Set_Device_Resolution(getWidth(),getHeight(),getBitDepth(),getWindowed(), true);
	Render2DClass::Set_Screen_Resolution(RectClass(static_cast<float>(0), static_cast<float>(0), static_cast<float>(getWidth()),static_cast<float>(getHeight())));
	Display::setDisplayMode(getWidth(),getHeight(),getBitDepth(), getWindowed());
	return FALSE;	//did not change to a new mode.
}

/** Set width of display */
//=============================================================================
void W3DDisplay::setWidth( UnsignedInt width )
{

	// extending functionality
	Display::setWidth( width );

	// our 2D renderer will use mapping coords to make (0,0) the upper left
	// of the screen with (width,height) at the lower right
	m_2DRender->Set_Coordinate_Range( RectClass( static_cast<float>(0), static_cast<float>(0), static_cast<float>(getWidth()), static_cast<float>(getHeight()) ) );

}  // end set width

// W3DDisplay::setHeight ======================================================
/** Set height of display */
//=============================================================================
void W3DDisplay::setHeight( UnsignedInt height )
{

	// extending functionality
	Display::setHeight( height );

	// our 2D renderer will use mapping coords to make (0,0) the upper left
	// of the screen with (width,height) at the lower right
	m_2DRender->Set_Coordinate_Range( RectClass( static_cast<float>(0), static_cast<float>(0), static_cast<float>(getWidth()), static_cast<float>(getHeight()) ) );

}  // end set height

// W3DDisplay::initAssets =====================================================
/** */
//=============================================================================
void W3DDisplay::initAssets( void )
{

}  // end initAssets

// W3DDisplay::init3DScene ====================================================
/** */
//=============================================================================
void W3DDisplay::init3DScene( void )
{

}  // end init3DScene

// W3DDisplay::init2DScene ====================================================
/** This is the 2D scene, you can use it to draw on a 2D plane over the
	* 3D background */
//=============================================================================
void W3DDisplay::init2DScene( void )
{

}  // end init2DScene

/// Texture package: mip levels of a texture above the size of the chosen tier (TextureLoader hook).
static unsigned packageMipOffset(const char *name, unsigned width, unsigned height, unsigned mipCount)
{
	return ::Engine::TexturePackage::instance().mipOffset(name, width, height, mipCount);
}

// W3DDisplay::init ===========================================================
/** Initialize or re-initialize the W3D display system.  Here we need to
  * create our window, and get our 3D hardware setup and online */
//=============================================================================
// While the main thread waits for the simulation thread, the latter may use the device: the texture
// loader only loads right away on the thread registered here.
static void setRenderOwner( unsigned threadId )
{
	RenderDevice::_Set_Main_Thread_ID( threadId );
}

void W3DDisplay::init( void )
{
	::Engine::SimThread::setRenderOwnerHook( setRenderOwner );

	//
	// call our base class init, this method should be able to handle re-entry
	// with its own logic
	//
	Display::init();

	// handle re-entry for ourselves
	if( m_initialized )
	{

		/// @todo W3DDisplay needs RE-init logic!
		return;

	}  // end if
	// Override the W3D File system
	::Engine::context().w3DFileSystem = NEW W3DFileSystem;

	// init the Westwood math library
	WWMath::Init();

	// create our 3D interface scene
	m_3DInterfaceScene = NEW_REF( RTS3DInterfaceScene, () );
	m_3DInterfaceScene->Set_Ambient_Light( Vector3( 1, 1, 1 ) );

	// create our 2D scene
	m_2DScene = NEW_REF( RTS2DScene, () );
	m_2DScene->Set_Ambient_Light( Vector3( 1, 1, 1 ) );

	// create our 3D scene
	m_3DScene =NEW_REF( RTS3DScene, () );
#if defined(_DEBUG)
	if( TheGlobalData->m_wireframe )
		m_3DScene->Set_Polygon_Mode( SceneClass::LINE );
#endif
//============================================================================
	// m_myLight = NEW_REF
//============================================================================
	Int lindex;
	for (lindex=0; lindex<TheGlobalData->m_numGlobalLights; lindex++) 
	{	m_myLight[lindex] = NEW_REF( LightClass, (LightClass::DIRECTIONAL) );
	}

	setTimeOfDay( TheGlobalData->m_timeOfDay );	//set each light to correct values for given time

	for (lindex=0; lindex<TheGlobalData->m_numGlobalLights; lindex++) 
	{	m_3DScene->setGlobalLight( m_myLight[lindex], lindex );
	}

#ifdef SAMPLE_DYNAMIC_LIGHT
	theDynamicLight = NEW_REF(W3DDynamicLight, ());
	Real red = 1;
	Real green = 1;
	Real blue = 0;
	if(red==0 && blue==0 && green==0) {
		red = green = blue = 1;
	}
	theDynamicLight->Set_Ambient( Vector3( red, green, blue ) );
	theDynamicLight->Set_Diffuse( Vector3( red, green, blue) );
	theDynamicLight->Set_Position(Vector3(0, 0, 4));
	theDynamicLight->Set_Far_Attenuation_Range(1, 8);
	// Note: Don't Add_Render_Object dynamic lights. 
	m_3DScene->addDynamicLight( theDynamicLight );
#endif

	// create a new asset manager
	m_assetManager = NEW W3DAssetManager;	
	m_assetManager->Register_Prototype_Loader(&_ParticleEmitterLoader );
	m_assetManager->Register_Prototype_Loader(&_AggregateLoader);
	m_assetManager->Set_WW3D_Load_On_Demand( true );


	if (TheGlobalData->m_incrementalAGPBuf)
	{
		SortingRendererClass::SetMinVertexBufferSize(1);
	}
	// Direct3D 9 is gone. A request for it (-d3d9) is logged and shown once per process,
	// except in automated runs (-runner, -quitAfter), which would block on the message box.
	{
		if( TheGlobalData->m_renderBackendD3D9Requested )
		{
			const char *source = "-d3d9";
			char line[160];
			WWFormat::Into(line, "RENDERBACKEND d3d9 no longer supported ({}), starting Direct3D 11", source);
			line[sizeof(line) - 1] = 0;
			GameEngine_RunnerLog( line );
			DEBUG_LOG(( "W3DDisplay: %s\n", line ));
			static Bool s_d3d9Reported = FALSE;
			if( !s_d3d9Reported && !TheGlobalData->m_runnerMode && TheGlobalData->m_quitAfterFrames == 0 )
			{
				s_d3d9Reported = TRUE;
				::MessageBoxA( ApplicationHWnd,
					"Direct3D 9 is no longer supported.\n\n"
					"The game starts with Direct3D 11. The switch -d3d9 has no effect and can be removed.",
					"Direct3D 9 no longer supported", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND );
			}
		}
	}

	// Render effects. They are already resolved in TheGlobalData: GameLODManager::applyStaticLODLevel
	// (runs before this init) combines the detail level, Options.ini "D3D11Effects" and -d3d11Effects.
	{
		static_assert(RENDER_EFFECT_FXAA == (1 << 0) && RENDER_EFFECT_SHARPEN == (1 << 1) &&
					  RENDER_EFFECT_SOFT_PARTICLES == (1 << 2) && RENDER_EFFECT_SSAO == (1 << 3) &&
					  RENDER_EFFECT_COLOR_GRADE == (1 << 4) && RENDER_EFFECT_SHADOW_MAP == (1 << 5) &&
					  RENDER_EFFECT_TERRAIN_DETAIL == (1 << 6) && RENDER_EFFECT_TERRAIN_ROCK == (1 << 7) &&
					  RENDER_EFFECT_BLOOM == (1 << 8) && RENDER_EFFECT_HEAT == (1 << 9) && RENDER_EFFECT_HAZE == (1 << 10) &&
					  RENDER_EFFECT_LIGHTS == (1 << 11) && RENDER_EFFECT_PARTICLE_LIGHT == (1 << 12) &&
					  RENDER_EFFECT_PARTICLE_GROUND == (1 << 13) && RENDER_EFFECT_TERRAIN_NORMAL == (1 << 14) &&
					  RENDER_EFFECT_TERRAIN_TESS == (1 << 15) && RENDER_EFFECT_ROCK_TESS == (1 << 16) &&
					  RENDER_EFFECT_ALPHA_COVERAGE == (1 << 17) && RENDER_EFFECT_SOFT_SHADOWS == (1 << 18) &&
					  RENDER_EFFECT_CONTACT_SHADOWS == (1 << 19) && RENDER_EFFECT_SHADOW_CASCADES == (1 << 20) &&
					  RENDER_EFFECT_MSAA_DEPTH == (1 << 21) && RENDER_EFFECT_HDR == (1 << 22) &&
					  RENDER_EFFECT_DEBAND == (1 << 23) && RENDER_EFFECT_WATER_SSR == (1 << 24) && RENDER_EFFECT_GTAO == (1 << 25) &&
					  RENDER_EFFECT_TAA == (1 << 26) && RENDER_EFFECT_TERRAIN_LIGHT == (1 << 27) &&
					  RENDER_EFFECT_NO_MSAA_DEPTH == (1 << 28) && RENDER_EFFECT_FLAT_SNOW == (1 << 29) &&
					  RENDER_EFFECT_MODEL_LIGHT == (1 << 30) && static_cast<unsigned>(RENDER_EFFECT_ATMOSPHERE) == (1u << 31),
					  "bits as in GlobalData::parseRenderEffects");
		RenderEffectSettings effects;
		effects.flags = TheGlobalData->m_renderEffects;
		effects.sharpen = TheGlobalData->m_renderEffectSharpen;
		effects.shadow_map_size = static_cast<unsigned>(TheGlobalData->m_renderEffectShadowMapSize);
		effects.shadow_terrain_strength = TheGlobalData->m_renderEffectShadowTerrain;
		effects.terrain_detail = TheGlobalData->m_renderEffectTerrainDetail;
		effects.terrain_anisotropy = static_cast<unsigned>(TheGlobalData->m_terrainAnisotropy);
		effects.terrain_lod_bias = TheGlobalData->m_terrainLodBias;
		effects.terrain_rock = TheGlobalData->m_renderEffectTerrainRock;
		effects.bloom = TheGlobalData->m_renderEffectBloom;
		effects.heat = TheGlobalData->m_renderEffectHeat;
		effects.haze = TheGlobalData->m_renderEffectHaze;
		effects.lights = TheGlobalData->m_renderEffectLights;
		effects.particle_light = TheGlobalData->m_renderEffectParticleLight;
		effects.terrain_normal = TheGlobalData->m_renderEffectTerrainNormal;
		effects.terrain_tess = TheGlobalData->m_renderEffectTerrainTess;
		effects.terrain_tess_max = TheGlobalData->m_terrainTessMax;
		effects.terrain_tess_pixels = TheGlobalData->m_terrainTessPixels;
		effects.terrain_tess_probe = TheGlobalData->m_terrainTessProbe ? 1u : 0u;
		effects.rock_tess = TheGlobalData->m_renderEffectRockTess;
		effects.flat_snow = TheGlobalData->m_renderEffectFlatSnow;
		effects.height_fog = TheGlobalData->m_renderEffectHeightFog;
		effects.sun_shafts = TheGlobalData->m_renderEffectSunShafts;
		effects.shadow_soft = TheGlobalData->m_renderEffectShadowSoft;
		effects.render_scale = TheGlobalData->m_renderScale;
		effects.render_scale_filter = static_cast<unsigned>(TheGlobalData->m_renderScaleFilter);
		effects.render_scale_sharpen = TheGlobalData->m_renderScaleSharpen;
		effects.scene_format = static_cast<unsigned>(TheGlobalData->m_sceneFormat);
		effects.hdr_knee = TheGlobalData->m_hdrKnee;
		effects.hdr_additive = TheGlobalData->m_hdrAdditive;
		effects.gpu_timing = TheGlobalData->m_gpuTiming ? 1u : 0u;
		RenderDevice::Set_Effect_Settings( effects );
		if( effects.scene_format == SCENE_FORMAT_RGBA16F )
			GameEngine_RunnerLog( "SZENENFORMAT rgba16f" );
		else if( effects.scene_format == SCENE_FORMAT_RGBA16 )
			GameEngine_RunnerLog( "SZENENFORMAT rgba16" );
		else if( effects.scene_format == SCENE_FORMAT_R11G11B10 )
			GameEngine_RunnerLog( "SZENENFORMAT r11g11b10" );
		if( effects.render_scale < 0.999f || effects.render_scale > 1.001f || effects.gpu_timing )
		{
			char line[160];
			WWFormat::Into(line, "RENDERSKALIERUNG anteil {:.3f} filter {:d} schaerfe {:.2f} zeitmessung {:d}", effects.render_scale,
				effects.render_scale_filter, effects.render_scale_sharpen, effects.gpu_timing);
			line[sizeof(line) - 1] = 0;
			GameEngine_RunnerLog( line );
			DEBUG_LOG(( "%s\n", line ));
		}
		SkinFVFCategoryContainer::Enable_GPU_Skinning( !TheGlobalData->m_cpuSkinning );
		RenderDevice::Enable_Constant_Buffer_Ring( !TheGlobalData->m_noConstantBufferRing );
		PointGroupClass::Enable_GPU_Particles( !TheGlobalData->m_cpuParticles );
		RenderDevice::Enable_Mesh_Instancing( !TheGlobalData->m_noMeshInstancing );
		if( effects.flags != 0 || TheGlobalData->m_renderEffectMsaa >= 0 || TheGlobalData->m_msaaSamples >= 0 )
		{
			char line[320];
			// "backend" is always 1 (Direct3D 11); the field stays for the log evaluation tools.
			WWFormat::Into(line, "D3D11EFFECTS flags 0x{:X} sharpen {:.2f} msaa {:d} backend {:d} shadowmap {:d} gelaende {:.2f} detailboden {:.2f} berge {:.2f} relief {:.2f} tessellation {:.2f} faktor {:.0f} pixel {:.0f} probe {:d} felstess {:.2f}", effects.flags, effects.sharpen, (TheGlobalData->m_msaaSamples >= 0) ? TheGlobalData->m_msaaSamples : TheGlobalData->m_renderEffectMsaa, 1, effects.shadow_map_size, effects.shadow_terrain_strength, effects.terrain_detail, effects.terrain_rock, effects.terrain_normal, effects.terrain_tess, effects.terrain_tess_max, effects.terrain_tess_pixels, effects.terrain_tess_probe, effects.rock_tess);
			line[sizeof(line) - 1] = 0;
			GameEngine_RunnerLog( line );
			DEBUG_LOG(( "D3D11 effects: %s\n", line ));
		}
	}

	// Settings.ini or -vulkan asks for the Vulkan backend, otherwise the first usable device layer starts. If
	// Vulkan cannot be used, RenderDevice starts the next layer (at Init or when the device fails on the window);
	// the reason is reported once the device exists.
	const Bool vulkanRequested = UserSettings::useVulkanRenderer();
	RenderDevice::Set_Backend_Request( vulkanRequested ? "vulkan" : nullptr );
	RenderDevice::Set_Float_Depth( TheGlobalData->m_depthFloat != FALSE );
	RenderDevice::Set_Upload_Diagnosis( TheGlobalData->m_logUploads, TheGlobalData->m_fillUnwritten );
	if (WW3D::Init( ApplicationHWnd ) != WW3D_ERROR_OK)
		throw ERROR_INVALID_D3D;	//failed to initialize Direct3D 11.

	WW3D::Set_Prelit_Mode( WW3D::PRELIT_MODE_LIGHTMAP_MULTI_PASS );
	WW3D::Set_Collision_Box_Display_Mask(0x00);	///<set to 0xff to make collision boxes visible
	WW3D::Enable_Static_Sort_Lists(true);
	WW3D::Set_Screen_UV_Bias( TRUE );  ///< this makes text look good :)
			
	// VSync and multisampling from Options.ini; set before device creation, RenderDevice takes
	// both into the present parameters.
	{
		WW3D::Set_Ext_Swap_Interval( (UserSettings::getVSyncEnabled() && !TheGlobalData->m_noVSync) ? 1 : 0 );
		RenderDevice::Set_Multisample_Samples( UserSettings::getAntiAliasing() );
		// "-d3d11Effects msaa=n" for comparison runs, independent of Options.ini.
		if( TheGlobalData->m_renderEffectMsaa >= 0 )
			RenderDevice::Set_Multisample_Samples( TheGlobalData->m_renderEffectMsaa );
		// "-msaa n" (test runs) over both.
		if( TheGlobalData->m_msaaSamples >= 0 )
			RenderDevice::Set_Multisample_Samples( TheGlobalData->m_msaaSamples );
	}

	// The monitor to open on: -bildschirm n, else the one of the last run. The window was created on the
	// primary monitor, before the settings were read.
	PlaceWindowOnMonitor( ApplicationHWnd, TheGlobalData->m_monitorIndex, UserSettings::get( "Window.Monitor" ),
		!TheGlobalData->m_windowed || TheGlobalData->m_borderless );

	// A borderless window (-borderless) runs windowed at the resolution of its monitor, overriding the
	// resolution stored in Options.ini.
	if( TheGlobalData->m_borderless )
	{
		TheWritableGlobalData->m_windowed = TRUE;
		Int width = 0;
		Int height = 0;
		if( !WindowMonitorSize( ApplicationHWnd, width, height ) )
		{
			width = GetSystemMetrics( SM_CXSCREEN );
			height = GetSystemMetrics( SM_CYSCREEN );
		}
		TheWritableGlobalData->m_xResolution = width;
		TheWritableGlobalData->m_yResolution = height;
	}

	setWindowed( TheGlobalData->m_windowed );
	RenderDevice::Set_Fullscreen_Area( TheGlobalData->m_fullscreenAreaWidth, TheGlobalData->m_fullscreenAreaHeight );

	// create a 2D renderer helper
	m_2DRender = std::make_unique<Render2DClass>();
	// Without the half pixel offset, 1:1 drawn UI images sample exactly at texel centers; with it,
	// tiled button graphics get dark seams at every tile edge. Text goes through
	// Render2DSentenceClass and keeps the offset.
	m_2DRender->Enable_Screen_UV_Bias( false );
	// From texture tier HD on, load UI textures with mips
	Render2DClass::Set_Mipmapped_Textures( TheGlobalData->m_textureTier > 0 );
	DEBUG_ASSERTCRASH( m_2DRender, ("Cannot create Render2DClass") );

	// set our default width and height and bit depth
	/// @todo we should set this according to options read from a file
	setWidth( TheGlobalData->m_xResolution );
	setHeight( TheGlobalData->m_yResolution );
	setBitDepth( W3D_DISPLAY_DEFAULT_BIT_DEPTH );

	if( WW3D::Set_Render_Device( 0, 
															 getWidth(), 
															 getHeight(), 
															 getBitDepth(), 
															 getWindowed(), 
															 true ) != WW3D_ERROR_OK ) 
	{
		// Getting the device at the default bit depth (32) didn't work, so try
		// getting a 16 bit display.  (Voodoo 1-3 only supported 16 bit.) jba.
		setBitDepth( 16 );
		if( WW3D::Set_Render_Device( 0, 
																 getWidth(), 
																 getHeight(), 
																 getBitDepth(), 
																 getWindowed(), 
																 true ) != WW3D_ERROR_OK ) 
		{

			WW3D::Shutdown();
			WWMath::Shutdown();
			throw ERROR_INVALID_D3D;	//failed to initialize Direct3D 11.
		}

	}  // end if

	// Which renderer runs: logged for runners, shown once per process when Vulkan was asked for and not
	// started (not in automated runs, like the D3D9 notice). A choice from Settings.ini goes back to
	// the default, so the notice does not come at every start.
	if( vulkanRequested )
	{
		const char *reason = RenderDevice::Get_Backend_Fallback_Reason();
		const char *started = RenderDevice::Get_Backend_Title();
		const Bool vulkanRuns = RenderDevice::Is_Backend( "vulkan" );
		const Bool fromSettings = !UserSettings::isForced( "Display.RenderBackend" );
		if( !vulkanRuns && fromSettings )
		{
			UserSettings::set( "Display.RenderBackend", "d3d11" );
			GameEngine_RunnerLog( "RENDERBACKEND setting reset to d3d11" );
			DEBUG_LOG(( "W3DDisplay: RENDERBACKEND setting reset to d3d11\n" ));
		}
		char line[640];
		if( vulkanRuns )
			WWFormat::Into(line, "RENDERBACKEND vulkan");
		else
			WWFormat::Into(line, "RENDERBACKEND vulkan not used ({}), starting {}", reason, started);
		line[sizeof(line) - 1] = 0;
		GameEngine_RunnerLog( line );
		DEBUG_LOG(( "W3DDisplay: %s\n", line ));
		static Bool s_vulkanReported = FALSE;
		if( !vulkanRuns && !s_vulkanReported
			&& !TheGlobalData->m_runnerMode && TheGlobalData->m_quitAfterFrames == 0 && TheGlobalData->m_maxFrames == 0 )
		{
			s_vulkanReported = TRUE;
			char text[768];
			char settingNote[160] = "";
			if( fromSettings )
				WWFormat::Into(settingNote, " The renderer setting in the options menu is set back to {}.", started);
			WWFormat::Into(text, "The Vulkan renderer could not be started:\n\n{}\n\nThe game starts with {}.{}", reason, started,
				settingNote);
			text[sizeof(text) - 1] = 0;
			::MessageBoxA( ApplicationHWnd, text, "Vulkan not available", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND );
		}
	}

	// Texture package: the tier chosen at startup decides how many top mip levels each texture of the
	// package skips (ArchiveFileSystem::loadTexturePackage).
	if (::Engine::TexturePackage::instance().active())
		TextureLoader::Set_Package_Mip_Offset_Function(packageMipOffset);

	//Check if level was never set and default to setting most suitable for system.
	if (TheGameLODManager->getStaticLODLevel() == STATIC_GAME_LOD_UNKNOWN)
		TheGameLODManager->setStaticLODLevel(TheGameLODManager->findStaticLODLevel());
	else
	{	//Static LOD level was applied during GameLOD manager init except for texture reduction
		//which needs to be applied here.
		Int txtReduction=TheWritableGlobalData->m_textureReductionFactor;
		if (txtReduction > 0)
		{		WW3D::Set_Texture_Reduction(txtReduction,32);
				//Tell LOD manager that texture reduction was applied.
				TheGameLODManager->setCurrentTextureReduction(txtReduction);
		}
	}

	// Request anisotropic filtering with a linear mip filter explicitly; without it WW3D uses
	// bilinear with point mip filtering. Must follow Set_Render_Device, because _Init_Filters sets
	// sampler states on the device.
	WW3D::Set_Texture_Filter(TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC);

	if (TheGlobalData->m_displayGamma != 1.0f)
		setGamma(TheGlobalData->m_displayGamma,0.0f,1.0f,FALSE);

	initAssets();
	init2DScene();
	init3DScene();
	W3DShaderManager::init();

	// Create and initialize the debug display
	m_nativeDebugDisplay = NEW W3DDebugDisplay();
	m_debugDisplay = m_nativeDebugDisplay;
	if ( m_nativeDebugDisplay )
	{
		m_nativeDebugDisplay->init();
		GameFont *font;

		if (TheGlobalLanguageData && TheGlobalLanguageData->m_nativeDebugDisplay.name.isNotEmpty())
		{
			font=TheFontLibrary->getFont(
				TheGlobalLanguageData->m_nativeDebugDisplay.name,
				TheGlobalLanguageData->m_nativeDebugDisplay.size,
				TheGlobalLanguageData->m_nativeDebugDisplay.bold);
		}
		else
			font=TheFontLibrary->getFont( AsciiString("FixedSys"), 8, FALSE );

		m_nativeDebugDisplay->setFont( font );
		m_nativeDebugDisplay->setFontHeight( 13 );
		m_nativeDebugDisplay->setFontWidth( 9 );
	}

	// we're now online
	m_initialized = true;
	if( TheGlobalData->m_displayDebug )
	{
		m_debugDisplayCallback = StatDebugDisplay;
	}

	// -modellPruefung: load every model and DDS once, write the result and quit
	if (!TheGlobalData->m_modelCheckFile.isEmpty() && m_assetManager != nullptr)
	{
		[[maybe_unused]] const int rejected = m_assetManager->Check_Model_Files(TheGlobalData->m_modelCheckFile.str());
		DEBUG_LOG(("model check: %d parts rejected, see %s\n", rejected, TheGlobalData->m_modelCheckFile.str()));
		TheGameEngine->setQuitting(TRUE);
	}
}  // end init

// W3DDisplay::reset ===========================================================
/** Reset the W3D display system.  Here we need to
  * remove the objects from the previous map. */
//=============================================================================
void W3DDisplay::reset( void )
{

	Display::reset();

	// Remove all render objects.

	SceneIterator *sceneIter = m_3DScene->Create_Iterator();
	sceneIter->First();
	while(!sceneIter->Is_Done()) {
		RenderObjClass * robj = sceneIter->Current_Item();
		robj->Add_Ref();
		m_3DScene->Remove_Render_Object(robj);
		robj->Release_Ref();
		sceneIter->Next();
	}
	m_3DScene->Destroy_Iterator(sceneIter);

	m_isClippedEnabled = FALSE;

	// release any unused assets from W3D
	/// @todo really need that "scene abstraction", having this stuff in the display is icky
	m_assetManager->Release_Unused_Assets();

	if (TheWritableGlobalData)
		TheWritableGlobalData->m_drawSkyBox =0;
}

/** Update a moving average of the last 30 fps measurements.  Also try to filter out temporary spikes.
	This code is designed to be used by the GameLOD sytems to determine the correct dynamic LOD setting.
*/
void W3DDisplay::updateAverageFPS(void)
{
	const Real MaximumFrameTimeCutoff = 0.5f;	//largest frame interval (seconds) we accept before ignoring it as a momentary "spike"
	const Int FPS_HISTORY_SIZE = 30;	//keep track of the last 30 frames

	static Int64 lastUpdateTime64 = 0;
	static Int historyOffset = 0;
	static Int numSamples = 0;
	static double fpsHistory[FPS_HISTORY_SIZE];

	Int64 freq64 = getPerformanceCounterFrequency();
	Int64 time64 = getPerformanceCounter();

#if defined(_DEBUG)
	if (TheGameLogic->getFrame() == START_CUMU_FRAME)
	{
		m_timerAtCumuFPSStart = time64;
	}
#endif

	Int64 timeDiff = time64 - lastUpdateTime64;

	// convert elapsed time to seconds
	double elapsedSeconds = static_cast<double>(timeDiff)/static_cast<double>(freq64);

	if (elapsedSeconds <= MaximumFrameTimeCutoff)	//make sure it's not a spike
	{
		// append new sameple to fps history.
		if (historyOffset >= FPS_HISTORY_SIZE)
			historyOffset = 0;

		double currentFPS = 1.0/elapsedSeconds; 
		fpsHistory[historyOffset++] = currentFPS;
		numSamples++;
		if (numSamples > FPS_HISTORY_SIZE)
			numSamples = FPS_HISTORY_SIZE;
	}

	if (numSamples)
	{	
		// determine average frame rate over our past history.
		Real average=0;
		for (Int i=0,j=historyOffset-1; i<numSamples; i++,j--)
		{
			if (j < 0)
				j=FPS_HISTORY_SIZE-1;	// wrap around to front of buffer
			average = static_cast<Real>(average + fpsHistory[j]);
		}

		m_averageFPS = average / static_cast<Real>(numSamples);
	}

	lastUpdateTime64 = time64;
}

#if defined(_DEBUG)	//debug hack to view object under mouse stats
ICoord2D TheMousePos;
#endif


// W3DDisplay::calculateTerrainLOD =================================================
/** Calculates an adequately speedy terrain Level Of Detail. */
//=============================================================================
// The debug build returns early, which leaves the sampling loop unreachable.
#pragma warning(push)
#pragma warning(disable : 4702)
void W3DDisplay::calculateTerrainLOD( void )
{
	const Int NUM_SAMPLES=20;
	const Int NUM_TO_DISCARD=5;
	
	Int64 freq64 = getPerformanceCounterFrequency();

	char buf[_MAX_PATH];
	float frameTime = 0;
	float maxTimeLimit = TheGlobalData->m_terrainLODTargetTimeMS/1000.0f;
	TerrainLOD goodLOD = TERRAIN_LOD_MIN;
	TerrainLOD curLOD = TERRAIN_LOD_AUTOMATIC;
	Int count = 0;
#ifdef _DEBUG
	// just go to TERRAIN_LOD_NO_WATER, mirror off.
	TheWritableGlobalData->m_terrainLOD = TERRAIN_LOD_NO_WATER;
	m_3DScene->drawTerrainOnly(false);
	TheTerrainRenderObject->adjustTerrainLOD(0);
	return;
#endif
	do {
		Int i;
		float timeForFrame=0;
		frameTime = 0;
		switch(curLOD) {
			default: curLOD = TERRAIN_LOD_DISABLE; break;
			case TERRAIN_LOD_AUTOMATIC: curLOD = TERRAIN_LOD_MAX; break;
			case TERRAIN_LOD_MAX: curLOD = TERRAIN_LOD_NO_WATER; break;
			case TERRAIN_LOD_HALF_CLOUDS: curLOD = TERRAIN_LOD_DISABLE; break;
			case TERRAIN_LOD_NO_WATER: curLOD = TERRAIN_LOD_HALF_CLOUDS; break;
		}
		if (curLOD == TERRAIN_LOD_DISABLE) {
			break;
		}
		TheWritableGlobalData->m_terrainLOD = curLOD;
		m_3DScene->drawTerrainOnly(true);
		TheTerrainRenderObject->adjustTerrainLOD(0);
		for (i=0; i<NUM_SAMPLES; i++) {
			Int64 startTime64 = getPerformanceCounter();
			// start render block
			updateViews();
			if (WW3D::Begin_Render( true, true, Vector3( 0.0f, 0.0f, 0.0f ) ) == WW3D_ERROR_OK)
			{	// draw all views of the world
				drawViews();
				// render is all done!
				WW3D::End_Render();
			}
			Int64 time64 = getPerformanceCounter();
			timeForFrame = static_cast<float>(static_cast<double>(time64-startTime64) / static_cast<double>(freq64));
			WWFormat::Into(buf, "{:.2f}ms ", timeForFrame*1000.0f);
			::OutputDebugString(buf);
			if (i>=NUM_TO_DISCARD) {
				frameTime += timeForFrame;
				if (i>NUM_TO_DISCARD+1 && 
					(timeForFrame / ((i+1)-NUM_TO_DISCARD)) > 2*maxTimeLimit) {
					i++;
					break;
				}
			}
		}
		frameTime /= ((i)-NUM_TO_DISCARD);
		count++;
		WWFormat::Into(buf, "\n LOD {:d}, time {:.2f}ms\n", static_cast<int>(curLOD), frameTime*1000.0f);
		::OutputDebugString(buf);
		if (frameTime<maxTimeLimit && goodLOD<curLOD) {
			goodLOD = curLOD;
		}
		if (frameTime < maxTimeLimit) break;
	} while (count<10);

	TheWritableGlobalData->m_terrainLOD = goodLOD;
	m_3DScene->drawTerrainOnly(false);
	TheTerrainRenderObject->adjustTerrainLOD(0);
#ifdef _DEBUG
	DEBUG_ASSERTCRASH(count<10, ("calculateTerrainLOD") );
#endif

}
#pragma warning(pop)


Real W3DDisplay::getAverageFPS()
{
	return m_averageFPS;
}

// When running under RenderDoc, -shotAfter also triggers a frame capture (next frame), so image
// differences can be inspected at exactly that frame. Without RenderDoc nothing happens. The struct
// is the head of RENDERDOC_API_1_0_0 from renderdoc_app.h (only up to TriggerCapture).
static void triggerRenderDocCapture( void )
{
	struct RenderDocApiHead
	{
		void *fn[11];
		void (__cdecl *SetCaptureFilePathTemplate)(const char *pathtemplate);
		void *fn2[3];
		void (__cdecl *TriggerCapture)(void);
	};
	static RenderDocApiHead *s_api = nullptr;
	static Bool s_checked = FALSE;
	if (!s_checked)
	{
		s_checked = TRUE;
		HMODULE mod = ::GetModuleHandle("renderdoc.dll");
		if (mod)
		{
			using GetApiFn = int (__cdecl *)(int version, void **api);
			GetApiFn getApi = reinterpret_cast<GetApiFn>(::GetProcAddress(mod, "RENDERDOC_GetAPI"));
			void *api = nullptr;
			if (getApi && getApi(10000, &api) == 1)			// eRENDERDOC_API_Version_1_0_0
				s_api = static_cast<RenderDocApiHead *>(api);
			if (s_api && TheGlobalData->m_shotDir.isNotEmpty())
			{
				AsciiString capturePath;
				capturePath.format("%s\\capture", TheGlobalData->m_shotDir.str());
				s_api->SetCaptureFilePathTemplate(capturePath.str());
			}
		}
	}
	if (s_api)
		s_api->TriggerCapture();
}

// -shotAfter N requests a back buffer screenshot in the tick draw of client frame N; the wrapper
// captures it right before Present (End_Scene).
static void takeTestShot( UnsignedInt frame, UnsignedInt actualFrame, const char *prefix = "shot" )
{
	AsciiString path;
	if (TheGlobalData->m_shotDir.isNotEmpty())
	{
		::CreateDirectory(TheGlobalData->m_shotDir.str(), nullptr);
		path.format("%s\\%s_%06u.bmp", TheGlobalData->m_shotDir.str(), prefix, frame);
	}
	else
	{
		path.format("%s_%06u.bmp", prefix, frame);
	}
	RenderDevice::Request_Back_Buffer_Capture(path.str());
	triggerRenderDocCapture();
	char line[MAX_PATH + 64];
	WWFormat::Into(line, "SHOT frame {:d} actual {:d} clock {:d} {}", frame, actualFrame, GameEngine_RenderTimeMs(), path.str());
	line[sizeof(line) - 1] = 0;
	GameEngine_RunnerLog(line);
}

static void requestTestShot( void )
{
	// The client frame can jump by more than one between images (several logic ticks per image), so
	// each entry fires exactly once when its frame is reached or passed; the file name keeps the
	// requested frame.
	static Bool s_taken[GlobalData::MAX_SHOT_FRAMES] = { FALSE };
	const UnsignedInt actualFrame = TheGameClient->getFrame();

	// -shotEveryDraw N: drawn frames of the whole process, so menus between games are covered too.
	if (TheGlobalData->m_shotEveryDraw > 0)
	{
		static UnsignedInt s_drawn = 0;
		if (++s_drawn % TheGlobalData->m_shotEveryDraw == 0)
			takeTestShot(s_drawn, actualFrame, "draw");
	}
	for (Int i = 0; i < TheGlobalData->m_shotAfterCount; ++i)
	{
		const UnsignedInt frame = TheGlobalData->m_shotAfterFrames[i];
		if (s_taken[i] || actualFrame < frame)
			continue;
		s_taken[i] = TRUE;
		takeTestShot(frame, actualFrame);
		return;
	}

	// -shotEvery N: in game, one image whenever the logic frame reaches a new multiple of N; file name
	// and log carry the logic frame (time base of the camera path). With -cameraPath only while the
	// path runs (first to last waypoint).
	if (TheGlobalData->m_shotEvery > 0 && TheGameLogic->isInGame())
	{
		static UnsignedInt s_lastEvery = 0;
		const UnsignedInt logicFrame = TheGameLogic->getFrame();
		if (TheGlobalData->m_cameraPathFile.isNotEmpty())
		{
			UnsignedInt first = 0, last = 0;
			if (!CameraPath_GetFrameRange(&first, &last) || logicFrame < first || logicFrame > last)
				return;
		}
		const UnsignedInt index = logicFrame / TheGlobalData->m_shotEvery;
		if (index > s_lastEvery)
		{
			s_lastEvery = index;
			takeTestShot(index * TheGlobalData->m_shotEvery, logicFrame);
		}
	}
}

// -logTextureStats N: every N client frames, write the loaded textures to the runner log: count, video
// memory including mips (MB), DXT share, level 0 texels, largest edge, textures without mips, not yet
// loaded ones, and the free video memory reported by the backend.
// -logCamera: per drawn image in a game the camera's clearance above the logic terrain. "clearance" is the eye
// height above the ground under the eye, "nearplane" the smallest height of a 5 x 5 grid on the near plane above
// the ground there; negative puts the camera or its near plane into the terrain (camera paths set zoom and pitch
// without the player's height limit). The drawn terrain may lie above the logic height by the tessellation
// displacement.
static void logCameraClearance( W3DView *view )
{
	CameraClass *camera = view ? view->get3DCamera() : nullptr;
	if (camera == nullptr || TheTerrainLogic == nullptr)
		return;
	const Matrix3D &tm = camera->Get_Transform();
	float zNear = 0.0f, zFar = 0.0f;
	camera->Get_Clip_Planes( zNear, zFar );
	Vector2 vMin, vMax;
	camera->Get_View_Plane( vMin, vMax );
	const Vector3 eye = tm.Get_Translation();
	if (!(fabs( eye.X ) + fabs( eye.Y ) + fabs( eye.Z ) < 1.0e30f))
		return;	// camera not set up yet (first frame of a game)
	const Real ground = TheTerrainLogic->getGroundHeight( eye.X, eye.Y );
	Real nearClear = FLT_MAX;
	for (Int j = 0; j < 5; ++j)
	{
		for (Int i = 0; i < 5; ++i)
		{
			const Real u = vMin.X + (vMax.X - vMin.X) * i * 0.25f;
			const Real v = vMin.Y + (vMax.Y - vMin.Y) * j * 0.25f;
			Vector3 p;
			Matrix3D::Transform_Vector( tm, Vector3( u * zNear, v * zNear, -zNear ), &p );
			nearClear = min( nearClear, p.Z - TheTerrainLogic->getGroundHeight( p.X, p.Y ) );
		}
	}
	char line[256];
	snprintf( line, sizeof(line), "CAMERA %u eye %.1f %.1f %.1f ground %.1f clearance %.2f nearplane %.2f near %.1f zoom %.3f pitch %.1f",
		TheGameLogic->getFrame(), eye.X, eye.Y, eye.Z, ground, eye.Z - ground, nearClear, zNear, view->getZoom(),
		view->getPitch() * 180.0f / PI );
	GameEngine_RunnerLog( line );
}

static void logTextureStatistics( void )
{
	static UnsignedInt s_next = 0;
	const UnsignedInt frame = TheGameClient->getFrame();
	if (frame < s_next)
		return;
	s_next = frame + TheGlobalData->m_logTextureStats;
	WW3DAssetManager::TextureStatisticsStruct stats;
	WW3DAssetManager::Get_Texture_Statistics(stats);
	const double mb = 1024.0 * 1024.0;
	char line[320];
	WWFormat::Into(line, "TEXSTATS frame {:d} logic {:d} count {:d} mb {:.1f} dxt {:d} dxtmb {:.1f} mtexel {:.1f} maxedge {:d} nomip {:d} pending {:d} freemb {:.0f}", frame, TheGameLogic ? TheGameLogic->getFrame() : 0, stats.Count, stats.Bytes / mb, stats.CompressedCount, stats.CompressedBytes / mb, stats.Texels / 1.0e6, stats.MaxEdge, stats.NoMipCount, stats.Uninitialized, RenderDevice::Get_Free_Texture_RAM() / mb);
	line[sizeof(line) - 1] = 0;
	GameEngine_RunnerLog(line);
}

// -logDrawStats N: draw calls, triangles and vertices per drawn image, averaged over the images of the last
// N client frames, with zoom and camera height above ground; for comparing zoom levels.
static void logDrawStatistics( void )
{
	static UnsignedInt s_next = 0;
	static UnsignedInt s_images = 0;
	static double s_draws = 0.0, s_polys = 0.0, s_verts = 0.0;
	s_draws += Debug_Statistics::Get_Draw_Calls();
	s_polys += Debug_Statistics::Get_Device_Polygons();
	s_verts += Debug_Statistics::Get_Device_Vertices();
	++s_images;
	const UnsignedInt frame = TheGameClient->getFrame();
	if (frame < s_next)
		return;
	s_next = frame + TheGlobalData->m_logDrawStats;
	const double n = static_cast<double>(s_images);
	char line[256];
	WWFormat::Into(line, "DRAWSTATS frame {:d} logic {:d} bilder {:d} zeichnungen {:.0f} dreiecke {:.0f} ecken {:.0f} zoom {:.3f} hoehe {:.0f}",
		frame, TheGameLogic ? TheGameLogic->getFrame() : 0, s_images, s_draws / n, s_polys / n, s_verts / n,
		TheTacticalView ? TheTacticalView->getZoom() : 0.0f, TheTacticalView ? TheTacticalView->getCurrentHeightAboveGround() : 0.0f);
	line[sizeof(line) - 1] = 0;
	GameEngine_RunnerLog(line);
	s_images = 0;
	s_draws = s_polys = s_verts = 0.0;
}

Int W3DDisplay::getLastFrameDrawCalls()
{
	return Debug_Statistics::Get_Draw_Calls();
}

//DECLARE_PERF_TIMER(BigAssRenderLoop)

// Point lights for RENDER_EFFECT_LIGHTS, derived from what the client already shows: the light pulses of
// the effect lists (explosions) and the additive billboard particle systems (fire, muzzle flashes, sparks).
// Read only; nothing here touches game logic state or random numbers. Lights in fogged or shrouded cells
// are dropped so the glow does not show what the player cannot see.
static void collectDynamicLights( RTS3DScene *scene )
{
	RenderDynamicLight lights[RENDER_MAX_DYNAMIC_LIGHT_CANDIDATES];
	Real scores[RENDER_MAX_DYNAMIC_LIGHT_CANDIDATES];
	Int count = 0;

	Int playerIndex = -1;
	if( TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() && ThePlayerList && ThePartitionManager )
		playerIndex = ThePlayerList->getLocalPlayer()->getPlayerIndex();

	// Keeps the strongest candidates: replaces the weakest one when full.
	auto add = [&]( const Coord3D &pos, Real radius, Real r, Real g, Real b, Real score )
	{
		if( playerIndex >= 0 && ThePartitionManager->getShroudStatusForPlayer( playerIndex, &pos ) != CellShroudStatus::CELLSHROUD_CLEAR )
			return;
		Int slot = count;
		if( count == RENDER_MAX_DYNAMIC_LIGHT_CANDIDATES )
		{
			slot = 0;
			for( Int i = 1; i < count; ++i )
				if( scores[i] < scores[slot] )
					slot = i;
			if( scores[slot] >= score )
				return;
		}
		else
			++count;
		// Raised a little so flat ground is not lit edge-on.
		lights[slot].x = pos.x;  lights[slot].y = pos.y;  lights[slot].z = pos.z + radius * 0.15f;
		lights[slot].radius = radius;
		lights[slot].r = r;  lights[slot].g = g;  lights[slot].b = b;
		scores[slot] = score;
	};

	// Light pulses (W3DDisplay::createLightPulse). They already tint vertex-lit terrain and objects, so
	// they count less here.
	if( scene )
	{
		RefRenderObjListIterator it( scene->getDynamicLights() );
		for( it.First(); !it.Is_Done(); it.Next() )
		{
			W3DDynamicLight *light = static_cast<W3DDynamicLight*>( it.Peek_Obj() );
			if( !light->isEnabled() )
				continue;
			Vector3 diffuse;
			light->Get_Diffuse( &diffuse );
			Real nearRange, farRange;
			light->Get_Far_Attenuation_Range( nearRange, farRange );
			const Real peak = MAX( diffuse.X, MAX( diffuse.Y, diffuse.Z ) );
			if( peak < 0.05f || farRange < 10.0f )
				continue;
			const Real radius = MIN( farRange, 400.0f );
			const Vector3 pos = light->Get_Position();
			Coord3D where;
			where.x = pos.X;  where.y = pos.Y;  where.z = pos.Z;
			const Real gain = 0.6f;
			add( where, radius, diffuse.X * gain, diffuse.Y * gain, diffuse.Z * gain, peak * radius * radius );
		}
	}

	// Additive billboard particle systems: one light per system at the weighted center of its particles. The light
	// follows each system smoothly, equally fast up and down so its mean brightness stays: flares that emit
	// short-lived particles every frame would otherwise switch it on and off at the logic rate.
	struct SmoothedLight
	{
		ParticleSystemID id;
		Coord3D center;
		Real radius, r, g, b, score, level;
		UnsignedInt pass;
	};
	// Fixed table (no heap memory held at shutdown); systems beyond it get an unsmoothed light.
	constexpr Int maxSmoothed = 256;
	static SmoothedLight s_smoothed[ maxSmoothed ];
	static Int s_smoothedCount = 0;
	static UnsignedInt s_lastMs = 0;
	static UnsignedInt s_pass = 0;
	const UnsignedInt nowMs = GameEngine_RenderTimeMs();
	const Real dtMs = ( s_lastMs != 0 && nowMs > s_lastMs ) ? static_cast<Real>( MIN( nowMs - s_lastMs, 250u ) ) : 0.0f;
	s_lastMs = nowMs;
	++s_pass;
	const Real follow = 1.0f - expf( -dtMs / 120.0f );
	auto emit = [&]( const SmoothedLight &s )
	{
		if( s.level >= 0.02f )
			add( s.center, s.radius, s.r * s.level, s.g * s.level, s.b * s.level, s.level * s.score * s.radius * s.radius );
	};
	if( TheParticleSystemManager )
	{
		ParticleSystemManager::ParticleSystemList &systems = TheParticleSystemManager->getAllParticleSystems();
		for( ParticleSystemManager::ParticleSystemListIt sit = systems.begin(); sit != systems.end(); ++sit )
		{
			ParticleSystem *sys = *sit;
			if( sys->getShaderType() != ParticleSystemInfo::ADDITIVE || sys->isUsingDrawables() || sys->isUsingStreak()
				|| sys->isUsingSmudge() || !sys->shouldBillboard() || sys->getParticleCount() == 0 )
				continue;
			Real weight = 0.0f, size = 0.0f, r = 0.0f, g = 0.0f, b = 0.0f;
			Coord3D center = { 0.0f, 0.0f, 0.0f };
			Int seen = 0;
			UnsignedInt slots;
			const ParticleState *states = sys->getParticleStates( &slots );
			for( UnsignedInt i = 0; i < slots && seen < 128; ++i )
			{
				const ParticleState *p = &states[i];
				if( !p->alive )
					continue;
				++seen;
				const Real alpha = p->alpha;
				if( alpha < 0.02f )
					continue;
				const Real w = alpha * MIN( p->size, 40.0f ) * ( 1.0f / 40.0f );
				const Coord3D *pos = &p->pos;
				const RGBColor *color = &p->color;
				weight += w;
				size += w * p->size;
				r += w * color->red;  g += w * color->green;  b += w * color->blue;
				center.x += w * pos->x;  center.y += w * pos->y;  center.z += w * pos->z;
			}
			SmoothedLight now = {};
			Real target = 0.0f;
			if( weight >= 0.1f )
			{
				const Real inv = 1.0f / weight;
				r *= inv;  g *= inv;  b *= inv;
				const Real peak = MAX( r, MAX( g, b ) );
				// Dark additive particles do not emit light.
				target = peak < 0.2f ? 0.0f : 1.0f;
				if( target > 0.0f )
				{
					now.center.x = center.x * inv;  now.center.y = center.y * inv;  now.center.z = center.z * inv;
					now.radius = MIN( MAX( size * inv * 4.0f, 50.0f ), 220.0f );
					const Real intensity = 0.25f * MIN( weight, 4.0f ) / peak;
					now.r = r * intensity;  now.g = g * intensity;  now.b = b * intensity;
					now.score = intensity * peak;
				}
			}
			const ParticleSystemID id = sys->getSystemID();
			SmoothedLight *found = nullptr;
			for( Int i = 0; i < s_smoothedCount; ++i )
				if( s_smoothed[ i ].id == id )
				{
					found = &s_smoothed[ i ];
					break;
				}
			if( !found )
			{
				if( target <= 0.0f )
					continue;
				now.id = id;
				now.level = target;
				now.pass = s_pass;
				if( s_smoothedCount < maxSmoothed )
					s_smoothed[ s_smoothedCount++ ] = now;
				emit( now );
				continue;
			}
			SmoothedLight &s = *found;
			s.pass = s_pass;
			if( target > 0.0f )
			{
				s.center = now.center;
				s.r = now.r;  s.g = now.g;  s.b = now.b;
				s.score = now.score;
				s.radius += ( now.radius - s.radius ) * follow;
			}
			s.level += ( target - s.level ) * follow;
			emit( s );
		}
	}
	// Systems that are gone or have no particles left give up their light at once; an afterglow of the many
	// short-lived systems of fires and explosions would brighten them.
	Int kept = 0;
	for( Int i = 0; i < s_smoothedCount; ++i )
		if( s_smoothed[ i ].pass == s_pass )
			s_smoothed[ kept++ ] = s_smoothed[ i ];
	s_smoothedCount = kept;

	RenderDevice::Set_Dynamic_Lights( lights, static_cast<unsigned>( count ) );
}

// -logUploads: in a game every drawn image writes a summary of the backend's uploads and draws to the runner
// log, images of the logic frames first..last also one line per upload and draw.
namespace
{
struct UploadLogFrame
{
	UploadLogFrame()
	{
		m_active = TheGlobalData->m_logUploads && TheGameLogic != nullptr && TheGameLogic->isInGame();
		if (!m_active)
			return;
		const UnsignedInt frame = TheGameLogic->getFrame();
		RenderDevice::Begin_Upload_Log( GameEngine_RunnerLog, frame,
			frame >= TheGlobalData->m_logUploadsFirst && frame <= TheGlobalData->m_logUploadsLast );
	}
	~UploadLogFrame()
	{
		if (m_active)
			RenderDevice::End_Upload_Log();
	}
	Bool m_active;
};
}

// W3DDisplay::draw ===========================================================
/** Draw the entire W3D Display */
//=============================================================================
//DECLARE_PERF_TIMER(W3DDisplay_draw)
void W3DDisplay::draw( void )
{
	ASSERT_MAIN_THREAD();

	RenderDevice::Set_Effect_Clock( GameEngine_RenderTimeMs() );
	const UploadLogFrame uploadLog;

	// A deterministic run waits for the background texture loader, so a frame never shows a texture
	// at a different load state than the same frame of another run.
	if( TheGlobalData->m_fixedTimeStep )
		TextureLoader::Flush_Pending_Load_Tasks();

	// Changing the detail level in the options menu updates TheGlobalData->m_renderEffects
	// (GameLODManager::applyStaticLODLevel); pass changes on to the render device here.
	{
		const RenderEffectSettings &current = RenderDevice::Get_Effect_Settings();
		if( current.flags != TheGlobalData->m_renderEffects || current.sharpen != TheGlobalData->m_renderEffectSharpen
			|| current.shadow_map_size != static_cast<unsigned>(TheGlobalData->m_renderEffectShadowMapSize)
			|| current.shadow_terrain_strength != TheGlobalData->m_renderEffectShadowTerrain
			|| current.terrain_detail != TheGlobalData->m_renderEffectTerrainDetail
			|| current.terrain_rock != TheGlobalData->m_renderEffectTerrainRock
			|| current.bloom != TheGlobalData->m_renderEffectBloom || current.heat != TheGlobalData->m_renderEffectHeat
			|| current.haze != TheGlobalData->m_renderEffectHaze || current.lights != TheGlobalData->m_renderEffectLights
			|| current.particle_light != TheGlobalData->m_renderEffectParticleLight
			|| current.terrain_normal != TheGlobalData->m_renderEffectTerrainNormal
			|| current.terrain_tess != TheGlobalData->m_renderEffectTerrainTess
			|| current.rock_tess != TheGlobalData->m_renderEffectRockTess
			|| current.flat_snow != TheGlobalData->m_renderEffectFlatSnow
			|| current.height_fog != TheGlobalData->m_renderEffectHeightFog
			|| current.sun_shafts != TheGlobalData->m_renderEffectSunShafts
			|| current.shadow_soft != TheGlobalData->m_renderEffectShadowSoft
			|| current.terrain_anisotropy != static_cast<unsigned>(TheGlobalData->m_terrainAnisotropy)
			|| current.terrain_lod_bias != TheGlobalData->m_terrainLodBias
			|| current.render_scale != TheGlobalData->m_renderScale
			|| current.render_scale_filter != static_cast<unsigned>(TheGlobalData->m_renderScaleFilter)
			|| current.render_scale_sharpen != TheGlobalData->m_renderScaleSharpen )
		{
			RenderEffectSettings effects = current;
			effects.flags = TheGlobalData->m_renderEffects;
			effects.sharpen = TheGlobalData->m_renderEffectSharpen;
			effects.shadow_map_size = static_cast<unsigned>(TheGlobalData->m_renderEffectShadowMapSize);
			effects.shadow_terrain_strength = TheGlobalData->m_renderEffectShadowTerrain;
			effects.terrain_detail = TheGlobalData->m_renderEffectTerrainDetail;
			effects.terrain_anisotropy = static_cast<unsigned>(TheGlobalData->m_terrainAnisotropy);
			effects.terrain_lod_bias = TheGlobalData->m_terrainLodBias;
			effects.terrain_rock = TheGlobalData->m_renderEffectTerrainRock;
			effects.bloom = TheGlobalData->m_renderEffectBloom;
			effects.heat = TheGlobalData->m_renderEffectHeat;
			effects.haze = TheGlobalData->m_renderEffectHaze;
			effects.lights = TheGlobalData->m_renderEffectLights;
			effects.particle_light = TheGlobalData->m_renderEffectParticleLight;
			effects.terrain_normal = TheGlobalData->m_renderEffectTerrainNormal;
			effects.terrain_tess = TheGlobalData->m_renderEffectTerrainTess;
			effects.rock_tess = TheGlobalData->m_renderEffectRockTess;
			effects.flat_snow = TheGlobalData->m_renderEffectFlatSnow;
			effects.height_fog = TheGlobalData->m_renderEffectHeightFog;
			effects.sun_shafts = TheGlobalData->m_renderEffectSunShafts;
			effects.shadow_soft = TheGlobalData->m_renderEffectShadowSoft;
			effects.render_scale = TheGlobalData->m_renderScale;
			effects.render_scale_filter = static_cast<unsigned>(TheGlobalData->m_renderScaleFilter);
			effects.render_scale_sharpen = TheGlobalData->m_renderScaleSharpen;
			effects.scene_format = static_cast<unsigned>(TheGlobalData->m_sceneFormat);
			effects.hdr_knee = TheGlobalData->m_hdrKnee;
			effects.hdr_additive = TheGlobalData->m_hdrAdditive;
			RenderDevice::Set_Effect_Settings( effects );
			DEBUG_LOG(( "D3D11 effects: flags now 0x%X (detail level)\n", effects.flags ));
		}
	}
	// Above the normal maximum camera height the shadow range grows with the height, so the far screen
	// edge keeps its shadows.
	{
		Real shadowDistance = RenderEffectSettings().shadow_distance;
		if( TheTacticalView && TheTacticalView->getZoomOutFactor() > 1.0f && TheGlobalData->m_maxCameraHeight > 0.0f )
		{
			const Real ratio = TheTacticalView->getHeightAboveGround() / TheGlobalData->m_maxCameraHeight;
			if( ratio > 1.0f )
				shadowDistance *= ratio;
		}
		const RenderEffectSettings &current = RenderDevice::Get_Effect_Settings();
		if( current.shadow_distance != shadowDistance )
		{
			RenderEffectSettings effects = current;
			effects.shadow_distance = shadowDistance;
			RenderDevice::Set_Effect_Settings( effects );
		}
	}
	//USE_PERF_TIMER(W3DDisplay_draw)
	static UnsignedInt syncTime = 0;

	extern HWND ApplicationHWnd;
	if (ApplicationHWnd && ::IsIconic(ApplicationHWnd)) {
		return;
	}


	// Role of this call (GameClient::DrawPhase). FULL advances views, particles and animation time and
	// then draws; UPDATE only advances (in the tick before the logic); RENDER only draws (an in-between
	// image or the tick image after the logic). -fixedTimeStep, -runner and fast forward always use FULL.
	const GameClient::DrawPhase drawPhase = TheGameClient->getDrawPhase();
	const Bool doUpdate = (drawPhase != GameClient::DrawPhase::DRAW_PHASE_RENDER);
	const Bool doRender = (drawPhase != GameClient::DrawPhase::DRAW_PHASE_UPDATE);

	if (doRender)
		GameEngine_BeginRenderFrameSteps();
	// HDR exposure follows the time of day of the map (-hdrNight on night maps).
	if (doRender && (RenderDevice::Get_Effect_Settings().flags & RENDER_EFFECT_HDR))
	{
		const Real exposure = (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT) ? TheGlobalData->m_hdrNight : 1.0f;
		if (RenderDevice::Get_Effect_Settings().hdr_exposure != exposure)
		{
			RenderEffectSettings effects = RenderDevice::Get_Effect_Settings();
			effects.hdr_exposure = exposure;
			RenderDevice::Set_Effect_Settings( effects );
		}
	}
	if( doRender && (RenderDevice::Get_Effect_Settings().flags & RENDER_EFFECT_LIGHTS) )
		collectDynamicLights( m_3DScene );

	if (doRender)	// the frame rate counts drawn images only
		updateAverageFPS();
	// No dynamic LOD with -fixedTimeStep: it depends on the real frame rate and would make test images
	// differ between runs.
	if (TheGlobalData->m_enableDynamicLOD && TheGameLogic->getShowDynamicLOD() && !TheGlobalData->m_fixedTimeStep)
	{
		DynamicGameLODLevel lod=TheGameLODManager->findDynamicLODLevel(m_averageFPS);
		TheGameLODManager->setDynamicLODLevel(lod);
	}
	else
	{	//if dynamic LOD is turned off, force highest LOD
		TheGameLODManager->setDynamicLODLevel(DYNAMIC_GAME_LOD_VERY_HIGH);
	}

	if (TheGlobalData->m_terrainLOD == TERRAIN_LOD_AUTOMATIC && TheTerrainRenderObject) 
	{
		calculateTerrainLOD();
	}
#ifdef EXTENDED_STATS
AGAIN:
#endif

#ifdef DUMP_PERF_STATS
	if( TheGlobalData->m_dumpPerformanceStatistics )
	{
		TheStatDump.dumpStats( FALSE, TRUE );
		TheWritableGlobalData->m_dumpPerformanceStatistics = FALSE;
	}
  //The <= GAME_REPLAY essentially means, GAME_SINGLE_PLAYER || GAME_LAN || GAME_SKIRMISH || GAME_REPLAY
  else if ( TheGlobalData->m_dumpStatsAtInterval && TheGameLogic->getGameMode() <= GAME_REPLAY )
  {
    Int interval = TheGlobalData->m_statsInterval;
    if ( TheGameLogic->getFrame() > 0 && (TheGameLogic->getFrame() % interval) == 0 )
    {
  	  TheStatDump.dumpStats( TRUE, TRUE );
    	TheInGameUI->message( UnicodeString( L"-stats is running, at interval: %d." ), TheGlobalData->m_statsInterval );
    }
  }




#endif

	// compute debug statistics for display later
	if ( m_debugDisplayCallback == StatDebugDisplay 
#if defined(_DEBUG)
				|| TheGlobalData->m_benchmarkTimer > 0
#endif
			)
	{
		if (doRender)	// statistics of the last drawn image
			gatherDebugStats();
	}
#ifdef EXTENDED_STATS
	else 
	{
		RenderDevice::stats.m_showingStats = false;
	}
#endif

#ifdef SAMPLE_DYNAMIC_LIGHT
	Vector3 loc;
	loc = theDynamicLight->Get_Position();
	loc.X += theLightXOffset;
	if(loc.X>128) theLightXOffset = -theLightXOffset;
	if(loc.X<0) theLightXOffset = -theLightXOffset;
	loc.Y += theLightYOffset;
	if(loc.Y>128) theLightYOffset = -theLightYOffset;
	if(loc.Y<0) theLightYOffset = -theLightYOffset;
	theDynamicLight->Set_Position(loc);
#endif


	/// @todo Make more explicit drawing layers(ground, ground UI, objects, object UI, overlay UI)

	///@todo: Ask Vegas why the LOD optimizer hangs particle system.
 	//
  	// Predictive LOD optimizer optimizes the mesh LOD levels to match 
  	// the given polygon budget
  	//
	//PredictiveLODOptimizerClass::Optimize_LODs( 5000 );

	Bool freezeTime = TheTacticalView->isTimeFrozen() && !TheTacticalView->isCameraMovementFinished();
	freezeTime = freezeTime || TheScriptEngine->isTimeFrozenDebug() || TheScriptEngine->isTimeFrozenScript();
	freezeTime = freezeTime || TheGameLogic->isGamePaused();
	// A render-only frame between two ticks: no view update, no time advance; camera and animation
	// time are interpolated between the last two ticks.
	const Bool logicFrozen = freezeTime;
	const Bool renderOnly = TheGameClient->isRenderOnlyFrame();
	const Real renderAlpha = TheGameClient->getRenderInterpolation();
	// The interpolated camera applies to everything in this frame; endInterpolatedCamera() restores it at the end.
	const Bool renderInterpolated = TheGameClient->isRenderInterpolated();	// also the tick image after the logic
	if (renderInterpolated)
		(static_cast<W3DView *>(getFirstView()))->beginInterpolatedCamera(renderAlpha);

	// hack to let client spin fast in network games but still do effects at the same pace. -MDC
	static UnsignedInt lastFrame = ~0u;
	// Render-only frames do not count here, otherwise the next tick draw would play a running camera
	// movement completely in the do/while loop below.
	if (doUpdate)	// once per tick (FULL or UPDATE), not in the tick image after the logic
	{
		freezeTime = freezeTime || (lastFrame == TheGameClient->getFrame());
		lastFrame = TheGameClient->getFrame();
	}

	/// @todo: I'm assuming the first view is our main 3D view.
	W3DView *primaryW3DView=static_cast<W3DView *>(getFirstView());
	if (!freezeTime && TheScriptEngine->isTimeFast())
	{
		if (doUpdate)	// an image without a tick advances nothing
		{
			primaryW3DView->updateCameraMovements();  // Update camera motion effects.
			syncTime += TheW3DFrameLengthInMsec;
		}
		primaryW3DView->endInterpolatedCamera();	// never leave with the interpolated camera
		return;
	}

	if (doRender)	// counters belong to the drawn image
		Debug_Statistics::Begin_Statistics();	//reset all counters (polygons, vertices, etc) before drawing

	//update state of all the terrain tracks (fade, remove, etc.)
	/// @todo: Is there a better place to put per-frame updates like this?

	if(doRender && TheGlobalData->m_loadScreenRender != TRUE)	// once per drawn image
	{

		if (TheTerrainTracksRenderObjClassSystem)
			TheTerrainTracksRenderObjClassSystem->update();

		//Shroud data is needed to render all other views, so handle this first.
		if (TheTerrainRenderObject)
		{	
			//update the shroud surface here since it may be needed by reflections
			if (TheTerrainRenderObject->getMap())	//make sure a valid map is loaded into terrain.
			{
				if (TheTerrainRenderObject->getShroud())
				{
					TheTerrainRenderObject->getShroud()->render(primaryW3DView->get3DCamera());
				}
			}
		}
	}

	if (!freezeTime && doUpdate)	// time advances only in the tick
	{
		/// @todo Decouple framerate from timestep
		// for now, use constant time steps to avoid animations running independent of framerate
		syncTime += TheW3DFrameLengthInMsec;
		// allow W3D to update its internals
		//	WW3D::Sync( GetTickCount() );
	}
	// Render frames get the interpolated time advance so animations run smoothly.
	const UnsignedInt frameSyncTime = (renderInterpolated && !logicFrozen) ? syncTime + static_cast<UnsignedInt>(renderAlpha * TheW3DFrameLengthInMsec) : syncTime;
	// A drawn frame without the tick's own update measures its frame time (WW3D::Get_Frame_Time, emitters, dazzles,
	// projector fades) from the previous drawn frame; otherwise the update sync of the tick in between would swallow the
	// time since that frame.
	static UnsignedInt lastDrawnSyncTime = 0;
	static Bool lastDrawnSyncValid = FALSE;
	if (doRender && !doUpdate && lastDrawnSyncValid && lastDrawnSyncTime <= frameSyncTime)
		WW3D::Sync( lastDrawnSyncTime );
	WW3D::Sync( frameSyncTime );
	if (doRender)
	{
		lastDrawnSyncTime = frameSyncTime;
		lastDrawnSyncValid = TRUE;
	}
	// Water moves per tick; drawn frames between two ticks show it advanced by the interpolation fraction.
	if (doRender && TheWaterRenderObj)
		TheWaterRenderObj->setRenderInterpolation( (renderInterpolated && !logicFrozen) ? renderAlpha : 0.0f );

	// Fast & Frozen time limits the time to 33 fps.
	Int minTime = 30;
	static Int prevTime = timeGetTime(), now;	

	now=timeGetTime();
	if (TheTacticalView->getTimeMultiplier()>1) 
	{
		static Int timeMultiplierCounter = 1;
		timeMultiplierCounter--;
		if (timeMultiplierCounter>1) 
		{
			primaryW3DView->endInterpolatedCamera();
			return;
		}
		timeMultiplierCounter = TheTacticalView->getTimeMultiplier();
		// limit the framerate, because while fast time is on, the game logic is running as fast as it can.
	}	
	else 
	{
		now = timeGetTime();
		prevTime = now - minTime;		 // do the first frame immediately.
	} 


	do {
		
		{
			if(TheGlobalData->m_loadScreenRender != TRUE)
			{
			
				// limit the framerate
				while(TheGlobalData->m_useFpsLimit && (now - prevTime) < minTime-1)
				{
					now = timeGetTime();
				}
				prevTime = now;
			}
		}

		// update all views of the world - recomputes data which will affect drawing
		if (RenderDevice::Is_Device_Ready())
		{	//Checking if we have the device before updating views because the heightmap crashes otherwise while
			//trying to refresh the visible terrain geometry.
//			if(TheGlobalData->m_loadScreenRender != TRUE)
				if (doUpdate)	// views update only in the tick
					updateViews();
				// Particle simulation only in the tick, after the views: systems attached to drawable bones read the
				// client transform while it is still valid.
				if (doUpdate)
				{
					const Engine::ScopedTimer particleTimer;
					TheParticleSystemManager->update();
					static_cast<W3DParticleSystemManager *>(TheParticleSystemManager)->addUpdateTime(particleTimer.elapsedMilliseconds());
				}


			if (doRender && TheWaterRenderObj && TheGlobalData->m_waterType == 2)	// only for the drawn image
				TheWaterRenderObj->updateRenderTargetTextures(primaryW3DView->get3DCamera());	//do a render into each texture

			//Can't render into textures while rendering to screen so these textures need to be updated
			//before we enter main rendering loop.
			if (doRender && TheW3DProjectedShadowManager)
				TheW3DProjectedShadowManager->updateRenderTargetTextures();
		}

		if (doRender)
			Debug_Statistics::End_Statistics();	//record number of polygons rendered in RenderTargetTextures.

		//Store number of polygons rendered in renderTargetTextures.
		Int numRenderTargetPolygons=Debug_Statistics::Get_Device_Polygons();
		Int numRenderTargetVertices=Debug_Statistics::Get_Device_Vertices();

		// start render block
	    if ( (TheGameLogic->getFrame() % 30 == 1) || ( ! (!TheGameLogic->isGamePaused() && TheGlobalData->m_TiVOFastMode && TheGameLogic->isInReplayGame())) )
		{
			//USE_PERF_TIMER(BigAssRenderLoop)
			static Bool couldRender = true;
			// Clear destination alpha to the minimum water opacity, or to 1 (depth code "deep") with
			// realistic water.
			if (doRender && (TheGlobalData->m_breakTheMovie == FALSE) && (TheGlobalData->m_disableRender == false) && WW3D::Begin_Render( true, true, Vector3( 0.0f, 0.0f, 0.0f ), WaterRenderObjClass::getFrameAlphaClearValue() ) == WW3D_ERROR_OK)		
			{
				
				if(TheGlobalData->m_loadScreenRender == TRUE)
				{	
					TheInGameUI->draw();
					if( TheMouse )
						TheMouse->draw();	//keep applying the current cursor style so it remains hidden if needed.
					WW3D::End_Render();	
					continue;
				}
				couldRender = true;
				// add the number of verts/polygons drawn before the main scene
				if (numRenderTargetPolygons || numRenderTargetVertices)
					Debug_Statistics::Record_Device_Polys_And_Vertices(numRenderTargetPolygons,numRenderTargetVertices,ShaderClass::_PresetOpaqueShader);

				// draw all views of the world
				drawViews();

				// draw the user interface
				RenderDevice::Gpu_Pass_Begin(RENDER_GPU_PASS_UI);
				TheInGameUI->DRAW();

				// end of video example code

				// draw the mouse
				// no mouse cursor in requested test screenshots
				if( TheMouse && TheGlobalData->m_shotAfterCount == 0 )
					TheMouse->DRAW();

				if ( m_videoStream && m_videoBuffer )
				{
					// Full screen movies are 4:3 sources; draw them centered in the stream aspect ratio
					// instead of stretching. The rest stays black (Begin_Render clears).
					Int x0 = 0, y0 = 0, x1 = getWidth(), y1 = getHeight();
					const Int videoW = m_videoStream->width();
					const Int videoH = m_videoStream->height();
					if ( videoW > 0 && videoH > 0 && x1 > 0 && y1 > 0 )
					{
						const Real videoAspect = static_cast<Real>(videoW) / static_cast<Real>(videoH);
						const Real displayAspect = static_cast<Real>(x1) / static_cast<Real>(y1);
						if ( displayAspect > videoAspect + 0.01f )
						{
							const Int w = static_cast<Int>( static_cast<Real>(y1) * videoAspect );
							x0 = ( x1 - w ) / 2;
							x1 = x0 + w;
						}
						else if ( displayAspect < videoAspect - 0.01f )
						{
							const Int h = static_cast<Int>( static_cast<Real>(x1) / videoAspect );
							y0 = ( y1 - h ) / 2;
							y1 = y0 + h;
						}
					}
					drawVideoBuffer( m_videoBuffer.get(), x0, y0, x1, y1 );
				}
				if( m_copyrightDisplayString )
				{
					Int x, y, dX, dY;
					m_copyrightDisplayString->getSize(&dX, &dY);
					x = (getWidth() / 2) - (dX /2);
					y = getHeight()  - dY - 20 ;
					m_copyrightDisplayString->draw(x, y, GameMakeColor(0,0,0,255), GameMakeColor(0,0,0,0),0,0);
				}
				// render letter box before debug display so debug info isn't hidden
				renderLetterBox(now);

				// display cinematicText over the black
				if( m_cinematicText != AsciiString::TheEmptyString && m_cinematicTextFrames != 0)
				{
					DisplayString *displayString = TheDisplayStringManager->newDisplayString();

					// set word wrap if neccessary

					Int wordWrapWidth = TheDisplay->getWidth() - 20;
					displayString->setWordWrap( wordWrapWidth );
					displayString->setWordWrapCentered( TRUE );

					UnicodeString text;
					text.translate( m_cinematicText );
					displayString->setText( text );
					Color color = GameMakeColor( 255, 255, 255, 255 );  // white
					Color backColor = GameMakeColor( 0, 0, 0, 0 );      // black
					displayString->setFont( m_cinematicFont );
					Int height = static_cast<Int>(TheDisplay->getHeight() * .9);

					Int width;
					if( static_cast<UnsignedInt>(displayString->getWidth()) > TheDisplay->getWidth() )
						width = 20;
					else
						width = ( TheDisplay->getWidth() - displayString->getWidth() ) / 2;
					displayString->draw( width, height, color, backColor );
					// The text is created per image; free it to avoid leaking a DisplayString each frame.
					TheDisplayStringManager->freeDisplayString( displayString );

					if (!renderOnly)	// counter runs per tick
						m_cinematicTextFrames--;
				}

				if ( m_debugDisplayCallback )
				{
					// draw the current debug display
					drawCurrentDebugDisplay();
				}

#if defined(_DEBUG)
				if (TheGlobalData->m_benchmarkTimer > 0)
				{
					drawFPSStats();
				}
#endif


#if defined(_DEBUG)
				if (TheGlobalData->m_debugShowGraphicalFramerate)
				{
					drawFramerateBar();
				}
#endif

				// test screenshots only in the tick draw (client frame count), never in render-only frames
				if (!renderOnly && (TheGlobalData->m_shotAfterCount > 0 || TheGlobalData->m_shotEvery > 0 || TheGlobalData->m_shotEveryDraw > 0))
					requestTestShot();
				if (!renderOnly && TheGlobalData->m_logTextureStats > 0)
					logTextureStatistics();
				
				// render is all done!
				RenderDevice::Gpu_Pass_End(RENDER_GPU_PASS_UI);
				WW3D::End_Render();	
				showScreenShotMessage();
				if (!renderOnly && TheGlobalData->m_logDrawStats > 0)
					logDrawStatistics();
				if (!renderOnly && TheGlobalData->m_logCamera && TheGameLogic->isInGame())
					logCameraClearance( static_cast<W3DView *>(getFirstView()) );
			}
			else
			{
				if (couldRender && doRender)
				{
					couldRender = false;
					DEBUG_LOG(("Could not do WW3D::Begin_Render()!  Are we ALT-Tabbed out?\n"));
				}
			}
		}
					
		if (TheScriptEngine->isTimeFrozenDebug() || TheScriptEngine->isTimeFrozenScript() || TheGameLogic->isGamePaused())	
		{
			freezeTime = false; // We're frozen for debug or for pause, and need to continue out of the loop.
		}

	// Render-only frames draw exactly once; the tick drives the camera movement.
	} while (freezeTime && drawPhase == GameClient::DrawPhase::DRAW_PHASE_FULL && !TheTacticalView->isCameraMovementFinished());

	// Restore the tick camera (no effect if nothing was interpolated)
	(static_cast<W3DView *>(getFirstView()))->endInterpolatedCamera();

#ifdef EXTENDED_STATS
	if (RenderDevice::stats.m_disableOverhead) {
		goto AGAIN;
	}
#endif
}  // end draw

constexpr auto LETTER_BOX_FADE_TIME = 1000.0f;		///1000 ms.

/** Render letter-box border at top/bottom of display
*/
void W3DDisplay::renderLetterBox(UnsignedInt currentTime)
{
		if (m_letterBoxEnabled)
		{	if (m_letterBoxFadeLevel != 1.0f)
			{
				m_letterBoxFadeLevel = (currentTime - m_letterBoxFadeStartTime)/LETTER_BOX_FADE_TIME;
				if (m_letterBoxFadeLevel > 1.0f)
					m_letterBoxFadeLevel = 1.0f;
			}

			UnsignedInt lbcolor = static_cast<Int>(m_letterBoxFadeLevel * 255.0f) << 24;

#ifdef SLIDE_LETTERBOX
			Int height = (Int)(getHeight() * 0.12f * m_letterBoxFadeLevel);
			TheTacticalView->setOrigin(0, height);
#else
			drawFillRect( 0, 0, m_width, static_cast<Int>((m_height-(9.0f/16.0f * m_width))*0.5f), lbcolor );
			drawFillRect( 0, static_cast<Int>(m_height-(m_height-(9.0f/16.0f * m_width))*0.5f), m_width, m_height, lbcolor );
#endif
		}
		else
		{	//letter box is disabled, but may still be fading out
			if (m_letterBoxFadeLevel != 0.0f)
			{
				m_letterBoxFadeLevel = 1.0f - (currentTime - m_letterBoxFadeStartTime)/LETTER_BOX_FADE_TIME;
				if (m_letterBoxFadeLevel < 0.0f)
					m_letterBoxFadeLevel = 0.0f;

				UnsignedInt lbcolor = static_cast<Int>(m_letterBoxFadeLevel * 255.0f) << 24;

#ifdef SLIDE_LETTERBOX
				Int height = (Int)(getHeight() * 0.12f * m_letterBoxFadeLevel);
				TheTacticalView->setOrigin(0, height);
#else
				drawFillRect( 0, 0, m_width, static_cast<Int>((m_height-(9.0f/16.0f * m_width))*0.5f), lbcolor );
				//drawFillRect( 0, m_height-(m_height-(9.0f/16.0f * m_width))*0.5f, m_width, m_height, lbcolor );
#endif
			}
			else
			{	//box has finished fading out
#ifdef SLIDE_LETTERBOX
				TheTacticalView->setOrigin(0, 0);
#else
				m_letterBoxEnabled = FALSE;
#endif
			}
		}
}

Bool W3DDisplay::isLetterBoxFading(void)
{
	if (m_letterBoxEnabled && m_letterBoxFadeLevel != 1.0f)
		return TRUE;
	if (!m_letterBoxEnabled && m_letterBoxFadeLevel != 0.0f)
		return TRUE;
	return FALSE;
}

//WST 10/2/2002 added query function.  JSC Integrated 5/20/03
Bool W3DDisplay::isLetterBoxed(void)
{
	return (m_letterBoxEnabled);
}

// W3DDisplay::createLightPulse ===============================================
/** Create a "light pulse" which is a dynamic light that grows, decays 
	* and vanishes over several frames */
//=============================================================================
void W3DDisplay::createLightPulse( const Coord3D *pos, const RGBColor *color, 
																	 Real innerRadius, Real attenuationWidth, 
																	 UnsignedInt increaseFrameTime, 
																	 UnsignedInt decayFrameTime//, Bool donut
																	 )
{
	if (innerRadius+attenuationWidth<2.0*PATHFIND_CELL_SIZE_F + 1.0f) {
		return; // it basically won't make any visual difference.  jba.
	}
	W3DDynamicLight * theDynamicLight = m_3DScene->getADynamicLight();
	// turn it on.
	theDynamicLight->setEnabled(true);

	theDynamicLight->Set_Ambient( Vector3( color->red, color->green, color->blue ) );
	theDynamicLight->Set_Diffuse( Vector3( color->red, color->green, color->blue) );
	theDynamicLight->Set_Position(Vector3(pos->x, pos->y, pos->z));
	theDynamicLight->Set_Far_Attenuation_Range(innerRadius, innerRadius + attenuationWidth);
	theDynamicLight->setFrameFade(increaseFrameTime, decayFrameTime);
	theDynamicLight->setDecayRange();
	theDynamicLight->setDecayColor();
	//theDynamicLight->setDonut(donut);
	// (gth) CNC3 enable far attenuation.  C&C3 defaults to disabled.  Must enable to match Generals. MW 8-06-03
	theDynamicLight->Set_Flag(LightClass::FAR_ATTENUATION,true);
}

void W3DDisplay::toggleLetterBox(void)
{
	m_letterBoxEnabled = !m_letterBoxEnabled;
	m_letterBoxFadeStartTime = timeGetTime();

	//WST  9/18/2002 This is not a script api to prevent cheat. JSC Integrated 5/20/03
	if( TheTacticalView )
	{
		TheTacticalView->setZoomLimited( !m_letterBoxEnabled );
	}  
}

void W3DDisplay::enableLetterBox(Bool enable)
{
	if (enable)
	{
		if (!m_letterBoxEnabled)
		{	//letterbox mode not previously enabled
			m_letterBoxEnabled = TRUE;
			m_letterBoxFadeStartTime = timeGetTime();

			//WST  9/18/2002 - This is not a script api to prevent cheat.  JSC Integrated 5/20/03
			if( TheTacticalView )
			{
				TheTacticalView->setZoomLimited( 0 );
			}  
		}
	}
	else
	{
		if (m_letterBoxEnabled)
		{	//letterbox mode no previously disabled
			m_letterBoxEnabled = FALSE;
			m_letterBoxFadeStartTime = timeGetTime();

			//WST  9/18/2002. JSC Integrated 5/20/03
			if( TheTacticalView )
			{
				TheTacticalView->setZoomLimited( 1 );
			}
		}
	}
}

// W3DDisplay::setTimeOfDay ===================================================
/** */
//=============================================================================
void W3DDisplay::setTimeOfDay( TimeOfDay tod )
{
	const GlobalData::TerrainLighting *ol=&TheGlobalData->m_terrainObjectsLighting[tod][0];

	if( m_3DScene )
	{
		m_3DScene->Set_Ambient_Light( Vector3(ol->ambient.red, ol->ambient.green, ol->ambient.blue) );
	}

	for (Int i=0; i<LightEnvironmentClass::MAX_LIGHTS; i++)
	{
		if( m_myLight[i] )
		{
			ol=&TheGlobalData->m_terrainObjectsLighting[tod][i];

			m_myLight[i]->Set_Ambient( Vector3( 0.0f, 0.0f, 0.0f ) );
			m_myLight[i]->Set_Diffuse( Vector3(ol->diffuse.red, ol->diffuse.green, ol->diffuse.blue ) );
			m_myLight[i]->Set_Specular( Vector3(0,0,0) );
			Matrix3D mtx;
			mtx.Set(Vector3(1,0,0), Vector3(0,1,0), Vector3(ol->lightPos.x, ol->lightPos.y, ol->lightPos.z), Vector3(0,0,0));
			m_myLight[i]->Set_Transform(mtx);
		}
	}
	if(TheTerrainRenderObject) {
		TheTerrainRenderObject->setTimeOfDay(tod);
		TheTacticalView->forceRedraw();
	}
}




//============================================================================
// W3DDisplay::createVideoBuffer
//============================================================================


//============================================================================
// W3DDisplay::drawVideoBuffer
//============================================================================

//=============================================================================
/* we don't really need to override this call, since we will soon be called to
	update every shroud cell explicitly...
*/
void W3DDisplay::clearShroud()
{
	// nothing
}

//=============================================================================
void W3DDisplay::setBorderShroudLevel(UnsignedByte level)
{
	if (TheTerrainRenderObject && TheTerrainRenderObject->getShroud())
	{
		TheTerrainRenderObject->getShroud()->setBorderShroudLevel(static_cast<W3DShroudLevel>(level));
	}
}

//=============================================================================
void W3DDisplay::setShroudLevel( Int x, Int y, CellShroudStatus setting )
{
	if (TheTerrainRenderObject && TheTerrainRenderObject->getShroud())
	{
		#ifdef INTENSE_DEBUG
		TheTerrainRenderObject->getShroud()->setShroudFilter(false);
		#endif
		if( setting == CellShroudStatus::CELLSHROUD_SHROUDED )
			TheTerrainRenderObject->getShroud()->setShroudLevel(x, y, static_cast<W3DShroudLevel>(TheGlobalData->m_shroudAlpha) );
		else if( setting == CellShroudStatus::CELLSHROUD_FOGGED )
			TheTerrainRenderObject->getShroud()->setShroudLevel(x, y, static_cast<W3DShroudLevel>(TheGlobalData->m_fogAlpha) );///< @todo placeholder to get feedback on logic work while graphic side being decided
		else
			TheTerrainRenderObject->getShroud()->setShroudLevel(x, y, static_cast<W3DShroudLevel>(TheGlobalData->m_clearAlpha) );
		//Logic is saying shroud.  We can add alpha levels here in client if needed.  
		// W3DShroud is a 0-255 alpha byte.  Logic shroud is a double reference count.

		TheTerrainRenderObject->notifyShroudChanged();
	
	}
}



//-------------------------------------------------------------------------------------------------
#if defined(_DEBUG)
static void drawFramerateBar(void)
{
	static DWORD prevTime = timeGetTime();
	DWORD now = timeGetTime();
	Real percTime = (1000.0f / (now - prevTime) ) / (1000.0f / TheGlobalData->m_framesPerSecondLimit);

	if (percTime > 1.0f)
		percTime = 1.0f;
	else if (percTime < 0.0f)
		percTime = 0.0f;
	Int width = REAL_TO_INT(percTime * TheDisplay->getWidth());
	UnsignedInt colorToUse = GameMakeColor( REAL_TO_UNSIGNEDBYTE((1.0f - percTime) * 255), 
																					REAL_TO_UNSIGNEDBYTE(percTime * 255), 
																					0, 
																					0x7F);

	TheDisplay->drawFillRect(1, 1, width, 15, colorToUse);
	prevTime = now;
}
#endif
