/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: TestScript.h /////////////////////////////////////////////////////////////////////////////
// Test script: drives the game without input through menus, games, campaign missions, save/load and
// up to exit. Meant for unattended tests, above all the leak check (tools/lecksuche/lecksuche.py).
//
// Usage: -testablauf <file>. Commands and file format: see the head of GameClient/TestScript.cpp.
// Without the switch this module does nothing.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __TESTSCRIPT_H_
#define __TESTSCRIPT_H_

/// Once per main loop pass (GameEngine::update), after the client and the message stream.
void TestScript_Update( void );

/// Free the command list (at engine shutdown, before the leak report).
void TestScript_Release( void );

#endif // __TESTSCRIPT_H_
