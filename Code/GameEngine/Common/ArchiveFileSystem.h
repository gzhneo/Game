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

//----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information					                  
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//----------------------------------------------------------------------------
//
// Project:    Generals
//
// Module:     Archive files
//
// File name:  Common/ArchiveFileSystem.h
//
// Created:    11/26/01 TR
//
//----------------------------------------------------------------------------

#pragma once

#ifndef __ARCHIVEFILESYSTEM_H_
#define __ARCHIVEFILESYSTEM_H_

constexpr char MUSIC_BIG[] = "Music.big";

//----------------------------------------------------------------------------
//           Includes                                                      
//----------------------------------------------------------------------------

#include "Common/SubsystemInterface.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "Common/AsciiString.h"
#include "Common/FileSystem.h" // for typedefs, etc.
#include "Common/STLTypedefs.h"
#include "Engine/Vfs.h"

//----------------------------------------------------------------------------
//           Forward References
//----------------------------------------------------------------------------

class File;
class ArchiveFile;

//----------------------------------------------------------------------------
//           Type Defines
//----------------------------------------------------------------------------


//===============================
// ArchiveFileSystem
//===============================
/**
  *	Creates and manages ArchiveFile interfaces. ArchiveFiles can be accessed
	* by calling the openArchiveFile() member. ArchiveFiles can be accessed by
	* name or by File interface.
	*
	* openFile() member searches all Archive files for the specified sub file.
	*/
//===============================
class DetailedArchivedDirectoryInfo;
class ArchivedFileInfo;

using DetailedArchivedDirectoryInfoMap = std::map<AsciiString, DetailedArchivedDirectoryInfo>;
using ArchivedFileInfoMap = std::map<AsciiString, ArchivedFileInfo>;
using ArchiveFileMap = std::map<AsciiString, std::unique_ptr<ArchiveFile>>;

class DetailedArchivedDirectoryInfo 
{
public:
	AsciiString												m_directoryName;
	DetailedArchivedDirectoryInfoMap	m_directories;
	ArchivedFileInfoMap								m_files;

	void clear()
	{
		m_directoryName.clear();
		m_directories.clear();
		m_files.clear();
	}
};

class ArchivedFileInfo 
{
public:
	AsciiString m_filename;
	AsciiString m_archiveFilename;
	UnsignedInt m_offset;
	UnsignedInt m_size;

	ArchivedFileInfo()
	{
		clear();
	}

	void clear()
	{
		m_filename.clear();
		m_archiveFilename.clear();
		m_offset = 0;
		m_size = 0;
	}
};


class ArchiveFileSystem : public SubsystemInterface
{
	public:
	ArchiveFileSystem();
	virtual ~ArchiveFileSystem() override;

	virtual void init( void ) override = 0;
	virtual void update( void ) override = 0;
	virtual void reset( void ) override = 0;
	virtual void postProcessLoad( void ) override = 0;

	// ArchiveFile operations
	virtual ArchiveFile*	openArchiveFile( const Char *filename ) = 0;		///< Create new or return existing Archive file from file name
	virtual void					closeArchiveFile( const Char *filename ) = 0;		///< Close the one specified big file.
	virtual void					closeAllArchiveFiles( void ) = 0;								///< Close all Archivefiles currently open

	// File operations
	virtual File*					openFile( const Char *filename, Int access = 0);	///< Search Archive files for specified file name and open it if found
	virtual void					closeAllFiles( void ) = 0;									///< Close all files associated with ArchiveFiles
	virtual Bool					doesFileExist(const Char *filename) const;		///< return true if that file exists in an archive file somewhere.

	void					getFileListInDirectory(const AsciiString& currentDirectory, const AsciiString& originalDirectory, const AsciiString& searchName, FilenameList &filenameList, Bool searchSubdirectories) const; ///< search the given directory for files matching the searchName (egs. *.ini, *.rep).  Possibly search subdirectories.  Scans each Archive file.
	Bool					getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const; ///< see FileSystem.h
	
	virtual Bool	loadBigFilesFromDirectory(AsciiString dir, AsciiString fileMask, Bool overwrite = FALSE) = 0;

	AsciiString						getArchiveFilenameForFile(const AsciiString& filename) const;
	/// Mods from -mod, the last one named on top (two mods, a folder and an archive: the folder on top),
	/// without -mod the active list of Settings.ini in its order. A mod is an archive (.big or .zip) or a
	/// folder: its archives (.big, .zip), above them its loose files, above those its texture package
	/// (Texturstufen\Paket) from HD on.
	void loadMods( void );

	/// A mod that did not load, or not completely; shown once in the main menu.
	enum class ModProblemKind
	{
		Missing,		///< not found (Mods folder or -mod path)
		OtherGame,		///< mod.txt names the other game
		Unreadable,		///< the archive is damaged; detail says why
		Partial,		///< count entries of an archive left out
		PackageRejected,///< count archives of its texture package rejected
		Manifest		///< mod.txt has unusable lines; the mod is loaded
	};
	struct ModProblem
	{
		AsciiString mod;		///< name as the player knows it (file or folder name)
		ModProblemKind kind;
		AsciiString detail;		///< the archive inside the mod it concerns, if any
		Int count;
	};
	/// logReason (English, technical) goes only to the logs; the hint shows translated texts.
	void addModProblem( const AsciiString& modPath, ModProblemKind kind, const AsciiString& detail, Int count = 0,
		const char *logReason = nullptr );
	const std::vector<ModProblem>& getModProblems( void ) const { return m_modProblems; }
	/// The loaded mods as games, replays and save games name them (Engine::encodeModTag, highest first);
	/// empty without mods.
	const AsciiString& getModTag( void ) const { return m_modTag; }

	/// TRUE if the file comes from a mod (a mod is its highest source); *diskPath (optional) gets its
	/// path on disk when it is a loose file of a mod folder, otherwise it is cleared.
	Bool isModFile( const AsciiString& filename, AsciiString *diskPath = nullptr ) const;
	/// Opens a file a mod provides from that mod, even if a loose file of the game folder has the same
	/// path (those rank above the index for everything else); nullptr if no mod provides it.
	File *openModFile( const Char *filename );

	/// Writes every path found in a loaded archive and the archive it resolves to, one line each,
	/// sorted. Used to compare the resolution of two file system implementations.
	Bool writeIndexDump( const char *path ) const;

	/// What the read-ahead thread reads (ArchivePrefetch): one job per archive, and per file of a mod
	/// folder; highest layer first, without sound (Engine::Vfs::prefetchPlan).
	struct PrefetchJob
	{
		std::string path;									///< on disk
		std::vector<Engine::Vfs::PrefetchRange> ranges;		///< empty: the whole file
		std::uint64_t bytes = 0;
	};
	std::vector<PrefetchJob> getPrefetchJobs( void ) const;

	// Texture tiers (TextureTiers.cpp). loadTextureTiers picks the tier; HD and UHD load the texture package
	// under Texturstufen\Paket with priority, and the tier only sets the mip offset of each texture
	// (Engine::TexturePackage). Without a package only SD exists. isTextureTierPath detects paths in the
	// Texturstufen folder, which the recursive startup scan skips.
	void loadTextureTiers( void );
	static Bool isTextureTierPath( const AsciiString& path );
	// Free content (TextureTiers.cpp): the packages under ProjectData\Inhalt, texture-only, above the texture
	// package and below the mods; a package with a manifest holds its highest version, scaled by the tier.
	void loadFreeContent( void );
	// After the free content and the mods: a texture whose file comes from a source whose manifest does not
	// name it loses its package entry (it is loaded and scaled as it is).
	void checkTexturePackageSources( void );

protected:
	ArchiveFile *openTexturePackageArchive( const AsciiString& archiveName );	///< nullptr = rejected; the manifest is read
	Bool loadModFolder( const AsciiString& folder );				///< loose files of a mod folder, with priority over everything loaded before
	Bool loadModArchives( const AsciiString& folder );				///< archives of a mod folder without its Texturstufen folder, in name order, each on top
	void loadModPackage( const AsciiString& folder );				///< archives of the mod's Texturstufen\Paket from HD on, on top
	Int loadFreeContentFolder( const AsciiString& folder, Int &manifestEntries );
	void noteSkippedEntries( const AsciiString& modPath, const ArchiveFile *archive, const AsciiString& archiveName );	///< a Partial problem when entries were left out	///< loose textures of a package folder; -1 = rejected

	void addArchiveFile( const AsciiString& archiveFilename, ArchiveFile *archiveFile );	///< takes ownership; a replaced archive file stays alive (the directory tree may still point into it)

	virtual void					loadIntoDirectoryTree(const ArchiveFile *archiveFile, const AsciiString& archiveFilename, Bool overwrite = FALSE );	///< mounts the archive for the index: with overwrite above all others, without it below them

	/// The layer that serves a file. Holds the snapshot of the index it was found in, so the layer stays
	/// valid while archives are mounted on another thread.
	struct Found
	{
		std::shared_ptr<const Engine::Vfs::Snapshot> snapshot;
		const Engine::Vfs::Layer *layer = nullptr;

		explicit operator bool() const { return layer != nullptr; }
		ArchiveFile *archive() const { return static_cast<ArchiveFile *>(const_cast<void *>(layer->owner)); }
		AsciiString name() const { return AsciiString(layer->name.c_str()); }	///< as given to loadIntoDirectoryTree
		Bool mod() const { return layer->flags != 0; }							///< loaded by loadMods
	};
	Found findLayer( const AsciiString& filename ) const;

	ArchiveFileMap m_archiveFileMap;
	std::vector<std::unique_ptr<ArchiveFile>> m_replacedArchiveFiles;	///< replaced or closed; a snapshot may still point into them

	Bool m_loadingMods = FALSE;
	std::vector<ModProblem> m_modProblems;
	AsciiString m_modTag;
	/// The loaded archives, highest first. An archive loaded with overwrite goes on top, one loaded without
	/// it below all others. Layer::owner is the ArchiveFile, Layer::flags 1 for a mod.
	Engine::Vfs::LiveIndex m_index;
};


#include "Engine/EngineContext.h"

//----------------------------------------------------------------------------
//           Inlining                                                       
//----------------------------------------------------------------------------

#endif // __ARCHIVEFILESYSTEM_H_
