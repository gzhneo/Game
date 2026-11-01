/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: WavParser.cpp /////////////////////////////////////////////////////////////////////////////
// Minimal RIFF/WAV parsing. Supports PCM and IMA-ADPCM (mono and stereo), the only two formats
// the game's audio files use. Kept separate from XAudio2AudioManager.cpp for unit-testability;
// explicitly includes ForceOrder.h itself so it compiles correctly both as part of GameEngineDevice
// (which force-includes it via /FI) and standalone in the UnitTests.

#include "ForceOrder.h"
#include "XAudio2Device/WavParser.h"

#include <string.h>

#define FOURCC(a,b,c,d) ((UnsignedInt)(UnsignedByte)(a) | ((UnsignedInt)(UnsignedByte)(b)<<8) | ((UnsignedInt)(UnsignedByte)(c)<<16) | ((UnsignedInt)(UnsignedByte)(d)<<24))
enum { WAVE_FMT_PCM = 1, WAVE_FMT_IMA_ADPCM = 0x0011 };

#pragma pack(push,1)
struct WavFmtChunk
{
	WORD formatTag;
	WORD channels;
	DWORD samplesPerSec;
	DWORD avgBytesPerSec;
	WORD blockAlign;
	WORD bitsPerSample;
	// Optionally followed by cbSize + extra data (ADPCM: wSamplesPerBlock) -- read separately.
};
#pragma pack(pop)

// Standard MS IMA ADPCM tables.
static const Int ImaIndexTable[16] =
{
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8
};
static const Int ImaStepTable[89] =
{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
	253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
	1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
	3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
	12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static Short ImaDecodeNibble(UnsignedByte nibble, Int &predictor, Int &stepIndex)
{
	Int step = ImaStepTable[stepIndex];
	Int diff = step >> 3;
	if (nibble & 1) diff += step >> 2;
	if (nibble & 2) diff += step >> 1;
	if (nibble & 4) diff += step;
	if (nibble & 8) diff = -diff;

	predictor += diff;
	if (predictor > 32767) predictor = 32767;
	if (predictor < -32768) predictor = -32768;

	stepIndex += ImaIndexTable[nibble];
	if (stepIndex < 0) stepIndex = 0;
	if (stepIndex > 88) stepIndex = 88;

	return static_cast<Short>(predictor);
}

// Decodes an interleaved MS IMA-ADPCM buffer (1 or 2 channels) into 16-bit PCM. Returns a newly
// allocated buffer (caller owns, delete[] as char*) and fills outLen with its byte length.
static Short *DecodeImaAdpcm(const UnsignedByte *src, UnsignedInt srcLen, UnsignedInt channels, UnsignedInt blockAlign, UnsignedInt &outSamples)
{
	// MS IMA-ADPCM has at most two channels (predictor/stepIndex have two slots), and a block must
	// at least hold the headers.
	if (blockAlign == 0 || channels == 0 || channels > 2 || blockAlign <= 4 * channels) { outSamples = 0; return nullptr; }
	UnsignedInt blockCount = srcLen / blockAlign;
	// Each block: 4 bytes header per channel, then (blockAlign - 4*channels) bytes of nibbles,
	// producing 1 sample (the header sample) + 2 nibbles-per-byte samples per channel.
	UnsignedInt samplesPerBlockPerChannel = 1 + ((blockAlign - 4 * channels) * 2) / channels;
	outSamples = blockCount * samplesPerBlockPerChannel;
	Short *out = NEW Short[outSamples * channels];

	const UnsignedByte *block = src;
	Short *outCursor = out;
	for (UnsignedInt b = 0; b < blockCount; ++b)
	{
		Int predictor[2] = { 0, 0 };
		Int stepIndex[2] = { 0, 0 };
		const UnsignedByte *p = block;
		Short *blockOut = outCursor;

		for (UnsignedInt c = 0; c < channels; ++c)
		{
			predictor[c] = static_cast<Short>(p[0] | (p[1] << 8));
			stepIndex[c] = p[2];
			if (stepIndex[c] > 88) stepIndex[c] = 88;
			blockOut[c] = static_cast<Short>(predictor[c]);
			p += 4;
		}

		UnsignedInt sampleIdx = 1;
		const UnsignedByte *nibbles = p;
		UnsignedInt nibbleBytesLeft = blockAlign - 4 * channels;
		if (channels == 1)
		{
			for (UnsignedInt i = 0; i < nibbleBytesLeft; ++i)
			{
				UnsignedByte byteVal = nibbles[i];
				blockOut[sampleIdx++] = ImaDecodeNibble(byteVal & 0xF, predictor[0], stepIndex[0]);
				blockOut[sampleIdx++] = ImaDecodeNibble((byteVal >> 4) & 0xF, predictor[0], stepIndex[0]);
			}
		}
		else
		{
			// Stereo MS-IMA interleaves in groups of 8 bytes: 4 bytes (=8 samples) channel 0, then 4 bytes channel 1.
			// Each byte yields two samples, so byte i of a group writes samples 2*i and 2*i+1; a group
			// advances the output by 8 stereo samples.
			UnsignedInt groups = nibbleBytesLeft / 8;
			for (UnsignedInt g = 0; g < groups; ++g)
			{
				for (UnsignedInt c = 0; c < 2; ++c)
				{
					const UnsignedByte *grp = nibbles + g * 8 + c * 4;
					for (UnsignedInt i = 0; i < 4; ++i)
					{
						UnsignedByte byteVal = grp[i];
						Short s0 = ImaDecodeNibble(byteVal & 0xF, predictor[c], stepIndex[c]);
						Short s1 = ImaDecodeNibble((byteVal >> 4) & 0xF, predictor[c], stepIndex[c]);
						blockOut[(sampleIdx + 2 * i) * 2 + c] = s0;
						blockOut[(sampleIdx + 2 * i + 1) * 2 + c] = s1;
					}
				}
				sampleIdx += 8;	// both channels filled the same 8 samples of this group
			}
		}

		block += blockAlign;
		outCursor += samplesPerBlockPerChannel * channels;
	}

	return out;
}

Bool ParseWavAndDecode(const UnsignedByte *buffer, UnsignedInt fileSize, WaveFormatInfo &info, void *&ppDecodedPcm)
{
	ppDecodedPcm = nullptr;
	if (fileSize < 12 || memcmp(buffer, "RIFF", 4) != 0 || memcmp(buffer + 8, "WAVE", 4) != 0) {
		return false;
	}

	const UnsignedByte *cursor = buffer + 12;
	const UnsignedByte *end = buffer + fileSize;

	WavFmtChunk fmt;
	memset(&fmt, 0, sizeof(fmt));
	Bool haveFmt = false;
	const UnsignedByte *dataPtr = nullptr;
	UnsignedInt dataLen = 0;

	while (cursor + 8 <= end)
	{
		UnsignedInt chunkId = FOURCC(cursor[0], cursor[1], cursor[2], cursor[3]);
		// memcpy: chunks are only word-aligned, the size field may be misaligned.
		UnsignedInt chunkSize;
		memcpy(&chunkSize, cursor + 4, sizeof(chunkSize));
		const UnsignedByte *chunkData = cursor + 8;
		// Compare lengths, not pointers: chunkData + chunkSize may lie far beyond the buffer.
		if (chunkSize > static_cast<UnsignedInt>(end - chunkData)) {
			chunkSize = static_cast<UnsignedInt>(end - chunkData);
		}

		if (chunkId == FOURCC('f','m','t',' ') && chunkSize >= sizeof(WavFmtChunk)) {
			memcpy(&fmt, chunkData, sizeof(WavFmtChunk));
			haveFmt = true;
		} else if (chunkId == FOURCC('d','a','t','a')) {
			dataPtr = chunkData;
			dataLen = chunkSize;
		}

		if (static_cast<UnsignedInt>(end - chunkData) - chunkSize < (chunkSize & 1)) {
			break;	// odd chunk at the very end: no padding byte left
		}
		cursor = chunkData + chunkSize + (chunkSize & 1); // chunks are word-aligned
	}

	if (!haveFmt || !dataPtr) {
		return false;
	}

	if (fmt.formatTag == WAVE_FMT_PCM) {
		info.channels = fmt.channels;
		info.samplesPerSec = fmt.samplesPerSec;
		info.bitsPerSample = fmt.bitsPerSample;
		void *copy = NEW char[dataLen];
		memcpy(copy, dataPtr, dataLen);
		info.data = copy;
		info.dataLen = dataLen;
		ppDecodedPcm = copy;
		return true;
	}

	if (fmt.formatTag == WAVE_FMT_IMA_ADPCM) {
		UnsignedInt outSamples = 0;
		Short *decoded = DecodeImaAdpcm(dataPtr, dataLen, fmt.channels, fmt.blockAlign, outSamples);
		if (!decoded) {
			return false;
		}
		info.channels = fmt.channels;
		info.samplesPerSec = fmt.samplesPerSec;
		info.bitsPerSample = 16;
		info.data = decoded;
		info.dataLen = outSamples * fmt.channels * sizeof(Short);
		ppDecodedPcm = decoded;
		return true;
	}

	return false;
}
