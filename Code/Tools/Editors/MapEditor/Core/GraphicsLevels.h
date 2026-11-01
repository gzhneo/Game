/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// GraphicsLevels.h -- the game's detail levels as the editor's 3D view uses them: the Direct3D 11
// effects, water quality and terrain filtering of each StaticGameLOD block in the project's
// GameLODOverride.ini, so the view draws a map as the game does at that level.

#pragma once

#include <string>
#include <vector>

namespace MapEditorCore
{

struct GraphicsLevel
{
	std::string name;				// Low, Medium, High, VeryHigh
	std::string effects;			// D3D11Effects, as -d3d11Effects takes it
	int waterQuality = 0;			// 0 the classic water, 1 and 2 the realistic one
	int terrainAnisotropy = 0;		// anisotropic filtering of the terrain atlas, 0 trilinear
};

// The StaticGameLOD blocks of the text in file order; keys a block lacks keep their defaults.
[[nodiscard]] std::vector<GraphicsLevel> parseGraphicsLevels(const std::string &text);

// The effect names of a list ("fxaa,shadowmap=2048+ssao" -> fxaa, shadowmap, ssao), in order.
[[nodiscard]] std::vector<std::string> effectNames(const std::string &list);
// The list without the named effects; a value after '=' goes with its effect.
[[nodiscard]] std::string effectsWithout(const std::string &list, const std::vector<std::string> &names);

} // namespace MapEditorCore
