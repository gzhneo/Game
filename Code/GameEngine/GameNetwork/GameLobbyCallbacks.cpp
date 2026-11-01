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
// FILE: GameLobbyCallbacks.cpp
// Author: Chris Huybregts, October 2001
// Description: what the game lobby tells the menus
///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "strtok_r.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "Common/QuotedPrintable.h"
#include "Common/RandomValue.h"
#include "Common/UserSettings.h"
#include "GameClient/GameText.h"
#include "GameClient/LanguageFilter.h"
#include "GameClient/MapUtil.h"
#include "GameClient/MessageBox.h"
#include "GameClient/OptionsMenuMods.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/FileTransfer.h"
#include "GameNetwork/GameLobbyCallbacks.h"
#include "GameNetwork/networkutil.h"
#include "Engine/ModCatalog.h"

extern Bool LANbuttonPushed;

//Colors used for the chat dialogs
const Color playerColor =  GameMakeColor(255,255,255,255);
const Color gameColor =  GameMakeColor(255,255,255,255);
const Color gameInProgressColor =  GameMakeColor(128,128,128,255);
const Color gameIncompatibleColor =  GameMakeColor(200,120,120,255);
const Color chatNormalColor =  GameMakeColor(50,215,230,255);
const Color chatActionColor =  GameMakeColor(255,0,255,255);
const Color chatLocalNormalColor =  GameMakeColor(255,128,0,255);
const Color chatLocalActionColor =  GameMakeColor(128,255,255,255);
const Color chatSystemColor =  GameMakeColor(255,255,255,255);
const Color acceptTrueColor =  GameMakeColor(0,255,0,255);
const Color acceptFalseColor =  GameMakeColor(255,0,0,255);


UnicodeString GameLobbyInterface::getErrorStringFromReturnType( ReturnType ret )
{
	switch (ret)
	{
		case RET_OK:
			return TheGameText->fetch("LAN:OK");
		case RET_TIMEOUT:
			return TheGameText->fetch("LAN:ErrorTimeout");
		case RET_GAME_FULL:
			return TheGameText->fetch("LAN:ErrorGameFull");
		case RET_DUPLICATE_NAME:
			return TheGameText->fetch("LAN:ErrorDuplicateName");
		case RET_CRC_MISMATCH:
			return TheGameText->fetch("LAN:ErrorCRCMismatch");
		case RET_GAME_STARTED:
			return TheGameText->fetch("LAN:ErrorGameStarted");
		case RET_GAME_EXISTS:
			return TheGameText->fetch("LAN:ErrorGameExists");
		case RET_GAME_GONE:
			return TheGameText->fetch("LAN:ErrorGameGone");
		case RET_BUSY:
			return TheGameText->fetch("LAN:ErrorBusy");
		case RET_SERIAL_DUPE:
			return TheGameText->fetch("WOL:ChatErrorSerialDup");
		case RET_VERSION_MISMATCH:
			return TheGameText->fetch("GZH:LanErrorVersion");
		case RET_DATA_MISMATCH:
			return TheGameText->fetch("GZH:LanErrorData");
		case RET_HOST_KEY_MISMATCH:
			return TheGameText->fetch("GZH:LanErrorHostKey");
		case RET_NOT_FOUND:
			return TheGameText->fetch("GZH:LanErrorNotFound");
		default:
			return TheGameText->fetch("LAN:ErrorUnknown");
	}
}

static const AsciiString &localModTag()
{
	return TheArchiveFileSystem ? TheArchiveFileSystem->getModTag() : AsciiString::TheEmptyString;
}

// On functions are (generally) the result of network traffic

void GameLobby::OnAccept( UnsignedInt playerIP, Bool status ) 
{ 
	if( AmIHost() )
	{
		Int i;
		for (i = 0; i < MAX_SLOTS; i++)
		{
			if (m_currentGame->getIP(i) == playerIP)
			{
				if(status)
					m_currentGame->getLANSlot(i)->setAccept();
				else
					m_currentGame->getLANSlot(i)->unAccept();
				break;
			}// if
		}// for
		if (i != MAX_SLOTS ) 
		{
			RequestGameOptions( GenerateGameOptionsString(), false );
			lanUpdateSlotList();
		}
	}//if
	else 
	{
		//i'm not the host but if the accept came from the host...
		if( m_currentGame->getIP(0) == playerIP )
		{
			UnicodeString text;
			text = TheGameText->fetch("GUI:HostWantsToStart");
			OnChat(UnicodeString(L"SYSTEM"), m_localIP, text, LANCHAT_SYSTEM);				
		}
	}
}// void GameLobby::OnAccept( UnicodeString player, Bool status ) 

void GameLobby::OnHasMap( UnsignedInt playerIP, Bool status ) 
{ 
	if( AmIHost() )
	{
		Int i;
		for (i = 0; i < MAX_SLOTS; i++)
		{
			if (m_currentGame->getIP(i) == playerIP)
			{
				m_currentGame->getLANSlot(i)->setMapAvailability( status );
				break;
			}// if
		}// for
		if (i != MAX_SLOTS ) 
		{
			UnicodeString mapDisplayName;
			const MapMetaData *mapData = TheMapCache->findMap( m_currentGame->getMap() );
			Bool willTransfer = TRUE;
			if (mapData)
			{
				mapDisplayName.format(L"%ls", mapData->m_displayName.str());
				if (mapData->m_isOfficial)
					willTransfer = FALSE;
			}
			else
			{
				mapDisplayName.format(L"%hs", m_currentGame->getMap().str());
				willTransfer = WouldMapTransfer(m_currentGame->getMap());
			}
			if (!status)
			{
				UnicodeString text;
				if (willTransfer)
					text.format(TheGameText->fetch("GUI:PlayerNoMapWillTransfer"), m_currentGame->getLANSlot(i)->getName().str(), mapDisplayName.str());
				else
					text.format(TheGameText->fetch("GUI:PlayerNoMap"), m_currentGame->getLANSlot(i)->getName().str(), mapDisplayName.str());
				OnChat(UnicodeString(L"SYSTEM"), m_localIP, text, LANCHAT_SYSTEM);
			}
			lanUpdateSlotList();
		}
	}//if
}// void GameLobby::OnHasMap( UnicodeString player, Bool status ) 

void GameLobby::OnGameStartTimer( Int seconds )
{
	UnicodeString text;
	if (seconds == 1)
		text.format(TheGameText->fetch("LAN:GameStartTimerSingular"), seconds);
	else
		text.format(TheGameText->fetch("LAN:GameStartTimerPlural"), seconds);
	OnChat(UnicodeString(L"SYSTEM"), m_localIP, text, LANCHAT_SYSTEM);
}

void GameLobby::OnGameStart( void )
{
	//DEBUG_LOG(("Map is '%s', preview is '%s'\n", m_currentGame->getMap().str(), GetPreviewFromMap(m_currentGame->getMap()).str()));
	//DEBUG_LOG(("Map is '%s', INI is '%s'\n", m_currentGame->getMap().str(), GetINIFromMap(m_currentGame->getMap()).str()));

	if (m_currentGame)
	{
		LobbySettings pref(LobbySettings::Kind::Lan);
		pref.setPreferredFaction(m_currentGame->getLANSlot( m_currentGame->getLocalSlotNum() )->getPlayerTemplate());
		pref.setPreferredColor(m_currentGame->getLANSlot( m_currentGame->getLocalSlotNum() )->getColor());
		if (m_currentGame->amIHost())
    {
    	pref.setPreferredMap(m_currentGame->getMap());
      pref.setSuperweaponRestricted( m_currentGame->getSuperweaponRestriction() > 0 );
      pref.setStartingCash( m_currentGame->getStartingCash() );
      pref.setGraceMinutes( m_currentGame->getGraceMinutes() );
      pref.setMaxZoomPercent( m_currentGame->getMaxZoomPercent() );
    }
		pref.write();

		m_isInLANMenu = FALSE;

		//m_currentGame->startGame(0);

		// Set up the game network
		DEBUG_ASSERTCRASH(TheNetwork == nullptr, ("For some reason TheNetwork isn't NULL at the start of this game.  Better look into that."));

		if (TheNetwork != nullptr) {
			delete TheNetwork;
			::Engine::context().network = nullptr;
		}

		// Time to initialize TheNetwork for this game. The game socket was opened in the lobby: its
		// ways to the other players (punched, relayed) carry over.
		::Engine::context().network = NetworkInterface::createNetwork();
		TheNetwork->init();
		Transport *gameTransport = takeGameTransport();
		if (gameTransport != nullptr)
		{
			// the slot list names us by our identity, not by an address
			TheNetwork->setLocalAddress(m_localIP, m_gamePort);
			TheNetwork->attachTransport(gameTransport);
		}
		else
		{
			TheNetwork->setLocalAddress(m_bindIP, m_gamePort);
			TheNetwork->initTransport();
		}

		applyMemberKeys();
		if (!m_currentGame->deriveSealKeys(m_keyPair))
			DEBUG_LOG(("GameLobby::OnGameStart - keys missing for some players; their game packets will be rejected\n"));
		TheNetwork->parseUserList(m_currentGame);
		TheNetwork->setReturnWindow(static_cast<UnsignedInt>(atoi(UserSettings::get("Network.ReturnWindow").str())));
		keepReturnTicket();

		if (TheGameLogic->isInGame())
			TheGameLogic->clearGameData();

		Bool filesOk = DoAnyMapTransfers(m_currentGame);

		// see if we really have the map.  if not, back out.
		TheMapCache->updateCache();
		if (!filesOk || TheMapCache->findMap(m_currentGame->getMap()) == nullptr)
		{
			DEBUG_LOG(("After transfer, we didn't really have the map.  Bailing...\n"));
			OnPlayerLeave(m_name);
			LANGameInfo *leftGame = m_currentGame;
			m_currentGame = nullptr;
			if (TheGameInfo == leftGame)	// DoAnyMapTransfers made it the current game
				::Engine::context().gameInfo = nullptr;
			m_directory.eraseGame(leftGame);
			m_inLobby = TRUE;
			if (TheNetwork != nullptr) {
				delete TheNetwork;
				::Engine::context().network = nullptr;
			}
			OnChat(UnicodeString::TheEmptyString, 0, TheGameText->fetch("GUI:CouldNotTransferMap"), LANCHAT_SYSTEM);
			return;
		}

		m_currentGame->startGame(0);

		// shutdown the top, but do not pop it off the stack
		//TheShell->hideShell();
		// setup the Global Data with the Map and Seed
		TheWritableGlobalData->m_pendingFile = m_currentGame->getMap();

		// send a message to the logic for a new game
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
		msg->appendIntegerArgument(GAME_LAN);

		TheWritableGlobalData->m_useFpsLimit = false;

		// Set the random seed
		InitGameLogicRandom( m_currentGame->getSeed() );
		DEBUG_LOG(("InitGameLogicRandom( %d )\n", m_currentGame->getSeed()));
	}
}

void GameLobby::OnGameOptions( UnsignedInt playerIP, Int playerSlot, AsciiString options )
{
	if (!m_currentGame)
		return;

	if (m_currentGame->getIP(playerSlot) != playerIP)
		return; // He's not in our game?!?
	
	
	if (m_currentGame->isGameInProgress())
		return; // we don't want to process any game options while in game.

	if (playerSlot == 0 && !m_currentGame->amIHost())
	{
		m_currentGame->setLastHeard(timeGetTime());
		// save these off for if we get booted; a game joined by address has no map before the first state
		const AsciiString currentMap = m_currentGame->getMap();
		const AsciiString oldOptions = (currentMap.isEmpty() || currentMap == "NOMAP") ? AsciiString::TheEmptyString : GameInfoToAsciiString(m_currentGame);
		if(ParseGameOptionsString(m_currentGame,options))
		{
			lanUpdateSlotList();
			updateGameOptions();
		}
		Bool booted = true;
		for(Int player = 1; player< MAX_SLOTS; player++)
		{
			if(m_currentGame->getIP(player) == m_localIP)
			{
				booted = false;
				break;
			}				
		}
		if(booted)
		{
			// restore the options with us in so we can save prefs
			if (!oldOptions.isEmpty())
				ParseGameOptionsString(m_currentGame, oldOptions);
			OnPlayerLeave(m_name);
		}

	}
	else
	{
		// Check for user/host updates
		{
			AsciiString key;
			AsciiString munkee = options;
			munkee.nextToken(&key, "=");
			//DEBUG_LOG(("GameOpt request: key=%s, val=%s from player %d\n", key.str(), munkee.str(), playerSlot));

			LANGameSlot *slot = m_currentGame->getLANSlot(playerSlot);
			if (!slot)
				return;

			if (key == "User")
			{
				slot->setLogin(munkee.str()+1);
				return;
			}
			else if (key == "Host")
			{
				slot->setHost(munkee.str()+1);
				return;
			}
		}

		// Parse player requests (side, color, etc)
		if( AmIHost() && m_localIP != playerIP)
		{
			if (options.compare("HELLO") == 0)
			{
				m_currentGame->setPlayerLastHeard(playerSlot, timeGetTime());
			}
			else
			{
				m_currentGame->setPlayerLastHeard(playerSlot, timeGetTime());
				Bool change = false;
				Bool shouldUnaccept = false;
				AsciiString key;
				options.nextToken(&key, "=");
				Int val = atoi(options.str()+1);
				DEBUG_LOG(("GameOpt request: key=%s, val=%s from player %d\n", key.str(), options.str(), playerSlot));

				LANGameSlot *slot = m_currentGame->getLANSlot(playerSlot);
				if (!slot)
					return;

				if (key == "Color")
				{
					if (val >= -1 && val < TheMultiplayerSettings->getNumColors() && val != slot->getColor() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
					{
						Bool colorAvailable = TRUE;
						if(val != -1 )
						{
							for(Int i=0; i <MAX_SLOTS; i++)
							{
								LANGameSlot *checkSlot = m_currentGame->getLANSlot(i);
								if(val == checkSlot->getColor() && slot != checkSlot)
								{
									colorAvailable = FALSE;
									break;
								}
							}
						}
						if(colorAvailable)
							slot->setColor(val);
						change = true;
					}
					else
					{
						DEBUG_LOG(("Rejecting invalid color %d\n", val));
					}
				}
				else if (key == "PlayerTemplate")
				{
					if (val >= PLAYERTEMPLATE_MIN && val < ThePlayerTemplateStore->getPlayerTemplateCount() && val != slot->getPlayerTemplate())
					{
						slot->setPlayerTemplate(val);
						if (val == PLAYERTEMPLATE_OBSERVER)
						{
							slot->setColor(-1);
							slot->setStartPos(-1);
							slot->setTeamNumber(-1);
						}
						change = true;
						shouldUnaccept = true;
					}
					else
					{
						DEBUG_LOG(("Rejecting invalid PlayerTemplate %d\n", val));
					}
				}
				else if (key == "StartPos" && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
				{
						
					if (val >= -1 && val < MAX_SLOTS && val != slot->getStartPos())
					{
						Bool startPosAvailable = TRUE;
						if(val != -1)
							for(Int i=0; i <MAX_SLOTS; i++)
							{
								LANGameSlot *checkSlot = m_currentGame->getLANSlot(i);
								if(val == checkSlot->getStartPos() && slot != checkSlot)
								{
									startPosAvailable = FALSE;
									break;
								}
							}
						if(startPosAvailable)
							slot->setStartPos(val);
						change = true;
						shouldUnaccept = true;
					}
					else
					{
						DEBUG_LOG(("Rejecting invalid startPos %d\n", val));
					}
				}
				else if (key == "Team")
				{
					if (val >= -1 && val < MAX_SLOTS/2 && val != slot->getTeamNumber() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
					{
						slot->setTeamNumber(val);
						change = true;
						shouldUnaccept = true;
					}
					else
					{
						DEBUG_LOG(("Rejecting invalid team %d\n", val));
					}
				}
				else if (key == "NAT")
				{
					if ((val >= FirewallHelperClass::FIREWALL_TYPE_SIMPLE) &&
							(val <= FirewallHelperClass::FIREWALL_TYPE_DESTINATION_PORT_DELTA))
					{
						slot->setNATBehavior(static_cast<FirewallHelperClass::FirewallBehaviorType>(val));
						DEBUG_LOG(("NAT behavior set to %d for player %d\n", val, playerSlot));
						change = true;
					}
					else
					{
						DEBUG_LOG(("Rejecting invalid NAT behavior %d\n", (Int)val));
					}
				}

				if (change)
				{
					if (shouldUnaccept)
						m_currentGame->resetAccepted();
					RequestGameOptions(GenerateGameOptionsString(), true);
					lanUpdateSlotList();
					DEBUG_LOG(("Slot value is color=%d, PlayerTemplate=%d, startPos=%d, team=%d\n",
						slot->getColor(), slot->getPlayerTemplate(), slot->getStartPos(), slot->getTeamNumber()));
					DEBUG_LOG(("Slot list updated to %s\n", GenerateGameOptionsString().str()));
				}
			}
		}
	}
}


/*
void GameLobby::OnSlotList( ReturnType ret, LANGameInfo *theGame )
{
	if (!theGame || theGame != m_currentGame)
		return;

	Bool foundMe = false;
	for (int player = 0; player < MAX_SLOTS; ++player)
	{
		if (m_currentGame->getIP(player) == m_localIP)
		{
			foundMe = true;
			break;
		}
	}
	if (!foundMe)
	{
		// I've been kicked - back to the lobby for me!
		// We're counting on the fact that OnPlayerLeave winds up calling reset on TheLobby.
		OnPlayerLeave(m_name);
		return;
	}

	lanUpdateSlotList();
}
*/
void GameLobby::OnPlayerJoin( Int slot, UnicodeString playerName )
{
	if (m_currentGame && m_currentGame->getIP(0) == m_localIP)
	{
		// Someone New Joined.. lets reset the accepts
		m_currentGame->resetAccepted();

		// Send out the game options
		RequestGameOptions(GenerateGameOptionsString(), true);
	}

	lanUpdateSlotList();
}

void GameLobby::OnGameJoin( ReturnType ret, LANGameInfo *theGame )
{
	if (ret == RET_OK)
	{
		LANbuttonPushed = true;
		TheShell->push( AsciiString("Menus/LanGameOptionsMenu.wnd") );
		//lanUpdateSlotList();

		LobbySettings pref(LobbySettings::Kind::Lan);
		AsciiString options;
		options.format("PlayerTemplate=%d", pref.getPreferredFaction());
		RequestGameOptions(options, true);
		options.format("Color=%d", pref.getPreferredColor());
		RequestGameOptions(options, true);
		options.format("User=%s", m_userName.str());
		RequestGameOptions( options, true );
		options.format("Host=%s", m_hostName.str());
		RequestGameOptions( options, true );
		options.format("NAT=%d", FirewallHelperClass::FIREWALL_TYPE_SIMPLE); // BGC: This is a LAN game, so there is no firewall.
		RequestGameOptions( options, true );
	}
	else if (ret != RET_BUSY)
	{
		/// @todo: re-enable lobby controls?  Error msgs?
		UnicodeString title, body;
		title = TheGameText->fetch("LAN:JoinFailed");
		body = getErrorStringFromReturnType(ret);
		if ((ret == RET_VERSION_MISMATCH || ret == RET_TIMEOUT) && !m_joinDenyDetail.isEmpty())
		{
			body.concat(L"\n\n");
			body.concat(m_joinDenyDetail);
		}
		else if (ret == RET_DATA_MISMATCH && theGame && !::Engine::sameModTags( theGame->getModTag().str(), localModTag().str() ))
		{
			UnicodeString detail;
			detail.formatUnchecked( TheGameText->fetch("GZH:LanModsDetail").str(), OptionsMenuMods::listText( theGame->getModTag() ).str(),
				OptionsMenuMods::listText( localModTag() ).str() );
			body = TheGameText->fetch("GZH:LanErrorMods");
			body.concat(L"\n\n");
			body.concat(detail);
		}
		MessageBoxFitText(MessageBoxOk(title, body, nullptr));
	}
}

void GameLobby::OnHostLeave( void )
{
	DEBUG_ASSERTCRASH(!m_inLobby && m_currentGame, ("Game info is gone!"));
	if (m_inLobby || !m_currentGame)
		return;
	LANbuttonPushed = true;
	DEBUG_LOG(("Host left - popping to lobby\n"));
	TheShell->pop();
}

void GameLobby::OnPlayerLeave( UnicodeString player )
{
	DEBUG_ASSERTCRASH(!m_inLobby && m_currentGame, ("Game info is gone!"));
	if (m_inLobby || !m_currentGame || m_currentGame->isGameInProgress())
		return;

	if (m_name.compare(player) == 0)
	{
		// We're leaving.  Save options and Pop the shell up a screen.
		//DEBUG_ASSERTCRASH(false, ("Slot is %d\n", m_currentGame->getLocalSlotNum()));
		if (m_currentGame && m_currentGame->isInGame() && m_currentGame->getLocalSlotNum() >= 0)
		{
			LobbySettings pref(LobbySettings::Kind::Lan);
			pref.setPreferredFaction(m_currentGame->getLANSlot( m_currentGame->getLocalSlotNum() )->getPlayerTemplate());
			pref.setPreferredColor(m_currentGame->getLANSlot( m_currentGame->getLocalSlotNum() )->getColor());
			if (m_currentGame->amIHost())
				pref.setPreferredMap(m_currentGame->getMap());
			pref.write();
		}
		LANbuttonPushed = true;
		DEBUG_LOG(("OnPlayerLeave says we're leaving!  pop away!\n"));
		TheShell->pop();
	}
	else
	{
		if (m_currentGame && m_currentGame->getIP(0) == m_localIP)
		{
			// Force a new slotlist send
			m_lastResendTime = 0;
			
			lanUpdateSlotList();
			RequestGameOptions( GenerateGameOptionsString(), true );

		}
	}
}

void GameLobby::OnGameList( LANGameInfo *gameList )
{
		
	if (m_inLobby)
	{
		LANDisplayGameList(listboxGames, gameList);
	}
}//void GameLobby::OnGameList( LANGameInfo *gameList ) 

void GameLobby::OnGameCreate( ReturnType ret )
{
	if (ret == RET_OK)
	{

		LANbuttonPushed = true;
		TheShell->push( AsciiString("Menus/LanGameOptionsMenu.wnd") );

		RequestLobbyLeave( false );
		//RequestGameAnnounce( ); // can't do this here, since we don't have a map set
	}
	else
	{
		if(m_inLobby)
		{
			switch( ret )
			{
			case RET_GAME_EXISTS:
				GadgetListBoxAddEntryText(listboxChatWindow, TheGameText->fetch("LAN:ErrorGameExists"), chatSystemColor, -1, -1);
				break;
			case RET_BUSY:
				GadgetListBoxAddEntryText(listboxChatWindow, TheGameText->fetch("LAN:ErrorBusy"), chatSystemColor, -1, -1);
				break;
			default:
				GadgetListBoxAddEntryText(listboxChatWindow, TheGameText->fetch("LAN:ErrorUnknown"), chatSystemColor, -1, -1);
			}
		}
	}

}//void OnGameCreate( ReturnType ret )

void GameLobby::OnPlayerList( LANPlayer *playerList )
{
	if (m_inLobby)
	{
		
		UnsignedInt selectedIP = 0;
		Int selectedIndex = -1;
		Int indexToSelect = -1;
		GadgetListBoxGetSelected(listboxPlayers, &selectedIndex);
		
		if (selectedIndex != -1 )
			selectedIP = static_cast<UnsignedInt>(reinterpret_cast<uintptr_t>(GadgetListBoxGetItemData(listboxPlayers, selectedIndex, 0)));

		GadgetListBoxReset(listboxPlayers);

		LANPlayer *player = m_directory.players();
		while (player)
		{
			Int addedIndex = GadgetListBoxAddEntryText(listboxPlayers, player->getName(), playerColor, -1, -1);
			GadgetListBoxSetItemData(listboxPlayers, reinterpret_cast<void *>(static_cast<uintptr_t>(player->getIP())),addedIndex, 0 );

			if (selectedIP == player->getIP())
				indexToSelect = addedIndex;

			player = player->getNext();
		}

		if (indexToSelect >= 0)
			GadgetListBoxSetSelected(listboxPlayers, indexToSelect);
	}
}

void GameLobby::OnNameChange( UnsignedInt IP, UnicodeString newName )
{
	OnPlayerList(m_directory.players());
}

void GameLobby::OnInActive(UnsignedInt IP) {
	
}

void GameLobby::OnChat( UnicodeString player, UnsignedInt ip, UnicodeString message, ChatType format )
{
	GameWindow *chatWindow = nullptr;

	if (m_inLobby)
	{
		chatWindow = listboxChatWindow;
	}
	else if( m_currentGame && m_currentGame->isGameInProgress() && TheShell->isShellActive())
	{
		chatWindow = listboxChatWindowScoreScreen;
	}
	else if( m_currentGame && !m_currentGame->isGameInProgress())
	{
		chatWindow = listboxChatWindowLanGame;	
	}
	if (chatWindow == nullptr)
		return;
	Int index = -1;
	UnicodeString unicodeChat;
	switch (format)
	{
		case GameLobbyInterface::LANCHAT_SYSTEM:
			unicodeChat = L"";
			unicodeChat.concat(message);
			unicodeChat.concat(L"");
			index =GadgetListBoxAddEntryText(chatWindow, unicodeChat, chatSystemColor, -1, -1);
			break;
		case GameLobbyInterface::LANCHAT_EMOTE:
			unicodeChat = player;
			unicodeChat.concat(L' ');
			unicodeChat.concat(message);
			if (ip == m_localIP)
				index =GadgetListBoxAddEntryText(chatWindow, unicodeChat, chatLocalActionColor, -1, -1);
			else
				index =GadgetListBoxAddEntryText(chatWindow, unicodeChat, chatActionColor, -1, -1);
			break;
		case GameLobbyInterface::LANCHAT_NORMAL:
		default:
		{
			// Do the language filtering.
			TheLanguageFilter->filterLine(message);

			Color chatColor = GameMakeColor(255, 255, 255, 255);
			if (m_currentGame)
			{
				Int slotNum = m_currentGame->getSlotNum(player);
				// it'll be -1 if its invalid.
				if (slotNum >= 0) {
					GameSlot *gs = m_currentGame->getSlot(slotNum);
					if (gs) {
						Int colorIndex = gs->getColor();
						MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor(colorIndex);
						if (def)
							chatColor = def->getColor();
					}
				}
			}
			
			unicodeChat = L"[";
			unicodeChat.concat(player);
			unicodeChat.concat(L"] ");
			unicodeChat.concat(message);
			if (ip == m_localIP)
				index =GadgetListBoxAddEntryText(chatWindow, unicodeChat, chatColor, -1, -1);
			else
				index =GadgetListBoxAddEntryText(chatWindow, unicodeChat, chatColor, -1, -1);
			break;
		}
	}
	GadgetListBoxSetItemData(chatWindow, reinterpret_cast<void *>(-1), index);
}
