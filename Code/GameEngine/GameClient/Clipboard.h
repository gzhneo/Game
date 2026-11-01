/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Clipboard.h -- text from and to the system clipboard, so that addresses can be passed on through
// a chat program.

#pragma once

#include "Common/UnicodeString.h"

/// The text on the clipboard; false if there is none.
Bool ClipboardGetText( UnicodeString &text );

/// Puts `text` on the clipboard; false if the clipboard is busy.
Bool ClipboardSetText( const UnicodeString &text );

/// The first line of clipboard text as typed input: up to the first line break, without control
/// characters and without the spaces around it.
UnicodeString ClipboardFirstLine( const UnicodeString &text );
