/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: XAudio2AudioManager.cpp //////////////////////////////////////////////////////////////////
// See XAudio2AudioManager.h for an overview. The architecture (PlayingAudio bookkeeping,
// request-list processing, priority/limit rules) follows the original audio manager so it plugs
// into the device-independent SoundManager/MusicManager/AudioRequest machinery in
// Common/GameAudio.cpp unchanged.

#include "XAudio2Device/XAudio2AudioManager.h"
#include "XAudio2Device/WavParser.h"
#include "XAudio2Device/Mp3Parser.h"

#include "Common/AudioAffect.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/AudioRequest.h"
#include "Common/AudioSettings.h"
#include "Common/AsciiString.h"
#include "Common/AudioEventInfo.h"
#include "Common/FileSystem.h"
#include "Common/file.h"
#include "Common/GameCommon.h"
#include "Common/GameSounds.h"
#include "Common/GlobalData.h"
#include "Common/ScopedMutex.h"

#include "GameClient/DebugDisplay.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/View.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/TerrainLogic.h"

#include <math.h>

enum { INFINITE_LOOP_COUNT = 1000000 };

static XAudio2SoundCallback TheXAudio2SoundCallback;

//===================================================================================================
// XAudio2SoundCallback
//===================================================================================================
void STDMETHODCALLTYPE XAudio2SoundCallback::OnBufferEnd(void *pBufferContext)
{
	// Runs on the XAudio2 engine thread -- only flip a flag (checked/acted on next update() from the
	// main thread), matching the push-model thread-safety design described in GameAudio.h.
	PlayingAudio *playing = static_cast<PlayingAudio *>(pBufferContext);
	if (playing) {
		(static_cast<XAudio2AudioManager *>(TheAudio))->onVoiceBufferEnd(playing);
	}
}

void STDMETHODCALLTYPE XAudio2SoundCallback::OnLoopEnd(void *pBufferContext)
{
	// Music loops forever; the scripts ask how often a track has completed (hasMusicTrackCompleted).
	PlayingAudio *playing = static_cast<PlayingAudio *>(pBufferContext);
	if (playing) {
		InterlockedIncrement(&playing->m_loopsCompleted);
	}
}

//===================================================================================================
// AudioFileCache
//===================================================================================================
AudioFileCache::AudioFileCache() : m_maxSize(0), m_currentlyUsedSize(0), m_mutexName("XAudio2FileCacheMutex")
{
	m_mutex = CreateMutex(nullptr, FALSE, m_mutexName);
}

AudioFileCache::~AudioFileCache()
{
	{
		ScopedMutex mut(m_mutex);
		OpenFilesHashIt it;
		for (it = m_openFiles.begin(); it != m_openFiles.end(); ++it) {
			releaseOpenAudioFile(&it->second);
		}
	}
	CloseHandle(m_mutex);
}

void *AudioFileCache::openFile(AudioEventRTS *eventToOpenFrom)
{
	ScopedMutex mut(m_mutex);

	AsciiString strToFind;
	switch (eventToOpenFrom->getNextPlayPortion())
	{
		case PortionToPlay::PP_Attack: strToFind = eventToOpenFrom->getAttackFilename(); break;
		case PortionToPlay::PP_Sound:  strToFind = eventToOpenFrom->getFilename(); break;
		case PortionToPlay::PP_Decay:  strToFind = eventToOpenFrom->getDecayFilename(); break;
		case PortionToPlay::PP_Done:   return nullptr;
	}

	OpenFilesHash::iterator it = m_openFiles.find(strToFind);
	if (it != m_openFiles.end()) {
		++it->second.m_openCount;
		return it->second.m_fileData;
	}

	OpenAudioFile openedAudioFile;
	openedAudioFile.m_eventInfo = eventToOpenFrom->getAudioEventInfo();

	void *decoded = nullptr;
	Bool parsed = false;

	// Music assets are .mp3 (WavParser only understands RIFF/WAV) -- decode those
	// via the FFmpeg backend already vendored for video (FFmpegFile, VideoDevice/FFmpeg/FFmpegFile.h). FFmpegFile
	// opens the file itself via TheFileSystem, so no separate File*/readEntireAndClose() step here.
	if (strToFind.endsWithNoCase(".mp3")) {
		parsed = ParseMp3AndDecode(strToFind, openedAudioFile.m_soundInfo, decoded);
	} else {
		// Event without a sound file: nothing to load, do not ask the file system at all.
		if (strToFind.isEmpty()) {
			return nullptr;
		}
		File *file = TheFileSystem->openFile(strToFind.str());
		if (!file) {
			DEBUG_ASSERTLOG(strToFind.isEmpty(), ("Missing Audio File: '%s'\n", strToFind.str()));
			AudioLog("file missing %s (%s)", strToFind.str(), eventToOpenFrom->getEventName().str());
			return nullptr;
		}

		UnsignedInt rawSize = file->size();
		char *rawBuffer = file->readEntireAndClose();
		parsed = ParseWavAndDecode(reinterpret_cast<const UnsignedByte*>(rawBuffer), rawSize, openedAudioFile.m_soundInfo, decoded);
		delete[] rawBuffer;
	}

	if (!parsed) {
		DEBUG_CRASH(("Unrecognized/unsupported audio file '%s' (only WAV PCM/IMA-ADPCM and MP3 are supported)\n", strToFind.str()));
		AudioLog("file not readable %s (%s)", strToFind.str(), eventToOpenFrom->getEventName().str());
		return nullptr;
	}

	if (eventToOpenFrom->isPositionalAudio() && openedAudioFile.m_soundInfo.channels > 1) {
		DEBUG_CRASH(("Requested Positional Play of audio '%s', but it is not mono.", strToFind.str()));
		AudioLog("3D file not mono %s (%s)", strToFind.str(), eventToOpenFrom->getEventName().str());
		delete[] static_cast<char*>(decoded);
		return nullptr;
	}

	openedAudioFile.m_fileData = decoded;
	openedAudioFile.m_fileSize = openedAudioFile.m_soundInfo.dataLen;
	openedAudioFile.m_openCount = 1;

	// A music track is decoded to raw S16 PCM in full and can easily be tens of MB for one
	// multi-minute song -- far larger than m_maxSize, which is sized for compressed WAV/IMA-ADPCM
	// sound effects, so freeEnoughSpaceForSample() could never free enough. At most one or two
	// tracks are loaded at once, so music is exempted from the size accounting entirely.
	Bool isMusic = eventToOpenFrom->getAudioEventInfo() && eventToOpenFrom->getAudioEventInfo()->m_soundType == AudioType::AT_Music;
	if (!isMusic) {
		m_currentlyUsedSize += openedAudioFile.m_fileSize;
		openedAudioFile.m_countedInCache = TRUE;
		if (m_currentlyUsedSize > m_maxSize) {
			if (!freeEnoughSpaceForSample(openedAudioFile)) {
				m_currentlyUsedSize -= openedAudioFile.m_fileSize;
				// A silently dropped cache entry means "no crash, but no sound", so always log it.
				DEBUG_LOG(("AudioFileCache::openFile - cache full (%u/%u bytes), dropping '%s' (%u bytes)\n",
					m_currentlyUsedSize, m_maxSize, strToFind.str(), openedAudioFile.m_fileSize));
				AudioLog("cache full %u/%u, dropped %s (%u bytes)", m_currentlyUsedSize, m_maxSize,
					strToFind.str(), openedAudioFile.m_fileSize);
				releaseOpenAudioFile(&openedAudioFile);
				return nullptr;
			}
		}
	}

	m_openFiles[strToFind] = openedAudioFile;
	return openedAudioFile.m_fileData;
}

void AudioFileCache::closeFile(void *fileToClose)
{
	if (!fileToClose) return;
	ScopedMutex mut(m_mutex);
	OpenFilesHash::iterator it;
	for (it = m_openFiles.begin(); it != m_openFiles.end(); ++it) {
		if (it->second.m_fileData == fileToClose) {
			--it->second.m_openCount;
			// Decoded music is tens of MB and not counted against m_maxSize: free it once unused.
			if (it->second.m_openCount == 0 && !it->second.m_countedInCache) {
				releaseOpenAudioFile(&it->second);
				m_openFiles.erase(it);
			}
			return;
		}
	}
}

void AudioFileCache::setMaxSize(UnsignedInt size)
{
	ScopedMutex mut(m_mutex);
	m_maxSize = size;
}

const WaveFormatInfo *AudioFileCache::findSoundInfo(const void *fileData) const
{
	OpenFilesHash::const_iterator it;
	for (it = m_openFiles.begin(); it != m_openFiles.end(); ++it) {
		if (it->second.m_fileData == fileData) {
			return &it->second.m_soundInfo;
		}
	}
	return nullptr;
}

void AudioFileCache::releaseOpenAudioFile(OpenAudioFile *fileToRelease)
{
	if (fileToRelease->m_openCount > 0) {
		TheAudio->closeAnySamplesUsingFile(fileToRelease->m_fileData);
	}
	if (fileToRelease->m_fileData) {
		delete[] static_cast<char*>(fileToRelease->m_fileData);
		fileToRelease->m_fileData = nullptr;
		fileToRelease->m_eventInfo = nullptr;
	}
}

Bool AudioFileCache::freeEnoughSpaceForSample(const OpenAudioFile& sampleThatNeedsSpace)
{
	// Kill the least-recently-opened, currently-unused files until we're back under budget.
	while (m_currentlyUsedSize > m_maxSize) {
		OpenFilesHashIt victim = m_openFiles.end();
		for (OpenFilesHashIt it = m_openFiles.begin(); it != m_openFiles.end(); ++it) {
			if (it->second.m_openCount == 0 && it->second.m_fileData) {
				victim = it;
				break;
			}
		}
		if (victim == m_openFiles.end()) {
			return false; // nothing left we can free
		}
		// Music entries are not counted in m_currentlyUsedSize. Only subtract what was added and
		// never go below 0; an unsigned underflow would make the cache drop every further sound.
		if (victim->second.m_countedInCache) {
			if (m_currentlyUsedSize >= victim->second.m_fileSize)
				m_currentlyUsedSize -= victim->second.m_fileSize;
			else
				m_currentlyUsedSize = 0;
		}
		releaseOpenAudioFile(&victim->second);
		m_openFiles.erase(victim);
	}
	return true;
}

//===================================================================================================
// XAudio2AudioManager -- construction / device lifecycle
//===================================================================================================
XAudio2AudioManager::XAudio2AudioManager() :
	m_xaudio2(nullptr),
	m_masteringVoice(nullptr),
	m_musicSubmix(nullptr),
	m_soundSubmix(nullptr),
	m_sound3DSubmix(nullptr),
	m_speechSubmix(nullptr),
	m_masterChannels(2),
	m_comInitialized(false),
	m_selectedSpeakerType(0),
	m_pref3DProvider(AsciiString::TheEmptyString),
	m_prefSpeaker(AsciiString::TheEmptyString),
	m_binkHandle(nullptr),
	m_num2DSamples(0),
	m_num3DSamples(0),
	m_numStreams(0)
{
	memset(m_x3dInstance, 0, sizeof(m_x3dInstance));
	m_audioCache = std::make_unique<AudioFileCache>();
}

XAudio2AudioManager::~XAudio2AudioManager()
{
	releaseHandleForBink();
	closeDevice();
	m_audioCache.reset();

	DEBUG_ASSERTCRASH(this == TheAudio, ("Umm...\n"));
	::Engine::context().audio = nullptr;
}

void XAudio2AudioManager::openDevice(void)
{
	if (!TheGlobalData->m_audioOn) {
		return;
	}

	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	m_comInitialized = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

	if (FAILED(XAudio2Create(&m_xaudio2, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
		setOn(false, AudioAffect_All);
		return;
	}

	const AudioSettings *audioSettings = getAudioSettings();
	UnsignedInt sampleRate = audioSettings->m_outputRate > 0 ? audioSettings->m_outputRate : 44100;

	if (FAILED(m_xaudio2->CreateMasteringVoice(&m_masteringVoice, XAUDIO2_DEFAULT_CHANNELS, sampleRate))) {
		setOn(false, AudioAffect_All);
		return;
	}

	XAUDIO2_VOICE_DETAILS details;
	m_masteringVoice->GetVoiceDetails(&details);
	m_masterChannels = details.InputChannels;
	// Guard against an unrealistic channel count: the panning matrix in apply3DAudioSettings()
	// is sized for XAUDIO2_MAX_AUDIO_CHANNELS destination/source channels.
	if (m_masterChannels > XAUDIO2_MAX_AUDIO_CHANNELS) {
		m_masterChannels = XAUDIO2_MAX_AUDIO_CHANNELS;
	}

	// Submixes use m_masterChannels, not mono; with one input channel every stereo source
	// (music, ambient sounds) would be downmixed to mono before reaching the mastering voice.
	m_xaudio2->CreateSubmixVoice(&m_musicSubmix, m_masterChannels, sampleRate, 0, 0, nullptr, nullptr);
	m_xaudio2->CreateSubmixVoice(&m_soundSubmix, m_masterChannels, sampleRate, 0, 0, nullptr, nullptr);
	m_xaudio2->CreateSubmixVoice(&m_sound3DSubmix, m_masterChannels, sampleRate, 0, 0, nullptr, nullptr);
	m_xaudio2->CreateSubmixVoice(&m_speechSubmix, m_masterChannels, sampleRate, 0, 0, nullptr, nullptr);

	// The mastering voice may be 5.1/7.1 (system setting), so query its real channel mask for
	// X3DAudioCalculate() and fall back to SPEAKER_STEREO.
	DWORD channelMask = 0;
	if (FAILED(m_masteringVoice->GetChannelMask(&channelMask)) || channelMask == 0) {
		channelMask = SPEAKER_STEREO;
	}
	X3DAudioInitialize(channelMask, X3DAUDIO_SPEED_OF_SOUND, m_x3dInstance);

	m_audioCache->setMaxSize(audioSettings->m_maxCacheSize);
	m_selectedSpeakerType = TheAudio->translateSpeakerTypeToUnsignedInt(m_prefSpeaker);
	TheAudio->refreshCachedVariables();
}

void XAudio2AudioManager::closeDevice(void)
{
	stopAllAudioImmediately();

	if (m_musicSubmix) { m_musicSubmix->DestroyVoice(); m_musicSubmix = nullptr; }
	if (m_soundSubmix) { m_soundSubmix->DestroyVoice(); m_soundSubmix = nullptr; }
	if (m_sound3DSubmix) { m_sound3DSubmix->DestroyVoice(); m_sound3DSubmix = nullptr; }
	if (m_speechSubmix) { m_speechSubmix->DestroyVoice(); m_speechSubmix = nullptr; }
	if (m_masteringVoice) { m_masteringVoice->DestroyVoice(); m_masteringVoice = nullptr; }
	if (m_xaudio2) { m_xaudio2->Release(); m_xaudio2 = nullptr; }

	if (m_comInitialized) {
		CoUninitialize();
		m_comInitialized = false;
	}

	m_num2DSamples = 0;
	m_num3DSamples = 0;
	m_numStreams = 0;
}

void XAudio2AudioManager::init()
{
	AudioManager::init();
	openDevice();
}

void XAudio2AudioManager::postProcessLoad()
{
	AudioManager::postProcessLoad();
}

void XAudio2AudioManager::reset()
{
#if defined(_DEBUG)
	dumpAllAssetsUsed();
	m_allEventsLoaded.clear();
#endif
	AudioManager::reset();
	stopAllAudioImmediately();
	// drop pending requests, otherwise they leak at shutdown
	removeAllAudioRequests();
	// Ambient sounds customized per object by the map (Object::updateObjValuesFromMapProperties, name
	// " CUSTOM <Drawable-ID> ...") belong to that map only; otherwise they would accumulate across loads.
	// At this point sounds are stopped and all drawables are gone (GameClient resets before audio).
	removeLevelSpecificAudioEventInfos();
}

void XAudio2AudioManager::update()
{
	AudioManager::update();
	setDeviceListenerPosition();
	processRequestList();
	processPlayingList();
	processFadingList();
	processStoppedList();
}

//===================================================================================================
// Submix routing / volume categories
//===================================================================================================
IXAudio2SubmixVoice *XAudio2AudioManager::getSubmixFor(AudioType soundType, Bool positional) const
{
	if (soundType == AudioType::AT_Music) return m_musicSubmix;
	if (soundType == AudioType::AT_Streaming) return m_speechSubmix;
	return positional ? m_sound3DSubmix : m_soundSubmix;
}

//===================================================================================================
// Provider / speaker-type: modern APIs have no such concept -- sane single-provider stubs.
//===================================================================================================
UnsignedInt XAudio2AudioManager::getProviderCount(void) const { return 1; }
AsciiString XAudio2AudioManager::getProviderName(UnsignedInt providerNum) const
{
	return providerNum == 0 ? AsciiString("XAudio2") : AsciiString::TheEmptyString;
}
UnsignedInt XAudio2AudioManager::getProviderIndex(AsciiString providerName) const { return 0; }
void XAudio2AudioManager::selectProvider(UnsignedInt providerNdx) { }
void XAudio2AudioManager::unselectProvider(void) { }
UnsignedInt XAudio2AudioManager::getSelectedProvider(void) const { return 0; }
void XAudio2AudioManager::setSpeakerType(UnsignedInt speakerType) { m_selectedSpeakerType = speakerType; }
UnsignedInt XAudio2AudioManager::getSpeakerType(void) { return m_selectedSpeakerType; }
void XAudio2AudioManager::setHardwareAccelerated(Bool accel) { AudioManager::setHardwareAccelerated(accel); }
void XAudio2AudioManager::setSpeakerSurround(Bool surround) { AudioManager::setSpeakerSurround(surround); }

void *XAudio2AudioManager::getHandleForBink(void) { return nullptr; }
void XAudio2AudioManager::releaseHandleForBink(void) { }

//===================================================================================================
// Music track name helpers
//===================================================================================================
void XAudio2AudioManager::nextMusicTrack(void)
{
	AsciiString trackName;
	std::list<PlayingAudio *>::iterator it;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		if (*it && (*it)->m_audioEventRTS->getAudioEventInfo()->m_soundType == AudioType::AT_Music) {
			trackName = (*it)->m_audioEventRTS->getEventName();
		}
	}
	TheAudio->removeAudioEvent(AHSV_StopTheMusic);
	trackName = nextTrackName(trackName);
	AudioEventRTS newTrack(trackName);
	TheAudio->addAudioEvent(&newTrack);
}

void XAudio2AudioManager::prevMusicTrack(void)
{
	AsciiString trackName;
	std::list<PlayingAudio *>::iterator it;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		if (*it && (*it)->m_audioEventRTS->getAudioEventInfo()->m_soundType == AudioType::AT_Music) {
			trackName = (*it)->m_audioEventRTS->getEventName();
		}
	}
	TheAudio->removeAudioEvent(AHSV_StopTheMusic);
	trackName = prevTrackName(trackName);
	AudioEventRTS newTrack(trackName);
	TheAudio->addAudioEvent(&newTrack);
}

Bool XAudio2AudioManager::isMusicPlaying(void) const
{
	std::list<PlayingAudio *>::const_iterator it;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		if (*it && (*it)->m_audioEventRTS->getAudioEventInfo()->m_soundType == AudioType::AT_Music) return TRUE;
	}
	return FALSE;
}

Bool XAudio2AudioManager::hasMusicTrackCompleted(const AsciiString& trackName, Int numberOfTimes) const
{
	// Only the playing track can have completed. A track that is not playing yet (its request is
	// still queued) or no longer plays answers "no"; the skirmish scripts chain the next track on
	// "yes", so a "yes" for a queued track would skip it at once.
	std::list<PlayingAudio *>::const_iterator it;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		if (*it && (*it)->m_audioEventRTS->getAudioEventInfo()->m_soundType == AudioType::AT_Music &&
			(*it)->m_audioEventRTS->getEventName() == trackName) {
			const LONG loops = (*it)->m_loopsCompleted;
			// The scripts ask every frame; log each finished pass once.
			if (loops != (*it)->m_loopsLogged) {
				(*it)->m_loopsLogged = loops;
				AudioLog("music %s played %d times", trackName.str(), static_cast<Int>(loops));
			}
			return loops >= numberOfTimes;
		}
	}
	return FALSE;
}

// Zero Hour feature; part of the workaround for cinematic hangs.
Bool XAudio2AudioManager::has3DSensitiveStreamsPlaying(void) const
{
	if (m_playingStreams.empty())
		return FALSE;

	std::list<PlayingAudio *>::const_iterator it;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		const PlayingAudio *playing = (*it);
		if (!playing)
			continue;
		if (playing->m_audioEventRTS->getAudioEventInfo()->m_soundType != AudioType::AT_Music)
			return TRUE;
		if (playing->m_audioEventRTS->getEventName().startsWith("Game_") == FALSE)
			return TRUE;
	}
	return FALSE;
}

AsciiString XAudio2AudioManager::getMusicTrackName(void) const
{
	std::list<PoolPtr<AudioRequest>>::const_iterator ait;
	for (ait = m_audioRequests.begin(); ait != m_audioRequests.end(); ++ait) {
		if ((*ait)->m_request != RequestType::AR_Play || !(*ait)->m_usePendingEvent) continue;
		if ((*ait)->m_pendingEvent->getAudioEventInfo()->m_soundType == AudioType::AT_Music) {
			return (*ait)->m_pendingEvent->getEventName();
		}
	}

	std::list<PlayingAudio *>::const_iterator it;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		if (*it && (*it)->m_audioEventRTS->getAudioEventInfo()->m_soundType == AudioType::AT_Music) {
			return (*it)->m_audioEventRTS->getEventName();
		}
	}
	return AsciiString::TheEmptyString;
}

//===================================================================================================
// stop / pause / resume
//===================================================================================================
void XAudio2AudioManager::stopAudio(AudioAffect which)
{
	std::list<PlayingAudio *>::iterator it;
	if (BitTest(which, AudioAffect_Sound)) {
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			if (*it && (*it)->m_voice) { (*it)->m_voice->Stop(); (*it)->m_status = PlayingStatus::PS_Stopped; }
		}
	}
	if (BitTest(which, AudioAffect_Sound3D)) {
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			if (*it && (*it)->m_voice) { (*it)->m_voice->Stop(); (*it)->m_status = PlayingStatus::PS_Stopped; }
		}
	}
	if (BitTest(which, AudioAffect_Speech | AudioAffect_Music)) {
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			PlayingAudio *playing = *it;
			if (!playing) continue;
			Bool isMusic = playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AudioType::AT_Music;
			if (isMusic && !BitTest(which, AudioAffect_Music)) continue;
			if (!isMusic && !BitTest(which, AudioAffect_Speech)) continue;
			if (playing->m_voice) playing->m_voice->Stop();
			playing->m_status = PlayingStatus::PS_Stopped;
		}
	}
}

void XAudio2AudioManager::pauseAudio(AudioAffect which)
{
	std::list<PlayingAudio *>::iterator it;
	if (BitTest(which, AudioAffect_Sound)) {
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			if (*it && (*it)->m_voice) (*it)->m_voice->Stop();
		}
	}
	if (BitTest(which, AudioAffect_Sound3D)) {
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			if (*it && (*it)->m_voice) (*it)->m_voice->Stop();
		}
	}
	if (BitTest(which, AudioAffect_Speech | AudioAffect_Music)) {
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			PlayingAudio *playing = *it;
			if (!playing) continue;
			Bool isMusic = playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AudioType::AT_Music;
			if (isMusic && !BitTest(which, AudioAffect_Music)) continue;
			if (!isMusic && !BitTest(which, AudioAffect_Speech)) continue;
			if (playing->m_voice) playing->m_voice->Stop();
		}
	}

	std::list<PoolPtr<AudioRequest>>::iterator ait;
	for (ait = m_audioRequests.begin(); ait != m_audioRequests.end(); /* empty */) {
		AudioRequest *req = (*ait).get();
		if (req && req->m_request == RequestType::AR_Play) {
			ait = m_audioRequests.erase(ait);
		} else {
			++ait;
		}
	}
}

void XAudio2AudioManager::resumeAudio(AudioAffect which)
{
	std::list<PlayingAudio *>::iterator it;
	if (BitTest(which, AudioAffect_Sound)) {
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			if (*it && (*it)->m_voice) (*it)->m_voice->Start();
		}
	}
	if (BitTest(which, AudioAffect_Sound3D)) {
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			if (*it && (*it)->m_voice) (*it)->m_voice->Start();
		}
	}
	if (BitTest(which, AudioAffect_Speech | AudioAffect_Music)) {
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			PlayingAudio *playing = *it;
			if (!playing) continue;
			Bool isMusic = playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AudioType::AT_Music;
			if (isMusic && !BitTest(which, AudioAffect_Music)) continue;
			if (!isMusic && !BitTest(which, AudioAffect_Speech)) continue;
			if (playing->m_voice) playing->m_voice->Start();
		}
	}
}

void XAudio2AudioManager::pauseAmbient(Bool shouldPause) { }
void XAudio2AudioManager::pauseAudioEvent(AudioHandle handle) { }

//===================================================================================================
// Playback core
//===================================================================================================
PlayingAudio *XAudio2AudioManager::allocatePlayingAudio(void)
{
	PlayingAudio *aud = NEW PlayingAudio;
	aud->m_status = PlayingStatus::PS_Playing;
	return aud;
}

void XAudio2AudioManager::destroyVoice(PlayingAudio *release)
{
	if (release->m_voice) {
		release->m_voice->Stop(0);
		release->m_voice->FlushSourceBuffers();
		release->m_voice->DestroyVoice();
		release->m_voice = nullptr;
	}
}

void XAudio2AudioManager::releasePlayingAudio(PlayingAudio *release)
{
	if (release->m_audioEventRTS->getAudioEventInfo()->m_soundType == AudioType::AT_SoundEffect) {
		if (release->m_type == PlayingAudioType::PAT_Sample) {
			if (release->m_voice) m_sound->notifyOf2DSampleCompletion();
		} else if (release->m_type == PlayingAudioType::PAT_3DSample) {
			if (release->m_voice) m_sound->notifyOf3DSampleCompletion();
		}
	}
	destroyVoice(release);
	closeFile(release->m_file);
	release->m_file = nullptr;
	if (release->m_cleanupAudioEventRTS) {
		releaseAudioEventRTS(release->m_audioEventRTS);
	}
	delete release;
}

void XAudio2AudioManager::stopAllAudioImmediately(void)
{
	std::list<PlayingAudio *>::iterator it;
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ) {
		releasePlayingAudio(*it);
		it = m_playingSounds.erase(it);
	}
	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ) {
		releasePlayingAudio(*it);
		it = m_playing3DSounds.erase(it);
	}
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ) {
		releasePlayingAudio(*it);
		it = m_playingStreams.erase(it);
	}
	for (it = m_fadingAudio.begin(); it != m_fadingAudio.end(); ) {
		releasePlayingAudio(*it);
		it = m_fadingAudio.erase(it);
	}
	for (it = m_stoppedAudio.begin(); it != m_stoppedAudio.end(); ) {
		releasePlayingAudio(*it);
		it = m_stoppedAudio.erase(it);
	}

	std::list<IXAudio2SourceVoice *>::iterator vit;
	for (vit = m_audioForcePlayed.begin(); vit != m_audioForcePlayed.end(); ++vit) {
		if (*vit) { (*vit)->Stop(0); (*vit)->DestroyVoice(); }
	}
	m_audioForcePlayed.clear();
}

void *XAudio2AudioManager::loadFileForRead(AudioEventRTS *eventToLoadFrom)
{
	return m_audioCache->openFile(eventToLoadFrom);
}

void XAudio2AudioManager::closeFile(void *fileRead)
{
	m_audioCache->closeFile(fileRead);
}

void XAudio2AudioManager::stopAllSpeech(void)
{
	std::list<PlayingAudio *>::iterator it;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ) {
		PlayingAudio *playing = *it;
		if (playing && playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AudioType::AT_Streaming) {
			releasePlayingAudio(playing);
			it = m_playingStreams.erase(it);
		} else {
			++it;
		}
	}
}

//===================================================================================================
// WAVEFORMATEX / voice creation helper
//===================================================================================================
static void FillWaveFormat(WAVEFORMATEX &wfx, const WaveFormatInfo &info)
{
	memset(&wfx, 0, sizeof(wfx));
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = static_cast<WORD>(info.channels);
	wfx.nSamplesPerSec = info.samplesPerSec;
	wfx.wBitsPerSample = static_cast<WORD>(info.bitsPerSample);
	wfx.nBlockAlign = static_cast<WORD>(info.channels * (info.bitsPerSample / 8));
	wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
	wfx.cbSize = 0;
}

void *XAudio2AudioManager::playSample(AudioEventRTS *event, PlayingAudio *audio)
{
	void *fileBuffer = loadFileForRead(event);
	if (!fileBuffer) return nullptr;

	const WaveFormatInfo *info = m_audioCache->findSoundInfo(fileBuffer);
	if (!info) { closeFile(fileBuffer); return nullptr; }

	WAVEFORMATEX wfx;
	FillWaveFormat(wfx, *info);

	XAUDIO2_SEND_DESCRIPTOR sendDesc = { 0, getSubmixFor(AudioType::AT_SoundEffect, false) };
	XAUDIO2_VOICE_SENDS sends = { 1, &sendDesc };

	destroyVoice(audio);
	if (FAILED(m_xaudio2->CreateSourceVoice(&audio->m_voice, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &TheXAudio2SoundCallback, &sends, nullptr))) {
		closeFile(fileBuffer);
		return nullptr;
	}

	Real desiredVolume = event->getVolume() * event->getVolumeShift() * m_soundVolume;
	audio->m_voice->SetVolume(desiredVolume);

	Real pitchShift = event->getPitchShift();
	if (pitchShift > 0.0f) {
		audio->m_voice->SetFrequencyRatio(pitchShift);
	}

	XAUDIO2_BUFFER buffer;
	memset(&buffer, 0, sizeof(buffer));
	buffer.AudioBytes = info->dataLen;
	buffer.pAudioData = static_cast<const BYTE*>(info->data);
	buffer.Flags = XAUDIO2_END_OF_STREAM;
	buffer.pContext = audio;

	audio->m_voice->SubmitSourceBuffer(&buffer);
	audio->m_voice->Start(0);

	return fileBuffer;
}

void XAudio2AudioManager::apply3DAudioSettings(PlayingAudio *audio, const Coord3D *pos)
{
	if (!audio->m_voice || !pos) return;

	// Panning only -- distance-based volume falloff is already computed by getEffectiveVolume()
	// from the INI-configured Min/MaxDistance, so we give X3DAudio a flat volume curve to avoid
	// double-attenuating and use it purely for the spatial (left/right/surround) matrix.
	static X3DAUDIO_DISTANCE_CURVE_POINT flatPoints[2] = { {0.0f, 1.0f}, {1.0f, 1.0f} };
	static X3DAUDIO_DISTANCE_CURVE flatCurve = { flatPoints, 2 };

	// X3DAudio requires OrientFront and OrientTop to be orthonormal. A tilted camera gives
	// m_listenerOrientation a Z component, so project the front onto the XY plane and normalize
	// (OrientTop stays (0,0,1)); looking straight up/down falls back to a fixed forward direction.
	X3DAUDIO_LISTENER listener;
	memset(&listener, 0, sizeof(listener));
	FLOAT32 frontLenSq = m_listenerOrientation.x * m_listenerOrientation.x
		+ m_listenerOrientation.y * m_listenerOrientation.y;
	if (frontLenSq > 0.0001f) {
		FLOAT32 invLen = 1.0f / sqrtf(frontLenSq);
		listener.OrientFront.x = m_listenerOrientation.x * invLen;
		listener.OrientFront.y = m_listenerOrientation.y * invLen;
	} else {
		listener.OrientFront.x = 0.0f;
		listener.OrientFront.y = 1.0f;
	}
	listener.OrientFront.z = 0.0f;
	listener.OrientTop.z = 1.0f;
	listener.Position.x = m_listenerPosition.x;
	listener.Position.y = m_listenerPosition.y;
	listener.Position.z = m_listenerPosition.z;

	// Use the source's real channel count: stereo 3D assets exist (see playSample3D), and a matrix
	// computed for one channel would be rejected by SetOutputMatrix with E_INVALIDARG.
	XAUDIO2_VOICE_DETAILS srcDetails;
	audio->m_voice->GetVoiceDetails(&srcDetails);
	UINT32 srcChannels = srcDetails.InputChannels > 0 ? srcDetails.InputChannels : 1;

	X3DAUDIO_EMITTER emitter;
	memset(&emitter, 0, sizeof(emitter));
	emitter.OrientFront.y = 1.0f;
	emitter.OrientTop.z = 1.0f;
	emitter.Position.x = pos->x;
	emitter.Position.y = pos->y;
	emitter.Position.z = pos->z;
	emitter.ChannelCount = srcChannels;
	emitter.CurveDistanceScaler = 1.0f;
	emitter.DopplerScaler = 0.0f;
	emitter.pVolumeCurve = &flatCurve;

	// Sized for XAUDIO2_MAX_AUDIO_CHANNELS per source/destination; covers multichannel sources and
	// mastering voices with many channels (m_masterChannels is clamped in openDevice()).
	FLOAT32 matrix[XAUDIO2_MAX_AUDIO_CHANNELS * XAUDIO2_MAX_AUDIO_CHANNELS];
	X3DAUDIO_DSP_SETTINGS dsp;
	memset(&dsp, 0, sizeof(dsp));
	dsp.SrcChannelCount = srcChannels;
	dsp.DstChannelCount = m_masterChannels;
	dsp.pMatrixCoefficients = matrix;

	X3DAudioCalculate(m_x3dInstance, &listener, &emitter, X3DAUDIO_CALCULATE_MATRIX, &dsp);

	// The destination channel count must match the submix's real channel count, otherwise only
	// the first matrix coefficient applies and 3D panning has no effect.
	audio->m_voice->SetOutputMatrix(m_sound3DSubmix, srcChannels, m_masterChannels, matrix);
}

void *XAudio2AudioManager::playSample3D(AudioEventRTS *event, PlayingAudio *audio)
{
	const Coord3D *pos = getCurrentPositionFromEvent(event);
	if (!pos) {
		AudioLog("no position %s", event->getEventName().str());
		return nullptr;
	}

	void *fileBuffer = loadFileForRead(event);
	if (!fileBuffer) return nullptr;

	const WaveFormatInfo *info = m_audioCache->findSoundInfo(fileBuffer);
	if (!info) { closeFile(fileBuffer); return nullptr; }

	// apply3DAudioSettings() reads the real source channel count from the voice (GetVoiceDetails),
	// so a stereo 3D asset is not an error. Positional sounds are mostly mono, so just log it,
	// and only when the count actually differs to avoid log spam.
	if (info->channels != 1) {
		DEBUG_LOG(("playSample3D: 3D sound has %d channels (usually mono)\n", info->channels));
	}

	WAVEFORMATEX wfx;
	FillWaveFormat(wfx, *info);

	XAUDIO2_SEND_DESCRIPTOR sendDesc = { 0, m_sound3DSubmix };
	XAUDIO2_VOICE_SENDS sends = { 1, &sendDesc };

	destroyVoice(audio);
	if (FAILED(m_xaudio2->CreateSourceVoice(&audio->m_voice, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &TheXAudio2SoundCallback, &sends, nullptr))) {
		closeFile(fileBuffer);
		return nullptr;
	}

	Real desiredVolume = event->getVolume() * event->getVolumeShift() * m_sound3DVolume;
	audio->m_voice->SetVolume(desiredVolume);

	Real pitchShift = event->getPitchShift();
	if (pitchShift > 0.0f) {
		audio->m_voice->SetFrequencyRatio(pitchShift);
	}

	apply3DAudioSettings(audio, pos);

	XAUDIO2_BUFFER buffer;
	memset(&buffer, 0, sizeof(buffer));
	buffer.AudioBytes = info->dataLen;
	buffer.pAudioData = static_cast<const BYTE*>(info->data);
	buffer.Flags = XAUDIO2_END_OF_STREAM;
	buffer.pContext = audio;

	audio->m_voice->SubmitSourceBuffer(&buffer);
	audio->m_voice->Start(0);

	return fileBuffer;
}

void XAudio2AudioManager::playStream(AudioEventRTS *event, PlayingAudio *audio)
{
	void *fileBuffer = loadFileForRead(event);
	if (!fileBuffer) return;

	const WaveFormatInfo *info = m_audioCache->findSoundInfo(fileBuffer);
	if (!info) { closeFile(fileBuffer); return; }

	WAVEFORMATEX wfx;
	FillWaveFormat(wfx, *info);

	Bool isMusic = event->getAudioEventInfo()->m_soundType == AudioType::AT_Music;
	XAUDIO2_SEND_DESCRIPTOR sendDesc = { 0, getSubmixFor(event->getAudioEventInfo()->m_soundType, false) };
	XAUDIO2_VOICE_SENDS sends = { 1, &sendDesc };

	destroyVoice(audio);
	if (FAILED(m_xaudio2->CreateSourceVoice(&audio->m_voice, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &TheXAudio2SoundCallback, &sends, nullptr))) {
		closeFile(fileBuffer);
		return;
	}

	Real curVolume = (isMusic ? m_musicVolume : m_speechVolume) * event->getVolume();
	audio->m_voice->SetVolume(curVolume);

	XAUDIO2_BUFFER buffer;
	memset(&buffer, 0, sizeof(buffer));
	buffer.AudioBytes = info->dataLen;
	buffer.pAudioData = static_cast<const BYTE*>(info->data);
	buffer.Flags = XAUDIO2_END_OF_STREAM;
	buffer.pContext = audio;
	if (isMusic) {
		buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
	}

	audio->m_file = fileBuffer;
	audio->m_voice->SubmitSourceBuffer(&buffer);
	audio->m_voice->Start(0);
}

//===================================================================================================
// playAudioEvent / stopAudioEvent -- request dispatch
//===================================================================================================
void XAudio2AudioManager::playAudioEvent(AudioEventRTS *event)
{
	const AudioEventInfo *info = event->getAudioEventInfo();
	if (!info) return;

	std::list<PlayingAudio *>::iterator it;
	AudioHandle handleToKill = event->getHandleToKill();
	PlayingAudio *audio = allocatePlayingAudio();

	switch (info->m_soundType)
	{
		case AudioType::AT_Music:
		case AudioType::AT_Streaming:
		{
			if (info->m_soundType == AudioType::AT_Streaming && event->getUninterruptable()) {
				stopAllSpeech();
			}

			Bool foundSoundToReplace = false;
			if (handleToKill) {
				for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
					if (*it && (*it)->m_audioEventRTS->getPlayingHandle() == handleToKill) {
						releasePlayingAudio(*it);
						m_playingStreams.erase(it);
						foundSoundToReplace = true;
						break;
					}
				}
			}

			audio->m_audioEventRTS = event;
			audio->m_type = PlayingAudioType::PAT_Stream;

			if (!handleToKill || foundSoundToReplace) {
				if (info->m_soundType == AudioType::AT_Streaming && event->getUninterruptable()) {
					setDisallowSpeech(TRUE);
				}
				playStream(event, audio);
			}

			if (audio->m_voice) {
				AudioLog("plays %s stream file=%s", event->getEventName().str(), event->getFilename().str());
				m_playingStreams.push_back(audio);
				audio = nullptr;
			} else {
				AudioLog("no voice %s stream%s", event->getEventName().str(),
					(handleToKill && !foundSoundToReplace) ? " (sound to replace missing)" : "");
			}
			break;
		}

		case AudioType::AT_SoundEffect:
		{
			if (event->isPositionalAudio())
			{
				Bool foundSoundToReplace = false;
				if (handleToKill) {
					for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
						if (*it && (*it)->m_audioEventRTS->getPlayingHandle() == handleToKill) {
							releasePlayingAudio(*it);
							m_playing3DSounds.erase(it);
							foundSoundToReplace = true;
							break;
						}
					}
				}

				audio->m_audioEventRTS = event;
				audio->m_type = PlayingAudioType::PAT_3DSample;

				Bool canPlay = (!handleToKill || foundSoundToReplace);
				if (canPlay && doesViolateLimit(event) && killLowestPrioritySoundImmediately(event)) {
					// freed a slot, fall through and try again below
				}
				if (canPlay) {
					m_playing3DSounds.push_back(audio);
					audio->m_file = playSample3D(event, audio);
					if (audio->m_file) {
						AudioLog("plays %s 3D file=%s", event->getEventName().str(),
						event->getNextPlayPortion() == PortionToPlay::PP_Attack ? event->getAttackFilename().str() : event->getFilename().str());
						m_sound->notifyOf3DSampleStart();
						audio = nullptr;
					} else {
						AudioLog("no voice %s 3D", event->getEventName().str());
						m_playing3DSounds.pop_back();
					}
				} else {
					AudioLog("no voice %s 3D (sound to replace missing)", event->getEventName().str());
				}
			}
			else
			{
				Bool foundSoundToReplace = false;
				if (handleToKill) {
					for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
						if (*it && (*it)->m_audioEventRTS->getPlayingHandle() == handleToKill) {
							releasePlayingAudio(*it);
							m_playingSounds.erase(it);
							foundSoundToReplace = true;
							break;
						}
					}
				}

				audio->m_audioEventRTS = event;
				audio->m_type = PlayingAudioType::PAT_Sample;

				Bool canPlay = (!handleToKill || foundSoundToReplace);
				if (canPlay) {
					m_playingSounds.push_back(audio);
					audio->m_file = playSample(event, audio);
					if (audio->m_file) {
						AudioLog("plays %s 2D file=%s", event->getEventName().str(),
					event->getNextPlayPortion() == PortionToPlay::PP_Attack ? event->getAttackFilename().str() : event->getFilename().str());
						m_sound->notifyOf2DSampleStart();
						audio = nullptr;
					} else {
						AudioLog("no voice %s 2D", event->getEventName().str());
						m_playingSounds.pop_back();
					}
				} else {
					AudioLog("no voice %s 2D (sound to replace missing)", event->getEventName().str());
				}
			}
			break;
		}
	}

	if (audio) {
		releasePlayingAudio(audio);
	}
}

void XAudio2AudioManager::stopAudioEvent(AudioHandle handle)
{
	std::list<PlayingAudio *>::iterator it;
	if (handle == AHSV_StopTheMusic || handle == AHSV_StopTheMusicFade) {
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			PlayingAudio *audio = *it;
			if (!audio) continue;
			if (audio->m_audioEventRTS->getAudioEventInfo()->m_soundType == AudioType::AT_Music) {
				AudioLog("music stopped %s%s", audio->m_audioEventRTS->getEventName().str(),
					handle == AHSV_StopTheMusicFade ? " (fade out)" : "");
				if (handle == AHSV_StopTheMusicFade) {
					m_fadingAudio.push_back(audio);
				} else {
					releasePlayingAudio(audio);
				}
				m_playingStreams.erase(it);
				break;
			}
		}
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		if (*it && (*it)->m_audioEventRTS->getPlayingHandle() == handle) {
			(*it)->m_requestStop = true;
			(*it)->m_status = PlayingStatus::PS_Stopped;
			break;
		}
	}
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		if (*it && (*it)->m_audioEventRTS->getPlayingHandle() == handle) {
			(*it)->m_requestStop = true;
			(*it)->m_status = PlayingStatus::PS_Stopped;
			break;
		}
	}
	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		if (*it && (*it)->m_audioEventRTS->getPlayingHandle() == handle) {
			(*it)->m_requestStop = true;
			(*it)->m_status = PlayingStatus::PS_Stopped;
			break;
		}
	}
}

void XAudio2AudioManager::killAudioEventImmediately(AudioHandle audioEvent)
{
	std::list<PoolPtr<AudioRequest>>::iterator ait;
	for (ait = m_audioRequests.begin(); ait != m_audioRequests.end(); ++ait) {
		AudioRequest *req = (*ait).get();
		if (req && req->m_request == RequestType::AR_Play && req->m_handleToInteractOn == audioEvent) {
			m_audioRequests.erase(ait);
			return;
		}
	}

	std::list<PlayingAudio *>::iterator it;
	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		if (*it && (*it)->m_audioEventRTS->getPlayingHandle() == audioEvent) {
			releasePlayingAudio(*it);
			m_playing3DSounds.erase(it);
			return;
		}
	}
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		if (*it && (*it)->m_audioEventRTS->getPlayingHandle() == audioEvent) {
			releasePlayingAudio(*it);
			m_playingSounds.erase(it);
			return;
		}
	}
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		if (*it && (*it)->m_audioEventRTS->getPlayingHandle() == audioEvent) {
			releasePlayingAudio(*it);
			m_playingStreams.erase(it);
			return;
		}
	}
}

//===================================================================================================
// Completion handling (attack -> sound -> decay chaining, looping, buffer-end cleanup)
//===================================================================================================
void XAudio2AudioManager::onVoiceBufferEnd(PlayingAudio *playing)
{
	// Called from the XAudio2 engine thread. Only flag it; advanceAudioPortion() (which touches
	// game-side lists/files) runs later from processPlayingList() on the main thread.
	playing->m_status = PlayingStatus::PS_Stopped;
}

Bool XAudio2AudioManager::startNextLoop(PlayingAudio *looping)
{
	closeFile(looping->m_file);
	looping->m_file = nullptr;

	if (looping->m_requestStop) return false;

	if (looping->m_audioEventRTS->hasMoreLoops()) {
		looping->m_audioEventRTS->generateFilename();

		if (looping->m_audioEventRTS->getDelay() > MSEC_PER_LOGICFRAME_REAL) {
			looping->m_cleanupAudioEventRTS = false;
			looping->m_requestStop = true;
			looping->m_status = PlayingStatus::PS_Stopped;

			AudioRequest *req = allocateAudioRequest(true);
			req->m_pendingEvent = looping->m_audioEventRTS;
			req->m_requiresCheckForSample = true;
			appendAudioRequest(req);
			return true;
		}

		if (looping->m_type == PlayingAudioType::PAT_3DSample) {
			looping->m_file = playSample3D(looping->m_audioEventRTS, looping);
		} else {
			looping->m_file = playSample(looping->m_audioEventRTS, looping);
		}
		if (looping->m_file) {
			looping->m_status = PlayingStatus::PS_Playing;
		}
		return looping->m_file != nullptr;
	}
	return false;
}

void XAudio2AudioManager::advanceAudioPortion(PlayingAudio *playing)
{
	if (getDisallowSpeech() && playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AudioType::AT_Streaming) {
		setDisallowSpeech(FALSE);
	}

	if (playing->m_audioEventRTS->getAudioEventInfo()->m_control & AC_LOOP) {
		if (playing->m_audioEventRTS->getNextPlayPortion() == PortionToPlay::PP_Attack) {
			playing->m_audioEventRTS->setNextPlayPortion(PortionToPlay::PP_Sound);
		}
		if (playing->m_audioEventRTS->getNextPlayPortion() == PortionToPlay::PP_Sound) {
			playing->m_audioEventRTS->decreaseLoopCount();
			if (startNextLoop(playing)) {
				return;
			}
		}
	}

	playing->m_audioEventRTS->advanceNextPlayPortion();
	if (playing->m_audioEventRTS->getNextPlayPortion() != PortionToPlay::PP_Done) {
		closeFile(playing->m_file);
		playing->m_file = (playing->m_type == PlayingAudioType::PAT_3DSample) ?
			playSample3D(playing->m_audioEventRTS, playing) :
			playSample(playing->m_audioEventRTS, playing);
		if (AudioLogEnabled()) {
			AudioEventRTS *ev = playing->m_audioEventRTS;
			AsciiString file = ev->getNextPlayPortion() == PortionToPlay::PP_Decay ? ev->getDecayFilename() : ev->getFilename();
			AudioLog("portion %s %s file=%s%s", ev->getNextPlayPortion() == PortionToPlay::PP_Decay ? "decay" : "sound",
				ev->getEventName().str(), file.str(), playing->m_file ? "" : " no voice");
		}
		if (playing->m_file) {
			playing->m_status = PlayingStatus::PS_Playing;
			return;
		}
	}

	if (playing->m_type == PlayingAudioType::PAT_Stream && playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AudioType::AT_Music) {
		// Native XAUDIO2_LOOP_INFINITE already handles repeating music; reaching here means the
		// voice was stopped/flushed deliberately, so just let it fall through to stopped cleanup.
	}

	playing->m_status = PlayingStatus::PS_Stopped;
}

//===================================================================================================
// Per-frame list processing
//===================================================================================================
void XAudio2AudioManager::processRequestList(void)
{
	std::list<PoolPtr<AudioRequest>>::iterator it;
	for (it = m_audioRequests.begin(); it != m_audioRequests.end(); /* empty */) {
		AudioRequest *req = (*it).get();
		if (req == nullptr) { ++it; continue; }

		if (!shouldProcessRequestThisFrame(req)) {
			adjustRequest(req);
			++it;
			continue;
		}

		if (!req->m_requiresCheckForSample || checkForSample(req)) {
			processRequest(req);
		} else if (req->m_usePendingEvent) {
			AudioLog("rejected after delay %s", req->m_pendingEvent->getEventName().str());
		}
		it = m_audioRequests.erase(it);
	}
}

void XAudio2AudioManager::processRequest(AudioRequest *req)
{
	switch (req->m_request)
	{
		case RequestType::AR_Play: playAudioEvent(req->m_pendingEvent); break;
		case RequestType::AR_Pause: pauseAudioEvent(req->m_handleToInteractOn); break;
		case RequestType::AR_Stop: stopAudioEvent(req->m_handleToInteractOn); break;
	}
}

Bool XAudio2AudioManager::shouldProcessRequestThisFrame(AudioRequest *req) const
{
	if (!req->m_usePendingEvent) return true;
	if (req->m_pendingEvent->getDelay() < MSEC_PER_LOGICFRAME_REAL) return true;
	return false;
}

void XAudio2AudioManager::adjustRequest(AudioRequest *req)
{
	if (!req->m_usePendingEvent) return;
	req->m_pendingEvent->decrementDelay(MSEC_PER_LOGICFRAME_REAL);
	req->m_requiresCheckForSample = true;
}

Bool XAudio2AudioManager::checkForSample(AudioRequest *req)
{
	if (!req->m_usePendingEvent) return true;
	if (req->m_pendingEvent->getAudioEventInfo()->m_type != static_cast<UnsignedInt>(AudioType::AT_SoundEffect)) return true;	// compares the SoundType bitmask against an AudioType value, kept as in the original
	return m_sound->canPlayNow(req->m_pendingEvent);
}

void XAudio2AudioManager::processPlayingList(void)
{
	std::list<PlayingAudio *>::iterator it;

	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); /* empty */) {
		PlayingAudio *playing = *it;
		if (!playing) { it = m_playingSounds.erase(it); continue; }
		if (playing->m_status == PlayingStatus::PS_Stopped) {
			advanceAudioPortion(playing);
			if (playing->m_status == PlayingStatus::PS_Stopped) {
				releasePlayingAudio(playing);
				it = m_playingSounds.erase(it);
				continue;
			}
		}
		if (m_volumeHasChanged) adjustPlayingVolume(playing);
		++it;
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ) {
		PlayingAudio *playing = *it;
		if (!playing) { it = m_playing3DSounds.erase(it); continue; }

		if (playing->m_status == PlayingStatus::PS_Stopped) {
			advanceAudioPortion(playing);
			if (playing->m_status == PlayingStatus::PS_Stopped) {
				releasePlayingAudio(playing);
				it = m_playing3DSounds.erase(it);
				continue;
			}
		}

		if (m_volumeHasChanged) adjustPlayingVolume(playing);

		const Coord3D *pos = getCurrentPositionFromEvent(playing->m_audioEventRTS);
		if (pos) {
			if (playing->m_audioEventRTS->isDead()) {
				stopAudioEvent(playing->m_audioEventRTS->getPlayingHandle());
				++it;
				continue;
			}
			Real volForConsideration = getEffectiveVolume(playing->m_audioEventRTS);
			volForConsideration /= (m_sound3DVolume > 0.0f ? m_sound3DVolume : 1.0f);
			Bool playAnyways = BitTest(playing->m_audioEventRTS->getAudioEventInfo()->m_type, ST_GLOBAL) ||
				playing->m_audioEventRTS->getAudioEventInfo()->m_priority == AP_CRITICAL;
			if (volForConsideration < m_audioSettings->m_minVolume && !playAnyways) {
				AudioLog("3D ended, too quiet %s", playing->m_audioEventRTS->getEventName().str());
				releasePlayingAudio(playing);
				it = m_playing3DSounds.erase(it);
				continue;
			}
			apply3DAudioSettings(playing, pos);
		} else {
			releasePlayingAudio(playing);
			it = m_playing3DSounds.erase(it);
			continue;
		}
		++it;
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ) {
		PlayingAudio *playing = *it;
		if (!playing) { it = m_playingStreams.erase(it); continue; }
		if (playing->m_status == PlayingStatus::PS_Stopped) {
			advanceAudioPortion(playing);
			if (playing->m_status == PlayingStatus::PS_Stopped) {
				AudioLog("stream ended %s", playing->m_audioEventRTS->getEventName().str());
				releasePlayingAudio(playing);
				it = m_playingStreams.erase(it);
				continue;
			}
		}
		if (m_volumeHasChanged) adjustPlayingVolume(playing);
		++it;
	}

	m_volumeHasChanged = false;
}

void XAudio2AudioManager::processFadingList(void)
{
	std::list<PlayingAudio *>::iterator it;
	for (it = m_fadingAudio.begin(); it != m_fadingAudio.end(); /* empty */) {
		PlayingAudio *playing = *it;
		if (!playing) { it = m_fadingAudio.erase(it); continue; }

		if (playing->m_framesFaded >= getAudioSettings()->m_fadeAudioFrames) {
			releasePlayingAudio(playing);
			it = m_fadingAudio.erase(it);
			continue;
		}

		++playing->m_framesFaded;
		Real volume = getEffectiveVolume(playing->m_audioEventRTS);
		volume *= (1.0f - 1.0f * playing->m_framesFaded / getAudioSettings()->m_fadeAudioFrames);
		if (playing->m_voice) playing->m_voice->SetVolume(volume);
		++it;
	}
}

void XAudio2AudioManager::processStoppedList(void)
{
	std::list<PlayingAudio *>::iterator it;
	for (it = m_stoppedAudio.begin(); it != m_stoppedAudio.end(); /* empty */) {
		if (*it) releasePlayingAudio(*it);
		it = m_stoppedAudio.erase(it);
	}
}

void XAudio2AudioManager::adjustPlayingVolume(PlayingAudio *audio)
{
	if (!audio->m_voice) return;
	Real desiredVolume = audio->m_audioEventRTS->getVolume() * audio->m_audioEventRTS->getVolumeShift();
	switch (audio->m_type)
	{
		case PlayingAudioType::PAT_Sample:    audio->m_voice->SetVolume(m_soundVolume * desiredVolume); break;
		case PlayingAudioType::PAT_3DSample:  audio->m_voice->SetVolume(m_sound3DVolume * desiredVolume); break;
		case PlayingAudioType::PAT_Stream:
		{
			Bool isMusic = audio->m_audioEventRTS->getAudioEventInfo()->m_soundType == AudioType::AT_Music;
			audio->m_voice->SetVolume((isMusic ? m_musicVolume : m_speechVolume) * desiredVolume);
			break;
		}
	}
}

//===================================================================================================
// 3-D helpers / effective volume (identical INI-driven formula to the original)
//===================================================================================================
void XAudio2AudioManager::setDeviceListenerPosition(void)
{
	// m_listenerPosition/Orientation are already updated by AudioManager::setListenerPosition();
	// nothing device-specific to push here since apply3DAudioSettings() reads them directly each
	// time a 3D voice is (re)positioned in processPlayingList().
}

const Coord3D *XAudio2AudioManager::getCurrentPositionFromEvent(AudioEventRTS *event)
{
	if (!event->isPositionalAudio()) return nullptr;
	return event->getCurrentPosition();
}

Real XAudio2AudioManager::getEffectiveVolume(AudioEventRTS *event) const
{
	Real volume = event->getVolume() * event->getVolumeShift();
	if (event->getAudioEventInfo()->m_soundType == AudioType::AT_Music) {
		volume *= m_musicVolume;
	} else if (event->getAudioEventInfo()->m_soundType == AudioType::AT_Streaming) {
		volume *= m_speechVolume;
	} else if (event->isPositionalAudio()) {
		volume *= m_sound3DVolume;
		Coord3D distance = m_listenerPosition;
		const Coord3D *pos = event->getCurrentPosition();
		if (pos) {
			distance.sub(pos);
			Real objMinDistance, objMaxDistance;
			if (event->getAudioEventInfo()->m_type & ST_GLOBAL) {
				objMinDistance = static_cast<Real>(TheAudio->getAudioSettings()->m_globalMinRange);
				objMaxDistance = static_cast<Real>(TheAudio->getAudioSettings()->m_globalMaxRange);
			} else {
				objMinDistance = event->getAudioEventInfo()->m_minDistance;
				objMaxDistance = event->getAudioEventInfo()->m_maxDistance;
			}
			Real objDistance = distance.length();
			if (objDistance > objMinDistance) {
				volume *= 1 / (objDistance / objMinDistance);
			}
			if (objDistance >= objMaxDistance) {
				volume = 0.0f;
			}
		}
	} else {
		volume *= m_soundVolume;
	}
	return volume;
}

//===================================================================================================
// Priority / limit rules (same logic as the original audio manager)
//===================================================================================================
UnsignedInt XAudio2AudioManager::getNum2DSamples(void) const { return m_num2DSamples ? m_num2DSamples : getAudioSettings()->m_sampleCount2D; }
UnsignedInt XAudio2AudioManager::getNum3DSamples(void) const { return m_num3DSamples ? m_num3DSamples : getAudioSettings()->m_sampleCount3D; }
UnsignedInt XAudio2AudioManager::getNumStreams(void) const { return m_numStreams ? m_numStreams : getAudioSettings()->m_streamCount; }

Bool XAudio2AudioManager::doesViolateLimit(AudioEventRTS *event) const
{
	Int limit = event->getAudioEventInfo()->m_limit;
	if (limit == 0) return false;

	Int totalCount = 0, totalRequestCount = 0;
	std::list<PlayingAudio *>::const_iterator it;
	const std::list<PlayingAudio *> &sameTypeList = event->isPositionalAudio() ? m_playing3DSounds : m_playingSounds;
	for (it = sameTypeList.begin(); it != sameTypeList.end(); ++it) {
		if ((*it)->m_audioEventRTS->getEventName() == event->getEventName()) {
			if (totalCount == 0) event->setHandleToKill((*it)->m_audioEventRTS->getPlayingHandle());
			++totalCount;
		}
	}

	std::list<PoolPtr<AudioRequest>>::const_iterator arIt;
	for (arIt = m_audioRequests.begin(); arIt != m_audioRequests.end(); ++arIt) {
		AudioRequest *req = (*arIt).get();
		if (req && req->m_usePendingEvent && req->m_pendingEvent->getEventName() == event->getEventName()) {
			++totalRequestCount;
			++totalCount;
		}
	}

	if (event->getAudioEventInfo()->m_control & AC_INTERRUPT) {
		if (totalRequestCount < limit) {
			Int totalPlayingCount = totalCount - totalRequestCount;
			if (totalRequestCount + totalPlayingCount < limit) {
				event->setHandleToKill(0);
			}
			return false;
		}
	}

	if (totalCount < limit) {
		event->setHandleToKill(0);
		return false;
	}
	return true;
}

Bool XAudio2AudioManager::isPlayingAlready(AudioEventRTS *event) const
{
	const std::list<PlayingAudio *> &list = event->isPositionalAudio() ? m_playing3DSounds : m_playingSounds;
	std::list<PlayingAudio *>::const_iterator it;
	for (it = list.begin(); it != list.end(); ++it) {
		if ((*it)->m_audioEventRTS->getEventName() == event->getEventName()) return true;
	}
	return false;
}

Bool XAudio2AudioManager::isObjectPlayingVoice(UnsignedInt objID) const
{
	if (objID == 0) return false;
	// ObjectID is signed but object IDs are never negative; the cast avoids C4389 under /W4.
	std::list<PlayingAudio *>::const_iterator it;
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		if (static_cast<UnsignedInt>((*it)->m_audioEventRTS->getObjectID()) == objID && (*it)->m_audioEventRTS->getAudioEventInfo()->m_type & ST_VOICE) return true;
	}
	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		if (static_cast<UnsignedInt>((*it)->m_audioEventRTS->getObjectID()) == objID && (*it)->m_audioEventRTS->getAudioEventInfo()->m_type & ST_VOICE) return true;
	}
	return false;
}

AudioEventRTS *XAudio2AudioManager::findLowestPrioritySound(AudioEventRTS *event)
{
	AudioPriority priority = event->getAudioEventInfo()->m_priority;
	if (priority == AP_LOWEST) return nullptr;

	AudioEventRTS *lowestPriorityEvent = nullptr;
	AudioPriority lowestPriority = AP_LOWEST;
	const std::list<PlayingAudio *> &list = event->isPositionalAudio() ? m_playing3DSounds : m_playingSounds;
	std::list<PlayingAudio *>::const_iterator it;
	for (it = list.begin(); it != list.end(); ++it) {
		AudioEventRTS *itEvent = (*it)->m_audioEventRTS;
		AudioPriority itPriority = itEvent->getAudioEventInfo()->m_priority;
		if (itPriority < priority) {
			if (!lowestPriorityEvent || lowestPriority > itPriority) {
				lowestPriorityEvent = itEvent;
				lowestPriority = itPriority;
				if (lowestPriority == AP_LOWEST) return lowestPriorityEvent;
			}
		}
	}
	return lowestPriorityEvent;
}

Bool XAudio2AudioManager::isPlayingLowerPriority(AudioEventRTS *event) const
{
	AudioPriority priority = event->getAudioEventInfo()->m_priority;
	if (priority == AP_LOWEST) return false;
	const std::list<PlayingAudio *> &list = event->isPositionalAudio() ? m_playing3DSounds : m_playingSounds;
	std::list<PlayingAudio *>::const_iterator it;
	for (it = list.begin(); it != list.end(); ++it) {
		if ((*it)->m_audioEventRTS->getAudioEventInfo()->m_priority < priority) return true;
	}
	return false;
}

Bool XAudio2AudioManager::killLowestPrioritySoundImmediately(AudioEventRTS *event)
{
	AudioEventRTS *lowestPriorityEvent = findLowestPrioritySound(event);
	if (!lowestPriorityEvent) return FALSE;

	std::list<PlayingAudio *> &list = event->isPositionalAudio() ? m_playing3DSounds : m_playingSounds;
	std::list<PlayingAudio *>::iterator it;
	for (it = list.begin(); it != list.end(); ++it) {
		if (*it && (*it)->m_audioEventRTS == lowestPriorityEvent) {
			releasePlayingAudio(*it);
			list.erase(it);
			return TRUE;
		}
	}
	return FALSE;
}

void XAudio2AudioManager::adjustVolumeOfPlayingAudio(AsciiString eventName, Real newVolume)
{
	std::list<PlayingAudio *>::iterator it;
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		if (*it && (*it)->m_audioEventRTS->getEventName() == eventName) {
			(*it)->m_audioEventRTS->setVolume(newVolume);
			adjustPlayingVolume(*it);
		}
	}
	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		if (*it && (*it)->m_audioEventRTS->getEventName() == eventName) {
			(*it)->m_audioEventRTS->setVolume(newVolume);
			adjustPlayingVolume(*it);
		}
	}
}

void XAudio2AudioManager::removePlayingAudio(AsciiString eventName)
{
	std::list<PlayingAudio *>::iterator it;
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ) {
		if (*it && (*it)->m_audioEventRTS->getEventName() == eventName) {
			releasePlayingAudio(*it);
			it = m_playingSounds.erase(it);
		} else { ++it; }
	}
	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ) {
		if (*it && (*it)->m_audioEventRTS->getEventName() == eventName) {
			releasePlayingAudio(*it);
			it = m_playing3DSounds.erase(it);
		} else { ++it; }
	}
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ) {
		if (*it && (*it)->m_audioEventRTS->getEventName() == eventName) {
			releasePlayingAudio(*it);
			it = m_playingStreams.erase(it);
		} else { ++it; }
	}
}

void XAudio2AudioManager::removeAllDisabledAudio()
{
	std::list<PlayingAudio *>::iterator it;
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ) {
		if (*it && (*it)->m_audioEventRTS->getVolume() == 0.0f) {
			releasePlayingAudio(*it);
			it = m_playingSounds.erase(it);
		} else { ++it; }
	}
	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ) {
		if (*it && (*it)->m_audioEventRTS->getVolume() == 0.0f) {
			releasePlayingAudio(*it);
			it = m_playing3DSounds.erase(it);
		} else { ++it; }
	}
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ) {
		if (*it && (*it)->m_audioEventRTS->getVolume() == 0.0f) {
			releasePlayingAudio(*it);
			it = m_playingStreams.erase(it);
		} else { ++it; }
	}
}

Bool XAudio2AudioManager::isCurrentlyPlaying(AudioHandle handle)
{
	std::list<PlayingAudio *>::iterator it;
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		if (*it && (*it)->m_audioEventRTS->getPlayingHandle() == handle) return true;
	}
	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		if (*it && (*it)->m_audioEventRTS->getPlayingHandle() == handle) return true;
	}
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		if (*it && (*it)->m_audioEventRTS->getPlayingHandle() == handle) return true;
	}
	std::list<PoolPtr<AudioRequest>>::iterator ait;
	for (ait = m_audioRequests.begin(); ait != m_audioRequests.end(); ++ait) {
		AudioRequest *req = (*ait).get();
		if (req && req->m_usePendingEvent && req->m_pendingEvent->getPlayingHandle() == handle) return true;
	}
	return false;
}

void XAudio2AudioManager::notifyOfAudioCompletion(UnsignedInt audioCompleted, UnsignedInt flags)
{
	// Handled by onVoiceBufferEnd()/advanceAudioPortion() (XAudio2 gives us the PlayingAudio*
	// directly via the buffer context, so no handle->object lookup is needed here). Exists only to
	// satisfy the AudioManager interface contract.
}

//===================================================================================================
// Misc
//===================================================================================================
Real XAudio2AudioManager::getFileLengthMS(AsciiString strToLoad) const
{
	if (strToLoad.isEmpty()) return 0.0f;

	File *file = TheFileSystem->openFile(strToLoad.str());
	if (!file) return 0.0f;

	UnsignedInt rawSize = file->size();
	char *rawBuffer = file->readEntireAndClose();

	WaveFormatInfo info;
	void *decoded = nullptr;
	Real lengthMS = 0.0f;
	if (ParseWavAndDecode(reinterpret_cast<const UnsignedByte*>(rawBuffer), rawSize, info, decoded)) {
		UnsignedInt bytesPerSample = info.channels * (info.bitsPerSample / 8);
		if (bytesPerSample > 0 && info.samplesPerSec > 0) {
			UnsignedInt totalSamples = info.dataLen / bytesPerSample;
			lengthMS = 1000.0f * static_cast<Real>(totalSamples) / static_cast<Real>(info.samplesPerSec);
		}
		delete[] static_cast<char*>(decoded);
	}
	delete[] rawBuffer;
	return lengthMS;
}

void XAudio2AudioManager::closeAnySamplesUsingFile(const void *fileToClose)
{
	std::list<PlayingAudio *>::iterator it;
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ) {
		if (*it && (*it)->m_file == fileToClose) {
			releasePlayingAudio(*it);
			it = m_playingSounds.erase(it);
		} else { ++it; }
	}
	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ) {
		if (*it && (*it)->m_file == fileToClose) {
			releasePlayingAudio(*it);
			it = m_playing3DSounds.erase(it);
		} else { ++it; }
	}
}

void XAudio2AudioManager::friend_forcePlayAudioEventRTS(const AudioEventRTS* eventToPlay)
{
	if (!eventToPlay->getAudioEventInfo()) {
		getInfoForAudioEvent(eventToPlay);
		if (!eventToPlay->getAudioEventInfo()) {
			DEBUG_CRASH(("No info for forced audio event '%s'\n", eventToPlay->getEventName().str()));
			return;
		}
	}

	switch (eventToPlay->getAudioEventInfo()->m_soundType)
	{
		case AudioType::AT_Music: if (!isOn(AudioAffect_Music)) return; break;
		case AudioType::AT_SoundEffect: if (!isOn(AudioAffect_Sound) || !isOn(AudioAffect_Sound3D)) return; break;
		case AudioType::AT_Streaming: if (!isOn(AudioAffect_Speech)) return; break;
	}

	AudioEventRTS event = *eventToPlay;
	event.generateFilename();
	event.generatePlayInfo();

	std::list<std::pair<AsciiString, Real> >::iterator it;
	for (it = m_adjustedVolumes.begin(); it != m_adjustedVolumes.end(); ++it) {
		if (it->first == event.getEventName()) { event.setVolume(it->second); break; }
	}

	AsciiString fileToPlay = event.getFilename();
	AudioEventRTS tempEvent = event;
	void *fileBuffer = m_audioCache->openFile(&tempEvent);
	if (!fileBuffer) return;

	const WaveFormatInfo *info = m_audioCache->findSoundInfo(fileBuffer);
	if (!info) { m_audioCache->closeFile(fileBuffer); return; }

	WAVEFORMATEX wfx;
	FillWaveFormat(wfx, *info);

	IXAudio2SourceVoice *voice = nullptr;
	XAUDIO2_SEND_DESCRIPTOR sendDesc = { 0, m_speechSubmix };
	XAUDIO2_VOICE_SENDS sends = { 1, &sendDesc };
	if (FAILED(m_xaudio2->CreateSourceVoice(&voice, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, nullptr, &sends, nullptr))) {
		m_audioCache->closeFile(fileBuffer);
		return;
	}

	voice->SetVolume(event.getVolume() * getVolume(AudioAffect_Speech));

	XAUDIO2_BUFFER buffer;
	memset(&buffer, 0, sizeof(buffer));
	buffer.AudioBytes = info->dataLen;
	buffer.pAudioData = static_cast<const BYTE*>(info->data);
	buffer.Flags = XAUDIO2_END_OF_STREAM;

	voice->SubmitSourceBuffer(&buffer);
	voice->Start(0);
	m_audioForcePlayed.push_back(voice);
	// Intentionally not closing fileBuffer here -- matches the original's "fire and forget"
	// lifetime; it stays cached (refcounted) until the cache evicts it under memory pressure.
}

#if defined(_DEBUG)
void XAudio2AudioManager::audioDebugDisplay(DebugDisplayInterface *dd, void *userData, FILE *fp)
{
	if (!dd) return;
	dd->printf("XAudio2 Audio Backend\n");
	dd->printf("Memory Usage: %d/%d\n", m_audioCache->getCurrentlyUsedSize(), m_audioCache->getMaxSize());
	dd->printf("Sound: %s  3DSound: %s  Speech: %s  Music: %s\n",
		(isOn(AudioAffect_Sound) ? "Yes" : "No"), (isOn(AudioAffect_Sound3D) ? "Yes" : "No"),
		(isOn(AudioAffect_Speech) ? "Yes" : "No"), (isOn(AudioAffect_Music) ? "Yes" : "No"));
	dd->printf("Volume: Sound %d  3DSound %d  Speech %d  Music %d\n",
		REAL_TO_INT(m_soundVolume * 100.0f), REAL_TO_INT(m_sound3DVolume * 100.0f),
		REAL_TO_INT(m_speechVolume * 100.0f), REAL_TO_INT(m_musicVolume * 100.0f));
	dd->printf("Playing: %d 2D, %d 3D, %d Streams\n",
		static_cast<Int>(m_playingSounds.size()), static_cast<Int>(m_playing3DSounds.size()), static_cast<Int>(m_playingStreams.size()));
}

void XAudio2AudioManager::dumpAllAssetsUsed()
{
	if (!TheGlobalData->m_preloadReport) return;
	SetAsciiString::iterator it;
	FILE *fp = fopen("AllAssetsUsed.txt", "a");
	if (!fp) return;
	for (it = m_allEventsLoaded.begin(); it != m_allEventsLoaded.end(); ++it) {
		fprintf(fp, "%s\n", it->str());
	}
	fclose(fp);
}
#endif
