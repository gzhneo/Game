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

// FILE: W3DParticleSys.h /////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __W3DParticleSys_H_
#define __W3DParticleSys_H_

#include "GameClient/ParticleSys.h"
#include <memory>
#include <vector>
#include "WW3D2/Effects/pointgr.h"
#include "WW3D2/Effects/streak.h"
#include "WW3D2/Scene/rinfo.h"
#include "WW3D2/Common/renderbackend.h"
#include "WWLib/bittype.h"

//=============================================================================
/** W3D implementation of the game display which is responsible for creating
  * all interaction with the screen and updating the display 
	*/
class W3DParticleSystemManager : public ParticleSystemManager
{

public:
	W3DParticleSystemManager();
	~W3DParticleSystemManager() override;

	virtual void doParticles(RenderInfoClass &rinfo) override;
	virtual void queueParticleRender() override;
	///< returns the number of particles shown on screen per frame
	virtual Int getOnScreenParticleCount() override { return m_onScreenParticleCount; }

	/// PointGroup and StreakLine hold textures, and the manager is destroyed after the display and
	/// the device. W3DDisplay calls this before WW3D shuts down so the textures are released while
	/// the device still exists.
	void releaseDeviceResources();

	/// CPU time of the particle simulation (ParticleSystemManager::update, once per logic tick).
	void addUpdateTime(double ms);
	/// Runner log line with the CPU times of simulation and render preparation (empty without data).
	void formatCpuTimes(char *text, size_t size) const;

private:
	enum { MAX_POINTS_PER_GROUP = 512 };

	/// A system whose particles are drawn as GPU instances built straight from its states.
	struct InstancedSystem
	{
		ParticleSystem *sys;
		UnsignedInt first;											///< its range in m_instances (at most MAX_POINTS_PER_GROUP)
		UnsignedInt count;											///< visible particles written
		UnsignedInt fieldParticles;								///< of these, ground aligned area effect particles
	};
	struct CullBox
	{
		Real centerX, centerY, centerZ;
		Real extentX, extentY, extentZ;
		Real renderAlpha;
		Bool interpolate;
	};
	static void buildInstances( InstancedSystem &system, const CullBox &cull, RenderParticleInstance *out );

	std::unique_ptr<PointGroupClass> m_pointGroup;							///< the point group that contains all of the particles
	StreakLineClass *m_streakLine;							///< the streak class that contains all of the streaks
	ShareBufferClass<Vector3> *m_posBuffer;			///< array of particle positions
	ShareBufferClass<Vector4> *m_RGBABuffer;		///< array of particle color and alpha
	ShareBufferClass<float> *m_sizeBuffer;			///< array of particle sizes
	ShareBufferClass<uint8> *m_angleBuffer;			///< array of particle orientations
	Bool m_readyToRender;											///< if true, it is OK to render
	double m_updateMs;													///< statistics: CPU time of the simulation
	double m_updateParticles;										///< statistics: particles over all simulated ticks
	UnsignedInt m_updates;
	double m_drawMs;														///< statistics: CPU time of doParticles (culling, buffers, draws)
	double m_drawParticles;											///< statistics: particles on screen over all drawn frames
	UnsignedInt m_draws;
	std::vector<InstancedSystem> m_instancedSystems;			///< this frame, in the order of the system list
	std::vector<RenderParticleInstance> m_instances;
};

#endif  // end __W3DParticleSys_H_
