/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapDump.cpp -- readable text form of a map.

#include "Model/MapDump.h"

#include <algorithm>
#include <format>
#include <map>
#include <type_traits>

namespace MapModel
{

std::string toUtf8(const std::u16string &text)
{
	std::string out;
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		std::uint32_t c = text[i];
		if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF)
		{
			c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<std::uint32_t>(text[i + 1]) - 0xDC00);
			++i;
		}
		if (c < 0x80)
			out.push_back(static_cast<char>(c));
		else if (c < 0x800)
		{
			out.push_back(static_cast<char>(0xC0 | (c >> 6)));
			out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
		}
		else if (c < 0x10000)
		{
			out.push_back(static_cast<char>(0xE0 | (c >> 12)));
			out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
		}
		else
		{
			out.push_back(static_cast<char>(0xF0 | (c >> 18)));
			out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
		}
	}
	return out;
}

std::string formatValue(const PropertyValue &value)
{
	return std::visit([](const auto &v) -> std::string {
		using T = std::decay_t<decltype(v)>;
		if constexpr (std::is_same_v<T, bool>)
			return v ? "true" : "false";
		else if constexpr (std::is_same_v<T, std::int32_t>)
			return std::to_string(v);
		else if constexpr (std::is_same_v<T, float>)
			return std::format("{:g}", v);
		else if constexpr (std::is_same_v<T, std::string>)
			return "\"" + v + "\"";
		else
			return "L\"" + toUtf8(v) + "\"";
	}, value);
}

namespace
{

void dict(std::string &out, const PropertyMap &map, const char *indent)
{
	for (const PropertyMap::Pair &p : map.pairs())
		out += std::format("{}{} = {}\n", indent, p.key, formatValue(p.value));
}

void call(std::string &out, const char *what, const ScriptCall &c, const char *indent)
{
	out += std::format("{}{} {} ({})", indent, what, c.internalName.empty() ? "?" : c.internalName, c.type);
	for (const ScriptParameter &p : c.parameters)
	{
		if (p.type == PARAMETER_COORD3D)
			out += std::format(" [{},{},{}]", p.coord[0], p.coord[1], p.coord[2]);
		else if (!p.text.empty())
			out += std::format(" \"{}\"", p.text);
		else if (p.realValue != 0.0f)
			out += std::format(" {:g}", p.realValue);
		else
			out += std::format(" {}", p.intValue);
	}
	out += "\n";
}

void script(std::string &out, const Script &s, const char *indent)
{
	out += std::format("{}script \"{}\"{}{}{}\n", indent, s.name, s.isActive ? "" : " (inactive)",
		s.isSubroutine ? " (subroutine)" : "", s.isOneShot ? "" : " (repeats)");
	const std::string inner = std::string(indent) + "  ";
	for (std::size_t i = 0; i < s.conditions.size(); ++i)
	{
		out += std::format("{}{}\n", inner, i == 0 ? "if" : "or");
		for (const ScriptCall &c : s.conditions[i].conditions)
			call(out, "cond", c, (inner + "  ").c_str());
	}
	for (const ScriptCall &a : s.actions)
		call(out, "then", a, inner.c_str());
	for (const ScriptCall &a : s.falseActions)
		call(out, "else", a, inner.c_str());
}

} // namespace

std::string dumpMap(const Map &map, DumpDetail detail)
{
	const bool full = detail == DumpDetail::Full;
	std::string out;
	if (map.heightMap)
	{
		const HeightMap &hm = *map.heightMap;
		const auto [lo, hi] = std::minmax_element(hm.heights.begin(), hm.heights.end());
		out += std::format("height map: {} x {} cells, border {}, heights {}..{}\n", hm.width, hm.height, hm.borderSize,
			hm.heights.empty() ? 0 : *lo, hm.heights.empty() ? 0 : *hi);
		for (const Point2 &b : hm.boundaries)
			out += std::format("  boundary {} x {}\n", b.x, b.y);
	}
	if (map.blendTiles)
	{
		const BlendTiles &bt = *map.blendTiles;
		out += std::format("textures: {} classes, {} bitmap tiles, {} blended tiles, {} cliff entries, {} edge classes\n",
			bt.textureClasses.size(), bt.numBitmapTiles, bt.blendedTiles.size(), bt.cliffInfos.size(), bt.edgeTextureClasses.size());
		for (const TextureClass &tc : bt.textureClasses)
			out += std::format("  {} (tiles {}+{}, width {})\n", tc.name, tc.firstTile, tc.numTiles, tc.width);
	}
	if (map.worldInfo)
	{
		out += "world info:\n";
		dict(out, *map.worldInfo, "  ");
	}
	if (map.sides)
	{
		out += std::format("sides: {}, teams: {}\n", map.sides->sides.size(), map.sides->teams.size());
		for (std::size_t i = 0; i < map.sides->sides.size(); ++i)
		{
			const Side &side = map.sides->sides[i];
			std::size_t grouped = 0;
			for (const ScriptGroup &g : side.scripts.groups)
				grouped += g.scripts.size();
			out += std::format("  side {}: {}, build list {}, scripts {} (+{} in {} groups)\n", i,
				side.properties.getAscii("playerName").value_or("?"), side.buildList.size(),
				side.scripts.scripts.size(), grouped, side.scripts.groups.size());
			if (full)
			{
				dict(out, side.properties, "    ");
				for (const BuildListEntry &e : side.buildList)
					out += std::format("    build {} \"{}\" at {:g},{:g}\n", e.templateName, e.buildingName, e.x, e.y);
				for (const Script &s : side.scripts.scripts)
					script(out, s, "    ");
				for (const ScriptGroup &g : side.scripts.groups)
				{
					out += std::format("    group \"{}\"{}\n", g.name, g.isActive ? "" : " (inactive)");
					for (const Script &s : g.scripts)
						script(out, s, "      ");
				}
			}
		}
		if (full)
			for (const PropertyMap &team : map.sides->teams)
			{
				out += std::format("  team {}\n", team.getAscii("teamName").value_or("?"));
				dict(out, team, "    ");
			}
	}
	if (map.objects)
	{
		std::map<std::string, int> counts;
		for (const MapObject &o : *map.objects)
			++counts[o.templateName];
		out += std::format("objects: {} ({} templates)\n", map.objects->size(), counts.size());
		if (full)
		{
			for (const MapObject &o : *map.objects)
			{
				out += std::format("  {} at {:g},{:g},{:g} angle {:g} flags {:#x}\n", o.templateName, o.x, o.y, o.z, o.angle, static_cast<std::uint32_t>(o.flags));
				dict(out, o.properties, "    ");
			}
		}
		else
		{
			std::vector<std::pair<int, std::string>> top;
			for (const auto &[name, n] : counts)
				top.emplace_back(n, name);
			std::sort(top.begin(), top.end(), [](const auto &a, const auto &b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
			for (std::size_t i = 0; i < top.size() && i < 12; ++i)
				out += std::format("  {:5} {}\n", top[i].first, top[i].second);
		}
	}
	if (map.polygons)
	{
		const auto water = std::count_if(map.polygons->begin(), map.polygons->end(), [](const PolygonArea &p) { return p.isWater; });
		const auto rivers = std::count_if(map.polygons->begin(), map.polygons->end(), [](const PolygonArea &p) { return p.isRiver; });
		out += std::format("polygons: {} ({} water, {} rivers)\n", map.polygons->size(), water, rivers);
		if (full)
			for (const PolygonArea &p : *map.polygons)
				out += std::format("  {} \"{}\" id {} points {}{}{}\n", p.isWater ? "water" : "trigger", p.name, p.id, p.points.size(),
					p.isRiver ? " river" : "", p.layer.empty() ? "" : " layer " + p.layer);
	}
	if (map.lighting)
		out += std::format("lighting: time of day {}{}\n", map.lighting->timeOfDay,
			map.lighting->shadowColor ? std::format(", shadow color {:#010x}", *map.lighting->shadowColor) : std::string());
	if (map.waypointLinks)
		out += std::format("waypoint links: {}\n", map.waypointLinks->size());
	for (const StoredChunk &c : map.otherChunks)
		out += std::format("other chunk: {} v{} ({} bytes)\n", c.name, c.version, c.data.size());
	return out;
}

} // namespace MapModel
