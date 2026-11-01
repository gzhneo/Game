/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: FFmpegVideoPlayer.h //////////////////////////////////////////////////////////////////////
// Video playback with FFmpeg (libavformat/libavcodec/libswscale/libswresample, via vcpkg),
// implementing the abstract VideoPlayer/VideoStream contract (GameClient/VideoPlayer.h).
// Each video's decoded and resampled audio is fed into its own IXAudio2SourceVoice obtained via
// AudioManager::getDevice().

#pragma once

#ifndef __VIDEODEVICE_FFMPEGVIDEOPLAYER_H_
#define __VIDEODEVICE_FFMPEGVIDEOPLAYER_H_

#include "GameClient/VideoPlayer.h"
#include <memory>
#include "Common/file.h"
#include "VideoDevice/FFmpeg/FFmpegFile.h"

#include <xaudio2.h>
#include <deque>

// Forward-declare instead of including the FFmpeg headers, as in VideoDevice/FFmpeg/FFmpegFile.h. m_swsSrcFormat below is stored as a plain Int
// instead of "enum AVPixelFormat" for the same reason (forward-declaring a C enum from an
// extern "C" header is fragile; the .cpp casts at its two use sites).
extern "C"
{
	struct AVFrame;
	struct AVPacket;
	struct SwsContext;
	struct SwrContext;
}

class FFmpegVideoPlayer;

// The stream itself frees the PCM buffers of the video's audio track (m_pcmPending,
// freePlayedAudioBuffers) instead of an OnBufferEnd callback: for buffers still queued when the
// video closes, FlushSourceBuffers() reports them only on the next processing pass, after DestroyVoice().

//===============================
// FFmpegVideoStream
//===============================
class FFmpegVideoStream : public VideoStream
{
	friend class FFmpegVideoPlayer;

	protected:
		std::unique_ptr<FFmpegFile>		m_file;
		AVFrame								*m_videoFrame;
		AVFrame								*m_audioFrame;
		AVPacket							*m_packet;
		SwsContext						*m_swsCtx;
		SwrContext						*m_swrCtx;
		Int							m_swsSrcFormat;		///< enum AVPixelFormat, cast at its two use sites (see header comment above); pix_fmt the current m_swsCtx was built for (rebuilt if it changes)

		IXAudio2SourceVoice		*m_audioVoice;
		UnsignedInt						m_audioSampleRate;
		std::deque<UnsignedByte *>	m_pcmPending;	///< PCM buffers submitted to m_audioVoice, in submit order; owned by the stream

		Int										m_frameIndex;			///< Index of the frame currently held in m_videoFrame (-1 = none decoded yet)
		Int										m_frameCount;			///< Cached total frame count
		UnsignedInt						m_frameTimeMS;		///< Cached ms-per-frame
		UnsignedInt						m_startTimeMS;		///< Wall clock time (GetTickCount) the stream started, for the isFrameReady() gate
		Bool									m_endOfStream;
		Bool									m_haveVideoFrame;	///< Is m_videoFrame currently holding a valid decoded frame

		FFmpegVideoStream();										///< only FFmpegVideoPlayer can create these
		virtual ~FFmpegVideoStream() override;

		Bool	decodeNextVideoFrame( void );			///< Pumps the demuxer until the next video frame decodes (or EOF); feeds any audio packets found along the way
		void	feedAudioFrame( AVFrame *frame );	///< Resamples one decoded audio frame to S16 and submits it to m_audioVoice
		void	ensureAudioVoice( void );					///< Lazily creates m_audioVoice once the audio stream's format is known
		void	freePlayedAudioBuffers( void );		///< Frees the PCM buffers that finished playing (all of them if there is no voice)

	public:

		virtual void	update( void ) override;
		virtual Bool	isFrameReady( void ) override;
		virtual void	frameDecompress( void ) override;
		virtual void	frameRender( VideoBuffer *buffer ) override;
		virtual void	frameNext( void ) override;
		virtual Int		frameIndex( void ) override;
		virtual Int		frameCount( void ) override;
		virtual void	frameGoto( Int index ) override;
		virtual Int		height( void ) override;
		virtual Int		width( void ) override;
};

//===============================
// FFmpegVideoPlayer
//===============================
class FFmpegVideoPlayer : public VideoPlayer
{
	protected:
		VideoStreamInterface* createStream( FFmpegFile *file );

	public:
		FFmpegVideoPlayer();
		~FFmpegVideoPlayer() override;

		virtual void	init( void ) override;
		virtual void	reset( void ) override;
		virtual void	update( void ) override;
		virtual void	deinit( void ) override;

		virtual void	loseFocus( void ) override;
		virtual void	regainFocus( void ) override;

		virtual VideoStreamInterface*	open( AsciiString movieTitle ) override;
		virtual VideoStreamInterface*	load( AsciiString movieTitle ) override;

		// XAudio2 has no "audio provider" change; each video owns its own source voice, so this is a no-op.
		virtual void notifyVideoPlayerOfNewProvider( Bool nowHasValid ) override { }
};

#endif // __VIDEODEVICE_FFMPEGVIDEOPLAYER_H_
