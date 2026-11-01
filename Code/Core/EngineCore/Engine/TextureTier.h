/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// TextureTier.h -- texture assets in three tiers SD, HD, UHD. Only the parts that need no
// engine header and are tested in EngineTests:
//   * tier names and parsing "Auto|SD|HD|UHD" (Options.ini TextureQuality, -textureQuality),
//   * the video memory query via DXGI without a device (budget, UMA flag, adapter name),
//     because the tier is fixed when the archive system starts, before the render device,
//   * the automatic selection rule.
// Loading the tier archives is done by ArchiveFileSystem::loadTextureTiers (TextureTiers.cpp).

#pragma once

#ifndef ENGINE_TEXTURETIER_H
#define ENGINE_TEXTURETIER_H

#include <cstdint>

namespace Engine
{

enum TextureTier
{
	TEXTURE_TIER_SD = 0,		///< original size
	TEXTURE_TIER_HD = 1,		///< double edge length
	TEXTURE_TIER_UHD = 2,		///< quadruple edge length
	TEXTURE_TIER_COUNT = 3
};

const int TEXTURE_TIER_AUTO = -1;			///< choose by output height and memory
const int TEXTURE_TIER_INVALID = -2;		///< text was not a valid tier
const int TEXTURE_TIER_UNSET = -3;			///< no command line switch given

/// "Auto", "SD", "HD", "UHD" (case-insensitive, "4K" counts as UHD). Returns a tier,
/// TEXTURE_TIER_AUTO or TEXTURE_TIER_INVALID.
int parseTextureTier(const char *text);

/// "SD", "HD", "UHD", "Auto" or "?".
const char *textureTierName(int tier);

/// Subfolder under Texturstufen\ ("HD", "UHD"); nullptr for SD (the originals).
const char *textureTierFolder(int tier);

/// Video memory of the first adapter, queried via DXGI without a render device.
struct VideoMemoryInfo
{
	bool			valid = false;				///< adapter found
	char			adapterName[128] = {};		///< adapter description (ASCII)
	unsigned		vendorId = 0;
	unsigned		deviceId = 0;
	std::uint64_t	dedicatedVideoMemory = 0;	///< DXGI_ADAPTER_DESC1, on UMA only the BIOS share
	std::uint64_t	sharedSystemMemory = 0;		///< DXGI_ADAPTER_DESC1
	bool			budgetKnown = false;		///< IDXGIAdapter3 available (Windows 10)
	std::uint64_t	localBudget = 0;			///< QueryVideoMemoryInfo, segment LOCAL: budget
	std::uint64_t	localUsage = 0;				///< same: currently used (other processes not counted)
	std::uint64_t	nonLocalBudget = 0;			///< segment NON_LOCAL (system memory for a dGPU)
	bool			umaKnown = false;			///< query via D3D11 succeeded
	bool			unifiedMemory = false;		///< D3D11_FEATURE_DATA_D3D11_OPTIONS2::UnifiedMemoryArchitecture

	/// Query (once, then cached). Outside Windows valid stays false.
	static const VideoMemoryInfo &get();

	/// Memory the automatic choice is based on: the budget, else the video memory size (shared
	/// memory on UMA). 0 = unknown.
	std::uint64_t budgetBytes() const;

	/// One line for the log, e.g. "Intel(R) HD Graphics 4600, UMA ja, Budget 3.9 GB, dediziert 128 MB".
	void describe(char *buffer, unsigned bufferSize) const;
};

/// Detail level for the first start, numbered like StaticGameLODLevel (0 Low ... 3 VeryHigh).
struct DetailRecommendation
{
	int				level = 3;
	const char *	reason = "";						///< short reason for the log (static text)
};

/// Integrated graphics (UMA, or less than DETAIL_LOW_BELOW_BYTES dedicated memory: Intel and AMD integrated chips
/// report only a BIOS share) -> Low; a dedicated card below DETAIL_VERY_HIGH_FROM_BYTES -> High, from there VeryHigh.
/// Without an adapter (no query possible) -> VeryHigh, as before.
DetailRecommendation recommendDetailLevel(const VideoMemoryInfo &info);

const std::uint64_t DETAIL_LOW_BELOW_BYTES = 2ull * 1024ull * 1024ull * 1024ull;
const std::uint64_t DETAIL_VERY_HIGH_FROM_BYTES = 6ull * 1024ull * 1024ull * 1024ull;

/// Inputs of the tier choice. Everything the engine would otherwise fetch from singletons is here
/// so the rule can be tested without the game.
struct TextureTierInput
{
	int				requested = TEXTURE_TIER_AUTO;		///< requested tier or TEXTURE_TIER_AUTO
	bool			lowDetail = false;					///< static detail level Low
	int				outputHeight = 0;					///< output height in pixels (0 = unknown)
	std::uint64_t	budgetBytes = 0;					///< video memory budget, 0 = unknown
	bool			unifiedMemory = false;				///< graphics with shared memory (UMA)
	bool			available[TEXTURE_TIER_COUNT] = { true, false, false };	///< tier archives present
	std::uint64_t	demandBytes[TEXTURE_TIER_COUNT] = {};	///< working set of a map per tier (0 = unknown)
};

struct TextureTierChoice
{
	int				tier = TEXTURE_TIER_SD;
	const char *	reason = "";						///< short reason for the log (static text)
};

/// Automatic choice:
///   0. detail level Low -> SD (minimum hardware: least memory and bandwidth, fastest loading),
///   1. output height H: H < 900 -> SD, H < 1800 -> HD, else UHD,
///   2. step down while working set * 1.5 > budget,
///   3. UMA at most HD,
///   4. only available tiers; if one is missing, fall back to the next lower one.
/// An explicit choice (SD/HD/UHD) skips 0 to 3.
TextureTierChoice chooseTextureTier(const TextureTierInput &input);

/// Height thresholds of rule 1 (for tests).
const int TEXTURE_TIER_HD_MIN_HEIGHT = 900;
const int TEXTURE_TIER_UHD_MIN_HEIGHT = 1800;

} // namespace Engine

#endif // ENGINE_TEXTURETIER_H
