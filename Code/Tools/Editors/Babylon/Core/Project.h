/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Project.h -- the files Babylon works on and their state: Generals.str (read only), the database
// main.db and the folder of recorded speech. By default, as in the original tool, under one folder:
// Data/Generals.str, Data/main.db and dialog/.

#pragma once

#include "Core/Checks.h"
#include "Core/Reports.h"
#include "Core/Update.h"
#include "Model/Database.h"
#include "Model/StrFile.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Babylon
{

struct ProjectPaths
{
	std::filesystem::path str;
	std::filesystem::path db;
	std::filesystem::path dialog;

	static ProjectPaths inFolder(const std::filesystem::path &root);
};

bool readFile(const std::filesystem::path &path, std::vector<std::uint8_t> &data, std::string &error);
// Writes a temporary file next to the target and renames it, so a failed write leaves the old file.
bool writeFile(const std::filesystem::path &path, std::span<const std::uint8_t> data, std::string &error);
bool writeFile(const std::filesystem::path &path, std::string_view text, std::string &error);
// main.db -> main_back_up.db, the name the original tool used.
std::filesystem::path backupPath(const std::filesystem::path &db);
std::string pathText(const std::filesystem::path &path);
std::filesystem::path pathFromText(const std::string &utf8);

class Project
{
public:
	ProjectPaths paths;
	Database str;
	Database db;
	std::vector<StrProblem> strProblems;
	std::vector<Finding> errors;
	std::vector<Finding> warnings;
	bool strFound = false;
	bool strLoaded = false;
	bool dbFound = false;
	bool dbLoaded = false;
	bool dbReadOnly = false;
	bool dbBroken = false;
	// What an update would change (counted when loading and after every update).
	UpdateCounts pending;
	LineSink log;

	Project();

	// Reads both files, checks Generals.str and counts the differences.
	void load();
	// The database differs from Generals.str.
	[[nodiscard]] bool strOutOfDate() const { return strLoaded && pending.changes > 0; }
	[[nodiscard]] bool canUpdate() const { return strLoaded && errors.empty() && !dbBroken && !dbReadOnly; }
	[[nodiscard]] bool canSave() const { return !dbBroken && !dbReadOnly; }
	UpdateCounts update(UpdateResolver &resolver);
	// Saves the database if it changed (a copy of the old file goes to backupPath).
	bool save(std::string &error);
	void recount();
	[[nodiscard]] DialogFiles dialogFiles() const { return DialogFiles(paths.dialog); }

private:
	void say(const std::string &line) const;
};

} // namespace Babylon
