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

// FILE: CrateSystem.h /////////////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood Feb 2002
// Desc:   System responsible for Crates as code objects - ini, new/delete etc
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef CRATE_SYSTEM_H
#define CRATE_SYSTEM_H

#include "Common/INI.h"
#include "Common/Overridable.h"
#include "Common/Override.h"
#include "Common/PoolPtr.h"

enum ScienceType : int;	// must match the underlying type of the definition

struct crateCreationEntry
{
	AsciiString crateName;
	Real crateChance;
};

using crateCreationEntryList = std::list< crateCreationEntry >;
using crateCreationEntryIterator = std::list< crateCreationEntry >::iterator;
using crateCreationEntryConstIterator = std::list< crateCreationEntry >::const_iterator;

/** 
		A CrateTemplate is a ini defined set of conditions plus a ThingTemplate that is the Object
		containing the correct CrateCollide module.
*/
class CrateTemplate : public Overridable
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( CrateTemplate, "CrateTemplate"  )

public:
	CrateTemplate();
	// virtual destructor declared by memory pool

	void setName( AsciiString name ) { m_name = name; }
	AsciiString getName(){ return m_name; }

	inline const FieldParse *getFieldParse() const { return TheCrateTemplateFieldParseTable; }
	static const FieldParse TheCrateTemplateFieldParseTable[];		///< the parse table for INI definition

	static void parseCrateCreationEntry( INI* ini, void *instance, void *store, const void* /*userData*/ );

	AsciiString m_name;													///< name for this CrateTemplate

	Real m_creationChance;											///< Condition for random percentage chance of creating
	VeterancyLevel m_veterancyLevel;						///< Condition specifing level of killed unit
	KindOfMaskType m_killedByTypeKindof;				///< Must be killed by something with all these bits set
	ScienceType m_killerScience;								///< Must be killed by something posessing this science
	crateCreationEntryList m_possibleCrates;		///< CreationChance is for this CrateData to succeed, this list controls one-of-n crates created on success
	Bool m_isOwnedByMaker;											///< Design needs crates to be owned sometimes.

private:

};

using CrateTemplateOverride = OVERRIDE<CrateTemplate>;


/** 
		System responsible for Crates as code objects - ini, new/delete etc
*/
class CrateSystem : public SubsystemInterface
{
public:
	CrateSystem();
	~CrateSystem() override;

	void init() override;
	void reset() override;
	void update() override {}

	const CrateTemplate *findCrateTemplate(AsciiString name) const;
	CrateTemplate *friend_findCrateTemplate(AsciiString name);

	CrateTemplate *newCrateTemplate( AsciiString name );
	CrateTemplate *newCrateTemplateOverride( CrateTemplate *crateToOverride );

	

	static void parseCrateTemplateDefinition(INI* ini);

private:
	std::vector<PoolPtr<CrateTemplate>> m_crateTemplateVector;

};

#include "Engine/EngineContext.h"
#endif
