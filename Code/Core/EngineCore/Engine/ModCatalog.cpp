/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ModCatalog.cpp -- see ModCatalog.h.

#include "Engine/ModCatalog.h"
#include "Engine/PositionalFile.h"
#include "Engine/UserDataFiles.h"
#include "Engine/Vfs.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <system_error>

namespace Engine
{

namespace
{

std::string lower(std::string_view text)
{
	std::string result(text);
	for (char &c : result)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return result;
}

std::string_view trim(std::string_view text)
{
	while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
		text.remove_prefix(1);
	while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
		text.remove_suffix(1);
	return text;
}

bool sameName(std::string_view a, std::string_view b)
{
	return a.size() == b.size() && lower(a) == lower(b);
}

void addError(ModInfo &info, const std::string &error)
{
	if (!info.manifestError.empty())
		info.manifestError += "; ";
	info.manifestError += error;
}

const std::size_t MAX_MANIFEST = 64 * 1024;

// mod.txt at the root of an archive, read through the same sources the file system uses.
bool readArchiveManifest(const std::string &path, bool zip, std::string &text)
{
	auto file = std::make_shared<PositionalFile>();
	if (!file->open(path))
		return false;
	Vfs::ReadAt readAt = [file](std::uint64_t offset, void *dest, std::size_t size) { return file->readAt(offset, dest, size); };
	if (zip)
	{
		std::unique_ptr<Vfs::ZipSource> source = Vfs::ZipSource::open(path, readAt, file->size());
		const Vfs::Entry *entry = source ? source->find("mod.txt") : nullptr;
		if (entry == nullptr || entry->size > MAX_MANIFEST)
			return false;
		std::vector<unsigned char> data;
		if (source->readEntry(*entry, data) != ZipArchiveError::None)
			return false;
		text.assign(data.begin(), data.end());
		return true;
	}
	std::unique_ptr<Vfs::BigSource> source = Vfs::BigSource::open(path, readAt, file->size());
	const Vfs::Entry *entry = source ? source->find("mod.txt") : nullptr;
	if (entry == nullptr || entry->size > MAX_MANIFEST)
		return false;
	text.assign(static_cast<std::size_t>(entry->size), '\0');
	text.resize(source->read(*entry, 0, text.data(), text.size()));
	return true;
}

}	// namespace

void parseModManifest(std::string_view text, ModInfo &info)
{
	info.hasManifest = true;
	if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB
		&& static_cast<unsigned char>(text[2]) == 0xBF)
		text.remove_prefix(3);
	int lineNumber = 0;
	while (!text.empty())
	{
		const std::size_t end = text.find('\n');
		std::string_view line = trim(text.substr(0, end));
		text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
		++lineNumber;
		if (line.empty() || line.front() == '#' || line.front() == ';')
			continue;
		const std::size_t equals = line.find('=');
		if (equals == std::string_view::npos)
		{
			addError(info, "line " + std::to_string(lineNumber) + " without '='");
			continue;
		}
		const std::string key = lower(trim(line.substr(0, equals)));
		const std::string value(trim(line.substr(equals + 1)));
		if (key == "name")
			info.title = value;
		else if (key == "version")
			info.version = value;
		else if (key == "author" || key == "autor")
			info.author = value;
		else if (key == "description" || key == "beschreibung")
			info.description = value;
		else if (key == "game" || key == "spiel")
		{
			const std::string game = lower(value);
			if (game == "generals")
				info.game = ModGame::Generals;
			else if (game == "zerohour" || game == "zero hour" || game == "zh")
				info.game = ModGame::ZeroHour;
			else
				addError(info, "unknown game '" + value + "'");
		}
		else
			addError(info, "unknown key '" + std::string(trim(line.substr(0, equals))) + "'");
	}
	if (info.title.empty())
		info.title = info.name;
}

std::vector<ModInfo> scanModFolder(std::string_view narrowFolder)
{
	namespace fs = std::filesystem;
	std::vector<ModInfo> mods;
	std::error_code ec;
	const fs::path folder = pathFromNarrow(narrowFolder);
	for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
	{
		ModInfo info;
		info.name = narrowFromPath(it->path().filename());
		const std::string extension = lower(narrowFromPath(it->path().extension()));
		std::error_code typeError;
		if (it->is_directory(typeError))
		{
			if (sameName(info.name, "Texturstufen"))
				continue;
			info.kind = ModKind::Folder;
#if defined(_WIN32)
			info.path = narrowFromPath(it->path()) + "\\";
#else
			info.path = narrowFromPath(it->path()) + "/";
#endif
		}
		else if (extension == ".big" || extension == ".zip")
		{
			info.kind = extension == ".zip" ? ModKind::Zip : ModKind::Big;
			info.path = narrowFromPath(it->path());
		}
		else
			continue;

		std::string text;
		bool found = false;
		if (info.kind == ModKind::Folder)
		{
			std::FILE *fp = openFile(it->path() / "mod.txt", "rb");
			if (fp != nullptr)
			{
				char buffer[4096];
				std::size_t got = 0;
				while ((got = std::fread(buffer, 1, sizeof(buffer), fp)) > 0 && text.size() < MAX_MANIFEST)
					text.append(buffer, got);
				std::fclose(fp);
				found = true;
			}
		}
		else
			found = readArchiveManifest(info.path, info.kind == ModKind::Zip, text);
		if (found)
			parseModManifest(text, info);
		else
			info.title = info.name;
		mods.push_back(std::move(info));
	}
	std::sort(mods.begin(), mods.end(), [](const ModInfo &a, const ModInfo &b) { return lower(a.name) < lower(b.name); });
	return mods;
}

std::vector<std::string> splitModList(std::string_view list)
{
	std::vector<std::string> names;
	while (true)
	{
		const std::size_t end = list.find(';');
		const std::string_view name = trim(list.substr(0, end));
		if (!name.empty() && std::none_of(names.begin(), names.end(), [name](const std::string &n) { return sameName(n, name); }))
			names.emplace_back(name);
		if (end == std::string_view::npos)
			break;
		list.remove_prefix(end + 1);
	}
	return names;
}

std::string joinModList(const std::vector<std::string> &names)
{
	std::string list;
	for (const std::string &name : names)
	{
		if (!list.empty())
			list += ';';
		list += name;
	}
	return list;
}

std::vector<ActiveMod> resolveModList(const std::vector<ModInfo> &catalog, const std::vector<std::string> &names, ModGame game)
{
	std::vector<ActiveMod> active;
	for (const std::string &name : names)
	{
		ActiveMod mod;
		mod.name = name;
		for (const ModInfo &info : catalog)
			if (sameName(info.name, name))
				mod.info = &info;
		if (mod.info == nullptr)
			mod.state = ModState::Missing;
		else if (mod.info->game != ModGame::Any && game != ModGame::Any && mod.info->game != game)
			mod.state = ModState::OtherGame;
		else
			mod.state = ModState::Ready;
		active.push_back(mod);
	}
	return active;
}

const char *modStateName(ModState state)
{
	switch (state)
	{
		case ModState::Ready: return "ready";
		case ModState::Missing: return "missing";
		case ModState::OtherGame: return "other game";
	}
	return "?";
}

namespace
{

// The quoting CommandLineToArgvW undoes: backslashes are literal unless they precede a quote.
void appendQuoted(std::wstring &line, const std::wstring &argument)
{
	if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
	{
		line += argument;
		return;
	}
	line += L'"';
	std::size_t backslashes = 0;
	for (wchar_t c : argument)
	{
		if (c == L'\\')
		{
			++backslashes;
			continue;
		}
		if (c == L'"')
			line.append(backslashes * 2 + 1, L'\\');
		else
			line.append(backslashes, L'\\');
		backslashes = 0;
		line += c;
	}
	line.append(backslashes * 2, L'\\');
	line += L'"';
}

bool isModSwitch(const std::wstring &argument)
{
	if (argument.size() != 4)
		return false;
	std::wstring lowerArgument;
	for (wchar_t c : argument)
		lowerArgument += static_cast<wchar_t>(c >= L'A' && c <= L'Z' ? c - L'A' + L'a' : c);
	return lowerArgument == L"-mod";
}

}	// namespace

std::wstring restartCommandLine(const std::vector<std::wstring> &arguments)
{
	std::wstring line;
	for (std::size_t i = 0; i < arguments.size(); ++i)
	{
		if (i > 0 && isModSwitch(arguments[i]))
		{
			++i;	// and its value
			continue;
		}
		if (!line.empty())
			line += L' ';
		appendQuoted(line, arguments[i]);
	}
	return line;
}

namespace
{

bool plainTagChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-'
		|| c == '_' || c == '(' || c == ')';
}

void appendEscaped(std::string &out, std::string_view text)
{
	static const char HEX[] = "0123456789ABCDEF";
	for (char c : text)
	{
		if (plainTagChar(c))
			out += c;
		else
		{
			const unsigned char u = static_cast<unsigned char>(c);
			out += '%';
			out += HEX[u >> 4];
			out += HEX[u & 0xF];
		}
	}
}

int hexValue(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

std::string unescape(std::string_view text)
{
	std::string out;
	std::size_t i = 0;
	while (i < text.size())
	{
		if (text[i] == '%' && i + 2 < text.size())
		{
			const int high = hexValue(text[i + 1]);
			const int low = hexValue(text[i + 2]);
			if (high >= 0 && low >= 0)
			{
				out += static_cast<char>(high * 16 + low);
				i += 3;
				continue;
			}
		}
		out += text[i++];
	}
	return out;
}

std::string joinEntries(const std::vector<std::string> &entries, std::size_t count)
{
	std::string text;
	for (std::size_t i = 0; i < count; ++i)
	{
		if (i > 0)
			text += ',';
		text += entries[i];
	}
	return text;
}

}	// namespace

std::string encodeModTag(const std::vector<ModTagEntry> &mods, std::size_t maxLength)
{
	std::vector<std::string> entries;
	for (const ModTagEntry &mod : mods)
	{
		std::string entry;
		appendEscaped(entry, mod.name);
		if (!mod.version.empty())
		{
			entry += '@';
			appendEscaped(entry, mod.version);
		}
		entries.push_back(std::move(entry));
	}
	std::string tag = joinEntries(entries, entries.size());
	if (tag.size() <= maxLength)
		return tag;
	// keep as many entries as fit next to the count of the rest
	for (std::size_t kept = entries.size(); kept-- > 0;)
	{
		tag = joinEntries(entries, kept);
		if (kept > 0)
			tag += ',';
		tag += '+' + std::to_string(entries.size() - kept);
		if (tag.size() <= maxLength)
			return tag;
	}
	return tag;
}

std::vector<ModTagEntry> decodeModTag(std::string_view tag, int *omitted)
{
	std::vector<ModTagEntry> mods;
	if (omitted)
		*omitted = 0;
	while (!tag.empty())
	{
		const std::size_t comma = tag.find(',');
		const std::string_view entry = tag.substr(0, comma);
		tag = comma == std::string_view::npos ? std::string_view() : tag.substr(comma + 1);
		if (entry.empty())
			continue;
		if (entry.front() == '+' && entry.size() > 1 && entry.find_first_not_of("0123456789", 1) == std::string_view::npos)
		{
			if (omitted)
				*omitted = std::atoi(std::string(entry.substr(1)).c_str());
			continue;
		}
		const std::size_t at = entry.find('@');
		ModTagEntry mod;
		mod.name = unescape(entry.substr(0, at));
		if (at != std::string_view::npos)
			mod.version = unescape(entry.substr(at + 1));
		mods.push_back(std::move(mod));
	}
	return mods;
}

bool sameModTags(std::string_view a, std::string_view b)
{
	int omittedA = 0, omittedB = 0;
	const std::vector<ModTagEntry> left = decodeModTag(a, &omittedA);
	const std::vector<ModTagEntry> right = decodeModTag(b, &omittedB);
	if (left.size() != right.size() || omittedA != omittedB)
		return false;
	for (std::size_t i = 0; i < left.size(); ++i)
		if (lower(left[i].name) != lower(right[i].name) || left[i].version != right[i].version)
			return false;
	return true;
}

std::string describeModTag(std::string_view tag)
{
	int omitted = 0;
	std::string text;
	for (const ModTagEntry &mod : decodeModTag(tag, &omitted))
	{
		if (!text.empty())
			text += ", ";
		text += mod.name;
		if (!mod.version.empty())
			text += " " + mod.version;
	}
	if (omitted > 0)
		text += (text.empty() ? "... (+" : ", ... (+") + std::to_string(omitted) + ")";
	return text;
}

}	// namespace Engine
