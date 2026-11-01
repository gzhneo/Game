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

#ifndef __NETPACKET_H
#define __NETPACKET_H

#include "NetworkDefs.h"
#include "GameNetwork/NetCommandList.h"
#include "Common/MessageStream.h"
#include "Common/GameMemory.h"

class NetPacket;

using NetPacketList = std::list<NetPacket *>;
using NetPacketListIter = std::list<NetPacket *>::iterator;

class NetPacket : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(NetPacket, "NetPacket")		
public:
	NetPacket();
	NetPacket(TransportMessage *msg);
	//virtual ~NetPacket();

	void init();
	void reset();
	void setAddress(Int addr, Int port);
	Bool addCommand(NetCommandRef *msg);
	Int getNumCommands();

	NetCommandList *getCommandList();

	static NetCommandRef * ConstructNetCommandMsgFromRawData(UnsignedByte *data, UnsignedInt dataLength);
	static NetPacketList ConstructBigCommandPacketList(NetCommandRef *ref);

	// Checks that at least "need" bytes from position i lie within the received packet length before a
	// field is read. Packet data comes from the network, so a malformed or truncated packet must not
	// cause reads past the end of the buffer.
	static Bool PacketHasBytes(Int i, UnsignedInt need, UnsignedInt dataLength);

	UnsignedByte *getData();
	Int getLength();
	UnsignedInt getAddr();
	UnsignedShort getPort();

	void dumpPacketToLog();

protected:
	// The command layouts (header fields and data of each command type) are in NetPacket.cpp.
	UnsignedInt writePackedHeader(NetCommandRef *ref, const char *fields, Bool apply);
	Bool isRepeat(NetCommandRef *ref);

	template <typename T>
	void put(T value)
	{
		memcpy(m_packet + m_packetLen, &value, sizeof(value));
		m_packetLen += sizeof(value);
	}

protected:
	UnsignedByte		m_packet[MAX_PACKET_SIZE];
	Int							m_packetLen;
	UnsignedInt			m_addr;
	Int							m_numCommands;
	NetCommandRef*	m_lastCommand;
	UnsignedInt			m_lastFrame;
	UnsignedShort		m_port;
	UnsignedShort		m_lastCommandID;
	UnsignedByte		m_lastPlayerID;
	UnsignedByte		m_lastCommandType;
	UnsignedByte		m_lastRelay;
};

#endif // __NETPACKET_H
