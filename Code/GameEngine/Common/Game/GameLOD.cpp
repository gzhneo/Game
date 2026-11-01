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

// FILE: GameLOD.cpp ///////////////////////////////////////////////////////////
//
// Used to set detail levels of various game systems.
//
// Author: Mark Wilczynski, Sept 2002
//
//
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/CommandLine.h"
#include "Common/GameLOD.h"
#include "GameClient/TerrainVisual.h"
#include "GameClient/GameClient.h"
#include "Common/UserSettings.h"

#define DEFINE_PARTICLE_SYSTEM_NAMES
#include "GameClient/ParticleSys.h"
#include "Common/FileSystem.h"	// TheFileSystem for the project's own detail level file
#include "Engine/TextureTier.h"	// VideoMemoryInfo, recommendDetailLevel


//Hack to get access to a static method on the W3DDevice side. -MW


// INI::parseAsciiString also splits at "=", so "...,shadowmap=2048,terraindetail" would become just
// "...,shadowmap" and the shadow map size and everything after it would be lost. Read up to the end of
// the line instead, splitting only at whitespace; a lone "=" (between field name and value) is skipped.
static void parseEffectList( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	AsciiString *list = static_cast<AsciiString *>(store);
	list->clear();
	for (const char *token = ini->getNextTokenOrNull(" \t\r\n"); token != nullptr; token = ini->getNextTokenOrNull(" \t\r\n"))
	{
		while (*token == '=')
			++token;
		if (*token == 0)
			continue;
		if (!list->isEmpty())
			list->concat(",");
		list->concat(token);
	}
}

static const FieldParse TheStaticGameLODFieldParseTable[] = 
{
	{ "MinimumFPS",						INI::parseInt,					nullptr,	offsetof( StaticGameLODInfo, m_minFPS)},
	{ "MinimumProcessorFps",			INI::parseInt,					nullptr,	offsetof( StaticGameLODInfo, m_minProcessorFPS)},
	{ "SampleCount2D",					INI::parseInt,					nullptr,	offsetof( StaticGameLODInfo, m_sampleCount2D ) },
	{ "SampleCount3D",					INI::parseInt,					nullptr,	offsetof( StaticGameLODInfo, m_sampleCount3D ) },
	{ "StreamCount",					INI::parseInt,					nullptr,	offsetof( StaticGameLODInfo, m_streamCount ) },
	{ "MaxParticleCount",				INI::parseInt,					nullptr,	offsetof( StaticGameLODInfo, m_maxParticleCount ) },
	{ "UseShadowVolumes",				INI::parseBool,					nullptr,	offsetof( StaticGameLODInfo, m_useShadowVolumes ) },
	{ "UseShadowDecals",				INI::parseBool,					nullptr,	offsetof( StaticGameLODInfo, m_useShadowDecals ) },
	{ "UseCloudMap",					INI::parseBool,					nullptr,	offsetof( StaticGameLODInfo, m_useCloudMap ) },
	{ "UseLightMap",					INI::parseBool,					nullptr,	offsetof( StaticGameLODInfo, m_useLightMap ) },
	{ "ShowSoftWaterEdge",				INI::parseBool,					nullptr,	offsetof( StaticGameLODInfo, m_showSoftWaterEdge ) },
	{ "WaterQuality",					INI::parseInt,					nullptr,	offsetof( StaticGameLODInfo, m_waterQuality ) },
	{ "D3D11Effects",					parseEffectList,				nullptr,	offsetof( StaticGameLODInfo, m_d3d11Effects ) },
	{ "TerrainAnisotropy",				INI::parseInt,					nullptr,	offsetof( StaticGameLODInfo, m_terrainAnisotropy ) },
	{ "RenderScale",					INI::parseReal,					nullptr,	offsetof( StaticGameLODInfo, m_renderScale ) },
	{ "MaxTankTrackEdges",				INI::parseInt,					nullptr,	offsetof( StaticGameLODInfo, m_maxTankTrackEdges) },
	{ "MaxTankTrackOpaqueEdges",		INI::parseInt,					nullptr,	offsetof( StaticGameLODInfo, m_maxTankTrackOpaqueEdges) },
	{ "MaxTankTrackFadeDelay",			INI::parseInt,					nullptr,	offsetof( StaticGameLODInfo, m_maxTankTrackFadeDelay) },
	{ "UseBuildupScaffolds",			INI::parseBool,					nullptr,	offsetof( StaticGameLODInfo, m_useBuildupScaffolds ) },
	{ "UseTreeSway",					INI::parseBool,					nullptr,	offsetof( StaticGameLODInfo, m_useTreeSway ) },
	{ "UseEmissiveNightMaterials",		INI::parseBool,					nullptr,	offsetof( StaticGameLODInfo, m_useEmissiveNightMaterials ) },
	{ "UseHeatEffects",					INI::parseBool,					nullptr,	offsetof( StaticGameLODInfo, m_useHeatEffects ) },
	{ "TextureReductionFactor",		INI::parseInt,					nullptr,	offsetof( StaticGameLODInfo, m_textureReduction ) },
	{ nullptr, nullptr, nullptr, 0 }
};

static const char *StaticGameLODNames[]=
{
	"Low",
	"Medium",
	"High",
	"VeryHigh",	// same name as in older Zero Hour Options.ini files, so they still apply
	"Custom"
};

StaticGameLODInfo::StaticGameLODInfo(void)
{
	m_minFPS=0;
	m_minProcessorFPS=0;
	m_sampleCount2D=6;
	m_sampleCount3D=24;
	m_streamCount=2;
	m_maxParticleCount=2500;

	m_useShadowVolumes=TRUE;
	m_useShadowDecals=TRUE;
	m_useCloudMap=TRUE;
	m_useLightMap=TRUE;
	m_showSoftWaterEdge=TRUE;
	m_waterQuality=0;	// without an entry in GameLODOverride.ini the EA water
	m_terrainAnisotropy=0;	// without an entry as in EA
	m_renderScale=1.0f;	// without an entry the full resolution
	m_maxTankTrackEdges=100;
	m_maxTankTrackOpaqueEdges=25;
	m_maxTankTrackFadeDelay=300000;
	m_useBuildupScaffolds=TRUE;
	m_useTreeSway=TRUE;
	m_useEmissiveNightMaterials=TRUE;
	m_useHeatEffects=TRUE;
	m_textureReduction = 0;	//none
	m_useFpsLimit = TRUE;
	m_enableDynamicLOD = TRUE;
	m_useTrees = TRUE;
}

static const FieldParse TheDynamicGameLODFieldParseTable[] = 
{
	{ "MinimumFPS",						INI::parseInt,					nullptr,	offsetof( DynamicGameLODInfo, m_minFPS)},
	{ "ParticleSkipMask",				INI::parseInt,					nullptr,	offsetof( DynamicGameLODInfo, m_dynamicParticleSkipMask)},
	{ "DebrisSkipMask",					INI::parseInt,					nullptr,	offsetof( DynamicGameLODInfo, m_dynamicDebrisSkipMask)},
	{ "SlowDeathScale",					INI::parseReal,					nullptr,	offsetof( DynamicGameLODInfo, m_slowDeathScale)},
	{ "MinParticlePriority",			INI::parseIndexList, ParticlePriorityNames,	offsetof( DynamicGameLODInfo, m_minDynamicParticlePriority)},
	{ "MinParticleSkipPriority",		INI::parseIndexList, ParticlePriorityNames,	offsetof( DynamicGameLODInfo, m_minDynamicParticleSkipPriority)},
	{ nullptr, nullptr, nullptr, 0 }
};

static const char *DynamicGameLODNames[]=
{
	"Low",
	"Medium",
	"High",
	"VeryHigh"
};

DynamicGameLODInfo::DynamicGameLODInfo(void)
{
	m_minFPS=0;
	m_dynamicParticleSkipMask=0;
	m_dynamicDebrisSkipMask=0;
	m_slowDeathScale=1.0f;
	m_minDynamicParticlePriority = PARTICLE_PRIORITY_LOWEST;
	m_minDynamicParticleSkipPriority = PARTICLE_PRIORITY_LOWEST;
};

// The hardware presets (LODPreset), benchmark profiles (BenchProfile) and ReallyLowMHz in
// GameLODPresets.ini/GameLOD.ini describe machines from 2003 (P3/P4/K7, Voodoo to Radeon 9700). The game
// is built for AVX2 and runs on none of them, so the detail level is not derived from them. The blocks
// are still in the game data and are read and discarded.
static void skipRestOfLine(INI* ini)
{
	while (ini->getNextTokenOrNull() != nullptr)
		;
}

void parseReallyLowMHz(INI* ini)
{
	skipRestOfLine(ini);
}

void INI::parseBenchProfile( INI* ini)
{
	skipRestOfLine(ini);
}

void INI::parseLODPreset( INI* ini )
{
	skipRestOfLine(ini);
}

GameLODManager::GameLODManager(void)
{
	m_currentStaticLOD = STATIC_GAME_LOD_UNKNOWN;
	m_currentDynamicLOD = DYNAMIC_GAME_LOD_HIGH;
	m_numParticleGenerations=0;
	m_dynamicParticleSkipMask=0;
	m_numDebrisGenerations=0;
	m_dynamicDebrisSkipMask=0;
	m_slowDeathScale=1.0f;
	m_idealDetailLevel = STATIC_GAME_LOD_UNKNOWN;
	m_currentTextureReduction=0;
	
	for (Int j=0; j<STATIC_GAME_LOD_COUNT; j++)
		m_staticGameLODDefined[j]=FALSE;
};

GameLODManager::~GameLODManager()
{

}

void GameLODManager::init(void)
{
	INI ini;
	//Get Presets for each LOD level.
	ini.load( AsciiString( "Data\\INI\\GameLOD.ini" ), INILoadType::INI_LOAD_OVERWRITE, nullptr );

	//Get presets for each known hardware configuration
	ini.load( AsciiString( "Data\\INI\\GameLODPresets.ini"), INILoadType::INI_LOAD_OVERWRITE, nullptr);

	// The project's own detail levels. The EA values target machines from 2003; the minimum hardware gets
	// the full image, and only what is expensive today is scaled. The file lives in the repository under
	// Data/INI (one file for both games) and is copied next to the exe as ProjectData/INI at build time
	// (separate folder, apart from EA's Data); it is loaded last and thus overrides the StaticGameLOD blocks
	// from GameLOD.ini. If it is missing, the EA values apply (and "VeryHigh" is derived from "High").
	if( TheFileSystem->doesFileExist( "ProjectData\\INI\\GameLODOverride.ini" ) )
		ini.load( AsciiString( "ProjectData\\INI\\GameLODOverride.ini" ), INILoadType::INI_LOAD_OVERWRITE, nullptr );

	// The base game's GameLOD.ini only defines Low/Medium/High (the data lives in the read-only EA
	// installation). "VeryHigh" is therefore derived from High: all effects on, double particle and track
	// limits. A GameLOD.ini with its own VeryHigh block takes precedence.
	if (!m_staticGameLODDefined[STATIC_GAME_LOD_VERY_HIGH])
	{
		StaticGameLODInfo &veryHigh = m_staticGameLODInfo[STATIC_GAME_LOD_VERY_HIGH];
		veryHigh = m_staticGameLODInfo[STATIC_GAME_LOD_HIGH];
		veryHigh.m_maxParticleCount *= 2;
		veryHigh.m_maxTankTrackEdges *= 2;
		veryHigh.m_maxTankTrackOpaqueEdges *= 2;
		veryHigh.m_maxTankTrackFadeDelay *= 2;
		veryHigh.m_useShadowVolumes = TRUE;
		veryHigh.m_useShadowDecals = TRUE;
		veryHigh.m_useCloudMap = TRUE;
		veryHigh.m_useLightMap = TRUE;
		veryHigh.m_showSoftWaterEdge = TRUE;
		veryHigh.m_useBuildupScaffolds = TRUE;
		veryHigh.m_useTreeSway = TRUE;
		veryHigh.m_useEmissiveNightMaterials = TRUE;
		veryHigh.m_useHeatEffects = TRUE;	// Zero Hour only (heat distortion, e.g. microwave tank)
		veryHigh.m_useTrees = TRUE;
		veryHigh.m_textureReduction = 0;
		m_staticGameLODDefined[STATIC_GAME_LOD_VERY_HIGH] = TRUE;
	}

	// Log the final levels. This shows at startup which values really apply, i.e. whether
	// GameLODOverride.ini was found and applied.
#if defined(DEBUG_LOGGING)
	static const char *lodNames[] = { "Low", "Medium", "High", "VeryHigh" };
	for( Int lodIdx = STATIC_GAME_LOD_LOW; lodIdx <= STATIC_GAME_LOD_VERY_HIGH; ++lodIdx )
	{
		const StaticGameLODInfo &lodDbg = m_staticGameLODInfo[ lodIdx ];
		DEBUG_LOG(( "detail level %s: particles %d, shadow volumes %s, tank track edges %d, texture reduction %d\n",
			lodNames[ lodIdx ], lodDbg.m_maxParticleCount,
			lodDbg.m_useShadowVolumes ? "yes" : "no", lodDbg.m_maxTankTrackEdges,
			lodDbg.m_textureReduction ));
	}
#endif

	//Get Presets for custom LOD level by pulling them out of initial globaldata (which should
	//have all settings already applied).
	refreshCustomStaticLODLevel();

	//Override with user preferences
	StaticGameLODLevel userSetDetail=static_cast<StaticGameLODLevel>(UserSettings::getStaticGameDetail());
	if (const char *forced = getForcedStaticLOD())
	{
		const Int forcedIndex = getStaticGameLODIndex(AsciiString(forced));
		if (forcedIndex != STATIC_GAME_LOD_UNKNOWN)
			userSetDetail = static_cast<StaticGameLODLevel>(forcedIndex);
	}

	// The ideal level is detected again on every start (findStaticLODLevel, cheap); the stored value is only a note
	// for the user. A detail level the user chose stays untouched.
	m_idealDetailLevel = STATIC_GAME_LOD_UNKNOWN;


	if (userSetDetail == STATIC_GAME_LOD_CUSTOM)
	{
		TheWritableGlobalData->m_textureReductionFactor = UserSettings::getTextureReduction();
		TheWritableGlobalData->m_useShadowVolumes = UserSettings::get3DShadowsEnabled();
		TheWritableGlobalData->m_useShadowDecals = UserSettings::get2DShadowsEnabled();
		TheWritableGlobalData->m_enableBehindBuildingMarkers = UserSettings::getBuildingOcclusionEnabled();
		TheWritableGlobalData->m_maxParticleCount = UserSettings::getParticleCap();
		TheWritableGlobalData->m_enableDynamicLOD = UserSettings::getDynamicLODEnabled();
		TheWritableGlobalData->m_useFpsLimit = UserSettings::getFPSLimitEnabled();
		TheWritableGlobalData->m_useLightMap = UserSettings::getLightmapEnabled();
		TheWritableGlobalData->m_useCloudMap = UserSettings::getCloudShadowsEnabled();
		TheWritableGlobalData->m_showSoftWaterEdge = UserSettings::getSmoothWaterEnabled();
		// The custom level has no water switch of its own; with soft water edges the water of the highest
		// level applies, otherwise the EA water.
		TheWritableGlobalData->m_waterQuality = TheWritableGlobalData->m_showSoftWaterEdge ?
			m_staticGameLODInfo[STATIC_GAME_LOD_VERY_HIGH].m_waterQuality : 0;
		m_staticGameLODInfo[STATIC_GAME_LOD_CUSTOM].m_waterQuality = TheWritableGlobalData->m_waterQuality;
		TheWritableGlobalData->m_useHeatEffects = UserSettings::getUseHeatEffects();
		TheWritableGlobalData->m_useDrawModuleLOD = UserSettings::getExtraAnimationsDisabled();
		TheWritableGlobalData->m_useTreeSway = !TheWritableGlobalData->m_useDrawModuleLOD;	//borrow same setting.
		TheWritableGlobalData->m_useTrees = UserSettings::getTreesEnabled();
	}

	setStaticLODLevel(userSetDetail);
}

void GameLODManager::refreshCustomStaticLODLevel(void)
{
	StaticGameLODInfo *lodInfo=&m_staticGameLODInfo[STATIC_GAME_LOD_CUSTOM];

	lodInfo->m_maxParticleCount=TheGlobalData->m_maxParticleCount;
	lodInfo->m_useShadowVolumes=TheGlobalData->m_useShadowVolumes;
	lodInfo->m_useShadowDecals=TheGlobalData->m_useShadowDecals;
	lodInfo->m_useCloudMap=TheGlobalData->m_useCloudMap;
	lodInfo->m_useLightMap=TheGlobalData->m_useLightMap;
	lodInfo->m_showSoftWaterEdge=TheGlobalData->m_showSoftWaterEdge;
	lodInfo->m_waterQuality=TheGlobalData->m_waterQuality;
	lodInfo->m_maxTankTrackEdges=TheGlobalData->m_maxTankTrackEdges;
	lodInfo->m_maxTankTrackOpaqueEdges=TheGlobalData->m_maxTankTrackOpaqueEdges;
	lodInfo->m_maxTankTrackFadeDelay=TheGlobalData->m_maxTankTrackFadeDelay;
	lodInfo->m_useBuildupScaffolds=!TheGlobalData->m_useDrawModuleLOD;
	lodInfo->m_useHeatEffects = TheGlobalData->m_useHeatEffects;
	lodInfo->m_useTreeSway=lodInfo->m_useBuildupScaffolds;// Borrow same setting. //TheGlobalData->m_useTreeSway;
	lodInfo->m_textureReduction=TheGlobalData->m_textureReductionFactor;
	lodInfo->m_useFpsLimit = TheGlobalData->m_useFpsLimit;
	lodInfo->m_enableDynamicLOD=TheGlobalData->m_enableDynamicLOD;
	lodInfo->m_useTrees = TheGlobalData->m_useTrees;

}

/**Convert LOD name to an index*/
Int GameLODManager::getStaticGameLODIndex(AsciiString name)
{
	for (Int i=0; i<STATIC_GAME_LOD_COUNT; ++i)
	{
		if (name.compareNoCase(StaticGameLODNames[i]) == 0)
			return i;
	}

	// This also runs on names read back from a saved Options.ini ("StaticGameLOD"/"IdealStaticGameLOD"),
	// which may contain a tier name from another game version. Every caller handles
	// STATIC_GAME_LOD_UNKNOWN (setStaticLODLevel() bails out safely), so log instead of asserting.
	DEBUG_LOG(( "GameLODManager::getStaticGameLODIndex - Unrecognized LOD name '%s', ignoring\n", name.str() ));
	return STATIC_GAME_LOD_UNKNOWN;
}

/**Parse a description of all the LOD settings for a given detail level*/
void INI::parseStaticGameLODDefinition( INI* ini )
{
	const char *c;
	AsciiString name;

	// read the name
	c = ini->getNextToken();
	name.set( c );	

	if( TheGameLODManager )
	{
		Int index = TheGameLODManager->getStaticGameLODIndex(name);
		if (index != STATIC_GAME_LOD_UNKNOWN)
		{
			StaticGameLODInfo *lodInfo = &(TheGameLODManager->m_staticGameLODInfo[index]);

			// parse the ini definition
			ini->initFromINI( lodInfo, TheStaticGameLODFieldParseTable );
			TheGameLODManager->m_staticGameLODDefined[index] = TRUE;
		}
	}
}

/**Parse an LOD level*/
void INI::parseStaticGameLODLevel( INI* ini, void * , void *store, const void*)
{
	const char *tok=ini->getNextToken();
	for (Int i=0; i<STATIC_GAME_LOD_COUNT; i++)
		if( stricmp(tok, StaticGameLODNames[i]) == 0 )
		{	*static_cast<StaticGameLODLevel*>(store) = static_cast<StaticGameLODLevel>(i);
			return;
		}

	DEBUG_CRASH(("invalid GameLODLevel token %s -- expected LOW/MEDIUM/HIGH\n",tok));
	throw INI_INVALID_DATA;
}

const char *GameLODManager::getStaticGameLODLevelName(StaticGameLODLevel level)
{
	return StaticGameLODNames[level];
}

// The level whose effect list, terrain filter and render resolution the custom level takes: the one recommended for
// this computer (no settings are written here, unlike findStaticLODLevel).
StaticGameLODLevel GameLODManager::customBaseLevel() const
{
	const Int recommended = ::Engine::recommendDetailLevel(::Engine::VideoMemoryInfo::get()).level;
	return (recommended >= STATIC_GAME_LOD_LOW && recommended <= STATIC_GAME_LOD_VERY_HIGH) ?
		static_cast<StaticGameLODLevel>(recommended) : STATIC_GAME_LOD_VERY_HIGH;
}

/**Function which calculates the recommended LOD level for current hardware
configuration.*/
StaticGameLODLevel GameLODManager::findStaticLODLevel(void)
{
	// By the video memory of the first adapter: integrated graphics (minimum hardware) gets Low, a dedicated card High
	// or VeryHigh (Engine::recommendDetailLevel). Only the first start without a chosen level takes it over.
	if (m_idealDetailLevel == STATIC_GAME_LOD_UNKNOWN)
	{
		const ::Engine::DetailRecommendation recommended = ::Engine::recommendDetailLevel(::Engine::VideoMemoryInfo::get());
		m_idealDetailLevel = static_cast<StaticGameLODLevel>(recommended.level);
		DEBUG_LOG(( "recommended detail level %s: %s\n", getStaticGameLODLevelName(m_idealDetailLevel), recommended.reason ));

		// Remembered for the next start; written with the next save, not at startup.
		UserSettings::set("Detection.IdealStaticGameLOD", getStaticGameLODLevelName(m_idealDetailLevel));
		if (getStaticLODLevel() == STATIC_GAME_LOD_UNKNOWN)
			UserSettings::set("Detail.StaticGameLOD", getStaticGameLODLevelName(m_idealDetailLevel));
	}

	return m_idealDetailLevel;
}

/**Set all game systems to match the desired LOD level.*/
Bool GameLODManager::setStaticLODLevel(StaticGameLODLevel level)
{
	if (!TheGlobalData->m_enableStaticLOD)
	{	m_currentStaticLOD = STATIC_GAME_LOD_CUSTOM; 
		return FALSE;
	}

	if (level == STATIC_GAME_LOD_UNKNOWN || (level != STATIC_GAME_LOD_CUSTOM && m_currentStaticLOD == level))
		return FALSE;	//level is already applied.  Custom levels are always applied since random options could change.

	applyStaticLODLevel(level);
	m_currentStaticLOD = level;

	return TRUE;
}

void GameLODManager::applyStaticLODLevel(StaticGameLODLevel level)
{
///@todo: Still need to implement these settings:
//	m_sampleCount2D=6;
//	m_sampleCount3D=24;
//	m_streamCount=2;
//	m_useEmissiveNightMaterials=TRUE;

	//save previous info for this level since it may be overwritten by refreshCustomStaticLODLevel().
	StaticGameLODInfo prevLodBackup;
	if (m_currentStaticLOD != STATIC_GAME_LOD_UNKNOWN)
		prevLodBackup=m_staticGameLODInfo[m_currentStaticLOD];

	if (level == STATIC_GAME_LOD_CUSTOM)
		refreshCustomStaticLODLevel();	//store current settings into custom preset

	StaticGameLODInfo *lodInfo=&m_staticGameLODInfo[level];
	StaticGameLODInfo *prevLodInfo=&prevLodBackup;

	Int requestedTextureReduction = 0;
	Bool requestedTrees = TRUE;
	if (level == STATIC_GAME_LOD_CUSTOM)
	{	requestedTextureReduction = lodInfo->m_textureReduction;
		requestedTrees = lodInfo->m_useTrees;
	}
	else
	if (level >= STATIC_GAME_LOD_LOW)
	{	//normal non-custom level gets texture reduction based on recommendation
		requestedTextureReduction = getRecommendedTextureReduction();
	}

	if (TheGlobalData)
	{
		TheWritableGlobalData->m_maxParticleCount=lodInfo->m_maxParticleCount;
		TheWritableGlobalData->m_useShadowVolumes=lodInfo->m_useShadowVolumes;
		TheWritableGlobalData->m_useShadowDecals=lodInfo->m_useShadowDecals;

		//Check if texture resolution changed.  No need to apply when current is unknown because display will do it
		if (requestedTextureReduction != m_currentTextureReduction)
		{
				TheWritableGlobalData->m_textureReductionFactor = requestedTextureReduction;
				if (TheGameClient)
					TheGameClient->adjustLOD(0);	//apply the new setting stored in globaldata
		}

		//Check if shadow state changed
		if (m_currentStaticLOD == STATIC_GAME_LOD_UNKNOWN	||
			lodInfo->m_useShadowVolumes != prevLodInfo->m_useShadowVolumes ||
			lodInfo->m_useShadowDecals != prevLodInfo->m_useShadowDecals)
		{
			if (TheGameClient)
			{
				TheGameClient->releaseShadows();	//free all shadows
				TheGameClient->allocateShadows();	//allocate those shadows that are enabled.
			}
		}

		TheWritableGlobalData->m_useCloudMap=lodInfo->m_useCloudMap;
		TheWritableGlobalData->m_useLightMap=lodInfo->m_useLightMap;
		TheWritableGlobalData->m_showSoftWaterEdge=lodInfo->m_showSoftWaterEdge;
		// The water level determines what the shore pass writes to destination alpha, so a change needs the
		// same recalculation as the soft edge.
		const Int prevWaterQuality = TheWritableGlobalData->m_waterQuality;
		const Int waterQuality = UserSettings::getWaterQuality();
		TheWritableGlobalData->m_waterQuality = (waterQuality >= 0) ? waterQuality : lodInfo->m_waterQuality;
		//Check if shoreline blending mode has changed
		if (m_currentStaticLOD == STATIC_GAME_LOD_UNKNOWN || lodInfo->m_showSoftWaterEdge != prevLodInfo->m_showSoftWaterEdge
			|| TheWritableGlobalData->m_waterQuality != prevWaterQuality)
		{
			if (TheTerrainVisual)
				TheTerrainVisual->setShoreLineDetail();
		}

		// Anisotropy of the terrain atlas from the level (the custom level uses the one of the recommended level);
		// -terrainAnisotropy overrides it. Takes effect from the next frame.
		{
			const StaticGameLODInfo &filterInfo = (level == STATIC_GAME_LOD_CUSTOM) ?
				m_staticGameLODInfo[customBaseLevel()] : *lodInfo;
			const Int anisotropy = UserSettings::getTerrainAnisotropy();
			TheWritableGlobalData->m_terrainAnisotropy = (anisotropy >= 0) ? anisotropy : filterInfo.m_terrainAnisotropy;
		}

		// Internal render resolution from the level (the custom level uses the one of the recommended level); Settings.ini
		// "RenderScale" overrides it, -renderScale both. W3DDisplay::draw passes a change to the render device.
		{
			const StaticGameLODInfo &scaleInfo = (level == STATIC_GAME_LOD_CUSTOM) ?
				m_staticGameLODInfo[customBaseLevel()] : *lodInfo;
			Real scale = GlobalData::normalizeRenderScale(scaleInfo.m_renderScale);
			const Real userScale = UserSettings::getRenderScale();
			if (userScale > 0.0f)
				scale = userScale;
			TheWritableGlobalData->m_renderScale = scale;
			const Int userFilter = UserSettings::getRenderScaleFilter();
			if (userFilter >= 0 && !TheGlobalData->m_renderScaleFilterFromCommandLine)
				TheWritableGlobalData->m_renderScaleFilter = userFilter;
		}

		applyRenderEffects(level);

		TheWritableGlobalData->m_maxTankTrackEdges=lodInfo->m_maxTankTrackEdges;
		TheWritableGlobalData->m_maxTankTrackOpaqueEdges=lodInfo->m_maxTankTrackOpaqueEdges;
		TheWritableGlobalData->m_maxTankTrackFadeDelay=lodInfo->m_maxTankTrackFadeDelay;
		TheWritableGlobalData->m_useTreeSway=lodInfo->m_useTreeSway;
		TheWritableGlobalData->m_useDrawModuleLOD=!lodInfo->m_useBuildupScaffolds;
		TheWritableGlobalData->m_useHeatEffects=lodInfo->m_useHeatEffects;
		TheWritableGlobalData->m_enableDynamicLOD = lodInfo->m_enableDynamicLOD;
		TheWritableGlobalData->m_useFpsLimit = lodInfo->m_useFpsLimit;
		TheWritableGlobalData->m_useTrees = requestedTrees;
	}
	if (TheTerrainVisual)
		TheTerrainVisual->setTerrainTracksDetail();

}

namespace
{
// Settings.ini [Effects]: one key per effect, an exception to the level's list. "auto" keeps the list, "off" removes the
// effect, "on" or a value adds it as "name" or "name=value". shadowterrain is a value without a bit of its own.
struct EffectOverride
{
	const char *id;
	const char *token;
	UnsignedInt bit;
};
const EffectOverride EFFECT_OVERRIDES[] = {
	{ "Effects.FXAA", "fxaa", 1u << 0 },
	{ "Effects.Sharpen", "sharpen", 1u << 1 },
	{ "Effects.SoftParticles", "softparticles", 1u << 2 },
	{ "Effects.SSAO", "ssao", 1u << 3 },
	{ "Effects.ColorGrade", "colorgrade", 1u << 4 },
	{ "Effects.ShadowMap", "shadowmap", 1u << 5 },
	{ "Effects.ShadowTerrain", "shadowterrain", 0 },
	{ "Effects.TerrainDetail", "terraindetail", 1u << 6 },
	{ "Effects.TerrainRock", "terrainrock", 1u << 7 },
	{ "Effects.Bloom", "bloom", 1u << 8 },
	{ "Effects.Heat", "heat", 1u << 9 },
	{ "Effects.Haze", "haze", 1u << 10 },
	{ "Effects.Lights", "lights", 1u << 11 },
	{ "Effects.ParticleLight", "particlelight", 1u << 12 },
	{ "Effects.ParticleGround", "particleground", 1u << 13 },
	{ "Effects.TerrainNormal", "terrainnormal", 1u << 14 },
	{ "Effects.TerrainTess", "terraintess", 1u << 15 },
	{ "Effects.RockTess", "rocktess", 1u << 16 },
	{ "Effects.AlphaCoverage", "alphacoverage", 1u << 17 },
	{ "Effects.SoftShadows", "softshadows", 1u << 18 },
	{ "Effects.ContactShadows", "contactshadows", 1u << 19 },
	{ "Effects.ShadowCascades", "shadowcascades", 1u << 20 },
	{ "Effects.MsaaDepth", "msaadepth", 1u << 21 },
	{ "Effects.Deband", "deband", 1u << 23 },
	{ "Effects.WaterSSR", "waterssr", 1u << 24 },
	{ "Effects.GTAO", "gtao", 1u << 25 },
	{ "Effects.TAA", "taa", 1u << 26 },
};

// The strengths a list entry without "=value" leaves as they are; taken once from the engine defaults, so that going
// back to "auto" does not keep a strength an earlier exception set.
struct EffectValues
{
	Real sharpen;
	Int msaa;
	Int shadowMapSize;
	Real shadowTerrain, terrainDetail, terrainRock, bloom, heat, haze, lights, particleLight, terrainNormal, terrainTess, rockTess,
		shadowSoft;
};

EffectValues defaultEffectValues()
{
	static Bool taken = FALSE;
	static EffectValues values;
	if (!taken)
	{
		const GlobalData &g = *TheGlobalData;
		values = EffectValues{ g.m_renderEffectSharpen, g.m_renderEffectMsaa, g.m_renderEffectShadowMapSize, g.m_renderEffectShadowTerrain,
			g.m_renderEffectTerrainDetail, g.m_renderEffectTerrainRock, g.m_renderEffectBloom, g.m_renderEffectHeat, g.m_renderEffectHaze,
			g.m_renderEffectLights, g.m_renderEffectParticleLight, g.m_renderEffectTerrainNormal, g.m_renderEffectTerrainTess,
			g.m_renderEffectRockTess, g.m_renderEffectShadowSoft };
		taken = TRUE;
	}
	return values;
}

Bool parseEffects(const char *list, UnsignedInt *flags, UnsignedInt base, EffectValues &v)
{
	return GlobalData::parseRenderEffects(list, flags, &v.sharpen, &v.msaa, base, &v.shadowMapSize, &v.shadowTerrain, &v.terrainDetail,
		&v.terrainRock, &v.bloom, &v.heat, &v.haze, &v.lights, &v.particleLight, &v.terrainNormal, &v.terrainTess, &v.rockTess,
		&v.shadowSoft);
}
}	// namespace

void GameLODManager::applyRenderEffects(StaticGameLODLevel level)
{
	// D3D11 effects: default of the level from GameLODOverride.ini "D3D11Effects" (the custom level uses the one of the
	// level recommended for this computer, so custom on integrated graphics does not start with every effect on), then
	// Settings.ini "D3D11Effects" (subtracts single effects or replaces the list), then the [Effects] exceptions;
	// -d3d11Effects overrides all of it. W3DDisplay::draw passes a change to the render device.
	if (TheGlobalData == nullptr || TheGlobalData->m_renderEffectsFromCommandLine)
		return;
	if (level < 0 || level >= STATIC_GAME_LOD_COUNT)
		return;
	const StaticGameLODInfo &effectInfo = m_staticGameLODInfo[(level == STATIC_GAME_LOD_CUSTOM) ? customBaseLevel() : level];
	EffectValues v = defaultEffectValues();
	UnsignedInt effectFlags = 0;
	parseEffects(effectInfo.m_d3d11Effects.str(), &effectFlags, 0, v);
	AsciiString userEffects = UserSettings::getD3D11Effects();
	if (userEffects.isNotEmpty())
		parseEffects(userEffects.str(), &effectFlags, effectFlags, v);
	for (const EffectOverride &effect : EFFECT_OVERRIDES)
	{
		const AsciiString choice = UserSettings::get(effect.id);
		if (choice.isEmpty() || choice.compareNoCase("auto") == 0)
			continue;
		if (choice.compareNoCase("off") == 0)
		{
			effectFlags &= ~effect.bit;
			if (effect.bit == (1u << 21))
				effectFlags |= 1u << 28;		// depth effects explicitly off with multisampling
			if (effect.bit == 0)
			{
				AsciiString token;
				token.format("%s=0", effect.token);
				UnsignedInt unused = 0;
				parseEffects(token.str(), &unused, 0, v);
			}
			continue;
		}
		AsciiString token = effect.token;
		if (choice.compareNoCase("on") != 0)
		{
			token.concat("=");
			token.concat(choice);
		}
		UnsignedInt added = 0;
		parseEffects(token.str(), &added, 0, v);
		effectFlags |= added;
	}
	TheWritableGlobalData->m_renderEffects = effectFlags;
	TheWritableGlobalData->m_renderEffectSharpen = v.sharpen;
	TheWritableGlobalData->m_renderEffectMsaa = v.msaa;
	TheWritableGlobalData->m_renderEffectShadowMapSize = v.shadowMapSize;
	TheWritableGlobalData->m_renderEffectShadowTerrain = v.shadowTerrain;
	TheWritableGlobalData->m_renderEffectTerrainDetail = v.terrainDetail;
	TheWritableGlobalData->m_renderEffectTerrainRock = v.terrainRock;
	TheWritableGlobalData->m_renderEffectBloom = v.bloom;
	TheWritableGlobalData->m_renderEffectHeat = v.heat;
	TheWritableGlobalData->m_renderEffectHaze = v.haze;
	TheWritableGlobalData->m_renderEffectLights = v.lights;
	TheWritableGlobalData->m_renderEffectParticleLight = v.particleLight;
	TheWritableGlobalData->m_renderEffectTerrainNormal = v.terrainNormal;
	TheWritableGlobalData->m_renderEffectTerrainTess = v.terrainTess;
	TheWritableGlobalData->m_renderEffectRockTess = v.rockTess;
	TheWritableGlobalData->m_renderEffectShadowSoft = v.shadowSoft;
}

/**Parse a description of all the LOD settings for a given detail level*/
void INI::parseDynamicGameLODDefinition( INI* ini )
{
	const char *c;
	AsciiString name;

	// read the name
	c = ini->getNextToken();
	name.set( c );	

	if( TheGameLODManager )
	{
		Int index = TheGameLODManager->getDynamicGameLODIndex(name);
		if (index != DYNAMIC_GAME_LOD_UNKNOWN)
		{
			DynamicGameLODInfo *lodInfo = &(TheGameLODManager->m_dynamicGameLODInfo[index]);

			// parse the ini weapon definition
			ini->initFromINI( lodInfo, TheDynamicGameLODFieldParseTable );
		}
	}
}

/**Parse an LOD level*/
void INI::parseDynamicGameLODLevel( INI* ini, void * , void *store, const void*)
{
	const char *tok=ini->getNextToken();
	for (Int i=0; i<DYNAMIC_GAME_LOD_COUNT; i++)
		if( stricmp(tok, DynamicGameLODNames[i]) == 0 )
		{	*static_cast<DynamicGameLODLevel*>(store) = static_cast<DynamicGameLODLevel>(i);
			return;
		}

	DEBUG_CRASH(("invalid GameLODLevel token %s -- expected LOW/MEDIUM/HIGH\n",tok));
	throw INI_INVALID_DATA;
}

/**Convert LOD name to an index*/
Int GameLODManager::getDynamicGameLODIndex(AsciiString name)
{
	for (Int i=0; i<DYNAMIC_GAME_LOD_COUNT; ++i)
	{
		if (name.compareNoCase(DynamicGameLODNames[i]) == 0)
			return i;
	}

	// See the identical reasoning in getStaticGameLODIndex() above.
	DEBUG_LOG(( "GameLODManager::getDynamicGameLODIndex - Unrecognized LOD name '%s', ignoring\n", name.str() ));
	return STATIC_GAME_LOD_UNKNOWN;
}

const char *GameLODManager::getDynamicGameLODLevelName(DynamicGameLODLevel level)
{
	return DynamicGameLODNames[level];
}

/**Given an average fps, return the optimal dynamic LOD level that matches this fps.*/
DynamicGameLODLevel GameLODManager::findDynamicLODLevel(Real averageFPS)
{
	Int ifps=static_cast<Int>(averageFPS);	//convert to integer.

	for (Int i=DYNAMIC_GAME_LOD_VERY_HIGH; i>=DYNAMIC_GAME_LOD_LOW; i--)
	{	//check which of the LOD levels matches our fps
		if (m_dynamicGameLODInfo[i].m_minFPS < ifps)
			return static_cast<DynamicGameLODLevel>(i);
	}
	return DYNAMIC_GAME_LOD_LOW;	//none of the low levels were slow enough so pick the lowest.
}

/**Set all game systems to match the desired LOD level.*/
Bool GameLODManager::setDynamicLODLevel(DynamicGameLODLevel level)
{
	if (level == DYNAMIC_GAME_LOD_UNKNOWN || m_currentDynamicLOD == level)
		return FALSE;

	m_currentDynamicLOD = level;

	applyDynamicLODLevel(level);

	return TRUE;
}

void GameLODManager::applyDynamicLODLevel(DynamicGameLODLevel level)
{
	m_numParticleGenerations=0;
	m_dynamicParticleSkipMask=m_dynamicGameLODInfo[level].m_dynamicParticleSkipMask;

	m_numDebrisGenerations=0;
	m_dynamicDebrisSkipMask=m_dynamicGameLODInfo[level].m_dynamicDebrisSkipMask;

	m_slowDeathScale=m_dynamicGameLODInfo[level].m_slowDeathScale;
	m_minDynamicParticlePriority=m_dynamicGameLODInfo[level].m_minDynamicParticlePriority;
	m_minDynamicParticleSkipPriority=m_dynamicGameLODInfo[level].m_minDynamicParticleSkipPriority;
}

Int GameLODManager::getRecommendedTextureReduction(void)
{
	if (m_idealDetailLevel == STATIC_GAME_LOD_UNKNOWN)
		findStaticLODLevel();	//it was never tested, so test now.

	return m_staticGameLODInfo[m_idealDetailLevel].m_textureReduction;
}

Int GameLODManager::getLevelTextureReduction(StaticGameLODLevel level)
{
	return m_staticGameLODInfo[level].m_textureReduction;
}

