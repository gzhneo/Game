/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WaterLook.cpp -- the WaterTransparency block of a map.ini.

#include "Core/WaterLook.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <vector>

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

std::vector<std::string> splitLines(const std::string &text)
{
	std::vector<std::string> lines;
	std::string line;
	for (const char c : text)
	{
		if (c == '\n')
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			lines.push_back(line);
			line.clear();
		}
		else
		{
			line += c;
		}
	}
	if (!line.empty())
		lines.push_back(line);
	return lines;
}

// The first word of a line without its comment, lower case.
std::string keyOf(const std::string &line)
{
	const std::string code = trim(line.substr(0, line.find(';')));
	const std::size_t end = code.find_first_of(" \t=");
	return lower(code.substr(0, end));
}

std::string valueOf(const std::string &line)
{
	const std::string code = line.substr(0, line.find(';'));
	const std::size_t eq = code.find('=');
	return eq == std::string::npos ? std::string() : trim(code.substr(eq + 1));
}

void readColor(const std::string &value, std::array<int, 3> &color)
{
	int r = 0, g = 0, b = 0;
	if (std::sscanf(value.c_str(), " R:%d G:%d B:%d", &r, &g, &b) == 3)
		color = {std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255)};
}

std::string colorText(const std::array<int, 3> &c)
{
	return "R:" + std::to_string(c[0]) + " G:" + std::to_string(c[1]) + " B:" + std::to_string(c[2]);
}

std::string realText(float v)
{
	char text[32];
	std::snprintf(text, sizeof(text), "%.2f", static_cast<double>(v));
	return text;
}

} // namespace

WaterLook readWaterLook(const std::string &mapIni, const WaterLook &base)
{
	WaterLook look = base;
	bool inBlock = false;
	for (const std::string &line : splitLines(mapIni))
	{
		const std::string key = keyOf(line);
		if (!inBlock)
		{
			inBlock = key == "watertransparency";
			continue;
		}
		if (key == "end")
		{
			inBlock = false;
			continue;
		}
		const std::string value = valueOf(line);
		float f = 0.0f;
		const bool number = std::sscanf(value.c_str(), " %f", &f) == 1;
		if (key == "transparentwaterdepth" && number)
			look.transparentDepth = f;
		else if (key == "transparentwaterminopacity" && number)
			look.minOpacity = f;
		else if (key == "standingwatercolor")
			readColor(value, look.standingColor);
		else if (key == "additiveblending")
			look.additive = lower(value) == "yes";
		else if (key == "turbiditydepth" && number)
			look.turbidityDepth = f;
		else if (key == "turbiditycolor")
			readColor(value, look.turbidityColor);
		else if (key == "refractionstrength" && number)
			look.refraction = f;
		else if (key == "reflectionstrength" && number)
			look.reflection = f;
	}
	return look;
}

std::string writeWaterLook(const std::string &mapIni, const WaterLook &look, const WaterLook &base)
{
	const std::string nl = mapIni.find("\r\n") != std::string::npos ? "\r\n" : "\n";
	// Every line outside the WaterTransparency blocks, as it was.
	std::vector<std::string> kept;
	bool inBlock = false;
	for (const std::string &line : splitLines(mapIni))
	{
		const std::string key = keyOf(line);
		if (!inBlock && key == "watertransparency")
		{
			inBlock = true;
			continue;
		}
		if (inBlock)
		{
			inBlock = key != "end";
			continue;
		}
		kept.push_back(line);
	}
	while (!kept.empty() && trim(kept.back()).empty())
		kept.pop_back();
	std::vector<std::string> block;
	const auto add = [&block](bool differs, const std::string &key, const std::string &value) {
		if (differs)
			block.push_back("  " + key + " = " + value);
	};
	const auto near = [](float a, float b) { return std::abs(a - b) < 0.005f; };
	add(!near(look.transparentDepth, base.transparentDepth), "TransparentWaterDepth", realText(look.transparentDepth));
	add(!near(look.minOpacity, base.minOpacity), "TransparentWaterMinOpacity", realText(look.minOpacity));
	add(look.standingColor != base.standingColor, "StandingWaterColor", colorText(look.standingColor));
	add(look.additive != base.additive, "AdditiveBlending", look.additive ? "Yes" : "No");
	add(!near(look.turbidityDepth, base.turbidityDepth), "TurbidityDepth", realText(look.turbidityDepth));
	add(look.turbidityColor != base.turbidityColor, "TurbidityColor", colorText(look.turbidityColor));
	add(!near(look.refraction, base.refraction), "RefractionStrength", realText(look.refraction));
	add(!near(look.reflection, base.reflection), "ReflectionStrength", realText(look.reflection));
	std::string out;
	for (const std::string &line : kept)
		out += line + nl;
	if (!block.empty())
	{
		if (!out.empty())
			out += nl;
		out += "WaterTransparency" + nl;
		for (const std::string &line : block)
			out += line + nl;
		out += "End" + nl;
	}
	return out;
}

} // namespace MapEditorCore
