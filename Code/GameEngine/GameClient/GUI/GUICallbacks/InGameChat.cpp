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

// FILE: InGameChat.cpp ///////////////////////////////////////////////////////////////////////
// Author: Matthew D. Campbell - June 2002
// Desc: GUI callbacks for the in-game chat entry
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/DisconnectMenu.h"
#include "GameClient/GameWindow.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameText.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/InGameUI.h"
#include "GameClient/LanguageFilter.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/NetworkInterface.h"

static GameWindow *chatWindow = nullptr;
static GameWindow *chatTextEntry = nullptr;
static GameWindow *chatTypeStaticText = nullptr;
static UnicodeString s_savedChat;
static InGameChatType inGameChatType;

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ShowInGameChat( Bool immediate )
{
	if (TheGameLogic->isInReplayGame())
		return;

	if (TheInGameUI->isQuitMenuVisible())
		return;

	if (TheDisconnectMenu && TheDisconnectMenu->isScreenVisible())
		return;

	if (chatWindow)
	{
		chatWindow->winHide(FALSE);
		chatWindow->winEnable(TRUE);
		chatTextEntry->winHide(FALSE);
		chatTextEntry->winEnable(TRUE);
		GadgetTextEntrySetText( chatTextEntry, s_savedChat );
		s_savedChat.clear();
	}
	else
	{
		chatWindow = TheWindowManager->winCreateFromScript( AsciiString("InGameChat.wnd") );

		static NameKeyType textEntryChatID = TheNameKeyGenerator->nameToKey( "InGameChat.wnd:TextEntryChat" );
		chatTextEntry = TheWindowManager->winGetWindowFromId( nullptr, textEntryChatID );
		GadgetTextEntrySetText( chatTextEntry, UnicodeString::TheEmptyString );

		static NameKeyType chatTypeStaticTextID = TheNameKeyGenerator->nameToKey( "InGameChat.wnd:StaticTextChatType" );
		chatTypeStaticText = TheWindowManager->winGetWindowFromId( nullptr, chatTypeStaticTextID );
	}
	TheWindowManager->winSetFocus( chatTextEntry );
	SetInGameChatType( InGameChatType::INGAME_CHAT_EVERYONE );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ResetInGameChat( void )
{
	if(chatWindow)
		TheWindowManager->winDestroy( chatWindow );
	chatWindow = nullptr;
	chatTextEntry = nullptr;
	chatTypeStaticText = nullptr;
	s_savedChat.clear();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void HideInGameChat( Bool immediate )
{
	if (chatWindow)
	{
		s_savedChat = GadgetTextEntryGetText( chatTextEntry );
		chatWindow->winHide(TRUE);
		chatWindow->winEnable(FALSE);
		chatTextEntry->winHide(TRUE);
		chatTextEntry->winEnable(FALSE);
		TheWindowManager->winSetFocus( nullptr );
	}
	TheWindowManager->winSetFocus( nullptr );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void SetInGameChatType( InGameChatType chatType )
{
	inGameChatType = chatType;
	if (chatTypeStaticText)
	{
		switch (inGameChatType)
		{
		case InGameChatType::INGAME_CHAT_EVERYONE:
			if (ThePlayerList->getLocalPlayer()->isPlayerActive())
				GadgetStaticTextSetText( chatTypeStaticText, TheGameText->fetch("Chat:Everyone") );
			else
				GadgetStaticTextSetText( chatTypeStaticText, TheGameText->fetch("Chat:Observers") );
			break;
		case InGameChatType::INGAME_CHAT_ALLIES:
			GadgetStaticTextSetText( chatTypeStaticText, TheGameText->fetch("Chat:Allies") );
			break;
		case InGameChatType::INGAME_CHAT_PLAYERS:
			GadgetStaticTextSetText( chatTypeStaticText, TheGameText->fetch("Chat:Players") );
			break;
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool IsInGameChatActive() {
	if (chatWindow != nullptr) {
		if (chatWindow->winIsHidden() == FALSE) {
			return TRUE;
		}
	}
	return FALSE;
}

// Slash commands -------------------------------------------------------------------------

// "/remove <name>": vote to remove a player; every other player has to vote the same.
static void voteToRemove(UnicodeString name)
{
	name.trim();
	if (name.isEmpty())
	{
		TheInGameUI->message("GZH:RemoveUsage");
		return;
	}
	for (Int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		if (static_cast<UnsignedInt>(slot) == TheNetwork->getLocalPlayerID() || !TheNetwork->isPlayerConnected(slot))
			continue;
		if (TheNetwork->getPlayerName(slot).compareNoCase(name) != 0)
			continue;
		if (!TheNetwork->voteToRemovePlayer(slot))
			TheInGameUI->message("GZH:RemoveRefused", name.str());
		return;		// the vote is reported to everyone when it is counted
	}
	TheInGameUI->message("GZH:RemoveUnknown", name.str());
}

Bool handleInGameSlashCommands(UnicodeString uText)
{
	AsciiString message;
	message.translate(uText);

	if (message.getCharAt(0) != '/')
	{
		return FALSE; // not a slash command
	}

	AsciiString remainder = message.str() + 1;
	AsciiString token;
	remainder.nextToken(&token);
	token.toLower();

	if ((token == "remove" || token == "kick") && TheNetwork != nullptr)
	{
		const WideChar *rest = uText.str() + 1;
		while (*rest != 0 && *rest != L' ')
			++rest;
		voteToRemove(UnicodeString(rest));
		return TRUE;
	}

	return FALSE; // not a slash command
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ToggleInGameChat( Bool immediate )
{
	static Bool justHid = false;
	if (justHid)
	{
		justHid = false;
		return;
	}

	if (TheGameLogic->isInReplayGame())
		return;

	if (!TheGameInfo->isMultiPlayer() && TheGlobalData->m_netMinPlayers)
		return;

	if (chatWindow)
	{
		Bool show = chatWindow->winIsHidden();
		if (show)
			ShowInGameChat( immediate );
		else
		{
			if (chatTextEntry)
			{
				// Send what is there, clear it out, and hide the window
				UnicodeString msg = GadgetTextEntryGetText( chatTextEntry );
				msg.trim();
				if (!msg.isEmpty() && !handleInGameSlashCommands(msg))
				{
					const Player *localPlayer = ThePlayerList->getLocalPlayer();
					AsciiString playerName;
					Int playerMask = 0;

					for (Int i=0; i<MAX_SLOTS; ++i)
					{
						playerName.format("player%d", i);
						const Player *player = ThePlayerList->findPlayerWithNameKey( TheNameKeyGenerator->nameToKey( playerName ) );
						if (player && localPlayer)
						{
							switch (inGameChatType)
							{
							case InGameChatType::INGAME_CHAT_EVERYONE:
								if (!TheGameInfo->getConstSlot(i)->isMuted())
									playerMask |= (1<<i);
								break;
							case InGameChatType::INGAME_CHAT_ALLIES:
								if ( (player->getRelationship(localPlayer->getDefaultTeam()) == ALLIES &&
									localPlayer->getRelationship(player->getDefaultTeam()) == ALLIES) || player==localPlayer )
									playerMask |= (1<<i);
								break;
							case InGameChatType::INGAME_CHAT_PLAYERS:
								if ( player == localPlayer )
									playerMask |= (1<<i);
								break;
							}
						}
					}
					TheLanguageFilter->filterLine(msg);
					TheNetwork->sendChat(msg, playerMask);
				}
				GadgetTextEntrySetText( chatTextEntry, UnicodeString::TheEmptyString );
				HideInGameChat( immediate );
				justHid = true;
			}
		}
	}
	else
	{
		ShowInGameChat( immediate );
	}
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType InGameChatInput( GameWindow *window, UnsignedInt msg,
																			WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = static_cast<UnsignedByte>(mData1);
//			UnsignedByte state = mData2;

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{
					HideInGameChat();
					return WindowMsgHandledType::MSG_HANDLED;
					//return MSG_IGNORED;
				}  // end escape

			}  // end switch( key )

			return WindowMsgHandledType::MSG_HANDLED;

		}  // end char

	}

	return WindowMsgHandledType::MSG_IGNORED;

}  // end InGameChatInput

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType InGameChatSystem( GameWindow *window, UnsignedInt msg, 
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg ) 
	{
		//---------------------------------------------------------------------------------------------
		case GGM_FOCUS_CHANGE:
		{
//			Bool focus = (Bool) mData1;
			//if (focus)
				//TheWindowManager->winSetGrabWindow( chatTextEntry );
			break;
		} // end focus change

		//---------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{	
			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*reinterpret_cast<Bool *>(mData2) = TRUE;

			return WindowMsgHandledType::MSG_HANDLED;
		}//case GWM_INPUT_FOCUS:

		//---------------------------------------------------------------------------------------------
		case GEM_EDIT_DONE:
		{
			ToggleInGameChat();
			//HideInGameChat();

			break;

		}  // end button selected

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
			static NameKeyType buttonClearID = TheNameKeyGenerator->nameToKey( AsciiString( "InGameChat.wnd:ButtonClear" ) );
			if (control && control->winGetWindowId() == buttonClearID)
			{
				if (chatTextEntry)
					GadgetTextEntrySetText( chatTextEntry, UnicodeString::TheEmptyString );
				s_savedChat.clear();
			}
			break;

		}  // end button selected

		//---------------------------------------------------------------------------------------------
		default:
			return WindowMsgHandledType::MSG_IGNORED;

	}  // end switch( msg )

	return WindowMsgHandledType::MSG_HANDLED;

}  // end InGameChatSystem

