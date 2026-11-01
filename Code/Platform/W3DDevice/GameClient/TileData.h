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

// TileData.h
// Class to hold 1 tile's data.
// Author: John Ahlquist, April 2001

#pragma once

#ifndef TileData_H
#define TileData_H

#include <stdio.h>

#include "Lib/BaseType.h"
#include "WWLib/refcount.h"
#include "Common/AsciiString.h"

typedef struct {
	Int blendNdx;
	UnsignedByte horiz;
	UnsignedByte vert;
	UnsignedByte rightDiagonal;
	UnsignedByte leftDiagonal;
	UnsignedByte inverted;
	UnsignedByte longDiagonal;
	Int customBlendEdgeClass; // Class of texture for a blend edge.  -1 means use alpha. 
} TBlendTileInfo;

constexpr auto INVERTED_MASK = 0x1;		//AND this with TBlendTileInfo.inverted to get actual inverted state
constexpr auto FLIPPED_MASK = 0x2;		//AND this with TBlendTileInfo.inverted to get forced flip state (for horizontal/vertical flips).
#define TILE_PIXEL_EXTENT 64
#define TILE_BYTES_PER_PIXEL 4
#define DATA_LEN_BYTES TILE_PIXEL_EXTENT*TILE_PIXEL_EXTENT*TILE_BYTES_PER_PIXEL
#define DATA_LEN_PIXELS TILE_PIXEL_EXTENT*TILE_PIXEL_EXTENT
#define TILE_PIXEL_EXTENT_MIP1 32
#define TILE_PIXEL_EXTENT_MIP2 16
#define TILE_PIXEL_EXTENT_MIP3 8
#define TILE_PIXEL_EXTENT_MIP4 4
#define TILE_PIXEL_EXTENT_MIP5 2
#define TILE_PIXEL_EXTENT_MIP6 1
constexpr auto TEXTURE_WIDTH = 2048; // was 1024 jba

/** This class holds the bitmap data from the .tga texture files.  It is used to
create the D3D texture in the game and 3d windows, and to create DIB data for the
2d window. */
// Tiles may have higher resolution. TILE_PIXEL_EXTENT (64) stays the LOGICAL tile size, which drives atlas layout
// and texture coordinates (some are stored in map files, e.g. cliff UVs). Physically a tile stores 64*s pixels
// (s = 1, 2 or 4) as in its file (TGA tag "ZHTS<s>", see WorldHeightMap::readTiles). Code that builds an atlas
// brings all tiles to the largest size with resample() and makes the atlas physically s times larger while its
// logical size stays. Without a tag s = 1.
class TileData : public RefCountClass
{
protected:

	// data is bgrabgrabgra to be compatible with windows blt. jba.
	// Also, first byte is lower left pixel, not upper left pixel.
	// so 0,0 is lower left, not upper left.
	// Size depends on the tile scale, hence allocated dynamically.
	enum { MAX_MIPS = 12 };
	Int				m_extent;						///< edge length in pixels (TILE_PIXEL_EXTENT*s)
	UnsignedByte	*m_tileData;
	/// Mipped down copies of the tile data, m_mips[0] = m_extent/2 ... 1x1.
	UnsignedByte	*m_mips[MAX_MIPS];
	Int				m_numMips;

	void allocate(Int extent);
	void release(void);

public:
	ICoord2D	m_tileLocationInTexture;


protected:
	/** doMip - generates the next mip level mipping pHiRes down to pLoRes.
				pLoRes is 1/2 the width of pHiRes, and both are square. */
	static void doMip(UnsignedByte *pHiRes, Int hiRow, UnsignedByte *pLoRes);



public:
	TileData(Int extent = TILE_PIXEL_EXTENT);
	virtual ~TileData(void);

public:
	UnsignedByte *getDataPtr(void) {return(m_tileData);};
	Int getExtent(void) const {return(m_extent);};	///< edge length of this tile in pixels
	Int dataLen(void) const {return(m_extent*m_extent*TILE_BYTES_PER_PIXEL);};

	void updateMips(void);

	Bool hasRGBDataForWidth(Int width);
	UnsignedByte *getRGBDataForWidth(Int width);

	/// Resamples to another edge length (upscale bilinear, downscale averaged; nearest = nearest pixel, for
	/// edge tiles with exact black/white values). Rebuilds the mips.
	void resample(Int extent, Bool nearest);
	/// Brings count tiles (nullptr allowed) to the largest edge length present; nearest as above.
	/// Returns the scale (edge length/TILE_PIXEL_EXTENT, at least 1).
	static Int unifyExtent(TileData **tiles, Int count, Bool nearest, Int minExtent = TILE_PIXEL_EXTENT);
	static Bool isValidExtent(Int extent) {return(extent == TILE_PIXEL_EXTENT || extent == 2*TILE_PIXEL_EXTENT || extent == 4*TILE_PIXEL_EXTENT);};
};

#endif