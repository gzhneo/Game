/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Minimal test target without an external framework: each test function returns true on
// success, main() collects failures and returns a non-zero exit code if any test fails (ctest
// treats that as failure). No Catch2/gtest, to avoid another dependency for a few numeric
// regression tests.
//
// Covers among others:
//   - Vector4::operator-= (a sign error in W once made the shell map invisible)
//   - Matrix3D::Get_Inverse (uses Matrix4x4::Inverse() -> Vector4::operator-= internally)
//   - BezierSegment::evaluateBezSegmentAtT (WWMath code used in place of D3DX)

#include "PreRTS.h"
#include "WWMath/vector4.h"
#include "WWMath/matrix3d.h"
#include "Common/BezierSegment.h"
#include "Common/STLTypedefs.h"
#include "XAudio2Device/WavParser.h"
#include "Compression.h"
#include "Common/crc.h"
// Only for comparing the key codes; the game itself does not include dinput.h
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include "GameClient/KeyDefs.h"
#include "GameClient/KeyScanCode.h"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>
#include "Lib/SafeString.h"
#include "trim.h"
#include "INI.H"
#include "trect.h"
#include <string>
#if defined(_DEBUG) && defined(_MSC_VER)
#include <crtdbg.h>
#endif

static const float kEpsilon = 0.0005f;

static bool nearlyEqual(float a, float b, float eps = kEpsilon)
{
	return fabsf(a - b) <= eps;
}

//-------------------------------------------------------------------------------------------------
// Vector4::operator-=/operator+= -- the W field must apply the component operation for both
// += AND -= (guards against a sign typo in W from the original code).
//-------------------------------------------------------------------------------------------------
static bool test_Vector4_AddSub(void)
{
	Vector4 a(1.0f, 2.0f, 3.0f, 4.0f);
	Vector4 b(0.5f, 0.5f, 0.5f, 0.5f);

	Vector4 sum = a;
	sum += b;
	if (!nearlyEqual(sum.W, 4.5f)) {
		printf("FAIL test_Vector4_AddSub: += gave W=%f, expected 4.5\n", sum.W);
		return false;
	}

	Vector4 diff = a;
	diff -= b;
	if (!nearlyEqual(diff.X, 0.5f) || !nearlyEqual(diff.Y, 1.5f) ||
		!nearlyEqual(diff.Z, 2.5f) || !nearlyEqual(diff.W, 3.5f)) {
		printf("FAIL test_Vector4_AddSub: -= gave (%f,%f,%f,%f), expected (0.5,1.5,2.5,3.5)\n",
			diff.X, diff.Y, diff.Z, diff.W);
		return false;
	}

	return true;
}

//-------------------------------------------------------------------------------------------------
// Matrix3D::Get_Inverse -- M * Get_Inverse(M) must yield the identity for a matrix with rotation
// AND translation. Get_Inverse calls Matrix4x4::Inverse() (Gauss-Jordan, uses
// Vector4::operator-=); a wrong translation column in the inverse makes the product visibly
// deviate from the identity.
//-------------------------------------------------------------------------------------------------
static bool test_Matrix3D_GetInverse_RoundTrip(void)
{
	Matrix3D m(true); // identity as base
	m.Rotate_X(0.7f);
	m.Rotate_Y(0.3f);
	m.Translate(500.0f, 300.0f, 200.0f);

	Matrix3D inv(true);
	m.Get_Inverse(inv);

	Matrix3D product(true);
	Matrix3D::Multiply(m, inv, &product);

	static const Matrix3D identity(true);
	for (int row = 0; row < 3; row++) {
		for (int col = 0; col < 4; col++) {
			float expected = identity[row][col];
			float actual = product[row][col];
			if (!nearlyEqual(actual, expected)) {
				printf("FAIL test_Matrix3D_GetInverse_RoundTrip: M*Inverse(M)[%d][%d]=%f, expected %f\n",
					row, col, actual, expected);
				return false;
			}
		}
	}

	return true;
}

//-------------------------------------------------------------------------------------------------
// BezierSegment::evaluateBezSegmentAtT -- a straight line from (0,0,0) to (1,1,1) with evenly
// spaced control points must lie exactly on the line at t=0/0.5/1.
//-------------------------------------------------------------------------------------------------
static bool test_BezierSegment_StraightLine(void)
{
	BezierSegment seg(
		0.0f, 0.0f, 0.0f,
		1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f,
		2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f,
		1.0f, 1.0f, 1.0f);

	Coord3D result;

	seg.evaluateBezSegmentAtT(0.0f, &result);
	if (!nearlyEqual(result.x, 0.0f) || !nearlyEqual(result.y, 0.0f) || !nearlyEqual(result.z, 0.0f)) {
		printf("FAIL test_BezierSegment_StraightLine: t=0 gave (%f,%f,%f), expected (0,0,0)\n",
			result.x, result.y, result.z);
		return false;
	}

	seg.evaluateBezSegmentAtT(1.0f, &result);
	if (!nearlyEqual(result.x, 1.0f) || !nearlyEqual(result.y, 1.0f) || !nearlyEqual(result.z, 1.0f)) {
		printf("FAIL test_BezierSegment_StraightLine: t=1 gave (%f,%f,%f), expected (1,1,1)\n",
			result.x, result.y, result.z);
		return false;
	}

	seg.evaluateBezSegmentAtT(0.5f, &result);
	if (!nearlyEqual(result.x, 0.5f) || !nearlyEqual(result.y, 0.5f) || !nearlyEqual(result.z, 0.5f)) {
		printf("FAIL test_BezierSegment_StraightLine: t=0.5 gave (%f,%f,%f), expected (0.5,0.5,0.5)\n",
			result.x, result.y, result.z);
		return false;
	}

	return true;
}

//-------------------------------------------------------------------------------------------------
// rts::stlStyleStringHash / rts::hash<const char*> -- two buffers with identical content at
// different addresses must hash equally, and an unordered_map lookup with a DIFFERENT buffer
// than the one used for insertion must find the entry. The test fails if stlStyleStringHash is
// replaced by std::hash<const char*> (which hashes pointer identity, not content). Tested with
// raw const char* buffers instead of AsciiString: AsciiString depends on the engine memory pool
// (TheDynamicMemoryAllocator/MemoryPoolFactory), whose full initialization would be excessive
// here -- the hash logic lives in stlStyleStringHash(), not in AsciiString.
//-------------------------------------------------------------------------------------------------
static bool test_StlStyleStringHash_ContentBased(void)
{
	char bufferA[64];
	char bufferB[64];
	safe_strcpy(bufferA, "ChinaVehicleSupplyTruck");
	safe_strcpy(bufferB, "ChinaVehicleSupplyTruck"); // separate buffer, different address, same content

	if (static_cast<const char *>(bufferA) == static_cast<const char *>(bufferB)) {	// explicit pointer comparison (C++20 warning C5056)
		printf("FAIL test_StlStyleStringHash_ContentBased: broken test setup, same buffer address\n");
		return false;
	}

	size_t hashA = rts::stlStyleStringHash(bufferA);
	size_t hashB = rts::stlStyleStringHash(bufferB);
	if (hashA != hashB) {
		printf("FAIL test_StlStyleStringHash_ContentBased: different hashes for identical content (%zu vs %zu)\n",
			hashA, hashB);
		return false;
	}

	std::unordered_map<const char*, int, rts::hash<const char*>, rts::equal_to<const char*> > map;
	map[bufferA] = 42;

	std::unordered_map<const char*, int, rts::hash<const char*>, rts::equal_to<const char*> >::iterator it = map.find(bufferB);
	if (it == map.end() || it->second != 42) {
		printf("FAIL test_StlStyleStringHash_ContentBased: unordered_map lookup with a different buffer (same content) failed\n");
		return false;
	}

	return true;
}

//-------------------------------------------------------------------------------------------------
// ParseWavAndDecode -- synthetic RIFF/WAVE buffers for PCM and IMA-ADPCM, assembled by hand (no
// test asset needed in the repo).
//-------------------------------------------------------------------------------------------------
static void appendChunk(std::vector<UnsignedByte> &buf, const char tag[4], const void *data, UnsignedInt dataLen)
{
	buf.insert(buf.end(), tag, tag + 4);
	buf.push_back(static_cast<UnsignedByte>(dataLen & 0xFF));
	buf.push_back(static_cast<UnsignedByte>((dataLen >> 8) & 0xFF));
	buf.push_back(static_cast<UnsignedByte>((dataLen >> 16) & 0xFF));
	buf.push_back(static_cast<UnsignedByte>((dataLen >> 24) & 0xFF));
	const UnsignedByte *bytes = static_cast<const UnsignedByte*>(data);
	buf.insert(buf.end(), bytes, bytes + dataLen);
	if (dataLen & 1) buf.push_back(0); // chunks are word aligned
}

#pragma pack(push,1)
struct TestWavFmtChunk
{
	WORD formatTag;
	WORD channels;
	DWORD samplesPerSec;
	DWORD avgBytesPerSec;
	WORD blockAlign;
	WORD bitsPerSample;
};
#pragma pack(pop)

static std::vector<UnsignedByte> buildRiffWave(const TestWavFmtChunk &fmt, const void *pcmData, UnsignedInt pcmLen)
{
	std::vector<UnsignedByte> riffBody;
	// "WAVE" must be evaluated only ONCE: two occurrences of the same string literal are not
	// guaranteed to share an address (no string pooling in debug builds) -- first/last would come
	// from unrelated pointers, which the MSVC debug STL rightly rejects as "transposed pointer
	// range".
	static const char kWaveTag[4] = { 'W', 'A', 'V', 'E' };
	riffBody.insert(riffBody.end(), reinterpret_cast<const UnsignedByte*>(kWaveTag), reinterpret_cast<const UnsignedByte*>(kWaveTag) + 4);
	appendChunk(riffBody, "fmt ", &fmt, sizeof(fmt));
	appendChunk(riffBody, "data", pcmData, pcmLen);

	std::vector<UnsignedByte> file;
	appendChunk(file, "RIFF", riffBody.data(), (UnsignedInt)riffBody.size());
	// appendChunk treats "RIFF" like a normal chunk with a length prefix, which is correct here:
	// the RIFF header itself is "RIFF" + length + "WAVE" + chunks.
	return file;
}

static bool test_WavParser_Pcm(void)
{
	Short pcmSamples[4] = { 100, -200, 300, -400 };

	TestWavFmtChunk fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.formatTag = 1; // WAVE_FORMAT_PCM
	fmt.channels = 1;
	fmt.samplesPerSec = 22050;
	fmt.avgBytesPerSec = 22050 * 2;
	fmt.blockAlign = 2;
	fmt.bitsPerSample = 16;

	std::vector<UnsignedByte> wav = buildRiffWave(fmt, pcmSamples, sizeof(pcmSamples));

	WaveFormatInfo info;
	memset(&info, 0, sizeof(info));
	void *decoded = nullptr;
	Bool ok = ParseWavAndDecode(wav.data(), (UnsignedInt)wav.size(), info, decoded);
	if (!ok) {
		printf("FAIL test_WavParser_Pcm: ParseWavAndDecode reported an error\n");
		return false;
	}
	if (info.channels != 1 || info.samplesPerSec != 22050 || info.bitsPerSample != 16 ||
		info.dataLen != sizeof(pcmSamples) || memcmp(info.data, pcmSamples, sizeof(pcmSamples)) != 0) {
		printf("FAIL test_WavParser_Pcm: decoded data differ from the original\n");
		delete[] static_cast<char*>(decoded);
		return false;
	}
	delete[] static_cast<char*>(decoded);
	return true;
}

static bool test_WavParser_ImaAdpcm(void)
{
	// A single mono IMA-ADPCM block: 4-byte header (predictor=0, step index=0, reserved),
	// followed by nibble data. blockAlign=8 -> 4 header bytes + 4 nibble bytes = 1 header sample
	// + 8 nibble samples. Nibble value 0 is a valid, deterministic decode step per
	// ImaIndexTable/ImaStepTable -- the test does not check exact waveform values (that would
	// test the IMA-ADPCM spec itself), only that the parser decodes a plausible sample count.
	UnsignedByte block[8] = { 0, 0, 0, 0, 0x00, 0x00, 0x00, 0x00 };

	TestWavFmtChunk fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.formatTag = 0x0011; // WAVE_FORMAT_IMA_ADPCM
	fmt.channels = 1;
	fmt.samplesPerSec = 22050;
	fmt.avgBytesPerSec = 22050;
	fmt.blockAlign = 8;
	fmt.bitsPerSample = 4;

	std::vector<UnsignedByte> wav = buildRiffWave(fmt, block, sizeof(block));

	WaveFormatInfo info;
	memset(&info, 0, sizeof(info));
	void *decoded = nullptr;
	Bool ok = ParseWavAndDecode(wav.data(), (UnsignedInt)wav.size(), info, decoded);
	if (!ok) {
		printf("FAIL test_WavParser_ImaAdpcm: ParseWavAndDecode reported an error\n");
		return false;
	}
	// 1 block * (1 header sample + (8-4)*2 nibble samples) = 9 samples * 2 bytes = 18 bytes.
	UnsignedInt expectedBytes = 9 * sizeof(Short);
	if (info.channels != 1 || info.bitsPerSample != 16 || info.dataLen != expectedBytes) {
		printf("FAIL test_WavParser_ImaAdpcm: expected dataLen=%u, got %u\n", expectedBytes, info.dataLen);
		delete[] static_cast<char*>(decoded);
		return false;
	}
	delete[] static_cast<char*>(decoded);
	return true;
}

// Stereo IMA-ADPCM. One block with two groups of eight: channel 0 only nibble 1, channel 1 only
// nibble 9. At step index 0 (step 7) that is +1 resp. -1 per sample and the step index stays 0;
// decoded correctly this yields 0, 1, ..., 16 and 0, -1, ..., -16 (17 samples per channel).
// Checks that interleaved channels are written to distinct positions within the output buffer.
static bool test_WavParser_ImaAdpcmStereo(void)
{
	UnsignedByte block[24];
	memset(block, 0, sizeof(block));	// header per channel: predictor 0, step index 0
	for (int g = 0; g < 2; ++g)
	{
		for (int i = 0; i < 4; ++i)
		{
			block[8 + g * 8 + i] = 0x11;		// channel 0
			block[8 + g * 8 + 4 + i] = 0x99;	// channel 1
		}
	}

	TestWavFmtChunk fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.formatTag = 0x0011; // WAVE_FORMAT_IMA_ADPCM
	fmt.channels = 2;
	fmt.samplesPerSec = 22050;
	fmt.avgBytesPerSec = 22050;
	fmt.blockAlign = sizeof(block);
	fmt.bitsPerSample = 4;

	std::vector<UnsignedByte> wav = buildRiffWave(fmt, block, sizeof(block));

	WaveFormatInfo info;
	memset(&info, 0, sizeof(info));
	void *decoded = nullptr;
	Bool ok = ParseWavAndDecode(wav.data(), (UnsignedInt)wav.size(), info, decoded);
	if (!ok) {
		printf("FAIL test_WavParser_ImaAdpcmStereo: ParseWavAndDecode reported an error\n");
		return false;
	}
	const UnsignedInt samplesPerChannel = 1 + 16;
	bool gut = (info.channels == 2 && info.dataLen == samplesPerChannel * 2 * sizeof(Short));
	const Short *s = static_cast<const Short *>(info.data);
	for (UnsignedInt i = 1; gut && i < samplesPerChannel; ++i)
	{
		if (s[i * 2 + 0] != static_cast<Short>(i) || s[i * 2 + 1] != static_cast<Short>(-static_cast<Int>(i)))
			gut = false;
	}
	if (!gut) {
		printf("FAIL test_WavParser_ImaAdpcmStereo: wrong channel values (dataLen %u)\n", info.dataLen);
		delete[] static_cast<char*>(decoded);
		return false;
	}
	delete[] static_cast<char*>(decoded);
	return true;
}

//-------------------------------------------------------------------------------------------------
// CompressionManager RefPack/ZLib round trip.
//-------------------------------------------------------------------------------------------------
static bool test_CompressionManager_RoundTrip(CompressionType compType, const char *typeName)
{
	char original[256];
	for (int i = 0; i < 255; i++) {
		// Repeating but non-trivial pattern -- compressible without an encoder edge case (e.g.
		// fully constant data) skewing the result.
		original[i] = static_cast<char>('A' + (i * 7) % 23);
	}
	original[255] = '\0';
	Int origLen = static_cast<Int>(sizeof(original));

	Int maxCompressed = CompressionManager::getMaxCompressedSize(origLen, compType);
	std::vector<char> compressed(maxCompressed);
	Int compressedLen = CompressionManager::compressData(compType, original, origLen, compressed.data(), maxCompressed);
	if (compressedLen <= 0) {
		printf("FAIL test_CompressionManager_RoundTrip(%s): compressData returned %d\n", typeName, compressedLen);
		return false;
	}

	if (!CompressionManager::isDataCompressed(compressed.data(), compressedLen)) {
		printf("FAIL test_CompressionManager_RoundTrip(%s): isDataCompressed does not recognize its own header\n", typeName);
		return false;
	}

	std::vector<char> decompressed(origLen);
	Int decompressedLen = CompressionManager::decompressData(compressed.data(), compressedLen, decompressed.data(), origLen);
	if (decompressedLen != origLen || memcmp(decompressed.data(), original, origLen) != 0) {
		printf("FAIL test_CompressionManager_RoundTrip(%s): decompressData returned %d bytes, expected %d, or content differs\n",
			typeName, decompressedLen, origLen);
		return false;
	}

	return true;
}

static bool test_CompressionManager_RefPack(void)
{
	return test_CompressionManager_RoundTrip(COMPRESSION_REFPACK, "RefPack");
}

static bool test_CompressionManager_ZLib(void)
{
	return test_CompressionManager_RoundTrip(COMPRESSION_ZLIB6, "ZLib6");
}

//-------------------------------------------------------------------------------------------------
// EA codecs (RefPack, Huffman, BTree) over varied data: the bounded decoders must return exactly
// what the encoders put in, for small and large inputs and different symbol distributions.
//-------------------------------------------------------------------------------------------------
static bool test_CompressionManager_EACodecs(void)
{
	static const CompressionType types[] = { COMPRESSION_REFPACK, COMPRESSION_HUFF, COMPRESSION_BTREE };
	static const char *names[] = { "RefPack", "Huff", "BTree" };
	static const Int lengths[] = { 1, 17, 300, 4096, 65536 };
	static const Int alphabets[] = { 2, 16, 64, 256 };
	bool ok = true;
	UnsignedInt seed = 12345u;
	for (Int t = 0; t < 3; ++t)
		for (Int li = 0; li < 5; ++li)
			for (Int ai = 0; ai < 4; ++ai)
			{
				const Int len = lengths[li];
				std::vector<char> original(len);
				for (Int i = 0; i < len; ++i) {
					seed = seed * 1103515245u + 12345u;
					// Skewed distribution: runs and a few frequent symbols, like real game data.
					const UnsignedInt r = seed >> 16;
					original[i] = static_cast<char>((r % 4 == 0 && i > 0) ? original[i - 1] : (r % alphabets[ai]));
				}
				const Int maxCompressed = CompressionManager::getMaxCompressedSize(len, types[t]) + 64;
				std::vector<char> compressed(maxCompressed);
				const Int compressedLen = CompressionManager::compressData(types[t], original.data(), len, compressed.data(), maxCompressed);
				if (compressedLen <= 0) {
					printf("FAIL test_CompressionManager_EACodecs(%s, %d, %d): compressData returned %d\n", names[t], len, alphabets[ai], compressedLen);
					ok = false;
					continue;
				}
				std::vector<char> decompressed(len);
				const Int decompressedLen = CompressionManager::decompressData(compressed.data(), compressedLen, decompressed.data(), len);
				if (decompressedLen != len || memcmp(decompressed.data(), original.data(), len) != 0) {
					printf("FAIL test_CompressionManager_EACodecs(%s, %d, %d): decompressData returned %d bytes or content differs\n",
						names[t], len, alphabets[ai], decompressedLen);
					ok = false;
				}
			}
	return ok;
}


//-------------------------------------------------------------------------------------------------
// CRC -- the class the replay determinism check measures with. Common/crc.h has a debug version
// and a hand-optimized release version (#ifdef _DEBUG); both must yield these values, otherwise
// the check that should find deviations measures wrongly itself. Expected values computed
// independently: crc = 0; per byte: hibit = crc >> 31; crc = (crc << 1) + byte + hibit.
//-------------------------------------------------------------------------------------------------
static bool test_CRC_KnownVector(void)
{
	struct Case { const char *text; UnsignedInt expected; };
	static const Case cases[] = {
		{ "",                                     0x00000000u },
		{ "A",                                    0x00000041u },
		{ "Generals",                             0x0000572Fu },
		{ "Command & Conquer Generals Zero Hour", 0x929DBFB0u },
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		CRC c;
		c.computeCRC(cases[i].text, static_cast<Int>(strlen(cases[i].text)));
		if (c.get() != cases[i].expected) {
			printf("FAIL test_CRC_KnownVector: \"%s\" gave 0x%08X, expected 0x%08X\n",
				cases[i].text, static_cast<unsigned>(c.get()), static_cast<unsigned>(cases[i].expected));
			return false;
		}
	}

	// Feeding piecewise must give the same result as all at once -- the replay run feeds the
	// checksum in pieces.
	CRC whole;
	whole.computeCRC("Command & Conquer", 17);
	CRC pieces;
	pieces.computeCRC("Command", 7);
	pieces.computeCRC(" & Conquer", 10);
	if (whole.get() != pieces.get()) {
		printf("FAIL test_CRC_KnownVector: piecewise 0x%08X, in one piece 0x%08X\n",
			static_cast<unsigned>(pieces.get()), static_cast<unsigned>(whole.get()));
		return false;
	}

	// A null pointer or zero length must not change the checksum.
	CRC untouched;
	untouched.computeCRC("Generals", 8);
	const UnsignedInt before = untouched.get();
	untouched.computeCRC(nullptr, 4);
	untouched.computeCRC("x", 0);
	if (untouched.get() != before) {
		printf("FAIL test_CRC_KnownVector: empty input changed the checksum\n");
		return false;
	}
	return true;
}

//-------------------------------------------------------------------------------------------------
// REAL_TO_INT -- truncation toward zero. The test pins this so a change to Lib/BaseType.h does
// not silently alter it: the macros are used throughout the game logic.
//-------------------------------------------------------------------------------------------------
static bool test_RealToInt_Truncation(void)
{
	struct Case { Real in; Int expected; };
	static const Case cases[] = {
		{  0.0f,  0 }, {  0.4f,  0 }, {  0.5f,  0 }, {  0.9f,  0 },
		{  1.0f,  1 }, {  1.9f,  1 }, { 13.0f, 13 }, { 42.7f, 42 },
		{ -0.4f,  0 }, { -0.9f,  0 }, { -1.0f, -1 }, { -1.9f, -1 }, { -13.0f, -13 },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		const Int got = REAL_TO_INT(cases[i].in);
		if (got != cases[i].expected) {
			printf("FAIL test_RealToInt_Truncation: REAL_TO_INT(%g) gave %d, expected %d\n",
				cases[i].in, got, cases[i].expected);
			return false;
		}
	}
	return true;
}

//-------------------------------------------------------------------------------------------------
// REAL_TO_INT_CEIL / _FLOOR -- rounding up and down, focused on exact integer arguments.
//
// A ceil/floor that adds or subtracts 0.99999994 and then truncates is wrong for exact integers:
// the sum is not representable and rounds to the next integer, so ceil(13) gives 14 and
// floor(-13) gives -14 for every integer with magnitude >= 1. The macros are used in the
// pathfinder (AIPathfind.h) and the PartitionManager, i.e. in the middle of the simulation.
//-------------------------------------------------------------------------------------------------
static bool test_RealToIntCeilFloor_ExactIntegers(void)
{
	struct Case { Real in; Int ceilExpected; Int floorExpected; };
	static const Case cases[] = {
		{      0.0f,      0,      0 },
		{      0.5f,      1,      0 },
		{      1.0f,      1,      1 },
		{      1.5f,      2,      1 },
		{     13.0f,     13,     13 },
		{     13.5f,     14,     13 },
		{    100.0f,    100,    100 },
		{ 100000.0f, 100000, 100000 },
		{     -0.5f,      0,     -1 },
		{     -1.0f,     -1,     -1 },
		{     -1.5f,     -1,     -2 },
		{    -13.0f,    -13,    -13 },
		{    -13.5f,    -13,    -14 },
		{   -100.0f,   -100,   -100 },
	};

	bool ok = true;
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		const Real x = cases[i].in;
		const Int gotCeil = REAL_TO_INT_CEIL(x);
		if (gotCeil != cases[i].ceilExpected) {
			printf("FAIL test_RealToIntCeilFloor_ExactIntegers: REAL_TO_INT_CEIL(%g) gave %d, expected %d\n",
				x, gotCeil, cases[i].ceilExpected);
			ok = false;
		}
		const Int gotFloor = REAL_TO_INT_FLOOR(x);
		if (gotFloor != cases[i].floorExpected) {
			printf("FAIL test_RealToIntCeilFloor_ExactIntegers: REAL_TO_INT_FLOOR(%g) gave %d, expected %d\n",
				x, gotFloor, cases[i].floorExpected);
			ok = false;
		}
	}

	// Integers over a wide range: this is where a rounding-trick "optimization" would fail.
	for (Int n = -20000; n <= 20000; n += 7) {
		const Real x = static_cast<Real>(n);
		if (REAL_TO_INT_CEIL(x) != n || REAL_TO_INT_FLOOR(x) != n) {
			printf("FAIL test_RealToIntCeilFloor_ExactIntegers: integer %d gave ceil=%d floor=%d\n",
				n, REAL_TO_INT_CEIL(x), REAL_TO_INT_FLOOR(x));
			ok = false;
			break;
		}
	}
	return ok;
}

//-------------------------------------------------------------------------------------------------
// Coord2D::toAngle -- direction of a 2D vector as an angle, 0 along +x, sign by rotation
// direction, computed with atan2f. The test checks the four axes and the diagonals exactly, the
// zero vector, and compares many directions against a double reference -- this catches a switch
// to a worse-conditioned formula (such as the arccosine of the normalized x component).
//-------------------------------------------------------------------------------------------------
static bool test_Coord2D_ToAngle(void)
{
	bool ok = true;
	const Real pi = 3.14159265358979323846f;

	struct Case { Real x, y, expected; const char *name; };
	const Case cases[] = {
		{  1.0f,  0.0f,        0.0f, "+x"        },
		{  0.0f,  1.0f,    pi / 2.0f, "+y"       },
		{ -1.0f,  0.0f,          pi, "-x"        },
		{  0.0f, -1.0f,   -pi / 2.0f, "-y"       },
		{  1.0f,  1.0f,    pi / 4.0f, "+x+y"     },
		{ -1.0f,  1.0f, 3.0f*pi/4.0f, "-x+y"     },
		{  1.0f, -1.0f,   -pi / 4.0f, "+x-y"     },
		{  0.0f,  0.0f,        0.0f, "Nullvektor" },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		Coord2D v;
		v.x = cases[i].x;
		v.y = cases[i].y;
		const Real got = v.toAngle();
		if (!nearlyEqual(got, cases[i].expected, 1.0e-6f)) {
			printf("FAIL test_Coord2D_ToAngle: %s gave %f, expected %f\n",
				cases[i].name, got, cases[i].expected);
			ok = false;
		}
	}

	// The length must not affect the angle.
	Coord2D shortV, longV;
	shortV.x = 0.001f;  shortV.y = 0.002f;
	longV.x  = 1000.0f; longV.y  = 2000.0f;
	if (!nearlyEqual(shortV.toAngle(), longV.toAngle(), 1.0e-6f)) {
		printf("FAIL test_Coord2D_ToAngle: length affects the angle (%f vs %f)\n",
			shortV.toAngle(), longV.toAngle());
		ok = false;
	}

	// Against a double reference, focused near the x axis -- where an arccosine formula is least
	// accurate (errors up to 3.9e-04).
	double worst = 0.0;
	for (int i = -500; i <= 500; i++) {
		for (int k = 0; k < 3; k++) {
			Coord2D v;
			v.x = static_cast<Real>(i) + 0.5f;
			v.y = (k == 0) ? static_cast<Real>(i) * 0.001f
			   : ((k == 1) ? static_cast<Real>(i) * 0.000001f : static_cast<Real>(-i) + 0.25f);
			if (v.x == 0.0f && v.y == 0.0f) continue;
			const double ref = atan2(static_cast<double>(v.y), static_cast<double>(v.x));
			const double err = fabs(static_cast<double>(v.toAngle()) - ref);
			if (err > worst) worst = err;
		}
	}
	if (worst > 1.0e-6) {
		printf("FAIL test_Coord2D_ToAngle: largest error %.3e vs atan2 in double "
			   "(allowed 1e-06)\n", worst);
		ok = false;
	}
	return ok;
}

//-------------------------------------------------------------------------------------------------
// KeyDefs.h does not include dinput.h; the key codes are plain numbers there. They must stay the
// DirectInput values (CommandMap.ini, bindings, replays) -- each one is checked against dinput.h
// here. Also the mapping Windows scan code -> key code (KeyScanCode.h) for the special cases,
// and against MapVirtualKey for all virtual keys Windows knows on the current keyboard layout.
//-------------------------------------------------------------------------------------------------
static bool test_KeyDefs_DInput(void)
{
	struct Pair { const char *name; int ours; int dinput; };
	static const Pair pairs[] = {
		{ "KEY_KP0", KEY_KP0, DIK_NUMPAD0 },
		{ "KEY_KP1", KEY_KP1, DIK_NUMPAD1 },
		{ "KEY_KP2", KEY_KP2, DIK_NUMPAD2 },
		{ "KEY_KP3", KEY_KP3, DIK_NUMPAD3 },
		{ "KEY_KP4", KEY_KP4, DIK_NUMPAD4 },
		{ "KEY_KP5", KEY_KP5, DIK_NUMPAD5 },
		{ "KEY_KP6", KEY_KP6, DIK_NUMPAD6 },
		{ "KEY_KP7", KEY_KP7, DIK_NUMPAD7 },
		{ "KEY_KP8", KEY_KP8, DIK_NUMPAD8 },
		{ "KEY_KP9", KEY_KP9, DIK_NUMPAD9 },
		{ "KEY_KPDEL", KEY_KPDEL, DIK_NUMPADPERIOD },
		{ "KEY_KPSTAR", KEY_KPSTAR, DIK_NUMPADSTAR },
		{ "KEY_KPMINUS", KEY_KPMINUS, DIK_NUMPADMINUS },
		{ "KEY_KPPLUS", KEY_KPPLUS, DIK_NUMPADPLUS },
		{ "KEY_ESC", KEY_ESC, DIK_ESCAPE },
		{ "KEY_BACKSPACE", KEY_BACKSPACE, DIK_BACK },
		{ "KEY_ENTER", KEY_ENTER, DIK_RETURN },
		{ "KEY_SPACE", KEY_SPACE, DIK_SPACE },
		{ "KEY_TAB", KEY_TAB, DIK_TAB },
		{ "KEY_F1", KEY_F1, DIK_F1 },
		{ "KEY_F2", KEY_F2, DIK_F2 },
		{ "KEY_F3", KEY_F3, DIK_F3 },
		{ "KEY_F4", KEY_F4, DIK_F4 },
		{ "KEY_F5", KEY_F5, DIK_F5 },
		{ "KEY_F6", KEY_F6, DIK_F6 },
		{ "KEY_F7", KEY_F7, DIK_F7 },
		{ "KEY_F8", KEY_F8, DIK_F8 },
		{ "KEY_F9", KEY_F9, DIK_F9 },
		{ "KEY_F10", KEY_F10, DIK_F10 },
		{ "KEY_F11", KEY_F11, DIK_F11 },
		{ "KEY_F12", KEY_F12, DIK_F12 },
		{ "KEY_A", KEY_A, DIK_A },
		{ "KEY_B", KEY_B, DIK_B },
		{ "KEY_C", KEY_C, DIK_C },
		{ "KEY_D", KEY_D, DIK_D },
		{ "KEY_E", KEY_E, DIK_E },
		{ "KEY_F", KEY_F, DIK_F },
		{ "KEY_G", KEY_G, DIK_G },
		{ "KEY_H", KEY_H, DIK_H },
		{ "KEY_I", KEY_I, DIK_I },
		{ "KEY_J", KEY_J, DIK_J },
		{ "KEY_K", KEY_K, DIK_K },
		{ "KEY_L", KEY_L, DIK_L },
		{ "KEY_M", KEY_M, DIK_M },
		{ "KEY_N", KEY_N, DIK_N },
		{ "KEY_O", KEY_O, DIK_O },
		{ "KEY_P", KEY_P, DIK_P },
		{ "KEY_Q", KEY_Q, DIK_Q },
		{ "KEY_R", KEY_R, DIK_R },
		{ "KEY_S", KEY_S, DIK_S },
		{ "KEY_T", KEY_T, DIK_T },
		{ "KEY_U", KEY_U, DIK_U },
		{ "KEY_V", KEY_V, DIK_V },
		{ "KEY_W", KEY_W, DIK_W },
		{ "KEY_X", KEY_X, DIK_X },
		{ "KEY_Y", KEY_Y, DIK_Y },
		{ "KEY_Z", KEY_Z, DIK_Z },
		{ "KEY_1", KEY_1, DIK_1 },
		{ "KEY_2", KEY_2, DIK_2 },
		{ "KEY_3", KEY_3, DIK_3 },
		{ "KEY_4", KEY_4, DIK_4 },
		{ "KEY_5", KEY_5, DIK_5 },
		{ "KEY_6", KEY_6, DIK_6 },
		{ "KEY_7", KEY_7, DIK_7 },
		{ "KEY_8", KEY_8, DIK_8 },
		{ "KEY_9", KEY_9, DIK_9 },
		{ "KEY_0", KEY_0, DIK_0 },
		{ "KEY_MINUS", KEY_MINUS, DIK_MINUS },
		{ "KEY_EQUAL", KEY_EQUAL, DIK_EQUALS },
		{ "KEY_LBRACKET", KEY_LBRACKET, DIK_LBRACKET },
		{ "KEY_RBRACKET", KEY_RBRACKET, DIK_RBRACKET },
		{ "KEY_SEMICOLON", KEY_SEMICOLON, DIK_SEMICOLON },
		{ "KEY_APOSTROPHE", KEY_APOSTROPHE, DIK_APOSTROPHE },
		{ "KEY_TICK", KEY_TICK, DIK_GRAVE },
		{ "KEY_BACKSLASH", KEY_BACKSLASH, DIK_BACKSLASH },
		{ "KEY_COMMA", KEY_COMMA, DIK_COMMA },
		{ "KEY_PERIOD", KEY_PERIOD, DIK_PERIOD },
		{ "KEY_SLASH", KEY_SLASH, DIK_SLASH },
		{ "KEY_SYSREQ", KEY_SYSREQ, DIK_SYSRQ },
		{ "KEY_CAPS", KEY_CAPS, DIK_CAPSLOCK },
		{ "KEY_NUM", KEY_NUM, DIK_NUMLOCK },
		{ "KEY_SCROLL", KEY_SCROLL, DIK_SCROLL },
		{ "KEY_LCTRL", KEY_LCTRL, DIK_LCONTROL },
		{ "KEY_LALT", KEY_LALT, DIK_LALT },
		{ "KEY_LSHIFT", KEY_LSHIFT, DIK_LSHIFT },
		{ "KEY_RSHIFT", KEY_RSHIFT, DIK_RSHIFT },
		{ "KEY_UP", KEY_UP, DIK_UPARROW },
		{ "KEY_DOWN", KEY_DOWN, DIK_DOWNARROW },
		{ "KEY_LEFT", KEY_LEFT, DIK_LEFTARROW },
		{ "KEY_RIGHT", KEY_RIGHT, DIK_RIGHTARROW },
		{ "KEY_RALT", KEY_RALT, DIK_RALT },
		{ "KEY_RCTRL", KEY_RCTRL, DIK_RCONTROL },
		{ "KEY_HOME", KEY_HOME, DIK_HOME },
		{ "KEY_END", KEY_END, DIK_END },
		{ "KEY_PGUP", KEY_PGUP, DIK_PGUP },
		{ "KEY_PGDN", KEY_PGDN, DIK_PGDN },
		{ "KEY_INS", KEY_INS, DIK_INSERT },
		{ "KEY_DEL", KEY_DEL, DIK_DELETE },
		{ "KEY_KPENTER", KEY_KPENTER, DIK_NUMPADENTER },
		{ "KEY_KPSLASH", KEY_KPSLASH, DIK_NUMPADSLASH },
		{ "KEY_102", KEY_102, DIK_OEM_102 },
		{ "KEY_KANA", KEY_KANA, DIK_KANA },
		{ "KEY_CONVERT", KEY_CONVERT, DIK_CONVERT },
		{ "KEY_NOCONVERT", KEY_NOCONVERT, DIK_NOCONVERT },
		{ "KEY_YEN", KEY_YEN, DIK_YEN },
		{ "KEY_CIRCUMFLEX", KEY_CIRCUMFLEX, DIK_CIRCUMFLEX },
		{ "KEY_KANJI", KEY_KANJI, DIK_KANJI },
	};
	bool ok = true;
	for (const Pair &p : pairs) {
		if (p.ours != p.dinput) {
			printf("FAIL test_KeyDefs_DInput: %s = 0x%02X, DirectInput 0x%02X\n", p.name, p.ours, p.dinput);
			ok = false;
		}
	}

	// Special cases of the scan code mapping (WM_KEYDOWN lParam: scan code, extended bit)
	struct Scan { unsigned sc; bool ext; int key; };
	static const Scan scans[] = {
		{ 0x1E, false, DIK_A }, { 0x1C, false, DIK_RETURN }, { 0x1C, true, DIK_NUMPADENTER },
		{ 0x1D, false, DIK_LCONTROL }, { 0x1D, true, DIK_RCONTROL }, { 0x38, false, DIK_LMENU },
		{ 0x38, true, DIK_RMENU }, { 0x2A, false, DIK_LSHIFT }, { 0x36, false, DIK_RSHIFT },
		{ 0x2A, true, 0 }, { 0x36, true, 0 }, { 0x45, true, DIK_NUMLOCK }, { 0x45, false, DIK_PAUSE },
		{ 0x46, false, DIK_SCROLL }, { 0x46, true, DIK_PAUSE }, { 0x37, true, DIK_SYSRQ },
		{ 0x54, false, DIK_SYSRQ }, { 0x37, false, DIK_MULTIPLY }, { 0x35, true, DIK_DIVIDE },
		{ 0x47, false, DIK_NUMPAD7 }, { 0x47, true, DIK_HOME }, { 0x48, true, DIK_UP },
		{ 0x53, false, DIK_DECIMAL }, { 0x53, true, DIK_DELETE }, { 0x56, false, DIK_OEM_102 },
		{ 0x5B, true, DIK_LWIN }, { 0x00, false, 0 }, { 0x9E, false, 0 },
	};
	for (const Scan &s : scans) {
		const int got = KeyScan::keyFromScanCode(s.sc, s.ext);
		if (got != s.key) {
			printf("FAIL test_KeyDefs_DInput: Scancode 0x%02X%s -> 0x%02X, expected 0x%02X\n",
				s.sc, s.ext ? " (E0)" : "", got, s.key);
			ok = false;
		}
	}

	// All virtual keys of the current layout: MapVirtualKey returns the scan code (E0 in the high
	// byte), which must map to a key code (except VKs without their own key).
	int mapped = 0;
	for (UINT vk = 1; vk < 256; vk++) {
		const UINT vsc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
		if (vsc == 0 || ((vsc >> 8) & 0xFF) == 0xE1)
			continue;
		const int key = KeyScan::keyFromScanCode(vsc & 0xFF, ((vsc >> 8) & 0xFF) == 0xE0);
		if (key != 0)
			mapped++;
	}
	if (mapped < 60) {
		printf("FAIL test_KeyDefs_DInput: only %d virtual keys mapped\n", mapped);
		ok = false;
	}
	return ok;
}


//-------------------------------------------------------------------------------------------------
// strtrim/wcstrim must trim both ends, also when leading whitespace moved the text forward.
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
// INIClass::Put_* / Get_*: text written and the value read back for hand-written and malformed input.
// The expected text was captured from the sscanf/snprintf implementation; it pins the behaviour
// that the std::format/std::from_chars implementation has to keep.
static bool test_INIClass_PutGet(void)
{
	INIClass ini;
	std::string log;
	char line[256];
	auto add = [&](const char *s) { log += s; log += "@"; };
	auto putRaw = [&](const char *raw) { ini.Put_String("S", "E", raw); };
	auto fnum = [&](double v) { std::snprintf(line, sizeof(line), "%.4f", v); return std::string(line); };

	// writing
	ini.Put_Int("S", "E", 5, 0); add(ini.Get_Alloc_String("S", "E", "?"));
	ini.Put_Int("S", "E", -5, 0); add(ini.Get_Alloc_String("S", "E", "?"));
	ini.Put_Int("S", "E", 255, 1); add(ini.Get_Alloc_String("S", "E", "?"));
	ini.Put_Int("S", "E", -1, 1); add(ini.Get_Alloc_String("S", "E", "?"));
	ini.Put_Int("S", "E", 255, 2); add(ini.Get_Alloc_String("S", "E", "?"));
	ini.Put_Int("S", "E", -256, 2); add(ini.Get_Alloc_String("S", "E", "?"));
	ini.Put_Hex("S", "E", 255); add(ini.Get_Alloc_String("S", "E", "?"));
	ini.Put_Hex("S", "E", -2); add(ini.Get_Alloc_String("S", "E", "?"));
	ini.Put_Float("S", "E", 1.5f); add(ini.Get_Alloc_String("S", "E", "?"));
	ini.Put_Float("S", "E", -0.125f); add(ini.Get_Alloc_String("S", "E", "?"));
	ini.Put_Float("S", "E", 123456.789f); add(ini.Get_Alloc_String("S", "E", "?"));
	ini.Put_Double("S", "E", 2.25); add(ini.Get_Alloc_String("S", "E", "?"));
	ini.Put_Rect("S", "E", TRect<int>(1, -2, 3, 4)); add(ini.Get_Alloc_String("S", "E", "?"));
	ini.Put_Point("S", "E", TPoint2D<int>(5, -6)); add(ini.Get_Alloc_String("S", "E", "?"));
	ini.Put_Point("S", "E", TPoint3D<int>(1, -2, 3)); add(ini.Get_Alloc_String("S", "E", "?"));
	ini.Put_Point("S", "E", TPoint3D<float>(1.5f, 2.0f, -3.25f)); add(ini.Get_Alloc_String("S", "E", "?"));

	// reading
	const char *ints[] = { "42", "-7", "$1F", "$ff", "1Fh", "ffH", "12h ", "0x10", "$", "$zz", "zz", "", " 5", "+3", "$-1", "7h" };
	for (const char *s : ints) { putRaw(s); std::snprintf(line, sizeof(line), "I[%s]=%d", s, ini.Get_Int("S", "E", -999)); add(line); }
	const char *hexes[] = { "FF", "0xff", "ff", "-1", "zz", "", " 10", "10 ", "1F,2" };
	for (const char *s : hexes) { putRaw(s); std::snprintf(line, sizeof(line), "H[%s]=%d", s, ini.Get_Hex("S", "E", -999)); add(line); }
	const char *floats[] = { "1.5", "-2.25", "+3.5", " 7", "1e2", "1.5abc", "abc", "", "50%", "1.5%", ".5", "5.", "-0" };
	for (const char *s : floats) {
		putRaw(s);
		std::snprintf(line, sizeof(line), "F[%s]=%s D[%s]=%s", s, fnum(ini.Get_Float("S", "E", -9.5f)).c_str(), s, fnum(ini.Get_Double("S", "E", -9.5)).c_str());
		add(line);
	}
	const char *rects[] = { "1,2,3,4", "1,2", "1", "-1,-2,-3,-4", " 5, 6, 7, 8", "1,2,3,4,5", "a,b,c,d", "", "1;2,3,4", "7,8,9" };
	for (const char *s : rects) {
		putRaw(s);
		TRect<int> r = ini.Get_Rect("S", "E", TRect<int>(-1, -2, -3, -4));
		std::snprintf(line, sizeof(line), "R[%s]=%d,%d,%d,%d", s, r.X, r.Y, r.Width, r.Height);
		add(line);
	}
	const char *p2[] = { "5,-6", "-1,2", " 3, 4", "8,9,10" };
	for (const char *s : p2) {
		putRaw(s);
		TPoint2D<int> p = ini.Get_Point("S", "E", TPoint2D<int>(-1, -1));
		std::snprintf(line, sizeof(line), "P2[%s]=%d,%d", s, p.X, p.Y);
		add(line);
	}
	const char *p3[] = { "1,2,3", "-1,-2,-3", " 4, 5, 6" };
	for (const char *s : p3) {
		putRaw(s);
		TPoint3D<int> p = ini.Get_Point("S", "E", TPoint3D<int>(-1, -1, -1));
		std::snprintf(line, sizeof(line), "P3[%s]=%d,%d,%d", s, p.X, p.Y, p.Z);
		add(line);
	}
	const char *f3[] = { "1.5,2,3.25", "-1.5,-2,-3", " 0.5, 1, 2" };
	for (const char *s : f3) {
		putRaw(s);
		TPoint3D<float> p = ini.Get_Point("S", "E", TPoint3D<float>(-1, -1, -1));
		std::snprintf(line, sizeof(line), "F3[%s]=%s,%s,%s", s, fnum(p.X).c_str(), fnum(p.Y).c_str(), fnum(p.Z).c_str());
		add(line);
	}
	const char *f2[] = { "1.5,2.5", "-1,-2", " 3.25, 4" };
	for (const char *s : f2) {
		putRaw(s);
		TPoint2D<float> p = ini.Get_Point("S", "E", TPoint2D<float>(-1, -1));
		std::snprintf(line, sizeof(line), "F2[%s]=%s,%s", s, fnum(p.X).c_str(), fnum(p.Y).c_str());
		add(line);
	}

	static const char *golden =
		"5@-5@FFh@FFFFFFFFh@$FF@$FFFFFF00@FF@FFFFFFFE@1.500000@-0.125000@123456.789062@2.250000@1,-2,3,4@5,-6@"
		"1,-2,3@1.500000,2.000000,-3.250000@I[42]=42@I[-7]=-7@I[$1F]=31@I[$ff]=255@I[1Fh]=31@I[ffH]=255@"
		"I[12h ]=12@I[0x10]=0@I[$]=-999@I[$zz]=-999@I[zz]=0@I[]=-999@I[ 5]=5@I[+3]=3@I[$-1]=-1@I[7h]=7@"
		"H[FF]=255@H[0xff]=255@H[ff]=255@H[-1]=-1@H[zz]=-999@H[]=-999@H[ 10]=16@H[10 ]=16@H[1F,2]=31@"
		"F[1.5]=1.5000 D[1.5]=1.5000@F[-2.25]=-2.2500 D[-2.25]=-2.2500@F[+3.5]=3.5000 D[+3.5]=3.5000@"
		"F[ 7]=7.0000 D[ 7]=7.0000@F[1e2]=100.0000 D[1e2]=100.0000@F[1.5abc]=1.5000 D[1.5abc]=1.5000@"
		"F[abc]=-9.5000 D[abc]=-9.5000@F[]=-9.5000 D[]=-9.5000@F[50%]=0.5000 D[50%]=0.5000@"
		"F[1.5%]=0.0150 D[1.5%]=0.0150@F[.5]=0.5000 D[.5]=0.5000@F[5.]=5.0000 D[5.]=5.0000@"
		"F[-0]=-0.0000 D[-0]=-0.0000@R[1,2,3,4]=1,2,3,4@R[1,2]=1,2,-3,-4@R[1]=1,-2,-3,-4@"
		"R[-1,-2,-3,-4]=-1,-2,-3,-4@R[ 5, 6, 7, 8]=5,6,7,8@R[1,2,3,4,5]=1,2,3,4@R[a,b,c,d]=-1,-2,-3,-4@"
		"R[]=0,0,0,0@R[1;2,3,4]=1,-2,-3,-4@R[7,8,9]=7,8,9,-4@P2[5,-6]=5,-6@P2[-1,2]=-1,2@P2[ 3, 4]=3,4@"
		"P2[8,9,10]=8,9@P3[1,2,3]=1,2,3@P3[-1,-2,-3]=-1,-2,-3@P3[ 4, 5, 6]=4,5,6@"
		"F3[1.5,2,3.25]=1.5000,2.0000,3.2500@F3[-1.5,-2,-3]=-1.5000,-2.0000,-3.0000@"
		"F3[ 0.5, 1, 2]=0.5000,1.0000,2.0000@F2[1.5,2.5]=1.5000,2.5000@F2[-1,-2]=-1.0000,-2.0000@"
		"F2[ 3.25, 4]=3.2500,4.0000@";
	if (log != golden) {
		printf("INIClass_PutGet output:%s%s%s", "\n", log.c_str(), "\n");
		return false;
	}
	return true;
}

static bool test_StrTrim_BothEnds(void)
{
	struct Case { const char *in; const char *out; };
	static const Case cases[] = {
		{ "", "" }, { "   ", "" }, { "a", "a" }, { "  a", "a" }, { "a  ", "a" },
		{ "  a  ", "a" }, { " \t key = value \r\n", "key = value" }, { "x y", "x y" },
	};
	bool ok = true;
	for (const Case &c : cases) {
		char buf[64];
		strcpy(buf, c.in);
		if (strcmp(strtrim(buf), c.out) != 0) {
			printf("FAIL test_StrTrim_BothEnds: \"%s\" -> \"%s\", expected \"%s\"\n", c.in, buf, c.out);
			ok = false;
		}
		wchar_t wbuf[64];
		wchar_t wout[64];
		mbstowcs(wbuf, c.in, 64);
		mbstowcs(wout, c.out, 64);
		if (wcscmp(wcstrim(wbuf), wout) != 0) {
			printf("FAIL test_StrTrim_BothEnds: wcstrim(\"%s\") wrong\n", c.in);
			ok = false;
		}
	}
	return ok;
}

//-------------------------------------------------------------------------------------------------
// Not covered: NameKeyGenerator. It determines the order in which INI entries are processed, so
// it matters for determinism -- but it derives from SubsystemInterface, whose constructor depends
// on TheSubsystemList. Testing it here would pull in the engine dependencies this target avoids
// (see CMakeLists.txt). It needs its own harness, not a stub.
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
int main(void)
{
#if defined(_DEBUG) && defined(_MSC_VER)
	// A CRT/STL debug assert would otherwise show a blocking modal dialog and hang every
	// automated ctest run (and CI) indefinitely -- redirect to stderr so a failure stays visible
	// but the process still ends.
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif

	struct TestCase { const char *name; bool (*fn)(void); };
	TestCase tests[] = {
		{ "Vector4_AddSub", test_Vector4_AddSub },
		{ "Matrix3D_GetInverse_RoundTrip", test_Matrix3D_GetInverse_RoundTrip },
		{ "BezierSegment_StraightLine", test_BezierSegment_StraightLine },
		{ "StlStyleStringHash_ContentBased", test_StlStyleStringHash_ContentBased },
		{ "WavParser_Pcm", test_WavParser_Pcm },
		{ "WavParser_ImaAdpcm", test_WavParser_ImaAdpcm },
		{ "WavParser_ImaAdpcmStereo", test_WavParser_ImaAdpcmStereo },
		{ "CompressionManager_RefPack", test_CompressionManager_RefPack },
		{ "CompressionManager_ZLib", test_CompressionManager_ZLib },
		{ "CompressionManager_EACodecs", test_CompressionManager_EACodecs },
		{ "CRC_KnownVector", test_CRC_KnownVector },
		{ "RealToInt_Truncation", test_RealToInt_Truncation },
		{ "RealToIntCeilFloor_ExactIntegers", test_RealToIntCeilFloor_ExactIntegers },
		{ "Coord2D_ToAngle", test_Coord2D_ToAngle },
		{ "KeyDefs_DInput", test_KeyDefs_DInput },
		{ "StrTrim_BothEnds", test_StrTrim_BothEnds },
		{ "INIClass_PutGet", test_INIClass_PutGet },
	};

	int failCount = 0;
	for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		printf("[RUN ] %s\n", tests[i].name);
		fflush(stdout);
		bool ok = tests[i].fn();
		printf("[%s] %s\n", ok ? "PASS" : "FAIL", tests[i].name);
		fflush(stdout);
		if (!ok) {
			failCount++;
		}
	}

	if (failCount == 0) {
		printf("All %d tests passed.\n", static_cast<int>(sizeof(tests) / sizeof(tests[0])));
		return 0;
	}
	printf("%d of %d tests FAILED.\n", failCount, static_cast<int>(sizeof(tests) / sizeof(tests[0])));
	return 1;
}
