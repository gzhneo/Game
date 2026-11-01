/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// LockstepEvents.h -- what the lockstep core reports to the player.
//
// The connection manager does not reach into the user interface: players leaving, chat, the map
// transfer and the waiting for overdue players go to a handler. The game's handler (LockstepUiEvents.cpp) shows them as in-game
// messages and sounds; tests install their own.

#pragma once

#include "Common/UnicodeString.h"

class LockstepEvents
{
public:
	virtual ~LockstepEvents() = default;

	/// A player left the game or was disconnected.
	virtual void playerLeft( Int slot, const UnicodeString &name ) = 0;
	/// The players of the game changed.
	virtual void playersChanged( void ) = 0;
	/// Chat of `slot` to the players in `playerMask`; `line` starts with the sender's name.
	virtual void chat( Int slot, UnsignedByte playerMask, Int localSlot, const UnicodeString &line ) = 0;
	/// Chat on the disconnect screen.
	virtual void disconnectChat( const UnicodeString &line ) = 0;
	/// A file of the map transfer could not be sent.
	virtual void fileNotSent( const UnicodeString &reason ) = 0;

	/// A frame is overdue and the game waits for the other players (`waiting`), or it plays on again.
	virtual void waitingForPlayers( Bool waiting ) = 0;
	/// While waiting: player `slot` (`name` empty: nobody in that slot) and the votes to drop it.
	virtual void waitingPlayer( Int slot, const UnicodeString &name, Int votes ) = 0;
	/// While waiting: milliseconds until `slot` is dropped for not answering.
	virtual void dropCountdown( Int slot, Int milliseconds ) = 0;
	/// The network game is over; whatever was shown for it goes.
	virtual void networkClosed( void ) = 0;

	/// `voter` voted to remove `target`; `votes` of the `needed` votes are there.
	virtual void removalVote( Int voter, Int target, Int votes, Int needed ) = 0;
	/// The players voted `slot` out; it leaves the game now (reported as leaving as well).
	virtual void removedByVote( Int slot, const UnicodeString &name ) = 0;
	/// `slot`, dropped for not answering, is back in the game.
	virtual void playerReturned( Int slot, const UnicodeString &name ) {}
	/// The return window of the dropped `slot` ran out: its units are destroyed, it stays out.
	virtual void returnClosed( Int slot ) {}

	/// The handler of the game: in-game messages, sounds, diplomacy screen, lobby chat.
	static LockstepEvents &game( void );
};
