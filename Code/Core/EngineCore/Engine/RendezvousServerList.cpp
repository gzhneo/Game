/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Engine/RendezvousServerList.h"

#include "Engine/NetAddress.h"
#include "Engine/Rendezvous.h"

#include <algorithm>

namespace Engine
{

namespace
{
	std::string_view trimmed(std::string_view line)
	{
		const std::size_t hash = line.find('#');
		if (hash != std::string_view::npos)
			line = line.substr(0, hash);
		const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
		while (!line.empty() && blank(line.front()))
			line.remove_prefix(1);
		while (!line.empty() && blank(line.back()))
			line.remove_suffix(1);
		return line;
	}

	template <typename Visit>
	void forEachLine(std::string_view text, Visit visit)
	{
		while (!text.empty())
		{
			const std::size_t end = text.find('\n');
			visit(trimmed(text.substr(0, end)));
			if (end == std::string_view::npos)
				break;
			text.remove_prefix(end + 1);
		}
	}
}

bool RendezvousServerList::isServerText(std::string_view text)
{
	return !text.empty() && text.size() <= TREFF_MAX_SERVER_TEXT
		&& parseNetTarget(text).kind != NetTarget::Kind::Invalid;
}

bool RendezvousServerList::contains(std::string_view text) const
{
	return std::find(m_servers.begin(), m_servers.end(), text) != m_servers.end();
}

bool RendezvousServerList::remove(std::string_view text)
{
	const auto it = std::find(m_servers.begin(), m_servers.end(), text);
	if (it == m_servers.end())
		return false;
	m_servers.erase(it);
	return true;
}

bool RendezvousServerList::add(std::string_view text)
{
	if (!isServerText(text) || contains(text) || m_servers.size() >= MAX_SERVERS)
		return false;
	m_servers.emplace_back(text);
	return true;
}

void RendezvousServerList::addLines(std::string_view fileText)
{
	forEachLine(fileText, [this](std::string_view line) { add(line); });
}

std::vector<std::string> RendezvousServerList::addFound(const std::vector<std::string> &named)
{
	std::vector<std::string> added;
	for (const std::string &text : named)
	{
		if (add(text))
			added.push_back(text);
	}
	return added;
}

std::string RendezvousServerList::foundFile(std::string_view oldText, const std::vector<std::string> &added)
{
	std::vector<std::string> found;
	const auto keep = [&found](std::string_view text) {
		if (isServerText(text) && std::find(found.begin(), found.end(), text) == found.end())
			found.emplace_back(text);
	};
	forEachLine(oldText, keep);
	for (const std::string &text : added)
		keep(text);
	if (found.size() > MAX_FOUND)
		found.erase(found.begin(), found.end() - static_cast<std::ptrdiff_t>(MAX_FOUND));
	std::string out = "# rendezvous servers named by other servers\n";
	for (const std::string &text : found)
		out += text + "\n";
	return out;
}

std::string RendezvousServerList::withServer(std::string_view fileText, std::string_view server)
{
	bool listed = false;
	forEachLine(fileText, [&](std::string_view line) { listed = listed || line == server; });
	std::string out(fileText);
	if (listed)
		return out;
	if (!out.empty() && out.back() != '\n')
		out += '\n';
	out.append(server);
	out += '\n';
	return out;
}

std::string RendezvousServerList::withoutServer(std::string_view fileText, std::string_view server)
{
	std::string out;
	while (!fileText.empty())
	{
		const std::size_t end = fileText.find('\n');
		const std::string_view line = fileText.substr(0, end);
		if (trimmed(line) != server)
		{
			out.append(line);
			if (end != std::string_view::npos)
				out += '\n';
		}
		if (end == std::string_view::npos)
			break;
		fileText.remove_prefix(end + 1);
	}
	return out;
}

} // namespace Engine
