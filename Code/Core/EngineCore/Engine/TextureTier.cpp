/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// TextureTier.cpp -- see TextureTier.h.

#include "Engine/TextureTier.h"
#include "Engine/FormatInto.h"

#include <cstring>
#include <cctype>

#if defined(_WIN32)
	#include <windows.h>
	#include <dxgi1_4.h>
	#include <d3d11.h>
#endif

namespace Engine
{

namespace
{
	bool equalsNoCase(const char *a, const char *b)
	{
		for (; *a != 0 && *b != 0; ++a, ++b)
		{
			if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b)))
				return false;
		}
		return *a == 0 && *b == 0;
	}

	const std::uint64_t MEGABYTE = 1024ull * 1024ull;

#if defined(_WIN32)
	template <class T> void releaseCom(T *&p)
	{
		if (p != nullptr)
		{
			p->Release();
			p = nullptr;
		}
	}

	/// DXGI and D3D11 are loaded dynamically: the query runs before the render device and adds no
	/// link dependency to this module.
	void queryWindows(VideoMemoryInfo &info)
	{
		HMODULE dxgi = ::LoadLibraryA("dxgi.dll");
		if (dxgi == nullptr)
			return;
		using CreateFactoryFn = HRESULT (WINAPI *)(REFIID, void **);
		CreateFactoryFn createFactory = reinterpret_cast<CreateFactoryFn>(
			reinterpret_cast<void *>(::GetProcAddress(dxgi, "CreateDXGIFactory1")));
		IDXGIFactory1 *factory = nullptr;
		IDXGIAdapter1 *adapter = nullptr;
		if (createFactory != nullptr
			&& SUCCEEDED(createFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory)))
			&& factory->EnumAdapters1(0, &adapter) == S_OK)
		{
			DXGI_ADAPTER_DESC1 desc;
			if (SUCCEEDED(adapter->GetDesc1(&desc)))
			{
				info.valid = true;
				info.vendorId = desc.VendorId;
				info.deviceId = desc.DeviceId;
				info.dedicatedVideoMemory = desc.DedicatedVideoMemory;
				info.sharedSystemMemory = desc.SharedSystemMemory;
				unsigned i = 0;
				for (; i + 1 < sizeof(info.adapterName) && desc.Description[i] != 0; ++i)
					info.adapterName[i] = (desc.Description[i] < 128) ? static_cast<char>(desc.Description[i]) : '?';
				info.adapterName[i] = 0;
			}

			IDXGIAdapter3 *adapter3 = nullptr;
			if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void **>(&adapter3))))
			{
				DXGI_QUERY_VIDEO_MEMORY_INFO local;
				DXGI_QUERY_VIDEO_MEMORY_INFO nonLocal;
				if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)))
				{
					info.budgetKnown = true;
					info.localBudget = local.Budget;
					info.localUsage = local.CurrentUsage;
				}
				if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocal)))
					info.nonLocalBudget = nonLocal.Budget;
				releaseCom(adapter3);
			}

			// The UMA flag can only be queried through a D3D11 device. Without swap chain and window
			// this costs a few milliseconds at startup.
			HMODULE d3d11 = ::LoadLibraryA("d3d11.dll");
			if (d3d11 != nullptr)
			{
				using CreateDeviceFn = HRESULT (WINAPI *)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
					const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
				CreateDeviceFn createDevice = reinterpret_cast<CreateDeviceFn>(
					reinterpret_cast<void *>(::GetProcAddress(d3d11, "D3D11CreateDevice")));
				ID3D11Device *device = nullptr;
				const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
				if (createDevice != nullptr
					&& SUCCEEDED(createDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels,
						static_cast<UINT>(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION, &device, nullptr, nullptr)))
				{
					D3D11_FEATURE_DATA_D3D11_OPTIONS2 options2;
					std::memset(&options2, 0, sizeof(options2));
					if (SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &options2, sizeof(options2))))
					{
						info.umaKnown = true;
						info.unifiedMemory = options2.UnifiedMemoryArchitecture != FALSE;
					}
					releaseCom(device);
				}
				::FreeLibrary(d3d11);
			}
		}
		releaseCom(adapter);
		releaseCom(factory);
		::FreeLibrary(dxgi);
	}
#endif
}

int parseTextureTier(const char *text)
{
	if (text == nullptr)
		return TEXTURE_TIER_INVALID;
	if (equalsNoCase(text, "auto"))
		return TEXTURE_TIER_AUTO;
	if (equalsNoCase(text, "sd"))
		return TEXTURE_TIER_SD;
	if (equalsNoCase(text, "hd"))
		return TEXTURE_TIER_HD;
	if (equalsNoCase(text, "uhd") || equalsNoCase(text, "4k"))
		return TEXTURE_TIER_UHD;
	return TEXTURE_TIER_INVALID;
}

const char *textureTierName(int tier)
{
	switch (tier)
	{
		case TEXTURE_TIER_SD:	return "SD";
		case TEXTURE_TIER_HD:	return "HD";
		case TEXTURE_TIER_UHD:	return "UHD";
		case TEXTURE_TIER_AUTO:	return "Auto";
		default:				return "?";
	}
}

const char *textureTierFolder(int tier)
{
	switch (tier)
	{
		case TEXTURE_TIER_HD:	return "HD";
		case TEXTURE_TIER_UHD:	return "UHD";
		default:				return nullptr;
	}
}

const VideoMemoryInfo &VideoMemoryInfo::get()
{
	static VideoMemoryInfo info;
	static bool queried = false;
	if (!queried)
	{
		queried = true;
#if defined(_WIN32)
		queryWindows(info);
#endif
	}
	return info;
}

DetailRecommendation recommendDetailLevel(const VideoMemoryInfo &info)
{
	DetailRecommendation r;
	if (!info.valid)
	{
		r.level = 3;
		r.reason = "no adapter found";
	}
	else if (info.umaKnown && info.unifiedMemory)
	{
		r.level = 0;
		r.reason = "shared video memory (UMA)";
	}
	else if (info.dedicatedVideoMemory < DETAIL_LOW_BELOW_BYTES)
	{
		r.level = 0;
		r.reason = "less than 2 GB dedicated video memory";
	}
	else if (info.dedicatedVideoMemory < DETAIL_VERY_HIGH_FROM_BYTES)
	{
		r.level = 2;
		r.reason = "dedicated graphics";
	}
	else
	{
		r.level = 3;
		r.reason = "dedicated graphics with 6 GB or more";
	}
	return r;
}

std::uint64_t VideoMemoryInfo::budgetBytes() const
{
	if (!valid)
		return 0;
	if (budgetKnown && localBudget > 0)
		return localBudget;
	return unifiedMemory ? sharedSystemMemory : dedicatedVideoMemory;
}

void VideoMemoryInfo::describe(char *buffer, unsigned bufferSize) const
{
	if (buffer == nullptr || bufferSize == 0)
		return;
	if (!valid)
	{
		formatInto(buffer, bufferSize, "no DXGI adapter");
		return;
	}
	formatInto(buffer, bufferSize,
		"{} ({:04X}:{:04X}), UMA {}, budget {}{} MB (used {} MB), dedicated {} MB, shared {} MB",
		static_cast<const char *>(adapterName), vendorId, deviceId,
		umaKnown ? (unifiedMemory ? "yes" : "no") : "unknown",
		budgetKnown ? "" : "unknown/",
		localBudget / MEGABYTE, localUsage / MEGABYTE, dedicatedVideoMemory / MEGABYTE, sharedSystemMemory / MEGABYTE);
}

TextureTierChoice chooseTextureTier(const TextureTierInput &input)
{
	TextureTierChoice choice;
	int tier = TEXTURE_TIER_SD;
	if (input.requested >= TEXTURE_TIER_SD && input.requested < TEXTURE_TIER_COUNT)
	{
		tier = input.requested;
		choice.reason = "explicitly chosen";
	}
	else if (input.lowDetail)
	{
		// Rule 0: detail level Low
		tier = TEXTURE_TIER_SD;
		choice.reason = "detail level Low";
	}
	else
	{
		// Rule 1: output height
		if (input.outputHeight >= TEXTURE_TIER_UHD_MIN_HEIGHT)
			tier = TEXTURE_TIER_UHD;
		else if (input.outputHeight >= TEXTURE_TIER_HD_MIN_HEIGHT)
			tier = TEXTURE_TIER_HD;
		else
			tier = TEXTURE_TIER_SD;
		choice.reason = (tier == TEXTURE_TIER_SD) ? "output height below 900" : "by output height";

		// Rule 3: UMA at most HD
		if (input.unifiedMemory && tier > TEXTURE_TIER_HD)
		{
			tier = TEXTURE_TIER_HD;
			choice.reason = "shared video memory (UMA): at most HD";
		}

		// Rule 2: budget
		while (tier > TEXTURE_TIER_SD && input.budgetBytes > 0 && input.demandBytes[tier] > 0
			&& input.demandBytes[tier] + input.demandBytes[tier] / 2 > input.budgetBytes)
		{
			--tier;
			choice.reason = "video memory budget too small";
		}
	}

	// Rule 4: only available tiers
	while (tier > TEXTURE_TIER_SD && !input.available[tier])
	{
		--tier;
		choice.reason = "tier archives missing";
	}

	choice.tier = tier;
	return choice;
}

} // namespace Engine
