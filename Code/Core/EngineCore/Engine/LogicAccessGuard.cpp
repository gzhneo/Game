/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// LogicAccessGuard.cpp -- see LogicAccessGuard.h.

#include "Engine/LogicAccessGuard.h"
#include "Engine/EngineLog.h"

#include <cstdio>
#include <format>
#include <map>
#include <mutex>
#include <string>

namespace Engine
{

namespace
{
	struct Finding
	{
		std::string			category;
		std::string			function;
		unsigned long long	count = 0;
	};

	struct GuardState
	{
		std::mutex					mutex;
		unsigned long long			totalReports = 0;
		std::map<std::string, Finding>	findings;		///< key "file:line"
	};

	/// Depth of open sections -- **per thread** and without a lock.
	///
	/// No lock because ASSERT_LOGIC_ACCESS precedes every marked access, among others in
	/// GameLogic::findObjectByID(). That is thousands of calls per tick; a mutex just to read a
	/// number would cost more than the lookup itself.
	///
	/// Per thread because the question is "is *this* thread inside the tick". With a separate sim
	/// thread holding the section open while the main thread draws, a shared counter would
	/// silently allow the main thread's accesses and swallow exactly the findings the guard is for.
	thread_local int t_openDepth = 0;

	// On the heap instead of a static object: clearLogicAccessFindings() frees it so nothing of it
	// lives after the memory report at the end of WinMain (even an empty std::map holds a head node).
	std::mutex &stateMutex() { static std::mutex m; return m; }
	GuardState *&statePtr() { static GuardState *p = nullptr; return p; }
	GuardState &state()
	{
		GuardState *&p = statePtr();
		if (p == nullptr)
		{
			std::lock_guard<std::mutex> lock(stateMutex());
			if (p == nullptr)
				p = new GuardState;
		}
		return *p;
	}
}

void openLogicAccess()
{
	++t_openDepth;
}

void closeLogicAccess()
{
	if (t_openDepth > 0)
		--t_openDepth;
}

bool isLogicAccessOpen()
{
	return t_openDepth > 0;
}

bool reportLogicAccess(const char *file, int line, const char *category, const char *function)
{
	const std::string key = std::format("{}:{}", file ? file : "?", line);

	bool firstTime = false;
	{
		GuardState &s = state();
		std::lock_guard<std::mutex> lock(s.mutex);
		++s.totalReports;
		Finding &finding = s.findings[key];
		firstTime = (finding.count == 0);
		if (firstTime)
		{
			finding.category = (category != nullptr) ? category : "?";
			finding.function = (function != nullptr) ? function : "?";
		}
		++finding.count;
	}

	if (firstTime)
	{
		// Log only the first time -- otherwise one frame would flood the log.
		Log::write(LOG_CAT_LOGIC, LOG_WARNING,
			"logic access outside the tick [%s] %s() in %s(%d)",
			(category != nullptr) ? category : "?", (function != nullptr) ? function : "?",
			(file != nullptr) ? file : "?", line);
	}
	return firstTime;
}

std::size_t logicAccessFindingCount()
{
	GuardState &s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	return s.findings.size();
}

unsigned long long logicAccessTotalCount()
{
	GuardState &s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	return s.totalReports;
}

void clearLogicAccessFindings()
{
	std::lock_guard<std::mutex> lock(stateMutex());
	delete statePtr();
	statePtr() = nullptr;
}

bool writeLogicAccessFindingsTo(const char *path)
{
	std::FILE *out = std::fopen(path, "w");
	if (out == nullptr)
		return false;

	GuardState &s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	std::fputs(std::format("Logic access findings: {} sites, {} accesses in total\n\n", s.findings.size(), s.totalReports).c_str(), out);
	std::fputs(std::format("{:<12} {:<40} {:>12}  {}\n", "Category", "Function", "Accesses", "Site").c_str(), out);
	for (std::map<std::string, Finding>::const_iterator it = s.findings.begin(); it != s.findings.end(); ++it)
	{
		std::fputs(std::format("{:<12} {:<40} {:>12}  {}\n", it->second.category, it->second.function, it->second.count,
			it->first).c_str(), out);
	}
	std::fclose(out);
	return true;
}

} // namespace Engine
