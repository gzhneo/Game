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

// EngineStart.cpp -- engine start and shutdown for the map editor, in the order of the old
// WorldBuilder (CWorldBuilderApp::InitInstance), without dialogs.
//
// The working directory becomes the data folder of the game, as the game and its tools expect it
// (relative paths to Data\ and the archives; Zero Hour finds the Generals base archives in
// ZH_Generals\). Optional INI files that are missing are skipped, so a reduced data set starts too
// (tools/mapeditor/testdaten.py); the report says what was skipped. Differences to the WorldBuilder:
// a silent audio manager (EditorAudio.h) and no video player.

#include "Engine/EngineApi.h"
#include "Engine/EditorAudio.h"
#include "Engine/EngineInternal.h"
#include "Engine/UserDataFiles.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/FileSystem.h"
#include "Common/GameEdition.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/INI.h"
#include "Common/LocalFileSystem.h"
#include "Common/NameKeyGenerator.h"
#include "Common/SubsystemInterface.h"
#include "Common/DamageFX.h"
#include "Common/LogicAudio.h"
#include "Common/ModuleFactory.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/TerrainTypes.h"
#include "Common/ThingFactory.h"
#include "Common/Upgrade.h"
#include "GameClient/Anim2D.h"
#include "GameClient/FXList.h"
#include "GameClient/GameText.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/TerrainRoads.h"
#include "GameClient/Water.h"
#include "GameLogic/Armor.h"
#include "GameLogic/CaveSystem.h"
#include "GameLogic/CrateSystem.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/Weapon.h"
#include "W3DDevice/Common/W3DModuleFactory.h"
#include "W3DDevice/GameClient/W3DFileSystem.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "Win32Device/Common/Win32BIGFileSystem.h"
#include "Win32Device/Common/Win32LocalFileSystem.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

// Globals that the game's WinMain.cpp defines and the engine libraries use.
HWND ApplicationHWnd = nullptr;
HINSTANCE ApplicationHInstance = nullptr;
const char *gAppPrefix = "me_";
const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";

// The crash log of StackDump.cpp prints the address of WinMain; this DLL has no program entry.
int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	return 0;
}

namespace
{

GameEditionType s_edition = GameEditionType::GAME_EDITION_ZERO_HOUR;
bool s_started = false;
bool s_failedOnce = false;
std::string s_report;
FILE *s_log = nullptr;
std::wstring s_shaderFolder;
std::string s_shaderFolderAnsi;		// with a trailing backslash; the engine's file API is ANSI
// Last step or INI file of the start, for the error message: INI errors are thrown as plain codes.
std::string s_step;
SubsystemInterfaceList *s_subsystems = nullptr;

std::string narrow(const wchar_t *text)
{
	if (!text)
		return std::string();
	const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	std::string out(n > 0 ? static_cast<size_t>(n - 1) : 0u, '\0');
	if (n > 1)
		WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), n, nullptr, nullptr);
	return out;
}

// The shaders are not game data: paths below shaders\ go to the Shaders folder next to the editor,
// everything else to the data folder (the working directory). Both the renderer (through the W3D
// file factory) and the shader manager (directly) read them through this file system.
class EditorLocalFileSystem : public Win32LocalFileSystem
{
public:
	virtual File *openFile(const Char *filename, Int access = 0) override
	{
		std::string buffer;
		return Win32LocalFileSystem::openFile(redirect(filename, buffer), access);
	}
	virtual Bool doesFileExist(const Char *filename) const override
	{
		std::string buffer;
		return Win32LocalFileSystem::doesFileExist(redirect(filename, buffer));
	}
	virtual Bool getFileInfo(const AsciiString &filename, FileInfo *fileInfo) const override
	{
		std::string buffer;
		return Win32LocalFileSystem::getFileInfo(AsciiString(redirect(filename.str(), buffer)), fileInfo);
	}

private:
	static const Char *redirect(const Char *filename, std::string &buffer)
	{
		static const char PREFIX[] = "shaders\\";
		if (!filename || s_shaderFolderAnsi.empty() || _strnicmp(filename, PREFIX, sizeof(PREFIX) - 1) != 0)
			return filename;
		buffer = s_shaderFolderAnsi + (filename + sizeof(PREFIX) - 1);
		return buffer.c_str();
	}
};

template <class SUBSYSTEM>
void initSubsystem(SUBSYSTEM *&ref, SUBSYSTEM *sys, const char *path1 = nullptr, const char *path2 = nullptr, const char *dir = nullptr)
{
	// Only files that exist: a reduced data set has no Default\ folder and fewer INI files.
	auto exists = [](const char *p) { return p && TheFileSystem->doesFileExist(p); };
	for (const char *p : {path1, path2})
	{
		if (p && !exists(p))
			MapEditorEngine::report(std::string("skipped (missing): ") + p);
	}
	s_step = std::string(path1 ? path1 : path2 ? path2 : dir ? dir : "subsystem init");
	ref = sys;
	// The object folder lives inside the archives, where doesFileExist knows no folders; it is
	// always passed (the reduced data set has one too).
	s_subsystems->initSubsystem(sys, exists(path1) ? path1 : nullptr, exists(path2) ? path2 : nullptr, dir, nullptr);
}

class StepTimer
{
public:
	explicit StepTimer(const char *name) : m_name(name), m_start(std::chrono::steady_clock::now()) { s_step = name; }
	~StepTimer()
	{
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_start).count();
		char line[160];
		std::snprintf(line, sizeof(line), "%-28s %8.1f ms", m_name, ms);
		MapEditorEngine::report(line);
	}

private:
	const char *m_name;
	std::chrono::steady_clock::time_point m_start;
};

void startSubsystems(bool windowsOnly)
{
	{
		StepTimer t("file system");
		::Engine::context().nameKeyGenerator = new NameKeyGenerator;
		TheNameKeyGenerator->init();
		::Engine::context().fileSystem = new FileSystem;
		initSubsystem(::Engine::context().localFileSystem, static_cast<LocalFileSystem *>(new EditorLocalFileSystem));
		initSubsystem(::Engine::context().archiveFileSystem, static_cast<ArchiveFileSystem *>(new Win32BIGFileSystem));
	}
	{
		StepTimer t("GameData.ini");
		initSubsystem(::Engine::context().writableGlobalData, new GlobalData(), "Data\\INI\\Default\\GameData.ini", "Data\\INI\\GameData.ini");
		TheWritableGlobalData->m_isWorldBuilder = TRUE;
		TheWritableGlobalData->m_useHalfHeightMap = false;
		TheWritableGlobalData->m_enableBehindBuildingMarkers = false;
		// Terrain and shroud divide by the cell size; incomplete data must not end in a division by zero.
		if (TheWritableGlobalData->m_partitionCellSize <= 0.0f)
		{
			TheWritableGlobalData->m_partitionCellSize = 40.0f;
			MapEditorEngine::report("PartitionCellSize missing, using 40");
		}
	}
	{
		StepTimer t("Water.ini");
		INI ini;
		for (const char *p : {"Data\\INI\\Default\\Water.ini", "Data\\INI\\Water.ini"})
		{
			if (TheFileSystem->doesFileExist(p))
				ini.load(AsciiString(p), INILoadType::INI_LOAD_OVERWRITE, nullptr);
		}
		// Begin_Render reads the water opacity; a data set without Water.ini gets the defaults.
		if (TheWaterTransparency == nullptr)
			TheWaterTransparency = newInstance(WaterTransparencySetting);
	}
	if (!windowsOnly)
	{
		StepTimer t("Terrain.ini, Roads.ini");
		initSubsystem(::Engine::context().terrainTypes, new TerrainTypeCollection(), "Data\\INI\\Default\\Terrain.ini", "Data\\INI\\Terrain.ini");
		initSubsystem(::Engine::context().terrainRoads, new TerrainRoadCollection(), "Data\\INI\\Default\\Roads.ini", "Data\\INI\\Roads.ini");
	}
	{
		StepTimer t(windowsOnly ? "texts" : "texts, science, scripts");
		initSubsystem(::Engine::context().gameText, CreateGameTextInterface());
	}
	if (!windowsOnly)
	{
		StepTimer t("science, scripts");
		initSubsystem(::Engine::context().scienceStore, new ScienceStore(), "Data\\INI\\Default\\Science.ini", "Data\\INI\\Science.ini");
		initSubsystem(::Engine::context().multiplayerSettings, new MultiplayerSettings(), "Data\\INI\\Default\\Multiplayer.ini", "Data\\INI\\Multiplayer.ini");
		initSubsystem(::Engine::context().scriptEngine, static_cast<ScriptEngine *>(new ScriptEngine()));
		TheScriptEngine->turnBreezeOff();
		// Only the Zero Hour data has the script names file.
		if (TheFileSystem->doesFileExist("Data\\Scripts\\Scripts.ini"))
		{
			INI ini;
			ini.load(AsciiString("Data\\Scripts\\Scripts.ini"), INILoadType::INI_LOAD_OVERWRITE, nullptr);
		}
	}
	{
		StepTimer t("sound definitions");
		initSubsystem(::Engine::context().audio, static_cast<AudioManager *>(new MapEditorEngine::EditorAudioManager()));
		::Engine::context().logicAudio = new LogicAudio();
	}
	if (!windowsOnly)
	{
		StepTimer t("object catalog");
		initSubsystem(::Engine::context().moduleFactory, static_cast<ModuleFactory *>(new W3DModuleFactory()));
		initSubsystem(::Engine::context().sidesList, new SidesList());
		initSubsystem(::Engine::context().caveSystem, new CaveSystem());
		initSubsystem(::Engine::context().rankInfoStore, new RankInfoStore(), nullptr, "Data\\INI\\Rank.ini");
		initSubsystem(::Engine::context().playerTemplateStore, new PlayerTemplateStore(), "Data\\INI\\Default\\PlayerTemplate.ini", "Data\\INI\\PlayerTemplate.ini");
		initSubsystem(::Engine::context().specialPowerStore, new SpecialPowerStore(), "Data\\INI\\Default\\SpecialPower.ini", "Data\\INI\\SpecialPower.ini");
		initSubsystem(::Engine::context().particleSystemManager, static_cast<ParticleSystemManager *>(new W3DParticleSystemManager()));
		initSubsystem(::Engine::context().fxListStore, new FXListStore(), "Data\\INI\\Default\\FXList.ini", "Data\\INI\\FXList.ini");
		initSubsystem(::Engine::context().weaponStore, new WeaponStore(), nullptr, "Data\\INI\\Weapon.ini");
		initSubsystem(::Engine::context().objectCreationListStore, new ObjectCreationListStore(), "Data\\INI\\Default\\ObjectCreationList.ini", "Data\\INI\\ObjectCreationList.ini");
		initSubsystem(::Engine::context().locomotorStore, new LocomotorStore(), nullptr, "Data\\INI\\Locomotor.ini");
		initSubsystem(::Engine::context().damageFXStore, new DamageFXStore(), nullptr, "Data\\INI\\DamageFX.ini");
		initSubsystem(::Engine::context().armorStore, new ArmorStore(), nullptr, "Data\\INI\\Armor.ini");
		initSubsystem(::Engine::context().thingFactory, new ThingFactory(), "Data\\INI\\Default\\Object.ini", nullptr, "Data\\INI\\Object");
		initSubsystem(::Engine::context().crateSystem, new CrateSystem(), "Data\\INI\\Default\\Crate.ini", "Data\\INI\\Crate.ini");
		initSubsystem(::Engine::context().upgradeCenter, new UpgradeCenter, "Data\\INI\\Default\\Upgrade.ini", "Data\\INI\\Upgrade.ini");
		initSubsystem(::Engine::context().anim2DCollection, new Anim2DCollection);
	}
	{
		StepTimer t("post-processing");
		s_subsystems->postProcessLoadAll();
	}
	::Engine::context().w3DFileSystem = new W3DFileSystem;
}

// Lighting of GameData.ini, kept before a map changes the global data.
struct DefaultLighting
{
	TimeOfDay timeOfDay = TIME_OF_DAY_AFTERNOON;
	GlobalData::TerrainLighting terrain[TIME_OF_DAY_COUNT][MAX_GLOBAL_LIGHTS] = {};
	GlobalData::TerrainLighting objects[TIME_OF_DAY_COUNT][MAX_GLOBAL_LIGHTS] = {};
	Real waterHeight = 0.0f;
};
DefaultLighting s_defaultLighting;

void keepDefaultLighting()
{
	const GlobalData &gd = *TheGlobalData;
	s_defaultLighting.timeOfDay = gd.m_timeOfDay;
	s_defaultLighting.waterHeight = gd.m_waterPositionZ;
	for (Int t = 0; t < TIME_OF_DAY_COUNT; t++)
	{
		for (Int i = 0; i < MAX_GLOBAL_LIGHTS; i++)
		{
			s_defaultLighting.terrain[t][i] = gd.m_terrainLighting[t][i];
			s_defaultLighting.objects[t][i] = gd.m_terrainObjectsLighting[t][i];
		}
	}
}

void copyLight(const GlobalData::TerrainLighting &from, MeLight &to)
{
	to.ambient[0] = from.ambient.red;
	to.ambient[1] = from.ambient.green;
	to.ambient[2] = from.ambient.blue;
	to.diffuse[0] = from.diffuse.red;
	to.diffuse[1] = from.diffuse.green;
	to.diffuse[2] = from.diffuse.blue;
	to.position[0] = from.lightPos.x;
	to.position[1] = from.lightPos.y;
	to.position[2] = from.lightPos.z;
}

} // namespace

GameEditionType getGameEdition(void)
{
	return s_edition;
}

namespace MapEditorEngine
{

void report(const std::string &line)
{
	s_report += line;
	s_report += '\n';
	if (s_log)
	{
		std::fprintf(s_log, "%s\n", line.c_str());
		std::fflush(s_log);
	}
}

bool engineStarted()
{
	return s_started;
}

void restoreDefaultLighting()
{
	if (!s_started)
		return;
	GlobalData &gd = *TheWritableGlobalData;
	gd.m_timeOfDay = s_defaultLighting.timeOfDay;
	for (Int t = 0; t < TIME_OF_DAY_COUNT; t++)
	{
		for (Int i = 0; i < MAX_GLOBAL_LIGHTS; i++)
		{
			gd.m_terrainLighting[t][i] = s_defaultLighting.terrain[t][i];
			gd.m_terrainObjectsLighting[t][i] = s_defaultLighting.objects[t][i];
		}
	}
}

void setError(char *error, int32_t size, const std::string &text)
{
	report("error: " + text);
	if (error && size > 0)
	{
		const size_t n = text.size() < static_cast<size_t>(size - 1) ? text.size() : static_cast<size_t>(size - 1);
		std::memcpy(error, text.data(), n);
		error[n] = '\0';
	}
}

} // namespace MapEditorEngine

MEAPI int32_t meApiVersion(void)
{
	return ME_API_VERSION;
}

MEAPI int32_t meEngineStart(const MeEngineSettings *settings, char *error, int32_t errorSize)
{
	using MapEditorEngine::setError;
	if (s_started)
		return 1;
	if (s_failedOnce)
	{
		setError(error, errorSize, "the engine failed to start before; restart the editor");
		return 0;
	}
	if (!settings || !settings->dataFolder || !settings->dataFolder[0])
	{
		setError(error, errorSize, "no data folder");
		return 0;
	}
	if (settings->logFile && settings->logFile[0])
		s_log = _wfopen(settings->logFile, L"w");
	s_edition = settings->game == ME_GAME_GENERALS ? GameEditionType::GAME_EDITION_GENERALS : GameEditionType::GAME_EDITION_ZERO_HOUR;
	s_shaderFolder = settings->shaderFolder ? settings->shaderFolder : L"";
	if (!s_shaderFolder.empty())
	{
		char ansi[MAX_PATH * 2] = {};
		WideCharToMultiByte(CP_ACP, 0, s_shaderFolder.c_str(), -1, ansi, sizeof(ansi), nullptr, nullptr);
		s_shaderFolderAnsi = ansi;
		if (!s_shaderFolderAnsi.empty() && s_shaderFolderAnsi.back() != '\\' && s_shaderFolderAnsi.back() != '/')
			s_shaderFolderAnsi += '\\';
	}
	MapEditorEngine::report(std::string("game: ") + (settings->game == ME_GAME_GENERALS ? "Generals" : "Zero Hour"));
	MapEditorEngine::report("data: " + narrow(settings->dataFolder));
	MapEditorEngine::report("shaders: " + narrow(s_shaderFolder.c_str()));

	if (!SetCurrentDirectoryW(settings->dataFolder))
	{
		setError(error, errorSize, "data folder not found: " + narrow(settings->dataFolder));
		return 0;
	}

	const auto start = std::chrono::steady_clock::now();
	try
	{
		initMemoryManager();
		s_subsystems = new SubsystemInterfaceList;
		startSubsystems(settings->windowsOnly != 0);
		keepDefaultLighting();
	}
	catch (const std::exception &e)
	{
		s_failedOnce = true;
		setError(error, errorSize, std::string("engine start failed: ") + e.what());
		return 0;
	}
	catch (...)
	{
		s_failedOnce = true;
		setError(error, errorSize, "engine start failed while reading the game data, at: " + s_step);
		return 0;
	}
	char line[96];
	std::snprintf(line, sizeof(line), "engine started in %.0f ms",
		std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
	MapEditorEngine::report(line);
	s_started = true;
	return 1;
}

MEAPI int32_t meMapDefaults(MeMapDefaults *defaults)
{
	if (!s_started || !defaults)
		return 0;
	*defaults = MeMapDefaults{};
	defaults->timeOfDay = s_defaultLighting.timeOfDay;
	for (Int t = 0; t < 4; t++)
	{
		for (Int i = 0; i < MAX_GLOBAL_LIGHTS && i < 3; i++)
		{
			copyLight(s_defaultLighting.terrain[t + TIME_OF_DAY_FIRST][i], defaults->terrain[t][i]);
			copyLight(s_defaultLighting.objects[t + TIME_OF_DAY_FIRST][i], defaults->objects[t][i]);
		}
	}
	defaults->waterHeight = s_defaultLighting.waterHeight;
	return 1;
}

MEAPI void meEngineStop(void)
{
	MapEditorEngine::shutdownView();
	MapEditorEngine::clearCatalog();
	MapEditorEngine::clearTerrains();
	MapEditorEngine::clearMolds();
	MapEditorEngine::clearScriptValues();
	delete TheLogicAudio;
	::Engine::context().logicAudio = nullptr;
	if (s_subsystems)
	{
		s_subsystems->shutdownAll();
		delete s_subsystems;
		s_subsystems = nullptr;
	}
	delete TheFileSystem;
	::Engine::context().fileSystem = nullptr;
	delete TheW3DFileSystem;
	::Engine::context().w3DFileSystem = nullptr;
	delete TheNameKeyGenerator;
	::Engine::context().nameKeyGenerator = nullptr;
	::Engine::UserDataFiles::clearRoot();	// set by GlobalData; freed before the memory report
	s_started = false;
	if (s_log)
	{
		std::fclose(s_log);
		s_log = nullptr;
	}
}

MEAPI int32_t meEngineReport(char *text, int32_t size)
{
	if (text && size > 0)
	{
		const size_t n = s_report.size() < static_cast<size_t>(size - 1) ? s_report.size() : static_cast<size_t>(size - 1);
		std::memcpy(text, s_report.data(), n);
		text[n] = '\0';
	}
	return static_cast<int32_t>(s_report.size());
}
