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


// WorldHeightMap.h
// Class to encapsulate height map.
// Author: John Ahlquist, April 2001

#pragma once

#ifndef WorldHeightMap_H
#define WorldHeightMap_H

#include "Lib/BaseType.h"
#include "WWLib/refcount.h"
#include "WWMath/vector3.h"
#include "W3DDevice/GameClient/TileData.h"
#include "Common/MapObject.h"

#include "Common/STLTypedefs.h"
using VecICoord2D = std::vector<ICoord2D>;


/** MapObject class 
Not ref counted.  Do not store pointers to this class.  */

constexpr auto K_MIN_HEIGHT = 0;
constexpr auto K_MAX_HEIGHT = 255;

constexpr auto NUM_SOURCE_TILES = 1024;
constexpr auto NUM_BLEND_TILES = 16192;
constexpr auto NUM_CLIFF_INFO = 32384;
constexpr auto FLAG_VAL = 0x7ADA0000;

// For backwards compatiblity.
#define TEX_PATH_LEN 256


/// Struct in memory.
typedef struct {
	Int globalTextureClass; 
	Int firstTile;
	Int numTiles;
	Int width;
	Int isBlendEdgeTile; ///< True if the texture contains blend edges.
	AsciiString name;
	ICoord2D positionInTexture;
} TXTextureClass;

typedef enum {POS_X, POS_Y, NEG_X, NEG_Y} TVDirection;
/// Struct in memory.
typedef struct {
	Real	u0, v0;	 // Upper left uv
	Real	u1, v1;	 // Lower left uv
	Real	u2, v2;	 // Lower right uv
	Real	u3, v3;	 // Upper right uv
	Bool  flip;
	Bool	mutant;  // Mutant mapping needed to get this to fit.
	Short tileIndex; // Tile texture.
} TCliffInfo;

constexpr auto NUM_TEXTURE_CLASSES = 256;


class TextureClass;
class ChunkInputStream;
class InputStream;
class OutputStream;
class DataChunkInput;
struct DataChunkInfo;
class AlphaEdgeTextureClass;
class TerrainTextureClass;
class AlphaTerrainTextureClass;

class WorldHeightMap : public RefCountClass,
                       public WorldHeightMapInterfaceClass
{
	friend class TerrainTextureClass;
	friend class AlphaTerrainTextureClass;
	friend class W3DCustomEdging;
	friend class AlphaEdgeTextureClass;

#define NO_EVAL_TILING_MODES

public:
#ifdef EVAL_TILING_MODES
	enum {TILE_4x4, TILE_6x6, TILE_8x8} m_tileMode;
#endif
	enum {
		NORMAL_DRAW_WIDTH = 129,
		NORMAL_DRAW_HEIGHT = 129,
		STRETCH_DRAW_WIDTH = 65,
		STRETCH_DRAW_HEIGHT = 65,
		DEEP_DRAW_SIZE = 257		///< minimum edge of the draw window, see getDefaultDrawSize()
	};
	/// Widescreen: width of the terrain draw window in cells; NORMAL_DRAW_WIDTH at 4:3, grown
	/// proportionally to the Hor+ factor on wider displays (rounded up to whole 32-cell vertex buffer tiles).
	static Int getDefaultDrawWidth(void);
	/// Edge of the square draw window: DEEP_DRAW_SIZE, but at least getDefaultDrawWidth(). Square because
	/// the screen width lies along world Y when the camera is rotated.
	static Int getDefaultDrawSize(void);

protected:
	Int m_width;				///< Height map width.
	Int m_height;				///< Height map height (y size of array).
	Int m_borderSize;		///< Non-playable border area.
	VecICoord2D m_boundaries;	///< the in-game boundaries
	Int m_dataSize;			///< size of m_data.
	UnsignedByte *m_data;	///< array of z(height) values in the height map.
	
  UnsignedByte *m_seismicUpdateFlag;  ///< array of bits to prevent ovelapping physics-update regions from doubling effects on shared cells
  UnsignedInt   m_seismicUpdateWidth; ///< width of the array holding SeismicUpdateFlags
  Real         *m_seismicZVelocities; ///< how fast is the dirt rising/falling at this location

  UnsignedByte *m_cellFlipState;	///< array of bits to indicate the flip state of each cell.
	Int m_flipStateWidth;			///< with of the array holding cellFlipState
	UnsignedByte *m_cellCliffState;	///< array of bits to indicate the cliff state of each cell.


	/// Texture indices.
	Short  *m_tileNdxes;  ///< matches m_Data, indexes into m_SourceTiles.
	Short  *m_blendTileNdxes;  ///< matches m_Data, indexes into m_blendedTiles.  0 means no blend info.	
	Short  *m_cliffInfoNdxes;  ///< matches m_Data, indexes into m_cliffInfo.	 0 means no cliff info.
	Short  *m_extraBlendTileNdxes;  ///< matches m_Data, indexes into m_extraBlendedTiles.  0 means no blend info.	

	
	Bool m_loadFailed = FALSE;	///< the file could not be read (see isLoadFailed)
	Int m_numBitmapTiles;	// Number of tiles initialized from bitmaps in m_SourceTiles.
	Int m_numEdgeTiles;	// Number of tiles initialized from bitmaps in m_SourceTiles.
	Int m_numBlendedTiles;	// Number of blended tiles created from bitmap tiles.

	TileData			*m_sourceTiles[NUM_SOURCE_TILES];	///< Tiles for m_textureClasses
	TileData			*m_edgeTiles[NUM_SOURCE_TILES];	///< Tiles for m_textureClasses
	/// Relief of m_sourceTiles (normals and gloss from <texture>_n.dds), only with the terrainnormal effect.
	TileData			*m_reliefTiles[NUM_SOURCE_TILES] = {};
	/// Heights of m_sourceTiles (<texture>_h.dds), only with the terraintess effect.
	TileData			*m_heightTiles[NUM_SOURCE_TILES] = {};

	TBlendTileInfo	m_blendedTiles[NUM_BLEND_TILES];
	TBlendTileInfo	m_extraBlendedTiles[NUM_BLEND_TILES];

	TCliffInfo	m_cliffInfo[NUM_CLIFF_INFO];
	Int m_numCliffInfo; ///< Number of cliffInfo's used in m_cliffInfo.

	// Texture classes.  There is one texture class for each bitmap read in.
	// A class may have more than one tile.  For example, if the grass bitmap is 
	// 128x128, it creates 4 64x64 tiles, so the grass texture class will have 4 tiles.
	int m_numTextureClasses;
	TXTextureClass m_textureClasses[NUM_TEXTURE_CLASSES];

	// Edge Texture classes.  There is one texture class for each bitmap read in.
	// An edge class will normally have 4 tiles.
	int m_numEdgeTextureClasses;
	TXTextureClass m_edgeTextureClasses[NUM_TEXTURE_CLASSES];

	/** The actual texture used to render the 3d mesh.  Note that it is 
	 basically m_SourceTiles laid out in rows, so by itself it is not useful.
	 Use GetUVData to get the mapping info for height cells to map into the 
	 texture. */
	TerrainTextureClass *m_terrainTex;
	Int	m_terrainTexHeight; /// Height of m_terrainTex allocated.
	/// Relief atlas in the layout of m_terrainTex (nullptr: no relief tiles loaded).
	TerrainTextureClass *m_terrainReliefTex = nullptr;
	/// Height atlas of the terrain tessellation in the layout of m_terrainTex (nullptr: no height tiles loaded).
	TerrainTextureClass *m_terrainHeightTex = nullptr;
	/** The texture that contains the alpha edge tiles that get blended on 
			top of the base texture. getAlphaUVData does the mapping. */
	AlphaTerrainTextureClass *m_alphaTerrainTex;
	Int	m_alphaTexHeight; /// Height of m_alphaTerrainTex allocated.

	/** The texture that contains custom blend edge tiles. */
	AlphaEdgeTextureClass *m_alphaEdgeTex;
	Int	m_alphaEdgeHeight; /// Height of m_alphaEdgeTex allocated.

	/// Drawing info - re the part of the map that is being drawn.
	Int m_drawOriginX;
	Int m_drawOriginY;
	Int m_drawWidthX;
	Int m_drawHeightY;

protected:
	TileData *getSourceTile(UnsignedInt ndx) { if (ndx<NUM_SOURCE_TILES) return(m_sourceTiles[ndx]); return(nullptr); };
	TileData *getEdgeTile(UnsignedInt ndx) { if (ndx<NUM_SOURCE_TILES) return(m_edgeTiles[ndx]); return(nullptr); };
	/// UV mapping data for a cell to map into the terrain texture.
	void getUVForNdx(Int ndx, float *minU, float *minV, float *maxU, float*maxV, Bool fullTile);
	Bool getUVForTileIndex(Int ndx, Short tileNdx, float U[4], float V[4], Bool fullTile);
	Int getTextureClassFromNdx(Int tileNdx);
	void readTexClass(TXTextureClass *texClass, TileData **tileData); 
	void readTexClassRelief(TXTextureClass *texClass);	///< relief tiles of a texture class, if present
	void readTexClassHeights(TXTextureClass *texClass);	///< height tiles of a texture class, if present
	/// Tiles from <texture><suffix> next to the terrain texture of a class into tiles; log tag for the protocol.
	void readTexClassExtra(TXTextureClass *texClass, const char *suffix, TileData **tiles, const char *tag);
	void updateTerrainReliefAtlas(Int pow2Height);		///< builds m_terrainReliefTex and passes it to the render device
	void updateTerrainHeightAtlas(Int pow2Height);		///< builds m_terrainHeightTex and the mixing weights for the render device
	/// Per grid point the two texture classes with the largest share and their weights (RGBA); returns the number of
	/// cells whose tile is not where the class lies in the world (their heights do not match the colour).
	Int bakeTessWeights(UnsignedByte *rgba);
	Int updateTileTexturePositions(Int *edgeHeight); ///< Places each tile in the texture.
	void updateTerrainDetailTable(void);	///< passes the atlas positions of the terrain textures to the render device
	void initCliffFlagsFromHeights(void);
	void setCellCliffFlagFromHeights(Int xIndex, Int yIndex);

protected:	 // file reader callbacks.
	static Bool ParseHeightMapDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
	Bool ParseHeightMapData(DataChunkInput &file, DataChunkInfo *info, void *userData);
	static Bool ParseSizeOnlyInChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
	Bool ParseSizeOnly(DataChunkInput &file, DataChunkInfo *info, void *userData);
	static Bool ParseBlendTileDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
	Bool ParseBlendTileData(DataChunkInput &file, DataChunkInfo *info, void *userData);
	static Bool ParseWorldDictDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
	static Bool ParseObjectsDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
	static Bool ParseObjectDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
	Bool ParseObjectData(DataChunkInput &file, DataChunkInfo *info, void *userData, Bool readDict);
	static Bool ParseLightingDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);

protected:
	WorldHeightMap(void);			///< Simple constructor for WorldHeightMapEdit class.

public: // constructors/destructors
	// A damaged file does not throw out of the constructor (that would destroy a RefCountClass with
	// one reference); the caller checks this, releases the object and reports the error.
	Bool isLoadFailed(void) const { return m_loadFailed; }
	WorldHeightMap(ChunkInputStream *pFile, Bool bHMapOnly=false);	// read from file.
	~WorldHeightMap(void) override;			// destroy.

public:  // Boundary info
	const VecICoord2D& getAllBoundaries(void) const { return m_boundaries; }

public:  // height map info.
	static Int getMinHeightValue(void) {return K_MIN_HEIGHT;}
	static Int getMaxHeightValue(void) {return K_MAX_HEIGHT;}

	UnsignedByte *getDataPtr(void) {return m_data;}

	
	Int getXExtent(void) {return m_width;}	///<number of vertices in x
	Int getYExtent(void) {return m_height;}	///<number of vertices in y

	inline Int getDrawOrgX(void) {return m_drawOriginX;}
	inline Int getDrawOrgY(void) {return m_drawOriginY;}

	inline Int getDrawWidth(void) {return m_drawWidthX;}
	inline Int getDrawHeight(void) {return m_drawHeightY;}
	inline void setDrawWidth(Int width) {m_drawWidthX = width; if (m_drawWidthX>m_width) m_drawWidthX = m_width;}
	inline void setDrawHeight(Int height) {m_drawHeightY = height; if (m_drawHeightY>m_height) m_drawHeightY = m_height;}
	virtual Int getBorderSize(void) override {return m_borderSize;}
  inline Int getBorderSizeInline(void) const { return m_borderSize; }
	/// Get height with the offset that HeightMapRenderObjClass uses built in.
	inline UnsignedByte getDisplayHeight(Int x, Int y) { return m_data[x+m_drawOriginX+m_width*(y+m_drawOriginY)];}

	/// Get height in normal coordinates.
	inline UnsignedByte getHeight(Int xIndex, Int yIndex) 
	{ 
		Int ndx = (yIndex*m_width)+xIndex;
		if ((ndx>=0) && (ndx<m_dataSize) && m_data) 
			return(m_data[ndx]); 
		else 
			return(0);
	};

	void getUVForBlend(Int edgeClass, Region2D *range);

	Bool setDrawOrg(Int xOrg, Int yOrg);

	static void freeListOfMapObjects(void);

	Int getTextureClassNoBlend(Int xIndex, Int yIndex, Bool baseClass=false);
	Int getTextureClass(Int xIndex, Int yIndex, Bool baseClass=false);
	TXTextureClass getTextureFromIndex( Int textureIndex );

public:  // tile and texture info.	
	void setTextureLOD(Int lod);	///< set maximum lod level sent to the hardware.
	TextureClass *getTerrainTexture(void);  //< generates if needed and returns the terrain texture
	TextureClass *getAlphaTerrainTexture(void); //< generates if needed and returns alpha terrain texture
	TextureClass *getEdgeTerrainTexture(void); //< generates if needed and returns blend edge texture
	/// UV mapping data for a cell to map into the terrain texture.  Returns true if the textures had to be stretched for cliffs.
	Bool getUVData(Int xIndex, Int yIndex, float U[4], float V[4], Bool fullTile);
	Bool getFlipState(Int xIndex, Int yIndex) const;
	///Faster version of above function without all the safety checks - For people that do checks externally.
	inline Bool getQuickFlipState(Int xIndex, Int yIndex) const
	{
		return m_cellFlipState[yIndex*m_flipStateWidth + (xIndex >> 3)] & (1<<(xIndex&0x7));
	}

	void clearFlipStates(void);
	Bool getCliffState(Int xIndex, Int yIndex) const;
	Bool getExtraAlphaUVData(Int xIndex, Int yIndex, float U[4], float V[4], UnsignedByte alpha[4], Bool *flip, Bool *cliff);
	/// UV mapping data for a cell to map into the alpha terrain texture.
	void getAlphaUVData(Int xIndex, Int yIndex, float U[4], float V[4], UnsignedByte alpha[4], Bool *flip, Bool fullTile);
	void getTerrainColorAt(Real x, Real y, RGBColor *pColor);
	AsciiString getTerrainNameAt(Real x, Real y);
	Bool isCliffMappedTexture(Int xIndex, Int yIndex);


  Bool getSeismicUpdateFlag(Int xIndex, Int yIndex) const;
  void setSeismicUpdateFlag(Int xIndex, Int yIndex, Bool value); 
  void clearSeismicUpdateFlags(void) ;
  virtual Real getSeismicZVelocity(Int xIndex, Int yIndex) const override;
  virtual void setSeismicZVelocity(Int xIndex, Int yIndex, Real value) override; 
  void fillSeismicZVelocities( Real value );
  virtual Real getBilinearSampleSeismicZVelocity( Int x, Int y) override;



public:  // modify height value
	void setRawHeight(Int xIndex, Int yIndex, UnsignedByte height) { 
		Int ndx = (yIndex*m_width)+xIndex;
		if ((ndx>=0) && (ndx<m_dataSize) && m_data) m_data[ndx]=height;
	};
public: // Read tile utilities. jba [7/9/2003]
	// Tile files are TGA or DDS (TerrainTileImage.h). tilesPerRow: tiles per row the caller expects,
	// 0 if unknown; it sets the tile size of a DDS only.
	static Bool readTiles(InputStream *pStrm, TileData **tiles, Int numRows, Int tilesPerRow=0);
	static Int countTiles(InputStream *pStrm, Bool *halfTile=nullptr, Int tilesPerRow=0);
	/// Texture package: name of the tile file the next countTiles/readTiles read (nullptr: none). With a
	/// name the package knows, they use the level of the chosen tier (TexturePackage).
	static void setTilePackageName(const char *name);

protected:
	void setCliffState(Int xIndex, Int yIndex, Bool state);

};

#endif
