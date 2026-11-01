/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// AIStats.cpp: log for AI-versus-AI series. Read only (see AIStats.h); evaluated by tools/ki/ki_serie.py.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "formatinto.h"

#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/ScoreKeeper.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIStats.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/VictoryConditions.h"
#include "GameNetwork/GameInfo.h"

namespace
{
	// Slot of a player: skirmish/LAN players are named "player<slot>" internally; the name key stays
	// even if the AI changes its display name (VictoryConditions::update).
	Int slotOfPlayer( const Player *p )
	{
		AsciiString name = TheNameKeyGenerator->keyToName( p->getPlayerNameKey() );
		if (name.getLength() < 7 || strncmp( name.str(), "player", 6 ) != 0)
			return -1;
		const char *digits = name.str() + 6;
		for (const char *c = digits; *c; ++c)
		{
			if (*c < '0' || *c > '9')
				return -1;
		}
		Int slot = atoi( digits );
		return (slot >= 0 && slot < MAX_SLOTS) ? slot : -1;
	}

	// Like VictoryConditions::cachePlayerPtrs: real parties only, no neutral, civilian or observer players.
	Bool isCountedPlayer( Player *p )
	{
		if (p == nullptr || p == ThePlayerList->getNeutralPlayer() || p->getPlayerTemplate() == nullptr)
			return FALSE;
		if (p->isPlayerObserver())
			return FALSE;
		return p->getSide().compareNoCase( "Civilian" ) != 0;
	}

	struct PlayerCounts
	{
		Int units;			///< mobile units without dozers and harvesters
		Int armyValue;		///< sum of their build costs
		Int idle;			///< of those, idle (AIUpdateInterface::isIdle)
		Int workers;		///< dozers and harvesters
		Int structures;		///< buildings
		Int stuck;			///< units that want to move but have not left the spot for STUCK_FRAMES
	};

	// Units that are ordered to move (AIUpdateInterface::isMoving) but stay within STUCK_MOVE_DIST of
	// the same spot for STUCK_FRAMES are counted as stuck. Tracked per object id in a fixed array (a
	// container would allocate from the memory pools and show up as a leak at exit); ids beyond it are
	// not tracked.
	constexpr UnsignedInt STUCK_TRACK_MAX = 65536;
	constexpr Real STUCK_MOVE_DIST = 3.0f;		///< world units, less than a third of a pathfind cell
	constexpr UnsignedInt STUCK_FRAMES = 300;	///< 10 seconds of game time

	struct StuckTrack
	{
		UnsignedInt id;
		Real x, y;
		UnsignedInt since;	///< frame at which the unit was last seen at (x, y)
		Bool reported;		///< STECKT line already written for this episode
	};
	StuckTrack s_stuckTrack[STUCK_TRACK_MAX];

	// Last blocker per unit (AIStats_noteBlocker), shown in the STECKT line.
	struct BlockerNote
	{
		UnsignedInt frame;		///< 0: none
		UnsignedInt blockerId;
		Bool blockerMoving;
		Bool mutual;
		Bool higherPriority;
	};
	BlockerNote s_blockerNote[STUCK_TRACK_MAX];
}

void AIStats_noteBlocker( const Object *obj, const Object *blocker, Bool blockerMoving, Bool mutual, Bool higherPriority )
{
	if (obj == nullptr || blocker == nullptr || TheGameLogic == nullptr)
		return;
	const UnsignedInt id = static_cast<UnsignedInt>( obj->getID() );
	if (id >= STUCK_TRACK_MAX)
		return;
	BlockerNote &n = s_blockerNote[id];
	n.frame = TheGameLogic->getFrame();
	n.blockerId = static_cast<UnsignedInt>( blocker->getID() );
	n.blockerMoving = blockerMoving;
	n.mutual = mutual;
	n.higherPriority = higherPriority;
}

void AIStats_logPathFailure( const Object *obj, const Coord3D *destination, Bool destinationValid, Bool reachable, const Coord3D *pathEnd )
{
	if (TheGlobalData->m_crcLogFile.isEmpty() || obj == nullptr || destination == nullptr || TheGameLogic == nullptr)
		return;
	static UnsignedInt s_lastLogged[STUCK_TRACK_MAX];
	const UnsignedInt id = static_cast<UnsignedInt>( obj->getID() );
	const UnsignedInt frame = TheGameLogic->getFrame();
	if (id >= STUCK_TRACK_MAX || (s_lastLogged[id] != 0 && frame - s_lastLogged[id] < STUCK_FRAMES))
		return;
	s_lastLogged[id] = frame;
	const Coord3D *pos = obj->getPosition();
	Int endDistance = -1;
	if (pathEnd)
	{
		const Real dx = pathEnd->x - destination->x;
		const Real dy = pathEnd->y - destination->y;
		endDistance = static_cast<Int>( sqrtf( dx * dx + dy * dy ) );
	}
	const Player *owner = obj->getControllingPlayer();
	char line[320];
	WWFormat::Into(line, "WEGFEHL {:d} id {:d} vorlage {} spieler {:d} seite {} von {:d} {:d} nach {:d} {:d} ziel_gueltig {:d} erreichbar {:d} ende_abstand {:d}", frame, id, obj->getTemplate()->getName().str(), owner ? owner->getPlayerIndex() : -1, owner ? owner->getSide().str() : "-", static_cast<Int>( pos->x ), static_cast<Int>( pos->y ), static_cast<Int>( destination->x ), static_cast<Int>( destination->y ), destinationValid ? 1 : 0, reachable ? 1 : 0, endDistance);
	GameEngine_RunnerLog( line );
}

void AIStats_logKill( const Object *killer, const Object *victim )
{
	if (TheGlobalData->m_aiStatsInterval <= 0 || TheGlobalData->m_crcLogFile.isEmpty() || killer == nullptr || victim == nullptr
		|| TheGameLogic == nullptr)
		return;
	const Player *vp = victim->getControllingPlayer();
	const Player *kp = killer->getControllingPlayer();
	if (vp == nullptr || kp == nullptr || vp == kp
		|| (vp->getPlayerType() != PLAYER_COMPUTER && kp->getPlayerType() != PLAYER_COMPUTER))
		return;
	// Near a base: within this distance of the base centre the classic AI keeps (AIPlayer::getBaseCenter).
	const Real BASE_ZONE = 1200.0f;
	auto nearBase = [&]( const Player *p ) -> Bool
	{
		Coord3D c;
		if (p->getAIPlayer() == nullptr || !p->getAIPlayer()->getBaseCenter( &c ))
			return FALSE;
		const Real dx = victim->getPosition()->x - c.x, dy = victim->getPosition()->y - c.y;
		return dx * dx + dy * dy <= BASE_ZONE * BASE_ZONE;
	};
	const char *zone = nearBase( vp ) ? "eigene_basis" : (nearBase( kp ) ? "feindbasis" : "feld");
	char line[256];
	WWFormat::Into(line, "KIVERLUST {:d} spieler {:d} vorlage {} wert {:d} gebaeude {:d} von_spieler {:d} von_vorlage {} von_gebaeude {:d} ort {}", TheGameLogic->getFrame(), vp->getPlayerIndex(), victim->getTemplate()->getName().str(), victim->getTemplate()->friend_getBuildCost(), victim->isKindOf( KINDOF_STRUCTURE ) ? 1 : 0, kp->getPlayerIndex(), killer->getTemplate()->getName().str(), killer->isKindOf( KINDOF_STRUCTURE ) ? 1 : 0, zone);
	GameEngine_RunnerLog( line );
}

void AIStats_logOrder( const Player *player, Bool structure, const ThingTemplate *thing )
{
	if (TheGlobalData->m_aiStatsInterval <= 0 || TheGlobalData->m_crcLogFile.isEmpty() || player == nullptr || thing == nullptr
		|| TheGameLogic == nullptr || player->getPlayerType() != PLAYER_COMPUTER)
		return;
	char line[224];
	WWFormat::Into(line, "KIAUFTRAG {:d} spieler {:d} art {} vorlage {} kosten {:d}", TheGameLogic->getFrame(), player->getPlayerIndex(), structure ? "gebaeude" : "einheit", thing->getName().str(), thing->calcCostToBuild( player ));
	GameEngine_RunnerLog( line );
}

void AIStats_logFrame( UnsignedInt frame )
{
	if (TheGlobalData->m_crcLogFile.isEmpty() || ThePlayerList == nullptr)
		return;

	PlayerCounts counts[MAX_PLAYER_COUNT];
	memset( counts, 0, sizeof( counts ) );
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->isEffectivelyDead())
			continue;
		Player *owner = obj->getControllingPlayer();
		if (owner == nullptr)
			continue;
		Int idx = owner->getPlayerIndex();
		if (idx < 0 || idx >= MAX_PLAYER_COUNT)
			continue;
		PlayerCounts &c = counts[idx];
		if (obj->isKindOf( KINDOF_STRUCTURE ))
		{
			++c.structures;
			continue;
		}
		if (obj->isKindOf( KINDOF_DOZER ) || obj->isKindOf( KINDOF_HARVESTER ))
		{
			++c.workers;
			continue;
		}
		if (!obj->isKindOf( KINDOF_INFANTRY ) && !obj->isKindOf( KINDOF_VEHICLE ))
			continue;
		if (obj->isKindOf( KINDOF_DRONE ) || obj->isKindOf( KINDOF_IGNORED_IN_GUI ) || obj->isKindOf( KINDOF_PROJECTILE ))
			continue;
		++c.units;
		c.armyValue += obj->getTemplate()->friend_getBuildCost();
		const AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (ai && ai->isIdle())
			++c.idle;

		// Stuck units: wants to move, but the position does not change (STECKT line once per episode)
		const UnsignedInt id = static_cast<UnsignedInt>( obj->getID() );
		if (ai && id < STUCK_TRACK_MAX)
		{
			StuckTrack &tr = s_stuckTrack[id];
			const Coord3D *pos = obj->getPosition();
			const Real dx = pos->x - tr.x;
			const Real dy = pos->y - tr.y;
			if (tr.id != id || !ai->isMoving() || ai->isWaitingForPath() || dx * dx + dy * dy >= STUCK_MOVE_DIST * STUCK_MOVE_DIST)
			{
				tr.id = id;
				tr.x = pos->x;
				tr.y = pos->y;
				tr.since = frame;
				tr.reported = FALSE;
			}
			else if (frame - tr.since >= STUCK_FRAMES)
			{
				++c.stuck;
				if (!tr.reported)
				{
					tr.reported = TRUE;
					char stuckLine[448];
					const Coord3D *goal = ai->getGoalPosition();
					const Real gx = goal ? goal->x - pos->x : 0.0f;
					const Real gy = goal ? goal->y - pos->y : 0.0f;
					const BlockerNote &bn = s_blockerNote[id];
					WWFormat::Into(stuckLine, "STECKT {:d} id {:d} vorlage {} spieler {:d} seite {} x {:d} y {:d} seit {:d} blockiert {:d} haengt {:d} ziel {:d} {:d} abstand_ziel {:d} zustand {:d} blockierer {:d} faehrt {:d} gegenseitig {:d} prioritaet {:d} vor_frames {:d}", frame, id, obj->getTemplate()->getName().str(), idx, owner->getSide().str(), static_cast<Int>( pos->x ), static_cast<Int>( pos->y ), tr.since, ai->getNumFramesBlocked(), ai->isBlockedAndStuck() ? 1 : 0, goal ? static_cast<Int>( goal->x ) : -1, goal ? static_cast<Int>( goal->y ) : -1, static_cast<Int>( sqrtf( gx * gx + gy * gy ) ), static_cast<UnsignedInt>( ai->getCurrentStateID() ), bn.frame ? bn.blockerId : 0, bn.blockerMoving ? 1 : 0, bn.mutual ? 1 : 0, bn.higherPriority ? 1 : 0, bn.frame ? static_cast<Int>( frame - bn.frame ) : -1);
					GameEngine_RunnerLog( stuckLine );
				}
			}
		}
	}

	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		if (!isCountedPlayer( p ))
			continue;
		Int slot = slotOfPlayer( p );
		const GameSlot *gs = (TheGameInfo && slot >= 0) ? TheGameInfo->getConstSlot( slot ) : nullptr;
		Int kind = (gs && gs->isAI()) ? gs->getAIKind() : -1;
		Int team = gs ? gs->getTeamNumber() : -1;
		ScoreKeeper *sk = p->getScoreKeeper();
		const PlayerCounts &c = counts[p->getPlayerIndex()];
		Bool defeated = TheVictoryConditions ? TheVictoryConditions->hasSinglePlayerBeenDefeated( p ) : FALSE;
		char line[512];
		WWFormat::Into(line, "KISTAT {:d} spieler {:d} platz {:d} seite {} art {:d} team {:d} besiegt {:d} geld {:d} einnahmen {:d} ausgaben {:d} einheiten {:d} armeewert {:d} leerlauf {:d} arbeiter {:d} gebaeude {:d} gebaut_e {:d} gebaut_g {:d} verloren_e {:d} verloren_g {:d} zerstoert_e {:d} zerstoert_g {:d} punkte {:d} rang {:d} gegner {:d} steckt {:d}", frame, p->getPlayerIndex(), slot, p->getSide().str(), kind, team, defeated ? 1 : 0, p->getMoney()->countMoney(), sk->getTotalMoneyEarned(), sk->getTotalMoneySpent(), c.units, c.armyValue, c.idle, c.workers, c.structures, sk->getTotalUnitsBuilt(), sk->getTotalBuildingsBuilt(), sk->getTotalUnitsLost(), sk->getTotalBuildingsLost(), sk->getTotalUnitsDestroyed(), sk->getTotalBuildingsDestroyed(), p->getSciencePurchasePoints(), p->getRankLevel(), p->peekCurrentEnemy() ? p->peekCurrentEnemy()->getPlayerIndex() : -1, c.stuck);
		GameEngine_RunnerLog( line );
	}
}

Bool AIStats_logVictoryIfDecided( UnsignedInt frame )
{
	if (TheVictoryConditions == nullptr || ThePlayerList == nullptr || TheVictoryConditions->getEndFrame() == 0)
		return FALSE;

	AIStats_logFrame( frame );
	AsciiString winners;
	Int winnerTeam = -2;
	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		if (!isCountedPlayer( p ) || !TheVictoryConditions->hasAchievedVictory( p ))
			continue;
		Int slot = slotOfPlayer( p );
		const GameSlot *gs = (TheGameInfo && slot >= 0) ? TheGameInfo->getConstSlot( slot ) : nullptr;
		AsciiString entry;
		entry.format( " %d/%d/%d", p->getPlayerIndex(), slot, (gs && gs->isAI()) ? gs->getAIKind() : -1 );
		winners.concat( entry );
		if (gs)
			winnerTeam = gs->getTeamNumber();
	}
	char line[256];
	WWFormat::Into(line, "KISIEG {:d} ende {:d} team {:d} sieger(spieler/platz/art){}", frame, TheVictoryConditions->getEndFrame(), winnerTeam, winners.isEmpty() ? " keiner" : winners.str());
	GameEngine_RunnerLog( line );
	return TRUE;
}
