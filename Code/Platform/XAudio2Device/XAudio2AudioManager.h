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

// FILE: XAudio2AudioManager.h ////////////////////////////////////////////////////////////////////
// Audio device layer on XAudio2 + X3DAudio (both part of the Windows SDK).
//
// Uses a PlayingAudio/request-list/priority-limit design that plugs into the device-independent
// SoundManager/MusicManager/AudioRequest machinery in Common/GameAudio.cpp; each playing sound owns
// an XAudio2 source voice.

#pragma once

#ifndef __XAUDIO2_AUDIOMANAGER_H_
#define __XAUDIO2_AUDIOMANAGER_H_

#include "Common/AsciiString.h"
#include <memory>
#include "Common/GameAudio.h"
#include <xaudio2.h>
#include <x3daudio.h>

class AudioEventRTS;
struct PlayingAudio;

//-------------------------------------------------------------------------------------------------
// Decoded audio info. Sound effects (AudioSettings::m_soundsExtension) are WAV, PCM or IMA-ADPCM
// (WavParser.h). Music (AudioSettings::m_musicExtension) is MP3, decoded via the FFmpeg backend
// also used for video (Mp3Parser.h, VideoDevice/FFmpeg/FFmpegFile.h); either way, this struct always ends up
// holding interleaved S16 PCM.
//-------------------------------------------------------------------------------------------------
struct WaveFormatInfo
{
	UnsignedInt channels;
	UnsignedInt samplesPerSec;
	UnsignedInt bitsPerSample;
	const void *data;			// points inside the cache's owned buffer, never individually freed
	UnsignedInt dataLen;		// bytes
};

enum class PlayingAudioType
{
	PAT_Sample,
	PAT_3DSample,
	PAT_Stream,
	PAT_INVALID
};

enum class PlayingStatus
{
	PS_Playing,
	PS_Stopped,
	PS_Paused
};

//-------------------------------------------------------------------------------------------------
// Cache of decoded WAV file contents, keyed by filename, refcounted.
//-------------------------------------------------------------------------------------------------
struct OpenAudioFile
{
	WaveFormatInfo m_soundInfo;
	void *m_fileData;								// owned heap buffer holding the decoded PCM bytes
	UnsignedInt m_openCount;
	UnsignedInt m_fileSize;
	const AudioEventInfo *m_eventInfo;	// not owned
	Bool m_countedInCache;				// m_fileSize was added to m_currentlyUsedSize (music is not counted)

	OpenAudioFile() : m_fileData(nullptr), m_openCount(0), m_fileSize(0), m_eventInfo(nullptr), m_countedInCache(FALSE) { }
};

using OpenFilesHash = std::unordered_map<AsciiString, OpenAudioFile, rts::hash<AsciiString>, rts::equal_to<AsciiString> >;
using OpenFilesHashIt = OpenFilesHash::iterator;

class AudioFileCache
{
	public:
		AudioFileCache();
		virtual ~AudioFileCache();

		void *openFile( AudioEventRTS *eventToOpenFrom );
		void closeFile( void *fileToClose );
		void setMaxSize( UnsignedInt size );
		const WaveFormatInfo *findSoundInfo( const void *fileData ) const;

		UnsignedInt getCurrentlyUsedSize() const { return m_currentlyUsedSize; }
		UnsignedInt getMaxSize() const { return m_maxSize; }

	protected:
		void releaseOpenAudioFile( OpenAudioFile *fileToRelease );
		Bool freeEnoughSpaceForSample(const OpenAudioFile& sampleThatNeedsSpace);

		OpenFilesHash m_openFiles;
		UnsignedInt m_currentlyUsedSize;
		UnsignedInt m_maxSize;
		HANDLE m_mutex;
		const char *m_mutexName;
};

//-------------------------------------------------------------------------------------------------
// One shared, stateless IXAudio2VoiceCallback: all per-sound state travels through the buffer's
// pContext (set to the owning PlayingAudio*), so a single instance can service every voice.
//-------------------------------------------------------------------------------------------------
class XAudio2SoundCallback : public IXAudio2VoiceCallback
{
	public:
		void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override { }
		void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override { }
		void STDMETHODCALLTYPE OnStreamEnd() override { }
		void STDMETHODCALLTYPE OnBufferStart(void *pBufferContext) override { }
		void STDMETHODCALLTYPE OnBufferEnd(void *pBufferContext) override;
		void STDMETHODCALLTYPE OnLoopEnd(void *pBufferContext) override;
		void STDMETHODCALLTYPE OnVoiceError(void *pBufferContext, HRESULT Error) override { }
};

//-------------------------------------------------------------------------------------------------
struct PlayingAudio
{
	IXAudio2SourceVoice *m_voice;
	PlayingAudioType m_type;
	volatile PlayingStatus m_status;	// set from the XAudio2 callback thread, read on the main thread
	AudioEventRTS *m_audioEventRTS;
	void *m_file;				// pointer returned by AudioFileCache::openFile(), used for refcounting/lookup
	Bool m_requestStop;
	Bool m_cleanupAudioEventRTS;
	Int m_framesFaded;
	volatile LONG m_loopsCompleted;	// full passes of a looping music buffer, counted on the XAudio2 thread
	LONG m_loopsLogged;				// last m_loopsCompleted written to the -audioLog

	PlayingAudio() :
		m_voice(nullptr),
		m_type(PlayingAudioType::PAT_INVALID),
		m_status(PlayingStatus::PS_Playing),
		m_audioEventRTS(nullptr),
		m_file(nullptr),
		m_requestStop(false),
		m_cleanupAudioEventRTS(true),
		m_framesFaded(0),
		m_loopsCompleted(0),
		m_loopsLogged(0)
	{ }
};

//-------------------------------------------------------------------------------------------------
class XAudio2AudioManager : public AudioManager
{
	public:
#if defined(_DEBUG)
		virtual void audioDebugDisplay(DebugDisplayInterface *dd, void *userData, FILE *fp = nullptr ) override;
#endif

		virtual void init() override;
		virtual void postProcessLoad() override;
		virtual void reset() override;
		virtual void update() override;

		XAudio2AudioManager();
		virtual ~XAudio2AudioManager() override;

		virtual void nextMusicTrack( void ) override;
		virtual void prevMusicTrack( void ) override;
		virtual Bool isMusicPlaying( void ) const override;
		virtual Bool hasMusicTrackCompleted( const AsciiString& trackName, Int numberOfTimes ) const override;
		virtual AsciiString getMusicTrackName( void ) const override;
		virtual Bool has3DSensitiveStreamsPlaying( void ) const override;

		virtual void openDevice( void ) override;
		virtual void closeDevice( void ) override;
		virtual void *getDevice( void ) override { return m_xaudio2; }

		virtual void stopAudio( AudioAffect which ) override;
		virtual void pauseAudio( AudioAffect which ) override;
		virtual void resumeAudio( AudioAffect which ) override;
		virtual void pauseAmbient( Bool shouldPause ) override;

		virtual void killAudioEventImmediately( AudioHandle audioEvent ) override;


		virtual Bool isCurrentlyPlaying( AudioHandle handle ) override;

		virtual void notifyOfAudioCompletion( UnsignedInt audioCompleted, UnsignedInt flags ) override;

		virtual UnsignedInt getProviderCount( void ) const override;
		virtual AsciiString getProviderName( UnsignedInt providerNum ) const override;
		virtual UnsignedInt getProviderIndex( AsciiString providerName ) const override;
		virtual void selectProvider( UnsignedInt providerNdx ) override;
		virtual void unselectProvider( void ) override;
		virtual UnsignedInt getSelectedProvider( void ) const override;
		virtual void setSpeakerType( UnsignedInt speakerType ) override;
		virtual UnsignedInt getSpeakerType( void ) override;

		virtual void *getHandleForBink( void ) override;
		virtual void releaseHandleForBink( void ) override;

		virtual void friend_forcePlayAudioEventRTS(const AudioEventRTS* eventToPlay) override;

		virtual UnsignedInt getNum2DSamples( void ) const override;
		virtual UnsignedInt getNum3DSamples( void ) const override;
		virtual UnsignedInt getNumStreams( void ) const override;

		virtual Bool doesViolateLimit( AudioEventRTS *event ) const override;
		virtual Bool isPlayingLowerPriority( AudioEventRTS *event ) const override;
		virtual Bool isPlayingAlready( AudioEventRTS *event ) const override;
		virtual Bool isObjectPlayingVoice( UnsignedInt objID ) const override;
		Bool killLowestPrioritySoundImmediately( AudioEventRTS *event );
		AudioEventRTS *findLowestPrioritySound( AudioEventRTS *event );

		virtual void adjustVolumeOfPlayingAudio(AsciiString eventName, Real newVolume) override;

		virtual void removePlayingAudio( AsciiString eventName ) override;
		virtual void removeAllDisabledAudio() override;

		virtual void processRequestList( void ) override;
		void processPlayingList( void );
		void processFadingList( void );
		void processStoppedList( void );

		Bool shouldProcessRequestThisFrame( AudioRequest *req ) const;
		void adjustRequest( AudioRequest *req );
		Bool checkForSample( AudioRequest *req );

		virtual void setHardwareAccelerated(Bool accel) override;
		virtual void setSpeakerSurround(Bool surround) override;

		virtual void setPreferredProvider(AsciiString provider) override { m_pref3DProvider = provider; }
		virtual void setPreferredSpeaker(AsciiString speakerType) override { m_prefSpeaker = speakerType; }

		virtual Real getFileLengthMS( AsciiString strToLoad ) const override;

		virtual void closeAnySamplesUsingFile( const void *fileToClose ) override;

		// Called by XAudio2SoundCallback::OnBufferEnd on the audio engine thread -- must stay
		// minimal/lock-free (just flags), matching the original push-model comment in GameAudio.h.
		void onVoiceBufferEnd( PlayingAudio *playing );

	protected:
		// 3-D functions
		virtual void setDeviceListenerPosition( void ) override;
		const Coord3D *getCurrentPositionFromEvent( AudioEventRTS *event );
		Real getEffectiveVolume(AudioEventRTS *event) const;

		// Looping/portion-chaining (attack -> sound -> decay, or repeated loops, possibly with a
		// freshly-randomized filename each time -- see AudioEventInfo's AC_RANDOM control flag)
		Bool startNextLoop( PlayingAudio *looping );
		void advanceAudioPortion( PlayingAudio *playing );

		void playStream( AudioEventRTS *event, PlayingAudio *audio );
		void *playSample( AudioEventRTS *event, PlayingAudio *audio );
		void *playSample3D( AudioEventRTS *event, PlayingAudio *audio );
		void apply3DAudioSettings( PlayingAudio *audio, const Coord3D *pos );

	protected:
		void processRequest( AudioRequest *req );

		void playAudioEvent( AudioEventRTS *event );
		void stopAudioEvent( AudioHandle handle );
		void pauseAudioEvent( AudioHandle handle );

		void *loadFileForRead( AudioEventRTS *eventToLoadFrom );
		void closeFile( void *fileRead );

		PlayingAudio *allocatePlayingAudio( void );
		void destroyVoice( PlayingAudio *release );
		void releasePlayingAudio( PlayingAudio *release );

		void stopAllAudioImmediately( void );

		void adjustPlayingVolume( PlayingAudio *audio );
		void stopAllSpeech( void );

		IXAudio2SubmixVoice *getSubmixFor( AudioType soundType, Bool positional ) const;

	protected:
		IXAudio2 *m_xaudio2;
		IXAudio2MasteringVoice *m_masteringVoice;
		IXAudio2SubmixVoice *m_musicSubmix;
		IXAudio2SubmixVoice *m_soundSubmix;
		IXAudio2SubmixVoice *m_sound3DSubmix;
		IXAudio2SubmixVoice *m_speechSubmix;
		X3DAUDIO_HANDLE m_x3dInstance;
		UnsignedInt m_masterChannels;
		Bool m_comInitialized;

		AsciiString m_pref3DProvider;
		AsciiString m_prefSpeaker;
		UnsignedInt m_selectedSpeakerType;

		std::list<PlayingAudio *> m_playingSounds;
		std::list<PlayingAudio *> m_playing3DSounds;
		std::list<PlayingAudio *> m_playingStreams;
		std::list<PlayingAudio *> m_fadingAudio;
		std::list<PlayingAudio *> m_stoppedAudio;

		// One-off, fire-and-forget playback used only by friend_forcePlayAudioEventRTS() (loading
		// screens) -- bypasses the priority/limit system entirely.
		std::list<IXAudio2SourceVoice *> m_audioForcePlayed;

		std::unique_ptr<AudioFileCache> m_audioCache;
		PlayingAudio *m_binkHandle;			// unused stub; video audio uses its own voices
		UnsignedInt m_num2DSamples;
		UnsignedInt m_num3DSamples;
		UnsignedInt m_numStreams;

#if defined(_DEBUG)
		using SetAsciiString = std::set<AsciiString>;
		SetAsciiString m_allEventsLoaded;
		void dumpAllAssetsUsed();
#endif
};

#endif // __XAUDIO2_AUDIOMANAGER_H_
