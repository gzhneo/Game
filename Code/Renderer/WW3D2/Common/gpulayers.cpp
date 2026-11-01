/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// gpulayers.cpp -- see gpulayers.h.

#include "gpulayers.h"

#include <string.h>

// The build sets both switches to 0 or 1; builds that set neither (test builds) have every layer.
#ifndef GZH_RENDER_D3D11
#define GZH_RENDER_D3D11 1
#endif
#ifndef GZH_RENDER_VULKAN
#define GZH_RENDER_VULKAN 1
#endif
#if !GZH_RENDER_D3D11 && !GZH_RENDER_VULKAN
#error At least one device layer must be built (GZH_RENDER_D3D11, GZH_RENDER_VULKAN).
#endif

// The registration functions are called by name: a static registration object in a static library
// would be dropped by the linker because nothing refers to it.
#if GZH_RENDER_D3D11
void Gpu_Register_D3D11();
#endif
#if GZH_RENDER_VULKAN
void Gpu_Register_Vulkan();
#endif

namespace {

const unsigned MAX_LAYERS = 4;
const GpuLayerInfo* Layers[MAX_LAYERS];
unsigned Count = 0;

void Register_All()
{
	static bool done = false;
	if (done) return;
	done = true;
#if GZH_RENDER_D3D11
	Gpu_Register_D3D11();
#endif
#if GZH_RENDER_VULKAN
	Gpu_Register_Vulkan();
#endif
}

}

void Gpu_Register_Layer(const GpuLayerInfo& info)
{
	if (Count == MAX_LAYERS) return;
	unsigned at = Count++;
	while (at > 0 && Layers[at - 1]->order > info.order) {
		Layers[at] = Layers[at - 1];
		--at;
	}
	Layers[at] = &info;
}

unsigned Gpu_Layer_Count()
{
	Register_All();
	return Count;
}

const GpuLayerInfo* Gpu_Layer(unsigned index)
{
	Register_All();
	return index < Count ? Layers[index] : nullptr;
}

const GpuLayerInfo* Gpu_Find_Layer(const char* name)
{
	Register_All();
	if (!name) return nullptr;
	for (unsigned i = 0; i < Count; ++i) {
		if (_stricmp(Layers[i]->name, name) == 0) return Layers[i];
	}
	return nullptr;
}
