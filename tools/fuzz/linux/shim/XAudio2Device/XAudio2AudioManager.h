/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/
// Native fuzz builds only: WavParser.h includes the XAudio2 manager for WaveFormatInfo alone. Same
// layout as in Platform/XAudio2Device/XAudio2AudioManager.h; if that changes, the
// parser stops compiling here.
#pragma once
#include "Lib/BaseType.h"
#include <new>
#ifndef NEW
#define NEW new
#endif

struct WaveFormatInfo
{
	UnsignedInt channels;
	UnsignedInt samplesPerSec;
	UnsignedInt bitsPerSample;
	const void *data;
	UnsignedInt dataLen;
};
