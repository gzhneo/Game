/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: GameEdition.h ///////////////////////////////////////////////////////////////////////////
// Which game this executable is.
//
// Both games are built from the same sources. Where Generals and Zero Hour must differ in code (not in
// data), shared code asks here at runtime instead of branching with #if. The answer comes from one
// translation unit per game (GameEdition.cpp in Code/Games/Generals or Code/Games/ZeroHour).
//
// Used for defaults the data does not carry: user data folder, retaliation, mod mouse cursors,
// online product id.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __GAMEEDITION_H_
#define __GAMEEDITION_H_

enum class GameEditionType
{
	GAME_EDITION_GENERALS = 0,		///< Command & Conquer Generals (GZHNeo.exe)
	GAME_EDITION_ZERO_HOUR = 1		///< Command & Conquer Generals Zero Hour (GZHNeoZH.exe)
};

/// The game this executable belongs to (fixed per build target).
GameEditionType getGameEdition( void );

inline bool isGameEditionZeroHour( void ) { return getGameEdition() == GameEditionType::GAME_EDITION_ZERO_HOUR; }
inline bool isGameEditionGenerals( void ) { return getGameEdition() == GameEditionType::GAME_EDITION_GENERALS; }

#endif // __GAMEEDITION_H_
