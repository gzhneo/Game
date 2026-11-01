/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// RealInputFilter.h -- the switches that keep real mouse and keyboard input out of a run, in one place
// for Win32Mouse and Win32Keyboard (rules in Engine/InputQueue.h).

#pragma once

#ifndef WIN32DEVICE_REALINPUTFILTER_H
#define WIN32DEVICE_REALINPUTFILTER_H

#include "Common/GlobalData.h"
#include "Engine/InputQueue.h"

inline ::Engine::RealInputFilter realInputFilter( void )
{
	::Engine::RealInputFilter filter;
	if( TheGlobalData == nullptr )
		return filter;
	filter.imageTest = TheGlobalData->m_shotAfterCount > 0 || TheGlobalData->m_shotEvery > 0
		|| TheGlobalData->m_cameraPathFile.isNotEmpty();
	filter.testSequence = TheGlobalData->m_testScriptFile.isNotEmpty() && !TheGlobalData->m_testRealInput;
	filter.noRealInput = TheGlobalData->m_noRealInput != FALSE;
	return filter;
}

#endif // WIN32DEVICE_REALINPUTFILTER_H
