/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// AIPlanner.h: planning of the new AI on the job system, with a fixed take-over latency.
//
// Cycle per AI player: at tick N the sim thread copies what the player knows into an
// AIPlanInput (plain integers, no engine pointers) and starts AIPlanner_compute on a worker.
// At tick N + PLAN_LATENCY the sim thread waits for the job (normally long done) and takes the
// result over. The result therefore depends only on the input and the tick, never on the
// number of workers or on thread timing; with -workers 0 the job runs inside the start call.
//
// The input contains only what the player may know: own and allied objects, enemy units in
// cells the player sees right now, enemy buildings and neutral supply/tech buildings in cells
// the player has seen at least once. Stealthed objects count only when detected.
//
// Combat model (AICombatModel.h): when the game starts, the planner builds a table from the game
// data -- per unit and building template its health, cost, armor and damage per second against
// every armor. Each job forecasts the own field army against the seen army of the current enemy
// (with and without his base defences) and ranks the units the AI can build by their fighting
// value per money against that enemy mix (counter production, used by the late game).
//
// Influence maps (KA-1): the job spreads the enemy's ground threat (damage per second within his
// weapons' reach) and the value of his objects over a coarse grid. For a running attack it
// judges every known building of the enemy as a target: the attack force against the armed
// enemies near that building (forecast), the value there, and the threat on the straight way to
// it. It also forecasts the force against the enemies around its current position, so the command
// layer can retreat before a lost fight.
//
// Log with -aiStats: KIPLAN (summary and checksum, equal in every run), KIPROGNOSE and KIKONTER
// (every 10 plans), KIZEIT (measured times, real time). They only read and do not change the CRC.

#pragma once

#ifndef _AI_PLANNER_H_
#define _AI_PLANNER_H_

#include "Engine/JobSystem.h"
#include "GameLogic/AICombatModel.h"

#include <vector>

class Player;
class ThingTemplate;
class Xfer;

/// One object as the planner sees it.
struct AIPlanObject
{
	enum Flags
	{
		STRUCTURE		= 0x01,
		INFANTRY		= 0x02,
		VEHICLE			= 0x04,
		AIRCRAFT		= 0x08,
		WORKER			= 0x10,		///< dozer or harvester
		SUPPLY			= 0x20,		///< supply source
		TECH				= 0x40,		///< capturable tech building
		CAN_ATTACK	= 0x80,
		SUPERWEAPON	= 0x100,	///< superweapon building
		ECONOMY			= 0x200		///< supply centre (cash generator)
	};

	UnsignedInt	id;					///< ObjectID
	Int					owner;			///< player index, -1 without owner
	Int					relation;		///< AIPlanRelation
	UnsignedInt	flags;
	Int					cost;				///< build cost
	Int					x, y;				///< position in whole world units
	Int					health;			///< health in per mille of the maximum
	Int					profile;		///< row of the combat table, -1 if none
};

/// A unit the AI can build now.
struct AIPlanCandidate
{
	Int profile;
	Int cost;
};

/// A ranked candidate of the counter production.
struct AIPlanChoice
{
	Int		profile;
	Real	score;		///< fighting value per money against the enemy mix
};

enum AIPlanRelation
{
	AI_PLAN_SELF = 0,
	AI_PLAN_ALLY = 1,
	AI_PLAN_ENEMY = 2,
	AI_PLAN_NEUTRAL = 3
};

/// Everything a planning job reads; copied on the sim thread.
struct AIPlanInput
{
	UnsignedInt	frame = 0;
	Int					self = -1;			///< player index
	Int					money = 0;
	Int					focusEnemy = -1;	///< player index of the current enemy, -1 = all enemies
	Int					worldWidth = 0;		///< map extent in world units
	Int					worldHeight = 0;
	std::vector<UnsignedInt> mission;	///< ObjectIDs of the running attack, empty without one
	std::vector<UnsignedInt> raid;		///< ObjectIDs of the raid squad (transport and riders), empty without one
	std::vector<AIPlanObject> objects;		///< in the order of the game logic object list
	std::vector<AIPlanCandidate> candidates;	///< buildable army units, in factory and button order
};

/// What a planning job yields.
struct AIPlanResult
{
	UnsignedInt	frame = 0;					///< frame of the input
	UnsignedInt	checksum = 0;				///< over the whole input
	Int					ownUnits = 0;
	Int					ownValue = 0;				///< build cost of the own mobile units
	Int					ownStructures = 0;
	Int					allyValue = 0;
	Int					enemyUnitsSeen = 0;
	Int					enemyValueSeen = 0;
	Int					enemyStructuresKnown = 0;
	Int					suppliesKnown = 0;

	// Forecast of the own field army against the seen field army of the enemy (field) and against
	// that army plus his known armed buildings (base).
	Int					fieldWinner = -1;		///< 0 = own army, 1 = enemy, -1 = neither
	Real				fieldSurvivors = 0.0f;	///< share of the winner's health left
	Real				fieldRatio = 0.0f;		///< own strength / enemy strength
	Int					baseWinner = -1;
	Real				baseSurvivors = 0.0f;
	Real				baseRatio = 0.0f;
	Int					enemyHealthSeen = 0;	///< health of the enemy mix the counters are chosen for
	std::vector<AIPlanChoice> counters;	///< best first; empty without a seen enemy

	// Attack (only with a mission): best target building and the forecast there, and the forecast
	// against the enemies around the force's current position.
	UnsignedInt	objectiveId = 0;		///< 0 = none found
	Int					objectiveX = 0, objectiveY = 0;
	Real				objectiveRatio = 0.0f;	///< force strength / strength of the defenders there
	Int					objectiveWinner = -1;
	Real				objectiveThreat = 0.0f;	///< threat on the way (sum of damage per second over the cells)
	Int					siteWinner = -1;
	Real				siteRatio = 0.0f;
	Int					siteEnemyHealth = 0;
	Real				maxThreat = 0.0f;			///< highest cell of the threat map (log)

	// Raid (only with a squad): best economic target (worker or supply centre) and the forecasts.
	UnsignedInt	raidTargetId = 0;
	Int					raidTargetX = 0, raidTargetY = 0;
	Int					raidTargetWinner = -1;
	Real				raidTargetRatio = 0.0f;
	Int					raidSiteWinner = -1;
	Real				raidSiteRatio = 0.0f;
	Int					raidSiteEnemyHealth = 0;
};

/// The planning job: a pure function of its input and the combat table (runs on a worker).
AIPlanResult AIPlanner_compute( const AIPlanInput &input, const AICombat::Table &table );

/// Superweapon building (FS_SUPERWEAPON, in the Generals data by name).
Bool AIPlanner_isSuperweapon( const ThingTemplate *t );

/// Builds the combat table from the game data; profileOfTemplate maps a template id to its row.
void AIPlanner_buildCombatTable( AICombat::Table *table, std::vector<Int> *profileOfTemplate,
	std::vector<const ThingTemplate *> *templateOfProfile );

class AIPlanner
{
public:
	enum
	{
		PLAN_INTERVAL	= 30,		///< logic frames between two snapshots (1 s)
		PLAN_LATENCY	= 15		///< logic frames from snapshot to take-over (0.5 s)
	};

	AIPlanner();
	~AIPlanner();

	/// First snapshot spread by player index, so the AIs do not all copy in the same tick.
	void init( Player *self );

	/// TRUE if update() takes a snapshot this frame; then call setInputs() first.
	Bool isSnapshotDue( void ) const;
	/// Inputs the command layer provides for the next snapshot.
	void setInputs( const std::vector<AIPlanCandidate> &candidates, Int focusEnemy, const std::vector<UnsignedInt> &mission,
		const std::vector<UnsignedInt> &raid );

	/// Every logic frame: take the result over when due, start the next job when due.
	void update( Player *self );

	/// Row of the combat table for a template, -1 if none.
	Int profileOf( const ThingTemplate *t ) const;
	/// Counter score of a profile in the last result, -1 if it is not ranked.
	Real counterScoreOf( Int profile ) const;
	const AICombat::Table &getCombatTable( void ) const { return m_table; }

	Bool hasResult( void ) const { return m_hasResult; }
	const AIPlanResult &getResult( void ) const { return m_last; }

	void xfer( Xfer *xfer );
	/// After loading: a job that was running at save time is started again from its saved input.
	void loadPostProcess( void );

private:
	AIPlanner( const AIPlanner & );
	AIPlanner &operator=( const AIPlanner & );

	void takeSnapshot( Player *self, UnsignedInt frame );
	void startJob( void );
	void takeOver( Player *self, UnsignedInt frame );
	void logTimes( Player *self, UnsignedInt frame );

	::Engine::AsyncTask	m_task;
	AICombat::Table	m_table;				///< built once in init(), read-only afterwards (also by jobs)
	std::vector<Int> m_profileOfTemplate;	///< by template id
	std::vector<const ThingTemplate *> m_templateOfProfile;	///< for log lines (sim thread only)
	std::vector<AIPlanCandidate> m_nextCandidates;
	std::vector<UnsignedInt> m_nextMission;
	std::vector<UnsignedInt> m_nextRaid;
	Int						m_nextFocusEnemy;
	Int						m_plans;					///< plans taken over (log rate)
	AIPlanInput		m_input;				///< read by the running job
	AIPlanResult	m_jobResult;		///< written by the running job
	AIPlanResult	m_last;					///< last result taken over
	Bool					m_hasResult;
	Bool					m_running;				///< a job is started and not yet taken over
	UnsignedInt		m_takeOverFrame;
	UnsignedInt		m_nextSnapshotFrame;

	// Measurement only (real time); not saved and never used for decisions.
	long long			m_jobMicros;			///< written by the running job
	Int						m_timedPlans;
	long long			m_snapshotSum, m_snapshotMax;
	long long			m_jobSum, m_jobMax;
	long long			m_waitSum, m_waitMax;
	long long			m_objectSum;
};

#endif // _AI_PLANNER_H_
