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

// FILE: RiderChangeContain.cpp //////////////////////////////////////////////////////////////////////
// Author: Kris Morness, May 2003
// Desc:   Contain module for the combat bike (transport that switches units).
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __RIDER_CHANGE_CONTAIN_H
#define __RIDER_CHANGE_CONTAIN_H

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/TransportContain.h"

constexpr auto MAX_RIDERS = 8; //***NOTE: If you change this, make sure you update the parsing section!

enum WeaponSetType : int;	// must match the underlying type of the definition
enum ObjectStatusType : int;
enum LocomotorSetType : int;	// must match the underlying type of the definition

struct RiderInfo
{
	AsciiString m_templateName;
	WeaponSetType m_weaponSetFlag;
	ModelConditionFlagType m_modelConditionFlagType; 
	ObjectStatusType m_objectStatusType;
	AsciiString m_commandSet;
	LocomotorSetType m_locomotorSetType;
};

//-------------------------------------------------------------------------------------------------
class RiderChangeContainModuleData : public TransportContainModuleData
{
public:
	
	RiderInfo m_riders[ MAX_RIDERS ];
	UnsignedInt m_scuttleFrames;
	ModelConditionFlagType m_scuttleState;

	RiderChangeContainModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);
	static void parseRiderInfo( INI* ini, void *instance, void *store, const void* /*userData*/ );

};

//-------------------------------------------------------------------------------------------------
class RiderChangeContain : public TransportContain
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( RiderChangeContain, "RiderChangeContain" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( RiderChangeContain, RiderChangeContainModuleData )

public:

	RiderChangeContain( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual Bool isValidContainerFor( const Object* obj, Bool checkCapacity) const override;

	virtual void onCapture( Player *oldOwner, Player *newOwner ) override; // have to kick everyone out on capture.
	virtual void onContaining( Object *obj, Bool wasSelected ) override;		///< object now contains 'obj'
	virtual void onRemoving( Object *obj ) override;			///< object no longer contains 'obj'
	virtual UpdateSleepTime update() override;							///< called once per frame

	virtual Bool isRiderChangeContain() const override { return TRUE; }
	virtual const Object *friend_getRider() const override; 

	virtual Int getContainMax( void ) const override;

	virtual Int getExtraSlotsInUse( void ) override { return m_extraSlotsInUse; }///< Transports have the ability to carry guys how take up more than spot.

	virtual Bool isExitBusy() const override;	///< Contain style exiters are getting the ability to space out exits, so ask this before reserveDoor as a kind of no-commitment check.
	virtual ExitDoorType reserveDoorForExit( const ThingTemplate* objType, Object *specificObject ) override;
	virtual void unreserveDoorForExit( ExitDoorType exitDoor ) override;
	virtual Bool isDisplayedOnControlBar() const override {return TRUE;}///< Does this container display its contents on the ControlBar?

	virtual Bool getContainerPipsToShow( Int& numTotal, Int& numFull ) override;

protected:

	// exists primarily for RiderChangeContain to override
	virtual void killRidersWhoAreNotFreeToExit() override;
	virtual Bool isSpecificRiderFreeToExit(Object* obj) override;
	virtual void createPayload() override;
	
private:

	Int m_extraSlotsInUse;
	UnsignedInt m_frameExitNotBusy;
	UnsignedInt m_scuttledOnFrame;

	Bool m_containing; //doesn't require xfer.

};

#endif // __RIDER_CHANGE_CONTAIN_H

