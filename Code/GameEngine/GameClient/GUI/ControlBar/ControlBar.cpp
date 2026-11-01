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

// FILE: ControlBar.cpp ///////////////////////////////////////////////////////////////////////////
// Author: Colin Day, March 2002
// Desc:   Context sensitive command interface
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#define DEFINE_GUI_COMMMAND_NAMES
#define DEFINE_COMMAND_OPTION_NAMES
#define DEFINE_WEAPONSLOTTYPE_NAMES
#define DEFINE_RADIUSCURSOR_NAMES

#include "Common/ActionManager.h"
#include "Common/GameType.h"
#include "Common/MultiplayerSettings.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Override.h"
#include "Common/PlayerTemplate.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ProductionPrerequisite.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/Upgrade.h"
#include "Common/Recorder.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/OCLUpdate.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "GameLogic/Module/RebuildHoleBehavior.h"
#include "GameLogic/ScriptEngine.h"

#include "GameClient/AnimateWindowManager.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarScheme.h"
#include "GameClient/Drawable.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayStringManager.h"
#include "Common/GameEdition.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameText.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetProgressBar.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/InGameUI.h"
#include "GameClient/WindowVideoManager.h"
#include "GameClient/ControlBarResizer.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/HotKey.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/GUICallbacks.h"

#include "GameNetwork/GameInfo.h"

// PUBLIC /////////////////////////////////////////////////////////////////////////////////////////

const Image* ControlBar::m_rankVeteranIcon	= nullptr;
const Image* ControlBar::m_rankEliteIcon		= nullptr;
const Image* ControlBar::m_rankHeroicIcon		= nullptr;

///////////////////////////////////////////////////////////////////////////////////////////////////
// CommandButton //////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
const FieldParse CommandButton::s_commandButtonFieldParseTable[] = 
{

	{ "Command",							CommandButton::parseCommand, nullptr, offsetof( CommandButton, m_command ) },
	{ "Options",							INI::parseBitString32,		   TheCommandOptionNames, offsetof( CommandButton, m_options ) },
	{ "Object",								INI::parseThingTemplate,		 nullptr, offsetof( CommandButton, m_thingTemplate ) },
	{ "Upgrade",							INI::parseUpgradeTemplate,	 nullptr, offsetof( CommandButton, m_upgradeTemplate ) },
	{ "WeaponSlot",						INI::parseLookupList,				 TheWeaponSlotTypeNamesLookupList, offsetof( CommandButton, m_weaponSlot ) },
	{ "MaxShotsToFire",				INI::parseInt,							 nullptr, offsetof( CommandButton, m_maxShotsToFire ) },
	{ "Science",							INI::parseScienceVector,					 nullptr, offsetof( CommandButton, m_science ) },
	{ "SpecialPower",					INI::parseSpecialPowerTemplate,			 nullptr, offsetof( CommandButton, m_specialPower ) },
	{ "TextLabel",						INI::parseAsciiString,			 nullptr, offsetof( CommandButton, m_textLabel ) },
	{ "DescriptLabel",				INI::parseAsciiString,			 nullptr, offsetof( CommandButton, m_descriptionLabel ) },
	{ "PurchasedLabel",				INI::parseAsciiString,			 nullptr, offsetof( CommandButton, m_purchasedLabel ) },
	{ "ConflictingLabel",			INI::parseAsciiString,			 nullptr, offsetof( CommandButton, m_conflictingLabel ) },
	{ "ButtonImage",					INI::parseAsciiString,			 nullptr, offsetof( CommandButton, m_buttonImageName ) },
	{ "CursorName",						INI::parseAsciiString,			 nullptr, offsetof( CommandButton, m_cursorName ) },
	{ "InvalidCursorName",		INI::parseAsciiString,       nullptr, offsetof( CommandButton, m_invalidCursorName ) },
	{ "ButtonBorderType",			INI::parseLookupList,				 CommandButtonMappedBorderTypeNames, offsetof( CommandButton, m_commandButtonBorder ) },
	{ "RadiusCursorType",			INI::parseIndexList,				 TheRadiusCursorNames, offsetof( CommandButton, m_radiusCursor ) },
	{ "UnitSpecificSound",		INI::parseAudioEventRTS,		 nullptr, offsetof( CommandButton, m_unitSpecificSound ) }, 

	{ nullptr,						nullptr,												 nullptr, 0 }  // keep this last

};
static void commandButtonTooltip(GameWindow *window,
													WinInstanceData *instData,
													UnsignedInt mouse)
{
	TheControlBar->showBuildTooltipLayout(window);
}

/// mark the UI as dirty so the context of everything is re-evaluated
void ControlBar::markUIDirty( void )
{ 
  m_UIDirty = TRUE;

#if defined( _DEBUG )
	UnsignedInt now = TheGameLogic->getFrame();
	if( now == m_lastFrameMarkedDirty )
	{
		//Do nothing.
	}
	else if( now == m_lastFrameMarkedDirty + 1 )
	{
		m_consecutiveDirtyFrames++;
	}
	else
	{
		m_consecutiveDirtyFrames = 1;
	}
	m_lastFrameMarkedDirty = now;

	if( m_consecutiveDirtyFrames > 20 )
	{
		DEBUG_CRASH( ("Serious flaw in interface system! Either new code or INI has caused the interface to be marked dirty every frame. This problem actually causes the interface to completely lockup not allowing you to click normal game buttons.") );
	}

#endif
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// CommandSet /////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
/** These are the fields you can define in a command set, they correspond to physical
	* buttons in the GUI */
//-------------------------------------------------------------------------------------------------
const FieldParse CommandSet::m_commandSetFieldParseTable[] = 
{
	
	{ "1",			CommandSet::parseCommandButton, static_cast<void *>(0),		offsetof( CommandSet, m_command ) },
	{ "2",			CommandSet::parseCommandButton, reinterpret_cast<void *>(1),		offsetof( CommandSet, m_command ) },
	{ "3",			CommandSet::parseCommandButton, reinterpret_cast<void *>(2),		offsetof( CommandSet, m_command ) },
	{ "4",			CommandSet::parseCommandButton, reinterpret_cast<void *>(3),		offsetof( CommandSet, m_command ) },
	{ "5",			CommandSet::parseCommandButton, reinterpret_cast<void *>(4),		offsetof( CommandSet, m_command ) },
	{ "6",			CommandSet::parseCommandButton, reinterpret_cast<void *>(5),		offsetof( CommandSet, m_command ) },
	{ "7",			CommandSet::parseCommandButton, reinterpret_cast<void *>(6),		offsetof( CommandSet, m_command ) },
	{ "8",			CommandSet::parseCommandButton, reinterpret_cast<void *>(7),		offsetof( CommandSet, m_command ) },
	{ "9",			CommandSet::parseCommandButton, reinterpret_cast<void *>(8),		offsetof( CommandSet, m_command ) },
	{ "10",			CommandSet::parseCommandButton, reinterpret_cast<void *>(9),		offsetof( CommandSet, m_command ) },
	{ "11",			CommandSet::parseCommandButton, reinterpret_cast<void *>(10),		offsetof( CommandSet, m_command ) },
	{ "12",			CommandSet::parseCommandButton, reinterpret_cast<void *>(11),		offsetof( CommandSet, m_command ) },
	{ "13",			CommandSet::parseCommandButton, reinterpret_cast<void *>(12),		offsetof( CommandSet, m_command ) },
	{ "14",			CommandSet::parseCommandButton, reinterpret_cast<void *>(13),		offsetof( CommandSet, m_command ) },
	{ "15",			CommandSet::parseCommandButton, reinterpret_cast<void *>(14),		offsetof( CommandSet, m_command ) },
	{ "16",			CommandSet::parseCommandButton, reinterpret_cast<void *>(15),		offsetof( CommandSet, m_command ) },
	{ "17",			CommandSet::parseCommandButton, reinterpret_cast<void *>(16),		offsetof( CommandSet, m_command ) },
	{ "18",			CommandSet::parseCommandButton, reinterpret_cast<void *>(17),		offsetof( CommandSet, m_command ) },
	{ nullptr,			nullptr,														 nullptr,				0	}  // keep this last

};

///////////////////////////////////////////////////////////////////////////////////////////////////
// ControlBar /////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ControlBar::ControlBar( void )
{
	Int i;
	m_commandButtons = nullptr;
	m_commandSets = nullptr;
	m_controlBarSchemeManager = nullptr;
	m_isObserverCommandBar = FALSE;
	m_observerLookAtPlayer = nullptr;
	m_buildToolTipLayout = nullptr;
	m_showBuildToolTipLayout = FALSE;

	// Added By Sadullah Nader
	// initializing vars to zero
	m_animateDownWin1Pos.x = m_animateDownWin1Pos.y = 0;
	m_animateDownWin1Size.x = m_animateDownWin1Size.y = 0;
	m_animateDownWin2Pos.x = m_animateDownWin2Pos.y = 0;
	m_animateDownWin2Size.x = m_animateDownWin2Size.y = 0;

	m_animateDownWindow = nullptr;
	m_animTime = 0;

	for( i = 0; i < MAX_COMMANDS_PER_SET; i++)
	{
		m_commonCommands[i] = 0;
	}
	
	m_currContext = ControlBarContext::CB_CONTEXT_NONE;
	m_defaultControlBarPosition.x = m_defaultControlBarPosition.y = 0;
	m_genStarFlash = FALSE;
  m_genStarOff = nullptr;
	m_genStarOn  = nullptr;
	m_UIDirty    = FALSE;
	//
//	m_controlBarResizer = NULL;
	m_buildUpClockColor = GameMakeColor(0,0,0,100);
	m_commandBarBorderColor = GameMakeColor(0,0,0,100);
	for( i = 0; i < NUM_CONTEXT_PARENTS; i++ )
		m_contextParent[ i ] = nullptr;
	for( i = 0; i < MAX_COMMANDS_PER_SET; i++ )
	{
		m_commandWindows[ i ] = nullptr;
	// removed from multiplayer branch
		//m_commandMarkers[ i ] = NULL;
	}

	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_1; i++ )
		m_sciencePurchaseWindowsRank1[i] = nullptr;
	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_3; i++ )
		m_sciencePurchaseWindowsRank3[i] = nullptr;
	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_8; i++ )
		m_sciencePurchaseWindowsRank8[i] = nullptr;

	for( i = 0; i < MAX_SPECIAL_POWER_SHORTCUTS; i++ )
	{
		m_specialPowerShortcutButtons[i] = nullptr;
		m_specialPowerShortcutButtonParents[i] = nullptr;
	}

	m_specialPowerShortcutParent = nullptr;
	m_specialPowerLayout = nullptr;
	m_scienceLayout = nullptr;
	m_rightHUDWindow = nullptr;
	m_rightHUDCameoWindow = nullptr;
	for( i = 0; i < MAX_RIGHT_HUD_UPGRADE_CAMEOS; i++ )
		m_rightHUDUpgradeCameos[i];
	m_rightHUDUnitSelectParent = nullptr;
	m_communicatorButton = nullptr;
	m_currentSelectedDrawable = nullptr;
	m_currContext = ControlBarContext::CB_CONTEXT_NONE;
	m_rallyPointDrawableID = INVALID_DRAWABLE_ID;
	m_displayedConstructPercent = -1.0f;
	m_displayedOCLTimerSeconds = 0;
	m_displayedQueueCount = 0;
	resetBuildQueueData();
	resetContainData();
	m_lastRecordedInventoryCount = 0;
	
	m_videoManager = nullptr;
	m_animateWindowManager = nullptr;
	m_generalsScreenAnimate = nullptr;
	m_animateWindowManagerForGenShortcuts = nullptr;
	m_flash = FALSE;
	m_toggleButtonUpIn = nullptr;
	m_toggleButtonUpOn = nullptr;
	m_toggleButtonUpPushed = nullptr;
	m_toggleButtonDownIn = nullptr;
	m_toggleButtonDownOn = nullptr;
	m_toggleButtonDownPushed = nullptr;
	
	m_generalButtonEnable = nullptr;
	m_generalButtonHighlight = nullptr;
	m_genArrow = nullptr;
	m_sideSelectAnimateDown = FALSE;
	updateCommanBarBorderColors(GAME_COLOR_UNDEFINED,GAME_COLOR_UNDEFINED,GAME_COLOR_UNDEFINED,GAME_COLOR_UNDEFINED);

	m_radarAttackGlowOn = FALSE;
	m_remainingRadarAttackGlowFrames = 0;
	m_radarAttackGlowWindow = nullptr;

#if defined( _DEBUG )
	m_lastFrameMarkedDirty = 0;
	m_consecutiveDirtyFrames = 0;
#endif

}  // end ControlBar

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ControlBar::~ControlBar( void )
{

	if(m_scienceLayout)
	{
		m_scienceLayout->destroyWindows();
		deleteInstanceOf(m_scienceLayout);
	}
	m_scienceLayout = nullptr;
	m_genArrow = nullptr;
	m_videoManager = nullptr;


	m_animateWindowManagerForGenShortcuts = nullptr;
	m_animateWindowManager = nullptr;
	
	m_generalsScreenAnimate = nullptr;

	m_controlBarSchemeManager = nullptr;

//	if(m_controlBarResizer)
//		delete m_controlBarResizer;
//	m_controlBarResizer = NULL;
	// destroy all the command set definitions
	CommandSet *set;
	while( m_commandSets )
	{
		set = m_commandSets->friend_getNext();
		deleteInstanceOf(m_commandSets);
		m_commandSets = set;

	}  // end while

	// destroy all our command button definitions
	CommandButton *button;
	while( m_commandButtons )
	{
		button = m_commandButtons->friend_getNext();
		deleteInstanceOf(m_commandButtons);
		m_commandButtons = button;

	}  // end while
	if(m_buildToolTipLayout)
	{
		m_buildToolTipLayout->destroyWindows();
		deleteInstanceOf(m_buildToolTipLayout);
		m_buildToolTipLayout = nullptr;
	}

	if(m_specialPowerLayout)
	{
		m_specialPowerLayout->destroyWindows();
		deleteInstanceOf(m_specialPowerLayout);
		m_specialPowerLayout = nullptr;
	}

	m_radarAttackGlowWindow = nullptr;

	if (m_rightHUDCameoWindow && m_rightHUDCameoWindow->winGetUserData())
		delete static_cast<PushButtonData *>(m_rightHUDCameoWindow->winGetUserData());

}  // end ~ControlBar
void ControlBarPopupDescriptionUpdateFunc( WindowLayout *layout, void *param );

//-------------------------------------------------------------------------------------------------
/** Initialzie the control bar, this is our interface to the context sinsitive GUI */
//-------------------------------------------------------------------------------------------------
void ControlBar::init( void )
{
	INI ini;
	m_sideSelectAnimateDown = FALSE;
	// load the command buttons
	ini.load( AsciiString( "Data\\INI\\Default\\CommandButton.ini" ), INILoadType::INI_LOAD_OVERWRITE, nullptr );
	ini.load( AsciiString( "Data\\INI\\CommandButton.ini" ), INILoadType::INI_LOAD_OVERWRITE, nullptr );

	// load the command sets
	ini.load( AsciiString( "Data\\INI\\CommandSet.ini" ), INILoadType::INI_LOAD_OVERWRITE, nullptr );

	// post process step after loading the command buttons and command sets
	postProcessCommands();

	// Init the scheme manager, this will call it's won INI init funciton.
	m_controlBarSchemeManager = std::make_unique<ControlBarSchemeManager>();
	m_controlBarSchemeManager->init();

	//Added this check because the builder uses the ControlBar, but doesn't care about
	//the GUI.
	if( TheWindowManager )
	{
		//
		// the control bar has several windows that make up our context sensitive interface, we
		// want those parent windows so that we can easily hide and show them to make the 
		// interface context sensitive
		//
		NameKeyType id;
		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ControlBarParent" );
		m_contextParent[ CP_MASTER ] = TheWindowManager->winGetWindowFromId( nullptr, id );
	m_contextParent[ CP_MASTER ]->winGetPosition(&m_defaultControlBarPosition.x, &m_defaultControlBarPosition.y);
		
		m_scienceLayout = TheWindowManager->winCreateLayout("GeneralsExpPoints.wnd");
		m_scienceLayout->hide(TRUE);
		id = TheNameKeyGenerator->nameToKey( "GeneralsExpPoints.wnd:GenExpParent" );

		m_contextParent[ CP_PURCHASE_SCIENCE ] = TheWindowManager->winGetWindowFromId( nullptr, id );//m_scienceLayout->getFirstWindow();

		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:UnderConstructionWindow" );
		m_contextParent[ CP_UNDER_CONSTRUCTION ] = TheWindowManager->winGetWindowFromId( nullptr, id );

		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:OCLTimerWindow" );
		m_contextParent[ CP_OCL_TIMER ] = TheWindowManager->winGetWindowFromId( nullptr, id );

		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:BeaconWindow" );
		m_contextParent[ CP_BEACON ] = TheWindowManager->winGetWindowFromId( nullptr, id );

		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:CommandWindow" );
		m_contextParent[ CP_COMMAND ] = TheWindowManager->winGetWindowFromId( nullptr, id );

		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ProductionQueueWindow" );
		m_contextParent[ CP_BUILD_QUEUE ] = TheWindowManager->winGetWindowFromId( nullptr, id );

		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ObserverPlayerListWindow" );
		m_contextParent[ CP_OBSERVER_LIST ] = TheWindowManager->winGetWindowFromId( nullptr, id );

		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ObserverPlayerInfoWindow" );
		m_contextParent[ CP_OBSERVER_INFO ] = TheWindowManager->winGetWindowFromId( nullptr, id );


		// get the command windows and save for easy access later
		Int i;
		ICoord2D commandSize, commandPos;
		AsciiString windowName;
		for( i = 0; i < MAX_COMMANDS_PER_SET; i++ )
		{
		
			windowName.format( "ControlBar.wnd:ButtonCommand%02d", i + 1 );
			id = TheNameKeyGenerator->nameToKey( windowName.str() );
			m_commandWindows[ i ] = 
				TheWindowManager->winGetWindowFromId( m_contextParent[ CP_COMMAND ], id );
			if (m_commandWindows[ i ])
			{
				m_commandWindows[ i ]->winGetPosition(&commandPos.x, &commandPos.y);
				m_commandWindows[ i ]->winGetSize(&commandSize.x, &commandSize.y);
				m_commandWindows[ i ]->winSetStatus( WIN_STATUS_USE_OVERLAY_STATES );
			}

	// removed from multiplayer branch
//			windowName.format( "ControlBar.wnd:CommandMarker%02d", i + 1 );
//			id = TheNameKeyGenerator->nameToKey( windowName.str() );
//			m_commandMarkers[ i ] = 
//				TheWindowManager->winGetWindowFromId( m_contextParent[ CP_COMMAND ], id );
//			// set the size and position to make sure their in the same place as the buttons.
//			m_commandMarkers[i]->winSetPosition(commandPos.x -2, commandPos.y - 2);
//			m_commandMarkers[i]->winSetSize(commandSize.x + 2, commandSize.y + 2);
			


		}  // end for i


		for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_1; i++ )
		{
			windowName.format( "GeneralsExpPoints.wnd:ButtonRank1Number%d", i );
			id = TheNameKeyGenerator->nameToKey( windowName.str() );
			m_sciencePurchaseWindowsRank1[ i ] =
				TheWindowManager->winGetWindowFromId( m_contextParent[ CP_PURCHASE_SCIENCE ], id );
			if( m_sciencePurchaseWindowsRank1[ i ] )	// the Generals window has fewer buttons
				m_sciencePurchaseWindowsRank1[ i ]->winSetStatus( WIN_STATUS_USE_OVERLAY_STATES );
		}  // end for i
		for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_3; i++ )
		{
			windowName.format( "GeneralsExpPoints.wnd:ButtonRank3Number%d", i );
			id = TheNameKeyGenerator->nameToKey( windowName.str() );
			m_sciencePurchaseWindowsRank3[ i ] =
				TheWindowManager->winGetWindowFromId( m_contextParent[ CP_PURCHASE_SCIENCE ], id );
			if( m_sciencePurchaseWindowsRank3[ i ] )	// the Generals window has fewer buttons
				m_sciencePurchaseWindowsRank3[ i ]->winSetStatus( WIN_STATUS_USE_OVERLAY_STATES );
		}  // end for i
		
		for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_8; i++ ) 
		{
			windowName.format( "GeneralsExpPoints.wnd:ButtonRank8Number%d", i );
			id = TheNameKeyGenerator->nameToKey( windowName.str() );
			m_sciencePurchaseWindowsRank8[ i ] =
				TheWindowManager->winGetWindowFromId( m_contextParent[ CP_PURCHASE_SCIENCE ], id );
			if( m_sciencePurchaseWindowsRank8[ i ] )	// the Generals window has fewer buttons
				m_sciencePurchaseWindowsRank8[ i ]->winSetStatus( WIN_STATUS_USE_OVERLAY_STATES );
		}  // end for i

		// keep a pointer to the window making up the right HUD display
		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:RightHUD" );
		m_rightHUDWindow = TheWindowManager->winGetWindowFromId( nullptr, id );
	
		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:WinUnitSelected" );
		m_rightHUDUnitSelectParent = TheWindowManager->winGetWindowFromId( nullptr, id );
		
		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:CameoWindow" );
		m_rightHUDCameoWindow = TheWindowManager->winGetWindowFromId( nullptr, id );
		for( i = 0; i < MAX_RIGHT_HUD_UPGRADE_CAMEOS; i++ )
		{
			windowName.format( "ControlBar.wnd:UnitUpgrade%d", i+1 );
			id = TheNameKeyGenerator->nameToKey( windowName.str() );
			m_rightHUDUpgradeCameos[ i ] = 
				TheWindowManager->winGetWindowFromId( m_rightHUDWindow, id );
			m_rightHUDUpgradeCameos[ i ]->winSetStatus( WIN_STATUS_USE_OVERLAY_STATES );
		}

//		m_transitionHandler = NEW GameWindowTransitionsHandler;
//		m_transitionHandler->load();
//		m_transitionHandler->init();

		// don't forget about the communicator button CCB
		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:PopupCommunicator" );
		m_communicatorButton = TheWindowManager->winGetWindowFromId( nullptr, id );
		setControlCommand(m_communicatorButton, findCommandButton("NonCommand_Communicator") );
		m_communicatorButton->winSetTooltipFunc(commandButtonTooltip);

		GameWindow *win = TheWindowManager->winGetWindowFromId(nullptr,TheNameKeyGenerator->nameToKey("ControlBar.wnd:ButtonOptions"));
		if(win)
		{
			setControlCommand(win, findCommandButton("NonCommand_Options") );
			win->winSetTooltipFunc(commandButtonTooltip);
		}
		win = TheWindowManager->winGetWindowFromId(nullptr,TheNameKeyGenerator->nameToKey("ControlBar.wnd:ButtonIdleWorker"));
		if(win)
		{
			setControlCommand(win, findCommandButton("NonCommand_IdleWorker") );
			win->winSetTooltipFunc(commandButtonTooltip);
		}
		win = TheWindowManager->winGetWindowFromId(nullptr,TheNameKeyGenerator->nameToKey("ControlBar.wnd:ButtonPlaceBeacon"));
		if(win)
		{
			setControlCommand(win, findCommandButton("NonCommand_Beacon") );
			win->winSetTooltipFunc(commandButtonTooltip);
		}
		win = TheWindowManager->winGetWindowFromId(nullptr,TheNameKeyGenerator->nameToKey("ControlBar.wnd:ButtonGeneral"));
		if(win)
		{
			setControlCommand(win, findCommandButton("NonCommand_GeneralsExperience") );
			win->winSetTooltipFunc(commandButtonTooltip);
		}
		win = TheWindowManager->winGetWindowFromId(nullptr,TheNameKeyGenerator->nameToKey("ControlBar.wnd:ButtonLarge"));
		if(win)
		{
			setControlCommand(win, findCommandButton("NonCommand_UpDown") );
			win->winSetTooltipFunc(commandButtonTooltip);
		}
		
		win = TheWindowManager->winGetWindowFromId(nullptr,TheNameKeyGenerator->nameToKey("ControlBar.wnd:PowerWindow"));
		if(win)
		{
			win->winSetTooltipFunc(commandButtonTooltip);
		}
		win = TheWindowManager->winGetWindowFromId(nullptr,TheNameKeyGenerator->nameToKey("ControlBar.wnd:MoneyDisplay"));
		if(win)
		{
			win->winSetTooltipFunc(commandButtonTooltip);
		}
		win = TheWindowManager->winGetWindowFromId(nullptr, TheNameKeyGenerator->nameToKey("ControlBar.wnd:GeneralsExp"));
		if(win)
		{
			win->winSetTooltipFunc(commandButtonTooltip);
		}

		m_radarAttackGlowWindow = TheWindowManager->winGetWindowFromId(nullptr, TheNameKeyGenerator->nameToKey("ControlBar.wnd:WinUAttack"));


		win = TheWindowManager->winGetWindowFromId(nullptr,TheNameKeyGenerator->nameToKey( AsciiString( "ControlBar.wnd:BackgroundMarker" ) ));
		win->winGetScreenPosition(&m_controlBarForegroundMarkerPos.x, &m_controlBarForegroundMarkerPos.y);
		win = TheWindowManager->winGetWindowFromId(nullptr,TheNameKeyGenerator->nameToKey( AsciiString( "ControlBar.wnd:BackgroundMarker" ) ));
		win->winGetScreenPosition(&m_controlBarBackgroundMarkerPos.x,&m_controlBarBackgroundMarkerPos.y);

		if(!m_videoManager)
			m_videoManager = std::make_unique<WindowVideoManager>();
		if(!m_animateWindowManager)
			m_animateWindowManager = std::make_unique<AnimateWindowManager>();
		if(!m_generalsScreenAnimate)
			m_generalsScreenAnimate = std::make_unique<AnimateWindowManager>();
		if(!m_animateWindowManagerForGenShortcuts)
			m_animateWindowManagerForGenShortcuts = std::make_unique<AnimateWindowManager>();
		m_buildToolTipLayout = TheWindowManager->winCreateLayout( "ControlBarPopupDescription.wnd" );
		if(m_buildToolTipLayout)
		{
			m_buildToolTipLayout->hide(TRUE);
			m_buildToolTipLayout->setUpdate(ControlBarPopupDescriptionUpdateFunc);
		}
			
		m_genStarOn = TheMappedImageCollection ? const_cast<Image *>(TheMappedImageCollection->findImageByName("BarButtonGenStarON")) : nullptr;
		m_genStarOff = TheMappedImageCollection ? const_cast<Image *>(TheMappedImageCollection->findImageByName("BarButtonGenStarOFF")) : nullptr;
		m_genStarFlash = TRUE;
		m_lastFlashedAtPointValue = -1;

		m_rankVeteranIcon = TheMappedImageCollection ? TheMappedImageCollection->findImageByName( "SSChevron1L" ) : nullptr;
		m_rankEliteIcon		= TheMappedImageCollection ? TheMappedImageCollection->findImageByName( "SSChevron2L" ) : nullptr;
		m_rankHeroicIcon	= TheMappedImageCollection ? TheMappedImageCollection->findImageByName( "SSChevron3L" ) : nullptr;


//		if(!m_controlBarResizer)
//			m_controlBarResizer = NEW ControlBarResizer;
//		m_controlBarResizer->init();
		


		// Initialize the Observer controls
		initObserverControls();

		// by default switch to the none context
		switchToContext( ControlBarContext::CB_CONTEXT_NONE, nullptr );
	}

}  // end init

//-------------------------------------------------------------------------------------------------
/** Reset the context sensitive control bar GUI */
//-------------------------------------------------------------------------------------------------
void ControlBar::reset( void )
{
	hideSpecialPowerShortcut();
	// do not destroy the rally drawable, it will get destroyed with everythign else during a reset
	m_rallyPointDrawableID = INVALID_DRAWABLE_ID;
	if(m_radarAttackGlowWindow)
		m_radarAttackGlowWindow->winEnable(TRUE);
	m_radarAttackGlowOn = FALSE;
	m_remainingRadarAttackGlowFrames = 0;

	m_displayedConstructPercent = -1.0f;
	m_displayedOCLTimerSeconds = 0;

	m_isObserverCommandBar = FALSE; // reset us to use a normal command bar
	m_observerLookAtPlayer = nullptr;
	
	if(m_buildToolTipLayout)
		m_buildToolTipLayout->hide(TRUE);
	m_showBuildToolTipLayout = FALSE;

	if(m_animateWindowManager)
		m_animateWindowManager->reset();

	if(m_animateWindowManagerForGenShortcuts)
		m_animateWindowManagerForGenShortcuts->reset();

	if(m_generalsScreenAnimate)
		m_generalsScreenAnimate->reset();


	if(m_videoManager)
		m_videoManager->reset();
	
	// go back to default context
	switchToContext( ControlBarContext::CB_CONTEXT_NONE, nullptr );
	m_sideSelectAnimateDown = FALSE;
	if(m_animateDownWindow)
	{
		TheWindowManager->winDestroy( m_animateDownWindow );
		m_animateDownWindow = nullptr;		
	}

	// Remove any overridden sets.
	CommandSet *set, *nextSet;
	set = m_commandSets;
	while (set) {
		Bool possibleAdjustment = FALSE;
		nextSet = set->friend_getNext();
		if (set == m_commandSets) {
			possibleAdjustment = TRUE;
		}

		Overridable *stillValid = set->deleteOverrides();
		if (stillValid == nullptr && possibleAdjustment) {
			m_commandSets = nextSet;
		}

		set = nextSet;
	}

	// Remove any overridden command buttons.
	CommandButton *button, *nextButton;
	button = m_commandButtons;
	while (button) {
		Bool possibleAdjustment = FALSE;
		nextButton = button->friend_getNext();
		if (button == m_commandButtons) {
			possibleAdjustment = TRUE;
		}

		Overridable *stillValid = button->deleteOverrides();
		if (stillValid == nullptr && possibleAdjustment) {
			m_commandButtons = nextButton;
		}

		button = nextButton;
	}
	if(TheTransitionHandler)
		TheTransitionHandler->remove("ControlBarArrow");
	m_genArrow = nullptr;

	m_lastFlashedAtPointValue = -1;
	m_genStarFlash = TRUE;
}  // end reset

//-------------------------------------------------------------------------------------------------
/** Update phase, we can track if our selected object is destroyed, update button
	* percentages, status, enabled status etc */
//-------------------------------------------------------------------------------------------------
void ControlBar::update( void )
{
	getStarImage();
	updateRadarAttackGlow();
	if(m_controlBarSchemeManager)
		m_controlBarSchemeManager->update();

	// Update our video manager
	if( m_videoManager )
		m_videoManager->update();

	if (m_animateWindowManager)
		m_animateWindowManager->update();

	if (m_animateWindowManager)
		{
			if (m_animateWindowManager->isFinished() && m_animateWindowManager->isReversed())
			{
				Int id = static_cast<Int>(TheNameKeyGenerator->nameToKey(AsciiString("ControlBar.wnd:ControlBarParent")));
				GameWindow *window = TheWindowManager->winGetWindowFromId(nullptr, id);
				if (window && !window->winIsHidden())
					window->winHide(TRUE);
			}
		}

	if(m_animateWindowManagerForGenShortcuts)
		m_animateWindowManagerForGenShortcuts->update();
	if (m_animateWindowManagerForGenShortcuts && m_specialPowerShortcutParent)
		{
			if (m_animateWindowManagerForGenShortcuts->isFinished() && m_animateWindowManagerForGenShortcuts->isReversed())
			{
				if (m_specialPowerShortcutParent && !m_specialPowerShortcutParent->winIsHidden())
					m_specialPowerShortcutParent->winHide(TRUE);
			}
		}



	if( !m_buildToolTipLayout->isHidden())
	{
		m_buildToolTipLayout->runUpdate();
		m_showBuildToolTipLayout = FALSE;
	}
/*
	else if( m_buildToolTipLayout )
	{
		hideBuildTooltipLayout();
	}*/

	updateSpecialPowerShortcut();
	// if we're an observer, don't do the complete update
	if( m_isObserverCommandBar)
	{
		if((TheGameLogic->getFrame() % (LOGICFRAMES_PER_SECOND/2)) == 0)
			populateObserverInfoWindow();

		Drawable *drawToEvaluateFor = nullptr;
		Bool multiSelect = FALSE;
		if( TheInGameUI->getSelectCount() > 1 )
		{
			// Attempt to isolate a Drawable here to evaluate
			// The need arises when selected is an AngryMob,
			// whose selection actually consists of varied units
			// but is represented in the UI as a single unit,
			// so we must isolate and evaluate only the Nexus 
			drawToEvaluateFor = TheGameClient->findDrawableByID( TheInGameUI->getSoloNexusSelectedDrawableID() ) ;
			multiSelect = ( drawToEvaluateFor == nullptr );

		}  
		else if( TheInGameUI->getSelectCount() == 1 ) // get the first and only drawble in the selection list
			drawToEvaluateFor = TheInGameUI->getAllSelectedDrawables()->front();
		Object *obj = drawToEvaluateFor ? drawToEvaluateFor->getObject() : nullptr;
		setPortraitByObject( obj );
		
		return;
	}
		

	// check flashing
	if( m_flash )
	{
		// go through all the command buttons to see which one needs to flash
		for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
		{
			GameWindow *button = m_commandWindows[ i ];
			if( button != nullptr)
			{
				const CommandButton *commandButton = static_cast<const CommandButton *>(GadgetButtonGetData(button));
				if( commandButton != nullptr )
				{
					if( commandButton->getFlashCount() > 0 && TheGameClient->getFrame() % 10 == 0 )
					{
						if( commandButton->getFlashCount() % 2 == 0 )
						{
							commandButton->setFlashCount(commandButton->getFlashCount() - 1);
							button->winSetStatus( WIN_STATUS_FLASHING );
						}
						else
						{
							commandButton->setFlashCount(commandButton->getFlashCount() - 1);
							button->winClearStatus( WIN_STATUS_FLASHING );
							if( commandButton->getFlashCount() == 0 )
							{
								setFlash( FALSE );
							}
						}
					}
				}
			}
		}
	}
	
	if(!m_contextParent[ CP_PURCHASE_SCIENCE ]->winIsHidden())
		updateContextPurchaseScience();

	//
	// first, if the UI is dirty repopulate the UI with what the user should see for all the
	// selected drawables
	//
	if( m_UIDirty )
	{
		evaluateContextUI();
		populateSpecialPowerShortcut(ThePlayerList->getLocalPlayer());
		// if we have a build tooltip layout, update it with the new data.
		repopulateBuildTooltipLayout(); 
	}

	// enable/disable the beacon button depending on if the max has been reached
	if (ThePlayerList && ThePlayerList->getLocalPlayer() && ThePlayerList->getLocalPlayer()->getPlayerTemplate())
	{
		Int count;
		const ThingTemplate *thing = TheThingFactory->findTemplate( ThePlayerList->getLocalPlayer()->getPlayerTemplate()->getBeaconTemplate() );
		ThePlayerList->getLocalPlayer()->countObjectsByThingTemplate( 1, &thing, false, &count );
		static NameKeyType beaconPlacementButtonID = NAMEKEY("ControlBar.wnd:ButtonPlaceBeacon");
		GameWindow *win = TheWindowManager->winGetWindowFromId(nullptr, beaconPlacementButtonID);
		if (win)
		{
			if (count < TheMultiplayerSettings->getMaxBeaconsPerPlayer())
			{
				win->winEnable(TRUE);
			}
			else
			{
				win->winEnable(FALSE);
			}
		}
	}

	//
	// most control bar contexts have one selected thing that we switch on and update
	// based on if that thing changed in some way ... the exception is when multi selected
	//
	if( m_currContext == ControlBarContext::CB_CONTEXT_MULTI_SELECT )
	{

		updateContextMultiSelect();
		return;

	}  // end if

	// if nothing is selected get out of here except if we're in the Purchase science context... that requires
	// us to not have anything selected
	if( m_currentSelectedDrawable == nullptr )
	{

		// we better be in the default none context
		DEBUG_ASSERTCRASH( m_currContext == ControlBarContext::CB_CONTEXT_NONE, ("ControlBar::update no selection, but not we're not showing the default NONE context\n") );
		return;

	}  // end if
	
	

	// if our selected drawable has no object get out of here
	Object *obj = nullptr;
	if(m_currentSelectedDrawable)
		obj = m_currentSelectedDrawable->getObject();
	if( obj == nullptr )
	{

		switchToContext( ControlBarContext::CB_CONTEXT_NONE, nullptr );
		return;

	}  // end if

	switch( m_currContext )
	{

		//---------------------------------------------------------------------------------------------
		case ControlBarContext::CB_CONTEXT_NONE:  
			break;

		//---------------------------------------------------------------------------------------------
		case ControlBarContext::CB_CONTEXT_COMMAND:
			updateContextCommand();
			break;

		//---------------------------------------------------------------------------------------------
		case ControlBarContext::CB_CONTEXT_STRUCTURE_INVENTORY:
			updateContextStructureInventory();
			break;

		//---------------------------------------------------------------------------------------------
		case ControlBarContext::CB_CONTEXT_BEACON:
			updateContextBeacon();
			break;

		//---------------------------------------------------------------------------------------------
		case ControlBarContext::CB_CONTEXT_UNDER_CONSTRUCTION:
			updateContextUnderConstruction();
			break;

		//---------------------------------------------------------------------------------------------
		case ControlBarContext::CB_CONTEXT_OCL_TIMER:
			updateContextOCLTimer();
			break;

	}  // end switch



}  // end update

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::onDrawableSelected( Drawable *draw )
{

	// set a dirty flag so next time we update we can reconstruct the UI
	markUIDirty();

	// cancel any pending GUI commands
	TheInGameUI->setGUICommand( nullptr );


}  // end onDrawableSelected

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::onDrawableDeselected( Drawable *draw )
{

	// set a dirty flag so next time we update we can reconstruct the UI
	markUIDirty();

	if (TheInGameUI->getSelectCount() == 0)
	{
		// we just deselected everything - cancel any pending GUI commands
		TheInGameUI->setGUICommand( nullptr );
	}

	//
	// always when becoming unselected should we remove any build placement icons because if
	// we have some and are in the middle of a build process, it must obiously be over now
	// because we are no longer selecting the dozer or worker
	//
	TheInGameUI->placeBuildAvailable( nullptr, nullptr );

}  // end onDrawableDeselected

//-------------------------------------------------------------------------------------------------

const Image *ControlBar::getStarImage(void )
{
	if(m_lastFlashedAtPointValue > ThePlayerList->getLocalPlayer()->getSciencePurchasePoints() || ThePlayerList->getLocalPlayer()->getSciencePurchasePoints() <= 0)
		m_genStarFlash = FALSE;
	else
		m_lastFlashedAtPointValue = ThePlayerList->getLocalPlayer()->getSciencePurchasePoints();
	
	GameWindow *win= TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ButtonGeneral" ) );
	if(!win)
		return nullptr;
	if(!m_genStarFlash)
	{
		GadgetButtonSetEnabledImage(win, m_generalButtonEnable);
		return nullptr;
	}

	if(TheGameLogic->getFrame()% LOGICFRAMES_PER_SECOND > LOGICFRAMES_PER_SECOND/2)
	{
		GadgetButtonSetEnabledImage(win, m_generalButtonHighlight);
		return nullptr;
	}

	GadgetButtonSetEnabledImage(win, m_generalButtonEnable);

	return nullptr;

}


//-------------------------------------------------------------------------------------------------
/** Given the drawables that we have selected into our context sensitive UI, evaluate 
	* and perform all UI manipulations to make the GUI show to the user what we want them
	* to see */
//-------------------------------------------------------------------------------------------------
void ControlBar::evaluateContextUI( void )
{

	//
	// the UI has been "evaluated" and is now displaying the most current and correct
	// information to the player
	//
	m_UIDirty = FALSE;
	
	// if our purchase science window is up, we will want to update it by repopulating it.
	if(!m_contextParent[ CP_PURCHASE_SCIENCE ]->winIsHidden())
		showPurchaseScience();

	// erase any current state of the GUI by switching out to the empty context
	switchToContext( ControlBarContext::CB_CONTEXT_NONE, nullptr );

	// sanity, nothing selected
	if( TheInGameUI->getSelectCount() == 0 )
		return;

	// get the list of drawable IDs from the in game UI
	const DrawableList *selectedDrawables = TheInGameUI->getAllSelectedDrawables();

	// sanity
	if( selectedDrawables->empty() == TRUE )
		return;

	//Make sure the selected objects are in fact, controllable! If not, then
	//we don't show any GUI commands for them!!!
	//This is used when we select enemy objects or objects on another team.
	//@todo we may want to show their portrait
	if( !TheInGameUI->areSelectedObjectsControllable() )
	{
		//Also make sure the unit isn't a garrisonable neutral civ team building!
		Drawable *draw = selectedDrawables->empty() ? nullptr : selectedDrawables->front();	// the list may be empty

		//sanity 
		if( !draw )
		{
			return;
		}
		Object *obj = draw->getObject();
		if( !obj )
		{
			return;
		}
		
		if (obj->getControllingPlayer()
			&& obj->getControllingPlayer()->getPlayerTemplate()
			&& obj->getControllingPlayer()->getPlayerTemplate()->getBeaconTemplate().compare(obj->getTemplate()->getName()) == 0
			)
		{
			switchToContext( ControlBarContext::CB_CONTEXT_BEACON, draw );
		}
		else
		{
			switchToContext( ControlBarContext::CB_CONTEXT_NONE, draw );
		}

		//Check for a contain interface and a enemy relationship and reject that!
		ContainModuleInterface *contain = obj->getContain();
		if( contain && contain->getContainMax() > 0 )
		{

			const Player *otherPlayer = contain->getApparentControllingPlayer(ThePlayerList->getLocalPlayer());
			if (!otherPlayer)
				otherPlayer = obj->getControllingPlayer();
			Player *player = ThePlayerList->getLocalPlayer();

			if( !player || !otherPlayer )
			{
				//Sanity.
				return;
			}
			Relationship relation = player->getRelationship( otherPlayer->getDefaultTeam() );

			//Note: All following checks already account for the fact that this object
			//isn't ours.

			//The only case we can actually see a non-controlled controlbar is a neutral garrisonable structure.
			if( !contain->isGarrisonable() || relation != NEUTRAL )
			{
				//Can't peek inside enemy/allied containers period!
				return;
			}
		}
		else
		{
			return;
		}
	}

	//
	// when we have multiple things selected, we will only display the common commands
	// in the center command bar that can be displayed with multi-units selected
	//


	Drawable *drawToEvaluateFor = nullptr;
	Bool multiSelect = FALSE;


	if( TheInGameUI->getSelectCount() > 1 )
	{
		// Attempt to isolate a Drawable here to evaluate
		// The need arises when selected is an AngryMob,
		// whose selection actually consists of varied units
		// but is represented in the UI as a single unit,
		// so we must isolate and evaluate only the Nexus 
		drawToEvaluateFor = TheGameClient->findDrawableByID( TheInGameUI->getSoloNexusSelectedDrawableID() ) ;
		multiSelect = ( drawToEvaluateFor == nullptr );

	}  
	else if( !selectedDrawables->empty() ) // get the first and only drawble in the selection list
		drawToEvaluateFor = selectedDrawables->front();	// the list may be empty
	


	if( multiSelect )
	{
		switchToContext( ControlBarContext::CB_CONTEXT_MULTI_SELECT, nullptr );
	}  
	else if ( drawToEvaluateFor )// either we have exactly one drawable, or we have isolated one to evaluate for...
	{

		// get the first and only drawble in the selection list
		//Drawable *draw = selectedDrawables->front();

		// sanity
		//if( draw == NULL )
		//	return;

		// get object
		Object *obj = drawToEvaluateFor->getObject();
		if( obj == nullptr )
			return;

		// we show no interface for objects being sold
		if( obj->getStatusBits().test( OBJECT_STATUS_SOLD ) )
			return;

		static const NameKeyType key_OCLUpdate = NAMEKEY( "OCLUpdate" );
		OCLUpdate *update = static_cast<OCLUpdate*>(obj->findUpdateModule( key_OCLUpdate ));
	
		//
		// a command center is context sensitive itself, if a side has *NOT* been chosen we display
		// the side select interface for command centers only, but note how under construction is
		// more important than anything
		//
		Bool contextSelected = FALSE;
		if( obj->getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		{

			switchToContext( ControlBarContext::CB_CONTEXT_UNDER_CONSTRUCTION, drawToEvaluateFor );
			contextSelected = TRUE;

		}  // end else if

		// check for a regular switch to the appropriate context
		if( contextSelected == FALSE )
		{
			ContainModuleInterface *cmi = obj->getContain();

			if( cmi && cmi->isGarrisonable() && obj->getCommandSetString().isEmpty() )
			{
				//Kris: This is a convenient section to graft an inventory commandset for
				//garrisoned troops. However, we only want to use this if we DON'T have
				//a commandset defined. If we do, then trust that the commandset will
				//handle it!

				Player *localPlayer = ThePlayerList->getLocalPlayer();
				Relationship relationship;

				// we cannot select objects that are controlled by our enemies
				relationship = localPlayer->getRelationship( obj->getTeam() );
				if( obj->isLocallyControlled() == TRUE || relationship == NEUTRAL )
					switchToContext( ControlBarContext::CB_CONTEXT_STRUCTURE_INVENTORY, drawToEvaluateFor );

			}  // end else if
			else if( update )
			{
				switchToContext( ControlBarContext::CB_CONTEXT_OCL_TIMER, drawToEvaluateFor );
			}
			else if( obj->getCommandSetString().isEmpty() == FALSE )
			{

				switchToContext( ControlBarContext::CB_CONTEXT_COMMAND, drawToEvaluateFor );

			}  // end else if
			else if (obj->getControllingPlayer()->getPlayerTemplate()->getBeaconTemplate().compare(obj->getTemplate()->getName()) == 0)
			{
				switchToContext( ControlBarContext::CB_CONTEXT_BEACON, drawToEvaluateFor );
			}
			else 
				switchToContext( ControlBarContext::CB_CONTEXT_NONE, drawToEvaluateFor );
		}  // end else

	}  // end else	

}  // end evaluateContextUI

//-------------------------------------------------------------------------------------------------
/** Find a command button of the given name if present */
//-------------------------------------------------------------------------------------------------
CommandButton *ControlBar::findNonConstCommandButton( const AsciiString& name )
{
	
	for( const CommandButton *command = m_commandButtons; command; command = command->getNext() )
		if( command->getName() == name )
			return const_cast<CommandButton*>(static_cast<const CommandButton*>(command->getFinalOverride()));

	return nullptr;  // not found

}  // end findCommandButton

//-------------------------------------------------------------------------------------------------
/** Allocate a new command button, assign name, and tie to list */
//-------------------------------------------------------------------------------------------------
CommandButton *ControlBar::newCommandButton( const AsciiString& name )
{
	CommandButton *newButton;

	// allocate new button
	newButton = newInstance(CommandButton);

	// assign name
	newButton->setName(name);

	// link to list
	newButton->friend_addToList(&m_commandButtons);

	// return the new button
	return newButton;

}  // end newCommandButton

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CommandButton *ControlBar::newCommandButtonOverride( CommandButton *buttonToOverride )
{
	if (!buttonToOverride) {
		return nullptr;
	}

	CommandButton *newOverride;

	// allocate new button
	newOverride = newInstance(CommandButton);

	*newOverride = *buttonToOverride;

	newOverride->markAsOverride();
	buttonToOverride->setNextOverride(newOverride);

	return newOverride;
}

//-------------------------------------------------------------------------------------------------
/** Parse a command set */
//-------------------------------------------------------------------------------------------------
/*static*/ void ControlBar::parseCommandSetDefinition( INI *ini )
{
	AsciiString name;
	CommandSet *commandSet;

	// read the name
	const char* c = ini->getNextToken();
	name.set( c );	

	// find existing item if present
	commandSet = TheControlBar->findNonConstCommandSet( name );
	if( commandSet == nullptr )
	{

		// allocate a new item
		commandSet = TheControlBar->newCommandSet( name );
		if (ini->getLoadType() == INILoadType::INI_LOAD_CREATE_OVERRIDES) {
			commandSet->markAsOverride();
		}
	}  // end if
	else if( ini->getLoadType() != INILoadType::INI_LOAD_CREATE_OVERRIDES )
	{
		//Holy crap, this sucks to debug!!!
		//If you have two different command sets, the previous
		//code would simply allow you to define multiple command set
		//with the same name, and just nuke the old button with the new one.
		//So, I (KM) have added this assert to notify in case of two same-name
		//command set.
		DEBUG_CRASH(( "[LINE: %d in '%s'] Duplicate commandset %s found!", ini->getLineNum(), ini->getFilename().str(), name.str() ));
		throw INI_INVALID_DATA;

		//@todo SUPPORT OVERRIDES -- JM
	} else {
		commandSet = TheControlBar->newCommandSetOverride(commandSet);
	}

	// sanity
	DEBUG_ASSERTCRASH( commandSet, ("parseCommandSetDefinition: Unable to allocate set '%s'\n", name.str()) );

	// parse the ini definition
	ini->initFromINI( commandSet, commandSet->friend_getFieldParse() );

}  // end parseCommandSetDefinition

//-------------------------------------------------------------------------------------------------
/** Find existing command set by name */
//-------------------------------------------------------------------------------------------------
CommandSet* ControlBar::findNonConstCommandSet( const AsciiString& name )
{
	CommandSet* set;

	for( set = m_commandSets; set != nullptr; set = set->friend_getNext() )
		if( set->getName() == name )
			return const_cast<CommandSet*>(static_cast<const CommandSet *>(set));

	return nullptr;  // set not found

}
//-------------------------------------------------------------------------------------------------
/** find existing command button if present	*/
//-------------------------------------------------------------------------------------------------
const CommandButton *ControlBar::findCommandButton( const AsciiString& name ) 
{ 
	CommandButton *btn =  findNonConstCommandButton(name); 
	if( btn )
	{
		btn = static_cast<CommandButton *>(btn->friend_getFinalOverride());
	}
	return btn; 
}

//-------------------------------------------------------------------------------------------------
/** Find existing command set by name */
//-------------------------------------------------------------------------------------------------
const CommandSet *ControlBar::findCommandSet( const AsciiString& name ) 
{ 
	CommandSet *set = findNonConstCommandSet(name); 
	if (set)
		set = static_cast<CommandSet*>(set->friend_getFinalOverride());
	return set; 
}

//-------------------------------------------------------------------------------------------------
/** Allocate a new command set, link to list, initialize to default, and return it */
//-------------------------------------------------------------------------------------------------
CommandSet *ControlBar::newCommandSet( const AsciiString& name )
{
	// allocate a new set
	CommandSet* set = newInstance(CommandSet)(name);
	// add it to the list.
	set->friend_addToList(&m_commandSets);
	// return the newly created set
	return set;

}  // end newCommandSet

//-------------------------------------------------------------------------------------------------
/** Create an overridden command set. */
//-------------------------------------------------------------------------------------------------
CommandSet *ControlBar::newCommandSetOverride( CommandSet *setToOverride )
{
	if (!setToOverride) {
		return nullptr;
	}

	// allocate a new set
	CommandSet* set = newInstance(CommandSet)(setToOverride->getName());

	// it's an override; DON'T add it to the main list.
	// !!! DO NOT DO THIS !!! -- > set->friend_addToList(&m_commandSets); <-- !!! DO NOT DO THIS !!!

	*set = *setToOverride;
	set->markAsOverride();

	setToOverride->setNextOverride(set);

	return set;
}

//-------------------------------------------------------------------------------------------------
/** Process a button click for the context sensitive GUI */
//-------------------------------------------------------------------------------------------------
CBCommandStatus ControlBar::processContextSensitiveButtonClick( GameWindow *button, 
																																GadgetGameMessage gadgetMessage )
{

	// call command processing method
	return processCommandUI( button, gadgetMessage );

}  // end processContextSensitiveButtonClick

//-------------------------------------------------------------------------------------------------
/** Process a button click for the context sensitive GUI */
//-------------------------------------------------------------------------------------------------
CBCommandStatus ControlBar::processContextSensitiveButtonTransition( GameWindow *button, 
																																GadgetGameMessage gadgetMessage )
{

	// call command processing method
	return processCommandTransitionUI( button, gadgetMessage );

}  // end processContextSensitiveButtonClick


//-------------------------------------------------------------------------------------------------
/** Switch the user interface to the new context specified and fill out any of the
	* art and/or buttons that we need to for the new context using data from the object
	* passed in */
//-------------------------------------------------------------------------------------------------
void ControlBar::switchToContext( ControlBarContext context, Drawable *draw )
{

	// restore the right hud to a plain window
	setPortraitByObject( nullptr );

	Object *obj = draw ? draw->getObject() : nullptr;
	setPortraitByObject( obj );

	// if we're switching context, we have to repopulate the hotkey manager
	if(TheHotKeyManager)
		TheHotKeyManager->reset();

	// ... and also remove any radius cursor that is active.
	TheInGameUI->setRadiusCursorNone();

	// save a pointer for the currently selected drawable
	m_currentSelectedDrawable = draw;

	if (IsInGameChatActive() == FALSE && TheGameLogic && !TheGameLogic->isInShellGame()) {
		TheWindowManager->winSetFocus( nullptr );
	}

	// hide/un-hide the appropriate windows for the context
	switch( context )
	{

		//-------------------------------------------------------------------------------------------------
		case ControlBarContext::CB_CONTEXT_NONE:
		{

			// show or hide the right window groups
			//m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
			m_contextParent[ CP_COMMAND ]->winHide( TRUE );
			m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );
			m_contextParent[ CP_BEACON ]->winHide( TRUE );
			m_contextParent[ CP_UNDER_CONSTRUCTION ]->winHide( TRUE );
			m_contextParent[ CP_OCL_TIMER ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_INFO ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_LIST ]->winHide( TRUE );

			//Clear any potentially flashing buttons!
			for( int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
			{
				// the implementation won't necessarily use the max number of windows possible
				if (m_commandWindows[ i ]) 
				{
					m_commandWindows[ i ]->winClearStatus( WIN_STATUS_FLASHING );
				}
			}
			// if there is a current selected drawable then we wil display a selection portrait if present
			if( draw )
			{
				//Get the current thing template.
				const ThingTemplate *thing = draw->getTemplate();

				//Special case -- if we are a GLA hole, then get the rebuild building template
				Object *obj3 = draw->getObject();
				if( obj3 && obj3->isKindOf( KINDOF_REBUILD_HOLE ) )
				{
					RebuildHoleBehaviorInterface *rhbi = RebuildHoleBehavior::getRebuildHoleBehaviorInterfaceFromObject( obj3 );
					if( rhbi )
					{
						thing = rhbi->getRebuildTemplate();
					}
				}

				//Set the correct portrait.
				setPortraitByObject( obj3 );
			}

			// do not show any rally point marker
			showRallyPoint( nullptr );
			
			break;

		}  // end none

		//---------------------------------------------------------------------------------------------
		case ControlBarContext::CB_CONTEXT_COMMAND:
		{

			// show or hide the right window groups
			//m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
			m_contextParent[ CP_COMMAND ]->winHide( FALSE );
			m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );
			m_contextParent[ CP_BEACON ]->winHide( TRUE );
			m_contextParent[ CP_UNDER_CONSTRUCTION ]->winHide( TRUE );
			m_contextParent[ CP_OCL_TIMER ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_INFO ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_LIST ]->winHide( TRUE );
			
			// fill the specific UI info
			populateCommand( draw->getObject() );

			//
			// for objects that are able to create units, we show a build queue if we actually
			// actually have something in production, otherwise we show the selection portrait
			//
			if( obj )
			{
				ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();

				if( pu && pu->firstProduction() != nullptr )
				{

					m_contextParent[ CP_BUILD_QUEUE ]->winHide( FALSE );
					populateBuildQueue( obj );
					setPortraitByObject( nullptr );
				}  // end if
				else 
				{
					setPortraitByObject( obj );
				}

			}  // end if

			break;

		}  // end command

		//---------------------------------------------------------------------------------------------
		case ControlBarContext::CB_CONTEXT_STRUCTURE_INVENTORY:
		{

			// show or hide the right window groups
			//m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
			m_contextParent[ CP_COMMAND ]->winHide( FALSE );
			m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );
			m_contextParent[ CP_BEACON ]->winHide( TRUE );
			m_contextParent[ CP_UNDER_CONSTRUCTION ]->winHide( TRUE );
			m_contextParent[ CP_OCL_TIMER ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_INFO ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_LIST ]->winHide( TRUE );

			// fill the specific UI info
			populateStructureInventory( draw->getObject() );

			break;

		}  // end inventory

		//---------------------------------------------------------------------------------------------
		case ControlBarContext::CB_CONTEXT_BEACON:
		{

			// show or hide the right window groups
			//m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
			m_contextParent[ CP_COMMAND ]->winHide( TRUE );
			m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );
			m_contextParent[ CP_BEACON ]->winHide( FALSE );
			m_contextParent[ CP_UNDER_CONSTRUCTION ]->winHide( TRUE );
			m_contextParent[ CP_OCL_TIMER ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_INFO ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_LIST ]->winHide( TRUE );

			
			// fill the specific UI info			
			populateBeacon( draw->getObject() );

			break;

		}  // end beacon

		//---------------------------------------------------------------------------------------------
		case ControlBarContext::CB_CONTEXT_UNDER_CONSTRUCTION:
		{

			// show or hide the right window groups
			//m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
			m_contextParent[ CP_COMMAND ]->winHide( TRUE );
			m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );
			m_contextParent[ CP_BEACON ]->winHide( TRUE );
			m_contextParent[ CP_UNDER_CONSTRUCTION ]->winHide( FALSE );
			m_contextParent[ CP_OCL_TIMER ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_INFO ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_LIST ]->winHide( TRUE );

			// fill the specific UI info
			populateUnderConstruction( draw->getObject() );

			break;

		}  // end under construction

		//---------------------------------------------------------------------------------------------
		case ControlBarContext::CB_CONTEXT_OCL_TIMER:
		{

			// show or hide the right window groups
//			m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
			m_contextParent[ CP_COMMAND ]->winHide( TRUE );
			m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );
			m_contextParent[ CP_BEACON ]->winHide( TRUE );
			m_contextParent[ CP_UNDER_CONSTRUCTION ]->winHide( TRUE );
			m_contextParent[ CP_OCL_TIMER ]->winHide( FALSE );
			m_contextParent[ CP_OBSERVER_INFO ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_LIST ]->winHide( TRUE );

			// fill the specific UI info
			populateOCLTimer( draw->getObject() );

			break;

		}  // end under construction

		//---------------------------------------------------------------------------------------------
		case ControlBarContext::CB_CONTEXT_MULTI_SELECT:
		{

			// show or hide the right window groups
//			m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
			m_contextParent[ CP_COMMAND ]->winHide( FALSE );		// multi select shows common commands
			m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );
			m_contextParent[ CP_BEACON ]->winHide( TRUE );
			m_contextParent[ CP_UNDER_CONSTRUCTION ]->winHide( TRUE );
			m_contextParent[ CP_OCL_TIMER ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_INFO ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_LIST ]->winHide( TRUE );


			// fill the specific UI info
			populateMultiSelect();

			break;

		}  // end multi select
		case ControlBarContext::CB_CONTEXT_OBSERVER_LIST:
		{

			// show or hide the right window groups
//			m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
			m_contextParent[ CP_COMMAND ]->winHide( TRUE );
			m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );
			m_contextParent[ CP_BEACON ]->winHide( TRUE );
			m_contextParent[ CP_UNDER_CONSTRUCTION ]->winHide( TRUE );
			m_contextParent[ CP_OCL_TIMER ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_INFO ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_LIST ]->winHide( FALSE );


			// fill the specific UI info
			populateObserverList();
			break;

		}  // end multi select

		//---------------------------------------------------------------------------------------------
		default:
		{

			DEBUG_ASSERTCRASH( 0, ("ControlBar::switchToContext, unknown context '%d'\n", static_cast<Int>(context)) );
			break;

		}  // end default

	}  // end switch

	// save our context
	m_currContext = context;

}  // end switchToContext

void ControlBar::setCommandBarBorder( GameWindow *button, CommandButtonMappedBorderType type)
{
	if(!button)
		return;

	switch( type )
	{
		case COMMAND_BUTTON_BORDER_BUILD:
		{
			GadgetButtonSetBorder(button, m_commandButtonBorderBuildColor);
			break;
		}
		//-------------------------------------------------------------------------------------------------
		case COMMAND_BUTTON_BORDER_UPGRADE:
		{
			GadgetButtonSetBorder(button, m_commandButtonBorderUpgradeColor );
			break;
		}
		//-------------------------------------------------------------------------------------------------
		case COMMAND_BUTTON_BORDER_ACTION:
		{
			GadgetButtonSetBorder(button, m_commandButtonBorderActionColor);
			break;
		}
		//-------------------------------------------------------------------------------------------------
		case COMMAND_BUTTON_BORDER_SYSTEM:
		{
			GadgetButtonSetBorder(button, m_commandButtonBorderSystemColor);
			break;
		}

		//-------------------------------------------------------------------------------------------------
		case COMMAND_BUTTON_BORDER_NONE:
		default:
			GadgetButtonSetBorder(button, GAME_COLOR_UNDEFINED, FALSE);
	}
}


//-------------------------------------------------------------------------------------------------
/** Set the command data into the control */
//-------------------------------------------------------------------------------------------------
void ControlBar::setControlCommand( GameWindow *button, const CommandButton *commandButton )
{

	// the window must be a gadget button
	if( button->winGetInputFunc() != GadgetPushButtonInput )
	{

		DEBUG_ASSERTCRASH( 0, ("setControlCommand: Window is not a button\n") );
		return;

	}  // end if

	// sanity
	if( commandButton == nullptr )
	{

		DEBUG_ASSERTCRASH( 0, ("setControlCommand: NULL commandButton passed in\n") );
		return;

	}  // end if

	//
	// set the button gadget control to be a normal button or a check like button if
	// the command says it needs one
	//
	if( BitTest( commandButton->getOptions(), CHECK_LIKE ))
		GadgetButtonEnableCheckLike( button, TRUE, FALSE );
	else
		GadgetButtonEnableCheckLike( button, FALSE, FALSE );

	//
	// set the imagry ... note that for 99% of the command buttons it's sufficient to specify
	// only the disabled, enabled, hilite, and hilite pushed images.  For push-like buttons
	// we actually utilize all the state available to a GameWindow.  We will replicate the
	// hilite pushed image to be the enabled pushed image ... and we will also replicate
	// the disabled image to be the disabled pushed image.  For complete control over all
	// the states of these buttons we would add additional lines to the INI for a command
	// button and store those additional images in the command button 
	//
	if( commandButton->getButtonImage() )
		GadgetButtonSetEnabledImage( button, commandButton->getButtonImage() );

	//if( commandButton->getDisabledImage() )
	//{
	//	GadgetButtonSetDisabledImage( button, commandButton->getDisabledImage() );
	//	GadgetButtonSetDisabledSelectedImage( button, commandButton->getDisabledImage() );
	//}  //end if
	//if( commandButton->getHiliteImage() )
	//	GadgetButtonSetHiliteImage( button, commandButton->getHiliteImage() );
	//if( commandButton->getPushedImage() )
	//{
	//	GadgetButtonSetHiliteSelectedImage( button, commandButton->getPushedImage() );
	//	GadgetButtonSetEnabledSelectedImage( button, commandButton->getPushedImage() );
	//}  // end if

	// set the text
	if( commandButton->getTextLabel().isEmpty() == FALSE || !commandButton->getScienceVec().empty()) 
	{
		button->winSetTooltipFunc(commandButtonTooltip);
	}
	else
		GadgetButtonSetText( button, UnicodeString( L"" ) );

	// save the command in the user data of the window
	GadgetButtonSetData(button, (void*)commandButton);
	//button->winSetUserData( commandButton );
	
	setCommandBarBorder(button, commandButton->getCommandButtonMappedBorderType());
	
	if (TheHotKeyManager)
	{
		AsciiString hotKey =	TheHotKeyManager->searchHotKey(commandButton->getTextLabel());
		if(hotKey.isNotEmpty())
			TheHotKeyManager->addHotKey(button, hotKey);
	}
	GadgetButtonSetAltSound(button, "GUICommandBarClick");

}  // end setControlCommand

//-------------------------------------------------------------------------------------------------
/** post process step, after all commands and command sets are loaded */
//-------------------------------------------------------------------------------------------------
void ControlBar::postProcessCommands( void )
{
	for ( CommandButton *button = m_commandButtons; button; button = button->friend_getNext() ) 
	{
		button->cacheButtonImage();
	}
}

//-------------------------------------------------------------------------------------------------
/** set the command for the button identified by the window name
	* NOTE that parent may be NULL, it only helps to speed up the search for a particular
	* window ID */
//-------------------------------------------------------------------------------------------------
void ControlBar::setControlCommand( const AsciiString& buttonWindowName, GameWindow *parent,
																		const CommandButton *commandButton )
{
	UnsignedInt winID = TheNameKeyGenerator->nameToKey( buttonWindowName );
	GameWindow *win = TheWindowManager->winGetWindowFromId( parent, winID );

	if( win == nullptr )
	{

		DEBUG_ASSERTCRASH( 0, ("setControlCommand: Unable to find window '%s'\n", buttonWindowName.str()) );
		return;

	}  // end if

	// call the workhorse
	setControlCommand( win, commandButton );

}  // end setControlCommand

//-------------------------------------------------------------------------------------------------
/** show/hide the portrait window image */
//-------------------------------------------------------------------------------------------------
void ControlBar::setPortraitByImage( const Image *image )
{

	if( image )
	{
		m_rightHUDUnitSelectParent->winHide(FALSE);
		m_rightHUDCameoWindow->winSetEnabledImage( 0, image );
		//m_rightHUDWindow->winSetEnabledImage( 0, image );
		m_rightHUDWindow->winClearStatus( WIN_STATUS_IMAGE );
		m_rightHUDCameoWindow->winSetStatus( WIN_STATUS_IMAGE );
		for(Int i = 0; i < MAX_UPGRADE_CAMEO_UPGRADES; ++i)
			m_rightHUDUpgradeCameos[i]->winHide(TRUE);

	}  // end if
	else
	{
		m_rightHUDWindow->winSetStatus( WIN_STATUS_IMAGE );
		m_rightHUDCameoWindow->winClearStatus( WIN_STATUS_IMAGE );
		m_rightHUDUnitSelectParent->winHide(TRUE);
		for(Int i = 0; i < MAX_UPGRADE_CAMEO_UPGRADES; ++i)
			m_rightHUDUpgradeCameos[i]->winHide(TRUE);
		//m_rightHUDWindow->winSetEnabledImage( 0, image );
		//m_rightHUDWindow->winSetStatus( WIN_STATUS_IMAGE );

	}

}  // end setPortraitByImage

//-------------------------------------------------------------------------------------------------
/** show/hide the portrait image by object.  We like to use this method as opposed to the
	* plain image one above so that we can build more intelligence into what portrait to
	* show for an object given its current state or object type */
//-------------------------------------------------------------------------------------------------
void ControlBar::setPortraitByObject( Object *obj )
{

	if( obj )
	{
		if( obj->isKindOf( KINDOF_SHOW_PORTRAIT_WHEN_CONTROLLED ) && !obj->isLocallyControlled() )
		{
			//Handles civ vehicles without terrorists in them
			setPortraitByObject( nullptr );
			return;
		}

		const ThingTemplate *thing = obj->getTemplate();
		Player *player = obj->getControllingPlayer();
		
		//If we have an enemy stealth disguised unit, swap portraits!
		Drawable *draw = obj->getDrawable();
		if( draw && draw->getStealthLook() == STEALTHLOOK_DISGUISED_ENEMY )
		{
			thing = draw->getTemplate();
			if( thing->isKindOf( KINDOF_SHOW_PORTRAIT_WHEN_CONTROLLED ) )
			{
				//If a bomb truck disguises as a civ vehicle, don't use it's portrait (or else you'll see the terrorist).
				setPortraitByObject( nullptr );
				return;
			}
      StealthUpdate *stealth = obj->getStealth();
			if( stealth && stealth->isDisguised() )
			{
				//Fake player upgrades too!
				player = ThePlayerList->getNthPlayer( stealth->getDisguisedPlayerIndex() );
			}
		}
		
		const Image* portrait = thing->getSelectedPortraitImage();

		m_rightHUDUnitSelectParent->winHide(FALSE);
		// enable the window window as an image window and set the image
		m_rightHUDCameoWindow->winSetEnabledImage( 0, portrait );

		//Display the veterancy rank of the object on the portrait.
		const Image *image = calculateVeterancyOverlayForObject( obj );
		GadgetButtonDrawOverlayImage( m_rightHUDCameoWindow, image );

		//m_rightHUDWindow->winSetEnabledImage( 0, portrait );
		m_rightHUDWindow->winClearStatus( WIN_STATUS_IMAGE );
		m_rightHUDCameoWindow->winSetStatus( WIN_STATUS_IMAGE );

		for(Int i = 0; i < MAX_UPGRADE_CAMEO_UPGRADES; ++i)
		{
			AsciiString upgradeName = thing->getUpgradeCameoName(i);
			if(upgradeName.isEmpty())
			{
				m_rightHUDUpgradeCameos[i]->winHide(TRUE);
				continue;
			}
			const UpgradeTemplate *ut =  TheUpgradeCenter->findUpgrade(upgradeName);
			if(!ut)
			{
				m_rightHUDUpgradeCameos[i]->winHide(TRUE);
				continue;
			}

			m_rightHUDUpgradeCameos[i]->winHide(FALSE);
			m_rightHUDUpgradeCameos[i]->winSetEnabledImage( 0, ut->getButtonImage() );
			if( obj->hasUpgrade(ut) )
			{
				//Object level upgrades
				m_rightHUDUpgradeCameos[i]->winEnable( TRUE );
			}
			else if( player && player->hasUpgradeComplete( ut ) )
			{
				//Player level upgrades
				m_rightHUDUpgradeCameos[i]->winEnable( TRUE );
			}
			else
			{
				//Failure
				m_rightHUDUpgradeCameos[i]->winEnable( FALSE );
			}
		}
	

	}  // end if
	else
	{
		m_rightHUDUnitSelectParent->winHide(TRUE);
		m_rightHUDWindow->winSetStatus( WIN_STATUS_IMAGE );
		m_rightHUDCameoWindow->winClearStatus( WIN_STATUS_IMAGE );
		for(Int i = 0; i < MAX_UPGRADE_CAMEO_UPGRADES; ++i)
			m_rightHUDUpgradeCameos[i]->winHide(TRUE);

		//Clear any overlay the portrait had on it.
		GadgetButtonDrawOverlayImage( m_rightHUDCameoWindow, nullptr );
	}

}  // end setPortraitByObject

// ------------------------------------------------------------------------------------------------
/** Show a rally point marker at the world location specified.  If no location is specified
	* any marker that we might have visible is hidden */
// ------------------------------------------------------------------------------------------------
void ControlBar::showRallyPoint( const Coord3D *loc )
{

	// if loc is NULL, destroy any rally point drawble we have shown
	if( loc == nullptr )
	{

		// destroy rally point drawable if present
		if( m_rallyPointDrawableID != INVALID_DRAWABLE_ID )
			TheGameClient->destroyDrawable( TheGameClient->findDrawableByID( m_rallyPointDrawableID ) );
		m_rallyPointDrawableID = INVALID_DRAWABLE_ID;

	}  // end if
	else
	{
		Drawable *marker = nullptr;

		// create a rally point drawble if necessary
		if( m_rallyPointDrawableID == INVALID_DRAWABLE_ID )
		{

			const ThingTemplate* ttn = TheThingFactory->findTemplate("RallyPointMarker");
			marker = TheThingFactory->newDrawable( ttn );
			DEBUG_ASSERTCRASH( marker, ("showRallyPoint: Unable to create rally point drawable\n") );
			if (marker)
			{
				marker->setDrawableStatus(DRAWABLE_STATUS_NO_SAVE);
				m_rallyPointDrawableID = marker->getID();
			}

		}  // end if
		else
			marker = TheGameClient->findDrawableByID( m_rallyPointDrawableID );

		// sanity
		DEBUG_ASSERTCRASH( marker, ("showRallyPoint: No rally point marker found\n" ) );

		// set the position of the rally point drawble to the position passed in
		marker->setPosition( loc );
		marker->setOrientation( TheGlobalData->m_downwindAngle );//To blow down wind -- ML

		// set the marker colors to that of the local player
		Player *player = ThePlayerList->getLocalPlayer();

		if (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT)
			marker->setIndicatorColor( player->getPlayerNightColor() );
		else
			marker->setIndicatorColor( player->getPlayerColor() );

	}  // end else

}  // end showRallyPoint

// ------------------------------------------------------------------------------------------------
/** Show a rally point marker at the world location specified.  If no location is specified
	* any marker that we might have visible is hidden */
// ------------------------------------------------------------------------------------------------
void ControlBar::setControlBarSchemeByPlayer(Player *p)
{
	if(m_controlBarSchemeManager)
		m_controlBarSchemeManager->setControlBarSchemeByPlayer(p);

	static NameKeyType buttonPlaceBeaconID = NAMEKEY( "ControlBar.wnd:ButtonPlaceBeacon" );
	static NameKeyType buttonIdleWorkerID = NAMEKEY("ControlBar.wnd:ButtonIdleWorker");
	static NameKeyType buttonGeneralID = NAMEKEY("ControlBar.wnd:ButtonGeneral");
	GameWindow *buttonPlaceBeacon = TheWindowManager->winGetWindowFromId( nullptr, buttonPlaceBeaconID );
	GameWindow *buttonIdleWorker = TheWindowManager->winGetWindowFromId( nullptr, buttonIdleWorkerID );
	GameWindow *buttonGeneral = TheWindowManager->winGetWindowFromId( nullptr, buttonGeneralID );

	if( !p->isPlayerActive() )
	{
		m_isObserverCommandBar = TRUE;
		switchToContext( ControlBarContext::CB_CONTEXT_OBSERVER_LIST, nullptr );
		DEBUG_LOG(("We're loading the Observer Command Bar\n"));

		if (buttonPlaceBeacon)
			buttonPlaceBeacon->winHide(TRUE);
		if (buttonIdleWorker)
			buttonIdleWorker->winHide(TRUE);
		if (buttonGeneral)
			buttonGeneral->winEnable(FALSE);
	}
	else
	{
		switchToContext( ControlBarContext::CB_CONTEXT_NONE, nullptr );
		m_isObserverCommandBar = FALSE;

		if (buttonPlaceBeacon)
			buttonPlaceBeacon->winHide(
			(TheGameLogic->getGameMode() != GAME_LAN && TheGameLogic->getGameMode() != GAME_INTERNET) ||
			!TheGameInfo->isMultiPlayer());
		if (buttonIdleWorker)
			buttonIdleWorker->winHide(FALSE);
		if (buttonGeneral)
		{
			buttonGeneral->winHide(FALSE);
			buttonGeneral->winEnable(TRUE);
		}
	}
	switchControlBarStage(ControlBarStages::CONTROL_BAR_STAGE_DEFAULT);
}

void ControlBar::setControlBarSchemeByPlayerTemplate( const PlayerTemplate *pt)
{
	if(m_controlBarSchemeManager)
		m_controlBarSchemeManager->setControlBarSchemeByPlayerTemplate(pt);

	static NameKeyType buttonPlaceBeaconID = NAMEKEY( "ControlBar.wnd:ButtonPlaceBeacon" );
	static NameKeyType buttonIdleWorkerID = NAMEKEY("ControlBar.wnd:ButtonIdleWorker");
	static NameKeyType buttonGeneralID = NAMEKEY("ControlBar.wnd:ButtonGeneral");
	GameWindow *buttonPlaceBeacon = TheWindowManager->winGetWindowFromId( nullptr, buttonPlaceBeaconID );
	GameWindow *buttonIdleWorker = TheWindowManager->winGetWindowFromId( nullptr, buttonIdleWorkerID );
	GameWindow *buttonGeneral = TheWindowManager->winGetWindowFromId( nullptr, buttonGeneralID );

	if(pt == ThePlayerTemplateStore->findPlayerTemplate(TheNameKeyGenerator->nameToKey("FactionObserver")))
	{
		m_isObserverCommandBar = TRUE;
		switchToContext( ControlBarContext::CB_CONTEXT_OBSERVER_LIST, nullptr );
		DEBUG_LOG(("We're loading the Observer Command Bar\n"));

		if (buttonPlaceBeacon)
			buttonPlaceBeacon->winHide(TRUE);
		if (buttonIdleWorker)
			buttonIdleWorker->winHide(TRUE);
		if (buttonGeneral)
			buttonGeneral->winEnable(FALSE);
	}
	else
	{
		switchToContext( ControlBarContext::CB_CONTEXT_NONE, nullptr );
		m_isObserverCommandBar = FALSE;

		if (buttonPlaceBeacon)
			buttonPlaceBeacon->winHide(
			(TheGameLogic->getGameMode() != GAME_LAN && TheGameLogic->getGameMode() != GAME_INTERNET) ||
			!TheGameInfo->isMultiPlayer());
		if (buttonIdleWorker)
			buttonIdleWorker->winHide(FALSE);
		if (buttonGeneral)
		{
			buttonGeneral->winHide(FALSE);
			buttonGeneral->winEnable(TRUE);
		}
	}
	switchControlBarStage(ControlBarStages::CONTROL_BAR_STAGE_DEFAULT);

	hidePurchaseScience();
}

void ControlBar::setControlBarSchemeByName(const AsciiString& name)
{
	if(m_controlBarSchemeManager)
		m_controlBarSchemeManager->setControlBarScheme( name );
	switchControlBarStage(ControlBarStages::CONTROL_BAR_STAGE_DEFAULT);

}

void ControlBar::preloadAssets( TimeOfDay timeOfDay )
{
	if (m_controlBarSchemeManager)
		m_controlBarSchemeManager->preloadAssets( timeOfDay );
}

void ControlBar::updateBuildQueueDisabledImages( const Image *image )
{
	if(!image)
		return;
	// We have to do this because the build queue data might have been reset
	static NameKeyType buildQueueIDs[ MAX_BUILD_QUEUE_BUTTONS ];
	static Bool idsInitialized = FALSE;
	Int i;

	// get name key ids for the build queue buttons
	if( idsInitialized == FALSE )
	{
		AsciiString buttonName;

		for( i = 0; i < MAX_BUILD_QUEUE_BUTTONS; i++ )
		{
			
			buttonName.format( "ControlBar.wnd:ButtonQueue%02d", i + 1 );
			buildQueueIDs[ i ] = TheNameKeyGenerator->nameToKey( buttonName );

		}  // end for i

		idsInitialized = TRUE;

	}  // end if

	// get window pointers to all the buttons for the build queue
	for( i = 0; i < MAX_BUILD_QUEUE_BUTTONS; i++ )
	{

		// get window commented out cause I believe we already set this.  We'll see in a few minutes
		m_queueData[ i ].control = TheWindowManager->winGetWindowFromId( m_contextParent[ CP_BUILD_QUEUE ],
																																		 buildQueueIDs[ i ] );

		GadgetButtonSetDisabledImage( m_queueData[ i ].control, image );

	}  // end for i

}

void ControlBar::updateRightHUDImage( const Image *image )
{
	if(!m_rightHUDWindow || !image)
		return;
	m_rightHUDWindow->winSetEnabledImage(0, image);

}

void ControlBar::updateBuildUpClockColor( Color color)
{
	m_buildUpClockColor = color;
}



void ControlBar::updateCommanBarBorderColors(Color build, Color action, Color upgrade, Color system )
{
	m_commandButtonBorderBuildColor = build;
	m_commandButtonBorderActionColor = action;
	m_commandButtonBorderUpgradeColor = upgrade;
	m_commandButtonBorderSystemColor = system;
}

// ---------------------------------------------------------------------------------------
// hides the communicator button
void ControlBar::hideCommunicator( Bool b )
{
	//sanity
	if( m_communicatorButton != nullptr )
		m_communicatorButton->winHide( b );
}

void ControlBar::toggleControlBarStage( void )
{
	if(m_currentControlBarStage == ControlBarStages::CONTROL_BAR_STAGE_DEFAULT )
		switchControlBarStage(ControlBarStages::CONTROL_BAR_STAGE_LOW);
	else
		switchControlBarStage(ControlBarStages::CONTROL_BAR_STAGE_DEFAULT);
}

// Functions for repositioning/resizing the control bar
void ControlBar::switchControlBarStage( ControlBarStages stage )
{
	if(stage < ControlBarStages::CONTROL_BAR_STAGE_DEFAULT || stage >= ControlBarStages::MAX_CONTROL_BAR_STAGES)
		return;
	if (TheRecorder && TheRecorder->getMode() == RecorderModeType::RECORDERMODETYPE_PLAYBACK)
		return;
	switch (stage) {
	case ControlBarStages::CONTROL_BAR_STAGE_DEFAULT:
		setDefaultControlBarConfig();
		break;
//	case CONTROL_BAR_STAGE_SQUISHED:
//		setSquishedControlBarConfig();
//		break;
	case ControlBarStages::CONTROL_BAR_STAGE_LOW:
		setLowControlBarConfig();
		break;
	case ControlBarStages::CONTROL_BAR_STAGE_HIDDEN:
		setHiddenControlBar();
		break;
	default:
		DEBUG_ASSERTCRASH(FALSE,("ControlBar::switchControlBarStage we were passed in a stage that's not supported %d", static_cast<Int>(stage)));
	}
	
}
void ControlBar::setDefaultControlBarConfig( void )
{
//	if(m_currentControlBarStage == CONTROL_BAR_STAGE_SQUISHED)
//	{
//		m_controlBarResizer->sizeWindowsDefault();
//		m_controlBarSchemeManager->setControlBarSchemeByPlayerTemplate(ThePlayerList->getLocalPlayer()->getPlayerTemplate(), FALSE);
//	}
	m_currentControlBarStage = ControlBarStages::CONTROL_BAR_STAGE_DEFAULT;
	TheTacticalView->setHeight(static_cast<Int>(TheDisplay->getHeight() * 0.80f)); 
	m_contextParent[ CP_MASTER ]->winSetPosition(m_defaultControlBarPosition.x, m_defaultControlBarPosition.y);
	m_contextParent[ CP_MASTER ]->winHide(FALSE);
	repopulateBuildTooltipLayout();
	setUpDownImages();

}

void ControlBar::setSquishedControlBarConfig( void )
{
	if(m_currentControlBarStage == ControlBarStages::CONTROL_BAR_STAGE_SQUISHED)
		return;
	m_currentControlBarStage = ControlBarStages::CONTROL_BAR_STAGE_SQUISHED;
	m_contextParent[ CP_MASTER ]->winSetPosition(m_defaultControlBarPosition.x, m_defaultControlBarPosition.y);
	
//	m_controlBarResizer->sizeWindowsAlt();
	repopulateBuildTooltipLayout();	
	TheTacticalView->setHeight(static_cast<Int>(TheDisplay->getHeight())); 
	m_controlBarSchemeManager->setControlBarSchemeByPlayerTemplate(ThePlayerList->getLocalPlayer()->getPlayerTemplate(), TRUE);
}

void ControlBar::setLowControlBarConfig( void )
{
//	if(m_currentControlBarStage == CONTROL_BAR_STAGE_SQUISHED)
//	{
//		m_controlBarResizer->sizeWindowsDefault();
//		m_controlBarSchemeManager->setControlBarSchemeByPlayerTemplate(ThePlayerList->getLocalPlayer()->getPlayerTemplate(), FALSE);
//	}
	
	m_currentControlBarStage = ControlBarStages::CONTROL_BAR_STAGE_LOW;
	ICoord2D pos;
	pos.x = m_defaultControlBarPosition.x;
	pos.y = static_cast<Int>(TheDisplay->getHeight() - .1 * TheDisplay->getHeight());
	TheTacticalView->setHeight(static_cast<Int>(TheDisplay->getHeight())); 
	m_contextParent[ CP_MASTER ]->winSetPosition(pos.x, pos.y);
	m_contextParent[ CP_MASTER ]->winHide(FALSE);
	setUpDownImages();

}

void ControlBar::setHiddenControlBar( void )
{
	m_currentControlBarStage = ControlBarStages::CONTROL_BAR_STAGE_HIDDEN;
	m_contextParent[ CP_MASTER ]->winHide(TRUE);
}
// removed from multiplayer test
//void ControlBar::showCommandMarkers( void )
//{
//	for(Int i =0; i < MAX_COMMANDS_PER_SET; ++i)
//	{
//		if(m_commandWindows[i]->winIsHidden())
//			m_commandMarkers[i]->winHide(FALSE);
//		else
//			m_commandMarkers[i]->winHide(TRUE);
//	}
//}
//
void ControlBar::updateCommandMarkerImage( const Image *image )
{
	// removed from multiplayer branch

//	// we don't mind if the image is null, that way we can not draw anything
	//	for(Int i =0; i < MAX_COMMANDS_PER_SET; ++i)
	//	{
	//		m_commandMarkers[i]->winSetEnabledImage(0, image);
	//	}
	
}
void ControlBar::updateSlotExitImage( const Image *image )
{
	//Hardcoding values here Not a good thing but there's no other way right now.
	if(!image)
		return;

	//Kris:
	//Other than this being a completely ridiculously retarded idea, I'm not inclined
	//to recode this in a better way, yikes! Btw, I DID NOT CODE THIS! But this is
	//what this does: The button images are overridden by a faction specific icon.
	//The proper way to fix this would be to make a commandbutton option and loop
	//through all buttons on init to replace the icon. We need a system like this
	//for neutral buildings which can have a different empty inventory icon based
	//on the faction player.

	CommandButton *cmdButton = findNonConstCommandButton( "Command_StructureExit" );
	if(cmdButton)
		cmdButton->setButtonImage(image);

	cmdButton = findNonConstCommandButton( "Command_TransportExit" );
	if(cmdButton)
		cmdButton->setButtonImage(image);

	cmdButton = findNonConstCommandButton( "Command_BunkerExit" );
	if(cmdButton)
		cmdButton->setButtonImage(image);

	cmdButton = findNonConstCommandButton( "Command_FireBaseExit" );
	if(cmdButton)
		cmdButton->setButtonImage(image);

}

void ControlBar::updateUpDownImages( const Image *toggleButtonUpIn, const Image *toggleButtonUpOn, const Image *toggleButtonUpPushed,
																		 const Image *toggleButtonDownIn, const Image *toggleButtonDownOn, const Image *toggleButtonDownPushed,
																		 const Image *generalButtonEnable, const Image *generalButtonHighlight  )
{
	m_toggleButtonUpIn = toggleButtonUpIn;
	m_toggleButtonUpOn = toggleButtonUpOn;
	m_toggleButtonUpPushed = toggleButtonUpPushed;
	m_toggleButtonDownIn = toggleButtonDownIn;
	m_toggleButtonDownOn = toggleButtonDownOn;
	m_toggleButtonDownPushed = toggleButtonDownPushed;


	m_generalButtonEnable = generalButtonEnable;
	m_generalButtonHighlight = generalButtonHighlight;

	setUpDownImages();
}

void ControlBar::setUpDownImages( void )
{
	GameWindow *win= TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ButtonLarge" ) );
	if(!win)
		return;
	// we only care if it's in it's low state, else we put the default images up
	if(m_currentControlBarStage == ControlBarStages::CONTROL_BAR_STAGE_LOW)
	{
		GadgetButtonSetEnabledImage(win, m_toggleButtonUpOn);
		GadgetButtonSetHiliteImage(win, m_toggleButtonUpIn);
		GadgetButtonSetHiliteSelectedImage(win, m_toggleButtonUpPushed);
		return;
	}

	GadgetButtonSetEnabledImage(win, m_toggleButtonDownOn);
	GadgetButtonSetHiliteImage(win, m_toggleButtonDownIn);
	GadgetButtonSetHiliteSelectedImage(win, m_toggleButtonDownPushed);

}

void ControlBar::getForegroundMarkerPos(Int *x, Int *y)
{
	*x = m_controlBarForegroundMarkerPos.x;
	*y = m_controlBarForegroundMarkerPos.y;
}
void ControlBar::getBackgroundMarkerPos(Int *x, Int *y)
{
	*x = m_controlBarBackgroundMarkerPos.x;
	*y = m_controlBarBackgroundMarkerPos.y;
}

void ControlBar::drawTransitionHandler( void )
{
//	if(m_transitionHandler)
//		m_transitionHandler->draw();
}
enum{
	RADAR_ATTACK_GLOW_FRAMES = 150,
	RADAR_ATTACK_GLOW_NUM_TIMES = 15  ///< number of times we'll flash
};

void ControlBar::triggerRadarAttackGlow( void )
{
	if(!m_radarAttackGlowWindow)
		return;
	m_radarAttackGlowOn = TRUE;
	m_remainingRadarAttackGlowFrames = RADAR_ATTACK_GLOW_FRAMES;
	if(BitTest(m_radarAttackGlowWindow->winGetStatus(),WIN_STATUS_ENABLED) == TRUE)
		m_radarAttackGlowWindow->winEnable(FALSE);
}

void ControlBar::updateRadarAttackGlow ( void )
{
	if(!m_radarAttackGlowOn || !m_radarAttackGlowWindow)
		return;
	m_remainingRadarAttackGlowFrames--;
	if(m_remainingRadarAttackGlowFrames <= 0)
	{
		m_radarAttackGlowOn = FALSE;
		m_radarAttackGlowWindow->winEnable(TRUE);
		return;
	}
	
	if(m_remainingRadarAttackGlowFrames % RADAR_ATTACK_GLOW_NUM_TIMES == 0)
	{
		m_radarAttackGlowWindow->winEnable(!BitTest(m_radarAttackGlowWindow->winGetStatus(),WIN_STATUS_ENABLED));
	}

	
}
