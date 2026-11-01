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

// InGameUI.cpp ///////////////////////////////////////////////////////////////////////////////////
// Implementation of in-game user interface singleton inteface
// Author: Michael S. Booth, March 2001
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Engine/ThreadRole.h"
#include "Common/Xfer.h"

#define DEFINE_SHADOW_NAMES

#include "Common/ActionManager.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/TestAids.h"
#include "Common/GameType.h"
#include "Common/MessageStream.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Radar.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/BuildAssistant.h"
#include "Common/Recorder.h"
#include "Common/BuildAssistant.h"
#include "Common/SpecialPower.h"

#include "GameClient/Anim2D.h"
#include "GameClient/ControlBar.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Diplomacy.h"
#include "GameClient/Eva.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameWindowGlobal.h"
#include "GameClient/GameWindowID.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/InGameUI.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/Mouse.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/View.h"
#include "GameClient/TerrainVisual.h"	
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/SelectionXlat.h"
#include "GameClient/Shadow.h"
#include "GameClient/GlobalLanguage.h"

#include "GameLogic/AIGuard.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Object.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "GameLogic/Module/SupplyWarehouseDockUpdate.h"
#include "GameLogic/Module/MobMemberSlavedUpdate.h"//ML



// ------------------------------------------------------------------------------------------------
static const Real placementOpacity = 0.45f;
static const RGBColor illegalBuildColor = { 1.0, 0.0, 0.0 };

//-------------------------------------------------------------------------------------------------
/// The InGameUI singleton instance.

GameWindow *m_replayWindow = nullptr;

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void showReplayControls( void )
{
	if (m_replayWindow)
	{
		Bool show = TheGameLogic->isInReplayGame();
		m_replayWindow->winHide(!show);
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void hideReplayControls( void )
{
	if (m_replayWindow)
	{
		m_replayWindow->winHide(TRUE);
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void toggleReplayControls( void )
{
	if (m_replayWindow)
	{
		Bool show = TheGameLogic->isInReplayGame() && m_replayWindow->winIsHidden();
		m_replayWindow->winHide(!show);
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
SuperweaponInfo::SuperweaponInfo(
	ObjectID id,
	UnsignedInt timestamp,
	Bool hiddenByScript,
	Bool hiddenByScience,
	Bool ready,
  Bool evaReadyPlayed,
	const AsciiString& superweaponNormalFont, 
	Int superweaponNormalPointSize, 
	Bool superweaponNormalBold,
	Color c, 
	const SpecialPowerTemplate* spt
) :
	m_id(id),
	m_timestamp(timestamp),
	m_hiddenByScript(hiddenByScript),
	m_hiddenByScience(hiddenByScience),
	m_ready(ready),
  m_evaReadyPlayed( evaReadyPlayed ),
	m_forceUpdateText(false),
	m_nameDisplayString(nullptr),
	m_timeDisplayString(nullptr),
	m_color(c),
	m_powerTemplate(spt)
{
	m_nameDisplayString = TheDisplayStringManager->newDisplayString();
	m_nameDisplayString->reset();
	m_nameDisplayString->setText( UnicodeString::TheEmptyString );

	m_timeDisplayString = TheDisplayStringManager->newDisplayString();
	m_timeDisplayString->reset();
	m_timeDisplayString->setText( UnicodeString::TheEmptyString );

	setFont( superweaponNormalFont, superweaponNormalPointSize, superweaponNormalBold );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
SuperweaponInfo::~SuperweaponInfo()
{
	if (m_nameDisplayString)
		TheDisplayStringManager->freeDisplayString( m_nameDisplayString );
	m_nameDisplayString = nullptr;

	if (m_timeDisplayString)
		TheDisplayStringManager->freeDisplayString( m_timeDisplayString );
	m_timeDisplayString = nullptr;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void SuperweaponInfo::setFont(const AsciiString& superweaponNormalFont, Int superweaponNormalPointSize, Bool superweaponNormalBold)
{
	m_nameDisplayString->setFont( TheFontLibrary->getFont( superweaponNormalFont, 
		TheGlobalLanguageData->adjustFontSize(superweaponNormalPointSize), superweaponNormalBold ) );
	m_timeDisplayString->setFont( TheFontLibrary->getFont( superweaponNormalFont, 
		TheGlobalLanguageData->adjustFontSize(superweaponNormalPointSize), superweaponNormalBold ) );
}

// ------------------------------------------------------------------------------------------------
void SuperweaponInfo::setText(const UnicodeString& name, const UnicodeString& time)
{
	m_nameDisplayString->setText(name);
	m_timeDisplayString->setText(time);
}

// ------------------------------------------------------------------------------------------------
void SuperweaponInfo::drawName(Int x, Int y, Color color, Color dropColor)
{
	if (color == 0)
		color = m_color;
 	m_nameDisplayString->draw(x - m_nameDisplayString->getWidth(), y, color, dropColor);
}

// ------------------------------------------------------------------------------------------------
void SuperweaponInfo::drawTime(Int x, Int y, Color color, Color dropColor)
{
	if (color == 0)
		color = m_color;
 	m_timeDisplayString->draw(x, y, color, dropColor);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Real SuperweaponInfo::getHeight() const
{
	return static_cast<Real>(m_nameDisplayString->getFont()->height);
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void InGameUI::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version 
	* 2: Save NamedTimers, but not specifically their Info structs.  We'll recreate them.
  * 3: Added m_evaReadyPlayed boolean to transfer
*/
// ------------------------------------------------------------------------------------------------
void InGameUI::xfer( Xfer *xfer )
{
	// version
	const XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	if( version >= 2 )
	{
		// Saving the named timer infos and their friends so we get script timers back after we load
		xfer->xferInt(&m_namedTimerLastFlashFrame);
		xfer->xferBool(&m_namedTimerUsedFlashColor);
		xfer->xferBool(&m_showNamedTimers);

		// For the timers themselves, all I need to save is the things that are used in the call to addNamedTimer.
		// It is okay to do this, because SuperweaponInfos pushes things on to a map; addNamedTimer is just a more
		// organized way to push things on the namedTimer Map.
		// addNamedTimer needs (const AsciiString& timerName, const UnicodeString& text, Bool isCountdown)
		if (xfer->getXferMode() == XFER_SAVE)
		{
			Int timerCount = static_cast<Int>(m_namedTimers.size());
			xfer->xferInt( &timerCount );
			for( NamedTimerMapIt timerIter = m_namedTimers.begin(); timerIter != m_namedTimers.end(); ++timerIter )
			{
				xfer->xferAsciiString( &(timerIter->second->m_timerName) );
				xfer->xferUnicodeString( &(timerIter->second->timerText) );
				xfer->xferBool( &(timerIter->second->isCountdown) );
			}
		}
		else // iz a Load
		{
			Int timerCount;
			xfer->xferInt( &timerCount );
			for( Int timerIndex = 0; timerIndex < timerCount; ++timerIndex )
			{
				AsciiString timerName;
				UnicodeString timerText;
				Bool isCountdown;
				xfer->xferAsciiString( &timerName );
				xfer->xferUnicodeString( &timerText );
				xfer->xferBool( &isCountdown );

				addNamedTimer( timerName, timerText, isCountdown );
			}
		}
	}

	xfer->xferBool(&m_superweaponHiddenByScript);
	//xfer->xferBool(&m_inputEnabled);	// no, don't save this yet. somewhat problematic.

	if (xfer->getXferMode() == XFER_SAVE)
	{
		for (Int playerIndex = 0; playerIndex < MAX_PLAYER_COUNT; ++playerIndex)
		{
			for (SuperweaponMap::iterator mapIt = m_superweapons[playerIndex].begin(); mapIt != m_superweapons[playerIndex].end(); ++mapIt)
			{
				AsciiString powerName = mapIt->first;
				SuperweaponList& swList = mapIt->second;
				for (SuperweaponList::iterator listIt = swList.begin(); listIt != swList.end(); ++listIt)
				{
					SuperweaponInfo* swInfo = *listIt;

					// since this list tends to be somewhat sparse, we write stuff out pretty explicitly.
					xfer->xferInt(&playerIndex);
					
					AsciiString templateName = swInfo->getSpecialPowerTemplate()->getName();

					xfer->xferAsciiString(&templateName);
					xfer->xferAsciiString(&powerName);
					xfer->xferObjectID(&swInfo->m_id);
					xfer->xferUnsignedInt(&swInfo->m_timestamp);
					xfer->xferBool(&swInfo->m_hiddenByScript);
					xfer->xferBool(&swInfo->m_hiddenByScience);
					xfer->xferBool(&swInfo->m_ready);
          if ( currentVersion >= 3 )
          {
            xfer->xferBool( &swInfo->m_evaReadyPlayed );
          }
				}
			}
		}
		Int noMorePlayers = -1;		// our "done" sentinel
		xfer->xferInt(&noMorePlayers);
	}
	else if (xfer->getXferMode() == XFER_LOAD)
	{
		for (;;)
		{
			Int playerIndex;
			xfer->xferInt(&playerIndex);

			if (playerIndex == -1)
			{
				break;	// our "done" sentinel
			}
			else if (playerIndex < 0 || playerIndex >= MAX_PLAYER_COUNT)
			{
				DEBUG_CRASH(("SWInfo bad plyrindex\n"));
				throw INI_INVALID_DATA;
			}

			AsciiString templateName;
			xfer->xferAsciiString(&templateName);
			const SpecialPowerTemplate* powerTemplate = TheSpecialPowerStore->findSpecialPowerTemplate(templateName);
			if (powerTemplate == nullptr)
			{
				DEBUG_CRASH(("power %s not found\n",templateName.str()));
				throw INI_INVALID_DATA;
			}

			AsciiString powerName;
			ObjectID id;
			UnsignedInt timestamp;
			Bool hiddenByScript, hiddenByScience, ready, evaReadyPlayed;

			xfer->xferAsciiString(&powerName);
			xfer->xferObjectID(&id);
			xfer->xferUnsignedInt(&timestamp);
			xfer->xferBool(&hiddenByScript);
			xfer->xferBool(&hiddenByScience);
			xfer->xferBool(&ready);
      if ( currentVersion >= 3 )
      {
        xfer->xferBool( &evaReadyPlayed );
      }
      else
      {
        evaReadyPlayed = ready;
      }

			// srj sez: due to order-of-operation stuff, sometimes these will already exist,
			// sometimes not. not sure why. so handle both cases. 
			SuperweaponInfo* swInfo = findSWInfo(playerIndex, powerName, id, powerTemplate);
			if (swInfo == nullptr)
			{
				const Player* player = ThePlayerList->getNthPlayer(playerIndex);
				swInfo = newInstance(SuperweaponInfo)(
					id,
					timestamp,
					hiddenByScript,
					hiddenByScience,
					ready,
          evaReadyPlayed,
					m_superweaponNormalFont, 
					m_superweaponNormalPointSize, 
					m_superweaponNormalBold, 
					player->getPlayerColor(), 
					powerTemplate);
				m_superweapons[playerIndex][powerName].push_back(swInfo);
			}
			else
			{
				// swInfo->m_id = id;	// redundant, already matches
				swInfo->m_timestamp = timestamp;
				swInfo->m_hiddenByScript = hiddenByScript;
				swInfo->m_hiddenByScience = hiddenByScience;
				swInfo->m_ready = ready;
        swInfo->m_evaReadyPlayed = evaReadyPlayed;
			}
			swInfo->m_forceUpdateText = true;
		
		}
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void InGameUI::loadPostProcess( void )
{

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::setMouseCursor(Mouse::MouseCursor c)
{
	if (!TheMouse)
		return;

	TheMouse->setCursor(c);

	if (m_mouseMode == MouseMode::MOUSEMODE_GUI_COMMAND && c != Mouse::ARROW && c != Mouse::SCROLL)
		m_mouseModeCursor = c;

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
SuperweaponInfo* InGameUI::findSWInfo(Int playerIndex, const AsciiString& powerName, ObjectID id, const SpecialPowerTemplate *powerTemplate)
{
	SuperweaponMap::iterator mapIt = m_superweapons[playerIndex].find(powerName);
	if (mapIt != m_superweapons[playerIndex].end())
	{
		for (SuperweaponList::iterator listIt = mapIt->second.begin(); listIt != mapIt->second.end(); ++listIt)
		{
			if ((*listIt)->m_id == id)
			{
				return *listIt;
			}
		}
	}
	return nullptr;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::addSuperweapon(Int playerIndex, const AsciiString& powerName, ObjectID id, const SpecialPowerTemplate *powerTemplate)
{
	if (powerTemplate == nullptr)
		return;

	// srj sez: don't allow adding the same superweapon more than once. it can happen. not sure how. (srj)
	SuperweaponInfo* swInfo = findSWInfo(playerIndex, powerName, id, powerTemplate);
	if (swInfo != nullptr)
		return;

	const Player* player = ThePlayerList->getNthPlayer(playerIndex);
	Bool hiddenByScience = (powerTemplate->getRequiredScience() != SCIENCE_INVALID) && (player->hasScience(powerTemplate->getRequiredScience()) == false);

  DEBUG_LOG(("Adding superweapon UI timer\n"));
	SuperweaponInfo *info = newInstance(SuperweaponInfo)(
					id,
					static_cast<UnsignedInt>(-1),			// timestamp
					FALSE,	// hiddenByScript
					hiddenByScience,//Aaayeeee! This is meaningless and just clogs up the works, sez srj, nuke or repair or SHIP WITH(tm), ASAP
													// THe trouble is: There is no mechanism to clear this bit when the science is granted, thus,
													// the timer never, ever, ever get drawn.... unless the owning object is post-science constructed.
					FALSE,	// ready
          FALSE,  // evaReadyPlayed
					m_superweaponNormalFont, 
					m_superweaponNormalPointSize, 
					m_superweaponNormalBold, 
					player->getPlayerColor(), 
					powerTemplate);

	m_superweapons[playerIndex][powerName].push_back(info);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool InGameUI::removeSuperweapon(Int playerIndex, const AsciiString& powerName, ObjectID id, const SpecialPowerTemplate *powerTemplate)
{
	DEBUG_LOG(("Removing superweapon UI timer\n"));
	SuperweaponMap::iterator mapIt = m_superweapons[playerIndex].find(powerName);
	if (mapIt != m_superweapons[playerIndex].end())
	{
		SuperweaponList& swList = mapIt->second;
		for (SuperweaponList::iterator listIt = swList.begin(); listIt != swList.end(); ++listIt)
		{
			if ((*listIt)->m_id == id)
			{
				SuperweaponInfo *info = *listIt;
				swList.erase(listIt);
				deleteInstanceOf(info);
				if (swList.size() == 0)
				{
					m_superweapons[playerIndex].erase(mapIt);
				}
				return TRUE;
			}
		}
	}

	return FALSE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::objectChangedTeam(const Object *obj, Int oldPlayerIndex, Int newPlayerIndex)
{
	// if we already had it listed, remove and re-add it
	if (obj && oldPlayerIndex >= 0 && newPlayerIndex >= 0)
	{
		ObjectID id = obj->getID();
		AsciiString powerName;
		for (BehaviorModule** m = obj->getBehaviorModules(); *m; ++m)
		{
			SpecialPowerModuleInterface* sp = (*m)->getSpecialPower();
			if (!sp)
				continue;

			const SpecialPowerTemplate *powerTemplate = sp->getSpecialPowerTemplate();
			powerName = powerTemplate->getName();

			SuperweaponMap::iterator mapIt = m_superweapons[oldPlayerIndex].find(powerName);
			Bool found = false;
			if (mapIt != m_superweapons[oldPlayerIndex].end())
			{
				for (SuperweaponList::iterator listIt = mapIt->second.begin(); listIt != mapIt->second.end(); ++listIt)
				{
					if ((*listIt)->m_id == id)
					{
						removeSuperweapon(oldPlayerIndex, powerName, id, powerTemplate);
						addSuperweapon(newPlayerIndex, powerName, id, powerTemplate);
						found = true;
						break;
					}
				}
			}
			if (!found)
			{
				if( TheGameLogic->getFrame() == 0 && !obj->getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) &&
					obj->isKindOf( KINDOF_COMMANDCENTER ) == FALSE )
					addSuperweapon(newPlayerIndex, powerName, id, powerTemplate);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::hideObjectSuperweaponDisplayByScript(const Object *obj)
{
	setObjectSuperweaponHiddenByScript( obj->getID(), TRUE );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::showObjectSuperweaponDisplayByScript(const Object *obj)
{
	setObjectSuperweaponHiddenByScript( obj->getID(), FALSE );
}

void InGameUI::setObjectSuperweaponHiddenByScript(ObjectID objID, Bool hidden)
{
	for (Int playerIndex = 0; playerIndex < MAX_PLAYER_COUNT; ++playerIndex)
	{
		for (SuperweaponMap::iterator mapIt = m_superweapons[playerIndex].begin(); mapIt != m_superweapons[playerIndex].end(); ++mapIt)
		{
			for (SuperweaponList::iterator listIt = mapIt->second.begin(); listIt != mapIt->second.end(); ++listIt)
			{
				if ((*listIt)->m_id == objID)
				{
					(*listIt)->m_hiddenByScript = hidden;
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::setSuperweaponDisplayEnabledByScript(Bool enable)
{
	m_superweaponHiddenByScript = !enable;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool InGameUI::getSuperweaponDisplayEnabledByScript(void) const
{
	return m_superweaponHiddenByScript;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::addNamedTimer( const AsciiString& timerName, const UnicodeString& text, Bool isCountdown )
{
	NamedTimerInfo *info = newInstance( NamedTimerInfo );	
	info->m_timerName = timerName;
	info->color = m_namedTimerNormalColor;
	info->timerText = text;
	info->displayString = TheDisplayStringManager->newDisplayString();
	info->displayString->reset();
	info->displayString->setFont( TheFontLibrary->getFont( m_namedTimerNormalFont, 
		TheGlobalLanguageData->adjustFontSize(m_namedTimerNormalPointSize), m_namedTimerNormalBold ) );
	info->displayString->setText( UnicodeString::TheEmptyString );
	info->timestamp = static_cast<UnsignedInt>(-1);
	info->isCountdown = isCountdown;

//	GameFont *font = info->displayString->getFont();

	removeNamedTimer(timerName);
	m_namedTimers[timerName] = info;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::removeNamedTimer( const AsciiString& timerName )
{
	NamedTimerMapIt mapIt = m_namedTimers.find(timerName);
	if (mapIt != m_namedTimers.end())
	{
		TheDisplayStringManager->freeDisplayString( mapIt->second->displayString );
		deleteInstanceOf(mapIt->second);
		m_namedTimers.erase(mapIt);
		return;
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::showNamedTimerDisplay( Bool show )
{
	m_showNamedTimers = show;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
// The Generals InGameUI.ini sets MilitaryCaptionDelayMS here (Zero Hour: Language.ini, the entry in
// its InGameUI.ini is commented out). Without an entry in the table loading would fail.
//-------------------------------------------------------------------------------------------------
static void parseMilitaryCaptionDelayMS( INI *ini, void * /*instance*/, void * /*store*/, const void * /*userData*/ )
{
	Int value = INI::scanInt( ini->getNextToken() );
	if( TheGlobalLanguageData )
		TheGlobalLanguageData->m_militaryCaptionDelayMS = value;
}

const FieldParse InGameUI::s_fieldParseTable[] =
{
	{ "MaxSelectionSize",								INI::parseInt,					nullptr,		offsetof( InGameUI, m_maxSelectCount ) },

	{ "MessageColor1",									INI::parseColorInt,			nullptr,		offsetof( InGameUI, m_messageColor1 ) },
	{ "MessageColor2",									INI::parseColorInt,			nullptr,		offsetof( InGameUI, m_messageColor2 ) },
	{ "MessagePosition",								INI::parseICoord2D,			nullptr,		offsetof( InGameUI, m_messagePosition ) },
	{ "MessageFont",										INI::parseAsciiString,	nullptr,		offsetof( InGameUI, m_messageFont ) },
	{ "MessagePointSize",								INI::parseInt,					nullptr,		offsetof( InGameUI, m_messagePointSize ) },
	{ "MessageBold",										INI::parseBool,					nullptr,		offsetof( InGameUI, m_messageBold ) },
	{ "MessageDelayMS",									INI::parseInt,					nullptr,		offsetof( InGameUI, m_messageDelayMS ) },

	{ "MilitaryCaptionColor",						INI::parseRGBAColorInt,	nullptr,		offsetof( InGameUI, m_militaryCaptionColor ) },
	{ "MilitaryCaptionPosition",				INI::parseICoord2D,			nullptr,		offsetof( InGameUI, m_militaryCaptionPosition ) },

	{ "MilitaryCaptionTitleFont",				INI::parseAsciiString,	nullptr,		offsetof( InGameUI, m_militaryCaptionTitleFont ) },
	{ "MilitaryCaptionTitlePointSize",	INI::parseInt,					nullptr,		offsetof( InGameUI, m_militaryCaptionTitlePointSize ) },
	{ "MilitaryCaptionTitleBold",				INI::parseBool,					nullptr,		offsetof( InGameUI, m_militaryCaptionTitleBold ) },

	{ "MilitaryCaptionFont",						INI::parseAsciiString,	nullptr,		offsetof( InGameUI, m_militaryCaptionFont ) },
	{ "MilitaryCaptionPointSize",				INI::parseInt,					nullptr,		offsetof( InGameUI, m_militaryCaptionPointSize ) },
	{ "MilitaryCaptionBold",						INI::parseBool,					nullptr,		offsetof( InGameUI, m_militaryCaptionBold ) },

	{ "MilitaryCaptionRandomizeTyping",	INI::parseBool,					nullptr,		offsetof( InGameUI, m_militaryCaptionRandomizeTyping ) },
	{ "MilitaryCaptionSpeed",						INI::parseInt,					nullptr,		offsetof( InGameUI, m_militaryCaptionSpeed ) },
	{ "MilitaryCaptionDelayMS",					parseMilitaryCaptionDelayMS,	nullptr,	0 },	// Generals data

	{ "MilitaryCaptionPosition",				INI::parseICoord2D,			nullptr,		offsetof( InGameUI, m_militaryCaptionPosition ) },

	{ "SuperweaponCountdownPosition",					INI::parseCoord2D,			nullptr,		offsetof( InGameUI, m_superweaponPosition ) },
	{ "SuperweaponCountdownFlashDuration",		INI::parseDurationReal,	nullptr,		offsetof( InGameUI, m_superweaponFlashDuration ) },
	{ "SuperweaponCountdownFlashColor",				INI::parseColorInt,			nullptr,		offsetof( InGameUI, m_superweaponFlashColor ) },

	{ "SuperweaponCountdownNormalFont",				INI::parseAsciiString,	nullptr,		offsetof( InGameUI, m_superweaponNormalFont ) },
	{ "SuperweaponCountdownNormalPointSize",	INI::parseInt,					nullptr,		offsetof( InGameUI, m_superweaponNormalPointSize ) },
	{ "SuperweaponCountdownNormalBold",				INI::parseBool,					nullptr,		offsetof( InGameUI, m_superweaponNormalBold ) },

	{ "SuperweaponCountdownReadyFont",				INI::parseAsciiString,	nullptr,		offsetof( InGameUI, m_superweaponReadyFont ) },
	{ "SuperweaponCountdownReadyPointSize",		INI::parseInt,					nullptr,		offsetof( InGameUI, m_superweaponReadyPointSize ) },
	{ "SuperweaponCountdownReadyBold",				INI::parseBool,					nullptr,		offsetof( InGameUI, m_superweaponReadyBold ) },

	{ "NamedTimerCountdownPosition",					INI::parseCoord2D,			nullptr,		offsetof( InGameUI, m_namedTimerPosition ) },
	{ "NamedTimerCountdownFlashDuration",			INI::parseDurationReal,	nullptr,		offsetof( InGameUI, m_namedTimerFlashDuration ) },
	{ "NamedTimerCountdownFlashColor",				INI::parseColorInt,			nullptr,		offsetof( InGameUI, m_namedTimerFlashColor ) },

	{ "NamedTimerCountdownNormalFont",				INI::parseAsciiString,	nullptr,		offsetof( InGameUI, m_namedTimerNormalFont ) },
	{ "NamedTimerCountdownNormalPointSize",		INI::parseInt,					nullptr,		offsetof( InGameUI, m_namedTimerNormalPointSize ) },
	{ "NamedTimerCountdownNormalBold",				INI::parseBool,					nullptr,		offsetof( InGameUI, m_namedTimerNormalBold ) },
	{ "NamedTimerCountdownNormalColor",				INI::parseColorInt,			nullptr,		offsetof( InGameUI, m_namedTimerNormalColor ) },

	{ "NamedTimerCountdownReadyFont",					INI::parseAsciiString,	nullptr,		offsetof( InGameUI, m_namedTimerReadyFont ) },
	{ "NamedTimerCountdownReadyPointSize",		INI::parseInt,					nullptr,		offsetof( InGameUI, m_namedTimerReadyPointSize ) },
	{ "NamedTimerCountdownReadyBold",					INI::parseBool,					nullptr,		offsetof( InGameUI, m_namedTimerReadyBold ) },
	{ "NamedTimerCountdownReadyColor",				INI::parseColorInt,			nullptr,		offsetof( InGameUI, m_namedTimerReadyColor ) },

	{ "FloatingTextTimeOut",									INI::parseDurationUnsignedInt,		nullptr,		offsetof( InGameUI, m_floatingTextTimeOut ) },
	{ "FloatingTextMoveUpSpeed",							INI::parseVelocityReal,	nullptr,		offsetof( InGameUI, m_floatingTextMoveUpSpeed ) },
	{ "FloatingTextVanishRate",								INI::parseVelocityReal,	nullptr,		offsetof( InGameUI, m_floatingTextMoveVanishRate ) },

	{ "PopupMessageColor",								INI::parseColorInt,					nullptr,		offsetof( InGameUI, m_popupMessageColor ) },
	
	{ "DrawableCaptionFont",									INI::parseAsciiString,	nullptr,		offsetof( InGameUI, m_drawableCaptionFont ) },
	{ "DrawableCaptionPointSize",							INI::parseInt,					nullptr,		offsetof( InGameUI, m_drawableCaptionPointSize ) },
	{ "DrawableCaptionBold",									INI::parseBool,					nullptr,		offsetof( InGameUI, m_drawableCaptionBold ) },
	{ "DrawableCaptionColor",									INI::parseColorInt,			nullptr,		offsetof( InGameUI, m_drawableCaptionColor ) },

	{ "DrawRMBScrollAnchor",									INI::parseBool,					nullptr,		offsetof( InGameUI, m_drawRMBScrollAnchor ) },
	{ "MoveRMBScrollAnchor",									INI::parseBool,					nullptr,		offsetof( InGameUI, m_moveRMBScrollAnchor ) },

	{ "AttackDamageAreaRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_ATTACK_DAMAGE_AREA] ) },
	{ "AttackScatterAreaRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_ATTACK_SCATTER_AREA] ) },
	{ "AttackContinueAreaRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_ATTACK_CONTINUE_AREA] ) },
	{ "FriendlySpecialPowerRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_FRIENDLY_SPECIALPOWER] ) },
	{ "OffensiveSpecialPowerRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_OFFENSIVE_SPECIALPOWER] ) },
	{ "SuperweaponScatterAreaRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_SUPERWEAPON_SCATTER_AREA] ) },

	{ "GuardAreaRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_GUARD_AREA] ) },
	{ "EmergencyRepairRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_EMERGENCY_REPAIR] ) },

	{ "ParticleCannonRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_PARTICLECANNON] ) },
	{ "A10StrikeRadiusCursor",			RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_A10STRIKE] ) },
	{ "CarpetBombRadiusCursor",			RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_CARPETBOMB] ) },
	{ "DaisyCutterRadiusCursor",		RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_DAISYCUTTER] ) },
	{ "ParadropRadiusCursor",				RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_PARADROP] ) },
	{ "SpySatelliteRadiusCursor",		RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_SPYSATELLITE] ) },
	{ "SpectreGunshipRadiusCursor",	RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_SPECTREGUNSHIP] ) },
	{ "HelixNapalmBombRadiusCursor",RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_HELIX_NAPALM_BOMB] ) },
	
	{ "NuclearMissileRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_NUCLEARMISSILE] ) }, 
	{ "EMPPulseRadiusCursor",		  	RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_EMPPULSE] ) },
	{ "ArtilleryRadiusCursor",		  RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_ARTILLERYBARRAGE] ) },
	{ "FrenzyRadiusCursor",				  RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_FRENZY] ) },
	{ "NapalmStrikeRadiusCursor",		RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_NAPALMSTRIKE] ) },
	{ "ClusterMinesRadiusCursor",		RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_CLUSTERMINES] ) },
	
	{ "ScudStormRadiusCursor",			RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_SCUDSTORM] ) }, 
	{ "AnthraxBombRadiusCursor",		RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_ANTHRAXBOMB] ) },
	{ "AmbushRadiusCursor",					RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_AMBUSH] ) }, 
	{ "RadarRadiusCursor",					RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_RADAR] ) },
	{ "SpyDroneRadiusCursor",				RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[ RADIUSCURSOR_SPYDRONE] ) },

	{ "ClearMinesRadiusCursor",			RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[ RADIUSCURSOR_CLEARMINES] ) },
	{ "AmbulanceRadiusCursor",			RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( InGameUI, m_radiusCursors[ RADIUSCURSOR_AMBULANCE] ) },

	{ nullptr,													nullptr,										nullptr,		0 }  // keep this last
};

//-------------------------------------------------------------------------------------------------
/** Parse MouseCursor entry */
//-------------------------------------------------------------------------------------------------
void INI::parseInGameUIDefinition( INI* ini )
{
	if( TheInGameUI )
	{
		// parse the ini weapon definition
		ini->initFromINI( TheInGameUI, TheInGameUI->getFieldParse() );
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
InGameUI::InGameUI()
{
	Int i;

	
  m_inputEnabled = true;
	m_isDragSelecting = false;
	m_nextMoveHint = 0;
	m_selectCount = 0;
	m_frameSelectionChanged = 0;
  m_duringDoubleClickAttackMoveGuardHintTimer = 0;
  m_duringDoubleClickAttackMoveGuardHintStashedPosition.zero();
	m_maxSelectCount = -1;
	m_isScrolling = FALSE;
	m_isSelecting = FALSE;
	m_mouseMode = MouseMode::MOUSEMODE_DEFAULT;
	m_mouseModeCursor = Mouse::ARROW;
	m_mousedOverDrawableID = INVALID_DRAWABLE_ID;
	
	//Added By Sadullah Nader
	//Initializations missing and needed
	m_currentlyPlayingMovie.clear();
	m_militarySubtitle = nullptr;
	m_popupMessageData = nullptr;
	m_waypointMode = FALSE;
	m_clientQuiet = FALSE;
	
	m_messageColor1 = GameMakeColor( 255, 255, 255, 255 );
	m_messageColor2 = GameMakeColor( 180, 180, 180, 255 );
	m_messagePosition.x = 10;
	m_messagePosition.y = 10;
	m_messageFont = "Arial";
	m_messagePointSize = 10;
	m_messageBold = FALSE;
	m_messageDelayMS = 5000;

	m_militaryCaptionColor.red   = 200;
	m_militaryCaptionColor.green = 200;
	m_militaryCaptionColor.blue  = 30;
	m_militaryCaptionColor.alpha = 255;
	m_militaryCaptionPosition.x = 10;
	m_militaryCaptionPosition.y = 380;

	m_militaryCaptionTitleFont = "Courier";
	m_militaryCaptionTitlePointSize = 12;
	m_militaryCaptionTitleBold = TRUE;

	m_militaryCaptionFont = "Courier";
	m_militaryCaptionPointSize = 12;
	m_militaryCaptionBold = FALSE;

	m_militaryCaptionRandomizeTyping = FALSE;
	m_militaryCaptionSpeed = 1;
	m_popupMessageColor = GameMakeColor(255,255,255,255);

	m_tooltipsDisabledUntil = 0;

	// init hint lists
	for( i = 0; i < MAX_MOVE_HINTS; i++ )
	{

		m_moveHint[ i ].pos.zero();
		m_moveHint[ i ].sourceID = 0;
		m_moveHint[ i ].frame = 0;

	}  //  end for i

	for( i = 0; i < MAX_BUILD_PROGRESS; i++ )
	{

		m_buildProgress[ i ].m_thingTemplate = nullptr;
		m_buildProgress[ i ].m_percentComplete = 0.0f;
		m_buildProgress[ i ].m_control = nullptr;

	}  // end for i

	m_pendingGUICommand = nullptr;

	// allocate an array for the placement icons
	m_placeIcon = NEW Drawable* [ TheGlobalData->m_maxLineBuildObjects ];
	for( i = 0; i < TheGlobalData->m_maxLineBuildObjects; i++ )
		m_placeIcon[ i ] = nullptr;
	m_pendingPlaceType = nullptr;
	m_pendingPlaceSourceObjectID = INVALID_ID;
	m_preventLeftClickDeselectionInAlternateMouseModeForOneClick = FALSE;
	m_placeAnchorStart.x = m_placeAnchorStart.y = 0;
	m_placeAnchorEnd.x = m_placeAnchorEnd.y = 0;
	m_placeAnchorInProgress = FALSE;

	m_videoStream = nullptr;
	m_videoBuffer = nullptr;
	m_cameoVideoStream = nullptr;
	m_cameoVideoBuffer = nullptr;

	// message info
	for( i = 0; i < MAX_UI_MESSAGES; i++ )
	{

		m_uiMessages[ i ].fullText.clear();
		m_uiMessages[ i ].displayString = nullptr;
		m_uiMessages[ i ].timestamp = 0;
		m_uiMessages[ i ].color = 0;

	}  // end for i

	m_replayWindow = nullptr;
	m_messagesOn = TRUE;

	m_superweaponPosition.x = 0.7f;
	m_superweaponPosition.y = 0.7f;
	m_superweaponFlashDuration = 1.0f;
	m_superweaponNormalFont = "Arial";
	m_superweaponNormalPointSize = 10;
	m_superweaponNormalBold = FALSE;
	m_superweaponReadyFont = "Arial";
	m_superweaponReadyPointSize = 10;
	m_superweaponReadyBold = FALSE;

	m_superweaponFlashColor = GameMakeColor(255, 255, 255, 255);
	m_superweaponLastFlashFrame = 0;
	m_superweaponUsedFlashColor = TRUE; // so next one is false
	m_superweaponHiddenByScript = FALSE;

	m_namedTimerPosition.x = 0.05f;
	m_namedTimerPosition.y = 0.7f;
	m_namedTimerFlashDuration = 1.0f;
	m_namedTimerNormalFont = "Arial";
	m_namedTimerNormalPointSize = 10;
	m_namedTimerNormalBold = FALSE;
	m_namedTimerReadyFont = "Arial";
	m_namedTimerReadyPointSize = 10;
	m_namedTimerReadyBold = FALSE;


	m_namedTimerNormalColor	= GameMakeColor(255, 255,   0, 255);
	m_namedTimerReadyColor	= GameMakeColor(255,   0, 255, 255);
	m_namedTimerFlashColor	= GameMakeColor(  0, 255, 255, 255);
	m_namedTimerLastFlashFrame = 0;
	m_namedTimerUsedFlashColor = TRUE; // so next one is false
	m_showNamedTimers = TRUE;
	m_graceDisplayString = nullptr;
	m_graceShownSecs = -1;

	m_floatingTextTimeOut = DEFAULT_FLOATING_TEXT_TIMEOUT;
	m_floatingTextMoveUpSpeed = 1.0f;
	m_floatingTextMoveVanishRate = 0.1f;

	m_drawableCaptionFont = "Arial";
	m_drawableCaptionPointSize = 10;
	m_drawableCaptionBold = FALSE;
	m_drawableCaptionColor = GameMakeColor(255, 255, 255, 255);

	m_drawRMBScrollAnchor = FALSE;
	m_moveRMBScrollAnchor = FALSE;
	m_displayedMaxWarning = FALSE; 

	m_idleWorkerWin = nullptr;
	m_currentIdleWorkerDisplay = -1;

	m_waypointMode			= false;
	m_forceAttackMode		= false;
	m_forceMoveToMode		= false;
	m_attackMoveToMode	= false;
	m_preferSelection		= false;

	m_curRcType = RADIUSCURSOR_NONE;
	
	m_soloNexusSelectedDrawableID = INVALID_DRAWABLE_ID;

}  // end InGameUI

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
InGameUI::~InGameUI()
{
	delete TheControlBar;
	::Engine::context().controlBar = nullptr;

	// free all the display strings if we're
	removeMilitarySubtitle();

	stopMovie();
	stopCameoMovie();

	// remove any build available status
	placeBuildAvailable( nullptr, nullptr );
	setRadiusCursorNone();

	// delete the message resources
	freeMessageResources();

	// delete the array for the drawbles
	delete [] m_placeIcon;
	m_placeIcon = nullptr;

	// clear floating text
	clearFloatingText();

	// clear world animations
	clearWorldAnimations();
	resetIdleWorker();
	freeGraceDisplay();
}

//-------------------------------------------------------------------------------------------------
void InGameUI::freeGraceDisplay( void )
{
	if( m_graceDisplayString && TheDisplayStringManager )
		TheDisplayStringManager->freeDisplayString( m_graceDisplayString );
	m_graceDisplayString = nullptr;
	m_graceShownSecs = -1;
}

//-------------------------------------------------------------------------------------------------
/** Initialize the in game user interface */
//-------------------------------------------------------------------------------------------------
void InGameUI::init( void )
{
	INI ini;
	ini.load( AsciiString( "Data\\INI\\InGameUI.ini" ), INILoadType::INI_LOAD_OVERWRITE, nullptr );

	//override INI values with language localized values:
	if (TheGlobalLanguageData)
	{
		if (TheGlobalLanguageData->m_drawableCaptionFont.name.isNotEmpty())
		{	m_drawableCaptionFont = TheGlobalLanguageData->m_drawableCaptionFont.name;
			m_drawableCaptionPointSize = TheGlobalLanguageData->m_drawableCaptionFont.size;
			m_drawableCaptionBold = TheGlobalLanguageData->m_drawableCaptionFont.bold;
		}

		if (TheGlobalLanguageData->m_messageFont.name.isNotEmpty())
		{	m_messageFont = TheGlobalLanguageData->m_messageFont.name;
			m_messagePointSize = TheGlobalLanguageData->m_messageFont.size;
			m_messageBold = TheGlobalLanguageData->m_messageFont.bold;
		}

		if (TheGlobalLanguageData->m_militaryCaptionTitleFont.name.isNotEmpty())
		{	m_militaryCaptionTitleFont = TheGlobalLanguageData->m_militaryCaptionTitleFont.name;
			m_militaryCaptionTitlePointSize = TheGlobalLanguageData->m_militaryCaptionTitleFont.size;
			m_militaryCaptionTitleBold = TheGlobalLanguageData->m_militaryCaptionTitleFont.bold;
		}

		if (TheGlobalLanguageData->m_militaryCaptionFont.name.isNotEmpty())
		{	m_militaryCaptionFont = TheGlobalLanguageData->m_militaryCaptionFont.name;
			m_militaryCaptionPointSize = TheGlobalLanguageData->m_militaryCaptionFont.size;
			m_militaryCaptionBold = TheGlobalLanguageData->m_militaryCaptionFont.bold;
		}

		if (TheGlobalLanguageData->m_superweaponCountdownNormalFont.name.isNotEmpty())
		{	m_superweaponNormalFont = TheGlobalLanguageData->m_superweaponCountdownNormalFont.name;
			m_superweaponNormalPointSize = TheGlobalLanguageData->m_superweaponCountdownNormalFont.size;
			m_superweaponNormalBold = TheGlobalLanguageData->m_superweaponCountdownNormalFont.bold;
		}

		if (TheGlobalLanguageData->m_superweaponCountdownReadyFont.name.isNotEmpty())
		{	m_superweaponReadyFont = TheGlobalLanguageData->m_superweaponCountdownReadyFont.name;
			m_superweaponReadyPointSize = TheGlobalLanguageData->m_superweaponCountdownReadyFont.size;
			m_superweaponReadyBold = TheGlobalLanguageData->m_superweaponCountdownReadyFont.bold;
		}

		if (TheGlobalLanguageData->m_namedTimerCountdownNormalFont.name.isNotEmpty())
		{	m_namedTimerNormalFont = TheGlobalLanguageData->m_namedTimerCountdownNormalFont.name;
			m_namedTimerNormalPointSize = TheGlobalLanguageData->m_namedTimerCountdownNormalFont.size;
			m_namedTimerNormalBold = TheGlobalLanguageData->m_namedTimerCountdownNormalFont.bold;
		}

		if (TheGlobalLanguageData->m_namedTimerCountdownReadyFont.name.isNotEmpty())
		{	m_namedTimerReadyFont = TheGlobalLanguageData->m_namedTimerCountdownReadyFont.name;
			m_namedTimerReadyPointSize = TheGlobalLanguageData->m_namedTimerCountdownReadyFont.size;
			m_namedTimerReadyBold = TheGlobalLanguageData->m_namedTimerCountdownReadyFont.bold;
		}
	}

	/**@ todo we used to put in the hint spy translator, but it's difficult
	to order the translators when the code is not centralized so it has
	been moved to where all the other translators are attached in game client */

	// create the tactical view
	if (TheDisplay)
	{
		::Engine::context().tacticalView = createView();
		TheTacticalView->init();
		TheDisplay->attachView( TheTacticalView );

		// make the tactical display the full screen width for now
		TheTacticalView->setWidth( TheDisplay->getWidth());
		// make the tactical display 0.76 of full screen so no drawing under GUI.
		TheTacticalView->setHeight( static_cast<Int>(TheDisplay->getHeight() * 0.77f));
	}
	TheTacticalView->setDefaultView(0.0f, 0.0f, 1.0f);

	/** @todo this may be the wrong place to create the sidebar, but for now
	this is where it lives */
	createControlBar();

	/** @todo This may be the wrong place to create the replay menu, but for now
	this is where it lives */
	createReplayControl();

	// create the command bar
	::Engine::context().controlBar = NEW ControlBar;
	TheControlBar->init();

	m_windowLayouts.clear();

	m_soloNexusSelectedDrawableID = INVALID_DRAWABLE_ID;


}  // end init

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void InGameUI::setRadiusCursor(RadiusCursorType cursorType, const SpecialPowerTemplate* specPowTempl, WeaponSlotType weaponSlot)
{
	if (cursorType == m_curRcType)
		return;

	m_curRadiusCursor.clear();
	m_curRcType = RADIUSCURSOR_NONE;

	if (cursorType == RADIUSCURSOR_NONE)
		return;

	Object* obj = nullptr;
	if( m_pendingGUICommand && m_pendingGUICommand->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT )
	{
		if( ThePlayerList && ThePlayerList->getLocalPlayer() && specPowTempl != nullptr )
		{
			obj = ThePlayerList->getLocalPlayer()->findMostReadyShortcutSpecialPowerOfType( specPowTempl->getSpecialPowerType() );
		}
	}
	else
	{
		if (getSelectCount() == 0)
			return;

		Drawable *draw = getFirstSelectedDrawable();
		if (draw == nullptr)
			return;

		obj = draw->getObject();
	}

	if (obj == nullptr)
		return;
	
	Player* controller = obj->getControllingPlayer();
	if (controller == nullptr)
		return;

	Real radius = 0.0f;
	const Weapon* w = nullptr;
	switch (cursorType)
	{
		// already handled
		//case RADIUSCURSOR_NONE:
		//	return;
		case RADIUSCURSOR_ATTACK_DAMAGE_AREA:
			w = obj->getWeaponInWeaponSlot(weaponSlot);
			radius = w ? w->getPrimaryDamageRadius(obj) : 0.0f;
			break;
		case RADIUSCURSOR_ATTACK_SCATTER_AREA:
			w = obj->getWeaponInWeaponSlot(weaponSlot);
			radius = w ? (w->getScatterRadius() + w->getScatterTargetScalar()) : 0.0f;
			break;
		case RADIUSCURSOR_ATTACK_CONTINUE_AREA:
		case RADIUSCURSOR_CLEARMINES:
			w = obj->getWeaponInWeaponSlot(weaponSlot);
			radius = w ? w->getContinueAttackRange() : 0.0f;
			break;
		case RADIUSCURSOR_GUARD_AREA:
			radius = AIGuardMachine::getStdGuardRange(obj);
			break;
		case RADIUSCURSOR_FRIENDLY_SPECIALPOWER:
		case RADIUSCURSOR_OFFENSIVE_SPECIALPOWER:
		case RADIUSCURSOR_SUPERWEAPON_SCATTER_AREA:
		case RADIUSCURSOR_EMERGENCY_REPAIR:
		case RADIUSCURSOR_PARTICLECANNON: 
		case RADIUSCURSOR_A10STRIKE:
		case RADIUSCURSOR_SPECTREGUNSHIP:
    case RADIUSCURSOR_HELIX_NAPALM_BOMB:
		case RADIUSCURSOR_DAISYCUTTER:
		case RADIUSCURSOR_CARPETBOMB:
		case RADIUSCURSOR_PARADROP:
		case RADIUSCURSOR_SPYSATELLITE: 
		case RADIUSCURSOR_NUCLEARMISSILE: 
		case RADIUSCURSOR_EMPPULSE:
		case RADIUSCURSOR_ARTILLERYBARRAGE:
		case RADIUSCURSOR_FRENZY:
		case RADIUSCURSOR_NAPALMSTRIKE:
		case RADIUSCURSOR_CLUSTERMINES:
		case RADIUSCURSOR_SCUDSTORM: 
		case RADIUSCURSOR_ANTHRAXBOMB:
		case RADIUSCURSOR_AMBUSH: 
		case RADIUSCURSOR_RADAR:
		case RADIUSCURSOR_SPYDRONE:
		case RADIUSCURSOR_AMBULANCE:
			radius = specPowTempl ? specPowTempl->getRadiusCursorRadius() : 0.0f;
			break;

	}

	if (radius <= 0.0f)
		return;

	Coord3D pos = { 0, 0, 0 };	// will be updated right away
	m_radiusCursors[cursorType].createRadiusDecal(pos, radius, controller, m_curRadiusCursor);
	m_curRcType = cursorType;

	handleRadiusCursor();
}

//-------------------------------------------------------------------------------------------------
/** handle updating of "radius cursors" that follow the mouse pos */
//-------------------------------------------------------------------------------------------------
void InGameUI::handleRadiusCursor()
{
	if (!m_curRadiusCursor.isEmpty())
	{
		const MouseIO* mouseIO = TheMouse->getMouseStatus();
		Coord3D pos;

		//
		// if the mouse is in the radar window, the position in the world is that which is
		// represented by the radar, otherwise we use the mouse position itself transformed
		// from screen to world
		// But only if the radar is on.
		//
		Bool radarOn = TheRadar->isRadarForced() 
									|| ( !TheRadar->isRadarHidden() 
												&& ThePlayerList->getLocalPlayer() 
												&& ThePlayerList->getLocalPlayer()->hasRadar()
											);

		if( !radarOn  ||  (TheRadar->screenPixelToWorld( &mouseIO->pos, &pos ) == FALSE) )// if radar off, or point not on radar
			TheTacticalView->screenToTerrain( &mouseIO->pos, &pos );


    if ( TheGlobalData->m_doubleClickAttackMove && m_duringDoubleClickAttackMoveGuardHintTimer > 0 )
    {
      m_curRadiusCursor.setOpacity( m_duringDoubleClickAttackMoveGuardHintTimer * 0.1f );
  		m_curRadiusCursor.setPosition( m_duringDoubleClickAttackMoveGuardHintStashedPosition );	//world space position of center of decal

    }
    else
    {
  		m_curRadiusCursor.setPosition(pos);	//world space position of center of decal
      m_curRadiusCursor.update();
    }

  }
}


void InGameUI::triggerDoubleClickAttackMoveGuardHint( void ) 
{
  m_duringDoubleClickAttackMoveGuardHintTimer = 11; 
	const MouseIO* mouseIO = TheMouse->getMouseStatus();
	TheTacticalView->screenToTerrain( &mouseIO->pos, &m_duringDoubleClickAttackMoveGuardHintStashedPosition );
}


//-------------------------------------------------------------------------------------------------
/** Handle the placement "icons" that appear at the cursor when we're putting down a 
	* structure to build.  Note that this has additional logic to also show a line
	* of objects because when we build "walls" we want to draw a line of repeating
	* wall pieces on the map where we want to put all of them */
//-------------------------------------------------------------------------------------------------


void InGameUI::evaluateSoloNexus( Drawable *newlyAddedDrawable )
{

	m_soloNexusSelectedDrawableID = INVALID_DRAWABLE_ID;//failsafe...

	// short test: If the thing just added is a nonmobster, bail with NULL
	if ( newlyAddedDrawable )
	{
		const Object *newObj = newlyAddedDrawable->getObject();
		if ( newObj && ! ( newObj->isKindOf(KINDOF_MOB_NEXUS) || newObj->isKindOf(KINDOF_IGNORED_IN_GUI) ) )
			return;
	}

	//LoopAllSelectedDrawables
	UnsignedShort nexaeFound = 0;
	for( DrawableListCIt it = m_selectedDrawables.begin(); it != m_selectedDrawables.end(); ++it ) 
	{

		Drawable *draw = (*it);
		const Object *obj = draw->getObject();


		if ( ! obj )
			continue;
			
		if ( obj->isKindOf( KINDOF_MOB_NEXUS ) )
		{
			++nexaeFound;
			if ( nexaeFound == 1 )
			{
				m_soloNexusSelectedDrawableID = draw->getID();
			}
			else // darn! more than one!
			{
				m_soloNexusSelectedDrawableID = INVALID_DRAWABLE_ID;
				return;
			}
		}
		else if ( ! obj->isKindOf( KINDOF_IGNORED_IN_GUI ) )// darn! a non-angrymobster!
		{
			m_soloNexusSelectedDrawableID = INVALID_DRAWABLE_ID;
			return;
		}

	}  // end for


}


void InGameUI::handleBuildPlacements( void )
{

	//
	// if we're in the process of placing something we need up update one or more drawables
	// based on the position of the mouse
	//
	if( m_pendingPlaceType )
	{
		ICoord2D loc;
		Coord3D world;
		Real angle = m_placeIcon[ 0 ]->getOrientation();

		// update the angle of the icon to match any placement angle and pick the
		// location the icon will be at (anchored is the start, otherwise it's the mouse)
		if( isPlacementAnchored() )
		{
			ICoord2D start, end;
								
			// get the placement arrow points	
			getPlacementPoints( &start, &end );

			// set icon to anchor point
			loc = start;

			// only adjust angle if we've actually moved the mouse
			if( start.x != end.x || start.y != end.y )
			{
				Coord3D worldStart, worldEnd;

				// project the start and the end points of the line anchor into the 3D world
				TheTacticalView->screenToTerrain( &start, &worldStart );
				TheTacticalView->screenToTerrain( &end, &worldEnd );
				
				Coord2D v;
				v.x = worldEnd.x - worldStart.x;
				v.y = worldEnd.y - worldStart.y;
				angle = v.toAngle();

			}  // end if

		}  // end if
		else
		{
			const MouseIO *mouseIO = TheMouse->getMouseStatus();

			// location is the mouse position
			loc = mouseIO->pos;

		}  // end else

		// set the location and angle of the place icon
		/**@todo this whole orientation vector thing is LAME! Must replace, all I want to
		to do is set a simple angle and have it automatically change, ug! */
		TheTacticalView->screenToTerrain( &loc, &world );
		m_placeIcon[ 0 ]->setPosition( &world );
		m_placeIcon[ 0 ]->setOrientation( angle );


		//
		// check to see if this is a legal location to build something at and tint or "un-tint"
		// the cursor icons as appropriate.  This involves a pathfind which could be
		// expensive so we don't want to do it on every frame (althought that would be ideal)
		// If we discover there are cases that this is just too slow we should increase the
		// delay time between checks or we need to come up with a way of recording what is
		// valid and what isn't or "fudge" the results to feel "ok"
		//
		if( TheGameClient->getFrame() & 0x1 )
		{
			TheTerrainVisual->removeAllBibs();

			Object *builderObject = TheGameLogic->findObjectByID( getPendingPlaceSourceObjectID() );

			LegalBuildCode lbc;
			lbc = TheBuildAssistant->isLocationLegalToBuild( &world,
																											 m_pendingPlaceType,
																											 angle,
																											 BuildAssistant::USE_QUICK_PATHFIND |
																											 BuildAssistant::TERRAIN_RESTRICTIONS | 
																											 BuildAssistant::CLEAR_PATH |
																											 BuildAssistant::NO_OBJECT_OVERLAP |
																											 BuildAssistant::SHROUD_REVEALED |
																											 BuildAssistant::IGNORE_STEALTHED,
																											 builderObject,
																											 nullptr );
			if( lbc != LegalBuildCode::LBC_OK )
				m_placeIcon[ 0 ]->colorTint( &illegalBuildColor );
			else
				m_placeIcon[ 0 ]->colorTint( nullptr );

			


			// Add the bibs around the structure.
			if (lbc != LegalBuildCode::LBC_OK) 
			{
				TheTerrainVisual->addFactionBibDrawable(m_placeIcon[0], lbc != LegalBuildCode::LBC_OK);
			} else {
				TheTerrainVisual->removeFactionBibDrawable(m_placeIcon[0]);
			}
		}  // end if



		//
		// we have additional place icons when we're placing down a line of walls or other
		// similarly placed object ... for those we will have them be oriented the same way
		// as the first one, but we'll set their positions so that they "tile" end to end
		//
		if( isPlacementAnchored() && TheBuildAssistant->isLineBuildTemplate( m_pendingPlaceType ) )
		{
			Int i;

			// get our line placement points
			ICoord2D screenStart, screenEnd;
			getPlacementPoints( &screenStart, &screenEnd );

			// project the start and the end points of the line anchor into the 3D world
			Coord3D worldStart, worldEnd;
			TheTacticalView->screenToTerrain( &screenStart, &worldStart );
			TheTacticalView->screenToTerrain( &screenEnd, &worldEnd );

			// how big are each of our objects
			Real objectSize = m_pendingPlaceType->getTemplateGeometryInfo().getMajorRadius() * 2.0f;
			
			// what is our max tiling length we can make
			Int maxObjects = TheGlobalData->m_maxLineBuildObjects;

			// get the builder object that will be constructing things
			Object *builderObject = TheGameLogic->findObjectByID( TheInGameUI->getPendingPlaceSourceObjectID() );

			//
			// given the start/end points in the world and the the angle of the wall, fill
			// out an array of positions that "tile" this wall across the landscape
			//
			BuildAssistant::TileBuildInfo *tileBuildInfo;
			tileBuildInfo = TheBuildAssistant->buildTiledLocations( m_pendingPlaceType, angle,
																															&worldStart, &worldEnd,
																															objectSize, maxObjects,
																															builderObject );	

			// create any necessary drawables we need to "fill out" the line
			for( i = 0; i < tileBuildInfo->tilesUsed; i++ )
			{
			
				if( m_placeIcon[ i ] == nullptr )
					m_placeIcon[ i ] = TheThingFactory->newDrawable( m_pendingPlaceType,
																													 DRAWABLE_STATUS_NO_STATE_PARTICLES );

			}  // end for i

			//
			// destroy any drawables that we're not using anymore because a previous
			// line length was longer
			//
			for( i = tileBuildInfo->tilesUsed; i < maxObjects; i++ )
			{

				if( m_placeIcon[ i ] != nullptr )
					TheGameClient->destroyDrawable( m_placeIcon[ i ] );
				m_placeIcon[ i ] = nullptr;

			}  // end for i

			//
			// march down each drawable and set the position based on its position in the
			// line and set their angles all the same
			//
			for( i = 0; i < tileBuildInfo->tilesUsed; i++ )
			{

				// set the drawble position
				m_placeIcon[ i ]->setPosition( &tileBuildInfo->positions[ i ] );

				// set opacity for the drawble
				m_placeIcon[ i ]->setDrawableOpacity( placementOpacity );

				// set the drawable angle
				m_placeIcon[ i ]->setOrientation( angle );

			}  // end for i

		}  // end if

	}  // end if

}  // end handleBuildPlacements

//-------------------------------------------------------------------------------------------------
/** Pre-draw phase of the in game ui */
//-------------------------------------------------------------------------------------------------
void InGameUI::preDraw( void )
{

	// handle any "icons" for the act of building things and placing them in the world
	handleBuildPlacements();

	// handle radius-cursors, if any
	handleRadiusCursor();

	// draw the floating text first;
	drawFloatingText();

	// draw world animations
	updateAndDrawWorldAnimations();

}  // end preDraw

//-------------------------------------------------------------------------------------------------
/** Update the in game user interface */
//-------------------------------------------------------------------------------------------------
//DECLARE_PERF_TIMER(InGameUI_update)
void InGameUI::update( void )
{ 
	//USE_PERF_TIMER(InGameUI_update)
	Int i;

	/// @todo make sure this code gets called even when the UI is not being drawn
	if ( m_videoStream && m_videoBuffer )
	{
		if ( m_videoStream->isFrameReady())
		{
			m_videoStream->frameDecompress();
			m_videoStream->frameRender( m_videoBuffer.get() );
			m_videoStream->frameNext();
			if ( m_videoStream->frameIndex() == 0 )
			{
				stopMovie();
			}
		}
	}

	if ( m_cameoVideoStream && m_cameoVideoBuffer )
	{
		if ( m_cameoVideoStream->isFrameReady())
		{
			m_cameoVideoStream->frameDecompress();
			m_cameoVideoStream->frameRender( m_cameoVideoBuffer.get() );
			m_cameoVideoStream->frameNext();
//			if ( m_cameoVideoStream->frameIndex() == 0 )
//			{
//				stopMovie();
//			}
		}
	}

	//
	// remove any message strings that have expired, note that the oldest strings are
	// always at the end of the array (higher index numbers) so we can just remove things
	// from the rear and never have to worry about shifting entries cause we check every
	// frame
	//
	UnsignedInt currLogicFrame = TheGameLogic->getFrame();
	const int messageTimeout = m_messageDelayMS / LOGICFRAMES_PER_SECOND / 1000;
	UnsignedByte r, g, b, a;
	Int amount;
	for( i = MAX_UI_MESSAGES - 1; i >= 0; i-- )
	{

		if( static_cast<UnsignedInt>(currLogicFrame) - m_uiMessages[ i ].timestamp > static_cast<UnsignedInt>(messageTimeout) )
		{

			// get the current color of this text
			GameGetColorComponents( m_uiMessages[ i ].color, &r, &g, &b, &a );

			// start fading the alpha on this color down
			amount = REAL_TO_INT( ((currLogicFrame - m_uiMessages[ i ].timestamp) * 0.01f) );
			if( a - amount < 0 )
				a = 0;
			else
				a = static_cast<UnsignedByte>(a - amount);
			
			// set the new color
			m_uiMessages[ i ].color = GameMakeColor( r, g, b, a );

			// when alpha is completely zero we remove this string
			if( a == 0 )
				removeMessageAtIndex( i );

		}  // end if

	}  // end for i

	//
	// Update the Military Subtitle display
	//
	if( m_militarySubtitle )		// if we have a subtitle, work on it
	{
		// if the timeis frozen by a script, then we still want the text to display
		if(TheScriptEngine->isTimeFrozenScript())
		{
			m_militarySubtitle->lifetime--;
			m_militarySubtitle->blockBeginFrame--;
			m_militarySubtitle->incrementOnFrame--;
		}
		// if it's time to remove the subtitle, Then remove it
		if(static_cast<Int>(m_militarySubtitle->lifetime) < static_cast<Int>(currLogicFrame))
		{
			//steal colins fade from above :)
			GameGetColorComponents( m_militarySubtitle->color, &r, &g, &b, &a );
			// start fading the alpha on this color down
			amount = REAL_TO_INT( ((currLogicFrame - m_militarySubtitle->lifetime ) * 0.1f) );
			if( a - amount < 0 )
			{
				removeMilitarySubtitle();
			}
			else
			{
				a = static_cast<UnsignedByte>(a - amount);
				m_militarySubtitle->color = GameMakeColor(r, g, b, a);
			}
		}
		else
		{
			// trigger whether or not we should draw the block	
			if( m_militarySubtitle->blockBeginFrame + 9 < currLogicFrame )
			{
				m_militarySubtitle->blockBeginFrame = currLogicFrame;
				m_militarySubtitle->blockDrawn = !m_militarySubtitle->blockDrawn;
			}

			// If it's time to add another letter to the display string, lets do that.
			if( m_militarySubtitle->incrementOnFrame < currLogicFrame )
			{
				// first grab the letter we want to add
				WideChar tempWChar = m_militarySubtitle->subtitle.getCharAt(m_militarySubtitle->index);
				// if that letter is a return, add a new line
				if(tempWChar == L'\n')
				{
					// increment the Block position's Y value to draw it on the next line
					Int height;
					m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString]->getSize(nullptr, &height);
					m_militarySubtitle->blockPos.y = m_militarySubtitle->blockPos.y + height;

					// Now add a new display string
					m_militarySubtitle->currentDisplayString++;
					if(!(m_militarySubtitle->currentDisplayString >= MAX_SUBTITLE_LINES) )
					{	
						m_militarySubtitle->blockPos.x = m_militarySubtitle->position.x;
						m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString] = TheDisplayStringManager->newDisplayString();
						m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString]->reset();
						m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString]->setFont(	TheFontLibrary->getFont( m_militaryCaptionFont, TheGlobalLanguageData->adjustFontSize(m_militaryCaptionPointSize), m_militaryCaptionBold ) )	;

						m_militarySubtitle->blockDrawn = TRUE;
						m_militarySubtitle->incrementOnFrame = currLogicFrame + static_cast<Int>((static_cast<Real>(LOGICFRAMES_PER_SECOND) * TheGlobalLanguageData->m_militaryCaptionDelayMS)/1000.0f);
					}
					else
					{
						// if we've exceeded the allocated number of display strings, this will force us to essentially truncate the remaining text
						m_militarySubtitle->index = m_militarySubtitle->subtitle.getLength();
						DEBUG_CRASH(("You're Only Allowed to use %d lines of subtitle text\n",MAX_SUBTITLE_LINES));
					}
				}
				else
				{
					// okay, we're not a \n, lets append this character to the display string
					m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString]->appendChar(tempWChar);
					// increment the draw position of the block
					Int width;
					m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString]->getSize(&width,nullptr);
					m_militarySubtitle->blockPos.x = m_militarySubtitle->position.x + width;

					// lets make a sound
					// Not static: the event's string buffer would otherwise outlive the leak report; addAudioEvent
					// copies it anyway.
					AudioEventRTS click("MilitarySubtitlesTyping");
					TheAudio->addAudioEvent(&click);
					if(TheGlobalLanguageData)
						m_militarySubtitle->incrementOnFrame = currLogicFrame + TheGlobalLanguageData->m_militaryCaptionSpeed;
					else
						m_militarySubtitle->incrementOnFrame = currLogicFrame + m_militaryCaptionSpeed;

				}
				// increment the index			
				m_militarySubtitle->index++;
				if(m_militarySubtitle->index >= static_cast<UnsignedInt>(m_militarySubtitle->subtitle.getLength()))
				{
					// We're at the end of the subtitle, set everything to persist till the subtitle has expired
					m_militarySubtitle->incrementOnFrame = m_militarySubtitle->lifetime + 1;
				}
	/*
							else
								{
									// randomize the space between printing of characters
									if(GameClientRandomValueReal(0,1) < 0.95f)
									{
										m_militarySubtitle->incrementOnFrame = GameClientRandomValue(2, 5) + currLogicFrame;
									}
									else
									{
										m_militarySubtitle->incrementOnFrame = GameClientRandomValue(10, 13) + currLogicFrame;
									}
								}*/
				
			}
		}
	}

	// update the player money window if the money amount has changed
	// this seems like as good a place as any to do the power hide/show
	static Int lastMoney = -1;
	static NameKeyType moneyWindowKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:MoneyDisplay" );	
	static NameKeyType powerWindowKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:PowerWindow" );	

	GameWindow *moneyWin = TheWindowManager->winGetWindowFromId( nullptr, moneyWindowKey );
	GameWindow *powerWin = TheWindowManager->winGetWindowFromId( nullptr, powerWindowKey );
//	if( moneyWin == NULL )
//	{
//		NameKeyType moneyWindowKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:MoneyDisplay" );	
//
//		moneyWin = TheWindowManager->winGetWindowFromId( NULL, moneyWindowKey );
//
//	}  // end if
	Player *moneyPlayer = nullptr;
	if( TheControlBar->isObserverControlBarOn())
		moneyPlayer = TheControlBar->getObserverLookAtPlayer();
	else
		moneyPlayer = ThePlayerList->getLocalPlayer();
	if( moneyPlayer)
	{
		Int currentMoney = moneyPlayer->getMoney()->countMoney();
		if( lastMoney != currentMoney )
		{
			UnicodeString buffer;

			buffer.format( TheGameText->fetch( "GUI:ControlBarMoneyDisplay" ), currentMoney );
			GadgetStaticTextSetText( moneyWin, buffer );
			lastMoney = currentMoney;
			
		}  // end if
		moneyWin->winHide(FALSE);
		powerWin->winHide(FALSE);
	}
	else
	{
		moneyWin->winHide(TRUE);
		powerWin->winHide(TRUE);
	}
	
	// Update the floating Text;
	updateFloatingText();

	// update the control bar
	TheControlBar->update();

	updateIdleWorker();

	// update any random window layout that so requests
	for (std::list<WindowLayout *>::iterator it = m_windowLayouts.begin(); it != m_windowLayouts.end(); ++it)
	{
		WindowLayout *layout = *it;
		layout->runUpdate();
	}

	//Handle keyboard camera rotations
	if( m_cameraRotatingLeft && !m_cameraRotatingRight )
	{
		//Keyboard rotate left
		TheTacticalView->setAngle( TheTacticalView->getAngle() - TheGlobalData->m_keyboardCameraRotateSpeed );
	}
	if( m_cameraRotatingRight && !m_cameraRotatingLeft )
	{
		//Keyboard rotate right
		TheTacticalView->setAngle( TheTacticalView->getAngle() + TheGlobalData->m_keyboardCameraRotateSpeed );
	}
	if( m_cameraZoomingIn && !m_cameraZoomingOut )
	{
		//Keyboard zoom in
		TheTacticalView->zoomIn();
	}
	if( m_cameraZoomingOut && !m_cameraZoomingIn )
	{
		//Keyboard zoom out
		TheTacticalView->zoomOut();
	}


}  // end update

//-------------------------------------------------------------------------------------------------
void InGameUI::registerWindowLayout( WindowLayout *layout )
{
	unregisterWindowLayout(layout); // sanity
	m_windowLayouts.push_back(layout);
}

//-------------------------------------------------------------------------------------------------
void InGameUI::unregisterWindowLayout( WindowLayout *layout )
{
	for (std::list<WindowLayout *>::iterator it = m_windowLayouts.begin(); it != m_windowLayouts.end(); ++it)
	{
		if (*it == layout)
		{
			m_windowLayouts.erase(it);
			return;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Reset the in game user interface */
//-------------------------------------------------------------------------------------------------
void InGameUI::reset( void )
{
	m_isQuitMenuVisible = FALSE;
	m_inputEnabled = true;
	// reset the command bar
	TheControlBar->reset();

	TheTacticalView->setDefaultView(0.0f, 0.0f, 1.0f);

	ResetInGameChat();

	// stop any movie currently playing
	stopMovie();

	// remove any pending GUI command
	setGUICommand( nullptr );

	// remove any build available status
	placeBuildAvailable( nullptr, nullptr );

	// free any message resources allocated
	freeMessageResources();

	Int i;
	for (i=0; i<MAX_PLAYER_COUNT; ++i)
	{
		for (SuperweaponMap::iterator mapIt = m_superweapons[i].begin(); mapIt != m_superweapons[i].end(); ++mapIt)
		{
			for (SuperweaponList::iterator listIt = mapIt->second.begin(); listIt != mapIt->second.end(); ++listIt)
			{
				SuperweaponInfo *info = *listIt;
				deleteInstanceOf(info);
			}
			mapIt->second.clear();
		}
		m_superweapons[i].clear();
	}

	for (NamedTimerMapIt timerIt = m_namedTimers.begin(); timerIt != m_namedTimers.end(); ++timerIt)
	{
		NamedTimerInfo *info = timerIt->second;
		TheDisplayStringManager->freeDisplayString(info->displayString);
		deleteInstanceOf(info);
	}
	m_namedTimers.clear();
	m_namedTimerLastFlashFrame = 0;
	m_namedTimerUsedFlashColor = TRUE; // so next one is false
	m_showNamedTimers = TRUE;
	freeGraceDisplay();

	removeMilitarySubtitle();
	clearPopupMessageData();
	m_superweaponLastFlashFrame = 0;
	m_superweaponUsedFlashColor = TRUE; // so next one is false
	m_superweaponHiddenByScript = FALSE;

	clearFloatingText();
	clearWorldAnimations();
	resetIdleWorker();
	// clear hint lists
	for( i = 0; i < MAX_MOVE_HINTS; i++ )
	{

		m_moveHint[ i ].pos.zero();
		m_moveHint[ i ].sourceID = 0;
		m_moveHint[ i ].frame = 0;

	}  //  end for i

	m_waypointMode			= false;
	m_forceAttackMode		= false;
	m_forceMoveToMode		= false;
	m_attackMoveToMode	= false;
	m_preferSelection		= false;
	m_clientQuiet    = false;
	
	m_windowLayouts.clear();

	m_tooltipsDisabledUntil = 0;

	UpdateDiplomacyBriefingText(AsciiString::TheEmptyString, TRUE);
}  // end reset

//-------------------------------------------------------------------------------------------------
/** Free any resources we used for our messages */
//-------------------------------------------------------------------------------------------------
void InGameUI::freeMessageResources( void )
{
	Int i;

	// release display strings and set text to empty
	for( i = 0; i < MAX_UI_MESSAGES; i++ )
	{

		// emtpy text
		m_uiMessages[ i ].fullText.clear();

		// free display string
		if( m_uiMessages[ i ].displayString )
			TheDisplayStringManager->freeDisplayString( m_uiMessages[ i ].displayString );
		m_uiMessages[ i ].displayString = nullptr;

		// set timestamp to zero
		m_uiMessages[ i ].timestamp = 0;

	}  // end for i

}  // end freeMessageResources

//-------------------------------------------------------------------------------------------------
/** Same as the unicode message method, but this takes an ascii string which is assumed
	* to me a string manager label */
//-------------------------------------------------------------------------------------------------
// srj sez: passing as const-ref screws up varargs for some reason. dunno why. just pass by value.
void InGameUI::message( AsciiString stringManagerLabel, ... )
{
	UnicodeString stringManagerString;
	UnicodeString formattedMessage;

	// fetch the string from the string manger
	stringManagerString = TheGameText->fetch( stringManagerLabel.str() );

	// construct the final text after formatting
	va_list args;
	// the label is passed by value on purpose (see above); va_start accepts it in MSVC
#pragma warning(suppress : 4840)
  va_start( args, stringManagerLabel );
	WideChar buf[ UnicodeString::MAX_FORMAT_BUF_LEN ];
  if( _vsnwprintf(buf, sizeof( buf )/sizeof( WideChar ) - 1, stringManagerString.str(), args ) < 0 )
			throw ERROR_OUT_OF_MEMORY;
	formattedMessage.set( buf );
  va_end(args);

	// add the text to the ui
	addMessageText( formattedMessage );

}  // end 

//-------------------------------------------------------------------------------------------------
/** Interface for display text messages to the user */
//-------------------------------------------------------------------------------------------------
// srj sez: passing as const-ref screws up varargs for some reason. dunno why. just pass by value.
void InGameUI::message( UnicodeString format, ... )
{
	UnicodeString formattedMessage;

	// construct the final text after formatting
	va_list args;
	// the label is passed by value on purpose (see above); va_start accepts it in MSVC
#pragma warning(suppress : 4840)
  va_start( args, format );
	WideChar buf[ UnicodeString::MAX_FORMAT_BUF_LEN ];
  if( _vsnwprintf(buf, sizeof( buf )/sizeof( WideChar ) - 1, format.str(), args ) < 0 )
			throw ERROR_OUT_OF_MEMORY;
	formattedMessage.set( buf );
  va_end(args);

	// add the text to the ui
	addMessageText( formattedMessage );

}  // end message

//-------------------------------------------------------------------------------------------------
/** Interface for display text messages to the user */
//-------------------------------------------------------------------------------------------------
// srj sez: passing as const-ref screws up varargs for some reason. dunno why. just pass by value.
void InGameUI::messageColor( const RGBColor *rgbColor, UnicodeString format, ... )
{
	UnicodeString formattedMessage;

	// construct the final text after formatting
	va_list args;
	// the label is passed by value on purpose (see above); va_start accepts it in MSVC
#pragma warning(suppress : 4840)
  va_start( args, format );
	WideChar buf[ UnicodeString::MAX_FORMAT_BUF_LEN ];
  if( _vsnwprintf(buf, sizeof( buf )/sizeof( WideChar ) - 1, format.str(), args ) < 0 )
			throw ERROR_OUT_OF_MEMORY;
	formattedMessage.set( buf );
  va_end(args);

	// add the text to the ui
	addMessageText( formattedMessage, rgbColor );

}  // end message

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void InGameUI::addMessageText( const UnicodeString& formattedMessage, const RGBColor *rgbColor )
{
	Int i;
	Color color1 = m_messageColor1;
	Color color2 = m_messageColor2;

	if (rgbColor)
	{
		color1 = rgbColor->getAsInt() | GameMakeColor( 0, 0, 0, 255 );
		color2 = rgbColor->getAsInt() | GameMakeColor( 0, 0, 0, 255 );
	}

	// delete the message stuff at the last index
	m_uiMessages[ MAX_UI_MESSAGES - 1 ].fullText.clear();
	if( m_uiMessages[ MAX_UI_MESSAGES - 1 ].displayString )
		TheDisplayStringManager->freeDisplayString( m_uiMessages[ MAX_UI_MESSAGES - 1 ].displayString );
	m_uiMessages[ MAX_UI_MESSAGES - 1 ].displayString = nullptr;
	m_uiMessages[ MAX_UI_MESSAGES - 1 ].timestamp = 0;

	// shift all the messages down one index and remove the last one
	for( i = MAX_UI_MESSAGES - 1; i >= 1; i-- )
		m_uiMessages[ i ] = m_uiMessages[ i - 1 ];

	//
	// set the new message in index 0, note that we need to allocate a display string, but
	// we do not need to free the one that is already there because it has been moved
	// "up" an index
	//
	m_uiMessages[ 0 ].fullText = formattedMessage;
	m_uiMessages[ 0 ].timestamp = TheGameLogic->getFrame();
	m_uiMessages[ 0 ].displayString = TheDisplayStringManager->newDisplayString();
	m_uiMessages[ 0 ].displayString->setFont( TheFontLibrary->getFont( m_messageFont, 
																						TheGlobalLanguageData->adjustFontSize(m_messagePointSize), m_messageBold ) );
	m_uiMessages[ 0 ].displayString->setText( m_uiMessages[ 0 ].fullText );
	
	//
	// assign a color for this string instance that will stay with it no matter what
	// line it is rendered on
	//
	if( m_uiMessages[ 1 ].displayString == nullptr || m_uiMessages[ 1 ].color == color2 )
		m_uiMessages[ 0 ].color = color1;
	else
		m_uiMessages[ 0 ].color = color2;

}  // end addFormattedMessage

//-------------------------------------------------------------------------------------------------
/** Remove the message on screen at index i */
//-------------------------------------------------------------------------------------------------
void InGameUI::removeMessageAtIndex( Int i )
{

	m_uiMessages[ i ].fullText.clear();
	if( m_uiMessages[ i ].displayString )
		TheDisplayStringManager->freeDisplayString( m_uiMessages[ i ].displayString );
	m_uiMessages[ i ].displayString = nullptr;
	m_uiMessages[ i ].timestamp = 0;

}  // end removeMessageAtIndex

#if defined(_DEBUG)
#define AI_DEBUG_TOOLTIPS		1

#ifdef AI_DEBUG_TOOLTIPS
#include "Common/StateMachine.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/AIPathfind.h"
#endif // AI_DEBUG_TOOLTIPS

#endif // defined(_DEBUG)

//-------------------------------------------------------------------------------------------------
/// Get drawable ID under cursor
//-------------------------------------------------------------------------------------------------
DrawableID InGameUI::getMousedOverDrawableID( void ) const
{

	return m_mousedOverDrawableID;

}

//-------------------------------------------------------------------------------------------------
/// set right-click scroll mode
//-------------------------------------------------------------------------------------------------
void InGameUI::setScrolling( Bool isScrolling )
{
	if (m_isScrolling == isScrolling)
	{
		return;
	}

	if (isScrolling)
	{
		TheMouse->capture();
		setMouseCursor( Mouse::SCROLL );

		// break any camera locks
		TheTacticalView->setCameraLock( INVALID_ID );
		TheTacticalView->setCameraLockDrawable( nullptr );
	}
	else
	{
		setMouseCursor( Mouse::ARROW );
		TheMouse->releaseCapture();
	}

	m_isScrolling = isScrolling;

}

//-------------------------------------------------------------------------------------------------
/// are we scrolling?
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isScrolling( void )
{
	return m_isScrolling;
}

//-------------------------------------------------------------------------------------------------
/// set drag select mode
//-------------------------------------------------------------------------------------------------
void InGameUI::setSelecting( Bool isSelecting )
{
	if (m_isSelecting == isSelecting)
	{
		return;
	}

	//setMouseCursor( Mouse::SELECTING );
	m_isSelecting = isSelecting;
}

//-------------------------------------------------------------------------------------------------
/// are we selecting?
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isSelecting( void )
{
	return m_isSelecting;
}

//-------------------------------------------------------------------------------------------------
/// get scroll amount
//-------------------------------------------------------------------------------------------------
void InGameUI::setScrollAmount( Coord2D amt )
{
	m_scrollAmt = amt;
}

//-------------------------------------------------------------------------------------------------
/// get scroll amount
//-------------------------------------------------------------------------------------------------
Coord2D InGameUI::getScrollAmount( void )
{
	return m_scrollAmt;
}

//-------------------------------------------------------------------------------------------------
/** Like the building "placement" mode, clicking on some buttons in the UI require us to
	* provide additional data by clicking on a target object/location in the world.  This
	* is where we enable that "mode" so that we can get the additional data needed for a
	* command from the user */
//-------------------------------------------------------------------------------------------------
void InGameUI::setGUICommand( const CommandButton *command )
{
	if (TheRecorder->getMode() == RecorderModeType::RECORDERMODETYPE_PLAYBACK)
		return;

	// sanity
	if( command )
	{

		if( BitTest( command->getOptions(), COMMAND_OPTION_NEED_TARGET ) == FALSE )
		{

			DEBUG_ASSERTCRASH( 0, ("setGUICommand: Command '%s' does not need additional user interaction\n",	
														command->getName().str()) );
			m_pendingGUICommand = nullptr;
			m_mouseMode = MouseMode::MOUSEMODE_DEFAULT;
			return;

		}  // end if

		m_mouseMode = MouseMode::MOUSEMODE_GUI_COMMAND;

	}  // end if
	else
	{
		m_mouseMode = MouseMode::MOUSEMODE_DEFAULT;
	}

	// set the command
	m_pendingGUICommand = command;

	// set the mouse cursor for commands that need a targeting or to normal with no command
	if( command && BitTest( command->getOptions(), COMMAND_OPTION_NEED_TARGET ) && !command->isContextCommand() )
	{
		setMouseCursor( Mouse::ARROW );// This occurs on the mouse-up of a panel button, so make an arrow
		// the mouseoverhint code will take care of the cursor context, once the mouse leaves the panel
		// but we will set the radius cursor here, so you can see it bleeding out from beneath the panel

		setRadiusCursor(command->getRadiusCursorType(), //*****************************************************************
										command->getSpecialPowerTemplate(),
										command->getWeaponSlot());
	}
	else
	{
		if (TheMouse)
		{
			setMouseCursor( Mouse::ARROW );
		}
		setRadiusCursorNone();
	}

	m_mouseModeCursor = TheMouse->getMouseCursor();

}  // end setGUICommand

//-------------------------------------------------------------------------------------------------
/** Get the pending gui command */
//-------------------------------------------------------------------------------------------------
const CommandButton *InGameUI::getGUICommand( void ) const
{

	return m_pendingGUICommand;

} 

//-------------------------------------------------------------------------------------------------
/** Destroy any drawables we have in our placement icon array and set to NULL */
//-------------------------------------------------------------------------------------------------
void InGameUI::destroyPlacementIcons( void )
{
	Int i;

	for( i = 0; i < TheGlobalData->m_maxLineBuildObjects; ++i )
	{

		if( m_placeIcon[ i ] ) 
		{
			TheTerrainVisual->removeFactionBibDrawable(m_placeIcon[ i ]);
			TheGameClient->destroyDrawable( m_placeIcon[ i ] );
		}
		m_placeIcon[ i ] = nullptr;

	}  // end for i
	TheTerrainVisual->removeAllBibs();

}  // end destroyPlacementIcons

//-------------------------------------------------------------------------------------------------
/** User has clicked on a built item that requires placement in the world.  We will 
	* record what that thing is so that the we can catch the next click in the world
	* and try to place the object there */
//-------------------------------------------------------------------------------------------------
void InGameUI::placeBuildAvailable( const ThingTemplate *build, Drawable *buildDrawable )
{

	if (build != nullptr)
	{
		// if building something, no radius cursor, thankew
		setRadiusCursorNone();
	}

	//
	// if we're setting another place available, but we're somehow already in the placement
	// mode, get out of it before we start a new one
	//
	if( m_pendingPlaceType != nullptr && build != nullptr )
		placeBuildAvailable( nullptr, nullptr );

	//
	// keep a record of what we are trying to place, if we are already trying to
	// place something, it is overwritten
	//
	m_pendingPlaceType = build;

	//Keep the prev pending place for left click deselection prevention in alternate mouse mode.
	//We want to keep our dozer selected after initiating construction.
	setPreventLeftClickDeselectionInAlternateMouseModeForOneClick( m_pendingPlaceSourceObjectID != INVALID_ID );
	m_pendingPlaceSourceObjectID = INVALID_ID;

	Object *sourceObject = nullptr;
	if( buildDrawable )
		sourceObject = buildDrawable->getObject();
	if( sourceObject )
		m_pendingPlaceSourceObjectID = sourceObject->getID();

	//
	// hack, change our cursor to at least something different ... also note that it's
	// possible to not have the mouse yet, as some UI systems as part of initialization
	// make sure that there isn't anything valid for to "place build"
	//
	if( TheMouse )
	{

		if( build )
		{
			m_mouseMode = MouseMode::MOUSEMODE_BUILD_PLACE;
			m_mouseModeCursor = Mouse::CROSS;

			Drawable *draw;

			// capture the mouse for our window, windows is lame and changes it if we don't
			TheMouse->capture();

			// hack for changing cursor
			setMouseCursor( Mouse::CROSS );

			// deselect all drawables, otherwise they move to the place we click
			///@ todo when message stream order more formalized eliminate this
//			TheInGameUI->deselectAllDrawables();

			// create a drawble of what we are building to be "attached" at the cursor
			draw = TheThingFactory->newDrawable( build, DRAWABLE_STATUS_NO_STATE_PARTICLES );
			if (sourceObject)
			{
				if (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT)
					draw->setIndicatorColor(sourceObject->getControllingPlayer()->getPlayerNightColor());
				else
					draw->setIndicatorColor(sourceObject->getControllingPlayer()->getPlayerColor());
			}
			DEBUG_ASSERTCRASH( draw, ("Unable to create icon at cursor for placement '%s'\n",
												 build->getName().str()) );

			//
			// set the initial angle of the free floating building to the property from INI
			// we have this so we can have the "cool" face the user until they click and
			// pick an actual direction for placement
			//
			Real angle = build->getPlacementViewAngle();

			// don't forget to take into account the current view angle
			// angle += TheTacticalView->getAngle();	Don't do this - makes odd angled building placements.  jba.

			// set the angle in the icon we just created
			draw->setOrientation( angle );

			// set the build icon attached to the cursor to be "see-thru"
			draw->setDrawableOpacity( placementOpacity );

			// set the "icon" in the icon array at the first index
			DEBUG_ASSERTCRASH( m_placeIcon[ 0 ] == nullptr, ("placeBuildAvailable, build icon array is not empty!") );
			m_placeIcon[ 0 ] = draw;	

		}  // end if
		else
		{
			if (m_mouseMode == MouseMode::MOUSEMODE_BUILD_PLACE)
			{
				m_mouseMode = MouseMode::MOUSEMODE_DEFAULT;
				m_mouseModeCursor = Mouse::ARROW;
			}

			TheMouse->releaseCapture();
			setMouseCursor( Mouse::ARROW );
			setPlacementStart( nullptr );

			// if we have a place icons destroy them
			destroyPlacementIcons();

			if( sourceObject )
			{
				ProductionUpdateInterface *puInterface = sourceObject->getProductionUpdateInterface();
				if( puInterface )
				{
					//Clear the special power mode for construction if we set it. Actually call it everytime
					//rather than checking if it's set before clearing (cheaper).
					puInterface->setSpecialPowerConstructionCommandButton( nullptr );
				}
			}

		}  // end else

	}  // end if

}  // end placeBuildAvailable

//-------------------------------------------------------------------------------------------------
/** Return the thing we're attempting to place */
//-------------------------------------------------------------------------------------------------
const ThingTemplate *InGameUI::getPendingPlaceType( void )
{
	return m_pendingPlaceType;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
const ObjectID InGameUI::getPendingPlaceSourceObjectID( void )
{

	return m_pendingPlaceSourceObjectID;

}  // end getPendingPlaceSourceObjectID

//-------------------------------------------------------------------------------------------------
/** Start the angle selection interface for selecting building angles when placing them */
//-------------------------------------------------------------------------------------------------
void InGameUI::setPlacementStart( const ICoord2D *start )
{

	// if we have a start point we turn "on" the interface, otherwise we turn it "off"
	if( start )
	{

		m_placeAnchorStart = *start;
		m_placeAnchorEnd = *start;
		m_placeAnchorInProgress = TRUE;

	}  // end if
	else
		m_placeAnchorInProgress = FALSE;

}  // end setPlacementStart

//-------------------------------------------------------------------------------------------------
/** Set the end anchor for the angle build interface */
//-------------------------------------------------------------------------------------------------
void InGameUI::setPlacementEnd( const ICoord2D *end )
{

	if( end )
		m_placeAnchorEnd = *end;

}  // end setPlacementEnd

//-------------------------------------------------------------------------------------------------
/** Is the angle selection interface for placing building at angles up? */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isPlacementAnchored( void )
{

	return m_placeAnchorInProgress;

}  // end isPlacementAnchored

//-------------------------------------------------------------------------------------------------
/** Get the start and end anchor points for the building angle selection interface */
//-------------------------------------------------------------------------------------------------
void InGameUI::getPlacementPoints( ICoord2D *start, ICoord2D *end )
{

	if( start )
		*start = m_placeAnchorStart;
	if( end )
		*end = m_placeAnchorEnd;

}  // end getPlacementPoints

//-------------------------------------------------------------------------------------------------
/** Return the angle of the drawable at the cursor if any */
//-------------------------------------------------------------------------------------------------
Real InGameUI::getPlacementAngle( void )
{

	if( m_placeIcon[ 0 ] )
		return m_placeIcon[ 0 ]->getOrientation();

	return 0.0f;

}  // end getPlacementAngle



//-------------------------------------------------------------------------------------------------
/** Set the input enabled/disabled */
//-------------------------------------------------------------------------------------------------
void InGameUI::setInputEnabled( Bool enable )
{
	if(!enable)
		setSelecting( FALSE );
	
	Bool wasEnabled = m_inputEnabled;

	m_inputEnabled = enable;
	
	if (wasEnabled && !enable)
	{
		/*
			when input is disabled, clear out all the special "modes" we can be in, since we can miss
			the "exit mode" message during the cinematic. e.g., hold down the ctrl key when a cinematic
			begins, then release it during the cinematic... since input is disabled, we never see the keyup
			and thus think we're still in forceattack when its done, until you jiggle that key again.
			(admittedly, this code will actually do the wrong thing if you were to hold down the ctrl
			key thru the whole cinematic, but that's even more unlikely...)
		*/
		setForceAttackMode( false );			// CTRL
		setForceMoveMode( false );				// apparently unmapped in current CommandMap.ini
		setWaypointMode( false );					// ALT
		setPreferSelectionMode( false );	// SHIFT
		setCameraRotateLeft( false );			// KP4
		setCameraRotateRight( false );		// KP6
		setCameraZoomIn( false );					// KP8
		setCameraZoomOut( false );				// KP2
	}
}

//-------------------------------------------------------------------------------------------------
/** Drawable is being destroyed, clean up any UI elements associated with it. */
//-------------------------------------------------------------------------------------------------
void InGameUI::disregardDrawable( Drawable *draw )
{

	// make sure drawable is no longer selected
	deselectDrawable( draw );		

}

//-------------------------------------------------------------------------------------------------
/** Create the control user interface GUI */
//-------------------------------------------------------------------------------------------------
void InGameUI::createControlBar( void )
{

	// the control bar code expects every window of this layout; stop with a reason instead of crashing later
	if( TheWindowManager->winCreateFromScript( AsciiString("ControlBar.wnd") ) == nullptr )
		ReleaseCrash( "Window\\ControlBar.wnd could not be loaded" );
	HideControlBar();
/*	
	// hide all windows created from this layout
	GameWindow *window = TheWindowManager->winGetWindowList();
	for( ; window; window = window->winGetPrev() )
		window->winHide( TRUE );
*/

}  // end createControlBar

//-------------------------------------------------------------------------------------------------
/** Create the replay control GUI */
//-------------------------------------------------------------------------------------------------
void InGameUI::createReplayControl( void )
{

	m_replayWindow = TheWindowManager->winCreateFromScript( AsciiString("ReplayControl.wnd") );

/*	
	// hide all windows created from this layout
	GameWindow *window = TheWindowManager->winGetWindowList();
	for( ; window; window = window->winGetPrev() )
		window->winHide( TRUE );
*/

}  // end createReplayControl

// ------------------------------------------------------------------------------------------------
// InGameUI::playMovie
// ------------------------------------------------------------------------------------------------
void InGameUI::playMovie( const AsciiString& movieName )
{

	stopMovie();

	m_videoStream = TheVideoPlayer->open( movieName );

	if ( m_videoStream == nullptr )
	{
		return;
	}

	m_currentlyPlayingMovie = movieName;
	m_videoBuffer.reset(TheDisplay->createVideoBuffer());

	if (	m_videoBuffer == nullptr || 
				!m_videoBuffer->allocate(	m_videoStream->width(), 
													m_videoStream->height())
		)
	{
		stopMovie();
		return;
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::stopMovie( void )
{
	m_videoBuffer.reset();
	m_videoBuffer = nullptr;

	if ( m_videoStream )
	{
		m_videoStream->close();
		m_videoStream = nullptr;
	}

	if (!m_currentlyPlayingMovie.isEmpty()) {
		//TheScriptEngine->notifyOfCompletedVideo(m_currentlyPlayingMovie); // removing sync error source -MDC
		m_currentlyPlayingMovie = AsciiString::TheEmptyString;
	}
}

// ------------------------------------------------------------------------------------------------
// InGameUI::videoBuffer
// ------------------------------------------------------------------------------------------------
VideoBuffer* InGameUI::videoBuffer( void )
{
	return m_videoBuffer.get();
}

// ------------------------------------------------------------------------------------------------
// InGameUI::playMovie
// ------------------------------------------------------------------------------------------------
void InGameUI::playCameoMovie( const AsciiString& movieName )
{

	stopCameoMovie();

	m_cameoVideoStream = TheVideoPlayer->open( movieName );

	if ( m_cameoVideoStream == nullptr )
	{
		return;
	}

	m_cameoVideoBuffer.reset(TheDisplay->createVideoBuffer());

	if (	m_cameoVideoBuffer == nullptr || 
				!m_cameoVideoBuffer->allocate(	m_cameoVideoStream->width(), 
													m_cameoVideoStream->height())
		)
	{
		stopCameoMovie();
		return;
	}
	GameWindow *window = TheWindowManager->winGetWindowFromId(nullptr,TheNameKeyGenerator->nameToKey( AsciiString("ControlBar.wnd:RightHUD") ));
	WinInstanceData *winData = window->winGetInstanceData();
	winData->setVideoBuffer( m_cameoVideoBuffer.get() );
//	window->winHide(FALSE);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::stopCameoMovie( void )
{
//RightHUD
	//GameWindow *window = TheWindowManager->winGetWindowFromId(NULL,TheNameKeyGenerator->nameToKey( AsciiString("ControlBar.wnd:CameoMovieWindow") ));
	GameWindow *window = TheWindowManager->winGetWindowFromId(nullptr,TheNameKeyGenerator->nameToKey( AsciiString("ControlBar.wnd:RightHUD") ));
//	window->winHide(FALSE);
	WinInstanceData *winData = window->winGetInstanceData();
	winData->setVideoBuffer(nullptr);
	
	m_cameoVideoBuffer.reset();
	m_cameoVideoBuffer = nullptr;

	if ( m_cameoVideoStream )
	{
		m_cameoVideoStream->close();
		m_cameoVideoStream = nullptr;
	}
	
}

// ------------------------------------------------------------------------------------------------
// InGameUI::videoBuffer
// ------------------------------------------------------------------------------------------------
VideoBuffer* InGameUI::cameoVideoBuffer( void )
{
	return m_cameoVideoBuffer.get();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void InGameUI::displayCantBuildMessage( LegalBuildCode lbc )
{

	switch( lbc )
	{

		//---------------------------------------------------------------------------------------------
		case LegalBuildCode::LBC_RESTRICTED_TERRAIN:
			TheInGameUI->message( "GUI:CantBuildRestrictedTerrain" );
			break;

		//---------------------------------------------------------------------------------------------
		case LegalBuildCode::LBC_NOT_FLAT_ENOUGH:
			TheInGameUI->message( "GUI:CantBuildNotFlatEnough" );
			break;

		//---------------------------------------------------------------------------------------------
		case LegalBuildCode::LBC_OBJECTS_IN_THE_WAY:
			TheInGameUI->message( "GUI:CantBuildObjectsInTheWay" );
			break;

		//---------------------------------------------------------------------------------------------
		case LegalBuildCode::LBC_TOO_CLOSE_TO_SUPPLIES:
			TheInGameUI->message( "GUI:CantBuildTooCloseToSupplies" );
			break;

		//---------------------------------------------------------------------------------------------
		case LegalBuildCode::LBC_NO_CLEAR_PATH:
		  TheInGameUI->message( "GUI:CantBuildNoClearPath" );
			break;

		//---------------------------------------------------------------------------------------------
		case LegalBuildCode::LBC_SHROUD:
			TheInGameUI->message( "GUI:CantBuildShroud" );
			break;

		//---------------------------------------------------------------------------------------------
		case LegalBuildCode::LBC_GENERIC_FAILURE:
		default:

			TheInGameUI->message( "GUI:CantBuildThere" );
			break;

	}  // end switch

}  // end displayCantBuildMessage

// ------------------------------------------------------------------------------------------------
// InGameUI::militarySubtitle
// ------------------------------------------------------------------------------------------------
void InGameUI::militarySubtitle( const AsciiString& label, Int duration )
{
	militarySubtitleIssued( label, duration, TheGameLogic->getFrame() );
}

void InGameUI::militarySubtitleIssued( const AsciiString& label, Int duration, UnsignedInt issuedFrame )
{
	// make sure we don't already have a subtitle up there
	removeMilitarySubtitle();

	// update our history
	UpdateDiplomacyBriefingText(label, FALSE);

	UnicodeString title = TheGameText->fetch(label);

	// make sure we actually will be displaying something
	if( title.isEmpty() || duration <= 0)
	{
		DEBUG_CRASH(("Trying to create a military subtitle but either title is empty (%ls) or duration is <= 0 (%d)",title.str(), duration));
		return;
	}

	// we need some frame info to set our timings
	UnsignedInt currLogicFrame = issuedFrame;
	const int messageTimeout = currLogicFrame + static_cast<Int>((static_cast<Real>(LOGICFRAMES_PER_SECOND) * duration)/1000.0f);

	// disable tooltips until this frame, cause we don't want to collide with the military subtitles.
	TheInGameUI->disableTooltipsUntil(messageTimeout);
	
	// calculate where this screen position should be since the position being passed in is based off 8x6
	// scale uniformly and center
	Coord2D multiplier;
	ICoord2D uiOffset;
	TheDisplay->getUILayoutTransform( 800, 600, &multiplier.x, &multiplier.y, &uiOffset.x, &uiOffset.y );
	
	// lets bring out the data structure!
	m_militarySubtitle = NEW MilitarySubtitleData;

	m_militarySubtitle->subtitle.set(title);
	m_militarySubtitle->blockDrawn = TRUE;
	m_militarySubtitle->blockBeginFrame = currLogicFrame;
	m_militarySubtitle->lifetime = messageTimeout;
	m_militarySubtitle->blockPos.x =  m_militarySubtitle->position.x = static_cast<Int>(m_militaryCaptionPosition.x * multiplier.x + uiOffset.x);
	m_militarySubtitle->blockPos.y =  m_militarySubtitle->position.y = static_cast<Int>(m_militaryCaptionPosition.y * multiplier.y + uiOffset.y);
	m_militarySubtitle->incrementOnFrame = currLogicFrame + static_cast<Int>((static_cast<Real>(LOGICFRAMES_PER_SECOND) * TheGlobalLanguageData->m_militaryCaptionDelayMS)/1000.0f);
	m_militarySubtitle->index = 0;
	for (int i = 1; i < MAX_SUBTITLE_LINES; i ++)
		m_militarySubtitle->displayStrings[i] = nullptr;

	m_militarySubtitle->currentDisplayString = 0;
	m_militarySubtitle->displayStrings[0] = TheDisplayStringManager->newDisplayString();
	m_militarySubtitle->displayStrings[0]->reset();
	m_militarySubtitle->displayStrings[0]->setFont(	TheFontLibrary->getFont( m_militaryCaptionTitleFont, 
		TheGlobalLanguageData->adjustFontSize(m_militaryCaptionTitlePointSize), m_militaryCaptionTitleBold ) );
	m_militarySubtitle->color = GameMakeColor(static_cast<UnsignedByte>(m_militaryCaptionColor.red), static_cast<UnsignedByte>(m_militaryCaptionColor.green), static_cast<UnsignedByte>(m_militaryCaptionColor.blue), static_cast<UnsignedByte>(m_militaryCaptionColor.alpha));
}

// ------------------------------------------------------------------------------------------------
// InGameUI::removeMilitarySubtitle
// ------------------------------------------------------------------------------------------------
void InGameUI::removeMilitarySubtitle( void )
{
	// sanity (is there really such a thing in this world?)
	if(!m_militarySubtitle)
		return;

	TheInGameUI->clearTooltipsDisabled();

	// loop through and free up the display strings
	for(Int i = 0; static_cast<UnsignedInt>(i) <= m_militarySubtitle->currentDisplayString; i ++)
	{
		TheDisplayStringManager->freeDisplayString(m_militarySubtitle->displayStrings[i]);
		m_militarySubtitle->displayStrings[i] = nullptr;
	}

	//delete it man!
	delete m_militarySubtitle;
	m_militarySubtitle= nullptr;

}

//------------------------------------------------------------------------------
//Resets the camera to default zoom and orientation.
//------------------------------------------------------------------------------
void InGameUI::resetCamera()
{
	ViewLocation currentView;
	TheTacticalView->getLocation( &currentView ); 
	TheTacticalView->resetCamera( &currentView.getPosition(), 1, 0.0f, 0.0f );
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------


///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
// WORLD ANIMATION DATA ///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////


void InGameUI::recreateControlBar( void )
{
	GameWindow *win = TheWindowManager->winGetWindowFromId(nullptr, TheNameKeyGenerator->nameToKey(AsciiString("ControlBar.wnd")));
	if(win)
		deleteInstanceOf(win);
	
	m_idleWorkerWin = nullptr;	
	
	createControlBar();
		
	if(TheControlBar)
	{
		delete TheControlBar;
		::Engine::context().controlBar = NEW ControlBar;
		TheControlBar->init();
	}


}

void InGameUI::disableTooltipsUntil(UnsignedInt frameNum)
{
	if (frameNum > m_tooltipsDisabledUntil) 
		m_tooltipsDisabledUntil = frameNum;
}

void InGameUI::clearTooltipsDisabled()
{
	m_tooltipsDisabledUntil = 0;
}

Bool InGameUI::areTooltipsDisabled() const
{
	return (TheGameLogic->getFrame() < m_tooltipsDisabledUntil);
}


WindowMsgHandledType IdleWorkerSystem( GameWindow *window, UnsignedInt msg, 
																				WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg ) 
	{
		//---------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{	
			// if we're givin the opportunity to take the keyboard focus we must say we don't want it
			if( mData1 == TRUE )
				*reinterpret_cast<Bool *>(mData2) = FALSE;
		}
			break;
		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
			static NameKeyType buttonSelectID = NAMEKEY( "IdleWorker.wnd:ButtonSelectNextIdleWorker" );
			if (control && control->winGetWindowId() == buttonSelectID)
			{
				TheInGameUI->selectNextIdleWorker( );
			}
			break;

		}  // end button selected

		//---------------------------------------------------------------------------------------------
		default:
			return WindowMsgHandledType::MSG_IGNORED;

	}  // end switch( msg )

	return WindowMsgHandledType::MSG_HANDLED;

}


