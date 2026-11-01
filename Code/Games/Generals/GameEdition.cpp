/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: GameEdition.cpp /////////////////////////////////////////////////////////////////////////
// Selects the game this executable belongs to. The only source file that differs between
// Code/Games/Generals and Code/Games/ZeroHour; see Common/GameEdition.h.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameEdition.h"

GameEditionType getGameEdition( void )
{
	return GameEditionType::GAME_EDITION_GENERALS;
}
