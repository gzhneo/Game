/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// LogicAudio.cpp -- see Common/LogicAudio.h.

#include "PreRTS.h"

#include "Common/LogicAudio.h"
#include "Common/AudioEventRTS.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/GameAudio.h"


//-------------------------------------------------------------------------------------------------
LogicAudio::LogicAudio() : m_addedTotal(0), m_cancelledTotal(0)
{
	m_queue.reserve(256);
}

//-------------------------------------------------------------------------------------------------
LogicAudio::~LogicAudio()
{
	clear();
}

//-------------------------------------------------------------------------------------------------
LogicAudio::Command& LogicAudio::push( CommandKind kind )
{
	m_queue.push_back(Command());
	Command& c = m_queue.back();
	c.kind = kind;
	c.event = nullptr;
	c.handle = AHSV_Error;
	c.value = 0.0f;
	c.affect = 0;
	c.flag = FALSE;
	return c;
}

//-------------------------------------------------------------------------------------------------
AudioHandle LogicAudio::addAudioEvent( const AudioEventRTS *eventToAdd )
{
	// Prepare immediately (consumes logic random numbers, assigns the handle, sets the play index on
	// the owning object); only the hand-off to the sound or music manager is deferred.
	AudioHandle handle = AHSV_Error;
	AudioEventRTS *prepared = TheAudio->prepareAudioEvent(eventToAdd, handle);
	if (prepared == nullptr)
		return handle;

	++m_addedTotal;
	Command& c = push(CommandKind::CMD_AddEvent);
	c.event = prepared;
	c.handle = handle;
	return handle;
}

//-------------------------------------------------------------------------------------------------
void LogicAudio::removeAudioEvent( AudioHandle audioEvent )
{
	// An event started and stopped in the same tick never reaches the audio system.
	if (audioEvent >= AHSV_FirstHandle)
	{
		for (std::vector<Command>::iterator it = m_queue.begin(); it != m_queue.end(); ++it)
		{
			if (it->kind == CommandKind::CMD_AddEvent && it->handle == audioEvent)
			{
				TheAudio->releaseAudioEventRTS(it->event);
				m_queue.erase(it);
				++m_cancelledTotal;
				return;
			}
		}
	}
	Command& c = push(CommandKind::CMD_RemoveHandle);
	c.handle = audioEvent;
}

//-------------------------------------------------------------------------------------------------
void LogicAudio::removeAudioEvent( AsciiString eventToRemove )
{
	Command& c = push(CommandKind::CMD_RemoveName);
	c.name = eventToRemove;
}

//-------------------------------------------------------------------------------------------------
void LogicAudio::pauseAudio( AudioAffect which )
{
	Command& c = push(CommandKind::CMD_PauseAudio);
	c.affect = static_cast<Int>(which);
}

//-------------------------------------------------------------------------------------------------
void LogicAudio::resumeAudio( AudioAffect which )
{
	Command& c = push(CommandKind::CMD_ResumeAudio);
	c.affect = static_cast<Int>(which);
}

//-------------------------------------------------------------------------------------------------
void LogicAudio::pauseAmbient( Bool shouldPause )
{
	Command& c = push(CommandKind::CMD_PauseAmbient);
	c.flag = shouldPause;
}

//-------------------------------------------------------------------------------------------------
void LogicAudio::setVolume( Real volume, AudioAffect whichToAffect )
{
	Command& c = push(CommandKind::CMD_SetVolume);
	c.value = volume;
	c.affect = static_cast<Int>(whichToAffect);
}

//-------------------------------------------------------------------------------------------------
void LogicAudio::setAudioEventEnabled( AsciiString eventToAffect, Bool enable )
{
	Command& c = push(CommandKind::CMD_SetEventEnabled);
	c.name = eventToAffect;
	c.flag = enable;
}

//-------------------------------------------------------------------------------------------------
void LogicAudio::setAudioEventVolumeOverride( AsciiString eventToAffect, Real newVolume )
{
	Command& c = push(CommandKind::CMD_SetEventVolumeOverride);
	c.name = eventToAffect;
	c.value = newVolume;
}

//-------------------------------------------------------------------------------------------------
void LogicAudio::removeDisabledEvents( void )
{
	push(CommandKind::CMD_RemoveDisabledEvents);
}

//-------------------------------------------------------------------------------------------------
void LogicAudio::addAudioEventInfo( AudioEventInfo *newEventInfo )
{
	// Map-specific event infos (Object::doSpecialPowerAudio and similar) must be visible to the next
	// findAudioEventInfo() of the same logic, so this is immediate, not queued. Only the logic and
	// loading write the table.
	TheAudio->addAudioEventInfo(newEventInfo);
}

//-------------------------------------------------------------------------------------------------
const MiscAudio *LogicAudio::getMiscAudio( void ) const
{
	return TheAudio->getMiscAudio();
}

//-------------------------------------------------------------------------------------------------
void LogicAudio::getInfoForAudioEvent( const AudioEventRTS *eventToFindAndFill ) const
{
	TheAudio->getInfoForAudioEvent(eventToFindAndFill);
}

//-------------------------------------------------------------------------------------------------
AudioEventInfo *LogicAudio::findAudioEventInfo( AsciiString eventName ) const
{
	return TheAudio->findAudioEventInfo(eventName);
}

//-------------------------------------------------------------------------------------------------
Bool LogicAudio::isValidAudioEvent( const AudioEventRTS *eventToCheck ) const
{
	return TheAudio->isValidAudioEvent(eventToCheck);
}

//-------------------------------------------------------------------------------------------------
Bool LogicAudio::isValidAudioEvent( AudioEventRTS *eventToCheck ) const
{
	return TheAudio->isValidAudioEvent(eventToCheck);
}

//-------------------------------------------------------------------------------------------------
Real LogicAudio::getAudioLengthMS( const AudioEventRTS *event )
{
	return TheAudio->getAudioLengthMS(event);
}

//-------------------------------------------------------------------------------------------------
Bool LogicAudio::isCurrentlyPlaying( AudioHandle handle )
{
	for (std::vector<Command>::const_iterator it = m_queue.begin(); it != m_queue.end(); ++it)
	{
		if (it->kind == CommandKind::CMD_AddEvent && it->handle == handle)
			return TRUE;
	}
	return TheAudio->isCurrentlyPlaying(handle);
}

//-------------------------------------------------------------------------------------------------
Bool LogicAudio::hasMusicTrackCompleted( const AsciiString& trackName, Int numberOfTimes ) const
{
	return TheAudio->hasMusicTrackCompleted(trackName, numberOfTimes);
}

//-------------------------------------------------------------------------------------------------
void LogicAudio::flush( void )
{
	if (m_queue.empty())
		return;

	// Nothing should be queued during the flush (TheAudio does not call the logic); the loop still
	// uses an index so that an append cannot invalidate an iterator.
	for (size_t i = 0; i < m_queue.size(); ++i)
	{
		Command& c = m_queue[i];
		switch (c.kind)
		{
			case CommandKind::CMD_AddEvent:
				TheAudio->addPreparedAudioEvent(c.event);	// takes ownership of the copy
				c.event = nullptr;
				break;
			case CommandKind::CMD_RemoveHandle:
				TheAudio->removeAudioEvent(c.handle);
				break;
			case CommandKind::CMD_RemoveName:
				TheAudio->removeAudioEvent(c.name);
				break;
			case CommandKind::CMD_PauseAudio:
				TheAudio->pauseAudio(static_cast<AudioAffect>(c.affect));
				break;
			case CommandKind::CMD_ResumeAudio:
				TheAudio->resumeAudio(static_cast<AudioAffect>(c.affect));
				break;
			case CommandKind::CMD_PauseAmbient:
				TheAudio->pauseAmbient(c.flag);
				break;
			case CommandKind::CMD_SetVolume:
				TheAudio->setVolume(c.value, static_cast<AudioAffect>(c.affect));
				break;
			case CommandKind::CMD_SetEventEnabled:
				TheAudio->setAudioEventEnabled(c.name, c.flag);
				break;
			case CommandKind::CMD_SetEventVolumeOverride:
				TheAudio->setAudioEventVolumeOverride(c.name, c.value);
				break;
			case CommandKind::CMD_RemoveDisabledEvents:
				TheAudio->removeDisabledEvents();
				break;
		}
	}
	m_queue.clear();
}

//-------------------------------------------------------------------------------------------------
void LogicAudio::clear( void )
{
	for (std::vector<Command>::iterator it = m_queue.begin(); it != m_queue.end(); ++it)
	{
		if (it->kind == CommandKind::CMD_AddEvent && it->event != nullptr && TheAudio != nullptr)
			TheAudio->releaseAudioEventRTS(it->event);
		it->event = nullptr;
	}
	m_queue.clear();
}
