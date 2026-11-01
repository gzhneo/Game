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

// Script state: counters, flags, timers, attack priorities, object type lists, named map reveals

/// Local classes 
/// AttackPriorityInfo class

static const Int ATTACK_PRIORITY_DEFAULT = 1;
//-------------------------------------------------------------------------------------------------
/** Ctor */
//-------------------------------------------------------------------------------------------------
AttackPriorityInfo::AttackPriorityInfo() :m_defaultPriority(ATTACK_PRIORITY_DEFAULT), m_priorityMap(nullptr) 
{
	m_name.clear();
}

//-------------------------------------------------------------------------------------------------
/** dtor */
//-------------------------------------------------------------------------------------------------
AttackPriorityInfo::~AttackPriorityInfo()
{
	m_priorityMap.reset();
	m_priorityMap = nullptr;
}

//-------------------------------------------------------------------------------------------------
/** set a priority for a thing template. */
//-------------------------------------------------------------------------------------------------
void AttackPriorityInfo::setPriority(const ThingTemplate *tThing, Int priority)
{
	if (tThing==nullptr) return;
	if (m_priorityMap==nullptr) {
		m_priorityMap = std::make_unique<AttackPriorityMap>();	// STL type, so impractical to use memorypool
	}
	tThing = static_cast<const ThingTemplate *>(tThing->getFinalOverride());
	Int &thePriority = (*m_priorityMap)[tThing];
	thePriority = priority;
}

/** set a priority for a thing template. */
//-------------------------------------------------------------------------------------------------
Int AttackPriorityInfo::getPriority(const ThingTemplate *tThing) const
{
	Int priority = m_defaultPriority;
	if (tThing==nullptr) return priority;
	tThing = static_cast<const ThingTemplate *>(tThing->getFinalOverride());
	if (m_priorityMap && !m_priorityMap->empty()) {
		AttackPriorityMap::const_iterator it = m_priorityMap->find(tThing);
		if (it != m_priorityMap->end()) 
		{
			priority = (*it).second;
		}
	}
	return priority;
}

#ifdef _DEBUG
/** Dump the info. */
//-------------------------------------------------------------------------------------------------
void AttackPriorityInfo::dumpPriorityInfo(void)
{
#ifdef DEBUG_LOGGING
	DEBUG_LOG(("Attack priority '%s', default %d\n", m_name.str(), m_defaultPriority));
	if (m_priorityMap==nullptr) return;
	for (AttackPriorityMap::const_iterator it = m_priorityMap->begin(); it != m_priorityMap->end(); ++it) 
    {
		const ThingTemplate *tThing = (*it).first;
		Int priority = (*it).second;
		DEBUG_LOG(("  Thing '%s' priority %d\n",tThing->getName().str(), priority));
	}
#endif
}
#endif

// ------------------------------------------------------------------------------------------------
/** Reset to default state */
// ------------------------------------------------------------------------------------------------
void AttackPriorityInfo::reset( void )
{

	// clear name just to be clean
	m_name.clear();

	// go back to default priority
	m_defaultPriority = ATTACK_PRIORITY_DEFAULT;

	// delete the priority map if present
	if( m_priorityMap )
	{

		m_priorityMap.reset();
		m_priorityMap = nullptr;

	}  // end if

}  // end reset

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AttackPriorityInfo::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void AttackPriorityInfo::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// name
	xfer->xferAsciiString( &m_name );

	xfer->xferInt( &m_defaultPriority );

	//
	// priority map count, note there is question to the following code that is
	// commented out in that we're not sure it returns the correct number of elements
	// in the map.  To be safe, we're getting the count by manually counting the
	// elements ourselves as we iterate through them
	//
	// Please KEEP the below code here for reference
	// UnsignedShort priorityMapCount = m_priorityMap ? m_priorityMap->size() : 0;
	//
	UnsignedShort priorityMapCount = 0;
	if( m_priorityMap )
	{
		AttackPriorityMap::const_iterator it;
		
		for( it = m_priorityMap->begin(); it != m_priorityMap->end(); ++it )
			++priorityMapCount;
	
	}  // end if
	xfer->xferUnsignedShort( &priorityMapCount );

	// priority map
	AsciiString thingTemplateName;
	const ThingTemplate *thingTemplate;
	Int priority;
	if( xfer->getXferMode() == XFER_SAVE )
	{

		if( m_priorityMap )
		{

			// iterate all the entries
			AttackPriorityMap::const_iterator it;
			UnsignedShort count = 0;
			for( it = m_priorityMap->begin(); it != m_priorityMap->end(); ++it )
			{

				// keep a count for sanity
				++count;

				// write thing template name
				thingTemplate = (*it).first;
				thingTemplateName = thingTemplate->getName();
				DEBUG_ASSERTCRASH( thingTemplateName.isEmpty() == FALSE, 
													 ("AttackPriorityInfo::xfer - Writing an empty thing template name\n") );
				xfer->xferAsciiString( &thingTemplateName );

				// write priority
				priority = (*it).second;
				xfer->xferInt( &priority );

			}  // end for i

			// sanity
			DEBUG_ASSERTCRASH( count == priorityMapCount, 
												("AttackPriorityInfo::xfer - Mismatch in priority map size.  Size() method returned '%d' but actual iteration count was '%d'\n",
												 priorityMapCount, count) );

		}  // end if

	}  // end if, save
	else
	{

		// read all entries
		for( UnsignedShort i = 0; i < priorityMapCount; ++i )
		{

			// read thing template name, and get template
			xfer->xferAsciiString( &thingTemplateName );
			thingTemplate = TheThingFactory->findTemplate( thingTemplateName );
			if( thingTemplate == nullptr )
			{

				DEBUG_CRASH(( "AttackPriorityInfo::xfer - Unable to find thing template '%s'\n",
											thingTemplateName.str() ));
				throw SaveCode::SC_INVALID_DATA;

			}  // end if

			// read priority
			xfer->xferInt( &priority );

			// set priority (this will allocate the map on the first call as well)
			setPriority( thingTemplate, priority );

		}  // end for

	}  // end else, load

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AttackPriorityInfo::loadPostProcess( void )
{

}  // end loadPostProcess

//-------------------------------------------------------------------------------------------------
/** clearFlag */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::clearFlag(const AsciiString &name)
{
	Int j;
	for (j=0; j<MAX_PLAYER_COUNT; j++) {
		AsciiString modName;
		modName.format("%s%d", name.str(), j);
		// Note - flags start at 1.  0 means not assigned.
		Int i;
		for (i=1; i<m_numFlags; i++) {
			if ((modName==m_flags[i].name)) {
				m_flags[i].value = FALSE;
			}
		}
	}
}  // end clearFlag

//-------------------------------------------------------------------------------------------------
/** clearTeamFlags */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::clearTeamFlags(void)
{
	clearFlag("USA Team is Building");
	clearFlag("USA Air Team Is Building");
	clearFlag("USA Inf Team Is Building");
	clearFlag("China Team is Building");
	clearFlag("China Air Team Is Building");
	clearFlag("China Inf Team Is Building");
	clearFlag("GLA Team is Building");
	clearFlag("GLA Inf Team is Building");

}  // end clearTeamFlags

//-------------------------------------------------------------------------------------------------
/** getObjectTypeList */
//-------------------------------------------------------------------------------------------------
ObjectTypes *ScriptEngine::getObjectTypes(const AsciiString& objectTypeList)
{
	AllObjectTypesIt it;

	for (it = m_allObjectTypeLists.begin(); it != m_allObjectTypeLists.end(); ++it) {
		if ((*it) == nullptr) {
			DEBUG_CRASH(("NULL object type list was unexpected. jkmcd"));
			continue;
		}

		if ((*it)->getListName() == objectTypeList) {
			return (*it);
		}
	}

	return nullptr;
}

//-------------------------------------------------------------------------------------------------
/** doObjectTypeListMaintenance */
/** If addObject is false, remove the object. If it is true, add the object. */
/** If the object removed is the last object, then the list is removed as well. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::doObjectTypeListMaintenance(const AsciiString& objectTypeList, const AsciiString& objectType, Bool addObject)
{
	ObjectTypes *currentObjectTypeVec = getObjectTypes(objectTypeList);
	
	if (!currentObjectTypeVec) {
		ObjectTypes *newVec = newInstance(ObjectTypes)(objectTypeList);
		m_allObjectTypeLists.push_back(newVec);
		currentObjectTypeVec = newVec;
	}

	if (addObject) {
		currentObjectTypeVec->addObjectType(objectType);	
	} else {
		currentObjectTypeVec->removeObjectType(objectType);
	}

	// Remove it. Its dead Jim.
	if (currentObjectTypeVec->getListSize() == 0) {
		removeObjectTypes(currentObjectTypeVec);
		
		// Semantic emphasis
		currentObjectTypeVec = nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
/** Allocates a counter, if this name doesn't exist. */
//-------------------------------------------------------------------------------------------------
Int ScriptEngine::allocateCounter( const AsciiString& name)
{
	Int i;
	// Note - counters start at 1.  0 means not assigned.
	for (i=1; i<m_numCounters; i++) {
		if (name==m_counters[i].name) {
			return i;
		}
	}
	DEBUG_ASSERTCRASH(m_numCounters<MAX_COUNTERS, ("Too many counters, failed to make '%s'.\n", name.str()));
	if (m_numCounters < MAX_COUNTERS) {
		m_counters[m_numCounters].name = name;
		i = m_numCounters;
		m_numCounters++;
		return(i);
	}
	return 0; // Shouldn't ever happen.
}

//-------------------------------------------------------------------------------------------------
/** Gets a counter */
//-------------------------------------------------------------------------------------------------
const TCounter *ScriptEngine::getCounter(const AsciiString& counterName)
{
	Int i;
	for (i=1; i<m_numCounters; i++)
	{
		if (counterName == m_counters[i].name)
		{
			return &(m_counters[i]);
		}
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
void ScriptEngine::createNamedMapReveal(const AsciiString& revealName, const AsciiString& waypointName, Real radiusToReveal, const AsciiString& playerName)
{
	VecNamedRevealIt it;

	// Will fail if there's already one in existence of the same name.
	for (it = m_namedReveals.begin(); it != m_namedReveals.end(); ++it) {
		if (it->m_revealName == revealName) {
			DEBUG_CRASH(("ScriptEngine::createNamedMapReveal: Attempted to redefine named Reveal '%s', so I won't change it.\n", revealName.str()));
			return;
		}
	}

	NamedReveal reveal;
	reveal.m_playerName = playerName;
	reveal.m_radiusToReveal = radiusToReveal;
	reveal.m_revealName = revealName;
	reveal.m_waypointName = waypointName;

	m_namedReveals.push_back(reveal);
}

//-------------------------------------------------------------------------------------------------
void ScriptEngine::doNamedMapReveal(const AsciiString& revealName)
{
	VecNamedRevealIt it;

	NamedReveal *reveal = nullptr;
	for (it = m_namedReveals.begin(); it != m_namedReveals.end(); ++it) {
		if (it->m_revealName == revealName) {
			reveal = &(*it);
			break;
		}
	}

	if (!reveal) {
		return;
	}

	Waypoint *way = TheTerrainLogic->getWaypointByName(reveal->m_waypointName);
	if (!way) {
		return;
	}

	Player *player = getPlayerFromAsciiString(reveal->m_playerName);
	if (!player) {
		return;
	}

	Coord3D pos;
	pos = *way->getLocation();

	ThePartitionManager->doShroudReveal(pos.x, pos.y, reveal->m_radiusToReveal, player->getPlayerMask());
}

//-------------------------------------------------------------------------------------------------
void ScriptEngine::undoNamedMapReveal(const AsciiString& revealName)
{
	VecNamedRevealIt it;

	NamedReveal *reveal = nullptr;
	for (it = m_namedReveals.begin(); it != m_namedReveals.end(); ++it) {
		if (it->m_revealName == revealName) {
			reveal = &(*it);
			break;
		}
	}

	if (!reveal) {
		return;
	}

	Waypoint *way = TheTerrainLogic->getWaypointByName(reveal->m_waypointName);
	if (!way) {
		return;
	}

	Player *player = getPlayerFromAsciiString(reveal->m_playerName);
	if (!player) {
		return;
	}

	Coord3D pos;
	pos = *way->getLocation();

	ThePartitionManager->undoShroudReveal(pos.x, pos.y, reveal->m_radiusToReveal, player->getPlayerMask());
}

//-------------------------------------------------------------------------------------------------
void ScriptEngine::removeNamedMapReveal(const AsciiString& revealName)
{
	VecNamedRevealIt it;
	
	for (it = m_namedReveals.begin(); it != m_namedReveals.end(); ++it) {
		if (it->m_revealName == revealName) {
			m_namedReveals.erase(it);
			return;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Allocates a flag, if this name doesn't exist. */
//-------------------------------------------------------------------------------------------------
Int ScriptEngine::allocateFlag( const AsciiString& name)
{
	Int i;
	// Note - flags start at 1.  0 means not assigned.
	for (i=1; i<m_numFlags; i++) {
		if ((name==m_flags[i].name)) {
			return i;
		}
	}
	DEBUG_ASSERTCRASH(m_numFlags < MAX_FLAGS, ("Too many flags, failed to make '%s'..\n", name.str()));
	if (m_numFlags < MAX_FLAGS) {
		m_flags[m_numFlags].name = name;
		i = m_numFlags;
		m_numFlags++;
		return(i);
	}
	return 0; // Shouldn't ever happen.
}

//-------------------------------------------------------------------------------------------------
/** Evaluates a counter condition */
//-------------------------------------------------------------------------------------------------
Bool ScriptEngine::evaluateCounter( Condition *pCondition )
{
	DEBUG_ASSERTCRASH(pCondition->getNumParameters() >= 3, ("Not enough parameters.\n"));
	DEBUG_ASSERTCRASH(pCondition->getConditionType() == Condition::COUNTER, ("Wrong condition.\n"));
	Int counterNdx = pCondition->getParameter(0)->getInt();
	if (counterNdx == 0) {
		counterNdx = allocateCounter(pCondition->getParameter(0)->getString());
		pCondition->getParameter(0)->friend_setInt(counterNdx);
	}
	Int value = pCondition->getParameter(2)->getInt();
	switch (pCondition->getParameter(1)->getInt()) {
		case Parameter::LESS_THAN: return m_counters[counterNdx].value < value;
		case Parameter::LESS_EQUAL: return m_counters[counterNdx].value <= value;
		case Parameter::EQUAL: return m_counters[counterNdx].value == value;
		case Parameter::GREATER_EQUAL: return m_counters[counterNdx].value >= value;
		case Parameter::GREATER: return m_counters[counterNdx].value > value;
		case Parameter::NOT_EQUAL: return m_counters[counterNdx].value != value;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
/** Sets a counter. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::setCounter( ScriptAction *pAction )
{
	DEBUG_ASSERTCRASH(pAction->getNumParameters() >= 2, ("Not enough parameters.\n"));
	Int counterNdx = pAction->getParameter(0)->getInt();
	if (counterNdx == 0) {
		counterNdx = allocateCounter(pAction->getParameter(0)->getString());
		pAction->getParameter(0)->friend_setInt(counterNdx);
	}
	Int value = pAction->getParameter(1)->getInt();
	m_counters[counterNdx].value = value;
}

//-------------------------------------------------------------------------------------------------
/** Adds to a counter. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::addCounter( ScriptAction *pAction )
{
	DEBUG_ASSERTCRASH(pAction->getNumParameters() >= 2, ("Not enough parameters.\n"));
	Int value = pAction->getParameter(0)->getInt();
	Int counterNdx = pAction->getParameter(1)->getInt();
	if (counterNdx == 0) {
		counterNdx = allocateCounter(pAction->getParameter(1)->getString());
		pAction->getParameter(1)->friend_setInt(counterNdx);
	}
	m_counters[counterNdx].value += value;
}

//-------------------------------------------------------------------------------------------------
/** Subtracts from a counter. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::subCounter( ScriptAction *pAction )
{
	DEBUG_ASSERTCRASH(pAction->getNumParameters() >= 2, ("Not enough parameters.\n"));
	Int value = pAction->getParameter(0)->getInt();
	Int counterNdx = pAction->getParameter(1)->getInt();
	if (counterNdx == 0) {
		counterNdx = allocateCounter(pAction->getParameter(1)->getString());
		pAction->getParameter(1)->friend_setInt(counterNdx);
	}
	m_counters[counterNdx].value -= value;
}

//-------------------------------------------------------------------------------------------------
/** Evaluates a flag */
//-------------------------------------------------------------------------------------------------
Bool ScriptEngine::evaluateFlag( Condition *pCondition )
{
	DEBUG_ASSERTCRASH(pCondition->getNumParameters() >= 2, ("Not enough parameters.\n"));
	DEBUG_ASSERTCRASH(pCondition->getConditionType() == Condition::FLAG, ("Wrong condition.\n"));
	Int flagNdx = pCondition->getParameter(0)->getInt();
	if (flagNdx == 0) {
		flagNdx = allocateFlag(pCondition->getParameter(0)->getString());
		pCondition->getParameter(0)->friend_setInt(flagNdx);
	}
	Int value = pCondition->getParameter(1)->getInt();
	Bool boolVal = (value!=0);
	Bool boolFlag = (m_flags[flagNdx].value != 0);
	
	if (boolVal == boolFlag) {
		return true;
	}

	for (ListAsciiStringIt it = m_uiInteractions.begin(); it != m_uiInteractions.end(); ++it) {
		if (it->compare(pCondition->getParameter(0)->getString()) == 0) {
			// just return. This flag will be cleared up at the end of the ScriptEngine::update() call
			return true;
		}
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
/** Sets a flag */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::setFlag( ScriptAction *pAction )
{
	DEBUG_ASSERTCRASH(pAction->getNumParameters() >= 2, ("Not enough parameters.\n"));
	Int flagNdx = pAction->getParameter(0)->getInt();
	if (flagNdx == 0) {
		flagNdx = allocateFlag(pAction->getParameter(0)->getString());
		pAction->getParameter(0)->friend_setInt(flagNdx);
	}
	Bool value = pAction->getParameter(1)->getInt();
	m_flags[flagNdx].value = value;
}

//-------------------------------------------------------------------------------------------------
/** Finds a named attack info.  Note - may return null. */
//-------------------------------------------------------------------------------------------------
AttackPriorityInfo * ScriptEngine::findAttackInfo(const AsciiString& name, Bool addIfNotFound)
{
	// Note - m_attackPriorityInfo[0] is the default info, with an empty name.
	Int i;
	for (i=1; i<m_numAttackInfo; i++) {
		if (m_attackPriorityInfo[i].getName() == name) {
			return &m_attackPriorityInfo[i];
		}
	}
	if (addIfNotFound && m_numAttackInfo<MAX_ATTACK_PRIORITIES) {
		m_attackPriorityInfo[m_numAttackInfo].friend_setName(name);
		m_numAttackInfo++;
		return &m_attackPriorityInfo[m_numAttackInfo-1];
	}
	return nullptr;
}

/// Attack priority stuff.
//-------------------------------------------------------------------------------------------------
/** Returns the default attack priority info.  Never returns null. */
//-------------------------------------------------------------------------------------------------
const AttackPriorityInfo *ScriptEngine::getDefaultAttackInfo(void)
{
	// Note - m_attackPriorityInfo[0] is the default info, with an empty name.
	return &m_attackPriorityInfo[0];
}

//-------------------------------------------------------------------------------------------------
/** Returns the named attack info, if non-existent returns default attack priority info.  
		Never returns null. */
//-------------------------------------------------------------------------------------------------
const AttackPriorityInfo *ScriptEngine::getAttackInfo(const AsciiString& name)
{
	Int i;
	for (i=1; i<m_numAttackInfo; i++) {
		if (m_attackPriorityInfo[i].getName() == name) {
			return &m_attackPriorityInfo[i];
		}
	}
	// Note - m_attackPriorityInfo[0] is the default info, with an empty name.
	return &m_attackPriorityInfo[0];
}

//-------------------------------------------------------------------------------------------------
/** Sets an Attack Priority Set value for a thing */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::setPriorityThing( ScriptAction *pAction )
{
	DEBUG_ASSERTCRASH(pAction->getNumParameters() >= 3, ("Not enough parameters.\n"));
	
	AsciiString typeArgument = pAction->getParameter(1)->getString();

	// Our argument could be an individual type, or a list name.
	const ObjectTypes *types = TheScriptEngine->getObjectTypes(typeArgument);
	if( !types ) 
	{
		// Lookup failed, so it is just a single type

		const ThingTemplate *thingTemplate;
		// get thing template based from map object name
		thingTemplate = TheThingFactory->findTemplate(typeArgument);
		if (thingTemplate==nullptr) {
			AppendDebugMessage("***Attempting to set attack priority on an invalid thing:***", false);
			AppendDebugMessage(pAction->getParameter(0)->getString(), false);
			return;
		}
		AttackPriorityInfo *info = findAttackInfo(pAction->getParameter(0)->getString(), true);
		if (info==nullptr) {
			AppendDebugMessage("***Error allocating attack priority set - fix or raise limit. ***", false);
			return;
		}
		// DEBUG LOGGING
//		AsciiString debug = "Set the priority on ";
//		debug.concat(thingTemplate->getName().str());
//		debug.concat(" to ");
//		char intBuffer[16];
//		itoa(pAction->getParameter(2)->getInt(), intBuffer, 10);
//		debug.concat(intBuffer);
//		AppendDebugMessage(debug, false);
		// END DEBUGGING LOGGING
		info->setPriority(thingTemplate, pAction->getParameter(2)->getInt());

		return;
	}
	else 
	{
		// Found a list by this name, so we have a bunch of things

		for( Int typeIndex = 0; static_cast<UnsignedInt>(typeIndex) < types->getListSize(); typeIndex ++ )
		{
			AsciiString thisTypeName = types->getNthInList(typeIndex);
			const ThingTemplate *thisType = TheThingFactory->findTemplate(thisTypeName);
			if (thisType==nullptr) {
				AppendDebugMessage("***Attempting to set attack priority on an invalid thing:***", false);
				AppendDebugMessage(pAction->getParameter(0)->getString(), false);
				return;
			}
			AttackPriorityInfo *info = findAttackInfo(pAction->getParameter(0)->getString(), true);
			if (info==nullptr) {
				AppendDebugMessage("***Error allocating attack priority set - fix or raise limit. ***", false);
				return;
			}
			// DEBUG LOGGING
//			AsciiString debug = "Set the priority on ";
//			debug.concat(thisType->getName().str());
//			debug.concat(" to ");
//			char intBuffer[16];
//			itoa(pAction->getParameter(2)->getInt(), intBuffer, 10);
//			debug.concat(intBuffer);
//			AppendDebugMessage(debug, false);
			// END DEBUGGING LOGGING
			info->setPriority(thisType, pAction->getParameter(2)->getInt());
		}

		return;
	}

}

//-------------------------------------------------------------------------------------------------
/** Sets an Attack Priority Set value for all things of a particular kind. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::setPriorityKind( ScriptAction *pAction )
{
	DEBUG_ASSERTCRASH(pAction->getNumParameters() >= 3, ("Not enough parameters.\n"));
	AttackPriorityInfo *info = findAttackInfo(pAction->getParameter(0)->getString(), true);
	if (info==nullptr) {
		AppendDebugMessage("***Error allocating attack priority set - fix or raise limit. ***", false);
		return;
	}
	KindOfType kind = static_cast<KindOfType>(pAction->getParameter(1)->getInt());
	Int priority = pAction->getParameter(2)->getInt();
	const ThingTemplate *tTemplate;
	for( tTemplate = TheThingFactory->firstTemplate();
			 tTemplate;
			 tTemplate = tTemplate->friend_getNextTemplate() )
	{
				 if (tTemplate->isKindOf(kind)) {
					 info->setPriority(tTemplate, priority);
				 }
	}
}

//-------------------------------------------------------------------------------------------------
/** Sets an Attack Priority Set default value. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::setPriorityDefault( ScriptAction *pAction )
{
	DEBUG_ASSERTCRASH(pAction->getNumParameters() >= 2, ("Not enough parameters.\n"));
	AttackPriorityInfo *info = findAttackInfo(pAction->getParameter(0)->getString(), true);
	if (info==nullptr) {
		AppendDebugMessage("***Error allocating attack priority set - fix or raise limit. ***", false);
		return;
	}
	info->friend_setDefaultPriority(pAction->getParameter(1)->getInt());
}

//-------------------------------------------------------------------------------------------------
Int ScriptEngine::getObjectCount(Int playerIndex, const AsciiString& objectTypeName) const
{
	if (!ThePlayerList->getNthPlayer(playerIndex)->isPlayerActive()) {
		// Inactive players should always pretend they have 0 objects.
		return 0;
	}

	const ObjectTypeCount &ocm = m_objectCounts[playerIndex];

	ObjectTypeCount::const_iterator it = ocm.find(objectTypeName);
	if (it == ocm.end()) {
		return 0;
	}

	return it->second;
}

//-------------------------------------------------------------------------------------------------
void ScriptEngine::setObjectCount(Int playerIndex, const AsciiString& objectTypeName, Int newCount)
{
	// Don't really need inactive player checks here.
	
	ObjectTypeCount &ocm = m_objectCounts[playerIndex];
	ocm[objectTypeName] = newCount;
}

//-------------------------------------------------------------------------------------------------
/** Removes an object types list from the list owned by the script engine, and then deletes the */
/**	associated item. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::removeObjectTypes(ObjectTypes *typesToRemove)
{
	if (typesToRemove == nullptr) {
		return;
	}

	AllObjectTypesIt it = std::find(m_allObjectTypeLists.begin(), m_allObjectTypeLists.end(), typesToRemove);

	if (it == m_allObjectTypeLists.end()) {
		// Debug crash maybe?
		return;
	}

	// delete it.
	deleteInstanceOf(typesToRemove);

	// remove it from the main array of stuff
	m_allObjectTypeLists.erase(it);
}

//-------------------------------------------------------------------------------------------------
/** Evaluates a timer */
//-------------------------------------------------------------------------------------------------
Bool ScriptEngine::evaluateTimer( Condition *pCondition )
{
	DEBUG_ASSERTCRASH(pCondition->getNumParameters() >= 1, ("Not enough parameters.\n"));
	DEBUG_ASSERTCRASH(pCondition->getConditionType() == Condition::TIMER_EXPIRED, ("Wrong condition.\n"));
	Int counterNdx = pCondition->getParameter(0)->getInt();
	if (counterNdx == 0) {
		counterNdx = allocateCounter(pCondition->getParameter(0)->getString());
		pCondition->getParameter(0)->friend_setInt(counterNdx);
	}
	if (!m_counters[counterNdx].isCountdownTimer) {
		return false; // Timer hasn't been started yet.
	}
	Int value = m_counters[counterNdx].value;
	// Timers decrement down to -1.
	return (value < 1);
}

//-------------------------------------------------------------------------------------------------
/** Starts a timer. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::setTimer( ScriptAction *pAction, Bool millisecondTimer, Bool random )
{
	DEBUG_ASSERTCRASH(pAction->getNumParameters() >= 2, ("Not enough parameters.\n"));
	Int counterNdx = pAction->getParameter(0)->getInt();
	if (counterNdx == 0) {
		counterNdx = allocateCounter(pAction->getParameter(0)->getString());
		pAction->getParameter(0)->friend_setInt(counterNdx);
	}
	if (millisecondTimer) {
		Real value = pAction->getParameter(1)->getReal();
		if (random) {
			Real randomValue = pAction->getParameter(2)->getReal();
			value = static_cast<Real>(GameLogicRandomValue(static_cast<Int>(value), static_cast<Int>(randomValue)));
		}
		m_counters[counterNdx].value = REAL_TO_INT_CEIL(ConvertDurationFromMsecsToFrames(value*1000));
	} else {
		Int value = pAction->getParameter(1)->getInt();
		if (random) {
			Int randomValue = pAction->getParameter(2)->getInt();
			value = GameLogicRandomValue(value, randomValue);
		}
		m_counters[counterNdx].value = value;
	}
	m_counters[counterNdx].isCountdownTimer = true;
}

//-------------------------------------------------------------------------------------------------
/** Stops a timer. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::pauseTimer( ScriptAction *pAction )
{
	DEBUG_ASSERTCRASH(pAction->getNumParameters() >= 1, ("Not enough parameters.\n"));
	Int counterNdx = pAction->getParameter(0)->getInt();
	if (counterNdx == 0) {
		counterNdx = allocateCounter(pAction->getParameter(0)->getString());
		pAction->getParameter(0)->friend_setInt(counterNdx);
	}
	m_counters[counterNdx].isCountdownTimer = false;
}

//-------------------------------------------------------------------------------------------------
/** Restarts a timer. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::restartTimer( ScriptAction *pAction )
{
	DEBUG_ASSERTCRASH(pAction->getNumParameters() >= 1, ("Not enough parameters.\n"));
	Int counterNdx = pAction->getParameter(0)->getInt();
	if (counterNdx == 0) {
		counterNdx = allocateCounter(pAction->getParameter(0)->getString());
		pAction->getParameter(0)->friend_setInt(counterNdx);
	}
	if (m_counters[counterNdx].value > 0) {
		m_counters[counterNdx].isCountdownTimer = true;
	}
}

//-------------------------------------------------------------------------------------------------
/** adjusts a timer. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::adjustTimer( ScriptAction *pAction, Bool millisecondTimer, Bool add)
{
	DEBUG_ASSERTCRASH(pAction->getNumParameters() >= 2, ("Not enough parameters.\n"));
	Int counterNdx = pAction->getParameter(1)->getInt();
	if (counterNdx == 0) {
		counterNdx = allocateCounter(pAction->getParameter(1)->getString());
		pAction->getParameter(1)->friend_setInt(counterNdx);
	}
	if (millisecondTimer) {
		Real value = pAction->getParameter(0)->getReal();
		if (!add)
			value = -value;
		m_counters[counterNdx].value += REAL_TO_INT_CEIL(ConvertDurationFromMsecsToFrames(value*1000));
	} else {
		Int value = pAction->getParameter(0)->getInt();
		if (!add)
			value = -value;
		m_counters[counterNdx].value += value;
	}
}
