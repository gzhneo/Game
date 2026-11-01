/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Win32Keyboard.h -- keyboard input from Win32 keyboard messages.
//
// The window procedure passes WM_KEYDOWN/WM_KEYUP/WM_SYSKEYDOWN/WM_SYSKEYUP to addWin32Event(); the
// class maps the scan code to the engine's DIK key code via KeyScan::keyFromScanCode() and buffers
// the events until the next Keyboard::update(), which fetches them via getKey().
// Key repeat is done by the engine (Keyboard::checkKeyRepeat), so Windows repeats are dropped.
// Text input (IME) goes through WM_CHAR/WM_IME_* in the IMEManager.

#pragma once

#ifndef __WIN32KEYBOARD_H_
#define __WIN32KEYBOARD_H_

#include <windows.h>

#include "GameClient/Keyboard.h"

//-----------------------------------------------------------------------------
class Win32Keyboard : public Keyboard
{

public:

	Win32Keyboard( void );
	virtual ~Win32Keyboard( void ) override;

	virtual void init( void ) override;
	virtual void reset( void ) override;
	virtual void update( void ) override;
	virtual Bool getCapsState( void ) override;

	/// from the window procedure: WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN, WM_SYSKEYUP
	void addWin32Event( UINT msg, WPARAM wParam, LPARAM lParam );

	/** Focus lost or regained: queue a release for every key still down so none gets stuck
		* (the key-up message goes to another window). */
	void releaseAllKeys( void );

protected:

	virtual void getKey( KeyboardIO *key ) override;

private:

	void pushEvent( UnsignedByte key, Bool down );

	enum { EVENT_BUFFER_SIZE = 256 };

	struct KeyEvent
	{
		UnsignedByte key;
		Bool down;
		UnsignedInt sequence;
	};

	KeyEvent m_events[ EVENT_BUFFER_SIZE ];	///< ring buffer until the next update()
	Int m_eventRead;
	Int m_eventCount;
	UnsignedInt m_nextSequence;
	Bool m_isDown[ 256 ];									///< state per key code as reported to the game

};

/// for the window procedure (like TheWin32Mouse); set by the constructor, cleared by the destructor
#include "Engine/EngineContext.h"

#endif // __WIN32KEYBOARD_H_
