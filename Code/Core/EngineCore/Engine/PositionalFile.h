/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// PositionalFile.h -- a file opened for reading at given offsets. There is no shared file position,
// so any number of threads may read from one open file at the same time without a lock: Windows
// reads with an OVERLAPPED offset, other systems with pread.

#pragma once

#ifndef ENGINE_POSITIONALFILE_H
#define ENGINE_POSITIONALFILE_H

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Engine
{

class PositionalFile
{
public:
	enum class Pattern
	{
		Random,		///< reads anywhere (archive entries)
		Sequential	///< reads front to back (read-ahead)
	};

	PositionalFile() = default;
	~PositionalFile() { close(); }
	PositionalFile(const PositionalFile &) = delete;
	PositionalFile &operator=(const PositionalFile &) = delete;

	/// Opens an existing file for reading; the path is narrow as for openNarrowFile.
	bool open(std::string_view narrowPath, Pattern pattern = Pattern::Random);
	void close();
	bool isOpen() const;

	/// Size when opened.
	std::uint64_t size() const { return m_size; }

	/// Reads up to size bytes at offset; returns how many were read (less at the end of the file,
	/// 0 on an error). Thread-safe.
	std::size_t readAt(std::uint64_t offset, void *dest, std::size_t size) const;

private:
#if defined(_WIN32)
	void *m_handle = nullptr;
#else
	int m_fd = -1;
#endif
	std::uint64_t m_size = 0;
};

}	// namespace Engine

#endif	// ENGINE_POSITIONALFILE_H
