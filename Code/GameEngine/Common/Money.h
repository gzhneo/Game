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

// FILE: Money.h ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information					         
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:    RTS3
//
// File name:  Money.h
//
// Created:    Steven Johnson, October 2001
//
// Desc:			 @todo
//
//-----------------------------------------------------------------------------

#pragma once

#ifndef _MONEY_H_
#define _MONEY_H_

#include "Lib/BaseType.h"
#include "Common/Debug.h"
#include "Common/Snapshot.h"

// ----------------------------------------------------------------------------------------------
/**
	How much "money" (Tiberium, Gems, Magic Resource Boxes, whatever) the Player has.
	This is currently a Very Simple Class but is encapsulated
	in anticipation of future expansion.
*/
class Money : public Snapshot
{

public:

	inline Money() : m_money(0), m_playerIndex(0), m_frozen(FALSE)
	{
	}

	void init()
	{
		m_money = 0;
		m_frozen = FALSE;
	}

	inline UnsignedInt countMoney() const 
	{ 
		return m_money; 
	}

	/// returns the actual amount withdrawn, which may be less than you want. (sorry, can't go into debt...)
	UnsignedInt withdraw(UnsignedInt amountToWithdraw, Bool playSound = TRUE);
	void deposit(UnsignedInt amountToDeposit, Bool playSound = TRUE);

	void setPlayerIndex(Int ndx) { m_playerIndex = ndx; }

	/// Test aid: frozen money, withdraw() deducts nothing. Set by the player (Player::setTestAid);
	/// not in xfer, the save game keeps it in Player.
	void setFrozen(Bool frozen) { m_frozen = frozen; }
	Bool isFrozen() const { return m_frozen; }
	
  static void parseMoneyAmount( INI *ini, void *instance, void *store, const void* userData );

  // Does the amount of this == the amount of that (compare everything except m_playerIndex)
  Bool amountEqual( const Money & that ) const
  {
    return m_money == that.m_money;
  }

protected:
	// snapshot methods
	virtual void crc( Xfer *xfer ) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess( void ) override;

private:

	UnsignedInt m_money;	///< amount of money
	Int m_playerIndex;	///< what is my player index?
	Bool m_frozen;	///< Test aid: spending deducts nothing
};

#endif // _MONEY_H_

