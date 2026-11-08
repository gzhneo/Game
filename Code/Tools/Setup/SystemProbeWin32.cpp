// Copyright (C) 2026 GZH Neo
// Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
// (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.

// Candidate folders on Windows. The registry is only read here, to find installations; the game itself does
// not use it. Registry values are often stale or point one level too high or too low, so they only name
// folders to look into; GameDataFinder decides by content.

#include "GameDataFinder.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>

namespace fs = std::filesystem;

namespace gzhsetup
{

namespace
{

std::optional<std::wstring> registryString(HKEY root, const std::wstring& subKey, const wchar_t* valueName)
{
	DWORD bytes = 0;
	if (RegGetValueW(root, subKey.c_str(), valueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, nullptr, &bytes)
		!= ERROR_SUCCESS || bytes == 0)
	{
		return std::nullopt;
	}
	std::wstring value(bytes / sizeof(wchar_t), L'\0');
	if (RegGetValueW(root, subKey.c_str(), valueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, value.data(), &bytes)
		!= ERROR_SUCCESS)
	{
		return std::nullopt;
	}
	value.resize(wcsnlen(value.c_str(), value.size()));
	if (value.empty())
		return std::nullopt;
	return value;
}

std::string readTextFile(const fs::path& path)
{
	std::ifstream file(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool nameMentionsGame(const fs::path& folder)
{
	std::wstring name = folder.filename().wstring();
	std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
	return name.find(L"generals") != std::wstring::npos || name.find(L"zero hour") != std::wstring::npos;
}

// Subfolders of a parent whose name mentions one of the games.
void addGameSubfolders(const fs::path& parent, const std::string& origin, std::vector<Candidate>& out)
{
	std::error_code ec;
	for (fs::directory_iterator it(parent, ec), end; !ec && it != end; it.increment(ec))
	{
		std::error_code dirEc;
		if (it->is_directory(dirEc) && nameMentionsGame(it->path()))
			out.push_back({ it->path(), origin });
	}
}

void addSteam(std::vector<Candidate>& out)
{
	std::vector<fs::path> steamRoots;
	if (std::optional<std::wstring> path = registryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"))
		steamRoots.emplace_back(*path);
	if (std::optional<std::wstring> path = registryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath"))
		steamRoots.emplace_back(*path);

	std::vector<fs::path> libraries;
	for (const fs::path& steam : steamRoots)
	{
		libraries.push_back(steam);
		for (const fs::path& library : parseSteamLibraryFolders(readTextFile(steam / "steamapps" / "libraryfolders.vdf")))
			libraries.push_back(library);
	}
	for (const fs::path& library : libraries)
	{
		const fs::path steamapps = library / "steamapps";
		const std::string installDir =
			parseSteamInstallDir(readTextFile(steamapps / (std::string("appmanifest_") + STEAM_APP_ID + ".acf")));
		if (!installDir.empty())
			out.push_back({ steamapps / "common" / fs::path(std::u8string(installDir.begin(), installDir.end())), "Steam" });
		addGameSubfolders(steamapps / "common", "Steam", out);
	}
}

void addRegistry(std::vector<Candidate>& out)
{
	struct Entry
	{
		const wchar_t* key;
		const wchar_t* value;
		const char* origin;
	};
	// The EA app's own key first (it names the origin of folders found twice), then EA's keys of the retail
	// releases, which the EA app and Steam write as well.
	const Entry entries[] = {
		{ L"EA Games\\Command and Conquer Generals Zero Hour", L"Install Dir", "EA app" },
		{ L"EA Games\\Generals", L"Install Dir", "EA app" },
		{ L"Electronic Arts\\EA Games\\Generals", L"InstallPath", "registry" },
		{ L"Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour", L"InstallPath", "registry" },
		{ L"Electronic Arts\\EA Games\\ZeroHour", L"InstallPath", "registry" },
	};
	for (const wchar_t* prefix : { L"SOFTWARE\\WOW6432Node\\", L"SOFTWARE\\" })
	{
		for (const Entry& entry : entries)
		{
			if (std::optional<std::wstring> path = registryString(HKEY_LOCAL_MACHINE, std::wstring(prefix) + entry.key, entry.value))
				out.push_back({ fs::path(*path), entry.origin });
		}
	}
}

void addDefaultFolders(std::vector<Candidate>& out)
{
	for (const wchar_t* variable : { L"ProgramFiles", L"ProgramFiles(x86)" })
	{
		wchar_t buffer[MAX_PATH];
		const DWORD length = GetEnvironmentVariableW(variable, buffer, MAX_PATH);
		if (length == 0 || length >= MAX_PATH)
			continue;
		const fs::path programFiles(buffer);
		for (const wchar_t* publisher : { L"EA Games", L"Origin Games", L"Electronic Arts" })
			addGameSubfolders(programFiles / publisher, "default folder", out);
	}
}

} // namespace

std::vector<Candidate> collectSystemCandidates()
{
	std::vector<Candidate> candidates;
	addSteam(candidates);
	addRegistry(candidates);
	addDefaultFolders(candidates);
	return candidates;
}

} // namespace gzhsetup
