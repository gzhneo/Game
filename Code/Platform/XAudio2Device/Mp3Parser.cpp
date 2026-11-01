/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: Mp3Parser.cpp /////////////////////////////////////////////////////////////////////////////
// See Mp3Parser.h. Demux/decode loop and swresample setup mirror FFmpegVideoStream::
// decodeNextVideoFrame()/feedAudioFrame() (VideoDevice/FFmpeg/FFmpegVideoPlayer.cpp) --
// same FFmpeg version/API (AVChannelLayout, swr_alloc_set_opts2), just accumulating every decoded
// chunk into one buffer instead of feeding a streaming XAudio2 voice frame-by-frame.

#include "XAudio2Device/Mp3Parser.h"

#include "VideoDevice/FFmpeg/FFmpegFile.h"

#include <cstring>
#include <vector>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

Bool ParseMp3AndDecode(AsciiString filePath, WaveFormatInfo &info, void *&ppDecodedPcm)
{
	ppDecodedPcm = nullptr;

	FFmpegFile file;
	if (!file.open(filePath) || !file.hasAudio())
		return false;

	AVFormatContext *fmtCtx = file.getFormatContext();
	AVCodecContext *audioCtx = file.getAudioCodecContext();
	Int audioStreamIdx = file.getAudioStreamIndex();
	if (audioCtx->sample_rate <= 0 || audioCtx->ch_layout.nb_channels <= 0)
		return false;

	Int channels = audioCtx->ch_layout.nb_channels;
	Int bytesPerSample = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);

	SwrContext *swrCtx = nullptr;
	std::vector<UnsignedByte> pcm;
	// Best-effort upfront reserve from the container's duration estimate, purely to cut down on
	// reallocations for a multi-minute music track -- pcm still grows normally if this is wrong
	// (unknown duration, VBR estimate off, etc.).
	if (fmtCtx->duration != static_cast<Int64>(AV_NOPTS_VALUE) && fmtCtx->duration > 0)
	{
		double durationSec = static_cast<double>(fmtCtx->duration) / static_cast<double>(AV_TIME_BASE);
		pcm.reserve(static_cast<size_t>(durationSec * audioCtx->sample_rate * channels * bytesPerSample));
	}

	AVPacket *packet = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();

	// decodeOneFrame(): converts one already-decoded frame to S16 and appends it to pcm. Lazily
	// creates swrCtx on the first frame (frame->format/ch_layout are only known once decoding has
	// actually started, same reasoning as FFmpegVideoStream::feedAudioFrame()).
	auto decodeOneFrame = [&](AVFrame *decodedFrame) -> Bool
	{
		if (!swrCtx)
		{
			AVChannelLayout outLayout;
			av_channel_layout_default(&outLayout, decodedFrame->ch_layout.nb_channels);

			Int result = swr_alloc_set_opts2(&swrCtx,
				&outLayout, AV_SAMPLE_FMT_S16, decodedFrame->sample_rate,
				&decodedFrame->ch_layout, static_cast<enum AVSampleFormat>(decodedFrame->format), decodedFrame->sample_rate,
				0, nullptr);
			av_channel_layout_uninit(&outLayout);

			if (result < 0 || !swrCtx || swr_init(swrCtx) < 0)
			{
				swrCtx = nullptr;
				return false;
			}
		}

		Int outSamples = swr_get_out_samples(swrCtx, decodedFrame->nb_samples);
		if (outSamples <= 0)
			return true; // nothing to convert yet, not an error

		size_t oldSize = pcm.size();
		pcm.resize(oldSize + static_cast<size_t>(outSamples) * channels * bytesPerSample);
		UnsignedByte *outPlanes[1] = { pcm.data() + oldSize };

		Int converted = swr_convert(swrCtx, outPlanes, outSamples,
			(const UnsignedByte **)decodedFrame->extended_data, decodedFrame->nb_samples);
		if (converted < 0)
			converted = 0;
		pcm.resize(oldSize + static_cast<size_t>(converted) * channels * bytesPerSample);
		return true;
	};

	for (;;)
	{
		Int readResult = av_read_frame(fmtCtx, packet);
		if (readResult < 0)
		{
			// End of stream -- drain whatever the decoder still has buffered.
			avcodec_send_packet(audioCtx, nullptr);
			while (avcodec_receive_frame(audioCtx, frame) == 0)
				decodeOneFrame(frame);
			break;
		}

		if (packet->stream_index == audioStreamIdx)
		{
			if (avcodec_send_packet(audioCtx, packet) == 0)
			{
				while (avcodec_receive_frame(audioCtx, frame) == 0)
					decodeOneFrame(frame);
			}
		}
		av_packet_unref(packet);
	}

	av_frame_free(&frame);
	av_packet_free(&packet);
	if (swrCtx)
		swr_free(&swrCtx);

	if (pcm.empty())
		return false;

	void *copy = NEW char[pcm.size()];
	memcpy(copy, pcm.data(), pcm.size());

	info.channels = channels;
	info.samplesPerSec = audioCtx->sample_rate;
	info.bitsPerSample = 16;
	info.data = copy;
	info.dataLen = static_cast<UnsignedInt>(pcm.size());
	ppDecodedPcm = copy;
	return true;
}
