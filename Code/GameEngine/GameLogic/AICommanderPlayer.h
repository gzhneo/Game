/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// AICommanderPlayer.h: the selectable computer AI.
//
// It is the classic skirmish AI (AISkirmishPlayer with the EA scripts for building, teams and
// waves) plus a command layer on top. The command layer has its own picture of the situation and
// gives units orders the scripts do not know:
//   Assist allies: if an allied player (AI or human) is attacked at its base or economy, the AI
//       sends available forces as far as its own defence allows.
//   Common enemy: the new AIs of an alliance hunt the same enemy (the alliance leader chooses,
//       the others follow) and switch when another enemy presses the alliance. The EA scripts
//       refer to "<This Player's Enemy>", so waves, attack paths and superweapons follow the
//       common enemy. Attack missions use a pincer: idle units gather at the edge of the own
//       base, wait for the ally and strike together from both bases. The planner picks the
//       target (value, defenders, threat on the way), the force sets out alone only if the
//       forecast against the defenders is not clearly lost, and retreats when the fight on site is.
//   Late game: from minute 6 the command layer spends money above 2000 itself on player
//       upgrades and ground units in idle factories: those the planner ranks best against the
//       seen enemy mix (counter production), otherwise the strongest of the side.
//   Sustainable economy: measure the remaining value of the own supply depots, open up new
//       depots in time and build permanent income (drop zone, black market, hackers and
//       internet center); the hacker target follows time, depots and supply centers, and
//       supply trucks are topped up. The depots' remaining time is forecast from the gather
//       rate; permanent income and hackers start early enough (hackers rank up only by hacking,
//       so they need a lead time read from their data to pay at the elite rank in time).
//   Planner: every second a copy of what the player knows goes to a job on a worker thread; the
//       result is taken over half a second later on every machine (AIPlanner). It forecasts
//       the own army against the enemy's and ranks the buildable units against his mix.
//   Raids: a fast transport whose riders fire from inside (the Humvee with rocket soldiers, read
//       from the data: transport containers that let passengers fire) strikes the enemy's
//       harvesters, dozers and supply centres chosen by the planner and falls back when the
//       transport is damaged or the forecast on site turns; the squad is kept for the next raid.
//   Fight control for the units of the own missions (attack, assist), twice a second: focus fire
//       on the enemy that hurts most per point of health, without overkill; long-ranged units step
//       back from short-ranged threats; a clumped group spreads out against area damage or when
//       an enemy superweapon is (nearly) ready. Units of the EA script teams are left alone:
//       a direct order would end their hunt or guard state.
//   Superweapons and general powers: the target is the spot with the highest value of known
//       enemy objects within the radius -- an army approaching the own base and the defences of
//       the own attack target count more, superweapon buildings double, own and allied objects
//       in the radius count against it. The enemy's superweapon is a priority attack target.
//   Difficulty: Brutal is the tuning above; Easy and Medium run the same layer with throttled
//       parameters (reaction time, share held at home, attack size and pause, late game start
//       and units per pass, concurrent missions, economy investment), see Pace in the source.
// The new AI is chosen per skirmish slot (GameSlot::getAIKind, "CHN" in the replay header);
// the classic AI (AISkirmishPlayer) stays unchanged and is the default.
//
// Determinism: decisions only from the game state, order only from the game logic object list,
// player index and ObjectID; no pointer or address order, no real time; randomness only through
// GameLogicRandomValue. Work on other threads only through AIPlanner (copied input, fixed
// take-over frame).

#pragma once

#ifndef _AI_COMMANDER_PLAYER_H_
#define _AI_COMMANDER_PLAYER_H_

#include "GameLogic/AIPlanner.h"
#include "GameLogic/AISkirmishPlayer.h"

#include <vector>

/// Phase of the attack mission; saved as Int.
enum class CommanderAttackPhase : Int
{
	NONE = 0,			///< no attack
	GATHER = 1,		///< units gather at the edge of the own base
	ATTACK = 2		///< on the way to or fighting the common enemy
};

/// Phase of the raid; saved as Int.
enum class CommanderRaidPhase : Int
{
	NONE = 0,			///< no raid (a squad may wait at home)
	ASSEMBLE = 1,	///< transport and riders are built and board
	RAID = 2,			///< on the way to or fighting at the target
	RETURN = 3		///< back home
};

class CommandButton;
class SpecialPowerTemplate;

class AICommanderPlayer : public AISkirmishPlayer
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AICommanderPlayer, "AICommanderPlayer" )

public:
	AICommanderPlayer( Player *p );
	void setPersonality( Int personality );			///< AIPersonality

	virtual void update() override;			///< classic AI, then the command layer

	/// Enemy choice within the alliance instead of the classic spread (acquireEnemy).
	virtual Player *getAiEnemy( void ) override;

	/// Superweapon and general power target: the richest known spot within the radius.
	virtual Bool computeSuperweaponTarget( const SpecialPowerTemplate *power, Coord3D *pos, Int playerNdx, Real weaponRadius ) override;

	/// The new AI of a player, otherwise nullptr (human player or classic AI).
	static AICommanderPlayer *commanderOf( Player *p );

	// Read by allies to coordinate the pincer: state only, no side effects.
	CommanderAttackPhase getAttackPhase( void ) const { return m_attackPhase; }
	Int getAttackEnemyIndex( void ) const { return m_attackEnemy; }
	UnsignedInt getAttackPhaseFrame( void ) const { return m_attackPhaseFrame; }
	Bool isGatherReady( void ) const { return m_gatherReady; }

protected:
	// snapshot methods
	virtual void crc( Xfer *xfer ) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess( void ) override;

private:
	// Assist allies
	void updateAllyAssist( void );
	void defendOwnBase( void );

	// Reconnaissance and memory: the AI knows of its enemies only what it (or an ally) has seen.
	struct KnownObject
	{
		ObjectID id;
		Int owner;						///< player index
		Coord3D pos;
		UnsignedInt frame;		///< last seen
		Real value;						///< build cost (combat value for units)
		Bool structure;
	};
	void updateMemory( void );
	void updateScouting( void );
	Bool isSeenNow( const Coord3D *pos ) const;
	Bool nextScoutTarget( const Coord3D &from, Coord3D *target ) const;
	const std::vector<std::pair<Int, Coord3D>> &startPositions( void ) const;

	// Enemy model: patterns of the enemies from the memory, recomputed every pass.
	void updateEnemyModel( void );
	Real reserveFraction( void ) const;
	Bool hitsAir( const ThingTemplate *t ) const;
	const ThingTemplate *findConstructableDetector( void ) const;
	static Bool isDetectorTemplate( const ThingTemplate *t );
	void updateDetector( void );
	void updateDefences( void );
	Bool defenceSiteHeld( const Coord3D &site ) const;
	void noteOwnOrder( const BuildListInfo *headBefore );
	void updateOwnOrders( void );
	Bool hasHumanPartner( void ) const;
	void updatePartner( void );
	enum OwnBeaconKind { OWN_BEACON_ATTACK = 0, OWN_BEACON_HELP, OWN_BEACON_RETREAT, OWN_BEACON_COMING };
	void placeOwnBeacon( const Coord3D &pos, Int kind, UnsignedInt until = 0 );
	void removeOwnBeacon( const char *reason );
	Bool findMarkObjective( const Player *enemy, Coord3D *pos, ObjectID *id ) const;
	Bool findAttackObjective( const Player *enemy, const Coord3D &from, Coord3D *pos, ObjectID *id ) const;
	void updateSuperweaponGuard( void );
	Bool isSuperweaponGuard( ObjectID id ) const;
	Bool isHelpableAlly( Player *p ) const;
	Bool findAllyEmergency( Int *allyIndex, Coord3D *hotspot, Coord3D *anchor, Real *threat ) const;
	Real enemyStrengthNear( const Player *victim, const Coord3D *pos, Real radius, Coord3D *centroid ) const;
	Real ownBaseThreat( void ) const;
	Bool isHelperCandidate( Object *obj ) const;
	Bool isHelper( ObjectID id ) const;
	Bool isOnMission( ObjectID id ) const;		///< helper or attacker
	Real missionValue( const std::vector<ObjectID> &ids ) const;
	void startAssist( Int allyIndex, const Coord3D &hotspot, const Coord3D &anchor, Real threat );
	Int  sendHelpers( const Coord3D &hotspot, Real needed, Real minimum, Real *sentValue, AsciiString *why = nullptr,
		Real *available = nullptr, Bool dryRun = FALSE );
	void continueAssist( void );
	void endAssist( const char *reason );
	void pruneHelpers( void );

	// Common enemy
	Bool isValidEnemy( Player *p ) const;
	Bool isAllianceMember( Player *p ) const;
	AICommanderPlayer *allianceLeader( void ) const;
	void chooseCommonEnemy( void );
	void setEnemy( Player *enemy, const char *reason, Real score );

	// Attack mission with pincer
	void updateAttack( void );
	Int  collectIdleForce( std::vector<Object *> *chosen, Real budget, Real *chosenValue, Real *sendable ) const;
	void takeIntoMission( const std::vector<Object *> &units, std::vector<ObjectID> *list, const Coord3D &pos );
	Bool startGather( void );
	void continueGather( void );
	void launchAttack( const char *reason );
	void continueAttack( void );
	void endAttack( const char *reason );
	Bool findObjective( const Player *enemy, const Coord3D &from, Coord3D *pos, ObjectID *id ) const;
	Real forceCentroid( const std::vector<ObjectID> &ids, Coord3D *centroid, const Coord3D *nearPos, Real radius, Real *valueNear ) const;
	void releaseUnits( std::vector<ObjectID> *list );
	const AIPlanResult *planFor( UnsignedInt since ) const;

	// Fight control (micro)
	void updateMicro( void );
	Bool isKnownEnemy( const Object *obj, Player *enemyOf ) const;

	// Raids with transports
	void updateRaid( void );
	Bool startRaidProduction( void );
	void continueRaidAssemble( void );
	void continueRaid( void );
	void continueRaidReturn( void );
	void returnRaid( const char *reason );
	void endRaid( const char *reason );
	Bool findRaidTemplates( const ThingTemplate **transport, Object **transportFactory, const ThingTemplate **rider,
		Object **riderFactory, Int *riders ) const;
	Bool isRaider( ObjectID id ) const;
	Int  ridersAboard( void ) const;

	// Late game: the army keeps growing, upgrades
	void updateLateGame( void );
	void updateFactories( void );
	Bool buyUpgrade( void );
	Int  produceUnits( void );
	void collectArmyCandidates( std::vector<AIPlanCandidate> *out ) const;
	Bool isButtonAllowed( const CommandButton *button, Object *obj ) const;
	Bool isArmyTemplate( const ThingTemplate *t ) const;

	// Sustainable economy
	void updateEconomy( void );
	Real supplyReserve( Int *centers, Int *depots = nullptr ) const;
	const ThingTemplate *findConstructable( KindOfType kind ) const;
	Int  countOwnOrPlanned( const ThingTemplate *t ) const;
	Bool queueBaseBuilding( const ThingTemplate *t, Coord3D *site = nullptr );
	Int  manageHackers( Int wanted );
	Real forecastSupplies( Real reserve, UnsignedInt now );
	const ThingTemplate *findHackerTemplate( void ) const;
	Int  manageSupplyTrucks( Int centers, Int depots );
	Int  queueEconomyUnit( Bool (*match)( const ThingTemplate * ), const char *what, Int have, Int wanted );
	Int  baseReserve( void ) const;
	Int  freeMoney( void ) const;
	Bool growthBuildPending( void ) const;
	void updateTechCapture( void );
	const CommandButton *findCaptureButton( Object *unit, Bool *allowed ) const;

	// Assist allies
	Int							m_assistAlly;							///< player index of the ally being helped (-1 = no mission)
	Coord3D					m_assistPos;							///< hotspot of the mission (centroid of the attackers)
	Coord3D					m_assistAnchor;						///< attacked spot of the ally; the hotspot stays near it
	UnsignedInt			m_lastReinforceFrame;			///< last reinforcement (do not send in small portions)
	Real						m_assistThreat;						///< threat at the last check (sum of build costs)
	UnsignedInt			m_assistStartFrame;				///< start of the mission
	UnsignedInt			m_assistLastThreatFrame;	///< last frame with enemies at the hotspot
	UnsignedInt			m_nextAssistCheckFrame;		///< next situation check
	UnsignedInt			m_assistCooldownFrame;		///< no new mission before this frame
	std::vector<ObjectID> m_helpers;					///< units sent, in dispatch order
	std::vector<KnownObject> m_known;					///< seen enemy buildings and combat units
	ObjectID				m_scout;									///< unit exploring the start positions
	Coord3D					m_scoutTarget;
	UnsignedInt			m_scoutFrame;							///< when the scout got its target
	UnsignedInt			m_startsSeen;							///< bit n: start position n+1 was in sight once
	mutable std::vector<std::pair<Int, Coord3D>> m_startPositions;	///< map start waypoints (slot, place), read once, not saved
	mutable Bool		m_startPositionsRead;
	UnsignedInt			m_rushSeenFrame;					///< last frame enemy combat units were seen in the base early
	UnsignedInt			m_turtleSeenFrame;				///< last frame the fortified pattern held
	UnsignedInt			m_airSeenFrame;						///< last frame the air pattern held
	UnsignedInt			m_stealthSeenFrame;				///< last frame stealthed enemy units were known
	UnsignedInt			m_strikeHeldSince;				///< a ready superweapon waits since this frame (0 = none)
	Int							m_personality;						///< AIPersonality
	struct DefenceSite
	{
		Coord3D pos;
		UnsignedInt frame;		///< ordered at
	};
	std::vector<DefenceSite> m_defenceSites;		///< base defences the turtle ordered itself
	struct OwnOrder
	{
		AsciiString name;			///< template
		Coord3D pos;					///< build list location
		UnsignedInt frame;		///< waiting since (money there)
	};
	std::vector<OwnOrder> m_ownOrders;					///< priority builds the command layer queued, not yet started
	ObjectID				m_markSeen;								///< newest beacon of a human partner already handled
	ObjectID				m_markBeacon;							///< the partner beacon that marks the attack target (INVALID_ID: none)
	Coord3D					m_markPos;
	UnsignedInt			m_markFrame;							///< frame the attack mark was set (0: none)
	Int							m_markEnemy;							///< player index of the enemy at the mark (-1: none known there)
	ObjectID				m_ownBeacon;							///< own beacon for the human partners (INVALID_ID: none)
	Int							m_ownBeaconKind;					///< OwnBeaconKind of the own beacon
	UnsignedInt			m_ownBeaconUntil;					///< frame the own beacon is taken away (0: with its cause)
	Bool						m_modelRush;							///< derived each pass, not saved
	Bool						m_modelTurtle;
	Bool						m_modelAir;
	Bool						m_modelStealth;
	Real						m_enemyAirShare;					///< air value / army value of the remembered enemies, not saved
	Int							m_loggedModel;						///< for the log only, not saved
	std::vector<ObjectID> m_superweaponGuards;	///< recomputed every pass from the game state, not saved
	Int							m_loggedGuardCount;				///< for the log only, not saved
	Bool						m_openingLogged;					///< for the log only, not saved
	Int							m_assistMissions;					///< number of missions (log)

	// Common enemy
	UnsignedInt			m_enemyChosenFrame;				///< since when the current enemy applies (hold time)

	// Attack mission
	CommanderAttackPhase m_attackPhase;
	std::vector<ObjectID> m_attackers;				///< units of the attack, in the order they joined
	Int							m_attackEnemy;						///< player index of the attack target (-1 = none)
	Coord3D					m_stagePos;								///< staging point at the edge of the own base
	Coord3D					m_objectivePos;						///< current target (enemy building)
	ObjectID				m_objectiveID;						///< target building (INVALID_ID = position)
	UnsignedInt			m_attackPhaseFrame;				///< start of the current phase
	Real						m_attackStartValue;				///< combat value when setting out
	UnsignedInt			m_attackReinforceFrame;		///< last reinforcement of the attack
	UnsignedInt			m_attackCooldownFrame;		///< no new attack before this frame
	Bool						m_gatherReady;						///< gathering finished (visible to the ally)
	Int							m_attackMissions;					///< number of attacks (log)

	// Late game
	Int							m_unitsBuilt;						///< units ordered by the command layer (log)
	Int							m_upgradesBought;					///< upgrades bought by the command layer (log)

	AIPlanner				m_planner;

	// Sustainable economy
	UnsignedInt			m_nextEconomyFrame;				///< next economy check
	UnsignedInt			m_nextExpandFrame;				///< earliest next expansion to a new depot
	UnsignedInt			m_nextEconomyReportFrame;	///< next status report (log)
	Int							m_economyOrders;					///< economy orders (log)
	Int							m_economyNeed;						///< build cost of planned, not yet started economy buildings
	ObjectID				m_captureUnit;						///< unit currently capturing a tech building
	ObjectID				m_captureTarget;					///< the tech building
	UnsignedInt			m_captureFrame;						///< start of the capture
	Int							m_captures;							///< captured tech buildings (log)
	Real						m_reserveMark;						///< supply reserve at the start of the rate measurement
	UnsignedInt			m_reserveMarkFrame;
	Real						m_gatherRate;							///< money gathered from the depots per minute (smoothed)
	Bool						m_hackerLead;							///< lead-time hackers are being built
	Int							m_unitReserve;						///< money queueEconomyUnit leaves untouched (set only around one call)
	UnsignedInt			m_objectiveFrame;					///< when the current attack target was chosen
	UnsignedInt			m_forecastPlanFrame;			///< plan last counted for the retreat forecast
	Int							m_forecastBad;						///< plans in a row that forecast a lost fight on site
	Real						m_regrowValue;						///< the next attack needs this combat value ...
	UnsignedInt			m_regrowUntil;						///< ... until this frame

	// Fight control
	UnsignedInt			m_nextMicroFrame;
	UnsignedInt			m_nextSpreadFrame;
	Int							m_microFocus, m_microKite, m_microSpread;	///< orders since the last report (log)
	UnsignedInt			m_nextMicroReportFrame;

	// Raid
	CommanderRaidPhase m_raidPhase;
	ObjectID				m_raidTransport;
	std::vector<ObjectID> m_raiders;						///< riders of the transport
	ObjectID				m_raidTarget;
	UnsignedInt			m_raidPhaseFrame;
	UnsignedInt			m_raidCooldownFrame;
	UnsignedInt			m_raidTargetFrame;					///< when the current raid target was chosen
	Int							m_raidRidersWanted;
	AsciiString			m_raidTransportName;				///< templates ordered for the squad
	AsciiString			m_raidRiderName;
	Int							m_raids;										///< raids started (log)
};

#endif // _AI_COMMANDER_PLAYER_H_
