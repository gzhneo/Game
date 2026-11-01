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

// FILE: LanMapSelectMenu.cpp ////////////////////////////////////////////////////////////////////////
// Author: Colin Day, October 2001
// Description: MapSelect menu window callbacks
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameEngine.h"
#include "Common/NameKeyGenerator.h"
#include "Common/MessageStream.h"
#include "Common/UserSettings.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetRadioButton.h"
#include "GameNetwork/GameLobbyCallbacks.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/GUIUtil.h"

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static NameKeyType buttonBack = NAMEKEY_INVALID;
static NameKeyType buttonOK = NAMEKEY_INVALID;
static NameKeyType listboxMap = NAMEKEY_INVALID;
static NameKeyType winMapPreviewID = NAMEKEY_INVALID;
static GameWindow *parent = nullptr;
static GameWindow *mapList = nullptr;
static GameWindow *winMapPreview = nullptr;
static NameKeyType radioButtonSystemMapsID = NAMEKEY_INVALID;
static NameKeyType radioButtonUserMapsID = NAMEKEY_INVALID;

static GameWindow *buttonMapStartPosition[MAX_SLOTS] = {nullptr,nullptr,nullptr,nullptr,
																								nullptr,nullptr,nullptr,nullptr };
static NameKeyType buttonMapStartPositionID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };


// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
void positionStartSpots( AsciiString mapName, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);
static const char *layoutFilename = "LanGameOptionsMenu.wnd";
static const char *parentName = "LanGameOptionsMenuParent";
static const char *gadgetsToHide[] =
{
	"MapWindow",
	//"StaticTextTitle",
	"StaticTextTeam",
	"StaticTextFaction",
	"StaticTextColor",
	"TextEntryMapDisplay",
	"ButtonSelectMap",
	"ButtonStart",
	"StaticTextMapPreview",

	nullptr // keep this last
};
static const char *perPlayerGadgetsToHide[] =
{
	"ComboBoxTeam",
	"ComboBoxColor",
	"ComboBoxPlayerTemplate",
	nullptr // keep this last
};

static void showLANGameOptionsUnderlyingGUIElements( Bool show )
{
	ShowUnderlyingGUIElements( show, layoutFilename, parentName, gadgetsToHide, perPlayerGadgetsToHide );
	GameWindow *win	= TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey("LanGameOptionsMenu.wnd:ButtonBack") );
	if(win)
		win->winEnable( show );

}

static void NullifyControls()
{
	mapList = nullptr;
	winMapPreview = nullptr;
	parent = nullptr;
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		buttonMapStartPosition[i] = nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
/** Initialize the MapSelect menu */
//-------------------------------------------------------------------------------------------------
void LanMapSelectMenuInit( WindowLayout *layout, void *userData )
{
	showLANGameOptionsUnderlyingGUIElements(FALSE);

	// set keyboard focus to main parent
	AsciiString parentName2( "LanMapSelectMenu.wnd:LanMapSelectMenuParent" );
	NameKeyType parentID = TheNameKeyGenerator->nameToKey( parentName2 );
	parent = TheWindowManager->winGetWindowFromId( nullptr, parentID );

	TheWindowManager->winSetFocus( parent );

	Bool usesSystemMapDir = LobbySettings(LobbySettings::Kind::Lan).usesSystemMapDir();

	const MapMetaData *mmd = TheMapCache->findMap(TheLobby->GetMyGame()->getMap());
	if (mmd)
	{
		usesSystemMapDir = mmd->m_isOfficial;
	}


	buttonBack = TheNameKeyGenerator->nameToKey( AsciiString("LanMapSelectMenu.wnd:ButtonBack") );
	buttonOK = TheNameKeyGenerator->nameToKey( AsciiString("LanMapSelectMenu.wnd:ButtonOK") );
	listboxMap = TheNameKeyGenerator->nameToKey( AsciiString("LanMapSelectMenu.wnd:ListboxMap") );
	winMapPreviewID = TheNameKeyGenerator->nameToKey( AsciiString("LanMapSelectMenu.wnd:WinMapPreview") );

	radioButtonSystemMapsID = TheNameKeyGenerator->nameToKey( "LanMapSelectMenu.wnd:RadioButtonSystemMaps" );
	radioButtonUserMapsID = TheNameKeyGenerator->nameToKey( "LanMapSelectMenu.wnd:RadioButtonUserMaps" );
	GameWindow *radioButtonSystemMaps = TheWindowManager->winGetWindowFromId( parent, radioButtonSystemMapsID );
	GameWindow *radioButtonUserMaps = TheWindowManager->winGetWindowFromId( parent, radioButtonUserMapsID );
	winMapPreview = TheWindowManager->winGetWindowFromId(parent, winMapPreviewID);
	if (usesSystemMapDir)
		GadgetRadioSetSelection( radioButtonSystemMaps, FALSE );
	else
		GadgetRadioSetSelection( radioButtonUserMaps, FALSE );

	AsciiString tmpString;
	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		tmpString.format("LanMapSelectMenu.wnd:ButtonMapStartPosition%d", i);
		buttonMapStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonMapStartPosition[i] = TheWindowManager->winGetWindowFromId( winMapPreview, buttonMapStartPositionID[i] );
		DEBUG_ASSERTCRASH(buttonMapStartPosition[i], ("Could not find the ButtonMapStartPosition[%d]",i ));
		buttonMapStartPosition[i]->winHide(TRUE);
		buttonMapStartPosition[i]->winEnable(FALSE);
	}

	// get the listbox window
	AsciiString listString( "LanMapSelectMenu.wnd:ListboxMap" );
	NameKeyType mapListID = TheNameKeyGenerator->nameToKey( listString );
	mapList = TheWindowManager->winGetWindowFromId( parent, mapListID );
	if( mapList )
	{
		if (TheMapCache)
			TheMapCache->updateCache();
		populateMapListbox( mapList, usesSystemMapDir, TRUE, TheLobby->GetMyGame()->getMap() );
	}
}  // end LanMapSelectMenuInit

//-------------------------------------------------------------------------------------------------
/** MapSelect menu shutdown method */
//-------------------------------------------------------------------------------------------------
void LanMapSelectMenuShutdown( WindowLayout *layout, void *userData )
{

	// hide menu
	layout->hide( TRUE );

	NullifyControls();
	
	// our shutdown is complete
	TheShell->shutdownComplete( layout );
}  // end LanMapSelectMenuShutdown

//-------------------------------------------------------------------------------------------------
/** MapSelect menu update method */
//-------------------------------------------------------------------------------------------------
void LanMapSelectMenuUpdate( WindowLayout *layout, void *userData )
{

}  // end LanMapSelectMenuUpdate

//-------------------------------------------------------------------------------------------------
/** Map select menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanMapSelectMenuInput( GameWindow *window, UnsignedInt msg,
																				 WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = static_cast<UnsignedByte>(mData1);
			UnsignedByte state = static_cast<UnsignedByte>(mData2);

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
						AsciiString buttonName( "LanMapSelectMenu.wnd:ButtonBack" );
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

}  // end LanMapSelectMenuInput

//-------------------------------------------------------------------------------------------------
/** MapSelect menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanMapSelectMenuSystem( GameWindow *window, UnsignedInt msg, 
																				  WindowMsgData mData1, WindowMsgData mData2 )
{
	GameWindow *mapWindow = nullptr;
	if (listboxMap != NULL)
	{
		mapWindow = TheWindowManager->winGetWindowFromId( parent, listboxMap );
	}

	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CREATE:
		{
			
			break;

		}  // end create

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{
			NullifyControls();

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
		case GLM_DOUBLE_CLICKED:
			{
				GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
				Int controlID = control->winGetWindowId();
				if( controlID == listboxMap ) 
				{
					int rowSelected = static_cast<Int>(mData2);
				
					if (rowSelected >= 0)
					{
						GadgetListBoxSetSelected( control, rowSelected );
						GameWindow *button = TheWindowManager->winGetWindowFromId( window, buttonOK );

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																								reinterpret_cast<WindowMsgData>(button), buttonOK );
					}
				}
				break;
			}
		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
			Int controlID = control->winGetWindowId();

			if ( controlID == radioButtonSystemMapsID )
			{
				if (TheMapCache)
					TheMapCache->updateCache();
				populateMapListbox( mapList, TRUE, TRUE, TheLobby->GetMyGame()->getMap() );
				LobbySettings pref(LobbySettings::Kind::Lan);
				pref.setUsesSystemMapDir(TRUE);
				pref.write();
			}
			else if ( controlID == radioButtonUserMapsID )
			{
				if (TheMapCache)
					TheMapCache->updateCache();
				populateMapListbox( mapList, FALSE, TRUE, TheLobby->GetMyGame()->getMap() );
				LobbySettings pref(LobbySettings::Kind::Lan);
				pref.setUsesSystemMapDir(FALSE);
				pref.write();
			}
			else if ( controlID == buttonBack )
			{
				
				mapSelectLayout->destroyWindows();
				deleteInstanceOf(mapSelectLayout);
				mapSelectLayout = nullptr;
				// set the controls to NULL since they've been destroyed.
				NullifyControls();
				showLANGameOptionsUnderlyingGUIElements(TRUE);
				PostToLanGameOptions( PostToLanGameType::MAP_BACK );
			}  // end if
			else if ( controlID == buttonOK )
			{
				Int selected = -1;
				UnicodeString map;

				// get the selected index
				if (mapWindow != nullptr)
				{
					GadgetListBoxGetSelected( mapWindow, &selected );
				}

				if( selected != -1 )
				{
					// get text of the map to load
					map = GadgetListBoxGetText( mapWindow, selected, 0 );

					// set the map name in the global data map name
					AsciiString asciiMap;
					const char *mapFname = static_cast<const char *>(GadgetListBoxGetItemData( mapWindow, selected ));
					DEBUG_ASSERTCRASH(mapFname, ("No map item data"));
					if (mapFname)
						asciiMap = mapFname;
					else
						asciiMap.translate( map );
					TheLobby->GetMyGame()->setMap( asciiMap );
					asciiMap.toLower();
					std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(asciiMap);
					if (it != TheMapCache->end())
					{
						TheLobby->GetMyGame()->getSlot(0)->setMapAvailability(true);
						TheLobby->GetMyGame()->setMapCRC( it->second.m_CRC );
						TheLobby->GetMyGame()->setMapSize( it->second.m_filesize );

						TheLobby->GetMyGame()->resetStartSpots();
						TheLobby->GetMyGame()->adjustSlotsForMap(); // BGC- adjust the slots for the new map.
					}

					
					mapSelectLayout->destroyWindows();
					deleteInstanceOf(mapSelectLayout);
					mapSelectLayout = nullptr;

					// set the controls to NULL since they've been destroyed.
					NullifyControls();

					showLANGameOptionsUnderlyingGUIElements(TRUE);
					PostToLanGameOptions(PostToLanGameType::SEND_GAME_OPTS);

				}  // end if
			}  // end else if

			break;

		}  // end selected

		case GLM_SELECTED:
			{
				
				GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
				Int controlID = control->winGetWindowId();
				if( controlID == listboxMap ) 
				{
					int rowSelected = static_cast<Int>(mData2);
					if( rowSelected < 0 )
					{
						positionStartSpots( AsciiString::TheEmptyString, buttonMapStartPosition, winMapPreview);
//						winMapPreview->winClearStatus(WIN_STATUS_IMAGE);
						break;
					}
					winMapPreview->winSetStatus(WIN_STATUS_IMAGE);
					UnicodeString map;
					// get text of the map to load
					map = GadgetListBoxGetText( mapWindow, rowSelected, 0 );

					// set the map name in the global data map name
					AsciiString asciiMap;
					const char *mapFname = static_cast<const char *>(GadgetListBoxGetItemData( mapWindow, rowSelected ));
					DEBUG_ASSERTCRASH(mapFname, ("No map item data"));
					if (mapFname)
						asciiMap = mapFname;
					else
						asciiMap.translate( map );
					asciiMap.toLower();
					Image *image = getMapPreviewImage(asciiMap);
					winMapPreview->winSetUserData((void *)TheMapCache->findMap(asciiMap));
					if(image)
					{
						winMapPreview->winSetEnabledImage(0, image);
					}
					else
					{
						winMapPreview->winClearStatus(WIN_STATUS_IMAGE);
					}
					positionStartSpots( asciiMap, buttonMapStartPosition, winMapPreview);
				}
				break;
			}

		default:
			return WindowMsgHandledType::MSG_IGNORED;

	}  // end switch

	return WindowMsgHandledType::MSG_HANDLED;

}  // end LanMapSelectMenuSystem
