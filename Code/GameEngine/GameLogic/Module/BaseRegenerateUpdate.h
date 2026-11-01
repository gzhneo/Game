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

// FILE: BaseRegenerateUpdate.h ///////////////////////////////////////////////////////////////////
// Author: Colin Day, July 2002
// Desc:   Update module for base objects automatically regenerating health
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __BASE_REGENERATE_UPDATE_H_
#define __BASE_REGENERATE_UPDATE_H_

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/DamageModule.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class Thing;

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class BaseRegenerateUpdateModuleData : public UpdateModuleData
{

public:
	BaseRegenerateUpdateModuleData( void );
	static void buildFieldParse( MultiIniFieldParse &p );

};

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class BaseRegenerateUpdate : public UpdateModule,
												 public DamageModuleInterface
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( BaseRegenerateUpdate, "BaseRegenerateUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( BaseRegenerateUpdate, BaseRegenerateUpdateModuleData );

public:

	BaseRegenerateUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	static Int getInterfaceMask() { return UpdateModule::getInterfaceMask() | MODULEINTERFACE_DAMAGE; }

	// BehaviorModule
	virtual DamageModuleInterface* getDamage() override { return this; }

	// UpdateModuleInterface
	virtual UpdateSleepTime update( void ) override;

	// DamageModuleInterface
	virtual void onDamage( DamageInfo *damageInfo ) override;
	virtual void onHealing( DamageInfo *damageInfo ) override { }
	virtual void onBodyDamageStateChange(const DamageInfo* damageInfo, BodyDamageType oldState, BodyDamageType newState) override { }
	virtual DisabledMaskType getDisabledTypesToProcess() const override { return MAKE_DISABLED_MASK( DISABLED_UNDERPOWERED ); }

private:

};

#endif  // end __BASE_REGENERATE_UPDATE_H_
