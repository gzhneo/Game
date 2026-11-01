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

// FILE: CaveSystem.cpp /////////////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood July 2002
// Desc:   System responsible for keeping track of all cave systems on the map
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameState.h"
#include "Common/TunnelTracker.h"
#include "Common/Xfer.h"
#include "GameLogic/CaveSystem.h"


CaveSystem::CaveSystem()
{
}

CaveSystem::~CaveSystem()
{
}

void CaveSystem::init()
{
}

void CaveSystem::reset()
{
	// entries may be null: deleted slots are not filled up, so the indexes do not shift
	m_tunnelTrackerVector.clear();
}

void CaveSystem::update()
{
}

Bool CaveSystem::canSwitchIndexToIndex( Int oldIndex, Int newIndex )
{
	// When I grant permission, you need to do it.  ie call Unregister and then re-register with the new number
	TunnelTracker *oldTracker = nullptr;
	TunnelTracker *newTracker = nullptr;
	if( m_tunnelTrackerVector.size() > oldIndex )
	{
		oldTracker = m_tunnelTrackerVector[oldIndex].get();
		if( oldTracker && oldTracker->getContainCount() > 0 )
			return FALSE;// You can't switch a connection if one of the two is non empty
	}
	if( m_tunnelTrackerVector.size() > newIndex )
	{
		newTracker = m_tunnelTrackerVector[newIndex].get();
		if( newTracker && newTracker->getContainCount() > 0 )
			return FALSE;// You can't switch a connection if one of the two is non empty
	}

	// Both are either empty or non-existent, so go ahead.  
	// (Remember non-exist is only a valid case because you are going to do the switch now.)

	return TRUE;
}

void CaveSystem::registerNewCave( Int theIndex )
{
	Bool needToCreate = FALSE;
	if( theIndex >= m_tunnelTrackerVector.size() )
	{
		// You are new and off the edge, so I will fill NULLs up to you and then make a newTracker at that spot
		while( theIndex >= m_tunnelTrackerVector.size() )
			m_tunnelTrackerVector.emplace_back();

		needToCreate = TRUE;
	}
	else
	{
		// else you either exist or have existed, so I will either let things be or re-create that slot
		if( m_tunnelTrackerVector[theIndex] == nullptr )
			needToCreate = TRUE;
	}

	if( needToCreate )// if true, we new theIndex is the index of a NULL to be filled
		m_tunnelTrackerVector[theIndex].reset(newInstance(TunnelTracker));
}

void CaveSystem::unregisterCave( Int theIndex )
{
	// Doesn't need to do a thing.  ContainModule logic knows how to say goodbye, and a TunnelTracker
	// knows how to exist while having no entry points.
	theIndex;
}

TunnelTracker *CaveSystem::getTunnelTrackerForCaveIndex( Int theIndex )
{
	TunnelTracker *theTracker = nullptr;
	if( theIndex < m_tunnelTrackerVector.size() )
	{
		theTracker = m_tunnelTrackerVector[theIndex].get();
	}

	DEBUG_ASSERTCRASH( theTracker != nullptr, ("No one should be interested in a sub-cave that doesn't exist.") );

	return theTracker;
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method
	* Version Info
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void CaveSystem::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// tunnel tracker size and data
	UnsignedShort count = static_cast<UnsignedShort>(m_tunnelTrackerVector.size());
	xfer->xferUnsignedShort( &count );
	TunnelTracker *tracker;
	if( xfer->getXferMode() == XFER_SAVE )
	{
		std::vector< PoolPtr<TunnelTracker> >::iterator it;

		for( it = m_tunnelTrackerVector.begin(); it != m_tunnelTrackerVector.end(); ++it )
		{

			// xfer data
			tracker = it->get();
			xfer->xferSnapshot( tracker );

		}  // end

	}  // end if, save
	else
	{

		// the list must be empty now
		if( m_tunnelTrackerVector.empty() == FALSE )
		{

			DEBUG_CRASH(( "CaveSystem::xfer - m_tunnelTrackerVector should be empty but is not\n" ));
			throw SaveCode::SC_INVALID_DATA;

		}  // end if

		// read each item
		for( UnsignedShort i = 0; i < count; ++i )
		{

			// allocate new tracker; the vector owns it before reading, so a failed read does not leak it
			tracker = newInstance( TunnelTracker );
			m_tunnelTrackerVector.emplace_back( tracker );

			// read data
			xfer->xferSnapshot( tracker );

		}  // end for, i

	}  // end else, laod

}  // end xfer


