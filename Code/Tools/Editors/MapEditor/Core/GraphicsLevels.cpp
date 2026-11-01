/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// GraphicsLevels.cpp -- the detail levels of GameLODOverride.ini.

#include "Core/GraphicsLevels.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

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

std::string lower(std::string s)
{
	for (char &c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

// The entries of an effect list, separated by comma or plus as GlobalData::parseRenderEffects takes them.
std::vector<std::string> entries(const std::string &list)
{
	std::vector<std::string> out;
	std::string current;
	for (const char c : list)
	{
		if (c == ',' || c == '+')
		{
			if (!trim(current).empty())
				out.push_back(trim(current));
			current.clear();
		}
		else
		{
			current += c;
		}
	}
	if (!trim(current).empty())
		out.push_back(trim(current));
	return out;
}

std::string nameOf(const std::string &entry)
{
	return lower(trim(entry.substr(0, entry.find('='))));
}

} // namespace

std::vector<GraphicsLevel> parseGraphicsLevels(const std::string &text)
{
	std::vector<GraphicsLevel> levels;
	std::istringstream in(text);
	std::string line;
	bool inBlock = false;
	while (std::getline(in, line))
	{
		line = trim(line.substr(0, line.find(';')));
		if (line.empty())
			continue;
		const std::size_t eq = line.find('=');
		const std::string key = lower(trim(line.substr(0, eq)));
		const std::string value = eq == std::string::npos ? std::string() : trim(line.substr(eq + 1));
		if (key == "staticgamelod")
		{
			levels.push_back(GraphicsLevel{value, std::string(), 0, 0});
			inBlock = true;
		}
		else if (key == "end")
		{
			inBlock = false;
		}
		else if (inBlock && key == "d3d11effects")
		{
			levels.back().effects = value;
		}
		else if (inBlock && key == "waterquality")
		{
			levels.back().waterQuality = std::atoi(value.c_str());
		}
		else if (inBlock && key == "terrainanisotropy")
		{
			levels.back().terrainAnisotropy = std::atoi(value.c_str());
		}
	}
	return levels;
}

std::vector<std::string> effectNames(const std::string &list)
{
	std::vector<std::string> names;
	for (const std::string &entry : entries(list))
		names.push_back(nameOf(entry));
	return names;
}

std::string effectsWithout(const std::string &list, const std::vector<std::string> &names)
{
	std::string out;
	for (const std::string &entry : entries(list))
	{
		if (std::find(names.begin(), names.end(), nameOf(entry)) != names.end())
			continue;
		out += (out.empty() ? "" : ",") + entry;
	}
	return out;
}

} // namespace MapEditorCore
