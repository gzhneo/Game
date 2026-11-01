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

// FILE: GameLogic.h //////////////////////////////////////////////////////////////////////////////
// GameLogic singleton class - defines interface to GameLogic methods and objects
// Author: Michael S. Booth, October 2000
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _GAME_LOGIC_H_
#define _GAME_LOGIC_H_

#include "Common/GameCommon.h"	// ensure we get DUMP_PERF_STATS, or not
#include <memory>
#include "Common/GameType.h"
#include "Common/Snapshot.h"
#include "Common/STLTypedefs.h"
#include "Common/ObjectStatusTypes.h"
#include "GameNetwork/NetworkDefs.h"
#include "Common/STLTypedefs.h"
#include "Engine/GenerationTable.h"
#include "Engine/LogicAccessGuard.h"
#include "GameLogic/ObjectHandle.h"
#include "GameLogic/Module/UpdateModule.h"	// needed for DIRECT_UPDATEMODULE_ACCESS

// forward declarations
class AudioEventRTS;
class Object;
class Drawable;
class Player;
class ThingTemplate;
class Team;
class CommandList;
class GameMessage;
class LoadScreen;
class WindowLayout;
class TerrainLogic;
class GhostObjectManager;
class PartitionManager;
class ScriptActionsInterface;
class ScriptConditionsInterface;
class ScriptEngine;
class StatsCollector;
class CommandButton;
enum BuildableStatus : int;	// must match the underlying type of the definition


using ConstCommandButtonPtr = const CommandButton*;

// What kind of game we're in.
enum
{
	GAME_SINGLE_PLAYER,
	GAME_LAN,
	GAME_SKIRMISH,
	GAME_REPLAY,
	GAME_SHELL,
	GAME_INTERNET,
	GAME_NONE
};

enum
{
	CRC_CACHED,
	CRC_RECALC
};


/// Function pointers for use by GameLogic callback functions.
using GameLogicFuncPtr = void (*)( Object *obj, void *userData ); 
//typedef std::hash_map<ObjectID, Object *, rts::hash<ObjectID>, rts::equal_to<ObjectID> > ObjectPtrHash;
//typedef ObjectPtrHash::const_iterator ObjectPtrIter;

using ObjectPtrVector = std::vector<Object*>;

// ------------------------------------------------------------------------------------------------
/**
 * The implementation of GameLogic 
 */
class GameLogic : public SubsystemInterface, public Snapshot
{

public:

	GameLogic( void );
	virtual ~GameLogic() override;

	// subsytem methods
	virtual void init( void ) override;															///< Initialize or re-initialize the instance
	virtual void reset( void ) override;															///< Reset the logic system
	virtual void update( void ) override;														///< update the world

#if defined(_DEBUG)
	Int getNumberSleepyUpdates() const {return static_cast<Int>(m_sleepyUpdates.size());} //For profiling, so not in Release.
#endif
	void processCommandList( CommandList *list );		///< process the command list

	void prepareNewGame( Int gameMode, GameDifficulty diff, Int rankPoints );						///< prepare for new game 

	void logicMessageDispatcher( GameMessage *msg, 
																			 void *userData );	///< Logic command list processing

	void registerObject( Object *obj );							///< Given an object, register it with the GameLogic and give it a unique ID

	void addObjectToLookupTable( Object *obj );			///< add object ID to hash lookup table
	void removeObjectFromLookupTable( Object *obj );///< remove object ID from hash lookup table

	/// @todo Change this to refer to a Region3D as an extent of the world
	void setWidth( Real width );										///< Sets the width of the world
	Real getWidth( void );													///< Returns the width of the world
	void setHeight( Real height );									///< Sets the height of the world
	Real getHeight( void );													///< Returns the height of the world

	Bool isInGameLogicUpdate( void ) const { return m_isInUpdate; }
	Bool isNewGamePending( void ) const { return m_startNewGame; }	///< the next update loads a map
	/// Time per tick phase and per update module type (collected with -crcLog); FALSE if nothing was collected.
	Bool formatPhaseStatistics( AsciiString &phases, AsciiString &modules, AsciiString &spikes ) const;
	void releasePhaseStatistics( void );
	UnsignedInt getFrame( void );										///< Returns the current simulation frame number
	UnsignedInt getCRC( Int mode = CRC_CACHED, AsciiString deepCRCFileName = AsciiString::TheEmptyString );		///< Returns the CRC

	// Desync diagnosis: writes the checksum of every subsystem and every object to a text file;
	// tools/determinism/compare_crc.py compares two such reports and names the first difference.
	Bool writeDesyncReport( const char *path );

	void setObjectIDCounter( ObjectID nextObjID ) { m_nextObjID = nextObjID; }
	ObjectID getObjectIDCounter( void ) { return m_nextObjID; }

	//-----------------------------------------------------------------------------------------------
	void setBuildableStatusOverride(const ThingTemplate* tt, BuildableStatus bs);
	Bool findBuildableStatusOverride(const ThingTemplate* tt, BuildableStatus& bs) const;

	void setControlBarOverride(const AsciiString& commandSetName, Int slot, ConstCommandButtonPtr commandButton);
	Bool findControlBarOverride(const AsciiString& commandSetName, Int slot, ConstCommandButtonPtr& commandButton) const;

	//-----------------------------------------------------------------------------------------------
	/// create an object given the thing template. (Only for use by ThingFactory.)
	Object *friend_createObject( const ThingTemplate *thing, const ObjectStatusMaskType &objectStatusMask, Team *team );
	void destroyObject( Object *obj );							///< Mark object as destroyed for later deletion
	Object *placeBeacon( Player *player, Coord3D *pos, Bool *tooMany );	///< beacon of the player with the notice for allies (nullptr: none placed)
	Object *findObjectByID( ObjectID id LOGIC_ACCESS_SITE_DEFAULT );		///< Given an ObjectID, return a pointer to the object.

	// Handles instead of raw pointers for references that outlive a tick; see GameLogic/ObjectHandle.h.
	ObjectHandle makeObjectHandle( const Object *obj ) const;	///< handle to a living object (empty handle for NULL)
	Object *resolveObjectHandle( const ObjectHandle& handle );	///< object for the handle, or NULL if it no longer lives
	ObjectGeneration getObjectGeneration( ObjectID id ) const;	///< generation of the slot (0 = never used), for tests and diagnosis only
 	Object *getFirstObject( void );									///< Returns the "first" object in the world. When used with the object method "getNextObject()", all objects in the world can be iterated.
	ObjectID allocateObjectID( void );							///< Returns a new unique object id

	// super hack
	void startNewGame( Bool loadSaveGame );
	Bool loadMapINI( AsciiString mapName );			///< FALSE if map.ini or solo.ini cannot be read

	void updateLoadProgress( Int progress );
	void deleteLoadScreen( void );
	
	//Kris: Cut setGameLoading() and replaced with setLoadingMap() and setLoadingSave() -- reason: nomenclature
	//void setGameLoading( Bool loading ) { m_loadingScene = loading; }
	void setLoadingMap( Bool loading ) { m_loadingMap = loading; }
	void setLoadingSave( Bool loading ) { m_loadingSave = loading; }
	void setClearingGameData( Bool clearing ) { m_clearingGameData = clearing; }
	
	void setGameMode( Int mode );
	Int getGameMode( void );
	Bool isInGame( void );
	Bool isInLanGame( void );
	Bool isInSinglePlayerGame( void );
	Bool isInSkirmishGame( void );
	Bool isInReplayGame( void );
	Bool isInInternetGame( void );
	Bool isInShellGame( void );
	Bool isInMultiplayerGame( void );

	//Kris: Cut isLoadingGame() and replaced with isLoadingMap() and isLoadingSave() -- reason: nomenclature
	//Bool isLoadingGame() const { return m_loadingScene; }		// This is the old function that isn't very clear on it's definition.
	Bool isLoadingMap() const { return m_loadingMap; }			// Whenever a map is in the process of loading.
	Bool isLoadingSave() const { return m_loadingSave; }		// Whenever a saved game is in the process of loading.
	Bool isClearingGameData() const { return m_clearingGameData; }

	void enableScoring(Bool score) { m_isScoringEnabled = score; }
	Bool isScoringEnabled() const { return m_isScoringEnabled; }

	void setShowBehindBuildingMarkers(Bool b) { m_showBehindBuildingMarkers = b; }
	Bool getShowBehindBuildingMarkers() const { return m_showBehindBuildingMarkers; }

	void setDrawIconUI(Bool b) { m_drawIconUI = b; }
	Bool getDrawIconUI() const { return m_drawIconUI; }

	void setShowDynamicLOD(Bool b) { m_showDynamicLOD = b; }
	Bool getShowDynamicLOD() const { return m_showDynamicLOD; }

	void setHulkMaxLifetimeOverride(Int b) { m_scriptHulkMaxLifetimeOverride = b; }
	Int getHulkMaxLifetimeOverride() const { return m_scriptHulkMaxLifetimeOverride; }

	Bool isIntroMoviePlaying();

	void updateObjectsChangedTriggerAreas(void) {m_frameObjectsChangedTriggerAreas = m_frame;}
	UnsignedInt getFrameObjectsChangedTriggerAreas(void) {return m_frameObjectsChangedTriggerAreas;}

	void clearGameData(Bool showScoreScreen = TRUE);														///< Clear the game data
	void closeWindows( void );

	void sendObjectCreated( Object *obj );
	void sendObjectDestroyed( Object *obj );

	void bindObjectAndDrawable(Object* obj, Drawable* draw);

	void setGamePaused( Bool paused, Bool pauseMusic = TRUE );
	Bool isGamePaused( void );
	Bool getInputEnabledMemory( void ) { return m_inputEnabledMemory; }

	void processProgress(Int playerId, Int percentage);
	void processProgressComplete(Int playerId);
	Bool isProgressComplete( void );
	void timeOutGameStart( void );
	void initTimeOutValues( void );
	UnsignedInt getObjectCount( void );

	Int getRankLevelLimit() const { return m_rankLevelLimit; }
	void setRankLevelLimit(Int limit) 
	{ 
		if (limit < 1) limit = 1;
		m_rankLevelLimit = limit; 
	}
	
	// We need to allow access to this, because on a restartGame, we need to restart with the settings we started with
	Int getRankPointsToAddAtGameStart() const { return m_rankPointsToAddAtGameStart; }

  UnsignedShort getSuperweaponRestriction( void ) const; ///< Get any optional limits on superweapons
  void setSuperweaponRestriction( void );

	/// Start protection: until this frame nobody may harm or attack an enemy (0 = none).
	UnsignedInt getGraceEndFrame( void ) const { return m_graceEndFrame; }
	Bool isGracePeriod( void ) const { return m_frame < m_graceEndFrame; }
	void friend_applyLoadedRandomState( void );		///< end of loading a save game: the saved random state
	/// During the start protection: does the victim belong to an enemy of the source (the source object's
	/// controlling player, else any player in sourceMask)?
	Bool isGraceProtected( const Object *victim, const Object *source, PlayerMaskType sourceMask ) const;

#ifdef DUMP_PERF_STATS
	void getAIMetricsStatistics( UnsignedInt *numAI, UnsignedInt *numMoving, UnsignedInt *numAttacking, UnsignedInt *numWaitingForPath, UnsignedInt *overallFailedPathfinds );
	void resetOverallFailedPathfinds() { m_overallFailedPathfinds = 0; }
	void incrementOverallFailedPathfinds() { m_overallFailedPathfinds++; }
	UnsignedInt getOverallFailedPathfinds() const { return m_overallFailedPathfinds; }
#endif
	
	// NOTE: selectObject and deselectObject should be called *only* by logical things, NEVER by the
	// client. These will cause the client to select or deselect the object, if affectClient is true.
	// If createToSelection is TRUE, this object causes a new group to be selected.
	void selectObject(Object *obj, Bool createNewSelection, PlayerMaskType playerMask, Bool affectClient = FALSE);
	void deselectObject(Object *obj, PlayerMaskType playerMask, Bool affectClient = FALSE);

	// this should be called only by UpdateModule, thanks.
	void friend_awakenUpdateModule(Object* obj, UpdateModulePtr update, UnsignedInt whenToWakeUp);

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer ) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess( void ) override;

private:

	void pushSleepyUpdate(UpdateModulePtr u);
	UpdateModulePtr peekSleepyUpdate() const;
	void popSleepyUpdate();
	void eraseSleepyUpdate(Int i);
	void rebalanceSleepyUpdate(Int i);
	Int rebalanceParentSleepyUpdate(Int i);
	Int rebalanceChildSleepyUpdate(Int i);
	void remakeSleepyUpdate();
	void validateSleepyUpdate() const;

private:

	/**
		overrides to thing template buildable status. doesn't really belong here,
		but has to go somewhere. (srj)
	*/
	using BuildableMap = std::unordered_map< AsciiString, BuildableStatus, rts::hash<AsciiString>, rts::equal_to<AsciiString> >;
	BuildableMap m_thingTemplateBuildableOverrides;

	/**
		overrides to control bars. doesn't really belong here, but has to go somewhere. (srj)
	*/
	using ControlBarOverrideMap = std::unordered_map< AsciiString, ConstCommandButtonPtr, rts::hash<AsciiString>, rts::equal_to<AsciiString> >;
	ControlBarOverrideMap m_controlBarOverrides;

	Real m_width, m_height;																	///< Dimensions of the world
	UnsignedInt m_frame;																		///< Simulation frame number
	
	// CRC cache system -----------------------------------------------------------------------------
	UnsignedInt	m_CRC;																			///< Cache of previous CRC value
	std::map<Int, UnsignedInt> m_cachedCRCs;								///< CRCs we've seen this frame
	Bool m_shouldValidateCRCs;															///< Should we validate CRCs this frame?
	//-----------------------------------------------------------------------------------------------

	//Added By Sadullah Nader
	//Used to for load scene
	//Bool m_loadingScene;
	Bool m_loadingMap;
	Bool m_loadingSave;
	Bool m_clearingGameData;

	Bool m_isInUpdate;

	Int m_rankPointsToAddAtGameStart;

	Bool m_isScoringEnabled;
	Bool m_showBehindBuildingMarkers;	//used by designers to override the user setting for cinematics
	Bool m_drawIconUI;
	Bool m_showDynamicLOD;	//used by designers to override the user setting for cinematics
	Int m_scriptHulkMaxLifetimeOverride;	///< Scripts can change the lifetime of a hulk -- defaults to off (-1) in frames.

	/// @todo remove this hack
	Bool m_startNewGame;
	WindowLayout *m_background;

	Object* m_objList;																			///< All of the objects in the world.
//	ObjectPtrHash m_objHash;																///< Used for ObjectID lookups
	ObjectPtrVector m_objVector;
	// Generation per slot in m_objVector. It increases on every allocation and every release, so a
	// handle to a dead object stays detectable even if its ID is reused. The mechanism lives in
	// Engine/GenerationTable.h and is tested there (EngineTests).
	Engine::GenerationTable m_objGeneration;

	// this is a vector, but is maintained as a priority queue.
	// never modify it directly; please use the proper access methods.
	// (for an excellent discussion of priority queues, please see:
	// http://dogma.net/markn/articles/pq_stl/priority.htm)
	std::vector<UpdateModulePtr> m_sleepyUpdates;
	ObjectID m_xferNextObjID;																	///< next object ID from a save game (loading creates objects)
	std::vector< std::pair<ObjectID, Int> > m_xferSleepyOrder;	///< heap order from a save game: object and update module index
	
	UpdateModulePtr					 m_curUpdateModule;

	ObjectPointerList m_objectsToDestroy;										///< List of things that need to be destroyed at end of frame

	ObjectID m_nextObjID;																		///< For allocating object id's

	void setDefaults( Bool loadSaveGame );									///< Set default values of class object
	void processDestroyList( void );												///< Destroy all pending objects on the destroy list

	void destroyAllObjectsImmediate();											///< destroy, and process destroy list immediately

	/// factory for TheTerrainLogic, called from init()
	virtual TerrainLogic *createTerrainLogic( void );
	virtual GhostObjectManager *createGhostObjectManager(void);

	Int m_gameMode;
	Int m_rankLevelLimit;
  UnsignedShort m_superweaponRestriction;
	UnsignedInt m_graceEndFrame;
	UnsignedInt m_xferRandomState[7];						///< random state read from a save game
	Bool m_xferRandomStatePending;

	LoadScreen *getLoadScreen( Bool loadSaveGame );
	std::unique_ptr<LoadScreen> m_loadScreen;
	Bool m_gamePaused;
	Bool m_inputEnabledMemory;// Latches used to remember what to restore to after we unpause
	Bool m_mouseVisibleMemory;

	Bool m_progressComplete[MAX_SLOTS];
	enum { PROGRESS_COMPLETE_TIMEOUT = 60000 };							///< Timeout we wait for when we've completed our Load
	Int m_progressCompleteTimeout[MAX_SLOTS];
	void testTimeOut( void );
	void lastHeardFrom( Int playerId );
	Bool m_startFailed;									///< startNewGame gave up; clearGameData tells the player
	void abortNewGame( void );
	Bool m_forceGameStartByTimeOut;													///< If we timeout someone we're waiting to load, set this flag to start the game

#ifdef DUMP_PERF_STATS
	UnsignedInt m_overallFailedPathfinds;
#endif

	UnsignedInt m_frameObjectsChangedTriggerAreas;					///< Last frame objects moved into/outof trigger areas, or were created/destroyed. jba.

	// ----------------------------------------------------------------------------------------------
	struct ObjectTOCEntry
	{
		AsciiString name;
		UnsignedShort id;
	};
	using ObjectTOCList = std::list< ObjectTOCEntry >;
	using ObjectTOCListIterator = ObjectTOCList::iterator;
	ObjectTOCList m_objectTOC;															///< the object TOC
	void addTOCEntry( AsciiString name, UnsignedShort id ); ///< add a new name/id TOC pair
	ObjectTOCEntry *findTOCEntryByName( AsciiString name );	///< find ObjectTOC by name
	ObjectTOCEntry *findTOCEntryById( UnsignedShort id );		///< find ObjectTOC by id
	void xferObjectTOC( Xfer *xfer );												///< save/load object TOC for current state of map
	void prepareLogicForObjectLoad( void );									///< prepare engine for object data from game file

	/// The subsystems the logic creates (init; the stats collector per game) and frees (destructor,
	/// reset); the global names (ThePartitionManager, TheScriptEngine, ...) point to them. The
	/// destructor frees them one by one in the order they need each other, so the order of these
	/// members does not matter.
	struct OwnedSubsystems
	{
		std::unique_ptr<PartitionManager> partitionManager;
		std::unique_ptr<GhostObjectManager> ghostObjectManager;
		std::unique_ptr<TerrainLogic> terrainLogic;
		std::unique_ptr<ScriptActionsInterface> scriptActions;
		std::unique_ptr<ScriptConditionsInterface> scriptConditions;
		std::unique_ptr<ScriptEngine> scriptEngine;
		std::unique_ptr<StatsCollector> statsCollector;
	};
	OwnedSubsystems m_own;
};

// INLINE /////////////////////////////////////////////////////////////////////////////////////////
inline void GameLogic::setWidth( Real width ) { m_width = width; }
inline Real GameLogic::getWidth( void ) { return m_width; }
inline void GameLogic::setHeight( Real height ) { m_height = height; }
inline Real GameLogic::getHeight( void ) { return m_height; }
inline UnsignedInt GameLogic::getFrame( void ) { return m_frame; }

inline Bool GameLogic::isInGame( void ) { return !(m_gameMode == GAME_NONE); }
inline void GameLogic::setGameMode( Int mode ) { m_gameMode = mode; }
inline Int  GameLogic::getGameMode( void ) { return m_gameMode; }
inline Bool GameLogic::isInLanGame( void ) { return (m_gameMode == GAME_LAN); }
inline Bool GameLogic::isInSkirmishGame( void ) { return (m_gameMode == GAME_SKIRMISH); }
inline Bool GameLogic::isInMultiplayerGame( void ) { return ((m_gameMode == GAME_LAN) || (m_gameMode == GAME_INTERNET)) ; }
inline Bool GameLogic::isInReplayGame( void ) { return (m_gameMode == GAME_REPLAY); }
inline Bool GameLogic::isInInternetGame( void ) { return (m_gameMode == GAME_INTERNET); }
inline Bool GameLogic::isInShellGame( void ) { return (m_gameMode == GAME_SHELL); }
inline UnsignedShort GameLogic::getSuperweaponRestriction() const { return m_superweaponRestriction; }

inline Object* GameLogic::findObjectByID( ObjectID id LOGIC_ACCESS_SITE )
{
	ASSERT_LOGIC_ACCESS_AT_SITE("GameLogic");
	if( id == INVALID_ID )
		return nullptr;

//	ObjectPtrHash::iterator it = m_objHash.find(id);
//	if (it == m_objHash.end())
//		return NULL;
//	
//	return (*it).second;
// unsigned compare, as in removeObjectFromLookupTable (C4018)
	if( static_cast<UnsignedInt>(id) < m_objVector.size() )
		return m_objVector[static_cast<UnsignedInt>(id)];

	return nullptr;
}



// the singleton
#include "Engine/EngineContext.h"

#endif // _GAME_LOGIC_H_

