/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// vulkandxc.h ------------------------------------------------------------------------------------
//
// HLSL to SPIR-V at runtime with the DirectX Shader Compiler (dxcompiler.dll next to the game, loaded
// on demand). The same shader files and macros as under Direct3D 11; the arguments place every
// resource class in its own binding range (vulkanspirv.h), keep the D3D constant buffer layout and
// the HLSL 2018 language rules of d3dcompiler_47.
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef VULKANDXC_H
#define VULKANDXC_H

#include "WW3D2/Common/gpudevice.h"

/// Loads dxcompiler.dll; false with the reason if it is missing or unusable.
bool		Dxc_Load(char* reason, unsigned reason_size);
void		Dxc_Unload();
/// Compiles one entry point; profile is the D3D name ("vs_4_0"), the stage decides the target.
HRESULT		Dxc_Compile(const char* source, SIZE_T length, const char* name, const GPU_SHADER_MACRO* macros,
						GpuShaderInclude* include, const char* entry, const char* profile, UINT flags,
						GpuBlob** code, GpuBlob** errors);
/// Preprocessed source (includes and macros resolved).
HRESULT		Dxc_Preprocess(const char* source, SIZE_T length, const char* name, const GPU_SHADER_MACRO* macros,
						   GpuShaderInclude* include, GpuBlob** text);
/// Compiler version and argument set, for the shader cache key.
UINT		Dxc_Version();

#endif // VULKANDXC_H
