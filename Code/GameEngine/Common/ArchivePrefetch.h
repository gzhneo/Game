/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ArchivePrefetch.h ----------------------------------------------------------------------------
//
// The first map load of a session is dominated by the cold OS file cache, not by map setup:
// textures and models are read from the archives on disk. This background thread reads what the
// file system index serves (ArchiveFileSystem::getPrefetchJobs: mods, free content, texture package,
// game archives, highest first; no sound) once during engine init and the menu, so it is cached when
// the first map loads. It touches no engine state, only its own file handles and buffer. Disable
// with Settings.ini "PrefetchArchives = no".
// ----------------------------------------------------------------------------------------------

#pragma once

#ifndef __ARCHIVEPREFETCH_H_
#define __ARCHIVEPREFETCH_H_

/// Takes the read-ahead plan from TheArchiveFileSystem and starts the thread; call after all archives are mounted.
void ArchivePrefetch_Start( void );

/// Call from the main thread each tick: writes the statistics to the logs once the thread has finished.
void ArchivePrefetch_Poll( void );

/// Signals cancellation and waits briefly for the thread to end (for shutdown).
void ArchivePrefetch_Stop( void );

#endif // __ARCHIVEPREFETCH_H_
