/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// AIEconomy.h: energy rules of the new AI, without engine types so the unit tests can include them.
//
// The classic AI builds a power plant only after the player is under-powered. The command layer orders it before the
// consumer that would tip the balance: the balance counts what the finished buildings produce and use plus everything
// that is ordered or under construction.

#pragma once

#ifndef _AI_ECONOMY_H_
#define _AI_ECONOMY_H_

namespace AIEconomy
{

/// Energy after the buildings that are ordered or under construction (positive values produce, negative ones use).
inline int energyAfter( int production, int consumption, const int *pending, int pendingCount )
{
	int balance = production - consumption;
	for (int i = 0; i < pendingCount; ++i)
		balance += pending[i];
	return balance;
}

/// Power plants to order so that the balance reaches the margin; 0 if it does, or a plant gives no energy.
inline int powerPlantsNeeded( int balance, int margin, int plantOutput )
{
	if (balance >= margin || plantOutput <= 0)
		return 0;
	return (margin - balance + plantOutput - 1) / plantOutput;
}

/// May a building with this energy (negative: uses) be ordered now?
inline bool energyAllows( int balanceWithPending, int buildingEnergy )
{
	return buildingEnergy >= 0 || balanceWithPending + buildingEnergy >= 0;
}

/// Weight (per mille) of an enemy unit last seen age frames ago: halves every halfLife frames (linear between the
/// halvings), zero from maxAge on. Integer only, the planner input must not depend on the platform.
inline int memoryWeight( unsigned age, unsigned halfLife, unsigned maxAge )
{
	if (halfLife == 0 || age >= maxAge)
		return 0;
	const unsigned halvings = age / halfLife;
	if (halvings >= 10)
		return 0;
	const int base = 1000 >> halvings;
	const unsigned rest = age % halfLife;
	return base - static_cast<int>( static_cast<unsigned>( base / 2 ) * rest / halfLife );
}

}  // namespace AIEconomy

#endif // _AI_ECONOMY_H_
