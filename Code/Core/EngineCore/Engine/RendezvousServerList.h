/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// RendezvousServerList.h -- the rendezvous servers a player knows, in the order they are asked.
//
// Entries are "host[:port]" as players type them. Lists come from the command line, text files
// (one server per line, '#' starts a comment) and from servers naming their neighbours; the file
// of servers found that way keeps the newest ones.
//
// No engine header: tested by EngineTests.

#pragma once

#ifndef ENGINE_RENDEZVOUSSERVERLIST_H
#define ENGINE_RENDEZVOUSSERVERLIST_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Engine
{

class RendezvousServerList
{
public:
	static constexpr std::size_t MAX_SERVERS = 128;
	static constexpr std::size_t MAX_FOUND = 64;

	/// An address or host name with an optional port that fits a rendezvous message.
	static bool isServerText(std::string_view text);

	/// Adds a valid entry that is not listed yet; false otherwise (also when the list is full).
	bool add(std::string_view text);
	/// Adds every valid line of a server file.
	void addLines(std::string_view fileText);
	/// Adds servers other servers named; returns those that were new.
	std::vector<std::string> addFound(const std::vector<std::string> &named);

	/// Takes an entry out; false if it was not listed.
	bool remove(std::string_view text);

	bool contains(std::string_view text) const;
	const std::vector<std::string> &servers() const { return m_servers; }
	bool empty() const { return m_servers.empty(); }
	void clear() { m_servers.clear(); }

	/// The found-servers file after adding `added`: its old entries and the new ones, the newest
	/// MAX_FOUND, below a comment line.
	static std::string foundFile(std::string_view oldText, const std::vector<std::string> &added);

	/// The player's own server file with `server` appended (unchanged if listed already); comments stay.
	static std::string withServer(std::string_view fileText, std::string_view server);
	/// The player's own server file without the lines naming `server`; comments stay.
	static std::string withoutServer(std::string_view fileText, std::string_view server);

private:
	std::vector<std::string> m_servers;
};

} // namespace Engine

#endif // ENGINE_RENDEZVOUSSERVERLIST_H
