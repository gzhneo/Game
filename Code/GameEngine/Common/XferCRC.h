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

// FILE: XferCRC.h ////////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, February 2002
// Desc:   Xfer hard disk read implementation
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __XFERCRC_H_
#define __XFERCRC_H_

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "Common/Xfer.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class Snapshot;

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class XferCRC : public Xfer
{

public:

	XferCRC( void );
	virtual ~XferCRC( void ) override;

	// Xfer methods
	virtual void open( AsciiString identifier ) override;		///< start a CRC session with this xfer instance
	virtual void close( void ) override;											///< stop CRC session
	virtual Int beginBlock( void ) override;									///< start block event
	virtual void endBlock( void ) override;									///< end block event
	virtual void skip( Int dataSize ) override;							///< skip xfer event

	virtual void xferSnapshot( Snapshot *snapshot ) override;		///< entry point for xfering a snapshot

	// Xfer CRC methods
	virtual UnsignedInt getCRC( void );										///< get computed CRC in network byte order

protected:

	virtual void xferImplementation( void *data, Int dataSize ) override;

	void addCRC( UnsignedInt val );								///< CRC a 4-byte block

	UnsignedInt m_crc;

};

#endif // __XFERDISKWRITE_H_

