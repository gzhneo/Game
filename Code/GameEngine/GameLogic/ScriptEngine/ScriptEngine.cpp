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

// FILE: ScriptEngine.cpp /////////////////////////////////////////////////////////////////////////
// The game scripting engine.  Interprets scripts.
// Author: John Ahlquist, Nov. 2001
///////////////////////////////////////////////////////////////////////////////////////////////////
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



enum { K_SCRIPTS_DATA_VERSION_1 = 1 };
#define NONE_STRING "<none>"

static const Int FRAMES_TO_SHOW_WIN_LOSE_MESSAGE = 120;


//------------------------------------------------------------------------------ Performance Timers 
//#include "Common/PerfMetrics.h"
//#include "Common/PerfTimer.h"

// GLOBALS ////////////////////////////////////////////////////////////////////////////////////////



//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ScriptEngine::ScriptEngine():
m_numCounters(0),
m_numFlags(0),
m_callingTeam(nullptr),
m_callingObject(nullptr),
m_conditionTeam(nullptr),
m_conditionObject(nullptr),
m_currentPlayer(nullptr),
m_skirmishHumanPlayer(nullptr),
m_fade(FADE_NONE),
m_freezeByScript(FALSE),
m_frameObjectCountChanged(0),
//Added By Sadullah Nader
//Initializations inserted
m_closeWindowTimer(0),
m_curFadeFrame(0),
m_curFadeValue(0.0f),
m_endGameTimer(0),
m_fadeFramesDecrease(0),
m_fadeFramesHold(0),
m_fadeFramesIncrease(0),
m_firstUpdate(TRUE),
m_maxFade(0.0f),
m_minFade(0.0f),
m_numAttackInfo(0),
m_shownMPLocalDefeatWindow(FALSE),
m_objectsShouldReceiveDifficultyBonus(TRUE),
m_ChooseVictimAlwaysUsesNormal(false)
//
{
	st_CanAppCont = true;
	st_LastCurrentFrame = st_CurrentFrame = 0;
	// By default, difficulty should be normal.
	setGlobalDifficulty(DIFFICULTY_NORMAL);

}  // end ScriptEngine

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ScriptEngine::~ScriptEngine()
{
	if (st_DebugDLL) {
		FARPROC proc = GetProcAddress(st_DebugDLL, "DestroyDebugDialog");
		if (proc) {
			proc();
		}

		FreeLibrary(st_DebugDLL);
		st_DebugDLL = nullptr;
	}


	reset(); // just in case.
#ifdef COUNT_SCRIPT_USAGE
	Int i;
	for (i=0; i<ScriptAction::NUM_ITEMS; i++) {
		DEBUG_LOG(("ScriptAction%d, %4d, %s, %s, %s\n", i, m_actionTemplates[i].m_numTimesUsed, 
			m_actionTemplates[i].m_internalName.str(), m_actionTemplates[i].m_firstMapUsed.str(),
			m_actionTemplates[i].m_uiName.str()));
	}
	for (i=0; i<Condition::NUM_ITEMS; i++) {
		DEBUG_LOG(("ScriptCondition%d, %4d, %s, %s, %s\n", i, m_conditionTemplates[i].m_numTimesUsed, 
			m_conditionTemplates[i].m_internalName.str(), m_conditionTemplates[i].m_firstMapUsed.str(),
			m_conditionTemplates[i].m_uiName.str()));
	}
#endif

}  // end ~ScriptEngine

//-------------------------------------------------------------------------------------------------
/** Reset */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::reset( void )
{
	// setting FPS limit in case a script had changed it
	if (TheGameEngine && TheGlobalData)
		TheGameEngine->setFramesPerSecondLimit(TheGlobalData->m_framesPerSecondLimit);

	if (TheScriptActions) {
		TheScriptActions->reset();	 
	}
	if (TheScriptConditions) {
		TheScriptConditions->reset();
	}
	m_numCounters = 1;
	m_numAttackInfo = 1;
	m_numFlags = 1;
	m_endGameTimer = -1;
	m_closeWindowTimer = -1;

	m_callingTeam = nullptr;
	m_callingObject = nullptr;
	m_conditionTeam = nullptr;
	m_conditionObject = nullptr;
	m_currentPlayer = nullptr;
	m_skirmishHumanPlayer = nullptr;
	m_frameObjectCountChanged = 0;

	m_shownMPLocalDefeatWindow = FALSE;

	Int i;
	for (i=0; i<MAX_COUNTERS; i++) {
		m_counters[i].value = 0;
		m_counters[i].isCountdownTimer = false;
		m_counters[i].name.clear();
	}
	for (i=0; i<MAX_FLAGS; i++) {
		m_flags[i].value = false;
		m_flags[i].name.clear();
	}

	m_breezeInfo.m_direction = PI/3;
	m_breezeInfo.m_directionVec.x = Sin(m_breezeInfo.m_direction);
	m_breezeInfo.m_directionVec.y = Cos(m_breezeInfo.m_direction);
	m_breezeInfo.m_intensity = 0.07f*PI/4;
	m_breezeInfo.m_lean = 0.07f*PI/4;
	m_breezeInfo.m_breezePeriod = LOGICFRAMES_PER_SECOND * 5;
	m_breezeInfo.m_randomness = 0.2f;
	m_breezeInfo.m_breezeVersion = 0;

	m_freezeByScript = FALSE;
	m_objectsShouldReceiveDifficultyBonus = TRUE;
	m_ChooseVictimAlwaysUsesNormal = false;

#ifdef SPECIAL_SCRIPT_PROFILING
#ifdef DEBUG_LOGGING
	if (m_numFrames > 1) {
		DEBUG_LOG(("\n***SCRIPT ENGINE STATS %.0f frames:\n", m_numFrames));
		DEBUG_LOG(("Avg time to update %.3f milisec\n", 1000*m_totalUpdateTime/m_numFrames));
		DEBUG_LOG(("  Max time to update %.3f miliseconds.\n", m_maxUpdateTime*1000));
	}
	m_numFrames=0;
	m_totalUpdateTime=0;
	m_maxUpdateTime=0;

	Int numToDump;
	if (TheSidesList) {
		for (numToDump=0; numToDump<10; numToDump++) {
			Real maxTime = 0;
			Script *maxScript = nullptr;
			/* Run through scripts & set condition team names. */
			for (i=0; i<TheSidesList->getNumSides(); i++) {
				ScriptList *pSL = TheSidesList->getSideInfo(i)->getScriptList();
				if (!pSL) continue;
				if (pSL == nullptr) continue;
				Script *pScr;
				for (pScr = pSL->getScript(); pScr; pScr=pScr->getNext()) {
					if (pScr->getConditionTime()>maxTime) {
						maxTime = pScr->getConditionTime();
						maxScript = pScr;
					}
				}
				ScriptGroup *pGroup;
				for (pGroup = pSL->getScriptGroup(); pGroup; pGroup=pGroup->getNext()) {
					for (pScr = pGroup->getScript(); pScr; pScr=pScr->getNext()) {
						if (pScr->getConditionTime()>maxTime) {
							maxTime = pScr->getConditionTime();
							maxScript = pScr;
						}
					}
				}
			}
			if (maxScript) {
				DEBUG_LOG(("   SCRIPT %s total time %f seconds,\n        evaluated %d times, avg execution %2.3f msec (Goal less than 0.05)\n",
					maxScript->getName().str(),
					maxScript->getConditionTime(), maxScript->getConditionCount(), 1000*maxScript->getConditionTime()/maxScript->getConditionCount()) );
				maxScript->addToConditionTime(-2*maxTime); // reset to negative.
			}

		}
		DEBUG_LOG(("***\n"));
	}
#endif
#endif

	VecSequentialScriptPtrIt seqScriptIt;
	// Use the iterator erase() returns; the old one is invalid afterwards. NULL entries are removed
	// right away so the loop ends.
	for (seqScriptIt = m_sequentialScripts.begin(); seqScriptIt != m_sequentialScripts.end(); ) {
		if (*seqScriptIt)
			seqScriptIt = cleanupSequentialScript(seqScriptIt, TRUE);
		else
			seqScriptIt = m_sequentialScripts.erase(seqScriptIt);
	}

	// clear out all the lists of object types that were in the old map.
	for (AllObjectTypesIt it = m_allObjectTypeLists.begin(); it != m_allObjectTypeLists.end(); it = m_allObjectTypeLists.begin() ) {
		if (*it) {
			removeObjectTypes(*it);
		} else {
			m_allObjectTypeLists.erase(it);
		}
	}
	DEBUG_ASSERTCRASH( m_allObjectTypeLists.empty() == TRUE, ("ScriptEngine::reset - m_allObjectTypeLists should be empty but is not!\n") );

	// reset all the reveals that have taken place.
	m_namedReveals.clear();
	
	// Clear the named objects list.
 	m_namedObjects.clear();

	m_completedVideo.clear();
	m_testingSpeech.clear();
	m_testingAudio.clear();
	m_uiInteractions.clear();
	for (i=0; i<MAX_PLAYER_COUNT; ++i)
	{
		m_triggeredSpecialPowers[i].clear();
		m_midwaySpecialPowers[i].clear();
		m_finishedSpecialPowers[i].clear();
		m_acquiredSciences[i].clear();
		m_completedUpgrades[i].clear();
	}

	ScriptList::reset(); // Deletes scripts loaded when the map was loaded.

	// reset the attack priority data
	for( i = 0; i < MAX_ATTACK_PRIORITIES; ++i )
		m_attackPriorityInfo[ i ].reset();

	// clear out all of our object counts.
	for( i = 0; i < MAX_PLAYER_COUNT; ++i )
		m_objectCounts[i].clear();

	// clear topple directions
	m_toppleDirections.clear();
		
}  // end reset

//-------------------------------------------------------------------------------------------------
/** newMap */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::newMap( void )
{
	m_numCounters = 1;
	Int i;
	for (i=0; i<MAX_COUNTERS; i++) {
		m_counters[i].value = 0;
		m_counters[i].isCountdownTimer = false;
		m_counters[i].name.clear();
	}
	m_numFlags = 1;
	for (i=0; i<MAX_FLAGS; i++) {
		m_flags[i].value = false;
		m_flags[i].name.clear();
	}
	m_endGameTimer = -1;
	m_closeWindowTimer = -1;
#ifdef SPECIAL_SCRIPT_PROFILING
#ifdef DEBUG_LOGGING
	m_numFrames=0;
	m_totalUpdateTime=0;
	m_maxUpdateTime=0;
#endif
#endif

	m_completedVideo.clear();
	m_testingSpeech.clear();
	m_testingAudio.clear();
	m_uiInteractions.clear();
	for (i=0; i<MAX_PLAYER_COUNT; ++i)
	{
		m_triggeredSpecialPowers[i].clear();
		m_midwaySpecialPowers[i].clear();
		m_finishedSpecialPowers[i].clear();
		m_acquiredSciences[i].clear();
		m_completedUpgrades[i].clear();
	}

	/* Run through scripts & set condition team names. */
	for (i=0; i<TheSidesList->getNumSides(); i++) {
		ScriptList *pSL = TheSidesList->getSideInfo(i)->getScriptList();
		if (!pSL) continue;
		if (pSL == nullptr) continue;
		Script *pScr;
		for (pScr = pSL->getScript(); pScr; pScr=pScr->getNext()) {
			checkConditionsForTeamNames(pScr);
		}
		ScriptGroup *pGroup;
		for (pGroup = pSL->getScriptGroup(); pGroup; pGroup=pGroup->getNext()) {
			for (pScr = pGroup->getScript(); pScr; pScr=pScr->getNext()) {
				checkConditionsForTeamNames(pScr);
			}
		}
	}	
	m_firstUpdate = true;

	m_fade = FADE_MULTIPLY; //default to a fade in from black.
	m_curFadeFrame = 0;
	m_minFade = 1.0f;
	m_maxFade = 0.0f;
	m_fadeFramesIncrease = 0;
	m_fadeFramesHold = 0;
	m_fadeFramesDecrease = FRAMES_TO_FADE_IN_AT_START;
	m_curFadeValue = 0.0f;

}  // end newMap

//-------------------------------------------------------------------------------------------------
/** Update */
//-------------------------------------------------------------------------------------------------
DECLARE_PERF_TIMER(ScriptEngine)
void ScriptEngine::update( void )
{
	USE_PERF_TIMER(ScriptEngine)
#ifdef SPECIAL_SCRIPT_PROFILING
#ifdef DEBUG_LOGGING
	__int64 startTime64;
	double timeToUpdate=0.0f;
	__int64 endTime64,freq64;
	QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);//LORENZEN'S NOTE_TO_SELF: USE THIS
	QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);//LORENZEN'S NOTE_TO_SELF: USE THIS
/* dump out the named objects table.  For extremely intense debug only.  jba. :P
	for (VecNamedRequestsIt it = m_namedObjects.begin(); it != m_namedObjects.end(); ++it) {
		AsciiString name = it->first;
		Object * obj = it->second;
		if (obj && obj->getAIUpdateInterface())
			DEBUG_LOG(("%s=%x('%s'), isDead%d\n", name.str(), obj, obj->getName().str(), obj->getAIUpdateInterface()->isDead()));
	}
	DEBUG_LOG(("\n\n"));
*/
#endif
#endif
	if (m_firstUpdate) {
		createNamedCache();
		m_firstUpdate = false;
	}
	
	if (m_closeWindowTimer>0) {
		m_closeWindowTimer--;
		if (m_closeWindowTimer < 1) {
			TheScriptActions->closeWindows(FALSE); // Close victory or defeat windows.
		}
	}
	if (m_endGameTimer>0) {
		m_endGameTimer--;
		if (m_endGameTimer < 1) {
			// clear out all the game data
			/*GameMessage *msg =*/ TheMessageStream->appendMessage( GameMessage::MSG_CLEAR_GAME_DATA );
			//TheScriptActions->closeWindows(FALSE); // Close victory or defeat windows.
		}
	}
	_updateFrameNumber();
	if (isTimeFrozenDebug()) {
		st_LastCurrentFrame = st_CurrentFrame - 1;	// Force us to get clean result from CanAppContinue
		return;
	}

	if (m_fade!=FADE_NONE) {
		updateFades(); 
	}

	if (m_endGameTimer>=0) {
		return; // we are just timing down 
	}
	
	if (TheScriptActions) {
		TheScriptActions->update();
	}
	if (TheScriptConditions) {
		TheScriptConditions->update();
	}
	// Update any countdown timers.
	Int i;
	// Note - counters start at 1.  0 means not assigned.
	for (i=1; i<m_numCounters; i++) {
		if (m_counters[i].isCountdownTimer) {
			// If counter has any time left, decrement.  Counters go to -1 and stop.
			if (m_counters[i].value >= 0) {
				m_counters[i].value--;
			}
		}
	}

	// Evaluate the scripts.
	for (i=0; i<TheSidesList->getNumSides(); i++) {
		m_currentPlayer = ThePlayerList->getNthPlayer(i);
		ScriptList *pSL = TheSidesList->getSideInfo(i)->getScriptList();
		if (!pSL) continue;
		executeScripts(pSL->getScript());
		ScriptGroup *pGroup;
		for (pGroup = pSL->getScriptGroup(); pGroup; pGroup=pGroup->getNext()) {
			if (!pGroup->isActive()) {
				continue; // Don't execute inactive groups.
			}
			if (pGroup->isSubroutine()) {
				continue; // Don't execute subroutine groups.
			}
			executeScripts(pGroup->getScript());
		}
		m_currentPlayer = nullptr;
	}
	
	// Reset the entered/exited flag in teams, so the next update sets them 
	// correctly.  Also, execute any team created scripts.
	ThePlayerList->updateTeamStates();

	// Clear the UI Interaction flags.
	m_uiInteractions.clear();

	// update all sequential stuff.
	evaluateAndProgressAllSequentialScripts();

	// Script debugger stuff
	st_CurrentFrame++;
	if (st_DebugDLL) { 
		for (int j = 1; j < m_numCounters; ++j) {
			_adjustVariable(m_counters[j].name.str(), m_counters[j].value);
		}

		for (int k = 1; k < m_numFlags; ++k) {
			_adjustVariable(m_flags[k].name.str(), m_flags[k].value);
		}
	}
#ifdef _DEBUG
	if (TheGameLogic->getFrame()==0) {
		for (i=0; i<m_numAttackInfo; i++) {
			m_attackPriorityInfo[i].dumpPriorityInfo();
		}
	}
#endif

#ifdef SPECIAL_SCRIPT_PROFILING
#ifdef DEBUG_LOGGING
	QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);//LORENZEN'S NOTE_TO_SELF: USE THIS
	timeToUpdate = ((double)(endTime64-startTime64) / (double)(freq64));//LORENZEN'S NOTE_TO_SELF: USE THIS
	m_numFrames++;
	m_totalUpdateTime+=timeToUpdate;
	if (timeToUpdate > m_maxUpdateTime) m_maxUpdateTime = timeToUpdate;
	m_curUpdateTime = timeToUpdate;
#endif
#endif
	

}  // end update

//-------------------------------------------------------------------------------------------------
/** getStats */
//-------------------------------------------------------------------------------------------------
AsciiString ScriptEngine::getStats(Real *curTimePtr, Real *script1Time, Real *script2Time)
{
	*curTimePtr = 0;
	*script1Time = 0;
	*script2Time = 0;
	AsciiString msg = "Script Engine Profiling disabled.";
#ifdef SPECIAL_SCRIPT_PROFILING
#ifdef DEBUG_LOGGING
	msg = "#1-";
	*curTimePtr = (Real)m_curUpdateTime;
	Int numToDump;
	Int i;
	if (TheSidesList) {
		for (numToDump=0; numToDump<2; numToDump++) {
			Real maxTime = 0;
			Script *maxScript = nullptr;
			/* Run through scripts & set condition team names. */
			for (i=0; i<TheSidesList->getNumSides(); i++) {
				ScriptList *pSL = TheSidesList->getSideInfo(i)->getScriptList();
				if (!pSL) continue;
				if (pSL == nullptr) continue;
				Script *pScr;
				for (pScr = pSL->getScript(); pScr; pScr=pScr->getNext()) {
					if (pScr->getCurTime()>maxTime) {
						maxTime = pScr->getCurTime();
						maxScript = pScr;
					}
				}
				ScriptGroup *pGroup;
				for (pGroup = pSL->getScriptGroup(); pGroup; pGroup=pGroup->getNext()) {
					for (pScr = pGroup->getScript(); pScr; pScr=pScr->getNext()) {
						if (pScr->getCurTime()>maxTime) {
							maxTime = pScr->getCurTime();
							maxScript = pScr;
						}
					}
				}
			}
			if (maxScript) {
				if (numToDump == 0) {
					*script1Time = maxTime;
				}	else {
					*script2Time = maxTime;
					msg.concat(", #2-");
				}
				msg.concat(maxScript->getName());
				maxScript->setCurTime(0); // reset to 0.
			}
		}
	}
#endif
#endif
	return msg;
}  // end getStats

//-------------------------------------------------------------------------------------------------
/** startQuickEndGameTimer */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::startQuickEndGameTimer( void )
{
	m_endGameTimer = 1;
}  // end startQuickEndGameTimer

//-------------------------------------------------------------------------------------------------
/** startEndGameTimer */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::startEndGameTimer( void )
{
	m_endGameTimer = FRAMES_TO_SHOW_WIN_LOSE_MESSAGE;
}  // end startEndGameTimer

//-------------------------------------------------------------------------------------------------
/** startCloseWindowTimer */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::startCloseWindowTimer( void )
{
	m_closeWindowTimer = FRAMES_TO_SHOW_WIN_LOSE_MESSAGE;
}  // end startCloseWindowTimer

//-------------------------------------------------------------------------------------------------
/** updateFades */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::updateFades( void )
{
	m_curFadeFrame++;
	Int fade = m_curFadeFrame;
	Real factor;
	if (fade<=m_fadeFramesIncrease) {
		factor = static_cast<Real>(m_curFadeFrame)/m_fadeFramesIncrease;
		m_curFadeValue = m_minFade + factor*(m_maxFade-m_minFade);
		return;
	}	
	fade -= m_fadeFramesIncrease;
	if (fade<=m_fadeFramesHold) {
		m_curFadeValue = m_maxFade;
		return;
	}
	fade -= m_fadeFramesHold;
	if (fade<=m_fadeFramesDecrease) {
		Int divisor = m_fadeFramesDecrease+1;
		if (divisor==0) divisor = 1;
		factor = static_cast<Real>(fade)/divisor;
		m_curFadeValue = m_maxFade + factor*(m_minFade-m_maxFade);
		return;
	}	
	// time is up.
	m_fade = FADE_NONE;
}  // end updateFades

//-------------------------------------------------------------------------------------------------
/** getCurrentPlayer */
//-------------------------------------------------------------------------------------------------
Player *ScriptEngine::getCurrentPlayer(void)
{
	if (m_currentPlayer==nullptr)
		AppendDebugMessage("***Unexpected NULL player:***", false);
	return m_currentPlayer;
}  // end getCurrentPlayer

//-------------------------------------------------------------------------------------------------
/** getSkirmishEnemyPlayer */
//-------------------------------------------------------------------------------------------------
Player *ScriptEngine::getSkirmishEnemyPlayer(void)
{
	Bool is_GeneralsChallengeContext = TheCampaignManager->getCurrentCampaign() && TheCampaignManager->getCurrentCampaign()->m_isChallengeCampaign;
	if (m_currentPlayer) {
		Player *enemy = m_currentPlayer->getCurrentEnemy();
		if (enemy==nullptr) {
			// get the human player.
			Int i;
			for (i=0; i<ThePlayerList->getPlayerCount(); i++) {
				enemy = ThePlayerList->getNthPlayer(i);
				if (/*enemy->isLocalPlayer() &&*/ enemy->getPlayerType()==PLAYER_HUMAN) {
					if (is_GeneralsChallengeContext && enemy->getPlayerNameKey() == NAMEKEY(THE_PLAYER)) {
						continue; // In generals challenge, ThePlayer is a dummy player. jba. [8/15/2003]
					}
					
					return enemy;
				}
				enemy = nullptr;
			}
		}
		return enemy;
	}
	DEBUG_CRASH(("No enemy found.  Unexpected but not fatal. jba."));
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
/** getPlayerFromAsciiString */
//-------------------------------------------------------------------------------------------------
Player *ScriptEngine::getPlayerFromAsciiString(const AsciiString& playerString)
{
	Bool is_GeneralsChallengeContext = TheCampaignManager->getCurrentCampaign() && TheCampaignManager->getCurrentCampaign()->m_isChallengeCampaign;
	if (playerString == LOCAL_PLAYER || (playerString == THE_PLAYER && is_GeneralsChallengeContext))
		// Designers have built their Generals' Challenge maps, referencing "ThePlayer" meaning the local player.
		// However, they've also built many of their single player maps with this string, where "ThePlayer" is not intended as an alias.
		return ThePlayerList->getLocalPlayer();
	if (playerString == THIS_PLAYER)
		return getCurrentPlayer();
	else if (playerString == THIS_PLAYER_ENEMY)	{
		return getSkirmishEnemyPlayer();
	}
	else {
		NameKeyType key = NAMEKEY(playerString);
		Player *pPlayer = ThePlayerList->findPlayerWithNameKey(key);
		if (pPlayer!=nullptr) {
			return pPlayer;
		}
	}
	
	AppendDebugMessage("***Invalid Player name:***", false);

	return nullptr;
}

//-------------------------------------------------------------------------------------------------
/** Given a name, return the associated trigger area, or NULL if one doesn't exist.
Handles skirmish name qualification.  */
//-------------------------------------------------------------------------------------------------
PolygonTrigger *ScriptEngine::getQualifiedTriggerAreaByName( AsciiString name )
{
	if (name == MY_INNER_PERIMETER || name == MY_OUTER_PERIMETER) {
		if (m_currentPlayer) {
			Int ndx = m_currentPlayer->getMpStartIndex()+1;
			if (name==MY_INNER_PERIMETER) {
				name.format("%s%d", INNER_PERIMETER, ndx);
			}	else {
				name.format("%s%d", OUTER_PERIMETER, ndx);
			}
		}	else {
			return nullptr;
		}
	} else if (name == ENEMY_INNER_PERIMETER || name == ENEMY_OUTER_PERIMETER) {

		Int mpNdx;
		mpNdx = -1;
		if (m_currentPlayer) {
			Player *enemy = getCurrentPlayer()->getCurrentEnemy();
			if (enemy) {
				mpNdx = enemy->getMpStartIndex()+1;
			}
		}
		if (name==ENEMY_INNER_PERIMETER) {
			name.format("%s%d", INNER_PERIMETER, mpNdx);
		}	else {
			name.format("%s%d", OUTER_PERIMETER, mpNdx);
		}
	}
	PolygonTrigger *trig = TheTerrainLogic->getTriggerAreaByName(name);
	if (trig==nullptr) {
		AsciiString msg = "!!!WARNING!!! Trigger area '";
		msg.concat(name);
		msg.concat("' not found.");
		AppendDebugMessage(msg, TRUE);
	}

	return trig;
}



//-------------------------------------------------------------------------------------------------
/** getTeamNamed */
//-------------------------------------------------------------------------------------------------
Team * ScriptEngine::getTeamNamed(const AsciiString& teamName)
{
	Bool is_GeneralsChallengeContext = TheCampaignManager->getCurrentCampaign() && TheCampaignManager->getCurrentCampaign()->m_isChallengeCampaign;
	if (teamName == TEAM_THE_PLAYER && is_GeneralsChallengeContext)
		// Designers have built their Generals' Challenge maps, referencing "teamThePlayer" meaning the local player's default (parent) team.
		// However, they've also built many of their single player maps with this string, where "teamThePlayer" is not intended as an alias.
		return ThePlayerList->getLocalPlayer()->getDefaultTeam();
	if (teamName == THIS_TEAM) {
		if (m_callingTeam) 
			return m_callingTeam;
		return m_conditionTeam;
	}
	if (m_callingTeam && m_callingTeam->getName() == teamName) {
		return m_callingTeam;
	}
	if (m_conditionTeam && m_conditionTeam->getName() == teamName) {
		return m_conditionTeam;
	}
	TeamPrototype *theTeamProto = TheTeamFactory->findTeamPrototype( teamName );
	if (theTeamProto == nullptr) return nullptr;
	if (theTeamProto->getIsSingleton()) {
		Team *theTeam = theTeamProto->getFirstItemIn_TeamInstanceList();
		if (theTeam && theTeam->isActive()) {
			return theTeam;
		}
		return nullptr; // team wasn't active.
	}
	
	static int warnCount = 0;
	if (theTeamProto->countTeamInstances()>1) {
		if (warnCount<10) {
			warnCount++;
			AppendDebugMessage("***Referencing multiple team by unspecific instance:***", false);
			AppendDebugMessage(teamName, false);
		}
	}
	return theTeamProto->getFirstItemIn_TeamInstanceList();
}  // end getTeamNamed

//-------------------------------------------------------------------------------------------------
/** getUnitNamed */
//-------------------------------------------------------------------------------------------------
Object * ScriptEngine::getUnitNamed(const AsciiString& unitName)
{
	if (unitName == THIS_OBJECT) {
		if (m_callingObject) {
			return m_callingObject;
		}
		return m_conditionObject;
	}

	for (VecNamedRequestsIt it = m_namedObjects.begin(); it != m_namedObjects.end(); ++it) {
		if (unitName == (it->first)) {
			return it->second;
		}
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
/** didUnitExist */
//-------------------------------------------------------------------------------------------------
Bool ScriptEngine::didUnitExist(const AsciiString& unitName)
{
	for (VecNamedRequestsIt it = m_namedObjects.begin(); it != m_namedObjects.end(); ++it) {
		if (unitName == (it->first)) {
			return (it->second == nullptr);
		}
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
/** runScript - Executes a subroutine script, or script group - tests conditions, and executes actions or false actions.  */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::runScript(const AsciiString& scriptName, Team *pThisTeam)
{
	if (scriptName.isEmpty()) {
		return; // no script, just return.
	}
	if (scriptName==NONE_STRING) {
		return; // no script
	}
	

	Player *savPlayer = m_currentPlayer;
//	Team *pSavConditionTeam = m_conditionTeam;
	LatchRestore<Team *> latch(m_callingTeam, pThisTeam);

	m_conditionTeam = nullptr;
	m_currentPlayer = nullptr;
	if (m_callingTeam) {
		m_currentPlayer = m_callingTeam->getControllingPlayer();
	}
	Script  *pScript = nullptr;
	ScriptGroup *pGroup = findGroup(scriptName);
	if (pGroup) {
		if (pGroup->isSubroutine()) {
			if (pGroup->isActive()) {
				executeScripts(pGroup->getScript());
			}
		}	else {
				AppendDebugMessage("***Attempting to call script that is not a subroutine:***", false);
				AppendDebugMessage(scriptName, false);
				DEBUG_LOG(("Attempting to call script '%s' that is not a subroutine.\n", scriptName.str()));
		}
	}	else {
		pScript = findScript(scriptName);
		if (pScript != nullptr) {
			if (pScript->isSubroutine()) {
				executeScript(pScript);
			} else {
				AppendDebugMessage("***Attempting to call script that is not a subroutine:***", false);
				AppendDebugMessage(scriptName, false);
				DEBUG_LOG(("Attempting to call script '%s' that is not a subroutine.\n", scriptName.str()));
			}
		} else {
			AppendDebugMessage("***Script not defined:***", false);
			AppendDebugMessage(scriptName, false);
			DEBUG_LOG(("WARNING: Script '%s' not defined.\n", scriptName.str()));
		}
	}
	// m_callingTeam is restored automatically via LatchRestore
	m_conditionTeam = m_conditionTeam;
	m_currentPlayer = savPlayer;
}  // end runScript


//-------------------------------------------------------------------------------------------------
/** runScript - Executes a subroutine script, or script group - tests conditions, and executes actions or false actions.  */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::runObjectScript(const AsciiString& scriptName, Object *pThisObject)
{
	if (scriptName.isEmpty()) {
		return; // no script, just return.
	}
	if (scriptName==NONE_STRING) {
		return; // no script
	}
	Object *pSavCallingObject = m_callingObject;
	m_callingObject = pThisObject;
	Script  *pScript = nullptr;
	ScriptGroup *pGroup = findGroup(scriptName);
	if (pGroup) {
		if (pGroup->isSubroutine()) {
			if (pGroup->isActive()) {
				executeScripts(pGroup->getScript());
			}
		}	else {
				AppendDebugMessage("***Attempting to call script that is not a subroutine:***", false);
				AppendDebugMessage(scriptName, false);
				DEBUG_LOG(("Attempting to call script '%s' that is not a subroutine.\n", scriptName.str()));
		}
	}	else {
		pScript = findScript(scriptName);
		if (pScript != nullptr) {
			if (pScript->isSubroutine()) {
				executeScript(pScript);
			} else {
				AppendDebugMessage("***Attempting to call script that is not a subroutine:***", false);
				AppendDebugMessage(scriptName, false);
				DEBUG_LOG(("Attempting to call script '%s' that is not a subroutine.\n", scriptName.str()));
			}
		} else {
			AppendDebugMessage("***Script not defined:***", false);
			AppendDebugMessage(scriptName, false);
			DEBUG_LOG(("WARNING: Script '%s' not defined.\n", scriptName.str()));
		}
	}
	m_callingObject = pSavCallingObject;
}  // end runScript


//-------------------------------------------------------------------------------------------------
/** Locates a group by name. */
//-------------------------------------------------------------------------------------------------
ScriptGroup  *ScriptEngine::findGroup(const AsciiString& name)
{
	Int i;
	for (i=0; i<TheSidesList->getNumSides(); i++) {
		ScriptList *pSL = TheSidesList->getSideInfo(i)->getScriptList();
		if (pSL==nullptr) continue;
		ScriptGroup *pGroup;
		for (pGroup = pSL->getScriptGroup(); pGroup; pGroup=pGroup->getNext()) {
			if (pGroup->getName() == name) {
				return pGroup;
			}
		}
	}
	return 0; // Shouldn't ever happen.
}

//-------------------------------------------------------------------------------------------------
/** Locates a script by name. */
//-------------------------------------------------------------------------------------------------
Script  *ScriptEngine::findScript(const AsciiString& name)
{
	Int i;
	for (i=0; i<TheSidesList->getNumSides(); i++) {
		ScriptList *pSL = TheSidesList->getSideInfo(i)->getScriptList();
		if (pSL==nullptr) continue;
		Script *pScr;
		for (pScr = pSL->getScript(); pScr; pScr=pScr->getNext()) {
			if ((name==pScr->getName())) {
				return pScr;
			}
		}
		ScriptGroup *pGroup;
		for (pGroup = pSL->getScriptGroup(); pGroup; pGroup=pGroup->getNext()) {
			for (pScr = pGroup->getScript(); pScr; pScr=pScr->getNext()) {
				if ((name==pScr->getName())) {
					return pScr;
				}
			}
		}
	}
	return 0; // Shouldn't ever happen.
}

//-------------------------------------------------------------------------------------------------
/** Sets a fade. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::setFade( ScriptAction *pAction )
{
#if defined(_DEBUG)
	if (TheGlobalData->m_disableCameraFade)
	{
		m_fade = FADE_NONE;
		return;
	}
#endif

	DEBUG_ASSERTCRASH(pAction->getNumParameters() >= 5, ("Not enough parameters.\n"));
	switch (pAction->getActionType()) {
		default:	m_fade = FADE_NONE; return;
		case ScriptAction::CAMERA_FADE_ADD: m_fade = FADE_ADD; break;
		case ScriptAction::CAMERA_FADE_SUBTRACT: m_fade = FADE_SUBTRACT; break;
		case ScriptAction::CAMERA_FADE_SATURATE: m_fade = FADE_SATURATE; break;
		case ScriptAction::CAMERA_FADE_MULTIPLY: m_fade = FADE_MULTIPLY; break;
	}
	m_curFadeFrame = 0;
	m_minFade = pAction->getParameter(0)->getReal();
	m_maxFade = pAction->getParameter(1)->getReal();
	m_fadeFramesIncrease = pAction->getParameter(2)->getInt();
	m_fadeFramesHold = pAction->getParameter(3)->getInt();
	m_fadeFramesDecrease = pAction->getParameter(4)->getInt();
	m_curFadeValue = m_minFade;
	if( m_fadeFramesIncrease == 0 )
	{
		updateFades();
	}
}

//-------------------------------------------------------------------------------------------------
/** Sets a counter. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::setSway( ScriptAction *pAction )
{
	DEBUG_ASSERTCRASH(pAction->getNumParameters() >= 5, ("Not enough parameters.\n"));
	++m_breezeInfo.m_breezeVersion;
	m_breezeInfo.m_direction = pAction->getParameter(0)->getReal();
	m_breezeInfo.m_directionVec.x = Sin(m_breezeInfo.m_direction);
	m_breezeInfo.m_directionVec.y = Cos(m_breezeInfo.m_direction);
	m_breezeInfo.m_intensity = pAction->getParameter(1)->getReal();
	m_breezeInfo.m_lean = pAction->getParameter(2)->getReal();
	m_breezeInfo.m_breezePeriod = static_cast<Short>(pAction->getParameter(3)->getInt());
	if (m_breezeInfo.m_breezePeriod<1) 
		m_breezeInfo.m_breezePeriod = 1;
	m_breezeInfo.m_randomness = pAction->getParameter(4)->getReal();

}



//-------------------------------------------------------------------------------------------------
/** Enables a script or group. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::enableScript( ScriptAction *pAction )
{
	DEBUG_ASSERTCRASH(pAction->getNumParameters() >= 1, ("Not enough parameters.\n"));
	ScriptGroup *pGroup = findGroup(pAction->getParameter(0)->getString());
	if (pGroup) {
		pGroup->setActive(true);
	}
	Script *pScript = findScript(pAction->getParameter(0)->getString());
	if (pScript) {
		pScript->setActive(true);
	}
}

//-------------------------------------------------------------------------------------------------
/** Enables a script or group. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::disableScript( ScriptAction *pAction )
{
	DEBUG_ASSERTCRASH(pAction->getNumParameters() >= 1, ("Not enough parameters.\n"));
	Script *pScript = findScript(pAction->getParameter(0)->getString());
	if (pScript) {
		pScript->setActive(false);
	}
	ScriptGroup *pGroup = findGroup(pAction->getParameter(0)->getString());
	if (pGroup) {
		pGroup->setActive(false);
	}
}

//-------------------------------------------------------------------------------------------------
/** Executes a script subroutine. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::callSubroutine( ScriptAction *pAction )
{
	DEBUG_ASSERTCRASH(pAction->getNumParameters() >= 1, ("Not enough parameters.\n"));
	AsciiString scriptName = pAction->getParameter(0)->getString();
	Script  *pScript;
	ScriptGroup *pGroup = findGroup(scriptName);
	if (pGroup) {
		if (pGroup->isSubroutine()) {
			if (pGroup->isActive()) {
				executeScripts(pGroup->getScript());
			}
		}	else {
				AppendDebugMessage("***Attempting to call script that is not a subroutine:***", false);
				AppendDebugMessage(scriptName, false);
				DEBUG_LOG(("Attempting to call script '%s' that is not a subroutine.\n", scriptName.str()));
		}
	}	else {
		pScript = findScript(scriptName);
		if (pScript != nullptr) {
			if (pScript->isSubroutine()) {
				executeScript(pScript);
			} else {
				AppendDebugMessage("***Attempting to call script that is not a subroutine:***", false);
				AppendDebugMessage(scriptName, false);
				DEBUG_LOG(("Attempting to call script '%s' that is not a subroutine.\n", scriptName.str()));
			}
		} else {
			AppendDebugMessage("***Script not defined:***", false);
			AppendDebugMessage(scriptName, false);
			DEBUG_LOG(("WARNING: Script '%s' not defined.\n", scriptName.str()));
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Checks to see if any teams are referenced in the conditions, so we can properly
iterate over multiple teams. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::checkConditionsForTeamNames(Script *pScript)
{
	AsciiString singletonTeamName;
	AsciiString multiTeamName;

	if (pScript->getDelayEvalSeconds()>0) {
		// Offset by a random number of frames
		pScript->setFrameToEvaluate(GameLogicRandomValue(0,2*LOGICFRAMES_PER_SECOND));
	} else {
		pScript->setFrameToEvaluate(0);
	}

	AsciiString scriptName = pScript->getName();
	OrCondition *pOr;
	for (pOr = pScript->getOrCondition(); pOr; pOr = pOr->getNextOrCondition()) {
		Condition *pCondition;
		for (pCondition = pOr->getFirstAndCondition(); pCondition; pCondition = pCondition->getNext()) {
			Int i;
			for (i=0; i<pCondition->getNumParameters(); i++) {
				if (Parameter::TEAM == pCondition->getParameter(i)->getParameterType()) {
					AsciiString teamName = pCondition->getParameter(i)->getString();
					TeamPrototype *proto = TheTeamFactory->findTeamPrototype(teamName);
					if (proto==nullptr) continue; // Undefined team - don't bother.
					Bool singleton = proto->getIsSingleton();
					if (proto->getTemplateInfo()->m_maxInstances < 2) {
						singleton = true;
					}
					if (singleton) {
						singletonTeamName = teamName;		// Singleton team - use if it is the only one, but can have multiple of these.
					} else {
						if (multiTeamName.isEmpty()) {
							multiTeamName = teamName;		// Use one multiply defined team.  Good.
						} else if (multiTeamName!=teamName) {
							// More than one multiply defined team - bad.
							AppendDebugMessage("***WARNING: Script contains multiple non-singleton team conditions::***", false);
							AppendDebugMessage(scriptName, false);
							AppendDebugMessage(multiTeamName, false);
							AppendDebugMessage(teamName, false);
							DEBUG_LOG(("WARNING: Script '%s' contains multiple non-singleton team conditions: %s & %s.\n", scriptName.str(), 
								multiTeamName.str(), teamName.str()));
						}
					}
				}
			}
		}
	}
	if (multiTeamName.isEmpty()) {
		if (!singletonTeamName.isEmpty()) {
			pScript->setConditionTeamName(singletonTeamName);
		}
  } else { 
		pScript->setConditionTeamName(multiTeamName);
	}

}

//-------------------------------------------------------------------------------------------------
/** Executes a script. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::executeScript( Script *pScript )
{

	pScript->setCurTime(0);
	// If script is not active, return.
	if (!pScript->isActive()) {
		return;
	}
	enum GameDifficulty difficulty = getGlobalDifficulty();
	if (m_currentPlayer) {
		difficulty = m_currentPlayer->getPlayerDifficulty();
	}
	// If script doesn't match difficulty level, return.
	switch (difficulty) {
		case DIFFICULTY_EASY : if (!pScript->isEasy()) return;  break;
		case DIFFICULTY_NORMAL : if (!pScript->isNormal()) return;  break;
		case DIFFICULTY_HARD : if (!pScript->isHard()) return;  break;
	}
	// If we are doing peridic evaluation, check the frame.
	if (TheGameLogic->getFrame()<pScript->getFrameToEvaluate()) {
		return;
	}
	Int delaySeconds = pScript->getDelayEvalSeconds();

	if (delaySeconds>0) {
		pScript->setFrameToEvaluate(TheGameLogic->getFrame()+delaySeconds*LOGICFRAMES_PER_SECOND);
	}
#ifdef DEBUG_LOGGING
#ifdef SPECIAL_SCRIPT_PROFILING
	__int64 startTime64;
	Real timeToEvaluate=0.0f;
	__int64 endTime64,freq64;
	QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);
	QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);
#endif
#endif

	Team *pSavConditionTeam = m_conditionTeam;
	TeamPrototype *pProto = nullptr;

	if (!pScript->getConditionTeamName().isEmpty()) {
		pProto = TheTeamFactory->findTeamPrototype(pScript->getConditionTeamName());
	}

	if (pProto && pProto->countTeamInstances() > 0) {
		// We have a team referred to in the conditions.  Iterate over the instances of the team, 
		// applying the script conditions (and possibly actions) to each instance of the team.
		for (DLINK_ITERATOR<Team> iter = pProto->iterate_TeamInstanceList(); !iter.done(); iter.advance()) {
			m_conditionTeam = iter.cur();
			// If conditions evaluate to true, execute actions.
			if (evaluateConditions(pScript)) {
				// Script Debug window
				if (pScript->getAction()) {
					_appendMessage(pScript->getName());
					executeActions(pScript->getAction());
				}
				
				if (pScript->isOneShot()) {
					pScript->setActive(false);
				}
			}	else if (pScript->getFalseAction()) {

				// Script Debug window
				_appendMessage(pScript->getName(), false);

				// Only do this is there are actually false actions.
				executeActions(pScript->getFalseAction());
      } 
		}

	} else {
		m_conditionTeam = nullptr;
		// If conditions evaluate to true, execute actions.
		if (evaluateConditions(pScript)) {
			if (pScript->getAction()) {
				// Script Debug window
				_appendMessage(pScript->getName());
				executeActions(pScript->getAction());
			}

			if (pScript->isOneShot()) {
				pScript->setActive(false);
			}
		}	else if (pScript->getFalseAction()) {

			// Script Debug window
			_appendMessage(pScript->getName(), false);

			// Only do this is there are actually false actions.
			executeActions(pScript->getFalseAction());
			if (pScript->isOneShot()) {
				pScript->setActive(false);
			}
		}
	}
#ifdef DEBUG_LOGGING
#ifdef SPECIAL_SCRIPT_PROFILING
	QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
	timeToEvaluate = ((Real)(endTime64-startTime64) / (Real)(freq64));
	pScript->setCurTime(timeToEvaluate);
#endif
#endif

	m_conditionTeam = pSavConditionTeam;
}

//-------------------------------------------------------------------------------------------------
/** Evaluates a condition */
//-------------------------------------------------------------------------------------------------
Bool ScriptEngine::evaluateCondition( Condition *pCondition )
{
	switch (pCondition->getConditionType()) {
		default: 
			return TheScriptConditions->evaluateCondition(pCondition);
		case Condition::CONDITION_FALSE: return false;
		case Condition::CONDITION_TRUE: return true;
		case Condition::COUNTER: return evaluateCounter(pCondition);
		case Condition::FLAG: return evaluateFlag(pCondition);
		case Condition::TIMER_EXPIRED: return evaluateTimer(pCondition);
	}
}

//-------------------------------------------------------------------------------------------------
/** Execute an action specified by pActionHead */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::friend_executeAction( ScriptAction *pActionHead, Team *pThisTeam )
{
	Team *pSavCallingTeam = m_callingTeam;
	Player *pSavPlayer = m_currentPlayer;
	m_callingTeam = pThisTeam;
	m_currentPlayer = nullptr;
	if (pThisTeam) {
		m_currentPlayer = pThisTeam->getControllingPlayer();
	}
	executeActions(pActionHead);
	m_callingTeam = pSavCallingTeam;
	m_currentPlayer = pSavPlayer;
}

//-------------------------------------------------------------------------------------------------
/** adds an object to the cache, allowing it to be looked up by name */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::addObjectToCache(Object* pNewObject)
{
	if (!pNewObject) {
		return;
	}

	AsciiString objName = pNewObject->getName();

	if (objName == AsciiString::TheEmptyString) {
		return;
	}

	for (VecNamedRequestsIt it = m_namedObjects.begin(); it != m_namedObjects.end(); ++it) {
		if (it->first == objName) {
			if (it->second == nullptr) {
				AsciiString newNameForDead;
				newNameForDead.format("Reassigning dead object's name '%s' to object (%d) of type '%s'\n", objName.str(), pNewObject->getID(), pNewObject->getTemplate()->getName().str());
				TheScriptEngine->AppendDebugMessage(newNameForDead, FALSE);
				DEBUG_LOG(("%s", newNameForDead.str()));
				it->second = pNewObject;
				return;
			} else {
				DEBUG_CRASH(("Attempting to assign the name '%s' to object (%d) of type '%s'," 
										 " but object (%d) of type '%s' already has that name\n",
										 objName.str(), pNewObject->getID(), pNewObject->getTemplate()->getName().str(), 
										 it->second->getID(), it->second->getTemplate()->getName().str()));
				return;
			}
		}

		if (pNewObject == (it->second)) {
			it->first = objName;
			return;
		}
	}

	NamedRequest req;
	req.first = objName;
	req.second = pNewObject;

	m_namedObjects.push_back(req);
}

//-------------------------------------------------------------------------------------------------
/** removes a dead object from the cache, to prevent "Bad Stuff"(r) */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::removeObjectFromCache( Object* pDeadObject )
{
	for (VecNamedRequestsIt it = m_namedObjects.begin(); it != m_namedObjects.end(); ++it) {
		if (pDeadObject == (it->second)) {
			it->second = nullptr;	// Don't remove it, cause we want to check whether we ever knew a name later
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Kris:
		Looks for existing cached object with same name and replaces that object point with the supplied one. 
		This is an important feature for units that change into something else. Good examples include terrorists
		entering a vehicle to convert it to a carbomb, pilots adding veterancy to vehicles, hijackers stealing
		vehicles, and infantry taking over disabled vehicles.
*/
//-------------------------------------------------------------------------------------------------
void ScriptEngine::transferObjectName( const AsciiString& unitName, Object *pNewObject )
{
	//Sanity checks
	if( !pNewObject || !unitName.getLength() )
	{
		return;
	}

	//John Ahlquist: When transferring an object name, make sure the new object isn't already in 
	//							 the vector. If so, remove it, or it'll end up there twice and cause a crash.
	if( pNewObject->getName().isNotEmpty() ) 
	{
		removeObjectFromCache(pNewObject);
	}

	pNewObject->setName(unitName); // make sure it's named the name.

	//Loop through the cached list and find the string entry. If found, change the object
	//so it's pointing to the new one.
	for( VecNamedRequestsIt it = m_namedObjects.begin(); it != m_namedObjects.end(); ++it )
	{
		if( !unitName.compare( it->first ) )
		{
			Object* pOldObj = it->second;
			if( pOldObj )
			{
				// if you are transferring your name, you should also transfer any custom indicator color you have.
				if (pOldObj->hasCustomIndicatorColor())
					pNewObject->setCustomIndicatorColor(pOldObj->getIndicatorColor());
				else
					pNewObject->removeCustomIndicatorColor();
			}

			it->second = pNewObject;

			return;
		}
	}

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ScriptEngine::notifyOfObjectDestruction( Object *pDeadObject )
{
	if (!pDeadObject->getName().isEmpty()) 
	{
		removeObjectFromCache(pDeadObject);
	}

	if (m_conditionObject == pDeadObject) {
		m_conditionObject = nullptr;
	}

	if (m_callingObject == pDeadObject) {
		m_callingObject = nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
/** Notify the script engine that a video has completed */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::notifyOfCompletedVideo( const AsciiString& completedVideo ) 
{
	m_completedVideo.push_back(completedVideo);
}

//-------------------------------------------------------------------------------------------------
/** Notify the script engine that a special power fired */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::notifyOfTriggeredSpecialPower( Int playerIndex, const AsciiString& completedPower, ObjectID sourceObj )
{
	m_triggeredSpecialPowers[playerIndex].push_back(AsciiStringObjectIDPair(completedPower, sourceObj));
}

//-------------------------------------------------------------------------------------------------
/** Notify the script engine that a special power fired */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::notifyOfMidwaySpecialPower( Int playerIndex, const AsciiString& completedPower, ObjectID sourceObj )
{
	m_midwaySpecialPowers[playerIndex].push_back(AsciiStringObjectIDPair(completedPower, sourceObj));
}

//-------------------------------------------------------------------------------------------------
/** Notify the script engine that a special power fired */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::notifyOfCompletedSpecialPower( Int playerIndex, const AsciiString& completedPower, ObjectID sourceObj )
{
	m_finishedSpecialPowers[playerIndex].push_back(AsciiStringObjectIDPair(completedPower, sourceObj));
}

//-------------------------------------------------------------------------------------------------
/** Notify the script engine that an upgrade finished */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::notifyOfCompletedUpgrade( Int playerIndex, const AsciiString& upgrade, ObjectID sourceObj )
{
	m_completedUpgrades[playerIndex].push_back(AsciiStringObjectIDPair(upgrade, sourceObj));
}

//-------------------------------------------------------------------------------------------------
/** Notify the script engine that a general was chosen fired */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::notifyOfAcquiredScience( Int playerIndex, ScienceType science )
{
	m_acquiredSciences[playerIndex].push_back(science);
}

//-------------------------------------------------------------------------------------------------
/** Notify that a UI button was pressed and some flag should go true, for one frame only. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::signalUIInteract(const AsciiString& hookName)
{
	m_uiInteractions.push_front(hookName);
#ifdef DEBUG_LOGGING
	AppendDebugMessage(hookName, false); // don't bother in Release
#endif
}

//-------------------------------------------------------------------------------------------------
/** Determine whether a video has completed */
//-------------------------------------------------------------------------------------------------
Bool ScriptEngine::isVideoComplete( const AsciiString& completedVideo, Bool removeFromList )
{
	ListAsciiStringIt findIt = std::find(m_completedVideo.begin(), m_completedVideo.end(), completedVideo);
	if (findIt != m_completedVideo.end()) {
		if (removeFromList) {
			m_completedVideo.erase(findIt);
		}
		return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
/** Determine whether a speech has completed */
//-------------------------------------------------------------------------------------------------
Bool ScriptEngine::isSpeechComplete( const AsciiString& testSpeech, Bool removeFromList )
{
	ListAsciiStringUINTIt findIt;
	for (findIt = m_testingSpeech.begin(); findIt != m_testingSpeech.end(); ++findIt) {
		if (findIt->first == testSpeech) {
			break;
		}
	}

	if (findIt == m_testingSpeech.end()) {
		PairAsciiStringUINT newPair;
		AudioEventRTS event(testSpeech);
		Real audioLength = TheLogicAudio->getAudioLengthMS(&event);
		UnsignedInt frameCount = REAL_TO_UNSIGNEDINT(audioLength / MSEC_PER_LOGICFRAME_REAL);

		newPair.first = testSpeech;
		newPair.second = frameCount + TheGameLogic->getFrame();
		m_testingSpeech.push_front(newPair);
		findIt = m_testingSpeech.begin();
	}

	if (TheGameLogic->getFrame() >= findIt->second) {
		// This audio is completed
		if (removeFromList) {
			m_testingSpeech.erase(findIt);
		}
		return true;
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
/** Determine whether a sound has completed */
//-------------------------------------------------------------------------------------------------
Bool ScriptEngine::isAudioComplete( const AsciiString& testAudio, Bool removeFromList )
{
	ListAsciiStringUINTIt findIt;
	for (findIt = m_testingAudio.begin(); findIt != m_testingAudio.end(); ++findIt) {
		if (findIt->first == testAudio) {
			break;
		}
	}

	if (findIt == m_testingAudio.end()) {
		PairAsciiStringUINT newPair;
		AudioEventRTS event(testAudio);
		Real audioLength = TheLogicAudio->getAudioLengthMS(&event);
		UnsignedInt frameCount = REAL_TO_UNSIGNEDINT(audioLength / MSEC_PER_LOGICFRAME_REAL);

		newPair.first = testAudio;
		newPair.second = frameCount + TheGameLogic->getFrame();
		m_testingAudio.push_front(newPair);
		findIt = m_testingAudio.begin();
	}

	if (TheGameLogic->getFrame() >= findIt->second) {
		// This audio is completed
		if (removeFromList) {
			m_testingAudio.erase(findIt);
		}
		return true;
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
/** Determine whether a special power has been started */
//-------------------------------------------------------------------------------------------------
Bool ScriptEngine::isSpecialPowerTriggered( Int playerIndex, const AsciiString& completedPower, Bool removeFromList, ObjectID sourceObj )
{
	if (playerIndex < 0 || playerIndex >= MAX_PLAYER_COUNT)
		return FALSE;

	ListAsciiStringObjectID *specialList = &(m_triggeredSpecialPowers[playerIndex]);

	for (ListAsciiStringObjectIDIt findIt = specialList->begin(); findIt != specialList->end(); ++findIt)
	{
		AsciiStringObjectIDPair pair = *findIt;
		if (pair.first == completedPower && (sourceObj == INVALID_ID || sourceObj == pair.second))
		{
			if (removeFromList) {
				specialList->erase(findIt);
			}
			return TRUE;
		}
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Determine whether a special power has reached a midpoint (not required for all special powers!) */
//-------------------------------------------------------------------------------------------------
Bool ScriptEngine::isSpecialPowerMidway( Int playerIndex, const AsciiString& completedPower, Bool removeFromList, ObjectID sourceObj )
{
	if (playerIndex < 0 || playerIndex >= MAX_PLAYER_COUNT)
		return FALSE;

	ListAsciiStringObjectID *specialList = &(m_midwaySpecialPowers[playerIndex]);

	for (ListAsciiStringObjectIDIt findIt = specialList->begin(); findIt != specialList->end(); ++findIt)
	{
		AsciiStringObjectIDPair pair = *findIt;
		if (pair.first == completedPower && (sourceObj == INVALID_ID || sourceObj == pair.second))
		{
			if (removeFromList) {
				specialList->erase(findIt);
			}
			return TRUE;
		}
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Determine whether a special power has been finished */
//-------------------------------------------------------------------------------------------------
Bool ScriptEngine::isSpecialPowerComplete( Int playerIndex, const AsciiString& completedPower, Bool removeFromList, ObjectID sourceObj )
{
	if (playerIndex < 0 || playerIndex >= MAX_PLAYER_COUNT)
		return FALSE;

	ListAsciiStringObjectID *specialList = &(m_finishedSpecialPowers[playerIndex]);

	for (ListAsciiStringObjectIDIt findIt = specialList->begin(); findIt != specialList->end(); ++findIt)
	{
		AsciiStringObjectIDPair pair = *findIt;
		if (pair.first == completedPower && (sourceObj == INVALID_ID || sourceObj == pair.second))
		{
			if (removeFromList) {
				specialList->erase(findIt);
			}
			return TRUE;
		}
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Determine whether an upgrade has been completed */
//-------------------------------------------------------------------------------------------------
Bool ScriptEngine::isUpgradeComplete( Int playerIndex, const AsciiString& upgrade, Bool removeFromList, ObjectID sourceObj )
{
	if (playerIndex < 0 || playerIndex >= MAX_PLAYER_COUNT)
		return FALSE;

	ListAsciiStringObjectID *specialList = &(m_completedUpgrades[playerIndex]);

	for (ListAsciiStringObjectIDIt findIt = specialList->begin(); findIt != specialList->end(); ++findIt)
	{
		AsciiStringObjectIDPair pair = *findIt;
		if (pair.first == upgrade && (sourceObj == INVALID_ID || sourceObj == pair.second))
		{
			if (removeFromList) {
				specialList->erase(findIt);
			}
			return TRUE;
		}
	}

	return FALSE;
}

#ifdef OLD_GENERALS_SPECIALIZATION
//-------------------------------------------------------------------------------------------------
/** Determine whether a general has been chosen */
//-------------------------------------------------------------------------------------------------
Bool ScriptEngine::isGeneralChosen( Int playerIndex, const AsciiString& generalName, Bool removeFromList, ObjectID sourceObj )
{
	if (playerIndex < 0 || playerIndex >= MAX_PLAYER_COUNT)
		return FALSE;

	ListAsciiStringObjectID *specialList = &(m_chosenGenerals[playerIndex]);

	for (ListAsciiStringObjectIDIt findIt = specialList->begin(); findIt != specialList->end(); ++findIt)
	{
		AsciiStringObjectIDPair pair = *findIt;
		if (pair.first == generalName && (sourceObj == INVALID_ID || sourceObj == pair.second))
		{
			if (removeFromList) {
				specialList->erase(findIt);
			}
			return TRUE;
		}
	}

	return FALSE;
}
#else
//-------------------------------------------------------------------------------------------------
/** Determine whether a science has been chosen */
//-------------------------------------------------------------------------------------------------
Bool ScriptEngine::isScienceAcquired( Int playerIndex, ScienceType science, Bool removeFromList )
{
	if (playerIndex < 0 || playerIndex >= MAX_PLAYER_COUNT)
		return FALSE;

	ScienceVec* specialList = &(m_acquiredSciences[playerIndex]);

	for (ScienceVec::iterator it = specialList->begin(); it != specialList->end(); ++it)
	{
		if (*it == science)
		{
			if (removeFromList) 
			{
				specialList->erase(it);
			}
			return TRUE;
		}
	}

	return FALSE;
}
#endif

//-------------------------------------------------------------------------------------------------
/** if the object has a specified topple direction, change it to direction. Otherwise add it to the
/** list. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::setToppleDirection( const AsciiString& objectName, const Coord3D *direction )
{
	if (objectName.isEmpty()) {
		return;
	}

	ListAsciiStringCoord3DIt it;
	for (it = m_toppleDirections.begin(); it != m_toppleDirections.end(); ++it) {
		if (it->first == objectName) {
			if (direction) {
				it->second = *direction;
			} else {
				m_toppleDirections.erase(it);
			}
			return;
		}
	}

	AsciiStringCoord3DPair newPair;
	newPair.first = objectName;
	newPair.second = (*direction);
	m_toppleDirections.push_front(newPair);
}

//-------------------------------------------------------------------------------------------------
/** if the object is named and has a specified topple direction, topple adjust direction to reflect
/** it. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::adjustToppleDirection( Object *object, Coord2D *direction)
{
	if (!(object && direction)) {
		return;
	}

	Coord3D dir;
	dir.x = (*direction).x;
	dir.y = (*direction).y;
	adjustToppleDirection(object, &dir);
	(*direction).x = dir.x;
	(*direction).y = dir.y;
}

//-------------------------------------------------------------------------------------------------
/** if the object is named and has a specified topple direction, topple adjust direction to reflect
/** it. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::adjustToppleDirection( Object *object, Coord3D *direction)
{
	AsciiString objName = object->getName();
	if (objName.isEmpty() || !direction) {
		return;
	}

	// this one *MAY* have a specified topple direction
	ListAsciiStringCoord3DIt it;
	for (it = m_toppleDirections.begin(); it != m_toppleDirections.end(); ++it) {
		if (it->first == objName) {
			(*direction) = it->second;
			(*direction).normalize();
			return;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Evaluates a list of conditions */
//-------------------------------------------------------------------------------------------------
Bool ScriptEngine::evaluateConditions( Script *pScript, Team *thisTeam, Player *player )
{
	LatchRestore<Team*> latch(m_callingTeam, thisTeam);
	if (thisTeam) player = thisTeam->getControllingPlayer();
	if (player==nullptr) player=m_currentPlayer;
	LatchRestore<Player*> latch2(m_currentPlayer, player);
	OrCondition *pConditionHead = pScript->getOrCondition();
	Bool testValue = false;

#ifdef DEBUG_LOGGING
#define COLLECT_CONDITION_EVAL_TIMES
#endif
#ifdef COLLECT_CONDITION_EVAL_TIMES
	__int64 startTime64;
	Real timeToEvaluate=0.0f;
	__int64 endTime64,freq64;
	QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER *>(&freq64));
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&startTime64));
#endif
	OrCondition *pCurCondition;
	for (pCurCondition = pConditionHead; pCurCondition; pCurCondition = pCurCondition->getNextOrCondition()) {
		Condition *pCondition = pCurCondition->getFirstAndCondition();
		if (!pCondition) continue; // No conditions, so go to the next or.
		Bool andTerm = true; 
		while (pCondition && andTerm) {
			if (!evaluateCondition(pCondition)) {
				andTerm = false;
				break; // Short circuit the and evauation - after the first false, we can quit.
			}
			pCondition = pCondition->getNext();
		}
		if (andTerm) { // The outer list is OR'ed - so any true inner means we are true.
			testValue = true;
			break;
		}
	}
#ifdef COLLECT_CONDITION_EVAL_TIMES
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&endTime64));
	timeToEvaluate = (static_cast<Real>(endTime64-startTime64) / static_cast<Real>(freq64));
	pScript->incrementConditionCount();
	pScript->addToConditionTime(timeToEvaluate);
#endif

	return testValue; // If none of the or's fired, then it is false.
}



//-------------------------------------------------------------------------------------------------
/** Execute a linked list of actions */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::executeActions( ScriptAction *pActionHead )
{
	ScriptAction *pCurAction;
	UnicodeString uStr1;
	for (pCurAction = pActionHead; pCurAction; pCurAction = pCurAction->getNext()) {
		switch (pCurAction->getActionType()) {
			default: if (TheScriptActions) TheScriptActions->executeAction(pCurAction); break;
			case ScriptAction::SET_COUNTER: setCounter(pCurAction);	break;
			case ScriptAction::SET_TREE_SWAY: setSway(pCurAction); break;
			case ScriptAction::INCREMENT_COUNTER: addCounter(pCurAction);	break;
			case ScriptAction::DECREMENT_COUNTER: subCounter(pCurAction);	break;
			case ScriptAction::SET_FLAG: setFlag(pCurAction);break;
			case ScriptAction::STOP_TIMER: pauseTimer(pCurAction);break;
			case ScriptAction::RESTART_TIMER: restartTimer(pCurAction);break;
			case ScriptAction::SET_TIMER: setTimer(pCurAction, false, false);break;
			case ScriptAction::SET_MILLISECOND_TIMER: setTimer(pCurAction, true, false);break;
			case ScriptAction::SET_RANDOM_TIMER: setTimer(pCurAction, false, true);break;
			case ScriptAction::SET_RANDOM_MSEC_TIMER: setTimer(pCurAction, true, true);break;
			case ScriptAction::ADD_TO_MSEC_TIMER: adjustTimer(pCurAction, true, true);break;
			case ScriptAction::SUB_FROM_MSEC_TIMER: adjustTimer(pCurAction, true, false);break;
			case ScriptAction::ENABLE_SCRIPT: enableScript(pCurAction);break;
			case ScriptAction::DISABLE_SCRIPT: disableScript(pCurAction);break;
			case ScriptAction::CALL_SUBROUTINE: callSubroutine(pCurAction);break;

			// Fade operations.
			case ScriptAction::CAMERA_FADE_ADD : 
			case ScriptAction::CAMERA_FADE_SUBTRACT : 
			case ScriptAction::CAMERA_FADE_SATURATE : 
			case ScriptAction::CAMERA_FADE_MULTIPLY : 
				setFade(pCurAction); break;

			// Attack priority set operations.
			case ScriptAction::SET_ATTACK_PRIORITY_THING : setPriorityThing(pCurAction); break;
			case ScriptAction::SET_ATTACK_PRIORITY_KIND_OF : setPriorityKind(pCurAction); break;
			case ScriptAction::SET_DEFAULT_ATTACK_PRIORITY : setPriorityDefault(pCurAction); break;

			case ScriptAction::NO_OP: /* just break. */; break;
		}
	}
}
																		
//-------------------------------------------------------------------------------------------------
/** Execute a linked list of scripts */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::executeScripts( Script *pScriptHead )
{

	// Evaluate the scripts.
	Script *pCurScript;
	for (pCurScript = pScriptHead; pCurScript; pCurScript=pCurScript->getNext()) {
		if (pCurScript->isSubroutine()) {
			continue; // Don't execute subroutines, except when called by other scripts.
		}
		executeScript(pCurScript);
	}
}  // end update


//-------------------------------------------------------------------------------------------------
/** Fills the named object cache initally. */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::createNamedCache( void )
{
	m_namedObjects.clear();

	if( !TheGameLogic )
	{
		return;
	}
	Object* pObj = TheGameLogic->getFirstObject();

	while (pObj) {
		if (!pObj->getName().isEmpty()) {
			NamedRequest req;
			req.first = pObj->getName();
			req.second = pObj;
			m_namedObjects.push_back(req);
		}
		pObj = pObj->getNextObject();
	}
}

void ScriptEngine::notifyOfObjectCreationOrDestruction(void)
{
	m_frameObjectCountChanged = TheGameLogic->getFrame();
}


//-------------------------------------------------------------------------------------------------
/** Is time frozen by a script? */
//-------------------------------------------------------------------------------------------------
Bool ScriptEngine::isTimeFrozenScript( void )
{
	return m_freezeByScript;
}

//-------------------------------------------------------------------------------------------------
/** Freeze time */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::doFreezeTime( void )
{
	m_freezeByScript = TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Unfreeze time */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::doUnfreezeTime( void )
{
	m_freezeByScript = FALSE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ScriptEngine::setGlobalDifficulty( GameDifficulty difficulty )
{
	DEBUG_LOG(("ScriptEngine::setGlobalDifficulty(%d)\n", ((Int)difficulty)));
	m_gameDifficulty = difficulty;
}

//#if defined(_DEBUG)
void ScriptEngine::debugVictory( void )
{
	ScriptAction *action = newInstance(ScriptAction)(ScriptAction::VICTORY);
	TheScriptActions->executeAction(action);
	deleteInstanceOf(action);
}
//#endif

Bool ScriptEngine::hasShownMPLocalDefeatWindow(void)
{
	return m_shownMPLocalDefeatWindow;
}

void ScriptEngine::markMPLocalDefeatWindowShown(void)
{
	m_shownMPLocalDefeatWindow = TRUE;
}


