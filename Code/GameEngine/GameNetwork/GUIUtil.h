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

// FILE: GUIUtil.h //////////////////////////////////////////////////////
// Author: Matthew D. Campbell, Sept 2002

#pragma once

#ifndef __GUIUTIL_H__
#define __GUIUTIL_H__

class GameWindow;
class GameInfo;

void ShowUnderlyingGUIElements( Bool show, const char *layoutFilename, const char *parentName,
															 const char **gadgetsToHide, const char **perPlayerGadgetsToHide );

void PopulateColorComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool isObserver = FALSE);
void PopulatePlayerTemplateComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool allowObservers );
void PopulateTeamComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool isObserver = FALSE);
void PopulateStartingCashComboBox(GameWindow *comboBox, GameInfo *myGame);

// Start protection choice (none, 5, 10 minutes; item data = minutes). The layouts lack it: the menus
// create it next to an existing combo box, which gives the look.
GameWindow *CreateGraceComboBox(GameWindow *model, const char *decoratedName);
void PopulateGraceComboBox(GameWindow *comboBox, UnsignedShort minutes);
void SelectGraceComboBox(GameWindow *comboBox, UnsignedShort minutes);
UnsignedShort GetGraceComboBoxMinutes(GameWindow *comboBox);

// Zoom-out limit the host sets for every player (100 to 125 percent of the normal height; item data =
// percent). Created like the start protection choice.
GameWindow *CreateMaxZoomComboBox(GameWindow *model, const char *decoratedName);
void PopulateMaxZoomComboBox(GameWindow *comboBox, UnsignedShort percent);
void SelectMaxZoomComboBox(GameWindow *comboBox, UnsignedShort percent);
UnsignedShort GetMaxZoomComboBoxPercent(GameWindow *comboBox);

// The new AI in the player combo boxes: entries NEW_AI_COMBO_FIRST.. after Open, Closed, Easy, Medium and
// Brutal, one block of the difficulties (Easy, Medium, Brutal) per personality, standard first;
// item data = slot state | AI kind << 8 | personality << 16.
enum { NEW_AI_COMBO_FIRST = 5, NEW_AI_COMBO_DIFFICULTIES = 3 };
void AddNewAIComboEntries( GameWindow *comboBox, Color color );

void EnableSlotListUpdates( Bool val );
Bool AreSlotListUpdatesEnabled( void );

void UpdateSlotList( GameInfo *myGame, GameWindow *comboPlayer[],
										GameWindow *comboColor[], GameWindow *comboPlayerTemplate[],
										GameWindow *comboTeam[], GameWindow *buttonAccept[], 
										GameWindow *buttonStart, GameWindow *buttonMapStartPosition[] );

void EnableAcceptControls(Bool Enabled, GameInfo *myGame, GameWindow *comboPlayer[],
										GameWindow *comboColor[], GameWindow *comboPlayerTemplate[],
										GameWindow *comboTeam[], GameWindow *buttonAccept[], GameWindow *buttonStart,
										GameWindow *buttonMapStartPosition[], Int slotNum = -1);

#endif // __GUIUTIL_H__
