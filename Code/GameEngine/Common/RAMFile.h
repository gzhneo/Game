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
// Project:    WSYS Library
//
// Module:     IO
//
// File name:  wsys/RAMFile.h
//
// Created:    11/08/01
//
//----------------------------------------------------------------------------

#pragma once

#ifndef __RAMFILE_H
#define __RAMFILE_H



//----------------------------------------------------------------------------
//           Includes                                                      
//----------------------------------------------------------------------------

#include "Common/file.h"
#include <functional>
#include <memory>

//----------------------------------------------------------------------------
//           Forward References
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//           Type Defines
//----------------------------------------------------------------------------

//===============================
// RAMFile
//===============================
/**
  *	File abstraction for standard C file operators: open, close, lseek, read, write
	*/
//===============================

class RAMFile : public File
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(RAMFile, "RAMFile")		
	protected:

		std::unique_ptr<Char[]>	m_data;											///< File data in memory
		Int					m_pos;												///< current read position
		Int					m_size;												///< size of file in memory
		
	public:
		
		RAMFile();
		//virtual				~RAMFile();


		virtual Bool	open( const Char *filename, Int access = 0 ) override;				///< Open a file for access
		virtual void	close( void ) override;																			///< Close the file
		virtual Int		read( void *buffer, Int bytes ) override;										///< Read the specified number of bytes in to buffer: See File::read
		virtual Int		write( const void *buffer, Int bytes ) override;							///< Write the specified number of bytes from the buffer: See File::write
		virtual Int		seek( Int new_pos, seekMode mode = seekMode::CURRENT ) override;				///< Set file position: See File::seek
		virtual void	nextLine(Char *buf = nullptr, Int bufSize = 0) override;				///< moves current position to after the next new-line

		virtual Bool	scanInt(Int &newInt) override;																///< return what gets read as an integer from the current memory position.
		virtual Bool	scanReal(Real &newReal) override;														///< return what gets read as a float from the current memory position.
		virtual Bool	scanString(AsciiString &newString) override;									///< return what gets read as a string from the current memory position.

		virtual Bool	open( File *file );																	///< Open file for fast RAM access
		Bool					openFromMemory(const AsciiString& filename, const void *data, Int size);		///< copy the given bytes (an unpacked archive entry)
		Bool					openFromReader(const AsciiString& filename, Int size, const std::function<Bool(void *dest)> &fill);	///< size bytes written by fill into the file's buffer
		virtual Bool	copyDataToFile(File *localFile);										///< write the contents of the RAM file to the given local file.  This could be REALLY slow.

		/**
			Allocate a buffer large enough to hold entire file, read 
			the entire file into the buffer, then close the file.
			the buffer is owned by the caller, who is responsible
			for freeing is (via delete[]). This is a Good Thing to
			use because it minimizes memory copies for BIG files.
		*/
		virtual char* readEntireAndClose() override;
		virtual File* convertToRAMFile() override;
};




//----------------------------------------------------------------------------
//           Inlining                                                       
//----------------------------------------------------------------------------


#endif // __WSYS_RAMFILE_H
