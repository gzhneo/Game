/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EngineScripts.cpp -- what the script editor needs from the game: the condition and action
// templates of the script engine, and the names the game data has for parameter types (the lists
// of the old WorldBuilder's parameter dialog, EditParameter).

// The name tables are defined in the headers only on request.
#define DEFINE_BUILDABLE_STATUS_NAMES
#define DEFINE_SCIENCE_AVAILABILITY_NAMES

#include "Engine/EngineApi.h"
#include "Engine/EngineInternal.h"

#include "Common/AudioEventInfo.h"
#include "Common/BorderColors.h"
#include "Common/FileSystem.h"
#include "Common/GameAudio.h"
#include "Common/KindOf.h"
#include "Common/ObjectStatusTypes.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/Radar.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/file.h"
#include "GameClient/Anim2D.h"
#include "GameClient/GameText.h"
#include "GameClient/ShellHooks.h"
#include "GameLogic/AI.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Scripts.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{

std::map<int, std::vector<MeNamedValue>> s_values;

void copyText(char *dest, std::size_t size, const char *text)
{
	std::strncpy(dest, text ? text : "", size - 1);
	dest[size - 1] = '\0';
}

void add(std::vector<MeNamedValue> &list, const char *name, int32_t value = 0)
{
	if (!name || !*name || std::strlen(name) >= sizeof(MeNamedValue::name))
		return;
	MeNamedValue v{};
	copyText(v.name, sizeof(v.name), name);
	v.value = value;
	list.push_back(v);
}

void sortByName(std::vector<MeNamedValue> &list)
{
	std::sort(list.begin(), list.end(), [](const MeNamedValue &a, const MeNamedValue &b) { return _stricmp(a.name, b.name) < 0; });
	list.erase(std::unique(list.begin(), list.end(), [](const MeNamedValue &a, const MeNamedValue &b) { return std::strcmp(a.name, b.name) == 0; }), list.end());
}

// The second word of every line of an INI file that starts with the block keyword
// ("CommandButton Command_Stop", "Video EALogoMovie").
void blockNames(const char *path, const char *keyword, std::vector<MeNamedValue> &list)
{
	if (!TheFileSystem)
		return;
	File *fp = TheFileSystem->openFile(path, File::READ | File::TEXT);
	if (!fp)
		return;
	std::string text;
	char buffer[4096];
	for (Int n = fp->read(buffer, sizeof(buffer)); n > 0; n = fp->read(buffer, sizeof(buffer)))
		text.append(buffer, static_cast<std::size_t>(n));
	fp->close();
	std::size_t pos = 0;
	while (pos < text.size())
	{
		std::size_t end = text.find('\n', pos);
		if (end == std::string::npos)
			end = text.size();
		const std::string line = text.substr(pos, end - pos);
		pos = end + 1;
		const char *seps = " ,\t\r";
		const std::size_t a = line.find_first_not_of(seps);
		if (a == std::string::npos)
			continue;
		const std::size_t b = line.find_first_of(seps, a);
		if (b == std::string::npos || line.compare(a, b - a, keyword) != 0)
			continue;
		const std::size_t c = line.find_first_not_of(seps, b);
		if (c == std::string::npos || line[c] == ';')
			continue;
		const std::size_t d = line.find_first_of(seps, c);
		add(list, line.substr(c, d == std::string::npos ? std::string::npos : d - c).c_str());
	}
}

void audioNames(AudioType type, std::vector<MeNamedValue> &list)
{
	if (!TheAudio)
		return;
	std::vector<AudioEventInfo *> events;
	TheAudio->findAllAudioEventsOfType(type, events);
	for (const AudioEventInfo *e : events)
		if (e)
			add(list, e->m_audioName.str());
	sortByName(list);
}

std::vector<MeNamedValue> build(int type)
{
	std::vector<MeNamedValue> list;
	switch (type)
	{
		case Parameter::SOUND:
			audioNames(AudioType::AT_SoundEffect, list);
			break;
		case Parameter::DIALOG:
			audioNames(AudioType::AT_Streaming, list);
			break;
		case Parameter::MUSIC:
			audioNames(AudioType::AT_Music, list);
			break;
		case Parameter::MOVIE:
			// The editor has no video player; the names come from Video.ini as the player reads them.
			blockNames("Data\\INI\\Default\\Video.ini", "Video", list);
			blockNames("Data\\INI\\Video.ini", "Video", list);
			sortByName(list);
			break;
		case Parameter::SPECIAL_POWER:
			if (TheSpecialPowerStore)
				for (Int i = 0; i < TheSpecialPowerStore->getNumSpecialPowers(); ++i)
					if (const SpecialPowerTemplate *p = TheSpecialPowerStore->getSpecialPowerTemplateByIndex(i))
						add(list, p->getName().str());
			sortByName(list);
			break;
		case Parameter::SCIENCE:
			if (TheScienceStore)
				for (const AsciiString &name : TheScienceStore->friend_getScienceNames())
					add(list, name.str());
			sortByName(list);
			break;
		case Parameter::SCIENCE_AVAILABILITY:
			for (Int i = 0; i < SCIENCE_AVAILABILITY_COUNT; ++i)
				add(list, ScienceAvailabilityNames[i], i);
			break;
		case Parameter::UPGRADE:
			if (TheUpgradeCenter)
				for (const AsciiString &name : TheUpgradeCenter->getUpgradeNames())
					add(list, name.str());
			sortByName(list);
			break;
		case Parameter::COMMAND_BUTTON:
		case Parameter::COMMANDBUTTON_ABILITY:
		case Parameter::COMMANDBUTTON_ALL_ABILITIES:
			blockNames("Data\\INI\\CommandButton.ini", "CommandButton", list);
			sortByName(list);
			break;
		case Parameter::EMOTICON:
			if (TheAnim2DCollection)
				for (Anim2DTemplate *t = TheAnim2DCollection->getTemplateHead(); t; t = TheAnim2DCollection->getNextTemplate(t))
					add(list, t->getName().str());
			sortByName(list);
			break;
		case Parameter::LOCALIZED_TEXT:
			if (TheGameText)
				for (const AsciiString &label : TheGameText->getStringsWithLabelPrefix(AsciiString("SCRIPT")))
					add(list, label.str());
			sortByName(list);
			break;
		case Parameter::KIND_OF_PARAM:
			for (Int i = KINDOF_FIRST; i < KINDOF_COUNT; ++i)
				add(list, KindOfMaskType::getBitNames()[i - KINDOF_FIRST], i);
			break;
		case Parameter::OBJECT_STATUS:
			for (Int i = 0; i < OBJECT_STATUS_COUNT; ++i)
				add(list, ObjectStatusMaskType::getBitNames()[i], i);
			break;
		case Parameter::OBJECT_PANEL_FLAG:
			for (Int i = 0; TheObjectFlagsNames[i]; ++i)
				add(list, TheObjectFlagsNames[i], i);
			break;
		case Parameter::FACTION_NAME:
			if (ThePlayerTemplateStore)
			{
				AsciiStringList sides;
				ThePlayerTemplateStore->getAllSideStrings(&sides);
				for (const AsciiString &side : sides)
					add(list, side.str());
			}
			break;
		case Parameter::BUILDABLE:
			for (Int i = BSTATUS_YES; i < BSTATUS_NUM_TYPES; ++i)
				add(list, BuildableStatusNames[i - BSTATUS_YES], i);
			break;
		case Parameter::SURFACES_ALLOWED:
			for (Int i = 0; i < 3; ++i)
				add(list, Surfaces[i], i + 1);
			break;
		case Parameter::SHAKE_INTENSITY:
			for (Int i = 0; i < 6; ++i)
				add(list, ShakeIntensities[i], i);
			break;
		case Parameter::BOUNDARY:
			for (Int i = 0; i < BORDER_COLORS_SIZE; ++i)
				add(list, BORDER_COLORS[i].m_colorName, i);
			break;
		case Parameter::FLAG:
			// Flags the shell sets; the map's own flags come from its scripts.
			for (Int i = 0; i < SHELL_SCRIPT_HOOK_TOTAL; ++i)
				add(list, TheShellHookNames[i], i);
			break;
		case Parameter::COMPARISON:
			add(list, "Less Than", Parameter::LESS_THAN);
			add(list, "Less Than or Equal", Parameter::LESS_EQUAL);
			add(list, "Equal To", Parameter::EQUAL);
			add(list, "Greater Than or Equal To", Parameter::GREATER_EQUAL);
			add(list, "Greater Than", Parameter::GREATER);
			add(list, "Not Equal To", Parameter::NOT_EQUAL);
			break;
		case Parameter::RELATION:
			add(list, "Enemy", Parameter::REL_ENEMY);
			add(list, "Neutral", Parameter::REL_NEUTRAL);
			add(list, "Friend", Parameter::REL_FRIEND);
			break;
		case Parameter::AI_MOOD:
			add(list, "Sleep", AI_SLEEP);
			add(list, "Passive", AI_PASSIVE);
			add(list, "Normal", AI_NORMAL);
			add(list, "Alert", AI_ALERT);
			add(list, "Aggressive", AI_AGGRESSIVE);
			break;
		case Parameter::RADAR_EVENT_TYPE:
			add(list, "Construction", RADAR_EVENT_CONSTRUCTION);
			add(list, "Upgrade", RADAR_EVENT_UPGRADE);
			add(list, "Under Attack", RADAR_EVENT_UNDER_ATTACK);
			add(list, "Information", RADAR_EVENT_INFORMATION);
			add(list, "Infiltration", RADAR_EVENT_INFILTRATION);
			break;
		case Parameter::LEFT_OR_RIGHT:
			add(list, "left", EVAC_TO_LEFT);
			add(list, "right", EVAC_TO_RIGHT);
			add(list, "normal (burst from center)", EVAC_BURST_FROM_CENTER);
			break;
		case Parameter::BOOLEAN:
			add(list, "FALSE", 0);
			add(list, "TRUE", 1);
			break;
		case Parameter::SKIRMISH_WAYPOINT_PATH:
			add(list, "Center");
			add(list, "Backdoor");
			add(list, "Flank");
			add(list, "Special");
			break;
		default:
			break;
	}
	return list;
}

const std::vector<MeNamedValue> &values(int type)
{
	auto it = s_values.find(type);
	if (it == s_values.end())
		it = s_values.emplace(type, build(type)).first;
	return it->second;
}

const Template *scriptTemplate(int32_t actions, int32_t index)
{
	if (!TheScriptEngine || index < 0)
		return nullptr;
	if (actions)
		return index < ScriptAction::NUM_ITEMS ? TheScriptEngine->getActionTemplate(index) : nullptr;
	return index < Condition::NUM_ITEMS ? TheScriptEngine->getConditionTemplate(index) : nullptr;
}

} // namespace

void MapEditorEngine::clearScriptValues()
{
	s_values.clear();
}

MEAPI int32_t meScriptTemplateCount(int32_t actions)
{
	if (!MapEditorEngine::engineStarted() || !TheScriptEngine)
		return 0;
	return actions ? ScriptAction::NUM_ITEMS : Condition::NUM_ITEMS;
}

MEAPI int32_t meScriptTemplate(int32_t actions, int32_t index, MeScriptTemplate *info)
{
	if (!MapEditorEngine::engineStarted() || !info)
		return 0;
	const Template *t = scriptTemplate(actions, index);
	if (!t)
		return 0;
	*info = MeScriptTemplate{};
	copyText(info->internalName, sizeof(info->internalName), t->m_internalName.str());
	copyText(info->uiName, sizeof(info->uiName), t->m_uiName.str());
	copyText(info->uiName2, sizeof(info->uiName2), t->m_uiName2.str());
	copyText(info->help, sizeof(info->help), t->m_helpText.str());
	info->numParameters = std::min<int32_t>(t->m_numParameters, ME_SCRIPT_MAX_PARAMETERS);
	for (int32_t i = 0; i < info->numParameters; ++i)
		info->parameterTypes[i] = static_cast<int32_t>(t->m_parameters[i]);
	info->numUiStrings = std::min<int32_t>(t->m_numUiStrings, ME_SCRIPT_MAX_PARAMETERS);
	for (int32_t i = 0; i < info->numUiStrings; ++i)
		copyText(info->uiStrings[i], sizeof(info->uiStrings[i]), t->m_uiStrings[i].str());
	return 1;
}

MEAPI int32_t meScriptValueCount(int32_t parameterType)
{
	if (!MapEditorEngine::engineStarted())
		return 0;
	return static_cast<int32_t>(values(parameterType).size());
}

MEAPI int32_t meScriptValue(int32_t parameterType, int32_t index, MeNamedValue *value)
{
	if (!MapEditorEngine::engineStarted() || !value)
		return 0;
	const std::vector<MeNamedValue> &list = values(parameterType);
	if (index < 0 || static_cast<std::size_t>(index) >= list.size())
		return 0;
	*value = list[static_cast<std::size_t>(index)];
	return 1;
}
