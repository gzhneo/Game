/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// AISquad.h: composition of an attack squad for its target.
//
// From the available units the squad is built in the order of their effect against the target's defenders until the
// forecast at the target reaches the wanted ratio, with mandatory shares: at least half of the value in front units,
// air cover when the enemy has shown aircraft, siege units when buildings cover the target. A share is only demanded
// as far as the available units can fill it: a side without siege units still attacks, the missing role is reported (a
// want for the production). A squad of only anti-air or only siege units is invalid. Badly damaged units stay home, slow
// units go to a second wave.
//
// Only standard library types, so the unit tests can include it without the engine. The function is pure and
// deterministic; the forecast comes from the caller.

#pragma once

#ifndef _AI_SQUAD_H_
#define _AI_SQUAD_H_

#include "GameLogic/AIRoles.h"

#include <algorithm>
#include <functional>
#include <vector>

namespace AISquad
{

struct Unit
{
	unsigned roles = 0;		///< AIRoles bits
	float value = 0.0f;
	float health = 1.0f;	///< share of the maximum health
	float speed = 0.0f;		///< top speed, 0 = unknown
	float effect = 0.0f;	///< fighting value against the defenders of the target, higher first
};

struct Request
{
	bool wantAirCover = false;
	bool targetCovered = false;		///< buildings with weapons protect the target
	int maxUnits = 1000;
	float valueLimit = 1.0e9f;
	float goalRatio = 1.3f;				///< stop adding at this forecast ratio (own strength / defenders)
	float minRatio = 0.8f;				///< below this at the end the squad is not sent
	float minHealth = 0.5f;
	float slowFactor = 0.5f;			///< slower than this share of the median speed: second wave
	float frontShare = 0.5f;
	float airShareMin = 0.10f;
	float airShareMax = 0.20f;
	float siegeShareMin = 0.10f;
};

struct Result
{
	bool ok = false;
	std::vector<int> picked;			///< indices into the available units, in the order they were added
	std::vector<int> secondWave;
	float value = 0.0f;
	float ratio = 0.0f;
	unsigned missing = 0;					///< AIRoles bits the squad lacks (a want for the production)
};

inline bool hasRole( const Unit &u, unsigned role ) { return (u.roles & role) != 0; }

/// ratioOf(picked) is the forecast strength ratio of that selection at the target (large without defenders).
inline Result composeSquad( const std::vector<Unit> &units, const Request &req,
	const std::function<float( const std::vector<int> & )> &ratioOf )
{
	Result r;
	std::vector<int> eligible;
	for (size_t i = 0; i < units.size(); ++i)
	{
		if (units[i].health >= req.minHealth && units[i].value > 0.0f)
			eligible.push_back( static_cast<int>( i ) );
	}
	// Slow units: below slowFactor of the median speed of the eligible ones.
	std::vector<float> speeds;
	for (int i : eligible)
	{
		if (units[static_cast<size_t>( i )].speed > 0.0f)
			speeds.push_back( units[static_cast<size_t>( i )].speed );
	}
	float median = 0.0f;
	if (!speeds.empty())
	{
		std::sort( speeds.begin(), speeds.end() );
		median = speeds[speeds.size() / 2];
	}
	std::vector<int> fast;
	for (int i : eligible)
	{
		const float s = units[static_cast<size_t>( i )].speed;
		if (median > 0.0f && s > 0.0f && s < req.slowFactor * median)
			r.secondWave.push_back( i );
		else
			fast.push_back( i );
	}
	std::stable_sort( fast.begin(), fast.end(), [&]( int a, int b ) { return units[static_cast<size_t>( a )].effect > units[static_cast<size_t>( b )].effect; } );

	float roleValue[AIRoles::SHARE_ROLES] = { 0, 0, 0, 0, 0 };
	float total = 0.0f;
	bool core = false;		// a unit that is neither pure anti-air nor pure siege
	std::vector<bool> used( units.size(), false );
	auto share = [&]( int role ) { return total > 0.0f ? roleValue[role] / total : 0.0f; };
	// roles that are still short, with or without an unused unit that could fill them
	auto shortRoles = [&]( bool attainableOnly ) -> unsigned {
		unsigned roles = 0;
		const unsigned fighting = AIRoles::ROLE_FRONT | AIRoles::ROLE_ANTI_VEHICLE | AIRoles::ROLE_ANTI_INFANTRY;
		auto unused = [&]( unsigned role ) {
			for (int i : fast)
				if (!used[static_cast<size_t>( i )] && hasRole( units[static_cast<size_t>( i )], role ) && r.value + units[static_cast<size_t>( i )].value <= req.valueLimit)
					return true;
			return false;
		};
		if (!core && (!attainableOnly || unused( fighting )))
			roles |= AIRoles::ROLE_FRONT;
		if (share( 0 ) < req.frontShare && (!attainableOnly || unused( AIRoles::ROLE_FRONT )))
			roles |= AIRoles::ROLE_FRONT;
		if (req.wantAirCover && share( 3 ) < req.airShareMin && (!attainableOnly || unused( AIRoles::ROLE_ANTI_AIR )))
			roles |= AIRoles::ROLE_ANTI_AIR;
		if (req.targetCovered && share( 4 ) < req.siegeShareMin && (!attainableOnly || unused( AIRoles::ROLE_SIEGE )))
			roles |= AIRoles::ROLE_SIEGE;
		return roles;
	};
	auto add = [&]( int i ) {
		const Unit &u = units[static_cast<size_t>( i )];
		used[static_cast<size_t>( i )] = true;
		r.picked.push_back( i );
		total += u.value;
		for (int k = 0; k < AIRoles::SHARE_ROLES; ++k)
		{
			if (u.roles & AIRoles::ROLE_BIT[k])
				roleValue[k] += u.value;
		}
		const unsigned fighting = AIRoles::ROLE_FRONT | AIRoles::ROLE_ANTI_VEHICLE | AIRoles::ROLE_ANTI_INFANTRY;
		if (u.roles & fighting)
			core = true;
		r.value += u.value;
	};
	// next candidate: the share that is short comes first, otherwise the best effect (anti-air only up to its maximum)
	auto next = [&]() -> int {
		auto first = [&]( unsigned role ) -> int {
			for (int i : fast)
			{
				if (!used[static_cast<size_t>( i )] && hasRole( units[static_cast<size_t>( i )], role ) && r.value + units[static_cast<size_t>( i )].value <= req.valueLimit)
					return i;
			}
			return -1;
		};
		if (share( 0 ) < req.frontShare || !core)
		{
			const int i = first( AIRoles::ROLE_FRONT );
			if (i >= 0)
				return i;
		}
		if (req.wantAirCover && share( 3 ) < req.airShareMin)
		{
			const int i = first( AIRoles::ROLE_ANTI_AIR );
			if (i >= 0)
				return i;
		}
		if (req.targetCovered && share( 4 ) < req.siegeShareMin)
		{
			const int i = first( AIRoles::ROLE_SIEGE );
			if (i >= 0)
				return i;
		}
		for (int i : fast)
		{
			const Unit &u = units[static_cast<size_t>( i )];
			if (used[static_cast<size_t>( i )] || r.value + u.value > req.valueLimit)
				continue;
			const bool air = (u.roles & AIRoles::ROLE_ANTI_AIR) != 0;
			if (air && total > 0.0f && (roleValue[3] + u.value) / (total + u.value) > req.airShareMax)
				continue;
			return i;
		}
		return -1;
	};
	while (static_cast<int>( r.picked.size() ) < req.maxUnits)
	{
		const int i = next();
		if (i < 0)
			break;
		add( i );
		r.ratio = ratioOf( r.picked );
		if (r.ratio >= req.goalRatio && shortRoles( true ) == 0)
			break;
	}
	if (r.picked.empty())
	{
		r.missing = AIRoles::ROLE_FRONT;
		return r;
	}
	r.ratio = ratioOf( r.picked );
	r.missing = shortRoles( false );		// what the squad lacks, also where no unit could fill it: a want for the production
	if (r.ratio < req.minRatio)
		r.missing |= AIRoles::ROLE_FRONT;
	r.ok = core && r.ratio >= req.minRatio;		// the shares are met as far as the units allow, they do not block the squad
	return r;
}

}  // namespace AISquad

#endif // _AI_SQUAD_H_
