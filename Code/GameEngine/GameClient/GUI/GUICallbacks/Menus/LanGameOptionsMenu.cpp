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
// FILE: LanGameOptionsMenu.cpp
// Author: Chris Huybregts, October 2001
// Description: Lan Game Options Menu
///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine


#include "Common/PlayerTemplate.h"
#include "Common/GameEdition.h"
#include "Common/GameEngine.h"
#include "Common/UserSettings.h"
#include "Common/QuotedPrintable.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/ChallengeGenerals.h"

#include "GameNetwork/FirewallHelper.h"
#include "GameNetwork/GameLobby.h"
#include "GameNetwork/IPEnumeration.h"
#include "GameNetwork/GameLobbyCallbacks.h"
#include "Common/MultiplayerSettings.h"
#include "GameClient/GameText.h"
#include "GameNetwork/GUIUtil.h"

extern char *LANnextScreen;
extern Bool LANisShuttingDown;
extern Bool LANbuttonPushed;
extern void MapSelectorTooltip(GameWindow *window, WinInstanceData *instData,	UnsignedInt mouse);
// Empty tooltip callbacks: a set callback suppresses the window's default tooltip, which keeps the
// menus as they were.
static void playerTemplateComboBoxTooltip( GameWindow *, WinInstanceData *, UnsignedInt ) {}
static void playerTemplateListBoxTooltip( GameWindow *, WinInstanceData *, UnsignedInt ) {}
static void gameAcceptTooltip( GameWindow *, WinInstanceData *, UnsignedInt ) {}
Color white = GameMakeColor( 255, 255, 255, 255 );
static bool s_isIniting = FALSE;
// window ids ------------------------------------------------------------------------------
static NameKeyType parentLanGameOptionsID = NAMEKEY_INVALID;

static NameKeyType comboBoxPlayerID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType buttonAcceptID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType comboBoxColorID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType comboBoxPlayerTemplateID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType comboBoxTeamID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

//static NameKeyType buttonStartPositionID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
//																										NAMEKEY_INVALID,NAMEKEY_INVALID,
//																										NAMEKEY_INVALID,NAMEKEY_INVALID,
//																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType buttonMapStartPositionID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType textEntryChatID = NAMEKEY_INVALID;
static NameKeyType textEntryMapDisplayID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType buttonStartID = NAMEKEY_INVALID;
static NameKeyType buttonEmoteID = NAMEKEY_INVALID;
static NameKeyType buttonSelectMapID = NAMEKEY_INVALID;
static NameKeyType checkboxLimitSuperweaponsID = NAMEKEY_INVALID;
static NameKeyType checkboxListPubliclyID = NAMEKEY_INVALID;
static NameKeyType comboBoxStartingCashID = NAMEKEY_INVALID;
static NameKeyType comboBoxGraceID = NAMEKEY_INVALID;
static NameKeyType comboBoxMaxZoomID = NAMEKEY_INVALID;
static NameKeyType windowMapID = NAMEKEY_INVALID;
// Window Pointers ------------------------------------------------------------------------
static GameWindow *parentLanGameOptions = nullptr;
static GameWindow *buttonBack = nullptr;
static GameWindow *buttonStart = nullptr;
static GameWindow *buttonSelectMap = nullptr;
static GameWindow *buttonEmote = nullptr;
static GameWindow *textEntryChat = nullptr;
static GameWindow *textEntryMapDisplay = nullptr;
static GameWindow *checkboxLimitSuperweapons = nullptr;
static GameWindow *checkboxListPublicly = nullptr;
static GameWindow *comboBoxStartingCash = nullptr;
static GameWindow *comboBoxGrace = nullptr;
static GameWindow *comboBoxMaxZoom = nullptr;
static GameWindow *windowMap = nullptr;

static GameWindow *comboBoxPlayer[MAX_SLOTS] = {nullptr,nullptr,nullptr,nullptr,
																									 nullptr,nullptr,nullptr,nullptr };
static GameWindow *buttonAccept[MAX_SLOTS] = {nullptr,nullptr,nullptr,nullptr,
																								nullptr,nullptr,nullptr,nullptr };

static GameWindow *comboBoxColor[MAX_SLOTS] = {nullptr,nullptr,nullptr,nullptr,
																								nullptr,nullptr,nullptr,nullptr };

static GameWindow *comboBoxPlayerTemplate[MAX_SLOTS] = {nullptr,nullptr,nullptr,nullptr,
																								nullptr,nullptr,nullptr,nullptr };

static GameWindow *comboBoxTeam[MAX_SLOTS] = {nullptr,nullptr,nullptr,nullptr,
																								nullptr,nullptr,nullptr,nullptr };

//static GameWindow *buttonStartPosition[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
//																								NULL,NULL,NULL,NULL };
//
static GameWindow *buttonMapStartPosition[MAX_SLOTS] = {nullptr,nullptr,nullptr,nullptr,
																								nullptr,nullptr,nullptr,nullptr };

//external declarations of the Gadgets the callbacks can use
GameWindow *listboxChatWindowLanGame = nullptr;
NameKeyType listboxChatWindowLanGameID = NAMEKEY_INVALID;
WindowLayout *mapSelectLayout = nullptr;

static Int getNextSelectablePlayer(Int start)
{
	LANGameInfo *game = TheLobby->GetMyGame();
	if (!game->amIHost())
		return -1;
	for (Int j=start; j<MAX_SLOTS; ++j)
	{
		LANGameSlot *slot = game->getLANSlot(j);
		if (slot && slot->getStartPos() == -1 &&
			( (j==game->getLocalSlotNum() && game->getConstSlot(j)->getPlayerTemplate()!=PLAYERTEMPLATE_OBSERVER)
			|| slot->isAI()))
		{
			return j;
		}
	}
	return -1;
}

static Int getFirstSelectablePlayer(const GameInfo *game)
{
	const GameSlot *slot = game->getConstSlot(game->getLocalSlotNum());
	if (!game->amIHost() || slot && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
		return game->getLocalSlotNum();

	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		slot = game->getConstSlot(i);
		if (slot && slot->isAI())
			return i;
	}

	return game->getLocalSlotNum();
}

void updateMapStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], Bool onLoadScreen = FALSE );
void positionStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);
void LanPositionStartSpots( void )
{
	
	positionStartSpots( TheLobby->GetMyGame(), buttonMapStartPosition, windowMap);
}
static void playerTooltip(GameWindow *window,
													WinInstanceData *instData,
													UnsignedInt mouse)
{
	Int idx = -1;
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		if (window && window == GadgetComboBoxGetEditBox(comboBoxPlayer[i]))
		{
			idx = i;
			break;
		}
	}
	if (idx == -1)
		return;

	// After "Back" (or when the host closes the game) our game is already freed by
	// GameLobby::RequestGameLeave while the menu is still fading out, so GetMyGame() may be null here.
	LANGameInfo *myGame = TheLobby ? TheLobby->GetMyGame() : nullptr;
	if (!myGame)
		return;
	LANGameSlot *slot = myGame->getLANSlot(idx);
	if (!slot)
		return;

	LANPlayer *player = slot->getUser();
	if (!player)
	{
		DEBUG_ASSERTCRASH(myGame->getIP(idx) == 0, ("No player info in listbox!"));
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString );
		return;
	}
	UnicodeString tooltip;
	tooltip.format(TheGameText->fetch("TOOLTIP:LANPlayer"), player->getName().str(), player->getLogin().str(), player->getHost().str());
	TheMouse->setCursorTooltip( tooltip );
}

void StartPressed(void)
{
	LANGameInfo *myGame = TheLobby->GetMyGame();

	Bool isReady = true;
	Bool allHaveMap = true;
	Int playerCount = 0;
	if (!myGame)
	{
		return;
	}
	myGame->getLANSlot(0)->setAccept(); // cause we are, of course!

	int i;

	int numUsers = 0;
	int numHumans = 0;
	for (i=0; i<MAX_SLOTS; ++i)
	{
		GameSlot *slot = myGame->getSlot(i);
		if (slot && slot->isOccupied() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
		{
			if (slot && slot->isHuman())
				numHumans++;
			numUsers++;
		}
	}

	// Check for too many players
	const MapMetaData *md = TheMapCache->findMap( myGame->getMap() );
	if (!md || md->m_numPlayers < numUsers)
	{
		if (TheLobby->AmIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:TooManyPlayers"), (md)?md->m_numPlayers:0);
			TheLobby->OnChat(UnicodeString(L"SYSTEM"), TheLobby->GetLocalIP(), text, GameLobby::LANCHAT_SYSTEM);
		}
		return;
	}

	// Check for observer + AI players
	if (TheGlobalData->m_netMinPlayers && !numHumans)
	{
		if (TheLobby->AmIHost())
		{
			UnicodeString text = TheGameText->fetch("GUI:NeedHumanPlayers");
			TheLobby->OnChat(UnicodeString(L"SYSTEM"), TheLobby->GetLocalIP(), text, GameLobby::LANCHAT_SYSTEM);
		}
		return;
	}

	// Check for too few players
	if (numUsers < TheGlobalData->m_netMinPlayers)
	{
		if (TheLobby->AmIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:NeedMorePlayers"),numUsers);
			TheLobby->OnChat(UnicodeString(L"SYSTEM"), TheLobby->GetLocalIP(), text, GameLobby::LANCHAT_SYSTEM);
		}
		return;
	}

	// Check for too few teams
	int numRandom = 0;
	std::set<Int> teams; 
	for (i=0; i<MAX_SLOTS; ++i)
	{
		GameSlot *slot = myGame->getSlot(i);
		if (slot && slot->isOccupied() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
		{
			if (slot->getTeamNumber() >= 0)
			{
				teams.insert(slot->getTeamNumber());
			}
			else
			{
				++numRandom;
			}
		}
	}
	if (numRandom + teams.size() < TheGlobalData->m_netMinPlayers)
	{
		if (TheLobby->AmIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:NeedMoreTeams"));
			TheLobby->OnChat(UnicodeString(L"SYSTEM"), TheLobby->GetLocalIP(), text, GameLobby::LANCHAT_SYSTEM);
		}
		return;
	}

	if (numRandom + teams.size() < 2)
	{
		UnicodeString text;
		text.format(TheGameText->fetch("GUI:SandboxMode"));
			TheLobby->OnChat(UnicodeString(L"SYSTEM"), TheLobby->GetLocalIP(), text, GameLobby::LANCHAT_SYSTEM);
	}

	// see if everyone's accepted and count the number of players in the game
	UnicodeString mapDisplayName;
	const MapMetaData *mapData = TheMapCache->findMap( myGame->getMap() );
	Bool willTransfer = TRUE;
	if (mapData)
	{
		mapDisplayName.format(L"%ls", mapData->m_displayName.str());
		willTransfer = !mapData->m_isOfficial;
	}
	else
	{
		mapDisplayName.format(L"%hs", myGame->getMap().str());
		willTransfer = WouldMapTransfer(myGame->getMap());
	}
	for( i = 0; i < MAX_SLOTS; i++ )
	{
		LANGameSlot *slot = myGame->getLANSlot(i);
		if( slot->isHuman() && !slot->isAccepted())
		{
			isReady = false;
			if (!willTransfer)
			{
				if (!slot->hasMap())
				{
					UnicodeString msg;
					msg.format(TheGameText->fetch("GUI:PlayerNoMap"), slot->getName().str(), mapDisplayName.str());
					GadgetListBoxAddEntryText(listboxChatWindowLanGame, msg , chatSystemColor, -1, 0);
					allHaveMap = false;
				}
			}
		}
		if( slot->isHuman() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER )
			playerCount++;
	}

	if(isReady)
	{
		for( i = 0; i < MAX_SLOTS; i++ )
		{
			LANGameSlot *slot = myGame->getLANSlot(i);
			if (slot && slot->isOpen())
			{
				slot->setState( SLOT_CLOSED );
				GadgetComboBoxSetSelectedPos(comboBoxPlayer[i], SLOT_CLOSED);
			}
		}
		Int seconds = TheMultiplayerSettings->getStartCountdownTimerSeconds();
		if (seconds)
			TheLobby->RequestGameStartTimer(seconds);
		else
			TheLobby->RequestGameStart();
		LANEnableStartButton(false);
	}
	else
	{
		// Does everyone have the map?
		if (allHaveMap)
		{
			GadgetListBoxAddEntryText(listboxChatWindowLanGame, TheGameText->fetch("GUI:NotifiedStartIntent") , chatSystemColor, -1, 0);
			TheLobby->RequestAccept();
		}
	}

}//void StartPressed(void)

// The lobby calls this whenever a player leaves, also while the menu is not up.
void LANEnableStartButton(Bool enabled)
{
	if (buttonStart)
		buttonStart->winEnable(enabled);
	if (buttonSelectMap)
		buttonSelectMap->winEnable(enabled);
}

static void handleColorSelection(int index)
{
	GameWindow *combo = comboBoxColor[index];
	Int color, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	color = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(combo, selIndex)));

	LANGameInfo *myGame = TheLobby->GetMyGame();

	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(index);
		if (color == slot->getColor())
			return;

		if (color >= -1 && color < TheMultiplayerSettings->getNumColors())
		{
			Bool colorAvailable = TRUE;
			if(color != -1 )
			{
				for(Int i=0; i <MAX_SLOTS; i++)
				{
					LANGameSlot *checkSlot = myGame->getLANSlot(i);
					if(color == checkSlot->getColor() && slot != checkSlot)
					{
						colorAvailable = FALSE;
						break;
					}
				}
			}
			if(!colorAvailable)
				return;
		}

		slot->setColor(color);

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				TheLobby->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the color from the host
			if (!slot->isLocalPlayer() || !AreSlotListUpdatesEnabled())
				return;

			AsciiString options;
			options.format("Color=%d", color);
			TheLobby->RequestGameOptions(options, true);
		}
	}
}

static void handlePlayerTemplateSelection(int index)
{
	GameWindow *combo = comboBoxPlayerTemplate[index];
	Int playerTemplate, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	playerTemplate = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(combo, selIndex)));
	LANGameInfo *myGame = TheLobby->GetMyGame();

	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(index);
		if (playerTemplate == slot->getPlayerTemplate())
			return;

		Int oldTemplate = slot->getPlayerTemplate();
		slot->setPlayerTemplate(playerTemplate);

		if (oldTemplate == PLAYERTEMPLATE_OBSERVER)
		{
			// was observer, so populate color & team with all, and enable
			GadgetComboBoxSetSelectedPos(comboBoxColor[index], 0);
			GadgetComboBoxSetSelectedPos(comboBoxTeam[index], 0);
			slot->setStartPos(-1);
		}
		else if (playerTemplate == PLAYERTEMPLATE_OBSERVER)
		{
			// is becoming observer, so populate color & team with random only, and disable
			GadgetComboBoxSetSelectedPos(comboBoxColor[index], 0);
			GadgetComboBoxSetSelectedPos(comboBoxTeam[index], 0);
			slot->setStartPos(-1);
		}

		myGame->resetAccepted();

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				TheLobby->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the playerTemplate from the host
			if (AreSlotListUpdatesEnabled())
			{
				AsciiString options;
				options.format("PlayerTemplate=%d", playerTemplate);
				TheLobby->RequestGameOptions(options, true);
			}
		}
	}
}

static void handleStartPositionSelection(Int player, int startPos)
{
	LANGameInfo *myGame = TheLobby->GetMyGame();
	
	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(player);
		if (startPos == slot->getStartPos())
			return;
		Bool skip = FALSE;
		if (startPos < 0)
		{
			skip = TRUE;
		}

		if(!skip)
		{	
			Bool isAvailable = TRUE;
			for(Int i = 0; i < MAX_SLOTS; ++i)
			{
				if(i != player && myGame->getSlot(i)->getStartPos() == startPos)
				{
					isAvailable = FALSE;
					break;
				}
			}
			if( !isAvailable )
				return;
		}
		slot->setStartPos(startPos);

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				myGame->resetAccepted();
				TheLobby->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the color from the host
			if (AreSlotListUpdatesEnabled())
			{
				AsciiString options;
				options.format("StartPos=%d", slot->getStartPos());
				TheLobby->RequestGameOptions(options, true);
			}
		}
	}
}



static void handleTeamSelection(int index)
{
	GameWindow *combo = comboBoxTeam[index];
	Int team, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	team = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(combo, selIndex)));
	LANGameInfo *myGame = TheLobby->GetMyGame();

	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(index);
		if (team == slot->getTeamNumber())
			return;

		slot->setTeamNumber(team);
		myGame->resetAccepted();

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				myGame->resetAccepted();
				TheLobby->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the team from the host
			if (AreSlotListUpdatesEnabled())
			{
				AsciiString options;
				options.format("Team=%d", team);
				TheLobby->RequestGameOptions(options, true);
			}
		}
	}
}

static void handleStartingCashSelection()
{
  LANGameInfo *myGame = TheLobby->GetMyGame();
  
  if (myGame)
  {
    Int selIndex;
    GadgetComboBoxGetSelectedPos(comboBoxStartingCash, &selIndex);

    Money startingCash;
    startingCash.deposit( static_cast<UnsignedInt>(reinterpret_cast<uintptr_t>(GadgetComboBoxGetItemData( comboBoxStartingCash, selIndex ))), FALSE );
    myGame->setStartingCash( startingCash );
    myGame->resetAccepted();

    if (myGame->amIHost())
    {
      if (!s_isIniting)
      {
        // send around the new data
        TheLobby->RequestGameOptions(GenerateGameOptionsString(), true);
        lanUpdateSlotList(); // Update the accepted button UI
      }
    }
  }
}

static void handleGraceSelection()
{
  LANGameInfo *myGame = TheLobby->GetMyGame();

  if (myGame)
  {
    myGame->setGraceMinutes( GetGraceComboBoxMinutes( comboBoxGrace ) );
    myGame->resetAccepted();

    if (myGame->amIHost() && !s_isIniting)
    {
      // send around the new data
      TheLobby->RequestGameOptions(GenerateGameOptionsString(), true);
      lanUpdateSlotList(); // Update the accepted button UI
    }
  }
}

static void handleMaxZoomSelection()
{
  LANGameInfo *myGame = TheLobby->GetMyGame();

  if (myGame)
  {
    myGame->setMaxZoomPercent( GetMaxZoomComboBoxPercent( comboBoxMaxZoom ) );
    myGame->resetAccepted();

    if (myGame->amIHost() && !s_isIniting)
    {
      // send around the new data
      TheLobby->RequestGameOptions(GenerateGameOptionsString(), true);
      lanUpdateSlotList(); // Update the accepted button UI
    }
  }
}

static void handleLimitSuperweaponsClick()
{
  LANGameInfo *myGame = TheLobby->GetMyGame();
  
  if (myGame)
  {
    // At the moment, 1 and 0 are the only choices supported in the GUI, though the system could
    // support more.
    if ( GadgetCheckBoxIsChecked( checkboxLimitSuperweapons ) )
    {
      myGame->setSuperweaponRestriction( 1 );
    }
    else
    {
      myGame->setSuperweaponRestriction( 0 );
    }
    myGame->resetAccepted();
    
    if (myGame->amIHost())
    {
      if (!s_isIniting)
      {
        // send around a new slotlist
        TheLobby->RequestGameOptions(GenerateGameOptionsString(), true);
        lanUpdateSlotList(); // Update the accepted button UI
      }
    }
  }
}

void lanUpdateSlotList( void )
{
	if(!AreSlotListUpdatesEnabled() || s_isIniting)
		return;
	UpdateSlotList( TheLobby->GetMyGame(), comboBoxPlayer, comboBoxColor,
		comboBoxPlayerTemplate, comboBoxTeam, buttonAccept, buttonStart, buttonMapStartPosition);
	
	updateMapStartSpots(TheLobby->GetMyGame(), buttonMapStartPosition);
}

//-------------------------------------------------------------------------------------------------
/** Initialize the Gadgets Options Menu */
//-------------------------------------------------------------------------------------------------
void InitLanGameGadgets( void )
{
	//Initialize the gadget IDs
	parentLanGameOptionsID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:LanGameOptionsMenuParent" ) );
	buttonBackID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ButtonBack" ) );
	buttonStartID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ButtonStart" ) );
	textEntryChatID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:TextEntryChat" ) );
	textEntryMapDisplayID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:TextEntryMapDisplay" ) );
	listboxChatWindowLanGameID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ListboxChatWindowLanGame" ) );
	buttonEmoteID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ButtonEmote" ) );
	buttonSelectMapID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ButtonSelectMap" ) );
  checkboxLimitSuperweaponsID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:CheckboxLimitSuperweapons" ) );
  comboBoxStartingCashID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ComboBoxStartingCash" ) );
	windowMapID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:MapWindow" ) );

	// Initialize the pointers to our gadgets
	parentLanGameOptions = TheWindowManager->winGetWindowFromId( nullptr, parentLanGameOptionsID );
	DEBUG_ASSERTCRASH(parentLanGameOptions, ("Could not find the parentLanGameOptions"));
	buttonEmote = TheWindowManager->winGetWindowFromId( parentLanGameOptions,buttonEmoteID  );
	DEBUG_ASSERTCRASH(buttonEmote, ("Could not find the buttonEmote"));
	buttonSelectMap = TheWindowManager->winGetWindowFromId( parentLanGameOptions,buttonSelectMapID  );
	DEBUG_ASSERTCRASH(buttonSelectMap, ("Could not find the buttonSelectMap"));
	buttonStart = TheWindowManager->winGetWindowFromId( parentLanGameOptions,buttonStartID  );
	DEBUG_ASSERTCRASH(buttonStart, ("Could not find the buttonStart"));
	buttonBack = TheWindowManager->winGetWindowFromId( parentLanGameOptions,  buttonBackID);
	DEBUG_ASSERTCRASH(buttonBack, ("Could not find the buttonBack"));
	listboxChatWindowLanGame = TheWindowManager->winGetWindowFromId( parentLanGameOptions, listboxChatWindowLanGameID );
	DEBUG_ASSERTCRASH(listboxChatWindowLanGame, ("Could not find the listboxChatWindowLanGame"));
	textEntryChat = TheWindowManager->winGetWindowFromId( parentLanGameOptions, textEntryChatID );
	DEBUG_ASSERTCRASH(textEntryChat, ("Could not find the textEntryChat"));
	textEntryMapDisplay = TheWindowManager->winGetWindowFromId( parentLanGameOptions, textEntryMapDisplayID );
	DEBUG_ASSERTCRASH(textEntryMapDisplay, ("Could not find the textEntryMapDisplay"));
  // The Generals LanGameOptionsMenu.wnd has neither the superweapon limit nor the starting cash
  // selection; the defaults apply then.
  checkboxLimitSuperweapons = TheWindowManager->winGetWindowFromId( parentLanGameOptions, checkboxLimitSuperweaponsID );
  DEBUG_ASSERTCRASH(checkboxLimitSuperweapons || !isGameEditionZeroHour(), ("Could not find the checkboxLimitSuperweapons"));
  comboBoxStartingCash = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxStartingCashID );
  DEBUG_ASSERTCRASH(comboBoxStartingCash || !isGameEditionZeroHour(), ("Could not find the comboBoxStartingCash"));
	// Internet: the host decides whether the rendezvous server lists the game. The window file has no
	// such box; it is made like the superweapon box (Generals has none: /public in the chat line).
	checkboxListPubliclyID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:CheckboxListPublicly" ) );
	checkboxListPublicly = TheWindowManager->winGetWindowFromId( parentLanGameOptions, checkboxListPubliclyID );
	const Bool offerListing = TheLobby->mode() == GameLobby::Mode::Internet && TheLobby->AmIHost();
	if (checkboxListPublicly == nullptr && offerListing)
		checkboxListPublicly = GadgetCheckBoxCreateBelow( checkboxLimitSuperweapons, "LanGameOptionsMenu.wnd:CheckboxListPublicly", "GZH:InternetListPublicly" );
	if (checkboxListPublicly)
	{
		checkboxListPublicly->winHide( !offerListing );
		GadgetCheckBoxSetChecked( checkboxListPublicly, TheLobby->listedPublicly() );
	}
	PopulateStartingCashComboBox(comboBoxStartingCash, TheLobby->GetMyGame());

	// The start protection choice is not in the layouts: next to the starting cash (Zero Hour) or below
	// the team column (Generals).
	comboBoxGraceID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ComboBoxGracePeriod" ) );
	comboBoxGrace = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxGraceID );
	if (comboBoxGrace == nullptr)
	{
		GameWindow *model = comboBoxStartingCash;
		if (model == nullptr)
			model = TheWindowManager->winGetWindowFromId( parentLanGameOptions,
				TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ComboBoxTeam7" ) ) );
		comboBoxGrace = CreateGraceComboBox( model, "LanGameOptionsMenu.wnd:ComboBoxGracePeriod" );
	}

	// The host's zoom-out limit for every player, not in the layouts either: next to the start protection.
	comboBoxMaxZoomID = TheNameKeyGenerator->nameToKey( AsciiString( "LanGameOptionsMenu.wnd:ComboBoxMaxZoom" ) );
	comboBoxMaxZoom = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxMaxZoomID );
	if (comboBoxMaxZoom == nullptr && comboBoxGrace != nullptr)
		comboBoxMaxZoom = CreateMaxZoomComboBox( comboBoxGrace, "LanGameOptionsMenu.wnd:ComboBoxMaxZoom" );

	// Filled after both exist: filling widens a box into free room, and the zoom box has to have its place by then.
	PopulateGraceComboBox( comboBoxGrace, TheLobby->GetMyGame()->getGraceMinutes() );
	PopulateMaxZoomComboBox( comboBoxMaxZoom, TheLobby->GetMyGame()->getMaxZoomPercent() );

	windowMap = TheWindowManager->winGetWindowFromId( parentLanGameOptions,windowMapID  );
	DEBUG_ASSERTCRASH(windowMap, ("Could not find the LanGameOptionsMenu.wnd:MapWindow" ));

	Int localSlotNum = TheLobby->GetMyGame()->getLocalSlotNum();
	DEBUG_ASSERTCRASH(localSlotNum >= 0, ("Bad slot number!"));

	//Added By Sadullah Nader
	//Tooltip function is being set for techBuildings, and supplyDocks

	windowMap->winSetTooltipFunc(MapSelectorTooltip);
	//End Add

	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		AsciiString tmpString;
		tmpString.format("LanGameOptionsMenu.wnd:ComboBoxPlayer%d", i);
		comboBoxPlayerID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxPlayer[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxPlayerID[i] );
		GadgetComboBoxReset(comboBoxPlayer[i]);
		GadgetComboBoxGetEditBox(comboBoxPlayer[i])->winSetTooltipFunc(playerTooltip);

		if(localSlotNum == i)
		{
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheLobby->GetMyName(),white);
		}
		else
		{
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:Open"),white);
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:Closed"),white);
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:EasyAI"),white);
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:MediumAI"),white);
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:HardAI"),white);
			AddNewAIComboEntries(comboBoxPlayer[i],white);
			GadgetComboBoxSetSelectedPos(comboBoxPlayer[i],0);
		}
		/*
		if(i != 0)
		{
			TheLobby->GetMyGame()->getLANSlot(i)->setState(SLOT_OPEN);
		}
		*/

		tmpString.format("LanGameOptionsMenu.wnd:ComboBoxColor%d", i);
		comboBoxColorID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxColor[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxColorID[i] );
		DEBUG_ASSERTCRASH(comboBoxColor[i], ("Could not find the comboBoxColor[%d]",i ));
		PopulateColorComboBox(i, comboBoxColor, TheLobby->GetMyGame());
		GadgetComboBoxSetSelectedPos(comboBoxColor[i], 0);
		
		tmpString.format("LanGameOptionsMenu.wnd:ComboBoxPlayerTemplate%d", i);
		comboBoxPlayerTemplateID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxPlayerTemplate[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxPlayerTemplateID[i] );
		DEBUG_ASSERTCRASH(comboBoxPlayerTemplate[i], ("Could not find the comboBoxPlayerTemplate[%d]",i ));
		PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, TheLobby->GetMyGame(), TRUE);

		// add tooltips to the player template combobox and listbox
		comboBoxPlayerTemplate[i]->winSetTooltipFunc(playerTemplateComboBoxTooltip);
		GadgetComboBoxGetListBox(comboBoxPlayerTemplate[i])->winSetTooltipFunc(playerTemplateListBoxTooltip);

		tmpString.format("LanGameOptionsMenu.wnd:ComboBoxTeam%d", i);
		comboBoxTeamID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxTeam[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxTeamID[i] );
		DEBUG_ASSERTCRASH(comboBoxTeam[i], ("Could not find the comboBoxTeam[%d]",i ));
		PopulateTeamComboBox(i, comboBoxTeam, TheLobby->GetMyGame());

		tmpString.clear();
		tmpString.format("LanGameOptionsMenu.wnd:ButtonAccept%d", i); 
		buttonAcceptID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonAccept[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, buttonAcceptID[i] );
		DEBUG_ASSERTCRASH(buttonAccept[i], ("Could not find the buttonAccept[%d]",i ));
		//Added by Saad for the tooltips on the MultiPlayer icons
		buttonAccept[i]->winSetTooltipFunc(gameAcceptTooltip);
//		
//		tmpString.format("LanGameOptionsMenu.wnd:ButtonStartPosition%d", i);
//		buttonStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
//		buttonStartPosition[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, buttonStartPositionID[i] );
//		DEBUG_ASSERTCRASH(buttonStartPosition[i], ("Could not find the ButtonStartPosition[%d]",i ));

		tmpString.format("LanGameOptionsMenu.wnd:ButtonMapStartPosition%d", i);
		buttonMapStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonMapStartPosition[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, buttonMapStartPositionID[i] );
		DEBUG_ASSERTCRASH(buttonMapStartPosition[i], ("Could not find the ButtonMapStartPosition[%d]",i ));

		if(i !=0 && buttonAccept[i])
			buttonAccept[i]->winHide(TRUE);
	}
	if( buttonAccept[0] )
		GadgetButtonSetEnabledColor(buttonAccept[0], acceptTrueColor );
	
}

void DeinitLanGameGadgets( void )
{
	parentLanGameOptions = nullptr;
	buttonEmote = nullptr;
	buttonSelectMap = nullptr;
	buttonStart = nullptr;
	buttonBack = nullptr;
	listboxChatWindowLanGame = nullptr;
	textEntryChat = nullptr;
	textEntryMapDisplay = nullptr;
  checkboxLimitSuperweapons = nullptr;
  checkboxListPublicly = nullptr;
  comboBoxStartingCash = nullptr;
  comboBoxGrace = nullptr;
  comboBoxMaxZoom = nullptr;
	windowMap = nullptr;
	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		comboBoxPlayer[i] = nullptr;
		comboBoxColor[i] = nullptr;
		comboBoxPlayerTemplate[i] = nullptr;
		comboBoxTeam[i] = nullptr;
		buttonAccept[i] = nullptr;
//		buttonStartPosition[i] = NULL;
		buttonMapStartPosition[i] = nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
/** Initialize the Lan Game Options Menu */
//-------------------------------------------------------------------------------------------------
void LanGameOptionsMenuInit( WindowLayout *layout, void *userData )
{
	if (TheLobby->GetMyGame() && TheLobby->GetMyGame()->isGameInProgress())
	{
		// If we init while the game is in progress, we are really returning to the menu
		// after the game.  So, we pop the menu and go back to the lobby.  Whee!
		DEBUG_LOG(("Popping to lobby after a game!\n"));
		TheShell->popImmediate();
		return;
	}
	s_isIniting = TRUE;

	LANbuttonPushed = false;
	LANisShuttingDown = false;

	//initialize the gadgets
	EnableSlotListUpdates(FALSE);
	InitLanGameGadgets();
	EnableSlotListUpdates(TRUE);
	Int start = 0;

	// Make sure the text fields are clear
	GadgetListBoxReset( listboxChatWindowLanGame );
	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);	

	//The dialog needs to react differently depending on whether it's the host or not.
	TheMapCache->updateCache();
	if (TheLobby->AmIHost())
	{
		// read in some prefs
		LANGameInfo *game = TheLobby->GetMyGame();
		LANGameSlot *slot = game->getLANSlot(0);
		LobbySettings pref(LobbySettings::Kind::Lan);
		slot->setColor( pref.getPreferredColor() );
		slot->setPlayerTemplate( pref.getPreferredFaction() );
		slot->setNATBehavior(FirewallHelperClass::FIREWALL_TYPE_SIMPLE);
		game->setMap( pref.getPreferredMap() );
    game->setStartingCash( pref.getStartingCash() );
    game->setSuperweaponRestriction( pref.getSuperweaponRestricted() ? 1 : 0 );
    game->setGraceMinutes( pref.getGraceMinutes() );
    game->setMaxZoomPercent( pref.getMaxZoomPercent() );
		AsciiString lowerMap = pref.getPreferredMap();
		lowerMap.toLower();
		std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
		if (it != TheMapCache->end())
		{
			TheLobby->GetMyGame()->getSlot(0)->setMapAvailability(true);
			TheLobby->GetMyGame()->setMapCRC( it->second.m_CRC );
			TheLobby->GetMyGame()->setMapSize( it->second.m_filesize );

			TheLobby->GetMyGame()->adjustSlotsForMap(); // BGC- adjust the slots for the selected map.
		}

		//GadgetTextEntrySetText(comboBoxPlayer[0], TheLobby->GetMyName());
		lanUpdateSlotList();
		updateGameOptions();
		start = 1; // leave my combo boxes usable
	}
	else
	{

		//DEBUG_LOG(("LanGameOptionsMenuInit(): map is %s\n", TheLobby->GetMyGame()->getMap().str()));
		buttonStart->winSetText(TheGameText->fetch("GUI:Accept"));
		buttonSelectMap->winEnable( FALSE );
    if (checkboxLimitSuperweapons)	// missing in the Generals data
    checkboxLimitSuperweapons->winEnable( FALSE ); // Can look but only host can touch
    if (comboBoxStartingCash)
    comboBoxStartingCash->winEnable( FALSE );      // Ditto
    if (comboBoxGrace)
      comboBoxGrace->winEnable( FALSE );
    if (comboBoxMaxZoom)
      comboBoxMaxZoom->winEnable( FALSE );
		TheLobby->GetMyGame()->setMapCRC( TheLobby->GetMyGame()->getMapCRC() );		// force a recheck
		TheLobby->GetMyGame()->setMapSize( TheLobby->GetMyGame()->getMapSize() ); // of if we have the map
		TheLobby->RequestHasMap();
		lanUpdateSlotList();
		updateGameOptions();
	}
	for (Int i = start; i < MAX_SLOTS; ++i)
	{
		//I'm a client, disable the controls I can't touch.
		if (!TheLobby->AmIHost())
			comboBoxPlayer[i]->winEnable(FALSE);

		comboBoxColor[i]->winEnable(FALSE);
		comboBoxPlayerTemplate[i]->winEnable(FALSE);
		comboBoxTeam[i]->winEnable(FALSE);
//		buttonStartPosition[i]->winEnable(FALSE);
	}

//	for (i = 0; i < MAX_SLOTS; ++i)
//	{
//		if (buttonStartPosition[i])
//			buttonStartPosition[i]->winHide(TRUE); // not picking start spots this way any more
//	}
//
	// Show the Menu
	layout->hide( FALSE );
	
	// Set Keyboard to Main Parent
	TheWindowManager->winSetFocus( parentLanGameOptions );

	s_isIniting = FALSE;

	if (TheLobby->AmIHost())
	{
		TheLobby->RequestGameOptions(GenerateGameOptionsString(),true);
		TheLobby->RequestGameAnnounce();
	}
	lanUpdateSlotList();
	LanPositionStartSpots();
	TheTransitionHandler->setGroup("LanGameOptionsFade");

	// animate controls
	//TheShell->registerWithAnimateManager(buttonBack, WIN_ANIMATION_SLIDE_RIGHT, TRUE, 1);
	
}// void LanGameOptionsMenuInit( WindowLayout *layout, void *userData )

//-------------------------------------------------------------------------------------------------
/** Update options on screen */
//-------------------------------------------------------------------------------------------------
void updateGameOptions( void )
{
	LANGameInfo *theGame = TheLobby->GetMyGame();
	UnicodeString mapDisplayName;
	if (theGame && AreSlotListUpdatesEnabled())
	{
		const GameSlot *localSlot = theGame->getConstSlot(theGame->getLocalSlotNum());
		const MapMetaData *mapData = TheMapCache->findMap( TheLobby->GetMyGame()->getMap() );
		if (mapData && localSlot && localSlot->hasMap())
		{
			mapDisplayName.format(L"%ls", mapData->m_displayName.str());
		}
		else
		{
			AsciiString s = TheLobby->GetMyGame()->getMap();
			if (s.reverseFind('\\'))
			{
				s = s.reverseFind('\\') + 1;
			}
			mapDisplayName.format(L"%hs", s.str());
		}
		UnicodeString old = GadgetStaticTextGetText(textEntryMapDisplay);
		if(old.compare(mapDisplayName) != 0)
			LanPositionStartSpots();
		GadgetStaticTextSetText(textEntryMapDisplay, mapDisplayName);

    SelectGraceComboBox( comboBoxGrace, theGame->getGraceMinutes() );
    SelectMaxZoomComboBox( comboBoxMaxZoom, theGame->getMaxZoomPercent() );

    // Missing in the Generals data, see LanGameOptionsMenuInit
    if ( checkboxLimitSuperweapons )
    GadgetCheckBoxSetChecked( checkboxLimitSuperweapons, theGame->getSuperweaponRestriction() != 0 );
    if ( comboBoxStartingCash )
    {
		Int itemCount = GadgetComboBoxGetLength(comboBoxStartingCash);
    Int index;
    for ( index = 0; index < itemCount; index++ )
    {
      Int value  = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(comboBoxStartingCash, index)));
      if ( static_cast<UnsignedInt>(value) == theGame->getStartingCash().countMoney() )
      {
        GadgetComboBoxSetSelectedPos(comboBoxStartingCash, index, TRUE);
        break;
      }
    }

    DEBUG_ASSERTCRASH( index < itemCount, ("Could not find new starting cash amount %d in list", theGame->getStartingCash().countMoney() ) );
    }
	}
}



//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{
	DeinitLanGameGadgets();
	textEntryMapDisplay = nullptr;
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
/** Lan Game Options menu shutdown method */
//-------------------------------------------------------------------------------------------------
void LanGameOptionsMenuShutdown( WindowLayout *layout, void *userData )
{
	TheMouse->setCursor(Mouse::ARROW);
	TheMouse->setMouseText(UnicodeString::TheEmptyString,nullptr,nullptr);
	EnableSlotListUpdates(FALSE);
	LANisShuttingDown = true;

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *static_cast<Bool *>(userData);
	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}  //end if

	TheShell->reverseAnimatewindow();
	TheTransitionHandler->reverse("LanGameOptionsFade");
	if (TheLobby)
		TheLobby->ResetGameStartTimer();

	/*
	// hide menu
	layout->hide( TRUE );

	// Reset the LAN singleton
//	TheLobby->reset();

	// our shutdown is complete
	TheShell->shutdownComplete( layout );
	*/
}  // void LanGameOptionsMenuShutdown( WindowLayout *layout, void *userData )

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu update method */
//-------------------------------------------------------------------------------------------------
void LanGameOptionsMenuUpdate( WindowLayout * layout, void *userData)
{
	if(LANisShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
		shutdownComplete(layout);
	//TheLobby->update(); // this is handled in the lobby
}// void LanGameOptionsMenuUpdate( WindowLayout * layout, void *userData)

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanGameOptionsMenuInput( GameWindow *window, UnsignedInt msg,
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
}//WindowMsgHandledType LanGameOptionsMenuInput( GameWindow *window, UnsignedInt msg,


//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanGameOptionsMenuSystem( GameWindow *window, UnsignedInt msg, 
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;
	switch( msg )
	{
		//-------------------------------------------------------------------------------------------------	
		case GWM_CREATE:
			{
				break;
			} // case GWM_DESTROY:
		//-------------------------------------------------------------------------------------------------
		case GWM_DESTROY:
			{
				break;
			} // case GWM_DESTROY:
		//-------------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
			{	
				// if we're givin the opportunity to take the keyboard focus we must say we want it
				if( mData1 == TRUE )
					*reinterpret_cast<Bool *>(mData2) = TRUE;

				return WindowMsgHandledType::MSG_HANDLED;
			}//case GWM_INPUT_FOCUS:
		//-------------------------------------------------------------------------------------------------
		case GCM_SELECTED:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
				Int controlID = control->winGetWindowId();
				LANGameInfo *myGame = TheLobby->GetMyGame();

        if ( controlID == comboBoxStartingCashID )
        {
          handleStartingCashSelection();
        }
        else if ( comboBoxGrace && controlID == comboBoxGraceID )
        {
          handleGraceSelection();
        }
        else if ( comboBoxMaxZoom && controlID == comboBoxMaxZoomID )
        {
          handleMaxZoomSelection();
        }
        else
        {
				  for (Int i = 0; i < MAX_SLOTS; i++)
				  {
					  if (controlID == comboBoxColorID[i])
					  {
						  handleColorSelection(i);
					  }
					  else if (controlID == comboBoxPlayerTemplateID[i])
					  {
						  handlePlayerTemplateSelection(i);
					  }
					  else if (controlID == comboBoxTeamID[i])
					  {
						  handleTeamSelection(i);
					  }
					  else if( controlID == comboBoxPlayerID[i] && myGame->amIHost() )
					  {
						  // We don't have anything that'll happen if we click on ourselves
						  if(i == myGame->getLocalSlotNum())
						   break;
						  // Get
						  Int pos = -1;
						  GadgetComboBoxGetSelectedPos(comboBoxPlayer[i], &pos);
						  // Entries of the new AI carry slot state | AI kind << 8 | personality << 16 as item data;
						  // the others are the slot state itself.
						  Int aiKind = AI_KIND_CLASSIC;
						  Int personality = AI_PERSONALITY_STANDARD;
						  if (pos >= NEW_AI_COMBO_FIRST)
						  {
							  Int data = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(comboBoxPlayer[i], pos)));
							  aiKind = (data >> 8) & 0xFF;
							  personality = (data >> 16) & 0xFF;
							  pos = data & 0xFF;
						  }
						  if( pos != SLOT_PLAYER && pos >= 0)
						  {
							  if( myGame->getLANSlot(i)->getState() == SLOT_PLAYER )
							  {
								  UnicodeString name = myGame->getPlayerName(i);
								  myGame->getLANSlot(i)->setState(SlotState(pos));
								  myGame->getLANSlot(i)->setAIKind(aiKind);
								  myGame->getLANSlot(i)->setAIPersonality(personality);
								  myGame->resetAccepted();
								  TheLobby->OnPlayerLeave(name);
							  }
							  else if( myGame->getLANSlot(i)->getState() != pos || myGame->getLANSlot(i)->getAIKind() != aiKind
								  || myGame->getLANSlot(i)->getAIPersonality() != personality )
							  {
								  Bool wasAI = (myGame->getLANSlot(i)->isAI());
								  myGame->getLANSlot(i)->setState(SlotState(pos));
								  myGame->getLANSlot(i)->setAIKind(aiKind);
								  myGame->getLANSlot(i)->setAIPersonality(personality);
								  Bool isAI = (myGame->getLANSlot(i)->isAI());
								  if (wasAI || isAI)
									  myGame->resetAccepted();
								  if (wasAI ^ isAI)
									  PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, myGame, wasAI);
								  if (!s_isIniting)
								  {
									  TheLobby->RequestGameOptions(GenerateGameOptionsString(), true);
									  lanUpdateSlotList();
								  }
							  }
						  }
              break;
            }
          }
				}
        break;
			}// case GCM_SELECTED:
		//-------------------------------------------------------------------------------------------------
		case GBM_SELECTED:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
				Int controlID = control->winGetWindowId();

				if ( controlID == buttonBackID )
				{
					if( mapSelectLayout )
						{
							mapSelectLayout->destroyWindows();
							deleteInstanceOf(mapSelectLayout);
							mapSelectLayout = nullptr;
						}
					TheLobby->RequestGameLeave();
					//TheShell->pop();

				} //if ( controlID == buttonBack )				
				else if ( controlID == buttonEmoteID )
				{
					// read the user's input
					txtInput.set(GadgetTextEntryGetText( textEntryChat ));
					// Clear the text entry line
					GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
					// Clean up the text (remove leading/trailing chars, etc)
					txtInput.trim();
					// Echo the user's input to the chat window
					if (!txtInput.isEmpty())
						TheLobby->RequestChat(txtInput, GameLobbyInterface::LANCHAT_EMOTE);
				} //if ( controlID == buttonEmote )
				else if ( controlID == buttonSelectMapID )
				{
					//buttonBack->winEnable( false );
				
					mapSelectLayout = TheWindowManager->winCreateLayout( AsciiString( "Menus/LanMapSelectMenu.wnd" ) );
					mapSelectLayout->runInit();
					mapSelectLayout->hide( FALSE );
					mapSelectLayout->bringForward();

				}
				else if ( controlID == buttonStartID )
				{
					if (TheLobby->AmIHost())
					{
						StartPressed();
						//TheLobby->RequestGameStart();
					}
					else
					{
						//I'm the Client... send an accept message to the host.
						TheLobby->RequestAccept();

						// Disable the accept button
						EnableAcceptControls(TRUE, TheLobby->GetMyGame(), comboBoxPlayer, comboBoxColor, comboBoxPlayerTemplate,
							comboBoxTeam, buttonAccept, buttonStart, buttonMapStartPosition);
						
					}
				}
        else if ( controlID == checkboxLimitSuperweaponsID )
        {
          handleLimitSuperweaponsClick();
        }
				else if ( controlID == checkboxListPubliclyID && checkboxListPublicly )
				{
					TheLobby->setListedPublicly( GadgetCheckBoxIsChecked( checkboxListPublicly ) );
				}
				else
				{
					for (Int i = 0; i < MAX_SLOTS; i++)
					{
						if (controlID == buttonMapStartPositionID[i])
						{
							LANGameInfo *game = TheLobby->GetMyGame();
							Int playerIdxInPos = -1;
							for (Int j=0; j<MAX_SLOTS; ++j)
							{
								LANGameSlot *slot = game->getLANSlot(j);
								if (slot && slot->getStartPos() == i)
								{
									playerIdxInPos = j;
									break;
								}
							}
							if (playerIdxInPos >= 0)
							{
								LANGameSlot *slot = game->getLANSlot(playerIdxInPos);
								if (playerIdxInPos == game->getLocalSlotNum() || (game->amIHost() && slot && slot->isAI()))
								{
									// it's one of my type.  Try to change it.
									Int nextPlayer = getNextSelectablePlayer(playerIdxInPos+1);
									handleStartPositionSelection(playerIdxInPos, -1);
									if (nextPlayer >= 0)
									{
										handleStartPositionSelection(nextPlayer, i);
									}
								}
							}
							else
							{
								// nobody in the slot - put us in
								Int nextPlayer = getNextSelectablePlayer(0);
								if (nextPlayer < 0)
									nextPlayer = getFirstSelectablePlayer(game);
								handleStartPositionSelection(nextPlayer, i);
							}
						}
					}
				}

				break;
			}// case GBM_SELECTED:
		//-------------------------------------------------------------------------------------------------
		case GBM_SELECTED_RIGHT:
		{
			if (LANbuttonPushed)
				break;

			GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
			Int controlID = control->winGetWindowId();
			for (Int i = 0; i < MAX_SLOTS; i++)
			{
				if (controlID == buttonMapStartPositionID[i])
				{
					LANGameInfo *game = TheLobby->GetMyGame();
					Int playerIdxInPos = -1;
					for (Int j=0; j<MAX_SLOTS; ++j)
					{
						LANGameSlot *slot = game->getLANSlot(j);
						if (slot && slot->getStartPos() == i)
						{
							playerIdxInPos = j;
							break;
						}
					}
					if (playerIdxInPos >= 0)
					{
						LANGameSlot *slot = game->getLANSlot(playerIdxInPos);
						if (playerIdxInPos == game->getLocalSlotNum() || (game->amIHost() && slot && slot->isAI()))
						{
							// it's one of my type.  Remove it.
							handleStartPositionSelection(playerIdxInPos, -1);
						}
					}
				}
			}					
			break;
		}
		//-------------------------------------------------------------------------------------------------
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
					txtInput.trim();
					// Echo the user's input to the chat window
					if (!txtInput.isEmpty())
						TheLobby->RequestChat(txtInput, GameLobbyInterface::LANCHAT_NORMAL);

				}// if ( controlID == textEntryChatID )
				break;
			}
		//-------------------------------------------------------------------------------------------------
		default:
			return WindowMsgHandledType::MSG_IGNORED;
	}//Switch
	return WindowMsgHandledType::MSG_HANDLED;
}//WindowMsgHandledType LanGameOptionsMenuSystem( GameWindow *window, UnsignedInt msg, 

//-------------------------------------------------------------------------------------------------
/** Utility FUnction used as a bridge from other windows to this one */
//-------------------------------------------------------------------------------------------------
void PostToLanGameOptions( PostToLanGameType post )
{
	if (post >= PostToLanGameType::POST_TO_LAN_GAME_TYPE_COUNT)
		return;
	LanPositionStartSpots();
	switch (post)
	{
			//-------------------------------------------------------------------------------------------------
		case PostToLanGameType::SEND_GAME_OPTS:
		{
			LANGameInfo *game = TheLobby->GetMyGame();
			game->resetAccepted();			
			updateGameOptions();		
			lanUpdateSlotList();
			
			//buttonBack->winEnable( true );
			
			for(Int i = 0; i < MAX_SLOTS; ++i)
			{
				game->getSlot(i)->setStartPos(-1);
			}

			TheLobby->RequestGameOptions(GenerateGameOptionsString(), true);
			break;
		}		//-------------------------------------------------------------------------------------------------
		case PostToLanGameType::MAP_BACK:
		{
				//buttonBack->winEnable( true );
		}
			return;
		default:
			return;
	}
}
