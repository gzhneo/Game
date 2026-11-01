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
#include "formatinto.h"
#include "GameLogic/DesyncReport.h"
#include "Common/LogicAudio.h"
#include "Common/OwnedSubsystem.h"
#include "Common/AudioAffect.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/BuildAssistant.h"
#include "Common/CRCDebug.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/GameLOD.h"
#include "Common/GameState.h"
#include "Common/INI.h"
#include "Common/LatchRestore.h"
#include "Common/MapObject.h"
#include "Common/ModuleFactory.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Radar.h"
#include "Common/RandomValue.h"
#include "Common/Recorder.h"
#include "Common/StatsCollector.h"
#include "Common/ThingFactory.h"
#include "Common/Team.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Water.h"
#include "GameClient/Snow.h"
#include "Common/WellKnownKeys.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"
#include "Common/XferDeepCRC.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameText.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/InGameUI.h"
#include "GameClient/LoadScreen.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/TerrainVisual.h"
#include "GameClient/View.h"
#include "GameClient/ControlBar.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/CaveSystem.h"
#include "GameLogic/CrateSystem.h"
#include "GameLogic/FPUControl.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Module/DestroyModule.h"
#include "GameLogic/Module/OpenContain.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptActions.h"
#include "GameLogic/ScriptConditions.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/GhostObject.h"
#include "Common/DataChunk.h"
#include "GameLogic/Scripts.h"
#include "GameNetwork/GameLobbyCallbacks.h"
#include "GameNetwork/NetworkInterface.h"
#include "Engine/Clock.h"
#include "Engine/ThreadRole.h"
#include "Engine/JobSystem.h"
#include "Engine/Watchdog.h"
#include "Lib/SafeString.h"
#include "GameLogic/AIStats.h"
#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <vector>

// Desync report, CRC and save games: object table of contents, xfer, load post-processing

Bool inCRCGen = FALSE;

// ------------------------------------------------------------------------------------------------
/** Desync report for desync diagnosis.
	*
	* On a CRC mismatch the total checksum only says "something differs". This report computes the
	* same parts one by one, per subsystem and per object, and writes them as text lines.
	* tools/determinism/compare_crc.py compares two reports (two machines, or recording against
	* playback) and names the first differing object or subsystem.
	*
	* Diagnosis only: it reads the state (XferCRC) and changes nothing. */
// ------------------------------------------------------------------------------------------------
Bool GameLogic::writeDesyncReport( const char *path )
{
	if( path == nullptr )
		return FALSE;

	FILE *out = fopen( path, "w" );
	if( out == nullptr )
		return FALSE;

	setFPMode();

	// header: what the report refers to
	fprintf( out, "DESYNC-REPORT 1\n" );
	fprintf( out, "FRAME %u\n", m_frame );
	fprintf( out, "MAP %s\n", TheGlobalData ? TheGlobalData->m_mapName.str() : "?" );
	fprintf( out, "SEED %u\n", GetGameLogicRandomSeedCRC() );
	fprintf( out, "CRC %08X\n", getCRC( CRC_RECALC ) );

	// one line per subsystem
	struct { const char *name; Snapshot *snapshot; } subsystems[] =
	{
		{ "PartitionManager",	static_cast<Snapshot *>(ThePartitionManager) },
		{ "PlayerList",			static_cast<Snapshot *>(ThePlayerList) },
		{ "AI",					static_cast<Snapshot *>(TheAI) },
	};
	for( Int i = 0; i < static_cast<Int>(sizeof(subsystems)/sizeof(subsystems[0])); ++i )
	{
		if( subsystems[i].snapshot == nullptr )
			continue;
		XferCRC xfer;
		xfer.open( "desyncReport" );
		xfer.xferSnapshot( subsystems[i].snapshot );
		xfer.close();
		fprintf( out, "SUB %s %08X\n", subsystems[i].name, xfer.getCRC() );
	}

	// one line per object, in list order (the same order as in the game CRC)
	Int objectCount = 0;
	for( Object *obj = m_objList; obj; obj = obj->getNextObject() )
	{
		XferCRC xfer;
		xfer.open( "desyncReport" );
		xfer.xferSnapshot( obj );
		xfer.close();

		const Coord3D *pos = obj->getPosition();
		fprintf( out, "OBJ %u %s %08X pos %.3f %.3f %.3f\n",
			static_cast<UnsignedInt>(obj->getID()),
			obj->getTemplate() ? obj->getTemplate()->getName().str() : "?",
			xfer.getCRC(), pos ? pos->x : 0.0f, pos ? pos->y : 0.0f, pos ? pos->z : 0.0f );
		++objectCount;
	}
	fprintf( out, "OBJECTS %d\n", objectCount );

	fclose( out );
	DEBUG_LOG(( "state report written: %s (frame %u, %d objects)\n", path, m_frame, objectCount ));
	return TRUE;
}

// One "offset ID name" line in the deep CRC dump index, so the first differing byte of two dumps
// can be mapped to an object or section.
static void deepCRCIndex( FILE *index, XferCRC *xfer, const char *name, Int id )
{
	if (index)
		fprintf(index, "%ld %d %s\n", static_cast<XferDeepCRC *>(xfer)->getFileOffset(), id, name);
}

UnsignedInt GameLogic::getCRC( Int mode, AsciiString deepCRCFileName )
{
	if (mode != CRC_RECALC)
		return m_CRC;

	setFPMode();

	LatchRestore<Bool> latch(inCRCGen, !isInGameLogicUpdate());

	XferCRC *xferCRC;
	AsciiString marker;
	FILE *deepIndex = nullptr;
	if (deepCRCFileName.isNotEmpty())
	{
		xferCRC = NEW XferDeepCRC;
		xferCRC->open(deepCRCFileName.str());
		// -deepCRCFull: write the full xfer() state per object instead of crc() only
		if (TheGlobalData->m_deepCRCFull)
			static_cast<XferDeepCRC *>(xferCRC)->setFullState(TRUE);
		AsciiString indexName;
		indexName.format("%s.idx", deepCRCFileName.str());
		deepIndex = fopen(indexName.str(), "w");
	}
	else
	{
		AsciiString crcName;
#ifdef DEBUG_CRC
		if (isInGameLogicUpdate() && g_keepCRCSaves && m_frame < 5)
		{
			xferCRC = NEW XferDeepCRC;
			crcName.format("logicFrame%d.crc", (m_frame%5));
		}
		else
#endif // DEBUG_CRC
		{
			xferCRC = NEW XferCRC;
			crcName = "lightCRC";
		}
		xferCRC->open(crcName);
	}

	// calculate CRCs
	Object *obj;
	DEBUG_ASSERTCRASH(this == TheGameLogic, ("Not in GameLogic"));
	if (isInGameLogicUpdate())
	{
		CRCGEN_LOG(("CRC at start of frame %d is 0x%8.8X\n", m_frame, xferCRC->getCRC()));
	}

	marker = "MARKER:Objects";
	deepCRCIndex(deepIndex, xferCRC, marker.str(), -1);
	xferCRC->xferAsciiString(&marker);
	// The object checksum can run on the job system: each block computes a partial checksum over a
	// slice of the object list, and the results are combined in block order on the sim thread. The
	// object list is only read.
	//
	// The resulting CRC differs from the serial path, so reference replays are per mode; the
	// parallel path is therefore only active with -parallelCRC.
	if( TheGlobalData->m_parallelCRC
			&& ::Engine::JobSystem::get().workerCount() > 0
			&& xferCRC->getXferMode() == XFER_CRC )
	{
		// Record the object order once (the list itself is linked).
		std::vector<Object *> objects;
		objects.reserve( 1024 );
		for( obj = m_objList; obj; obj=obj->getNextObject() )
			objects.push_back( obj );

		constexpr size_t GRAIN = 64;	// fixed block size, independent of the worker count
		const size_t blockCount = (objects.size() + GRAIN - 1) / GRAIN;
		std::vector<UnsignedInt> blockCRC( blockCount, 0 );

		::Engine::parallelFor( objects.size(), GRAIN,
			[&objects, &blockCRC]( size_t begin, size_t end )
			{
				XferCRC blockXfer;
				blockXfer.open( "blockCRC" );
				for( size_t i = begin; i < end; ++i )
					blockXfer.xferSnapshot( objects[i] );
				blockXfer.close();
				blockCRC[ begin / GRAIN ] = blockXfer.getCRC();
			} );

		// In verify mode compute the same blocks serially again and compare, so a mismatch shows up at
		// once with its block number instead of as a desync.
		if( ::Engine::JobSystem::get().isVerifyParallel() )
		{
			for( size_t block = 0; block < blockCount; ++block )
			{
				const size_t begin = block * GRAIN;
				const size_t end = (begin + GRAIN < objects.size()) ? (begin + GRAIN) : objects.size();
				XferCRC serialXfer;
				serialXfer.open( "blockCRC" );
				for( size_t i = begin; i < end; ++i )
					serialXfer.xferSnapshot( objects[i] );
				serialXfer.close();
				if( serialXfer.getCRC() != blockCRC[block] )
				{
					::Engine::JobSystem::get().reportVerifyMismatch( "GameLogic::getCRC/Objects", block );
					blockCRC[block] = serialXfer.getCRC();	// on a mismatch the serial result wins
				}
			}
		}

		for( size_t i = 0; i < blockCount; ++i )
			xferCRC->xferUnsignedInt( &blockCRC[i] );
	}
	else
	{
		for( obj = m_objList; obj; obj=obj->getNextObject() )
		{
			deepCRCIndex(deepIndex, xferCRC, obj->getTemplate()->getName().str(), static_cast<Int>(obj->getID()));
			xferCRC->xferSnapshot( obj );
		}
	}
	deepCRCIndex(deepIndex, xferCRC, "RandomSeed", -1);
	UnsignedInt seed = GetGameLogicRandomSeedCRC();
	if (isInGameLogicUpdate())
	{
		CRCGEN_LOG(("CRC after objects for frame %d is 0x%8.8X\n", m_frame, xferCRC->getCRC()));
	}

	if (isInGameLogicUpdate())
	{
		CRCGEN_LOG(("RandomSeed: %d\n", seed));
	}
	if (xferCRC->getXferMode() == XFER_CRC)
	{
		xferCRC->xferUnsignedInt( &seed );
	}
	marker = "MARKER:ThePartitionManager";
	deepCRCIndex(deepIndex, xferCRC, marker.str(), -1);
	xferCRC->xferAsciiString(&marker);
	xferCRC->xferSnapshot( ThePartitionManager );
	if (isInGameLogicUpdate())
	{
		CRCGEN_LOG(("CRC after partition manager for frame %d is 0x%8.8X\n", m_frame, xferCRC->getCRC()));
	}

#ifdef DEBUG_CRC
	if ((g_crcModuleDataFromClient && !isInGameLogicUpdate()) ||
		(g_crcModuleDataFromLogic && isInGameLogicUpdate()))
	{
		marker = "MARKER:TheModuleFactory";
		xferCRC->xferAsciiString(&marker);
		xferCRC->xferSnapshot( TheModuleFactory );
		if (isInGameLogicUpdate())
		{
			CRCGEN_LOG(("CRC after module factory for frame %d is 0x%8.8X\n", m_frame, xferCRC->getCRC()));
		}
	}
#endif //DEBUG_CRC

	marker = "MARKER:ThePlayerList";
	deepCRCIndex(deepIndex, xferCRC, marker.str(), -1);
	xferCRC->xferAsciiString(&marker);
	xferCRC->xferSnapshot( ThePlayerList );
	if (isInGameLogicUpdate())
	{
		CRCGEN_LOG(("CRC after PlayerList for frame %d is 0x%8.8X\n", m_frame, xferCRC->getCRC()));
	}

	marker = "MARKER:TheAI";
	deepCRCIndex(deepIndex, xferCRC, marker.str(), -1);
	xferCRC->xferAsciiString(&marker);
	xferCRC->xferSnapshot( TheAI );
	if (isInGameLogicUpdate())
	{
		CRCGEN_LOG(("CRC after AI for frame %d is 0x%8.8X\n", m_frame, xferCRC->getCRC()));
	}

	if (xferCRC->getXferMode() == XFER_SAVE)
	{
		marker = "MARKER:GameSave";
		deepCRCIndex(deepIndex, xferCRC, marker.str(), -1);
		xferCRC->xferAsciiString(&marker);
		TheGameState->friend_xferSaveDataForCRC(xferCRC, SNAPSHOT_DEEPCRC_LOGICONLY);
	}

	if (deepIndex)
	{
		marker = "MARKER:PartitionOrder";
		deepCRCIndex(deepIndex, xferCRC, marker.str(), -1);
		xferCRC->xferAsciiString(&marker);
		ThePartitionManager->xferOrderForDiagnosis(xferCRC);

		marker = "MARKER:PathfindCells";
		deepCRCIndex(deepIndex, xferCRC, marker.str(), -1);
		xferCRC->xferAsciiString(&marker);
		TheAI->pathfinder()->xferCellsForDiagnosis(xferCRC);
	}

	deepCRCIndex(deepIndex, xferCRC, "END", -1);
	if (deepIndex)
		fclose(deepIndex);
	xferCRC->close();

	UnsignedInt theCRC = xferCRC->getCRC();

	delete xferCRC;
	xferCRC = nullptr;

	if (isInGameLogicUpdate())
	{
		CRCGEN_LOG(("CRC for frame %d is 0x%8.8X\n", m_frame, theCRC));
	}
	return theCRC;
}

// ------------------------------------------------------------------------------------------------
/** Light CRC */
// ------------------------------------------------------------------------------------------------
void GameLogic::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Given a string name, find the object TOC entry (if any) associated with it */
// ------------------------------------------------------------------------------------------------
GameLogic::ObjectTOCEntry *GameLogic::findTOCEntryByName( AsciiString name )
{

	for( ObjectTOCListIterator it = m_objectTOC.begin(); it != m_objectTOC.end(); ++it )
		if( (*it).name == name )
			return &(*it);

	return nullptr;

}  // end findTOCEntryByname

// ------------------------------------------------------------------------------------------------
/** Given a object TOC identifier, find the object TOC if any */
// ------------------------------------------------------------------------------------------------
GameLogic::ObjectTOCEntry *GameLogic::findTOCEntryById( UnsignedShort id )
{

	for( ObjectTOCListIterator it = m_objectTOC.begin(); it != m_objectTOC.end(); ++it )
		if( (*it).id == id )
			return &(*it);

	return nullptr;

}  // end findTOCEntryById

// ------------------------------------------------------------------------------------------------
/** Add an object TOC entry */
// ------------------------------------------------------------------------------------------------
void GameLogic::addTOCEntry( AsciiString name, UnsignedShort id )
{

	ObjectTOCEntry tocEntry;
	tocEntry.name = name;
	tocEntry.id = id;
	m_objectTOC.push_back( tocEntry );

}  // end addTOCEntry

// ------------------------------------------------------------------------------------------------
/** Xfer object table of contents */
// ------------------------------------------------------------------------------------------------
void GameLogic::xferObjectTOC( Xfer *xfer )
{

	// version
	const XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// clear our current table of contents
	m_objectTOC.clear();

	// xfer the table
	UnsignedInt tocCount = 0;
	if( xfer->getXferMode() == XFER_SAVE )
	{
		AsciiString templateName;

		// generate a new TOC based on the objects that are in the map
		for( Object *obj = getFirstObject(); obj; obj = obj->getNextObject() )
		{

			// get the name we're working with
			templateName = obj->getTemplate()->getName();

			// if is this object name already in the TOC, skip it
			if( findTOCEntryByName( templateName ) != nullptr )
				continue;
			
			// add this entry to the TOC
			addTOCEntry( obj->getTemplate()->getName(), static_cast<UnsignedShort>(++tocCount) );

		}  // end for obj

		// xfer entries in the TOC
		xfer->xferUnsignedInt( &tocCount );

		// xfer each TOC entry
		ObjectTOCListIterator it;
		ObjectTOCEntry *tocEntry;
		for( it = m_objectTOC.begin(); it != m_objectTOC.end(); ++it )
		{

			// get this toc entry
			tocEntry = &(*it);

			// xfer the name
			xfer->xferAsciiString( &tocEntry->name );

			// xfer the paired id
			xfer->xferUnsignedShort( &tocEntry->id );

		}  // end for

	}  // end if
	else
	{
		AsciiString templateName;
		UnsignedShort id;

		// how many entries are we going to read
		xfer->xferUnsignedInt( &tocCount );

		// read all the entries
		for( UnsignedInt i = 0; i < tocCount; ++i )
		{

			// read the name
			xfer->xferAsciiString( &templateName );

			// read the id
			xfer->xferUnsignedShort( &id );
			
			// add this to the TOC
			addTOCEntry( templateName, id );
					
		}  // end for i

	}  // end else

}  // end xferObjectTOC

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::prepareLogicForObjectLoad( void )
{

	//
	// this is a band-aid :(
	// when loading from a map file, objects were created for the bridges, towers, walls etc.
	// since we're going to load that data from the save game file we will destroy any of
	// those bridge objects and tower objects so that we can create them from the save data
	//
	Object *obj, *next;
	for( obj = getFirstObject(); obj; obj = next )
	{

		// get next
		next = obj->getNextObject();

		// is this a bridge object?
		if( obj->isKindOf( KINDOF_BRIDGE ) )
		{
			Bridge *bridge = TheTerrainLogic->findBridgeAt( obj->getPosition() );

			// sanity
			DEBUG_ASSERTCRASH( bridge, ("GameLogic::prepareLogicForObjectLoad - Unable to find bridge\n" ));

			// get the old object that is in the bridge info
			const BridgeInfo *bridgeInfo = bridge->peekBridgeInfo();
			Object *oldObject = findObjectByID( bridgeInfo->bridgeObjectID );
			DEBUG_ASSERTCRASH( oldObject, ("GameLogic::prepareLogicForObjectLoad - Unable to find old bridge object\n") );
			DEBUG_ASSERTCRASH( oldObject == obj, ("GameLogic::prepareLogicForObjectLoad - obj != oldObject\n") );

			//
			// destroy the 4 towers that are attached to this old object (they will be loaded from
			// the save game file
			//
			Object *oldTower;
			for( Int i = 0; i < BRIDGE_MAX_TOWERS; ++i )
			{

				oldTower = findObjectByID( bridgeInfo->towerObjectID[ i ] );
				if (oldTower) {
					destroyObject( oldTower );
				}

			}  // end for, i

			// destroy the old bridge object
			destroyObject( oldObject );

		}  // end if, bridge
		else if( obj->isKindOf( KINDOF_WALK_ON_TOP_OF_WALL ) )
		{

			// destroy walk on top of wall things too
			destroyObject( obj );

		}  // end else if

	}  // end for, obj

	// process the destruction of these objects immediately before we proceed with the load process
	processDestroyList();

	// there should be no objects anywhere
	DEBUG_ASSERTCRASH( getFirstObject() == nullptr,
										 ("GameLogic::prepareLogicForObjectLoad - There are still objects loaded in the engine, but it should be empty (Top is '%s')\n",
										 getFirstObject()->getTemplate()->getName().str()) );

}  // end prepareLogicForObjectLoad

// ------------------------------------------------------------------------------------------------
/** Load/Save game logic to xfer 
	*	Version Info:
	* 1: Initial version
	* 2: Added m_isScoringEnabled flag (BGC) 
	* 3: Added polygon triggers (CBD)
	* 4: Added block markers around object data, no version checking is done and therefore
	*		 this version breaks compatibility with previous versions. (CBD)
	* 5: Added xfering the BuildAssistant's sell list.
	* 9: Added m_rankPointsToAddAtGameStart, or else on a load game, your RestartGame button will forget your exp
  * 10: xfer m_superweaponRestriction
	* 11: xfer m_graceEndFrame (start protection)
	* 12: xfer the logic random state, the next object ID and the order of the sleepy update heap
	*/	
// ------------------------------------------------------------------------------------------------
void GameLogic::xfer( Xfer *xfer )
{
  
	// version
	const XferVersion currentVersion = 12;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// logic frame number
	xfer->xferUnsignedInt( &m_frame );

	//
	// note that we do not do the id counter here, we did it in the game state block because
	// it's important to do that part very early in the load process
	//
	// !!!DON'T DO THIS!!! ----> xfer->xferObjectID( &m_nextObjectID ); <---- !!!DON'T DO THIS!!! 

	//
	// xfer a table of contents that contain thing template and indentifier pairs.  this
	// table of contents is good for this save file only as unique numbers are only
	// generated and stored for the actual things that are on this map
	//
	xferObjectTOC( xfer );

	// when loading, we need to clean up bridges and get them ready for a load
	if( xfer->getXferMode() == XFER_LOAD )
		prepareLogicForObjectLoad();

	// object count
	UnsignedInt objectCount = getObjectCount();
	xfer->xferUnsignedInt( &objectCount );

	// object data
	Object *obj;
	ObjectTOCEntry *tocEntry;
	if( xfer->getXferMode() == XFER_SAVE )
	{

		for( obj = getFirstObject(); obj; obj = obj->getNextObject() )
		{

			// get the object TOC entry for this template
			tocEntry = findTOCEntryByName( obj->getTemplate()->getName() );
			if( tocEntry == nullptr )
			{

				DEBUG_CRASH(( "GameLogic::xfer - Object TOC entry not found for '%s'\n", obj->getTemplate()->getName().str() ));
				throw SaveCode::SC_INVALID_DATA;

			}  // end if

			// transfer TOC id entry
			xfer->xferUnsignedShort( &tocEntry->id );

			// begin a block of data
			xfer->beginBlock();

			// write object data
			xfer->xferSnapshot( obj );

			// end a block of data
			xfer->endBlock();

		}  // end for

	}  // end if, save
	else
	{
		Team *defaultTeam = ThePlayerList->getNeutralPlayer()->getDefaultTeam();
		const ThingTemplate *thingTemplate;

		// read all objects
		Object *const firstBeforeLoad = m_objList;
		Int objectDataSize;
		UnsignedShort tocID;
		ObjectTOCEntry *tocEntry2;
		for( UnsignedInt i = 0; i < objectCount; ++i )
		{

			// read toc entry identifier
			xfer->xferUnsignedShort( &tocID );

			// find Object TOC entry with this identifier
			tocEntry2 = findTOCEntryById( tocID );
			if( tocEntry2 == nullptr )
			{

				DEBUG_CRASH(( "GameLogic::xfer - No TOC entry match for id '%d'\n", tocID ));
				throw SaveCode::SC_INVALID_DATA;

			}  // end if

			// a block of data has begun
			objectDataSize = xfer->beginBlock();

			// find matching thing template
			thingTemplate = TheThingFactory->findTemplate( tocEntry2->name );
			if( thingTemplate == nullptr )
			{

				DEBUG_CRASH(( "GameLogic::xfer - Unrecognized thing template name '%s', skipping.  ENGINEERS - Are you *sure* it's OK to be ignoring this object from the save file???  Think hard about it!\n",
											tocEntry2->name.str() ));
				xfer->skip( objectDataSize );
				continue;
							
			}  // end if

			// create new object
			obj = TheThingFactory->newObject( thingTemplate, defaultTeam );

			// xfer the rest of the object data
			xfer->xferSnapshot( obj );

			// end of block of data (not necessary in a load, but looks symettrically nice)
			xfer->endBlock();

			// special case for wall pieces, need to add them to the pathfinder
			if( obj->isKindOf( KINDOF_WALK_ON_TOP_OF_WALL ) )
				TheAI->pathfinder()->addWallPiece( obj );

		}  // end for, i

		// registerObject prepended each loaded object, so they are in reverse saved order; the order of the
		// object list decides the update order and much more, put it back
		std::vector< Object * > loaded;
		for( obj = m_objList; obj && obj != firstBeforeLoad; obj = obj->getNextObject() )
			loaded.push_back( obj );
		for( size_t k = 0; k < loaded.size(); ++k )
			loaded[ k ]->removeFromList( &m_objList );
		for( size_t k = 0; k < loaded.size(); ++k )
			loaded[ k ]->prependToList( &m_objList );

	}  // end else

	// campaign info
	xfer->xferSnapshot( TheCampaignManager );

	// cave system info
	xfer->xferSnapshot( TheCaveSystem );

	// is scoring enabled
	if( version >= 2 ) 
		xfer->xferBool(&m_isScoringEnabled);

	// polygon triggers
	if( version >= 3 )
	{
		PolygonTrigger *poly;

		// count the number of polygon triggers we have
		UnsignedInt triggerCount = 0;
		for( poly = PolygonTrigger::getFirstPolygonTrigger(); poly; poly = poly->getNext() )
			triggerCount++;

		// sanity count
		UnsignedInt sanityTriggerCount = triggerCount;

		// xfer count
		xfer->xferUnsignedInt( &triggerCount );

		//
		// since the save game loaded should have exactly the same number of polygon triggers
		// in it that we did from loading the map this is just a sanity check here
		//
		if( sanityTriggerCount != triggerCount )
		{

			DEBUG_CRASH(( "GameLogic::xfer - Polygon trigger count mismatch.  Save file has a count of '%d', but map had '%d' triggers\n",
										sanityTriggerCount, triggerCount ));
			throw SaveCode::SC_INVALID_DATA;

		}  // end if

		// xfer each of the polygon triggers
		if( xfer->getXferMode() == XFER_SAVE )
		{
			Int triggerID;

			for( poly = PolygonTrigger::getFirstPolygonTrigger(); poly; poly = poly->getNext() )
			{

				// write polygon ID
				triggerID = poly->getID();	
				xfer->xferInt( &triggerID );

				// xfer polygon data
				xfer->xferSnapshot( poly );

			}  // end for, poly

		}  // end if, save
		else
		{
			Int triggerID;

			// loop through all triggers
			for( UnsignedInt i = 0; i < triggerCount; ++i )
			{

				// read ID
				xfer->xferInt( &triggerID );

				// find this polygon trigger
				poly = PolygonTrigger::getPolygonTriggerByID( triggerID );

				// sanity
				if( poly == nullptr )
				{

					DEBUG_CRASH(( "GameLogic::xfer - Unable to find polygon trigger with id '%d'\n",
												triggerID ));
					throw SaveCode::SC_INVALID_DATA;

				}  // end if

				// xfer polygon data
				xfer->xferSnapshot( poly );

			}  // end for, i

			//
			// force a recalculation of the pathfinding cause some of these polygon triggers
			// are water which can move during run-time, and therefore affecting the area
			// that objects can move on. Also, map objects need recalculation. jba.
			//
			TheAI->pathfinder()->newMap();

		}  // end else, load

	}	 // end if, version >= 3

	// note that version=4 is the same as version=3
	if (version >= 5)
	{
		xfer->xferInt(&m_rankLevelLimit);
	}

	if (version>=6) {
		// We need the list of buildings in process of being sold.
		TheBuildAssistant->xferTheSellList(xfer);
	}

	if (version >= 7) 
	{
		if( xfer->getXferMode() == XFER_SAVE )
		{
			for (BuildableMap::const_iterator it = m_thingTemplateBuildableOverrides.begin(); it != m_thingTemplateBuildableOverrides.end(); ++it )
			{
				AsciiString name = it->first;
				BuildableStatus bs = it->second;
				xfer->xferAsciiString(&name);
				xfer->xferUser(&bs, sizeof(bs));
			}
			AsciiString empty;
			xfer->xferAsciiString(&empty);
		}
		else if (xfer->getXferMode() == XFER_LOAD)
		{
			if (m_thingTemplateBuildableOverrides.empty() == false)
			{
				DEBUG_CRASH(( "GameLogic::xfer - m_thingTemplateBuildableOverrides should be empty, but is not\n"));
				throw SaveCode::SC_INVALID_DATA;
			}
			
			for (;;) 
			{
				AsciiString name;
				xfer->xferAsciiString(&name);
				if (name.isEmpty())
					break;
				BuildableStatus bs;
				xfer->xferUser(&bs, sizeof(bs));
				m_thingTemplateBuildableOverrides[name] = bs;
			}
		}
	}

	if (version >= 8)
	{
		xfer->xferBool(&m_showBehindBuildingMarkers);
		xfer->xferBool(&m_drawIconUI);
		xfer->xferBool(&m_showDynamicLOD);
		xfer->xferInt(&m_scriptHulkMaxLifetimeOverride);

		if( xfer->getXferMode() == XFER_SAVE )
		{
			for (ControlBarOverrideMap::const_iterator it = m_controlBarOverrides.begin(); it != m_controlBarOverrides.end(); ++it )
			{
				AsciiString name = it->first;
				AsciiString value = it->second ? it->second->getName() : AsciiString::TheEmptyString;
				xfer->xferAsciiString(&name);
				xfer->xferAsciiString(&value);
			}
			AsciiString empty;
			xfer->xferAsciiString(&empty);
		}
		else if (xfer->getXferMode() == XFER_LOAD)
		{
			if (m_controlBarOverrides.empty() == false)
			{
				DEBUG_CRASH(( "GameLogic::xfer - m_controlBarOverrides should be empty, but is not\n"));
				throw SaveCode::SC_INVALID_DATA;
			}
			
			for (;;) 
			{
				AsciiString name;
				xfer->xferAsciiString(&name);
				if (name.isEmpty())
					break;
				AsciiString value;
				xfer->xferAsciiString(&value);
				ConstCommandButtonPtr button = nullptr;
				if (value.isNotEmpty())
				{
					button = TheControlBar->findCommandButton(value);
					DEBUG_ASSERTCRASH(button != nullptr, ("Could not find button %s\n",value.str()));
				}
				m_controlBarOverrides[name] = button;
			}
		}
	}

	if (version >= 9)
	{
		xfer->xferInt(&m_rankPointsToAddAtGameStart);
	}

  if ( version >= 10 )
  {
    xfer->xferUnsignedShort( &m_superweaponRestriction );
  }
  else if ( xfer->getXferMode() == XFER_LOAD )
  {
    m_superweaponRestriction = 0;
  }

	if ( version >= 11 )
		xfer->xferUnsignedInt( &m_graceEndFrame );
	else if ( xfer->getXferMode() == XFER_LOAD )
		m_graceEndFrame = 0;

	// The random state; set at the very end of loading, since loading itself may draw random numbers.
	if ( version >= 12 )
	{
		UnsignedInt state[7];
		GetGameLogicRandomState( state );
		for ( Int i = 0; i < 7; ++i )
			xfer->xferUnsignedInt( &state[i] );
		if ( xfer->getXferMode() == XFER_LOAD )
		{
			for ( Int i = 0; i < 7; ++i )
				m_xferRandomState[i] = state[i];
			m_xferRandomStatePending = TRUE;
		}

		// next object ID: the highest one in use may have died since
		ObjectID nextObjID = m_nextObjID;
		xfer->xferObjectID( &nextObjID );
		if ( xfer->getXferMode() == XFER_LOAD )
			m_xferNextObjID = nextObjID;

		// Modules with the same wake frame and phase run in the order of the heap array; rebuilding the heap
		// from the object list would change it, so the array order is saved (object ID, update module index).
		UnsignedInt count = static_cast<UnsignedInt>( m_sleepyUpdates.size() );
		xfer->xferUnsignedInt( &count );
		if ( xfer->getXferMode() == XFER_LOAD )
		{
			m_xferSleepyOrder.clear();
			for ( UnsignedInt i = 0; i < count; ++i )
			{
				ObjectID id = INVALID_ID;
				Int index = -1;
				xfer->xferObjectID( &id );
				xfer->xferInt( &index );
				m_xferSleepyOrder.push_back( std::make_pair( id, index ) );
			}
		}
		else
		{
			std::map< UpdateModulePtr, std::pair<ObjectID, Int> > keys;
			for ( Object *sleepyObj = getFirstObject(); sleepyObj; sleepyObj = sleepyObj->getNextObject() )
			{
				Int k = 0;
				for ( BehaviorModule** b = sleepyObj->getBehaviorModules(); *b; ++b )
				{
#ifdef DIRECT_UPDATEMODULE_ACCESS
					UpdateModulePtr m = static_cast<UpdateModulePtr>((*b)->getUpdate());
#else
					UpdateModulePtr m = (*b)->getUpdate();
#endif
					if ( m == nullptr )
						continue;
					keys[ m ] = std::make_pair( sleepyObj->getID(), k );
					++k;
				}
			}
			for ( UnsignedInt i = 0; i < count; ++i )
			{
				std::map< UpdateModulePtr, std::pair<ObjectID, Int> >::const_iterator it = keys.find( m_sleepyUpdates[i] );
				ObjectID id = ( it != keys.end() ) ? it->second.first : INVALID_ID;
				Int index = ( it != keys.end() ) ? it->second.second : -1;
				xfer->xferObjectID( &id );
				xfer->xferInt( &index );
			}
		}
	}
}  // end xfer

// ------------------------------------------------------------------------------------------------
void GameLogic::friend_applyLoadedRandomState( void )
{
	if ( !m_xferRandomStatePending )
		return;
	SetGameLogicRandomState( m_xferRandomState );
	m_xferRandomStatePending = FALSE;
}

// ------------------------------------------------------------------------------------------------
/** Load post process entry point */
// ------------------------------------------------------------------------------------------------
void GameLogic::loadPostProcess( void )
{

	//
	// the act of loading objects can (theoretically) as a side effect create other objects, 
	// our m_nextObjID that we maintain to give objects unique ID is also continually
	// climbing higher and higher due to us allocating objects during load (even though
	// those objects have their ids overwritten with data from the file.  To prevent the
	// m_nextObjID from getting un-necessarily high we will set it to the next available
	// id from the objects that are now in the world and actually in use
	//
	m_nextObjID = INVALID_ID;
	Object *obj;
	for( obj = getFirstObject(); obj; obj = obj->getNextObject() )
		if( obj->getID() >= m_nextObjID )
			m_nextObjID = static_cast<ObjectID>(static_cast<UnsignedInt>(obj->getID()) + 1);
	// a save game knows the counter; continue from there so that new objects get the same IDs as without saving
	if( m_xferNextObjID > m_nextObjID )
		m_nextObjID = m_xferNextObjID;
	m_xferNextObjID = INVALID_ID;

	// blow away the sleepy update and normal update module lists
	for (std::vector<UpdateModulePtr>::iterator it = m_sleepyUpdates.begin(); it != m_sleepyUpdates.end(); ++it)
	{
		(*it)->friend_setIndexInLogic(-1);
	}
	m_sleepyUpdates.clear();
	UnsignedInt now = TheGameLogic->getFrame();
	if (now == 0)
		now = 1;

	// go through all objects, examine each update module and put it on the appropriate update list
	std::map< std::pair<ObjectID, Int>, UpdateModulePtr > byKey;
	std::vector< UpdateModulePtr > all;
	for( obj = getFirstObject(); obj; obj = obj->getNextObject() )
	{

		// get the update list of modules for this object
		Int k = 0;
		for( BehaviorModule** b = obj->getBehaviorModules(); *b; ++b )
		{
#ifdef DIRECT_UPDATEMODULE_ACCESS
			// evil, but necessary at this point. (srj)
			UpdateModulePtr u = static_cast<UpdateModulePtr>((*b)->getUpdate());
#else
			UpdateModulePtr u = (*b)->getUpdate();
#endif
			if (!u)
				continue;
			
			DEBUG_ASSERTCRASH(u->friend_getIndexInLogic() == -1, ("Hmm, expected index to be -1 here"));

			// check each update module
			UnsignedInt when = u->friend_getNextCallFrame();
			// note that 'when' will only be zero for legacy save files.
			if (when == 0)
				u->friend_setNextCallFrame(now);
			byKey[ std::make_pair( obj->getID(), k ) ] = u;
			all.push_back( u );
			++k;
				
		}  // end for, u

	}  // end for, obj

	// first the modules in the saved heap order (a valid heap as it is), then any the save did not know
	for( size_t i = 0; i < m_xferSleepyOrder.size(); ++i )
	{
		std::map< std::pair<ObjectID, Int>, UpdateModulePtr >::iterator it = byKey.find( m_xferSleepyOrder[i] );
		if( it == byKey.end() || it->second->friend_getIndexInLogic() != -1 )
			continue;
		m_sleepyUpdates.push_back( it->second );
		it->second->friend_setIndexInLogic( static_cast<Int>(m_sleepyUpdates.size()) - 1 );
	}
	m_xferSleepyOrder.clear();
	for( size_t i = 0; i < all.size(); ++i )
	{
		if( all[i]->friend_getIndexInLogic() != -1 )
			continue;
		m_sleepyUpdates.push_back( all[i] );
		all[i]->friend_setIndexInLogic( static_cast<Int>(m_sleepyUpdates.size()) - 1 );
	}

	// re-sort the priority queue all at once now that all modules are on it
	remakeSleepyUpdate();

}  // end loadPostProcess
