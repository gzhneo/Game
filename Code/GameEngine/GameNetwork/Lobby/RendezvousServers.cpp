/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/Lobby/RendezvousServers.h"

#include "Common/GlobalData.h"
#include "Engine/InviteCode.h"
#include "Engine/NetAddressBook.h"
#include "GameNetwork/Lobby/LobbyTiming.h"
#include "GameNetwork/networkutil.h"

#include <cstdio>

namespace Lobby
{

namespace
{
	constexpr char PROJECT_LIST[] = "ProjectData\\Network\\Rendezvous.txt";

	std::string readFile( const AsciiString &path )
	{
		std::string text;
		FILE *fp = fopen(path.str(), "rb");
		if (fp == nullptr)
			return text;
		char buffer[4096];
		size_t n = 0;
		while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0 && text.size() < 64 * 1024)
			text.append(buffer, n);
		fclose(fp);
		return text;
	}

	AsciiString userFile( const char *name )
	{
		AsciiString path;
		path.format("%s%s", TheGlobalData->getPath_UserData().str(), name);
		return path;
	}
}

void RendezvousServers::load( void )
{
	if (m_fixed)
		return;
	m_list.clear();
	m_resolved.clear();
	if (TheGlobalData == nullptr || TheGlobalData->m_netNoRendezvous)
		return;
	AsciiString commandLine = TheGlobalData->m_netRendezvous;
	AsciiString entry;
	while (commandLine.nextToken(&entry, ";"))
	{
		entry.trim();
		m_list.add(entry.str());
	}
	m_list.addLines(readFile(userFile("Rendezvous.txt")));
	m_list.addLines(readFile(AsciiString(PROJECT_LIST)));
	m_list.addLines(readFile(userFile("RendezvousFound.txt")));
}

namespace
{
	Bool writeFile( const AsciiString &path, const std::string &text )
	{
		FILE *fp = fopen(path.str(), "wb");
		if (fp == nullptr)
			return FALSE;
		const Bool ok = fwrite(text.data(), 1, text.size(), fp) == text.size();
		fclose(fp);
		return ok;
	}
}

RendezvousServers::Change RendezvousServers::addOwn( const std::string &server )
{
	if (!::Engine::RendezvousServerList::isServerText(server))
		return Change::Invalid;
	if (m_list.contains(server))
		return Change::Unchanged;
	if (m_fixed || TheGlobalData == nullptr)
	{
		m_list.add(server);
		return Change::Done;
	}
	const AsciiString path = userFile("Rendezvous.txt");
	if (!writeFile(path, ::Engine::RendezvousServerList::withServer(readFile(path), server)))
		return Change::Unchanged;
	load();
	return Change::Done;
}

RendezvousServers::Change RendezvousServers::removeOwn( const std::string &server )
{
	if (!m_list.contains(server))
		return Change::Unchanged;
	if (m_fixed || TheGlobalData == nullptr)
	{
		m_list.remove(server);
		return Change::Done;
	}
	const AsciiString path = userFile("Rendezvous.txt");
	const std::string before = readFile(path);
	const std::string after = ::Engine::RendezvousServerList::withoutServer(before, server);
	if (after == before)
		return Change::NotOwn;
	if (!writeFile(path, after))
		return Change::Unchanged;
	load();
	return Change::Done;
}

void RendezvousServers::setFixed( const std::vector<std::string> &servers )
{
	m_fixed = TRUE;
	m_list.clear();
	m_resolved.clear();
	for (const std::string &text : servers)
		m_list.add(text);
}

Bool RendezvousServers::resolve( const std::string &text, ::Engine::NetAddress &out, Bool withIPv6 )
{
	for (const auto &[known, address] : m_resolved)
	{
		if (known == text)
		{
			out = address;
			return TRUE;
		}
	}
	const ::Engine::NetTarget target = ::Engine::parseNetTarget(text);
	::Engine::NetAddress address;
	if (target.kind == ::Engine::NetTarget::Kind::Address)
	{
		address = target.address;
	}
	else if (target.kind == ::Engine::NetTarget::Kind::HostName)
	{
		UnsignedInt ip = 0;
		UnsignedInt handle = 0;
		if (ResolveHostIPv4(target.hostName.c_str(), ip) && ip != 0)
			address = ::Engine::NetAddress::fromIPv4(ip);
		else if (withIPv6 && ResolveHostIPv6(target.hostName.c_str(), handle))
			TheNetAddressBook().addressOf(handle, address);
	}
	if (!(address.isV4() || address.isV6()) || (address.isV6() && !withIPv6))
		return FALSE;
	address.port = target.port != 0 ? target.port : ::Engine::RENDEZVOUS_DEFAULT_PORT;
	if (m_resolved.size() >= Limit::RESOLVED_SERVERS)
		m_resolved.erase(m_resolved.begin());
	m_resolved.emplace_back(text, address);
	out = address;
	return TRUE;
}

Bool RendezvousServers::isKnown( const ::Engine::NetAddress &from ) const
{
	for (const auto &[text, address] : m_resolved)
		if (address == from)
			return TRUE;
	return FALSE;
}

void RendezvousServers::addFound( const std::vector<std::string> &named )
{
	const std::vector<std::string> added = m_list.addFound(named);
	if (added.empty() || m_fixed || TheGlobalData == nullptr)
		return;
	const AsciiString path = userFile("RendezvousFound.txt");
	const std::string text = ::Engine::RendezvousServerList::foundFile(readFile(path), added);
	writeFile(path, text);
}

} // namespace Lobby
