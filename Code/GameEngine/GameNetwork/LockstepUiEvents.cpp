/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/LockstepEvents.h"

#include "Common/AudioEventRTS.h"
#include "Common/GameAudio.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/DisconnectMenu.h"
#include "GameClient/Diplomacy.h"
#include "GameClient/InGameUI.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GameLobby.h"
#include "GameNetwork/NetworkInterface.h"

namespace
{

// The waiting screen exists while a network game shows it, created when it is first needed.
DisconnectMenu &waitingScreen( void )
{
	if ( TheDisconnectMenu == nullptr )
	{
		::Engine::context().disconnectMenu = NEW DisconnectMenu;
		TheDisconnectMenu->init();
	}
	return *TheDisconnectMenu;
}

Int screenPosition( Int slot )
{
	return DisconnectMenu::screenPosition( slot, static_cast<Int>(TheNetwork->getLocalPlayerID()) );
}

class GameLockstepEvents final : public LockstepEvents
{
public:
	void playerLeft( Int slot, const UnicodeString &name ) override
	{
		if ( TheDisconnectMenu != nullptr && TheDisconnectMenu->isScreenVisible() && TheNetwork != nullptr )
		{
			const Int position = screenPosition( slot );
			if ( position >= 0 )
				TheDisconnectMenu->removePlayer( position, name );
		}
		TheInGameUI->message( "Network:PlayerLeftGame", name.str() );
		// Not static: the event's string buffer would otherwise outlive the leak report; addAudioEvent
		// copies it anyway.
		AudioEventRTS leftGameSound( "GUIMessageReceived" );
		TheAudio->addAudioEvent( &leftGameSound );
	}

	void playersChanged( void ) override
	{
		PopulateInGameDiplomacyPopup();
	}

	void chat( Int slot, UnsignedByte playerMask, Int localSlot, const UnicodeString &line ) override
	{
		AsciiString playerName;
		playerName.format( "player%d", slot );
		const Player *player = ThePlayerList->findPlayerWithNameKey( TheNameKeyGenerator->nameToKey( playerName ) );
		if ( !player )
		{
			TheInGameUI->message( UnicodeString( L"%ls" ), line.str() );
			return;
		}

		// observers read everything; players do not read observers or muted players
		const Bool fromObserver = !player->isPlayerActive();
		const Bool amIObserver = !ThePlayerList->getLocalPlayer()->isPlayerActive();
		const Bool canSeeChat = amIObserver || (!fromObserver && !TheGameInfo->getConstSlot( slot )->isMuted());
		if ( ((1 << localSlot) & playerMask) && canSeeChat )
		{
			RGBColor rgb;
			rgb.setFromInt( player->getPlayerColor() );
			TheInGameUI->messageColor( &rgb, UnicodeString( L"%ls" ), line.str() );

			AudioEventRTS audioEvent( "GUICommunicatorIncoming" );
			TheAudio->addAudioEvent( &audioEvent );
		}
	}

	void disconnectChat( const UnicodeString &line ) override
	{
		TheDisconnectMenu->showChat( line );
	}

	void fileNotSent( const UnicodeString &reason ) override
	{
		if ( TheLobby )
			TheLobby->OnChat( UnicodeString( L"sendFile" ), 0, reason, GameLobby::LANCHAT_SYSTEM );
	}

	void waitingForPlayers( Bool waiting ) override
	{
		if ( waiting )
		{
			waitingScreen().showScreen();
			TheDisconnectMenu->hidePacketRouterTimeout();
		}
		else if ( TheDisconnectMenu != nullptr )
		{
			TheDisconnectMenu->hideScreen();
		}
	}

	void waitingPlayer( Int slot, const UnicodeString &name, Int votes ) override
	{
		const Int position = screenPosition( slot );
		if ( position < 0 )
			return;
		waitingScreen().setPlayerName( position, name );
		TheDisconnectMenu->updateVotes( position, votes );
	}

	void dropCountdown( Int slot, Int milliseconds ) override
	{
		const Int position = screenPosition( slot );
		if ( position >= 0 )
			waitingScreen().setPlayerTimeoutTime( position, milliseconds );
	}

	void networkClosed( void ) override
	{
		delete TheDisconnectMenu;
		::Engine::context().disconnectMenu = nullptr;
	}

	void removalVote( Int voter, Int target, Int votes, Int needed ) override
	{
		const UnicodeString voterName = TheNetwork->getPlayerName( voter );
		const UnicodeString targetName = TheNetwork->getPlayerName( target );
		TheInGameUI->message( "GZH:RemoveVote", voterName.str(), targetName.str(), votes, needed, targetName.str() );
		AudioEventRTS sound( "GUIMessageReceived" );
		TheAudio->addAudioEvent( &sound );
	}

	void removedByVote( Int slot, const UnicodeString &name ) override
	{
		if ( static_cast<UnsignedInt>(slot) == TheNetwork->getLocalPlayerID() )
			TheInGameUI->message( "GZH:YouWereRemoved" );
		else
			TheInGameUI->message( "GZH:RemovedByVote", name.str() );
	}

	void playerReturned( Int, const UnicodeString &name ) override
	{
		TheInGameUI->message( "GZH:PlayerReturned", name.str() );
		AudioEventRTS sound( "GUIMessageReceived" );
		TheAudio->addAudioEvent( &sound );
	}

	void returnClosed( Int slot ) override
	{
		const GameSlot *s = TheGameInfo != nullptr ? TheGameInfo->getConstSlot( slot ) : nullptr;
		if ( s != nullptr && s->isHuman() )
			TheInGameUI->message( "GZH:ReturnClosed", s->getName().str() );
	}
};

} // namespace

LockstepEvents &LockstepEvents::game( void )
{
	static GameLockstepEvents events;
	return events;
}
