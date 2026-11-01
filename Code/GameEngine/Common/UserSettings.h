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

// UserSettings.h -- the game's access to Settings.ini and State.ini. One Engine::Settings::Store holds
// both files; a setting is addressed by its schema id ("Section.Key", Engine/SettingsSchema.cpp).
//
// The typed readers below keep the fallbacks of the old Options.ini readers: when the file does not set
// a value, it comes from game data (GlobalData, AudioSettings, the detail level), not from the schema.
// Values change in memory at once; save() writes the files that changed, unless the run may not write
// (test runs, -einstellungenNurLesen).

#pragma once

#ifndef USERSETTINGS_H
#define USERSETTINGS_H

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"
#include "Common/UnicodeString.h"

#include <vector>

class Money;

namespace Engine { namespace Settings { class Store; } }

namespace UserSettings
{

// ---- by schema id

/// Effective value (command line, else file, else default) in CP1252; empty if none.
AsciiString get(const char *id);
/// True if Settings.ini or State.ini sets the value.
Bool isSetInFile(const char *id);
/// True if the command line sets the value (the menu shows it locked and does not save it).
Bool isForced(const char *id);
/// True for a setting that applies at the next start whose value is no longer the one this run started
/// with (false for all other settings).
Bool differsFromStart(const char *id);
/// The value the file sets, else fallback.
Bool getBool(const char *id, Bool fallback);
Int getInt(const char *id, Int fallback);
Real getReal(const char *id, Real fallback);
AsciiString getText(const char *id, const AsciiString &fallback);

/// Sets the file layer (text in CP1252). False if the id is unknown or the value invalid.
Bool set(const char *id, const AsciiString &value);
Bool setBool(const char *id, Bool value);
Bool setInt(const char *id, Int value);
/// Removes the value from the file layer, so the default (or game data) applies again.
void unset(const char *id);

/// Command line layer: overrides the file, never written. May be called before the files are read.
void setCommandLine(const char *id, const char *value);

/// Writes Settings.ini and State.ini if they changed and the run may write; false if it may not.
Bool save();
/// Frees the settings before the engine's leak check at exit.
void release();

Engine::Settings::Store &store();

// ---- typed readers with the fallbacks of the old readers

Int getCampaignDifficulty();
void setCampaignDifficulty(Int diff);
UnsignedInt getLANIPAddress();
Bool getAlternateMouseModeEnabled();
Bool getRetaliationModeEnabled();
Bool getDoubleClickAttackMoveEnabled();
Real getScrollFactor();
Bool usesSystemMapDir();
Bool saveCameraInReplays();
Bool useCameraInReplays();
Int getStaticGameDetail();			///< detail level selected by the user
Int getIdealStaticGameDetail();		///< detail level detected for this computer
Real getSoundVolume();
Real get3DSoundVolume();
Real getSpeechVolume();
Real getMusicVolume();
Bool getCloudShadowsEnabled();
Bool getLightmapEnabled();
Bool getSmoothWaterEnabled();
Bool getTreesEnabled();
Bool getExtraAnimationsDisabled();
Bool getUseHeatEffects();
Bool getDynamicLODEnabled();
Bool getFPSLimitEnabled();
Bool get3DShadowsEnabled();
Bool get2DShadowsEnabled();
Bool getBuildingOcclusionEnabled();
Int getParticleCap();
Int getTextureReduction();			///< -1 if not set
Int getGammaValue();				///< slider position 0..100, -gamma included
void getResolution(Int *xres, Int *yres);
Int getWorkerThreads();				///< -1: automatic, 0: serial
Bool getHardwareHint();
Bool getHardwareHintShown();
void setHardwareHintShown();
Bool getTestAids();
Bool getPrefetchArchives();
Int getRenderFpsLimit();			///< 0: none
Bool getVSyncEnabled();
Bool useVulkanRenderer();			///< Display.RenderBackend, -vulkan and -d3d11 included
Int getAntiAliasing();				///< multisampling samples: menu index 0/1/2 or 4/8/16
Int getTextureQuality();			///< Engine::TEXTURE_TIER_*
AsciiString getD3D11Effects();
Real getRenderScale();				///< 0.5..1, -renderScale included; -1 if not set
Int getRenderScaleFilter();			///< 0 bilinear, 1 bicubic, 2 cas; -1 if not set
Int getWaterQuality();				///< -waterQuality, else -1 (the detail level decides)
Int getTerrainAnisotropy();			///< -terrainAnisotropy, else -1 (the detail level decides)

}	// namespace UserSettings

/// What the LAN lobby or the skirmish setup remembers (State.ini, [Lan] or [Skirmish]). Values change in the
/// shared settings object at once; write() saves.
class LobbySettings
{
public:
	enum class Kind { Lan, Skirmish };
	explicit LobbySettings(Kind kind) : m_kind(kind) {}

	UnicodeString getUserName() const;			///< the computer name if not set
	void setUserName(const UnicodeString &name);
	Int getPreferredColor() const;				///< -1: random
	void setPreferredColor(Int color);
	Int getPreferredFaction() const;			///< PLAYERTEMPLATE_RANDOM if not set or not playable
	void setPreferredFaction(Int faction);
	AsciiString getPreferredMap() const;		///< the default map if not set or missing
	void setPreferredMap(const AsciiString &map);
	Bool usesSystemMapDir() const;
	void setUsesSystemMapDir(Bool system);
	Bool getSuperweaponRestricted() const;
	void setSuperweaponRestricted(Bool restricted);
	Money getStartingCash() const;
	void setStartingCash(const Money &cash);
	UnsignedShort getGraceMinutes() const;
	void setGraceMinutes(UnsignedShort minutes);
	UnsignedShort getMaxZoomPercent() const;	///< LAN host: zoom-out limit for every player, 100 to 125
	void setMaxZoomPercent(UnsignedShort percent);

	// LAN: addresses of direct connections, newest first, each "address" or "address(name)"
	std::vector<AsciiString> getRemoteIPs() const;
	Int getNumRemoteIPs() const;
	UnicodeString getRemoteIPEntry(Int i) const;
	void setRemoteIPs(const std::vector<AsciiString> &entries);

	// Skirmish: the slot setup and the game speed of the last game
	AsciiString getSlotList() const;
	void setSlotList(const AsciiString &slots);
	Int getGameSpeed(Int fallback) const;
	void setGameSpeed(Int fps);

	/// Saves State.ini (see UserSettings::save).
	Bool write() const;

private:
	const char *id(const char *key, char *buffer, size_t size) const;
	Kind m_kind;
};

#endif	// USERSETTINGS_H
