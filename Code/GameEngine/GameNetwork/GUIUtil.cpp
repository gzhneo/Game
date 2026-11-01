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

// FILE: GUIUtil.cpp //////////////////////////////////////////////////////
// Author: Matthew D. Campbell, Sept 2002

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/GUIUtil.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/MapUtil.h"
#include "Common/NameKeyGenerator.h"

#include "Common/MultiplayerSettings.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameText.h"
#include "GameLogic/GameLogic.h" // SUPERWEAPON_RESTRICT_COUNT
#include "GameNetwork/GameInfo.h"
#include "Common/PlayerTemplate.h"
#include "GameNetwork/GameLobbyCallbacks.h" // for acceptTrueColor, etc
#include "GameClient/ChallengeGenerals.h"

// -----------------------------------------------------------------------------

static Bool winInitialized = FALSE;

void EnableSlotListUpdates( Bool val )
{
	winInitialized = val;
}

Bool AreSlotListUpdatesEnabled( void )
{
	return winInitialized;
}

// -----------------------------------------------------------------------------

void EnableAcceptControls(Bool Enabled, GameInfo *myGame, GameWindow *comboPlayer[],
										GameWindow *comboColor[], GameWindow *comboPlayerTemplate[],
										GameWindow *comboTeam[], GameWindow *buttonAccept[], GameWindow *buttonStart,
										GameWindow *buttonMapStartPosition[], Int slotNum)
{
	if(slotNum == -1 || slotNum >= MAX_SLOTS )
		slotNum = myGame->getLocalSlotNum();

	Bool isObserver = myGame->getConstSlot(slotNum)->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER;

	if( !myGame->amIHost() && (buttonStart != nullptr) )
		buttonStart->winEnable(Enabled);
	if(comboColor[slotNum])
	{
		if (isObserver)
		{
			GadgetComboBoxHideList(comboColor[slotNum]);
		}
		comboColor[slotNum]->winEnable(Enabled && !isObserver);
	}
	if(comboPlayerTemplate[slotNum])
		comboPlayerTemplate[slotNum]->winEnable(Enabled);
	if(comboTeam[slotNum])
	{
		if (isObserver)
		{
			GadgetComboBoxHideList(comboTeam[slotNum]);
		}
		comboTeam[slotNum]->winEnable(Enabled && !isObserver);
	}

	Bool canChooseStartSpot = FALSE;
	if (!isObserver)
		canChooseStartSpot = TRUE;
	for (Int i=0; i<MAX_SLOTS && !canChooseStartSpot && myGame->amIHost(); ++i)
	{
		if (myGame->getConstSlot(i) && myGame->getConstSlot(i)->isAI())
			canChooseStartSpot = TRUE;
	}

	if (slotNum == myGame->getLocalSlotNum())
	{
		if (myGame->getConstSlot(myGame->getLocalSlotNum())->hasMap())
		{
			for (Int i=0; i<MAX_SLOTS; ++i)
			{
				if (buttonMapStartPosition[i])
				{
					buttonMapStartPosition[i]->winEnable(Enabled && canChooseStartSpot);
				}
			}
		}
		else
		{
			for (Int i=0; i<MAX_SLOTS; ++i)
			{
				if (buttonMapStartPosition[i])
					buttonMapStartPosition[i]->winEnable(FALSE);
			}
		}
	}
}

// -----------------------------------------------------------------------------

void ShowUnderlyingGUIElements( Bool show, const char *layoutFilename, const char *parentName,
															 const char **gadgetsToHide, const char **perPlayerGadgetsToHide )
{
	AsciiString parentNameStr;
	parentNameStr.format("%s:%s", layoutFilename, parentName);
	NameKeyType parentID = NAMEKEY(parentNameStr);
	GameWindow *parent = TheWindowManager->winGetWindowFromId( nullptr, parentID );
	if (!parent)
	{
		DEBUG_CRASH(("Window %s not found\n", parentNameStr.str()));
		return;
	}

	// hide some GUI elements of the screen underneath
	GameWindow *win;

	Int player;
	const char **text;

	text = gadgetsToHide;
	while (*text)
	{
		AsciiString gadgetName;
		gadgetName.format("%s:%s", layoutFilename, *text);
		win	= TheWindowManager->winGetWindowFromId( parent, NAMEKEY(gadgetName) );
		//DEBUG_ASSERTCRASH(win, ("Cannot find %s to show/hide it", gadgetName.str()));
		if (win)
		{
			win->winHide( !show );
		}
		++text;
	}

	text = perPlayerGadgetsToHide;
	while (*text)
	{
		for (player = 0; player < MAX_SLOTS; ++player)
		{
			AsciiString gadgetName;
			gadgetName.format("%s:%s%d", layoutFilename, *text, player);
			win	= TheWindowManager->winGetWindowFromId( parent, NAMEKEY(gadgetName) );
			//DEBUG_ASSERTCRASH(win, ("Cannot find %s to show/hide it", gadgetName.str()));
			if (win)
			{
				win->winHide( !show );
			}
		}
		++text;
	}
}

// -----------------------------------------------------------------------------

void PopulateColorComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool isObserver)
{
	Int numColors = TheMultiplayerSettings->getNumColors();
	UnicodeString colorName;
	std::vector<bool> availableColors;

	Int i;
	for (i = 0; i < numColors; i++)
		availableColors.push_back(true);

	for (i = 0; i < MAX_SLOTS; i++)
	{
		GameSlot *slot = myGame->getSlot(i);	
		if( slot && (i != comboBox) && (slot->getColor() >= 0 )&& (slot->getColor() < numColors))
		{
			DEBUG_ASSERTCRASH(slot->getColor() >= 0,("We've tried to access array %d and that ain't good",slot->getColor()));
			availableColors[slot->getColor()] = false;
		}
	}

	Bool wasObserver = (GadgetComboBoxGetLength(comboArray[comboBox]) == 1);
	GadgetComboBoxReset(comboArray[comboBox]);

	MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_RANDOM);
	Int newIndex = GadgetComboBoxAddEntry(comboArray[comboBox],
		(isObserver)?TheGameText->fetch("GUI:None"):TheGameText->fetch("GUI:???"), def->getColor());
	GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, reinterpret_cast<void *>(-1));

	if (isObserver)
	{
		GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);
		return;
	}

	for (Int c=0; c<numColors; ++c)
	{
		def = TheMultiplayerSettings->getColor(c);
		if (!def || availableColors[c] == false)
			continue;

		colorName = TheGameText->fetch(def->getTooltipName().str());
		newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], colorName, def->getColor());
		GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, reinterpret_cast<void *>(static_cast<intptr_t>(c)));
	}
	if (wasObserver)
		GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);
}

// -----------------------------------------------------------------------------

void PopulatePlayerTemplateComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool allowObservers)
{
	Int numPlayerTemplates = ThePlayerTemplateStore->getPlayerTemplateCount();
	UnicodeString playerTemplateName;

	GadgetComboBoxReset(comboArray[comboBox]);

	MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_RANDOM);
	Int newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], TheGameText->fetch("GUI:Random"), def->getColor());
	GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, reinterpret_cast<void *>(PLAYERTEMPLATE_RANDOM));

	std::set<AsciiString> seenSides;

	for (Int c=0; c<numPlayerTemplates; ++c)
	{
		const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(c);
		if (!fac)
			continue;

		if (fac->getStartingBuilding().isEmpty())
			continue;

		if ( myGame->oldFactionsOnly() && !fac->isOldFaction() )
		  continue;

		// Prevent players from selecting the disabled Generals for use.
		// This is also enforced at game loading (GameLogic.cpp and UserSettings.cpp).
		// @todo: unlock these when something rad happens
		Bool disallowLockedGenerals = TRUE;
		const GeneralPersona *general = TheChallengeGenerals->getGeneralByTemplateName(fac->getName());
		Bool startsLocked = general ? !general->isStartingEnabled() : FALSE;
		if (disallowLockedGenerals && startsLocked)
			continue;


		AsciiString side;
		side.format("SIDE:%s", fac->getSide().str());
		if (seenSides.find(side) != seenSides.end())
			continue;

		seenSides.insert(side);

		newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], TheGameText->fetch(side), def->getColor());
		GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, reinterpret_cast<void *>(static_cast<intptr_t>(c)));
	}
	seenSides.clear();

	// disabling observers for Multiplayer test
	if (allowObservers)
	{
		def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_OBSERVER);
		newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], TheGameText->fetch("GUI:Observer"), def->getColor());
		GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, reinterpret_cast<void *>(PLAYERTEMPLATE_OBSERVER));
	}
	GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);

}

// -----------------------------------------------------------------------------

void PopulateTeamComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool isObserver)
{
	Int numTeams = MAX_SLOTS/2;
	UnicodeString teamName;

	GadgetComboBoxReset(comboArray[comboBox]);

	MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_RANDOM);
	Int newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], TheGameText->fetch("Team:0"), def->getColor());
	GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, reinterpret_cast<void *>(-1));

	if (isObserver)
	{
		GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);
		return;
	}

	for (Int c=0; c<numTeams; ++c)
	{
		AsciiString teamStr;
		teamStr.format("Team:%d", c + 1);
		teamName = TheGameText->fetch(teamStr.str());
		newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], teamName, def->getColor());
		GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, reinterpret_cast<void *>(static_cast<intptr_t>(c)));
	}
	GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);
}

// -----------------------------------------------------------------------------
static UnicodeString formatMoneyForStartingCashComboBox( const Money & moneyAmount )
{
  UnicodeString rtn;
  rtn.format( TheGameText->fetch( "GUI:StartingMoneyFormat" ), moneyAmount.countMoney() );
  return rtn;
}

void PopulateStartingCashComboBox(GameWindow *comboBox, GameInfo *myGame)
{
  // The Generals menus (LanGameOptionsMenu.wnd, SkirmishGameOptionsMenu.wnd) have no starting cash
  // selection.
  if( comboBox == nullptr )
    return;

  GadgetComboBoxReset(comboBox);

  const MultiplayerStartingMoneyList & startingCashMap = TheMultiplayerSettings->getStartingMoneyList(); 
  Int currentSelectionIndex = -1;

  MultiplayerStartingMoneyList::const_iterator it;
  for ( it = startingCashMap.begin(); it != startingCashMap.end(); it++ )
  {
    Int newIndex = GadgetComboBoxAddEntry(comboBox, formatMoneyForStartingCashComboBox( *it ), 
                                          comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor());
    GadgetComboBoxSetItemData(comboBox, newIndex, reinterpret_cast<void *>(static_cast<uintptr_t>(it->countMoney())));

    if ( myGame->getStartingCash().amountEqual( *it ) )
    {
      currentSelectionIndex = newIndex;
    }
  }

  if ( currentSelectionIndex == -1 )
  {
    DEBUG_CRASH( ("Current selection for starting cash not found in list") );
    currentSelectionIndex = GadgetComboBoxAddEntry(comboBox, formatMoneyForStartingCashComboBox( myGame->getStartingCash() ), 
                                          comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor());
    GadgetComboBoxSetItemData(comboBox, currentSelectionIndex, reinterpret_cast<void *>(static_cast<uintptr_t>(it->countMoney())) );
  }

  GadgetComboBoxSetSelectedPos(comboBox, currentSelectionIndex);
}

// -----------------------------------------------------------------------------
static UnicodeString formatGraceEntry( UnsignedShort minutes )
{
  UnicodeString text;
  if ( minutes == 0 )
    text = TheGameText->fetch( "GZH:GraceNone" );
  else
    text.format( TheGameText->fetch( "GZH:GraceMinutes" ), static_cast<Int>(minutes) );
  return text;
}

GameWindow *CreateGraceComboBox(GameWindow *model, const char *decoratedName)
{
  GameWindow *box = GadgetComboBoxCreateNear( model, decoratedName );
  DEBUG_ASSERTCRASH( box || model == nullptr, ("No room for the start protection choice next to %s", decoratedName) );
  return box;
}

void PopulateGraceComboBox(GameWindow *comboBox, UnsignedShort minutes)
{
  if ( comboBox == nullptr )
    return;
  GadgetComboBoxReset( comboBox );
  static const UnsignedShort choices[] = { 0, 5, 10 };
  const Color color = comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor();
  for ( size_t i = 0; i < sizeof( choices ) / sizeof( choices[0] ); ++i )
  {
    const Int index = GadgetComboBoxAddEntry( comboBox, formatGraceEntry( choices[i] ), color );
    GadgetComboBoxSetItemData( comboBox, index, reinterpret_cast<void *>(static_cast<uintptr_t>(choices[i])) );
  }
  SelectGraceComboBox( comboBox, minutes );
  GadgetComboBoxWidenToFit( comboBox );
  GadgetComboBoxFitText( comboBox );
}

void SelectGraceComboBox(GameWindow *comboBox, UnsignedShort minutes)
{
  if ( comboBox == nullptr )
    return;
  const Int count = GadgetComboBoxGetLength( comboBox );
  for ( Int index = 0; index < count; ++index )
  {
    if ( reinterpret_cast<uintptr_t>(GadgetComboBoxGetItemData( comboBox, index )) == minutes )
    {
      GadgetComboBoxSetSelectedPos( comboBox, index, TRUE );
      return;
    }
  }
  // a value from the command line or another version: show it as an extra entry
  const Color color = comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor();
  const Int index = GadgetComboBoxAddEntry( comboBox, formatGraceEntry( minutes ), color );
  GadgetComboBoxSetItemData( comboBox, index, reinterpret_cast<void *>(static_cast<uintptr_t>(minutes)) );
  GadgetComboBoxSetSelectedPos( comboBox, index, TRUE );
}

UnsignedShort GetGraceComboBoxMinutes(GameWindow *comboBox)
{
  if ( comboBox == nullptr )
    return 0;
  Int selIndex = -1;
  GadgetComboBoxGetSelectedPos( comboBox, &selIndex );
  if ( selIndex < 0 )
    return 0;
  return static_cast<UnsignedShort>( reinterpret_cast<uintptr_t>(GadgetComboBoxGetItemData( comboBox, selIndex )) );
}

// -----------------------------------------------------------------------------
static UnicodeString formatMaxZoomEntry( UnsignedShort percent )
{
  UnicodeString text;
  if ( percent <= 100 )
    text = TheGameText->fetch( "GZH:MaxZoomNormal" );
  else
    text.format( TheGameText->fetch( "GZH:MaxZoomPercent" ), static_cast<Int>(percent) );
  return text;
}

GameWindow *CreateMaxZoomComboBox(GameWindow *model, const char *decoratedName)
{
  GameWindow *box = GadgetComboBoxCreateNear( model, decoratedName );
  DEBUG_ASSERTCRASH( box || model == nullptr, ("No room for the zoom-out choice next to %s", decoratedName) );
  return box;
}

void PopulateMaxZoomComboBox(GameWindow *comboBox, UnsignedShort percent)
{
  if ( comboBox == nullptr )
    return;
  GadgetComboBoxReset( comboBox );
  static const UnsignedShort choices[] = { 100, 105, 110, 115, 120, 125 };
  const Color color = comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor();
  for ( size_t i = 0; i < sizeof( choices ) / sizeof( choices[0] ); ++i )
  {
    const Int index = GadgetComboBoxAddEntry( comboBox, formatMaxZoomEntry( choices[i] ), color );
    GadgetComboBoxSetItemData( comboBox, index, reinterpret_cast<void *>(static_cast<uintptr_t>(choices[i])) );
  }
  SelectMaxZoomComboBox( comboBox, percent );
  GadgetComboBoxWidenToFit( comboBox );
  GadgetComboBoxFitText( comboBox );
}

void SelectMaxZoomComboBox(GameWindow *comboBox, UnsignedShort percent)
{
  if ( comboBox == nullptr )
    return;
  const Int count = GadgetComboBoxGetLength( comboBox );
  for ( Int index = 0; index < count; ++index )
  {
    if ( reinterpret_cast<uintptr_t>(GadgetComboBoxGetItemData( comboBox, index )) == percent )
    {
      GadgetComboBoxSetSelectedPos( comboBox, index, TRUE );
      return;
    }
  }
  // a value between the steps (State.ini, another version): show it as an extra entry
  const Color color = comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor();
  const Int index = GadgetComboBoxAddEntry( comboBox, formatMaxZoomEntry( percent ), color );
  GadgetComboBoxSetItemData( comboBox, index, reinterpret_cast<void *>(static_cast<uintptr_t>(percent)) );
  GadgetComboBoxSetSelectedPos( comboBox, index, TRUE );
}

UnsignedShort GetMaxZoomComboBoxPercent(GameWindow *comboBox)
{
  if ( comboBox == nullptr )
    return 100;
  Int selIndex = -1;
  GadgetComboBoxGetSelectedPos( comboBox, &selIndex );
  if ( selIndex < 0 )
    return 100;
  return static_cast<UnsignedShort>( reinterpret_cast<uintptr_t>(GadgetComboBoxGetItemData( comboBox, selIndex )) );
}

// -----------------------------------------------------------------------------

//  -----------------------------------------------------------------------------------------
// The slot list displaying function
//-------------------------------------------------------------------------------------------------
void AddNewAIComboEntries( GameWindow *comboBox, Color color )
{
	static const struct { SlotState state; const char *key; } entries[] =
	{
		{ SLOT_EASY_AI, "GUI:EasyAI" },
		{ SLOT_MED_AI, "GUI:MediumAI" },
		{ SLOT_BRUTAL_AI, "GUI:HardAI" }
	};
	// one block of the three difficulties per personality, standard first
	for (Int personality = AI_PERSONALITY_STANDARD; personality < AI_PERSONALITY_COUNT; ++personality)
	{
		for (size_t i = 0; i < sizeof( entries ) / sizeof( entries[0] ); ++i)
		{
			// personalities exist only for the new AI, so their entries name the personality instead of the
			// suffix (the player column is narrow)
			UnicodeString text = TheGameText->fetch( entries[i].key );
			if (personality == AI_PERSONALITY_STANDARD)
			{
				text.concat( L" " );
				text.concat( TheGameText->fetch( "GZH:NewAISuffix" ) );
			}
			else
			{
				text.concat( L" (" );
				text.concat( TheGameText->fetch( AIPersonalityTextKey( personality ) ) );
				text.concat( L")" );
			}
			Int index = GadgetComboBoxGetLength( comboBox );
			GadgetComboBoxAddEntry( comboBox, text, color );
			GadgetComboBoxSetItemData( comboBox, index,
				reinterpret_cast<void *>(static_cast<intptr_t>(entries[i].state | (AI_KIND_COMMANDER << 8) | (personality << 16))) );
		}
	}
}

void UpdateSlotList( GameInfo *myGame, GameWindow *comboPlayer[],
										GameWindow *comboColor[], GameWindow *comboPlayerTemplate[],
										GameWindow *comboTeam[], GameWindow *buttonAccept[], 
										GameWindow *buttonStart, GameWindow *buttonMapStartPosition[] )
{
	if(!AreSlotListUpdatesEnabled())
		return;
	//LANGameInfo *myGame = TheLobby->GetMyGame();

	const MapMetaData *mapData = TheMapCache->findMap( myGame->getMap() );
	Bool willTransfer = TRUE;
	if (mapData)
	{
		willTransfer = !mapData->m_isOfficial;
	}
	else
	{
		willTransfer = WouldMapTransfer(myGame->getMap());
	}

	if (myGame)
	{
		for( int i =0; i < MAX_SLOTS; i++ )
		{
			GameSlot * slot = myGame->getSlot(i);
			// if i'm host, enable the controls for AI
			if(myGame->amIHost() && slot && slot->isAI())
			{
				EnableAcceptControls(TRUE, myGame, comboPlayer, comboColor, comboPlayerTemplate,
					comboTeam, buttonAccept, buttonStart, buttonMapStartPosition, i);
			}
			else if (slot && myGame->getLocalSlotNum() == i)
			{
				if(slot->isAccepted() && !myGame->amIHost())
				{
					EnableAcceptControls(FALSE, myGame, comboPlayer, comboColor, comboPlayerTemplate,
						comboTeam, buttonAccept, buttonStart, buttonMapStartPosition);
				}
				else
				{
					if (slot->hasMap()) {
						EnableAcceptControls(TRUE, myGame, comboPlayer, comboColor, comboPlayerTemplate,
							comboTeam, buttonAccept, buttonStart, buttonMapStartPosition);
					}
					else
					{
						EnableAcceptControls(willTransfer, myGame, comboPlayer, comboColor, comboPlayerTemplate,
							comboTeam, buttonAccept, buttonStart, buttonMapStartPosition);
					}
				}
				
			}
			else if(myGame->amIHost())
			{
				EnableAcceptControls(FALSE, myGame, comboPlayer, comboColor, comboPlayerTemplate,
					comboTeam, buttonAccept, buttonStart, buttonMapStartPosition, i);
			}
			if(slot && slot->isHuman())
			{
				UnicodeString newName = slot->getName();
				UnicodeString oldName = GadgetComboBoxGetText(comboPlayer[i]);
				if (comboPlayer[i] && newName.compare(oldName))
				{
					GadgetComboBoxSetText(comboPlayer[i], newName);
				}
				if(i!= 0 && buttonAccept && buttonAccept[i])
				{
					buttonAccept[i]->winHide(FALSE);
				//Color In the little accepted boxes
					if(slot->isAccepted())
					{
						if(BitTest(buttonAccept[i]->winGetStatus(), WIN_STATUS_IMAGE	))
							buttonAccept[i]->winEnable(TRUE);
						else
							GadgetButtonSetEnabledColor(buttonAccept[i], acceptTrueColor );
					}
					else
					{
						if(BitTest(buttonAccept[i]->winGetStatus(), WIN_STATUS_IMAGE	))
							buttonAccept[i]->winEnable(FALSE);
						else
							GadgetButtonSetEnabledColor(buttonAccept[i], acceptFalseColor );
					}
				}
			}
			else
			{
				// in the else branch of "slot && slot->isHuman()" slot can be NULL
				if (slot)
				{
					// The new AI has its own entries after "Brutal" (AddNewAIComboEntries), one per difficulty;
					// menus without them show the classic entry.
					Int pos = slot->getState();
					if (slot->isAI() && slot->getAIKind() == AI_KIND_COMMANDER)
					{
						Int newPos = NEW_AI_COMBO_FIRST + slot->getAIPersonality() * NEW_AI_COMBO_DIFFICULTIES + (slot->getState() - SLOT_EASY_AI);
						if (GadgetComboBoxGetLength(comboPlayer[i]) > newPos)
							pos = newPos;
					}
					GadgetComboBoxSetSelectedPos(comboPlayer[i], pos, TRUE);
				}
        if( buttonAccept &&  buttonAccept[i] )
				  buttonAccept[i]->winHide(TRUE);
			}
/*
			if (myGame->getLocalSlotNum() == i && i!=0)
			{
				if (comboPlayer[i])
					comboPlayer[i]->winEnable( TRUE );
			}
			else*/ if (!myGame->amIHost())
			{
				if (comboPlayer[i])
					comboPlayer[i]->winEnable( FALSE );
			}
			//if( i == myGame->getLocalSlotNum())
      if((comboColor[i] != nullptr) && BitTest(comboColor[i]->winGetStatus(), WIN_STATUS_ENABLED))
				PopulateColorComboBox(i, comboColor, myGame, myGame->getConstSlot(i)->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER);
			Int max, idx;
			if (comboColor[i] != nullptr) {
				max = GadgetComboBoxGetLength(comboColor[i]);
				for (idx=0; idx<max; ++idx)
				{
					Int color = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(comboColor[i], idx)));
					if (color == slot->getColor())
					{
						GadgetComboBoxSetSelectedPos(comboColor[i], idx, TRUE);
						break;
					}
				}
			}

			if (comboTeam[i] != nullptr) {
				max = GadgetComboBoxGetLength(comboTeam[i]);
				for (idx=0; idx<max; ++idx)
				{
					Int team = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(comboTeam[i], idx)));
					if (team == slot->getTeamNumber())
					{
						GadgetComboBoxSetSelectedPos(comboTeam[i], idx, TRUE);
						break;
					}
				}
			}

			if (comboPlayerTemplate[i] != nullptr) {
				max = GadgetComboBoxGetLength(comboPlayerTemplate[i]);
				for (idx=0; idx<max; ++idx)
				{
					Int playerTemplate = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(comboPlayerTemplate[i], idx)));
					if (playerTemplate == slot->getPlayerTemplate())
					{
						GadgetComboBoxSetSelectedPos(comboPlayerTemplate[i], idx, TRUE);
						break;
					}
				}
			}
		}
	}
}

// -----------------------------------------------------------------------------
