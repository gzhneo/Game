/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// KeyLayout.h -- which characters of the active keyboard layout may replace the built-in key name
// tables. The tables follow the game language; the layout follows the keycaps the player sees. Key
// names only label keys (hotkey letters, jumping in list boxes); bindings stay on scan codes.

#pragma once

#ifndef ENGINE_KEYLAYOUT_H
#define ENGINE_KEYLAYOUT_H

namespace Engine
{

/// Scan codes (set 1, not extended) of the typing block: digit row, the three letter rows and the
/// extra key next to left shift. Keypad, function and control keys keep their table entries.
inline bool isLayoutLabelKey(unsigned int scanCode)
{
	return (scanCode >= 0x02 && scanCode <= 0x0D)		// 1 .. =
		|| (scanCode >= 0x10 && scanCode <= 0x1B)		// Q .. ]
		|| (scanCode >= 0x1E && scanCode <= 0x29)		// A .. `
		|| (scanCode >= 0x2B && scanCode <= 0x35)		// \ .. /
		|| scanCode == 0x56;							// < > on ISO keyboards
}

/// Can a layout character label a key? Latin letters, digits, punctuation and currency signs can.
/// Other scripts (Cyrillic, Greek, CJK) cannot: hotkeys in the string tables are Latin letters, so
/// such a layout keeps the table entry for that key.
inline bool isUsableLayoutChar(unsigned int c)
{
	return (c >= 0x20 && c < 0x7F)					// ASCII
		|| (c >= 0xA0 && c <= 0x24F)				// Latin-1 supplement and Latin extended
		|| (c >= 0x2000 && c <= 0x20CF);			// general punctuation and currency (euro sign)
}

}	// namespace Engine

#endif	// ENGINE_KEYLAYOUT_H
