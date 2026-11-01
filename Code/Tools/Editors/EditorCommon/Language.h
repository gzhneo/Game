/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Language.h -- interface texts in English (source) and German.
//
// Source texts in the code are English. The German texts live in a file of the program (UTF-8, one
// "English<TAB>German" pair per line, setTranslationFile), so no build step or Qt Linguist is needed. A language switch
// takes effect at once: the windows rebuild their texts.

#pragma once

#include <QString>

namespace EditorCommon
{

enum class Language
{
	English,
	German
};

// The German texts of the program: a UTF-8 file of "English<TAB>German" lines (a Qt resource path);
// set once before the first text is translated.
void setTranslationFile(const QString &path);

// Installs the translation for the language; English removes it.
void setLanguage(Language language);
Language currentLanguage();

// Default from the Windows interface language: German for German, otherwise English.
Language systemLanguage();

// Translated text of an English source text.
QString T(const char *english);

// English source text of a text as shown; the text itself if it is no translation.
QString sourceText(const QString &shown);
// Number of source texts the German file translates (for the self test).
int germanEntryCount();

} // namespace EditorCommon
