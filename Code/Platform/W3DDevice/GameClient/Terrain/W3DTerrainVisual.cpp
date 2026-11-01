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

// FILE: W3DTerrainVisual.cpp /////////////////////////////////////////////////////////////////////
// W3D implementation details for visual aspects of terrain
// Author: Colin Day, April 2001
///////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include "Engine/ThreadRole.h"
#include <stdlib.h>
#include <windows.h>

#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/MapReaderWriterInfo.h"
#include "Common/ThingTemplate.h"
#include "Common/WellKnownKeys.h"
#include "Common/TerrainTypes.h"
#include "Common/Xfer.h"
#include "Common/QuickTrig.h"

#include "GameClient/Drawable.h"
#include "GameClient/ClientRandomValue.h"

#include "GameLogic/Object.h"
#include "GameLogic/GameLogic.h"

#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DTerrainVisual.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/W3DWater.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DDebugIcons.h"
#include "W3DDevice/GameClient/W3DTerrainTracks.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DSmudge.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "WW3D2/Scene/light.h"
#include "WW3D2/Mesh/rendobj.h"
#include "WW3D2/Scene/coltype.h"
#include "WW3D2/Scene/coltest.h"
#include "WW3D2/System/assetmgr.h"



class TestSeismicFilter : public SeismicSimulationFilterBase
{

  virtual SeismicSimStatusCode filterCallback( WorldHeightMapInterfaceClass *heightMap, const SeismicSimulationNode *node ) override
  {


    Int life = node->m_life;

    if ( heightMap == nullptr )
      return SeismicSimStatusCode::SEISMIC_STATUS_INVALID;


    if ( life == 0 )
      return SeismicSimStatusCode::SEISMIC_STATUS_ACTIVE;
    if ( life < 15 )
    {
      // ADD HEIGHT BECAUSE THE EXPLOSION IS PUSHING DIRT UP

      Real magnitude = node->m_magnitude;

      Real offsScalar =  magnitude / static_cast<Real>(life); // real-life, get it?
      Int radius = node->m_radius;
      Int border = heightMap->getBorderSize();
      Int centerX = node->m_center.x + border ;
      Int centerY = node->m_center.y + border ;
  
      UnsignedInt workspaceWidth = radius*2;
      Real *workspace = NEW( Real[ sqr(workspaceWidth) ] );
      Real *workspaceEnd = workspace + sqr(workspaceWidth);


      for ( Real *t = workspace; t < workspaceEnd; ++t ) *t = 0.0f;// clear the workspace

      Int x;
      for (x = 0; x < radius; ++x)
      {
        for (Int y = 0; y < radius; ++y)
        {

          Real distance = static_cast<Real>(sqrt( sqr(x) + sqr(y) ));//Pythagoras
      
          if ( distance < radius )
          {
            Real distScalar = cos( ( distance / radius * (PI/2) ) );
            Real height = (offsScalar * distScalar); 

            workspace[ (radius + x) +  workspaceWidth * (radius + y) ] = height + heightMap->getBilinearSampleSeismicZVelocity( centerX + x,  centerY + y ) ;//kaleidoscope
            
            if ( x != 0 ) // non-zero test prevents cross-shaped double stamp 
            {
      			  workspace[ (radius - x) + workspaceWidth * (radius + y) ] = height + heightMap->getBilinearSampleSeismicZVelocity( centerX - x,  centerY + y ) ;
              if ( y != 0 )
                workspace[ (radius - x) + workspaceWidth * (radius - y) ] =  height + heightMap->getBilinearSampleSeismicZVelocity( centerX - x,  centerY - y ) ;
            }
            if ( y != 0 )
      			  workspace[ (radius + x) + workspaceWidth * (radius - y) ] = height + heightMap->getBilinearSampleSeismicZVelocity( centerX + x,  centerY - y ) ;
          }
        }
      }

      // stuff the values from the workspace into the heightmap's velocities
      for (x = 0; static_cast<UnsignedInt>(x) < workspaceWidth; ++x)
        for (Int y = 0; static_cast<UnsignedInt>(y) < workspaceWidth; ++y)
    			heightMap->setSeismicZVelocity( centerX - radius + x, centerY - radius + y,  workspace[  x + workspaceWidth * y ]  );

      delete [] workspace;

      return SeismicSimStatusCode::SEISMIC_STATUS_ACTIVE;
    }
    else
      return SeismicSimStatusCode::SEISMIC_STATUS_ZERO_ENERGY;
  }

  virtual Real applyGravityCallback( Real velocityIn ) override
  {
    Real velocityOut = velocityIn;
    velocityOut -= 1.5f;
    return velocityOut;
  }


};
static TestSeismicFilter testSeismicFilter;




//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DTerrainVisual::W3DTerrainVisual()
{

	m_terrainRenderObject = nullptr;
	m_waterRenderObject = nullptr;
	::Engine::context().waterRenderObj = nullptr;
	
  m_logicHeightMap   = nullptr;
  










}  // end W3DTerrainVisual

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DTerrainVisual::~W3DTerrainVisual()
{
	// release our render object
	if (TheTerrainRenderObject == m_terrainRenderObject) {
		::Engine::context().terrainRenderObject = nullptr;
	}

	if (TheTerrainTracksRenderObjClassSystem)
	{
		delete TheTerrainTracksRenderObjClassSystem;
		::Engine::context().terrainTracksRenderObjClassSystem=nullptr;
	}


	if (TheW3DShadowManager)
	{	
		delete TheW3DShadowManager;
		::Engine::context().w3DShadowManager=nullptr;
	}

	if (TheSmudgeManager)
	{
		delete TheSmudgeManager;
		::Engine::context().smudgeManager=nullptr;
	}

	REF_PTR_RELEASE( m_waterRenderObject );
	::Engine::context().waterRenderObj=nullptr;
	REF_PTR_RELEASE( m_terrainRenderObject );
	REF_PTR_RELEASE( m_logicHeightMap );

}  // end ~W3DTerrainVisual

//-------------------------------------------------------------------------------------------------
/** init */
//-------------------------------------------------------------------------------------------------
void W3DTerrainVisual::init( void )
{

	// extend
	TerrainVisual::init();
	// create a new render object for W3D
	m_terrainRenderObject = NEW_REF( HeightMapRenderObjClass, () );
	m_terrainRenderObject->Set_Collision_Type( PICK_TYPE_TERRAIN );
	::Engine::context().terrainRenderObject = m_terrainRenderObject;

	// initialize track drawing system
	::Engine::context().terrainTracksRenderObjClassSystem = NEW TerrainTracksRenderObjClassSystem;
	TheTerrainTracksRenderObjClassSystem->init(W3DDisplay::m_3DScene);


	// initialize object shadow drawing system
	::Engine::context().w3DShadowManager = NEW W3DShadowManager;
 	TheW3DShadowManager->init();
	
	// create a water plane render object
	::Engine::context().waterRenderObj=m_waterRenderObject = NEW_REF( WaterRenderObjClass, () );
	m_waterRenderObject->init(TheGlobalData->m_waterPositionZ, TheGlobalData->m_waterExtentX, TheGlobalData->m_waterExtentY, W3DDisplay::m_3DScene, static_cast<WaterRenderObjClass::WaterType>(TheGlobalData->m_waterType));	//create a water plane that's 128x128 units
	m_waterRenderObject->Set_Position(Vector3(TheGlobalData->m_waterPositionX,TheGlobalData->m_waterPositionY,TheGlobalData->m_waterPositionZ));	//place water in world

	// create smudge rendering system.
	::Engine::context().smudgeManager = NEW(W3DSmudgeManager);
	TheSmudgeManager->init();

		if (TheGlobalData->m_waterType == WaterRenderObjClass::WATER_TYPE_1_FB_REFLECTION)
		{	// add water render object to the pre-pass scene (to be rendered before main scene)
 			//W3DDisplay::m_prePass3DScene->Add_Render_Object( m_waterRenderObject);
		}
		else
		{	// add water render object to the post-pass scene (to be rendered after main scene)
			W3DDisplay::m_3DScene->Add_Render_Object( m_waterRenderObject);
		}
	if (TheGlobalData->m_useCloudPlane)
		m_waterRenderObject->toggleCloudLayer(true);
	else
		m_waterRenderObject->toggleCloudLayer(false);

	// set the vertex animated water properties
	Int waterSettingIndex = 0;  // use index 0 settings by default
	TheTerrainVisual->setWaterGridHeightClamps( nullptr, 
																							TheGlobalData->m_vertexWaterHeightClampLow[ waterSettingIndex ], 
																							TheGlobalData->m_vertexWaterHeightClampHi[ waterSettingIndex ] );
	TheTerrainVisual->setWaterTransform( nullptr, 
																			 TheGlobalData->m_vertexWaterAngle[ waterSettingIndex ], 
																			 TheGlobalData->m_vertexWaterXPosition[ waterSettingIndex ], 
																			 TheGlobalData->m_vertexWaterYPosition[ waterSettingIndex ], 
																			 TheGlobalData->m_vertexWaterZPosition[ waterSettingIndex ] );
	TheTerrainVisual->setWaterGridResolution( nullptr, 
																						static_cast<Real>(TheGlobalData->m_vertexWaterXGridCells[ waterSettingIndex ]), 
																						static_cast<Real>(TheGlobalData->m_vertexWaterYGridCells[ waterSettingIndex ]), 
																						TheGlobalData->m_vertexWaterGridSize[ waterSettingIndex ] );
	TheTerrainVisual->setWaterAttenuationFactors( nullptr, 
																								TheGlobalData->m_vertexWaterAttenuationA[ waterSettingIndex ], 
																								TheGlobalData->m_vertexWaterAttenuationB[ waterSettingIndex ], 
																								TheGlobalData->m_vertexWaterAttenuationC[ waterSettingIndex ], 
																								TheGlobalData->m_vertexWaterAttenuationRange[ waterSettingIndex ] );	
	m_isWaterGridRenderingEnabled = FALSE;

}  // end init

//-------------------------------------------------------------------------------------------------
/** reset */
//-------------------------------------------------------------------------------------------------
void W3DTerrainVisual::reset( void )
{

	// extend
	TerrainVisual::reset();

	m_terrainRenderObject->reset();

	if (TheW3DShadowManager)
		TheW3DShadowManager->Reset();

	if (TheSmudgeManager)
		TheSmudgeManager->reset();

	if (TheTerrainTracksRenderObjClassSystem)
		TheTerrainTracksRenderObjClassSystem->Reset();

	// reset water render object if present
	if( m_waterRenderObject )
	{
		for (Int i=0; i<5; i++)
		{
			//check if this texture was ever changed from default
			if (m_currentSkyboxTexNames[i] != m_initialSkyboxTexNames[i])
			{
				m_waterRenderObject->replaceSkyboxTexture(m_currentSkyboxTexNames[i], m_initialSkyboxTexNames[i]);
				m_currentSkyboxTexNames[i]=m_initialSkyboxTexNames[i];	//update current state to new texture
			}
		}

		m_waterRenderObject->reset();
	}

}  // end reset

//-------------------------------------------------------------------------------------------------
/** update */
//-------------------------------------------------------------------------------------------------
void W3DTerrainVisual::update( void )
{

	// extend
	TerrainVisual::update();

	// if we have a water render object, it has an update method
	if( m_waterRenderObject )
		m_waterRenderObject->update();


}  // end update


  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



//-------------------------------------------------------------------------------------------------
/** load method for W3D visual terrain */
//-------------------------------------------------------------------------------------------------
Bool W3DTerrainVisual::load( AsciiString filename )
{
	

	// enhancing functionality specific for W3D terrain
	if( TerrainVisual::load( filename ) == FALSE )
		return FALSE;  // failed

	// open the terrain file
	CachedFileInputStream fileStrm;
	if( !fileStrm.open(filename) )
	{

		REF_PTR_RELEASE( m_terrainRenderObject );
		return FALSE;

	}  // end if

	if( m_terrainRenderObject == nullptr )
		return FALSE;


  ChunkInputStream *pStrm = &fileStrm;

  // allocate new height map data to read from file
  REF_PTR_RELEASE( m_logicHeightMap );
	m_logicHeightMap = NEW WorldHeightMap(pStrm);
	if (m_logicHeightMap->isLoadFailed()) {
		REF_PTR_RELEASE(m_logicHeightMap);
		throw ERROR_CORRUPT_FILE_FORMAT;
	}











	// Add any lights loaded by map.
	MapObject *pMapObj = MapObject::getFirstMapObject();
	while (pMapObj) 
	{
		Dict *d = pMapObj->getProperties();
		if (pMapObj->isLight()) 
		{ 
			Coord3D loc = *pMapObj->getLocation();
			if (loc.z < 0) {
				Vector3 vec;
				loc.z = m_terrainRenderObject->getHeightMapHeight(loc.x, loc.y, nullptr);
				loc.z += d->getReal(TheKey_lightHeightAboveTerrain);
			}
			// It is a light, and handled at the device level.  jba.
			LightClass* lightP = NEW_REF(LightClass, (LightClass::POINT));

			RGBColor c;
			c.setFromInt(d->getInt(TheKey_lightAmbientColor));
			lightP->Set_Ambient( Vector3( c.red, c.green, c.blue ) );

			c.setFromInt(d->getInt(TheKey_lightDiffuseColor));
			lightP->Set_Diffuse( Vector3(  c.red, c.green, c.blue) );

			lightP->Set_Position(Vector3(loc.x, loc.y, loc.z));

			lightP->Set_Far_Attenuation_Range(d->getReal(TheKey_lightInnerRadius), d->getReal(TheKey_lightOuterRadius));
 			W3DDisplay::m_3DScene->Add_Render_Object(lightP);
			REF_PTR_RELEASE( lightP );
		}
		pMapObj = pMapObj->getNext();
	}


	RefRenderObjListIterator *it = W3DDisplay::m_3DScene->createLightsIterator();
	// apply the heightmap to the terrain render object

	m_terrainRenderObject->initHeightData( m_logicHeightMap->getDrawWidth(), 
																				 m_logicHeightMap->getDrawHeight(),
																				 m_logicHeightMap,
																				 it);


	if (it) {
	 W3DDisplay::m_3DScene->destroyLightsIterator(it);
	 it = nullptr;
	}
	// add our terrain render object to the scene
	W3DDisplay::m_3DScene->Add_Render_Object( m_terrainRenderObject );

#if defined _DEBUG
	// Icon drawing utility object for pathfinding.
	W3DDebugIcons *icons = NEW W3DDebugIcons;
 	W3DDisplay::m_3DScene->Add_Render_Object( icons );
	icons->Release_Ref(); // belongs to scene.
#endif

	if (m_waterRenderObject)
	{
		W3DDisplay::m_3DScene->Add_Render_Object( m_waterRenderObject);
		m_waterRenderObject->enableWaterGrid(false);
		m_waterRenderObject->updateMapOverrides();
	}

	pMapObj = MapObject::getFirstMapObject();
	while (pMapObj) 
	{
		Dict *d = pMapObj->getProperties();
		if (pMapObj->isScorch()) {
			const Coord3D *pos = pMapObj->getLocation();
			Vector3 loc(pos->x, pos->y, pos->z);
			Real radius = d->getReal(TheKey_objectRadius);
			Scorches type = static_cast<Scorches>(d->getInt(TheKey_scorchType));
			m_terrainRenderObject->addScorch(loc, radius, type);
		}
		pMapObj = pMapObj->getNext();
	}

	// reset water render object if present
	if( m_waterRenderObject )
	{
		m_waterRenderObject->load();
	}

	return TRUE;  // success

}  // end load

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::enableWaterGrid( Bool enable )
{

	//Get default water type
	m_isWaterGridRenderingEnabled = enable;

	// make the changes in the water render object
	if( m_waterRenderObject )
		m_waterRenderObject->enableWaterGrid( enable );

}  // end enableWaterGrid

//-------------------------------------------------------------------------------------------------
/** intersect the ray with the terrain, if a hit occurs TRUE is returned
	* and the result point on the terrain is returned in "result" */
//-------------------------------------------------------------------------------------------------
Bool W3DTerrainVisual::intersectTerrain( Coord3D *rayStart, 
																				 Coord3D *rayEnd, 
																				 Coord3D *result )
{
	Bool hit = FALSE;

	// sanity
	if( rayStart == nullptr || rayEnd == nullptr )
		return hit;

	if( m_terrainRenderObject )
	{
		CastResultStruct res;
		LineSegClass lineSeg( Vector3( rayStart->x, rayStart->y, rayStart->z ),
													Vector3( rayEnd->x, rayEnd->y, rayEnd->z ) );
		RayCollisionTestClass rayTest( lineSeg, &res );

		hit = m_terrainRenderObject->Cast_Ray( rayTest );
		if( hit && result )
		{
			Vector3 point = rayTest.Result->ContactPoint;

			result->x = point.X;
			result->y = point.Y;
			result->z = point.Z;	

		}  // end if
		
	}  // end if

	// return hit result
	return hit;

}  // end intersectTerrain

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DTerrainVisual::getTerrainColorAt( Real x, Real y, RGBColor *pColor )
{

  if( m_logicHeightMap )
		m_logicHeightMap->getTerrainColorAt( x, y, pColor );

}  // end getTerrainColorAt

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
TerrainType *W3DTerrainVisual::getTerrainTile( Real x, Real y )
{
	TerrainType *tile = nullptr;


  if( m_logicHeightMap )
	{
		AsciiString tileName = m_logicHeightMap->getTerrainNameAt( x, y );
		tile = TheTerrainTypes->findTerrain( tileName );
	}  // end if




	return tile;

}  // end getTerrainTile

// ------------------------------------------------------------------------------------------------
/** set min/max height values allowed in water grid pointed to by waterTable */
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::setWaterGridHeightClamps( const WaterHandle *waterTable, 
																								 Real minZ, Real maxZ )
{

	if( m_waterRenderObject )
		m_waterRenderObject->setGridHeightClamps( minZ, maxZ );

}  // end setWaterGridHeightClamps

// ------------------------------------------------------------------------------------------------
/** adjust fallof parameters for grid change method */
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::setWaterAttenuationFactors( const WaterHandle *waterTable, 
																									 Real a, Real b, Real c, Real range )
{

	if( m_waterRenderObject )
		m_waterRenderObject->setGridChangeAttenuationFactors( a, b, c, range );

}  // end setWaterAttenuationFactors

// ------------------------------------------------------------------------------------------------
/** set the water table position and orientation in world space */
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::setWaterTransform( const WaterHandle *waterTable, 
																					Real angle, Real x, Real y, Real z )
{

	if( m_waterRenderObject )
		m_waterRenderObject->setGridTransform( angle, x, y, z );

}  // end setWaterTransform

// ------------------------------------------------------------------------------------------------
/** set water table transform by matrix */
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::setWaterTransform( const Matrix3D *transform )
{

	if( m_waterRenderObject )
		m_waterRenderObject->setGridTransform( transform );
			
}  // end setWaterTransform

// ------------------------------------------------------------------------------------------------
/** get the water transform matrix */
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::getWaterTransform( const WaterHandle *waterTable, Matrix3D *transform )
{

	if( m_waterRenderObject )
		m_waterRenderObject->getGridTransform( transform );

}  // end getWaterTransform

// ------------------------------------------------------------------------------------------------
/** water grid resolution spacing */
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::setWaterGridResolution( const WaterHandle *waterTable,
																							 Real gridCellsX, Real gridCellsY, Real cellSize )
{

	if( m_waterRenderObject )
		m_waterRenderObject->setGridResolution( gridCellsX, gridCellsY, cellSize );

}  // end setWaterGridResolution

// ------------------------------------------------------------------------------------------------
/** get water grid resolution spacing */
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::getWaterGridResolution( const WaterHandle *waterTable,	
																							 Real *gridCellsX, Real *gridCellsY, Real *cellSize )
{

	if( m_waterRenderObject )
		m_waterRenderObject->getGridResolution( gridCellsX, gridCellsY, cellSize );

}  // end getWaterGridResolution

// ------------------------------------------------------------------------------------------------
/** adjust the water grid in world coords by the delta */
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::changeWaterHeight( Real x, Real y, Real delta )
{

	if( m_waterRenderObject )
		m_waterRenderObject->changeGridHeight( x, y, delta );

}  // end changeWaterHeight

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::addWaterVelocity( Real worldX, Real worldY, 
																				 Real velocity, Real preferredHeight )
{

	if( m_waterRenderObject )
		m_waterRenderObject->addVelocity( worldX, worldY, velocity, preferredHeight );

}  // end addWaterVelocity

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool W3DTerrainVisual::getWaterGridHeight( Real worldX, Real worldY, Real *height)
{
	Real gridX, gridY;

	if (m_isWaterGridRenderingEnabled && m_waterRenderObject &&
		m_waterRenderObject->worldToGridSpace(worldX, worldY, gridX, gridY))
	{	//point falls within grid, return correct height
		m_waterRenderObject->getGridVertexHeight(REAL_TO_INT(gridX),REAL_TO_INT(gridY),height);
		return TRUE;
	}
	return FALSE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::setRawMapHeight(const ICoord2D *gridPos, Int height)
{
	ASSERT_MAIN_THREAD();	// the logic still calls this directly; must move to the sync point
  // Writes to m_logicHeightMap.

	if (m_logicHeightMap) 
  {
		Int x = gridPos->x+m_logicHeightMap->getBorderSizeInline();
		Int y = gridPos->y+m_logicHeightMap->getBorderSizeInline();
 		//if (m_logicHeightMap->getHeight(x,y) != height) //ML changed to prevent scissoring with roads
 		if (m_logicHeightMap->getHeight(x,y) > height) 
		{
			m_logicHeightMap->setRawHeight(x, y, static_cast<UnsignedByte>(height));
			IRegion2D range;
			range.lo.x = x;
			range.lo.y = y;
			range.hi.x = x + 1;
			range.hi.y = y + 1;
			m_terrainRenderObject->heightsChanged(range);


		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Int W3DTerrainVisual::getRawMapHeight(const ICoord2D *gridPos)
{
	if (m_logicHeightMap) 
  {
		Int x = gridPos->x+m_logicHeightMap->getBorderSizeInline();
		Int y = gridPos->y+m_logicHeightMap->getBorderSizeInline();
 		//if (m_logicHeightMap->getHeight(x,y) != height) //ML changed to prevent scissoring with roads
    return m_logicHeightMap->getHeight(x,y) ;
	}
  return 0;

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::addFactionBibDrawable(Drawable *factionBuilding, Bool highlight, Real extra)
{
	if (m_logicHeightMap)
  {
		const Matrix3D * mtx = factionBuilding->getTransformMatrix();
		Vector3 corners[4];
		Coord3D pos;
		pos.set(0,0,0);
		Real exitWidth = factionBuilding->getTemplate()->getFactoryExitWidth();
		Real extraWidth = factionBuilding->getTemplate()->getFactoryExtraBibWidth() + extra;
		const GeometryInfo info = factionBuilding->getTemplate()->getTemplateGeometryInfo();
		Real sizeX = info.getMajorRadius();
		Real sizeY = info.getMinorRadius();
		if (info.getGeomType() != GEOMETRY_BOX) {
			sizeY = sizeX;
		}
		corners[0].Set(pos.x, pos.y, pos.z);
		corners[0].X -= sizeX+extraWidth;
		corners[0].Y -= sizeY+extraWidth;
		corners[1].Set(pos.x, pos.y, pos.z);
		corners[1].X += sizeX+exitWidth+extraWidth;
		corners[1].Y -= sizeY+extraWidth;
		corners[2].Set(pos.x, pos.y, pos.z);
		corners[2].X += sizeX+exitWidth+extraWidth;
		corners[2].Y += sizeY+extraWidth;
		corners[3].Set(pos.x, pos.y, pos.z);
		corners[3].X -= sizeX+extraWidth;
		corners[3].Y += sizeY+extraWidth;
		mtx->Transform_Vector(*mtx, corners[0], &corners[0]);
		mtx->Transform_Vector(*mtx, corners[1], &corners[1]);
		mtx->Transform_Vector(*mtx, corners[2], &corners[2]);
		mtx->Transform_Vector(*mtx, corners[3], &corners[3]);
		m_terrainRenderObject->addTerrainBibDrawable(corners, factionBuilding->getID(), highlight);
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::addFactionBib(Object *factionBuilding, Bool highlight, Real extra)
{
	if (m_logicHeightMap) 
  {
		const Matrix3D * mtx = factionBuilding->getTransformMatrix();
		Vector3 corners[4];
		Coord3D pos;
		pos.set(0,0,0);
		Real exitWidth = factionBuilding->getTemplate()->getFactoryExitWidth();
		Real extraWidth = factionBuilding->getTemplate()->getFactoryExtraBibWidth() + extra;
		const GeometryInfo info = factionBuilding->getGeometryInfo();
		Real sizeX = info.getMajorRadius();
		Real sizeY = info.getMinorRadius();
		if (info.getGeomType() != GEOMETRY_BOX) {
			sizeY = sizeX;
		}
		corners[0].Set(pos.x, pos.y, pos.z);
		corners[0].X -= sizeX+extraWidth;
		corners[0].Y -= sizeY+extraWidth;
		corners[1].Set(pos.x, pos.y, pos.z);
		corners[1].X += sizeX+exitWidth+extraWidth;
		corners[1].Y -= sizeY+extraWidth;
		corners[2].Set(pos.x, pos.y, pos.z);
		corners[2].X += sizeX+exitWidth+extraWidth;
		corners[2].Y += sizeY+extraWidth;
		corners[3].Set(pos.x, pos.y, pos.z);
		corners[3].X -= sizeX+extraWidth;
		corners[3].Y += sizeY+extraWidth;
		mtx->Transform_Vector(*mtx, corners[0], &corners[0]);
		mtx->Transform_Vector(*mtx, corners[1], &corners[1]);
		mtx->Transform_Vector(*mtx, corners[2], &corners[2]);
		mtx->Transform_Vector(*mtx, corners[3], &corners[3]);
		m_terrainRenderObject->addTerrainBib(corners, factionBuilding->getID(), highlight);
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::removeFactionBibDrawable(Drawable *factionBuilding)
{
	if (m_terrainRenderObject) {
		m_terrainRenderObject->removeTerrainBibDrawable(factionBuilding->getID());
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::removeFactionBib(Object *factionBuilding)
{
	if (m_terrainRenderObject) {
		m_terrainRenderObject->removeTerrainBib(factionBuilding->getID());
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::removeAllBibs(void)
{
	if (m_terrainRenderObject) {
		m_terrainRenderObject->removeAllTerrainBibs();
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::removeBibHighlighting(void)
{
	if (m_terrainRenderObject) {
		m_terrainRenderObject->removeTerrainBibHighlighting();
	}
}
 
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::removeTreesAndPropsForConstruction(const Coord3D* pos, 
																				const GeometryInfo& geom,
																				Real angle)
{
	if (m_terrainRenderObject) {
		m_terrainRenderObject->removeTreesAndPropsForConstruction(pos, geom, angle);
	}
}
 
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::addProp(const ThingTemplate *tTemplate, const Coord3D *pos, Real angle)
{
	ModelConditionFlags state;
	state.clear();
	if (TheGlobalData->m_weather == WEATHER_SNOWY)
	{
		state.set(MODELCONDITION_SNOW);
	}
	if (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT)
	{
		state.set(MODELCONDITION_NIGHT);
	}
	AsciiString modelName;
	Real scale = tTemplate->getAssetScale();
	const ModuleInfo& mi = tTemplate->getDrawModuleInfo();
	if (mi.getCount() > 0)
	{
		const ModuleData* mdd = mi.getNthData(0);
		const W3DModelDrawModuleData* md = mdd ? mdd->getAsW3DModelDrawModuleData() : nullptr;
		if (md)
		{
			modelName = md->getBestModelNameForWB(state);
		}
	}
	if (m_terrainRenderObject && modelName.isNotEmpty()) {
		m_terrainRenderObject->addProp(1, *pos, angle, scale, modelName);
	}
}
 
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::setTerrainTracksDetail(void)
{
	if (TheTerrainTracksRenderObjClassSystem)
		TheTerrainTracksRenderObjClassSystem->setDetail();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::setShoreLineDetail(void)
{
	if (m_terrainRenderObject) 
		m_terrainRenderObject->setShoreLineDetail();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/// Replace the skybox texture
void W3DTerrainVisual::replaceSkyboxTextures(const AsciiString *oldTexName[5], const AsciiString *newTexName[5])
{
	if (m_waterRenderObject)
	{
		for (Int i=0; i<5; i++)
		{
			//check if this texture was never changed before and is still using the default art.
			if (m_initialSkyboxTexNames[i].isEmpty())
			{	m_initialSkyboxTexNames[i]=*oldTexName[i];
				m_currentSkyboxTexNames[i]=*oldTexName[i];
			}

			if (m_currentSkyboxTexNames[i] != *newTexName[i])
			{	m_waterRenderObject->replaceSkyboxTexture(m_currentSkyboxTexNames[i], *newTexName[i]);
				m_currentSkyboxTexNames[i]=*newTexName[i];	//update current state to new texture
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::crc( Xfer *xfer )
{

	// extend base class
	TerrainVisual::crc( xfer );

}  // end CRC

// ------------------------------------------------------------------------------------------------
/** Xfer
	* Version Info:
	* 1: Initial version 
	* 2: Add height map heights.
	* 3: Add client side trees & props. jba.
*/
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	TerrainVisual::xfer( xfer );

	// flag for whether or not the water grid is enabled
	Bool gridEnabled = m_isWaterGridRenderingEnabled;
	xfer->xferBool( &gridEnabled );
	if( gridEnabled != m_isWaterGridRenderingEnabled )
	{

		DEBUG_CRASH(( "W3DTerrainVisual::xfer - m_isWaterGridRenderingEnabled mismatch\n" ));
		throw SaveCode::SC_INVALID_DATA;

	}  // end if

	// xfer grid data if enabled
	if( gridEnabled )
		xfer->xferSnapshot( m_waterRenderObject );

/*
	{

		// grid width and height
		Int width = getGridWidth();
		Int height = getGridheight();
		xfer->xferInt( &width );
		xfer->xferInt( &height );
		if( width != getGridWidth() )
		{

			DEBUG_CRASH(( "W3DTerainVisual::xfer - grid width mismatch '%d' should be '%d'\n",
										width, getGridWidth() ));
			throw SC_INVALID_DATA;

		}  // end if
		if( height != getGridHeight() )
		{

			DEBUG_CRASH(( "W3DTerainVisual::xfer - grid height mismatch '%d' should be '%d'\n",
										height, getGridHeight() ));
			throw SC_INVALID_DATA;

		}  // end if

		// write data for each grid

	}  // end if
*/

	// Write out the terrain height data.
	if (version >= 2) {
		UnsignedByte *data = m_logicHeightMap->getDataPtr();
		Int len = m_logicHeightMap->getXExtent()*m_logicHeightMap->getYExtent();
		Int xferLen = len;
		xfer->xferInt(&xferLen);
		if (len!=xferLen) {
			DEBUG_CRASH(("Bad height map length."));
			if (len>xferLen) {
				len = xferLen;
			}
		}
		xfer->xferUser(data, len);	
		if (xfer->getXferMode() == XFER_LOAD)	
    {	
			// Update the display height map.
			m_terrainRenderObject->staticLightingChanged();
		}
	}

	if (version >= 3) {
		xfer->xferSnapshot(m_terrainRenderObject);
	}



         // XFER //
         // X  R //
         // X  R //
         // XFER //
 /////   /USE CLIENT HEIGHT MAPCLIENT HEIGHT MAP
//   //  /USE CLIENT HEIGHT MAPCLIENT HEIGHT MAP
//    ////USE CLIENT HEIGHT MAPCLIENT HEIGHT MAP----------------------------
//   //  /USE CLIENT HEIGHT MAPCLIENT HEIGHT MAP                            ^
 /////   /USE CLIENT HEIGHT MAPCLIENT HEIGHT MAP                            0
         // XFER //
         // X  R //
         // X  R //
         // XFER //





}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::loadPostProcess( void )
{

	// extend base class
	TerrainVisual::loadPostProcess();

}  // end loadPostProcess

