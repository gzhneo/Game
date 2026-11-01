/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WndFields.h -- field readers of the window layout (.wnd) parser that need no engine header, so
// EngineTests can feed them real layout lines. GameWindowManagerScript.cpp calls them.
//
// Layout files come from mods and from maps, so every reader stays inside the text it is given: a
// missing quote, value or number gives an empty or zero result, never a read past the end.

#pragma once

#ifndef ENGINE_WNDFIELDS_H
#define ENGINE_WNDFIELDS_H

#include <string>

namespace Engine
{

/// Reads the value part of a FONT field: `FONT = NAME: "Arial", SIZE: 14, BOLD: 1`. buffer is
/// modified (strtok). A field without a quote gives an empty name and returns false; size and bold
/// stay 0 when missing.
bool parseWndFont(char *buffer, std::string &name, int &size, int &bold);

/// Pointer behind the first '"' of the text, or to the terminating NUL when there is none.
char *wndAfterFirstQuote(char *text);

/// The value in quotes, cut at the first character of stringSeps: `NAME = "Foo"` gives "Foo". Empty
/// (never null) when there is no quote or no value. text is modified (strtok).
const char *wndQuotedValue(char *text, const char *stringSeps);

/// Source of bytes for readWndChunk: stores the next byte in byte and returns true, false at the end.
typedef bool (*WndByteSource)(void *context, char &byte);

/// Reads the next chunk of a layout file, up to the ';' that ends it (not stored). Leading whitespace is
/// dropped, every other whitespace character becomes a space. At the end of the data the rest counts
/// as the last chunk. Returns false when the chunk does not fit in maxLen bytes: the text is then cut
/// and terminated (maxLen must be at least 1).
bool readWndChunk(WndByteSource source, void *context, char *buffer, int maxLen);

/// Numbers of a field. A missing value (null, from strtok) gives 0 and returns 0; an unparsable one
/// too. The return value is the number of converted items, as with sscanf.
int wndScanBool(const char *source, bool &value);
int wndScanShort(const char *source, short &value);
/// These two leave value unchanged when nothing is converted.
int wndScanInt(const char *source, int &value);
int wndScanUnsigned(const char *source, unsigned int &value);

/// Integer of a token; a missing token (null, from strtok) or an unparsable one gives 0.
int wndToInt(const char *token);

/// value limited to lo..hi (lo <= hi).
int wndClamp(int value, int lo, int hi);

/// Does a field with its own data block belong to a window of this type? `LISTBOXDATA` only to a
/// `SCROLLLISTBOX`, `SLIDERDATA` to `VERTSLIDER` and `HORZSLIDER`, and so on for the seven data fields.
/// Every other field fits every window. The data block of a window has the layout of its type, so a
/// field of another type would write into memory of another size (or into none at all).
bool wndFieldFitsType(const char *field, const char *type);

/// Reads count values (as strtok tokens, separated by seps, the label is already consumed) into out;
/// values beyond capacity are read and dropped, a missing value ends the list. Returns the number of
/// values read.
int wndReadBoolList(int count, const char *seps, bool *out, int capacity);

}	// namespace Engine

#endif	// ENGINE_WNDFIELDS_H
