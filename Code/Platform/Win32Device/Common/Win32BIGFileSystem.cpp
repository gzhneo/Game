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

//////// Win32BIGFileSystem.h ///////////////////////////
// Bryan Cleveland, August 2002
/////////////////////////////////////////////////////////////

#include "Common/AudioAffect.h"
#include "Common/ArchiveFile.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/file.h"
#include "Common/GameAudio.h"
#include "Common/GameMemory.h"
#include "Common/LocalFileSystem.h"
#include "Win32Device/Common/Win32BIGFileSystem.h"
#include "Common/InstallInfo.h"
#include "Common/GameEdition.h"
#include "Common/VfsArchiveFile.h"

#include <vector>


Win32BIGFileSystem::Win32BIGFileSystem() : ArchiveFileSystem() {
}

Win32BIGFileSystem::~Win32BIGFileSystem() {
}

void Win32BIGFileSystem::init() {
	DEBUG_ASSERTCRASH(TheLocalFileSystem != nullptr, ("TheLocalFileSystem must be initialized before TheArchiveFileSystem."));
	if (TheLocalFileSystem == nullptr) {
		return;
	}

	loadBigFilesFromDirectory("", "*.big");

	// Generals has no base archives from another game.
	if (!isGameEditionZeroHour())
		return;

	// Generals base archives for Zero Hour, in this order:
	//  1. -baseGame <folder> on the command line (read directly from __argv because
	//     parseCommandLine runs after TheArchiveFileSystem; CommandLine.cpp only skips the switch),
	//  2. subfolder ZH_Generals next to the exe. It is already loaded above: the search is
	//     recursive, the list is sorted case-insensitively, "ZH_Generals\..." comes after all
	//     ZH archives, and the first match of a file wins.
	for (Int i = 1; i + 1 < __argc; ++i) {
		if (stricmp(__argv[i], "-baseGame") == 0) {
			AsciiString baseDir = __argv[i + 1];
			if (!baseDir.endsWith("\\") && !baseDir.endsWith("/"))
				baseDir.concat('\\');
			DEBUG_LOG(("Win32BIGFileSystem::init - Generals-Basisarchive aus -baseGame %s\n", baseDir.str()));
			[[maybe_unused]] Bool found = loadBigFilesFromDirectory(baseDir, "*.big");	// only used in the assert (Release: C4189)
			DEBUG_ASSERTCRASH(found, ("-baseGame %s: no .big archives found", baseDir.str()));
			return;
		}
	}
	if (TheLocalFileSystem->doesFileExist("ZH_Generals")) {
		DEBUG_LOG(("Win32BIGFileSystem::init - Generals base archives loaded from ZH_Generals, registry InstallPath ignored\n"));
		return;
	}

    DEBUG_LOG(("Win32BIGFileSystem::init - no Generals base archives found (neither -baseGame nor ZH_Generals)\n"));
}

void Win32BIGFileSystem::reset() {
}

void Win32BIGFileSystem::update() {
}

void Win32BIGFileSystem::postProcessLoad() {
}

// Archives come from the game, from mods and from other players; the directory is checked by
// Engine::readBigArchiveDirectory (VfsArchiveFile::openBig), and a damaged archive is not used at all.
ArchiveFile * Win32BIGFileSystem::openArchiveFile(const Char *filename) {
	// .zip comes only from mods; a damaged one is reported in the runner log, not as a crash.
	const AsciiString name = filename;
	if (name.endsWithNoCase(".zip"))
		return VfsArchiveFile::openZip(name);
	return VfsArchiveFile::openBig(name, m_loadingMods);
}

void Win32BIGFileSystem::closeArchiveFile(const Char *filename) {
	// Need to close the specified big file
	ArchiveFileMap::iterator it =  m_archiveFileMap.find(filename);
	if (it == m_archiveFileMap.end()) {
		return;
	}

	if (stricmp(filename, MUSIC_BIG) == 0) {
		// Stop the current audio
		TheAudio->stopAudio(AudioAffect_Music);

		// No need to turn off other audio, as the lookups will just fail.
	}
	DEBUG_ASSERTCRASH(stricmp(filename, MUSIC_BIG) == 0, ("Attempting to close Archive file '%s', need to add code to handle its shutdown correctly.", filename));

	// may need to do some other processing here first.
	
	// A lookup on another thread may still hold a snapshot with this archive, so it stays alive.
	m_index.removeOwner(it->second.get());
	m_replacedArchiveFiles.push_back(std::move(it->second));
	m_archiveFileMap.erase(it);
}

void Win32BIGFileSystem::closeAllArchiveFiles() {
}

void Win32BIGFileSystem::closeAllFiles() {
}

Bool Win32BIGFileSystem::loadBigFilesFromDirectory(AsciiString dir, AsciiString fileMask, Bool overwrite) {

	// fileMask may name several patterns separated by ';'; the archives of all of them are loaded
	// together in name order.
	FilenameList filenameList;
	AsciiString masks = fileMask;
	AsciiString mask;
	while (masks.nextToken(&mask, ";"))
		TheLocalFileSystem->getFileListInDirectory(dir, AsciiString(""), mask, filenameList, TRUE);

	// The texture tier folder is skipped by the recursive search; otherwise its archives would enter the tree
	// alphabetically before Textures*.big without priority. ArchiveFileSystem::loadTextureTiers loads only the
	// selected tier, with priority.
	const Bool skipTiers = !isTextureTierPath(dir);

	Bool actuallyAdded = FALSE;
	FilenameListIter it = filenameList.begin();
	while (it != filenameList.end()) {
		if (skipTiers && isTextureTierPath(*it)) {
			it++;
			continue;
		}
		ArchiveFile *archiveFile = openArchiveFile((*it).str());

		if (archiveFile != nullptr) {
			DEBUG_LOG(("Win32BIGFileSystem::loadBigFilesFromDirectory - loading %s into the directory tree.\n", (*it).str()));
			loadIntoDirectoryTree(archiveFile, *it, overwrite);
			addArchiveFile(*it, archiveFile);
			DEBUG_LOG(("Win32BIGFileSystem::loadBigFilesFromDirectory - %s inserted into the archive file map.\n", (*it).str()));
			actuallyAdded = TRUE;
		}

		it++;
	}

	return actuallyAdded;
}
