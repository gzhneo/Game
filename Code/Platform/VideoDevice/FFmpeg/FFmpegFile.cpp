/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: FFmpegFile.cpp ////////////////////////////////////////////////////////////////////////////
// See VideoDevice/FFmpeg/FFmpegFile.h. Shared by the video player and Mp3Parser.cpp so the custom
// AVIOContext/TheFileSystem plumbing exists only once.

#include "VideoDevice/FFmpeg/FFmpegFile.h"
#include "Common/FileSystem.h"

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

//===================================================================================================
// FFmpegFile
//===================================================================================================
FFmpegFile::FFmpegFile() :
	m_file(nullptr), m_avioCtx(nullptr), m_avioBuffer(nullptr), m_fmtCtx(nullptr),
	m_videoCodecCtx(nullptr), m_audioCodecCtx(nullptr), m_videoStreamIndex(-1), m_audioStreamIndex(-1)
{
}

FFmpegFile::~FFmpegFile()
{
	close();
}

Int FFmpegFile::readPacket( void *opaque, UnsignedByte *buf, Int bufSize )
{
	File *file = static_cast<File *>(opaque);
	Int bytesRead = file->read( buf, bufSize );
	if ( bytesRead <= 0 )
		return AVERROR_EOF;
	return bytesRead;
}

Int64 FFmpegFile::seekPacket( void *opaque, Int64 offset, Int whence )
{
	File *file = static_cast<File *>(opaque);

	if ( whence == AVSEEK_SIZE )
		return static_cast<Int64>(file->size());

	File::seekMode mode;
	switch ( whence & ~AVSEEK_FORCE )
	{
		case SEEK_SET: mode = File::seekMode::START;   break;
		case SEEK_CUR: mode = File::seekMode::CURRENT; break;
		case SEEK_END: mode = File::seekMode::END;     break;
		default: return -1;
	}

	Int newPos = file->seek( static_cast<Int>(offset), mode );
	if ( newPos < 0 )
		return -1;
	return static_cast<Int64>(newPos);
}

Bool FFmpegFile::open( AsciiString filePath )
{
	return open( TheFileSystem->openFile( filePath.str(), File::READ | File::BINARY ) );
}

Bool FFmpegFile::open( File *file )
{
	m_file = file;
	if ( !m_file )
		return false;
	m_file->deleteOnClose();

	const size_t avioBufferSize = 0x10000;
	m_avioBuffer = static_cast<UnsignedByte *>(av_malloc( avioBufferSize ));
	m_avioCtx = avio_alloc_context( m_avioBuffer, static_cast<Int>(avioBufferSize), 0, m_file,
		&FFmpegFile::readPacket, nullptr, &FFmpegFile::seekPacket );
	if ( !m_avioCtx )
	{
		close();
		return false;
	}

	m_fmtCtx = avformat_alloc_context();
	m_fmtCtx->pb = m_avioCtx;
	m_fmtCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

	if ( avformat_open_input( &m_fmtCtx, nullptr, nullptr, nullptr ) != 0 )
	{
		close();
		return false;
	}

	if ( avformat_find_stream_info( m_fmtCtx, nullptr ) < 0 )
	{
		close();
		return false;
	}

	// Video is optional: an audio-only file (e.g. an .mp3, see Mp3Parser.cpp) has no video stream.
	// FFmpegVideoPlayer rejects a file with no video stream itself, at its own call site.
	m_videoStreamIndex = av_find_best_stream( m_fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0 );
	m_audioStreamIndex = av_find_best_stream( m_fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0 );
	if ( m_videoStreamIndex < 0 && m_audioStreamIndex < 0 )
	{
		close();
		return false;
	}

	if ( m_videoStreamIndex >= 0 )
	{
		const AVCodec *videoCodec = avcodec_find_decoder( m_fmtCtx->streams[m_videoStreamIndex]->codecpar->codec_id );
		if ( !videoCodec )
		{
			m_videoStreamIndex = -1;
		}
		else
		{
			m_videoCodecCtx = avcodec_alloc_context3( videoCodec );
			avcodec_parameters_to_context( m_videoCodecCtx, m_fmtCtx->streams[m_videoStreamIndex]->codecpar );
			if ( avcodec_open2( m_videoCodecCtx, videoCodec, nullptr ) < 0 )
			{
				avcodec_free_context( &m_videoCodecCtx );
				m_videoStreamIndex = -1;
			}
		}
	}

	if ( m_audioStreamIndex >= 0 )
	{
		const AVCodec *audioCodec = avcodec_find_decoder( m_fmtCtx->streams[m_audioStreamIndex]->codecpar->codec_id );
		if ( audioCodec )
		{
			m_audioCodecCtx = avcodec_alloc_context3( audioCodec );
			avcodec_parameters_to_context( m_audioCodecCtx, m_fmtCtx->streams[m_audioStreamIndex]->codecpar );
			if ( avcodec_open2( m_audioCodecCtx, audioCodec, nullptr ) < 0 )
			{
				avcodec_free_context( &m_audioCodecCtx );
				m_audioStreamIndex = -1;
			}
		}
		else
		{
			m_audioStreamIndex = -1;
		}
	}

	if ( m_videoStreamIndex < 0 && m_audioStreamIndex < 0 )
	{
		// both codecs failed to open even though a stream was found
		close();
		return false;
	}

	return true;
}

void FFmpegFile::close( void )
{
	if ( m_videoCodecCtx ) avcodec_free_context( &m_videoCodecCtx );
	if ( m_audioCodecCtx ) avcodec_free_context( &m_audioCodecCtx );
	if ( m_fmtCtx ) avformat_close_input( &m_fmtCtx );		// does not touch m_avioCtx (AVFMT_FLAG_CUSTOM_IO)
	if ( m_avioCtx )
	{
		av_freep( &m_avioCtx->buffer );
		avio_context_free( &m_avioCtx );
	}
	m_avioBuffer = nullptr;
	if ( m_file )
	{
		m_file->close();		// self-deletes (deleteOnClose() was set in open())
		m_file = nullptr;
	}
	m_videoStreamIndex = -1;
	m_audioStreamIndex = -1;
}

Int FFmpegFile::getFrameCount( void ) const
{
	if ( m_videoStreamIndex < 0 )
		return 0;

	AVStream *stream = m_fmtCtx->streams[m_videoStreamIndex];
	if ( stream->nb_frames > 0 )
		return static_cast<Int>(stream->nb_frames);

	Real fps = static_cast<Real>(av_q2d( stream->avg_frame_rate ));
	if ( fps <= 0.0f )
		fps = 30.0f;		// unknown framerate -- fallback guess

	Real durationSec = 0.0f;
	if ( stream->duration != static_cast<Int64>(AV_NOPTS_VALUE) )
		durationSec = static_cast<Real>(stream->duration * av_q2d( stream->time_base ));
	else if ( m_fmtCtx->duration != static_cast<Int64>(AV_NOPTS_VALUE) )
		durationSec = static_cast<Real>(m_fmtCtx->duration) / static_cast<Real>(AV_TIME_BASE);

	Int count = static_cast<Int>(durationSec * fps + 0.5f);
	return count > 0 ? count : 1;
}

UnsignedInt FFmpegFile::getFrameTimeMS( void ) const
{
	if ( m_videoStreamIndex < 0 )
		return 33;

	Real fps = static_cast<Real>(av_q2d( m_fmtCtx->streams[m_videoStreamIndex]->avg_frame_rate ));
	if ( fps <= 0.0f )
		fps = 30.0f;

	return static_cast<UnsignedInt>(1000.0f / fps);
}
