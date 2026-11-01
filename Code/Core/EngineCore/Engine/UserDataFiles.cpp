/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// UserDataFiles.cpp -- see UserDataFiles.h.

#include "Engine/UserDataFiles.h"

#include <cctype>
#include <chrono>
#include <cwchar>
#include <system_error>

#if defined(_WIN32)
	#include <windows.h>
	#include <fcntl.h>
	#include <io.h>
#else
	#include <fcntl.h>
	#include <unistd.h>
#endif

namespace Engine
{

bool isUtf8WithNonAscii(std::string_view text)
{
	bool nonAscii = false;
	std::size_t i = 0;
	while (i < text.size())
	{
		const unsigned char c = static_cast<unsigned char>(text[i]);
		if (c < 0x80)
		{
			++i;
			continue;
		}
		nonAscii = true;
		std::size_t length = 0;
		std::uint32_t code = 0;
		if (c >= 0xC2 && c <= 0xDF)			{ length = 2; code = c & 0x1F; }
		else if (c >= 0xE0 && c <= 0xEF)	{ length = 3; code = c & 0x0F; }
		else if (c >= 0xF0 && c <= 0xF4)	{ length = 4; code = c & 0x07; }
		else
			return false;
		if (i + length > text.size())
			return false;
		for (std::size_t k = 1; k < length; ++k)
		{
			const unsigned char next = static_cast<unsigned char>(text[i + k]);
			if ((next & 0xC0) != 0x80)
				return false;
			code = (code << 6) | (next & 0x3F);
		}
		// overlong forms, surrogates and code points past U+10FFFF
		if ((length == 3 && code < 0x800) || (length == 4 && (code < 0x10000 || code > 0x10FFFF))
			|| (code >= 0xD800 && code <= 0xDFFF))
			return false;
		i += length;
	}
	return nonAscii;
}

#if defined(_WIN32)

static std::wstring widen(std::string_view text, UINT codePage)
{
	if (text.empty())
		return std::wstring();
	const int count = ::MultiByteToWideChar(codePage, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring wide(static_cast<std::size_t>(count > 0 ? count : 0), L'\0');
	if (count > 0)
		::MultiByteToWideChar(codePage, 0, text.data(), static_cast<int>(text.size()), wide.data(), count);
	return wide;
}

static std::string narrowUtf8(const std::wstring &wide)
{
	if (wide.empty())
		return std::string();
	const int count = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
	std::string text(static_cast<std::size_t>(count > 0 ? count : 0), '\0');
	if (count > 0)
		::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), text.data(), count, nullptr, nullptr);
	return text;
}

// The ANSI form, if the code page holds every character. A result that would read back as UTF-8 does
// not count: pathFromNarrow would decode it differently.
static bool toAnsi(const std::wstring &wide, std::string &out)
{
	out.clear();
	if (wide.empty())
		return true;
	if (::GetACP() == CP_UTF8)
	{
		out = narrowUtf8(wide);
		return true;
	}
	BOOL usedDefault = FALSE;
	const int count = ::WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.data(), static_cast<int>(wide.size()),
		nullptr, 0, nullptr, &usedDefault);
	if (count <= 0 || usedDefault)
		return false;
	out.assign(static_cast<std::size_t>(count), '\0');
	::WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.data(), static_cast<int>(wide.size()), out.data(), count,
		nullptr, &usedDefault);
	if (usedDefault || isUtf8WithNonAscii(out))
	{
		out.clear();
		return false;
	}
	return true;
}

std::filesystem::path pathFromNarrow(std::string_view narrow)
{
	return std::filesystem::path(widen(narrow, isUtf8WithNonAscii(narrow) ? CP_UTF8 : CP_ACP));
}

std::string narrowFromPath(const std::filesystem::path &path)
{
	std::string ansi;
	if (toAnsi(path.native(), ansi))
		return ansi;
	return narrowUtf8(path.native());
}

std::string narrowForAnsiCallers(const std::filesystem::path &path)
{
	std::string ansi;
	if (toAnsi(path.native(), ansi))
		return ansi;
	const DWORD length = ::GetShortPathNameW(path.c_str(), nullptr, 0);
	if (length > 0)
	{
		std::wstring shortName(length, L'\0');
		const DWORD written = ::GetShortPathNameW(path.c_str(), shortName.data(), length);
		if (written > 0 && written < length)
		{
			shortName.resize(written);
			if (toAnsi(shortName, ansi))
				return ansi;
		}
	}
	return narrowUtf8(path.native());
}

std::FILE *openFile(const std::filesystem::path &path, const char *mode)
{
	return ::_wfopen(path.c_str(), widen(mode, CP_ACP).c_str());
}

int openFileDescriptor(std::string_view narrow, int flags, int permissions)
{
	return ::_wopen(pathFromNarrow(narrow).c_str(), flags, permissions);
}

#else	// not _WIN32: narrow paths are UTF-8

std::filesystem::path pathFromNarrow(std::string_view narrow)
{
	return std::filesystem::path(std::string(narrow));
}

std::string narrowFromPath(const std::filesystem::path &path)
{
	return path.string();
}

std::string narrowForAnsiCallers(const std::filesystem::path &path)
{
	return path.string();
}

std::FILE *openFile(const std::filesystem::path &path, const char *mode)
{
	return std::fopen(path.c_str(), mode);
}

int openFileDescriptor(std::string_view narrow, int flags, int permissions)
{
	return ::open(std::string(narrow).c_str(), flags, permissions);
}

#endif

std::FILE *openNarrowFile(std::string_view narrow, const char *mode)
{
	return openFile(pathFromNarrow(narrow), mode);
}

bool matchesWildcard(std::string_view name, std::string_view pattern)
{
	// Iterative match with one backtrack point for the last '*'.
	std::size_t n = 0, p = 0, starP = std::string_view::npos, starN = 0;
	while (n < name.size())
	{
		if (p < pattern.size() && (pattern[p] == '?'
			|| std::tolower(static_cast<unsigned char>(pattern[p])) == std::tolower(static_cast<unsigned char>(name[n]))))
		{
			++p;
			++n;
		}
		else if (p < pattern.size() && pattern[p] == '*')
		{
			starP = p++;
			starN = n;
		}
		else if (starP != std::string_view::npos)
		{
			p = starP + 1;
			n = ++starN;
		}
		else
			return false;
	}
	while (p < pattern.size() && pattern[p] == '*')
		++p;
	return p == pattern.size();
}

namespace UserDataFiles
{

// A heap object rather than a static path: a debug STL allocates a container proxy in every path's
// constructor, and a static one would only free it after the memory report at shutdown.
static std::filesystem::path *s_root = nullptr;

void setRoot(const std::filesystem::path &root)
{
	if (s_root)
		*s_root = root;
	else
		s_root = new std::filesystem::path(root);
}

std::filesystem::path root()
{
	return s_root ? *s_root : std::filesystem::path();
}

void clearRoot()
{
	delete s_root;
	s_root = nullptr;
}

std::filesystem::path resolve(std::string_view narrowRelative)
{
#if defined(_WIN32)
	return s_root ? *s_root / pathFromNarrow(narrowRelative) : pathFromNarrow(narrowRelative);
#else
	std::string relative(narrowRelative);
	for (char &c : relative)
		if (c == '\\')
			c = '/';
	return s_root ? *s_root / pathFromNarrow(relative) : pathFromNarrow(relative);
#endif
}

static void setError(std::string *error, std::string text)
{
	if (error)
		*error = std::move(text);
}

bool replaceFile(const std::filesystem::path &from, const std::filesystem::path &to, std::string *error)
{
#if defined(_WIN32)
	if (::MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		return true;
	setError(error, "MoveFileEx " + std::to_string(::GetLastError()));
	return false;
#else
	std::error_code ec;
	std::filesystem::rename(from, to, ec);
	if (!ec)
		return true;
	setError(error, "rename " + ec.message());
	return false;
#endif
}

bool writeAtomically(const std::filesystem::path &path, std::string_view bytes, std::string *error)
{
	std::filesystem::path temporary = path;
	temporary += ".tmp";
	std::FILE *file = openFile(temporary, "wb");
	if (!file)
	{
		setError(error, "cannot create " + narrowFromPath(temporary));
		return false;
	}
	bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
	ok = std::fflush(file) == 0 && ok;
#if defined(_WIN32)
	ok = ::_commit(::_fileno(file)) == 0 && ok;
#endif
	ok = std::fclose(file) == 0 && ok;
	if (!ok)
	{
		setError(error, "cannot write " + narrowFromPath(temporary));
		UserDataFiles::remove(temporary);
		return false;
	}
	if (!replaceFile(temporary, path, error))
	{
		UserDataFiles::remove(temporary);
		return false;
	}
	return true;
}

bool copyFile(const std::filesystem::path &from, const std::filesystem::path &to, bool overwrite)
{
#if defined(_WIN32)
	return ::CopyFileW(from.c_str(), to.c_str(), overwrite ? FALSE : TRUE) != 0;
#else
	std::error_code ec;
	const auto options = overwrite ? std::filesystem::copy_options::overwrite_existing : std::filesystem::copy_options::none;
	return std::filesystem::copy_file(from, to, options, ec) && !ec;
#endif
}

bool readFile(const std::filesystem::path &path, std::string &bytes)
{
	bytes.clear();
	std::FILE *file = openFile(path, "rb");
	if (!file)
		return false;
	char buffer[16384];
	std::size_t count = 0;
	while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
		bytes.append(buffer, count);
	const bool ok = std::ferror(file) == 0;
	std::fclose(file);
	return ok;
}

std::vector<UserDataEntry> list(const std::filesystem::path &dir, std::string_view pattern)
{
	std::vector<UserDataEntry> entries;
#if defined(_WIN32)
	// FindFirstFileW keeps the pattern rules the game used before (it also matches short names).
	const std::filesystem::path search = dir / pathFromNarrow(pattern);
	WIN32_FIND_DATAW item = {};
	HANDLE find = ::FindFirstFileW(search.c_str(), &item);
	if (find == INVALID_HANDLE_VALUE)
		return entries;
	do
	{
		if (std::wcscmp(item.cFileName, L".") == 0 || std::wcscmp(item.cFileName, L"..") == 0)
			continue;
		UserDataEntry entry;
		entry.name = narrowFromPath(std::filesystem::path(item.cFileName));
		entry.directory = (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		entry.size = (static_cast<std::uint64_t>(item.nFileSizeHigh) << 32) | item.nFileSizeLow;
		entry.writeTime = (static_cast<std::uint64_t>(item.ftLastWriteTime.dwHighDateTime) << 32)
			| item.ftLastWriteTime.dwLowDateTime;
		entries.push_back(std::move(entry));
	} while (::FindNextFileW(find, &item));
	::FindClose(find);
#else
	std::error_code ec;
	std::filesystem::directory_iterator it(dir, ec);
	if (ec)
		return entries;
	for (const std::filesystem::directory_entry &item : it)
	{
		const std::string name = item.path().filename().string();
		if (!matchesWildcard(name, pattern))
			continue;
		UserDataEntry entry;
		entry.name = name;
		entry.directory = item.is_directory(ec);
		if (!entry.directory)
			entry.size = item.file_size(ec);
		const auto time = item.last_write_time(ec);
		entry.writeTime = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());
		entries.push_back(std::move(entry));
	}
#endif
	return entries;
}

bool exists(const std::filesystem::path &path)
{
	std::error_code ec;
	return std::filesystem::exists(path, ec);
}

bool isDirectory(const std::filesystem::path &path)
{
	std::error_code ec;
	return std::filesystem::is_directory(path, ec);
}

bool remove(const std::filesystem::path &path)
{
	std::error_code ec;
	std::filesystem::remove(path, ec);
	return !UserDataFiles::exists(path);
}

bool createDirectories(const std::filesystem::path &path)
{
	std::error_code ec;
	std::filesystem::create_directories(path, ec);
	return UserDataFiles::isDirectory(path);
}

}	// namespace UserDataFiles

}	// namespace Engine
