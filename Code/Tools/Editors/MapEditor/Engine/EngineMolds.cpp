/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EngineMolds.cpp -- the mesh molds of the old WorldBuilder (Data\Editor\Molds\*.w3d): their names,
// and a mold's top surface as a height field, which is where a ray from above hits the mesh first.

#include "Engine/EngineApi.h"
#include "Engine/EngineInternal.h"

#include "Common/FileSystem.h"
#include "Common/file.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "mesh.h"
#include "meshmdl.h"
#include "RAMFILE.H"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace
{

constexpr const char *MOLD_DIR = "Data\\Editor\\Molds\\";
constexpr int MAX_SAMPLES_ACROSS = 256;

struct MoldFile
{
	std::string name;	// file name without directory and extension
	std::string path;
};

std::vector<MoldFile> s_molds;
bool s_moldsListed = false;

void listMolds()
{
	if (s_moldsListed || !TheFileSystem)
		return;
	s_moldsListed = true;
	FilenameList files;
	TheFileSystem->getFileListInDirectory(AsciiString(MOLD_DIR), AsciiString("*.w3d"), files, FALSE);
	for (const AsciiString &file : files)
	{
		std::string path = file.str();
		std::string name = path;
		const std::size_t slash = name.find_last_of("\\/");
		if (slash != std::string::npos)
			name.erase(0, slash + 1);
		const std::size_t dot = name.find_last_of('.');
		if (dot != std::string::npos)
			name.erase(dot);
		if (!name.empty() && name.size() < 64)
			s_molds.push_back({name, path});
	}
	std::sort(s_molds.begin(), s_molds.end(), [](const MoldFile &a, const MoldFile &b) { return a.name < b.name; });
	char line[80];
	std::snprintf(line, sizeof(line), "molds: %d files", static_cast<int>(s_molds.size()));
	MapEditorEngine::report(line);
}

const MoldFile *findMold(const char *name)
{
	for (const MoldFile &m : s_molds)
	{
		if (m.name == name)
			return &m;
	}
	return nullptr;
}

// The mold's mesh, loaded from its file on first use; null if the file holds no mesh of that name.
MeshClass *loadMold(const MoldFile &mold)
{
	W3DAssetManager *assets = static_cast<W3DAssetManager *>(W3DAssetManager::Get_Instance());
	if (!assets)
		return nullptr;
	// Only the mold file itself, not the game's art folders.
	assets->Set_WW3D_Load_On_Demand(false);
	RenderObjClass *object = assets->Create_Render_Obj(mold.name.c_str());
	if (!object)
	{
		File *file = TheFileSystem->openFile(mold.path.c_str(), File::READ | File::BINARY);
		if (file)
		{
			const Int size = file->size();
			std::vector<char> bytes(static_cast<std::size_t>(std::max<Int>(size, 0)));
			const bool read = size > 0 && file->read(bytes.data(), size) == size;
			file->close();
			if (read)
			{
				// The file overload of the base class; W3DAssetManager hides it behind the name overload.
				RAMFileClass ram(bytes.data(), size);
				static_cast<WW3DAssetManager *>(assets)->Load_3D_Assets(static_cast<FileClass &>(ram));
				object = assets->Create_Render_Obj(mold.name.c_str());
			}
		}
	}
	assets->Set_WW3D_Load_On_Demand(true);
	if (object && object->Class_ID() != RenderObjClass::CLASSID_MESH)
	{
		REF_PTR_RELEASE(object);
	}
	return static_cast<MeshClass *>(object);
}

} // namespace

namespace MapEditorEngine
{

void clearMolds()
{
	s_molds.clear();
	s_moldsListed = false;
}

} // namespace MapEditorEngine

MEAPI int32_t meMoldCount(void)
{
	if (!MapEditorEngine::engineStarted())
		return 0;
	listMolds();
	return static_cast<int32_t>(s_molds.size());
}

MEAPI int32_t meMoldName(int32_t index, char *name, int32_t size)
{
	if (!MapEditorEngine::engineStarted() || !name || size < 2)
		return 0;
	listMolds();
	if (index < 0 || static_cast<std::size_t>(index) >= s_molds.size())
		return 0;
	std::snprintf(name, static_cast<std::size_t>(size), "%s", s_molds[static_cast<std::size_t>(index)].name.c_str());
	return 1;
}

MEAPI int32_t meMoldSample(const char *name, MeMoldGrid *grid, float *z, int32_t capacity)
{
	if (!MapEditorEngine::engineStarted() || !MapEditorEngine::viewReady() || !name || !grid)
		return 0;
	listMolds();
	const MoldFile *mold = findMold(name);
	if (!mold)
		return 0;
	MeshClass *mesh = loadMold(*mold);
	if (!mesh)
	{
		MapEditorEngine::report(std::string("molds: ") + mold->path + " has no mesh " + mold->name);
		return 0;
	}
	MeshModelClass *model = mesh->Peek_Model();
	const int vertexCount = model ? model->Get_Vertex_Count() : 0;
	const int polygonCount = model ? static_cast<int>(model->Get_Polygon_Count()) : 0;
	const Vector3 *vertices = vertexCount > 0 ? model->Get_Vertex_Array() : nullptr;
	const TriIndex *polygons = polygonCount > 0 ? model->Get_Polygon_Array() : nullptr;
	if (!vertices || !polygons)
	{
		REF_PTR_RELEASE(mesh);
		return 0;
	}
	float minX = vertices[0].X, maxX = minX, minY = vertices[0].Y, maxY = minY;
	for (int i = 1; i < vertexCount; ++i)
	{
		minX = std::min(minX, vertices[i].X);
		maxX = std::max(maxX, vertices[i].X);
		minY = std::min(minY, vertices[i].Y);
		maxY = std::max(maxY, vertices[i].Y);
	}
	const float extent = std::max(maxX - minX, maxY - minY);
	const float step = std::max(2.5f, extent / static_cast<float>(MAX_SAMPLES_ACROSS - 1));
	grid->x0 = minX;
	grid->y0 = minY;
	grid->step = step;
	grid->cols = static_cast<int32_t>(std::floor((maxX - minX) / step)) + 1;
	grid->rows = static_cast<int32_t>(std::floor((maxY - minY) / step)) + 1;
	const int32_t samples = grid->cols * grid->rows;
	if (z && capacity >= samples)
	{
		std::fill(z, z + samples, std::numeric_limits<float>::quiet_NaN());
		for (int p = 0; p < polygonCount; ++p)
		{
			const Vector3 &a = vertices[polygons[p].I];
			const Vector3 &b = vertices[polygons[p].J];
			const Vector3 &c = vertices[polygons[p].K];
			const float area = (b.X - a.X) * (c.Y - a.Y) - (c.X - a.X) * (b.Y - a.Y);
			if (std::abs(area) < 1e-6f)
				continue;	// seen edge-on from above
			const int c0 = std::max(0, static_cast<int>(std::ceil((std::min({a.X, b.X, c.X}) - minX) / step)));
			const int c1 = std::min(grid->cols - 1, static_cast<int>(std::floor((std::max({a.X, b.X, c.X}) - minX) / step)));
			const int r0 = std::max(0, static_cast<int>(std::ceil((std::min({a.Y, b.Y, c.Y}) - minY) / step)));
			const int r1 = std::min(grid->rows - 1, static_cast<int>(std::floor((std::max({a.Y, b.Y, c.Y}) - minY) / step)));
			for (int r = r0; r <= r1; ++r)
			{
				for (int col = c0; col <= c1; ++col)
				{
					const float x = minX + static_cast<float>(col) * step;
					const float y = minY + static_cast<float>(r) * step;
					const float u = ((b.X - x) * (c.Y - y) - (c.X - x) * (b.Y - y)) / area;
					const float v = ((c.X - x) * (a.Y - y) - (a.X - x) * (c.Y - y)) / area;
					const float w = 1.0f - u - v;
					if (u < -1e-4f || v < -1e-4f || w < -1e-4f)
						continue;
					const float height = u * a.Z + v * b.Z + w * c.Z;
					float &out = z[static_cast<std::size_t>(r) * static_cast<std::size_t>(grid->cols) + static_cast<std::size_t>(col)];
					if (std::isnan(out) || height > out)
						out = height;
				}
			}
		}
	}
	REF_PTR_RELEASE(mesh);
	return samples;
}
