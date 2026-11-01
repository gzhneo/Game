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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// W3DDynamicLight.cpp
// Class to handle dynamic lights.
// Author: John Ahlquist, April 2001
#include <stdlib.h>

#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "Common/GameEngine.h"

W3DDynamicLight::W3DDynamicLight(void):
LightClass(LightClass::POINT)
{

	m_priorEnable = false;
	m_enabled = true;

}

W3DDynamicLight::~W3DDynamicLight(void)
{
}

void W3DDynamicLight::On_Frame_Update(void)
{	
	if (!m_enabled) {
		return;
	}
	// The fade is counted in frames of 1/30 s; a drawn frame advances it by its steps.
	const Real steps = GameEngine_RenderFrameSteps();
	Real factor = 1.0f;
	if (m_curIncreaseFrameCount>0 && m_increaseFrameCount>0) {
		// increasing 
		m_curIncreaseFrameCount -= steps;
		if (m_curIncreaseFrameCount < 0)
			m_curIncreaseFrameCount = 0;
		factor = (m_increaseFrameCount-m_curIncreaseFrameCount)/static_cast<Real>(m_increaseFrameCount);

	}	else if (m_decayFrameCount==0) {
		factor = 1.0;  // never decays,
	}	else {
		m_curDecayFrameCount -= steps;
		if (m_curDecayFrameCount <= 0) {
			m_enabled = false;
			return;
		}
		factor = m_curDecayFrameCount/static_cast<Real>(m_decayFrameCount);
	}
	if (m_decayRange) {
		this->FarAttenEnd = factor*m_targetRange;
		if (FarAttenEnd < FarAttenStart) {
			FarAttenEnd = FarAttenStart;
		}
	}
	if (m_decayColor) {
		this->Ambient = m_targetAmbient*factor;
		this->Diffuse = m_targetDiffuse*factor;
	}
}

void W3DDynamicLight::setFrameFade(UnsignedInt frameIncreaseTime, UnsignedInt decayFrameTime)
{
	m_decayFrameCount = decayFrameTime;
	m_curDecayFrameCount = static_cast<Real>(decayFrameTime);
	m_curIncreaseFrameCount = static_cast<Real>(frameIncreaseTime);
	m_increaseFrameCount = frameIncreaseTime;
	m_targetAmbient = Ambient;
	m_targetDiffuse = Diffuse;
	m_targetRange = FarAttenEnd;
}