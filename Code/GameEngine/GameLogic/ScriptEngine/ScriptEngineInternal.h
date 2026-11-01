/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// ScriptEngineInternal.h -- what the ScriptEngine*.cpp files share and nobody else needs.

#pragma once

// These are for debugger window (defined in ScriptEngineDebug.cpp)
extern int st_LastCurrentFrame;
extern int st_CurrentFrame;
extern Bool st_CanAppCont;
extern HMODULE st_DebugDLL;
void _appendMessage(const AsciiString& str, Bool isTrueMessage = true, Bool shouldPause = false);
void _adjustVariable(const AsciiString& str, Int value, Bool shouldPause = false);
void _updateFrameNumber( void );
// That's it for debugger window

static const Int FRAMES_TO_FADE_IN_AT_START = 33;
