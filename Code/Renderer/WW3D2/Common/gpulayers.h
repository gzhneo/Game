/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// gpulayers.h ------------------------------------------------------------------------------------
//
// The device layers built into the program (D3D11/, Vulkan/). Each layer registers a description; the
// render core knows only this list. Which layers exist is a build switch (GZH_RENDER_D3D11,
// GZH_RENDER_VULKAN); Gpu_Layer_Count and Gpu_Layer register them on first use.
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef GPULAYERS_H
#define GPULAYERS_H

class GpuOutput;

struct GpuLayerInfo
{
	const char*	name;			///< "d3d11", "vulkan": requests (-vulkan, Settings.ini RenderBackend, editors) and logs
	const char*	title;			///< "Direct3D 11", "Vulkan": messages to the user
	const char*	requirement;	///< minimum hardware, for the message when no layer starts
	int			order;			///< default and fallback order, lower first
	/// Usable on this machine? Otherwise the reason (log and notice). Leaves nothing created behind.
	bool		(*probe)(char* reason, unsigned reason_size);
	/// The layer's output (adapters, device, swap chain); a static object.
	GpuOutput*	(*output)();
	/// Why the last device creation failed, or nullptr.
	const char*	(*device_error)();
};

/// Adds a layer; called by the layers' registration functions.
void					Gpu_Register_Layer(const GpuLayerInfo& info);
/// Number of layers built in.
unsigned				Gpu_Layer_Count();
/// Layer by position in order (default first), nullptr past the end.
const GpuLayerInfo*		Gpu_Layer(unsigned index);
/// Layer by name (case-insensitive), nullptr if not built in.
const GpuLayerInfo*		Gpu_Find_Layer(const char* name);

#endif // GPULAYERS_H
