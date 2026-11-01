/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// AICommanderPlayer.cpp: selectable computer AI, a command layer on top of the classic skirmish AI.
// Behaviours: assist allies, common enemy and pincer attack, late game spending, sustainable
// economy. Overview in AICommanderPlayer.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "formatinto.h"

#include <algorithm>

#include "Common/BuildAssistant.h"
#include "Common/GameEngine.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/Xfer.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameText.h"
#include "GameClient/TerrainVisual.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/AICombatModel.h"
#include "GameLogic/AICommanderPlayer.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/HackInternetAIUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/SupplyWarehouseDockUpdate.h"
#include "GameLogic/Module/TransportContain.h"
#include "GameNetwork/GameInfo.h"

namespace
{
	// Tuning for "assist allies". Distances are world units (10 per pathfinding cell), values are
	// build costs; a Crusader costs 900.
	constexpr UnsignedInt CHECK_INTERVAL			= 2 * LOGICFRAMES_PER_SECOND;		///< situation check every 2 s
	constexpr UnsignedInt ATTACK_RECENT				= 4 * LOGICFRAMES_PER_SECOND;		///< damage counts as recent for 4 s
	constexpr UnsignedInt THREAT_CLEAR				= 12 * LOGICFRAMES_PER_SECOND;	///< 12 s without enemies = repelled
	constexpr UnsignedInt MISSION_TIMEOUT			= 150 * LOGICFRAMES_PER_SECOND;	///< longest mission
	constexpr UnsignedInt COOLDOWN						= 6 * LOGICFRAMES_PER_SECOND;		///< pause after a mission
	constexpr Real HOTSPOT_RADIUS							= 400.0f;		///< radius in which attackers count
	constexpr Real MAX_HELP_DISTANCE					= 2400.0f;	///< units farther away would arrive too late
	constexpr Real OWN_BASE_MARGIN						= 400.0f;		///< own base: build list radius plus this margin
	constexpr Real MIN_THREAT									= 600.0f;		///< below this it is harassment, not an emergency
	constexpr Real HELP_FACTOR								= 1.5f;			///< send 1.5 times the threat ...
	constexpr Real MIN_SEND_FACTOR						= 0.6f;			///< ... but at least 60 %, otherwise nothing
	constexpr Real RESERVE_FRACTION						= 0.3f;			///< 30 % of the own army always stays home
	constexpr Int  MAX_HELPERS								= 40;
	// Keep helpers near the attacked spot instead of chasing the attackers' centroid into their base,
	// and send reinforcements in batches instead of one by one.
	constexpr Real ANCHOR_RADIUS							= 600.0f;		///< hotspot stays at most this far from the attacked spot
	constexpr Real MIN_REINFORCE							= 1500.0f;	///< reinforcements only in batches of at least this value ...
	constexpr UnsignedInt REINFORCE_INTERVAL	= 10 * LOGICFRAMES_PER_SECOND;	///< ... and at most every 10 s
	constexpr Real HOPELESS_FACTOR						= 2.0f;			///< threat > 2 x (helpers + available): give up

	// "Common enemy". The choice is a cost in world units: mean distance from the alliance's
	// bases, a surcharge for strong enemies, a discount for whoever is pressing the alliance.
	constexpr UnsignedInt ENEMY_CHECK_INTERVAL = 5 * LOGICFRAMES_PER_SECOND;	///< like the classic AI
	constexpr UnsignedInt ENEMY_HOLD					= 45 * LOGICFRAMES_PER_SECOND;	///< minimum time to keep an enemy
	constexpr Real STRENGTH_WEIGHT						= 1500.0f;	///< surcharge per "average strength army"
	constexpr Real PRESSURE_WEIGHT						= 3000.0f;	///< largest discount for pressure
	constexpr Real PRESSURE_HALF							= 2000.0f;	///< pressure at which half the discount applies
	constexpr Real PRESSURE_RADIUS						= 450.0f;		///< an enemy this close to an alliance building counts as pressure
	constexpr Real SWITCH_MARGIN							= 1000.0f;	///< switch only if the new enemy is this much cheaper
	constexpr Real URGENT_PRESSURE						= 2000.0f;	///< from this pressure on, switch before the hold time ends

	// Attack mission with pincer.
	constexpr Real ATTACK_MIN_VALUE						= 5000.0f;	///< idle combat value an attack needs ...
	constexpr Int  ATTACK_MIN_UNITS						= 6;				///< ... and this many units
	constexpr Real JOIN_MIN_VALUE							= 2000.0f;	///< if an ally is already gathering, this much is enough ...
	constexpr Int  JOIN_MIN_UNITS							= 3;				///< ... and this many units
	constexpr Int  MAX_ATTACKERS							= 80;
	constexpr Real HOME_RADIUS_MARGIN					= 1200.0f;	///< units within base radius plus this margin count as "at home"
	constexpr Real STAGE_MARGIN								= 250.0f;		///< staging point: this far beyond the edge of the build list
	constexpr Real GATHER_RADIUS							= 450.0f;		///< arrived at the staging point
	constexpr Real GATHER_READY_FRACTION			= 0.7f;			///< share (by value) that must have arrived
	constexpr UnsignedInt GATHER_MAX					= 40 * LOGICFRAMES_PER_SECOND;	///< longest gathering without an ally
	constexpr UnsignedInt GATHER_SYNC_MAX			= 75 * LOGICFRAMES_PER_SECOND;	///< longest wait for the ally
	constexpr UnsignedInt JOIN_WINDOW					= 40 * LOGICFRAMES_PER_SECOND;	///< ally set out this recently: join it
	constexpr Real HUNT_RADIUS								= 700.0f;		///< this close to the target: hunt instead of attack-move
	constexpr Real RETREAT_FRACTION						= 0.35f;		///< below this share of the start value ...
	constexpr Real RETREAT_ODDS								= 1.2f;			///< ... and outnumbered this much on site: retreat
	constexpr Real LOCAL_RADIUS								= 700.0f;		///< radius for the situation at the attack site
	// Planner forecasts (strength ratio = own / enemy under the square law).
	constexpr Real LAUNCH_MIN_RATIO						= 0.8f;			///< alone, set out only if the target's defenders are not stronger than this
	constexpr Real RETREAT_FORECAST_RATIO			= 0.7f;			///< retreat when the fight on site is forecast this badly ...
	constexpr Int  RETREAT_FORECAST_PLANS			= 2;				///< ... in this many plans in a row ...
	constexpr Real RETREAT_FORECAST_LOSS			= 0.85f;		///< ... and the force is below this share of its start value ...
	constexpr Real RETREAT_HOPELESS_RATIO			= 0.4f;			///< ... unless the forecast is this bad
	// After a broken-off attack the next one waits until the force has grown, otherwise the same force
	// runs into the same defence again.
	constexpr Real REGROW_FACTOR							= 1.3f;			///< next attack needs this times the last start value ...
	constexpr UnsignedInt REGROW_TIME					= 180 * LOGICFRAMES_PER_SECOND;	///< ... for at most this long
	constexpr UnsignedInt OBJECTIVE_HOLD			= 20 * LOGICFRAMES_PER_SECOND;	///< keep a target at least this long

	// Fight control.
	constexpr UnsignedInt MICRO_INTERVAL			= LOGICFRAMES_PER_SECOND / 2;	///< twice a second
	constexpr Real MICRO_ENEMY_MARGIN					= 600.0f;		///< enemies this far around the own fighters count
	constexpr Real FOCUS_APPROACH							= 100.0f;		///< a target this far beyond the weapon's reach may be taken
	constexpr Real FOCUS_KILL_SECONDS					= 4.0f;			///< shooters per target: enough to kill it in this time
	constexpr Real SPLASH_MIN									= 30.0f;		///< area damage from this radius on makes clumps dangerous
	constexpr Real SPLASH_SHARE								= 0.3f;			///< ... if such weapons deal this share of the enemy damage nearby
	constexpr Real SPLASH_SPACING_FACTOR			= 1.5f;			///< spacing = largest damage radius times this
	constexpr Real SUPERWEAPON_SPACING				= 150.0f;		///< spacing while an enemy superweapon is (nearly) ready
	constexpr UnsignedInt SUPERWEAPON_WARNING	= 15 * LOGICFRAMES_PER_SECOND;	///< "nearly ready"
	constexpr UnsignedInt SPREAD_INTERVAL			= 10 * LOGICFRAMES_PER_SECOND;	///< spread a group at most this often
	constexpr Real GROUP_RADIUS								= 500.0f;		///< enemies this close to a group's centre threaten it

	// Raids.
	constexpr UnsignedInt RAID_START					= 2 * 60 * LOGICFRAMES_PER_SECOND;	///< first raid squad from minute 2
	constexpr UnsignedInt RAID_COOLDOWN				= 45 * LOGICFRAMES_PER_SECOND;	///< pause at home between raids
	constexpr UnsignedInt RAID_ASSEMBLE_MAX		= 120 * LOGICFRAMES_PER_SECOND;	///< build and board within this time
	constexpr UnsignedInt RAID_MAX						= 180 * LOGICFRAMES_PER_SECOND;	///< longest raid
	constexpr UnsignedInt RAID_TARGET_HOLD		= 15 * LOGICFRAMES_PER_SECOND;	///< keep a raid target at least this long
	constexpr Real RAID_RETREAT_HEALTH				= 0.5f;			///< fall back below this share of the transport's health
	constexpr Int  RAID_KEEP_MONEY						= 500;			///< the squad must leave this much money
	constexpr Int  RAID_MIN_SLOTS							= 3;				///< transports with fewer slots are no raid vehicles
	constexpr Int  RAID_MAX_RIDERS						= 5;
	constexpr Real RAID_CLAIM_RANGE						= 800.0f;		///< new squad units this far beyond the base edge are taken
	constexpr Real RAID_HOME_MARGIN						= 200.0f;		///< home: within the base radius plus this

	// Superweapon target weights (multiply the build cost of a known enemy object in the radius).
	constexpr Real STRIKE_APPROACH_RANGE			= 1500.0f;	///< units this close beyond the own base edge are approaching
	constexpr Real STRIKE_APPROACH_FACTOR			= 1.5f;
	constexpr Real STRIKE_DEFENCE_RANGE				= 1500.0f;	///< armed buildings this close to the own attack target
	constexpr Real STRIKE_DEFENCE_FACTOR			= 1.5f;
	constexpr Real STRIKE_SUPERWEAPON_FACTOR	= 2.0f;
	constexpr Real STRIKE_FRIENDLY_FACTOR			= 2.0f;		///< own and allied objects in the radius count this much against
	constexpr Real BASE_RECALL_THREAT					= 3000.0f;	///< a threat at home this strong recalls the attack
	constexpr Real DEFENCE_RECALL_THREAT			= 1500.0f;	///< with base defence: from this threat on, if the units at home are weaker
	constexpr UnsignedInt ATTACK_TIMEOUT			= 480 * LOGICFRAMES_PER_SECOND;	///< longest attack
	constexpr UnsignedInt ATTACK_COOLDOWN			= 20 * LOGICFRAMES_PER_SECOND;	///< pause after an attack
	constexpr Real ATTACK_REINFORCE_VALUE			= 2500.0f;	///< attack reinforcements in batches of at least this value ...
	constexpr UnsignedInt ATTACK_REINFORCE_INTERVAL = 20 * LOGICFRAMES_PER_SECOND;	///< ... at most every 20 s

	// Late game: the command layer spends money above the threshold itself.
	constexpr UnsignedInt LATE_GAME_START			= 6 * 60 * LOGICFRAMES_PER_SECOND;	///< before this the classic AI builds its base
	constexpr Int  LATE_PRODUCE_MONEY					= 2000;			///< from this balance on the command layer buys ...
	constexpr Int  LATE_KEEP_MONEY						= 1000;			///< ... but never below this rest (left for the classic AI)
	constexpr Int  LATE_INFANTRY_MONEY					= 4000;			///< infantry only from this balance on
	constexpr Int  LATE_SAME_TYPE_CAP					= 6;				///< from this many units of one template on, it counts only one third

	// Opening after human play: against the AI humans build the first war factory after about 1.5
	// minutes and have 3 to 6 after 10, buy units from the start and attack from minute 2 to 3 on.
	// Only for Brutal (the pace without a unit limit).
	constexpr UnsignedInt FACTORY_SECOND_START	= 150 * LOGICFRAMES_PER_SECOND;	///< second war factory from 2:30 ...
	constexpr UnsignedInt FACTORY_THIRD_START	= 300 * LOGICFRAMES_PER_SECOND;	///< ... a third from 5:00
	constexpr Int  FACTORY_KEEP_MONEY					= 300;			///< money left after paying the factory
	constexpr UnsignedInt EARLY_PRODUCTION_START = 180 * LOGICFRAMES_PER_SECOND;	///< own unit purchases from minute 3
	constexpr UnsignedInt EARLY_ATTACK_UNTIL		= 8 * 60 * LOGICFRAMES_PER_SECOND;	///< smaller attacks until minute 8 ...
	constexpr Real EARLY_ATTACK_MIN_VALUE			= 2500.0f;	///< ... from this idle combat value ...
	constexpr Int  EARLY_ATTACK_MIN_UNITS			= 4;				///< ... and this many units

	// Sustainable economy. Money values are in build cost units.
	constexpr UnsignedInt ECONOMY_INTERVAL		= 5 * LOGICFRAMES_PER_SECOND;		///< economy check every 5 s
	constexpr Real SUPPLY_NEAR_RADIUS					= 700.0f;		///< supply depots this close to an own supply center count
	constexpr Real EXPAND_RESERVE							= 12000.0f;	///< below this, open up a new depot ...
	constexpr Int  EXPAND_MIN_CASH							= 6000;			///< ... that still holds at least this much
	constexpr UnsignedInt EXPAND_INTERVAL			= 90 * LOGICFRAMES_PER_SECOND;	///< at most one expansion every 90 s
	constexpr UnsignedInt ECONOMY_START				= 8 * 60 * LOGICFRAMES_PER_SECOND;	///< before this the classic AI builds its base and depots
	constexpr Real PERMANENT_RESERVE					= 25000.0f;	///< below this, build permanent income ...
	constexpr UnsignedInt PERMANENT_START			= 20 * 60 * LOGICFRAMES_PER_SECOND;	///< ... at the latest from minute 20
	constexpr Real DEPLETED_RESERVE						= 5000.0f;		///< below this the depots count as depleted: more permanent income
	// Hackers (China) earn money independent of the depots, inside the internet center (the free slots)
	// and outside it (anywhere); the target grows with the time and with how weak the depots are.
	constexpr UnsignedInt HACKER_START				= 4 * 60 * LOGICFRAMES_PER_SECOND;	///< first hackers from minute 4
	constexpr UnsignedInt HACKER_MORE_START		= 10 * 60 * LOGICFRAMES_PER_SECOND;	///< larger base target from minute 10
	constexpr Int  HACKERS_BASE								= 3;				///< base target from HACKER_START ...
	constexpr Int  HACKERS_BASE_LATE					= 5;				///< ... and from HACKER_MORE_START
	constexpr Int  HACKERS_DEPLETING_EXTRA		= 3;				///< supplies run low: more ...
	constexpr Int  HACKERS_DEPLETED_EXTRA			= 3;				///< ... depots depleted: even more
	constexpr Int  HACKERS_FEW_DEPOTS_EXTRA		= 2;				///< at most one depot with content left in reach
	constexpr Int  HACKERS_NO_CENTER_EXTRA		= 4;				///< no supply center at all
	constexpr Int  HACKERS_MAX								= 14;
	// Lead time: hackers rank up only by hacking (experience per payment), and the elite rank pays
	// much more. They must be at work early enough to have ranked up when the depots run dry.
	constexpr UnsignedInt RATE_WINDOW					= 60 * LOGICFRAMES_PER_SECOND;	///< gather rate measured over a minute
	constexpr UnsignedInt HACKER_LEAD_START		= 3 * 60 * LOGICFRAMES_PER_SECOND;	///< lead-time hackers from minute 3
	constexpr Real HACKER_LEAD_MARGIN					= 2.0f;		///< minutes added to the time a hacker needs to reach elite
	constexpr Real PERMANENT_LEAD							= 5.0f;		///< permanent income buildings this many minutes before the depots run dry
	constexpr Real NO_FORECAST								= 999.0f;	///< minutes left when nothing is gathered
	// Growth: spread out early and take as much income as possible -- further depots, tech
	// buildings (oil derricks), hackers, drop zones, black markets -- not only replace the depots
	// once they run dry. The money left after paying is the only brake.
	constexpr UnsignedInt GROWTH_EXPAND_START		= 90 * LOGICFRAMES_PER_SECOND;	///< open up further depots from 1:30 ...
	constexpr UnsignedInt GROWTH_EXPAND_INTERVAL	= 45 * LOGICFRAMES_PER_SECOND;	///< ... at most one per 45 s ...
	constexpr Int  GROWTH_EXPAND_MIN_CASH				= 8000;			///< ... at depots holding at least this much ...
	constexpr Int  GROWTH_EXPAND_RESERVE				= 500;			///< ... if this much money is left after paying the centre
	constexpr Real GROWTH_SAFE_RADIUS						= 600.0f;		///< no enemy with ...
	constexpr Real GROWTH_SAFE_THREAT						= 600.0f;		///< ... this much combat value this close to the depot
	constexpr UnsignedInt GROWTH_PERMANENT_START	= 3 * 60 * LOGICFRAMES_PER_SECOND;	///< first permanent income building from minute 3 ...
	constexpr Int  GROWTH_PERMANENT_RESERVE		= 800;			///< ... if this much money is left after paying it
	constexpr UnsignedInt GROWTH_SECOND_START		= 6 * 60 * LOGICFRAMES_PER_SECOND;	///< a second one from minute 6 ...
	constexpr Int  GROWTH_SECOND_RESERVE				= 1500;			///< ... with this much left
	constexpr UnsignedInt HACKER_INVEST_START		= 60 * LOGICFRAMES_PER_SECOND;	///< hackers as investment from minute 1:
	constexpr Int  HACKER_INVEST_BASE					= 2;				///< this many ...
	constexpr UnsignedInt HACKER_INVEST_STEP		= 60 * LOGICFRAMES_PER_SECOND;	///< ... plus one every minute ...
	constexpr Int  HACKERS_INVEST_MAX					= 20;				///< ... up to this many ...
	constexpr Real HACKER_PAYBACK_MAX					= 12.0f;		///< ... while one pays back its cost within this many minutes at the lowest rank
	constexpr Int  HACKERS_PER_CENTER					= 8;				///< an internet centre holds about this many
	constexpr Real INVEST_MIN_RATIO						= 0.8f;			///< growth waits while the own army is forecast this much weaker
	constexpr Int  TRUCKS_PER_CENTER					= 3;				///< supply trucks: at most this many per supply center ...
	constexpr Int  TRUCKS_PER_DEPOT						= 2;				///< ... and two per depot with content, plus one
	constexpr Int  ECONOMY_KEEP_MONEY					= 300;			///< economy investments may go lower
	constexpr UnsignedInt CAPTURE_START				= 60 * LOGICFRAMES_PER_SECOND;	///< capture tech buildings (oil derricks) from minute 1
	constexpr Real CAPTURE_RANGE							= 4000.0f;	///< only this close to the own base
	constexpr UnsignedInt CAPTURE_START_SLOW	= 180 * LOGICFRAMES_PER_SECOND;	///< without early growth: from minute 3 ...
	constexpr Real CAPTURE_RANGE_SLOW					= 2500.0f;	///< ... this close, the nearest first
	constexpr Real CAPTURE_SAFE_RADIUS				= 500.0f;		///< no enemy with ...
	constexpr Real CAPTURE_SAFE_THREAT				= 300.0f;		///< ... this much combat value within this radius
	constexpr UnsignedInt CAPTURE_TIMEOUT			= 90 * LOGICFRAMES_PER_SECOND;	///< longest capture

	// Opening by starting cash. Standard (from LOW_CASH_LIMIT to below RICH_CASH_LIMIT) keeps the tuning
	// constants unchanged. Low: money thresholds of the late game lower, supply trucks earlier. Rich (Brutal
	// only for production and factories): thresholds scale with the cash, units from OPENING_RICH_PRODUCTION,
	// further war factories from the first and second minute.
	constexpr Int LOW_CASH_LIMIT							= 7500;
	constexpr Int RICH_CASH_LIMIT							= 20000;
	constexpr Real LOW_MONEY_SCALE						= 0.75f;
	constexpr UnsignedInt LOW_TRUCK_START			= 60 * LOGICFRAMES_PER_SECOND;
	constexpr UnsignedInt OPENING_RICH_PRODUCTION	= 60 * LOGICFRAMES_PER_SECOND;
	constexpr UnsignedInt RICH_FACTORY_SECOND	= 60 * LOGICFRAMES_PER_SECOND;
	constexpr UnsignedInt RICH_FACTORY_THIRD	= 120 * LOGICFRAMES_PER_SECOND;

	enum class CashClass { LOW, STANDARD, RICH };

	Int startingCash( void )
	{
		return TheGameInfo ? static_cast<Int>( TheGameInfo->getStartingCash().countMoney() ) : 10000;
	}

	CashClass cashClass( void )
	{
		const Int cash = startingCash();
		if (cash <= LOW_CASH_LIMIT)
			return CashClass::LOW;
		return (cash >= RICH_CASH_LIMIT) ? CashClass::RICH : CashClass::STANDARD;
	}

	// Multiplies the money thresholds of the late game (LATE_*_MONEY).
	Real cashScale( void )
	{
		switch (cashClass())
		{
			case CashClass::LOW:	return LOW_MONEY_SCALE;
			case CashClass::RICH:	return std::clamp( static_cast<Real>( startingCash() ) / 10000.0f, 2.0f, 5.0f );
			default:							return 1.0f;
		}
	}

	Int scaledMoney( Int money )
	{
		return static_cast<Int>( static_cast<Real>( money ) * cashScale() + 0.5f );
	}

	// Memory (no map knowledge): enemy units count for this long after they were last seen; buildings until
	// the AI sees that they are gone. Scouting: from SCOUT_START one cheap unit visits the start positions
	// nobody has looked at yet, at most SCOUT_TIMEOUT per target.
	constexpr UnsignedInt UNIT_MEMORY					= 180 * LOGICFRAMES_PER_SECOND;
	constexpr UnsignedInt SCOUT_START					= 30 * LOGICFRAMES_PER_SECOND;
	constexpr UnsignedInt SCOUT_TIMEOUT				= 150 * LOGICFRAMES_PER_SECOND;
	constexpr Int SCOUT_MIN_HOME							= 3;		///< a scout only goes when this many idle units are at home

	// Enemy model (from the memory). Rush: enemy combat units in the own base before RUSH_UNTIL; for
	// RUSH_HOLD afterwards RUSH_RESERVE more of the army stays home and growth pauses. Turtle: known armed
	// enemy buildings worth TURTLE_RATIO times his seen army (at least TURTLE_MIN_VALUE); attacks wait for
	// TURTLE_ATTACK_SCALE times the force. Air: aircraft are AIR_SHARE of his seen army; units that hit
	// air count AIR_PREFERENCE times when buying, and while the own anti-air value is below his air share
	// (at most AIR_DEFENSE_MAX) a factory buys anti-air first. Stealth: known enemy units that can stealth
	// worth STEALTH_MIN_VALUE; one stealth detecting building is built. Turtle, air and stealth hold for
	// MODEL_HOLD after the pattern was last seen, so that units coming and going in the memory do not flip them.
	constexpr UnsignedInt RUSH_UNTIL					= 8 * 60 * LOGICFRAMES_PER_SECOND;
	constexpr UnsignedInt RUSH_HOLD						= 180 * LOGICFRAMES_PER_SECOND;
	constexpr Real RUSH_RESERVE								= 0.2f;
	constexpr Real TURTLE_RATIO								= 1.5f;
	constexpr Real TURTLE_MIN_VALUE						= 2000.0f;
	constexpr Real TURTLE_ATTACK_SCALE				= 1.5f;
	constexpr Real AIR_SHARE									= 0.25f;
	constexpr Real AIR_PREFERENCE							= 2.0f;
	constexpr Real AIR_DEFENSE_MAX						= 0.5f;
	constexpr Real STEALTH_MIN_VALUE					= 300.0f;
	constexpr Int STEALTH_DETECTOR_UNITS			= 2;		///< units that detect stealth bought while stealth is known
	constexpr UnsignedInt MODEL_HOLD					= 120 * LOGICFRAMES_PER_SECOND;

	// Superweapon limit: the one own superweapon is the key building; idle combat units at home worth up to
	// this much stay beside it, the nearest first, and attacks leave them out.
	constexpr Real SUPERWEAPON_GUARD_VALUE		= 1500.0f;
	constexpr Real SUPERWEAPON_GUARD_RADIUS		= 200.0f;

	// Start protection: nobody can attack before it ends. Until GRACE_ARMY_LEAD before the end the money
	// goes into economy and tech (growth acts as if the end had come already), then into the army; the
	// first attack gathers GRACE_GATHER_LEAD before the end and sets out GRACE_LAUNCH_LEAD before it, so
	// it arrives when the protection falls.
	constexpr UnsignedInt GRACE_ARMY_LEAD			= 3 * 60 * LOGICFRAMES_PER_SECOND;
	constexpr UnsignedInt GRACE_FACTORY_LEAD	= 60 * LOGICFRAMES_PER_SECOND;	///< more war factories this much before the army
	constexpr UnsignedInt GRACE_GATHER_LEAD		= 90 * LOGICFRAMES_PER_SECOND;
	constexpr UnsignedInt GRACE_LAUNCH_LEAD		= 30 * LOGICFRAMES_PER_SECOND;

	UnsignedInt graceLeft( void )
	{
		const UnsignedInt end = TheGameLogic->getGraceEndFrame();
		const UnsignedInt now = TheGameLogic->getFrame();
		return end > now ? end - now : 0;
	}

	// Throttles of the command layer per difficulty. Brutal uses the tuning constants above unchanged;
	// Easy and Medium react slower, keep more at home, attack with bigger and rarer forces, start the
	// late game later, order fewer units at once and invest less in the economy.
	struct Pace
	{
		UnsignedInt checkInterval;		///< frames between two passes of the command layer (reaction time)
		Real reserveFraction;				///< share of the army that stays home
		Real attackScale;						///< multiplies the minimum value and unit count of an attack
		Real cooldownScale;					///< multiplies the pause after a mission
		UnsignedInt lateStart;			///< late game spending from this frame
		Int lateMaxUnits;						///< units ordered per pass in the late game (0 = no limit)
		Bool assistDuringAttack;		///< may help an ally while an own attack runs (concurrent missions)
		Real economyScale;					///< multiplies the hacker target
	};

	const Pace &difficultyPace( GameDifficulty difficulty )
	{
		static const Pace hard = { CHECK_INTERVAL, RESERVE_FRACTION, 1.0f, 1.0f, LATE_GAME_START, 0, TRUE, 1.0f };
		static const Pace normal = { 4 * LOGICFRAMES_PER_SECOND, 0.45f, 1.3f, 1.5f, 7 * 60 * LOGICFRAMES_PER_SECOND, 3, TRUE, 0.75f };
		static const Pace easy = { 8 * LOGICFRAMES_PER_SECOND, 0.6f, 1.8f, 2.5f, 10 * 60 * LOGICFRAMES_PER_SECOND, 2, FALSE, 0.5f };
		switch (difficulty)
		{
			case DIFFICULTY_EASY:		return easy;
			case DIFFICULTY_NORMAL:	return normal;
			default:								return hard;
		}
	}

	// The personality shifts the throttles of the difficulty: aggressive keeps less at home and attacks
	// earlier and more often, the technician invests (hackers) and attacks later, the turtle keeps much at
	// home and attacks only with a large force (and builds base defences, updateDefences).
	Pace paceFor( GameDifficulty difficulty, Int personality )
	{
		Pace p = difficultyPace( difficulty );
		switch (personality)
		{
			case AI_PERSONALITY_AGGRESSIVE:
				p.reserveFraction = std::max( p.reserveFraction - 0.1f, 0.05f );
				p.attackScale *= 0.75f;
				p.cooldownScale *= 0.6f;
				break;
			case AI_PERSONALITY_TECHNICIAN:
				p.attackScale *= 1.25f;
				p.cooldownScale *= 1.2f;
				p.economyScale *= 1.5f;
				break;
			case AI_PERSONALITY_TURTLE:
				p.reserveFraction = std::min( p.reserveFraction + 0.15f, 0.8f );
				p.attackScale *= 1.4f;
				p.cooldownScale *= 1.5f;
				break;
			default:
				break;
		}
		return p;
	}

	constexpr Int TURTLE_DEFENCES							= 4;		///< base defence buildings of the turtle
	constexpr Real DEFENCE_SITE_RADIUS				= 40.0f;	///< a defence this near to an ordered site holds it
	constexpr UnsignedInt OWN_ORDER_TIMEOUT		= 60 * LOGICFRAMES_PER_SECOND;	///< an own order not started by then (money there) is withdrawn
	constexpr Real TECHNICIAN_UPGRADE_MONEY		= 0.5f;	///< the technician buys upgrades with this share of LATE_KEEP_MONEY left

	// Beacons with human partners.
	constexpr Real MARK_HELP_RADIUS					= 500.0f;		///< a partner beacon this near to one of his buildings asks for help
	constexpr Real MARK_RADIUS							= 350.0f;		///< known enemy objects this near to a beacon are its target
	constexpr UnsignedInt MARK_TIME					= 180 * LOGICFRAMES_PER_SECOND;	///< an attack beacon counts this long
	constexpr UnsignedInt RETREAT_BEACON_TIME	= 20 * LOGICFRAMES_PER_SECOND;	///< the own retreat beacon stays this long
	constexpr Real MARK_REACHED							= 200.0f;		///< attackers this near to an empty marked spot: done
	constexpr Real MARK_START_RADIUS				= 500.0f;		///< nothing known at the mark: the enemy whose start position is this near
	constexpr Real ASK_HELP_THREAT					= 2.0f * MIN_THREAT;	///< own base threat from which the AI asks its partners for help

	// Hacker economics from the data of a hacker template (HackInternetAIUpdate module): money per
	// minute at each rank and the frames a new hacker needs from the factory to the elite rank.
	struct HackerEconomy
	{
		Bool valid;
		Real incomePerMinute[LEVEL_COUNT];
		UnsignedInt framesToElite;
	};

	HackerEconomy hackerEconomy( const ThingTemplate *t, const Player *player, Bool inCenter )
	{
		HackerEconomy e;
		e.valid = FALSE;
		e.framesToElite = 0;
		for (Int r = 0; r < LEVEL_COUNT; ++r)
			e.incomePerMinute[r] = 0.0f;
		if (t == nullptr)
			return e;
		const HackInternetAIUpdateModuleData *data = nullptr;
		const ModuleInfo &modules = t->getBehaviorModuleInfo();
		for (Int i = 0; i < modules.getCount() && data == nullptr; ++i)
		{
			if (modules.getNthName( i ).compareNoCase( "HackInternetAIUpdate" ) == 0)
				data = static_cast<const HackInternetAIUpdateModuleData *>( modules.getNthData( i ) );
		}
		if (data == nullptr)
			return e;
		const UnsignedInt delay = (inCenter && data->m_cashUpdateDelayFast > 0) ? data->m_cashUpdateDelayFast : data->m_cashUpdateDelay;
		if (delay == 0)
			return e;
		const Real paymentsPerMinute = static_cast<Real>(60 * LOGICFRAMES_PER_SECOND) / static_cast<Real>(delay);
		e.incomePerMinute[LEVEL_REGULAR] = static_cast<Real>(data->m_regularCashAmount) * paymentsPerMinute;
		e.incomePerMinute[LEVEL_VETERAN] = static_cast<Real>(data->m_veteranCashAmount) * paymentsPerMinute;
		e.incomePerMinute[LEVEL_ELITE] = static_cast<Real>(data->m_eliteCashAmount) * paymentsPerMinute;
		e.incomePerMinute[LEVEL_HEROIC] = static_cast<Real>(data->m_heroicCashAmount) * paymentsPerMinute;
		UnsignedInt payments = 0;
		const Int xpElite = t->getExperienceRequired( LEVEL_ELITE );
		if (data->m_xpPerCashUpdate > 0 && xpElite > 0)
			payments = (static_cast<UnsignedInt>(xpElite) + data->m_xpPerCashUpdate - 1) / data->m_xpPerCashUpdate;
		const Int buildFrames = t->calcTimeToBuild( player );
		e.framesToElite = static_cast<UnsignedInt>(buildFrames > 0 ? buildFrames : 0) + data->m_unpackTime + payments * delay;
		e.valid = TRUE;
		return e;
	}

	Real distSqr2D( const Coord3D *a, const Coord3D *b )
	{
		Real dx = a->x - b->x;
		Real dy = a->y - b->y;
		return dx*dx + dy*dy;
	}

	// Economy traits. Zero Hour marks the buildings with FS_ kinds and hackers with MONEY_HACKER;
	// the Generals data lacks these kinds. Fallback there: supply centers are buildings with
	// CASH_GENERATOR (like AIPlayer::findSupplyCenter), hackers have the HackInternet behaviour, and
	// drop zone, black market and internet center are recognized by the template name.
	Bool templateNameHas( const ThingTemplate *t, const char *part )
	{
		return t != nullptr && strstr( t->getName().str(), part ) != nullptr;
	}

	Bool isHackerTemplate( const ThingTemplate *t )
	{
		return t->isKindOf( KINDOF_MONEY_HACKER ) || templateNameHas( t, "Hacker" );
	}

	Bool isHarvesterTemplate( const ThingTemplate *t )
	{
		return t->isKindOf( KINDOF_HARVESTER );
	}

	Bool isHackerObject( const Object *obj )
	{
		if (obj->isKindOf( KINDOF_MONEY_HACKER ))
			return TRUE;
		const AIUpdateInterface *ai = obj->getAIUpdateInterface();
		return ai != nullptr && ai->getHackInternetAIInterface() != nullptr;
	}

	/// Part of the new AI switched off with -aiOhne.
	Bool partOff( UnsignedInt part )
	{
		return (TheGlobalData->m_aiPartsOff & part) != 0;
	}

	Bool isEconomyKind( const ThingTemplate *t, KindOfType kind )
	{
		if (t == nullptr)
			return FALSE;
		if (t->isKindOf( kind ))
			return TRUE;
		switch (kind)
		{
			case KINDOF_FS_SUPPLY_CENTER:		return t->isKindOf( KINDOF_STRUCTURE ) && t->isKindOf( KINDOF_CASH_GENERATOR );
			case KINDOF_FS_SUPPLY_DROPZONE:	return templateNameHas( t, "SupplyDropZone" );
			case KINDOF_FS_BLACK_MARKET:		return templateNameHas( t, "BlackMarket" );
			case KINDOF_FS_INTERNET_CENTER:	return templateNameHas( t, "InternetCenter" );
			default:												return FALSE;
		}
	}

	// Mobile combat unit: infantry or vehicle with a weapon; no dozer, harvester or drone.
	Bool isMobileCombatUnit( const Object *obj )
	{
		if (obj->isEffectivelyDead() || obj->isKindOf( KINDOF_STRUCTURE ))
			return FALSE;
		if (!obj->isKindOf( KINDOF_INFANTRY ) && !obj->isKindOf( KINDOF_VEHICLE ))
			return FALSE;
		if (obj->isKindOf( KINDOF_DOZER ) || obj->isKindOf( KINDOF_HARVESTER ) || obj->isKindOf( KINDOF_DRONE )
			|| obj->isKindOf( KINDOF_IGNORED_IN_GUI ) || obj->isKindOf( KINDOF_PROJECTILE ))
			return FALSE;
		if (isHackerObject( obj ))
			return FALSE;	// hackers earn money and stay out of combat
		return obj->isAbleToAttack();
	}

	// Visible to the AI: not stealthed, or detected (it sees no more than a player would).
	Bool isVisibleToAI( const Object *obj )
	{
		return !obj->testStatus( OBJECT_STATUS_STEALTHED ) || obj->testStatus( OBJECT_STATUS_DETECTED );
	}

	// Combat value of a unit: its build cost (at least 50, so free units count too).
	Real unitValue( const Object *obj )
	{
		Int cost = obj->getTemplate()->friend_getBuildCost();
		return static_cast<Real>(cost > 50 ? cost : 50);
	}

	struct HelperCandidate
	{
		Object *obj;
		Real value;
		Real distSqr;
		Bool idle;
	};

	// Total order without addresses: idle units first, then the nearest, then ObjectID.
	bool helperBefore( const HelperCandidate &a, const HelperCandidate &b )
	{
		if (a.idle != b.idle)
			return a.idle;
		if (a.distSqr != b.distSqr)
			return a.distSqr < b.distSqr;
		return a.obj->getID() < b.obj->getID();
	}

	// Picture per player for choosing the enemy (one pass over the object list).
	struct PlayerPicture
	{
		Real army;					///< combat value of the visible mobile combat units
		Real structX, structY;	///< sum of building positions
		Int structures;
		Real unitX, unitY;		///< sum of unit positions
		Int units;
		Real pressure;			///< combat value of this player near alliance buildings
	};

	Bool pictureCenter( const PlayerPicture &p, Coord3D *pos )
	{
		pos->zero();
		if (p.structures > 0)
		{
			pos->x = p.structX / static_cast<Real>(p.structures);
			pos->y = p.structY / static_cast<Real>(p.structures);
			return TRUE;
		}
		if (p.units > 0)
		{
			pos->x = p.unitX / static_cast<Real>(p.units);
			pos->y = p.unitY / static_cast<Real>(p.units);
			return TRUE;
		}
		return FALSE;
	}

	void addToGroup( AIGroup **group, Object *obj )
	{
		if (*group == nullptr)
			*group = TheAI->createGroup();
		if (*group)
			(*group)->add( obj );
	}
}

// ------------------------------------------------------------------------------------------------
AICommanderPlayer::AICommanderPlayer( Player *p ) : AISkirmishPlayer( p ),
	m_assistAlly( -1 ),
	m_assistThreat( 0.0f ),
	m_assistStartFrame( 0 ),
	m_assistLastThreatFrame( 0 ),
	m_nextAssistCheckFrame( 0 ),
	m_assistCooldownFrame( 0 ),
	m_assistMissions( 0 ),
	m_enemyChosenFrame( 0 ),
	m_attackPhase( CommanderAttackPhase::NONE ),
	m_attackEnemy( -1 ),
	m_objectiveID( INVALID_ID ),
	m_attackPhaseFrame( 0 ),
	m_attackStartValue( 0.0f ),
	m_attackReinforceFrame( 0 ),
	m_attackCooldownFrame( 0 ),
	m_gatherReady( FALSE ),
	m_attackMissions( 0 ),
	m_unitsBuilt( 0 ),
	m_upgradesBought( 0 ),
	m_nextEconomyFrame( 0 ),
	m_nextExpandFrame( 0 ),
	m_nextEconomyReportFrame( 0 ),
	m_economyOrders( 0 ),
	m_economyNeed( 0 ),
	m_captureUnit( INVALID_ID ),
	m_captureTarget( INVALID_ID ),
	m_captureFrame( 0 ),
	m_captures( 0 ),
	m_reserveMark( 0.0f ),
	m_reserveMarkFrame( 0 ),
	m_gatherRate( 0.0f ),
	m_hackerLead( FALSE ),
	m_unitReserve( 0 ),
	m_objectiveFrame( 0 ),
	m_forecastPlanFrame( 0 ),
	m_forecastBad( 0 ),
	m_regrowValue( 0.0f ),
	m_regrowUntil( 0 ),
	m_nextMicroFrame( 0 ),
	m_nextSpreadFrame( 0 ),
	m_microFocus( 0 ),
	m_microKite( 0 ),
	m_microSpread( 0 ),
	m_nextMicroReportFrame( 0 ),
	m_raidPhase( CommanderRaidPhase::NONE ),
	m_raidTransport( INVALID_ID ),
	m_raidTarget( INVALID_ID ),
	m_raidPhaseFrame( 0 ),
	m_raidCooldownFrame( 0 ),
	m_raidTargetFrame( 0 ),
	m_raidRidersWanted( 0 ),
	m_raids( 0 )
{
	m_loggedGuardCount = 0;
	m_openingLogged = FALSE;
	m_scout = INVALID_ID;
	m_scoutTarget.zero();
	m_scoutFrame = 0;
	m_startsSeen = 0;
	m_startPositionsRead = FALSE;
	m_rushSeenFrame = 0;
	m_turtleSeenFrame = 0;
	m_airSeenFrame = 0;
	m_stealthSeenFrame = 0;
	m_strikeHeldSince = 0;
	m_personality = AI_PERSONALITY_STANDARD;
	m_markSeen = INVALID_ID;
	m_markBeacon = INVALID_ID;
	m_markPos.zero();
	m_markFrame = 0;
	m_markEnemy = -1;
	m_ownBeacon = INVALID_ID;
	m_ownBeaconKind = OWN_BEACON_ATTACK;
	m_ownBeaconUntil = 0;
	m_modelRush = FALSE;
	m_modelTurtle = FALSE;
	m_modelAir = FALSE;
	m_modelStealth = FALSE;
	m_enemyAirShare = 0.0f;
	m_loggedModel = 0;
	m_assistPos.zero();
	m_assistAnchor.zero();
	m_stagePos.zero();
	m_objectivePos.zero();
	m_lastReinforceFrame = 0;
	// Spread the players' checks over different frames (load only; order does not matter).
	m_nextAssistCheckFrame = TheGameLogic->getFrame() + CHECK_INTERVAL + static_cast<UnsignedInt>(p->getPlayerIndex());
	m_planner.init( p );
	if (TheGlobalData->m_aiPartsOff != 0)
	{
		char line[96];
		WWFormat::Into(line, "KIOHNE {:d} spieler {:d} teile {:d}", TheGameLogic->getFrame(), p->getPlayerIndex(), static_cast<Int>( TheGlobalData->m_aiPartsOff ));	// negative: an unknown name
		GameEngine_RunnerLog( line );
	}
}

AICommanderPlayer::~AICommanderPlayer()
{
}

AICommanderPlayer *AICommanderPlayer::commanderOf( Player *p )
{
	if (p == nullptr)
		return nullptr;
	return dynamic_cast<AICommanderPlayer *>( p->getAIPlayer() );
}

// ------------------------------------------------------------------------------------------------
void AICommanderPlayer::update( void )
{
	AISkirmishPlayer::update();
	if (m_planner.isSnapshotDue())
	{
		std::vector<AIPlanCandidate> candidates;
		collectArmyCandidates( &candidates );
		Player *enemy = m_player->peekCurrentEnemy();
		std::vector<UnsignedInt> mission;
		if (m_attackPhase != CommanderAttackPhase::NONE)
		{
			for (size_t i = 0; i < m_attackers.size(); ++i)
				mission.push_back( static_cast<UnsignedInt>( m_attackers[i] ) );
		}
		std::vector<UnsignedInt> raid;
		if (m_raidPhase == CommanderRaidPhase::RAID || m_raidPhase == CommanderRaidPhase::RETURN)
		{
			raid.push_back( static_cast<UnsignedInt>( m_raidTransport ) );
			for (size_t i = 0; i < m_raiders.size(); ++i)
				raid.push_back( static_cast<UnsignedInt>( m_raiders[i] ) );
		}
		m_planner.setInputs( candidates, enemy ? enemy->getPlayerIndex() : -1, mission, raid );
	}
	m_planner.update( m_player );
	updateMicro();

	UnsignedInt now = TheGameLogic->getFrame();
	if (now >= m_nextAssistCheckFrame)
	{
		m_nextAssistCheckFrame = now + paceFor( getAIDifficulty(), m_personality ).checkInterval;
		if (!m_openingLogged)
		{
			m_openingLogged = TRUE;
			static const char *const names[] = { "knapp", "standard", "reich" };
			char line[160];
			WWFormat::Into(line, "KIEROEFFNUNG {:d} spieler {:d} startkapital {:d} klasse {} geldfaktor {:.2f}", now, m_player->getPlayerIndex(),
				startingCash(), names[static_cast<Int>( cashClass() )], cashScale());
			GameEngine_RunnerLog( line );
			if (m_personality != AI_PERSONALITY_STANDARD)
			{
				static const char *const characters[AI_PERSONALITY_COUNT] = { "standard", "aggressiv", "techniker", "igel" };
				WWFormat::Into(line, "KIPERSOENLICHKEIT {:d} spieler {:d} art {}", now, m_player->getPlayerIndex(), characters[m_personality]);
				GameEngine_RunnerLog( line );
			}
		}
		updateMemory();
		updateEnemyModel();
		updateDetector();
		updateDefences();
		getAiEnemy();			// choose the enemy at a fixed rate even without a script query
		updateScouting();
		updateSuperweaponGuard();
		defendOwnBase();
		updatePartner();
		updateOwnOrders();
		updateAllyAssist();
		updateAttack();
		updateRaid();
		updateEconomy();
		updateFactories();
		updateLateGame();
	}
}

// ------------------------------------------------------------------------------------------------
// Assist allies
// ------------------------------------------------------------------------------------------------
// Base defence: the classic AI defends with its script teams; idle units outside a team stand at the
// factory and only fight what comes into their reach, so raids on the outskirts of the base go unanswered.
// While enemies worth MIN_THREAT are in the base, every idle own combat unit at home attack-moves to them.
// The units stay out of missions; once idle again they are sent anew while the threat lasts.
void AICommanderPlayer::defendOwnBase( void )
{
	// During the start protection enemies in the base do no harm; shortly before its end the units go to
	// them, so they fight when it falls.
	if (partOff( AI_PART_DEFENCE ) || !m_baseCenterSet || graceLeft() > GRACE_LAUNCH_LEAD)
		return;
	Coord3D hotspot;
	const Real threat = enemyStrengthNear( m_player, &m_baseCenter, m_baseRadius + OWN_BASE_MARGIN, &hotspot );
	if (threat < MIN_THREAT)
		return;
	const Real homeRadius = m_baseRadius + HOME_RADIUS_MARGIN;
	AIGroup *group = nullptr;
	Int units = 0;
	Real value = 0.0f;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() != m_player || !isMobileCombatUnit( obj ) || isOnMission( obj->getID() ) || isRaider( obj->getID() ))
			continue;
		if (!isHelperCandidate( obj ) || !obj->getAIUpdateInterface()->isIdle())
			continue;
		if (distSqr2D( obj->getPosition(), &m_baseCenter ) > homeRadius * homeRadius)
			continue;
		addToGroup( &group, obj );
		++units;
		value += unitValue( obj );
	}
	if (group == nullptr)
		return;
	group->groupAttackMoveToPosition( &hotspot, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
	char line[192];
	WWFormat::Into(line, "KIVERTEIDIGUNG {:d} spieler {:d} bedrohung {:.0f} einheiten {:d} wert {:.0f} ort {:.0f} {:.0f}", TheGameLogic->getFrame(), m_player->getPlayerIndex(), threat, units, value, hotspot.x, hotspot.y);
	GameEngine_RunnerLog( line );
}

// ------------------------------------------------------------------------------------------------
// Reconnaissance and memory
// ------------------------------------------------------------------------------------------------
// In sight of the AI or an ally right now (allies share their vision).
Bool AICommanderPlayer::isSeenNow( const Coord3D *pos ) const
{
	return ThePartitionManager->getShroudStatusForPlayer( m_player->getPlayerIndex(), pos ) == CellShroudStatus::CELLSHROUD_CLEAR;
}

// Enemy buildings and combat units in sight are remembered with position and time. A building is
// forgotten once its place is in sight and it is not there any more (destroyed, sold, captured); a unit
// when its last position is in sight without it, or after UNIT_MEMORY.
void AICommanderPlayer::updateMemory( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	const Team *ownTeam = m_player->getDefaultTeam();
	if (ownTeam == nullptr)
		return;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->isEffectivelyDead() || !isVisibleToAI( obj ))
			continue;
		Player *owner = obj->getControllingPlayer();
		if (owner == nullptr || owner == m_player || owner->getDefaultTeam() == nullptr
			|| m_player->getRelationship( owner->getDefaultTeam() ) != ENEMIES)
			continue;
		const Bool structure = obj->isKindOf( KINDOF_STRUCTURE ) && !obj->isKindOf( KINDOF_MINE );
		if (!structure && !isMobileCombatUnit( obj ))
			continue;
		if (!isSeenNow( obj->getPosition() ))
			continue;
		KnownObject *k = nullptr;
		for (size_t i = 0; i < m_known.size() && k == nullptr; ++i)
			if (m_known[i].id == obj->getID())
				k = &m_known[i];
		if (k == nullptr)
		{
			m_known.push_back( KnownObject() );
			k = &m_known.back();
			k->id = obj->getID();
		}
		k->owner = owner->getPlayerIndex();
		k->pos = *obj->getPosition();
		k->frame = now;
		k->value = unitValue( obj );
		k->structure = structure;
	}
	// drop the forgotten entries in place (order kept, no allocation per pass)
	size_t kept = 0;
	for (size_t i = 0; i < m_known.size(); ++i)
	{
		const KnownObject &k = m_known[i];
		Bool keep = TRUE;
		if (k.frame != now)
		{
			if (!k.structure && now - k.frame > UNIT_MEMORY)
				keep = FALSE;
			else if (isSeenNow( &k.pos ))
				keep = FALSE;	// in sight but not seen this pass: gone or moved away
		}
		if (keep)
		{
			if (kept != i)
				m_known[kept] = k;
			++kept;
		}
	}
	m_known.resize( kept );
	// start positions in sight count as explored (independent of the shroud setting of the game)
	const std::vector<std::pair<Int, Coord3D>> &starts = startPositions();
	for (size_t i = 0; i < starts.size(); ++i)
	{
		if (isSeenNow( &starts[i].second ))
			m_startsSeen |= 1u << (starts[i].first - 1);
	}
}

// The start waypoints of the map (slot, place); a map's waypoints do not change, so they are looked up once.
const std::vector<std::pair<Int, Coord3D>> &AICommanderPlayer::startPositions( void ) const
{
	if (!m_startPositionsRead)
	{
		m_startPositionsRead = TRUE;
		for (Int i = 1; i <= MAX_SLOTS; ++i)
		{
			AsciiString name;
			name.format( "Player_%d_Start", i );
			Waypoint *way = TheTerrainLogic->getWaypointByName( name );
			if (way)
				m_startPositions.push_back( std::make_pair( i, *way->getLocation() ) );
		}
	}
	return m_startPositions;
}

// Nearest start position (from the map, as every player knows them) that nobody of the alliance has
// had in sight yet (m_startsSeen).
Bool AICommanderPlayer::nextScoutTarget( const Coord3D &from, Coord3D *target ) const
{
	Bool found = FALSE;
	Real best = 0.0f;
	const std::vector<std::pair<Int, Coord3D>> &starts = startPositions();
	for (size_t s = 0; s < starts.size(); ++s)
	{
		const Coord3D *pos = &starts[s].second;
		if (m_startsSeen & (1u << (starts[s].first - 1)))
			continue;
		const Real d = distSqr2D( pos, &from );
		if (!found || d < best)
		{
			*target = *pos;
			best = d;
			found = TRUE;
		}
	}
	return found;
}

// One scout at a time: the cheapest idle combat unit at home (vehicles first), moving (not fighting)
// from one unexplored start position to the next until all have been seen. Not the last defenders: a
// scout often brings pursuers home.
void AICommanderPlayer::updateScouting( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if (partOff( AI_PART_SCOUTING ) || !m_baseCenterSet || now < SCOUT_START)
		return;
	Object *scout = (m_scout != INVALID_ID) ? TheGameLogic->findObjectByID( m_scout ) : nullptr;
	if (scout && (scout->isEffectivelyDead() || scout->getControllingPlayer() != m_player || scout->getAIUpdateInterface() == nullptr))
		scout = nullptr;
	const Bool reached = scout && (isSeenNow( &m_scoutTarget ) || now - m_scoutFrame > SCOUT_TIMEOUT || scout->getAIUpdateInterface()->isIdle());
	if (scout && !reached)
		return;
	const Coord3D from = scout ? *scout->getPosition() : m_baseCenter;
	Coord3D target;
	const Bool more = nextScoutTarget( from, &target );
	if (!more)
	{
		if (scout)
		{
			scout->getAIUpdateInterface()->aiMoveToPosition( &m_baseCenter, CMD_FROM_AI );
			char line[160];
			WWFormat::Into(line, "KIAUFKLAERUNG {:d} spieler {:d} fertig einheit {}", now, m_player->getPlayerIndex(), scout->getTemplate()->getName().str());
			GameEngine_RunnerLog( line );
		}
		m_scout = INVALID_ID;
		return;
	}
	if (scout == nullptr)
	{
		m_scout = INVALID_ID;
		const Real homeRadius = m_baseRadius + HOME_RADIUS_MARGIN;
		Real bestCost = 0.0f;
		Bool bestVehicle = FALSE;
		Int atHome = 0;
		for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
		{
			if (!isHelperCandidate( obj ) || !obj->getAIUpdateInterface()->isIdle() || isSuperweaponGuard( obj->getID() ))
				continue;
			if (distSqr2D( obj->getPosition(), &m_baseCenter ) > homeRadius * homeRadius)
				continue;
			++atHome;
			const Bool vehicle = obj->isKindOf( KINDOF_VEHICLE );
			const Real cost = unitValue( obj );
			if (m_scout == INVALID_ID || (vehicle && !bestVehicle) || (vehicle == bestVehicle && cost < bestCost))
			{
				m_scout = obj->getID();
				bestCost = cost;
				bestVehicle = vehicle;
				scout = obj;
			}
		}
		if (scout == nullptr || atHome < SCOUT_MIN_HOME)
		{
			m_scout = INVALID_ID;
			return;
		}
	}
	m_scoutTarget = target;
	m_scoutFrame = now;
	scout->getAIUpdateInterface()->aiMoveToPosition( &m_scoutTarget, CMD_FROM_AI );
	char line[192];
	WWFormat::Into(line, "KIAUFKLAERUNG {:d} spieler {:d} spaeher {} ziel {:.0f} {:.0f}", now, m_player->getPlayerIndex(), scout->getTemplate()->getName().str(), target.x, target.y);
	GameEngine_RunnerLog( line );
}

// ------------------------------------------------------------------------------------------------
// Enemy model
// ------------------------------------------------------------------------------------------------
void AICommanderPlayer::updateEnemyModel( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	const Real baseRadius = m_baseRadius + OWN_BASE_MARGIN;
	Real unitValueSum = 0.0f, airValue = 0.0f, armedStructures = 0.0f, stealthValue = 0.0f;
	for (size_t i = 0; i < m_known.size(); ++i)
	{
		const KnownObject &k = m_known[i];
		const Object *obj = TheGameLogic->findObjectByID( k.id );	// the type of a seen object is known
		if (k.structure)
		{
			if (obj && !obj->isEffectivelyDead() && obj->isAbleToAttack())
				armedStructures += k.value;
			continue;
		}
		unitValueSum += k.value;
		if (obj && obj->isKindOf( KINDOF_AIRCRAFT ))
			airValue += k.value;
		if (obj && obj->getStatusBits().test( OBJECT_STATUS_CAN_STEALTH ))
			stealthValue += k.value;
		if (m_baseCenterSet && k.frame == now && now < RUSH_UNTIL && !TheGameLogic->isGracePeriod()
			&& distSqr2D( &k.pos, &m_baseCenter ) <= baseRadius * baseRadius)
			m_rushSeenFrame = now;
	}
	if (armedStructures >= TURTLE_MIN_VALUE && armedStructures >= TURTLE_RATIO * unitValueSum)
		m_turtleSeenFrame = now;
	if (unitValueSum > 0.0f && airValue >= AIR_SHARE * unitValueSum)
		m_airSeenFrame = now;
	if (stealthValue >= STEALTH_MIN_VALUE)
		m_stealthSeenFrame = now;
	if (unitValueSum > 0.0f)
		m_enemyAirShare = airValue / unitValueSum;
	m_modelRush = m_rushSeenFrame != 0 && now - m_rushSeenFrame < RUSH_HOLD;
	m_modelTurtle = m_turtleSeenFrame != 0 && now - m_turtleSeenFrame < MODEL_HOLD;
	m_modelAir = m_airSeenFrame != 0 && now - m_airSeenFrame < MODEL_HOLD;
	m_modelStealth = m_stealthSeenFrame != 0 && now - m_stealthSeenFrame < MODEL_HOLD;
	const Int model = (m_modelRush ? 1 : 0) | (m_modelTurtle ? 2 : 0) | (m_modelAir ? 4 : 0) | (m_modelStealth ? 8 : 0);
	if (model == m_loggedModel)
		return;
	m_loggedModel = model;
	char line[224];
	WWFormat::Into(line, "KIGEGNERMODELL {:d} spieler {:d} ansturm {:d} einigeln {:d} luft {:d} tarnung {:d} armee {:.0f} luftwert {:.0f} abwehr {:.0f} getarnt {:.0f}", now,
		m_player->getPlayerIndex(), m_modelRush ? 1 : 0, m_modelTurtle ? 1 : 0, m_modelAir ? 1 : 0, m_modelStealth ? 1 : 0, unitValueSum, airValue,
		armedStructures, stealthValue);
	GameEngine_RunnerLog( line );
}

// A building the own dozers can put up that detects stealthed units (StealthDetectorUpdate).
const ThingTemplate *AICommanderPlayer::findConstructableDetector( void ) const
{
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() != m_player || obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_DOZER ))
			continue;
		const CommandSet *set = TheControlBar->findCommandSet( obj->getCommandSetString() );
		if (set == nullptr)
			continue;
		for (Int j = 0; j < MAX_COMMANDS_PER_SET; ++j)
		{
			const CommandButton *button = set->getCommandButton( j );
			if (button == nullptr || button->getCommandType() != GUI_COMMAND_DOZER_CONSTRUCT || !isButtonAllowed( button, obj ))
				continue;
			const ThingTemplate *t = button->getThingTemplate();
			if (t == nullptr || t->getBuildable() == BSTATUS_NO || !m_player->canBuild( t ))
				continue;
			if (isDetectorTemplate( t ))
				return t;
		}
	}
	return nullptr;
}

// Detects stealthed units (StealthDetectorUpdate).
Bool AICommanderPlayer::isDetectorTemplate( const ThingTemplate *t )
{
	const ModuleInfo &modules = t->getBehaviorModuleInfo();
	for (Int i = 0; i < modules.getCount(); ++i)
	{
		if (modules.getNthName( i ).compareNoCase( "StealthDetectorUpdate" ) == 0)
			return TRUE;
	}
	return FALSE;
}

void AICommanderPlayer::setPersonality( Int personality )
{
	m_personality = (personality > AI_PERSONALITY_STANDARD && personality < AI_PERSONALITY_COUNT) ? personality : AI_PERSONALITY_STANDARD;
}

// An ordered defence site is held while an own defence stands there (also under construction) or its
// build order still waits.
Bool AICommanderPlayer::defenceSiteHeld( const Coord3D &site ) const
{
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() == m_player && obj->isKindOf( KINDOF_FS_BASE_DEFENSE ) && !obj->isEffectivelyDead()
			&& distSqr2D( obj->getPosition(), &site ) <= DEFENCE_SITE_RADIUS * DEFENCE_SITE_RADIUS)
			return TRUE;
	}
	for (BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext())
	{
		if (info->isPriorityBuild() && info->isBuildable() && TheGameLogic->findObjectByID( info->getObjectID() ) == nullptr
			&& distSqr2D( info->getLocation(), &site ) <= 1.0f)
			return TRUE;
	}
	return FALSE;
}

// Turtle: up to TURTLE_DEFENCES own base defence buildings (FS_BASE_DEFENSE) in addition to those of the build
// list and scripts, one at a time, from the second minute; a destroyed one is ordered anew.
void AICommanderPlayer::updateDefences( void )
{
	if (m_personality != AI_PERSONALITY_TURTLE || TheGameLogic->getFrame() < FACTORY_SECOND_START / 2)
		return;
	char line[224];
	const UnsignedInt now = TheGameLogic->getFrame();
	for (size_t i = 0; i < m_defenceSites.size(); )
	{
		DefenceSite &site = m_defenceSites[i];
		if (defenceSiteHeld( site.pos ))
		{
			++i;
			continue;
		}
		WWFormat::Into(line, "KIWIRTSCHAFT {:d} spieler {:d} {} ort {:.0f} {:.0f}", now, m_player->getPlayerIndex(), "abwehr_verloren", site.pos.x, site.pos.y);
		GameEngine_RunnerLog( line );
		m_defenceSites.erase( m_defenceSites.begin() + static_cast<std::ptrdiff_t>( i ) );
	}
	if (growthBuildPending())
		return;
	const ThingTemplate *t = findConstructable( KINDOF_FS_BASE_DEFENSE );
	if (t == nullptr)
		return;
	const Int have = static_cast<Int>( m_defenceSites.size() );
	if (have >= TURTLE_DEFENCES || freeMoney() - t->calcCostToBuild( m_player ) < scaledMoney( LATE_KEEP_MONEY ))
		return;
	DefenceSite site;
	site.frame = now;
	if (queueBaseBuilding( t, &site.pos ))
	{
		++m_economyOrders;
		m_defenceSites.push_back( site );
		WWFormat::Into(line, "KIWIRTSCHAFT {:d} spieler {:d} bau {} bestand {:d} ziel {:d} vorrat {:.0f} grund {}", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), t->getName().str(), have, TURTLE_DEFENCES, 0.0f, "abwehr");
		GameEngine_RunnerLog( line );
	}
}

// A priority build the command layer just queued (the build list grows at its head).
void AICommanderPlayer::noteOwnOrder( const BuildListInfo *headBefore )
{
	BuildListInfo *head = m_player->getBuildList();
	if (head == nullptr || head == headBefore || !head->isPriorityBuild())
		return;
	OwnOrder order;
	order.name = head->getTemplateName();
	order.pos = *head->getLocation();
	order.frame = TheGameLogic->getFrame();
	m_ownOrders.push_back( order );
}

// Own priority builds the classic AI does not start (site unsafe or blocked, prerequisites missing) are
// withdrawn after OWN_ORDER_TIMEOUT with the money there: a waiting order would block all growth
// (growthBuildPending). Waiting for money is no reason; the clock starts when the money is there.
void AICommanderPlayer::updateOwnOrders( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	for (size_t i = 0; i < m_ownOrders.size(); )
	{
		OwnOrder &order = m_ownOrders[i];
		BuildListInfo *entry = nullptr;
		for (BuildListInfo *info = m_player->getBuildList(); info && entry == nullptr; info = info->getNext())
		{
			if (info->isPriorityBuild() && info->getTemplateName() == order.name && distSqr2D( info->getLocation(), &order.pos ) <= 1.0f)
				entry = info;
		}
		const Bool waiting = entry && entry->isBuildable() && TheGameLogic->findObjectByID( entry->getObjectID() ) == nullptr;
		if (!waiting)
		{
			m_ownOrders.erase( m_ownOrders.begin() + static_cast<std::ptrdiff_t>( i ) );	// started, used or gone
			continue;
		}
		const ThingTemplate *t = TheThingFactory->findTemplate( order.name );
		if (t == nullptr || static_cast<Int>( m_player->getMoney()->countMoney() ) < t->calcCostToBuild( m_player ))
			order.frame = now;
		if (now - order.frame <= OWN_ORDER_TIMEOUT)
		{
			++i;
			continue;
		}
		entry->setNumRebuilds( 0 );
		char line[224];
		WWFormat::Into(line, "KIWIRTSCHAFT {:d} spieler {:d} auftrag_abgebrochen {} ort {:.0f} {:.0f}", now, m_player->getPlayerIndex(), order.name.str(), order.pos.x, order.pos.y);
		GameEngine_RunnerLog( line );
		m_ownOrders.erase( m_ownOrders.begin() + static_cast<std::ptrdiff_t>( i ) );
	}
}

// The beacon template of the owner's side.
static Bool isBeaconOf( const Object *obj, const Player *owner )
{
	const PlayerTemplate *pt = owner ? owner->getPlayerTemplate() : nullptr;
	return pt != nullptr && !pt->getBeaconTemplate().isEmpty() && obj->getTemplate()->getName() == pt->getBeaconTemplate();
}

Bool AICommanderPlayer::hasHumanPartner( void ) const
{
	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		if (p->getPlayerType() == PLAYER_HUMAN && isHelpableAlly( p ))
			return TRUE;
	}
	return FALSE;
}

// Known object of the enemy nearest to the attack mark, within MARK_RADIUS.
Bool AICommanderPlayer::findMarkObjective( const Player *enemy, Coord3D *pos, ObjectID *id ) const
{
	const KnownObject *best = nullptr;
	Real bestDist = MARK_RADIUS * MARK_RADIUS;
	for (size_t i = 0; i < m_known.size(); ++i)
	{
		const KnownObject &k = m_known[i];
		if (k.owner != enemy->getPlayerIndex())
			continue;
		const Real d = distSqr2D( &k.pos, &m_markPos );
		if (d < bestDist || (d == bestDist && best && k.id < best->id))
		{
			best = &k;
			bestDist = d;
		}
	}
	if (best == nullptr)
		return FALSE;
	*pos = best->pos;
	*id = best->id;
	return TRUE;
}

// Attack target: a partner's mark first (what is known there, otherwise the spot itself), then the usual choice.
Bool AICommanderPlayer::findAttackObjective( const Player *enemy, const Coord3D &from, Coord3D *pos, ObjectID *id ) const
{
	if (m_markFrame != 0 && (m_markEnemy < 0 || m_markEnemy == enemy->getPlayerIndex()))
	{
		if (findMarkObjective( enemy, pos, id ))
			return TRUE;
		*pos = m_markPos;
		*id = INVALID_ID;
		return TRUE;
	}
	return findObjective( enemy, from, pos, id );
}

void AICommanderPlayer::placeOwnBeacon( const Coord3D &pos, Int kind, UnsignedInt until )
{
	static const char *const names[] = { "angriff", "hilfe", "rueckzug", "komme" };
	static const char *const captions[] = { "GZH:AIBeaconAttack", "GZH:AIBeaconHelp", "GZH:AIBeaconRetreat", "GZH:AIBeaconComing" };
	if (m_ownBeacon != INVALID_ID)
		removeOwnBeacon( "ersetzt" );
	Coord3D where = pos;
	Object *beacon = TheGameLogic->placeBeacon( m_player, &where, nullptr );
	char line[192];
	if (beacon == nullptr)
	{
		WWFormat::Into(line, "KIMARKIERUNG {:d} spieler {:d} setzen_fehlgeschlagen {}", TheGameLogic->getFrame(), m_player->getPlayerIndex(), names[kind]);
		GameEngine_RunnerLog( line );
		return;
	}
	m_ownBeacon = beacon->getID();
	m_ownBeaconKind = kind;
	m_ownBeaconUntil = until;
	if (beacon->getDrawable() && TheGameText)
		beacon->getDrawable()->setCaptionText( TheGameText->fetch( captions[kind] ) );
	WWFormat::Into(line, "KIMARKIERUNG {:d} spieler {:d} setzt {} ort {:.0f} {:.0f}", TheGameLogic->getFrame(), m_player->getPlayerIndex(), names[kind], where.x, where.y);
	GameEngine_RunnerLog( line );
}

void AICommanderPlayer::removeOwnBeacon( const char *reason )
{
	Object *beacon = TheGameLogic->findObjectByID( m_ownBeacon );
	if (beacon && beacon->getControllingPlayer() == m_player && isBeaconOf( beacon, m_player ))
		TheGameLogic->destroyObject( beacon );
	m_ownBeacon = INVALID_ID;
	char line[160];
	WWFormat::Into(line, "KIMARKIERUNG {:d} spieler {:d} entfernt {}", TheGameLogic->getFrame(), m_player->getPlayerIndex(), reason);
	GameEngine_RunnerLog( line );
}

// Beacons with human partners. A partner's beacon near one of his buildings asks for help (an assist
// mission there); elsewhere it marks the attack target (what is known there), for MARK_TIME or until the
// partner removes it. The AI itself marks the target of its attack and, with its base under attack, asks
// for help. Without a human partner nothing happens.
void AICommanderPlayer::updatePartner( void )
{
	const Bool partner = hasHumanPartner();
	if (!partner && m_markFrame == 0 && m_ownBeacon == INVALID_ID)
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	char line[256];

	// a new beacon of a partner
	Object *newest = nullptr;
	for (Object *obj = TheGameLogic->getFirstObject(); obj && partner; obj = obj->getNextObject())
	{
		Player *owner = obj->getControllingPlayer();
		if (obj->getID() <= m_markSeen || owner == nullptr || owner->getPlayerType() != PLAYER_HUMAN || !isBeaconOf( obj, owner ) || !isHelpableAlly( owner ))
			continue;
		if (newest == nullptr || obj->getID() > newest->getID())
			newest = obj;
	}
	if (newest)
	{
		m_markSeen = newest->getID();
		Player *ally = newest->getControllingPlayer();
		const Coord3D pos = *newest->getPosition();
		Bool help = FALSE;
		for (Object *obj = TheGameLogic->getFirstObject(); obj && !help; obj = obj->getNextObject())
		{
			help = obj->getControllingPlayer() == ally && obj->isKindOf( KINDOF_STRUCTURE ) && !obj->isEffectivelyDead()
				&& distSqr2D( obj->getPosition(), &pos ) <= MARK_HELP_RADIUS * MARK_HELP_RADIUS;
		}
		if (help)
		{
			const char *result = "angenommen";
			if (m_assistAlly == ally->getPlayerIndex())
			{
				m_assistPos = pos;
				m_assistAnchor = pos;
				result = "verlegt";
				if (m_ownBeacon != INVALID_ID && m_ownBeaconKind == OWN_BEACON_COMING)
					placeOwnBeacon( pos, OWN_BEACON_COMING );
			}
			else if (m_assistAlly >= 0 || ownBaseThreat() >= MIN_THREAT)
				result = "abgelehnt";
			else
			{
				Coord3D centroid;
				const Real threat = std::max( enemyStrengthNear( ally, &pos, HOTSPOT_RADIUS, &centroid ), MIN_THREAT );
				startAssist( ally->getPlayerIndex(), pos, pos, threat );
				if (m_assistAlly != ally->getPlayerIndex())
					result = "zu_schwach";	// not enough idle units to send
			}
			WWFormat::Into(line, "KIMARKIERUNG {:d} spieler {:d} von {:d} art hilfe ort {:.0f} {:.0f} {}", now, m_player->getPlayerIndex(), ally->getPlayerIndex(), pos.x, pos.y, result);
			GameEngine_RunnerLog( line );
		}
		else
		{
			// the enemy at the spot: owner of the known object nearest to it
			Int enemy = -1;
			Real bestDist = MARK_RADIUS * MARK_RADIUS;
			ObjectID bestId = INVALID_ID;
			for (size_t i = 0; i < m_known.size(); ++i)
			{
				const KnownObject &k = m_known[i];
				if (!isValidEnemy( ThePlayerList->getNthPlayer( k.owner ) ))
					continue;
				const Real d = distSqr2D( &k.pos, &pos );
				if (d < bestDist || (d == bestDist && bestId != INVALID_ID && k.id < bestId))
				{
					bestDist = d;
					bestId = k.id;
					enemy = k.owner;
				}
			}
			// nothing known there: the enemy who started there (start positions are public, as in the lobby)
			const std::vector<std::pair<Int, Coord3D>> &starts = startPositions();
			Real startDist = MARK_START_RADIUS * MARK_START_RADIUS;
			for (size_t i = 0; i < starts.size() && enemy < 0; ++i)
			{
				if (distSqr2D( &starts[i].second, &pos ) > startDist)
					continue;
				for (Int j = 0; j < ThePlayerList->getPlayerCount(); ++j)
				{
					Player *p = ThePlayerList->getNthPlayer( j );
					if (isValidEnemy( p ) && p->getMpStartIndex() + 1 == starts[i].first)
						enemy = p->getPlayerIndex();
				}
			}
			m_markBeacon = newest->getID();
			m_markPos = pos;
			m_markFrame = now;
			m_markEnemy = enemy;
			if (enemy >= 0)
				setEnemy( ThePlayerList->getNthPlayer( enemy ), "markierung", 0.0f );
			if (m_attackPhase == CommanderAttackPhase::ATTACK)
				m_objectiveID = INVALID_ID;	// the attack turns to the mark in its next pass
			if (m_ownBeacon != INVALID_ID && m_ownBeaconKind == OWN_BEACON_ATTACK)
				removeOwnBeacon( "partnerziel" );
			WWFormat::Into(line, "KIMARKIERUNG {:d} spieler {:d} von {:d} art angriff ort {:.0f} {:.0f} gegner {:d}", now, m_player->getPlayerIndex(), ally->getPlayerIndex(), pos.x, pos.y, enemy);
			GameEngine_RunnerLog( line );
		}
	}

	// end of the attack mark
	if (m_markFrame != 0)
	{
		const char *reason = nullptr;
		Object *beacon = TheGameLogic->findObjectByID( m_markBeacon );
		Coord3D target;
		ObjectID targetId = INVALID_ID;
		Player *attackEnemy = (m_attackPhase == CommanderAttackPhase::ATTACK) ? ThePlayerList->getNthPlayer( m_attackEnemy ) : nullptr;
		Coord3D centroid;
		if (beacon == nullptr || beacon->isDestroyed())
			reason = "entfernt";
		else if (now - m_markFrame > MARK_TIME)
			reason = "zeit";
		else if (m_markEnemy >= 0 && !isValidEnemy( ThePlayerList->getNthPlayer( m_markEnemy ) ))
			reason = "gegner_besiegt";
		else if (attackEnemy && !findMarkObjective( attackEnemy, &target, &targetId )
			&& forceCentroid( m_attackers, &centroid, nullptr, 0.0f, nullptr ) > 0.0f && distSqr2D( &centroid, &m_markPos ) <= MARK_REACHED * MARK_REACHED)
			reason = "erreicht";
		if (reason)
		{
			m_markFrame = 0;
			m_markBeacon = INVALID_ID;
			m_markEnemy = -1;
			WWFormat::Into(line, "KIMARKIERUNG {:d} spieler {:d} ziel_ende {}", now, m_player->getPlayerIndex(), reason);
			GameEngine_RunnerLog( line );
		}
	}

	// own beacon: ask for help with the base under attack
	if (m_ownBeacon != INVALID_ID && TheGameLogic->findObjectByID( m_ownBeacon ) == nullptr)
		m_ownBeacon = INVALID_ID;
	const Real threat = ownBaseThreat();
	if (partner && threat >= ASK_HELP_THREAT && (m_ownBeacon == INVALID_ID || m_ownBeaconKind != OWN_BEACON_HELP))
	{
		Coord3D centroid;
		enemyStrengthNear( m_player, &m_baseCenter, m_baseRadius + OWN_BASE_MARGIN, &centroid );
		placeOwnBeacon( centroid, OWN_BEACON_HELP );
	}
	else if (m_ownBeacon != INVALID_ID && m_ownBeaconUntil != 0 && now >= m_ownBeaconUntil)
		removeOwnBeacon( "abgelaufen" );
	else if (m_ownBeacon != INVALID_ID && m_ownBeaconKind == OWN_BEACON_COMING && m_assistAlly < 0)
		removeOwnBeacon( "hilfe_ende" );
	else if (m_ownBeacon != INVALID_ID && m_ownBeaconKind == OWN_BEACON_HELP && threat < MIN_THREAT * 0.5f)
		removeOwnBeacon( "abgewehrt" );
	else if (m_ownBeacon != INVALID_ID && !partner)
		removeOwnBeacon( "kein_partner" );
}

// Stealthed enemies known: one detecting building near the base.
void AICommanderPlayer::updateDetector( void )
{
	if (!m_modelStealth || ownBaseThreat() >= MIN_THREAT || growthBuildPending())
		return;
	const ThingTemplate *t = findConstructableDetector();
	if (t == nullptr || countOwnOrPlanned( t ) > 0 || freeMoney() < t->calcCostToBuild( m_player ))
		return;
	if (queueBaseBuilding( t ))
	{
		++m_economyOrders;
		char line[224];
		WWFormat::Into(line, "KIWIRTSCHAFT {:d} spieler {:d} bau {} bestand {:d} ziel {:d} vorrat {:.0f} grund {}", TheGameLogic->getFrame(),
			m_player->getPlayerIndex(), t->getName().str(), 0, 1, 0.0f, "detektor");
		GameEngine_RunnerLog( line );
	}
}

// Share of the army that stays home: the difficulty's, more while a rush is going on.
Real AICommanderPlayer::reserveFraction( void ) const
{
	const Real base = paceFor( getAIDifficulty(), m_personality ).reserveFraction;
	return m_modelRush ? std::min( base + RUSH_RESERVE, 0.8f ) : base;
}

// Has a weapon that hurts air targets (combat table of the planner).
Bool AICommanderPlayer::hitsAir( const ThingTemplate *t ) const
{
	const Int profile = m_planner.profileOf( t );
	if (profile < 0)
		return FALSE;
	const AICombat::Table &table = m_planner.getCombatTable();
	for (Int armor = -1; armor < table.armorCount; ++armor)
	{
		const size_t index = (static_cast<size_t>( profile ) * static_cast<size_t>( table.slots() ) + static_cast<size_t>( armor + 1 )) * 2 + 1;
		if (index < table.dps.size() && table.dps[index] > 0.0f)
			return TRUE;
	}
	return FALSE;
}

// Superweapon limit: guards for the own superweapon (see SUPERWEAPON_GUARD_VALUE). Stray guards walk back
// to it; the rest of the time they stand there and fight what comes into reach.
void AICommanderPlayer::updateSuperweaponGuard( void )
{
	m_superweaponGuards.clear();
	if (TheGameLogic->getSuperweaponRestriction() == 0 || partOff( AI_PART_SUPERWEAPON ) || !m_baseCenterSet)
		return;
	Object *weapon = nullptr;
	for (Object *obj = TheGameLogic->getFirstObject(); obj && weapon == nullptr; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() == m_player && obj->isKindOf( KINDOF_STRUCTURE ) && !obj->isEffectivelyDead()
			&& AIPlanner_isSuperweapon( obj->getTemplate() ))
			weapon = obj;
	}
	if (weapon == nullptr)
		return;
	const Real homeRadius = m_baseRadius + HOME_RADIUS_MARGIN;
	std::vector<std::pair<Real, Object *>> candidates;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (!isHelperCandidate( obj ) || isRaider( obj->getID() ))
			continue;
		if (distSqr2D( obj->getPosition(), &m_baseCenter ) > homeRadius * homeRadius)
			continue;
		candidates.push_back( std::make_pair( distSqr2D( obj->getPosition(), weapon->getPosition() ), obj ) );
	}
	std::stable_sort( candidates.begin(), candidates.end(),
		[]( const std::pair<Real, Object *> &a, const std::pair<Real, Object *> &b ) { return a.first < b.first; } );
	AIGroup *group = nullptr;
	Real value = 0.0f;
	Int sent = 0;
	for (size_t i = 0; i < candidates.size() && value < SUPERWEAPON_GUARD_VALUE; ++i)
	{
		Object *obj = candidates[i].second;
		m_superweaponGuards.push_back( obj->getID() );
		value += unitValue( obj );
		if (obj->getAIUpdateInterface()->isIdle() && candidates[i].first > SUPERWEAPON_GUARD_RADIUS * SUPERWEAPON_GUARD_RADIUS)
		{
			addToGroup( &group, obj );
			++sent;
		}
	}
	if (group)
		group->groupAttackMoveToPosition( weapon->getPosition(), NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
	if (group == nullptr && static_cast<Int>( m_superweaponGuards.size() ) == m_loggedGuardCount)
		return;
	m_loggedGuardCount = static_cast<Int>( m_superweaponGuards.size() );
	char line[192];
	WWFormat::Into(line, "KISUPERWACHE {:d} spieler {:d} superwaffe {} wachen {:d} geschickt {:d} wert {:.0f}", TheGameLogic->getFrame(),
		m_player->getPlayerIndex(), weapon->getTemplate()->getName().str(), static_cast<Int>( m_superweaponGuards.size() ), sent, value);
	GameEngine_RunnerLog( line );
}

Bool AICommanderPlayer::isSuperweaponGuard( ObjectID id ) const
{
	return std::find( m_superweaponGuards.begin(), m_superweaponGuards.end(), id ) != m_superweaponGuards.end();
}

Bool AICommanderPlayer::isHelpableAlly( Player *p ) const
{
	if (p == nullptr || p == m_player || p == ThePlayerList->getNeutralPlayer() || p->isPlayerObserver())
		return FALSE;
	if (p->getDefaultTeam() == nullptr || m_player->getDefaultTeam() == nullptr)
		return FALSE;
	if (m_player->getRelationship( p->getDefaultTeam() ) != ALLIES || p->getRelationship( m_player->getDefaultTeam() ) != ALLIES)
		return FALSE;
	return p->hasAnyObjects();
}

// Sum of the combat values of all enemies of victim within radius, plus their value-weighted centroid.
// Stealthed, undetected units do not count (the AI cannot see them otherwise either).
Real AICommanderPlayer::enemyStrengthNear( const Player *victim, const Coord3D *pos, Real radius, Coord3D *centroid ) const
{
	Real sum = 0.0f, cx = 0.0f, cy = 0.0f;
	Real radiusSqr = radius * radius;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (!isMobileCombatUnit( obj ))
			continue;
		Player *owner = obj->getControllingPlayer();
		if (owner == nullptr || owner == victim || owner->getDefaultTeam() == nullptr)
			continue;
		if (victim->getRelationship( owner->getDefaultTeam() ) != ENEMIES)
			continue;
		if (!isVisibleToAI( obj ) || !isSeenNow( obj->getPosition() ))
			continue;
		if (distSqr2D( obj->getPosition(), pos ) > radiusSqr)
			continue;
		Real v = unitValue( obj );
		sum += v;
		cx += obj->getPosition()->x * v;
		cy += obj->getPosition()->y * v;
	}
	if (centroid)
	{
		*centroid = *pos;
		if (sum > 0.0f)
		{
			centroid->x = cx / sum;
			centroid->y = cy / sum;
		}
	}
	return sum;
}

Real AICommanderPlayer::ownBaseThreat( void ) const
{
	// enemies cannot do harm during the start protection
	if (!m_baseCenterSet || TheGameLogic->isGracePeriod())
		return 0.0f;
	return enemyStrengthNear( m_player, &m_baseCenter, m_baseRadius + OWN_BASE_MARGIN, nullptr );
}

// Ally emergency: buildings, harvesters or dozers recently damaged by enemies, with attackers worth
// at least MIN_THREAT at one of these spots. Picks the spot with the largest threat (on a tie the
// first in player and object order).
Bool AICommanderPlayer::findAllyEmergency( Int *allyIndex, Coord3D *hotspot, Coord3D *anchor, Real *threat ) const
{
	const Int MAX_SPOTS_PER_ALLY = 8;
	UnsignedInt now = TheGameLogic->getFrame();
	Real best = 0.0f;
	Bool found = FALSE;
	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *ally = ThePlayerList->getNthPlayer( i );
		if (!isHelpableAlly( ally ))
			continue;
		if (ally->getAttackedFrame() == 0 || ally->getAttackedFrame() + ATTACK_RECENT < now)
			continue;
		Int spots = 0;
		for (Object *obj = TheGameLogic->getFirstObject(); obj && spots < MAX_SPOTS_PER_ALLY; obj = obj->getNextObject())
		{
			if (obj->getControllingPlayer() != ally || obj->isEffectivelyDead())
				continue;
			if (!obj->isKindOf( KINDOF_STRUCTURE ) && !obj->isKindOf( KINDOF_HARVESTER ) && !obj->isKindOf( KINDOF_DOZER ))
				continue;
			BodyModuleInterface *body = obj->getBodyModule();
			if (body == nullptr)
				continue;
			UnsignedInt stamp = body->getLastDamageTimestamp();
			if (stamp == 0xffffffff || stamp + ATTACK_RECENT < now)
				continue;
			const DamageInfo *info = body->getLastDamageInfo();
			if (info == nullptr || info->out.m_noEffect)
				continue;
			if (info->in.m_sourcePlayerMask == 0 || (info->in.m_sourcePlayerMask & ally->getPlayerMask()) != 0)
				continue;	// unknown source or friendly fire
			++spots;
			Coord3D centroid;
			Real t = enemyStrengthNear( ally, obj->getPosition(), HOTSPOT_RADIUS, &centroid );
			if (t > best)
			{
				best = t;
				*hotspot = centroid;
				*anchor = *obj->getPosition();
				*allyIndex = i;
				found = TRUE;
			}
		}
	}
	*threat = best;
	return found && best >= MIN_THREAT;
}

Bool AICommanderPlayer::isHelper( ObjectID id ) const
{
	return std::find( m_helpers.begin(), m_helpers.end(), id ) != m_helpers.end();
}

Bool AICommanderPlayer::isOnMission( ObjectID id ) const
{
	return isHelper( id ) || std::find( m_attackers.begin(), m_attackers.end(), id ) != m_attackers.end() || isRaider( id )
		|| (id != INVALID_ID && id == m_scout);
}

Real AICommanderPlayer::missionValue( const std::vector<ObjectID> &ids ) const
{
	Real sum = 0.0f;
	for (size_t i = 0; i < ids.size(); ++i)
	{
		Object *obj = TheGameLogic->findObjectByID( ids[i] );
		if (obj && !obj->isEffectivelyDead())
			sum += unitValue( obj );
	}
	return sum;
}

// Units the command layer may use for a mission: own mobile ground combat units, not loaded, not
// disabled, not in a team the classic AI is still assembling (inactive teams), and not already on a
// mission (assist or attack).
Bool AICommanderPlayer::isHelperCandidate( Object *obj ) const
{
	if (obj->getControllingPlayer() != m_player || !isMobileCombatUnit( obj ))
		return FALSE;
	if (obj->isKindOf( KINDOF_AIRCRAFT ) || obj->isContained() || obj->isDisabled())
		return FALSE;
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (ai == nullptr || ai->isClearingMines())
		return FALSE;
	Team *team = obj->getTeam();
	if (team == nullptr || !team->isActive())
		return FALSE;
	return !isOnMission( obj->getID() );
}

void AICommanderPlayer::pruneHelpers( void )
{
	std::vector<ObjectID> alive;
	alive.reserve( m_helpers.size() );
	for (size_t i = 0; i < m_helpers.size(); ++i)
	{
		Object *obj = TheGameLogic->findObjectByID( m_helpers[i] );
		if (obj && !obj->isEffectivelyDead() && obj->getControllingPlayer() == m_player)
			alive.push_back( m_helpers[i] );
	}
	m_helpers.swap( alive );
}

// Picks helpers up to needed without touching the reserve and attack-moves them to the hotspot.
// If they do not reach minimum, nobody goes.
Int AICommanderPlayer::sendHelpers( const Coord3D &hotspot, Real needed, Real minimum, Real *sentValue, AsciiString *why,
	Real *available, Bool dryRun )
{
	*sentValue = 0.0f;
	std::vector<HelperCandidate> candidates;
	Real army = 0.0f;
	Real onMission = 0.0f;
	Real maxDistSqr = MAX_HELP_DISTANCE * MAX_HELP_DISTANCE;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() != m_player || !isMobileCombatUnit( obj ))
			continue;
		Real v = unitValue( obj );
		army += v;
		if (isOnMission( obj->getID() ))
		{
			onMission += v;
			continue;
		}
		if (!isHelperCandidate( obj ))
			continue;
		Real d = distSqr2D( obj->getPosition(), &hotspot );
		if (d > maxDistSqr)
			continue;
		HelperCandidate c;
		c.obj = obj;
		c.value = v;
		c.distSqr = d;
		c.idle = obj->getAIUpdateInterface()->isIdle();
		candidates.push_back( c );
	}
	Real sendable = army * (1.0f - reserveFraction()) - onMission;
	Real candidateValue = 0.0f;
	for (size_t i = 0; i < candidates.size(); ++i)
		candidateValue += candidates[i].value;
	if (why)	// log why help was (not) sent
		why->format( "armee %.0f einsetzbar %.0f kandidaten %d wert %.0f", army, sendable, static_cast<Int>(candidates.size()), candidateValue );
	if (available)
		*available = (sendable <= 0.0f) ? 0.0f : ((candidateValue < sendable) ? candidateValue : sendable);
	if (sendable <= 0.0f || candidates.empty() || dryRun)
		return 0;
	std::sort( candidates.begin(), candidates.end(), helperBefore );

	std::vector<Object *> chosen;
	Real sum = 0.0f;
	for (size_t i = 0; i < candidates.size() && sum < needed; ++i)
	{
		if (static_cast<Int>(m_helpers.size() + chosen.size()) >= MAX_HELPERS)
			break;
		if (sum + candidates[i].value > sendable)
			continue;
		chosen.push_back( candidates[i].obj );
		sum += candidates[i].value;
	}
	if (chosen.empty() || sum < minimum)
		return 0;

	takeIntoMission( chosen, &m_helpers, hotspot );
	*sentValue = sum;
	return static_cast<Int>(chosen.size());
}

// Take units into a mission: remove them from their script team so the EA scripts do not redirect
// them, make them unrecruitable for the mission (else the classic AI pulls them into a new team),
// and attack-move them to the target.
void AICommanderPlayer::takeIntoMission( const std::vector<Object *> &units, std::vector<ObjectID> *list, const Coord3D &pos )
{
	if (units.empty())
		return;
	AIGroup *group = TheAI->createGroup();
	if (group == nullptr)
		return;
	Team *defaultTeam = m_player->getDefaultTeam();
	for (size_t i = 0; i < units.size(); ++i)
	{
		Object *obj = units[i];
		if (defaultTeam && obj->getTeam() != defaultTeam)
			obj->setTeam( defaultTeam );
		obj->getAIUpdateInterface()->setIsRecruitable( FALSE );
		group->add( obj );
		list->push_back( obj->getID() );
	}
	group->groupAttackMoveToPosition( &pos, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
}

// Release the units of a mission: recruitable again and attack-moved back to the own base, where
// they are in the default team and the classic AI can take them into its teams again.
void AICommanderPlayer::releaseUnits( std::vector<ObjectID> *list )
{
	AIGroup *group = nullptr;
	for (size_t i = 0; i < list->size(); ++i)
	{
		Object *obj = TheGameLogic->findObjectByID( (*list)[i] );
		if (obj == nullptr || obj->isEffectivelyDead() || obj->getControllingPlayer() != m_player || obj->getAIUpdateInterface() == nullptr)
			continue;
		obj->getAIUpdateInterface()->setIsRecruitable( TRUE );
		addToGroup( &group, obj );
	}
	if (group && group->isEmpty())
		TheAI->destroyGroup( group );	// no units left: do not leave an empty group behind
	else if (group)
	{
		if (m_baseCenterSet)
			group->groupAttackMoveToPosition( &m_baseCenter, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		else
			group->groupIdle( CMD_FROM_AI );
	}
	list->clear();
}

void AICommanderPlayer::startAssist( Int allyIndex, const Coord3D &hotspot, const Coord3D &anchor, Real threat )
{
	Real sent = 0.0f;
	AsciiString why;
	Int n = sendHelpers( hotspot, threat * HELP_FACTOR, threat * MIN_SEND_FACTOR, &sent, &why );
	UnsignedInt now = TheGameLogic->getFrame();
	char line[320];
	if (n == 0)
	{
		WWFormat::Into(line, "KIHILFE {:d} spieler {:d} verworfen verbuendeter {:d} bedrohung {:.0f} {}", now, m_player->getPlayerIndex(), allyIndex, threat, why.str());
		GameEngine_RunnerLog( line );
		m_assistCooldownFrame = now + static_cast<UnsignedInt>(static_cast<Real>(COOLDOWN) * paceFor( getAIDifficulty(), m_personality ).cooldownScale);
		return;
	}
	m_assistAlly = allyIndex;
	m_assistPos = hotspot;
	m_assistAnchor = anchor;
	m_assistThreat = threat;
	m_assistStartFrame = now;
	m_lastReinforceFrame = now;
	m_assistLastThreatFrame = now;
	++m_assistMissions;
	WWFormat::Into(line, "KIHILFE {:d} spieler {:d} start verbuendeter {:d} bedrohung {:.0f} einheiten {:d} wert {:.0f} ort {:.0f} {:.0f} {}", now, m_player->getPlayerIndex(), allyIndex, threat, n, sent, hotspot.x, hotspot.y, why.str());
	GameEngine_RunnerLog( line );
	// tell a human ally that help is on its way; asking for help ourselves goes first
	Player *ally = ThePlayerList->getNthPlayer( allyIndex );
	if (ally && ally->getPlayerType() == PLAYER_HUMAN && !(m_ownBeacon != INVALID_ID && m_ownBeaconKind == OWN_BEACON_HELP))
		placeOwnBeacon( hotspot, OWN_BEACON_COMING );
}

void AICommanderPlayer::endAssist( const char *reason )
{
	pruneHelpers();
	UnsignedInt now = TheGameLogic->getFrame();
	char line[192];
	WWFormat::Into(line, "KIHILFE {:d} spieler {:d} ende {} dauer {:d} verbleibend {:d}", now, m_player->getPlayerIndex(), reason, (now - m_assistStartFrame) / LOGICFRAMES_PER_SECOND, static_cast<Int>(m_helpers.size()));
	GameEngine_RunnerLog( line );

	releaseUnits( &m_helpers );
	m_assistAlly = -1;
	m_assistThreat = 0.0f;
	m_assistCooldownFrame = now + static_cast<UnsignedInt>(static_cast<Real>(COOLDOWN) * paceFor( getAIDifficulty(), m_personality ).cooldownScale);
}

void AICommanderPlayer::continueAssist( void )
{
	UnsignedInt now = TheGameLogic->getFrame();
	pruneHelpers();
	if (m_helpers.empty())
	{
		endAssist( "verloren" );
		return;
	}
	Player *ally = ThePlayerList->getNthPlayer( m_assistAlly );
	if (!isHelpableAlly( ally ))
	{
		endAssist( "verbuendeter_weg" );
		return;
	}
	if (ownBaseThreat() >= MIN_THREAT)
	{
		endAssist( "eigene_basis" );
		return;
	}
	if (now - m_assistStartFrame > MISSION_TIMEOUT)
	{
		endAssist( "zeit" );
		return;
	}

	// Track the hotspot: a new emergency of the same ally takes precedence (new anchor), otherwise the
	// attackers around the attacked spot (anchor) count. Helpers defend and do not chase into the
	// enemy base.
	Int emergencyAlly = -1;
	Coord3D hotspot, anchor;
	Real threat = 0.0f;
	if (findAllyEmergency( &emergencyAlly, &hotspot, &anchor, &threat ) && emergencyAlly == m_assistAlly)
	{
		m_assistPos = hotspot;
		m_assistAnchor = anchor;
	}
	else
	{
		threat = enemyStrengthNear( ally, &m_assistAnchor, ANCHOR_RADIUS, &hotspot );
		m_assistPos = (threat > 0.0f) ? hotspot : m_assistAnchor;
	}
	m_assistThreat = threat;
	if (threat < MIN_THREAT * 0.5f)
	{
		if (now - m_assistLastThreatFrame > THREAT_CLEAR)
			endAssist( "abgewehrt" );
		return;
	}
	m_assistLastThreatFrame = now;

	// Send idle helpers (arrived or target gone) back to the hotspot.
	AIGroup *group = nullptr;
	for (size_t i = 0; i < m_helpers.size(); ++i)
	{
		Object *obj = TheGameLogic->findObjectByID( m_helpers[i] );
		if (obj == nullptr || obj->getAIUpdateInterface() == nullptr || !obj->getAIUpdateInterface()->isIdle())
			continue;
		addToGroup( &group, obj );
	}
	if (group)
		group->groupAttackMoveToPosition( &m_assistPos, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );

	// If the threat has grown, send reinforcements in batches, not piece by piece into a lost
	// fight. If the situation is hopeless, give up instead.
	Real sentValue = 0.0f;
	Real helpersValue = missionValue( m_helpers );
	Real available = 0.0f;
	sendHelpers( m_assistPos, 0.0f, 0.0f, &sentValue, nullptr, &available, TRUE );
	if (threat > HOPELESS_FACTOR * (helpersValue + available))
	{
		endAssist( "aussichtslos" );
		return;
	}
	Real deficit = threat * HELP_FACTOR - helpersValue;
	if (deficit >= MIN_REINFORCE && now - m_lastReinforceFrame >= REINFORCE_INTERVAL)
	{
		Int n = sendHelpers( m_assistPos, deficit, MIN_REINFORCE, &sentValue );
		if (n > 0)
		{
			m_lastReinforceFrame = now;
			char line[192];
			WWFormat::Into(line, "KIHILFE {:d} spieler {:d} verstaerkung einheiten {:d} wert {:.0f} bedrohung {:.0f}", now, m_player->getPlayerIndex(), n, sentValue, threat);
			GameEngine_RunnerLog( line );
		}
	}
}

void AICommanderPlayer::updateAllyAssist( void )
{
	if (m_assistAlly >= 0)
	{
		continueAssist();
		return;
	}
	if (TheGameLogic->getFrame() < m_assistCooldownFrame)
		return;
	if (m_attackPhase != CommanderAttackPhase::NONE && !paceFor( getAIDifficulty(), m_personality ).assistDuringAttack)
		return;		// one mission at a time

	Int allyIndex = -1;
	Coord3D hotspot, anchor;
	Real threat = 0.0f;
	if (!findAllyEmergency( &allyIndex, &hotspot, &anchor, &threat ))
		return;
	// Own defence first: with an enemy at the own base everything stays home.
	if (ownBaseThreat() >= MIN_THREAT)
		return;
	startAssist( allyIndex, hotspot, anchor, threat );
}

// ------------------------------------------------------------------------------------------------
// Common enemy
// ------------------------------------------------------------------------------------------------
Bool AICommanderPlayer::isValidEnemy( Player *p ) const
{
	if (p == nullptr || p == m_player || p == ThePlayerList->getNeutralPlayer() || p->isPlayerObserver())
		return FALSE;
	if (p->getDefaultTeam() == nullptr || m_player->getRelationship( p->getDefaultTeam() ) != ENEMIES)
		return FALSE;
	return p->hasAnyObjects();
}

// Alliance: oneself and every player mutually allied with us (AI or human).
Bool AICommanderPlayer::isAllianceMember( Player *p ) const
{
	if (p == m_player)
		return TRUE;
	return isHelpableAlly( p );
}

// Alliance leader: the living new AI with the lowest player index. It chooses the common enemy
// and the other new AIs follow (a shared board without extra storage: everyone reads the leader's
// state, which is in the save game anyway).
AICommanderPlayer *AICommanderPlayer::allianceLeader( void ) const
{
	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		if (p != m_player && !isHelpableAlly( p ))
			continue;
		AICommanderPlayer *c = commanderOf( p );
		if (c)
			return c;
	}
	return nullptr;
}

Player *AICommanderPlayer::getAiEnemy( void )
{
	UnsignedInt now = TheGameLogic->getFrame();
	if (now >= m_frameToCheckEnemy)
	{
		m_frameToCheckEnemy = now + ENEMY_CHECK_INTERVAL;
		chooseCommonEnemy();
	}
	return m_currentEnemy;
}

void AICommanderPlayer::setEnemy( Player *enemy, const char *reason, Real score )
{
	if (enemy == m_currentEnemy)
		return;
	char line[192];
	WWFormat::Into(line, "KIFOKUS {:d} spieler {:d} gegner {:d} vorher {:d} grund {} kosten {:.0f}", TheGameLogic->getFrame(), m_player->getPlayerIndex(), enemy ? enemy->getPlayerIndex() : -1, m_currentEnemy ? m_currentEnemy->getPlayerIndex() : -1, reason, score);
	GameEngine_RunnerLog( line );
	m_currentEnemy = enemy;
	m_enemyChosenFrame = TheGameLogic->getFrame();
}

// Enemy choice in the alliance. Followers take the leader's enemy; the leader (or whoever has no
// usable leader) computes a cost per enemy:
//   mean distance from the alliance bases to the enemy base
//   + STRENGTH_WEIGHT * (enemy army / mean enemy army)
//   - PRESSURE_WEIGHT * pressure / (pressure + PRESSURE_HALF)
// Pressure is the enemy's combat value near alliance buildings, so whoever presses us becomes the
// target. Switch only by at least SWITCH_MARGIN and after the hold time, unless the pressure is
// strong. Unlike the classic AI there is no surcharge when another player hunts the same enemy;
// concentrating on one enemy is intended.
void AICommanderPlayer::chooseCommonEnemy( void )
{
	// A human partner's mark on an enemy decides.
	if (m_markFrame != 0 && m_markEnemy >= 0 && isValidEnemy( ThePlayerList->getNthPlayer( m_markEnemy ) ))
	{
		setEnemy( ThePlayerList->getNthPlayer( m_markEnemy ), "markierung", 0.0f );
		return;
	}
	AICommanderPlayer *leader = allianceLeader();
	if (leader && leader != this)
	{
		Player *e = leader->peekAiEnemy();
		if (isValidEnemy( e ))
		{
			setEnemy( e, "buendnis", 0.0f );
			return;
		}
	}

	PlayerPicture pic[MAX_PLAYER_COUNT];
	memset( pic, 0, sizeof( pic ) );
	std::vector<Coord3D> allianceStructures;
	Int count = ThePlayerList->getPlayerCount();
	if (count > MAX_PLAYER_COUNT)
		count = MAX_PLAYER_COUNT;
	Bool member[MAX_PLAYER_COUNT];
	Bool enemy[MAX_PLAYER_COUNT];
	for (Int i = 0; i < count; ++i)
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		member[i] = isAllianceMember( p ) && p->hasAnyObjects();
		enemy[i] = isValidEnemy( p );
	}
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->isEffectivelyDead())
			continue;
		Player *owner = obj->getControllingPlayer();
		if (owner == nullptr)
			continue;
		Int idx = owner->getPlayerIndex();
		if (idx < 0 || idx >= count || !member[idx])
			continue;	// enemies come from the memory below
		PlayerPicture &pp = pic[idx];
		if (obj->isKindOf( KINDOF_STRUCTURE ))
		{
			if (obj->isKindOf( KINDOF_MINE ) || !isVisibleToAI( obj ))
				continue;
			pp.structX += obj->getPosition()->x;
			pp.structY += obj->getPosition()->y;
			++pp.structures;
			if (member[idx])
				allianceStructures.push_back( *obj->getPosition() );
			continue;
		}
		if (!isMobileCombatUnit( obj ) || !isVisibleToAI( obj ))
			continue;
		pp.army += unitValue( obj );
		pp.unitX += obj->getPosition()->x;
		pp.unitY += obj->getPosition()->y;
		++pp.units;
	}
	// Enemies: what the AI has seen of them.
	for (size_t i = 0; i < m_known.size(); ++i)
	{
		const KnownObject &k = m_known[i];
		if (k.owner < 0 || k.owner >= count || !enemy[k.owner])
			continue;
		PlayerPicture &pp = pic[k.owner];
		if (k.structure)
		{
			pp.structX += k.pos.x;
			pp.structY += k.pos.y;
			++pp.structures;
			continue;
		}
		pp.army += k.value;
		pp.unitX += k.pos.x;
		pp.unitY += k.pos.y;
		++pp.units;
	}
	// Pressure: enemy combat units in sight near alliance buildings (second pass, enemies only).
	Real pressureRadiusSqr = PRESSURE_RADIUS * PRESSURE_RADIUS;
	for (Object *obj = TheGameLogic->getFirstObject(); obj && !allianceStructures.empty(); obj = obj->getNextObject())
	{
		Player *owner = obj->getControllingPlayer();
		if (owner == nullptr)
			continue;
		Int idx = owner->getPlayerIndex();
		if (idx < 0 || idx >= count || !enemy[idx])
			continue;
		if (!isMobileCombatUnit( obj ) || !isVisibleToAI( obj ) || !isSeenNow( obj->getPosition() ))
			continue;
		for (size_t s = 0; s < allianceStructures.size(); ++s)
		{
			if (distSqr2D( obj->getPosition(), &allianceStructures[s] ) <= pressureRadiusSqr)
			{
				pic[idx].pressure += unitValue( obj );
				break;
			}
		}
	}

	Real armySum = 0.0f;
	Int enemies = 0;
	for (Int i = 0; i < count; ++i)
	{
		if (enemy[i])
		{
			armySum += pic[i].army;
			++enemies;
		}
	}
	if (enemies == 0)
		return;
	Real avgArmy = armySum / static_cast<Real>(enemies);
	if (avgArmy < 1.0f)
		avgArmy = 1.0f;

	Real bestCost = 0.0f, currentCost = 0.0f;
	Int best = -1;
	Bool currentValid = FALSE;
	for (Int i = 0; i < count; ++i)
	{
		if (!enemy[i])
			continue;
		Coord3D enemyPos;
		if (!pictureCenter( pic[i], &enemyPos ))
			continue;	// nothing visible: no target
		Real distSum = 0.0f;
		Int members = 0;
		for (Int m = 0; m < count; ++m)
		{
			Coord3D memberPos;
			if (!member[m] || !pictureCenter( pic[m], &memberPos ))
				continue;
			distSum += sqrtf( distSqr2D( &memberPos, &enemyPos ) );
			++members;
		}
		Real cost = (members > 0) ? distSum / static_cast<Real>(members) : 0.0f;
		cost += STRENGTH_WEIGHT * pic[i].army / avgArmy;
		cost -= PRESSURE_WEIGHT * pic[i].pressure / (pic[i].pressure + PRESSURE_HALF);
		if (best < 0 || cost < bestCost)
		{
			best = i;
			bestCost = cost;
		}
		if (m_currentEnemy && m_currentEnemy->getPlayerIndex() == i)
		{
			currentValid = TRUE;
			currentCost = cost;
		}
	}
	if (best < 0)
		return;
	Player *bestPlayer = ThePlayerList->getNthPlayer( best );
	if (!currentValid || !isValidEnemy( m_currentEnemy ))
	{
		setEnemy( bestPlayer, "wahl", bestCost );
		return;
	}
	if (bestPlayer == m_currentEnemy)
		return;
	UnsignedInt now = TheGameLogic->getFrame();
	Bool holdOver = now - m_enemyChosenFrame >= ENEMY_HOLD;
	if (bestCost < currentCost - SWITCH_MARGIN && (holdOver || pic[best].pressure >= URGENT_PRESSURE))
		setEnemy( bestPlayer, pic[best].pressure >= URGENT_PRESSURE ? "druck" : "wechsel", bestCost );
}

// ------------------------------------------------------------------------------------------------
// Attack mission with pincer
// ------------------------------------------------------------------------------------------------

// Idle own combat units at home (base radius plus margin), by ObjectID, up to the budget and
// without touching the reserve. Returns the number of chosen units.
Int AICommanderPlayer::collectIdleForce( std::vector<Object *> *chosen, Real budget, Real *chosenValue, Real *sendable ) const
{
	*chosenValue = 0.0f;
	Real army = 0.0f;
	Real onMission = 0.0f;
	Real homeRadius = m_baseRadius + HOME_RADIUS_MARGIN;
	Real homeRadiusSqr = homeRadius * homeRadius;
	std::vector<Object *> idle;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() != m_player || !isMobileCombatUnit( obj ))
			continue;
		Real v = unitValue( obj );
		army += v;
		if (isOnMission( obj->getID() ))
		{
			onMission += v;
			continue;
		}
		if (!isHelperCandidate( obj ) || !obj->getAIUpdateInterface()->isIdle() || isSuperweaponGuard( obj->getID() ))
			continue;
		if (m_baseCenterSet && distSqr2D( obj->getPosition(), &m_baseCenter ) > homeRadiusSqr)
			continue;
		idle.push_back( obj );	// object list: fixed order
	}
	Real limit = army * (1.0f - reserveFraction()) - onMission;
	if (limit > budget)
		limit = budget;
	*sendable = limit;
	for (size_t i = 0; i < idle.size(); ++i)
	{
		if (static_cast<Int>(chosen->size() + m_attackers.size()) >= MAX_ATTACKERS)
			break;
		Real v = unitValue( idle[i] );
		if (*chosenValue + v > limit)
			continue;
		chosen->push_back( idle[i] );
		*chosenValue += v;
	}
	return static_cast<Int>(chosen->size());
}

// Value-weighted centroid of the units; optionally the value within radius around nearPos.
Real AICommanderPlayer::forceCentroid( const std::vector<ObjectID> &ids, Coord3D *centroid, const Coord3D *nearPos, Real radius, Real *valueNear ) const
{
	Real sum = 0.0f, cx = 0.0f, cy = 0.0f, nearSum = 0.0f;
	Real radiusSqr = radius * radius;
	for (size_t i = 0; i < ids.size(); ++i)
	{
		Object *obj = TheGameLogic->findObjectByID( ids[i] );
		if (obj == nullptr || obj->isEffectivelyDead())
			continue;
		Real v = unitValue( obj );
		sum += v;
		cx += obj->getPosition()->x * v;
		cy += obj->getPosition()->y * v;
		if (nearPos && distSqr2D( obj->getPosition(), nearPos ) <= radiusSqr)
			nearSum += v;
	}
	if (centroid)
	{
		centroid->zero();
		if (sum > 0.0f)
		{
			centroid->x = cx / sum;
			centroid->y = cy / sum;
		}
	}
	if (valueNear)
		*valueNear = nearSum;
	return sum;
}

// Nearest known enemy building (lowest ObjectID on a tie); else the centroid of its known units; else
// the nearest unexplored start position.
Bool AICommanderPlayer::findObjective( const Player *enemy, const Coord3D &from, Coord3D *pos, ObjectID *id ) const
{
	const KnownObject *best = nullptr;
	Real bestDist = 0.0f;
	Real ux = 0.0f, uy = 0.0f;
	Int units = 0;
	for (size_t i = 0; i < m_known.size(); ++i)
	{
		const KnownObject &k = m_known[i];
		if (k.owner != enemy->getPlayerIndex())
			continue;
		if (k.structure)
		{
			Real d = distSqr2D( &k.pos, &from );
			if (best == nullptr || d < bestDist || (d == bestDist && k.id < best->id))
			{
				best = &k;
				bestDist = d;
			}
			continue;
		}
		ux += k.pos.x;
		uy += k.pos.y;
		++units;
	}
	if (best)
	{
		*pos = best->pos;
		*id = best->id;
		return TRUE;
	}
	if (units > 0)
	{
		pos->set( ux / static_cast<Real>(units), uy / static_cast<Real>(units), 0.0f );
		pos->z = TheTerrainLogic->getGroundHeight( pos->x, pos->y );
		*id = INVALID_ID;
		return TRUE;
	}
	// nothing known: go and look at the nearest start position nobody has seen yet
	if (nextScoutTarget( from, pos ))
	{
		*id = INVALID_ID;
		return TRUE;
	}
	return FALSE;
}

Bool AICommanderPlayer::startGather( void )
{
	if (!m_baseCenterSet)
		return FALSE;
	Player *enemy = m_currentEnemy;
	if (!isValidEnemy( enemy ))
		return FALSE;
	Coord3D objective;
	ObjectID objectiveID = INVALID_ID;
	if (!findAttackObjective( enemy, m_baseCenter, &objective, &objectiveID ))
		return FALSE;

	// If an allied commander is already gathering against the same enemy, less is enough: better
	// together with it from two sides than alone later (pincer).
	Bool invited = FALSE;
	for (Int i = 0; i < ThePlayerList->getPlayerCount() && !invited; ++i)
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		AICommanderPlayer *c = isHelpableAlly( p ) ? commanderOf( p ) : nullptr;
		invited = c && c->getAttackPhase() == CommanderAttackPhase::GATHER && c->getAttackEnemyIndex() == enemy->getPlayerIndex();
	}
	invited = invited || m_markFrame != 0;	// a human partner marked the target
	std::vector<Object *> chosen;
	Real value = 0.0f, sendable = 0.0f;
	collectIdleForce( &chosen, 1.0e9f, &value, &sendable );
	const Pace &pace = paceFor( getAIDifficulty(), m_personality );
	Real minValue = (invited ? JOIN_MIN_VALUE : ATTACK_MIN_VALUE) * pace.attackScale;
	Int minUnits = static_cast<Int>((invited ? JOIN_MIN_UNITS : ATTACK_MIN_UNITS) * pace.attackScale + 0.5f);
	if (!invited && pace.lateMaxUnits == 0 && !partOff( AI_PART_EARLY_ATTACK ) && TheGameLogic->getFrame() < EARLY_ATTACK_UNTIL)
	{
		minValue = std::min( minValue, EARLY_ATTACK_MIN_VALUE );
		minUnits = std::min( minUnits, EARLY_ATTACK_MIN_UNITS );
	}
	if (m_modelTurtle)
		minValue *= TURTLE_ATTACK_SCALE;	// a fortified enemy needs a bigger force
	if (value < minValue || static_cast<Int>(chosen.size()) < minUnits)
		return FALSE;
	if (!invited && value < m_regrowValue && TheGameLogic->getFrame() < m_regrowUntil)
		return FALSE;

	// Staging point: from the base center towards the target, just beyond the edge of the build
	// list; every AI sets out from its own base, so two allies attack from two sides.
	Coord2D dir;
	dir.x = objective.x - m_baseCenter.x;
	dir.y = objective.y - m_baseCenter.y;
	Real len = dir.length();
	Real stageDist = m_baseRadius + STAGE_MARGIN;
	if (len > 1.0f)
	{
		if (stageDist > len * 0.5f)
			stageDist = len * 0.5f;
		dir.x /= len;
		dir.y /= len;
	}
	else
	{
		dir.x = 0.0f;
		dir.y = 0.0f;
	}
	m_stagePos = m_baseCenter;
	m_stagePos.x += dir.x * stageDist;
	m_stagePos.y += dir.y * stageDist;
	Region3D extent;
	TheTerrainLogic->getExtent( &extent );
	m_stagePos.x = std::clamp( m_stagePos.x, extent.lo.x + 50.0f, extent.hi.x - 50.0f );
	m_stagePos.y = std::clamp( m_stagePos.y, extent.lo.y + 50.0f, extent.hi.y - 50.0f );
	m_stagePos.z = TheTerrainLogic->getGroundHeight( m_stagePos.x, m_stagePos.y );

	UnsignedInt now = TheGameLogic->getFrame();
	m_attackers.clear();
	takeIntoMission( chosen, &m_attackers, m_stagePos );
	m_attackPhase = CommanderAttackPhase::GATHER;
	m_attackPhaseFrame = now;
	m_attackEnemy = enemy->getPlayerIndex();
	m_objectivePos = objective;
	m_objectiveID = objectiveID;
	m_attackStartValue = value;
	m_attackReinforceFrame = now;
	m_gatherReady = FALSE;
	++m_attackMissions;

	char line[256];
	WWFormat::Into(line, "KIANGRIFF {:d} spieler {:d} sammeln gegner {:d} einheiten {:d} wert {:.0f} einsetzbar {:.0f} ort {:.0f} {:.0f}{}", now, m_player->getPlayerIndex(), m_attackEnemy, static_cast<Int>(chosen.size()), value, sendable, m_stagePos.x, m_stagePos.y, invited ? " eingeladen" : "");
	GameEngine_RunnerLog( line );
	return TRUE;
}

void AICommanderPlayer::continueGather( void )
{
	UnsignedInt now = TheGameLogic->getFrame();
	Real value = forceCentroid( m_attackers, nullptr, nullptr, 0.0f, nullptr );
	if (value <= 0.0f)
	{
		endAttack( "verloren" );
		return;
	}
	if (ownBaseThreat() >= MIN_THREAT)
	{
		endAttack( "eigene_basis" );	// the units are at home anyway and defend
		return;
	}
	if (isValidEnemy( m_currentEnemy ))
		m_attackEnemy = m_currentEnemy->getPlayerIndex();
	else
	{
		endAttack( "kein_gegner" );
		return;
	}

	// New idle units join while gathering.
	std::vector<Object *> more;
	Real moreValue = 0.0f, sendable = 0.0f;
	collectIdleForce( &more, 1.0e9f, &moreValue, &sendable );
	if (!more.empty())
		takeIntoMission( more, &m_attackers, m_stagePos );

	// Send stragglers without orders back to the staging point.
	AIGroup *group = nullptr;
	Real stageRadiusSqr = GATHER_RADIUS * GATHER_RADIUS;
	for (size_t i = 0; i < m_attackers.size(); ++i)
	{
		Object *obj = TheGameLogic->findObjectByID( m_attackers[i] );
		if (obj == nullptr || obj->isEffectivelyDead() || obj->getAIUpdateInterface() == nullptr)
			continue;
		if (obj->getAIUpdateInterface()->isIdle() && distSqr2D( obj->getPosition(), &m_stagePos ) > stageRadiusSqr)
			addToGroup( &group, obj );
	}
	if (group)
		group->groupAttackMoveToPosition( &m_stagePos, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );

	Real atStage = 0.0f;
	value = forceCentroid( m_attackers, nullptr, &m_stagePos, GATHER_RADIUS, &atStage );
	UnsignedInt waited = now - m_attackPhaseFrame;
	m_gatherReady = atStage >= GATHER_READY_FRACTION * value || waited >= GATHER_MAX;
	if (!m_gatherReady)
		return;
	// Start protection: set out so that the force arrives when it ends; meanwhile more units join.
	if (graceLeft() > GRACE_LAUNCH_LEAD)
		return;

	// Pincer: if an allied commander just set out against the same enemy, go along at once. If it is
	// still gathering, wait for it (at most GATHER_SYNC_MAX). Otherwise go alone.
	Bool waitForAlly = FALSE;
	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		if (!isHelpableAlly( p ))
			continue;
		AICommanderPlayer *c = commanderOf( p );
		if (c == nullptr || c->getAttackEnemyIndex() != m_attackEnemy)
			continue;
		if (c->getAttackPhase() == CommanderAttackPhase::ATTACK && now - c->getAttackPhaseFrame() <= JOIN_WINDOW)
		{
			launchAttack( "mit_verbuendetem" );
			return;
		}
		if (c->getAttackPhase() == CommanderAttackPhase::GATHER && !c->isGatherReady())
			waitForAlly = TRUE;
		if (c->getAttackPhase() == CommanderAttackPhase::GATHER && c->isGatherReady())
		{
			launchAttack( "zange" );
			return;
		}
	}
	if (waitForAlly && waited < GATHER_SYNC_MAX)
		return;
	// Alone: only if the forecast does not see the target's defenders clearly stronger; meanwhile
	// more units join. If it stays hopeless, give up.
	if (const AIPlanResult *plan = partOff( AI_PART_LAUNCH_GATE ) ? nullptr : planFor( m_attackPhaseFrame ))
	{
		if (plan->objectiveId != 0 && plan->objectiveWinner == 1 && plan->objectiveRatio < LAUNCH_MIN_RATIO)
		{
			if (waited < 2 * GATHER_MAX)
				return;
			endAttack( "zu_schwach" );
			return;
		}
	}
	launchAttack( "allein" );
}

// ------------------------------------------------------------------------------------------------
// Fight control for the units of the own missions (attack and assist). Only what the player knows
// counts as enemy: units in sight, armed buildings once seen, stealthed ones only detected.
Bool AICommanderPlayer::isKnownEnemy( const Object *obj, Player *enemyOf ) const
{
	Player *owner = obj->getControllingPlayer();
	if (owner == nullptr || owner == enemyOf || enemyOf->getDefaultTeam() == nullptr || owner->getDefaultTeam() == nullptr
		|| enemyOf->getRelationship( owner->getDefaultTeam() ) != ENEMIES)
		return FALSE;
	if (!isVisibleToAI( obj ))
		return FALSE;
	const CellShroudStatus seen = ThePartitionManager->getShroudStatusForPlayer( enemyOf->getPlayerIndex(), obj->getPosition() );
	return obj->isKindOf( KINDOF_STRUCTURE ) ? seen != CellShroudStatus::CELLSHROUD_SHROUDED : seen == CellShroudStatus::CELLSHROUD_CLEAR;
}

// Top speed of the current locomotor in the current damage state; 0 for immobile objects.
static Real topSpeed( const Object *obj )
{
	const AIUpdateInterface *ai = obj->getAIUpdateInterface();
	const Locomotor *loco = ai ? ai->getCurLocomotor() : nullptr;
	const BodyModuleInterface *body = obj->getBodyModule();
	if (loco == nullptr || body == nullptr)
		return 0.0f;
	return loco->getMaxSpeedForCondition( body->getDamageState() );
}

void AICommanderPlayer::updateMicro( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_nextMicroFrame)
		return;
	m_nextMicroFrame = now + MICRO_INTERVAL;
	if ((m_attackers.empty() && m_helpers.empty()) || partOff( AI_PART_MICRO ))
		return;
	const AICombat::Table &table = m_planner.getCombatTable();

	// Own fighters: the mission units on the ground and free (not loaded).
	std::vector<AICombat::Fighter> own;
	std::vector<Object *> ownObjects;
	std::vector<Bool> inAttack;
	Real minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
	auto addOwn = [&]( ObjectID id, Bool attack )
	{
		Object *obj = TheGameLogic->findObjectByID( id );
		if (obj == nullptr || obj->isEffectivelyDead() || obj->isContained() || obj->isKindOf( KINDOF_AIRCRAFT )
			|| obj->getAIUpdateInterface() == nullptr)
			return;
		const Int profile = m_planner.profileOf( obj->getTemplate() );
		if (profile < 0)
			return;
		AICombat::Fighter f;
		f.id = static_cast<unsigned>( obj->getID() );
		f.profile = profile;
		f.health = table.profiles[static_cast<size_t>( profile )].maxHealth;
		const BodyModuleInterface *body = obj->getBodyModule();
		if (body && body->getMaxHealth() > 0.0f)
			f.health *= body->getHealth() / body->getMaxHealth();
		f.x = obj->getPosition()->x;
		f.y = obj->getPosition()->y;
		f.speed = topSpeed( obj );
		if (own.empty())
		{
			minX = maxX = f.x;
			minY = maxY = f.y;
		}
		minX = std::min( minX, f.x );
		maxX = std::max( maxX, f.x );
		minY = std::min( minY, f.y );
		maxY = std::max( maxY, f.y );
		own.push_back( f );
		ownObjects.push_back( obj );
		inAttack.push_back( attack );
	};
	if (m_attackPhase == CommanderAttackPhase::ATTACK)
		for (size_t i = 0; i < m_attackers.size(); ++i)
			addOwn( m_attackers[i], TRUE );
	for (size_t i = 0; i < m_helpers.size(); ++i)
		addOwn( m_helpers[i], FALSE );
	if (own.empty())
		return;

	// Known enemies around them; superweapons of any enemy that are (nearly) ready.
	std::vector<AICombat::Fighter> enemies;
	std::vector<Object *> enemyObjects;
	Bool superweaponDanger = FALSE;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->isEffectivelyDead() || obj->isKindOf( KINDOF_PROJECTILE ) || obj->isKindOf( KINDOF_MINE ) || obj->isContained())
			continue;
		if (!isKnownEnemy( obj, m_player ))
			continue;
		if (obj->isKindOf( KINDOF_STRUCTURE ) && AIPlanner_isSuperweapon( obj->getTemplate() ) && !superweaponDanger)
		{
			const SpecialPowerModuleInterface *power = obj->findSpecialPowerModuleInterface( SPECIAL_INVALID );
			if (power && power->getReadyFrame() <= now + SUPERWEAPON_WARNING)
				superweaponDanger = TRUE;
		}
		const Coord3D *pos = obj->getPosition();
		if (pos->x < minX - MICRO_ENEMY_MARGIN || pos->x > maxX + MICRO_ENEMY_MARGIN || pos->y < minY - MICRO_ENEMY_MARGIN || pos->y > maxY + MICRO_ENEMY_MARGIN)
			continue;
		const Int profile = m_planner.profileOf( obj->getTemplate() );
		if (profile < 0)
			continue;
		AICombat::Fighter f;
		f.id = static_cast<unsigned>( obj->getID() );
		f.profile = profile;
		f.health = table.profiles[static_cast<size_t>( profile )].maxHealth;
		const BodyModuleInterface *body = obj->getBodyModule();
		if (body && body->getMaxHealth() > 0.0f)
			f.health *= body->getHealth() / body->getMaxHealth();
		f.x = pos->x;
		f.y = pos->y;
		f.speed = topSpeed( obj );
		enemies.push_back( f );
		enemyObjects.push_back( obj );
	}

	std::vector<Bool> moved( own.size(), FALSE );

	// Spread the attack group against area damage or an imminent superweapon.
	if (now >= m_nextSpreadFrame)
	{
		std::vector<size_t> members;
		std::vector<AICombat::Fighter> group;
		Real cx = 0.0f, cy = 0.0f;
		for (size_t i = 0; i < own.size(); ++i)
		{
			if (!inAttack[i])
				continue;
			members.push_back( i );
			group.push_back( own[i] );
			cx += own[i].x;
			cy += own[i].y;
		}
		Real spacing = 0.0f;
		if (!group.empty())
		{
			cx /= static_cast<Real>( group.size() );
			cy /= static_cast<Real>( group.size() );
			Real splashDps = 0.0f, allDps = 0.0f, splash = 0.0f;
			for (const AICombat::Fighter &e : enemies)
			{
				const Real dx = e.x - cx, dy = e.y - cy;
				const AICombat::Profile &p = table.profiles[static_cast<size_t>( e.profile )];
				if (dx * dx + dy * dy > (GROUP_RADIUS + p.range) * (GROUP_RADIUS + p.range))
					continue;
				const Real dps = AICombat::groundThreat( table, e.profile );
				allDps += dps;
				if (p.splash >= SPLASH_MIN)
				{
					splashDps += dps;
					splash = std::max( splash, p.splash );
				}
			}
			if (allDps > 0.0f && splashDps >= SPLASH_SHARE * allDps)
				spacing = splash * SPLASH_SPACING_FACTOR;
			if (superweaponDanger)
				spacing = std::max( spacing, SUPERWEAPON_SPACING );
		}
		if (spacing > 0.0f && AICombat::isClumped( group, spacing ))
		{
			const std::vector<AICombat::Fighter> spots = AICombat::spreadPositions( group, spacing );
			for (size_t k = 0; k < members.size(); ++k)
			{
				Coord3D goal;
				goal.set( spots[k].x, spots[k].y, TheTerrainLogic->getGroundHeight( spots[k].x, spots[k].y ) );
				ownObjects[members[k]]->getAIUpdateInterface()->aiMoveToPosition( &goal, CMD_FROM_AI );
				moved[members[k]] = TRUE;
			}
			m_nextSpreadFrame = now + SPREAD_INTERVAL;
			m_microSpread += static_cast<Int>( members.size() );
			char line[192];
			WWFormat::Into(line, "KIGEFECHT {:d} spieler {:d} verteilen einheiten {:d} abstand {:.0f} superwaffe {:d}", now, m_player->getPlayerIndex(), static_cast<Int>( members.size() ), spacing, superweaponDanger ? 1 : 0);
			GameEngine_RunnerLog( line );
		}
	}

	// Keep distance from short-ranged threats.
	for (size_t i = 0; i < own.size(); ++i)
	{
		if (moved[i])
			continue;
		Real x, y;
		if (AICombat::kiteStep( table, own[i], enemies, &x, &y ))
		{
			Coord3D goal;
			goal.set( x, y, TheTerrainLogic->getGroundHeight( x, y ) );
			ownObjects[i]->getAIUpdateInterface()->aiMoveToPosition( &goal, CMD_FROM_AI );
			moved[i] = TRUE;
			++m_microKite;
		}
	}

	// Focus fire with the rest.
	std::vector<AICombat::Fighter> shooters;
	std::vector<size_t> shooterIndex;
	for (size_t i = 0; i < own.size(); ++i)
	{
		if (!moved[i])
		{
			shooters.push_back( own[i] );
			shooterIndex.push_back( i );
		}
	}
	const std::vector<AICombat::FocusAssignment> plan = AICombat::assignFocusFire( table, shooters, enemies, FOCUS_APPROACH, FOCUS_KILL_SECONDS );
	for (const AICombat::FocusAssignment &a : plan)
	{
		Object *shooter = nullptr;
		for (size_t k = 0; k < shooters.size() && shooter == nullptr; ++k)
			if (shooters[k].id == a.shooter)
				shooter = ownObjects[shooterIndex[k]];
		Object *target = nullptr;
		for (size_t k = 0; k < enemies.size() && target == nullptr; ++k)
			if (enemies[k].id == a.target)
				target = enemyObjects[k];
		if (shooter == nullptr || target == nullptr)
			continue;
		AIUpdateInterface *ai = shooter->getAIUpdateInterface();
		if (ai->getGoalObject() == target || ai->getCurrentVictim() == target)
			continue;
		ai->aiAttackObject( target, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		++m_microFocus;
	}

	if (now >= m_nextMicroReportFrame && (m_microFocus + m_microKite + m_microSpread) > 0)
	{
		m_nextMicroReportFrame = now + 10 * LOGICFRAMES_PER_SECOND;
		char line[192];
		WWFormat::Into(line, "KIGEFECHT {:d} spieler {:d} lage fokus {:d} abstand {:d} verteilt {:d} eigene {:d} feinde {:d}", now, m_player->getPlayerIndex(), m_microFocus, m_microKite, m_microSpread, static_cast<Int>( own.size() ), static_cast<Int>( enemies.size() ));
		GameEngine_RunnerLog( line );
		m_microFocus = m_microKite = m_microSpread = 0;
	}
}

// ------------------------------------------------------------------------------------------------
// Raids with transports. A squad is a transport that lets its riders fire from inside plus as many
// riders as fit (at most RAID_MAX_RIDERS); the planner picks the enemy's workers or supply centres
// as targets. The squad falls back when the transport is damaged, empty or the fight on site is
// forecast lost, and is kept for the next raid.
Bool AICommanderPlayer::isRaider( ObjectID id ) const
{
	if (id == INVALID_ID)
		return FALSE;
	return id == m_raidTransport || std::find( m_raiders.begin(), m_raiders.end(), id ) != m_raiders.end();
}

Int AICommanderPlayer::ridersAboard( void ) const
{
	Int aboard = 0;
	for (size_t i = 0; i < m_raiders.size(); ++i)
	{
		Object *obj = TheGameLogic->findObjectByID( m_raiders[i] );
		if (obj && !obj->isEffectivelyDead() && obj->getContainedBy() && obj->getContainedBy()->getID() == m_raidTransport)
			++aboard;
	}
	return aboard;
}

// Transport (cheapest that lets riders fire, with at least RAID_MIN_SLOTS slots) and rider (the
// infantry the planner ranks best against the enemy mix, otherwise the most damage per money) the
// own factories may build now.
Bool AICommanderPlayer::findRaidTemplates( const ThingTemplate **transport, Object **transportFactory, const ThingTemplate **rider,
	Object **riderFactory, Int *riders ) const
{
	*transport = nullptr;
	*rider = nullptr;
	*transportFactory = nullptr;
	*riderFactory = nullptr;
	*riders = 0;
	Int slots = 0;
	Int transportCost = 0;
	Real riderScore = 0.0f;
	const AICombat::Table &table = m_planner.getCombatTable();
	for (Object *factory = TheGameLogic->getFirstObject(); factory; factory = factory->getNextObject())
	{
		if (factory->getControllingPlayer() != m_player || !factory->isKindOf( KINDOF_STRUCTURE ) || factory->isEffectivelyDead()
			|| factory->getProductionUpdateInterface() == nullptr)
			continue;
		const CommandSet *set = TheControlBar->findCommandSet( factory->getCommandSetString() );
		if (set == nullptr)
			continue;
		for (Int j = 0; j < MAX_COMMANDS_PER_SET; ++j)
		{
			const CommandButton *button = set->getCommandButton( j );
			if (button == nullptr || button->getCommandType() != GUI_COMMAND_UNIT_BUILD || !isButtonAllowed( button, factory ))
				continue;
			const ThingTemplate *t = button->getThingTemplate();
			if (t == nullptr)
				continue;
			if (t->isKindOf( KINDOF_TRANSPORT ) && t->isKindOf( KINDOF_VEHICLE ) && !t->isKindOf( KINDOF_AIRCRAFT ))
			{
				const ModuleInfo &modules = t->getBehaviorModuleInfo();
				for (Int m = 0; m < modules.getCount(); ++m)
				{
					if (modules.getNthName( m ).compareNoCase( "TransportContain" ) != 0)
						continue;
					const TransportContainModuleData *data = static_cast<const TransportContainModuleData *>( modules.getNthData( m ) );
					const Int cost = t->calcCostToBuild( m_player );
					if (data->m_passengersAllowedToFire && data->m_slotCapacity >= RAID_MIN_SLOTS
						&& (*transport == nullptr || cost < transportCost))
					{
						*transport = t;
						*transportFactory = factory;
						transportCost = cost;
						slots = data->m_slotCapacity;
					}
					break;
				}
			}
			else if (t->isKindOf( KINDOF_INFANTRY ) && isArmyTemplate( t ))
			{
				const Int profile = m_planner.profileOf( t );
				const Int cost = t->calcCostToBuild( m_player );
				Real score = m_planner.counterScoreOf( profile );
				if (score <= 0.0f && cost > 0)
					score = 1.0e-6f * AICombat::groundThreat( table, profile ) / static_cast<Real>( cost );	// below any counter score
				if (score > 0.0f && (*rider == nullptr || score > riderScore))
				{
					*rider = t;
					*riderFactory = factory;
					riderScore = score;
				}
			}
		}
	}
	if (*transport == nullptr || *rider == nullptr)
		return FALSE;
	const Int perRider = std::max( 1, (*rider)->getRawTransportSlotCount() );
	*riders = std::min( RAID_MAX_RIDERS, slots / perRider );
	return *riders > 0;
}

Bool AICommanderPlayer::startRaidProduction( void )
{
	const ThingTemplate *transport, *rider;
	Object *transportFactory, *riderFactory;
	Int riders;
	if (!findRaidTemplates( &transport, &transportFactory, &rider, &riderFactory, &riders ))
		return FALSE;
	const Int cost = transport->calcCostToBuild( m_player ) + riders * rider->calcCostToBuild( m_player );
	if (freeMoney() - cost < RAID_KEEP_MONEY)
		return FALSE;
	ProductionUpdateInterface *tp = transportFactory->getProductionUpdateInterface();
	ProductionUpdateInterface *rp = riderFactory->getProductionUpdateInterface();
	if (TheBuildAssistant->canMakeUnit( transportFactory, transport ) != CanMakeType::CANMAKE_OK
		|| !tp->queueCreateUnit( transport, tp->requestUniqueUnitID() ))
		return FALSE;
	Int queued = 0;
	for (Int i = 0; i < riders; ++i)
	{
		if (TheBuildAssistant->canMakeUnit( riderFactory, rider ) != CanMakeType::CANMAKE_OK
			|| !rp->queueCreateUnit( rider, rp->requestUniqueUnitID() ))
			break;
		++queued;
	}
	m_raidTransportName = transport->getName();
	m_raidRiderName = rider->getName();
	m_raidRidersWanted = std::max( 1, queued );
	m_raidPhase = CommanderRaidPhase::ASSEMBLE;
	m_raidPhaseFrame = TheGameLogic->getFrame();
	char line[256];
	WWFormat::Into(line, "KIUEBERFALL {:d} spieler {:d} bau transport {} fahrer {} anzahl {:d} kosten {:d}", m_raidPhaseFrame, m_player->getPlayerIndex(), transport->getName().str(), rider->getName().str(), queued, cost);
	GameEngine_RunnerLog( line );
	return TRUE;
}

void AICommanderPlayer::continueRaidAssemble( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	Object *transport = (m_raidTransport != INVALID_ID) ? TheGameLogic->findObjectByID( m_raidTransport ) : nullptr;
	if (m_raidTransport != INVALID_ID && (transport == nullptr || transport->isEffectivelyDead()))
	{
		endRaid( "verloren" );
		return;
	}

	// Take the new squad units: idle, near the base, of the ordered templates, not on a mission.
	const Real claim = m_baseRadius + RAID_CLAIM_RANGE;
	Team *defaultTeam = m_player->getDefaultTeam();
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() != m_player || obj->isEffectivelyDead() || obj->isContained() || obj->getAIUpdateInterface() == nullptr
			|| !obj->getAIUpdateInterface()->isIdle() || isOnMission( obj->getID() ))
			continue;
		if (m_baseCenterSet && distSqr2D( obj->getPosition(), &m_baseCenter ) > claim * claim)
			continue;
		const AsciiString &name = obj->getTemplate()->getName();
		Bool take = FALSE;
		if (m_raidTransport == INVALID_ID && name == m_raidTransportName)
		{
			m_raidTransport = obj->getID();
			transport = obj;
			take = TRUE;
		}
		else if (static_cast<Int>( m_raiders.size() ) < m_raidRidersWanted && name == m_raidRiderName)
		{
			m_raiders.push_back( obj->getID() );
			take = TRUE;
		}
		if (take)
		{
			if (defaultTeam && obj->getTeam() != defaultTeam)
				obj->setTeam( defaultTeam );
			obj->getAIUpdateInterface()->setIsRecruitable( FALSE );
		}
	}

	// Riders board the transport.
	if (transport)
	{
		AIGroup *board = nullptr;
		for (size_t i = 0; i < m_raiders.size(); ++i)
		{
			Object *obj = TheGameLogic->findObjectByID( m_raiders[i] );
			if (obj && !obj->isEffectivelyDead() && !obj->isContained() && obj->getAIUpdateInterface() && obj->getAIUpdateInterface()->isIdle())
				addToGroup( &board, obj );
		}
		if (board)
			board->groupEnter( transport, CMD_FROM_AI );
	}

	const Int aboard = (transport != nullptr) ? ridersAboard() : 0;
	const Bool full = transport != nullptr && aboard >= m_raidRidersWanted;
	const Bool late = now - m_raidPhaseFrame >= RAID_ASSEMBLE_MAX;
	if (full || (late && aboard > 0))
	{
		m_raidPhase = CommanderRaidPhase::RAID;
		m_raidPhaseFrame = now;
		m_raidTarget = INVALID_ID;
		++m_raids;
		char line[192];
		WWFormat::Into(line, "KIUEBERFALL {:d} spieler {:d} los fahrer {:d}", now, m_player->getPlayerIndex(), aboard);
		GameEngine_RunnerLog( line );
	}
	else if (late)
		endRaid( "sammeln_zeit" );
}

void AICommanderPlayer::continueRaid( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	Object *transport = TheGameLogic->findObjectByID( m_raidTransport );
	if (transport == nullptr || transport->isEffectivelyDead())
	{
		endRaid( "verloren" );
		return;
	}
	const BodyModuleInterface *body = transport->getBodyModule();
	if (body && body->getMaxHealth() > 0.0f && body->getHealth() < RAID_RETREAT_HEALTH * body->getMaxHealth())
	{
		returnRaid( "beschaedigt" );
		return;
	}
	if (ridersAboard() == 0)
	{
		returnRaid( "leer" );
		return;
	}
	if (now - m_raidPhaseFrame > RAID_MAX)
	{
		returnRaid( "zeit" );
		return;
	}
	const AIPlanResult *plan = planFor( m_raidPhaseFrame );
	if (plan && plan->raidSiteEnemyHealth > 0 && plan->raidSiteWinner == 1)
	{
		returnRaid( "prognose" );
		return;
	}

	Object *target = (m_raidTarget != INVALID_ID) ? TheGameLogic->findObjectByID( m_raidTarget ) : nullptr;
	const Bool gone = target == nullptr || target->isEffectivelyDead() || target->getControllingPlayer() == m_player;
	Bool order = transport->getAIUpdateInterface() && transport->getAIUpdateInterface()->isIdle();
	// A partner's attack mark names the raid target too: what is known of the enemy there.
	Object *marked = nullptr;
	Player *markEnemy = (m_markFrame != 0 && m_markEnemy >= 0) ? ThePlayerList->getNthPlayer( m_markEnemy ) : nullptr;
	Coord3D markedPos;
	ObjectID markedId = INVALID_ID;
	if (markEnemy && isValidEnemy( markEnemy ) && findMarkObjective( markEnemy, &markedPos, &markedId ))
	{
		marked = TheGameLogic->findObjectByID( markedId );
		if (marked && (marked->isEffectivelyDead() || marked->getControllingPlayer() != markEnemy))
			marked = nullptr;
	}
	if (marked && marked->getID() != m_raidTarget)
	{
		m_raidTarget = marked->getID();
		m_raidTargetFrame = now;
		target = marked;
		order = TRUE;
		char line[192];
		WWFormat::Into(line, "KIUEBERFALL {:d} spieler {:d} ziel {} id {:d} markierung", now, m_player->getPlayerIndex(), marked->getTemplate()->getName().str(), static_cast<Int>( marked->getID() ));
		GameEngine_RunnerLog( line );
	}
	else if (marked == nullptr && plan && plan->raidTargetId != 0 && static_cast<ObjectID>( plan->raidTargetId ) != m_raidTarget
		&& (gone || now - m_raidTargetFrame >= RAID_TARGET_HOLD))
	{
		Object *next = TheGameLogic->findObjectByID( static_cast<ObjectID>( plan->raidTargetId ) );
		if (next && !next->isEffectivelyDead())
		{
			m_raidTarget = next->getID();
			m_raidTargetFrame = now;
			target = next;
			order = TRUE;
			char line[224];
			WWFormat::Into(line, "KIUEBERFALL {:d} spieler {:d} ziel {} id {:d} sieger {:d} verhaeltnis {:.2f}", now, m_player->getPlayerIndex(), next->getTemplate()->getName().str(), static_cast<Int>( next->getID() ), plan->raidTargetWinner, plan->raidTargetRatio);
			GameEngine_RunnerLog( line );
		}
	}
	if (target == nullptr || target->isEffectivelyDead() || target->getControllingPlayer() == m_player)
	{
		// Target gone: the next plan names a new one; none left means the raid is over.
		if (marked == nullptr && plan && plan->raidTargetId == 0)
			returnRaid( "kein_ziel" );
		return;
	}
	if (order)
	{
		AIGroup *group = nullptr;
		addToGroup( &group, transport );
		if (group)
			group->groupAttackMoveToPosition( target->getPosition(), NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
	}
}

void AICommanderPlayer::returnRaid( const char *reason )
{
	m_raidPhase = CommanderRaidPhase::RETURN;
	m_raidPhaseFrame = TheGameLogic->getFrame();
	m_raidTarget = INVALID_ID;
	Object *transport = TheGameLogic->findObjectByID( m_raidTransport );
	if (transport && m_baseCenterSet)
	{
		AIGroup *group = nullptr;
		addToGroup( &group, transport );
		if (group)
			group->groupMoveToPosition( &m_baseCenter, FALSE, CMD_FROM_AI );
	}
	char line[160];
	WWFormat::Into(line, "KIUEBERFALL {:d} spieler {:d} rueckzug {}", m_raidPhaseFrame, m_player->getPlayerIndex(), reason);
	GameEngine_RunnerLog( line );
}

void AICommanderPlayer::continueRaidReturn( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	Object *transport = TheGameLogic->findObjectByID( m_raidTransport );
	if (transport == nullptr || transport->isEffectivelyDead())
	{
		endRaid( "verloren" );
		return;
	}
	const Real home = m_baseRadius + RAID_HOME_MARGIN;
	if (!m_baseCenterSet || distSqr2D( transport->getPosition(), &m_baseCenter ) <= home * home || now - m_raidPhaseFrame > RAID_MAX)
	{
		m_raidPhase = CommanderRaidPhase::NONE;
		m_raidCooldownFrame = now + RAID_COOLDOWN;
		char line[160];
		WWFormat::Into(line, "KIUEBERFALL {:d} spieler {:d} heim fahrer {:d}", now, m_player->getPlayerIndex(), ridersAboard());
		GameEngine_RunnerLog( line );
		return;
	}
	if (transport->getAIUpdateInterface() && transport->getAIUpdateInterface()->isIdle())
	{
		AIGroup *group = nullptr;
		addToGroup( &group, transport );
		if (group)
			group->groupMoveToPosition( &m_baseCenter, FALSE, CMD_FROM_AI );
	}
}

// The squad breaks up: riders get out, all units go back to the classic AI.
void AICommanderPlayer::endRaid( const char *reason )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	Object *transport = (m_raidTransport != INVALID_ID) ? TheGameLogic->findObjectByID( m_raidTransport ) : nullptr;
	if (transport && !transport->isEffectivelyDead() && ridersAboard() > 0)
	{
		AIGroup *group = nullptr;
		addToGroup( &group, transport );
		if (group)
			group->groupEvacuate( CMD_FROM_AI );
	}
	std::vector<ObjectID> all = m_raiders;
	if (m_raidTransport != INVALID_ID)
		all.push_back( m_raidTransport );
	releaseUnits( &all );
	m_raiders.clear();
	m_raidTransport = INVALID_ID;
	m_raidTarget = INVALID_ID;
	m_raidPhase = CommanderRaidPhase::NONE;
	m_raidCooldownFrame = now + RAID_COOLDOWN;
	char line[160];
	WWFormat::Into(line, "KIUEBERFALL {:d} spieler {:d} ende {}", now, m_player->getPlayerIndex(), reason);
	GameEngine_RunnerLog( line );
}

void AICommanderPlayer::updateRaid( void )
{
	if (getAIDifficulty() == DIFFICULTY_EASY || partOff( AI_PART_RAIDS ))
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	switch (m_raidPhase)
	{
		case CommanderRaidPhase::ASSEMBLE:
			continueRaidAssemble();
			return;
		case CommanderRaidPhase::RAID:
			continueRaid();
			return;
		case CommanderRaidPhase::RETURN:
			continueRaidReturn();
			return;
		default:
			break;
	}
	if (now < m_raidCooldownFrame || TheGameLogic->isGracePeriod())
		return;
	// A squad from an earlier raid goes again; without riders it breaks up.
	if (m_raidTransport != INVALID_ID)
	{
		Object *transport = TheGameLogic->findObjectByID( m_raidTransport );
		if (transport && !transport->isEffectivelyDead() && ridersAboard() > 0)
		{
			m_raidPhase = CommanderRaidPhase::RAID;
			m_raidPhaseFrame = now;
			m_raidTarget = INVALID_ID;
			++m_raids;
			char line[160];
			WWFormat::Into(line, "KIUEBERFALL {:d} spieler {:d} los fahrer {:d}", now, m_player->getPlayerIndex(), ridersAboard());
			GameEngine_RunnerLog( line );
			return;
		}
		endRaid( "aufgeloest" );
		return;
	}
	if (now < RAID_START || !isValidEnemy( m_currentEnemy ))
		return;
	startRaidProduction();
}

// ------------------------------------------------------------------------------------------------
// Superweapons and general powers (fired by the EA scripts, which ask for the target here).
// Only what the player knows counts: enemy buildings once seen, units while in sight, stealthed
// ones only detected. Mines and the sneak attack keep the placement of the classic AI.
Bool AICommanderPlayer::computeSuperweaponTarget( const SpecialPowerTemplate *power, Coord3D *pos, Int playerNdx, Real weaponRadius )
{
	const SpecialPowerType type = power->getSpecialPowerType();
	Player *target = ThePlayerList->getNthPlayer( playerNdx );
	if (type == SPECIAL_CLUSTER_MINES || type == NUKE_SPECIAL_CLUSTER_MINES || type == SPECIAL_SNEAK_ATTACK || target == nullptr
		|| partOff( AI_PART_SUPERWEAPON ))
		return AISkirmishPlayer::computeSuperweaponTarget( power, pos, playerNdx, weaponRadius );

	const Real radius = std::max( weaponRadius, 4.0f * PATHFIND_CELL_SIZE_F );
	Coord3D base;
	const Bool hasBase = getBaseCenter( &base );
	const Real approach = m_baseRadius + STRIKE_APPROACH_RANGE;
	const Int self = m_player->getPlayerIndex();
	std::vector<AICombat::StrikePoint> points;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->isEffectivelyDead() || obj->isKindOf( KINDOF_PROJECTILE ) || obj->isKindOf( KINDOF_MINE ) || obj->isKindOf( KINDOF_IGNORED_IN_GUI ))
			continue;
		Player *owner = obj->getControllingPlayer();
		const Bool structure = obj->isKindOf( KINDOF_STRUCTURE );
		Real value = static_cast<Real>( std::max( obj->getTemplate()->friend_getBuildCost(), 50 ) );
		const BodyModuleInterface *body = obj->getBodyModule();
		if (body && body->getMaxHealth() > 0.0f)
			value *= body->getHealth() / body->getMaxHealth();
		if (owner == target)
		{
			if (!isVisibleToAI( obj ))
				continue;
			const CellShroudStatus seen = ThePartitionManager->getShroudStatusForPlayer( self, obj->getPosition() );
			if (structure ? seen == CellShroudStatus::CELLSHROUD_SHROUDED : seen != CellShroudStatus::CELLSHROUD_CLEAR)
				continue;
			if (!structure && hasBase && distSqr2D( obj->getPosition(), &base ) <= approach * approach)
				value *= STRIKE_APPROACH_FACTOR;
			if (structure && obj->isAbleToAttack() && m_attackPhase != CommanderAttackPhase::NONE
				&& distSqr2D( obj->getPosition(), &m_objectivePos ) <= STRIKE_DEFENCE_RANGE * STRIKE_DEFENCE_RANGE)
				value *= STRIKE_DEFENCE_FACTOR;
			if (structure && AIPlanner_isSuperweapon( obj->getTemplate() ))
				value *= STRIKE_SUPERWEAPON_FACTOR;
		}
		else if (owner == m_player || isHelpableAlly( owner ))
			value *= -STRIKE_FRIENDLY_FACTOR;
		else
			continue;
		AICombat::StrikePoint p;
		p.id = static_cast<unsigned>( obj->getID() );
		p.x = obj->getPosition()->x;
		p.y = obj->getPosition()->y;
		p.value = value;
		points.push_back( p );
	}
	const AICombat::StrikeChoice choice = AICombat::bestStrike( points, radius );
	if (!choice.found)
		return AISkirmishPlayer::computeSuperweaponTarget( power, pos, playerNdx, weaponRadius );
	// Timing: the scripts ask as soon as the power is ready. Waiting returns a zero position, which the
	// script action does not fire at (FALSE would assert in debug builds on every script pass). Generals
	// scripts fire at the returned position unchecked, so there the power goes at once.
	const UnsignedInt now = TheGameLogic->getFrame();
	const UnsignedInt held = (m_strikeHeldSince != 0) ? now - m_strikeHeldSince : 0;
	if (!TheGlobalData->m_scriptsLikeGenerals && !AICombat::fireStrikeNow( choice.value, m_attackPhase == CommanderAttackPhase::ATTACK, held, AICombat::StrikeTiming() ))
	{
		if (m_strikeHeldSince == 0)
		{
			m_strikeHeldSince = now;
			char wait[192];
			WWFormat::Into(wait, "KISUPERWAFFE {:d} spieler {:d} macht {} warten wert {:.0f}", now, self, power->getName().str(), choice.value);
			GameEngine_RunnerLog( wait );
		}
		pos->zero();
		return TRUE;
	}
	m_strikeHeldSince = 0;
	pos->set( choice.x, choice.y, TheTerrainLogic->getGroundHeight( choice.x, choice.y ) );
	char line[256];
	WWFormat::Into(line, "KISUPERWAFFE {:d} spieler {:d} macht {} gegner {:d} ziel_id {:d} wert {:.0f} radius {:.0f}", TheGameLogic->getFrame(), self, power->getName().str(), playerNdx, static_cast<Int>( choice.id ), choice.value, radius);
	GameEngine_RunnerLog( line );
	return TRUE;
}

// Last planner result if it was computed after `since` (and so knows the current mission).
const AIPlanResult *AICommanderPlayer::planFor( UnsignedInt since ) const
{
	if (!m_planner.hasResult() || m_planner.getResult().frame < since)
		return nullptr;
	return &m_planner.getResult();
}

void AICommanderPlayer::launchAttack( const char *reason )
{
	UnsignedInt now = TheGameLogic->getFrame();
	Coord3D centroid;
	Real value = forceCentroid( m_attackers, &centroid, nullptr, 0.0f, nullptr );
	Player *enemy = ThePlayerList->getNthPlayer( m_attackEnemy );
	if (enemy == nullptr || !findAttackObjective( enemy, centroid, &m_objectivePos, &m_objectiveID ))
	{
		endAttack( "kein_ziel" );
		return;
	}
	m_attackPhase = CommanderAttackPhase::ATTACK;
	m_attackPhaseFrame = now;
	m_attackStartValue = value;
	m_attackReinforceFrame = now;
	m_gatherReady = FALSE;
	m_forecastBad = 0;

	AIGroup *group = nullptr;
	for (size_t i = 0; i < m_attackers.size(); ++i)
	{
		Object *obj = TheGameLogic->findObjectByID( m_attackers[i] );
		if (obj && !obj->isEffectivelyDead() && obj->getAIUpdateInterface())
			addToGroup( &group, obj );
	}
	if (group)
		group->groupAttackMoveToPosition( &m_objectivePos, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );

	char line[256];
	WWFormat::Into(line, "KIANGRIFF {:d} spieler {:d} los {} gegner {:d} einheiten {:d} wert {:.0f} ziel {:.0f} {:.0f}", now, m_player->getPlayerIndex(), reason, m_attackEnemy, static_cast<Int>(m_attackers.size()), value, m_objectivePos.x, m_objectivePos.y);
	GameEngine_RunnerLog( line );
	if (m_markFrame == 0 && !(m_ownBeacon != INVALID_ID && m_ownBeaconKind == OWN_BEACON_HELP) && hasHumanPartner())
		placeOwnBeacon( m_objectivePos, OWN_BEACON_ATTACK );
}

void AICommanderPlayer::continueAttack( void )
{
	UnsignedInt now = TheGameLogic->getFrame();
	Coord3D centroid;
	Real value = forceCentroid( m_attackers, &centroid, nullptr, 0.0f, nullptr );
	if (value <= 0.0f)
	{
		endAttack( "verloren" );
		return;
	}
	if (now - m_attackPhaseFrame > ATTACK_TIMEOUT)
	{
		endAttack( "zeit" );
		return;
	}

	// Home in danger: return on a strong threat the units left at home cannot handle.
	Real homeThreat = ownBaseThreat();
	if (homeThreat >= (partOff( AI_PART_DEFENCE ) ? BASE_RECALL_THREAT : DEFENCE_RECALL_THREAT))
	{
		std::vector<Object *> dummy;
		Real homeValue = 0.0f, sendable = 0.0f;
		collectIdleForce( &dummy, 1.0e9f, &homeValue, &sendable );
		if (homeValue < homeThreat)
		{
			endAttack( "eigene_basis" );
			return;
		}
	}

	// Forecast: the fight where the force stands is lost. A single plan may catch the force while it
	// is still strung out, and the model ignores terrain and reinforcements, so the forecast must
	// hold over several plans and be confirmed by losses -- unless it is hopeless.
	const AIPlanResult *plan = planFor( m_attackPhaseFrame );
	if (plan && plan->frame != m_forecastPlanFrame)
	{
		m_forecastPlanFrame = plan->frame;
		const Bool lost = plan->siteEnemyHealth > 0 && plan->siteWinner == 1 && plan->siteRatio < RETREAT_FORECAST_RATIO;
		m_forecastBad = lost ? m_forecastBad + 1 : 0;
	}
	const Bool confirm = !partOff( AI_PART_CONFIRM );
	if (plan && !partOff( AI_PART_FORECAST_RETREAT ) && m_forecastBad >= (confirm ? RETREAT_FORECAST_PLANS : 1)
		&& (!confirm || value < RETREAT_FORECAST_LOSS * m_attackStartValue || plan->siteRatio < RETREAT_HOPELESS_RATIO))
	{
		endAttack( "prognose" );
		return;
	}

	// Losses: below RETREAT_FRACTION of the start value and outnumbered on site, retreat.
	if (value < RETREAT_FRACTION * m_attackStartValue)
	{
		Real local = enemyStrengthNear( m_player, &centroid, LOCAL_RADIUS, nullptr );
		if (local > RETREAT_ODDS * value)
		{
			endAttack( "rueckzug" );
			return;
		}
	}

	// Track the target: the common enemy may change or be defeated.
	if (isValidEnemy( m_currentEnemy ) && m_currentEnemy->getPlayerIndex() != m_attackEnemy)
	{
		m_attackEnemy = m_currentEnemy->getPlayerIndex();
		m_objectiveID = INVALID_ID;
		m_objectivePos.zero();
	}
	Player *enemy = ThePlayerList->getNthPlayer( m_attackEnemy );
	if (!isValidEnemy( enemy ))
	{
		endAttack( "gegner_besiegt" );
		return;
	}
	Bool objectiveGone = TRUE;
	if (m_objectiveID != INVALID_ID)
	{
		Object *target = TheGameLogic->findObjectByID( m_objectiveID );
		objectiveGone = target == nullptr || target->isEffectivelyDead() || target->getControllingPlayer() != enemy;
	}
	// Target from the planner (value, defenders, threat on the way); held a while to avoid wavering.
	Bool retarget = FALSE;
	if (plan && m_markFrame == 0 && !partOff( AI_PART_PLANNER_TARGET ) && plan->objectiveId != 0 && static_cast<ObjectID>( plan->objectiveId ) != m_objectiveID
		&& (objectiveGone || now - m_objectiveFrame >= OBJECTIVE_HOLD))
	{
		Object *target = TheGameLogic->findObjectByID( static_cast<ObjectID>( plan->objectiveId ) );
		if (target && !target->isEffectivelyDead() && target->getControllingPlayer() == enemy)
		{
			m_objectiveID = target->getID();
			m_objectivePos = *target->getPosition();
			m_objectiveFrame = now;
			objectiveGone = FALSE;
			retarget = TRUE;
			char line[256];
			WWFormat::Into(line, "KIZIEL {:d} spieler {:d} ziel {} id {:d} sieger {:d} verhaeltnis {:.2f} weg_bedrohung {:.0f}", now, m_player->getPlayerIndex(), target->getTemplate()->getName().str(), static_cast<Int>( target->getID() ), plan->objectiveWinner, plan->objectiveRatio, plan->objectiveThreat);
			GameEngine_RunnerLog( line );
		}
	}
	if (objectiveGone)
	{
		if (!findAttackObjective( enemy, centroid, &m_objectivePos, &m_objectiveID ))
		{
			endAttack( "kein_ziel" );
			return;
		}
		m_objectiveFrame = now;
	}

	// Units without orders: hunt near the target (units and buildings nearby), otherwise
	// attack-move to the target.
	AIGroup *huntGroup = nullptr;
	AIGroup *moveGroup = nullptr;
	Real huntRadiusSqr = HUNT_RADIUS * HUNT_RADIUS;
	for (size_t i = 0; i < m_attackers.size(); ++i)
	{
		Object *obj = TheGameLogic->findObjectByID( m_attackers[i] );
		if (obj == nullptr || obj->isEffectivelyDead() || obj->getAIUpdateInterface() == nullptr)
			continue;
		const Bool distant = distSqr2D( obj->getPosition(), &m_objectivePos ) > huntRadiusSqr;
		// A new target redirects the units still on their way; those fighting near it stay.
		if (!obj->getAIUpdateInterface()->isIdle() && !(retarget && distant))
			continue;
		if (!distant)
			addToGroup( &huntGroup, obj );
		else
			addToGroup( &moveGroup, obj );
	}
	if (huntGroup)
		huntGroup->groupHunt( CMD_FROM_AI );
	if (moveGroup)
		moveGroup->groupAttackMoveToPosition( &m_objectivePos, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );

	// Reinforcements: new idle units at home follow in batches.
	if (now - m_attackReinforceFrame >= ATTACK_REINFORCE_INTERVAL)
	{
		std::vector<Object *> more;
		Real moreValue = 0.0f, sendable = 0.0f;
		collectIdleForce( &more, 1.0e9f, &moreValue, &sendable );
		if (moreValue >= ATTACK_REINFORCE_VALUE)
		{
			takeIntoMission( more, &m_attackers, centroid );
			m_attackReinforceFrame = now;
			m_attackStartValue += moreValue;
			char line[192];
			WWFormat::Into(line, "KIANGRIFF {:d} spieler {:d} nachschub einheiten {:d} wert {:.0f}", now, m_player->getPlayerIndex(), static_cast<Int>(more.size()), moreValue);
			GameEngine_RunnerLog( line );
		}
	}
}

void AICommanderPlayer::endAttack( const char *reason )
{
	UnsignedInt now = TheGameLogic->getFrame();
	Real value = forceCentroid( m_attackers, nullptr, nullptr, 0.0f, nullptr );
	char line[224];
	WWFormat::Into(line, "KIANGRIFF {:d} spieler {:d} ende {} phase {:d} dauer {:d} wert {:.0f} start {:.0f}", now, m_player->getPlayerIndex(), reason, static_cast<Int>(m_attackPhase), (now - m_attackPhaseFrame) / LOGICFRAMES_PER_SECOND, value, m_attackStartValue);
	GameEngine_RunnerLog( line );

	const Bool brokenOff = strcmp( reason, "prognose" ) == 0 || strcmp( reason, "rueckzug" ) == 0 || strcmp( reason, "zu_schwach" ) == 0;
	Coord3D retreatFrom;
	const Bool retreatBeacon = brokenOff && m_attackPhase == CommanderAttackPhase::ATTACK && hasHumanPartner()
		&& !(m_ownBeacon != INVALID_ID && m_ownBeaconKind == OWN_BEACON_HELP)
		&& forceCentroid( m_attackers, &retreatFrom, nullptr, 0.0f, nullptr ) > 0.0f;
	if (brokenOff && !partOff( AI_PART_REGROW ))
	{
		m_regrowValue = REGROW_FACTOR * m_attackStartValue;
		m_regrowUntil = now + REGROW_TIME;
		WWFormat::Into(line, "KIANGRIFF {:d} spieler {:d} erst_ab wert {:.0f} bis {:d}", now, m_player->getPlayerIndex(), m_regrowValue, m_regrowUntil);
		GameEngine_RunnerLog( line );
	}

	releaseUnits( &m_attackers );
	if (m_ownBeacon != INVALID_ID && m_ownBeaconKind == OWN_BEACON_ATTACK)
		removeOwnBeacon( "angriff_ende" );
	// warn the human partners where the army fell back from
	if (retreatBeacon)
		placeOwnBeacon( retreatFrom, OWN_BEACON_RETREAT, now + RETREAT_BEACON_TIME );
	m_attackPhase = CommanderAttackPhase::NONE;
	m_attackPhaseFrame = now;
	m_objectiveID = INVALID_ID;
	m_gatherReady = FALSE;
	m_attackCooldownFrame = now + static_cast<UnsignedInt>(static_cast<Real>(ATTACK_COOLDOWN) * paceFor( getAIDifficulty(), m_personality ).cooldownScale);
}

void AICommanderPlayer::updateAttack( void )
{
	switch (m_attackPhase)
	{
		case CommanderAttackPhase::GATHER:
			continueGather();
			break;
		case CommanderAttackPhase::ATTACK:
			continueAttack();
			break;
		case CommanderAttackPhase::NONE:
		default:
			if (TheGameLogic->getFrame() < m_attackCooldownFrame || m_assistAlly >= 0)
				break;
			if (graceLeft() > GRACE_GATHER_LEAD)
				break;
			if (ownBaseThreat() >= MIN_THREAT)
				break;
			startGather();
			break;
	}
}

// ------------------------------------------------------------------------------------------------
// Late game: the army keeps growing, upgrades
// ------------------------------------------------------------------------------------------------

// May the AI use this button on this building? Same rules as the control bar
// (ControlBar::getCommandAvailability), but from the game state only: building finished, not
// being sold, not disabled, required upgrades and sciences present.
Bool AICommanderPlayer::isButtonAllowed( const CommandButton *button, Object *obj ) const
{
	if (button == nullptr || obj == nullptr)
		return FALSE;
	if (obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || obj->testStatus( OBJECT_STATUS_SOLD ) || obj->isDisabled())
		return FALSE;
	if (obj->testScriptStatusBit( OBJECT_STATUS_SCRIPT_DISABLED ) || obj->testScriptStatusBit( OBJECT_STATUS_SCRIPT_UNPOWERED ))
		return FALSE;
	if (BitTest( button->getOptions(), NEED_UPGRADE ))
	{
		const UpgradeTemplate *needed = button->getUpgradeTemplate();
		if (needed)
		{
			if (needed->getUpgradeType() == UPGRADE_TYPE_PLAYER && !m_player->hasUpgradeComplete( needed ))
				return FALSE;
			if (needed->getUpgradeType() == UPGRADE_TYPE_OBJECT && !obj->hasUpgrade( needed ))
				return FALSE;
		}
	}
	for (size_t i = 0; i < button->getScienceVec().size(); ++i)
	{
		if (!m_player->hasScience( button->getScienceVec()[i] ))
			return FALSE;
	}
	return TRUE;
}

// Unit for the army: infantry or vehicle with a weapon on the ground; no dozer, harvester, drone
// or aircraft (the command layer's missions use ground forces only).
Bool AICommanderPlayer::isArmyTemplate( const ThingTemplate *t ) const
{
	if (t == nullptr)
		return FALSE;
	if (!t->isKindOf( KINDOF_INFANTRY ) && !t->isKindOf( KINDOF_VEHICLE ))
		return FALSE;
	if (t->isKindOf( KINDOF_DOZER ) || t->isKindOf( KINDOF_HARVESTER ) || t->isKindOf( KINDOF_DRONE ) || t->isKindOf( KINDOF_AIRCRAFT )
		|| isHackerTemplate( t )
		|| t->isKindOf( KINDOF_TRANSPORT )	// transports would otherwise be bought as the "most expensive unit" in Generals
		|| t->isKindOf( KINDOF_IGNORED_IN_GUI ) || t->isKindOf( KINDOF_PROJECTILE ))
		return FALSE;
	return t->canPossiblyHaveAnyWeapon();
}

// Buy an upgrade: the first in object and button order the AI can afford without dropping below
// LATE_KEEP_MONEY. Player upgrades only (armor, ammunition, ...): per-building object upgrades
// (mines) would eat the money, and the classic AI buys upgrades of single units through its scripts.
Bool AICommanderPlayer::buyUpgrade( void )
{
	Int money = freeMoney();
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() != m_player || !obj->isKindOf( KINDOF_STRUCTURE ) || obj->isEffectivelyDead())
			continue;
		ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
		if (pu == nullptr || pu->getProductionCount() > 0)
			continue;
		const CommandSet *set = TheControlBar->findCommandSet( obj->getCommandSetString() );
		if (set == nullptr)
			continue;
		for (Int j = 0; j < MAX_COMMANDS_PER_SET; ++j)
		{
			const CommandButton *button = set->getCommandButton( j );
			if (button == nullptr || button->getUpgradeTemplate() == nullptr)
				continue;
			if (button->getCommandType() != GUI_COMMAND_PLAYER_UPGRADE)
				continue;
			const UpgradeTemplate *up = button->getUpgradeTemplate();
			if (up->getUpgradeType() != UPGRADE_TYPE_PLAYER || m_player->hasUpgradeComplete( up ) || m_player->hasUpgradeInProduction( up ))
				continue;
			Int cost = up->calcCostToBuild( m_player );
			const Real keep = static_cast<Real>( scaledMoney( LATE_KEEP_MONEY ) ) * ((m_personality == AI_PERSONALITY_TECHNICIAN) ? TECHNICIAN_UPGRADE_MONEY : 1.0f);
			if (static_cast<Real>( money - cost ) < keep)
				continue;
			if (!isButtonAllowed( button, obj ))
				continue;
			if (pu->queueUpgrade( up ))
			{
				++m_upgradesBought;
				char line[224];
				WWFormat::Into(line, "KIBAU {:d} spieler {:d} upgrade {} kosten {:d} geld {:d}", TheGameLogic->getFrame(), m_player->getPlayerIndex(), up->getUpgradeName().str(), cost, money);
				GameEngine_RunnerLog( line );
				return TRUE;
			}
		}
	}
	return FALSE;
}

// Build units: one per idle factory while the money stays above LATE_KEEP_MONEY; infantry only
// from LATE_INFANTRY_MONEY on, so the barracks do not take all the money. Picks the most expensive
// buildable ground unit; once the AI has LATE_SAME_TYPE_CAP of it, it counts only one third, which
// yields a mix of the side's strongest units.
// Army units the own factories may build now (button allowed), one entry per combat table row,
// in factory and button order; the planner ranks them against the enemy mix.
void AICommanderPlayer::collectArmyCandidates( std::vector<AIPlanCandidate> *out ) const
{
	out->clear();
	for (Object *factory = TheGameLogic->getFirstObject(); factory; factory = factory->getNextObject())
	{
		if (factory->getControllingPlayer() != m_player || !factory->isKindOf( KINDOF_STRUCTURE ) || factory->isEffectivelyDead())
			continue;
		if (factory->getProductionUpdateInterface() == nullptr)
			continue;
		const CommandSet *set = TheControlBar->findCommandSet( factory->getCommandSetString() );
		if (set == nullptr)
			continue;
		for (Int j = 0; j < MAX_COMMANDS_PER_SET; ++j)
		{
			const CommandButton *button = set->getCommandButton( j );
			if (button == nullptr || button->getCommandType() != GUI_COMMAND_UNIT_BUILD)
				continue;
			const ThingTemplate *t = button->getThingTemplate();
			if (!isArmyTemplate( t ) || !isButtonAllowed( button, factory ))
				continue;
			const Int profile = m_planner.profileOf( t );
			if (profile < 0)
				continue;
			Bool known = FALSE;
			for (size_t i = 0; i < out->size() && !known; ++i)
				known = (*out)[i].profile == profile;
			if (known)
				continue;
			AIPlanCandidate c;
			c.profile = profile;
			c.cost = t->calcCostToBuild( m_player );
			out->push_back( c );
		}
	}
}

Int AICommanderPlayer::produceUnits( void )
{
	// Count per template (lookup only, no order over pointers).
	std::vector<std::pair<const ThingTemplate *, Int>> have;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() != m_player || !isMobileCombatUnit( obj ))
			continue;
		const ThingTemplate *t = obj->getTemplate();
		Bool found = FALSE;
		for (size_t i = 0; i < have.size(); ++i)
		{
			if (have[i].first->isEquivalentTo( t ))
			{
				++have[i].second;
				found = TRUE;
				break;
			}
		}
		if (!found)
			have.push_back( std::make_pair( t, 1 ) );
	}

	// Share of the own army (by cost) that can hit aircraft.
	Real armyValue = 0.0f, airDefenseValue = 0.0f;
	for (size_t i = 0; i < have.size(); ++i)
	{
		const Real value = static_cast<Real>( have[i].first->calcCostToBuild( m_player ) * have[i].second );
		armyValue += value;
		if (hitsAir( have[i].first ))
			airDefenseValue += value;
	}
	Real airDefenseShare = (armyValue > 0.0f) ? airDefenseValue / armyValue : 0.0f;
	Int detectorUnits = 0;
	for (size_t i = 0; i < have.size(); ++i)
	{
		if (isDetectorTemplate( have[i].first ))
			detectorUnits += have[i].second;
	}

	Int queued = 0;
	const Int maxUnits = paceFor( getAIDifficulty(), m_personality ).lateMaxUnits;
	for (Object *factory = TheGameLogic->getFirstObject(); factory; factory = factory->getNextObject())
	{
		if (maxUnits > 0 && queued >= maxUnits)
			break;
		if (factory->getControllingPlayer() != m_player || !factory->isKindOf( KINDOF_STRUCTURE ) || factory->isEffectivelyDead())
			continue;
		ProductionUpdateInterface *pu = factory->getProductionUpdateInterface();
		if (pu == nullptr || pu->getProductionCount() > 0)
			continue;
		const CommandSet *set = TheControlBar->findCommandSet( factory->getCommandSetString() );
		if (set == nullptr)
			continue;
		Int money = freeMoney();
		// Two choices: by the planner's counter score against the enemy mix, and by cost (the
		// strongest units of the side) when the planner ranks none of this factory's units.
		const ThingTemplate *best = nullptr;
		Real bestScore = 0.0f;
		const ThingTemplate *bestCounter = nullptr;
		Real bestCounterScore = 0.0f;
		const ThingTemplate *bestAir = nullptr;
		Real bestAirScore = 0.0f;
		const ThingTemplate *bestDetector = nullptr;
		for (Int j = 0; j < MAX_COMMANDS_PER_SET; ++j)
		{
			const CommandButton *button = set->getCommandButton( j );
			if (button == nullptr || button->getCommandType() != GUI_COMMAND_UNIT_BUILD)
				continue;
			const ThingTemplate *t = button->getThingTemplate();
			if (!isArmyTemplate( t ) || !isButtonAllowed( button, factory ))
				continue;
			Int cost = t->calcCostToBuild( m_player );
			if (cost <= 0 || money - cost < scaledMoney( LATE_KEEP_MONEY ))
				continue;
			if (t->isKindOf( KINDOF_INFANTRY ) && money < scaledMoney( LATE_INFANTRY_MONEY ))
				continue;	// vehicles first; infantry only with a full account
			if (TheBuildAssistant->canMakeUnit( factory, t ) != CanMakeType::CANMAKE_OK)
				continue;
			Int count = 0;
			for (size_t i = 0; i < have.size(); ++i)
			{
				if (have[i].first->isEquivalentTo( t ))
				{
					count = have[i].second;
					break;
				}
			}
			Real score = static_cast<Real>(cost);
			Real counter = m_planner.counterScoreOf( m_planner.profileOf( t ) );
			if (m_modelAir && hitsAir( t ))
			{
				score *= AIR_PREFERENCE;
				counter *= AIR_PREFERENCE;
				if (bestAir == nullptr || score > bestAirScore)
				{
					bestAir = t;
					bestAirScore = score;
				}
			}
			if (m_modelStealth && bestDetector == nullptr && isDetectorTemplate( t ))
				bestDetector = t;
			if (count >= LATE_SAME_TYPE_CAP)
			{
				score /= 3.0f;
				counter /= 3.0f;
			}
			if (best == nullptr || score > bestScore)
			{
				best = t;
				bestScore = score;
			}
			if (counter > 0.0f && (bestCounter == nullptr || counter > bestCounterScore))
			{
				bestCounter = t;
				bestCounterScore = counter;
			}
		}
		const char *why = "kosten";
		if (bestCounter && !partOff( AI_PART_COUNTERS ))
		{
			best = bestCounter;
			why = "konter";
		}
		if (bestAir && airDefenseShare < std::min( m_enemyAirShare, AIR_DEFENSE_MAX ))
		{
			best = bestAir;
			why = "luftabwehr";
		}
		if (bestDetector && detectorUnits < STEALTH_DETECTOR_UNITS)
		{
			best = bestDetector;
			why = "detektor";
		}
		if (best == nullptr)
			continue;
		Int cost = best->calcCostToBuild( m_player );
		if (pu->queueCreateUnit( best, pu->requestUniqueUnitID() ))
		{
			++queued;
			++m_unitsBuilt;
			Bool found = FALSE;
			for (size_t i = 0; i < have.size(); ++i)
			{
				if (have[i].first->isEquivalentTo( best ))
				{
					++have[i].second;
					found = TRUE;
					break;
				}
			}
			if (!found)
				have.push_back( std::make_pair( best, 1 ) );
			armyValue += static_cast<Real>( cost );
			if (hitsAir( best ))
				airDefenseValue += static_cast<Real>( cost );
			if (isDetectorTemplate( best ))
				++detectorUnits;
			airDefenseShare = airDefenseValue / armyValue;
			char line[224];
			WWFormat::Into(line, "KIBAU {:d} spieler {:d} einheit {} kosten {:d} geld {:d} wahl {}", TheGameLogic->getFrame(), m_player->getPlayerIndex(), best->getName().str(), cost, money, why);
			GameEngine_RunnerLog( line );
		}
	}
	return queued;
}

// Late game: after its last wave the classic AI only replaces destroyed teams and money piles up.
// From the late game start of the difficulty (LATE_GAME_START for Brutal) on, the command layer spends money above LATE_PRODUCE_MONEY itself: first
// upgrades, then units in idle factories. The new units stand at the factory without a team and
// join the attacks.
void AICommanderPlayer::updateLateGame( void )
{
	const Pace &pace = paceFor( getAIDifficulty(), m_personality );
	UnsignedInt start = pace.lateStart;
	if (pace.lateMaxUnits == 0 && !partOff( AI_PART_EARLY_PRODUCTION ))
		start = std::min( start, EARLY_PRODUCTION_START );
	// With start protection the army is built up during its last minutes, whatever the usual start.
	const UnsignedInt graceEnd = TheGameLogic->getGraceEndFrame();
	if (graceEnd > GRACE_ARMY_LEAD)
		start = std::min( start, graceEnd - GRACE_ARMY_LEAD );
	// With a rich start the money is there from the first minute.
	if (pace.lateMaxUnits == 0 && cashClass() == CashClass::RICH)
		start = std::min( start, OPENING_RICH_PRODUCTION );
	if (TheGameLogic->getFrame() < start)
		return;
	// Money for planned economy buildings (new depot, permanent income) comes first.
	if (freeMoney() < scaledMoney( LATE_PRODUCE_MONEY ) + m_economyNeed)
		return;
	buyUpgrade();
	if (graceLeft() > GRACE_ARMY_LEAD)
		return;
	produceUnits();
}

// Production breadth: a second war factory from FACTORY_SECOND_START, a third from FACTORY_THIRD_START,
// as priority builds of the classic AI once its first one stands. Like growth: not while the base
// is attacked, not while another priority build waits, only with the money left after the budget.
void AICommanderPlayer::updateFactories( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	const Bool rich = cashClass() == CashClass::RICH;
	const UnsignedInt second = rich ? RICH_FACTORY_SECOND : FACTORY_SECOND_START;
	const UnsignedInt third = rich ? RICH_FACTORY_THIRD : FACTORY_THIRD_START;
	if ((partOff( AI_PART_FACTORIES ) && !rich) || paceFor( getAIDifficulty(), m_personality ).lateMaxUnits != 0 || now < second
		|| graceLeft() > GRACE_ARMY_LEAD + GRACE_FACTORY_LEAD)
		return;
	const ThingTemplate *t = findConstructable( KINDOF_FS_WARFACTORY );
	if (t == nullptr)
		return;
	const Int wanted = (now >= third) ? 3 : 2;
	const Int have = countOwnOrPlanned( t );
	if (have == 0 || have >= wanted)
		return;		// the first one comes from the classic AI's build list
	if (ownBaseThreat() >= MIN_THREAT || growthBuildPending() || freeMoney() - t->calcCostToBuild( m_player ) < FACTORY_KEEP_MONEY)
		return;
	if (queueBaseBuilding( t ))
	{
		++m_economyOrders;
		char line[224];
		WWFormat::Into(line, "KIWIRTSCHAFT {:d} spieler {:d} bau {} bestand {:d} ziel {:d} vorrat {:.0f} grund {}", now, m_player->getPlayerIndex(), t->getName().str(), have, wanted, 0.0f, "produktion");
		GameEngine_RunnerLog( line );
	}
}

// ------------------------------------------------------------------------------------------------
// Sustainable economy
// ------------------------------------------------------------------------------------------------

// Remaining value of the supply depots the AI is harvesting: depots (SupplyWarehouseDockUpdate)
// within SUPPLY_NEAR_RADIUS of own supply centers. centers = number of own centers, depots (optional)
// = number of those depots that still hold something.
Real AICommanderPlayer::supplyReserve( Int *centers, Int *depots ) const
{
	static const NameKeyType key_warehouseUpdate = NAMEKEY( "SupplyWarehouseDockUpdate" );	// as in AIPlayer::findSupplyCenter
	std::vector<Coord3D> own;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() == m_player && !obj->isEffectivelyDead() && isEconomyKind( obj->getTemplate(), KINDOF_FS_SUPPLY_CENTER ))
			own.push_back( *obj->getPosition() );
	}
	*centers = static_cast<Int>(own.size());
	if (depots)
		*depots = 0;
	if (own.empty())
		return 0.0f;
	Real radiusSqr = SUPPLY_NEAR_RADIUS * SUPPLY_NEAR_RADIUS;
	Real reserve = 0.0f;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (!obj->isKindOf( KINDOF_SUPPLY_SOURCE ) || obj->isEffectivelyDead())
			continue;
		SupplyWarehouseDockUpdate *warehouse = static_cast<SupplyWarehouseDockUpdate *>( obj->findUpdateModule( key_warehouseUpdate ) );
		if (warehouse == nullptr)
			continue;
		for (size_t i = 0; i < own.size(); ++i)
		{
			if (distSqr2D( obj->getPosition(), &own[i] ) <= radiusSqr)
			{
				reserve += static_cast<Real>(warehouse->getBoxesStored() * TheGlobalData->m_baseValuePerSupplyBox);
				if (depots && warehouse->getBoxesStored() > 0)
					++*depots;
				break;
			}
		}
	}
	return reserve;
}

// Buildable building of one kind from the build commands of an own dozer (button order, first
// dozer in the object list); only what the player may build given the prerequisites.
const ThingTemplate *AICommanderPlayer::findConstructable( KindOfType kind ) const
{
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() != m_player || obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_DOZER ))
			continue;
		const CommandSet *set = TheControlBar->findCommandSet( obj->getCommandSetString() );
		if (set == nullptr)
			continue;
		for (Int j = 0; j < MAX_COMMANDS_PER_SET; ++j)
		{
			const CommandButton *button = set->getCommandButton( j );
			if (button == nullptr || button->getCommandType() != GUI_COMMAND_DOZER_CONSTRUCT)
				continue;
			const ThingTemplate *t = button->getThingTemplate();
			if (t == nullptr || !isEconomyKind( t, kind ) || t->getBuildable() == BSTATUS_NO)
				continue;
			for (size_t s = 0; s < button->getScienceVec().size(); ++s)
			{
				if (!m_player->hasScience( button->getScienceVec()[s] ))
				{
					t = nullptr;
					break;
				}
			}
			if (t && m_player->canBuild( t ))
				return t;
		}
	}
	return nullptr;
}

// Own buildings of this template (including under construction) plus build list entries not yet started.
Int AICommanderPlayer::countOwnOrPlanned( const ThingTemplate *t ) const
{
	Int count = 0;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() == m_player && !obj->isEffectivelyDead() && obj->getTemplate()->isEquivalentTo( t ))
			++count;
	}
	for (BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext())
	{
		if (info->getTemplateName() == t->getName() && TheGameLogic->findObjectByID( info->getObjectID() ) == nullptr && info->isPriorityBuild()
			&& info->isBuildable())
			++count;
	}
	return count;
}

// Building site near the base: rings around the base center, starting on the side facing away
// from the enemy, fixed angle steps; the first legal site wins. Adds it to the classic AI's build
// list as a priority build (like AIPlayer::buildBySupplies); its dozer then builds it.
Bool AICommanderPlayer::queueBaseBuilding( const ThingTemplate *t, Coord3D *site )
{
	if (!m_baseCenterSet || t == nullptr)
		return FALSE;
	Real away = 0.0f;
	if (m_currentEnemy)
	{
		Coord3D enemyPos;
		ObjectID id = INVALID_ID;
		if (findObjective( m_currentEnemy, m_baseCenter, &enemyPos, &id ))
			away = atan2f( m_baseCenter.y - enemyPos.y, m_baseCenter.x - enemyPos.x );
	}
	Real angle = t->getPlacementViewAngle();
	const Real rings[] = { 0.5f, 0.8f, 1.0f, 1.25f };
	const Int STEPS = 16;
	for (size_t r = 0; r < sizeof( rings ) / sizeof( rings[0] ); ++r)
	{
		Real radius = m_baseRadius * rings[r] + 60.0f;
		for (Int k = 0; k < STEPS; ++k)
		{
			// alternate left and right of the direction facing away
			Int step = (k + 1) / 2;
			Real dir = away + ((k % 2) ? 1.0f : -1.0f) * static_cast<Real>(step) * (2.0f * PI / STEPS);
			Coord3D pos = m_baseCenter;
			pos.x += cosf( dir ) * radius;
			pos.y += sinf( dir ) * radius;
			pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
			LegalBuildCode code = TheBuildAssistant->isLocationLegalToBuild( &pos, t, angle,
				BuildAssistant::CLEAR_PATH | BuildAssistant::TERRAIN_RESTRICTIONS | BuildAssistant::NO_OBJECT_OVERLAP, nullptr, m_player );
			if (code != LegalBuildCode::LBC_OK)
				continue;
			TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild leaves build footprint feedback behind (as in AIPlayer)
			pos.z = 0.0f;	// build list positions are relative to the ground
			const BuildListInfo *headBefore = m_player->getBuildList();
			m_player->addToPriorityBuildList( t->getName(), &pos, angle );
			noteOwnOrder( headBefore );
			m_buildDelay = 0;
			if (site)
				*site = pos;
			return TRUE;
		}
	}
	TheTerrainVisual->removeAllBibs();
	return FALSE;
}

// Gather rate of the own depots (money per minute, measured over a minute; a new depot raises the
// reserve and only restarts the measurement) and the minutes until they run dry.
Real AICommanderPlayer::forecastSupplies( Real reserve, UnsignedInt now )
{
	if (m_reserveMarkFrame == 0 || reserve > m_reserveMark)
	{
		m_reserveMark = reserve;
		m_reserveMarkFrame = now;
	}
	else if (now - m_reserveMarkFrame >= RATE_WINDOW)
	{
		const Real sample = (m_reserveMark - reserve) * static_cast<Real>(60 * LOGICFRAMES_PER_SECOND) / static_cast<Real>(now - m_reserveMarkFrame);
		m_gatherRate = (m_gatherRate > 0.0f) ? 0.5f * (m_gatherRate + sample) : sample;
		m_reserveMark = reserve;
		m_reserveMarkFrame = now;
	}
	if (m_gatherRate < 1.0f)
		return NO_FORECAST;
	return std::min( NO_FORECAST, reserve / m_gatherRate );
}

// Hacker template: that of an own hacker, otherwise the first one an own factory can build.
const ThingTemplate *AICommanderPlayer::findHackerTemplate( void ) const
{
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() == m_player && !obj->isEffectivelyDead() && isHackerObject( obj ))
			return obj->getTemplate();
	}
	for (Object *factory = TheGameLogic->getFirstObject(); factory; factory = factory->getNextObject())
	{
		if (factory->getControllingPlayer() != m_player || !factory->isKindOf( KINDOF_STRUCTURE ) || factory->isEffectivelyDead())
			continue;
		const CommandSet *set = TheControlBar->findCommandSet( factory->getCommandSetString() );
		if (set == nullptr)
			continue;
		for (Int j = 0; j < MAX_COMMANDS_PER_SET; ++j)
		{
			const CommandButton *button = set->getCommandButton( j );
			if (button && button->getCommandType() == GUI_COMMAND_UNIT_BUILD && button->getThingTemplate()
				&& isHackerTemplate( button->getThingTemplate() ))
				return button->getThingTemplate();
		}
	}
	return nullptr;
}

// China: build hackers up to the target and put idle hackers to work: into the own internet center
// while there is room, otherwise hack the internet on the spot (hackers beyond the center's slots
// are the free ones outside).
Int AICommanderPlayer::manageHackers( Int wanted )
{
	Int hackers = 0;
	AIGroup *hackGroup = nullptr;
	std::vector<Object *> idle;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() != m_player || obj->isEffectivelyDead() || !isHackerObject( obj ))
			continue;
		++hackers;
		if (!obj->isContained() && obj->getAIUpdateInterface() && obj->getAIUpdateInterface()->isIdle())
			idle.push_back( obj );
		// Hackers stay at work: the classic AI must not pull them into combat teams.
		if (obj->getAIUpdateInterface())
			obj->getAIUpdateInterface()->setIsRecruitable( FALSE );
	}
	if (!idle.empty())
	{
		// internet center with free slots (first in the object list)
		Object *center = nullptr;
		Int freeSlots = 0;
		for (Object *obj = TheGameLogic->getFirstObject(); obj && center == nullptr; obj = obj->getNextObject())
		{
			if (obj->getControllingPlayer() != m_player || obj->isEffectivelyDead() || !isEconomyKind( obj->getTemplate(), KINDOF_FS_INTERNET_CENTER )
				|| obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ))
				continue;
			ContainModuleInterface *contain = obj->getContain();
			if (contain && contain->getContainMax() > 0 && static_cast<Int>(contain->getContainCount()) < contain->getContainMax())
			{
				center = obj;
				freeSlots = contain->getContainMax() - static_cast<Int>(contain->getContainCount());
			}
		}
		AIGroup *enterGroup = nullptr;
		for (size_t i = 0; i < idle.size(); ++i)
		{
			if (center && freeSlots > 0)
			{
				addToGroup( &enterGroup, idle[i] );
				--freeSlots;
			}
			else
				addToGroup( &hackGroup, idle[i] );
		}
		if (enterGroup)
			enterGroup->groupEnter( center, CMD_FROM_AI );
		if (hackGroup)
			hackGroup->groupHackInternet( CMD_FROM_AI );
	}
	if (hackers >= wanted)
		return hackers;

	if (hackers >= wanted)
		return hackers;
	queueEconomyUnit( isHackerTemplate, "hacker", hackers, wanted );
	return hackers;
}

// Supply trucks: the classic AI builds its gatherers through the scripts; this only tops up when
// fewer are left than the depots in reach justify (destroyed trucks, new depots).
Int AICommanderPlayer::manageSupplyTrucks( Int centers, Int depots )
{
	Int trucks = 0;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() == m_player && !obj->isEffectivelyDead() && obj->isKindOf( KINDOF_HARVESTER ))
			++trucks;
	}
	Int wanted = 0;
	if (centers > 0 && depots > 0)
	{
		wanted = std::min( TRUCKS_PER_CENTER * centers, TRUCKS_PER_DEPOT * depots + 1 );
		if (trucks < wanted)
			queueEconomyUnit( isHarvesterTemplate, "sammler", trucks, wanted );
	}
	return trucks;
}

// Queue units the match function accepts in idle factories (first in object order that can build
// them), until have + ordered reaches wanted; stops when the money would fall below ECONOMY_KEEP_MONEY.
Int AICommanderPlayer::queueEconomyUnit( Bool (*match)( const ThingTemplate * ), const char *what, Int have, Int wanted )
{
	Int ordered = 0;
	for (Object *factory = TheGameLogic->getFirstObject(); factory && have + ordered < wanted; factory = factory->getNextObject())
	{
		if (factory->getControllingPlayer() != m_player || !factory->isKindOf( KINDOF_STRUCTURE ) || factory->isEffectivelyDead())
			continue;
		ProductionUpdateInterface *pu = factory->getProductionUpdateInterface();
		if (pu == nullptr || pu->getProductionCount() > 0)
			continue;
		const CommandSet *set = TheControlBar->findCommandSet( factory->getCommandSetString() );
		if (set == nullptr)
			continue;
		Int money = static_cast<Int>(m_player->getMoney()->countMoney());
		for (Int j = 0; j < MAX_COMMANDS_PER_SET; ++j)
		{
			const CommandButton *button = set->getCommandButton( j );
			if (button == nullptr || button->getCommandType() != GUI_COMMAND_UNIT_BUILD)
				continue;
			const ThingTemplate *t = button->getThingTemplate();
			if (t == nullptr || !match( t ) || !isButtonAllowed( button, factory ))
				continue;
			if (money - t->calcCostToBuild( m_player ) < ECONOMY_KEEP_MONEY + m_unitReserve)
				return ordered;
			if (TheBuildAssistant->canMakeUnit( factory, t ) != CanMakeType::CANMAKE_OK)
				continue;
			if (pu->queueCreateUnit( t, pu->requestUniqueUnitID() ))
			{
				++ordered;
				++m_economyOrders;
				char line[192];
				WWFormat::Into(line, "KIWIRTSCHAFT {:d} spieler {:d} {} {} bestand {:d} ziel {:d}", TheGameLogic->getFrame(), m_player->getPlayerIndex(), what, t->getName().str(), have + ordered - 1, wanted);
				GameEngine_RunnerLog( line );
			}
			break;
		}
	}
	return ordered;
}

// "Capture building" button of a unit (its command set); allowed is set if the AI may already
// use it (otherwise the upgrade is usually missing).
const CommandButton *AICommanderPlayer::findCaptureButton( Object *unit, Bool *allowed ) const
{
	*allowed = FALSE;
	const CommandSet *set = TheControlBar->findCommandSet( unit->getCommandSetString() );
	if (set == nullptr)
		return nullptr;
	for (Int j = 0; j < MAX_COMMANDS_PER_SET; ++j)
	{
		const CommandButton *button = set->getCommandButton( j );
		if (button == nullptr || button->getCommandType() != GUI_COMMAND_SPECIAL_POWER || button->getSpecialPowerTemplate() == nullptr)
			continue;
		if (button->getSpecialPowerTemplate()->getSpecialPowerType() != SPECIAL_INFANTRY_CAPTURE_BUILDING)
			continue;
		*allowed = isButtonAllowed( button, unit );
		return button;
	}
	return nullptr;
}

// Capture tech buildings: oil derricks and other tech buildings near the base that belong to no
// alliance and are unguarded are captured by an idle own infantry unit. If the upgrade is missing,
// the AI buys it. One mission at a time.
void AICommanderPlayer::updateTechCapture( void )
{
	UnsignedInt now = TheGameLogic->getFrame();
	const Bool growth = !partOff( AI_PART_GROWTH );
	if (now < (growth ? CAPTURE_START : CAPTURE_START_SLOW) || !m_baseCenterSet)
		return;

	if (m_captureTarget != INVALID_ID)
	{
		Object *target = TheGameLogic->findObjectByID( m_captureTarget );
		Object *unit = TheGameLogic->findObjectByID( m_captureUnit );
		const char *result = nullptr;
		if (target && target->getControllingPlayer() == m_player)
		{
			result = "eingenommen";
			++m_captures;
		}
		else if (target == nullptr || target->isEffectivelyDead())
			result = "ziel_weg";
		else if (unit == nullptr || unit->isEffectivelyDead())
			result = "einheit_weg";
		else if (now - m_captureFrame > CAPTURE_TIMEOUT)
			result = "zeit";
		if (result == nullptr)
			return;
		if (unit && !unit->isEffectivelyDead() && unit->getAIUpdateInterface())
			unit->getAIUpdateInterface()->setIsRecruitable( TRUE );
		char line[192];
		WWFormat::Into(line, "KIWIRTSCHAFT {:d} spieler {:d} technik_ende {} dauer {:d}", now, m_player->getPlayerIndex(), result, (now - m_captureFrame) / LOGICFRAMES_PER_SECOND);
		GameEngine_RunnerLog( line );
		m_captureTarget = INVALID_ID;
		m_captureUnit = INVALID_ID;
		return;
	}

	// Target: a free, unguarded tech building in range -- those that pay money (oil derricks: an
	// AutoDepositUpdate module) first, then the nearest.
	static const NameKeyType key_autoDeposit = NAMEKEY( "AutoDepositUpdate" );
	Object *target = nullptr;
	Real targetDist = 0.0f;
	Bool targetPays = FALSE;
	const Real range = growth ? CAPTURE_RANGE : CAPTURE_RANGE_SLOW;
	Real rangeSqr = range * range;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (!obj->isKindOf( KINDOF_TECH_BUILDING ) || !obj->isKindOf( KINDOF_CAPTURABLE ) || obj->isEffectivelyDead())
			continue;
		Player *owner = obj->getControllingPlayer();
		if (owner == m_player || (owner && owner->getDefaultTeam() && m_player->getRelationship( owner->getDefaultTeam() ) != NEUTRAL))
			continue;	// own, allied or enemy building
		Real d = distSqr2D( obj->getPosition(), &m_baseCenter );
		if (d > rangeSqr)
			continue;
		const Bool pays = growth && obj->findUpdateModule( key_autoDeposit ) != nullptr;
		if (target && ((targetPays && !pays) || (pays == targetPays && d >= targetDist)))
			continue;
		if (enemyStrengthNear( m_player, obj->getPosition(), CAPTURE_SAFE_RADIUS, nullptr ) >= CAPTURE_SAFE_THREAT)
			continue;
		target = obj;
		targetDist = d;
		targetPays = pays;
	}
	if (target == nullptr)
		return;

	// Unit: the nearest idle own infantry with an allowed capture button.
	Object *unit = nullptr;
	const CommandButton *unitButton = nullptr;
	const UpgradeTemplate *missing = nullptr;
	Real unitDist = 0.0f;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() != m_player || !obj->isKindOf( KINDOF_INFANTRY ) || !isHelperCandidate( obj ))
			continue;
		if (!obj->getAIUpdateInterface()->isIdle())
			continue;
		Bool allowed = FALSE;
		const CommandButton *button = findCaptureButton( obj, &allowed );
		if (button == nullptr)
			continue;
		if (!allowed)
		{
			if (missing == nullptr && button->getUpgradeTemplate() && BitTest( button->getOptions(), NEED_UPGRADE ))
				missing = button->getUpgradeTemplate();
			continue;
		}
		Real d = distSqr2D( obj->getPosition(), target->getPosition() );
		if (unit && d >= unitDist)
			continue;
		unit = obj;
		unitButton = button;
		unitDist = d;
	}
	if (unit == nullptr)
	{
		// "Capture building" upgrade missing: buy it if there is money (AIPlayer::buildUpgrade, as the scripts do).
		if (missing && missing->getUpgradeType() == UPGRADE_TYPE_PLAYER && !m_player->hasUpgradeComplete( missing )
			&& !m_player->hasUpgradeInProduction( missing )
			&& static_cast<Int>(m_player->getMoney()->countMoney()) >= missing->calcCostToBuild( m_player ) + ECONOMY_KEEP_MONEY)
		{
			buildUpgrade( missing->getUpgradeName() );
			if (m_player->hasUpgradeInProduction( missing ))
			{
				char line[192];
				WWFormat::Into(line, "KIWIRTSCHAFT {:d} spieler {:d} technik_upgrade {}", now, m_player->getPlayerIndex(), missing->getUpgradeName().str());
				GameEngine_RunnerLog( line );
			}
		}
		return;
	}
	unit->getAIUpdateInterface()->setIsRecruitable( FALSE );
	unit->doCommandButtonAtObject( unitButton, target, CMD_FROM_AI );
	m_captureUnit = unit->getID();
	m_captureTarget = target->getID();
	m_captureFrame = now;
	char line[224];
	WWFormat::Into(line, "KIWIRTSCHAFT {:d} spieler {:d} technik {} einheit {} ort {:.0f} {:.0f}", now, m_player->getPlayerIndex(), target->getTemplate()->getName().str(), unit->getTemplate()->getName().str(), target->getPosition()->x, target->getPosition()->y);
	GameEngine_RunnerLog( line );
}

// Sustainable economy: supply depots run dry; after that, money only comes from new depots,
// permanent income (USA drop zone, GLA black market, China hackers and internet center) and tech
// buildings. The command layer measures the remaining value of its depots and changes course in time:
//   - from ECONOMY_START with the remainder below EXPAND_RESERVE: another supply center at the
//     nearest rich depot (AIPlayer::buildBySupplies as the scripts do; the classic AI assigns
//     harvesters), at most every EXPAND_INTERVAL;
//   - from ECONOMY_START with the remainder below PERMANENT_RESERVE, at the latest from
//     PERMANENT_START: permanent income, whatever the faction's dozer can build; more of it below
//     DEPLETED_RESERVE. These builds go into the build list as priority builds without a money
//     check; until they are started, the late game logic holds back their money.
// Budget: the classic AI's base comes first. It builds one building of its build list at a time and
// takes priority builds first, so every extra building of the command layer delays its factories
// and defences, and every extra purchase may leave it without the money for the next one.
// The skirmish scripts build the base by marking build list entries as priority builds
// (buildSpecificAIBuilding); the command layer's own priority builds are economy buildings.
// baseReserve = cost of the next missing base building: the first unstarted priority build that is
// not an economy building, else the first missing automatic entry. The extra programs (growth,
// raids, late army, hackers) spend only what is left. 0 with -aiOhne haushalt.
Int AICommanderPlayer::baseReserve( void ) const
{
	if (partOff( AI_PART_BUDGET ))
		return 0;
	Int automatic = 0;
	for (BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext())
	{
		const Object *built = TheGameLogic->findObjectByID( info->getObjectID() );
		if (built && !built->isEffectivelyDead())
			continue;
		const ThingTemplate *t = TheThingFactory->findTemplate( info->getTemplateName() );
		if (t == nullptr || !m_player->canBuild( t ))	// not yet allowed (prerequisites): would block everything
			continue;
		if (TheGameLogic->getSuperweaponRestriction() > 0 && !m_player->canBuildMoreOfType( t ))
			continue;	// a further superweapon under the limit is never built
		if (info->isPriorityBuild())
		{
			if (!info->isBuildable())
				continue;	// used up: built once, the building is gone
			if (!isEconomyKind( t, KINDOF_FS_SUPPLY_CENTER ) && !isEconomyKind( t, KINDOF_FS_SUPPLY_DROPZONE )
				&& !isEconomyKind( t, KINDOF_FS_BLACK_MARKET ) && !isEconomyKind( t, KINDOF_FS_INTERNET_CENTER ))
				return t->calcCostToBuild( m_player );
		}
		else if (automatic == 0 && info->isAutomaticBuild() && info->isBuildable())
			automatic = t->calcCostToBuild( m_player );
	}
	return automatic;
}

Int AICommanderPlayer::freeMoney( void ) const
{
	return static_cast<Int>( m_player->getMoney()->countMoney() ) - baseReserve();
}

// A priority build (the classic AI's next base building or an own economy building) is planned but
// not yet started. With the budget, growth adds no further one then, so the classic AI's own list
// is not pushed back by a queue of extra buildings.
Bool AICommanderPlayer::growthBuildPending( void ) const
{
	if (partOff( AI_PART_BUDGET ))
		return FALSE;
	for (BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext())
	{
		if (!info->isPriorityBuild() || !info->isBuildable() || TheGameLogic->findObjectByID( info->getObjectID() ) != nullptr)
			continue;	// a used-up entry (built once, building gone) is no open order
		const ThingTemplate *t = TheThingFactory->findTemplate( info->getTemplateName() );
		if (t && TheGameLogic->getSuperweaponRestriction() > 0 && !m_player->canBuildMoreOfType( t ))
			continue;	// a further superweapon under the limit is never built
		return TRUE;
	}
	return FALSE;
}

void AICommanderPlayer::updateEconomy( void )
{
	UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_nextEconomyFrame)
		return;
	m_nextEconomyFrame = now + ECONOMY_INTERVAL;

	Int centers = 0;
	Int depots = 0;
	Real reserve = supplyReserve( &centers, &depots );
	Int money = static_cast<Int>(m_player->getMoney()->countMoney());
	const Real minutesLeft = forecastSupplies( reserve, now );
	Bool depleting = (now >= ECONOMY_START && (reserve < PERMANENT_RESERVE || minutesLeft <= PERMANENT_LEAD)) || now >= PERMANENT_START;
	Bool depleted = reserve < DEPLETED_RESERVE;
	// Balance: everything put into the economy is missing in the army. Growth investments wait while
	// the base is attacked or the own army is forecast clearly weaker than the enemy's seen one;
	// what replaces running-dry depots goes on regardless.
	const AIPlanResult *forecast = m_planner.hasResult() ? &m_planner.getResult() : nullptr;
	const Bool armyBehind = forecast && forecast->enemyHealthSeen > 0 && forecast->fieldWinner == 1 && forecast->fieldRatio < INVEST_MIN_RATIO
		&& !TheGameLogic->isGracePeriod() && !m_modelTurtle;
	const Bool invest = ownBaseThreat() < MIN_THREAT && !armyBehind && !m_modelRush && !partOff( AI_PART_GROWTH );
	// Growth steps by game time; during the start protection as at its end (the army comes later).
	const UnsignedInt growthNow = now + graceLeft();

	// Open up new depots: when the own run low (planned without a money check), and for growth from
	// minute 3 when the money is there and a safe depot with enough content is free.
	const ThingTemplate *growthCenter = (growthNow >= GROWTH_EXPAND_START && now >= m_nextExpandFrame) ? findConstructable( KINDOF_FS_SUPPLY_CENTER ) : nullptr;
	Bool growth = FALSE;
	if (invest && growthCenter && growthCenter->isKindOf( KINDOF_CASH_GENERATOR ) && !growthBuildPending()
		&& freeMoney() - growthCenter->calcCostToBuild( m_player ) >= GROWTH_EXPAND_RESERVE)
	{
		Object *depot = findSupplyCenter( GROWTH_EXPAND_MIN_CASH );
		static const NameKeyType key_growthWarehouse = NAMEKEY( "SupplyWarehouseDockUpdate" );
		SupplyWarehouseDockUpdate *warehouse = depot ? static_cast<SupplyWarehouseDockUpdate *>( depot->findUpdateModule( key_growthWarehouse ) ) : nullptr;
		growth = warehouse && warehouse->getBoxesStored() * TheGlobalData->m_baseValuePerSupplyBox >= GROWTH_EXPAND_MIN_CASH
			&& enemyStrengthNear( m_player, depot->getPosition(), GROWTH_SAFE_RADIUS, nullptr ) < GROWTH_SAFE_THREAT;
	}
	if (now >= m_nextExpandFrame && ((now >= ECONOMY_START && reserve < EXPAND_RESERVE) || growth))
	{
		m_nextExpandFrame = now + (growth ? GROWTH_EXPAND_INTERVAL : EXPAND_INTERVAL);
		const ThingTemplate *center = findConstructable( KINDOF_FS_SUPPLY_CENTER );
		// Plan without a money check: the classic AI builds priority builds as soon as the money is
		// there, and the late game logic holds back money until then (m_economyNeed).
		// Only one planned expansion at a time: while an unstarted supply center is still in the
		// build list, that one first (otherwise the entries pile up).
		Bool planned = FALSE;
		for (BuildListInfo *info = m_player->getBuildList(); info && !planned; info = info->getNext())
		{
			if (!info->isPriorityBuild() || !info->isBuildable() || TheGameLogic->findObjectByID( info->getObjectID() ) != nullptr)
				continue;
			planned = isEconomyKind( TheThingFactory->findTemplate( info->getTemplateName() ), KINDOF_FS_SUPPLY_CENTER );
		}
		if (center && center->isKindOf( KINDOF_CASH_GENERATOR ) && !planned)
		{
			Int before = countOwnOrPlanned( center );
			const BuildListInfo *headBefore = m_player->getBuildList();
			buildBySupplies( growth ? GROWTH_EXPAND_MIN_CASH : EXPAND_MIN_CASH, center->getName() );
			noteOwnOrder( headBefore );
			if (countOwnOrPlanned( center ) > before)
			{
				++m_economyOrders;
				char line[224];
				WWFormat::Into(line, "KIWIRTSCHAFT {:d} spieler {:d} ausbau {} vorrat {:.0f} zentren {:d} grund {}", now, m_player->getPlayerIndex(), center->getName().str(), reserve, centers, growth ? "wachstum" : "knapp");
				GameEngine_RunnerLog( line );
			}
		}
	}

	// Hacker target: base growing with time, more with weak supplies, plus extra with at most one
	// depot with content or without a supply center; scaled by the difficulty.
	Int hackerTarget = 0;
	if (growthNow >= HACKER_START)
	{
		hackerTarget = (growthNow >= HACKER_MORE_START) ? HACKERS_BASE_LATE : HACKERS_BASE;
		if (depleted)
			hackerTarget += HACKERS_DEPLETING_EXTRA + HACKERS_DEPLETED_EXTRA;
		else if (depleting)
			hackerTarget += HACKERS_DEPLETING_EXTRA;
		if (depots <= 1)
			hackerTarget += HACKERS_FEW_DEPOTS_EXTRA;
		if (centers == 0)
			hackerTarget += HACKERS_NO_CENTER_EXTRA;
	}
	// Hackers as investment: while one pays back its cost soon enough at the lowest rank, the target
	// grows with the game time (they rank up by hacking and pay more and more).
	Int hackerCap = HACKERS_MAX;
	const ThingTemplate *hacker = (growthNow >= HACKER_INVEST_START) ? findHackerTemplate() : nullptr;
	Bool hasCenter = FALSE;
	for (Object *obj = TheGameLogic->getFirstObject(); obj && !hasCenter && hacker; obj = obj->getNextObject())
		hasCenter = obj->getControllingPlayer() == m_player && !obj->isEffectivelyDead() && isEconomyKind( obj->getTemplate(), KINDOF_FS_INTERNET_CENTER );
	const HackerEconomy he = hackerEconomy( hacker, m_player, hasCenter );
	if (he.valid && he.incomePerMinute[LEVEL_REGULAR] > 0.0f)
	{
		const Real payback = static_cast<Real>( hacker->calcCostToBuild( m_player ) ) / he.incomePerMinute[LEVEL_REGULAR];
		if (invest && payback <= HACKER_PAYBACK_MAX)
		{
			const Int hackerInvest = std::min( HACKERS_INVEST_MAX, HACKER_INVEST_BASE + static_cast<Int>( (growthNow - HACKER_INVEST_START) / HACKER_INVEST_STEP ) );
			hackerTarget = std::max( hackerTarget, hackerInvest );
			hackerCap = std::max( hackerCap, hackerInvest );
		}
	}
	// Lead time: enough hackers to replace the depot income at the elite rank, started early enough
	// to reach that rank before the depots run dry.
	if (now >= HACKER_LEAD_START)
	{
		if (he.valid && he.incomePerMinute[LEVEL_ELITE] > 0.0f && m_gatherRate > 0.0f)
		{
			const Real eliteMinutes = static_cast<Real>(he.framesToElite) / static_cast<Real>(60 * LOGICFRAMES_PER_SECOND);
			const Int need = std::min( HACKERS_MAX, static_cast<Int>(m_gatherRate / he.incomePerMinute[LEVEL_ELITE] + 0.999f) );
			const Bool lead = depleted || minutesLeft <= eliteMinutes + HACKER_LEAD_MARGIN;
			if (lead && !m_hackerLead)
			{
				char line[256];
				WWFormat::Into(line, "KIWIRTSCHAFT {:d} spieler {:d} vorlauf restzeit_min {:.1f} sammelrate {:.0f} elite_min {:.1f} ertrag_elite {:.0f} bedarf {:d}", now, m_player->getPlayerIndex(), minutesLeft, m_gatherRate, eliteMinutes, he.incomePerMinute[LEVEL_ELITE], need);
				GameEngine_RunnerLog( line );
			}
			m_hackerLead = lead;
			if (lead)
				hackerTarget = std::max( hackerTarget, need );
		}
	}
	if (hackerTarget > 0)
	{
		hackerTarget = std::min( hackerTarget, hackerCap );
		hackerTarget = static_cast<Int>(static_cast<Real>(hackerTarget) * paceFor( getAIDifficulty(), m_personality ).economyScale + 0.5f);
	}

	// Growth buildings wait while another one waits in the build list (budget).
	auto growthSlot = [&]() { return invest && !growthBuildPending(); };

	// Permanent income: when the depots run low (planned without a money check), and as investment
	// from minute 3 (a second from minute 6) when the money is there. Internet centres follow the
	// hacker target.
	{
		const KindOfType kinds[] = { KINDOF_FS_SUPPLY_DROPZONE, KINDOF_FS_BLACK_MARKET, KINDOF_FS_INTERNET_CENTER };
		for (size_t k = 0; k < sizeof( kinds ) / sizeof( kinds[0] ); ++k)
		{
			const ThingTemplate *t = findConstructable( kinds[k] );
			if (t == nullptr)
				continue;
			const Bool center = kinds[k] == KINDOF_FS_INTERNET_CENTER;
			const Int cost = t->calcCostToBuild( m_player );
			Int wanted = 0;
			if (depleting)
				wanted = (depleted && !center) ? 2 : 1;
			if (center)
			{
				if (growthSlot() && growthNow >= GROWTH_PERMANENT_START && hackerTarget > 0 && freeMoney() - cost >= GROWTH_PERMANENT_RESERVE)
					wanted = std::max( wanted, (hackerTarget + HACKERS_PER_CENTER - 1) / HACKERS_PER_CENTER );
			}
			else
			{
				if (growthSlot() && growthNow >= GROWTH_PERMANENT_START && freeMoney() - cost >= GROWTH_PERMANENT_RESERVE)
					wanted = std::max( wanted, 1 );
				if (growthSlot() && growthNow >= GROWTH_SECOND_START && freeMoney() - cost >= GROWTH_SECOND_RESERVE)
					wanted = std::max( wanted, 2 );
			}
			Int have = countOwnOrPlanned( t );
			if (have >= wanted)
				continue;
			if (queueBaseBuilding( t ))
			{
				++m_economyOrders;
				char line[224];
				WWFormat::Into(line, "KIWIRTSCHAFT {:d} spieler {:d} bau {} bestand {:d} ziel {:d} vorrat {:.0f} grund {}", now, m_player->getPlayerIndex(), t->getName().str(), have, wanted, reserve, depleting ? "knapp" : "wachstum");
				GameEngine_RunnerLog( line );
			}
		}
	}
	Int trucks = 0;
	if (now >= ((cashClass() == CashClass::LOW) ? LOW_TRUCK_START : HACKER_START / 2))
		trucks = manageSupplyTrucks( centers, depots );
	m_unitReserve = baseReserve();
	Int hackers = manageHackers( hackerTarget );
	m_unitReserve = 0;
	updateTechCapture();

	// Cost of planned, not yet started economy buildings: the late game logic holds back this much.
	m_economyNeed = 0;
	for (BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext())
	{
		if (!info->isPriorityBuild() || !info->isBuildable() || TheGameLogic->findObjectByID( info->getObjectID() ) != nullptr)
			continue;
		const ThingTemplate *t = TheThingFactory->findTemplate( info->getTemplateName() );
		if (isEconomyKind( t, KINDOF_FS_SUPPLY_CENTER ) || isEconomyKind( t, KINDOF_FS_SUPPLY_DROPZONE ) || isEconomyKind( t, KINDOF_FS_BLACK_MARKET )
			|| isEconomyKind( t, KINDOF_FS_INTERNET_CENTER ))
			m_economyNeed += t->calcCostToBuild( m_player );
	}

	// Status report every minute (income per minute is measured from KISTAT).
	if (now >= m_nextEconomyReportFrame)
	{
		m_nextEconomyReportFrame = now + 60 * LOGICFRAMES_PER_SECOND;
		// plus own income buildings, own and free tech buildings
		Int incomeBuildings = 0, ownTech = 0, freeTech = 0;
		for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
		{
			if (obj->isEffectivelyDead())
				continue;
			const ThingTemplate *t = obj->getTemplate();
			if (obj->getControllingPlayer() == m_player && (isEconomyKind( t, KINDOF_FS_SUPPLY_DROPZONE ) || isEconomyKind( t, KINDOF_FS_BLACK_MARKET )
				|| isEconomyKind( t, KINDOF_FS_INTERNET_CENTER )))
				++incomeBuildings;
			if (obj->isKindOf( KINDOF_TECH_BUILDING ) && obj->isKindOf( KINDOF_CAPTURABLE ))
			{
				Player *owner = obj->getControllingPlayer();
				if (owner == m_player)
					++ownTech;
				else if (owner == nullptr || owner->getDefaultTeam() == nullptr || m_player->getRelationship( owner->getDefaultTeam() ) == NEUTRAL)
					++freeTech;
			}
		}
		if (!partOff( AI_PART_BUDGET ))
		{
			char budget[160];
			WWFormat::Into(budget, "KIHAUSHALT {:d} spieler {:d} geld {:d} basis {:d} frei {:d} vorrang_offen {:d}", now, m_player->getPlayerIndex(), money, baseReserve(), freeMoney(), growthBuildPending() ? 1 : 0);
			GameEngine_RunnerLog( budget );
		}
		char line[320];
		WWFormat::Into(line, "KIWIRTSCHAFT {:d} spieler {:d} lage vorrat {:.0f} zentren {:d} geld {:d} hacker {:d} sammler {:d} bedarf {:d} einnahmebauten {:d} technik {:d} frei {:d} sammelrate {:.0f} restzeit_min {:.1f} investieren {:d}", now, m_player->getPlayerIndex(), reserve, centers, money, hackers, trucks, m_economyNeed, incomeBuildings, ownTech, freeTech, m_gatherRate, minutesLeft, invest ? 1 : 0);
		GameEngine_RunnerLog( line );
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AICommanderPlayer::crc( Xfer *xfer )
{
	AISkirmishPlayer::crc( xfer );
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info;
	* 1: Initial version
	* 2: common enemy and attack mission
	* 3: late game, counters
	* 4: sustainable economy
	* 5: planner
	* 6: supply forecast
	* 7: attack target from the planner
	* 8: raids
	* 9: fight control
	* 10: forecast confirmation, regrow after a broken-off attack
	* 11: memory of seen enemies, scout
	* 12: enemy model (time of the last early rush)
	* 13: enemy model (last frames of the fortified, air and stealth patterns)
	* 14: superweapon waiting since
	* 15: personality, defences ordered
	* 16: beacons with human partners
	* 17: sites of the turtle's defences instead of their count
	* 18: own priority builds waiting
	* 19: kind and end of the own beacon */
// ------------------------------------------------------------------------------------------------
void AICommanderPlayer::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 19;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	AISkirmishPlayer::xfer( xfer );

	xfer->xferInt( &m_assistAlly );
	xfer->xferCoord3D( &m_assistPos );
	xfer->xferCoord3D( &m_assistAnchor );
	xfer->xferUnsignedInt( &m_lastReinforceFrame );
	xfer->xferReal( &m_assistThreat );
	xfer->xferUnsignedInt( &m_assistStartFrame );
	xfer->xferUnsignedInt( &m_assistLastThreatFrame );
	xfer->xferUnsignedInt( &m_nextAssistCheckFrame );
	xfer->xferUnsignedInt( &m_assistCooldownFrame );
	xfer->xferInt( &m_assistMissions );

	UnsignedShort count = static_cast<UnsignedShort>(m_helpers.size());
	xfer->xferUnsignedShort( &count );
	if (xfer->getXferMode() == XFER_LOAD)
		m_helpers.assign( count, INVALID_ID );
	for (UnsignedShort i = 0; i < count; ++i)
		xfer->xferObjectID( &m_helpers[i] );

	if (version >= 2)
	{
		// The current enemy itself (m_currentEnemy) is not saved, as in the classic AI.
		xfer->xferUnsignedInt( &m_enemyChosenFrame );
		Int phase = static_cast<Int>(m_attackPhase);
		xfer->xferInt( &phase );
		m_attackPhase = static_cast<CommanderAttackPhase>(phase);
		xfer->xferInt( &m_attackEnemy );
		xfer->xferCoord3D( &m_stagePos );
		xfer->xferCoord3D( &m_objectivePos );
		xfer->xferObjectID( &m_objectiveID );
		xfer->xferUnsignedInt( &m_attackPhaseFrame );
		xfer->xferReal( &m_attackStartValue );
		xfer->xferUnsignedInt( &m_attackReinforceFrame );
		xfer->xferUnsignedInt( &m_attackCooldownFrame );
		xfer->xferBool( &m_gatherReady );
		xfer->xferInt( &m_attackMissions );
		UnsignedShort attackers = static_cast<UnsignedShort>(m_attackers.size());
		xfer->xferUnsignedShort( &attackers );
		if (xfer->getXferMode() == XFER_LOAD)
			m_attackers.assign( attackers, INVALID_ID );
		for (UnsignedShort i = 0; i < attackers; ++i)
			xfer->xferObjectID( &m_attackers[i] );
	}
	if (version >= 3)
	{
		xfer->xferInt( &m_unitsBuilt );
		xfer->xferInt( &m_upgradesBought );
	}
	if (version >= 4)
	{
		xfer->xferUnsignedInt( &m_nextEconomyFrame );
		xfer->xferUnsignedInt( &m_nextExpandFrame );
		xfer->xferUnsignedInt( &m_nextEconomyReportFrame );
		xfer->xferInt( &m_economyOrders );
		xfer->xferInt( &m_economyNeed );
		xfer->xferObjectID( &m_captureUnit );
		xfer->xferObjectID( &m_captureTarget );
		xfer->xferUnsignedInt( &m_captureFrame );
		xfer->xferInt( &m_captures );
	}
	if (version >= 5)
		m_planner.xfer( xfer );
	if (version >= 6)
	{
		xfer->xferReal( &m_reserveMark );
		xfer->xferUnsignedInt( &m_reserveMarkFrame );
		xfer->xferReal( &m_gatherRate );
		xfer->xferBool( &m_hackerLead );
	}
	if (version >= 7)
		xfer->xferUnsignedInt( &m_objectiveFrame );
	if (version >= 8)
	{
		Int phase = static_cast<Int>( m_raidPhase );
		xfer->xferInt( &phase );
		m_raidPhase = static_cast<CommanderRaidPhase>( phase );
		xfer->xferObjectID( &m_raidTransport );
		UnsignedShort riders = static_cast<UnsignedShort>( m_raiders.size() );
		xfer->xferUnsignedShort( &riders );
		if (xfer->getXferMode() == XFER_LOAD)
			m_raiders.assign( riders, INVALID_ID );
		for (UnsignedShort i = 0; i < riders; ++i)
			xfer->xferObjectID( &m_raiders[i] );
		xfer->xferObjectID( &m_raidTarget );
		xfer->xferUnsignedInt( &m_raidPhaseFrame );
		xfer->xferUnsignedInt( &m_raidCooldownFrame );
		xfer->xferUnsignedInt( &m_raidTargetFrame );
		xfer->xferInt( &m_raidRidersWanted );
		xfer->xferAsciiString( &m_raidTransportName );
		xfer->xferAsciiString( &m_raidRiderName );
		xfer->xferInt( &m_raids );
	}
	if (version >= 9)
	{
		xfer->xferUnsignedInt( &m_nextMicroFrame );
		xfer->xferUnsignedInt( &m_nextSpreadFrame );
		xfer->xferInt( &m_microFocus );
		xfer->xferInt( &m_microKite );
		xfer->xferInt( &m_microSpread );
		xfer->xferUnsignedInt( &m_nextMicroReportFrame );
	}
	if (version >= 10)
	{
		xfer->xferUnsignedInt( &m_forecastPlanFrame );
		xfer->xferInt( &m_forecastBad );
		xfer->xferReal( &m_regrowValue );
		xfer->xferUnsignedInt( &m_regrowUntil );
	}
	if (version >= 11)
	{
		UnsignedInt known = static_cast<UnsignedInt>( m_known.size() );
		xfer->xferUnsignedInt( &known );
		if (xfer->getXferMode() == XFER_LOAD)
			m_known.assign( known, KnownObject() );
		for (UnsignedInt i = 0; i < known; ++i)
		{
			KnownObject &k = m_known[i];
			xfer->xferObjectID( &k.id );
			xfer->xferInt( &k.owner );
			xfer->xferCoord3D( &k.pos );
			xfer->xferUnsignedInt( &k.frame );
			xfer->xferReal( &k.value );
			xfer->xferBool( &k.structure );
		}
		xfer->xferObjectID( &m_scout );
		xfer->xferCoord3D( &m_scoutTarget );
		xfer->xferUnsignedInt( &m_scoutFrame );
		xfer->xferUnsignedInt( &m_startsSeen );
	}
	if (version >= 12)
		xfer->xferUnsignedInt( &m_rushSeenFrame );
	if (version >= 13)
	{
		xfer->xferUnsignedInt( &m_turtleSeenFrame );
		xfer->xferUnsignedInt( &m_airSeenFrame );
		xfer->xferUnsignedInt( &m_stealthSeenFrame );
	}
	if (version >= 14)
		xfer->xferUnsignedInt( &m_strikeHeldSince );
	if (version >= 15)
	{
		xfer->xferInt( &m_personality );
		if (version < 17)
		{
			Int ordered = 0;	// the old count; the sites are not known, so the turtle orders anew
			xfer->xferInt( &ordered );
		}
	}
	if (version >= 16)
	{
		xfer->xferObjectID( &m_markSeen );
		xfer->xferObjectID( &m_markBeacon );
		xfer->xferCoord3D( &m_markPos );
		xfer->xferUnsignedInt( &m_markFrame );
		xfer->xferInt( &m_markEnemy );
		xfer->xferObjectID( &m_ownBeacon );
		Bool help = m_ownBeaconKind == OWN_BEACON_HELP;
		xfer->xferBool( &help );
		if (xfer->getXferMode() == XFER_LOAD)
			m_ownBeaconKind = help ? OWN_BEACON_HELP : OWN_BEACON_ATTACK;
	}
	if (version >= 17)
	{
		UnsignedShort sites = static_cast<UnsignedShort>( m_defenceSites.size() );
		xfer->xferUnsignedShort( &sites );
		if (xfer->getXferMode() == XFER_LOAD)
			m_defenceSites.resize( sites );
		for (UnsignedShort i = 0; i < sites; ++i)
		{
			xfer->xferCoord3D( &m_defenceSites[i].pos );
			xfer->xferUnsignedInt( &m_defenceSites[i].frame );
		}
	}
	if (version >= 18)
	{
		UnsignedShort orders = static_cast<UnsignedShort>( m_ownOrders.size() );
		xfer->xferUnsignedShort( &orders );
		if (xfer->getXferMode() == XFER_LOAD)
			m_ownOrders.resize( orders );
		for (UnsignedShort i = 0; i < orders; ++i)
		{
			xfer->xferAsciiString( &m_ownOrders[i].name );
			xfer->xferCoord3D( &m_ownOrders[i].pos );
			xfer->xferUnsignedInt( &m_ownOrders[i].frame );
		}
	}
	if (version >= 19)
	{
		xfer->xferInt( &m_ownBeaconKind );
		xfer->xferUnsignedInt( &m_ownBeaconUntil );
	}
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AICommanderPlayer::loadPostProcess( void )
{
	AISkirmishPlayer::loadPostProcess();
	m_planner.loadPostProcess();
}
