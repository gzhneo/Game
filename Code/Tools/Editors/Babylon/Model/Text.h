/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Text.h -- character sets and the text rules of the translation database.
//
// Texts are UTF-16 (std::u16string) as in main.db and the .csf files. Generals.str and the generated
// .str files are Latin-1: the game widens every byte (GameTextManager::translateCopy), so Babylon
// does the same. The normalising rules (formatMetaString, stripSpaces, convertMetaChars) decide
// whether a text of Generals.str equals the one in an existing main.db, so they keep the exact
// behaviour of the original tool, quirks included.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Babylon
{

std::string toUtf8(std::u16string_view text);
// Invalid sequences become U+FFFD; *ok (if given) turns false then.
std::u16string fromUtf8(std::string_view text, bool *ok = nullptr);
std::u16string fromLatin1(std::string_view text);
// Characters beyond U+00FF become '?'; *lost counts them.
std::string toLatin1(std::u16string_view text, int *lost = nullptr);
// ASCII text (labels, names) as UTF-16.
std::u16string ascii(std::string_view text);

// Compares like wcsicmp in the C locale: only A-Z and a-z fold.
int compareNoCase(std::u16string_view a, std::u16string_view b);
inline bool equalNoCase(std::u16string_view a, std::u16string_view b) { return compareNoCase(a, b) == 0; }
bool equalNoCase(std::string_view a, std::string_view b);

// Removes leading, doubled and trailing spaces and the spaces around real line breaks and tabs.
void stripSpaces(std::u16string &text);
// Turns the escapes \n and \t into line breaks and tabs; any other escaped character stands for
// itself, a backslash at the end is dropped.
void convertMetaChars(std::u16string &text);
// The form a text of Generals.str is stored in: doubled spaces collapsed, " \n " around escaped
// line breaks and tabs, no trailing space.
void formatMetaString(std::u16string &text);

// printf conversions of a text in order ("%d", "%-5.2f", ...); "%%" is no conversion.
std::vector<std::u16string> formatSpecifiers(std::u16string_view text);
// True if both texts have the same printf conversions in the same order. A translation with
// another set would make the game read other arguments than it passes.
bool sameFormat(std::u16string_view translation, std::u16string_view original);

// The test language "Jabberwockie": every word reversed, escapes and printf conversions kept.
std::u16string jabber(std::u16string_view text);

std::u16string toU16(int value);

} // namespace Babylon
