// Copyright (C) 2026 GZH Neo
// Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
// (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.

// Copies the game data of Generals and Zero Hour from the player's installation into the folders of GZH Neo
// and applies the project's data fixes as loose files. The installation is only read. Portable.

#pragma once

#include "GameDataFinder.h"

#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace gzhsetup
{

// Entries of a .big archive (EA's open BIG format), by lower-case name.
class BigArchive
{
public:
	bool open(const std::filesystem::path& path);
	bool contains(const std::string& name) const;
	// Bytes of an entry; throws std::runtime_error if missing or unreadable.
	std::string read(const std::string& name) const;
	std::vector<std::string> names() const;

private:
	struct Entry
	{
		std::uint32_t offset = 0;
		std::uint32_t size = 0;
	};
	std::filesystem::path m_path;
	std::map<std::string, Entry> m_entries;
};

// Removes the useless turret lines of GLARocketBuggyFullDebris from FactionUnit.ini (its debris model has no
// turret). Throws std::runtime_error if the file does not have exactly the expected lines.
std::string fixRocketBuggyDebris(const std::string& factionUnitIni);

// Called after each file with the bytes handled so far (copied or already in place); returning false cancels.
using CopyProgress = std::function<bool(std::uint64_t bytesDone)>;

struct CopyCancelled : std::runtime_error
{
	CopyCancelled() : std::runtime_error("cancelled") {}
};

struct CopyReport
{
	std::vector<std::string> lines;
	std::uint64_t bytesCopied = 0;
	std::uint64_t bytesDone = 0;
	int filesCopied = 0;
	int filesLinked = 0;
	CopyProgress progress;
};

struct CopyEstimate
{
	// Everything the copy reads (the scale of the progress), and what is not yet in place (needs free space).
	std::uint64_t totalBytes = 0;
	std::uint64_t missingBytes = 0;
};

using CopyLog = std::function<void(const std::string&)>;

// Changes whenever the fixes change, so an update knows the loose files have to be rewritten.
inline constexpr int DATA_FIXES_VERSION = 1;

// Name of the manifest written into each game folder after a complete copy.
inline constexpr const char* MANIFEST_NAME = "GZHNeoData.json";

CopyEstimate estimateCopy(const GameFolder& generals, const GameFolder& zeroHour, const std::filesystem::path& target);

// Sets up <target>/Generals and <target>/ZeroHour. Repeatable: copies only what is missing or changed
// (size and modification time), removes excluded files left by an earlier copy. Throws std::runtime_error,
// CopyCancelled if the progress callback returns false.
CopyReport setUpGameData(const GameFolder& generals, const GameFolder& zeroHour, const std::filesystem::path& target,
	const CopyLog& log, const CopyProgress& progress = {});

// Single steps, also for the comparison with tools/assets/setup_assets.py.
void copyGeneralsData(const GameFolder& generals, const std::filesystem::path& zeroHourSource,
	const std::filesystem::path& dest, CopyReport& report, const CopyLog& log);
void copyZeroHourData(const GameFolder& zeroHour, const std::filesystem::path& generalsDest,
	const std::filesystem::path& dest, CopyReport& report, const CopyLog& log);

} // namespace gzhsetup
