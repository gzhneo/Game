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

// UserSettings.cpp -- Settings.ini and State.ini for the game (see UserSettings.h). The typed readers
// are the former OptionPreferences readers, now on the settings object.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/UserSettings.h"

#include "Common/AudioSettings.h"
#include "Common/GameAudio.h"
#include "Common/GameLOD.h"
#include "Common/GlobalData.h"
#include "Common/Money.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "Common/QuotedPrintable.h"
#include "Engine/Settings.h"
#include "Engine/TextureTier.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/ScriptEngine.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/IPEnumeration.h"

#include <string>
#include <utility>
#include <vector>

namespace
{
const char *const SETTINGS_FILE = "Settings.ini";
const char *const STATE_FILE = "State.ini";

struct SettingsFiles
{
	Engine::Settings::Store store;
	AsciiString folder;
	/// Settings that apply at the next start, with the value this run started with.
	std::vector<std::pair<const char *, std::string>> started;
};

void rememberStart(SettingsFiles &settings, const char *onlyId = nullptr)
{
	for (const Engine::Settings::Definition &definition : Engine::Settings::schema())
	{
		if (definition.apply != Engine::Settings::Apply::Restart)
			continue;
		if (onlyId != nullptr && _stricmp(onlyId, definition.id) != 0)
			continue;
		const std::string value = settings.store.text(definition.id);
		bool found = false;
		for (std::pair<const char *, std::string> &entry : settings.started)
			if (entry.first == definition.id)
			{
				entry.second = value;
				found = true;
			}
		if (!found)
			settings.started.emplace_back(definition.id, value);
	}
}

// On the heap, freed by release() before the leak check.
SettingsFiles *s_settings = nullptr;

// Command line values given before the files are read. Fixed size: no allocation that would outlive
// the leak check.
struct PendingCommandLine
{
	const char *id;
	char value[64];
};
PendingCommandLine s_pending[8];
Int s_pendingCount = 0;

std::string readWholeFile(const AsciiString &path, Bool *found = nullptr)
{
	std::string text;
	FILE *fp = fopen(path.str(), "rb");
	if (found != nullptr)
		*found = fp != nullptr;
	if (fp == nullptr)
		return text;
	char buffer[4096];
	size_t got;
	while ((got = fread(buffer, 1, sizeof(buffer), fp)) > 0)
		text.append(buffer, got);
	fclose(fp);
	return text;
}

// Test runs and several local instances leave the user's files alone; the values still change
// in memory for the rest of the run.
Bool settingsWritable()
{
	if (TheGlobalData == nullptr)
		return FALSE;
	const GlobalData &g = *TheGlobalData;
	return g.m_netLocalInstance == 0 && !g.m_settingsReadOnly && !g.m_runnerMode && g.m_maxFrames == 0
		&& g.m_quitAfterFrames == 0 && g.m_shotAfterCount == 0 && g.m_shotEvery == 0 && g.m_shotEveryDraw == 0
		&& g.m_cameraPathFile.isEmpty() && g.m_testScriptFile.isEmpty();
}

// Writes a temporary file and moves it over the old one, so a crash never leaves half a file.
Bool writeAtomically(const AsciiString &path, const std::string &text)
{
	AsciiString temporary = path;
	temporary.concat(".tmp");
	FILE *fp = fopen(temporary.str(), "wb");
	if (fp == nullptr)
		return FALSE;
	const Bool written = fwrite(text.data(), 1, text.size(), fp) == text.size();
	if (fclose(fp) != 0 || !written)
	{
		DeleteFileA(temporary.str());
		return FALSE;
	}
	if (!MoveFileExA(temporary.str(), path.str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		DeleteFileA(temporary.str());
		return FALSE;
	}
	return TRUE;
}

void logReport(const Engine::Settings::Report &report)
{
	for ([[maybe_unused]] const std::string &line : report.lines)
		DEBUG_LOG(("Settings: %s\n", line.c_str()));
}

SettingsFiles &files()
{
	if (s_settings != nullptr)
		return *s_settings;
	s_settings = NEW SettingsFiles;
	s_settings->folder = TheGlobalData->getPath_UserData();

	Engine::Settings::Report report;
	AsciiString settingsPath = s_settings->folder;
	settingsPath.concat(SETTINGS_FILE);
	AsciiString statePath = s_settings->folder;
	statePath.concat(STATE_FILE);
	Bool haveSettings = FALSE;
	const std::string settingsText = readWholeFile(settingsPath, &haveSettings);
	s_settings->store.loadFile(Engine::Settings::File::Settings, settingsText, &report);
	s_settings->store.loadFile(Engine::Settings::File::State, readWholeFile(statePath), &report);
	if (!haveSettings)
	{
		// First start with the new files: take over the old ones. Saved with the first save() or at
		// exit, once the command line says whether this run may write.
		Engine::Settings::LegacyFiles legacy;
		AsciiString path = s_settings->folder;
		path.concat("Options.ini");
		Bool haveOptions = FALSE;
		legacy.options = readWholeFile(path, &haveOptions);
		if (!haveOptions && TheGlobalData->getPath_EAUserData().isNotEmpty())
		{
			// A new user folder starts from the settings of EA's original game; nothing else is taken over.
			path = TheGlobalData->getPath_EAUserData();
			path.concat("Options.ini");
			legacy.options = readWholeFile(path);
		}
		path = s_settings->folder;
		path.concat("Network.ini");
		legacy.network = readWholeFile(path);
		path = s_settings->folder;
		path.concat("Skirmish.ini");
		legacy.skirmish = readWholeFile(path);
		Engine::Settings::importLegacy(s_settings->store, legacy, report);
		DEBUG_LOG(("Settings: %s not found, old preference files imported\n", settingsPath.str()));
	}
	for (Int i = 0; i < s_pendingCount; ++i)
		s_settings->store.setCommandLine(s_pending[i].id, s_pending[i].value, &report);
	s_pendingCount = 0;
	logReport(report);
	rememberStart(*s_settings);
	return *s_settings;
}

const std::string *fileValue(const char *id)
{
	DEBUG_ASSERTCRASH(Engine::Settings::findDefinition(id) != nullptr, ("Unknown setting %s", id));
	return files().store.fileValue(id);
}

// The old readers saw the values in CP1252.
AsciiString toAscii(const std::string &utf8)
{
	return AsciiString(Engine::Settings::utf8ToCp1252(utf8).c_str());
}

// "yes" as the old readers compared it; the object writes yes/no.
Bool isYes(const std::string &value)
{
	return stricmp(value.c_str(), "yes") == 0 ? TRUE : FALSE;
}
}

namespace UserSettings
{

Engine::Settings::Store &store()
{
	return files().store;
}

AsciiString get(const char *id)
{
	DEBUG_ASSERTCRASH(Engine::Settings::findDefinition(id) != nullptr, ("Unknown setting %s", id));
	return toAscii(files().store.text(id));
}

Bool isSetInFile(const char *id)
{
	return fileValue(id) != nullptr;
}

Bool differsFromStart(const char *id)
{
	SettingsFiles &settings = files();
	const Engine::Settings::Definition *definition = Engine::Settings::findDefinition(id);
	if (definition == nullptr)
		return FALSE;
	for (const std::pair<const char *, std::string> &entry : settings.started)
		if (entry.first == definition->id)
			return settings.store.text(definition->id) != entry.second;
	return FALSE;
}

Bool isForced(const char *id)
{
	return files().store.sourceOf(id) == Engine::Settings::Source::CommandLine ? TRUE : FALSE;
}

Bool getBool(const char *id, Bool fallback)
{
	const std::string *value = fileValue(id);
	return value == nullptr ? fallback : isYes(*value);
}

Int getInt(const char *id, Int fallback)
{
	const std::string *value = fileValue(id);
	return value == nullptr ? fallback : atoi(value->c_str());
}

Real getReal(const char *id, Real fallback)
{
	const std::string *value = fileValue(id);
	return value == nullptr ? fallback : static_cast<Real>(atof(value->c_str()));
}

AsciiString getText(const char *id, const AsciiString &fallback)
{
	const std::string *value = fileValue(id);
	return value == nullptr ? fallback : toAscii(*value);
}

Bool set(const char *id, const AsciiString &value)
{
	DEBUG_ASSERTCRASH(Engine::Settings::findDefinition(id) != nullptr, ("Unknown setting %s", id));
	const Bool done = files().store.set(id, Engine::Settings::cp1252ToUtf8(value.str())) ? TRUE : FALSE;
	DEBUG_ASSERTLOG(done, ("Settings: %s = %s not taken\n", id, value.str()));
	return done;
}

Bool setBool(const char *id, Bool value)
{
	return set(id, AsciiString(value ? "yes" : "no"));
}

void unset(const char *id)
{
	DEBUG_ASSERTCRASH(Engine::Settings::findDefinition(id) != nullptr, ("Unknown setting %s", id));
	files().store.unset(id);
}

Bool setInt(const char *id, Int value)
{
	AsciiString text;
	text.format("%d", value);
	return set(id, text);
}

void setCommandLine(const char *id, const char *value)
{
	DEBUG_ASSERTCRASH(Engine::Settings::findDefinition(id) != nullptr, ("Unknown setting %s", id));
	if (s_settings != nullptr)
	{
		Engine::Settings::Report report;
		s_settings->store.setCommandLine(id, value, &report);
		logReport(report);
		// the run starts with the command line value
		rememberStart(*s_settings, id);
		return;
	}
	Int slot = 0;
	while (slot < s_pendingCount && strcmp(s_pending[slot].id, id) != 0)
		++slot;
	if (slot == static_cast<Int>(std::size(s_pending)))
	{
		DEBUG_CRASH(("Too many command line settings, %s ignored", id));
		return;
	}
	s_pending[slot].id = id;
	snprintf(s_pending[slot].value, sizeof(s_pending[slot].value), "%s", value);
	if (slot == s_pendingCount)
		++s_pendingCount;
}

Bool save()
{
	if (!settingsWritable())
		return FALSE;
	if (s_settings == nullptr)
		return TRUE;
	const Engine::Settings::File kinds[2] = { Engine::Settings::File::Settings, Engine::Settings::File::State };
	const char *const names[2] = { SETTINGS_FILE, STATE_FILE };
	for (int i = 0; i < 2; ++i)
	{
		if (!s_settings->store.dirty(kinds[i]))
			continue;
		AsciiString path = s_settings->folder;
		path.concat(names[i]);
		if (writeAtomically(path, s_settings->store.fileText(kinds[i])))
			s_settings->store.markSaved(kinds[i]);
		else
			DEBUG_LOG(("Settings: cannot write %s\n", path.str()));
	}
	return TRUE;
}

void release()
{
	delete s_settings;
	s_settings = nullptr;
	s_pendingCount = 0;
}

// ---- typed readers

Int getCampaignDifficulty()
{
	const std::string *value = fileValue("General.CampaignDifficulty");
	if (value == nullptr)
		return TheScriptEngine->getGlobalDifficulty();

	Int factor = atoi(value->c_str());
	if (factor < DIFFICULTY_EASY)
		factor = DIFFICULTY_EASY;
	if (factor > DIFFICULTY_HARD)
		factor = DIFFICULTY_HARD;
	return factor;
}

void setCampaignDifficulty(Int diff)
{
	setInt("General.CampaignDifficulty", diff);
}

UnsignedInt getLANIPAddress()
{
	const AsciiString selectedIP = getText("Network.IPAddress", AsciiString::TheEmptyString);
	IPEnumeration IPs;
	EnumeratedIP *IPlist = IPs.getAddresses();
	while (IPlist)
	{
		if (selectedIP.compareNoCase(IPlist->getIPstring()) == 0)
			return IPlist->getIP();
		IPlist = IPlist->getNext();
	}
	return 0;	// every adapter
}

Bool getAlternateMouseModeEnabled()
{
	return getBool("Controls.UseAlternateMouse", TheGlobalData->m_useAlternateMouse);
}

Bool getRetaliationModeEnabled()
{
	return getBool("Controls.Retaliation", TheGlobalData->m_clientRetaliationModeEnabled);
}

Bool getDoubleClickAttackMoveEnabled()
{
	return getBool("Controls.UseDoubleClickAttackMove", TheGlobalData->m_doubleClickAttackMove);
}

Real getScrollFactor()
{
	const std::string *value = fileValue("Controls.ScrollFactor");
	if (value == nullptr)
		return TheGlobalData->m_keyboardDefaultScrollFactor;

	Int factor = atoi(value->c_str());
	if (factor < 0)
		factor = 0;
	if (factor > 100)
		factor = 100;
	return factor/100.0f;
}

Bool usesSystemMapDir()
{
	return getBool("General.UseSystemMapDir", TRUE);
}

Bool saveCameraInReplays()
{
	return getBool("Replays.SaveCamera", TRUE);
}

Bool useCameraInReplays()
{
	return getBool("Replays.UseCamera", TRUE);
}

Int getIdealStaticGameDetail()
{
	const std::string *value = fileValue("Detection.IdealStaticGameLOD");
	if (value == nullptr)
		return STATIC_GAME_LOD_UNKNOWN;
	return TheGameLODManager->getStaticGameLODIndex(toAscii(*value));
}

Int getStaticGameDetail()
{
	const std::string *value = fileValue("Detail.StaticGameLOD");
	if (value == nullptr)
		return TheGameLODManager->getStaticLODLevel();
	return TheGameLODManager->getStaticGameLODIndex(toAscii(*value));
}

// A volume the file does not set comes from AudioSettings; the shared effects slider lowers 2D or 3D
// by the relative volume.
Real getSoundVolume()
{
	const std::string *value = fileValue("Audio.SFXVolume");
	if (value == nullptr)
	{
		Real relative = TheAudio->getAudioSettings()->m_relative2DVolume;
		if( relative < 0 )
		{
			Real scale = 1.0f + relative;
			return TheAudio->getAudioSettings()->m_defaultSoundVolume * 100.0f * scale;
		}
		return TheAudio->getAudioSettings()->m_defaultSoundVolume * 100.0f;
	}
	const Real volume = static_cast<Real>(atof(value->c_str()));
	return volume < 0.0f ? 0.0f : volume;
}

Real get3DSoundVolume()
{
	const std::string *value = fileValue("Audio.SFX3DVolume");
	if (value == nullptr)
	{
		Real relative = TheAudio->getAudioSettings()->m_relative2DVolume;
		if( relative > 0 )
		{
			Real scale = 1.0f - relative;
			return TheAudio->getAudioSettings()->m_default3DSoundVolume * 100.0f * scale;
		}
		return TheAudio->getAudioSettings()->m_default3DSoundVolume * 100.0f;
	}
	const Real volume = static_cast<Real>(atof(value->c_str()));
	return volume < 0.0f ? 0.0f : volume;
}

Real getSpeechVolume()
{
	const std::string *value = fileValue("Audio.VoiceVolume");
	if (value == nullptr)
		return TheAudio->getAudioSettings()->m_defaultSpeechVolume * 100.0f;
	const Real volume = static_cast<Real>(atof(value->c_str()));
	return volume < 0.0f ? 0.0f : volume;
}

Real getMusicVolume()
{
	const std::string *value = fileValue("Audio.MusicVolume");
	if (value == nullptr)
		return TheAudio->getAudioSettings()->m_defaultMusicVolume * 100.0f;
	const Real volume = static_cast<Real>(atof(value->c_str()));
	return volume < 0.0f ? 0.0f : volume;
}

Bool getCloudShadowsEnabled()
{
	return getBool("Detail.UseCloudMap", TheGlobalData->m_useCloudMap);
}

Bool getLightmapEnabled()
{
	return getBool("Detail.UseLightMap", TheGlobalData->m_useLightMap);
}

Bool getSmoothWaterEnabled()
{
	return getBool("Detail.ShowSoftWaterEdge", TheGlobalData->m_showSoftWaterEdge);
}

Bool getTreesEnabled()
{
	return getBool("Detail.ShowTrees", TheGlobalData->m_useTrees);
}

// The file says whether extra animations are on; the engine keeps the opposite (draw module LOD).
Bool getExtraAnimationsDisabled()
{
	const std::string *value = fileValue("Detail.ExtraAnimations");
	if (value == nullptr)
		return TheGlobalData->m_useDrawModuleLOD;
	return isYes(*value) ? FALSE : TRUE;
}

Bool getUseHeatEffects()
{
	return getBool("Detail.HeatEffects", TheGlobalData->m_useHeatEffects);
}

Bool getDynamicLODEnabled()
{
	return getBool("Detail.DynamicLOD", TheGlobalData->m_enableDynamicLOD);
}

Bool getFPSLimitEnabled()
{
	return getBool("Detail.FPSLimit", TheGlobalData->m_useFpsLimit);
}

Bool get3DShadowsEnabled()
{
	return getBool("Detail.UseShadowVolumes", TheGlobalData->m_useShadowVolumes);
}

Bool get2DShadowsEnabled()
{
	return getBool("Detail.UseShadowDecals", TheGlobalData->m_useShadowDecals);
}

Bool getBuildingOcclusionEnabled()
{
	return getBool("Detail.BuildingOcclusion", TheGlobalData->m_enableBehindBuildingMarkers);
}

Int getParticleCap()
{
	const std::string *value = fileValue("Detail.MaxParticleCount");
	if (value == nullptr)
		return TheGlobalData->m_maxParticleCount;
	const Int factor = atoi(value->c_str());
	return factor < 100 ? 100 : factor;	// at least 100 particles
}

Int getTextureReduction()
{
	const std::string *value = fileValue("Detail.TextureReduction");
	if (value == nullptr)
		return -1;
	const Int factor = atoi(value->c_str());
	return factor > 2 ? 2 : factor;
}

Int getGammaValue()
{
	const std::string value = files().store.text("Display.Gamma");
	return value.empty() ? 50 : atoi(value.c_str());
}

void getResolution(Int *xres, Int *yres)
{
	*xres = TheGlobalData->m_xResolution;
	*yres = TheGlobalData->m_yResolution;

	const std::string *value = fileValue("Display.Resolution");
	if (value == nullptr)
		return;

	Int selectedXRes, selectedYRes;
	if (sscanf(value->c_str(), "%d%d", &selectedXRes, &selectedYRes) != 2)
		return;
	*xres = selectedXRes;
	*yres = selectedYRes;
}

// Without an entry the CPU topology decides (logical processors - 2); 0 is the serial reference mode.
Int getWorkerThreads()
{
	return getInt("System.WorkerThreads", -1);
}

// A hint about too few CPU cores: no abort, shown once.
Bool getHardwareHint()
{
	return getBool("General.HardwareHint", TRUE);
}

Bool getHardwareHintShown()
{
	return getBool("Hints.HardwareHintShown", FALSE);
}

void setHardwareHintShown()
{
	setBool("Hints.HardwareHintShown", TRUE);
	save();
}

Bool getTestAids()
{
	return getBool("General.TestAids", FALSE);
}

Bool getPrefetchArchives()
{
	return getBool("System.PrefetchArchives", TRUE);
}

Int getRenderFpsLimit()
{
	const Int limit = getInt("Display.RenderFPSLimit", 0);
	return limit > 0 ? limit : 0;
}

// No entry in the options menu (OptionsMenu.wnd is part of the unmodified game data).
Bool getVSyncEnabled()
{
	return getBool("Display.VSync", TRUE);
}

Bool useVulkanRenderer()
{
	return stricmp(files().store.text("Display.RenderBackend").c_str(), "vulkan") == 0 ? TRUE : FALSE;
}

// The menu stores the index of its selection (off/low/high): 0 = off, 1 = 4 samples, 2 = 8 samples;
// 4/8/16 are sample counts. Set_Render_Device falls back to the next level the hardware supports.
Int getAntiAliasing()
{
	switch (getInt("Display.AntiAliasing", 0))
	{
		case 1:		return 4;
		case 2:		return 8;
		case 4:		return 4;
		case 8:		return 8;
		case 16:	return 16;
		default:	return 0;
	}
}

Int getTextureQuality()
{
	const std::string *value = fileValue("Detail.TextureQuality");
	if (value == nullptr)
		return ::Engine::TEXTURE_TIER_AUTO;
	const Int tier = ::Engine::parseTextureTier(value->c_str());
	return (tier == ::Engine::TEXTURE_TIER_INVALID) ? ::Engine::TEXTURE_TIER_AUTO : tier;
}

AsciiString getD3D11Effects()
{
	return getText("Detail.D3D11Effects", AsciiString::TheEmptyString);
}

Real getRenderScale()
{
	const std::string value = files().store.text("Display.RenderScale");
	if (value.empty())
		return -1.0f;
	return GlobalData::normalizeRenderScale(static_cast<Real>(atof(value.c_str())));
}

Int getRenderScaleFilter()
{
	const std::string *value = fileValue("Display.RenderScaleFilter");
	if (value == nullptr)
		return -1;
	return GlobalData::parseRenderScaleFilter(value->c_str());
}

Int getWaterQuality()
{
	const std::string value = files().store.text("Detail.WaterQuality");
	return value.empty() ? -1 : atoi(value.c_str());
}

Int getTerrainAnisotropy()
{
	const std::string value = files().store.text("Detail.TerrainAnisotropy");
	return value.empty() ? -1 : atoi(value.c_str());
}

}	// namespace UserSettings

// ---- LobbySettings: the former LANPreferences and SkirmishPreferences readers

const char *LobbySettings::id(const char *key, char *buffer, size_t size) const
{
	snprintf(buffer, size, "%s.%s", m_kind == Kind::Lan ? "Lan" : "Skirmish", key);
	return buffer;
}

UnicodeString LobbySettings::getUserName() const
{
	char buffer[64];
	const std::string *value = fileValue(id("UserName", buffer, sizeof(buffer)));
	UnicodeString ret;
	if (value != nullptr)
	{
		// the names are UTF-8 in the file; the quoted-printable form is the way to UTF-16
		ret = QuotedPrintableToUnicodeString(AsciiString(Engine::Settings::encodeQuotedUnicode(*value).c_str()));
		ret.trim();
	}
	if (ret.isEmpty())
	{
		IPEnumeration IPs;
		ret.translate(IPs.getMachineName());
	}
	return ret;
}

void LobbySettings::setUserName(const UnicodeString &name)
{
	char buffer[64];
	const std::string utf8 = Engine::Settings::decodeQuotedUnicode(UnicodeStringToQuotedPrintable(name).str());
	files().store.set(id("UserName", buffer, sizeof(buffer)), utf8);
}

Int LobbySettings::getPreferredColor() const
{
	char buffer[64];
	const std::string *value = fileValue(id("Color", buffer, sizeof(buffer)));
	if (value == nullptr)
		return -1;
	Int ret = atoi(value->c_str());
	if (ret < -1 || ret >= TheMultiplayerSettings->getNumColors())
		ret = -1;
	return ret;
}

void LobbySettings::setPreferredColor(Int color)
{
	char buffer[64];
	UserSettings::setInt(id("Color", buffer, sizeof(buffer)), color);
}

// The LAN lobby does not start as an observer; the skirmish setup keeps what was chosen.
Int LobbySettings::getPreferredFaction() const
{
	char buffer[64];
	const std::string *value = fileValue(id("PlayerTemplate", buffer, sizeof(buffer)));
	if (value == nullptr)
		return PLAYERTEMPLATE_RANDOM;

	Int ret = atoi(value->c_str());
	if ((m_kind == Kind::Lan && ret == PLAYERTEMPLATE_OBSERVER) || ret < PLAYERTEMPLATE_MIN
		|| ret >= ThePlayerTemplateStore->getPlayerTemplateCount())
		ret = PLAYERTEMPLATE_RANDOM;

	if (ret >= 0)
	{
		const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(ret);
		if (!fac)
			ret = PLAYERTEMPLATE_RANDOM;
		else if (fac->getStartingBuilding().isEmpty())
			ret = PLAYERTEMPLATE_RANDOM;
	}
	return ret;
}

void LobbySettings::setPreferredFaction(Int faction)
{
	char buffer[64];
	UserSettings::setInt(id("PlayerTemplate", buffer, sizeof(buffer)), faction);
}

AsciiString LobbySettings::getPreferredMap() const
{
	char buffer[64];
	const std::string *value = fileValue(id("Map", buffer, sizeof(buffer)));
	AsciiString ret;
	if (value != nullptr)
	{
		ret = toAscii(*value);
		ret.trim();
	}
	if (ret.isEmpty() || !isValidMap(ret, TRUE))
		ret = getDefaultMap(TRUE);
	return ret;
}

void LobbySettings::setPreferredMap(const AsciiString &map)
{
	char buffer[64];
	UserSettings::set(id("Map", buffer, sizeof(buffer)), map);
}

Bool LobbySettings::usesSystemMapDir() const
{
	char buffer[64];
	return UserSettings::getBool(id("UseSystemMapDir", buffer, sizeof(buffer)), TRUE);
}

void LobbySettings::setUsesSystemMapDir(Bool system)
{
	char buffer[64];
	UserSettings::setBool(id("UseSystemMapDir", buffer, sizeof(buffer)), system);
}

Bool LobbySettings::getSuperweaponRestricted() const
{
	char buffer[64];
	return UserSettings::getBool(id("SuperweaponRestrict", buffer, sizeof(buffer)), FALSE);
}

void LobbySettings::setSuperweaponRestricted(Bool restricted)
{
	char buffer[64];
	UserSettings::setBool(id("SuperweaponRestrict", buffer, sizeof(buffer)), restricted);
}

Money LobbySettings::getStartingCash() const
{
	char buffer[64];
	const std::string *value = fileValue(id("StartingCash", buffer, sizeof(buffer)));
	if (value == nullptr)
		return TheMultiplayerSettings->getDefaultStartingMoney();

	Money money;
	money.deposit(strtoul(value->c_str(), nullptr, 10), FALSE);
	return money;
}

void LobbySettings::setStartingCash(const Money &cash)
{
	char buffer[64];
	AsciiString option;
	option.format("%d", cash.countMoney());
	UserSettings::set(id("StartingCash", buffer, sizeof(buffer)), option);
}

UnsignedShort LobbySettings::getGraceMinutes() const
{
	char buffer[64];
	const Int minutes = UserSettings::getInt(id("GracePeriod", buffer, sizeof(buffer)), 0);
	return static_cast<UnsignedShort>((minutes < 0) ? 0 : ((minutes > 60) ? 60 : minutes));
}

void LobbySettings::setGraceMinutes(UnsignedShort minutes)
{
	char buffer[64];
	UserSettings::setInt(id("GracePeriod", buffer, sizeof(buffer)), minutes);
}

UnsignedShort LobbySettings::getMaxZoomPercent() const
{
	char buffer[64];
	const Int percent = UserSettings::getInt(id("MaxZoom", buffer, sizeof(buffer)), 100);
	return static_cast<UnsignedShort>((percent < 100) ? 100 : ((percent > 125) ? 125 : percent));
}

void LobbySettings::setMaxZoomPercent(UnsignedShort percent)
{
	char buffer[64];
	UserSettings::setInt(id("MaxZoom", buffer, sizeof(buffer)), percent);
}

// Entries of Lan.RemoteIPs, separated by ';', in CP1252.
std::vector<AsciiString> LobbySettings::getRemoteIPs() const
{
	std::vector<AsciiString> entries;
	const std::string *value = fileValue("Lan.RemoteIPs");
	if (value == nullptr)
		return entries;
	std::size_t pos = 0;
	while (pos < value->size())
	{
		std::size_t end = value->find(';', pos);
		if (end == std::string::npos)
			end = value->size();
		if (end > pos)
			entries.push_back(toAscii(value->substr(pos, end - pos)));
		pos = end + 1;
	}
	return entries;
}

Int LobbySettings::getNumRemoteIPs() const
{
	return static_cast<Int>(getRemoteIPs().size());
}

UnicodeString LobbySettings::getRemoteIPEntry(Int i) const
{
	UnicodeString ret;
	const std::vector<AsciiString> entries = getRemoteIPs();
	if (i < 0 || i >= static_cast<Int>(entries.size()))
		return ret;

	AsciiString asciientry = entries[i];
	AsciiString ipstr;

	// "address:name"; an IPv6 address is kept in brackets, its colons do not separate
	const char *start = asciientry.str();
	const char *close = (start[0] == '[') ? strchr(start, ']') : nullptr;
	const char *colon = strchr(close ? close : start, ':');
	if (colon)
	{
		for (const char *p = start; p < colon; ++p)
			ipstr.concat(*p);
		asciientry.set(colon + 1); // skip the ':'
	}
	else
	{
		ipstr = asciientry;
		asciientry.clear();
	}

	ret.translate(ipstr);
	if (asciientry.getLength() > 0)
	{
		ret.concat(L"(");
		ret.concat(QuotedPrintableToUnicodeString(asciientry));
		ret.concat(L")");
	}
	return ret;
}

void LobbySettings::setRemoteIPs(const std::vector<AsciiString> &entries)
{
	std::string list;
	for (const AsciiString &entry : entries)
	{
		if (!list.empty())
			list += ';';
		list += Engine::Settings::cp1252ToUtf8(entry.str());
	}
	files().store.set("Lan.RemoteIPs", list);
}

AsciiString LobbySettings::getSlotList() const
{
	return UserSettings::getText("Skirmish.SlotList", AsciiString::TheEmptyString);
}

void LobbySettings::setSlotList(const AsciiString &slots)
{
	UserSettings::set("Skirmish.SlotList", slots);
}

Int LobbySettings::getGameSpeed(Int fallback) const
{
	return UserSettings::getInt("Skirmish.FPS", fallback);
}

void LobbySettings::setGameSpeed(Int fps)
{
	UserSettings::setInt("Skirmish.FPS", fps);
}

Bool LobbySettings::write() const
{
	return UserSettings::save();
}
