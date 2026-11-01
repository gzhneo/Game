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

///////// Win32LocalFileSystem.cpp /////////////////////////
// Bryan Cleveland, August 2002
////////////////////////////////////////////////////////////

#include <windows.h>
#include "Common/AsciiString.h"
#include "Common/GameMemory.h"
#include "Common/PerfTimer.h"
#include "Win32Device/Common/Win32LocalFileSystem.h"
#include "Win32Device/Common/Win32LocalFile.h"
#include <io.h>
#include "Engine/UserDataFiles.h"

Win32LocalFileSystem::Win32LocalFileSystem() : LocalFileSystem() 
{
}

Win32LocalFileSystem::~Win32LocalFileSystem() {
}

//DECLARE_PERF_TIMER(Win32LocalFileSystem_openFile)
File * Win32LocalFileSystem::openFile(const Char *filename, Int access /* = 0 */) 
{
	//USE_PERF_TIMER(Win32LocalFileSystem_openFile)
	// sanity check -- before creating the file object so an empty name cannot leak it
	// (audio events without a sound file ask for an empty name)
	if (strlen(filename) <= 0) {
		return nullptr;
	}

	Win32LocalFile *file = newInstance( Win32LocalFile );	

	if (access & File::WRITE) {
		// if opening the file for writing, we need to make sure the directory is there
		// before we try to create the file.
		AsciiString string;
		string = filename;
		AsciiString token;
		AsciiString dirName;
		string.nextToken(&token, "\\/");
		dirName = token;
		while ((token.find('.') == nullptr) || (string.find('.') != nullptr)) {
			createDirectory(dirName);
			string.nextToken(&token, "\\/");
			dirName.concat('\\');
			dirName.concat(token);
		}
	}

	if (file->open(filename, access) == FALSE) {
		file->close();
		deleteInstanceOf(file);
		file = nullptr;
	} else {
		file->deleteOnClose();
	}

// this will also need to play nice with the STREAMING type that I added, if we ever enable this

// srj sez: this speeds up INI loading, but makes BIG files unusable. 
// don't enable it without further tweaking.
//
// unless you like running really slowly.
//	if (!(access&File::WRITE)) {
//		// Return a ramfile.
//		RAMFile *ramFile = newInstance( RAMFile );
//		if (ramFile->open(file)) {
//			file->close(); // is deleteonclose, so should delete.
//			ramFile->deleteOnClose();
//			return ramFile;
//		}	else {
//			ramFile->close();
//			deleteInstanceOf(ramFile);
//		}
//	}

	return file;
}

void Win32LocalFileSystem::update() 
{
}

void Win32LocalFileSystem::init() 
{
}

void Win32LocalFileSystem::reset() 
{
}

//DECLARE_PERF_TIMER(Win32LocalFileSystem_doesFileExist)
Bool Win32LocalFileSystem::doesFileExist(const Char *filename) const
{
	//USE_PERF_TIMER(Win32LocalFileSystem_doesFileExist)
	if (_waccess(::Engine::pathFromNarrow(filename).c_str(), 0) == 0) {
		return TRUE;
	}
	return FALSE;
}

void Win32LocalFileSystem::getFileListInDirectory(const AsciiString& currentDirectory, const AsciiString& originalDirectory, const AsciiString& searchName, FilenameList & filenameList, Bool searchSubdirectories) const
{
	// Wide calls: narrow paths and names may be UTF-8 (see Engine/UserDataFiles.h).
	HANDLE fileHandle = nullptr;
	WIN32_FIND_DATAW findData;

	AsciiString asciisearch;
	asciisearch = originalDirectory;
	asciisearch.concat(currentDirectory);
	asciisearch.concat(searchName);

	Bool done = FALSE;

	fileHandle = FindFirstFileW(::Engine::pathFromNarrow(asciisearch.str()).c_str(), &findData);
	done = (fileHandle == INVALID_HANDLE_VALUE);

	while (!done)	{
		if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
				(wcscmp(findData.cFileName, L".") && wcscmp(findData.cFileName, L".."))) {
			// if we haven't already, add this filename to the list.
				// a stl set should only allow one copy of each filename
				AsciiString newFilename;
				newFilename = originalDirectory;
				newFilename.concat(currentDirectory);
				newFilename.concat(::Engine::narrowFromPath(findData.cFileName).c_str());
				if (filenameList.find(newFilename) == filenameList.end()) {
					filenameList.insert(newFilename);
				}
		}

		done = (FindNextFileW(fileHandle, &findData) == 0);
	}
	FindClose(fileHandle);

	if (searchSubdirectories) {
		AsciiString subdirsearch;
		subdirsearch = originalDirectory;
		subdirsearch.concat(currentDirectory);
		subdirsearch.concat("*.");
		fileHandle = FindFirstFileW(::Engine::pathFromNarrow(subdirsearch.str()).c_str(), &findData);
		done = fileHandle == INVALID_HANDLE_VALUE;

		while (!done) {
			if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
					(wcscmp(findData.cFileName, L".") && wcscmp(findData.cFileName, L".."))) {

					AsciiString tempsearchstr;
					tempsearchstr.concat(currentDirectory);
					tempsearchstr.concat(::Engine::narrowFromPath(findData.cFileName).c_str());
					tempsearchstr.concat('\\');
					
					// recursively add files in subdirectories if required.
					getFileListInDirectory(tempsearchstr, originalDirectory, searchName, filenameList, searchSubdirectories);
			}

			done = (FindNextFileW(fileHandle, &findData) == 0);
		}

		FindClose(fileHandle);
	}

}

Bool Win32LocalFileSystem::getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const 
{
	WIN32_FIND_DATAW findData;
	HANDLE findHandle = nullptr;
	findHandle = FindFirstFileW(::Engine::pathFromNarrow(filename.str()).c_str(), &findData);

	if (findHandle == INVALID_HANDLE_VALUE) {
		return FALSE;
	}

	fileInfo->timestampHigh = findData.ftLastWriteTime.dwHighDateTime;
	fileInfo->timestampLow = findData.ftLastWriteTime.dwLowDateTime;
	fileInfo->sizeHigh = findData.nFileSizeHigh;
	fileInfo->sizeLow = findData.nFileSizeLow;

	FindClose(findHandle);

	return TRUE;
}

Bool Win32LocalFileSystem::createDirectory(AsciiString directory) 
{
	if ((directory.getLength() > 0) && (directory.getLength() < _MAX_DIR)) {
		return (CreateDirectoryW(::Engine::pathFromNarrow(directory.str()).c_str(), nullptr) != 0);
	}
	return FALSE;
}
