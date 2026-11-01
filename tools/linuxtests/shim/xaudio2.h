/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/
/* xaudio2.h -- Stub fuer den Linux-Testlauf (tools/linuxtests/README.md).
 *
 * MinGW liefert die XAudio2-Header nicht mit. Die Unit-Tests brauchen aus
 * XAudio2AudioManager.h nur die Deklarationen, nicht die Bibliothek: WavParser.h bindet
 * den Header wegen WaveFormatInfo ein, und XAudio2SoundCallback leitet von
 * IXAudio2VoiceCallback ab. Die uebrigen XAudio2-Typen kommen nur als Zeiger vor, eine
 * Vorwaertsdeklaration genuegt.
 *
 * Die sieben Methoden unten muessen der echten Schnittstelle entsprechen -- wenn nicht,
 * scheitert XAudio2SoundCallback am override und der Fehler faellt sofort auf. Ausgefuehrt
 * wird hiervon nichts.
 */
#pragma once
#ifndef __LINUXTESTS_XAUDIO2_H
#define __LINUXTESTS_XAUDIO2_H

#include <windows.h>

struct IXAudio2;
struct IXAudio2SourceVoice;
struct IXAudio2SubmixVoice;
struct IXAudio2MasteringVoice;

struct IXAudio2VoiceCallback
{
	virtual void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32 BytesRequired) = 0;
	virtual void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() = 0;
	virtual void STDMETHODCALLTYPE OnStreamEnd() = 0;
	virtual void STDMETHODCALLTYPE OnBufferStart(void* pBufferContext) = 0;
	virtual void STDMETHODCALLTYPE OnBufferEnd(void* pBufferContext) = 0;
	virtual void STDMETHODCALLTYPE OnLoopEnd(void* pBufferContext) = 0;
	virtual void STDMETHODCALLTYPE OnVoiceError(void* pBufferContext, HRESULT Error) = 0;
};

#endif
