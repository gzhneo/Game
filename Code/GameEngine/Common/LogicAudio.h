/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// LogicAudio.h -- audio events issued by the game logic.
//
// The logic (Source/GameLogic) does not call TheAudio directly but goes through this facade. Every
// call that changes audio state (start, stop, pause, volume) is queued in call order and handed to
// TheAudio by GameEngine::update() via flush() after the logic tick. This keeps the audio system on
// the main thread, which a separate simulation thread requires.
//
// Determinism: preparing an event (looking up the event info, rolling filename and play parameters,
// assigning the handle) still happens immediately in the call, because logical events consume logic
// random numbers there (AudioEventRTS::generateFilename/generatePlayInfo). Only the hand-off to the
// sound or music manager (AudioManager::addPreparedAudioEvent) is deferred, so the handle is known
// at once and is the real audio-system handle.
//
// Read-only calls (event info, lengths, MiscAudio) pass straight through; they only read the INI
// tables loaded at startup. isCurrentlyPlaying() also considers events not yet handed off.

#pragma once

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"
#include "Common/GameAudio.h"
#include <vector>

class AudioEventRTS;
struct AudioEventInfo;	// struct as in GameAudio.h/AudioEventRTS.h (C4099)
struct MiscAudio;

class LogicAudio
{
public:
	LogicAudio();
	~LogicAudio();

	// --- writing: queued, executed in flush() ---------------------------------------------
	AudioHandle addAudioEvent( const AudioEventRTS *eventToAdd );
	void removeAudioEvent( AudioHandle audioEvent );
	void removeAudioEvent( AsciiString eventToRemove );
	void pauseAudio( AudioAffect which );
	void resumeAudio( AudioAffect which );
	void pauseAmbient( Bool shouldPause );
	void setVolume( Real volume, AudioAffect whichToAffect );
	void setAudioEventEnabled( AsciiString eventToAffect, Bool enable );
	void setAudioEventVolumeOverride( AsciiString eventToAffect, Real newVolume );
	void removeDisabledEvents( void );
	void addAudioEventInfo( AudioEventInfo *newEventInfo );

	// --- reading: passed straight through -----------------------------------------------------
	const MiscAudio *getMiscAudio( void ) const;
	void getInfoForAudioEvent( const AudioEventRTS *eventToFindAndFill ) const;
	AudioEventInfo *findAudioEventInfo( AsciiString eventName ) const;
	Bool isValidAudioEvent( const AudioEventRTS *eventToCheck ) const;
	Bool isValidAudioEvent( AudioEventRTS *eventToCheck ) const;
	Real getAudioLengthMS( const AudioEventRTS *event );
	Bool isCurrentlyPlaying( AudioHandle handle );
	Bool hasMusicTrackCompleted( const AsciiString& trackName, Int numberOfTimes ) const;

	// --- engine side --------------------------------------------------------------------------
	/// Hand the queued calls to TheAudio in order (GameEngine::update, after the logic tick).
	void flush( void );
	/// Discard the queued calls and free prepared events (GameEngine::reset, shutdown).
	void clear( void );
	UnsignedInt getQueuedCount( void ) const { return static_cast<UnsignedInt>(m_queue.size()); }

	// Statistics for the debug log at shutdown: events queued, and how many were removed before hand-off.
	UnsignedInt getAddedTotal( void ) const { return m_addedTotal; }
	UnsignedInt getCancelledTotal( void ) const { return m_cancelledTotal; }

private:
	enum class CommandKind : int
	{
		CMD_AddEvent,
		CMD_RemoveHandle,
		CMD_RemoveName,
		CMD_PauseAudio,
		CMD_ResumeAudio,
		CMD_PauseAmbient,
		CMD_SetVolume,
		CMD_SetEventEnabled,
		CMD_SetEventVolumeOverride,
		CMD_RemoveDisabledEvents,
	};

	struct Command
	{
		CommandKind kind;
		AudioEventRTS *event;		///< CMD_AddEvent: prepared copy (owned by the facade until flush())
		AudioHandle handle;			///< CMD_AddEvent, CMD_RemoveHandle
		AsciiString name;			///< CMD_RemoveName, CMD_SetEvent*
		Real value;					///< CMD_SetVolume, CMD_SetEventVolumeOverride
		Int affect;					///< AudioAffect for CMD_PauseAudio/ResumeAudio/SetVolume
		Bool flag;					///< CMD_PauseAmbient, CMD_SetEventEnabled
	};

	Command& push( CommandKind kind );

	std::vector<Command> m_queue;
	UnsignedInt m_addedTotal;
	UnsignedInt m_cancelledTotal;
};

#include "Engine/EngineContext.h"
