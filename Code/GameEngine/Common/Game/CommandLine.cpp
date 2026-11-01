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


#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ArchiveFileSystem.h"
#include "Common/CommandLine.h"
#include "Common/CRCDebug.h"
#include "Common/LocalFileSystem.h"
#include "Common/version.h"
#include "Common/UserSettings.h"
#include "GameClient/TerrainVisual.h" // for TERRAIN_LOD_MIN definition
#include "GameClient/GameText.h"
#include "GameNetwork/NetworkDefs.h"	// MAX_SLOTS
#include "Engine/JobSystem.h"
#include "Common/GameEdition.h"
#include "Engine/TextureTier.h"

#include <algorithm>
#include <string>



Bool TheDebugIgnoreSyncErrors = FALSE;
extern Int RenderDevice_PreserveFPU;

#ifdef DEBUG_CRC
Int TheCRCFirstFrameToLog = -1;
UnsignedInt TheCRCLastFrameToLog = 0xffffffff;
Bool g_keepCRCSaves = FALSE;
Bool g_crcModuleDataFromLogic = FALSE;
Bool g_crcModuleDataFromClient = FALSE;
Bool g_verifyClientCRC = FALSE; // verify that GameLogic CRC doesn't change from client
Bool g_clientDeepCRC = FALSE;
Bool g_logObjectCRCs = FALSE;
#endif

#if defined(_DEBUG)
extern Bool g_useStringFile;
#endif

// Retval is number of cmd-line args eaten
using FuncPtr = Int (*)( char *args[], int num );

static const UnsignedByte F_NOCASE = 1; // Case-insensitive

struct CommandLineParam
{
	const char *name;
	FuncPtr func;
};

static void ConvertShortMapPathToLongMapPath(AsciiString &mapName)
{
	AsciiString path = mapName;
	AsciiString token;
	AsciiString actualpath;

	if ((path.find('\\') == nullptr) && (path.find('/') == nullptr))
	{
		DEBUG_CRASH(("Invalid map name %s", mapName.str()));
		return;
	}
	AsciiString folder;	// last folder before the file name
	path.nextToken(&token, "\\/");
	while (!token.endsWithNoCase(".map") && (token.getLength() > 0))
	{
		actualpath.concat(token);
		actualpath.concat('\\');
		folder = token;
		path.nextToken(&token, "\\/");
	}

	if (!token.endsWithNoCase(".map"))
	{
		DEBUG_CRASH(("Invalid map name %s", mapName.str()));
		return;
	}
	// remove the .map from the end.
	token.removeLastChar();
	token.removeLastChar();
	token.removeLastChar();
	token.removeLastChar();

	// already the long form <folder>\<name>\<name>.map
	if (folder.compareNoCase(token) == 0)
		return;

	actualpath.concat(token);
	actualpath.concat('\\');
	actualpath.concat(token);
	actualpath.concat(".map");

	mapName = actualpath;
}

//=============================================================================
//=============================================================================
Int parseNoLogOrCrash(char *args[], int)
{
#ifdef ALLOW_DEBUG_UTILS
	DEBUG_CRASH(("-NoLogOrCrash not supported in this build\n"));
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseWin(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_windowed = true;
	}
	return 1;
}

// Borderless window at desktop resolution (implies -win). WinMain.cpp also checks the flag early,
// because it determines the window style.
Int parseBorderless(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_windowed = true;
		TheWritableGlobalData->m_borderless = TRUE;
	}
	return 1;
}

// -fullscreenArea <width> <height>: the fullscreen path, but the borderless window covers only this
// area centred on the monitor. Lets tests run the fullscreen window and scaling code without
// covering the screen. WinMain.cpp also reads it early for the initial window size.
Int parseFullscreenArea(char *args[], int num)
{
	if (TheWritableGlobalData && num > 2)
	{
		TheWritableGlobalData->m_fullscreenAreaWidth = atoi(args[1]);
		TheWritableGlobalData->m_fullscreenAreaHeight = atoi(args[2]);
		return 3;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoMusic(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_musicOn = false;
	}
	return 1;
}


//=============================================================================
//=============================================================================
Int parseNoVideo(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_videoOn = false;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseFPUPreserve(char *args[], int argc)
{
	if (argc > 1)
	{
		RenderDevice_PreserveFPU = atoi(args[1]);
	}
	return 2;
}

#if defined(_DEBUG)
//=============================================================================
//=============================================================================
Int parseUseCSF(char *args[], int)
{
	g_useStringFile = FALSE;
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoInputDisable(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableScriptedInputDisabling = true;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoFade(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableCameraFade = true;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoMilCap(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableMilitaryCaption = true;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseDebugCRCFromFrame(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		TheCRCFirstFrameToLog = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseDebugCRCUntilFrame(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		TheCRCLastFrameToLog = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseKeepCRCSave(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_keepCRCSaves = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseCRCLogicModuleData(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_crcModuleDataFromLogic = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseCRCClientModuleData(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_crcModuleDataFromClient = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseClientDeepCRC(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_clientDeepCRC = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseVerifyClientCRC(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_verifyClientCRC = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseLogObjectCRCs(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_logObjectCRCs = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNetCRCInterval(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		NET_CRC_INTERVAL = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseReplayCRCInterval(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		REPLAY_CRC_INTERVAL = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseNoDraw(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_noDraw = TRUE;
	}
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseLogToConsole(char *args[], int)
{
	DebugSetFlags(DebugGetFlags() | DEBUG_FLAG_LOG_TO_CONSOLE);
	return 1;
}

#endif // _DEBUG

//=============================================================================
//=============================================================================
Int parseNoAudio(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_audioOn = false;
		TheWritableGlobalData->m_speechOn = false;
		TheWritableGlobalData->m_soundsOn = false;
		TheWritableGlobalData->m_musicOn = false;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoWin(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_windowed = false;
	}
	return 1;
}

Int parseFullVersion(char *args[], int num)
{
	if (TheVersion && num > 1)
	{
		TheVersion->setShowFullVersion(atoi(args[1]) != 0);
	}
	return 1;
}

Int parseNoShadows(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_useShadowVolumes = false;
		TheWritableGlobalData->m_useShadowDecals = false;
	}
	return 1;
}

Int parseMapName(char *args[], int num)
{
	if (TheWritableGlobalData && num == 2)
	{
		TheWritableGlobalData->m_mapName.set( args[ 1 ] );
		ConvertShortMapPathToLongMapPath(TheWritableGlobalData->m_mapName);
	}
	return 1;
}

Int parseXRes(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_xResolution = atoi(args[1]);
		return 2;
	}
	return 1;
}

Int parseYRes(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_yResolution = atoi(args[1]);
		return 2;
	}
	return 1;
}

#if defined(_DEBUG)
//=============================================================================
//=============================================================================
Int parseLowDetail(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_terrainLOD = TERRAIN_LOD_MIN;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoDynamicLOD(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_enableDynamicLOD = FALSE;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoStaticLOD(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_enableStaticLOD = FALSE;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseUseWaveEditor(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_usingWaterTrackEditor = TRUE;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseFPSLimit(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_framesPerSecondLimit = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
Int parseNoViewLimit(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_useCameraConstraints = FALSE;
	}
	return 1;
}

Int parseWireframe(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_wireframe = TRUE;
	}
	return 1;
}

Int parseShowCollision(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_showCollisionExtents = TRUE;
	}
	return 1;
}

Int parseNoShowClientPhysics(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_showClientPhysics = FALSE;
	}
	return 1;
}

Int parseShowTerrainNormals(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_showTerrainNormals = TRUE;
	}
	return 1;
}

Int parseStateMachineDebug(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_stateMachineDebug = TRUE;
	}
	return 1;
}

Int parseJabber(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_jabberOn = TRUE;
	}
	return 1;
}

Int parseMunkee(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_munkeeOn = TRUE;
	}
	return 1;
}
#endif // defined(_DEBUG)

Int parseScriptDebug(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_scriptDebug = TRUE;
		TheWritableGlobalData->m_winCursors = TRUE;
	}
	return 1;
}



Int parseBuildMapCache(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_buildMapCache = true;
	}
	return 1;
}


#if defined(_DEBUG)
Int parsePreload( char *args[], int num )
{
	if( TheWritableGlobalData )
		TheWritableGlobalData->m_preloadAssets = TRUE;
	return 1;
}
#endif


#if defined(_DEBUG) 
Int parseDisplayDebug(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_displayDebug = TRUE;
	}
	return 1;
}

Int parseFile(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_initialFile = args[1];
		ConvertShortMapPathToLongMapPath(TheWritableGlobalData->m_initialFile);
	}
	return 2;
}


Int parsePreloadEverything( char *args[], int num )
{
	if( TheWritableGlobalData )
	{
		TheWritableGlobalData->m_preloadAssets = TRUE;
		TheWritableGlobalData->m_preloadEverything = TRUE;
	}
	return 1;
}

Int parseLogAssets( char *args[], int num )
{
	if( TheWritableGlobalData )
	{
		FILE *logfile=fopen("PreloadedAssets.txt","w");
		if (logfile)	//clear the file
			fclose(logfile);
		TheWritableGlobalData->m_preloadReport = TRUE;
	}
	return 1;
}

#endif // defined(_DEBUG)

//=============================================================================
//=============================================================================

Int parseNoFX(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_useFX = FALSE;
	}
	return 1;
}

#if defined(_DEBUG)
Int parseNoShroud(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_shroudOn = FALSE;
	}
	return 1;
}
#endif

Int parseNoMoveCamera(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableCameraMovement = true;
	}
	return 1;
}

#if defined(_DEBUG)
Int parseNoCinematic(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableCameraMovement = true;
		TheWritableGlobalData->m_disableMilitaryCaption = true;
		TheWritableGlobalData->m_disableCameraFade = true;
		TheWritableGlobalData->m_disableScriptedInputDisabling = true;
	}
	return 1;
}
#endif

Int parseSync(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheDebugIgnoreSyncErrors = true;
	}
	return 1;
}

Int parseNoShellMap(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_shellMapOn = FALSE;
	}
	return 1;
}

Int parseNoShaders(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_chipSetType = 1;	// No effect: m_chipSetType is not evaluated.
	}
	return 1;
}

// Available in all build configurations. Otherwise the EA logo and sizzle movie play, and while a
// movie plays no interpolated frames are drawn, which skews measurements.
Int parseNoLogo(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_playIntro = FALSE;
		TheWritableGlobalData->m_afterIntro = TRUE;
		TheWritableGlobalData->m_playSizzle = FALSE;
	}
	return 1;
}

Int parseNoSizzle( char *args[], int )
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_playSizzle = FALSE;
	}
	return 1;
}

Int parseShellMap(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_shellMapName = args[1];
	}
	return 2;
}

Int parseNoWindowAnimation(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_animateWindows = FALSE;
	}
	return 1;
}

Int parseWinCursors(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_winCursors = TRUE;
	}
	return 1;
}

Int parseQuickStart( char *args[], int num )
{
#if defined(_DEBUG)
  parseNoLogo( args, num );
#else
	// Both games skip the logo and the sizzle video.
	parseNoLogo( args, num );
	parseNoSizzle( args, num );
#endif
	parseNoShellMap( args, num );
	parseNoWindowAnimation( args, num );
	return 1;
}

Int parseConstantDebug( char *args[], int num )
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_constantDebugUpdate = TRUE;
	}
	return 1;
}

#if defined(_DEBUG)
Int parseExtraLogging( char *args[], int num )
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_extraLogging = TRUE;
	}
	return 1;
}
#endif

//-allAdvice feature
/*
Int parseAllAdvice( char *args[], int num )
{
	if( TheWritableGlobalData )
	{
		TheWritableGlobalData->m_allAdvice = TRUE;
	}
	return 1;
}
*/

Int parseShowTeamDot( char *args[], int num )
{
	if( TheWritableGlobalData )
	{
		TheWritableGlobalData->m_showTeamDot = TRUE;
	}
	return 1;
}


#if defined(_DEBUG)
Int parseSelectAll( char *args[], int num )
{
	if( TheWritableGlobalData )
	{
		TheWritableGlobalData->m_allowUnselectableSelection = TRUE;
	}
	return 1;
}

Int parseRunAhead( char *args[], Int num )
{
	if (num > 2)
	{
		MIN_RUNAHEAD = atoi(args[1]);
		MAX_FRAMES_AHEAD = atoi(args[2]);
		FRAME_DATA_LENGTH = (MAX_FRAMES_AHEAD + 1)*2;
	}
	return 3;
}
#endif


Int parseSeed(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_fixedSeed = atoi(args[1]);
	}
	return 2;
}

Int parseIncrAGPBuf(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_incrementalAGPBuf = TRUE;
	}
	return 1;
}

Int parseNetMinPlayers(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_netMinPlayers = atoi(args[1]);
	}
	return 2;
}

Int parsePlayStats(char *args[], int num)
{
	if (TheWritableGlobalData  && num > 1)
	{
		TheWritableGlobalData->m_playStats  = atoi(args[1]);
	}
	return 2;
}

Int parseDemoLoadScreen(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_loadScreenDemo = TRUE;
	}
	return 1;
}

#if defined(_DEBUG)
Int parseSaveStats(char *args[], int num)
{
	if (TheWritableGlobalData  && num > 1)
	{
		TheWritableGlobalData->m_saveStats = TRUE;
		TheWritableGlobalData->m_baseStatsDir = args[1];
	}
	return 2;
}
#endif

#if defined(_DEBUG)
Int parseSaveAllStats(char *args[], int num)
{
	if (TheWritableGlobalData  && num > 1)
	{
		TheWritableGlobalData->m_saveStats = TRUE;
		TheWritableGlobalData->m_baseStatsDir = args[1];
		TheWritableGlobalData->m_saveAllStats = TRUE;
	}
	return 2;
}
#endif

#if defined(_DEBUG)
Int parseLocalMOTD(char *args[], int num)
{
	if (TheWritableGlobalData  && num > 1)
	{
		TheWritableGlobalData->m_useLocalMOTD = TRUE;
		TheWritableGlobalData->m_MOTDPath = args[1];
	}
	return 2;
}
#endif

#if defined(_DEBUG)
Int parseCameraDebug(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_debugCamera = TRUE;
	}
	return 1;
}
#endif

#if defined(_DEBUG)
Int parseBenchmark(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_benchmarkTimer = atoi(args[1]);
		TheWritableGlobalData->m_playStats  = atoi(args[1]);
	}
	return 2;
}
#endif

#if defined(_DEBUG)
#ifdef DUMP_PERF_STATS
Int parseStats(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_dumpStatsAtInterval = TRUE;
		TheWritableGlobalData->m_statsInterval  = atoi(args[1]);
	}
	return 2;
}
#endif
#endif

#if defined(_DEBUG)
Int parseIgnoreAsserts(char *args[], int num)
{
	if (TheWritableGlobalData && num > 0)
	{
		TheWritableGlobalData->m_debugIgnoreAsserts = true;
	}
	return 1;
}
#endif

#if defined(_DEBUG)
Int parseIgnoreStackTrace(char *args[], int num)
{
	if (TheWritableGlobalData && num > 0)
	{
		TheWritableGlobalData->m_debugIgnoreStackTrace = true;
	}
	return 1;
}
#endif

Int parseNoFPSLimit(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_useFpsLimit = false;
		TheWritableGlobalData->m_framesPerSecondLimit = 30000;
	}
	return 1;
}

Int parseDumpAssetUsage(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_dumpAssetUsage = true;
	}
	return 1;
}

Int parseJumpToFrame(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		parseNoFPSLimit(args, num);
		TheWritableGlobalData->m_noDraw = atoi(args[1]);
		return 2;
	}
	return 1;
}

Int parseUpdateImages(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_shouldUpdateTGAToDDS = TRUE;
	}
	return 1;
}

// -baseGame <folder> (Generals base archives for Zero Hour) is evaluated by
// Win32BIGFileSystem::init before parseCommandLine; here only skip the value.
Int parseBaseGame(char *args[], Int num)
{
	return (num > 1) ? 2 : 1;
}

// -benutzerdaten <folder> (user data folder, so test runs stay out of the real one) is evaluated by the
// GlobalData constructor before parseCommandLine; here only skip the value.
Int parseUserDataFolder(char *args[], Int num)
{
	return (num > 1) ? 2 : 1;
}

// -vorlagenAusgeben <file>: after the subsystems are loaded, write the things, upgrades, sciences and special
// powers with the numbers that replays and network commands carry (GameEngine::init). A plain std::string:
// this static lives past the memory manager.
static std::string s_gameDataDumpPath;

Int parseGameDataDump(char *args[], Int num)
{
	if (num > 1)
	{
		s_gameDataDumpPath = args[1];
		return 2;
	}
	return 1;
}

// -staticLOD <name>: detail level for this run (benchmarks on other machines); the options file is not changed.
static std::string s_forcedStaticLOD;

Int parseStaticLOD(char *args[], Int num)
{
	if (num > 1)
	{
		s_forcedStaticLOD = args[1];
		return 2;
	}
	return 1;
}

const char *getForcedStaticLOD()
{
	return s_forcedStaticLOD.empty() ? nullptr : s_forcedStaticLOD.c_str();
}

const char *getGameDataDumpPath()
{
	return s_gameDataDumpPath.empty() ? nullptr : s_gameDataDumpPath.c_str();
}

// -dateiIndexAusgeben <file>: after the tiers and mods are loaded, write every archived path and the
// archive it resolves to (ArchiveFileSystem::writeIndexDump). A plain std::string: this static lives
// past the memory manager, which an AsciiString would need at exit. Its heap block is released right
// after the dump, otherwise the leak report at exit counts it.
static std::string s_indexDumpPath;

// -modellPruefung <file>: after the display is up, load every model under Art\W3D and every DDS under
// Art\Textures, write loaded, duplicate and rejected parts to <file> and quit (W3DAssetManager::Check_Model_Files).
Int parseModelCheck(char *args[], Int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_modelCheckFile = args[1];
		return 2;
	}
	return 1;
}

Int parseIndexDump(char *args[], Int num)
{
	if (num > 1)
	{
		s_indexDumpPath = args[1];
		return 2;
	}
	return 1;
}

Int parseMod(char *args[], Int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		AsciiString modPath = args[1];
		if (strchr(modPath.str(), ':') || modPath.startsWith("/") || modPath.startsWith("\\"))
		{
			// full path passed in.  Don't append base path.
		}
		else
		{
			// relative to the user data folder, then to its Mods subfolder
			modPath.format("%s%s", TheGlobalData->getPath_UserData().str(), args[1]);
			if (!TheLocalFileSystem->doesFileExist(modPath.str()))
			{
				AsciiString inMods;
				inMods.format("%sMods\\%s", TheGlobalData->getPath_UserData().str(), args[1]);
				if (TheLocalFileSystem->doesFileExist(inMods.str()))
					modPath = inMods;
			}
		}
		DEBUG_LOG(("Looking for mod '%s'\n", modPath.str()));

		if (!TheLocalFileSystem->doesFileExist(modPath.str()))
		{
			DEBUG_LOG(("Mod does not exist.\n"));
			if (TheArchiveFileSystem)
				TheArchiveFileSystem->addModProblem(AsciiString(args[1]), ArchiveFileSystem::ModProblemKind::Missing, AsciiString::TheEmptyString);
			return 2; // no such file/dir.
		}

		// now check for dir-ness
		struct _stat statBuf;
		if (_stat(modPath.str(), &statBuf) != 0)
		{
			DEBUG_LOG(("Could not _stat() mod.\n"));
			return 2; // could not stat the file/dir.
		}

		if (statBuf.st_mode & _S_IFDIR)
		{
			if (!modPath.endsWith("\\") && !modPath.endsWith("/"))
				modPath.concat('\\');
			DEBUG_LOG(("Mod dir is '%s'.\n", modPath.str()));
		}
		else
		{
			DEBUG_LOG(("Mod file is '%s'.\n", modPath.str()));
		}
		// The same mod named twice keeps its first place.
		std::vector<AsciiString> &mods = TheWritableGlobalData->m_mods;
		if (std::find_if(mods.begin(), mods.end(), [&modPath](const AsciiString &mod) { return mod.compareNoCase(modPath) == 0; }) == mods.end())
			mods.push_back(modPath);
		// The command line replaces the active list of Settings.ini for this run (highest first, like the list).
		AsciiString list;
		for (std::vector<AsciiString>::const_reverse_iterator it = mods.rbegin(); it != mods.rend(); ++it)
		{
			if (list.isNotEmpty())
				list.concat(';');
			list.concat(*it);
		}
		UserSettings::setCommandLine("Mods.Active", list.str());

		return 2;
	}
	return 1;
}

#if defined(_DEBUG)
Int parseSetDebugLevel(char *args[], int num)
{
	if (num > 1)
	{
		AsciiString val = args[1];
		for (Int i=0; i<DEBUG_LEVEL_MAX; ++i)
		{
			if (val == TheDebugLevels[i])
			{
				DebugLevelMask |= 1<<i;
				break;
			}
		}
	}
	return 2;
}

Int parseClearDebugLevel(char *args[], int num)
{
	if (num > 1)
	{
		AsciiString val = args[1];
		for (Int i=0; i<DEBUG_LEVEL_MAX; ++i)
		{
			if (val == TheDebugLevels[i])
			{
				DebugLevelMask &= ~(1<<i);
				break;
			}
		}
	}
	return 2;
}
#endif

//=============================================================================
// Replay runner and automatic skirmish start for determinism tests, benchmarks and long runs.
// Available in all build configurations on purpose (measurements use the release/profile build).
//=============================================================================
static void skipIntroAndShellMap()
{
	TheWritableGlobalData->m_shellMapOn = FALSE;
	TheWritableGlobalData->m_playIntro = FALSE;
	TheWritableGlobalData->m_afterIntro = TRUE;
}

// -replay <file>: plays a replay (absolute path or relative to the replay folder). Through
// m_initialFile the shell skips the main menu, and the end of the match quits the game
// (GameLogic::clearGameData).
Int parseReplay(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_initialFile = args[1];
		skipIntroAndShellMap();
	}
	return 2;
}

// -runner: draws nothing (only the first frame), no sound, logic ticks without waiting.
Int parseRunner(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_runnerMode = TRUE;
		TheWritableGlobalData->m_noDraw = 0xFFFFFFFF;
		parseNoAudio(args, 1);
		skipIntroAndShellMap();
	}
	return 1;
}

Int parseCRCLog(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_crcLogFile = args[1];
	return 2;
}

Int parseCRCLogInterval(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		Int interval = atoi(args[1]);
		TheWritableGlobalData->m_crcLogInterval = (interval > 0) ? interval : 1;
	}
	return 2;
}

// Deep CRC dump with offset index for logic frames first..last, stored next to the -crcLog file
// (<crcLog>.f<frame>.deep and .deep.idx). Requires -crcLog.
Int parseDeepCRCFrames(char *args[], int num)
{
	if (TheWritableGlobalData && num > 2)
	{
		TheWritableGlobalData->m_deepCRCFrameFirst = atoi(args[1]);
		TheWritableGlobalData->m_deepCRCFrameLast = atoi(args[2]);
	}
	return 3;
}

// Full save game state per object in the deep CRC dump (xfer() instead of crc() only), so it also
// covers physics, locomotor, move target and AI state. Requires -deepCRCFrames.
Int parseDeepCRCFull(char *args[], int num)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_deepCRCFull = TRUE;
	return 1;
}

Int parseMaxFrames(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_maxFrames = static_cast<UnsignedInt>(atoi(args[1]));
	return 2;
}

// -skirmish <map>: "Alpine Assault" or "Maps\Alpine Assault\Alpine Assault.map".
Int parseSkirmish(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		AsciiString map = args[1];
		if (map.find('\\') == nullptr && map.find('/') == nullptr)
			map.format("Maps\\%s\\%s.map", args[1], args[1]);
		else
			ConvertShortMapPathToLongMapPath(map);
		TheWritableGlobalData->m_autoSkirmishMap = map;
		TheWritableGlobalData->m_initialFile = map;	// skip the main menu; the end of the match quits the game
		skipIntroAndShellMap();
	}
	return 2;
}

Int parseSkirmishAI(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		// Either a count or a list per slot, e.g. "N1,N1,K2,K2": K = classic AI, N = new AI, E = new AI
		// Easy, M = new AI Medium, digit = team 1 to 4 (no digit = free for all). A plain number means
		// that many classic AIs, free for all.
		const char *spec = args[1];
		Int count;
		if (strpbrk(spec, "KkNnEeMm") != nullptr)
		{
			TheWritableGlobalData->m_autoSkirmishSpec = spec;
			count = 1;
			for (const char *c = spec; *c; ++c)
				if (*c == ',')
					++count;
			// a leading H<digit> is the local player as a partner, not an AI
			const char *first = spec;
			while (*first == ' ')
				++first;
			if (*first == 'H' || *first == 'h')
				--count;
		}
		else
		{
			TheWritableGlobalData->m_autoSkirmishSpec.clear();
			count = atoi(spec);
		}
		TheWritableGlobalData->m_autoSkirmishAIs = (count < 1) ? 1 : ((count > MAX_SLOTS) ? MAX_SLOTS : count);
	}
	return 2;
}

// -aiStats N: KISTAT every N logic frames to the runner log, KISIEG and quit as soon as only one
// alliance is left.
Int parseAIStats(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		Int n = atoi(args[1]);
		TheWritableGlobalData->m_aiStatsInterval = (n < 0) ? 0 : n;
	}
	return 2;
}

// Bits of a comma list of AIPart names; an unknown name gives AI_PART_UNKNOWN, which the AI reports in
// its KIOHNE log line.
static UnsignedInt parseAIPartList( const char *text )
{
	static const struct { const char *name; UnsignedInt part; } parts[] =
	{
		{ "prognose", AI_PART_FORECAST_RETREAT }, { "bestaetigen", AI_PART_CONFIRM }, { "erstab", AI_PART_REGROW },
		{ "zuschwach", AI_PART_LAUNCH_GATE }, { "ziel", AI_PART_PLANNER_TARGET }, { "konter", AI_PART_COUNTERS },
		{ "ueberfall", AI_PART_RAIDS }, { "gefecht", AI_PART_MICRO }, { "superwaffe", AI_PART_SUPERWEAPON },
		{ "wachstum", AI_PART_GROWTH }, { "haushalt", AI_PART_BUDGET }, { "fabriken", AI_PART_FACTORIES },
		{ "fruehproduktion", AI_PART_EARLY_PRODUCTION }, { "fruehangriff", AI_PART_EARLY_ATTACK },
		{ "verteidigung", AI_PART_DEFENCE }, { "aufklaerung", AI_PART_SCOUTING }
	};
	UnsignedInt bits = 0;
	std::string list = text;
	size_t start = 0;
	while (start <= list.size())
	{
		size_t end = list.find( ',', start );
		if (end == std::string::npos)
			end = list.size();
		const std::string name = list.substr( start, end - start );
		if (!name.empty())
		{
			UnsignedInt bit = AI_PART_UNKNOWN;
			for (const auto &p : parts)
			{
				if (name == p.name)
					bit = p.part;
			}
			bits |= bit;
		}
		start = end + 1;
	}
	return bits;
}

// -aiOhne prognose,erstab,...: switch parts of the new AI off (AIPart).
Int parseAIOhne(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_aiPartsOff |= parseAIPartList( args[1] );
	return 2;
}

// -aiMit fabriken,...: switch parts of the new AI on that are off by default (AI_PARTS_OFF_BY_DEFAULT).
Int parseAIMit(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		const UnsignedInt bits = parseAIPartList( args[1] );
		TheWritableGlobalData->m_aiPartsOff &= ~(bits & ~AI_PART_UNKNOWN);
		TheWritableGlobalData->m_aiPartsOff |= bits & AI_PART_UNKNOWN;
	}
	return 2;
}

// -noDeadlockFix: without the new path for vehicles that have been blocked by moving vehicles for seconds (EA behavior, for
// comparisons and for replays recorded without the fix).
Int parseNoDeadlockFix(char *args[], int num)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_blockedDeadlockFix = FALSE;
	return 1;
}

// -deadlockFixFrom N: the deadlock fix applies from logic frame N on (a replay recorded without it stays in sync until then).
Int parseDeadlockFixFrom(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		Int n = atoi(args[1]);
		TheWritableGlobalData->m_blockedDeadlockFixFrame = (n < 0) ? 0 : static_cast<UnsignedInt>(n);
	}
	return 2;
}

Int parseSkirmishSeed(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_autoSkirmishSeed = atoi(args[1]);
	return 2;
}

// -startkapital N: starting cash of the auto skirmish (-skirmish).
Int parseStartkapital(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		const Int cash = atoi(args[1]);
		TheWritableGlobalData->m_autoSkirmishStartingCash = (cash < 0) ? 0 : cash;
	}
	return 2;
}

// -superwaffenlimit: the auto skirmish (-skirmish) limits superweapons (one per player).
Int parseSuperwaffenlimit(char *args[], int num)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_autoSkirmishSuperweaponLimit = TRUE;
	return 1;
}

// -schonzeit N: start protection of the auto skirmish (-skirmish) in minutes.
Int parseSchonzeit(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		const Int minutes = atoi(args[1]);
		TheWritableGlobalData->m_autoSkirmishGraceMinutes = (minutes < 0) ? 0 : ((minutes > 60) ? 60 : minutes);
	}
	return 2;
}

Int parseRecordReplay(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_recordReplayFile = args[1];
	return 2;
}

// -workers N sets the number of job workers. 0 = serial mode (reference for determinism tests),
// negative = automatic (logical processors - 2).
Int parseWorkers(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_workerThreads = atoi(args[1]);
	return 2;
}

// -parallelCRC computes the object checksum in parallel blocks. This changes the CRC value, hence a
// separate switch: with and without it the reference replays must each stay stable.
Int parseParallelCRC(char *args[], int)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_parallelCRC = TRUE;
	return 1;
}

// -verifyParallel additionally runs every parallel section serially and compares. Takes about twice
// the time; meant for test and long runs.
Int parseVerifyParallel(char *args[], int)
{
	::Engine::JobSystem::get().setVerifyParallel(true);
	return 1;
}

// -crcReport <file> <frame> writes a state report in this logic frame (checksum per subsystem and per
// object). tools/determinism/compare_crc.py compares two reports. On a replay desync the report is
// written automatically anyway (DesyncReport-<frame>.txt).
Int parseCRCReport(char *args[], int num)
{
	if (TheWritableGlobalData && num > 2)
	{
		TheWritableGlobalData->m_crcReportFile = args[1];
		TheWritableGlobalData->m_crcReportFrame = static_cast<UnsignedInt>(atoi(args[2]));
	}
	return 3;
}

// -affinity N restricts the process to the first N logical processors (e.g. -affinity 8 to measure
// 4 cores/8 threads on larger machines). Applied in GameEngine::execute.
Int parseAffinity(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_affinityLogicalProcessors = atoi(args[1]);
	return 2;
}

//=============================================================================
// Image comparison and render baseline, in all build configurations. Tools:
// tools/render/run_render_test.py and compare_shots.py.
//=============================================================================
// -shotAfter N: backbuffer screenshot after N client frames (repeatable), file shot_NNNNNN.bmp.
Int parseShotAfter(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1 && TheWritableGlobalData->m_shotAfterCount < GlobalData::MAX_SHOT_FRAMES)
	{
		Int frame = atoi(args[1]);
		if (frame > 0)
			TheWritableGlobalData->m_shotAfterFrames[TheWritableGlobalData->m_shotAfterCount++] = static_cast<UnsignedInt>(frame);
	}
	return 2;
}

// -shotDir <folder>: target folder of the screenshots (default: working directory, i.e. the exe folder).
Int parseShotDir(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_shotDir = args[1];
	return 2;
}

// -shotEvery N: in a running game a backbuffer screenshot every N logic frames, file shot_NNNNNN.bmp
// named after the logic frame. Meant for camera paths (-cameraPath).
Int parseShotEvery(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_shotEvery = static_cast<UnsignedInt>(atoi(args[1]));
	return 2;
}

// -shotEveryDraw N: a screenshot every N drawn frames, counted over the whole process (menus and shell map
// included; the client frame starts again with every game). Files draw_NNNNNN.bmp.
Int parseShotEveryDraw(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_shotEveryDraw = static_cast<UnsignedInt>(atoi(args[1]));
	return 2;
}

// -logActions: write the place of every destroyed object and every new particle system to the runner log
// (-crcLog), see GameEngine_LogAction. tools/render/find_action.py turns that into a camera path.
Int parseLogActions(char *args[], int num)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_logActions = TRUE;
	return 1;
}

// -cameraPath <file>: camera path for image comparisons, format in GameClient/CameraPath.cpp.
Int parseCameraPath(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_cameraPathFile = args[1];
	return 2;
}

// -testablauf <file>: script for unattended tests, format in GameClient/TestScript.cpp.
Int parseTestScript(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_testScriptFile = args[1];
	return 2;
}

// -quitAfter N: quit after N client frames, also in the main menu and on the shell map.
Int parseQuitAfter(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_quitAfterFrames = static_cast<UnsignedInt>(atoi(args[1]));
	return 2;
}

// -fixedTimeStep: one tick per loop pass without waiting, virtual render clock, fixed client seed.
// Two runs of the same scene thus produce the same frames.
Int parseFixedTimeStep(char *args[], int)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_fixedTimeStep = TRUE;
	return 1;
}

// -noVSync: present without vertical sync (performance measurement), overrides Options.ini.
Int parseNoVSync(char *args[], int)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_noVSync = TRUE;
	return 1;
}

// -logCursor <file> writes every cursor change and every time the hardware cursor image is set again, with real time,
// plus a summary per second (drawn frames, setCursor calls, changes, image sets) (W3DMouse.cpp).
Int parseLogCursor(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_cursorLogFile = args[1];
	return 2;
}

// -logRenderTime <file> writes tick, alpha and presentation time of each shown frame as CSV
// (GameEngine.cpp, logRenderTime; evaluated by tools/render/rendertime_check.py).
Int parseLogRenderTime(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_renderTimeLogFile = args[1];
	return 2;
}

// -logTextureStats N: every N client frames a TEXSTATS line (loaded textures, memory) to the runner
// log (-crcLog), plus TERRAINATLAS and LOADTIME.
Int parseLogTextureStats(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		const Int frames = atoi(args[1]);
		TheWritableGlobalData->m_logTextureStats = (frames > 0) ? static_cast<UnsignedInt>(frames) : 0;
	}
	return 2;
}

// -logDrawStats N: draw calls and triangles per drawn image to the runner log every N client frames.
Int parseLogDrawStats(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		const Int frames = atoi(args[1]);
		TheWritableGlobalData->m_logDrawStats = (frames > 0) ? static_cast<UnsignedInt>(frames) : 0;
	}
	return 2;
}

// -logUploads first last: hashes of the render backend's uploads and draws to the runner log; a summary line per
// drawn image in a game, one line per upload and draw for the logic frames first..last (last < first: summaries only).
Int parseLogUploads(char *args[], int num)
{
	if (TheWritableGlobalData && num > 2)
	{
		TheWritableGlobalData->m_logUploads = TRUE;
		TheWritableGlobalData->m_logUploadsFirst = static_cast<UnsignedInt>(atoi(args[1]));
		TheWritableGlobalData->m_logUploadsLast = static_cast<UnsignedInt>(atoi(args[2]));
	}
	return 3;
}

// -logCamera: per drawn image in a game a line CAMERA with the camera's clearance above the logic terrain (eye and
// near plane) to the runner log; negative values put the camera into the terrain.
Int parseLogCamera(char *args[], int num)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_logCamera = TRUE;
	return 1;
}

// -cameraClearance n: smallest height of the camera eye and its near plane above the terrain (default 2, 0 = off,
// as before: camera paths and scripts could put the camera into a slope).
Int parseCameraClearance(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		const Real f = static_cast<Real>(atof(args[1]));
		TheWritableGlobalData->m_cameraClearance = (f < 0.0f) ? 0.0f : ((f > 100.0f) ? 100.0f : f);
	}
	return 2;
}

// -fillUnwritten 0..255: the render backend presets memory it hands out unwritten (new buffers and textures,
// discarded mappings, constant windows past the data) with this byte. Makes reads of unwritten memory reproducible.
Int parseFillUnwritten(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		const Int fill = atoi(args[1]);
		TheWritableGlobalData->m_fillUnwritten = (fill >= 0 && fill <= 255) ? fill : -1;
	}
	return 2;
}

// -maxZoom <1..1.25>: limit of the player's free camera as a factor of MaxCameraHeight, overrides
// Settings.ini Camera.MaxZoomFactor; clamped like the setting.
Int parseMaxZoom(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		const Real factor = static_cast<Real>(atof(args[1]));
		if (factor > 0.0f)
			TheWritableGlobalData->m_maxZoomFactor = GlobalData::clampMaxZoomFactor(factor);
		else
			DEBUG_LOG(("-maxZoom %s: not a factor, switch ignored\n", args[1]));
	}
	return 2;
}

// -textureQuality auto|sd|hd|uhd: texture tier, overrides Options.ini "TextureQuality". The test
// runners set sd so reference images do not depend on the machine.
Int parseTextureQuality(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		const Int tier = ::Engine::parseTextureTier(args[1]);
		if (tier == ::Engine::TEXTURE_TIER_INVALID)
			DEBUG_LOG(("-textureQuality %s: unknown (auto, sd, hd, uhd), switch ignored\n", args[1]));
		else
			TheWritableGlobalData->m_textureQuality = tier;
	}
	return 2;
}

// -gamma <0..100>: brightness slider position, overrides Options.ini "Gamma". The image tests set 50
// (neutral) so their references do not depend on the user's slider, which under D3D11 also applies
// in windowed mode.
Int parseGamma(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		const Int v = atoi(args[1]);
		if (v < 0 || v > 100)
			DEBUG_LOG(("-gamma %s: allowed are 0 to 100, switch ignored\n", args[1]));
		else
		{
			char text[16];
			snprintf(text, sizeof(text), "%d", v);
			UserSettings::setCommandLine("Display.Gamma", text);
			TheWritableGlobalData->m_displayGamma = GlobalData::gammaFromSlider(v);
		}
	}
	return 2;
}

// -waterQuality 0|1|2: water level, overrides the detail level (0 = EA water, 1 = turbidity and
// Fresnel, 2 = plus refraction). For image comparisons.
Int parseWaterQuality(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		const Int q = atoi(args[1]);
		if (q < 0 || q > 2)
			DEBUG_LOG(("-waterQuality %s: allowed are 0, 1 and 2, switch ignored\n", args[1]));
		else
		{
			char text[16];
			snprintf(text, sizeof(text), "%d", q);
			UserSettings::setCommandLine("Detail.WaterQuality", text);
			TheWritableGlobalData->m_waterQuality = q;
		}
	}
	return 2;
}

// -waterTexture ea|free: color texture of the water, overrides Options.ini "WaterTexture" (ea = from the
// game data, free = the CC0 tiles in ProjectData\Textures\Wasser, the default).
Int parseWaterTexture(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		if (stricmp(args[1], "free") == 0)
			TheWritableGlobalData->m_waterTexture = 1;
		else if (stricmp(args[1], "ea") == 0)
			TheWritableGlobalData->m_waterTexture = 0;
		else
			DEBUG_LOG(("-waterTexture %s: allowed are ea and free, switch ignored\n", args[1]));
	}
	return 2;
}

// -freeContent ea|free: free content packages in ProjectData\Inhalt (terrain, roads), overrides Options.ini
// "FreeContent" (ea = game data only, free = the packages on top of the texture tiers, the default).
Int parseFreeContent(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		if (stricmp(args[1], "free") == 0)
			TheWritableGlobalData->m_freeContent = 1;
		else if (stricmp(args[1], "ea") == 0)
			TheWritableGlobalData->m_freeContent = 0;
		else
			DEBUG_LOG(("-freeContent %s: allowed are ea and free, switch ignored\n", args[1]));
	}
	return 2;
}

// -terrainAnisotropy n (0 = trilinear as in EA, 2..16) overrides the detail level, for old-vs-new
// image comparisons.
Int parseTerrainAnisotropy(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		Int a = atoi(args[1]);
		if (a < 0)
			a = 0;
		if (a > 16)
			a = 16;
		char text[16];
		snprintf(text, sizeof(text), "%d", a);
		UserSettings::setCommandLine("Detail.TerrainAnisotropy", text);
		TheWritableGlobalData->m_terrainAnisotropy = a;
	}
	return 2;
}

// -texLodBias f: mip LOD bias of the terrain textures (default 0, negative = sharper, limited to -2..1).
// Pairs with -terrainAnisotropy; without anisotropy a negative bias shimmers at flat camera angles.
Int parseTexLodBias(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		Real b = static_cast<Real>(atof(args[1]));
		if (b < -2.0f)
			b = -2.0f;
		if (b > 1.0f)
			b = 1.0f;
		TheWritableGlobalData->m_terrainLodBias = b;
	}
	return 2;
}

// -renderScale f: share of the window resolution the 3D scene is drawn in (0.5..2 or 50..200 percent), scaled
// up (below 1) or down (above 1, supersampling) before the 2D interface. Overrides the detail level and
// Settings.ini "RenderScale"; 1 = full resolution.
Int parseRenderScale(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		const Real s = GlobalData::normalizeRenderScale(static_cast<Real>(atof(args[1])));
		char text[32];
		snprintf(text, sizeof(text), "%g", s);
		UserSettings::setCommandLine("Display.RenderScale", text);
		TheWritableGlobalData->m_renderScale = s;
	}
	return 2;
}

// -renderScaleFilter bilinear|bicubic|cas: filter of the upscale from -renderScale (default bicubic).
Int parseRenderScaleFilter(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		const Int f = GlobalData::parseRenderScaleFilter(args[1]);
		if (f >= 0)
		{
			TheWritableGlobalData->m_renderScaleFilter = f;
			TheWritableGlobalData->m_renderScaleFilterFromCommandLine = TRUE;
		}
	}
	return 2;
}

// -renderScaleSharpen f: sharpness of the cas upscale (0..1, default 0.5).
Int parseRenderScaleSharpen(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		Real s = static_cast<Real>(atof(args[1]));
		TheWritableGlobalData->m_renderScaleSharpen = (s < 0.0f) ? 0.0f : ((s > 1.0f) ? 1.0f : s);
	}
	return 2;
}

// -hdrKnee <0.6..0.95>, -hdrNight <0.5..2>, -hdrAdditive <0..1>: tone curve of the effect hdr (knee, exposure on
// night maps, factor for additively blended draws such as fire and explosions). Defaults 0.8, 1 and 1.
static Real clampedArg(const char *arg, Real lo, Real hi)
{
	const Real v = static_cast<Real>(atof(arg));
	return (v < lo) ? lo : ((v > hi) ? hi : v);
}
Int parseHdrKnee(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_hdrKnee = clampedArg(args[1], 0.6f, 0.95f);
	return 2;
}
Int parseHdrNight(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_hdrNight = clampedArg(args[1], 0.5f, 2.0f);
	return 2;
}
Int parseHdrAdditive(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_hdrAdditive = clampedArg(args[1], 0.0f, 1.0f);
	return 2;
}

// -sceneFormat rgba16f|rgba16|r11g11b10|bgra8: format of the 3D scene target (default bgra8). rgba16f is the base
// for HDR; rgba16 (UNORM) clamps like bgra8 and serves as the comparison for it; r11g11b10 halves the bandwidth of
// rgba16f and keeps the destination alpha in a target of its own. The image leaves the scene through an 8-bit
// conversion pass.
Int parseSceneFormat(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		static const char* const names[] = { "bgra8", "rgba16f", "rgba16", "r11g11b10" };
		TheWritableGlobalData->m_sceneFormat = 0;
		for (Int i = 0; i < 4; ++i)
			if (stricmp(args[1], names[i]) == 0) TheWritableGlobalData->m_sceneFormat = i;
	}
	return 2;
}

// -gpuTiming: GPU timestamps (3D scene, upscale, whole frame) also without effects; GPUEFFEKTE line at the end.
Int parseGpuTiming(char *args[], int num)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_gpuTiming = TRUE;
	return 1;
}

// -terrainTessMax n: largest tessellation factor of the terrain (1..64, default 16), for measurements.
Int parseTerrainTessMax(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		Real f = static_cast<Real>(atof(args[1]));
		TheWritableGlobalData->m_terrainTessMax = (f < 1.0f) ? 1.0f : ((f > 64.0f) ? 64.0f : f);
	}
	return 2;
}

// -terrainTessPixels n: target screen length of a tessellated terrain edge piece (2..64 pixels, default 8).
Int parseTerrainTessPixels(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		Real p = static_cast<Real>(atof(args[1]));
		TheWritableGlobalData->m_terrainTessPixels = (p < 2.0f) ? 2.0f : ((p > 64.0f) ? 64.0f : p);
	}
	return 2;
}

// -terrainTessProbe: crack probe of the terrain tessellation (tessellated terrain green, scene cleared to red,
// displacement eight times stronger).
Int parseTerrainTessProbe(char *args[], int)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_terrainTessProbe = 1;
	return 1;
}

// Render backend for this run, over Settings.ini [Display] RenderBackend: -vulkan asks for Vulkan
// (Direct3D 11 starts if it cannot be used, W3DDisplay reports why), -d3d11 for Direct3D 11. -d3d9 is
// still accepted so W3DDisplay can report it as unsupported. The last switch wins.
static void setRenderBackend(Bool d3d9, Bool vulkan)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_renderBackendD3D9Requested = d3d9;
	UserSettings::setCommandLine("Display.RenderBackend", vulkan ? "vulkan" : "d3d11");
}

Int parseD3D9(char *args[], int)
{
	setRenderBackend(TRUE, FALSE);
	return 1;
}

Int parseD3D11(char *args[], int)
{
	setRenderBackend(FALSE, FALSE);
	return 1;
}

Int parseVulkan(char *args[], int)
{
	setRenderBackend(FALSE, TRUE);
	return 1;
}

// -depthD32: scene depth as 32-bit float in both backends, the format Vulkan has to use where there is no
// D24S8 (AMD); for comparing Direct3D 11 and Vulkan with the same depth format.
Int parseDepthD32(char *args[], int)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_depthFloat = TRUE;
	return 1;
}

// Network emulator for received game and lobby packets (Transport, Engine::NetEmulator), and a LAN
// on loopback addresses so several instances (-mehrfach) play a LAN game on one machine.
static Int parseNetInt(char *args[], int num, Int GlobalData::*field)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->*field = atoi(args[1]);
	return 2;
}
Int parseNetLatency(char *args[], int num) { return parseNetInt(args, num, &GlobalData::m_netEmuLatency); }
Int parseNetJitter(char *args[], int num) { return parseNetInt(args, num, &GlobalData::m_netEmuJitter); }
Int parseNetLoss(char *args[], int num) { return parseNetInt(args, num, &GlobalData::m_netEmuLoss); }
Int parseNetDuplicate(char *args[], int num) { return parseNetInt(args, num, &GlobalData::m_netEmuDuplicate); }
Int parseNetReorder(char *args[], int num) { return parseNetInt(args, num, &GlobalData::m_netEmuReorder); }
// -msaa n: multisampling with n samples (0 or 1 = off), over Settings.ini and the effect list; leaves the effects alone.
// Test runs set it so their images do not depend on the user's settings.
Int parseMsaa(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		const Int n = atoi(args[1]);
		TheWritableGlobalData->m_msaaSamples = (n < 0) ? 0 : ((n > 16) ? 16 : n);
	}
	return 2;
}

// -einstellungenNurLesen: the settings files are read but not written (tests on the user's machine).
Int parseSettingsReadOnly(char *[], int)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_settingsReadOnly = TRUE;
	return 1;
}
Int parseNetLocal(char *args[], int num) { return parseNetInt(args, num, &GlobalData::m_netLocalInstance); }
Int parseDesyncReportFrame(char *args[], int num) { return parseNetInt(args, num, &GlobalData::m_desyncReportFrame); }
Int parseWatchTest(char *args[], int num) { return parseNetInt(args, num, &GlobalData::m_watchTestStart); }
Int parseMonitor(char *args[], int num) { return parseNetInt(args, num, &GlobalData::m_monitorIndex); }
Int parseNoRealInput(char *[], int)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_noRealInput = TRUE;
	return 1;
}
Int parseNetNoPortMapping(char *[], int)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_netNoPortMapping = TRUE;
	return 1;
}
// Rendezvous servers for games over the internet; -treffpunkt may be given several times.
Int parseRendezvous(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		if (!TheWritableGlobalData->m_netRendezvous.isEmpty())
			TheWritableGlobalData->m_netRendezvous.concat(';');
		TheWritableGlobalData->m_netRendezvous.concat(args[1]);
	}
	return 2;
}
Int parseNoRendezvous(char *[], int)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_netNoRendezvous = TRUE;
	return 1;
}
// -netzNurRelais: with a rendezvous server (-treffpunkt) every way between the game sockets uses the relay, no direct path
Int parseNetForceRelay(char *[], int)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_netForceRelay = TRUE;
	return 1;
}
Int parseNetSeed(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_netEmuSeed = static_cast<UnsignedInt>(strtoul(args[1], nullptr, 10));
	return 2;
}

// -testhilfen enables the test helpers (effective in single player only); -testhilfenAblauf <file>
// triggers them at fixed logic frames (replay test, also enables them); -mission <campaign> <nr> starts
// a campaign mission without menu. GameEngine::init sets the map once the campaigns are loaded; until
// then a placeholder holds the shell.
// -audioLog: audio diagnostics (requests, culls, voices, music tracks) into the -crcLog file.
Int parseAudioLog(char *args[], int)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_audioLog = TRUE;
	return 1;
}

Int parseTesthilfen(char *args[], int)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_testAids = TRUE;
	return 1;
}

Int parseTestAidsScript(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_testAids = TRUE;
		TheWritableGlobalData->m_testAidsScript = args[1];
	}
	return 2;
}

Int parseMission(char *args[], int num)
{
	if (TheWritableGlobalData && num > 2)
	{
		TheWritableGlobalData->m_testAidsMission = args[1];
		TheWritableGlobalData->m_testAidsMissionNr = atoi(args[2]);
		TheWritableGlobalData->m_initialFile = "mission";	// placeholder, GameEngine::init sets the map
		skipIntroAndShellMap();
	}
	return 3;
}

// -effects <list> (also -d3d11Effects, the older name) selects the render effects (fxaa, sharpen[=0..1], softparticles, ssao,
// colorgrade, bloom[=0..2], heat[=0..2], haze[=0..1], lights[=0..2], particlelight[=0..1], particleground,
// terrainnormal[=0..2], terraintess[=0..2], rocktess[=0..2], alphacoverage, softshadows[=0..2],
// contactshadows, shadowcascades, msaadepth, hdr, deband, waterssr, gtao, taa, flatsnow[=0..1], all, none; msaa=n for comparisons) and overrides Options.ini "D3D11Effects". "none"
// gives the plain EA image.
Int parseD3D11Effects(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		GlobalData::parseRenderEffects(args[1], &TheWritableGlobalData->m_renderEffects,
			&TheWritableGlobalData->m_renderEffectSharpen, &TheWritableGlobalData->m_renderEffectMsaa,
			0, &TheWritableGlobalData->m_renderEffectShadowMapSize,	// shadowmap[=n]
			&TheWritableGlobalData->m_renderEffectShadowTerrain,	// shadowterrain=x
			&TheWritableGlobalData->m_renderEffectTerrainDetail,	// terraindetail[=x]
			&TheWritableGlobalData->m_renderEffectTerrainRock,	// terrainrock[=x]
			&TheWritableGlobalData->m_renderEffectBloom,			// bloom[=x]
			&TheWritableGlobalData->m_renderEffectHeat,			// heat[=x]
			&TheWritableGlobalData->m_renderEffectHaze,			// haze[=x]
			&TheWritableGlobalData->m_renderEffectLights,		// lights[=x]
			&TheWritableGlobalData->m_renderEffectParticleLight,	// particlelight[=x]
			&TheWritableGlobalData->m_renderEffectTerrainNormal,	// terrainnormal[=x]
			&TheWritableGlobalData->m_renderEffectTerrainTess,	// terraintess[=x]
			&TheWritableGlobalData->m_renderEffectRockTess,		// rocktess[=x]
			&TheWritableGlobalData->m_renderEffectShadowSoft,	// softshadows[=x]
			&TheWritableGlobalData->m_renderEffectFlatSnow,		// flatsnow[=x]
			&TheWritableGlobalData->m_renderEffectHeightFog,	// heightfog[=x]
			&TheWritableGlobalData->m_renderEffectSunShafts);	// sunshafts[=x]
		TheWritableGlobalData->m_renderEffectsFromCommandLine = TRUE;
		UserSettings::setCommandLine("Detail.D3D11Effects", args[1]);
	}
	return 2;
}

// -cpuTerrainLights: terrain prelighting and dynamic terrain lights on the CPU (in the terrain vertex
// buffers) instead of the vertex shader; for comparisons.
Int parseCpuTerrainLights(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_cpuTerrainLights = TRUE;
	}
	return 1;
}

// -cpuSkinning: skinned meshes (infantry, animated models) deformed on the CPU instead of the vertex
// shader; for comparisons.
Int parseCpuSkinning(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_cpuSkinning = TRUE;
	}
	return 1;
}

// -cpuParticles: particle quads built on the CPU (and sorted as triangles) instead of the vertex shader;
// for comparisons.
Int parseCpuParticles(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_cpuParticles = TRUE;
	}
	return 1;
}

// -simThread: the logic tick of a running game on the simulation thread; the main thread waits for it.
Int parseSimThread(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_simThread = TRUE;
	}
	return 1;
}

// -noMeshInstancing: every mesh drawn on its own instead of instanced draws of the same model; for comparisons.
Int parseNoMeshInstancing(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_noMeshInstancing = TRUE;
	}
	return 1;
}

// -noConstantBufferRing: constant buffers mapped with DISCARD per draw instead of the Direct3D 11.1 ring with
// offsets; for comparisons.
Int parseNoConstantBufferRing(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_noConstantBufferRing = TRUE;
	}
	return 1;
}

static CommandLineParam params[] =
{
	{ "-noshellmap", parseNoShellMap },
	{ "-noConstantBufferRing", parseNoConstantBufferRing },
	{ "-cpuTerrainLights", parseCpuTerrainLights },
	{ "-cpuSkinning", parseCpuSkinning },
	{ "-cpuParticles", parseCpuParticles },
	{ "-noMeshInstancing", parseNoMeshInstancing },
	{ "-simThread", parseSimThread },
	// Runner and auto skirmish (all build configurations)
	{ "-replay", parseReplay },
	{ "-runner", parseRunner },
	{ "-crcLog", parseCRCLog },
	{ "-crcLogInterval", parseCRCLogInterval },
	{ "-deepCRCFrames", parseDeepCRCFrames },
	{ "-deepCRCFull", parseDeepCRCFull },
	{ "-maxFrames", parseMaxFrames },
	{ "-skirmish", parseSkirmish },
	{ "-skirmishAI", parseSkirmishAI },
	{ "-skirmishSeed", parseSkirmishSeed },
	{ "-schonzeit", parseSchonzeit },
	{ "-superwaffenlimit", parseSuperwaffenlimit },
	{ "-startkapital", parseStartkapital },
	{ "-aiStats", parseAIStats },
	{ "-aiOhne", parseAIOhne },
	{ "-aiMit", parseAIMit },
	{ "-noDeadlockFix", parseNoDeadlockFix },
	{ "-deadlockFixFrom", parseDeadlockFixFrom },
	{ "-recordReplay", parseRecordReplay },
	{ "-affinity", parseAffinity },
	{ "-audioLog", parseAudioLog },
	{ "-netzLatenz", parseNetLatency },
	{ "-netzSchwankung", parseNetJitter },
	{ "-netzVerlust", parseNetLoss },
	{ "-netzDoppelt", parseNetDuplicate },
	{ "-netzUmordnung", parseNetReorder },
	{ "-netzSaat", parseNetSeed },
	{ "-netzLokal", parseNetLocal },
	{ "-einstellungenNurLesen", parseSettingsReadOnly },
	{ "-msaa", parseMsaa },
	{ "-netzOhnePortfreigabe", parseNetNoPortMapping },
	{ "-treffpunkt", parseRendezvous },
	{ "-netzOhneTreffpunkt", parseNoRendezvous },
	{ "-netzNurRelais", parseNetForceRelay },
	{ "-desyncBerichtBei", parseDesyncReportFrame },
	{ "-zuschauerTest", parseWatchTest },
	{ "-bildschirm", parseMonitor },
	{ "-echteEingabeAus", parseNoRealInput },
	{ "-testhilfen", parseTesthilfen },
	{ "-testhilfenAblauf", parseTestAidsScript },
	{ "-mission", parseMission },
	// Image comparison and render baseline (all build configurations)
	{ "-shotAfter", parseShotAfter },
	{ "-shotDir", parseShotDir },
	{ "-shotEvery", parseShotEvery },
	{ "-shotEveryDraw", parseShotEveryDraw },
	{ "-cameraPath", parseCameraPath },
	{ "-logActions", parseLogActions },
	{ "-quitAfter", parseQuitAfter },
	{ "-testablauf", parseTestScript },
	{ "-fixedTimeStep", parseFixedTimeStep },
	{ "-noVSync", parseNoVSync },
	{ "-logRenderTime", parseLogRenderTime },
	{ "-logCursor", parseLogCursor },
	{ "-logTextureStats", parseLogTextureStats },
	{ "-logDrawStats", parseLogDrawStats },
	{ "-logUploads", parseLogUploads },
	{ "-fillUnwritten", parseFillUnwritten },
	{ "-logCamera", parseLogCamera },
	{ "-cameraClearance", parseCameraClearance },
	{ "-maxZoom", parseMaxZoom },
	{ "-textureQuality", parseTextureQuality },
	{ "-waterQuality", parseWaterQuality },
	{ "-waterTexture", parseWaterTexture },
	{ "-freeContent", parseFreeContent },
	{ "-terrainAnisotropy", parseTerrainAnisotropy },
	{ "-texLodBias", parseTexLodBias },
	{ "-renderScale", parseRenderScale },
	{ "-renderScaleFilter", parseRenderScaleFilter },
	{ "-renderScaleSharpen", parseRenderScaleSharpen },
	{ "-sceneFormat", parseSceneFormat },
	{ "-hdrKnee", parseHdrKnee },
	{ "-hdrNight", parseHdrNight },
	{ "-hdrAdditive", parseHdrAdditive },
	{ "-gpuTiming", parseGpuTiming },
	{ "-terrainTessMax", parseTerrainTessMax },
	{ "-terrainTessPixels", parseTerrainTessPixels },
	{ "-terrainTessProbe", parseTerrainTessProbe },
	{ "-gamma", parseGamma },					// brightness slider 0..100
	{ "-vorlagenAusgeben", parseGameDataDump },
	{ "-staticLOD", parseStaticLOD },
	{ "-nologo", parseNoLogo },	// also in release builds
	// Render backend (all build configurations)
	{ "-d3d9", parseD3D9 },
	{ "-d3d11", parseD3D11 },
	{ "-vulkan", parseVulkan },
	{ "-depthD32", parseDepthD32 },
	{ "-effects", parseD3D11Effects },
	{ "-d3d11Effects", parseD3D11Effects },
	{ "-workers", parseWorkers },
	{ "-parallelCRC", parseParallelCRC },
	{ "-crcReport", parseCRCReport },
	{ "-verifyParallel", parseVerifyParallel },
	{ "-win", parseWin },
	{ "-borderless", parseBorderless },
	{ "-fullscreenArea", parseFullscreenArea },
	{ "-xres", parseXRes },
	{ "-yres", parseYRes },
	{ "-fullscreen", parseNoWin },
	{ "-fullVersion", parseFullVersion },
	{ "-scriptDebug", parseScriptDebug },
	{ "-playStats", parsePlayStats },
	{ "-mod", parseMod },
	{ "-baseGame", parseBaseGame },
	{ "-benutzerdaten", parseUserDataFolder },
	{ "-dateiIndexAusgeben", parseIndexDump },
	{ "-modellPruefung", parseModelCheck },
	{ "-noshaders", parseNoShaders },
	{ "-quickstart", parseQuickStart },

#if defined(_DEBUG)
	{ "-noaudio", parseNoAudio },
	{ "-map", parseMapName },
	{ "-nomusic", parseNoMusic },
	{ "-novideo", parseNoVideo },
	{ "-noLogOrCrash", parseNoLogOrCrash },
	{ "-FPUPreserve", parseFPUPreserve },
	{ "-benchmark", parseBenchmark },
#ifdef DUMP_PERF_STATS
	{ "-stats", parseStats }, 
#endif
  { "-saveStats", parseSaveStats },
	{ "-localMOTD", parseLocalMOTD },
	{ "-UseCSF", parseUseCSF },
	{ "-NoInputDisable", parseNoInputDisable },
	{ "-DebugCRCFromFrame", parseDebugCRCFromFrame },
	{ "-DebugCRCUntilFrame", parseDebugCRCUntilFrame },
	{ "-KeepCRCSaves", parseKeepCRCSave },
	{ "-CRCLogicModuleData", parseCRCLogicModuleData },
	{ "-CRCClientModuleData", parseCRCClientModuleData },
	{ "-ClientDeepCRC", parseClientDeepCRC },
	{ "-VerifyClientCRC", parseVerifyClientCRC },
	{ "-LogObjectCRCs", parseLogObjectCRCs },
	{ "-saveAllStats", parseSaveAllStats },
	{ "-NetCRCInterval", parseNetCRCInterval },
	{ "-ReplayCRCInterval", parseReplayCRCInterval },
	{ "-noDraw", parseNoDraw },
	{ "-nomilcap", parseNoMilCap },
	{ "-nofade", parseNoFade },
	{ "-nomovecamera", parseNoMoveCamera },
	{ "-nocinematic", parseNoCinematic },
	{ "-noViewLimit", parseNoViewLimit },
	{ "-lowDetail", parseLowDetail },
	{ "-noDynamicLOD", parseNoDynamicLOD },
	{ "-noStaticLOD", parseNoStaticLOD },
	{ "-useWaveEditor", parseUseWaveEditor },
	{ "-fps", parseFPSLimit },
	{ "-wireframe", parseWireframe },
	{ "-showCollision", parseShowCollision },
	{ "-noShowClientPhysics", parseNoShowClientPhysics },
	{ "-showTerrainNormals", parseShowTerrainNormals },
	{ "-stateMachineDebug", parseStateMachineDebug },
	{ "-jabber", parseJabber },
	{ "-munkee", parseMunkee },
	{ "-displayDebug", parseDisplayDebug },
	{ "-file", parseFile },
  
//	{ "-preload", parsePreload },
	
  { "-preloadEverything", parsePreloadEverything },
	{ "-logAssets", parseLogAssets },
	{ "-netMinPlayers", parseNetMinPlayers },
	{ "-DemoLoadScreen", parseDemoLoadScreen },
	{ "-cameraDebug", parseCameraDebug },
	{ "-ignoreAsserts", parseIgnoreAsserts },
	{ "-ignoreStackTrace", parseIgnoreStackTrace },
	{ "-logToCon", parseLogToConsole },
	{ "-selectTheUnselectable", parseSelectAll },
	{ "-RunAhead", parseRunAhead },
	{ "-noshroud", parseNoShroud },
	{ "-setDebugLevel", parseSetDebugLevel },
	{ "-clearDebugLevel", parseClearDebugLevel },
	{ "-buildmapcache", parseBuildMapCache },
	{ "-noshadowvolumes", parseNoShadows },
	{ "-nofx", parseNoFX },
	{ "-ignoresync", parseSync },
	{ "-shellmap", parseShellMap },
	{ "-noShellAnim", parseNoWindowAnimation },
	{ "-winCursors", parseWinCursors },
	{ "-constantDebug", parseConstantDebug },
	{ "-seed", parseSeed },
	{ "-noagpfix", parseIncrAGPBuf },
	{ "-noFPSLimit", parseNoFPSLimit },
	{ "-dumpAssetUsage", parseDumpAssetUsage },
	{ "-jumpToFrame", parseJumpToFrame },
	{ "-updateImages", parseUpdateImages },
	{ "-showTeamDot", parseShowTeamDot },
	{ "-extraLogging", parseExtraLogging },

#endif

	//-allAdvice feature
	//{ "-allAdvice", parseAllAdvice },

#if defined(_DEBUG)
  { "-preload", parsePreload },
#endif


};

#if !defined(_DEBUG)
// Switches that both games also offer in release builds (in debug builds they are in params[]).
// -nologo and -quickstart are listed above.
static CommandLineParam paramsGeneralsRelease[] =
{
	{ "-noaudio", parseNoAudio },
	{ "-map", parseMapName },
	{ "-nomusic", parseNoMusic },
	{ "-novideo", parseNoVideo },
	{ "-noLogOrCrash", parseNoLogOrCrash },
	{ "-FPUPreserve", parseFPUPreserve },
	{ "-buildmapcache", parseBuildMapCache },
	{ "-noshadowvolumes", parseNoShadows },
	{ "-nofx", parseNoFX },
	{ "-ignoresync", parseSync },
	{ "-shellmap", parseShellMap },
	{ "-noShellAnim", parseNoWindowAnimation },
	{ "-winCursors", parseWinCursors },
	{ "-constantDebug", parseConstantDebug },
	{ "-seed", parseSeed },
	{ "-noagpfix", parseIncrAGPBuf },
	{ "-noFPSLimit", parseNoFPSLimit },
	{ "-dumpAssetUsage", parseDumpAssetUsage },
	{ "-jumpToFrame", parseJumpToFrame },
	{ "-updateImages", parseUpdateImages },
	{ "-showTeamDot", parseShowTeamDot },
};
#endif

// parseCommandLine ===========================================================
/** Parse command-line parameters. */
//=============================================================================
void parseCommandLine(int argc, char *argv[])
{
	// To parse command-line parameters, we loop through a table holding arguments
	// and functions to handle them.  Comparisons can be case-(in)sensitive, and
	// can check the entire string (for testing the presence of a flag) or check
	// just the start (for a key=val argument).  The handling function can also
	// look at the next argument(s), to accomodate multi-arg parameters, e.g. "-p 1234".
	int arg=1, param;
	Bool found;

#ifdef DEBUG_LOGGING
	DEBUG_LOG(("Command-line args:"));
	int debugFlags = DebugGetFlags();
	DebugSetFlags(debugFlags & ~DEBUG_FLAG_PREPEND_TIME); // turn off timestamps
	for (arg=1; arg<argc; arg++)
	{
		DEBUG_LOG((" %s", argv[arg]));
	}
	DEBUG_LOG(("\n"));
	DebugSetFlags(debugFlags); // turn timestamps back on iff they were on before
	arg = 1;
#endif // DEBUG_LOGGING

	while (arg<argc)
	{
		// Look at arg #i
		found = false;
		for (param=0; !found && param<sizeof(params)/sizeof(params[0]); ++param)
		{
			int len = static_cast<Int>(strlen(params[param].name));
			int len2 = static_cast<Int>(strlen(argv[arg]));
			if (len2 != len)
				continue;
			if (!strnicmp(argv[arg], params[param].name, len))
			{
				arg += params[param].func(argv+arg, argc-arg);
				found = true;
			}
		}	// for
#if !defined(_DEBUG)
		// Release switches from Generals, available in both games
		for (param=0; !found && param<sizeof(paramsGeneralsRelease)/sizeof(paramsGeneralsRelease[0]); ++param)
		{
			int len = static_cast<int>(strlen(paramsGeneralsRelease[param].name));
			int len2 = static_cast<int>(strlen(argv[arg]));
			if (len2 != len)
				continue;
			if (!strnicmp(argv[arg], paramsGeneralsRelease[param].name, len))
			{
				arg += paramsGeneralsRelease[param].func(argv+arg, argc-arg);
				found = true;
			}
		}	// for
#endif
		if (!found)
		{
			arg++;
		}
	}

	// Choose the texture tier and load its archives with priority. Done here because only now are
	// TheGlobalData (resolution, Options.ini) and -textureQuality known. The free content goes above the
	// tiers (it replaces their textures), the mods above everything.
	TheArchiveFileSystem->loadTextureTiers();
	TheArchiveFileSystem->loadFreeContent();
	TheArchiveFileSystem->loadMods();
	TheArchiveFileSystem->checkTexturePackageSources();
	if (!s_indexDumpPath.empty())
	{
		TheArchiveFileSystem->writeIndexDump(s_indexDumpPath.c_str());
		s_indexDumpPath.clear();
		s_indexDumpPath.shrink_to_fit();	// back to the inline buffer; a swap would bring a new debug proxy block
	}
}


