/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: WavParser.h ///////////////////////////////////////////////////////////////////////////////
// Minimal RIFF/WAV parsing (PCM + IMA-ADPCM). Kept separate from XAudio2AudioManager.cpp so it can
// be unit-tested without linking the whole XAudio2/COM-heavy audio manager.

#pragma once

#ifndef __WAVPARSER_H_
#define __WAVPARSER_H_

#include "XAudio2Device/XAudio2AudioManager.h"

// Parses a RIFF/WAVE buffer, decoding IMA-ADPCM if necessary. On success, allocates and returns a
// buffer of raw PCM sample data via *ppDecodedPcm (caller owns, delete[] as char*), fills info.
Bool ParseWavAndDecode(const UnsignedByte *buffer, UnsignedInt fileSize, WaveFormatInfo &info, void *&ppDecodedPcm);

#endif // __WAVPARSER_H_
