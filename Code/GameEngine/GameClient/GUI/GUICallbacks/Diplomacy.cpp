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

// FILE: Diplomacy.cpp ///////////////////////////////////////////////////////////////////////
// Author: Matthew D. Campbell - August 2002
// Desc: GUI callbacks for the diplomacy menu
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GlobalData.h"
#include "Common/MultiplayerSettings.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Recorder.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/Diplomacy.h"
#include "GameClient/DisconnectMenu.h"
#include "GameClient/GameWindow.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetRadioButton.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameText.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/VictoryConditions.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/NetworkInterface.h"

//-------------------------------------------------------------------------------------------------

static NameKeyType staticTextPlayerID[MAX_SLOTS];
static NameKeyType staticTextSideID[MAX_SLOTS];
static NameKeyType staticTextTeamID[MAX_SLOTS];
static NameKeyType staticTextStatusID[MAX_SLOTS];
static NameKeyType buttonMuteID[MAX_SLOTS];
static NameKeyType buttonUnMuteID[MAX_SLOTS];
static NameKeyType radioButtonInGameID = NAMEKEY_INVALID;
static NameKeyType radioButtonBuddiesID = NAMEKEY_INVALID;
static GameWindow *radioButtonInGame = nullptr;
static GameWindow *radioButtonBuddies = nullptr;
static NameKeyType winInGameID = NAMEKEY_INVALID;
static NameKeyType winBuddiesID = NAMEKEY_INVALID;
static NameKeyType winSoloID = NAMEKEY_INVALID;
static GameWindow *winInGame = nullptr;
static GameWindow *winBuddies = nullptr;
static GameWindow *winSolo = nullptr;
static GameWindow *staticTextPlayer[MAX_SLOTS] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
static GameWindow *staticTextSide[MAX_SLOTS] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
static GameWindow *staticTextTeam[MAX_SLOTS] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
static GameWindow *staticTextStatus[MAX_SLOTS] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
static GameWindow *buttonMute[MAX_SLOTS] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
static GameWindow *buttonUnMute[MAX_SLOTS] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
static Int slotNumInRow[MAX_SLOTS];

//-------------------------------------------------------------------------------------------------

static WindowLayout *theLayout = nullptr;
static GameWindow *theWindow = nullptr;
static AnimateWindowManager *theAnimateWindowManager = nullptr;
// Diplomacy.wnd from the game data names WOLBuddyOverlayInit/Update/Shutdown as layout callbacks.
// There is no buddy list, so they are empty functions that let the window load.
void WOLBuddyOverlayInit( WindowLayout *layout, void *userData )
{
}
void WOLBuddyOverlayUpdate( WindowLayout *layout, void *userData )
{
}
void WOLBuddyOverlayShutdown( WindowLayout *layout, void *userData )
{
}
static void grabWindowPointers( void )
{
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		AsciiString temp;
		temp.format("Diplomacy.wnd:StaticTextPlayer%d", i);
		staticTextPlayerID[i] = NAMEKEY(temp);
		temp.format("Diplomacy.wnd:StaticTextSide%d", i);
		staticTextSideID[i] = NAMEKEY(temp);
		temp.format("Diplomacy.wnd:StaticTextTeam%d", i);
		staticTextTeamID[i] = NAMEKEY(temp);
		temp.format("Diplomacy.wnd:StaticTextStatus%d", i);
		staticTextStatusID[i] = NAMEKEY(temp);
		temp.format("Diplomacy.wnd:ButtonMute%d", i);
		buttonMuteID[i] = NAMEKEY(temp);
		temp.format("Diplomacy.wnd:ButtonUnMute%d", i);
		buttonUnMuteID[i] = NAMEKEY(temp);

		staticTextPlayer[i] = TheWindowManager->winGetWindowFromId(theWindow, staticTextPlayerID[i]);
		staticTextSide[i] = TheWindowManager->winGetWindowFromId(theWindow, staticTextSideID[i]);
		staticTextTeam[i] = TheWindowManager->winGetWindowFromId(theWindow, staticTextTeamID[i]);
		staticTextStatus[i] = TheWindowManager->winGetWindowFromId(theWindow, staticTextStatusID[i]);
		buttonMute[i] = TheWindowManager->winGetWindowFromId(theWindow, buttonMuteID[i]);
		buttonUnMute[i] = TheWindowManager->winGetWindowFromId(theWindow, buttonUnMuteID[i]);

		slotNumInRow[i] = -1;
	}
}

static void releaseWindowPointers( void )
{
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		staticTextPlayer[i] = nullptr;
		staticTextSide[i] = nullptr;
		staticTextTeam[i] = nullptr;
		staticTextStatus[i] = nullptr;
		buttonMute[i] = nullptr;
		buttonUnMute[i] = nullptr;

		slotNumInRow[i] = -1;
	}
}


//-------------------------------------------------------------------------------------------------

static void updateFunc( WindowLayout *layout, void *param )
{
	if (theAnimateWindowManager && TheGlobalData->m_animateWindows)
	{
		Bool wasFinished = theAnimateWindowManager->isFinished();
		theAnimateWindowManager->update();
		if (theAnimateWindowManager->isFinished() && !wasFinished && theAnimateWindowManager->isReversed())
			theWindow->winHide( TRUE );
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
static BriefingList theBriefingList;

//-------------------------------------------------------------------------------------------------
BriefingList* GetBriefingTextList(void)
{
	return &theBriefingList;
}

//-------------------------------------------------------------------------------------------------
void UpdateDiplomacyBriefingText(AsciiString newText, Bool clear)
{
	GameWindow *listboxSolo = TheWindowManager->winGetWindowFromId(theWindow, NAMEKEY("Diplomacy.wnd:ListboxSolo"));

	if (clear)
	{
		theBriefingList.clear();
		if (listboxSolo)
			GadgetListBoxReset(listboxSolo);
	}

	if (newText.isEmpty())
		return;

	if (std::find(theBriefingList.begin(), theBriefingList.end(), newText) != theBriefingList.end())
		return;

	theBriefingList.push_back(newText);
	if (!listboxSolo)
		return;

	UnicodeString translated = TheGameText->fetch(newText);

	Int numEntries = GadgetListBoxGetNumEntries(listboxSolo);
	GadgetListBoxAddEntryText(listboxSolo, translated, TheInGameUI->getMessageColor(numEntries%2), -1);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ShowDiplomacy( Bool immediate )
{
	if (!TheInGameUI->getInputEnabled() || TheGameLogic->isIntroMoviePlaying() || 
			TheGameLogic->isLoadingMap())
		return;
	

	if (TheInGameUI->isQuitMenuVisible())
		return;

	if (TheDisconnectMenu && TheDisconnectMenu->isScreenVisible())
		return;

	if (theWindow)
	{
		theWindow->winHide(FALSE);
		theWindow->winEnable(TRUE);
	}
	else
	{
		theLayout = TheWindowManager->winCreateLayout( "Diplomacy.wnd" );
		theWindow = theLayout->getFirstWindow();
		theLayout->setUpdate(updateFunc);
		theAnimateWindowManager = NEW AnimateWindowManager;
		radioButtonInGameID = TheNameKeyGenerator->nameToKey("Diplomacy.wnd:RadioButtonInGame");
		radioButtonBuddiesID = TheNameKeyGenerator->nameToKey("Diplomacy.wnd:RadioButtonBuddies");
		radioButtonInGame = TheWindowManager->winGetWindowFromId(nullptr, radioButtonInGameID);
		radioButtonBuddies = TheWindowManager->winGetWindowFromId(nullptr, radioButtonBuddiesID);
		winInGameID = TheNameKeyGenerator->nameToKey("Diplomacy.wnd:InGameParent");
		winBuddiesID = TheNameKeyGenerator->nameToKey("Diplomacy.wnd:BuddiesParent");
		winSoloID = TheNameKeyGenerator->nameToKey("Diplomacy.wnd:SoloParent");
		winInGame = TheWindowManager->winGetWindowFromId(nullptr, winInGameID);
		winBuddies = TheWindowManager->winGetWindowFromId(nullptr, winBuddiesID);
		winSolo = TheWindowManager->winGetWindowFromId(nullptr, winSoloID);

		if (!TheRecorder->isMultiplayer())
		{
			GameWindow *listboxSolo = TheWindowManager->winGetWindowFromId(theWindow, NAMEKEY("Diplomacy.wnd:ListboxSolo"));
			if (listboxSolo)
			{
				for (BriefingList::iterator it = theBriefingList.begin(); it != theBriefingList.end(); ++it)
				{
					UnicodeString translated = TheGameText->fetch(*it);
					Int numEntries = GadgetListBoxGetNumEntries(listboxSolo);
					GadgetListBoxAddEntryText(listboxSolo, translated, TheInGameUI->getMessageColor(numEntries%2), -1);
				}
			}
		}
	}
	theLayout->hide(FALSE);

	radioButtonInGame->winHide(TRUE);
	radioButtonBuddies->winHide(TRUE);
	GadgetRadioSetSelection(radioButtonInGame, FALSE);
	if (TheRecorder->isMultiplayer())
	{
		winInGame->winHide(FALSE);
		winBuddies->winHide(TRUE);
		winSolo->winHide(TRUE);
	}
	else
	{
		winInGame->winHide(TRUE);
		winBuddies->winHide(TRUE);
		winSolo->winHide(FALSE);
	}

	theAnimateWindowManager->reset();
	if (!immediate && TheGlobalData->m_animateWindows)
		theAnimateWindowManager->registerGameWindow( theWindow, AnimTypes::WIN_ANIMATION_SLIDE_TOP, TRUE, 200 );

	TheInGameUI->registerWindowLayout(theLayout);
	grabWindowPointers();
	PopulateInGameDiplomacyPopup();


}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ResetDiplomacy( void )
{
	if(theLayout)
	{
		TheInGameUI->unregisterWindowLayout(theLayout);
		theLayout->destroyWindows();
		deleteInstanceOf(theLayout);
	}
	theLayout = nullptr;
	theWindow = nullptr;
	if (theAnimateWindowManager)
		delete theAnimateWindowManager;
	theAnimateWindowManager = nullptr;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void HideDiplomacy( Bool immediate )
{
	releaseWindowPointers();
	if (theWindow)
	{
		if (immediate || !TheGlobalData->m_animateWindows)
		{
			theWindow->winHide(TRUE);
			theWindow->winEnable(FALSE);
		}
		else
		{
			if (theAnimateWindowManager->isFinished())
				theAnimateWindowManager->reverseAnimateWindow();
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ToggleDiplomacy( Bool immediate )
{
	// If we bring this up, let's hide the quit menu
	HideQuitMenu();

	if (theWindow)
	{
		Bool show = theWindow->winIsHidden();
		if (show)
			ShowDiplomacy( immediate );
		else
			HideDiplomacy( immediate );
	}
	else
	{
		ShowDiplomacy( immediate );
	}
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType DiplomacyInput( GameWindow *window, UnsignedInt msg,
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
					HideDiplomacy();
					return WindowMsgHandledType::MSG_HANDLED;
					//return MSG_IGNORED;
				}  // end escape

			}  // end switch( key )

			return WindowMsgHandledType::MSG_HANDLED;

		}  // end char

	}

	return WindowMsgHandledType::MSG_IGNORED;

}  // end DiplomacyInput

//-------------------------------------------------------------------------------------------------
WindowMsgHandledType DiplomacySystem( GameWindow *window, UnsignedInt msg, 
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
			// if we're given the opportunity to take the keyboard focus we must say we don't want it
			if( mData1 == TRUE )
				*reinterpret_cast<Bool *>(mData2) = FALSE;

			return WindowMsgHandledType::MSG_HANDLED;
		}//case GWM_INPUT_FOCUS:

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
			NameKeyType controlID = static_cast<NameKeyType>(control->winGetWindowId());
			static NameKeyType buttonHideID = NAMEKEY( "Diplomacy.wnd:ButtonHide" );
			if (controlID == buttonHideID)
			{
				HideDiplomacy( FALSE );
			}
			else if( controlID == radioButtonInGameID)
			{
				winInGame->winHide(FALSE);
				winBuddies->winHide(TRUE);
			}
			else if( controlID == radioButtonBuddiesID)
			{
				winInGame->winHide(TRUE);
				winBuddies->winHide(FALSE);
			}

			for (Int i=0; i<MAX_SLOTS; ++i)
			{
				if (controlID == buttonMuteID[i] && slotNumInRow[i] >= 0)
				{
					TheGameInfo->getSlot(slotNumInRow[i])->mute(TRUE);
					PopulateInGameDiplomacyPopup();
					break;
				}
				if (controlID == buttonUnMuteID[i] && slotNumInRow[i] >= 0)
				{
					TheGameInfo->getSlot(slotNumInRow[i])->mute(FALSE);
					PopulateInGameDiplomacyPopup();
					break;
				}
			}
			break;

		}  // end button selected

		//---------------------------------------------------------------------------------------------
		default:
			return WindowMsgHandledType::MSG_IGNORED;

	}  // end switch( msg )

	return WindowMsgHandledType::MSG_HANDLED;

}  // end DiplomacySystem

void PopulateInGameDiplomacyPopup( void )
{
	// no players yet (before the game, or without a game logic as in network tests)
	if (!TheGameInfo || !ThePlayerList || !TheVictoryConditions)
		return;

	Int rowNum = 0;
	for (Int slotNum=0; slotNum<MAX_SLOTS; ++slotNum)
	{
		const GameSlot *slot = TheGameInfo->getConstSlot(slotNum);
		if (slot && slot->isOccupied())
		{
			Bool isInGame = false;
			// Note - for skirmish, TheNetwork == NULL.  jba.
			if (TheNetwork &&	TheNetwork->isPlayerConnected(slotNum)) {
				isInGame = true;
			} else if ((TheNetwork == nullptr) && slot->isHuman()) {
				// this is a skirmish game and it is the human player.
				isInGame = true;
			}
			if (slot->isAI())
				isInGame = true;
			AsciiString playerName;
			playerName.format("player%d", slotNum);
			Player *player = ThePlayerList->findPlayerWithNameKey(NAMEKEY(playerName));
			if (player == nullptr)
				continue;
			Bool isAlive = !TheVictoryConditions->hasSinglePlayerBeenDefeated(player);
			Bool isObserver = player->isPlayerObserver();

			if (slot->isHuman() && TheGameInfo->getLocalSlotNum() != slotNum && isInGame)
			{
				// show mute button
				if (buttonMute[rowNum])
				{
					buttonMute[rowNum]->winHide(slot->isMuted());
				}
				if (buttonUnMute[rowNum])
				{
					buttonUnMute[rowNum]->winHide(!slot->isMuted());
				}
			}
			else
			{
				// can't mute self, AI players, or MIA humans
				if (buttonMute[rowNum])
					buttonMute[rowNum]->winHide(TRUE);
				if (buttonUnMute[rowNum])
					buttonUnMute[rowNum]->winHide(TRUE);
			}

			Color playerColor = TheMultiplayerSettings->getColor(slot->getApparentColor())->getColor();
			Color backColor = GameMakeColor(0, 0, 0, 255);
			Color aliveColor = GameMakeColor(0, 255, 0, 255);
			Color deadColor = GameMakeColor(255, 0, 0, 255);
			Color observerInGameColor = GameMakeColor(255, 255, 255, 255);
			Color goneColor = GameMakeColor(196, 0, 0, 255);
			Color observerGoneColor = GameMakeColor(196, 196, 196, 255);

			if (staticTextPlayer[rowNum])
			{
				staticTextPlayer[rowNum]->winSetEnabledTextColors( playerColor, backColor );
				GadgetStaticTextSetText(staticTextPlayer[rowNum], slot->getName());
			}
			if (staticTextSide[rowNum])
			{
				staticTextSide[rowNum]->winSetEnabledTextColors( playerColor, backColor );
				GadgetStaticTextSetText(staticTextSide[rowNum], slot->getApparentPlayerTemplateDisplayName() );
			}
			if (staticTextTeam[rowNum])
			{
				staticTextTeam[rowNum]->winSetEnabledTextColors( playerColor, backColor );
				AsciiString teamStr;
				teamStr.format("Team:%d", slot->getTeamNumber() + 1);
				if (slot->isAI() && slot->getTeamNumber() == -1)
					teamStr = "Team:AI";
				GadgetStaticTextSetText(staticTextTeam[rowNum], TheGameText->fetch(teamStr) );
			}
			if (staticTextStatus[rowNum])
			{
				staticTextStatus[rowNum]->winHide(FALSE);
				if (isInGame)
				{
					if (isAlive)
					{
						staticTextStatus[rowNum]->winSetEnabledTextColors( aliveColor, backColor );
						GadgetStaticTextSetText(staticTextStatus[rowNum], TheGameText->fetch("GUI:PlayerAlive"));
					}
					else
					{
						if (isObserver)
						{
							staticTextStatus[rowNum]->winSetEnabledTextColors( observerInGameColor, backColor );
							GadgetStaticTextSetText(staticTextStatus[rowNum], TheGameText->fetch("GUI:PlayerObserver"));
						}
						else
						{
							staticTextStatus[rowNum]->winSetEnabledTextColors( deadColor, backColor );
							GadgetStaticTextSetText(staticTextStatus[rowNum], TheGameText->fetch("GUI:PlayerDead"));
						}
					}
				}
				else
				{
					// not in game
					if (isObserver)
					{
						staticTextStatus[rowNum]->winSetEnabledTextColors( observerGoneColor, backColor );
						GadgetStaticTextSetText(staticTextStatus[rowNum], TheGameText->fetch("GUI:PlayerObserverGone"));
					}
					else
					{
						staticTextStatus[rowNum]->winSetEnabledTextColors( goneColor, backColor );
						GadgetStaticTextSetText(staticTextStatus[rowNum], TheGameText->fetch("GUI:PlayerGone"));
					}
				}
			}

			slotNumInRow[rowNum++] = slotNum;
		}
	}

	while (rowNum < MAX_SLOTS)
	{
		slotNumInRow[rowNum] = -1;
		if (staticTextPlayer[rowNum])
			staticTextPlayer[rowNum]->winHide(TRUE);
		if (staticTextSide[rowNum])
			staticTextSide[rowNum]->winHide(TRUE);
		if (staticTextTeam[rowNum])
			staticTextTeam[rowNum]->winHide(TRUE);
		if (staticTextStatus[rowNum])
			staticTextStatus[rowNum]->winHide(TRUE);
		if (buttonMute[rowNum])
			buttonMute[rowNum]->winHide(TRUE);
		if (buttonUnMute[rowNum])
			buttonUnMute[rowNum]->winHide(TRUE);

		++rowNum;
	}
}



