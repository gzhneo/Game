/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// vulkanmemory.cpp -- the Vulkan Memory Allocator implementation (third-party code, MIT), compiled in
// this one translation unit with its warnings off like volk (vulkanloader.cpp). Its assertions go to
// the log instead of stopping the game: they fire for allocations still alive at shutdown, which the
// device already reports.

#include "vulkanloader.h"
#include "wwdebug.h"

static void Vma_Assert_Failed([[maybe_unused]] const char* expression, [[maybe_unused]] int line)
{
	static int reported = 0;
	if (reported++ < 8) { WWDEBUG_SAY(("Vulkan memory allocator: assertion '%s' (line %d)\n", expression, line)); }
}

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_ASSERT(expr) do { if (!(expr)) Vma_Assert_Failed(#expr, __LINE__); } while (0)
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
