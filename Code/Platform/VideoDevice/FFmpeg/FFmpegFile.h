/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: FFmpegFile.h //////////////////////////////////////////////////////////////////////////////
// Shared by the video player and the audio side (Mp3Parser.cpp, MP3 music decoding) so there is
// only one custom AVIOContext. The video stream is optional so an audio-only file (e.g. an .mp3)
// opens too; FFmpegVideoPlayer rejects a file with no video stream at its own call site.

#pragma once

#ifndef __COMMON_FFMPEGFILE_H_
#define __COMMON_FFMPEGFILE_H_

#include "Common/AsciiString.h"
#include "Common/file.h"

// Forward-declare instead of including the FFmpeg headers: this header is reachable from
// App/WinMain.cpp, which should not need FFmpeg's include path. Only pointers appear below; the
// .cpp files that dereference them include the real headers.
extern "C"
{
	struct AVFormatContext;
	struct AVCodecContext;
	struct AVIOContext;
}

//===============================
// FFmpegFile
//===============================
/**
  * Wraps an AVFormatContext opened against a game File* (BIG-archive aware, via
  * TheFileSystem->openFile()) instead of a raw filesystem path, like the AudioFileCache.
  * Owns the video/audio decoder contexts for the streams it selected. Either
  * stream may be absent (video-only, audio-only); open() only fails if the file has neither.
  */
//===============================
class FFmpegFile
{
	public:
		FFmpegFile();
		~FFmpegFile();

		Bool open( AsciiString filePath );		///< Opens filePath via TheFileSystem and sets up FFmpeg demux/decode contexts
		Bool open( File *file );				///< Takes an opened file (closed here on failure) and sets up the contexts
		void close( void );

		AVFormatContext	*getFormatContext( void ) const	{ return m_fmtCtx; }
		AVCodecContext	*getVideoCodecContext( void ) const	{ return m_videoCodecCtx; }
		AVCodecContext	*getAudioCodecContext( void ) const	{ return m_audioCodecCtx; }
		Int							getVideoStreamIndex( void ) const	{ return m_videoStreamIndex; }
		Int							getAudioStreamIndex( void ) const	{ return m_audioStreamIndex; }
		Bool						hasVideo( void ) const	{ return m_videoStreamIndex >= 0; }
		Bool						hasAudio( void ) const	{ return m_audioStreamIndex >= 0; }

		Int							getFrameCount( void ) const;	///< Total frame count of the video stream (nb_frames, or an estimate); 0 if no video
		UnsignedInt			getFrameTimeMS( void ) const;	///< Milliseconds per video frame, from the stream's average frame rate; 33 if no video

	protected:
		static Int	readPacket( void *opaque, UnsignedByte *buf, Int bufSize );
		static Int64	seekPacket( void *opaque, Int64 offset, Int whence );

		File						*m_file;
		AVIOContext			*m_avioCtx;
		UnsignedByte		*m_avioBuffer;
		AVFormatContext	*m_fmtCtx;
		AVCodecContext	*m_videoCodecCtx;
		AVCodecContext	*m_audioCodecCtx;
		Int							m_videoStreamIndex;
		Int							m_audioStreamIndex;
};

#endif // __COMMON_FFMPEGFILE_H_
