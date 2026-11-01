/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

/////// ArchiveFile.h ////////////////////
// Bryan Cleveland, August 2002
//////////////////////////////////////////

#pragma once

#ifndef __ARCHIVEFILE_H
#define __ARCHIVEFILE_H

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"
#include "Common/ArchiveFileSystem.h"

class File;

/**
  *	An archive file is itself a collection of sub files. Each file inside the archive file
	* has a unique name by which it can be accessed. The ArchiveFile object class is the
	* runtime interface to the mix file and the sub files. Each file inside the mix 
	* file can be accessed by the openFile().
	*
	* ArchiveFile interfaces can be created by the TheArchiveFileSystem object.
	*/
//===============================

namespace Engine
{
namespace Vfs
{
class Source;
}
}

class ArchiveFile
{
public:
	ArchiveFile();
	virtual ~ArchiveFile();

	virtual Bool					getFileInfo( const AsciiString& filename, FileInfo *fileInfo) const = 0;	///< fill in the fileInfo struct with info about the file requested.
	virtual File*					openFile( const Char *filename, Int access = 0) = 0;	///< Open the specified file within the archive file
	virtual void					closeAllFiles( void ) = 0;									///< Close all file opened in this archive file
	virtual AsciiString		getName( void ) = 0;												///< Returns the name of the archive file
	virtual AsciiString		getPath( void ) = 0;												///< Returns full path and name of archive file
	virtual void					setSearchPriority( Int new_priority ) = 0;	///< Set this archive file's search priority
	virtual void					close( void ) = 0;													///< Close this archive file
	/// Where the file comes from, for the index dump: the archive name, or the file itself for a folder.
	virtual AsciiString		getSourceForFile( const AsciiString& archiveName, const AsciiString& filename ) const { return archiveName; }
	/// The entries as an Engine::Vfs source, for the index of the archive file system; nullptr for the legacy BIG reader.
	virtual const Engine::Vfs::Source *getVfsSource( void ) const { return nullptr; }

	void									getFileListInDirectory(const AsciiString& currentDirectory, const AsciiString& originalDirectory, const AsciiString& searchName, FilenameList &filenameList, Bool searchSubdirectories) const;
	void									getFileListInDirectory(const DetailedArchivedDirectoryInfo *dirInfo, const AsciiString& currentDirectory, const AsciiString& searchName, FilenameList &filenameList, Bool searchSubdirectories) const;

	void									addFile(const AsciiString& path, const ArchivedFileInfo *fileInfo); ///< add this file to our directory tree.

protected:
	const ArchivedFileInfo *		getArchivedFileInfo(const AsciiString& filename) const;	///< return the ArchivedFileInfo from the directory tree.

	DetailedArchivedDirectoryInfo m_rootDirectory;
};

#endif // __ARCHIVEFILE_H