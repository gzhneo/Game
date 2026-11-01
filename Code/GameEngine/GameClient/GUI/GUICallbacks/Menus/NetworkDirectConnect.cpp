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
// FILE: NetworkDirectConnect.cpp
// Author: Bryan Cleveland, November 2001
// Description: Lan Lobby Menu
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/QuotedPrintable.h"
#include "Common/UserSettings.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/Shell.h"
#include "GameClient/GameWindowTransitions.h"

#include "GameNetwork/IPEnumeration.h"
#include "GameNetwork/GameLobby.h"
#include "GameNetwork/GameLobbyCallbacks.h"
#include "GameNetwork/networkutil.h"
#include "GameClient/MessageBox.h"
#include "Engine/InviteCode.h"
#include "Engine/NetAddress.h"
#include "Engine/NetAddressBook.h"
#include "Engine/PacketSeal.h"
#include "Lib/SafeString.h"

#include <string_view>

// window ids ------------------------------------------------------------------------------

// Window Pointers ------------------------------------------------------------------------

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////

extern Bool LANbuttonPushed;
extern Bool LANisShuttingDown;

static Bool isShuttingDown = false;
static Bool buttonPushed = false;

static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType buttonHostID = NAMEKEY_INVALID;
static NameKeyType buttonJoinID = NAMEKEY_INVALID;
static NameKeyType editPlayerNameID = NAMEKEY_INVALID;
static NameKeyType comboboxRemoteIPID = NAMEKEY_INVALID;
static NameKeyType staticLocalIPID = NAMEKEY_INVALID;

static GameWindow *buttonBack = nullptr;
static GameWindow *buttonHost = nullptr;
static GameWindow *buttonJoin = nullptr;
static GameWindow *editPlayerName = nullptr;
static GameWindow *comboboxRemoteIP = nullptr;
static GameWindow *staticLocalIP = nullptr;

void PopulateRemoteIPComboBox()
{
	LobbySettings userprefs(LobbySettings::Kind::Lan);
	GadgetComboBoxReset(comboboxRemoteIP);

	Int numRemoteIPs = userprefs.getNumRemoteIPs();
	Color white = GameMakeColor(255,255,255,255);

	for (Int i = 0; i < numRemoteIPs; ++i)
	{
		UnicodeString entry;
		entry = userprefs.getRemoteIPEntry(i);
		GadgetComboBoxAddEntry(comboboxRemoteIP, entry, white);
	}

	if (numRemoteIPs > 0)
	{
		GadgetComboBoxSetSelectedPos(comboboxRemoteIP, 0, TRUE);
	}
}

// The address part of a list entry "address(name)".
static AsciiString remoteAddressPart( const UnicodeString &entry )
{
	AsciiString ascii;
	ascii.translate(entry);
	AsciiString address;
	ascii.nextToken(&address, "(");
	address.trim();
	return address;
}

// "address#K7Q2-9XPA": the game key the host must present, as it shows it in its chat.
static AsciiString gameKeyPart( const AsciiString &entry )
{
	const char *hash = strchr(entry.str(), '#');
	AsciiString code = hash ? AsciiString(hash + 1) : AsciiString::TheEmptyString;
	code.trim();
	return code;
}

// An address or a host name without port: the lobby uses its fixed port. A game key after '#'
// must have its form.
static Engine::NetTarget parseRemoteAddress( const AsciiString &entry )
{
	AsciiString address = entry;
	const char *hash = strchr(entry.str(), '#');
	if (hash)
	{
		address.clear();
		for (const char *p = entry.str(); p < hash; ++p)
			address.concat(*p);
		address.trim();
	}
	Engine::NetTarget target = Engine::parseNetTarget(std::string_view(address.str(), static_cast<size_t>(address.getLength())));
	if (hash && !::Engine::isSealFingerprintText(gameKeyPart(entry).str()))
		target.kind = Engine::NetTarget::Kind::Invalid;
	if (target.port != 0)
		target.kind = Engine::NetTarget::Kind::Invalid;
	return target;
}

void UpdateRemoteIPList()
{
	Int numEntries = GadgetComboBoxGetLength(comboboxRemoteIP);
	Int currentSelection = -1;
	GadgetComboBoxGetSelectedPos(comboboxRemoteIP, &currentSelection);
	UnicodeString unisel = GadgetComboBoxGetText(comboboxRemoteIP);
	AsciiString sel;
	sel.translate(unisel);

	const AsciiString newAddress = remoteAddressPart(unisel);
	const Engine::NetTarget target = parseRemoteAddress(newAddress);
	if (target.kind == Engine::NetTarget::Kind::Invalid) {
		// neither an address nor a host name, don't change a thing.
		return;
	}
	// The list separates address and name at a colon: an IPv6 address is kept in brackets.
	if (target.kind == Engine::NetTarget::Kind::Address && target.address.isV6() && sel.getCharAt(0) != '[')
	{
		const char *rest = sel.str();
		while (*rest != 0 && *rest != '#' && *rest != '(')
			++rest;
		AsciiString bracketed;
		bracketed.format("[%s]%s", target.address.toString(false).c_str(), rest);
		sel = bracketed;
	}

	std::vector<AsciiString> entries;
	entries.push_back(sel);

	for (Int i = 0; i < numEntries; ++i)
	{
		if (i != currentSelection)
		{
			GadgetComboBoxSetSelectedPos(comboboxRemoteIP, i, FALSE);
			UnicodeString uni;
			uni = GadgetComboBoxGetText(comboboxRemoteIP);
			AsciiString ascii;
			ascii.translate(uni);

			// prevent more than one copy of an address from being put in the list.
			if (currentSelection == -1 && remoteAddressPart(uni).compareNoCase(newAddress) == 0)
			{
				--numEntries;
				continue;
			}
			entries.push_back(ascii);
		}
	}

	LobbySettings prefs(LobbySettings::Kind::Lan);
	prefs.setRemoteIPs(entries);
	prefs.write();
}

void HostDirectConnectGame()
{
	// Init LAN API Singleton
	DEBUG_ASSERTCRASH(TheLobby != nullptr, ("TheLobby is NULL!"));
	if (!TheLobby)
	{
		::Engine::context().lobby = NEW GameLobby();
	}

	UnsignedInt localIP = TheLobby->GetLocalIP();
	UnicodeString localIPString;
	localIPString.format(L"%d.%d.%d.%d", localIP >> 24, (localIP & 0xff0000) >> 16, (localIP & 0xff00) >> 8, localIP & 0xff);

	UnicodeString name;
	name = GadgetTextEntryGetText(editPlayerName);

	LobbySettings prefs(LobbySettings::Kind::Lan);
	prefs.setUserName(name);
	prefs.write();

	while (name.getLength() > g_lanPlayerNameLength)
		name.removeLastChar();
	TheLobby->RequestSetName(name);
	TheLobby->RequestGameCreate(localIPString, TRUE);
}

void JoinDirectConnectGame()
{
	// Init LAN API Singleton

	if (!TheLobby)
	{
		::Engine::context().lobby = NEW GameLobby();
	}

	UnsignedInt ipaddress = 0;
	const AsciiString address = remoteAddressPart(GadgetComboBoxGetText(comboboxRemoteIP));

	// an invitation code ("K7Q2-DP941QG2@server") leads to the host over a rendezvous server
	Engine::InviteCode invite;
	if (Engine::parseInviteCode(address.str(), invite))
	{
		UnicodeString name;
		name = GadgetTextEntryGetText(editPlayerName);
		LobbySettings prefs(LobbySettings::Kind::Lan);
		prefs.setUserName(name);
		prefs.write();
		while (name.getLength() > g_lanPlayerNameLength)
			name.removeLastChar();
		TheLobby->RequestSetName(name);
		TheLobby->RequestGameJoinInvite(address);
		return;
	}

	// the game key alone ("K7Q2-9XPA", as the host's chat shows it) names no computer: without this
	// it would be looked up as a host name
	if (address.getLength() == 9 && address.getCharAt(4) == '-' && ::Engine::isSealFingerprintText(address.str()))
	{
		MessageBoxOk(TheGameText->fetch("LAN:JoinFailed"), TheGameText->fetch("GZH:DirectConnectKeyOnly"), nullptr);
		return;
	}

	const Engine::NetTarget target = parseRemoteAddress(address);
	const char *problem = nullptr;
	if (target.kind == Engine::NetTarget::Kind::Invalid)
		problem = "GZH:DirectConnectBadAddress";
	else if (target.kind == Engine::NetTarget::Kind::Address && target.address.isV6() && !TheLobby->hasIPv6())
		problem = "GZH:DirectConnectNoIPv6";
	else if (target.kind == Engine::NetTarget::Kind::Address)
		ipaddress = TheNetAddressBook().registerAddress(target.address);	// IPv4 itself, IPv6 a handle
	else if (!ResolveHostIPv4(target.hostName.c_str(), ipaddress)
		&& !(TheLobby->hasIPv6() && ResolveHostIPv6(target.hostName.c_str(), ipaddress)))
		problem = "GZH:DirectConnectNotFound";
	if (problem == nullptr && ipaddress == 0)
		problem = "GZH:DirectConnectBadAddress";
	if (problem != nullptr && strchr(address.str(), '#') && !::Engine::isSealFingerprintText(gameKeyPart(address).str()))
		problem = "GZH:DirectConnectBadCode";
	if (problem != nullptr)
	{
		MessageBoxOk(TheGameText->fetch("LAN:JoinFailed"), TheGameText->fetch(problem), nullptr);
		return;
	}

	DEBUG_LOG(("JoinDirectConnectGame - joining %s at %d.%d.%d.%d\n", address.str(),
		ipaddress >> 24, (ipaddress >> 16) & 0xff, (ipaddress >> 8) & 0xff, ipaddress & 0xff));

	UnicodeString name;
	name = GadgetTextEntryGetText(editPlayerName);

	LobbySettings prefs(LobbySettings::Kind::Lan);
	prefs.setUserName(name);
	prefs.write();

	UpdateRemoteIPList();
	PopulateRemoteIPComboBox();

	while (name.getLength() > g_lanPlayerNameLength)
		name.removeLastChar();
	TheLobby->RequestSetName(name);

	TheLobby->setExpectedHostCode(gameKeyPart(address));
	TheLobby->RequestGameJoinDirectConnect(ipaddress);
}

//-------------------------------------------------------------------------------------------------
/** Initialize the Network Direct Connect Menu */
//-------------------------------------------------------------------------------------------------
void NetworkDirectConnectInit( WindowLayout *layout, void *userData )
{
	LANbuttonPushed = false;
	LANisShuttingDown = false;

	if (TheLobby == nullptr)
	{
		::Engine::context().lobby = NEW GameLobby();
		TheLobby->init();
	}
	TheLobby->reset();

	buttonPushed = false;
	isShuttingDown = false;
	TheShell->showShellMap(TRUE);
	buttonBackID = TheNameKeyGenerator->nameToKey( AsciiString( "NetworkDirectConnect.wnd:ButtonBack" ) );
	buttonHostID = TheNameKeyGenerator->nameToKey( AsciiString( "NetworkDirectConnect.wnd:ButtonHost" ) );
	buttonJoinID = TheNameKeyGenerator->nameToKey( AsciiString( "NetworkDirectConnect.wnd:ButtonJoin" ) );
	editPlayerNameID = TheNameKeyGenerator->nameToKey( AsciiString( "NetworkDirectConnect.wnd:EditPlayerName" ) );
	comboboxRemoteIPID = TheNameKeyGenerator->nameToKey( AsciiString( "NetworkDirectConnect.wnd:ComboboxRemoteIP" ) );
	staticLocalIPID = TheNameKeyGenerator->nameToKey( AsciiString( "NetworkDirectConnect.wnd:StaticLocalIP" ) );

	buttonBack = TheWindowManager->winGetWindowFromId( nullptr,  buttonBackID);
	buttonHost = TheWindowManager->winGetWindowFromId( nullptr,	buttonHostID);
	buttonJoin = TheWindowManager->winGetWindowFromId( nullptr,	buttonJoinID);
	editPlayerName = TheWindowManager->winGetWindowFromId( nullptr,	editPlayerNameID);
	comboboxRemoteIP = TheWindowManager->winGetWindowFromId( nullptr,	comboboxRemoteIPID);
	// The layout sizes the field for an IPv4 address; host names need more room and letters.
	if (comboboxRemoteIP)
	{
		GadgetComboBoxSetMaxChars(comboboxRemoteIP, 64);
		ComboBoxData *comboData = static_cast<ComboBoxData *>(comboboxRemoteIP->winGetUserData());
		if (comboData && comboData->entryData)
		{
			comboData->entryData->numericalOnly = FALSE;
			comboData->entryData->alphaNumericalOnly = FALSE;
			comboData->entryData->aSCIIOnly = TRUE;
		}
	}
	staticLocalIP = TheWindowManager->winGetWindowFromId( nullptr, staticLocalIPID);

//	// animate controls
//	TheShell->registerWithAnimateManager(buttonBack, WIN_ANIMATION_SLIDE_LEFT, TRUE, 800);
//	TheShell->registerWithAnimateManager(buttonHost, WIN_ANIMATION_SLIDE_LEFT, TRUE, 600);
//	TheShell->registerWithAnimateManager(buttonJoin, WIN_ANIMATION_SLIDE_LEFT, TRUE, 200);
//
	LobbySettings userprefs(LobbySettings::Kind::Lan);
	UnicodeString name;
	name = userprefs.getUserName();

	if (name.getLength() == 0)
	{
		name = TheGameText->fetch("GUI:Player");
	}

	GadgetTextEntrySetText(editPlayerName, name);

	PopulateRemoteIPComboBox();

	UnicodeString ipstr;

	delete TheLobby;
	::Engine::context().lobby = nullptr;

	if (TheLobby == nullptr) {
//		DEBUG_ASSERTCRASH(TheLobby != NULL, ("TheLobby is null initializing the direct connect screen."));
		::Engine::context().lobby = NEW GameLobby();

		// every adapter, unless -netzLokal or the setting Network.IPAddress names one address
		const UnsignedInt IP = GetLocalNetInstanceIP() ? GetLocalNetInstanceIP() : TheGlobalData->m_defaultIP;
		TheLobby->init();
		TheLobby->SetLocalIP(IP);
	}

	UnsignedInt ip = TheLobby->GetLocalIP();
	ipstr.format(L"%d.%d.%d.%d", ip >> 24, (ip & 0xff0000) >> 16, (ip & 0xff00) >> 8, ip & 0xff);
	GadgetStaticTextSetText(staticLocalIP, ipstr);

	TheLobby->RequestLobbyLeave(true);
	layout->hide(FALSE);
	layout->bringForward();
	TheTransitionHandler->setGroup("NetworkDirectConnectFade");


} // NetworkDirectConnectInit

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

//-------------------------------------------------------------------------------------------------
/** Network Direct Connect Menu shutdown method */
//-------------------------------------------------------------------------------------------------
void NetworkDirectConnectShutdown( WindowLayout *layout, void *userData )
{
	isShuttingDown = true;

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *static_cast<Bool *>(userData);
	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}  //end if

	TheShell->reverseAnimatewindow();

	TheTransitionHandler->reverse("NetworkDirectConnectFade");
}  // NetworkDirectConnectShutdown


//-------------------------------------------------------------------------------------------------
/** Network Direct Connect Menu update method */
//-------------------------------------------------------------------------------------------------
void NetworkDirectConnectUpdate( WindowLayout * layout, void *userData)
{
	// We'll only be successful if we've requested to 
	if(isShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
		shutdownComplete(layout);
}// NetworkDirectConnectUpdate

//-------------------------------------------------------------------------------------------------
/** Network Direct Connect Menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType NetworkDirectConnectInput( GameWindow *window, UnsignedInt msg,
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
}// NetworkDirectConnectInput

//-------------------------------------------------------------------------------------------------
/** Network Direct Connect Menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType NetworkDirectConnectSystem( GameWindow *window, UnsignedInt msg, 
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;

	switch( msg )
	{
		
		
		case GWM_CREATE:
			{
				
				break;
			} // case GWM_DESTROY:

		case GWM_DESTROY:
			{
				break;
			} // case GWM_DESTROY:

		case GWM_INPUT_FOCUS:
			{	
				// if we're givin the opportunity to take the keyboard focus we must say we want it
				if( mData1 == TRUE )
					*reinterpret_cast<Bool *>(mData2) = TRUE;

				return WindowMsgHandledType::MSG_HANDLED;
			}//case GWM_INPUT_FOCUS:

		case GBM_SELECTED:
			{
				if (buttonPushed)
					break;

				GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
				Int controlID = control->winGetWindowId();

				if ( controlID == buttonBackID )
				{
					UnicodeString name;
					name = GadgetTextEntryGetText(editPlayerName);

					LobbySettings prefs(LobbySettings::Kind::Lan);
					prefs.setUserName(name);
					prefs.write();

					while (name.getLength() > g_lanPlayerNameLength)
						name.removeLastChar();
					TheLobby->RequestSetName(name);

					buttonPushed = true;
					LANbuttonPushed = true;
					TheShell->pop();
				} //if ( controlID == buttonBack )
				else if (controlID == buttonHostID)
				{
					HostDirectConnectGame();
				}
				else if (controlID == buttonJoinID)
				{
					JoinDirectConnectGame();
				}
				break;
			}// case GBM_SELECTED:
	
		case GEM_EDIT_DONE:
			{
				break;
			}
		default:
			return WindowMsgHandledType::MSG_IGNORED;

	}//Switch

	return WindowMsgHandledType::MSG_HANDLED;
}// NetworkDirectConnectSystem
