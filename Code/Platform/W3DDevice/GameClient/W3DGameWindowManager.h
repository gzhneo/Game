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

// FILE: W3DGameWindowManager.h ///////////////////////////////////////////////////////////////////
// Created:    Colin Day, June 2001
// Desc:			 W3D implementation specific parts for the game window manager,
//						 which controls all access to the game windows for the in and
//						 of game GUI controls and windows
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __W3DGAMEWINDOWMANAGER_H_
#define __W3DGAMEWINDOWMANAGER_H_

#include "GameClient/GameWindowManager.h"
#include "W3DDevice/GameClient/W3DGameWindow.h"
#include "W3DDevice/GameClient/W3DGadget.h"

//-------------------------------------------------------------------------------------------------
/** W3D implementation of the game window manager which controls all windows
	* and user interface controls */
//-------------------------------------------------------------------------------------------------
class W3DGameWindowManager : public GameWindowManager
{

public:

	W3DGameWindowManager( void );
	virtual ~W3DGameWindowManager( void ) override;

	virtual void init( void ) override;  ///< initialize the singlegon

	virtual GameWindow *allocateNewWindow( void ) override;  ///< allocate a new game window
	virtual GameWinDrawFunc getDefaultDraw( void ) override;  ///< return default draw func to use

	virtual GameWinDrawFunc getPushButtonImageDrawFunc( void ) override;
	virtual GameWinDrawFunc getPushButtonDrawFunc( void ) override;
	virtual GameWinDrawFunc getCheckBoxImageDrawFunc( void ) override;
	virtual GameWinDrawFunc getCheckBoxDrawFunc( void ) override;
	virtual GameWinDrawFunc getRadioButtonImageDrawFunc( void ) override;
	virtual GameWinDrawFunc getRadioButtonDrawFunc( void ) override;
	virtual GameWinDrawFunc getTabControlImageDrawFunc( void ) override;
	virtual GameWinDrawFunc getTabControlDrawFunc( void ) override;
	virtual GameWinDrawFunc getListBoxImageDrawFunc( void ) override;
	virtual GameWinDrawFunc getListBoxDrawFunc( void ) override;
	virtual GameWinDrawFunc getComboBoxImageDrawFunc( void ) override;
	virtual GameWinDrawFunc getComboBoxDrawFunc( void ) override;
	virtual GameWinDrawFunc getHorizontalSliderImageDrawFunc( void ) override;
	virtual GameWinDrawFunc getHorizontalSliderDrawFunc( void ) override;
	virtual GameWinDrawFunc getVerticalSliderImageDrawFunc( void ) override;
	virtual GameWinDrawFunc getVerticalSliderDrawFunc( void ) override;
	virtual GameWinDrawFunc getProgressBarImageDrawFunc( void ) override;
	virtual GameWinDrawFunc getProgressBarDrawFunc( void ) override;
	virtual GameWinDrawFunc getStaticTextImageDrawFunc( void ) override;
	virtual GameWinDrawFunc getStaticTextDrawFunc( void ) override;
	virtual GameWinDrawFunc getTextEntryImageDrawFunc( void ) override;
	virtual GameWinDrawFunc getTextEntryDrawFunc( void ) override;

protected:

};  // end class W3DGameWindowManager

// INLINE //////////////////////////////////////////////////////////////////////////////////////////
inline GameWindow *W3DGameWindowManager::allocateNewWindow( void ) { return newInstance(W3DGameWindow); }
inline GameWinDrawFunc W3DGameWindowManager::getDefaultDraw( void ) { return W3DGameWinDefaultDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getPushButtonImageDrawFunc( void ) { return W3DGadgetPushButtonImageDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getPushButtonDrawFunc( void ) { return W3DGadgetPushButtonDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getCheckBoxImageDrawFunc( void ) { return W3DGadgetCheckBoxImageDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getCheckBoxDrawFunc( void ) { return W3DGadgetCheckBoxDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getRadioButtonImageDrawFunc( void ) { return W3DGadgetRadioButtonImageDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getRadioButtonDrawFunc( void ) { return W3DGadgetRadioButtonDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getTabControlImageDrawFunc( void ) { return W3DGadgetTabControlImageDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getTabControlDrawFunc( void ) { return W3DGadgetTabControlDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getListBoxImageDrawFunc( void ) { return W3DGadgetListBoxImageDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getListBoxDrawFunc( void ) { return W3DGadgetListBoxDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getComboBoxImageDrawFunc( void ) { return W3DGadgetComboBoxImageDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getComboBoxDrawFunc( void ) { return W3DGadgetComboBoxDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getHorizontalSliderImageDrawFunc( void ) { return W3DGadgetHorizontalSliderImageDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getHorizontalSliderDrawFunc( void ) { return W3DGadgetHorizontalSliderDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getVerticalSliderImageDrawFunc( void ) { return W3DGadgetVerticalSliderImageDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getVerticalSliderDrawFunc( void ) { return W3DGadgetVerticalSliderDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getProgressBarImageDrawFunc( void ) { return W3DGadgetProgressBarImageDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getProgressBarDrawFunc( void ) { return W3DGadgetProgressBarDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getStaticTextImageDrawFunc( void ) { return W3DGadgetStaticTextImageDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getStaticTextDrawFunc( void ) { return W3DGadgetStaticTextDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getTextEntryImageDrawFunc( void ) { return W3DGadgetTextEntryImageDraw; }
inline GameWinDrawFunc W3DGameWindowManager::getTextEntryDrawFunc( void ) { return W3DGadgetTextEntryDraw; }

#endif // __W3DGAMEWINDOWMANAGER_H_

