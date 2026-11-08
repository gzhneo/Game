// Copyright (C) 2026 GZH Neo
// Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
// (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.

#include "DataCopy.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>

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

std::string utf8(const fs::path& path)
{
	const std::u8string text = path.u8string();
	return std::string(text.begin(), text.end());
}

fs::path fromUtf8(const std::string& text)
{
	return fs::path(std::u8string(text.begin(), text.end()));
}

std::uint32_t readBigEndian(const unsigned char* bytes)
{
	return (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) | (std::uint32_t(bytes[2]) << 8) | bytes[3];
}

std::string readFile(const fs::path& path)
{
	std::ifstream file(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void writeFile(const fs::path& path, const std::string& content)
{
	fs::create_directories(path.parent_path());
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file.write(content.data(), static_cast<std::streamsize>(content.size()));
	if (!file)
		throw std::runtime_error("cannot write " + utf8(path));
}

// Writes a generated file unless it already has this content. Returns true if written.
bool updateFile(const fs::path& path, const std::string& content)
{
	std::error_code ec;
	if (fs::is_regular_file(path, ec) && readFile(path) == content)
		return false;
	writeFile(path, content);
	return true;
}

// Same size and the same modification time in whole seconds: the file is already copied.
bool sameFile(const fs::path& source, const fs::path& dest)
{
	std::error_code ec;
	if (!fs::is_regular_file(dest, ec))
		return false;
	if (fs::file_size(source, ec) != fs::file_size(dest, ec) || ec)
		return false;
	using std::chrono::duration_cast;
	using std::chrono::seconds;
	const auto sourceTime = duration_cast<seconds>(fs::last_write_time(source, ec).time_since_epoch());
	const auto destTime = duration_cast<seconds>(fs::last_write_time(dest, ec).time_since_epoch());
	return !ec && sourceTime == destTime;
}

// Copies with the modification time of the source; files already in place count for the progress as well.
void copyFile(const fs::path& source, const fs::path& dest, CopyReport& report)
{
	const std::uintmax_t size = fs::file_size(source);
	if (!sameFile(source, dest))
	{
		fs::create_directories(dest.parent_path());
		fs::copy_file(source, dest, fs::copy_options::overwrite_existing);
		fs::last_write_time(dest, fs::last_write_time(source));
		report.bytesCopied += size;
		++report.filesCopied;
	}
	report.bytesDone += size;
	if (report.progress && !report.progress(report.bytesDone))
		throw CopyCancelled();
}

void removeExcluded(const fs::path& path, const CopyLog& log)
{
	std::error_code ec;
	if (fs::is_regular_file(path, ec) && fs::remove(path, ec))
		log("removed (excluded): " + utf8(path));
}

// Archives of the digital releases that belong to a menu entry ("custom mission") the source code does not
// know. PatchWindow.big would load before Window.big or WindowZH.big and cover the real menus.
const std::set<std::string> EXCLUDED_ARCHIVES = { "patchdata.big", "patchwindow.big", "patchini.big" };

// Folders below Data that EA ships. Loose files there take precedence over the archives, so files a mod or a
// mod launcher put elsewhere below Data (for example loose INI files) stay out of the copy. Data\INI only holds
// the outdated INIZH.big of Zero Hour.
const std::set<std::string> EA_DATA_FOLDERS = { "cursors", "movies", "scripts", "waterplane", "english", "german",
	"german2", "french", "spanish", "italian", "korean", "chinese", "brazilian", "polish" };

// Small files next to the archives; the game itself only reads Install_Final.bmp (start picture).
const std::vector<const char*> GENERALS_EXTRA_FILES = { "Generals.dat", "Install_Final.bmp", "00000000.016", "00000000.256" };
const std::vector<const char*> ZERO_HOUR_EXTRA_FILES = { "Generals.dat", "Install_Final.bmp", "00000000.016", "00000000.256",
	"00000001.016", "00000001.256", "langdata.dat" };

// A file to copy: its place in the installation and below the game folder of GZH Neo.
struct CopyJob
{
	fs::path source;
	fs::path relative;
};

// What the copy takes from an installation: EA's archives (not mods, not the excluded ones), the small files
// and EA's folders below Data. Notes what is missing or left out if a log is given.
std::vector<CopyJob> collectCopyJobs(const GameFolder& source, const CopyLog* log)
{
	std::vector<CopyJob> jobs;
	const std::set<std::string> known = eaArchiveNames(source.game, source.language);
	std::vector<fs::path> files;
	std::error_code ec;
	for (fs::directory_iterator it(source.path, ec), end; !ec && it != end; it.increment(ec))
	{
		if (it->is_regular_file())
			files.push_back(it->path());
	}
	std::sort(files.begin(), files.end());
	for (const fs::path& file : files)
	{
		const std::string key = lower(utf8(file.filename()));
		if (known.count(key) && !EXCLUDED_ARCHIVES.count(key))
			jobs.push_back({ file, file.filename() });
	}

	for (const char* name : source.game == Game::ZeroHour ? ZERO_HOUR_EXTRA_FILES : GENERALS_EXTRA_FILES)
	{
		if (fs::is_regular_file(source.path / name, ec))
			jobs.push_back({ source.path / name, name });
		else if (log)
			(*log)(std::string("missing in the installation (optional): ") + name);
	}

	const fs::path data = source.path / "Data";
	if (!fs::is_directory(data, ec))
	{
		if (log)
			(*log)("no Data folder in the installation (retail discs keep everything in the archives)");
		return jobs;
	}
	std::vector<fs::path> folders;
	for (fs::directory_iterator it(data, ec), end; !ec && it != end; it.increment(ec))
	{
		if (it->is_directory() && EA_DATA_FOLDERS.count(lower(utf8(it->path().filename()))))
			folders.push_back(it->path());
		else if (log)
			(*log)("left out: Data\\" + utf8(it->path().filename()));
	}
	std::sort(folders.begin(), folders.end());
	for (const fs::path& folder : folders)
	{
		for (fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
		{
			if (it->is_regular_file())
				jobs.push_back({ it->path(), it->path().lexically_relative(source.path) });
		}
	}
	if (ec)
		throw std::runtime_error("cannot read " + utf8(data) + ": " + ec.message());
	return jobs;
}

void copyGameFiles(const GameFolder& source, const fs::path& dest, CopyReport& report, const CopyLog& log)
{
	fs::create_directories(dest);
	const std::vector<CopyJob> jobs = collectCopyJobs(source, &log);
	for (const CopyJob& job : jobs)
		copyFile(job.source, dest / job.relative, report);
	for (const std::string& name : EXCLUDED_ARCHIVES)
		removeExcluded(dest / name, log);
	log(std::to_string(jobs.size()) + " files of " + gameName(source.game) + " checked");
}

// --- Key bindings of Zero Hour for Generals ----------------------------------------------------------------
// The game loads Data\<language>\CommandMap.ini and then Data\INI\CommandMap.ini, both overwriting, one entry
// per command. So that Generals gets exactly Zero Hour's bindings, every path that either game has gets a
// loose file with Zero Hour's content; no Generals file from an archive can then cover them.

const char* const COMMANDMAP_INI = "ini";

// Folder (lower case) -> content of the effective CommandMap.ini per path, in the game's order: loose files
// below Data, then the archives of the folders alphabetically, the first hit wins.
std::map<std::string, std::string> commandMapFiles(const fs::path& dataRoot, const std::vector<fs::path>& bigFolders)
{
	std::map<std::string, std::string> found;
	std::error_code ec;
	for (fs::directory_iterator it(dataRoot / "Data", ec), end; !ec && it != end; it.increment(ec))
	{
		const fs::path file = it->path() / "CommandMap.ini";
		std::error_code fileEc;
		if (fs::is_regular_file(file, fileEc))
			found[lower(utf8(it->path().filename()))] = readFile(file);
	}
	for (const fs::path& folder : bigFolders)
	{
		std::vector<std::string> archives;
		std::error_code dirEc;
		for (fs::directory_iterator it(folder, dirEc), end; !dirEc && it != end; it.increment(dirEc))
		{
			const std::string name = utf8(it->path().filename());
			const std::string key = lower(name);
			if (it->is_regular_file() && key.size() > 4 && key.compare(key.size() - 4, 4, ".big") == 0
				&& !EXCLUDED_ARCHIVES.count(key))
			{
				archives.push_back(name);
			}
		}
		std::sort(archives.begin(), archives.end());
		for (const std::string& name : archives)
		{
			BigArchive archive;
			if (!archive.open(folder / fromUtf8(name)))
				continue;
			for (const std::string& entry : archive.names())
			{
				// data\<folder>\commandmap.ini
				const std::string prefix = "data\\";
				const std::string suffix = "\\commandmap.ini";
				if (entry.size() <= prefix.size() + suffix.size() || entry.compare(0, prefix.size(), prefix) != 0
					|| entry.compare(entry.size() - suffix.size(), suffix.size(), suffix) != 0)
				{
					continue;
				}
				const std::string sub = entry.substr(prefix.size(), entry.size() - prefix.size() - suffix.size());
				if (sub.find('\\') == std::string::npos && !found.count(sub))
					found[sub] = archive.read(entry);
			}
		}
	}
	return found;
}

std::map<std::string, std::string> zeroHourCommandMapPlan(const std::map<std::string, std::string>& generals,
	const std::map<std::string, std::string>& zeroHour)
{
	if (zeroHour.empty())
		throw std::runtime_error("no CommandMap.ini in the Zero Hour data");
	std::vector<std::string> zhLanguages;
	std::vector<std::string> generalsLanguages;
	for (const auto& entry : zeroHour)
	{
		if (entry.first != COMMANDMAP_INI)
			zhLanguages.push_back(entry.first);
	}
	for (const auto& entry : generals)
	{
		if (entry.first != COMMANDMAP_INI)
			generalsLanguages.push_back(entry.first);
	}
	std::set<std::string> targets;
	for (const auto& entry : generals)
		targets.insert(entry.first);
	for (const auto& entry : zeroHour)
		targets.insert(entry.first);

	std::map<std::string, std::string> plan;
	for (const std::string& target : targets)
	{
		if (zeroHour.count(target))
			plan[target] = zeroHour.at(target);
		else if (target != COMMANDMAP_INI)
		{
			// A language Zero Hour does not have: there Zero Hour only loads its Data\INI file.
			if (zeroHour.count(COMMANDMAP_INI))
				plan[target] = zeroHour.at(COMMANDMAP_INI);
			else if (zhLanguages.size() == 1)
				plan[target] = zeroHour.at(zhLanguages[0]);
			else
				throw std::runtime_error("language " + target + ": no unique Zero Hour key bindings");
		}
		else
		{
			// Generals has Data\INI\CommandMap.ini, Zero Hour does not: load the language file twice.
			std::string language;
			if (generalsLanguages.size() == 1 && zeroHour.count(generalsLanguages[0]))
				language = generalsLanguages[0];
			else if (zhLanguages.size() == 1)
				language = zhLanguages[0];
			else
				throw std::runtime_error("Data\\INI\\CommandMap.ini: no unique Zero Hour language file");
			plan[target] = zeroHour.at(language);
		}
	}
	return plan;
}

// Spelling of EA's folders (Data\INI, Data\English, ...); the archives store lower-case names.
fs::path commandMapPath(const fs::path& dest, const std::string& folder)
{
	std::string name = folder == COMMANDMAP_INI ? std::string("INI") : folder;
	if (folder != COMMANDMAP_INI && !name.empty())
		name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
	return dest / "Data" / fromUtf8(name) / "CommandMap.ini";
}

void applyZeroHourCommandMap(const fs::path& dest, const fs::path& zeroHourSource, CopyReport& report, const CopyLog& log)
{
	const auto generals = commandMapFiles(dest, { dest });
	const auto zeroHour = commandMapFiles(zeroHourSource, { zeroHourSource, zeroHourSource / "ZH_Generals" });
	for (const auto& [folder, content] : zeroHourCommandMapPlan(generals, zeroHour))
	{
		if (updateFile(commandMapPath(dest, folder), content))
			log("key bindings of Zero Hour written: " + utf8(commandMapPath(dest, folder).lexically_relative(dest)));
	}
	report.lines.push_back("key bindings of Zero Hour for Generals");
}

// Layouts Generals shows like Zero Hour, from Zero Hour's window archive as loose files. Their texts that
// Generals lacks are among the project texts.
void applyZeroHourLayouts(const fs::path& dest, const fs::path& zeroHourSource, CopyReport& report, const CopyLog& log)
{
	BigArchive windows;
	if (!windows.open(zeroHourSource / "WindowZH.big"))
		throw std::runtime_error("WindowZH.big missing in " + utf8(zeroHourSource));
	const std::string entry = "Window\\Menus\\OptionsMenu.wnd";
	const fs::path out = dest / "Window" / "Menus" / "OptionsMenu.wnd";
	if (updateFile(out, windows.read(entry)))
		log("layout of Zero Hour written: Window\\Menus\\OptionsMenu.wnd");
	report.lines.push_back("options menu layout of Zero Hour for Generals");
}

void applyGeneralsFixes(const fs::path& dest, CopyReport& report, const CopyLog& log)
{
	BigArchive ini;
	if (!ini.open(dest / "INI.big"))
		throw std::runtime_error("cannot read " + utf8(dest / "INI.big"));
	const std::string entry = "Data\\INI\\Object\\FactionUnit.ini";
	if (updateFile(dest / "Data" / "INI" / "Object" / "FactionUnit.ini", fixRocketBuggyDebris(ini.read(entry))))
		log("data fix written: Data\\INI\\Object\\FactionUnit.ini");
	report.lines.push_back("FactionUnit.ini: GLARocketBuggyFullDebris without turret lines");
}

std::string jsonString(const std::string& text)
{
	std::string out = "\"";
	for (const char c : text)
	{
		if (c == '"' || c == '\\')
			out += '\\';
		out += c;
	}
	return out + "\"";
}

void writeManifest(const fs::path& dest, const GameFolder& source)
{
	std::string json = "{\n";
	json += "  \"format\": 1,\n";
	json += "  \"game\": " + jsonString(gameName(source.game)) + ",\n";
	json += "  \"source\": " + jsonString(utf8(source.path)) + ",\n";
	json += "  \"origin\": " + jsonString(source.origin) + ",\n";
	json += "  \"language\": " + jsonString(source.language) + ",\n";
	json += "  \"edition\": " + jsonString(source.edition) + ",\n";
	json += "  \"fixesVersion\": " + std::to_string(DATA_FIXES_VERSION) + "\n";
	json += "}\n";
	writeFile(dest / MANIFEST_NAME, json);
}

// One line of a block: the text and its line ending.
struct Line
{
	std::string text;
	std::string ending;
};

std::vector<Line> splitLines(const std::string& text)
{
	std::vector<Line> lines;
	size_t start = 0;
	while (start < text.size())
	{
		const size_t newline = text.find('\n', start);
		if (newline == std::string::npos)
		{
			lines.push_back({ text.substr(start), "" });
			break;
		}
		size_t end = newline;
		if (end > start && text[end - 1] == '\r')
			--end;
		lines.push_back({ text.substr(start, end - start), text.substr(end, newline + 1 - end) });
		start = newline + 1;
	}
	return lines;
}

bool isBlank(char c)
{
	return c == ' ' || c == '\t';
}

// "<blanks>Key<blanks>=<blanks>Value<blanks>" followed by a line ending.
bool isSetting(const Line& line, const std::string& key, const std::string& value)
{
	if (line.ending.empty())
		return false;
	const std::string& t = line.text;
	size_t i = 0;
	while (i < t.size() && isBlank(t[i]))
		++i;
	if (t.compare(i, key.size(), key) != 0)
		return false;
	i += key.size();
	while (i < t.size() && isBlank(t[i]))
		++i;
	if (i >= t.size() || t[i] != '=')
		return false;
	++i;
	while (i < t.size() && isBlank(t[i]))
		++i;
	if (t.compare(i, value.size(), value) != 0)
		return false;
	i += value.size();
	while (i < t.size() && isBlank(t[i]))
		++i;
	return i == t.size();
}

bool isWord(const Line& line, const std::string& word)
{
	if (line.ending.empty())
		return false;
	size_t i = 0;
	while (i < line.text.size() && isBlank(line.text[i]))
		++i;
	size_t j = line.text.size();
	while (j > i && isBlank(line.text[j - 1]))
		--j;
	return line.text.substr(i, j - i) == word;
}

} // namespace

bool BigArchive::open(const fs::path& path)
{
	m_path = path;
	m_entries.clear();
	std::ifstream file(path, std::ios::binary);
	unsigned char header[16];
	if (!file.read(reinterpret_cast<char*>(header), sizeof(header)))
		return false;
	if (std::string(reinterpret_cast<char*>(header), 4) != "BIGF" && std::string(reinterpret_cast<char*>(header), 4) != "BIG4")
		return false;
	const std::uint32_t count = readBigEndian(header + 8);
	const std::uint32_t bodyOffset = readBigEndian(header + 12);
	if (bodyOffset < sizeof(header))
		return false;
	std::string directory(bodyOffset - sizeof(header), '\0');
	if (!file.read(directory.data(), static_cast<std::streamsize>(directory.size())))
		return false;
	size_t pos = 0;
	for (std::uint32_t i = 0; i < count; ++i)
	{
		if (pos + 8 > directory.size())
			return false;
		Entry entry;
		entry.offset = readBigEndian(reinterpret_cast<const unsigned char*>(directory.data() + pos));
		entry.size = readBigEndian(reinterpret_cast<const unsigned char*>(directory.data() + pos + 4));
		pos += 8;
		const size_t end = directory.find('\0', pos);
		if (end == std::string::npos)
			return false;
		m_entries[lower(directory.substr(pos, end - pos))] = entry;
		pos = end + 1;
	}
	return true;
}

bool BigArchive::contains(const std::string& name) const
{
	return m_entries.count(lower(name)) != 0;
}

std::string BigArchive::read(const std::string& name) const
{
	const auto it = m_entries.find(lower(name));
	if (it == m_entries.end())
		throw std::runtime_error(name + " missing in " + utf8(m_path));
	std::ifstream file(m_path, std::ios::binary);
	std::string data(it->second.size, '\0');
	file.seekg(it->second.offset);
	if (!file.read(data.data(), static_cast<std::streamsize>(data.size())))
		throw std::runtime_error("cannot read " + name + " from " + utf8(m_path));
	return data;
}

std::vector<std::string> BigArchive::names() const
{
	std::vector<std::string> result;
	for (const auto& entry : m_entries)
		result.push_back(entry.first);
	return result;
}

std::string fixRocketBuggyDebris(const std::string& ini)
{
	// The object block runs from its header line to the first line that is exactly "End".
	const std::string header = "Object GLARocketBuggyFullDebris";
	size_t blockStart = std::string::npos;
	for (size_t pos = ini.find(header); pos != std::string::npos; pos = ini.find(header, pos + 1))
	{
		const bool lineStart = pos == 0 || ini[pos - 1] == '\n';
		const size_t after = pos + header.size();
		const bool lineEnd = ini.compare(after, 1, "\n") == 0 || ini.compare(after, 2, "\r\n") == 0;
		if (lineStart && lineEnd)
		{
			blockStart = pos;
			break;
		}
	}
	if (blockStart == std::string::npos)
		throw std::runtime_error("Object GLARocketBuggyFullDebris not found");

	std::vector<Line> lines = splitLines(ini.substr(blockStart));
	size_t blockLines = 0;
	for (size_t i = 1; i < lines.size(); ++i)
	{
		if (lines[i].text == "End" && !lines[i].ending.empty())
		{
			blockLines = i + 1;
			break;
		}
	}
	if (blockLines == 0)
		throw std::runtime_error("end of GLARocketBuggyFullDebris not found");
	size_t blockLength = 0;
	for (size_t i = 0; i < blockLines; ++i)
		blockLength += lines[i].text.size() + lines[i].ending.size();
	lines.resize(blockLines);

	int shown = 0;
	int hidden = 0;
	std::vector<Line> kept;
	for (const Line& line : lines)
	{
		if (isSetting(line, "ShowSubObject", "Turret"))
			++shown;
		else if (isSetting(line, "HideSubObject", "TurretUp01"))
			++hidden;
		else
			kept.push_back(line);
	}
	int upgradeStates = 0;
	std::vector<Line> result;
	for (size_t i = 0; i < kept.size(); ++i)
	{
		if (i + 3 < kept.size() && isSetting(kept[i], "ConditionState", "WEAPONSET_PLAYER_UPGRADE")
			&& isSetting(kept[i + 1], "ShowSubObject", "TurretUp01") && isSetting(kept[i + 2], "HideSubObject", "Turret")
			&& isWord(kept[i + 3], "End"))
		{
			++upgradeStates;
			i += 3;
			if (i + 1 < kept.size() && isWord(kept[i + 1], ""))
				++i;
			continue;
		}
		result.push_back(kept[i]);
	}
	if (shown != 1 || hidden != 1 || upgradeStates != 1)
	{
		throw std::runtime_error("unexpected data version: " + std::to_string(shown) + "/" + std::to_string(hidden) + "/"
			+ std::to_string(upgradeStates) + " instead of 1/1/1");
	}
	std::string block;
	for (const Line& line : result)
		block += line.text + line.ending;
	return ini.substr(0, blockStart) + block + ini.substr(blockStart + blockLength);
}

void copyGeneralsData(const GameFolder& generals, const fs::path& zeroHourSource, const fs::path& dest,
	CopyReport& report, const CopyLog& log)
{
	copyGameFiles(generals, dest, report, log);
	applyGeneralsFixes(dest, report, log);
	applyZeroHourCommandMap(dest, zeroHourSource, report, log);
	applyZeroHourLayouts(dest, zeroHourSource, report, log);
}

void copyZeroHourData(const GameFolder& zeroHour, const fs::path& generalsDest, const fs::path& dest,
	CopyReport& report, const CopyLog& log)
{
	copyGameFiles(zeroHour, dest, report, log);
	// Outdated second INIZH.big that the game deletes at start (EA's patch 1.01), left by an earlier copy.
	removeExcluded(dest / "Data" / "INI" / "INIZH.big", log);

	// The Generals archives Zero Hour loads in addition: hard links to our own Generals copy, a copy if the
	// file system cannot link.
	const fs::path base = dest / "ZH_Generals";
	fs::create_directories(base);
	std::error_code ec;
	for (fs::directory_iterator it(generalsDest, ec), end; !ec && it != end; it.increment(ec))
	{
		const std::string key = lower(utf8(it->path().filename()));
		if (!it->is_regular_file() || key.size() < 4 || key.compare(key.size() - 4, 4, ".big") != 0)
			continue;
		const fs::path link = base / it->path().filename();
		std::error_code linkEc;
		if (fs::exists(link, linkEc) && fs::equivalent(link, it->path(), linkEc))
			continue;
		fs::remove(link, linkEc);
		fs::create_hard_link(it->path(), link, linkEc);
		if (linkEc)
			copyFile(it->path(), link, report);
		else
			++report.filesLinked;
	}
	for (const std::string& name : EXCLUDED_ARCHIVES)
		removeExcluded(base / name, log);
}

CopyEstimate estimateCopy(const GameFolder& generals, const GameFolder& zeroHour, const fs::path& target)
{
	CopyEstimate estimate;
	const std::pair<const GameFolder*, fs::path> games[] = { { &generals, target / "Generals" }, { &zeroHour, target / "ZeroHour" } };
	for (const auto& [game, dest] : games)
	{
		for (const CopyJob& job : collectCopyJobs(*game, nullptr))
		{
			std::error_code ec;
			const std::uintmax_t size = fs::file_size(job.source, ec);
			if (ec)
				continue;
			estimate.totalBytes += size;
			if (!sameFile(job.source, dest / job.relative))
				estimate.missingBytes += size;
		}
	}
	return estimate;
}

CopyReport setUpGameData(const GameFolder& generals, const GameFolder& zeroHour, const fs::path& target, const CopyLog& log,
	const CopyProgress& progress)
{
	CopyReport report;
	report.progress = progress;
	const fs::path generalsDest = target / "Generals";
	const fs::path zeroHourDest = target / "ZeroHour";
	// The manifest marks a complete copy: removed first, written last, so a cancelled run does not count.
	std::error_code ec;
	fs::remove(generalsDest / MANIFEST_NAME, ec);
	fs::remove(zeroHourDest / MANIFEST_NAME, ec);
	copyGeneralsData(generals, zeroHour.path, generalsDest, report, log);
	copyZeroHourData(zeroHour, generalsDest, zeroHourDest, report, log);
	writeManifest(generalsDest, generals);
	writeManifest(zeroHourDest, zeroHour);
	return report;
}

} // namespace gzhsetup
