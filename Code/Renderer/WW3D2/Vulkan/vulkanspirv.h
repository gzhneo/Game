/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// vulkanspirv.h ----------------------------------------------------------------------------------
//
// What the Vulkan device needs to know about and change in the SPIR-V that DXC produces, without the
// Vulkan headers (RenderUnitTests check it with hand-written modules):
//
//   - Resources: DXC places b<n> at binding n, t<n> at 16 + n and s<n> at 32 + n, all in set 0. The
//     device gives every shader stage its own descriptor set, so the set number becomes the stage.
//   - Stage inputs and outputs: DXC numbers locations in declaration order per shader, Direct3D links
//     stages by semantic. With -fspv-reflect every variable carries its semantic; the device moves the
//     inputs of a stage to the locations the previous stage writes the same semantic to.
//   - The reflection decorations need device extensions the device does not ask for, so they are removed
//     before the module is created.
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef VULKANSPIRV_H
#define VULKANSPIRV_H

#include <cstdint>
#include <vector>

/// First binding of each register class inside a stage's descriptor set.
enum
{
	SPIRV_BINDING_CB		= 0,	///< b0..b15
	SPIRV_BINDING_SRV		= 16,	///< t0..t15
	SPIRV_BINDING_SAMPLER	= 32,	///< s0..s15
	SPIRV_BINDING_END		= 48,
	SPIRV_MAX_IO			= 32,
};

/// One stage input or output.
struct SpirvIo
{
	char		semantic[32];	///< upper case, without the trailing index ("TEXCOORD")
	unsigned	index;			///< semantic index (TEXCOORD3 -> 3)
	unsigned	location;		///< ~0u for built-ins
	unsigned	id;				///< result id of the variable
};

struct SpirvReflection
{
	unsigned	cb_mask;			///< bit n: b<n> used
	unsigned	srv_mask;			///< bit n: t<n> used
	unsigned	sampler_mask;		///< bit n: s<n> used
	unsigned	input_count;
	SpirvIo		inputs[SPIRV_MAX_IO];
	unsigned	output_count;
	SpirvIo		outputs[SPIRV_MAX_IO];
};

/// Checks a DXC module, reflects it, moves every resource to descriptor set `set` and removes the
/// reflection decorations. false with a message in error if the module is not usable.
bool	Spirv_Prepare(std::vector<uint32_t>& words, unsigned set, SpirvReflection& reflection, char* error, unsigned error_size);

/// Moves the non built-in inputs of `module` (prepared, reflection `self`) to the location the
/// previous stage writes the same semantic to. Inputs the previous stage does not write keep their
/// location. Returns true if a location changed.
bool	Spirv_Link_Inputs(std::vector<uint32_t>& words, const SpirvReflection& self, const SpirvReflection& previous);

/// Splits "TEXCOORD12" into "TEXCOORD" and 12 (upper case); names without digits get index 0.
void	Spirv_Split_Semantic(const char* text, char* name, unsigned name_size, unsigned* index);

#endif // VULKANSPIRV_H
