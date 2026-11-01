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

///////////////////////////////////////////////////////////////////////////////////////
// FILE: LanLobbyMenu.cpp
// Author: Chris Huybregts, October 2001
// Description: Lan Lobby Menu
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Lib/BaseType.h"
#include "Common/crc.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/MultiplayerSettings.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/QuotedPrintable.h"
#include "Common/UserSettings.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/ShellHooks.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameInfoWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/MessageBox.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/IPEnumeration.h"
#include "GameNetwork/GameLobbyCallbacks.h"
#include "GameNetwork/LANGameInfo.h"
#include "GameNetwork/networkutil.h"

Bool LANisShuttingDown = false;
Bool LANbuttonPushed = false;
Bool LANSocketErrorDetected = FALSE;
char *LANnextScreen = nullptr;

static Int	initialGadgetDelay = 2;
static Bool justEntered = FALSE;



// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////


// window ids ------------------------------------------------------------------------------
static NameKeyType parentLanLobbyID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType buttonClearID = NAMEKEY_INVALID;
static NameKeyType buttonHostID = NAMEKEY_INVALID;
static NameKeyType buttonJoinID = NAMEKEY_INVALID;
static NameKeyType buttonDirectConnectID = NAMEKEY_INVALID;
static NameKeyType buttonEmoteID = NAMEKEY_INVALID;
static NameKeyType staticToolTipID = NAMEKEY_INVALID;
static NameKeyType textEntryPlayerNameID = NAMEKEY_INVALID;
static NameKeyType textEntryChatID = NAMEKEY_INVALID;
static NameKeyType listboxPlayersID = NAMEKEY_INVALID;
static NameKeyType staticTextGameInfoID = NAMEKEY_INVALID;


// Window Pointers ------------------------------------------------------------------------
static GameWindow *parentLanLobby = nullptr;
static GameWindow *buttonBack = nullptr;
static GameWindow *buttonClear = nullptr;
static GameWindow *buttonHost = nullptr;
static GameWindow *buttonJoin = nullptr;
static GameWindow *buttonDirectConnect = nullptr;
static GameWindow *buttonEmote = nullptr;
static GameWindow *staticToolTip = nullptr;
static GameWindow *textEntryPlayerName = nullptr;
static GameWindow *textEntryChat = nullptr;
static GameWindow *staticTextGameInfo = nullptr;

//external declarations of the Gadgets the callbacks can use
NameKeyType listboxChatWindowID = NAMEKEY_INVALID;
GameWindow *listboxChatWindow = nullptr;
GameWindow *listboxPlayers = nullptr;
NameKeyType listboxGamesID = NAMEKEY_INVALID;
GameWindow *listboxGames = nullptr;

// hack to disable framerate limiter in LAN games
//static Bool shellmapOn;
static Bool useFpsLimit;
static UnicodeString defaultName;

static void playerTooltip(GameWindow *window,
													WinInstanceData *instData,
													UnsignedInt mouse)
{
	Int x, y, row, col;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);

	GadgetListBoxGetEntryBasedOnXY(window, x, y, row, col);

	if (row == -1 || col == -1)
	{
		//TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:LobbyPlayers") );
		return;
	}

	// "Back" frees the lobby while the menu still fades out and the mouse is still over the list
	if (!TheLobby)
		return;

	UnsignedInt playerIP = static_cast<UnsignedInt>(reinterpret_cast<uintptr_t>(GadgetListBoxGetItemData( window, row, col )));
	LANPlayer *player = TheLobby->LookupPlayer(playerIP);
	if (!player)
	{
		DEBUG_CRASH(("No player info in listbox!"));
		//TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:LobbyPlayers") );
		return;
	}
	UnicodeString tooltip;
	tooltip.format(TheGameText->fetch("TOOLTIP:LANPlayer"), player->getName().str(), player->getLogin().str(), player->getHost().str());
	TheMouse->setCursorTooltip( tooltip );
}


//-------------------------------------------------------------------------------------------------
/** Initialize the Lan Lobby Menu */
//-------------------------------------------------------------------------------------------------
void LanLobbyMenuInit( WindowLayout *layout, void *userData )
{
	LANnextScreen = nullptr;
	LANbuttonPushed = false;
	LANisShuttingDown = false;

	// get the ids for our controls
	parentLanLobbyID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:LanLobbyMenuParent" ) );
	buttonBackID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:ButtonBack" ) );
	buttonClearID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:ButtonClear" ) );
	buttonHostID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:ButtonHost" ) );
	buttonJoinID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:ButtonJoin" ) );
	buttonDirectConnectID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:ButtonDirectConnect" ) );
	buttonEmoteID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:ButtonEmote" ) );
	staticToolTipID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:StaticToolTip" ) );
	textEntryPlayerNameID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:TextEntryPlayerName" ) );
	textEntryChatID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:TextEntryChat" ) );
	listboxPlayersID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:ListboxPlayers" ) );
	listboxChatWindowID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:ListboxChatWindowLanLobby" ) );
	listboxGamesID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:ListboxGames" ) );
	staticTextGameInfoID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:StaticTextGameInfo" ) );


	// Get pointers to the window buttons
	parentLanLobby = TheWindowManager->winGetWindowFromId( nullptr, parentLanLobbyID );
	buttonBack = TheWindowManager->winGetWindowFromId( nullptr,  buttonBackID);
	buttonClear = TheWindowManager->winGetWindowFromId( nullptr,  buttonClearID);
	buttonHost = TheWindowManager->winGetWindowFromId( nullptr, buttonHostID );
	buttonJoin = TheWindowManager->winGetWindowFromId( nullptr, buttonJoinID );
	buttonDirectConnect = TheWindowManager->winGetWindowFromId( nullptr, buttonDirectConnectID );
	buttonEmote = TheWindowManager->winGetWindowFromId( nullptr,buttonEmoteID  );
	staticToolTip = TheWindowManager->winGetWindowFromId( nullptr, staticToolTipID );
	textEntryPlayerName = TheWindowManager->winGetWindowFromId( nullptr, textEntryPlayerNameID );
	textEntryChat = TheWindowManager->winGetWindowFromId( nullptr, textEntryChatID );
	listboxPlayers = TheWindowManager->winGetWindowFromId( nullptr, listboxPlayersID );
	listboxChatWindow = TheWindowManager->winGetWindowFromId( nullptr, listboxChatWindowID );
	listboxGames = TheWindowManager->winGetWindowFromId( nullptr, listboxGamesID );
	staticTextGameInfo = TheWindowManager->winGetWindowFromId( nullptr, staticTextGameInfoID );
	listboxPlayers->winSetTooltipFunc(playerTooltip);

	// Show Menu
	layout->hide( FALSE );

	// Init LAN API Singleton
	if (!TheLobby)
	{
		::Engine::context().lobby = NEW GameLobby();	/// @todo clh delete TheLobby and 
		useFpsLimit = TheGlobalData->m_useFpsLimit;
	}
	else
	{
		TheWritableGlobalData->m_useFpsLimit = useFpsLimit;
		TheLobby->reset();
	}

	// Every adapter, unless -netzLokal or the setting Network.IPAddress names one address.
	const UnsignedInt IP = GetLocalNetInstanceIP() ? GetLocalNetInstanceIP() : TheGlobalData->m_defaultIP;

	// TheLobby->init() sets us to be in a LAN menu screen automatically.
	TheLobby->init();
	TheLobby->setMode(GameLobby::menuMode());
	if (TheLobby->SetLocalIP(IP) == FALSE) {
		LANSocketErrorDetected = TRUE;
	}

	//Initialize the gadgets on the window
	//UnicodeString	txtInput;
	//txtInput.translate(IPs.getMachineName());
	defaultName = LobbySettings(LobbySettings::Kind::Lan).getUserName();
	if (TheGlobalData->m_netLocalInstance > 0 && GetLocalNetInstanceIP() != 0)
	{
		// Local instances share the preferences; the lobby refuses a second player with the same name.
		UnicodeString suffix;
		suffix.format(L"%d", TheGlobalData->m_netLocalInstance);
		while (defaultName.getLength() + suffix.getLength() > g_lanPlayerNameLength)
			defaultName.removeLastChar();
		defaultName.concat(suffix);
	}
	while (defaultName.getLength() > g_lanPlayerNameLength)
		defaultName.removeLastChar();
	GadgetTextEntrySetText( textEntryPlayerName, defaultName);
	// Clear the text entry line
	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);

	GadgetListBoxReset(listboxPlayers);
	GadgetListBoxReset(listboxGames);

	while (defaultName.getLength() > g_lanPlayerNameLength)
		defaultName.removeLastChar();
	TheLobby->RequestSetName(defaultName);
	TheLobby->RequestLocations();
	TheLobby->showModeHelp();

	/*
	UnicodeString unicodeChat;

	unicodeChat = L"Local IP list:";
	GadgetListBoxAddEntryText(listboxChatWindow, unicodeChat, chatSystemColor, -1, 0);

	IPlist = IPs.getAddresses();
	while (IPlist)
	{
		unicodeChat.translate(IPlist->getIPstring());
		GadgetListBoxAddEntryText(listboxChatWindow, unicodeChat, chatSystemColor, -1, 0);
		IPlist = IPlist->getNext();
	}
	*/

	// Set Keyboard to Main Parent
	//TheWindowManager->winSetFocus( parentLanLobby );
	TheWindowManager->winSetFocus( textEntryChat );
	CreateLANGameInfoWindow(staticTextGameInfo);

	//TheShell->showShellMap(FALSE);
	//shellmapOn = FALSE;
	// coming out of a game, re-load the shell map
	TheShell->showShellMap(TRUE);
		
	// check for MOTD
	TheLobby->checkMOTD();
	layout->hide(FALSE);
	layout->bringForward();

	justEntered = TRUE;
	initialGadgetDelay = 2;
	GameWindow *win = TheWindowManager->winGetWindowFromId(nullptr, TheNameKeyGenerator->nameToKey("LanLobbyMenu.wnd:GadgetParent"));
	if(win)
		win->winHide(TRUE);

	
	// animate controls
	//TheShell->registerWithAnimateManager(parentLanLobby, WIN_ANIMATION_SLIDE_TOP, TRUE);
//	TheShell->registerWithAnimateManager(buttonHost, WIN_ANIMATION_SLIDE_LEFT, TRUE, 600);
//	TheShell->registerWithAnimateManager(buttonJoin, WIN_ANIMATION_SLIDE_LEFT, TRUE, 400);
//	TheShell->registerWithAnimateManager(buttonDirectConnect, WIN_ANIMATION_SLIDE_LEFT, TRUE, 200);
//	//TheShell->registerWithAnimateManager(buttonOptions, WIN_ANIMATION_SLIDE_LEFT, TRUE, 1);
//	TheShell->registerWithAnimateManager(buttonBack, WIN_ANIMATION_SLIDE_RIGHT, TRUE, 1);

} // GameLobbyMenuInit

//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{

	LANisShuttingDown = false;

	// hide the layout
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout, (LANnextScreen != nullptr) );

	if (LANnextScreen != nullptr)
	{
		TheShell->push(LANnextScreen);
	}

	LANnextScreen = nullptr;

}  // end if

//-------------------------------------------------------------------------------------------------
/** Lan Lobby menu shutdown method */
//-------------------------------------------------------------------------------------------------
void LanLobbyMenuShutdown( WindowLayout *layout, void *userData )
{
	LobbySettings prefs(LobbySettings::Kind::Lan);
	prefs.setUserName(GadgetTextEntryGetText( textEntryPlayerName ));
	prefs.write();
	// Release the static default name's buffer: it is only needed while the lobby is open
	// (LanLobbyMenuInit sets it again) and would otherwise outlive the leak report.
	defaultName = UnicodeString::TheEmptyString;

	DestroyGameInfoWindow();
	// hide menu
	//layout->hide( TRUE );

	TheLobby->RequestLobbyLeave( true );

	// Reset the LAN singleton
	//TheLobby->reset();

	// our shutdown is complete
	//TheShell->shutdownComplete( layout );
	TheWritableGlobalData->m_useFpsLimit = useFpsLimit;

	LANisShuttingDown = true;

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *static_cast<Bool *>(userData);

	LANSocketErrorDetected = FALSE;

	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}  //end if

	TheShell->reverseAnimatewindow();
	TheTransitionHandler->reverse("LanLobbyFade");
	//if(	shellmapOn)
//		TheShell->showShellMap(TRUE);
}  // LanLobbyMenuShutdown


//-------------------------------------------------------------------------------------------------
/** Lan Lobby menu update method */
//-------------------------------------------------------------------------------------------------
void LanLobbyMenuUpdate( WindowLayout * layout, void *userData)
{
	if (TheGameLogic->isInShellGame() && TheGameLogic->getFrame() == 1)
	{
		SignalUIInteraction(SHELL_SCRIPT_HOOK_LAN_ENTERED_FROM_GAME);
	}

	if(justEntered)
	{
		if(initialGadgetDelay == 1)
		{
			TheTransitionHandler->setGroup("LanLobbyFade");
			initialGadgetDelay = 2;
			justEntered = FALSE;
		}
		else
			initialGadgetDelay--;
	}

	if(LANisShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
		shutdownComplete(layout);

	if (TheShell->isAnimFinished() && !LANbuttonPushed && TheLobby)
		TheLobby->update();

	if (LANSocketErrorDetected == TRUE) {
		LANSocketErrorDetected = FALSE;
		DEBUG_LOG(("SOCKET ERROR!  BAILING!\n"));
		MessageBoxOk(TheGameText->fetch("GUI:NetworkError"), TheGameText->fetch("GUI:SocketError"), nullptr);

		// we have a socket problem, back out to the main menu.
		TheWindowManager->winSendSystemMsg(buttonBack->winGetParent(), GBM_SELECTED,
																			 reinterpret_cast<WindowMsgData>(buttonBack), buttonBackID);
	}

	
}// LanLobbyMenuUpdate

//-------------------------------------------------------------------------------------------------
/** Lan Lobby menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanLobbyMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = static_cast<UnsignedByte>(mData1);
			UnsignedByte state = static_cast<UnsignedByte>(mData2);
			if (LANbuttonPushed)
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
						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																							reinterpret_cast<WindowMsgData>(buttonBack), buttonBackID );

					}  // end if

					// don't let key fall through anywhere else
					return WindowMsgHandledType::MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

		}  // end char

	}  // end switch( msg )

	return WindowMsgHandledType::MSG_IGNORED;
}// LanLobbyMenuInput

//-------------------------------------------------------------------------------------------------
/** Lan Lobby menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanLobbyMenuSystem( GameWindow *window, UnsignedInt msg, 
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;

	switch( msg )
	{
		
		
		case GWM_CREATE:
			{
				SignalUIInteraction(SHELL_SCRIPT_HOOK_LAN_OPENED);
				break;
			} // case GWM_DESTROY:

		case GWM_DESTROY:
			{
				SignalUIInteraction(SHELL_SCRIPT_HOOK_LAN_CLOSED);
				break;
			} // case GWM_DESTROY:

		case GWM_INPUT_FOCUS:
			{	
				// if we're givin the opportunity to take the keyboard focus we must say we want it
				if( mData1 == TRUE )
					*reinterpret_cast<Bool *>(mData2) = TRUE;

				return WindowMsgHandledType::MSG_HANDLED;
			}//case GWM_INPUT_FOCUS:
		case GLM_DOUBLE_CLICKED:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
				Int controlID = control->winGetWindowId();
				if( controlID == listboxGamesID ) 
				{
					int rowSelected = static_cast<Int>(mData2);
				
					if (rowSelected >= 0)
					{
						LANGameInfo * theGame = TheLobby->LookupGameByListOffset(rowSelected);
						if (theGame)
						{
							TheLobby->RequestGameJoin(theGame);
						}
					}
				}
				break;
			}
		case GLM_SELECTED:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
				Int controlID = control->winGetWindowId();
				if( controlID == listboxGamesID ) 
				{
					int rowSelected = static_cast<Int>(mData2);
					if( rowSelected < 0 )
					{
						HideGameInfoWindow(TRUE);
						break;
					}
					LANGameInfo * theGame = TheLobby->LookupGameByListOffset(rowSelected);
					if (theGame)
						RefreshGameInfoWindow(theGame, theGame->getName());
					else
						HideGameInfoWindow(TRUE);

				}
				break;
			}
		case GBM_SELECTED:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
				Int controlID = control->winGetWindowId();

				if ( controlID == buttonBackID )
				{
					//shellmapOn = TRUE;
					LANbuttonPushed = true;
					DEBUG_LOG(("Back was hit - popping to main menu\n"));
					TheShell->pop();
					delete TheLobby;
					::Engine::context().lobby = nullptr;
					//TheTransitionHandler->reverse("LanLobbyFade");

				} //if ( controlID == buttonBack )
				else if ( controlID == buttonHostID )
				{
					TheLobby->RequestGameCreate( UnicodeString(L""), FALSE);
					
				}//else if ( controlID == buttonHostID )
				else if ( controlID == buttonClearID )
				{
					GadgetTextEntrySetText(textEntryPlayerName, UnicodeString::TheEmptyString);
					TheWindowManager->winSendSystemMsg( window, 
																						GEM_UPDATE_TEXT,
																						reinterpret_cast<WindowMsgData>(textEntryPlayerName), 
																						0 );

				}
				else if ( controlID == buttonJoinID )
				{

					//TheShell->push( AsciiString("Menus/LanGameOptionsMenu.wnd") );

					int rowSelected = -1;
					GadgetListBoxGetSelected( listboxGames, &rowSelected );

					if (rowSelected >= 0)
					{
						LANGameInfo * theGame = TheLobby->LookupGameByListOffset(rowSelected);
						if (theGame)
						{
							TheLobby->RequestGameJoin(theGame);
						}
					}
					else
					{
						GadgetListBoxAddEntryText(listboxChatWindow, TheGameText->fetch("LAN:ErrorNoGameSelected") , chatSystemColor, -1, 0);
					}

				} //else if ( controlID == buttonJoinID )
				else if ( controlID == buttonEmoteID )
				{
					// read the user's input
					txtInput.set(GadgetTextEntryGetText( textEntryChat ));
					// Clear the text entry line
					GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
					// Clean up the text (remove leading/trailing chars, etc)
					txtInput.trim();
					// Echo the user's input to the chat window
					if (!txtInput.isEmpty()) {
//						TheLobby->RequestChat(txtInput, GameLobbyInterface::LANCHAT_EMOTE);
						TheLobby->RequestChat(txtInput, GameLobbyInterface::LANCHAT_NORMAL);
					}
				} //if ( controlID == buttonEmote )
				else if (controlID == buttonDirectConnectID)
				{
					TheLobby->RequestLobbyLeave( false );
					TheShell->push(AsciiString("Menus/NetworkDirectConnect.wnd"));
				}
				
				break;
			}// case GBM_SELECTED:
	
		case GEM_UPDATE_TEXT:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
				Int controlID = control->winGetWindowId();

				if ( controlID == textEntryPlayerNameID )
				{
					// grab the user's name
					txtInput.set(GadgetTextEntryGetText( textEntryPlayerName ));

					// Clean up the text (remove leading/trailing chars, etc)
					const WideChar *c = txtInput.str();
					while (c && (iswspace(*c)))
						c++;

					if (c)
						txtInput = UnicodeString(c);
					else
						txtInput = UnicodeString::TheEmptyString;

					while (txtInput.getLength() > g_lanPlayerNameLength)
						txtInput.removeLastChar();
					
					if (!txtInput.isEmpty() && txtInput.getCharAt(txtInput.getLength()-1) == L',')
						txtInput.removeLastChar(); // we use , for strtok's so we can't allow them in names.  :(

					if (!txtInput.isEmpty() && txtInput.getCharAt(txtInput.getLength()-1) == L':')
						txtInput.removeLastChar(); // we use : for strtok's so we can't allow them in names.  :(

					if (!txtInput.isEmpty() && txtInput.getCharAt(txtInput.getLength()-1) == L';')
						txtInput.removeLastChar(); // we use ; for strtok's so we can't allow them in names.  :(

					// send it over the network
					if (!txtInput.isEmpty())
						TheLobby->RequestSetName(txtInput);
					else
						{
							TheLobby->RequestSetName(defaultName);
						}

					// Put the whitespace-free version in the box
					GadgetTextEntrySetText( textEntryPlayerName, txtInput );

				}// if ( controlID == textEntryPlayerNameID )
				break;
			}//case GEM_UPDATE_TEXT:
		case GEM_EDIT_DONE:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
				Int controlID = control->winGetWindowId();

				// Take the user's input and echo it into the chat window as well as
				// send it to the other clients on the lan
				if ( controlID == textEntryChatID )
				{
					
					// read the user's input
					txtInput.set(GadgetTextEntryGetText( textEntryChat ));
					// Clear the text entry line
					GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
					// Clean up the text (remove leading/trailing chars, etc)
					while (!txtInput.isEmpty() && iswspace(txtInput.getCharAt(0)))
						txtInput = UnicodeString(txtInput.str()+1);

					// Echo the user's input to the chat window
					if (!txtInput.isEmpty())
						TheLobby->RequestChat(txtInput, GameLobbyInterface::LANCHAT_NORMAL);

				}// if ( controlID == textEntryChatID )
				/*
				else if ( controlID == textEntryPlayerNameID )
				{
					// grab the user's name
					txtInput.set(GadgetTextEntryGetText( textEntryPlayerName ));

					// Clean up the text (remove leading/trailing chars, etc)
					txtInput.trim();

					// send it over the network
					if (!txtInput.isEmpty())
						TheLobby->RequestSetName(txtInput);

					// Put the whitespace-free version in the box
					GadgetTextEntrySetText( textEntryPlayerName, txtInput );

				}// if ( controlID == textEntryPlayerNameID )
				*/
				break;
			}
		default:
			return WindowMsgHandledType::MSG_IGNORED;

	}//Switch

	return WindowMsgHandledType::MSG_HANDLED;
}// LanLobbyMenuSystem
