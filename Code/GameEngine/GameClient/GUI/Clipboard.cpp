/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/Clipboard.h"

#include <cstring>

extern HWND ApplicationHWnd;

Bool ClipboardGetText( UnicodeString &text )
{
	text.clear();
	if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(ApplicationHWnd))
		return FALSE;
	HANDLE data = GetClipboardData(CF_UNICODETEXT);
	const WideChar *chars = data ? static_cast<const WideChar *>(GlobalLock(data)) : nullptr;
	if (chars != nullptr)
	{
		// bounded: nothing typed into the game is longer
		constexpr size_t MAX_CHARS = 1024;
		const size_t size = GlobalSize(data) / sizeof(WideChar);
		for (size_t i = 0; i < size && i < MAX_CHARS && chars[i] != 0; ++i)
			text.concat(chars[i]);
		GlobalUnlock(data);
	}
	CloseClipboard();
	return !text.isEmpty();
}

Bool ClipboardSetText( const UnicodeString &text )
{
	// the clipboard needs an owner window, or SetClipboardData fails after EmptyClipboard
	if (ApplicationHWnd == nullptr || !OpenClipboard(ApplicationHWnd))
		return FALSE;
	Bool ok = FALSE;
	const size_t bytes = (static_cast<size_t>(text.getLength()) + 1) * sizeof(WideChar);
	HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (memory != nullptr && EmptyClipboard())
	{
		void *target = GlobalLock(memory);
		if (target != nullptr)
		{
			std::memcpy(target, text.str(), bytes);
			GlobalUnlock(memory);
			ok = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
		}
	}
	if (!ok && memory != nullptr)
		GlobalFree(memory);	// the clipboard owns it only after SetClipboardData succeeded
	CloseClipboard();
	return ok;
}

UnicodeString ClipboardFirstLine( const UnicodeString &text )
{
	UnicodeString line;
	for (const WideChar *c = text.str(); *c != 0 && *c != L'\r' && *c != L'\n'; ++c)
	{
		if (*c >= 32 || *c == L'\t')
			line.concat(*c == L'\t' ? L' ' : *c);
	}
	line.trim();
	return line;
}
