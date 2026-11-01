/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// DesyncReport.h -- what each player writes when the game states of a network game drift apart.
//
// The logic CRCs of all players are compared in the same logic frame on every machine, so every
// player writes its report at the same point of the simulation: a summary (<base>.txt), a deep CRC
// dump of the full object state (<base>.deep with index <base>.deep.idx) and, when the game ends,
// a copy of the replay (<base>.rep). Two players' dumps compared with
// tools/determinism/deepcrc_diff.py show the first object whose state differs.
// Files go to <user data>\Desync.

#pragma once

#ifndef _DESYNCREPORT_H_
#define _DESYNCREPORT_H_

#include <map>

/// Writes the summary and the deep CRC dump; `crcs` maps player index to the CRC it reported.
void WriteDesyncReport( const std::map<Int, UnsignedInt> &crcs, Int connectedPlayers );

/// Base path of the report written in this game (without extension), empty if none.
AsciiString GetDesyncReportBase( void );

/// Forgets the report once the game is over.
void ClearDesyncReport( void );

#endif // _DESYNCREPORT_H_
