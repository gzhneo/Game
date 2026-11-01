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

// Minimal stub for AnimatedSoundMgrClass. Animatable3DObjClass and WW3D::Init/Shutdown
// reference it unconditionally, but sounds embedded in HAnim data have no XAudio2 path,
// so all methods are no-ops.

#include "animatedsoundmgr.h"

void AnimatedSoundMgrClass::Initialize(const char *ini_filename)
{
}

void AnimatedSoundMgrClass::Shutdown(void)
{
}

const char *AnimatedSoundMgrClass::Get_Embedded_Sound_Name(HAnimClass *anim)
{
	return nullptr;
}

float AnimatedSoundMgrClass::Trigger_Sound(HAnimClass *anim, float old_frame, float new_frame, const Matrix3D &tm)
{
	// Pass through, as when no sound library is set.
	return old_frame;
}

void AnimatedSoundMgrClass::Set_Sound_Library(SoundLibraryBridgeClass *library)
{
}
