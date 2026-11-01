/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: FFmpegVideoPlayer.cpp ////////////////////////////////////////////////////////////////////
// See FFmpegVideoPlayer.h for an overview. Implements the abstract VideoPlayer/VideoStream contract
// (GameClient/VideoPlayer.h) on FFmpeg.

#include "VideoDevice/FFmpeg/FFmpegVideoPlayer.h"
#include <memory>

#include "Common/AudioAffect.h"
#include "Common/GameAudio.h"
#include "VideoDevice/FFmpeg/FFmpegFile.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/FileSystem.h"
#include "Common/GameEngine.h"
#include "formatinto.h"
#include "Common/GlobalData.h"
#include "Common/InstallInfo.h"

#include <cstdio>
#include <cstring>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

constexpr char VIDEO_LANG_PATH_FORMAT[] = "Data/%s/Movies/%s.%s";
constexpr char VIDEO_PATH[] = "Data\\Movies";
constexpr char VIDEO_EXT[] = "bik";		// "bik": FFmpeg's libavformat/libavcodec demux/decode original Bink
														// containers natively, so the original movie assets work unmodified.

//===================================================================================================
// FFmpegVideoStream
//===================================================================================================
FFmpegVideoStream::FFmpegVideoStream() :
	m_file(nullptr), m_videoFrame(nullptr), m_audioFrame(nullptr), m_packet(nullptr),
	m_swsCtx(nullptr), m_swrCtx(nullptr), m_swsSrcFormat(AV_PIX_FMT_NONE),
	m_audioVoice(nullptr), m_audioSampleRate(0),
	m_frameIndex(-1), m_frameCount(0), m_frameTimeMS(33), m_startTimeMS(0),
	m_endOfStream(false), m_haveVideoFrame(false)
{
	m_videoFrame = av_frame_alloc();
	m_audioFrame = av_frame_alloc();
	m_packet = av_packet_alloc();
}

FFmpegVideoStream::~FFmpegVideoStream()
{
	if ( m_audioVoice )
	{
		// DestroyVoice() returns only once XAudio2 no longer processes the voice; after that all
		// submitted PCM buffers belong to us again and freePlayedAudioBuffers() releases them.
		m_audioVoice->Stop( 0 );
		m_audioVoice->DestroyVoice();
		m_audioVoice = nullptr;
	}
	freePlayedAudioBuffers();
	if ( m_swsCtx ) sws_freeContext( m_swsCtx );
	if ( m_swrCtx ) swr_free( &m_swrCtx );
	if ( m_videoFrame ) av_frame_free( &m_videoFrame );
	if ( m_audioFrame ) av_frame_free( &m_audioFrame );
	if ( m_packet ) av_packet_free( &m_packet );
	if ( m_file )
	{
		m_file.reset();
	}
}

Bool FFmpegVideoStream::decodeNextVideoFrame( void )
{
	if ( m_endOfStream )
		return false;

	AVFormatContext *fmtCtx = m_file->getFormatContext();
	AVCodecContext *videoCtx = m_file->getVideoCodecContext();
	Int videoStreamIdx = m_file->getVideoStreamIndex();
	Int audioStreamIdx = m_file->getAudioStreamIndex();

	for ( ;; )
	{
		Int readResult = av_read_frame( fmtCtx, m_packet );
		if ( readResult < 0 )
		{
			// End of stream -- drain any frame(s) still buffered inside the decoder.
			avcodec_send_packet( videoCtx, nullptr );
			Int recvResult = avcodec_receive_frame( videoCtx, m_videoFrame );
			if ( recvResult == 0 )
			{
				m_frameIndex++;
				m_haveVideoFrame = true;
				return true;
			}
			m_endOfStream = true;
			return false;
		}

		if ( m_packet->stream_index == videoStreamIdx )
		{
			Int sendResult = avcodec_send_packet( videoCtx, m_packet );
			av_packet_unref( m_packet );
			if ( sendResult < 0 )
				continue;

			Int recvResult = avcodec_receive_frame( videoCtx, m_videoFrame );
			if ( recvResult == 0 )
			{
				m_frameIndex++;
				m_haveVideoFrame = true;
				return true;
			}
			// AVERROR(EAGAIN): decoder needs more packets before it can produce a frame -- keep pumping.
			continue;
		}
		else if ( audioStreamIdx >= 0 && m_packet->stream_index == audioStreamIdx )
		{
			AVCodecContext *audioCtx = m_file->getAudioCodecContext();
			if ( avcodec_send_packet( audioCtx, m_packet ) == 0 )
			{
				while ( avcodec_receive_frame( audioCtx, m_audioFrame ) == 0 )
					feedAudioFrame( m_audioFrame );
			}
			av_packet_unref( m_packet );
		}
		else
		{
			av_packet_unref( m_packet );
		}
	}
}

void FFmpegVideoStream::ensureAudioVoice( void )
{
	if ( m_audioVoice || !m_file->hasAudio() )
		return;

	AVCodecContext *audioCtx = m_file->getAudioCodecContext();
	if ( !audioCtx || audioCtx->sample_rate <= 0 )
		return;

	IXAudio2 *xaudio2 = static_cast<IXAudio2 *>(TheAudio->getDevice());
	if ( !xaudio2 )
		return;

	WAVEFORMATEX wfx;
	memset( &wfx, 0, sizeof(wfx) );
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = static_cast<WORD>(audioCtx->ch_layout.nb_channels);
	wfx.nSamplesPerSec = audioCtx->sample_rate;
	wfx.wBitsPerSample = 16;
	wfx.nBlockAlign = static_cast<WORD>(wfx.nChannels * (wfx.wBitsPerSample / 8));
	wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

	// Routed to the mastering voice directly (default XAudio2 behavior when no send list is given)
	// instead of TheAudio's Speech submix, so this self-contained voice needs no access to
	// XAudio2AudioManager's internals.
	if ( FAILED( xaudio2->CreateSourceVoice( &m_audioVoice, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO,
			nullptr, nullptr, nullptr ) ) )
	{
		m_audioVoice = nullptr;
		return;
	}

	m_audioVoice->SetVolume( TheAudio->getVolume( AudioAffect_Speech ) );
	m_audioVoice->Start( 0 );
	m_audioSampleRate = audioCtx->sample_rate;
}

// XAudio2 plays a voice's buffers in submit order; BuffersQueued counts the unfinished ones (including
// the one playing). Everything before them in m_pcmPending is done and can go. Without a voice (after
// DestroyVoice) all buffers are freed.
void FFmpegVideoStream::freePlayedAudioBuffers( void )
{
	size_t queued = 0;
	if ( m_audioVoice )
	{
		XAUDIO2_VOICE_STATE state;
		m_audioVoice->GetState( &state, XAUDIO2_VOICE_NOSAMPLESPLAYED );
		queued = state.BuffersQueued;
	}
	while ( m_pcmPending.size() > queued )
	{
		delete[] m_pcmPending.front();
		m_pcmPending.pop_front();
	}
}

void FFmpegVideoStream::feedAudioFrame( AVFrame *frame )
{
	ensureAudioVoice();
	if ( !m_audioVoice )
		return;
	freePlayedAudioBuffers();

	if ( !m_swrCtx )
	{
		AVChannelLayout outLayout;
		av_channel_layout_default( &outLayout, frame->ch_layout.nb_channels );

		Int result = swr_alloc_set_opts2( &m_swrCtx,
			&outLayout, AV_SAMPLE_FMT_S16, frame->sample_rate,
			&frame->ch_layout, static_cast<enum AVSampleFormat>(frame->format), frame->sample_rate,
			0, nullptr );
		av_channel_layout_uninit( &outLayout );

		if ( result < 0 || !m_swrCtx || swr_init( m_swrCtx ) < 0 )
		{
			m_swrCtx = nullptr;
			return;
		}
	}

	Int outSamples = swr_get_out_samples( m_swrCtx, frame->nb_samples );
	if ( outSamples <= 0 )
		return;

	Int channels = frame->ch_layout.nb_channels;
	Int bytesPerSample = av_get_bytes_per_sample( AV_SAMPLE_FMT_S16 );
	auto pcmBuffer = std::make_unique<UnsignedByte[]>(outSamples * channels * bytesPerSample);
	UnsignedByte *outPlanes[1] = { pcmBuffer.get() };

	Int converted = swr_convert( m_swrCtx, outPlanes, outSamples,
		(const UnsignedByte **)frame->extended_data, frame->nb_samples );
	if ( converted <= 0 )
		return;

	XAUDIO2_BUFFER buffer;
	memset( &buffer, 0, sizeof(buffer) );
	buffer.AudioBytes = converted * channels * bytesPerSample;
	buffer.pAudioData = pcmBuffer.get();

	// The stream keeps the buffer until XAudio2 has played it (freePlayedAudioBuffers); if XAudio2
	// rejects it (queue full), free it right away.
	if ( SUCCEEDED( m_audioVoice->SubmitSourceBuffer( &buffer ) ) )
		m_pcmPending.push_back( pcmBuffer.release() );
}

void FFmpegVideoStream::update( void )
{
}

Bool FFmpegVideoStream::isFrameReady( void )
{
	if ( !m_haveVideoFrame && m_endOfStream )
		return false;

	// Simple constant-frame-rate wall-clock gate (no PTS tracking) -- matches the frame-index-based
	// contract of this interface (frameIndex/frameCount).
	UnsignedInt elapsed = GetTickCount() - m_startTimeMS;
	return elapsed >= static_cast<UnsignedInt>(m_frameIndex) * m_frameTimeMS;
}

void FFmpegVideoStream::frameDecompress( void )
{
	// Frames are decoded eagerly and synchronously in frameNext()/the initial priming decode in
	// FFmpegVideoPlayer::createStream(), so there is nothing left to finalize here once a frame
	// is current.
}

void FFmpegVideoStream::frameRender( VideoBuffer *buffer )
{
	if ( !buffer || !m_haveVideoFrame )
		return;
	// avcodec_receive_frame clears m_videoFrame on every call, even when it yields no new picture
	// (end of stream, EAGAIN). An empty frame (no data, height 0) makes sws_scale assert and abort().
	if ( m_videoFrame == nullptr || m_videoFrame->data[0] == nullptr || m_videoFrame->width <= 0 || m_videoFrame->height <= 0 )
		return;

	enum AVPixelFormat dstFmt;
	switch ( buffer->format() )
	{
		case VideoBuffer::TYPE_X8R8G8B8: dstFmt = AV_PIX_FMT_BGR0;   break;
		case VideoBuffer::TYPE_R8G8B8:   dstFmt = AV_PIX_FMT_BGR24;  break;
		case VideoBuffer::TYPE_R5G6B5:   dstFmt = AV_PIX_FMT_RGB565; break;
		case VideoBuffer::TYPE_X1R5G5B5: dstFmt = AV_PIX_FMT_RGB555; break;
		default: return;
	}

	void *mem = buffer->lock();
	if ( !mem )
		return;

	enum AVPixelFormat srcFmt = static_cast<enum AVPixelFormat>(m_videoFrame->format);
	// sws_getCachedContext keeps the context while all parameters (sizes, formats) match and recreates
	// it otherwise, so a change in picture or target size never scales with stale values.
	m_swsCtx = sws_getCachedContext( m_swsCtx,
		m_videoFrame->width, m_videoFrame->height, srcFmt,
		static_cast<Int>(buffer->width()), static_cast<Int>(buffer->height()), dstFmt,
		SWS_BILINEAR, nullptr, nullptr, nullptr );
	m_swsSrcFormat = srcFmt;

	if ( m_swsCtx )
	{
		UnsignedByte *dstPlanes[1] = { static_cast<UnsignedByte *>(mem) };
		Int dstStrides[1] = { static_cast<Int>(buffer->pitch()) };
		sws_scale( m_swsCtx, m_videoFrame->data, m_videoFrame->linesize, 0, m_videoFrame->height, dstPlanes, dstStrides );
	}

	buffer->unlock();
}

void FFmpegVideoStream::frameNext( void )
{
	decodeNextVideoFrame();
}

Int FFmpegVideoStream::frameIndex( void )
{
	return m_frameIndex;
}

Int FFmpegVideoStream::frameCount( void )
{
	return m_frameCount;
}

void FFmpegVideoStream::frameGoto( Int index )
{
	if ( !m_file || index < 0 )
		return;
	if ( index >= m_frameCount )
		index = m_frameCount - 1;

	AVFormatContext *fmtCtx = m_file->getFormatContext();
	Int videoStreamIdx = m_file->getVideoStreamIndex();
	AVStream *stream = fmtCtx->streams[videoStreamIdx];

	AVRational msTimeBase = { 1, 1000 };
	Int64 targetTs = av_rescale_q( static_cast<Int64>(index) * m_frameTimeMS, msTimeBase, stream->time_base );

	if ( av_seek_frame( fmtCtx, videoStreamIdx, targetTs, AVSEEK_FLAG_BACKWARD ) >= 0 )
	{
		avcodec_flush_buffers( m_file->getVideoCodecContext() );
		m_frameIndex = -1;
		m_haveVideoFrame = false;
		m_endOfStream = false;

		// Containers only seek to keyframes -- decode forward (discarding output) to reach the
		// exact requested frame. Bounded so a pathological seek can't hang the game.
		Int guard = m_frameCount + 16;
		while ( m_frameIndex < index && guard-- > 0 )
		{
			if ( !decodeNextVideoFrame() )
				break;
		}
	}

	m_startTimeMS = GetTickCount() - static_cast<UnsignedInt>(m_frameIndex < 0 ? 0 : m_frameIndex) * m_frameTimeMS;
}

Int FFmpegVideoStream::width( void )
{
	return m_file->getVideoCodecContext()->width;
}

Int FFmpegVideoStream::height( void )
{
	return m_file->getVideoCodecContext()->height;
}

//===================================================================================================
// FFmpegVideoPlayer
//===================================================================================================
FFmpegVideoPlayer::FFmpegVideoPlayer()
{
}

FFmpegVideoPlayer::~FFmpegVideoPlayer()
{
	deinit();
}

void FFmpegVideoPlayer::init( void )
{
	VideoPlayer::init();
}

void FFmpegVideoPlayer::deinit( void )
{
	VideoPlayer::deinit();
}

void FFmpegVideoPlayer::reset( void )
{
	VideoPlayer::reset();
}

void FFmpegVideoPlayer::update( void )
{
	VideoPlayer::update();
}

void FFmpegVideoPlayer::loseFocus( void )
{
	VideoPlayer::loseFocus();
}

void FFmpegVideoPlayer::regainFocus( void )
{
	VideoPlayer::regainFocus();
}

VideoStreamInterface *FFmpegVideoPlayer::createStream( FFmpegFile *file )
{
	if ( !file )
		return nullptr;

	// FFmpegFile accepts an audio-only file (shared with Mp3Parser.cpp); this is a VIDEO player,
	// so it requires an actual video stream.
	if ( !file->hasVideo() )
	{
		delete file;
		return nullptr;
	}

	FFmpegVideoStream *stream = NEW FFmpegVideoStream;
	stream->m_file.reset(file);
	stream->m_frameCount = file->getFrameCount();
	stream->m_frameTimeMS = file->getFrameTimeMS();
	stream->m_startTimeMS = GetTickCount();
	stream->m_next = m_firstStream;
	stream->m_player = this;
	m_firstStream = stream;

	// Prime the pipeline with frame 0 so frameIndex()/frameCount() are valid immediately and the
	// first frame is ready after open.
	stream->decodeNextVideoFrame();

	return stream;
}

VideoStreamInterface *FFmpegVideoPlayer::open( AsciiString movieTitle )
{
	const Video *pVideo = getVideo( movieTitle );
	if ( !pVideo )
		return nullptr;

	auto file = std::make_unique<FFmpegFile>();
	AsciiString filePath;

	// A mod's movie comes first, also from its archives, even over a localized movie of the game.
	filePath.format( "%s\\%s.%s", VIDEO_PATH, pVideo->m_filename.str(), VIDEO_EXT );
	if ( TheArchiveFileSystem != nullptr )
	{
		File *modFile = TheArchiveFileSystem->openModFile( filePath.str() );
		if ( modFile != nullptr )
		{
			const Bool opened = file->open( modFile );
			char line[512];
			WWFormat::Into( line, "MOD film {} {}", filePath.str(), opened ? "geladen" : "nicht lesbar" );
			line[sizeof(line) - 1] = 0;
			GameEngine_RunnerLog( line );
			if ( opened )
				return createStream( file.release() );
		}
	}

	filePath.format( VIDEO_LANG_PATH_FORMAT, GetGameLanguage().str(), pVideo->m_filename.str(), VIDEO_EXT );
	if ( !file->open( filePath ) )
	{
		filePath.format( "%s\\%s.%s", VIDEO_PATH, pVideo->m_filename.str(), VIDEO_EXT );
		if ( !file->open( filePath ) )
			return nullptr;
	}

	return createStream( file.release() );
}

VideoStreamInterface *FFmpegVideoPlayer::load( AsciiString movieTitle )
{
	return open( movieTitle );		// load and open are the same for this player
}
