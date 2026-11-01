/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ThreadRole.cpp -- see ThreadRole.h.

#include "Engine/ThreadRole.h"
#include "Engine/EngineLog.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <format>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
	#include <windows.h>
#endif

namespace Engine
{

namespace
{
	thread_local ThreadRole s_role = THREAD_ROLE_UNKNOWN;

	/// Sites already reported (logged once, counted always), so a violation inside a loop does not
	/// flood the log. On the heap so clearThreadRoleViolations() can free it before the memory report.
	struct Violation
	{
		std::string function, file, expected, actual;
		int line = 0;
		unsigned long long count = 0;
	};
	using ViolationMap = std::map<std::string, Violation>;
	std::mutex &violationMutex()
	{
		static std::mutex m;
		return m;
	}
	ViolationMap *&violationsPtr()
	{
		static ViolationMap *p = nullptr;
		return p;
	}
}

void setThreadRole(ThreadRole role, const char *threadName)
{
	s_role = role;

	if (threadName != nullptr && threadName[0] != '\0')
	{
#if defined(_WIN32)
		// SetThreadDescription exists from Windows 10 1607 (target platform is Windows 10/11).
		wchar_t wide[64];
		if (::MultiByteToWideChar(CP_UTF8, 0, threadName, -1, wide, static_cast<int>(sizeof(wide) / sizeof(wide[0]))) > 0)
			::SetThreadDescription(::GetCurrentThread(), wide);
#endif
		ENGINE_LOG_INFO(LOG_CAT_ENGINE, "thread '%s' has role %s", threadName, threadRoleName(role));
	}
}

ThreadRole threadRole()
{
	return s_role;
}

const char *threadRoleName(ThreadRole role)
{
	static const char *names[THREAD_ROLE_COUNT] =
		{ "Unknown", "Main", "Sim", "Main+Sim", "Worker", "IO", "Audio" };
	return (role >= 0 && role < THREAD_ROLE_COUNT) ? names[role] : "?";
}

bool threadHasRole(ThreadRole role)
{
	const ThreadRole mine = s_role;
	if (mine == role)
		return true;
	// While logic and main thread are the same thread, it satisfies both roles.
	if (mine == THREAD_ROLE_MAIN_AND_SIM && (role == THREAD_ROLE_MAIN || role == THREAD_ROLE_SIM))
		return true;
	return false;
}

bool threadIsWorker()
{
	return s_role == THREAD_ROLE_WORKER;
}

bool reportThreadRoleViolation(const char *file, int line, const char *expected, const char *function)
{
	const std::string key = std::format("{}:{}:{}", file ? file : "?", line, expected ? expected : "?");

	{
		std::lock_guard<std::mutex> lock(violationMutex());
		ViolationMap *&violations = violationsPtr();
		if (violations == nullptr)
			violations = new ViolationMap;
		Violation &v = (*violations)[key];
		if (v.count++ > 0)
			return false;	// this site was already reported
		v.function = function ? function : "?";
		v.file = file ? file : "?";
		v.expected = expected ? expected : "?";
		v.actual = threadRoleName(threadRole());
		v.line = line;
	}

	Log::write(LOG_CAT_ENGINE, LOG_ERROR,
		"thread role: %s() in %s(%d) expects %s but runs as %s",
		function ? function : "?", file ? file : "?", line,
		expected ? expected : "?", threadRoleName(threadRole()));
	return true;
}

std::size_t threadRoleViolationSiteCount()
{
	std::lock_guard<std::mutex> lock(violationMutex());
	return violationsPtr() ? violationsPtr()->size() : 0;
}

bool writeThreadRoleViolationsTo(const char *path)
{
	std::vector<Violation> list;
	{
		std::lock_guard<std::mutex> lock(violationMutex());
		if (violationsPtr() != nullptr)
			for (const auto &entry : *violationsPtr())
				list.push_back(entry.second);
	}
	std::sort(list.begin(), list.end(), [](const Violation &a, const Violation &b) { return a.count > b.count; });
	std::FILE *f = std::fopen(path, "w");
	if (f == nullptr)
		return false;
	unsigned long long total = 0;
	for (const Violation &v : list)
		total += v.count;
	std::fputs(std::format("Thread role violations: {} sites, {} calls in total\n\n", list.size(), total).c_str(), f);
	std::fputs(std::format("{:<10} {:<6} {:<6} {:<44} {}\n", "Calls", "Wants", "Runs", "Function", "Site").c_str(), f);
	for (const Violation &v : list)
		std::fputs(std::format("{:<10} {:<6} {:<6} {:<44} {}:{}\n", v.count, v.expected, v.actual, v.function, v.file, v.line).c_str(), f);
	std::fclose(f);
	return true;
}

void clearThreadRoleViolations()
{
	std::lock_guard<std::mutex> lock(violationMutex());
	delete violationsPtr();
	violationsPtr() = nullptr;
}

} // namespace Engine
