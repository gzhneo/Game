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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////


#pragma once

#include "Common/MessageStream.h"
#include <memory>
#include "GameNetwork/GameInfo.h"

/**
  * The ReplayGameInfo class holds information about the replay game and
	* the contents of its slot list for reconstructing multiplayer games.
	*/
class ReplayGameInfo : public GameInfo
{
private:
	GameSlot m_ReplaySlot[MAX_SLOTS];

public:
	ReplayGameInfo()
	{
		for (Int i = 0; i< MAX_SLOTS; ++i)
			setSlotPointer(i, &m_ReplaySlot[i]);
	}
};

enum class RecorderModeType {
	RECORDERMODETYPE_RECORD,
	RECORDERMODETYPE_PLAYBACK,
	RECORDERMODETYPE_NONE // this is a valid state to be in on the shell map, or in saved games
};

class CRCInfo;

class RecorderClass : public SubsystemInterface {
public:
	RecorderClass();																	///< Constructor.
	virtual ~RecorderClass() override;													///< Destructor.

	void init() override;																			///< Initialize TheRecorder.
	void reset() override;																			///< Reset the state of TheRecorder.
	void update() override;																		///< General purpose update function.

	// Methods dealing with recording.
	void updateRecord();															///< The update function for recording.

	// Methods dealing with playback.
	void updatePlayback();														///< The update function for playing back a file.
	Bool playbackFile(AsciiString filename);					///< Starts playback of the specified file.
	/// Plays a replay that still grows (a game watched over the network): frames are only played up to
	/// the limit setLiveLimit() names, the end of the file is waited at until the stream has ended.
	Bool playbackLive(AsciiString filename);
	void setLiveLimit(UnsignedInt frames, Bool ended);
	Bool isLive() const { return m_live; }
	/// Live: the next logic frame needs data that has not arrived yet.
	Bool isLiveWaiting() const;
	/// Live: far behind the game; frames run without the tick rate until close again.
	Bool isLiveCatchingUp() const;
	/// Recording: the full path of the file being written; empty when none is.
	AsciiString getRecordingPath();
	Bool testVersionPlayback(AsciiString filename);   ///< Returns if the playback is a valid playback file for this version or not.
	Bool replayModTag(AsciiString filename, AsciiString &tag);	///< the mods the replay was recorded with (empty without)
	AsciiString getCurrentReplayFilename( void );			///< valid during playback only
	void stopPlayback();															///< Stops playback.  Its fine to call this even if not playing back a file.
	Bool isPlaybackFinished() const { return m_playbackFinished; }
	void setPlaybackFinished() { m_playbackFinished = TRUE; }	///< Playback reached the end of the file (runner mode; GameEngine::execute cleans up)
#if defined _DEBUG
	Bool analyzeReplay( AsciiString filename );
	Bool isAnalysisInProgress( void );
#endif

public:
	void handleCRCMessage(UnsignedInt newCRC, Int playerIndex, Bool fromPlayback);
protected:
	std::unique_ptr<CRCInfo> m_crcInfo;
public:

	// read in info relating to a replay, conditionally setting up m_file for playback
	struct ReplayHeader
	{
		AsciiString filename;
		Bool forPlayback;
		UnicodeString replayName;
		SYSTEMTIME timeVal;
		UnicodeString versionString;
		UnicodeString versionTimeString;
		UnsignedInt versionNumber;
		UnsignedInt exeCRC;
		UnsignedInt iniCRC;
		time_t startTime;
		time_t endTime;
		UnsignedInt frameDuration;
		Bool quitEarly;
		Bool desyncGame;
		Bool playerDiscons[MAX_SLOTS];
		AsciiString gameOptions;
		Int localPlayerIndex;
	};
	Bool readReplayHeader( ReplayHeader& header );
	/// Reads the header of a replay from `file` up to its first command (another file than the one
	/// recorded or played: a replay that arrives over the network). False if it is none.
	static Bool readHeaderFrom( FILE *file, ReplayHeader &header );
	/// Reads the command at the position of `file` (its frame number read before). The command, or
	/// nullptr for a message outside the network commands, which is read and dropped. `ok` is false
	/// if the file ended within it.
	static GameMessage *readCommandFrom( FILE *file, Bool &ok, Bool logIt = FALSE );

	RecorderModeType getMode();												///< Returns the current operating mode.
	void initControls();															///< Show or Hide the Replay controls

	AsciiString getReplayDir();												///< Returns the directory that holds the replay files.
	AsciiString getReplayExtention();									///< Returns the file extention for replay files.
	AsciiString getLastReplayFileName();							///< Returns the filename used for the default replay.

	GameInfo *getGameInfo( void ) { return &m_gameInfo; }	///< Returns the slot list for playback game start

	Bool isMultiplayer( void );												///< is this a multiplayer game (record OR playback)?

	Int getGameMode( void ) { return m_originalGameMode; }

	void logPlayerDisconnect(UnicodeString player, Int slot);
	void logCRCMismatch( void );
	void cleanUpReplayFile( void );										///< after a crash, send replay/debug info to a central repository

	void stopRecording();															///< Stop recording and close m_file.
protected:
	void startRecording(GameDifficulty diff, Int originalGameMode, Int rankPoints, Int maxFPS);					///< Start recording to m_file.
	void writeToFile(GameMessage *msg);								///< Write this GameMessage to m_file.

	void logGameStart(AsciiString options);
	void logGameEnd( void );

	static AsciiString readAsciiString(FILE *file);							///< Read the next string using ascii characters.
	static UnicodeString readUnicodeString(FILE *file);						///< Read the next string using unicode characters.
	static Bool readHeaderFields(FILE *file, ReplayHeader &header);	///< from GENREP to the local player's slot
	void readNextFrame();															///< Read the next frame number to execute a command on.
	void appendNextCommand();													///< Read the next GameMessage and append it to TheCommandList.
	void writeArgument(GameMessageArgumentDataType type, const GameMessageArgumentType arg);
	static Bool readArgument(FILE *file, GameMessageArgumentDataType type, GameMessage *msg, Bool logIt);

	void cullBadCommands();														///< prevent the user from giving mouse commands that he shouldn't be able to do during playback.

	FILE *m_file;
	Bool m_playbackFinished;	///< stopPlayback in runner mode reached the end of the file
	AsciiString m_fileName;
	Int m_currentFilePosition;
	RecorderModeType m_mode;
	AsciiString m_currentReplayFilename;							///< valid during playback only

	ReplayGameInfo m_gameInfo;
	Bool m_wasDesync;

	Bool m_doingAnalysis;

	Int m_originalGameMode; // valid in replays

	UnsignedInt m_nextFrame;												///< The Frame that the next message is to be executed on.  This can be -1.

	Bool m_live;				///< playbackLive
	Bool m_liveEnded;			///< the live file is complete
	Bool m_liveFrameDue;		///< live: the next frame number is not in the file yet
	UnsignedInt m_liveFrames;	///< live: frames below this are complete in the file
};

#include "Engine/EngineContext.h"
RecorderClass *createRecorder();