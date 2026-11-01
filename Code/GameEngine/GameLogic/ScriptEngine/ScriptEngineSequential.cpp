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

// Sequential scripts: scripts that run their actions one after the other on a team or unit

enum { MAX_SPIN_COUNT = 20 };

void ScriptEngine::appendSequentialScript(const SequentialScript *scriptToSequence)
{
	SequentialScript *newSequentialScript = newInstance( SequentialScript );	
	(*newSequentialScript) = (*scriptToSequence);

	// Must set this to NULL, as we don't want an infinite loop.
	newSequentialScript->m_nextScriptInSequence = nullptr;

	// reset the instruction pointer
	newSequentialScript->m_currentInstruction = -1;
	
	VecSequentialScriptPtrIt it;
	Bool found = false;
	for (it = m_sequentialScripts.begin(); it != m_sequentialScripts.end(); ++it) {
		SequentialScript *seqScript = (*it);
		if (!seqScript) {
			continue;
		}

		if ((scriptToSequence->m_objectID && scriptToSequence->m_objectID == seqScript->m_objectID) ||
				 (scriptToSequence->m_teamToExecOn && scriptToSequence->m_teamToExecOn == seqScript->m_teamToExecOn)) {
			found = true;
			while (seqScript->m_nextScriptInSequence) { 
				seqScript = seqScript->m_nextScriptInSequence;
			}

			seqScript->m_nextScriptInSequence = newSequentialScript;
			break;
		}
	}

	if (!found) {
		m_sequentialScripts.push_back(newSequentialScript);
	}

	// do not delete either of these here. 
}

void ScriptEngine::removeSequentialScript(SequentialScript *scriptToRemove)
{
	
}

void ScriptEngine::removeAllSequentialScripts(Object *obj)
{
	if (!obj) {
		return;
	}

	ObjectID id = obj->getID();
	VecSequentialScriptPtrIt it;
	for (it = m_sequentialScripts.begin(); it != m_sequentialScripts.end(); /* empty */) {
		SequentialScript *seqScript = (*it);
		if (!seqScript) {
      ++it;
			continue;
		}
		if (seqScript->m_objectID == id) {
			it = cleanupSequentialScript(it, TRUE);
		}
    else
		  ++it;
	}
}

void ScriptEngine::removeAllSequentialScripts(Team *team)
{
	// this function will remove all pending scripts for this team, so just call it.
	notifyOfTeamDestruction(team);
}

void ScriptEngine::notifyOfTeamDestruction(Team *teamDestroyed)
{
	if (!teamDestroyed) {
		return;		
	}

	VecSequentialScriptPtrIt it;
	for (it = m_sequentialScripts.begin(); it != m_sequentialScripts.end(); /* empty */) {
		SequentialScript *seqScript = (*it);
		if (!seqScript) {
			continue;
		}
		
		if (seqScript->m_teamToExecOn == teamDestroyed) {
			it = cleanupSequentialScript(it, TRUE);
			continue;
		}
		++it;
	}

	if (m_callingTeam == teamDestroyed)
		m_callingTeam = nullptr;
	if (m_conditionTeam == teamDestroyed)
		m_conditionTeam = nullptr;
}

void ScriptEngine::setSequentialTimer(Object *obj, Int frameCount)
{
	if (!obj) {
		return;
	}

	ObjectID id = obj->getID();
	VecSequentialScriptPtrIt it;
	for (it = m_sequentialScripts.begin(); it != m_sequentialScripts.end(); ++it) {
		SequentialScript *seqScript = (*it);
		if (!seqScript) {
			continue;
		}

		if (seqScript->m_objectID == id) {
			// this is it.
			seqScript->m_framesToWait = frameCount;
			return;
		}
	}
}

void ScriptEngine::setSequentialTimer(Team *team, Int frameCount)
{
	if (!team) {
		return;
	}

	VecSequentialScriptPtrIt it;
	for (it = m_sequentialScripts.begin(); it != m_sequentialScripts.end(); ++it) {
		SequentialScript *seqScript = (*it);
		if (!seqScript) {
			continue;
		}

		if (seqScript->m_teamToExecOn == team) {
			// this is it.
			seqScript->m_framesToWait = frameCount;
			return;
		}
	}
}

void ScriptEngine::evaluateAndProgressAllSequentialScripts( void )
{
	// Index instead of iterator: executeActions() and appendSequentialScript() may grow
	// m_sequentialScripts with push_back (reallocation), which would invalidate iterators. After
	// erase() the index names the next element, just like the returned iterator, and a script
	// appended during the loop is still reached in the same frame.
	size_t i;
	size_t lastIndex = static_cast<size_t>(-1);

	Int spinCount = 0;
	for (i = 0; i < m_sequentialScripts.size(); /* empty */) {
		if (i == lastIndex) {
			++spinCount;
		} else {
			spinCount = 0;
		}

		if (spinCount > MAX_SPIN_COUNT) {
			SequentialScript *seqScript = m_sequentialScripts[i];
			if (seqScript) {
				DEBUG_LOG(("Sequential script %s appears to be in an infinite loop.\n", 
					seqScript->m_scriptToExecuteSequentially->getName().str()));
			}
			++i;
			continue;
		}

		lastIndex = i;
		
		Bool itAdvanced = false;

		SequentialScript *seqScript = m_sequentialScripts[i];
		if (seqScript == nullptr) {
			i = cleanupSequentialScript(m_sequentialScripts.begin() + i, false) - m_sequentialScripts.begin();
			continue;
		}

		Team *team = seqScript->m_teamToExecOn;
		Object *obj = TheGameLogic->findObjectByID(seqScript->m_objectID);
		if (!(obj || team)) {
			i = cleanupSequentialScript(m_sequentialScripts.begin() + i, false) - m_sequentialScripts.begin();
			itAdvanced = true;
			continue;
		}
		m_currentPlayer = nullptr;
		if (obj) {
			m_currentPlayer = obj->getControllingPlayer();
		} else if (team) {
			m_currentPlayer = team->getControllingPlayer();
		}
		if (m_currentPlayer && !m_currentPlayer->isSkirmishAIPlayer()) {
			m_currentPlayer = nullptr;
		}

		AIUpdateInterface *ai = obj ? obj->getAIUpdateInterface() : nullptr;
		AIGroup *aigroup = (team ? TheAI->createGroup() : nullptr);
		if (aigroup) {
			team->getTeamAsAIGroup(aigroup);
		}

		if( ai || aigroup ) {
			if (((ai && (ai->isIdle()) || (aigroup && aigroup->isIdle())) && 
				seqScript->m_framesToWait < 1) || (seqScript->m_framesToWait == 0)) {
				
				// We want to supress messages if we're repeatedly waiting for an event to occur, cause 
				// it KILLS our debug framerate.
				Bool displayMessage = TRUE;

				// Time to progress to the next task.
				if (seqScript->m_dontAdvanceInstruction) {
					seqScript->m_dontAdvanceInstruction = FALSE;
					displayMessage = FALSE;
				} else {
					++seqScript->m_currentInstruction;
				}

				AsciiString msg = "Advancing SeqScript '";
				msg.concat(seqScript->m_scriptToExecuteSequentially->getName());
				msg.concat("' on ");
				AsciiString name;
				if (team) name = team->getName();
				if (obj) name = obj->getName();
				msg.concat(name);
				msg.concat(" -- ");

				int instruction = seqScript->m_currentInstruction;
				ScriptAction *action = seqScript->m_scriptToExecuteSequentially->getAction();
				while (action && instruction) {
					--instruction;
					action = action->getNext();
				}

				if (action) {
					m_conditionTeam = team;
					m_conditionObject = obj;
					seqScript->m_framesToWait = -1;

					// Save off the next action
					ScriptAction *nextAction = action->getNext();
					action->setNextAction(nullptr);
					if (action->getActionType() == ScriptAction::SKIRMISH_WAIT_FOR_COMMANDBUTTON_AVAILABLE_ALL) {
						if (!TheScriptConditions->evaluateSkirmishCommandButtonIsReady(nullptr, action->getParameter(1), action->getParameter(2), true)) {
							seqScript->m_dontAdvanceInstruction = TRUE;
						}
					} else if (action->getActionType() == ScriptAction::SKIRMISH_WAIT_FOR_COMMANDBUTTON_AVAILABLE_PARTIAL) {
						if (!TheScriptConditions->evaluateSkirmishCommandButtonIsReady(nullptr, action->getParameter(1), action->getParameter(2), false)) {
							seqScript->m_dontAdvanceInstruction = TRUE;
						}
					} else if (action->getActionType() == ScriptAction::TEAM_WAIT_FOR_NOT_CONTAINED_ALL) {
						if (TheScriptConditions->evaluateTeamIsContained(action->getParameter(0), true)) {
							seqScript->m_dontAdvanceInstruction = TRUE;
						}
					} else if (action->getActionType() == ScriptAction::TEAM_WAIT_FOR_NOT_CONTAINED_PARTIAL) {
						if (TheScriptConditions->evaluateTeamIsContained(action->getParameter(0), false)) {
							seqScript->m_dontAdvanceInstruction = TRUE;
						}
					} else {
						executeActions(action);
					}

					if (displayMessage) {
						msg.concat(action->getUiText());
						AppendDebugMessage(msg, false);
					} else {
						msg.clear();
					}

					action->setNextAction(nextAction);
					
					// Check to see if executing our action told us to wait. If so, skip to the next Sequential script
					if (seqScript->m_dontAdvanceInstruction) {
						++i;
						itAdvanced = true;
						continue;
					}

					if (ai && ai->isIdle()) {
						// pretend like we've already advanced to allow multiple checks on this object this frame.
						itAdvanced = true;
					} else if (team) {
						// attempt to rebuild the aigroup, as it probably expired during the action execution
						aigroup = (team ? TheAI->createGroup() : nullptr);
						team->getTeamAsAIGroup(aigroup);
					}

					if (aigroup && aigroup->isIdle()) {
						// pretend like we've already advanced to allow multiple checks on this object this frame.
						itAdvanced = true;
					}

					if (itAdvanced) {	// check to make sure they aren't dead.
						if (obj && obj->isEffectivelyDead()) {
							i = cleanupSequentialScript(m_sequentialScripts.begin() + i, true) - m_sequentialScripts.begin();
							continue;
						}

						if (aigroup && aigroup->isGroupAiDead()) {
							i = cleanupSequentialScript(m_sequentialScripts.begin() + i, true) - m_sequentialScripts.begin();
							continue;
						}
					}
				} else {
					if (seqScript->m_timesToLoop != 0) {
						if (seqScript->m_timesToLoop != -1) {
							--seqScript->m_timesToLoop;
						}

						seqScript->m_framesToWait = -1;
						appendSequentialScript(seqScript);
					}

					i = cleanupSequentialScript(m_sequentialScripts.begin() + i, false) - m_sequentialScripts.begin();
					itAdvanced = true;
				}
			} else if (seqScript->m_framesToWait > 0) {
				--seqScript->m_framesToWait;
			}
		}

		if (!itAdvanced) {
			++i;
		}
	}
	m_currentPlayer = nullptr;
}

ScriptEngine::VecSequentialScriptPtrIt ScriptEngine::cleanupSequentialScript(VecSequentialScriptPtrIt it, Bool cleanDanglers)
{
	SequentialScript *seqScript;
	seqScript = (*it);
	if (!seqScript) {
		return it;				
	}

	SequentialScript *scriptToDelete = seqScript;
	if (cleanDanglers) {
		while (seqScript) {
			scriptToDelete = seqScript;
			seqScript = seqScript->m_nextScriptInSequence;
			deleteInstanceOf(scriptToDelete);
			scriptToDelete = nullptr;
		}
		(*it) = nullptr;
	} else {
		// we want to make sure to not delete any dangling scripts.
		(*it) = scriptToDelete->m_nextScriptInSequence;
		deleteInstanceOf(scriptToDelete);
		scriptToDelete = nullptr;
	}


	if ((*it) == nullptr) {
		return m_sequentialScripts.erase(it);
	}

	return it;
}

Bool ScriptEngine::hasUnitCompletedSequentialScript( Object *object, const AsciiString& sequentialScriptName )
{

	return FALSE;
}

Bool ScriptEngine::hasTeamCompletedSequentialScript( Team *team, const AsciiString& sequentialScriptName )
{

	return FALSE;
}

//----SequentialScript-----------------------------------------------------------------------------
SequentialScript::SequentialScript() : m_teamToExecOn(nullptr), 
																			 m_objectID(INVALID_ID), 
																			 m_scriptToExecuteSequentially(nullptr), 
																			 m_currentInstruction(START_INSTRUCTION),
																			 m_timesToLoop(0),
																			 m_framesToWait(-1),
																			 m_dontAdvanceInstruction(FALSE),
																			 m_nextScriptInSequence(nullptr)
{
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void SequentialScript::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void SequentialScript::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// team
	TeamID teamID = m_teamToExecOn ? m_teamToExecOn->getID() : TEAM_ID_INVALID;
	xfer->xferUser( &teamID, sizeof( TeamID ) );
	if( xfer->getXferMode() == XFER_LOAD )
	{

		// tie up pointer
		m_teamToExecOn = TheTeamFactory->findTeamByID( teamID );

		// sanity
		if( teamID != TEAM_ID_INVALID && m_teamToExecOn == nullptr )
		{

			DEBUG_CRASH(( "SequentialScript::xfer - Unable to find team by ID (#%d) for m_teamToExecOn\n", 
										teamID ));
			throw SaveCode::SC_INVALID_DATA;

		}  // end if

	}  // end if

	// object id
	xfer->xferObjectID( &m_objectID );

	// saving
	AsciiString scriptName;
	if( xfer->getXferMode() == XFER_SAVE )
	{

		// write script name
		scriptName = m_scriptToExecuteSequentially->getName();
		xfer->xferAsciiString( &scriptName );

	}  // end if, save
	else
	{

		// read script name
		xfer->xferAsciiString( &scriptName );

		// script pointer
		DEBUG_ASSERTCRASH( m_scriptToExecuteSequentially == nullptr, ("SequentialScript::xfer - m_scripttoExecuteSequentially\n") );
			
		// find script
		m_scriptToExecuteSequentially = const_cast<Script*>(TheScriptEngine->findScriptByName(scriptName));

		// sanity	
		DEBUG_ASSERTCRASH( m_scriptToExecuteSequentially != nullptr,
											 ("SequentialScript::xfer - m_scriptToExecuteSequentially is NULL but should not be\n") );

	}  // end else, load

	// current instruction
	xfer->xferInt( &m_currentInstruction );

	// times to loop
	xfer->xferInt( &m_timesToLoop );

	// frames to wait
	xfer->xferInt( &m_framesToWait );

	// dont advance instruction
	xfer->xferBool( &m_dontAdvanceInstruction );
	
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void SequentialScript::loadPostProcess( void )
{

}  // end loadPostProcess
