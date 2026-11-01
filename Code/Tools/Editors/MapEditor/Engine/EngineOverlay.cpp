/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EngineOverlay.cpp -- the editor's overlay in the 3D view: lines and markers that the editor hands
// over in world coordinates (boundaries, waypoints and their links, areas, a tint of the terrain cells),
// laid on the terrain every frame and drawn after the scene. Widths are given in pixels and scaled with the distance of each
// point to the camera, so lines keep their width from front to back at any zoom; depth test on,
// depth write off, so hills and buildings hide the overlay as they hide the ground.

#include "Engine/EngineApi.h"
#include "Engine/EngineInternal.h"

#include "W3DDevice/GameClient/HeightMap.h"
#include "camera.h"
#include "WW3D2/Common/renderdevice.h"
#include "shader.h"
#include "vertexbuffer.h"
#include "indexbuffer.h"
#include "matrix4.h"
#include "vertmaterial.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{

struct Vertex
{
	float x, y, z;
	unsigned color;
};

std::vector<float> s_points;			// x, y, z per point
std::vector<MeOverlayShape> s_shapes;

std::vector<Vertex> s_vertices;
std::vector<unsigned short> s_indices;

// Terrain tint: one colour per height map cell (alpha 0: none).
std::vector<uint32_t> s_tint;
int s_tintWidth = 0;
int s_tintHeight = 0;
int s_tintBorder = 0;
// The tinted cells as quads, built when the tint or the heights change rather than every frame.
std::vector<Vertex> s_tintQuads;
bool s_tintDirty = true;

// Selection rectangle in pixels; empty when x0 == x1 or y0 == y1.
int s_marquee[4] = {0, 0, 0, 0};

// World units per pixel at unit distance from the camera, and the camera position of this frame.
float s_perPixel = 0.0f;
Vector3 s_eye;

float pixels(float x, float y, float z)
{
	const float dx = x - s_eye.X;
	const float dy = y - s_eye.Y;
	const float dz = z - s_eye.Z;
	return s_perPixel * std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Segments are cut into pieces of this length so the line follows the ground.
constexpr float STEP = 5.0f;
constexpr float ON_TERRAIN = ME_OVERLAY_ON_TERRAIN;

float groundAt(HeightMapRenderObjClass *terrain, float x, float y, float minZ)
{
	const float h = terrain->getHeightMapHeight(x, y, nullptr);
	return std::max(h, minZ);
}

void flush()
{
	if (s_indices.empty())
		return;
	DynamicVBAccessClass vb(BUFFER_TYPE_DYNAMIC_DEVICE, dynamic_fvf_type, static_cast<unsigned short>(s_vertices.size()));
	{
		DynamicVBAccessClass::WriteLockClass lock(&vb);
		const FVFInfoClass &fi = vb.FVF_Info();
		unsigned char *va = reinterpret_cast<unsigned char *>(lock.Get_Formatted_Vertex_Array());
		for (const Vertex &v : s_vertices)
		{
			*reinterpret_cast<Vector3 *>(va + fi.Get_Location_Offset()) = Vector3(v.x, v.y, v.z);
			*reinterpret_cast<unsigned *>(va + fi.Get_Diffuse_Offset()) = v.color;
			*reinterpret_cast<Vector2 *>(va + fi.Get_Tex_Offset(0)) = Vector2(0.0f, 0.0f);
			va += fi.Get_FVF_Size();
		}
	}
	DynamicIBAccessClass ib(BUFFER_TYPE_DYNAMIC_DEVICE, static_cast<unsigned short>(s_indices.size()));
	{
		DynamicIBAccessClass::WriteLockClass lock(&ib);
		std::copy(s_indices.begin(), s_indices.end(), lock.Get_Index_Array());
	}
	RenderDevice::Set_Vertex_Buffer(vb);
	RenderDevice::Set_Index_Buffer(ib, 0);
	RenderDevice::Draw_Triangles(0, static_cast<unsigned short>(s_indices.size() / 3), 0, static_cast<unsigned short>(s_vertices.size()));
	s_vertices.clear();
	s_indices.clear();
}

// Six indices per four vertices: this many vertices keep both counts within 16 bits.
constexpr std::size_t MAX_BATCH_VERTICES = 40000;

// One quad; flushes first when the 16-bit vertex or index counts would overflow.
void quad(const Vertex &a, const Vertex &b, const Vertex &c, const Vertex &d)
{
	if (s_vertices.size() + 4 > MAX_BATCH_VERTICES)
		flush();
	const unsigned short base = static_cast<unsigned short>(s_vertices.size());
	s_vertices.push_back(a);
	s_vertices.push_back(b);
	s_vertices.push_back(c);
	s_vertices.push_back(d);
	const unsigned short idx[6] = {base, static_cast<unsigned short>(base + 1), static_cast<unsigned short>(base + 2),
		base, static_cast<unsigned short>(base + 2), static_cast<unsigned short>(base + 3)};
	s_indices.insert(s_indices.end(), idx, idx + 6);
}

void segment(HeightMapRenderObjClass *terrain, float x0, float y0, float z0, float x1, float y1, float z1,
	float widthPixels, unsigned color)
{
	const float dx = x1 - x0;
	const float dy = y1 - y0;
	const float length = std::sqrt(dx * dx + dy * dy);
	if (length <= 0.0f)
		return;
	// Unit perpendicular and direction in the ground plane; the pieces overlap by half a width so
	// joints stay closed. The line is lifted by about a pixel against depth fighting with the ground.
	const float ux = dx / length;
	const float uy = dy / length;
	const int pieces = std::max(1, static_cast<int>(std::ceil(length / STEP)));
	float px = x0;
	float py = y0;
	float pz = groundAt(terrain, px, py, z0);
	float pw = pixels(px, py, pz);
	pz += std::max(0.3f, pw);
	for (int i = 1; i <= pieces; ++i)
	{
		const float t = static_cast<float>(i) / static_cast<float>(pieces);
		const float qx = x0 + dx * t;
		const float qy = y0 + dy * t;
		float qz = groundAt(terrain, qx, qy, z0 + (z1 - z0) * t);
		const float qw = pixels(qx, qy, qz);
		qz += std::max(0.3f, qw);
		const float ph = pw * widthPixels * 0.5f;
		const float qh = qw * widthPixels * 0.5f;
		const float sx = i == 1 ? px : px - ux * ph * 0.5f;
		const float sy = i == 1 ? py : py - uy * ph * 0.5f;
		quad({sx - uy * ph, sy + ux * ph, pz, color}, {sx + uy * ph, sy - ux * ph, pz, color},
			{qx + uy * qh, qy - ux * qh, qz, color}, {qx - uy * qh, qy + ux * qh, qz, color});
		px = qx;
		py = qy;
		pz = qz;
		pw = qw;
	}
}

void marker(HeightMapRenderObjClass *terrain, float x, float y, float z, float radiusPixels, unsigned color)
{
	float h = groundAt(terrain, x, y, z);
	const float perPixel = pixels(x, y, h);
	h += std::max(0.3f, perPixel);
	const float radius = radiusPixels * perPixel;
	// A diamond on the ground and a dark rim a little larger below it, so it reads on any ground.
	const unsigned rim = (color & 0xFF000000u) | 0x00202020u;
	const float r2 = radius * 1.35f;
	quad({x, y - r2, h, rim}, {x + r2, y, h, rim}, {x, y + r2, h, rim}, {x - r2, y, h, rim});
	quad({x, y - radius, h, color}, {x + radius, y, h, color}, {x, y + radius, h, color}, {x - radius, y, h, color});
}

} // namespace

namespace MapEditorEngine
{

void terrainHeightsChanged()
{
	s_tintDirty = true;
}

void clearOverlay()
{
	s_points.clear();
	s_shapes.clear();
}

namespace
{

// The tinted cells as quads a little above the ground, which the depth test hides behind hills.
void renderTint(HeightMapRenderObjClass *terrain)
{
	if (s_tintDirty)
	{
		constexpr float LIFT = 0.4f;
		const auto corner = [terrain](int x, int y, unsigned color) {
			const float wx = static_cast<float>(x - s_tintBorder) * MAP_XY_FACTOR;
			const float wy = static_cast<float>(y - s_tintBorder) * MAP_XY_FACTOR;
			return Vertex{wx, wy, terrain->getHeightMapHeight(wx, wy, nullptr) + LIFT, color};
		};
		s_tintQuads.clear();
		for (int y = 0; y + 1 < s_tintHeight; ++y)
		{
			for (int x = 0; x + 1 < s_tintWidth; ++x)
			{
				const unsigned color = s_tint[static_cast<size_t>(y) * static_cast<size_t>(s_tintWidth) + static_cast<size_t>(x)];
				if ((color >> 24) == 0)
					continue;
				s_tintQuads.insert(s_tintQuads.end(), {corner(x, y, color), corner(x + 1, y, color), corner(x + 1, y + 1, color), corner(x, y + 1, color)});
			}
		}
		s_tintDirty = false;
	}
	for (size_t i = 0; i + 3 < s_tintQuads.size(); i += 4)
		quad(s_tintQuads[i], s_tintQuads[i + 1], s_tintQuads[i + 2], s_tintQuads[i + 3]);
}

} // namespace

void renderOverlay(CameraClass *camera, HeightMapRenderObjClass *terrain, float perPixelAtUnitDistance)
{
	if ((s_shapes.empty() && s_tint.empty()) || !terrain)
		return;
	s_perPixel = perPixelAtUnitDistance;
	s_eye = camera->Get_Position();
	camera->Apply();
	RenderDevice::Set_World_Identity();
	VertexMaterialClass *material = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	RenderDevice::Set_Material(material);
	REF_PTR_RELEASE(material);
	ShaderClass shader = ShaderClass::_PresetAlphaSolidShader;
	shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
	RenderDevice::Set_Shader(shader);
	RenderDevice::Set_Texture(0, nullptr);
	RenderDevice::Set_Texture(1, nullptr);

	renderTint(terrain);
	const int pointCount = static_cast<int>(s_points.size() / 3);
	for (const MeOverlayShape &shape : s_shapes)
	{
		if (shape.firstPoint < 0 || shape.pointCount <= 0 || shape.firstPoint + shape.pointCount > pointCount)
			continue;
		const float *p = &s_points[static_cast<size_t>(shape.firstPoint) * 3];
		if (shape.kind == ME_OVERLAY_MARKER)
		{
			marker(terrain, p[0], p[1], p[2], shape.size, shape.color);
			continue;
		}
		const int n = shape.pointCount;
		const int segments = shape.kind == ME_OVERLAY_LOOP && n > 2 ? n : n - 1;
		for (int i = 0; i < segments; ++i)
		{
			const float *a = p + i * 3;
			const float *b = p + ((i + 1) % n) * 3;
			segment(terrain, a[0], a[1], a[2], b[0], b[1], b[2], shape.size, shape.color);
		}
	}
	flush();
}

void setMarquee(int x0, int y0, int x1, int y1)
{
	s_marquee[0] = std::min(x0, x1);
	s_marquee[1] = std::min(y0, y1);
	s_marquee[2] = std::max(x0, x1);
	s_marquee[3] = std::max(y0, y1);
}

namespace
{

// Straight in clip space: identity transforms, no depth test.
void beginScreenQuads()
{
	const Matrix4x4 identity(true);
	RenderDevice::Set_World_Identity();
	RenderDevice::Set_View_Identity();
	RenderDevice::Set_Transform(WW3D_TS_PROJECTION, identity);
	VertexMaterialClass *material = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	RenderDevice::Set_Material(material);
	REF_PTR_RELEASE(material);
	ShaderClass shader = ShaderClass::_PresetAlphaSolidShader;
	shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
	shader.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);
	shader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);
	RenderDevice::Set_Shader(shader);
	RenderDevice::Set_Texture(0, nullptr);
	RenderDevice::Set_Texture(1, nullptr);
}

} // namespace

void renderLetterbox(int width, int height)
{
	// As the old WorldBuilder: bars of the height the 16:9 picture leaves free.
	const float bar = (static_cast<float>(height) - 9.0f / 16.0f * static_cast<float>(width)) * 0.5f;
	if (bar <= 0.0f || width <= 0)
		return;
	beginScreenQuads();
	const float h = static_cast<float>(height);
	const float yTop = 1.0f - bar / h * 2.0f;
	constexpr unsigned BLACK = 0xFF000000u;
	quad({-1.0f, 1.0f, 0.5f, BLACK}, {1.0f, 1.0f, 0.5f, BLACK}, {1.0f, yTop, 0.5f, BLACK}, {-1.0f, yTop, 0.5f, BLACK});
	quad({-1.0f, -yTop, 0.5f, BLACK}, {1.0f, -yTop, 0.5f, BLACK}, {1.0f, -1.0f, 0.5f, BLACK}, {-1.0f, -1.0f, 0.5f, BLACK});
	flush();
}

void renderMarquee(int width, int height)
{
	if (s_marquee[0] == s_marquee[2] || s_marquee[1] == s_marquee[3] || width <= 0 || height <= 0)
		return;
	beginScreenQuads();

	const auto nx = [width](float px) { return px / static_cast<float>(width) * 2.0f - 1.0f; };
	const auto ny = [height](float py) { return 1.0f - py / static_cast<float>(height) * 2.0f; };
	const auto rect = [&](float l, float t, float r, float b, unsigned color) {
		quad({nx(l), ny(t), 0.5f, color}, {nx(r), ny(t), 0.5f, color}, {nx(r), ny(b), 0.5f, color}, {nx(l), ny(b), 0.5f, color});
	};
	const float l = static_cast<float>(s_marquee[0]);
	const float t = static_cast<float>(s_marquee[1]);
	const float r = static_cast<float>(s_marquee[2]);
	const float b = static_cast<float>(s_marquee[3]);
	constexpr unsigned FILL = 0x3045C8FFu;
	constexpr unsigned EDGE = 0xFF45C8FFu;
	rect(l, t, r, b, FILL);
	rect(l, t, r, t + 1.0f, EDGE);
	rect(l, b - 1.0f, r, b, EDGE);
	rect(l, t, l + 1.0f, b, EDGE);
	rect(r - 1.0f, t, r, b, EDGE);
	flush();
}

} // namespace MapEditorEngine

MEAPI void meViewSetOverlay(const float *points, int32_t pointCount, const MeOverlayShape *shapes, int32_t shapeCount)
{
	MapEditorEngine::clearOverlay();
	if (!points || !shapes || pointCount <= 0 || shapeCount <= 0)
		return;
	s_points.assign(points, points + static_cast<size_t>(pointCount) * 3);
	// Heights below ME_OVERLAY_ON_TERRAIN (or NaN) mean "on the terrain".
	for (size_t i = 2; i < s_points.size(); i += 3)
	{
		if (!(s_points[i] > ON_TERRAIN))
			s_points[i] = ON_TERRAIN;
	}
	s_shapes.assign(shapes, shapes + shapeCount);
}

MEAPI void meViewSetTerrainTint(const uint32_t *colors, int32_t width, int32_t height, int32_t border)
{
	if (!colors || width <= 1 || height <= 1)
	{
		s_tint.clear();
		s_tintWidth = s_tintHeight = 0;
		s_tintQuads.clear();
		s_tintDirty = true;
		return;
	}
	s_tint.assign(colors, colors + static_cast<size_t>(width) * static_cast<size_t>(height));
	s_tintWidth = width;
	s_tintHeight = height;
	s_tintBorder = border;
	s_tintDirty = true;
}
