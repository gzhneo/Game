/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// AICombatModel.h: combat forecast and counter scoring for the new AI.
//
// The table holds, per unit or building template ("profile"), its maximum health, cost, armor
// and whether it flies, and its damage per second against every armor, separately for ground and
// air targets. It is built once from the game data (AIPlanner) and only read afterwards, also by
// planning jobs on worker threads.
//
// Forecast: Lanchester's square law on health. Side A deals dpsA damage per second spread over
// B's health, so alpha = dpsA / healthB is the share of B destroyed per second at full strength
// (beta likewise). Firepower falls with the remaining health, the side with the larger share wins
// with sqrt(1 - beta/alpha) of its health left. Range, splash, upgrades and veterancy are not
// modelled.
//
// Counter score of a candidate against an enemy mix: sqrt(offense * endurance) / cost, where
// offense is the share of enemy units one candidate destroys per second and endurance the
// seconds it survives the fire of an average enemy unit. Under the square law a force's strength
// grows with offense * endurance per unit squared, so this is the fighting value per money.
//
// Influence map: a coarse grid over the map (cells of some hundred world units) that sums a
// quantity per cell, for example the enemy's damage per second within his weapons' reach (threat)
// or the build cost of his objects (value). Paths and targets are judged by sampling it.
//
// Only standard library types, so the unit tests can include it without the engine. Results are
// deterministic for the same program: sums run in a fixed order, no threads inside.

#pragma once

#ifndef _AI_COMBAT_MODEL_H_
#define _AI_COMBAT_MODEL_H_

#include <algorithm>
#include <cmath>
#include <vector>

namespace AICombat
{

struct Profile
{
	float maxHealth = 0.0f;
	float cost = 0.0f;
	int armor = -1;			///< index into the armor list, -1 = takes damage unchanged
	bool air = false;		///< a flying target (only anti-air weapons hit it)
	bool structure = false;
	float range = 0.0f;		///< longest reach of a damaging weapon (world units)
	float splash = 0.0f;		///< largest damage radius of a damaging weapon (area damage)
};

class Table
{
public:
	int armorCount = 0;
	std::vector<Profile> profiles;
	/// [profile][armor + 1][0 ground target, 1 air target]; armor slot 0 is "no armor".
	std::vector<float> dps;

	int slots() const { return armorCount + 1; }

	void resize( int profileCount, int armors )
	{
		armorCount = armors;
		profiles.assign( static_cast<size_t>(profileCount), Profile() );
		dps.assign( static_cast<size_t>(profileCount) * static_cast<size_t>(slots()) * 2, 0.0f );
	}

	float &dpsRef( int attacker, int armor, bool air )
	{
		return dps[(static_cast<size_t>(attacker) * static_cast<size_t>(slots()) + static_cast<size_t>(armor + 1)) * 2 + (air ? 1 : 0)];
	}

	/// Damage per second of one attacker at full health against the target profile.
	float dpsAgainst( int attacker, int target ) const
	{
		if (attacker < 0 || target < 0)
			return 0.0f;
		const Profile &t = profiles[static_cast<size_t>(target)];
		return dps[(static_cast<size_t>(attacker) * static_cast<size_t>(slots()) + static_cast<size_t>(t.armor + 1)) * 2 + (t.air ? 1 : 0)];
	}
};

/// One entry of a force: a profile with its current health (sum over the units of that entry).
struct Group
{
	int profile = -1;
	float health = 0.0f;
};

struct Outcome
{
	int winner = -1;					///< 0 = A, 1 = B, -1 = neither (both or none destroyed)
	float survivorsA = 1.0f;		///< share of A's health left at the end
	float survivorsB = 1.0f;
	float seconds = -1.0f;			///< duration until the loser is destroyed, -1 = never
	float strengthRatio = 0.0f;	///< alpha / beta (> 1: A stronger); 0 if beta is 0 and alpha too
};

/// Square law with alpha, beta = share of the enemy's health destroyed per second at full strength.
inline Outcome lanchester( float alpha, float beta )
{
	Outcome o;
	if (alpha <= 0.0f && beta <= 0.0f)
		return o;
	if (beta <= 0.0f)
	{
		o.winner = 0;
		o.survivorsB = 0.0f;
		o.seconds = 1.0f / alpha;		// y' = -alpha with x fixed at 1
		o.strengthRatio = 1.0e6f;
		return o;
	}
	o.strengthRatio = alpha / beta;
	if (alpha <= 0.0f)
	{
		o.winner = 1;
		o.survivorsA = 0.0f;
		o.seconds = 1.0f / beta;
		return o;
	}
	const float k = std::sqrt( alpha * beta );
	if (alpha > beta)
	{
		o.winner = 0;
		o.survivorsA = std::sqrt( 1.0f - beta / alpha );
		o.survivorsB = 0.0f;
		o.seconds = std::atanh( std::sqrt( beta / alpha ) ) / k;
	}
	else if (beta > alpha)
	{
		o.winner = 1;
		o.survivorsB = std::sqrt( 1.0f - alpha / beta );
		o.survivorsA = 0.0f;
		o.seconds = std::atanh( std::sqrt( alpha / beta ) ) / k;
	}
	else
	{
		o.survivorsA = 0.0f;
		o.survivorsB = 0.0f;
	}
	return o;
}

inline float totalHealth( const std::vector<Group> &force )
{
	float h = 0.0f;
	for (const Group &g : force)
		h += g.health;
	return h;
}

/// Damage per second of `from` against `to`, each attacker's fire spread over the targets by their
/// share of the health; an attacker counts with its health relative to one full unit.
inline float forceDps( const Table &table, const std::vector<Group> &from, const std::vector<Group> &to )
{
	const float toHealth = totalHealth( to );
	if (toHealth <= 0.0f)
		return 0.0f;
	float sum = 0.0f;
	for (const Group &a : from)
	{
		if (a.profile < 0 || a.health <= 0.0f)
			continue;
		const float maxHealth = table.profiles[static_cast<size_t>(a.profile)].maxHealth;
		const float units = (maxHealth > 0.0f) ? a.health / maxHealth : 0.0f;
		float perUnit = 0.0f;
		for (const Group &b : to)
		{
			if (b.profile < 0 || b.health <= 0.0f)
				continue;
			perUnit += (b.health / toHealth) * table.dpsAgainst( a.profile, b.profile );
		}
		sum += units * perUnit;
	}
	return sum;
}

/// Forecast of A against B.
inline Outcome predict( const Table &table, const std::vector<Group> &a, const std::vector<Group> &b )
{
	const float healthA = totalHealth( a );
	const float healthB = totalHealth( b );
	if (healthA <= 0.0f || healthB <= 0.0f)
	{
		Outcome o;
		o.winner = (healthA > 0.0f) ? 0 : ((healthB > 0.0f) ? 1 : -1);
		o.survivorsA = (healthA > 0.0f) ? 1.0f : 0.0f;
		o.survivorsB = (healthB > 0.0f) ? 1.0f : 0.0f;
		o.seconds = 0.0f;
		return o;
	}
	const float alpha = forceDps( table, a, b ) / healthB;
	const float beta = forceDps( table, b, a ) / healthA;
	return lanchester( alpha, beta );
}

/// Fighting value per money of one candidate profile against the enemy mix (0 if it cannot hurt it).
inline float counterScore( const Table &table, int candidate, const std::vector<Group> &enemy )
{
	const float MAX_ENDURANCE = 600.0f;		// seconds; for enemies that cannot hit the candidate at all
	if (candidate < 0)
		return 0.0f;
	const Profile &c = table.profiles[static_cast<size_t>(candidate)];
	const float enemyHealth = totalHealth( enemy );
	if (c.cost <= 0.0f || c.maxHealth <= 0.0f || enemyHealth <= 0.0f)
		return 0.0f;
	float offense = 0.0f;		// enemy units destroyed per second by one candidate
	float incoming = 0.0f;	// damage per second of all enemy units against the candidate
	float enemyUnits = 0.0f;
	for (const Group &e : enemy)
	{
		if (e.profile < 0 || e.health <= 0.0f)
			continue;
		const float eMax = table.profiles[static_cast<size_t>(e.profile)].maxHealth;
		if (eMax <= 0.0f)
			continue;
		const float units = e.health / eMax;
		offense += (e.health / enemyHealth) * table.dpsAgainst( candidate, e.profile ) / eMax;
		incoming += units * table.dpsAgainst( e.profile, candidate );
		enemyUnits += units;
	}
	if (offense <= 0.0f || enemyUnits <= 0.0f)
		return 0.0f;
	const float perEnemy = incoming / enemyUnits;
	float endurance = (perEnemy > 0.0f) ? c.maxHealth / perEnemy : MAX_ENDURANCE;
	if (endurance > MAX_ENDURANCE)
		endurance = MAX_ENDURANCE;
	return std::sqrt( offense * endurance ) / c.cost;
}

/// Highest damage per second of a profile against any ground target (for threat maps).
inline float groundThreat( const Table &table, int profile )
{
	if (profile < 0)
		return 0.0f;
	float best = 0.0f;
	const size_t base = static_cast<size_t>(profile) * static_cast<size_t>(table.slots()) * 2;
	for (int a = 0; a < table.slots(); ++a)
	{
		const float d = table.dps[base + static_cast<size_t>(a) * 2];
		if (d > best)
			best = d;
	}
	return best;
}

class InfluenceMap
{
public:
	float cell = 1.0f;
	int width = 0;
	int height = 0;
	std::vector<float> values;

	void reset( float worldWidth, float worldHeight, float cellSize )
	{
		cell = (cellSize > 1.0f) ? cellSize : 1.0f;
		width = static_cast<int>( worldWidth / cell ) + 1;
		height = static_cast<int>( worldHeight / cell ) + 1;
		if (width < 1)
			width = 1;
		if (height < 1)
			height = 1;
		values.assign( static_cast<size_t>(width) * static_cast<size_t>(height), 0.0f );
	}

	int cellX( float x ) const
	{
		int c = static_cast<int>( std::floor( x / cell ) );
		return (c < 0) ? 0 : ((c >= width) ? width - 1 : c);
	}

	int cellY( float y ) const
	{
		int c = static_cast<int>( std::floor( y / cell ) );
		return (c < 0) ? 0 : ((c >= height) ? height - 1 : c);
	}

	/// Adds amount to every cell whose centre lies within radius of (x, y), at least to the cell at (x, y).
	void add( float x, float y, float radius, float amount )
	{
		const int cx = cellX( x ), cy = cellY( y );
		values[static_cast<size_t>(cy) * static_cast<size_t>(width) + static_cast<size_t>(cx)] += amount;
		if (radius <= 0.0f)
			return;
		const int reach = static_cast<int>( radius / cell ) + 1;
		const float r2 = radius * radius;
		for (int j = cy - reach; j <= cy + reach; ++j)
		{
			if (j < 0 || j >= height)
				continue;
			for (int i = cx - reach; i <= cx + reach; ++i)
			{
				if (i < 0 || i >= width || (i == cx && j == cy))
					continue;
				const float dx = (static_cast<float>(i) + 0.5f) * cell - x;
				const float dy = (static_cast<float>(j) + 0.5f) * cell - y;
				if (dx * dx + dy * dy <= r2)
					values[static_cast<size_t>(j) * static_cast<size_t>(width) + static_cast<size_t>(i)] += amount;
			}
		}
	}

	float at( float x, float y ) const
	{
		if (values.empty())
			return 0.0f;
		return values[static_cast<size_t>(cellY( y )) * static_cast<size_t>(width) + static_cast<size_t>(cellX( x ))];
	}

	/// Sum of the cells along the straight line from (x0, y0) to (x1, y1), one sample per cell
	/// length, leaving out the last `skipEnd` world units (the fight at the target itself).
	float alongLine( float x0, float y0, float x1, float y1, float skipEnd ) const
	{
		const float dx = x1 - x0, dy = y1 - y0;
		const float length = std::sqrt( dx * dx + dy * dy );
		const float usable = length - skipEnd;
		if (usable <= 0.0f || values.empty())
			return 0.0f;
		const int steps = static_cast<int>( usable / cell ) + 1;
		float sum = 0.0f;
		for (int k = 0; k < steps; ++k)
		{
			const float t = (static_cast<float>(k) * cell) / length;
			sum += at( x0 + dx * t, y0 + dy * t );
		}
		return sum;
	}
};

/// An object as the attack planning sees it.
struct Unit
{
	unsigned id = 0;
	int profile = -1;
	float health = 0.0f;		///< current health (same units as Profile::maxHealth)
	float x = 0.0f, y = 0.0f;
	float cost = 0.0f;
	bool own = false;				///< belongs to the planning player
	bool enemy = false;
	bool focus = false;			///< belongs to the enemy currently attacked
	bool structure = false;
	bool worker = false;
	bool superweapon = false;	///< a superweapon building: counts SUPERWEAPON_TARGET_FACTOR times as a target
	bool economic = false;		///< harvester, dozer or supply centre: a raid target
};

/// A superweapon building of the enemy is a priority target (with limited superweapons it is his only one).
constexpr float SUPERWEAPON_TARGET_FACTOR = 3.0f;

/// A point for a superweapon strike: enemies count positive, own and allied objects negative.
struct StrikePoint
{
	unsigned id = 0;
	float x = 0.0f, y = 0.0f;
	float value = 0.0f;
};

struct StrikeChoice
{
	bool found = false;
	unsigned id = 0;		///< the enemy object at the centre
	float x = 0.0f, y = 0.0f;
	float value = 0.0f;	///< sum of the values within the radius
};

/// Best centre for a strike of the given radius: tried at every point with positive value, the sum
/// of all values within the radius counts; ties go to the lower id.
inline StrikeChoice bestStrike( const std::vector<StrikePoint> &points, float radius )
{
	StrikeChoice best;
	const float r2 = radius * radius;
	for (const StrikePoint &c : points)
	{
		if (c.value <= 0.0f)
			continue;
		float sum = 0.0f;
		for (const StrikePoint &p : points)
		{
			const float dx = p.x - c.x, dy = p.y - c.y;
			if (dx * dx + dy * dy <= r2)
				sum += p.value;
		}
		if (sum <= 0.0f)
			continue;
		if (!best.found || sum > best.value || (sum == best.value && c.id < best.id))
		{
			best.found = true;
			best.id = c.id;
			best.x = c.x;
			best.y = c.y;
			best.value = sum;
		}
	}
	return best;
}

/// When a ready superweapon fires: a target worth highValue at once; otherwise together with the own
/// attack (the strike then softens the target and its defence), but not later than maxHoldFrames after
/// it was first ready.
struct StrikeTiming
{
	float highValue = 3000.0f;
	unsigned maxHoldFrames = 90 * 30;
};

inline bool fireStrikeNow( float value, bool ownAttackRunning, unsigned heldFrames, const StrikeTiming &t )
{
	return value >= t.highValue || ownAttackRunning || heldFrames >= t.maxHoldFrames;
}

struct AttackParams
{
	float cell = 250.0f;						///< influence map cell
	float siteRadius = 700.0f;				///< armed enemies this close to the force fight it
	float defenceRadius = 600.0f;			///< armed enemies this close to a target defend it
	float distanceScale = 3000.0f;		///< a target this much farther away counts half
	bool economicTargets = false;			///< raid: targets are the enemy's workers and supply centres, not his buildings
};

struct AttackPlan
{
	unsigned objectiveId = 0;				///< 0 = none
	float objectiveX = 0.0f, objectiveY = 0.0f;
	int objectiveWinner = -1;
	float objectiveRatio = 0.0f;
	float objectiveThreat = 0.0f;			///< threat on the way
	int siteWinner = -1;
	float siteRatio = 0.0f;
	float siteEnemyHealth = 0.0f;
	float maxThreat = 0.0f;
};

/// Threat map of the armed enemies, target choice for the force (the own units whose ids are in
/// `missionSorted`, ascending) and the forecast where it stands. Target score: value in the
/// target's cell times the chance against its defenders, divided by the threat on the straight
/// way (relative to the force's own damage) and by the distance.
inline AttackPlan planAttack( const Table &table, float worldWidth, float worldHeight, const std::vector<Unit> &units,
	const std::vector<unsigned> &missionSorted, const AttackParams &params = AttackParams() )
{
	AttackPlan plan;
	InfluenceMap threat, value;
	threat.reset( worldWidth, worldHeight, params.cell );
	value.reset( worldWidth, worldHeight, params.cell );
	std::vector<size_t> armed, targets;
	for (size_t i = 0; i < units.size(); ++i)
	{
		const Unit &u = units[i];
		if (!u.enemy)
			continue;
		const float maxHealth = (u.profile >= 0) ? table.profiles[static_cast<size_t>(u.profile)].maxHealth : 0.0f;
		const float dps = (maxHealth > 0.0f) ? groundThreat( table, u.profile ) * u.health / maxHealth : 0.0f;
		if (dps > 0.0f && !u.worker)
		{
			armed.push_back( i );
			threat.add( u.x, u.y, table.profiles[static_cast<size_t>(u.profile)].range + 0.5f * params.cell, dps );
		}
		if (u.focus)
		{
			value.add( u.x, u.y, 0.0f, u.superweapon ? u.cost * SUPERWEAPON_TARGET_FACTOR : u.cost );
			if (params.economicTargets ? u.economic : u.structure)
				targets.push_back( i );
		}
	}
	for (float v : threat.values)
		if (v > plan.maxThreat)
			plan.maxThreat = v;
	if (missionSorted.empty())
		return plan;

	std::vector<Group> force;
	float cx = 0.0f, cy = 0.0f;
	int members = 0;
	for (const Unit &u : units)
	{
		if (!u.own || !std::binary_search( missionSorted.begin(), missionSorted.end(), u.id ))
			continue;
		bool merged = false;
		for (Group &g : force)
		{
			if (g.profile == u.profile)
			{
				g.health += u.health;
				merged = true;
				break;
			}
		}
		if (!merged && u.profile >= 0)
		{
			Group g;
			g.profile = u.profile;
			g.health = u.health;
			force.push_back( g );
		}
		cx += u.x;
		cy += u.y;
		++members;
	}
	if (members == 0 || totalHealth( force ) <= 0.0f)
		return plan;
	cx /= static_cast<float>(members);
	cy /= static_cast<float>(members);
	float forceDamage = 0.0f;
	for (const Group &g : force)
	{
		const float maxHealth = table.profiles[static_cast<size_t>(g.profile)].maxHealth;
		if (maxHealth > 0.0f)
			forceDamage += groundThreat( table, g.profile ) * g.health / maxHealth;
	}

	auto defenders = [&]( float x, float y, float radius )
	{
		std::vector<Group> d;
		const float r2 = radius * radius;
		for (size_t k : armed)
		{
			const Unit &u = units[k];
			const float dx = u.x - x, dy = u.y - y;
			if (dx * dx + dy * dy > r2)
				continue;
			bool merged = false;
			for (Group &g : d)
			{
				if (g.profile == u.profile)
				{
					g.health += u.health;
					merged = true;
					break;
				}
			}
			if (!merged)
			{
				Group g;
				g.profile = u.profile;
				g.health = u.health;
				d.push_back( g );
			}
		}
		return d;
	};

	const std::vector<Group> site = defenders( cx, cy, params.siteRadius );
	plan.siteEnemyHealth = totalHealth( site );
	if (!site.empty())
	{
		const Outcome o = predict( table, force, site );
		plan.siteWinner = o.winner;
		plan.siteRatio = o.strengthRatio;
	}

	float bestScore = -1.0f;
	for (size_t k : targets)
	{
		const Unit &t = units[k];
		const std::vector<Group> d = defenders( t.x, t.y, params.defenceRadius );
		Outcome o;
		o.winner = 0;
		o.survivorsA = 1.0f;
		o.strengthRatio = 1.0e6f;
		if (!d.empty())
			o = predict( table, force, d );
		const float chance = (o.winner == 0) ? 0.5f + o.survivorsA : 0.1f * ((o.strengthRatio < 1.0f) ? o.strengthRatio : 1.0f);
		const float wayThreat = threat.alongLine( cx, cy, t.x, t.y, params.defenceRadius );
		const float dx = t.x - cx, dy = t.y - cy;
		const float distance = std::sqrt( dx * dx + dy * dy );
		const float score = (value.at( t.x, t.y ) + 1.0f) * chance
			/ (1.0f + wayThreat / ((forceDamage > 1.0f) ? forceDamage : 1.0f)) / (1.0f + distance / params.distanceScale);
		if (score > bestScore || (score == bestScore && t.id < plan.objectiveId))
		{
			bestScore = score;
			plan.objectiveId = t.id;
			plan.objectiveX = t.x;
			plan.objectiveY = t.y;
			plan.objectiveWinner = o.winner;
			plan.objectiveRatio = o.strengthRatio;
			plan.objectiveThreat = wayThreat;
		}
	}
	return plan;
}

// ------------------------------------------------------------------------------------------------
// Fight control (micro) for the units in a fight.

/// A unit in a fight: own or enemy.
struct Fighter
{
	unsigned id = 0;
	int profile = -1;
	float health = 0.0f;
	float x = 0.0f, y = 0.0f;
	float speed = 0.0f;		///< top speed now (world units per frame), 0 = unknown or immobile
};

struct FocusAssignment
{
	unsigned shooter = 0;
	unsigned target = 0;
};

inline float distance2( const Fighter &a, const Fighter &b )
{
	const float dx = a.x - b.x, dy = a.y - b.y;
	return dx * dx + dy * dy;
}

/// Focus fire. Targets in order of the damage they deal to the own units per point of their
/// remaining health (kill first what hurts most and dies fastest); each target takes shooters
/// that reach it (range plus `approach`) and can hurt it until their fire kills it within
/// `killSeconds` -- the rest go on to the next target instead of overkilling. Shooters in id order.
inline std::vector<FocusAssignment> assignFocusFire( const Table &table, const std::vector<Fighter> &own,
	const std::vector<Fighter> &enemies, float approach = 100.0f, float killSeconds = 4.0f )
{
	std::vector<FocusAssignment> out;
	if (own.empty() || enemies.empty())
		return out;
	float ownHealth = 0.0f;
	for (const Fighter &o : own)
		ownHealth += o.health;

	struct Ranked { size_t index; float score; };
	std::vector<Ranked> ranked;
	for (size_t i = 0; i < enemies.size(); ++i)
	{
		const Fighter &e = enemies[i];
		if (e.profile < 0 || e.health <= 0.0f)
			continue;
		float threat = 0.0f;
		for (const Fighter &o : own)
			if (ownHealth > 0.0f)
				threat += (o.health / ownHealth) * table.dpsAgainst( e.profile, o.profile );
		Ranked r;
		r.index = i;
		r.score = threat / e.health + 1.0e-6f / e.health;	// unarmed: weakest first, after all armed
		ranked.push_back( r );
	}
	std::sort( ranked.begin(), ranked.end(), [&]( const Ranked &a, const Ranked &b )
	{
		if (a.score != b.score)
			return a.score > b.score;
		return enemies[a.index].id < enemies[b.index].id;
	} );

	std::vector<size_t> order( own.size() );
	for (size_t i = 0; i < own.size(); ++i)
		order[i] = i;
	std::sort( order.begin(), order.end(), [&]( size_t a, size_t b ) { return own[a].id < own[b].id; } );
	std::vector<bool> busy( own.size(), false );

	for (const Ranked &r : ranked)
	{
		const Fighter &t = enemies[r.index];
		const float needed = t.health / ((killSeconds > 0.1f) ? killSeconds : 0.1f);
		float assigned = 0.0f;
		for (size_t k : order)
		{
			if (busy[k] || assigned >= needed)
				continue;
			const Fighter &s = own[k];
			if (s.profile < 0)
				continue;
			const Profile &p = table.profiles[static_cast<size_t>(s.profile)];
			const float dps = table.dpsAgainst( s.profile, t.profile );
			const float reach = p.range + approach;
			if (dps <= 0.0f || distance2( s, t ) > reach * reach)
				continue;
			busy[k] = true;
			FocusAssignment a;
			a.shooter = s.id;
			a.target = t.id;
			out.push_back( a );
			assigned += dps * ((p.maxHealth > 0.0f) ? s.health / p.maxHealth : 1.0f);
		}
	}
	return out;
}

/// Keeping distance: if an enemy that can hurt `me` but reaches less than `rangeShare` of my range
/// stands within its own reach plus `margin`, step back so that it is at 90 % of my range. An enemy
/// clearly faster than `me` (both speeds known) would catch up anyway; then `me` keeps firing instead.
/// Returns false if nothing is to be done.
inline bool kiteStep( const Table &table, const Fighter &me, const std::vector<Fighter> &enemies, float *outX, float *outY,
	float rangeShare = 0.7f, float margin = 60.0f )
{
	if (me.profile < 0)
		return false;
	const Profile &mine = table.profiles[static_cast<size_t>(me.profile)];
	if (mine.structure || mine.range <= 0.0f)
		return false;
	const Fighter *closest = nullptr;
	float closestD2 = 0.0f;
	for (const Fighter &e : enemies)
	{
		if (e.profile < 0 || table.dpsAgainst( e.profile, me.profile ) <= 0.0f)
			continue;
		const float reach = table.profiles[static_cast<size_t>(e.profile)].range;
		if (reach <= 0.0f || reach >= rangeShare * mine.range)
			continue;
		if (me.speed > 0.0f && e.speed > 1.1f * me.speed)
			continue;
		const float d2 = distance2( me, e );
		if (d2 > (reach + margin) * (reach + margin))
			continue;
		if (closest == nullptr || d2 < closestD2 || (d2 == closestD2 && e.id < closest->id))
		{
			closest = &e;
			closestD2 = d2;
		}
	}
	if (closest == nullptr)
		return false;
	float dx = me.x - closest->x, dy = me.y - closest->y;
	float d = std::sqrt( dx * dx + dy * dy );
	if (d < 1.0f)
	{
		dx = 1.0f;
		dy = 0.0f;
		d = 1.0f;
	}
	const float keep = 0.9f * mine.range;
	*outX = closest->x + dx / d * keep;
	*outY = closest->y + dy / d * keep;
	return true;
}

/// TRUE if the group stands so close that area damage hits several at once: the mean distance to
/// the nearest neighbour is below `spacing` * 0.7.
inline bool isClumped( const std::vector<Fighter> &group, float spacing )
{
	if (group.size() < 3)
		return false;
	float sum = 0.0f;
	for (size_t i = 0; i < group.size(); ++i)
	{
		float best = -1.0f;
		for (size_t j = 0; j < group.size(); ++j)
		{
			if (i == j)
				continue;
			const float d2 = distance2( group[i], group[j] );
			if (best < 0.0f || d2 < best)
				best = d2;
		}
		sum += std::sqrt( best );
	}
	return sum / static_cast<float>(group.size()) < 0.7f * spacing;
}

/// Spread positions: rings around the group's centre with `spacing` between rings and slots
/// (ring k has 6k slots); units nearest to the centre take the inner slots, each its slot nearest
/// to its current direction. Returned in the order of `group`.
inline std::vector<Fighter> spreadPositions( const std::vector<Fighter> &group, float spacing )
{
	std::vector<Fighter> out = group;
	if (group.empty())
		return out;
	float cx = 0.0f, cy = 0.0f;
	for (const Fighter &f : group)
	{
		cx += f.x;
		cy += f.y;
	}
	cx /= static_cast<float>(group.size());
	cy /= static_cast<float>(group.size());

	std::vector<size_t> order( group.size() );
	for (size_t i = 0; i < group.size(); ++i)
		order[i] = i;
	std::sort( order.begin(), order.end(), [&]( size_t a, size_t b )
	{
		const float da = (group[a].x - cx) * (group[a].x - cx) + (group[a].y - cy) * (group[a].y - cy);
		const float db = (group[b].x - cx) * (group[b].x - cx) + (group[b].y - cy) * (group[b].y - cy);
		if (da != db)
			return da < db;
		return group[a].id < group[b].id;
	} );

	const float twoPi = 6.28318530718f;
	size_t next = 0;
	for (int ring = 0; next < order.size(); ++ring)
	{
		const int slots = (ring == 0) ? 1 : 6 * ring;
		std::vector<bool> taken( static_cast<size_t>(slots), false );
		for (int n = 0; n < slots && next < order.size(); ++n, ++next)
		{
			const size_t i = order[next];
			int slot = 0;
			if (ring > 0)
			{
				const float angle = std::atan2( group[i].y - cy, group[i].x - cx );
				float want = angle / twoPi * static_cast<float>(slots);
				int base = static_cast<int>( std::floor( want + 0.5f ) );
				slot = -1;
				for (int k = 0; k < slots && slot < 0; ++k)
				{
					for (int sign = 0; sign < 2 && slot < 0; ++sign)
					{
						int c = ((base + (sign ? -k : k)) % slots + slots) % slots;
						if (!taken[static_cast<size_t>(c)])
							slot = c;
					}
				}
			}
			taken[static_cast<size_t>(slot)] = true;
			const float a = twoPi * static_cast<float>(slot) / static_cast<float>(slots);
			out[i].x = cx + std::cos( a ) * spacing * static_cast<float>(ring);
			out[i].y = cy + std::sin( a ) * spacing * static_cast<float>(ring);
		}
	}
	return out;
}

} // namespace AICombat

#endif // _AI_COMBAT_MODEL_H_
