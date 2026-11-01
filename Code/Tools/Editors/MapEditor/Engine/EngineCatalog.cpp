/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EngineCatalog.cpp -- the object catalog for the editor: every object template the engine read
// from the game data, with its side, editor sorting, display name and placement angle, and the road
// and bridge types and the sound effects. Collected once after the engine start.

#include "Engine/EngineApi.h"
#include "Engine/EngineInternal.h"

#include "Common/AudioEventInfo.h"
#include "Common/AudioEventRTS.h"
#include "Common/FileSystem.h"
#include "Common/Geometry.h"
#include "Common/GameAudio.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/GameText.h"
#include "GameClient/TerrainRoads.h"
#include "GameLogic/Weapon.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

std::vector<MeTemplateInfo> s_catalog;
std::vector<MeRoadInfo> s_roads;
std::vector<MeSoundInfo> s_sounds;
std::vector<MePlayerTemplateInfo> s_factions;
std::vector<MeColorInfo> s_colors;
bool s_built = false;

void copyText(char *dest, std::size_t size, const char *text)
{
	std::strncpy(dest, text ? text : "", size - 1);
	dest[size - 1] = '\0';
}

// UTF-16 of the game's texts to UTF-8 for the editor.
std::string toUtf8(const WideChar *text)
{
	std::string out;
	for (const WideChar *p = text; p && *p; ++p)
	{
		unsigned c = static_cast<unsigned>(*p) & 0xFFFFu;
		if (c >= 0xD800u && c <= 0xDBFFu && p[1] >= 0xDC00 && p[1] <= 0xDFFF)
		{
			c = 0x10000u + ((c - 0xD800u) << 10) + ((static_cast<unsigned>(p[1]) & 0xFFFFu) - 0xDC00u);
			++p;
		}
		if (c < 0x80u)
			out += static_cast<char>(c);
		else if (c < 0x800u)
		{
			out += static_cast<char>(0xC0u | (c >> 6));
			out += static_cast<char>(0x80u | (c & 0x3Fu));
		}
		else if (c < 0x10000u)
		{
			out += static_cast<char>(0xE0u | (c >> 12));
			out += static_cast<char>(0x80u | ((c >> 6) & 0x3Fu));
			out += static_cast<char>(0x80u | (c & 0x3Fu));
		}
		else
		{
			out += static_cast<char>(0xF0u | (c >> 18));
			out += static_cast<char>(0x80u | ((c >> 12) & 0x3Fu));
			out += static_cast<char>(0x80u | ((c >> 6) & 0x3Fu));
			out += static_cast<char>(0x80u | (c & 0x3Fu));
		}
	}
	return out;
}

// A UTF-8 string cut to fit, without splitting a character.
void copyUtf8(char *dest, std::size_t size, const std::string &text)
{
	std::size_t n = std::min(text.size(), size - 1);
	while (n > 0 && n < text.size() && (static_cast<unsigned char>(text[n]) & 0xC0u) == 0x80u)
		--n;
	std::memcpy(dest, text.data(), n);
	dest[n] = '\0';
}

void build()
{
	if (s_built || !TheThingFactory)
		return;
	s_built = true;
	if (TheAudio)
	{
		for (const auto &entry : TheAudio->getAllAudioEvents())
		{
			const AudioEventInfo *event = entry.second.get();
			if (!event || event->m_soundType != AudioType::AT_SoundEffect || event->m_audioName.getLength() >= 96)
				continue;
			MeSoundInfo info{};
			copyText(info.name, sizeof(info.name), event->m_audioName.str());
			info.volume = event->m_volume;
			info.minVolume = event->m_minVolume;
			info.minRange = event->m_minDistance;
			info.maxRange = event->m_maxDistance;
			info.priority = static_cast<int32_t>(event->m_priority);
			info.looping = BitTest(event->m_control, AC_LOOP) ? 1 : 0;
			info.loopCount = event->m_loopCount;
			s_sounds.push_back(info);
		}
		std::sort(s_sounds.begin(), s_sounds.end(), [](const MeSoundInfo &a, const MeSoundInfo &b) { return std::strcmp(a.name, b.name) < 0; });
	}
	if (TheTerrainRoads)
	{
		for (TerrainRoadType *road = TheTerrainRoads->firstRoad(); road; road = TheTerrainRoads->nextRoad(road))
		{
			MeRoadInfo info{};
			if (road->getName().getLength() >= static_cast<int>(sizeof(info.name)))
				continue;
			copyText(info.name, sizeof(info.name), road->getName().str());
			s_roads.push_back(info);
		}
		for (TerrainRoadType *bridge = TheTerrainRoads->firstBridge(); bridge; bridge = TheTerrainRoads->nextBridge(bridge))
		{
			MeRoadInfo info{};
			if (bridge->getName().getLength() >= static_cast<int>(sizeof(info.name)))
				continue;
			copyText(info.name, sizeof(info.name), bridge->getName().str());
			info.bridge = 1;
			s_roads.push_back(info);
		}
	}
	for (const ThingTemplate *t = TheThingFactory->firstTemplate(); t; t = t->friend_getNextTemplate())
	{
		MeTemplateInfo info{};
		// Names that do not fit would place a different template; they are left out.
		if (t->getName().getLength() >= static_cast<int>(sizeof(info.name)))
			continue;
		copyText(info.name, sizeof(info.name), t->getName().str());
		copyText(info.side, sizeof(info.side), t->getDefaultOwningSide().str());
		copyUtf8(info.displayName, sizeof(info.displayName), toUtf8(t->getDisplayName().str()));
		info.sorting = static_cast<int32_t>(t->getEditorSorting());
		info.placementAngle = t->getPlacementViewAngle();
		info.fenceWidth = t->getFenceWidth();
		info.fenceOffset = t->getFenceXOffset();
		info.energy = t->getEnergyProduction();
		const GeometryInfo &geometry = t->getTemplateGeometryInfo();
		info.geometry = static_cast<int32_t>(geometry.getGeomType());
		info.majorRadius = geometry.getMajorRadius();
		info.minorRadius = geometry.getMinorRadius();
		info.visionRange = t->friend_calcVisionRange();
		// The longest range per slot over all weapon sets (upgrades, veterancy).
		for (const WeaponTemplateSet &set : t->getWeaponTemplateSets())
		{
			for (int slot = 0; slot < WEAPONSLOT_COUNT && slot < 3; ++slot)
			{
				if (const WeaponTemplate *weapon = set.getNth(static_cast<WeaponSlotType>(slot)))
					info.weaponRange[slot] = std::max(info.weaponRange[slot], weapon->getUnmodifiedAttackRange());
			}
		}
		// The silent event stands for "no sound" (as in the old WorldBuilder's object dialog).
		if (t->hasSoundAmbient() && t->getSoundAmbient() && TheAudio && t->getSoundAmbient() != TheAudio->getValidSilentAudioEvent())
			copyText(info.ambientSound, sizeof(info.ambientSound), t->getSoundAmbient()->getEventName().str());
		s_catalog.push_back(info);
	}
	if (ThePlayerTemplateStore)
	{
		for (Int i = 0; i < ThePlayerTemplateStore->getPlayerTemplateCount(); ++i)
		{
			const PlayerTemplate *pt = ThePlayerTemplateStore->getNthPlayerTemplate(i);
			MePlayerTemplateInfo info{};
			if (!pt || pt->getName().getLength() >= static_cast<int>(sizeof(info.name)))
				continue;
			copyText(info.name, sizeof(info.name), pt->getName().str());
			copyText(info.side, sizeof(info.side), pt->getSide().str());
			copyUtf8(info.displayName, sizeof(info.displayName), toUtf8(pt->getDisplayName().str()));
			info.color = static_cast<uint32_t>(pt->getPreferredColor()->getAsInt()) & 0xFFFFFFu;
			info.playable = pt->isPlayableSide() ? 1 : 0;
			s_factions.push_back(info);
		}
	}
	if (TheMultiplayerSettings)
	{
		for (Int c = 0; c < TheMultiplayerSettings->getNumColors(); ++c)
		{
			const MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor(c);
			if (!def)
				continue;
			MeColorInfo info{};
			const std::string name = TheGameText ? toUtf8(TheGameText->fetch(def->getTooltipName().str()).str()) : std::string(def->getTooltipName().str());
			copyUtf8(info.name, sizeof(info.name), name);
			info.color = static_cast<uint32_t>(def->getColor());
			s_colors.push_back(info);
		}
	}
	char line[96];
	std::snprintf(line, sizeof(line), "catalog: %d object templates, %d road and bridge types, %d sound effects",
		static_cast<int>(s_catalog.size()), static_cast<int>(s_roads.size()), static_cast<int>(s_sounds.size()));
	MapEditorEngine::report(line);
}

} // namespace

std::string MapEditorEngine::utf8(const wchar_t *text)
{
	return toUtf8(text);
}

void MapEditorEngine::clearCatalog()
{
	s_catalog.clear();
	s_roads.clear();
	s_sounds.clear();
	s_factions.clear();
	s_colors.clear();
	s_built = false;
}

MEAPI int32_t meSoundCount(void)
{
	if (!MapEditorEngine::engineStarted())
		return 0;
	build();
	return static_cast<int32_t>(s_sounds.size());
}

MEAPI int32_t meSoundEntry(int32_t index, MeSoundInfo *info)
{
	if (!MapEditorEngine::engineStarted() || !info)
		return 0;
	build();
	if (index < 0 || static_cast<std::size_t>(index) >= s_sounds.size())
		return 0;
	*info = s_sounds[static_cast<std::size_t>(index)];
	return 1;
}

MEAPI int32_t mePlayerTemplateCount(void)
{
	if (!MapEditorEngine::engineStarted())
		return 0;
	build();
	return static_cast<int32_t>(s_factions.size());
}

MEAPI int32_t mePlayerTemplateEntry(int32_t index, MePlayerTemplateInfo *info)
{
	if (!MapEditorEngine::engineStarted() || !info)
		return 0;
	build();
	if (index < 0 || static_cast<std::size_t>(index) >= s_factions.size())
		return 0;
	*info = s_factions[static_cast<std::size_t>(index)];
	return 1;
}

MEAPI int32_t meColorCount(void)
{
	if (!MapEditorEngine::engineStarted())
		return 0;
	build();
	return static_cast<int32_t>(s_colors.size());
}

MEAPI int32_t meColorEntry(int32_t index, MeColorInfo *info)
{
	if (!MapEditorEngine::engineStarted() || !info)
		return 0;
	build();
	if (index < 0 || static_cast<std::size_t>(index) >= s_colors.size())
		return 0;
	*info = s_colors[static_cast<std::size_t>(index)];
	return 1;
}

MEAPI int32_t meRoadCount(void)
{
	if (!MapEditorEngine::engineStarted())
		return 0;
	build();
	return static_cast<int32_t>(s_roads.size());
}

MEAPI int32_t meRoadEntry(int32_t index, MeRoadInfo *info)
{
	if (!MapEditorEngine::engineStarted() || !info)
		return 0;
	build();
	if (index < 0 || static_cast<std::size_t>(index) >= s_roads.size())
		return 0;
	*info = s_roads[static_cast<std::size_t>(index)];
	return 1;
}

MEAPI int32_t meCatalogCount(void)
{
	if (!MapEditorEngine::engineStarted())
		return 0;
	build();
	return static_cast<int32_t>(s_catalog.size());
}

MEAPI int32_t meCatalogEntry(int32_t index, MeTemplateInfo *info)
{
	if (!MapEditorEngine::engineStarted() || !info)
		return 0;
	build();
	if (index < 0 || static_cast<std::size_t>(index) >= s_catalog.size())
		return 0;
	*info = s_catalog[static_cast<std::size_t>(index)];
	return 1;
}

namespace
{

// Noise textures for the macro texture: the game's names contain "Noise" (TSNoiseUrb.tga, ...).
const std::vector<std::string> &macroTextures()
{
	static std::vector<std::string> names;
	static bool read = false;
	if (read || !TheFileSystem)
		return names;
	read = true;
	FilenameList files;
	TheFileSystem->getFileListInDirectory(AsciiString("Art\\Textures\\"), AsciiString("*.tga"), files, FALSE);
	for (const AsciiString &file : files)
	{
		std::string name = file.str();
		const std::size_t slash = name.find_last_of("\\/");
		if (slash != std::string::npos)
			name = name.substr(slash + 1);
		std::string lower = name;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (lower.find("noise") != std::string::npos)
			names.push_back(name);
	}
	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());
	return names;
}

} // namespace

MEAPI int32_t meMacroTextureCount(void)
{
	return static_cast<int32_t>(macroTextures().size());
}

MEAPI int32_t meMacroTextureName(int32_t index, char *name, int32_t size)
{
	const std::vector<std::string> &names = macroTextures();
	if (index < 0 || index >= static_cast<int32_t>(names.size()) || !name || size <= 0)
		return 0;
	std::snprintf(name, static_cast<std::size_t>(size), "%s", names[static_cast<std::size_t>(index)].c_str());
	return 1;
}
