/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////


#pragma once
#ifndef __NETWORKUTIL_H
#define __NETWORKUTIL_H

#include "Engine/LocalNetworks.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/NetworkInterface.h"

UnsignedInt ResolveIP(AsciiString host);
/// IPv4 address of a host name (host byte order); FALSE if the name does not resolve to one.
Bool ResolveHostIPv4(const char *host, UnsignedInt &ip);

namespace Engine { class NetAddressBook; }
/// Identities versus network addresses, shared by all transports (see Engine/NetAddressBook.h).
::Engine::NetAddressBook &TheNetAddressBook();
/// Frees what the network code keeps in statics (shared packet, address book); for the end of the program.
void ReleaseNetworkStatics();
/// IPv6 address of a host name as a registered identity handle; FALSE if there is none.
Bool ResolveHostIPv6(const char *host, UnsignedInt &handle);
/// The IPv4 networks of the adapters that are up, private ones first (Engine::usableLanNetworks).
std::vector<::Engine::LocalNetwork> LocalIPv4Networks();
/// The global IPv6 address to give to players on the internet (Family::None if there is none): a
/// stable one before a temporary one, which the system changes from day to day.
::Engine::NetAddress PublishedIPv6Address();

/// -netzLokal: highest instance number; instance N uses the loopback address 127.0.0.N.
constexpr Int MAX_LOCAL_NET_INSTANCES = 8;
/// Loopback address of this instance with -netzLokal, 0 without.
UnsignedInt GetLocalNetInstanceIP();
/// Loopback address of instance n (1..MAX_LOCAL_NET_INSTANCES).
inline UnsignedInt LocalNetInstanceIP(Int n) { return 0x7F000000u | static_cast<UnsignedInt>(n); }
UnsignedShort GenerateNextCommandID();
Bool DoesCommandRequireACommandID(NetCommandType type);
Bool CommandRequiresAck(NetCommandMsg *msg);
Bool CommandRequiresDirectSend(NetCommandMsg *msg);
Bool IsCommandSynchronized(NetCommandType type);
AsciiString GetAsciiNetCommandType(NetCommandType type);

#ifdef DEBUG_LOGGING
extern "C" {
void dumpBufferToLog(const void *vBuf, Int len, const char *fname, Int line);
};
#define LOGBUFFER(buf, len) dumpBufferToLog(buf, len, __FILE__, __LINE__)
#else
#define LOGBUFFER(buf, len) {}
#endif // DEBUG_LOGGING

#endif
