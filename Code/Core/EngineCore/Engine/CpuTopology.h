/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// CpuTopology.h -- detects cores, SMT and hybrid CPUs.
//
// Used for:
//   * a one-time startup hint when fewer than 4 cores/8 threads are present (hint, not abort);
//   * the default worker count = logical processors - 2 (sim and main thread), at least 1;
//   * placing the sim and main thread on different physical cores, on P-cores for hybrid
//     CPUs (Intel P/E).
//
// The query runs once (GetLogicalProcessorInformationEx) and is cached. Outside Windows only
// the logical processor count is available (std::thread::hardware_concurrency) so unit tests
// and tools build.

#pragma once

#ifndef ENGINE_CPUTOPOLOGY_H
#define ENGINE_CPUTOPOLOGY_H

#include <cstdint>

namespace Engine
{

struct CpuTopology
{
	int				physicalCores = 0;			///< physical cores (all classes)
	int				logicalProcessors = 0;		///< logical processors (with SMT/HT)
	int				performanceCores = 0;		///< P-cores on hybrid CPUs, else = physicalCores
	int				efficiencyCores = 0;		///< E-cores on hybrid CPUs, else 0
	bool			hasSimultaneousMultithreading = false;	///< logicalProcessors > physicalCores
	bool			isHybrid = false;			///< cores with different EfficiencyClass
	std::uint64_t	performanceCoreMask = 0;	///< affinity mask of the P-cores (group 0), 0 = unknown
	int				numaNodes = 1;

	/// Query (once, then cached).
	static const CpuTopology &get();

	/// Recommended worker thread count: logical processors minus main and sim thread,
	/// at least 1. On the 4C/8T target hardware that is 6.
	int recommendedWorkerCount() const
	{
		const int workers = logicalProcessors - 2;
		return (workers < 1) ? 1 : workers;
	}

	/// Meets the recommended minimum (quad core with 8 threads)?
	bool meetsRecommendedMinimum() const { return physicalCores >= 4 && logicalProcessors >= 8; }

	/// One line for the log/hint, e.g.
	/// "16 Kerne (16 P, 0 E), 32 logische Prozessoren, SMT ja, Hybrid nein".
	void describe(char *buffer, unsigned bufferSize) const;
};

/// Logs the topology and warns once if there are too few cores.
/// `showHint` is FALSE when the user disabled the hint (Options.ini "HardwareHint = no").
/// Returns TRUE if the hint would be due.
bool logCpuTopologyAtStartup(bool showHint);

} // namespace Engine

#endif // ENGINE_CPUTOPOLOGY_H
