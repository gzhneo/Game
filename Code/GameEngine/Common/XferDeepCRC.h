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

// FILE: XferDeepCRC.h ////////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, February 2002
// Desc:   Xfer hard disk read implementation
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __XFERDEEPCRC_H_
#define __XFERDEEPCRC_H_

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "Common/Xfer.h"
#include "Common/XferCRC.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class Snapshot;

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class XferDeepCRC : public XferCRC
{

public:

	XferDeepCRC( void );
	virtual ~XferDeepCRC( void ) override;

	// Xfer methods
	virtual void open( AsciiString identifier ) override;		///< start a CRC session with this xfer instance
	virtual void close( void ) override;											///< stop CRC session

	// xfer methods
	virtual void xferMarkerLabel( AsciiString asciiStringData ) override;  ///< xfer ascii string (need our own)
	virtual void xferAsciiString( AsciiString *asciiStringData ) override;  ///< xfer ascii string (need our own)
	virtual void xferUnicodeString( UnicodeString *unicodeStringData ) override;	///< xfer unicode string (need our own);

	// -deepCRCFull: write each object with full state depth (xfer() instead of crc()).
	// Without setFullState only crc() is used, like XferCRC::xferSnapshot.
	virtual void xferSnapshot( Snapshot *snapshot ) override;
	void setFullState( Bool full ) { m_fullState = full; }

	// Current write position, for the offset index of the deep CRC dump
	long getFileOffset( void ) const { return m_fileFP ? ftell( m_fileFP ) : -1; }

protected:

	virtual void xferImplementation( void *data, Int dataSize ) override;

	FILE * m_fileFP;																			///< pointer to file
	Bool m_fullState;																		///< -deepCRCFull: xfer() instead of crc() per snapshot
};

#endif // __XFERDEEPCRC_H_

