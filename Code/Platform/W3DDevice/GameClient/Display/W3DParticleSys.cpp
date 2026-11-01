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

// W3DParticleSys.cpp
// W3D Particle System implementation
// Author: Michael S. Booth, November 2001

#include "Common/GlobalData.h"
#include "GameClient/Color.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DSmudge.h"
#include "W3DDevice/GameClient/W3DSnow.h"
#include "WW3D2/Scene/camera.h"
#include "Engine/Clock.h"
#include "formatinto.h"

//------------------------------------------------------------------------------ Performance Timers 
//#include "Common/PerfMetrics.h"
//#include "Common/PerfTimer.h"

//-------------------------------------------------------------------------------------------------


#include "Common/QuickTrig.h"
#include "GameClient/GameClient.h"	// render interpolation state
#include "GameLogic/GameLogic.h"		// extrapolate particles only while the logic advances
#include "WW3D2/Common/renderdevice.h"
W3DParticleSystemManager::W3DParticleSystemManager()
{
	m_pointGroup = nullptr;
	m_streakLine = nullptr;
	m_posBuffer = nullptr;
	m_RGBABuffer = nullptr;
	m_sizeBuffer = nullptr;
	m_angleBuffer = nullptr;
	m_readyToRender = false;
	m_updateMs = 0.0;
	m_updateParticles = 0.0;
	m_updates = 0;
	m_drawMs = 0.0;
	m_drawParticles = 0.0;
	m_draws = 0;

	m_onScreenParticleCount = 0;

	m_pointGroup = std::make_unique<PointGroupClass>();
	//m_streakLine = NULL;
	m_streakLine = NEW StreakLineClass();
	
	m_posBuffer = NEW_REF( ShareBufferClass<Vector3>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_posBuffer") );
	m_RGBABuffer = NEW_REF( ShareBufferClass<Vector4>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_RGBABuffer") );
	m_sizeBuffer = NEW_REF( ShareBufferClass<float>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_sizeBuffer") );
	m_angleBuffer = NEW_REF( ShareBufferClass<uint8>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_angleBuffer") );
}

void W3DParticleSystemManager::releaseDeviceResources()
{
	m_pointGroup.reset();
	m_pointGroup = nullptr;
	if (m_streakLine)
	{
		REF_PTR_RELEASE(m_streakLine);
		m_streakLine = nullptr;
	}
}

W3DParticleSystemManager::~W3DParticleSystemManager()
{
	m_pointGroup.reset();

//	W3DDisplay::m_3DScene->Remove_Render_Object( m_streakLine );

	if (m_streakLine)
	{
		REF_PTR_RELEASE(m_streakLine);
	}

	REF_PTR_RELEASE(m_posBuffer);
	REF_PTR_RELEASE(m_RGBABuffer);
	REF_PTR_RELEASE(m_sizeBuffer);
	REF_PTR_RELEASE(m_angleBuffer);
}

/**
 * Hack because DoParticles is called from Flush(), which is called
 * multiple times per frame.  We only want to render once.
 * @todo Clean up the flag/Flush hack.
 */
void W3DParticleSystemManager::queueParticleRender()
{
	m_readyToRender = true;
}

/**
 * Nasty hack to render particles last. Called directly by WW3D::Flush()
 */
void DoParticles( RenderInfoClass &rinfo )
{
	if (TheParticleSystemManager)
		TheParticleSystemManager->doParticles(rinfo);
}

//temporary hack that checks if texture name starts with "SMUD" - if so, we can assume it's a smudge type
static Bool isSmudgeSystem( ParticleSystem *sys )
{
	return /*sys->isUsingSmudge()*/ *((DWORD *)sys->getParticleTypeName().str()) == 0x44554D53;
}

// Culls the particles of one system and writes the visible ones as GPU instances, with the values the
// position, color, size and angle arrays of the point group path end up as.
void W3DParticleSystemManager::buildInstances( InstancedSystem &system, const CullBox &cull, RenderParticleInstance *out )
{
	ParticleSystem *sys = system.sys;
	const Bool field = ( sys->getPriority() == AREA_EFFECT && sys->m_isGroundAligned != FALSE );
	RenderParticleInstance *dst = out + system.first;
	UnsignedInt count = 0;
	UnsignedInt fieldParticles = 0;
	Coord3D interpPos;

	UnsignedInt slots;
	const ParticleState *states = sys->getParticleStates( &slots );
	for (UnsignedInt i = 0; i < slots; ++i)
	{
		const ParticleState *p = &states[i];
		if (!p->alive)
			continue;
		const Coord3D *pos = &p->pos;
		if (cull.interpolate)
		{
			const Coord3D *vel = &p->vel;
			interpPos.x = pos->x + vel->x * cull.renderAlpha;
			interpPos.y = pos->y + vel->y * cull.renderAlpha;
			interpPos.z = pos->z + vel->z * cull.renderAlpha;
			pos = &interpPos;
		}
		Real psize = p->size;

		//Cull particle to edges of screen and terrain.
		if (WWMath::Fabs(pos->x - cull.centerX) > (cull.extentX + psize))
			continue;

		if (WWMath::Fabs(pos->y - cull.centerY) > (cull.extentY + psize))
			continue;

		if (WWMath::Fabs(pos->z - cull.centerZ) > (cull.extentZ + psize))
			continue;

		fieldParticles += field;

		RenderParticleInstance &instance = dst[count];
		instance.x = pos->x;
		instance.y = pos->y;
		instance.z = pos->z;
		instance.size = psize;
		instance.color = RenderDevice::Convert_Color_Clamp( Vector4( p->color.red, p->color.green, p->color.blue, p->alpha ) );
		instance.orientation = static_cast<uint8>(p->angleZ * 255.0f / (2.0f * PI));
		instance.pad[0] = instance.pad[1] = instance.pad[2] = 0;

		if (++count == MAX_POINTS_PER_GROUP)
			break;
	}
	system.count = count;
	system.fieldParticles = fieldParticles;
}

void W3DParticleSystemManager::doParticles(RenderInfoClass &rinfo)
{
	// Particles are advanced by their per-tick motion * alpha so they keep up with interpolated models.
	// Only done when the logic has counted a frame since the last update(); while paused or with frozen
	// time they would otherwise oscillate with alpha.
	const Bool interpolate = GameClient_IsRenderInterpolated() && TheGameLogic != nullptr
		&& m_lastLogicFrameUpdate != TheGameLogic->getFrame();
	const Real renderAlpha = GameClient_RenderInterpolation();
	Coord3D interpPos;

	if (m_readyToRender == false)
		return;

	// external mechanism must tell us when it's OK to render again...
	m_readyToRender = false;
	const Engine::ScopedTimer drawTimer;

	// Particle lighting: object light of the current time of day relative to the afternoon, per channel
	// (ambient plus half the diffuse lights, as on a surface facing no light in particular). The authored
	// daylight look stays; evening and night smoke gets darker and tinted.
	if ((RenderDevice::Get_Effect_Settings().flags & RENDER_EFFECT_PARTICLE_LIGHT)
		&& TheGlobalData->m_timeOfDay >= TIME_OF_DAY_FIRST && TheGlobalData->m_timeOfDay < TIME_OF_DAY_COUNT)
	{
		const Int lightCount = (TheGlobalData->m_numGlobalLights < MAX_GLOBAL_LIGHTS) ? TheGlobalData->m_numGlobalLights : MAX_GLOBAL_LIGHTS;
		const GlobalData::TerrainLighting *now = TheGlobalData->m_terrainObjectsLighting[TheGlobalData->m_timeOfDay];
		const GlobalData::TerrainLighting *noon = TheGlobalData->m_terrainObjectsLighting[TIME_OF_DAY_AFTERNOON];
		Real nowRGB[3] = { now[0].ambient.red, now[0].ambient.green, now[0].ambient.blue };
		Real noonRGB[3] = { noon[0].ambient.red, noon[0].ambient.green, noon[0].ambient.blue };
		for (Int i = 0; i < lightCount; ++i)
		{
			nowRGB[0] += 0.5f * now[i].diffuse.red;
			nowRGB[1] += 0.5f * now[i].diffuse.green;
			nowRGB[2] += 0.5f * now[i].diffuse.blue;
			noonRGB[0] += 0.5f * noon[i].diffuse.red;
			noonRGB[1] += 0.5f * noon[i].diffuse.green;
			noonRGB[2] += 0.5f * noon[i].diffuse.blue;
		}
		float scale[3];
		for (Int c = 0; c < 3; ++c)
		{
			const Real ratio = (noonRGB[c] > 0.05f) ? nowRGB[c] / noonRGB[c] : 1.0f;
			scale[c] = static_cast<float>((ratio < 0.2f) ? 0.2f : ((ratio > 1.25f) ? 1.25f : ratio));
		}
		RenderDevice::Set_Particle_Lighting(scale);
	}

	//reset each frame
	/// @todo lorenzen sez: this should be debug only:
	m_onScreenParticleCount = 0;

	Int visibleSmudgeCount = 0;
	if (TheSmudgeManager)
		TheSmudgeManager->setSmudgeCountLastFrame(0);	//keep track of visible smudges

 	const FrustumClass & frustum = rinfo.Camera.Get_Frustum();
	AABoxClass bbox;

	//Get a bounding box around our visible universe.  Bounded by terrain and the sky
	//so much tighter fitting volume than what's actually visible.  This will cull
	//particles falling under the ground.

 	TheTerrainRenderObject->getMaximumVisibleBox(frustum, &bbox, TRUE);

	//@todo lorenzen sez: put these in registers for sure
	Real bcX = bbox.Center.X;
	Real bcY = bbox.Center.Y;
	Real bcZ = bbox.Center.Z;
	Real beX = bbox.Extent.X;
	Real beY = bbox.Extent.Y;
	Real beZ = bbox.Extent.Z;

	unsigned int personalities[MAX_POINTS_PER_GROUP];


	m_fieldParticleCount = 0;

	SmudgeSet *set=nullptr;
	if (TheSmudgeManager)
		set=TheSmudgeManager->addSmudgeSet();	//global smudge set through which all smudges are rendered.

	ParticleSystemManager::ParticleSystemList &particleSysList = TheParticleSystemManager->getAllParticleSystems();

	// Plain particle systems get their GPU instances built up front, straight from the particle states; the
	// draws below keep the order of the system list. (Building takes well under 0.1 ms for 4000 particles,
	// less than waking worker threads costs.)
	m_instancedSystems.clear();
	if (m_pointGroup && PointGroupClass::Prepare_GPU_Instances())
	{
		UnsignedInt total = 0;
		for( ParticleSystemManager::ParticleSystemListIt it = particleSysList.begin(); it != particleSysList.end(); ++it)
		{
			ParticleSystem *sys = (*it);
			if (!sys || sys->isUsingDrawables() || isSmudgeSystem(sys) || sys->isUsingStreak() || sys->getVolumeParticleDepth() > 1)
				continue;
			UnsignedInt slots;
			sys->getParticleStates( &slots );
			const InstancedSystem system = { sys, total, 0, 0 };
			m_instancedSystems.push_back( system );
			total += (slots < static_cast<UnsignedInt>(MAX_POINTS_PER_GROUP)) ? slots : static_cast<UnsignedInt>(MAX_POINTS_PER_GROUP);
		}
		if (m_instances.size() < total)
			m_instances.resize( total );

		const CullBox cull = { bcX, bcY, bcZ, beX, beY, beZ, renderAlpha, interpolate };
		for (size_t k = 0; k < m_instancedSystems.size(); ++k)
			buildInstances( m_instancedSystems[k], cull, m_instances.data() );
	}
	size_t nextInstanced = 0;

	for( ParticleSystemManager::ParticleSystemListIt it = particleSysList.begin(); it != particleSysList.end(); ++it)
	{
		ParticleSystem *sys = (*it);
		if (!sys) {
			continue;
		}

		// only look at particle/point style systems
		if (sys->isUsingDrawables())
			continue;

		if (isSmudgeSystem(sys))
		{
			if (TheSmudgeManager && (static_cast<W3DSmudgeManager*>(TheSmudgeManager))->getHardwareSupport() && TheGlobalData->m_useHeatEffects)
			{
				//set-up all the per-particle
				UnsignedInt slots;
				const ParticleState *states = sys->getParticleStates( &slots );
				for (UnsignedInt i = 0; i < slots; ++i)
				{
					const ParticleState *p = &states[i];
					if (!p->alive)
						continue;
					const Coord3D *pos = &p->pos;
					Real psize = p->size;

					//Cull particle to edges of screen and terrain.
					if (WWMath::Fabs( pos->x - bcX ) > ( beX + psize ) )
						continue;

					if (WWMath::Fabs( pos->y - bcY ) > ( beY + psize ) )
						continue;

					if (WWMath::Fabs( pos->z - bcZ ) > ( beZ + psize ) )
						continue;

					Smudge *smudge = set->addSmudgeToSet();

					smudge->m_pos.Set( pos->x, pos->y, pos->z );
					smudge->m_offset.Set( GameClientRandomValueReal(-0.06f,0.06f), GameClientRandomValueReal(-0.03f,0.03f) );
					smudge->m_size = psize;
					smudge->m_opacity = p->alpha;
					visibleSmudgeCount++;
				}
			}
			continue;
		}

		if (nextInstanced < m_instancedSystems.size() && m_instancedSystems[nextInstanced].sys == sys)
		{
			const InstancedSystem &system = m_instancedSystems[nextInstanced++];
			m_fieldParticleCount += system.fieldParticles;
			if (system.count == 0)
				continue;	//this system has no particles to render

			TextureClass *texture = W3DDisplay::m_assetManager->Get_Texture( sys->getParticleTypeName().str() );
			m_pointGroup->Set_Texture( texture );
			texture->Release_Ref();//release reference since it's held by pointGroup
			m_pointGroup->Set_Flag( PointGroupClass::TRANSFORM, true );	// transform to screen space
			switch( sys->getShaderType() )
			{
				case ParticleSystemInfo::ADDITIVE:
					m_pointGroup->Set_Shader( ShaderClass::_PresetAdditiveSpriteShader );
					break;
				case ParticleSystemInfo::ALPHA:
					m_pointGroup->Set_Shader( ShaderClass::_PresetAlphaSpriteShader );
					break;
				case ParticleSystemInfo::ALPHA_TEST:
					m_pointGroup->Set_Shader( ShaderClass::_PresetATestSpriteShader );
					break;
				case ParticleSystemInfo::MULTIPLY:
					m_pointGroup->Set_Shader( ShaderClass::_PresetMultiplicativeSpriteShader );
					break;
			}
			m_pointGroup->Set_Point_Mode( PointGroupClass::QUADS );
			m_pointGroup->Set_Billboard( sys->shouldBillboard() );
			m_pointGroup->Set_Point_Frame( 0 );
			m_pointGroup->Render_Instances( &m_instances[system.first], static_cast<int>(system.count) );

			m_onScreenParticleCount += system.count;
			continue;
		}

		/// @todo lorenzen sez: declare these outside the sys loop, and put some in registers
		// initialize them here still, of course
		// build W3D particle buffer
		Int count = 0;
		Vector3 *posArray = m_posBuffer->Get_Array();
		Real *sizeArray = m_sizeBuffer->Get_Array();
		Vector4 *RGBAArray = m_RGBABuffer->Get_Array();
		uint8 *angleArray = m_angleBuffer->Get_Array();
		const Coord3D *pos;
		const RGBColor *color;
		Real psize;



		//set-up all the per-particle
		UnsignedInt slots;
		const ParticleState *states = sys->getParticleStates( &slots );
		for (UnsignedInt i = 0; i < slots; ++i)
		{
			const ParticleState *p = &states[i];
			if (!p->alive)
				continue;
			pos = &p->pos;
			if (interpolate)
			{
				const Coord3D *vel = &p->vel;
				interpPos.x = pos->x + vel->x * renderAlpha;
				interpPos.y = pos->y + vel->y * renderAlpha;
				interpPos.z = pos->z + vel->z * renderAlpha;
				pos = &interpPos;
			}
			psize = p->size;

			//Cull particle to edges of screen and terrain.
			if (WWMath::Fabs(pos->x - bcX) > (beX + psize))
				continue;

			if (WWMath::Fabs(pos->y - bcY) > (beY + psize))
				continue;

			if (WWMath::Fabs(pos->z - bcZ) > (beZ + psize))
				continue;

			m_fieldParticleCount += ( sys->getPriority() == AREA_EFFECT && sys->m_isGroundAligned != FALSE );
			
			//@todo lorenzen sez: use pointer arithmetic for these arrays
			personalities[count] = p->personality;
			
			posArray[count].X = pos->x;
			posArray[count].Y = pos->y;
			posArray[count].Z = pos->z;

			sizeArray[count] = psize;

			color = &p->color;
			RGBAArray[count].X = color->red;
			RGBAArray[count].Y = color->green;
			RGBAArray[count].Z = color->blue;
			RGBAArray[count].W = p->alpha;
		
			angleArray[count] = static_cast<uint8>(p->angleZ * 255.0f / (2.0f * PI));
			
			if (++count == MAX_POINTS_PER_GROUP)
				break;
		}

		if ( count == 0 )
			continue;	//this system has no particles to render

		TextureClass *texture = W3DDisplay::m_assetManager->Get_Texture( sys->getParticleTypeName().str() );
		
		if ( m_streakLine && sys->isUsingStreak() && (count >= 2) ) 
		{
			m_streakLine->Reset_Line();

			m_streakLine->Set_Texture( texture );
			texture->Release_Ref();//release reference since it's held by streakline
			switch( sys->getShaderType() )
			{
				case ParticleSystemInfo::ADDITIVE:
					m_streakLine->Set_Shader( ShaderClass::_PresetAdditiveSpriteShader );
					break;
				case ParticleSystemInfo::ALPHA:
					m_streakLine->Set_Shader( ShaderClass::_PresetAlphaSpriteShader );
					break;
				case ParticleSystemInfo::ALPHA_TEST:
					m_streakLine->Set_Shader( ShaderClass::_PresetATestSpriteShader );
					break;
				case ParticleSystemInfo::MULTIPLY:
					m_streakLine->Set_Shader( ShaderClass::_PresetMultiplicativeSpriteShader );
					break;
			}
			
			//UPDATE THE STREAK'S ARRAYS
			m_streakLine->Set_LocsWidthsColors( 
				count,
				m_posBuffer->Get_Array(),
				m_sizeBuffer->Get_Array(),
				m_RGBABuffer->Get_Array(),
				&personalities[0]
				);

			//WWASSERT( m_streakLine->Get_Num_Points() == count );

			// This is the happy place for this!
			RGBAArray[0].X = 0;//eliminates the scissor edge on the trailing edge of the streak
			RGBAArray[0].Y = 0;
			RGBAArray[0].Z = 0;
			RGBAArray[0].W = 0;


			//RENDER STREAK!
			m_streakLine->Render( rinfo );
			
		}
		else 
		{

			WWASSERT( m_pointGroup );

			if ( m_pointGroup ) // this catches the particle and volumeparticle cases
			{
				// render all the systems' particles
				m_pointGroup->Set_Texture( texture );
				texture->Release_Ref();//release reference since it's held by pointGroup
				m_pointGroup->Set_Flag( PointGroupClass::TRANSFORM, true );	// transform to screen space

				switch( sys->getShaderType() )
				{
					case ParticleSystemInfo::ADDITIVE:
						m_pointGroup->Set_Shader( ShaderClass::_PresetAdditiveSpriteShader );
						break;
					case ParticleSystemInfo::ALPHA:
						m_pointGroup->Set_Shader( ShaderClass::_PresetAlphaSpriteShader );
						break;
					case ParticleSystemInfo::ALPHA_TEST:
						m_pointGroup->Set_Shader( ShaderClass::_PresetATestSpriteShader );
						break;
					case ParticleSystemInfo::MULTIPLY:
						m_pointGroup->Set_Shader( ShaderClass::_PresetMultiplicativeSpriteShader );
						break;
				}

				/// @todo Use both QUADS and TRIS for particles
				m_pointGroup->Set_Point_Mode( PointGroupClass::QUADS );
				m_pointGroup->Set_Arrays( m_posBuffer, m_RGBABuffer, nullptr, m_sizeBuffer, m_angleBuffer, nullptr, count );
				m_pointGroup->Set_Billboard(sys->shouldBillboard());

				/// @todo Support animated texture particles
				/// @todo lorenzen sez: unimplemented code wastes cpu cycles
				m_pointGroup->Set_Point_Frame( 0 );

				//RENDER IT!
				if( sys->getVolumeParticleDepth() > 1 )
				{
					m_pointGroup->RenderVolumeParticle( rinfo, sys->getVolumeParticleDepth() );
				}
				else
					m_pointGroup->Render( rinfo );
		
			}
		}


		/// @todo lorenzen sez: this should be debug only:
		//add particle count to total
		m_onScreenParticleCount += count;

	/*
		// draw the wind vector for this particle system on the screen
		UnsignedInt width = TheDisplay->getWidth();
		UnsignedInt height = TheDisplay->getHeight();
		Coord3D worldStart, worldEnd;
		ICoord2D pixelStart, pixelEnd;
		sys->getPosition( &worldStart );
		worldEnd.x = Cos( sys->getWindAngle() ) * 50.0f + worldStart.x;
		worldEnd.y = Sin( sys->getWindAngle() ) * 50.0f + worldStart.y;
		worldEnd.z = worldStart.z;
		TheTacticalView->worldToScreen( &worldStart, &pixelStart );
		TheTacticalView->worldToScreen( &worldEnd, &pixelEnd );
		Color colorStart = GameMakeColor( 255, 255, 255, 255 );
		Color colorEnd = GameMakeColor( 255, 128, 128, 255 );
		TheDisplay->drawLine( pixelStart.x, pixelStart.y, pixelEnd.x, pixelEnd.y, 1.0f, colorStart, colorEnd );
	*/


	}// next system

		/// @todo lorenzen sez: this should be debug only:
	TheParticleSystemManager->setOnScreenParticleCount(m_onScreenParticleCount);

	//Draw any particles belonging to weather effects
	if (TheSnowManager)
		(static_cast<W3DSnowManager *>(TheSnowManager))->render(rinfo);

	//Now process screen smudges which are particles that distort the background behind them.
	if(TheSmudgeManager)
	{
		(static_cast<W3DSmudgeManager *>(TheSmudgeManager))->render(rinfo);
		TheSmudgeManager->reset();	//clear all the smudges after rendering since we fill again each frame.
		TheSmudgeManager->setSmudgeCountLastFrame(visibleSmudgeCount);
	}

	m_drawMs += drawTimer.elapsedMilliseconds();
	m_drawParticles += m_onScreenParticleCount;
	++m_draws;
}

void W3DParticleSystemManager::addUpdateTime(double ms)
{
	m_updateMs += ms;
	m_updateParticles += getParticleCount();
	++m_updates;
}

void W3DParticleSystemManager::formatCpuTimes(char *text, size_t size) const
{
	text[0] = 0;
	if (m_updates == 0 && m_draws == 0)
		return;
	const double updates = m_updates ? static_cast<double>(m_updates) : 1.0;
	const double draws = m_draws ? static_cast<double>(m_draws) : 1.0;
	WWFormat::Into(text, size, "PARTIKELZEIT simulation {:.4f} ms je Tick ({:d} Ticks, {:.1f} Partikel) aufbereiten {:.4f} ms je Bild ({:d} Bilder, {:.1f} Partikel sichtbar)",
		m_updateMs / updates, m_updates, m_updateParticles / updates, m_drawMs / draws, m_draws, m_drawParticles / draws);
}
