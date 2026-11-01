/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// PreferenceFile.cpp -- see PreferenceFile.h.

#include "Engine/PreferenceFile.h"

namespace Engine
{

namespace
{

std::string_view trimmed(std::string_view text)
{
	const char *const space = " \t\r\n\v\f";
	const std::size_t first = text.find_first_not_of(space);
	if (first == std::string_view::npos)
		return {};
	const std::size_t last = text.find_last_not_of(space);
	return text.substr(first, last - first + 1);
}

bool isSet(const PreferenceValues &values, const std::string &key, const std::string **value)
{
	const auto found = values.find(key);
	if (found == values.end() || found->second.empty())
		return false;
	*value = &found->second;
	return true;
}

}	// namespace

bool parsePreferenceLine(std::string_view line, std::string &key, std::string &value)
{
	const std::size_t separator = line.find('=');
	if (separator == std::string_view::npos)
		return false;
	key = std::string(trimmed(line.substr(0, separator)));
	value = std::string(trimmed(line.substr(separator + 1)));
	return !key.empty() && !value.empty();
}

void mergePreferenceChanges(const PreferenceValues &loaded, const PreferenceValues &current,
	PreferenceValues &onDisk)
{
	for (const auto &entry : current)
	{
		const std::string *old = nullptr;
		const bool wasSet = isSet(loaded, entry.first, &old);
		if (entry.second.empty())
		{
			if (wasSet)
				onDisk.erase(entry.first);		// cleared by this reader
		}
		else if (!wasSet || *old != entry.second)
			onDisk[entry.first] = entry.second;
	}
	for (const auto &entry : loaded)
	{
		if (current.find(entry.first) == current.end())
			onDisk.erase(entry.first);			// removed by this reader
	}
}

}	// namespace Engine
