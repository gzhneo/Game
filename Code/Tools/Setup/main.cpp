// Copyright (C) 2026 GZH Neo
// Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
// (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.

// GZHNeoEinrichtung: sets up the game data of GZH Neo from the player's own installation of Generals and
// Zero Hour. Without arguments it opens the setup window for the package it belongs to (its own folder).
//   GZHNeoEinrichtung [--ziel <folder>]                                  setup window
//   GZHNeoEinrichtung --erkennen [--quelle <folder>]...                  list the installations found and the choice
//   GZHNeoEinrichtung --kopieren --ziel <folder> [--quelle <folder>]...  copy into <folder>/Generals and /ZeroHour
//   GZHNeoEinrichtung --selftest                                         built-in tests
// Exit code: 0 done, 2 a game is missing or the window was cancelled, 1 usage or copy error.
// A Windows program, so the window comes without a console; the command line modes write to the console of
// the caller or to redirected output.

#include "DataCopy.h"
#include "GameDataFinder.h"
#include "SetupWindow.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace fs = std::filesystem;
using namespace gzhsetup;

namespace
{

std::string utf8(const fs::path& path)
{
	const std::u8string text = path.u8string();
	return std::string(text.begin(), text.end());
}

void printFolder(const GameFolder& folder)
{
	std::printf("  %-9s  %-8s  %-12s  %-14s  %s%s\n", gameName(folder.game),
		folder.language.empty() ? "?" : folder.language.c_str(),
		folder.edition.empty() ? "unknown" : folder.edition.c_str(), folder.origin.c_str(), utf8(folder.path).c_str(),
		folder.baseFolderOfZeroHour ? "  (Generals part of Zero Hour)" : "");
	if (!folder.foreignArchives.empty())
	{
		std::printf("             other archives, not copied:");
		for (const std::string& name : folder.foreignArchives)
			std::printf(" %s", name.c_str());
		std::printf("\n");
	}
}

Detection detect(const std::vector<Candidate>& manual)
{
	std::vector<Candidate> candidates = manual;
	if (manual.empty())
		candidates = collectSystemCandidates();
	const Detection detection = chooseGameFolders(findGameFolders(candidates));

	std::printf("Searched %zu folders, found %zu game folders:\n", candidates.size(), detection.found.size());
	for (const GameFolder& folder : detection.found)
		printFolder(folder);
	std::printf("Choice:\n");
	for (const std::optional<GameFolder>* chosen : { &detection.generals, &detection.zeroHour })
	{
		if (*chosen)
			printFolder(**chosen);
	}
	if (!detection.generals)
		std::printf("  Generals not found.\n");
	if (!detection.zeroHour)
		std::printf("  Zero Hour not found.\n");
	for (const std::optional<GameFolder>* chosen : { &detection.generals, &detection.zeroHour })
	{
		if (*chosen && (*chosen)->edition.empty())
			std::printf("Warning: %s is no known release (Generals 1.08 or Zero Hour 1.04 expected).\n", gameName((*chosen)->game));
	}
	return detection;
}

int copy(const std::vector<Candidate>& manual, const fs::path& target)
{
	const Detection detection = detect(manual);
	if (!detection.generals || !detection.zeroHour)
		return 2;
	std::printf("Copying into %s\n", utf8(target).c_str());
	try
	{
		const CopyReport report = setUpGameData(*detection.generals, *detection.zeroHour, target,
			[](const std::string& line) { std::printf("  %s\n", line.c_str()); });
		std::printf("Done: %d files copied (%llu MB), %d linked.\n", report.filesCopied,
			static_cast<unsigned long long>(report.bytesCopied >> 20), report.filesLinked);
		return 0;
	}
	catch (const std::exception& e)
	{
		std::printf("Error: %s\n", e.what());
		return 1;
	}
}

// Output of the command line modes: redirected handles stay, otherwise the caller's console.
void attachConsole()
{
	const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
	if (output != nullptr && output != INVALID_HANDLE_VALUE)
		return;
	if (!AttachConsole(ATTACH_PARENT_PROCESS))
		return;
	FILE* stream = nullptr;
	freopen_s(&stream, "CONOUT$", "w", stdout);
	freopen_s(&stream, "CONOUT$", "w", stderr);
	SetConsoleOutputCP(CP_UTF8);
}

fs::path ownFolder()
{
	std::wstring path(MAX_PATH, L'\0');
	for (;;)
	{
		const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
		if (length < path.size())
		{
			path.resize(length);
			break;
		}
		path.resize(path.size() * 2);
	}
	return fs::path(path).parent_path();
}

int run(int argc, wchar_t** argv)
{
	std::vector<Candidate> manual;
	bool selfTest = false;
	bool copyData = false;
	bool detectOnly = false;
	fs::path target;
	for (int i = 1; i < argc; ++i)
	{
		if (std::wcscmp(argv[i], L"--selftest") == 0)
			selfTest = true;
		else if (std::wcscmp(argv[i], L"--erkennen") == 0)
			detectOnly = true;
		else if (std::wcscmp(argv[i], L"--kopieren") == 0)
			copyData = true;
		else if (std::wcscmp(argv[i], L"--ziel") == 0 && i + 1 < argc)
			target = fs::path(argv[++i]);
		else if (std::wcscmp(argv[i], L"--quelle") == 0 && i + 1 < argc)
			manual.push_back({ fs::path(argv[++i]), "manual" });
		else
		{
			attachConsole();
			std::fprintf(stderr, "usage: GZHNeoEinrichtung [--ziel <folder>] | [--erkennen | --kopieren --ziel <folder>] [--quelle <folder>]... | --selftest\n");
			return 1;
		}
	}
	if (!selfTest && !copyData && !detectOnly && manual.empty())
		return runSetupWindow(target.empty() ? ownFolder() : target);
	attachConsole();
	if (selfTest)
		return runSelfTest() == 0 ? 0 : 1;
	if (copyData)
	{
		if (target.empty())
		{
			std::fprintf(stderr, "--kopieren needs --ziel <folder>\n");
			return 1;
		}
		return copy(manual, target);
	}
	const Detection detection = detect(manual);
	return detection.generals && detection.zeroHour ? 0 : 2;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	int argc = 0;
	wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	const int result = run(argc, argv);
	LocalFree(argv);
	std::fflush(stdout);
	return result;
}
