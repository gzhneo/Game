/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EditorAudio.h -- a silent audio manager for the map editor.
//
// Object definitions name their sounds, and the INI reader looks each one up in TheAudio, so the
// catalog needs an audio manager. The base class keeps the sound definitions (AudioSettings.ini,
// SoundEffects.ini, ...); this one plays nothing and opens no audio device, so the editor also
// starts on machines without sound output.

#pragma once

#include "Common/AsciiString.h"
#include "Common/GameAudio.h"

namespace MapEditorEngine
{

class EditorAudioManager : public AudioManager
{
public:
#if defined(_DEBUG)
	virtual void audioDebugDisplay(DebugDisplayInterface *, void *, FILE * = nullptr) override {}
#endif
	virtual void stopAudio(AudioAffect) override {}
	virtual void pauseAudio(AudioAffect) override {}
	virtual void resumeAudio(AudioAffect) override {}
	virtual void pauseAmbient(Bool) override {}
	virtual void killAudioEventImmediately(AudioHandle) override {}
	virtual void nextMusicTrack(void) override {}
	virtual void prevMusicTrack(void) override {}
	virtual Bool isMusicPlaying(void) const override { return FALSE; }
	virtual Bool hasMusicTrackCompleted(const AsciiString &, Int) const override { return FALSE; }
	virtual AsciiString getMusicTrackName(void) const override { return AsciiString::TheEmptyString; }
	virtual void openDevice(void) override {}
	virtual void closeDevice(void) override {}
	virtual void *getDevice(void) override { return nullptr; }
	virtual void notifyOfAudioCompletion(UnsignedInt, UnsignedInt) override {}
	virtual UnsignedInt getProviderCount(void) const override { return 0; }
	virtual AsciiString getProviderName(UnsignedInt) const override { return AsciiString::TheEmptyString; }
	virtual UnsignedInt getProviderIndex(AsciiString) const override { return 0; }
	virtual void selectProvider(UnsignedInt) override {}
	virtual void unselectProvider(void) override {}
	virtual UnsignedInt getSelectedProvider(void) const override { return 0; }
	virtual void setSpeakerType(UnsignedInt) override {}
	virtual UnsignedInt getSpeakerType(void) override { return 0; }
	virtual UnsignedInt getNum2DSamples(void) const override { return 0; }
	virtual UnsignedInt getNum3DSamples(void) const override { return 0; }
	virtual UnsignedInt getNumStreams(void) const override { return 0; }
	virtual Bool doesViolateLimit(AudioEventRTS *) const override { return TRUE; }
	virtual Bool isPlayingLowerPriority(AudioEventRTS *) const override { return FALSE; }
	virtual Bool isPlayingAlready(AudioEventRTS *) const override { return FALSE; }
	virtual Bool isObjectPlayingVoice(UnsignedInt) const override { return FALSE; }
	virtual void adjustVolumeOfPlayingAudio(AsciiString, Real) override {}
	virtual void removePlayingAudio(AsciiString) override {}
	virtual void removeAllDisabledAudio() override {}
	virtual Bool has3DSensitiveStreamsPlaying(void) const override { return FALSE; }
	virtual void *getHandleForBink(void) override { return nullptr; }
	virtual void releaseHandleForBink(void) override {}
	virtual void friend_forcePlayAudioEventRTS(const AudioEventRTS *) override {}
	virtual void setPreferredProvider(AsciiString) override {}
	virtual void setPreferredSpeaker(AsciiString) override {}
	virtual Real getFileLengthMS(AsciiString) const override { return 0.0f; }
	virtual void closeAnySamplesUsingFile(const void *) override {}
	virtual void setDeviceListenerPosition(void) override {}
};

} // namespace MapEditorEngine
