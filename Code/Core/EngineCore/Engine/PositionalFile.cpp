/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// PositionalFile.cpp -- see PositionalFile.h.

#include "Engine/PositionalFile.h"
#include "Engine/UserDataFiles.h"

#if defined(_WIN32)
	#include <windows.h>
#else
	#include <cerrno>
	#include <fcntl.h>
	#include <sys/stat.h>
	#include <unistd.h>
#endif

namespace Engine
{

#if defined(_WIN32)

bool PositionalFile::open(std::string_view narrowPath, Pattern pattern)
{
	close();
	// Overlapped: each read carries its own offset and waits on its own event, so reads of several
	// threads run side by side instead of queuing on the file object.
	const DWORD flags = FILE_FLAG_OVERLAPPED
		| (pattern == Pattern::Sequential ? FILE_FLAG_SEQUENTIAL_SCAN : FILE_FLAG_RANDOM_ACCESS);
	HANDLE handle = ::CreateFileW(pathFromNarrow(narrowPath).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_EXISTING, flags, nullptr);
	if (handle == INVALID_HANDLE_VALUE)
		return false;
	LARGE_INTEGER size;
	if (!::GetFileSizeEx(handle, &size))
	{
		::CloseHandle(handle);
		return false;
	}
	m_handle = handle;
	m_size = static_cast<std::uint64_t>(size.QuadPart);
	return true;
}

void PositionalFile::close()
{
	if (m_handle != nullptr)
		::CloseHandle(static_cast<HANDLE>(m_handle));
	m_handle = nullptr;
	m_size = 0;
}

bool PositionalFile::isOpen() const
{
	return m_handle != nullptr;
}

std::size_t PositionalFile::readAt(std::uint64_t offset, void *dest, std::size_t size) const
{
	if (m_handle == nullptr || size == 0)
		return 0;
	HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (event == nullptr)
		return 0;
	std::size_t done = 0;
	while (done < size)
	{
		const DWORD chunk = static_cast<DWORD>(size - done < (1u << 30) ? size - done : (1u << 30));
		OVERLAPPED overlapped = {};
		const std::uint64_t at = offset + done;
		overlapped.Offset = static_cast<DWORD>(at & 0xFFFFFFFFu);
		overlapped.OffsetHigh = static_cast<DWORD>(at >> 32);
		overlapped.hEvent = event;
		DWORD got = 0;
		if (!::ReadFile(static_cast<HANDLE>(m_handle), static_cast<char *>(dest) + done, chunk, nullptr, &overlapped)
			&& ::GetLastError() != ERROR_IO_PENDING)
			break;	// ERROR_HANDLE_EOF at or past the end
		if (!::GetOverlappedResult(static_cast<HANDLE>(m_handle), &overlapped, &got, TRUE) || got == 0)
			break;
		done += got;
	}
	::CloseHandle(event);
	return done;
}

#else

bool PositionalFile::open(std::string_view narrowPath, Pattern pattern)
{
	close();
	const int fd = openFileDescriptor(narrowPath, O_RDONLY, 0);
	if (fd < 0)
		return false;
	struct stat info;
	if (::fstat(fd, &info) != 0)
	{
		::close(fd);
		return false;
	}
#if defined(POSIX_FADV_SEQUENTIAL)
	::posix_fadvise(fd, 0, 0, pattern == Pattern::Sequential ? POSIX_FADV_SEQUENTIAL : POSIX_FADV_RANDOM);
#else
	(void)pattern;
#endif
	m_fd = fd;
	m_size = static_cast<std::uint64_t>(info.st_size);
	return true;
}

void PositionalFile::close()
{
	if (m_fd >= 0)
		::close(m_fd);
	m_fd = -1;
	m_size = 0;
}

bool PositionalFile::isOpen() const
{
	return m_fd >= 0;
}

std::size_t PositionalFile::readAt(std::uint64_t offset, void *dest, std::size_t size) const
{
	if (m_fd < 0)
		return 0;
	std::size_t done = 0;
	while (done < size)
	{
		const ssize_t got = ::pread(m_fd, static_cast<char *>(dest) + done, size - done, static_cast<off_t>(offset + done));
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
			break;
		done += static_cast<std::size_t>(got);
	}
	return done;
}

#endif

}	// namespace Engine
