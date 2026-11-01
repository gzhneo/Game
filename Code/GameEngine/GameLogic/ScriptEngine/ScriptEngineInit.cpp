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

// Script engine setup: the action and condition templates and their INI overrides

// ScriptEngine class
static const FieldParse TheTemplateFieldParseTable[] = 
{																	 
	{ "InternalName",	INI::parseAsciiString,nullptr,		offsetof( Template, m_internalName ) },
	{ "UIName",				INI::parseAsciiString,nullptr,		offsetof( Template, m_uiName ) },
	{ "UIName2",			INI::parseAsciiString,nullptr,		offsetof( Template, m_uiName2 ) },
	{ "HelpText",			INI::parseAsciiString,nullptr,		offsetof( Template, m_helpText ) },
	{ nullptr,						nullptr,									nullptr, 0 }  // keep this last
};

//-------------------------------------------------------------------------------------------------
/** Parse script action entry.  The InternalName has to match the action's internal name, and then it
overrides the ui name and help text.  If no entry is present in the ini file, the default code 
initialized value in ScriptEngine::init() is used. jba*/
//-------------------------------------------------------------------------------------------------
void ScriptEngine::parseScriptAction( INI* ini )
{
	// parse the ini weapon definition
	ActionTemplate scriptTemplate;
	ini->initFromINI( &scriptTemplate, TheTemplateFieldParseTable );
	if (TheScriptEngine) {
		TheScriptEngine->addActionTemplateInfo(&scriptTemplate);
	}
}

//-------------------------------------------------------------------------------------------------
/** Update the info from a script action's .ini entry.  The InternalName has to match the action's 
internal name, and then it
overrides the ui name and help text.  If no entry is present in the ini file, the default code 
initialized value in ScriptEngine::init() is used. jba*/
//-------------------------------------------------------------------------------------------------
void ScriptEngine::addActionTemplateInfo( Template *actionTemplate)
{
	Int i;
	for (i=0; i<ScriptAction::NUM_ITEMS; i++) {
		if (m_actionTemplates[i].m_internalName == actionTemplate->m_internalName) {
			m_actionTemplates[i].m_uiName = actionTemplate->m_uiName;
			m_actionTemplates[i].m_uiName2 = actionTemplate->m_uiName2;
			m_actionTemplates[i].m_helpText = actionTemplate->m_helpText;
			return;
		}
	}
	DEBUG_LOG(("Couldn't find script action named %s\n", actionTemplate->m_internalName.str()));
}

//-------------------------------------------------------------------------------------------------
/** Parse script condition entry.  The InternalName has to match the condition's internal name, and then it
overrides the ui name and help text.  If no entry is present in the ini file, the default code 
initialized value in ScriptEngine::init() is used. jba*/
//-------------------------------------------------------------------------------------------------
void ScriptEngine::parseScriptCondition( INI* ini )
{
	// parse the ini weapon definition			 
	ActionTemplate scriptTemplate;
	ini->initFromINI( &scriptTemplate, TheTemplateFieldParseTable );
	if (TheScriptEngine) {
		TheScriptEngine->addConditionTemplateInfo(&scriptTemplate);
	}
}

//-------------------------------------------------------------------------------------------------
/** Update the info from a script action's .ini entry.  The InternalName has to match the action's 
internal name, and then it
overrides the ui name and help text.  If no entry is present in the ini file, the default code 
initialized value in ScriptEngine::init() is used. jba*/
//-------------------------------------------------------------------------------------------------
void ScriptEngine::addConditionTemplateInfo( Template *actionTemplate)
{
	Int i;
	for (i=0; i<Condition::NUM_ITEMS; i++) {
		if (m_conditionTemplates[i].m_internalName == actionTemplate->m_internalName) {
			m_conditionTemplates[i].m_uiName = actionTemplate->m_uiName;
			m_conditionTemplates[i].m_uiName2 = actionTemplate->m_uiName2;
			m_conditionTemplates[i].m_helpText = actionTemplate->m_helpText;
			return;
		}
	}
	DEBUG_LOG(("Couldn't find script condition named %s", actionTemplate->m_internalName.str()));
}

//-------------------------------------------------------------------------------------------------
/** Init */
//-------------------------------------------------------------------------------------------------
void ScriptEngine::init( void )
{
	if (TheGlobalData->m_windowed) {
		if (TheGlobalData->m_scriptDebug) {
			st_DebugDLL = LoadLibrary("DebugWindow.dll");
		} else {
			st_DebugDLL = nullptr;
		}
		
		if (st_DebugDLL) {
			FARPROC proc = GetProcAddress(st_DebugDLL, "CreateDebugDialog");
			if (proc) {
				proc();
			}
		}
	}


#ifdef SPECIAL_SCRIPT_PROFILING
#ifdef DEBUG_LOGGING
	m_numFrames=0;
	m_totalUpdateTime=0;
	m_maxUpdateTime=0;
#endif
#endif
	
	if (TheScriptActions) {
		TheScriptActions->init();
	}
	if (TheScriptConditions) {
		TheScriptConditions->init();
	}
	/* Recipe for adding an action:
			1. In Scripts.h, add an enum element to enum ScriptActionType just before NUM_ITEMS.
			2. Go to the end of this section of templates, and create a template.
			3. Go to ScriptActions.h and add a protected method.
			4. Go to ScriptActions.cpp, and add your enum to the 
					switch in ScriptActions::executeAction to call your method in 3 above.
	*/

	// Set up the script action templates.
	Template *curTemplate = &m_actionTemplates[ScriptAction::DEBUG_MESSAGE_BOX];
	curTemplate->m_internalName = "DEBUG_MESSAGE_BOX";
	curTemplate->m_uiName = "Scripting_/Debug/Display message and pause";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEXT_STRING;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Show debug string and pause: ";

	curTemplate = &m_actionTemplates[ScriptAction::DEBUG_STRING];
	curTemplate->m_internalName = "DEBUG_STRING";
	curTemplate->m_uiName = "Scripting_/Debug/Display string";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEXT_STRING;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Show debug string without pausing: ";

	curTemplate = &m_actionTemplates[ScriptAction::DEBUG_CRASH_BOX];
	curTemplate->m_internalName = "DEBUG_CRASH_BOX";
	curTemplate->m_uiName = "{INTERNAL}_/Debug/Display a crash box (debug/internal builds only).";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEXT_STRING;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Display a crash box with the text: ";

	curTemplate = &m_actionTemplates[ScriptAction::SET_FLAG];
	curTemplate->m_internalName = "SET_FLAG";
	curTemplate->m_uiName = "Scripting_/Flags/Set flag to value";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::FLAG;
	curTemplate->m_parameters[1] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Set ";
	curTemplate->m_uiStrings[1] = " to ";

	curTemplate = &m_actionTemplates[ScriptAction::SET_COUNTER];
	curTemplate->m_internalName = "SET_COUNTER";
	curTemplate->m_uiName = "Scripting_/Counters/Set counter to a value";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::COUNTER;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Set ";
	curTemplate->m_uiStrings[1] = " to ";

	curTemplate = &m_actionTemplates[ScriptAction::SET_TREE_SWAY];
	curTemplate->m_internalName = "SET_TREE_SWAY";
	curTemplate->m_uiName = "Map_/Environment/Set wind sway amount and direction.";
	curTemplate->m_numParameters = 5;
	curTemplate->m_parameters[0] = Parameter::ANGLE;
	curTemplate->m_parameters[1] = Parameter::ANGLE;
	curTemplate->m_parameters[2] = Parameter::ANGLE;
	curTemplate->m_parameters[3] = Parameter::INT;
	curTemplate->m_parameters[4] = Parameter::REAL;
	curTemplate->m_numUiStrings = 6;
	curTemplate->m_uiStrings[0] = "Set wind direction to ";
	curTemplate->m_uiStrings[1] = ", amount to sway ";
	curTemplate->m_uiStrings[2] = ", amount to lean with the wind ";
	curTemplate->m_uiStrings[3] = ", frames to take to sway once ";
	curTemplate->m_uiStrings[4] = ", randomness ";
	curTemplate->m_uiStrings[5] = "(0=lock step, 1=large random variation).";

	curTemplate = &m_actionTemplates[ScriptAction::SET_INFANTRY_LIGHTING_OVERRIDE];
	curTemplate->m_internalName = "SET_INFANTRY_LIGHTING_OVERRIDE";
	curTemplate->m_uiName = "Map_/Environment/Infantry Lighting - Set.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Set lighting percent on infantry to ";
	curTemplate->m_uiStrings[1] = " (0.0==min, 1.0==normal day, 2.0==max (which is normal night).)";

	curTemplate = &m_actionTemplates[ScriptAction::RESET_INFANTRY_LIGHTING_OVERRIDE];
	curTemplate->m_internalName = "RESET_INFANTRY_LIGHTING_OVERRIDE";
	curTemplate->m_uiName = "Map_/Environment/Infantry Lighting - Reset.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Reset infantry lighting to the normal setting. 1.0 for the two day states, 2.0 for the two night states. (Look in GamesData.ini)";

	curTemplate = &m_actionTemplates[ScriptAction::QUICKVICTORY];
	curTemplate->m_internalName = "QUICKVICTORY";
	curTemplate->m_uiName = "User_/ Announce quick win";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "End game in victory immediately.";

	curTemplate = &m_actionTemplates[ScriptAction::VICTORY];
	curTemplate->m_internalName = "VICTORY";
	curTemplate->m_uiName = "User_/ Announce win";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Announce win.";

	curTemplate = &m_actionTemplates[ScriptAction::DEFEAT];
	curTemplate->m_internalName = "DEFEAT";
	curTemplate->m_uiName = "User_/ Announce lose";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Announce lose.";

	curTemplate = &m_actionTemplates[ScriptAction::NO_OP];
	curTemplate->m_internalName = "NO_OP";
	curTemplate->m_uiName = "Scripting_/Debug/Null operation.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Null operation. (Does nothing.)";

	curTemplate = &m_actionTemplates[ScriptAction::SET_TIMER];
	curTemplate->m_internalName = "SET_TIMER";
	curTemplate->m_uiName = "Scripting_/Timer/Frame countdown timer -- set.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::COUNTER;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Set timer ";
	curTemplate->m_uiStrings[1] = " to expire in ";
	curTemplate->m_uiStrings[2] = " frames.";

	curTemplate = &m_actionTemplates[ScriptAction::SET_RANDOM_TIMER];
	curTemplate->m_internalName = "SET_RANDOM_TIMER";
	curTemplate->m_uiName = "Scripting_/Timer/Frame countdown timer -- set random.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::COUNTER;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "Set timer ";
	curTemplate->m_uiStrings[1] = " to expire between ";
	curTemplate->m_uiStrings[2] = " and ";
	curTemplate->m_uiStrings[3] = " frames.";

	curTemplate = &m_actionTemplates[ScriptAction::STOP_TIMER];
	curTemplate->m_internalName = "STOP_TIMER";
	curTemplate->m_uiName = "Scripting_/Timer/Timer -- stop.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::COUNTER;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Stop timer ";

	curTemplate = &m_actionTemplates[ScriptAction::RESTART_TIMER];
	curTemplate->m_internalName = "RESTART_TIMER";
	curTemplate->m_uiName = "Scripting_/Timer/Timer -- restart stopped.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::COUNTER;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Restart timer ";

	curTemplate = &m_actionTemplates[ScriptAction::PLAY_SOUND_EFFECT];
	curTemplate->m_internalName = "PLAY_SOUND_EFFECT";
	curTemplate->m_uiName = "Multimedia_/Sound Effect/Play sound effect.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SOUND;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Play ";
	curTemplate->m_uiStrings[1] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::ENABLE_SCRIPT];
	curTemplate->m_internalName = "ENABLE_SCRIPT";
	curTemplate->m_uiName = "Scripting_/Script/Enable Script.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SCRIPT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Enable ";
	curTemplate->m_uiStrings[1] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::DISABLE_SCRIPT];
	curTemplate->m_internalName = "DISABLE_SCRIPT";
	curTemplate->m_uiName = "Scripting_/Script/Disable script.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SCRIPT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Disable ";
	curTemplate->m_uiStrings[1] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::CALL_SUBROUTINE];
	curTemplate->m_internalName = "CALL_SUBROUTINE";
	curTemplate->m_uiName = "Scripting_/Script/Run subroutine script.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SCRIPT_SUBROUTINE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Run ";
	curTemplate->m_uiStrings[1] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::PLAY_SOUND_EFFECT_AT];
	curTemplate->m_internalName = "PLAY_SOUND_EFFECT_AT";
	curTemplate->m_uiName = "Multimedia_/Sound Effect/Play sound effect at waypoint.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SOUND;
	curTemplate->m_parameters[1] = Parameter::WAYPOINT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Play ";
	curTemplate->m_uiStrings[1] = " at ";
	curTemplate->m_uiStrings[2] = ".";

 	curTemplate = &m_actionTemplates[ScriptAction::DAMAGE_MEMBERS_OF_TEAM];
	curTemplate->m_internalName = "DAMAGE_MEMBERS_OF_TEAM";
	curTemplate->m_uiName = "Team_/Damage/Damage the members of a team.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Damage ";
	curTemplate->m_uiStrings[1] = ", amount=";
	curTemplate->m_uiStrings[2] = " (-1==kill).";

 	curTemplate = &m_actionTemplates[ScriptAction::MOVE_TEAM_TO];
	curTemplate->m_internalName = "MOVE_TEAM_TO";
	curTemplate->m_uiName = "Team_/Move/Set to move to a location.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::WAYPOINT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Move ";
	curTemplate->m_uiStrings[1] = " to ";
	curTemplate->m_uiStrings[2] = ".";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_FOLLOW_WAYPOINTS];
	curTemplate->m_internalName = "TEAM_FOLLOW_WAYPOINTS";
	curTemplate->m_uiName = "Team_/Move/Set to follow a waypoint path.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::WAYPOINT_PATH;
	curTemplate->m_parameters[2] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Have ";
	curTemplate->m_uiStrings[1] = " follow ";
	curTemplate->m_uiStrings[2] = " , as a team is ";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_FOLLOW_WAYPOINTS_EXACT];
	curTemplate->m_internalName = "TEAM_FOLLOW_WAYPOINTS_EXACT";
	curTemplate->m_uiName = "Team_/Move/Set to EXACTLY follow a waypoint path.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::WAYPOINT_PATH;
	curTemplate->m_parameters[2] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Have ";
	curTemplate->m_uiStrings[1] = " EXACTLY follow ";
	curTemplate->m_uiStrings[2] = " , as a team is ";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_WANDER_IN_PLACE];
	curTemplate->m_internalName = "TEAM_WANDER_IN_PLACE";
	curTemplate->m_uiName = "Team_/Move/Set to wander around current location.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Have ";
	curTemplate->m_uiStrings[1] = " wander around it's current location.";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_INCREASE_PRIORITY];
	curTemplate->m_internalName = "TEAM_INCREASE_PRIORITY";
	curTemplate->m_uiName = "Team_/AI/Increase priority by Success Priority Increase amount.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Increase the AI priority for";
	curTemplate->m_uiStrings[1] = "  by its Success Priority Increase amount.";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_DECREASE_PRIORITY];
	curTemplate->m_internalName = "TEAM_DECREASE_PRIORITY";
	curTemplate->m_uiName = "Team_/AI/Reduce priority by Failure Priority Decrease amount.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Reduce the AI priority for";
	curTemplate->m_uiStrings[1] = "  by its Failure Priority Decrease amount.";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_WANDER];
	curTemplate->m_internalName = "TEAM_WANDER";
	curTemplate->m_uiName = "Team_/Move/Set to follow a waypoint path -- wander.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::WAYPOINT_PATH;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Have ";
	curTemplate->m_uiStrings[1] = " wander along ";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_PANIC];
	curTemplate->m_internalName = "TEAM_PANIC";
	curTemplate->m_uiName = "Team_/Move/Set to follow a waypoint path -- panic.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::WAYPOINT_PATH;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Have ";
	curTemplate->m_uiStrings[1] = " move in panic along ";

	curTemplate = &m_actionTemplates[ScriptAction::MOVE_NAMED_UNIT_TO];
	curTemplate->m_internalName = "MOVE_NAMED_UNIT_TO";
	curTemplate->m_uiName = "Unit_/Move/Move a specific unit to a location.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::WAYPOINT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Move ";
	curTemplate->m_uiStrings[1] = " to ";
	curTemplate->m_uiStrings[2] = ".";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_SET_STATE];
	curTemplate->m_internalName = "TEAM_SET_STATE";
	curTemplate->m_uiName = "Team_/Misc/Team custom state - set state.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::TEAM_STATE;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Set ";
	curTemplate->m_uiStrings[1] = " to ";
	curTemplate->m_uiStrings[2] = ".";

 	curTemplate = &m_actionTemplates[ScriptAction::CREATE_REINFORCEMENT_TEAM];
	curTemplate->m_internalName = "CREATE_REINFORCEMENT_TEAM";
	curTemplate->m_uiName = "Team_/ Spawn a reinforcement team.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::WAYPOINT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Spawn an instance of ";
	curTemplate->m_uiStrings[1] = " at ";
	curTemplate->m_uiStrings[2] = ".";

 	curTemplate = &m_actionTemplates[ScriptAction::SKIRMISH_BUILD_BUILDING];
	curTemplate->m_internalName = "SKIRMISH_BUILD_BUILDING";
	curTemplate->m_uiName = "Skirmish Only_/ Build a building.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::OBJECT_TYPE;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Build a building of type ";
	curTemplate->m_uiStrings[1] = ".";

  curTemplate = &m_actionTemplates[ScriptAction::AI_PLAYER_BUILD_SUPPLY_CENTER];
	curTemplate->m_internalName = "AI_PLAYER_BUILD_SUPPLY_CENTER";
	curTemplate->m_uiName = "Player_/AI/AI player build near a supply source.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::OBJECT_TYPE;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "Have AI ";
	curTemplate->m_uiStrings[1] = " build a ";
	curTemplate->m_uiStrings[2] = " near a supply src with at least ";
	curTemplate->m_uiStrings[3] = " available resources.";

  curTemplate = &m_actionTemplates[ScriptAction::AI_PLAYER_BUILD_TYPE_NEAREST_TEAM];
	curTemplate->m_internalName = "AI_PLAYER_BUILD_TYPE_NEAREST_TEAM";
	curTemplate->m_uiName = "Player_/AI/AI player build nearest specified team.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::OBJECT_TYPE;
	curTemplate->m_parameters[2] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "Have AI ";
	curTemplate->m_uiStrings[1] = " build a ";
	curTemplate->m_uiStrings[2] = " nearest team ";
	curTemplate->m_uiStrings[3] = ".";

  curTemplate = &m_actionTemplates[ScriptAction::TEAM_GUARD_SUPPLY_CENTER];
	curTemplate->m_internalName = "TEAM_GUARD_SUPPLY_CENTER";
	curTemplate->m_uiName = "Team_/Guard/Set to guard a supply source.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;									  
	curTemplate->m_uiStrings[0] = "Have Team ";
	curTemplate->m_uiStrings[1] = " guard attacked or closest supply src with at least ";
	curTemplate->m_uiStrings[2] = " available resources";

  curTemplate = &m_actionTemplates[ScriptAction::AI_PLAYER_BUILD_UPGRADE];
	curTemplate->m_internalName = "AI_PLAYER_BUILD_UPGRADE";
	curTemplate->m_uiName = "Player_/AI/AI player build an upgrade.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::UPGRADE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Have AI ";
	curTemplate->m_uiStrings[1] = " build this upgrade: ";

	curTemplate = &m_actionTemplates[ScriptAction::SKIRMISH_FOLLOW_APPROACH_PATH	];
	curTemplate->m_internalName = "SKIRMISH_FOLLOW_APPROACH_PATH";
	curTemplate->m_uiName = "Skirmish Only_/Move/Team follow approach path.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::SKIRMISH_WAYPOINT_PATH;
	curTemplate->m_parameters[2] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Have ";
	curTemplate->m_uiStrings[1] = " approach the enemy using path ";
	curTemplate->m_uiStrings[2] = ", as a team is ";

 	curTemplate = &m_actionTemplates[ScriptAction::SKIRMISH_MOVE_TO_APPROACH_PATH	];
	curTemplate->m_internalName = "SKIRMISH_MOVE_TO_APPROACH_PATH";
	curTemplate->m_uiName = "Skirmish Only_/Move/Team move to approach path.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::SKIRMISH_WAYPOINT_PATH;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Have ";
	curTemplate->m_uiStrings[1] = " move to the start of enemy path ";
	curTemplate->m_uiStrings[2] = ".";

 	curTemplate = &m_actionTemplates[ScriptAction::SKIRMISH_BUILD_BASE_DEFENSE_FRONT];
	curTemplate->m_internalName = "SKIRMISH_BUILD_BASE_DEFENSE_FRONT";
	curTemplate->m_uiName = "Skirmish Only_/Build/Build base defense on front perimeter.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Build one additional perimeter base defenses, on the front.";

 	curTemplate = &m_actionTemplates[ScriptAction::SKIRMISH_BUILD_BASE_DEFENSE_FLANK];
	curTemplate->m_internalName = "SKIRMISH_BUILD_BASE_DEFENSE_FLANK";
	curTemplate->m_uiName = "Skirmish Only_/Build/Build base defense on flank perimeter.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Build one additional perimeter base defenses, on the flank.";

 	curTemplate = &m_actionTemplates[ScriptAction::SKIRMISH_BUILD_STRUCTURE_FRONT];
	curTemplate->m_internalName = "SKIRMISH_BUILD_STRUCTURE_FRONT";
	curTemplate->m_uiName = "Skirmish Only_/Build/Build structure on front perimeter.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::OBJECT_TYPE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Build one additional ";
	curTemplate->m_uiStrings[1] = ", on the front.";

 	curTemplate = &m_actionTemplates[ScriptAction::SKIRMISH_BUILD_STRUCTURE_FLANK];
	curTemplate->m_internalName = "SKIRMISH_BUILD_STRUCTURE_FLANK";
	curTemplate->m_uiName = "Skirmish Only_/Build/Build structure on flank perimeter.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::OBJECT_TYPE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Build one additional ";
	curTemplate->m_uiStrings[1] = ", on the flank.";

 	curTemplate = &m_actionTemplates[ScriptAction::RECRUIT_TEAM];
	curTemplate->m_internalName = "RECRUIT_TEAM";
	curTemplate->m_uiName = "Team_/Create/Recruit a team.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Recruit an instance of ";
	curTemplate->m_uiStrings[1] = ", maximum recruiting distance (feet):";
	curTemplate->m_uiStrings[2] = ".";

 	curTemplate = &m_actionTemplates[ScriptAction::MOVE_CAMERA_TO];
	curTemplate->m_internalName = "MOVE_CAMERA_TO";
	curTemplate->m_uiName = "Camera_/Move/Move the camera to a location.";
	curTemplate->m_numParameters = 5;
	curTemplate->m_parameters[0] = Parameter::WAYPOINT;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::REAL;
	curTemplate->m_parameters[3] = Parameter::REAL;
	curTemplate->m_parameters[4] = Parameter::REAL;
	curTemplate->m_numUiStrings = 6;
	curTemplate->m_uiStrings[0] = "Move camera to ";
	curTemplate->m_uiStrings[1] = " in ";
	curTemplate->m_uiStrings[2] = " seconds, camera shutter ";
	curTemplate->m_uiStrings[3] = " seconds, ease-in ";
	curTemplate->m_uiStrings[4] = " seconds, ease-out ";
	curTemplate->m_uiStrings[5] = " seconds.";

 	curTemplate = &m_actionTemplates[ScriptAction::ZOOM_CAMERA];
	curTemplate->m_internalName = "ZOOM_CAMERA";
	curTemplate->m_uiName = "Camera_/Adjust/Change the camera zoom.";
	curTemplate->m_numParameters = 4;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::REAL;
	curTemplate->m_parameters[3] = Parameter::REAL;
	curTemplate->m_numUiStrings = 5;
	curTemplate->m_uiStrings[0] = "Change camera zoom to ";
	curTemplate->m_uiStrings[1] = " in ";
	curTemplate->m_uiStrings[2] = " seconds, ease-in ";
	curTemplate->m_uiStrings[3] = " seconds, ease-out ";
	curTemplate->m_uiStrings[4] = " seconds.";

 	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_FADE_ADD];
	curTemplate->m_internalName = "CAMERA_FADE_ADD";
	curTemplate->m_uiName = "Camera_/Fade Effects/Fade using an add blend to white.";
	curTemplate->m_numParameters = 5;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_parameters[3] = Parameter::INT;
	curTemplate->m_parameters[4] = Parameter::INT;
	curTemplate->m_numUiStrings = 6;
	curTemplate->m_uiStrings[0] = "Fade (0-1) from ";
	curTemplate->m_uiStrings[1] = " to ";
	curTemplate->m_uiStrings[2] = " adding toward white. Take ";
	curTemplate->m_uiStrings[3] = " frames to increase, hold for ";
	curTemplate->m_uiStrings[4] = " fames, and decrease ";
	curTemplate->m_uiStrings[5] = " frames.";

 	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_FADE_SUBTRACT];
	curTemplate->m_internalName = "CAMERA_FADE_SUBTRACT";
	curTemplate->m_uiName = "Camera_/Fade Effects/Fade using a subtractive blend to black.";
	curTemplate->m_numParameters = 5;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_parameters[3] = Parameter::INT;
	curTemplate->m_parameters[4] = Parameter::INT;
	curTemplate->m_numUiStrings = 6;
	curTemplate->m_uiStrings[0] = "Fade (0-1) from ";
	curTemplate->m_uiStrings[1] = " to ";
	curTemplate->m_uiStrings[2] = " subtracting toward black. Take ";
	curTemplate->m_uiStrings[3] = " frames to increase, hold for ";
	curTemplate->m_uiStrings[4] = " fames, and decrease ";
	curTemplate->m_uiStrings[5] = " frames.";

 	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_FADE_MULTIPLY];
	curTemplate->m_internalName = "CAMERA_FADE_MULTIPLY";
	curTemplate->m_uiName = "Camera_/Fade Effects/Fade using a multiply blend to black.";
	curTemplate->m_numParameters = 5;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_parameters[3] = Parameter::INT;
	curTemplate->m_parameters[4] = Parameter::INT;
	curTemplate->m_numUiStrings = 6;
	curTemplate->m_uiStrings[0] = "Fade (1-0) from ";
	curTemplate->m_uiStrings[1] = " to ";
	curTemplate->m_uiStrings[2] = " multiplying toward black. Take ";
	curTemplate->m_uiStrings[3] = " frames to increase, hold for ";
	curTemplate->m_uiStrings[4] = " fames, and decrease ";
	curTemplate->m_uiStrings[5] = " frames.";

 	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_FADE_SATURATE];
	curTemplate->m_internalName = "CAMERA_FADE_SATURATE";
	curTemplate->m_uiName = "Camera_/Fade Effects/Fade using a saturate blend.";
	curTemplate->m_numParameters = 5;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_parameters[3] = Parameter::INT;
	curTemplate->m_parameters[4] = Parameter::INT;
	curTemplate->m_numUiStrings = 6;
	curTemplate->m_uiStrings[0] = "Fade (0.5-1) from ";
	curTemplate->m_uiStrings[1] = " to ";
	curTemplate->m_uiStrings[2] = " increasing saturation. Take ";
	curTemplate->m_uiStrings[3] = " frames to increase, hold for ";
	curTemplate->m_uiStrings[4] = " fames, and decrease ";
	curTemplate->m_uiStrings[5] = " frames.";

	curTemplate = &m_actionTemplates[ScriptAction::PITCH_CAMERA];
	curTemplate->m_internalName = "PITCH_CAMERA";
	curTemplate->m_uiName = "Camera_/Adjust/Change the camera pitch.";
	curTemplate->m_numParameters = 4;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::REAL;
	curTemplate->m_parameters[3] = Parameter::REAL;
	curTemplate->m_numUiStrings = 5;
	curTemplate->m_uiStrings[0] = "Change camera pitch to ";
	curTemplate->m_uiStrings[1] = " in ";
	curTemplate->m_uiStrings[2] = " seconds, ease-in ";
	curTemplate->m_uiStrings[3] = " seconds, ease-out ";
	curTemplate->m_uiStrings[4] = " seconds.";

 	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_FOLLOW_NAMED];
	curTemplate->m_internalName = "CAMERA_FOLLOW_NAMED";
	curTemplate->m_uiName = "Camera_/Move/Follow a specific unit.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Have the camera follow ";
	curTemplate->m_uiStrings[1] = ".  Snap camera to object is ";
	curTemplate->m_uiStrings[2] = ".";

 	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_STOP_FOLLOW];
	curTemplate->m_internalName = "CAMERA_STOP_FOLLOW";
	curTemplate->m_uiName = "Camera_/Move/Stop following any units.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Stop following any units.";

	curTemplate = &m_actionTemplates[ScriptAction::SETUP_CAMERA];
	curTemplate->m_internalName = "SETUP_CAMERA";
	curTemplate->m_uiName = "Camera_/Adjust/Set up the camera.";
	curTemplate->m_numParameters = 4;
	curTemplate->m_parameters[0] = Parameter::WAYPOINT;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::REAL;
	curTemplate->m_parameters[3] = Parameter::WAYPOINT;
	curTemplate->m_numUiStrings = 5;
	curTemplate->m_uiStrings[0] = "Position camera at ";
	curTemplate->m_uiStrings[1] = ", zoom = ";
	curTemplate->m_uiStrings[2] = "(0.0 to 1.0), pitch = ";
	curTemplate->m_uiStrings[3] = "(1.0==default), looking towards ";
	curTemplate->m_uiStrings[4] = ".";


 	curTemplate = &m_actionTemplates[ScriptAction::INCREMENT_COUNTER];
	curTemplate->m_internalName = "INCREMENT_COUNTER";
	curTemplate->m_uiName = "Scripting_/Counters/Increment counter.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::INT;
	curTemplate->m_parameters[1] = Parameter::COUNTER;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Add ";
	curTemplate->m_uiStrings[1] = " to counter ";

 	curTemplate = &m_actionTemplates[ScriptAction::DECREMENT_COUNTER];
	curTemplate->m_internalName = "DECREMENT_COUNTER";
	curTemplate->m_uiName = "Scripting_/Counters/Decrement counter.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::INT;
	curTemplate->m_parameters[1] = Parameter::COUNTER;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Subtract ";
	curTemplate->m_uiStrings[1] = " from counter ";

 	curTemplate = &m_actionTemplates[ScriptAction::MOVE_CAMERA_ALONG_WAYPOINT_PATH];
	curTemplate->m_internalName = "MOVE_CAMERA_ALONG_WAYPOINT_PATH";
	curTemplate->m_uiName = "Camera_/Move/Move along a waypoint path.";
	curTemplate->m_numParameters = 5;
	curTemplate->m_parameters[0] = Parameter::WAYPOINT;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::REAL;
	curTemplate->m_parameters[3] = Parameter::REAL;
	curTemplate->m_parameters[4] = Parameter::REAL;
	curTemplate->m_numUiStrings = 6;
	curTemplate->m_uiStrings[0] = "Move along path starting with ";
	curTemplate->m_uiStrings[1] = " in ";
	curTemplate->m_uiStrings[2] = " seconds, camera shutter ";
	curTemplate->m_uiStrings[3] = " seconds, ease-in ";
	curTemplate->m_uiStrings[4] = " seconds, ease-out ";
	curTemplate->m_uiStrings[5] = " seconds.";

 	curTemplate = &m_actionTemplates[ScriptAction::ROTATE_CAMERA];
	curTemplate->m_internalName = "ROTATE_CAMERA";
	curTemplate->m_uiName = "Camera_/Rotate/ Rotate around the current viewpoint.";
	curTemplate->m_numParameters = 4;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::REAL;
	curTemplate->m_parameters[3] = Parameter::REAL;
	curTemplate->m_numUiStrings = 5;
	curTemplate->m_uiStrings[0] = "Rotate ";
	curTemplate->m_uiStrings[1] = " times, taking ";
	curTemplate->m_uiStrings[2] = " seconds, ease-in ";
	curTemplate->m_uiStrings[3] = " seconds, ease-out ";
	curTemplate->m_uiStrings[4] = " seconds.";

	curTemplate = &m_actionTemplates[ScriptAction::RESET_CAMERA];
	curTemplate->m_internalName = "RESET_CAMERA";
	curTemplate->m_uiName = "Camera_/Move/ Reset to the default view.";
	curTemplate->m_numParameters = 4;
	curTemplate->m_parameters[0] = Parameter::WAYPOINT;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::REAL;
	curTemplate->m_parameters[3] = Parameter::REAL;
	curTemplate->m_numUiStrings = 5;
	curTemplate->m_uiStrings[0] = "Reset to ";
	curTemplate->m_uiStrings[1] = ", taking ";
	curTemplate->m_uiStrings[2] = " seconds, ease-in ";
	curTemplate->m_uiStrings[3] = " seconds, ease-out ";
	curTemplate->m_uiStrings[4] = " seconds.";

	curTemplate = &m_actionTemplates[ScriptAction::MOVE_CAMERA_TO_SELECTION];
	curTemplate->m_internalName = "MOVE_CAMERA_TO_SELECTION";
	curTemplate->m_uiName = "Camera_/Move/Modify/ End movement at selected unit.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "End movement at selected unit.";

	curTemplate = &m_actionTemplates[ScriptAction::SET_MILLISECOND_TIMER];
	curTemplate->m_internalName = "SET_MILLISECOND_TIMER";
	curTemplate->m_uiName = "Scripting_/Timer/Seconds countdown timer -- set.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::COUNTER;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Set timer ";
	curTemplate->m_uiStrings[1] = " to expire in ";
	curTemplate->m_uiStrings[2] = " seconds.";

	curTemplate = &m_actionTemplates[ScriptAction::SET_RANDOM_MSEC_TIMER];
	curTemplate->m_internalName = "SET_RANDOM_MSEC_TIMER";
	curTemplate->m_uiName = "Scripting_/Timer/Seconds countdown timer -- set random.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::COUNTER;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::REAL;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "Set timer ";
	curTemplate->m_uiStrings[1] = " to expire between ";
	curTemplate->m_uiStrings[2] = " and ";
	curTemplate->m_uiStrings[3] = " seconds.";

	curTemplate = &m_actionTemplates[ScriptAction::ADD_TO_MSEC_TIMER];
	curTemplate->m_internalName = "ADD_TO_MSEC_TIMER";
	curTemplate->m_uiName = "Scripting_/Timer/Seconds countdown timer -- add seconds.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_parameters[1] = Parameter::COUNTER;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Add ";
	curTemplate->m_uiStrings[1] = " seconds to timer ";
	curTemplate->m_uiStrings[2] = " .";

	curTemplate = &m_actionTemplates[ScriptAction::SUB_FROM_MSEC_TIMER];
	curTemplate->m_internalName = "SUB_FROM_MSEC_TIMER";
	curTemplate->m_uiName = "Scripting_/Timer/Seconds countdown timer -- subtract seconds.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_parameters[1] = Parameter::COUNTER;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Subtract ";
	curTemplate->m_uiStrings[1] = " seconds from timer ";
	curTemplate->m_uiStrings[2] = " .";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_MOD_FREEZE_TIME];
	curTemplate->m_internalName = "CAMERA_MOD_FREEZE_TIME";
	curTemplate->m_uiName = "Camera_/Move/Modify/ Freeze time during the camera movement.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Freeze time during the camera movement.";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_MOD_FREEZE_ANGLE];
	curTemplate->m_internalName = "CAMERA_MOD_FREEZE_ANGLE";
	curTemplate->m_uiName = "Camera_/Move/Modify/ Freeze camera angle during the camera movement.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Freeze camera angle during the camera movement.";

	curTemplate = &m_actionTemplates[ScriptAction::SUSPEND_BACKGROUND_SOUNDS];
	curTemplate->m_internalName = "SUSPEND_BACKGROUND_SOUNDS";
	curTemplate->m_uiName = "Multimedia_/All Sounds/Suspend all sounds.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Suspend background sounds.";

	curTemplate = &m_actionTemplates[ScriptAction::RESUME_BACKGROUND_SOUNDS];
	curTemplate->m_internalName = "RESUME_BACKGROUND_SOUNDS";
	curTemplate->m_uiName = "Multimedia_/All Sounds/Resume all sounds.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Resume background sounds.";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_MOD_SET_FINAL_ZOOM];
	curTemplate->m_internalName = "CAMERA_MOD_SET_FINAL_ZOOM";
	curTemplate->m_uiName = "Camera_/Move/Modify/Set Final zoom for camera movement.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_parameters[1] = Parameter::PERCENT;
	curTemplate->m_parameters[2] = Parameter::PERCENT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "Adjust zoom to ";
	curTemplate->m_uiStrings[1] = " (1.0==max height, 0.0==in the ground) ";
	curTemplate->m_uiStrings[2] = " ease-in ";
	curTemplate->m_uiStrings[3] = " ease-out.";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_MOD_SET_FINAL_PITCH];
	curTemplate->m_internalName = "CAMERA_MOD_SET_FINAL_PITCH";
	curTemplate->m_uiName = "Camera_/Move/Modify/Set Final pitch for camera movement.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_parameters[1] = Parameter::PERCENT;
	curTemplate->m_parameters[2] = Parameter::PERCENT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "Adjust pitch to ";
	curTemplate->m_uiStrings[1] = " (1.0==default, 0.0==toward horizon, >1 = toward ground) ";
	curTemplate->m_uiStrings[2] = " ease-in ";
	curTemplate->m_uiStrings[3] = " ease-out.";

	curTemplate = &m_actionTemplates[ScriptAction::SET_VISUAL_SPEED_MULTIPLIER];
	curTemplate->m_internalName = "SET_VISUAL_SPEED_MULTIPLIER";
	curTemplate->m_uiName = "{Compatibility}_/Multimedia/Modify visual game time.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::INT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Make visual time ";
	curTemplate->m_uiStrings[1] = " time normal (1=normal, 2 = twice as fast, ...).";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_MOD_SET_FINAL_SPEED_MULTIPLIER];
	curTemplate->m_internalName = "CAMERA_MOD_SET_FINAL_SPEED_MULTIPLIER";
	curTemplate->m_uiName = "{Compatibility}_/Camera/Modify/Final visual game time for camera movement.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::INT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Adjust game time to";
	curTemplate->m_uiStrings[1] = " times normal (1=normal, 2 = twice as fast, ...).";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_MOD_SET_ROLLING_AVERAGE];
	curTemplate->m_internalName = "CAMERA_MOD_SET_ROLLING_AVERAGE";
	curTemplate->m_uiName = "Camera_/Move/Modify/ Number of frames to average movements.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::INT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Average position and angle changes over";
	curTemplate->m_uiStrings[1] = " frames. (1=no smoothing, 5 = very smooth)";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_MOD_FINAL_LOOK_TOWARD];
	curTemplate->m_internalName = "CAMERA_MOD_FINAL_LOOK_TOWARD";
	curTemplate->m_uiName = "{Compatibility}_/Camera/Modify/Move/ Final camera look toward point.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::WAYPOINT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Look toward";
	curTemplate->m_uiStrings[1] = " at the end of the camera movement.";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_MOD_LOOK_TOWARD];
	curTemplate->m_internalName = "CAMERA_MOD_LOOK_TOWARD";
	curTemplate->m_uiName = "Camera_/Modify/Move/Camera look toward point while moving.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::WAYPOINT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Look toward";
	curTemplate->m_uiStrings[1] = " during the camera movement.";

	curTemplate = &m_actionTemplates[ScriptAction::CREATE_OBJECT];
	curTemplate->m_internalName = "CREATE_OBJECT";
	curTemplate->m_uiName = "Unit_/Spawn/Spawn object.";
	curTemplate->m_numParameters = 4;
	curTemplate->m_parameters[0] = Parameter::OBJECT_TYPE;
	curTemplate->m_parameters[1] = Parameter::TEAM;
	curTemplate->m_parameters[2] = Parameter::COORD3D;
	curTemplate->m_parameters[3] = Parameter::ANGLE;
	curTemplate->m_numUiStrings = 5;
	curTemplate->m_uiStrings[0] = "Spawn object ";
	curTemplate->m_uiStrings[1] = " in ";
	curTemplate->m_uiStrings[2] = " at position (";
	curTemplate->m_uiStrings[3] = "), rotated ";
	curTemplate->m_uiStrings[4] = " .";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_ATTACK_TEAM];
	curTemplate->m_internalName = "TEAM_ATTACK_TEAM";
	curTemplate->m_uiName = "Team_/Attack/Set to attack -- another team.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " begin attack on ";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_ATTACK_NAMED];
	curTemplate->m_internalName = "NAMED_ATTACK_NAMED";
	curTemplate->m_uiName = "Unit_/Attack/Set unit to attack another unit.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " begin attack on ";

	curTemplate = &m_actionTemplates[ScriptAction::CREATE_NAMED_ON_TEAM_AT_WAYPOINT];
	curTemplate->m_internalName = "CREATE_NAMED_ON_TEAM_AT_WAYPOINT";
	curTemplate->m_uiName = "Unit_/Spawn/Spawn -- named unit on a team at a waypoint.";
	curTemplate->m_numParameters = 4;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::OBJECT_TYPE;
	curTemplate->m_parameters[2] = Parameter::TEAM;
	curTemplate->m_parameters[3] = Parameter::WAYPOINT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "Spawn ";
	curTemplate->m_uiStrings[1] = " of type ";
	curTemplate->m_uiStrings[2] = " on ";
	curTemplate->m_uiStrings[3] = " at waypoint ";

	curTemplate = &m_actionTemplates[ScriptAction::CREATE_UNNAMED_ON_TEAM_AT_WAYPOINT];
	curTemplate->m_internalName = "CREATE_UNNAMED_ON_TEAM_AT_WAYPOINT";
	curTemplate->m_uiName = "Unit_/Spawn/Spawn -- unnamed unit on a team at a waypoint.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::OBJECT_TYPE;
	curTemplate->m_parameters[1] = Parameter::TEAM;
	curTemplate->m_parameters[2] = Parameter::WAYPOINT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Spawn unit of type ";
	curTemplate->m_uiStrings[1] = " on ";
	curTemplate->m_uiStrings[2] = " at waypoint ";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_APPLY_ATTACK_PRIORITY_SET];
	curTemplate->m_internalName = "NAMED_APPLY_ATTACK_PRIORITY_SET";
	curTemplate->m_uiName = "AttackPrioritySet_/Apply/Unit/Apply unit's attack priority set.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::ATTACK_PRIORITY_SET;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Have ";
	curTemplate->m_uiStrings[1] = " use ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_APPLY_ATTACK_PRIORITY_SET];
	curTemplate->m_internalName = "TEAM_APPLY_ATTACK_PRIORITY_SET";
	curTemplate->m_uiName = "AttackPrioritySet_/Apply/Team/Apply a team's attack priority set.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::ATTACK_PRIORITY_SET;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Have ";
	curTemplate->m_uiStrings[1] = " use ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::SET_ATTACK_PRIORITY_THING];
	curTemplate->m_internalName = "SET_ATTACK_PRIORITY_THING";
	curTemplate->m_uiName = "AttackPrioritySet_/Set/Modify a set's priority for a single unit type.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::ATTACK_PRIORITY_SET;
	curTemplate->m_parameters[1] = Parameter::OBJECT_TYPE;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "For ";
	curTemplate->m_uiStrings[1] = " set the priority of object type ";
	curTemplate->m_uiStrings[2] = " to ";

	curTemplate = &m_actionTemplates[ScriptAction::SET_ATTACK_PRIORITY_KIND_OF];
	curTemplate->m_internalName = "SET_ATTACK_PRIORITY_KIND_OF";
	curTemplate->m_uiName = "AttackPrioritySet_/Set/Modify a set's priorities for all of a kind.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::ATTACK_PRIORITY_SET;
	curTemplate->m_parameters[1] = Parameter::KIND_OF_PARAM;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "For ";
	curTemplate->m_uiStrings[1] = " set the priority of object type ";
	curTemplate->m_uiStrings[2] = " to ";

	curTemplate = &m_actionTemplates[ScriptAction::SET_DEFAULT_ATTACK_PRIORITY];
	curTemplate->m_internalName = "SET_DEFAULT_ATTACK_PRIORITY";
	curTemplate->m_uiName = "AttackPrioritySet_/Set/Specify the set's default priority.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::ATTACK_PRIORITY_SET;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "For ";
	curTemplate->m_uiStrings[1] = " set the default priority to ";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_ADD_SKILLPOINTS];
	curTemplate->m_internalName = "PLAYER_ADD_SKILLPOINTS";
	curTemplate->m_uiName = "Player_/Experience/Add or Subtract Skill Points.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is given ";
	curTemplate->m_uiStrings[2] = " Skill Points.";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_ADD_RANKLEVEL];
	curTemplate->m_internalName = "PLAYER_ADD_RANKLEVEL";
	curTemplate->m_uiName = "Player_/Experience/Add or Subtract Rank Levels.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is given ";
	curTemplate->m_uiStrings[2] = " Rank Levels.";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_SET_RANKLEVEL];
	curTemplate->m_internalName = "PLAYER_SET_RANKLEVEL";
	curTemplate->m_uiName = "Player_/Experience/Set Rank Level.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is given a Rank Level of ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_SET_RANKLEVELLIMIT];
	curTemplate->m_internalName = "PLAYER_SET_RANKLEVELLIMIT";
	curTemplate->m_uiName = "Map_/Experience/Set Rank Level Limit for current Map.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::INT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "The current map is given a Rank Level Limit of ";
	curTemplate->m_uiStrings[1] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_GRANT_SCIENCE];
	curTemplate->m_internalName = "PLAYER_GRANT_SCIENCE";
	curTemplate->m_uiName = "Player_/Science/Grant a Science to a given Player (ignoring prerequisites).";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::SCIENCE;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is granted ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_PURCHASE_SCIENCE];
	curTemplate->m_internalName = "PLAYER_PURCHASE_SCIENCE";
	curTemplate->m_uiName = "Player_/Science/Player attempts to purchase a Science.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::SCIENCE;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " attempts to purchase Science ";
	curTemplate->m_uiStrings[2] = ".";
	
	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_SCIENCE_AVAILABILITY];
	curTemplate->m_internalName = "PLAYER_SCIENCE_AVAILABILITY";
	curTemplate->m_uiName = "Player_/Science/Set science availability.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::SCIENCE;
	curTemplate->m_parameters[2] = Parameter::SCIENCE_AVAILABILITY;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " set ";
	curTemplate->m_uiStrings[2] = " availability to ";
	curTemplate->m_uiStrings[3] = ".";
	
	curTemplate = &m_actionTemplates[ScriptAction::SET_BASE_CONSTRUCTION_SPEED];
	curTemplate->m_internalName = "SET_BASE_CONSTRUCTION_SPEED";
	curTemplate->m_uiName = "Player_/AI/Set the delay between building teams.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " will delay ";
	curTemplate->m_uiStrings[1] = " seconds between building teams.";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_SET_ATTITUDE];
	curTemplate->m_internalName = "NAMED_SET_ATTITUDE";
	curTemplate->m_uiName = "Unit_/Mood/Set the general attitude of a specific unit.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::AI_MOOD;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " changes his attitude to ";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_SET_ATTITUDE];
	curTemplate->m_internalName = "TEAM_SET_ATTITUDE";
	curTemplate->m_uiName = "Team_/Mood/Set the general attitude of a team.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::AI_MOOD;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " change their attitude to ";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_SET_REPULSOR];
	curTemplate->m_internalName = "NAMED_SET_REPULSOR";
	curTemplate->m_uiName = "Unit_/Internal/Repulsor/Set the REPULSOR flag of a specific unit.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " REPULSOR flag is ";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_SET_REPULSOR];
	curTemplate->m_internalName = "TEAM_SET_REPULSOR";
	curTemplate->m_uiName = "Team_/Internal/Repulsor/Set the REPULSOR flag of a team.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " REPULSOR flag is ";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_ATTACK_AREA];
	curTemplate->m_internalName = "NAMED_ATTACK_AREA";
	curTemplate->m_uiName = "Unit_/Attack/Set a specific unit to attack a specific trigger area.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::TRIGGER_AREA;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " attacks anything in ";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_ATTACK_TEAM];
	curTemplate->m_internalName = "NAMED_ATTACK_TEAM";
	curTemplate->m_uiName = "Unit_/Attack/Set a specific unit to attack a team.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " attacks ";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_ATTACK_AREA];
	curTemplate->m_internalName = "TEAM_ATTACK_AREA";
	curTemplate->m_uiName = "Team_/Attack/Set to attack -- trigger area.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::TRIGGER_AREA;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " attack anything in ";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_ATTACK_NAMED];
	curTemplate->m_internalName = "TEAM_ATTACK_NAMED";
	curTemplate->m_uiName = "Team_/Attack/Set to attack -- specific unit.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " attacks ";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_LOAD_TRANSPORTS];
	curTemplate->m_internalName = "TEAM_LOAD_TRANSPORTS";
	curTemplate->m_uiName = "Team_/Transport/Transport -- automatically load.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " load into transports.";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_ENTER_NAMED];
	curTemplate->m_internalName = "NAMED_ENTER_NAMED";
	curTemplate->m_uiName = "Unit_/Transport/Transport -- load unit into specific.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " loads into ";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_ENTER_NAMED];
	curTemplate->m_internalName = "TEAM_ENTER_NAMED";
	curTemplate->m_uiName = "Team_/Transport/Transport -- load team into specific.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " attempt to load into ";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_EXIT_ALL];
	curTemplate->m_internalName = "NAMED_EXIT_ALL";
	curTemplate->m_uiName = "Unit_/Transport/Transport -- unload units from specific.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " unloads.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_EXIT_ALL];
	curTemplate->m_internalName = "TEAM_EXIT_ALL";
	curTemplate->m_uiName = "Team_/Transport/Transport -- unload team from all.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " unload.";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_FOLLOW_WAYPOINTS];
	curTemplate->m_internalName = "NAMED_FOLLOW_WAYPOINTS";
	curTemplate->m_uiName = "Unit_/Move/Set a specific unit to follow a waypoint path.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::WAYPOINT_PATH;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " follows waypoints, beginning at ";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_FOLLOW_WAYPOINTS_EXACT];
	curTemplate->m_internalName = "NAMED_FOLLOW_WAYPOINTS_EXACT";
	curTemplate->m_uiName = "Unit_/Move/Set a specific unit to EXACTLY follow a waypoint path.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::WAYPOINT_PATH;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " EXACTLY follows waypoints, beginning at ";

		curTemplate = &m_actionTemplates[ScriptAction::NAMED_GUARD];
		curTemplate->m_internalName = "NAMED_GUARD";
	curTemplate->m_uiName = "Unit_/Move/Set to guard.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " begins guarding.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_GUARD];
	curTemplate->m_internalName = "TEAM_GUARD";
	curTemplate->m_uiName = "Team_/Guard/Set to guard -- current location.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " begins guarding.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_GUARD_POSITION];
	curTemplate->m_internalName = "TEAM_GUARD_POSITION";
	curTemplate->m_uiName = "Team_/Guard/Set to guard -- location.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::WAYPOINT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " begins guarding at ";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_GUARD_OBJECT];
	curTemplate->m_internalName = "TEAM_GUARD_OBJECT";
	curTemplate->m_uiName = "Team_/Guard/Set to guard -- specific unit.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " begins guarding ";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_GUARD_AREA];
	curTemplate->m_internalName = "TEAM_GUARD_AREA";
	curTemplate->m_uiName = "Team_/Guard/Set to guard -- area.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::TRIGGER_AREA;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " begins guarding ";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_HUNT];
	curTemplate->m_internalName = "NAMED_HUNT";
	curTemplate->m_uiName = "Unit_/Hunt/Set a specific unit to hunt.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " begins hunting.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_HUNT_WITH_COMMAND_BUTTON];
	curTemplate->m_internalName = "TEAM_HUNT_WITH_COMMAND_BUTTON";
	curTemplate->m_uiName = "Team_/Hunt/Set to hunt using commandbutton ability.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::COMMANDBUTTON_ALL_ABILITIES;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " begins hunting using ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_HUNT];
	curTemplate->m_internalName = "TEAM_HUNT";
	curTemplate->m_uiName = "Team_/Hunt/Set to hunt.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " begins hunting.";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_HUNT];
	curTemplate->m_internalName = "PLAYER_HUNT";
	curTemplate->m_uiName = "Player_/Hunt/Set all of a player's units to hunt.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " begins hunting.";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_SELL_EVERYTHING];
	curTemplate->m_internalName = "PLAYER_SELL_EVERYTHING";
	curTemplate->m_uiName = "Player_/Set/Set a player to sell everything.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " sells everything.";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_DISABLE_BASE_CONSTRUCTION];
	curTemplate->m_internalName = "PLAYER_DISABLE_BASE_CONSTRUCTION";
	curTemplate->m_uiName = "Player_/Build/Set a player to be unable to build buildings.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is unable to build buildings.";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_DISABLE_FACTORIES];
	curTemplate->m_internalName = "PLAYER_DISABLE_FACTORIES";
	curTemplate->m_uiName = "Player_/Build/Set a player to be unable to build from a specific building.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::OBJECT_TYPE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is unable to build from ";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_DISABLE_UNIT_CONSTRUCTION];
	curTemplate->m_internalName = "PLAYER_DISABLE_UNIT_CONSTRUCTION";
	curTemplate->m_uiName = "Player_/Build/Set a player to be unable to build units.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is unable to build units.";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_ENABLE_BASE_CONSTRUCTION];
	curTemplate->m_internalName = "PLAYER_ENABLE_BASE_CONSTRUCTION";
	curTemplate->m_uiName = "Player_/Build/Set a player to be able to build buildings.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is able to build buildings.";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_ENABLE_FACTORIES];
	curTemplate->m_internalName = "PLAYER_ENABLE_FACTORIES";
	curTemplate->m_uiName = "Player_/Build/Set a player to be able to build from a specific building.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::OBJECT_TYPE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is able to build from ";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_ENABLE_UNIT_CONSTRUCTION];
	curTemplate->m_internalName = "PLAYER_ENABLE_UNIT_CONSTRUCTION";
	curTemplate->m_uiName = "Player_/Build/Set a player to be able to build units.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is able to build units.";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_MOVE_HOME];
	curTemplate->m_internalName = "CAMERA_MOVE_HOME";
	curTemplate->m_uiName = "Camera_/Move/Move the camera to the home position.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "The camera moves to the home base.";

	curTemplate = &m_actionTemplates[ScriptAction::OVERSIZE_TERRAIN];
	curTemplate->m_internalName = "OVERSIZE_TERRAIN";
	curTemplate->m_uiName = "Camera_/Terrain/Oversize the terrain.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::INT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Oversize the terrain ";
	curTemplate->m_uiStrings[1] = " tiles on each side [0 = reset to normal].";

	curTemplate = &m_actionTemplates[ScriptAction::BUILD_TEAM];
	curTemplate->m_internalName = "BUILD_TEAM";
	curTemplate->m_uiName = "Team_/AI/Start building a team.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Start building team ";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_DAMAGE];
	curTemplate->m_internalName = "NAMED_DAMAGE";
	curTemplate->m_uiName = "Unit_/Damage/Deal damage to a specific unit.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " takes ";
	curTemplate->m_uiStrings[2] = " points of damage.";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_DELETE];
	curTemplate->m_internalName = "NAMED_DELETE";
	curTemplate->m_uiName = "Unit_/Damage or Remove/Delete a specific unit.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is removed from the world.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_DELETE];
	curTemplate->m_internalName = "TEAM_DELETE";
	curTemplate->m_uiName = "Team_/Damage or Remove/Delete a team.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is removed from the world.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_DELETE_LIVING];
	curTemplate->m_internalName = "TEAM_DELETE_LIVING";
	curTemplate->m_uiName = "Team_/Damage or Remove/Delete a team, but ignore dead guys.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Each living member of team ";
	curTemplate->m_uiStrings[1] = " is removed from the world.";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_KILL];
	curTemplate->m_internalName = "NAMED_KILL";
	curTemplate->m_uiName = "Unit_/Damage or Remove/Kill a specific unit.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = "is dealt a lethal amount of damage.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_KILL];
	curTemplate->m_internalName = "TEAM_KILL";
	curTemplate->m_uiName = "Team_/Damage or Remove/Kill an entire team.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is dealt a lethal amount of damage.";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_KILL];
	curTemplate->m_internalName = "PLAYER_KILL";
	curTemplate->m_uiName = "Player_/Damage or Remove/Kill a player.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "All of ";
	curTemplate->m_uiStrings[1] = "'s buildings and units are dealt a lethal amount of damage.";

	curTemplate = &m_actionTemplates[ScriptAction::DISPLAY_TEXT];
	curTemplate->m_internalName = "DISPLAY_TEXT";
	curTemplate->m_uiName = "User_/String/Display a string.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::LOCALIZED_TEXT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Displays ";
	curTemplate->m_uiStrings[1] = " in the text log and message area.";

	curTemplate = &m_actionTemplates[ScriptAction::DISPLAY_CINEMATIC_TEXT];
	curTemplate->m_internalName = "DISPLAY_CINEMATIC_TEXT";
	curTemplate->m_uiName = "User_/String/Display a cinematic string.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::LOCALIZED_TEXT;
	curTemplate->m_parameters[1] = Parameter::FONT_NAME;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "Displays ";
	curTemplate->m_uiStrings[1] = " with font type ";
	curTemplate->m_uiStrings[2] = " in the bottom letterbox for ";
	curTemplate->m_uiStrings[3] = " seconds.";

	curTemplate = &m_actionTemplates[ScriptAction::CAMEO_FLASH];
	curTemplate->m_internalName = "CAMEO_FLASH";
	curTemplate->m_uiName = "User_/Flash/Flash a cameo for a specified amount of time.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::COMMAND_BUTTON;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " flashes for ";
	curTemplate->m_uiStrings[2] = " seconds.";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_FLASH];
	curTemplate->m_internalName = "NAMED_FLASH";
	curTemplate->m_uiName = "User_/Flash/Flash a specific unit for a specified amount of time.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " flashes for ";
	curTemplate->m_uiStrings[2] = " seconds.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_FLASH];
	curTemplate->m_internalName = "TEAM_FLASH";
	curTemplate->m_uiName = "User_/Flash/Flash a team for a specified amount of time.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " flashes for ";
	curTemplate->m_uiStrings[2] = " seconds.";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_CUSTOM_COLOR];
	curTemplate->m_internalName = "NAMED_CUSTOM_COLOR";
	curTemplate->m_uiName = "User_/Flash/Set a specific unit to use a special indicator color.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::COLOR;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " uses the color ";
	curTemplate->m_uiStrings[2] = " .";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_FLASH_WHITE];
	curTemplate->m_internalName = "NAMED_FLASH_WHITE";
	curTemplate->m_uiName = "User_/Flash/Flash a specific unit white for a specified amount of time.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " flashes white for ";
	curTemplate->m_uiStrings[2] = " seconds.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_FLASH_WHITE];
	curTemplate->m_internalName = "TEAM_FLASH_WHITE";
	curTemplate->m_uiName = "User_/Flash/Flash a team white for a specified amount of time.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " flashes white for ";
	curTemplate->m_uiStrings[2] = " seconds.";

	curTemplate = &m_actionTemplates[ScriptAction::INGAME_POPUP_MESSAGE];
	curTemplate->m_internalName = "INGAME_POPUP_MESSAGE";
	curTemplate->m_uiName = "User_/String/Display Popup Message Box.";
	curTemplate->m_numParameters = 5;
	curTemplate->m_parameters[0] = Parameter::LOCALIZED_TEXT;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_parameters[3] = Parameter::INT;
	curTemplate->m_parameters[4] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 6;
	curTemplate->m_uiStrings[0] = "Displays ";
	curTemplate->m_uiStrings[1] = " at ";
	curTemplate->m_uiStrings[2] = " percent of the screen Width ";
	curTemplate->m_uiStrings[3] = " percent of the screen Height and a width of ";
	curTemplate->m_uiStrings[4] = " pixels and pauses the game (";
	curTemplate->m_uiStrings[5] = " )";

	curTemplate = &m_actionTemplates[ScriptAction::MOVIE_PLAY_FULLSCREEN];
	curTemplate->m_internalName = "MOVIE_PLAY_FULLSCREEN";
	curTemplate->m_uiName = "Multimedia_/Movie/Play a movie in fullscreen mode.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::MOVIE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " plays fullscreen.";

	curTemplate = &m_actionTemplates[ScriptAction::MOVIE_PLAY_RADAR];
	curTemplate->m_internalName = "MOVIE_PLAY_RADAR";
	curTemplate->m_uiName = "Multimedia_/Movie/Play a movie in the radar.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::MOVIE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " plays in the radar window.";

	curTemplate = &m_actionTemplates[ScriptAction::SOUND_PLAY_NAMED];
	curTemplate->m_internalName = "SOUND_PLAY_NAMED";
	curTemplate->m_uiName = "Multimedia_/Sound Effects/Play a sound as though coming from a specific unit.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEXT_STRING;
	curTemplate->m_parameters[1] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " plays as though coming from ";

	curTemplate = &m_actionTemplates[ScriptAction::SPEECH_PLAY];
	curTemplate->m_internalName = "SPEECH_PLAY";
	curTemplate->m_uiName = "Multimedia_/Sound Effects/Play a speech file.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::DIALOG;
	curTemplate->m_parameters[1] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " plays, allowing overlap ";
	curTemplate->m_uiStrings[2] = " (true to allow, false to disallow).";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_TRANSFER_OWNERSHIP_PLAYER];
	curTemplate->m_internalName = "PLAYER_TRANSFER_OWNERSHIP_PLAYER";
	curTemplate->m_uiName = "Player_/Transfer/Transfer assets from one player to another player.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "All assets of ";
	curTemplate->m_uiStrings[1] = " are transferred to ";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_TRANSFER_OWNERSHIP_PLAYER];
	curTemplate->m_internalName = "NAMED_TRANSFER_OWNERSHIP_PLAYER";
	curTemplate->m_uiName = "Player_/Transfer/Transfer a specific unit to the control of a player.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is transferred to the command of ";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_EXCLUDE_FROM_SCORE_SCREEN];
	curTemplate->m_internalName = "PLAYER_EXCLUDE_FROM_SCORE_SCREEN";
	curTemplate->m_uiName = "Player_/Score/Exclude this player from the score screen.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Exclude ";
	curTemplate->m_uiStrings[1] = " from the score screen.";

	curTemplate = &m_actionTemplates[ScriptAction::ENABLE_SCORING];
	curTemplate->m_internalName = "ENABLE_SCORING";
	curTemplate->m_uiName = "Player_/Score/Turn on scoring.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Turn on scoring.";

	curTemplate = &m_actionTemplates[ScriptAction::DISABLE_SCORING];
	curTemplate->m_internalName = "DISABLE_SCORING";
	curTemplate->m_uiName = "Player_/Score/Turn off scoring.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Turn off scoring.";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_RELATES_PLAYER];
	curTemplate->m_internalName = "PLAYER_RELATES_PLAYER";
	curTemplate->m_uiName = "Player_/Alliances/Change how a player relates to another player.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::SIDE;
	curTemplate->m_parameters[2] = Parameter::RELATION;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " considers ";
	curTemplate->m_uiStrings[2] = " to be ";

	curTemplate = &m_actionTemplates[ScriptAction::RADAR_CREATE_EVENT];
	curTemplate->m_internalName = "RADAR_CREATE_EVENT";
	curTemplate->m_uiName = "Radar_/Create Event/Create a radar event at a specified location.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::COORD3D;
	curTemplate->m_parameters[1] = Parameter::RADAR_EVENT_TYPE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "A radar event occurs at ";
	curTemplate->m_uiStrings[1] = " of type ";

	curTemplate = &m_actionTemplates[ScriptAction::OBJECT_CREATE_RADAR_EVENT];
	curTemplate->m_internalName = "OBJECT_CREATE_RADAR_EVENT";
	curTemplate->m_uiName = "Radar_/Create Event/Create  a radar event at a specific object.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::RADAR_EVENT_TYPE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "A radar event occurs at ";
	curTemplate->m_uiStrings[1] = " of type ";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_CREATE_RADAR_EVENT];
	curTemplate->m_internalName = "TEAM_CREATE_RADAR_EVENT";
	curTemplate->m_uiName = "Radar_/Create Event/Create  a radar event at a specific team.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::RADAR_EVENT_TYPE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "A radar event occurs at ";
	curTemplate->m_uiStrings[1] = " of type ";
	
	curTemplate = &m_actionTemplates[ScriptAction::RADAR_DISABLE];
	curTemplate->m_internalName = "RADAR_DISABLE";
	curTemplate->m_uiName = "Radar_/Control/Disable the radar.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "The radar is disabled.";

	curTemplate = &m_actionTemplates[ScriptAction::RADAR_ENABLE];
	curTemplate->m_internalName = "RADAR_ENABLE";
	curTemplate->m_uiName = "Radar_/Control/Enable the radar.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "The radar is enabled.";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_SET_STEALTH_ENABLED];
	curTemplate->m_internalName = "NAMED_SET_STEALTH_ENABLED";
	curTemplate->m_uiName = "Unit_/Status/Stealth set enabled or disabled.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Set ";
	curTemplate->m_uiStrings[1] = " stealth ability to ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_SET_STEALTH_ENABLED];
	curTemplate->m_internalName = "TEAM_SET_STEALTH_ENABLED";
	curTemplate->m_uiName = "Team_/Status/Stealth set enabled or disabled.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Set ";
	curTemplate->m_uiStrings[1] = " stealth ability to ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_SET_UNMANNED_STATUS];
	curTemplate->m_internalName = "NAMED_SET_UNMANNED_STATUS";
	curTemplate->m_uiName = "Unit_/Status/Make unmanned.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Make ";
	curTemplate->m_uiStrings[1] = " unmanned.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_SET_UNMANNED_STATUS];
	curTemplate->m_internalName = "TEAM_SET_UNMANNED_STATUS";
	curTemplate->m_uiName = "Team_/Status/Make unmanned.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Make ";
	curTemplate->m_uiStrings[1] = " unmanned.";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_SET_BOOBYTRAPPED];
	curTemplate->m_internalName = "NAMED_SET_BOOBYTRAPPED";
	curTemplate->m_uiName = "Unit_/Status/Add boobytrap.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::OBJECT_TYPE;
	curTemplate->m_parameters[1] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Add boobytrap of type ";
	curTemplate->m_uiStrings[1] = " to ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_SET_BOOBYTRAPPED];
	curTemplate->m_internalName = "TEAM_SET_BOOBYTRAPPED";
	curTemplate->m_uiName = "Team_/Status/Add boobytrap.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::OBJECT_TYPE;
	curTemplate->m_parameters[1] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Add boobytrap of type ";
	curTemplate->m_uiStrings[1] = " to team ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::MAP_REVEAL_AT_WAYPOINT];
	curTemplate->m_internalName = "MAP_REVEAL_AT_WAYPOINT";
	curTemplate->m_uiName = "Map_/Shroud or Reveal/Reveal map at waypoint -- fog.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::WAYPOINT;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "The map is revealed at ";
	curTemplate->m_uiStrings[1] = " with a radius of ";
	curTemplate->m_uiStrings[2] = " feet for ";
	curTemplate->m_uiStrings[3] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::MAP_SHROUD_AT_WAYPOINT];
	curTemplate->m_internalName = "MAP_SHROUD_AT_WAYPOINT";
	curTemplate->m_uiName = "Map_/Shroud or Reveal/Shroud map at waypoint -- add fog.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::WAYPOINT;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "The map is shrouded at ";
	curTemplate->m_uiStrings[1] = " with a radius of ";
	curTemplate->m_uiStrings[2] = " feet for ";
	curTemplate->m_uiStrings[3] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::MAP_REVEAL_ALL];
	curTemplate->m_internalName = "MAP_REVEAL_ALL";
	curTemplate->m_uiName = "Map_/Shroud or Reveal/Reveal the entire map for a player.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "The world is revealed for ";
	curTemplate->m_uiStrings[1] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::MAP_REVEAL_ALL_PERM];
	curTemplate->m_internalName = "MAP_REVEAL_ALL_PERM";
	curTemplate->m_uiName = "Map_/Shroud or Reveal/Reveal the entire map permanently for a player.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "The world is revealed permanently for ";
	curTemplate->m_uiStrings[1] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::MAP_REVEAL_ALL_UNDO_PERM];
	curTemplate->m_internalName = "MAP_REVEAL_ALL_UNDO_PERM";
	curTemplate->m_uiName = "Map_/Shroud or Reveal/Un-Reveal the entire map permanently for a player.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Undo the permanent reveal for ";
	curTemplate->m_uiStrings[1] = ".  This will mess things up badly if called when there has been no permanent reveal.";

	curTemplate = &m_actionTemplates[ScriptAction::MAP_SHROUD_ALL];
	curTemplate->m_internalName = "MAP_SHROUD_ALL";
	curTemplate->m_uiName = "Map_/Shroud or Reveal/Shroud the entire map for a player.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "The world is shrouded for ";
	curTemplate->m_uiStrings[1] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::DISABLE_BORDER_SHROUD];
	curTemplate->m_internalName = "DISABLE_BORDER_SHROUD";
	curTemplate->m_uiName = "Map_/Shroud or Reveal/Border Shroud is turned off.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Shroud off the map edges is turned off.";

	curTemplate = &m_actionTemplates[ScriptAction::ENABLE_BORDER_SHROUD];
	curTemplate->m_internalName = "ENABLE_BORDER_SHROUD";
	curTemplate->m_uiName = "Map_/Shroud or Reveal/Border Shroud is turned on.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Shroud off the map edges is turned on.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_GARRISON_SPECIFIC_BUILDING];
	curTemplate->m_internalName = "TEAM_GARRISON_SPECIFIC_BUILDING";
	curTemplate->m_uiName = "Team_/Garrison/Garrison a specific building with a team.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " enters into building named ";

	curTemplate = &m_actionTemplates[ScriptAction::EXIT_SPECIFIC_BUILDING];
	curTemplate->m_internalName = "EXIT_SPECIFIC_BUILDING";
	curTemplate->m_uiName = "Unit_/Garrison/Empty a specific building.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " empties.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_GARRISON_NEAREST_BUILDING];
	curTemplate->m_internalName = "TEAM_GARRISON_NEAREST_BUILDING";
	curTemplate->m_uiName = "Team_/Garrison/Garrison a nearby building with a team.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " garrison a nearby building.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_EXIT_ALL_BUILDINGS];
	curTemplate->m_internalName = "TEAM_EXIT_ALL_BUILDINGS";
	curTemplate->m_uiName = "Team_/Garrison/Exit all buildings a team is in.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " exits all buildings.";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_GARRISON_SPECIFIC_BUILDING];
	curTemplate->m_internalName = "NAMED_GARRISON_SPECIFIC_BUILDING";
	curTemplate->m_uiName = "Unit_/Garrison/Garrison a specific building with a specific unit.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " garrison building ";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_GARRISON_NEAREST_BUILDING];
	curTemplate->m_internalName = "NAMED_GARRISON_NEAREST_BUILDING";
	curTemplate->m_uiName = "Unit_/Garrison/Garrison a nearby building with a specific unit.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " garrison a nearby building.";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_EXIT_BUILDING];
	curTemplate->m_internalName = "NAMED_EXIT_BUILDING";
	curTemplate->m_uiName = "Unit_/Garrison/Exit the building the unit is in.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " leaves the building.";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_GARRISON_ALL_BUILDINGS];
	curTemplate->m_internalName = "PLAYER_GARRISON_ALL_BUILDINGS";
	curTemplate->m_uiName = "Player_/Garrison/Garrison as many buildings as player has units for.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " garrison buildings.";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_EXIT_ALL_BUILDINGS];
	curTemplate->m_internalName = "PLAYER_EXIT_ALL_BUILDINGS";
	curTemplate->m_uiName = "Player_/Garrison/All units leave their garrisons.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " evacuate.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_AVAILABLE_FOR_RECRUITMENT];
	curTemplate->m_internalName = "TEAM_AVAILABLE_FOR_RECRUITMENT";
	curTemplate->m_uiName = "Team_/AI/Set whether members of a team can be recruited into another team.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " sets their willingness to join teams to ";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_COLLECT_NEARBY_FOR_TEAM];
	curTemplate->m_internalName = "TEAM_COLLECT_NEARBY_FOR_TEAM";
	curTemplate->m_uiName = "Team_/AI/Set to collect nearby units.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " attempts to collect nearby units for a team.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_MERGE_INTO_TEAM];
	curTemplate->m_internalName = "TEAM_MERGE_INTO_TEAM";
	curTemplate->m_uiName = "Team_/Merge/Merge a team into another team.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " merges onto ";

	curTemplate = &m_actionTemplates[ScriptAction::IDLE_ALL_UNITS];
	curTemplate->m_internalName = "IDLE_ALL_UNITS";
	curTemplate->m_uiName = "Scripting_/Idle or Restart/Idle all units for all players.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Idle all units for all players.";

	curTemplate = &m_actionTemplates[ScriptAction::RESUME_SUPPLY_TRUCKING];
	curTemplate->m_internalName = "RESUME_SUPPLY_TRUCKING";
	curTemplate->m_uiName = "Scripting_/Idle or Restart/All idle Supply Trucks attempt to resume supply routes.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "All idle Supply Trucks attempt to resume supply routes.";

	curTemplate = &m_actionTemplates[ScriptAction::DISABLE_INPUT];
	curTemplate->m_internalName = "DISABLE_INPUT";
	curTemplate->m_uiName = "User_/Input/User input -- disable.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Disable mouse and keyboard input.";

	curTemplate = &m_actionTemplates[ScriptAction::ENABLE_INPUT];
	curTemplate->m_internalName = "ENABLE_INPUT";
	curTemplate->m_uiName = "User_/Input/User input -- enable.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Enable mouse and keyboard input.";

	curTemplate = &m_actionTemplates[ScriptAction::SOUND_AMBIENT_PAUSE];
	curTemplate->m_internalName = "SOUND_AMBIENT_PAUSE";
	curTemplate->m_uiName = "Multimedia_/SoundEffects/Pause the ambient sounds.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Pause the ambient sounds.";

	curTemplate = &m_actionTemplates[ScriptAction::SOUND_AMBIENT_RESUME];
	curTemplate->m_internalName = "SOUND_AMBIENT_RESUME";
	curTemplate->m_uiName = "Multimedia_/SoundEffects/Resume the ambient sounds.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Resume the ambient sounds.";

	curTemplate = &m_actionTemplates[ScriptAction::MUSIC_SET_TRACK];
	curTemplate->m_internalName = "MUSIC_SET_TRACK";
	curTemplate->m_uiName = "Multimedia_/Music/Play a music track.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::MUSIC;
	curTemplate->m_parameters[1] = Parameter::BOOLEAN;
	curTemplate->m_parameters[2] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "Play ";
	curTemplate->m_uiStrings[1] = " using fadeout (";
	curTemplate->m_uiStrings[2] = ") and fadein (";
	curTemplate->m_uiStrings[3] = ").";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_LETTERBOX_BEGIN];
	curTemplate->m_internalName = "CAMERA_LETTERBOX_BEGIN";
	curTemplate->m_uiName = "Camera_/Letterbox/Start letterbox mode.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Start letterbox mode (hide UI, add border).";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_LETTERBOX_END];
	curTemplate->m_internalName = "CAMERA_LETTERBOX_END";
	curTemplate->m_uiName = "Camera_/ End letterbox mode.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "End letterbox mode (show UI, remove border).";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_BW_MODE_BEGIN];
	curTemplate->m_internalName = "CAMERA_BW_MODE_BEGIN";
	curTemplate->m_uiName = "Camera_/Fade Effects/Start black & white mode.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::INT;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Frames to fade into black & white mode = ";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_BW_MODE_END];
	curTemplate->m_internalName = "CAMERA_BW_MODE_END";
	curTemplate->m_uiName = "Camera_/Fade Effects/End black & white mode.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Frames to fade into color mode = ";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_MOTION_BLUR];
	curTemplate->m_internalName = "CAMERA_MOTION_BLUR";
	curTemplate->m_uiName = "Camera_/Fade Effects/Motion blur zoom.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::BOOLEAN;
	curTemplate->m_parameters[1] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Blur zoom, zoom in = ";
	curTemplate->m_uiStrings[1] = " (true=zoom in, false = zoom out), saturate colors = ";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_MOTION_BLUR_JUMP];
	curTemplate->m_internalName = "CAMERA_MOTION_BLUR_JUMP";
	curTemplate->m_uiName = "Camera_/Fade Effects/Motion blur zoom with jump cut.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::WAYPOINT;
	curTemplate->m_parameters[1] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Blur zoom, zoom in at current location, zoom out at ";
	curTemplate->m_uiStrings[1] = ", saturate colors = ";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_MOTION_BLUR_FOLLOW];
	curTemplate->m_internalName = "CAMERA_MOTION_BLUR_FOLLOW";
	curTemplate->m_uiName = "Camera_/Fade Effects/Start motion blur as the camera moves.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Start motion blur as the camera moves, amount= ";
	curTemplate->m_uiStrings[1] = " (start with 30 and adjust up or down). ";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_MOTION_BLUR_END_FOLLOW];
	curTemplate->m_internalName = "CAMERA_MOTION_BLUR_END_FOLLOW";
	curTemplate->m_uiName = "Camera_/Fade Effects/End motion blur as the camera moves.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "End motion blur as the camera moves.";

	curTemplate = &m_actionTemplates[ScriptAction::DRAW_SKYBOX_BEGIN];
	curTemplate->m_internalName = "DRAW_SKYBOX_BEGIN";
	curTemplate->m_uiName = "Camera_/Skybox/Start skybox mode.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Start skybox mode (draw sky background).";

	curTemplate = &m_actionTemplates[ScriptAction::DRAW_SKYBOX_END];
	curTemplate->m_internalName = "DRAW_SKYBOX_END";
	curTemplate->m_uiName = "Camera_/Skybox/End skybox mode.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "End skybox mode (draw black background).";

	curTemplate = &m_actionTemplates[ScriptAction::FREEZE_TIME];
	curTemplate->m_internalName = "FREEZE_TIME";
	curTemplate->m_uiName = "Scripting_/Time/Freeze time.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Freeze time.";

	curTemplate = &m_actionTemplates[ScriptAction::UNFREEZE_TIME];
	curTemplate->m_internalName = "UNFREEZE_TIME";
	curTemplate->m_uiName = "Scripting_/Time/Unfreeze time.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Unfreeze time.";

	curTemplate = &m_actionTemplates[ScriptAction::SHOW_MILITARY_CAPTION];
	curTemplate->m_internalName = "SHOW_MILITARY_CAPTION";
	curTemplate->m_uiName = "Scripting_/Briefing/Show military briefing caption.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEXT_STRING;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Show military briefing ";
	curTemplate->m_uiStrings[1] = " for ";
	curTemplate->m_uiStrings[2] = " milliseconds.";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_SET_AUDIBLE_DISTANCE];
	curTemplate->m_internalName = "CAMERA_SET_AUDIBLE_DISTANCE";
	curTemplate->m_uiName = "Camera_/Sounds/Set the audible distance for camera-up shots.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Set the audible range during camera-up shots to ";
	
	curTemplate = &m_actionTemplates[ScriptAction::NAMED_SET_HELD];
	curTemplate->m_internalName = "NAMED_SET_HELD";
	curTemplate->m_uiName = "Unit_/Move/Set unit to be held in place, ignoring Physics, Locomotors, etc.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Set Held status for ";
	curTemplate->m_uiStrings[1] = " to ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_SET_STOPPING_DISTANCE];
	curTemplate->m_internalName = "NAMED_SET_STOPPING_DISTANCE";
	curTemplate->m_uiName = "Unit_/Move/Set stopping distance for current locomotor.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Set stopping distance for ";
	curTemplate->m_uiStrings[1] = " to ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::SET_STOPPING_DISTANCE];
	curTemplate->m_internalName = "SET_STOPPING_DISTANCE";
	curTemplate->m_uiName = "Team_/Move/Set stopping distance for each unit's current locomotor.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Set stopping distances for ";
	curTemplate->m_uiStrings[1] = " to ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::SET_FPS_LIMIT];
	curTemplate->m_internalName = "SET_FPS_LIMIT";
	curTemplate->m_uiName = "Scripting_/ Set max frames per second.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::INT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Set max FPS to ";
	curTemplate->m_uiStrings[1] = ".  (0 sets to default.)";

	curTemplate = &m_actionTemplates[ScriptAction::DISABLE_SPECIAL_POWER_DISPLAY];
	curTemplate->m_internalName = "DISABLE_SPECIAL_POWER_DISPLAY";
	curTemplate->m_uiName = "Scripting_/ Special power countdown display -- disable.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Disables special power countdown display.";

	curTemplate = &m_actionTemplates[ScriptAction::ENABLE_SPECIAL_POWER_DISPLAY];
	curTemplate->m_internalName = "ENABLE_SPECIAL_POWER_DISPLAY";
	curTemplate->m_uiName = "Scripting_/ Special power countdown display -- enable.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Enables special power countdown display.";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_HIDE_SPECIAL_POWER_DISPLAY];
	curTemplate->m_internalName = "NAMED_HIDE_SPECIAL_POWER_DISPLAY";
	curTemplate->m_uiName = "Unit_/ Special power countdown timer -- hide.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Hides special power countdowns for ";
	curTemplate->m_uiStrings[1] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_SHOW_SPECIAL_POWER_DISPLAY];
	curTemplate->m_internalName = "NAMED_SHOW_SPECIAL_POWER_DISPLAY";
	curTemplate->m_uiName = "Unit_/ Special power countdown timer -- display.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Shows special power countdowns for ";
	curTemplate->m_uiStrings[1] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::MUSIC_SET_VOLUME];
	curTemplate->m_internalName = "MUSIC_SET_VOLUME";
	curTemplate->m_uiName = "Multimedia_/ Set the current music volume.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Set the desired music volume to ";
	curTemplate->m_uiStrings[1] = "%. (0-100)";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_TRANSFER_TO_PLAYER];
	curTemplate->m_internalName = "TEAM_TRANSFER_TO_PLAYER";
	curTemplate->m_uiName = "Team_/ Transfer control of a team to a player.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Control of ";
	curTemplate->m_uiStrings[1] = " transfers to ";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_SET_MONEY];
	curTemplate->m_internalName = "PLAYER_SET_MONEY";
	curTemplate->m_uiName = "Player_/ Set player's money.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Set ";
	curTemplate->m_uiStrings[1] = "'s money to $";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_GIVE_MONEY];
	curTemplate->m_internalName = "PLAYER_GIVE_MONEY";
	curTemplate->m_uiName = "Player_/ Gives/takes from player's money.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " gets $";

	curTemplate = &m_actionTemplates[ScriptAction::DISPLAY_COUNTER];
	curTemplate->m_internalName = "DISPLAY_COUNTER";
	curTemplate->m_uiName = "Scripting_/ Counter -- display an individual counter to the user.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::COUNTER;
	curTemplate->m_parameters[1] = Parameter::LOCALIZED_TEXT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Show ";
	curTemplate->m_uiStrings[1] = " with text ";

	curTemplate = &m_actionTemplates[ScriptAction::HIDE_COUNTER];
	curTemplate->m_internalName = "HIDE_COUNTER";
	curTemplate->m_uiName = "Scripting_/ Counter -- hides an individual counter from the user.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::COUNTER;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Hide ";

	curTemplate = &m_actionTemplates[ScriptAction::DISPLAY_COUNTDOWN_TIMER];
	curTemplate->m_internalName = "DISPLAY_COUNTDOWN_TIMER";
	curTemplate->m_uiName = "Scripting_/ Timer -- display an individual timer to the user.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::COUNTER;
	curTemplate->m_parameters[1] = Parameter::LOCALIZED_TEXT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Show ";
	curTemplate->m_uiStrings[1] = " with text ";

	curTemplate = &m_actionTemplates[ScriptAction::HIDE_COUNTDOWN_TIMER];
	curTemplate->m_internalName = "HIDE_COUNTDOWN_TIMER";
	curTemplate->m_uiName = "Scripting_/ Timer -- hides an individual timer from the user.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::COUNTER;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Hide ";

	curTemplate = &m_actionTemplates[ScriptAction::DISABLE_COUNTDOWN_TIMER_DISPLAY];
	curTemplate->m_internalName = "DISABLE_COUNTDOWN_TIMER_DISPLAY";
	curTemplate->m_uiName = "Scripting_/ Timer -- hide all timers from the user.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Disables timer display.";

	curTemplate = &m_actionTemplates[ScriptAction::ENABLE_COUNTDOWN_TIMER_DISPLAY];
	curTemplate->m_internalName = "ENABLE_COUNTDOWN_TIMER_DISPLAY";
	curTemplate->m_uiName = "Scripting_/ Timer -- display all timers to the user.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Enables timer display.";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_STOP_SPECIAL_POWER_COUNTDOWN];
	curTemplate->m_internalName = "NAMED_STOP_SPECIAL_POWER_COUNTDOWN";
	curTemplate->m_uiName = "Unit_/ Special power countdown timer -- pause.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::SPECIAL_POWER;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Pause ";
	curTemplate->m_uiStrings[1] = "'s ";
	curTemplate->m_uiStrings[2] = " countdown.";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_START_SPECIAL_POWER_COUNTDOWN];
	curTemplate->m_internalName = "NAMED_START_SPECIAL_POWER_COUNTDOWN";
	curTemplate->m_uiName = "Unit_/ Special power countdown timer -- resume.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::SPECIAL_POWER;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Resume ";
	curTemplate->m_uiStrings[1] = "'s ";
	curTemplate->m_uiStrings[2] = " countdown.";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_SET_SPECIAL_POWER_COUNTDOWN];
	curTemplate->m_internalName = "NAMED_SET_SPECIAL_POWER_COUNTDOWN";
	curTemplate->m_uiName = "Unit_/ Special power countdown timer -- set.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::SPECIAL_POWER;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "Set ";
	curTemplate->m_uiStrings[1] = "'s ";
	curTemplate->m_uiStrings[2] = " to ";
	curTemplate->m_uiStrings[3] = " seconds.";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_ADD_SPECIAL_POWER_COUNTDOWN];
	curTemplate->m_internalName = "NAMED_ADD_SPECIAL_POWER_COUNTDOWN";
	curTemplate->m_uiName = "Unit_/ Special power countdown timer -- add seconds.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::SPECIAL_POWER;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = "'s ";
	curTemplate->m_uiStrings[2] = " has ";
	curTemplate->m_uiStrings[3] = " seconds added to it.";

	curTemplate = &m_actionTemplates[ScriptAction::SKIRMISH_FIRE_SPECIAL_POWER_AT_MOST_COST];
	curTemplate->m_internalName = "SKIRMISH_FIRE_SPECIAL_POWER_AT_MOST_COST";
	curTemplate->m_uiName = "Skirmish_/ Special power -- fire at enemy's highest cost area.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::SPECIAL_POWER;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " fire ";
	curTemplate->m_uiStrings[2] = " at enemy's most costly area.";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_REPAIR_NAMED_STRUCTURE];
	curTemplate->m_internalName = "PLAYER_REPAIR_NAMED_STRUCTURE";
	curTemplate->m_uiName = "Player_/ Repair named bridge or structure.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Have ";
	curTemplate->m_uiStrings[1] = " repair ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_FIRE_SPECIAL_POWER_AT_WAYPOINT];
	curTemplate->m_internalName = "NAMED_FIRE_SPECIAL_POWER_AT_WAYPOINT";
	curTemplate->m_uiName = "Unit_/ Special power -- fire at location.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::SPECIAL_POWER;
	curTemplate->m_parameters[2] = Parameter::WAYPOINT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " fires ";
	curTemplate->m_uiStrings[2] = " at ";
	curTemplate->m_uiStrings[3] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_FIRE_SPECIAL_POWER_AT_NAMED];
	curTemplate->m_internalName = "NAMED_FIRE_SPECIAL_POWER_AT_NAMED";
	curTemplate->m_uiName = "Unit_/ Special power -- fire at unit.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::SPECIAL_POWER;
	curTemplate->m_parameters[2] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " fires ";
	curTemplate->m_uiStrings[2] = " at ";
	curTemplate->m_uiStrings[3] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::REFRESH_RADAR];
	curTemplate->m_internalName = "REFRESH_RADAR";
	curTemplate->m_uiName = "Scripting_/ Refresh radar terrain.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Refresh radar terrain.";
	
	curTemplate = &m_actionTemplates[ScriptAction::NAMED_STOP];
	curTemplate->m_internalName = "NAMED_STOP";
	curTemplate->m_uiName = "Unit_/ Set a specific unit to stop.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " stops.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_STOP];
	curTemplate->m_internalName = "TEAM_STOP";
	curTemplate->m_uiName = "Team_/ Set to stop.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " stops.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_STOP_AND_DISBAND];
	curTemplate->m_internalName = "TEAM_STOP_AND_DISBAND";
	curTemplate->m_uiName = "Team_/ Set to stop, then disband.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " stops, then disbands.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_SET_OVERRIDE_RELATION_TO_TEAM];
	curTemplate->m_internalName = "TEAM_SET_OVERRIDE_RELATION_TO_TEAM";
	curTemplate->m_uiName = "Team_/ Override a team's relationship to another team.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::TEAM;
	curTemplate->m_parameters[2] = Parameter::RELATION;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " considers ";
	curTemplate->m_uiStrings[2] = " to be ";
	curTemplate->m_uiStrings[3] = " (rather than using the the player relationship).";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_REMOVE_OVERRIDE_RELATION_TO_TEAM];
	curTemplate->m_internalName = "TEAM_REMOVE_OVERRIDE_RELATION_TO_TEAM";
	curTemplate->m_uiName = "Team_/ Remove an override to a team's relationship to another team.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " uses the player relationship to ";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_REMOVE_ALL_OVERRIDE_RELATIONS];
	curTemplate->m_internalName = "TEAM_REMOVE_ALL_OVERRIDE_RELATIONS";
	curTemplate->m_uiName = "Team_/ Remove all overrides to team's relationship to teams and/or players.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " uses the player relationship to all other teams and players.";

 	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_TETHER_NAMED];
	curTemplate->m_internalName = "CAMERA_TETHER_NAMED";
	curTemplate->m_uiName = "Camera_/ Tether camera to a specific unit.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::BOOLEAN;
	curTemplate->m_parameters[2] = Parameter::REAL;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "Have the camera tethered to ";
	curTemplate->m_uiStrings[1] = ".  Snap camera to object is ";
	curTemplate->m_uiStrings[2] = ".  Amount of play is ";
	curTemplate->m_uiStrings[3] = ".";

 	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_STOP_TETHER_NAMED];
	curTemplate->m_internalName = "CAMERA_STOP_TETHER_NAMED";
	curTemplate->m_uiName = "Camera_/ Stop tether to any units.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Stop tether to any units.";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_SET_DEFAULT];
	curTemplate->m_internalName = "CAMERA_SET_DEFAULT";
	curTemplate->m_uiName = "Camera_/ Set default camera.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::REAL;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "Camera Pitch = ";
	curTemplate->m_uiStrings[1] = "(0.0==default), angle = ";
	curTemplate->m_uiStrings[2] = "(0.0 is N, 90.0 is W, etc), height = ";
	curTemplate->m_uiStrings[3] = "(1.0==default).";

 	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_LOOK_TOWARD_OBJECT];
	curTemplate->m_internalName = "CAMERA_LOOK_TOWARD_OBJECT";
	curTemplate->m_uiName = "Camera (R)_/ Rotate toward unit.";
	curTemplate->m_numParameters = 5;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::REAL;
	curTemplate->m_parameters[3] = Parameter::REAL;
	curTemplate->m_parameters[4] = Parameter::REAL;
	curTemplate->m_numUiStrings = 6;
	curTemplate->m_uiStrings[0] = "Rotate toward ";
	curTemplate->m_uiStrings[1] = ", taking ";
	curTemplate->m_uiStrings[2] = " seconds and holding ";
	curTemplate->m_uiStrings[3] = " seconds, ease-in ";
	curTemplate->m_uiStrings[4] = " seconds, ease-out ";
	curTemplate->m_uiStrings[5] = " seconds.";

 	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_LOOK_TOWARD_WAYPOINT];
	curTemplate->m_internalName = "CAMERA_LOOK_TOWARD_WAYPOINT";
	curTemplate->m_uiName = "Camera (R)_/ Rotate to look at a waypoint.";
	curTemplate->m_numParameters = 5;
	curTemplate->m_parameters[0] = Parameter::WAYPOINT;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::REAL;
	curTemplate->m_parameters[3] = Parameter::REAL;
	curTemplate->m_parameters[4] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 6;
	curTemplate->m_uiStrings[0] = "Rotate to look at ";
	curTemplate->m_uiStrings[1] = ", taking ";
	curTemplate->m_uiStrings[2] = " seconds, ease-in ";
	curTemplate->m_uiStrings[3] = " seconds, ease-out ";
	curTemplate->m_uiStrings[4] = " seconds, reverse rotation ";
	curTemplate->m_uiStrings[5] = ".";

 	curTemplate = &m_actionTemplates[ScriptAction::UNIT_DESTROY_ALL_CONTAINED];
	curTemplate->m_internalName = "UNIT_DESTROY_ALL_CONTAINED";
	curTemplate->m_uiName = "Unit_/ Kill all units contained within a specific transport or structure.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
//	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "All units inside ";
	curTemplate->m_uiStrings[1] = " are killed.";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_FIRE_WEAPON_FOLLOWING_WAYPOINT_PATH];
	curTemplate->m_internalName = "NAMED_FIRE_WEAPON_FOLLOWING_WAYPOINT_PATH";
	curTemplate->m_uiName = "Unit_/ Fire waypoint-weapon following waypoint path.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::WAYPOINT_PATH;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " fire waypoint-weapon following waypoints starting at ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_SET_OVERRIDE_RELATION_TO_PLAYER];
	curTemplate->m_internalName = "TEAM_SET_OVERRIDE_RELATION_TO_PLAYER";
	curTemplate->m_uiName = "Team_/ Override a team's relationship to another player.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::SIDE;
	curTemplate->m_parameters[2] = Parameter::RELATION;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " considers ";
	curTemplate->m_uiStrings[2] = " to be ";
	curTemplate->m_uiStrings[3] = " (rather than using the the player relationship).";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_REMOVE_OVERRIDE_RELATION_TO_PLAYER];
	curTemplate->m_internalName = "TEAM_REMOVE_OVERRIDE_RELATION_TO_PLAYER";
	curTemplate->m_uiName = "Team_/ Remove an override to a team's relationship to another player.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " uses the player relationship to ";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_SET_OVERRIDE_RELATION_TO_TEAM];
	curTemplate->m_internalName = "PLAYER_SET_OVERRIDE_RELATION_TO_TEAM";
	curTemplate->m_uiName = "Player_/ Override a player's relationship to another team.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::TEAM;
	curTemplate->m_parameters[2] = Parameter::RELATION;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " considers ";
	curTemplate->m_uiStrings[2] = " to be ";
	curTemplate->m_uiStrings[3] = " (rather than using the the player relationship).";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_REMOVE_OVERRIDE_RELATION_TO_TEAM];
	curTemplate->m_internalName = "PLAYER_REMOVE_OVERRIDE_RELATION_TO_TEAM";
	curTemplate->m_uiName = "Player_/ Remove an override to a player's relationship to another team.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " uses the player relationship to ";

	curTemplate = &m_actionTemplates[ScriptAction::UNIT_EXECUTE_SEQUENTIAL_SCRIPT];
	curTemplate->m_internalName = "UNIT_EXECUTE_SEQUENTIAL_SCRIPT";
	curTemplate->m_uiName = "Unit_/ Set a specific unit to execute a script sequentially.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::SCRIPT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " executes ";
	curTemplate->m_uiStrings[2] = " sequentially.";

	curTemplate = &m_actionTemplates[ScriptAction::UNIT_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING];
	curTemplate->m_internalName = "UNIT_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING";
	curTemplate->m_uiName = "Unit_/ Set a specific unit to execute a looping sequential script.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::SCRIPT;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " executes ";
	curTemplate->m_uiStrings[2] = " sequentially, ";
	curTemplate->m_uiStrings[3] = " times. (0=forever)";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_EXECUTE_SEQUENTIAL_SCRIPT];
	curTemplate->m_internalName = "TEAM_EXECUTE_SEQUENTIAL_SCRIPT";
	curTemplate->m_uiName = "Team_/ Execute script sequentially -- start.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::SCRIPT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " executes ";
	curTemplate->m_uiStrings[2] = " sequentially.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING];
	curTemplate->m_internalName = "TEAM_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING";
	curTemplate->m_uiName = "Team_/ Execute script sequentially -- looping.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::SCRIPT;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " executes ";
	curTemplate->m_uiStrings[2] = " sequentially, ";
	curTemplate->m_uiStrings[3] = " times. (0=forever)";

	curTemplate = &m_actionTemplates[ScriptAction::UNIT_STOP_SEQUENTIAL_SCRIPT];
	curTemplate->m_internalName = "UNIT_STOP_SEQUENTIAL_SCRIPT";
	curTemplate->m_uiName = "Unit_/ Set a specific unit to stop executing a sequential script.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " stops executing.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_STOP_SEQUENTIAL_SCRIPT];
	curTemplate->m_internalName = "TEAM_STOP_SEQUENTIAL_SCRIPT";
	curTemplate->m_uiName = "Team_/ Execute script sequentially -- stop.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " stops executing.";

	curTemplate = &m_actionTemplates[ScriptAction::UNIT_GUARD_FOR_FRAMECOUNT];
	curTemplate->m_internalName = "UNIT_GUARD_FOR_FRAMECOUNT";
	curTemplate->m_uiName = "Unit_/ Set to guard for some number of frames.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " guards for ";
	curTemplate->m_uiStrings[2] = " frames.";

	curTemplate = &m_actionTemplates[ScriptAction::UNIT_IDLE_FOR_FRAMECOUNT];
	curTemplate->m_internalName = "UNIT_IDLE_FOR_FRAMECOUNT";
	curTemplate->m_uiName = "Unit_/ Set to idle for some number of frames.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " idles for ";
	curTemplate->m_uiStrings[2] = " frames.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_GUARD_FOR_FRAMECOUNT];
	curTemplate->m_internalName = "TEAM_GUARD_FOR_FRAMECOUNT";
	curTemplate->m_uiName = "Team_/ Set to guard -- number of frames.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " guards for ";
	curTemplate->m_uiStrings[2] = " frames.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_IDLE_FOR_FRAMECOUNT];
	curTemplate->m_internalName = "TEAM_IDLE_FOR_FRAMECOUNT";
	curTemplate->m_uiName = "Team_/ Set to idle for some number of frames.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " idles for ";
	curTemplate->m_uiStrings[2] = " frames.";

	curTemplate = &m_actionTemplates[ScriptAction::WATER_CHANGE_HEIGHT];
	curTemplate->m_internalName = "WATER_CHANGE_HEIGHT";
	curTemplate->m_uiName = "Map_/ Adjust water height to a new level";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TRIGGER_AREA;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " changes altitude to ";

	curTemplate = &m_actionTemplates[ ScriptAction::WATER_CHANGE_HEIGHT_OVER_TIME ];
	curTemplate->m_internalName = "WATER_CHANGE_HEIGHT_OVER_TIME";
	curTemplate->m_uiName = "Map_/ Adjust water height to a new level with damage over time";
	curTemplate->m_numParameters = 4;
	curTemplate->m_parameters[0] = Parameter::TRIGGER_AREA;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::REAL;
	curTemplate->m_parameters[3] = Parameter::REAL;
	curTemplate->m_numUiStrings = 5;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " changes altitude to ";
	curTemplate->m_uiStrings[2] = " in ";
	curTemplate->m_uiStrings[3] = " seconds doing ";
	curTemplate->m_uiStrings[4] = " dam_/sec.";

	curTemplate = &m_actionTemplates[ ScriptAction::NAMED_USE_COMMANDBUTTON_ABILITY ];
	curTemplate->m_internalName = "NAMED_USE_COMMANDBUTTON_ABILITY";
	curTemplate->m_uiName = "Unit_/ Use commandbutton ability.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::COMMANDBUTTON_ABILITY;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " use ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_actionTemplates[ ScriptAction::NAMED_USE_COMMANDBUTTON_ABILITY_ON_NAMED ];
	curTemplate->m_internalName = "NAMED_USE_COMMANDBUTTON_ABILITY_ON_NAMED";
	curTemplate->m_uiName = "Unit_/ Use commandbutton ability on an object.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::COMMANDBUTTON_ABILITY;
	curTemplate->m_parameters[2] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " use ";
	curTemplate->m_uiStrings[2] = " on ";
	curTemplate->m_uiStrings[3] = ".";

	curTemplate = &m_actionTemplates[ ScriptAction::NAMED_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT ];
	curTemplate->m_internalName = "NAMED_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT";
	curTemplate->m_uiName = "Unit_/ Use commandbutton ability at a waypoint.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::COMMANDBUTTON_ABILITY;
	curTemplate->m_parameters[2] = Parameter::WAYPOINT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " use ";
	curTemplate->m_uiStrings[2] = " at ";
	curTemplate->m_uiStrings[3] = ".";

	curTemplate = &m_actionTemplates[ ScriptAction::NAMED_USE_COMMANDBUTTON_ABILITY_USING_WAYPOINT_PATH ];
	curTemplate->m_internalName = "NAMED_USE_COMMANDBUTTON_ABILITY_USING_WAYPOINT_PATH";
	curTemplate->m_uiName = "Unit_/ Use commandbutton ability using a waypoint path.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::COMMANDBUTTON_ABILITY;
	curTemplate->m_parameters[2] = Parameter::WAYPOINT_PATH;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " use ";
	curTemplate->m_uiStrings[2] = " to follow ";
	curTemplate->m_uiStrings[3] = " path.";

	curTemplate = &m_actionTemplates[ ScriptAction::TEAM_USE_COMMANDBUTTON_ABILITY ];
	curTemplate->m_internalName = "TEAM_USE_COMMANDBUTTON_ABILITY";
	curTemplate->m_uiName = "Team_/ Use commandbutton ability.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::COMMANDBUTTON_ALL_ABILITIES;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " use ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_actionTemplates[ ScriptAction::TEAM_USE_COMMANDBUTTON_ABILITY_ON_NAMED ];
	curTemplate->m_internalName = "TEAM_USE_COMMANDBUTTON_ABILITY_ON_NAMED";
	curTemplate->m_uiName = "Team_/ Use commandbutton ability on an object.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::COMMANDBUTTON_ALL_ABILITIES;
	curTemplate->m_parameters[2] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " use ";
	curTemplate->m_uiStrings[2] = " on ";
	curTemplate->m_uiStrings[3] = ".";

	curTemplate = &m_actionTemplates[ ScriptAction::TEAM_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT ];
	curTemplate->m_internalName = "TEAM_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT";
	curTemplate->m_uiName = "Team_/ Use commandbutton ability at a waypoint.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::COMMANDBUTTON_ALL_ABILITIES;
	curTemplate->m_parameters[2] = Parameter::WAYPOINT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " use ";
	curTemplate->m_uiStrings[2] = " at ";
	curTemplate->m_uiStrings[3] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::MAP_SWITCH_BORDER];
	curTemplate->m_internalName = "MAP_SWITCH_BORDER";
	curTemplate->m_uiName = "Map_/ Change the active boundary.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::BOUNDARY;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " becomes the active border.";

	curTemplate = &m_actionTemplates[ScriptAction::OBJECT_FORCE_SELECT];
	curTemplate->m_internalName = "OBJECT_FORCE_SELECT";
	curTemplate->m_uiName = "Scripting_/ Select the first object type on a team.";
	curTemplate->m_numParameters = 4;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::OBJECT_TYPE;
	curTemplate->m_parameters[2] = Parameter::BOOLEAN;
	curTemplate->m_parameters[3] = Parameter::DIALOG;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " 's first ";
	curTemplate->m_uiStrings[2] = ", centers in view (";
	curTemplate->m_uiStrings[3] = ") while playing ";

	curTemplate = &m_actionTemplates[ScriptAction::RADAR_FORCE_ENABLE];
	curTemplate->m_internalName = "RADAR_FORCE_ENABLE";
	curTemplate->m_uiName = "Radar_/ Force enable the radar.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "The radar is now forced to be enabled.";


	curTemplate = &m_actionTemplates[ScriptAction::RADAR_REVERT_TO_NORMAL];
	curTemplate->m_internalName = "RADAR_REVERT_TO_NORMAL";
	curTemplate->m_uiName = "Radar_/ Revert radar to normal behavior.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "The radar is now reverting to its normal behavior.";

	curTemplate = &m_actionTemplates[ScriptAction::SCREEN_SHAKE];
	curTemplate->m_internalName = "SCREEN_SHAKE";
	curTemplate->m_uiName = "Camera_/ Shake Screen.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SHAKE_INTENSITY;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "The screen will shake with ";

	curTemplate = &m_actionTemplates[ScriptAction::TECHTREE_MODIFY_BUILDABILITY_OBJECT];
	curTemplate->m_internalName = "TECHTREE_MODIFY_BUILDABILITY_OBJECT";
	curTemplate->m_uiName = "Map_/ Adjust the tech tree for a specific object type.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::OBJECT_TYPE;
	curTemplate->m_parameters[1] = Parameter::BUILDABLE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " becomes ";

 	curTemplate = &m_actionTemplates[ScriptAction::SET_CAVE_INDEX];
	curTemplate->m_internalName = "SET_CAVE_INDEX";
	curTemplate->m_uiName = "Unit_/ Set Cave connectivity index.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Cave named ";
	curTemplate->m_uiStrings[1] = " is set to being connected to all caves of index ";
	curTemplate->m_uiStrings[2] = ", but only if both Cave listings have no occupants. ";

 	curTemplate = &m_actionTemplates[ScriptAction::WAREHOUSE_SET_VALUE];
	curTemplate->m_internalName = "WAREHOUSE_SET_VALUE";
	curTemplate->m_uiName = "Unit_/ Set cash value of Warehouse.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Warehouse named ";
	curTemplate->m_uiStrings[1] = " is set to having ";
	curTemplate->m_uiStrings[2] = " dollars worth of boxes. ";

 	curTemplate = &m_actionTemplates[ScriptAction::SOUND_DISABLE_TYPE];
	curTemplate->m_internalName = "SOUND_DISABLE_TYPE";
	curTemplate->m_uiName = "Multimedia_/ Sound Events -- disable type.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SOUND;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is disabled.";

 	curTemplate = &m_actionTemplates[ScriptAction::SOUND_ENABLE_TYPE];
	curTemplate->m_internalName = "SOUND_ENABLE_TYPE";
	curTemplate->m_uiName = "Multimedia_/ Sound Events -- enable type.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SOUND;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is enabled.";

 	curTemplate = &m_actionTemplates[ScriptAction::SOUND_REMOVE_TYPE];
	curTemplate->m_internalName = "SOUND_REMOVE_TYPE";
	curTemplate->m_uiName = "Multimedia_/ Sound Events -- remove type.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SOUND;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is removed.";

 	curTemplate = &m_actionTemplates[ScriptAction::SOUND_REMOVE_ALL_DISABLED];
	curTemplate->m_internalName = "SOUND_REMOVE_ALL_DISABLED";
	curTemplate->m_uiName = "Multimedia_/ Sound Events -- remove all disabled.";
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Remove all disabled sound events.";

 	curTemplate = &m_actionTemplates[ScriptAction::SOUND_ENABLE_ALL];
	curTemplate->m_internalName = "SOUND_ENABLE_ALL";
	curTemplate->m_uiName = "Multimedia_/ Sound Events -- enable all.";
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Enable all sound events.";

 	curTemplate = &m_actionTemplates[ScriptAction::AUDIO_OVERRIDE_VOLUME_TYPE];
	curTemplate->m_internalName = "AUDIO_OVERRIDE_VOLUME_TYPE";
	curTemplate->m_uiName = "Multimedia_/ Sound Events -- override volume -- type.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SOUND;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " play at ";
	curTemplate->m_uiStrings[2] = "% of full volume.";

 	curTemplate = &m_actionTemplates[ScriptAction::AUDIO_RESTORE_VOLUME_TYPE];
	curTemplate->m_internalName = "AUDIO_RESTORE_VOLUME_TYPE";
	curTemplate->m_uiName = "Multimedia_/ Sound Events -- restore volume -- type.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SOUND;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " play at normal volume.";

 	curTemplate = &m_actionTemplates[ScriptAction::AUDIO_RESTORE_VOLUME_ALL_TYPE];
	curTemplate->m_internalName = "AUDIO_RESTORE_VOLUME_ALL_TYPE";
	curTemplate->m_uiName = "Multimedia_/ Sound Events -- restore volume -- all.";
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "All sound events play at normal volume.";

 	curTemplate = &m_actionTemplates[ScriptAction::NAMED_SET_TOPPLE_DIRECTION];
	curTemplate->m_internalName = "NAMED_SET_TOPPLE_DIRECTION";
	curTemplate->m_uiName = "Unit_/ Set topple direction.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::COORD3D;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " will topple towards ";
	curTemplate->m_uiStrings[2] = " if destroyed.";

 	curTemplate = &m_actionTemplates[ScriptAction::UNIT_MOVE_TOWARDS_NEAREST_OBJECT_TYPE];
	curTemplate->m_internalName = "UNIT_MOVE_TOWARDS_NEAREST_OBJECT_TYPE";
	curTemplate->m_uiName = "Unit_/ Move unit towards the nearest object of a specific type.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::OBJECT_TYPE;
	curTemplate->m_parameters[2] = Parameter::TRIGGER_AREA;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " will move towards the nearest ";
	curTemplate->m_uiStrings[2] = " within ";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_MOVE_TOWARDS_NEAREST_OBJECT_TYPE];
	curTemplate->m_internalName = "TEAM_MOVE_TOWARDS_NEAREST_OBJECT_TYPE";
	curTemplate->m_uiName = "Team_/ Move team towards the nearest object of a specific type.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::OBJECT_TYPE;
	curTemplate->m_parameters[2] = Parameter::TRIGGER_AREA;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " will move towards the nearest ";
	curTemplate->m_uiStrings[2] = " within ";
	
 	curTemplate = &m_actionTemplates[ScriptAction::SKIRMISH_ATTACK_NEAREST_GROUP_WITH_VALUE];
	curTemplate->m_internalName = "SKIRMISH_ATTACK_NEAREST_GROUP_WITH_VALUE";
	curTemplate->m_uiName = "Skirmish_/ Team attacks nearest group matching value comparison.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::COMPARISON;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " attacks nearest group worth ";
	curTemplate->m_uiStrings[2] = " ";

 	curTemplate = &m_actionTemplates[ScriptAction::SKIRMISH_PERFORM_COMMANDBUTTON_ON_MOST_VALUABLE_OBJECT];
	curTemplate->m_internalName = "SKIRMISH_PERFORM_COMMANDBUTTON_ON_MOST_VALUABLE_OBJECT";
	curTemplate->m_uiName = "Skirmish_/ Team performs command ability on most valuable object.";
	curTemplate->m_numParameters = 4;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::COMMANDBUTTON_ALL_ABILITIES;
	curTemplate->m_parameters[2] = Parameter::REAL;
	curTemplate->m_parameters[3] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 5;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " performs ";
	curTemplate->m_uiStrings[2] = " on most expensive object within ";
	curTemplate->m_uiStrings[3] = " ";
	curTemplate->m_uiStrings[4] = " (true = all valid sources, false = first valid source).";

 	curTemplate = &m_actionTemplates[ScriptAction::SKIRMISH_WAIT_FOR_COMMANDBUTTON_AVAILABLE_ALL];
	curTemplate->m_internalName = "SKIRMISH_WAIT_FOR_COMMANDBUTTON_AVAILABLE_ALL";
	curTemplate->m_uiName = "Skirmish_/ Delay a sequential script until the specified command ability is ready - all.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::TEAM;
	curTemplate->m_parameters[2] = Parameter::COMMANDBUTTON_ALL_ABILITIES;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " 's ";
	curTemplate->m_uiStrings[2] = " all wait until ";
	curTemplate->m_uiStrings[3] = " is ready.";
	
 	curTemplate = &m_actionTemplates[ScriptAction::SKIRMISH_WAIT_FOR_COMMANDBUTTON_AVAILABLE_PARTIAL];
	curTemplate->m_internalName = "SKIRMISH_WAIT_FOR_COMMANDBUTTON_AVAILABLE_PARTIAL";
	curTemplate->m_uiName = "Skirmish_/ Delay a sequential script until the specified command ability is ready - partial.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::TEAM;
	curTemplate->m_parameters[2] = Parameter::COMMANDBUTTON_ALL_ABILITIES;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " 's ";
	curTemplate->m_uiStrings[2] = " wait until at least one member is ";
	curTemplate->m_uiStrings[3] = " ready.";
	
 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_SPIN_FOR_FRAMECOUNT];
	curTemplate->m_internalName = "TEAM_SPIN_FOR_FRAMECOUNT";
	curTemplate->m_uiName = "Team_/ Set to continue current action for some number of frames.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " continue their current action for at least ";
	curTemplate->m_uiStrings[2] = " frames.";

 	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_FADE_MULTIPLY];
	curTemplate->m_internalName = "CAMERA_FADE_MULTIPLY";
	curTemplate->m_uiName = "Camera_/Fade Effects/Fade using a multiply blend to black.";

 	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_ENABLE_SLAVE_MODE];
	curTemplate->m_internalName = "CAMERA_ENABLE_SLAVE_MODE";
	curTemplate->m_uiName = "Camera_/Enable 3DSMax Camera Animation Playback mode.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_parameters[0] = Parameter::TEXT_STRING;
	curTemplate->m_parameters[1] = Parameter::TEXT_STRING;
	curTemplate->m_uiStrings[0] = "Enable 3DSMax Camera playback of animation with thing name ";
	curTemplate->m_uiStrings[1] = " containing bone name ";

 	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_DISABLE_SLAVE_MODE];
	curTemplate->m_internalName = "CAMERA_DISABLE_SLAVE_MODE";
	curTemplate->m_uiName = "Camera_/Disable 3DSMax Camera Animation Playback mode.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Disable camera playback mode.";

	curTemplate = &m_actionTemplates[ScriptAction::CAMERA_ADD_SHAKER_AT];
	curTemplate->m_internalName = "CAMERA_ADD_SHAKER_AT";
	curTemplate->m_uiName = "Camera_/Add Camera Shaker Effect at.";
	curTemplate->m_numParameters = 4;
	curTemplate->m_parameters[0] = Parameter::WAYPOINT;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::REAL;
	curTemplate->m_parameters[3] = Parameter::REAL;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "Add Camera Shaker Effect at waypoint ";
	curTemplate->m_uiStrings[1] = " with Amplitude ";
	curTemplate->m_uiStrings[2] = " Duration (seconds) ";
	curTemplate->m_uiStrings[3] = " Radius.";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NAMED];
	curTemplate->m_internalName = "TEAM_ALL_USE_COMMANDBUTTON_ON_NAMED";
	curTemplate->m_uiName = "Team_/ Use command ability -- all -- named enemy";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::COMMANDBUTTON_ALL_ABILITIES;
	curTemplate->m_parameters[2] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " use ";
	curTemplate->m_uiStrings[2] = "  on ";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_ENEMY_UNIT];
	curTemplate->m_internalName = "TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_ENEMY_UNIT";
	curTemplate->m_uiName = "Team_/ Use command ability -- all -- nearest enemy unit";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::COMMANDBUTTON_ALL_ABILITIES;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " use ";
	curTemplate->m_uiStrings[2] = "  on nearest enemy unit.";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_GARRISONED_BUILDING];
	curTemplate->m_internalName = "TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_GARRISONED_BUILDING";
	curTemplate->m_uiName = "Team_/ Use command ability -- all -- nearest enemy garrisoned building.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::COMMANDBUTTON_ALL_ABILITIES;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " use ";
	curTemplate->m_uiStrings[2] = "  on nearest enemy garrisoned building.";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_KINDOF];
	curTemplate->m_internalName = "TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_KINDOF";
	curTemplate->m_uiName = "Team_/ Use command ability -- all -- nearest enemy object with kind of.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::COMMANDBUTTON_ALL_ABILITIES;
	curTemplate->m_parameters[2] = Parameter::KIND_OF_PARAM;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " use ";
	curTemplate->m_uiStrings[2] = "  on nearest enemy with ";
	curTemplate->m_uiStrings[4] = ".";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_ENEMY_BUILDING];
	curTemplate->m_internalName = "TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_ENEMY_BUILDING";
	curTemplate->m_uiName = "Team_/ Use command ability -- all -- nearest enemy building.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::COMMANDBUTTON_ALL_ABILITIES;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " use ";
	curTemplate->m_uiStrings[2] = "  on nearest enemy building.";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_ENEMY_BUILDING_CLASS];
	curTemplate->m_internalName = "TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_ENEMY_BUILDING_CLASS";
	curTemplate->m_uiName = "Team_/ Use command ability -- all -- nearest enemy building kindof.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::COMMANDBUTTON_ALL_ABILITIES;
	curTemplate->m_parameters[2] = Parameter::KIND_OF_PARAM;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " use ";
	curTemplate->m_uiStrings[2] = "  on nearest enemy building with ";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_OBJECTTYPE];
	curTemplate->m_internalName = "TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_OBJECTTYPE";
	curTemplate->m_uiName = "Team_/ Use command ability -- all -- nearest object type.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::COMMANDBUTTON_ALL_ABILITIES;
	curTemplate->m_parameters[2] = Parameter::OBJECT_TYPE;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " use ";
	curTemplate->m_uiStrings[2] = " on nearest object of type ";
	curTemplate->m_uiStrings[3] = ".";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_PARTIAL_USE_COMMANDBUTTON];
	curTemplate->m_internalName = "TEAM_PARTIAL_USE_COMMANDBUTTON";
	curTemplate->m_uiName = "Team_/ Use command ability -- partial -- self.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_parameters[1] = Parameter::TEAM;
	curTemplate->m_parameters[2] = Parameter::COMMANDBUTTON_ALL_ABILITIES;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = "% of ";
	curTemplate->m_uiStrings[2] = " perform ";
	curTemplate->m_uiStrings[3] = ".";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_CAPTURE_NEAREST_UNOWNED_FACTION_UNIT];
	curTemplate->m_internalName = "TEAM_CAPTURE_NEAREST_UNOWNED_FACTION_UNIT";
	curTemplate->m_uiName = "Team_/ Capture unowned faction unit -- nearest.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " capture the nearest unowned faction unit.";

 	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_CREATE_TEAM_FROM_CAPTURED_UNITS];
	curTemplate->m_internalName = "PLAYER_CREATE_TEAM_FROM_CAPTURED_UNITS";
	curTemplate->m_uiName = "Player_/ Create team from all captured units.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " creates a new ";
	curTemplate->m_uiStrings[2] = " from units it has captured. (There's nothing quite like being assaulted by your own captured units!)";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_WAIT_FOR_NOT_CONTAINED_ALL];
	curTemplate->m_internalName = "TEAM_WAIT_FOR_NOT_CONTAINED_ALL";
	curTemplate->m_uiName = "Team_/ Delay a sequential script until the team is no longer contained - all";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " all delay until they are no longer contained.";

 	curTemplate = &m_actionTemplates[ScriptAction::TEAM_WAIT_FOR_NOT_CONTAINED_PARTIAL];
	curTemplate->m_internalName = "TEAM_WAIT_FOR_NOT_CONTAINED_PARTIAL";
	curTemplate->m_uiName = "Team_/ Delay a sequential script until the team is no longer contained - partial";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " delay until at least one of them is no longer contained.";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_SET_EMOTICON];
	curTemplate->m_internalName = "TEAM_SET_EMOTICON";
	curTemplate->m_uiName = "Team_/ Set emoticon for duration (-1.0 permanent, otherwise duration in sec).";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::EMOTICON;
	curTemplate->m_parameters[2] = Parameter::REAL;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " use ";
	curTemplate->m_uiStrings[2] = " emoticon for ";
	curTemplate->m_uiStrings[3] = " seconds.";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_SET_EMOTICON];
	curTemplate->m_internalName = "NAMED_SET_EMOTICON";
	curTemplate->m_uiName = "Unit_/ Set emoticon for duration (-1.0 permanent, otherwise duration in sec).";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::EMOTICON;
	curTemplate->m_parameters[2] = Parameter::REAL;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " use ";
	curTemplate->m_uiStrings[2] = " emoticon for ";
	curTemplate->m_uiStrings[3] = " seconds.";

 	curTemplate = &m_actionTemplates[ScriptAction::OBJECTLIST_ADDOBJECTTYPE];
	curTemplate->m_internalName = "OBJECTLIST_ADDOBJECTTYPE";
	curTemplate->m_uiName = "Scripting_/ Object Type List -- Add Object Type.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::OBJECT_TYPE_LIST;
	curTemplate->m_parameters[1] = Parameter::OBJECT_TYPE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " : add ";

 	curTemplate = &m_actionTemplates[ScriptAction::OBJECTLIST_REMOVEOBJECTTYPE];
	curTemplate->m_internalName = "OBJECTLIST_REMOVEOBJECTTYPE";
	curTemplate->m_uiName = "Scripting_/ Object Type List -- Remove Object Type.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::OBJECT_TYPE_LIST;
	curTemplate->m_parameters[1] = Parameter::OBJECT_TYPE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " : remove ";

 	curTemplate = &m_actionTemplates[ScriptAction::MAP_REVEAL_PERMANENTLY_AT_WAYPOINT];
	curTemplate->m_internalName = "MAP_REVEAL_PERMANENTLY_AT_WAYPOINT";
	curTemplate->m_uiName = "Map_/ Reveal map at waypoint -- permanently.";
	curTemplate->m_numParameters = 4;
	curTemplate->m_parameters[0] = Parameter::WAYPOINT;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::SIDE;
	curTemplate->m_parameters[3] = Parameter::REVEALNAME;
	curTemplate->m_numUiStrings = 5;
	curTemplate->m_uiStrings[0] = "The map is permanently revealed at ";
	curTemplate->m_uiStrings[1] = " with a radius of ";
	curTemplate->m_uiStrings[2] = " for ";
	curTemplate->m_uiStrings[3] = ". (Afterwards referred to as ";
	curTemplate->m_uiStrings[4] = ").";

 	curTemplate = &m_actionTemplates[ScriptAction::MAP_UNDO_REVEAL_PERMANENTLY_AT_WAYPOINT];
	curTemplate->m_internalName = "MAP_UNDO_REVEAL_PERMANENTLY_AT_WAYPOINT";
	curTemplate->m_uiName = "Map_/ Reveal map at waypoint -- undo permanently.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::REVEALNAME;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is undone.";

 	curTemplate = &m_actionTemplates[ScriptAction::EVA_SET_ENABLED_DISABLED];
	curTemplate->m_internalName = "EVA_SET_ENABLED_DISABLED";
	curTemplate->m_uiName = "Scripting_/ Enable or Disable EVA.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Set EVA to be enabled ";
	curTemplate->m_uiStrings[1] = " (False to disable.)";

 	curTemplate = &m_actionTemplates[ScriptAction::OPTIONS_SET_OCCLUSION_MODE];
	curTemplate->m_internalName = "OPTIONS_SET_OCCLUSION_MODE";
	curTemplate->m_uiName = "Scripting_/ Enable or Disable Occlusion (Drawing Behind Buildings).";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Set Occlusion to be enabled ";
	curTemplate->m_uiStrings[1] = " (False to disable.)";

	curTemplate = &m_actionTemplates[ScriptAction::OPTIONS_SET_DRAWICON_UI_MODE];
	curTemplate->m_internalName = "OPTIONS_SET_DRAWICON_UI_MODE";
	curTemplate->m_uiName = "Scripting_/ Enable or Disable Draw-icon UI.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Set Draw-icon UI to be enabled ";
	curTemplate->m_uiStrings[1] = " (False to disable.)";

	curTemplate = &m_actionTemplates[ScriptAction::OPTIONS_SET_PARTICLE_CAP_MODE];
	curTemplate->m_internalName = "OPTIONS_SET_PARTICLE_CAP_MODE";
	curTemplate->m_uiName = "Scripting_/ Enable or Disable Particle Cap.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Set Particle Cap to be enabled ";
	curTemplate->m_uiStrings[1] = " (False to disable.)";

	curTemplate = &m_actionTemplates[ScriptAction::UNIT_AFFECT_OBJECT_PANEL_FLAGS];
	curTemplate->m_internalName = "UNIT_AFFECT_OBJECT_PANEL_FLAGS";
	curTemplate->m_uiName = "Unit_/ Affect flags set on object panel.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::OBJECT_PANEL_FLAG;
	curTemplate->m_parameters[2] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " changes the value of flag ";
	curTemplate->m_uiStrings[2] = " to ";
	curTemplate->m_uiStrings[3] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_AFFECT_OBJECT_PANEL_FLAGS];
	curTemplate->m_internalName = "TEAM_AFFECT_OBJECT_PANEL_FLAGS";
	curTemplate->m_uiName = "Team_/ Affect flags set on object panel - all.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::OBJECT_PANEL_FLAG;
	curTemplate->m_parameters[2] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " change the value of flag ";
	curTemplate->m_uiStrings[2] = " to ";
	curTemplate->m_uiStrings[3] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_SELECT_SKILLSET];
	curTemplate->m_internalName = "PLAYER_SELECT_SKILLSET";
	curTemplate->m_uiName = "Player_/ Set the skillset for a computer player.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " uses skillset number ";
	curTemplate->m_uiStrings[2] = " (1-5).";

	curTemplate = &m_actionTemplates[ScriptAction::SCRIPTING_OVERRIDE_HULK_LIFETIME ];
	curTemplate->m_internalName = "SCRIPTING_OVERRIDE_HULK_LIFETIME";
	curTemplate->m_uiName = "Scripting_/ Hulk set override lifetime.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Override hulk lifetime to ";
	curTemplate->m_uiStrings[1] = " seconds. Negative value reverts to normal behavior.";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_FACE_NAMED];
	curTemplate->m_internalName = "NAMED_FACE_NAMED";
	curTemplate->m_uiName = "Unit_/ Set unit to face another unit.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " begin facing ";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_FACE_WAYPOINT];
	curTemplate->m_internalName = "NAMED_FACE_WAYPOINT";
	curTemplate->m_uiName = "Unit_/ Set unit to face a waypoint.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::WAYPOINT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " begin facing ";
	
	curTemplate = &m_actionTemplates[ScriptAction::TEAM_FACE_NAMED];
	curTemplate->m_internalName = "TEAM_FACE_NAMED";
	curTemplate->m_uiName = "Team_/ Set team to face another unit.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " begin facing ";

	curTemplate = &m_actionTemplates[ScriptAction::TEAM_FACE_WAYPOINT];
	curTemplate->m_internalName = "TEAM_FACE_WAYPOINT";
	curTemplate->m_uiName = "Team_/ Set team to face a waypoint.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::WAYPOINT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " begin facing ";
	
	curTemplate = &m_actionTemplates[ScriptAction::COMMANDBAR_REMOVE_BUTTON_OBJECTTYPE];
	curTemplate->m_internalName = "COMMANDBAR_REMOVE_BUTTON_OBJECTTYPE";
	curTemplate->m_uiName = "Scripting_/ Remove a command button from an object type.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::COMMAND_BUTTON;
	curTemplate->m_parameters[1] = Parameter::OBJECT_TYPE;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is removed from all objects of type ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_actionTemplates[ScriptAction::COMMANDBAR_ADD_BUTTON_OBJECTTYPE_SLOT];
	curTemplate->m_internalName = "COMMANDBAR_ADD_BUTTON_OBJECTTYPE_SLOT";
	curTemplate->m_uiName = "Scripting_/ Add a command button to an object type.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::COMMAND_BUTTON;
	curTemplate->m_parameters[1] = Parameter::OBJECT_TYPE;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is added to all objects of type ";
	curTemplate->m_uiStrings[2] = " in slot number ";
	curTemplate->m_uiStrings[3] = " (1-12).";

	curTemplate = &m_actionTemplates[ScriptAction::UNIT_SPAWN_NAMED_LOCATION_ORIENTATION];
	curTemplate->m_internalName = "UNIT_SPAWN_NAMED_LOCATION_ORIENTATION";
	curTemplate->m_uiName = "Unit_/ Spawn -- named unit on a team at a position with an orientation.";
	curTemplate->m_numParameters = 5;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::OBJECT_TYPE;
	curTemplate->m_parameters[2] = Parameter::TEAM;
	curTemplate->m_parameters[3] = Parameter::COORD3D;
	curTemplate->m_parameters[4] = Parameter::ANGLE;
	curTemplate->m_numUiStrings = 6;
	curTemplate->m_uiStrings[0] = "Spawn ";
	curTemplate->m_uiStrings[1] = " of type ";
	curTemplate->m_uiStrings[2] = " on team ";
	curTemplate->m_uiStrings[3] = " at position (";
	curTemplate->m_uiStrings[4] = "), rotated ";
	curTemplate->m_uiStrings[5] = " .";

	curTemplate = &m_actionTemplates[ScriptAction::PLAYER_AFFECT_RECEIVING_EXPERIENCE];
	curTemplate->m_internalName = "PLAYER_AFFECT_RECEIVING_EXPERIENCE";
	curTemplate->m_uiName = "Player_/ Change the modifier to generals experience that a player receives.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " gains experience at ";
	curTemplate->m_uiStrings[2] = " times the usual rate (0.0 for no gain, 1.0 for normal rate)";

	curTemplate = &m_actionTemplates[ScriptAction::SOUND_SET_VOLUME];
	curTemplate->m_internalName = "SOUND_SET_VOLUME";
	curTemplate->m_uiName = "Multimedia_/ Set the current sound volume.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Set the desired sound volume to ";
	curTemplate->m_uiStrings[1] = "%. (0-100)";

	curTemplate = &m_actionTemplates[ScriptAction::SPEECH_SET_VOLUME];
	curTemplate->m_internalName = "SPEECH_SET_VOLUME";
	curTemplate->m_uiName = "Multimedia_/ Set the current speech volume.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Set the desired speech volume to ";
	curTemplate->m_uiStrings[1] = "%. (0-100)";

	curTemplate = &m_actionTemplates[ScriptAction::OBJECT_ALLOW_BONUSES];
	curTemplate->m_internalName = "OBJECT_ALLOW_BONUSES";
	curTemplate->m_uiName = "Map_/ Adjust Object Bonuses based on difficulty.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Enable Object Bonuses based on difficulty ";
	curTemplate->m_uiStrings[1] = " (true to enable, false to disable).";
	
	curTemplate = &m_actionTemplates[ScriptAction::TEAM_GUARD_IN_TUNNEL_NETWORK];
	curTemplate->m_internalName = "TEAM_GUARD_IN_TUNNEL_NETWORK";
	curTemplate->m_uiName = "Team_/ Set to guard - from inside tunnel network.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " Enter and guard from tunnel network.";

	curTemplate = &m_actionTemplates[ScriptAction::LOCALDEFEAT];
	curTemplate->m_internalName = "LOCALDEFEAT";
	curTemplate->m_uiName = "Multiplayer_/ Announce local defeat.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Show 'Game Over' window";

	curTemplate = &m_actionTemplates[ScriptAction::VICTORY];
	curTemplate->m_internalName = "VICTORY";
	curTemplate->m_uiName = "Multiplayer_/ Announce victory.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Show 'Victorious' window and end game";

	curTemplate = &m_actionTemplates[ScriptAction::DEFEAT];
	curTemplate->m_internalName = "DEFEAT";
	curTemplate->m_uiName = "Multiplayer_/ Announce defeat.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Show 'Defeated' window and end game";

	curTemplate = &m_actionTemplates[ScriptAction::RESIZE_VIEW_GUARDBAND];
	curTemplate->m_internalName = "RESIZE_VIEW_GUARDBAND";
	curTemplate->m_uiName = "Map_/ Resize view guardband.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::REAL;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Allow bigger objects to be perceived as onscreen near the edge (" ;
	curTemplate->m_uiStrings[1] = ",";
	curTemplate->m_uiStrings[2] = ") Width then height, in world units.";

	curTemplate = &m_actionTemplates[ScriptAction::DELETE_ALL_UNMANNED];
	curTemplate->m_internalName = "DELETE_ALL_UNMANNED";
	curTemplate->m_uiName = "Scripting_/ Delete all unmanned (sniped) vehicles.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Delete all unmanned (sniped) vehicles." ;

	curTemplate = &m_actionTemplates[ScriptAction::CHOOSE_VICTIM_ALWAYS_USES_NORMAL];
	curTemplate->m_internalName = "CHOOSE_VICTIM_ALWAYS_USES_NORMAL";
	curTemplate->m_uiName = "Map_/ Force ChooseVictim to ignore game difficulty and always use Normal setting.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Force ChooseVictim to ignore game difficulty and always use Normal setting ";
	curTemplate->m_uiStrings[1] = " (true to enable, false to disable).";

	curTemplate = &m_actionTemplates[ScriptAction::SET_TRAIN_HELD];
	curTemplate->m_internalName = "SET_TRAIN_HELD";
	curTemplate->m_uiName = "Unit/ Set a train to stay at a station. TRUE = stay. FALSE = go-ahead.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " sets its held status to ";

	curTemplate = &m_actionTemplates[ScriptAction::NAMED_SET_EVAC_LEFT_OR_RIGHT];
	curTemplate->m_internalName = "NAMED_SET_EVAC_LEFT_OR_RIGHT";
	curTemplate->m_uiName = "Unit/ Set which side of a container (likely a train) you want the riders to exit on.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
  curTemplate->m_parameters[1] = Parameter::LEFT_OR_RIGHT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " will exit its riders on its ";
 
  curTemplate = &m_actionTemplates[ScriptAction::ENABLE_OBJECT_SOUND];
  curTemplate->m_internalName = "ENABLE_OBJECT_SOUND";
  curTemplate->m_uiName = "Multimedia_/Sound Effect/Enable object's ambient sound";
  curTemplate->m_numParameters = 1;
  curTemplate->m_parameters[0] = Parameter::UNIT;
  curTemplate->m_numUiStrings = 2;
  curTemplate->m_uiStrings[0] = "Enable (or trigger) ";
  curTemplate->m_uiStrings[1] = "'s ambient sound.";
  
  curTemplate = &m_actionTemplates[ScriptAction::DISABLE_OBJECT_SOUND];
  curTemplate->m_internalName = "DISABLE_OBJECT_SOUND";
  curTemplate->m_uiName = "Multimedia_/Sound Effect/Disable object's ambient sound";
  curTemplate->m_numParameters = 1;
  curTemplate->m_parameters[0] = Parameter::UNIT;
  curTemplate->m_numUiStrings = 2;
  curTemplate->m_uiStrings[0] = "Disable ";
  curTemplate->m_uiStrings[1] = "'s ambient sound.";
  

	///////////////////////////////////////////////////////////////////////////////////////////////////

	/* Recipe for adding a condition:
			1. In Scripts.h, add an enum element to enum ConditionType just before NUM_ITEMS.
			2. Go to the end of this section of templates, and create a template.
			3. Go to ScriptConditions.h and add a protected method.
			4. Go to ScriptConditions.cpp, and add your enum to the 
					switch in ScriptConditions::evaluateCondition to call your method in 3 above.
	*/

	// Set up condition templates.
	curTemplate = &m_conditionTemplates[Condition::CONDITION_FALSE];
	curTemplate->m_internalName = "CONDITION_FALSE";
	curTemplate->m_uiName = "Scripting_/ False.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "False.";

	curTemplate = &m_conditionTemplates[Condition::COUNTER];
	curTemplate->m_internalName = "COUNTER";
	curTemplate->m_uiName = "Scripting_/ Counter compared to a value.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::COUNTER;
	curTemplate->m_parameters[1] = Parameter::COMPARISON;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Counter ";
	curTemplate->m_uiStrings[1] = " IS ";
	curTemplate->m_uiStrings[2] = " ";

	curTemplate = &m_conditionTemplates[Condition::UNIT_HEALTH];
	curTemplate->m_internalName = "UNIT_HEALTH";
	curTemplate->m_uiName = "Unit_/ Unit health % compared to a value.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::COMPARISON;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " Health IS ";
	curTemplate->m_uiStrings[2] = " ";
	curTemplate->m_uiStrings[3] = " percent.";

	curTemplate = &m_conditionTemplates[Condition::FLAG];
	curTemplate->m_internalName = "FLAG";
	curTemplate->m_uiName = "Scripting_/ Flag compared to a value.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::FLAG;
	curTemplate->m_parameters[1] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " IS ";

	curTemplate = &m_conditionTemplates[Condition::TEAM_STATE_IS];
	curTemplate->m_internalName = "TEAM_STATE_IS";
	curTemplate->m_uiName = "Team_/ Team state is.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::TEAM_STATE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " state IS ";

	curTemplate = &m_conditionTemplates[Condition::TEAM_STATE_IS_NOT];
	curTemplate->m_internalName = "TEAM_STATE_IS_NOT";
	curTemplate->m_uiName = "Team_/ Team state is not.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::TEAM_STATE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " state IS NOT ";
																										 
	curTemplate = &m_conditionTemplates[Condition::CONDITION_TRUE];
	curTemplate->m_internalName = "CONDITION_TRUE";
	curTemplate->m_uiName = "Scripting_/ True.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "True.";

	curTemplate = &m_conditionTemplates[Condition::TIMER_EXPIRED];
	curTemplate->m_internalName = "TIMER_EXPIRED";
	curTemplate->m_uiName = "Scripting_/ Timer expired.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::COUNTER;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Timer ";
	curTemplate->m_uiStrings[1] = " has expired.";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_ALL_DESTROYED];
	curTemplate->m_internalName = "PLAYER_ALL_DESTROYED";
	curTemplate->m_uiName = "Player_/ All destroyed.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "Everything belonging to  ";
	curTemplate->m_uiStrings[1] = " has been destroyed.";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_ALL_BUILDFACILITIES_DESTROYED];
	curTemplate->m_internalName = "PLAYER_ALL_BUILDFACILITIES_DESTROYED";
	curTemplate->m_uiName = "Player_/ All factories destroyed.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = "All factories belonging to  ";
	curTemplate->m_uiStrings[1] = " have been destroyed.";


	curTemplate = &m_conditionTemplates[Condition::TEAM_INSIDE_AREA_PARTIALLY];
	curTemplate->m_internalName = "TEAM_INSIDE_AREA_PARTIALLY";
	curTemplate->m_uiName = "Team_/ Team has units in an area.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::TRIGGER_AREA;
	curTemplate->m_parameters[2] = Parameter::SURFACES_ALLOWED;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
 	curTemplate->m_uiStrings[1] = " has one or more units in ";
	curTemplate->m_uiStrings[2] = " (";
	curTemplate->m_uiStrings[3] = ").";

	curTemplate = &m_conditionTemplates[Condition::NAMED_INSIDE_AREA];
	curTemplate->m_internalName = "NAMED_INSIDE_AREA";
	curTemplate->m_uiName = "Unit_/ Unit entered area.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::TRIGGER_AREA;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
 	curTemplate->m_uiStrings[1] = " is in ";
	curTemplate->m_uiStrings[2] = " (";
	curTemplate->m_uiStrings[3] = ").";


	curTemplate = &m_conditionTemplates[Condition::TEAM_DESTROYED];
	curTemplate->m_internalName = "TEAM_DESTROYED";
	curTemplate->m_uiName = "Team_/ Team is destroyed.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has been destroyed.";

	curTemplate = &m_conditionTemplates[Condition::NAMED_DESTROYED];
	curTemplate->m_internalName = "NAMED_DESTROYED";
	curTemplate->m_uiName = "Unit_/ Unit is destroyed.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has been destroyed.";

	curTemplate = &m_conditionTemplates[Condition::NAMED_DYING];
	curTemplate->m_internalName = "NAMED_DYING";
	curTemplate->m_uiName = "Unit_/ Unit is dying.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has been killed, but still on screen.";

	curTemplate = &m_conditionTemplates[Condition::NAMED_TOTALLY_DEAD];
	curTemplate->m_internalName = "NAMED_TOTALLY_DEAD";
	curTemplate->m_uiName = "Unit_/ Unit is finished dying.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has been killed, and is finished dying.";

	curTemplate = &m_conditionTemplates[Condition::BRIDGE_BROKEN];
	curTemplate->m_internalName = "BRIDGE_BROKEN";
	curTemplate->m_uiName = "Unit_/ Bridge is broken.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::BRIDGE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has been broken.";

	curTemplate = &m_conditionTemplates[Condition::BRIDGE_REPAIRED];
	curTemplate->m_internalName = "BRIDGE_REPAIRED";
	curTemplate->m_uiName = "Unit_/ Bridge is repaired.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::BRIDGE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has been repaired.";

	curTemplate = &m_conditionTemplates[Condition::NAMED_NOT_DESTROYED];
	curTemplate->m_internalName = "NAMED_NOT_DESTROYED";
	curTemplate->m_uiName = "Unit_/ Unit exists and is alive.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " exists and is alive.";

	curTemplate = &m_conditionTemplates[Condition::TEAM_HAS_UNITS];
	curTemplate->m_internalName = "TEAM_HAS_UNITS";
	curTemplate->m_uiName = "Team_/ Team has units.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has one or more units.";

	curTemplate = &m_conditionTemplates[Condition::CAMERA_MOVEMENT_FINISHED];
	curTemplate->m_internalName = "CAMERA_MOVEMENT_FINISHED";
	curTemplate->m_uiName = "Camera_/ Camera movement finished.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "The camera movement has finished.";

	curTemplate = &m_conditionTemplates[Condition::NAMED_INSIDE_AREA];
	curTemplate->m_internalName = "NAMED_INSIDE_AREA";
	curTemplate->m_uiName = "Unit_/ Unit inside an area.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::TRIGGER_AREA;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is inside ";

	curTemplate = &m_conditionTemplates[Condition::NAMED_OUTSIDE_AREA];
	curTemplate->m_internalName = "NAMED_OUTSIDE_AREA";
	curTemplate->m_uiName = "Unit_/ Unit outside an area.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::TRIGGER_AREA;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is outside ";

	curTemplate = &m_conditionTemplates[Condition::TEAM_INSIDE_AREA_ENTIRELY];
	curTemplate->m_internalName = "TEAM_INSIDE_AREA_ENTIRELY";
	curTemplate->m_uiName = "Team_/ Team completely inside an area.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::TRIGGER_AREA;
	curTemplate->m_parameters[2] = Parameter::SURFACES_ALLOWED;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is all inside ";
	curTemplate->m_uiStrings[2] = " (";
	curTemplate->m_uiStrings[3] = ").";

	curTemplate = &m_conditionTemplates[Condition::TEAM_OUTSIDE_AREA_ENTIRELY];
	curTemplate->m_internalName = "TEAM_OUTSIDE_AREA_ENTIRELY";
	curTemplate->m_uiName = "Team_/ Team is completely outside an area.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::TRIGGER_AREA;
	curTemplate->m_parameters[2] = Parameter::SURFACES_ALLOWED;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is completely outside ";
	curTemplate->m_uiStrings[2] = " (";
	curTemplate->m_uiStrings[3] = ").";

	curTemplate = &m_conditionTemplates[Condition::NAMED_ATTACKED_BY_OBJECTTYPE];
	curTemplate->m_internalName = "NAMED_ATTACKED_BY_OBJECTTYPE";
	curTemplate->m_uiName = "Unit_/ Unit is attacked by a specific unit type.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::OBJECT_TYPE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has been attacked by a(n) ";

	curTemplate = &m_conditionTemplates[Condition::TEAM_ATTACKED_BY_OBJECTTYPE];
	curTemplate->m_internalName = "TEAM_ATTACKED_BY_OBJECTTYPE";
	curTemplate->m_uiName = "Team_/ Team is attacked by a specific unit type.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::OBJECT_TYPE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has been attacked by a(n) ";

	curTemplate = &m_conditionTemplates[Condition::NAMED_ATTACKED_BY_PLAYER];
	curTemplate->m_internalName = "NAMED_ATTACKED_BY_PLAYER";
	curTemplate->m_uiName = "Unit_/ Unit has been attacked by a player.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has been attacked by ";

	curTemplate = &m_conditionTemplates[Condition::TEAM_ATTACKED_BY_PLAYER];
	curTemplate->m_internalName = "TEAM_ATTACKED_BY_PLAYER";
	curTemplate->m_uiName = "Team_/ Team has been attacked by a player.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has been attacked by ";

	curTemplate = &m_conditionTemplates[Condition::BUILT_BY_PLAYER];
	curTemplate->m_internalName = "BUILT_BY_PLAYER";
	curTemplate->m_uiName = "Player_/ Player has built an object type.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::OBJECT_TYPE;
	curTemplate->m_parameters[1] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has been built by ";

	curTemplate = &m_conditionTemplates[Condition::NAMED_CREATED];
	curTemplate->m_internalName = "NAMED_CREATED";
	curTemplate->m_uiName = "Unit_/ Unit has been created.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has been created.";

	curTemplate = &m_conditionTemplates[Condition::TEAM_CREATED];
	curTemplate->m_internalName = "TEAM_CREATED";
	curTemplate->m_uiName = "Team_/ Team has been created.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has been created.";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_HAS_CREDITS];
	curTemplate->m_internalName = "PLAYER_HAS_CREDITS";
	curTemplate->m_uiName = "Player_/ Player has (comparison) to a number of credits.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::INT;
	curTemplate->m_parameters[1] = Parameter::COMPARISON;
	curTemplate->m_parameters[2] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is ";
	curTemplate->m_uiStrings[2] = " the number of credits possessed by ";

	curTemplate = &m_conditionTemplates[Condition::NAMED_DISCOVERED];
	curTemplate->m_internalName = "NAMED_DISCOVERED";
	curTemplate->m_uiName = "Player_/ Player has discovered a specific unit.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has been discovered by ";

	curTemplate = &m_conditionTemplates[Condition::NAMED_BUILDING_IS_EMPTY];
	curTemplate->m_internalName = "NAMED_BUILDING_IS_EMPTY";
	curTemplate->m_uiName = "Unit_/ A specific building is empty.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
 	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is empty.";

	curTemplate = &m_conditionTemplates[Condition::BUILDING_ENTERED_BY_PLAYER];
	curTemplate->m_internalName = "BUILDING_ENTERED_BY_PLAYER";
	curTemplate->m_uiName = "Player_/ Player has entered a specific building.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::UNIT;
 	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has entered building named ";

	curTemplate = &m_conditionTemplates[Condition::ENEMY_SIGHTED];
	curTemplate->m_internalName = "ENEMY_SIGHTED";
	curTemplate->m_uiName = "Unit_/ Unit has sighted a(n) friendly/neutral/enemy unit belonging to a side.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::RELATION;
	curTemplate->m_parameters[2] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " sees a(n) ";
	curTemplate->m_uiStrings[2] = " unit belonging to ";
	curTemplate->m_uiStrings[3] = ".";

	curTemplate = &m_conditionTemplates[Condition::TYPE_SIGHTED];
	curTemplate->m_internalName = "TYPE_SIGHTED";
	curTemplate->m_uiName = "Unit_/ Unit has sighted a type of unit belonging to a side.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::OBJECT_TYPE;
	curTemplate->m_parameters[2] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " sees a(n) ";
	curTemplate->m_uiStrings[2] = " belonging to ";
	curTemplate->m_uiStrings[3] = ".";

	curTemplate = &m_conditionTemplates[Condition::TEAM_DISCOVERED];
	curTemplate->m_internalName = "TEAM_DISCOVERED";
	curTemplate->m_uiName = "Player_/ Player has discovered a team.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has been discovered by ";

	curTemplate = &m_conditionTemplates[Condition::MISSION_ATTEMPTS];
	curTemplate->m_internalName = "MISSION_ATTEMPTS";
	curTemplate->m_uiName = "Player_/ Player has attempted the mission a number of times.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::COMPARISON;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has attempted the mission ";
	curTemplate->m_uiStrings[2] = " ";
	curTemplate->m_uiStrings[3] = " times.";

	curTemplate = &m_conditionTemplates[Condition::NAMED_OWNED_BY_PLAYER];
	curTemplate->m_internalName = "NAMED_OWNED_BY_PLAYER";
	curTemplate->m_uiName = "Player_/ Player owns the specific Unit.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is owned by ";

	curTemplate = &m_conditionTemplates[Condition::TEAM_OWNED_BY_PLAYER];
	curTemplate->m_internalName = "TEAM_OWNED_BY_PLAYER";
	curTemplate->m_uiName = "Player_/ Player owns a specific team.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is owned by ";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_HAS_N_OR_FEWER_BUILDINGS];
	curTemplate->m_internalName = "PLAYER_HAS_N_OR_FEWER_BUILDINGS";
	curTemplate->m_uiName = "Player_/ Player currently owns N or fewer buildings.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " currently owns ";
	curTemplate->m_uiStrings[2] = " or fewer buildings.";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_HAS_N_OR_FEWER_FACTION_BUILDINGS];
	curTemplate->m_internalName = "PLAYER_HAS_N_OR_FEWER_FACTION_BUILDINGS";
	curTemplate->m_uiName = "Player_/ Player currently owns N or fewer faction buildings.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " currently owns ";
	curTemplate->m_uiStrings[2] = " or fewer faction buildings.";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_HAS_POWER];
	curTemplate->m_internalName = "PLAYER_HAS_POWER";
	curTemplate->m_uiName = "Player_/ Player's base currently has power.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " buildings are powered.";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_HAS_NO_POWER];
	curTemplate->m_internalName = "PLAYER_HAS_NO_POWER";
	curTemplate->m_uiName = "Player_/ Player's base currently has no power.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " buildings are not powered.";

	curTemplate = &m_conditionTemplates[Condition::NAMED_REACHED_WAYPOINTS_END];
	curTemplate->m_internalName = "NAMED_REACHED_WAYPOINTS_END";
	curTemplate->m_uiName = "Unit_/ Unit has reached the end of a specific waypoint path.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::WAYPOINT_PATH;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has reached the end of ";

	curTemplate = &m_conditionTemplates[Condition::TEAM_REACHED_WAYPOINTS_END];
	curTemplate->m_internalName = "TEAM_REACHED_WAYPOINTS_END";
	curTemplate->m_uiName = "Team_/ Team has reached the end of a specific waypoint path.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::WAYPOINT_PATH;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has reached the end of ";

	curTemplate = &m_conditionTemplates[Condition::NAMED_SELECTED];
	curTemplate->m_internalName = "NAMED_SELECTED";
	curTemplate->m_uiName = "Unit_/ Unit currently selected.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is currently selected.";

	curTemplate = &m_conditionTemplates[Condition::NAMED_ENTERED_AREA];
	curTemplate->m_internalName = "NAMED_ENTERED_AREA";
	curTemplate->m_uiName = "Unit_/ Unit enters an area.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::TRIGGER_AREA;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " enters ";

	curTemplate = &m_conditionTemplates[Condition::NAMED_EXITED_AREA];
	curTemplate->m_internalName = "NAMED_EXITED_AREA";
	curTemplate->m_uiName = "Unit_/ Unit exits an area.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::TRIGGER_AREA;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " exits ";

	curTemplate = &m_conditionTemplates[Condition::TEAM_ENTERED_AREA_ENTIRELY];
	curTemplate->m_internalName = "TEAM_ENTERED_AREA_ENTIRELY";
	curTemplate->m_uiName = "Team_/ Team entirely enters an area.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::TRIGGER_AREA;
	curTemplate->m_parameters[2] = Parameter::SURFACES_ALLOWED;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " all enter ";
	curTemplate->m_uiStrings[2] = " (";
	curTemplate->m_uiStrings[3] = ").";



	curTemplate = &m_conditionTemplates[Condition::TEAM_ENTERED_AREA_PARTIALLY];
	curTemplate->m_internalName = "TEAM_ENTERED_AREA_PARTIALLY";
	curTemplate->m_uiName = "Team_/ One unit enters an area.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::TRIGGER_AREA;
	curTemplate->m_parameters[2] = Parameter::SURFACES_ALLOWED;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "One unit from ";
	curTemplate->m_uiStrings[1] = " enters ";
	curTemplate->m_uiStrings[2] = " (";
	curTemplate->m_uiStrings[3] = ").";

	curTemplate = &m_conditionTemplates[Condition::TEAM_EXITED_AREA_ENTIRELY];
	curTemplate->m_internalName = "TEAM_EXITED_AREA_ENTIRELY";
	curTemplate->m_uiName = "Team_/ Team entirely exits an area.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::TRIGGER_AREA;
	curTemplate->m_parameters[2] = Parameter::SURFACES_ALLOWED;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " all exit ";
	curTemplate->m_uiStrings[2] = " (";
	curTemplate->m_uiStrings[3] = ").";

	curTemplate = &m_conditionTemplates[Condition::TEAM_EXITED_AREA_PARTIALLY];
	curTemplate->m_internalName = "TEAM_EXITED_AREA_PARTIALLY";
	curTemplate->m_uiName = "Team_/ One unit exits an area.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::TRIGGER_AREA;
	curTemplate->m_parameters[2] = Parameter::SURFACES_ALLOWED;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "One unit from ";
	curTemplate->m_uiStrings[1] = " exits ";
	curTemplate->m_uiStrings[2] = " (";
	curTemplate->m_uiStrings[3] = ").";

	curTemplate = &m_conditionTemplates[Condition::MULTIPLAYER_ALLIED_VICTORY];
	curTemplate->m_internalName = "MULTIPLAYER_ALLIED_VICTORY";
	curTemplate->m_uiName = "Multiplayer_/ Multiplayer allied victory.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "The multiplayer game has ended in victory for the local player and his allies.";

	curTemplate = &m_conditionTemplates[Condition::MULTIPLAYER_ALLIED_DEFEAT];
	curTemplate->m_internalName = "MULTIPLAYER_ALLIED_DEFEAT";
	curTemplate->m_uiName = "Multiplayer_/ Multiplayer allied defeat.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "The multiplayer game has ended in defeat for the local player and his allies.";

	curTemplate = &m_conditionTemplates[Condition::MULTIPLAYER_PLAYER_DEFEAT];
	curTemplate->m_internalName = "MULTIPLAYER_PLAYER_DEFEAT";
	curTemplate->m_uiName = "Multiplayer_/ Multiplayer local player defeat check.";
	curTemplate->m_numParameters = 0;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Everything belonging to the local player has been destroyed, but his allies may or may not have been defeated.";

	curTemplate = &m_conditionTemplates[Condition::HAS_FINISHED_VIDEO];
	curTemplate->m_internalName = "HAS_FINISHED_VIDEO";
	curTemplate->m_uiName = "Multimedia_/ Video has completed playing.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::MOVIE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has completed playing.";

	curTemplate = &m_conditionTemplates[Condition::HAS_FINISHED_SPEECH];
	curTemplate->m_internalName = "HAS_FINISHED_SPEECH";
	curTemplate->m_uiName = "Multimedia_/ Speech has completed playing.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::DIALOG;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has completed playing.";

	curTemplate = &m_conditionTemplates[Condition::HAS_FINISHED_AUDIO];
	curTemplate->m_internalName = "HAS_FINISHED_AUDIO";
	curTemplate->m_uiName = "Multimedia_/ Sound has completed playing.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SOUND;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has completed playing.";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_TRIGGERED_SPECIAL_POWER];
	curTemplate->m_internalName = "PLAYER_TRIGGERED_SPECIAL_POWER";
	curTemplate->m_uiName = "Player_/ Player starts using a special power.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::SPECIAL_POWER;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Player ";
	curTemplate->m_uiStrings[1] = " starts using ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_TRIGGERED_SPECIAL_POWER_FROM_NAMED];
	curTemplate->m_internalName = "PLAYER_TRIGGERED_SPECIAL_POWER_FROM_NAMED";
	curTemplate->m_uiName = "Player_/ Player start using a special power from a named unit.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::SPECIAL_POWER;
	curTemplate->m_parameters[2] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "Player ";
	curTemplate->m_uiStrings[1] = " starts using ";
	curTemplate->m_uiStrings[2] = " from ";
	curTemplate->m_uiStrings[3] = ".";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_MIDWAY_SPECIAL_POWER];
	curTemplate->m_internalName = "PLAYER_MIDWAY_SPECIAL_POWER";
	curTemplate->m_uiName = "Player_/ Player is midway through using a special power.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::SPECIAL_POWER;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Player ";
	curTemplate->m_uiStrings[1] = " is midway using ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_MIDWAY_SPECIAL_POWER_FROM_NAMED];
	curTemplate->m_internalName = "PLAYER_MIDWAY_SPECIAL_POWER_FROM_NAMED";
	curTemplate->m_uiName = "Player_/ Player is midway through using a special power from a named unit.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::SPECIAL_POWER;
	curTemplate->m_parameters[2] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "Player ";
	curTemplate->m_uiStrings[1] = " is midway using ";
	curTemplate->m_uiStrings[2] = " from ";
	curTemplate->m_uiStrings[3] = ".";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_COMPLETED_SPECIAL_POWER];
	curTemplate->m_internalName = "PLAYER_COMPLETED_SPECIAL_POWER";
	curTemplate->m_uiName = "Player_/ Player completed using a special power.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::SPECIAL_POWER;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Player ";
	curTemplate->m_uiStrings[1] = " completed using ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_COMPLETED_SPECIAL_POWER_FROM_NAMED];
	curTemplate->m_internalName = "PLAYER_COMPLETED_SPECIAL_POWER_FROM_NAMED";
	curTemplate->m_uiName = "Player_/ Player completed using a special power from a named unit.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::SPECIAL_POWER;
	curTemplate->m_parameters[2] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "Player ";
	curTemplate->m_uiStrings[1] = " completed using ";
	curTemplate->m_uiStrings[2] = " from ";
	curTemplate->m_uiStrings[3] = ".";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_ACQUIRED_SCIENCE];
	curTemplate->m_internalName = "PLAYER_ACQUIRED_SCIENCE";
	curTemplate->m_uiName = "Player_/ Player acquired a Science.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::SCIENCE;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Player ";
	curTemplate->m_uiStrings[1] = " acquired ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_CAN_PURCHASE_SCIENCE];
	curTemplate->m_internalName = "PLAYER_CAN_PURCHASE_SCIENCE";
	curTemplate->m_uiName = "Player_/ Player can purchase a particular Science (has all prereqs & points).";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::SCIENCE;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Player ";
	curTemplate->m_uiStrings[1] = " can purchase ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_HAS_SCIENCEPURCHASEPOINTS];
	curTemplate->m_internalName = "PLAYER_HAS_SCIENCEPURCHASEPOINTS";
	curTemplate->m_uiName = "Player_/ Player has a certain number of Science Purchase Points available.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Player ";
	curTemplate->m_uiStrings[1] = " has at least ";
	curTemplate->m_uiStrings[2] = " Science Purchase Points available.";
	
	curTemplate = &m_conditionTemplates[Condition::NAMED_HAS_FREE_CONTAINER_SLOTS];
	curTemplate->m_internalName = "NAMED_HAS_FREE_CONTAINER_SLOTS";
	curTemplate->m_uiName = "Unit_/ Unit has free container slots.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has free container slots.";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_BUILT_UPGRADE];
	curTemplate->m_internalName = "PLAYER_BUILT_UPGRADE";
	curTemplate->m_uiName = "Player_/ Player built an upgrade.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::UPGRADE;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = "Player ";
	curTemplate->m_uiStrings[1] = " built ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_BUILT_UPGRADE_FROM_NAMED];
	curTemplate->m_internalName = "PLAYER_BUILT_UPGRADE_FROM_NAMED";
	curTemplate->m_uiName = "Player_/ Player built an upgrade from a named unit.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::UPGRADE;
	curTemplate->m_parameters[2] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "Player ";
	curTemplate->m_uiStrings[1] = " built ";
	curTemplate->m_uiStrings[2] = " from ";
	curTemplate->m_uiStrings[3] = ".";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_DESTROYED_N_BUILDINGS_PLAYER];
	curTemplate->m_internalName = "PLAYER_DESTROYED_N_BUILDINGS_PLAYER";
	curTemplate->m_uiName = "Player_/ Player destroyed N or more of an opponent's buildings.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_parameters[2] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = "Player ";
	curTemplate->m_uiStrings[1] = " destroyed ";
	curTemplate->m_uiStrings[2] = " or more buildings owned by ";
	curTemplate->m_uiStrings[3] = ".";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_HAS_OBJECT_COMPARISON];
	curTemplate->m_internalName = "PLAYER_HAS_OBJECT_COMPARISON";
	curTemplate->m_uiName = "Player_/ Player has (comparison) unit type.";
	curTemplate->m_numParameters = 4;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::COMPARISON;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_parameters[3] = Parameter::OBJECT_TYPE;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has ";
	curTemplate->m_uiStrings[2] = " ";
	curTemplate->m_uiStrings[3] = " unit or structure of type ";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA];
	curTemplate->m_internalName = "PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA";
	curTemplate->m_uiName = "Player_/ Player has (comparison) unit type in an area.";
	curTemplate->m_numParameters = 5;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::COMPARISON;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_parameters[3] = Parameter::OBJECT_TYPE;
	curTemplate->m_parameters[4] = Parameter::TRIGGER_AREA;
	curTemplate->m_numUiStrings = 5;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has ";
	curTemplate->m_uiStrings[2] = " ";
	curTemplate->m_uiStrings[3] = " unit or structure of type ";
	curTemplate->m_uiStrings[4] = " in the ";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_HAS_COMPARISON_UNIT_KIND_IN_TRIGGER_AREA];
	curTemplate->m_internalName = "PLAYER_HAS_COMPARISON_UNIT_KIND_IN_TRIGGER_AREA";
	curTemplate->m_uiName = "Player_/ Player has (comparison) kind of unit or structure in an area.";
	curTemplate->m_numParameters = 5;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::COMPARISON;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_parameters[3] = Parameter::KIND_OF_PARAM;
	curTemplate->m_parameters[4] = Parameter::TRIGGER_AREA	;
	curTemplate->m_numUiStrings = 5;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has ";
	curTemplate->m_uiStrings[2] = " ";
	curTemplate->m_uiStrings[3] = " unit or structure with ";
	curTemplate->m_uiStrings[4] = " in the ";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_POWER_COMPARE_PERCENT];
	curTemplate->m_internalName = "PLAYER_POWER_COMPARE_PERCENT";
	curTemplate->m_uiName = "Player_/ Player has (comparison) percent power supply to consumption.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::COMPARISON;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has ";
	curTemplate->m_uiStrings[2] = " ";
	curTemplate->m_uiStrings[3] = " percent power supply ratio.";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_EXCESS_POWER_COMPARE_VALUE];
	curTemplate->m_internalName = "PLAYER_EXCESS_POWER_COMPARE_VALUE";
	curTemplate->m_uiName = "Player_/ Player has (comparison) kilowatts excess power supply.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::COMPARISON;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has ";
	curTemplate->m_uiStrings[2] = " ";
	curTemplate->m_uiStrings[3] = " excess kilowatts power supply.";

	curTemplate = &m_conditionTemplates[Condition::UNIT_EMPTIED];
	curTemplate->m_internalName = "UNIT_EMPTIED";
	curTemplate->m_uiName = "Unit_/ Unit has emptied its contents.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " emptied its contents.";

	curTemplate = &m_conditionTemplates[Condition::SKIRMISH_SPECIAL_POWER_READY];
	curTemplate->m_internalName = "SKIRMISH_SPECIAL_POWER_READY";
	curTemplate->m_uiName = "Skirmish_/ Player's special power is ready to fire.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::SPECIAL_POWER;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is ready to fire ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_conditionTemplates[Condition::UNIT_HAS_OBJECT_STATUS];
	curTemplate->m_internalName = "UNIT_HAS_OBJECT_STATUS";
	curTemplate->m_uiName = "Unit_/ Unit has object status.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::UNIT;
	curTemplate->m_parameters[1] = Parameter::OBJECT_STATUS;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has ";

	curTemplate = &m_conditionTemplates[Condition::TEAM_ALL_HAS_OBJECT_STATUS];
	curTemplate->m_internalName = "TEAM_ALL_HAS_OBJECT_STATUS";
	curTemplate->m_uiName = "Team_/ Team has object status - all.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::OBJECT_STATUS;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has ";

	curTemplate = &m_conditionTemplates[Condition::TEAM_SOME_HAVE_OBJECT_STATUS];
	curTemplate->m_internalName = "TEAM_SOME_HAVE_OBJECT_STATUS";
	curTemplate->m_uiName = "Team_/ Team has object status - partial.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::TEAM;
	curTemplate->m_parameters[1] = Parameter::OBJECT_STATUS;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has ";

	curTemplate = &m_conditionTemplates[Condition::SKIRMISH_VALUE_IN_AREA];
	curTemplate->m_internalName = "SKIRMISH_VALUE_IN_AREA";
	curTemplate->m_uiName = "Skirmish Only_/ Player has total value in area.";
	curTemplate->m_numParameters = 4;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::COMPARISON;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_parameters[3] = Parameter::TRIGGER_AREA;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has ";
	curTemplate->m_uiStrings[2] = " ";
	curTemplate->m_uiStrings[3] = " within area ";

	curTemplate = &m_conditionTemplates[Condition::SKIRMISH_PLAYER_FACTION];
	curTemplate->m_internalName = "SKIRMISH_PLAYER_FACTION";
	curTemplate->m_uiName = "Skirmish_/ Player is faction. - untested";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::FACTION_NAME;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " is ";

	curTemplate = &m_conditionTemplates[Condition::SKIRMISH_SUPPLIES_VALUE_WITHIN_DISTANCE];
	curTemplate->m_internalName = "SKIRMISH_SUPPLIES_VALUE_WITHIN_DISTANCE";
	curTemplate->m_uiName = "Skirmish Only_/ Supplies are within specified distance.";
	curTemplate->m_numParameters = 4;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::TRIGGER_AREA;
	curTemplate->m_parameters[3] = Parameter::REAL;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has supplies within ";
	curTemplate->m_uiStrings[2] = " of ";
	curTemplate->m_uiStrings[3] = " worth at least ";


	curTemplate = &m_conditionTemplates[Condition::SKIRMISH_TECH_BUILDING_WITHIN_DISTANCE];
	curTemplate->m_internalName = "SKIRMISH_TECH_BUILDING_WITHIN_DISTANCE";
	curTemplate->m_uiName = "Skirmish Only_/ Tech building is within specified distance.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::REAL;
	curTemplate->m_parameters[2] = Parameter::TRIGGER_AREA;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has a tech building within ";
	curTemplate->m_uiStrings[2] = " of ";

	curTemplate = &m_conditionTemplates[Condition::SKIRMISH_COMMAND_BUTTON_READY_ALL];
	curTemplate->m_internalName = "SKIRMISH_COMMAND_BUTTON_READY_ALL";
	curTemplate->m_uiName = "Skirmish_/ Command Ability is ready - all.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::TEAM;
	curTemplate->m_parameters[2] = Parameter::COMMANDBUTTON_ALL_ABILITIES;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = "'s ";
	curTemplate->m_uiStrings[2] = " are ready to use ";
	curTemplate->m_uiStrings[3] = " (all applicable members).";

	curTemplate = &m_conditionTemplates[Condition::SKIRMISH_COMMAND_BUTTON_READY_PARTIAL];
	curTemplate->m_internalName = "SKIRMISH_COMMAND_BUTTON_READY_PARTIAL";
	curTemplate->m_uiName = "Skirmish_/ Command Ability is ready - partial";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::TEAM;
	curTemplate->m_parameters[2] = Parameter::COMMANDBUTTON_ALL_ABILITIES;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = "'s ";
	curTemplate->m_uiStrings[2] = " are ready to use ";
	curTemplate->m_uiStrings[3] = " (at least one member).";

	curTemplate = &m_conditionTemplates[Condition::SKIRMISH_UNOWNED_FACTION_UNIT_EXISTS];
	curTemplate->m_internalName = "SKIRMISH_UNOWNED_FACTION_UNIT_EXISTS";
	curTemplate->m_uiName = "Skirmish_/ Unowned faction unit -- comparison.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::COMPARISON;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = ". There are ";
	curTemplate->m_uiStrings[2] = " ";
	curTemplate->m_uiStrings[3] = " unowned faction units.";
	
	
	curTemplate = &m_conditionTemplates[Condition::SKIRMISH_PLAYER_HAS_PREREQUISITE_TO_BUILD];
	curTemplate->m_internalName = "SKIRMISH_PLAYER_HAS_PREREQUISITE_TO_BUILD";
	curTemplate->m_uiName = "Skirmish_/ Player has prerequisites to build an object type.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::OBJECT_TYPE;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " can build ";
	curTemplate->m_uiStrings[2] = ".";
	
	curTemplate = &m_conditionTemplates[Condition::SKIRMISH_PLAYER_HAS_COMPARISON_GARRISONED];
	curTemplate->m_internalName = "SKIRMISH_PLAYER_HAS_COMPARISON_GARRISONED";
	curTemplate->m_uiName = "Skirmish_/ Player has garrisoned buildings -- comparison.";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::COMPARISON;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has ";
	curTemplate->m_uiStrings[2] = " ";
	curTemplate->m_uiStrings[3] = " garrisoned buildings.";

	curTemplate = &m_conditionTemplates[Condition::SKIRMISH_PLAYER_HAS_COMPARISON_CAPTURED_UNITS];
	curTemplate->m_internalName = "SKIRMISH_PLAYER_HAS_COMPARISON_CAPTURED_UNITS";
	curTemplate->m_uiName = "Skirmish_/ Player has captured units -- comparison";
	curTemplate->m_numParameters = 3;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::COMPARISON;
	curTemplate->m_parameters[2] = Parameter::INT;
	curTemplate->m_numUiStrings = 4;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has captured ";
	curTemplate->m_uiStrings[2] = " ";
	curTemplate->m_uiStrings[3] = " units.";

	curTemplate = &m_conditionTemplates[Condition::SKIRMISH_NAMED_AREA_EXIST];
	curTemplate->m_internalName = "SKIRMISH_NAMED_AREA_EXIST";
	curTemplate->m_uiName = "Skirmish_/ Area exists.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::TRIGGER_AREA;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = ". ";
	curTemplate->m_uiStrings[2] = " exists.";
	
	curTemplate = &m_conditionTemplates[Condition::SKIRMISH_PLAYER_HAS_UNITS_IN_AREA];
	curTemplate->m_internalName = "SKIRMISH_PLAYER_HAS_UNITS_IN_AREA";
	curTemplate->m_uiName = "Skirmish_/ Player has units in an area";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::TRIGGER_AREA;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has units in ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_conditionTemplates[Condition::SKIRMISH_PLAYER_HAS_BEEN_ATTACKED_BY_PLAYER];
	curTemplate->m_internalName = "SKIRMISH_PLAYER_HAS_BEEN_ATTACKED_BY_PLAYER";
	curTemplate->m_uiName = "Skirmish_/ Player has been attacked by player.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has been attacked by ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_conditionTemplates[Condition::SKIRMISH_PLAYER_IS_OUTSIDE_AREA];
	curTemplate->m_internalName = "SKIRMISH_PLAYER_IS_OUTSIDE_AREA";
	curTemplate->m_uiName = "Skirmish_/ Player doesn't have units in an area.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::TRIGGER_AREA;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has doesn't have units in ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_conditionTemplates[Condition::SKIRMISH_PLAYER_HAS_DISCOVERED_PLAYER];
	curTemplate->m_internalName = "SKIRMISH_PLAYER_HAS_DISCOVERED_PLAYER";
	curTemplate->m_uiName = "Skirmish_/ Player has discovered another player.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has discovered ";
	curTemplate->m_uiStrings[2] = ".";

	curTemplate = &m_conditionTemplates[Condition::MUSIC_TRACK_HAS_COMPLETED];
	curTemplate->m_internalName = "MUSIC_TRACK_HAS_COMPLETED";
	curTemplate->m_uiName = "Multimedia_/ Music track has completed some number of times.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::MUSIC;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has completed at least ";
	curTemplate->m_uiStrings[2] = " times. (NOTE: This can only be used to "
		"start other music. USING THIS SCRIPT IN ANY OTHER WAY WILL CAUSE REPLAYS TO NOT WORK.)";

	curTemplate = &m_conditionTemplates[Condition::SUPPLY_SOURCE_SAFE];
	curTemplate->m_internalName = "SUPPLY_SOURCE_SAFE";
	curTemplate->m_uiName = "Skirmish_/ Supply source is safe.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " closest supply src with at least ";
	curTemplate->m_uiStrings[2] = " available resources is SAFE from enemy influence.";

	curTemplate = &m_conditionTemplates[Condition::SUPPLY_SOURCE_ATTACKED];
	curTemplate->m_internalName = "SUPPLY_SOURCE_ATTACKED";
	curTemplate->m_uiName = "Skirmish_/ Supply source is attacked.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_numUiStrings = 2;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " supply source is under attack.";

	curTemplate = &m_conditionTemplates[Condition::START_POSITION_IS];
	curTemplate->m_internalName = "START_POSITION_IS";
	curTemplate->m_uiName = "Skirmish_/ Start position.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::INT;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " starting position is ";
	curTemplate->m_uiStrings[2] = " .";

	curTemplate = &m_conditionTemplates[Condition::PLAYER_LOST_OBJECT_TYPE];
	curTemplate->m_internalName = "PLAYER_LOST_OBJECT_TYPE";
	curTemplate->m_uiName = "Player_/ Player has lost an object of type.";
	curTemplate->m_numParameters = 2;
	curTemplate->m_parameters[0] = Parameter::SIDE;
	curTemplate->m_parameters[1] = Parameter::OBJECT_TYPE;
	curTemplate->m_numUiStrings = 3;
	curTemplate->m_uiStrings[0] = " ";
	curTemplate->m_uiStrings[1] = " has lost an object of type ";
	curTemplate->m_uiStrings[2] = " (can be an object type list).";

	curTemplate = &m_actionTemplates[ScriptAction::SHOW_WEATHER];
	curTemplate->m_internalName = "SHOW_WEATHER";
	curTemplate->m_uiName = "Map/Environment/Show Weather.";
	curTemplate->m_numParameters = 1;
	curTemplate->m_parameters[0] = Parameter::BOOLEAN;
	curTemplate->m_numUiStrings = 1;
	curTemplate->m_uiStrings[0] = "Show Weather = ";

	Int i;
	for (i=0; i<Condition::NUM_ITEMS; i++) {
		AsciiString str;
		str.format("[%d]", i);
		m_conditionTemplates[i].m_uiName.concat(str);	 
		m_conditionTemplates[i].m_internalNameKey = NAMEKEY(m_conditionTemplates[i].m_internalName);
	}

	for (i=0; i<ScriptAction::NUM_ITEMS; i++) {
		AsciiString str;
		str.format("[%d]", i);
		m_actionTemplates[i].m_uiName.concat(str);
		m_actionTemplates[i].m_internalNameKey = NAMEKEY(m_actionTemplates[i].m_internalName);
	}


	reset();

}  // end init

//-------------------------------------------------------------------------------------------------
/** Gets the ui and parameter template for a script action */
//-------------------------------------------------------------------------------------------------
const ActionTemplate * ScriptEngine::getActionTemplate( Int ndx )
{
	DEBUG_ASSERTCRASH(ndx >= 0 && ndx < ScriptAction::NUM_ITEMS, ("Out of range.\n"));
	if (ndx <0 || ndx >= ScriptAction::NUM_ITEMS) ndx = 0;
	DEBUG_ASSERTCRASH (!m_actionTemplates[ndx].getName().isEmpty(), ("Need to initialize action enum=%d.\n", ndx));
	
	return &m_actionTemplates[ndx];
}  // end getActionTemplate

//-------------------------------------------------------------------------------------------------
/** Gets the ui and parameter template for a script condition */
//-------------------------------------------------------------------------------------------------
const ConditionTemplate * ScriptEngine::getConditionTemplate( Int ndx )
{
	DEBUG_ASSERTCRASH(ndx >= 0 && ndx < ScriptAction::NUM_ITEMS, ("Out of range.\n"));
	if (ndx <0 || ndx >= Condition::NUM_ITEMS) ndx = 0;
	DEBUG_ASSERTCRASH (!m_conditionTemplates[ndx].getName().isEmpty(), ("Need to initialize Condition enum=%d.\n", ndx));
	return &m_conditionTemplates[ndx];
}  // end getConditionTemplate
