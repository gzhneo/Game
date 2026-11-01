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

// FILE: KeyDefs.h ////////////////////////////////////////////////////////////
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
// File name:  KeyDefs.h
//
// Created:    Mike Morrison, 1995
//						 Colin Day, June 2001
//
// Desc:       Basic keyboard key definitions.
//
/** @todo NOTE: These key definitions are currently tied directly to the
*		Direct Input key codes, therefore making these definitions device 
*		dependent even though this code lives on the device INdependent side
*		of the engine.  In the future to be truly device independent we 
*		need to define our own key codes, and have a translation between 
*		what we read from the device to our own system*/
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __KEYDEFS_H_
#define __KEYDEFS_H_

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include <stdlib.h>
// The key codes are the DirectInput values (DIK_*, set 1 scan code, extended keys with 0x80),
// written as numbers in KeyDefType with the DIK name in the comment. This keeps CommandMap.ini,
// key bindings and replays compatible; UnitTests checks every value against dinput.h
// (testKeyDefsGegenDInput). The mapping Windows scan code -> key code is in
// GameClient/KeyScanCode.h.

// USER INCLUDES //////////////////////////////////////////////////////////////

// FORWARD REFERENCES /////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// TYPE DEFINES ///////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

//=============================================================================
/** The key tables */
//=============================================================================

enum KeyDefType
{
	// keypad keys ---------------------------------------------------------------- 
	KEY_KP0 								= 0x52,	// DIK_NUMPAD0
	KEY_KP1 								= 0x4F,	// DIK_NUMPAD1
	KEY_KP2 								= 0x50,	// DIK_NUMPAD2
	KEY_KP3 								= 0x51,	// DIK_NUMPAD3
	KEY_KP4 								= 0x4B,	// DIK_NUMPAD4
	KEY_KP5 								= 0x4C,	// DIK_NUMPAD5
	KEY_KP6 								= 0x4D,	// DIK_NUMPAD6
	KEY_KP7 								= 0x47,	// DIK_NUMPAD7
	KEY_KP8 								= 0x48,	// DIK_NUMPAD8
	KEY_KP9 								= 0x49,	// DIK_NUMPAD9
	KEY_KPDEL 							= 0x53,	// DIK_NUMPADPERIOD
	KEY_KPSTAR 							= 0x37,	// DIK_NUMPADSTAR
	KEY_KPMINUS 						= 0x4A,	// DIK_NUMPADMINUS
	KEY_KPPLUS 							= 0x4E,	// DIK_NUMPADPLUS

	// regular keys ---------------------------------------------------------------
	KEY_ESC 								= 0x01,	// DIK_ESCAPE
	KEY_BACKSPACE 					= 0x0E,	// DIK_BACK
	KEY_ENTER 							= 0x1C,	// DIK_RETURN
	KEY_SPACE 							= 0x39,	// DIK_SPACE
	KEY_TAB 								= 0x0F,	// DIK_TAB
	KEY_F1 									= 0x3B,	// DIK_F1
	KEY_F2 									= 0x3C,	// DIK_F2
	KEY_F3 									= 0x3D,	// DIK_F3
	KEY_F4 									= 0x3E,	// DIK_F4
	KEY_F5 									= 0x3F,	// DIK_F5
	KEY_F6 									= 0x40,	// DIK_F6
	KEY_F7 									= 0x41,	// DIK_F7
	KEY_F8 									= 0x42,	// DIK_F8
	KEY_F9 									= 0x43,	// DIK_F9
	KEY_F10 								= 0x44,	// DIK_F10
	KEY_F11 								= 0x57,	// DIK_F11
	KEY_F12 								= 0x58,	// DIK_F12
	KEY_A 									= 0x1E,	// DIK_A
	KEY_B 									= 0x30,	// DIK_B
	KEY_C 									= 0x2E,	// DIK_C
	KEY_D 									= 0x20,	// DIK_D
	KEY_E 									= 0x12,	// DIK_E
	KEY_F 									= 0x21,	// DIK_F
	KEY_G 									= 0x22,	// DIK_G
	KEY_H 									= 0x23,	// DIK_H
	KEY_I 									= 0x17,	// DIK_I
	KEY_J 									= 0x24,	// DIK_J
	KEY_K 									= 0x25,	// DIK_K
	KEY_L 									= 0x26,	// DIK_L
	KEY_M 									= 0x32,	// DIK_M
	KEY_N 									= 0x31,	// DIK_N
	KEY_O 									= 0x18,	// DIK_O
	KEY_P 									= 0x19,	// DIK_P
	KEY_Q 									= 0x10,	// DIK_Q
	KEY_R 									= 0x13,	// DIK_R
	KEY_S 									= 0x1F,	// DIK_S
	KEY_T 									= 0x14,	// DIK_T
	KEY_U 									= 0x16,	// DIK_U
	KEY_V 									= 0x2F,	// DIK_V
	KEY_W 									= 0x11,	// DIK_W
	KEY_X 									= 0x2D,	// DIK_X
	KEY_Y 									= 0x15,	// DIK_Y
	KEY_Z 									= 0x2C,	// DIK_Z
	KEY_1 									= 0x02,	// DIK_1
	KEY_2 									= 0x03,	// DIK_2
	KEY_3 									= 0x04,	// DIK_3
	KEY_4 									= 0x05,	// DIK_4
	KEY_5 									= 0x06,	// DIK_5
	KEY_6 									= 0x07,	// DIK_6
	KEY_7 									= 0x08,	// DIK_7
	KEY_8 									= 0x09,	// DIK_8
	KEY_9 									= 0x0A,	// DIK_9
	KEY_0 									= 0x0B,	// DIK_0
	KEY_MINUS 							= 0x0C,	// DIK_MINUS
	KEY_EQUAL 							= 0x0D,	// DIK_EQUALS
	KEY_LBRACKET 						= 0x1A,	// DIK_LBRACKET
	KEY_RBRACKET 						= 0x1B,	// DIK_RBRACKET
	KEY_SEMICOLON 					= 0x27,	// DIK_SEMICOLON
	KEY_APOSTROPHE 					= 0x28,	// DIK_APOSTROPHE
	KEY_TICK 								= 0x29,	// DIK_GRAVE
	KEY_BACKSLASH 					= 0x2B,	// DIK_BACKSLASH
	KEY_COMMA 							= 0x33,	// DIK_COMMA
	KEY_PERIOD 							= 0x34,	// DIK_PERIOD
	KEY_SLASH 							= 0x35,	// DIK_SLASH

	// special keys ---------------------------------------------------------------
	KEY_SYSREQ 							= 0xB7,	// DIK_SYSRQ

	KEY_CAPS 								= 0x3A,	// DIK_CAPSLOCK
	KEY_NUM 								= 0x45,	// DIK_NUMLOCK
	KEY_SCROLL 							= 0x46,	// DIK_SCROLL
	KEY_LCTRL 							= 0x1D,	// DIK_LCONTROL
	KEY_LALT 								= 0x38,	// DIK_LALT
	KEY_LSHIFT 							= 0x2A,	// DIK_LSHIFT
	KEY_RSHIFT 							= 0x36,	// DIK_RSHIFT

	KEY_UP 									= 0xC8,	// DIK_UPARROW
	KEY_DOWN 								= 0xD0,	// DIK_DOWNARROW
	KEY_LEFT 								= 0xCB,	// DIK_LEFTARROW
	KEY_RIGHT 							= 0xCD,	// DIK_RIGHTARROW
	KEY_RALT 								= 0xB8,	// DIK_RALT
	KEY_RCTRL 							= 0x9D,	// DIK_RCONTROL
	KEY_HOME 								= 0xC7,	// DIK_HOME
	KEY_END 								= 0xCF,	// DIK_END
	KEY_PGUP 								= 0xC9,	// DIK_PGUP
	KEY_PGDN 								= 0xD1,	// DIK_PGDN
	KEY_INS 								= 0xD2,	// DIK_INSERT
	KEY_DEL 								= 0xD3,	// DIK_DELETE
	KEY_KPENTER 						= 0x9C,	// DIK_NUMPADENTER
	KEY_KPSLASH 						= 0xB5,	// DIK_NUMPADSLASH

	KEY_102 								= 0x56,	// DIK_OEM_102

	// Japanese keyboard keys -----------------------------------------------------
	KEY_KANA 								= 0x70,	// DIK_KANA
	KEY_CONVERT 						= 0x79,	// DIK_CONVERT
	KEY_NOCONVERT 					= 0x7B,	// DIK_NOCONVERT
	KEY_YEN 								= 0x7D,	// DIK_YEN
	KEY_CIRCUMFLEX 					= 0x90,	// DIK_CIRCUMFLEX
	KEY_KANJI 							= 0x94,	// DIK_KANJI

	// specials -------------------------------------------------------------------
	KEY_NONE								= 0x00,		///< to report end of key stream
	KEY_LOST								= 0xFF		///< to report lost keyboard focus

};	// end KeyDefType

// state for keyboard IO ------------------------------------------------------
enum
{
	KEY_STATE_NONE								= 0x0000, // No modifier state
	KEY_STATE_UP									= 0x0001,	// Key is up (default state)
	KEY_STATE_DOWN								= 0x0002,	// Key is down
	KEY_STATE_LCONTROL						= 0x0004,	// Left control is pressed
	KEY_STATE_RCONTROL						= 0x0008,	// Right control is pressed
	KEY_STATE_LSHIFT							= 0x0010,	// left shift is pressed
	KEY_STATE_RSHIFT							= 0x0020,	// right shift is pressed
	KEY_STATE_LALT								= 0x0040,	// left alt is pressed
	KEY_STATE_RALT								= 0x0080,	// right alt is pressed
	KEY_STATE_AUTOREPEAT					= 0x0100,	// Key is down due to autorepeat (only seen in conjunction with KEY_STATE_DOWN)
	KEY_STATE_CAPSLOCK						= 0x0200, // Caps Lock key is on.
	KEY_STATE_SHIFT2							= 0x0400, // Alternate shift key is pressed (I think this is for foreign keyboards..)

	// modifier combinations when left/right isn't a factor
	KEY_STATE_CONTROL		= (KEY_STATE_LCONTROL | KEY_STATE_RCONTROL),
	KEY_STATE_SHIFT			= (KEY_STATE_LSHIFT | KEY_STATE_RSHIFT | KEY_STATE_SHIFT2 ),
	KEY_STATE_ALT				= (KEY_STATE_LALT | KEY_STATE_RALT)

};	// end KeyStateType

// INLINING ///////////////////////////////////////////////////////////////////

// EXTERNALS //////////////////////////////////////////////////////////////////

#endif // __KEYDEFS_H_
