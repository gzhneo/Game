/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EngineTerrain.cpp -- the terrain textures (Terrain.ini) for the editor's texture brush: how many
// tiles each texture file holds and how wide it is, counted as the old WorldBuilder counts them
// (WorldHeightMapEdit::loadImagesFromTerrainType), and a small picture of one tile.

#include "Engine/EngineApi.h"
#include "Engine/EngineInternal.h"

#include "Common/FileSystem.h"
#include "Common/MapReaderWriterInfo.h"
#include "Common/TerrainTypes.h"
#include "W3DDevice/GameClient/TileData.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

std::vector<MeTerrainInfo> s_terrains;
bool s_terrainsBuilt = false;

AsciiString texturePath(TerrainType *terrain)
{
	AsciiString path(TERRAIN_TGA_DIR_PATH);
	path.concat(terrain->getTexture());
	return path;
}

void buildTerrains()
{
	if (s_terrainsBuilt || !TheTerrainTypes)
		return;
	s_terrainsBuilt = true;
	for (TerrainType *t = TheTerrainTypes->firstTerrain(); t; t = TheTerrainTypes->nextTerrain(t))
	{
		MeTerrainInfo info{};
		if (t->getName().getLength() >= static_cast<int>(sizeof(info.name)))
			continue;
		CachedFileInputStream stream;
		if (!stream.open(texturePath(t)))
			continue;
		const Int tiles = WorldHeightMap::countTiles(&stream);
		if (tiles < 1)
			continue;
		// The largest square of tiles the file holds (at most 10 x 10).
		Int width = 10;
		while (width > 1 && tiles < width * width)
			--width;
		std::strncpy(info.name, t->getName().str(), sizeof(info.name) - 1);
		info.numTiles = width * width;
		info.width = width;
		info.blendEdge = t->isBlendEdge() ? 1 : 0;
		s_terrains.push_back(info);
	}
	std::sort(s_terrains.begin(), s_terrains.end(), [](const MeTerrainInfo &a, const MeTerrainInfo &b) { return std::strcmp(a.name, b.name) < 0; });
	char line[80];
	std::snprintf(line, sizeof(line), "terrain: %d textures", static_cast<int>(s_terrains.size()));
	MapEditorEngine::report(line);
}

TerrainType *findTerrain(const char *name)
{
	for (TerrainType *t = TheTerrainTypes ? TheTerrainTypes->firstTerrain() : nullptr; t; t = TheTerrainTypes->nextTerrain(t))
	{
		if (t->getName() == name)
			return t;
	}
	return nullptr;
}

} // namespace

namespace MapEditorEngine
{

void clearTerrains()
{
	s_terrains.clear();
	s_terrainsBuilt = false;
}

} // namespace MapEditorEngine

MEAPI int32_t meTerrainCount(void)
{
	if (!MapEditorEngine::engineStarted())
		return 0;
	buildTerrains();
	return static_cast<int32_t>(s_terrains.size());
}

MEAPI int32_t meTerrainEntry(int32_t index, MeTerrainInfo *info)
{
	if (!MapEditorEngine::engineStarted() || !info)
		return 0;
	buildTerrains();
	if (index < 0 || static_cast<std::size_t>(index) >= s_terrains.size())
		return 0;
	*info = s_terrains[static_cast<std::size_t>(index)];
	return 1;
}

MEAPI int32_t meTerrainPreview(const char *name, int32_t size, uint8_t *rgba)
{
	if (!MapEditorEngine::engineStarted() || !name || !rgba || size < 8 || size > 256)
		return 0;
	TerrainType *terrain = findTerrain(name);
	if (!terrain)
		return 0;
	CachedFileInputStream stream;
	if (!stream.open(texturePath(terrain)))
		return 0;
	// One tile (the file's lower left) is enough for a swatch.
	TileData *tiles[1] = {nullptr};
	const bool ok = WorldHeightMap::readTiles(&stream, tiles, 1) && tiles[0] && tiles[0]->getDataPtr();
	if (ok)
	{
		const Int extent = tiles[0]->getExtent();
		const UnsignedByte *data = tiles[0]->getDataPtr();
		for (int y = 0; y < size; ++y)
		{
			// The tile's first row is its bottom row.
			const Int sy = extent - 1 - (y * extent) / size;
			for (int x = 0; x < size; ++x)
			{
				const Int sx = (x * extent) / size;
				const UnsignedByte *p = data + (sy * extent + sx) * TILE_BYTES_PER_PIXEL;
				uint8_t *out = rgba + (static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4;
				out[0] = p[2];
				out[1] = p[1];
				out[2] = p[0];
				out[3] = 255;
			}
		}
	}
	REF_PTR_RELEASE(tiles[0]);
	return ok ? 1 : 0;
}
