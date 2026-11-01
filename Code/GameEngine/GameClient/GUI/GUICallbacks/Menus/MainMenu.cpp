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

// FILE: MainMenu.cpp /////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, October 2001
// Description: Main menu window callbacks
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Lib/BaseType.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/RandomValue.h"
#include "Common/UserSettings.h"
#include "Common/version.h"
#include "Common/GameLOD.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/ExtendedMessageBox.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Display.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameText.h"
#include "GameClient/GadgetPushButton.h"
#include "GameNetwork/GameLobby.h"
#include "GameClient/HeaderTemplate.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Shell.h"
#include "GameClient/ShellHooks.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/Mouse.h"
#include "GameClient/WindowVideoManager.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/HotKey.h"
#include "GameClient/GameClient.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/ChallengeGenerals.h"


//Added By Saad
//for accessing the InGameUI
#include "GameClient/InGameUI.h"



#include <string>
// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////

enum
{
	DROPDOWN_NONE = 0,
	DROPDOWN_SINGLE,
	DROPDOWN_MULTIPLAYER,
	DROPDOWN_MAIN,
	DROPDOWN_LOADREPLAY,
	DROPDOWN_DIFFICULTY,
	
	DROPDOWN_COUNT // keep last
};

static Bool campaignSelected = FALSE;
#if defined _DEBUG
static NameKeyType campaignID = NAMEKEY_INVALID;
static GameWindow *buttonCampaign = nullptr;
#endif


// window ids -------------------------------------------------------------------------------------
static NameKeyType mainMenuID = NAMEKEY_INVALID;
static NameKeyType skirmishID = NAMEKEY_INVALID;
static NameKeyType onlineID = NAMEKEY_INVALID;
static NameKeyType networkID = NAMEKEY_INVALID;
static NameKeyType optionsID = NAMEKEY_INVALID;
static NameKeyType exitID = NAMEKEY_INVALID;
static NameKeyType motdID = NAMEKEY_INVALID;
static NameKeyType worldBuilderID = NAMEKEY_INVALID;
static NameKeyType getUpdateID = NAMEKEY_INVALID;
static NameKeyType buttonTRAININGID = NAMEKEY_INVALID;
static NameKeyType buttonChallengeID = NAMEKEY_INVALID;
static NameKeyType buttonUSAID = NAMEKEY_INVALID;
static NameKeyType buttonGLAID = NAMEKEY_INVALID;
static NameKeyType buttonChinaID = NAMEKEY_INVALID;
static NameKeyType buttonUSARecentSaveID = NAMEKEY_INVALID;
static NameKeyType buttonUSALoadGameID = NAMEKEY_INVALID;
static NameKeyType buttonGLARecentSaveID = NAMEKEY_INVALID;
static NameKeyType buttonGLALoadGameID = NAMEKEY_INVALID;
static NameKeyType buttonChinaRecentSaveID = NAMEKEY_INVALID;
static NameKeyType buttonChinaLoadGameID = NAMEKEY_INVALID;
static NameKeyType buttonSinglePlayerID = NAMEKEY_INVALID;
static NameKeyType buttonMultiPlayerID = NAMEKEY_INVALID;
static NameKeyType buttonMultiBackID = NAMEKEY_INVALID;
static NameKeyType buttonSingleBackID = NAMEKEY_INVALID;
static NameKeyType buttonLoadReplayBackID = NAMEKEY_INVALID;
static NameKeyType buttonReplayID = NAMEKEY_INVALID;
static NameKeyType buttonLoadReplayID = NAMEKEY_INVALID;
static NameKeyType buttonLoadID = NAMEKEY_INVALID;
static NameKeyType buttonCreditsID = NAMEKEY_INVALID;
static NameKeyType buttonEasyID = NAMEKEY_INVALID;
static NameKeyType buttonMediumID = NAMEKEY_INVALID;
static NameKeyType buttonHardID = NAMEKEY_INVALID;
static NameKeyType buttonDiffBackID = NAMEKEY_INVALID;


// window pointers --------------------------------------------------------------------------------
static GameWindow *parentMainMenu = nullptr;
static GameWindow *buttonSinglePlayer = nullptr;
static GameWindow *buttonMultiPlayer = nullptr;
static GameWindow *buttonSkirmish = nullptr;
static GameWindow *buttonOnline = nullptr;
static GameWindow *buttonNetwork = nullptr;
static GameWindow *buttonOptions = nullptr;
static GameWindow *buttonExit = nullptr;
static GameWindow *buttonMOTD = nullptr;
static GameWindow *buttonWorldBuilder = nullptr;
static GameWindow *mainMenuMovie = nullptr;
static GameWindow *getUpdate = nullptr;
static GameWindow *buttonTRAINING = nullptr;
static GameWindow *buttonChallenge = nullptr;
static GameWindow *buttonUSA = nullptr;
static GameWindow *buttonGLA = nullptr;
static GameWindow *buttonChina = nullptr;
static GameWindow *buttonUSARecentSave = nullptr;
static GameWindow *buttonUSALoadGame = nullptr;
static GameWindow *buttonGLARecentSave = nullptr;
static GameWindow *buttonGLALoadGame = nullptr;
static GameWindow *buttonChinaRecentSave = nullptr;
static GameWindow *buttonChinaLoadGame = nullptr;
static GameWindow *buttonReplay = nullptr;
static GameWindow *buttonLoadReplay = nullptr;
static GameWindow *buttonLoad = nullptr;
static GameWindow *buttonCredits = nullptr;
static GameWindow *buttonEasy = nullptr;
static GameWindow *buttonMedium = nullptr;
static GameWindow *buttonHard = nullptr;
static GameWindow *buttonDiffBack = nullptr;
static GameWindow *dropDownWindows[DROPDOWN_COUNT];
// The drop-downs hidden while the options menu is open over the main menu.
static GameWindow *hiddenForOptions[DROPDOWN_COUNT];

static Bool buttonPushed = FALSE;
static Bool isShuttingDown = FALSE;
static Bool startGame = FALSE;
static Int	initialGadgetDelay = 210;

enum
{
	SHOW_NONE = 0,
	SHOW_TRAINING,
	SHOW_USA,
	SHOW_GLA,
	SHOW_CHINA,
	SHOW_SKIRMISH,
	SHOW_FRAMES_LIMIT = 20
};

static Bool showFade = FALSE;
static Int dropDown = DROPDOWN_NONE;
static Int pendingDropDown = DROPDOWN_NONE;
static AnimateWindowManager *localAnimateWindowManager = nullptr;
static Bool notShown = TRUE;
static Bool FirstTimeRunningTheGame = TRUE;

static Bool showLogo = FALSE;
static Int  showFrames = 0;
static Int  showSide = SHOW_NONE;
static Bool logoIsShown = FALSE;
static Bool justEntered = FALSE;
static Bool launchChallengeMenu = FALSE;

static Bool dontAllowTransitions = FALSE;

//Added by Saad
const /*Int TIME_OUT = 15,*/ Int CORNER = 10;
void AcceptResolution();
void DeclineResolution();
GameWindow *resAcceptMenu = nullptr;
extern DisplaySettings oldDispSettings, newDispSettings;
extern Bool dispChanged;
//static time_t timeStarted = 0, currentTime = 0;
//

void diffReverseSide( void );

//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------

static void showSelectiveButtons( Int show )
{
	buttonUSARecentSave->winHide(!(show == SHOW_USA ));
	buttonUSALoadGame->winHide(!(show == SHOW_USA ));
	buttonGLARecentSave->winHide(!(show == SHOW_GLA ));
	buttonGLALoadGame->winHide(!(show == SHOW_GLA ));
	buttonChinaRecentSave->winHide(!(show == SHOW_CHINA ));
	buttonChinaLoadGame->winHide(!(show == SHOW_CHINA ));
}
 
static void quitCallback( void )
{
	buttonPushed = TRUE;
	TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_EXIT_SELECTED]);
	TheShell->pop();
	TheGameEngine->setQuitting( TRUE );

  

	if (TheGameLogic->isInGame())
		TheMessageStream->appendMessage( GameMessage::MSG_CLEAR_GAME_DATA );
}


void setupGameStart(AsciiString mapName, GameDifficulty diff)
{
	TheCampaignManager->setGameDifficulty(diff);

	if (launchChallengeMenu)
	{
		if (TheChallengeGenerals)
			TheChallengeGenerals->setCurrentDifficulty(diff);
		
		campaignSelected = TRUE;
		TheShell->push( AsciiString("Menus/ChallengeMenu.wnd") );
		TheTransitionHandler->reverse("MainMenuDifficultyMenuTraining");
	}
	else
	{
		startGame = TRUE;
		TheWritableGlobalData->m_pendingFile = mapName;
		TheShell->reverseAnimatewindow();
		TheTransitionHandler->setGroup("FadeWholeScreen");
	}
}

void prepareCampaignGame(GameDifficulty diff)
{
	dontAllowTransitions = TRUE;
	UserSettings::setCampaignDifficulty(diff);
	UserSettings::save();
	TheScriptEngine->setGlobalDifficulty(diff);

	buttonPushed = FALSE;
	TheTransitionHandler->reverse("MainMenuDifficultyMenuBack");
	setupGameStart(TheCampaignManager->getCurrentMap(), diff );
}

static void doGameStart( void )
{
	startGame = FALSE;

	if (TheGameLogic->isInGame())
		TheGameLogic->clearGameData();

	// send a message to the logic for a new game
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
	msg->appendIntegerArgument(TheCampaignManager->getGameDifficulty());
	msg->appendIntegerArgument(TheCampaignManager->getRankPoints());
	InitRandom(0);

	isShuttingDown = TRUE;
}

static void shutdownComplete( WindowLayout *layout )
{
	isShuttingDown = FALSE;
	
	// hide the layout
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout );

}  // end if



/*
static void TimetToFileTime( time_t t, LPFILETIME pft )
{
	LONGLONG ll = Int32x32To64(t, 10000000) + 116444736000000000;
	pft->dwLowDateTime = (DWORD) ll;
	pft->dwHighDateTime = ll >>32;
}
*/

void initialHide( void )
{
GameWindow *win = nullptr;
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionGLA"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionChina"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionUS"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinGrowMarker"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionTraining"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionTrainingSmall"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionTrainingMedium"));
	if(win)
		win->winHide(TRUE);

	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionSkirmish"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionSkirmishSmall"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionSkirmishMedium"));
	if(win)
		win->winHide(TRUE);

	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionUS"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionUSSmall"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionUSMedium"));
	if(win)
		win->winHide(TRUE);

	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionGLA"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionGLASmall"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionGLAMedium"));
	if(win)
		win->winHide(TRUE);

	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionChina"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionChinaSmall"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionChinaMedium"));
	if(win)
		win->winHide(TRUE);

}
//-------------------------------------------------------------------------------------------------
/** Initialize the main menu */
//-------------------------------------------------------------------------------------------------
void MainMenuInit( WindowLayout *layout, void *userData )
{
	TheWritableGlobalData->m_breakTheMovie = FALSE;

	TheShell->showShellMap(TRUE);
	TheMouse->setVisibility(TRUE);
	//winVidManager = NEW WindowVideoManager;
	buttonPushed = FALSE;
	isShuttingDown = FALSE;
	startGame = FALSE;
	dropDown = DROPDOWN_NONE;
	pendingDropDown = DROPDOWN_NONE;
	Int i;
	for(i = 0; i < DROPDOWN_COUNT; ++i)
	{
		dropDownWindows[i] = nullptr;
		hiddenForOptions[i] = nullptr;
	}

	// get ids for our windows
	mainMenuID = TheNameKeyGenerator->nameToKey( AsciiString( "MainMenu.wnd:MainMenuParent" ) );
//	campaignID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonCampaign") );
	skirmishID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonSkirmish") );
	onlineID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonOnline") );
	networkID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonNetwork") );
	optionsID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonOptions") );
	exitID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonExit") );
	motdID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonMOTD") );
	worldBuilderID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonWorldBuilder") );
//	NameKeyType versionID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:LabelVersion") );
	getUpdateID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonGetUpdate") );
	// The Generals MainMenu.wnd has the training button instead of Challenge; the branches below only
	// apply when it exists (never with the Zero Hour data).
	buttonTRAININGID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonTRAINING") );
	buttonChallengeID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonChallenge") );
	buttonUSAID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonUSA") );
	buttonGLAID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonGLA") );
	buttonChinaID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonChina") );
	buttonUSARecentSaveID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonUSARecentSave") );
	buttonUSALoadGameID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonUSALoadGame") );
	buttonGLARecentSaveID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonGLARecentSave") );
	buttonGLALoadGameID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonGLALoadGame") );
	buttonChinaRecentSaveID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonChinaRecentSave") );
	buttonChinaLoadGameID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonChinaLoadGame") );
	buttonSinglePlayerID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonSinglePlayer") );
	buttonMultiPlayerID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonMultiplayer") );
	buttonMultiBackID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonMultiBack") );
	buttonSingleBackID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonSingleBack") );
	buttonLoadReplayBackID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonLoadReplayBack") );
	buttonReplayID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonReplay") );
	buttonLoadReplayID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonLoadReplay") );
	buttonLoadID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonLoadGame") );
	buttonCreditsID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonCredits") );

	buttonEasyID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonEasy") );
	buttonMediumID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonMedium") );
	buttonHardID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonHard") );
	buttonDiffBackID = TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:ButtonDiffBack") );

	// get pointers to the window buttons
	parentMainMenu = TheWindowManager->winGetWindowFromId( nullptr, mainMenuID );
	//buttonCampaign = TheWindowManager->winGetWindowFromId( parentMainMenu, campaignID );
	buttonSinglePlayer = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonSinglePlayerID );
	buttonMultiPlayer = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonMultiPlayerID );
	buttonSkirmish = TheWindowManager->winGetWindowFromId( parentMainMenu, skirmishID );
	buttonOnline = TheWindowManager->winGetWindowFromId( parentMainMenu, onlineID );
	buttonNetwork = TheWindowManager->winGetWindowFromId( parentMainMenu, networkID );
	// "Online" was the button of the online service; it opens the Internet lobby (games of the
	// rendezvous servers) now.
	if (buttonOnline)
	{
		GadgetButtonSetText( buttonOnline, TheGameText->fetch( "GZH:MainMenuInternet" ) );
		buttonOnline->winEnable( TRUE );
	}
	buttonOptions = TheWindowManager->winGetWindowFromId( parentMainMenu, optionsID );
	buttonExit = TheWindowManager->winGetWindowFromId( parentMainMenu, exitID );
	buttonMOTD = TheWindowManager->winGetWindowFromId( parentMainMenu, motdID );
	buttonWorldBuilder = TheWindowManager->winGetWindowFromId( parentMainMenu, worldBuilderID );
	buttonReplay = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonReplayID );
	buttonLoadReplay = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonLoadReplayID );
	buttonLoad = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonLoadID );
	buttonCredits = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonCreditsID );

	buttonEasy = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonEasyID );
	buttonMedium = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonMediumID );
	buttonHard = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonHardID );
	buttonDiffBack = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonDiffBackID );


//	GameWindow *labelVersion = TheWindowManager->winGetWindowFromId( parentMainMenu, versionID );
	// The version label of the layout names the project and its version.
	if( GameWindow *labelVersion = TheWindowManager->winGetWindowFromId( parentMainMenu,
			TheNameKeyGenerator->nameToKey( AsciiString( "MainMenu.wnd:LabelVersion" ) ) ) )
	{
		UnicodeString version;
		version.format( L"GZH Neo %ls", TheVersion ? TheVersion->getUnicodeVersion().str() : L"" );
		GadgetStaticTextSetText( labelVersion, version );
	}
	
	getUpdate = TheWindowManager->winGetWindowFromId( parentMainMenu, getUpdateID );
	buttonTRAINING = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonTRAININGID );	// Generals data only
	buttonChallenge = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonChallengeID );
	buttonUSA = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonUSAID );
	buttonGLA = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonGLAID );
	buttonChina = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonChinaID );
	buttonUSARecentSave = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonUSARecentSaveID );
	buttonUSALoadGame = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonUSALoadGameID );
	buttonGLARecentSave = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonGLARecentSaveID );
	buttonGLALoadGame = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonGLALoadGameID );
	buttonChinaRecentSave = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonChinaRecentSaveID );
	buttonChinaLoadGame = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonChinaLoadGameID );

	dropDownWindows[DROPDOWN_SINGLE] = TheWindowManager->winGetWindowFromId( parentMainMenu, TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:MapBorder") ));
	dropDownWindows[DROPDOWN_MULTIPLAYER] = TheWindowManager->winGetWindowFromId( parentMainMenu, TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:MapBorder1") ) );
	dropDownWindows[DROPDOWN_MAIN] = TheWindowManager->winGetWindowFromId( parentMainMenu, TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:MapBorder2") ) );
	dropDownWindows[DROPDOWN_LOADREPLAY] = TheWindowManager->winGetWindowFromId( parentMainMenu, TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:MapBorder3") ) );
	dropDownWindows[DROPDOWN_DIFFICULTY] = TheWindowManager->winGetWindowFromId( parentMainMenu, TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:MapBorder4") ) );
	for(i = 1; i < DROPDOWN_COUNT; ++i)
		dropDownWindows[i]->winHide(TRUE);

	initialHide();
	
	showSelectiveButtons(SHOW_NONE);
	// Set up the version number
#if defined _DEBUG
	WinInstanceData instData;

	instData.init();
	BitSet( instData.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
	instData.m_textLabelString = "Debug: Load Map";
	
	instData.setTooltipText(UnicodeString(L"Only Used in Debug and Internal!"));
	// MainMenuInit runs on every return to the main menu (Shell::doPop) while the layout stays. The
	// button has its own ID and is only created if it does not exist yet.
	const NameKeyType debugLoadMapID = TheNameKeyGenerator->nameToKey( AsciiString( "MainMenu.wnd:DebugButtonLoadMap" ) );
	buttonCampaign = TheWindowManager->winGetWindowFromId( parentMainMenu, debugLoadMapID );
	if( buttonCampaign == nullptr )
	{
		buttonCampaign = TheWindowManager->gogoGadgetPushButton( parentMainMenu, 
																										 WIN_STATUS_ENABLED, 
																										 25, 54, 
																										 180, 26, 
																										 &instData, nullptr, TRUE );
		buttonCampaign->winSetWindowId( debugLoadMapID );
	}
	
//	if (TheVersion)
//	{
//		UnicodeString version;
//		version.format(L"%s\n%s", TheVersion->getUnicodeVersion().str(), TheVersion->getUnicodeBuildTime().str());
//		GadgetStaticTextSetText( labelVersion, version );
//	}
//	else
//	{
//		labelVersion->winHide( TRUE );
//	}
//#else
	
//	GadgetStaticTextSetText( labelVersion, TheVersion->getUnicodeVersion() );
#endif

	//TheShell->registerWithAnimateManager(buttonCampaign, WIN_ANIMATION_SLIDE_LEFT, TRUE, 800);
	//TheShell->registerWithAnimateManager(buttonSkirmish, WIN_ANIMATION_SLIDE_LEFT, TRUE, 600);
//	TheShell->registerWithAnimateManager(buttonSinglePlayer, WIN_ANIMATION_SLIDE_LEFT, TRUE, 400);
//	TheShell->registerWithAnimateManager(buttonMultiPlayer, WIN_ANIMATION_SLIDE_LEFT, TRUE, 200);
//	TheShell->registerWithAnimateManager(buttonOptions, WIN_ANIMATION_SLIDE_LEFT, TRUE, 1);
//	TheShell->registerWithAnimateManager(buttonExit, WIN_ANIMATION_SLIDE_RIGHT, TRUE, 1);
//	
	layout->hide( FALSE );

	/*
	if (!checkedForUpdate)
	{
		DWORD state = 0;
		Bool isConnected = InternetGetConnectedState(&state, 0);
		if (isConnected && !(state & INTERNET_CONNECTION_MODEM_BUSY))
		{
			// wohoo - we're connected!  fire off a check for updates
			checkedForUpdate = TRUE;
			DEBUG_LOG(("Looking for a patch for productID=%d, versionStr=%s, distribution=%d\n",
				gameProductID, gameVersionUniqueIDStr, gameDistributionID));
			ptCheckForPatch( gameProductID, gameVersionUniqueIDStr, gameDistributionID, patchAvailableCallback, PTFalse, NULL );
			//ptCheckForPatch( productID, versionUniqueIDStr, distributionID, mapPackAvailableCallback, PTFalse, NULL );
		}
	}
	if (getUpdate != NULL)
	{
		getUpdate->winHide( TRUE );
		//getUpdate->winEnable( FALSE );
	}
	/**/

	if (TheMapCache)
		TheMapCache->updateCache();

	/*
	if (MOTDBuffer && buttonMOTD)
	{
		buttonMOTD->winHide(FALSE);
	}
	*/
	
	TheShell->loadScheme("MainMenu");

//	if(!localAnimateWindowManager)
//		localAnimateWindowManager = NEW AnimateWindowManager;

	//pendingDropDown =DROPDOWN_MAIN;
	

	GameWindow *rule = TheWindowManager->winGetWindowFromId( parentMainMenu, TheNameKeyGenerator->nameToKey( AsciiString("MainMenu.wnd:MainMenuRuler") ) );
	if(rule)
		rule->winHide(TRUE);
	campaignSelected = FALSE;
//	dropDownWindows[DROPDOWN_MAIN]->winHide(FALSE);
	if(FirstTimeRunningTheGame)
	{
		TheMouse->setVisibility(FALSE);

		TheTransitionHandler->reverse("FadeWholeScreen");
		FirstTimeRunningTheGame  = FALSE;
	}
	else
	{
		showFade = TRUE;
		justEntered = TRUE;
		initialGadgetDelay = 2;
		if(rule)
		rule->winHide(FALSE);
	}

	layout->bringForward();	
	// set keyboard focus to main parent
	TheWindowManager->winSetFocus( parentMainMenu );
	
	
}  // end MainMenuInit

//-------------------------------------------------------------------------------------------------
/** Main menu shutdown method */
//-------------------------------------------------------------------------------------------------
// The options menu opens over the main menu; the open drop-down with its buttons would show through
// beside the pages. It goes while the options are open and comes back as it was.
static void hideForOptions( void )
{
	for( Int i = 0; i < DROPDOWN_COUNT; ++i )
	{
		hiddenForOptions[i] = nullptr;
		if( dropDownWindows[i] && !dropDownWindows[i]->winIsHidden() )
		{
			dropDownWindows[i]->winHide( TRUE );
			hiddenForOptions[i] = dropDownWindows[i];
		}
	}
}

void MainMenuShowAfterOptions( void )
{
	for( Int i = 0; i < DROPDOWN_COUNT; ++i )
	{
		if( hiddenForOptions[i] )
			hiddenForOptions[i]->winHide( FALSE );
		hiddenForOptions[i] = nullptr;
	}
}

void MainMenuShutdown( WindowLayout *layout, void *userData )
{
	for( Int i = 0; i < DROPDOWN_COUNT; ++i )
		hiddenForOptions[i] = nullptr;
	if (!startGame)
		isShuttingDown = TRUE;

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *static_cast<Bool *>(userData);
	
//	if(winVidManager)
	//		delete winVidManager;
	//	winVidManager = NULL;
	

	if( popImmediate )
	{
//		if(localAnimateWindowManager)
//		{
//			delete localAnimateWindowManager;
//			localAnimateWindowManager = NULL;
//		}
		shutdownComplete( layout );
		return;

	}  //end if

	if (!startGame)
		TheShell->reverseAnimatewindow();
	//TheShell->reverseAnimatewindow();
//	if(localAnimateWindowManager && dropDown != DROPDOWN_NONE)
//		localAnimateWindowManager->reverseAnimateWindow();
}  // end MainMenuShutdown

// Also used by ScoreScreen.cpp and PopupSaveLoad.cpp.
Bool DontShowMainMenu = FALSE;

////////////////////////////////////////////////////////////////////////////
//Added By Sadullah Nader
//Added as a fix to the resolution change
//Allows the user to confirm the change, goes back to the previous mode 
//if the time to change expires.
////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
// Accept Resolution callback method
//-------------------------------------------------------------------------------------------------
void AcceptResolution()
{
	//Keep new settings and bail with setting the display changed flag
	//set to off
	oldDispSettings = newDispSettings;
	dispChanged = FALSE;
}

//-------------------------------------------------------------------------------------------------
// Decline Resolution callback method
//-------------------------------------------------------------------------------------------------
void DeclineResolution()
{
	//Revert back to old resolution and reset all necessary 
	//parts of the shell

	if (TheDisplay->setDisplayMode(oldDispSettings.xRes, oldDispSettings.yRes, 
										oldDispSettings.bitDepth, oldDispSettings.windowed))
	{
		dispChanged = FALSE;
		newDispSettings = oldDispSettings;

		TheWritableGlobalData->m_xResolution = newDispSettings.xRes;
		TheWritableGlobalData->m_yResolution = newDispSettings.yRes;
		
		TheHeaderTemplateManager->headerNotifyResolutionChange();
		TheMouse->mouseNotifyResolutionChange();
				
		AsciiString prefString;
		prefString.format("%d %d", newDispSettings.xRes, newDispSettings.yRes);
		
		UserSettings::set("Display.Resolution", prefString);
		UserSettings::save();

		// delete the shell
		delete TheShell;
		::Engine::context().shell = nullptr;

		// create the shell
		::Engine::context().shell = MSGNEW("GameClientSubsystem") Shell;
		if( TheShell )
			TheShell->init();
		
		TheInGameUI->recreateControlBar();

		TheShell->push( AsciiString("Menus/MainMenu.wnd") );
	}
}

//-------------------------------------------------------------------------------------------------
// Accept/Decline Resolution dialog method
//-------------------------------------------------------------------------------------------------
void DoResolutionDialog()
{
	//Bring up a dialog to accept the resolution chosen in the options menu
	UnicodeString resolutionNew;
	
	UnicodeString resTimerString = TheGameText->fetch("GUI:Resolution");
	
	resolutionNew.format(L": %dx%d\n", newDispSettings.xRes , newDispSettings.yRes);
	
	resTimerString.concat(resolutionNew);
		
	
	resAcceptMenu = TheWindowManager->gogoMessageBox( CORNER, CORNER, -1, -1,MSG_BOX_OK | MSG_BOX_CANCEL , 
																									 TheGameText->fetch("GUI:Resolution"), 
																									 resTimerString, nullptr, nullptr, AcceptResolution, 
																									 DeclineResolution);
}

/* This function is not being currently used because we do not need a timer on the 
// dialog box.
//-------------------------------------------------------------------------------------------------
//ResolutionDialogUpdate() - if resolution dialog box is shown, this must count 10 seconds for 
//	accepting resolution changes otherwise we go back to previous display settings 
//-------------------------------------------------------------------------------------------------
void ResolutionDialogUpdate()
{
	if (timeStarted == 0 && currentTime == 0)
	{
		timeStarted = currentTime = time(NULL);
	}
	else 
	{
		currentTime = time(NULL);
	}
	
	if ( ( currentTime - timeStarted ) >= TIME_OUT)
	{
		currentTime = timeStarted = 0;
		DeclineResolution();
	}
	//------------------------------------------------------------------------------------------------------
	// Used for debugging purposes
	//------------------------------------------------------------------------------------------------------
	DEBUG_LOG(("Resolution Timer :  started at %d,  current time at %d, frameTicker is %d\n", timeStarted, 
							time(NULL) , currentTime));
}
*/

// Notices of the start, once the main menu is up, once per run, one after the other: the hardware hint
// (below the recommended CPU), then the mods that did not load, or not completely. Unattended runs
// (-runner, -quitAfter) get none.
static Bool s_modProblemsPending = FALSE;

// The texts carry numbers as %d; the format check needs a fixed format, so they are put in by hand,
// in order.
static UnicodeString withNumbers( const UnicodeString &text, Int first, Int second = 0 )
{
	std::wstring result;
	const WideChar *p = text.str();
	Int used = 0;
	while( *p )
	{
		if( p[0] == L'%' && p[1] == L'd' )
		{
			UnicodeString number;
			number.format( L"%d", used++ == 0 ? first : second );
			result += number.str();
			p += 2;
		}
		else
			result += *p++;
	}
	return UnicodeString( result.c_str() );
}

static void showModProblems( void )
{
	s_modProblemsPending = FALSE;
	if( TheArchiveFileSystem == nullptr || TheArchiveFileSystem->getModProblems().empty() )
		return;
	static const char *const REASON_KEYS[] = { "GZH:ModProblemMissing", "GZH:ModProblemOtherGame", "GZH:ModProblemUnreadable",
		"GZH:ModProblemPartial", "GZH:ModProblemPackage", "GZH:ModProblemManifest" };
	UnicodeString body = TheGameText->fetch( "GZH:ModProblemsText" );
	for( const ArchiveFileSystem::ModProblem &problem : TheArchiveFileSystem->getModProblems() )
	{
		UnicodeString name, reason, detail;
		name.translate( problem.mod );
		reason = withNumbers( TheGameText->fetch( REASON_KEYS[static_cast<Int>( problem.kind )] ), problem.count );
		body.concat( L"\n" );
		body.concat( name );
		body.concat( L": " );
		body.concat( reason );
		if( problem.detail.isNotEmpty() )
		{
			detail.translate( problem.detail );
			body.concat( L" (" );
			body.concat( detail );
			body.concat( L")" );
		}
	}
	MessageBoxFitText( MessageBoxOk( TheGameText->fetch( "GZH:ModProblemsTitle" ), body, nullptr ) );
}

// The OK of the hardware hint: the mod notice follows with the next update (the first box still exists now).
static void queueModProblems( void )
{
	s_modProblemsPending = TRUE;
}

static void showStartupNoticesOnce( void )
{
	static Bool shown = FALSE;
	if( s_modProblemsPending && !justEntered )
	{
		showModProblems();
		return;
	}
	if( shown || justEntered )
		return;
	shown = TRUE;
	if( TheGlobalData->m_runnerMode || TheGlobalData->m_quitAfterFrames != 0 )
		return;
	Int cores = 0, threads = 0;
	if( !GameEngine_PendingHardwareHint( &cores, &threads ) )
	{
		showModProblems();
		return;
	}
	UnicodeString body = TheGameText->fetch( "GZH:HardwareHintText" );
	body.concat( L"\n" );
	body.concat( withNumbers( TheGameText->fetch( "GZH:HardwareHintCpu" ), cores, threads ) );
	body.concat( L"\n\n" );
	body.concat( TheGameText->fetch( "GZH:HardwareHintOnce" ) );
	GameEngine_ClearHardwareHint();
	UserSettings::setHardwareHintShown();
	MessageBoxFitText( MessageBoxOk( TheGameText->fetch( "GZH:HardwareHintTitle" ), body, queueModProblems ) );
}

//-------------------------------------------------------------------------------------------------
/** Main menu update method */
//-------------------------------------------------------------------------------------------------
void MainMenuUpdate( WindowLayout *layout, void *userData )
{
	if( TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() )
	{
		return;
	}
	if(DontShowMainMenu && justEntered)
		justEntered = FALSE;
	

	// Added by Saad to the confirmation or decline of the resoluotion change
	// dialog box.
	/* This is also commented for the same reason as the top
	if (dispChanged)
	{
		ResolutionDialogUpdate();
		return;
	}
	*/

	// On first start the menu only fades in on a mouse move (> 20 pixels) or a key press (notShown path
	// in MainMenuInput). Take the key press path at once where that cannot happen or must not wait for it:
	// -fixedTimeStep (the menu appears at a fixed frame for image comparisons), test sequences and
	// -echteEingabeAus (both ignore the real mouse, so no mouse move arrives).
	const Bool showAtOnce = TheGlobalData->m_fixedTimeStep || TheGlobalData->m_testScriptFile.isNotEmpty()
		|| TheGlobalData->m_noRealInput;
	if(notShown && showAtOnce && dropDownWindows[DROPDOWN_MAIN])
	{
		initialGadgetDelay = 1;
		dropDownWindows[DROPDOWN_MAIN]->winHide(FALSE);
		TheTransitionHandler->setGroup("MainMenuFade", TRUE);
		TheTransitionHandler->setGroup("MainMenuDefaultMenu");
		TheMouse->setVisibility(TRUE);
		notShown = FALSE;
	}

	if(justEntered)
	{
		if(initialGadgetDelay == 1)
		{
			TheTransitionHandler->setGroup("MainMenuDefaultMenuLogoFade");
			TheWindowManager->winSetFocus( parentMainMenu );
			initialGadgetDelay = 2;
			justEntered = FALSE;
		}
		else
			initialGadgetDelay--;
	}
	
	if(dontAllowTransitions && TheTransitionHandler->isFinished())
		dontAllowTransitions = FALSE;

	showStartupNoticesOnce();

	if(showLogo && dontAllowTransitions == FALSE)
	{
//		if(showFrames == SHOW_FRAMES_LIMIT)
//		{
//			TheTransitionHandler->remove("MainMenuSinglePlayerMenu");
			switch (showSide) {
			case SHOW_TRAINING:
				TheTransitionHandler->setGroup("MainMenuFactionTraining");
				break;
			case SHOW_CHINA:
				TheTransitionHandler->setGroup("MainMenuFactionChina");
				break;
			case SHOW_GLA:
				TheTransitionHandler->setGroup("MainMenuFactionGLA");
				break;
			case SHOW_USA:
				TheTransitionHandler->setGroup("MainMenuFactionUS");
				break;
			case SHOW_SKIRMISH:
				TheTransitionHandler->setGroup("MainMenuFactionSkirmish");
				break;
			}
			showLogo = FALSE;
//			showFrames = 0;	
//			logoIsShown = TRUE;
//		}
//		else
//			showFrames++;
	}

//	if(showFade)
//	{
//		showFade = FALSE;
//		TheTransitionHandler->reverse("FadeWholeScreen");
//	}
////
//	if (notShown)
//	{
//		if(initialGadgetDelay == 1)
//		{
//			dropDownWindows[DROPDOWN_MAIN]->winHide(FALSE);
//			TheTransitionHandler->setGroup("MainMenuFade", TRUE);
//			TheTransitionHandler->setGroup("MainMenuDefaultMenu");
//			TheMouse->setVisibility(TRUE);
//			initialGadgetDelay = 2;
//			notShown = FALSE;
//		}
//		else
//			initialGadgetDelay--;
//	}

//	if(localAnimateWindowManager)
//		localAnimateWindowManager->update();
//	if(localAnimateWindowManager && pendingDropDown != DROPDOWN_NONE && localAnimateWindowManager->isFinished())
//	{
//		localAnimateWindowManager->reset();
//		if(dropDown != DROPDOWN_NONE)
//			dropDownWindows[dropDown]->winHide(TRUE);
//		dropDown = pendingDropDown;
//		dropDownWindows[dropDown]->winHide(FALSE);
//		localAnimateWindowManager->registerGameWindow(dropDownWindows[dropDown],WIN_ANIMATION_SLIDE_TOP_FAST,TRUE,1,1);						
//		//buttonPushed = FALSE;
//		pendingDropDown = DROPDOWN_NONE;
//	}
//	else if(localAnimateWindowManager && dropDown == DROPDOWN_NONE && pendingDropDown == DROPDOWN_NONE && localAnimateWindowManager->isReversed() && localAnimateWindowManager->isFinished())
//	{
//		localAnimateWindowManager->reset();
//		for(Int i = 1; i < DROPDOWN_COUNT; ++i)
//			dropDownWindows[i]->winHide(TRUE);
//	}
	
	
	
	

	if (startGame && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
	{
		doGameStart();
	}

	// We'll only be successful if we've requested to 
	if(isShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
	{
		shutdownComplete(layout);
	}
	

	// We'll only be successful if we've requested to 
//	if(TheShell->isAnimReversed() && TheShell->isAnimFinished())
//		shutdownComplete( layout );

//	if(winVidManager)
//		winVidManager->update();

}  // end MainMenuUpdate

//-------------------------------------------------------------------------------------------------
/** Main menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType MainMenuInput( GameWindow *window, UnsignedInt msg,
																		WindowMsgData mData1, WindowMsgData mData2 )
{

	if(!notShown)
		return WindowMsgHandledType::MSG_IGNORED;
	
	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_MOUSE_POS:
		{
			ICoord2D mouse;
			mouse.x = mData1 & 0xFFFF;
			mouse.y = static_cast<Int>(mData1 >> 16);
			if( mouse.x == 0 && mouse.y == 0)
				break;

			static Int mousePosX = mouse.x;
			static Int mousePosY = mouse.y;
			if(abs(mouse.x - mousePosX) > 20 || abs(mouse.y - mousePosY) > 20)
			{
			
				DEBUG_LOG(("Mouse X:%d, Y:%d\n", mouse.x, mouse.y));
				if(notShown)
				{
					initialGadgetDelay = 1;
					dropDownWindows[DROPDOWN_MAIN]->winHide(FALSE);
					TheTransitionHandler->setGroup("MainMenuFade", TRUE);
					TheTransitionHandler->setGroup("MainMenuDefaultMenu");
					TheMouse->setVisibility(TRUE);
					notShown = FALSE;
					return WindowMsgHandledType::MSG_HANDLED;
				}
			}
			
		}  // end char
		break;
		case GWM_CHAR:
		{
			if(notShown)
			{
				initialGadgetDelay = 1;
				dropDownWindows[DROPDOWN_MAIN]->winHide(FALSE);
				TheTransitionHandler->setGroup("MainMenuFade", TRUE);
				TheTransitionHandler->setGroup("MainMenuDefaultMenu");
				TheMouse->setVisibility(TRUE);
				notShown = FALSE;
				return WindowMsgHandledType::MSG_HANDLED;
			}
			
		}  // end char

	}  // end switch( msg )
	

	return WindowMsgHandledType::MSG_IGNORED;

}  // end MainMenuInput
void PrintOffsetsFromControlBarParent( void );
//-------------------------------------------------------------------------------------------------
/** Main menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType MainMenuSystem( GameWindow *window, UnsignedInt msg, 
										 WindowMsgData mData1, WindowMsgData mData2 )
{
	static Bool triedToInitWOLAPI = FALSE;
	static Bool canInitWOLAPI = FALSE;
	
	switch( msg ) 
	{

		//---------------------------------------------------------------------------------------------
		case GWM_CREATE:
		{
			break;
		}  // end case

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{
			break;

		}  // end case

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*reinterpret_cast<Bool *>(mData2) = TRUE;

			break;

		}  // end input
		//---------------------------------------------------------------------------------------------
		case GBM_MOUSE_ENTERING:
		{
			GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
			Int controlID = control->winGetWindowId();
			if(controlID == onlineID)
			{
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_ONLINE_HIGHLIGHTED]);
			}
			else if(controlID == networkID)
			{
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_NETWORK_HIGHLIGHTED]);
			}
			else if(controlID == optionsID)
			{
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_OPTIONS_HIGHLIGHTED]);
			}
			else if(controlID == exitID)
			{
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_EXIT_HIGHLIGHTED]);
			}
			else if(controlID == buttonChallengeID)
			{
				if(dontAllowTransitions && !campaignSelected)
				{
					showLogo = TRUE;
					showSide = SHOW_TRAINING;
				}

				if(campaignSelected || dontAllowTransitions)
					break;

				TheTransitionHandler->setGroup("MainMenuFactionTraining");
			}
			// training button of the Generals data
			else if(controlID == buttonTRAININGID)
			{
				if(dontAllowTransitions && !campaignSelected)
				{
					showLogo = TRUE;
					showSide = SHOW_TRAINING;
				}

				if(campaignSelected || dontAllowTransitions)
					break;
				//TheTransitionHandler->remove("MainMenuSinglePlayerMenu");

				TheTransitionHandler->setGroup("MainMenuFactionTraining");

				//showSelectiveButtons(SHOW_NONE);
			}
			else if(controlID == skirmishID)
			{
				if(dontAllowTransitions && !campaignSelected)
				{
					showLogo = TRUE;
					showSide = SHOW_SKIRMISH;
				}

				if(campaignSelected || dontAllowTransitions)
					break;
				//TheTransitionHandler->remove("MainMenuSinglePlayerMenu");

				TheTransitionHandler->setGroup("MainMenuFactionSkirmish");
				//showSelectiveButtons(SHOW_NONE);
			}
			
			else if(controlID == buttonUSAID)
			{
				if(dontAllowTransitions && !campaignSelected)
				{
					showLogo = TRUE;
					showSide = SHOW_USA;
				}

				if(campaignSelected || dontAllowTransitions)
					break;
				//TheTransitionHandler->remove("MainMenuSinglePlayerMenu");

				TheTransitionHandler->setGroup("MainMenuFactionUS");
//				showLogo = TRUE;
//				showFrames = 0;
//				showSide = SHOW_USA;

			}
			else if(controlID == buttonGLAID)
			{
				if(dontAllowTransitions && !campaignSelected)
				{
					showLogo = TRUE;
					showSide = SHOW_GLA;
				}

				if(campaignSelected || dontAllowTransitions)
					break;
				//TheTransitionHandler->remove("MainMenuSinglePlayerMenu");
				TheTransitionHandler->setGroup("MainMenuFactionGLA");
//				showLogo = TRUE;
//				showFrames = 0;
//				showSide = SHOW_GLA;
			}
			else if(controlID == buttonChinaID)
			{
				if(dontAllowTransitions && !campaignSelected)
				{
					showLogo = TRUE;
					showSide = SHOW_CHINA;
				}
				if(campaignSelected || dontAllowTransitions)
					break;
				//TheTransitionHandler->remove("MainMenuSinglePlayerMenu");
				TheTransitionHandler->setGroup("MainMenuFactionChina");
//				showLogo = TRUE;
//				showFrames = 0;
//				showSide = SHOW_CHINA;
			}
			
		break;
		}
		//---------------------------------------------------------------------------------------------
		case GBM_MOUSE_LEAVING:
		{
			GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
			Int controlID = control->winGetWindowId();

			if(controlID == onlineID)
			{
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_ONLINE_UNHIGHLIGHTED]);
			}
			else if(controlID == networkID)
			{
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_NETWORK_UNHIGHLIGHTED]);
			}
			else if(controlID == optionsID)
			{
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_OPTIONS_UNHIGHLIGHTED]);
			}
			else if(controlID == exitID)
			{
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_EXIT_UNHIGHLIGHTED]);
			}
			else if(controlID == buttonChallengeID)
			{
				if(dontAllowTransitions && !campaignSelected && showLogo)
				{
					showLogo = FALSE;
					showSide = SHOW_NONE;
				}

				if(campaignSelected || dontAllowTransitions)
					break;

				// we'll just use the training logo anim for now
				TheTransitionHandler->reverse("MainMenuFactionTraining");
			}
			// training button of the Generals data
			else if(controlID == buttonTRAININGID)
			{
				if(dontAllowTransitions && !campaignSelected && showLogo)
				{
					showLogo = FALSE;
					showSide = SHOW_NONE;
				}

				if(campaignSelected || dontAllowTransitions)
					break;
				TheTransitionHandler->reverse("MainMenuFactionTraining");
				
				//showSelectiveButtons(SHOW_NONE);
			}
			else if(controlID == skirmishID)
			{
				if(dontAllowTransitions && !campaignSelected && showLogo)
				{
					showLogo = FALSE;
					showSide = SHOW_NONE;
				}
				if(campaignSelected || dontAllowTransitions)
					break;
				TheTransitionHandler->reverse("MainMenuFactionSkirmish");
				//showSelectiveButtons(SHOW_NONE);
			}
			else if(controlID == buttonUSAID)
			{
				if(dontAllowTransitions && !campaignSelected && showLogo)
				{
					showLogo = FALSE;
					showSide = SHOW_NONE;
				}
				if(campaignSelected || dontAllowTransitions)
					break;
				TheTransitionHandler->reverse("MainMenuFactionUS");
				
				//showSelectiveButtons(SHOW_NONE);
			}
			else if(controlID == buttonGLAID)
			{
				if(dontAllowTransitions && !campaignSelected && showLogo)
				{
					showLogo = FALSE;
					showSide = SHOW_NONE;
				}
				if(campaignSelected || dontAllowTransitions)
					break;
				TheTransitionHandler->reverse("MainMenuFactionGLA");
				//showSelectiveButtons(SHOW_NONE);
			}
			else if(controlID == buttonChinaID)
			{
				if(dontAllowTransitions && !campaignSelected && showLogo)
				{
					showLogo = FALSE;
					showSide = SHOW_NONE;
				}
				if(campaignSelected || dontAllowTransitions)
					break;
				TheTransitionHandler->reverse("MainMenuFactionChina");
				//showSelectiveButtons(SHOW_NONE);
			}	
		break;
		}
		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			
			GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
			Int controlID = control->winGetWindowId();
			
			if(buttonPushed)
				break;
#if defined _DEBUG
			if( control == buttonCampaign )
			{
				buttonPushed = TRUE;
				TheShell->push(AsciiString( "Menus/MapSelectMenu.wnd" ));
				// As soon as we have a campaign, add it in here!;
			}
			else 
#endif

			// don't allow mouse click slop that occurs during transitions to unset this flag
			if (TheTransitionHandler->isFinished()
				&& controlID != buttonEasyID && controlID != buttonMediumID && controlID != buttonHardID)
			{
				// this toggle must only be reset if one of these buttons have not been pressed
				// ...the difficulty selection behavior must have a chance to act upon this toggle
				launchChallengeMenu = FALSE;
			}


			if( controlID == buttonSinglePlayerID )
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				//buttonPushed = TRUE;
				buttonPushed = FALSE;
				dropDownWindows[DROPDOWN_SINGLE]->winHide(FALSE);
				TheTransitionHandler->remove("MainMenuDefaultMenu");
				TheTransitionHandler->reverse("MainMenuDefaultMenuBack");
				TheTransitionHandler->setGroup("MainMenuSinglePlayerMenu");
			}  // end if
			else if( controlID == buttonSingleBackID )
			{
				if(campaignSelected || dontAllowTransitions)
					break;
				buttonPushed = FALSE;
				dropDownWindows[DROPDOWN_MAIN]->winHide(FALSE);
				TheTransitionHandler->remove("MainMenuSinglePlayerMenu");
				TheTransitionHandler->reverse("MainMenuSinglePlayerMenuBack");
				TheTransitionHandler->setGroup("MainMenuDefaultMenu");
				dontAllowTransitions = TRUE;
			}  // end if
			else if( controlID == buttonMultiBackID )
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				buttonPushed = FALSE;
				dropDownWindows[DROPDOWN_MAIN]->winHide(FALSE);
				TheTransitionHandler->remove("MainMenuMultiPlayerMenu");
				TheTransitionHandler->reverse("MainMenuMultiPlayerMenuReverse");
				TheTransitionHandler->setGroup("MainMenuDefaultMenu");
			}  // end if
			else if( controlID == buttonLoadReplayBackID )
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				buttonPushed = FALSE;
				dropDownWindows[DROPDOWN_MAIN]->winHide(FALSE);
				TheTransitionHandler->remove("MainMenuLoadReplayMenu");
				TheTransitionHandler->reverse("MainMenuLoadReplayMenuBack");
				TheTransitionHandler->setGroup("MainMenuDefaultMenu");
			}  // end if
			
			else if( control == buttonCredits )
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				buttonPushed = TRUE;
				TheShell->push("Menus/CreditsMenu.wnd" );
				dropDownWindows[DROPDOWN_MAIN]->winHide(FALSE);
				TheTransitionHandler->reverse("MainMenuDefaultMenu");
			}
			else if( controlID == buttonMultiPlayerID)
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				//buttonPushed = TRUE;
				buttonPushed = FALSE;
				dropDownWindows[DROPDOWN_MULTIPLAYER]->winHide(FALSE);
				TheTransitionHandler->remove("MainMenuDefaultMenu");
				TheTransitionHandler->reverse("MainMenuDefaultMenuBack");
				TheTransitionHandler->setGroup("MainMenuMultiPlayerMenu");		
			}
			else if( controlID == buttonLoadReplayID)
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				//buttonPushed = TRUE;
				buttonPushed = FALSE;
				dropDownWindows[DROPDOWN_LOADREPLAY]->winHide(FALSE);
				TheTransitionHandler->remove("MainMenuDefaultMenu");
				TheTransitionHandler->reverse("MainMenuDefaultMenuBack");
				TheTransitionHandler->setGroup("MainMenuLoadReplayMenu");
			}
			else if( controlID == buttonLoadID )
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
//				SaveLoadLayoutType layoutType = SLLT_LOAD_ONLY;
//        WindowLayout *saveLoadMenuLayout = TheShell->getSaveLoadMenuLayout();
//				DEBUG_ASSERTCRASH( saveLoadMenuLayout, ("Unable to get save load menu layout.\n") );
//				saveLoadMenuLayout->runInit( &layoutType );
//				saveLoadMenuLayout->hide( FALSE );
//				saveLoadMenuLayout->bringForward();
				buttonPushed = TRUE;
				dropDownWindows[DROPDOWN_LOADREPLAY]->winHide(FALSE);
				TheTransitionHandler->reverse("MainMenuLoadReplayMenuBackTransition");
				TheShell->push(AsciiString("Menus/SaveLoad.wnd"));

			}
			else if( controlID == buttonReplayID )
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				buttonPushed = TRUE;
				dropDownWindows[DROPDOWN_LOADREPLAY]->winHide(FALSE);
				TheTransitionHandler->reverse("MainMenuLoadReplayMenuBackTransition");
				TheShell->push(AsciiString("Menus/ReplayMenu.wnd"));
			}
			else if( controlID == skirmishID )
			{
				if(campaignSelected || dontAllowTransitions)
					break;
				buttonPushed = TRUE;
				campaignSelected = TRUE;
				dropDownWindows[DROPDOWN_SINGLE]->winHide(FALSE);
				TheTransitionHandler->remove("MainMenuFactionSkirmish");

				TheTransitionHandler->reverse("MainMenuSinglePlayerMenuBackSkirmish");
#ifdef _CAMPEA_DEMO
				TheCampaignManager->setCampaign( "MD_CAMPEA_DEMO" );
/*
				TheTransitionHandler->setGroup("MainMenuDifficultyMenuUS");
				logoIsShown = FALSE;
				showLogo = FALSE;
				showSide = SHOW_USA;
*/
				prepareCampaignGame(DIFFICULTY_NORMAL);
				break;
#endif
				TheShell->push( AsciiString("Menus/SkirmishGameOptionsMenu.wnd") );
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_SKIRMISH_SELECTED]);
			}
			else if( controlID == networkID || controlID == onlineID )
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				buttonPushed = TRUE;
				dropDownWindows[DROPDOWN_MULTIPLAYER]->winHide(FALSE);
				TheTransitionHandler->reverse("MainMenuMultiPlayerMenuTransitionToNext");
				GameLobby::setMenuMode( controlID == onlineID ? GameLobby::Mode::Internet : GameLobby::Mode::Lan );
				TheShell->push( AsciiString("Menus/LanLobbyMenu.wnd") );

				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_NETWORK_SELECTED]);
			}  // end else if
			else if( controlID == optionsID )
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				//buttonPushed = TRUE;
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_OPTIONS_SELECTED]);

				// load the options menu
				WindowLayout *optLayout = TheShell->getOptionsLayout(TRUE);
				DEBUG_ASSERTCRASH(optLayout != nullptr, ("unable to get options menu layout"));
				optLayout->runInit();
				optLayout->hide(FALSE);
				optLayout->bringForward();
				hideForOptions();
			}  // end else if
			else if( controlID == worldBuilderID )
			{
#if defined _DEBUG
				if(_spawnl(_P_NOWAIT,"WorldBuilderD.exe","WorldBuilderD.exe", nullptr) < 0)
					MessageBoxOk(TheGameText->fetch("GUI:WorldBuilder"), TheGameText->fetch("GUI:WorldBuilderLoadFailed"),nullptr);
#else
				if(_spawnl(_P_NOWAIT,"WorldBuilder.exe","WorldBuilder.exe", nullptr) < 0)
					MessageBoxOk(TheGameText->fetch("GUI:WorldBuilder"), TheGameText->fetch("GUI:WorldBuilderLoadFailed"),nullptr);
#endif
			}
			else if( controlID == exitID )
			{
				// If we ever want to add a dialog before we exit out of the game, uncomment this line and kill the quitCallback() line below.
//#if defined(_DEBUG)
				
				//Added By Sadullah Nader
				//Changed the preprocessing code to normal code
				if (TheGlobalData->m_windowed)
				{
					quitCallback();
//#else	
				}
				else
				{
					QuitMessageBoxYesNo(TheGameText->fetch("GUI:QuitPopupTitle"), TheGameText->fetch("GUI:QuitPopupMessage"),quitCallback,nullptr);
				}
				//
//#endif
				
			}  // end else if
			else if(controlID == buttonChallengeID)
			{
				if(campaignSelected || dontAllowTransitions)
					break;

				// set up for the difficulty select into challenge menu
				TheTransitionHandler->setGroup("MainMenuFactionTraining");
				GameWindow *win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionTraining"));
				if(win)
					win->winHide(TRUE);
				TheTransitionHandler->reverse("MainMenuSinglePlayerMenuBackTraining");
				TheTransitionHandler->setGroup("MainMenuDifficultyMenuTraining");
				campaignSelected = TRUE;
				showLogo = FALSE;
				showSide = SHOW_TRAINING;
				launchChallengeMenu = TRUE;
			}
			

// training button of the Generals data
			else if(controlID == buttonTRAININGID)
			{
				if(campaignSelected || dontAllowTransitions)
					break;
				TheCampaignManager->setCampaign( "TRAINING" );
				TheTransitionHandler->setGroup("MainMenuFactionTraining");
				TheTransitionHandler->remove("MainMenuFactionTraining", TRUE);
				GameWindow *win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionTraining"));
				if(win)
					win->winHide(TRUE);
				TheTransitionHandler->reverse("MainMenuSinglePlayerMenuBackTraining");
				TheTransitionHandler->setGroup("MainMenuDifficultyMenuTraining");
				campaignSelected = TRUE;
				showLogo = FALSE;
				showSide = SHOW_TRAINING;

//				setupGameStart(TheCampaignManager->getCurrentMap());
			}
			else if(controlID == buttonUSAID)
			{
				if(campaignSelected || dontAllowTransitions)
					break;
				TheCampaignManager->setCampaign( "USA" );
#ifdef _CAMPEA_DEMO
				TheCampaignManager->setCampaign( "MD_USA_1_DEMO" );
#endif
				TheTransitionHandler->setGroup("MainMenuFactionUS");
				TheTransitionHandler->remove("MainMenuFactionUS", TRUE);
				GameWindow *win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionUS"));
				if(win)
					win->winHide(TRUE);
				TheTransitionHandler->reverse("MainMenuSinglePlayerMenuBackUS");
				TheTransitionHandler->setGroup("MainMenuDifficultyMenuUS");
				campaignSelected = TRUE;
				logoIsShown = FALSE;
				showLogo = FALSE;
				showSide = SHOW_USA;
//				launchChallengeMenu = FALSE;
//				WindowLayout *layout = NULL;
//				layout = TheWindowManager->winCreateLayout( AsciiString( "Menus/DifficultySelect.wnd" ) );
//				layout->runInit();
//				layout->hide( FALSE );
//				layout->bringForward();

//				setupGameStart(TheCampaignManager->getCurrentMap());
			}
			else if(controlID == buttonGLAID)
			{
				if(campaignSelected || dontAllowTransitions)
					break;
				TheCampaignManager->setCampaign( "GLA" );
#ifdef _CAMPEA_DEMO
				TheCampaignManager->setCampaign( "MD_USA_2_DEMO" );
#endif
				TheTransitionHandler->setGroup("MainMenuFactionGLA");
				TheTransitionHandler->remove("MainMenuFactionGLA", TRUE);
				GameWindow *win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionGLA"));
				if(win)
					win->winHide(TRUE);				
				TheTransitionHandler->reverse("MainMenuSinglePlayerMenuBackGLA");
				TheTransitionHandler->setGroup("MainMenuDifficultyMenuGLA");
				campaignSelected = TRUE;
				logoIsShown = FALSE;
				showLogo = FALSE;
				showSide = SHOW_GLA;
//				launchChallengeMenu = FALSE;
//				WindowLayout *layout = NULL;
//				layout = TheWindowManager->winCreateLayout( AsciiString( "Menus/DifficultySelect.wnd" ) );
//				layout->runInit();
//				layout->hide( FALSE );
//				layout->bringForward();

//				setupGameStart(TheCampaignManager->getCurrentMap());
			}
			else if(controlID == buttonChinaID)
			{
				if(campaignSelected || dontAllowTransitions)
					break;
				TheCampaignManager->setCampaign( "China" );
#ifdef _CAMPEA_DEMO
				TheCampaignManager->setCampaign( "MD_GLA_3_DEMO" );
#endif
				TheTransitionHandler->setGroup("MainMenuFactionChina");
				TheTransitionHandler->remove("MainMenuFactionChina", TRUE);
				GameWindow *win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionChina"));
				if(win)
					win->winHide(TRUE);				
				TheTransitionHandler->reverse("MainMenuSinglePlayerMenuBackChina");
				TheTransitionHandler->setGroup("MainMenuDifficultyMenuChina");
				campaignSelected = TRUE;
				logoIsShown = FALSE;
				showLogo = FALSE;
				showSide = SHOW_CHINA;
//				launchChallengeMenu = FALSE;
//				WindowLayout *layout = NULL;
//				layout = TheWindowManager->winCreateLayout( AsciiString( "Menus/DifficultySelect.wnd" ) );
//				layout->runInit();
//				layout->hide( FALSE );
//				layout->bringForward();

//				setupGameStart(TheCampaignManager->getCurrentMap());
			}// end else if
			else if(controlID == buttonEasyID)
			{
				if(dontAllowTransitions)
					break;

				prepareCampaignGame(DIFFICULTY_EASY);
			}
			else if(controlID == buttonMediumID)
			{
				if(dontAllowTransitions)
					break;

				prepareCampaignGame(DIFFICULTY_NORMAL);
			}
			else if(controlID == buttonHardID)
			{
				if(dontAllowTransitions)
					break;

				prepareCampaignGame(DIFFICULTY_HARD);
			}
			else if(controlID == buttonDiffBackID)
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				TheCampaignManager->setCampaign( AsciiString::TheEmptyString );
				diffReverseSide();
				campaignSelected = FALSE;
			}


			break;

		}  // end selected
		
		//---------------------------------------------------------------------------------------------
		default:
			return WindowMsgHandledType::MSG_IGNORED;

	}  // end switch

	return WindowMsgHandledType::MSG_HANDLED;

}  // end MainMenuSystem

void diffReverseSide( void )
{
	switch (showSide) {
	case SHOW_TRAINING:
		TheTransitionHandler->reverse("MainMenuDifficultyMenuTrainingBack");
		TheTransitionHandler->setGroup("MainMenuSinglePlayerTrainingMenuFromDiff");
		break;
	case SHOW_USA:
		TheTransitionHandler->reverse("MainMenuDifficultyMenuUSBack");
		TheTransitionHandler->setGroup("MainMenuSinglePlayerUSAMenuFromDiff");
		break;
	case SHOW_GLA:
		TheTransitionHandler->reverse("MainMenuDifficultyMenuGLABack");
		TheTransitionHandler->setGroup("MainMenuSinglePlayerGLAMenuFromDiff");
		break;
	case SHOW_CHINA:
		TheTransitionHandler->reverse("MainMenuDifficultyMenuChinaBack");
		TheTransitionHandler->setGroup("MainMenuSinglePlayerChinaMenuFromDiff");

		break;
	}
}
