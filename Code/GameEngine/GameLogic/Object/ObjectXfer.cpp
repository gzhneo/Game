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

#include "Common/LogicToClient.h"
#include "Common/LogicAudio.h"
#include "Common/BitFlagsIO.h"
#include "Common/BuildAssistant.h"
#include "Common/Dict.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/ModuleFactory.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Radar.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/WellKnownKeys.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"
#include "Common/PerfTimer.h"
#include "GameClient/Anim2D.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/Eva.h"
#include "GameClient/GameClient.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/AIStats.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/FiringTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/AutoHealBehavior.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/CountermeasuresBehavior.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Module/DamageModule.h"
#include "GameLogic/Module/DeletionUpdate.h"
#include "GameLogic/Module/DestroyModule.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/ObjectDefectionHelper.h"
#include "GameLogic/Module/ObjectRepulsorHelper.h"
#include "GameLogic/Module/ObjectSMCHelper.h"
#include "GameLogic/Module/ObjectWeaponStatusHelper.h"
#include "GameLogic/Module/OverchargeBehavior.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/PowerPlantUpgrade.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/RadarUpgrade.h"
#include "GameLogic/Module/RebuildHoleBehavior.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/SpecialAbilityUpdate.h"
#include "GameLogic/Module/StatusDamageHelper.h"
#include "GameLogic/Module/StickyBombUpdate.h"
#include "GameLogic/Module/SubdualDamageHelper.h"
#include "GameLogic/Module/TempWeaponBonusHelper.h"
#include "GameLogic/Module/ToppleUpdate.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/UpgradeModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSet.h"
#include "GameLogic/Module/RadarUpdate.h"
#include "GameLogic/Module/PowerPlantUpdate.h"
#include "Common/CRCDebug.h"
#include "Common/MiscAudio.h"
#include "Common/AudioEventInfo.h"
#include "Common/DynamicAudioEventInfo.h"

// Save game and CRC: crc, xfer, loadPostProcess

//-------------------------------------------------------------------------------------------------
/** Object CRC implemtation */
//-------------------------------------------------------------------------------------------------
void Object::crc( Xfer *xfer )
{
	// This is evil - we cast the const Matrix3D * to a Matrix3D * because the XferCRC class must use
	// the same interface as the XferLoad class for save game restore.  This only works because
	// XferCRC does not modify its data.

#ifdef DEBUG_CRC
//	g_logObjectCRCs = TRUE;
//	Bool g_logAllObjects = TRUE;
	AsciiString logString;
	AsciiString tmp;
	Bool doLogging = g_logObjectCRCs /* && getControllingPlayer()->getPlayerType() == PLAYER_HUMAN */;
	if (doLogging)
	{
		tmp.format("CRC of Object %d (%s), owned by player %d, ", m_id, getTemplate()->getName().str(), getControllingPlayer()->getPlayerIndex());
		logString.concat(tmp);
	}
#endif DEBUG_CRC

	xfer->xferUnsignedByte(&m_privateStatus);
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("m_privateStatus: %X, ", static_cast<UnsignedInt>(m_privateStatus));
		logString.concat(tmp);
	}
#endif // DEBUG_CRC

	xfer->xferUser(const_cast<Matrix3D *>(getTransformMatrix()),	sizeof(Matrix3D));
#ifdef DEBUG_CRC
	if (doLogging)
	{
		XferCRC tmpXfer;
		tmpXfer.open("tmp");
		tmpXfer.xferUser(const_cast<Matrix3D *>(getTransformMatrix()),	sizeof(Matrix3D));
		tmp.format("getTransformMatrix(): %8.8X, ", tmpXfer.getCRC());
		tmpXfer.close();
		logString.concat(tmp);
	}
#endif DEBUG_CRC
	

#ifdef DEBUG_CRC
	if (doLogging)
	{
		const Matrix3D *mtx = getTransformMatrix();
		CRCDEBUG_LOG(("CRC of Object %d (%s), owned by player %d, ", m_id, getTemplate()->getName().str(), getControllingPlayer()->getPlayerIndex()));
		DUMPMATRIX3D(mtx);
	}
#endif DEBUG_CRC











	xfer->xferUser(&m_id,															sizeof(m_id));
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("m_id: %d, ", m_id);
		logString.concat(tmp);
	}
#endif DEBUG_CRC
	xfer->xferUser(&m_objectUpgradesCompleted,				sizeof(Int64));
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("m_objectUpgradesCompleted: %d bits, ", m_objectUpgradesCompleted.count());
		logString.concat(tmp);
	}
#endif DEBUG_CRC
	if (m_experienceTracker)
		xfer->xferSnapshot( m_experienceTracker );
#ifdef DEBUG_CRC
	if (doLogging)
	{
		XferCRC tmpXfer;
		tmpXfer.open("tmp");
		tmpXfer.xferSnapshot(m_experienceTracker);
		tmp.format("m_experienceTracker: %8.8X, ", tmpXfer.getCRC());
		tmpXfer.close();
		logString.concat(tmp);
	}
#endif DEBUG_CRC

	Real health = getBodyModule()->getHealth();
	xfer->xferUser(&health,														sizeof(health));
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("health: %g/%8.8X, ", health, AS_INT(health));
		logString.concat(tmp);
	}
#endif DEBUG_CRC

	xfer->xferUnsignedInt(&m_weaponBonusCondition);
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("m_weaponBonusCondition: %8.8X, ", m_weaponBonusCondition);
		logString.concat(tmp);
	}
#endif DEBUG_CRC

	Real scalar = getBodyModule()->getDamageScalar();
	xfer->xferUser(&scalar,														sizeof(scalar));
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("damage scalar: %g/%8.8X\n", scalar, AS_INT(scalar));
		logString.concat(tmp);

		CRCDEBUG_LOG(("%s", logString.str()));
	}
#endif DEBUG_CRC

	for (Int i=0; i<WEAPONSLOT_COUNT; ++i)
	{
		Weapon *thisWeapon = getWeaponInWeaponSlot(static_cast<WeaponSlotType>(i));
		if (thisWeapon)
		{
			xfer->xferSnapshot( thisWeapon );
		}
	}
	
}  // end crc

//-------------------------------------------------------------------------------------------------
/** Object xfer implemtation
	* Version Info:
	* 1: Initial version 
	* 2: Xfers m_singleUseCommandUsed... determines if the single use command button has been used or not.
	* 3: Xfers the solehealingbenefactor ID and expiration frame
	* 4: misc stuff that got missed somehow
	* 5: m_isReceivingDifficultyBonus
	* 6: We do indeed need to save m_containedBy.  The comment misrepresents what the contain module will do.
	* 7: save full mtx, not pos+orient.
	* 8: Kris: Conversion of object status bits from UnsignedInt to BitFlags<>
	* 9: Extra sighting for reveal to all with different range units
	* 10: partition cell of the last vision update
	*/
//-------------------------------------------------------------------------------------------------
void Object::xfer( Xfer *xfer )
{
	
	// version
	const XferVersion currentVersion = 10;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// object ID
	ObjectID id = getID();
	xfer->xferObjectID( &id );
	setID( id );

	DEBUG_LOG(("Xfer Object %s id=%d\n",getTemplate()->getName().str(),id));

	if (version >= 7)
	{
		Matrix3D mtx = *getTransformMatrix();
		xfer->xferMatrix3D(&mtx);
		setTransformMatrix(&mtx);
	}
	else
	{
		// object position
		Coord3D pos = *getPosition();
		xfer->xferCoord3D( &pos );
		setPosition( &pos );

		// orientation
		Real orientation = getOrientation();
		xfer->xferReal( &orientation );
		setOrientation( orientation );
	}

	// team
	TeamID teamID = m_team ? m_team->getID() : TEAM_ID_INVALID;
	xfer->xferUser( &teamID, sizeof( TeamID ) );
	// DON'T set the team yet; must wait till we read our status bits, 
	// since setTeam can affect the player's power usage, but that could
	// be done incorrectly if our status bits aren't accurate yet... (srj)

	// producer id
	xfer->xferObjectID( &m_producerID );

	// builder id
	xfer->xferObjectID( &m_builderID );

	// drawable id
	Drawable *draw = getDrawable();
	DrawableID drawableID = draw ? draw->getID() : INVALID_DRAWABLE_ID;
	xfer->xferDrawableID( &drawableID );
	if( xfer->getXferMode() == XFER_LOAD )
	{

		// change the ID of the drawable attached to be the same ID as it was when it was saved
		draw->setID( drawableID );

	}  // end if

	// internal name
	xfer->xferAsciiString( &m_name );

	// The loader created this object in a default team with default status bits, which booked its power there
	// (Player::becomingTeamMember). Remember that before the saved status bits arrive.
	const Bool creationPowerBooked = xfer->getXferMode() == XFER_LOAD && m_team != nullptr
		&& !getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION );

	// status
	if( version >= 8 )
	{
		m_status.xfer( xfer );
	}
	else
	{
		//We are loading an old version, so we must convert it from a 32-bit int to a bitflag
		UnsignedInt oldStatus;
		xfer->xferUnsignedInt( &oldStatus );

		//Clear our status
		m_status.clear();

		for( int i = 0; i < 32; i++ )
		{
			UnsignedInt bit = 1<<i;
			if( oldStatus & bit )
			{
				ObjectStatusTypes status = static_cast<ObjectStatusTypes>(i+1);
				m_status.set( MAKE_OBJECT_STATUS_MASK( status ) );
			}
		}
	}

	// script status
	xfer->xferUnsignedByte( &m_scriptStatus );

	// private status
	xfer->xferUnsignedByte( &m_privateStatus );

	// OK, now that we have xferred our status bits, it's safe to set the team...
	if( xfer->getXferMode() == XFER_LOAD )
	{
		Team *team = TheTeamFactory->findTeamByID( teamID );
		if( team == nullptr )
		{
			DEBUG_CRASH(( "Object::xfer - Unable to load team\n" ));
			throw SaveCode::SC_INVALID_DATA;
		}
		// Under construction, leaving the creation team below does not book the power off again (and the
		// finished building books it once more); the creation team would keep it for good.
		if( creationPowerBooked && getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
			m_team->getControllingPlayer()->getEnergy()->objectLeavingInfluence( this );
		const Bool restoring = true;
		setOrRestoreTeam( team, restoring );
	}

	// geometry info
	xfer->xferSnapshot( &m_geometryInfo );

	// sighting info, last look - must be saved cause we save PartitionCell::m_shroudLevel
	xfer->xferSnapshot( m_partitionLastLook );

	if( version >= 9 )
		xfer->xferSnapshot( m_partitionRevealAllLastLook );

	// sighting info, last shroud - must be saved cause we save PartitionCell::m_shroudLevel
	xfer->xferSnapshot( m_partitionLastShroud );

	// vision spied by
	xfer->xferUser( m_visionSpiedBy, sizeof( Int ) * MAX_PLAYER_COUNT );

	// vision spied by mask
	xfer->xferUser( &m_visionSpiedMask, sizeof( PlayerMaskType ) );

	// sighting info, last threat
	// John M says we don't need to save this (CBD)
//	xfer->xferSnapshot( &m_partitionLastThreat );

	// sighting info, last value
	// John M says we don't need to save this (CBD)
//	xfer->xferSnapshot( &m_partitionLastValue );

	// vision range
	xfer->xferReal( &m_visionRange );

	// shroud clearing range
	xfer->xferReal( &m_shroudClearingRange );

	// shroud range
	xfer->xferReal( &m_shroudRange );

	// disabled mask
	m_disabledMask.xfer( xfer );

	//New var added for version 2. Determines if the single use command button has been used or not.
	if( xfer->getXferMode() == XFER_SAVE || version >= 2 )
	{
		xfer->xferBool( &m_singleUseCommandUsed );
	}
	else
	{
		m_singleUseCommandUsed = false;
	}

	// disabled till frame
	xfer->xferUser( m_disabledTillFrame, sizeof( UnsignedInt ) * DISABLED_COUNT );

	// special model condition until
	xfer->xferUnsignedInt( &m_smcUntil );

	//
	// radar data ... when loading, we will remove all objects from the radar and let
	// the radar system load itself as a separate chunk of data from the save file
	//
	if( xfer->getXferMode() == XFER_LOAD && m_radarData )
		TheRadar->removeObject( this );

	// experience tracker
	xfer->xferSnapshot( m_experienceTracker );

	//
	// we do not need to do anything with our m_containedBy pointer, the post process
	// of that objects contain module will actually re-do the contain process again
	//
	// m_containedBy <-- do nothing with this right now
	if( version >= 6 )
	{
		// No, the contain module is just going to friend_ reach in and set this for us.
		// Containers more complicated than Open (like Tunnel) can't do that.  Our variable,
		// our responsibility.
		if( xfer->getXferMode() == XFER_SAVE )
		{
			if( m_containedBy != nullptr )
				m_xferContainedByID = m_containedBy->getID();
			else
				m_xferContainedByID = INVALID_ID;
		}


		xfer->xferObjectID( &m_xferContainedByID );
	}

	// contained by frame
	xfer->xferUnsignedInt( &m_containedByFrame );

	// construction percent
	xfer->xferReal( &m_constructionPercent );

	// upgrades completed
	xfer->xferUpgradeMask( &m_objectUpgradesCompleted );

	// original team name
	xfer->xferAsciiString( &m_originalTeamName );

	// indicator color
	xfer->xferColor( &m_indicatorColor );

	// health box offset
	xfer->xferCoord3D( &m_healthBoxOffset );

	// Entered & exited housekeeping.
	Int i;
	xfer->xferByte(&m_numTriggerAreasActive);
	xfer->xferUnsignedInt(&m_enteredOrExitedFrame);
	xfer->xferICoord3D(&m_iPos);
	if (m_numTriggerAreasActive<0 || m_numTriggerAreasActive>MAX_TRIGGER_AREA_INFOS) {
		DEBUG_CRASH(("Invalid m_numTriggerAreasActive = %d, max is %d", m_numTriggerAreasActive, 
			MAX_TRIGGER_AREA_INFOS));
		throw SaveCode::SC_INVALID_DATA;
	}	 
	for (i=0; i<m_numTriggerAreasActive; i++) {
		AsciiString triggerName;
		if (m_triggerInfo[i].pTrigger) {
			triggerName = m_triggerInfo[i].pTrigger->getTriggerName();
		}
		xfer->xferAsciiString(&triggerName);
		if (xfer->getXferMode() == XFER_LOAD)
		{
			//
			// CBD (11-13-2002) I'm disabling this because it appears there might be some areas with
			// empty names, see John A. for more info
			//
			//if (triggerName.isNotEmpty())
			  m_triggerInfo[i].pTrigger = TheTerrainLogic->getTriggerAreaByName(triggerName);
		}
		xfer->xferByte(&m_triggerInfo[i].entered);
		xfer->xferByte(&m_triggerInfo[i].exited);
		xfer->xferByte(&m_triggerInfo[i].isInside);
	}
	// Layer object is pathing on.
	xfer->xferUser(&m_layer, sizeof(m_layer));

	// Layer of current path goal.
	xfer->xferUser(&m_destinationLayer, sizeof(m_destinationLayer));

	// Object selectability.
	xfer->xferBool(&m_isSelectable);

	xfer->xferUnsignedInt(&m_safeOcclusionFrame);

	// User formations.
	xfer->xferUser(&m_formationID, sizeof(m_formationID));
	if (m_formationID!=NO_FORMATION_ID) {
		xfer->xferCoord2D(&m_formationOffset);
	}

	// module count
	UnsignedShort moduleCount = 0;
	for (BehaviorModule** b = m_behaviors; *b; ++b)
		++moduleCount;

	xfer->xferUnsignedShort( &moduleCount );
	AsciiString moduleIdentifier;
	BehaviorModule *module;
	if( xfer->getXferMode() == XFER_SAVE )
	{

		// go through all modules
		for (BehaviorModule** b = m_behaviors; *b; ++b)
		{

			// get module
			module = *b;

			// write module identifier
			moduleIdentifier = TheNameKeyGenerator->keyToName( module->getModuleTagNameKey() );
			DEBUG_ASSERTCRASH( moduleIdentifier != AsciiString::TheEmptyString,
												 ("Object::xfer - Module tag key does not translate to a string!\n") );
			xfer->xferAsciiString( &moduleIdentifier );

			// begin a data block
			xfer->beginBlock();

			// xfer data
			xfer->xferSnapshot( module );

			// end data block
			xfer->endBlock();

		}  // end for, it

	}  // end if, save
	else
	{
		AsciiString otherModuleIdentifier;

		// read all module data
		for( UnsignedShort i2 = 0; i2 < moduleCount; ++i2 )
		{

			// read module name
			xfer->xferAsciiString( &moduleIdentifier );
			NameKeyType moduleIdentifierKey = TheNameKeyGenerator->nameToKey(moduleIdentifier);

			// find the module with this identifier in the module list
			module = nullptr;
			for (BehaviorModule** b = m_behaviors; b && *b; ++b)
			{

				if (moduleIdentifierKey == (*b)->getModuleTagNameKey())
				{
					module = *b;
					break;
				}

			}  // end for, moduleIt

			// start of a new block
			Int dataSize = xfer->beginBlock();

			//
			// if we didn't find the module, it's quite possible that we have removed
			// it from the object definition in a future patch, if that is so, we need to
			// skip the module data in the file
			//
			if( module == nullptr )
			{

				// for testing purposes, this module better be found
//				DEBUG_CRASH(( "Object::xfer - Module '%s' was indicated in file, but not found on object '%s'(%d)\n",
//											moduleIdentifier.str(), getTemplate()->getName().str(), getID() ));

				// skip this data in the file
				xfer->skip( dataSize );

			}  // end if
			else
			{

				// xfer the data into this module
				xfer->xferSnapshot( module );

			}  // end else
			
			// end block
			xfer->endBlock();

		}  // end for, i module count recorded in file

	}  // end else, load


	if ( version >= 3 )
	{
		xfer->xferObjectID( &m_soleHealingBenefactorID );
		xfer->xferUnsignedInt( &m_soleHealingBenefactorExpirationFrame );
	}
	else if ( xfer->getXferMode() == XFER_LOAD )
	{
		m_soleHealingBenefactorID = INVALID_ID;
		m_soleHealingBenefactorExpirationFrame = 0;
	}

	// Doesn't need to be saved.  These are created as needed.  jba.
	//AIGroup*		m_group;															///< if non-NULL, we are part of this group of agents

	// don't need to save m_partitionData.
	DEBUG_ASSERTCRASH(!(xfer->getXferMode() == XFER_LOAD && m_partitionData == nullptr), ("should not be in partitionmgr yet"));

	// don't need to be saved or loaded; are inited & cached for runtime only by our ctor (srj)
	//m_repulsorHelper;
	//m_smcHelper;
	//m_wsHelper;
	//m_defectionHelper;
	//m_firingTracker;
	//m_contain;
	//m_body;
	//m_ai;
	//m_physics;
#if defined(_DEBUG)
	//m_hasDiedAlready;
#endif

	if (version >= 4)
	{
		// xfer the weaponSetFlags FIRST, since we need 'em to restore the weaponSet properly. (srj)
		m_curWeaponSetFlags.xfer( xfer );
		xfer->xferUnsignedInt(&m_weaponBonusCondition);
		xfer->xferUser(&m_lastWeaponCondition, sizeof(m_lastWeaponCondition));

		// do the weaponSet itself after all the weapon-related stuff, just in case
		xfer->xferSnapshot(&m_weaponSet);

		m_specialPowerBits.xfer( xfer );

		xfer->xferAsciiString(&m_commandSetStringOverride);

		xfer->xferBool(&m_modulesReady);
	}

	if (version >= 5)
	{
		xfer->xferBool(&m_isReceivingDifficultyBonus);
	} 
	else
		m_isReceivingDifficultyBonus = FALSE;

	// The cell the partition last saw the center in. Without it the first partition update after loading
	// takes every object for moved and renews its look at the current position, so the shroud and with it
	// the game would differ from the run without saving.
	if (version >= 10)
	{
		Int cellX = -1;
		Int cellY = -1;
		const PartitionCell *lastCell = m_partitionData ? m_partitionData->friend_getLastCell() : nullptr;
		if (lastCell)
		{
			cellX = lastCell->getCellX();
			cellY = lastCell->getCellY();
		}
		xfer->xferInt( &cellX );
		xfer->xferInt( &cellY );
		if (xfer->getXferMode() == XFER_LOAD && m_partitionData)
			m_partitionData->friend_setLastCell( (cellX >= 0 && cellY >= 0) ? ThePartitionManager->getCellAt( cellX, cellY ) : nullptr );
	}

}  // end xfer

//-------------------------------------------------------------------------------------------------
/** Object load game post process phase */
//-------------------------------------------------------------------------------------------------
void Object::loadPostProcess()
{
	if( m_xferContainedByID != INVALID_ID )
		m_containedBy = TheGameLogic->findObjectByID(m_xferContainedByID);
	else
		m_containedBy = nullptr;

}  // end loadPostProcess
