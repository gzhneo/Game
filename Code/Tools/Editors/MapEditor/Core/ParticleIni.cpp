/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ParticleIni.cpp -- a particle system as its INI block.

#include "Core/ParticleIni.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <utility>

namespace MapEditorCore
{

namespace
{

std::string trim(const std::string &s)
{
	std::size_t a = 0;
	std::size_t b = s.size();
	while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
		++a;
	while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
		--b;
	return s.substr(a, b - a);
}

bool sameWord(const std::string &a, const char *b)
{
	std::size_t i = 0;
	for (; i < a.size() && b[i]; ++i)
		if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
			return false;
	return i == a.size() && !b[i];
}

// The text before a ';' comment.
std::string withoutComment(const std::string &line)
{
	const std::size_t semicolon = line.find(';');
	return semicolon == std::string::npos ? line : line.substr(0, semicolon);
}

struct Vec3
{
	float x = 0.0f, y = 0.0f, z = 0.0f;
};

Vec3 coordOf(const ParticleBlock &block, const char *key)
{
	Vec3 v;
	if (const std::string *value = block.find(key))
		std::sscanf(value->c_str(), " X:%f Y:%f Z:%f", &v.x, &v.y, &v.z);
	return v;
}

float realOf(const ParticleBlock &block, const char *key)
{
	float v = 0.0f;
	if (const std::string *value = block.find(key))
		std::sscanf(value->c_str(), " %f", &v);
	return v;
}

// A circle of the given radius around c in the plane of the axes u and v (0 = x, 1 = y, 2 = z).
ParticleOutline circle(const Vec3 &c, float radius, int u, int v)
{
	constexpr int SEGMENTS = 32;
	ParticleOutline out;
	out.closed = true;
	for (int i = 0; i < SEGMENTS; ++i)
	{
		const float angle = 6.2831853f * static_cast<float>(i) / SEGMENTS;
		float p[3] = {c.x, c.y, c.z};
		p[u] += radius * std::cos(angle);
		p[v] += radius * std::sin(angle);
		out.points.insert(out.points.end(), {p[0], p[1], p[2]});
	}
	return out;
}

ParticleOutline segment(const Vec3 &a, const Vec3 &b)
{
	ParticleOutline out;
	out.points = {a.x, a.y, a.z, b.x, b.y, b.z};
	return out;
}

} // namespace

const std::string *ParticleBlock::find(const std::string &key) const
{
	for (const ParticleField &f : fields)
		if (f.key == key)
			return &f.value;
	return nullptr;
}

void ParticleBlock::set(const std::string &key, const std::string &value)
{
	for (ParticleField &f : fields)
	{
		if (f.key == key)
		{
			f.value = value;
			return;
		}
	}
	fields.push_back({key, value});
}

std::optional<ParticleBlock> parseParticleBlock(const std::string &text)
{
	std::istringstream in(text);
	std::string line;
	std::optional<ParticleBlock> block;
	while (std::getline(in, line))
	{
		const std::string clean = trim(withoutComment(line));
		if (clean.empty())
			continue;
		if (!block)
		{
			std::istringstream words(clean);
			std::string head;
			std::string name;
			words >> head >> name;
			if (sameWord(head, "ParticleSystem") && !name.empty())
			{
				block = ParticleBlock{};
				block->name = name;
			}
			continue;
		}
		if (sameWord(clean, "End"))
			return block;
		const std::size_t eq = clean.find('=');
		if (eq == std::string::npos)
			continue;
		block->fields.push_back({trim(clean.substr(0, eq)), trim(clean.substr(eq + 1))});
	}
	return std::nullopt;
}

std::vector<std::string> particleBlockNames(const std::string &text)
{
	std::vector<std::string> names;
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line))
	{
		std::istringstream words(withoutComment(line));
		std::string word;
		std::string name;
		if (words >> word && sameWord(word, "ParticleSystem") && words >> name && std::find(names.begin(), names.end(), name) == names.end())
			names.push_back(name);
	}
	return names;
}

std::string writeParticleBlock(const ParticleBlock &block)
{
	std::string out = "ParticleSystem " + block.name + "\n";
	for (const ParticleField &f : block.fields)
		out += "  " + f.key + " = " + f.value + "\n";
	out += "End\n";
	return out;
}

std::optional<ColorKey> parseColorKey(const std::string &value)
{
	ColorKey key;
	if (std::sscanf(value.c_str(), " R:%d G:%d B:%d %d", &key.r, &key.g, &key.b, &key.frame) != 4)
		return std::nullopt;
	return key;
}

std::string writeColorKey(const ColorKey &key)
{
	return "R:" + std::to_string(key.r) + " G:" + std::to_string(key.g) + " B:" + std::to_string(key.b) + " " + std::to_string(key.frame);
}

std::optional<AlphaKey> parseAlphaKey(const std::string &value)
{
	AlphaKey key;
	if (std::sscanf(value.c_str(), " %f %f %d", &key.low, &key.high, &key.frame) != 3)
		return std::nullopt;
	return key;
}

std::string writeAlphaKey(const AlphaKey &key)
{
	char text[64];
	std::snprintf(text, sizeof(text), "%.2f %.2f %d", static_cast<double>(key.low), static_cast<double>(key.high), key.frame);
	return text;
}

std::vector<std::string> particleChoices(const std::string &key)
{
	// The name lists of the game (GameClient/ParticleSys.h) without their NONE entry.
	if (key == "Priority")
		return {"WEAPON_EXPLOSION", "SCORCHMARK", "DUST_TRAIL", "BUILDUP", "DEBRIS_TRAIL", "UNIT_DAMAGE_FX", "DEATH_EXPLOSION",
			"SEMI_CONSTANT", "CONSTANT", "WEAPON_TRAIL", "AREA_EFFECT", "CRITICAL", "ALWAYS_RENDER"};
	if (key == "Shader")
		return {"ADDITIVE", "ALPHA", "ALPHA_TEST", "MULTIPLY"};
	if (key == "Type")
		return {"PARTICLE", "DRAWABLE", "STREAK", "VOLUME_PARTICLE", "SMUDGE"};
	if (key == "VelocityType")
		return {"ORTHO", "SPHERICAL", "HEMISPHERICAL", "CYLINDRICAL", "OUTWARD"};
	if (key == "VolumeType")
		return {"POINT", "LINE", "BOX", "SPHERE", "CYLINDER"};
	if (key == "WindMotion")
		return {"Unused", "PingPong", "Circular"};
	if (key.rfind("Is", 0) == 0)
		return {"Yes", "No"};
	return {};
}

std::vector<ParticleOutline> emissionOutline(const ParticleBlock &block, float x, float y, float z)
{
	std::vector<ParticleOutline> out;
	const std::string *type = block.find("VolumeType");
	const Vec3 c{x, y, z};
	const auto at = [&c](float dx, float dy, float dz) { return Vec3{c.x + dx, c.y + dy, c.z + dz}; };
	if (!type || sameWord(*type, "POINT"))
	{
		constexpr float CROSS = 3.0f;
		out.push_back(segment(at(-CROSS, 0, 0), at(CROSS, 0, 0)));
		out.push_back(segment(at(0, -CROSS, 0), at(0, CROSS, 0)));
		out.push_back(segment(at(0, 0, -CROSS), at(0, 0, CROSS)));
	}
	else if (sameWord(*type, "LINE"))
	{
		const Vec3 a = coordOf(block, "VolLineStart");
		const Vec3 b = coordOf(block, "VolLineEnd");
		out.push_back(segment(at(a.x, a.y, a.z), at(b.x, b.y, b.z)));
	}
	else if (sameWord(*type, "BOX"))
	{
		const Vec3 h = coordOf(block, "VolBoxHalfSize");
		for (const float dz : {-h.z, h.z})
		{
			ParticleOutline ring;
			ring.closed = true;
			for (const Vec3 &p : {at(-h.x, -h.y, dz), at(h.x, -h.y, dz), at(h.x, h.y, dz), at(-h.x, h.y, dz)})
				ring.points.insert(ring.points.end(), {p.x, p.y, p.z});
			out.push_back(ring);
		}
		for (const float sx : {-1.0f, 1.0f})
			for (const float sy : {-1.0f, 1.0f})
				out.push_back(segment(at(sx * h.x, sy * h.y, -h.z), at(sx * h.x, sy * h.y, h.z)));
	}
	else if (sameWord(*type, "SPHERE"))
	{
		const float r = realOf(block, "VolSphereRadius");
		out.push_back(circle(c, r, 0, 1));
		out.push_back(circle(c, r, 0, 2));
		out.push_back(circle(c, r, 1, 2));
	}
	else if (sameWord(*type, "CYLINDER"))
	{
		const float r = realOf(block, "VolCylinderRadius");
		const float half = realOf(block, "VolCylinderLength") * 0.5f;
		out.push_back(circle(at(0, 0, -half), r, 0, 1));
		out.push_back(circle(at(0, 0, half), r, 0, 1));
		for (const auto &[dx, dy] : {std::pair{r, 0.0f}, std::pair{-r, 0.0f}, std::pair{0.0f, r}, std::pair{0.0f, -r}})
			out.push_back(segment(at(dx, dy, -half), at(dx, dy, half)));
	}
	return out;
}

std::vector<AlphaKey> usedAlphaKeys(const ParticleBlock &block)
{
	std::vector<AlphaKey> keys;
	for (int i = 0; i < PARTICLE_KEYS; ++i)
	{
		const std::string *value = block.find("Alpha" + std::to_string(i + 1));
		const std::optional<AlphaKey> key = value ? parseAlphaKey(*value) : std::nullopt;
		if (!key || (i > 0 && key->frame == 0))
			break;
		keys.push_back(*key);
	}
	return keys;
}

std::vector<ColorKey> usedColorKeys(const ParticleBlock &block)
{
	std::vector<ColorKey> keys;
	for (int i = 0; i < PARTICLE_KEYS; ++i)
	{
		const std::string *value = block.find("Color" + std::to_string(i + 1));
		const std::optional<ColorKey> key = value ? parseColorKey(*value) : std::nullopt;
		if (!key || (i > 0 && key->frame == 0))
			break;
		keys.push_back(*key);
	}
	return keys;
}

namespace
{

// First key stays; the rest sorted and strictly increasing from 1 on; at most eight.
template <class Key>
void orderKeys(std::vector<Key> &keys)
{
	if (keys.size() > static_cast<std::size_t>(PARTICLE_KEYS))
		keys.resize(static_cast<std::size_t>(PARTICLE_KEYS));
	if (keys.size() < 2)
		return;
	std::stable_sort(keys.begin() + 1, keys.end(), [](const Key &a, const Key &b) { return a.frame < b.frame; });
	int previous = std::max(0, keys[0].frame);
	for (std::size_t i = 1; i < keys.size(); ++i)
	{
		keys[i].frame = std::max(keys[i].frame, previous + 1);
		previous = keys[i].frame;
	}
}

} // namespace

void setAlphaKeys(ParticleBlock &block, std::vector<AlphaKey> keys)
{
	orderKeys(keys);
	for (int i = 0; i < PARTICLE_KEYS; ++i)
		block.set("Alpha" + std::to_string(i + 1), i < static_cast<int>(keys.size()) ? writeAlphaKey(keys[static_cast<std::size_t>(i)]) : writeAlphaKey(AlphaKey{}));
}

void setColorKeys(ParticleBlock &block, std::vector<ColorKey> keys)
{
	orderKeys(keys);
	for (int i = 0; i < PARTICLE_KEYS; ++i)
		block.set("Color" + std::to_string(i + 1), i < static_cast<int>(keys.size()) ? writeColorKey(keys[static_cast<std::size_t>(i)]) : writeColorKey(ColorKey{}));
}

ColorKey colorAt(const std::vector<ColorKey> &keys, int frame)
{
	if (keys.empty())
		return ColorKey{};
	if (frame <= keys.front().frame)
		return ColorKey{keys.front().r, keys.front().g, keys.front().b, frame};
	for (std::size_t i = 1; i < keys.size(); ++i)
	{
		const ColorKey &a = keys[i - 1];
		const ColorKey &b = keys[i];
		if (frame <= b.frame)
		{
			const float f = b.frame > a.frame ? static_cast<float>(frame - a.frame) / static_cast<float>(b.frame - a.frame) : 1.0f;
			const auto mix = [f](int x, int y) { return static_cast<int>(std::lround(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * f)); };
			return ColorKey{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), frame};
		}
	}
	return ColorKey{keys.back().r, keys.back().g, keys.back().b, frame};
}

std::vector<ParticleChange> compareParticleBlocks(const ParticleBlock &before, const ParticleBlock &after)
{
	std::vector<ParticleChange> changes;
	for (const ParticleField &f : after.fields)
	{
		const std::string *old = before.find(f.key);
		if (!old || *old != f.value)
			changes.push_back(ParticleChange{f.key, old ? *old : std::string(), f.value});
	}
	for (const ParticleField &f : before.fields)
		if (!after.find(f.key))
			changes.push_back(ParticleChange{f.key, f.value, std::string()});
	return changes;
}

} // namespace MapEditorCore
