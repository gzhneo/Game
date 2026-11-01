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

// TileData.cpp
// Class to handle tile data.
// Author: John Ahlquist, April 2001

#include "W3DDevice/GameClient/TileData.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include <math.h>	// floorf in TileData::resample



//
// TileData - no destructor.
//

//
// TileData - create a new texture tile .
//
// Edge length per tile, see TileData.h.
TileData::TileData(Int extent) :
	m_extent(0),
	m_tileData(nullptr),
	m_numMips(0)
{
	for (Int i = 0; i < MAX_MIPS; i++) {
		m_mips[i] = nullptr;
	}
	allocate(isValidExtent(extent) ? extent : TILE_PIXEL_EXTENT);
}

TileData::~TileData()
{
	release();
}

void TileData::allocate(Int extent)
{
	// Memory is left uninitialized; readTiles writes every pixel.
	m_extent = extent;
	m_tileData = new UnsignedByte[m_extent*m_extent*TILE_BYTES_PER_PIXEL];
	Int w = m_extent/2;
	while (w >= 1 && m_numMips < MAX_MIPS) {
		m_mips[m_numMips++] = new UnsignedByte[w*w*TILE_BYTES_PER_PIXEL];
		w /= 2;
	}
}

void TileData::release(void)
{
	delete [] m_tileData;
	m_tileData = nullptr;
	for (Int i = 0; i < m_numMips; i++) {
		delete [] m_mips[i];
		m_mips[i] = nullptr;
	}
	m_numMips = 0;
	m_extent = 0;
}

void TileData::resample(Int extent, Bool nearest)
{
	if (extent == m_extent || !isValidExtent(extent)) return;
	const Int src = m_extent;
	UnsignedByte *old = m_tileData;
	m_tileData = nullptr;
	for (Int i = 0; i < m_numMips; i++) {
		delete [] m_mips[i];
		m_mips[i] = nullptr;
	}
	m_numMips = 0;
	allocate(extent);
	const Int dst = m_extent;
	for (Int y = 0; y < dst; y++) {
		for (Int x = 0; x < dst; x++) {
			UnsignedByte *out = m_tileData + (y*dst + x)*TILE_BYTES_PER_PIXEL;
			if (nearest) {
				const UnsignedByte *p = old + ((y*src/dst)*src + x*src/dst)*TILE_BYTES_PER_PIXEL;
				for (Int c = 0; c < TILE_BYTES_PER_PIXEL; c++) out[c] = p[c];
			} else if (src > dst) {
				const Int f = src/dst;		// integer: 2 or 4
				for (Int c = 0; c < TILE_BYTES_PER_PIXEL; c++) {
					Int acc = 0;
					for (Int dy = 0; dy < f; dy++) {
						for (Int dx = 0; dx < f; dx++) {
							acc += old[((y*f+dy)*src + (x*f+dx))*TILE_BYTES_PER_PIXEL + c];
						}
					}
					out[c] = (UnsignedByte)((acc + f*f/2)/(f*f));
				}
			} else {
				// bilinear, sample point in source pixels, clamped at the tile edge
				const Real sx = ((Real)x + 0.5f)*src/dst - 0.5f;
				const Real sy = ((Real)y + 0.5f)*src/dst - 0.5f;
				Int x0 = (Int)floorf(sx), y0 = (Int)floorf(sy);
				const Real fx = sx - x0, fy = sy - y0;
				Int x1 = x0 + 1, y1 = y0 + 1;
				if (x0 < 0) x0 = 0;
				if (y0 < 0) y0 = 0;
				if (x1 > src-1) x1 = src-1;
				if (y1 > src-1) y1 = src-1;
				const UnsignedByte *p00 = old + (y0*src + x0)*TILE_BYTES_PER_PIXEL;
				const UnsignedByte *p10 = old + (y0*src + x1)*TILE_BYTES_PER_PIXEL;
				const UnsignedByte *p01 = old + (y1*src + x0)*TILE_BYTES_PER_PIXEL;
				const UnsignedByte *p11 = old + (y1*src + x1)*TILE_BYTES_PER_PIXEL;
				for (Int c = 0; c < TILE_BYTES_PER_PIXEL; c++) {
					const Real v = (p00[c]*(1-fx) + p10[c]*fx)*(1-fy) + (p01[c]*(1-fx) + p11[c]*fx)*fy;
					out[c] = (UnsignedByte)(v + 0.5f);
				}
			}
		}
	}
	delete [] old;
	updateMips();
}

Int TileData::unifyExtent(TileData **tiles, Int count, Bool nearest, Int minExtent)
{
	Int extent = minExtent;
	for (Int i = 0; i < count; i++) {
		if (tiles[i] && tiles[i]->m_extent > extent) extent = tiles[i]->m_extent;
	}
	for (Int i = 0; i < count; i++) {
		if (tiles[i]) tiles[i]->resample(extent, nearest);
	}
	return extent/TILE_PIXEL_EXTENT;
}

Bool TileData::hasRGBDataForWidth(Int width)
{
	for (Int w = m_extent; w >= 1; w /= 2) {
		if (width == w) return(true);
	}
	return(false);
}

UnsignedByte * TileData::getRGBDataForWidth(Int width)
{
	// default: full size
	Int w = m_extent/2;
	for (Int i = 0; i < m_numMips; i++, w /= 2) {
		if (width == w) return(m_mips[i]);
	}
	return(m_tileData);
}

void TileData::updateMips(void)
{
	UnsignedByte *hi = m_tileData;
	Int w = m_extent;
	for (Int i = 0; i < m_numMips; i++) {
		doMip(hi, w, m_mips[i]);
		hi = m_mips[i];
		w /= 2;
	}
}


void TileData::doMip(UnsignedByte *pHiRes, Int hiRow, UnsignedByte *pLoRes) 
{
	Int i, j;
	for (i=0; i<hiRow; i+=2) {
		for (j=0; j<hiRow; j+=2) {
			Int pxl;
			Int ndx = (j*hiRow+i)*TILE_BYTES_PER_PIXEL;
			Int loNdx = (j/2)*(hiRow/2) + (i/2);
			loNdx *= TILE_BYTES_PER_PIXEL;
			Int p;
			for (p=0; p<TILE_BYTES_PER_PIXEL; p++,ndx++,loNdx++) {
				pxl = pHiRes[ndx] + pHiRes[ndx+TILE_BYTES_PER_PIXEL] + pHiRes[ndx+TILE_BYTES_PER_PIXEL*hiRow] + pHiRes[ndx+TILE_BYTES_PER_PIXEL*hiRow+TILE_BYTES_PER_PIXEL] +2;
				pxl /= 4;
				pLoRes[loNdx] = static_cast<UnsignedByte>(pxl);
			}
		}
	}

}

