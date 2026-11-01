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

// GameEngine.cpp /////////////////////////////////////////////////////////////////////////////////
// Implementation of the Game Engine singleton
// Author: Michael S. Booth, April 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#include "formatinto.h"

#include <algorithm>	// std::sort for the tick statistics
#include <vector>		// list of tick times

#include "Common/ActionManager.h"
#include "Common/KindOf.h"
#include "Common/ThingTemplate.h"
#include "Common/AudioAffect.h"
#include "Common/BuildAssistant.h"
#include "Common/CRCDebug.h"
#include "Common/Radar.h"
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"
#include "Common/PlayerList.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/OwnedSubsystem.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/MessageStream.h"
#include "Common/ThingFactory.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/LocalFileSystem.h"
#include "Common/GlobalData.h"
#include "Common/TestAids.h"
#include "Common/PerfTimer.h"
#include "Common/RandomValue.h"
#include "Common/NameKeyGenerator.h"
#include "Common/ModuleFactory.h"
#include "Common/Debug.h"
#include "Common/GameState.h"
#include "Common/GameStateMap.h"
#include "Common/Science.h"
#include "Common/FunctionLexicon.h"
#include "Common/CommandLine.h"
#include "Common/DamageFX.h"
#include "Common/MultiplayerSettings.h"
#include "Common/Recorder.h"
#include "Common/SpecialPower.h"
#include "Common/TerrainTypes.h"
#include "Common/Upgrade.h"
#include "Common/UserSettings.h"
#include "Common/ArchivePrefetch.h"
#include "GameClient/MapUtil.h"			// auto skirmish
#include "GameNetwork/GameInfo.h"		// auto skirmish
#include "GameNetwork/GameLobby.h"		// spectators
#include "Engine/CpuTopology.h"
#include "Engine/UserDataFiles.h"
#include "Engine/JobSystem.h"
#include "Engine/SimThread.h"
#include "Engine/ThreadRole.h"
#include "Engine/TextureTier.h"		// video memory
#include "Engine/LogicAccessGuard.h"
#include "Common/CriticalSection.h"
#include "Common/LogicAudio.h"
#include "Common/LogicToClient.h"
#include "Engine/Watchdog.h"
#include "Common/CrashReport.h"
#include "Engine/EngineLog.h"

static void startAutoSkirmish( void );
static Bool s_autoSkirmishPending = FALSE;	// set in init(), started in the first update()
static Bool s_replayPending = FALSE;				// same for -replay/-file with .rep

// -zuschauerTest N: the replay plays as if it arrived from a host (RecorderClass::playbackLive). At first N
// frames are there (the spectator catches up), then one more arrives in each of 40 passes out of 100 (the
// spectator, one tick per pass in the runner, waits for the rest). Replays checksum alike either way.
static UnsignedInt s_watchTestEnd = 0;	// frames of the replay; 0: not running
static void feedWatchTest( void )
{
	if (s_watchTestEnd == 0 || !TheRecorder->isLive())
		return;
	static UnsignedInt s_pass = 0;
	static UnsignedInt s_limit = 0;
	static UnsignedInt s_waits = 0;
	if (s_limit == 0)
		s_limit = static_cast<UnsignedInt>(TheGlobalData->m_watchTestStart);
	else if ((s_pass % 100) < 40)
		s_limit += 1;
	++s_pass;
	if (TheRecorder->isLiveWaiting())
		++s_waits;
	const Bool ended = s_limit >= s_watchTestEnd;
	TheRecorder->setLiveLimit(s_limit, ended);
	if (ended)
	{
		char line[96];
		WWFormat::Into(line, "LIVE complete at pass {:d}, waited {:d} passes", s_pass, s_waits);
		GameEngine_RunnerLog(line);
		s_watchTestEnd = 0;
	}
}

// The hardware hint the main menu still has to show (see GameEngine.h).
static Bool s_hardwareHintPending = FALSE;
static Int s_hardwareHintCores = 0, s_hardwareHintThreads = 0;

Bool GameEngine_PendingHardwareHint( Int *cores, Int *threads )
{
	*cores = s_hardwareHintCores;
	*threads = s_hardwareHintThreads;
	return s_hardwareHintPending;
}

void GameEngine_ClearHardwareHint( void )
{
	s_hardwareHintPending = FALSE;
}

static Bool s_restartRequested = FALSE;

void GameEngine_RequestRestart( void )
{
	s_restartRequested = TRUE;
	if( TheGameEngine )
		TheGameEngine->setQuitting( TRUE );
}

Bool GameEngine_RestartRequested( void )
{
	return s_restartRequested;
}

// Process exit code and runner log, see GameEngine.h.
Int TheGameExitCode = GAME_EXIT_OK;
static FILE *s_runnerLog = nullptr;
void GameEngine_RunnerLog( const char *line )
{
	if (TheGlobalData == nullptr || TheGlobalData->m_crcLogFile.isEmpty())
		return;
	if (s_runnerLog == nullptr)
		s_runnerLog = fopen(TheGlobalData->m_crcLogFile.str(), "w");
	if (s_runnerLog)
	{
		fputs(line, s_runnerLog);
		fputc('\n', s_runnerLog);
		fflush(s_runnerLog);
	}
}

void GameEngine_LogAction( UnsignedInt frame, const char *kind, const char *name, Real x, Real y, Real z )
{
	if (TheGlobalData == nullptr || !TheGlobalData->m_logActions)
		return;
	char line[256];
	snprintf(line, sizeof(line), "ACTION %u %s %s %.1f %.1f %.1f", frame, kind, name, x, y, z);
	GameEngine_RunnerLog(line);
}

// Render clock, see GameEngine.h.
static UnsignedInt s_fixedRenderClockMs = 0;
UnsignedInt GameEngine_RenderTimeMs( void )
{
	if (TheGlobalData != nullptr && TheGlobalData->m_fixedTimeStep)
		return s_fixedRenderClockMs;
	return timeGetTime();
}

// Steps of the drawn frame, see GameEngine.h. The first frame and frames after a long stall (loading, pause in the
// debugger) count as one step resp. at most 250 ms.
static Real s_renderFrameSteps = 1.0f;
static UnsignedInt s_lastRenderFrameMs = 0;
static Bool s_renderFrameTimeValid = FALSE;
void GameEngine_BeginRenderFrameSteps( void )
{
	const UnsignedInt nowMs = GameEngine_RenderTimeMs();
	if (s_renderFrameTimeValid)
	{
		UnsignedInt dt = nowMs - s_lastRenderFrameMs;
		if (dt > 250)
			dt = 250;
		s_renderFrameSteps = static_cast<Real>(dt) / 33.0f;
	}
	else
		s_renderFrameSteps = 1.0f;
	s_lastRenderFrameMs = nowMs;
	s_renderFrameTimeValid = TRUE;
}
Real GameEngine_RenderFrameSteps( void )
{
	return s_renderFrameSteps;
}
// -logRenderTime, defined before GameEngine::execute()
static void logRenderTime( char kind, Real alpha, Real tickLenMs );
#include "Common/Xfer.h"
#include "Common/XferCRC.h"
#include "Common/GameLOD.h"
#include "Common/InstallInfo.h"
#include "Common/GameCommon.h"	// FOR THE ALLOW_DEBUG_CHEATS_IN_RELEASE #define
#include "Common/GameEdition.h"

#include "GameLogic/Armor.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/CaveSystem.h"
#include "GameLogic/CrateSystem.h"
#include "GameLogic/Damage.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/SidesList.h"

#include "GameClient/Display.h"
#include "GameClient/FXList.h"
#include "GameClient/GameClient.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Shell.h"
#include "GameClient/GameText.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/Water.h"
#include "GameClient/TerrainRoads.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/MapUtil.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Drawable.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/TestScript.h"

#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/GameLobby.h"
#include "GameNetwork/networkutil.h"

#include "Common/version.h"
#include "GameLogic/AIStats.h"

//-------------------------------------------------------------------------------------------------

#ifdef DEBUG_CRC
class DeepCRCSanityCheck : public SubsystemInterface
{
public:
	DeepCRCSanityCheck() {}
	virtual ~DeepCRCSanityCheck() override {}

	virtual void init(void) override {}
	virtual void reset(void) override;
	virtual void update(void) override {}

protected:
};

DeepCRCSanityCheck *TheDeepCRCSanityCheck = nullptr;

void DeepCRCSanityCheck::reset(void)
{
	static Int timesThrough = 0;
	static UnsignedInt lastCRC = 0;

	AsciiString fname;
	fname.format("%sCRCAfter%dMaps.dat", TheGlobalData->getPath_UserData().str(), timesThrough);
	UnsignedInt thisCRC = TheGameLogic->getCRC( CRC_RECALC, fname );

	DEBUG_LOG(("DeepCRCSanityCheck: CRC is %X\n", thisCRC));
	DEBUG_ASSERTCRASH(timesThrough == 0 || thisCRC == lastCRC,
		("CRC after reset did not match beginning CRC!\nNetwork games won't work after this.\nOld: 0x%8.8X, New: 0x%8.8X",
		lastCRC, thisCRC));
	lastCRC = thisCRC;

	timesThrough++;
}
#endif // DEBUG_CRC

//-------------------------------------------------------------------------------------------------
/// The GameEngine singleton instance

//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
template<class SUBSYSTEM>
void initSubsystem(SUBSYSTEM*& sysref, AsciiString name, SUBSYSTEM* sys, Xfer *pXfer,  const char* path1 = nullptr, 
									 const char* path2 = nullptr, const char* dirpath = nullptr)
{
	sysref = sys;
	TheSubsystemList->initSubsystem(sys, path1, path2, dirpath, pXfer, name, [&sysref, sys] { if (sysref == sys) sysref = nullptr; });
}

//-------------------------------------------------------------------------------------------------
extern HINSTANCE ApplicationHInstance;  ///< our application instance

//-------------------------------------------------------------------------------------------------
static void updateTGAtoDDS();

Int GameEngine::getFramesPerSecondLimit( void )
{
	return m_maxFPS;
}

//-------------------------------------------------------------------------------------------------
GameEngine::GameEngine( void )
{
	// Set the time slice size to 1 ms.
	timeBeginPeriod(1);

	// initialize to non garbage values
	m_maxFPS = 0;
	m_renderFpsLimit = 0;
	m_quitting = FALSE;
	m_isActive = FALSE;

}

//-------------------------------------------------------------------------------------------------
GameEngine::~GameEngine()
{
	TestScript_Release();	// free the command list before the leak report
#if defined(_DEBUG)
	DebugLatchIgnoreAsserts();	// -ignoreAsserts also applies to the leak report after WinMain
#endif
	// If a match is still running on shutdown, clean it up before the subsystems are torn down, like
	// -quitAfter, -maxFrames and Alt+F4. The main menu's quit button only appends MSG_CLEAR_GAME_DATA,
	// which is never processed after setQuitting; with a running shell map shutdownAll() would then tear
	// down the radar before the PlayerList, and ~PlayerList -> init -> becomingLocalPlayer would access
	// the freed radar.
	if (TheGameLogic && TheGameLogic->isInGame())
	{
		if (TheRecorder && TheRecorder->getMode() == RecorderModeType::RECORDERMODETYPE_RECORD)
			TheRecorder->stopRecording();
		TheGameLogic->clearGameData( FALSE );
	}
	ArchivePrefetch_Stop();
	::Engine::Watchdog::get().stop();
	::Engine::simThread().stop();
	::Engine::JobSystem::get().shutdown();
	Engine_LogCriticalSectionStats();	// lock access/wait statistics (only active with ENGINE_LOCK_STATS)
#if defined(_DEBUG)
	// Write the list of logic accesses outside the tick next to the exe.
	if (::Engine::logicAccessFindingCount() > 0)
	{
		::Engine::writeLogicAccessFindingsTo("LogicAccessFindings.txt");
		DEBUG_LOG(("%u sites with logic access outside the tick, %llu accesses -- see LogicAccessFindings.txt\n",
			(unsigned)::Engine::logicAccessFindingCount(), ::Engine::logicAccessTotalCount()));
	}
#endif
	::Engine::clearLogicAccessFindings();	// otherwise static state shows up as leaks in the memory report
#if defined(_DEBUG)
	// Client functions called from the simulation thread (-simThread): the work list for overlapping.
	if (::Engine::threadRoleViolationSiteCount() > 0)
		::Engine::writeThreadRoleViolationsTo("ThreadRoleFindings.txt");
#endif
	::Engine::clearThreadRoleViolations();
	::Engine::UserDataFiles::clearRoot();	// static path, would show up as a leak like the lists above
	//extern std::vector<std::string>	preloadTextureNamesGlobalHack;
	//preloadTextureNamesGlobalHack.clear();

	// When started directly with -skirmish, startAutoSkirmish creates TheSkirmishGameInfo; otherwise only
	// the skirmish menu or GameLogic (single player) frees it. The global marker lists of the map preview
	// (TheSupplyAndTechImageLocations) keep their pool blocks until after the leak report, so clean up
	// both here (leak report when quitting in the middle of a match).
	delete TheSkirmishGameInfo;
	::Engine::context().skirmishGameInfo = nullptr;
	TheSupplyAndTechImageLocations.m_supplyPosList.clear();
	TheSupplyAndTechImageLocations.m_techPosList.clear();
	destroySubsystem(m_own.mapCache, ::Engine::context().mapCache);

//	delete TheShell;
//	TheShell = NULL;

	if (TheLogicAudio)
		ENGINE_LOG_INFO(::Engine::LOG_CAT_AUDIO, "Logic audio: %u events handed over in batches, %u of them removed within the same tick (not handed over)",
			TheLogicAudio->getAddedTotal(), TheLogicAudio->getCancelledTotal());
	if (TheLogicToClient)
		ENGINE_LOG_INFO(::Engine::LOG_CAT_ENGINE, "Logic to client: %u calls handed over after the logic tick", TheLogicToClient->getPostedTotal());
	UserSettings::save();		// while GlobalData still says whether this run may write
	destroySubsystem(m_own.logicAudio, ::Engine::context().logicAudio);	// before the audio system (subsystem list); frees events that were not handed over
	destroySubsystem(m_own.logicToClient, ::Engine::context().logicToClient);
	TheSubsystemList->shutdownAll();
	destroySubsystem(m_own.subsystemList, ::Engine::context().subsystemList);

	delete TheNetwork;
	::Engine::context().network = nullptr;
	ReleaseNetworkStatics();
	UserSettings::release();

	destroySubsystem(m_own.commandList, ::Engine::context().commandList);

	destroySubsystem(m_own.nameKeyGenerator, ::Engine::context().nameKeyGenerator);

	destroySubsystem(m_own.fileSystem, ::Engine::context().fileSystem);

	destroySubsystem(m_own.gameLODManager, ::Engine::context().gameLODManager);

	Drawable::killStaticImages();



	// Restore the previous time slice for Windows.
	timeEndPeriod(1);
}

void GameEngine::setFramesPerSecondLimit( Int fps )
{
	DEBUG_LOG(("GameEngine::setFramesPerSecondLimit() - setting max fps to %d (TheGlobalData->m_useFpsLimit == %d)\n", fps, TheGlobalData->m_useFpsLimit));
	m_maxFPS = fps;
}

/** -----------------------------------------------------------------------------------------------
 * Initialize the game engine by initializing the GameLogic and GameClient.
 */
// Writes the numbers that replay and network commands carry, with their names: thing templates (production
// and construction commands), upgrades (by name key), sciences (by index) and special powers (by id).
static void writeGameDataDump(const char *path)
{
	std::FILE *f = std::fopen(path, "wb");
	if (!f)
		return;
	std::fprintf(f, "# type id name side cost kinds\n");
	static const struct { KindOfType kind; const char *name; } kinds[] = {
		{ KINDOF_STRUCTURE, "STRUCTURE" }, { KINDOF_INFANTRY, "INFANTRY" }, { KINDOF_VEHICLE, "VEHICLE" },
		{ KINDOF_AIRCRAFT, "AIRCRAFT" }, { KINDOF_DOZER, "DOZER" }, { KINDOF_HARVESTER, "HARVESTER" },
		{ KINDOF_COMMANDCENTER, "COMMANDCENTER" }, { KINDOF_TRANSPORT, "TRANSPORT" }, { KINDOF_HERO, "HERO" },
		{ KINDOF_CASH_GENERATOR, "CASH_GENERATOR" }, { KINDOF_SUPPLY_SOURCE, "SUPPLY_SOURCE" },
		{ KINDOF_FS_POWER, "FS_POWER" }, { KINDOF_FS_FACTORY, "FS_FACTORY" }, { KINDOF_FS_BARRACKS, "FS_BARRACKS" },
		{ KINDOF_FS_WARFACTORY, "FS_WARFACTORY" }, { KINDOF_FS_AIRFIELD, "FS_AIRFIELD" },
		{ KINDOF_FS_SUPPLY_CENTER, "FS_SUPPLY_CENTER" }, { KINDOF_FS_SUPPLY_DROPZONE, "FS_SUPPLY_DROPZONE" },
		{ KINDOF_FS_SUPERWEAPON, "FS_SUPERWEAPON" }, { KINDOF_FS_BLACK_MARKET, "FS_BLACK_MARKET" },
		{ KINDOF_FS_STRATEGY_CENTER, "FS_STRATEGY_CENTER" }, { KINDOF_FS_INTERNET_CENTER, "FS_INTERNET_CENTER" },
		{ KINDOF_MP_COUNT_FOR_VICTORY, "MP_COUNT_FOR_VICTORY" }, { KINDOF_SCORE, "SCORE" }
	};
	if (TheThingFactory)
	{
		for (const ThingTemplate *t = TheThingFactory->firstTemplate(); t; t = t->friend_getNextTemplate())
		{
			std::fprintf(f, "THING %u %s %s %d", static_cast<unsigned>(t->getTemplateID()), t->getName().str(),
				t->getDefaultOwningSide().isEmpty() ? "-" : t->getDefaultOwningSide().str(), t->friend_getBuildCost());
			const char *sep = " ";
			for (const auto &k : kinds)
				if (t->isKindOf(k.kind))
				{
					std::fprintf(f, "%s%s", sep, k.name);
					sep = ",";
				}
			std::fprintf(f, "\n");
		}
	}
	if (TheUpgradeCenter)
	{
		for (const UpgradeTemplate *u = TheUpgradeCenter->firstUpgradeTemplate(); u; u = u->friend_getNext())
			std::fprintf(f, "UPGRADE %d %s\n", static_cast<int>(u->getUpgradeNameKey()), u->getUpgradeName().str());
	}
	if (TheScienceStore)
	{
		for (const AsciiString &name : TheScienceStore->friend_getScienceNames())
			std::fprintf(f, "SCIENCE %d %s\n", static_cast<int>(TheScienceStore->getScienceFromInternalName(name)), name.str());
	}
	if (TheSpecialPowerStore)
	{
		for (UnsignedInt i = 0; i < static_cast<UnsignedInt>(TheSpecialPowerStore->getNumSpecialPowers()); ++i)
			if (const SpecialPowerTemplate *sp = TheSpecialPowerStore->getSpecialPowerTemplateByIndex(i))
				std::fprintf(f, "SPECIAL %u %s\n", sp->getID(), sp->getName().str());
	}
	if (ThePlayerTemplateStore)
	{
		for (Int i = 0; i < ThePlayerTemplateStore->getPlayerTemplateCount(); ++i)
			if (const PlayerTemplate *pt = ThePlayerTemplateStore->getNthPlayerTemplate(i))
				std::fprintf(f, "PLAYERTEMPLATE %d %s %s\n", i, pt->getName().str(), pt->getSide().str());
	}
	std::fclose(f);
}

void GameEngine::init( void ) {} /// @todo: I changed this to take argc & argv so we can parse those after the GDF is loaded.  We need to rethink this immediately as it is a nasty hack
void GameEngine::init( int argc, char *argv[] )
{
	try {
		//create an INI object to use for loading stuff
		INI ini;

#ifdef DEBUG_LOGGING
		if (TheVersion)
		{
			DEBUG_LOG(("================================================================================\n"));
	#if defined _DEBUG
			const char *buildType = "Debug";
	#else
			const char *buildType = "Release";
	#endif
			DEBUG_LOG(("Generals version %s (%s)\n", TheVersion->getAsciiVersion().str(), buildType));
			DEBUG_LOG(("Build date: %s\n", TheVersion->getAsciiBuildTime().str()));
			DEBUG_LOG(("Build location: %s\n", TheVersion->getAsciiBuildLocation().str()));
			DEBUG_LOG(("Built by: %s\n", TheVersion->getAsciiBuildUser().str()));
			DEBUG_LOG(("================================================================================\n"));
		}
#endif

	#ifdef DUMP_PERF_STATS
		DEBUG_LOG(("Calculating CPU frequency for performance timers.\n"));
		InitPrecisionTimer();
	#endif




	#ifdef DUMP_PERF_STATS////////////////////////////////////////////////////////////
	__int64 startTime64;//////////////////////////////////////////////////////////////
	__int64 endTime64,freq64;///////////////////////////////////////////////////////////
	GetPrecisionTimerTicksPerSec(&freq64);///////////////////////////////////////////////
	GetPrecisionTimer(&startTime64);////////////////////////////////////////////////////
  char Buf[256];//////////////////////////////////////////////////////////////////////
	#endif//////////////////////////////////////////////////////////////////////////////
		
		m_maxFPS = DEFAULT_MAX_FPS;

		::Engine::context().subsystemList = adoptSubsystem(m_own.subsystemList, MSGNEW("GameEngineSubsystem") SubsystemInterfaceList);
		
		TheSubsystemList->addSubsystem(this);

		// initialize the random number system
		InitRandom();

		// Create the low-level file system interface
		::Engine::context().fileSystem = adoptSubsystem(m_own.fileSystem, createFileSystem());

		//Kris: Patch 1.01 - November 17, 2003
		//I was unable to resolve the RTPatch method of deleting a shipped file. English, Chinese, and Korean
		//SKU's shipped with two INIZH.big files. One properly in the Run directory and the other in Run\INI\Data.
		//We need to toast the latter in order for the game to patch properly.
		// Zero Hour only (Generals never shipped that archive).
		if (isGameEditionZeroHour())
			DeleteFile( "Data\\INI\\INIZH.big" );

		// not part of the subsystem list, because it should normally never be reset!
		::Engine::context().nameKeyGenerator = adoptSubsystem(m_own.nameKeyGenerator, MSGNEW("GameEngineSubsystem") NameKeyGenerator);
		TheNameKeyGenerator->init();


    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	WWFormat::Into(Buf, "----------------------------------------------------------------------------After TheNameKeyGenerator  = {:f} seconds \n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		// not part of the subsystem list, because it should normally never be reset!
		::Engine::context().commandList = adoptSubsystem(m_own.commandList, MSGNEW("GameEngineSubsystem") CommandList);
		TheCommandList->init();

    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	WWFormat::Into(Buf, "----------------------------------------------------------------------------After TheCommandList  = {:f} seconds \n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		XferCRC xferCRC;
		xferCRC.open("lightCRC");


		initSubsystem(::Engine::context().localFileSystem, "TheLocalFileSystem", createLocalFileSystem(), nullptr);


    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	WWFormat::Into(Buf, "----------------------------------------------------------------------------After TheLocalFileSystem  = {:f} seconds \n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(::Engine::context().archiveFileSystem, "TheArchiveFileSystem", createArchiveFileSystem(), nullptr); // this MUST come after TheLocalFileSystem creation

    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	WWFormat::Into(Buf, "----------------------------------------------------------------------------After TheArchiveFileSystem  = {:f} seconds \n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(::Engine::context().writableGlobalData, "TheWritableGlobalData", MSGNEW("GameEngineSubsystem") GlobalData(), &xferCRC, "Data\\INI\\Default\\GameData.ini", "Data\\INI\\GameData.ini");


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	WWFormat::Into(Buf, "----------------------------------------------------------------------------After  TheWritableGlobalData = {:f} seconds \n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////



	#if defined(_DEBUG)
		// If we're in Debug or Internal, load the Debug info as well.
		ini.load( AsciiString( "Data\\INI\\GameDataDebug.ini" ), INILoadType::INI_LOAD_OVERWRITE, nullptr );
	#endif
		
		// special-case: parse command-line parameters after loading global data
		parseCommandLine(argc, argv);

		// -fixedTimeStep also seeds client and audio randomness with a fixed value (InitRandom() above uses
		// the time), otherwise particles differ between two runs of the same scene.
		if (TheGlobalData->m_fixedTimeStep)
			InitRandom(0);

		// Log cores and logical processors; below 4 cores/8 threads the main menu shows a one-time hint, no abort.
		// Settings.ini "HardwareHint = no" disables it; after the first time HardwareHintShown is stored there.
		// A message box of the game, not of Windows: one without owner can end up behind the game window.
		{
			const Bool unattended = TheGlobalData->m_runnerMode || TheGlobalData->m_quitAfterFrames != 0;
			const Bool wantHint = UserSettings::getHardwareHint() && !UserSettings::getHardwareHintShown() && !unattended;
			if (::Engine::logCpuTopologyAtStartup(wantHint == TRUE))
			{
				s_hardwareHintCores = ::Engine::CpuTopology::get().physicalCores;
				s_hardwareHintThreads = ::Engine::CpuTopology::get().logicalProcessors;
				s_hardwareHintPending = TRUE;
			}
		}

		// Video memory without a device via DXGI (budget, UMA), basis for the automatic texture tier.
		// One line in the log and in the runner log.
		{
			char text[320];
			::Engine::VideoMemoryInfo::get().describe(text, sizeof(text));
			char line[360];
			WWFormat::Into(line, "VIDEOMEM {}", text);
			line[sizeof(line) - 1] = 0;
			GameEngine_RunnerLog(line);
			DEBUG_LOG(("HD_ASSETS: Grafikspeicher %s\n", text));
		}

		// Start the job system. The worker count comes from -workers, else from Options.ini "WorkerThreads",
		// else from the CPU topology (logical processors - 2, i.e. 6 on the 4C/8T target hardware). 0 is the
		// serial reference mode, in which ParallelFor computes everything in the caller.
		{
			Int workers = TheGlobalData->m_workerThreads;	// -workers takes precedence
			if (workers < 0)
			{
				workers = UserSettings::getWorkerThreads();
			}
			::Engine::JobSystem::get().start(workers);
			char line[64];
			WWFormat::Into(line, "WORKERS {:d}", ::Engine::JobSystem::get().workerCount());
			GameEngine_RunnerLog(line);	// also in the runner log (determinism tests)
			DEBUG_LOG(("Job system with %d workers\n", ::Engine::JobSystem::get().workerCount()));
		}

		// Simulation thread (-simThread): the tick of a running game is handed to it.
		if (TheGlobalData->m_simThread)
		{
			::Engine::simThread().start();
			GameEngine_RunnerLog("SIMTHREAD on");
		}

		// Watchdog. Main and sim thread report on every pass; if a report is missing for more than 10 s, a
		// full crash report with the stacks of all threads is written, so a deadlock can be analyzed instead
		// of just "the game hangs".
		{
			::Engine::Watchdog &watchdog = ::Engine::Watchdog::get();
			watchdog.setCallback( []( ::Engine::WatchdogSubject subject, unsigned stalledMs ) {
				// Stopped at a breakpoint in the debugger everything stands still; that is not a hang.
				if( ::IsDebuggerPresent() )
					return;
				char reason[128];
				WWFormat::Into(reason, "Watchdog: {} hung for {:d} ms", ::Engine::Watchdog::subjectName( subject ), stalledMs);
				WriteCrashReport( reason, nullptr );
			} );
			watchdog.start();
		}

		// Read archives ahead in the background so the file cache is warm when the first map loads.
		// Settings.ini "PrefetchArchives = no" disables it; all archives are mounted by now.
		{
			if (UserSettings::getPrefetchArchives())
				ArchivePrefetch_Start();
		}

		// doesn't require resets so just create a single instance here.
		::Engine::context().gameLODManager = adoptSubsystem(m_own.gameLODManager, MSGNEW("GameEngineSubsystem") GameLODManager);
		TheGameLODManager->init();
		
		// after parsing the command line, we may want to perform dds stuff. Do that here.
		if (TheGlobalData->m_shouldUpdateTGAToDDS) {
			// update any out of date targas here.
			updateTGAtoDDS();
		}

		// read the water settings from INI (must do prior to initing GameClient, apparently)
		ini.load( AsciiString( "Data\\INI\\Default\\Water.ini" ), INILoadType::INI_LOAD_OVERWRITE, &xferCRC );
		ini.load( AsciiString( "Data\\INI\\Water.ini" ), INILoadType::INI_LOAD_OVERWRITE, &xferCRC );
		// Generals ships no Weather.ini, and INI::load would throw INI_CANT_OPEN_FILE. Do not add one for
		// Generals: the file would go into m_iniCRC (replay header).
		if( TheFileSystem->doesFileExist( "Data\\INI\\Default\\Weather.ini" ) )
			ini.load( AsciiString( "Data\\INI\\Default\\Weather.ini" ), INILoadType::INI_LOAD_OVERWRITE, &xferCRC );
		if( TheFileSystem->doesFileExist( "Data\\INI\\Weather.ini" ) )
			ini.load( AsciiString( "Data\\INI\\Weather.ini" ), INILoadType::INI_LOAD_OVERWRITE, &xferCRC );



	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	WWFormat::Into(Buf, "----------------------------------------------------------------------------After water INI's = {:f} seconds \n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


#ifdef DEBUG_CRC
		initSubsystem(TheDeepCRCSanityCheck, "TheDeepCRCSanityCheck", MSGNEW("GameEngineSubystem") DeepCRCSanityCheck, nullptr, nullptr, nullptr, nullptr);
#endif // DEBUG_CRC
		initSubsystem(::Engine::context().gameText, "TheGameText", CreateGameTextInterface(), nullptr);

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	WWFormat::Into(Buf, "----------------------------------------------------------------------------After TheGameText = {:f} seconds \n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(::Engine::context().scienceStore,"TheScienceStore", MSGNEW("GameEngineSubsystem") ScienceStore(), &xferCRC, "Data\\INI\\Default\\Science.ini", "Data\\INI\\Science.ini");
		initSubsystem(::Engine::context().multiplayerSettings,"TheMultiplayerSettings", MSGNEW("GameEngineSubsystem") MultiplayerSettings(), &xferCRC, "Data\\INI\\Default\\Multiplayer.ini", "Data\\INI\\Multiplayer.ini");
		initSubsystem(::Engine::context().terrainTypes,"TheTerrainTypes", MSGNEW("GameEngineSubsystem") TerrainTypeCollection(), &xferCRC, "Data\\INI\\Default\\Terrain.ini", "Data\\INI\\Terrain.ini");
		initSubsystem(::Engine::context().terrainRoads,"TheTerrainRoads", MSGNEW("GameEngineSubsystem") TerrainRoadCollection(), &xferCRC, "Data\\INI\\Default\\Roads.ini", "Data\\INI\\Roads.ini");
		initSubsystem(::Engine::context().globalLanguageData,"TheGlobalLanguageData",MSGNEW("GameEngineSubsystem") GlobalLanguage, nullptr); // must be before the game text
		initSubsystem(::Engine::context().audio,"TheAudio", createAudioManager(), nullptr);
		::Engine::context().logicAudio = adoptSubsystem(m_own.logicAudio, MSGNEW("GameEngineSubsystem") LogicAudio());	// logic audio facade, flush() in update()
		::Engine::context().logicToClient = adoptSubsystem(m_own.logicToClient, MSGNEW("GameEngineSubsystem") LogicToClient());	// logic -> client calls, flush() in update()
		if (!TheAudio->isMusicAlreadyLoaded())
			setQuitting(TRUE);

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	WWFormat::Into(Buf, "----------------------------------------------------------------------------After TheAudio = {:f} seconds \n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(::Engine::context().functionLexicon,"TheFunctionLexicon", createFunctionLexicon(), nullptr);
		initSubsystem(::Engine::context().moduleFactory,"TheModuleFactory", createModuleFactory(), nullptr);
		initSubsystem(::Engine::context().messageStream,"TheMessageStream", createMessageStream(), nullptr);
		initSubsystem(::Engine::context().sidesList,"TheSidesList", MSGNEW("GameEngineSubsystem") SidesList(), nullptr);
		initSubsystem(::Engine::context().caveSystem,"TheCaveSystem", MSGNEW("GameEngineSubsystem") CaveSystem(), nullptr);
		initSubsystem(::Engine::context().rankInfoStore,"TheRankInfoStore", MSGNEW("GameEngineSubsystem") RankInfoStore(), &xferCRC, nullptr, "Data\\INI\\Rank.ini");
		initSubsystem(::Engine::context().playerTemplateStore,"ThePlayerTemplateStore", MSGNEW("GameEngineSubsystem") PlayerTemplateStore(), &xferCRC, "Data\\INI\\Default\\PlayerTemplate.ini", "Data\\INI\\PlayerTemplate.ini");
		initSubsystem(::Engine::context().particleSystemManager,"TheParticleSystemManager", createParticleSystemManager(), nullptr);

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	WWFormat::Into(Buf, "----------------------------------------------------------------------------After TheParticleSystemManager = {:f} seconds \n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////
    
    
		initSubsystem(::Engine::context().fxListStore,"TheFXListStore", MSGNEW("GameEngineSubsystem") FXListStore(), &xferCRC, "Data\\INI\\Default\\FXList.ini", "Data\\INI\\FXList.ini");
		initSubsystem(::Engine::context().weaponStore,"TheWeaponStore", MSGNEW("GameEngineSubsystem") WeaponStore(), &xferCRC, nullptr, "Data\\INI\\Weapon.ini");
		initSubsystem(::Engine::context().objectCreationListStore,"TheObjectCreationListStore", MSGNEW("GameEngineSubsystem") ObjectCreationListStore(), &xferCRC, "Data\\INI\\Default\\ObjectCreationList.ini", "Data\\INI\\ObjectCreationList.ini");
		initSubsystem(::Engine::context().locomotorStore,"TheLocomotorStore", MSGNEW("GameEngineSubsystem") LocomotorStore(), &xferCRC, nullptr, "Data\\INI\\Locomotor.ini");
		initSubsystem(::Engine::context().specialPowerStore,"TheSpecialPowerStore", MSGNEW("GameEngineSubsystem") SpecialPowerStore(), &xferCRC, "Data\\INI\\Default\\SpecialPower.ini", "Data\\INI\\SpecialPower.ini");
		initSubsystem(::Engine::context().damageFXStore,"TheDamageFXStore", MSGNEW("GameEngineSubsystem") DamageFXStore(), &xferCRC, nullptr, "Data\\INI\\DamageFX.ini");
		initSubsystem(::Engine::context().armorStore,"TheArmorStore", MSGNEW("GameEngineSubsystem") ArmorStore(), &xferCRC, nullptr, "Data\\INI\\Armor.ini");
		initSubsystem(::Engine::context().buildAssistant,"TheBuildAssistant", MSGNEW("GameEngineSubsystem") BuildAssistant, nullptr);


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	WWFormat::Into(Buf, "----------------------------------------------------------------------------After TheBuildAssistant = {:f} seconds \n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////



		initSubsystem(::Engine::context().thingFactory,"TheThingFactory", createThingFactory(), &xferCRC, "Data\\INI\\Default\\Object.ini", nullptr, "Data\\INI\\Object");

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	WWFormat::Into(Buf, "----------------------------------------------------------------------------After TheThingFactory = {:f} seconds \n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////
    
    
		initSubsystem(::Engine::context().upgradeCenter,"TheUpgradeCenter", MSGNEW("GameEngineSubsystem") UpgradeCenter, &xferCRC, "Data\\INI\\Default\\Upgrade.ini", "Data\\INI\\Upgrade.ini");
		initSubsystem(::Engine::context().gameClient,"TheGameClient", createGameClient(), nullptr);


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	WWFormat::Into(Buf, "----------------------------------------------------------------------------After TheGameClient = {:f} seconds \n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////

	
		initSubsystem(::Engine::context().ai,"TheAI", MSGNEW("GameEngineSubsystem") AI(), &xferCRC,  "Data\\INI\\Default\\AIData.ini", "Data\\INI\\AIData.ini");
		initSubsystem(::Engine::context().gameLogic,"TheGameLogic", createGameLogic(), nullptr);
		initSubsystem(::Engine::context().teamFactory,"TheTeamFactory", MSGNEW("GameEngineSubsystem") TeamFactory(), nullptr);
		initSubsystem(::Engine::context().crateSystem,"TheCrateSystem", MSGNEW("GameEngineSubsystem") CrateSystem(), &xferCRC, "Data\\INI\\Default\\Crate.ini", "Data\\INI\\Crate.ini");
		initSubsystem(::Engine::context().playerList,"ThePlayerList", MSGNEW("GameEngineSubsystem") PlayerList(), nullptr);
		initSubsystem(::Engine::context().recorder,"TheRecorder", createRecorder(), nullptr);
		initSubsystem(::Engine::context().radar,"TheRadar", createRadar(), nullptr);
		initSubsystem(::Engine::context().victoryConditions,"TheVictoryConditions", createVictoryConditions(), nullptr);



	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	WWFormat::Into(Buf, "----------------------------------------------------------------------------After TheVictoryConditions = {:f} seconds \n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		AsciiString fname;
		fname.format("Data\\%s\\CommandMap.ini", GetGameLanguage().str());
		initSubsystem(::Engine::context().metaMap,"TheMetaMap", MSGNEW("GameEngineSubsystem") MetaMap(), nullptr, fname.str(), "Data\\INI\\CommandMap.ini");

#if defined(_DEBUG)
		ini.load("Data\\INI\\CommandMapDebug.ini", INILoadType::INI_LOAD_MULTIFILE, nullptr);
#endif

		// Test helpers, enabled with -testhilfen or Options.ini "Testhilfen = yes". The key bindings live in the
		// project data (Data/INI, copied to ProjectData\INI at build time) and are loaded last:
		// MetaEventTranslator checks the most recently added entries first, so keys shared with
		// CommandMapDebug.ini go to the test helpers.
		if (!TheGlobalData->m_testAids)
		{
			if (UserSettings::getTestAids())
				TheWritableGlobalData->m_testAids = TRUE;
		}
		if (TheGlobalData->m_testAids && TheFileSystem->doesFileExist("ProjectData\\INI\\CommandMapTestAids.ini"))
			ini.load("ProjectData\\INI\\CommandMapTestAids.ini", INILoadType::INI_LOAD_MULTIFILE, nullptr);


		initSubsystem(::Engine::context().actionManager,"TheActionManager", MSGNEW("GameEngineSubsystem") ActionManager(), nullptr);
		//initSubsystem((CComObject<WebBrowser> *)TheWebBrowser,"(CComObject<WebBrowser> *)TheWebBrowser", (CComObject<WebBrowser> *)createWebBrowser(), NULL);
		initSubsystem(::Engine::context().gameStateMap,"TheGameStateMap", MSGNEW("GameEngineSubsystem") GameStateMap, nullptr, nullptr, nullptr );
		initSubsystem(::Engine::context().gameState,"TheGameState", MSGNEW("GameEngineSubsystem") GameState, nullptr, nullptr, nullptr );



	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	WWFormat::Into(Buf, "----------------------------------------------------------------------------After TheGameResultsQueue = {:f} seconds \n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		xferCRC.close();
		TheWritableGlobalData->m_iniCRC = xferCRC.getCRC();
		DEBUG_LOG(("INI CRC is 0x%8.8X\n", TheGlobalData->m_iniCRC));

		TheSubsystemList->postProcessLoadAll();

		if (const char *dumpPath = getGameDataDumpPath())
			writeGameDataDump(dumpPath);

		setFramesPerSecondLimit(TheGlobalData->m_framesPerSecondLimit);

		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_musicOn, AudioAffect_Music);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_soundsOn, AudioAffect_Sound);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_sounds3DOn, AudioAffect_Sound3D);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_speechOn, AudioAffect_Speech);
			
		// We're not in a network game yet, so set the network singleton to NULL.
		::Engine::context().network = nullptr;

		// If we turn m_quitting to FALSE here, then we throw away any requests to quit that
		// took place during loading. :-\ - jkmcd
		// If this really needs to take place, please make sure that pressing cancel on the audio 
		// load music dialog will still cause the game to quit.
		// m_quitting = FALSE;

		// for fingerprinting, we need to ensure the presence of these files


#if !defined(_DEBUG)
		// File names per game. Generals: generalsb.sec in gensec.big, generalsa.sec in music.big. Checked in
		// release builds only.
		const Bool zh = isGameEditionZeroHour();
		const char *secB = zh ? "generalsbzh.sec" : "generalsb.sec";
		const char *secBBig = zh ? "genseczh.big" : "gensec.big";
		const char *secA = zh ? "generalsazh.sec" : "generalsa.sec";
		const char *secABig = zh ? "musiczh.big" : "music.big";
		AsciiString dirName;
    dirName = TheArchiveFileSystem->getArchiveFilenameForFile(secB);

    if (dirName.compareNoCase(secBBig) != 0)
		{
			DEBUG_LOG(("%s was not found in %s - it was in '%s'\n", secB, secBBig, dirName.str()));
			m_quitting = TRUE;
		}
		
		dirName = TheArchiveFileSystem->getArchiveFilenameForFile(secA);
		const char *noPath = dirName.reverseFind('\\');
		if (noPath) {
			dirName = noPath + 1;
		}

		if (dirName.compareNoCase(secABig) != 0)
		{
			DEBUG_LOG(("%s was not found in %s - it was in '%s'\n", secA, secABig, dirName.str()));
			m_quitting = TRUE;
		}
#endif


		// initialize the MapCache
		::Engine::context().mapCache = adoptSubsystem(m_own.mapCache, MSGNEW("GameEngineSubsystem") MapCache);
		TheMapCache->updateCache();


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	WWFormat::Into(Buf, "----------------------------------------------------------------------------After TheMapCache->updateCache = {:f} seconds \n", (static_cast<double>(endTime64-startTime64)/static_cast<double>(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		if (TheGlobalData->m_buildMapCache)
		{
			// just quit, since the map cache has already updated
			//populateMapListbox(NULL, true, true);
			m_quitting = TRUE;
		}
		
		// load the initial shell screen
		//TheShell->push( AsciiString("Menus/MainMenu.wnd") );
		
		// -mission <campaign> <nr>. The campaigns are loaded now; the map replaces the placeholder in
		// m_initialFile and starts below like -file. If not found, the placeholder is removed and the game
		// goes to the main menu.
		if (TheGlobalData->m_testAidsMission.isNotEmpty() && !TestAids_PrepareMission())
			TheWritableGlobalData->m_initialFile.clear();

		// Skirmish without menu (-skirmish); m_initialFile is set as well so the shell skips the main menu
		// and the end of the match quits the game. The match is started in the first tick of the main loop
		// (GameEngine::update), like from the skirmish menu: init() still runs resetAll() and further random
		// accesses afterwards, and the recorder would record the commands with wrong frame numbers.
		if (TheGlobalData->m_autoSkirmishMap.isNotEmpty())
		{
			s_autoSkirmishPending = TRUE;
		}
		// This allows us to run a map/reply from the command line
		else if (TheGlobalData->m_initialFile.isEmpty() == FALSE)
		{
			AsciiString fname2 = TheGlobalData->m_initialFile;
			fname2.toLower();

			if (fname2.endsWithNoCase(".map"))
			{
				TheWritableGlobalData->m_shellMapOn = FALSE;
				TheWritableGlobalData->m_playIntro = FALSE;
				TheWritableGlobalData->m_pendingFile = TheGlobalData->m_initialFile;

				// shutdown the top, but do not pop it off the stack
	//			TheShell->hideShell();

				// send a message to the logic for a new game
				GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
				msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
				msg->appendIntegerArgument(DIFFICULTY_NORMAL);
				msg->appendIntegerArgument(0);
				InitRandom(0);
			}
			else if (fname2.endsWithNoCase(".rep"))
			{
				// Not started here: the resetAll() at the end of init() also resets the recorder and would clear the
				// slot list just read from the replay header, so the match would run without players. Started in the
				// first tick (GameEngine::update), like from the replay menu.
				s_replayPending = TRUE;
			}
		}

		// 
		if (TheMapCache && TheGlobalData->m_shellMapOn)
		{
			AsciiString lowerName = TheGlobalData->m_shellMapName;
			lowerName.toLower();

			MapCache::const_iterator it = TheMapCache->find(lowerName);
			if (it == TheMapCache->end())
			{
				TheWritableGlobalData->m_shellMapOn = FALSE;
			}
		}

		if(!TheGlobalData->m_playIntro)
			TheWritableGlobalData->m_afterIntro = TRUE;

		//initDisabledMasks();
		
	}
	catch (ErrorCode ec)
	{
		if (ec == ERROR_INVALID_D3D)
		{
			RELEASE_CRASHLOCALIZED("ERROR:D3DFailurePrompt", "ERROR:D3DFailureMessage");
		}
	}
	catch (INIException e)
	{
		if (e.mFailureMessage)
			RELEASE_CRASH((e.mFailureMessage));
		else
			RELEASE_CRASH(("Uncaught Exception during initialization."));

	}
	catch (...)
	{
		RELEASE_CRASH(("Uncaught Exception during initialization."));
	}

	if(!TheGlobalData->m_playIntro)
		TheWritableGlobalData->m_afterIntro = TRUE;

	initKindOfMasks();
	initDisabledMasks();
	initDamageTypeFlags();

	TheSubsystemList->resetAll();
	HideControlBar();
}  // end init

/** -----------------------------------------------------------------------------------------------
	* Reset all necessary parts of the game engine to be ready to accept new game data 
	*/
void GameEngine::reset( void )
{

	WindowLayout *background = TheWindowManager->winCreateLayout("Menus/BlankWindow.wnd");
	DEBUG_ASSERTCRASH(background,("We Couldn't Load Menus/BlankWindow.wnd"));
	background->hide(FALSE);
	background->bringForward();
	background->getFirstWindow()->winClearStatus(WIN_STATUS_IMAGE);
	Bool deleteNetwork = false;
	if (TheGameLogic->isInMultiplayerGame())
		deleteNetwork = true;

	if (TheLogicAudio)
		TheLogicAudio->clear();	// drop events not handed over before the audio system is reset
	if (TheLogicToClient)
		TheLogicToClient->clear();
	TheSubsystemList->resetAll();
	// Objects destroyed by the reset queue UI updates for the old game; the UI was reset as well.
	if (TheLogicToClient)
		TheLogicToClient->clear();

	if (deleteNetwork)
	{
		DEBUG_ASSERTCRASH(TheNetwork, ("Deleting NULL TheNetwork!"));
		if (TheNetwork)
			delete TheNetwork;
		::Engine::context().network = nullptr;
	}
	if(background)
	{
		background->destroyWindows();
		deleteInstanceOf(background);
		background = nullptr;
	}
}

/// -----------------------------------------------------------------------------------------------
DECLARE_PERF_TIMER(GameEngine_update)

/** -----------------------------------------------------------------------------------------------
 * Update the game engine by updating the GameClient and GameLogic singletons.
 * @todo Allow the client to run as fast as possible, but limit the execution
 * of TheNetwork and TheGameLogic to a fixed framerate.
 */
// The tick of a running game goes to the simulation thread. Map loading (the load screen draws and
// presents itself) and the shell map stay on the main thread.
static Bool useSimThreadForTick( void )
{
	return ::Engine::simThread().isRunning() && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame()
		&& !TheGameLogic->isNewGamePending();
}

void GameEngine::update( void )
{
	// Access to logic objects is allowed during the tick (client and logic run one after the other in the
	// same thread here). Outside of it, i.e. in the render-only frames, the guard reports every marked
	// place. This only collects findings and does not change behavior.
	SCOPED_LOGIC_ACCESS();
 
	USE_PERF_TIMER(GameEngine_update)
	{

		{
			
			// VERIFY CRC needs to be in this code block.  Please to not pull TheGameLogic->update() inside this block.
			VERIFY_CRC

			ArchivePrefetch_Poll();
			{
				// Asserts from non-main threads show no message box there but end up in a queue. The main thread
				// reports them here like a normal assert.
				char pendingAssert[512];
				while (::Engine::Log::pollPendingAssert(pendingAssert, sizeof(pendingAssert)))
				{
					DEBUG_CRASH(("assert from a worker thread: %s", pendingAssert));
				}
			}
			TheRadar->UPDATE();

			/// @todo Move audio init, update, etc, into GameClient update
			
			TheAudio->UPDATE();
			TheGameClient->UPDATE();
			TheMessageStream->propagateMessages();

			if (TheNetwork != nullptr)
			{
				TheNetwork->UPDATE();
			}
			// the host serves spectators of its game, a spectator receives the game it watches
			if (TheLobby != nullptr)
			{
				TheLobby->serviceInGame();
			}
		}


		feedWatchTest();
		// A watched game waits where the host's stream has not arrived yet, like a network game waits
		// for a late player.
		const Bool liveWaiting = TheRecorder != nullptr && TheRecorder->isLiveWaiting();
		if ((TheNetwork == nullptr && !TheGameLogic->isGamePaused() && !liveWaiting) || (TheNetwork && TheNetwork->isFrameDataReady()))
		{
			if (useSimThreadForTick())
				::Engine::simThread().runAndWait([]() { SCOPED_LOGIC_ACCESS(); TheGameLogic->UPDATE(); });
			else
				TheGameLogic->UPDATE();
		}
		// Hand the collected logic audio calls to the audio system in order. Every frame, not only after a
		// tick: nothing may be left over during pause or loading either.
		if (TheLogicAudio)
			TheLogicAudio->flush();
		if (TheLogicToClient)
			TheLogicToClient->flush();
		// The drawables keep a copy of what drawing reads from their objects.
		if (TheGameClient)
			TheGameClient->captureObjectViews();

		// Test sequence (-testablauf) after client, message stream and logic
		if (TheGlobalData->m_testScriptFile.isNotEmpty())
			TestScript_Update();

		// Start the auto skirmish in the first tick of the main loop (see init()). The message is propagated
		// in the next tick and processed by the logic, like a start from the menu.
		if (s_autoSkirmishPending && !m_quitting && !TheGameLogic->isInGame())
		{
			s_autoSkirmishPending = FALSE;
			startAutoSkirmish();
		}
		if (s_replayPending && !m_quitting && !TheGameLogic->isInGame())
		{
			s_replayPending = FALSE;
			Bool started = FALSE;
			if (TheGlobalData->m_watchTestStart > 0)
			{
				RecorderClass::ReplayHeader header;
				header.forPlayback = FALSE;
				header.filename = TheGlobalData->m_initialFile;
				if (TheRecorder->readReplayHeader(header) && header.frameDuration > 0)
				{
					s_watchTestEnd = header.frameDuration + 1;
					started = TheRecorder->playbackLive(TheGlobalData->m_initialFile);
				}
			}
			else
			{
				started = TheRecorder->playbackFile(TheGlobalData->m_initialFile);
			}
			if (!started)
			{
				// Runner runs must not get stuck in an empty main menu
				GameEngine_RunnerLog("ERROR replay not found or invalid");
				TheGameExitCode = GAME_EXIT_NOT_FOUND;
				m_quitting = TRUE;
			}
		}

		// -maxFrames ends the match. When recording via MSG_CLEAR_GAME_DATA so the recorder closes the replay
		// cleanly (GameLogic::clearGameData then quits the game because of m_initialFile); when playing back
		// or without recorder directly.
		if (TheGlobalData->m_maxFrames > 0 && !m_quitting && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame()
			&& TheGameLogic->getFrame() >= TheGlobalData->m_maxFrames)
		{
			static Bool s_maxFramesHandled = FALSE;
			if (!s_maxFramesHandled)
			{
				s_maxFramesHandled = TRUE;
				char line[64];
				WWFormat::Into(line, "END maxFrames {:d}", TheGameLogic->getFrame());
				GameEngine_RunnerLog(line);
				if (TheRecorder && TheRecorder->getMode() == RecorderModeType::RECORDERMODETYPE_RECORD && TheGlobalData->m_initialFile.isNotEmpty())
					TheMessageStream->appendMessage(GameMessage::MSG_CLEAR_GAME_DATA);
				else
				{
					// Like -quitAfter, clean up the match before quitting. Otherwise shutdownAll() destroys the radar
					// before the PlayerList, and ~PlayerList -> init() -> Player::becomingLocalPlayer calls
					// TheRadar->addObject on the freed radar.
					TheGameLogic->clearGameData( FALSE );
					m_quitting = TRUE;
				}
			}
		}

		// With -aiStats a recorded match ends as soon as only one alliance is left (KISIEG in the runner log);
		// AI series need the winner, not the time until -maxFrames. Ends like -maxFrames.
		if (TheGlobalData->m_aiStatsInterval > 0 && !m_quitting && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame()
			&& TheRecorder && TheRecorder->getMode() != RecorderModeType::RECORDERMODETYPE_PLAYBACK)
		{
			static Bool s_aiVictoryHandled = FALSE;
			if (!s_aiVictoryHandled && AIStats_logVictoryIfDecided(TheGameLogic->getFrame()))
			{
				s_aiVictoryHandled = TRUE;
				if (TheRecorder->getMode() == RecorderModeType::RECORDERMODETYPE_RECORD && TheGlobalData->m_initialFile.isNotEmpty())
					TheMessageStream->appendMessage(GameMessage::MSG_CLEAR_GAME_DATA);
				else
				{
					TheGameLogic->clearGameData( FALSE );
					m_quitting = TRUE;
				}
			}
		}

		// In the runner, treat the end of the replay like -maxFrames; see RecorderClass::stopPlayback.
		if (TheGlobalData->m_runnerMode && !m_quitting && TheRecorder && TheRecorder->isPlaybackFinished()
			&& TheGameLogic->isInGame() && !TheGameLogic->isInShellGame())
		{
			char line[64];
			WWFormat::Into(line, "END replay {:d}", TheGameLogic->getFrame());
			GameEngine_RunnerLog(line);
			TheGameLogic->clearGameData( FALSE );
			m_quitting = TRUE;
		}

	}	// end perfGather

}

// Horrible reference, but we really, really need to know if we are windowed.
extern bool RenderDevice_IsWindowed;
extern HWND ApplicationHWnd;

// Duration of each logic tick for benchmark evaluation (only with -crcLog). Loading (frames 0 to 2)
// and the shell do not count. TickTimer in runTick() fills the list, execute() evaluates it at the
// end, hence it is defined up here. On the heap rather than static objects: a static std::vector
// keeps its iterator proxy in the debug build until after WinMain and counts as a leak.
static std::vector<float> *s_tickMs = nullptr;
static std::vector<float> *s_frameMs = nullptr;	// duration of each main loop pass (render baseline)
static std::vector<float> &timingVector(std::vector<float> *&p)
{
	if (p == nullptr)
		p = new std::vector<float>;
	return *p;
}

// Timing statistics (mean, percentiles, maximum) to the runner log, individual values as CSV next to it.
static void writeTimingStats( const char *label, const char *countName, const char *csvColumn,
	const std::vector<float> &values, const char *csvSuffix )
{
	if (values.empty())
		return;
	std::vector<float> sorted(values);
	std::sort(sorted.begin(), sorted.end());
	double sum = 0.0;
	for (size_t i = 0; i < sorted.size(); ++i)
		sum += sorted[i];
	const size_t n = sorted.size();
	char line[256];
	WWFormat::Into(line, "{} {} {:d} mean {:.3f} p50 {:.3f} p95 {:.3f} p99 {:.3f} max {:.3f} total {:.0f} ms", label, countName, static_cast<unsigned>(n), sum / n, sorted[n / 2], sorted[(n * 95) / 100], sorted[(n * 99) / 100], sorted[n - 1], sum);
	GameEngine_RunnerLog(line);
	AsciiString csvName = TheGlobalData->m_crcLogFile;
	csvName.concat(csvSuffix);
	FILE *csv = fopen(csvName.str(), "w");
	if (csv)
	{
		fprintf(csv, "%s,ms\n", csvColumn);
		for (size_t i = 0; i < values.size(); ++i)
			fprintf(csv, "%u,%.4f\n", static_cast<unsigned>(i), values[i]);
		fclose(csv);
	}
}

// -logRenderTime <file>: one CSV line per shown frame with its presentation time, to check that it
// runs steadily across tick and in-between frames. Columns:
//   ms       real time after drawing (with VSync: after Present), since the first line
//   art      T = tick frame after the logic (interpolated), R = in-between frame (interpolated),
//            F = tick frame before the logic without interpolation (-fixedTimeStep, -runner, time lapse,
//            frozen camera path); there the objects show "anzeige", the camera already anzeige + 1
//   tick     GameClient tick counter, frame = logic frame, alpha = interpolation fraction
//   anzeige  shown point in time in ticks: tick - 1 + alpha (objects lerp(T-1, T, alpha))
//   ticklen  tick length in ms
// Steady means: anzeige never falls and grows per frame by about ms delta / ticklen
// (tools/render/rendertime_check.py). Without the switch the function returns immediately.
static FILE *s_renderTimeLog = nullptr;
static Bool s_renderTimeLogFailed = FALSE;
static LARGE_INTEGER s_renderTimeStart;
static void logRenderTime( char kind, Real alpha, Real tickLenMs )
{
	if (TheGlobalData == nullptr || TheGlobalData->m_renderTimeLogFile.isEmpty() || s_renderTimeLogFailed
		|| TheGameClient == nullptr || TheGameLogic == nullptr)
		return;
	LARGE_INTEGER now, freq;
	::QueryPerformanceCounter(&now);
	::QueryPerformanceFrequency(&freq);
	if (s_renderTimeLog == nullptr)
	{
		s_renderTimeLog = fopen(TheGlobalData->m_renderTimeLogFile.str(), "w");
		if (s_renderTimeLog == nullptr)
		{
			s_renderTimeLogFailed = TRUE;
			return;
		}
		setvbuf(s_renderTimeLog, nullptr, _IOFBF, 1 << 16);
		fprintf(s_renderTimeLog, "ms,art,tick,frame,alpha,anzeige,ticklen\n");
		s_renderTimeStart = now;
	}
	const double ms = static_cast<double>(now.QuadPart - s_renderTimeStart.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
	const UnsignedInt tick = TheGameClient->getTickSerial();
	fprintf(s_renderTimeLog, "%.3f,%c,%u,%u,%.4f,%.4f,%.3f\n", ms, kind, tick, TheGameLogic->getFrame(),
		alpha, static_cast<double>(tick) - 1.0 + static_cast<double>(alpha), tickLenMs);
}

/** -----------------------------------------------------------------------------------------------
 * The "main loop" of the game engine. It will not return until the game exits. 
 */
void GameEngine::execute( void )
{
	
	DWORD prevTime = timeGetTime();
	Real tickAccum = 0.0f;	// elapsed time in ms not yet ticked
#if defined(_DEBUG)
	UnsignedInt tickFrames = 0, renderOnlyFrames = 0;	// statistics for the debug log
	DWORD statsTime = prevTime;
#endif
	// -affinity N: measure "4 cores/8 threads" on larger machines.
	if (TheGlobalData->m_affinityLogicalProcessors > 0)
	{
		DWORD_PTR processMask = 0, systemMask = 0;
		if (::GetProcessAffinityMask(::GetCurrentProcess(), &processMask, &systemMask))
		{
			DWORD_PTR mask = 0;
			Int taken = 0;
			for (Int bit = 0; bit < static_cast<Int>(sizeof(DWORD_PTR) * 8) && taken < TheGlobalData->m_affinityLogicalProcessors; ++bit)
			{
				DWORD_PTR b = static_cast<DWORD_PTR>(1) << bit;
				if (systemMask & b)
				{
					mask |= b;
					++taken;
				}
			}
			char line[96];
			WWFormat::Into(line, "AFFINITY {:d} logical processors mask {:X} {}", taken, static_cast<unsigned long long>(mask), (mask != 0 && ::SetProcessAffinityMask(::GetCurrentProcess(), mask)) ? "ok" : "FAILED");
			GameEngine_RunnerLog(line);
		}
	}

	{	// cap for render-only frames from Options.ini (0 = none; VSync limits anyway)
		m_renderFpsLimit = UserSettings::getRenderFpsLimit();
	}
#if defined(_DEBUG)
	DWORD startTime = timeGetTime() / 1000;
#endif

	// pretty basic for now
	// Render cap: every drawn frame, tick frames included, takes one slot of a fixed schedule (paceDue is the time
	// the next frame is due). The schedule does not drift with the time a frame took, so with VSync a limit that is
	// no divisor of the refresh rate still averages out (120 on 144 Hz alternates one and two refreshes per frame).
	LARGE_INTEGER qpcFreq;
	::QueryPerformanceFrequency(&qpcFreq);
	LONGLONG paceDue = 0;
	const auto qpcNow = []() { LARGE_INTEGER t; ::QueryPerformanceCounter(&t); return t.QuadPart; };
	const auto paceAdvance = [&]()
	{
		if (m_renderFpsLimit <= 0)
			return;
		const LONGLONG interval = qpcFreq.QuadPart / m_renderFpsLimit;
		const LONGLONG now = qpcNow();
		paceDue += interval;
		if (now > paceDue)
			paceDue = now;	// behind the schedule: no burst to catch up
	};

	while( !m_quitting )
	{
		LARGE_INTEGER frameStart;	// frame times for the render baseline
		::QueryPerformanceCounter(&frameStart);

		// Window messages in every pass, render-only frames included: input waits in the device buffers
		// until a tick translates it, and a tick in this pass gets what arrived just before it.
		serviceWindowsOS();
		if (m_quitting)
			continue;

		//if (TheGlobalData->m_vTune)
		{
		}

		{

#if defined(_DEBUG)
			{
				// enter only if in benchmark mode
				if (TheGlobalData->m_benchmarkTimer > 0)
				{
					DWORD currentTime = timeGetTime() / 1000;
					if (static_cast<DWORD>(TheGlobalData->m_benchmarkTimer) < currentTime - startTime)
					{
						if (TheGameLogic->isInGame())
						{
							if (TheRecorder->getMode() == RecorderModeType::RECORDERMODETYPE_RECORD)
							{
								TheRecorder->stopRecording();
							}
							TheGameLogic->clearGameData( FALSE );
						}
						TheGameEngine->setQuitting(TRUE);
					}
				}
			}
#endif
			
			{
				// Logic ticks run at a fixed rate (m_maxFPS ticks/s: GameData.ini FramesPerSecondLimit or game
				// speed), rendering happens as often as possible. A time accumulator decides per pass whether a tick
				// is due (update(): client, network, logic; the tick draws a frame itself); otherwise a render-only
				// frame is drawn that interpolates drawables, camera and animation time between the last two tick
				// states. Time lapse (replay multiplier, script "time fast", ZH TiVO fast forward) runs one tick per
				// pass, unthrottled. The tick rate does not depend on m_useFpsLimit.
				const Bool tivoFast = TheGlobalData->m_TiVOFastMode && TheGameLogic->isInReplayGame();
				// -runner computes ticks without waiting, like time lapse.
				// -fixedTimeStep likewise, but draws every tick (no interpolated render frames).
				// A spectator who joined late catches up with the game the same way, as does a player coming
				// back into a running game.
				const Bool liveCatchUp = (TheRecorder != nullptr && TheRecorder->isLiveCatchingUp())
					|| (TheNetwork != nullptr && TheNetwork->isCatchingUp());
				const Bool fastTime = TheGlobalData->m_runnerMode || TheGlobalData->m_fixedTimeStep
					|| TheTacticalView->getTimeMultiplier() > 1 || TheScriptEngine->isTimeFast() || tivoFast || liveCatchUp;
				const Bool decoupled = !fastTime && m_maxFPS > 0;
				DWORD now = timeGetTime();
				DWORD elapsed = now - prevTime;
				prevTime = now;
				if (decoupled)
				{
					const Real tickLen = 1000.0f / static_cast<Real>(m_maxFPS);
					if (elapsed > 250)
						elapsed = 250;	// cap against catch-up spirals (debugger, Alt-Tab, loading pause)
					// Test helper time lapse xN: the clock runs N times as fast and more ticks are computed per pass;
					// the tick rate itself (logic frames per game second) stays. Single player only, otherwise 1.
					const Int fastForward = TestAids_FastForward();
					tickAccum += static_cast<Real>(elapsed) * static_cast<Real>(fastForward);
					const Int MAX_TICKS_PER_LOOP = 4 * fastForward;
					Int ticks = 0;
					while (tickAccum >= tickLen && ticks < MAX_TICKS_PER_LOOP)
					{
						// Draw the tick's frame after the logic, interpolated with the alpha valid after the tick. Drawing
						// before the logic without interpolation would put the camera on the new tick while the following
						// in-between frame (lerp(T-1, T, alpha)) is behind it again; at 60 Hz with VSync both alternate and
						// moving things appear twice.
						const Bool deferDraw = canDeferTickDraw();
						if (TheGameClient != nullptr)
							TheGameClient->setDeferTickDraw(deferDraw);
						runTick();
						if (TheGameClient != nullptr)
							TheGameClient->setDeferTickDraw(FALSE);
						tickAccum -= tickLen;
#if defined(_DEBUG)
						++tickFrames;
#endif
						++ticks;
						if (ticks == MAX_TICKS_PER_LOOP && tickAccum >= tickLen)
							tickAccum = 0.0f;	// cannot keep up: drop the rest, the game runs slower instead of spiraling
						// Several ticks in one pass: the early frames stand at alpha 1 (end of their tick), only the last
						// one shows the state that matches real time.
						// Test helper time lapse: only draw the frame of the last tick in the pass
						const Bool lastTick = (fastForward == 1) || !(tickAccum >= tickLen && ticks < MAX_TICKS_PER_LOOP);
						if (deferDraw)
						{
							if (lastTick)
								renderTickFrame(tickAccum / tickLen);
						}
						else
							logRenderTime('F', 0.0f, tickLen);
					}
					if (ticks > 0)
					{
						if (paceDue == 0)
							paceDue = qpcNow();
						else
							paceAdvance();
					}
					if (ticks == 0)
					{
						// Render cap (Options.ini RenderFPSLimit): wait until the frame is due. A logic tick that falls due
						// meanwhile is not delayed; its pass draws instead.
						if (m_renderFpsLimit > 0 && paceDue != 0)
						{
							const LONGLONG target = paceDue;
							Bool tickDue = FALSE;
							for (;;)
							{
								const LONGLONG left = target - qpcNow();
								if (left <= 0)
									break;
								if (tickAccum + static_cast<Real>(timeGetTime() - prevTime) * static_cast<Real>(fastForward) >= tickLen)
								{
									tickDue = TRUE;
									break;
								}
								if (left * 1000 > 2 * qpcFreq.QuadPart)
									::Sleep(1);
								else
									::SwitchToThread();
							}
							if (tickDue)
								continue;
						}
#if defined(_DEBUG)
						++renderOnlyFrames;
#endif
						const DWORD renderStart = timeGetTime();
						renderInterpolatedFrame(tickAccum / tickLen);
						if (paceDue == 0)
							paceDue = qpcNow();
						else
							paceAdvance();
						// Without a cap, relief when nothing was drawn: never spin hard.
						if (m_renderFpsLimit <= 0 && timeGetTime() - renderStart < 1)
							::Sleep(1);
					}
#if defined(_DEBUG)
					if (now - statsTime >= 5000)
					{
						DEBUG_LOG(("%u ticks, %u render-only frames in %u ms\n", tickFrames, renderOnlyFrames, static_cast<unsigned>(now - statsTime)));
						tickFrames = renderOnlyFrames = 0;
						statsTime = now;
					}
#endif
				}
				else
				{
					tickAccum = 0.0f;
					// With -fixedTimeStep the render clock advances exactly one tick length per pass.
					if (TheGlobalData->m_fixedTimeStep)
						s_fixedRenderClockMs += (m_maxFPS > 0) ? static_cast<UnsignedInt>(1000 / m_maxFPS) : 33;
					runTick();
					logRenderTime('F', 0.0f, (m_maxFPS > 0) ? 1000.0f / static_cast<Real>(m_maxFPS) : 1000.0f / 30.0f);	// only with -logRenderTime
#if defined(_DEBUG)
					// I'm disabling this in internal because many people need alt-tab capability.  If you happen to be
					// doing performance tuning, please just change this on your local system. -MDC
					if (!fastTime)
						::Sleep(1); // give everyone else a tiny time slice.
#endif
				}
			}

		}	// perfgather for execute_loop

		PROF_FRAME();	// one main loop pass = one drawn frame (tick or render frame)

		// Duration of each pass for the render baseline, only with -crcLog and without the first 60 client
		// frames (loading, fade-in).
		if (TheGlobalData->m_crcLogFile.isNotEmpty() && TheGameClient != nullptr && TheGameClient->getFrame() > 60)
		{
			LARGE_INTEGER frameEnd, freq;
			::QueryPerformanceCounter(&frameEnd);
			::QueryPerformanceFrequency(&freq);
			timingVector(s_frameMs).push_back(static_cast<float>((frameEnd.QuadPart - frameStart.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart)));
		}

		// -quitAfter N quits after N client frames, also in the main menu and on the shell map
		// (-maxFrames only applies in matches).
		if (TheGlobalData->m_quitAfterFrames > 0 && !m_quitting && TheGameClient != nullptr
			&& TheGameClient->getFrame() >= TheGlobalData->m_quitAfterFrames)
		{
			char line[64];
			WWFormat::Into(line, "END quitAfter {:d}", TheGameClient->getFrame());
			GameEngine_RunnerLog(line);
			// Like the -benchmark path: clean up a running match (including the shell map) before quitting.
			// Otherwise shutdownAll() destroys the PlayerList before the TeamFactory, and the teams access freed
			// players when deleted (crash in Player::removeTeamRelationship).
			if (TheGameLogic->isInGame())
			{
				if (TheRecorder && TheRecorder->getMode() == RecorderModeType::RECORDERMODETYPE_RECORD)
					TheRecorder->stopRecording();
				TheGameLogic->clearGameData( FALSE );
			}
			setQuitting(TRUE);
		}
		::Engine::Watchdog::get().heartbeat( ::Engine::WATCHDOG_MAIN );


	}

	// Tick statistics (mean, percentiles, maximum) to the runner log, individual values as CSV next to it;
	// the same evaluation for all main loop passes (render baseline, FRAMES line).
	writeTimingStats("BENCH", "ticks", "tick", timingVector(s_tickMs), ".ticks.csv");
	writeTimingStats("FRAMES", "frames", "frame", timingVector(s_frameMs), ".frames.csv");
	if (TheAI && TheAI->pathfinder())
	{
		char line[256];
		TheAI->pathfinder()->formatStatistics(line, sizeof(line));
		if (line[0])
		{
			GameEngine_RunnerLog(line);
			char kinds[1024];
			TheAI->pathfinder()->formatSearchStatistics(kinds, sizeof(kinds));
			GameEngine_RunnerLog(kinds);
		}
	}
	if (TheGameLogic)
	{
		AsciiString phases, modules, spikes;
		if (TheGameLogic->formatPhaseStatistics(phases, modules, spikes))
		{
			GameEngine_RunnerLog(phases.str());
			GameEngine_RunnerLog(modules.str());
			GameEngine_RunnerLog(spikes.str());
		}
		TheGameLogic->releasePhaseStatistics();
	}
	if (TheDisplay)
		TheDisplay->writeRenderStatistics();	// GPUEFFEKTE line, only with a selected effect
	delete s_tickMs; s_tickMs = nullptr;
	delete s_frameMs; s_frameMs = nullptr;

	if (s_renderTimeLog != nullptr)	// -logRenderTime
	{
		fclose(s_renderTimeLog);
		s_renderTimeLog = nullptr;
	}

	// Final line for the runner log and copy of the recorded replay (-recordReplay).
	{
		char line[64];
		WWFormat::Into(line, "EXIT code {:d}", TheGameExitCode);
		GameEngine_RunnerLog(line);
	}
	if (TheGlobalData->m_recordReplayFile.isNotEmpty() && TheRecorder)
	{
		AsciiString recorded = TheRecorder->getReplayDir();
		recorded.concat(TheRecorder->getLastReplayFileName());
		recorded.concat(TheRecorder->getReplayExtention());
		if (!::CopyFileW(::Engine::pathFromNarrow(recorded.str()).c_str(), ::Engine::pathFromNarrow(TheGlobalData->m_recordReplayFile.str()).c_str(), FALSE))
		{
			DEBUG_LOG(("replay copy %s -> %s failed (%u)\n", recorded.str(), TheGlobalData->m_recordReplayFile.str(), static_cast<unsigned>(::GetLastError())));
			GameEngine_RunnerLog("ERROR recordReplay copy failed");
		}
		else
			::DeleteFileW(::Engine::pathFromNarrow(recorded.str()).c_str());	// separate file per process, do not leave it behind after copying
	}

}

/** -----------------------------------------------------------------------------------------------
	* Factory for the message stream
	*/
MessageStream *GameEngine::createMessageStream( void )
{
	// if you change this update the tools that use the engine systems
	// like GUIEdit, it creates a message stream to run in "test" mode
	return MSGNEW("GameEngineSubsystem") MessageStream;
}

//-------------------------------------------------------------------------------------------------
FileSystem *GameEngine::createFileSystem( void )
{
	return MSGNEW("GameEngineSubsystem") FileSystem;
}

//-------------------------------------------------------------------------------------------------
Bool GameEngine::isMultiplayerSession( void )
{
	return TheRecorder->isMultiplayer();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
constexpr char CONVERT_EXEC1[] = "..\\Build\\nvdxt -list buildDDS.txt -dxt5 -full -outdir Art\\Textures > buildDDS.out";

void updateTGAtoDDS()
{
	// Here's the scoop. We're going to traverse through all of the files in the Art\Textures folder
	// and determine if there are any .tga files that are newer than associated .dds files. If there 
	// are, then we will re-run the compression tool on them.
	
	File *fp = TheLocalFileSystem->openFile("buildDDS.txt", File::WRITE | File::CREATE | File::TRUNCATE | File::TEXT);
	if (!fp) {
		return;
	}

	FilenameList files;
	TheLocalFileSystem->getFileListInDirectory("Art\\Textures\\", "", "*.tga", files, TRUE);
	FilenameList::iterator it;
	for (it = files.begin(); it != files.end(); ++it) {
		AsciiString filenameTGA = *it;
		AsciiString filenameDDS = *it;
		FileInfo infoTGA;
		TheLocalFileSystem->getFileInfo(filenameTGA, &infoTGA);

		// skip the water textures, since they need to be NOT compressed
		filenameTGA.toLower();
		if (strstr(filenameTGA.str(), "caust"))
		{
			continue;
		}
		// and the recolored stuff.
		if (strstr(filenameTGA.str(), "zhca"))
		{
			continue;
		}

		// replace tga with dds
		filenameDDS.removeLastChar();	// a
		filenameDDS.removeLastChar();	// g
		filenameDDS.removeLastChar();	// t
		filenameDDS.concat("dds");

		Bool needsToBeUpdated = FALSE;
		FileInfo infoDDS;
		if (TheFileSystem->doesFileExist(filenameDDS.str())) {
			TheFileSystem->getFileInfo(filenameDDS, &infoDDS);
			if (infoTGA.timestampHigh > infoDDS.timestampHigh || 
					(infoTGA.timestampHigh == infoDDS.timestampHigh && 
					 infoTGA.timestampLow > infoDDS.timestampLow)) {
				needsToBeUpdated = TRUE;
			}
		} else {
			needsToBeUpdated = TRUE;
		}

		if (!needsToBeUpdated) {
			continue;
		}

		filenameTGA.concat("\n");
		fp->write(filenameTGA.str(), filenameTGA.getLength());
	}

	fp->close();

	system(CONVERT_EXEC1);
}

//-------------------------------------------------------------------------------------------------
// System things

// If we're using the Wide character version of MessageBox, then there's no additional
// processing necessary. Please note that this is a sleazy way to get this information,
// but pending a better one, this'll have to do.
extern const Bool TheSystemIsUnicode = ((reinterpret_cast<void*>(::MessageBox)) == (reinterpret_cast<void*>(::MessageBoxW)));

// -----------------------------------------------------------------------------------------------
// Starts a skirmish without menu. The local player is an observer, all other slots are AI (brutal,
// faction/color/start position random from the game seed). With as many AIs as slots the observer takes
// no slot: the game then makes its always present observer player (ReplayObserver) the local one. This runs AI-vs-AI matches for reference
// replays, benchmarks and long runs unattended; the recorder records them like any match. Modeled on
// SkirmishGameOptionsMenu (Init and reallyDoStart).
//
// Entry n of the -skirmishAI list ("N1,N1,K2,K2"): letter K = classic AI (Brutal), N = new AI (Brutal),
// E = new AI Easy, M = new AI Medium; digit = team 1 to 4 (missing or 0: no team); then optionally the
// difficulty for either AI: e = easy, m = medium, b = brutal ("K2e", "N1m"); and for the new AI a personality:
// a = aggressive, t = technician, i = turtle ("N1a", "N2mi"). A leading H<digit> is the local player as a
// human partner in that team instead of the observer ("H1,N1,K2"); the AI entries follow it.
static Bool autoSkirmishHuman( Int *team )
{
	const char *p = TheGlobalData->m_autoSkirmishSpec.str();
	if (p == nullptr)
		return FALSE;
	while (*p == ' ')
		++p;
	if (*p != 'H' && *p != 'h')
		return FALSE;
	const Int t = (p[1] >= '0' && p[1] <= '9') ? atoi(p + 1) : 0;
	*team = (t >= 1 && t <= MAX_SLOTS / 2) ? t - 1 : -1;
	return TRUE;
}

static Bool autoSkirmishSlotSpec( Int n, Int *aiKind, SlotState *state, Int *team, Int *personality )
{
	const char *p = TheGlobalData->m_autoSkirmishSpec.str();
	if (p == nullptr || *p == 0)
		return FALSE;
	Int humanTeam = -1;
	if (autoSkirmishHuman( &humanTeam ))
		++n;
	for (Int k = 0; k < n; ++k)
	{
		p = strchr(p, ',');
		if (p == nullptr)
			return FALSE;
		++p;
	}
	while (*p == ' ')
		++p;
	*aiKind = (*p == 'N' || *p == 'n' || *p == 'E' || *p == 'e' || *p == 'M' || *p == 'm') ? AI_KIND_COMMANDER : AI_KIND_CLASSIC;
	*state = (*p == 'E' || *p == 'e') ? SLOT_EASY_AI : ((*p == 'M' || *p == 'm') ? SLOT_MED_AI : SLOT_BRUTAL_AI);
	if (*p != 0 && *p != ',')
		++p;
	Int t = (*p >= '0' && *p <= '9') ? atoi(p) : 0;
	*team = (t >= 1 && t <= MAX_SLOTS / 2) ? t - 1 : -1;
	while (*p >= '0' && *p <= '9')
		++p;
	*personality = AI_PERSONALITY_STANDARD;
	for (; *p != 0 && *p != ','; ++p)
	{
		if (*p == 'e')
			*state = SLOT_EASY_AI;
		else if (*p == 'm')
			*state = SLOT_MED_AI;
		else if (*p == 'b')
			*state = SLOT_BRUTAL_AI;
		else if (*p == 'a')
			*personality = AI_PERSONALITY_AGGRESSIVE;
		else if (*p == 't')
			*personality = AI_PERSONALITY_TECHNICIAN;
		else if (*p == 'i')
			*personality = AI_PERSONALITY_TURTLE;
	}
	return TRUE;
}

static void startAutoSkirmish( void )
{
	const MapMetaData *md = TheMapCache ? TheMapCache->findMap(TheGlobalData->m_autoSkirmishMap) : nullptr;
	if (md == nullptr)
	{
		DEBUG_LOG(("map '%s' not in the map cache\n", TheGlobalData->m_autoSkirmishMap.str()));
		GameEngine_RunnerLog("ERROR map not found");
		TheGameExitCode = GAME_EXIT_NOT_FOUND;
		TheGameEngine->setQuitting(TRUE);
		return;
	}

	if (TheSkirmishGameInfo == nullptr)
		::Engine::context().skirmishGameInfo = NEW SkirmishGameInfo;
	TheSkirmishGameInfo->init();
	TheSkirmishGameInfo->clearSlotList();
	TheSkirmishGameInfo->reset();
	TheSkirmishGameInfo->setLocalIP(TheSkirmishGameInfo->getSlot(0)->getIP());
	TheSkirmishGameInfo->enterGame();

	Int humanTeam = -1;
	const Bool human = autoSkirmishHuman( &humanTeam );
	Int numAI = TheGlobalData->m_autoSkirmishAIs;
	if (md->m_numPlayers > 0 && numAI > md->m_numPlayers - (human ? 1 : 0))
		numAI = md->m_numPlayers - (human ? 1 : 0);
	if (human && numAI > MAX_SLOTS - 1)
		numAI = MAX_SLOTS - 1;
	const Int firstAISlot = (numAI >= MAX_SLOTS) ? 0 : 1;
	if (human)
	{
		GameSlot partner;
		partner.setState(SLOT_PLAYER, UnicodeString(L"Partner"));
		partner.setPlayerTemplate(PLAYERTEMPLATE_RANDOM);
		partner.setColor(-1);
		partner.setStartPos(-1);
		partner.setTeamNumber(humanTeam);
		TheSkirmishGameInfo->setSlot(0, partner);
	}
	else if (firstAISlot == 1)
	{
		GameSlot observer;
		observer.setState(SLOT_PLAYER, UnicodeString(L"Observer"));
		observer.setPlayerTemplate(PLAYERTEMPLATE_OBSERVER);
		observer.setColor(-1);
		TheSkirmishGameInfo->setSlot(0, observer);
	}
	for (Int i = firstAISlot; i < firstAISlot + numAI && i < MAX_SLOTS; ++i)
	{
		GameSlot ai;
		ai.setState(SLOT_BRUTAL_AI);
		ai.setPlayerTemplate(PLAYERTEMPLATE_RANDOM);
		ai.setColor(-1);
		ai.setStartPos(-1);
		ai.setTeamNumber(-1);
		// -skirmishAI as a per-slot list ("N1,N1,K2,K2")
		Int aiKind = AI_KIND_CLASSIC, team = -1, personality = AI_PERSONALITY_STANDARD;
		SlotState aiState = SLOT_BRUTAL_AI;
		if (autoSkirmishSlotSpec(i - firstAISlot, &aiKind, &aiState, &team, &personality))
		{
			ai.setState(aiState);
			ai.setTeamNumber(team);
			ai.setAIKind(aiKind);
			if (aiKind == AI_KIND_COMMANDER)
				ai.setAIPersonality(personality);
		}
		TheSkirmishGameInfo->setSlot(i, ai);
	}

	// Without any AI there is nothing to play against. One AI party is fine: the local observer needs no party of its own.
	Int parties = 0;
	UnsignedInt teamsSeen = 0;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		const GameSlot *slot = TheSkirmishGameInfo->getConstSlot(i);
		if (slot == nullptr || !slot->isAI())
			continue;
		const Int team = slot->getTeamNumber();
		if (team < 0)
			++parties;
		else if ((teamsSeen & (1u << team)) == 0)
		{
			teamsSeen |= 1u << team;
			++parties;
		}
	}
	if (parties < 1)
	{
		DEBUG_LOG(("auto skirmish: %d parties, at least one is needed\n", parties));
		GameEngine_RunnerLog("ERROR auto skirmish needs at least one AI");
		TheGameExitCode = GAME_EXIT_BAD_SETUP;
		TheGameEngine->setQuitting(TRUE);
		return;
	}

	TheSkirmishGameInfo->setMap(md->m_fileName);
	TheSkirmishGameInfo->setMapCRC(md->m_CRC);
	TheSkirmishGameInfo->setMapSize(md->m_filesize);
	TheSkirmishGameInfo->setSeed(TheGlobalData->m_autoSkirmishSeed);
	TheSkirmishGameInfo->setGraceMinutes(static_cast<UnsignedShort>(TheGlobalData->m_autoSkirmishGraceMinutes));
	TheSkirmishGameInfo->setSuperweaponRestriction(TheGlobalData->m_autoSkirmishSuperweaponLimit ? 1 : 0);
	if (TheGlobalData->m_autoSkirmishStartingCash > 0)
	{
		Money cash;
		cash.deposit(static_cast<UnsignedInt>(TheGlobalData->m_autoSkirmishStartingCash), FALSE);
		TheSkirmishGameInfo->setStartingCash(cash);
	}

	TheWritableGlobalData->m_mapName = md->m_fileName;
	TheSkirmishGameInfo->startGame(0);
	InitGameLogicRandom(TheSkirmishGameInfo->getSeed());

	DEBUG_LOG(("auto skirmish '%s', %d AI, seed %d\n", md->m_fileName.str(), numAI, TheSkirmishGameInfo->getSeed()));

	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument(md->m_isMultiplayer ? GAME_SKIRMISH : GAME_SINGLE_PLAYER);
	msg->appendIntegerArgument(DIFFICULTY_NORMAL);
	msg->appendIntegerArgument(0);
	msg->appendIntegerArgument(TheGlobalData->m_framesPerSecondLimit);
}

// -----------------------------------------------------------------------------------------------
// One engine tick with exception handling.
struct TickTimer
{
	LARGE_INTEGER start;
	TickTimer() { ::QueryPerformanceCounter(&start); }
	~TickTimer()
	{
		if (TheGlobalData == nullptr || TheGlobalData->m_crcLogFile.isEmpty() || TheGameLogic == nullptr
			|| !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() || TheGameLogic->getFrame() <= 2)
			return;
		LARGE_INTEGER end, freq;
		::QueryPerformanceCounter(&end);
		::QueryPerformanceFrequency(&freq);
		timingVector(s_tickMs).push_back(static_cast<float>((end.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart)));
	}
};

// Keeps a multiplayer replay usable when a tick ends in an exception.
static void saveReplayAfterException()
{
	try
	{
		if (TheRecorder && TheRecorder->getMode() == RecorderModeType::RECORDERMODETYPE_RECORD && TheRecorder->isMultiplayer())
			TheRecorder->cleanUpReplayFile();
	}
	catch (...)
	{
	}
}

void GameEngine::runTick( void )
{
	PROF_ZONE("GameEngine::runTick");
	// Simulation heartbeat; the simulation currently runs on the main thread.
	::Engine::Watchdog::get().heartbeat( ::Engine::WATCHDOG_SIM );
	TickTimer tickTimer;
	try
	{
		// compute a frame
		update();
	}
	catch (INIException e)
	{
		// Release CRASH doesn't return, so don't worry about executing additional code.
		if (e.mFailureMessage)
			RELEASE_CRASH((e.mFailureMessage));
		else
			RELEASE_CRASH(("Uncaught Exception in GameEngine::update"));
	}
	catch (ErrorCode ec)
	{
		saveReplayAfterException();
		// The map readers reject damaged files with this code; the game cannot continue with a
		// half-loaded map, so name the cause instead of a generic exception.
		if (ec == ERROR_CORRUPT_FILE_FORMAT)
			RELEASE_CRASH(("A map or data file is damaged and cannot be loaded."));
		else
			RELEASE_CRASH(("Uncaught Exception in GameEngine::update"));
	}
	catch (...)
	{
		saveReplayAfterException();
		RELEASE_CRASH(("Uncaught Exception in GameEngine::update"));
	}	// catch
	PROF_FRAME_NAMED("Logic tick");	// separate frame bar for the 30 Hz ticks
}

// -----------------------------------------------------------------------------------------------
// Render-only frame between two logic ticks.
void GameEngine::renderInterpolatedFrame( Real alpha )
{
	PROF_ZONE("GameEngine::renderInterpolatedFrame");
	if (TheGameClient == nullptr || TheDisplay == nullptr || TheTacticalView == nullptr)
		return;
	// The client draws intro/videos and the load screen itself.
	if (TheDisplay->isMoviePlaying() || TheGlobalData->m_loadScreenRender)
		return;
	// Frozen time during a script camera move: W3DDisplay::draw() plays the move in its own loop and
	// needs the view update for it, which belongs to the tick.
	if (TheTacticalView->isTimeFrozen() && !TheTacticalView->isCameraMovementFinished())
		return;
#if defined(_DEBUG)
	if (TheGlobalData->m_noDraw > TheGameLogic->getFrame() && TheGameLogic->getFrame() > 0)
		return;
#endif
	if (alpha < 0.0f) alpha = 0.0f;
	if (alpha > 1.0f) alpha = 1.0f;
	TheGameClient->setRenderInterpolation(alpha, TRUE);
	TheDisplay->draw();
	TheGameClient->setRenderInterpolation(1.0f, FALSE);
	logRenderTime('R', alpha, (m_maxFPS > 0) ? 1000.0f / static_cast<Real>(m_maxFPS) : 1000.0f / 30.0f);	// only with -logRenderTime
}

// -----------------------------------------------------------------------------------------------
// May the tick draw its frame after the logic? Not for videos and the load screen (the client draws
// those itself) and not with frozen time during a script camera move: then W3DDisplay::draw() plays
// the move in its loop (DRAW_PHASE_FULL).
Bool GameEngine::canDeferTickDraw( void ) const
{
	if (TheGameClient == nullptr || TheDisplay == nullptr || TheTacticalView == nullptr)
		return FALSE;
	if (TheDisplay->isMoviePlaying() || TheGlobalData->m_loadScreenRender)
		return FALSE;
	if (TheTacticalView->isTimeFrozen() && !TheTacticalView->isCameraMovementFinished())
		return FALSE;
	return TRUE;
}

// -----------------------------------------------------------------------------------------------
// Frame of the tick just computed. Before the logic the tick only advanced (DRAW_PHASE_UPDATE: camera,
// particles, animation time). Now the new logic state is in place too, and the frame is interpolated
// like an in-between frame between the previous and this tick, with the alpha valid after the tick.
// Unlike an in-between frame it advances the display counters (isRenderOnlyFrame() stays FALSE), once
// per tick.
void GameEngine::renderTickFrame( Real alpha )
{
	PROF_ZONE("GameEngine::renderTickFrame");
	if (TheGameClient == nullptr || !TheGameClient->takeDeferredTickDraw())
		return;	// the tick did not get to drawing (intro, -noDraw)
	if (TheDisplay == nullptr || TheTacticalView == nullptr)
		return;
	if (TheDisplay->isMoviePlaying() || TheGlobalData->m_loadScreenRender)
		return;
	// Time lapse that started in this tick: from the next pass on the tick draws itself.
	if (TheTacticalView->getTimeMultiplier() > 1 || TheScriptEngine->isTimeFast())
		return;
	if (alpha < 0.0f) alpha = 0.0f;
	if (alpha > 1.0f) alpha = 1.0f;
	TheGameClient->setTickFrameInterpolation(alpha);
	TheDisplay->draw();
	TheGameClient->setRenderInterpolation(1.0f, FALSE);
	logRenderTime('T', alpha, (m_maxFPS > 0) ? 1000.0f / static_cast<Real>(m_maxFPS) : 1000.0f / 30.0f);
}
