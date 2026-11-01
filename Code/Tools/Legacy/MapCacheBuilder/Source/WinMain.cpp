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

// FILE: WinMain.cpp //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:    MapCacheBuilder
//
// File name:  WinMain.cpp
//
// Created:    Matthew D. Campbell, October 2002
//
// Desc:       Application entry point for the standalone MapCache Builder
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include <windows.h>
#include <stdlib.h>
#include <string.h>

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "Lib/BaseType.h"
#include "Common/Debug.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Resource.h"

#include "Common/ThingFactory.h"
#include "Common/FileSystem.h"
#include "Win32Device/Common/Win32LocalFileSystem.h"
#include "Win32Device/Common/Win32BIGFileSystem.h"
#include "Common/SubsystemInterface.h"
#include "GameClient/MapUtil.h"
#include "W3DDevice/Common/W3DModuleFactory.h"


#include "Common/FileSystem.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/LocalFileSystem.h"
#include "Common/Debug.h"
#include "Common/StackDump.h"
#include "Common/GameMemory.h"
#include "Common/Science.h"
#include "Common/ThingFactory.h"
#include "Common/INI.h"
#include "Common/GameAudio.h"
#include "Common/LogicAudio.h"
#include "Common/SpecialPower.h"
#include "Common/TerrainTypes.h"
#include "Common/DamageFX.h"
#include "Common/Upgrade.h"
#include "Common/ModuleFactory.h"
#include "Common/PlayerTemplate.h"
#include "Common/MultiplayerSettings.h"

#include "GameLogic/Armor.h"
#include "GameLogic/CaveSystem.h"
#include "GameLogic/CrateSystem.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/ScriptActions.h"
#include "GameClient/Anim2D.h"
#include "GameClient/GameText.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/Water.h"
#include "GameClient/TerrainRoads.h"
#include "GameClient/FXList.h"
#include "GameClient/VideoPlayer.h"
#include "GameLogic/Locomotor.h"

#include "W3DDevice/Common/W3DModuleFactory.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "XAudio2Device/XAudio2AudioManager.h"

#include <io.h>
#include "Win32Device/GameClient/Win32Mouse.h"
#include "Win32Device/Common/Win32LocalFileSystem.h"
#include "Win32Device/Common/Win32BIGFileSystem.h"
#include "Engine/UserDataFiles.h"


// DEFINES ////////////////////////////////////////////////////////////////////

// PRIVATE TYPES //////////////////////////////////////////////////////////////

// PRIVATE DATA ///////////////////////////////////////////////////////////////

static SubsystemInterfaceList _TheSubsystemList;

template<class SUBSYSTEM>
void initSubsystem(SUBSYSTEM*& sysref, SUBSYSTEM* sys, const char* path1 = nullptr, const char* path2 = nullptr, const char* dirpath = nullptr)
{
	sysref = sys;
	_TheSubsystemList.initSubsystem(sys, path1, path2, dirpath, nullptr);
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC DATA ////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
HINSTANCE ApplicationHInstance = nullptr;  ///< our application instance

/// just to satisfy the game libraries we link to
HWND ApplicationHWnd = nullptr;

const char *gAppPrefix = "MC_";

// Where are the default string files?
const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";

// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// trimWhitespace =============================================================
/** Trim leading and trailing whitespace from a character string (in place). */
//=============================================================================
static char* trimWhitespace(char* buffer)
{
	if (buffer != nullptr) {
		//	Strip leading white space from the string.
		char * source = buffer;
		while ((*source != 0) && (static_cast<unsigned char>(*source) <= 32))
		{
			source++;
		}

		if (source != buffer)
		{
			memmove(buffer, source, strlen(source) + 1);	// overlapping ranges
		}

		//	Clip trailing white space from the string.
		for (int index = static_cast<int>(strlen(buffer))-1; index >= 0; index--)
		{
			if (static_cast<unsigned char>(buffer[index]) <= 32)
			{
				buffer[index] = '\0';
			}
			else
			{
				break;
			}
		}
	}

	return buffer;
}

static char *nextParam(char *newSource, const char *seps)
{
	static char *source = nullptr;
	if (newSource)
	{
		source = newSource;
	}
	if (!source)
	{
		return nullptr;
	}

	// find first separator
	char *first = source;//strpbrk(source, seps);
	if (first)
	{
		// go past initial spaces
		char *firstNonSpace = first;
		while (*firstNonSpace == ' ')
			++firstNonSpace;
		first = firstNonSpace;

		// go past separator
		char *firstSep = strpbrk(first, seps);
		char firstChar[2] = {0,0};
		if (firstSep == first)
		{
			firstChar[0] = *first;
			while (*first == firstChar[0]) first++;
		}

		// find end
		char *end;
		if (firstChar[0])
			end = strpbrk(first, firstChar);
		else
			end = strpbrk(first, seps);

		// trim string & save next start pos
		if (end)
		{
			source = end+1;
			*end = 0;

			if (!*source)
				source = nullptr;
		}
		else
		{
			source = nullptr;
		}

		if (first && !*first)
			first = nullptr;
	}

	return first;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// WinMain ====================================================================
/** Application entry point */
//=============================================================================
Int APIENTRY WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance,
                      LPSTR lpCmdLine, Int nCmdShow )
{

	// start the log
	DEBUG_INIT(DEBUG_FLAGS_DEFAULT);
	initMemoryManager();

	try
	{

	// save application instance
	ApplicationHInstance = hInstance;


	// Set the current directory to the app directory.
	char buf[_MAX_PATH];
	GetModuleFileName(nullptr, buf, sizeof(buf));
	char *pEnd = buf + strlen(buf);
	while (pEnd != buf) {
		if (*pEnd == '\\') {
			*pEnd = 0;
			break;
		}
		pEnd--;
	}
	::SetCurrentDirectory(buf);

	/*
	** Convert WinMain arguments to simple main argc and argv
	*/
	std::list<std::string> argvSet;
	char *token;
	token = nextParam(lpCmdLine, "\" ");
	while (token != nullptr) {
		char * str = trimWhitespace(token);
		argvSet.push_back(str);
		DEBUG_LOG(("Adding '%s'\n", str));
		token = nextParam(nullptr, "\" ");	   
	}

	// not part of the subsystem list, because it should normally never be reset!
	::Engine::context().nameKeyGenerator = new NameKeyGenerator;
	TheNameKeyGenerator->init();

	::Engine::context().fileSystem = new FileSystem;

	initSubsystem(::Engine::context().localFileSystem, static_cast<LocalFileSystem*>(new Win32LocalFileSystem));
	initSubsystem(::Engine::context().archiveFileSystem, static_cast<ArchiveFileSystem*>(new Win32BIGFileSystem));
	INI ini;
	initSubsystem(::Engine::context().writableGlobalData, new GlobalData(), "Data\\INI\\Default\\GameData.ini", "Data\\INI\\GameData.ini");
	initSubsystem(::Engine::context().gameText, CreateGameTextInterface());
	initSubsystem(::Engine::context().scienceStore, new ScienceStore(), "Data\\INI\\Default\\Science.ini", "Data\\INI\\Science.ini");
	initSubsystem(::Engine::context().multiplayerSettings, new MultiplayerSettings(), "Data\\INI\\Default\\Multiplayer.ini", "Data\\INI\\Multiplayer.ini");
	initSubsystem(::Engine::context().terrainTypes, new TerrainTypeCollection(), "Data\\INI\\Default\\Terrain.ini", "Data\\INI\\Terrain.ini");
	initSubsystem(::Engine::context().terrainRoads, new TerrainRoadCollection(), "Data\\INI\\Default\\Roads.ini", "Data\\INI\\Roads.ini");
	initSubsystem(::Engine::context().scriptEngine, static_cast<ScriptEngine*>(new ScriptEngine()));
	initSubsystem(::Engine::context().audio, static_cast<AudioManager*>(new XAudio2AudioManager()));
	::Engine::context().logicAudio = new LogicAudio();
	initSubsystem(::Engine::context().videoPlayer, static_cast<VideoPlayerInterface*>(new VideoPlayer()));
	initSubsystem(::Engine::context().moduleFactory, static_cast<ModuleFactory*>(new W3DModuleFactory()));
	initSubsystem(::Engine::context().sidesList, new SidesList());
	initSubsystem(::Engine::context().caveSystem, new CaveSystem());
	initSubsystem(::Engine::context().rankInfoStore, new RankInfoStore(), nullptr, "Data\\INI\\Rank.ini");
	initSubsystem(::Engine::context().playerTemplateStore, new PlayerTemplateStore(), "Data\\INI\\Default\\PlayerTemplate.ini", "Data\\INI\\PlayerTemplate.ini");
	initSubsystem(::Engine::context().specialPowerStore, new SpecialPowerStore(), "Data\\INI\\Default\\SpecialPower.ini", "Data\\INI\\SpecialPower.ini" );
	initSubsystem(::Engine::context().particleSystemManager, static_cast<ParticleSystemManager*>(new W3DParticleSystemManager()));
	initSubsystem(::Engine::context().fxListStore, new FXListStore(), "Data\\INI\\Default\\FXList.ini", "Data\\INI\\FXList.ini");
	initSubsystem(::Engine::context().weaponStore, new WeaponStore(), nullptr, "Data\\INI\\Weapon.ini");
	initSubsystem(::Engine::context().objectCreationListStore, new ObjectCreationListStore(), "Data\\INI\\Default\\ObjectCreationList.ini", "Data\\INI\\ObjectCreationList.ini");
	initSubsystem(::Engine::context().locomotorStore, new LocomotorStore(), nullptr, "Data\\INI\\Locomotor.ini");
	initSubsystem(::Engine::context().damageFXStore, new DamageFXStore(), nullptr, "Data\\INI\\DamageFX.ini");
	initSubsystem(::Engine::context().armorStore, new ArmorStore(), nullptr, "Data\\INI\\Armor.ini");
	initSubsystem(::Engine::context().thingFactory, new ThingFactory(), "Data\\INI\\Default\\Object.ini", nullptr, "Data\\INI\\Object");
	initSubsystem(::Engine::context().crateSystem, new CrateSystem(), "Data\\INI\\Default\\Crate.ini", "Data\\INI\\Crate.ini");
	initSubsystem(::Engine::context().upgradeCenter, new UpgradeCenter, "Data\\INI\\Default\\Upgrade.ini", "Data\\INI\\Upgrade.ini");
	initSubsystem(::Engine::context().anim2DCollection, new Anim2DCollection ); //Init's itself.

	_TheSubsystemList.postProcessLoadAll();

	TheWritableGlobalData->m_buildMapCache = TRUE;

	::Engine::context().mapCache = new MapCache;

	// add in allowed maps
	for (std::list<std::string>::const_iterator cit = argvSet.begin(); cit != argvSet.end(); ++cit)
	{
		DEBUG_LOG(("Adding shipping map: '%s'\n", cit->c_str()));
		TheMapCache->addShippingMap((*cit).c_str());
	}

	TheMapCache->updateCache();

	delete TheMapCache;
	::Engine::context().mapCache = nullptr;

	// load the dialog box
	//DialogBox( hInstance, (LPCTSTR)IMAGE_PACKER_DIALOG, 
	//					 NULL, (DLGPROC)ImagePackerProc );

	// delete TheGlobalData
	//delete TheGlobalData;
	//TheGlobalData = NULL;

	delete TheLogicAudio;
	::Engine::context().logicAudio = nullptr;
	_TheSubsystemList.shutdownAll();

	delete TheFileSystem;
	::Engine::context().fileSystem = nullptr;

	delete TheNameKeyGenerator;
	::Engine::context().nameKeyGenerator = nullptr;
	::Engine::UserDataFiles::clearRoot();	// set by GlobalData; freed before the memory report

	}
	catch (...)
	{
		DEBUG_CRASH(("Munkee munkee!"));
	}

	// close the log
	shutdownMemoryManager();
	DEBUG_SHUTDOWN();

	// all done
	return 0;

}  // end WinMain
