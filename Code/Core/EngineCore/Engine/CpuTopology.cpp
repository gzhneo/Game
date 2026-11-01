/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// CpuTopology.cpp -- see CpuTopology.h.

#include "Engine/CpuTopology.h"
#include "Engine/EngineLog.h"
#include "Engine/FormatInto.h"

#include <cstring>
#include <thread>
#include <vector>

#if defined(_WIN32)
	#include <windows.h>
#endif

namespace Engine
{

namespace
{
#if defined(_WIN32)
	/// EfficiencyClass is the byte after Flags in PROCESSOR_RELATIONSHIP; the MinGW-w64 headers do not
	/// name the member, so it is read by offset (same layout as in the Windows SDK).
	int efficiencyClassOf(const PROCESSOR_RELATIONSHIP &processor)
	{
		return reinterpret_cast<const unsigned char *>(&processor.Flags)[1];
	}

	/// Evaluates GetLogicalProcessorInformationEx. Returns false if the query fails (then
	/// hardware_concurrency is used instead).
	bool queryWindowsTopology(CpuTopology &topo)
	{
		DWORD length = 0;
		if (::GetLogicalProcessorInformationEx(RelationAll, nullptr, &length) != FALSE
			|| ::GetLastError() != ERROR_INSUFFICIENT_BUFFER || length == 0)
			return false;

		std::vector<unsigned char> buffer(length);
		if (::GetLogicalProcessorInformationEx(RelationAll,
				reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &length) == FALSE)
			return false;

		int firstEfficiencyClass = -1;
		bool sawDifferentEfficiencyClass = false;
		// Separate counter: `topo.numaNodes` starts at 1 (there is always one node). Counting up
		// on it directly would report two for a single-node system.
		int numaNodeCount = 0;

		DWORD offset = 0;
		while (offset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) <= length)
		{
			PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info =
				reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() + offset);
			if (info->Size == 0)
				break;

			switch (info->Relationship)
			{
				case RelationProcessorCore:
				{
					++topo.physicalCores;
					int logicalInCore = 0;
					for (WORD g = 0; g < info->Processor.GroupCount; ++g)
					{
						KAFFINITY mask = info->Processor.GroupMask[g].Mask;
						for (KAFFINITY bit = 1; bit != 0; bit <<= 1)
							if (mask & bit)
								++logicalInCore;
					}
					topo.logicalProcessors += logicalInCore;
					if (logicalInCore > 1)
						topo.hasSimultaneousMultithreading = true;

					// EfficiencyClass: higher = faster. All cores with the highest class count as
					// P-cores. On non-hybrid CPUs the value is 0 everywhere.
					const int efficiencyClass = efficiencyClassOf(info->Processor);
					if (firstEfficiencyClass < 0)
						firstEfficiencyClass = efficiencyClass;
					else if (efficiencyClass != firstEfficiencyClass)
						sawDifferentEfficiencyClass = true;
					break;
				}
				case RelationNumaNode:
					++numaNodeCount;
					break;
				default:
					break;
			}
			offset += info->Size;
		}

		if (topo.physicalCores == 0 || topo.logicalProcessors == 0)
			return false;

		topo.isHybrid = sawDifferentEfficiencyClass;
		topo.numaNodes = (numaNodeCount > 0) ? numaNodeCount : 1;

		// Second pass: count P/E-cores and build the P-core affinity mask (group 0 only; more than
		// 64 logical processors do not matter for the target hardware).
		int highestClass = 0;
		offset = 0;
		while (offset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) <= length)
		{
			PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info =
				reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() + offset);
			if (info->Size == 0)
				break;
			if (info->Relationship == RelationProcessorCore)
			{
				const int efficiencyClass = efficiencyClassOf(info->Processor);
				if (efficiencyClass > highestClass)
					highestClass = efficiencyClass;
			}
			offset += info->Size;
		}

		offset = 0;
		while (offset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) <= length)
		{
			PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info =
				reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() + offset);
			if (info->Size == 0)
				break;
			if (info->Relationship == RelationProcessorCore)
			{
				const int efficiencyClass = efficiencyClassOf(info->Processor);
				if (efficiencyClass == highestClass)
				{
					++topo.performanceCores;
					for (WORD g = 0; g < info->Processor.GroupCount; ++g)
						if (info->Processor.GroupMask[g].Group == 0)
							topo.performanceCoreMask |= static_cast<std::uint64_t>(info->Processor.GroupMask[g].Mask);
				}
				else
				{
					++topo.efficiencyCores;
				}
			}
			offset += info->Size;
		}
		return true;
	}
#endif // _WIN32

	CpuTopology queryTopology()
	{
		CpuTopology topo;
#if defined(_WIN32)
		if (queryWindowsTopology(topo))
			return topo;
#endif
		// Fallback: only the logical processor count is reliably available.
		const unsigned hc = std::thread::hardware_concurrency();
		topo.logicalProcessors = (hc > 0) ? static_cast<int>(hc) : 1;
		topo.physicalCores = topo.logicalProcessors;	// no better information: assume no SMT
		topo.performanceCores = topo.physicalCores;
		topo.efficiencyCores = 0;
		topo.hasSimultaneousMultithreading = false;
		topo.isHybrid = false;
		topo.numaNodes = 1;
		return topo;
	}
}

const CpuTopology &CpuTopology::get()
{
	static const CpuTopology topo = queryTopology();
	return topo;
}

void CpuTopology::describe(char *buffer, unsigned bufferSize) const
{
	if (buffer == nullptr || bufferSize == 0)
		return;
	formatInto(buffer, bufferSize,
		"{} cores ({} P, {} E), {} logical processors, SMT {}, hybrid {}, NUMA nodes {}, recommended workers {}",
		physicalCores, performanceCores, efficiencyCores, logicalProcessors,
		hasSimultaneousMultithreading ? "yes" : "no", isHybrid ? "yes" : "no",
		numaNodes, recommendedWorkerCount());
}

bool logCpuTopologyAtStartup(bool showHint)
{
	const CpuTopology &topo = CpuTopology::get();
	char text[256];
	topo.describe(text, sizeof(text));
	ENGINE_LOG_INFO(LOG_CAT_ENGINE, "CPU: %s", text);

	if (topo.meetsRecommendedMinimum())
		return false;

	// Hint only, no abort.
	ENGINE_LOG_WARNING(LOG_CAT_ENGINE,
		"This CPU has %d cores/%d threads. 4 cores/8 threads are recommended; the game runs "
		"but uses less concurrency.", topo.physicalCores, topo.logicalProcessors);
	return showHint;
}

} // namespace Engine
