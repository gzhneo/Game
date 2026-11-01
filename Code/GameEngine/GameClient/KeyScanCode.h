/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// KeyScanCode.h -- maps keyboard scan codes to internal key codes.
//
// The internal key codes (KeyDefType in KeyDefs.h) are the DirectInput DIK_* values: set 1 scan
// codes, extended keys (prefix E0) with bit 0x80 set. Keeping them keeps CommandMap.ini, key
// bindings, replays and test helpers compatible.
//
// Windows passes the scan code (bits 16..23) and the extended bit (bit 24) in lParam of
// WM_KEYDOWN/WM_KEYUP (and the WM_SYS* counterparts). The mapping is almost always
// "scancode | (extended ? 0x80 : 0)"; the exceptions are listed below. The function needs no
// Windows header so EngineTests can check it natively (testKeyScanCode).

#pragma once

#ifndef __KEYSCANCODE_H_
#define __KEYSCANCODE_H_

namespace KeyScan
{

// Scan codes that need special handling (set 1).
enum : unsigned int
{
	SC_LSHIFT				= 0x2A,
	SC_RSHIFT				= 0x36,
	SC_PAUSE_NUMLOCK	= 0x45,	///< without E0: Pause (E1 1D 45), with the E0 bit from Windows: Num Lock
	SC_SCROLL_BREAK		= 0x46,	///< without E0: Scroll Lock, with E0: Ctrl+Pause (Break)
	SC_ALT_PRINT			= 0x54	///< Alt+Print Screen gives scan code 0x54 instead of E0 37
};

/** Returns the internal key code (DIK value) for a scan code from WM_KEYDOWN/WM_KEYUP, or 0
	* (KEY_NONE) if the key is to be dropped. */
inline unsigned char keyFromScanCode( unsigned int scanCode, bool extended )
{
	scanCode &= 0xFF;

	// 0 = no scan code (e.g. a synthesized message), 0x80 and up would be release codes; neither is a
	// valid key in WM_KEY*.
	if( scanCode == 0 || scanCode >= 0x80 )
		return 0;

	// The keyboard sends fake shift keys E0 2A / E0 36 before some extended keys (arrows, Home, ...
	// with Num Lock on). DirectInput dropped them; so do we.
	if( extended && ( scanCode == SC_LSHIFT || scanCode == SC_RSHIFT ) )
		return 0;

	// Num Lock and Pause share scan code 0x45. Windows reports Num Lock with the extended bit and
	// Pause without; DirectInput has it the other way round (DIK_NUMLOCK 0x45, DIK_PAUSE 0xC5).
	if( scanCode == SC_PAUSE_NUMLOCK )
		return extended ? 0x45 : 0xC5;

	// Ctrl+Pause arrives as "Break" (E0 46); DirectInput reported DIK_PAUSE for it.
	if( scanCode == SC_SCROLL_BREAK && extended )
		return 0xC5;

	// Alt+Print Screen: own scan code 0x54, DirectInput reported DIK_SYSRQ.
	if( scanCode == SC_ALT_PRINT )
		return 0xB7;

	// E0 7F would give 0xFF = KEY_LOST (keyboard lost), not a real key; drop it.
	if( extended && scanCode == 0x7F )
		return 0;

	return static_cast<unsigned char>( extended ? ( scanCode | 0x80 ) : scanCode );
}

}  // namespace KeyScan

#endif // __KEYSCANCODE_H_
