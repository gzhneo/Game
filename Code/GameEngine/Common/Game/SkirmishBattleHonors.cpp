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
// FILE: SkirmishBattleHonors.cpp
// Author: Matthew D. Campbell, April 2002
// Description: Saving/Loading of skirmish battle honors info
///////////////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "Common/SkirmishBattleHonors.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/QuotedPrintable.h"
#include "Common/MultiplayerSettings.h"
#include "GameClient/MapUtil.h"

//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PRIVATE TYPES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PRIVATE DATA ///////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PUBLIC DATA ////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// SkirmishBattleHonors base class 
//-----------------------------------------------------------------------------

namespace
{
// Reads "key = value" lines; lines without '=' or with an empty key or value are skipped.
Bool readPreferenceFile(const AsciiString &path, Engine::PreferenceValues &into)
{
	FILE *fp = fopen(path.str(), "r");
	if (fp == nullptr)
		return false;

	char buf[2048];
	std::string key, value;
	while( fgets( buf, sizeof(buf), fp ) != nullptr )
	{
		if (Engine::parsePreferenceLine(buf, key, value))
			into[key] = value;
	}
	fclose(fp);
	return true;
}
}

SkirmishBattleHonors::SkirmishBattleHonors()
{
	m_filename = TheGlobalData->getPath_UserData();
	m_filename.concat("SkirmishStats.ini");
	readPreferenceFile(m_filename, m_values);
	m_loaded = m_values;
}

SkirmishBattleHonors::~SkirmishBattleHonors()
{
}

void SkirmishBattleHonors::clear()
{
	m_values.clear();
}

Bool SkirmishBattleHonors::write()
{
	// Several local instances (-netzLokal) share the file and would overwrite each other.
	if (TheGlobalData && TheGlobalData->m_netLocalInstance > 0)
		return false;

	// Several instances can be alive at once: write only what this one changed, on top of the file.
	Engine::PreferenceValues merged;
	readPreferenceFile(m_filename, merged);
	Engine::mergePreferenceChanges(m_loaded, m_values, merged);

	FILE *fp = fopen(m_filename.str(), "w");
	if (fp == nullptr)
		return false;
	for (Engine::PreferenceValues::const_iterator it = merged.begin(); it != merged.end(); ++it)
		fprintf(fp, "%s = %s\n", it->first.c_str(), it->second.c_str());
	fclose(fp);
	m_values = merged;
	m_loaded = merged;
	return true;
}

Bool SkirmishBattleHonors::getBool(const AsciiString &key, Bool defaultValue) const
{
	AsciiString val = getAsciiString(key, AsciiString::TheEmptyString);
	if (val.isEmpty())
		return defaultValue;
	val.toLower();
	return (val == "1" || val == "t" || val == "true" || val == "y" || val == "yes" || val == "ok");
}

Int SkirmishBattleHonors::getInt(const AsciiString &key, Int defaultValue) const
{
	AsciiString val = getAsciiString(key, AsciiString::TheEmptyString);
	if (val.isEmpty())
		return defaultValue;
	return atoi(val.str());
}

AsciiString SkirmishBattleHonors::getAsciiString(const AsciiString &key, const AsciiString &defaultValue) const
{
	Engine::PreferenceValues::const_iterator it = m_values.find(key.str());
	if (it == m_values.end())
		return defaultValue;
	return AsciiString(it->second.c_str());
}

void SkirmishBattleHonors::setBool(const AsciiString &key, Bool val)
{
	m_values[key.str()] = val ? "1" : "0";
}

void SkirmishBattleHonors::setInt(const AsciiString &key, Int val)
{
	m_values[key.str()] = std::to_string(val);
}

void SkirmishBattleHonors::setAsciiString(const AsciiString &key, const AsciiString &val)
{
	m_values[key.str()] = val.str();
}

void SkirmishBattleHonors::setWins(Int val)
{
	setInt("Wins", val);
}

Int SkirmishBattleHonors::getWins(void) const
{
	return getInt("Wins", 0);
}

void SkirmishBattleHonors::setLosses(Int val)
{
	setInt("Losses", val);
}

Int SkirmishBattleHonors::getLosses(void) const
{
	return getInt("Losses", 0);
}

void SkirmishBattleHonors::setWinStreak(Int val)
{
	setInt("WinStreak", val);
}

Int SkirmishBattleHonors::getWinStreak(void) const
{
	return getInt("WinStreak", 0);
}

void SkirmishBattleHonors::setBestWinStreak(Int val)
{
	setInt("BestWinStreak", val);
}

Int SkirmishBattleHonors::getBestWinStreak(void) const
{
	return getInt("BestWinStreak", 0);
}

void SkirmishBattleHonors::setChallengeMedals(Int val)
{
	setInt("Challenge", val);
}

Int SkirmishBattleHonors::getChallengeMedals(void) const
{
	return getInt("Challenge", 0);
}

void SkirmishBattleHonors::setBuiltSCUD(void)
{
	setBool("SCUD", TRUE);
}

Bool SkirmishBattleHonors::builtSCUD(void) const
{
	return getBool("SCUD", FALSE);
}

void SkirmishBattleHonors::setBuiltParticleCannon(void)
{
	setBool("PPC", TRUE);
}

Bool SkirmishBattleHonors::builtParticleCannon(void) const
{
	return getBool("PPC", FALSE);
}

void SkirmishBattleHonors::setBuiltNuke(void)
{
	setBool("Nuke", TRUE);
}

Bool SkirmishBattleHonors::builtNuke(void) const
{
	return getBool("Nuke", FALSE);
}

void SkirmishBattleHonors::setHonors(Int which)
{
	Int honors = getInt("Honors", 0);
	setInt("Honors", honors | which);
}

Int SkirmishBattleHonors::getHonors(void) const
{
	return getInt("Honors", 0);
}

void SkirmishBattleHonors::setEnduranceMedal(AsciiString mapName, Int difficulty, int numAIs)
{
	AsciiString key;
	key.format("%s_%d", mapName.str(), difficulty);
	setInt(key, numAIs);
}

Int SkirmishBattleHonors::getEnduranceMedal(AsciiString mapName, Int difficulty) const
{
	AsciiString key;
	key.format("%s_%d", mapName.str(), difficulty);
	return getInt(key, 0);
}

void SkirmishBattleHonors::setLastGeneral(AsciiString val)
{
	setAsciiString("LastHouse", val);
}

AsciiString SkirmishBattleHonors::getLastGeneral(void) const
{
	return getAsciiString("LastHouse", AsciiString::TheEmptyString);
}

void SkirmishBattleHonors::setNumGamesLoyal(Int val)
{
	setInt("LoyalGames", val);
}

Int SkirmishBattleHonors::getNumGamesLoyal(void) const
{
	return getInt("LoyalGames", 0);
}

void SkirmishBattleHonors::setUSACampaignComplete(GameDifficulty difficulty)
{
	AsciiString key;
	key.format("USACampaign_%d", static_cast<int>(difficulty));
	setInt(key, 1);
}

Bool SkirmishBattleHonors::getUSACampaignComplete(GameDifficulty difficulty) const
{
	AsciiString key;
	key.format("USACampaign_%d", static_cast<int>(difficulty));
	return (getInt(key, 0) != 0);
}

void SkirmishBattleHonors::setCHINACampaignComplete(GameDifficulty difficulty)
{
	AsciiString key;
	key.format("CHINACampaign_%d", static_cast<int>(difficulty));
	setInt(key, 1);
}

Bool SkirmishBattleHonors::getCHINACampaignComplete(GameDifficulty difficulty) const
{
	AsciiString key;
	key.format("CHINACampaign_%d",static_cast<int>(difficulty));
	return (getInt(key, 0) != 0);
}

void SkirmishBattleHonors::setGLACampaignComplete(GameDifficulty difficulty)
{
	AsciiString key;
	key.format("GLACampaign_%d", static_cast<int>(difficulty));
	setInt(key, 1);
}

Bool SkirmishBattleHonors::getGLACampaignComplete(GameDifficulty difficulty) const
{
	AsciiString key;
	key.format("GLACampaign_%d", static_cast<int>(difficulty));
	return (getInt(key, 0) != 0);
}

void SkirmishBattleHonors::setChallengeCampaignComplete(Int challenge, GameDifficulty difficulty)
{
	AsciiString key;
	key.format("ChallengeCampaign%d_%d", challenge, static_cast<int>(difficulty));
	setInt(key, 1);
}

Bool SkirmishBattleHonors::getChallengeCampaignComplete(Int challenge, GameDifficulty difficulty) const
{
	AsciiString key;
	key.format("ChallengeCampaign%d_%d", challenge, static_cast<int>(difficulty));
	return (getInt(key, 0) != 0);
}
