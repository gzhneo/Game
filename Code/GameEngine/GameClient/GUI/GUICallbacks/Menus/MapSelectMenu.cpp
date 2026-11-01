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

// FILE: MapSelectMenu.cpp ////////////////////////////////////////////////////////////////////////
// Author: Colin Day, October 2001
// Description: MapSelect menu window callbacks
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameEngine.h"
#include "Common/MessageStream.h"
#include "Common/RandomValue.h"
#include "Common/UserSettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetRadioButton.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
static NameKeyType radioButtonSystemMapsID = NAMEKEY_INVALID;
static NameKeyType radioButtonUserMapsID = NAMEKEY_INVALID;
static GameWindow *mapList = nullptr;

static Bool showSoloMaps = true;
static Bool isShuttingDown = false;
static Bool startGame = false;
static Bool buttonPushed = false;
static GameDifficulty s_AIDiff = DIFFICULTY_NORMAL;
static void setupGameStart(AsciiString mapName)
{
	startGame = true;
	TheWritableGlobalData->m_pendingFile = mapName;
	TheShell->reverseAnimatewindow();
}

static void doGameStart( void )
{
	startGame = false;

	if (TheGameLogic->isInGame())
		TheGameLogic->clearGameData();
	//TheScriptEngine->setGlobalDifficulty(s_AIDiff); // CANNOT DO THIS! REPLAYS WILL BREAK!!!
	// send a message to the logic for a new game
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
	msg->appendIntegerArgument(s_AIDiff);
	msg->appendIntegerArgument(0);

	/// @todo: when Campaign & skirmish are separated, make campaign have fixed seed and skirmish random.
	InitRandom(0);
	/*
	if (TheGlobalData->m_fixedSeed >= 0)
		InitGameLogicRandom(TheGlobalData->m_fixedSeed);
	else
		InitGameLogicRandom(GameClientRandomValue(0, INT_MAX - 1));
	*/

	isShuttingDown = true;
}


//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{

	isShuttingDown = false;

	// hide the layout
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout );

}  // end if

void SetDifficultyRadioButton( void )
{
	AsciiString parentName( "MapSelectMenu.wnd:MapSelectMenuParent" );
	NameKeyType parentID = TheNameKeyGenerator->nameToKey( parentName );
	GameWindow *parent = TheWindowManager->winGetWindowFromId( nullptr, parentID );

	if (!TheScriptEngine)
	{
		s_AIDiff = DIFFICULTY_EASY;
	}
	else
	{
		switch (TheScriptEngine->getGlobalDifficulty())
		{
			case DIFFICULTY_EASY:
			{
				NameKeyType radioButtonEasyAIID = TheNameKeyGenerator->nameToKey( AsciiString("MapSelectMenu.wnd:RadioButtonEasyAI") );
				GameWindow *radioButtonEasyAI = TheWindowManager->winGetWindowFromId( parent, radioButtonEasyAIID );			
				GadgetRadioSetSelection(radioButtonEasyAI, FALSE);
				s_AIDiff = DIFFICULTY_EASY;
				break;
			}
			case DIFFICULTY_NORMAL:
			{
				NameKeyType radioButtonMediumAIID = TheNameKeyGenerator->nameToKey( AsciiString("MapSelectMenu.wnd:RadioButtonMediumAI") );
				GameWindow *radioButtonMediumAI = TheWindowManager->winGetWindowFromId( parent, radioButtonMediumAIID );
				GadgetRadioSetSelection(radioButtonMediumAI, FALSE);
				s_AIDiff = DIFFICULTY_NORMAL;
				break;
			}
			case DIFFICULTY_HARD:
			{
				NameKeyType radioButtonHardAIID = TheNameKeyGenerator->nameToKey( AsciiString("MapSelectMenu.wnd:RadioButtonHardAI") );
				GameWindow *radioButtonHardAI = TheWindowManager->winGetWindowFromId( parent, radioButtonHardAIID );			
				GadgetRadioSetSelection(radioButtonHardAI, FALSE);
				s_AIDiff = DIFFICULTY_HARD;
				break;
			}

		default:
			{
				DEBUG_CRASH(("unrecognized difficulty level in the script engine"));
			}
			
		}
	} // if (TheScriptEngine)
}

//-------------------------------------------------------------------------------------------------
/** Initialize the MapSelect menu */
//-------------------------------------------------------------------------------------------------
void MapSelectMenuInit( WindowLayout *layout, void *userData )
{
	showSoloMaps = true;
	buttonPushed = false;
	isShuttingDown = false;
	startGame = false;
	TheShell->showShellMap(TRUE);
	// show menu
	layout->hide( FALSE );

	Bool usesSystemMapDir = UserSettings::usesSystemMapDir();

	// get the listbox window
	AsciiString listString( "MapSelectMenu.wnd:ListboxMap" );
	NameKeyType mapListID = TheNameKeyGenerator->nameToKey( listString );
	mapList = TheWindowManager->winGetWindowFromId( nullptr, mapListID );
	if( mapList )
	{
		if (TheMapCache)
			TheMapCache->updateCache();
		populateMapListbox( mapList, usesSystemMapDir, !showSoloMaps );
	}

	
	// set keyboard focus to main parent
	AsciiString parentName( "MapSelectMenu.wnd:MapSelectMenuParent" );
	NameKeyType parentID = TheNameKeyGenerator->nameToKey( parentName );
	GameWindow *parent = TheWindowManager->winGetWindowFromId( nullptr, parentID );
	TheWindowManager->winSetFocus( parent );

	NameKeyType buttonBackID = TheNameKeyGenerator->nameToKey( AsciiString("MapSelectMenu.wnd:ButtonBack") );
	GameWindow *buttonBack = TheWindowManager->winGetWindowFromId( nullptr, buttonBackID );
	
	NameKeyType buttonOKID = TheNameKeyGenerator->nameToKey( AsciiString("MapSelectMenu.wnd:ButtonOK") );
	GameWindow *buttonOK = TheWindowManager->winGetWindowFromId( nullptr, buttonOKID );


	TheShell->registerWithAnimateManager(buttonBack, AnimTypes::WIN_ANIMATION_SLIDE_RIGHT, TRUE,0);
	TheShell->registerWithAnimateManager(buttonOK, AnimTypes::WIN_ANIMATION_SLIDE_LEFT, TRUE, 0);

	SetDifficultyRadioButton();

	radioButtonSystemMapsID = TheNameKeyGenerator->nameToKey( "MapSelectMenu.wnd:RadioButtonSystemMaps" );
	radioButtonUserMapsID = TheNameKeyGenerator->nameToKey( "MapSelectMenu.wnd:RadioButtonUserMaps" );
	GameWindow *radioButtonSystemMaps = TheWindowManager->winGetWindowFromId( parent, radioButtonSystemMapsID );
	GameWindow *radioButtonUserMaps = TheWindowManager->winGetWindowFromId( parent, radioButtonUserMapsID );
	if (usesSystemMapDir)
		GadgetRadioSetSelection( radioButtonSystemMaps, FALSE );
	else
		GadgetRadioSetSelection( radioButtonUserMaps, FALSE );
}  // end MapSelectMenuInit

//-------------------------------------------------------------------------------------------------
/** MapSelect menu shutdown method */
//-------------------------------------------------------------------------------------------------
void MapSelectMenuShutdown( WindowLayout *layout, void *userData )
{
	if (!startGame)
		isShuttingDown = true;

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *static_cast<Bool *>(userData);
	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}  //end if

	if (!startGame)
		TheShell->reverseAnimatewindow();

}  // end MapSelectMenuShutdown

//-------------------------------------------------------------------------------------------------
/** MapSelect menu update method */
//-------------------------------------------------------------------------------------------------
void MapSelectMenuUpdate( WindowLayout *layout, void *userData )
{

	if (startGame && TheShell->isAnimFinished())
		doGameStart();

	// We'll only be successful if we've requested to 
	if(isShuttingDown && TheShell->isAnimFinished())
		shutdownComplete(layout);


}  // end MapSelectMenuUpdate

//-------------------------------------------------------------------------------------------------
/** Map select menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType MapSelectMenuInput( GameWindow *window, UnsignedInt msg,
																				 WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = static_cast<UnsignedByte>(mData1);
			UnsignedByte state = static_cast<UnsignedByte>(mData2);
			if (buttonPushed)
				break;

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{
					
					//
					// send a simulated selected event to the parent window of the
					// back/exit button
					//
					if( BitTest( state, KEY_STATE_UP ) )
					{
						AsciiString buttonName( "MapSelectMenu.wnd:ButtonBack" );
						NameKeyType buttonID = TheNameKeyGenerator->nameToKey( buttonName );
						GameWindow *button = TheWindowManager->winGetWindowFromId( window, buttonID );

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																								reinterpret_cast<WindowMsgData>(button), buttonID );

					}  // end if

					// don't let key fall through anywhere else
					return WindowMsgHandledType::MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

		}  // end char

	}  // end switch( msg )

	return WindowMsgHandledType::MSG_IGNORED;

}  // end MapSelectMenuInput

//-------------------------------------------------------------------------------------------------
/** MapSelect menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType MapSelectMenuSystem( GameWindow *window, UnsignedInt msg, 
																				  WindowMsgData mData1, WindowMsgData mData2 )
{
	static NameKeyType buttonBack = NAMEKEY_INVALID;
	static NameKeyType buttonOK = NAMEKEY_INVALID;
	static NameKeyType listboxMap = NAMEKEY_INVALID;
	static NameKeyType radioButtonEasyAI = NAMEKEY_INVALID;
	static NameKeyType radioButtonMediumAI = NAMEKEY_INVALID;
	static NameKeyType radioButtonHardAI = NAMEKEY_INVALID;
	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CREATE:
		{

			// get ids for our children controls
			buttonBack = TheNameKeyGenerator->nameToKey( AsciiString("MapSelectMenu.wnd:ButtonBack") );
			buttonOK = TheNameKeyGenerator->nameToKey( AsciiString("MapSelectMenu.wnd:ButtonOK") );
			listboxMap = TheNameKeyGenerator->nameToKey( AsciiString("MapSelectMenu.wnd:ListboxMap") );
			radioButtonEasyAI = TheNameKeyGenerator->nameToKey( AsciiString("MapSelectMenu.wnd:RadioButtonEasyAI") );
			radioButtonMediumAI = TheNameKeyGenerator->nameToKey( AsciiString("MapSelectMenu.wnd:RadioButtonMediumAI") );
			radioButtonHardAI = TheNameKeyGenerator->nameToKey( AsciiString("MapSelectMenu.wnd:RadioButtonHardAI") );
			break;

		}  // end create

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

			return WindowMsgHandledType::MSG_HANDLED;

		}  // end input

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			if (buttonPushed)
				break;

			GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
			Int controlID = control->winGetWindowId();

			static NameKeyType singlePlayerID = NAMEKEY("MapSelectMenu.wnd:ButtonSinglePlayer");
			static NameKeyType multiplayerID = NAMEKEY("MapSelectMenu.wnd:ButtonMultiplayer");
			if ( controlID == singlePlayerID )
			{
				showSoloMaps = true;
				populateMapListbox( mapList, UserSettings::usesSystemMapDir(), !showSoloMaps );
			}
			else if ( controlID == multiplayerID )
			{
				showSoloMaps = false;
				populateMapListbox( mapList, UserSettings::usesSystemMapDir(), !showSoloMaps );
			}
			else if ( controlID == radioButtonSystemMapsID )
			{
				if (TheMapCache)
					TheMapCache->updateCache();
				populateMapListbox( mapList, TRUE, !showSoloMaps );
				UserSettings::setBool("General.UseSystemMapDir", TRUE);
				UserSettings::save();
			}
			else if ( controlID == radioButtonUserMapsID )
			{
				if (TheMapCache)
					TheMapCache->updateCache();
				populateMapListbox( mapList, FALSE, !showSoloMaps );
				UserSettings::setBool("General.UseSystemMapDir", FALSE);
				UserSettings::save();
			}
			else if( controlID == buttonBack )
			{

				// go back one screen
				TheShell->pop();
				buttonPushed = true;

			}  // end if
			else if( controlID == buttonOK )
			{

				Int selected;
				UnicodeString map;
				GameWindow *mapWindow = TheWindowManager->winGetWindowFromId( nullptr, listboxMap );

				// get the selected index
				GadgetListBoxGetSelected( mapWindow, &selected );

				if( selected != -1 )
				{
					buttonPushed = true;
					// reset the campaign manager to empty
					if( TheCampaignManager )
					  TheCampaignManager->setCampaign( AsciiString( "" ) );
					// get text of the map to load
					const char *mapFname = static_cast<const char *>(GadgetListBoxGetItemData( mapWindow, selected ));
					DEBUG_ASSERTCRASH(mapFname, ("No map item data"));
					if (mapFname)
						setupGameStart(mapFname);
				}  // end if

			}  // end else if
			else if( controlID == radioButtonEasyAI)
			{
				s_AIDiff = DIFFICULTY_EASY;
			}
			else if( controlID == radioButtonMediumAI)
			{
				s_AIDiff = DIFFICULTY_NORMAL;
			}
			else if( controlID == radioButtonHardAI)
			{
				s_AIDiff = DIFFICULTY_HARD;
			}
			break;

		}  // end selected
		case GLM_DOUBLE_CLICKED:
			{
				if (buttonPushed)
					break;

				GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
				Int controlID = control->winGetWindowId();
				if( controlID == listboxMap ) 
				{
					int rowSelected = static_cast<Int>(mData2);
				
					if (rowSelected >= 0)
					{
						//buttonPushed = true;
						GadgetListBoxSetSelected( control, rowSelected );
						NameKeyType buttonOKID = TheNameKeyGenerator->nameToKey( AsciiString("MapSelectMenu.wnd:ButtonOK") );
						GameWindow *buttonOK2 = TheWindowManager->winGetWindowFromId( nullptr, buttonOKID );

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																								reinterpret_cast<WindowMsgData>(buttonOK2), buttonOKID );
					}
				}
				break;
			}
		default:
			return WindowMsgHandledType::MSG_IGNORED;

	}  // end switch

	return WindowMsgHandledType::MSG_HANDLED;

}  // end MapSelectMenuSystem
