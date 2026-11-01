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

// FILE: ArmorTemplate.cpp ///////////////////////////////////////////////////////////////////////////////
// Author: Steven Johnson, November 2001
// Desc:   ArmorTemplate descriptions
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine


#include "Common/INI.h"
#include "Common/ThingFactory.h"
#include "GameLogic/Armor.h"
#include "GameLogic/Damage.h"

#include <algorithm>

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC DATA ////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
ArmorTemplate::ArmorTemplate()
{
	clear();
}

//-------------------------------------------------------------------------------------------------
void ArmorTemplate::clear()
{
	for (int i = 0; i < DAMAGE_NUM_TYPES; i++)
	{
		m_damageCoefficient[i] = 1.0f;
	}
}

//-------------------------------------------------------------------------------------------------
Real ArmorTemplate::adjustDamage(DamageType t, Real damage) const 
{ 
	if (t == DAMAGE_UNRESISTABLE)
		return damage;
	if (t == DAMAGE_SUBDUAL_UNRESISTABLE)
		return damage;

	damage *= m_damageCoefficient[t];

	if (damage < 0.0f)
		damage = 0.0f;
	
	return damage;
}

//-------------------------------------------------------------------------------------------Static
/*static*/ void ArmorTemplate::parseArmorCoefficients( INI* ini, void *instance, void* /* store */, const void* userData )
{
	ArmorTemplate* self = static_cast<ArmorTemplate*>(instance);

	const char* damageName = ini->getNextToken();
	Real pct = INI::scanPercentToReal(ini->getNextToken());

	if (stricmp(damageName, "Default") == 0)
	{
		for (Int i = 0; i < DAMAGE_NUM_TYPES; i++)
		{
			self->m_damageCoefficient[i] = pct;
		}
		return;
	}

	DamageType dt = static_cast<DamageType>(DamageTypeFlags::getSingleBitFromName(damageName));
	self->m_damageCoefficient[dt] = pct;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ArmorStore::ArmorStore()
{
	m_armorTemplates.clear();
}

//-------------------------------------------------------------------------------------------------
ArmorStore::~ArmorStore()
{
	m_armorTemplates.clear();
}

//-------------------------------------------------------------------------------------------------
const ArmorTemplate* ArmorStore::findArmorTemplate(AsciiString name) const
{
	NameKeyType namekey = TheNameKeyGenerator->nameToKey(name);
  ArmorTemplateMap::const_iterator it = m_armorTemplates.find(namekey);
  if (it == m_armorTemplates.end()) 
	{
		return nullptr;
	}
	else
	{
		return &(*it).second;
	}
}

//-------------------------------------------------------------------------------------------------
/*static */ void ArmorStore::parseArmorDefinition(INI *ini)
{
	static const FieldParse myFieldParse[] = 
	{
		{ "Armor", ArmorTemplate::parseArmorCoefficients, nullptr, 0 },
		{ nullptr, nullptr, nullptr, 0 }
	};

	const char *c = ini->getNextToken();
	NameKeyType key = TheNameKeyGenerator->nameToKey(c);
	if (ini->getLoadType() == INILoadType::INI_LOAD_CREATE_OVERRIDES)
		TheArmorStore->noteMapDefinition(key);
	ArmorTemplate& armorTmpl = TheArmorStore->m_armorTemplates[key];
	armorTmpl.clear();
	ini->initFromINI(&armorTmpl, myFieldParse);
}

//-------------------------------------------------------------------------------------------------
// Keeps what a map INI is about to change, once per armor and map.
void ArmorStore::noteMapDefinition(NameKeyType key)
{
	if (m_mapBackups.find(key) != m_mapBackups.end()
		|| std::find(m_mapAdded.begin(), m_mapAdded.end(), key) != m_mapAdded.end())
		return;
	ArmorTemplateMap::const_iterator it = m_armorTemplates.find(key);
	if (it != m_armorTemplates.end())
		m_mapBackups.emplace(key, it->second);
	else
		m_mapAdded.push_back(key);
}

//-------------------------------------------------------------------------------------------------
// Undoes the map INI at the end of the game. Objects keep pointers to armor templates, so a redefined
// armor gets its old value back in place; armors the map created are removed (their users are gone).
void ArmorStore::reset()
{
	for (ArmorTemplateMap::const_iterator it = m_mapBackups.begin(); it != m_mapBackups.end(); ++it)
		m_armorTemplates[it->first] = it->second;
	for (NameKeyType key : m_mapAdded)
		m_armorTemplates.erase(key);
	m_mapBackups.clear();
	m_mapAdded.clear();
}

//-------------------------------------------------------------------------------------------------
/*static*/ void INI::parseArmorDefinition(INI *ini)
{
	ArmorStore::parseArmorDefinition(ini);
}

