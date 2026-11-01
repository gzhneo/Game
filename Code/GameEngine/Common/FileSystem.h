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

//----------------------------------------------------------------------------=
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information					                  
//                Copyright(C) 2001 - All Rights Reserved                  
//                                                                          
//----------------------------------------------------------------------------
//
// Project:    GameEngine
//
// Module:     IO
//
// File name:  FileSystem.h
//
// Created:    
//
//----------------------------------------------------------------------------

#pragma once

#ifndef __FILESYSTEM_H
#define __FILESYSTEM_H

//----------------------------------------------------------------------------
//           Includes                                                      
//----------------------------------------------------------------------------

//#include "Common/File.h"
#include "Common/STLTypedefs.h"
#include "Common/SubsystemInterface.h"

//----------------------------------------------------------------------------
//           Forward References
//----------------------------------------------------------------------------
class File;

//----------------------------------------------------------------------------
//           Type Defines
//----------------------------------------------------------------------------

using FilenameList = std::set<AsciiString, rts::less_than_nocase<AsciiString> >;
using FilenameListIter = FilenameList::iterator;

//----------------------------------------------------------------------------
//           Type Defines
//----------------------------------------------------------------------------
//#define W3D_DIR_PATH "../FinalArt/W3D/"					///< .w3d files live here
//#define TGA_DIR_PATH "../FinalArt/Textures/"		///< .tga texture files live here
//#define TERRAIN_TGA_DIR_PATH "../FinalArt/Terrain/"		///< terrain .tga texture files live here
constexpr char W3D_DIR_PATH[] = "Art/W3D/";					///< .w3d files live here
constexpr char TGA_DIR_PATH[] = "Art/Textures/";		///< .tga texture files live here
constexpr char TERRAIN_TGA_DIR_PATH[] = "Art/Terrain/";		///< terrain .tga texture files live here
constexpr char MAP_PREVIEW_DIR_NAME[] = "MapPreviews/";	///< We need a common place we can copy the map previews to at runtime.
constexpr char USER_W3D_DIR_NAME[] = "W3D/";					///< .w3d files live here
constexpr char USER_TGA_DIR_NAME[] = "Textures/";		///< User .tga texture files live here

// the following defines are only to be used while maintaining legacy compatability
// with old files until they are completely gone and in the regular art set
#ifdef MAINTAIN_LEGACY_FILES
#define LEGACY_W3D_DIR_PATH "../LegacyArt/W3D/"				///< .w3d files live here
#define LEGACY_TGA_DIR_PATH "../LegacyArt/Textures/"	///< .tga texture files live here
#endif  // MAINTAIN_LEGACY_FILES

// LOAD_TEST_ASSETS automatically loads w3d assets from the TEST_W3D_DIR_PATH
// without having to add an INI entry.
///@todo this allows us to use the test art directory, it should be removed for FINAL release
#define LOAD_TEST_ASSETS 1
#ifdef LOAD_TEST_ASSETS
	#define ROAD_DIRECTORY		"../TestArt/TestRoad/"
	#define TEST_STRING				"***TESTING"
// the following directories will be used to look for test art
#define LOOK_FOR_TEST_ART
#define TEST_W3D_DIR_PATH "../TestArt/"					///< .w3d files live here
#define TEST_TGA_DIR_PATH "../TestArt/"		///< .tga texture files live here
#endif

struct FileInfo {
	Int sizeHigh;
	Int sizeLow;
	Int timestampHigh;
	Int timestampLow;
};

//===============================
// FileSystem
//===============================
/**
  * FileSystem is an interface class for creating specific FileSystem objects.
  * 
	* A FileSystem object's implemenation decides what derivative of File object needs to be 
	* created when FileSystem::Open() gets called.
	*/
//===============================
#include <map>
#include <string>

class FileSystem : public SubsystemInterface
{
  FileSystem(const FileSystem&);
  FileSystem& operator=(const FileSystem&);
  
public:
	FileSystem();
	virtual	~FileSystem() override;

	void init() override;
	void reset() override;
	void update() override;

	File* openFile( const Char *filename, Int access = 0 );		///< opens a File interface to the specified file
	Bool doesFileExist(const Char *filename) const;								///< returns TRUE if the file exists.  filename should have no directory.
	void getFileListInDirectory(const AsciiString& directory, const AsciiString& searchName, FilenameList &filenameList, Bool searchSubdirectories) const; ///< search the given directory for files matching the searchName (egs. *.ini, *.rep).  Possibly search subdirectories.
	Bool getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const; ///< fills in the FileInfo struct for the file given. returns TRUE if successful.

	Bool createDirectory(AsciiString directory); ///< create a directory of the given name.
};

#include "Engine/EngineContext.h"



//----------------------------------------------------------------------------
//           Inlining                                                       
//----------------------------------------------------------------------------



#endif // __WSYS_FILESYSTEM_H
