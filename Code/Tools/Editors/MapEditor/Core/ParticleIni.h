/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ParticleIni.h -- a particle system as the INI block the game writes and reads
// ("ParticleSystem <Name>", one "Key = value" per line, "End"): parsed into its fields in file order
// and written back, plus the colour and alpha key frames the editor shows as a gradient.

#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace MapEditorCore
{

struct ParticleField
{
	std::string key;
	std::string value;
};

struct ParticleBlock
{
	std::string name;
	std::vector<ParticleField> fields;

	// The value of a key, nullptr if the block has none.
	const std::string *find(const std::string &key) const;
	// Replaces the value of a key, or appends the key.
	void set(const std::string &key, const std::string &value);
};

// The first ParticleSystem block of the text; nullopt if there is none or it has no End.
[[nodiscard]] std::optional<ParticleBlock> parseParticleBlock(const std::string &text);
// The names of every ParticleSystem block in the text, in order, each once.
[[nodiscard]] std::vector<std::string> particleBlockNames(const std::string &text);
// The block as the game's writer lays it out: two spaces before each key, " = ", \n line ends.
[[nodiscard]] std::string writeParticleBlock(const ParticleBlock &block);

// A colour key frame ("R:255 G:128 B:0 30": colour and frame) and an alpha key frame ("0.0 1.0 30":
// alpha range and frame). The game keeps eight of each (Color1..8, Alpha1..8).
struct ColorKey
{
	int r = 0, g = 0, b = 0;
	int frame = 0;
};
struct AlphaKey
{
	float low = 0.0f, high = 0.0f;
	int frame = 0;
};
constexpr int PARTICLE_KEYS = 8;
[[nodiscard]] std::optional<ColorKey> parseColorKey(const std::string &value);
[[nodiscard]] std::string writeColorKey(const ColorKey &key);
[[nodiscard]] std::optional<AlphaKey> parseAlphaKey(const std::string &value);
[[nodiscard]] std::string writeAlphaKey(const AlphaKey &key);

// The key frames the game uses: the first always (the start value), then each following one up to the
// first whose frame is 0. Missing or unreadable keys end the list as well.
[[nodiscard]] std::vector<AlphaKey> usedAlphaKeys(const ParticleBlock &block);
[[nodiscard]] std::vector<ColorKey> usedColorKeys(const ParticleBlock &block);
// Writes keys as Alpha1..8 / Color1..8, at most eight. The first keeps its frame; the others are sorted
// by frame and made strictly increasing from 1 on, since frame 0 would end the list. The slots after the
// last key are cleared as the game's writer does ("0.00 0.00 0", "R:0 G:0 B:0 0").
void setAlphaKeys(ParticleBlock &block, std::vector<AlphaKey> keys);
void setColorKeys(ParticleBlock &block, std::vector<ColorKey> keys);
// The colour of a particle at the frame, linear between the keys like the game; the last key holds.
[[nodiscard]] ColorKey colorAt(const std::vector<ColorKey> &keys, int frame);

// A field whose value differs between two blocks; an empty value means the block has no such field.
struct ParticleChange
{
	std::string key;
	std::string before;
	std::string after;
};
// The fields of after that differ from before, in the order of after, then the fields only before has.
[[nodiscard]] std::vector<ParticleChange> compareParticleBlocks(const ParticleBlock &before, const ParticleBlock &after);

// Allowed words of the fields that name an option of the game (Priority, Shader, Type, VelocityType,
// VolumeType, WindMotion) and of the switches (Yes/No); empty for free values.
[[nodiscard]] std::vector<std::string> particleChoices(const std::string &key);

// The emission volume (VolumeType with its Vol* sizes) as lines around the system at x, y, z: each
// outline runs through its points (x, y, z per point) and is closed if flagged. A point volume is a
// small cross, a sphere three circles, a cylinder (upright, centred) two rings and four sides.
struct ParticleOutline
{
	std::vector<float> points;
	bool closed = false;
};
[[nodiscard]] std::vector<ParticleOutline> emissionOutline(const ParticleBlock &block, float x, float y, float z);

} // namespace MapEditorCore
