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

// Script debug window (DebugWindow.dll): messages, variables, pause and fast run

// These are for debugger window
int st_LastCurrentFrame;
int st_CurrentFrame;
Bool st_CanAppCont;
static Bool st_AppIsFast = false;
HMODULE st_DebugDLL;
// That's it for debugger window

//-------------------------------------------------------------------------------------------------
/** For Debug and Internal builds, returns whether to continue (!pause), for release, returns false */
//-------------------------------------------------------------------------------------------------
Bool ScriptEngine::isTimeFrozenDebug(void)
{
	using funcptr = Bool (*)(void);

	if (st_DebugDLL) {
		if (st_LastCurrentFrame != st_CurrentFrame) {
			st_LastCurrentFrame = st_CurrentFrame;

			FARPROC proc = GetProcAddress(st_DebugDLL, "CanAppContinue");
			if (proc) {
				st_CanAppCont = (reinterpret_cast<funcptr>(proc))();

				if (st_CanAppCont) {
				}
			}
		}
		return !st_CanAppCont;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
/** For Debug and Internal builds, returns whether we are running fast (skipping draw) */
//-------------------------------------------------------------------------------------------------
Bool ScriptEngine::isTimeFast(void)
{
	using funcptr = Bool (*)(void);

	if (st_DebugDLL) {
		FARPROC proc = GetProcAddress(st_DebugDLL, "CanAppContinue");
 		proc = GetProcAddress(st_DebugDLL, "RunAppFast");
		if (proc && (reinterpret_cast<funcptr>(proc))()) {
			st_AppIsFast = true;
		} else {
			if (st_AppIsFast) {
				st_AppIsFast = false;
			} 
		}
		if (st_AppIsFast) {
			if ((TheGameLogic->getFrame()%10) == 0) {
				return false;
			}	 
			return true;
		} else {
			return false;
		}
	}
	return false;
}

void ScriptEngine::forceUnfreezeTime(void)
{
	using funcptr = void (*)(void);

	if (st_DebugDLL) {
		FARPROC proc = GetProcAddress(st_DebugDLL, "ForceAppContinue");
		if (proc) {
			(reinterpret_cast<funcptr>(proc))();
		}
	}
}

void ScriptEngine::AppendDebugMessage(const AsciiString& strToAdd, Bool forcePause)
{
#ifdef INTENSE_DEBUG
	DEBUG_LOG(("-SCRIPT- %d %s\n", TheGameLogic->getFrame(), strToAdd.str()));
#endif
	using funcptr = void (*)(const char*);
	if (!st_DebugDLL) {
		return;
	}

	FARPROC proc;
	if (forcePause) {
		proc = GetProcAddress(st_DebugDLL, "AppendMessageAndPause");
	} else {
		proc = GetProcAddress(st_DebugDLL, "AppendMessage");
	}

	if (!proc) {
		return;
	}
	AsciiString msg;
	msg.format("%d ", TheGameLogic->getFrame());
	msg.concat(strToAdd);
	(reinterpret_cast<funcptr>(proc))(msg.str());
}

void ScriptEngine::AdjustDebugVariableData(const AsciiString& variableName, Int value, Bool forcePause)
{
	_adjustVariable(variableName, value, (forcePause != 0));
}

// ------------------------------------------------------------------------------------------------
/** Misc helper functions (ParticleEdit, etc) */
// ------------------------------------------------------------------------------------------------

void _appendMessage(const AsciiString& str, Bool isTrueMessage, Bool shouldPause)
{
	using funcptr = void (*)(const char*);

	AsciiString msg;
	msg.format("%d ", TheGameLogic->getFrame());
	if (isTrueMessage) {
		msg.concat("Run script - ");
	} else {
		msg.concat("Run script false -");
	}
	msg.concat(str);

#ifdef INTENSE_DEBUG
	DEBUG_LOG(("-SCRIPT- %s\n", msg.str()));
#endif
	// With the environment variable GENERALS_SCRIPT_TRACE set, every executed script action is
	// written to that file as "Frame T|F script name" (T = actions, F = false actions). Read only;
	// the logic is not affected.
	{
		static FILE *s_scriptTrace = nullptr;
		static Bool s_scriptTraceChecked = FALSE;
		if (!s_scriptTraceChecked)
		{
			s_scriptTraceChecked = TRUE;
			const char *traceFile = getenv("GENERALS_SCRIPT_TRACE");
			if (traceFile && *traceFile)
				s_scriptTrace = fopen(traceFile, "w");
		}
		if (s_scriptTrace)
		{
			fprintf(s_scriptTrace, "%u %c %s\n", TheGameLogic->getFrame(), isTrueMessage ? 'T' : 'F', str.str());
			fflush(s_scriptTrace);
		}
	}
	if (!st_DebugDLL) {
		return;
	}

	FARPROC proc;
	if (shouldPause) {
		proc = GetProcAddress(st_DebugDLL, "AppendMessageAndPause");
	} else {
		proc = GetProcAddress(st_DebugDLL, "AppendMessage");
	}
	if (!proc) {
		return;
	}

	(reinterpret_cast<funcptr>(proc))(msg.str());
}

void _adjustVariable(const AsciiString& str, Int value, Bool shouldPause)
{
	using funcptr = void (*)(const char*, const char*);
	if (!st_DebugDLL) {
		return;
	}

	FARPROC proc;
	if (shouldPause) {
		proc = GetProcAddress(st_DebugDLL, "AdjustVariableAndPause");
	} else {
		proc = GetProcAddress(st_DebugDLL, "AdjustVariable");
	}

	if (!proc) {
		return;
	}

	char buff[12];	// for sprintf
	WWFormat::Into(buff, "{:d}", value);

	(reinterpret_cast<funcptr>(proc))(str.str(), buff);
}

void _updateFrameNumber( void )
{
	if (TheScriptEngine->isTimeFast()) return;
	using funcptr = void (*)(int);
	if (!st_DebugDLL) {
		return;
	}

	FARPROC proc;
	proc = GetProcAddress(st_DebugDLL, "SetFrameNumber");
	if (!proc) {
		return;
	}

	UnsignedInt frameNum = TheGameLogic->getFrame();

	(reinterpret_cast<funcptr>(proc))(frameNum);
}
