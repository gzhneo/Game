/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// VfsArchiveFile.h -- a .big or .zip archive or the loose files of a folder behind the ArchiveFile
// interface. The contents come from an Engine::Vfs source; files of a folder are opened from disk,
// entries of an archive are read (a .zip entry unpacked) into a RAMFile.

#pragma once

#ifndef VFSARCHIVEFILE_H
#define VFSARCHIVEFILE_H

#include "Common/ArchiveFile.h"

#include <memory>
#include <vector>

class RAMFile;

class VfsArchiveFile : public ArchiveFile
{
public:
	/// Opens a .big archive from disk. nullptr if it cannot be read or its directory is damaged; a damaged
	/// game archive stops a debug build, a damaged mod archive (quiet) only sets lastOpenError.
	static VfsArchiveFile *openBig(const AsciiString &path, Bool quiet = FALSE);

	/// Opens a .zip archive from disk. nullptr if it is damaged; the reason and every entry left
	/// out go to the runner log.
	static VfsArchiveFile *openZip(const AsciiString &path);

	/// Why the last openBig or openZip returned nullptr (English, for the logs and the mod hint).
	static const char *lastOpenError();
	/// Entries of the archive that are not served (a .zip leaves out what it cannot read).
	Int getSkippedCount() const;

	/// Loose files of a folder. folder ends with a separator; relativePaths lie below it.
	static VfsArchiveFile *openFolder(const AsciiString &folder, const std::vector<AsciiString> &relativePaths);

	virtual ~VfsArchiveFile() override;

	virtual Bool getFileInfo(const AsciiString &filename, FileInfo *fileInfo) const override;
	virtual File *openFile(const Char *filename, Int access = 0) override;
	virtual void closeAllFiles(void) override {}
	virtual AsciiString getName(void) override { return m_path; }
	virtual AsciiString getPath(void) override { return m_path; }
	virtual void setSearchPriority(Int) override {}
	virtual void close(void) override {}
	virtual AsciiString getSourceForFile(const AsciiString &archiveName, const AsciiString &filename) const override;
	virtual const Engine::Vfs::Source *getVfsSource(void) const override { return m_source.get(); }

	Int getFileCount(void) const;

private:
	enum class Kind
	{
		Big,
		Zip,
		Folder
	};

	VfsArchiveFile(const AsciiString &path, Kind kind, std::unique_ptr<Engine::Vfs::Source> source);
	void fillDirectoryTree(void);
	static File *finishOpen(RAMFile *ramFile, const Char *filename, Int access);	///< the entry for reading, or a copy on disk for writing

	AsciiString m_path;		///< archive file, or folder with a trailing separator
	Kind m_kind;
	std::unique_ptr<Engine::Vfs::Source> m_source;
};

#endif // VFSARCHIVEFILE_H
