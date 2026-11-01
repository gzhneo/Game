/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// AIStats.h: log for AI-versus-AI series.
//
// With -aiStats <N> the game logic writes a KISTAT line per player to the runner log (-crcLog)
// every N logic frames, and a KISIEG line once only one alliance is left; then the game ends as
// with -maxFrames. The functions only read the game state (no random numbers, no commands, no
// new name keys), so the CRC sequence stays the same.
//
// Stuck units: KISTAT ends with "steckt N" (units that are ordered to move but have not left a 3 unit
// radius for 300 frames); the first frame of each episode also writes a STECKT line with the unit,
// its position, the frames it was blocked and whether the pathfinder marked it "blocked and stuck",
// its goal, the state of its state machine and the last unit that blocked it (id, moving or not,
// mutual block, own path priority) with the number of frames since that block.

#pragma once

#ifndef _AI_STATS_H_
#define _AI_STATS_H_

/// One KISTAT line per player in the runner log (only with -crcLog).
void AIStats_logFrame( UnsignedInt frame );

/// A unit was blocked by another one (AIUpdateInterface::processCollision): remembered per unit and shown in
/// its STECKT line (last blocker, whether it moves, whether it is blocked by us in turn, whether we have the
/// higher path priority). Read only.
void AIStats_noteBlocker( const Object *obj, const Object *blocker, Bool blockerMoving, Bool mutual, Bool higherPriority );

/// A move order got no direct path (AIUpdateInterface::computePath fell back to the closest reachable
/// point): WEGFEHL line with the unit, both positions, whether the destination cell is valid and whether
/// the pathfinder thinks the destination is reachable at all, and how far the fallback path ends from
/// the destination (-1: no path). At most one line per unit and 300 frames.
void AIStats_logPathFailure( const Object *obj, const Coord3D *destination, Bool destinationValid, Bool reachable, const Coord3D *pathEnd );

/// A computer player ordered a unit (queued in a factory) or started a building: KIAUFTRAG line with the
/// template and its build cost, for comparing the build orders of both AIs with human replays.
void AIStats_logOrder( const Player *player, Bool structure, const ThingTemplate *thing );

/// An object of a computer player was destroyed, or a computer player destroyed one: KIVERLUST line with
/// victim, build cost, killer (unit or building) and where it happened relative to the AI bases
/// (eigene_basis = near the victim's base, feindbasis = near the killer's base, feld = elsewhere).
void AIStats_logKill( const Object *killer, const Object *victim );

/// Is the game decided (one alliance left)? Then KISTAT for everyone, KISIEG and TRUE.
Bool AIStats_logVictoryIfDecided( UnsignedInt frame );

#endif // _AI_STATS_H_
