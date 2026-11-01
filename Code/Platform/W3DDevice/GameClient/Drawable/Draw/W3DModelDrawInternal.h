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

// W3DModelDrawInternal.h -- what the W3DModelDraw*.cpp files share and nobody else needs.

#pragma once

// Asset faults are logged once per message (defined in W3DModelDraw.cpp).
#ifdef DEBUG_LOGGING
void assetErrorLog(const char *format, ...);
#else
#define assetErrorLog(...) ((void)0)
#endif

//-------------------------------------------------------------------------------------------------
static inline Bool isValidTimeToCalcLogicStuff()
{
	return (TheGameLogic && TheGameLogic->isInGameLogicUpdate()) ||
		(TheGameState && TheGameState->isInLoadGame());
}


#if defined(DEBUG_CRC) && defined(_DEBUG)
#include <cstdarg>
#include "formatinto.h"
class LogClass
{
public:
	LogClass(const char *fname);
	~LogClass();
	void log(const char *fmt, ...);
	void dumpMatrix3D(const Matrix3D *m, AsciiString name, AsciiString fname, Int line);
	void dumpReal(Real r, AsciiString name, AsciiString fname, Int line);

protected:
	FILE *m_fp;
};

extern LogClass BonePosLog;

#define BONEPOS_LOG(x) BonePosLog.log x
#define BONEPOS_DUMPMATRIX3D(x) BONEPOS_DUMPMATRIX3DNAMED(x, #x)
#define BONEPOS_DUMPMATRIX3DNAMED(x, y) BonePosLog.dumpMatrix3D(x, y, __FILE__, __LINE__)
#define BONEPOS_DUMPREAL(x) BONEPOS_DUMPREALNAMED(x, #x)
#define BONEPOS_DUMPREALNAMED(x, y) BonePosLog.dumpReal(x, y, __FILE__, __LINE__)

#else // DEBUG_CRC

#define BONEPOS_LOG(x) {}
#define BONEPOS_DUMPMATRIX3D(x) {}
#define BONEPOS_DUMPMATRIX3DNAMED(x, y) {}
#define BONEPOS_DUMPREAL(x) {}
#define BONEPOS_DUMPREALNAMED(x, y) {}

#endif // DEBUG_CRC

#if defined(_DEBUG)
extern AsciiString TheThingTemplateBeingParsedName;
extern Real TheSkateDistOverride;
#endif

enum ACBits
{
	RANDOMIZE_START_FRAME = 0,
	START_FRAME_FIRST,
	START_FRAME_LAST,
	ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT,
	PRISTINE_BONE_POS_IN_FINAL_FRAME,
	MAINTAIN_FRAME_ACROSS_STATES,
	RESTART_ANIM_WHEN_COMPLETE,
	MAINTAIN_FRAME_ACROSS_STATES2,
	MAINTAIN_FRAME_ACROSS_STATES3,
	MAINTAIN_FRAME_ACROSS_STATES4,
};

const UnsignedInt NO_NEXT_DURATION = 0xffffffff;

//-------------------------------------------------------------------------------------------------
inline Bool testFlagBit(Int flags, Int bit)
{
	return (flags & (1<<bit)) != 0;
}
