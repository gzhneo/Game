/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/
/* x3daudio.h -- Stub fuer den Linux-Testlauf (tools/linuxtests/README.md).
 *
 * MinGW liefert die XAudio2-Header nicht mit. Fuer die Unit-Tests wird nichts davon
 * ausgefuehrt: XAudio2AudioManager.h haengt nur als Include an WavParser.h (dort steht
 * WaveFormatInfo), und gebraucht wird aus diesem Header genau ein Typ. Alles hier ist
 * reine Deklaration -- gegen die echte Bibliothek gelinkt wird nie.
 */
#pragma once
#ifndef __LINUXTESTS_X3DAUDIO_H
#define __LINUXTESTS_X3DAUDIO_H

/* Im Original ein undurchsichtiger Byteblock; nur die Groesse muss stimmen, damit
 * sizeof() und die Feldanordnung von XAudio2AudioManager gleich bleiben. */
#define X3DAUDIO_HANDLE_BYTESIZE 20
typedef unsigned char X3DAUDIO_HANDLE[X3DAUDIO_HANDLE_BYTESIZE];

#endif
