/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// GameCommon.h
// Part of header detangling
// John McDonald, Aug 2002

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameCommon.h"

#include <cmath>

const char *TheVeterancyNames[] = 
{
	"REGULAR",
	"VETERAN",
	"ELITE",
	"HEROIC",
	nullptr
};

const char *TheRelationshipNames[] =
{
	"ENEMIES",
	"NEUTRAL",
	"ALLIES",
	nullptr
};

//-------------------------------------------------------------------------------------------------
Real normalizeAngle(Real angle)
{
	DEBUG_ASSERTCRASH(!_isnan(angle), ("Angle is NAN in normalizeAngle!\n"));

	if( _isnan(angle) )
		return 0;// ARGH!!!! Don't assert and then not handle it!  Error bad!  Fix error!

	// Angles also come from files (map objects). An infinite angle would never leave the loops below,
	// a huge one practically never (subtracting 2*PI no longer changes it). Values up to 1e6 keep the
	// exact subtraction, so the game logic computes as before.
	if( !std::isfinite(angle) )
		return 0;
	if( angle > 1.0e6f || angle < -1.0e6f )
		angle = std::fmod(angle, 2*PI);

	while (angle > PI) 
		angle -= 2*PI;

	while (angle <= -PI) 
		angle += 2*PI;

	return angle;
}

