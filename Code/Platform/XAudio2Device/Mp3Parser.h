/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: Mp3Parser.h ///////////////////////////////////////////////////////////////////////////////
// Decodes an MP3 file (game music, see AudioSettings::m_musicExtension) into interleaved S16 PCM,
// via the FFmpeg backend also used for video.
// Mirrors WavParser.h's contract so AudioFileCache::openFile can treat both the same way.

#pragma once

#ifndef __MP3PARSER_H_
#define __MP3PARSER_H_

#include "XAudio2Device/XAudio2AudioManager.h"

// Opens and fully decodes filePath (an MP3, via TheFileSystem -- BIG-archive aware) into one
// interleaved S16 PCM buffer. On success, allocates *ppDecodedPcm (caller owns, delete[] as char*,
// same convention as ParseWavAndDecode) and fills info.
Bool ParseMp3AndDecode(AsciiString filePath, WaveFormatInfo &info, void *&ppDecodedPcm);

#endif // __MP3PARSER_H_
