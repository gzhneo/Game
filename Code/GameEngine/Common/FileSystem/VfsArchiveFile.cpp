/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// VfsArchiveFile.cpp -- .big and .zip archives and loose files as ArchiveFile, see VfsArchiveFile.h.

#include "PreRTS.h"
#include "formatinto.h"

#include "Common/VfsArchiveFile.h"
#include "Engine/BigArchive.h"
#include "Engine/PositionalFile.h"
#include "Engine/Vfs.h"
#include "Common/file.h"
#include "Common/GameEngine.h"
#include "Common/LocalFileSystem.h"
#include "Common/RAMFile.h"

#include <climits>
#include <cstdio>
#include <string>

namespace
{

/// The archive on disk, shared by all reads of its entries. Every read names its offset, so threads
/// read side by side without a lock.
class SharedArchive
{
public:
	bool open(const char *path) { return m_file.open(path); }
	std::uint64_t size() const { return m_file.size(); }
	std::size_t read(std::uint64_t offset, void *dest, std::size_t size) const { return m_file.readAt(offset, dest, size); }

private:
	Engine::PositionalFile m_file;
};

// Set while archives are mounted, on the main thread. A fixed buffer: a static string would keep its heap
// block until after the leak report.
char s_lastOpenError[128] = "";

void setLastOpenError(const char *reason)
{
	std::snprintf(s_lastOpenError, sizeof(s_lastOpenError), "%s", reason);
}

void runnerLog(const char *line)
{
	GameEngine_RunnerLog(line);
	DEBUG_LOG(("%s\n", line));
}

}	// namespace

VfsArchiveFile::VfsArchiveFile(const AsciiString &path, Kind kind, std::unique_ptr<Engine::Vfs::Source> source)
	: m_path(path), m_kind(kind), m_source(std::move(source))
{
	// The per-archive directory serves the content checks of tier and package archives and the
	// legacy name tree; the index of the archive file system reads the source itself.
	fillDirectoryTree();
}

VfsArchiveFile::~VfsArchiveFile()
{
}

const char *VfsArchiveFile::lastOpenError()
{
	return s_lastOpenError;
}

Int VfsArchiveFile::getSkippedCount() const
{
	std::size_t skipped = m_source->skippedEntries();
	if (m_kind == Kind::Zip)
		skipped += static_cast<const Engine::Vfs::ZipSource *>(m_source.get())->rejected().size();
	return static_cast<Int>(skipped);
}

VfsArchiveFile *VfsArchiveFile::openBig(const AsciiString &path, Bool quiet)
{
	s_lastOpenError[0] = 0;
	auto archive = std::make_shared<SharedArchive>();
	if (!archive->open(path.str()))
	{
		setLastOpenError("not readable");
		if (!quiet)
			DEBUG_CRASH(("Could not open archive file %s for parsing", path.str()));
		return nullptr;
	}

	Engine::BigArchiveError error = Engine::BigArchiveError::None;
	std::unique_ptr<Engine::Vfs::BigSource> source = Engine::Vfs::BigSource::open(path.str(),
		[archive](std::uint64_t offset, void *dest, std::size_t count) { return archive->read(offset, dest, count); },
		archive->size(), &error);
	if (source == nullptr)
	{
		setLastOpenError(Engine::bigArchiveErrorText(error));
		if (quiet)
			DEBUG_LOG(("Archive %s not used: %s\n", path.str(), Engine::bigArchiveErrorText(error)));
		else
			DEBUG_CRASH(("Archive %s not used: %s", path.str(), Engine::bigArchiveErrorText(error)));
		return nullptr;
	}
	DEBUG_LOG(("VfsArchiveFile::openBig - %s: %d files\n", path.str(), static_cast<Int>(source->entries().size())));
	return NEW VfsArchiveFile(path, Kind::Big, std::move(source));
}

VfsArchiveFile *VfsArchiveFile::openZip(const AsciiString &path)
{
	s_lastOpenError[0] = 0;
	char line[MAX_PATH * 2 + 128];
	auto archive = std::make_shared<SharedArchive>();
	if (!archive->open(path.str()))
	{
		setLastOpenError("not readable");
		WWFormat::Into(line, "MOD zip abgewiesen {} wegen nicht lesbar", path.str());
		line[sizeof(line) - 1] = 0;
		runnerLog(line);
		return nullptr;
	}

	Engine::ZipArchiveError error = Engine::ZipArchiveError::None;
	std::unique_ptr<Engine::Vfs::ZipSource> source = Engine::Vfs::ZipSource::open(path.str(),
		[archive](std::uint64_t offset, void *dest, std::size_t count) { return archive->read(offset, dest, count); },
		archive->size(), &error);
	if (source == nullptr)
	{
		setLastOpenError(Engine::zipArchiveErrorText(error));
		WWFormat::Into(line, "MOD zip abgewiesen {} wegen {}", path.str(), Engine::zipArchiveErrorText(error));
		line[sizeof(line) - 1] = 0;
		runnerLog(line);
		return nullptr;
	}

	for (const Engine::ZipArchiveSkipped &skipped : source->rejected())
	{
		WWFormat::Into(line, "MOD zip {} eintrag ausgelassen {} wegen {}", path.str(), skipped.path.c_str(),
			Engine::zipArchiveErrorText(skipped.reason));
		line[sizeof(line) - 1] = 0;
		runnerLog(line);
	}
	WWFormat::Into(line, "MOD zip {} dateien {:d} ausgelassen {:d}", path.str(), static_cast<Int>(source->entries().size()),
		static_cast<Int>(source->rejected().size() + source->skippedEntries()));
	line[sizeof(line) - 1] = 0;
	runnerLog(line);

	return NEW VfsArchiveFile(path, Kind::Zip, std::move(source));
}

VfsArchiveFile *VfsArchiveFile::openFolder(const AsciiString &folder, const std::vector<AsciiString> &relativePaths)
{
	std::vector<Engine::Vfs::FolderSource::File> files;
	files.reserve(relativePaths.size());
	for (const AsciiString &relative : relativePaths)
	{
		AsciiString full = folder;
		full.concat(relative);
		FileInfo info;
		if (!TheLocalFileSystem->getFileInfo(full, &info))
			continue;
		files.push_back({relative.str(), (static_cast<std::uint64_t>(info.sizeHigh) << 32) | static_cast<UnsignedInt>(info.sizeLow)});
	}

	const std::string base = folder.str();
	auto source = std::make_unique<Engine::Vfs::FolderSource>(base, files,
		[base](const std::string &relative, std::uint64_t offset, void *dest, std::size_t size) -> std::size_t
		{
			if (offset > static_cast<std::uint64_t>(INT_MAX) || size > static_cast<std::size_t>(INT_MAX))
				return 0;
			File *fp = TheLocalFileSystem->openFile((base + relative).c_str(), File::READ | File::BINARY);
			if (fp == nullptr)
				return 0;
			Int got = 0;
			if (fp->seek(static_cast<Int>(offset), File::seekMode::START) == static_cast<Int>(offset))
				got = fp->read(dest, static_cast<Int>(size));
			fp->close();
			return got > 0 ? static_cast<std::size_t>(got) : 0;
		});

	char line[MAX_PATH * 2 + 64];
	WWFormat::Into(line, "MOD ordner {} dateien {:d}", folder.str(), static_cast<Int>(source->entries().size()));
	line[sizeof(line) - 1] = 0;
	runnerLog(line);

	return NEW VfsArchiveFile(folder, Kind::Folder, std::move(source));
}

void VfsArchiveFile::fillDirectoryTree()
{
	AsciiString archiveName = m_path;
	archiveName.toLower();
	ArchivedFileInfo fileInfo;
	fileInfo.m_archiveFilename = archiveName;
	for (const Engine::Vfs::Entry &entry : m_source->entries())
	{
		// split the path into the directory (with its trailing separator) and the file name
		const std::size_t separator = entry.stored.find_last_of("\\/");
		const std::size_t nameStart = separator == std::string::npos ? 0 : separator + 1;
		fileInfo.m_filename = entry.stored.c_str() + nameStart;
		fileInfo.m_filename.toLower();
		fileInfo.m_offset = 0;
		fileInfo.m_size = static_cast<UnsignedInt>(entry.size);
		addFile(AsciiString(entry.stored.substr(0, nameStart).c_str()), &fileInfo);
	}
}

Int VfsArchiveFile::getFileCount() const
{
	return static_cast<Int>(m_source->entries().size());
}

AsciiString VfsArchiveFile::getSourceForFile(const AsciiString &archiveName, const AsciiString &filename) const
{
	if (m_kind != Kind::Folder)
		return archiveName;
	const Engine::Vfs::Entry *entry = m_source->find(filename.str());
	if (entry == nullptr)
		return archiveName;
	AsciiString path = m_path;
	path.concat(entry->stored.c_str());
	return path;
}

Bool VfsArchiveFile::getFileInfo(const AsciiString &filename, FileInfo *fileInfo) const
{
	const Engine::Vfs::Entry *entry = m_source->find(filename.str());
	if (entry == nullptr || fileInfo == nullptr)
		return FALSE;

	AsciiString onDisk = m_path;
	if (m_kind == Kind::Folder)
		onDisk.concat(entry->stored.c_str());
	if (!TheLocalFileSystem->getFileInfo(onDisk, fileInfo))
		return FALSE;
	fileInfo->sizeHigh = static_cast<Int>(entry->size >> 32);
	fileInfo->sizeLow = static_cast<Int>(entry->size & 0xFFFFFFFFu);
	return TRUE;
}

File *VfsArchiveFile::openFile(const Char *filename, Int access)
{
	const Engine::Vfs::Entry *entry = m_source->find(filename);
	if (entry == nullptr)
		return nullptr;

	// Loose files are read from disk like any other loose file.
	if (m_kind == Kind::Folder && (access & File::WRITE) == 0)
	{
		AsciiString full = m_path;
		full.concat(entry->stored.c_str());
		return TheLocalFileSystem->openFile(full.str(), access);
	}

	if (entry->size > static_cast<std::uint64_t>(INT_MAX))
		return nullptr;
	RAMFile *ramFile = nullptr;
	if (m_kind == Kind::Big)
	{
		// Read straight into the file; named like the legacy reader, by the file name alone in lower case.
		const std::size_t separator = entry->stored.find_last_of("\\/");
		AsciiString name = entry->stored.c_str() + (separator == std::string::npos ? 0 : separator + 1);
		name.toLower();
		ramFile = newInstance(RAMFile);
		ramFile->deleteOnClose();
		const Engine::Vfs::Source *source = m_source.get();
		if (!ramFile->openFromReader(name, static_cast<Int>(entry->size),
			[source, entry](void *dest) { return source->read(*entry, 0, dest, static_cast<std::size_t>(entry->size)) == entry->size; }))
		{
			ramFile->close();
			return nullptr;
		}
		return finishOpen(ramFile, filename, access);
	}

	std::vector<unsigned char> data;
	if (m_kind == Kind::Folder)
	{
		data.resize(static_cast<std::size_t>(entry->size));
		if (m_source->read(*entry, 0, data.data(), data.size()) != data.size())
			return nullptr;
	}
	else
	{
		const Engine::ZipArchiveError error = static_cast<const Engine::Vfs::ZipSource *>(m_source.get())->readEntry(*entry, data);
		if (error != Engine::ZipArchiveError::None)
		{
			char line[MAX_PATH * 2 + 128];
			WWFormat::Into(line, "MOD zip {} eintrag {} nicht lesbar: {}", m_path.str(), entry->stored.c_str(),
				Engine::zipArchiveErrorText(error));
			line[sizeof(line) - 1] = 0;
			runnerLog(line);
			return nullptr;
		}
	}

	ramFile = newInstance(RAMFile);
	ramFile->deleteOnClose();
	if (!ramFile->openFromMemory(AsciiString(filename), data.data(), static_cast<Int>(data.size())))
	{
		ramFile->close();
		return nullptr;
	}
	return finishOpen(ramFile, filename, access);
}

File *VfsArchiveFile::finishOpen(RAMFile *ramFile, const Char *filename, Int access)
{
	if ((access & File::WRITE) == 0)
		return ramFile;

	// Write access: copy the entry to the local disk and hand out that file.
	File *localFile = TheLocalFileSystem->openFile(filename, access);
	if (localFile != nullptr)
		ramFile->copyDataToFile(localFile);
	ramFile->close();
	return localFile;
}
