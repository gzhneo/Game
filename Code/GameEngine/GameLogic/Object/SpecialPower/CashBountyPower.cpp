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

// FILE: CashBountyPower.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Aug 2002
//
//	Filename: 	CashBountyPower.cpp
//
//	author:		Steven Johnson
//	
//	purpose:
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Player.h"
#include "Common/Xfer.h"
#include "GameLogic/Module/CashBountyPower.h"
#include "GameLogic/Object.h"

//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CashBountyPowerModuleData::CashBountyPowerModuleData()
{
	m_defaultBounty = 0.0;
} 

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/* static */ void CashBountyPowerModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	SpecialPowerModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] = 
	{
		{ "Bounty",			INI::parsePercentToReal, nullptr, offsetof( CashBountyPowerModuleData, m_defaultBounty ) },
		{ 0, 0, 0, 0 } 
	};
	p.add(dataFieldParse);

}  // end buildFieldParse

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CashBountyPower::CashBountyPower( Thing *thing, const ModuleData* moduleData ) : 
							SpecialPowerModule( thing, moduleData )
{
}  // end CashBountyPower

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CashBountyPower::~CashBountyPower()
{

}  // end ~CashBountyPower

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void CashBountyPower::onObjectCreated()
{
	Player* controller = getObject()->getControllingPlayer();
	if (controller && controller->hasScience(getRequiredScience()))
	{
		Real bounty = findBounty();
		if (bounty > controller->getCashBounty())
			controller->setCashBounty(bounty);
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Real CashBountyPower::findBounty() const
{
	const CashBountyPowerModuleData* d = getCashBountyPowerModuleData();
	return d->m_defaultBounty;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void CashBountyPower::onSpecialPowerCreation()
{
	SpecialPowerModule::onSpecialPowerCreation();

	Player* controller = getObject()->getControllingPlayer();
	if (controller)
	{
		Real bounty = findBounty();
		if (bounty > controller->getCashBounty())
			controller->setCashBounty(bounty);
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void CashBountyPower::crc( Xfer *xfer )
{

	// extend base class
	SpecialPowerModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void CashBountyPower::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	SpecialPowerModule::xfer( xfer );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void CashBountyPower::loadPostProcess( void )
{

	// extend base class
	SpecialPowerModule::loadPostProcess();

}  // end loadPostProcess
