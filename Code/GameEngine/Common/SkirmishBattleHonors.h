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

///////////////////////////////////////////////////////////////////////////////////////
// FILE: SkirmishBattleHonors.h
// Author: Matthew D. Campbell, November 2002
// Description: Saving/Loading of skirmish battle honors info
///////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __SKIRMISHBATTLEHONORS_H__
#define __SKIRMISHBATTLEHONORS_H__

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "Common/UnicodeString.h"
#include "Common/AsciiString.h"
#include "Engine/PreferenceFile.h"

class Image;
class GameWindow;
class WinInstanceData;

//-----------------------------------------------------------------------------
// SkirmishBattleHonors: statistics of skirmish games and campaigns (SkirmishStats.ini, "key = value")
//-----------------------------------------------------------------------------
class SkirmishBattleHonors
{
public:
	SkirmishBattleHonors();
	~SkirmishBattleHonors();

	/// Forgets every value; the next write() empties the file.
	void clear();
	/// Writes what this instance changed since it loaded or last wrote, on top of the file.
	Bool write();

	void setWins(Int val);
	Int getWins(void) const;

	void setLosses(Int val);
	Int getLosses(void) const;

	void setWinStreak(Int val);
	Int getWinStreak(void) const;

	void setBestWinStreak(Int val);
	Int getBestWinStreak(void) const;

	void setChallengeMedals(Int val);
	Int getChallengeMedals(void) const;

	void setBuiltSCUD(void);
	Bool builtSCUD(void) const;

	void setBuiltParticleCannon(void);
	Bool builtParticleCannon(void) const;

	void setBuiltNuke(void);
	Bool builtNuke(void) const;

	void setHonors(Int mask);
	Int getHonors(void) const;

	void setEnduranceMedal(AsciiString mapName, Int difficulty, int numAIs);
	Int getEnduranceMedal(AsciiString mapName, Int difficulty) const;

	void setLastGeneral(AsciiString val);
	AsciiString getLastGeneral(void) const;

	void setNumGamesLoyal(Int val);
	Int getNumGamesLoyal(void) const;

	void setUSACampaignComplete(GameDifficulty difficulty);
	Bool getUSACampaignComplete(GameDifficulty difficulty) const;

	void setCHINACampaignComplete(GameDifficulty difficulty);
	Bool getCHINACampaignComplete(GameDifficulty difficulty) const;

	void setGLACampaignComplete(GameDifficulty difficulty);
	Bool getGLACampaignComplete(GameDifficulty difficulty) const;

	void setChallengeCampaignComplete(Int challenge, GameDifficulty difficulty);
	Bool getChallengeCampaignComplete(Int challenge, GameDifficulty difficulty) const;

private:
	Bool getBool(const AsciiString &key, Bool defaultValue) const;
	Int getInt(const AsciiString &key, Int defaultValue) const;
	AsciiString getAsciiString(const AsciiString &key, const AsciiString &defaultValue) const;
	void setBool(const AsciiString &key, Bool val);
	void setInt(const AsciiString &key, Int val);
	void setAsciiString(const AsciiString &key, const AsciiString &val);

	AsciiString m_filename;
	Engine::PreferenceValues m_values;
	Engine::PreferenceValues m_loaded;	///< file contents at load or the last write
};

// Empty ResetBattleHonorInsertion/InsertBattleHonor/BattleHonorTooltip are in SkirmishGameOptionsMenu.cpp.

#endif // __SKIRMISHBATTLEHONORS_H__
