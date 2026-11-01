/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: CpuCheck.cpp -------------------------------------------------------------------------
//
// The project is compiled with /arch:AVX2. On a CPU without AVX2 (Intel before Haswell 2013, AMD
// before Excavator 2015) the process would die with "Illegal Instruction", possibly already in a
// global constructor before WinMain. This translation unit is therefore built WITHOUT /arch:AVX2
// (see App/CMakeLists.txt) and registers its check via init_seg(compiler) as the very first
// static initialiser: a clear message instead of a crash, then a hard exit.
//
// Nothing from the rest of the project may be included here (everything else is AVX2 code).
// ---------------------------------------------------------------------------------------------

#include <windows.h>	// WIN32_LEAN_AND_MEAN comes project-wide from CMakeLists.txt
#include <intrin.h>

#ifndef GAME_EDITION_NAME	// set per game by App/CMakeLists.txt
#error GAME_EDITION_NAME missing (App/CMakeLists.txt)
#endif

namespace
{
	bool cpuSupportsAvx2()
	{
		int regs[4] = { 0, 0, 0, 0 };

		__cpuid(regs, 0);
		const int maxLeaf = regs[0];
		if (maxLeaf < 7)
			return false;

		// Leaf 1: ECX bit 27 = OSXSAVE (OS saves YMM registers), bit 28 = AVX
		__cpuid(regs, 1);
		const bool osxsave = (regs[2] & (1 << 27)) != 0;
		const bool avx     = (regs[2] & (1 << 28)) != 0;
		if (!osxsave || !avx)
			return false;

		// XCR0 bits 1 (SSE state) and 2 (AVX state) must be enabled by the OS
		const unsigned long long xcr0 = _xgetbv(0);
		if ((xcr0 & 6) != 6)
			return false;

		// Leaf 7, subleaf 0: EBX bit 5 = AVX2
		__cpuidex(regs, 7, 0);
		return (regs[1] & (1 << 5)) != 0;
	}

	struct CpuCheckAtStartup
	{
		CpuCheckAtStartup()
		{
			if (cpuSupportsAvx2())
				return;

			MessageBoxW(nullptr,
				L"This build requires a CPU with AVX2 support\n"
				L"(Intel Haswell 2013 or later, AMD Excavator 2015 or Ryzen).\n"
				L"The processor of this computer does not support AVX2.",
				L"GZH Neo \u2013 " GAME_EDITION_NAME,	// game name per game from App/CMakeLists.txt
				MB_OK | MB_ICONSTOP | MB_SETFOREGROUND);
			ExitProcess(1);
		}
	};

	// init_seg(compiler): runs before all ordinary global constructors of the program.
	// C4074 ("initializers put in compiler reserved initialization area") is intended here.
#pragma warning(push)
#pragma warning(disable : 4073 4074)
#pragma init_seg(compiler)
	CpuCheckAtStartup g_cpuCheckAtStartup;
#pragma warning(pop)
}
