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

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "formatinto.h"
#include "Common/LogicAudio.h"
#include "Common/DataChunk.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/LatchRestore.h"
#include "Common/MessageStream.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Shell.h"
#include "GameClient/View.h"
#include "GameClient/CampaignManager.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/ObjectTypes.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/ScriptActions.h"
#include "GameLogic/ScriptConditions.h"
#include "GameLogic/ScriptEngine.h"
#include "ScriptEngineInternal.h"
#include "GameLogic/SidesList.h"
#include "GameClient/ParticleSys.h"
#include "Common/MapObject.h"
#include "W3DDevice/GameClient/W3DAssetManagerExposed.h"

// Script engine save game: crc, xfer and load post process

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void ScriptEngine::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
static void xferListAsciiString( Xfer *xfer, ListAsciiString *list )
{

	// sanity
	DEBUG_ASSERTCRASH( list != nullptr, ("xferListAsciiString - Invalid parameters\n") );

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// size of list
	UnsignedShort count = static_cast<UnsignedShort>(list->size());
	xfer->xferUnsignedShort( &count );

	// list data
	AsciiString string;
	if( xfer->getXferMode() == XFER_SAVE )
	{

		// write each string
		ListAsciiStringIt it;
		for( it = list->begin(); it != list->end(); ++it )
		{

			string = *it;
			xfer->xferAsciiString( &string );

		}  // end for, it

	}  // end if, save
	else
	{

		// this list should be empty upon loading
		if( list->empty() == FALSE )
		{

			DEBUG_CRASH(( "xferListAsciiString - list should be empty upon loading but is not\n" ));
			throw SaveCode::SC_INVALID_DATA;

		}  // end if

		// read each string
		for( UnsignedShort i = 0; i < count; ++i )
		{

			// read string
			xfer->xferAsciiString( &string );

			// put on list
			list->push_back( string );

		}  // end for, i

	}  // end else, load

}  // end xferListAsciiString

// ------------------------------------------------------------------------------------------------
/** Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
static void xferListAsciiStringUINT( Xfer *xfer, ListAsciiStringUINT *list )
{

	// sanity
	DEBUG_ASSERTCRASH( list != nullptr, ("xferListAsciiStringUINT - Invalid parameters\n") );

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// size of list
	UnsignedShort count = static_cast<UnsignedShort>(list->size());
	xfer->xferUnsignedShort( &count );

	// list data
	AsciiString string;
	UnsignedInt unsignedIntData;
	if( xfer->getXferMode() == XFER_SAVE )
	{

		// write each string
		ListAsciiStringUINTIt it;
		for( it = list->begin(); it != list->end(); ++it )
		{

			// string
			string = it->first;
			xfer->xferAsciiString( &string );

			// unsigned int data
			unsignedIntData = it->second;
			xfer->xferUnsignedInt( &unsignedIntData );

		}  // end for, it

	}  // end if, save
	else
	{
		PairAsciiStringUINT newPair;

		// this list should be empty upon loading
		if( list->empty() == FALSE )
		{

			DEBUG_CRASH(( "xferListAsciiStringUINT - list should be empty upon loading but is not\n" ));
			throw SaveCode::SC_INVALID_DATA;

		}  // end if

		// read each string
		for( UnsignedShort i = 0; i < count; ++i )
		{

			// read string
			xfer->xferAsciiString( &string );

			// read unsigned int data
			xfer->xferUnsignedInt( &unsignedIntData );

			// put on list
			newPair.first = string;
			newPair.second = unsignedIntData;
			list->push_back( newPair );

		}  // end for, i

	}  // end else, load

}  // end xferListAsciiStringUINT

// ------------------------------------------------------------------------------------------------
/** Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
static void xferListAsciiStringObjectID( Xfer *xfer, ListAsciiStringObjectID *list )
{

	// sanity
	DEBUG_ASSERTCRASH( list != nullptr, ("xferListAsciiStringObjectID - Invalid parameters\n") );

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// size of list
	UnsignedShort count = static_cast<UnsignedShort>(list->size());
	xfer->xferUnsignedShort( &count );

	// list data
	AsciiString string;
	ObjectID objectID;
	if( xfer->getXferMode() == XFER_SAVE )
	{

		// write each string
		ListAsciiStringObjectIDIt it;
		for( it = list->begin(); it != list->end(); ++it )
		{

			// string
			string = it->first;
			xfer->xferAsciiString( &string );

			// object id
			objectID = it->second;
			xfer->xferObjectID( &objectID );

		}  // end for, it

	}  // end if, save
	else
	{
		AsciiStringObjectIDPair newPair;

		// this list should be empty upon loading
		if( list->empty() == FALSE )
		{

			DEBUG_CRASH(( "xferListAsciiStringObjectID - list should be empty upon loading but is not\n" ));
			throw SaveCode::SC_INVALID_DATA;

		}  // end if

		// read each string
		for( UnsignedShort i = 0; i < count; ++i )
		{

			// read string
			xfer->xferAsciiString( &string );

			// read object id data
			xfer->xferObjectID( &objectID );

			// put on list
			newPair.first = string;
			newPair.second = objectID;
			list->push_back( newPair );

		}  // end for, i

	}  // end else, load

}  // end xferListAsciiStringObjectID

// ------------------------------------------------------------------------------------------------
/** Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
static void xferListAsciiStringCoord3D( Xfer *xfer, ListAsciiStringCoord3D *list )
{

	// sanity
	DEBUG_ASSERTCRASH( list != nullptr, ("xferListAsciiStringCoord3D - Invalid parameters\n") );

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// size of list
	UnsignedShort count = static_cast<UnsignedShort>(list->size());
	xfer->xferUnsignedShort( &count );

	// list data
	AsciiString string;
	Coord3D coord;
	if( xfer->getXferMode() == XFER_SAVE )
	{

		// write each string
		ListAsciiStringCoord3DIt it;
		for( it = list->begin(); it != list->end(); ++it )
		{

			// string
			string = it->first;
			xfer->xferAsciiString( &string );

			// coord
			coord = it->second;
			xfer->xferCoord3D( &coord );

		}  // end for, it

	}  // end if, save
	else
	{
		AsciiStringCoord3DPair newPair;

		// this list should be empty upon loading
		if( list->empty() == FALSE )
		{

			DEBUG_CRASH(( "xferListAsciiStringCoord3D - list should be empty upon loading but is not\n" ));
			throw SaveCode::SC_INVALID_DATA;

		}  // end if

		// read each string
		for( UnsignedShort i = 0; i < count; ++i )
		{

			// read string
			xfer->xferAsciiString( &string );

			// read coord 
			xfer->xferCoord3D( &coord );

			// put on list
			newPair.first = string;
			newPair.second = coord;
			list->push_back( newPair );

		}  // end for, i

	}  // end else, load

}  // ene xferListAsciiStringCoord3D

// ------------------------------------------------------------------------------------------------
/** Xfer method 
	* Version Info:
	* 1: Initial version
	* 2: Added m_namedReveals and m_allObjectTypeLists (CBD)
	* 3: Added m_objectsShouldReceiveDifficultyBonus (JKMCD)
	* 4: current music track info
	* 5: add ChooseVictimAlwaysUsesNormal
	*/
// ------------------------------------------------------------------------------------------------
void ScriptEngine::xfer( Xfer *xfer )
{
	Int i;

	// version
	const XferVersion currentVersion = 5;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// sequential script count and data
	UnsignedShort sequentialScriptCount = static_cast<UnsignedShort>(m_sequentialScripts.size());
	xfer->xferUnsignedShort( &sequentialScriptCount );
	SequentialScript *sequentialScript;
	if( xfer->getXferMode() == XFER_SAVE )
	{

		// save each element
		VecSequentialScriptPtrIt it;
		for( it = m_sequentialScripts.begin(); it != m_sequentialScripts.end(); ++it )
		{

			// get data
			sequentialScript = *it;

			// xfer data
			xfer->xferSnapshot( sequentialScript );

		}  // end for, it

	}  // end if, save
	else
	{

		// this list should be empty on loading
		if( m_sequentialScripts.size() != 0 )
		{

			DEBUG_CRASH(( "ScriptEngine::xfer - m_sequentialScripts should be empty but is not\n" ));
			throw SaveCode::SC_INVALID_DATA;

		}  // end if

		// read each entry
		for( UnsignedShort i5 = 0; i5 < sequentialScriptCount; ++i5 )
		{

			// allocate new sequential script and put on our list
			sequentialScript = newInstance( SequentialScript );

			// tie to our list
			m_sequentialScripts.push_back( sequentialScript );

			// xfer data
			xfer->xferSnapshot( sequentialScript );

		}  // end for i

	}  // end else, load

	// counters
	UnsignedShort countersSize = static_cast<UnsignedShort>(m_numCounters);
	xfer->xferUnsignedShort( &countersSize );
	if( countersSize > MAX_COUNTERS )
	{

		DEBUG_CRASH(( "ScriptEngine::xfer - MAX_COUNTERS has changed size, need to version this\n" ));
		throw SaveCode::SC_INVALID_DATA;

	}  // end if
	for( i = 0; i < countersSize; ++i )
	{

		// value
		xfer->xferInt( &m_counters[ i ].value );

		// name
		xfer->xferAsciiString( &m_counters[ i ].name );

		// countdown timer
		xfer->xferBool( &m_counters[ i ].isCountdownTimer );
		
	}  // end for, i

	// num counters
	xfer->xferInt( &m_numCounters );

	// flags
	UnsignedShort flagsSize = static_cast<UnsignedShort>(m_numFlags);
	xfer->xferUnsignedShort( &flagsSize );
	if( flagsSize > MAX_FLAGS )
	{
	
		DEBUG_CRASH(( "ScriptEngine::xfer - MAX_FLAGS has changed size, need to version this\n" ));
		throw SaveCode::SC_INVALID_DATA;

	}  // end if
	for( i = 0; i < flagsSize; ++i )
	{

		// value
		xfer->xferBool( &m_flags[ i ].value );
		
		// name
		xfer->xferAsciiString( &m_flags[ i ].name );

	}  // end for i
		
	// num flags
	xfer->xferInt( &m_numFlags );

	// attack priority info
	UnsignedShort attackPriorityInfoSize = static_cast<UnsignedShort>(m_numAttackInfo);
	xfer->xferUnsignedShort( &attackPriorityInfoSize );
	if( attackPriorityInfoSize > MAX_ATTACK_PRIORITIES )
	{

		DEBUG_CRASH(( "ScriptEngine::xfer - MAX_ATTACK_PRIORITIES size has changed, need to version this\n" ));
		throw SaveCode::SC_INVALID_DATA;

	}  // end if
	for( i = 0; i < attackPriorityInfoSize; ++i )
	{

		// xfer each data
		xfer->xferSnapshot( &m_attackPriorityInfo[ i ] );

	}  // end for i

	// num attack info
	xfer->xferInt( &m_numAttackInfo );

	// end game timers
	xfer->xferInt( &m_endGameTimer );
	xfer->xferInt( &m_closeWindowTimer );

	// named objects
	UnsignedShort namedObjectsCount = static_cast<UnsignedShort>(m_namedObjects.size());
	xfer->xferUnsignedShort( &namedObjectsCount );
	AsciiString namedObjectName;
	Object *obj;
	ObjectID objectID;
	if( xfer->getXferMode() == XFER_SAVE )
	{

		// iterate elemnts
		VecNamedRequestsIt it;
		for( it = m_namedObjects.begin(); it != m_namedObjects.end(); ++it )
		{

			// write name
			namedObjectName = it->first;
			xfer->xferAsciiString( &namedObjectName );

			// write object id (note that object may be NULL)
			obj = it->second;
			objectID = obj ? obj->getID() : INVALID_ID;
			xfer->xferObjectID( &objectID );

		}  // end for i

	}  // end if, save
	else
	{
		NamedRequest req;

		//
		// list should be empty, it is legal for it to not be empty at this point
		// according to John M., so we're clearing it now
		//
		m_namedObjects.clear();

		// read each element
		for( UnsignedShort i4 = 0; i4 < namedObjectsCount; ++i4 )
		{

			// read name
			xfer->xferAsciiString( &namedObjectName );

			// read object id and turn into object pointer
			xfer->xferObjectID( &objectID );
			obj = TheGameLogic->findObjectByID( objectID );
			if( obj == nullptr && objectID != INVALID_ID )
			{

				DEBUG_CRASH(( "ScriptEngine::xfer - Unable to find object by ID for m_namedObjects\n" ));
				throw SaveCode::SC_INVALID_DATA;

			}  // end if

			// assign
			req.first = namedObjectName;
			req.second = obj;
			m_namedObjects.push_back( req );

		}  // end for, i

	}  // end else, load

	// first update
	xfer->xferBool( &m_firstUpdate );

	// trade (this needs a better descriptive name (CBD)
	xfer->xferUser( &m_fade, sizeof( TFade ) );

	// min fade
	xfer->xferReal( &m_minFade );

	// max fade
	xfer->xferReal( &m_maxFade );

	// curr fade value
	xfer->xferReal( &m_curFadeValue );

	// current fade frame
	xfer->xferInt( &m_curFadeFrame );

	// fade frames increase
	xfer->xferInt( &m_fadeFramesIncrease );

	// fade frames hold
	xfer->xferInt( &m_fadeFramesHold );

	// fade frames decrease
	xfer->xferInt( &m_fadeFramesDecrease );

	// complete video
	xferListAsciiString( xfer, &m_completedVideo );

	// testing speech
	xferListAsciiStringUINT( xfer, &m_testingSpeech );

	// testing audio
	xferListAsciiStringUINT( xfer, &m_testingAudio );

	// ui interactions
	xferListAsciiString( xfer, &m_uiInteractions );

	// triggered special powers
	UnsignedShort triggeredSpecialPowersSize = MAX_PLAYER_COUNT;
	xfer->xferUnsignedShort( &triggeredSpecialPowersSize );
	if( triggeredSpecialPowersSize != MAX_PLAYER_COUNT )
	{
	
		DEBUG_CRASH(( "ScriptEngine::xfer - MAX_PLAYER_COUNT has changed, m_triggeredSpecialPowers size is now different and we must version this\n" ));
		throw SaveCode::SC_INVALID_DATA;

	}  // end if
	for( i = 0; i < triggeredSpecialPowersSize; ++i )
		xferListAsciiStringObjectID( xfer, &m_triggeredSpecialPowers[ i ] );

	// midway special powers
	UnsignedShort midwaySpecialPowersSize = MAX_PLAYER_COUNT;
	xfer->xferUnsignedShort( &midwaySpecialPowersSize );
	if( midwaySpecialPowersSize != MAX_PLAYER_COUNT )
	{
	
		DEBUG_CRASH(( "ScriptEngine::xfer - MAX_PLAYER_COUNT has changed, m_midwaySpecialPowers size is now different and we must version this\n" ));
		throw SaveCode::SC_INVALID_DATA;

	}  // end if
	for( i = 0; i < midwaySpecialPowersSize; ++i )
		xferListAsciiStringObjectID( xfer, &m_midwaySpecialPowers[ i ] );

	// finished special powers
	UnsignedShort finishedSpecialPowersSize = MAX_PLAYER_COUNT;
	xfer->xferUnsignedShort( &finishedSpecialPowersSize );
	if( finishedSpecialPowersSize != MAX_PLAYER_COUNT )
	{
	
		DEBUG_CRASH(( "ScriptEngine::xfer - MAX_PLAYER_COUNT has changed, m_finishedSpecialPowers size is now different and we must version this\n" ));
		throw SaveCode::SC_INVALID_DATA;

	}  // end if
	for( i = 0; i < finishedSpecialPowersSize; ++i )
		xferListAsciiStringObjectID( xfer, &m_finishedSpecialPowers[ i ] );

	// completed upgrades
	UnsignedShort completedUpgradesSize = MAX_PLAYER_COUNT;
	xfer->xferUnsignedShort( &completedUpgradesSize );
	if( completedUpgradesSize != MAX_PLAYER_COUNT )
	{
	
		DEBUG_CRASH(( "ScriptEngine::xfer - MAX_PLAYER_COUNT has changed, m_completedUpgrades size is now different and we must version this\n" ));
		throw SaveCode::SC_INVALID_DATA;

	}  // end if
	for( i = 0; i < completedUpgradesSize; ++i )
		xferListAsciiStringObjectID( xfer, &m_completedUpgrades[ i ] );

	// acquired sciences
	UnsignedShort acquiredSciencesSize = MAX_PLAYER_COUNT;
	xfer->xferUnsignedShort( &acquiredSciencesSize );
	if( acquiredSciencesSize != MAX_PLAYER_COUNT )
	{
	
		DEBUG_CRASH(( "ScriptEngine::xfer - MAX_PLAYER_COUNT has changed, m_acquiredSciences size is now different and we must version this\n" ));
		throw SaveCode::SC_INVALID_DATA;

	}  // end if
	for( i = 0; i < acquiredSciencesSize; ++i )
		xfer->xferScienceVec( &m_acquiredSciences[ i ] );

	// topple directions
	xferListAsciiStringCoord3D( xfer, &m_toppleDirections );

	// breeze info
	xfer->xferReal(	&m_breezeInfo.m_direction );
	xfer->xferReal(	&m_breezeInfo.m_directionVec.x );
	xfer->xferReal(	&m_breezeInfo.m_directionVec.y );
	xfer->xferReal( &m_breezeInfo.m_intensity );
	xfer->xferReal( &m_breezeInfo.m_lean );
	xfer->xferReal( &m_breezeInfo.m_randomness );
	xfer->xferShort( &m_breezeInfo.m_breezePeriod );
	xfer->xferShort( &m_breezeInfo.m_breezeVersion );

	// game difficulty
	xfer->xferUser( &m_gameDifficulty, sizeof( GameDifficulty ) );

	// freeze by script
	xfer->xferBool( &m_freezeByScript );
	
	// version 2
	if( version >= 2 )
	{

		// number of entries in named reveals
		UnsignedShort namedRevealCount = static_cast<UnsignedShort>(m_namedReveals.size());
		xfer->xferUnsignedShort( &namedRevealCount );

		// named reveal data
		if( xfer->getXferMode() == XFER_SAVE )
		{

			// iterate vector
			VecNamedRevealIt it;
			for( it = m_namedReveals.begin(); it != m_namedReveals.end(); ++it )
			{

				// name
				xfer->xferAsciiString( &it->m_revealName );

				// waypoint name
				xfer->xferAsciiString( &it->m_waypointName );

				// radius
				xfer->xferReal( &it->m_radiusToReveal );

				// player name
				xfer->xferAsciiString( &it->m_playerName );
				
			}  // end for, it

		}  // end if, save
		else
		{

			// the vector should be emtpy now
			if( m_namedReveals.empty() == FALSE )
			{

				DEBUG_CRASH(( "ScriptEngine::xfer - m_namedReveals should be empty but is not!\n" ));
				throw SaveCode::SC_INVALID_DATA;

			}  // end if

			// read all entries
			NamedReveal reveal;
			for( UnsignedShort i3 = 0; i3 < namedRevealCount; ++i3 )
			{
				
				// read name
				xfer->xferAsciiString( &reveal.m_revealName );

				// read waypoint name
				xfer->xferAsciiString( &reveal.m_waypointName );

				// read radius
				xfer->xferReal( &reveal.m_radiusToReveal );
				
				// read player name
				xfer->xferAsciiString( &reveal.m_playerName );

				// put on list
				m_namedReveals.push_back( reveal );

			}  // end for, i

		}  // end else, load

		// all object type lists size
		UnsignedShort allObjectTypesCount = static_cast<UnsignedShort>(m_allObjectTypeLists.size());
		xfer->xferUnsignedShort( &allObjectTypesCount );

		// all object type lists data
		if( xfer->getXferMode() == XFER_SAVE )
		{
			
			// iterate list
			AllObjectTypesIt it;
			ObjectTypes *objectTypes;
			for( it = m_allObjectTypeLists.begin(); it != m_allObjectTypeLists.end(); ++it )
			{

				// get object types from iterator
				objectTypes = *it;

				// save object types
				xfer->xferSnapshot( objectTypes );

			}  // end for, it

		}  // end if, save
		else
		{

			// sanity, the list should be empty now
			if( m_allObjectTypeLists.empty() == FALSE )
			{

				DEBUG_CRASH(( "ScriptEngine::xfer - m_allObjectTypeLists should be empty but is not!\n" ));
				throw SaveCode::SC_INVALID_DATA;

			}  // end if

			// read all data
			ObjectTypes *objectTypes;
			for( UnsignedShort i2 = 0; i2 < allObjectTypesCount; ++i2 )
			{

				// allocate a new object types
				objectTypes = newInstance( ObjectTypes );

				// xfer object types data
				xfer->xferSnapshot( objectTypes );

				// put on list
				m_allObjectTypeLists.push_back( objectTypes );

			}  // end for, i

		}  //  end else, load

	}  // end if, version 2

	if (version >= 3) {
		xfer->xferBool(&m_objectsShouldReceiveDifficultyBonus);
	} else {
		m_objectsShouldReceiveDifficultyBonus = TRUE;
	}

	if (version >= 4)
	{
		xfer->xferAsciiString(&m_currentTrackName);
	}

	if (version >= 5)
	{
		xfer->xferBool(&m_ChooseVictimAlwaysUsesNormal);
	}
	else
	{
		m_ChooseVictimAlwaysUsesNormal = false;
	}

	if( xfer->getXferMode() == XFER_LOAD ) {
		// We are doing a load.  If there is no fade active, do a black fade in to start.
		if (m_fade == FADE_NONE) {
			m_fade = FADE_MULTIPLY; //default to a fade in from black.
			m_curFadeFrame = 0;
			m_minFade = 1.0f;
			m_maxFade = 0.0f;	
			m_fadeFramesIncrease = 0;
			m_fadeFramesHold = 0;
			m_fadeFramesDecrease = FRAMES_TO_FADE_IN_AT_START;
			m_curFadeValue = 0.0f;
		}
	}
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void ScriptEngine::loadPostProcess( void )
{

	// Now that we've loaded everything, go through and set them all back in sync with what we
	// currently think they should be.
	TheScriptActions->doEnableOrDisableObjectDifficultyBonuses(m_objectsShouldReceiveDifficultyBonus);

	if (m_currentTrackName.isNotEmpty())
	{
		AudioEventRTS event(m_currentTrackName);
		event.setPlayerIndex(ThePlayerList->getLocalPlayer()->getPlayerIndex());
		TheLogicAudio->addAudioEvent(&event);
	}

}  // end loadPostProcess
