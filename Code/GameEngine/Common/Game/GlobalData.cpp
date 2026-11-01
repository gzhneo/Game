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

// FILE: GlobalData.cpp ///////////////////////////////////////////////////////////////////////////
// The GameLogicData object
// Author: trolfs, Michael Booth, Colin Day, April 2001
///////////////////////////////////////////////////////////////////////////////////////////////////

//#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#define DEFINE_TERRAIN_LOD_NAMES
#define DEFINE_TIME_OF_DAY_NAMES
#define DEFINE_WEATHER_NAMES
#define DEFINE_BODYDAMAGETYPE_NAMES
#define DEFINE_PANNING_NAMES

#include "formatinto.h"
#include "Common/crc.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/GameEngine.h"
#include "Common/GameAudio.h"
#include "Common/INI.h"
#include "Common/InstallInfo.h"
#include "Common/GameEdition.h"
#include "Common/LogicVersion.h"
#include "Common/UserSettings.h"
#include "Common/version.h"
#include "Engine/UserDataFiles.h"

#include "GameLogic/AI.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Module/BodyModule.h"

#include "GameClient/Color.h"
#include "GameClient/TerrainVisual.h"

#include "GameNetwork/FirewallHelper.h"

// PUBLIC DATA ////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
GlobalData* GlobalData::m_theOriginal = nullptr;

///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
/*static*/ const FieldParse GlobalData::s_GlobalDataFieldParseTable[] = 
{
	{ "Windowed",									INI::parseBool,				nullptr,			offsetof( GlobalData, m_windowed ) },
	{ "XResolution",							INI::parseInt,				nullptr,			offsetof( GlobalData, m_xResolution ) },
	{ "YResolution",							INI::parseInt,				nullptr,			offsetof( GlobalData, m_yResolution ) },
	{ "MapName",									INI::parseAsciiString,nullptr,			offsetof( GlobalData, m_mapName ) },
	{ "MoveHintName",							INI::parseAsciiString,nullptr,			offsetof( GlobalData, m_moveHintName ) },
	{ "UseTrees",									INI::parseBool,				nullptr,			offsetof( GlobalData, m_useTrees ) },
	{ "UseFPSLimit",							INI::parseBool,				nullptr,			offsetof( GlobalData, m_useFpsLimit ) },
	{ "DumpAssetUsage",						INI::parseBool,				nullptr,			offsetof( GlobalData, m_dumpAssetUsage ) },
	{ "FramesPerSecondLimit",			INI::parseInt,				nullptr,			offsetof( GlobalData, m_framesPerSecondLimit ) },
	{ "ChipsetType",							INI::parseInt,				nullptr,			offsetof( GlobalData, m_chipSetType ) },
	{ "MaxShellScreens",					INI::parseInt,				nullptr,			offsetof( GlobalData, m_maxShellScreens ) },
	{ "UseCloudMap",							INI::parseBool,				nullptr,			offsetof( GlobalData, m_useCloudMap ) },
	{ "UseLightMap",							INI::parseBool,				nullptr,			offsetof( GlobalData, m_useLightMap ) },
	{ "BilinearTerrainTex",				INI::parseBool,				nullptr,			offsetof( GlobalData, m_bilinearTerrainTex ) },
	{ "TrilinearTerrainTex",			INI::parseBool,				nullptr,			offsetof( GlobalData, m_trilinearTerrainTex ) },
	{ "MultiPassTerrain",					INI::parseBool,				nullptr,			offsetof( GlobalData, m_multiPassTerrain ) },
	{ "AdjustCliffTextures",			INI::parseBool,				nullptr,			offsetof( GlobalData, m_adjustCliffTextures ) },
	{ "Use3WayTerrainBlends",			INI::parseInt,				nullptr,			offsetof( GlobalData, m_use3WayTerrainBlends ) },
	{ "StretchTerrain",						INI::parseBool,				nullptr,			offsetof( GlobalData, m_stretchTerrain ) },
	{ "UseHalfHeightMap",					INI::parseBool,				nullptr,			offsetof( GlobalData, m_useHalfHeightMap ) },
	{ "UserDataLeafName",					INI::parseQuotedAsciiString,	nullptr,			offsetof( GlobalData, m_userDataLeafName ) },
	{ "EMPSparesAlliedTransports",	INI::parseBool,				nullptr,			offsetof( GlobalData, m_empSparesAlliedTransports ) },
	{ "WeaponChoiceLikeGenerals",		INI::parseBool,				nullptr,			offsetof( GlobalData, m_weaponChoiceLikeGenerals ) },
	{ "WeaponBonusRestartsReload",	INI::parseBool,				nullptr,			offsetof( GlobalData, m_weaponBonusRestartsReload ) },
	{ "DronesNeverSelectable",			INI::parseBool,				nullptr,			offsetof( GlobalData, m_dronesNeverSelectable ) },
	{ "GroupMovesClampedToMap",		INI::parseBool,				nullptr,			offsetof( GlobalData, m_groupMovesClampedToMap ) },
	{ "BattlePlanRevealsStrategyCenter",	INI::parseBool,	nullptr,			offsetof( GlobalData, m_battlePlanRevealsStrategyCenter ) },
	{ "OCLUpdatePausedWhenDisabled",	INI::parseBool,				nullptr,			offsetof( GlobalData, m_oclUpdatePausedWhenDisabled ) },
	{ "ScriptsLikeGenerals",				INI::parseBool,				nullptr,			offsetof( GlobalData, m_scriptsLikeGenerals ) },
	{ "MapFluffAsClientProps",			INI::parseBool,				nullptr,			offsetof( GlobalData, m_mapFluffAsClientProps ) },
	
	
	{ "DrawEntireTerrain",					INI::parseBool,				nullptr,			offsetof( GlobalData, m_drawEntireTerrain ) },
	{ "TerrainLOD",									INI::parseIndexList,	TerrainLODNames,	offsetof( GlobalData, m_terrainLOD ) },
	{ "TerrainLODTargetTimeMS",			INI::parseInt,				nullptr,			offsetof( GlobalData, m_terrainLODTargetTimeMS ) },
	{ "RightMouseAlwaysScrolls",		INI::parseBool,				nullptr,			offsetof( GlobalData, m_rightMouseAlwaysScrolls ) },
	{ "UseWaterPlane",							INI::parseBool,				nullptr,			offsetof( GlobalData, m_useWaterPlane ) },
	{ "UseCloudPlane",							INI::parseBool,				nullptr,			offsetof( GlobalData, m_useCloudPlane ) },
	{ "DownwindAngle",							INI::parseReal,				nullptr,			offsetof( GlobalData, m_downwindAngle ) },
	{ "UseShadowVolumes",						INI::parseBool,				nullptr,			offsetof( GlobalData, m_useShadowVolumes ) },
	{ "UseShadowDecals",						INI::parseBool,				nullptr,			offsetof( GlobalData, m_useShadowDecals ) },
	{ "TextureReductionFactor",			INI::parseInt,				nullptr,			offsetof( GlobalData, m_textureReductionFactor ) },
	{ "UseBehindBuildingMarker",		INI::parseBool,				nullptr,			offsetof( GlobalData, m_enableBehindBuildingMarkers ) },
	{ "WaterPositionX",							INI::parseReal,				nullptr,			offsetof( GlobalData, m_waterPositionX ) },
	{ "WaterPositionY",							INI::parseReal,				nullptr,			offsetof( GlobalData, m_waterPositionY ) },
	{ "WaterPositionZ",							INI::parseReal,				nullptr,			offsetof( GlobalData, m_waterPositionZ ) },
	{ "WaterExtentX",								INI::parseReal,				nullptr,			offsetof( GlobalData, m_waterExtentX ) },
	{ "WaterExtentY",								INI::parseReal,				nullptr,			offsetof( GlobalData, m_waterExtentY ) },
	{ "WaterType",									INI::parseInt,				nullptr,			offsetof( GlobalData, m_waterType ) },
	{ "FeatherWater",						  	INI::parseInt,				nullptr,			offsetof( GlobalData, m_featherWater ) },
	{ "ShowSoftWaterEdge",					INI::parseBool,				nullptr,			offsetof( GlobalData, m_showSoftWaterEdge ) },

	// nasty ick, we need to save this data with a map and not hard code INI values
	{ "VertexWaterAvailableMaps1",		INI::parseAsciiString,	nullptr,		offsetof( GlobalData, m_vertexWaterAvailableMaps[ 0 ] ) },
	{ "VertexWaterHeightClampLow1",		INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterHeightClampLow[ 0 ] ) },
	{ "VertexWaterHeightClampHi1",		INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterHeightClampHi[ 0 ] ) },
	{ "VertexWaterAngle1",						INI::parseAngleReal,		nullptr,		offsetof( GlobalData, m_vertexWaterAngle[ 0 ] ) },
	{ "VertexWaterXPosition1",				INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterXPosition[ 0 ] ) },
	{ "VertexWaterYPosition1",				INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterYPosition[ 0 ] ) },
	{ "VertexWaterZPosition1",				INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterZPosition[ 0 ] ) },
	{ "VertexWaterXGridCells1",				INI::parseInt,					nullptr,		offsetof( GlobalData, m_vertexWaterXGridCells[ 0 ] ) },
	{ "VertexWaterYGridCells1",				INI::parseInt,					nullptr,		offsetof( GlobalData, m_vertexWaterYGridCells[ 0 ] ) },
	{ "VertexWaterGridSize1",					INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterGridSize[ 0 ] ) },
	{ "VertexWaterAttenuationA1",			INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterAttenuationA[ 0 ] ) },
	{ "VertexWaterAttenuationB1",			INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterAttenuationB[ 0 ] ) },
	{ "VertexWaterAttenuationC1",			INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterAttenuationC[ 0 ] ) },
	{ "VertexWaterAttenuationRange1",	INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterAttenuationRange[ 0 ] ) },

	// nasty ick, we need to save this data with a map and not hard code INI values
	{ "VertexWaterAvailableMaps2",		INI::parseAsciiString,	nullptr,		offsetof( GlobalData, m_vertexWaterAvailableMaps[ 1 ] ) },
	{ "VertexWaterHeightClampLow2",		INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterHeightClampLow[ 1 ] ) },
	{ "VertexWaterHeightClampHi2",		INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterHeightClampHi[ 1 ] ) },
	{ "VertexWaterAngle2",						INI::parseAngleReal,		nullptr,		offsetof( GlobalData, m_vertexWaterAngle[ 1 ] ) },
	{ "VertexWaterXPosition2",				INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterXPosition[ 1 ] ) },
	{ "VertexWaterYPosition2",				INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterYPosition[ 1 ] ) },
	{ "VertexWaterZPosition2",				INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterZPosition[ 1 ] ) },
	{ "VertexWaterXGridCells2",				INI::parseInt,					nullptr,		offsetof( GlobalData, m_vertexWaterXGridCells[ 1 ] ) },
	{ "VertexWaterYGridCells2",				INI::parseInt,					nullptr,		offsetof( GlobalData, m_vertexWaterYGridCells[ 1 ] ) },
	{ "VertexWaterGridSize2",					INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterGridSize[ 1 ] ) },
	{ "VertexWaterAttenuationA2",			INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterAttenuationA[ 1 ] ) },
	{ "VertexWaterAttenuationB2",			INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterAttenuationB[ 1 ] ) },
	{ "VertexWaterAttenuationC2",			INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterAttenuationC[ 1 ] ) },
	{ "VertexWaterAttenuationRange2",	INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterAttenuationRange[ 1 ] ) },

	// nasty ick, we need to save this data with a map and not hard code INI values
	{ "VertexWaterAvailableMaps3",		INI::parseAsciiString,	nullptr,		offsetof( GlobalData, m_vertexWaterAvailableMaps[ 2 ] ) },
	{ "VertexWaterHeightClampLow3",		INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterHeightClampLow[ 2 ] ) },
	{ "VertexWaterHeightClampHi3",		INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterHeightClampHi[ 2 ] ) },
	{ "VertexWaterAngle3",						INI::parseAngleReal,		nullptr,		offsetof( GlobalData, m_vertexWaterAngle[ 2 ] ) },
	{ "VertexWaterXPosition3",				INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterXPosition[ 2 ] ) },
	{ "VertexWaterYPosition3",				INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterYPosition[ 2 ] ) },
	{ "VertexWaterZPosition3",				INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterZPosition[ 2 ] ) },
	{ "VertexWaterXGridCells3",				INI::parseInt,					nullptr,		offsetof( GlobalData, m_vertexWaterXGridCells[ 2 ] ) },
	{ "VertexWaterYGridCells3",				INI::parseInt,					nullptr,		offsetof( GlobalData, m_vertexWaterYGridCells[ 2 ] ) },
	{ "VertexWaterGridSize3",					INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterGridSize[ 2 ] ) },
	{ "VertexWaterAttenuationA3",			INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterAttenuationA[ 2 ] ) },
	{ "VertexWaterAttenuationB3",			INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterAttenuationB[ 2 ] ) },
	{ "VertexWaterAttenuationC3",			INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterAttenuationC[ 2 ] ) },
	{ "VertexWaterAttenuationRange3",	INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterAttenuationRange[ 2 ] ) },

	// nasty ick, we need to save this data with a map and not hard code INI values
	{ "VertexWaterAvailableMaps4",		INI::parseAsciiString,	nullptr,		offsetof( GlobalData, m_vertexWaterAvailableMaps[ 3 ] ) },
	{ "VertexWaterHeightClampLow4",		INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterHeightClampLow[ 3 ] ) },
	{ "VertexWaterHeightClampHi4",		INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterHeightClampHi[ 3 ] ) },
	{ "VertexWaterAngle4",						INI::parseAngleReal,		nullptr,		offsetof( GlobalData, m_vertexWaterAngle[ 3 ] ) },
	{ "VertexWaterXPosition4",				INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterXPosition[ 3 ] ) },
	{ "VertexWaterYPosition4",				INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterYPosition[ 3 ] ) },
	{ "VertexWaterZPosition4",				INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterZPosition[ 3 ] ) },
	{ "VertexWaterXGridCells4",				INI::parseInt,					nullptr,		offsetof( GlobalData, m_vertexWaterXGridCells[ 3 ] ) },
	{ "VertexWaterYGridCells4",				INI::parseInt,					nullptr,		offsetof( GlobalData, m_vertexWaterYGridCells[ 3 ] ) },
	{ "VertexWaterGridSize4",					INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterGridSize[ 3 ] ) },
	{ "VertexWaterAttenuationA4",			INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterAttenuationA[ 3 ] ) },
	{ "VertexWaterAttenuationB4",			INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterAttenuationB[ 3 ] ) },
	{ "VertexWaterAttenuationC4",			INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterAttenuationC[ 3 ] ) },
	{ "VertexWaterAttenuationRange4",	INI::parseReal,					nullptr,		offsetof( GlobalData, m_vertexWaterAttenuationRange[ 3 ] ) },

	{ "SkyBoxPositionZ",				INI::parseReal,				nullptr,			offsetof( GlobalData, m_skyBoxPositionZ ) },
	{ "SkyBoxScale",				INI::parseReal,				nullptr,			offsetof( GlobalData, m_skyBoxScale ) },
	{ "DrawSkyBox",				INI::parseBool,				nullptr,			offsetof( GlobalData, m_drawSkyBox ) },
	{ "CameraPitch",								INI::parseReal,				nullptr,			offsetof( GlobalData, m_cameraPitch ) },
	{ "CameraYaw",									INI::parseReal,				nullptr,			offsetof( GlobalData, m_cameraYaw ) },
	{ "CameraHeight",								INI::parseReal,				nullptr,			offsetof( GlobalData, m_cameraHeight ) },
	{ "MaxCameraHeight",						INI::parseReal,				nullptr,			offsetof( GlobalData, m_maxCameraHeight ) },
	{ "MinCameraHeight",						INI::parseReal,				nullptr,			offsetof( GlobalData, m_minCameraHeight ) },
	{ "TerrainHeightAtEdgeOfMap",					INI::parseReal,				nullptr,			offsetof( GlobalData, m_terrainHeightAtEdgeOfMap ) },
	{ "UnitDamagedThreshold",				INI::parseReal,				nullptr,			offsetof( GlobalData, m_unitDamagedThresh ) },
	{ "UnitReallyDamagedThreshold",	INI::parseReal,				nullptr,			offsetof( GlobalData, m_unitReallyDamagedThresh ) },
	{ "GroundStiffness",					INI::parseReal,				nullptr,				offsetof( GlobalData, m_groundStiffness ) },
	{ "StructureStiffness",					INI::parseReal,				nullptr,				offsetof( GlobalData, m_structureStiffness ) },
	{ "Gravity",									INI::parseAccelerationReal,				nullptr,				offsetof( GlobalData, m_gravity ) },
	{ "StealthFriendlyOpacity",		INI::parsePercentToReal,				nullptr,				offsetof( GlobalData, m_stealthFriendlyOpacity ) },
	{ "DefaultOcclusionDelay",				INI::parseDurationUnsignedInt,				nullptr,			offsetof( GlobalData, m_defaultOcclusionDelay ) },
	
	{ "PartitionCellSize",				INI::parseReal,				nullptr,			offsetof( GlobalData, m_partitionCellSize ) },

	{ "AmmoPipScaleFactor",				INI::parseReal,				nullptr,			offsetof( GlobalData, m_ammoPipScaleFactor ) },
	{ "ContainerPipScaleFactor",	INI::parseReal,				nullptr,			offsetof( GlobalData, m_containerPipScaleFactor ) },
	{ "AmmoPipWorldOffset",						INI::parseCoord3D,				nullptr,			offsetof( GlobalData, m_ammoPipWorldOffset ) },
	{ "ContainerPipWorldOffset",				INI::parseCoord3D,				nullptr,			offsetof( GlobalData, m_containerPipWorldOffset ) },
	{ "AmmoPipScreenOffset",						INI::parseCoord2D,				nullptr,			offsetof( GlobalData, m_ammoPipScreenOffset ) },
	{ "ContainerPipScreenOffset",				INI::parseCoord2D,				nullptr,			offsetof( GlobalData, m_containerPipScreenOffset ) },

	{ "HistoricDamageLimit",				INI::parseDurationUnsignedInt,				nullptr,			offsetof( GlobalData, m_historicDamageLimit ) },

	{ "MaxTerrainTracks",					INI::parseInt,				nullptr,			offsetof( GlobalData, m_maxTerrainTracks ) },
	{ "TimeOfDay",								INI::parseIndexList,	TimeOfDayNames,			offsetof( GlobalData, m_timeOfDay ) },
	{ "Weather",									INI::parseIndexList,	WeatherNames,			offsetof( GlobalData, m_weather ) },
	{ "MakeTrackMarks",						INI::parseBool,				nullptr,			offsetof( GlobalData, m_makeTrackMarks ) },
	{ "HideGarrisonFlags",						INI::parseBool,				nullptr,			offsetof( GlobalData, m_hideGarrisonFlags ) },
	{ "ForceModelsToFollowTimeOfDay",						INI::parseBool,				nullptr,			offsetof( GlobalData, m_forceModelsToFollowTimeOfDay ) },
	{ "ForceModelsToFollowWeather",						INI::parseBool,				nullptr,			offsetof( GlobalData, m_forceModelsToFollowWeather ) },

	{ "LevelGainAnimationName",		INI::parseAsciiString,	nullptr,	offsetof( GlobalData, m_levelGainAnimationName ) },
	{ "LevelGainAnimationTime",		INI::parseReal,					nullptr, offsetof( GlobalData, m_levelGainAnimationDisplayTimeInSeconds ) },
	{ "LevelGainAnimationZRise",	INI::parseReal,					nullptr, offsetof( GlobalData, m_levelGainAnimationZRisePerSecond ) },

	{ "GetHealedAnimationName",		INI::parseAsciiString,	nullptr,	offsetof( GlobalData, m_getHealedAnimationName ) },
	{ "GetHealedAnimationTime",		INI::parseReal,					nullptr, offsetof( GlobalData, m_getHealedAnimationDisplayTimeInSeconds ) },
	{ "GetHealedAnimationZRise",	INI::parseReal,					nullptr, offsetof( GlobalData, m_getHealedAnimationZRisePerSecond ) },

	{ "TerrainLightingMorningAmbient",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_MORNING ][0].ambient ) },
	{ "TerrainLightingMorningDiffuse",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_MORNING ][0].diffuse ) },
	{ "TerrainLightingMorningLightPos",		INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_MORNING ][0].lightPos ) },
	{ "TerrainLightingAfternoonAmbient",		INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_AFTERNOON ][0].ambient ) },
	{ "TerrainLightingAfternoonDiffuse",		INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_AFTERNOON ][0].diffuse ) },
	{ "TerrainLightingAfternoonLightPos",	INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_AFTERNOON ][0].lightPos ) },
	{ "TerrainLightingEveningAmbient",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_EVENING ][0].ambient ) },
	{ "TerrainLightingEveningDiffuse",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_EVENING ][0].diffuse ) },
	{ "TerrainLightingEveningLightPos",		INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_EVENING ][0].lightPos ) },
	{ "TerrainLightingNightAmbient",				INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_NIGHT ][0].ambient ) },
	{ "TerrainLightingNightDiffuse",				INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_NIGHT ][0].diffuse ) },
	{ "TerrainLightingNightLightPos",			INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_NIGHT ][0].lightPos ) },

	{ "TerrainObjectsLightingMorningAmbient",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_MORNING ][0].ambient ) },
	{ "TerrainObjectsLightingMorningDiffuse",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_MORNING ][0].diffuse ) },
	{ "TerrainObjectsLightingMorningLightPos",		INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_MORNING ][0].lightPos ) },
	{ "TerrainObjectsLightingAfternoonAmbient",		INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_AFTERNOON ][0].ambient ) },
	{ "TerrainObjectsLightingAfternoonDiffuse",		INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_AFTERNOON ][0].diffuse ) },
	{ "TerrainObjectsLightingAfternoonLightPos",	INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_AFTERNOON ][0].lightPos ) },
	{ "TerrainObjectsLightingEveningAmbient",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_EVENING ][0].ambient ) },
	{ "TerrainObjectsLightingEveningDiffuse",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_EVENING ][0].diffuse ) },
	{ "TerrainObjectsLightingEveningLightPos",		INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_EVENING ][0].lightPos ) },
	{ "TerrainObjectsLightingNightAmbient",				INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_NIGHT ][0].ambient ) },
	{ "TerrainObjectsLightingNightDiffuse",				INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_NIGHT ][0].diffuse ) },
	{ "TerrainObjectsLightingNightLightPos",			INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_NIGHT ][0].lightPos ) },

	//Secondary global light	
	{ "TerrainLightingMorningAmbient2",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_MORNING ][1].ambient ) },
	{ "TerrainLightingMorningDiffuse2",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_MORNING ][1].diffuse ) },
	{ "TerrainLightingMorningLightPos2",		INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_MORNING ][1].lightPos ) },
	{ "TerrainLightingAfternoonAmbient2",		INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_AFTERNOON ][1].ambient ) },
	{ "TerrainLightingAfternoonDiffuse2",		INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_AFTERNOON ][1].diffuse ) },
	{ "TerrainLightingAfternoonLightPos2",	INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_AFTERNOON ][1].lightPos ) },
	{ "TerrainLightingEveningAmbient2",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_EVENING ][1].ambient ) },
	{ "TerrainLightingEveningDiffuse2",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_EVENING ][1].diffuse ) },
	{ "TerrainLightingEveningLightPos2",		INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_EVENING ][1].lightPos ) },
	{ "TerrainLightingNightAmbient2",				INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_NIGHT ][1].ambient ) },
	{ "TerrainLightingNightDiffuse2",				INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_NIGHT ][1].diffuse ) },
	{ "TerrainLightingNightLightPos2",			INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_NIGHT ][1].lightPos ) },

	{ "TerrainObjectsLightingMorningAmbient2",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_MORNING ][1].ambient ) },
	{ "TerrainObjectsLightingMorningDiffuse2",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_MORNING ][1].diffuse ) },
	{ "TerrainObjectsLightingMorningLightPos2",		INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_MORNING ][1].lightPos ) },
	{ "TerrainObjectsLightingAfternoonAmbient2",		INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_AFTERNOON ][1].ambient ) },
	{ "TerrainObjectsLightingAfternoonDiffuse2",		INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_AFTERNOON ][1].diffuse ) },
	{ "TerrainObjectsLightingAfternoonLightPos2",	INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_AFTERNOON ][1].lightPos ) },
	{ "TerrainObjectsLightingEveningAmbient2",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_EVENING ][1].ambient ) },
	{ "TerrainObjectsLightingEveningDiffuse2",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_EVENING ][1].diffuse ) },
	{ "TerrainObjectsLightingEveningLightPos2",		INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_EVENING ][1].lightPos ) },
	{ "TerrainObjectsLightingNightAmbient2",				INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_NIGHT ][1].ambient ) },
	{ "TerrainObjectsLightingNightDiffuse2",				INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_NIGHT ][1].diffuse ) },
	{ "TerrainObjectsLightingNightLightPos2",			INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_NIGHT ][1].lightPos ) },

	//Third global light
	{ "TerrainLightingMorningAmbient3",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_MORNING ][2].ambient ) },
	{ "TerrainLightingMorningDiffuse3",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_MORNING ][2].diffuse ) },
	{ "TerrainLightingMorningLightPos3",		INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_MORNING ][2].lightPos ) },
	{ "TerrainLightingAfternoonAmbient3",		INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_AFTERNOON ][2].ambient ) },
	{ "TerrainLightingAfternoonDiffuse3",		INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_AFTERNOON ][2].diffuse ) },
	{ "TerrainLightingAfternoonLightPos3",	INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_AFTERNOON ][2].lightPos ) },
	{ "TerrainLightingEveningAmbient3",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_EVENING ][2].ambient ) },
	{ "TerrainLightingEveningDiffuse3",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_EVENING ][2].diffuse ) },
	{ "TerrainLightingEveningLightPos3",		INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_EVENING ][2].lightPos ) },
	{ "TerrainLightingNightAmbient3",				INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_NIGHT ][2].ambient ) },
	{ "TerrainLightingNightDiffuse3",				INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_NIGHT ][2].diffuse ) },
	{ "TerrainLightingNightLightPos3",			INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_NIGHT ][2].lightPos ) },

	{ "TerrainObjectsLightingMorningAmbient3",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_MORNING ][2].ambient ) },
	{ "TerrainObjectsLightingMorningDiffuse3",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_MORNING ][2].diffuse ) },
	{ "TerrainObjectsLightingMorningLightPos3",		INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_MORNING ][2].lightPos ) },
	{ "TerrainObjectsLightingAfternoonAmbient3",		INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_AFTERNOON ][2].ambient ) },
	{ "TerrainObjectsLightingAfternoonDiffuse3",		INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_AFTERNOON ][2].diffuse ) },
	{ "TerrainObjectsLightingAfternoonLightPos3",	INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_AFTERNOON ][2].lightPos ) },
	{ "TerrainObjectsLightingEveningAmbient3",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_EVENING ][2].ambient ) },
	{ "TerrainObjectsLightingEveningDiffuse3",			INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_EVENING ][2].diffuse ) },
	{ "TerrainObjectsLightingEveningLightPos3",		INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_EVENING ][2].lightPos ) },
	{ "TerrainObjectsLightingNightAmbient3",				INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_NIGHT ][2].ambient ) },
	{ "TerrainObjectsLightingNightDiffuse3",				INI::parseRGBColor,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_NIGHT ][2].diffuse ) },
	{ "TerrainObjectsLightingNightLightPos3",			INI::parseCoord3D,			nullptr,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_NIGHT ][2].lightPos ) },

	
	{ "NumberGlobalLights",								INI::parseInt,				nullptr,			offsetof( GlobalData, m_numGlobalLights)},
	{ "InfantryLightMorningScale",				INI::parseReal,			nullptr,			offsetof( GlobalData, m_infantryLightScale[TIME_OF_DAY_MORNING] ) },
	{ "InfantryLightAfternoonScale",				INI::parseReal,			nullptr,			offsetof( GlobalData, m_infantryLightScale[TIME_OF_DAY_AFTERNOON] ) },
	{ "InfantryLightEveningScale",				INI::parseReal,			nullptr,			offsetof( GlobalData, m_infantryLightScale[TIME_OF_DAY_EVENING] ) },
	{ "InfantryLightNightScale",				INI::parseReal,			nullptr,			offsetof( GlobalData, m_infantryLightScale[TIME_OF_DAY_NIGHT] ) },

	{ "MaxTranslucentObjects",						INI::parseInt,				nullptr,			offsetof( GlobalData, m_maxVisibleTranslucentObjects) },
	{ "OccludedColorLuminanceScale",				INI::parseReal,				nullptr,			offsetof( GlobalData, m_occludedLuminanceScale) },

/* These are internal use only, they do not need file definitons 
	{ "TerrainAmbientRGB",				INI::parseRGBColor,		NULL,			offsetof( GlobalData, m_terrainAmbient ) },
	{ "TerrainDiffuseRGB",				INI::parseRGBColor,		NULL,			offsetof( GlobalData, m_terrainDiffuse ) },
	{ "TerrainLightPos",					INI::parseCoord3D,		NULL,			offsetof( GlobalData, m_terrainLightPos ) },
*/
	{ "MaxRoadSegments",						INI::parseInt,				nullptr,			offsetof( GlobalData, m_maxRoadSegments ) },
	{ "MaxRoadVertex",							INI::parseInt,				nullptr,			offsetof( GlobalData, m_maxRoadVertex ) },
	{ "MaxRoadIndex",								INI::parseInt,				nullptr,			offsetof( GlobalData, m_maxRoadIndex ) },
	{ "MaxRoadTypes",								INI::parseInt,				nullptr,			offsetof( GlobalData, m_maxRoadTypes ) },

	{ "ValuePerSupplyBox",					INI::parseInt,				nullptr,			offsetof( GlobalData, m_baseValuePerSupplyBox ) },
	
	{ "AudioOn",										INI::parseBool,				nullptr,			offsetof( GlobalData, m_audioOn ) },
	{ "MusicOn",										INI::parseBool,				nullptr,			offsetof( GlobalData, m_musicOn ) },
	{ "SoundsOn",										INI::parseBool,				nullptr,			offsetof( GlobalData, m_soundsOn ) },
	{ "Sounds3DOn",									INI::parseBool,				nullptr,			offsetof( GlobalData, m_sounds3DOn ) },
	{ "SpeechOn",										INI::parseBool,				nullptr,			offsetof( GlobalData, m_speechOn ) },
	{ "VideoOn",										INI::parseBool,				nullptr,			offsetof( GlobalData, m_videoOn ) },
	{ "DisableCameraMovements",			INI::parseBool,				nullptr,			offsetof( GlobalData, m_disableCameraMovement ) },

/* These are internal use only, they do not need file definitons 		
	/// @todo remove this hack
	{ "InGame",											INI::parseBool,				NULL,			offsetof( GlobalData, m_inGame ) },
*/

	{ "DebugAI",										INI::parseBool,				nullptr,			offsetof( GlobalData, m_debugAI ) },
	{ "DebugAIObstacles",						INI::parseBool,				nullptr,			offsetof( GlobalData, m_debugAIObstacles ) },
	{ "ShowClientPhysics",				INI::parseBool,				nullptr,			offsetof( GlobalData, m_showClientPhysics ) },
	{ "ShowTerrainNormals",				INI::parseBool,				nullptr,			offsetof( GlobalData, m_showTerrainNormals ) },
	{ "ShowObjectHealth",						INI::parseBool,				nullptr,			offsetof( GlobalData, m_showObjectHealth ) },

	{ "ParticleScale",										INI::parseReal,					nullptr,	 offsetof( GlobalData, m_particleScale ) },
	{ "AutoFireParticleSmallPrefix",			INI::parseAsciiString,  nullptr,  offsetof( GlobalData, m_autoFireParticleSmallPrefix ) },
	{ "AutoFireParticleSmallSystem",			INI::parseAsciiString,  nullptr,  offsetof( GlobalData, m_autoFireParticleSmallSystem ) },
	{ "AutoFireParticleSmallMax",					INI::parseInt,					nullptr,	 offsetof( GlobalData, m_autoFireParticleSmallMax ) },
	{ "AutoFireParticleMediumPrefix",			INI::parseAsciiString,  nullptr,  offsetof( GlobalData, m_autoFireParticleMediumPrefix ) },
	{ "AutoFireParticleMediumSystem",			INI::parseAsciiString,  nullptr,  offsetof( GlobalData, m_autoFireParticleMediumSystem ) },
	{ "AutoFireParticleMediumMax",				INI::parseInt,					nullptr,	 offsetof( GlobalData, m_autoFireParticleMediumMax ) },
	{ "AutoFireParticleLargePrefix",			INI::parseAsciiString,  nullptr,  offsetof( GlobalData, m_autoFireParticleLargePrefix ) },
	{ "AutoFireParticleLargeSystem",			INI::parseAsciiString,  nullptr,  offsetof( GlobalData, m_autoFireParticleLargeSystem ) },
	{ "AutoFireParticleLargeMax",					INI::parseInt,					nullptr,	 offsetof( GlobalData, m_autoFireParticleLargeMax ) },
	{ "AutoSmokeParticleSmallPrefix",			INI::parseAsciiString,  nullptr,  offsetof( GlobalData, m_autoSmokeParticleSmallPrefix ) },
	{ "AutoSmokeParticleSmallSystem",			INI::parseAsciiString,  nullptr,  offsetof( GlobalData, m_autoSmokeParticleSmallSystem ) },
	{ "AutoSmokeParticleSmallMax",				INI::parseInt,					nullptr,	 offsetof( GlobalData, m_autoSmokeParticleSmallMax ) },
	{ "AutoSmokeParticleMediumPrefix",		INI::parseAsciiString,  nullptr,  offsetof( GlobalData, m_autoSmokeParticleMediumPrefix ) },
	{ "AutoSmokeParticleMediumSystem",		INI::parseAsciiString,  nullptr,  offsetof( GlobalData, m_autoSmokeParticleMediumSystem ) },
	{ "AutoSmokeParticleMediumMax",				INI::parseInt,					nullptr,	 offsetof( GlobalData, m_autoSmokeParticleMediumMax ) },
	{ "AutoSmokeParticleLargePrefix",			INI::parseAsciiString,  nullptr,  offsetof( GlobalData, m_autoSmokeParticleLargePrefix ) },
	{ "AutoSmokeParticleLargeSystem",			INI::parseAsciiString,  nullptr,  offsetof( GlobalData, m_autoSmokeParticleLargeSystem ) },
	{ "AutoSmokeParticleLargeMax",				INI::parseInt,					nullptr,	 offsetof( GlobalData, m_autoSmokeParticleLargeMax ) },
	{ "AutoAflameParticlePrefix",					INI::parseAsciiString,  nullptr,  offsetof( GlobalData, m_autoAflameParticlePrefix ) },
	{ "AutoAflameParticleSystem",					INI::parseAsciiString,  nullptr,  offsetof( GlobalData, m_autoAflameParticleSystem ) },
	{ "AutoAflameParticleMax",						INI::parseInt,					nullptr,	 offsetof( GlobalData, m_autoAflameParticleMax ) },

	{ "BuildSpeed",									INI::parseReal,				nullptr,			offsetof( GlobalData, m_BuildSpeed ) },
	{ "MinDistFromEdgeOfMapForBuild",	 INI::parseReal,				nullptr,			offsetof( GlobalData, m_MinDistFromEdgeOfMapForBuild ) },
	{ "SupplyBuildBorder",	 INI::parseReal,				nullptr,			offsetof( GlobalData, m_SupplyBuildBorder ) },
	{ "AllowedHeightVariationForBuilding", INI::parseReal,nullptr,			offsetof( GlobalData, m_allowedHeightVariationForBuilding ) },
	{ "MinLowEnergyProductionSpeed",INI::parseReal,				nullptr,			offsetof( GlobalData, m_MinLowEnergyProductionSpeed ) },
	{ "MaxLowEnergyProductionSpeed",INI::parseReal,				nullptr,			offsetof( GlobalData, m_MaxLowEnergyProductionSpeed ) },
	{ "LowEnergyPenaltyModifier",		INI::parseReal,				nullptr,			offsetof( GlobalData, m_LowEnergyPenaltyModifier ) },
	{ "MultipleFactory",						INI::parseReal,				nullptr,			offsetof( GlobalData, m_MultipleFactory ) },
	{ "RefundPercent",							INI::parsePercentToReal,			nullptr,			offsetof( GlobalData, m_RefundPercent ) },

	{ "CommandCenterHealRange",			INI::parseReal,				nullptr,			offsetof( GlobalData, m_commandCenterHealRange ) },
	{ "CommandCenterHealAmount",		INI::parseReal,				nullptr,			offsetof( GlobalData, m_commandCenterHealAmount ) },

	{ "StandardMinefieldDensity",		INI::parseReal,				nullptr,			offsetof( GlobalData, m_standardMinefieldDensity ) },
	{ "StandardMinefieldDistance",		INI::parseReal,				nullptr,			offsetof( GlobalData, m_standardMinefieldDistance ) },

	{ "MaxLineBuildObjects",				INI::parseInt,				nullptr,			offsetof( GlobalData, m_maxLineBuildObjects ) },
	{ "MaxTunnelCapacity",					INI::parseInt,				nullptr,			offsetof( GlobalData, m_maxTunnelCapacity ) },

	{ "MaxParticleCount",						INI::parseInt,				nullptr,			offsetof( GlobalData, m_maxParticleCount ) },
	{ "MaxFieldParticleCount",						INI::parseInt,				nullptr,			offsetof( GlobalData, m_maxFieldParticleCount ) },
	{ "HorizontalScrollSpeedFactor",INI::parseReal,				nullptr,			offsetof( GlobalData, m_horizontalScrollSpeedFactor ) },
	{ "VerticalScrollSpeedFactor",	INI::parseReal,				nullptr,			offsetof( GlobalData, m_verticalScrollSpeedFactor ) },
	{ "ScrollAmountCutoff",					INI::parseReal,				nullptr,			offsetof( GlobalData, m_scrollAmountCutoff ) },
	{ "CameraAdjustSpeed",					INI::parseReal,				nullptr,			offsetof( GlobalData, m_cameraAdjustSpeed ) },
	{ "EnforceMaxCameraHeight",			INI::parseBool,				nullptr,			offsetof( GlobalData, m_enforceMaxCameraHeight ) },
	{ "KeyboardScrollSpeedFactor",	INI::parseReal,				nullptr,			offsetof( GlobalData, m_keyboardScrollFactor ) },
	{ "KeyboardDefaultScrollSpeedFactor",	INI::parseReal,				nullptr,			offsetof( GlobalData, m_keyboardDefaultScrollFactor ) },
	{ "MovementPenaltyDamageState",	INI::parseIndexList,	TheBodyDamageTypeNames,	 offsetof( GlobalData, m_movementPenaltyDamageState ) },

// you cannot set this; it always has a value of 100%.
//{ "HealthBonus_Regular",				INI::parsePercentToReal, NULL,	offsetof( GlobalData, m_healthBonus[LEVEL_REGULAR]) },
	{ "HealthBonus_Veteran",				INI::parsePercentToReal, nullptr,	offsetof( GlobalData, m_healthBonus[LEVEL_VETERAN]) },
	{ "HealthBonus_Elite",					INI::parsePercentToReal, nullptr,	offsetof( GlobalData, m_healthBonus[LEVEL_ELITE]) },
	{ "HealthBonus_Heroic",					INI::parsePercentToReal, nullptr,	offsetof( GlobalData, m_healthBonus[LEVEL_HEROIC]) },

	{ "HumanSoloPlayerHealthBonus_Easy",					INI::parsePercentToReal,			nullptr,			offsetof( GlobalData, m_soloPlayerHealthBonusForDifficulty[PLAYER_HUMAN][DIFFICULTY_EASY] ) },
	{ "HumanSoloPlayerHealthBonus_Normal",				INI::parsePercentToReal,			nullptr,			offsetof( GlobalData, m_soloPlayerHealthBonusForDifficulty[PLAYER_HUMAN][DIFFICULTY_NORMAL] ) },
	{ "HumanSoloPlayerHealthBonus_Hard",				INI::parsePercentToReal,			nullptr,			offsetof( GlobalData, m_soloPlayerHealthBonusForDifficulty[PLAYER_HUMAN][DIFFICULTY_HARD] ) },

	{ "AISoloPlayerHealthBonus_Easy",					INI::parsePercentToReal,			nullptr,			offsetof( GlobalData, m_soloPlayerHealthBonusForDifficulty[PLAYER_COMPUTER][DIFFICULTY_EASY] ) },
	{ "AISoloPlayerHealthBonus_Normal",				INI::parsePercentToReal,			nullptr,			offsetof( GlobalData, m_soloPlayerHealthBonusForDifficulty[PLAYER_COMPUTER][DIFFICULTY_NORMAL] ) },
	{ "AISoloPlayerHealthBonus_Hard",				INI::parsePercentToReal,			nullptr,			offsetof( GlobalData, m_soloPlayerHealthBonusForDifficulty[PLAYER_COMPUTER][DIFFICULTY_HARD] ) },

	{ "WeaponBonus",								WeaponBonusSet::parseWeaponBonusSetPtr,	nullptr,	offsetof( GlobalData, m_weaponBonusSet ) },

	{ "DefaultStructureRubbleHeight",	INI::parseReal,			nullptr,			offsetof( GlobalData, m_defaultStructureRubbleHeight ) },

	{ "FixedSeed",									INI::parseInt,				nullptr,			offsetof( GlobalData, m_fixedSeed ) },

	{ "ShellMapName",								INI::parseAsciiString,nullptr,			offsetof( GlobalData, m_shellMapName ) },
	{ "ShellMapOn",									INI::parseBool,				nullptr,			offsetof( GlobalData, m_shellMapOn ) },
	{	"PlayIntro",									INI::parseBool,				nullptr,			offsetof( GlobalData, m_playIntro ) },

	{ "FirewallBehavior",						INI::parseInt,				nullptr,			offsetof( GlobalData, m_firewallBehavior ) },
	{ "FirewallPortOverride",				INI::parseInt,				nullptr,			offsetof( GlobalData, m_firewallPortOverride ) },
	{	"FirewallPortAllocationDelta",INI::parseInt,				nullptr,			offsetof( GlobalData, m_firewallPortAllocationDelta) },

	{	"GroupSelectMinSelectSize",		INI::parseInt,				nullptr,			offsetof( GlobalData, m_groupSelectMinSelectSize ) },
	{	"GroupSelectVolumeBase",			INI::parseReal,				nullptr,			offsetof( GlobalData, m_groupSelectVolumeBase ) },
	{	"GroupSelectVolumeIncrement",	INI::parseReal,				nullptr,			offsetof( GlobalData, m_groupSelectVolumeIncrement ) },
	{	"MaxUnitSelectSounds",				INI::parseInt,				nullptr,			offsetof( GlobalData, m_maxUnitSelectSounds ) },

	{	"SelectionFlashSaturationFactor",	INI::parseReal,		nullptr,			offsetof( GlobalData, m_selectionFlashSaturationFactor ) },
	{	"SelectionFlashHouseColor",	      INI::parseBool,		nullptr,			offsetof( GlobalData, m_selectionFlashHouseColor ) },

	{	"CameraAudibleRadius",				INI::parseReal,				nullptr,			offsetof( GlobalData, m_cameraAudibleRadius ) },
	{ "GroupMoveClickToGatherAreaFactor", INI::parseReal,	nullptr,			offsetof( GlobalData, m_groupMoveClickToGatherFactor ) },
	{ "ShakeSubtleIntensity",				INI::parseReal,				nullptr,			offsetof( GlobalData, m_shakeSubtleIntensity ) },
	{ "ShakeNormalIntensity",				INI::parseReal,				nullptr,			offsetof( GlobalData, m_shakeNormalIntensity ) },
	{ "ShakeStrongIntensity",				INI::parseReal,				nullptr,			offsetof( GlobalData, m_shakeStrongIntensity ) },
	{ "ShakeSevereIntensity",				INI::parseReal,				nullptr,			offsetof( GlobalData, m_shakeSevereIntensity ) },
	{ "ShakeCineExtremeIntensity",	INI::parseReal,				nullptr,			offsetof( GlobalData, m_shakeCineExtremeIntensity ) },
	{ "ShakeCineInsaneIntensity",		INI::parseReal,				nullptr,			offsetof( GlobalData, m_shakeCineInsaneIntensity ) },
	{ "MaxShakeIntensity",					INI::parseReal,				nullptr,			offsetof( GlobalData, m_maxShakeIntensity ) },
	{ "MaxShakeRange",							INI::parseReal,				nullptr,			offsetof( GlobalData, m_maxShakeRange) },
	{ "SellPercentage",							INI::parsePercentToReal,	nullptr,			offsetof( GlobalData, m_sellPercentage ) },
	{ "BaseRegenHealthPercentPerSecond", INI::parsePercentToReal, nullptr,	offsetof( GlobalData, m_baseRegenHealthPercentPerSecond ) },
	{ "BaseRegenDelay",							INI::parseDurationUnsignedInt, nullptr,offsetof( GlobalData, m_baseRegenDelay ) },

	{ "SpecialPowerViewObject",			INI::parseAsciiString,	nullptr,			offsetof( GlobalData, m_specialPowerViewObjectName ) },

	{ "StandardPublicBone", INI::parseAsciiStringVectorAppend, nullptr, offsetof(GlobalData, m_standardPublicBones) },
	{ "ShowMetrics",								INI::parseBool,				   nullptr,		offsetof( GlobalData, m_showMetrics ) },
  { "DefaultStartingCash",				Money::parseMoneyAmount, nullptr,		offsetof( GlobalData, m_defaultStartingCash ) },

// NOTE: m_doubleClickTimeMS is still in use, but we disallow setting it from the GameData.ini file. It is now set in the constructor according to the windows parameter.
//	{ "DoubleClickTimeMS",									INI::parseUnsignedInt,			NULL, offsetof( GlobalData, m_doubleClickTimeMS ) },
	
	{ "ShroudColor",		INI::parseRGBColor,						nullptr,	offsetof( GlobalData, m_shroudColor) },
	{ "ClearAlpha",			INI::parseUnsignedByte,				nullptr,	offsetof( GlobalData, m_clearAlpha) },
	{ "FogAlpha",				INI::parseUnsignedByte,				nullptr,	offsetof( GlobalData, m_fogAlpha) },
	{ "ShroudAlpha",		INI::parseUnsignedByte,				nullptr,	offsetof( GlobalData, m_shroudAlpha) },

	{ "HotKeyTextColor",										INI::parseColorInt,					nullptr,	offsetof( GlobalData, m_hotKeyTextColor ) },

	{ "PowerBarBase",												INI::parseInt,							nullptr,	offsetof( GlobalData, m_powerBarBase) },
	{ "PowerBarIntervals",									INI::parseReal,							nullptr,	offsetof( GlobalData, m_powerBarIntervals) },
	{ "PowerBarYellowRange",								INI::parseInt,							nullptr,	offsetof( GlobalData, m_powerBarYellowRange) },
	{ "UnlookPersistDuration",							INI::parseDurationUnsignedInt, nullptr, offsetof( GlobalData, m_unlookPersistDuration) },

	{ "NetworkFPSHistoryLength", INI::parseInt, nullptr, offsetof(GlobalData, m_networkFPSHistoryLength) },
	{ "NetworkLatencyHistoryLength", INI::parseInt, nullptr, offsetof(GlobalData, m_networkLatencyHistoryLength) },
	{ "NetworkRunAheadMetricsTime", INI::parseInt, nullptr, offsetof(GlobalData, m_networkRunAheadMetricsTime) },
	{ "NetworkCushionHistoryLength", INI::parseInt, nullptr, offsetof(GlobalData, m_networkCushionHistoryLength) },
	{ "NetworkRunAheadSlack", INI::parseInt, nullptr, offsetof(GlobalData, m_networkRunAheadSlack) },
	{ "NetworkKeepAliveDelay", INI::parseInt, nullptr, offsetof(GlobalData, m_networkKeepAliveDelay) },
	{ "NetworkDisconnectTime", INI::parseInt, nullptr, offsetof(GlobalData, m_networkDisconnectTime) },
	{ "NetworkPlayerTimeoutTime", INI::parseInt, nullptr, offsetof(GlobalData, m_networkPlayerTimeoutTime) },
	{ "NetworkDisconnectScreenNotifyTime", INI::parseInt, nullptr, offsetof(GlobalData, m_networkDisconnectScreenNotifyTime) },
	
	{ "KeyboardCameraRotateSpeed", INI::parseReal, nullptr, offsetof( GlobalData, m_keyboardCameraRotateSpeed ) },
	{ "PlayStats",									INI::parseInt,				nullptr,			offsetof( GlobalData, m_playStats ) },

#if defined(_DEBUG)
	{ "DisableCameraFade",			INI::parseBool,				nullptr,			offsetof( GlobalData, m_disableCameraFade ) },
	{ "DisableScriptedInputDisabling",			INI::parseBool,		nullptr,			offsetof( GlobalData, m_disableScriptedInputDisabling ) },
	{ "DisableMilitaryCaption",			INI::parseBool,				nullptr,			offsetof( GlobalData, m_disableMilitaryCaption ) },
	{ "BenchmarkTimer",			INI::parseInt,				nullptr,			offsetof( GlobalData, m_benchmarkTimer ) },
	{ "CheckMemoryLeaks", INI::parseBool, nullptr, offsetof(GlobalData, m_checkForLeaks) },
	{ "Wireframe",								INI::parseBool,				nullptr,			offsetof( GlobalData, m_wireframe ) },
	{ "StateMachineDebug",				INI::parseBool,				nullptr,			offsetof( GlobalData, m_stateMachineDebug ) },
	{ "UseCameraConstraints",				INI::parseBool,				nullptr,			offsetof( GlobalData, m_useCameraConstraints ) },
	{ "ShroudOn",										INI::parseBool,				nullptr,			offsetof( GlobalData, m_shroudOn ) },
	{ "FogOfWarOn",										INI::parseBool,				nullptr,			offsetof( GlobalData, m_fogOfWarOn ) },
	{ "ShowCollisionExtents",				INI::parseBool,				nullptr,			offsetof( GlobalData, m_showCollisionExtents ) },
  { "ShowAudioLocations",  				INI::parseBool,				nullptr,			offsetof( GlobalData, m_showAudioLocations ) },
	{ "DebugProjectileTileWidth",		INI::parseReal,				nullptr,			offsetof( GlobalData, m_debugProjectileTileWidth) },
	{ "DebugProjectileTileDuration",INI::parseInt,				nullptr,			offsetof( GlobalData, m_debugProjectileTileDuration) },
	{ "DebugProjectileTileColor",		INI::parseRGBColor,		nullptr,			offsetof( GlobalData, m_debugProjectileTileColor) },
	{ "DebugVisibilityTileCount",		INI::parseInt,				nullptr,			offsetof( GlobalData, m_debugVisibilityTileCount) },
	{ "DebugVisibilityTileWidth",		INI::parseReal,				nullptr,			offsetof( GlobalData, m_debugVisibilityTileWidth) },
	{ "DebugVisibilityTileDuration",INI::parseInt,				nullptr,			offsetof( GlobalData, m_debugVisibilityTileDuration) },
	{ "DebugVisibilityTileTargettableColor",INI::parseRGBColor, nullptr,	offsetof( GlobalData, m_debugVisibilityTargettableColor) },
	{ "DebugVisibilityTileDeshroudColor",		INI::parseRGBColor,	nullptr,	offsetof( GlobalData, m_debugVisibilityDeshroudColor) },
	{ "DebugVisibilityTileGapColor",				INI::parseRGBColor,	nullptr,	offsetof( GlobalData, m_debugVisibilityGapColor) },
	{ "DebugThreatMapTileDuration",					INI::parseInt,							nullptr,	offsetof( GlobalData, m_debugThreatMapTileDuration) },
	{ "MaxDebugThreatMapValue",							INI::parseUnsignedInt,			nullptr,	offsetof( GlobalData, m_maxDebugThreat) },
	{ "DebugCashValueMapTileDuration",			INI::parseInt,							nullptr,	offsetof( GlobalData, m_debugCashValueMapTileDuration) },
	{ "MaxDebugCashValueMapValue",					INI::parseUnsignedInt,			nullptr,	offsetof( GlobalData, m_maxDebugValue) },
	{ "VTune", INI::parseBool,	nullptr,			offsetof( GlobalData, m_vTune ) },
	{ "SaveStats",									INI::parseBool,				nullptr,			offsetof( GlobalData, m_saveStats ) },
	{ "UseLocalMOTD",								INI::parseBool,				nullptr,			offsetof( GlobalData, m_useLocalMOTD ) },
	{ "BaseStatsDir",								INI::parseAsciiString,nullptr,			offsetof( GlobalData, m_baseStatsDir ) },
	{ "LocalMOTDPath",							INI::parseAsciiString,nullptr,			offsetof( GlobalData, m_MOTDPath ) },
	{ "ExtraLogging",								INI::parseBool,				nullptr,			offsetof( GlobalData, m_extraLogging ) },
#endif

	{ nullptr,					nullptr,						nullptr,						0 }  // keep this last

};


// True when the command line names -benutzerdaten <folder>.
static bool userDataFolderGiven()
{
	int count = 0;
	LPWSTR *args = ::CommandLineToArgvW(::GetCommandLineW(), &count);
	bool given = false;
	if (args)
	{
		for (int i = 1; i + 1 < count && !given; ++i)
			given = ::_wcsicmp(args[i], L"-benutzerdaten") == 0;
		::LocalFree(args);
	}
	return given;
}

//-------------------------------------------------------------------------------------------------
// EA's user data folder of the original game (<Documents>\<leafName>), only read: the first start without
// Settings.ini takes over its Options.ini. Empty with -benutzerdaten (test runs keep to their own folder) or
// when the folder does not exist.
//-------------------------------------------------------------------------------------------------
static AsciiString eaUserDataDirectory(const char *leafName)
{
	if (userDataFolderGiven())
		return AsciiString::TheEmptyString;
	wchar_t documents[MAX_PATH] = {};
	if (FAILED(::SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, documents)))
		return AsciiString::TheEmptyString;
	const std::filesystem::path dir = std::filesystem::path(documents) / ::Engine::pathFromNarrow(leafName);
	std::error_code ec;
	if (!std::filesystem::is_directory(dir, ec))
		return AsciiString::TheEmptyString;
	AsciiString narrow = ::Engine::narrowForAnsiCallers(dir).c_str();
	if (narrow.isNotEmpty() && narrow.getCharAt(narrow.getLength() - 1) != '\\')
		narrow.concat('\\');
	return narrow;
}

//-------------------------------------------------------------------------------------------------
// The user data folder: -benutzerdaten <folder>, or <Documents>\<leafName>. The switch is read from the
// wide command line here, before parseCommandLine, because Options.ini is read while GameData.ini is
// parsed. The real path goes to Engine::UserDataFiles; the narrow form for the rest of the engine is in
// the ANSI code page, or the short name, or UTF-8 when neither holds the path.
//-------------------------------------------------------------------------------------------------
static AsciiString userDataDirectory(const char *leafName)
{
	std::filesystem::path dir;
	int count = 0;
	LPWSTR *args = ::CommandLineToArgvW(::GetCommandLineW(), &count);
	if (args)
	{
		for (int i = 1; i + 1 < count; ++i)
		{
			if (::_wcsicmp(args[i], L"-benutzerdaten") == 0)
			{
				std::error_code ec;
				dir = std::filesystem::absolute(std::filesystem::path(args[i + 1]), ec);
				if (ec)
					dir = std::filesystem::path(args[i + 1]);
			}
		}
		::LocalFree(args);
	}
	if (dir.empty())
	{
		wchar_t documents[MAX_PATH] = {};
		if (FAILED(::SHGetFolderPathW(nullptr, CSIDL_PERSONAL | CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT, documents)))
			return AsciiString::TheEmptyString;
		dir = std::filesystem::path(documents) / ::Engine::pathFromNarrow(leafName);
	}
	::Engine::UserDataFiles::createDirectories(dir);
	::Engine::UserDataFiles::setRoot(dir);
	AsciiString narrow = ::Engine::narrowForAnsiCallers(dir).c_str();
	if (narrow.isNotEmpty() && narrow.getCharAt(narrow.getLength() - 1) != '\\')
		narrow.concat('\\');
	DEBUG_LOG(("user data folder %s\n", narrow.str()));
	return narrow;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
GlobalData::GlobalData()
{
	Int i, j;

	//
	// we have now instanced a global data instance, if theOriginal is NULL, this is
	// *the* very first instance and it shall be recorded.  This way, when we load 
	// overrides of the global data, we can revert to the common, original data
	// in m_theOriginal
	//
	if( m_theOriginal == nullptr )
		m_theOriginal = this;
	m_next = nullptr;

#if defined(_DEBUG)
	m_specialPowerUsesDelay = TRUE;
#endif
  m_TiVOFastMode = FALSE;

#if defined(_DEBUG)
	m_wireframe = 0;
	m_stateMachineDebug = FALSE;
	m_useCameraConstraints = TRUE;
	m_shroudOn = TRUE;
	m_fogOfWarOn = FALSE;
	m_jabberOn = FALSE;
	m_munkeeOn = FALSE;
	m_showCollisionExtents = FALSE;
  m_showAudioLocations = FALSE;
	m_debugCamera = FALSE;
	m_debugVisibility = FALSE;
	m_debugVisibilityTileCount = 32;	// default to 32.
	m_debugVisibilityTileDuration = LOGICFRAMES_PER_SECOND;
	m_debugProjectilePath = FALSE;
	m_debugProjectileTileWidth = 10;
	m_debugProjectileTileDuration = LOGICFRAMES_PER_SECOND;  // Changed By Sadullah Nader
	m_debugThreatMap = FALSE;
	m_maxDebugThreat = 5000;
	m_debugThreatMapTileDuration = LOGICFRAMES_PER_SECOND;  // Changed By Sadullah Nader
	m_debugCashValueMap = FALSE;
	m_maxDebugValue = 10000;
	m_debugCashValueMapTileDuration = LOGICFRAMES_PER_SECOND; // Changed By Sadullah Nader
	m_debugIgnoreAsserts = FALSE;
	m_debugIgnoreStackTrace = FALSE;
	m_vTune = false;
	m_checkForLeaks = TRUE;
	m_benchmarkTimer = -1;
  

	m_allowUnselectableSelection = FALSE;
	m_disableCameraFade = false;
	m_disableScriptedInputDisabling = false;
	m_disableMilitaryCaption = false;
	m_saveStats = FALSE;
	m_saveAllStats = FALSE;
	m_useLocalMOTD = FALSE;
	m_baseStatsDir = ".\\";
	m_MOTDPath = "MOTD.txt";
	m_extraLogging = FALSE;
#endif

	m_playStats = -1;
	m_incrementalAGPBuf = FALSE;
	m_mapName.clear();
	m_moveHintName.clear();
	m_useTrees = 0;
	m_useTreeSway = TRUE;
	m_useDrawModuleLOD = FALSE;
	m_useHeatEffects = TRUE;
	m_useFpsLimit = FALSE;
	m_dumpAssetUsage = FALSE;
	m_framesPerSecondLimit = 0;
	m_chipSetType = 0;
	m_windowed = 0;
	m_borderless = FALSE;
	m_fullscreenAreaWidth = 0;
	m_fullscreenAreaHeight = 0;
	m_xResolution = 800;
	m_yResolution = 600;
	m_maxShellScreens = 0;
	m_useCloudMap = FALSE;
	m_use3WayTerrainBlends = 1;
	m_useLightMap = FALSE;
	m_bilinearTerrainTex = FALSE;
	m_trilinearTerrainTex = FALSE;
	m_multiPassTerrain = FALSE;
	m_adjustCliffTextures = FALSE;
	m_stretchTerrain = FALSE;
	m_useHalfHeightMap = FALSE;
	m_terrainLOD = TERRAIN_LOD_AUTOMATIC;
	m_terrainLODTargetTimeMS = 0;
	m_enableDynamicLOD = TRUE;
	m_enableStaticLOD = TRUE;
	m_rightMouseAlwaysScrolls = FALSE;
	m_useWaterPlane = FALSE;
	m_useCloudPlane = FALSE;
	m_downwindAngle = ( -0.785f );//Northeast!
	m_useShadowVolumes = FALSE;
	m_useShadowDecals = FALSE;
	m_textureReductionFactor = -1;
	m_enableBehindBuildingMarkers = TRUE;
	m_scriptDebug = FALSE;
	m_displayDebug = FALSE;
	m_winCursors = TRUE;
	m_constantDebugUpdate = FALSE;
	m_showTeamDot = FALSE;
	m_fixedSeed = -1; // disabled
	m_horizontalScrollSpeedFactor = 1.0;
	m_verticalScrollSpeedFactor = 1.0;

	m_waterPositionX = 0.0f;
	m_waterPositionY = 0.0f;
	m_waterPositionZ = 0.0f;
	m_waterExtentX = 0.0f;	
	m_waterExtentY = 0.0f;
	m_waterType = 0;
	m_featherWater = FALSE;
	m_showSoftWaterEdge = TRUE;	//display soft water edge
	m_waterQuality = 0;			// EA water until the detail level applies
	m_waterTexture = -1;		// -waterTexture or Options.ini "WaterTexture", read by the water renderer
	m_freeContent = -1;			// -freeContent or Options.ini "FreeContent", read by ArchiveFileSystem::loadFreeContent
	m_terrainAnisotropy = 0;			// as in EA until the detail level applies
	m_terrainLodBias = 0.0f;
	m_usingWaterTrackEditor = FALSE;
	m_isWorldBuilder = FALSE;
	m_editorGameGraphics = FALSE;

	m_showMetrics = false;

	for( i = 0; i < MAX_WATER_GRID_SETTINGS; i++ )
	{

		m_vertexWaterHeightClampLow[ i ] = 0.0f;
		m_vertexWaterHeightClampHi[ i ] = 0.0f;
		m_vertexWaterAngle[ i ] = 0.0f;
		m_vertexWaterXPosition[ i ] = 0.0f;
		m_vertexWaterYPosition[ i ] = 0.0f;
		m_vertexWaterZPosition[ i ] = 0.0f;
		m_vertexWaterXGridCells[ i ] = 0;
		m_vertexWaterYGridCells[ i ] = 0;
		m_vertexWaterGridSize[ i ] = 0.0f;
		m_vertexWaterAttenuationA[ i ] = 0.0f;
		m_vertexWaterAttenuationB[ i ] = 0.0f;
		m_vertexWaterAttenuationC[ i ] = 0.0f;
		m_vertexWaterAttenuationRange[ i ] = 0.0f;
		//Added By Sadullah Nader
		//Initializations missing and needed
		m_vertexWaterAvailableMaps[i].clear();
	}  // end for i

	m_skyBoxPositionZ = 0.0f;
	m_drawSkyBox = FALSE;
	m_skyBoxScale = 4.5f;

	m_historicDamageLimit = 0;
	m_maxTerrainTracks = 0;

	m_levelGainAnimationDisplayTimeInSeconds = 0.0f;
	m_levelGainAnimationZRisePerSecond = 0.0f;

	m_getHealedAnimationDisplayTimeInSeconds = 0.0f;
	m_getHealedAnimationZRisePerSecond = 0.0f;

	m_maxTankTrackEdges=100;
	m_maxTankTrackOpaqueEdges=25;
	m_maxTankTrackFadeDelay=300000;

	m_timeOfDay = TIME_OF_DAY_AFTERNOON;
	m_weather = WEATHER_NORMAL;
	m_makeTrackMarks = FALSE;
	m_hideGarrisonFlags = FALSE;
	m_forceModelsToFollowTimeOfDay = true;
	m_forceModelsToFollowWeather = true;

	m_partitionCellSize = 0.0f;
	m_ammoPipScaleFactor = 1.0f;
	m_containerPipScaleFactor = 1.0f;
	m_ammoPipWorldOffset.zero();
	m_containerPipWorldOffset.zero();
	m_ammoPipScreenOffset.x = m_ammoPipScreenOffset.y = 0;
	m_containerPipScreenOffset.x = m_containerPipScreenOffset.y = 0;

	for (i=0; i<MAX_GLOBAL_LIGHTS; i++)
	{
		m_terrainAmbient[i].red = 0.0f;
		m_terrainAmbient[i].green = 0.0f;
		m_terrainAmbient[i].blue = 0.0f;
		m_terrainDiffuse[i].red = 0.0f;
		m_terrainDiffuse[i].green = 0.0f;
		m_terrainDiffuse[i].blue = 0.0f;
		m_terrainLightPos[i].x = 0.0f;
		m_terrainLightPos[i].y = 0.0f;
		m_terrainLightPos[i].z = -1.0f;

		for (j=0; j<TIME_OF_DAY_COUNT; j++)
		{	m_terrainLighting[ j ][i].ambient.red=0;
			m_terrainLighting[ j ][i].ambient.green=0;
			m_terrainLighting[ j ][i].ambient.blue=0;
			m_terrainLighting[ j ][i].diffuse.red=0;
			m_terrainLighting[ j ][i].diffuse.green=0;
			m_terrainLighting[ j ][i].diffuse.blue=0;
			m_terrainLighting[ j ][i].lightPos.x=0;
			m_terrainLighting[ j ][i].lightPos.y=0;
			m_terrainLighting[ j ][i].lightPos.z=-1.0f;

			m_terrainObjectsLighting[ j ][i].ambient.red=0;
			m_terrainObjectsLighting[ j ][i].ambient.green=0;
			m_terrainObjectsLighting[ j ][i].ambient.blue=0;
			m_terrainObjectsLighting[ j ][i].diffuse.red=0;
			m_terrainObjectsLighting[ j ][i].diffuse.green=0;
			m_terrainObjectsLighting[ j ][i].diffuse.blue=0;
			m_terrainObjectsLighting[ j ][i].lightPos.x=0;
			m_terrainObjectsLighting[ j ][i].lightPos.y=0;
			m_terrainObjectsLighting[ j ][i].lightPos.z=-1.0f;
		}
	}

	for (j=TIME_OF_DAY_FIRST; j<TIME_OF_DAY_COUNT; j++)
		m_infantryLightScale[j] = 1.5f;
	
	m_scriptOverrideInfantryLightScale = -1.0f;

	m_numGlobalLights = 3;
	m_maxRoadSegments = 0;
	m_maxRoadVertex = 0;
	m_maxRoadIndex = 0;
	m_maxRoadTypes = 0;

	m_baseValuePerSupplyBox = 100;

	m_audioOn = TRUE;
	m_musicOn = TRUE;
	m_soundsOn = TRUE;
	m_sounds3DOn = TRUE;
	m_speechOn = TRUE;
	m_videoOn = TRUE;
	m_disableCameraMovement = FALSE;
	m_maxVisibleTranslucentObjects = 512;
	m_maxVisibleOccluderObjects = 512;
	m_maxVisibleOccludeeObjects = 512;
	m_maxVisibleNonOccluderOrOccludeeObjects = 512;
	m_occludedLuminanceScale = 0.5f;

	m_useFX = TRUE;

//	m_inGame = FALSE;	

	m_noDraw = 0;
	m_runnerMode = FALSE;
	m_crcLogInterval = 30;
	m_deepCRCFrameFirst = -1;
	m_deepCRCFrameLast = -1;
	m_deepCRCFull = FALSE;
	m_maxFrames = 0;
	m_autoSkirmishAIs = 3;
	m_autoSkirmishSeed = 1;
	m_autoSkirmishGraceMinutes = 0;
	m_autoSkirmishSuperweaponLimit = FALSE;
	m_autoSkirmishStartingCash = 0;
	m_aiStatsInterval = 0;
	m_aiPartsOff = AI_PARTS_OFF_BY_DEFAULT;
	m_blockedDeadlockFix = TRUE;
	m_blockedDeadlockFixFrame = 0;
	m_affinityLogicalProcessors = 0;
	m_audioLog = FALSE;
	m_netEmuLatency = 0;
	m_netEmuJitter = 0;
	m_netEmuLoss = 0;
	m_netEmuDuplicate = 0;
	m_netEmuReorder = 0;
	m_netEmuSeed = 0;
	m_settingsReadOnly = FALSE;
	m_netLocalInstance = 0;
	m_netNoPortMapping = FALSE;
	m_netRendezvous.clear();
	m_netNoRendezvous = FALSE;
	m_netForceRelay = FALSE;
	m_desyncReportFrame = 0;
	m_watchTestStart = 0;
	m_noRealInput = FALSE;
	m_testRealInput = FALSE;
	m_monitorIndex = 0;
	m_testAids = FALSE;
	m_testAidsMissionNr = 0;
	m_shotAfterCount = 0;
	m_quitAfterFrames = 0;
	m_shotEvery = 0;
	m_shotEveryDraw = 0;
	m_logActions = FALSE;
	m_noVSync = FALSE;
	m_fixedTimeStep = FALSE;
	m_renderBackendD3D9Requested = FALSE;	// -d3d9 gets reported, Direct3D 11 starts
	m_depthFloat = FALSE;
	m_renderEffects = 0;				// no effects, plain EA image
	m_renderEffectSharpen = 0.5f;
	m_renderEffectMsaa = -1;
	m_msaaSamples = -1;
	m_renderEffectShadowMapSize = 2048;
	m_renderEffectShadowTerrain = 0.5f;	// same as RenderEffectSettings
	m_renderEffectTerrainDetail = 1.0f;	// same as RenderEffectSettings
	m_renderEffectTerrainRock = 1.0f;	// same as RenderEffectSettings
	m_renderEffectBloom = 1.0f;			// same as RenderEffectSettings
	m_renderEffectHeat = 1.0f;			// same as RenderEffectSettings
	m_renderEffectHaze = 0.4f;			// same as RenderEffectSettings
	m_renderEffectLights = 1.0f;		// same as RenderEffectSettings
	m_renderEffectParticleLight = 1.0f;	// same as RenderEffectSettings
	m_renderEffectTerrainNormal = 1.0f;	// same as RenderEffectSettings
	m_renderEffectTerrainTess = 1.0f;	// same as RenderEffectSettings
	m_renderEffectRockTess = 1.0f;		// same as RenderEffectSettings
	m_renderEffectShadowSoft = 1.0f;	// same as RenderEffectSettings
	m_renderEffectFlatSnow = 0.6f;		// same as RenderEffectSettings
	m_renderEffectHeightFog = 0.0f;
	m_renderEffectSunShafts = 0.0f;
	m_terrainTessMax = 16.0f;
	m_terrainTessPixels = 8.0f;
	m_terrainTessProbe = 0;
	m_cpuTerrainLights = FALSE;
	m_cpuSkinning = FALSE;
	m_cpuParticles = FALSE;
	m_noMeshInstancing = FALSE;
	m_simThread = FALSE;
	m_noConstantBufferRing = FALSE;
	m_renderEffectsFromCommandLine = FALSE;
	m_renderScale = 1.0f;				// full resolution until the detail level applies
	m_renderScaleFilter = 1;			// bicubic (Catmull-Rom), as RenderEffectSettings
	m_renderScaleFilterFromCommandLine = FALSE;
	m_renderScaleSharpen = 0.5f;
	m_sceneFormat = 0;
	m_hdrKnee = 0.8f;
	m_hdrNight = 1.0f;
	m_hdrAdditive = 1.0f;
	m_gpuTiming = FALSE;
	m_logTextureStats = 0;
	m_logDrawStats = 0;
	m_logUploads = FALSE;
	m_logUploadsFirst = 1;
	m_logUploadsLast = 0;
	m_fillUnwritten = -1;
	m_logCamera = FALSE;
	m_cameraClearance = 2.0f;
	m_textureQuality = -3;				// Engine::TEXTURE_TIER_UNSET
	m_textureTier = 0;					// SD until loadTextureTiers chooses
	m_workerThreads = -1;				// automatic
	m_parallelCRC = FALSE;				// off by default
	m_crcReportFrame = 0;
	m_particleScale = 1.0f;

	m_autoFireParticleSmallMax = 0;
	m_autoFireParticleMediumMax = 0;
	m_autoFireParticleLargeMax = 0;
	m_autoSmokeParticleSmallMax = 0;
	m_autoSmokeParticleMediumMax = 0;
	m_autoSmokeParticleLargeMax = 0;
	m_autoAflameParticleMax = 0;
  
	// Added By Sadullah Nader
	// Initializations missing and needed
	m_autoFireParticleSmallPrefix.clear();
	m_autoFireParticleMediumPrefix.clear();
	m_autoFireParticleLargePrefix.clear();
	m_autoSmokeParticleSmallPrefix.clear();
	m_autoSmokeParticleMediumPrefix.clear();
	m_autoSmokeParticleLargePrefix.clear();
	m_autoAflameParticlePrefix.clear();

	m_autoFireParticleSmallSystem.clear();
	m_autoFireParticleMediumSystem.clear();
	m_autoFireParticleLargeSystem.clear();
	m_autoSmokeParticleSmallSystem.clear();
	m_autoSmokeParticleMediumSystem.clear();
	m_autoSmokeParticleLargeSystem.clear();
	m_autoAflameParticleSystem.clear();
	m_levelGainAnimationName.clear();
	m_getHealedAnimationName.clear();
	m_specialPowerViewObjectName.clear();

	m_drawEntireTerrain = FALSE;
	m_maxParticleCount = 0;
	m_maxFieldParticleCount = 30;
	
	// End Add

	m_debugAI = AI_DEBUG_NONE;
	m_debugSupplyCenterPlacement = FALSE;
	m_debugAIObstacles = FALSE;
	m_showClientPhysics = TRUE;
	m_showTerrainNormals = FALSE;
	m_showObjectHealth = FALSE;


	m_cameraPitch = 0.0f;
	m_cameraYaw = 0.0f;
	m_cameraHeight = 0.0f;
	m_minCameraHeight = 100.0f;
	m_maxCameraHeight = 300.0f;
	m_maxZoomFactor = 0.0f;			// -maxZoom, else Settings.ini
	m_terrainHeightAtEdgeOfMap = 0.0f;

	m_unitDamagedThresh = 0.5f;
	m_unitReallyDamagedThresh = 0.1f;
	m_groundStiffness = 0.5f;
	m_structureStiffness = 0.5f;
	m_gravity = -1.0f;
	m_stealthFriendlyOpacity = 0.5f;
	m_defaultOcclusionDelay = LOGICFRAMES_PER_SECOND * 3;	//default to 3 seconds

	m_preloadAssets = FALSE;
	m_preloadEverything = FALSE;
	m_preloadReport = FALSE;

	m_netMinPlayers = 1; // allowing sandbox mode

	m_defaultIP = 0;

	m_BuildSpeed = 0.0f;
	m_MinDistFromEdgeOfMapForBuild = 0.0f;
	m_SupplyBuildBorder = 0.0f;
	m_allowedHeightVariationForBuilding = 0.0f;
	m_MinLowEnergyProductionSpeed = 0.0f;
	m_MaxLowEnergyProductionSpeed = 0.0f;
	m_LowEnergyPenaltyModifier = 0.0f;
	m_MultipleFactory = 0.0f;
	m_RefundPercent = 0.0f;

	m_commandCenterHealRange = 0.0f;
	m_commandCenterHealAmount = 0.0f;
	m_maxTunnelCapacity = 0;
	m_maxLineBuildObjects = 0;

	m_standardMinefieldDensity = 0.01f;
	m_standardMinefieldDistance = 40.0f;
	
	m_groupSelectMinSelectSize = 5;
	m_groupSelectVolumeBase = 0.5f;
	m_groupSelectVolumeIncrement = 0.02f;
	m_maxUnitSelectSounds = 8;

	m_selectionFlashSaturationFactor = 0.5f; /// how colorful should the selection flash be? 0-4
	m_selectionFlashHouseColor = FALSE;  /// skip the house color and just use white.

	m_cameraAudibleRadius = 500.0;
	m_groupMoveClickToGatherFactor = 1.0f;

	m_shakeSubtleIntensity = 0.5f;
	m_shakeNormalIntensity = 1.0f;
	m_shakeStrongIntensity = 2.5f;
	m_shakeSevereIntensity = 5.0f;
	m_shakeCineExtremeIntensity = 8.0f;
	m_shakeCineInsaneIntensity = 12.0f;
	m_maxShakeIntensity = 10.0f;
	m_maxShakeRange = 150.f;

	m_sellPercentage = 1.0f;
	m_baseRegenHealthPercentPerSecond = 0.0f;
	m_baseRegenDelay = 0;

	m_hotKeyTextColor = GameMakeColor(255,255,0,255);

  // THis is put ON ice until later
  //  m_cheaterHasBeenSpiedIfMyLowestBitIsTrue = GameMakeColor(255,128,0,0);// orange, to the hacker's eye

	m_shroudColor.setFromInt( 0x00FFFFFF ) ;
	m_clearAlpha = 255;
	m_fogAlpha = 127;
	m_shroudAlpha = 0;

	m_powerBarBase = 7;
	m_powerBarIntervals = 3;
	m_powerBarYellowRange = 5;
	m_displayGamma = 1.0f;	//ramp that does nothing

	m_standardPublicBones.clear();

	m_antiAliasBoxValue = 0;

//	m_languageFilterPref = false;
	m_languageFilterPref = true;
	m_firewallBehavior = FirewallHelperClass::FIREWALL_TYPE_UNKNOWN;
	m_firewallPortOverride = 0;
	m_firewallPortAllocationDelta = 0;
	m_loadScreenDemo = FALSE;
	m_disableRender = false;
	
	m_saveCameraInReplay = FALSE;
	m_useCameraInReplay = FALSE;


	m_debugShowGraphicalFramerate = FALSE;

	// By default, show all asserts.

	m_unlookPersistDuration		= 30;

	//-----------------------------------------------------------------------------------------------

	// network timing values.  Having these default to 0 would be bad. - BGC
	m_networkFPSHistoryLength = 30;
	m_networkLatencyHistoryLength = 200;
	m_networkRunAheadMetricsTime = 500;
	m_networkCushionHistoryLength = 10;
	m_networkRunAheadSlack = 10;
	m_networkKeepAliveDelay = 20;
	m_networkDisconnectTime = 5000;
	m_networkPlayerTimeoutTime = 60000;
	m_networkDisconnectScreenNotifyTime = 15000;

	m_isBreakableMovie = FALSE;
	m_breakTheMovie = FALSE;

	setTimeOfDay( m_timeOfDay );

	m_buildMapCache = FALSE;
	m_initialFile.clear();
	m_pendingFile.clear();

	for (i = LEVEL_FIRST; i <= LEVEL_LAST; ++i)
		m_healthBonus[i] = 1.0f;

	for (i = 0; i < PLAYERTYPE_COUNT; ++i)
	{
		for (j = 0; j < DIFFICULTY_COUNT; ++j)
		{
			m_soloPlayerHealthBonusForDifficulty[i][j] = 1.0f;
		}
	}

	m_defaultStructureRubbleHeight = 1.0f;
	m_weaponBonusSet = newInstance(WeaponBonusSet);

	m_shellMapName.set("Maps\\ShellMap1\\ShellMap1.map");
	m_shellMapOn =TRUE;
	m_playIntro = TRUE;
	m_playSizzle = TRUE;
	m_afterIntro = FALSE;
	m_allowExitOutOfMovies = FALSE;
	m_loadScreenRender = FALSE;
  m_musicVolumeFactor = 0.5f;
 	m_SFXVolumeFactor = 0.5f;
  m_voiceVolumeFactor = 0.5f;
  m_3DSoundPref = false;

	m_keyboardDefaultScrollFactor = m_keyboardScrollFactor = 0.5f;
	m_scrollFactorKeyboard = m_scrollFactorEdge = m_scrollFactorDrag = -1.0f;
	m_edgeScrollInWindow = FALSE;
	m_scrollAmountCutoff = 10.0f;
	m_cameraAdjustSpeed = 0.1f;
	m_enforceMaxCameraHeight = TRUE;
	
	m_animateWindows = TRUE;
	
	m_iniCRC = 0;
	m_exeCRC = 0;
	
	// The program checksum stands for how the program computes the game: the logic version (a hash of
	// the reference replay CRCs, which are recorded anew with every deliberate logic change), the game
	// and the multiplayer scripts. Builds that differ only in rendering, menus, network code or assets
	// thus play together and play each other's replays; the running CRC check of a network game stays
	// the last line of defence.
	const Int blockSize = 65536;
	CRC exeCRC;
	// Add in MP scripts to the EXE CRC, since the game will go out of sync if they change
	File *fp = TheFileSystem->openFile("Data\\Scripts\\SkirmishScripts.scb", File::READ | File::BINARY);
	if (fp != nullptr) {
		unsigned char crcBlock[blockSize];
		Int amtRead = 0;
		while ( (amtRead=fp->read(crcBlock, blockSize)) > 0 )
		{
			exeCRC.computeCRC(crcBlock, amtRead);
		}
		fp->close();
		fp = nullptr;
	}
	fp = TheFileSystem->openFile("Data\\Scripts\\MultiplayerScripts.scb", File::READ | File::BINARY);
	if (fp != nullptr) {
		unsigned char crcBlock[blockSize];
		Int amtRead = 0;
		while ( (amtRead=fp->read(crcBlock, blockSize)) > 0 )
		{
			exeCRC.computeCRC(crcBlock, amtRead);
		}
		fp->close();
		fp = nullptr;
	}

	// The logic version is mixed in with a real hash: the CRC above only rotates and adds, and two short
	// inputs would often give the same value.
	auto mix = [](UnsignedInt h) {
		h ^= h >> 16;
		h *= 0x85EBCA6Bu;
		h ^= h >> 13;
		h *= 0xC2B2AE35u;
		h ^= h >> 16;
		return h;
	};
	const UnsignedInt edition = static_cast<UnsignedInt>(getGameEdition());
	m_exeCRC = mix(GZH_LOGIC_VERSION ^ mix(exeCRC.get() + 0x9E3779B9u * (edition + 1)));
	DEBUG_LOG(("EXE CRC: 0x%8.8X (logic version 0x%8.8X)\n", m_exeCRC, GZH_LOGIC_VERSION));
	
	m_movementPenaltyDamageState = BODY_REALLYDAMAGED;
	
	m_shouldUpdateTGAToDDS = FALSE;
	
	// Default DoubleClickTime to System double click time.
	m_doubleClickTimeMS = GetDoubleClickTime(); // Note: This is actual MS, not frames.
	
#ifdef DUMP_PERF_STATS
	m_dumpPerformanceStatistics = FALSE;
  m_dumpStatsAtInterval = FALSE;
  m_statsInterval = 30;      
#endif


	m_keyboardCameraRotateSpeed = 0.1f;

  // User data folder of the project in Documents, one per game, apart from EA's folder of the original game:
  // saved games, replays and network games of the two are not compatible.
  m_userDataDir = userDataDirectory(isGameEditionZeroHour() ? "GZH Neo\\Zero Hour" : "GZH Neo\\Generals");
  m_eaUserDataDir = eaUserDataDirectory(isGameEditionZeroHour() ? "Command and Conquer Generals Zero Hour Data" : "Command and Conquer Generals Data");
	
	//-allAdvice feature
	//m_allAdvice = FALSE;

	// Automatic retaliation is on in Zero Hour and off in Generals (the Generals options menu has no switch
	// for it; Options.ini "Retaliation" applies in both).
	m_clientRetaliationModeEnabled = isGameEditionZeroHour(); //On by default.

	// Object/combat switches, default per game. EMP: Generals spares allied transport aircraft from its own
	// EMP, Zero Hour checks KINDOF_EMP_HARDENED. Weapon choice: Generals only chooses among weapons in range
	// and lets heroes with a KILLPILOT weapon target vehicles without weapon lock. Weapon bonus: Zero Hour
	// restarts reload and shot delay when the weapon bonuses change. Drones: Generals excludes drones in
	// Object::isSelectable (the Generals data has no KINDOF_NO_SELECT, and in Zero Hour that only excludes
	// "next unit").
	m_empSparesAlliedTransports = isGameEditionGenerals();
	m_weaponChoiceLikeGenerals = isGameEditionGenerals();
	m_weaponBonusRestartsReload = TRUE;
	m_dronesNeverSelectable = isGameEditionGenerals();
	// Zero Hour clamps the target of group moves (players and scripts, AIGroup) to the map edge; the
	// Generals missions send teams to waypoints outside the map, e.g. the raptors in USA01 to their
	// deletion point.
	m_groupMovesClampedToMap = isGameEditionZeroHour();

	// Zero Hour reveals the strategy center via ShroudRevealToAllRange in the object data and has no
	// viewer object on plan change; the Generals data does not know that field.
	m_battlePlanRevealsStrategyCenter = isGameEditionGenerals();
	// The drop zone (OCLUpdate) pauses while it is disabled, as in Zero Hour; GameData.ini can switch it off.
	m_oclUpdatePausedWhenDisabled = TRUE;
	// Scripts as in Generals, on for Generals only (GlobalData.h).
	m_scriptsLikeGenerals = isGameEditionGenerals();
	// Zero Hour creates decorative map objects (CLEARED_BY_BUILD without fence width: lanterns, hydrants,
	// mailboxes ...) only as client props, always in multiplayer and otherwise below detail level High
	// (GameLogic::startNewGame); Generals creates them as logic objects.
	m_mapFluffAsClientProps = isGameEditionZeroHour();

}  // end GlobalData


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
GlobalData::~GlobalData( void )
{
	DEBUG_ASSERTCRASH( TheWritableGlobalData->m_next == nullptr, ("~GlobalData: theOriginal is not original\n") );

	if (m_weaponBonusSet)
		deleteInstanceOf(m_weaponBonusSet);

	if( m_theOriginal == this )	{
		m_theOriginal = nullptr;
		::Engine::context().writableGlobalData = nullptr;
	}

}  // end ~GlobalData

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool GlobalData::setTimeOfDay( TimeOfDay tod )
{
	if( tod >= TIME_OF_DAY_COUNT || tod < TIME_OF_DAY_FIRST )
	{
		return FALSE;
	}

	m_timeOfDay = tod;
	for (Int i=0; i<MAX_GLOBAL_LIGHTS; i++)
	{	m_terrainAmbient[i] = m_terrainLighting[ tod ][i].ambient;
		m_terrainDiffuse[i] = m_terrainLighting[ tod ][i].diffuse;
		m_terrainLightPos[i] = m_terrainLighting[ tod ][i].lightPos;
	}

	return TRUE;

}

//-------------------------------------------------------------------------------------------------
/** Create a new global data instance to override the existing data set.  The
	* initial values of the newly created instance will be a copy of the current
	* data (or the most recently created override) */
//-------------------------------------------------------------------------------------------------
GlobalData *GlobalData::newOverride( void )
{
	GlobalData *override = NEW GlobalData;

	// copy the data from the latest override (TheWritableGlobalData) to the newly created instance
	DEBUG_ASSERTCRASH( TheWritableGlobalData, ("GlobalData::newOverride() - no existing data\n") );
	*override = *TheWritableGlobalData;

	//
	// link the override to the previously created one, the link order is important here
	// for the reset function, if you change the way things are linked
	// for overrides make sure you update the reset function
	//
	override->m_next = TheWritableGlobalData;

	// set this new instance as the 'most current override' where we will access all data from
	::Engine::context().writableGlobalData = override;

	return override;

}  // end newOveride

//-------------------------------------------------------------------------------------------------
void GlobalData::init( void )
{
	// nothing
}

//-------------------------------------------------------------------------------------------------
/** Reset, remove any override data instances and return to just the initial one
	*/
//-------------------------------------------------------------------------------------------------
void GlobalData::reset( void )
{
	DEBUG_ASSERTCRASH(this == TheWritableGlobalData, ("calling reset on wrong GlobalData"));

	//
	// delete	any data instances that were loaded as an override and set the original
	// global data instance as the singleton TheWritableGlobalData once again
	//
	while (TheWritableGlobalData != GlobalData::m_theOriginal)
	{

		// get next instance
		GlobalData* next = TheWritableGlobalData->m_next;

		// delete the head of the global data list (the latest override)
		delete TheWritableGlobalData;

		// set next as top
		::Engine::context().writableGlobalData = next;

	}  // end while

	//
	// we now have the one single global data in TheWritableGlobalData singleton, lets sanity check
	// some of all that
	//
	DEBUG_ASSERTCRASH( TheWritableGlobalData->m_next == nullptr, ("ResetGlobalData: theOriginal is not original\n") );
	DEBUG_ASSERTCRASH( TheWritableGlobalData == GlobalData::m_theOriginal, ("ResetGlobalData: oops\n") );

}  // end ResetGlobalData

//-------------------------------------------------------------------------------------------------
/** Parse GameData entry */
//-------------------------------------------------------------------------------------------------
// List of D3D11 render effects from -d3d11Effects or Options.ini "D3D11Effects". Entries separated by
// comma, plus or space, case-insensitive. The bits match RENDER_EFFECT_* in WW3D2/renderdevice.h
// (W3DDisplay::init checks that with static_assert).
Bool GlobalData::parseRenderEffects( const char *list, UnsignedInt *flags, Real *sharpen, Int *msaa, UnsignedInt base, Int *shadowMapSize,
	Real *shadowTerrain, Real *terrainDetail, Real *terrainRock, Real *bloom, Real *heat, Real *haze, Real *lights,
	Real *particleLight, Real *terrainNormal, Real *terrainTess, Real *rockTess, Real *shadowSoft, Real *flatSnow,
	Real *heightFog, Real *sunShafts )
{
	if (list == nullptr || flags == nullptr)
		return FALSE;
	// An entry prefixed with "-" removes an effect from the default base (e.g. "-colorgrade" against the
	// detail level's list). If at least one entry without "-" is present, the list applies completely and
	// base is ignored; "none" clears, "all" sets all.
	UnsignedInt absolute = 0;
	UnsignedInt removed = 0;
	Bool hasAbsolute = FALSE;
	Bool ok = TRUE;
	const char *p = list;
	while (*p)
	{
		while (*p == ',' || *p == '+' || *p == ' ' || *p == '\t')
			++p;
		if (*p == 0)
			break;
		char token[64];
		Int n = 0;
		while (*p && *p != ',' && *p != '+' && *p != ' ' && *p != '\t')
		{
			if (n < static_cast<Int>(sizeof(token)) - 1)
				token[n++] = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
			++p;
		}
		token[n] = 0;
		char *name = token;
		const Bool remove = (*name == '-');
		if (remove)
			++name;
		char *value = strchr(name, '=');
		if (value)
			*value++ = 0;

		UnsignedInt bit = 0;
		if (strcmp(name, "none") == 0 || strcmp(name, "off") == 0 || strcmp(name, "aus") == 0)
		{
			if (!remove)
			{
				hasAbsolute = TRUE;
				absolute = 0;
			}
			continue;
		}
		else if (strcmp(name, "all") == 0 || strcmp(name, "alle") == 0)
			bit = 0x1F;
		else if (strcmp(name, "fxaa") == 0)
			bit = 1 << 0;
		else if (strcmp(name, "sharpen") == 0 || strcmp(name, "cas") == 0)
		{
			bit = 1 << 1;
			if (value && sharpen && !remove)
			{
				Real s = static_cast<Real>(atof(value));
				*sharpen = (s < 0.0f) ? 0.0f : ((s > 1.0f) ? 1.0f : s);
			}
		}
		else if (strcmp(name, "softparticles") == 0 || strcmp(name, "soft") == 0)
			bit = 1 << 2;
		else if (strcmp(name, "ssao") == 0)
			bit = 1 << 3;
		else if (strcmp(name, "colorgrade") == 0 || strcmp(name, "grade") == 0)
			bit = 1 << 4;
		else if (strcmp(name, "msaa") == 0 && value && msaa)
		{
			*msaa = atoi(value);
			continue;
		}
		// Sun shadows via shadow map, not included in "all".
		else if (strcmp(name, "shadowmap") == 0 || strcmp(name, "schatten") == 0)
		{
			bit = 1 << 5;
			if (value && shadowMapSize && !remove)
			{
				Int s = atoi(value);
				*shadowMapSize = (s < 512) ? 512 : ((s > 8192) ? 8192 : s);
			}
		}
		// Share of the shadows the terrain casts onto the terrain (1 = full like the other shadows); only a
		// value, no bit of its own.
		else if (strcmp(name, "shadowterrain") == 0 || strcmp(name, "schattengelaende") == 0)
		{
			if (value && shadowTerrain && !remove)
			{
				Real s = static_cast<Real>(atof(value));
				*shadowTerrain = (s < 0.0f) ? 0.0f : ((s > 1.0f) ? 1.0f : s);
			}
			continue;
		}
		// Terrain detail layer from CC0 photo scans, not in "all". "detailboden" is a German alias (like
		// "schatten"); "=x" sets the strength.
		else if (strcmp(name, "terraindetail") == 0 || strcmp(name, "detailboden") == 0)
		{
			bit = 1 << 6;
			if (value && terrainDetail && !remove)
			{
				Real s = static_cast<Real>(atof(value));
				*terrainDetail = (s < 0.0f) ? 0.0f : ((s > 2.0f) ? 2.0f : s);
			}
		}
		// Triplanar rock layer on steep slopes, not in "all"; "berge" is a German alias, "=x" sets the
		// strength (0..1).
		else if (strcmp(name, "terrainrock") == 0 || strcmp(name, "berge") == 0)
		{
			bit = 1 << 7;
			if (value && terrainRock && !remove)
			{
				Real s = static_cast<Real>(atof(value));
				*terrainRock = (s < 0.0f) ? 0.0f : ((s > 1.0f) ? 1.0f : s);
			}
		}
		// Bloom, heat shimmer and distance haze, not in "all"; "hitze" and "dunst" are German aliases. "=x" sets
		// the strength (bloom and heat 0..2, haze 0..1).
		else if (strcmp(name, "bloom") == 0)
		{
			bit = 1 << 8;
			if (value && bloom && !remove)
			{
				Real s = static_cast<Real>(atof(value));
				*bloom = (s < 0.0f) ? 0.0f : ((s > 2.0f) ? 2.0f : s);
			}
		}
		else if (strcmp(name, "heat") == 0 || strcmp(name, "hitze") == 0)
		{
			bit = 1 << 9;
			if (value && heat && !remove)
			{
				Real s = static_cast<Real>(atof(value));
				*heat = (s < 0.0f) ? 0.0f : ((s > 2.0f) ? 2.0f : s);
			}
		}
		else if (strcmp(name, "haze") == 0 || strcmp(name, "dunst") == 0)
		{
			bit = 1 << 10;
			if (value && haze && !remove)
			{
				Real s = static_cast<Real>(atof(value));
				*haze = (s < 0.0f) ? 0.0f : ((s > 1.0f) ? 1.0f : s);
			}
		}
		// Per-pixel point lights from explosions, fire and muzzle flashes, not in "all"; "=x" is the strength (0..2).
		else if (strcmp(name, "lights") == 0 || strcmp(name, "dynlights") == 0 || strcmp(name, "lichter") == 0)
		{
			bit = 1 << 11;
			if (value && lights && !remove)
			{
				Real s = static_cast<Real>(atof(value));
				*lights = (s < 0.0f) ? 0.0f : ((s > 2.0f) ? 2.0f : s);
			}
		}
		// Particles on the GPU, not in "all": smoke and dust take the time of day and the dynamic lights
		// ("=x" strength 0..1); billboards stay above the terrain.
		else if (strcmp(name, "particlelight") == 0 || strcmp(name, "partikellicht") == 0)
		{
			bit = 1 << 12;
			if (value && particleLight && !remove)
			{
				Real s = static_cast<Real>(atof(value));
				*particleLight = (s < 0.0f) ? 0.0f : ((s > 1.0f) ? 1.0f : s);
			}
		}
		else if (strcmp(name, "particleground") == 0 || strcmp(name, "partikelboden") == 0)
			bit = 1 << 13;
		// Relief of the terrain textures (normal maps and gloss next to them), not in "all"; "relief" is a German
		// alias, "=x" sets the strength (0..2).
		else if (strcmp(name, "terrainnormal") == 0 || strcmp(name, "relief") == 0)
		{
			bit = 1 << 14;
			if (value && terrainNormal && !remove)
			{
				Real s = static_cast<Real>(atof(value));
				*terrainNormal = (s < 0.0f) ? 0.0f : ((s > 2.0f) ? 2.0f : s);
			}
		}
		// Terrain tessellation with displacement from the heights of the terrain textures, not in "all";
		// "=x" sets the displacement strength (0..2; 0 tessellates without displacement, for comparisons).
		else if (strcmp(name, "terraintess") == 0 || strcmp(name, "tessellation") == 0)
		{
			bit = 1 << 15;
			if (value && terrainTess && !remove)
			{
				Real s = static_cast<Real>(atof(value));
				*terrainTess = (s < 0.0f) ? 0.0f : ((s > 2.0f) ? 2.0f : s);
			}
		}
		// Rock slopes in the tessellation (needs terraintess and terrainrock), not in "all"; "felstess" is a German
		// alias, "=x" sets the strength (0..2).
		else if (strcmp(name, "rocktess") == 0 || strcmp(name, "felstess") == 0)
		{
			bit = 1 << 16;
			if (value && rockTess && !remove)
			{
				Real s = static_cast<Real>(atof(value));
				*rockTess = (s < 0.0f) ? 0.0f : ((s > 2.0f) ? 2.0f : s);
			}
		}
		// Alpha test as coverage with multisampling (cut-out edges smoothed like geometry), not in "all"; without
		// multisampling it does nothing.
		else if (strcmp(name, "alphacoverage") == 0 || strcmp(name, "a2c") == 0)
			bit = 1 << 17;
		// Sun shadows (with shadowmap), not in "all": penumbra by the distance to the caster ("=x" light size 0..2),
		// contact shadows from short screen space rays.
		else if (strcmp(name, "softshadows") == 0 || strcmp(name, "weicheschatten") == 0)
		{
			bit = 1 << 18;
			if (value && shadowSoft && !remove)
			{
				Real s = static_cast<Real>(atof(value));
				*shadowSoft = (s < 0.0f) ? 0.0f : ((s > 2.0f) ? 2.0f : s);
			}
		}
		else if (strcmp(name, "contactshadows") == 0 || strcmp(name, "kontaktschatten") == 0)
			bit = 1 << 19;
		// Two shadow cascades (near map plus a far map over twice the range), not in "all".
		else if (strcmp(name, "shadowcascades") == 0 || strcmp(name, "schattenkaskaden") == 0)
			bit = 1 << 20;
		// Depth and color effects with multisampling (resolved depth), not in "all". They are on with multisampling anyway;
		// "-msaadepth" switches them off explicitly (bit 28).
		else if (strcmp(name, "msaadepth") == 0 || strcmp(name, "msaatiefe") == 0)
			bit = 1 << 21;
		// Values above 1 in the 3D scene and a tone curve into 8 bits, not in "all".
		else if (strcmp(name, "hdr") == 0)
			bit = 1 << 22;
		// Dithering against banding, water reflection, horizon based ambient occlusion; none of them in "all".
		else if (strcmp(name, "deband") == 0 || strcmp(name, "entbaenderung") == 0)
			bit = 1 << 23;
		else if (strcmp(name, "waterssr") == 0 || strcmp(name, "wasserspiegelung") == 0)
			bit = 1 << 24;
		else if (strcmp(name, "gtao") == 0)
			bit = 1 << 25;
		// Temporal anti-aliasing (replaces fxaa), not in "all".
		else if (strcmp(name, "taa") == 0)
			bit = 1 << 26;
		// Terrain light per pixel instead of per vertex, not in "all"; "bodenlicht" is a German alias.
		else if (strcmp(name, "terrainlight") == 0 || strcmp(name, "bodenlicht") == 0)
			bit = 1 << 27;
		// Snow on flat ground of winter maps (needs terrainrock), not in "all"; "schneeflach" is a German alias, "=x"
		// sets the share of snow (0..1).
		else if (strcmp(name, "flatsnow") == 0 || strcmp(name, "schneeflach") == 0)
		{
			bit = 1 << 29;
			if (value && flatSnow && !remove)
			{
				Real s = static_cast<Real>(atof(value));
				*flatSnow = (s < 0.0f) ? 0.0f : ((s > 1.0f) ? 1.0f : s);
			}
		}
		// Models lit per pixel instead of per vertex, not in "all"; "modelllicht" is a German alias.
		else if (strcmp(name, "modellight") == 0 || strcmp(name, "modelllicht") == 0)
			bit = 1 << 30;
		// Atmosphere (needs shadowmap), not in "all": height fog and sun shafts share bit 31, each with its own strength
		// (0..2, 1 without a value); "-heightfog" sets the fog strength to 0 and keeps the bit for the shafts.
		else if (strcmp(name, "heightfog") == 0 || strcmp(name, "hoehennebel") == 0 ||
				 strcmp(name, "sunshafts") == 0 || strcmp(name, "lichtstrahlen") == 0)
		{
			const Bool fog = (name[0] == 'h');
			Real *strength = fog ? heightFog : sunShafts;
			Real s = value ? static_cast<Real>(atof(value)) : 1.0f;
			s = (s < 0.0f) ? 0.0f : ((s > 2.0f) ? 2.0f : s);
			if (strength)
				*strength = remove ? 0.0f : s;
			if (remove)
				continue;
			bit = 1u << 31;
		}
		else
		{
			DEBUG_LOG(("D3D11Effects: unknown entry '%s' (fxaa, sharpen[=0..1], softparticles, ssao, colorgrade, shadowmap[=n], shadowterrain=0..1, terraindetail[=0..2], terrainrock[=0..1], bloom[=0..2], heat[=0..2], haze[=0..1], lights[=0..2], particlelight[=0..1], particleground, terrainnormal[=0..2], terraintess[=0..2], rocktess[=0..2], alphacoverage, softshadows[=0..2], contactshadows, shadowcascades, msaadepth, hdr, deband, waterssr, gtao, taa, terrainlight, flatsnow[=0..1], modellight, heightfog[=0..2], sunshafts[=0..2], all, none, msaa=n; a leading '-' removes)\n", token));
			ok = FALSE;
			continue;
		}
		if (remove)
			removed |= bit;
		else
		{
			hasAbsolute = TRUE;
			absolute |= bit;
		}
	}
	*flags = (hasAbsolute ? absolute : base) & ~removed;
	if (removed & (1u << 21))
		*flags |= 1u << 28;
	return ok;
}

//-------------------------------------------------------------------------------------------------
// Converts the slider position 0..100 to gamma 0.6..2.0; 50 is neutral. Shared by the Options.ini
// reader and the -gamma switch.
Real GlobalData::gammaFromSlider( Int val )
{
	//generate a value between 0.6 and 2.0.
	if (val < 50)
	{	//darker gamma
		if (val <= 0)
			return 0.6f;
		return 1.0f-(0.4f) * static_cast<Real>(50-val)/50.0f;
	}
	if (val > 50)
		return 1.0f+(1.0f) * static_cast<Real>(val-50)/50.0f;
	return 1.0f;
}

// Internal render resolution: below 0.5 the image gets too coarse for the interface around it; above 1 is
// supersampling (the scene target grows), at most 2 (four times the pixels).
Real GlobalData::normalizeRenderScale( Real value )
{
	if (value > 4.0f)
		value *= 0.01f;							// given in percent
	if (!(value > 0.0f))
		return 1.0f;
	if (value > 2.0f)
		return 2.0f;
	return (value < 0.5f) ? 0.5f : value;
}

Int GlobalData::parseRenderScaleFilter( const char *name )
{
	if (name == nullptr)
		return -1;
	if (stricmp(name, "bilinear") == 0 || stricmp(name, "linear") == 0 || strcmp(name, "0") == 0)
		return 0;
	if (stricmp(name, "bicubic") == 0 || stricmp(name, "bikubisch") == 0 || stricmp(name, "catmullrom") == 0 || strcmp(name, "1") == 0)
		return 1;
	if (stricmp(name, "cas") == 0 || strcmp(name, "2") == 0)
		return 2;
	return -1;
}

/**
 * Limit of Camera.MaxZoomFactor and -maxZoom. The visible area grows with the square of the factor; 1.25 keeps
 * the advantage small. A value outside 1 to 1.25 is clamped with a note in the logs.
 */
Real GlobalData::clampMaxZoomFactor( Real factor )
{
	const Real MAX_ZOOM_FACTOR = 1.25f;
	if( factor >= 1.0f && factor <= MAX_ZOOM_FACTOR )
		return factor;
	const Real clamped = factor > MAX_ZOOM_FACTOR ? MAX_ZOOM_FACTOR : 1.0f;
	DEBUG_LOG(( "MaxZoomFactor %.2f outside 1 to %.2f, using %.2f\n", factor, MAX_ZOOM_FACTOR, clamped ));
	char line[96];
	WWFormat::Into( line, "MAXZOOM {:.2f} outside 1 to {:.2f}, using {:.2f}", factor, MAX_ZOOM_FACTOR, clamped );
	line[sizeof(line) - 1] = 0;
	GameEngine_RunnerLog( line );
	return clamped;
}

void GlobalData::parseGameDataDefinition( INI* ini )
{
	if( TheWritableGlobalData && ini->getLoadType() != INILoadType::INI_LOAD_MULTIFILE)
	{

		// 
		// if the type of loading we're doing creates override data, we need to
		// be loading into a new override item
		//
		if( ini->getLoadType() == INILoadType::INI_LOAD_CREATE_OVERRIDES )
			TheWritableGlobalData->newOverride();

	}  // end if
	else if (!TheWritableGlobalData)
	{

		// we don't have any global data instance at all yet, create one
		::Engine::context().writableGlobalData = NEW GlobalData;

	}  // end else
	// If we're multifile, then continue loading stuff into the Global Data as normal.

	// parse the ini weapon definition
	ini->initFromINI( TheWritableGlobalData, s_GlobalDataFieldParseTable );

	// UserDataLeafName from the Generals GameData.ini is read but not used: the folder is the project's own.

	// override INI values with user preferences
	TheWritableGlobalData->m_useAlternateMouse = UserSettings::getAlternateMouseModeEnabled();
	TheWritableGlobalData->m_clientRetaliationModeEnabled = UserSettings::getRetaliationModeEnabled();
	TheWritableGlobalData->m_doubleClickAttackMove = UserSettings::getDoubleClickAttackMoveEnabled();
	TheWritableGlobalData->m_keyboardScrollFactor = UserSettings::getScrollFactor();
	{
		// separate speeds per kind of scrolling, only in Settings.ini; not set: the one ScrollFactor
		struct { const char *id; Real GlobalData::*field; } speeds[] = {
			{ "Controls.ScrollSpeedKeyboard", &GlobalData::m_scrollFactorKeyboard },
			{ "Controls.ScrollSpeedEdge", &GlobalData::m_scrollFactorEdge },
			{ "Controls.ScrollSpeedDrag", &GlobalData::m_scrollFactorDrag } };
		for (const auto &speed : speeds)
		{
			const AsciiString text = UserSettings::get(speed.id);
			TheWritableGlobalData->*speed.field = text.isEmpty() ? -1.0f : static_cast<Real>(atoi(text.str())) / 100.0f;
		}
		TheWritableGlobalData->m_edgeScrollInWindow = UserSettings::get("Controls.EdgeScrollInWindow").compareNoCase("yes") == 0;
	}
	// -maxZoom wins over Settings.ini Camera.MaxZoomFactor
	if( TheWritableGlobalData->m_maxZoomFactor <= 0.0f )
	{
		const AsciiString text = UserSettings::get("Camera.MaxZoomFactor");
		TheWritableGlobalData->m_maxZoomFactor = text.isEmpty() ? 1.0f : static_cast<Real>(atof(text.str()));
	}
	TheWritableGlobalData->m_maxZoomFactor = clampMaxZoomFactor( TheWritableGlobalData->m_maxZoomFactor );
	TheWritableGlobalData->m_defaultIP = UserSettings::getLANIPAddress();
	
	TheWritableGlobalData->m_saveCameraInReplay = UserSettings::saveCameraInReplays();
	TheWritableGlobalData->m_useCameraInReplay = UserSettings::useCameraInReplays();
	
	// Slider position of Display.Gamma (-gamma included); conversion in gammaFromSlider.
	TheWritableGlobalData->m_displayGamma = gammaFromSlider(UserSettings::getGammaValue());

	Int xres,yres;
	UserSettings::getResolution(&xres, &yres);

	TheWritableGlobalData->m_xResolution = xres;
	TheWritableGlobalData->m_yResolution = yres;
}

