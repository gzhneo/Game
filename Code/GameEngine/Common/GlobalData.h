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

// FILE: GlobalData.h /////////////////////////////////////////////////////////////////////////////
// Global data used by both the client and logic
// Author: trolfs, Michae Booth, Colin Day, April 2001
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _GLOBALDATA_H_
#define _GLOBALDATA_H_

#include "Common/GameCommon.h"	// ensure we get DUMP_PERF_STATS, or not
#include "Common/AsciiString.h"
#include "Common/GameType.h"
#include "Common/GameMemory.h"
#include "Common/SubsystemInterface.h"
#include "GameClient/Color.h"
#include "Common/STLTypedefs.h"
#include "Common/GameCommon.h"
#include "Common/Money.h"

// FORWARD DECLARATIONS ///////////////////////////////////////////////////////////////////////////
struct FieldParse;
enum _TerrainLOD : int;	// Explicit underlying type so the forward declaration matches the definition (C4471)
class GlobalData;
class INI;
class WeaponBonusSet;
enum BodyDamageType : int;	// Explicit underlying type so the forward declaration matches the definition (C4471)
enum AIDebugOptions : int;	// Explicit underlying type so the forward declaration matches the definition (C4471)

// PUBLIC /////////////////////////////////////////////////////////////////////////////////////////

const Int MAX_GLOBAL_LIGHTS	= 3;

/// Parts of the new AI that -aiOhne can switch off, to measure each part on its own.
enum AIPart : UnsignedInt
{
	AI_PART_FORECAST_RETREAT	= 0x001,	///< -aiOhne prognose: retreat when the planner forecasts a lost fight
	AI_PART_CONFIRM						= 0x002,	///< -aiOhne bestaetigen: that retreat only after several plans and losses
	AI_PART_REGROW						= 0x004,	///< -aiOhne erstab: after a broken-off attack wait for a stronger force
	AI_PART_LAUNCH_GATE				= 0x008,	///< -aiOhne zuschwach: set out alone only if the target's defenders are not stronger
	AI_PART_PLANNER_TARGET		= 0x010,	///< -aiOhne ziel: attack target from the planner
	AI_PART_COUNTERS					= 0x020,	///< -aiOhne konter: late units by counter score
	AI_PART_RAIDS							= 0x040,	///< -aiOhne ueberfall: raids with transports
	AI_PART_MICRO							= 0x080,	///< -aiOhne gefecht: fight control
	AI_PART_SUPERWEAPON				= 0x100,	///< -aiOhne superwaffe: own superweapon targeting
	AI_PART_GROWTH						= 0x200,	///< -aiOhne wachstum: early expansion, permanent income and hackers as investment
	AI_PART_BUDGET						= 0x400,	///< -aiOhne haushalt: the classic AI's base build list comes before the extra spending
	AI_PART_FACTORIES					= 0x800,	///< -aiOhne fabriken: second and third war factory early
	AI_PART_EARLY_PRODUCTION	= 0x1000,	///< -aiOhne fruehproduktion: own unit purchases from minute 3 instead of the late game
	AI_PART_EARLY_ATTACK			= 0x2000,	///< -aiOhne fruehangriff: smaller attacks in the first minutes
	AI_PART_DEFENCE						= 0x4000,	///< -aiOhne verteidigung: idle units defend the own base, attacks come back earlier
	AI_PART_SCOUTING					= 0x8000,	///< -aiOhne aufklaerung: a scout explores the start positions
	AI_PART_UNKNOWN						= 0x80000000	///< a name -aiOhne did not know
};

/// Off unless -aiMit names them: they did not raise the win rate.
constexpr UnsignedInt AI_PARTS_OFF_BY_DEFAULT = AI_PART_FACTORIES | AI_PART_EARLY_PRODUCTION | AI_PART_EARLY_ATTACK;

//-------------------------------------------------------------------------------------------------
/** Global data container class
  *	Defines all global game data used by the system
	* @todo Change this entire system. Otherwise this will end up a huge class containing tons of variables,
	* and will cause re-compilation dependancies throughout the codebase. 
  * OOPS -- TOO LATE! :) */
//-------------------------------------------------------------------------------------------------
class GlobalData : public SubsystemInterface
{

public:

	GlobalData();
	virtual ~GlobalData() override;

	void init() override;
	void reset() override;
	void update() override { }

	Bool setTimeOfDay( TimeOfDay tod );		///< Use this function to set the Time of day;

	static void parseGameDataDefinition( INI* ini );
	/// Parses the list of D3D11 render effects, e.g. "fxaa,sharpen=0.6,softparticles,ssao,colorgrade" or
	/// "none"/"all" (also "msaa=4" for tests). Sets flags (RENDER_EFFECT_* bits), the sharpness for "sharpen=x"
	/// and the sample count for "msaa=n". base is the default that entries prefixed with "-" subtract from.
	/// "shadowmap[=edge]" sets bit 5 and the shadow map size; "shadowterrain=x" sets the share of terrain
	/// shadows on the terrain (0..1); "terraindetail[=strength]" (also "detailboden") sets bit 6;
	/// "terrainrock[=strength]" (also "berge") sets bit 7; "bloom[=x]", "heat[=x]" (also "hitze") and "haze[=x]"
	/// (also "dunst") set bits 8 to 10 and their strengths; "lights[=x]" (also "dynlights", "lichter") sets bit 11;
	/// "particlelight[=x]" (also "partikellicht") sets bit 12 and its strength (0..1); "particleground" (also
	/// "partikelboden") sets bit 13; "terrainnormal[=x]" (also "relief") sets bit 14 and its strength (0..2);
	/// "terraintess[=x]" (also "tessellation") sets bit 15 and the displacement strength (0..2); "rocktess[=x]" (also
	/// "felstess") sets bit 16 and the strength of the rock displacement (0..2); "alphacoverage" (also "a2c") sets bit 17;
	/// "softshadows[=x]" (also "weicheschatten") sets bit 18 and the light size (0..2); "contactshadows" (also
	/// "kontaktschatten") sets bit 19; "shadowcascades" (also "schattenkaskaden") sets bit 20; "msaadepth" (also
	/// "msaatiefe") sets bit 21 ("-msaadepth" sets bit 28, the depth effects explicitly off with multisampling); "deband" (also "entbaenderung") sets bit 23; "waterssr" (also "wasserspiegelung")
	/// sets bit 24; "gtao" sets bit 25; "taa" sets bit 26; "terrainlight" (also "bodenlicht") sets bit 27; "flatsnow[=x]"
	/// (also "schneeflach") sets bit 29 and the share of snow on flat ground (0..1); "modellight" (also "modelllicht") sets
	/// bit 30; "heightfog[=x]" (also "hoehennebel") and "sunshafts[=x]" (also "lichtstrahlen") set bit 31 and their
	/// strength (0..2, 1 without a value).
	static Bool parseRenderEffects( const char *list, UnsignedInt *flags, Real *sharpen, Int *msaa, UnsignedInt base = 0, Int *shadowMapSize = nullptr,
		Real *shadowTerrain = nullptr, Real *terrainDetail = nullptr, Real *terrainRock = nullptr,
		Real *bloom = nullptr, Real *heat = nullptr, Real *haze = nullptr, Real *lights = nullptr, Real *particleLight = nullptr,
		Real *terrainNormal = nullptr, Real *terrainTess = nullptr, Real *rockTess = nullptr, Real *shadowSoft = nullptr,
		Real *flatSnow = nullptr, Real *heightFog = nullptr, Real *sunShafts = nullptr );
	// Maps the brightness slider position 0..100 to gamma 0.6..2.0 (50 = 1.0), like the options menu.
	static Real clampMaxZoomFactor( Real factor );	///< limit of Camera.MaxZoomFactor and -maxZoom, notes a clamped value in the logs
	static Real gammaFromSlider( Int val );
	// Internal render resolution: a share (0.5..1) or percent (50..100) to the share, clamped; anything else is 1.
	static Real normalizeRenderScale( Real value );
	// Upscale filter name ("bilinear", "bicubic", "cas" or 0..2) to 0..2, -1 if unknown.
	static Int parseRenderScaleFilter( const char *name );

	//-----------------------------------------------------------------------------------------------
	struct TerrainLighting
	{
		RGBColor ambient;
		RGBColor diffuse;
		Coord3D lightPos;
	};

	//-----------------------------------------------------------------------------------------------
	//-----------------------------------------------------------------------------------------------
	//-----------------------------------------------------------------------------------------------

	AsciiString m_mapName;  ///< hack for now, this whole this is going away
	AsciiString m_moveHintName;
	Bool m_useTrees;
	Bool m_useTreeSway;
	Bool m_useDrawModuleLOD;
	Bool m_useHeatEffects;
	Bool m_useFpsLimit;
	Bool m_dumpAssetUsage;
	Int m_framesPerSecondLimit;
	Int	m_chipSetType;	///< Read from INI "ChipsetType" and set by -noshaders, but not evaluated
	Bool m_windowed;
	Bool m_borderless;	///< -borderless: borderless window at desktop resolution
	Int m_fullscreenAreaWidth;	///< -fullscreenArea: fullscreen covers only this centred area (tests)
	Int m_fullscreenAreaHeight;
	Int m_xResolution;
	Int m_yResolution;
	Int m_maxShellScreens;  ///< this many shells layouts can be loaded at once
	Bool m_useCloudMap;
	Int  m_use3WayTerrainBlends;	///< 0 is none, 1 is normal, 2 is debug.
	Bool m_useLightMap;
	Bool m_bilinearTerrainTex;
	Bool m_trilinearTerrainTex;
	Bool m_multiPassTerrain;
	Bool m_adjustCliffTextures;
	Bool m_stretchTerrain;
	Bool m_useHalfHeightMap;
	Bool m_drawEntireTerrain;
	_TerrainLOD m_terrainLOD;
	Bool m_enableDynamicLOD;
	Bool m_enableStaticLOD;
	Int m_terrainLODTargetTimeMS;
	Bool m_useAlternateMouse;
	Bool m_clientRetaliationModeEnabled;
	// Switches for logic differences between the two games. Default per game in the constructor,
	// can be overridden in GameData.ini.
	Bool m_empSparesAlliedTransports;				///< EMP spares allied transport aircraft instead of checking EMP_HARDENED (Generals)
	Bool m_weaponChoiceLikeGenerals;				///< Weapon choice only among weapons in range; hero KILLPILOT without lock (Generals)
	Bool m_weaponBonusRestartsReload;				///< Changing weapon bonuses restarts reload/shot delay (Zero Hour)
	Bool m_dronesNeverSelectable;						///< Objects with KINDOF_DRONE are never selectable (Generals)
	Bool m_groupMovesClampedToMap;					///< Group orders clamp their target to the map edge (Zero Hour)
	Bool m_battlePlanRevealsStrategyCenter;	///< Changing battle plan reveals the strategy center via a viewer object (Generals)
	Bool m_oclUpdatePausedWhenDisabled;			///< OCLUpdate timer pauses while the object is disabled (Zero Hour)
	// Script actions and conditions that Zero Hour evaluates differently behave as in Generals
	// (ScriptActions.cpp, ScriptConditions.cpp). Default from GameEdition (on for Generals only),
	// GameData.ini "ScriptsLikeGenerals".
	Bool m_scriptsLikeGenerals;
	Bool m_mapFluffAsClientProps;					///< Decorative map objects become client props depending on detail level/multiplayer (Zero Hour)
	Bool m_doubleClickAttackMove;
	Bool m_rightMouseAlwaysScrolls;
	Bool m_useWaterPlane;
	Bool m_useCloudPlane;
	Bool m_useShadowVolumes;
	Bool m_useShadowDecals;
	Int  m_textureReductionFactor;	//how much to cut texture resolution: 2 is half, 3 is quarter, etc.
	Bool m_enableBehindBuildingMarkers;
	Real m_waterPositionX;
	Real m_waterPositionY;
	Real m_waterPositionZ;
	Real m_waterExtentX;	
	Real m_waterExtentY;
	Int	m_waterType;
	Bool m_showSoftWaterEdge;
	// Water: 0 = EA water, 1 = turbidity and Fresnel, 2 = plus refraction. Comes from the detail level
	// (WaterQuality); the setting Detail.WaterQuality (-waterQuality) overrides the level.
	Int m_waterQuality;
	// Water color texture: 0 = the texture named in Water.ini/map.ini, 1 = the free tiles from
	// ProjectData\Textures\Wasser (default). -1 until set by -waterTexture ea|free or Options.ini "WaterTexture".
	Int m_waterTexture;
	// Free content packages (ProjectData\Inhalt): 0 = off (game data only), 1 = loaded (default). -1 until set by
	// -freeContent ea|free or Options.ini "FreeContent"; fixed by ArchiveFileSystem::loadFreeContent.
	Int m_freeContent;
	// Anisotropy for the terrain atlas (0 = trilinear as in EA, otherwise 2..16, limited by RenderCaps).
	// From the detail level (TerrainAnisotropy); the setting Detail.TerrainAnisotropy (-terrainAnisotropy)
	// overrides the level.
	Int m_terrainAnisotropy;
	// Mip LOD bias of the terrain textures (atlas, detail and rock layer); negative selects finer mips and
	// sharpens the near ground, 0 as in EA. Set by -texLodBias.
	Real m_terrainLodBias;
	Bool m_usingWaterTrackEditor;
	Bool m_isWorldBuilder;
	// The new map editor draws its view like the game (realistic water); the old WorldBuilder keeps
	// the classic water. Only meaningful together with m_isWorldBuilder.
	Bool m_editorGameGraphics;

	Int m_featherWater;

	// these are for WATER_TYPE_3 vertex animated water
	enum { MAX_WATER_GRID_SETTINGS = 4 };
	AsciiString m_vertexWaterAvailableMaps[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterHeightClampLow[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterHeightClampHi[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterAngle[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterXPosition[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterYPosition[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterZPosition[ MAX_WATER_GRID_SETTINGS ];
	Int m_vertexWaterXGridCells[ MAX_WATER_GRID_SETTINGS ];
	Int m_vertexWaterYGridCells[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterGridSize[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterAttenuationA[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterAttenuationB[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterAttenuationC[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterAttenuationRange[ MAX_WATER_GRID_SETTINGS ];

	Real m_downwindAngle;
	Real m_skyBoxPositionZ;
	Real m_drawSkyBox;
	Real m_skyBoxScale;
	Real m_cameraPitch;
	Real m_cameraYaw;
	Real m_cameraHeight;
	Real m_maxCameraHeight;
	Real m_minCameraHeight;
	// Settings.ini Camera.MaxZoomFactor or -maxZoom: the limit of the player's free camera as a factor of
	// m_maxCameraHeight, 1 to 1.25 (1 = unchanged; 0 until read at startup, treated as 1).
	Real m_maxZoomFactor;
	Real m_terrainHeightAtEdgeOfMap;
	Real m_unitDamagedThresh;
	Real m_unitReallyDamagedThresh;
	Real m_groundStiffness;
	Real m_structureStiffness;
	Real m_gravity;	// acceleration due to gravity, in dist/frame^2
	Real m_stealthFriendlyOpacity;
	UnsignedInt m_defaultOcclusionDelay;	///<time to delay building occlusion after object is created.

	Bool m_preloadAssets;
	Bool m_preloadEverything;			///< Preload everything, everywhere (for debugging only)
	Bool m_preloadReport;					///< dump a log of all W3D assets that are being preloaded.

	Real m_partitionCellSize;

	Coord3D m_ammoPipWorldOffset;
	Coord3D m_containerPipWorldOffset;
	Coord2D m_ammoPipScreenOffset;
	Coord2D m_containerPipScreenOffset;
	Real m_ammoPipScaleFactor;
	Real m_containerPipScaleFactor;

	UnsignedInt m_historicDamageLimit;

	//Settings for terrain tracks left by vehicles with treads or wheels
	Int m_maxTerrainTracks; ///<maximum number of objects allowed to generate tracks.
	Int m_maxTankTrackEdges;	///<maximum length of tank track
	Int m_maxTankTrackOpaqueEdges;	///<maximum length of tank track before it starts fading.
	Int m_maxTankTrackFadeDelay;	///<maximum amount of time a tank track segment remains visible.
	
	AsciiString m_levelGainAnimationName; ///< The animation to play when a level is gained.
	Real m_levelGainAnimationDisplayTimeInSeconds;		///< time to play animation for
	Real m_levelGainAnimationZRisePerSecond;					///< rise animation up while playing

	AsciiString m_getHealedAnimationName; ///< The animation to play when emergency repair does its thing.
	Real m_getHealedAnimationDisplayTimeInSeconds;		///< time to play animation for
	Real m_getHealedAnimationZRisePerSecond;					///< rise animation up while playing

	TimeOfDay	m_timeOfDay;
	Weather m_weather;
	Bool m_makeTrackMarks;
	Bool m_hideGarrisonFlags;
	Bool m_forceModelsToFollowTimeOfDay;
	Bool m_forceModelsToFollowWeather;

	TerrainLighting	m_terrainLighting[TIME_OF_DAY_COUNT][MAX_GLOBAL_LIGHTS];
	TerrainLighting	m_terrainObjectsLighting[TIME_OF_DAY_COUNT][MAX_GLOBAL_LIGHTS];

	//Settings for each global light
	RGBColor m_terrainAmbient[MAX_GLOBAL_LIGHTS];
	RGBColor m_terrainDiffuse[MAX_GLOBAL_LIGHTS];
	Coord3D m_terrainLightPos[MAX_GLOBAL_LIGHTS];

	Real m_infantryLightScale[TIME_OF_DAY_COUNT];
	Real m_scriptOverrideInfantryLightScale;

	Real m_soloPlayerHealthBonusForDifficulty[PLAYERTYPE_COUNT][DIFFICULTY_COUNT];

	Int m_maxVisibleTranslucentObjects;
	Int m_maxVisibleOccluderObjects;
	Int m_maxVisibleOccludeeObjects;
	Int m_maxVisibleNonOccluderOrOccludeeObjects;
	Real m_occludedLuminanceScale;

	Int m_numGlobalLights;	//number of active global lights
	Int m_maxRoadSegments;
	Int m_maxRoadVertex;
	Int m_maxRoadIndex;
	Int m_maxRoadTypes;

	Bool m_audioOn;
	Bool m_musicOn;
	Bool m_soundsOn;
	Bool m_sounds3DOn;
	Bool m_speechOn;
	Bool m_videoOn;
	Bool m_disableCameraMovement;

	Bool m_useFX;									///< If false, don't render effects
	Bool m_showClientPhysics;
	Bool m_showTerrainNormals;

	UnsignedInt m_noDraw;					///< Used to disable drawing, to profile game logic code.
	// Replay runner and automatic skirmish start, in all build configurations (measurements use the
	// release/profile build)
	Bool m_runnerMode;											///< -runner: no drawing/sound, logic ticks unthrottled
	AsciiString m_crcLogFile;								///< -crcLog: file for "Frame CRC" lines
	AsciiString m_modelCheckFile;							///< -modellPruefung: load every model and DDS, write the result there, quit
	Int m_crcLogInterval;										///< -crcLogInterval: log the CRC every N logic frames
	Int m_deepCRCFrameFirst;								///< -deepCRCFrames: deep CRC dump (file + .idx) from this logic frame (-1 = off)
	Int m_deepCRCFrameLast;									///< -deepCRCFrames: ... up to and including this logic frame
	Bool m_deepCRCFull;											///< -deepCRCFull: deep CRC dump with full xfer() state per object instead of crc() only
	UnsignedInt m_maxFrames;								///< -maxFrames: end the match after N logic frames (0 = never)
	AsciiString m_autoSkirmishMap;					///< -skirmish: map for a skirmish without menu
	Int m_autoSkirmishAIs;									///< -skirmishAI: number of AI opponents (brutal)
	Int m_autoSkirmishSeed;									///< -skirmishSeed: game seed
	Int m_autoSkirmishGraceMinutes;					///< -schonzeit N: start protection of the auto skirmish in minutes
	Bool m_autoSkirmishSuperweaponLimit;		///< -superwaffenlimit: the auto skirmish limits superweapons
	Int m_autoSkirmishStartingCash;					///< -startkapital N: starting cash of the auto skirmish (0 = default)
	AsciiString m_autoSkirmishSpec;					///< -skirmishAI as a per-slot list, e.g. "N1,N1,K2,K2"
	Int m_aiStatsInterval;									///< -aiStats N: KISTAT every N logic frames, ends on victory (0 = off)
	UnsignedInt m_aiPartsOff;								///< AIPart bits switched off: AI_PARTS_OFF_BY_DEFAULT, plus -aiOhne, minus -aiMit (not stored in replays)
	Bool m_blockedDeadlockFix;							///< vehicles blocked by moving vehicles for seconds get a new path (off with -noDeadlockFix)
	UnsignedInt m_blockedDeadlockFixFrame;			///< -deadlockFixFrom N: the fix applies from logic frame N on (keeps a replay in sync up to there)
	AsciiString m_recordReplayFile;					///< -recordReplay: copy the recorded replay here at the end
	Int m_affinityLogicalProcessors;				///< -affinity: restrict the process to the first N logical processors (0 = all)
	Bool m_audioLog;												///< -audioLog: one line per audio request, cull and playback into the runner log

	// Network test aids, all build configurations
	Int m_netEmuLatency;									///< -netzLatenz: delay of received game packets in ms
	Int m_netEmuJitter;										///< -netzSchwankung: additional random delay 0..N ms
	Int m_netEmuLoss;											///< -netzVerlust: percent of received packets dropped
	Int m_netEmuDuplicate;								///< -netzDoppelt: percent of received packets delivered twice
	Int m_netEmuReorder;									///< -netzUmordnung: percent of received packets held back behind later ones
	UnsignedInt m_netEmuSeed;							///< -netzSaat: seed of the emulator decisions (0 = from the process ID)
	Bool m_settingsReadOnly;							///< -einstellungenNurLesen: Settings.ini and State.ini are not written
	Int m_netLocalInstance;								///< -netzLokal N (1..8): LAN on loopback address 127.0.0.N, several instances on one machine
	Bool m_netNoPortMapping;							///< -netzOhnePortfreigabe: do not ask the router to forward the game's ports
	AsciiString m_netRendezvous;					///< -treffpunkt host[:port] (repeatable, ';'-separated): rendezvous servers tried first
	Bool m_netNoRendezvous;								///< -netzOhneTreffpunkt: no rendezvous servers (no invitation codes, no relay)
	Bool m_netForceRelay;									///< -netzNurRelais: every way between the game sockets goes through the relay (tests)
	Int m_desyncReportFrame;							///< -desyncBerichtBei N: write a desync report at logic frame N (test of the report)
	Bool m_noRealInput;										///< -echteEingabeAus: real mouse and keyboard input is ignored (image and input tests)
	Bool m_testRealInput;									///< a test script command sends window messages through the real input path
	Int m_monitorIndex;										///< -bildschirm N: open on monitor N (1 = primary); 0: the monitor of the last run
	Int m_watchTestStart;									///< -zuschauerTest N: play -replay as a watched game that is N frames ahead at first and then grows in bursts

	// Test helpers
	Bool m_testAids;											///< -testhilfen or Options.ini "Testhilfen = yes": test helpers enabled (single player only)
	AsciiString m_testAidsScript;					///< -testhilfenAblauf: file of "logic frame command [player]" lines (replay test of the test helpers)
	AsciiString m_testAidsMission;				///< -mission: campaign (e.g. USA, GLA, China)
	Int m_testAidsMissionNr;							///< -mission: mission number, starting at 1
	// Image comparison and render baseline, all build configurations
	enum { MAX_SHOT_FRAMES = 32 };
	UnsignedInt m_shotAfterFrames[MAX_SHOT_FRAMES];	///< -shotAfter: backbuffer screenshot after N client frames (repeatable)
	Int m_shotAfterCount;										///< Number of valid entries in m_shotAfterFrames
	AsciiString m_shotDir;									///< -shotDir: target folder of the screenshots (empty = working directory)
	UnsignedInt m_shotEvery;								///< -shotEvery: in game, a screenshot every N logic frames (0 = off)
	UnsignedInt m_shotEveryDraw;							///< -shotEveryDraw: a screenshot every N drawn frames of the process, menus included (0 = off)
	Bool m_logActions;										///< -logActions: runner log lines for destroyed objects and new particle systems
	AsciiString m_cameraPathFile;						///< -cameraPath: camera path for image comparisons (GameClient/CameraPath.h)
	AsciiString m_testScriptFile;						///< -testablauf: script for unattended tests (GameClient/TestScript.h)
	UnsignedInt m_quitAfterFrames;					///< -quitAfter: quit after N client frames, also in the menu (0 = never)
	Bool m_noVSync;												///< -noVSync: present without vertical sync (performance measurement)
	AsciiString m_cursorLogFile;					///< -logCursor: cursor changes and images with time, a summary per second
	AsciiString m_renderTimeLogFile;				///< -logRenderTime: presentation time of each shown frame as CSV
	Bool m_fixedTimeStep;										///< -fixedTimeStep: one tick per loop pass, virtual render clock, fixed client seed
	// Render backend: Settings.ini [Display] RenderBackend, -vulkan and -d3d11 override it for the run
	// (UserSettings::useVulkanRenderer). -d3d9 is still recognized so W3DDisplay can report it as unsupported.
	Bool m_renderBackendD3D9Requested;		///< -d3d9 (-d3d11 and -vulkan clear it)
	Bool m_depthFloat;						///< -depthD32: scene depth as 32-bit float (as Vulkan on AMD), for comparisons
	// D3D11 render effects. Source: the detail level (GameLODOverride.ini "D3D11Effects"); Options.ini
	// "D3D11Effects" subtracts from it or replaces the list; -d3d11Effects overrides both
	// (GameLODManager::applyStaticLODLevel, parseRenderEffects).
	UnsignedInt m_renderEffects;						///< RENDER_EFFECT_* bits (WW3D2/renderdevice.h)
	Real m_renderEffectSharpen;							///< CAS sharpness 0..1
	Int m_renderEffectMsaa;								///< -1 = Options.ini "AntiAliasing", otherwise sample count (comparison runs)
	Int m_msaaSamples;									///< -msaa n: sample count over all other sources (test runs), -1 = not set
	Int m_renderEffectShadowMapSize;					///< Edge length of the shadow map
	Real m_renderEffectShadowTerrain;					///< Share of terrain shadows on the terrain
	Real m_renderEffectTerrainDetail;					///< Strength of the terrain detail layer (0..2)
	Real m_renderEffectTerrainRock;						///< Strength of the rock layer on steep slopes (0..1)
	Real m_renderEffectBloom;							///< Bloom strength (0..2)
	Real m_renderEffectHeat;							///< Heat shimmer amplitude (0..2)
	Real m_renderEffectHaze;							///< Distance haze strength (0..1)
	Real m_renderEffectLights;							///< Dynamic light strength (0..2)
	Real m_renderEffectParticleLight;					///< Particle lighting strength (0..1)
	Real m_renderEffectTerrainNormal;					///< Strength of the terrain relief (normal maps and gloss, 0..2)
	Real m_renderEffectTerrainTess;						///< Displacement strength of the terrain tessellation (0..2, 0 = no displacement)
	Real m_renderEffectRockTess;						///< Strength of the rock displacement on steep slopes (0..2)
	Real m_renderEffectShadowSoft;						///< Soft shadows: light size (0..2)
	Real m_renderEffectFlatSnow;						///< Share of snow on flat ground (0..1)
	Real m_renderEffectHeightFog;						///< Height fog density (0..2, 0 = off)
	Real m_renderEffectSunShafts;						///< Sun shaft strength (0..2, 0 = off)
	Real m_terrainTessMax;								///< Largest tessellation factor (-terrainTessMax, 1..64)
	Real m_terrainTessPixels;							///< Target screen length of a tessellated edge piece (-terrainTessPixels, 2..64)
	Int m_terrainTessProbe;								///< Crack probe: tessellated terrain green on red (-terrainTessProbe)
	Bool m_noConstantBufferRing;						///< One DISCARD per constant buffer and draw instead of the ring (-noConstantBufferRing, comparisons)
	Bool m_noMeshInstancing;						///< every mesh drawn on its own, no instanced draws (-noMeshInstancing, comparisons)
	Bool m_simThread;										///< logic tick on the simulation thread, main thread waits (-simThread)
	Bool m_cpuParticles;							///< Particle quads built on the CPU as in EA's code (-cpuParticles, comparisons)
	Bool m_cpuSkinning;								///< Skinned meshes deformed on the CPU as in EA's code (-cpuSkinning, comparisons)
	Bool m_cpuTerrainLights;							///< Terrain prelighting and dynamic lights on the CPU as in EA's code (-cpuTerrainLights, comparisons)
	Bool m_renderEffectsFromCommandLine;
	// Internal render resolution of the 3D scene. m_renderScale is the share in use: the detail level
	// (GameLODOverride.ini RenderScale), the setting Display.RenderScale (-renderScale) overrides it.
	// Filter 0 bilinear, 1 bicubic (Catmull-Rom), 2 Catmull-Rom with CAS sharpening.
	Real m_renderScale;
	Int m_renderScaleFilter;
	Bool m_renderScaleFilterFromCommandLine;
	Real m_renderScaleSharpen;							///< sharpness of the CAS upscale (0..1)
	Real m_hdrKnee;										///< -hdrKnee: knee of the HDR tone curve (0.6..0.95)
	Real m_hdrNight;									///< -hdrNight: HDR exposure on night maps
	Real m_hdrAdditive;									///< -hdrAdditive: HDR factor for additively blended draws
	Int m_sceneFormat;									///< -sceneFormat: scene target format (SCENE_FORMAT_* of the renderer)
	Bool m_gpuTiming;									///< GPU timestamps also without effects (-gpuTiming)
	// -logTextureStats N writes the loaded textures (count, memory) to the runner log every N client frames,
	// plus atlas height and load time (0 = off).
	UnsignedInt m_logTextureStats;
	// -logDrawStats N writes draw calls, triangles and vertices per drawn image, averaged over N client frames,
	// with zoom and camera height to the runner log (0 = off).
	UnsignedInt m_logDrawStats;
	// -logUploads first last: hashes of the render backend's uploads and draws to the runner log, a summary per
	// drawn image in a game and one line per upload and draw for the logic frames first..last.
	Bool m_logUploads;
	UnsignedInt m_logUploadsFirst;
	UnsignedInt m_logUploadsLast;
	// -logCamera: per drawn image in a game the camera's clearance above the logic terrain to the runner log.
	Bool m_logCamera;
	// Smallest height of the camera eye and its near plane above the terrain (-cameraClearance, 0 = no limit);
	// the tessellation displacement comes on top.
	Real m_cameraClearance;
	// -fillUnwritten 0..255: the render backend presets memory it hands out unwritten with this byte (-1 = off).
	Int m_fillUnwritten;
	// Texture tier. m_textureQuality is the request from -textureQuality (Engine::TEXTURE_TIER_UNSET = no switch,
	// then Options.ini "TextureQuality" applies), m_textureTier the tier chosen at startup (0 = SD, 1 = HD,
	// 2 = UHD); it is fixed from loadTextureTiers on.
	Int m_textureQuality;
	Int m_textureTier;
	Int m_workerThreads;											///< -workers/Options.ini WorkerThreads: -1 = automatic, 0 = serial (reference mode)
	Bool m_parallelCRC;											///< -parallelCRC: compute the object CRC in parallel blocks (changes the CRC value)
	UnsignedInt m_crcReportFrame;						///< -crcReport: write a state report in this frame (0 = never)
	AsciiString m_crcReportFile;						///< -crcReport <file>
	AIDebugOptions m_debugAI;			///< Used to display AI debug information
	Bool m_debugSupplyCenterPlacement; ///< Dumps to log everywhere it thinks about placing a supply center
	Bool m_debugAIObstacles;			///< Used to display AI obstacle debug information
	Bool m_showObjectHealth;			///< debug display object health
	Bool m_scriptDebug;						///< Should we attempt to load the script debugger window (.DLL)
	Bool m_displayDebug;					///< Used to display display debug info
	Bool m_winCursors;						///< Should we force use of windows cursors?
	Bool m_constantDebugUpdate;		///< should we update the debug stats constantly, vs every 2 seconds?
	Bool m_showTeamDot;						///< Shows the little colored team dot representing which team you are controlling.
	
#ifdef DUMP_PERF_STATS
	Bool m_dumpPerformanceStatistics;
  Bool  m_dumpStatsAtInterval;///< should I automatically dum stats every in N frames
  Int   m_statsInterval;       ///< if so, how many is N?
#endif
	

	Int m_fixedSeed;							///< fixed random seed for game logic (less than 0 to disable)

	Real m_particleScale;					///< Global size modifier for particle effects.

	AsciiString m_autoFireParticleSmallPrefix;
	AsciiString m_autoFireParticleSmallSystem;
	Int m_autoFireParticleSmallMax;
	AsciiString m_autoFireParticleMediumPrefix;
	AsciiString m_autoFireParticleMediumSystem;
	Int m_autoFireParticleMediumMax;
	AsciiString m_autoFireParticleLargePrefix;
	AsciiString m_autoFireParticleLargeSystem;
	Int m_autoFireParticleLargeMax;
	AsciiString m_autoSmokeParticleSmallPrefix;
	AsciiString m_autoSmokeParticleSmallSystem;
	Int m_autoSmokeParticleSmallMax;
	AsciiString m_autoSmokeParticleMediumPrefix;
	AsciiString m_autoSmokeParticleMediumSystem;
	Int m_autoSmokeParticleMediumMax;
	AsciiString m_autoSmokeParticleLargePrefix;
	AsciiString m_autoSmokeParticleLargeSystem;
	Int m_autoSmokeParticleLargeMax;
	AsciiString m_autoAflameParticlePrefix;
	AsciiString m_autoAflameParticleSystem;
	Int m_autoAflameParticleMax;

	// Latency insertion, packet loss for network debugging
	Int m_netMinPlayers;					///< Min players needed to start a net game

	UnsignedInt m_defaultIP;			///< address for network games (setting Network.IPAddress); 0: every adapter
	// GameData.ini may still name these; nothing reads them since the online service is gone
	UnsignedInt m_firewallBehavior;
	UnsignedInt m_firewallPortOverride;
	Short m_firewallPortAllocationDelta;

	Int m_baseValuePerSupplyBox;
	Real m_BuildSpeed;
	Real m_MinDistFromEdgeOfMapForBuild;
	Real m_SupplyBuildBorder;
	Real m_allowedHeightVariationForBuilding;  ///< how "flat" is still flat enough to build on
	Real m_MinLowEnergyProductionSpeed;
	Real m_MaxLowEnergyProductionSpeed;
	Real m_LowEnergyPenaltyModifier;
	Real m_MultipleFactory;
	Real m_RefundPercent;

	Real m_commandCenterHealRange;		///< radius in which close by ally things are healed
	Real m_commandCenterHealAmount;   ///< health per logic frame close by things are healed
	Int m_maxLineBuildObjects;				///< line style builds can be no longer than this
	Int m_maxTunnelCapacity;					///< Max people in Player's tunnel network
	Real m_horizontalScrollSpeedFactor;	///< Factor applied to the game screen scrolling speed.
	Real m_verticalScrollSpeedFactor;		///< Seperated because of our aspect ratio
	Real m_scrollAmountCutoff;				///< Scroll speed to not adjust camera height
	Real m_cameraAdjustSpeed;					///< Rate at which we adjust camera height
	Bool m_enforceMaxCameraHeight;		///< Enfoce max camera height while scrolling?
	Bool m_buildMapCache;
	AsciiString m_initialFile;				///< If this is specified, load a specific map/replay from the command-line
	AsciiString m_pendingFile;				///< If this is specified, use this map at the next game start

	Int m_maxParticleCount;						///< maximum number of particles that can exist
	Int m_maxFieldParticleCount;			///< maximum number of field-type particles that can exist (roughly)
	WeaponBonusSet* m_weaponBonusSet;
	Real m_healthBonus[LEVEL_COUNT];			///< global bonuses to health for veterancy.
	Real m_defaultStructureRubbleHeight;	///< for rubbled structures, compress height to this if none specified

	AsciiString m_shellMapName;				///< Holds the shell map name
	Bool m_shellMapOn;								///< User can set the shell map not to load
	Bool m_playIntro;									///< Flag to say if we're to play the intro or not
	Bool m_playSizzle;								///< Flag to say whether we play the sizzle movie after the logo movie.
	Bool m_afterIntro;								///< we need to tell the game our intro is done
	Bool m_allowExitOutOfMovies;			///< flag to allow exit out of movies only after the Intro has played

	Bool m_loadScreenRender;						///< flag to disallow rendering of almost everything during a loadscreen

	Real m_keyboardScrollFactor;			///< Factor applied to game scrolling speed via keyboard scrolling
	Real m_keyboardDefaultScrollFactor;			///< Factor applied to game scrolling speed via keyboard scrolling
	Real m_scrollFactorKeyboard;		///< Settings.ini ScrollSpeedKeyboard / 100, or -1: use m_keyboardScrollFactor
	Real m_scrollFactorEdge;				///< Settings.ini ScrollSpeedEdge / 100, or -1: use m_keyboardScrollFactor
	Real m_scrollFactorDrag;				///< Settings.ini ScrollSpeedDrag / 100, or -1: use m_keyboardScrollFactor
	Bool m_edgeScrollInWindow;			///< Settings.ini EdgeScrollInWindow: edge scrolling also in a window
	
  Real m_musicVolumeFactor;         ///< Factor applied to loudness of music volume
  Real m_SFXVolumeFactor;           ///< Factor applied to loudness of SFX volume
  Real m_voiceVolumeFactor;         ///< Factor applied to loudness of voice volume
  Bool m_3DSoundPref;               ///< Whether user wants to use 3DSound or not

	Bool m_animateWindows;						///< Should we animate window transitions?

	Bool m_incrementalAGPBuf;
	
	UnsignedInt m_iniCRC;							///< CRC of important INI files
	UnsignedInt m_exeCRC;							///< CRC of the executable

	BodyDamageType m_movementPenaltyDamageState;	///< at this body damage state, we have movement penalties

	Int m_groupSelectMinSelectSize;		// min number of units to treat as group select for audio feedback
	Real m_groupSelectVolumeBase;			// base volume for group select sound
	Real m_groupSelectVolumeIncrement;// increment to volume for selecting more units
	Int m_maxUnitSelectSounds;				// max number of select sounds to play per selection

	Real m_selectionFlashSaturationFactor; /// how colorful should the selection flash be? 0-4
	Bool m_selectionFlashHouseColor ;  /// skip the house color and just use white.

	Real m_cameraAudibleRadius;				///< If the camera is being used as the position of audio, then how far can we hear?
	Real m_groupMoveClickToGatherFactor; /** if you take all the selected units and calculate the smallest possible rectangle 
																			 that contains them all, and click within that, all the selected units will break 
																			 formation and gather at the point the user clicked (if the value is 1.0). If it's 0.0,
																			 units will always keep their formation. If it's <1.0, then the user must click a 
																			 smaller area within the rectangle to order the gather. */

	Int m_antiAliasBoxValue;          ///< value of selected antialias from combo box in options menu
	Bool m_languageFilterPref;        ///< Bool if user wants to filter language
	Bool m_loadScreenDemo;						///< Bool if true, run the loadscreen demo movie
	Bool m_disableRender;							///< if true, no rendering!

	Bool m_saveCameraInReplay;
	Bool m_useCameraInReplay;

	Real m_shakeSubtleIntensity;			///< Intensity for shaking a camera with SHAKE_SUBTLE
	Real m_shakeNormalIntensity;			///< Intensity for shaking a camera with SHAKE_NORMAL
	Real m_shakeStrongIntensity;			///< Intensity for shaking a camera with SHAKE_STRONG
	Real m_shakeSevereIntensity;			///< Intensity for shaking a camera with SHAKE_SEVERE
	Real m_shakeCineExtremeIntensity; ///< Intensity for shaking a camera with SHAKE_CINE_EXTREME
	Real m_shakeCineInsaneIntensity;  ///< Intensity for shaking a camera with SHAKE_CINE_INSANE
	Real m_maxShakeIntensity;					///< The maximum shake intensity we can have
	Real m_maxShakeRange;							///< The maximum shake range we can have

	Real m_sellPercentage;						///< when objects are sold, you get this much of the cost it would take to build it back
	Real m_baseRegenHealthPercentPerSecond;	///< auto healing for bases
	UnsignedInt m_baseRegenDelay;			///< delay in frames we must be damage free before we can auto heal

	Color m_hotKeyTextColor;					///< standard color for all hotkeys.

  //THis is put on ice until later - M Lorenzen
  //	Int m_cheaterHasBeenSpiedIfMyLowestBitIsTrue; ///< says it all.. this lives near other "colors" cause it is masquerading as one
	
	AsciiString m_specialPowerViewObjectName;	///< Created when certain special powers are fired so players can watch.

	std::vector<AsciiString> m_standardPublicBones;

	Real m_standardMinefieldDensity;
	Real m_standardMinefieldDistance;

	
	Bool  m_showMetrics;								///< whether or not to show the metrics.
	Money m_defaultStartingCash;				///< The amount of cash a player starts with by default.
	
	Bool m_debugShowGraphicalFramerate;		///< Whether or not to show the graphical framerate bar.

	Int m_powerBarBase;										///< Logrithmic base for the power bar scale
	Real m_powerBarIntervals;							///< how many logrithmic intervals the width will be divided into
	Int m_powerBarYellowRange;						///< Red if consumption exceeds production, yellow if consumption this close but under, green if further under
	Real m_displayGamma;									///<display gamma that's adjusted with "brightness" control on options screen.

	UnsignedInt m_unlookPersistDuration;	///< How long after unlook until the sighting info executes the undo

	Bool m_shouldUpdateTGAToDDS;					///< Should we attempt to update old TGAs to DDS stuff on loadup?
  
	UnsignedInt m_doubleClickTimeMS;	///< What is the maximum amount of time that can seperate two clicks in order
																		///< for us to generate a double click message?

	RGBColor m_shroudColor;						///< What color should the shroud be?  Remember, this is a lighting multiply, not an add
	UnsignedByte m_clearAlpha;				///< 255 means perfect visibility
	UnsignedByte m_fogAlpha;					///< 127 means fog is half as obscuring as shroud
	UnsignedByte m_shroudAlpha;				///< 0 makes this opaque, but they may get fancy

	// network timing values.
	UnsignedInt m_networkFPSHistoryLength;		      	///< The number of fps history entries
	UnsignedInt m_networkLatencyHistoryLength;      	///< The number of ping history entries.
	UnsignedInt m_networkCushionHistoryLength;      	///< The number of cushion values to keep.
	UnsignedInt m_networkRunAheadMetricsTime;	      	///< The number of miliseconds between run ahead metrics things
	UnsignedInt m_networkKeepAliveDelay;			      	///< The number of seconds between when the connections to each player send a keep-alive packet.
	UnsignedInt m_networkRunAheadSlack;				      	///< The amount of slack in the run ahead value. This is the percentage of the calculated run ahead that is added.
	UnsignedInt m_networkDisconnectTime;			      	///< The number of milliseconds between when the game gets stuck on a frame for a network stall and when the disconnect dialog comes up.
	UnsignedInt m_networkPlayerTimeoutTime;		      	///< The number of milliseconds between when a player's last keep alive command was recieved and when they are considered disconnected from the game.
	UnsignedInt	m_networkDisconnectScreenNotifyTime;  ///< The number of milliseconds between when the disconnect screen comes up and when the other players are notified that we are on the disconnect screen.
	
	Real				m_keyboardCameraRotateSpeed;    ///< How fast the camera rotates when rotated via keyboard controls.
  Int					m_playStats;									///< Int whether we want to log play stats or not, if <= 0 then we don't log

#if defined(_DEBUG)
	Bool m_specialPowerUsesDelay ;
#endif
  Bool m_TiVOFastMode;            ///< When true, the client speeds up the framerate... set by HOTKEY!
  


#if defined(_DEBUG)
	Bool m_wireframe;
	Bool m_stateMachineDebug;
	Bool m_useCameraConstraints;
	Bool m_shroudOn;
	Bool m_fogOfWarOn;
	Bool m_jabberOn;
	Bool m_munkeeOn;
	Bool m_allowUnselectableSelection;			///< Are we allowed to select things that are unselectable?
	Bool m_disableCameraFade;								///< if true, script commands affecting camera are disabled
	Bool m_disableScriptedInputDisabling;		///< if true, script commands can't disable input
	Bool m_disableMilitaryCaption;					///< if true, military briefings go fast
	Int m_benchmarkTimer;										///< how long to play the game in benchmark mode?
  Bool m_checkForLeaks;
	Bool m_vTune;							///< Read only: GameDataDebug.ini sets "VTune = No"
	Bool m_debugCamera;						///< Used to display Camera debug information
	Bool m_debugVisibility;						///< Should we actively debug the visibility
	Int m_debugVisibilityTileCount;		///< How many tiles we should show when debugging visibility
	Real m_debugVisibilityTileWidth;	///< How wide should these tiles be?
	Int m_debugVisibilityTileDuration;	///< How long should these tiles stay around, in frames?
	Bool m_debugThreatMap;						///< Should we actively debug the threat map
	UnsignedInt m_maxDebugThreat;			///< This value (and any values greater) will appear full RED.
	Int m_debugThreatMapTileDuration;	///< How long should these tiles stay around, in frames?
	Bool m_debugCashValueMap;					///< Should we actively debug the threat map
	UnsignedInt m_maxDebugValue;			///< This value (and any values greater) will appear full GREEN.
	Int m_debugCashValueMapTileDuration;	///< How long should these tiles stay around, in frames?
	RGBColor m_debugVisibilityTargettableColor;	///< What color should the targettable cells be?
	RGBColor m_debugVisibilityDeshroudColor;			///< What color should the deshrouding cells be?
	RGBColor m_debugVisibilityGapColor;					///< What color should the gap generator cells be?
	Bool m_debugProjectilePath;						///< Should we actively debug the bezier paths on projectiles
	Real m_debugProjectileTileWidth;			///< How wide should these tiles be?
	Int m_debugProjectileTileDuration;		///< How long should these tiles stay around, in frames?
	RGBColor m_debugProjectileTileColor;	///< What color should these tiles be?
	Bool m_debugIgnoreAsserts;						///< Ignore all asserts.
	Bool m_debugIgnoreStackTrace;					///< No stacktraces for asserts.
	Bool m_showCollisionExtents;	///< Used to display collision extents
  Bool m_showAudioLocations;    ///< Used to display audio markers and ambient sound radii
	Bool m_saveStats;
	Bool m_saveAllStats;
	Bool m_useLocalMOTD;
	AsciiString m_baseStatsDir;
	AsciiString m_MOTDPath;
	Bool m_extraLogging;					///< More expensive debug logging to catch crashes.
#endif

	Bool				m_isBreakableMovie;							///< if we enter a breakable movie, set this flag
	Bool				m_breakTheMovie;								///< The user has hit escape!
	/// -mod in command line order: a folder ends with a separator, an archive (.big, .zip) does not.
	std::vector<AsciiString> m_mods;

	//-allAdvice feature
	//Bool m_allAdvice;


	// the trailing '\' is included!
  const AsciiString &getPath_UserData() const { return m_userDataDir; }
  /// EA's user data folder of the original game (read only), empty if absent or with -benutzerdaten.
  const AsciiString &getPath_EAUserData() const { return m_eaUserDataDir; }

private:

	static const FieldParse s_GlobalDataFieldParseTable[];

	// this is private, since we read the info from Windows and cache it for
	// future use. No one is allowed to change it, ever. (srj)
	AsciiString m_userDataDir;
	AsciiString m_eaUserDataDir;

	// "UserDataLeafName" from the Generals GameData.ini; parsed so the data loads, not used.
	AsciiString m_userDataLeafName;
	
	static GlobalData *m_theOriginal;		///< the original global data instance (no overrides)
	GlobalData *m_next;									///< next instance (for overrides)
	GlobalData *newOverride( void );		/** create a new override, copy data from previous
																			override, and return it */


	GlobalData(const GlobalData& that) { DEBUG_CRASH(("unimplemented")); }
	GlobalData& operator=(const GlobalData& that) { DEBUG_CRASH(("unimplemented")); return *this; }

};

// singleton
#include "Engine/EngineContext.h"

#define TheGlobalData ((const GlobalData*)TheWritableGlobalData)

#endif
