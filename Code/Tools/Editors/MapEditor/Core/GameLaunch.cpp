/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// GameLaunch.cpp -- starting the game with a map.

#include "Core/GameLaunch.h"

#include <algorithm>
#include <cctype>

namespace MapEditorCore
{

namespace
{

bool sameNoCase(const std::string &a, const std::string &b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
	});
}

} // namespace

std::optional<std::string> gameFileArgument(const std::string &mapPath)
{
	const std::size_t fileStart = mapPath.find_last_of("\\/");
	if (fileStart == std::string::npos || fileStart == 0)
		return std::nullopt;
	const std::string file = mapPath.substr(fileStart + 1);
	if (file.size() <= 4 || !sameNoCase(file.substr(file.size() - 4), ".map"))
		return std::nullopt;
	const std::string base = file.substr(0, file.size() - 4);
	const std::size_t folderStart = mapPath.find_last_of("\\/", fileStart - 1);
	const std::string folder = mapPath.substr(folderStart == std::string::npos ? 0 : folderStart + 1,
		fileStart - (folderStart == std::string::npos ? 0 : folderStart + 1));
	if (!sameNoCase(folder, base) || folderStart == std::string::npos)
		return std::nullopt;
	// The game wants backslashes.
	std::string out = mapPath.substr(0, folderStart + 1) + file;
	std::replace(out.begin(), out.end(), '/', '\\');
	return out;
}

std::vector<std::string> gameArguments(const std::string &fileArgument)
{
	return {"-win", "-file", fileArgument};
}

} // namespace MapEditorCore
