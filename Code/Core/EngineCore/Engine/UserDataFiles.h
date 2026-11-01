/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/
// UserDataFiles.h -- files under the user data folder (saves, replays, maps, settings) and the narrow
// paths the engine passes around.
//
// The engine keeps paths in char strings. A narrow path is in the ANSI code page as long as that code
// page can hold it, otherwise in UTF-8; pathFromNarrow tells the two apart (valid UTF-8 with a byte
// above 127 is read as UTF-8). Pure ASCII is the same in both. Real paths are std::filesystem::path,
// UTF-16 on Windows, so a profile folder outside the ANSI code page still works.
#pragma once
#ifndef ENGINE_USERDATAFILES_H
#define ENGINE_USERDATAFILES_H

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Engine
{

/// True for valid UTF-8 that has at least one byte above 127.
bool isUtf8WithNonAscii(std::string_view text);

/// Narrow path (ANSI code page or UTF-8, see above) to a real path.
std::filesystem::path pathFromNarrow(std::string_view narrow);

/// Real path to a narrow path: ANSI code page if it holds the path without loss, else UTF-8.
std::string narrowFromPath(const std::filesystem::path &path);

/// Narrow path that also code without pathFromNarrow can open: as narrowFromPath, but outside the ANSI
/// code page the short (8.3) name of an existing path is preferred when it fits the code page.
std::string narrowForAnsiCallers(const std::filesystem::path &path);

/// fopen and _open with a narrow path.
std::FILE *openNarrowFile(std::string_view narrow, const char *mode);
std::FILE *openFile(const std::filesystem::path &path, const char *mode);
int openFileDescriptor(std::string_view narrow, int flags, int permissions);

/// Case-insensitive (ASCII) match with '*' and '?'.
bool matchesWildcard(std::string_view name, std::string_view pattern);

struct UserDataEntry
{
	std::string name;			///< narrow, see narrowFromPath
	bool directory = false;
	std::uint64_t size = 0;
	std::uint64_t writeTime = 0;	///< FILETIME on Windows, seconds elsewhere; only for comparing
};

namespace UserDataFiles
{
	/// The user data folder; set once at startup.
	void setRoot(const std::filesystem::path &root);
	std::filesystem::path root();
	/// Frees the stored root (shutdown, before the memory report); root() is empty afterwards.
	void clearRoot();
	/// root() joined with a narrow relative path ('\\' or '/').
	std::filesystem::path resolve(std::string_view narrowRelative);

	/// Writes to "<path>.tmp" and moves it over path in one step, so a crash leaves either the old or the
	/// new file. On failure the old file stays and error (if given) says why.
	bool writeAtomically(const std::filesystem::path &path, std::string_view bytes, std::string *error = nullptr);
	/// Moves from over to in one step (replacing to); used after writing a temporary file.
	bool replaceFile(const std::filesystem::path &from, const std::filesystem::path &to, std::string *error = nullptr);
	bool copyFile(const std::filesystem::path &from, const std::filesystem::path &to, bool overwrite);
	bool readFile(const std::filesystem::path &path, std::string &bytes);

	/// Entries of dir whose name matches pattern ('*', '?'), in the order the file system gives; no "."
	/// and "..". A missing folder gives an empty list.
	std::vector<UserDataEntry> list(const std::filesystem::path &dir, std::string_view pattern);
	bool exists(const std::filesystem::path &path);
	bool isDirectory(const std::filesystem::path &path);
	/// Removes a file or an empty folder; true if it is gone afterwards.
	bool remove(const std::filesystem::path &path);
	/// Creates the folder and its parents; true if it exists afterwards.
	bool createDirectories(const std::filesystem::path &path);
}

}	// namespace Engine

#endif	// ENGINE_USERDATAFILES_H
