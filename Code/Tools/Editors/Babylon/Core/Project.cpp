/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Core/Project.h"

#include "Model/DbFile.h"

#include <fstream>
#include <system_error>

namespace Babylon
{

namespace fs = std::filesystem;

ProjectPaths ProjectPaths::inFolder(const fs::path &root)
{
	ProjectPaths p;
	p.str = root / "Data" / "Generals.str";
	p.db = root / "Data" / "main.db";
	p.dialog = root / "dialog";
	return p;
}

std::string pathText(const fs::path &path)
{
	const std::u8string u = path.u8string();
	return std::string(u.begin(), u.end());
}

fs::path pathFromText(const std::string &utf8)
{
	return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

bool readFile(const fs::path &path, std::vector<std::uint8_t> &data, std::string &error)
{
	data.clear();
	std::ifstream in(path, std::ios::binary);
	if (!in)
	{
		error = "cannot open " + pathText(path);
		return false;
	}
	in.seekg(0, std::ios::end);
	const std::streamoff size = in.tellg();
	in.seekg(0, std::ios::beg);
	if (size < 0)
	{
		error = "cannot read " + pathText(path);
		return false;
	}
	data.resize(static_cast<std::size_t>(size));
	if (size > 0 && !in.read(reinterpret_cast<char *>(data.data()), size))
	{
		error = "cannot read " + pathText(path);
		data.clear();
		return false;
	}
	return true;
}

bool writeFile(const fs::path &path, std::span<const std::uint8_t> data, std::string &error)
{
	fs::path temp = path;
	temp += ".new";
	std::error_code dirError;
	if (path.has_parent_path())
		fs::create_directories(path.parent_path(), dirError);
	{
		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			error = "cannot create " + pathText(temp);
			return false;
		}
		out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
		out.close();
		if (!out)
		{
			error = "cannot write " + pathText(temp);
			std::error_code ec;
			fs::remove(temp, ec);
			return false;
		}
	}
	std::error_code ec;
	fs::rename(temp, path, ec);
	if (ec)
	{
		error = "cannot replace " + pathText(path) + ": " + ec.message();
		fs::remove(temp, ec);
		return false;
	}
	return true;
}

bool writeFile(const fs::path &path, std::string_view text, std::string &error)
{
	return writeFile(path, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(text.data()), text.size()), error);
}

fs::path backupPath(const fs::path &db)
{
	fs::path p = db;
	p.replace_filename(pathFromText(pathText(db.stem()) + "_back_up" + pathText(db.extension())));
	return p;
}

Project::Project()
{
	str.nextId = -1;
}

void Project::say(const std::string &line) const
{
	if (log)
		log(line);
}

void Project::load()
{
	str.clear();
	str.nextId = -1;
	db.clear();
	db.nextId = FIRST_STRING_ID;
	strProblems.clear();
	errors.clear();
	warnings.clear();
	strFound = strLoaded = dbFound = dbLoaded = dbReadOnly = dbBroken = false;
	pending = UpdateCounts();

	std::error_code ec;
	std::string error;
	strFound = fs::is_regular_file(paths.str, ec);
	if (!strFound)
		say("Loading \"" + pathText(paths.str) + "\"... FILE NOT FOUND");
	else
	{
		std::vector<std::uint8_t> data;
		if (!readFile(paths.str, data, error))
			say("Loading \"" + pathText(paths.str) + "\"... FAILED: " + error);
		else
		{
			const bool complete = readStrFile(std::string_view(reinterpret_cast<const char *>(data.data()), data.size()), str, strProblems);
			strLoaded = true;
			errors = strErrors(str, strProblems);
			warnings = strWarnings(str, strProblems);
			std::string line = "Loading \"" + pathText(paths.str) + "\"... " + std::to_string(str.labels.size()) + " labels";
			if (!complete)
				line += ", file incomplete";
			if (errors.empty() && warnings.empty())
				line += " OK";
			else
				line += ", " + std::to_string(errors.size()) + " errors, " + std::to_string(warnings.size()) + " warnings";
			say(line);
		}
	}

	dbFound = fs::is_regular_file(paths.db, ec);
	if (!dbFound)
		say("Loading \"" + pathText(paths.db) + "\"... FILE NOT FOUND (a new database will be created)");
	else
	{
		std::vector<std::uint8_t> data;
		if (!readFile(paths.db, data, error) || !readDatabase(data, db, error))
		{
			dbBroken = true;
			db.clear();
			say("Loading \"" + pathText(paths.db) + "\"... FAILED: " + error);
		}
		else
		{
			dbLoaded = true;
			const fs::perms perms = fs::status(paths.db, ec).permissions();
			dbReadOnly = !ec && (perms & fs::perms::owner_write) == fs::perms::none;
			say("Loading \"" + pathText(paths.db) + "\"... " + std::to_string(db.labels.size()) + " labels, " + std::to_string(db.textCount()) + " texts" +
				(dbReadOnly ? " READ ONLY" : " OK"));
		}
	}
	recount();
}

void Project::recount()
{
	pending = UpdateCounts();
	if (strLoaded && !dbBroken && errors.empty())
	{
		Database copy = db;
		pending = updateDatabase(str, copy, nullptr, false);
	}
}

UpdateCounts Project::update(UpdateResolver &resolver)
{
	say("Updating \"" + pathText(paths.db) + "\" from \"" + pathText(paths.str) + "\"...");
	const UpdateCounts counts = updateDatabase(str, db, &resolver, true);
	for (const std::string &line : updateSummary(counts))
		say("  " + line);
	recount();
	return counts;
}

bool Project::save(std::string &error)
{
	if (dbBroken)
	{
		error = "the database could not be read, so it is not overwritten";
		return false;
	}
	if (!db.modified && dbFound)
		return true;
	if (dbReadOnly)
	{
		error = "the database file is read only";
		return false;
	}
	std::error_code ec;
	fs::create_directories(paths.db.parent_path(), ec);
	if (fs::is_regular_file(paths.db, ec))
		fs::copy_file(paths.db, backupPath(paths.db), fs::copy_options::overwrite_existing, ec);
	const std::vector<std::uint8_t> data = writeDatabase(db);
	if (!writeFile(paths.db, data, error))
	{
		say("Saving main database... FAILED: " + error);
		return false;
	}
	db.modified = false;
	dbFound = dbLoaded = true;
	say("Saving main database... OK (" + std::to_string(db.labels.size()) + " labels)");
	return true;
}

} // namespace Babylon
