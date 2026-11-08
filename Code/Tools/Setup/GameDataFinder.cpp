// Copyright (C) 2026 GZH Neo
// Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
// (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.

#include "GameDataFinder.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace gzhsetup
{

namespace
{

std::string lower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

std::string fileNameUtf8(const fs::path& path)
{
	const std::u8string name = path.filename().u8string();
	return std::string(name.begin(), name.end());
}

// Regular files directly in the folder: lower-case name -> size.
std::map<std::string, std::uintmax_t> listFiles(const fs::path& folder)
{
	std::map<std::string, std::uintmax_t> files;
	std::error_code ec;
	for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
	{
		std::error_code fileEc;
		if (!it->is_regular_file(fileEc))
			continue;
		const std::uintmax_t size = it->file_size(fileEc);
		files[lower(fileNameUtf8(it->path()))] = fileEc ? 0 : size;
	}
	return files;
}

bool endsWith(const std::string& text, const std::string& suffix)
{
	return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// The language archive pair is Audio<Language>.big (Generals) or Audio<Language>ZH.big (Zero Hour).
std::string findLanguage(const std::map<std::string, std::uintmax_t>& files, Game game)
{
	const std::string suffix = game == Game::ZeroHour ? "zh.big" : ".big";
	for (const auto& [name, size] : files)
	{
		(void)size;
		if (name.rfind("audio", 0) != 0 || !endsWith(name, suffix))
			continue;
		const std::string language = name.substr(5, name.size() - 5 - suffix.size());
		if (!language.empty())
			return language;
	}
	return {};
}

struct KnownRelease
{
	const char* archive;
	std::uintmax_t archiveSize;
	const char* patchArchive;
	std::uintmax_t patchSize;
	const char* edition;
};

// Generals 1.08: the retail discs (English and uncut German share these archives) and the German digital release
// (EA app, Steam; the censored "Generaele"). The English digital release has the archives of the English disc plus
// the patch archives of the digital releases (see findEdition). Zero Hour 1.04 is the same on disc and digital.
constexpr KnownRelease KNOWN_RELEASES[] = {
	{ "ini.big", 7607479, "patch.big", 1462590, "1.08 retail" },
	{ "ini.big", 7612095, "patch.big", 1311980, "1.08 digital" },
	{ "inizh.big", 18764687, "patchzh.big", 118822, "1.04" },
};

std::string findEdition(const std::map<std::string, std::uintmax_t>& files)
{
	for (const KnownRelease& release : KNOWN_RELEASES)
	{
		const auto archive = files.find(release.archive);
		const auto patch = files.find(release.patchArchive);
		if (archive != files.end() && patch != files.end() && archive->second == release.archiveSize
			&& patch->second == release.patchSize)
		{
			// Only the digital releases ship PatchWindow.big. The ZH_Generals folder of a digital release lacks it
			// and therefore shows as the retail archives it contains.
			if (std::string(release.edition) == "1.08 retail" && files.count("patchwindow.big"))
				return "1.08 digital";
			return release.edition;
		}
	}
	return {};
}

} // namespace

std::set<std::string> eaArchiveNames(Game game, const std::string& language)
{
	// The patch archives of the digital release (custom mission menu entry) count as EA's; the copy decides
	// separately what to leave out.
	std::set<std::string> names = { "patchdata.big", "patchini.big", "patchwindow.big" };
	if (game == Game::Generals)
	{
		for (const char* name : { "ini", "w3d", "textures", "terrain", "window", "maps", "music", "patch", "gensec",
				 "shaders", "audio", "speech" })
		{
			names.insert(std::string(name) + ".big");
		}
		if (!language.empty())
		{
			names.insert(language + ".big");
			names.insert("audio" + language + ".big");
			names.insert("speech" + language + ".big");
		}
	}
	else
	{
		for (const char* name : { "inizh", "w3dzh", "textureszh", "terrainzh", "windowzh", "mapszh", "musiczh", "music",
				 "patchzh", "genseczh", "shaderszh", "audiozh", "speechzh" })
		{
			names.insert(std::string(name) + ".big");
		}
		if (!language.empty())
		{
			names.insert(language + "zh.big");
			names.insert("audio" + language + "zh.big");
			names.insert("speech" + language + "zh.big");
			names.insert("w3d" + language + "zh.big");
		}
	}
	return names;
}

namespace
{

std::string folderKey(const fs::path& folder)
{
	std::error_code ec;
	fs::path canonical = fs::weakly_canonical(folder, ec);
	if (ec)
		canonical = folder.lexically_normal();
	std::u8string text = canonical.u8string();
	while (!text.empty() && (text.back() == u8'\\' || text.back() == u8'/'))
		text.pop_back();
	return lower(std::string(text.begin(), text.end()));
}

void visit(const fs::path& folder, const std::string& origin, int depth, std::set<std::string>& seen,
	std::vector<GameFolder>& found)
{
	std::error_code ec;
	if (!fs::is_directory(folder, ec))
		return;
	if (!seen.insert(folderKey(folder)).second)
		return;
	// Report the folder as the file system spells it (registry values may be lower case with forward slashes).
	fs::path real = fs::weakly_canonical(folder, ec);
	if (ec)
		real = folder;
	real.make_preferred();
	if (std::optional<GameFolder> game = classifyFolder(real, origin))
		found.push_back(std::move(*game));
	if (depth >= 2)
		return;
	std::vector<fs::path> subfolders;
	for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
	{
		std::error_code dirEc;
		if (it->is_directory(dirEc) && !it->is_symlink(dirEc))
			subfolders.push_back(it->path());
	}
	std::sort(subfolders.begin(), subfolders.end());
	for (const fs::path& sub : subfolders)
		visit(sub, origin, depth + 1, seen, found);
}

// Quoted strings of a Valve KeyValues text, with \\ and \" unescaped.
std::vector<std::string> vdfTokens(const std::string& text)
{
	std::vector<std::string> tokens;
	for (size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] != '"')
			continue;
		std::string token;
		for (++i; i < text.size() && text[i] != '"'; ++i)
		{
			if (text[i] == '\\' && i + 1 < text.size())
				++i;
			token += text[i];
		}
		tokens.push_back(std::move(token));
	}
	return tokens;
}

} // namespace

const char* gameName(Game game)
{
	return game == Game::ZeroHour ? "Zero Hour" : "Generals";
}

std::optional<GameFolder> classifyFolder(const fs::path& folder, const std::string& origin)
{
	const std::map<std::string, std::uintmax_t> files = listFiles(folder);
	GameFolder result;
	if (files.count("inizh.big") && files.count("w3dzh.big"))
		result.game = Game::ZeroHour;
	else if (files.count("ini.big") && files.count("w3d.big"))
		result.game = Game::Generals;
	else
		return std::nullopt;

	result.path = folder;
	result.origin = origin;
	result.language = findLanguage(files, result.game);
	result.edition = findEdition(files);
	result.baseFolderOfZeroHour = result.game == Game::Generals && lower(fileNameUtf8(folder)) == "zh_generals";

	const std::set<std::string> known = eaArchiveNames(result.game, result.language);
	std::error_code ec;
	for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
	{
		const std::string name = fileNameUtf8(it->path());
		const std::string key = lower(name);
		if (endsWith(key, ".big") && !known.count(key))
			result.foreignArchives.push_back(name);
	}
	std::sort(result.foreignArchives.begin(), result.foreignArchives.end());
	return result;
}

std::vector<GameFolder> findGameFolders(const std::vector<Candidate>& candidates)
{
	std::vector<GameFolder> found;
	std::set<std::string> seen;
	for (const Candidate& candidate : candidates)
		visit(candidate.path, candidate.origin, 0, seen, found);
	return found;
}

Detection chooseGameFolders(std::vector<GameFolder> found)
{
	Detection detection;
	auto pick = [&found](Game game, auto score) {
		std::optional<GameFolder> best;
		for (const GameFolder& folder : found)
		{
			if (folder.game == game && (!best || score(folder) > score(*best)))
				best = folder;
		}
		return best;
	};
	detection.zeroHour = pick(Game::ZeroHour, [](const GameFolder& folder) { return folder.edition.empty() ? 0 : 1; });
	const std::string zeroHourOrigin = detection.zeroHour ? detection.zeroHour->origin : std::string();
	detection.generals = pick(Game::Generals, [&zeroHourOrigin](const GameFolder& folder) {
		return (folder.edition.empty() ? 0 : 4) + (folder.origin == zeroHourOrigin ? 2 : 0) + (folder.baseFolderOfZeroHour ? 0 : 1);
	});
	detection.found = std::move(found);
	return detection;
}

std::vector<fs::path> parseSteamLibraryFolders(const std::string& vdfText)
{
	std::vector<fs::path> folders;
	const std::vector<std::string> tokens = vdfTokens(vdfText);
	for (size_t i = 0; i + 1 < tokens.size(); ++i)
	{
		if (lower(tokens[i]) == "path")
			folders.emplace_back(std::u8string(tokens[i + 1].begin(), tokens[i + 1].end()));
	}
	return folders;
}

std::string parseSteamInstallDir(const std::string& acfText)
{
	const std::vector<std::string> tokens = vdfTokens(acfText);
	for (size_t i = 0; i + 1 < tokens.size(); ++i)
	{
		if (lower(tokens[i]) == "installdir")
			return tokens[i + 1];
	}
	return {};
}

} // namespace gzhsetup
