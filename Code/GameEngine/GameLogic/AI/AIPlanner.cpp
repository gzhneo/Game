/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// AIPlanner.cpp: planning of the new AI on the job system. Overview in AIPlanner.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "formatinto.h"
#include "wwtracy.h"

#include <algorithm>
#include <chrono>

#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/AIPlanner.h"
#include "GameLogic/Armor.h"
#include "GameLogic/ArmorSet.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSet.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/BodyModule.h"

namespace
{
	constexpr Int TIME_REPORT_PLANS = 30;		///< KIZEIT after this many plans (30 s)
	constexpr Int FORECAST_REPORT_PLANS = 10;	///< KIPROGNOSE and KIKONTER every this many plans (10 s)
	constexpr Real SUICIDE_SECONDS = 10.0f;	///< a weapon that destroys its carrier counts as one hit in this time
	constexpr Real MAP_CELL = 250.0f;				///< cell size of the influence maps (world units)
	constexpr Real SITE_RADIUS = 700.0f;		///< enemies this close to the attack force fight it
	constexpr Real DEFENCE_RADIUS = 600.0f;	///< armed enemies this close to a target building defend it
	constexpr Real DISTANCE_SCALE = 3000.0f;	///< a target this much farther away counts half

	// Damage types that lower health. The others heal, capture, disable, subdue or kill only pilots
	// or passengers; the forecast leaves them out.
	Bool countsAsHealthDamage( DamageType t )
	{
		switch (t)
		{
			case DAMAGE_HEALING: case DAMAGE_DEPLOY: case DAMAGE_SURRENDER: case DAMAGE_HACK: case DAMAGE_KILLPILOT:
			case DAMAGE_PENALTY: case DAMAGE_FALLING: case DAMAGE_DISARM: case DAMAGE_HAZARD_CLEANUP: case DAMAGE_TOPPLING:
			case DAMAGE_SUBDUAL_MISSILE: case DAMAGE_SUBDUAL_VEHICLE: case DAMAGE_SUBDUAL_BUILDING: case DAMAGE_SUBDUAL_UNRESISTABLE:
			case DAMAGE_KILL_GARRISONED: case DAMAGE_STATUS:
				return FALSE;
			default:
				return TRUE;
		}
	}

	// Maximum health from the body module of a template (all bodies with health share ActiveBody's data).
	Real templateMaxHealth( const ThingTemplate *t )
	{
		static const char *const bodies[] = { "ActiveBody", "StructureBody", "HiveStructureBody", "UndeadBody", "HighlanderBody", "ImmortalBody" };
		const ModuleInfo &modules = t->getBehaviorModuleInfo();
		for (Int i = 0; i < modules.getCount(); ++i)
		{
			const AsciiString name = modules.getNthName( i );
			for (size_t b = 0; b < sizeof( bodies ) / sizeof( bodies[0] ); ++b)
			{
				if (name.compareNoCase( bodies[b] ) == 0)
					return static_cast<const ActiveBodyModuleData *>( modules.getNthData( i ) )->m_maxHealth;
			}
		}
		return 0.0f;
	}

	// The armor and weapon set without conditions (the one a fresh unit has); else the first.
	const ArmorTemplate *defaultArmor( const ThingTemplate *t )
	{
		const ArmorTemplateSetVector &sets = t->getArmorTemplateSets();
		for (size_t i = 0; i < sets.size(); ++i)
		{
			if (!sets[i].getNthConditionsYes( 0 ).any())
				return sets[i].getArmorTemplate();
		}
		return sets.empty() ? nullptr : sets[0].getArmorTemplate();
	}

	const WeaponTemplateSet *defaultWeapons( const ThingTemplate *t )
	{
		const WeaponTemplateSetVector &sets = t->getWeaponTemplateSets();
		for (size_t i = 0; i < sets.size(); ++i)
		{
			if (!sets[i].getNthConditionsYes( 0 ).any())
				return &sets[i];
		}
		return sets.empty() ? nullptr : &sets[0];
	}

	// Damage per second of a weapon without bonuses: shots of a clip at the mean delay, then the reload.
	Real weaponDps( const WeaponTemplate *w )
	{
		const WeaponBonus none;
		const Real damage = w->getPrimaryDamage( none );
		if (damage <= 0.0f || !countsAsHealthDamage( w->getDamageType() ))
			return 0.0f;
		if (w->getDamageDealtAtSelfPosition())
			return damage / SUICIDE_SECONDS;
		Real delay = w->getMeanDelayBetweenShots();
		if (delay < 1.0f)
			delay = 1.0f;
		const Int clip = w->getClipSize();
		Real shotsPerFrame = 1.0f / delay;
		if (clip > 0)
			shotsPerFrame = static_cast<Real>(clip) / (static_cast<Real>(clip) * delay + static_cast<Real>(w->getClipReloadTime( none )));
		return damage * shotsPerFrame * static_cast<Real>(LOGICFRAMES_PER_SECOND);
	}

	// Health of a snapshot object in the units of the combat table.
	Real objectHealth( const AICombat::Table &table, const AIPlanObject &o )
	{
		if (o.profile < 0)
			return 0.0f;
		return table.profiles[static_cast<size_t>(o.profile)].maxHealth * static_cast<Real>(o.health) / 1000.0f;
	}

	// Adds health to the group of a profile (groups in order of first appearance).
	void addToForce( std::vector<AICombat::Group> *force, Int profile, Real health )
	{
		if (profile < 0 || health <= 0.0f)
			return;
		for (size_t i = 0; i < force->size(); ++i)
		{
			if ((*force)[i].profile == profile)
			{
				(*force)[i].health += health;
				return;
			}
		}
		AICombat::Group g;
		g.profile = profile;
		g.health = health;
		force->push_back( g );
	}

	Bool hasWeapon( const AICombat::Table &table, Int profile )
	{
		if (profile < 0)
			return FALSE;
		const size_t base = static_cast<size_t>(profile) * static_cast<size_t>(table.slots()) * 2;
		for (size_t i = 0; i < static_cast<size_t>(table.slots()) * 2; ++i)
		{
			if (table.dps[base + i] > 0.0f)
				return TRUE;
		}
		return FALSE;
	}

	long long microsSince( std::chrono::steady_clock::time_point start )
	{
		return std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now() - start ).count();
	}

	// FNV-1a over 32-bit values.
	inline UnsignedInt mix( UnsignedInt hash, UnsignedInt value )
	{
		for (Int i = 0; i < 4; ++i)
		{
			hash ^= (value >> (8 * i)) & 0xFFu;
			hash *= 16777619u;
		}
		return hash;
	}

	Int relationOf( Player *self, Player *owner )
	{
		if (owner == self)
			return AI_PLAN_SELF;
		if (owner == nullptr || owner == ThePlayerList->getNeutralPlayer() || owner->getDefaultTeam() == nullptr
			|| self->getDefaultTeam() == nullptr)
			return AI_PLAN_NEUTRAL;
		const Relationship mine = self->getRelationship( owner->getDefaultTeam() );
		if (mine == ENEMIES)
			return AI_PLAN_ENEMY;
		if (mine == ALLIES && owner->getRelationship( self->getDefaultTeam() ) == ALLIES)
			return AI_PLAN_ALLY;
		return AI_PLAN_NEUTRAL;
	}

	// Seen by the player's own sight: the cell under the object (a pure read of the partition
	// cells, unlike Object::getShroudedStatus, which updates cached state).
	CellShroudStatus cellStatus( Int playerIndex, const Object *obj )
	{
		return ThePartitionManager->getShroudStatusForPlayer( playerIndex, obj->getPosition() );
	}
}

// ------------------------------------------------------------------------------------------------
// Influence maps and the attack (AICombat::planAttack): target choice and forecasts for the
// running attack force.
static void planAttack( const AIPlanInput &input, const AICombat::Table &table, AIPlanResult *r )
{
	std::vector<AICombat::Unit> units;
	units.reserve( input.objects.size() );
	for (const AIPlanObject &o : input.objects)
	{
		if (o.relation != AI_PLAN_SELF && o.relation != AI_PLAN_ENEMY)
			continue;
		AICombat::Unit u;
		u.id = o.id;
		u.profile = o.profile;
		u.health = objectHealth( table, o );
		u.x = static_cast<Real>( o.x );
		u.y = static_cast<Real>( o.y );
		u.cost = static_cast<Real>( o.cost );
		u.own = o.relation == AI_PLAN_SELF;
		u.enemy = o.relation == AI_PLAN_ENEMY;
		u.focus = u.enemy && (input.focusEnemy < 0 || o.owner == input.focusEnemy);
		u.structure = (o.flags & AIPlanObject::STRUCTURE) != 0;
		u.worker = (o.flags & AIPlanObject::WORKER) != 0;
		u.superweapon = (o.flags & AIPlanObject::SUPERWEAPON) != 0;
		u.economic = (o.flags & (AIPlanObject::WORKER | AIPlanObject::ECONOMY)) != 0;
		units.push_back( u );
	}
	std::vector<unsigned> mission( input.mission.begin(), input.mission.end() );
	std::sort( mission.begin(), mission.end() );
	AICombat::AttackParams params;
	params.cell = MAP_CELL;
	params.siteRadius = SITE_RADIUS;
	params.defenceRadius = DEFENCE_RADIUS;
	params.distanceScale = DISTANCE_SCALE;
	const AICombat::AttackPlan plan = AICombat::planAttack( table, static_cast<Real>( input.worldWidth ),
		static_cast<Real>( input.worldHeight ), units, mission, params );
	r->objectiveId = plan.objectiveId;
	r->objectiveX = static_cast<Int>( plan.objectiveX );
	r->objectiveY = static_cast<Int>( plan.objectiveY );
	r->objectiveWinner = plan.objectiveWinner;
	r->objectiveRatio = plan.objectiveRatio;
	r->objectiveThreat = plan.objectiveThreat;
	r->siteWinner = plan.siteWinner;
	r->siteRatio = plan.siteRatio;
	r->siteEnemyHealth = static_cast<Int>( plan.siteEnemyHealth );
	r->maxThreat = plan.maxThreat;

	if (!input.raid.empty())
	{
		std::vector<unsigned> raid( input.raid.begin(), input.raid.end() );
		std::sort( raid.begin(), raid.end() );
		params.economicTargets = true;
		const AICombat::AttackPlan raidPlan = AICombat::planAttack( table, static_cast<Real>( input.worldWidth ),
			static_cast<Real>( input.worldHeight ), units, raid, params );
		r->raidTargetId = raidPlan.objectiveId;
		r->raidTargetX = static_cast<Int>( raidPlan.objectiveX );
		r->raidTargetY = static_cast<Int>( raidPlan.objectiveY );
		r->raidTargetWinner = raidPlan.objectiveWinner;
		r->raidTargetRatio = raidPlan.objectiveRatio;
		r->raidSiteWinner = raidPlan.siteWinner;
		r->raidSiteRatio = raidPlan.siteRatio;
		r->raidSiteEnemyHealth = static_cast<Int>( raidPlan.siteEnemyHealth );
	}
}

// ------------------------------------------------------------------------------------------------
AIPlanResult AIPlanner_compute( const AIPlanInput &input, const AICombat::Table &table )
{
	PROF_ZONE( "AIPlanner::compute" );
	AIPlanResult r;
	r.frame = input.frame;
	UnsignedInt hash = 2166136261u;
	hash = mix( hash, input.frame );
	hash = mix( hash, static_cast<UnsignedInt>( input.self ) );
	hash = mix( hash, static_cast<UnsignedInt>( input.money ) );
	hash = mix( hash, static_cast<UnsignedInt>( input.focusEnemy ) );
	hash = mix( hash, static_cast<UnsignedInt>( input.worldWidth ) );
	hash = mix( hash, static_cast<UnsignedInt>( input.worldHeight ) );
	for (UnsignedInt id : input.mission)
		hash = mix( hash, id );
	for (UnsignedInt id : input.raid)
		hash = mix( hash, id );
	for (const AIPlanCandidate &c : input.candidates)
	{
		hash = mix( hash, static_cast<UnsignedInt>( c.profile ) );
		hash = mix( hash, static_cast<UnsignedInt>( c.cost ) );
	}
	std::vector<AICombat::Group> ownField, enemyField, enemyDefence;
	for (const AIPlanObject &o : input.objects)
	{
		hash = mix( hash, static_cast<UnsignedInt>( o.profile ) );
		hash = mix( hash, o.id );
		hash = mix( hash, static_cast<UnsignedInt>( o.owner ) );
		hash = mix( hash, static_cast<UnsignedInt>( o.relation ) );
		hash = mix( hash, o.flags );
		hash = mix( hash, static_cast<UnsignedInt>( o.cost ) );
		hash = mix( hash, static_cast<UnsignedInt>( o.x ) );
		hash = mix( hash, static_cast<UnsignedInt>( o.y ) );
		hash = mix( hash, static_cast<UnsignedInt>( o.health ) );

		const Bool structure = (o.flags & AIPlanObject::STRUCTURE) != 0;
		const Bool mobile = !structure && (o.flags & AIPlanObject::WORKER) == 0;
		if (o.flags & AIPlanObject::SUPPLY)
			++r.suppliesKnown;
		switch (o.relation)
		{
			case AI_PLAN_SELF:
				if (structure)
					++r.ownStructures;
				else if (mobile)
				{
					++r.ownUnits;
					r.ownValue += o.cost;
					addToForce( &ownField, o.profile, objectHealth( table, o ) );
				}
				break;
			case AI_PLAN_ALLY:
				if (mobile)
					r.allyValue += o.cost;
				break;
			case AI_PLAN_ENEMY:
			{
				const Bool focus = input.focusEnemy < 0 || o.owner == input.focusEnemy;
				if (structure)
				{
					++r.enemyStructuresKnown;
					if (focus && hasWeapon( table, o.profile ))
						addToForce( &enemyDefence, o.profile, objectHealth( table, o ) );
				}
				else if (mobile)
				{
					++r.enemyUnitsSeen;
					r.enemyValueSeen += o.cost;
					if (focus)
						addToForce( &enemyField, o.profile, objectHealth( table, o ) );
				}
				break;
			}
			default:
				break;
		}
	}
	r.checksum = hash;

	// Forecast: own field army against the enemy's seen field army, and against it plus his defences.
	std::vector<AICombat::Group> enemyBase = enemyField;
	for (const AICombat::Group &g : enemyDefence)
		addToForce( &enemyBase, g.profile, g.health );
	const AICombat::Outcome field = AICombat::predict( table, ownField, enemyField );
	r.fieldWinner = field.winner;
	r.fieldSurvivors = (field.winner == 0) ? field.survivorsA : ((field.winner == 1) ? field.survivorsB : 0.0f);
	r.fieldRatio = field.strengthRatio;
	const AICombat::Outcome base = AICombat::predict( table, ownField, enemyBase );
	r.baseWinner = base.winner;
	r.baseSurvivors = (base.winner == 0) ? base.survivorsA : ((base.winner == 1) ? base.survivorsB : 0.0f);
	r.baseRatio = base.strengthRatio;

	// Counter production against the enemy mix (field army and defences).
	r.enemyHealthSeen = static_cast<Int>( AICombat::totalHealth( enemyBase ) );
	if (!enemyBase.empty())
	{
		for (const AIPlanCandidate &c : input.candidates)
		{
			AIPlanChoice choice;
			choice.profile = c.profile;
			choice.score = AICombat::counterScore( table, c.profile, enemyBase );
			if (c.cost > 0 && c.profile >= 0)
			{
				// The table holds the base build cost; the player may pay another price.
				const Real baseCost = table.profiles[static_cast<size_t>(c.profile)].cost;
				if (baseCost > 0.0f)
					choice.score *= baseCost / static_cast<Real>(c.cost);
			}
			if (choice.score > 0.0f)
				r.counters.push_back( choice );
		}
		std::stable_sort( r.counters.begin(), r.counters.end(), []( const AIPlanChoice &a, const AIPlanChoice &b )
		{
			if (a.score != b.score)
				return a.score > b.score;
			return a.profile < b.profile;
		} );
	}
	planAttack( input, table, &r );
	return r;
}

// ------------------------------------------------------------------------------------------------
// Zero Hour marks superweapons with FS_SUPERWEAPON; the Generals data lacks the kind, there the
// name of the template decides.
Bool AIPlanner_isSuperweapon( const ThingTemplate *t )
{
	if (t == nullptr)
		return FALSE;
	if (t->isKindOf( KINDOF_FS_SUPERWEAPON ))
		return TRUE;
	static const char *const names[] = { "ScudStorm", "NuclearMissile", "ParticleCannon", "ParticleUplink" };
	const char *name = t->getName().str();
	for (size_t i = 0; i < sizeof( names ) / sizeof( names[0] ); ++i)
	{
		if (strstr( name, names[i] ) != nullptr)
			return TRUE;
	}
	return FALSE;
}

// ------------------------------------------------------------------------------------------------
void AIPlanner_buildCombatTable( AICombat::Table *table, std::vector<Int> *profileOfTemplate,
	std::vector<const ThingTemplate *> *templateOfProfile )
{
	PROF_ZONE( "AIPlanner::buildCombatTable" );
	// Rows: every unit and building template, in the order of the template list.
	std::vector<const ThingTemplate *> rows;
	std::vector<const ArmorTemplate *> armors;
	UnsignedShort maxId = 0;
	for (const ThingTemplate *t = TheThingFactory->firstTemplate(); t; t = t->friend_getNextTemplate())
	{
		const ThingTemplate *final = static_cast<const ThingTemplate *>( t->getFinalOverride() );
		if (final->getTemplateID() > maxId)
			maxId = final->getTemplateID();
		if (t->getTemplateID() > maxId)
			maxId = t->getTemplateID();
		if (!final->isKindOf( KINDOF_INFANTRY ) && !final->isKindOf( KINDOF_VEHICLE ) && !final->isKindOf( KINDOF_AIRCRAFT )
			&& !final->isKindOf( KINDOF_STRUCTURE ))
			continue;
		if (final->isKindOf( KINDOF_PROJECTILE ))
			continue;
		rows.push_back( final );
		const ArmorTemplate *armor = defaultArmor( final );
		if (armor && std::find( armors.begin(), armors.end(), armor ) == armors.end())
			armors.push_back( armor );
	}

	table->resize( static_cast<Int>( rows.size() ), static_cast<Int>( armors.size() ) );
	profileOfTemplate->assign( static_cast<size_t>( maxId ) + 1, -1 );
	*templateOfProfile = rows;
	for (size_t p = 0; p < rows.size(); ++p)
	{
		const ThingTemplate *t = rows[p];
		(*profileOfTemplate)[t->getTemplateID()] = static_cast<Int>( p );
		AICombat::Profile &profile = table->profiles[p];
		profile.maxHealth = templateMaxHealth( t );
		profile.cost = static_cast<Real>( t->friend_getBuildCost() );
		profile.air = t->isKindOf( KINDOF_AIRCRAFT );
		profile.structure = t->isKindOf( KINDOF_STRUCTURE );
		const ArmorTemplate *armor = defaultArmor( t );
		profile.armor = armor ? static_cast<Int>( std::find( armors.begin(), armors.end(), armor ) - armors.begin() ) : -1;

		const WeaponTemplateSet *weapons = defaultWeapons( t );
		if (weapons == nullptr)
			continue;
		for (Int slot = 0; slot < WEAPONSLOT_COUNT; ++slot)
		{
			const WeaponTemplate *w = weapons->getNth( static_cast<WeaponSlotType>( slot ) );
			if (w == nullptr)
				continue;
			const Real raw = weaponDps( w );
			if (raw <= 0.0f)
				continue;
			const Real reach = w->getAttackRange( WeaponBonus() );
			if (reach > profile.range)
				profile.range = reach;
			const Real splash = w->getPrimaryDamageRadius( WeaponBonus() );
			if (splash > profile.splash)
				profile.splash = splash;
			const Bool ground = (w->getAntiMask() & WEAPON_ANTI_GROUND) != 0;
			const Bool air = (w->getAntiMask() & WEAPON_ANTI_AIRBORNE_VEHICLE) != 0;
			for (Int a = -1; a < static_cast<Int>( armors.size() ); ++a)
			{
				const Real dps = (a < 0) ? raw : armors[static_cast<size_t>( a )]->adjustDamage( w->getDamageType(), raw );
				// A unit fires one weapon at a time: the best one against this target counts.
				if (ground && dps > table->dpsRef( static_cast<Int>( p ), a, false ))
					table->dpsRef( static_cast<Int>( p ), a, false ) = dps;
				if (air && dps > table->dpsRef( static_cast<Int>( p ), a, true ))
					table->dpsRef( static_cast<Int>( p ), a, true ) = dps;
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
AIPlanner::AIPlanner() :
	m_hasResult( FALSE ),
	m_running( FALSE ),
	m_takeOverFrame( 0 ),
	m_nextSnapshotFrame( 0 ),
	m_nextFocusEnemy( -1 ),
	m_plans( 0 ),
	m_jobMicros( 0 ),
	m_timedPlans( 0 ),
	m_snapshotSum( 0 ), m_snapshotMax( 0 ),
	m_jobSum( 0 ), m_jobMax( 0 ),
	m_waitSum( 0 ), m_waitMax( 0 ),
	m_objectSum( 0 )
{
}

AIPlanner::~AIPlanner()
{
	// The job writes into members; it must end before they are destroyed.
	m_task.wait();
}

void AIPlanner::init( Player *self )
{
	m_nextSnapshotFrame = TheGameLogic->getFrame() + PLAN_INTERVAL
		+ static_cast<UnsignedInt>( self->getPlayerIndex() ) % PLAN_INTERVAL;
	AIPlanner_buildCombatTable( &m_table, &m_profileOfTemplate, &m_templateOfProfile );
}

Bool AIPlanner::isSnapshotDue( void ) const
{
	const UnsignedInt now = TheGameLogic->getFrame();
	const Bool takenOver = m_running && now >= m_takeOverFrame;
	return (!m_running || takenOver) && now >= m_nextSnapshotFrame;
}

void AIPlanner::setInputs( const std::vector<AIPlanCandidate> &candidates, Int focusEnemy, const std::vector<UnsignedInt> &mission,
	const std::vector<UnsignedInt> &raid )
{
	m_nextRaid = raid;
	m_nextCandidates = candidates;
	m_nextFocusEnemy = focusEnemy;
	m_nextMission = mission;
}

Int AIPlanner::profileOf( const ThingTemplate *t ) const
{
	if (t == nullptr)
		return -1;
	const UnsignedShort id = t->getTemplateID();
	return (id < m_profileOfTemplate.size()) ? m_profileOfTemplate[id] : -1;
}

Real AIPlanner::counterScoreOf( Int profile ) const
{
	if (!m_hasResult)
		return -1.0f;
	for (const AIPlanChoice &c : m_last.counters)
	{
		if (c.profile == profile)
			return c.score;
	}
	return -1.0f;
}

// ------------------------------------------------------------------------------------------------
void AIPlanner::update( Player *self )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if (m_running && now >= m_takeOverFrame)
		takeOver( self, now );
	if (!m_running && now >= m_nextSnapshotFrame)
	{
		takeSnapshot( self, now );
		startJob();
		m_running = TRUE;
		m_takeOverFrame = now + PLAN_LATENCY;
		m_nextSnapshotFrame = now + PLAN_INTERVAL;
	}
}

// ------------------------------------------------------------------------------------------------
void AIPlanner::takeSnapshot( Player *self, UnsignedInt frame )
{
	PROF_ZONE( "AIPlanner::snapshot" );
	const auto start = std::chrono::steady_clock::now();

	const Int selfIndex = self->getPlayerIndex();
	m_input.frame = frame;
	m_input.self = selfIndex;
	m_input.money = static_cast<Int>( self->getMoney()->countMoney() );
	m_input.focusEnemy = m_nextFocusEnemy;
	m_input.candidates = m_nextCandidates;
	m_input.mission = m_nextMission;
	m_input.raid = m_nextRaid;
	Region3D extent;
	TheTerrainLogic->getExtent( &extent );
	m_input.worldWidth = static_cast<Int>( extent.hi.x );
	m_input.worldHeight = static_cast<Int>( extent.hi.y );
	m_input.objects.clear();

	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->isEffectivelyDead() || obj->isKindOf( KINDOF_PROJECTILE ) || obj->isKindOf( KINDOF_IGNORED_IN_GUI ))
			continue;
		Player *owner = obj->getControllingPlayer();
		const Int relation = relationOf( self, owner );
		const Bool structure = obj->isKindOf( KINDOF_STRUCTURE );
		const Bool supply = obj->isKindOf( KINDOF_SUPPLY_SOURCE );
		const Bool tech = obj->isKindOf( KINDOF_TECH_BUILDING ) && obj->isKindOf( KINDOF_CAPTURABLE );

		if (relation == AI_PLAN_ENEMY || relation == AI_PLAN_NEUTRAL)
		{
			// Neutral objects: only what matters for the economy (trees and civilians do not).
			if (relation == AI_PLAN_NEUTRAL && !supply && !tech)
				continue;
			if (obj->testStatus( OBJECT_STATUS_STEALTHED ) && !obj->testStatus( OBJECT_STATUS_DETECTED ))
				continue;
			const CellShroudStatus seen = cellStatus( selfIndex, obj );
			if (structure || supply)
			{
				if (seen == CellShroudStatus::CELLSHROUD_SHROUDED)
					continue;	// buildings stay known once seen
			}
			else if (seen != CellShroudStatus::CELLSHROUD_CLEAR)
				continue;		// units only while in sight
		}

		AIPlanObject o;
		o.id = static_cast<UnsignedInt>( obj->getID() );
		o.owner = owner ? owner->getPlayerIndex() : -1;
		o.relation = relation;
		o.flags = 0;
		if (structure)										o.flags |= AIPlanObject::STRUCTURE;
		if (obj->isKindOf( KINDOF_INFANTRY ))	o.flags |= AIPlanObject::INFANTRY;
		if (obj->isKindOf( KINDOF_VEHICLE ))	o.flags |= AIPlanObject::VEHICLE;
		if (obj->isKindOf( KINDOF_AIRCRAFT ))	o.flags |= AIPlanObject::AIRCRAFT;
		if (obj->isKindOf( KINDOF_DOZER ) || obj->isKindOf( KINDOF_HARVESTER ))	o.flags |= AIPlanObject::WORKER;
		if (supply)												o.flags |= AIPlanObject::SUPPLY;
		if (tech)													o.flags |= AIPlanObject::TECH;
		if (obj->isAbleToAttack())				o.flags |= AIPlanObject::CAN_ATTACK;
		if (structure && AIPlanner_isSuperweapon( obj->getTemplate() ))	o.flags |= AIPlanObject::SUPERWEAPON;
		if (structure && (obj->isKindOf( KINDOF_CASH_GENERATOR ) || obj->isKindOf( KINDOF_FS_SUPPLY_CENTER )))	o.flags |= AIPlanObject::ECONOMY;
		o.cost = obj->getTemplate()->friend_getBuildCost();
		const Coord3D *pos = obj->getPosition();
		o.x = static_cast<Int>( pos->x );
		o.y = static_cast<Int>( pos->y );
		o.profile = profileOf( obj->getTemplate() );
		o.health = 1000;
		const BodyModuleInterface *body = obj->getBodyModule();
		if (body && body->getMaxHealth() > 0.0f)
			o.health = static_cast<Int>( body->getHealth() * 1000.0f / body->getMaxHealth() );
		m_input.objects.push_back( o );
	}

	const long long micros = microsSince( start );
	m_snapshotSum += micros;
	if (micros > m_snapshotMax)
		m_snapshotMax = micros;
	m_objectSum += static_cast<long long>( m_input.objects.size() );
}

void AIPlanner::startJob( void )
{
	m_task.start( [this]()
	{
		const auto start = std::chrono::steady_clock::now();
		m_jobResult = AIPlanner_compute( m_input, m_table );
		m_jobMicros = microsSince( start );
	} );
}

// ------------------------------------------------------------------------------------------------
void AIPlanner::takeOver( Player *self, UnsignedInt frame )
{
	PROF_ZONE( "AIPlanner::takeOver" );
	const auto start = std::chrono::steady_clock::now();
	m_task.wait();
	const long long waited = microsSince( start );

	m_last = m_jobResult;
	m_hasResult = TRUE;
	m_running = FALSE;
	++m_plans;

	m_waitSum += waited;
	if (waited > m_waitMax)
		m_waitMax = waited;
	m_jobSum += m_jobMicros;
	if (m_jobMicros > m_jobMax)
		m_jobMax = m_jobMicros;
	++m_timedPlans;

	if (TheGlobalData->m_aiStatsInterval > 0)
	{
		char line[320];
		WWFormat::Into( line, "KIPLAN {:d} spieler {:d} eingabe {:d} objekte {:d} pruefsumme {:08X} eigene {:d} wert {:d} gebaeude {:d} verbuendet_wert {:d} feind_gesehen {:d} feind_wert {:d} feind_gebaeude {:d} lager {:d}",
			frame, self->getPlayerIndex(), m_last.frame, static_cast<Int>( m_input.objects.size() ), m_last.checksum,
			m_last.ownUnits, m_last.ownValue, m_last.ownStructures, m_last.allyValue, m_last.enemyUnitsSeen,
			m_last.enemyValueSeen, m_last.enemyStructuresKnown, m_last.suppliesKnown );
		GameEngine_RunnerLog( line );
		if (m_plans % FORECAST_REPORT_PLANS == 0)
		{
			char forecast[320];
			WWFormat::Into( forecast, "KIPROGNOSE {:d} spieler {:d} gegner {:d} feld_sieger {:d} feld_rest {:.2f} feld_verhaeltnis {:.2f} basis_sieger {:d} basis_rest {:.2f} basis_verhaeltnis {:.2f} feind_leben {:d} bedrohung_max {:.0f} trupp {:d} ziel {:d} ziel_sieger {:d} ziel_verhaeltnis {:.2f} ort_sieger {:d} ort_verhaeltnis {:.2f}",
				frame, self->getPlayerIndex(), m_input.focusEnemy, m_last.fieldWinner, m_last.fieldSurvivors, m_last.fieldRatio,
				m_last.baseWinner, m_last.baseSurvivors, m_last.baseRatio, m_last.enemyHealthSeen, m_last.maxThreat,
				static_cast<Int>( m_input.mission.size() ), m_last.objectiveId, m_last.objectiveWinner, m_last.objectiveRatio,
				m_last.siteWinner, m_last.siteRatio );
			GameEngine_RunnerLog( forecast );
			AsciiString best;
			for (size_t i = 0; i < m_last.counters.size() && i < 3; ++i)
			{
				const Int profile = m_last.counters[i].profile;
				AsciiString entry;
				entry.format( " %s %.5f", (profile >= 0 && profile < static_cast<Int>( m_templateOfProfile.size() ))
					? m_templateOfProfile[static_cast<size_t>( profile )]->getName().str() : "?", m_last.counters[i].score );
				best.concat( entry );
			}
			char counters[512];
			WWFormat::Into( counters, "KIKONTER {:d} spieler {:d} kandidaten {:d} bewertet {:d}{}", frame, self->getPlayerIndex(),
				static_cast<Int>( m_input.candidates.size() ), static_cast<Int>( m_last.counters.size() ), best.str() );
			GameEngine_RunnerLog( counters );
		}
		if (m_timedPlans >= TIME_REPORT_PLANS)
			logTimes( self, frame );
	}
	if (m_timedPlans >= TIME_REPORT_PLANS)
	{
		m_timedPlans = 0;
		m_snapshotSum = m_snapshotMax = m_jobSum = m_jobMax = m_waitSum = m_waitMax = m_objectSum = 0;
	}
}

// Real-time figures, therefore in their own line: KIPLAN must be equal between runs, KIZEIT is not.
void AIPlanner::logTimes( Player *self, UnsignedInt frame )
{
	const long long n = m_timedPlans;
	char line[320];
	WWFormat::Into( line, "KIZEIT {:d} spieler {:d} plaene {:d} worker {:d} objekte {:d} schnappschuss_us {:d} schnappschuss_max {:d} rechnen_us {:d} rechnen_max {:d} warten_us {:d} warten_max {:d}",
		frame, self->getPlayerIndex(), static_cast<Int>( n ), ::Engine::JobSystem::get().workerCount(),
		static_cast<Int>( m_objectSum / n ), static_cast<Int>( m_snapshotSum / n ), static_cast<Int>( m_snapshotMax ),
		static_cast<Int>( m_jobSum / n ), static_cast<Int>( m_jobMax ), static_cast<Int>( m_waitSum / n ),
		static_cast<Int>( m_waitMax ) );
	GameEngine_RunnerLog( line );
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: combat forecast and counter production
	* 3: influence maps, attack target and site forecast
	* 4: raid target */
// ------------------------------------------------------------------------------------------------
void AIPlanner::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 4;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	m_task.wait();	// the job reads m_input, which loading overwrites

	xfer->xferBool( &m_running );
	xfer->xferUnsignedInt( &m_takeOverFrame );
	xfer->xferUnsignedInt( &m_nextSnapshotFrame );

	// The input of a running job, so the job can run again after loading.
	xfer->xferUnsignedInt( &m_input.frame );
	xfer->xferInt( &m_input.self );
	xfer->xferInt( &m_input.money );
	UnsignedInt count = static_cast<UnsignedInt>( m_input.objects.size() );
	xfer->xferUnsignedInt( &count );
	if (xfer->getXferMode() == XFER_LOAD)
		m_input.objects.assign( count, AIPlanObject() );
	for (UnsignedInt i = 0; i < count; ++i)
	{
		AIPlanObject &o = m_input.objects[i];
		xfer->xferUnsignedInt( &o.id );
		xfer->xferInt( &o.owner );
		xfer->xferInt( &o.relation );
		xfer->xferUnsignedInt( &o.flags );
		xfer->xferInt( &o.cost );
		xfer->xferInt( &o.x );
		xfer->xferInt( &o.y );
		xfer->xferInt( &o.health );
		if (version >= 2)
			xfer->xferInt( &o.profile );
		else
			o.profile = -1;
	}
	if (version >= 2)
	{
		xfer->xferInt( &m_input.focusEnemy );
		UnsignedInt candidates = static_cast<UnsignedInt>( m_input.candidates.size() );
		xfer->xferUnsignedInt( &candidates );
		if (xfer->getXferMode() == XFER_LOAD)
			m_input.candidates.assign( candidates, AIPlanCandidate() );
		for (UnsignedInt i = 0; i < candidates; ++i)
		{
			xfer->xferInt( &m_input.candidates[i].profile );
			xfer->xferInt( &m_input.candidates[i].cost );
		}
		xfer->xferInt( &m_plans );
	}
	if (version >= 3)
	{
		xfer->xferInt( &m_input.worldWidth );
		xfer->xferInt( &m_input.worldHeight );
		UnsignedInt mission = static_cast<UnsignedInt>( m_input.mission.size() );
		xfer->xferUnsignedInt( &mission );
		if (xfer->getXferMode() == XFER_LOAD)
			m_input.mission.assign( mission, 0 );
		for (UnsignedInt i = 0; i < mission; ++i)
			xfer->xferUnsignedInt( &m_input.mission[i] );
	}
	if (version >= 4)
	{
		UnsignedInt raid = static_cast<UnsignedInt>( m_input.raid.size() );
		xfer->xferUnsignedInt( &raid );
		if (xfer->getXferMode() == XFER_LOAD)
			m_input.raid.assign( raid, 0 );
		for (UnsignedInt i = 0; i < raid; ++i)
			xfer->xferUnsignedInt( &m_input.raid[i] );
	}

	xfer->xferBool( &m_hasResult );
	xfer->xferUnsignedInt( &m_last.frame );
	xfer->xferUnsignedInt( &m_last.checksum );
	xfer->xferInt( &m_last.ownUnits );
	xfer->xferInt( &m_last.ownValue );
	xfer->xferInt( &m_last.ownStructures );
	xfer->xferInt( &m_last.allyValue );
	xfer->xferInt( &m_last.enemyUnitsSeen );
	xfer->xferInt( &m_last.enemyValueSeen );
	xfer->xferInt( &m_last.enemyStructuresKnown );
	xfer->xferInt( &m_last.suppliesKnown );
	if (version >= 2)
	{
		xfer->xferInt( &m_last.fieldWinner );
		xfer->xferReal( &m_last.fieldSurvivors );
		xfer->xferReal( &m_last.fieldRatio );
		xfer->xferInt( &m_last.baseWinner );
		xfer->xferReal( &m_last.baseSurvivors );
		xfer->xferReal( &m_last.baseRatio );
		xfer->xferInt( &m_last.enemyHealthSeen );
		UnsignedInt counters = static_cast<UnsignedInt>( m_last.counters.size() );
		xfer->xferUnsignedInt( &counters );
		if (xfer->getXferMode() == XFER_LOAD)
			m_last.counters.assign( counters, AIPlanChoice() );
		for (UnsignedInt i = 0; i < counters; ++i)
		{
			xfer->xferInt( &m_last.counters[i].profile );
			xfer->xferReal( &m_last.counters[i].score );
		}
	}
	if (version >= 3)
	{
		xfer->xferUnsignedInt( &m_last.objectiveId );
		xfer->xferInt( &m_last.objectiveX );
		xfer->xferInt( &m_last.objectiveY );
		xfer->xferReal( &m_last.objectiveRatio );
		xfer->xferInt( &m_last.objectiveWinner );
		xfer->xferReal( &m_last.objectiveThreat );
		xfer->xferInt( &m_last.siteWinner );
		xfer->xferReal( &m_last.siteRatio );
		xfer->xferInt( &m_last.siteEnemyHealth );
		xfer->xferReal( &m_last.maxThreat );
	}
	if (version >= 4)
	{
		xfer->xferUnsignedInt( &m_last.raidTargetId );
		xfer->xferInt( &m_last.raidTargetX );
		xfer->xferInt( &m_last.raidTargetY );
		xfer->xferInt( &m_last.raidTargetWinner );
		xfer->xferReal( &m_last.raidTargetRatio );
		xfer->xferInt( &m_last.raidSiteWinner );
		xfer->xferReal( &m_last.raidSiteRatio );
		xfer->xferInt( &m_last.raidSiteEnemyHealth );
	}
}

void AIPlanner::loadPostProcess( void )
{
	if (m_running)
		startJob();
}
