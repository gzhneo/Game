// Copyright (C) 2026 GZH Neo
// Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
// (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.

// Tests of the detection on generated folder trees that copy the layouts of the real releases (archive names
// and the sizes that identify a release; the files themselves are empty).

#include "DataCopy.h"
#include "GameDataFinder.h"

#include <cstdio>
#include <fstream>
#include <random>

namespace fs = std::filesystem;

namespace gzhsetup
{

namespace
{

int g_failures = 0;

void check(bool condition, const char* what)
{
	std::printf("%s  %s\n", condition ? "ok  " : "FAIL", what);
	if (!condition)
		++g_failures;
}

void makeFile(const fs::path& path, std::uintmax_t size = 1)
{
	fs::create_directories(path.parent_path());
	{
		std::ofstream file(path, std::ios::binary);
	}
	fs::resize_file(path, size);
}

void makeGenerals(const fs::path& folder, const char* language, bool digital)
{
	makeFile(folder / "INI.big", digital ? 7612095 : 7607479);
	makeFile(folder / "Patch.big", digital ? 1311980 : 1462590);
	for (const char* name : { "W3D.big", "Textures.big", "Terrain.big", "Window.big", "maps.big", "Music.big",
			 "gensec.big", "shaders.big", "Audio.big", "Speech.big" })
	{
		makeFile(folder / name);
	}
	makeFile(folder / (std::string(language) + ".big"));
	makeFile(folder / ("Audio" + std::string(language) + ".big"));
	makeFile(folder / ("Speech" + std::string(language) + ".big"));
	if (digital)
	{
		makeFile(folder / "PatchINI.big");
	}
}

void makeZeroHour(const fs::path& folder, const char* language)
{
	makeFile(folder / "INIZH.big", 18764687);
	makeFile(folder / "PatchZH.big", 118822);
	for (const char* name : { "W3DZH.big", "TexturesZH.big", "TerrainZH.big", "WindowZH.big", "MapsZH.big",
			 "MusicZH.big", "Music.big", "gensecZH.big", "ShadersZH.big", "AudioZH.big", "SpeechZH.big" })
	{
		makeFile(folder / name);
	}
	makeFile(folder / (std::string(language) + "ZH.big"));
	makeFile(folder / ("Audio" + std::string(language) + "ZH.big"));
	makeFile(folder / ("Speech" + std::string(language) + "ZH.big"));
	makeFile(folder / ("W3D" + std::string(language) + "ZH.big"));
	// The outdated second INIZH.big below Data\INI must not make Data\INI a game folder.
	makeFile(folder / "Data" / "INI" / "INIZH.big");
}

void testSteamFiles()
{
	const std::string vdf = "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"C:\\\\Program Files (x86)\\\\Steam\"\n"
		"\t\t\"apps\"\n\t\t{\n\t\t\t\"2732960\"\t\t\"1\"\n\t\t}\n\t}\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"D:\\\\SteamLibrary\"\n\t}\n}\n";
	const std::vector<fs::path> libraries = parseSteamLibraryFolders(vdf);
	check(libraries.size() == 2, "libraryfolders.vdf: two libraries");
	check(libraries.size() == 2 && libraries[0] == fs::path("C:\\Program Files (x86)\\Steam")
			&& libraries[1] == fs::path("D:\\SteamLibrary"),
		"libraryfolders.vdf: paths with unescaped backslashes");
	const std::string acf = "\"AppState\"\n{\n\t\"appid\"\t\t\"2732960\"\n\t\"installdir\"\t\t\"Command & Conquer Generals - Zero Hour\"\n}\n";
	check(parseSteamInstallDir(acf) == "Command & Conquer Generals - Zero Hour", "appmanifest: installdir");
	check(parseSteamInstallDir("\"AppState\" { }").empty(), "appmanifest without installdir");
}

// Steam: Zero Hour in the app folder, Generals only in its ZH_Generals subfolder; a mod left files there.
void testSteamLayout(const fs::path& root)
{
	const fs::path app = root / "steamapps" / "common" / "Command & Conquer Generals - Zero Hour";
	makeZeroHour(app, "German");
	makeGenerals(app / "ZH_Generals", "German2", true);
	makeFile(app / "!Shw_ini.gib");
	makeFile(app / "ShwMod.big");

	const Detection d = chooseGameFolders(findGameFolders({ { app, "Steam" }, { app, "Steam" } }));
	check(d.found.size() == 2, "Steam: two game folders, the doubled candidate counted once");
	check(d.zeroHour && d.zeroHour->language == "german" && d.zeroHour->edition == "1.04", "Steam: Zero Hour german 1.04");
	check(d.zeroHour && d.zeroHour->foreignArchives == std::vector<std::string>{ "ShwMod.big" },
		"Steam: mod archive reported, .gib ignored");
	check(d.generals && d.generals->baseFolderOfZeroHour && d.generals->language == "german2"
			&& d.generals->edition == "1.08 digital",
		"Steam: Generals from ZH_Generals, german2, 1.08 digital");
	check(d.generals && d.generals->foreignArchives.empty(), "Steam: no foreign archives for Generals");
}

// EA app: both games below one folder, Zero Hour again with ZH_Generals.
void testEaAppLayout(const fs::path& root)
{
	makeGenerals(root / "Command and Conquer Generals", "German2", true);
	makeZeroHour(root / "Command and Conquer Generals Zero Hour", "German");
	makeGenerals(root / "Command and Conquer Generals Zero Hour" / "ZH_Generals", "German2", true);

	const Detection d = chooseGameFolders(findGameFolders({ { root, "EA app" } }));
	check(d.found.size() == 3, "EA app: three game folders");
	check(d.generals && !d.generals->baseFolderOfZeroHour
			&& d.generals->path.filename() == fs::path("Command and Conquer Generals"),
		"EA app: Generals from its own folder before ZH_Generals");
	check(d.zeroHour && d.zeroHour->origin == "EA app", "EA app: Zero Hour found");
}

// Steam and EA app on one machine (layouts from the two tests above): both games from the same installation.
void testTwoInstallations(const fs::path& root)
{
	const fs::path steamApp = root / "steam" / "steamapps" / "common" / "Command & Conquer Generals - Zero Hour";
	const Detection d = chooseGameFolders(findGameFolders({ { steamApp, "Steam" }, { root / "ea", "EA app" } }));
	check(d.found.size() == 5, "Steam and EA app: five game folders");
	check(d.zeroHour && d.generals && d.zeroHour->origin == "Steam" && d.generals->origin == "Steam",
		"Steam and EA app: both games from Steam, not mixed");
}

// Retail discs with the English patches, each game in a folder of its own.
void testRetailLayout(const fs::path& root)
{
	makeGenerals(root / "Generals", "English", false);
	makeZeroHour(root / "Zero Hour", "English");
	const Detection d = chooseGameFolders(findGameFolders({ { root / "Generals", "registry" }, { root / "Zero Hour", "registry" } }));
	check(d.generals && d.generals->edition == "1.08 retail" && d.generals->language == "english", "retail: Generals english 1.08");
	check(d.zeroHour && d.zeroHour->language == "english", "retail: Zero Hour english");

	// English digital release: the archives of the English disc plus the patch archives of the digital releases.
	makeGenerals(root / "EnglishDigital", "English", false);
	for (const char* name : { "PatchData.big", "PatchWindow.big", "PatchINI.big" })
		makeFile(root / "EnglishDigital" / name);
	const std::optional<GameFolder> digital = classifyFolder(root / "EnglishDigital", "EA app");
	check(digital && digital->edition == "1.08 digital" && digital->language == "english" && digital->foreignArchives.empty(),
		"English digital release: 1.08 digital, patch archives not foreign");
}

void testIncomplete(const fs::path& root)
{
	makeZeroHour(root / "OnlyZH", "German");
	Detection d = chooseGameFolders(findGameFolders({ { root / "OnlyZH", "manual" } }));
	check(d.zeroHour && !d.generals, "Zero Hour without Generals: Generals missing");

	makeGenerals(root / "Unknown", "English", false);
	fs::resize_file(root / "Unknown" / "INI.big", 1234);
	makeGenerals(root / "Known", "English", false);
	d = chooseGameFolders(findGameFolders({ { root / "Unknown", "manual" }, { root / "Known", "manual" } }));
	check(d.found.size() == 2 && d.generals && d.generals->path.filename() == fs::path("Known"),
		"known release preferred over unknown sizes");
	check(!classifyFolder(root / "nonexistent", "manual"), "missing folder: nothing found");
}

// The data fix on a copy of the block as EA ships it (CRLF), next to objects that must stay untouched.
void testRocketBuggyFix()
{
	const std::string before = "Object Other\r\n  ShowSubObject = Turret\r\nEnd\r\n\r\n"
		"Object GLARocketBuggyFullDebris\r\n"
		"  Draw = W3DModelDraw ModuleTag_01\r\n"
		"    DefaultConditionState\r\n"
		"      Model = UVRockBug_d1\r\n"
		"      ShowSubObject = Turret\r\n"
		"      HideSubObject = TurretUp01\r\n"
		"    End\r\n"
		"    ConditionState = WEAPONSET_PLAYER_UPGRADE\r\n"
		"      ShowSubObject = TurretUp01\r\n"
		"      HideSubObject = Turret\r\n"
		"    End\r\n"
		"\r\n"
		"  End\r\n"
		"End\r\n"
		"Object Next\r\nEnd\r\n";
	const std::string expected = "Object Other\r\n  ShowSubObject = Turret\r\nEnd\r\n\r\n"
		"Object GLARocketBuggyFullDebris\r\n"
		"  Draw = W3DModelDraw ModuleTag_01\r\n"
		"    DefaultConditionState\r\n"
		"      Model = UVRockBug_d1\r\n"
		"    End\r\n"
		"  End\r\n"
		"End\r\n"
		"Object Next\r\nEnd\r\n";
	check(fixRocketBuggyDebris(before) == expected, "data fix: turret lines and upgrade state removed, rest unchanged");
	bool threw = false;
	try
	{
		fixRocketBuggyDebris(expected);
	}
	catch (const std::exception&)
	{
		threw = true;
	}
	check(threw, "data fix: already fixed or other data version is refused");
}

void writeBig(const fs::path& path, const std::vector<std::pair<std::string, std::string>>& entries)
{
	auto bigEndian = [](std::string& out, std::uint32_t value) {
		for (int shift = 24; shift >= 0; shift -= 8)
			out += static_cast<char>((value >> shift) & 0xff);
	};
	std::uint32_t directorySize = 0;
	for (const auto& entry : entries)
		directorySize += 8 + static_cast<std::uint32_t>(entry.first.size()) + 1;
	std::uint32_t offset = 16 + directorySize;
	std::string head = "BIGF";
	std::string directory;
	std::string body;
	for (const auto& entry : entries)
	{
		bigEndian(directory, offset);
		bigEndian(directory, static_cast<std::uint32_t>(entry.second.size()));
		directory += entry.first;
		directory += '\0';
		body += entry.second;
		offset += static_cast<std::uint32_t>(entry.second.size());
	}
	bigEndian(head, offset);
	bigEndian(head, static_cast<std::uint32_t>(entries.size()));
	bigEndian(head, 16 + directorySize);
	fs::create_directories(path.parent_path());
	std::ofstream file(path, std::ios::binary);
	file << head << directory << body;
}

void testBigArchive(const fs::path& root)
{
	writeBig(root / "Test.big", { { "Data\\INI\\CommandMap.ini", "keys" }, { "Window\\Menus\\OptionsMenu.wnd", "layout" } });
	BigArchive archive;
	check(archive.open(root / "Test.big"), "BIG: archive opened");
	check(archive.contains("data\\ini\\commandmap.ini") && archive.read("DATA\\INI\\COMMANDMAP.INI") == "keys",
		"BIG: names without regard to case, content read");
	check(archive.read("Window\\Menus\\OptionsMenu.wnd") == "layout", "BIG: second entry");
	makeFile(root / "NotABig.big", 20);
	check(!BigArchive().open(root / "NotABig.big"), "BIG: other file refused");
}

} // namespace

int runSelfTest()
{
	g_failures = 0;
	std::random_device random;
	const fs::path root = fs::temp_directory_path() / ("gzhneo_setup_selftest_" + std::to_string(random()));
	std::error_code ec;
	try
	{
		testSteamFiles();
		testSteamLayout(root / "steam");
		testEaAppLayout(root / "ea");
		testTwoInstallations(root);
		testRetailLayout(root / "retail");
		testIncomplete(root / "incomplete");
		testRocketBuggyFix();
		testBigArchive(root / "big");
	}
	catch (const std::exception& e)
	{
		std::printf("FAIL  exception: %s\n", e.what());
		++g_failures;
	}
	fs::remove_all(root, ec);
	std::printf("%s: %d failures\n", g_failures ? "FAILED" : "PASSED", g_failures);
	return g_failures;
}

} // namespace gzhsetup
