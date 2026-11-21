/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// AIRoles.h: roles of army units and the unit mix the new AI aims for.
//
// A role is derived from the combat table (damage per second against the armors of infantry,
// vehicles, buildings and aircraft, reach, splash, health per cost). The wanted share per role
// follows the enemy mix; the next unit to build is the one that closes the largest gap per money.
//
// Only standard library types and the combat model, so the unit tests can include it without the
// engine. All functions are pure and deterministic.

#pragma once

#ifndef _AI_ROLES_H_
#define _AI_ROLES_H_

#include "GameLogic/AICombatModel.h"

#include <algorithm>
#include <vector>

namespace AIRoles
{

enum Role
{
	ROLE_FRONT					= 0x01,	///< takes hits: much health per cost, hurts ground targets
	ROLE_ANTI_VEHICLE		= 0x02,
	ROLE_ANTI_INFANTRY	= 0x04,
	ROLE_ANTI_AIR				= 0x08,	///< hurts aircraft much more than ground targets, or only aircraft
	ROLE_SIEGE					= 0x10,	///< reaches farther than the usual defence building and hurts buildings
	ROLE_SUPPORT				= 0x20	///< detector, healer, transport (set by the caller, kept out of the shares)
};

/// The five roles the shares are given for, in this order.
constexpr int SHARE_ROLES = 5;
constexpr unsigned ROLE_BIT[SHARE_ROLES] = { ROLE_FRONT, ROLE_ANTI_VEHICLE, ROLE_ANTI_INFANTRY, ROLE_ANTI_AIR, ROLE_SIEGE };

/// Reference values taken from the whole table once.
struct Context
{
	float defenceRange = 0.0f;			///< median reach of the armed buildings
	float healthPerCost = 0.0f;			///< median health per cost of the armed ground units
	// Average of the damage per second a unit of the table deals ... (indexed by AICombat kinds below)
};

/// Reach and toughness the roles are measured against.
inline Context makeContext( const AICombat::Table &table )
{
	std::vector<float> ranges, toughness;
	for (const AICombat::Profile &p : table.profiles)
	{
		if (p.cost <= 0.0f || p.maxHealth <= 0.0f)
			continue;
		if (p.structure)
		{
			if (p.range > 0.0f)
				ranges.push_back( p.range );
		}
		else if (!p.air && p.range > 0.0f)
			toughness.push_back( p.maxHealth / p.cost );
	}
	Context c;
	if (!ranges.empty())
	{
		std::sort( ranges.begin(), ranges.end() );
		c.defenceRange = ranges[ranges.size() / 2];
	}
	if (!toughness.empty())
	{
		std::sort( toughness.begin(), toughness.end() );
		c.healthPerCost = toughness[toughness.size() / 2];
	}
	return c;
}

/// Mean damage per second of the attacker against the profiles of one class of targets.
inline float meanDps( const AICombat::Table &table, int attacker, int targetKind, bool air, bool structure )
{
	float sum = 0.0f;
	int count = 0;
	for (size_t q = 0; q < table.profiles.size(); ++q)
	{
		const AICombat::Profile &t = table.profiles[q];
		if (t.air != air || t.structure != structure || (!air && !structure && t.kind != targetKind))
			continue;
		if (t.cost <= 0.0f)
			continue;
		sum += table.dpsAgainst( attacker, static_cast<int>( q ) );
		++count;
	}
	return count > 0 ? sum / static_cast<float>( count ) : 0.0f;
}

/// Roles of a profile (bit set, zero for a unit without a weapon). support: the caller knows it is a detector, healer or
/// transport.
inline unsigned classifyRole( const AICombat::Table &table, const Context &ctx, int profile, bool support )
{
	unsigned roles = support ? ROLE_SUPPORT : 0;
	if (profile < 0 || static_cast<size_t>( profile ) >= table.profiles.size())
		return roles;
	const AICombat::Profile &p = table.profiles[static_cast<size_t>( profile )];
	const float inf = meanDps( table, profile, AICombat::KIND_INFANTRY, false, false );
	const float veh = meanDps( table, profile, AICombat::KIND_VEHICLE, false, false );
	const float bld = meanDps( table, profile, 0, false, true );
	const float air = meanDps( table, profile, 0, true, false );
	const float ground = std::max( inf, veh );
	if (air > 0.0f && (ground <= 0.0f || air > 2.0f * ground))
		roles |= ROLE_ANTI_AIR;
	if (ground > 0.0f)
	{
		if (veh > 0.0f && veh >= 0.75f * inf)
			roles |= ROLE_ANTI_VEHICLE;
		if (inf > 0.0f && inf >= 0.75f * veh)
			roles |= ROLE_ANTI_INFANTRY;
		if (p.cost > 0.0f && ctx.healthPerCost > 0.0f && p.maxHealth / p.cost >= ctx.healthPerCost)
			roles |= ROLE_FRONT;
	}
	if (bld > 0.0f && ctx.defenceRange > 0.0f && p.range > ctx.defenceRange)
		roles |= ROLE_SIEGE;
	return roles;
}

/// What the enemy has, as shares of his field army by value, and the value of his armed buildings.
struct EnemyMix
{
	float infantryShare = 0.0f;
	float vehicleShare = 0.0f;
	float airShare = 0.0f;
	float defenceValue = 0.0f;
	float fieldValue = 0.0f;
	bool known = false;
};

/// Wanted share of the army value per role (ROLE_BIT order), sums to one. Base: front 45 %, anti-vehicle 20 %,
/// anti-infantry 15 %, anti-air 10 %, siege 10 %; shifted by the known enemy.
inline void targetShares( const EnemyMix &enemy, float out[SHARE_ROLES] )
{
	float front = 0.45f, veh = 0.20f, inf = 0.15f, air = 0.10f, siege = 0.10f;
	if (enemy.known)
	{
		air = std::min( 0.40f, std::max( air, enemy.airShare ) );
		inf = std::min( 0.40f, std::max( 0.05f, 0.05f + 0.55f * enemy.infantryShare ) );
		veh = std::min( 0.40f, std::max( 0.05f, 0.05f + 0.55f * enemy.vehicleShare ) );
		const float total = enemy.defenceValue + enemy.fieldValue;
		const float defenceShare = total > 0.0f ? enemy.defenceValue / total : 0.0f;
		siege = std::min( 0.30f, 0.05f + 0.35f * defenceShare );
	}
	front = std::max( 0.20f, 1.0f - veh - inf - air - siege );
	const float sum = front + veh + inf + air + siege;
	out[0] = front / sum;
	out[1] = veh / sum;
	out[2] = inf / sum;
	out[3] = air / sum;
	out[4] = siege / sum;
}

/// One unit the AI can build now.
struct Candidate
{
	unsigned roles = 0;		///< Role bits
	float cost = 0.0f;
	float counter = 0.0f;	///< planner counter score (fighting value per money), 0 = none
	float damping = 1.0f;	///< e.g. one third from the cap per template on
};

/// Index of the candidate to build next, -1 if none. have: army value per role (ROLE_BIT order, a unit counts for
/// every role it has). Score = gap of the best of its roles x counter x damping; without any counter score the gap
/// decides, the more expensive unit wins a tie. Candidates without a share role (support) are never chosen here.
inline int chooseNextUnit( const float have[SHARE_ROLES], const std::vector<Candidate> &candidates, const float shares[SHARE_ROLES] )
{
	float total = 0.0f;
	for (int r = 0; r < SHARE_ROLES; ++r)
		total += have[r];
	bool anyCounter = false;
	for (const Candidate &c : candidates)
		anyCounter = anyCounter || c.counter > 0.0f;
	int best = -1;
	float bestScore = 0.0f;
	for (size_t i = 0; i < candidates.size(); ++i)
	{
		const Candidate &c = candidates[i];
		float gap = -1.0f;
		bool any = false;
		for (int r = 0; r < SHARE_ROLES; ++r)
		{
			if ((c.roles & ROLE_BIT[r]) == 0)
				continue;
			any = true;
			gap = std::max( gap, shares[r] - (total > 0.0f ? have[r] / total : 0.0f) );
		}
		if (!any)
			continue;
		const float weight = anyCounter ? c.counter : 1.0f;
		if (weight <= 0.0f)
			continue;
		const float score = std::max( gap, 0.01f ) * weight * c.damping;
		if (best < 0 || score > bestScore || (score == bestScore && c.cost > candidates[static_cast<size_t>( best )].cost))
		{
			best = static_cast<int>( i );
			bestScore = score;
		}
	}
	return best;
}

}  // namespace AIRoles

#endif // _AI_ROLES_H_
