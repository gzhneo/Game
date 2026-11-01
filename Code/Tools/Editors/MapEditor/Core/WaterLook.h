/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WaterLook.h -- how a map's water looks: the WaterTransparency values the game reads from
// Water.ini, which a map overrides in the block of the same name in its map.ini. The realistic
// water of the detail levels Medium and up uses the turbidity, refraction and reflection.

#pragma once

#include <array>
#include <string>

namespace MapEditorCore
{

struct WaterLook
{
	float transparentDepth = 3.0f;				// TransparentWaterDepth
	float minOpacity = 1.0f;					// TransparentWaterMinOpacity
	std::array<int, 3> standingColor{255, 255, 255};	// StandingWaterColor, 0..255
	bool additive = false;						// AdditiveBlending
	float turbidityDepth = 10.0f;				// TurbidityDepth; 0 keeps the classic water on this map
	std::array<int, 3> turbidityColor{70, 115, 120};	// TurbidityColor
	float refraction = 1.0f;					// RefractionStrength
	float reflection = 1.0f;					// ReflectionStrength

	bool operator==(const WaterLook &) const = default;
};

// The look with the keys of the map.ini's WaterTransparency block over base (the game data).
[[nodiscard]] WaterLook readWaterLook(const std::string &mapIni, const WaterLook &base);
// The map.ini text with one WaterTransparency block holding the keys where look differs from base,
// none if it does not; other blocks and lines stay as they are, line ends as in the text.
[[nodiscard]] std::string writeWaterLook(const std::string &mapIni, const WaterLook &look, const WaterLook &base);

} // namespace MapEditorCore
