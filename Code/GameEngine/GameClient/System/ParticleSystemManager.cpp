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

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Engine/ThreadRole.h"

#include "Common/GameState.h"
#include "Common/INI.h"
#include "Common/PerfTimer.h"
#include "Common/ThingFactory.h"
#include "Common/GameLOD.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameClient/DebugDisplay.h"
#include "GameClient/Display.h"
#include "GameClient/GameClient.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"

#include <algorithm>

// ParticleSystemManager: all particle systems -- creating, finding, updating, the particle budget

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
ParticleSystemManager::ParticleSystemManager( void )
{

	m_uniqueSystemID = INVALID_PARTICLE_SYSTEM_ID;

	m_onScreenParticleCount = 0;
	m_localPlayerIndex = 0;
	
	//Added By Sadullah Nader
	//Initializations inserted
	m_lastLogicFrameUpdate = 0;
	m_particleCount = 0;
	m_fieldParticleCount = 0;
	m_particleSystemCount = 0;
	//

	for( Int i = 0; i < NUM_PARTICLE_PRIORITIES; ++i )
	{
		
		m_allParticlesHead[ i ] = nullptr;
		m_allParticlesTail[ i ] = nullptr;

	}  // end for, i

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
ParticleSystemManager::~ParticleSystemManager()
{
	reset();

	TemplateMap::iterator begin(m_templateMap.begin());
	TemplateMap::iterator end(m_templateMap.end());
	for (; begin != end; ++begin) {
		deleteInstanceOf((*begin).second);
	}
}

// ------------------------------------------------------------------------------------------------
/** Initialize the manager */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::init( void )
{
	/// Read INI data and build templates
	INI ini;
	ini.load( AsciiString( "Data\\INI\\ParticleSystem.ini" ), INILoadType::INI_LOAD_OVERWRITE, nullptr );

	// sanity, our lists must be empty!!
	for( Int i = 0; i < NUM_PARTICLE_PRIORITIES; ++i )
	{

		// sanity		
		DEBUG_ASSERTCRASH( m_allParticlesHead[ i ] == nullptr, ("INIT: ParticleSystem all particles head[%d] is not NULL!\n", i) );
		DEBUG_ASSERTCRASH( m_allParticlesTail[ i ] == nullptr, ("INIT: ParticleSystem all particles tail[%d] is not NULL!\n", i) );

		// just to be clean set them to NULL
		m_allParticlesHead[ i ] = nullptr;
		m_allParticlesTail[ i ] = nullptr;

	}  // end for, i

}

// ------------------------------------------------------------------------------------------------
/** Reset the manager and all particle systems */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::reset( void )
{
	while (getParticleSystemCount()) {
		if (m_allParticleSystemList.front()) {
			deleteInstanceOf(m_allParticleSystemList.front());
		}
	}

	// sanity, our lists must be empty!!
	for( Int i = 0; i < NUM_PARTICLE_PRIORITIES; ++i )
	{

		// sanity		
		DEBUG_ASSERTCRASH( m_allParticlesHead[ i ] == nullptr, ("RESET: ParticleSystem all particles head[%d] is not NULL!\n", i) );
		DEBUG_ASSERTCRASH( m_allParticlesTail[ i ] == nullptr, ("RESET: ParticleSystem all particles tail[%d] is not NULL!\n", i) );

		// just to be clean set them to NULL
		m_allParticlesHead[ i ] = nullptr;
		m_allParticlesTail[ i ] = nullptr;

	}  // end for, i

	m_particleCount = 0;
	m_fieldParticleCount = 0;
	m_particleSystemCount = 0;

	m_uniqueSystemID = INVALID_PARTICLE_SYSTEM_ID;
	
	m_lastLogicFrameUpdate = static_cast<UnsignedInt>(-1);

	// templates stay, except what a map INI changed
	undoMapDefinitions();
}

// ------------------------------------------------------------------------------------------------
// A map INI redefines or creates a template: keep the old values once per template and map.
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::noteMapDefinition( const AsciiString &name )
{
	if( std::find( m_mapAdded.begin(), m_mapAdded.end(), name ) != m_mapAdded.end() )
		return;
	for( const MapBackup &backup : m_mapBackups )
		if( backup.tmpl->m_name == name )
			return;
	ParticleSystemTemplate *tmpl = findTemplate( name );
	if( tmpl == nullptr )
	{
		m_mapAdded.push_back( name );
		return;
	}
	m_mapBackups.push_back( MapBackup{ tmpl, static_cast<const ParticleSystemInfo &>( *tmpl ), tmpl->m_slaveTemplate } );
}

// ------------------------------------------------------------------------------------------------
// Undoes the map INI once the game's particle systems are gone. Module data and effect lists keep pointers to
// templates, so a redefined template gets its old values back in place; templates the map created are removed (the
// map's objects and effect lists that used them are gone, reverse init order in SubsystemInterfaceList::resetAll).
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::undoMapDefinitions( void )
{
	for( MapBackup &backup : m_mapBackups )
	{
		static_cast<ParticleSystemInfo &>( *backup.tmpl ) = backup.info;
		backup.tmpl->m_slaveTemplate = backup.slave;
	}
	m_mapBackups.clear();

	for( const AsciiString &name : m_mapAdded )
	{
		TemplateMap::iterator it = m_templateMap.find( name );
		if( it == m_templateMap.end() )
			continue;
		ParticleSystemTemplate *tmpl = it->second;
		m_templateMap.erase( it );
		// a remaining template may still name it as its slave
		for( auto &entry : m_templateMap )
			if( entry.second->m_slaveTemplate == tmpl )
				entry.second->m_slaveTemplate = nullptr;
		deleteInstanceOf(tmpl);
	}
	m_mapAdded.clear();
}

// ------------------------------------------------------------------------------------------------
/** Update all particle systems */
// ------------------------------------------------------------------------------------------------
//DECLARE_PERF_TIMER(ParticleSystemManager)
void ParticleSystemManager::update( void )
{
	if (m_lastLogicFrameUpdate == TheGameLogic->getFrame()) {
		return;
	}

	// update the last logic frame.
	m_lastLogicFrameUpdate = TheGameLogic->getFrame();

	//USE_PERF_TIMER(ParticleSystemManager)
	ParticleSystem *sys;

	for(ParticleSystemListIt it = m_allParticleSystemList.begin(); it != m_allParticleSystemList.end();) 
	{
		sys = (*it);
		if (!sys) {
			continue;
		}

		if (sys->update(m_localPlayerIndex) == false)
		{
			++it;
			deleteInstanceOf(sys);
		} else {
			++it;
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** sets the count of the particles on screen after each frame */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::setOnScreenParticleCount(int count)
{
	m_onScreenParticleCount = count;
}

// ------------------------------------------------------------------------------------------------
/** Given a file containing particle system properties, create a new instance of it */
// ------------------------------------------------------------------------------------------------
ParticleSystem *ParticleSystemManager::createParticleSystem( const ParticleSystemTemplate *sysTemplate, Bool createSlaves )
{
	ASSERT_MAIN_THREAD();	// the logic still calls this directly; must move to the sync point
	// sanity
	if (sysTemplate == nullptr)
		return nullptr;

	m_uniqueSystemID = static_cast<ParticleSystemID>(static_cast<UnsignedInt>(m_uniqueSystemID) + 1);
	ParticleSystem *sys = newInstance(ParticleSystem)( sysTemplate, m_uniqueSystemID, createSlaves );
	return sys;
}

// ------------------------------------------------------------------------------------------------
/// given a template, instantiate a particle system attached to the given object, and return its ID
// ------------------------------------------------------------------------------------------------
ParticleSystemID ParticleSystemManager::createAttachedParticleSystemID( 
																			const ParticleSystemTemplate *sysTemplate,
																			Object* attachTo,
																			Bool createSlaves )
{
	ParticleSystem* pSystem = TheParticleSystemManager->createParticleSystem(sysTemplate, createSlaves);
	if (pSystem && attachTo)
		pSystem->attachToObject(attachTo);
	return pSystem ? pSystem->getSystemID() : INVALID_PARTICLE_SYSTEM_ID;
}

// ------------------------------------------------------------------------------------------------
/** Find a particle system with the matching system id  */
// ------------------------------------------------------------------------------------------------
ParticleSystem *ParticleSystemManager::findParticleSystem( ParticleSystemID id )
{
	ASSERT_MAIN_THREAD();	// the logic still calls this directly; must move to the sync point
	if (id == INVALID_PARTICLE_SYSTEM_ID)
		return nullptr;	// my, that was easy

	ParticleSystem *system = nullptr;

	for( ParticleSystemListIt it = m_allParticleSystemList.begin(); it != m_allParticleSystemList.end(); ++it ) {
		system = *it;
		if (!system) {
			continue;
		}

		if( system->getSystemID() == id ) {
			return system;
		}
	}

	return nullptr;

}  // end findParticleSystem

// ------------------------------------------------------------------------------------------------
/** destroy the particle system with the given id (if it still exists) */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::destroyParticleSystemByID(ParticleSystemID id)
{
	ASSERT_MAIN_THREAD();	// the logic still calls this directly; must move to the sync point
	ParticleSystem* pSystem = findParticleSystem(id);
	if( pSystem )
		pSystem->destroy();
}

// ------------------------------------------------------------------------------------------------
/** Locate an existing ParticleSystemTemplate */
// ------------------------------------------------------------------------------------------------
ParticleSystemTemplate *ParticleSystemManager::findTemplate( const AsciiString &name ) const
{
	ParticleSystemTemplate *sysTemplate = nullptr;

	TemplateMap::const_iterator find(m_templateMap.find(name));
	if (find != m_templateMap.end()) {
		sysTemplate = (*find).second;
	}

	return sysTemplate;
}

// ------------------------------------------------------------------------------------------------
/** Create a new ParticleSystemTemplate */
// ------------------------------------------------------------------------------------------------
ParticleSystemTemplate *ParticleSystemManager::newTemplate( const AsciiString &name )
{
	ParticleSystemTemplate *sysTemplate = findTemplate(name);
	if (sysTemplate == nullptr) {
		sysTemplate = newInstance(ParticleSystemTemplate)( name );

		if (! m_templateMap.insert(std::make_pair(name, sysTemplate)).second) {
			deleteInstanceOf(sysTemplate);
			sysTemplate = nullptr;
		}
	}

	return sysTemplate;
}

// ------------------------------------------------------------------------------------------------
/** Find a particle system's parent. Should really only be called by TheScriptEngine */
// ------------------------------------------------------------------------------------------------
ParticleSystemTemplate *ParticleSystemManager::findParentTemplate( const AsciiString &name, Int parentNum ) const
{
	if (name.isEmpty()) {
		return nullptr;
	}

	TemplateMap::const_iterator begin(m_templateMap.begin());
	TemplateMap::const_iterator end(m_templateMap.end());
	for(; begin != end; ++begin) {
		ParticleSystemTemplate *sysTemplate = (*begin).second;
		if (name.compare(sysTemplate->m_slaveSystemName) == 0) {
			if (! parentNum--) {
				return sysTemplate;
			}
		}
	}

	return nullptr;	
}

// ------------------------------------------------------------------------------------------------
/** Destroy any particle systems that are attached to this object */	
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::destroyAttachedSystems( Object *obj )
{

	// sanity
	if( obj == nullptr )
		return;

	// iterate through all systems
	ParticleSystem *system = nullptr;

	for( ParticleSystemListIt it = m_allParticleSystemList.begin(); 
			 it != m_allParticleSystemList.end(); 
			 ++it ) 
	{

		system = *it;
		if( system == nullptr )
			continue;
		
		if( system->getAttachedObject() == obj->getID() )
			system->destroy();

	}

}

// ------------------------------------------------------------------------------------------------
/** Add a particle to the global particle list. */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::addParticle( Particle *particleToAdd, ParticlePriorityType priority )
{
	if (particleToAdd->m_inOverallList)
		return;

	if (!m_allParticlesHead[ priority ])
	{
		m_allParticlesHead[ priority ] = particleToAdd;
	}

	if (m_allParticlesTail[ priority ])
	{
		m_allParticlesTail[ priority ]->m_overallNext = particleToAdd;
		particleToAdd->m_overallPrev = m_allParticlesTail[ priority ];
	}
	else
	{
		particleToAdd->m_overallPrev = nullptr;
	}

	m_allParticlesTail[ priority ] = particleToAdd;
	particleToAdd->m_overallNext = nullptr;
	particleToAdd->m_inOverallList = TRUE;

	++m_particleCount;
	

}

// ------------------------------------------------------------------------------------------------
/** Remove a particle from the global particle list. */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::removeParticle( Particle *particleToRemove)
{
	if (!particleToRemove->m_inOverallList)
		return;

	// get priority of the particle we're removing
	ParticlePriorityType priority = particleToRemove->getPriority();

	// remove links from prev & next objs
	if (particleToRemove->m_overallNext)
		particleToRemove->m_overallNext->m_overallPrev = particleToRemove->m_overallPrev;
	if (particleToRemove->m_overallPrev)
		particleToRemove->m_overallPrev->m_overallNext = particleToRemove->m_overallNext;

	// update head & tail if neccessary
	if (particleToRemove == m_allParticlesHead[ priority ])
		m_allParticlesHead[ priority ] = particleToRemove->m_overallNext;
	if (particleToRemove == m_allParticlesTail[ priority ])
		m_allParticlesTail[ priority ] = particleToRemove->m_overallPrev;

	particleToRemove->m_overallNext = particleToRemove->m_overallPrev = nullptr;
	particleToRemove->m_inOverallList = FALSE;
	--m_particleCount;


}

// ------------------------------------------------------------------------------------------------
/** Add a particle system to the master particle system list. */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::friend_addParticleSystem( ParticleSystem *particleSystemToAdd )
{
	m_allParticleSystemList.push_back(particleSystemToAdd);
	++m_particleSystemCount;
}

// ------------------------------------------------------------------------------------------------
/** Remove a particle system from the master particle system list. */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::friend_removeParticleSystem( ParticleSystem *particleSystemToRemove )
{
	ParticleSystemListIt it = std::find(m_allParticleSystemList.begin(), m_allParticleSystemList.end(), particleSystemToRemove);
	if (it != m_allParticleSystemList.end()) {
		m_allParticleSystemList.erase(it);
		--m_particleSystemCount;
	}

}

// ------------------------------------------------------------------------------------------------
/** Remove the oldest N number of particles from the lowest priority lists first.  We will
 * not remove particles from any priorities higher or equal to the priorityCap parameter. */
// ------------------------------------------------------------------------------------------------
Int ParticleSystemManager::removeOldestParticles( UnsignedInt count, 
																									ParticlePriorityType priorityCap )
{
	Int countToRemove = count;

	while (count-- && getParticleCount()) 
	{
		for( Int i = PARTICLE_PRIORITY_LOWEST;
				 i < priorityCap;
				 ++i )
		{
			if( m_allParticlesHead[ i ] ) 
			{
				deleteInstanceOf(m_allParticlesHead[ i ]);
				break;  // exit for
			}
		}
	}

	// return the number of particles actually removed
	return countToRemove - count;

}

// ------------------------------------------------------------------------------------------------
/** Preload particle system textures */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::preloadAssets( TimeOfDay timeOfDay )
{
	TemplateMap::iterator begin(m_templateMap.begin());
	TemplateMap::iterator end(m_templateMap.end());

	for (; begin != end; ++begin) {
		const ParticleSystemTemplate *tmplate = (*begin).second;
		if (tmplate->m_particleType == ParticleSystemInfo::PARTICLE &&
			 	(! tmplate->m_particleTypeName.isEmpty()))
		{
			TheDisplay->preloadTextureAssets(tmplate->m_particleTypeName);
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** Output particle system statistics to the screen
 * @todo Implement a real console (MSB) */
// ------------------------------------------------------------------------------------------------
void ParticleSystemDebugDisplay( DebugDisplayInterface *dd, void *, FILE *fp )
{
	if (!dd)
		return;

	dd->setCursorPos( 0, 0 );
	dd->setRightMargin( 2 );
	
	dd->printf( "Total Particles: %d\n", TheParticleSystemManager->getParticleCount() );
	dd->printf( "Total Particles (On Screen): %d\n", TheParticleSystemManager->getOnScreenParticleCount());
	dd->printf( "Total Particle Systems: %d\n", TheParticleSystemManager->getParticleSystemCount() );

	ParticleSystemManager::ParticleSystemList list = TheParticleSystemManager->getAllParticleSystems();
	ParticleSystemManager::ParticleSystemList::iterator it;

	std::map<AsciiString, Int> templateMap;
	std::map<AsciiString, Int> templateMapParticleCount;

	std::map<AsciiString, Int>::iterator templateMapIt;
	std::map<AsciiString, Int>::iterator templateMapParticleCountIt;

	for ( it = list.begin(); it != list.end(); ++it )
	{
		AsciiString templateName = (*it)->getTemplate()->getName();
		templateMapIt = templateMap.find(templateName);
		if (templateMapIt == templateMap.end())
		{
			templateMap.insert(std::make_pair(templateName, 1));
			templateMapParticleCount.insert(std::make_pair(templateName, (*it)->getParticleCount()));
		}
		else
		{
			++templateMapIt->second;

			templateMapParticleCountIt = templateMapParticleCount.find(templateName);
			if (templateMapParticleCountIt != templateMapParticleCount.end())
				templateMapParticleCountIt->second += (*it)->getParticleCount();
		}
	}

	for (templateMapIt = templateMap.begin(); templateMapIt != templateMap.end(); ++templateMapIt)
	{
		templateMapParticleCountIt = templateMapParticleCount.find(templateMapIt->first);

		dd->printf("  %s: %d instances", templateMapIt->first.str(), templateMapIt->second);
		if (templateMapParticleCountIt != templateMapParticleCount.end())
			dd->printf("  (Avg per system %.2f)", INT_TO_REAL(templateMapParticleCountIt->second) / templateMapIt->second);
		dd->printf("\n");
	}
}
